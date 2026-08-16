#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Depth, in 352-sample ALAC frames, shared by this file's decoded-PCM ring
// (BUFFER_FRAMES in audio_buffer.c) and raop_core.c's RTP jitter buffer
// (RAOP_INT_SETUP's heap_caps_malloc(..., MALLOC_CAP_SPIRAM, ...)) - both
// PSRAM allocations, both upstream at 512 frames each (~4.1s / ~2.7MB
// combined: 512 frames * 1408-or-2048 bytes/frame), sized for multi-room
// sync headroom this receiver doesn't do. 384 frames is ~3.05s
// (384 * 352 / 44100 Hz) - a few seconds of headroom against Wi-Fi jitter,
// nothing more - for roughly half the combined PSRAM cost (~1.3MB).
// rtp.c's own BUFFER_FRAMES_MAX/MIN are left alone: those size a small
// internal-RAM bookkeeping array (abuf_t pointers/counters), not a PSRAM
// buffer, so they are not what this constant is about.
#define RAOP_BUFFER_FRAMES 384

// Audio output callback - called when audio needs to be written to hardware
typedef void (*audio_output_callback_t)(const uint8_t *data, size_t len, void *user_ctx);

// PCM tap callback - called with audio data at the moment it is sent to hardware
typedef void (*audio_pcm_tap_cb_t)(const uint8_t *data, size_t len, void *user_ctx);

// Initialize the audio buffer with output callback
void audio_buffer_init(audio_output_callback_t output_cb, void *user_ctx,
                       audio_pcm_tap_cb_t tap_cb, float *volume_ptr);

// Start audio playback task
void audio_buffer_start(void);

// Set the time when playback should actually begin
void audio_buffer_set_start_time(uint32_t playtime);

// Stop audio playback task (buffer remains allocated)
void audio_buffer_stop(void);

// Write audio data with timing information (called from RTP data callback)
// Returns true if written, false if buffer full
bool audio_buffer_write(const uint8_t *data, size_t len, uint32_t playtime);

// Flush all buffered audio
void audio_buffer_flush(void);

// Cleanup (stops playback and frees memory)
void audio_buffer_deinit(void);

// Get current buffer level for sync calculations
void audio_buffer_get_timing(uint32_t *frames_buffered, uint32_t *head_playtime);

// Timing correction
void audio_buffer_skip_frames(uint32_t count);
void audio_buffer_pause_frames(uint32_t count);

// Initialization check
bool audio_buffer_is_ready(void);