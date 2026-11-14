// SPDX-License-Identifier: GPL-2.0-only
#include "emu51.h"

// Classic Intel 8052 Timer 2, without derivative-specific T2MOD extensions.
// Timer clocks and external edges are observed at instruction boundaries.
unsigned emu51::advance_timer2(std::uint8_t cycles)
{
    auto& control = SFR[0xc8];
    const auto pins = static_cast<std::uint8_t>(P1 & timer2Pins_ & 3);
    const bool t2Edge = (previousTimer2Pins_ & 1) && !(pins & 1);
    const bool exEdge = (previousTimer2Pins_ & 2) && !(pins & 2);
    previousTimer2Pins_ = pins;
    if (!(control & bit2)) return 0; // TR2

    const bool baud = (control & (bit4 | bit5)) != 0;
    const bool capture = !baud && (control & bit0);
    const unsigned reload = (static_cast<unsigned>(SFR[0xcb]) << 8) | SFR[0xca];
    unsigned value = (static_cast<unsigned>(SFR[0xcd]) << 8) | SFR[0xcc];
    const unsigned ticks = (control & bit1) ? (t2Edge ? 1u : 0u) : cycles * (baud ? 6u : 1u);
    const unsigned next = value + ticks;
    unsigned overflows = next >> 16;
    if (overflows && !capture) {
        const unsigned period = 0x10000u - reload;
        overflows = 1 + (next - 0x10000u) / period;
        value = reload + (next - 0x10000u) % period;
    } else value = next & 0xffff;
    if (overflows && !baud) control |= bit7; // TF2 suppressed in baud mode

    if (exEdge && (control & bit3)) {
        control |= bit6; // EXF2 remains an interrupt source in baud mode
        if (capture) {
            SFR[0xca] = static_cast<std::uint8_t>(value);
            SFR[0xcb] = static_cast<std::uint8_t>(value >> 8);
        } else if (!baud) value = reload;
    }
    SFR[0xcc] = static_cast<std::uint8_t>(value);
    SFR[0xcd] = static_cast<std::uint8_t>(value >> 8);
    return overflows;
}
