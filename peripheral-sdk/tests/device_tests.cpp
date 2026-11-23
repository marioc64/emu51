// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#include "emu51/devices.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
void check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void ok(e51p_status status) { check(status == E51P_OK, "SDK operation succeeds"); }
struct Graph {
    const e51p_api* api = nullptr;
    e51p_context* c = nullptr;
    e51p_device host;
    Graph() {
        ok(e51p_get_api(E51P_ABI_VERSION, &api));
        ok(api->create(12000000, &c));
        e51p_device_desc desc{};
        desc.struct_size = sizeof(desc); desc.instance_id = "host";
        ok(api->add_device(c, &desc, &host));
    }
    ~Graph() { api->destroy(c); }
    Graph(const Graph&) = delete;
    Graph& operator=(const Graph&) = delete;
    e51p_endpoint endpoint(const char* name, uint32_t kind, uint32_t width,
        uint32_t direction, uint32_t addressBits = 0) {
        e51p_endpoint_desc desc{sizeof(desc), name, kind, direction, width, addressBits, 0, 0};
        e51p_endpoint e;
        ok(api->add_endpoint(c, host, &desc, &e));
        return e;
    }
    e51p_digital_state sample(e51p_endpoint e) {
        e51p_digital_state state{}; ok(api->sample(c, e, &state)); return state;
    }
    uint64_t time() {
        uint64_t now, hz; ok(api->clock(c, &now, &hz)); return now;
    }
    void advance(uint64_t ticks) { ok(api->advance_to(c, time() + ticks)); }
};

