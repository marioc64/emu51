// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#include "emu51/audio.h"
#include "emu51/devices.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <thread>

namespace {
void check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void ok(e51p_status status) { check(status == E51P_OK, "audio operation succeeds"); }
struct Graph {
    const e51p_api* api = nullptr;
    e51p_context* c = nullptr;
    e51p_audio_stream* audio = nullptr;
    e51p_audio_endpoints pins{};
    e51p_endpoint output;
    uint64_t now = 0;
    explicit Graph(uint64_t hz = 12, uint32_t rate = 4, uint32_t capacity = 8, uint32_t latency = 0, bool wireOutput = true) {
        ok(e51p_get_api(E51P_ABI_VERSION, &api)); ok(api->create(hz, &c));
        auto config = e51p_audio_config_init(3);
        config.sample_rate = rate; config.capacity_frames = capacity; config.latency_frames = latency;
        pins.struct_size = sizeof(pins);
        ok(e51p_audio_create(api, c, "audio", &config, &audio, &pins));
        e51p_device_desc desc{}; desc.struct_size = sizeof(desc); desc.instance_id = "host";
        e51p_device d; ok(api->add_device(c, &desc, &d));
        e51p_endpoint_desc endpoint{sizeof(endpoint), "voltage", E51P_ANALOG, E51P_OUTPUT, 1, 0, 0, 0};
        ok(api->add_endpoint(c, d, &endpoint, &output));
        if (wireOutput) ok(api->connect_analog(c, output, pins.playback_voltage));
    }
    ~Graph() { if (c) api->destroy(c); e51p_audio_release(audio); }
    void start() { ok(api->start(c)); }
    void advance(uint64_t ticks) { now += ticks; ok(api->advance_to(c, now)); }
    double input() { double volts; ok(api->analog_read(c, pins.capture_voltage, &volts)); return volts; }
    e51p_audio_stats stats() { e51p_audio_stats stats{}; stats.struct_size = sizeof(stats); ok(e51p_audio_get_stats(audio, &stats)); return stats; }
};
void scaling_and_fractional_time() {
    Graph g; g.start();
    const float input[] = {-1, -0.5f, 0, 0.5f, 1, std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(), 10};
    check(e51p_audio_capture(g.audio, input, 8) == 8, "capture queue accepts normalized frames");
    const double expected[] = {0, 1.25, 2.5, 3.75, 5, 2.5, 2.5, 5};
    ok(g.api->analog_write(g.c, g.output, 3.75));
    for (double value : expected) {
        g.advance(2);
        const auto before = g.input();
        g.advance(1);
        check(g.input() == value, "captured samples map to expected analog voltage");
        (void)before;
    }
    float output[8]{};
    check(e51p_audio_playback(g.audio, output, 8) == 8, "playback queue provides all generated samples");
    for (auto value : output) check(value == 0.5f, "voltage maps to normalized playback sample");
    check(g.stats().capture_used == 8 && g.stats().playback_used == 8, "audio counters report samples actually used");
    Graph fractional(11, 3, 8);
    fractional.start(); ok(fractional.api->analog_write(fractional.c, fractional.output, 5));
    for (unsigned i = 0; i < 11; ++i) fractional.advance(1);
    check(fractional.stats().playback_queued == 3, "rational sample clock retains fractional ticks");
    float clipped[3]; check(e51p_audio_playback(fractional.audio, clipped, 3) == 3, "fractional clock output count");
    for (auto value : clipped) check(value == 1, "full scale output maps to one");
    ok(fractional.api->analog_write(fractional.c, fractional.output, -100)); fractional.advance(4);
    e51p_audio_playback(fractional.audio, clipped, 1);
    check(clipped[0] == -1, "out-of-range voltage clips without integer overflow");
}
void live_gain_and_scope() {
    Graph g; g.start();
    const float input[] = {0.03125f, -0.03125f, 0.25f, -0.25f, 0, 0.03125f};
    check(e51p_audio_capture(g.audio, input, 6) == 6, "gain fixture queues quiet microphone samples");
    g.advance(3); check(g.input() == 2.578125, "unity gain preserves quiet input");
    ok(e51p_audio_set_input_gain(g.audio, 8));
    g.advance(3); check(g.input() == 1.875, "live gain affects already queued input without rebasing time");
    g.advance(3); check(g.input() == 5, "positive amplification clips to ADC high rail");
    g.advance(3); check(g.input() == 0, "negative amplification clips to ADC low rail");
    g.advance(3); check(g.input() == 2.5, "amplification leaves silence at midpoint");
    check(e51p_audio_set_input_gain(g.audio, -1) == E51P_INVALID_ARGUMENT &&
        e51p_audio_set_input_gain(g.audio, std::numeric_limits<double>::quiet_NaN()) == E51P_INVALID_ARGUMENT &&
        e51p_audio_set_input_gain(g.audio, std::numeric_limits<double>::infinity()) == E51P_INVALID_ARGUMENT &&
        e51p_audio_set_input_gain(g.audio, 1001) == E51P_INVALID_ARGUMENT, "invalid gains are rejected");
    g.advance(3); check(g.input() == 3.125, "invalid gain changes preserve previous setting");
    e51p_audio_scope_frame scope[4096]{}; uint32_t count = 0, rate = 0;
    ok(e51p_audio_read_scope(g.audio, scope, 4096, &count, &rate));
    check(count == 6 && rate == 4 && scope[0].input == input[0] && scope[0].adc == input[0] &&
        scope[1].adc == -0.25f && scope[2].adc == 1 && scope[3].adc == -1 && scope[4].adc == 0,
        "scope captures raw input and amplified/clipped ADC signal at sample boundaries");
    ok(e51p_audio_set_paused(g.audio, 1)); g.advance(120);
    ok(e51p_audio_read_scope(g.audio, scope, 4096, &count, &rate));
    check(count == 6, "paused waveform freezes");
    ok(e51p_audio_set_paused(g.audio, 0));
    ok(e51p_audio_read_scope(g.audio, scope, 4096, &count, &rate));
    check(count == 0, "resume clears old waveform epoch");
    ok(g.api->reset(g.c)); g.now = 0;
    check(e51p_audio_capture(g.audio, input, 1) == 1, "capture after reset");
    g.advance(3); check(g.input() == 3.125, "gain persists across pause and reset");
    ok(e51p_audio_set_input_gain(g.audio, 0));
    check(e51p_audio_capture(g.audio, input, 1) == 1, "capture with zero gain");
    g.advance(3); check(g.input() == 2.5, "zero gain mutes input");
    ok(g.api->analog_write(g.c, g.output, 3.75));
    for (unsigned i = 0; i < 4101; ++i) {
        const float value = (i % 2) ? -0.25f : 0.25f;
        e51p_audio_capture(g.audio, &value, 1); g.advance(3);
    }
    const auto used = g.stats().capture_used;
    ok(e51p_audio_read_scope(g.audio, scope, 3, &count, &rate));
    check(count == 3 && scope[0].input == 0.25f && scope[1].input == -0.25f && scope[2].input == 0.25f &&
        scope[2].output == 0.5f && g.stats().capture_used == used,
        "wrapped scope returns newest samples in order without consuming audio");
    ok(e51p_audio_read_scope(g.audio, scope, 4096, &count, &rate));
    check(count == 4096, "scope history has bounded capacity");
    ok(g.api->reset(g.c));
    ok(e51p_audio_read_scope(g.audio, scope, 4096, &count, &rate));
    check(count == 0 && e51p_audio_read_scope(g.audio, nullptr, 1, &count, &rate) == E51P_INVALID_ARGUMENT,
        "scope handles empty history and validates output capacity");
}
void configurable_buffering() {
    Graph g; g.start();
    const float first = 0.25f;
    e51p_audio_capture(g.audio, &first, 1); g.advance(3);
    const auto before = g.stats();
    ok(e51p_audio_set_input_gain(g.audio, 8));
    ok(g.api->analog_write(g.c, g.output, 4.375));
    check(e51p_audio_set_buffering(g.audio, 32, 8) == E51P_INVALID_STATE, "Running stream cannot resize queues");
    ok(e51p_audio_set_paused(g.audio, 1));
    check(e51p_audio_set_buffering(g.audio, 3, 0) == E51P_INVALID_ARGUMENT &&
        e51p_audio_set_buffering(g.audio, 32, 32) == E51P_INVALID_ARGUMENT,
        "Invalid buffer capacity/latency is rejected");
    ok(e51p_audio_set_buffering(g.audio, 32, 8));
    check(g.stats().capture_used == before.capture_used && g.stats().captured_frames == before.captured_frames,
        "Resizing retains cumulative counters");
    uint64_t time = 0, hz = 0; ok(g.api->clock(g.c, &time, &hz));
    check(time == 3 && hz == 12, "Buffer change preserves context time");
    ok(e51p_audio_set_paused(g.audio, 0));
    const float quiet = 0.03125f;
    check(e51p_audio_capture(g.audio, &quiet, 1) == 1, "Fresh queue accepts input after resizing");
    g.advance(24); check(g.input() == 2.5, "New capture latency supplies eight startup frames of silence");
    g.advance(3); check(g.input() == 3.125, "Resized capture delivers the correct sample with gain preserved");
    float output[9]{};
    check(e51p_audio_playback(g.audio, output, 9) == 1 && output[8] == 0.75f,
        "Resized playback uses new latency and retains held output voltage");
    for (unsigned i = 0; i < 8; ++i) check(output[i] == 0, "New playback latency contains silence");
    ok(e51p_audio_set_paused(g.audio, 1));
    ok(e51p_audio_set_buffering(g.audio, 2, 1));
    ok(e51p_audio_set_paused(g.audio, 0));
    const float frames[3] = {0,0,0};
    check(e51p_audio_capture(g.audio, frames, 3) == 2, "Shrinking updates actual queue capacity");
}
void gaps_latency_and_pause() {
    Graph g(12, 4, 4); g.start();
    const float input[] = {0.25f, 0.5f, 0.75f, 1, -1, -0.75f};
    check(e51p_audio_capture(g.audio, input, 6) == 4 && g.stats().capture_dropped == 2, "full capture queue drops newest samples");
    for (unsigned i = 0; i < 4; ++i) g.advance(3);
    check(g.input() == 5, "queued capture timeline preserved");
    check(e51p_audio_capture(g.audio, input, 1) == 1, "capture resumes after capacity is available");
    g.advance(3); check(g.input() == 2.5, "missing capture index becomes silence, not a shifted future sample");
    g.advance(3); check(g.input() == 2.5, "second missing capture index remains silent");
    g.advance(3); check(g.input() == 3.125, "later capture retains its original sample index");
    check(g.stats().playback_dropped == 3, "playback queue overflow is counted");
    float output[7];
    check(e51p_audio_playback(g.audio, output, 4) == 4, "playback consumes the original queued interval");
    ok(g.api->analog_write(g.c, g.output, 3.125)); g.advance(3); // index 7, after the missing indices 4..6
    check(e51p_audio_playback(g.audio, output, 4) == 1 && output[3] == 0.25f,
        "future playback sample keeps its position after missing indices");
    for (unsigned i = 0; i < 3; ++i) check(output[i] == 0, "playback underrun fills zero");
    ok(g.api->analog_write(g.c, g.output, 4.375)); g.advance(3); // stale 0.75 sample
    ok(e51p_audio_set_paused(g.audio, 1));
    check(g.input() == 2.5 && e51p_audio_capture(g.audio, input, 1) == 0, "pause silences capture and rejects new data");
    check(e51p_audio_playback(g.audio, output, 1) == 0 && output[0] == 0, "paused output is silent and drains old frames");
    g.advance(100);
    ok(g.api->analog_write(g.c, g.output, 3.75));
    ok(e51p_audio_set_paused(g.audio, 0));
    const float resumed = -0.25f;
    check(e51p_audio_capture(g.audio, &resumed, 1) == 1, "new capture epoch starts at index zero");
    g.advance(3);
    check(g.input() == 1.875 && e51p_audio_playback(g.audio, output, 1) == 1 && output[0] == 0.5f,
        "resume rebases both timelines without replaying paused data");
    ok(g.api->reset(g.c)); g.now = 0;
    g.advance(3);
    check(g.input() == 2.5 && e51p_audio_playback(g.audio, output, 1) == 1 && output[0] == 0,
        "context reset invalidates old audio and starts a silent epoch");
    Graph latency(12, 4, 8, 2); latency.start();
    check(e51p_audio_capture(latency.audio, input, 1) == 1, "capture latency fixture");
    ok(latency.api->analog_write(latency.c, latency.output, 3.75)); latency.advance(9);
    check(latency.input() == 3.125, "capture waits for configured latency before consuming frame zero");
    check(e51p_audio_playback(latency.audio, output, 3) == 1 && output[0] == 0 && output[1] == 0 && output[2] == 0.5f,
        "playback startup latency is explicit silence");
}
void adc_dac_bridge() {
    Graph g(12, 4, 8, 0, false);
    e51p_adc0804_endpoints adc{}; adc.struct_size = sizeof(adc);
    ok(e51p_add_adc0804(g.api, g.c, "adc", 1, 5, &adc));
    e51p_endpoint dacData, dacVoltage;
    ok(e51p_add_parallel_dac(g.api, g.c, "dac", 5, &dacData, &dacVoltage));
    ok(g.api->connect_analog(g.c, dacVoltage, g.pins.playback_voltage));
    e51p_device_desc desc{}; desc.struct_size = sizeof(desc); desc.instance_id = "control";
    e51p_device d; ok(g.api->add_device(g.c, &desc, &d));
    e51p_endpoint controls, data;
    e51p_endpoint_desc endpoint{sizeof(endpoint), "controls", E51P_DIGITAL, E51P_OUTPUT, 3, 0, 0, 0};
    ok(g.api->add_endpoint(g.c, d, &endpoint, &controls));
    endpoint.name = "data"; endpoint.width = 8;
    ok(g.api->add_endpoint(g.c, d, &endpoint, &data));
    ok(g.api->connect_digital(g.c, controls, 0, adc.cs_n, 0, 1));
    ok(g.api->connect_digital(g.c, controls, 1, adc.rd_n, 0, 1));
    ok(g.api->connect_digital(g.c, controls, 2, adc.wr_n, 0, 1));
    ok(g.api->connect_digital(g.c, data, 0, dacData, 0, 8));
    ok(g.api->connect_analog(g.c, g.pins.capture_voltage, adc.vin_positive));
    g.start();
    ok(e51p_audio_set_input_gain(g.audio, 8));
    const float sample = 0.03125f; e51p_audio_capture(g.audio, &sample, 1); g.advance(3);
    ok(g.api->drive(g.c, controls, 2, 7, 0)); ok(g.api->drive(g.c, controls, 6, 7, 0));
    g.advance(1); ok(g.api->drive(g.c, controls, 4, 7, 0));
    e51p_digital_state state{}; ok(g.api->sample(g.c, adc.data, &state));
    check(state.value == 160 && !state.floating, "captured normalized audio drives an actual ADC conversion");
    ok(g.api->drive(g.c, data, state.value, 255, 0));
    double volts; ok(g.api->analog_read(g.c, dacVoltage, &volts));
    check(volts == 3.125, "DAC emits the expected captured voltage");
    g.advance(2);
    float output[2]; check(e51p_audio_playback(g.audio, output, 2) == 2 && output[1] == 0.25f,
        "ADC code through parallel DAC returns the normalized sample to playback");
}
void bounds_and_ownership() {
    Graph g; g.start();
    g.advance(UINT64_C(1) << 40);
    check(g.stats().playback_queued == 8 && g.stats().playback_dropped > 1000000,
        "huge fast-forward intervals do bounded work and count dropped output");
    g.api->destroy(g.c); g.c = nullptr;
    float output = 1;
    check(e51p_audio_playback(g.audio, &output, 1) == 0 && output == 0,
        "retained stream is safe and silent after context destruction");
    check(e51p_audio_set_paused(g.audio, 0) == E51P_INVALID_STATE, "detached stream does not access a destroyed context");
    Graph overflow(1, 192000, 8); overflow.start(); overflow.advance(std::numeric_limits<uint64_t>::max());
    check(overflow.stats().status == E51P_INVALID_ARGUMENT, "unrepresentable frame clock fails without wrapping");
    ok(overflow.api->reset(overflow.c)); overflow.now = 0;
    check(overflow.stats().status == E51P_OK, "context reset clears bridge fault");
    e51p_audio_stream* invalid = reinterpret_cast<e51p_audio_stream*>(1);
    auto cfg = e51p_audio_config_init(3); cfg.capacity_frames = 3;
    check(e51p_audio_create(overflow.api, overflow.c, "bad", &cfg, &invalid, &overflow.pins) == E51P_INVALID_ARGUMENT && !invalid,
        "non-power-of-two ring capacity is rejected");
    cfg = e51p_audio_config_init(3); cfg.input_offset_volts = cfg.input_scale_volts = std::numeric_limits<double>::max();
    check(e51p_audio_create(overflow.api, overflow.c, "bad", &cfg, &invalid, &overflow.pins) == E51P_INVALID_ARGUMENT,
        "voltage mapping cannot overflow finite analog signals");
}
void callback_concurrency() {
    Graph g(48000, 48000, 256, 32); g.start();
    std::atomic<bool> done{false}, ready{false}, bad{false};
    std::thread callback([&] {
        float input[32], output[32];
        for (auto& value : input) value = 0.25f;
        while (!done.load(std::memory_order_acquire)) {
            e51p_audio_capture(g.audio, input, 32);
            e51p_audio_playback(g.audio, output, 32);
            for (auto value : output) if (!std::isfinite(value) || value < -1 || value > 1) bad.store(true);
            ready.store(true, std::memory_order_release);
        }
    });
    while (!ready.load(std::memory_order_acquire)) std::this_thread::yield();
    for (unsigned i = 0; i < 5000; ++i) {
        ok(g.api->analog_write(g.c, g.output, double(i % 256) * 5 / 256));
        g.advance(1);
        if (i % 101 == 0) { ok(e51p_audio_set_paused(g.audio, 1)); ok(e51p_audio_set_paused(g.audio, 0)); }
        if (i % 503 == 0) { ok(g.api->reset(g.c)); g.now = 0; }
    }
    done.store(true, std::memory_order_release); callback.join();
    check(!bad.load() && g.stats().captured_frames > 0 && g.stats().status == E51P_OK,
        "callback producer/consumer remain valid during simulation pause and reset");
}
}
int main() {
    scaling_and_fractional_time(); live_gain_and_scope(); configurable_buffering(); gaps_latency_and_pause(); adc_dac_bridge(); bounds_and_ownership(); callback_concurrency();
    std::puts("Audio scaling, clocks, gaps, epochs, ADC/DAC wiring, lifetime and concurrent callback checks passed.");
}
