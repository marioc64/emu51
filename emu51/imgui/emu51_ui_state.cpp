// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_ui_layout_schema.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>

namespace {
constexpr std::size_t kMaxDocument = 1024 * 1024;
struct StoredState {
    int version;
    emu51_ui_layout::StoredLayout layout;
    char recentPrograms[10][4096]; std::size_t programCount;
    char recentProjects[10][4096]; std::size_t projectCount;
};

const kason_schema* stateSchema()
{
    const auto* layoutSchema = emu51_ui_layout::schema();
    static const kason_schema_field fields[] = {
        KaSON_FIELD_INT(StoredState, version, "version", KaSON_REQUIRED),
        KaSON_FIELD_STRUCT(StoredState, layout, "layout", layoutSchema, KaSON_REQUIRED),
        KaSON_FIELD_STRING_ARRAY(StoredState, recentPrograms, programCount, "recent_programs", KaSON_DEFAULT_EMPTY_ARRAY),
        KaSON_FIELD_STRING_ARRAY(StoredState, recentProjects, projectCount, "recent_projects", KaSON_DEFAULT_EMPTY_ARRAY),
    };
    KaSON_SCHEMA_DEFINE(schema, StoredState, fields, 8);
    static const bool initialized = layoutSchema && kason_schema_init(&schema) == KaSON_SCHEMA_SUCCESS;
    return initialized ? &schema : nullptr;
}

template<std::size_t N> bool copyText(char (&to)[N], const std::string& from)
{
    if (from.size() >= N || from.find('\0') != std::string::npos) return false;
    std::memcpy(to, from.c_str(), from.size()+1);
    return true;
}

bool validPane(const Emu51UiPaneState& pane)
{
    const bool knownKind = pane.kind == "registers" || pane.kind == "peripherals" || pane.kind == "assembly_editor" || pane.kind == "project" ||
        pane.kind == "disassembly" || pane.kind == "internal_ram" || pane.kind == "monitor" ||
        pane.kind == "debugger" || pane.kind == "serial_terminal";
    const bool knownTarget = pane.target == "none" || pane.target == "left" || pane.target == "right" ||
        pane.target == "top" || pane.target == "bottom" || pane.target == "center";
    return knownKind && knownTarget && pane.id >= 0 && pane.id < std::numeric_limits<int>::max() &&
        ((pane.kind != "disassembly" && pane.kind != "monitor") || pane.id > 0) &&
        (pane.kind != "debugger" || pane.target == "none") &&
        pane.memoryKind >= 0 && pane.memoryKind <= 2 && pane.address >= 0 && pane.address <= 65535 &&
        std::isfinite(pane.x) && std::abs(pane.x) <= 100000 &&
        std::isfinite(pane.y) && std::abs(pane.y) <= 100000 &&
        std::isfinite(pane.share) && pane.share >= 0.05 && pane.share <= 10000 &&
        std::isfinite(pane.width) && pane.width >= 0 && pane.width <= 100000 &&
        std::isfinite(pane.height) && pane.height >= 0 && pane.height <= 100000;
}

bool validLayout(const Emu51UiLayout& state)
{
    for (double fraction : {state.leftWidth,state.rightWidth,state.topHeight,state.bottomHeight})
        if (!std::isfinite(fraction) || fraction < 0.12-1e-7 || fraction > 0.70+1e-7) return false;
    for (std::size_t i = 0; i < state.panes.size(); ++i) {
        if (!validPane(state.panes[i])) return false;
        for (std::size_t j = 0; j < i; ++j)
            if (state.panes[i].kind == state.panes[j].kind && state.panes[i].id == state.panes[j].id) return false;
    }
    return true;
}

void readRecent(std::istream& input, std::vector<std::string>& paths)
{
    std::string line;
    while (paths.size() < 10 && std::getline(input,line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line.size() < 4096 && line.find('\0') == std::string::npos &&
            std::find(paths.begin(),paths.end(),line) == paths.end()) paths.push_back(line);
    }
}
} // namespace

