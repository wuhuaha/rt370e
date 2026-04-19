/* Generic ASR capture bridge runtime: frame sizing, pre-roll allocation, and bridge-state reset. */
#include <stdio.h>
#include <string.h>

#include "river/river_runtime_stats.h"
#include "river/river_wifi_station.h"

#include "river_cloud_internal.h"

static uint32_t river_cloud_asr_ms_to_frames(uint32_t duration_ms, uint32_t frame_ms)
{
    if (duration_ms == 0U || frame_ms == 0U) {
        return 0U;
    }

    return (duration_ms + frame_ms - 1U) / frame_ms;
}

static size_t river_cloud_asr_frame_bytes(const river_cloud_asr_audio_desc_t *audio)
{
    if (audio == NULL || audio->sample_rate == 0U || audio->frame_ms == 0U ||
        audio->channels == 0U || audio->bits_per_sample == 0U) {
        return 0U;
    }

    return (size_t)((audio->sample_rate * audio->frame_ms) / 1000U) * audio->channels *
           (audio->bits_per_sample / 8U);
}

bool river_cloud_business_time_ready(void)
{
#if RIVER_CLOUD_BUSINESS_TIME_WAIT_REQUIRED
    return river_cloud_time_ready();
#else
    return true;
#endif
}

river_status_t river_cloud_prepare_audio_bridge_state(const river_cloud_asr_audio_desc_t *audio,
                                                      uint32_t pre_roll_ms,
                                                      uint32_t post_roll_ms)
{
    size_t pre_roll_bytes;

    if (audio == NULL) {
        return RIVER_ERR_ARG;
    }

    g_river_cloud.audio_desc = *audio;
    g_river_cloud.frame_bytes = river_cloud_asr_frame_bytes(audio);
    g_river_cloud.pre_roll_capacity_frames =
        river_cloud_asr_ms_to_frames(pre_roll_ms, audio->frame_ms);
    g_river_cloud.post_roll_frames = river_cloud_asr_ms_to_frames(post_roll_ms, audio->frame_ms);
    g_river_cloud.silence_frames = 0U;
    g_river_cloud.stream_started_ms = 0U;
    river_cloud_pre_roll_reset();

    if (g_river_cloud.frame_bytes == 0U) {
        river_cloud_reset_audio_bridge_state();
        return RIVER_ERR_ARG;
    }

    if (g_river_cloud.pre_roll_capacity_frames == 0U) {
        return RIVER_OK;
    }

    pre_roll_bytes = (size_t)g_river_cloud.pre_roll_capacity_frames * g_river_cloud.frame_bytes;
    g_river_cloud.pre_roll_buffer = (uint8_t *)rtos_mem_zmalloc((uint32_t)pre_roll_bytes);
    if (g_river_cloud.pre_roll_buffer == NULL) {
        river_cloud_reset_audio_bridge_state();
        return RIVER_ERR_NO_MEMORY;
    }

    return RIVER_OK;
}

river_status_t river_cloud_stream_open_and_flush(void)
{
    uint32_t read_index;
    uint32_t frame_index;
    uint32_t pre_roll_frames;
    river_status_t status;

    if (g_river_cloud.provider == NULL || !g_river_cloud.provider->supports_streaming()) {
        return RIVER_ERR_UNSUPPORTED;
    }
    if (!river_wifi_station_is_connected()) {
        return RIVER_ERR_BUSY;
    }

    river_cloud_start_sntp_if_needed();
    river_cloud_seed_time_from_build_if_needed();
    if (!river_cloud_business_time_ready()) {
        snprintf(g_river_cloud.last_error,
                 sizeof(g_river_cloud.last_error),
                 "%s",
                 "system utc not ready");
        return RIVER_ERR_BUSY;
    }

    pre_roll_frames = g_river_cloud.pre_roll_count_frames;
    status = g_river_cloud.provider->stream_open(&g_river_cloud.audio_desc);
    if (status != RIVER_OK) {
        return status;
    }

    if (pre_roll_frames == 0U) {
        return RIVER_OK;
    }

    if (pre_roll_frames == g_river_cloud.pre_roll_capacity_frames) {
        read_index = g_river_cloud.pre_roll_write_index_frames;
    } else {
        read_index = 0U;
    }

    for (frame_index = 0U; frame_index < pre_roll_frames; ++frame_index) {
        const uint8_t *src = g_river_cloud.pre_roll_buffer +
                             ((size_t)read_index * g_river_cloud.frame_bytes);

        status = g_river_cloud.provider->stream_feed(src, g_river_cloud.frame_bytes);
        if (status != RIVER_OK) {
            return status;
        }

        read_index++;
        if (read_index >= g_river_cloud.pre_roll_capacity_frames) {
            read_index = 0U;
        }
    }

    river_cloud_pre_roll_reset();
    return RIVER_OK;
}

void river_cloud_pre_roll_reset(void)
{
    g_river_cloud.pre_roll_count_frames = 0U;
    g_river_cloud.pre_roll_write_index_frames = 0U;
}

void river_cloud_pre_roll_store(const uint8_t *pcm)
{
    uint8_t *dst;

    if (g_river_cloud.pre_roll_buffer == NULL || pcm == NULL ||
        g_river_cloud.pre_roll_capacity_frames == 0U || g_river_cloud.frame_bytes == 0U) {
        return;
    }

    dst = g_river_cloud.pre_roll_buffer +
          ((size_t)g_river_cloud.pre_roll_write_index_frames * g_river_cloud.frame_bytes);
    memcpy(dst, pcm, g_river_cloud.frame_bytes);

    g_river_cloud.pre_roll_write_index_frames++;
    if (g_river_cloud.pre_roll_write_index_frames >= g_river_cloud.pre_roll_capacity_frames) {
        g_river_cloud.pre_roll_write_index_frames = 0U;
    }
    if (g_river_cloud.pre_roll_count_frames < g_river_cloud.pre_roll_capacity_frames) {
        g_river_cloud.pre_roll_count_frames++;
    }
}

river_status_t river_cloud_stream_finish_active(void)
{
    river_status_t status;

    if (!g_river_cloud.stream_active || g_river_cloud.provider == NULL) {
        return RIVER_OK;
    }

    status = g_river_cloud.provider->stream_finish();
    if (status == RIVER_OK) {
        g_river_cloud.stream_close_ok++;
    } else {
        g_river_cloud.stream_close_fail++;
    }
    g_river_cloud.stream_active = false;
    g_river_cloud.silence_frames = 0U;
    g_river_cloud.stream_started_ms = 0U;
    river_cloud_pre_roll_reset();
    river_runtime_stats_snapshot("asr_stream_finish");
    return status;
}

void river_cloud_reset_audio_bridge_state(void)
{
    if (g_river_cloud.pre_roll_buffer != NULL) {
        rtos_mem_free(g_river_cloud.pre_roll_buffer);
        g_river_cloud.pre_roll_buffer = NULL;
    }

    g_river_cloud.audio_bridge_open = false;
    g_river_cloud.stream_active = false;
    g_river_cloud.pre_roll_capacity_frames = 0U;
    g_river_cloud.post_roll_frames = 0U;
    g_river_cloud.frame_bytes = 0U;
    g_river_cloud.silence_frames = 0U;
    g_river_cloud.stream_started_ms = 0U;
    river_cloud_pre_roll_reset();
    memset(&g_river_cloud.audio_desc, 0, sizeof(g_river_cloud.audio_desc));
}
