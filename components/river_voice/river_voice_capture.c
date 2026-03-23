#include <stdbool.h>
#include <string.h>

#include "basic_types.h"
#include "audio/audio_control.h"
#include "audio/audio_record.h"
#include "os_wrapper.h"

#include "river/river_audio_frame_ring.h"
#include "river/river_log.h"
#include "river/river_voice_board.h"
#include "river/river_voice_capture.h"
#include "river/river_voice_profile.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.voice.capture"

#define RIVER_VOICE_CAPTURE_RING_BUF_MS    1600U
#define RIVER_VOICE_CAPTURE_THREAD_STACK   (1024U * 4U)
/* Priority 6: Above all other voice/app tasks to ensure IPC responsiveness */
#define RIVER_VOICE_CAPTURE_THREAD_PRIO    6U

typedef struct {
    bool initialized;
    size_t frame_bytes;
    uint32_t frame_capacity;
    uint32_t dropped_frames;
    uint32_t read_ok;
    uint32_t read_wait_timeout;
    rtos_sema_t ready;
    river_audio_frame_ring_t ring;
} river_capture_frame_queue_t;

static struct {
    river_capture_frame_queue_t queue;
    rtos_task_t thread;
    bool running;
    river_voice_capture_t *active_capture;
    uint8_t *buffer_block;
    size_t buffer_block_bytes;
    uint8_t *io_buf;
    uint8_t *frame_buf;
    uint8_t *discard_buf;
} g_river_cap_internal;

static void river_capture_frame_queue_drain_signal(river_capture_frame_queue_t *queue)
{
    if (queue == NULL || !queue->initialized) {
        return;
    }

    while (rtos_sema_get_count(queue->ready) > 0U) {
        if (rtos_sema_take(queue->ready, 0U) != RTK_SUCCESS) {
            break;
        }
    }
}

static river_status_t river_capture_frame_queue_init(river_capture_frame_queue_t *queue,
                                                     size_t frame_bytes,
                                                     uint32_t frame_capacity,
                                                     void *storage,
                                                     size_t storage_bytes)
{
    river_status_t status;

    if (queue == NULL || frame_bytes == 0U || frame_capacity == 0U) {
        return RIVER_ERR_ARG;
    }

    if (queue->initialized) {
        if (queue->frame_bytes != frame_bytes || queue->frame_capacity != frame_capacity) {
            return RIVER_ERR_BUSY;
        }

        queue->dropped_frames = 0U;
        queue->read_ok = 0U;
        queue->read_wait_timeout = 0U;
        river_audio_frame_ring_reset(&queue->ring);
        river_capture_frame_queue_drain_signal(queue);
        return RIVER_OK;
    }

    memset(queue, 0, sizeof(*queue));
    status = river_audio_frame_ring_init_with_storage(&queue->ring,
                                                      storage,
                                                      storage_bytes,
                                                      frame_bytes,
                                                      frame_capacity);
    if (status != RIVER_OK) {
        return status;
    }

    if (rtos_sema_create(&queue->ready, 0U, frame_capacity) != RTK_SUCCESS) {
        river_audio_frame_ring_deinit(&queue->ring);
        return RIVER_ERR_NO_MEMORY;
    }

    queue->initialized = true;
    queue->frame_bytes = frame_bytes;
    queue->frame_capacity = frame_capacity;
    return RIVER_OK;
}

static void river_capture_frame_queue_deinit(river_capture_frame_queue_t *queue)
{
    if (queue == NULL || !queue->initialized) {
        return;
    }

    river_capture_frame_queue_drain_signal(queue);
    rtos_sema_delete(queue->ready);
    river_audio_frame_ring_deinit(&queue->ring);
    memset(queue, 0, sizeof(*queue));
}

static void river_capture_release_persistent_buffers(void)
{
    if (g_river_cap_internal.buffer_block != NULL) {
        rtos_mem_free(g_river_cap_internal.buffer_block);
    }
    g_river_cap_internal.buffer_block = NULL;
    g_river_cap_internal.buffer_block_bytes = 0U;
    g_river_cap_internal.io_buf = NULL;
    g_river_cap_internal.frame_buf = NULL;
    g_river_cap_internal.discard_buf = NULL;
}

