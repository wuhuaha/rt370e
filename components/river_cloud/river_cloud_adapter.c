#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lwip/apps/sntp.h"
#include "sntp/sntp_api.h"

#include "os_wrapper.h"

#include "river/river_cloud.h"
#include "river/river_log.h"
#include "river/river_wifi_station.h"
#include "river_asr_provider_internal.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.cloud"

#define RIVER_CLOUD_DEFAULT_SNTP_SERVER      "pool.ntp.org"
#define RIVER_CLOUD_SNTP_UPDATE_INTERVAL_MS  (60U * 60U * 1000U)
#define RIVER_CLOUD_TIME_READY_EPOCH_MIN     1700000000UL

typedef struct {
    bool initialized;
    bool sntp_started;
    bool audio_bridge_open;
    bool stream_active;
    river_cloud_asr_result_handler_t result_handler;
    void *result_handler_user;
    const river_cloud_asr_provider_ops_t *provider;
    river_cloud_asr_audio_desc_t audio_desc;
    uint8_t *pre_roll_buffer;
    uint32_t pre_roll_capacity_frames;
    uint32_t pre_roll_count_frames;
    uint32_t pre_roll_write_index_frames;
    uint32_t post_roll_frames;
    uint32_t silence_frames;
    size_t frame_bytes;
    uint32_t stream_open_ok;
    uint32_t stream_open_fail;
    uint32_t stream_feed_ok;
    uint32_t stream_feed_fail;
    uint32_t stream_close_ok;
    uint32_t stream_close_fail;
    uint32_t batch_submit_ok;
    uint32_t batch_submit_fail;
    uint32_t batch_submit_unsupported;
    uint32_t partial_results;
    uint32_t final_results;
    uint32_t error_results;
    char last_text[192];
    char last_error[128];
} river_cloud_context_t;

static river_cloud_context_t g_river_cloud;

static uint32_t river_cloud_ms_to_frames(uint32_t duration_ms, uint32_t frame_ms)
{
    if (duration_ms == 0U || frame_ms == 0U) {
        return 0U;
    }

    return (duration_ms + frame_ms - 1U) / frame_ms;
}

static bool river_cloud_time_ready(void)
{
    time_t now;

    time(&now);
    return now >= (time_t)RIVER_CLOUD_TIME_READY_EPOCH_MIN;
}

static void river_cloud_start_sntp_if_needed(void)
{
    if (g_river_cloud.sntp_started) {
        return;
    }

    sntp_setservername(0, RIVER_CLOUD_DEFAULT_SNTP_SERVER);
    sntp_set_update_interval(RIVER_CLOUD_SNTP_UPDATE_INTERVAL_MS);
    sntp_init();
    g_river_cloud.sntp_started = true;
    RIVER_LOGI("sntp init: server=%s interval_ms=%u",
               RIVER_CLOUD_DEFAULT_SNTP_SERVER,
               (unsigned int)RIVER_CLOUD_SNTP_UPDATE_INTERVAL_MS);
}

static void river_cloud_notify_result(const river_cloud_asr_result_t *result,
                                      void *user_data)
{
    (void)user_data;

    if (result == NULL) {
        return;
    }

    if (result->type == RIVER_CLOUD_ASR_EVENT_PARTIAL) {
        g_river_cloud.partial_results++;
    } else if (result->type == RIVER_CLOUD_ASR_EVENT_FINAL) {
        g_river_cloud.final_results++;
    } else if (result->type == RIVER_CLOUD_ASR_EVENT_ERROR) {
        g_river_cloud.error_results++;
    }

    if (result->text != NULL) {
        snprintf(g_river_cloud.last_text,
                 sizeof(g_river_cloud.last_text),
                 "%s",
                 result->text);
    }
    if (result->message != NULL) {
        snprintf(g_river_cloud.last_error,
                 sizeof(g_river_cloud.last_error),
                 "%s",
                 result->message);
    }

    if (g_river_cloud.result_handler != NULL) {
        g_river_cloud.result_handler(result, g_river_cloud.result_handler_user);
    }
}

static void river_cloud_pre_roll_reset(void)
{
    g_river_cloud.pre_roll_count_frames = 0U;
    g_river_cloud.pre_roll_write_index_frames = 0U;
}

static void river_cloud_pre_roll_store(const uint8_t *pcm)
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

