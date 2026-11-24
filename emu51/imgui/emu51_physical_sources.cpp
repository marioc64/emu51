// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_imgui_app.h"
#include <algorithm>
#include <cctype>
#include <tuple>

void Emu51ImguiApp::installProjectSources(const Emu51ProjectBuild& build)
{
    projectBuild_ = build;
    std::set<std::pair<std::string,std::uint64_t>> pages;
    for (const auto& [key,byte] : build.writes) { (void)byte; pages.emplace(key.first,key.second & ~UINT64_C(255)); }
    physicalSourcePages_.assign(pages.begin(),pages.end());
    physicalSourceRevision_ = 0;
    refreshPhysicalSources();
}
void Emu51ImguiApp::refreshPhysicalSources()
{
    if (executionWorker_.onWorkerThread() || rebuildingPhysicalSources_ || !peripherals_ || !peripherals_->circuitBus() || projectBuild_.writes.empty()) return;
    auto revision = peripherals_->routingRevision(false);
    for (const auto& [id,offset] : physicalSourcePages_) {
        revision ^= e51p_memory_page_revision(peripherals_->memory(id),offset);
        revision *= UINT64_C(1099511628211);
    }
    if (revision == physicalSourceRevision_) return;
    struct Guard { bool& flag; explicit Guard(bool& f) : flag(f) { flag = true; } ~Guard() { flag = false; } } guard(rebuildingPhysicalSources_);
    emu51_asm::AssemblyImage visible;
    std::map<std::string,const Emu51BuiltUnit*> units;
    for (const auto& unit : projectBuild_.units) units.emplace(unit.id,&unit);
    using Line = std::tuple<std::string,std::string,std::size_t>;
    std::set<Line> invalid;
    std::map<std::uint16_t,Line> lines;
    std::map<std::pair<std::string,std::uint16_t>,std::uint16_t> logicalAddresses;
    std::set<std::string> visibleUnits;
    for (unsigned a = 0; a < 65536; ++a) {
        Emu51PhysicalAddress physical; std::uint8_t value;
        if (!peripherals_->resolve(true,static_cast<std::uint16_t>(a),physical,&value)) continue;
        const auto programmed = projectBuild_.writes.find({physical.device,physical.offset});
        if (programmed == projectBuild_.writes.end()) continue;
        const auto& byte = programmed->second;
        const auto unit = units.find(byte.unit);
        if (unit == units.end()) continue;
        visibleUnits.insert(byte.unit);
        logicalAddresses.try_emplace(std::pair(byte.unit,byte.source),static_cast<std::uint16_t>(a));
        const auto source = unit->second->image.sourceLines.find(byte.source);
        if (source != unit->second->image.sourceLines.end()) {
            const Line key{byte.unit,source->second.file,source->second.line};
            if (value != byte.value) invalid.insert(key);
            lines.emplace(static_cast<std::uint16_t>(a),key);
            visible.sourceLines.emplace(static_cast<std::uint16_t>(a),source->second);
        }
        visible.bytes.emplace(static_cast<std::uint16_t>(a),value);
        if (unit->second->image.instructionAddresses.contains(byte.source)) visible.instructionAddresses.insert(static_cast<std::uint16_t>(a));
    }
    for (const auto& [address,line] : lines) if (invalid.contains(line)) {
        visible.sourceLines.erase(address); visible.instructionAddresses.erase(address);
    }
    std::set<std::string> ambiguous;
    for (const auto& id : visibleUnits) {
        const auto& image = units.at(id)->image;
        for (const auto& [name,symbol] : image.symbols) {
            auto mapped = symbol;
            if (symbol.kind == emu51_asm::SymbolKind::label) {
                const auto address = logicalAddresses.find({id,symbol.value});
                if (address == logicalAddresses.end()) continue;
                mapped.value = address->second;
            }
            auto qualified = id+"."+name;
            std::transform(qualified.begin(),qualified.end(),qualified.begin(),[](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            visible.symbols.emplace(qualified,mapped);
            if (!visible.symbols.emplace(name,mapped).second) ambiguous.insert(name);
        }
        for (const auto& [path,text] : image.sources) visible.sources.insert_or_assign(path,text);
    }
    for (const auto& name : ambiguous) visible.symbols.erase(name);
    setLoadedSourceMap(visible);
    physicalSourceRevision_ = revision;
    invalidateInstructionCache();
}