static river_status_t river_capture_prepare_persistent_buffers(size_t frame_bytes,
                                                               uint32_t frame_capacity)
{
    uint64_t ring_bytes64;
    uint64_t total_bytes64;
    size_t ring_bytes;
    size_t total_bytes;
    uint8_t *ring_storage;

    if (frame_bytes == 0U || frame_capacity == 0U) {
        return RIVER_ERR_ARG;
    }

    ring_bytes64 = (uint64_t)frame_bytes * (uint64_t)frame_capacity;
    total_bytes64 = ring_bytes64 + ((uint64_t)frame_bytes * 3ULL);
    if (ring_bytes64 == 0U || total_bytes64 > UINT32_MAX) {
        return RIVER_ERR_ARG;
    }

    ring_bytes = (size_t)ring_bytes64;
    total_bytes = (size_t)total_bytes64;

    if (g_river_cap_internal.buffer_block != NULL &&
        (g_river_cap_internal.queue.frame_bytes != frame_bytes ||
         g_river_cap_internal.queue.frame_capacity != frame_capacity ||
         g_river_cap_internal.buffer_block_bytes < total_bytes)) {
        river_capture_frame_queue_deinit(&g_river_cap_internal.queue);
        river_capture_release_persistent_buffers();
    }

    if (g_river_cap_internal.buffer_block == NULL) {
        g_river_cap_internal.buffer_block = (uint8_t *)rtos_mem_zmalloc((uint32_t)total_bytes);
        if (g_river_cap_internal.buffer_block == NULL) {
            return RIVER_ERR_NO_MEMORY;
        }
        g_river_cap_internal.buffer_block_bytes = total_bytes;
    }

    memset(g_river_cap_internal.buffer_block, 0, total_bytes);
    ring_storage = g_river_cap_internal.buffer_block;
    g_river_cap_internal.io_buf = ring_storage + ring_bytes;
    g_river_cap_internal.frame_buf = g_river_cap_internal.io_buf + frame_bytes;
    g_river_cap_internal.discard_buf = g_river_cap_internal.frame_buf + frame_bytes;

    return river_capture_frame_queue_init(&g_river_cap_internal.queue,
                                          frame_bytes,
                                          frame_capacity,
                                          ring_storage,
                                          ring_bytes);
}

static void river_capture_frame_queue_write(river_capture_frame_queue_t *queue,
                                            const uint8_t *frame,
                                            uint8_t *discard_frame)
{
    river_status_t status;

    if (queue == NULL || !queue->initialized || frame == NULL) {
        return;
    }

    status = river_audio_frame_ring_write(&queue->ring, frame);
    if (status == RIVER_OK) {
        rtos_sema_give(queue->ready);
        return;
    }

    if (status != RIVER_ERR_NO_MEMORY || discard_frame == NULL) {
        return;
    }

    if (river_audio_frame_ring_read(&queue->ring, discard_frame) != RIVER_OK) {
        return;
    }

    queue->dropped_frames++;
    status = river_audio_frame_ring_write(&queue->ring, frame);
    if (status != RIVER_OK) {
        return;
    }

    if ((queue->dropped_frames & 0x3FU) == 1U) {
        RIVER_LOGW("capture frame ring overflow: dropped=%lu frames=%lu capacity=%lu",
                   (unsigned long)queue->dropped_frames,
                   (unsigned long)river_audio_frame_ring_count(&queue->ring),
                   (unsigned long)queue->frame_capacity);
    }
}

static int32_t river_capture_frame_queue_read(river_capture_frame_queue_t *queue, uint8_t *frame)
{
    if (queue == NULL || !queue->initialized || frame == NULL) {
        return -1;
    }

    for (;;) {
        if (!g_river_cap_internal.running) {
            return 0;
        }
        if (rtos_sema_take(queue->ready, 500U) != RTK_SUCCESS) {
            queue->read_wait_timeout++;
            continue;
        }
        if (river_audio_frame_ring_read(&queue->ring, frame) == RIVER_OK) {
            queue->read_ok++;
            return (int32_t)queue->frame_bytes;
        }
    }
}

