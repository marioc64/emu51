// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#ifndef EMU51_PERIPHERAL_AUDIO_HOST_H
#define EMU51_PERIPHERAL_AUDIO_HOST_H
#include "audio.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct e51p_audio_host e51p_audio_host;
enum { E51P_AUDIO_NATIVE = 0, E51P_AUDIO_NULL = 1 };
#define E51P_AUDIO_DEFAULT_DEVICE UINT32_MAX
#define E51P_AUDIO_ALL_CHANNELS UINT32_MAX

typedef struct e51p_audio_device_info {
    uint32_t struct_size;
    uint32_t index;          /* index in this host's immutable enumeration */
    uint32_t is_default;
    char name[256];
} e51p_audio_device_info;
typedef struct e51p_audio_host_config {
    uint32_t struct_size;
    uint32_t capture_device, playback_device; /* index or DEFAULT_DEVICE */
    uint32_t capture_channel;                 /* zero-based native channel */
    uint32_t playback_channel;                /* channel or ALL_CHANNELS */
} e51p_audio_host_config;

e51p_audio_host_config e51p_audio_host_config_init(void);

/* Native backend is optional: link emu51::audio_host. Creation enumerates
 * devices but does not open them. NULL selects a software device for tests,
 * never a microphone or speakers. Native selection never falls back to NULL.
 * All host operations belong to one control/simulation thread. */
e51p_status e51p_audio_host_create(uint32_t backend, e51p_audio_host**);
void e51p_audio_host_destroy(e51p_audio_host*);
e51p_status e51p_audio_host_device_count(const e51p_audio_host*, uint32_t direction, uint32_t* count);
e51p_status e51p_audio_host_device_info(const e51p_audio_host*, uint32_t direction,
    uint32_t index, e51p_audio_device_info*);
/* Open may request OS microphone permission for capture. It does not start
 * callbacks. Only one host may attach to a stream. The host retains a reference.
 * Float mono data are converted to the native format/rate by the backend. */
e51p_status e51p_audio_host_open(e51p_audio_host*, e51p_audio_stream*, const e51p_audio_host_config*);
/* Start flushes old queues while callbacks are stopped, starts the device with
 * the bridge still paused, then rebases/unpauses after device startup completes.
 * Hosts must begin simulation wall-clock pacing after start returns, including
 * on resume. Stop pauses the bridge and waits for the device to stop.
 * No SDK calls occur in callbacks. close/destroy stop callbacks before releasing
 * their stream reference. Repeated stop/start does not require a context reset. */
e51p_status e51p_audio_host_start(e51p_audio_host*);
e51p_status e51p_audio_host_stop(e51p_audio_host*);
void e51p_audio_host_close(e51p_audio_host*);
e51p_status e51p_audio_host_error(const e51p_audio_host*, char* message, uint32_t capacity);
#ifdef __cplusplus
}
#endif
#endif