void i8255_mode0() {
    Graph g;
    const auto bus = g.endpoint("bus", E51P_BUS_INITIATOR, 8, 0, 16);
    const e51p_endpoint drivers[] = {
        g.endpoint("A", E51P_DIGITAL, 8, E51P_OUTPUT),
        g.endpoint("B", E51P_DIGITAL, 8, E51P_OUTPUT),
        g.endpoint("C", E51P_DIGITAL, 8, E51P_OUTPUT)};
    const auto reset = g.endpoint("reset", E51P_DIGITAL, 1, E51P_OUTPUT);
    e51p_i8255_endpoints pins{};
    check(e51p_add_i8255_mode0(g.api, g.c, "ppi", &pins) == E51P_INVALID_ARGUMENT,
        "8255 requires a sized endpoint result");
    pins.struct_size = sizeof(pins);
    ok(e51p_add_i8255_mode0(g.api, g.c, "ppi", &pins));
    const e51p_endpoint ports[] = {pins.port_a, pins.port_b, pins.port_c};
    ok(g.api->map_bus(g.c, bus, 0x8000, 4, pins.bus, 0));
    for (unsigned i = 0; i < 3; ++i) ok(g.api->connect_digital(g.c, drivers[i], 0, ports[i], 0, 8));
    ok(g.api->connect_digital(g.c, reset, 0, pins.reset, 0, 1));
    ok(g.api->start(g.c));
    for (auto p : ports) check(g.sample(p).floating == 255, "8255 resets with all ports as inputs");
    uint32_t readback = 123;
    check(g.api->bus_read(g.c, bus, 0x8003, E51P_PEEK, &readback) == E51P_UNSUPPORTED && readback == 123,
        "original 8255 control register is write-only");
    for (unsigned config = 0; config < 16; ++config) {
        const unsigned mode = 0x80 | ((config & 8) ? 0x10 : 0) | ((config & 4) ? 8 : 0) |
            ((config & 2) ? 2 : 0) | ((config & 1) ? 1 : 0);
        const unsigned masks[] = {
            (config & 8) ? 0u : 255u, (config & 2) ? 0u : 255u,
            ((config & 4) ? 0u : 240u) | ((config & 1) ? 0u : 15u)};
        for (auto d : drivers) ok(g.api->drive(g.c, d, 0, 0, 0));
        ok(g.api->bus_write(g.c, bus, 0x8003, mode));
        for (unsigned p = 0; p < 3; ++p) {
            auto state = g.sample(ports[p]);
            check(state.value == 0 && state.floating == (masks[p] ^ 255), "all 16 mode-0 direction combinations");
            ok(g.api->bus_write(g.c, bus, 0x8000 + p, 0xa5));
            ok(g.api->drive(g.c, drivers[p], 0x5a, masks[p] ^ 255, 0));
            ok(g.api->bus_read(g.c, bus, 0x8000 + p, E51P_READ, &readback));
            check(readback == ((0xa5 & masks[p]) | (0x5a & (masks[p] ^ 255))), "mode-0 reads merge live inputs and driven outputs");
            const auto now = g.time();
            ok(g.api->bus_read(g.c, bus, 0x8000 + p, E51P_PEEK, &readback));
            check(g.time() == now, "8255 peek does not clock the device");
        }
        ok(g.api->bus_write(g.c, bus, 0x8003, mode));
        for (unsigned p = 0; p < 3; ++p)
            check((g.sample(ports[p]).value & masks[p]) == 0, "rewriting mode clears all output latches");
    }
    for (auto d : drivers) ok(g.api->drive(g.c, d, 0, 0, 0));
    ok(g.api->bus_write(g.c, bus, 0x8003, 0x80));
    ok(g.api->bus_write(g.c, bus, 0x8000, 0xa5));
    ok(g.api->bus_write(g.c, bus, 0x8001, 0x5a));
    uint32_t cLatch = 0;
    for (unsigned command = 0; command < 128; ++command) {
        const auto bit = 1u << ((command >> 1) & 7);
        cLatch = command & 1 ? cLatch | bit : cLatch & ~bit;
        ok(g.api->bus_write(g.c, bus, 0x8003, command));
        check(g.sample(pins.port_c).value == cLatch && !g.sample(pins.port_c).floating,
            "all BSR encodings preserve direction and address the selected C bit");
        check(g.sample(pins.port_a).value == 0xa5 && g.sample(pins.port_b).value == 0x5a,
            "BSR does not clear or change A/B latches");
    }
    for (unsigned command = 128; command < 256; ++command) {
        if (!(command & 0x64)) continue;
        check(g.api->bus_write(g.c, bus, 0x8003, command) == E51P_UNSUPPORTED,
            "unsupported handshake modes are rejected explicitly");
        check(g.sample(pins.port_a).value == 0xa5 && g.sample(pins.port_b).value == 0x5a &&
            g.sample(pins.port_c).value == cLatch, "unsupported mode write leaves all existing state intact");
    }
    ok(g.api->bus_write(g.c, bus, 0x8003, 0x88)); // C upper input, lower output.
    ok(g.api->drive(g.c, drivers[2], 0xa0, 0xf0, 0));
    ok(g.api->bus_write(g.c, bus, 0x8003, 15)); // BSR set C7 (input).
    ok(g.api->bus_write(g.c, bus, 0x8003, 1));  // BSR set C0 (output).
    ok(g.api->bus_read(g.c, bus, 0x8002, E51P_READ, &readback));
    check(readback == 0xa1, "BSR preserves independently directed C halves and does not drive input bits");
    ok(g.api->drive(g.c, drivers[2], 0, 0, 0));
    ok(g.api->bus_write(g.c, bus, 0x8003, 0x80));
    for (unsigned p = 0; p < 3; ++p) {
        ok(g.api->bus_write(g.c, bus, 0x8000 + p, 255));
        check(g.api->drive(g.c, drivers[p], 0, 255, 0) == E51P_CONTENTION, "8255 output short is visible on nets");
        check(g.api->bus_read(g.c, bus, 0x8000 + p, E51P_READ, &readback) == E51P_CONTENTION &&
            readback == (p == 0 ? 0u : 255u), "A reads physical pins, B/C output bits read their latch");
        ok(g.api->drive(g.c, drivers[p], 0, 0, 0));
    }
    ok(g.api->drive(g.c, reset, 1, 1, 0));
    for (auto p : ports) check(g.sample(p).floating == 255, "asserted reset releases all outputs immediately");
    check(g.api->bus_write(g.c, bus, 0x8003, 0x80) == E51P_INVALID_STATE,
        "held reset prevents mode programming");
    check(g.api->bus_read(g.c, bus, 0x8000, E51P_READ, &readback) == E51P_INVALID_STATE,
        "held reset blocks data transactions");
    ok(g.api->drive(g.c, reset, 0, 1, 0));
    ok(g.api->bus_write(g.c, bus, 0x8003, 0x80));
    for (auto p : ports) check(g.sample(p).value == 0 && !g.sample(p).floating, "reset clears output latches before reprogramming");
    ok(g.api->reset(g.c));
    for (auto p : ports) check(g.sample(p).floating == 255, "context reset also resets 8255");
}

