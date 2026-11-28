// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_peripheral_session.h"
#include <cstdio>
#include <fstream>
#include <limits>
#include <stdexcept>

bool Emu51PeripheralSession::circuitAccess(unsigned kind, std::uint16_t address, std::uint8_t& value, bool peek)
{
    e51p_bus_result result{255,E51P_INVALID_ID,0};
    const auto status = e51p_wired_bus_access(wiredBus_,kind,address,value,peek,&result);
    if (status == E51P_OK || status == E51P_NOT_FOUND) value = static_cast<std::uint8_t>(result.value);
    else {
        value = 255;
        if (!peek) {
            // Electrical contention in a memory transaction is a fatal bus
            // fault, unlike an advisory unresolved front-panel GPIO signal.
            busStatus_ = status == E51P_CONTENTION ? e51p_status{E51P_DEVICE_ERROR} : status;
            char location[64]; std::snprintf(location,sizeof(location),"%s %04X: ",kind == E51P_CODE_READ ? "CODE" : "XDATA",address);
            busError_ = std::string(location) + e51p_wired_bus_error(wiredBus_);
        }
    }
    return true;
}
bool Emu51PeripheralSession::readProgram(std::uint16_t address, std::uint8_t& value, bool peek)
{
    return circuitBus() && circuitAccess(E51P_CODE_READ,address,value,peek);
}
bool Emu51PeripheralSession::resolve(bool code, std::uint16_t address, Emu51PhysicalAddress& physical, std::uint8_t* value) const
{
    physical = {};
    if (!circuitBus()) return false;
    e51p_bus_result result{};
    const auto status = e51p_wired_bus_access(wiredBus_,code ? E51P_CODE_READ : E51P_DATA_READ,address,0,1,&result);
    if (value) *value = (status == E51P_OK || status == E51P_NOT_FOUND) ? static_cast<std::uint8_t>(result.value) : 255;
    if (status != E51P_OK) return false;
    for (const auto& m : memories_) if (m.target == result.target) {
        physical = {m.id,result.physical_address,e51p_memory_revision(m.storage.get())};
        return true;
    }
    return false;
}
bool Emu51PeripheralSession::programLogical(bool code, std::uint16_t address, std::uint8_t value)
{
    if (!circuitBus()) return false;
    Emu51PhysicalAddress physical;
    if (!resolve(code,address,physical)) {
        busError_ = "Programming requires an explicit memory target for this address";
        return true;
    }
    (void)e51p_memory_program(memory(physical.device),physical.offset,&value,1);
    return true;
}
e51p_memory* Emu51PeripheralSession::memory(const std::string& id) const
{
    for (const auto& m : memories_) if (m.id == id) return m.storage.get();
    return nullptr;
}
std::vector<std::uint8_t> Emu51PeripheralSession::snapshotMemory(const std::string& id, std::uint64_t offset, std::uint64_t length) const
{
    const auto* m = memory(id);
    if (!m || offset > e51p_memory_capacity(m) || length > e51p_memory_capacity(m)-offset || length > std::numeric_limits<std::size_t>::max())
        throw std::runtime_error("Memory snapshot range is outside the device");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if (e51p_memory_copy(m,offset,bytes.data(),bytes.size()) != E51P_OK) throw std::runtime_error("Cannot snapshot memory");
    return bytes;
}
bool Emu51PeripheralSession::programMemory(const std::string& id, std::uint64_t offset, const std::vector<std::uint8_t>& bytes)
{
    return e51p_memory_program(memory(id),offset,bytes.data(),bytes.size()) == E51P_OK;
}
std::vector<std::uint8_t> Emu51PeripheralSession::readBinary(const std::filesystem::path& path, std::uint64_t exactLength)
{
    if (exactLength > std::numeric_limits<std::size_t>::max() || exactLength > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max()))
        throw std::runtime_error("Binary range is too large");
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if (!input || input.tellg() < 0 || static_cast<std::uint64_t>(input.tellg()) != exactLength)
        throw std::runtime_error("Binary image must contain exactly " + std::to_string(exactLength) + " bytes: " + path.string());
    input.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(exactLength));
    if (exactLength && !input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("Cannot read binary image: " + path.string());
    if (input.peek() != std::char_traits<char>::eof() || input.bad()) throw std::runtime_error("Binary image changed while reading: " + path.string());
    return bytes;
}
std::uint64_t Emu51PeripheralSession::routingRevision(bool includeStorage) const
{
    std::uint64_t hash = UINT64_C(1469598103934665603);
    const auto mix = [&](std::uint64_t value) { hash ^= value; hash *= UINT64_C(1099511628211); };
    for (const auto& [pin,bit] : selectorPins_) {
        e51p_digital_state state{}; (void)api_->sample(context_,pin,&state);
        mix((state.value >> bit)&1); mix((state.floating >> bit)&1); mix((state.contention >> bit)&1);
    }
    if (includeStorage) for (const auto& m : memories_) mix(e51p_memory_revision(m.storage.get()));
    return hash ? hash : 1;
}
