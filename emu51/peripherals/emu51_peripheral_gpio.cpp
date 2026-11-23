// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_peripheral_gpio.h"
#include <limits>
#include <stdexcept>
#include <string>

struct Emu51PeripheralGpio::State {
    const e51p_api& api;
    e51p_context& context;
    std::array<e51p_endpoint, 4> ports{E51P_INVALID_ID, E51P_INVALID_ID, E51P_INVALID_ID, E51P_INVALID_ID};
    std::array<std::uint8_t, 4> latches{0xff, 0xff, 0xff, 0xff};
    e51p_status status = E51P_OK;

    void record(e51p_status result) {
        if (result != E51P_OK && (status == E51P_OK || status == E51P_CONTENTION)) status = result;
    }
    void drive(unsigned index) {
        // Classic GPIO approximation: low is strong, high releases. P1..P3
        // have weak endpoint pull-ups; P0 needs an external pull-up/driver.
        record(api.drive(&context, ports[index], latches[index], 0xff, 0xff));
    }
    static void reset(void* user, e51p_context*) {
        auto& s = *static_cast<State*>(user);
        s.status = E51P_OK;
        s.latches.fill(0xff);
        for (unsigned index = 0; index < 4; ++index)
            if (s.ports[index] != E51P_INVALID_ID) s.drive(index);
    }
};

Emu51PeripheralGpio::Emu51PeripheralGpio(const e51p_api& api, e51p_context& context,
    const char* instanceId) : state_(nullptr)
{
    if (api.abi_version != E51P_ABI_VERSION || api.struct_size < sizeof(e51p_api))
        throw std::invalid_argument("GPIO adapter requires a matching peripheral SDK ABI");
    auto state = std::make_unique<State>(State{api, context});
    e51p_device_desc desc{};
    desc.struct_size = sizeof(desc);
    desc.instance_id = instanceId;
    desc.user = state.get();
    desc.destroy = [](void* user) { delete static_cast<State*>(user); };
    desc.reset = State::reset;
    e51p_device device;
    auto status = api.add_device(&context, &desc, &device);
    if (status != E51P_OK) throw std::runtime_error("Cannot register CPU GPIO: " + std::to_string(status));
    state_ = state.release(); // From here the context owns callback storage.
    const char* names[] = {"P0", "P1", "P2", "P3"};
    for (unsigned index = 0; index < 4; ++index) {
        e51p_endpoint_desc endpoint{sizeof(endpoint), names[index], E51P_DIGITAL,
            E51P_INPUT | E51P_OUTPUT, 8, 0, index ? 0xffu : 0u, 0};
        status = api.add_endpoint(&context, device, &endpoint, &state_->ports[index]);
        if (status != E51P_OK) throw std::runtime_error("Cannot register CPU port: " + std::to_string(status));
    }
}
e51p_endpoint Emu51PeripheralGpio::port(unsigned index) const
{
    if (index > 3) throw std::out_of_range("8051 port index");
    return state_->ports[index];
}
std::uint8_t Emu51PeripheralGpio::readPort(unsigned index) const
{
    if (index > 3) { state_->record(E51P_INVALID_ARGUMENT); return 0xff; }
    e51p_digital_state sample{};
    const auto status = state_->api.sample(&state_->context, state_->ports[index], &sample);
    state_->record(status);
    if (status != E51P_OK) return 0;
    if (sample.contention) state_->record(E51P_CONTENTION);
    return static_cast<std::uint8_t>(sample.value);
}
void Emu51PeripheralGpio::writePort(unsigned index, std::uint8_t latch)
{
    if (index > 3) { state_->record(E51P_INVALID_ARGUMENT); return; }
    if (state_->latches[index] == latch) return;
    state_->latches[index] = latch;
    state_->drive(index);
}
void Emu51PeripheralGpio::advance(unsigned machineCycles)
{
    std::uint64_t time = 0, hz = 0;
    const auto status = state_->api.clock(&state_->context, &time, &hz);
    state_->record(status);
    if (status != E51P_OK) return;
    if (machineCycles > (std::numeric_limits<std::uint64_t>::max() - time) / 12) {
        state_->record(E51P_INVALID_ARGUMENT);
        return;
    }
    state_->record(state_->api.advance_to(&state_->context, time + machineCycles * 12));
}
void Emu51PeripheralGpio::reset()
{
    state_->status = E51P_OK;
    state_->record(state_->api.reset(&state_->context));
}
e51p_status Emu51PeripheralGpio::status() const { return state_->status; }
e51p_status Emu51PeripheralGpio::clearStatus()
{
    const auto status = state_->status;
    state_->status = E51P_OK;
    return status;
}
