// SPDX-License-Identifier: LGPL-2.1-only
#ifndef EMU51_DISPLAYS_H
#define EMU51_DISPLAYS_H
#include "peripheral.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct e51p_display e51p_display;
enum {
    E51P_DISPLAY_7SEG, E51P_DISPLAY_14SEG, E51P_DISPLAY_16SEG, E51P_DISPLAY_BAR,
    E51P_DISPLAY_CD4511, E51P_DISPLAY_LS47, E51P_DISPLAY_MAX7219,
    E51P_DISPLAY_MAX7221, E51P_DISPLAY_HT16K33, E51P_DISPLAY_TM1637,
    E51P_DISPLAY_COUNT
};
typedef struct e51p_display_config {
    uint32_t struct_size, kind, digits, bar_segments;
    int common_anode, colon;
    double persistence_ms;
    uint32_t i2c_address;
} e51p_display_config;
typedef struct e51p_segment_snapshot {
    uint32_t struct_size, digits, segments; /* segments includes DP, except bars */
    uint32_t instantaneous[8], programmed[8], undefined[8];
    float brightness[8][17]; /* 0..1 actual duty, without visual multiplex gain */
    uint32_t colon;
    float colon_brightness[2];
    uint64_t keys;
} e51p_segment_snapshot;
e51p_display_config e51p_display_config_init(uint32_t kind);
const char* e51p_display_type_id(uint32_t kind);
const char* e51p_display_name(uint32_t kind);
const e51p_endpoint_desc* e51p_display_pins(uint32_t kind, uint32_t* count);
e51p_status e51p_add_display(const e51p_api*, e51p_context*, const char* id,
    const e51p_display_config*, e51p_display** result, e51p_endpoint* pins, uint32_t capacity);
/* Passive snapshots; reading does not clear history or advance emulated time. */
e51p_status e51p_display_snapshot(const e51p_display*, e51p_segment_snapshot*);
/* HT16K33: bits column*13+row (39 keys); TM1637: K1 SG1..8 then K2 SG1..8.
 * Keys can also be wired through the active-low parallel key-input aliases. */
e51p_status e51p_display_set_keys(e51p_display*, uint64_t keys);
#ifdef __cplusplus
}
#endif
#endif
