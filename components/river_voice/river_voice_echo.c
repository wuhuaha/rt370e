#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "basic_types.h"
#include "os_wrapper.h"

#include "audio/audio_control.h"
#include "audio/audio_record.h"
#include "audio/audio_service.h"
#include "audio/audio_track.h"

#include "river/river_voice.h"

#define RIVER_VOICE_ECHO_SAMPLE_RATE       16000U
#define RIVER_VOICE_ECHO_CHANNELS          2U
#define RIVER_VOICE_ECHO_BYTES_PER_SAMPLE  2U
#define RIVER_VOICE_ECHO_FRAME_MS          20U
#define RIVER_VOICE_ECHO_DELAY_MS          1000U
#define RIVER_VOICE_ECHO_WARMUP_MS         100U
#define RIVER_VOICE_ECHO_TASK_STACK        (1024U * 12U)
#define RIVER_VOICE_ECHO_TASK_PRIORITY     4U
#define RIVER_VOICE_ECHO_CAPTURE_VOLUME    0x30U

typedef struct {
    bool running;
    bool stop_requested;
    bool record_started;
    bool track_started;
    rtos_task_t task;
    struct AudioRecord *record;
    struct AudioTrack *track;
    uint8_t *delay_buffer;
    uint8_t *capture_buffer;
    uint8_t *playback_buffer;
    size_t chunk_bytes;
    size_t delay_buffer_bytes;
    size_t read_offset;
    size_t write_offset;
    uint32_t warmup_bytes_remaining;
} river_voice_echo_context_t;

static river_voice_echo_context_t g_river_voice_echo;

static size_t river_voice_min_size(size_t left, size_t right)
{
    return left < right ? left : right;
}

static void river_voice_echo_ring_read(void *dst, size_t bytes)
{
    size_t first_copy;

    first_copy = river_voice_min_size(bytes, g_river_voice_echo.delay_buffer_bytes - g_river_voice_echo.read_offset);
    memcpy(dst, g_river_voice_echo.delay_buffer + g_river_voice_echo.read_offset, first_copy);
    if (bytes > first_copy) {
        memcpy((uint8_t *)dst + first_copy, g_river_voice_echo.delay_buffer, bytes - first_copy);
    }

    g_river_voice_echo.read_offset = (g_river_voice_echo.read_offset + bytes) % g_river_voice_echo.delay_buffer_bytes;
}

static void river_voice_echo_ring_write(const void *src, size_t bytes)
{
    size_t first_copy;

    first_copy = river_voice_min_size(bytes, g_river_voice_echo.delay_buffer_bytes - g_river_voice_echo.write_offset);
    memcpy(g_river_voice_echo.delay_buffer + g_river_voice_echo.write_offset, src, first_copy);
    if (bytes > first_copy) {
        memcpy(g_river_voice_echo.delay_buffer, (const uint8_t *)src + first_copy, bytes - first_copy);
    }

    g_river_voice_echo.write_offset = (g_river_voice_echo.write_offset + bytes) % g_river_voice_echo.delay_buffer_bytes;
}

static void river_voice_echo_apply_warmup(uint8_t *buffer, size_t bytes)
{
    size_t mute_bytes;

    if (g_river_voice_echo.warmup_bytes_remaining == 0U) {
        return;
    }

    mute_bytes = river_voice_min_size(bytes, (size_t)g_river_voice_echo.warmup_bytes_remaining);
    memset(buffer, 0, mute_bytes);
    g_river_voice_echo.warmup_bytes_remaining -= (uint32_t)mute_bytes;
}

static void river_voice_echo_close_audio(void)
{
    if (g_river_voice_echo.track != 0) {
        if (g_river_voice_echo.track_started) {
            AudioTrack_Pause(g_river_voice_echo.track);
            AudioTrack_Flush(g_river_voice_echo.track);
            AudioTrack_Stop(g_river_voice_echo.track);
            g_river_voice_echo.track_started = false;
        }
        AudioTrack_Destroy(g_river_voice_echo.track);
        g_river_voice_echo.track = 0;
    }

    if (g_river_voice_echo.record != 0) {
        if (g_river_voice_echo.record_started) {
            AudioRecord_Stop(g_river_voice_echo.record);
            g_river_voice_echo.record_started = false;
        }
        AudioRecord_Destroy(g_river_voice_echo.record);
        g_river_voice_echo.record = 0;
    }
}