namespace emu51_ui_layout {

const kason_schema* schema()
{
    static const kason_schema_field paneFields[] = {
        KaSON_FIELD_STRING(StoredPane, kind, "kind", KaSON_REQUIRED),
        KaSON_FIELD_INT(StoredPane, id, "id", KaSON_REQUIRED),
        KaSON_FIELD_STRING(StoredPane, target, "target", KaSON_DEFAULT_STRING("none")),
        KaSON_FIELD_DOUBLE(StoredPane, share, "share", KaSON_DEFAULT_DOUBLE(1)),
        KaSON_FIELD_DOUBLE(StoredPane, width, "width", KaSON_DEFAULT_DOUBLE(0)),
        KaSON_FIELD_DOUBLE(StoredPane, height, "height", KaSON_DEFAULT_DOUBLE(0)),
        KaSON_FIELD_BOOL(StoredPane, followPc, "follow_pc", KaSON_DEFAULT_BOOL(true)),
        KaSON_FIELD_BOOL(StoredPane, open, "open", KaSON_DEFAULT_BOOL(true)),
        KaSON_FIELD_BOOL(StoredPane, positioned, "positioned", KaSON_DEFAULT_BOOL(false)),
        KaSON_FIELD_DOUBLE(StoredPane, x, "x", KaSON_DEFAULT_DOUBLE(0)),
        KaSON_FIELD_DOUBLE(StoredPane, y, "y", KaSON_DEFAULT_DOUBLE(0)),
        KaSON_FIELD_INT(StoredPane, memoryKind, "memory_kind", KaSON_DEFAULT_INT(0)),
        KaSON_FIELD_INT(StoredPane, address, "address", KaSON_DEFAULT_INT(0)),
        KaSON_FIELD_BOOL(StoredPane, collapsed, "collapsed", KaSON_DEFAULT_BOOL(false)),
    };
    KaSON_SCHEMA_DEFINE(paneSchema, StoredPane, paneFields, 16);
    static const kason_schema_field layoutFields[] = {
        KaSON_FIELD_DOUBLE(StoredLayout, leftWidth, "left_width", KaSON_DEFAULT_DOUBLE(0.24)),
        KaSON_FIELD_DOUBLE(StoredLayout, rightWidth, "right_width", KaSON_DEFAULT_DOUBLE(0.24)),
        KaSON_FIELD_DOUBLE(StoredLayout, topHeight, "top_height", KaSON_DEFAULT_DOUBLE(0.22)),
        KaSON_FIELD_DOUBLE(StoredLayout, bottomHeight, "bottom_height", KaSON_DEFAULT_DOUBLE(0.22)),
        KaSON_FIELD_BOOL(StoredLayout, registersVisible, "registers_visible", KaSON_DEFAULT_BOOL(true)),
        KaSON_FIELD_BOOL(StoredLayout, peripheralInspector, "peripheral_inspector", KaSON_DEFAULT_BOOL(true)),
        KaSON_FIELD_BOOL(StoredLayout, leftCollapsed, "left_collapsed", KaSON_DEFAULT_BOOL(false)),
        KaSON_FIELD_BOOL(StoredLayout, rightCollapsed, "right_collapsed", KaSON_DEFAULT_BOOL(false)),
        KaSON_FIELD_BOOL(StoredLayout, topCollapsed, "top_collapsed", KaSON_DEFAULT_BOOL(false)),
        KaSON_FIELD_BOOL(StoredLayout, bottomCollapsed, "bottom_collapsed", KaSON_DEFAULT_BOOL(false)),
        KaSON_FIELD_STRUCT_ARRAY(StoredLayout, panes, paneCount, "panes", &paneSchema, KaSON_REQUIRED),
    };
    KaSON_SCHEMA_DEFINE(layoutSchema, StoredLayout, layoutFields, 16);
    static const bool initialized = kason_schema_init(&paneSchema) == KaSON_SCHEMA_SUCCESS &&
        kason_schema_init(&layoutSchema) == KaSON_SCHEMA_SUCCESS;
    return initialized ? &layoutSchema : nullptr;
}

bool decode(const StoredLayout& stored, Emu51UiLayout& layout)
{
    Emu51UiLayout decoded;
    decoded.leftWidth = stored.leftWidth; decoded.rightWidth = stored.rightWidth;
    decoded.topHeight = stored.topHeight; decoded.bottomHeight = stored.bottomHeight;
    decoded.registersVisible = stored.registersVisible != 0;
    decoded.peripheralInspector = stored.peripheralInspector != 0;
    decoded.collapsedEdgeDocks = {stored.leftCollapsed != 0, stored.rightCollapsed != 0,
        stored.topCollapsed != 0, stored.bottomCollapsed != 0};
    decoded.panes.clear();
    for (std::size_t i = 0; i < stored.paneCount; ++i) {
        const auto& pane = stored.panes[i];
        decoded.panes.push_back({pane.kind,pane.id,pane.target,pane.share,pane.width,pane.height,
            pane.followPc != 0,pane.open != 0,pane.positioned != 0,pane.x,pane.y,pane.memoryKind,pane.address,pane.collapsed != 0});
    }
    if (!validLayout(decoded)) return false;
    layout = std::move(decoded);
    return true;
}

bool encode(const Emu51UiLayout& layout, StoredLayout& stored)
{
    if (layout.panes.size() > std::size(stored.panes) || !validLayout(layout)) return false;
    stored.leftWidth = layout.leftWidth; stored.rightWidth = layout.rightWidth;
    stored.topHeight = layout.topHeight; stored.bottomHeight = layout.bottomHeight;
    stored.registersVisible = layout.registersVisible; stored.peripheralInspector = layout.peripheralInspector;
    stored.leftCollapsed = layout.collapsedEdgeDocks[0];
    stored.rightCollapsed = layout.collapsedEdgeDocks[1];
    stored.topCollapsed = layout.collapsedEdgeDocks[2];
    stored.bottomCollapsed = layout.collapsedEdgeDocks[3];
    stored.paneCount = 0;
    for (const auto& pane : layout.panes) {
        auto& out = stored.panes[stored.paneCount++];
        if (!copyText(out.kind,pane.kind) || !copyText(out.target,pane.target)) return false;
        out.id = pane.id; out.share = pane.share; out.width = pane.width; out.height = pane.height;
        out.followPc = pane.followPc; out.open = pane.open; out.positioned = pane.positioned;
        out.x = pane.x; out.y = pane.y; out.memoryKind = pane.memoryKind; out.address = pane.address;
        out.collapsed = pane.collapsed;
    }
    return true;
}

} // namespace emu51_ui_layout

