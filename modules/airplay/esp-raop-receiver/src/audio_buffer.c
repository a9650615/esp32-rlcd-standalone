#include "audio_buffer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include "math.h"

static const char *TAG = "audio_buffer";

// See RAOP_BUFFER_FRAMES in audio_buffer.h for why this is 384, not
// upstream's 512.
#define BUFFER_FRAMES RAOP_BUFFER_FRAMES
#define MAX_FRAME_SIZE 2048
#define TIMING_THRESHOLD_MS 50  // Max drift before correction
#define TAP_LOOKAHEAD_MS    150  // Fire PCM tap this many ms before playtime

typedef struct {
    uint8_t data[MAX_FRAME_SIZE];
    size_t len;
    uint32_t playtime;
    bool ready;
    bool tapped;    // true once the lookahead tap has fired for this frame
} audio_frame_t;

static struct {
    audio_frame_t *frames;
    uint32_t read_idx;
    uint32_t write_idx;
    SemaphoreHandle_t mutex;
    TaskHandle_t task;
    bool running;
    uint32_t start_time;
    audio_output_callback_t output_cb;
    void *user_ctx;
    // Optional PCM tap — called ~TAP_LOOKAHEAD_MS before playtime
    audio_pcm_tap_cb_t tap_cb;
    void *tap_user_ctx;
    float *volume_ptr;
    // Scratch for the PCM tap, in PSRAM - see its use in audio_output_task().
    uint8_t *tap_scratch;
} audio_buf = {0};

static struct {
    uint32_t skip_frames;
    uint32_t pause_frames;
} timing_correction = {0, 0};