static void river_voice_echo_release_buffers(void)
{
    if (g_river_voice_echo.playback_buffer != 0) {
        rtos_mem_free(g_river_voice_echo.playback_buffer);
        g_river_voice_echo.playback_buffer = 0;
    }

    if (g_river_voice_echo.capture_buffer != 0) {
        rtos_mem_free(g_river_voice_echo.capture_buffer);
        g_river_voice_echo.capture_buffer = 0;
    }

    if (g_river_voice_echo.delay_buffer != 0) {
        rtos_mem_free(g_river_voice_echo.delay_buffer);
        g_river_voice_echo.delay_buffer = 0;
    }

    g_river_voice_echo.chunk_bytes = 0U;
    g_river_voice_echo.delay_buffer_bytes = 0U;
    g_river_voice_echo.read_offset = 0U;
    g_river_voice_echo.write_offset = 0U;
    g_river_voice_echo.warmup_bytes_remaining = 0U;
}

static river_status_t river_voice_echo_prepare_buffers(void)
{
    size_t bytes_per_second;

    bytes_per_second = RIVER_VOICE_ECHO_SAMPLE_RATE * RIVER_VOICE_ECHO_CHANNELS * RIVER_VOICE_ECHO_BYTES_PER_SAMPLE;
    g_river_voice_echo.chunk_bytes = (bytes_per_second * RIVER_VOICE_ECHO_FRAME_MS) / 1000U;
    g_river_voice_echo.delay_buffer_bytes = (bytes_per_second * RIVER_VOICE_ECHO_DELAY_MS) / 1000U;
    g_river_voice_echo.warmup_bytes_remaining = (uint32_t)((bytes_per_second * RIVER_VOICE_ECHO_WARMUP_MS) / 1000U);

    g_river_voice_echo.delay_buffer = (uint8_t *)rtos_mem_zmalloc((uint32_t)g_river_voice_echo.delay_buffer_bytes);
    g_river_voice_echo.capture_buffer = (uint8_t *)rtos_mem_zmalloc((uint32_t)g_river_voice_echo.chunk_bytes);
    g_river_voice_echo.playback_buffer = (uint8_t *)rtos_mem_zmalloc((uint32_t)g_river_voice_echo.chunk_bytes);

    if (g_river_voice_echo.delay_buffer == 0 ||
        g_river_voice_echo.capture_buffer == 0 ||
        g_river_voice_echo.playback_buffer == 0) {
        river_voice_echo_release_buffers();
        return RIVER_ERR_NO_MEMORY;
    }

    return RIVER_OK;
}

