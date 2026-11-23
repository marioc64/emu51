// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#include "emu51/devices.h"
#include <math.h>
#include <stdlib.h>

typedef struct adc0804 {
    const e51p_api* api;
    e51p_adc0804_endpoints pins;
    uint64_t conversion_ticks, remaining;
    double span;
    uint32_t result, pending;
    int valid, pending_valid, busy, held_reset, interrupt;
} adc0804;

static int controls(adc0804* adc, e51p_context* context, int* cs, int* rd, int* wr) {
    const e51p_endpoint pins[] = {adc->pins.cs_n, adc->pins.rd_n, adc->pins.wr_n};
    int* levels[] = {cs, rd, wr};
    unsigned index;
    for (index = 0; index < 3; ++index) {
        e51p_digital_state state = {0};
        if (adc->api->sample(context, pins[index], &state) != E51P_OK || state.floating || state.contention)
            return 0;
        *levels[index] = state.value != 0;
    }
    return 1;
}
static void outputs(adc0804* adc, e51p_context* context) {
    int cs = 1, rd = 1, wr = 1;
    const int known = controls(adc, context, &cs, &rd, &wr);
    const int reading = known && !cs && !rd;
    (void)wr;
    if (reading) adc->interrupt = 0;
    if (adc->pins.data != E51P_INVALID_ID)
        (void)adc->api->drive(context, adc->pins.data, adc->result, reading && adc->valid ? 255 : 0, 0);
    if (adc->pins.intr_n != E51P_INVALID_ID)
        (void)adc->api->drive(context, adc->pins.intr_n, adc->interrupt ? 0 : 1, 1, 0);
}
static void sample_input(adc0804* adc, e51p_context* context) {
    double positive = 0, negative = 0, scaled;
    e51p_status status;
    adc->pending_valid = 0;
    if (adc->api->analog_read(context, adc->pins.vin_positive, &positive) != E51P_OK) return;
    status = adc->api->analog_read(context, adc->pins.vin_negative, &negative);
    if (status != E51P_OK && status != E51P_NOT_FOUND) return;
    scaled = ((positive - negative) / adc->span) * 256.0;
    if (scaled <= 0) adc->pending = 0;
    else if (scaled >= 255) adc->pending = 255;
    else adc->pending = (uint32_t)(scaled + 0.5);
    adc->pending_valid = 1;
}
static void changed(void* user, e51p_context* context, e51p_endpoint endpoint,
    e51p_digital_state state, uint64_t time) {
    adc0804* adc = (adc0804*)user;
    int cs = 1, rd = 1, wr = 1;
    (void)endpoint; (void)state; (void)time;
    if (controls(adc, context, &cs, &rd, &wr)) {
        const int reset = !cs && !wr;
        if (reset) {
            adc->held_reset = 1;
            adc->busy = 0;
            adc->interrupt = 0;
        } else if (adc->held_reset) {
            adc->held_reset = 0;
            adc->busy = 1;
            adc->remaining = adc->conversion_ticks;
            adc->interrupt = 0;
            sample_input(adc, context);
        }
    }
    outputs(adc, context);
}
static void advance(void* user, e51p_context* context, uint64_t from, uint64_t to) {
    adc0804* adc = (adc0804*)user;
    if (!adc->busy) return;
    if (to - from < adc->remaining) { adc->remaining -= to - from; return; }
    adc->busy = 0;
    adc->result = adc->pending;
    adc->valid = adc->pending_valid;
    adc->interrupt = 1;
    outputs(adc, context);
}
static void reset(void* user, e51p_context* context) {
    adc0804* adc = (adc0804*)user;
    adc->busy = adc->held_reset = adc->interrupt = 0;
    adc->result = adc->pending = 0;
    adc->valid = 1;
    adc->pending_valid = 0;
    adc->remaining = 0;
    // Context reset has not resolved initial controls yet. Publish only the
    // inactive interrupt and release the data bus; callbacks handle the pins.
    if (adc->pins.intr_n != E51P_INVALID_ID) (void)adc->api->drive(context, adc->pins.intr_n, 1, 1, 0);
    if (adc->pins.data != E51P_INVALID_ID) (void)adc->api->drive(context, adc->pins.data, 0, 0, 0);
}
e51p_status e51p_add_adc0804(const e51p_api* api, e51p_context* context,
    const char* instance_id, uint64_t conversion_ticks, double span, e51p_adc0804_endpoints* result) {
    adc0804* adc;
    e51p_device_desc device = {0};
    e51p_endpoint_desc endpoint = {0};
    e51p_device id;
    e51p_status status;
    e51p_endpoint* control_pins[3];
    const char* control_names[] = {"cs_n", "rd_n", "wr_n"};
    unsigned index;
    if (!api || api->abi_version != E51P_ABI_VERSION || api->struct_size < sizeof(*api) ||
        !context || !result || result->struct_size < sizeof(*result) || !conversion_ticks ||
        !isfinite(span) || span <= 0) return E51P_INVALID_ARGUMENT;
    adc = (adc0804*)calloc(1, sizeof(*adc));
    if (!adc) return E51P_NO_MEMORY;
    adc->api = api; adc->span = span; adc->conversion_ticks = conversion_ticks;
    adc->pins.struct_size = sizeof(adc->pins);
    adc->pins.data = adc->pins.cs_n = adc->pins.rd_n = adc->pins.wr_n = E51P_INVALID_ID;
    adc->pins.intr_n = adc->pins.vin_positive = adc->pins.vin_negative = E51P_INVALID_ID;
    device.struct_size = sizeof(device);
    device.instance_id = instance_id;
    device.user = adc;
    device.destroy = free;
    device.reset = reset;
    device.digital_changed = changed;
    device.advance = advance;
    status = api->add_device(context, &device, &id);
    if (status != E51P_OK) { free(adc); return status; }
    endpoint.struct_size = sizeof(endpoint);
    endpoint.name = "data";
    endpoint.kind = E51P_DIGITAL;
    endpoint.direction = E51P_OUTPUT;
    endpoint.width = 8;
    status = api->add_endpoint(context, id, &endpoint, &adc->pins.data);
    if (status != E51P_OK) return status;
    endpoint.direction = E51P_INPUT;
    endpoint.width = 1;
    endpoint.pull_up = 1;
    control_pins[0] = &adc->pins.cs_n; control_pins[1] = &adc->pins.rd_n; control_pins[2] = &adc->pins.wr_n;
    for (index = 0; index < 3; ++index) {
        endpoint.name = control_names[index];
        status = api->add_endpoint(context, id, &endpoint, control_pins[index]);
        if (status != E51P_OK) return status;
    }
    endpoint.name = "intr_n";
    endpoint.direction = E51P_OUTPUT;
    endpoint.pull_up = 0;
    status = api->add_endpoint(context, id, &endpoint, &adc->pins.intr_n);
    if (status != E51P_OK) return status;
    endpoint.name = "vin_positive";
    endpoint.kind = E51P_ANALOG;
    endpoint.direction = E51P_INPUT;
    status = api->add_endpoint(context, id, &endpoint, &adc->pins.vin_positive);
    if (status != E51P_OK) return status;
    endpoint.name = "vin_negative";
    status = api->add_endpoint(context, id, &endpoint, &adc->pins.vin_negative);
    if (status == E51P_OK) *result = adc->pins;
    return status;
}
