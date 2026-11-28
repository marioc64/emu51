// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#include "audio_internal.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <new>

namespace {
constexpr auto maximum = std::numeric_limits<uint64_t>::max();
float normalized(double value) noexcept {
    if (std::isnan(value)) return 0;
    return static_cast<float>(std::max(-1.0, std::min(1.0, value)));
}
void fault(e51p_audio_stream& s, e51p_status status) noexcept {
    if (status == E51P_OK || status == E51P_CONTENTION) return;
    e51p_status expected = E51P_OK;
    s.status.compare_exchange_strong(expected, status, std::memory_order_relaxed);
}
bool take(e51p_audio_stream& s, E51pAudioRing& ring, uint64_t epoch, uint64_t wanted,
    float& sample, std::atomic<uint64_t>& skipped, uint32_t& budget) noexcept {
    E51pAudioFrame frame;
    while (budget && ring.peek(frame)) {
        if (s.epoch.load(std::memory_order_acquire) != epoch) return false;
        if (frame.epoch != epoch || frame.index < wanted) {
            ring.pop(); --budget; skipped.fetch_add(1, std::memory_order_relaxed); continue;
        }
        if (frame.index > wanted) return false;
        ring.pop(); --budget; sample = frame.sample;
        return true;
    }
    return false;
}
void rebase(e51p_audio_stream& s) noexcept {
    s.epoch.fetch_add(1, std::memory_order_acq_rel);
    s.phase = s.simulation_index = 0;
    s.capture.discard();
}
void detach(void* user) {
    auto& s = *static_cast<e51p_audio_stream*>(user);
    s.context = nullptr; s.started = false;
    s.paused.store(true, std::memory_order_release);
    s.epoch.fetch_add(1, std::memory_order_acq_rel);
    e51p_audio_release(&s);
}
void reset(void* user, e51p_context* context) {
    auto& s = *static_cast<e51p_audio_stream*>(user);
    s.started = true;
    s.status.store(E51P_OK, std::memory_order_relaxed);
    s.held_output = 0;
    s.scope_next = s.scope_count = 0;
    rebase(s);
    if (s.pins.capture_voltage != E51P_INVALID_ID)
        fault(s, s.api.analog_write(context, s.pins.capture_voltage, s.config.input_offset_volts));
}
void changed(void* user, e51p_context*, e51p_endpoint, double volts, uint64_t) {
    auto& s = *static_cast<e51p_audio_stream*>(user);
    s.held_output = normalized((volts - s.config.output_offset_volts) / s.config.output_scale_volts);
}
void advance(void* user, e51p_context* context, uint64_t from, uint64_t to) {
    auto& s = *static_cast<e51p_audio_stream*>(user);
    if (s.paused.load(std::memory_order_acquire) || s.status.load(std::memory_order_relaxed) != E51P_OK) return;
    const auto delta = to - from;
    const auto whole = delta / s.hz;
    const auto numerator = s.phase + (delta % s.hz) * s.config.sample_rate;
    const auto fraction = numerator / s.hz;
    if (whole > (maximum - fraction) / s.config.sample_rate) { fault(s, E51P_INVALID_ARGUMENT); return; }
    const auto frames = whole * s.config.sample_rate + fraction;
    if (frames > maximum - s.simulation_index) { fault(s, E51P_INVALID_ARGUMENT); return; }
    s.phase = numerator % s.hz;
    if (!frames) return;
    const auto epoch = s.epoch.load(std::memory_order_acquire);
    const float output = s.held_output;
    if (s.config.directions & E51P_AUDIO_PLAYBACK) {
        const auto work = std::min<uint64_t>(frames, s.config.capacity_frames);
        uint64_t queued = 0;
        for (uint64_t i = 0; i < work; ++i)
            if (s.playback.push({epoch, s.simulation_index + i, s.held_output})) ++queued;
        s.playback_queued.fetch_add(queued, std::memory_order_relaxed);
        s.playback_dropped.fetch_add(frames - queued, std::memory_order_relaxed);
    }
    s.simulation_index += frames;
    float input = 0, amplified = 0;
    if (s.config.directions & E51P_AUDIO_CAPTURE) {
        const auto index = s.simulation_index - 1;
        if (index >= s.config.latency_frames) {
            uint32_t budget = s.config.capacity_frames;
            if (take(s, s.capture, epoch, index - s.config.latency_frames, input, s.capture_skipped, budget))
                s.capture_used.fetch_add(1, std::memory_order_relaxed);
            else s.capture_misses.fetch_add(1, std::memory_order_relaxed);
        }
        // Inputs are held at the last sample boundary. If an advance spans
        // several samples, only its final input value is observable by the CPU.
        amplified = normalized(input * s.input_gain);
        const auto volts = s.config.input_offset_volts + amplified * s.config.input_scale_volts;
        fault(s, s.api.analog_write(context, s.pins.capture_voltage, volts));
    }
    // Same simulation-thread ownership as the UI. No callback synchronization,
    // queue consumption or allocation is needed for visualization.
    const auto scopeFrames = std::min<uint64_t>(frames, s.scope.size());
    for (uint64_t i = 0; i < scopeFrames; ++i) {
        s.scope[s.scope_next] = {input, amplified, output};
        s.scope_next = (s.scope_next + 1) % s.scope.size();
    }
    s.scope_count = static_cast<uint32_t>(std::min<uint64_t>(s.scope_count + scopeFrames, s.scope.size()));
}
}