static river_status_t river_cloud_stream_open_and_flush(void)
{
    uint32_t read_index;
    uint32_t frame_index;
    river_status_t status;

    if (g_river_cloud.provider == NULL || !g_river_cloud.provider->supports_streaming()) {
        return RIVER_ERR_UNSUPPORTED;
    }
    if (!river_wifi_station_is_connected()) {
        return RIVER_ERR_BUSY;
    }

    river_cloud_start_sntp_if_needed();
    if (!river_cloud_time_ready()) {
        snprintf(g_river_cloud.last_error,
                 sizeof(g_river_cloud.last_error),
                 "%s",
                 "system utc not ready");
        return RIVER_ERR_BUSY;
    }

    status = g_river_cloud.provider->stream_open(&g_river_cloud.audio_desc);
    if (status != RIVER_OK) {
        return status;
    }

    if (g_river_cloud.pre_roll_count_frames == 0U) {
        return RIVER_OK;
    }

    if (g_river_cloud.pre_roll_count_frames == g_river_cloud.pre_roll_capacity_frames) {
        read_index = g_river_cloud.pre_roll_write_index_frames;
    } else {
        read_index = 0U;
    }

    for (frame_index = 0U; frame_index < g_river_cloud.pre_roll_count_frames; ++frame_index) {
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

static river_status_t river_cloud_stream_finish_active(void)
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
    river_cloud_pre_roll_reset();
    return status;
}

river_status_t river_cloud_adapter_init(void)
{
    if (g_river_cloud.initialized) {
        return RIVER_OK;
    }

    memset(&g_river_cloud, 0, sizeof(g_river_cloud));
    g_river_cloud.provider = river_cloud_provider_default();
    if (g_river_cloud.provider == NULL) {
        RIVER_LOGE("no online asr provider registered");
        return RIVER_ERR_UNSUPPORTED;
    }
    if (g_river_cloud.provider->init(river_cloud_notify_result, &g_river_cloud) != RIVER_OK) {
        RIVER_LOGE("asr provider init failed");
        return RIVER_ERR_UNSUPPORTED;
    }

    river_cloud_start_sntp_if_needed();
    RIVER_LOGI("online asr provider init: %s stream=%s batch=%s",
               river_cloud_asr_provider_name(),
               river_cloud_asr_streaming_supported() ? "yes" : "no",
               river_cloud_asr_batch_supported() ? "yes" : "no");
    g_river_cloud.initialized = true;
    return RIVER_OK;
}

river_status_t river_cloud_adapter_set_result_handler(river_cloud_asr_result_handler_t handler,
                                                      void *user_data)
{
    g_river_cloud.result_handler = handler;
    g_river_cloud.result_handler_user = user_data;
    return RIVER_OK;
}

river_status_t river_cloud_adapter_submit_text(const char *text)
{
    if (text == NULL) {
        return RIVER_ERR_ARG;
    }

    RIVER_LOGI("text stub=%s", text);
    return RIVER_OK;
}

const char *river_cloud_asr_provider_name(void)
{
    if (g_river_cloud.provider == NULL) {
        return "disabled";
    }
    return g_river_cloud.provider->provider_name();
}

bool river_cloud_asr_streaming_supported(void)
{
    return (g_river_cloud.provider != NULL) &&
           g_river_cloud.provider->supports_streaming();
}

bool river_cloud_asr_batch_supported(void)
{
    return (g_river_cloud.provider != NULL) &&
           g_river_cloud.provider->supports_batch();
}

river_status_t river_cloud_asr_audio_open(const river_cloud_asr_audio_desc_t *audio,
                                          uint32_t pre_roll_ms,
                                          uint32_t post_roll_ms)
{
    size_t pre_roll_bytes;

    if (audio == NULL || audio->sample_rate == 0U || audio->frame_ms == 0U ||
        audio->channels == 0U || audio->bits_per_sample == 0U) {
        return RIVER_ERR_ARG;
    }

    river_cloud_asr_audio_close();
    g_river_cloud.audio_desc = *audio;
    g_river_cloud.frame_bytes =
        (size_t)((audio->sample_rate * audio->frame_ms) / 1000U) *
        audio->channels * (audio->bits_per_sample / 8U);
    g_river_cloud.pre_roll_capacity_frames =
        river_cloud_ms_to_frames(pre_roll_ms, audio->frame_ms);
    g_river_cloud.post_roll_frames =
        river_cloud_ms_to_frames(post_roll_ms, audio->frame_ms);

    if (g_river_cloud.pre_roll_capacity_frames > 0U) {
        pre_roll_bytes = (size_t)g_river_cloud.pre_roll_capacity_frames * g_river_cloud.frame_bytes;
        g_river_cloud.pre_roll_buffer = (uint8_t *)rtos_mem_zmalloc((uint32_t)pre_roll_bytes);
        if (g_river_cloud.pre_roll_buffer == NULL) {
            river_cloud_asr_audio_close();
            return RIVER_ERR_NO_MEMORY;
        }
    }

    g_river_cloud.audio_bridge_open = true;
    RIVER_LOGI("asr bridge open: provider=%s %luHz/%luch/%lubit frame=%lums pre=%ums post=%ums",
               river_cloud_asr_provider_name(),
               (unsigned long)audio->sample_rate,
               (unsigned long)audio->channels,
               (unsigned long)audio->bits_per_sample,
               (unsigned long)audio->frame_ms,
               (unsigned int)pre_roll_ms,
               (unsigned int)post_roll_ms);
    return RIVER_OK;
}

void river_cloud_asr_audio_close(void)
{
    river_cloud_stream_finish_active();
    if (g_river_cloud.provider != NULL) {
        g_river_cloud.provider->stream_poll(0U);
    }

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
    river_cloud_pre_roll_reset();
    memset(&g_river_cloud.audio_desc, 0, sizeof(g_river_cloud.audio_desc));
}

river_status_t river_cloud_asr_stream_push_frame(const uint8_t *pcm,
                                                 size_t bytes,
                                                 bool is_speech)
{
    river_status_t status;

    if (!g_river_cloud.audio_bridge_open || pcm == NULL || bytes != g_river_cloud.frame_bytes) {
        return RIVER_ERR_ARG;
    }
    if (!river_cloud_asr_streaming_supported()) {
        return RIVER_ERR_UNSUPPORTED;
    }

    if (g_river_cloud.provider != NULL) {
        g_river_cloud.provider->stream_poll(0U);
    }

    if (!g_river_cloud.stream_active) {
        river_cloud_pre_roll_store(pcm);
        if (!is_speech) {
            return RIVER_OK;
        }

        status = river_cloud_stream_open_and_flush();
        if (status != RIVER_OK) {
            g_river_cloud.stream_open_fail++;
            RIVER_LOGD("asr stream open deferred: provider=%s status=%d wifi=%s time_ready=%s",
                       river_cloud_asr_provider_name(),
                       status,
                       river_wifi_station_status_name(),
                       river_cloud_time_ready() ? "yes" : "no");
            return status;
        }

        g_river_cloud.stream_active = true;
        g_river_cloud.stream_open_ok++;
        g_river_cloud.silence_frames = 0U;
        return RIVER_OK;
    }

    status = g_river_cloud.provider->stream_feed(pcm, bytes);
    if (status != RIVER_OK) {
        g_river_cloud.stream_feed_fail++;
        return status;
    }
    g_river_cloud.stream_feed_ok++;

    if (is_speech) {
        g_river_cloud.silence_frames = 0U;
    } else {
        g_river_cloud.silence_frames++;
        if (g_river_cloud.silence_frames >= g_river_cloud.post_roll_frames) {
            river_cloud_stream_finish_active();
        }
    }

    if (g_river_cloud.provider != NULL) {
        g_river_cloud.provider->stream_poll(0U);
    }
    return RIVER_OK;
}

river_status_t river_cloud_asr_batch_submit_segment(const uint8_t *pcm,
                                                    size_t bytes,
                                                    const river_voice_segment_desc_t *segment)
{
    river_status_t status;

    if (pcm == NULL || bytes == 0U || segment == NULL) {
        return RIVER_ERR_ARG;
    }
    if (g_river_cloud.provider == NULL) {
        return RIVER_ERR_UNSUPPORTED;
    }

    status = g_river_cloud.provider->batch_submit(pcm, bytes, segment);
    if (status == RIVER_OK) {
        g_river_cloud.batch_submit_ok++;
        return RIVER_OK;
    }
    if (status == RIVER_ERR_UNSUPPORTED) {
        g_river_cloud.batch_submit_unsupported++;
        return RIVER_ERR_UNSUPPORTED;
    }

    g_river_cloud.batch_submit_fail++;
    return status;
}

void river_cloud_adapter_dump_status(void)
{
    RIVER_LOGI("asr provider=%s stream=%s batch=%s wifi=%s bridge=%s time_ready=%s partial=%lu final=%lu err=%lu open_ok=%lu open_fail=%lu feed_ok=%lu feed_fail=%lu close_ok=%lu close_fail=%lu batch_ok=%lu batch_unsupported=%lu batch_fail=%lu last_text=%s last_err=%s",
               river_cloud_asr_provider_name(),
               river_cloud_asr_streaming_supported() ? "yes" : "no",
               river_cloud_asr_batch_supported() ? "yes" : "no",
               river_wifi_station_status_name(),
               g_river_cloud.audio_bridge_open ? "open" : "closed",
               river_cloud_time_ready() ? "yes" : "no",
               (unsigned long)g_river_cloud.partial_results,
               (unsigned long)g_river_cloud.final_results,
               (unsigned long)g_river_cloud.error_results,
               (unsigned long)g_river_cloud.stream_open_ok,
               (unsigned long)g_river_cloud.stream_open_fail,
               (unsigned long)g_river_cloud.stream_feed_ok,
               (unsigned long)g_river_cloud.stream_feed_fail,
               (unsigned long)g_river_cloud.stream_close_ok,
               (unsigned long)g_river_cloud.stream_close_fail,
               (unsigned long)g_river_cloud.batch_submit_ok,
               (unsigned long)g_river_cloud.batch_submit_unsupported,
               (unsigned long)g_river_cloud.batch_submit_fail,
               g_river_cloud.last_text[0] != '\0' ? g_river_cloud.last_text : "-",
               g_river_cloud.last_error[0] != '\0' ? g_river_cloud.last_error : "-");
    if (g_river_cloud.provider != NULL) {
        g_river_cloud.provider->dump_status();
    }
}
