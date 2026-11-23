// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#ifndef EMU51_PERIPHERAL_DEVICES_H
#define EMU51_PERIPHERAL_DEVICES_H
#include "peripheral.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ideal eight-bit parallel resistor-ladder DAC, suitable for Covox-style
 * wiring. vout = reference_volts * code / 256. No audio backend or filter.
 * Undefined digital inputs retain the previous valid output (zero at reset).
 * The context owns device storage. On partial setup failure discard the graph.
 * Result handles are written only on success. */
e51p_status e51p_add_parallel_dac(const e51p_api* api, e51p_context* context,
    const char* instance_id, double reference_volts,
    e51p_endpoint* digital_input, e51p_endpoint* analog_output);

typedef struct e51p_i8255_endpoints {
    uint32_t struct_size;
    e51p_endpoint bus;    /* 8-bit target; offsets 0=A, 1=B, 2=C, 3=control */
    e51p_endpoint port_a, port_b, port_c;
    e51p_endpoint reset;  /* active high, weak pull-down */
} e51p_i8255_endpoints;

/* Intel 8255A Mode 0 + Port C bit set/reset profile. Mode 1/2 writes return
 * UNSUPPORTED without changing the device. Control reads are unsupported.
 * Caller sets endpoints->struct_size; output is unchanged on setup failure.
 * All new device storage belongs to the context. */
e51p_status e51p_add_i8255_mode0(const e51p_api* api, e51p_context* context,
    const char* instance_id, e51p_i8255_endpoints* endpoints);

typedef struct e51p_adc0804_endpoints {
    uint32_t struct_size;
    e51p_endpoint data;   /* DB0..DB7, tri-state digital output */
    e51p_endpoint cs_n, rd_n, wr_n; /* active low, weak pull-ups */
    e51p_endpoint intr_n; /* active-low conversion complete, push-pull */
    e51p_endpoint vin_positive, vin_negative; /* analog volts */
} e51p_adc0804_endpoints;

/* Instruction-boundary ADC0804 profile. conversion_ticks must be nonzero;
 * full_scale_volts is the positive input span (2 * VREF/2). Quantization rounds
 * to the nearest 1/256 span, saturating to 0..255. The differential voltage is
 * sampled when CS/WR leave their both-low state; there is no analog SAR model.
 * An absent VIN- means 0 V. Absent VIN+ makes that conversion's data undefined
 * (high impedance), while conversion timing and INTR still complete.
 * Caller sets endpoints->struct_size; outputs are written only on success. */
e51p_status e51p_add_adc0804(const e51p_api* api, e51p_context* context,
    const char* instance_id, uint64_t conversion_ticks, double full_scale_volts,
    e51p_adc0804_endpoints* endpoints);

#ifdef __cplusplus
}
#endif
#endif
