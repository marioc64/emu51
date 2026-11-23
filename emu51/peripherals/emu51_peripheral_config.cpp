// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_peripheral_config.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

const char* emu51PeripheralKindName(Emu51PeripheralKind kind)
{
    if (const int display=emu51PeripheralDisplayKind(kind); display>=0) {
        static const char* names[]{"7-segment LED display","14-segment LED display","16-segment LED display","LED bar graph",
            "CD4511 BCD decoder","74LS47 BCD decoder","MAX7219","MAX7221","HT16K33","TM1637"};
        return names[display];
    }
    switch (kind) {
    case Emu51PeripheralKind::I8255: return "8255 (mode 0 / BSR)";
    case Emu51PeripheralKind::Adc0804: return "ADC0804";
    case Emu51PeripheralKind::Dac: return "Parallel DAC / Covox";
    case Emu51PeripheralKind::Voltage: return "Voltage source";
    case Emu51PeripheralKind::Digital: return "8-bit digital source";
    case Emu51PeripheralKind::Audio: return "Host audio";
    case Emu51PeripheralKind::Led: return "LED";
    case Emu51PeripheralKind::Switch: return "Switch";
    case Emu51PeripheralKind::Button: return "Pushbutton";
    case Emu51PeripheralKind::Lcd: return "HD44780 LCD";
    case Emu51PeripheralKind::Keypad: return "4x4 matrix keypad";
    case Emu51PeripheralKind::Shift595: return "74HC595";
    case Emu51PeripheralKind::Shift165: return "74HC165";
    case Emu51PeripheralKind::Eeprom: return "24C02 EEPROM";
    case Emu51PeripheralKind::Ay38910: return "AY-3-8910";
    default: break;
    }
    return "Unknown";
}
void emu51ValidatePeripheralConfig(const Emu51PeripheralConfig& c)
{
    if (c.devices.size() > 32 || c.digital.size() > 128 || c.analog.size() > 64)
        throw std::runtime_error("Peripheral graph supports 32 devices, 128 digital and 64 analog wires");
    std::set<std::string> ids{"cpu"};
    std::set<int> addresses;
    std::set<std::string> images;
    int audioCount = 0;
    for (const auto& d : c.devices) {
        if (d.id.empty() || d.id.size() > 48 ||
            !std::all_of(d.id.begin(), d.id.end(), [](unsigned char ch) {
                return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                    (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
            }) || !ids.insert(d.id).second)
            throw std::runtime_error("Device IDs must be unique letters, digits, '_' or '-' (cpu is reserved)");
        if (d.kind < Emu51PeripheralKind::I8255 || d.kind > Emu51PeripheralKind::Tm1637 ||
            !std::isfinite(d.referenceVolts) || d.referenceVolts <= 0 || d.referenceVolts > 1000 ||
            !std::isfinite(d.voltage) || std::abs(d.voltage) > 1000 || d.conversionTicks < 1 ||
            d.digitalValue < 0 || d.digitalValue > 255 || d.audioDirections < 1 || d.audioDirections > 3 ||
            !std::isfinite(d.inputGainDb) || d.inputGainDb < -20 || d.inputGainDb > 60 || !emu51ValidAudioBufferMs(d.audioBufferMs) ||
            d.bounceMs < 0 || d.bounceMs > 1000 || d.columns < 1 || d.columns > 20 ||
            (d.rows != 1 && d.rows != 2 && d.rows != 4) || d.i2cAddress < 0x50 || d.i2cAddress > 0x57 ||
            !std::isfinite(d.clockHz) || d.clockHz < 1000 || d.clockHz > 4000000 ||
            d.displayDigits < 1 || d.displayDigits > (d.kind == Emu51PeripheralKind::Tm1637 ? 6 : 8) ||
            d.barSegments < 1 || d.barSegments > 16 || !std::isfinite(d.persistenceMs) || d.persistenceMs < 1 || d.persistenceMs > 1000 ||
            d.displayColor < 0 || d.displayColor > 4 || (d.displayStyle != 7 && d.displayStyle != 14 && d.displayStyle != 16) ||
            d.displayAddress < 0x70 || d.displayAddress > 0x77 || d.storagePath.size() >= 4096 || d.storagePath.find('\0') != std::string::npos)
            throw std::runtime_error("Invalid device settings: " + d.id);
        if (const int size = emu51PeripheralBusSize(d.kind)) {
            if (d.address < 0 || d.address > 65536-size) throw std::runtime_error("Invalid XDATA address window");
            for (int offset = 0; offset != size; ++offset)
                if (!addresses.insert(d.address + offset).second) throw std::runtime_error("Overlapping XDATA devices");
        }
        if (d.kind == Emu51PeripheralKind::Audio && ++audioCount > 1)
            throw std::runtime_error("One host audio device is supported per project");
        if (d.kind == Emu51PeripheralKind::Eeprom && !d.storagePath.empty() && !images.insert(d.storagePath).second)
            throw std::runtime_error("EEPROM devices require separate image files");
    }
    for (const auto& w : c.digital)
        if (w.a.empty() || w.b.empty() || w.a.size() > 64 || w.b.size() > 64 ||
            w.aBit < 0 || w.aBit > 31 || w.bBit < 0 || w.bBit > 31 || w.width < 1 || w.width > 32)
            throw std::runtime_error("Invalid digital wire");
    for (const auto& w : c.analog)
        if (w.from.empty() || w.to.empty() || w.from.size() > 64 || w.to.size() > 64)
            throw std::runtime_error("Invalid analog wire");
    if (c.devices.empty() && (!c.digital.empty() || !c.analog.empty()))
        throw std::runtime_error("Add a device before connecting wires");
}
Emu51PeripheralConfig emu51PeripheralPreset(int preset)
{
    Emu51PeripheralConfig c;
    if (preset == 1) {
        c.devices.push_back({"dac", Emu51PeripheralKind::Dac});
        c.digital.push_back({"cpu.P1", "dac.data", 0, 0, 8});
    } else if (preset == 2) {
        c.devices = {{"ppi", Emu51PeripheralKind::I8255}, {"adc", Emu51PeripheralKind::Adc0804},
            {"dac", Emu51PeripheralKind::Dac}, {"input", Emu51PeripheralKind::Voltage},
            {"ground", Emu51PeripheralKind::Voltage}};
        c.devices[1].conversionTicks = 120;
        c.devices[3].voltage = 3.125;
        c.devices[4].voltage = 0;
        c.digital = {{"adc.data", "ppi.A", 0, 0, 8}, {"ppi.B", "dac.data", 0, 0, 8},
            {"ppi.C", "adc.CS", 0, 0, 1}, {"ppi.C", "adc.WR", 1, 0, 1},
            {"ppi.C", "adc.RD", 2, 0, 1}, {"adc.INTR", "ppi.C", 0, 4, 1},
            {"adc.INTR", "cpu.P3", 0, 2, 1}};
        c.analog = {{"input.out", "adc.plus"}, {"ground.out", "adc.minus"}};
    } else if (preset == 3) {
        c.devices = {{"adc", Emu51PeripheralKind::Adc0804}, {"covox", Emu51PeripheralKind::Dac},
            {"ground", Emu51PeripheralKind::Voltage}, {"audio", Emu51PeripheralKind::Audio}};
        c.devices[0].conversionTicks = 240;
        c.devices[2].voltage = 0;
        c.devices[3].audioDirections = 3;
        c.devices[3].inputGainDb = 20; // microphone preamplification: 10x
        c.digital = {{"adc.data", "cpu.P0", 0, 0, 8}, {"cpu.P1", "covox.data", 0, 0, 8},
            {"cpu.P3", "adc.CS", 4, 0, 1}, {"cpu.P3", "adc.WR", 5, 0, 1},
            {"cpu.P3", "adc.RD", 6, 0, 1}, {"adc.INTR", "cpu.P3", 0, 2, 1}};
        c.analog = {{"audio.capture", "adc.plus"}, {"ground.out", "adc.minus"}, {"covox.out", "audio.playback"}};
    }
    if (preset == 4) {
        c.devices = {{"covox", Emu51PeripheralKind::Dac}, {"audio", Emu51PeripheralKind::Audio}};
        c.digital = {{"cpu.P1", "covox.data", 0, 0, 8}};
        c.analog = {{"covox.out", "audio.playback"}};
    }
    return c;
}