struct Adc : Graph {
    e51p_adc0804_endpoints pins{};
    e51p_endpoint control, positive, negative, other;
    explicit Adc(uint64_t ticks = 100, bool wireNegative = true) {
        pins.struct_size = sizeof(pins);
        ok(e51p_add_adc0804(api, c, "adc", ticks, 5.0, &pins));
        control = endpoint("control", E51P_DIGITAL, 3, E51P_OUTPUT);
        positive = endpoint("positive", E51P_ANALOG, 1, E51P_OUTPUT);
        negative = endpoint("negative", E51P_ANALOG, 1, E51P_OUTPUT);
        other = endpoint("other", E51P_DIGITAL, 8, E51P_OUTPUT);
        ok(api->connect_digital(c, control, 0, pins.cs_n, 0, 1));
        ok(api->connect_digital(c, control, 1, pins.rd_n, 0, 1));
        ok(api->connect_digital(c, control, 2, pins.wr_n, 0, 1));
        ok(api->connect_digital(c, other, 0, pins.data, 0, 8));
        ok(api->connect_analog(c, positive, pins.vin_positive));
        if (wireNegative) ok(api->connect_analog(c, negative, pins.vin_negative));
        ok(api->start(c));
    }
    void set(bool cs, bool rd, bool wr) { ok(api->drive(c, control, unsigned(cs) | (unsigned(rd) << 1) | (unsigned(wr) << 2), 7, 0)); }
    void pulse() { set(false, true, false); set(false, true, true); }
    void voltage(double plus, double minus = 0) {
        ok(api->analog_write(c, positive, plus)); ok(api->analog_write(c, negative, minus));
    }
    bool ready() { return sample(pins.intr_n).value == 0; }
    unsigned read() {
        set(false, false, true);
        const auto state = sample(pins.data);
        check(!state.floating && !state.contention, "ADC supplies a defined read value");
        set(false, true, true);
        return state.value;
    }
};
void adc_protocol() {
    Adc adc;
    check(!adc.ready() && adc.sample(adc.pins.data).floating == 255, "ADC idle interrupt and tri-state outputs");
    adc.voltage(2.5);
    adc.set(true, true, false); adc.set(true, true, true);
    adc.advance(100);
    check(!adc.ready(), "WR pulse while deselected does not start conversion");
    adc.pulse();
    adc.advance(99);
    check(!adc.ready() && adc.sample(adc.pins.data).floating == 255, "conversion is not early and read bus stays disabled");
    const auto time = adc.time();
    adc.sample(adc.pins.intr_n); adc.sample(adc.pins.data);
    check(adc.time() == time, "inspection does not advance ADC conversion");
    adc.advance(1);
    check(adc.ready() && adc.sample(adc.pins.data).floating == 255, "completed conversion asserts INTR but waits for RD");
    check(adc.read() == 128 && !adc.ready() && adc.sample(adc.pins.data).floating == 255,
        "read strobe acknowledges interrupt and then releases data bus");
    adc.voltage(4.0); adc.pulse();
    adc.voltage(1.0); // The profile samples the differential input at start.
    adc.set(false, false, true);
    check(adc.sample(adc.pins.data).value == 128, "reading while busy retains previous output latch");
    adc.advance(100);
    check(!adc.ready() && adc.sample(adc.pins.data).value == 205,
        "held RD sees new result with interrupt held inactive");
    adc.set(true, false, true);
    check(adc.sample(adc.pins.data).floating == 255, "deselection releases data even when RD is low");
    adc.voltage(3.0);
    adc.set(false, true, false);
    adc.advance(200);
    check(!adc.ready(), "CS/WR both low hold converter reset");
    adc.set(true, true, false); // CS rising is also a valid reset release.
    adc.advance(100);
    check(adc.ready() && adc.read() == 154, "CS rising can start an armed conversion");
    adc.voltage(1.0); adc.pulse(); adc.advance(50);
    adc.voltage(2.0); adc.pulse(); adc.advance(99);
    check(!adc.ready(), "second write pulse restarts conversion timing");
    adc.advance(1);
    check(adc.ready() && adc.read() == 102, "restarted conversion samples the new input");
    adc.pulse(); adc.advance(50); ok(adc.api->reset(adc.c)); adc.advance(200);
    check(!adc.ready() && adc.read() == 0, "reset cancels pending conversion and clears output latch");
}
void adc_quantization() {
    Adc adc(1);
    for (unsigned code = 0; code < 256; ++code) {
        adc.voltage(code * 5.0 / 256.0); adc.pulse(); adc.advance(1);
        check(adc.read() == code, "all ADC code centers round to their exact code");
    }
    for (unsigned code = 0; code < 255; ++code) {
        const auto threshold = (code + 0.5) * 5.0 / 256.0;
        adc.voltage(threshold - 0.000001); adc.pulse(); adc.advance(1);
        check(adc.read() == code, "ADC threshold lower side");
        adc.voltage(threshold); adc.pulse(); adc.advance(1);
        check(adc.read() == code + 1, "ADC threshold rounds up at half an LSB");
    }
    adc.voltage(2.0, 1.0); adc.pulse(); adc.advance(1);
    check(adc.read() == 51, "ADC supports differential input offset");
    adc.voltage(-1.0); adc.pulse(); adc.advance(1);
    check(adc.read() == 0, "negative differential input saturates low");
    adc.voltage(7.0); adc.pulse(); adc.advance(1);
    check(adc.read() == 255, "above-span input saturates high");
    adc.voltage(std::numeric_limits<double>::max(), -std::numeric_limits<double>::max());
    adc.pulse(); adc.advance(1);
    check(adc.read() == 255, "large finite differential input cannot overflow integer quantization");
    adc.voltage(2.5); adc.pulse(); adc.advance(1); adc.set(false, false, true);
    check(adc.api->drive(adc.c, adc.other, 127, 255, 0) == E51P_CONTENTION &&
        adc.sample(adc.pins.data).contention == 255, "ADC read data participates in electrical contention");
    ok(adc.api->drive(adc.c, adc.other, 0, 0, 0));
}
void adc_missing_input_and_clock() {
    Adc adc(100, false);
    adc.pulse(); adc.advance(100); adc.set(false, false, true);
    check(adc.ready() == false && adc.sample(adc.pins.data).floating == 255,
        "missing analog input does not fabricate a conversion result");
    adc.voltage(2.5); adc.pulse(); adc.advance(100);
    check(adc.read() == 128, "unconnected negative input defaults to ground");
    ok(adc.api->advance_to(adc.c, UINT64_C(1) << 40));
    adc.pulse(); adc.advance(99); check(!adc.ready(), "conversion works past 32-bit time");
    adc.advance(1); check(adc.ready() && adc.read() == 128, "64-bit conversion completion");
    Adc longConversion(std::numeric_limits<uint64_t>::max());
    longConversion.voltage(2.5); longConversion.pulse();
    longConversion.advance(std::numeric_limits<uint64_t>::max());
    check(longConversion.ready() && longConversion.read() == 128, "full-range conversion duration avoids deadline overflow");
    Graph invalid;
    e51p_adc0804_endpoints pins{};
    pins.struct_size = sizeof(pins); pins.data = 123;
    check(e51p_add_adc0804(invalid.api, invalid.c, "invalid", 0, 5, &pins) == E51P_INVALID_ARGUMENT && pins.data == 123,
        "zero conversion duration rejected without writing result");
    check(e51p_add_adc0804(invalid.api, invalid.c, "invalid", 1, 0, &pins) == E51P_INVALID_ARGUMENT,
        "zero analog span rejected");
    check(e51p_add_adc0804(invalid.api, invalid.c, "invalid", 1,
        std::numeric_limits<double>::quiet_NaN(), &pins) == E51P_INVALID_ARGUMENT, "nonfinite span rejected");
    ok(e51p_add_adc0804(invalid.api, invalid.c, "adc", 1, 5, &pins));
    const auto original = pins.data;
    check(e51p_add_adc0804(invalid.api, invalid.c, "adc", 1, 5, &pins) == E51P_CONFLICT && pins.data == original,
        "duplicate ADC instance preserves result and releases failed setup allocation");
}
}
int main() {
    i8255_mode0(); adc_protocol(); adc_quantization(); adc_missing_input_and_clock();
    std::puts("8255 Mode 0/BSR and ADC0804 signal, timing, quantization and reset checks passed.");
}
