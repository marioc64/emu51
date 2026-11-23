// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_project.h"
#include "emu51_editor_files.h"
#include "kason_schema.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
constexpr std::size_t kMaxDocument = 32 * 1024 * 1024;
struct StoredProgram {
    char path[4096];
    int binaryAddress;
    int fromEditor;
    char sourceText[256 * 1024 + 1];
};
struct StoredWatchpoint { int space, address, read, write, enabled; };
struct StoredEditor {
    int present, open, draft;
    char path[4096], name[4096], baseDirectory[4096], text[1024 * 1024 + 1];
    int line, column;
};
struct StoredPeripheralDevice {
    char id[49];
    int kind, address;
    double referenceVolts, voltage;
    int conversionTicks, digitalValue, audioDirections;
    double inputGainDb;
    int audioBufferMs;
    int activeLow, initiallyPressed, bounceMs, columns, rows, i2cAddress;
    double clockHz;
    char storagePath[4096];
    int displayDigits, barSegments, commonAnode, displayColon;
    double persistenceMs;
    int displayColor, displayStyle, displayAddress;
};
struct StoredPeripheralWire { char a[65], b[65]; int aBit, bBit, width; };
struct StoredAnalogWire { char from[65], to[65]; };
struct StoredPeripheralPosition { char id[49]; double x, y; char name[129]; };
struct StoredLiveBox { char id[49]; double x, y, width, height; int momentaryKeys; };
struct StoredProject {
    int version, unified, debuggerOpen, traceEnabled, cycleExact, stepsPerFrame;
    int serialTerminalOpen, serialLineEnding, serialFileLineEnding, serialFileLineDelayMs;
    char hardwareModule[128], cpuModel[16];
    double crystalMHz;
    StoredPeripheralDevice devices[32]; std::size_t deviceCount;
    StoredPeripheralWire digital[128]; std::size_t digitalCount;
    StoredAnalogWire analog[64]; std::size_t analogCount;
    StoredPeripheralPosition positions[33]; std::size_t positionCount;
    StoredLiveBox liveBoxes[33]; std::size_t liveBoxCount;
    StoredProgram programs[32];
    std::size_t programCount;
    char reloadPath[4096];
    int reloadBinaryAddress;
    StoredEditor editor;
    int breakpoints[65536];
    std::size_t breakpointCount;
    StoredWatchpoint watchpoints[1024];
    std::size_t watchpointCount;
    char watches[1024][128];
    std::size_t watchCount;
};

