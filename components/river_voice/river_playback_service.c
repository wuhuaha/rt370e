/* 播放服务实现：统一管理 AudioTrack 生命周期、抢占和参考输出。 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "basic_types.h"
#include "os_wrapper.h"

#include "audio/audio_control.h"
#include "audio/audio_track.h"

#include "river/river_log.h"
#include "river/river_playback_service.h"
#include "river/river_reference_service.h"
#include "river/river_runtime_stats.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.playback"

#define RIVER_PLAYBACK_SERVICE_HW_VOLUME 1.0f

typedef struct {
    bool initialized;
    bool ref_owned;
    bool track_started;
    bool track_ready;
    rtos_mutex_t lock;
    struct AudioTrack *track;
    AudioTrackConfig prepared_track_config;
    river_playback_stream_config_t config;
    river_playback_service_stats_t stats;
    river_playback_service_listener_t listener;
    void *listener_user_data;
} river_playback_service_context_t;

static river_playback_service_context_t g_river_playback_service;

typedef enum {
    RIVER_PLAYBACK_CONTROL_STOP = 0,
    RIVER_PLAYBACK_CONTROL_INTERRUPT,
    RIVER_PLAYBACK_CONTROL_FLUSH,
    RIVER_PLAYBACK_CONTROL_DUCK
} river_playback_control_cmd_t;

static void river_playback_service_copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0U) {
        return;
    }

    if (src == NULL || src[0] == '\0') {
        strncpy(dst, "-", dst_size - 1U);
    } else {
        strncpy(dst, src, dst_size - 1U);
    }
    dst[dst_size - 1U] = '\0';
}

static void river_playback_service_record_control_locked(const char *control, const char *reason)
{
    river_playback_service_copy_text(g_river_playback_service.stats.last_control,
                                     sizeof(g_river_playback_service.stats.last_control),
                                     control);
    river_playback_service_copy_text(g_river_playback_service.stats.last_control_reason,
                                     sizeof(g_river_playback_service.stats.last_control_reason),
                                     reason);
    if (control != NULL && strcmp(control, "interrupt") == 0) {
        river_playback_service_copy_text(g_river_playback_service.stats.last_interrupt_reason,
                                         sizeof(g_river_playback_service.stats.last_interrupt_reason),
                                         reason);
    }
}

static void river_playback_service_advance_epoch_locked(const char *reason)
{
    if (g_river_playback_service.stats.epoch == UINT32_MAX) {
        g_river_playback_service.stats.epoch = 1U;
    } else {
        g_river_playback_service.stats.epoch++;
    }
    g_river_playback_service.stats.epoch_advance_count++;
    river_playback_service_copy_text(g_river_playback_service.stats.last_epoch_reason,
                                     sizeof(g_river_playback_service.stats.last_epoch_reason),
                                     reason);
}

static uint32_t river_playback_service_category(const river_playback_stream_config_t *config)
{
    (void)config;
    /*
     * Keep playback on the SDK MEDIA route for now.
     *
     * Ameba's public audio examples that verify simultaneous record+playback
     * use AUDIO_CATEGORY_MEDIA. The AUDIO_CATEGORY_TTS route description in
     * the SDK headers is ambiguous ("voice recognition"), and in practice it
     * is a poor fit for our barge-in validation path.
     */
    return AUDIO_CATEGORY_MEDIA;
}

bool river_playback_service_state_active(river_playback_state_t state)
{
    return state == RIVER_PLAYBACK_PREPARING ||
           state == RIVER_PLAYBACK_RUNNING ||
           state == RIVER_PLAYBACK_DRAINING ||
           state == RIVER_PLAYBACK_STOPPING ||
           state == RIVER_PLAYBACK_RECOVERING;
}

bool river_playback_service_active(void)
{
    return river_playback_service_state_active(g_river_playback_service.stats.state);
}

static float river_playback_service_clamp_gain(float gain)
{
    if (gain < 0.05f) {
        return 0.05f;
    }
    if (gain > 1.0f) {
        return 1.0f;
    }
    return gain;
}