static river_status_t river_voice_echo_open_audio(void)
{
    AudioRecordConfig record_config;
    AudioTrackConfig track_config;
    size_t track_buffer_bytes;

    AudioService_Init();
    AudioControl_SetPlaybackDevice(DEVICE_OUT_SPEAKER);
    AudioControl_SetPlaybackMute(false);
    AudioControl_SetAmplifierMute(false);
    AudioControl_SetHardwareVolume(0.70f, 0.70f);

    AudioControl_SetMicUsage(AUDIO_CAPTURE_USAGE_AMIC);
    AudioControl_SetChannelMicCategory(0, AUDIO_AMIC1);
    AudioControl_SetChannelMicCategory(1, AUDIO_AMIC3);
    AudioControl_SetMicBstGain(AUDIO_AMIC1, AUDIO_MICBST_GAIN_15DB);
    AudioControl_SetMicBstGain(AUDIO_AMIC3, AUDIO_MICBST_GAIN_15DB);
    AudioControl_SetCaptureVolume(RIVER_VOICE_ECHO_CHANNELS, RIVER_VOICE_ECHO_CAPTURE_VOLUME);

    g_river_voice_echo.record = AudioRecord_Create();
    if (g_river_voice_echo.record == 0) {
        printf("[river][voice] create AudioRecord failed\n");
        return RIVER_ERR_UNSUPPORTED;
    }

    record_config.sample_rate = RIVER_VOICE_ECHO_SAMPLE_RATE;
    record_config.channel_count = RIVER_VOICE_ECHO_CHANNELS;
    record_config.format = AUDIO_FORMAT_PCM_16_BIT;
    record_config.device = DEVICE_IN_MIC;
    record_config.buffer_bytes = (uint32_t)g_river_voice_echo.chunk_bytes;
    if (AudioRecord_Init(g_river_voice_echo.record, &record_config, AUDIO_INPUT_FLAG_NONE) != 0) {
        printf("[river][voice] AudioRecord_Init failed\n");
        return RIVER_ERR_UNSUPPORTED;
    }

    AudioRecord_SetParameters(g_river_voice_echo.record, "cap_mode=no_afe_pure_data");

    g_river_voice_echo.track = AudioTrack_Create();
    if (g_river_voice_echo.track == 0) {
        printf("[river][voice] create AudioTrack failed\n");
        return RIVER_ERR_UNSUPPORTED;
    }

    track_buffer_bytes = AudioTrack_GetMinBufferBytes(g_river_voice_echo.track,
                                                      AUDIO_CATEGORY_MEDIA,
                                                      RIVER_VOICE_ECHO_SAMPLE_RATE,
                                                      AUDIO_FORMAT_PCM_16_BIT,
                                                      RIVER_VOICE_ECHO_CHANNELS);
    if (track_buffer_bytes < g_river_voice_echo.chunk_bytes) {
        track_buffer_bytes = g_river_voice_echo.chunk_bytes;
    }
    track_buffer_bytes *= 4U;

    track_config.category_type = AUDIO_CATEGORY_MEDIA;
    track_config.sample_rate = RIVER_VOICE_ECHO_SAMPLE_RATE;
    track_config.format = AUDIO_FORMAT_PCM_16_BIT;
    track_config.channel_count = RIVER_VOICE_ECHO_CHANNELS;
    track_config.buffer_bytes = (uint32_t)track_buffer_bytes;
    if (AudioTrack_Init(g_river_voice_echo.track, &track_config, AUDIO_OUTPUT_FLAG_NONE) != 0) {
        printf("[river][voice] AudioTrack_Init failed\n");
        return RIVER_ERR_UNSUPPORTED;
    }

    AudioTrack_SetStartThresholdBytes(g_river_voice_echo.track, (int32_t)track_buffer_bytes);

    if (AudioRecord_Start(g_river_voice_echo.record) != 0) {
        printf("[river][voice] AudioRecord_Start failed\n");
        return RIVER_ERR_UNSUPPORTED;
    }
    g_river_voice_echo.record_started = true;

    if (AudioTrack_Start(g_river_voice_echo.track) != 0) {
        printf("[river][voice] AudioTrack_Start failed\n");
        return RIVER_ERR_UNSUPPORTED;
    }
    g_river_voice_echo.track_started = true;

    printf("[river][voice] audio echo config: %lu Hz, %lu ch, %lu ms delay, AMIC1+AMIC3 -> speaker\n",
           (unsigned long)RIVER_VOICE_ECHO_SAMPLE_RATE,
           (unsigned long)RIVER_VOICE_ECHO_CHANNELS,
           (unsigned long)RIVER_VOICE_ECHO_DELAY_MS);
    return RIVER_OK;
}

