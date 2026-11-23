// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_peripheral_bus.h"
#include <limits>
#include <stdexcept>

Emu51PeripheralBus::Emu51PeripheralBus(const e51p_api& api, e51p_context& context,
    e51p_endpoint initiator, std::shared_ptr<address_space> backing)
    : api_(api), context_(context), initiator_(initiator), backing_(std::move(backing))
{
    if (!backing_ || api_.abi_version != E51P_ABI_VERSION || api_.struct_size < sizeof(e51p_api))
        throw std::invalid_argument("Peripheral bus requires a backing map and matching SDK ABI");
}
std::uint8_t Emu51PeripheralBus::readData(std::uint16_t address)
{
    std::uint32_t value = 0xff;
    lastStatus_ = api_.bus_read(&context_, initiator_, address, E51P_READ, &value);
    if (lastStatus_ == E51P_NOT_FOUND) return backing_->readData(address);
    if (value > 0xff) { lastStatus_ = E51P_DEVICE_ERROR; return 0xff; }
    return lastStatus_ == E51P_OK || lastStatus_ == E51P_CONTENTION
        ? static_cast<std::uint8_t>(value) : 0xff;
}
void Emu51PeripheralBus::writeData(std::uint16_t address, std::uint8_t value)
{
    lastStatus_ = api_.bus_write(&context_, initiator_, address, value);
    if (lastStatus_ == E51P_NOT_FOUND) backing_->writeData(address, value);
}
std::uint8_t Emu51PeripheralBus::readProg(std::uint16_t address) { return backing_->readProg(address); }
void Emu51PeripheralBus::writeProg(std::uint16_t address, std::uint8_t value) { backing_->writeProg(address, value); }
e51p_status Emu51PeripheralBus::peekData(std::uint16_t address, std::uint8_t& value)
{
    std::uint32_t result = 0;
    const auto status = api_.bus_read(&context_, initiator_, address, E51P_PEEK, &result);
    // The backing address_space has no inspection contract. Leave unmapped
    // inspection to its owner, rather than causing a potentially consuming read.
    if (status == E51P_OK || status == E51P_CONTENTION) {
        if (result > 0xff) return E51P_DEVICE_ERROR;
        value = static_cast<std::uint8_t>(result);
    }
    return status;
}
e51p_status Emu51PeripheralBus::advanceMachineCycles(std::uint64_t cycles)
{
    std::uint64_t now = 0, hz = 0;
    lastStatus_ = api_.clock(&context_, &now, &hz);
    if (lastStatus_ != E51P_OK) return lastStatus_;
    if (cycles > (std::numeric_limits<std::uint64_t>::max() - now) / 12)
        return lastStatus_ = E51P_INVALID_ARGUMENT;
    return lastStatus_ = api_.advance_to(&context_, now + cycles * 12);
}