static river_status_t river_playback_service_compute_buffer_bytes(
    const river_playback_stream_config_t *config,
    size_t min_buffer_bytes,
    size_t *track_buffer_bytes_out,
    size_t *desired_buffer_bytes_out)
{
    uint32_t desired_frame_count;
    uint64_t desired_buffer_bytes64;
    size_t desired_buffer_bytes;
    size_t track_buffer_bytes;

    if (config == NULL || track_buffer_bytes_out == NULL || desired_buffer_bytes_out == NULL) {
        return RIVER_ERR_ARG;
    }

    desired_frame_count = config->buffer_frame_count;
    if (desired_frame_count == 0U) {
        desired_frame_count = 4U;
    }

    /*
     * AudioTrack_GetMinBufferBytes() already reports the SDK's minimum whole
     * track buffer size. buffer_frame_count models how many application audio
     * frames we want queued, so take the larger of the two budgets instead of
     * multiplying the SDK minimum again.
     */
    desired_buffer_bytes64 =
        (uint64_t)config->playback_frame_bytes * (uint64_t)desired_frame_count;
    if (desired_buffer_bytes64 == 0U || desired_buffer_bytes64 > UINT32_MAX) {
        return RIVER_ERR_ARG;
    }

    desired_buffer_bytes = (size_t)desired_buffer_bytes64;
    track_buffer_bytes = min_buffer_bytes;
    if (track_buffer_bytes < desired_buffer_bytes) {
        track_buffer_bytes = desired_buffer_bytes;
    }
    if (track_buffer_bytes == 0U || track_buffer_bytes > UINT32_MAX) {
        return RIVER_ERR_ARG;
    }

    *track_buffer_bytes_out = track_buffer_bytes;
    *desired_buffer_bytes_out = desired_buffer_bytes;
    return RIVER_OK;
}

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
        river_playback_service_copy_text(g_river_playback_service.stats.stream_name,
                                         sizeof(g_river_playback_service.stats.stream_name),
                                         NULL);
        return;
    }

    river_playback_service_copy_text(g_river_playback_service.stats.stream_name,
                                     sizeof(g_river_playback_service.stats.stream_name),
                                     name);
}

static void river_playback_service_set_state_locked(river_playback_state_t state)
{
    if (g_river_playback_service.stats.state == state) {
        return;
    }

    g_river_playback_service.stats.state = state;
    river_playback_service_notify_locked();
}

static void river_playback_service_apply_volume_locked(void)
{
    float left;
    float right;

    if (g_river_playback_service.track == NULL) {
        return;
    }

    left = g_river_playback_service.config.volume_left;
    right = g_river_playback_service.config.volume_right;
    if (g_river_playback_service.stats.ducked) {
        left *= g_river_playback_service.stats.duck_gain;
        right *= g_river_playback_service.stats.duck_gain;
    }
    AudioTrack_SetVolume(g_river_playback_service.track, left, right);
}

static void river_playback_service_prepare_output_locked(void)
{
    AudioControl_SetPlaybackMute(false);
    AudioControl_SetAmplifierMute(false);
    AudioControl_SetHardwareVolume(RIVER_PLAYBACK_SERVICE_HW_VOLUME,
                                   RIVER_PLAYBACK_SERVICE_HW_VOLUME);
}

static void river_playback_service_reset_stream_locked(void)
{
    memset(&g_river_playback_service.config, 0, sizeof(g_river_playback_service.config));
    g_river_playback_service.stats.priority = RIVER_PLAYBACK_PRIO_DEBUG;
    g_river_playback_service.stats.reference_export = false;
    g_river_playback_service.stats.ducked = false;
    g_river_playback_service.stats.duck_gain = 1.0f;
    g_river_playback_service.stats.track_buffer_bytes = 0U;
    river_playback_service_copy_stream_name(NULL);
}

static void river_playback_service_close_reference_locked(void)
{
    if (g_river_playback_service.ref_owned) {
        river_reference_service_close();
        g_river_playback_service.ref_owned = false;
    }
}

static void river_playback_service_release_track_locked(void)
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
        g_river_playback_service.stats.track_destroy_count++;
    }

    g_river_playback_service.track_ready = false;
    memset(&g_river_playback_service.prepared_track_config,
           0,
           sizeof(g_river_playback_service.prepared_track_config));
}