// Byte-for-byte the same clock as util.c's gettime_ms(), which is what
// produces the playtime values compared against it below. Duplicated rather
// than shared only because this file predates the include; do not "fix" it
// by switching one side to gettimeofday() or to tick count. A frame's
// playtime and this `now` must come from one time base, and a session spent
// hunting a mismatch that was never there is the reason this note exists.
static uint32_t gettime_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void audio_output_task(void *arg) {
    ESP_LOGD(TAG, "Audio output task started");

    // Same instrumentation raop.c's rtsp_thread() and rtp.c already carry:
    // report only on a new minimum, so a task that is comfortable stays quiet
    // and one that is not says so before it overflows rather than after.
    UBaseType_t stack_min = (UBaseType_t) -1;

    while (audio_buf.running) {
        UBaseType_t stack_now = uxTaskGetStackHighWaterMark(NULL);
        if (stack_now < stack_min) {
            stack_min = stack_now;
            ESP_LOGI(TAG, "audio_output task stack high water mark %u bytes free (new minimum)",
                     (unsigned) stack_min);
        }

        // Wait until we have a start time
        if (audio_buf.start_time == 0) {
            // Rate-limited (same shape as the stack high-water-mark logs
            // above): this task polls every 10ms, so logging every miss
            // would spam. If this fires more than a handful of times
            // during a session that clearly reached RAOP_INT_PLAY (see
            // audio_buffer_set_start_time()'s own log), this task is not
            // seeing that write - the two logs together are what tells
            // "never written" apart from "written but not observed here".
            static int stuck_logged = 0;
            if (stuck_logged < 5) {
                stuck_logged++;
                ESP_LOGW(TAG, "waiting for start_time (still 0, audio_buf=%p)", (void*) &audio_buf);
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // Handle skip frames
        if (timing_correction.skip_frames > 0) {
            xSemaphoreTake(audio_buf.mutex, portMAX_DELAY);

            while (timing_correction.skip_frames > 0 && audio_buf.read_idx != audio_buf.write_idx) {
                audio_buf.frames[audio_buf.read_idx].ready = false;
                audio_buf.frames[audio_buf.read_idx].tapped = false;
                audio_buf.read_idx = (audio_buf.read_idx + 1) % BUFFER_FRAMES;
                timing_correction.skip_frames--;
            }

            xSemaphoreGive(audio_buf.mutex);
            continue;
        }

        // Handle pause frames (insert silence)
        if (timing_correction.pause_frames > 0) {
            // const, so it lives in flash and costs no RAM of any kind.
            // Upstream declared this 2048-byte array as automatic storage in
            // a task with a 4096-byte stack; see UPSTREAM.md. The callback
            // takes a const pointer and only reads, and the contents are
            // zeroes that never change, so there is nothing to write here.
            static const uint8_t silence[1408] = {0}; // 352 samples * 4 bytes
            audio_buf.output_cb(silence, sizeof(silence), audio_buf.user_ctx);
            timing_correction.pause_frames--;
            continue;
        }

        // Normal playback
        audio_frame_t *frame = NULL;

        xSemaphoreTake(audio_buf.mutex, portMAX_DELAY);
        if (audio_buf.read_idx != audio_buf.write_idx) {
            frame = &audio_buf.frames[audio_buf.read_idx];
            if (frame->ready) {
                uint32_t now = gettime_ms();

                // Fire the PCM tap ~TAP_LOOKAHEAD_MS before playtime.
                // This gives the FFT pipeline time to process and send
                // the UDP packet so visualisation aligns with audio.
                if (audio_buf.tap_cb && !frame->tapped &&
                    frame->playtime > 0 && now >= frame->playtime - TAP_LOOKAHEAD_MS) {
                    frame->tapped = true;
                    // Copy data before releasing mutex — tap must not block
                    // PSRAM, allocated once in audio_buffer_init(), for the
                    // same reason as `silence` above: 2 KiB of automatic
                    // storage does not fit this task's 4 KiB stack alongside
                    // the tap callback, and internal RAM is the scarcest
                    // memory on this board - moving it to .bss would have
                    // spent exactly the same bytes. See UPSTREAM.md.
                    uint8_t *tap_data = audio_buf.tap_scratch;
                    memcpy(tap_data, frame->data, frame->len);
                    size_t tap_len = frame->len;
                    xSemaphoreGive(audio_buf.mutex);
                    audio_buf.tap_cb(tap_data, tap_len, audio_buf.tap_user_ctx);
                    // Re-acquire and re-check — frame may have been skipped/flushed
                    xSemaphoreTake(audio_buf.mutex, portMAX_DELAY);
                    frame = &audio_buf.frames[audio_buf.read_idx];
                    if (!frame->ready) {
                        xSemaphoreGive(audio_buf.mutex);
                        continue;
                    }
                    now = gettime_ms();
                }

                // Check if it's time to play this frame
                if (now < frame->playtime) {
                    // Diagnostic, first few only: a stream that produces no
                    // writes at all within the sink's 2 s watchdog window
                    // looks identical from outside to one that is decoding
                    // nothing, and this gate is the difference. Logged with
                    // both sides of the comparison because "waiting" is only
                    // meaningful next to how long the wait would be.
                    static int waits_logged = 0;
                    if (waits_logged < 5) {
                        waits_logged++;
                        ESP_LOGW(TAG, "playback gate: now=%u playtime=%u (waiting %d ms) start_time=%u",
                                 (unsigned) now, (unsigned) frame->playtime,
                                 (int) (frame->playtime - now),
                                 (unsigned) audio_buf.start_time);
                    }
                    xSemaphoreGive(audio_buf.mutex);
                    vTaskDelay(pdMS_TO_TICKS(5));
                    continue;
                }

                static bool first_write_logged = false;
                if (!first_write_logged) {
                    first_write_logged = true;
                    ESP_LOGI(TAG, "playback gate: first frame released at now=%u playtime=%u",
                             (unsigned) now, (unsigned) frame->playtime);
                }

                // Third MAX_FRAME_SIZE automatic buffer in this task, and the
                // one on the hot path - see UPSTREAM.md on the stack.
                uint8_t data[MAX_FRAME_SIZE];
                memcpy(data, frame->data, frame->len);
                size_t len = frame->len;

                // Apply software volume at read time
                if (audio_buf.volume_ptr) {
                    float vol_db = *audio_buf.volume_ptr;
                    if (vol_db <= -144.0f) {
                        memset(data, 0, len);
                    } else if (vol_db < 0.0f) {
                        float linear = powf(10.0f, vol_db / 20.0f);
                        int16_t *samples = (int16_t *)data;
                        size_t count = len / sizeof(int16_t);
                        for (size_t i = 0; i < count; i++) {
                            int32_t scaled = (int32_t)(samples[i] * linear);
                            if (scaled > 32767) scaled = 32767;
                            if (scaled < -32768) scaled = -32768;
                            samples[i] = (int16_t)scaled;
                        }
                    }
                    // vol_db == 0.0f: full volume, no scaling needed
                }

                frame->ready = false;
                frame->tapped = false;
                audio_buf.read_idx = (audio_buf.read_idx + 1) % BUFFER_FRAMES;

                xSemaphoreGive(audio_buf.mutex);

                audio_buf.output_cb(data, len, audio_buf.user_ctx);
            } else {
                xSemaphoreGive(audio_buf.mutex);
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        } else {
            xSemaphoreGive(audio_buf.mutex);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    ESP_LOGD(TAG, "Audio output task exiting");
    vTaskSuspend(NULL);
}

void audio_buffer_init(audio_output_callback_t output_cb, void *user_ctx,
                       audio_pcm_tap_cb_t tap_cb, float *volume_ptr) {
    // If already initialized, just return
    if (audio_buf.frames && audio_buf.mutex) {
        ESP_LOGD(TAG, "Audio buffer already initialized");
        return;
    }

    // Clean up any partial state
    if (audio_buf.frames) free(audio_buf.frames);
    if (audio_buf.tap_scratch) free(audio_buf.tap_scratch);
    if (audio_buf.mutex) vSemaphoreDelete(audio_buf.mutex);

    memset(&audio_buf, 0, sizeof(audio_buf));

    // Store callback
    audio_buf.output_cb = output_cb;
    audio_buf.user_ctx = user_ctx;
    audio_buf.tap_cb = tap_cb;
    audio_buf.volume_ptr = volume_ptr;

    // Allocate frame buffer in PSRAM
    audio_buf.frames = heap_caps_malloc(BUFFER_FRAMES * sizeof(audio_frame_t),
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!audio_buf.frames) {
        ESP_LOGE(TAG, "Failed to allocate frame buffer in PSRAM");
        return;
    }

    memset(audio_buf.frames, 0, BUFFER_FRAMES * sizeof(audio_frame_t));

    audio_buf.tap_scratch = heap_caps_malloc(MAX_FRAME_SIZE,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!audio_buf.tap_scratch) {
        ESP_LOGE(TAG, "Failed to allocate tap scratch in PSRAM");
        free(audio_buf.frames);
        audio_buf.frames = NULL;
        return;
    }

    audio_buf.mutex = xSemaphoreCreateMutex();
    if (!audio_buf.mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        free(audio_buf.frames);
        audio_buf.frames = NULL;
        free(audio_buf.tap_scratch);
        audio_buf.tap_scratch = NULL;
        return;
    }

    ESP_LOGI(TAG, "Audio buffer initialized");
}

void audio_buffer_start(void) {
    if (!audio_buf.frames || !audio_buf.mutex) {
        ESP_LOGE(TAG, "Cannot start: buffer not initialized");
        return;
    }

    if (audio_buf.running) {
        ESP_LOGW(TAG, "Audio buffer already running");
        return;
    }

    audio_buf.running = true;
    audio_buf.read_idx = 0;
    audio_buf.write_idx = 0;
    audio_buf.start_time = 0;

    // Upstream used 4096. That overflowed on the first real stream this
    // receiver ever decoded, immediately after RECORD - the crash lands as
    // soon as playback starts, which is when output_cb() first runs. The
    // depth is in that callback, not in this file: it leads into this
    // project's own ES8311/I2S sink (modules/audio). Two of the 2 KiB
    // scratch buffers this function's task used to keep on the stack were
    // moved to flash and PSRAM in the same change, so the extra 4 KiB here
    // is paid for out of internal RAM that was just given back rather than
    // taken from anywhere else. Sized provisionally: the task now reports
    // its own high water mark, so the next reading replaces this guess with
    // a measurement.
    xTaskCreatePinnedToCore(
        audio_output_task,
        "audio_output",
        8192,
        NULL,
        3,
        &audio_buf.task,
        1
    );

    ESP_LOGI(TAG, "Audio playback started");
}

void audio_buffer_set_start_time(uint32_t playtime) {
    if (!audio_buf.running) {
        ESP_LOGW(TAG, "Cannot set start time: buffer not running");
        return;
    }

    audio_buf.start_time = playtime;
    // Raised from D to I: this write is the only thing that gets
    // audio_output_task() past its start_time==0 gate, and the "buffer
    // full, dropping frame" spam it produces when consumers never wake up
    // looks identical whether this write never happened, happened with a
    // stale/zero value, or happened but the reader task - pinned to a
    // different core (xTaskCreatePinnedToCore(...,1) in
    // audio_buffer_start()) - never observed it. Temporary-diagnostic in
    // spirit but left at I rather than reverted: it is one line per
    // session, not per frame, so it costs nothing to keep.
    ESP_LOGI(TAG, "start_time set to %u ms (audio_buf=%p)", playtime, (void*) &audio_buf);
}

void audio_buffer_stop(void) {
    if (!audio_buf.running) {
        ESP_LOGD(TAG, "Audio buffer already stopped");
        return;
    }

    audio_buf.running = false;

    if (audio_buf.task) {
        vTaskDelay(pdMS_TO_TICKS(100));
        vTaskDelete(audio_buf.task);
        audio_buf.task = NULL;
    }

    ESP_LOGI(TAG, "Audio playback stopped");
}

bool audio_buffer_write(const uint8_t *data, size_t len, uint32_t playtime) {
    if (!audio_buf.frames || !audio_buf.mutex) {
        ESP_LOGW(TAG, "Buffer not initialized, dropping frame");
        return false;
    }

    if (len > MAX_FRAME_SIZE) {
        ESP_LOGE(TAG, "Frame too large: %zu bytes", len);
        return false;
    }

    // Try up to 5 times with 10ms delays between attempts
    for (int retries = 0; retries < 5; retries++) {
        xSemaphoreTake(audio_buf.mutex, portMAX_DELAY);

        uint32_t next_write = (audio_buf.write_idx + 1) % BUFFER_FRAMES;

        // Check if buffer has space
        if (next_write != audio_buf.read_idx) {
            audio_frame_t *frame = &audio_buf.frames[audio_buf.write_idx];
            memcpy(frame->data, data, len);
            frame->len = len;
            frame->playtime = playtime;
            frame->ready = true;
            frame->tapped = false;

            audio_buf.write_idx = next_write;

            xSemaphoreGive(audio_buf.mutex);
            return true;
        }

        xSemaphoreGive(audio_buf.mutex);

        // Buffer full, wait for I2S to drain
        if (retries < 4) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    ESP_LOGW(TAG, "Buffer full after retries, dropping frame");
    return false;
}

void audio_buffer_flush(void) {
    xSemaphoreTake(audio_buf.mutex, portMAX_DELAY);

    // Mark all frames as not ready
    for (int i = 0; i < BUFFER_FRAMES; i++) {
        audio_buf.frames[i].ready = false;
        audio_buf.frames[i].tapped = false;
    }

    audio_buf.read_idx = 0;
    audio_buf.write_idx = 0;

    xSemaphoreGive(audio_buf.mutex);

    ESP_LOGD(TAG, "Buffer flushed");
}

void audio_buffer_deinit(void) {
    // Stop playback if running
    if (audio_buf.running) {
        audio_buffer_stop();
    }

    if (audio_buf.mutex) {
        vSemaphoreDelete(audio_buf.mutex);
        audio_buf.mutex = NULL;
    }

    if (audio_buf.frames) {
        free(audio_buf.frames);
        audio_buf.frames = NULL;
    }

    ESP_LOGI(TAG, "Audio buffer deinitialized");
}

void audio_buffer_get_timing(uint32_t *frames_buffered, uint32_t *head_playtime) {

    if (!audio_buf.frames || !audio_buf.mutex) {
        *frames_buffered = 0;
        *head_playtime = 0;
        return;
    }

    xSemaphoreTake(audio_buf.mutex, portMAX_DELAY);

    // Calculate how many frames are buffered
    if (audio_buf.write_idx >= audio_buf.read_idx) {
        *frames_buffered = audio_buf.write_idx - audio_buf.read_idx;
    } else {
        *frames_buffered = BUFFER_FRAMES - audio_buf.read_idx + audio_buf.write_idx;
    }

    // Get the playtime of the most recent frame
    if (*frames_buffered > 0) {
        uint32_t head_idx = (audio_buf.write_idx == 0) ? BUFFER_FRAMES - 1 : audio_buf.write_idx - 1;
        *head_playtime = audio_buf.frames[head_idx].playtime;
    } else {
        *head_playtime = 0;
    }

    xSemaphoreGive(audio_buf.mutex);
}

void audio_buffer_skip_frames(uint32_t count) {
    timing_correction.skip_frames = count;
    ESP_LOGD(TAG, "Will skip %u frames", count);
}

void audio_buffer_pause_frames(uint32_t count) {
    timing_correction.pause_frames = count;
    ESP_LOGD(TAG, "Will pause %u frames", count);
}

bool audio_buffer_is_ready(void) {
    return audio_buf.frames != NULL && audio_buf.running;
}