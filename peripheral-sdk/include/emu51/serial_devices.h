// SPDX-License-Identifier: LGPL-2.1-only
#ifndef EMU51_SERIAL_DEVICES_H
#define EMU51_SERIAL_DEVICES_H
#include "emu51/peripheral.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { E51P_SSD1306, E51P_DS1307, E51P_MCP23017, E51P_W25Q64, E51P_LOGIC_ANALYZER, E51P_SERIAL_DEVICE_COUNT };
enum { E51P_FLASH_SIZE = 8 * 1024 * 1024, E51P_LOGIC_CAPACITY = 4096 };
typedef struct e51p_serial_device e51p_serial_device;
typedef struct e51p_serial_config {
    uint32_t kind, address;
    /* RTC initial date as days since 2000-01-01 and seconds since midnight. */
    uint32_t rtc_day, rtc_second;
} e51p_serial_config;

typedef struct e51p_oled_snapshot {
    /* One bit per pixel, rows top to bottom, leftmost pixel in bit zero. */
    uint8_t pixels[64][16];
    uint8_t contrast;
} e51p_oled_snapshot;
typedef struct e51p_rtc_snapshot {
    uint32_t year, month, day, hour, minute, second;
    int halted;
} e51p_rtc_snapshot;
typedef struct e51p_logic_sample {
    uint64_t tick;
    e51p_digital_state pins;
} e51p_logic_sample;
typedef struct e51p_logic_status {
    uint32_t count;
    uint64_t end_tick, trigger_tick, tick_hz;
    int armed, triggered;
} e51p_logic_status;

enum { E51P_LOGIC_I2C, E51P_LOGIC_SPI };
enum { E51P_LOGIC_BEGIN, E51P_LOGIC_END, E51P_LOGIC_ADDRESS, E51P_LOGIC_BYTE };
typedef struct e51p_logic_event {
    uint64_t tick;
    uint32_t kind, a, b, flags; /* I2C flags: ACK. SPI flags: valid MOSI/MISO bits 0/1. */
} e51p_logic_event;
/* I2C uses D0=SCL, D1=SDA. SPI uses D0=SCK, D1=MOSI, D2=MISO, D3=CS (low). */
uint32_t e51p_logic_decode(const e51p_logic_sample*, uint32_t count, uint32_t protocol,
    e51p_logic_event*, uint32_t capacity);

e51p_serial_config e51p_serial_config_init(uint32_t kind);
const char* e51p_serial_type_id(uint32_t kind);
const e51p_endpoint_desc* e51p_serial_pins(uint32_t kind, uint32_t* count);
e51p_status e51p_add_serial_device(const e51p_api*, e51p_context*, const char* id,
    const e51p_serial_config*, e51p_serial_device**, e51p_endpoint*, uint32_t capacity);
e51p_status e51p_oled_state(const e51p_serial_device*, e51p_oled_snapshot*);
e51p_status e51p_rtc_state(const e51p_serial_device*, e51p_rtc_snapshot*);
e51p_status e51p_flash_import(e51p_serial_device*, const uint8_t*, uint32_t size);
e51p_status e51p_flash_export(const e51p_serial_device*, uint8_t*, uint32_t size);
uint64_t e51p_flash_revision(const e51p_serial_device*);
/* Use channel -1 to capture immediately, or 0..7 to wait for an edge. */
e51p_status e51p_logic_arm(e51p_serial_device*, int channel, int rising);
e51p_status e51p_logic_stop(e51p_serial_device*);
e51p_status e51p_logic_read(const e51p_serial_device*, e51p_logic_status*,
    e51p_logic_sample*, uint32_t capacity);

#ifdef __cplusplus
}
#endif
#endif
