// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#include "registry_internal.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace {
constexpr uint32_t maxFactories = 256, maxFields = 64;
bool text(const char* s, std::size_t limit, bool nonempty = true) {
    if (!s || (nonempty && !*s)) return false;
    for (std::size_t i = 0; i <= limit; ++i) if (!s[i]) return true;
    return false;
}
bool identifier(const char* s, std::size_t limit) {
    if (!text(s, limit)) return false;
    for (; *s; ++s) if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
        (*s >= '0' && *s <= '9') || *s == '.' || *s == '-' || *s == '_')) return false;
    return true;
}
bool parameter(const e51p_parameter_desc& p) {
    if (p.struct_size < sizeof(p) || !identifier(p.name, 64) || !text(p.label, 256)) return false;
    switch (p.type) {
    case E51P_PARAMETER_INTEGER:
        return p.integer_min <= p.integer_default && p.integer_default <= p.integer_max;
    case E51P_PARAMETER_REAL:
        return std::isfinite(p.real_default) && std::isfinite(p.real_min) && std::isfinite(p.real_max) &&
            p.real_min <= p.real_default && p.real_default <= p.real_max;
    case E51P_PARAMETER_BOOLEAN: return p.integer_default == 0 || p.integer_default == 1;
    case E51P_PARAMETER_STRING:
        return p.string_max_length <= 4096 && text(p.string_default, p.string_max_length, false);
    default: return false;
    }
}
bool pin(const e51p_endpoint_desc& p) {
    if (p.struct_size < sizeof(p) || !identifier(p.name, 64) || p.width < 1 || p.width > 32) return false;
    const auto mask = p.width == 32 ? UINT32_MAX : (UINT32_C(1) << p.width) - 1;
    switch (p.kind) {
    case E51P_DIGITAL:
        return p.direction >= E51P_INPUT && p.direction <= (E51P_INPUT | E51P_OUTPUT) && !p.address_bits &&
            !((p.pull_up | p.pull_down) & ~mask) && !(p.pull_up & p.pull_down);
    case E51P_ANALOG:
        return p.width == 1 && (p.direction == E51P_INPUT || p.direction == E51P_OUTPUT) &&
            !p.address_bits && !p.pull_up && !p.pull_down;
    case E51P_BUS_INITIATOR:
    case E51P_BUS_TARGET:
        return !p.direction && p.address_bits >= 1 && p.address_bits <= 32 && !p.pull_up && !p.pull_down;
    default: return false;
    }
}
bool valid(const e51p_factory_desc& d) {
    if (d.struct_size < sizeof(d) || !identifier(d.type_id, 96) || !text(d.label, 256) || !d.create ||
        d.parameter_count > maxFields || d.pin_count > maxFields ||
        (d.parameter_count && !d.parameters) || (d.pin_count && !d.pins)) return false;
    for (uint32_t i = 0; i < d.parameter_count; ++i) {
        if (!parameter(d.parameters[i])) return false;
        for (uint32_t j = 0; j < i; ++j) if (std::string(d.parameters[i].name) == d.parameters[j].name) return false;
    }
    for (uint32_t i = 0; i < d.pin_count; ++i) {
        if (!pin(d.pins[i])) return false;
        for (uint32_t j = 0; j < i; ++j) if (std::string(d.pins[i].name) == d.pins[j].name) return false;
    }
    return true;
}
struct Factory {
    e51p_factory_desc desc;
    std::string id, label;
    std::vector<e51p_parameter_desc> parameters;
    std::vector<e51p_endpoint_desc> pins;
    std::vector<std::string> names, labels, defaults, pinNames;
    explicit Factory(const e51p_factory_desc& source) : desc(source), id(source.type_id), label(source.label) {
        names.reserve(source.parameter_count); labels.reserve(source.parameter_count); defaults.reserve(source.parameter_count);
        parameters.reserve(source.parameter_count); pins.reserve(source.pin_count); pinNames.reserve(source.pin_count);
        for (uint32_t i = 0; i < source.parameter_count; ++i) {
            const auto& p = source.parameters[i];
            e51p_parameter_desc copy{}; copy.struct_size = sizeof(copy); copy.type = p.type;
            switch (p.type) {
            case E51P_PARAMETER_INTEGER:
                copy.integer_default = p.integer_default; copy.integer_min = p.integer_min; copy.integer_max = p.integer_max; break;
            case E51P_PARAMETER_BOOLEAN: copy.integer_default = p.integer_default; break;
            case E51P_PARAMETER_REAL:
                copy.real_default = p.real_default; copy.real_min = p.real_min; copy.real_max = p.real_max; break;
            case E51P_PARAMETER_STRING: copy.string_max_length = p.string_max_length; break;
            }
            parameters.push_back(copy);
            names.emplace_back(p.name); labels.emplace_back(p.label);
            defaults.emplace_back(p.type == E51P_PARAMETER_STRING ? p.string_default : "");
        }
        for (uint32_t i = 0; i < source.pin_count; ++i) { pins.push_back(source.pins[i]); pinNames.emplace_back(source.pins[i].name); }
        // Bind only after string storage is final (including short strings).
        for (std::size_t i = 0; i < parameters.size(); ++i) {
            parameters[i].struct_size = sizeof(e51p_parameter_desc);
            parameters[i].name = names[i].c_str(); parameters[i].label = labels[i].c_str();
            parameters[i].string_default = parameters[i].type == E51P_PARAMETER_STRING ? defaults[i].c_str() : nullptr;
        }
        for (std::size_t i = 0; i < pins.size(); ++i) {
            pins[i].struct_size = sizeof(e51p_endpoint_desc); pins[i].name = pinNames[i].c_str();
        }
        desc.struct_size = sizeof(desc); desc.type_id = id.c_str(); desc.label = label.c_str();
        desc.parameters = parameters.data(); desc.pins = pins.data();
    }
};
template<class F> e51p_status protect(F&& f) noexcept {
    try { return f(); }
    catch (const std::bad_alloc&) { return E51P_NO_MEMORY; }
    catch (...) { return E51P_DEVICE_ERROR; }
}
}
struct e51p_registry {
    std::vector<std::unique_ptr<Factory>> factories;
    bool busy = false;
};
namespace {
const Factory* find(const e51p_registry& r, const char* id) {
    for (const auto& f : r.factories) if (f->id == id) return f.get();
    return nullptr;
}
struct Busy {
    e51p_registry& registry;
    explicit Busy(e51p_registry& r) : registry(r) { registry.busy = true; }
    ~Busy() { registry.busy = false; }
};
}
extern "C" e51p_status e51p_registry_create(uint32_t version, e51p_registry** result) {
    if (!result) return E51P_INVALID_ARGUMENT;
    *result = nullptr;
    if (version != E51P_REGISTRY_ABI_VERSION) return E51P_UNSUPPORTED;
    return protect([&]() -> e51p_status { *result = new e51p_registry; return E51P_OK; });
}
extern "C" void e51p_registry_destroy(e51p_registry* r) { delete r; }
extern "C" e51p_status e51p_registry_register(e51p_registry* r, const e51p_factory_desc* d) {
    if (!r || !d) return E51P_INVALID_ARGUMENT;
    if (r->busy) return E51P_INVALID_STATE;
    return protect([&]() -> e51p_status {
        if (!valid(*d)) return E51P_INVALID_ARGUMENT;
        if (find(*r, d->type_id)) return E51P_CONFLICT;
        if (r->factories.size() == maxFactories) return E51P_INVALID_STATE;
        auto owned = std::make_unique<Factory>(*d);
        r->factories.push_back(std::move(owned)); return E51P_OK;
    });
}
extern "C" e51p_status e51p_registry_add_builtins(e51p_registry* r) {
    if (!r) return E51P_INVALID_ARGUMENT;
    if (r->busy) return E51P_INVALID_STATE;
    return protect([&]() -> e51p_status {
        std::size_t count = 0; const auto descriptors = e51p_builtin_factories(count);
        if (r->factories.size() + count > maxFactories) return E51P_INVALID_STATE;
        std::vector<std::unique_ptr<Factory>> pending;
        for (std::size_t i = 0; i < count; ++i) {
            if (!valid(*descriptors[i])) return E51P_DEVICE_ERROR;
            if (find(*r, descriptors[i]->type_id)) return E51P_CONFLICT;
            pending.push_back(std::make_unique<Factory>(*descriptors[i]));
        }
        r->factories.reserve(r->factories.size() + count);
        for (auto& f : pending) r->factories.push_back(std::move(f));
        return E51P_OK;
    });
}
extern "C" e51p_status e51p_registry_count(const e51p_registry* r, uint32_t* count) {
    if (!count) return E51P_INVALID_ARGUMENT;
    *count = 0;
    if (!r) return E51P_INVALID_ARGUMENT;
    *count = static_cast<uint32_t>(r->factories.size()); return E51P_OK;
}
extern "C" e51p_status e51p_registry_at(const e51p_registry* r, uint32_t index, const e51p_factory_desc** d) {
    if (!d) return E51P_INVALID_ARGUMENT;
    *d = nullptr;
    if (!r) return E51P_INVALID_ARGUMENT;
    if (index >= r->factories.size()) return E51P_NOT_FOUND;
    *d = &r->factories[index]->desc; return E51P_OK;
}
extern "C" e51p_status e51p_registry_find(const e51p_registry* r, const char* id, const e51p_factory_desc** d) {
    if (!d) return E51P_INVALID_ARGUMENT;
    *d = nullptr;
    if (!r || !identifier(id, 96)) return E51P_INVALID_ARGUMENT;
    const auto* f = find(*r, id);
    if (!f) return E51P_NOT_FOUND;
    *d = &f->desc; return E51P_OK;
}
extern "C" e51p_status e51p_registry_instantiate(e51p_registry* r, const char* id,
    const e51p_api* api, e51p_context* context, const char* instance,
    const e51p_parameter_value* supplied, uint32_t count, e51p_endpoint* handles, uint32_t capacity) {
    if (handles && capacity <= maxFields) std::fill(handles, handles + capacity, E51P_INVALID_ID);
    if (!r || !api || api->struct_size < sizeof(*api) || api->abi_version != E51P_ABI_VERSION ||
        !context || !text(instance, 128) || !identifier(id, 96) || count > maxFields || capacity > maxFields ||
        (count && !supplied) || (capacity && !handles)) return E51P_INVALID_ARGUMENT;
    if (r->busy) return E51P_INVALID_STATE;
    return protect([&]() -> e51p_status {
        const auto* factory = find(*r, id);
        if (!factory) return E51P_NOT_FOUND;
        const auto& d = factory->desc;
        if (capacity < d.pin_count) return E51P_INVALID_ARGUMENT;
        std::vector<e51p_parameter_value> values(d.parameter_count);
        std::vector<bool> seen(d.parameter_count);
        for (uint32_t i = 0; i < d.parameter_count; ++i) {
            const auto& p = d.parameters[i];
            values[i] = {sizeof(e51p_parameter_value), p.name, p.type, 0, 0, nullptr};
            switch (p.type) {
            case E51P_PARAMETER_INTEGER:
            case E51P_PARAMETER_BOOLEAN: values[i].integer = p.integer_default; break;
            case E51P_PARAMETER_REAL: values[i].real = p.real_default; break;
            case E51P_PARAMETER_STRING: values[i].string = p.string_default; break;
            }
        }
        for (uint32_t i = 0; i < count; ++i) {
            const auto& v = supplied[i];
            if (v.struct_size < sizeof(v) || !identifier(v.name, 64)) return E51P_INVALID_ARGUMENT;
            uint32_t index = 0;
            while (index < d.parameter_count && std::string(d.parameters[index].name) != v.name) ++index;
            if (index == d.parameter_count || seen[index] || v.type != d.parameters[index].type) return E51P_INVALID_ARGUMENT;
            seen[index] = true;
            const auto& p = d.parameters[index];
            auto& value = values[index];
            switch (p.type) {
            case E51P_PARAMETER_INTEGER:
                if (v.integer < p.integer_min || v.integer > p.integer_max) return E51P_INVALID_ARGUMENT;
                value.integer = v.integer; break;
            case E51P_PARAMETER_BOOLEAN:
                if (v.integer != 0 && v.integer != 1) return E51P_INVALID_ARGUMENT;
                value.integer = v.integer; break;
            case E51P_PARAMETER_REAL:
                if (!std::isfinite(v.real) || v.real < p.real_min || v.real > p.real_max) return E51P_INVALID_ARGUMENT;
                value.real = v.real; break;
            case E51P_PARAMETER_STRING:
                if (!text(v.string, p.string_max_length, false)) return E51P_INVALID_ARGUMENT;
                value.string = v.string; break;
            }
        }
        std::vector<e51p_endpoint> result(d.pin_count, E51P_INVALID_ID);
        Busy guard(*r);
        const auto status = d.create(d.user, api, context, instance, values.data(), d.parameter_count, result.data());
        if (status != E51P_OK) return status;
        for (const auto handle : result) if (handle == E51P_INVALID_ID) return E51P_DEVICE_ERROR;
        if (d.pin_count) std::copy(result.begin(), result.end(), handles);
        return E51P_OK;
    });
}
