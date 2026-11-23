// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#include "emu51/audio_host.h"
#include "audio_internal.h"
#include "miniaudio.h"
#include <algorithm>
#include <cstdio>
#include <memory>
#include <new>
#include <vector>

struct e51p_audio_host {
    ma_context context{};
    ma_device device{};
    bool initialized = false, opened = false, running = false;
    e51p_audio_stream* stream = nullptr;
    e51p_audio_host_config config{};
    std::vector<ma_device_info> capture, playback;
    char error[256]{};
    ~e51p_audio_host() {
        e51p_audio_host_close(this);
        if (initialized) ma_context_uninit(&context);
    }
};
namespace {
e51p_status failed(e51p_audio_host& h, ma_result result) {
    std::snprintf(h.error, sizeof(h.error), "%s", ma_result_description(result));
    return E51P_DEVICE_ERROR;
}
const std::vector<ma_device_info>* devices(const e51p_audio_host* h, uint32_t direction) {
    if (!h) return nullptr;
    if (direction == E51P_AUDIO_CAPTURE) return &h->capture;
    if (direction == E51P_AUDIO_PLAYBACK) return &h->playback;
    return nullptr;
}
void callback(ma_device* device, void* output, const void* input, ma_uint32 frames) {
    auto& h = *static_cast<e51p_audio_host*>(device->pUserData);
    const auto* in = static_cast<const float*>(input);
    auto* out = static_cast<float*>(output);
    constexpr uint32_t block = 256;
    float mono[block];
    // Bounded stack workspace; no SDK calls, device lifecycle, allocation,
    // wall-clock reads or mutexes are performed by this callback.
    for (uint32_t offset = 0; offset < frames;) {
        const auto count = std::min<uint32_t>(block, frames - offset);
        if (h.stream->config.directions & E51P_AUDIO_CAPTURE) {
            for (uint32_t i = 0; i < count; ++i)
                mono[i] = in ? in[(size_t(offset) + i) * device->capture.channels + h.config.capture_channel] : 0;
            e51p_audio_capture(h.stream, mono, count);
        }
        if (out) {
            e51p_audio_playback(h.stream, mono, count);
            for (uint32_t i = 0; i < count; ++i)
                for (uint32_t channel = 0; channel < device->playback.channels; ++channel)
                    out[(size_t(offset) + i) * device->playback.channels + channel] =
                        h.config.playback_channel == E51P_AUDIO_ALL_CHANNELS || h.config.playback_channel == channel ? mono[i] : 0;
        }
        offset += count;
    }
}
}
extern "C" e51p_audio_host_config e51p_audio_host_config_init(void) {
    return {sizeof(e51p_audio_host_config), E51P_AUDIO_DEFAULT_DEVICE, E51P_AUDIO_DEFAULT_DEVICE, 0, E51P_AUDIO_ALL_CHANNELS};
}
extern "C" e51p_status e51p_audio_host_create(uint32_t backend, e51p_audio_host** result) {
    if (!result) return E51P_INVALID_ARGUMENT;
    *result = nullptr;
    if (backend != E51P_AUDIO_NATIVE && backend != E51P_AUDIO_NULL) return E51P_INVALID_ARGUMENT;
    try {
        auto host = std::make_unique<e51p_audio_host>();
        const ma_backend native[] = {ma_backend_wasapi, ma_backend_dsound, ma_backend_winmm, ma_backend_coreaudio,
            ma_backend_pulseaudio, ma_backend_alsa, ma_backend_jack, ma_backend_sndio, ma_backend_audio4, ma_backend_oss};
        const ma_backend null[] = {ma_backend_null};
        const auto status = ma_context_init(backend == E51P_AUDIO_NULL ? null : native,
            backend == E51P_AUDIO_NULL ? 1 : static_cast<ma_uint32>(sizeof(native) / sizeof(native[0])), nullptr, &host->context);
        if (status != MA_SUCCESS) return E51P_DEVICE_ERROR;
        host->initialized = true;
        ma_device_info* capture = nullptr; ma_device_info* playback = nullptr;
        ma_uint32 captureCount = 0, playbackCount = 0;
        if (ma_context_get_devices(&host->context, &playback, &playbackCount, &capture, &captureCount) != MA_SUCCESS)
            return E51P_DEVICE_ERROR;
        if (captureCount) host->capture.assign(capture, capture + captureCount);
        if (playbackCount) host->playback.assign(playback, playback + playbackCount);
        *result = host.release();
        return E51P_OK;
    } catch (const std::bad_alloc&) { return E51P_NO_MEMORY; }
    catch (...) { return E51P_DEVICE_ERROR; }
}
extern "C" void e51p_audio_host_destroy(e51p_audio_host* host) { delete host; }
extern "C" e51p_status e51p_audio_host_device_count(const e51p_audio_host* h, uint32_t direction, uint32_t* count) {
    const auto* list = devices(h, direction);
    if (!list || !count) return E51P_INVALID_ARGUMENT;
    *count = static_cast<uint32_t>(list->size());
    return E51P_OK;
}
extern "C" e51p_status e51p_audio_host_device_info(const e51p_audio_host* h, uint32_t direction,
    uint32_t index, e51p_audio_device_info* result) {
    const auto* list = devices(h, direction);
    if (!list || !result || result->struct_size < sizeof(*result) || index >= list->size()) return E51P_INVALID_ARGUMENT;
    *result = {sizeof(*result), index, (*list)[index].isDefault, {}};
    std::snprintf(result->name, sizeof(result->name), "%s", (*list)[index].name);
    return E51P_OK;
}
extern "C" e51p_status e51p_audio_host_open(e51p_audio_host* h, e51p_audio_stream* s, const e51p_audio_host_config* config) {
    if (!h || !s || !config || config->struct_size < sizeof(*config)) return E51P_INVALID_ARGUMENT;
    if (h->opened) return E51P_INVALID_STATE;
    const bool capture = (s->config.directions & E51P_AUDIO_CAPTURE) != 0;
    const bool playback = (s->config.directions & E51P_AUDIO_PLAYBACK) != 0;
    if ((capture && config->capture_device != E51P_AUDIO_DEFAULT_DEVICE && config->capture_device >= h->capture.size()) ||
        (playback && config->playback_device != E51P_AUDIO_DEFAULT_DEVICE && config->playback_device >= h->playback.size()))
        return E51P_INVALID_ARGUMENT;
    if (s->host_attached.exchange(true, std::memory_order_acq_rel)) return E51P_CONFLICT;
    auto device = ma_device_config_init(capture && playback ? ma_device_type_duplex : capture ? ma_device_type_capture : ma_device_type_playback);
    device.sampleRate = s->config.sample_rate;
    device.periodSizeInFrames = 256;
    device.capture.format = device.playback.format = ma_format_f32;
    device.capture.channels = device.playback.channels = 0; // Native channels, selected in the callback.
    if (capture && config->capture_device != E51P_AUDIO_DEFAULT_DEVICE)
        device.capture.pDeviceID = &h->capture[config->capture_device].id;
    if (playback && config->playback_device != E51P_AUDIO_DEFAULT_DEVICE)
        device.playback.pDeviceID = &h->playback[config->playback_device].id;
    device.dataCallback = callback;
    device.pUserData = h;
    h->stream = s; h->config = *config;
    const auto result = ma_device_init(&h->context, &device, &h->device);
    if (result != MA_SUCCESS) {
        s->host_attached.store(false, std::memory_order_release); h->stream = nullptr;
        return failed(*h, result);
    }
    if ((capture && config->capture_channel >= h->device.capture.channels) ||
        (playback && config->playback_channel != E51P_AUDIO_ALL_CHANNELS && config->playback_channel >= h->device.playback.channels)) {
        ma_device_uninit(&h->device);
        s->host_attached.store(false, std::memory_order_release); h->stream = nullptr;
        std::snprintf(h->error, sizeof(h->error), "Selected audio channel is unavailable");
        return E51P_INVALID_ARGUMENT;
    }
    e51p_audio_retain(s);
    h->opened = true; h->error[0] = 0;
    return E51P_OK;
}
extern "C" e51p_status e51p_audio_host_start(e51p_audio_host* h) {
    if (!h || !h->opened || h->running) return E51P_INVALID_STATE;
    auto status = e51p_audio_set_paused(h->stream, 1);
    if (status != E51P_OK && status != E51P_CONTENTION) return status;
    // stop() has joined all callbacks. Clear both queues here, including old
    // playback frames that never received a final paused callback to drain them.
    h->stream->capture.discard();
    h->stream->playback.discard();
    // Starting a native device can block. Keep its initial callbacks silent so
    // they cannot consume a new timeline before the simulation can resume.
    const auto result = ma_device_start(&h->device);
    if (result != MA_SUCCESS) return failed(*h, result);
    status = e51p_audio_set_paused(h->stream, 0);
    if (status != E51P_OK && status != E51P_CONTENTION) {
        (void)e51p_audio_set_paused(h->stream, 1);
        (void)ma_device_stop(&h->device);
        std::snprintf(h->error, sizeof(h->error), "Cannot resume audio stream (SDK status %u)", status);
        return status;
    }
    h->running = true;
    h->error[0] = 0;
    return E51P_OK;
}
extern "C" e51p_status e51p_audio_host_stop(e51p_audio_host* h) {
    if (!h || !h->opened) return E51P_INVALID_STATE;
    (void)e51p_audio_set_paused(h->stream, 1);
    if (!h->running) return E51P_OK;
    const auto result = ma_device_stop(&h->device);
    if (result != MA_SUCCESS) return failed(*h, result);
    h->running = false;
    h->error[0] = 0;
    return E51P_OK;
}
extern "C" void e51p_audio_host_close(e51p_audio_host* h) {
    if (!h || !h->opened) return;
    (void)e51p_audio_host_stop(h);
    ma_device_uninit(&h->device);
    h->stream->host_attached.store(false, std::memory_order_release);
    e51p_audio_release(h->stream);
    h->stream = nullptr; h->opened = h->running = false;
}
extern "C" e51p_status e51p_audio_host_error(const e51p_audio_host* h, char* message, uint32_t capacity) {
    if (!h || !message || !capacity) return E51P_INVALID_ARGUMENT;
    std::snprintf(message, capacity, "%s", h->error);
    return E51P_OK;
}
