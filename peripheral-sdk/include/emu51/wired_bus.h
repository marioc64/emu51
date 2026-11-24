// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#ifndef EMU51_PERIPHERAL_WIRED_BUS_H
#define EMU51_PERIPHERAL_WIRED_BUS_H
#include "memory.h"
#include "decoder.h"
#include "snapshot.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct e51p_wired_bus e51p_wired_bus;
typedef struct e51p_bus_pins {
    e51p_endpoint address, data, cs, oe, we;
} e51p_bus_pins;
typedef struct e51p_initiator_pins {
    e51p_endpoint address, data, code_read_n, data_read_n, data_write_n;
} e51p_initiator_pins;
enum { E51P_CODE_READ = 0, E51P_DATA_READ = 1, E51P_DATA_WRITE = 2 };
typedef struct e51p_bus_result {
    uint32_t value;
    uint32_t target; /* index from add_memory/add_target, or INVALID_ID */
    uint32_t physical_address;
} e51p_bus_result;
/* Configure before context start, compile afterwards. The bus retains memory
 * and owns decoder instances. Destroy the bus before destroying its context. */
e51p_status e51p_wired_bus_create(const e51p_api*, e51p_context*, const char* id,
    uint32_t address_bits, e51p_wired_bus**, e51p_initiator_pins*);
void e51p_wired_bus_destroy(e51p_wired_bus*);
e51p_status e51p_wired_bus_add_memory(e51p_wired_bus*, const char* id, e51p_memory*,
    uint32_t cs_low, uint32_t oe_low, uint32_t we_low, e51p_bus_pins*, uint32_t* target);
e51p_status e51p_wired_bus_add_target(e51p_wired_bus*, const char* id, e51p_endpoint legacy_target,
    uint32_t address_bits, uint32_t cs_low, uint32_t oe_low, uint32_t we_low,
    e51p_bus_pins*, uint32_t* target);
/* add_target address_bits=0 omits ADDR (INVALID_ID) and dispatches local address
 * zero, for a single latch or input buffer selected entirely by CS. */
e51p_status e51p_wired_bus_add_decoder(e51p_wired_bus*, const char* id, uint32_t input_bits,
    const e51p_decoder_output*, uint32_t count, e51p_endpoint* input, e51p_endpoint* outputs);
e51p_status e51p_wired_bus_compile(e51p_wired_bus*);
e51p_status e51p_wired_bus_access(e51p_wired_bus*, uint32_t kind, uint32_t address,
    uint32_t value, uint32_t peek, e51p_bus_result*);
e51p_status e51p_wired_bus_sample(e51p_wired_bus*, e51p_endpoint, e51p_digital_state*);
const char* e51p_wired_bus_error(const e51p_wired_bus*);
#ifdef __cplusplus
}
#endif
#endif
