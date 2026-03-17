#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "basic_types.h"
#include "os_wrapper.h"

#include "audio/audio_track.h"

#include "river/river_log.h"
#include "river/river_playback_service.h"
#include "river/river_reference_service.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.playback"

typedef struct {
    bool initialized;
    bool ref_owned;
    bool track_started;
    rtos_mutex_t lock;
    struct AudioTrack *track;
    river_playback_stream_config_t config;
    river_playback_service_stats_t stats;
    river_playback_service_listener_t listener;
    void *listener_user_data;
} river_playback_service_context_t;

static river_playback_service_context_t g_river_playback_service;

static void river_playback_service_notify_locked(void)
{
    if (g_river_playback_service.listener == NULL) {
        return;
    }

    g_river_playback_service.listener(g_river_playback_service.stats.state,
                                      &g_river_playback_service.config,
                                      g_river_playback_service.listener_user_data);
}

static void river_playback_service_copy_stream_name(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        strncpy(g_river_playback_service.stats.stream_name,
                "-",
                sizeof(g_river_playback_service.stats.stream_name) - 1U);
        g_river_playback_service.stats.stream_name[sizeof(g_river_playback_service.stats.stream_name) - 1U] = '\0';
        return;
    }

    strncpy(g_river_playback_service.stats.stream_name,
            name,
            sizeof(g_river_playback_service.stats.stream_name) - 1U);
    g_river_playback_service.stats.stream_name[sizeof(g_river_playback_service.stats.stream_name) - 1U] = '\0';
}

static void river_playback_service_set_state_locked(river_playback_state_t state)
{
    if (g_river_playback_service.stats.state == state) {
        return;
    }

    g_river_playback_service.stats.state = state;
    river_playback_service_notify_locked();
}

static void river_playback_service_reset_stream_locked(void)
{
    memset(&g_river_playback_service.config, 0, sizeof(g_river_playback_service.config));
    g_river_playback_service.stats.priority = RIVER_PLAYBACK_PRIO_DEBUG;
    g_river_playback_service.stats.reference_export = false;
    g_river_playback_service.stats.track_buffer_bytes = 0U;
    river_playback_service_copy_stream_name(NULL);
}

static void river_playback_service_close_locked(void)
{
    if (g_river_playback_service.track != NULL) {
        if (g_river_playback_service.track_started) {
            AudioTrack_Pause(g_river_playback_service.track);
            AudioTrack_Flush(g_river_playback_service.track);
            AudioTrack_Stop(g_river_playback_service.track);
            g_river_playback_service.track_started = false;
        }
        AudioTrack_Destroy(g_river_playback_service.track);
        g_river_playback_service.track = NULL;
    }

    if (g_river_playback_service.ref_owned) {
        river_reference_service_close();
        g_river_playback_service.ref_owned = false;
    }

    river_playback_service_reset_stream_locked();
}

river_status_t river_playback_service_init(void)
{
    if (g_river_playback_service.initialized) {
        return RIVER_OK;
    }

    memset(&g_river_playback_service, 0, sizeof(g_river_playback_service));
    if (rtos_mutex_create(&g_river_playback_service.lock) != RTK_SUCCESS) {
        return RIVER_ERR_NO_MEMORY;
    }

    g_river_playback_service.initialized = true;
    g_river_playback_service.stats.state = RIVER_PLAYBACK_IDLE;
    g_river_playback_service.stats.priority = RIVER_PLAYBACK_PRIO_DEBUG;
    river_playback_service_copy_stream_name(NULL);
    return RIVER_OK;
}

