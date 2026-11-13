// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_project.h"
#include "emu51_editor_files.h"
#include "kason_schema.h"

#include <algorithm>
#include <cctype>
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
struct StoredProject {
    int version, unified, debuggerOpen, traceEnabled, cycleExact, stepsPerFrame;
    double crystalMHz;
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
    static const kason_schema_field fields[] = {
        KaSON_FIELD_INT(StoredProject, version, "version", KaSON_REQUIRED),
        KaSON_FIELD_BOOL(StoredProject, unified, "unified_memory", KaSON_REQUIRED),
        KaSON_FIELD_BOOL(StoredProject, debuggerOpen, "debugger_open", KaSON_REQUIRED),
        KaSON_FIELD_BOOL(StoredProject, traceEnabled, "trace_enabled", KaSON_REQUIRED),
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
        kason_schema_init(&editorSchema) == KaSON_SCHEMA_SUCCESS && kason_schema_init(&schema) == KaSON_SCHEMA_SUCCESS;
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
    Emu51Project p;
    p.unified = s.unified; p.debuggerOpen = s.debuggerOpen; p.traceEnabled = s.traceEnabled;
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
        if (w.space < 0 || w.space > 2 || !addressValid(w.address) || (!w.read && !w.write) ||
            (w.space == 0 && (w.address > 255 || w.read || !w.write)))
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
            p.watches.size() > std::size(s->watches)) throw std::runtime_error("project has too many program loads or watches");
        const auto base = projectBase(path);
        s->version = 1; s->unified = p.unified; s->debuggerOpen = p.debuggerOpen; s->traceEnabled = p.traceEnabled;
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