extern "C" e51p_audio_config e51p_audio_config_init(uint32_t directions) {
    return {sizeof(e51p_audio_config), directions, 48000, 4096, 480, 2.5, 2.5, 2.5, 2.5};
}
extern "C" e51p_status e51p_audio_create(const e51p_api* api, e51p_context* context,
    const char* id, const e51p_audio_config* cfg, e51p_audio_stream** result, e51p_audio_endpoints* pins) {
    if (!result) return E51P_INVALID_ARGUMENT;
    *result = nullptr;
    if (!api || api->abi_version != E51P_ABI_VERSION || api->struct_size < sizeof(*api) || !context || !cfg ||
        cfg->struct_size < sizeof(*cfg) || !pins || pins->struct_size < sizeof(*pins) ||
        !cfg->directions || (cfg->directions & ~3u) || !cfg->sample_rate || cfg->sample_rate > 192000 ||
        cfg->capacity_frames < 2 || cfg->capacity_frames > 1048576 ||
        (cfg->capacity_frames & (cfg->capacity_frames - 1)) || cfg->latency_frames >= cfg->capacity_frames ||
        !std::isfinite(cfg->input_offset_volts) || !std::isfinite(cfg->output_offset_volts) ||
        !std::isfinite(cfg->input_scale_volts) || cfg->input_scale_volts <= 0 ||
        !std::isfinite(cfg->output_scale_volts) || cfg->output_scale_volts <= 0 ||
        !std::isfinite(cfg->input_offset_volts + cfg->input_scale_volts) ||
        !std::isfinite(cfg->input_offset_volts - cfg->input_scale_volts)) return E51P_INVALID_ARGUMENT;
    uint64_t time, hz;
    auto status = api->clock(context, &time, &hz);
    if (status != E51P_OK) return status;
    if (hz > 1000000000) return E51P_UNSUPPORTED;
    try {
        auto owned = std::make_unique<e51p_audio_stream>(*api, context, *cfg, hz);
        auto* s = owned.get();
        if (!s->epoch.is_lock_free() || !s->refs.is_lock_free() || !s->paused.is_lock_free() || !s->status.is_lock_free())
            return E51P_UNSUPPORTED;
        e51p_device_desc desc{};
        desc.struct_size = sizeof(desc); desc.instance_id = id; desc.user = s;
        desc.destroy = detach; desc.reset = reset; desc.advance = advance; desc.analog_changed = changed;
        e51p_device device;
        status = api->add_device(context, &desc, &device);
        if (status != E51P_OK) return status;
        e51p_audio_retain(s); // Context and returned caller reference.
        owned.release();
        e51p_endpoint_desc endpoint{sizeof(endpoint), "capture_voltage", E51P_ANALOG, E51P_OUTPUT, 1, 0, 0, 0};
        if (cfg->directions & E51P_AUDIO_CAPTURE)
            status = api->add_endpoint(context, device, &endpoint, &s->pins.capture_voltage);
        if (status == E51P_OK && (cfg->directions & E51P_AUDIO_PLAYBACK)) {
            endpoint.name = "playback_voltage"; endpoint.direction = E51P_INPUT;
            status = api->add_endpoint(context, device, &endpoint, &s->pins.playback_voltage);
        }
        if (status != E51P_OK) { e51p_audio_release(s); return status; }
        *result = s; *pins = s->pins;
        return E51P_OK;
    } catch (const std::bad_alloc&) { return E51P_NO_MEMORY; }
    catch (...) { return E51P_DEVICE_ERROR; }
}