river_status_t river_playback_service_register_listener(river_playback_service_listener_t listener,
                                                        void *user_data)
{
    if (river_playback_service_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }

    if (rtos_mutex_take(g_river_playback_service.lock, MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
        return RIVER_ERR_BUSY;
    }

    g_river_playback_service.listener = listener;
    g_river_playback_service.listener_user_data = user_data;
    rtos_mutex_give(g_river_playback_service.lock);
    return RIVER_OK;
}

river_status_t river_playback_service_start_stream(const river_playback_stream_config_t *config)
{
    AudioTrackConfig track_config;
    size_t track_buffer_bytes;
    river_status_t status;

    if (config == NULL || config->sample_rate == 0U || config->frame_ms == 0U ||
        config->playback_channels == 0U || config->bits_per_sample != 16U ||
        config->playback_frame_bytes == 0U) {
        return RIVER_ERR_ARG;
    }

    status = river_playback_service_init();
    if (status != RIVER_OK) {
        return status;
    }

    if (rtos_mutex_take(g_river_playback_service.lock, MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
        return RIVER_ERR_BUSY;
    }

    if (g_river_playback_service.stats.state != RIVER_PLAYBACK_IDLE) {
        rtos_mutex_give(g_river_playback_service.lock);
        return RIVER_ERR_BUSY;
    }

    river_playback_service_set_state_locked(RIVER_PLAYBACK_PREPARING);

    if (config->reference_export) {
        river_reference_service_config_t ref_config;

        memset(&ref_config, 0, sizeof(ref_config));
        ref_config.stream_name = config->stream_name;
        ref_config.source_name = "post-delay mono speaker feed";
        ref_config.sample_rate = config->sample_rate;
        ref_config.frame_ms = config->frame_ms;
        ref_config.channels = config->reference_channels;
        ref_config.history_ms = config->reference_history_ms;
        if (river_reference_service_open(&ref_config) != RIVER_OK) {
            river_playback_service_set_state_locked(RIVER_PLAYBACK_ERROR);
            river_playback_service_close_locked();
            river_playback_service_set_state_locked(RIVER_PLAYBACK_IDLE);
            rtos_mutex_give(g_river_playback_service.lock);
            RIVER_LOGE("playback ref open failed");
            return RIVER_ERR_UNSUPPORTED;
        }
        g_river_playback_service.ref_owned = true;
    }

    g_river_playback_service.track = AudioTrack_Create();
    if (g_river_playback_service.track == NULL) {
        river_playback_service_set_state_locked(RIVER_PLAYBACK_ERROR);
        river_playback_service_close_locked();
        river_playback_service_set_state_locked(RIVER_PLAYBACK_IDLE);
        rtos_mutex_give(g_river_playback_service.lock);
        RIVER_LOGE("AudioTrack_Create failed");
        return RIVER_ERR_UNSUPPORTED;
    }

    track_buffer_bytes = AudioTrack_GetMinBufferBytes(g_river_playback_service.track,
                                                      AUDIO_CATEGORY_MEDIA,
                                                      config->sample_rate,
                                                      AUDIO_FORMAT_PCM_16_BIT,
                                                      config->playback_channels);
    if (track_buffer_bytes < config->playback_frame_bytes) {
        track_buffer_bytes = config->playback_frame_bytes;
    }
    if (config->buffer_frame_count > 1U) {
        track_buffer_bytes *= config->buffer_frame_count;
    } else {
        track_buffer_bytes *= 4U;
    }

    memset(&track_config, 0, sizeof(track_config));
    track_config.category_type = AUDIO_CATEGORY_MEDIA;
    track_config.sample_rate = config->sample_rate;
    track_config.format = AUDIO_FORMAT_PCM_16_BIT;
    track_config.channel_count = config->playback_channels;
    track_config.buffer_bytes = (uint32_t)track_buffer_bytes;

    if (AudioTrack_Init(g_river_playback_service.track, &track_config, AUDIO_OUTPUT_FLAG_NONE) != 0) {
        river_playback_service_set_state_locked(RIVER_PLAYBACK_ERROR);
        river_playback_service_close_locked();
        river_playback_service_set_state_locked(RIVER_PLAYBACK_IDLE);
        rtos_mutex_give(g_river_playback_service.lock);
        RIVER_LOGE("AudioTrack_Init failed");
        return RIVER_ERR_UNSUPPORTED;
    }

    AudioTrack_SetVolume(g_river_playback_service.track, config->volume_left, config->volume_right);
    AudioTrack_SetStartThresholdBytes(g_river_playback_service.track, (int32_t)track_buffer_bytes);
    if (AudioTrack_Start(g_river_playback_service.track) != 0) {
        river_playback_service_set_state_locked(RIVER_PLAYBACK_ERROR);
        river_playback_service_close_locked();
        river_playback_service_set_state_locked(RIVER_PLAYBACK_IDLE);
        rtos_mutex_give(g_river_playback_service.lock);
        RIVER_LOGE("AudioTrack_Start failed");
        return RIVER_ERR_UNSUPPORTED;
    }

    g_river_playback_service.track_started = true;
    g_river_playback_service.config = *config;
    g_river_playback_service.stats.priority = config->priority;
    g_river_playback_service.stats.reference_export = config->reference_export;
    g_river_playback_service.stats.track_buffer_bytes = track_buffer_bytes;
    g_river_playback_service.stats.start_count++;
    river_playback_service_copy_stream_name(config->stream_name);
    river_playback_service_set_state_locked(RIVER_PLAYBACK_RUNNING);

    rtos_mutex_give(g_river_playback_service.lock);
    return RIVER_OK;
}

river_status_t river_playback_service_write(const uint8_t *playback,
                                            size_t playback_bytes,
                                            const uint8_t *reference,
                                            size_t reference_bytes,
                                            bool block)
{
    int32_t write_result;

    if (playback == NULL || playback_bytes == 0U) {
        return RIVER_ERR_ARG;
    }
    if (river_playback_service_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    if (rtos_mutex_take(g_river_playback_service.lock, MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
        return RIVER_ERR_BUSY;
    }
    if (g_river_playback_service.stats.state != RIVER_PLAYBACK_RUNNING ||
        g_river_playback_service.track == NULL) {
        rtos_mutex_give(g_river_playback_service.lock);
        return RIVER_ERR_BUSY;
    }

    if (g_river_playback_service.config.reference_export) {
        if (reference != NULL &&
            reference_bytes == g_river_playback_service.config.reference_frame_bytes &&
            river_reference_service_write(reference, reference_bytes) == RIVER_OK) {
            g_river_playback_service.stats.ref_write_ok++;
        } else {
            g_river_playback_service.stats.ref_write_fail++;
        }
    }

    write_result = AudioTrack_Write(g_river_playback_service.track,
                                    playback,
                                    playback_bytes,
                                    block);
    if (write_result < 0) {
        g_river_playback_service.stats.write_fail++;
        river_playback_service_set_state_locked(RIVER_PLAYBACK_ERROR);
        rtos_mutex_give(g_river_playback_service.lock);
        return RIVER_ERR_IO;
    }

    g_river_playback_service.stats.write_ok++;
    rtos_mutex_give(g_river_playback_service.lock);
    return RIVER_OK;
}

river_status_t river_playback_service_stop_stream(void)
{
    if (!g_river_playback_service.initialized) {
        return RIVER_OK;
    }

    if (rtos_mutex_take(g_river_playback_service.lock, MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
        return RIVER_ERR_BUSY;
    }

    if (g_river_playback_service.stats.state == RIVER_PLAYBACK_IDLE) {
        rtos_mutex_give(g_river_playback_service.lock);
        return RIVER_OK;
    }

    river_playback_service_set_state_locked(RIVER_PLAYBACK_STOPPING);
    river_playback_service_close_locked();
    g_river_playback_service.stats.stop_count++;
    river_playback_service_set_state_locked(RIVER_PLAYBACK_IDLE);

    rtos_mutex_give(g_river_playback_service.lock);
    return RIVER_OK;
}

river_playback_state_t river_playback_service_state(void)
{
    return g_river_playback_service.stats.state;
}

const char *river_playback_service_state_name(river_playback_state_t state)
{
    switch (state) {
    case RIVER_PLAYBACK_IDLE:
        return "idle";
    case RIVER_PLAYBACK_PREPARING:
        return "preparing";
    case RIVER_PLAYBACK_RUNNING:
        return "running";
    case RIVER_PLAYBACK_DRAINING:
        return "draining";
    case RIVER_PLAYBACK_STOPPING:
        return "stopping";
    case RIVER_PLAYBACK_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

bool river_playback_service_reference_enabled(void)
{
    return g_river_playback_service.stats.reference_export;
}

void river_playback_service_get_stats(river_playback_service_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }

    memset(stats, 0, sizeof(*stats));
    if (!g_river_playback_service.initialized) {
        stats->state = RIVER_PLAYBACK_IDLE;
        return;
    }

    if (rtos_mutex_take(g_river_playback_service.lock, MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
        stats->state = RIVER_PLAYBACK_ERROR;
        return;
    }

    *stats = g_river_playback_service.stats;
    rtos_mutex_give(g_river_playback_service.lock);
}

void river_playback_service_dump_status(void)
{
    river_playback_service_stats_t stats;

    river_playback_service_get_stats(&stats);
    RIVER_LOGI("playback_service=%s stream=%s prio=%lu ref=%s writes=%lu/%lu ref_writes=%lu/%lu starts=%lu stops=%lu buf=%luB",
               river_playback_service_state_name(stats.state),
               stats.stream_name[0] != '\0' ? stats.stream_name : "-",
               (unsigned long)stats.priority,
               stats.reference_export ? "on" : "off",
               (unsigned long)stats.write_ok,
               (unsigned long)stats.write_fail,
               (unsigned long)stats.ref_write_ok,
               (unsigned long)stats.ref_write_fail,
               (unsigned long)stats.start_count,
               (unsigned long)stats.stop_count,
               (unsigned long)stats.track_buffer_bytes);
}