const kason_schema* projectSchema()
{
    static const kason_schema_field programFields[] = {
        KaSON_FIELD_STRING(StoredProgram, path, "path", KaSON_REQUIRED),
        KaSON_FIELD_INT(StoredProgram, binaryAddress, "binary_address", KaSON_DEFAULT_INT(0)),
        KaSON_FIELD_BOOL(StoredProgram, fromEditor, "from_editor", KaSON_DEFAULT_BOOL(false)),
        KaSON_FIELD_STRING(StoredProgram, sourceText, "source_text", KaSON_DEFAULT_STRING("")),
    };
    KaSON_SCHEMA_DEFINE(programSchema, StoredProgram, programFields, 8);
    static const kason_schema_field watchFields[] = {
        KaSON_FIELD_INT(StoredWatchpoint, space, "space", KaSON_REQUIRED),
        KaSON_FIELD_INT(StoredWatchpoint, address, "address", KaSON_REQUIRED),
        KaSON_FIELD_BOOL(StoredWatchpoint, read, "read", KaSON_REQUIRED),
        KaSON_FIELD_BOOL(StoredWatchpoint, write, "write", KaSON_REQUIRED),
        KaSON_FIELD_BOOL(StoredWatchpoint, enabled, "enabled", KaSON_REQUIRED),
    };
    KaSON_SCHEMA_DEFINE(watchSchema, StoredWatchpoint, watchFields, 16);
    static const kason_schema_field editorFields[] = {
        KaSON_FIELD_BOOL(StoredEditor, present, "present", KaSON_REQUIRED),
        KaSON_FIELD_BOOL(StoredEditor, open, "open", KaSON_REQUIRED),
        KaSON_FIELD_BOOL(StoredEditor, draft, "draft", KaSON_REQUIRED),
        KaSON_FIELD_STRING(StoredEditor, path, "path", KaSON_REQUIRED),
        KaSON_FIELD_STRING(StoredEditor, name, "name", KaSON_REQUIRED),
        KaSON_FIELD_STRING(StoredEditor, baseDirectory, "base_directory", KaSON_REQUIRED),
        KaSON_FIELD_STRING(StoredEditor, text, "text", KaSON_REQUIRED),
        KaSON_FIELD_INT(StoredEditor, line, "line", KaSON_REQUIRED),
        KaSON_FIELD_INT(StoredEditor, column, "column", KaSON_REQUIRED),
    };
    KaSON_SCHEMA_DEFINE(editorSchema, StoredEditor, editorFields, 32);
    static const kason_schema_field deviceFields[] = {
        KaSON_FIELD_STRING(StoredPeripheralDevice, id, "id", KaSON_REQUIRED),
        KaSON_FIELD_INT(StoredPeripheralDevice, kind, "kind", KaSON_REQUIRED),
        KaSON_FIELD_INT(StoredPeripheralDevice, address, "address", KaSON_DEFAULT_INT(32768)),
        KaSON_FIELD_DOUBLE(StoredPeripheralDevice, referenceVolts, "reference_volts", KaSON_DEFAULT_DOUBLE(5)),
        KaSON_FIELD_DOUBLE(StoredPeripheralDevice, voltage, "voltage", KaSON_DEFAULT_DOUBLE(2.5)),
        KaSON_FIELD_INT(StoredPeripheralDevice, conversionTicks, "conversion_ticks", KaSON_DEFAULT_INT(768)),
        KaSON_FIELD_INT(StoredPeripheralDevice, digitalValue, "digital_value", KaSON_DEFAULT_INT(0)),
        KaSON_FIELD_INT(StoredPeripheralDevice, audioDirections, "audio_directions", KaSON_DEFAULT_INT(2)),
        KaSON_FIELD_DOUBLE(StoredPeripheralDevice, inputGainDb, "input_gain_db", KaSON_DEFAULT_DOUBLE(0)),
        KaSON_FIELD_INT(StoredPeripheralDevice, audioBufferMs, "audio_buffer_ms", KaSON_DEFAULT_INT(0)),
        KaSON_FIELD_BOOL(StoredPeripheralDevice, activeLow, "active_low", KaSON_DEFAULT_BOOL(true)),
        KaSON_FIELD_BOOL(StoredPeripheralDevice, initiallyPressed, "initially_pressed", KaSON_DEFAULT_BOOL(false)),
        KaSON_FIELD_INT(StoredPeripheralDevice, bounceMs, "bounce_ms", KaSON_DEFAULT_INT(0)),
        KaSON_FIELD_INT(StoredPeripheralDevice, columns, "columns", KaSON_DEFAULT_INT(16)),
        KaSON_FIELD_INT(StoredPeripheralDevice, rows, "rows", KaSON_DEFAULT_INT(2)),
        KaSON_FIELD_INT(StoredPeripheralDevice, i2cAddress, "i2c_address", KaSON_DEFAULT_INT(80)),
        KaSON_FIELD_DOUBLE(StoredPeripheralDevice, clockHz, "clock_hz", KaSON_DEFAULT_DOUBLE(2000000)),
        KaSON_FIELD_STRING(StoredPeripheralDevice, storagePath, "storage_path", KaSON_DEFAULT_STRING("")),
        KaSON_FIELD_INT(StoredPeripheralDevice, displayDigits, "display_digits", KaSON_DEFAULT_INT(4)),
        KaSON_FIELD_INT(StoredPeripheralDevice, barSegments, "bar_segments", KaSON_DEFAULT_INT(10)),
        KaSON_FIELD_BOOL(StoredPeripheralDevice, commonAnode, "common_anode", KaSON_DEFAULT_BOOL(false)),
        KaSON_FIELD_BOOL(StoredPeripheralDevice, displayColon, "display_colon", KaSON_DEFAULT_BOOL(false)),
        KaSON_FIELD_DOUBLE(StoredPeripheralDevice, persistenceMs, "persistence_ms", KaSON_DEFAULT_DOUBLE(20)),
        KaSON_FIELD_INT(StoredPeripheralDevice, displayColor, "display_color", KaSON_DEFAULT_INT(0)),
        KaSON_FIELD_INT(StoredPeripheralDevice, displayStyle, "display_style", KaSON_DEFAULT_INT(7)),
        KaSON_FIELD_INT(StoredPeripheralDevice, displayAddress, "display_address", KaSON_DEFAULT_INT(112)),
    };
    KaSON_SCHEMA_DEFINE(deviceSchema, StoredPeripheralDevice, deviceFields, 32);
    static const kason_schema_field digitalFields[] = {
        KaSON_FIELD_STRING(StoredPeripheralWire, a, "a", KaSON_REQUIRED),
        KaSON_FIELD_STRING(StoredPeripheralWire, b, "b", KaSON_REQUIRED),
        KaSON_FIELD_INT(StoredPeripheralWire, aBit, "a_bit", KaSON_REQUIRED),
        KaSON_FIELD_INT(StoredPeripheralWire, bBit, "b_bit", KaSON_REQUIRED),
        KaSON_FIELD_INT(StoredPeripheralWire, width, "width", KaSON_REQUIRED),
    };
    KaSON_SCHEMA_DEFINE(digitalSchema, StoredPeripheralWire, digitalFields, 16);
    static const kason_schema_field analogFields[] = {
        KaSON_FIELD_STRING(StoredAnalogWire, from, "from", KaSON_REQUIRED),
        KaSON_FIELD_STRING(StoredAnalogWire, to, "to", KaSON_REQUIRED),
    };
    KaSON_SCHEMA_DEFINE(analogSchema, StoredAnalogWire, analogFields, 8);
    static const kason_schema_field positionFields[] = {
        KaSON_FIELD_STRING(StoredPeripheralPosition, id, "id", KaSON_REQUIRED),
        KaSON_FIELD_DOUBLE(StoredPeripheralPosition, x, "x", KaSON_REQUIRED),
        KaSON_FIELD_DOUBLE(StoredPeripheralPosition, y, "y", KaSON_REQUIRED),
        KaSON_FIELD_STRING(StoredPeripheralPosition, name, "name", KaSON_DEFAULT_STRING("")),
    };
    KaSON_SCHEMA_DEFINE(positionSchema, StoredPeripheralPosition, positionFields, 8);
    static const kason_schema_field liveBoxFields[] = {
        KaSON_FIELD_STRING(StoredLiveBox, id, "id", KaSON_REQUIRED),
        KaSON_FIELD_DOUBLE(StoredLiveBox, x, "x", KaSON_REQUIRED),
        KaSON_FIELD_DOUBLE(StoredLiveBox, y, "y", KaSON_REQUIRED),
        KaSON_FIELD_DOUBLE(StoredLiveBox, width, "width", KaSON_REQUIRED),
        KaSON_FIELD_DOUBLE(StoredLiveBox, height, "height", KaSON_REQUIRED),
        KaSON_FIELD_BOOL(StoredLiveBox, momentaryKeys, "momentary_keys", KaSON_DEFAULT_BOOL(false)),
    };
    KaSON_SCHEMA_DEFINE(liveBoxSchema, StoredLiveBox, liveBoxFields, 8);
    static const kason_schema_field fields[] = {
        KaSON_FIELD_INT(StoredProject, version, "version", KaSON_REQUIRED),
        KaSON_FIELD_BOOL(StoredProject, unified, "unified_memory", KaSON_REQUIRED),
        KaSON_FIELD_BOOL(StoredProject, debuggerOpen, "debugger_open", KaSON_REQUIRED),
        KaSON_FIELD_BOOL(StoredProject, traceEnabled, "trace_enabled", KaSON_REQUIRED),
        KaSON_FIELD_BOOL(StoredProject, serialTerminalOpen, "serial_terminal_open", KaSON_DEFAULT_BOOL(false)),
        KaSON_FIELD_INT(StoredProject, serialLineEnding, "serial_input_ending", KaSON_DEFAULT_INT(0)),
        KaSON_FIELD_INT(StoredProject, serialFileLineEnding, "serial_file_ending", KaSON_DEFAULT_INT(0)),
        KaSON_FIELD_INT(StoredProject, serialFileLineDelayMs, "serial_file_line_delay_ms", KaSON_DEFAULT_INT(0)),
        KaSON_FIELD_STRING(StoredProject, cpuModel, "cpu_model", KaSON_DEFAULT_STRING("8051")),
        KaSON_FIELD_STRING(StoredProject, hardwareModule, "hardware_module", KaSON_DEFAULT_STRING("")),
        KaSON_FIELD_STRUCT_ARRAY(StoredProject, devices, deviceCount, "peripheral_devices", &deviceSchema, KaSON_DEFAULT_EMPTY_ARRAY),
        KaSON_FIELD_STRUCT_ARRAY(StoredProject, digital, digitalCount, "peripheral_digital_wires", &digitalSchema, KaSON_DEFAULT_EMPTY_ARRAY),
        KaSON_FIELD_STRUCT_ARRAY(StoredProject, analog, analogCount, "peripheral_analog_wires", &analogSchema, KaSON_DEFAULT_EMPTY_ARRAY),
        KaSON_FIELD_STRUCT_ARRAY(StoredProject, positions, positionCount, "peripheral_layout", &positionSchema, KaSON_DEFAULT_EMPTY_ARRAY),
        KaSON_FIELD_STRUCT_ARRAY(StoredProject, liveBoxes, liveBoxCount, "peripheral_live_layout", &liveBoxSchema, KaSON_DEFAULT_EMPTY_ARRAY),
        KaSON_FIELD_BOOL(StoredProject, cycleExact, "cycle_exact", KaSON_REQUIRED),
        KaSON_FIELD_DOUBLE(StoredProject, crystalMHz, "crystal_mhz", KaSON_REQUIRED),
        KaSON_FIELD_INT(StoredProject, stepsPerFrame, "instructions_per_frame", KaSON_REQUIRED),
        KaSON_FIELD_STRUCT_ARRAY(StoredProject, programs, programCount, "programs", &programSchema, KaSON_REQUIRED),
        KaSON_FIELD_STRING(StoredProject, reloadPath, "reload_program", KaSON_REQUIRED),
        KaSON_FIELD_INT(StoredProject, reloadBinaryAddress, "reload_binary_address", KaSON_REQUIRED),
        KaSON_FIELD_STRUCT(StoredProject, editor, "editor", &editorSchema, KaSON_REQUIRED),
        KaSON_FIELD_INT_ARRAY(StoredProject, breakpoints, breakpointCount, "breakpoints", KaSON_REQUIRED),
        KaSON_FIELD_STRUCT_ARRAY(StoredProject, watchpoints, watchpointCount, "watchpoints", &watchSchema, KaSON_REQUIRED),
        KaSON_FIELD_STRING_ARRAY(StoredProject, watches, watchCount, "watches", KaSON_REQUIRED),
    };
    KaSON_SCHEMA_DEFINE(schema, StoredProject, fields, 32);
    static const bool initialized = kason_schema_init(&programSchema) == KaSON_SCHEMA_SUCCESS &&
        kason_schema_init(&watchSchema) == KaSON_SCHEMA_SUCCESS &&
        kason_schema_init(&editorSchema) == KaSON_SCHEMA_SUCCESS &&
        kason_schema_init(&deviceSchema) == KaSON_SCHEMA_SUCCESS &&
        kason_schema_init(&digitalSchema) == KaSON_SCHEMA_SUCCESS &&
        kason_schema_init(&analogSchema) == KaSON_SCHEMA_SUCCESS &&
        kason_schema_init(&positionSchema) == KaSON_SCHEMA_SUCCESS &&
        kason_schema_init(&liveBoxSchema) == KaSON_SCHEMA_SUCCESS && kason_schema_init(&schema) == KaSON_SCHEMA_SUCCESS;
    return initialized ? &schema : nullptr;
}

