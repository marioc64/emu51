// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#include "emu51/devices.h"
#include "emu51/snapshot.h"
#include <stdlib.h>

typedef struct i8255 {
    const e51p_api* api;
    e51p_context* context;
    e51p_i8255_endpoints pins;
    uint32_t control, latch[3];
} i8255;
static int reset_held(i8255*, e51p_context*);

static e51p_status validate_write(void* user, uint32_t address, uint32_t value) {
    i8255* ppi = (i8255*)user;
    if (address > 3 || value > 255) return E51P_INVALID_ARGUMENT;
    if (address == 3 && (value & 0x80) && (value & 0x64)) return E51P_UNSUPPORTED;
    if (reset_held(ppi,ppi->context)) return E51P_INVALID_STATE;
    return E51P_OK;
}

static uint32_t output_mask(const i8255* ppi, unsigned port) {
    if (port == 0) return (ppi->control & 0x10) ? 0 : 255;
    if (port == 1) return (ppi->control & 2) ? 0 : 255;
    return ((ppi->control & 8) ? 0 : 240) | ((ppi->control & 1) ? 0 : 15);
}
static e51p_status update(i8255* ppi, e51p_context* context) {
    const e51p_endpoint ports[] = {ppi->pins.port_a, ppi->pins.port_b, ppi->pins.port_c};
    unsigned port;
    for (port = 0; port < 3; ++port) {
        e51p_status status;
        if (ports[port] == E51P_INVALID_ID) continue;
        status = ppi->api->drive(context, ports[port], ppi->latch[port], output_mask(ppi, port), 0);
        if (status != E51P_OK) return status;
    }
    return E51P_OK;
}
static void reset(void* user, e51p_context* context) {
    i8255* ppi = (i8255*)user;
    ppi->control = 0x9b;
    ppi->latch[0] = ppi->latch[1] = ppi->latch[2] = 0;
    (void)update(ppi, context);
}
static int reset_held(i8255* ppi, e51p_context* context) {
    e51p_digital_state state = {0};
    if (ppi->api->sample(context, ppi->pins.reset, &state) != E51P_OK) return 1;
    return state.value || state.floating || state.contention;
}
static void changed(void* user, e51p_context* context, e51p_endpoint endpoint,
    e51p_digital_state state, uint64_t time) {
    i8255* ppi = (i8255*)user;
    (void)time;
    if (endpoint == ppi->pins.reset && !state.floating && !state.contention && state.value)
        reset(user, context);
}
static e51p_status read_bus(void* user, e51p_context* context, e51p_endpoint endpoint,
    uint32_t address, uint32_t access, uint32_t* value) {
    i8255* ppi = (i8255*)user;
    const e51p_endpoint ports[] = {ppi->pins.port_a, ppi->pins.port_b, ppi->pins.port_c};
    e51p_digital_state state = {0};
    e51p_status status;
    uint32_t outputs;
    (void)endpoint;
    (void)access; /* Mode 0 reads and peeks do not consume input. */
    if (address > 3) return E51P_INVALID_ARGUMENT;
    if (address == 3) return E51P_UNSUPPORTED; /* Original 8255A, not 82C55A. */
    if (reset_held(ppi, context)) return E51P_INVALID_STATE;
    status = ppi->api->sample(context, ports[address], &state);
    if (status != E51P_OK) return status;
    /* Intel output readback: A reads physical pins; B/C read output latches.
     * Input bits always read pins, including independently directed C halves. */
    outputs = address == 0 ? 0 : output_mask(ppi, address);
    *value = (state.value & ~outputs) | (ppi->latch[address] & outputs);
    return (state.contention & ~outputs) ? E51P_CONTENTION : E51P_OK;
}
static e51p_status write_bus(void* user, e51p_context* context, e51p_endpoint endpoint,
    uint32_t address, uint32_t value) {
    i8255* ppi = (i8255*)user;
    (void)endpoint;
    if (address > 3 || value > 255) return E51P_INVALID_ARGUMENT;
    if (reset_held(ppi, context)) return E51P_INVALID_STATE;
    if (address < 3) ppi->latch[address] = value;
    else if (value & 0x80) {
        if (value & 0x64) return E51P_UNSUPPORTED;
        ppi->control = value;
        ppi->latch[0] = ppi->latch[1] = ppi->latch[2] = 0;
    } else {
        const uint32_t bit = 1u << ((value >> 1) & 7);
        if (value & 1) ppi->latch[2] |= bit;
        else ppi->latch[2] &= ~bit;
    }
    return update(ppi, context);
}
e51p_status e51p_add_i8255_mode0(const e51p_api* api, e51p_context* context,
    const char* instance_id, e51p_i8255_endpoints* result) {
    i8255* ppi;
    e51p_device_desc device = {0};
    e51p_endpoint_desc endpoint = {0};
    e51p_device id;
    e51p_status status;
    e51p_endpoint* ports[3];
    const char* names[] = {"A", "B", "C"};
    unsigned port;
    if (!api || api->abi_version != E51P_ABI_VERSION || api->struct_size < sizeof(*api) ||
        !context || !result || result->struct_size < sizeof(*result)) return E51P_INVALID_ARGUMENT;
    ppi = (i8255*)calloc(1, sizeof(*ppi));
    if (!ppi) return E51P_NO_MEMORY;
    ppi->api = api;
    ppi->context = context;
    ppi->pins.struct_size = sizeof(ppi->pins);
    ppi->pins.bus = ppi->pins.reset = E51P_INVALID_ID;
    ppi->pins.port_a = ppi->pins.port_b = ppi->pins.port_c = E51P_INVALID_ID;
    device.struct_size = sizeof(device);
    device.instance_id = instance_id;
    device.user = ppi;
    device.destroy = free;
    device.reset = reset;
    device.digital_changed = changed;
    device.bus_read = read_bus;
    device.bus_write = write_bus;
    status = api->add_device(context, &device, &id);
    if (status != E51P_OK) { free(ppi); return status; }
    endpoint.struct_size = sizeof(endpoint);
    endpoint.name = "registers";
    endpoint.kind = E51P_BUS_TARGET;
    endpoint.width = 8;
    endpoint.address_bits = 2;
    status = api->add_endpoint(context, id, &endpoint, &ppi->pins.bus);
    if (status != E51P_OK) return status;
    status = e51p_target_set_write_validator(context, ppi->pins.bus, validate_write, ppi);
    if (status != E51P_OK) return status;
    endpoint.kind = E51P_DIGITAL;
    endpoint.direction = E51P_INPUT | E51P_OUTPUT;
    endpoint.address_bits = 0;
    ports[0] = &ppi->pins.port_a; ports[1] = &ppi->pins.port_b; ports[2] = &ppi->pins.port_c;
    for (port = 0; port < 3; ++port) {
        endpoint.name = names[port];
        status = api->add_endpoint(context, id, &endpoint, ports[port]);
        if (status != E51P_OK) return status;
    }
    endpoint.name = "reset";
    endpoint.direction = E51P_INPUT;
    endpoint.width = 1;
    endpoint.pull_down = 1;
    status = api->add_endpoint(context, id, &endpoint, &ppi->pins.reset);
    if (status == E51P_OK) *result = ppi->pins;
    return status;
}
