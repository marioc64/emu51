// SPDX-License-Identifier: GPL-2.0-only
// A complete headless host: CPU P1 -> ideal parallel DAC -> analog voltage.
#include "emu51_peripheral_gpio.h"
#include "emu51/devices.h"
#include <cstdio>
#include <stdexcept>

namespace {
void require(e51p_status status) {
    if (status != E51P_OK) throw std::runtime_error("Peripheral operation failed");
}
}
int main()
{
    try {
        const e51p_api* api = nullptr;
        require(e51p_get_api(E51P_ABI_VERSION, &api));
        e51p_context* raw = nullptr;
        require(api->create(12000000, &raw));
        // Declaration order keeps callback storage alive until CPU detaches.
        std::unique_ptr<e51p_context, decltype(api->destroy)> context(raw, api->destroy);
        auto gpio = std::make_shared<Emu51PeripheralGpio>(*api, *context);
        e51p_endpoint data, voltage;
        require(e51p_add_parallel_dac(api, context.get(), "covox", 5.0, &data, &voltage));
        require(api->connect_digital(context.get(), gpio->port(1), 0, data, 0, 8));
        require(api->start(context.get()));
        emu51 cpu;
        cpu.setPortIo(gpio);
        const std::uint8_t program[] = {0x75, 0x90, 0, 0x75, 0x90, 128, 0x75, 0x90, 255};
        for (unsigned i = 0; i < sizeof(program); ++i) cpu.writeProg(static_cast<std::uint16_t>(i), program[i]);
        for (unsigned i = 0; i < 3; ++i) {
            cpu.process(); // GPIO adapter advances peripheral time automatically.
            require(gpio->status());
            std::uint64_t time, hz;
            double volts;
            require(api->clock(context.get(), &time, &hz));
            require(api->analog_read(context.get(), voltage, &volts));
            std::printf("tick=%llu P1=%02X voltage=%.6f V\n",
                static_cast<unsigned long long>(time), static_cast<unsigned>(cpu.P1), volts);
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
