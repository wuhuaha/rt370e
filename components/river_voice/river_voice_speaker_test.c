#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "basic_types.h"
#include "os_wrapper.h"

#include "audio/audio_control.h"
#include "audio/audio_service.h"
#include "audio/audio_track.h"

#include "river/river_voice.h"

#define RIVER_VOICE_SPK_TEST_SAMPLE_RATE       16000U
#define RIVER_VOICE_SPK_TEST_CHANNELS          2U
#define RIVER_VOICE_SPK_TEST_BYTES_PER_SAMPLE  2U
#define RIVER_VOICE_SPK_TEST_FRAME_MS          20U
#define RIVER_VOICE_SPK_TEST_TASK_STACK        (1024U * 8U)
#define RIVER_VOICE_SPK_TEST_TASK_PRIORITY     4U
#define RIVER_VOICE_SPK_TEST_HW_VOLUME         0.20f
#define RIVER_VOICE_SPK_TEST_SW_VOLUME         1.00f
#define RIVER_VOICE_SPK_TEST_AMPLITUDE         6000
#define RIVER_VOICE_SPK_TEST_DIAG_WINDOW_MS    1000U

typedef struct {
    uint32_t frequency_hz;
    uint32_t chunk_count;
    const char *name;
} river_voice_speaker_test_segment_t;

typedef struct {
    bool running;
    bool stop_requested;
    bool track_started;
    bool diag_enabled;
    rtos_task_t task;
    struct AudioTrack *track;
    uint8_t *buffer;
    size_t chunk_bytes;
    uint32_t frames_per_chunk;
    uint32_t phase_q32;
    uint32_t segment_index;
    uint32_t chunk_index_in_segment;
    uint32_t current_frequency_hz;
    const char *current_segment_name;
    uint16_t last_peak;
    uint32_t diag_write_ok;
    uint32_t diag_write_fail;
    uint32_t diag_chunks_until_log;
} river_voice_speaker_test_context_t;

static const river_voice_speaker_test_segment_t k_river_voice_speaker_test_pattern[] = {
    {1000U, 20U, "tone_a"},
    {0U,    10U, "gap_a"},
    {1500U, 20U, "tone_b"},
    {0U,    50U, "gap_b"},
};

static river_voice_speaker_test_context_t g_river_voice_speaker_test;
static bool g_river_voice_speaker_test_diag_enabled;

static void river_voice_speaker_test_reset_diag_counters(void)
{
    g_river_voice_speaker_test.diag_write_ok = 0U;
    g_river_voice_speaker_test.diag_write_fail = 0U;
    g_river_voice_speaker_test.diag_chunks_until_log = RIVER_VOICE_SPK_TEST_DIAG_WINDOW_MS / RIVER_VOICE_SPK_TEST_FRAME_MS;
}

static void river_voice_speaker_test_log_diagnostics_if_needed(void)
{
    if (!g_river_voice_speaker_test.diag_enabled) {
        return;
    }

    if (g_river_voice_speaker_test.diag_chunks_until_log > 0U) {
        g_river_voice_speaker_test.diag_chunks_until_log--;
    }
    if (g_river_voice_speaker_test.diag_chunks_until_log > 0U) {
        return;
    }

    printf("[river][voice][spk] segment=%s freq=%luHz peak=%u write_ok=%lu write_fail=%lu\n",
           g_river_voice_speaker_test.current_segment_name != 0 ? g_river_voice_speaker_test.current_segment_name : "n/a",
           (unsigned long)g_river_voice_speaker_test.current_frequency_hz,
           (unsigned int)g_river_voice_speaker_test.last_peak,
           (unsigned long)g_river_voice_speaker_test.diag_write_ok,
           (unsigned long)g_river_voice_speaker_test.diag_write_fail);

    river_voice_speaker_test_reset_diag_counters();
}

static void river_voice_speaker_test_release_buffer(void)
{
    if (g_river_voice_speaker_test.buffer != 0) {
        rtos_mem_free(g_river_voice_speaker_test.buffer);
        g_river_voice_speaker_test.buffer = 0;
    }

    g_river_voice_speaker_test.chunk_bytes = 0U;
    g_river_voice_speaker_test.frames_per_chunk = 0U;
    g_river_voice_speaker_test.phase_q32 = 0U;
    g_river_voice_speaker_test.segment_index = 0U;
    g_river_voice_speaker_test.chunk_index_in_segment = 0U;
    g_river_voice_speaker_test.current_frequency_hz = 0U;
    g_river_voice_speaker_test.current_segment_name = "idle";
    g_river_voice_speaker_test.last_peak = 0U;
    river_voice_speaker_test_reset_diag_counters();
}