static void river_playback_service_close_locked(bool release_track)
{
    if (g_river_playback_service.track != NULL && g_river_playback_service.track_started) {
        AudioTrack_Pause(g_river_playback_service.track);
        AudioTrack_Flush(g_river_playback_service.track);
        AudioTrack_Stop(g_river_playback_service.track);
        g_river_playback_service.track_started = false;
    }

    river_playback_service_close_reference_locked();
    if (release_track) {
        river_playback_service_release_track_locked();
    }

    river_playback_service_reset_stream_locked();
}

static river_status_t river_playback_service_ensure_track_handle_locked(void)
{
    if (g_river_playback_service.track != NULL) {
        return RIVER_OK;
    }

    g_river_playback_service.track = AudioTrack_Create();
    if (g_river_playback_service.track == NULL) {
        return RIVER_ERR_UNSUPPORTED;
    }

    g_river_playback_service.stats.track_create_count++;
    return RIVER_OK;
}

static bool river_playback_service_track_compatible_locked(uint32_t category_type,
                                                           const river_playback_stream_config_t *config,
                                                           size_t required_track_buffer_bytes)
{
    const AudioTrackConfig *prepared;

    if (config == NULL || g_river_playback_service.track == NULL ||
        !g_river_playback_service.track_ready) {
        return false;
    }

    prepared = &g_river_playback_service.prepared_track_config;
    if (prepared->category_type != category_type ||
        prepared->sample_rate != config->sample_rate ||
        prepared->channel_count != config->playback_channels ||
        prepared->format != AUDIO_FORMAT_PCM_16_BIT) {
        return false;
    }

    return prepared->buffer_bytes >= required_track_buffer_bytes;
}

static river_status_t river_playback_service_prepare_track_locked(
    const river_playback_stream_config_t *config,
    uint32_t category_type,
    size_t required_track_buffer_bytes,
    bool *reused_out)
{
    AudioTrackConfig track_config;
    river_status_t status;

    if (config == NULL || reused_out == NULL) {
        return RIVER_ERR_ARG;
    }

    *reused_out = false;
    status = river_playback_service_ensure_track_handle_locked();
    if (status != RIVER_OK) {
        return status;
    }

    if (river_playback_service_track_compatible_locked(category_type,
                                                       config,
                                                       required_track_buffer_bytes)) {
        *reused_out = true;
        g_river_playback_service.stats.track_reuse_count++;
        return RIVER_OK;
    }

    if (g_river_playback_service.track_ready) {
        river_playback_service_release_track_locked();
        status = river_playback_service_ensure_track_handle_locked();
        if (status != RIVER_OK) {
            return status;
        }
    }

    memset(&track_config, 0, sizeof(track_config));
    track_config.category_type = category_type;
    track_config.sample_rate = config->sample_rate;
    track_config.format = AUDIO_FORMAT_PCM_16_BIT;
    track_config.channel_count = config->playback_channels;
    track_config.buffer_bytes = (uint32_t)required_track_buffer_bytes;

    if (AudioTrack_Init(g_river_playback_service.track, &track_config, AUDIO_OUTPUT_FLAG_NONE) != 0) {
        river_playback_service_release_track_locked();
        return RIVER_ERR_UNSUPPORTED;
    }

    g_river_playback_service.track_ready = true;
    g_river_playback_service.prepared_track_config = track_config;
    return RIVER_OK;
}

static river_status_t river_playback_service_stop_locked(bool interrupted, const char *reason)
{
    if (g_river_playback_service.stats.state == RIVER_PLAYBACK_IDLE) {
        return RIVER_OK;
    }

    river_runtime_stats_snapshot("playback_stop_prepare");
    river_playback_service_record_control_locked(interrupted ? "interrupt" : "stop", reason);
    RIVER_LOGI("playback %s: stream=%s epoch=%lu",
               interrupted ? "interrupt" : "stop",
               g_river_playback_service.stats.stream_name[0] != '\0' ?
                   g_river_playback_service.stats.stream_name :
                   "-",
               (unsigned long)g_river_playback_service.stats.epoch);
    river_playback_service_set_state_locked(RIVER_PLAYBACK_STOPPING);
    river_playback_service_advance_epoch_locked(reason != NULL ?
                                                    reason :
                                                    (interrupted ? "interrupt" : "stop"));
    river_playback_service_close_locked(false);
    g_river_playback_service.stats.stop_count++;
    if (interrupted) {
        g_river_playback_service.stats.interrupt_count++;
    }
    river_playback_service_set_state_locked(RIVER_PLAYBACK_IDLE);
    river_runtime_stats_snapshot("playback_stop_cached");
    return RIVER_OK;
}

