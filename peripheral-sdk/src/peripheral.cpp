// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#include "emu51/peripheral.h"
#include "emu51/snapshot.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <string>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace {
struct Device { std::string id; e51p_device_desc desc; };
struct Endpoint {
    std::string name;
    e51p_device owner;
    e51p_endpoint_desc desc;
    uint32_t first_pin = 0;
    uint32_t value = 0, enable = 0, open_drain = 0;
    e51p_digital_state state{};
    bool notified = false, changed = false;
    double analog = 0;
    bool analog_valid = false;
    e51p_endpoint source = E51P_INVALID_ID;
    e51p_write_validator validate_write = nullptr;
    void* validator_user = nullptr;
};
struct Pin {
    uint32_t parent;
    bool high = false, low = false, up = false, down = false;
    uint32_t high_count = 0, low_count = 0;
};
struct Mapping {
    e51p_endpoint initiator, target;
    uint32_t base, target_base;
    uint64_t size;
};
uint32_t mask(uint32_t width) {
    return width == 32 ? UINT32_MAX : (UINT32_C(1) << width) - 1;
}
uint32_t first_set_bit(uint32_t bits) {
#if defined(_MSC_VER)
    unsigned long index;
    _BitScanForward(&index, bits);
    return static_cast<uint32_t>(index);
#else
    return static_cast<uint32_t>(__builtin_ctz(bits));
#endif
}
bool equal(e51p_digital_state a, e51p_digital_state b) {
    return a.value == b.value && a.floating == b.floating && a.contention == b.contention;
}
}

struct e51p_context {
    uint64_t tick_hz = 0, time = 0;
    uint64_t settled_revision = 0;
    bool started = false, busy = false, dirty = false, fault = false, peek = false;
    std::vector<Device> devices;
    std::vector<Endpoint> endpoints;
    std::vector<Pin> pins;
    std::vector<Mapping> mappings;
    const e51p_snapshot* inspection = nullptr;
};

struct e51p_snapshot {
    const e51p_context* context;
    struct Net { uint32_t high = 0, low = 0, undefined = 0; bool up = false, down = false; };
    struct Drive { uint32_t value = 0, enable = 0, drain = 0, undefined = 0; };
    std::vector<uint32_t> roots;
    std::vector<Net> nets;
    std::vector<Drive> drives;
    std::vector<Net> live_nets;
    std::vector<Drive> live_drives;
    uint64_t live_revision = 0;
};

