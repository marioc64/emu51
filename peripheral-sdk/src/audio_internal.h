// SPDX-License-Identifier: LGPL-2.1-only
#pragma once
#include "emu51/audio.h"
#include <atomic>
#include <array>
#include <vector>

struct E51pAudioFrame {
    uint64_t epoch = 0, index = 0;
    float sample = 0;
};
class E51pAudioRing {
public:
    explicit E51pAudioRing(uint32_t size) : frames_(size) {}
    bool push(E51pAudioFrame frame) noexcept {
        const auto write = write_.load(std::memory_order_relaxed);
        if (write - read_.load(std::memory_order_acquire) >= frames_.size()) return false;
        frames_[write % frames_.size()] = frame;
        write_.store(write + 1, std::memory_order_release);
        return true;
    }
    bool peek(E51pAudioFrame& frame) const noexcept {
        const auto read = read_.load(std::memory_order_relaxed);
        if (read == write_.load(std::memory_order_acquire)) return false;
        frame = frames_[read % frames_.size()];
        return true;
    }
    void pop() noexcept { read_.store(read_.load(std::memory_order_relaxed) + 1, std::memory_order_release); }
    // All callback users must be stopped before replacing storage.
    void replace(std::vector<E51pAudioFrame>& storage) noexcept {
        frames_.swap(storage);
        read_.store(0, std::memory_order_relaxed);
        write_.store(0, std::memory_order_relaxed);
    }
    void discard() noexcept {
        // Consumer only. A producer publishing later remains visible normally.
        read_.store(write_.load(std::memory_order_acquire), std::memory_order_release);
    }
private:
    std::vector<E51pAudioFrame> frames_;
    std::atomic<uint32_t> write_{0}, read_{0};
};

struct e51p_audio_stream {
    const e51p_api& api;
    e51p_context* context; // simulation thread only; cleared by device cleanup
    e51p_audio_config config;
    e51p_audio_endpoints pins{sizeof(pins), E51P_INVALID_ID, E51P_INVALID_ID};
    E51pAudioRing capture, playback;
    std::atomic<uint32_t> refs{1};
    std::atomic<uint64_t> epoch{1};
    std::atomic<bool> paused{false}, host_attached{false};
    std::atomic<e51p_status> status{E51P_OK};
    std::atomic<uint64_t> captured{0}, capture_dropped{0}, capture_used{0}, capture_misses{0}, capture_skipped{0};
    std::atomic<uint64_t> playback_queued{0}, playback_dropped{0}, playback_used{0}, playback_misses{0}, playback_skipped{0};
    // These fields have exactly one owner each, independent of the other thread.
    uint64_t capture_epoch = 0, capture_index = 0;   // capture producer
    uint64_t playback_epoch = 0, playback_index = 0; // playback consumer
    uint64_t hz, phase = 0, simulation_index = 0;   // simulation thread
    double input_gain = 1; // simulation thread only
    std::array<e51p_audio_scope_frame, 4096> scope{};
    uint32_t scope_next = 0, scope_count = 0;
    float held_output = 0;
    bool started = false;
    e51p_audio_stream(const e51p_api& a, e51p_context* c, e51p_audio_config cfg, uint64_t clock)
        : api(a), context(c), config(cfg), capture(cfg.capacity_frames), playback(cfg.capacity_frames), hz(clock) {}
};
