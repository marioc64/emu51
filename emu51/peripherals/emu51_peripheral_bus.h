// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "emu51.h"
#include "emu51/peripheral.h"

// Instruction-boundary host adapter. The caller owns the SDK context and must
// keep it alive longer than this address space. Program accesses always use
// backing; only unmapped data accesses fall back. Claimed access errors are
// visible through lastStatus(), never silently redirected to backing RAM.
class Emu51PeripheralBus final : public address_space {
public:
    Emu51PeripheralBus(const e51p_api& api, e51p_context& context,
        e51p_endpoint initiator, std::shared_ptr<address_space> backing);
    std::uint8_t readData(std::uint16_t address) override;
    void writeData(std::uint16_t address, std::uint8_t value) override;
    std::uint8_t readProg(std::uint16_t address) override;
    void writeProg(std::uint16_t address, std::uint8_t value) override;
    e51p_status peekData(std::uint16_t address, std::uint8_t& value);
    e51p_status lastStatus() const { return lastStatus_; }
    // For a context whose tick rate equals the fixed oscillator Hz: call after
    // every completed instruction, including its interrupt-entry cycles.
    e51p_status advanceMachineCycles(std::uint64_t cycles);
private:
    const e51p_api& api_;
    e51p_context& context_;
    e51p_endpoint initiator_;
    std::shared_ptr<address_space> backing_;
    e51p_status lastStatus_ = E51P_OK;
};