static river_status_t river_playback_service_flush_locked(const char *reason)
{
    bool recovering = g_river_playback_service.stats.state == RIVER_PLAYBACK_RECOVERING;

    if (!river_playback_service_state_active(g_river_playback_service.stats.state) &&
        !recovering) {
        return RIVER_OK;
    }

    river_playback_service_record_control_locked(recovering ? "recover" : "flush", reason);
    river_playback_service_advance_epoch_locked(reason != NULL ? reason : "flush");

    if (g_river_playback_service.ref_owned) {
        river_reference_service_reset();
    }

    if (g_river_playback_service.track != NULL && g_river_playback_service.track_started) {
        AudioTrack_Pause(g_river_playback_service.track);
        AudioTrack_Flush(g_river_playback_service.track);
        AudioTrack_Stop(g_river_playback_service.track);
        if (AudioTrack_Start(g_river_playback_service.track) != 0) {
            if (recovering) {
                char stream_name[sizeof(g_river_playback_service.stats.stream_name)];

                memcpy(stream_name,
                       g_river_playback_service.stats.stream_name,
                       sizeof(stream_name));
                stream_name[sizeof(stream_name) - 1U] = '\0';
                river_playback_service_close_locked(true);
                river_playback_service_set_state_locked(RIVER_PLAYBACK_IDLE);
                RIVER_LOGW("playback recover fallback: stream=%s epoch=%lu reason=restart_failed",
                           stream_name[0] != '\0' ? stream_name : "-",
                           (unsigned long)g_river_playback_service.stats.epoch);
                return RIVER_ERR_BUSY;
            }
            river_playback_service_set_state_locked(RIVER_PLAYBACK_ERROR);
            river_playback_service_close_locked(true);
            river_playback_service_set_state_locked(RIVER_PLAYBACK_IDLE);
            RIVER_LOGE("playback flush restart failed");
            return RIVER_ERR_UNSUPPORTED;
        }
        g_river_playback_service.track_started = true;
        river_playback_service_apply_volume_locked();
    }

    if (recovering) {
        river_playback_service_set_state_locked(RIVER_PLAYBACK_RUNNING);
    }
    g_river_playback_service.stats.flush_count++;
    RIVER_LOGI("playback %s: stream=%s epoch=%lu",
               recovering ? "recover" : "flush",
               g_river_playback_service.stats.stream_name[0] != '\0' ?
                   g_river_playback_service.stats.stream_name :
                   "-",
               (unsigned long)g_river_playback_service.stats.epoch);
    return RIVER_OK;
}

static river_status_t river_playback_service_set_duck_locked(bool enabled, float gain, const char *reason)
{
    if (!river_playback_service_state_active(g_river_playback_service.stats.state)) {
        return enabled ? RIVER_ERR_BUSY : RIVER_OK;
    }

    if (!enabled) {
        gain = 1.0f;
    } else {
        gain = river_playback_service_clamp_gain(gain);
    }

    if (g_river_playback_service.stats.ducked == enabled &&
        (!enabled || g_river_playback_service.stats.duck_gain == gain)) {
        return RIVER_OK;
    }

    river_playback_service_record_control_locked(enabled ? "duck" : "unduck", reason);
    g_river_playback_service.stats.ducked = enabled;
    g_river_playback_service.stats.duck_gain = gain;
    g_river_playback_service.stats.duck_count++;
    river_playback_service_prepare_output_locked();
    river_playback_service_apply_volume_locked();
    RIVER_LOGI("playback duck: stream=%s enabled=%s gain=%.2f epoch=%lu",
               g_river_playback_service.stats.stream_name[0] != '\0' ?
                   g_river_playback_service.stats.stream_name :
                   "-",
               enabled ? "yes" : "no",
               (double)gain,
               (unsigned long)g_river_playback_service.stats.epoch);
    return RIVER_OK;
}