static void river_voice_speaker_test_close_audio(void)
{
    if (g_river_voice_speaker_test.track != 0) {
        if (g_river_voice_speaker_test.track_started) {
            AudioTrack_Pause(g_river_voice_speaker_test.track);
            AudioTrack_Flush(g_river_voice_speaker_test.track);
            AudioTrack_Stop(g_river_voice_speaker_test.track);
            g_river_voice_speaker_test.track_started = false;
        }
        AudioTrack_Destroy(g_river_voice_speaker_test.track);
        g_river_voice_speaker_test.track = 0;
    }
}

static river_status_t river_voice_speaker_test_prepare_buffer(void)
{
    size_t bytes_per_second;

    bytes_per_second = RIVER_VOICE_SPK_TEST_SAMPLE_RATE *
                       RIVER_VOICE_SPK_TEST_CHANNELS *
                       RIVER_VOICE_SPK_TEST_BYTES_PER_SAMPLE;
    g_river_voice_speaker_test.frames_per_chunk = (RIVER_VOICE_SPK_TEST_SAMPLE_RATE * RIVER_VOICE_SPK_TEST_FRAME_MS) / 1000U;
    g_river_voice_speaker_test.chunk_bytes = (bytes_per_second * RIVER_VOICE_SPK_TEST_FRAME_MS) / 1000U;
    g_river_voice_speaker_test.diag_enabled = g_river_voice_speaker_test_diag_enabled;
    g_river_voice_speaker_test.current_segment_name = "idle";

    g_river_voice_speaker_test.buffer = (uint8_t *)rtos_mem_zmalloc((uint32_t)g_river_voice_speaker_test.chunk_bytes);
    if (g_river_voice_speaker_test.buffer == 0) {
        river_voice_speaker_test_release_buffer();
        return RIVER_ERR_NO_MEMORY;
    }

    return RIVER_OK;
}

static river_status_t river_voice_speaker_test_open_audio(void)
{
    AudioTrackConfig track_config;
    size_t track_buffer_bytes;

    AudioService_Init();
    AudioControl_SetPlaybackDevice(DEVICE_OUT_SPEAKER);
    AudioControl_SetPlaybackMute(false);
    AudioControl_SetAmplifierMute(false);
    AudioControl_SetHardwareVolume(RIVER_VOICE_SPK_TEST_HW_VOLUME, RIVER_VOICE_SPK_TEST_HW_VOLUME);

    g_river_voice_speaker_test.track = AudioTrack_Create();
    if (g_river_voice_speaker_test.track == 0) {
        printf("[river][voice] create speaker AudioTrack failed\n");
        return RIVER_ERR_UNSUPPORTED;
    }

    track_buffer_bytes = AudioTrack_GetMinBufferBytes(g_river_voice_speaker_test.track,
                                                      AUDIO_CATEGORY_MEDIA,
                                                      RIVER_VOICE_SPK_TEST_SAMPLE_RATE,
                                                      AUDIO_FORMAT_PCM_16_BIT,
                                                      RIVER_VOICE_SPK_TEST_CHANNELS);
    if (track_buffer_bytes < g_river_voice_speaker_test.chunk_bytes) {
        track_buffer_bytes = g_river_voice_speaker_test.chunk_bytes;
    }
    track_buffer_bytes *= 8U;

    track_config.category_type = AUDIO_CATEGORY_MEDIA;
    track_config.sample_rate = RIVER_VOICE_SPK_TEST_SAMPLE_RATE;
    track_config.format = AUDIO_FORMAT_PCM_16_BIT;
    track_config.channel_count = RIVER_VOICE_SPK_TEST_CHANNELS;
    track_config.buffer_bytes = (uint32_t)track_buffer_bytes;
    if (AudioTrack_Init(g_river_voice_speaker_test.track, &track_config, AUDIO_OUTPUT_FLAG_NONE) != 0) {
        printf("[river][voice] AudioTrack_Init failed for speaker test\n");
        return RIVER_ERR_UNSUPPORTED;
    }

    AudioTrack_SetVolume(g_river_voice_speaker_test.track,
                         RIVER_VOICE_SPK_TEST_SW_VOLUME,
                         RIVER_VOICE_SPK_TEST_SW_VOLUME);
    AudioTrack_SetStartThresholdBytes(g_river_voice_speaker_test.track, (int32_t)track_buffer_bytes);

    if (AudioTrack_Start(g_river_voice_speaker_test.track) != 0) {
        printf("[river][voice] AudioTrack_Start failed for speaker test\n");
        return RIVER_ERR_UNSUPPORTED;
    }
    g_river_voice_speaker_test.track_started = true;

    printf("[river][voice] speaker test config: %lu Hz, %lu ch, 16-bit, dual-mono tone -> speaker\n",
           (unsigned long)RIVER_VOICE_SPK_TEST_SAMPLE_RATE,
           (unsigned long)RIVER_VOICE_SPK_TEST_CHANNELS);
    printf("[river][voice] speaker test pattern: 1000Hz 400ms, gap 200ms, 1500Hz 400ms, gap 1000ms\n");
    return RIVER_OK;
}

