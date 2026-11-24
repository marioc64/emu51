// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#ifndef EMU51_PERIPHERAL_SNAPSHOT_H
#define EMU51_PERIPHERAL_SNAPSHOT_H
#include "peripheral.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Reusable electrical workspace. Overrides and samples never invoke callbacks,
 * alter live pins or advance time. The host serializes access. */
typedef struct e51p_snapshot e51p_snapshot;
e51p_status e51p_snapshot_create(const e51p_context*, e51p_snapshot**);
void e51p_snapshot_destroy(e51p_snapshot*);
e51p_status e51p_snapshot_begin(e51p_snapshot*);
e51p_status e51p_snapshot_drive(e51p_snapshot*, e51p_endpoint, uint32_t value,
    uint32_t enable, uint32_t open_drain, uint32_t undefined);
e51p_status e51p_snapshot_sample(const e51p_snapshot*, e51p_endpoint, e51p_digital_state*);
e51p_status e51p_digital_net(const e51p_context*, e51p_endpoint, uint32_t bit, uint32_t* net);
/* Direct dispatch for pin-routed targets. PEEK samples the workspace and cannot
 * drive signals. READ/WRITE commit only after transaction validation. These
 * extensions leave the ABI v1 function table and descriptors unchanged. */
e51p_status e51p_target_read(e51p_context*, e51p_endpoint, uint32_t address,
    uint32_t access, const e51p_snapshot*, uint32_t* value);
e51p_status e51p_target_write(e51p_context*, e51p_endpoint, uint32_t address,
    uint32_t value, const e51p_snapshot*);
typedef e51p_status (*e51p_write_validator)(void* user, uint32_t address, uint32_t value);
e51p_status e51p_target_set_write_validator(e51p_context*, e51p_endpoint,
    e51p_write_validator, void* user);
e51p_status e51p_target_check_write(e51p_context*, e51p_endpoint, uint32_t address, uint32_t value, const e51p_snapshot*);
#ifdef __cplusplus
}
#endif
#endif