static river_status_t river_playback_service_control_locked(river_playback_control_cmd_t cmd,
                                                            bool enabled,
                                                            float gain,
                                                            const char *reason)
{
    switch (cmd) {
    case RIVER_PLAYBACK_CONTROL_STOP:
        return river_playback_service_stop_locked(false, reason);
    case RIVER_PLAYBACK_CONTROL_INTERRUPT:
        return river_playback_service_stop_locked(true, reason);
    case RIVER_PLAYBACK_CONTROL_FLUSH:
        return river_playback_service_flush_locked(reason);
    case RIVER_PLAYBACK_CONTROL_DUCK:
        return river_playback_service_set_duck_locked(enabled, gain, reason);
    default:
        return RIVER_ERR_ARG;
    }
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
    g_river_playback_service.stats.epoch = 0U;
    g_river_playback_service.stats.duck_gain = 1.0f;
    river_playback_service_copy_stream_name(NULL);
    river_playback_service_copy_text(g_river_playback_service.stats.last_epoch_reason,
                                     sizeof(g_river_playback_service.stats.last_epoch_reason),
                                     NULL);
    river_playback_service_copy_text(g_river_playback_service.stats.last_control,
                                     sizeof(g_river_playback_service.stats.last_control),
                                     NULL);
    river_playback_service_copy_text(g_river_playback_service.stats.last_control_reason,
                                     sizeof(g_river_playback_service.stats.last_control_reason),
                                     NULL);
    river_playback_service_copy_text(g_river_playback_service.stats.last_interrupt_reason,
                                     sizeof(g_river_playback_service.stats.last_interrupt_reason),
                                     NULL);
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

bool river_playback_service_release_idle_track_cache(void)
{
    bool released = false;

    if (!g_river_playback_service.initialized || g_river_playback_service.lock == NULL) {
        return false;
    }

    if (rtos_mutex_take(g_river_playback_service.lock, MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
        return false;
    }

    if (g_river_playback_service.stats.state == RIVER_PLAYBACK_IDLE &&
        g_river_playback_service.track != NULL &&
        !g_river_playback_service.track_started &&
        !g_river_playback_service.ref_owned) {
        river_playback_service_release_track_locked();
        released = true;
    }

    rtos_mutex_give(g_river_playback_service.lock);
    return released;
}

river_status_t river_playback_service_start_stream(const river_playback_stream_config_t *config)
{
    size_t min_buffer_bytes;
    size_t track_buffer_bytes;
    size_t desired_buffer_bytes;
    size_t active_track_buffer_bytes;
    uint32_t category_type;
    river_status_t status;
    bool reused_track;

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

    river_runtime_stats_snapshot("playback_start_prepare");
    river_playback_service_record_control_locked("start", "start_stream");
    river_playback_service_advance_epoch_locked("start_stream");
    river_playback_service_set_state_locked(RIVER_PLAYBACK_PREPARING);
    category_type = river_playback_service_category(config);

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
            river_playback_service_close_locked(false);
            river_playback_service_set_state_locked(RIVER_PLAYBACK_IDLE);
            rtos_mutex_give(g_river_playback_service.lock);
            RIVER_LOGE("playback ref open failed");
            return RIVER_ERR_UNSUPPORTED;
        }
        g_river_playback_service.ref_owned = true;
    }

    status = river_playback_service_ensure_track_handle_locked();
    if (status != RIVER_OK) {
        river_playback_service_set_state_locked(RIVER_PLAYBACK_ERROR);
        river_playback_service_close_locked(false);
        river_playback_service_set_state_locked(RIVER_PLAYBACK_IDLE);
        rtos_mutex_give(g_river_playback_service.lock);
        RIVER_LOGE("AudioTrack_Create failed");
        return status;
    }

    min_buffer_bytes = AudioTrack_GetMinBufferBytes(g_river_playback_service.track,
                                                    category_type,
                                                    config->sample_rate,
                                                    AUDIO_FORMAT_PCM_16_BIT,
                                                    config->playback_channels);
    status = river_playback_service_compute_buffer_bytes(config,
                                                         min_buffer_bytes,
                                                         &track_buffer_bytes,
                                                         &desired_buffer_bytes);
    if (status != RIVER_OK) {
        river_playback_service_set_state_locked(RIVER_PLAYBACK_ERROR);
        river_playback_service_close_locked(false);
        river_playback_service_set_state_locked(RIVER_PLAYBACK_IDLE);
        rtos_mutex_give(g_river_playback_service.lock);
        RIVER_LOGE("playback buffer compute failed: min=%luB frame=%luB frames=%lu",
                   (unsigned long)min_buffer_bytes,
                   (unsigned long)config->playback_frame_bytes,
                   (unsigned long)config->buffer_frame_count);
        return status;
    }

    g_river_playback_service.config = *config;
    g_river_playback_service.stats.priority = config->priority;
    g_river_playback_service.stats.reference_export = config->reference_export;
    river_playback_service_copy_stream_name(config->stream_name);

    status = river_playback_service_prepare_track_locked(config,
                                                         category_type,
                                                         track_buffer_bytes,
                                                         &reused_track);
    if (status != RIVER_OK) {
        river_playback_service_set_state_locked(RIVER_PLAYBACK_ERROR);
        river_playback_service_close_locked(true);
        river_playback_service_set_state_locked(RIVER_PLAYBACK_IDLE);
        rtos_mutex_give(g_river_playback_service.lock);
        RIVER_LOGE("AudioTrack_%s failed",
                   reused_track ? "Reuse" : "Init");
        return status;
    }

    river_playback_service_apply_volume_locked();
    active_track_buffer_bytes =
        (size_t)g_river_playback_service.prepared_track_config.buffer_bytes;
    AudioTrack_SetStartThresholdBytes(g_river_playback_service.track,
                                      (int32_t)track_buffer_bytes);
    if (AudioTrack_Start(g_river_playback_service.track) != 0) {
        river_playback_service_set_state_locked(RIVER_PLAYBACK_ERROR);
        river_playback_service_close_locked(true);
        river_playback_service_set_state_locked(RIVER_PLAYBACK_IDLE);
        rtos_mutex_give(g_river_playback_service.lock);
        RIVER_LOGE("AudioTrack_Start failed");
        return RIVER_ERR_UNSUPPORTED;
    }

    g_river_playback_service.track_started = true;
    g_river_playback_service.stats.track_buffer_bytes = active_track_buffer_bytes;
    g_river_playback_service.stats.start_count++;
    river_playback_service_set_state_locked(RIVER_PLAYBACK_RUNNING);
    RIVER_LOGI("playback start: stream=%s rate=%luHz frame=%lums frame_bytes=%luB min=%luB target=%luB track=%luB ref=%s reuse=%s",
               g_river_playback_service.stats.stream_name[0] != '\0' ?
                   g_river_playback_service.stats.stream_name :
                   "-",
               (unsigned long)config->sample_rate,
               (unsigned long)config->frame_ms,
               (unsigned long)config->playback_frame_bytes,
               (unsigned long)min_buffer_bytes,
               (unsigned long)desired_buffer_bytes,
               (unsigned long)active_track_buffer_bytes,
               config->reference_export ? "yes" : "no",
               reused_track ? "yes" : "no");
    river_runtime_stats_snapshot(reused_track ? "playback_start_reuse" :
                                                "playback_start_new");

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
        river_playback_service_record_control_locked("write_error", "playback_write_failed");
        river_playback_service_set_state_locked(RIVER_PLAYBACK_RECOVERING);
        rtos_mutex_give(g_river_playback_service.lock);
        return RIVER_ERR_IO;
    }

    g_river_playback_service.stats.write_ok++;
    rtos_mutex_give(g_river_playback_service.lock);
    return RIVER_OK;
}