static void river_voice_speaker_test_generate_tone(uint8_t *buffer,
                                                   uint32_t frames,
                                                   uint32_t channels,
                                                   uint32_t frequency_hz,
                                                   uint32_t sample_rate,
                                                   uint32_t *phase_q32,
                                                   uint16_t *peak)
{
    int16_t *samples;
    uint32_t frame_index;
    uint32_t channel_index;
    uint32_t phase;
    uint32_t phase_step;
    uint16_t local_peak;

    if (buffer == 0 || phase_q32 == 0 || peak == 0) {
        return;
    }

    samples = (int16_t *)buffer;
    phase = *phase_q32;
    phase_step = (uint32_t)(((uint64_t)frequency_hz << 32) / sample_rate);
    local_peak = 0U;

    for (frame_index = 0; frame_index < frames; ++frame_index) {
        int16_t sample_value;

        sample_value = (phase & 0x80000000U) != 0U ?
                       (int16_t)RIVER_VOICE_SPK_TEST_AMPLITUDE :
                       (int16_t)(-RIVER_VOICE_SPK_TEST_AMPLITUDE);

        for (channel_index = 0; channel_index < channels; ++channel_index) {
            *samples++ = sample_value;
        }

        phase += phase_step;
        local_peak = RIVER_VOICE_SPK_TEST_AMPLITUDE;
    }

    *phase_q32 = phase;
    *peak = local_peak;
}

static void river_voice_speaker_test_fill_buffer(void)
{
    const river_voice_speaker_test_segment_t *segment;

    segment = &k_river_voice_speaker_test_pattern[g_river_voice_speaker_test.segment_index];
    g_river_voice_speaker_test.current_frequency_hz = segment->frequency_hz;
    g_river_voice_speaker_test.current_segment_name = segment->name;

    if (segment->frequency_hz == 0U) {
        memset(g_river_voice_speaker_test.buffer, 0, g_river_voice_speaker_test.chunk_bytes);
        g_river_voice_speaker_test.phase_q32 = 0U;
        g_river_voice_speaker_test.last_peak = 0U;
    } else {
        river_voice_speaker_test_generate_tone(g_river_voice_speaker_test.buffer,
                                               g_river_voice_speaker_test.frames_per_chunk,
                                               RIVER_VOICE_SPK_TEST_CHANNELS,
                                               segment->frequency_hz,
                                               RIVER_VOICE_SPK_TEST_SAMPLE_RATE,
                                               &g_river_voice_speaker_test.phase_q32,
                                               &g_river_voice_speaker_test.last_peak);
    }

    g_river_voice_speaker_test.chunk_index_in_segment++;
    if (g_river_voice_speaker_test.chunk_index_in_segment >= segment->chunk_count) {
        g_river_voice_speaker_test.chunk_index_in_segment = 0U;
        g_river_voice_speaker_test.segment_index++;
        if (g_river_voice_speaker_test.segment_index >=
            (sizeof(k_river_voice_speaker_test_pattern) / sizeof(k_river_voice_speaker_test_pattern[0]))) {
            g_river_voice_speaker_test.segment_index = 0U;
        }
    }
}