static void river_voice_capture_thread(void *param)
{
    river_voice_capture_t *capture = (river_voice_capture_t *)param;
    uint8_t *io_buf = g_river_cap_internal.io_buf;
    uint8_t *frame_buf = g_river_cap_internal.frame_buf;
    uint8_t *discard_buf = g_river_cap_internal.discard_buf;
    size_t pending_bytes = 0U;

    RIVER_LOGI("internal capture thread started (priority=%d stack=%luB)",
               RIVER_VOICE_CAPTURE_THREAD_PRIO,
               (unsigned long)RIVER_VOICE_CAPTURE_THREAD_STACK);

    if (io_buf == NULL || frame_buf == NULL || discard_buf == NULL) {
        RIVER_LOGE("capture thread buffers unavailable: frame=%luB",
                   (unsigned long)capture->frame_bytes);
        g_river_cap_internal.running = false;
        rtos_task_delete(NULL);
        return;
    }

    while (g_river_cap_internal.running) {
        int32_t ret;

        ret = AudioRecord_Read((struct AudioRecord *)capture->record, io_buf, capture->frame_bytes, true);
        if (ret > 0) {
            size_t consumed = 0U;
            size_t available = (size_t)ret;

            while (consumed < available) {
                size_t chunk = capture->frame_bytes - pending_bytes;
                if (chunk > (available - consumed)) {
                    chunk = available - consumed;
                }

                memcpy(frame_buf + pending_bytes, io_buf + consumed, chunk);
                pending_bytes += chunk;
                consumed += chunk;

                if (pending_bytes == capture->frame_bytes) {
                    river_capture_frame_queue_write(&g_river_cap_internal.queue, frame_buf, discard_buf);
                    pending_bytes = 0U;
                }
            }
        } else {
            rtos_time_delay_ms(5);
        }
    }

    rtos_task_delete(NULL);
}

river_status_t river_voice_capture_open(river_voice_capture_t *capture)
{
    const river_voice_board_array_profile_t *profile;
    const river_voice_profile_config_t *voice_profile;
    AudioRecordConfig record_config;

    if (capture == 0) {
        return RIVER_ERR_ARG;
    }

    memset(capture, 0, sizeof(*capture));
    profile = river_voice_board_array_profile();
    voice_profile = river_voice_profile_active();

    capture->sample_rate = profile->sample_rate;
    capture->channels = voice_profile->capture_channels;
    capture->frame_ms = profile->frame_ms;
    capture->frame_samples = (profile->sample_rate * profile->frame_ms) / 1000U;
    capture->frame_bytes = capture->frame_samples * capture->channels * sizeof(int16_t);

    AudioControl_SetMicUsage(AUDIO_CAPTURE_USAGE_AMIC);
    AudioControl_SetChannelMicCategory(0, profile->primary_mic);
    AudioControl_SetMicBstGain(profile->primary_mic, profile->primary_mic_gain);
    if (profile->capture_channels > 1U) {
        AudioControl_SetChannelMicCategory(1, profile->secondary_mic);
        AudioControl_SetMicBstGain(profile->secondary_mic, profile->secondary_mic_gain);
    }

    capture->record = AudioRecord_Create();
    if (capture->record == 0) {
        return RIVER_ERR_UNSUPPORTED;
    }

    record_config.sample_rate = capture->sample_rate;
    record_config.channel_count = capture->channels;
    record_config.format = AUDIO_FORMAT_PCM_16_BIT;
    record_config.device = DEVICE_IN_MIC;
    record_config.buffer_bytes = (uint32_t)capture->frame_bytes * 4U;
    
    if (AudioRecord_Init((struct AudioRecord *)capture->record, &record_config, AUDIO_INPUT_FLAG_NONE) != 0) {
        river_voice_capture_close(capture);
        return RIVER_ERR_UNSUPPORTED;
    }

    AudioRecord_SetParameters((struct AudioRecord *)capture->record,
                              voice_profile->capture_audio_record_params);

    if (AudioRecord_Start((struct AudioRecord *)capture->record) != 0) {
        river_voice_capture_close(capture);
        return RIVER_ERR_UNSUPPORTED;
    }

    {
        uint32_t ring_frames;

        ring_frames = (RIVER_VOICE_CAPTURE_RING_BUF_MS + capture->frame_ms - 1U) / capture->frame_ms;
        if (ring_frames == 0U) {
            ring_frames = 1U;
        }
        if (river_capture_prepare_persistent_buffers(capture->frame_bytes,
                                                    ring_frames) != RIVER_OK) {
            river_voice_capture_close(capture);
            return RIVER_ERR_NO_MEMORY;
        }
    }
    
    g_river_cap_internal.running = true;
    g_river_cap_internal.active_capture = capture;

    if (rtos_task_create(&g_river_cap_internal.thread, "river_cap_drv", river_voice_capture_thread, 
                         capture, RIVER_VOICE_CAPTURE_THREAD_STACK, RIVER_VOICE_CAPTURE_THREAD_PRIO) != RTK_SUCCESS) {
        river_voice_capture_close(capture);
        return RIVER_ERR_IO;
    }

    capture->started = 1;
    return RIVER_OK;
}

