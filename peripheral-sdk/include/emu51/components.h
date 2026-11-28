// SPDX-License-Identifier: LGPL-2.1-only
#ifndef EMU51_COMPONENTS_H
#define EMU51_COMPONENTS_H
#include "peripheral.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct e51p_component e51p_component;
enum {
    E51P_COMPONENT_LED, E51P_COMPONENT_SWITCH, E51P_COMPONENT_BUTTON,
    E51P_COMPONENT_LCD, E51P_COMPONENT_KEYPAD, E51P_COMPONENT_595,
    E51P_COMPONENT_165, E51P_COMPONENT_EEPROM, E51P_COMPONENT_AY,
    E51P_COMPONENT_BUS_LATCH, E51P_COMPONENT_BUS_BUFFER, E51P_COMPONENT_MATRIX,
    E51P_COMPONENT_24LC256, E51P_COMPONENT_ULN2003, E51P_COMPONENT_STEPPER, E51P_COMPONENT_ADC0809,
    E51P_COMPONENT_COUNT
};
typedef struct e51p_component_config {
    uint32_t struct_size, kind;
    int active_low, initially_pressed;
    uint64_t bounce_ticks;
    uint32_t columns, rows, i2c_address;
    double clock_hz;
} e51p_component_config;
typedef struct e51p_lcd_snapshot {
    uint32_t struct_size, columns, rows;
    uint8_t cells[80], cgram[64];
    int display_on, cursor_on, blink_on, cursor_cell;
} e51p_lcd_snapshot;

e51p_component_config e51p_component_config_init(uint32_t kind);
/* Immutable pin metadata; order matches add_component result handles. */
const e51p_endpoint_desc* e51p_component_pins(uint32_t kind, uint32_t* count);
/* Context owns the component. Discard the context on partial setup failure.
 * No host I/O. All operations below belong to the simulation/control thread. */
e51p_status e51p_add_component(const e51p_api*, e51p_context*, const char* id,
    const e51p_component_config*, e51p_component** result,
    e51p_endpoint* pins, uint32_t capacity);
e51p_status e51p_component_set_pressed(e51p_component*, int pressed);
/* Legacy keypad: bit = row*4+column. Configurable matrix: row*columns+column. */
e51p_status e51p_component_set_keys(e51p_component*, uint32_t mask);
/* Bus latch only, before start. Every committed write retriggers PULSE/PULSE_n
 * for this many context ticks. Zero disables the pulse; Q remains latched. */
e51p_status e51p_component_set_latch_pulse_ticks(e51p_component*, uint64_t ticks);
/* Matrix only, before start. Append 0..8 independent keys after the scanned
 * contacts (total <=32). They drive matching bits of the direct output low. */
e51p_status e51p_component_set_matrix_direct_keys(e51p_component*, uint32_t count);
e51p_status e51p_component_get_led(const e51p_component*, int* on, int* defined);
e51p_status e51p_component_get_lcd(const e51p_component*, e51p_lcd_snapshot*);
typedef struct e51p_stepper_snapshot {
    uint32_t coils, position_half_steps, invalid_transitions;
    int direction; /* -1 reverse, +1 forward, 0 no direction */
    int valid;
} e51p_stepper_snapshot;
enum { E51P_STEPPER_HALF_STEPS_PER_TURN = 400 };
e51p_status e51p_component_get_stepper(const e51p_component*, e51p_stepper_snapshot*);
/* ADC0809 only, before start. References are fixed at 0 V and full_scale_volts. */
e51p_status e51p_component_set_adc0809(e51p_component*, uint64_t conversion_ticks, double full_scale_volts);
/* Image length is 256 bytes for 24C02 or 32768 bytes for 24LC256.
 * Import before the first time advance. Exports include committed bytes.
 * EEPROM contents survive context reset. File persistence belongs to the host. */
e51p_status e51p_component_eeprom_import(e51p_component*, const uint8_t*, uint32_t size);
e51p_status e51p_component_eeprom_export(const e51p_component*, uint8_t*, uint32_t size);
uint64_t e51p_component_eeprom_revision(const e51p_component*);
#ifdef __cplusplus
}
#endif
#endif