namespace {
/* Every status-returning exported entry point catches allocation and device
 * exceptions. Device callbacks are contractually non-throwing; an exception
 * faults the context because the device may have partially changed state. */
template<class F> e51p_status protect(e51p_context* c, F&& function) noexcept {
    try { return function(); }
    catch (const std::bad_alloc&) { if (c && c->started) c->fault = true; return E51P_NO_MEMORY; }
    catch (...) { if (c) c->fault = true; return E51P_DEVICE_ERROR; }
}
struct Busy {
    e51p_context& c;
    explicit Busy(e51p_context& context) : c(context) { c.busy = true; }
    ~Busy() { c.busy = false; c.peek = false; }
};
bool configuring(const e51p_context* c) { return c && !c->started && !c->busy; }
bool live(const e51p_context* c) { return c && c->started && !c->fault; }
bool endpoint(const e51p_context* c, e51p_endpoint id, uint32_t kind) {
    return c && id < c->endpoints.size() && c->endpoints[id].desc.kind == kind;
}
uint32_t root(e51p_context& c, uint32_t pin) {
    while (c.pins[pin].parent != pin) {
        c.pins[pin].parent = c.pins[c.pins[pin].parent].parent;
        pin = c.pins[pin].parent;
    }
    return pin;
}
bool contention(const e51p_context& c) {
    for (const auto& e : c.endpoints) if (e.state.contention) return true;
    return false;
}

e51p_status settle(e51p_context& c) {
    /* Evaluate all nets before delivering callbacks. Callback writes are
     * evaluated in the next delta round, never recursively. */
    for (unsigned round = 0; c.dirty; ++round) {
        if (round == 1024) { c.fault = true; return E51P_OSCILLATION; }
        c.dirty = false;
        ++c.settled_revision;
        for (auto& p : c.pins) {
            p.high = p.low = p.up = p.down = false;
            p.high_count = p.low_count = 0;
        }
        for (const auto& e : c.endpoints) {
            if (e.desc.kind != E51P_DIGITAL) continue;
            for (uint32_t bit = 0; bit < e.desc.width; ++bit) {
                const uint32_t flag = UINT32_C(1) << bit;
                auto& p = c.pins[root(c, e.first_pin + bit)];
                p.up |= (e.desc.pull_up & flag) != 0;
                p.down |= (e.desc.pull_down & flag) != 0;
                if (!(e.enable & flag)) continue;
                if (!(e.value & flag)) { p.low = true; ++p.low_count; }
                else if (!(e.open_drain & flag)) { p.high = true; ++p.high_count; }
            }
        }
        for (auto& e : c.endpoints) {
            e.changed = false;
            if (e.desc.kind == E51P_DIGITAL) {
                e51p_digital_state next{};
                for (uint32_t bit = 0; bit < e.desc.width; ++bit) {
                    const uint32_t flag = UINT32_C(1) << bit;
                    const auto& p = c.pins[root(c, e.first_pin + bit)];
                    const bool strong = p.high || p.low;
                    const bool conflict = strong ? p.high && p.low : p.up && p.down;
                    if (conflict) next.contention |= flag;
                    else if (strong ? p.high : p.up) next.value |= flag;
                    else if (!strong && !p.down) next.floating |= flag;
                }
                e.changed = !e.notified || !equal(e.state, next);
                e.state = next;
                e.notified = true;
            } else if (e.desc.kind == E51P_ANALOG && e.source != E51P_INVALID_ID) {
                const auto& source = c.endpoints[e.source];
                e.changed = source.analog_valid && (!e.analog_valid || e.analog != source.analog);
                e.analog = source.analog;
                e.analog_valid = source.analog_valid;
            }
        }
        for (e51p_endpoint id = 0; id < c.endpoints.size(); ++id) {
            const auto& e = c.endpoints[id];
            if (!e.changed || !(e.desc.direction & E51P_INPUT)) continue;
            const auto& d = c.devices[e.owner].desc;
            if (e.desc.kind == E51P_DIGITAL && d.digital_changed)
                d.digital_changed(d.user, &c, id, e.state, c.time);
            if (e.desc.kind == E51P_ANALOG && d.analog_changed)
                d.analog_changed(d.user, &c, id, e.analog, c.time);
        }
    }
    return contention(c) ? E51P_CONTENTION : E51P_OK;
}

e51p_status create(uint64_t hz, e51p_context** result) noexcept {
    if (!result) return E51P_INVALID_ARGUMENT;
    *result = nullptr;
    if (!hz) return E51P_INVALID_ARGUMENT;
    return protect(nullptr, [&]() -> e51p_status {
        auto* c = new e51p_context;
        c->tick_hz = hz;
        *result = c;
        return E51P_OK;
    });
}
void destroy(e51p_context* c) noexcept {
    if (!c || c->busy) return;
    c->busy = true;
    for (auto it = c->devices.rbegin(); it != c->devices.rend(); ++it) {
        try { if (it->desc.destroy) it->desc.destroy(it->desc.user); }
        catch (...) { /* Continue releasing other instances; never cross the C ABI. */ }
    }
    delete c;
}
e51p_status add_device(e51p_context* c, const e51p_device_desc* desc, e51p_device* result) noexcept {
    if (!configuring(c)) return E51P_INVALID_STATE;
    if (!desc || !result || desc->struct_size < sizeof(*desc) || !desc->instance_id || !*desc->instance_id)
        return E51P_INVALID_ARGUMENT;
    return protect(c, [&]() -> e51p_status {
        for (const auto& d : c->devices) if (d.id == desc->instance_id) return E51P_CONFLICT;
        if (c->devices.size() >= E51P_INVALID_ID) return E51P_NO_MEMORY;
        Device d{desc->instance_id, *desc};
        d.desc.instance_id = nullptr; // owned string is stored separately
        c->devices.push_back(std::move(d));
        *result = static_cast<e51p_device>(c->devices.size() - 1);
        return E51P_OK;
    });
}
e51p_status add_endpoint(e51p_context* c, e51p_device device,
    const e51p_endpoint_desc* desc, e51p_endpoint* result) noexcept {
    if (!configuring(c)) return E51P_INVALID_STATE;
    if (!desc || !result || desc->struct_size < sizeof(*desc) || device >= c->devices.size() ||
        !desc->name || !*desc->name || !desc->width || desc->width > 32) return E51P_INVALID_ARGUMENT;
    const bool digital = desc->kind == E51P_DIGITAL;
    const bool analog = desc->kind == E51P_ANALOG;
    const bool bus = desc->kind == E51P_BUS_INITIATOR || desc->kind == E51P_BUS_TARGET;
    if ((!digital && !analog && !bus) ||
        ((digital || analog) && (!desc->direction || (desc->direction & ~UINT32_C(3)))) ||
        (analog && (desc->width != 1 || desc->direction == (E51P_INPUT | E51P_OUTPUT))) ||
        (bus && (desc->direction || !desc->address_bits || desc->address_bits > 32)) ||
        (!bus && desc->address_bits) || (!digital && (desc->pull_up || desc->pull_down)) ||
        ((desc->pull_up | desc->pull_down) & ~mask(desc->width)) ||
        (desc->pull_up & desc->pull_down)) return E51P_INVALID_ARGUMENT;
    return protect(c, [&]() -> e51p_status {
        for (const auto& e : c->endpoints)
            if (e.owner == device && e.name == desc->name) return E51P_CONFLICT;
        if (c->endpoints.size() >= E51P_INVALID_ID ||
            (digital && c->pins.size() > E51P_INVALID_ID - desc->width)) return E51P_NO_MEMORY;
        Endpoint e{desc->name, device, *desc};
        e.desc.name = nullptr;
        e.first_pin = static_cast<uint32_t>(c->pins.size());
        // Reserve both containers before committing anything.
        c->endpoints.reserve(c->endpoints.size() + 1);
        if (digital) c->pins.reserve(c->pins.size() + desc->width);
        c->endpoints.push_back(std::move(e));
        if (digital) for (uint32_t bit = 0; bit < desc->width; ++bit)
            c->pins.push_back(Pin{static_cast<uint32_t>(c->pins.size())});
        *result = static_cast<e51p_endpoint>(c->endpoints.size() - 1);
        return E51P_OK;
    });
}
e51p_status connect_digital(e51p_context* c, e51p_endpoint a, uint32_t a_lsb,
    e51p_endpoint b, uint32_t b_lsb, uint32_t width) noexcept {
    if (!configuring(c)) return E51P_INVALID_STATE;
    if (!endpoint(c, a, E51P_DIGITAL) || !endpoint(c, b, E51P_DIGITAL) || !width ||
        a_lsb >= c->endpoints[a].desc.width || b_lsb >= c->endpoints[b].desc.width ||
        width > c->endpoints[a].desc.width - a_lsb || width > c->endpoints[b].desc.width - b_lsb)
        return E51P_INVALID_ARGUMENT;
    for (uint32_t bit = 0; bit < width; ++bit) {
        const auto ra = root(*c, c->endpoints[a].first_pin + a_lsb + bit);
        const auto rb = root(*c, c->endpoints[b].first_pin + b_lsb + bit);
        c->pins[rb].parent = ra;
    }
    return E51P_OK;
}
e51p_status connect_analog(e51p_context* c, e51p_endpoint out, e51p_endpoint in) noexcept {
    if (!configuring(c)) return E51P_INVALID_STATE;
    if (!endpoint(c, out, E51P_ANALOG) || !endpoint(c, in, E51P_ANALOG) ||
        c->endpoints[out].desc.direction != E51P_OUTPUT || c->endpoints[in].desc.direction != E51P_INPUT)
        return E51P_INVALID_ARGUMENT;
    if (c->endpoints[in].source != E51P_INVALID_ID) return E51P_CONFLICT;
    c->endpoints[in].source = out;
    return E51P_OK;
}
e51p_status map_bus(e51p_context* c, e51p_endpoint initiator, uint32_t base,
    uint64_t size, e51p_endpoint target, uint32_t target_base) noexcept {
    if (!configuring(c)) return E51P_INVALID_STATE;
    if (!endpoint(c, initiator, E51P_BUS_INITIATOR) || !endpoint(c, target, E51P_BUS_TARGET) || !size)
        return E51P_INVALID_ARGUMENT;
    const auto& a = c->endpoints[initiator].desc;
    const auto& b = c->endpoints[target].desc;
    const uint64_t a_limit = UINT64_C(1) << a.address_bits, b_limit = UINT64_C(1) << b.address_bits;
    if (a.width != b.width || base >= a_limit || target_base >= b_limit ||
        size > a_limit - base || size > b_limit - target_base) return E51P_INVALID_ARGUMENT;
    return protect(c, [&]() -> e51p_status {
        for (const auto& m : c->mappings)
            if (m.initiator == initiator && base < uint64_t(m.base) + m.size &&
                m.base < uint64_t(base) + size) return E51P_CONFLICT;
        c->mappings.push_back({initiator, target, base, target_base, size});
        return E51P_OK;
    });
}
e51p_status reset(e51p_context* c) noexcept {
    if (!c || !c->started || c->busy) return E51P_INVALID_STATE;
    return protect(c, [&]() -> e51p_status {
        Busy busy(*c);
        c->fault = false;
        c->time = 0;
        c->dirty = true;
        for (auto& e : c->endpoints) {
            e.value = e.enable = e.open_drain = 0;
            e.state = {};
            e.notified = e.analog_valid = false;
        }
        for (const auto& d : c->devices) if (d.desc.reset) d.desc.reset(d.desc.user, c);
        return settle(*c);
    });
}
e51p_status start(e51p_context* c) noexcept {
    if (!configuring(c)) return E51P_INVALID_STATE;
    c->started = true;
    return reset(c);
}
e51p_status advance_to(e51p_context* c, uint64_t time) noexcept {
    if (!live(c) || c->busy) return E51P_INVALID_STATE;
    if (time < c->time) return E51P_INVALID_ARGUMENT;
    if (time == c->time) return contention(*c) ? E51P_CONTENTION : E51P_OK;
    return protect(c, [&]() -> e51p_status {
        Busy busy(*c);
        const auto from = c->time;
        c->time = time;
        for (const auto& d : c->devices) if (d.desc.advance) d.desc.advance(d.desc.user, c, from, time);
        return settle(*c);
    });
}
e51p_status clock(const e51p_context* c, uint64_t* time, uint64_t* hz) noexcept {
    if (!c || !time || !hz) return E51P_INVALID_ARGUMENT;
    *time = c->time;
    *hz = c->tick_hz;
    return E51P_OK;
}
e51p_status drive(e51p_context* c, e51p_endpoint id, uint32_t value,
    uint32_t enable, uint32_t open_drain) noexcept {
    if (!live(c) || c->peek) return E51P_INVALID_STATE;
    if (!endpoint(c, id, E51P_DIGITAL)) return E51P_INVALID_ARGUMENT;
    auto& e = c->endpoints[id];
    if (!(e.desc.direction & E51P_OUTPUT) || ((value | enable | open_drain) & ~mask(e.desc.width)))
        return E51P_INVALID_ARGUMENT;
    if (e.value != value || e.enable != enable || e.open_drain != open_drain) {
        e.value = value; e.enable = enable; e.open_drain = open_drain;
        c->dirty = true;
    }
    if (c->busy) return E51P_OK;
    return protect(c, [&]() -> e51p_status { Busy busy(*c); return settle(*c); });
}
e51p_status sample(const e51p_context* c, e51p_endpoint id, e51p_digital_state* state) noexcept {
    if (!live(c)) return E51P_INVALID_STATE;
    if (!state || !endpoint(c, id, E51P_DIGITAL)) return E51P_INVALID_ARGUMENT;
    if (c->inspection) return e51p_snapshot_sample(c->inspection, id, state);
    *state = c->endpoints[id].state;
    return E51P_OK;
}
e51p_status analog_write(e51p_context* c, e51p_endpoint id, double volts) noexcept {
    if (!live(c) || c->peek) return E51P_INVALID_STATE;
    if (!endpoint(c, id, E51P_ANALOG) || c->endpoints[id].desc.direction != E51P_OUTPUT || !std::isfinite(volts))
        return E51P_INVALID_ARGUMENT;
    auto& e = c->endpoints[id];
    if (!e.analog_valid || e.analog != volts) { e.analog = volts; e.analog_valid = true; c->dirty = true; }
    if (c->busy) return E51P_OK;
    return protect(c, [&]() -> e51p_status { Busy busy(*c); return settle(*c); });
}
e51p_status analog_read(const e51p_context* c, e51p_endpoint id, double* volts) noexcept {
    if (!live(c)) return E51P_INVALID_STATE;
    if (!volts || !endpoint(c, id, E51P_ANALOG)) return E51P_INVALID_ARGUMENT;
    if (!c->endpoints[id].analog_valid) return E51P_NOT_FOUND;
    *volts = c->endpoints[id].analog;
    return E51P_OK;
}
const Mapping* mapping(const e51p_context& c, e51p_endpoint initiator, uint32_t address) {
    for (const auto& m : c.mappings)
        if (m.initiator == initiator && address >= m.base && uint64_t(address) - m.base < m.size) return &m;
    return nullptr;
}
e51p_status bus_read(e51p_context* c, e51p_endpoint id, uint32_t address,
    uint32_t access, uint32_t* value) noexcept {
    if (!live(c) || c->busy) return E51P_INVALID_STATE;
    if (!value || !endpoint(c, id, E51P_BUS_INITIATOR) || (access != E51P_READ && access != E51P_PEEK) ||
        uint64_t(address) >= (UINT64_C(1) << c->endpoints[id].desc.address_bits)) return E51P_INVALID_ARGUMENT;
    const auto* m = mapping(*c, id, address);
    if (!m) return E51P_NOT_FOUND;
    const auto& d = c->devices[c->endpoints[m->target].owner].desc;
    if (!d.bus_read) return E51P_UNSUPPORTED;
    return protect(c, [&]() -> e51p_status {
        Busy busy(*c);
        c->peek = access == E51P_PEEK;
        uint32_t result = 0;
        auto status = d.bus_read(d.user, c, m->target, address - m->base + m->target_base, access, &result);
        c->peek = false;
        const auto propagated = settle(*c);
        if (status == E51P_NOT_FOUND) return E51P_DEVICE_ERROR;
        if (status != E51P_OK && status != E51P_CONTENTION) return status;
        if (result & ~mask(c->endpoints[id].desc.width)) return E51P_DEVICE_ERROR;
        *value = result;
        return propagated == E51P_OK ? status : propagated;
    });
}
e51p_status bus_write(e51p_context* c, e51p_endpoint id, uint32_t address, uint32_t value) noexcept {
    if (!live(c) || c->busy) return E51P_INVALID_STATE;
    if (!endpoint(c, id, E51P_BUS_INITIATOR) ||
        uint64_t(address) >= (UINT64_C(1) << c->endpoints[id].desc.address_bits) ||
        (value & ~mask(c->endpoints[id].desc.width))) return E51P_INVALID_ARGUMENT;
    const auto* m = mapping(*c, id, address);
    if (!m) return E51P_NOT_FOUND;
    const auto& d = c->devices[c->endpoints[m->target].owner].desc;
    if (!d.bus_write) return E51P_UNSUPPORTED;
    return protect(c, [&]() -> e51p_status {
        Busy busy(*c);
        const auto status = d.bus_write(d.user, c, m->target, address - m->base + m->target_base, value);
        const auto propagated = settle(*c);
        if (status == E51P_NOT_FOUND) return E51P_DEVICE_ERROR;
        return status == E51P_OK ? propagated : status;
    });
}
const e51p_api api = {
    sizeof(e51p_api), E51P_ABI_VERSION, create, destroy, add_device, add_endpoint,
    connect_digital, connect_analog, map_bus, start, reset, advance_to, clock,
    drive, sample, analog_write, analog_read, bus_read, bus_write
};
}

