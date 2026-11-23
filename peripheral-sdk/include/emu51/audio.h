// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#ifndef EMU51_PERIPHERAL_AUDIO_H
#define EMU51_PERIPHERAL_AUDIO_H
#include "peripheral.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct e51p_audio_stream e51p_audio_stream;
enum { E51P_AUDIO_CAPTURE = 1, E51P_AUDIO_PLAYBACK = 2 };

typedef struct e51p_audio_config {
    uint32_t struct_size;
    uint32_t directions;        /* CAPTURE, PLAYBACK, or both */
    uint32_t sample_rate;       /* normalized mono floats per second: 1..192000 */
    uint32_t capacity_frames;   /* each SPSC queue: power of two, 2..1048576 */
    uint32_t latency_frames;    /* startup latency in each direction; < capacity */
    double input_offset_volts;  /* voltage = offset + normalized sample * scale */
    double input_scale_volts;   /* finite, positive */
    double output_offset_volts; /* normalized sample = (voltage - offset) / scale */
    double output_scale_volts;  /* finite, positive; output is clipped to [-1,1] */
} e51p_audio_config;

typedef struct e51p_audio_endpoints {
    uint32_t struct_size;
    e51p_endpoint capture_voltage;  /* analog OUTPUT, or INVALID_ID */
    e51p_endpoint playback_voltage; /* analog INPUT, or INVALID_ID */
} e51p_audio_endpoints;

typedef struct e51p_audio_stats {
    uint32_t struct_size;
    e51p_status status; /* first fatal bridge error, cleared by context reset */
    uint64_t captured_frames;
    uint64_t capture_dropped;  /* queue full: newest sample dropped */
    uint64_t capture_used;     /* samples actually presented on the voltage pin */
    uint64_t capture_misses;   /* sample boundaries missing their expected input */
    uint64_t capture_skipped;  /* stale/intermediate input frames discarded */
    uint64_t playback_queued;
    uint64_t playback_dropped;
    uint64_t playback_used;
    uint64_t playback_misses;
    uint64_t playback_skipped;
} e51p_audio_stats;

/* 48 kHz, 4096-frame queues, 480-frame latency, +/-1 mapped to 0..5 V. */
e51p_audio_config e51p_audio_config_init(uint32_t directions);

/* Register before start. Clock frequency must be 1..1 GHz. The returned stream
 * owns one reference, and the context owns another. Release the caller reference
 * after its audio callback users are stopped. Context destruction silences the
 * stream and releases its reference; callbacks never access the SDK context.
 * Discard the graph if multi-step endpoint registration fails. */
e51p_status e51p_audio_create(const e51p_api*, e51p_context*, const char* instance_id,
    const e51p_audio_config*, e51p_audio_stream**, e51p_audio_endpoints*);
void e51p_audio_retain(e51p_audio_stream*);
void e51p_audio_release(e51p_audio_stream*);

/* Simulation-thread operations. Pause/resume rebase sample timelines at the
 * current emulated time and invalidate queued frames. Paused input is the
 * configured zero-sample voltage; paused playback is silence. Context reset
 * similarly begins a new epoch without touching callback-owned queue indices.
 * Statistics are cumulative, approximate atomic snapshots across threads. */
e51p_status e51p_audio_set_paused(e51p_audio_stream*, uint32_t paused);
e51p_status e51p_audio_get_stats(const e51p_audio_stream*, e51p_audio_stats*);

/* Simulation thread only. Linear input gain (0..1000, default 1) is applied
 * before voltage mapping, clipping the amplified sample to [-1,1]. Changes do
 * not restart audio or reset time; gain survives pause and context reset. */
e51p_status e51p_audio_set_input_gain(e51p_audio_stream*, double gain);

/* Simulation thread only, paused stream, no attached native host, and all
 * manual callbacks stopped/joined. Reallocate both queues transactionally;
 * capacity uses the same bounds as config_init/create, latency < capacity.
 * Starts a fresh sample epoch without resetting context time, gain, held
 * playback voltage or cumulative statistics. Failure leaves buffers intact. */
e51p_status e51p_audio_set_buffering(e51p_audio_stream*, uint32_t capacity_frames,
    uint32_t latency_frames);

typedef struct e51p_audio_scope_frame {
    float input;    /* normalized microphone before gain */
    float adc;      /* normalized input after gain and clipping */
    float output;   /* normalized sample queued for host playback */
} e51p_audio_scope_frame;
/* Simulation thread only. Copy the newest min(capacity,4096) sample boundaries
 * in chronological order. Returns count and configured sample rate. History
 * freezes while paused, clears on reset/resume, and never consumes audio queues.
 * These are bridge signals, not a recording of physical device latency/gaps.
 * Coarse advances repeat their final observable input over skipped boundaries. */
e51p_status e51p_audio_read_scope(const e51p_audio_stream*, e51p_audio_scope_frame*,
    uint32_t capacity, uint32_t* count, uint32_t* sample_rate);

/* Real-time boundary: exactly one capture producer and one playback consumer.
 * They may be different threads. Do not mix manual calls with a native host
 * attached to the same stream. No allocation, locks, SDK calls, or wall clocks.
 * Capture returns accepted frames; a null input supplies normalized silence.
 * Playback always fills output, returning the number of actual queued samples
 * used. Missing, paused, startup-latency and stale frames become silence.
 * Each direction uses sequence numbers so overruns cannot shift later audio
 * into the missing time interval. Nonfinite capture samples become zero. */
uint32_t e51p_audio_capture(e51p_audio_stream*, const float* input, uint32_t frames);
uint32_t e51p_audio_playback(e51p_audio_stream*, float* output, uint32_t frames);

#ifdef __cplusplus
}
#endif
#endif
