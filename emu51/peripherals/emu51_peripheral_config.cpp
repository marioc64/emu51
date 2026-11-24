// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_peripheral_config.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <map>
#include <sstream>
#include <limits>
#include <cctype>

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
    case Emu51PeripheralKind::Ram: return "RAM";
    case Emu51PeripheralKind::Rom: return "ROM";
    case Emu51PeripheralKind::Clock: return "Clock";
    case Emu51PeripheralKind::AddressDecoder: return "Address Decoder";
    case Emu51PeripheralKind::BusLatch: return "8-bit bus output latch";
    case Emu51PeripheralKind::BusBuffer: return "8-bit bus input buffer";
    case Emu51PeripheralKind::KeyMatrix: return "Configurable key matrix";
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
        if (d.kind < Emu51PeripheralKind::I8255 || d.kind > Emu51PeripheralKind::KeyMatrix ||
            !std::isfinite(d.referenceVolts) || d.referenceVolts <= 0 || d.referenceVolts > 1000 ||
            !std::isfinite(d.voltage) || std::abs(d.voltage) > 1000 || d.conversionTicks < 1 ||
            d.digitalValue < 0 || d.digitalValue > 255 || d.audioDirections < 1 || d.audioDirections > 3 ||
            !std::isfinite(d.inputGainDb) || d.inputGainDb < -20 || d.inputGainDb > 60 || !emu51ValidAudioBufferMs(d.audioBufferMs) ||
            d.bounceMs < 0 || d.bounceMs > 1000 ||
            (d.kind == Emu51PeripheralKind::KeyMatrix ? (d.columns < 1 || d.columns > 4 || d.rows < 1 || d.rows > 8) :
                (d.columns < 1 || d.columns > 20 || (d.rows != 1 && d.rows != 2 && d.rows != 4))) ||
            d.i2cAddress < 0x50 || d.i2cAddress > 0x57 ||
            !std::isfinite(d.clockHz) || d.clockHz < (d.kind == Emu51PeripheralKind::Clock ? 1 : 1000) ||
            d.clockHz > (d.kind == Emu51PeripheralKind::Clock ? 1000000000 : 4000000) ||
            d.displayDigits < 1 || d.displayDigits > (d.kind == Emu51PeripheralKind::Tm1637 ? 6 : 8) ||
            d.barSegments < 1 || d.barSegments > 16 || !std::isfinite(d.persistenceMs) || d.persistenceMs < 1 || d.persistenceMs > 1000 ||
            d.displayColor < 0 || d.displayColor > 4 || (d.displayStyle != 7 && d.displayStyle != 14 && d.displayStyle != 16) ||
            d.displayAddress < 0x70 || d.displayAddress > 0x77 || d.storagePath.size() >= 4096 || d.storagePath.find('\0') != std::string::npos)
            throw std::runtime_error("Invalid device settings: " + d.id);
        if (const int size = emu51PeripheralBusSize(d.kind); size && !d.wiredBus) {
            if (d.address < 0 || d.address > 65536-size) throw std::runtime_error("Invalid XDATA address window");
            for (int offset = 0; offset != size; ++offset)
                if (!addresses.insert(d.address + offset).second) throw std::runtime_error("Overlapping XDATA devices");
        }
        if (d.kind == Emu51PeripheralKind::Audio && ++audioCount > 1)
            throw std::runtime_error("One host audio device is supported per project");
        if (d.kind == Emu51PeripheralKind::Eeprom && !d.storagePath.empty() && !images.insert(d.storagePath).second)
            throw std::runtime_error("EEPROM devices require separate image files");
        if (d.kind == Emu51PeripheralKind::Ram || d.kind == Emu51PeripheralKind::Rom) {
            if (!d.memoryCapacity || d.memoryCapacity > (UINT64_C(1) << 32) || d.memoryFill < 0 || d.memoryFill > 255 || d.memoryImages.size() > 32)
                throw std::runtime_error("Invalid memory capacity, fill or image count: " + d.id);
            for (const auto& image : d.memoryImages)
                if (image.path.empty() || image.path.size() >= 4096 || image.path.find('\0') != std::string::npos ||
                    image.offset > d.memoryCapacity || !image.length || image.length > d.memoryCapacity - image.offset)
                    throw std::runtime_error("Invalid memory initialization image: " + d.id);
        }
        if (d.kind == Emu51PeripheralKind::AddressDecoder) {
            const auto error = emu51DecoderError(d);
            if (!error.empty()) throw std::runtime_error(error);
        }
        if (d.wiredBus && !emu51PeripheralBusSize(d.kind)) throw std::runtime_error("Only register bus devices can enable wired bus pins: "+d.id);
        if ((d.kind == Emu51PeripheralKind::BusLatch || d.kind == Emu51PeripheralKind::BusBuffer) && !d.wiredBus)
            throw std::runtime_error("Bus latch/buffer requires wired bus pins: "+d.id);
        for (const auto* labels : {&d.keyLabels,&d.keyLegends})
            if (labels->size() > 32 || std::any_of(labels->begin(),labels->end(),[](const auto& label) {
                return label.size() > 63 || std::any_of(label.begin(),label.end(),[](unsigned char c) { return c < 32; });
            })) throw std::runtime_error("Invalid key labels or legends: "+d.id);
        if (d.directKeys < 0 || d.directKeys > 8 || (d.directKeys && d.kind != Emu51PeripheralKind::KeyMatrix) ||
            (d.kind == Emu51PeripheralKind::KeyMatrix && emu51KeyCount(d) > 32))
            throw std::runtime_error("Key matrix supports at most 32 contacts, including 0..8 independent keys: "+d.id);
        if (d.keyLayoutColumns || d.keyLayoutRows || !d.keyLayout.empty()) {
            if ((d.kind != Emu51PeripheralKind::Keypad && d.kind != Emu51PeripheralKind::KeyMatrix) ||
                d.keyLayoutColumns < 1 || d.keyLayoutColumns > 8 || d.keyLayoutRows < 1 || d.keyLayoutRows > 8 ||
                d.keyLayout.size() != static_cast<std::size_t>(d.keyLayoutColumns*d.keyLayoutRows))
                throw std::runtime_error("Key layout requires 1..8 columns and rows with one assignment per position: "+d.id);
            std::set<int> used;
            for (int key : d.keyLayout) {
                if (key < -1 || key >= emu51KeyCount(d)) throw std::runtime_error("Key layout contact is outside the electrical matrix: "+d.id);
                if (key >= 0 && !used.insert(key).second) throw std::runtime_error("Key layout assigns a contact more than once: "+d.id);
            }
        }
        if (!std::isfinite(d.pulseMs) || d.pulseMs < 0 || d.pulseMs > 1000) throw std::runtime_error("Latch pulse duration must be 0..1000 ms: "+d.id);
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
    if (emu51CircuitBus(c)) {
        for (const auto& d : c.devices) if (emu51PeripheralBusSize(d.kind) && !d.wiredBus)
            throw std::runtime_error("Circuit bus requires wired address/data/control pins for " + d.id);
        if (std::none_of(c.devices.begin(),c.devices.end(),[](const auto& d) { return d.kind == Emu51PeripheralKind::Clock; }))
            throw std::runtime_error("Add a Clock and connect its OUT to cpu.CLK");
        (void)emu51PeripheralClockMHz(c,12);
    }
}

