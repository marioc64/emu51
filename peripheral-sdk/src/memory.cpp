// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#include "emu51/memory.h"
#include <algorithm>
#include <atomic>
#include <limits>
#include <memory>
#include <new>
#include <vector>

struct e51p_memory {
    std::atomic<unsigned> references{1};
    uint32_t kind = E51P_RAM;
    uint64_t revision = 0;
    std::vector<uint8_t> bytes;
    std::vector<uint64_t> pages;
};
namespace {
bool range(const e51p_memory* m, uint64_t offset, uint64_t length) {
    return m && offset <= m->bytes.size() && length <= m->bytes.size() - offset;
}
void changed(e51p_memory* m, uint64_t offset, uint64_t length) {
    ++m->revision;
    for (auto page = offset/256; length && page <= (offset+length-1)/256; ++page) m->pages[static_cast<size_t>(page)] = m->revision;
}
}
extern "C" {
e51p_status e51p_memory_create(const e51p_memory_config* config, e51p_memory** result) {
    if (!result) return E51P_INVALID_ARGUMENT;
    *result = nullptr;
    if (!config || config->struct_size < sizeof(*config) || config->kind > E51P_ROM ||
        !config->capacity || config->capacity > (UINT64_C(1) << 32) ||
        config->capacity > std::numeric_limits<size_t>::max()) return E51P_INVALID_ARGUMENT;
    try {
        auto m = std::make_unique<e51p_memory>();
        m->kind = config->kind;
        m->bytes.assign(static_cast<size_t>(config->capacity), config->fill);
        m->pages.assign(static_cast<size_t>((config->capacity+255)/256),0);
        *result = m.release();
        return E51P_OK;
    } catch (const std::bad_alloc&) { return E51P_NO_MEMORY; }
    catch (...) { return E51P_INVALID_ARGUMENT; }
}
void e51p_memory_retain(e51p_memory* m) { if (m) ++m->references; }
void e51p_memory_release(e51p_memory* m) { if (m && --m->references == 0) delete m; }
uint64_t e51p_memory_capacity(const e51p_memory* m) { return m ? m->bytes.size() : 0; }
uint64_t e51p_memory_revision(const e51p_memory* m) { return m ? m->revision : 0; }
uint64_t e51p_memory_page_revision(const e51p_memory* m, uint64_t offset) { return m && offset < m->bytes.size() ? m->pages[static_cast<size_t>(offset/256)] : 0; }
uint32_t e51p_memory_kind(const e51p_memory* m) { return m ? m->kind : UINT32_MAX; }
uint32_t e51p_memory_address_bits(const e51p_memory* m) {
    if (!m) return 0;
    uint64_t highest = m->bytes.size() - 1;
    uint32_t bits = 1;
    while ((highest >>= 1) != 0) ++bits;
    return bits;
}
e51p_status e51p_memory_read(const e51p_memory* m, uint64_t offset, uint8_t* value) {
    if (!value || !range(m, offset, 1)) return E51P_INVALID_ARGUMENT;
    *value = m->bytes[static_cast<size_t>(offset)];
    return E51P_OK;
}
e51p_status e51p_memory_write(e51p_memory* m, uint64_t offset, uint8_t value) {
    if (!range(m, offset, 1)) return E51P_INVALID_ARGUMENT;
    if (m->kind == E51P_ROM) return E51P_UNSUPPORTED;
    return e51p_memory_program(m, offset, &value, 1);
}
e51p_status e51p_memory_program(e51p_memory* m, uint64_t offset, const uint8_t* data, size_t length) {
    if ((!data && length) || !range(m, offset, length)) return E51P_INVALID_ARGUMENT;
    if (length) {
        auto first = m->bytes.begin() + static_cast<size_t>(offset);
        if (!std::equal(data, data + length, first)) {
            std::copy(data, data + length, first);
            changed(m,offset,length);
        }
    }
    return E51P_OK;
}
e51p_status e51p_memory_fill(e51p_memory* m, uint64_t offset, uint64_t length, uint8_t value) {
    if (!range(m, offset, length)) return E51P_INVALID_ARGUMENT;
    auto first = m->bytes.begin() + static_cast<size_t>(offset);
    auto last = first + static_cast<size_t>(length);
    if (std::any_of(first, last, [value](uint8_t b) { return b != value; })) {
        std::fill(first, last, value);
        changed(m,offset,length);
    }
    return E51P_OK;
}
e51p_status e51p_memory_copy(const e51p_memory* m, uint64_t offset, uint8_t* data, size_t length) {
    if ((!data && length) || !range(m, offset, length)) return E51P_INVALID_ARGUMENT;
    if (length) std::copy_n(m->bytes.begin() + static_cast<size_t>(offset), length, data);
    return E51P_OK;
}
}
