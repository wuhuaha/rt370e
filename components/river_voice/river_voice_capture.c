/* 原始采集实现：负责双麦 AudioRecord 打开、读取和统计。 */
#include <stdbool.h>
#include <stdint.h>
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

static bool river_voice_capture_mic_is_amic(uint32_t mic_category)
{
    switch (mic_category) {
    case AUDIO_AMIC1:
    case AUDIO_AMIC2:
    case AUDIO_AMIC3:
    case AUDIO_AMIC4:
    case AUDIO_AMIC5:
        return true;
    default:
        return false;
    }
}

static void river_voice_capture_apply_board_mics(const river_voice_board_array_profile_t *profile)
{
    if (profile == NULL) {
        return;
    }

    AudioControl_SetMicUsage(profile->capture_usage);
    AudioControl_SetChannelMicCategory(0, profile->primary_mic);
    if (river_voice_capture_mic_is_amic(profile->primary_mic)) {
        AudioControl_SetMicBstGain(profile->primary_mic, profile->primary_mic_gain);
    }
    if (profile->capture_channels > 1U) {
        AudioControl_SetChannelMicCategory(1, profile->secondary_mic);
        if (river_voice_capture_mic_is_amic(profile->secondary_mic)) {
            AudioControl_SetMicBstGain(profile->secondary_mic, profile->secondary_mic_gain);
        }
    }
    RIVER_LOGI("capture board mics applied: usage=%s ch0=%s ch1=%s",
               river_voice_board_capture_usage_name(profile->capture_usage),
               river_voice_board_mic_name(profile->primary_mic),
               river_voice_board_mic_name(profile->secondary_mic));
}

#if defined(CONFIG_RIVER_VOICE_CAPTURE_PATH_SWEEP_EN)

#ifndef CONFIG_RIVER_VOICE_CAPTURE_PATH_SWEEP_FRAMES
#define CONFIG_RIVER_VOICE_CAPTURE_PATH_SWEEP_FRAMES 96
#endif

#ifndef CONFIG_RIVER_VOICE_CAPTURE_PATH_SWEEP_WARMUP_FRAMES
#define CONFIG_RIVER_VOICE_CAPTURE_PATH_SWEEP_WARMUP_FRAMES 8
#endif

#ifndef CONFIG_RIVER_VOICE_CAPTURE_PATH_SWEEP_START_DELAY_MS
#define CONFIG_RIVER_VOICE_CAPTURE_PATH_SWEEP_START_DELAY_MS 0
#endif

#define RIVER_VOICE_CAPTURE_SWEEP_POLL_DELAY_MS          4U
#define RIVER_VOICE_CAPTURE_SWEEP_MAX_POLLS_PER_READ     8U
#define RIVER_VOICE_CAPTURE_SWEEP_CLOSE_DELAY_MS         80U

typedef struct {
    const char *name;
    uint32_t primary_mic;
    uint32_t secondary_mic;
} river_voice_capture_sweep_candidate_t;

typedef struct {
    bool valid;
    const char *name;
    uint32_t primary_mic;
    uint32_t secondary_mic;
    size_t frame_bytes;
    uint32_t measured_reads;
    uint32_t measure_frames;
    uint32_t polls;
    uint32_t max_polls;
    uint32_t warmup_frames;
    uint32_t first_peak0;
    uint32_t first_peak1;
    uint32_t peak0;
    uint32_t peak1;
    uint32_t nonzero_frames;
    uint32_t short_reads;
    uint32_t timeouts;
    uint32_t errors;
    river_status_t status;
} river_voice_capture_sweep_result_t;

static const river_voice_capture_sweep_candidate_t g_river_voice_capture_sweep_candidates[] = {
    {"pdm-2mic-pa-data0", AUDIO_DMIC1, AUDIO_DMIC2},
    {"pdm-2mic-pa-data1", AUDIO_DMIC3, AUDIO_DMIC4},
    {"pdm-2mic-pa-data2", AUDIO_DMIC5, AUDIO_DMIC6},
    {"pdm-2mic-pa-data3", AUDIO_DMIC7, AUDIO_DMIC8},
};