int32_t river_voice_capture_read(river_voice_capture_t *capture, void *buffer, size_t bytes)
{
    if (capture == 0 || buffer == 0 || !g_river_cap_internal.running) {
        return -1;
    }

    if (bytes != capture->frame_bytes) {
        RIVER_LOGE("capture read expects fixed frame: req=%luB frame=%luB",
                   (unsigned long)bytes,
                   (unsigned long)capture->frame_bytes);
        return -1;
    }

    return river_capture_frame_queue_read(&g_river_cap_internal.queue, (uint8_t *)buffer);
}

void river_voice_capture_close(river_voice_capture_t *capture)
{
    if (capture == 0) {
        return;
    }

    g_river_cap_internal.running = false;
    if (g_river_cap_internal.queue.initialized) {
        rtos_sema_give(g_river_cap_internal.queue.ready);
    }
    rtos_time_delay_ms(100);
    g_river_cap_internal.active_capture = 0;

    if (capture->record != 0) {
        if (capture->started) {
            AudioRecord_Stop((struct AudioRecord *)capture->record);
            capture->started = 0;
        }
        AudioRecord_Destroy((struct AudioRecord *)capture->record);
        capture->record = 0;
    }
}

void river_voice_capture_get_stats(river_voice_capture_stats_t *stats)
{
    river_voice_capture_t *capture;

    if (stats == NULL) {
        return;
    }

    memset(stats, 0, sizeof(*stats));
    stats->running = g_river_cap_internal.running;
    capture = g_river_cap_internal.active_capture;
    if (capture != NULL) {
        stats->frame_bytes = capture->frame_bytes;
        stats->sample_rate = capture->sample_rate;
        stats->channels = capture->channels;
        stats->frame_ms = capture->frame_ms;
    }
    if (!g_river_cap_internal.queue.initialized) {
        return;
    }

    stats->queue_frames = river_audio_frame_ring_count(&g_river_cap_internal.queue.ring);
    stats->queue_peak_frames = river_audio_frame_ring_peak_count(&g_river_cap_internal.queue.ring);
    stats->queue_capacity_frames = g_river_cap_internal.queue.frame_capacity;
    stats->dropped_frames = g_river_cap_internal.queue.dropped_frames;
    stats->read_ok = g_river_cap_internal.queue.read_ok;
    stats->read_wait_timeout = g_river_cap_internal.queue.read_wait_timeout;
}

void river_voice_capture_dump_profile(void)
{
    const river_voice_board_array_profile_t *profile;
    const river_voice_profile_config_t *voice_profile;

    profile = river_voice_board_array_profile();
    voice_profile = river_voice_profile_active();
    RIVER_LOGI("capture profile: %lu Hz, %lums, %luch, %s+%s%s",
               (unsigned long)profile->sample_rate,
               (unsigned long)profile->frame_ms,
               (unsigned long)voice_profile->capture_channels,
               river_voice_board_mic_name(profile->primary_mic),
               river_voice_board_mic_name(profile->secondary_mic),
               voice_profile->uses_native_capture_ref ? "+REF(native ch3)" : "");
}

void river_voice_capture_dump_status(void)
{
    river_voice_capture_stats_t stats;

    river_voice_capture_get_stats(&stats);
    RIVER_LOGI("capture_service=%s frame=%luB %luHz/%luch/%lums queue=%lu/%lu peak=%lu dropped=%lu reads=%lu wait_to=%lu",
               stats.running ? "running" : "stopped",
               (unsigned long)stats.frame_bytes,
               (unsigned long)stats.sample_rate,
               (unsigned long)stats.channels,
               (unsigned long)stats.frame_ms,
               (unsigned long)stats.queue_frames,
               (unsigned long)stats.queue_capacity_frames,
               (unsigned long)stats.queue_peak_frames,
               (unsigned long)stats.dropped_frames,
               (unsigned long)stats.read_ok,
               (unsigned long)stats.read_wait_timeout);
}