static void river_voice_echo_task(void *param)
{
    (void)param;

    while (!g_river_voice_echo.stop_requested) {
        int32_t bytes_read;

        memset(g_river_voice_echo.capture_buffer, 0, g_river_voice_echo.chunk_bytes);
        bytes_read = AudioRecord_Read(g_river_voice_echo.record,
                                      g_river_voice_echo.capture_buffer,
                                      g_river_voice_echo.chunk_bytes,
                                      true);
        if (bytes_read < 0) {
            printf("[river][voice] AudioRecord_Read failed: %ld\n", (long)bytes_read);
            rtos_time_delay_ms(RIVER_VOICE_ECHO_FRAME_MS);
            continue;
        }

        river_voice_echo_apply_warmup(g_river_voice_echo.capture_buffer, g_river_voice_echo.chunk_bytes);
        river_voice_echo_ring_read(g_river_voice_echo.playback_buffer, g_river_voice_echo.chunk_bytes);
        river_voice_echo_ring_write(g_river_voice_echo.capture_buffer, g_river_voice_echo.chunk_bytes);

        if (AudioTrack_Write(g_river_voice_echo.track,
                             g_river_voice_echo.playback_buffer,
                             g_river_voice_echo.chunk_bytes,
                             true) < 0) {
            printf("[river][voice] AudioTrack_Write failed\n");
            break;
        }
    }

    river_voice_echo_close_audio();
    river_voice_echo_release_buffers();
    g_river_voice_echo.stop_requested = false;
    g_river_voice_echo.running = false;
    g_river_voice_echo.task = 0;
    printf("[river][voice] audio echo stopped\n");
    rtos_task_delete(NULL);
}

river_status_t river_voice_echo_start(void)
{
    river_status_t status;

    if (g_river_voice_echo.running) {
        printf("[river][voice] audio echo already running\n");
        return RIVER_OK;
    }

    memset(&g_river_voice_echo, 0, sizeof(g_river_voice_echo));

    status = river_voice_echo_prepare_buffers();
    if (status != RIVER_OK) {
        printf("[river][voice] allocate echo buffers failed\n");
        return status;
    }

    status = river_voice_echo_open_audio();
    if (status != RIVER_OK) {
        river_voice_echo_close_audio();
        river_voice_echo_release_buffers();
        return status;
    }

    g_river_voice_echo.running = true;
    if (rtos_task_create(&g_river_voice_echo.task,
                         "river_audio_echo",
                         river_voice_echo_task,
                         0,
                         RIVER_VOICE_ECHO_TASK_STACK,
                         RIVER_VOICE_ECHO_TASK_PRIORITY) != RTK_SUCCESS) {
        printf("[river][voice] create echo task failed\n");
        g_river_voice_echo.running = false;
        river_voice_echo_close_audio();
        river_voice_echo_release_buffers();
        return RIVER_ERR_NO_MEMORY;
    }

    printf("[river][voice] audio echo started\n");
    return RIVER_OK;
}

river_status_t river_voice_echo_stop(void)
{
    uint32_t wait_count;

    if (!g_river_voice_echo.running) {
        printf("[river][voice] audio echo already stopped\n");
        return RIVER_OK;
    }

    g_river_voice_echo.stop_requested = true;
    for (wait_count = 0; wait_count < 100U; ++wait_count) {
        if (!g_river_voice_echo.running) {
            return RIVER_OK;
        }
        rtos_time_delay_ms(20U);
    }

    printf("[river][voice] audio echo stop timeout\n");
    return RIVER_ERR_BUSY;
}

bool river_voice_echo_is_running(void)
{
    return g_river_voice_echo.running;
}

const char *river_voice_echo_status_name(void)
{
    return river_voice_echo_is_running() ? "running" : "stopped";
}

void river_voice_echo_dump_status(void)
{
    printf("[river] audio_echo=%s\n", river_voice_echo_status_name());
    if (river_voice_echo_is_running()) {
        printf("[river] audio_echo_profile=%luHz/%luch/%lums\n",
               (unsigned long)RIVER_VOICE_ECHO_SAMPLE_RATE,
               (unsigned long)RIVER_VOICE_ECHO_CHANNELS,
               (unsigned long)RIVER_VOICE_ECHO_DELAY_MS);
    }
}