bool emu51DecodeUiState(std::string_view json, Emu51UiState& state)
{
    if (json.empty() || json.size() > kMaxDocument) return false;
    std::string document(json); // Kason decodes escaped strings in-place.
    auto stored = std::make_unique<StoredState>();
    const auto* schema = stateSchema();
    if (!schema || kason_unpack_range(document.data(),document.data()+document.size()-1,schema,stored.get(),nullptr) != KaSON_SCHEMA_SUCCESS ||
        stored->version != 1) return false;
    Emu51UiState decoded;
    if (!emu51_ui_layout::decode(stored->layout,decoded)) return false;
    for (std::size_t i = 0; i < stored->programCount; ++i) decoded.recentPrograms.emplace_back(stored->recentPrograms[i]);
    for (std::size_t i = 0; i < stored->projectCount; ++i) decoded.recentProjects.emplace_back(stored->recentProjects[i]);
    state = std::move(decoded);
    return true;
}

bool emu51EncodeUiState(const Emu51UiState& state, std::string& json)
{
    if (state.recentPrograms.size() > 10 || state.recentProjects.size() > 10) return false;
    auto stored = std::make_unique<StoredState>();
    stored->version = 1;
    if (!emu51_ui_layout::encode(state,stored->layout)) return false;
    for (const auto& path : state.recentPrograms)
        if (!copyText(stored->recentPrograms[stored->programCount++],path)) return false;
    for (const auto& path : state.recentProjects)
        if (!copyText(stored->recentProjects[stored->projectCount++],path)) return false;
    const auto* schema = stateSchema();
    kason_writer writer{};
    if (!schema || kason_writer_init_counter(&writer) != KaSON_SCHEMA_SUCCESS ||
        kason_pack(&writer,schema,stored.get(),0,nullptr) != KaSON_SCHEMA_SUCCESS || writer.length >= kMaxDocument) return false;
    std::string encoded(writer.length+1,'\0');
    if (kason_writer_init_buffer(&writer,encoded.data(),encoded.size()) != KaSON_SCHEMA_SUCCESS ||
        kason_pack(&writer,schema,stored.get(),0,nullptr) != KaSON_SCHEMA_SUCCESS) return false;
    encoded.resize(writer.length); encoded += '\n';
    json = std::move(encoded);
    return true;
}

