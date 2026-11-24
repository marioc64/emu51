// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#include "emu51/registry.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void ok(e51p_status s) { check(s == E51P_OK, "Registry operation succeeds"); }
struct Graph {
    const e51p_api* api = nullptr;
    e51p_context* context = nullptr;
    e51p_registry* registry = nullptr;
    Graph() {
        ok(e51p_get_api(E51P_ABI_VERSION, &api)); ok(api->create(12000000, &context));
        ok(e51p_registry_create(E51P_REGISTRY_ABI_VERSION, &registry));
    }
    ~Graph() { api->destroy(context); e51p_registry_destroy(registry); }
};
struct Probe {
    int calls = 0;
    std::vector<e51p_parameter_value> values;
    std::string string;
    static e51p_status create(void* user, const e51p_api*, e51p_context*, const char*,
        const e51p_parameter_value* values, uint32_t count, e51p_endpoint*) {
        auto& probe = *static_cast<Probe*>(user); ++probe.calls;
        probe.values.assign(values, values + count);
        if (count == 4) probe.string = values[3].string;
        return E51P_OK;
    }
};
void validation_and_ownership() {
    Graph g;
    Probe probe;
    char id[] = "example.configurable", label[] = "Configurable", key[] = "integer", defaultText[] = "hello";
    e51p_parameter_desc params[] = {
        {sizeof(e51p_parameter_desc), key, "Integer", E51P_PARAMETER_INTEGER, 7, -10, 10, 0, 0, 0, nullptr, 0},
        {sizeof(e51p_parameter_desc), "real", "Real", E51P_PARAMETER_REAL, 0, 0, 0, 2.5, -5, 5, nullptr, 0},
        {sizeof(e51p_parameter_desc), "boolean", "Boolean", E51P_PARAMETER_BOOLEAN, 1, 0, 0, 0, 0, 0, nullptr, 0},
        {sizeof(e51p_parameter_desc), "string", "String", E51P_PARAMETER_STRING, 0, 0, 0, 0, 0, 0, defaultText, 8},
    };
    e51p_factory_desc desc{sizeof(desc), id, label, params, 4, nullptr, 0, &probe, Probe::create};
    ok(e51p_registry_register(g.registry, &desc));
    const e51p_factory_desc* saved = nullptr;
    ok(e51p_registry_find(g.registry, id, &saved));
    check(e51p_registry_register(g.registry, &desc) == E51P_CONFLICT, "Duplicate factory IDs are rejected");
    id[0] = label[0] = key[0] = defaultText[0] = 'X';
    params[0].integer_default = 99;
    ok(e51p_registry_add_builtins(g.registry));
    check(std::strcmp(saved->type_id, "example.configurable") == 0 && std::strcmp(saved->label, "Configurable") == 0 &&
        std::strcmp(saved->parameters[0].name, "integer") == 0 && saved->parameters[0].integer_default == 7 &&
        std::strcmp(saved->parameters[3].string_default, "hello") == 0,
        "Metadata strings, defaults and arrays are deeply copied and stable across registry growth");
    ok(e51p_registry_instantiate(g.registry, saved->type_id, g.api, g.context, "default", nullptr, 0, nullptr, 0));
    check(probe.calls == 1 && probe.values.size() == 4 && probe.values[0].integer == 7 &&
        probe.values[1].real == 2.5 && probe.values[2].integer == 1 && probe.string == "hello", "All typed defaults reach callback in schema order");
    e51p_parameter_value values[] = {
        {sizeof(e51p_parameter_value), "string", E51P_PARAMETER_STRING, 0, 0, "override"},
        {sizeof(e51p_parameter_value), "real", E51P_PARAMETER_REAL, 0, -5, nullptr},
        {sizeof(e51p_parameter_value), "integer", E51P_PARAMETER_INTEGER, -10, 0, nullptr},
        {sizeof(e51p_parameter_value), "boolean", E51P_PARAMETER_BOOLEAN, 0, 0, nullptr},
    };
    ok(e51p_registry_instantiate(g.registry, saved->type_id, g.api, g.context, "overrides", values, 4, nullptr, 0));
    check(probe.values[0].integer == -10 && probe.values[1].real == -5 && !probe.values[2].integer && probe.string == "override",
        "Named overrides are reordered and inclusive range boundaries are accepted");
    const auto rejected = [&](e51p_parameter_value v) {
        const auto before = probe.calls;
        check(e51p_registry_instantiate(g.registry, saved->type_id, g.api, g.context, "invalid", &v, 1, nullptr, 0) == E51P_INVALID_ARGUMENT && probe.calls == before,
            "Invalid configuration is rejected before factory side effects");
    };
    auto bad = values[0]; bad.name = "unknown"; rejected(bad);
    bad = values[2]; bad.integer = 11; rejected(bad);
    bad = values[2]; bad.type = E51P_PARAMETER_REAL; rejected(bad);
    bad = values[3]; bad.integer = 2; rejected(bad);
    bad = values[0]; bad.string = nullptr; rejected(bad);
    bad = values[0]; bad.string = "too long!"; rejected(bad);
    bad = values[1]; bad.real = std::numeric_limits<double>::quiet_NaN(); rejected(bad);
    bad.real = std::numeric_limits<double>::infinity(); rejected(bad);
    bad = values[1]; bad.struct_size = 1; rejected(bad);
    const e51p_parameter_value duplicate[]{values[0], values[0]};
    const auto before = probe.calls;
    check(e51p_registry_instantiate(g.registry, saved->type_id, g.api, g.context, "duplicate", duplicate, 2, nullptr, 0) == E51P_INVALID_ARGUMENT && probe.calls == before,
        "Duplicate parameter keys are rejected before creation");
    uint32_t count = 0; ok(e51p_registry_count(g.registry, &count));
    check(count == 26, "Built-ins append to a user registry");
    uint32_t cleared = 42;
    check(e51p_registry_count(nullptr, &cleared) == E51P_INVALID_ARGUMENT && cleared == 0,
        "Invalid count queries clear their output");
    check(e51p_registry_at(g.registry, count, &saved) == E51P_NOT_FOUND && !saved, "Out-of-range catalog queries clear output");
    check(e51p_registry_find(g.registry, "missing", &saved) == E51P_NOT_FOUND && !saved, "Unknown type lookup clears output");
}
e51p_status empty(void*, const e51p_api*, e51p_context*, const char*, const e51p_parameter_value*, uint32_t, e51p_endpoint*) { return E51P_OK; }
void descriptor_validation_and_atomicity() {
    Graph g;
    const e51p_factory_desc* d = nullptr;
    ok(e51p_registry_add_builtins(g.registry));
    ok(e51p_registry_find(g.registry, "emu51.adc0804", &d));
    e51p_registry* other = nullptr; ok(e51p_registry_create(E51P_REGISTRY_ABI_VERSION, &other));
    ok(e51p_registry_register(other, d));
    check(e51p_registry_add_builtins(other) == E51P_CONFLICT, "Built-in registration rejects a partial collision");
    uint32_t count = 0; ok(e51p_registry_count(other, &count));
    check(count == 1, "Failed built-in registration leaves every previous entry unchanged");
    e51p_registry_destroy(other);
    e51p_factory_desc invalid{sizeof(invalid), "example.invalid", "Invalid", nullptr, 0, nullptr, 0, nullptr, empty};
    invalid.struct_size = 1; check(e51p_registry_register(g.registry, &invalid) == E51P_INVALID_ARGUMENT, "Truncated factory descriptor rejected");
    invalid.struct_size = sizeof(invalid); invalid.type_id = "bad id";
    check(e51p_registry_register(g.registry, &invalid) == E51P_INVALID_ARGUMENT, "Invalid stable type ID rejected");
    invalid.type_id = "example.invalid";
    e51p_parameter_desc p{sizeof(p), "value", "Value", E51P_PARAMETER_REAL, 0, 0, 0, 99, -5, 5, nullptr, 0};
    invalid.parameters = &p; invalid.parameter_count = 1;
    check(e51p_registry_register(g.registry, &invalid) == E51P_INVALID_ARGUMENT, "Invalid parameter defaults rejected");
    p.real_default = 0; p.real_max = std::numeric_limits<double>::infinity();
    check(e51p_registry_register(g.registry, &invalid) == E51P_INVALID_ARGUMENT, "Infinite parameter limits rejected");
    invalid.parameter_count = 0;
    e51p_endpoint_desc pin{sizeof(pin), "out", E51P_DIGITAL, E51P_OUTPUT, 8, 0, 0x100, 0};
    invalid.pins = &pin; invalid.pin_count = 1;
    check(e51p_registry_register(g.registry, &invalid) == E51P_INVALID_ARGUMENT, "Out-of-width pull mask rejected");
    pin.pull_up = 0; pin.kind = E51P_ANALOG;
    check(e51p_registry_register(g.registry, &invalid) == E51P_INVALID_ARGUMENT, "Invalid analog width rejected");
    pin.kind = E51P_DIGITAL;
    e51p_endpoint_desc pins[]{pin, pin}; invalid.pins = pins; invalid.pin_count = 2;
    check(e51p_registry_register(g.registry, &invalid) == E51P_INVALID_ARGUMENT, "Duplicate pin names rejected");
    invalid.pins = &pin; invalid.pin_count = 1; ok(e51p_registry_register(g.registry, &invalid));
    // A callback that does not fill its advertised pins cannot report success.
    e51p_endpoint output = 42;
    check(e51p_registry_instantiate(g.registry, invalid.type_id, g.api, g.context, "bad", nullptr, 0, &output, 1) == E51P_DEVICE_ERROR && output == E51P_INVALID_ID,
        "Unfilled callback pin results fail without exposing partial handles");
}
struct Failure {
    e51p_registry* registry;
    bool shouldThrow = false;
    bool badAllocation = false;
    e51p_status recursive = E51P_OK;
    static e51p_status create(void* user, const e51p_api* api, e51p_context* context, const char*,
        const e51p_parameter_value*, uint32_t, e51p_endpoint* pins) {
        auto& f = *static_cast<Failure*>(user);
        f.recursive = e51p_registry_instantiate(f.registry, "example.failure", api, context, "recursive", nullptr, 0, pins, 1);
        pins[0] = 99;
        if (f.badAllocation) throw std::bad_alloc();
        if (f.shouldThrow) throw std::runtime_error("device failed");
        return E51P_UNSUPPORTED;
    }
};
void failure_boundaries() {
    Graph g; Failure failure{g.registry};
    const e51p_endpoint_desc pin{sizeof(pin), "out", E51P_DIGITAL, E51P_OUTPUT, 8, 0, 0, 0};
    const e51p_factory_desc desc{sizeof(desc), "example.failure", "Failure", nullptr, 0, &pin, 1, &failure, Failure::create};
    ok(e51p_registry_register(g.registry, &desc));
    e51p_endpoint output = 42;
    check(e51p_registry_instantiate(g.registry, desc.type_id, g.api, g.context, "short", nullptr, 0, nullptr, 0) == E51P_INVALID_ARGUMENT && failure.recursive == E51P_OK,
        "Insufficient output capacity is rejected before callback");
    check(e51p_registry_instantiate(g.registry, desc.type_id, g.api, g.context, "failure", nullptr, 0, &output, 1) == E51P_UNSUPPORTED &&
        output == E51P_INVALID_ID && failure.recursive == E51P_INVALID_STATE, "Factory failures clear outputs and recursive creation is rejected");
    failure.shouldThrow = true;
    check(e51p_registry_instantiate(g.registry, desc.type_id, g.api, g.context, "throw", nullptr, 0, &output, 1) == E51P_DEVICE_ERROR && output == E51P_INVALID_ID,
        "Callback exception cannot cross the C ABI or expose partial handles");
    failure.badAllocation = true;
    check(e51p_registry_instantiate(g.registry, desc.type_id, g.api, g.context, "allocate", nullptr, 0, &output, 1) == E51P_NO_MEMORY && output == E51P_INVALID_ID,
        "Allocation exceptions retain their specific status at the C boundary");
    ok(e51p_registry_add_builtins(g.registry)); // exception unwound the busy guard
    e51p_registry* registry = reinterpret_cast<e51p_registry*>(1);
    check(e51p_registry_create(999, &registry) == E51P_UNSUPPORTED && !registry, "Unsupported registry ABI clears output");
}
void catalog_limits_and_pin_ownership() {
    Graph g;
    char name[] = "input";
    e51p_endpoint_desc pin{sizeof(pin), name, E51P_DIGITAL, E51P_INPUT, 4, 0, 15, 0};
    e51p_factory_desc desc{sizeof(desc), "example.pins", "Pins", nullptr, 0, &pin, 1, nullptr, empty};
    ok(e51p_registry_register(g.registry, &desc));
    const e51p_factory_desc* saved = nullptr;
    ok(e51p_registry_find(g.registry, desc.type_id, &saved));
    name[0] = 'X'; pin.width = 8;
    check(std::strcmp(saved->pins[0].name, "input") == 0 && saved->pins[0].width == 4 && saved->pins[0].pull_up == 15,
        "Pin names and electrical descriptions are deeply copied");
    desc.pins = nullptr; desc.pin_count = 0;
    for (unsigned i = 1; i != 256; ++i) {
        const auto id = "example.type" + std::to_string(i); desc.type_id = id.c_str();
        ok(e51p_registry_register(g.registry, &desc));
    }
    const e51p_factory_desc* after = nullptr;
    ok(e51p_registry_find(g.registry, "example.pins", &after));
    check(saved == after && std::strcmp(saved->pins[0].name, "input") == 0,
        "Metadata addresses survive vector growth through the entire catalog");
    desc.type_id = "example.overflow";
    check(e51p_registry_register(g.registry, &desc) == E51P_INVALID_STATE,
        "Factory count is bounded without modifying existing registrations");
    uint32_t count = 0; ok(e51p_registry_count(g.registry, &count));
    check(count == 256, "Catalog retains exactly the supported number of factories");
}
void builtin_devices() {
    Graph g; ok(e51p_registry_add_builtins(g.registry));
    e51p_endpoint ppi[5], adc[7], dac[2];
    const e51p_parameter_value reference{sizeof(reference), "reference_volts", E51P_PARAMETER_REAL, 0, 3.3, nullptr};
    ok(e51p_registry_instantiate(g.registry, "emu51.i8255.mode0", g.api, g.context, "ppi", nullptr, 0, ppi, 5));
    ok(e51p_registry_instantiate(g.registry, "emu51.adc0804", g.api, g.context, "adc", nullptr, 0, adc, 7));
    ok(e51p_registry_instantiate(g.registry, "emu51.parallel-dac", g.api, g.context, "dac", &reference, 1, dac, 2));
    // Use a PPI output port as the DAC input. Create a bus master separately.
    e51p_device_desc host{}; host.struct_size = sizeof(host); host.instance_id = "host";
    e51p_device device; ok(g.api->add_device(g.context, &host, &device));
    e51p_endpoint_desc busDesc{sizeof(busDesc), "bus", E51P_BUS_INITIATOR, 0, 8, 16, 0, 0};
    e51p_endpoint bus; ok(g.api->add_endpoint(g.context, device, &busDesc, &bus));
    ok(g.api->connect_digital(g.context, ppi[2], 0, dac[0], 0, 8));
    ok(g.api->map_bus(g.context, bus, 0x8000, 4, ppi[0], 0));
    ok(g.api->start(g.context));
    ok(g.api->bus_write(g.context, bus, 0x8003, 0x98));
    ok(g.api->bus_write(g.context, bus, 0x8001, 128));
    double voltage = 0; ok(g.api->analog_read(g.context, dac[1], &voltage));
    check(voltage == 1.65, "Factories create working bus/port/analog devices with supplied parameters");
    ok(g.api->reset(g.context)); ok(g.api->analog_read(g.context, dac[1], &voltage));
    check(voltage == 0, "Factory-created devices retain normal reset behavior");
}
}
int main() {
    validation_and_ownership(); descriptor_validation_and_atomicity(); failure_boundaries(); catalog_limits_and_pin_ownership(); builtin_devices();
    std::puts("Peripheral factory registry tests passed");
}
