// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#ifndef E51P_EXAMPLE_PARALLEL_EXPANDER_H
#define E51P_EXAMPLE_PARALLEL_EXPANDER_H
#include "emu51/peripheral.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Generic four-register example, NOT an 8255 implementation. Registers 0..2
 * are parallel ports A/B/C. Register 3 bits 0..2 enable each whole port's
 * output drivers. Reset releases all outputs. Storage lives with the caller. */
typedef struct e51p_parallel_expander {
    const e51p_api* api;
    e51p_endpoint bus, ports[3];
    uint32_t latch[3], output_enable;
} e51p_parallel_expander;

e51p_status e51p_parallel_expander_add(const e51p_api*, e51p_context*,
    const char* instance_id, e51p_parallel_expander*);

#ifdef __cplusplus
}
#endif
#endif
