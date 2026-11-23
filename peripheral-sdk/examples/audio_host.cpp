// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#include "emu51/audio_host.h"
#include "emu51/devices.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
void usage() {
    std::puts("Usage: emu51_peripheral_audio_demo --list [--null]\n"
        "       emu51_peripheral_audio_demo [--capture index|default] [--playback index|default]\n"
        "          [--capture-channel index] [--playback-channel index|all] [--seconds 5] [--null]");
}
void require(e51p_status status) {
    if (status != E51P_OK) throw std::runtime_error("Audio/SDK operation failed: " + std::to_string(status));
}
uint32_t index(const std::string& text, const char* special) {
    if (special && text == special) return UINT32_MAX;
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) throw std::invalid_argument("Invalid device/channel index");
    const auto value = std::stoull(text);
    if (value >= UINT32_MAX) throw std::invalid_argument("Device/channel index out of range");
    return static_cast<uint32_t>(value);
}
}
int main(int argc, char** argv) {
    try {
        bool list = false, null = false;
        uint32_t directions = 0;
        auto selection = e51p_audio_host_config_init();
        double seconds = 5;
        for (int arg = 1; arg < argc; ++arg) {
            const std::string option = argv[arg];
            if (option == "--help") { usage(); return 0; }
            if (option == "--list") { list = true; continue; }
            if (option == "--null") { null = true; continue; }
            if (arg + 1 == argc) throw std::invalid_argument("Option requires a value: " + option);
            const std::string value = argv[++arg];
            if (option == "--capture") { directions |= E51P_AUDIO_CAPTURE; selection.capture_device = index(value, "default"); }
            else if (option == "--playback") { directions |= E51P_AUDIO_PLAYBACK; selection.playback_device = index(value, "default"); }
            else if (option == "--capture-channel") selection.capture_channel = index(value, nullptr);
            else if (option == "--playback-channel") selection.playback_channel = index(value, "all");
            else if (option == "--seconds") {
                size_t consumed = 0; seconds = std::stod(value, &consumed);
                if (consumed != value.size() || !std::isfinite(seconds) || seconds < 0.05 || seconds > 3600)
                    throw std::invalid_argument("Seconds must be between 0.05 and 3600");
            } else throw std::invalid_argument("Unknown option: " + option);
        }
        if (!list && !directions) {
            usage();
            return argc == 1 ? 0 : 1;
        }
        const e51p_api* api = nullptr; require(e51p_get_api(E51P_ABI_VERSION, &api));
        std::unique_ptr<e51p_context, decltype(api->destroy)> context(nullptr, api->destroy);
        std::unique_ptr<e51p_audio_stream, decltype(&e51p_audio_release)> stream(nullptr, e51p_audio_release);
        std::unique_ptr<e51p_audio_host, decltype(&e51p_audio_host_destroy)> host(nullptr, e51p_audio_host_destroy);
        e51p_audio_host* native = nullptr;
        require(e51p_audio_host_create(null ? E51P_AUDIO_NULL : E51P_AUDIO_NATIVE, &native)); host.reset(native);
        if (list) {
            for (uint32_t direction : {uint32_t(E51P_AUDIO_CAPTURE), uint32_t(E51P_AUDIO_PLAYBACK)}) {
                uint32_t count = 0; require(e51p_audio_host_device_count(host.get(), direction, &count));
                for (uint32_t i = 0; i < count; ++i) {
                    e51p_audio_device_info info{}; info.struct_size = sizeof(info);
                    require(e51p_audio_host_device_info(host.get(), direction, i, &info));
                    std::printf("%s %u: %s%s\n", direction == E51P_AUDIO_CAPTURE ? "capture" : "playback",
                        i, info.name, info.is_default ? " (default)" : "");
                }
            }
            return 0;
        }
        e51p_context* raw = nullptr; require(api->create(12000000, &raw)); context.reset(raw);
        auto config = e51p_audio_config_init(directions);
        config.latency_frames = 1024;
        config.output_scale_volts = 25; // Demo playback gain: +/-2.5 V -> +/-0.1.
        e51p_audio_endpoints audio{}; audio.struct_size = sizeof(audio);
        e51p_audio_stream* pipe = nullptr;
        require(e51p_audio_create(api, context.get(), "audio", &config, &pipe, &audio)); stream.reset(pipe);
        e51p_adc0804_endpoints adc{}; adc.struct_size = sizeof(adc);
        require(e51p_add_adc0804(api, context.get(), "adc", 1200, 5, &adc));
        e51p_endpoint dacData, dacVoltage;
        require(e51p_add_parallel_dac(api, context.get(), "covox", 5, &dacData, &dacVoltage));
        e51p_device_desc d{}; d.struct_size = sizeof(d); d.instance_id = "driver";
        e51p_device driver; require(api->add_device(context.get(), &d, &driver));
        e51p_endpoint controls, data, tone;
        e51p_endpoint_desc endpoint{sizeof(endpoint), "controls", E51P_DIGITAL, E51P_OUTPUT, 3, 0, 0, 0};
        require(api->add_endpoint(context.get(), driver, &endpoint, &controls));
        endpoint.name = "data"; endpoint.width = 8;
        require(api->add_endpoint(context.get(), driver, &endpoint, &data));
        endpoint = {sizeof(endpoint), "tone", E51P_ANALOG, E51P_OUTPUT, 1, 0, 0, 0};
        require(api->add_endpoint(context.get(), driver, &endpoint, &tone));
        require(api->connect_digital(context.get(), controls, 0, adc.cs_n, 0, 1));
        require(api->connect_digital(context.get(), controls, 1, adc.rd_n, 0, 1));
        require(api->connect_digital(context.get(), controls, 2, adc.wr_n, 0, 1));
        require(api->connect_digital(context.get(), data, 0, dacData, 0, 8));
        require(api->connect_analog(context.get(), directions & E51P_AUDIO_CAPTURE ? audio.capture_voltage : tone, adc.vin_positive));
        if (directions & E51P_AUDIO_PLAYBACK) require(api->connect_analog(context.get(), dacVoltage, audio.playback_voltage));
        require(api->start(context.get()));
        require(api->analog_write(context.get(), tone, 2.5));
        require(api->drive(context.get(), data, 128, 255, 0));
        require(api->drive(context.get(), controls, 2, 7, 0));
        require(api->drive(context.get(), controls, 6, 7, 0));
        auto status = e51p_audio_host_open(host.get(), stream.get(), &selection);
        if (status != E51P_OK) {
            char error[256]{}; e51p_audio_host_error(host.get(), error, sizeof(error));
            throw std::runtime_error(std::string("Cannot open selected audio device: ") + error);
        }
        require(e51p_audio_host_start(host.get()));
        const auto begin = std::chrono::steady_clock::now();
        const auto limit = static_cast<uint64_t>(seconds * 12000000);
        uint64_t now = 0, conversions = 0;
        while (now + 250 <= limit) {
            const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
            const auto target = std::min(limit, static_cast<uint64_t>(elapsed * 12000000));
            if (now + 250 > target) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }
            now += 250;
            require(api->advance_to(context.get(), now));
            e51p_digital_state ready{}; require(api->sample(context.get(), adc.intr_n, &ready));
            if (!ready.value) {
                require(api->drive(context.get(), controls, 4, 7, 0));
                e51p_digital_state sample{}; require(api->sample(context.get(), adc.data, &sample));
                if (sample.floating || sample.contention) throw std::runtime_error("ADC read bus is undefined");
                require(api->drive(context.get(), data, sample.value, 255, 0));
                require(api->drive(context.get(), controls, 2, 7, 0));
                if (!(directions & E51P_AUDIO_CAPTURE))
                    require(api->analog_write(context.get(), tone, 2.5 + std::sin(6.283185307179586 * 440 * double(now) / 12000000)));
                require(api->drive(context.get(), controls, 6, 7, 0));
                ++conversions;
            }
        }
        require(e51p_audio_host_stop(host.get()));
        e51p_audio_stats stats{}; stats.struct_size = sizeof(stats); require(e51p_audio_get_stats(stream.get(), &stats));
        require(stats.status);
        if (!conversions || ((directions & E51P_AUDIO_CAPTURE) && (!stats.captured_frames || !stats.capture_used)) ||
            ((directions & E51P_AUDIO_PLAYBACK) && !stats.playback_used)) throw std::runtime_error("Audio device did not deliver samples");
        std::printf("%s: ADC conversions=%llu capture used=%llu playback used=%llu misses=%llu/%llu drops=%llu/%llu\n",
            null ? "null" : "native", static_cast<unsigned long long>(conversions),
            static_cast<unsigned long long>(stats.capture_used), static_cast<unsigned long long>(stats.playback_used),
            static_cast<unsigned long long>(stats.capture_misses), static_cast<unsigned long long>(stats.playback_misses),
            static_cast<unsigned long long>(stats.capture_dropped), static_cast<unsigned long long>(stats.playback_dropped));
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
