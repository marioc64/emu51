// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#include "parallel_expander.h"
#include <stdio.h>

#define CHECK(call) do { if ((call) != E51P_OK) { result = 1; goto done; } } while (0)

/* Pure C consumer, independently buildable without the emulator or ImGui. */
int main(void) {
    const e51p_api* api = NULL;
    e51p_context* context = NULL;
    e51p_device host;
    e51p_endpoint bus, leds, audio_source, adc_input;
    e51p_parallel_expander expander = {0};
    e51p_device_desc device = {0};
    e51p_endpoint_desc endpoint = {0};
    e51p_digital_state state;
    double volts = 0;
    int result = 0;
    CHECK(e51p_get_api(E51P_ABI_VERSION, &api));
    CHECK(api->create(12000000, &context));
    device.struct_size = sizeof(device);
    device.instance_id = "host";
    CHECK(api->add_device(context, &device, &host));
    endpoint.struct_size = sizeof(endpoint);
    endpoint.name = "xdata";
    endpoint.kind = E51P_BUS_INITIATOR;
    endpoint.width = 8;
    endpoint.address_bits = 16;
    CHECK(api->add_endpoint(context, host, &endpoint, &bus));
    endpoint.name = "leds";
    endpoint.kind = E51P_DIGITAL;
    endpoint.direction = E51P_INPUT;
    endpoint.address_bits = 0;
    endpoint.width = 4;
    CHECK(api->add_endpoint(context, host, &endpoint, &leds));
    endpoint.name = "audio_source";
    endpoint.kind = E51P_ANALOG;
    endpoint.direction = E51P_OUTPUT;
    endpoint.width = 1;
    CHECK(api->add_endpoint(context, host, &endpoint, &audio_source));
    endpoint.name = "adc_input";
    endpoint.direction = E51P_INPUT;
    CHECK(api->add_endpoint(context, host, &endpoint, &adc_input));
    CHECK(e51p_parallel_expander_add(api, context, "expander", &expander));
    CHECK(api->map_bus(context, bus, 0x8000, 4, expander.bus, 0));
    CHECK(api->connect_digital(context, expander.ports[0], 4, leds, 0, 4));
    CHECK(api->connect_analog(context, audio_source, adc_input));
    CHECK(api->start(context));
    CHECK(api->bus_write(context, bus, 0x8003, 1));
    CHECK(api->bus_write(context, bus, 0x8000, 0xa0));
    CHECK(api->sample(context, leds, &state));
    CHECK(api->advance_to(context, 120));
    CHECK(api->analog_write(context, audio_source, 2.5));
    CHECK(api->analog_read(context, adc_input, &volts));
    printf("expander.A[7:4] -> leds[3:0] = %X; analog input = %.1f V\n", state.value, volts);
    if (state.value != 10 || state.floating || state.contention || volts != 2.5) result = 1;
done:
    if (api) api->destroy(context);
    return result;
}
