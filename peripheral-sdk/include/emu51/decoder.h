// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#ifndef EMU51_PERIPHERAL_DECODER_H
#define EMU51_PERIPHERAL_DECODER_H
#include "peripheral.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct e51p_decoder e51p_decoder;
typedef struct e51p_decoder_output {
    uint32_t struct_size;
    const char* id;
    uint32_t active_low;
    const uint32_t* values;
    uint32_t value_count;
} e51p_decoder_output;
/* Immutable sparse lookup, 1..32 input bits. Copies IDs and assignments.
 * Every key belongs to exactly one output; even same-output duplicates and
 * duplicate IDs are conflicts. No allocation proportional to 2^input_bits. */
e51p_status e51p_decoder_create(uint32_t input_bits, const e51p_decoder_output*,
    uint32_t output_count, e51p_decoder**);
void e51p_decoder_destroy(e51p_decoder*);
/* Attach a combinational device to ordinary digital nets. The context retains
 * the immutable lookup until destruction. Unknown inputs release outputs;
 * functional bus snapshots additionally propagate their undefined state. */
e51p_status e51p_decoder_attach(const e51p_api*, e51p_context*, const char* id,
    e51p_decoder*, e51p_endpoint* input, e51p_endpoint* outputs, uint32_t capacity);
uint32_t e51p_decoder_input_bits(const e51p_decoder*);
uint32_t e51p_decoder_output_count(const e51p_decoder*);
const char* e51p_decoder_output_id(const e51p_decoder*, uint32_t output);
/* NOT_FOUND means all outputs inactive; INVALID_STATE means unresolved input.
 * selected is INVALID_ID on either result. No signals/time are changed. */
e51p_status e51p_decoder_select(const e51p_decoder*, e51p_digital_state input, uint32_t* selected);
e51p_status e51p_decoder_level(const e51p_decoder*, uint32_t output,
    e51p_digital_state input, e51p_digital_state* level);
#ifdef __cplusplus
}
#endif
#endif