static void river_voice_speaker_test_task(void *param)
{
    (void)param;

    while (!g_river_voice_speaker_test.stop_requested) {
        if (g_river_voice_speaker_test.buffer == 0) {
            break;
        }

        river_voice_speaker_test_fill_buffer();
        if (AudioTrack_Write(g_river_voice_speaker_test.track,
                             g_river_voice_speaker_test.buffer,
                             g_river_voice_speaker_test.chunk_bytes,
                             true) < 0) {
            g_river_voice_speaker_test.diag_write_fail++;
            river_voice_speaker_test_log_diagnostics_if_needed();
            printf("[river][voice] AudioTrack_Write failed in speaker test\n");
            break;
        }

        g_river_voice_speaker_test.diag_write_ok++;
        river_voice_speaker_test_log_diagnostics_if_needed();
    }

    river_voice_speaker_test_close_audio();
    river_voice_speaker_test_release_buffer();
    g_river_voice_speaker_test.stop_requested = false;
    g_river_voice_speaker_test.running = false;
    g_river_voice_speaker_test.task = 0;
    printf("[river][voice] speaker test stopped\n");
    rtos_task_delete(NULL);
}

river_status_t river_voice_speaker_test_start(void)
{
    river_status_t status;

    if (g_river_voice_speaker_test.running) {
        printf("[river][voice] speaker test already running\n");
        return RIVER_OK;
    }

    memset(&g_river_voice_speaker_test, 0, sizeof(g_river_voice_speaker_test));

    status = river_voice_speaker_test_prepare_buffer();
    if (status != RIVER_OK) {
        printf("[river][voice] allocate speaker test buffer failed\n");
        return status;
    }

    status = river_voice_speaker_test_open_audio();
    if (status != RIVER_OK) {
        river_voice_speaker_test_close_audio();
        river_voice_speaker_test_release_buffer();
        return status;
    }

    g_river_voice_speaker_test.running = true;
    if (rtos_task_create(&g_river_voice_speaker_test.task,
                         "river_spk_test",
                         river_voice_speaker_test_task,
                         0,
                         RIVER_VOICE_SPK_TEST_TASK_STACK,
                         RIVER_VOICE_SPK_TEST_TASK_PRIORITY) != RTK_SUCCESS) {
        printf("[river][voice] create speaker test task failed\n");
        g_river_voice_speaker_test.running = false;
        river_voice_speaker_test_close_audio();
        river_voice_speaker_test_release_buffer();
        return RIVER_ERR_NO_MEMORY;
    }

    printf("[river][voice] speaker test started\n");
    return RIVER_OK;
}

river_status_t river_voice_speaker_test_stop(void)
{
    uint32_t wait_count;

    if (!g_river_voice_speaker_test.running) {
        printf("[river][voice] speaker test already stopped\n");
        return RIVER_OK;
    }

    g_river_voice_speaker_test.stop_requested = true;
    for (wait_count = 0; wait_count < 100U; ++wait_count) {
        if (!g_river_voice_speaker_test.running) {
            return RIVER_OK;
        }
        rtos_time_delay_ms(20U);
    }

    printf("[river][voice] speaker test stop timeout\n");
    return RIVER_ERR_BUSY;
}

bool river_voice_speaker_test_is_running(void)
{
    return g_river_voice_speaker_test.running;
}

const char *river_voice_speaker_test_status_name(void)
{
    return river_voice_speaker_test_is_running() ? "running" : "stopped";
}

void river_voice_speaker_test_set_diag_enabled(bool enabled)
{
    g_river_voice_speaker_test_diag_enabled = enabled;
    g_river_voice_speaker_test.diag_enabled = enabled;
    river_voice_speaker_test_reset_diag_counters();
}

bool river_voice_speaker_test_diag_enabled(void)
{
    return g_river_voice_speaker_test_diag_enabled;
}

void river_voice_speaker_test_dump_status(void)
{
    printf("[river] speaker_test=%s\n", river_voice_speaker_test_status_name());
    printf("[river] speaker_test_diag=%s\n", river_voice_speaker_test_diag_enabled() ? "on" : "off");

    if (g_river_voice_speaker_test.running) {
        printf("[river][voice] speaker_test segment=%s freq=%luHz peak=%u\n",
               g_river_voice_speaker_test.current_segment_name != 0 ? g_river_voice_speaker_test.current_segment_name : "n/a",
               (unsigned long)g_river_voice_speaker_test.current_frequency_hz,
               (unsigned int)g_river_voice_speaker_test.last_peak);
    }
}
