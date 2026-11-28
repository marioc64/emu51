// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <string>
#include <cstdint>
#include <vector>

enum class Emu51PeripheralKind { I8255, Adc0804, Dac, Voltage, Digital, Audio,
    Led, Switch, Button, Lcd, Keypad, Shift595, Shift165, Eeprom, Ay38910,
    Display7, Display14, Display16, Bargraph, Cd4511, Ls47, Max7219, Max7221, Ht16k33, Tm1637,
    Ram, Rom, Clock, AddressDecoder, BusLatch, BusBuffer, KeyMatrix, Inverter, Ssd1306, Ds1307, Mcp23017, W25q64, LogicAnalyzer, Eeprom256, Uln2003, Stepper, Adc0809 };
struct Emu51MemoryImage {
    std::string path;
    std::uint64_t offset = 0, length = 0;
    bool operator==(const Emu51MemoryImage&) const = default;
};
struct Emu51DecoderOutput {
    std::string id;
    std::string name = {};
    bool activeLow = true;
    std::vector<std::uint32_t> values = {};
    bool operator==(const Emu51DecoderOutput&) const = default;
};
struct Emu51PeripheralDevice {
    std::string id;
    Emu51PeripheralKind kind = Emu51PeripheralKind::I8255;
    int address = 0x8000;
    double referenceVolts = 5.0;
    double voltage = 2.5;
    int conversionTicks = 768;
    int digitalValue = 0;
    int audioDirections = 2; // capture=1, playback=2
    double inputGainDb = 0; // live microphone gain, -20..60 dB
    int audioBufferMs = 0; // 0=automatic; otherwise 10..2000 ms
    bool activeLow = true;
    bool initiallyPressed = false;
    int bounceMs = 0;
    int columns = 16, rows = 2, i2cAddress = 0x50;
    double clockHz = 2000000;
    std::string storagePath = {};
    int displayDigits = 4, barSegments = 10;
    bool commonAnode = false, displayColon = false;
    double persistenceMs = 20;
    int displayColor = 0, displayStyle = 7, displayAddress = 0x70;
    std::uint64_t memoryCapacity = 65536;
    int memoryFill = 0;
    bool csLow = true, oeLow = true, weLow = true;
    std::vector<Emu51MemoryImage> memoryImages = {};
    int decoderBits = 4;
    std::vector<Emu51DecoderOutput> decoderOutputs = {};
    bool wiredBus = false;
    std::vector<std::string> keyLabels = {};
    double pulseMs = 0;
    int directKeys = 0;
    int keyLayoutColumns = 0, keyLayoutRows = 0;
    std::vector<int> keyLayout = {}; // visual row-major slots; -1 = empty, otherwise electrical key index
    std::vector<std::string> keyLegends = {};
    int serialAddress = 0, rtcDay = 0, rtcSecond = 0;
    bool operator==(const Emu51PeripheralDevice&) const = default;
};
struct Emu51PeripheralWire {
    std::string a, b; // stable names, e.g. cpu.P1, ppi.A, adc.CS
    int aBit = 0, bBit = 0, width = 1;
    bool operator==(const Emu51PeripheralWire&) const = default;
};
struct Emu51PeripheralAnalogWire {
    std::string from, to;
    bool operator==(const Emu51PeripheralAnalogWire&) const = default;
};
struct Emu51PeripheralConfig {
    std::vector<Emu51PeripheralDevice> devices;
    std::vector<Emu51PeripheralWire> digital;
    std::vector<Emu51PeripheralAnalogWire> analog;
    bool circuitBus = false;
    bool operator==(const Emu51PeripheralConfig&) const = default;
};
// Project presentation only: moving a block never changes the active circuit.
struct Emu51PeripheralBlockPosition {
    std::string id;
    double x = 0, y = 0;
    std::string name = {}; // presentation label; endpoint IDs remain stable
    bool operator==(const Emu51PeripheralBlockPosition&) const = default;
};
struct Emu51PeripheralLiveBox {
    std::string id;
    double x = 0, y = 0, width = 360, height = 240;
    bool momentaryKeys = false;
    bool operator==(const Emu51PeripheralLiveBox&) const = default;
};
struct Emu51PeripheralLiveLabel {
    static constexpr unsigned maxCount = 64, textCapacity = 256;
    std::string text;
    double x = 0, y = 0;
    bool operator==(const Emu51PeripheralLiveLabel&) const = default;
};
bool emu51ValidLiveLabel(const Emu51PeripheralLiveLabel&);
// Preset 0: empty; 1: P1 -> DAC; 2: XDATA -> 8255 -> ADC/DAC;
// 3: microphone -> ADC -> P0, P1 -> Covox -> default host playback.
// 4: two-voice music on P1 -> Covox -> playback only.
Emu51PeripheralConfig emu51PeripheralPreset(int preset);
const char* emu51PeripheralKindName(Emu51PeripheralKind kind);
inline constexpr bool emu51ValidAudioBufferMs(int ms) { return ms == 0 || (ms >= 10 && ms <= 2000); }
// Structural validation. Session construction also validates endpoints and nets.
void emu51ValidatePeripheralConfig(const Emu51PeripheralConfig& config);
bool emu51CircuitBus(const Emu51PeripheralConfig& config);
double emu51PeripheralClockMHz(const Emu51PeripheralConfig& config, double fallbackMHz);
std::string emu51DecoderError(const Emu51PeripheralDevice&);
bool emu51ParseDecoderValues(const std::string&, std::vector<std::uint32_t>&, std::string& error);
int emu51KeyCount(const Emu51PeripheralDevice&);
std::string emu51KeyLabel(const Emu51PeripheralDevice&, unsigned key);

