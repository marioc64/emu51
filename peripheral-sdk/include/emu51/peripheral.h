// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#ifndef EMU51_PERIPHERAL_H
#define EMU51_PERIPHERAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Experimental ABI: request the exact version and check struct_size before
 * using a table. No emulator, C++ or GUI types cross this interface. */
#define E51P_ABI_VERSION UINT32_C(1)
#define E51P_INVALID_ID UINT32_MAX

typedef struct e51p_context e51p_context;
typedef uint32_t e51p_device;
typedef uint32_t e51p_endpoint;
typedef uint32_t e51p_status;

enum {
    E51P_OK = 0,
    E51P_INVALID_ARGUMENT = 1,
    E51P_INVALID_STATE = 2,
    E51P_NOT_FOUND = 3,
    E51P_CONFLICT = 4,      /* rejected topology, not an electrical conflict */
    E51P_CONTENTION = 5,    /* signal update committed; inspect contention mask */
    E51P_OSCILLATION = 6,   /* propagation did not converge; reset is required */
    E51P_UNSUPPORTED = 7,
    E51P_NO_MEMORY = 8,
    E51P_DEVICE_ERROR = 9
};

enum {
    E51P_DIGITAL = 1,
    E51P_BUS_INITIATOR = 2,
    E51P_BUS_TARGET = 3,
    E51P_ANALOG = 4
};
enum { E51P_INPUT = 1, E51P_OUTPUT = 2 };
enum { E51P_READ = 0, E51P_PEEK = 1 };

typedef struct e51p_digital_state {
    uint32_t value;
    uint32_t floating;     /* no active driver and no pull resistor */
    uint32_t contention;   /* conflicting drivers, or conflicting weak pulls */
} e51p_digital_state;

/* All callbacks run on the caller's simulation thread, must return promptly,
 * and must not throw. Signal callbacks may drive signals, which propagate in
 * subsequent delta rounds at the same timestamp. Lifecycle, bus and time
 * operations cannot be called recursively. Peek must have no side effects.
 * user stays caller-owned unless a destroy callback is supplied; successful
 * add_device transfers its cleanup responsibility to the context. */
typedef struct e51p_device_desc {
    uint32_t struct_size;
    const char* instance_id;
    void* user;
    void (*destroy)(void* user);
    void (*reset)(void* user, e51p_context* context);
    void (*advance)(void* user, e51p_context* context, uint64_t from, uint64_t to);
    e51p_status (*bus_read)(void* user, e51p_context* context,
        e51p_endpoint endpoint, uint32_t address, uint32_t access, uint32_t* value);
    e51p_status (*bus_write)(void* user, e51p_context* context,
        e51p_endpoint endpoint, uint32_t address, uint32_t value);
    void (*digital_changed)(void* user, e51p_context* context,
        e51p_endpoint endpoint, e51p_digital_state state, uint64_t time);
    void (*analog_changed)(void* user, e51p_context* context,
        e51p_endpoint endpoint, double volts, uint64_t time);
} e51p_device_desc;

typedef struct e51p_endpoint_desc {
    uint32_t struct_size;
    const char* name;      /* unique within this device; copied by the runtime */
    uint32_t kind;
    uint32_t direction;   /* digital: INPUT/OUTPUT/both; analog: one direction */
    uint32_t width;       /* digital bits or bus data bits: 1..32; analog: 1 */
    uint32_t address_bits; /* bus only: 1..32 */
    uint32_t pull_up;     /* digital only, masks in endpoint bit order */
    uint32_t pull_down;
} e51p_endpoint_desc;

typedef struct e51p_api {
    uint32_t struct_size;
    uint32_t abi_version;
    /* One tick = 1/tick_hz seconds. Frequency is fixed for the context lifetime.
     * Handles are context-local and remain valid until context destruction. */
    e51p_status (*create)(uint64_t tick_hz, e51p_context** context);
    void (*destroy)(e51p_context* context);
    e51p_status (*add_device)(e51p_context*, const e51p_device_desc*, e51p_device*);
    e51p_status (*add_endpoint)(e51p_context*, e51p_device,
        const e51p_endpoint_desc*, e51p_endpoint*);
    /* Bidirectional electrical connection, including port slices. Call once
     * per bit for arbitrary permutations or groups spanning several ports. */
    e51p_status (*connect_digital)(e51p_context*, e51p_endpoint a, uint32_t a_lsb,
        e51p_endpoint b, uint32_t b_lsb, uint32_t width);
    /* Analog outputs fan out; an input has exactly one source, in volts. */
    e51p_status (*connect_analog)(e51p_context*, e51p_endpoint output, e51p_endpoint input);
    /* Maps [base, base+size) to [target_base, target_base+size). Overlapping
     * windows on one initiator are rejected before changing the topology. */
    e51p_status (*map_bus)(e51p_context*, e51p_endpoint initiator, uint32_t base,
        uint64_t size, e51p_endpoint target, uint32_t target_base);
    /* start seals topology and resets all devices. reset retains wiring and
     * tick_hz, releases drives, clears analog validity, and sets time to zero. */
    e51p_status (*start)(e51p_context*);
    e51p_status (*reset)(e51p_context*);
    e51p_status (*advance_to)(e51p_context*, uint64_t time);
    e51p_status (*clock)(const e51p_context*, uint64_t* time, uint64_t* tick_hz);
    /* drive replaces the endpoint's complete output state. Enabled bits are
     * push-pull unless in open_drain; open-drain high releases the line.
     * Updates are committed even when propagation reports CONTENTION. */
    e51p_status (*drive)(e51p_context*, e51p_endpoint, uint32_t value,
        uint32_t enable, uint32_t open_drain);
    e51p_status (*sample)(const e51p_context*, e51p_endpoint, e51p_digital_state*);
    e51p_status (*analog_write)(e51p_context*, e51p_endpoint, double volts);
    e51p_status (*analog_read)(const e51p_context*, e51p_endpoint, double* volts);
    /* Reads/writes settle signal changes before returning. Unmapped accesses
     * return NOT_FOUND so a host can fall back to its ordinary backing memory.
     * A mapped callback returning NOT_FOUND is converted to DEVICE_ERROR.
     * PEEK and sample/analog_read are passive and do not advance time. */
    e51p_status (*bus_read)(e51p_context*, e51p_endpoint initiator,
        uint32_t address, uint32_t access, uint32_t* value);
    e51p_status (*bus_write)(e51p_context*, e51p_endpoint initiator,
        uint32_t address, uint32_t value);
} e51p_api;

e51p_status e51p_get_api(uint32_t abi_version, const e51p_api** api);

#ifdef __cplusplus
}
#endif
#endif