Emu51UiStateLoad emu51LoadUiState(const std::filesystem::path& path, Emu51UiState& state)
{
    std::error_code error;
    const bool exists = std::filesystem::exists(path,error);
    if (error) return Emu51UiStateLoad::Invalid;
    if (!exists) return Emu51UiStateLoad::Missing;
    std::ifstream input(path,std::ios::binary);
    if (!input) return Emu51UiStateLoad::Invalid;
    std::string json;
    char chunk[4096];
    while (input.read(chunk,sizeof(chunk)) || input.gcount()) {
        json.append(chunk,static_cast<std::size_t>(input.gcount()));
        if (json.size() > kMaxDocument) return Emu51UiStateLoad::Invalid;
    }
    return !input.bad() && emu51DecodeUiState(json,state) ? Emu51UiStateLoad::Loaded : Emu51UiStateLoad::Invalid;
}

bool emu51ImportLegacyUiState(const std::filesystem::path& directory, Emu51UiState& state)
{
    Emu51UiState imported;
    std::ifstream layout(directory/"imgui_layout_state.txt");
    std::ifstream programs(directory/"recent_programs.txt"), projects(directory/"recent_projects.txt");
    const bool found = layout.is_open() || programs.is_open() || projects.is_open();
    if (!found) return false;
    bool sawDisassembly = false;
    std::string line;
    while (std::getline(layout,line)) {
        std::istringstream record(line);
        std::string type;
        record >> type;
        if (type == "prefs") {
            double left, right, top, bottom;
            if (record >> left >> right >> top >> bottom && std::isfinite(left) && std::isfinite(right) &&
                std::isfinite(top) && std::isfinite(bottom)) {
                imported.leftWidth = std::clamp(left,0.12,0.70); imported.rightWidth = std::clamp(right,0.12,0.70);
                imported.topHeight = std::clamp(top,0.12,0.70); imported.bottomHeight = std::clamp(bottom,0.12,0.70);
            }
        } else if (type == "registers_visible" || type == "peripheral_inspector") {
            int visible;
            if (record >> visible) (type == "registers_visible" ? imported.registersVisible : imported.peripheralInspector) = visible != 0;
        } else if (type == "pane") {
            Emu51UiPaneState pane;
            if (!(record >> pane.kind >> pane.id >> pane.target >> pane.share >> pane.width >> pane.height)) continue;
            std::string follow;
            if (record >> follow) pane.followPc = follow != "0";
            pane.share = std::max(0.05,pane.share);
            if (!validPane(pane)) continue;
            if (pane.kind == "disassembly" && !sawDisassembly) {
                std::erase_if(imported.panes,[](const auto& existing) { return existing.kind == "disassembly"; });
                sawDisassembly = true;
            }
            const auto duplicate = std::find_if(imported.panes.begin(),imported.panes.end(),[&](const auto& existing) {
                return existing.kind == pane.kind && existing.id == pane.id;
            });
            if (duplicate == imported.panes.end() && imported.panes.size() < 1024) imported.panes.push_back(std::move(pane));
        }
    }
    readRecent(programs,imported.recentPrograms); readRecent(projects,imported.recentProjects);
    state = std::move(imported);
    return true;
}
