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
#define RIVER_CLOUD_BUILD_TZ_OFFSET_SECONDS  (8L * 60L * 60L)
#define RIVER_CLOUD_STREAM_MIN_ACTIVE_MS     1200U

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
    bool time_ready_announced;
    bool time_seeded_from_build;
    int stream_open_defer_status;
    bool stream_open_defer_wifi_connected;
    bool stream_open_defer_time_ready;
    uint32_t seeded_utc_epoch;
    uint64_t seeded_utc_rtos_ms;
    uint32_t stream_started_ms;
    char last_text[192];
    char last_error[128];
} river_cloud_context_t;

static river_cloud_context_t g_river_cloud;

static bool river_cloud_is_leap_year(int year)
{
    return ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
}

static int river_cloud_month_from_abbrev(const char *month)
{
    static const char *const k_months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    int index;

    if (month == NULL) {
        return -1;
    }

    for (index = 0; index < 12; ++index) {
        if (strncmp(month, k_months[index], 3U) == 0) {
            return index + 1;
        }
    }

    return -1;
}

static uint32_t river_cloud_days_before_month(int year, int month)
{
    static const uint16_t k_days_before_month[] = {
        0U,   31U,  59U,  90U,  120U, 151U,
        181U, 212U, 243U, 273U, 304U, 334U
    };
    uint32_t days;

    if (month <= 0) {
        return 0U;
    }

    days = k_days_before_month[month - 1];
    if (month > 2 && river_cloud_is_leap_year(year)) {
        days++;
    }
    return days;
}

static time_t river_cloud_epoch_from_utc_components(int year,
                                                    int month,
                                                    int day,
                                                    int hour,
                                                    int minute,
                                                    int second)
{
    uint32_t days;
    int current_year;

    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 59) {
        return (time_t)0;
    }

    days = 0U;
    for (current_year = 1970; current_year < year; ++current_year) {
        days += river_cloud_is_leap_year(current_year) ? 366U : 365U;
    }
    days += river_cloud_days_before_month(year, month);
    days += (uint32_t)(day - 1);

    return (time_t)((days * 24U * 60U * 60U) +
                    ((uint32_t)hour * 60U * 60U) +
                    ((uint32_t)minute * 60U) +
                    (uint32_t)second);
}

static uint32_t river_cloud_ms_to_frames(uint32_t duration_ms, uint32_t frame_ms)
{
    if (duration_ms == 0U || frame_ms == 0U) {
        return 0U;
    }

    return (duration_ms + frame_ms - 1U) / frame_ms;
}

uint32_t river_cloud_now_utc_seconds(void)
{
    uint32_t sec = 0U;
    uint32_t usec = 0U;
    time_t now;

    sntp_get_system_time(&sec, &usec);
    if (sec >= RIVER_CLOUD_TIME_READY_EPOCH_MIN) {
        return sec;
    }

    time(&now);
    if (now >= (time_t)RIVER_CLOUD_TIME_READY_EPOCH_MIN) {
        return (uint32_t)now;
    }

    if (g_river_cloud.time_seeded_from_build) {
        uint64_t elapsed_ms;

        elapsed_ms = (uint64_t)rtos_time_get_current_system_time_ms() -
                     g_river_cloud.seeded_utc_rtos_ms;
        return g_river_cloud.seeded_utc_epoch + (uint32_t)(elapsed_ms / 1000ULL);
    }

    return 0U;
}

bool river_cloud_utc_ready(void)
{
    return river_cloud_now_utc_seconds() >= RIVER_CLOUD_TIME_READY_EPOCH_MIN;
}

static bool river_cloud_time_ready(void)
{
    return river_cloud_utc_ready();
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

static void river_cloud_seed_time_from_build_if_needed(void)
{
    char month_text[4];
    int month;
    int day;
    int year;
    int hour;
    int minute;
    int second;
    time_t build_local_epoch;
    uint32_t seeded_utc_epoch;

    if (river_cloud_time_ready()) {
        return;
    }

    if (sscanf(__DATE__, "%3s %d %d", month_text, &day, &year) != 3) {
        return;
    }
    month_text[3] = '\0';
    if (sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second) != 3) {
        return;
    }

    month = river_cloud_month_from_abbrev(month_text);
    if (month < 1) {
        return;
    }

    build_local_epoch = river_cloud_epoch_from_utc_components(year,
                                                              month,
                                                              day,
                                                              hour,
                                                              minute,
                                                              second);
    if (build_local_epoch == (time_t)0) {
        return;
    }

    seeded_utc_epoch = (uint32_t)(build_local_epoch - RIVER_CLOUD_BUILD_TZ_OFFSET_SECONDS);
    if (seeded_utc_epoch < RIVER_CLOUD_TIME_READY_EPOCH_MIN) {
        return;
    }

    g_river_cloud.time_seeded_from_build = true;
    g_river_cloud.seeded_utc_epoch = seeded_utc_epoch;
    g_river_cloud.seeded_utc_rtos_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    sntp_set_system_time(seeded_utc_epoch, 0U);
    if (river_cloud_time_ready()) {
        RIVER_LOGI("seed system utc from build time: utc=%lu build_local=%s %s tz_offset_sec=%ld",
                   (unsigned long)river_cloud_now_utc_seconds(),
                   __DATE__,
                   __TIME__,
                   (long)RIVER_CLOUD_BUILD_TZ_OFFSET_SECONDS);
    }
}

