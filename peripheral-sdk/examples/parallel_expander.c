// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#include "parallel_expander.h"

static e51p_status update(e51p_parallel_expander* device, e51p_context* context) {
    uint32_t i;
    for (i = 0; i < 3; ++i) {
        e51p_status status = device->api->drive(context, device->ports[i], device->latch[i],
            (device->output_enable & (1u << i)) ? 255u : 0u, 0);
        if (status != E51P_OK) return status;
    }
    return E51P_OK;
}
static void reset(void* user, e51p_context* context) {
    e51p_parallel_expander* device = (e51p_parallel_expander*)user;
    device->latch[0] = device->latch[1] = device->latch[2] = 0;
    device->output_enable = 0;
    (void)update(device, context);
}
static e51p_status read_bus(void* user, e51p_context* context, e51p_endpoint endpoint,
    uint32_t address, uint32_t access, uint32_t* value) {
    e51p_parallel_expander* device = (e51p_parallel_expander*)user;
    e51p_digital_state state;
    e51p_status status;
    (void)endpoint;
    (void)access; /* Reading this example never consumes state. */
    if (address == 3) { *value = device->output_enable; return E51P_OK; }
    if (address > 3) return E51P_INVALID_ARGUMENT;
    status = device->api->sample(context, device->ports[address], &state);
    if (status != E51P_OK) return status;
    *value = state.value;
    return state.contention ? E51P_CONTENTION : E51P_OK;
}
static e51p_status write_bus(void* user, e51p_context* context, e51p_endpoint endpoint,
    uint32_t address, uint32_t value) {
    e51p_parallel_expander* device = (e51p_parallel_expander*)user;
    (void)endpoint;
    if (address == 3) {
        if (value > 7) return E51P_INVALID_ARGUMENT;
        device->output_enable = value;
    } else if (address < 3) device->latch[address] = value;
    else return E51P_INVALID_ARGUMENT;
    return update(device, context);
}
e51p_status e51p_parallel_expander_add(const e51p_api* api, e51p_context* context,
    const char* instance_id, e51p_parallel_expander* device) {
    static const char* names[3] = {"A", "B", "C"};
    e51p_device_desc desc = {0};
    e51p_endpoint_desc port = {0};
    e51p_device id;
    e51p_status status;
    uint32_t i;
    if (!api || !device) return E51P_INVALID_ARGUMENT;
    device->api = api;
    desc.struct_size = sizeof(desc);
    desc.instance_id = instance_id;
    desc.user = device;
    desc.reset = reset;
    desc.bus_read = read_bus;
    desc.bus_write = write_bus;
    status = api->add_device(context, &desc, &id);
    if (status != E51P_OK) return status;
    port.struct_size = sizeof(port);
    port.name = "registers";
    port.kind = E51P_BUS_TARGET;
    port.width = 8;
    port.address_bits = 2;
    status = api->add_endpoint(context, id, &port, &device->bus);
    if (status != E51P_OK) return status;
    port.kind = E51P_DIGITAL;
    port.address_bits = 0;
    port.direction = E51P_INPUT | E51P_OUTPUT;
    for (i = 0; i < 3; ++i) {
        port.name = names[i];
        status = api->add_endpoint(context, id, &port, &device->ports[i]);
        if (status != E51P_OK) return status;
    }
    return E51P_OK;
}