fs::path projectBase(const fs::path& path) { return fs::weakly_canonical(fs::absolute(path).parent_path()); }
std::string resolvePath(const char* path, const fs::path& base)
{
    const std::u8string encoded(path, path + std::strlen(path));
    return *path ? (base / fs::path(encoded)).lexically_normal().string() : std::string{};
}
std::string portablePath(const std::string& path, const fs::path& base)
{
    if (path.empty()) return {};
    const auto absolute = fs::weakly_canonical(fs::absolute(fs::path(path)));
    const auto relative = absolute.lexically_relative(base);
    const auto portable = (relative.empty() ? absolute : relative).generic_u8string();
    return std::string(portable.begin(), portable.end());
}
template<std::size_t N> void copyText(char (&to)[N], const std::string& from)
{
    if (from.size() >= N || from.find('\0') != std::string::npos)
        throw std::runtime_error("project text or collection exceeds the supported size");
    std::memcpy(to, from.c_str(), from.size() + 1);
}
bool addressValid(int value) { return value >= 0 && value <= 65535; }

Emu51Project decode(const StoredProject& s, const fs::path& base)
{
    if (s.version != 1) throw std::runtime_error("unsupported project version");
    if (!Emu51CycleTiming::validCrystalMHz(s.crystalMHz) || s.stepsPerFrame < 1 || s.stepsPerFrame > 1000000 ||
        !addressValid(s.reloadBinaryAddress) || s.editor.line < 0 || s.editor.column < 0)
        throw std::runtime_error("invalid execution settings, cursor, or load address");
    const auto model = emu51ParseModel(s.cpuModel);
    if (!model) throw std::runtime_error("unsupported CPU model");
    if (s.serialLineEnding < 0 || s.serialLineEnding > 3 || s.serialFileLineEnding < 0 || s.serialFileLineEnding > 3 ||
        s.serialFileLineDelayMs < 0 || s.serialFileLineDelayMs > 10000)
        throw std::runtime_error("invalid serial transfer settings");
    Emu51Project p;
    p.cpuModel = *model;
    p.unified = s.unified; p.debuggerOpen = s.debuggerOpen; p.traceEnabled = s.traceEnabled;
    p.serialTerminalOpen = s.serialTerminalOpen;
    p.serialLineEnding = s.serialLineEnding;
    p.serialFileLineEnding = s.serialFileLineEnding;
    p.serialFileLineDelayMs = s.serialFileLineDelayMs;
    p.hardwareModule = s.hardwareModule;
    for (std::size_t i = 0; i < s.deviceCount; ++i) {
        const auto& d = s.devices[i];
        p.peripherals.devices.push_back({d.id, static_cast<Emu51PeripheralKind>(d.kind), d.address,
            d.referenceVolts, d.voltage, d.conversionTicks, d.digitalValue, d.audioDirections, d.inputGainDb, d.audioBufferMs, d.activeLow != 0, d.initiallyPressed != 0,
            d.bounceMs, d.columns, d.rows, d.i2cAddress, d.clockHz, resolvePath(d.storagePath,base),
            d.displayDigits, d.barSegments, d.commonAnode != 0, d.displayColon != 0, d.persistenceMs,
            d.displayColor, d.displayStyle, d.displayAddress});
    }
    for (std::size_t i = 0; i < s.digitalCount; ++i) {
        const auto& w = s.digital[i]; p.peripherals.digital.push_back({w.a, w.b, w.aBit, w.bBit, w.width});
    }
    for (std::size_t i = 0; i < s.analogCount; ++i)
        p.peripherals.analog.push_back({s.analog[i].from, s.analog[i].to});
    emu51ValidatePeripheralConfig(p.peripherals);
    std::set<std::string> positioned;
    for (std::size_t i = 0; i < s.positionCount; ++i) {
        const auto& n = s.positions[i];
        if (!std::isfinite(n.x) || !std::isfinite(n.y) || std::abs(n.x) > 100000 || std::abs(n.y) > 100000 ||
            std::any_of(n.name,n.name+std::strlen(n.name),[](unsigned char ch) { return ch < 32; }) ||
            !positioned.insert(n.id).second || (std::string(n.id) != "cpu" &&
                std::none_of(p.peripherals.devices.begin(),p.peripherals.devices.end(),[&](const auto& d) { return d.id == n.id; })))
            throw std::runtime_error("Invalid peripheral block position");
        p.peripheralLayout.push_back({n.id,n.x,n.y,n.name});
    }
    positioned.clear();
    for (std::size_t i = 0; i < s.liveBoxCount; ++i) {
        const auto& b = s.liveBoxes[i];
        if (!std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(b.width) || !std::isfinite(b.height) ||
            b.x < 0 || b.y < 0 || b.x > 100000 || b.y > 100000 || b.width < 80 || b.height < 48 || b.width > 4096 || b.height > 4096 ||
            !positioned.insert(b.id).second || (std::string(b.id) != "cpu" &&
                std::none_of(p.peripherals.devices.begin(),p.peripherals.devices.end(),[&](const auto& d) { return d.id == b.id; })))
            throw std::runtime_error("Invalid peripheral Live box layout");
        p.peripheralLiveLayout.push_back({b.id,b.x,b.y,b.width,b.height,b.momentaryKeys != 0});
    }
    if (!p.hardwareModule.empty() && !p.peripherals.devices.empty())
        throw std::runtime_error("A project cannot enable both a hardware module and a peripheral graph");
    p.execution = {s.cycleExact != 0, s.crystalMHz, s.stepsPerFrame};
    p.reloadPath = resolvePath(s.reloadPath, base);
    p.reloadBinaryAddress = static_cast<std::uint16_t>(s.reloadBinaryAddress);
    p.editor = {s.editor.present != 0, s.editor.open != 0, s.editor.draft != 0,
        resolvePath(s.editor.path, base), s.editor.name, resolvePath(s.editor.baseDirectory, base),
        s.editor.text, s.editor.line, s.editor.column};
    if (p.editor.present && (p.editor.name.empty() || (!p.editor.draft && p.editor.path.empty())))
        throw std::runtime_error("editor requires a source file or a recovery draft");
    if ((!p.editor.present && (p.editor.draft || !p.editor.path.empty() || !p.editor.text.empty())) ||
        (!p.editor.draft && !p.editor.text.empty()))
        throw std::runtime_error("inconsistent editor draft state");
    for (std::size_t i = 0; i < s.programCount; ++i) {
        const auto& program = s.programs[i];
        if (!addressValid(program.binaryAddress) || !*program.path || (!program.fromEditor && *program.sourceText))
            throw std::runtime_error("invalid program entry");
        p.programs.push_back({resolvePath(program.path, base), static_cast<std::uint16_t>(program.binaryAddress),
            program.fromEditor != 0, program.sourceText});
    }
    for (std::size_t i = 0; i < s.breakpointCount; ++i) {
        if (!addressValid(s.breakpoints[i])) throw std::runtime_error("invalid breakpoint address");
        p.breakpoints.insert(static_cast<std::uint16_t>(s.breakpoints[i]));
    }
    for (std::size_t i = 0; i < s.watchpointCount; ++i) {
        const auto& w = s.watchpoints[i];
        if (w.space < 0 || w.space > 3 || !addressValid(w.address) || (!w.read && !w.write) ||
            ((w.space == 0 || w.space == 3) && (w.address > 255 || w.read || !w.write)))
            throw std::runtime_error("invalid watchpoint");
        p.watchpoints.push_back({w.space, static_cast<std::uint16_t>(w.address), w.read != 0, w.write != 0, w.enabled != 0});
    }
    for (std::size_t i = 0; i < s.watchCount; ++i) {
        std::string name = s.watches[i];
        if (name.empty()) throw std::runtime_error("empty symbol watch");
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        p.watches.insert(std::move(name));
    }
    return p;
}
} // namespace

