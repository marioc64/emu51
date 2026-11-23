// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#include "emu51/audio_host.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace {
void check(bool value, const char* text) { if (!value) { std::fprintf(stderr, "FAIL: %s\n", text); std::exit(1); } }
void ok(e51p_status status) { check(status == E51P_OK, "native host operation succeeds"); }
}
int main() {
    const e51p_api* api = nullptr; ok(e51p_get_api(E51P_ABI_VERSION, &api));
    e51p_context* context = nullptr; ok(api->create(48000, &context));
    auto config = e51p_audio_config_init(3); config.latency_frames = 1024;
    e51p_audio_endpoints pins{}; pins.struct_size = sizeof(pins);
    e51p_audio_stream* stream = nullptr;
    ok(e51p_audio_create(api, context, "audio", &config, &stream, &pins)); ok(api->start(context));
    e51p_audio_host* host = nullptr; e51p_audio_host* second = nullptr;
    ok(e51p_audio_host_create(E51P_AUDIO_NULL, &host));
    ok(e51p_audio_host_create(E51P_AUDIO_NULL, &second));
    uint32_t count = 0; ok(e51p_audio_host_device_count(host, E51P_AUDIO_CAPTURE, &count));
    check(count > 0, "null backend enumerates a capture device");
    e51p_audio_device_info info{}; info.struct_size = sizeof(info);
    ok(e51p_audio_host_device_info(host, E51P_AUDIO_CAPTURE, 0, &info));
    check(info.name[0] != 0 && info.index == 0, "enumerated device has name and stable index");
    auto native = e51p_audio_host_config_init(); native.capture_channel = 1000;
    check(e51p_audio_host_open(host, stream, &native) == E51P_INVALID_ARGUMENT, "invalid channel rejected after native negotiation");
    char error[256]{}; ok(e51p_audio_host_error(host, error, sizeof(error)));
    check(error[0] != 0, "native errors have a readable diagnostic");
    native = e51p_audio_host_config_init(); ok(e51p_audio_host_open(host, stream, &native));
    check(e51p_audio_host_open(second, stream, &native) == E51P_CONFLICT, "one producer/consumer host per stream");
    ok(e51p_audio_set_paused(stream, 1));
    check(e51p_audio_set_buffering(stream, 8192, 512) == E51P_INVALID_STATE,
        "Attached native host prevents unsafe queue replacement even while paused");
    ok(e51p_audio_host_start(host));
    uint64_t now = 0;
    for (unsigned i = 0; i < 150; ++i) {
        now += 48; ok(api->advance_to(context, now));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    e51p_audio_stats stats{}; stats.struct_size = sizeof(stats); ok(e51p_audio_get_stats(stream, &stats));
    check(stats.captured_frames > 0 && stats.playback_used > 0 && stats.status == E51P_OK,
        "null device executes real threaded capture/playback callbacks");
    ok(e51p_audio_host_stop(host));
    for (unsigned restart = 0; restart < 5; ++restart) {
        ok(e51p_audio_get_stats(stream, &stats));
        const auto before = stats;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        ok(e51p_audio_get_stats(stream, &stats));
        check(stats.captured_frames == before.captured_frames && stats.playback_used == before.playback_used,
            "stopped host has no capture/playback callbacks after stop returns");
        if (restart == 2) { ok(api->reset(context)); now = 0; }
        ok(e51p_audio_host_start(host));
        for (unsigned i = 0; i < 80; ++i) {
            now += 48; ok(api->advance_to(context, now));
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ok(e51p_audio_host_stop(host));
        ok(e51p_audio_get_stats(stream, &stats));
        check(stats.captured_frames > before.captured_frames && stats.capture_used > before.capture_used &&
            stats.playback_used > before.playback_used && stats.status == E51P_OK,
            "each restart resumes actual capture use and playback, with and without CPU/context reset");
        ok(e51p_audio_host_error(host, error, sizeof(error)));
        check(!error[0], "successful restart/stop clears any earlier device diagnostic");
    }
    ok(e51p_audio_host_start(host));
    // Context teardown while the host retains the stream must silence its
    // callbacks, without leaving a dangling context or freeing queued storage.
    api->destroy(context);
    ok(e51p_audio_host_stop(host));
    e51p_audio_release(stream);
    e51p_audio_host_destroy(host); e51p_audio_host_destroy(second);
    std::puts("Null-backend enumeration, channels, callbacks, restart and retained lifetime checks passed.");
}
