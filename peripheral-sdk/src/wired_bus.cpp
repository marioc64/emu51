// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#include "emu51/wired_bus.h"
#include <algorithm>
#include <memory>
#include <new>
#include <string>
#include <vector>

struct e51p_wired_bus {
    const e51p_api* api;
    e51p_context* context;
    e51p_initiator_pins cpu{};
    uint32_t bits;
    e51p_snapshot* snapshot = nullptr;
    struct Target {
        std::string id;
        e51p_memory* memory = nullptr;
        e51p_endpoint legacy = E51P_INVALID_ID;
        e51p_bus_pins pins{};
        bool cs_low, oe_low, we_low;
        uint32_t address = 0, value = 0;
        bool reading = false, writing = false;
    };
    struct Decoder {
        std::unique_ptr<e51p_decoder, decltype(&e51p_decoder_destroy)> model{nullptr, e51p_decoder_destroy};
        e51p_endpoint input;
        std::vector<e51p_endpoint> outputs;
        std::vector<uint8_t> active_low;
    };
    std::vector<Target> targets;
    std::vector<Decoder> decoders;
    std::vector<size_t> decoder_order;
    std::string error;
    uint32_t last_address = 0;
    ~e51p_wired_bus() {
        e51p_snapshot_destroy(snapshot);
        for (auto& t : targets) e51p_memory_release(t.memory);
    }
};
namespace {
uint32_t mask(uint32_t bits) { return bits == 32 ? UINT32_MAX : (UINT32_C(1) << bits) - 1; }
template<class F> e51p_status protect(F&& f) {
    try { return f(); }
    catch (const std::bad_alloc&) { return E51P_NO_MEMORY; }
    catch (...) { return E51P_DEVICE_ERROR; }
}
e51p_status device(e51p_wired_bus& b, const std::string& id, e51p_device& handle) {
    e51p_device_desc d{}; d.struct_size = sizeof(d); d.instance_id = id.c_str();
    return b.api->add_device(b.context, &d, &handle);
}
e51p_status pin(e51p_wired_bus& b, e51p_device device, const char* name,
    uint32_t direction, uint32_t bits, uint32_t pull, e51p_endpoint& endpoint, uint32_t down = 0) {
    const e51p_endpoint_desc d{sizeof(d), name, E51P_DIGITAL, direction, bits, 0, pull, down};
    return b.api->add_endpoint(b.context, device, &d, &endpoint);
}
e51p_status add_target(e51p_wired_bus& b, const char* id, e51p_memory* memory,
    e51p_endpoint legacy, uint32_t bits, uint32_t cs, uint32_t oe, uint32_t we,
    e51p_bus_pins* pins, uint32_t* target) {
    if (!id || !*id || !pins || !target || bits > 32 || cs > 1 || oe > 1 || we > 1 || b.snapshot)
        return E51P_INVALID_ARGUMENT;
    e51p_wired_bus::Target t{};
    t.id = id; t.cs_low = cs != 0; t.oe_low = oe != 0; t.we_low = we != 0;
    t.memory = memory; t.legacy = legacy;
    e51p_device d;
    auto status = device(b, t.id + "-bus-pins", d);
    if (status != E51P_OK) return status;
    t.pins.address = E51P_INVALID_ID;
    if (bits && (status = pin(b,d,"ADDR",E51P_INPUT,bits,0,t.pins.address)) != E51P_OK) return status;
    if ((status = pin(b,d,"DATA",E51P_INPUT|E51P_OUTPUT,8,0,t.pins.data)) != E51P_OK ||
        (status = pin(b,d,"CS",E51P_INPUT,1,cs,t.pins.cs,1-cs)) != E51P_OK ||
        (status = pin(b,d,"OE",E51P_INPUT,1,oe,t.pins.oe,1-oe)) != E51P_OK) return status;
    t.pins.we = E51P_INVALID_ID;
    if (!memory || e51p_memory_kind(memory) == E51P_RAM)
        if ((status = pin(b,d,"WE",E51P_INPUT,1,we,t.pins.we,1-we)) != E51P_OK) return status;
    b.targets.push_back(std::move(t));
    e51p_memory_retain(memory);
    *pins = b.targets.back().pins;
    *target = static_cast<uint32_t>(b.targets.size() - 1);
    return E51P_OK;
}
e51p_status fail(e51p_wired_bus& b, e51p_status status, const std::string& text) {
    b.error = text;
    return status;
}
e51p_status sample(e51p_wired_bus& b, e51p_endpoint pin, uint32_t& value) {
    e51p_digital_state state{};
    const auto status = e51p_snapshot_sample(b.snapshot,pin,&state);
    if (status != E51P_OK) return status;
    if (state.contention) return E51P_CONTENTION;
    if (state.floating) return E51P_INVALID_STATE;
    value = state.value;
    return E51P_OK;
}
e51p_status capture(e51p_wired_bus& b, uint32_t kind, uint32_t address, uint32_t value) {
    auto status = e51p_snapshot_begin(b.snapshot);
    if (status != E51P_OK) return status;
    e51p_snapshot_drive(b.snapshot,b.cpu.address,address,mask(b.bits),0,0);
    e51p_snapshot_drive(b.snapshot,b.cpu.data,value,kind == E51P_DATA_WRITE ? 255 : 0,0,0);
    e51p_snapshot_drive(b.snapshot,b.cpu.code_read_n,kind != E51P_CODE_READ,1,0,0);
    e51p_snapshot_drive(b.snapshot,b.cpu.data_read_n,kind != E51P_DATA_READ,1,0,0);
    e51p_snapshot_drive(b.snapshot,b.cpu.data_write_n,kind != E51P_DATA_WRITE,1,0,0);
    for (auto& t : b.targets) e51p_snapshot_drive(b.snapshot,t.pins.data,0,0,0,0);
    for (const auto index : b.decoder_order) {
        const auto& d = b.decoders[index];
        e51p_digital_state input{};
        e51p_snapshot_sample(b.snapshot,d.input,&input);
        uint32_t selected;
        const auto selection = e51p_decoder_select(d.model.get(),input,&selected);
        const bool resolved = selection == E51P_OK || selection == E51P_NOT_FOUND;
        for (uint32_t i = 0; i < d.outputs.size(); ++i) {
            const bool level = resolved && ((selected == i) != (d.active_low[i] != 0));
            e51p_snapshot_drive(b.snapshot,d.outputs[i],level,1,0,resolved ? 0 : 1);
        }
    }
    return E51P_OK;
}
}
extern "C" {
e51p_status e51p_wired_bus_create(const e51p_api* api, e51p_context* context,
    const char* id, uint32_t bits, e51p_wired_bus** result, e51p_initiator_pins* pins) {
    if (!result) return E51P_INVALID_ARGUMENT;
    *result = nullptr;
    if (!api || !context || !id || !*id || !bits || bits > 32 || !pins) return E51P_INVALID_ARGUMENT;
    return protect([&]() -> e51p_status {
        auto b = std::make_unique<e51p_wired_bus>(); b->api = api; b->context = context; b->bits = bits;
        e51p_device d;
        auto s = device(*b,id,d);
        if (s != E51P_OK) return s;
        if ((s = pin(*b,d,"ADDR",E51P_OUTPUT,bits,0,b->cpu.address)) != E51P_OK ||
            (s = pin(*b,d,"DATA",E51P_INPUT|E51P_OUTPUT,8,255,b->cpu.data)) != E51P_OK ||
            (s = pin(*b,d,"PSEN",E51P_OUTPUT,1,0,b->cpu.code_read_n)) != E51P_OK ||
            (s = pin(*b,d,"RD",E51P_OUTPUT,1,0,b->cpu.data_read_n)) != E51P_OK ||
            (s = pin(*b,d,"WR",E51P_OUTPUT,1,0,b->cpu.data_write_n)) != E51P_OK) return s;
        *pins = b->cpu;
        *result = b.release();
        return E51P_OK;
    });
}
void e51p_wired_bus_destroy(e51p_wired_bus* b) { delete b; }
e51p_status e51p_wired_bus_add_memory(e51p_wired_bus* b, const char* id, e51p_memory* m,
    uint32_t cs, uint32_t oe, uint32_t we, e51p_bus_pins* pins, uint32_t* target) {
    if (!b || !m) return E51P_INVALID_ARGUMENT;
    return protect([&] { return add_target(*b,id,m,E51P_INVALID_ID,e51p_memory_address_bits(m),cs,oe,we,pins,target); });
}
e51p_status e51p_wired_bus_add_target(e51p_wired_bus* b, const char* id, e51p_endpoint legacy,
    uint32_t bits, uint32_t cs, uint32_t oe, uint32_t we, e51p_bus_pins* pins, uint32_t* target) {
    if (!b || legacy == E51P_INVALID_ID) return E51P_INVALID_ARGUMENT;
    return protect([&] { return add_target(*b,id,nullptr,legacy,bits,cs,oe,we,pins,target); });
}
e51p_status e51p_wired_bus_add_decoder(e51p_wired_bus* b, const char* id, uint32_t bits,
    const e51p_decoder_output* outputs, uint32_t count, e51p_endpoint* input, e51p_endpoint* pins) {
    if (!b || !id || !*id || !input || (!pins && count) || b->snapshot) return E51P_INVALID_ARGUMENT;
    return protect([&]() -> e51p_status {
        e51p_wired_bus::Decoder d;
        e51p_decoder* model = nullptr;
        auto status = e51p_decoder_create(bits,outputs,count,&model);
        if (status != E51P_OK) return status;
        d.model.reset(model); d.outputs.resize(count);
        d.active_low.reserve(count);
        for (uint32_t i = 0; i < count; ++i)
            d.active_low.push_back(static_cast<uint8_t>(outputs[i].active_low));
        status = e51p_decoder_attach(b->api,b->context,id,model,&d.input,d.outputs.data(),count);
        if (status != E51P_OK) return status;
        *input = d.input;
        if (count) std::copy(d.outputs.begin(),d.outputs.end(),pins);
        b->decoders.push_back(std::move(d));
        return E51P_OK;
    });
}
e51p_status e51p_wired_bus_compile(e51p_wired_bus* b) {
    if (!b || b->snapshot) return E51P_INVALID_STATE;
    return protect([&]() -> e51p_status {
        // Find decoder dependencies from sealed electrical nets, including
        // arbitrary bit permutations, and reject combinational cycles.
        const auto n = b->decoders.size();
        std::vector<std::vector<size_t>> edges(n);
        std::vector<unsigned> degree(n,0);
        for (size_t i = 0; i < n; ++i) for (size_t j = 0; j < n; ++j) {
            bool linked = false;
            for (auto out : b->decoders[i].outputs) {
                uint32_t a;
                if (e51p_digital_net(b->context,out,0,&a) != E51P_OK) return E51P_INVALID_STATE;
                for (uint32_t bit = 0; bit < e51p_decoder_input_bits(b->decoders[j].model.get()); ++bit) {
                    uint32_t c;
                    e51p_digital_net(b->context,b->decoders[j].input,bit,&c);
                    linked |= a == c;
                }
            }
            if (linked) { edges[i].push_back(j); ++degree[j]; }
        }
        std::vector<size_t> order;
        for (size_t i = 0; i < n; ++i) if (!degree[i]) order.push_back(i);
        for (size_t cursor = 0; cursor < order.size(); ++cursor)
            for (auto j : edges[order[cursor]]) if (!--degree[j]) order.push_back(j);
        if (order.size() != n) return fail(*b,E51P_CONFLICT,"Combinational decoder cycle");
        b->decoder_order = std::move(order);
        return e51p_snapshot_create(b->context,&b->snapshot);
    });
}
e51p_status e51p_wired_bus_access(e51p_wired_bus* b, uint32_t kind, uint32_t address,
    uint32_t value, uint32_t peek, e51p_bus_result* result) {
    if (!b || !b->snapshot || !result || kind > E51P_DATA_WRITE || peek > 1 || value > 255 ||
        (address & ~mask(b->bits)) || (peek && kind == E51P_DATA_WRITE)) return E51P_INVALID_ARGUMENT;
    return protect([&]() -> e51p_status {
        b->error.clear(); *result = {255,E51P_INVALID_ID,0};
        auto status = capture(*b,kind,address,value);
        if (status != E51P_OK) return status;
        unsigned readers = 0, writers = 0;
        for (size_t i = 0; i < b->targets.size(); ++i) {
            auto& t = b->targets[i]; t.reading = t.writing = false;
            uint32_t cs = 0, oe = 0, we = t.we_low ? 1 : 0;
            const auto csStatus = sample(*b,t.pins.cs,cs);
            if (csStatus == E51P_OK && ((cs != 0) == t.cs_low)) continue;
            const auto oeStatus = sample(*b,t.pins.oe,oe);
            const auto weStatus = t.pins.we == E51P_INVALID_ID ? e51p_status{E51P_OK} : sample(*b,t.pins.we,we);
            // A disabled read and write cannot affect this transaction, even
            // when the chip-select decoder has an unresolved external input.
            if (oeStatus == E51P_OK && weStatus == E51P_OK &&
                ((oe != 0) == t.oe_low) && ((we != 0) == t.we_low)) continue;
            if (csStatus != E51P_OK) return fail(*b,csStatus,t.id + ": unresolved CS");
            if (oeStatus != E51P_OK || weStatus != E51P_OK)
                return fail(*b,oeStatus != E51P_OK ? oeStatus : weStatus,t.id + ": unresolved read/write control");
            t.reading = (oe != 0) != t.oe_low;
            t.writing = t.pins.we != E51P_INVALID_ID && ((we != 0) != t.we_low);
            if (t.reading && t.writing) return fail(*b,E51P_CONFLICT,t.id + ": output and write both enabled");
            if (!t.reading && !t.writing) continue;
            if (t.pins.address != E51P_INVALID_ID && (status = sample(*b,t.pins.address,t.address)) != E51P_OK)
                return fail(*b,status,t.id + ": unresolved address");
            if (t.memory && t.address >= e51p_memory_capacity(t.memory))
                return fail(*b,E51P_INVALID_ARGUMENT,t.id + ": physical address outside capacity");
            if (t.reading) {
                if (t.memory) { uint8_t byte = 255; status = e51p_memory_read(t.memory,t.address,&byte); t.value = byte; }
                else status = e51p_target_read(b->context,t.legacy,t.address,E51P_PEEK,b->snapshot,&t.value);
                if (status != E51P_OK) return fail(*b,status,t.id + ": cannot inspect target register");
                e51p_snapshot_drive(b->snapshot,t.pins.data,t.value,255,0,0);
                ++readers;
                result->target = static_cast<uint32_t>(i); result->physical_address = t.address;
            }
            writers += t.writing;
        }
        // A read with two active parallel targets is a configuration fault even
        // when they happen to drive identical bytes today.
        if (readers > 1) return fail(*b,E51P_CONTENTION,"Multiple enabled bus data drivers");
        if (kind == E51P_DATA_WRITE && readers) return fail(*b,E51P_CONFLICT,"Output enabled during CPU write");
        if (kind != E51P_DATA_WRITE && writers) return fail(*b,E51P_CONFLICT,"Write enabled during CPU read");
        uint32_t resolved;
        if ((status = sample(*b,b->cpu.data,resolved)) != E51P_OK)
            return fail(*b,status,"Unresolved CPU data bus");
        for (auto& t : b->targets) if (t.writing) {
            if ((status = sample(*b,t.pins.data,t.value)) != E51P_OK)
                return fail(*b,status,t.id + ": unresolved write data");
            if (!t.memory && (status = e51p_target_check_write(b->context,t.legacy,t.address,t.value,b->snapshot)) != E51P_OK)
                return fail(*b,status,t.id + ": unsupported register write");
        }
        if (!peek) {
            for (auto& t : b->targets) {
                if (t.writing) status = t.memory ? e51p_memory_write(t.memory,t.address,static_cast<uint8_t>(t.value)) :
                    e51p_target_write(b->context,t.legacy,t.address,t.value,b->snapshot);
                else if (t.reading && !t.memory) { uint32_t ignored; status = e51p_target_read(b->context,t.legacy,t.address,E51P_READ,b->snapshot,&ignored); }
                else continue;
                if (status != E51P_OK) return fail(*b,status,t.id + ": transaction commit failed");
            }
            b->last_address = address;
        }
        result->value = resolved;
        return readers || writers ? E51P_OK : E51P_NOT_FOUND;
    });
}
e51p_status e51p_wired_bus_sample(e51p_wired_bus* b, e51p_endpoint endpoint, e51p_digital_state* result) {
    if (!b || !b->snapshot || !result) return E51P_INVALID_ARGUMENT;
    const auto status = capture(*b,UINT32_MAX,b->last_address,0);
    return status == E51P_OK ? e51p_snapshot_sample(b->snapshot,endpoint,result) : status;
}
const char* e51p_wired_bus_error(const e51p_wired_bus* b) { return b ? b->error.c_str() : "Invalid bus"; }
}