std::optional<Emu51Project> emu51LoadProject(const fs::path& path, std::string& error)
{
    error.clear();
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("unable to open project file");
        std::string json;
        char chunk[4096];
        while (input.read(chunk, sizeof(chunk)) || input.gcount()) {
            json.append(chunk, static_cast<std::size_t>(input.gcount()));
            if (json.size() > kMaxDocument) throw std::runtime_error("project file exceeds 32 MiB");
        }
        if (input.bad() || !input.eof() || json.empty()) throw std::runtime_error("unable to read project file");
        const auto* schema = projectSchema();
        auto stored = std::make_unique<StoredProject>();
        kason_schema_error detail{};
        if (!schema || kason_unpack_range(json.data(), json.data() + json.size() - 1, schema, stored.get(), &detail) != KaSON_SCHEMA_SUCCESS)
            throw std::runtime_error(std::string("invalid project JSON") +
                (detail.field ? std::string(" at '") + detail.field->json_key + "'" : ""));
        return decode(*stored, projectBase(path));
    } catch (const std::exception& e) { error = e.what(); return std::nullopt; }
}

bool emu51SaveProject(const fs::path& path, const Emu51Project& p, std::string& error)
{
    error.clear();
    try {
        if (path.empty()) throw std::runtime_error("project path is empty");
        auto s = std::make_unique<StoredProject>();
        if (p.programs.size() > std::size(s->programs) || p.watchpoints.size() > std::size(s->watchpoints) ||
            p.watches.size() > std::size(s->watches) || p.peripheralLayout.size() > std::size(s->positions) ||
            p.peripheralLiveLayout.size() > std::size(s->liveBoxes))
            throw std::runtime_error("project has too many program loads, watches, or peripheral block positions");
        const auto base = projectBase(path);
        copyText(s->cpuModel, emu51ModelInfo(p.cpuModel).id);
        s->version = 1; s->unified = p.unified; s->debuggerOpen = p.debuggerOpen; s->traceEnabled = p.traceEnabled;
        s->serialTerminalOpen = p.serialTerminalOpen;
        s->serialLineEnding = p.serialLineEnding;
        s->serialFileLineEnding = p.serialFileLineEnding;
        s->serialFileLineDelayMs = p.serialFileLineDelayMs;
        copyText(s->hardwareModule, p.hardwareModule);
        emu51ValidatePeripheralConfig(p.peripherals);
        for (const auto& d : p.peripherals.devices) {
            auto& to = s->devices[s->deviceCount++]; copyText(to.id, d.id);
            to.kind = static_cast<int>(d.kind); to.address = d.address; to.referenceVolts = d.referenceVolts;
            to.voltage = d.voltage; to.conversionTicks = d.conversionTicks;
            to.digitalValue = d.digitalValue; to.audioDirections = d.audioDirections; to.inputGainDb = d.inputGainDb; to.audioBufferMs = d.audioBufferMs;
            to.activeLow = d.activeLow; to.initiallyPressed = d.initiallyPressed; to.bounceMs = d.bounceMs;
            to.columns = d.columns; to.rows = d.rows; to.i2cAddress = d.i2cAddress; to.clockHz = d.clockHz;
            copyText(to.storagePath,portablePath(d.storagePath,base));
            to.displayDigits=d.displayDigits; to.barSegments=d.barSegments; to.commonAnode=d.commonAnode;
            to.displayColon=d.displayColon; to.persistenceMs=d.persistenceMs; to.displayColor=d.displayColor;
            to.displayStyle=d.displayStyle; to.displayAddress=d.displayAddress;
        }
        for (const auto& w : p.peripherals.digital) {
            auto& to = s->digital[s->digitalCount++]; copyText(to.a, w.a); copyText(to.b, w.b);
            to.aBit = w.aBit; to.bBit = w.bBit; to.width = w.width;
        }
        for (const auto& w : p.peripherals.analog) {
            auto& to = s->analog[s->analogCount++]; copyText(to.from, w.from); copyText(to.to, w.to);
        }
        for (const auto& n : p.peripheralLayout) {
            auto& to = s->positions[s->positionCount++]; copyText(to.id,n.id); to.x = n.x; to.y = n.y;
            copyText(to.name,n.name);
        }
        for (const auto& b : p.peripheralLiveLayout) {
            auto& to = s->liveBoxes[s->liveBoxCount++]; copyText(to.id,b.id);
            to.x = b.x; to.y = b.y; to.width = b.width; to.height = b.height;
            to.momentaryKeys = b.momentaryKeys;
        }
        s->cycleExact = p.execution.cycleExact; s->crystalMHz = p.execution.crystalMHz; s->stepsPerFrame = p.execution.stepsPerFrame;
        copyText(s->reloadPath, portablePath(p.reloadPath, base)); s->reloadBinaryAddress = p.reloadBinaryAddress;
        auto& editor = s->editor;
        editor.present = p.editor.present; editor.open = p.editor.open; editor.draft = p.editor.draft;
        copyText(editor.path, portablePath(p.editor.path, base)); copyText(editor.name, p.editor.name);
        copyText(editor.baseDirectory, portablePath(p.editor.baseDirectory, base)); copyText(editor.text, p.editor.text);
        editor.line = p.editor.line; editor.column = p.editor.column;
        for (const auto& program : p.programs) {
            auto& target = s->programs[s->programCount++];
            copyText(target.path, portablePath(program.path, base)); copyText(target.sourceText, program.sourceText);
            target.binaryAddress = program.binaryAddress; target.fromEditor = program.fromEditor;
        }
        for (const auto value : p.breakpoints) s->breakpoints[s->breakpointCount++] = value;
        for (const auto& watch : p.watchpoints)
            s->watchpoints[s->watchpointCount++] = {watch.space, watch.address, watch.read, watch.write, watch.enabled};
        for (const auto& name : p.watches) copyText(s->watches[s->watchCount++], name);
        (void)decode(*s, base); // Use the same semantic validation in both directions.
        const auto* schema = projectSchema();
        kason_writer writer{};
        if (!schema || kason_writer_init_counter(&writer) != KaSON_SCHEMA_SUCCESS ||
            kason_pack(&writer, schema, s.get(), 0, nullptr) != KaSON_SCHEMA_SUCCESS || writer.length >= kMaxDocument)
            throw std::runtime_error("unable to encode project");
        std::string json(writer.length + 1, '\0');
        if (kason_writer_init_buffer(&writer, json.data(), json.size()) != KaSON_SCHEMA_SUCCESS ||
            kason_pack(&writer, schema, s.get(), 0, nullptr) != KaSON_SCHEMA_SUCCESS)
            throw std::runtime_error("unable to encode project");
        json.resize(writer.length); json += '\n';
        if (!emu51WriteTextFileAtomically(path, json)) throw std::runtime_error("unable to save project; original file preserved");
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}
