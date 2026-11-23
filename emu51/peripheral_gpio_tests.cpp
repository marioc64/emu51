// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_peripheral_gpio.h"
#include "emu51/devices.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <vector>

namespace {
void check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void ok(e51p_status status) { check(status == E51P_OK, "SDK operation succeeds"); }
struct Fixture {
    const e51p_api* api = nullptr;
    e51p_context* context = nullptr;
    std::shared_ptr<Emu51PeripheralGpio> gpio;
    emu51 cpu;
    bool table;
    e51p_endpoint external[4]{};

    explicit Fixture(bool dispatch, bool p0Pullup = true) : table(dispatch) {
        ok(e51p_get_api(E51P_ABI_VERSION, &api));
        ok(api->create(12000000, &context));
        gpio = std::make_shared<Emu51PeripheralGpio>(*api, *context);
        const auto d = device("external");
        const char* names[] = {"P0", "P1", "P2", "P3"};
        for (unsigned p = 0; p < 4; ++p) {
            e51p_endpoint_desc desc{sizeof(desc), names[p], E51P_DIGITAL, E51P_OUTPUT, 8, 0,
                p == 0 && p0Pullup ? 255u : 0u, 0};
            ok(api->add_endpoint(context, d, &desc, &external[p]));
            ok(api->connect_digital(context, gpio->port(p), 0, external[p], 0, 8));
        }
    }
    ~Fixture() {
        cpu.setPortIo(nullptr);
        gpio.reset();
        api->destroy(context);
    }
    e51p_device device(const char* id, e51p_device_desc desc = {}) {
        desc.struct_size = sizeof(desc); desc.instance_id = id;
        e51p_device d;
        ok(api->add_device(context, &desc, &d));
        return d;
    }
    e51p_endpoint input(e51p_device d, const char* name, uint32_t kind, uint32_t width) {
        e51p_endpoint_desc desc{sizeof(desc), name, kind, E51P_INPUT, width, 0, 0, 0};
        e51p_endpoint e;
        ok(api->add_endpoint(context, d, &desc, &e));
        return e;
    }
    void start() { ok(api->start(context)); cpu.setPortIo(gpio); ok(gpio->status()); }
    int step() { return table ? cpu.process_table() : cpu.process(); }
    int execute(std::initializer_list<std::uint8_t> code) {
        std::uint16_t address = 0x200;
        for (auto byte : code) cpu.writeProg(address++, byte);
        cpu.PC = 0x200;
        return step();
    }
    void drive(unsigned port, std::uint8_t value, std::uint8_t enable = 255) {
        ok(api->drive(context, external[port], value, enable, 255));
    }
    uint64_t time() {
        uint64_t time, hz;
        ok(api->clock(context, &time, &hz));
        check(hz == 12000000, "GPIO uses oscillator Hz");
        return time;
    }
};

void port_reads_and_rmw(bool table) {
    Fixture f(table);
    f.start();
    for (unsigned p = 0; p < 4; ++p) {
        const auto address = static_cast<std::uint8_t>(0x80 + 16 * p);
        for (unsigned value = 0; value < 256; ++value) {
            f.cpu.writeDirect(address, 255);
            f.drive(p, static_cast<std::uint8_t>(value));
            f.execute({0xe5, address}); // MOV A,port
            check(f.cpu.Acc == value && f.cpu.readLatch(address) == 255, "ordinary port read samples pins without changing latch");
        }
        f.drive(p, 0); // Force all pins low regardless of latch contents.
        for (unsigned latch = 0; latch < 256; ++latch) {
            auto prepare = [&] {
                f.cpu.writeDirect(address, static_cast<std::uint8_t>(latch));
                f.cpu.Acc = 0x5a;
            };
            auto verify = [&](unsigned expected) {
                check(f.cpu.readLatch(address) == (expected & 255) && f.cpu.readDirect(address) == 0,
                    "read-modify-write uses latch even while external device holds all pins low");
            };
            prepare(); f.execute({0x05, address}); verify(latch + 1);
            prepare(); f.execute({0x15, address}); verify(latch - 1);
            prepare(); f.execute({0x42, address}); verify(latch | 0x5a);
            prepare(); f.execute({0x43, address, 0x5a}); verify(latch | 0x5a);
            prepare(); f.execute({0x52, address}); verify(latch & 0x5a);
            prepare(); f.execute({0x53, address, 0x5a}); verify(latch & 0x5a);
            prepare(); f.execute({0x62, address}); verify(latch ^ 0x5a);
            prepare(); f.execute({0x63, address, 0x5a}); verify(latch ^ 0x5a);
            prepare(); f.execute({0xd5, address, 2}); verify(latch - 1);
            check(f.cpu.PC == (latch == 1 ? 0x203 : 0x205), "DJNZ branches on decremented latch");
            prepare(); f.execute({0xd2, address}); verify(latch | 1);
            prepare(); f.execute({0xc2, address}); verify(latch & ~1u);
            prepare(); f.execute({0xb2, address}); verify(latch ^ 1);
            prepare(); f.cpu.PSW |= bit7; f.execute({0x92, address}); verify(latch | 1);
            prepare(); f.cpu.PSW &= static_cast<std::uint8_t>(~bit7);
            f.execute({0x92, address}); verify(latch & ~1u);
            prepare(); f.execute({0x10, address, 2}); verify(latch & ~1u);
            check(f.cpu.PC == (latch & 1 ? 0x205 : 0x203), "JBC tests the latch, not the externally low pin");
        }
        f.cpu.writeDirect(address, 255);
        for (unsigned bit = 0; bit < 8; ++bit) {
            const auto operand = static_cast<std::uint8_t>(address + bit);
            const auto flag = static_cast<std::uint8_t>(1 << bit);
            f.cpu.writeDirect(address, 255);
            f.execute({0xc2, operand});
            check(f.cpu.readLatch(address) == (255 ^ flag), "bit writes preserve all other port latch bits");
            f.execute({0xd2, operand});
            check(f.cpu.readLatch(address) == 255, "SETB addresses every port bit correctly");
        }
        f.execute({0xa2, address});
        check(!(f.cpu.PSW & bit7), "MOV C,port.bit samples the pin");
        f.execute({0x20, address, 2});
        check(f.cpu.PC == 0x203, "JB samples the pin");
        f.execute({0x30, address, 2});
        check(f.cpu.PC == 0x205, "JNB samples the pin");
        f.cpu.Acc = 0x5a;
        f.execute({0xc5, address});
        check(f.cpu.Acc == 0 && f.cpu.readLatch(address) == 0x5a, "XCH reads pins while writing accumulator to latch");
        f.drive(p, 255);
    }
    f.drive(2, 0);
    f.cpu.P2 = 0x12;
    f.cpu.Ri(0, 0x34);
    f.cpu.writeData(0x1234, 0x7a); f.cpu.writeData(0x0034, 0x5a);
    f.execute({0xe2});
    check(f.cpu.Acc == 0x7a && f.cpu.readDirect(0xa0) == 0, "MOVX @Ri page address uses P2 latch, not sampled pins");
    ok(f.gpio->status());
}

void wiring_and_dac(bool table) {
    Fixture f(table, false);
    struct Observer { unsigned changes = 0; uint64_t time = 0; double volts = 0; } observed;
    e51p_device_desc desc{};
    desc.user = &observed;
    desc.analog_changed = [](void* user, e51p_context*, e51p_endpoint, double value, uint64_t time) {
        auto& o = *static_cast<Observer*>(user); ++o.changes; o.time = time; o.volts = value;
    };
    const auto observer = f.device("speaker", desc);
    const auto sink = f.input(observer, "voltage", E51P_ANALOG, 1);
    e51p_endpoint data, voltage;
    ok(e51p_add_parallel_dac(f.api, f.context, "covox", 5.0, &data, &voltage));
    // An eight-bit DAC input assembled from two four-bit CPU port slices.
    ok(f.api->connect_digital(f.context, f.gpio->port(1), 0, data, 0, 4));
    ok(f.api->connect_digital(f.context, f.gpio->port(2), 4, data, 4, 4));
    ok(f.api->connect_analog(f.context, voltage, sink));
    f.start();
    check(f.cpu.readDirect(0x80) == 0 && f.cpu.readLatch(0x80) == 255, "P0 has no internal pull-up");
    check(f.cpu.readDirect(0x90) == 255 && f.cpu.readDirect(0xa0) == 255, "P1/P2 have weak pull-ups");
    f.execute({0x75, 0x90, 0x05});
    f.execute({0x75, 0xa0, 0x50});
    check(observed.volts == 0x55 * 5.0 / 256.0, "CPU bit groups drive Covox-style DAC analog output");
    const auto changes = observed.changes;
    const auto time = f.time();
    f.cpu.readDirect(0x90); f.cpu.readLatch(0x90); f.cpu.readPort(1);
    check(observed.changes == changes && f.time() == time, "inspection does not clock devices or generate output");
    f.execute({0x75, 0x90, 0x0a});
    check(observed.volts == 0x5a * 5.0 / 256.0 && observed.time == time,
        "port write propagates at instruction start before device time advances");
    f.cpu.P1 = 3; // Legacy raw host edits synchronize at the next instruction.
    f.execute({0xe5, 0x90});
    check(f.cpu.Acc == 3 && observed.volts == 0x53 * 5.0 / 256.0, "legacy latch edits publish before operand reads");
    f.cpu.init();
    check(f.time() == 0 && f.cpu.P1 == 255 && observed.volts == 255 * 5.0 / 256.0,
        "CPU reset resets graph epoch and republishes high port latches");
    f.cpu.setPortIo(nullptr);
    f.cpu.P1 = 0x42;
    f.execute({0xe5, 0x90});
    check(f.cpu.Acc == 0x42 && f.time() == 0, "detached core returns to standalone latch semantics");
}

void interrupt_boundaries(bool table) {
    for (unsigned source = 0; source < 2; ++source) {
        Fixture f(table);
        struct Request { Fixture* f; unsigned pin; bool fired = false; std::vector<uint64_t> boundaries; } request{&f, 4u << source, false, {}};
        e51p_device_desc desc{};
        desc.user = &request;
        desc.advance = [](void* user, e51p_context*, uint64_t, uint64_t to) {
            auto& r = *static_cast<Request*>(user);
            r.boundaries.push_back(to);
            if (!r.fired) { r.fired = true; r.f->drive(3, 0, static_cast<std::uint8_t>(r.pin)); }
        };
        f.device("interrupt-source", desc);
        f.start();
        f.cpu.IE = static_cast<std::uint8_t>(0x80 | (1 << (source * 2)));
        f.cpu.TCON = static_cast<std::uint8_t>(1 << (source * 2)); // edge mode
        const auto vector = static_cast<std::uint16_t>(source ? 0x13 : 3);
        check(f.execute({0x00}) == 3 && f.cpu.PC == vector, "device IRQ is sampled before same-boundary arbitration");
        check(f.time() == 36 && request.boundaries == std::vector<uint64_t>({12, 36}),
            "instruction and interrupt-entry clocks advance once each");
        f.cpu.writeProg(vector, 0x32); // RETI while line remains low.
        check(f.step() == 2 && f.cpu.PC == 0x201, "held low edge input does not retrigger");
        f.drive(3, 255, 0); f.step();
        f.drive(3, 0, static_cast<std::uint8_t>(request.pin));
        check(f.step() == 3 && f.cpu.PC == vector, "new falling edge triggers another interrupt");
        ok(f.gpio->status());
    }
    Fixture level(table);
    level.start();
    level.cpu.IE = 0x81;
    level.drive(3, 0, bit2);
    check(level.execute({0x00}) == 3 && level.cpu.PC == 3 && (level.cpu.TCON & bit1),
        "level-sensitive interrupt request stays asserted during entry");
    level.drive(3, 255, 0);
    level.cpu.writeProg(3, 0x32);
    check(level.step() == 2 && level.cpu.PC == 0x201 && !(level.cpu.TCON & bit1),
        "released level input clears request before RETI arbitration");
}

void timers_and_errors(bool table) {
    Fixture f(table);
    f.start();
    f.cpu.TMOD = 0x99; f.cpu.TCON = 0x50; // Gate both 16-bit timers.
    f.drive(3, 0, bit2 | bit3);
    f.execute({0x00});
    check(f.cpu.TL0 == 0 && f.cpu.TL1 == 0 && f.cpu.P3 == 255, "timer gates read pins while latch remains high");
    f.drive(3, 255, 0);
    f.execute({0x00});
    check(f.cpu.TL0 == 1 && f.cpu.TL1 == 1, "released gate pins enable timer clocks");
    f.cpu.TMOD = 0x55; // Both timers in external counter mode.
    f.drive(3, 0, bit4 | bit5);
    f.execute({0xa4}); // MUL AB consumes four machine cycles.
    check(f.cpu.TL0 == 2 && f.cpu.TL1 == 2, "counter mode counts one falling edge, not four cycles");
    f.execute({0x00});
    check(f.cpu.TL0 == 2 && f.cpu.TL1 == 2, "held-low counter input does not repeat");
    f.drive(3, 255, 0); f.execute({0x00});
    f.drive(3, 0, bit4 | bit5); f.execute({0x00});
    check(f.cpu.TL0 == 3 && f.cpu.TL1 == 3, "second sampled falling edge increments counters");
    f.cpu.setModel(Emu51Model::i8052); // Also resets graph/drives/time.
    f.cpu.writeDirect(0xc8, 6); // TR2 + counter mode.
    f.drive(1, 0, 1); f.execute({0x00});
    check(f.cpu.readDirect(0xcc) == 1, "8052 Timer 2 samples wired P1.0");
    f.drive(1, 255, 0); f.execute({0x00});
    f.drive(1, 0, 1); f.execute({0x00});
    check(f.cpu.readDirect(0xcc) == 2, "Timer 2 sees subsequent GPIO edges");
    f.cpu.setModel(Emu51Model::i8052);
    f.cpu.writeDirect(0xc8, 0x0d); // TR2 + EXEN2 + capture.
    f.cpu.writeDirect(0xcc, 0x34); f.cpu.writeDirect(0xcd, 0x12);
    f.drive(1, 0, bit1); f.execute({0x00});
    check(f.cpu.readDirect(0xca) == 0x35 && f.cpu.readDirect(0xcb) == 0x12 && (f.cpu.readDirect(0xc8) & bit6),
        "wired T2EX captures the completed timer value and raises EXF2");
    ok(f.api->advance_to(f.context, std::numeric_limits<uint64_t>::max() - 11));
    f.execute({0x00});
    check(f.gpio->status() == E51P_INVALID_ARGUMENT, "clock overflow is retained across subsequent successful pin reads");
    f.cpu.init(); ok(f.gpio->status());
    {
        Fixture broken(table);
        bool fail = true;
        e51p_device_desc desc{};
        desc.user = &fail;
        desc.advance = [](void* user, e51p_context*, uint64_t, uint64_t) {
            if (*static_cast<bool*>(user)) throw 1;
        };
        broken.device("failure", desc); broken.start();
        broken.execute({0x00});
        check(broken.gpio->status() == E51P_DEVICE_ERROR, "failed device advance remains visible after pin reads");
        broken.execute({0x00});
        check(broken.gpio->status() == E51P_DEVICE_ERROR, "later invalid-state errors preserve first failure");
        fail = false;
        broken.cpu.init(); broken.execute({0x00}); ok(broken.gpio->status());
        check(broken.time() == 12, "reset recovers faulted graph and CPU clock");
    }
}
void counter_modes(bool table) {
    for (unsigned timer = 0; timer < 2; ++timer) {
        for (unsigned mode = 0; mode < (timer ? 3u : 4u); ++mode) {
            Fixture f(table);
            f.start();
            f.cpu.TMOD = static_cast<std::uint8_t>((4 | mode) << (timer * 4));
            f.cpu.TCON = timer ? bit6 : bit4;
            auto& high = timer ? f.cpu.TH1 : f.cpu.TH0;
            auto& low = timer ? f.cpu.TL1 : f.cpu.TL0;
            high = mode == 2 ? 0x80 : 0xff;
            low = 0xff;
            f.drive(3, 0, static_cast<std::uint8_t>(bit4 << timer));
            f.execute({0xa4}); // Four-cycle instruction, one falling edge.
            check(low == (mode == 0 ? 0xe0 : mode == 2 ? 0x80 : 0), "counter rollover/reload follows selected mode");
            check(f.cpu.TCON & (timer ? bit7 : bit5), "external counter overflow sets timer flag");
            if (mode < 2) check(high == 0, "counter overflow wraps high byte");
            const auto previous = low;
            f.execute({0x00});
            check(low == previous, "counter mode waits for a new edge after overflow");
        }
    }
}
}
int main() {
    for (bool table : {false, true}) {
        port_reads_and_rmw(table); wiring_and_dac(table);
        interrupt_boundaries(table); timers_and_errors(table); counter_modes(table);
    }
    std::puts("Both dispatch paths passed GPIO/latch, DAC wiring, interrupt, counter and error checks.");
}
