// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#include "emu51/peripheral.h"
#include "emu51/devices.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace {
const e51p_api* api = nullptr;
void check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void ok(e51p_status status) { check(status == E51P_OK, "operation succeeds"); }
struct Context {
    e51p_context* c = nullptr;
    Context() { ok(api->create(12000000, &c)); }
    ~Context() { api->destroy(c); }
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
};
e51p_device device(e51p_context* c, const char* name, e51p_device_desc desc = {}) {
    desc.struct_size = sizeof(desc); desc.instance_id = name;
    e51p_device id = E51P_INVALID_ID;
    ok(api->add_device(c, &desc, &id));
    return id;
}
e51p_endpoint port(e51p_context* c, e51p_device d, const char* name, uint32_t kind,
    uint32_t width = 8, uint32_t direction = E51P_INPUT | E51P_OUTPUT,
    uint32_t address_bits = 0, uint32_t up = 0, uint32_t down = 0) {
    e51p_endpoint_desc desc{sizeof(desc), name, kind, direction, width, address_bits, up, down};
    e51p_endpoint id = E51P_INVALID_ID;
    ok(api->add_endpoint(c, d, &desc, &id));
    return id;
}
e51p_digital_state sample(e51p_context* c, e51p_endpoint e) {
    e51p_digital_state state{}; ok(api->sample(c, e, &state)); return state;
}
void abi_and_ownership() {
    const e51p_api* result = api;
    check(e51p_get_api(999, &result) == E51P_UNSUPPORTED && !result, "unknown ABI clears output");
    check(e51p_get_api(1, nullptr) == E51P_INVALID_ARGUMENT, "null ABI output rejected");
    check(api->struct_size == sizeof(*api) && api->abi_version == 1, "ABI table metadata");
    e51p_context* invalid = reinterpret_cast<e51p_context*>(1);
    check(api->create(0, &invalid) == E51P_INVALID_ARGUMENT && !invalid, "zero clock rejected");
    check(api->create(1, nullptr) == E51P_INVALID_ARGUMENT, "null context output rejected");
    api->destroy(nullptr);
    std::vector<int> destroyed;
    struct Owned { int id; std::vector<int>* log; } first{1, &destroyed}, second{2, &destroyed};
    {
        Context context;
        e51p_device_desc desc{};
        desc.user = &first;
        desc.destroy = [](void* p) { auto& o = *static_cast<Owned*>(p); o.log->push_back(o.id); };
        std::string name = "owned";
        const auto a = device(context.c, name.c_str(), desc);
        name = "changed";
        desc.struct_size = sizeof(desc); desc.instance_id = "owned";
        e51p_device duplicate = E51P_INVALID_ID;
        check(api->add_device(context.c, &desc, &duplicate) == E51P_CONFLICT &&
            duplicate == E51P_INVALID_ID && destroyed.empty(), "ID copied; failed add retains ownership");
        desc.user = &second;
        device(context.c, "second", desc);
        const auto e = port(context.c, a, "p", E51P_DIGITAL);
        e51p_endpoint_desc endpoint_desc{sizeof(endpoint_desc), "p", E51P_DIGITAL, E51P_INPUT, 8, 0, 0, 0};
        e51p_endpoint output;
        check(api->add_endpoint(context.c, a, &endpoint_desc, &output) == E51P_CONFLICT, "duplicate endpoint rejected");
        endpoint_desc.name = "bad"; endpoint_desc.width = 33;
        check(api->add_endpoint(context.c, a, &endpoint_desc, &output) == E51P_INVALID_ARGUMENT, "invalid width rejected");
        endpoint_desc.width = 8; endpoint_desc.struct_size = 0;
        check(api->add_endpoint(context.c, a, &endpoint_desc, &output) == E51P_INVALID_ARGUMENT, "short descriptor rejected");
        check(api->drive(context.c, e, 0, 255, 0) == E51P_INVALID_STATE, "no drive before start");
        ok(api->start(context.c));
        check(api->start(context.c) == E51P_INVALID_STATE, "cannot start twice");
        check(api->add_device(context.c, &desc, &duplicate) == E51P_INVALID_STATE, "topology sealed");
        check(api->connect_digital(context.c, e, 0, e, 1, 1) == E51P_INVALID_STATE, "connections sealed");
    }
    check(destroyed == std::vector<int>({2, 1}), "cleanup once, in reverse order");
}
void digital_wiring() {
    Context context;
    auto* c = context.c;
    const auto a = device(c, "cpu"), b = device(c, "peripheral");
    const auto p = port(c, a, "P1", E51P_DIGITAL);
    const auto q = port(c, a, "P2", E51P_DIGITAL);
    const auto in = port(c, b, "data", E51P_DIGITAL, 8, E51P_INPUT);
    const auto fan = port(c, b, "fanout", E51P_DIGITAL, 4, E51P_INPUT);
    check(api->connect_digital(c, p, 7, in, 0, 2) == E51P_INVALID_ARGUMENT, "slice bounds checked before connecting");
    ok(api->connect_digital(c, p, 4, in, 0, 4));
    ok(api->connect_digital(c, q, 0, in, 4, 4));
    ok(api->connect_digital(c, in, 0, fan, 0, 4));
    ok(api->start(c));
    check(sample(c, in).floating == 255, "unconnected and undriven bits float");
    ok(api->drive(c, p, 0xa0, 0xf0, 0));
    ok(api->drive(c, q, 5, 15, 0));
    check(sample(c, in).value == 0x5a && !sample(c, in).floating, "group from two port slices");
    check(sample(c, fan).value == 0xa, "fanout and transitive connections");
    ok(api->drive(c, q, 5, 3, 0));
    check(sample(c, in).floating == 0xc0, "per-bit output enable");
    check(api->drive(c, in, 0, 1, 0) == E51P_INVALID_ARGUMENT, "cannot drive input");
    check(api->drive(c, p, 256, 0, 0) == E51P_INVALID_ARGUMENT, "out-of-width drive rejected");
    check(sample(c, fan).value == 10, "invalid drive leaves signals unchanged");
    ok(api->reset(c));
    check(sample(c, in).floating == 255, "reset releases all output drivers");
}
void electrical_resolution() {
    Context context;
    auto* c = context.c;
    auto d = device(c, "drivers");
    auto a = port(c, d, "a", E51P_DIGITAL, 32, 3, 0, UINT32_MAX);
    auto b = port(c, d, "b", E51P_DIGITAL, 32);
    ok(api->connect_digital(c, a, 0, b, 0, 32));
    ok(api->start(c));
    check(sample(c, b).value == UINT32_MAX, "32-bit pull-up resolution");
    ok(api->drive(c, b, 0, 0x80000000, 0x80000000));
    check(sample(c, a).value == 0x7fffffff, "open-drain low dominates pull-up");
    ok(api->drive(c, b, UINT32_MAX, UINT32_MAX, UINT32_MAX));
    check(sample(c, a).value == UINT32_MAX, "open-drain high releases line");
    ok(api->drive(c, a, 1, 1, 0));
    check(api->drive(c, b, 0, 1, 0) == E51P_CONTENTION, "conflicting push-pull outputs detected");
    check(sample(c, b).contention == 1 && !(sample(c, b).value & 1), "contention mask and deterministic value");
    ok(api->drive(c, b, 0, 0, 0));
    check(!sample(c, a).contention, "contention recovers when driver releases");
    {
        Context pulls;
        d = device(pulls.c, "pulls");
        a = port(pulls.c, d, "up", E51P_DIGITAL, 1, 3, 0, 1);
        b = port(pulls.c, d, "down", E51P_DIGITAL, 1, 3, 0, 0, 1);
        ok(api->connect_digital(pulls.c, a, 0, b, 0, 1));
        check(api->start(pulls.c) == E51P_CONTENTION, "opposing weak pulls reported");
        ok(api->drive(pulls.c, a, 1, 1, 0));
        check(sample(pulls.c, b).value == 1, "strong drive overrides weak pull conflict");
    }
}
struct BusDevice {
    uint32_t last_address = 0, value = 0;
    unsigned reads = 0, peeks = 0;
    e51p_endpoint drive_endpoint = E51P_INVALID_ID;
    bool try_peek_drive = false;
    e51p_status response = E51P_OK;
};
e51p_status read_bus(void* user, e51p_context* c, e51p_endpoint,
    uint32_t address, uint32_t access, uint32_t* value) {
    auto& b = *static_cast<BusDevice*>(user);
    b.last_address = address;
    if (access == E51P_PEEK) {
        ++b.peeks;
        if (b.try_peek_drive)
            check(api->drive(c, b.drive_endpoint, 0, 1, 0) == E51P_INVALID_STATE, "peek cannot drive signals");
    } else ++b.reads;
    *value = b.value;
    return b.response;
}
e51p_status write_bus(void* user, e51p_context*, e51p_endpoint, uint32_t address, uint32_t value) {
    auto& b = *static_cast<BusDevice*>(user); b.last_address = address; b.value = value;
    return E51P_OK;
}
void bus_mapping() {
    Context context;
    auto* c = context.c;
    BusDevice state;
    e51p_device_desc desc{};
    desc.user = &state; desc.bus_read = read_bus; desc.bus_write = write_bus;
    auto d = device(c, "registers", desc);
    auto a = port(c, d, "master", E51P_BUS_INITIATOR, 8, 0, 16);
    auto t = port(c, d, "slave", E51P_BUS_TARGET, 8, 0, 8);
    auto bad = port(c, d, "wrong_width", E51P_BUS_TARGET, 16, 0, 16);
    state.drive_endpoint = port(c, d, "pin", E51P_DIGITAL, 1);
    state.try_peek_drive = true;
    ok(api->map_bus(c, a, 0x8000, 4, t, 16));
    check(api->map_bus(c, a, 0x8003, 4, t, 0) == E51P_CONFLICT, "overlapping windows rejected");
    check(api->map_bus(c, a, 0xffff, 2, t, 0) == E51P_INVALID_ARGUMENT, "initiator overflow rejected");
    check(api->map_bus(c, a, 0x8004, 4, t, 254) == E51P_INVALID_ARGUMENT, "target overflow rejected");
    check(api->map_bus(c, a, 0x8004, 4, bad, 0) == E51P_INVALID_ARGUMENT, "bus width mismatch rejected");
    ok(api->map_bus(c, a, 0x8004, 4, t, 0));
    ok(api->start(c));
    ok(api->bus_write(c, a, 0x8003, 0xa5));
    check(state.last_address == 19 && state.value == 0xa5, "bus rebases target address");
    uint32_t value = 0;
    ok(api->bus_read(c, a, 0x8000, E51P_PEEK, &value));
    check(value == 0xa5 && state.peeks == 1 && state.reads == 0, "passive peek distinct from consuming read");
    ok(api->bus_read(c, a, 0x8004, E51P_READ, &value));
    check(state.last_address == 0 && state.reads == 1, "adjacent mapping remains intact after rejected map");
    value = 123;
    check(api->bus_read(c, a, 0x7fff, E51P_READ, &value) == E51P_NOT_FOUND && value == 123,
        "unmapped read leaves output unchanged for host fallback");
    check(api->bus_write(c, a, 0x8000, 256) == E51P_INVALID_ARGUMENT && state.value == 0xa5,
        "invalid bus write does not reach device");
    check(api->bus_read(c, a, 0x10000, E51P_READ, &value) == E51P_INVALID_ARGUMENT, "address width checked");
    state.value = 256;
    check(api->bus_read(c, a, 0x8000, E51P_READ, &value) == E51P_DEVICE_ERROR && value == 123,
        "out-of-width device response rejected");
    state.value = 0x5a;
    state.response = E51P_CONTENTION;
    check(api->bus_read(c, a, 0x8000, E51P_READ, &value) == E51P_CONTENTION && value == 0x5a,
        "contended device response still supplies its sampled value");
    state.response = E51P_NOT_FOUND;
    check(api->bus_read(c, a, 0x8000, E51P_READ, &value) == E51P_DEVICE_ERROR,
        "claimed target cannot request fallback to backing RAM");
    state.response = E51P_OK;
    {
        Context full;
        d = device(full.c, "wide", desc);
        a = port(full.c, d, "master", E51P_BUS_INITIATOR, 32, 0, 32);
        t = port(full.c, d, "target", E51P_BUS_TARGET, 32, 0, 32);
        ok(api->map_bus(full.c, a, 0, UINT64_C(1) << 32, t, 0));
        ok(api->start(full.c));
        ok(api->bus_write(full.c, a, UINT32_MAX, UINT32_MAX));
        ok(api->bus_read(full.c, a, UINT32_MAX, E51P_READ, &value));
        check(value == UINT32_MAX && state.last_address == UINT32_MAX, "full 32-bit bus window");
    }
}
void callbacks_and_time() {
    Context context;
    auto* c = context.c;
    struct Relay { e51p_endpoint out; uint64_t time = 0; unsigned changes = 0; } relay{};
    e51p_device_desc desc{};
    desc.user = &relay;
    desc.digital_changed = [](void* user, e51p_context* context, e51p_endpoint, e51p_digital_state state, uint64_t time) {
        auto& r = *static_cast<Relay*>(user); ++r.changes; r.time = time;
        ok(api->drive(context, r.out, state.value, 255, 0));
        check(api->advance_to(context, time) == E51P_INVALID_STATE, "time cannot reenter from callback");
    };
    auto d = device(c, "relay", desc), h = device(c, "host");
    auto in = port(c, d, "in", E51P_DIGITAL, 8, E51P_INPUT);
    relay.out = port(c, d, "out", E51P_DIGITAL, 8, E51P_OUTPUT);
    auto source = port(c, h, "source", E51P_DIGITAL);
    auto sink = port(c, h, "sink", E51P_DIGITAL, 8, E51P_INPUT);
    ok(api->connect_digital(c, source, 0, in, 0, 8));
    ok(api->connect_digital(c, relay.out, 0, sink, 0, 8));
    ok(api->start(c));
    const uint64_t large = (UINT64_C(1) << 40) + 7;
    ok(api->advance_to(c, large));
    ok(api->drive(c, source, 0x5a, 255, 0));
    check(sample(c, sink).value == 0x5a && relay.time == large, "callback-driven propagation settles at same timestamp");
    const auto changes = relay.changes;
    sample(c, sink); sample(c, sink);
    ok(api->drive(c, source, 0x5a, 255, 0));
    check(relay.changes == changes, "inspection and identical drive produce no events");
    check(api->advance_to(c, large - 1) == E51P_INVALID_ARGUMENT, "time is monotonic");
    uint64_t time, hz;
    ok(api->clock(c, &time, &hz));
    check(time == large && hz == 12000000, "64-bit clock and immutable frequency");
    ok(api->reset(c)); ok(api->clock(c, &time, &hz));
    check(time == 0 && hz == 12000000, "reset starts new epoch retaining tick rate");
}
void analog_signals() {
    Context context;
    auto* c = context.c;
    struct Analog { double value = 0; uint64_t time = 0; unsigned changes = 0; } observed;
    e51p_device_desc desc{};
    desc.user = &observed;
    desc.analog_changed = [](void* p, e51p_context*, e51p_endpoint, double value, uint64_t time) {
        auto& a = *static_cast<Analog*>(p); a.value = value; a.time = time; ++a.changes;
    };
    auto d = device(c, "analog", desc);
    auto out = port(c, d, "source", E51P_ANALOG, 1, E51P_OUTPUT);
    auto out2 = port(c, d, "other", E51P_ANALOG, 1, E51P_OUTPUT);
    auto in = port(c, d, "adc", E51P_ANALOG, 1, E51P_INPUT);
    auto fan = port(c, d, "meter", E51P_ANALOG, 1, E51P_INPUT);
    ok(api->connect_analog(c, out, in));
    ok(api->connect_analog(c, out, fan));
    check(api->connect_analog(c, out2, in) == E51P_CONFLICT, "analog input has one source");
    check(api->connect_analog(c, in, out2) == E51P_INVALID_ARGUMENT, "analog direction enforced");
    ok(api->start(c));
    double volts = 123;
    check(api->analog_read(c, in, &volts) == E51P_NOT_FOUND && volts == 123, "unset analog source is explicitly absent");
    ok(api->advance_to(c, 120));
    ok(api->analog_write(c, out, 2.5));
    ok(api->analog_read(c, fan, &volts));
    check(volts == 2.5 && observed.changes == 2 && observed.time == 120, "analog fanout has voltage and simulation timestamp");
    check(api->analog_write(c, out, std::numeric_limits<double>::quiet_NaN()) == E51P_INVALID_ARGUMENT,
        "nonfinite analog sample rejected");
    ok(api->analog_write(c, out, 2.5));
    check(observed.changes == 2, "identical voltage does not produce extra changes");
    ok(api->reset(c));
    check(api->analog_read(c, in, &volts) == E51P_NOT_FOUND, "reset invalidates analog samples");
}
void faults() {
    Context context;
    auto* c = context.c;
    struct Loop { e51p_endpoint pin; bool enabled = true; } loop{};
    e51p_device_desc desc{};
    desc.user = &loop;
    desc.digital_changed = [](void* p, e51p_context* context, e51p_endpoint, e51p_digital_state state, uint64_t) {
        auto& l = *static_cast<Loop*>(p);
        if (l.enabled) ok(api->drive(context, l.pin, state.value ^ 1, 1, 0));
    };
    auto d = device(c, "loop", desc);
    loop.pin = port(c, d, "pin", E51P_DIGITAL, 1);
    check(api->start(c) == E51P_OSCILLATION, "feedback loop has bounded propagation");
    check(api->advance_to(c, 10) == E51P_INVALID_STATE, "oscillating graph faults");
    loop.enabled = false;
    ok(api->reset(c)); ok(api->advance_to(c, 10));
    {
        Context throwing;
        e51p_device_desc bad{};
        bad.advance = [](void*, e51p_context*, uint64_t, uint64_t) { throw 1; };
        device(throwing.c, "throw", bad);
        ok(api->start(throwing.c));
        check(api->advance_to(throwing.c, 1) == E51P_DEVICE_ERROR, "device exceptions do not cross C ABI");
        check(api->advance_to(throwing.c, 2) == E51P_INVALID_STATE, "throwing device faults graph");
    }
    {
        Context allocation;
        e51p_device_desc bad{};
        bad.advance = [](void*, e51p_context*, uint64_t, uint64_t) { throw std::bad_alloc(); };
        device(allocation.c, "allocation", bad);
        ok(api->start(allocation.c));
        check(api->advance_to(allocation.c, 1) == E51P_NO_MEMORY, "allocation failure contained");
        check(api->advance_to(allocation.c, 2) == E51P_INVALID_STATE, "allocation failure in callback faults graph");
    }
}
void parallel_dac() {
    Context context;
    auto* c = context.c;
    const auto host = device(c, "host");
    const auto source = port(c, host, "data", E51P_DIGITAL, 8, E51P_OUTPUT);
    const auto conflict = port(c, host, "other", E51P_DIGITAL, 8, E51P_OUTPUT);
    const auto sink = port(c, host, "voltage", E51P_ANALOG, 1, E51P_INPUT);
    e51p_endpoint data = E51P_INVALID_ID, voltage = E51P_INVALID_ID;
    check(e51p_add_parallel_dac(api, c, "invalid", 0, &data, &voltage) == E51P_INVALID_ARGUMENT &&
        data == E51P_INVALID_ID && voltage == E51P_INVALID_ID, "DAC validates reference before graph mutation");
    check(e51p_add_parallel_dac(api, c, "invalid", std::numeric_limits<double>::infinity(), &data, &voltage)
        == E51P_INVALID_ARGUMENT, "DAC rejects infinite reference");
    ok(e51p_add_parallel_dac(api, c, "covox", 5.0, &data, &voltage));
    ok(api->connect_digital(c, source, 0, data, 0, 8));
    ok(api->connect_digital(c, conflict, 0, data, 0, 8));
    ok(api->connect_analog(c, voltage, sink));
    ok(api->start(c));
    double value = 123;
    ok(api->analog_read(c, sink, &value));
    check(value == 0, "DAC starts at zero with floating inputs");
    for (uint32_t code = 0; code < 256; ++code) {
        ok(api->drive(c, source, code, 255, 0));
        ok(api->analog_read(c, sink, &value));
        check(value == code * 5.0 / 256.0, "every DAC code maps to specified voltage");
    }
    ok(api->drive(c, source, 0, 0, 0));
    ok(api->analog_read(c, sink, &value));
    check(value == 255 * 5.0 / 256.0, "undefined DAC input retains previous voltage");
    ok(api->drive(c, source, 128, 255, 0));
    check(api->drive(c, conflict, 0, 255, 0) == E51P_CONTENTION, "DAC sees electrical conflict");
    ok(api->analog_read(c, sink, &value));
    check(value == 2.5, "contended DAC input retains previous voltage");
    ok(api->reset(c));
    ok(api->analog_read(c, sink, &value));
    check(value == 0, "DAC resets voltage and releases external drivers");
}
}
int main() {
    ok(e51p_get_api(E51P_ABI_VERSION, &api));
    abi_and_ownership(); digital_wiring(); electrical_resolution(); bus_mapping();
    callbacks_and_time(); analog_signals(); faults(); parallel_dac();
    std::puts("Peripheral ABI, ownership, wiring, electrical, bus, time, analog and fault checks passed.");
}
