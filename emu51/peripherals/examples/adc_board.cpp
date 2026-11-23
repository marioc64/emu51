// SPDX-License-Identifier: GPL-2.0-only
// CPU MOVX -> 8255 -> ADC0804 control/data, then 8255 -> Covox-style DAC.
#include "emu51_peripheral_bus.h"
#include "emu51_peripheral_gpio.h"
#include "emu51/devices.h"
#include "assembler.h"
#include <array>
#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace {
void require(e51p_status status) {
    if (status != E51P_OK) throw std::runtime_error("Peripheral operation failed: " + std::to_string(status));
}
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
constexpr const char* firmware = R"(
org 0
    ljmp main
org 3
    mov 030h,#1
    reti
org 100h
main:
    mov DPTR,#8003h
    mov A,#098h          ; A=input, B=output, C upper=input, C lower=output
    movx @DPTR,A
    mov DPTR,#8002h
    mov A,#006h          ; CS=0, WR=1, RD=1: release converter reset/start
    movx @DPTR,A
wait_adc:
    jb 0B2h,wait_adc     ; INTR -> CPU P3.2, also serviced at vector 0003h
    mov A,#002h          ; CS=0, WR=1, RD=0: enable ADC data and acknowledge
    movx @DPTR,A
    mov DPTR,#8000h
    movx A,@DPTR        ; read ADC through 8255 Port A
    mov 031h,A
    mov DPTR,#8001h
    movx @DPTR,A        ; 8255 Port B -> DAC
    mov 032h,#1
done:
    sjmp done
end
)";
}

int main(int argc, char** argv)
{
    try {
        check(argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--table"), "Usage: emu51_peripheral_adc_demo [--table]");
        const bool table = argc == 2;
        const e51p_api* api = nullptr;
        require(e51p_get_api(E51P_ABI_VERSION, &api));
        e51p_context* raw = nullptr;
        require(api->create(12000000, &raw));
        std::unique_ptr<e51p_context, decltype(api->destroy)> context(raw, api->destroy);
        auto gpio = std::make_shared<Emu51PeripheralGpio>(*api, *context);
        e51p_i8255_endpoints ppi{}; ppi.struct_size = sizeof(ppi);
        e51p_adc0804_endpoints adc{}; adc.struct_size = sizeof(adc);
        require(e51p_add_i8255_mode0(api, context.get(), "ppi", &ppi));
        require(e51p_add_adc0804(api, context.get(), "adc", 120, 5.0, &adc));
        e51p_endpoint dacData, dacVoltage;
        require(e51p_add_parallel_dac(api, context.get(), "covox", 5.0, &dacData, &dacVoltage));
        e51p_device_desc hostDesc{};
        hostDesc.struct_size = sizeof(hostDesc); hostDesc.instance_id = "host";
        e51p_device host;
        require(api->add_device(context.get(), &hostDesc, &host));
        e51p_endpoint_desc desc{sizeof(desc), "xdata", E51P_BUS_INITIATOR, 0, 8, 16, 0, 0};
        e51p_endpoint xdata, positive, negative;
        require(api->add_endpoint(context.get(), host, &desc, &xdata));
        desc = {sizeof(desc), "source_positive", E51P_ANALOG, E51P_OUTPUT, 1, 0, 0, 0};
        require(api->add_endpoint(context.get(), host, &desc, &positive));
        desc.name = "source_negative";
        require(api->add_endpoint(context.get(), host, &desc, &negative));
        require(api->map_bus(context.get(), xdata, 0x8000, 4, ppi.bus, 0));
        require(api->connect_digital(context.get(), adc.data, 0, ppi.port_a, 0, 8));
        require(api->connect_digital(context.get(), ppi.port_b, 0, dacData, 0, 8));
        require(api->connect_digital(context.get(), ppi.port_c, 0, adc.cs_n, 0, 1));
        require(api->connect_digital(context.get(), ppi.port_c, 1, adc.wr_n, 0, 1));
        require(api->connect_digital(context.get(), ppi.port_c, 2, adc.rd_n, 0, 1));
        require(api->connect_digital(context.get(), adc.intr_n, 0, ppi.port_c, 4, 1));
        require(api->connect_digital(context.get(), adc.intr_n, 0, gpio->port(3), 2, 1));
        require(api->connect_analog(context.get(), positive, adc.vin_positive));
        require(api->connect_analog(context.get(), negative, adc.vin_negative));
        require(api->start(context.get()));

        std::array<std::uint8_t, 65536> data{}, program{};
        auto backing = std::make_shared<functional_address_space>(
            [&](std::uint16_t address) { return data[address]; },
            [&](std::uint16_t address, std::uint8_t value) { data[address] = value; },
            [&](std::uint16_t address) { return program[address]; },
            [&](std::uint16_t address, std::uint8_t value) { program[address] = value; });
        auto bus = std::make_shared<Emu51PeripheralBus>(*api, *context, xdata, backing);
        emu51 cpu(bus);
        cpu.setPortIo(gpio);
        const auto assembled = emu51_asm::Assembler{}.assemble_string("adc-board.asm", firmware);
        check(assembled.success(), "Cannot assemble the board demonstration firmware");
        for (const auto& [address, byte] : assembled.image.bytes) cpu.writeProg(address, byte);

        for (const auto code : {160u, 64u}) {
            cpu.init(); // Resets CPU, 8255, ADC, DAC and the emulated clock.
            require(gpio->status());
            double output = -1;
            require(api->analog_read(context.get(), dacVoltage, &output));
            check(output == 0, "Board reset must clear DAC and abort old conversions");
            const auto input = code * 5.0 / 256.0;
            require(api->analog_write(context.get(), positive, input + 1.0));
            require(api->analog_write(context.get(), negative, 1.0));
            cpu.IE = 0x81; cpu.TCON = 1; // EA, EX0, falling-edge INT0.
            unsigned steps = 0;
            std::uint64_t cycles = 0;
            while (!cpu.IRAM[0x32] && steps < 1000) {
                cycles += static_cast<unsigned>(table ? cpu.process_table() : cpu.process());
                require(gpio->status());
                require(bus->lastStatus());
                ++steps;
            }
            check(steps < 1000 && cpu.IRAM[0x30] == 1 && cpu.IRAM[0x31] == code,
                "Firmware must service ADC interrupt and read its result through the 8255");
            require(api->analog_read(context.get(), dacVoltage, &output));
            check(output == input, "ADC -> 8255 -> DAC voltage round trip failed");
            std::uint64_t time, hz;
            require(api->clock(context.get(), &time, &hz));
            check(time == cycles * 12 && hz == 12000000, "Peripheral time must include interrupt entry exactly once");
            for (unsigned offset = 0; offset < 4; ++offset)
                check(data[0x8000 + offset] == 0, "Claimed 8255 accesses must bypass backing RAM");
            std::uint8_t status = 0;
            require(bus->peekData(0x8002, status));
            check(status == 0x12, "Read strobe must acknowledge ADC INTR, visible on 8255 PC4");
            require(api->clock(context.get(), &time, &hz));
            check(time == cycles * 12, "Board inspection must not advance devices");
            std::printf("%s: ADC=%02X IRQ=handled DAC=%.6f V ticks=%llu\n", table ? "table" : "switch",
                code, output, static_cast<unsigned long long>(time));
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
