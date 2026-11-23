// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "emu51.h"
#include "emu51/peripheral.h"

// Register while configuring the graph, wire ports(), start the context, then
// attach with cpu.setPortIo(). The context owns callback state and must outlive
// the CPU/adapter. A failed constructor requires discarding the partial graph.
class Emu51PeripheralGpio final : public emu51_port_io {
public:
    Emu51PeripheralGpio(const e51p_api& api, e51p_context& context,
        const char* instanceId = "cpu-gpio");
    e51p_endpoint port(unsigned index) const;
    std::uint8_t readPort(unsigned index) const override;
    void writePort(unsigned index, std::uint8_t latch) override;
    void advance(unsigned machineCycles) override;
    // cpu.init()/setModel() resets the entire connected context, including time.
    void reset() override;
    // Errors are sticky until cleared/reset, so a later successful pin read
    // cannot hide a failed device advance. Hosts should stop on fatal errors.
    e51p_status status() const;
    e51p_status clearStatus();
private:
    struct State;
    State* state_;
};