river_status_t river_playback_service_stop_stream_ex(const char *reason)
{
    if (!g_river_playback_service.initialized) {
        return RIVER_OK;
    }

    if (rtos_mutex_take(g_river_playback_service.lock, MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
        return RIVER_ERR_BUSY;
    }

    (void)river_playback_service_control_locked(RIVER_PLAYBACK_CONTROL_STOP, false, 0.0f, reason);

    rtos_mutex_give(g_river_playback_service.lock);
    return RIVER_OK;
}

river_status_t river_playback_service_interrupt_stream_ex(const char *reason)
{
    if (!g_river_playback_service.initialized) {
        return RIVER_OK;
    }

    if (rtos_mutex_take(g_river_playback_service.lock, MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
        return RIVER_ERR_BUSY;
    }

    (void)river_playback_service_control_locked(RIVER_PLAYBACK_CONTROL_INTERRUPT, false, 0.0f, reason);

    rtos_mutex_give(g_river_playback_service.lock);
    return RIVER_OK;
}

river_status_t river_playback_service_flush_stream_ex(const char *reason)
{
    if (!g_river_playback_service.initialized) {
        return RIVER_OK;
    }

    if (rtos_mutex_take(g_river_playback_service.lock, MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
        return RIVER_ERR_BUSY;
    }

    (void)river_playback_service_control_locked(RIVER_PLAYBACK_CONTROL_FLUSH, false, 0.0f, reason);

    rtos_mutex_give(g_river_playback_service.lock);
    return RIVER_OK;
}

river_status_t river_playback_service_set_ducking_ex(bool enabled, float gain, const char *reason)
{
    river_status_t status;

    if (!g_river_playback_service.initialized) {
        return enabled ? RIVER_ERR_BUSY : RIVER_OK;
    }

    if (rtos_mutex_take(g_river_playback_service.lock, MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
        return RIVER_ERR_BUSY;
    }

    status = river_playback_service_control_locked(RIVER_PLAYBACK_CONTROL_DUCK,
                                                   enabled,
                                                   enabled ? gain : 1.0f,
                                                   reason);

    rtos_mutex_give(g_river_playback_service.lock);
    return status;
}

river_status_t river_playback_service_stop_stream(void)
{
    return river_playback_service_stop_stream_ex(NULL);
}

river_status_t river_playback_service_interrupt_stream(void)
{
    return river_playback_service_interrupt_stream_ex(NULL);
}

river_status_t river_playback_service_flush_stream(void)
{
    return river_playback_service_flush_stream_ex(NULL);
}

river_status_t river_playback_service_set_ducking(bool enabled, float gain)
{
    return river_playback_service_set_ducking_ex(enabled, gain, NULL);
}

river_playback_state_t river_playback_service_state(void)
{
    return g_river_playback_service.stats.state;
}

uint32_t river_playback_service_epoch(void)
{
    return g_river_playback_service.stats.epoch;
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
    case RIVER_PLAYBACK_RECOVERING:
        return "recovering";
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

bool river_playback_service_ducked(void)
{
    return g_river_playback_service.stats.ducked;
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
    RIVER_LOGI("playback_service=%s stream=%s prio=%lu ref=%s duck=%s/%.2f epoch=%lu epoch_adv=%lu writes=%lu/%lu ref_writes=%lu/%lu starts=%lu stops=%lu interrupts=%lu flushes=%lu ducks=%lu track=%lu/%lu/%lu buf=%luB ctrl=%s ctrl_reason=%s epoch_reason=%s int_reason=%s",
               river_playback_service_state_name(stats.state),
               stats.stream_name[0] != '\0' ? stats.stream_name : "-",
               (unsigned long)stats.priority,
               stats.reference_export ? "on" : "off",
               stats.ducked ? "on" : "off",
               (double)stats.duck_gain,
               (unsigned long)stats.epoch,
               (unsigned long)stats.epoch_advance_count,
               (unsigned long)stats.write_ok,
               (unsigned long)stats.write_fail,
               (unsigned long)stats.ref_write_ok,
               (unsigned long)stats.ref_write_fail,
               (unsigned long)stats.start_count,
               (unsigned long)stats.stop_count,
               (unsigned long)stats.interrupt_count,
               (unsigned long)stats.flush_count,
               (unsigned long)stats.duck_count,
               (unsigned long)stats.track_create_count,
               (unsigned long)stats.track_reuse_count,
               (unsigned long)stats.track_destroy_count,
               (unsigned long)stats.track_buffer_bytes,
               stats.last_control[0] != '\0' ? stats.last_control : "-",
               stats.last_control_reason[0] != '\0' ? stats.last_control_reason : "-",
               stats.last_epoch_reason[0] != '\0' ? stats.last_epoch_reason : "-",
               stats.last_interrupt_reason[0] != '\0' ? stats.last_interrupt_reason : "-");
}