static river_voice_capture_sweep_result_t g_river_voice_capture_sweep_results[
    sizeof(g_river_voice_capture_sweep_candidates) /
    sizeof(g_river_voice_capture_sweep_candidates[0])];

static uint32_t river_voice_capture_abs_i16(int16_t value)
{
    int32_t wide = (int32_t)value;

    if (wide < 0) {
        wide = -wide;
    }
    return (uint32_t)wide;
}

static void river_voice_capture_frame_peak(const uint8_t *buffer,
                                           size_t bytes,
                                           uint32_t channels,
                                           uint32_t *peak0,
                                           uint32_t *peak1)
{
    const int16_t *samples = (const int16_t *)buffer;
    size_t sample_count;
    size_t frame_count;
    size_t i;

    if (buffer == NULL || peak0 == NULL || peak1 == NULL || channels == 0U) {
        return;
    }

    sample_count = bytes / sizeof(int16_t);
    frame_count = sample_count / channels;
    for (i = 0U; i < frame_count; i++) {
        uint32_t v0;
        uint32_t v1 = 0U;

        v0 = river_voice_capture_abs_i16(samples[(i * channels) + 0U]);
        if (channels > 1U) {
            v1 = river_voice_capture_abs_i16(samples[(i * channels) + 1U]);
        }
        if (v0 > *peak0) {
            *peak0 = v0;
        }
        if (v1 > *peak1) {
            *peak1 = v1;
        }
    }
}