extern "C" e51p_status e51p_get_api(uint32_t version, const e51p_api** result) {
    if (!result) return E51P_INVALID_ARGUMENT;
    *result = nullptr;
    if (version != E51P_ABI_VERSION) return E51P_UNSUPPORTED;
    *result = &api;
    return E51P_OK;
}

extern "C" {
e51p_status e51p_snapshot_create(const e51p_context* c, e51p_snapshot** result) {
    if (!result) return E51P_INVALID_ARGUMENT;
    *result = nullptr;
    if (!live(c) || c->busy) return E51P_INVALID_STATE;
    try {
        auto s = new e51p_snapshot;
        try {
            s->context = c;
            s->roots.resize(c->pins.size());
            s->nets.resize(c->pins.size());
            s->drives.resize(c->endpoints.size());
            s->live_nets.resize(c->pins.size());
            s->live_drives.resize(c->endpoints.size());
            for (uint32_t i = 0; i < c->pins.size(); ++i) {
                auto r = i;
                while (c->pins[r].parent != r) r = c->pins[r].parent;
                s->roots[i] = r;
            }
        } catch (...) { delete s; throw; }
        const auto status = e51p_snapshot_begin(s);
        if (status != E51P_OK) { delete s; return status; }
        *result = s;
        return E51P_OK;
    } catch (const std::bad_alloc&) { return E51P_NO_MEMORY; }
    catch (...) { return E51P_DEVICE_ERROR; }
}
void e51p_snapshot_destroy(e51p_snapshot* s) { delete s; }
e51p_status e51p_snapshot_begin(e51p_snapshot* s) {
    if (!s || !live(s->context) || s->context->busy) return E51P_INVALID_STATE;
    const auto& c = *s->context;
    // Most bus transactions run between changes to the live circuit.
    // Restore the working snapshot from contiguous copies in that case.
    if (s->live_revision != c.settled_revision) {
        for (size_t i = 0; i < c.pins.size(); ++i) {
            const auto& p = c.pins[i];
            s->live_nets[i] = {p.high_count, p.low_count, 0, p.up, p.down};
        }
        for (size_t i = 0; i < c.endpoints.size(); ++i) {
            const auto& e = c.endpoints[i];
            s->live_drives[i] = {e.value, e.enable, e.open_drain, 0};
        }
        s->live_revision = c.settled_revision;
    }
    std::copy(s->live_nets.begin(), s->live_nets.end(), s->nets.begin());
    std::copy(s->live_drives.begin(), s->live_drives.end(), s->drives.begin());
    return E51P_OK;
}
e51p_status e51p_snapshot_drive(e51p_snapshot* s, e51p_endpoint id,
    uint32_t value, uint32_t enable, uint32_t drain, uint32_t undefined) {
    if (!s || !endpoint(s->context, id, E51P_DIGITAL)) return E51P_INVALID_ARGUMENT;
    const auto& e = s->context->endpoints[id];
    if (!(e.desc.direction & E51P_OUTPUT) || ((value | enable | drain | undefined) & ~mask(e.desc.width)))
        return E51P_INVALID_ARGUMENT;
    auto& previous = s->drives[id];
    uint32_t changed = (previous.value ^ value) | (previous.enable ^ enable) |
        (previous.drain ^ drain) | (previous.undefined ^ undefined);
    while (changed) {
        const auto bit = first_set_bit(changed);
        const auto flag = UINT32_C(1) << bit;
        changed &= changed - 1;
        auto& net = s->nets[s->roots[e.first_pin + bit]];
        const auto adjust = [&](uint32_t v, uint32_t en, uint32_t od, uint32_t un, bool add) {
            if (un & flag) { if (add) ++net.undefined; else --net.undefined; }
            else if (en & flag) {
                if (!(v & flag)) { if (add) ++net.low; else --net.low; }
                else if (!(od & flag)) { if (add) ++net.high; else --net.high; }
            }
        };
        adjust(previous.value, previous.enable, previous.drain, previous.undefined, false);
        adjust(value, enable, drain, undefined, true);
    }
    previous = {value, enable, drain, undefined};
    return E51P_OK;
}
e51p_status e51p_snapshot_sample(const e51p_snapshot* s, e51p_endpoint id, e51p_digital_state* result) {
    if (!s || !result || !endpoint(s->context, id, E51P_DIGITAL)) return E51P_INVALID_ARGUMENT;
    const auto& e = s->context->endpoints[id];
    e51p_digital_state state{};
    for (uint32_t bit = 0; bit < e.desc.width; ++bit) {
        const auto flag = UINT32_C(1) << bit;
        const auto& net = s->nets[s->roots[e.first_pin + bit]];
        const bool strong = net.high || net.low;
        if (net.undefined) state.floating |= flag;
        else if (strong ? net.high && net.low : net.up && net.down) state.contention |= flag;
        else if (strong ? net.high != 0 : net.up) state.value |= flag;
        else if (!strong && !net.down) state.floating |= flag;
    }
    *result = state;
    return E51P_OK;
}
e51p_status e51p_digital_net(const e51p_context* c, e51p_endpoint id, uint32_t bit, uint32_t* net) {
    if (!live(c) || !net || !endpoint(c, id, E51P_DIGITAL) || bit >= c->endpoints[id].desc.width)
        return E51P_INVALID_ARGUMENT;
    auto r = c->endpoints[id].first_pin + bit;
    while (c->pins[r].parent != r) r = c->pins[r].parent;
    *net = r;
    return E51P_OK;
}
e51p_status e51p_target_set_write_validator(e51p_context* c, e51p_endpoint id,
    e51p_write_validator validator, void* user) {
    if (!configuring(c) || !endpoint(c, id, E51P_BUS_TARGET)) return E51P_INVALID_ARGUMENT;
    c->endpoints[id].validate_write = validator;
    c->endpoints[id].validator_user = user;
    return E51P_OK;
}
e51p_status e51p_target_check_write(e51p_context* c, e51p_endpoint id, uint32_t address, uint32_t value, const e51p_snapshot* snapshot) {
    if (!live(c) || c->busy || !endpoint(c, id, E51P_BUS_TARGET) || (snapshot && snapshot->context != c)) return E51P_INVALID_ARGUMENT;
    const auto& e = c->endpoints[id];
    if (uint64_t(address) >= (UINT64_C(1) << e.desc.address_bits) || (value & ~mask(e.desc.width)))
        return E51P_INVALID_ARGUMENT;
    if (!e.validate_write) return E51P_UNSUPPORTED;
    return protect(c,[&]() -> e51p_status {
        Busy busy(*c); c->inspection = snapshot; c->peek = true;
        e51p_status status;
        try { status = e.validate_write(e.validator_user,address,value); }
        catch (...) { c->inspection = nullptr; throw; }
        c->inspection = nullptr;
        return status;
    });
}
e51p_status e51p_target_read(e51p_context* c, e51p_endpoint id, uint32_t address,
    uint32_t access, const e51p_snapshot* snapshot, uint32_t* value) {
    if (!live(c) || c->busy) return E51P_INVALID_STATE;
    if (!value || !endpoint(c, id, E51P_BUS_TARGET) || (snapshot && snapshot->context != c) ||
        (access != E51P_READ && access != E51P_PEEK)) return E51P_INVALID_ARGUMENT;
    const auto& e = c->endpoints[id];
    if (uint64_t(address) >= (UINT64_C(1) << e.desc.address_bits)) return E51P_INVALID_ARGUMENT;
    const auto& d = c->devices[e.owner].desc;
    if (!d.bus_read) return E51P_UNSUPPORTED;
    return protect(c, [&]() -> e51p_status {
        Busy busy(*c);
        c->inspection = snapshot;
        c->peek = access == E51P_PEEK;
        uint32_t result = 0;
        e51p_status status;
        try { status = d.bus_read(d.user, c, id, address, access, &result); }
        catch (...) { c->inspection = nullptr; throw; }
        c->inspection = nullptr;
        c->peek = false;
        if (access != E51P_PEEK) {
            const auto propagated = settle(*c);
            if (status == E51P_OK) status = propagated;
        }
        if (status != E51P_OK) return status == E51P_NOT_FOUND ? E51P_DEVICE_ERROR : status;
        if (result & ~mask(e.desc.width)) return E51P_DEVICE_ERROR;
        *value = result;
        return E51P_OK;
    });
}
e51p_status e51p_target_write(e51p_context* c, e51p_endpoint id, uint32_t address,
    uint32_t value, const e51p_snapshot* snapshot) {
    if (!live(c) || c->busy) return E51P_INVALID_STATE;
    if (!endpoint(c, id, E51P_BUS_TARGET) || (snapshot && snapshot->context != c)) return E51P_INVALID_ARGUMENT;
    const auto& e = c->endpoints[id];
    if (uint64_t(address) >= (UINT64_C(1) << e.desc.address_bits) || (value & ~mask(e.desc.width)))
        return E51P_INVALID_ARGUMENT;
    const auto& d = c->devices[e.owner].desc;
    if (!d.bus_write) return E51P_UNSUPPORTED;
    return protect(c, [&]() -> e51p_status {
        Busy busy(*c);
        c->inspection = snapshot;
        e51p_status status;
        try { status = d.bus_write(d.user, c, id, address, value); }
        catch (...) { c->inspection = nullptr; throw; }
        c->inspection = nullptr;
        const auto propagated = settle(*c);
        if (status == E51P_NOT_FOUND) return E51P_DEVICE_ERROR;
        return status == E51P_OK ? propagated : status;
    });
}
}
