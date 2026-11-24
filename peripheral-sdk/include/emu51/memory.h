// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#ifndef EMU51_PERIPHERAL_MEMORY_H
#define EMU51_PERIPHERAL_MEMORY_H
#include "peripheral.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Physical storage is independent of a context and can survive circuit rebuilds.
 * The host serializes access on its simulation thread (or execution lock).
 * No operation changes selectors, signals or simulated time. */
typedef struct e51p_memory e51p_memory;
enum { E51P_RAM = 0, E51P_ROM = 1 };
typedef struct e51p_memory_config {
    uint32_t struct_size;
    uint32_t kind;
    uint64_t capacity;
    uint8_t fill;
} e51p_memory_config;

e51p_status e51p_memory_create(const e51p_memory_config*, e51p_memory**);
void e51p_memory_retain(e51p_memory*);
void e51p_memory_release(e51p_memory*);
uint64_t e51p_memory_capacity(const e51p_memory*);
uint64_t e51p_memory_revision(const e51p_memory*);
uint64_t e51p_memory_page_revision(const e51p_memory*, uint64_t offset);
uint32_t e51p_memory_kind(const e51p_memory*);
uint32_t e51p_memory_address_bits(const e51p_memory*);
e51p_status e51p_memory_read(const e51p_memory*, uint64_t offset, uint8_t*);
/* Bus writes cannot modify ROM; returns UNSUPPORTED for ROM. */
e51p_status e51p_memory_write(e51p_memory*, uint64_t offset, uint8_t);
/* Programming and fill deliberately permit ROM. All ranges are checked before
 * changing a byte. Empty ranges at capacity are valid; nonempty null buffers
 * are invalid. Copy returns a consistent snapshot when externally serialized. */
e51p_status e51p_memory_program(e51p_memory*, uint64_t offset, const uint8_t*, size_t length);
e51p_status e51p_memory_fill(e51p_memory*, uint64_t offset, uint64_t length, uint8_t);
e51p_status e51p_memory_copy(const e51p_memory*, uint64_t offset, uint8_t*, size_t length);

#ifdef __cplusplus
}
#endif
#endif