static river_status_t river_voice_capture_sweep_one(
    const river_voice_capture_sweep_candidate_t *candidate,
    size_t candidate_index,
    const river_voice_board_array_profile_t *base_profile,
    const river_voice_profile_config_t *voice_profile)
{
    river_voice_board_array_profile_t sweep_profile;
    AudioRecordConfig record_config;
    struct AudioRecord *record = NULL;
    uint8_t *buffer = NULL;
    size_t frame_bytes;
    uint32_t frame_samples;
    uint32_t warmup_frames = (uint32_t)CONFIG_RIVER_VOICE_CAPTURE_PATH_SWEEP_WARMUP_FRAMES;
    uint32_t measure_frames = (uint32_t)CONFIG_RIVER_VOICE_CAPTURE_PATH_SWEEP_FRAMES;
    uint32_t target_reads = 0U;
    uint32_t max_polls = 0U;
    uint32_t polls = 0U;
    uint32_t reads = 0U;
    uint32_t measured_reads = 0U;
    uint32_t nonzero_frames = 0U;
    uint32_t first_peak0 = 0U;
    uint32_t first_peak1 = 0U;
    uint32_t peak0 = 0U;
    uint32_t peak1 = 0U;
    uint32_t short_reads = 0U;
    uint32_t timeouts = 0U;
    uint32_t errors = 0U;
    int32_t init_ret;
    int32_t start_ret;
    int32_t params_ret;
    bool started = false;
    river_status_t status = RIVER_OK;
    river_voice_capture_sweep_result_t *result = NULL;

    if (candidate == NULL || base_profile == NULL || voice_profile == NULL) {
        return RIVER_ERR_ARG;
    }
    if (candidate_index < (sizeof(g_river_voice_capture_sweep_results) /
                           sizeof(g_river_voice_capture_sweep_results[0]))) {
        result = &g_river_voice_capture_sweep_results[candidate_index];
        memset(result, 0, sizeof(*result));
        result->name = candidate->name;
        result->primary_mic = candidate->primary_mic;
        result->secondary_mic = candidate->secondary_mic;
    }

    sweep_profile = *base_profile;
    sweep_profile.geometry_name = candidate->name;
    sweep_profile.primary_mic = candidate->primary_mic;
    sweep_profile.secondary_mic = candidate->secondary_mic;
    sweep_profile.aux_mic = candidate->primary_mic;
    sweep_profile.capture_usage = AUDIO_CAPTURE_USAGE_DMIC;
    sweep_profile.capture_channels = 2U;

    frame_samples = (sweep_profile.sample_rate * sweep_profile.frame_ms) / 1000U;
    frame_bytes = (size_t)frame_samples * (size_t)sweep_profile.capture_channels * sizeof(int16_t);
    if (frame_bytes == 0U) {
        errors++;
        status = RIVER_ERR_ARG;
        goto out;
    }

    buffer = (uint8_t *)rtos_mem_zmalloc((uint32_t)frame_bytes);
    if (buffer == NULL) {
        RIVER_LOGE("capture path sweep alloc failed: path=%s frame=%luB",
                   candidate->name,
                   (unsigned long)frame_bytes);
        errors++;
        status = RIVER_ERR_NO_MEMORY;
        goto out;
    }

    record = AudioRecord_Create();
    if (record == NULL) {
        RIVER_LOGE("capture path sweep create failed: path=%s", candidate->name);
        errors++;
        status = RIVER_ERR_UNSUPPORTED;
        goto out;
    }

    river_voice_capture_apply_board_mics(&sweep_profile);
    river_voice_board_apply_capture_pinmux();

    memset(&record_config, 0, sizeof(record_config));
    record_config.sample_rate = sweep_profile.sample_rate;
    record_config.channel_count = sweep_profile.capture_channels;
    record_config.format = AUDIO_FORMAT_PCM_16_BIT;
    record_config.device = DEVICE_IN_DMIC_REF_AMIC;
    record_config.buffer_bytes = (uint32_t)frame_bytes * 4U;

    init_ret = AudioRecord_Init(record, &record_config, AUDIO_INPUT_FLAG_NONE);
    if (init_ret != 0) {
        RIVER_LOGE("capture path sweep init failed: path=%s ret=%ld",
                   candidate->name,
                   (long)init_ret);
        errors++;
        status = RIVER_ERR_UNSUPPORTED;
        goto out;
    }

    river_voice_capture_apply_board_mics(&sweep_profile);
    river_voice_board_apply_capture_pinmux();

    start_ret = AudioRecord_Start(record);
    if (start_ret != 0) {
        RIVER_LOGE("capture path sweep start failed: path=%s ret=%ld",
                   candidate->name,
                   (long)start_ret);
        errors++;
        status = RIVER_ERR_UNSUPPORTED;
        goto out;
    }
    started = true;

    river_voice_capture_apply_board_mics(&sweep_profile);
    river_voice_board_apply_capture_pinmux();

    params_ret = AudioRecord_SetParameters(record, voice_profile->capture_audio_record_params);
    RIVER_LOGI("capture path sweep params: path=%s ret=%ld params=%s",
               candidate->name,
               (long)params_ret,
               voice_profile->capture_audio_record_params);
    if (params_ret != 0) {
        errors++;
        status = RIVER_ERR_UNSUPPORTED;
        goto out;
    }

    target_reads = warmup_frames + measure_frames;
    max_polls = target_reads * RIVER_VOICE_CAPTURE_SWEEP_MAX_POLLS_PER_READ;
    if (max_polls < target_reads) {
        max_polls = target_reads;
    }
    while (reads < target_reads && polls < max_polls) {
        int32_t ret;
        uint32_t frame_peak0 = 0U;
        uint32_t frame_peak1 = 0U;

        polls++;
        ret = AudioRecord_Read(record, buffer, frame_bytes, false);
        if (ret <= 0) {
            if (ret < 0) {
                timeouts++;
            } else {
                errors++;
            }
            rtos_time_delay_ms(RIVER_VOICE_CAPTURE_SWEEP_POLL_DELAY_MS);
            continue;
        }

        reads++;
        if ((size_t)ret < frame_bytes) {
            short_reads++;
        }
        river_voice_capture_frame_peak(buffer,
                                       (size_t)ret,
                                       sweep_profile.capture_channels,
                                       &frame_peak0,
                                       &frame_peak1);
        if (reads == 1U) {
            first_peak0 = frame_peak0;
            first_peak1 = frame_peak1;
        }
        if (reads <= warmup_frames) {
            continue;
        }

        measured_reads++;
        if (frame_peak0 != 0U || frame_peak1 != 0U) {
            nonzero_frames++;
        }
        if (frame_peak0 > peak0) {
            peak0 = frame_peak0;
        }
        if (frame_peak1 > peak1) {
            peak1 = frame_peak1;
        }
    }

out:
    RIVER_LOGI("capture path sweep result: path=%s pair=%s/%s status=%ld frame=%luB read=%lu/%lu poll=%lu/%lu warmup=%lu first_peak=%lu/%lu peak=%lu/%lu nonzero=%lu short=%lu timeout=%lu err=%lu",
               candidate->name,
               river_voice_board_mic_name(candidate->primary_mic),
               river_voice_board_mic_name(candidate->secondary_mic),
               (long)status,
               (unsigned long)frame_bytes,
               (unsigned long)measured_reads,
               (unsigned long)measure_frames,
               (unsigned long)polls,
               (unsigned long)max_polls,
               (unsigned long)warmup_frames,
               (unsigned long)first_peak0,
               (unsigned long)first_peak1,
               (unsigned long)peak0,
               (unsigned long)peak1,
               (unsigned long)nonzero_frames,
               (unsigned long)short_reads,
               (unsigned long)timeouts,
               (unsigned long)errors);

    if (result != NULL) {
        result->valid = true;
        result->frame_bytes = frame_bytes;
        result->measured_reads = measured_reads;
        result->measure_frames = measure_frames;
        result->polls = polls;
        result->max_polls = max_polls;
        result->warmup_frames = warmup_frames;
        result->first_peak0 = first_peak0;
        result->first_peak1 = first_peak1;
        result->peak0 = peak0;
        result->peak1 = peak1;
        result->nonzero_frames = nonzero_frames;
        result->short_reads = short_reads;
        result->timeouts = timeouts;
        result->errors = errors;
        result->status = status;
    }

    if (started) {
        AudioRecord_Stop(record);
    }
    if (record != NULL) {
        AudioRecord_Destroy(record);
    }
    if (buffer != NULL) {
        rtos_mem_free(buffer);
    }
    rtos_time_delay_ms(RIVER_VOICE_CAPTURE_SWEEP_CLOSE_DELAY_MS);
    return status;
}

