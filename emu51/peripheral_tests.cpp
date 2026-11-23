// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_peripheral_bus.h"
#include "emu51_peripheral_gpio.h"
#include "parallel_expander.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <iterator>

namespace {
void check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void ok(e51p_status status) { check(status == E51P_OK, "SDK operation succeeds"); }
void run(bool table, bool withGpio) {
    const e51p_api* api = nullptr;
    ok(e51p_get_api(E51P_ABI_VERSION, &api));
    e51p_context* context = nullptr;
    ok(api->create(12000000, &context));
    e51p_parallel_expander expander{};
    ok(e51p_parallel_expander_add(api, context, "expander", &expander));
    e51p_device_desc host{};
    host.struct_size = sizeof(host); host.instance_id = "cpu";
    e51p_device cpuDevice;
    ok(api->add_device(context, &host, &cpuDevice));
    e51p_endpoint_desc desc{sizeof(desc), "xdata", E51P_BUS_INITIATOR, 0, 8, 16, 0, 0};
    e51p_endpoint bus, leds;
    ok(api->add_endpoint(context, cpuDevice, &desc, &bus));
    desc = {sizeof(desc), "leds", E51P_DIGITAL, E51P_INPUT, 4, 0, 0, 0};
    ok(api->add_endpoint(context, cpuDevice, &desc, &leds));
    ok(api->map_bus(context, bus, 0x8000, 4, expander.bus, 0));
    ok(api->connect_digital(context, expander.ports[0], 4, leds, 0, 4));
    std::shared_ptr<Emu51PeripheralGpio> gpio;
    if (withGpio) {
        gpio = std::make_shared<Emu51PeripheralGpio>(*api, *context);
        ok(api->connect_digital(context, expander.ports[0], 0, gpio->port(1), 0, 8));
    }
    ok(api->start(context));
    {
        std::array<std::uint8_t, 65536> data{}, program{};
        const std::uint8_t code[] = {
            0x90, 0x80, 0x03, // MOV DPTR,#8003h
            0x74, 0x01,       // MOV A,#1 (port A output)
            0xf0,             // MOVX @DPTR,A
            0x90, 0x80, 0x00, // MOV DPTR,#8000h
            0x74, 0xa0,       // MOV A,#A0h
            0xf0,             // MOVX @DPTR,A
            0xe0,             // MOVX A,@DPTR
            0xe5, 0x90        // MOV A,P1 (when GPIO is attached)
        };
        std::copy(std::begin(code), std::end(code), program.begin());
        auto backing = std::make_shared<functional_address_space>(
            [&](std::uint16_t address) { return data[address]; },
            [&](std::uint16_t address, std::uint8_t value) { data[address] = value; },
            [&](std::uint16_t address) { return program[address]; },
            [&](std::uint16_t address, std::uint8_t value) { program[address] = value; });
        auto map = std::make_shared<Emu51PeripheralBus>(*api, *context, bus, backing);
        emu51 cpu(map);
        if (gpio) cpu.setPortIo(gpio);
        std::uint64_t cycles = 0;
        for (unsigned i = 0; i < (gpio ? 8u : 7u); ++i) {
            const int step = table ? cpu.process_table() : cpu.process();
            cycles += static_cast<unsigned>(step);
            if (!gpio) ok(map->advanceMachineCycles(static_cast<unsigned>(step)));
        }
        e51p_digital_state state;
        ok(api->sample(context, leds, &state));
        check(state.value == 10 && !state.floating && !state.contention, "MOVX through expander drives downstream bit slice");
        check(cpu.Acc == 0xa0 && data[0x8000] == 0, "claimed data access bypasses backing memory");
        if (gpio) {
            check(cpu.P1 == 255 && cpu.readDirect(0x90) == 0xa0,
                "bus-connected expander drives CPU input port while its output latch stays high");
            ok(gpio->status());
        }
        std::uint64_t time, hz;
        ok(api->clock(context, &time, &hz));
        check(time == cycles * 12 && hz == 12000000, "host advances completed CPU machine cycles");
        map->writeData(0x1234, 0x55);
        check(map->readData(0x1234) == 0x55, "unmapped data uses backing memory");
        map->writeProg(0x8000, 0x42);
        check(map->readProg(0x8000) == 0x42 && expander.latch[0] == 0xa0, "program loading bypasses peripheral map");
        map->writeData(0x8003, 0xff);
        check(map->lastStatus() == E51P_INVALID_ARGUMENT && data[0x8003] == 0,
            "claimed device failure never writes backing memory");
        std::uint8_t peek = 0;
        ok(map->peekData(0x8000, peek));
        check(peek == 0xa0, "inspection uses passive bus access");
        check(map->peekData(0x1234, peek) == E51P_NOT_FOUND && peek == 0xa0,
            "unmapped peek does not invoke arbitrary backing read");
        check(map->advanceMachineCycles(std::numeric_limits<std::uint64_t>::max()) == E51P_INVALID_ARGUMENT,
            "host rejects clock overflow");
        cpu.init();
        if (!gpio) ok(api->reset(context));
        ok(api->sample(context, leds, &state));
        check(expander.output_enable == 0 && (gpio ? state.value == 15 : state.floating == 15),
            "CPU/device reset releases expander drivers; connected GPIO pull-ups remain");
    }
    gpio.reset();
    api->destroy(context);
}
}
int main() {
    run(false, false); run(true, false); run(false, true); run(true, true);
    std::puts("Both CPU dispatch paths passed bus-expander wiring and host adapter checks.");
}
