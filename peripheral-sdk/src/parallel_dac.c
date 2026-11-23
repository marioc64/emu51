// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#include "emu51/devices.h"
#include <math.h>
#include <stdlib.h>

typedef struct parallel_dac {
    const e51p_api* api;
    e51p_endpoint input, output;
    double reference;
} parallel_dac;

static void reset(void* user, e51p_context* context) {
    parallel_dac* dac = (parallel_dac*)user;
    if (dac->output != E51P_INVALID_ID) (void)dac->api->analog_write(context, dac->output, 0.0);
}
static void changed(void* user, e51p_context* context, e51p_endpoint endpoint,
    e51p_digital_state state, uint64_t time) {
    parallel_dac* dac = (parallel_dac*)user;
    (void)endpoint;
    (void)time;
    if (!state.floating && !state.contention && dac->output != E51P_INVALID_ID)
        (void)dac->api->analog_write(context, dac->output, dac->reference * (state.value / 256.0));
}
e51p_status e51p_add_parallel_dac(const e51p_api* api, e51p_context* context,
    const char* instance_id, double reference, e51p_endpoint* input, e51p_endpoint* output) {
    parallel_dac* dac;
    e51p_device_desc device = {0};
    e51p_endpoint_desc endpoint = {0};
    e51p_device id;
    e51p_status status;
    if (!api || api->abi_version != E51P_ABI_VERSION || api->struct_size < sizeof(*api) ||
        !context || !input || !output || input == output || !isfinite(reference) || reference <= 0)
        return E51P_INVALID_ARGUMENT;
    dac = (parallel_dac*)malloc(sizeof(*dac));
    if (!dac) return E51P_NO_MEMORY;
    dac->api = api;
    dac->reference = reference;
    dac->input = dac->output = E51P_INVALID_ID;
    device.struct_size = sizeof(device);
    device.instance_id = instance_id;
    device.user = dac;
    device.destroy = free;
    device.reset = reset;
    device.digital_changed = changed;
    status = api->add_device(context, &device, &id);
    if (status != E51P_OK) { free(dac); return status; }
    endpoint.struct_size = sizeof(endpoint);
    endpoint.name = "data";
    endpoint.kind = E51P_DIGITAL;
    endpoint.direction = E51P_INPUT;
    endpoint.width = 8;
    status = api->add_endpoint(context, id, &endpoint, &dac->input);
    if (status != E51P_OK) return status;
    endpoint.name = "voltage";
    endpoint.kind = E51P_ANALOG;
    endpoint.direction = E51P_OUTPUT;
    endpoint.width = 1;
    status = api->add_endpoint(context, id, &endpoint, &dac->output);
    if (status == E51P_OK) { *input = dac->input; *output = dac->output; }
    return status;
}