static void river_cloud_log_time_ready_once(void)
{
    if (g_river_cloud.time_ready_announced) {
        return;
    }

    if (!river_cloud_time_ready()) {
        return;
    }

    g_river_cloud.time_ready_announced = true;
    RIVER_LOGI("sntp ready: utc=%lu", (unsigned long)river_cloud_now_utc_seconds());
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

static void river_cloud_log_stream_open_deferred_once(river_status_t status)
{
    bool wifi_connected;
    bool time_ready;

    wifi_connected = river_wifi_station_is_connected();
    time_ready = river_cloud_time_ready();

    if ((g_river_cloud.stream_open_defer_status == status) &&
        (g_river_cloud.stream_open_defer_wifi_connected == wifi_connected) &&
        (g_river_cloud.stream_open_defer_time_ready == time_ready)) {
        return;
    }

    g_river_cloud.stream_open_defer_status = status;
    g_river_cloud.stream_open_defer_wifi_connected = wifi_connected;
    g_river_cloud.stream_open_defer_time_ready = time_ready;
    RIVER_LOGI("asr stream deferred: provider=%s status=%d wifi=%s time_ready=%s",
               river_cloud_asr_provider_name(),
               status,
               river_wifi_station_status_name(),
               time_ready ? "yes" : "no");
}

static void river_cloud_reset_stream_open_deferred_state(void)
{
    g_river_cloud.stream_open_defer_status = 0;
    g_river_cloud.stream_open_defer_wifi_connected = false;
    g_river_cloud.stream_open_defer_time_ready = false;
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
    if (!river_cloud_time_ready()) {
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
    g_river_cloud.stream_started_ms = 0U;
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
    river_cloud_seed_time_from_build_if_needed();
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

    river_cloud_log_time_ready_once();

    if (!g_river_cloud.stream_active) {
        river_cloud_pre_roll_store(pcm);
        if (!is_speech) {
            return RIVER_OK;
        }

        {
            uint32_t pre_roll_frames_before_open = g_river_cloud.pre_roll_count_frames;

            status = river_cloud_stream_open_and_flush();
            if (status != RIVER_OK) {
                g_river_cloud.stream_open_fail++;
                river_cloud_log_stream_open_deferred_once(status);
                return status;
            }

            /*
             * The current speech frame triggered stream activation. Feed it
             * immediately after opening so the provider sees real speech rather
             * than only historical pre-roll or silence.
             */
            status = g_river_cloud.provider->stream_feed(pcm, bytes);
            if (status == RIVER_ERR_BUSY) {
                return status;
            }
            if (status != RIVER_OK) {
                g_river_cloud.stream_feed_fail++;
                return status;
            }
            g_river_cloud.stream_feed_ok++;

            river_cloud_reset_stream_open_deferred_state();
            g_river_cloud.stream_active = true;
            g_river_cloud.stream_open_ok++;
            g_river_cloud.silence_frames = 0U;
            g_river_cloud.stream_started_ms = rtos_time_get_current_system_time_ms();
            RIVER_LOGI("asr stream active: provider=%s pre_roll_frames=%lu",
                       river_cloud_asr_provider_name(),
                       (unsigned long)pre_roll_frames_before_open);
            return RIVER_OK;
        }
    }

    status = g_river_cloud.provider->stream_feed(pcm, bytes);
    if (status == RIVER_ERR_BUSY) {
        return status;
    }
    if (status != RIVER_OK) {
        g_river_cloud.stream_feed_fail++;
        return status;
    }
    g_river_cloud.stream_feed_ok++;

    if (is_speech) {
        g_river_cloud.silence_frames = 0U;
    } else {
        g_river_cloud.silence_frames++;
        if (g_river_cloud.silence_frames >= g_river_cloud.post_roll_frames &&
            (rtos_time_get_current_system_time_ms() - g_river_cloud.stream_started_ms) >=
                RIVER_CLOUD_STREAM_MIN_ACTIVE_MS) {
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