inline constexpr int emu51PeripheralComponentKind(Emu51PeripheralKind kind) {
    if (kind >= Emu51PeripheralKind::BusLatch && kind <= Emu51PeripheralKind::KeyMatrix)
        return 9+static_cast<int>(kind)-static_cast<int>(Emu51PeripheralKind::BusLatch);
    if (kind >= Emu51PeripheralKind::Eeprom256 && kind <= Emu51PeripheralKind::Adc0809)
        return 12+static_cast<int>(kind)-static_cast<int>(Emu51PeripheralKind::Eeprom256);
    return kind >= Emu51PeripheralKind::Led && kind <= Emu51PeripheralKind::Ay38910 ?
        static_cast<int>(kind)-static_cast<int>(Emu51PeripheralKind::Led) : -1;
}
inline constexpr unsigned emu51PeripheralEepromSize(Emu51PeripheralKind kind) {
    return kind == Emu51PeripheralKind::Eeprom256 ? 32768 : kind == Emu51PeripheralKind::Eeprom ? 256 : 0;
}
inline constexpr int emu51PeripheralBusSize(Emu51PeripheralKind kind) {
    return kind == Emu51PeripheralKind::I8255 ? 4 : kind == Emu51PeripheralKind::Ay38910 ? 2 :
        (kind == Emu51PeripheralKind::BusLatch || kind == Emu51PeripheralKind::BusBuffer) ? 1 : 0;
}

inline constexpr int emu51PeripheralSerialKind(Emu51PeripheralKind kind) {
    return kind >= Emu51PeripheralKind::Ssd1306 && kind <= Emu51PeripheralKind::LogicAnalyzer ?
        static_cast<int>(kind)-static_cast<int>(Emu51PeripheralKind::Ssd1306) : -1;
}

inline constexpr int emu51PeripheralDisplayKind(Emu51PeripheralKind kind) {
    return kind >= Emu51PeripheralKind::Display7 && kind <= Emu51PeripheralKind::Tm1637 ?
        static_cast<int>(kind)-static_cast<int>(Emu51PeripheralKind::Display7) : -1;
}