int emu51KeyCount(const Emu51PeripheralDevice& d)
{
    if (d.kind == Emu51PeripheralKind::Keypad) return 16;
    return d.kind == Emu51PeripheralKind::KeyMatrix && d.rows >= 1 && d.rows <= 8 && d.columns >= 1 && d.columns <= 4 &&
        d.directKeys >= 0 && d.directKeys <= 8 ? d.rows*d.columns+d.directKeys : 0;
}
std::string emu51KeyLabel(const Emu51PeripheralDevice& d, unsigned key)
{
    if (key < d.keyLabels.size() && !d.keyLabels[key].empty()) return d.keyLabels[key];
    static const char* labels[]{"1","2","3","A","4","5","6","B","7","8","9","C","*","0","#","D"};
    return d.kind == Emu51PeripheralKind::Keypad && key < 16 ? labels[key] : std::to_string(key+1);
}

bool emu51CircuitBus(const Emu51PeripheralConfig& c)
{
    return c.circuitBus || std::any_of(c.devices.begin(),c.devices.end(),[](const auto& d) {
        return d.kind == Emu51PeripheralKind::Ram || d.kind == Emu51PeripheralKind::Rom || d.wiredBus;
    });
}
std::string emu51DecoderError(const Emu51PeripheralDevice& d)
{
    if (d.decoderBits < 1 || d.decoderBits > 32) return d.id + ": decoder requires 1..32 input bits";
    if (d.decoderOutputs.size() > 32) return d.id + ": decoder supports 32 output lines";
    std::set<std::string> ids;
    std::map<std::uint32_t,std::string> assigned;
    for (const auto& output : d.decoderOutputs) {
        if (output.id.empty() || output.id.size() > 48 || !ids.insert(output.id).second ||
            !std::all_of(output.id.begin(),output.id.end(),[](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-'; }) ||
            output.name.size() > 128 || std::any_of(output.name.begin(),output.name.end(),[](unsigned char c) { return c < 32; }))
            return d.id + ": decoder output IDs must be unique letters, digits, '_' or '-'";
        for (auto value : output.values) {
            if (d.decoderBits < 32 && value >= (UINT32_C(1) << d.decoderBits))
                return d.id + ": input value " + std::to_string(value) + " exceeds input width";
            const auto [it,inserted] = assigned.emplace(value,output.id);
            if (!inserted) return d.id + ": input value " + std::to_string(value) + " collides with output " + it->second;
        }
    }
    return {};
}
bool emu51ParseDecoderValues(const std::string& text, std::vector<std::uint32_t>& result, std::string& error)
{
    error.clear();
    std::vector<std::uint32_t> values;
    std::string normalized = text;
    std::replace(normalized.begin(),normalized.end(),',',' ');
    std::replace(normalized.begin(),normalized.end(),';',' ');
    std::istringstream input(normalized);
    std::string token;
    while (input >> token) {
        try {
            if (token.front() == '-' || token.front() == '+') throw std::invalid_argument("sign");
            int base = 10;
            if (token.back() == 'h' || token.back() == 'H') { base = 16; token.pop_back(); }
            else if (token.starts_with("0x") || token.starts_with("0X")) base = 16;
            std::size_t end = 0;
            const auto value = std::stoull(token,&end,base);
            if (end != token.size() || value > UINT32_MAX) throw std::invalid_argument("range");
            values.push_back(static_cast<std::uint32_t>(value));
        } catch (...) { error = "Invalid input value: " + token; return false; }
    }
    result = std::move(values);
    return true;
}
double emu51PeripheralClockMHz(const Emu51PeripheralConfig& c, double fallback)
{
    // The graph wiring itself forms the clock net; no second frequency setting.
    std::set<std::pair<std::string,int>> net{{"cpu.CLK",0}};
    bool changed;
    do {
        changed = false;
        for (const auto& w : c.digital) for (int bit = 0; bit < w.width; ++bit) {
            const auto a = std::pair(w.a,w.aBit+bit), b = std::pair(w.b,w.bBit+bit);
            if (net.contains(a)) changed |= net.insert(b).second;
            if (net.contains(b)) changed |= net.insert(a).second;
        }
    } while (changed);
    const Emu51PeripheralDevice* source = nullptr;
    bool anyClock = false;
    for (const auto& d : c.devices) if (d.kind == Emu51PeripheralKind::Clock) {
        anyClock = true;
        if (net.contains({d.id+".OUT",0})) {
            if (source) throw std::runtime_error("CPU clock has competing Clock sources");
            source = &d;
        }
    }
    if (anyClock && !source) throw std::runtime_error("Connect a Clock output to cpu.CLK");
    return source ? source->clockHz / 1000000.0 : fallback;
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