extern "C" void e51p_audio_retain(e51p_audio_stream* s) { if (s) s->refs.fetch_add(1, std::memory_order_relaxed); }
extern "C" void e51p_audio_release(e51p_audio_stream* s) {
    if (s && s->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) delete s;
}
extern "C" e51p_status e51p_audio_set_paused(e51p_audio_stream* s, uint32_t paused) {
    if (!s || paused > 1) return E51P_INVALID_ARGUMENT;
    if (!s->started || !s->context) return E51P_INVALID_STATE;
    if (s->paused.load(std::memory_order_relaxed) == bool(paused)) return E51P_OK;
    s->paused.store(paused != 0, std::memory_order_release);
    rebase(*s);
    if (!paused) s->scope_next = s->scope_count = 0;
    if (s->pins.capture_voltage != E51P_INVALID_ID)
        return s->api.analog_write(s->context, s->pins.capture_voltage, s->config.input_offset_volts);
    return E51P_OK;
}
extern "C" e51p_status e51p_audio_set_input_gain(e51p_audio_stream* s, double gain) {
    if (!s || !std::isfinite(gain) || gain < 0 || gain > 1000) return E51P_INVALID_ARGUMENT;
    if (!s->context) return E51P_INVALID_STATE;
    s->input_gain = gain;
    return E51P_OK;
}
extern "C" e51p_status e51p_audio_set_buffering(e51p_audio_stream* s, uint32_t capacity, uint32_t latency) {
    if (!s || capacity < 2 || capacity > 1048576 || (capacity & (capacity - 1)) || latency >= capacity)
        return E51P_INVALID_ARGUMENT;
    if (!s->context || !s->paused.load(std::memory_order_acquire) || s->host_attached.load(std::memory_order_acquire))
        return E51P_INVALID_STATE;
    try {
        // Complete both allocations before publishing any change.
        std::vector<E51pAudioFrame> capture(capacity), playback(capacity);
        s->capture.replace(capture); s->playback.replace(playback);
        s->config.capacity_frames = capacity; s->config.latency_frames = latency;
        rebase(*s);
        s->scope_next = s->scope_count = 0;
        return E51P_OK;
    } catch (const std::bad_alloc&) { return E51P_NO_MEMORY; }
    catch (...) { return E51P_DEVICE_ERROR; }
}
extern "C" e51p_status e51p_audio_read_scope(const e51p_audio_stream* s, e51p_audio_scope_frame* out,
    uint32_t capacity, uint32_t* count, uint32_t* rate) {
    if (!s || !count || !rate || (capacity && !out)) return E51P_INVALID_ARGUMENT;
    if (!s->context) return E51P_INVALID_STATE;
    *count = std::min(capacity, s->scope_count); *rate = s->config.sample_rate;
    const auto first = (s->scope_next + s->scope.size() - *count) % s->scope.size();
    for (uint32_t i = 0; i < *count; ++i) out[i] = s->scope[(first + i) % s->scope.size()];
    return E51P_OK;
}
extern "C" e51p_status e51p_audio_get_stats(const e51p_audio_stream* s, e51p_audio_stats* out) {
    if (!s || !out || out->struct_size < sizeof(*out)) return E51P_INVALID_ARGUMENT;
    *out = {sizeof(*out), s->status.load(std::memory_order_relaxed),
        s->captured.load(), s->capture_dropped.load(), s->capture_used.load(), s->capture_misses.load(), s->capture_skipped.load(),
        s->playback_queued.load(), s->playback_dropped.load(), s->playback_used.load(), s->playback_misses.load(), s->playback_skipped.load()};
    return E51P_OK;
}
extern "C" uint32_t e51p_audio_capture(e51p_audio_stream* s, const float* input, uint32_t frames) {
    if (!s || !(s->config.directions & E51P_AUDIO_CAPTURE) || s->paused.load(std::memory_order_acquire) ||
        s->status.load(std::memory_order_relaxed) != E51P_OK) return 0;
    const auto epoch = s->epoch.load(std::memory_order_acquire);
    if (s->capture_epoch != epoch) { s->capture_epoch = epoch; s->capture_index = 0; }
    uint32_t accepted = 0;
    for (uint32_t i = 0; i < frames; ++i) {
        if (s->capture_index == maximum) { fault(*s, E51P_INVALID_ARGUMENT); break; }
        const float sample = input && std::isfinite(input[i]) ? normalized(input[i]) : 0;
        if (s->capture.push({epoch, s->capture_index++, sample})) ++accepted;
    }
    s->captured.fetch_add(frames, std::memory_order_relaxed);
    s->capture_dropped.fetch_add(frames - accepted, std::memory_order_relaxed);
    return accepted;
}
extern "C" uint32_t e51p_audio_playback(e51p_audio_stream* s, float* output, uint32_t frames) {
    if (!output) return 0;
    if (!s || !(s->config.directions & E51P_AUDIO_PLAYBACK)) { std::fill_n(output, frames, 0.0f); return 0; }
    uint32_t used = 0, budget = s->config.capacity_frames;
    for (uint32_t i = 0; i < frames; ++i) {
        output[i] = 0;
        if (s->paused.load(std::memory_order_acquire) || s->status.load(std::memory_order_relaxed) != E51P_OK) {
            s->playback.discard(); continue;
        }
        const auto epoch = s->epoch.load(std::memory_order_acquire);
        if (s->playback_epoch != epoch) { s->playback_epoch = epoch; s->playback_index = 0; }
        if (s->playback_index == maximum) { fault(*s, E51P_INVALID_ARGUMENT); continue; }
        const auto index = s->playback_index++;
        if (index < s->config.latency_frames) continue;
        if (take(*s, s->playback, epoch, index - s->config.latency_frames, output[i], s->playback_skipped, budget)) ++used;
        else s->playback_misses.fetch_add(1, std::memory_order_relaxed);
    }
    s->playback_used.fetch_add(used, std::memory_order_relaxed);
    return used;
}
