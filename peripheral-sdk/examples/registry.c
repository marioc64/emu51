// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#include "emu51/registry.h"
#include <stdio.h>
#include <stdlib.h>

/* A separately authored, context-owned source factory. No emulator types. */
typedef struct byte_source {
    const e51p_api* api;
    e51p_endpoint output;
    uint32_t initial;
} byte_source;
static void reset_source(void* user, e51p_context* c) {
    byte_source* source = (byte_source*)user;
    (void)source->api->drive(c, source->output, source->initial, 255, 0);
}
static e51p_status create_source(void* user, const e51p_api* api, e51p_context* c, const char* id,
    const e51p_parameter_value* values, uint32_t count, e51p_endpoint* pins) {
    byte_source* source;
    e51p_device device;
    e51p_device_desc desc = {0};
    e51p_endpoint_desc pin = {sizeof(pin), "out", E51P_DIGITAL, E51P_OUTPUT, 8, 0, 0, 0};
    e51p_status status;
    (void)user;
    if (count != 1 || !values || !pins) return E51P_INVALID_ARGUMENT;
    source = (byte_source*)calloc(1, sizeof(*source));
    if (!source) return E51P_NO_MEMORY;
    source->api = api; source->initial = (uint32_t)values[0].integer;
    desc.struct_size = sizeof(desc); desc.instance_id = id; desc.user = source;
    desc.destroy = free; desc.reset = reset_source;
    status = api->add_device(c, &desc, &device);
    if (status != E51P_OK) { free(source); return status; }
    /* The context now owns source even if endpoint registration fails. */
    status = api->add_endpoint(c, device, &pin, &source->output);
    if (status == E51P_OK) pins[0] = source->output;
    return status;
}
static e51p_status register_source(e51p_registry* registry) {
    const e51p_parameter_desc initial = {sizeof(initial), "initial", "Initial byte", E51P_PARAMETER_INTEGER,
        128, 0, 255, 0, 0, 0, NULL, 0};
    const e51p_endpoint_desc pin = {sizeof(pin), "out", E51P_DIGITAL, E51P_OUTPUT, 8, 0, 0, 0};
    const e51p_factory_desc desc = {sizeof(desc), "example.byte-source", "Example byte source",
        &initial, 1, &pin, 1, NULL, create_source};
    /* All descriptors and strings are copied, so stack metadata is safe. */
    return e51p_registry_register(registry, &desc);
}
#define CHECK(call) do { e51p_status s_ = (call); if (s_ != E51P_OK) { \
    fprintf(stderr, "%s: SDK status %u\n", #call, s_); result = 1; goto done; } } while (0)

int main(void) {
    const e51p_api* api = NULL;
    e51p_context* context = NULL;
    e51p_registry* registry = NULL;
    e51p_endpoint source, dac[2];
    uint32_t count = 0, i;
    double volts = 0;
    int result = 0;
    CHECK(e51p_get_api(E51P_ABI_VERSION, &api));
    CHECK(e51p_registry_create(E51P_REGISTRY_ABI_VERSION, &registry));
    CHECK(e51p_registry_add_builtins(registry));
    CHECK(register_source(registry));
    CHECK(e51p_registry_count(registry, &count));
    for (i = 0; i < count; ++i) {
        const e51p_factory_desc* type = NULL;
        CHECK(e51p_registry_at(registry, i, &type));
        printf("%s: %s (%u parameters, %u pins)\n", type->type_id, type->label, type->parameter_count, type->pin_count);
    }
    CHECK(api->create(12000000, &context));
    CHECK(e51p_registry_instantiate(registry, "example.byte-source", api, context, "source", NULL, 0, &source, 1));
    CHECK(e51p_registry_instantiate(registry, "emu51.parallel-dac", api, context, "dac", NULL, 0, dac, 2));
    /* Devices live with context, independently of registry metadata. */
    e51p_registry_destroy(registry); registry = NULL;
    CHECK(api->connect_digital(context, source, 0, dac[0], 0, 8));
    CHECK(api->start(context));
    CHECK(api->analog_read(context, dac[1], &volts));
    printf("Custom source 128 -> discovered DAC -> %.6f V\n", volts);
    if (volts != 2.5) result = 1;
done:
    if (api) api->destroy(context);
    e51p_registry_destroy(registry);
    return result;
}