void river_voice_capture_run_path_sweep(void)
{
    const river_voice_board_array_profile_t *base_profile;
    const river_voice_profile_config_t *voice_profile;
    size_t i;
    static bool s_sweep_done;

    if (s_sweep_done) {
        return;
    }
    s_sweep_done = true;

    base_profile = river_voice_board_array_profile();
    voice_profile = river_voice_profile_active();
    if (CONFIG_RIVER_VOICE_CAPTURE_PATH_SWEEP_START_DELAY_MS > 0) {
        RIVER_LOGW("temporary capture path sweep delay: %lums",
                   (unsigned long)CONFIG_RIVER_VOICE_CAPTURE_PATH_SWEEP_START_DELAY_MS);
        rtos_time_delay_ms((uint32_t)CONFIG_RIVER_VOICE_CAPTURE_PATH_SWEEP_START_DELAY_MS);
    }

    RIVER_LOGW("temporary capture path sweep start: candidates=%lu warmup=%lu measure=%lu params=%s",
               (unsigned long)(sizeof(g_river_voice_capture_sweep_candidates) /
                               sizeof(g_river_voice_capture_sweep_candidates[0])),
               (unsigned long)CONFIG_RIVER_VOICE_CAPTURE_PATH_SWEEP_WARMUP_FRAMES,
               (unsigned long)CONFIG_RIVER_VOICE_CAPTURE_PATH_SWEEP_FRAMES,
               voice_profile->capture_audio_record_params);

    for (i = 0U;
         i < (sizeof(g_river_voice_capture_sweep_candidates) /
              sizeof(g_river_voice_capture_sweep_candidates[0]));
         i++) {
        (void)river_voice_capture_sweep_one(&g_river_voice_capture_sweep_candidates[i],
                                            i,
                                            base_profile,
                                            voice_profile);
    }

    RIVER_LOGW("temporary capture path sweep done; normal capture will open next");
}

