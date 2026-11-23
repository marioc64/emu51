// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#ifndef EMU51_PERIPHERAL_REGISTRY_H
#define EMU51_PERIPHERAL_REGISTRY_H
#include "peripheral.h"

#ifdef __cplusplus
extern "C" {
#endif

#define E51P_REGISTRY_ABI_VERSION UINT32_C(1)
typedef struct e51p_registry e51p_registry;
enum { E51P_PARAMETER_INTEGER = 1, E51P_PARAMETER_REAL = 2,
    E51P_PARAMETER_BOOLEAN = 3, E51P_PARAMETER_STRING = 4 };

/* Zero-initialize descriptors and values, then set fields for the chosen type. */
typedef struct e51p_parameter_desc {
    uint32_t struct_size;
    const char* name;              /* stable configuration key */
    const char* label;             /* display text */
    uint32_t type;
    int64_t integer_default, integer_min, integer_max;
    double real_default, real_min, real_max;
    const char* string_default;
    uint32_t string_max_length;    /* bytes excluding NUL, 0..4096 */
} e51p_parameter_desc;

typedef struct e51p_parameter_value {
    uint32_t struct_size;
    const char* name;
    uint32_t type;
    int64_t integer;               /* INTEGER or BOOLEAN (0 or 1) */
    double real;                  /* REAL; finite, no implicit coercion */
    const char* string;            /* STRING; copied if retained by a device */
} e51p_parameter_value;

typedef struct e51p_factory_desc {
    uint32_t struct_size;
    const char* type_id;           /* stable, namespaced ID; e.g. emu51.adc0804 */
    const char* label;
    const e51p_parameter_desc* parameters;
    uint32_t parameter_count;
    /* Logical pin names and electrical/bus metadata. Callback results follow
     * this order; names may be aliases for a model's native endpoint names. */
    const e51p_endpoint_desc* pins;
    uint32_t pin_count;
    void* user;
    /* Called only after full parameter validation, with all defaults inserted
     * in descriptor order. Borrowed arguments are valid until return. Register
     * all device state/destroy callbacks with context; do not retain pin_handles
     * or the values array. No graph start, wiring, host I/O or exceptions here.
     * On failure the host must discard the context: registration may be partial.
     * Callback code must remain loaded while its device contexts exist. */
    e51p_status (*create)(void* user, const e51p_api*, e51p_context*, const char* instance_id,
        const e51p_parameter_value* values, uint32_t value_count, e51p_endpoint* pin_handles);
} e51p_factory_desc;

/* Simulation/control-thread API, not audio-callback safe. No concurrent or
 * reentrant mutation. Up to 256 factories, 64 parameters and 64 pins each.
 * Registry metadata (including strings) is copied at registration; user is
 * borrowed. Returned metadata remains valid until registry destruction, even
 * after more registrations. Destroying a registry does not destroy devices.
 * A callback must not destroy the registry invoking it. */
e51p_status e51p_registry_create(uint32_t abi_version, e51p_registry**);
void e51p_registry_destroy(e51p_registry*);
e51p_status e51p_registry_register(e51p_registry*, const e51p_factory_desc*);
/* Transactionally adds emu51.i8255.mode0, emu51.adc0804 and emu51.parallel-dac.
 * Does not replace an existing type ID or load/open any host resources. */
e51p_status e51p_registry_add_builtins(e51p_registry*);
e51p_status e51p_registry_count(const e51p_registry*, uint32_t* count);
e51p_status e51p_registry_at(const e51p_registry*, uint32_t index, const e51p_factory_desc**);
e51p_status e51p_registry_find(const e51p_registry*, const char* type_id, const e51p_factory_desc**);
/* Unknown/duplicate keys, type/range errors, bad descriptors and insufficient
 * pin capacity are rejected before calling the factory. For valid capacity
 * (0..64), pin_handles is cleared to INVALID_ID on failure; it must cover pin_count.
 * Create in a fresh configuring context and discard it on any factory failure.
 * The ordinary peripheral ABI and direct device constructors remain available. */
e51p_status e51p_registry_instantiate(e51p_registry*, const char* type_id,
    const e51p_api*, e51p_context*, const char* instance_id,
    const e51p_parameter_value*, uint32_t value_count,
    e51p_endpoint* pin_handles, uint32_t pin_capacity);

#ifdef __cplusplus
}
#endif
#endif