void river_voice_capture_dump_path_sweep_results(void)
{
    size_t i;

    for (i = 0U;
         i < (sizeof(g_river_voice_capture_sweep_results) /
              sizeof(g_river_voice_capture_sweep_results[0]));
         i++) {
        const river_voice_capture_sweep_result_t *result =
            &g_river_voice_capture_sweep_results[i];

        if (!result->valid) {
            continue;
        }
        RIVER_LOGI("capture path sweep replay: path=%s pair=%s/%s status=%ld frame=%luB read=%lu/%lu poll=%lu/%lu warmup=%lu first_peak=%lu/%lu peak=%lu/%lu nonzero=%lu short=%lu timeout=%lu err=%lu",
                   result->name,
                   river_voice_board_mic_name(result->primary_mic),
                   river_voice_board_mic_name(result->secondary_mic),
                   (long)result->status,
                   (unsigned long)result->frame_bytes,
                   (unsigned long)result->measured_reads,
                   (unsigned long)result->measure_frames,
                   (unsigned long)result->polls,
                   (unsigned long)result->max_polls,
                   (unsigned long)result->warmup_frames,
                   (unsigned long)result->first_peak0,
                   (unsigned long)result->first_peak1,
                   (unsigned long)result->peak0,
                   (unsigned long)result->peak1,
                   (unsigned long)result->nonzero_frames,
                   (unsigned long)result->short_reads,
                   (unsigned long)result->timeouts,
                   (unsigned long)result->errors);
    }
}

#else

void river_voice_capture_run_path_sweep(void)
{
}

void river_voice_capture_dump_path_sweep_results(void)
{
}

#endif

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

    capture->record = AudioRecord_Create();
    if (capture->record == 0) {
        return RIVER_ERR_UNSUPPORTED;
    }

    river_voice_capture_apply_board_mics(profile);
    river_voice_board_apply_capture_pinmux();

    record_config.sample_rate = capture->sample_rate;
    record_config.channel_count = capture->channels;
    record_config.format = AUDIO_FORMAT_PCM_16_BIT;
    record_config.device = DEVICE_IN_DMIC_REF_AMIC;
    record_config.buffer_bytes = (uint32_t)capture->frame_bytes * 4U;
    
    if (AudioRecord_Init((struct AudioRecord *)capture->record, &record_config, AUDIO_INPUT_FLAG_NONE) != 0) {
        river_voice_capture_close(capture);
        return RIVER_ERR_UNSUPPORTED;
    }

    river_voice_capture_apply_board_mics(profile);
    river_voice_board_apply_capture_pinmux();

    if (AudioRecord_Start((struct AudioRecord *)capture->record) != 0) {
        river_voice_capture_close(capture);
        return RIVER_ERR_UNSUPPORTED;
    }
    capture->started = 1;

    river_voice_capture_apply_board_mics(profile);
    river_voice_board_apply_capture_pinmux();

    {
        int32_t params_ret;

        params_ret = AudioRecord_SetParameters((struct AudioRecord *)capture->record,
                                               voice_profile->capture_audio_record_params);
        RIVER_LOGI("capture params applied: ret=%ld params=%s",
                   (long)params_ret,
                   voice_profile->capture_audio_record_params);
        if (params_ret != 0) {
            river_voice_capture_close(capture);
            return RIVER_ERR_UNSUPPORTED;
        }
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
    RIVER_LOGI("capture profile: %lu Hz, %lums, %luch, usage=%s, %s+%s%s",
               (unsigned long)profile->sample_rate,
               (unsigned long)profile->frame_ms,
               (unsigned long)voice_profile->capture_channels,
               river_voice_board_capture_usage_name(profile->capture_usage),
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
