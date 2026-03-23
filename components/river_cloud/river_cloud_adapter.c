#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lwip/apps/sntp.h"
#include "sntp/sntp_api.h"

#include "os_wrapper.h"

#include "river/river_audio_frame_ring.h"
#include "river/river_cloud.h"
#include "river/river_log.h"
#include "river/river_opus_codec.h"
#include "river/river_playback_service.h"
#include "river/river_runtime_stats.h"
#include "river/river_voice_kws.h"
#include "river/river_voice_profile.h"
#include "river/river_wifi_station.h"
#include "river/river_xiaozhi_credentials.h"
#include "river/river_xiaozhi_ws.h"
#include "river_cloud_internal.h"
#include "river_asr_provider_internal.h"
#include "river_tts_internal.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.cloud"

river_cloud_context_t g_river_cloud;

#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
static void river_cloud_xiaozhi_event_handler(const river_xiaozhi_event_t *event, void *user_data);
static void river_cloud_xiaozhi_check_pending_playback_stop(void);
static river_status_t river_cloud_xiaozhi_send_uplink_packet(const uint8_t *pcm,
                                                             size_t pcm_bytes);
static void river_cloud_xiaozhi_finalize_listen_stop_if_ready(void);
static void river_cloud_xiaozhi_reset_playback_flags(void);
static void river_cloud_xiaozhi_reset_downlink_ring(void);
static river_status_t river_cloud_xiaozhi_start_playback_if_needed(uint32_t sample_rate,
                                                                   uint32_t frame_duration_ms,
                                                                   size_t mono_bytes);
#endif

#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
static bool river_cloud_xiaozhi_pump_active(void)
{
    if (!g_river_cloud.initialized || !g_river_cloud.xiaozhi_enabled) {
        return false;
    }

    return river_xiaozhi_session_open() ||
           g_river_cloud.xiaozhi_playback_active ||
           g_river_cloud.xiaozhi_tts_stop_pending ||
           g_river_cloud.xiaozhi_listening;
}

static bool river_cloud_xiaozhi_uplink_active(void)
{
    if (!g_river_cloud.initialized || !g_river_cloud.xiaozhi_enabled ||
        g_river_cloud.xiaozhi_encoder.handle == NULL) {
        return false;
    }

    return river_audio_frame_ring_count(&g_river_cloud.xiaozhi_uplink_ring) > 0U ||
           g_river_cloud.xiaozhi_listen_stop_pending;
}

static bool river_cloud_xiaozhi_downlink_active(void)
{
    if (!g_river_cloud.initialized || !g_river_cloud.xiaozhi_enabled) {
        return false;
    }

    return river_audio_frame_ring_count(&g_river_cloud.xiaozhi_downlink_ring) > 0U ||
           g_river_cloud.xiaozhi_tts_stop_pending ||
           g_river_cloud.xiaozhi_playback_active;
}

static void river_cloud_xiaozhi_pump_task(void *arg)
{
    (void)arg;

    for (;;) {
        river_cloud_xiaozhi_check_window_timeout();
        if (river_cloud_xiaozhi_pump_active()) {
            if (river_xiaozhi_session_open()) {
                (void)river_xiaozhi_poll(RIVER_CLOUD_XIAOZHI_PUMP_ACTIVE_MS);
            }
            river_cloud_xiaozhi_check_pending_playback_stop();
            rtos_time_delay_ms(5U);
            continue;
        }

        rtos_time_delay_ms(RIVER_CLOUD_XIAOZHI_PUMP_IDLE_MS);
    }
}

static void river_cloud_xiaozhi_downlink_expand_stereo(const uint8_t *mono_frame,
                                                       size_t mono_bytes)
{
    size_t sample_count;
    size_t index;
    const int16_t *mono;

    mono = (const int16_t *)mono_frame;
    sample_count = mono_bytes / sizeof(int16_t);
    for (index = 0U; index < sample_count; ++index) {
        g_river_cloud.xiaozhi_downlink_stereo[index * 2U] = mono[index];
        g_river_cloud.xiaozhi_downlink_stereo[(index * 2U) + 1U] = mono[index];
    }
}

static void river_cloud_xiaozhi_downlink_task(void *arg)
{
    river_status_t status;
    size_t mono_bytes;
    size_t stereo_bytes;
    uint32_t queued_frames;

    (void)arg;

    for (;;) {
        if (!river_cloud_xiaozhi_downlink_active()) {
            rtos_time_delay_ms(RIVER_CLOUD_XIAOZHI_DOWNLINK_IDLE_MS);
            continue;
        }

        queued_frames = river_audio_frame_ring_count(&g_river_cloud.xiaozhi_downlink_ring);
        if (queued_frames == 0U) {
            river_cloud_xiaozhi_check_pending_playback_stop();
            rtos_time_delay_ms(RIVER_CLOUD_XIAOZHI_DOWNLINK_POLL_MS);
            continue;
        }

        if (!g_river_cloud.xiaozhi_playback_active || !river_playback_service_active()) {
            if (!g_river_cloud.xiaozhi_tts_stop_pending &&
                queued_frames < RIVER_CLOUD_XIAOZHI_DOWNLINK_START_FRAMES) {
                rtos_time_delay_ms(RIVER_CLOUD_XIAOZHI_DOWNLINK_POLL_MS);
                continue;
            }

            status = river_cloud_xiaozhi_start_playback_if_needed(
                g_river_cloud.xiaozhi_downlink_sample_rate,
                g_river_cloud.xiaozhi_downlink_frame_duration_ms,
                g_river_cloud.xiaozhi_downlink_ring.frame_bytes);
            if (status != RIVER_OK) {
                rtos_time_delay_ms(RIVER_CLOUD_XIAOZHI_DOWNLINK_POLL_MS);
                continue;
            }
        }

        status = river_audio_frame_ring_read(&g_river_cloud.xiaozhi_downlink_ring,
                                             g_river_cloud.xiaozhi_downlink_task_frame);
        if (status != RIVER_OK) {
            river_cloud_xiaozhi_check_pending_playback_stop();
            rtos_time_delay_ms(RIVER_CLOUD_XIAOZHI_DOWNLINK_POLL_MS);
            continue;
        }

        mono_bytes = g_river_cloud.xiaozhi_downlink_ring.frame_bytes;
        stereo_bytes = mono_bytes * 2U;
        if (stereo_bytes > sizeof(g_river_cloud.xiaozhi_downlink_stereo)) {
            river_cloud_xiaozhi_reset_downlink_ring();
            (void)river_playback_service_stop_stream_ex("xiaozhi_downlink_frame_oversize");
            river_cloud_xiaozhi_reset_playback_flags();
            continue;
        }

        river_cloud_xiaozhi_downlink_expand_stereo(g_river_cloud.xiaozhi_downlink_task_frame,
                                                   mono_bytes);
        if (river_playback_service_write((const uint8_t *)g_river_cloud.xiaozhi_downlink_stereo,
                                         stereo_bytes,
                                         g_river_cloud.xiaozhi_downlink_task_frame,
                                         mono_bytes,
                                         true) != RIVER_OK) {
            RIVER_LOGW("xiaozhi playback write failed: mono=%luB stereo=%luB",
                       (unsigned long)mono_bytes,
                       (unsigned long)stereo_bytes);
            river_cloud_xiaozhi_reset_downlink_ring();
            (void)river_playback_service_stop_stream_ex("xiaozhi_playback_write_failed");
            river_cloud_xiaozhi_reset_playback_flags();
            rtos_time_delay_ms(RIVER_CLOUD_XIAOZHI_DOWNLINK_POLL_MS);
            continue;
        }

        river_cloud_xiaozhi_check_pending_playback_stop();
    }
}

static void river_cloud_xiaozhi_uplink_task(void *arg)
{
    river_status_t status;

    (void)arg;

    for (;;) {
        if (river_cloud_xiaozhi_uplink_active()) {
            status = river_audio_frame_ring_read(&g_river_cloud.xiaozhi_uplink_ring,
                                                 g_river_cloud.xiaozhi_uplink_task_frame);
            if (status == RIVER_OK) {
                if (river_xiaozhi_session_open() && g_river_cloud.xiaozhi_listening) {
                    uint32_t frame_ms = g_river_cloud.xiaozhi_encoder.frame_duration_ms;
                    uint64_t now_ms = (uint64_t)rtos_time_get_current_system_time_ms();

                    if (g_river_cloud.xiaozhi_uplink_next_send_ms != 0U &&
                        now_ms < g_river_cloud.xiaozhi_uplink_next_send_ms) {
                        rtos_time_delay_ms(
                            (uint32_t)(g_river_cloud.xiaozhi_uplink_next_send_ms - now_ms));
                    }
                    status = river_cloud_xiaozhi_send_uplink_packet(
                        g_river_cloud.xiaozhi_uplink_task_frame,
                        g_river_cloud.xiaozhi_encoder.pcm_frame_bytes);
                    if (status != RIVER_OK) {
                        RIVER_LOGW("xiaozhi uplink send failed: status=%d", status);
                    }
                    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
                    if (frame_ms == 0U ||
                        g_river_cloud.xiaozhi_uplink_next_send_ms == 0U ||
                        now_ms >
                            (g_river_cloud.xiaozhi_uplink_next_send_ms + (uint64_t)frame_ms)) {
                        g_river_cloud.xiaozhi_uplink_next_send_ms = now_ms + (uint64_t)frame_ms;
                    } else {
                        g_river_cloud.xiaozhi_uplink_next_send_ms += (uint64_t)frame_ms;
                    }
                }
                river_cloud_xiaozhi_finalize_listen_stop_if_ready();
                continue;
            }

            river_cloud_xiaozhi_finalize_listen_stop_if_ready();
            rtos_time_delay_ms(RIVER_CLOUD_XIAOZHI_UPLINK_POLL_MS);
            continue;
        }

        g_river_cloud.xiaozhi_uplink_next_send_ms = 0U;
        rtos_time_delay_ms(RIVER_CLOUD_XIAOZHI_UPLINK_IDLE_MS);
    }
}

static void river_cloud_xiaozhi_start_pump_if_needed(void)
{
    if (!g_river_cloud.xiaozhi_enabled || g_river_cloud.xiaozhi_pump_started) {
        return;
    }

    if (rtos_task_create(&g_river_cloud.xiaozhi_pump_task,
                         "river_xz_pump",
                         river_cloud_xiaozhi_pump_task,
                         NULL,
                         RIVER_CLOUD_XIAOZHI_PUMP_STACK,
                         RIVER_CLOUD_XIAOZHI_PUMP_PRIO) != RTK_SUCCESS) {
        RIVER_LOGW("xiaozhi pump task create failed");
        return;
    }

    g_river_cloud.xiaozhi_pump_started = true;
    RIVER_LOGI("xiaozhi pump started");
}

static void river_cloud_xiaozhi_start_downlink_if_needed(void)
{
    if (!g_river_cloud.xiaozhi_enabled || g_river_cloud.xiaozhi_downlink_started) {
        return;
    }

    if (rtos_task_create(&g_river_cloud.xiaozhi_downlink_task,
                         "river_xz_down",
                         river_cloud_xiaozhi_downlink_task,
                         NULL,
                         RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STACK,
                         RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_PRIO) != RTK_SUCCESS) {
        RIVER_LOGW("xiaozhi downlink task create failed");
        return;
    }

    g_river_cloud.xiaozhi_downlink_started = true;
    RIVER_LOGI("xiaozhi downlink worker started");
}

static void river_cloud_xiaozhi_start_uplink_if_needed(void)
{
    if (!g_river_cloud.xiaozhi_enabled || g_river_cloud.xiaozhi_uplink_started) {
        return;
    }

    if (rtos_task_create(&g_river_cloud.xiaozhi_uplink_task,
                         "river_xz_up",
                         river_cloud_xiaozhi_uplink_task,
                         NULL,
                         RIVER_CLOUD_XIAOZHI_UPLINK_TASK_STACK,
                         RIVER_CLOUD_XIAOZHI_UPLINK_TASK_PRIO) != RTK_SUCCESS) {
        RIVER_LOGW("xiaozhi uplink task create failed");
        return;
    }

    g_river_cloud.xiaozhi_uplink_started = true;
    RIVER_LOGI("xiaozhi uplink worker started");
}
#endif

static uint32_t river_cloud_system_utc_seconds(void)
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

    return 0U;
}

static uint32_t river_cloud_estimated_utc_seconds(void)
{
    uint32_t sec;

    sec = river_cloud_system_utc_seconds();
    if (sec != 0U) {
        return sec;
    }

    if (g_river_cloud.time_seeded_from_build) {
        uint64_t elapsed_ms;

        elapsed_ms = (uint64_t)rtos_time_get_current_system_time_ms() -
                     g_river_cloud.seeded_utc_rtos_ms;
        return g_river_cloud.seeded_utc_epoch + (uint32_t)(elapsed_ms / 1000ULL);
    }

    return 0U;
}

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
    return river_cloud_system_utc_seconds();
}

bool river_cloud_utc_ready(void)
{
    return river_cloud_now_utc_seconds() >= RIVER_CLOUD_TIME_READY_EPOCH_MIN;
}

bool river_cloud_time_ready(void)
{
    return river_cloud_system_utc_seconds() >= RIVER_CLOUD_TIME_READY_EPOCH_MIN;
}

void river_cloud_start_sntp_if_needed(void)
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

static void river_cloud_kick_sntp_on_network_ready(void)
{
    if (river_cloud_time_ready()) {
        river_cloud_log_time_ready_once();
        return;
    }

    if (g_river_cloud.sntp_started) {
        sntp_stop();
        g_river_cloud.sntp_started = false;
    }

    river_cloud_start_sntp_if_needed();
    RIVER_LOGI("sntp kick: network ready; request immediate sync");
}

void river_cloud_seed_time_from_build_if_needed(void)
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

    if (g_river_cloud.time_seeded_from_build) {
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
    RIVER_LOGI("seed utc estimate from build time: est_utc=%lu build_local=%s %s tz_offset_sec=%ld",
               (unsigned long)river_cloud_estimated_utc_seconds(),
               __DATE__,
               __TIME__,
               (long)RIVER_CLOUD_BUILD_TZ_OFFSET_SECONDS);
}

void river_cloud_log_time_ready_once(void)
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

static const char *river_cloud_split_asr_provider_name(void)
{
    if (g_river_cloud.provider == NULL) {
        return "disabled";
    }
    return g_river_cloud.provider->provider_name();
}

static bool river_cloud_xiaozhi_enabled(void)
{
#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    return g_river_cloud.xiaozhi_enabled;
#else
    return false;
#endif
}

static const char *river_cloud_active_provider_name(void)
{
    return river_cloud_xiaozhi_enabled() ?
               RIVER_CLOUD_XIAOZHI_PROVIDER_NAME :
               river_cloud_split_asr_provider_name();
}

#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
void river_cloud_emit_asr_result(river_cloud_asr_event_type_t type,
                                 const char *text,
                                 const char *sid,
                                 const char *message,
                                 int code,
                                 bool is_final)
{
    river_cloud_asr_result_t result;

    memset(&result, 0, sizeof(result));
    result.type = type;
    result.provider_name = river_cloud_active_provider_name();
    result.text = text;
    result.sid = sid;
    result.message = message;
    result.code = code;
    result.is_final = is_final;
    river_cloud_notify_result(&result, &g_river_cloud);
}
#endif

#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
static void river_cloud_xiaozhi_reset_playback_flags(void)
{
    g_river_cloud.xiaozhi_playback_active = false;
    g_river_cloud.xiaozhi_tts_stop_pending = false;
    g_river_cloud.xiaozhi_tts_stop_deadline_ms = 0U;
}

static void river_cloud_xiaozhi_reset_downlink_ring(void)
{
    if (g_river_cloud.xiaozhi_downlink_ring.initialized) {
        river_audio_frame_ring_reset(&g_river_cloud.xiaozhi_downlink_ring);
    }
    g_river_cloud.xiaozhi_downlink_ring_dropped = 0U;
}

static void river_cloud_xiaozhi_check_pending_playback_stop(void)
{
    uint64_t now_ms;

    if (!g_river_cloud.xiaozhi_tts_stop_pending) {
        return;
    }

    if (!river_playback_service_active()) {
        river_cloud_xiaozhi_reset_playback_flags();
        return;
    }

    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    if (now_ms < g_river_cloud.xiaozhi_tts_stop_deadline_ms) {
        return;
    }

    (void)river_playback_service_stop_stream_ex("xiaozhi_tts_stop");
    river_cloud_xiaozhi_reset_playback_flags();
}

static river_status_t river_cloud_xiaozhi_try_start_playback(uint32_t sample_rate,
                                                             uint32_t frame_duration_ms,
                                                             size_t mono_bytes,
                                                             uint32_t buffer_frame_count,
                                                             bool reference_export,
                                                             uint32_t reference_history_ms)
{
    river_playback_stream_config_t config;

    memset(&config, 0, sizeof(config));
    config.stream_name = RIVER_CLOUD_XIAOZHI_TTS_STREAM_NAME;
    config.priority = RIVER_PLAYBACK_PRIO_TTS;
    config.sample_rate = sample_rate;
    config.frame_ms = frame_duration_ms;
    config.playback_channels = 2U;
    config.bits_per_sample = 16U;
    config.playback_frame_bytes = mono_bytes * 2U;
    config.buffer_frame_count = buffer_frame_count;
    config.volume_left = 1.0f;
    config.volume_right = 1.0f;
    config.reference_export = reference_export;
    config.reference_channels = reference_export ? 1U : 0U;
    config.reference_frame_bytes = reference_export ? mono_bytes : 0U;
    config.reference_history_ms = reference_export ? reference_history_ms : 0U;
    return river_playback_service_start_stream(&config);
}

static river_status_t river_cloud_xiaozhi_start_playback_if_needed(uint32_t sample_rate,
                                                                   uint32_t frame_duration_ms,
                                                                   size_t mono_bytes)
{
    river_status_t status;
    const bool reference_export = river_cloud_xiaozhi_playback_allows_vad_open();
    const char *mode = reference_export ? "ref" : "no_ref";

    if (g_river_cloud.xiaozhi_playback_active && river_playback_service_active()) {
        return RIVER_OK;
    }
    if (sample_rate == 0U || frame_duration_ms == 0U || mono_bytes == 0U) {
        return RIVER_ERR_ARG;
    }
    if (river_playback_service_active() && !g_river_cloud.xiaozhi_playback_active) {
        if (river_playback_service_interrupt_stream_ex("xiaozhi_tts_takeover") != RIVER_OK &&
            river_playback_service_stop_stream_ex("xiaozhi_tts_takeover") != RIVER_OK) {
            return RIVER_ERR_BUSY;
        }
    }

    status = river_cloud_xiaozhi_try_start_playback(sample_rate,
                                                    frame_duration_ms,
                                                    mono_bytes,
                                                    RIVER_CLOUD_XIAOZHI_PLAYBACK_BUFFER_FRAMES,
                                                    reference_export,
                                                    reference_export ?
                                                        RIVER_CLOUD_XIAOZHI_PLAYBACK_REF_HISTORY_MS :
                                                        0U);
    if (status != RIVER_OK) {
        mode = "compact";
        RIVER_LOGW("xiaozhi playback start retry: status=%d -> compact mode no_ref buffer_frames=%u",
                   (int)status,
                   (unsigned int)RIVER_CLOUD_XIAOZHI_PLAYBACK_BUFFER_FRAMES_FALLBACK);
        status = river_cloud_xiaozhi_try_start_playback(
            sample_rate,
            frame_duration_ms,
            mono_bytes,
            RIVER_CLOUD_XIAOZHI_PLAYBACK_BUFFER_FRAMES_FALLBACK,
            false,
            0U);
        if (status != RIVER_OK) {
            return RIVER_ERR_BUSY;
        }
    }

    RIVER_LOGI("xiaozhi playback start: %luHz frame=%lums mono=%luB queued=%lu mode=%s",
               (unsigned long)sample_rate,
               (unsigned long)frame_duration_ms,
               (unsigned long)mono_bytes,
               (unsigned long)river_audio_frame_ring_count(&g_river_cloud.xiaozhi_downlink_ring),
               mode);
    g_river_cloud.xiaozhi_playback_active = true;
    g_river_cloud.xiaozhi_tts_stop_pending = false;
    g_river_cloud.xiaozhi_tts_stop_deadline_ms = 0U;
    return RIVER_OK;
}

static void river_cloud_xiaozhi_prepare_decoder_if_needed(uint32_t sample_rate,
                                                          uint32_t frame_duration_ms)
{
    if (sample_rate == 0U) {
        sample_rate = 24000U;
    }
    if (frame_duration_ms == 0U) {
        frame_duration_ms = 60U;
    }

    if (g_river_cloud.xiaozhi_decoder.handle != NULL &&
        g_river_cloud.xiaozhi_decoder.sample_rate == sample_rate &&
        g_river_cloud.xiaozhi_decoder.frame_duration_ms == frame_duration_ms &&
        g_river_cloud.xiaozhi_decoder.channels == 1U) {
        return;
    }

    river_opus_decoder_close(&g_river_cloud.xiaozhi_decoder);
    if (river_opus_decoder_open(&g_river_cloud.xiaozhi_decoder,
                                sample_rate,
                                1U,
                                frame_duration_ms) == RIVER_OK) {
        g_river_cloud.xiaozhi_server_sample_rate = sample_rate;
        g_river_cloud.xiaozhi_server_frame_duration_ms = frame_duration_ms;
    }
}

static river_status_t river_cloud_xiaozhi_handle_audio_event(const river_xiaozhi_event_t *event)
{
    size_t mono_bytes;
    river_status_t status;

    if (event == NULL || event->binary_data == NULL || event->binary_bytes == 0U) {
        return RIVER_ERR_ARG;
    }

    river_cloud_xiaozhi_prepare_decoder_if_needed(event->sample_rate, event->frame_duration_ms);
    if (g_river_cloud.xiaozhi_decoder.handle == NULL) {
        return RIVER_ERR_UNSUPPORTED;
    }

    if (river_opus_decode(&g_river_cloud.xiaozhi_decoder,
                          event->binary_data,
                          event->binary_bytes,
                          g_river_cloud.xiaozhi_downlink_mono,
                          sizeof(g_river_cloud.xiaozhi_downlink_mono),
                          &mono_bytes) != RIVER_OK) {
        return RIVER_ERR_IO;
    }

    if (g_river_cloud.xiaozhi_downlink_ring.initialized &&
        g_river_cloud.xiaozhi_downlink_ring.frame_bytes != mono_bytes) {
        river_audio_frame_ring_deinit(&g_river_cloud.xiaozhi_downlink_ring);
    }
    if (!g_river_cloud.xiaozhi_downlink_ring.initialized) {
        status = river_audio_frame_ring_init_with_storage_ex(
            &g_river_cloud.xiaozhi_downlink_ring,
            g_river_cloud.xiaozhi_downlink_ring_storage,
            sizeof(g_river_cloud.xiaozhi_downlink_ring_storage),
            mono_bytes,
            RIVER_CLOUD_XIAOZHI_DOWNLINK_RING_FRAMES,
            RIVER_AUDIO_FRAME_RING_MODE_SPSC);
        if (status != RIVER_OK) {
            return status;
        }
    }

    g_river_cloud.xiaozhi_downlink_sample_rate = g_river_cloud.xiaozhi_decoder.sample_rate;
    g_river_cloud.xiaozhi_downlink_frame_duration_ms =
        g_river_cloud.xiaozhi_decoder.frame_duration_ms;

    status = river_audio_frame_ring_write(&g_river_cloud.xiaozhi_downlink_ring,
                                          (const uint8_t *)g_river_cloud.xiaozhi_downlink_mono);
    if (status != RIVER_OK) {
        if (river_audio_frame_ring_read(&g_river_cloud.xiaozhi_downlink_ring,
                                        g_river_cloud.xiaozhi_downlink_drop_frame) == RIVER_OK &&
            river_audio_frame_ring_write(&g_river_cloud.xiaozhi_downlink_ring,
                                         (const uint8_t *)g_river_cloud.xiaozhi_downlink_mono) ==
                RIVER_OK) {
            g_river_cloud.xiaozhi_downlink_ring_dropped++;
            RIVER_LOGW("xiaozhi downlink ring overflow: dropped=%lu queued=%lu capacity=%u",
                       (unsigned long)g_river_cloud.xiaozhi_downlink_ring_dropped,
                       (unsigned long)river_audio_frame_ring_count(&g_river_cloud.xiaozhi_downlink_ring),
                       (unsigned int)RIVER_CLOUD_XIAOZHI_DOWNLINK_RING_FRAMES);
            status = RIVER_OK;
        }
    }

    g_river_cloud.xiaozhi_tts_stop_pending = false;
    g_river_cloud.xiaozhi_tts_stop_deadline_ms = 0U;
    return status;
}

static river_status_t river_cloud_xiaozhi_send_uplink_packet(const uint8_t *pcm,
                                                             size_t pcm_bytes)
{
    size_t packet_bytes = 0U;
    river_status_t status;

    status = river_opus_encode(&g_river_cloud.xiaozhi_encoder,
                               (const int16_t *)pcm,
                               pcm_bytes,
                               g_river_cloud.xiaozhi_uplink_packet,
                               sizeof(g_river_cloud.xiaozhi_uplink_packet),
                               &packet_bytes);
    if (status != RIVER_OK) {
        return status;
    }

    status = river_xiaozhi_send_audio(g_river_cloud.xiaozhi_uplink_packet,
                                      packet_bytes,
                                      g_river_cloud.xiaozhi_uplink_timestamp_ms);
    if (status == RIVER_OK) {
        g_river_cloud.xiaozhi_uplink_timestamp_ms +=
            g_river_cloud.xiaozhi_encoder.frame_duration_ms;
    }
    return status;
}

static river_status_t river_cloud_xiaozhi_queue_uplink_packet(const uint8_t *pcm,
                                                              size_t pcm_bytes)
{
    river_status_t status;

    if (pcm == NULL || pcm_bytes == 0U || !g_river_cloud.xiaozhi_uplink_ring.initialized) {
        return RIVER_ERR_ARG;
    }

    status = river_audio_frame_ring_write(&g_river_cloud.xiaozhi_uplink_ring, pcm);
    if (status == RIVER_OK) {
        return RIVER_OK;
    }

    if (river_audio_frame_ring_read(&g_river_cloud.xiaozhi_uplink_ring,
                                    g_river_cloud.xiaozhi_uplink_drop_frame) != RIVER_OK) {
        return status;
    }

    status = river_audio_frame_ring_write(&g_river_cloud.xiaozhi_uplink_ring, pcm);
    if (status == RIVER_OK) {
        g_river_cloud.xiaozhi_uplink_ring_dropped++;
        RIVER_LOGW("xiaozhi uplink ring overflow: dropped=%lu queued=%lu capacity=%u",
                   (unsigned long)g_river_cloud.xiaozhi_uplink_ring_dropped,
                   (unsigned long)river_audio_frame_ring_count(&g_river_cloud.xiaozhi_uplink_ring),
                   (unsigned int)RIVER_CLOUD_XIAOZHI_UPLINK_RING_FRAMES);
    }
    return status;
}

static river_status_t river_cloud_xiaozhi_push_pcm(const uint8_t *pcm, size_t pcm_bytes)
{
    size_t frame_bytes;

    if (pcm == NULL || pcm_bytes == 0U || g_river_cloud.xiaozhi_encoder.handle == NULL) {
        return RIVER_ERR_ARG;
    }

    frame_bytes = g_river_cloud.xiaozhi_encoder.pcm_frame_bytes;
    if ((g_river_cloud.xiaozhi_uplink_accum_bytes + pcm_bytes) >
        sizeof(g_river_cloud.xiaozhi_uplink_accum)) {
        return RIVER_ERR_BUSY;
    }

    memcpy(g_river_cloud.xiaozhi_uplink_accum + g_river_cloud.xiaozhi_uplink_accum_bytes,
           pcm,
           pcm_bytes);
    g_river_cloud.xiaozhi_uplink_accum_bytes += pcm_bytes;

    while (g_river_cloud.xiaozhi_uplink_accum_bytes >= frame_bytes) {
        river_status_t status;

        status = river_cloud_xiaozhi_queue_uplink_packet(g_river_cloud.xiaozhi_uplink_accum,
                                                         frame_bytes);
        if (status != RIVER_OK) {
            return status;
        }
        g_river_cloud.xiaozhi_uplink_accum_bytes -= frame_bytes;
        if (g_river_cloud.xiaozhi_uplink_accum_bytes > 0U) {
            memmove(g_river_cloud.xiaozhi_uplink_accum,
                    g_river_cloud.xiaozhi_uplink_accum + frame_bytes,
                    g_river_cloud.xiaozhi_uplink_accum_bytes);
        }
    }

    return RIVER_OK;
}

static river_status_t river_cloud_xiaozhi_flush_accumulator_padded(void)
{
    size_t frame_bytes;
    river_status_t status;

    if (g_river_cloud.xiaozhi_encoder.handle == NULL ||
        g_river_cloud.xiaozhi_uplink_accum_bytes == 0U) {
        return RIVER_OK;
    }

    frame_bytes = g_river_cloud.xiaozhi_encoder.pcm_frame_bytes;
    if (g_river_cloud.xiaozhi_uplink_accum_bytes < frame_bytes) {
        memset(g_river_cloud.xiaozhi_uplink_accum + g_river_cloud.xiaozhi_uplink_accum_bytes,
               0,
               frame_bytes - g_river_cloud.xiaozhi_uplink_accum_bytes);
    }
    status = river_cloud_xiaozhi_queue_uplink_packet(g_river_cloud.xiaozhi_uplink_accum,
                                                     frame_bytes);
    g_river_cloud.xiaozhi_uplink_accum_bytes = 0U;
    return status;
}

static void river_cloud_xiaozhi_finalize_listen_stop_if_ready(void)
{
    if (!g_river_cloud.xiaozhi_listen_stop_pending ||
        river_audio_frame_ring_count(&g_river_cloud.xiaozhi_uplink_ring) != 0U ||
        g_river_cloud.xiaozhi_uplink_accum_bytes != 0U) {
        return;
    }

    if (river_xiaozhi_session_open() && g_river_cloud.xiaozhi_listening) {
        (void)river_xiaozhi_send_listen_stop();
    }
    g_river_cloud.xiaozhi_listening = false;
    g_river_cloud.xiaozhi_listen_stop_pending = false;
}

static void river_cloud_xiaozhi_finish_active_stream(void)
{
    if (!g_river_cloud.stream_active) {
        return;
    }

    (void)river_cloud_xiaozhi_flush_accumulator_padded();
    g_river_cloud.xiaozhi_listen_stop_pending = true;
    river_cloud_xiaozhi_finalize_listen_stop_if_ready();
    river_cloud_xiaozhi_finalize_pending_text();
    g_river_cloud.stream_active = false;
    g_river_cloud.silence_frames = 0U;
    g_river_cloud.stream_started_ms = 0U;
    g_river_cloud.xiaozhi_open_speech_frames = 0U;
    g_river_cloud.xiaozhi_uplink_accum_bytes = 0U;
    g_river_cloud.xiaozhi_uplink_next_send_ms = 0U;
    river_cloud_pre_roll_reset();
    river_cloud_xiaozhi_emit_session_closed();
    river_runtime_stats_snapshot("asr_stream_finish");
}

static void river_cloud_xiaozhi_event_handler(const river_xiaozhi_event_t *event, void *user_data)
{
    (void)user_data;

    if (event == NULL) {
        return;
    }

    switch (event->type) {
    case RIVER_XIAOZHI_EVENT_SERVER_HELLO:
        g_river_cloud.xiaozhi_server_sample_rate =
            event->sample_rate != 0U ? event->sample_rate : 24000U;
        g_river_cloud.xiaozhi_server_frame_duration_ms =
            event->frame_duration_ms != 0U ? event->frame_duration_ms : 60U;
        river_cloud_xiaozhi_copy_session_id_from_transport();
        break;
    case RIVER_XIAOZHI_EVENT_STT:
        river_cloud_xiaozhi_window_touch(RIVER_CLOUD_XIAOZHI_WAKE_WINDOW_FOLLOWUP_MS,
                                         "stt");
        if (event->text != NULL && event->text[0] != '\0') {
            if (!g_river_cloud.xiaozhi_pending_text_valid ||
                strcmp(g_river_cloud.xiaozhi_pending_text, event->text) != 0) {
                snprintf(g_river_cloud.xiaozhi_pending_text,
                         sizeof(g_river_cloud.xiaozhi_pending_text),
                         "%s",
                         event->text);
                g_river_cloud.xiaozhi_pending_text_valid = true;
                g_river_cloud.xiaozhi_pending_text_finalized = false;
                river_cloud_emit_asr_result(RIVER_CLOUD_ASR_EVENT_PARTIAL,
                                            g_river_cloud.xiaozhi_pending_text,
                                            river_cloud_xiaozhi_current_sid(),
                                            NULL,
                                            0,
                                            false);
            }
        }
        break;
    case RIVER_XIAOZHI_EVENT_LLM:
        river_cloud_xiaozhi_window_touch(RIVER_CLOUD_XIAOZHI_WAKE_WINDOW_FOLLOWUP_MS,
                                         "llm");
        if (event->emotion != NULL || event->text != NULL) {
            RIVER_LOGI("xiaozhi llm emotion=%s text=%s",
                       event->emotion != NULL ? event->emotion : "-",
                       event->text != NULL ? event->text : "-");
        }
        river_cloud_xiaozhi_finalize_pending_text();
        break;
    case RIVER_XIAOZHI_EVENT_TTS:
        if (event->state != NULL && strcmp(event->state, "start") == 0) {
            river_cloud_xiaozhi_window_touch(RIVER_CLOUD_XIAOZHI_WAKE_WINDOW_FOLLOWUP_MS,
                                             "tts_start");
            river_cloud_xiaozhi_finalize_pending_text();
            g_river_cloud.xiaozhi_tts_stop_pending = false;
            g_river_cloud.xiaozhi_tts_stop_deadline_ms = 0U;
        } else if (event->state != NULL && strcmp(event->state, "sentence_start") == 0) {
            river_cloud_xiaozhi_window_touch(RIVER_CLOUD_XIAOZHI_WAKE_WINDOW_FOLLOWUP_MS,
                                             "tts_sentence");
            if (event->text != NULL && event->text[0] != '\0') {
                snprintf(g_river_cloud.last_text,
                         sizeof(g_river_cloud.last_text),
                         "%s",
                         event->text);
            }
            RIVER_LOGI("xiaozhi tts sentence_start: %s",
                       event->text != NULL ? event->text : "-");
        } else if (event->state != NULL && strcmp(event->state, "stop") == 0) {
            river_cloud_xiaozhi_finalize_pending_text();
            river_cloud_xiaozhi_window_touch(
                RIVER_CLOUD_XIAOZHI_POST_TTS_SILENCE_CLOSE_MS,
                "tts_stop");
            if (g_river_cloud.xiaozhi_playback_active) {
                g_river_cloud.xiaozhi_tts_stop_pending = true;
                g_river_cloud.xiaozhi_tts_stop_deadline_ms =
                    (uint64_t)rtos_time_get_current_system_time_ms() +
                    RIVER_CLOUD_XIAOZHI_PLAYBACK_DRAIN_MS;
            }
        }
        break;
    case RIVER_XIAOZHI_EVENT_AUDIO:
        (void)river_cloud_xiaozhi_handle_audio_event(event);
        break;
    case RIVER_XIAOZHI_EVENT_SESSION_CLOSED:
        river_cloud_xiaozhi_finalize_pending_text();
        g_river_cloud.stream_active = false;
        g_river_cloud.silence_frames = 0U;
        g_river_cloud.stream_started_ms = 0U;
        g_river_cloud.xiaozhi_open_speech_frames = 0U;
        g_river_cloud.xiaozhi_listening = false;
        g_river_cloud.xiaozhi_listen_stop_pending = false;
        g_river_cloud.xiaozhi_uplink_accum_bytes = 0U;
        g_river_cloud.xiaozhi_uplink_next_send_ms = 0U;
        river_audio_frame_ring_reset(&g_river_cloud.xiaozhi_uplink_ring);
        river_cloud_xiaozhi_reset_downlink_ring();
        river_cloud_pre_roll_reset();
        river_cloud_xiaozhi_emit_session_closed();
        if (g_river_cloud.xiaozhi_playback_active && !g_river_cloud.xiaozhi_tts_stop_pending) {
            g_river_cloud.xiaozhi_tts_stop_pending = true;
            g_river_cloud.xiaozhi_tts_stop_deadline_ms =
                (uint64_t)rtos_time_get_current_system_time_ms() +
                RIVER_CLOUD_XIAOZHI_PLAYBACK_DRAIN_MS;
        }
        river_cloud_xiaozhi_check_window_timeout();
        break;
    case RIVER_XIAOZHI_EVENT_ERROR:
        river_cloud_emit_asr_result(RIVER_CLOUD_ASR_EVENT_ERROR,
                                    NULL,
                                    river_cloud_xiaozhi_current_sid(),
                                    river_xiaozhi_last_error() != NULL ?
                                        river_xiaozhi_last_error() :
                                        "xiaozhi_transport_error",
                                    -1,
                                    true);
        break;
    default:
        break;
    }
}
#endif

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

void river_cloud_pre_roll_reset(void)
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
    river_runtime_stats_snapshot("asr_stream_finish");
    return status;
}

river_status_t river_cloud_adapter_init(void)
{
#if RIVER_CLOUD_BACKEND_IFLYTEK_ENABLED
    bool split_backend_ready = false;
#endif
#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    bool xiaozhi_backend_ready = false;
#endif

    if (g_river_cloud.initialized) {
        return RIVER_OK;
    }

    memset(&g_river_cloud, 0, sizeof(g_river_cloud));
#if RIVER_CLOUD_BACKEND_IFLYTEK_ENABLED
    g_river_cloud.provider = river_cloud_provider_default();
    if (g_river_cloud.provider == NULL) {
        RIVER_LOGE("no online asr provider registered");
        return RIVER_ERR_UNSUPPORTED;
    }
    if (g_river_cloud.provider->init(river_cloud_notify_result, &g_river_cloud) != RIVER_OK) {
        RIVER_LOGE("asr provider init failed");
        return RIVER_ERR_UNSUPPORTED;
    }
    split_backend_ready = true;
#else
    g_river_cloud.provider = NULL;
#endif

    river_cloud_start_sntp_if_needed();
    river_cloud_seed_time_from_build_if_needed();
#if RIVER_CLOUD_BACKEND_IFLYTEK_ENABLED
    RIVER_LOGI("online asr provider init: %s stream=%s batch=%s",
               river_cloud_asr_provider_name(),
               river_cloud_asr_streaming_supported() ? "yes" : "no",
               river_cloud_asr_batch_supported() ? "yes" : "no");
    if (river_tts_iflytek_init() != RIVER_OK) {
        RIVER_LOGE("tts provider init failed");
        return RIVER_ERR_UNSUPPORTED;
    }
    RIVER_LOGI("online tts provider init: iflytek_ws speak=yes scheme=ws");
#endif
#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    if (river_xiaozhi_init() == RIVER_OK) {
        xiaozhi_backend_ready = true;
        g_river_cloud.xiaozhi_enabled = river_xiaozhi_configured();
        (void)river_xiaozhi_set_event_handler(river_cloud_xiaozhi_event_handler, &g_river_cloud);
        river_cloud_xiaozhi_start_pump_if_needed();
        river_cloud_xiaozhi_start_downlink_if_needed();
        river_cloud_xiaozhi_start_uplink_if_needed();
        RIVER_LOGI("realtime session backend init: xiaozhi configured=%s",
                   g_river_cloud.xiaozhi_enabled ? "yes" : "no");
    } else {
        RIVER_LOGW("xiaozhi session backend init failed; keep fallback split cloud path only");
    }
    g_river_cloud.xiaozhi_server_sample_rate = 24000U;
    g_river_cloud.xiaozhi_server_frame_duration_ms = 60U;
#endif
#if RIVER_CLOUD_BACKEND_IFLYTEK_ENABLED && RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    if (!split_backend_ready && !xiaozhi_backend_ready) {
#elif RIVER_CLOUD_BACKEND_IFLYTEK_ENABLED
    if (!split_backend_ready) {
#elif RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    if (!xiaozhi_backend_ready) {
#endif
#if RIVER_CLOUD_BACKEND_IFLYTEK_ENABLED || RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
        RIVER_LOGE("no cloud backend initialized");
        return RIVER_ERR_UNSUPPORTED;
    }
#endif
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

river_status_t river_cloud_adapter_set_xiaozhi_config(const river_xiaozhi_config_t *config)
{
#if !RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    (void)config;
    return RIVER_ERR_UNSUPPORTED;
#else
    river_status_t status;

    if (config == NULL) {
        return RIVER_ERR_ARG;
    }

    if (!g_river_cloud.initialized) {
        status = river_cloud_adapter_init();
        if (status != RIVER_OK) {
            return status;
        }
    }

    if (g_river_cloud.audio_bridge_open || g_river_cloud.stream_active ||
        g_river_cloud.xiaozhi_listening || g_river_cloud.xiaozhi_playback_active ||
        g_river_cloud.xiaozhi_tts_stop_pending || river_xiaozhi_session_open()) {
        return RIVER_ERR_BUSY;
    }

    status = river_xiaozhi_set_config(config);
    if (status != RIVER_OK) {
        return status;
    }

    g_river_cloud.xiaozhi_enabled = river_xiaozhi_configured();
    if (g_river_cloud.xiaozhi_enabled) {
        river_cloud_xiaozhi_start_pump_if_needed();
        river_cloud_xiaozhi_start_downlink_if_needed();
        river_cloud_xiaozhi_start_uplink_if_needed();
    }

    g_river_cloud.xiaozhi_pending_text_valid = false;
    g_river_cloud.xiaozhi_pending_text_finalized = false;
    g_river_cloud.xiaozhi_pending_text[0] = '\0';
    g_river_cloud.xiaozhi_session_id[0] = '\0';
    river_cloud_xiaozhi_reset_playback_flags();
    RIVER_LOGI("realtime session backend refresh: xiaozhi configured=%s",
               g_river_cloud.xiaozhi_enabled ? "yes" : "no");
    return RIVER_OK;
#endif
}

void river_cloud_adapter_notify_network_ready(void)
{
    if (!g_river_cloud.initialized) {
        return;
    }

    river_cloud_seed_time_from_build_if_needed();
    river_cloud_kick_sntp_on_network_ready();
    river_cloud_log_time_ready_once();
    river_cloud_reset_stream_open_deferred_state();
}

void river_cloud_adapter_notify_network_lost(void)
{
    if (!g_river_cloud.initialized) {
        return;
    }

    river_cloud_reset_stream_open_deferred_state();

#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    if (river_cloud_xiaozhi_enabled()) {
        if (g_river_cloud.xiaozhi_playback_active && river_playback_service_active()) {
            (void)river_playback_service_stop_stream_ex("xiaozhi_network_lost");
        }

        g_river_cloud.stream_active = false;
        g_river_cloud.silence_frames = 0U;
        g_river_cloud.stream_started_ms = 0U;
        g_river_cloud.xiaozhi_open_speech_frames = 0U;
        g_river_cloud.xiaozhi_listening = false;
        g_river_cloud.xiaozhi_window_active = false;
        g_river_cloud.xiaozhi_listen_stop_pending = false;
        g_river_cloud.xiaozhi_window_deadline_ms = 0U;
        g_river_cloud.xiaozhi_uplink_accum_bytes = 0U;
        g_river_cloud.xiaozhi_uplink_next_send_ms = 0U;
        river_audio_frame_ring_reset(&g_river_cloud.xiaozhi_uplink_ring);
        river_cloud_xiaozhi_reset_downlink_ring();
        g_river_cloud.xiaozhi_pending_text_valid = false;
        g_river_cloud.xiaozhi_pending_text_finalized = false;
        g_river_cloud.xiaozhi_pending_text[0] = '\0';
        river_cloud_pre_roll_reset();
        river_cloud_xiaozhi_emit_session_closed();
        river_cloud_xiaozhi_reset_playback_flags();
        river_xiaozhi_close_session();
        g_river_cloud.xiaozhi_session_id[0] = '\0';
        return;
    }
#endif

    if (g_river_cloud.stream_active) {
        (void)river_cloud_stream_finish_active();
    }
}

river_status_t river_cloud_adapter_submit_text(const char *text)
{
#if RIVER_CLOUD_BACKEND_IFLYTEK_ENABLED
    river_status_t status;
#endif

    if (text == NULL) {
        return RIVER_ERR_ARG;
    }
    if (!g_river_cloud.initialized) {
        return RIVER_ERR_UNSUPPORTED;
    }
    if (!river_wifi_station_is_connected()) {
        return RIVER_ERR_BUSY;
    }
    if (!river_cloud_time_ready()) {
        return RIVER_ERR_BUSY;
    }

#if !RIVER_CLOUD_BACKEND_IFLYTEK_ENABLED
    return RIVER_ERR_UNSUPPORTED;
#else
    RIVER_LOGI("tts submit text=%s", text);
    river_runtime_stats_snapshot("tts_submit_start");
    status = river_tts_iflytek_submit_text(text);
    if (status == RIVER_OK) {
        river_runtime_stats_snapshot("tts_submit_finish");
    } else if (status == RIVER_ERR_BUSY) {
        river_runtime_stats_snapshot("tts_submit_interrupt");
    } else {
        river_runtime_stats_snapshot("tts_submit_fail");
    }
    return status;
#endif
}

river_status_t river_cloud_adapter_interrupt_tts_with_reason(const char *reason)
{
#if RIVER_CLOUD_BACKEND_IFLYTEK_ENABLED || RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    river_status_t status;
#endif

#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    if (river_cloud_xiaozhi_enabled() &&
        (g_river_cloud.xiaozhi_playback_active ||
         g_river_cloud.xiaozhi_tts_stop_pending ||
         g_river_cloud.xiaozhi_listening ||
         river_xiaozhi_session_open())) {
        status = RIVER_OK;
        if (river_playback_service_active()) {
            if (river_playback_service_interrupt_stream_ex(reason != NULL ? reason :
                                                               "xiaozhi_interrupt") != RIVER_OK) {
                status = RIVER_ERR_BUSY;
            }
        }
        river_cloud_xiaozhi_reset_playback_flags();
        if (river_xiaozhi_session_open()) {
            (void)river_xiaozhi_send_abort(reason);
        }
        RIVER_LOGI("tts interrupt requested: reason=%s", reason != NULL ? reason : "-");
        return status;
    }
#endif

#if !RIVER_CLOUD_BACKEND_IFLYTEK_ENABLED
    (void)reason;
    return RIVER_ERR_UNSUPPORTED;
#else
    status = river_tts_iflytek_request_stop_with_reason(reason);
    if (status == RIVER_OK) {
        RIVER_LOGI("tts interrupt requested: reason=%s", reason != NULL ? reason : "-");
    }
    return status;
#endif
}

river_status_t river_cloud_adapter_interrupt_tts(void)
{
    return river_cloud_adapter_interrupt_tts_with_reason(NULL);
}

const char *river_cloud_asr_provider_name(void)
{
    return river_cloud_active_provider_name();
}

bool river_cloud_asr_streaming_supported(void)
{
#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    if (river_cloud_xiaozhi_enabled()) {
        return true;
    }
#endif
    return (g_river_cloud.provider != NULL) &&
           g_river_cloud.provider->supports_streaming();
}

bool river_cloud_asr_batch_supported(void)
{
#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    if (river_cloud_xiaozhi_enabled()) {
        return false;
    }
#endif
    return (g_river_cloud.provider != NULL) &&
           g_river_cloud.provider->supports_batch();
}

river_status_t river_cloud_asr_audio_open(const river_cloud_asr_audio_desc_t *audio,
                                          uint32_t pre_roll_ms,
                                          uint32_t post_roll_ms)
{
    size_t pre_roll_bytes;
#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    river_status_t status;
#endif

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

#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    if (river_cloud_xiaozhi_enabled()) {
        uint32_t xiaozhi_pre_roll_frames =
            river_cloud_ms_to_frames(RIVER_CLOUD_XIAOZHI_PRE_ROLL_MAX_MS, audio->frame_ms);

        if (audio->sample_rate != RIVER_XIAOZHI_UPLINK_SAMPLE_RATE ||
            audio->channels != RIVER_XIAOZHI_UPLINK_CHANNELS ||
            audio->bits_per_sample != 16U) {
            river_cloud_asr_audio_close();
            return RIVER_ERR_UNSUPPORTED;
        }

        if (xiaozhi_pre_roll_frames > 0U &&
            g_river_cloud.pre_roll_capacity_frames > xiaozhi_pre_roll_frames) {
            g_river_cloud.pre_roll_capacity_frames = xiaozhi_pre_roll_frames;
        }

        status = river_opus_encoder_open(&g_river_cloud.xiaozhi_encoder,
                                         audio->sample_rate,
                                         audio->channels,
                                         RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS,
                                         24000);
        if (status != RIVER_OK) {
            river_cloud_asr_audio_close();
            return status;
        }

        if (g_river_cloud.xiaozhi_uplink_ring.initialized &&
            g_river_cloud.xiaozhi_uplink_ring.frame_bytes !=
                g_river_cloud.xiaozhi_encoder.pcm_frame_bytes) {
            river_audio_frame_ring_deinit(&g_river_cloud.xiaozhi_uplink_ring);
        }
        if (!g_river_cloud.xiaozhi_uplink_ring.initialized) {
            status = river_audio_frame_ring_init_with_storage_ex(
                &g_river_cloud.xiaozhi_uplink_ring,
                g_river_cloud.xiaozhi_uplink_ring_storage,
                sizeof(g_river_cloud.xiaozhi_uplink_ring_storage),
                g_river_cloud.xiaozhi_encoder.pcm_frame_bytes,
                RIVER_CLOUD_XIAOZHI_UPLINK_RING_FRAMES,
                RIVER_AUDIO_FRAME_RING_MODE_SPSC);
            if (status != RIVER_OK) {
                river_cloud_asr_audio_close();
                return status;
            }
        }
        river_audio_frame_ring_reset(&g_river_cloud.xiaozhi_uplink_ring);
        g_river_cloud.xiaozhi_uplink_ring_dropped = 0U;
        g_river_cloud.xiaozhi_uplink_timestamp_ms =
            (uint32_t)rtos_time_get_current_system_time_ms();
        g_river_cloud.xiaozhi_uplink_next_send_ms = 0U;
        g_river_cloud.xiaozhi_uplink_accum_bytes = 0U;
        g_river_cloud.xiaozhi_open_speech_frames = 0U;
        RIVER_LOGI("xiaozhi uplink audio: %luHz/%luch frame=%lums opus_pcm=%luB",
                   (unsigned long)audio->sample_rate,
                   (unsigned long)audio->channels,
                   (unsigned long)RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS,
                   (unsigned long)g_river_cloud.xiaozhi_encoder.pcm_frame_bytes);
        RIVER_LOGI("xiaozhi listen gate: pre_roll_cap=%lums open_hold_frames=%u",
                   (unsigned long)(g_river_cloud.pre_roll_capacity_frames * audio->frame_ms),
                   (unsigned int)RIVER_CLOUD_XIAOZHI_OPEN_HOLD_FRAMES);
    }
#endif

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
#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    if (river_cloud_xiaozhi_enabled()) {
        if (g_river_cloud.stream_active) {
            river_cloud_xiaozhi_finish_active_stream();
        }
        river_xiaozhi_close_session();
        river_opus_encoder_close(&g_river_cloud.xiaozhi_encoder);
        river_opus_decoder_close(&g_river_cloud.xiaozhi_decoder);
        river_audio_frame_ring_reset(&g_river_cloud.xiaozhi_uplink_ring);
        river_cloud_xiaozhi_reset_downlink_ring();
        g_river_cloud.xiaozhi_uplink_accum_bytes = 0U;
        g_river_cloud.xiaozhi_uplink_next_send_ms = 0U;
        g_river_cloud.xiaozhi_open_speech_frames = 0U;
        g_river_cloud.xiaozhi_listening = false;
        g_river_cloud.xiaozhi_listen_stop_pending = false;
        g_river_cloud.xiaozhi_pending_text_valid = false;
        g_river_cloud.xiaozhi_pending_text_finalized = false;
        g_river_cloud.xiaozhi_pending_text[0] = '\0';
        g_river_cloud.xiaozhi_session_id[0] = '\0';
        river_cloud_xiaozhi_reset_playback_flags();
    } else {
        river_cloud_stream_finish_active();
    }
#else
    river_cloud_stream_finish_active();
#endif

    if (g_river_cloud.provider != NULL && !river_cloud_xiaozhi_enabled()) {
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
#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    g_river_cloud.xiaozhi_open_speech_frames = 0U;
#endif
    river_cloud_pre_roll_reset();
    memset(&g_river_cloud.audio_desc, 0, sizeof(g_river_cloud.audio_desc));
}

#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
static river_status_t river_cloud_xiaozhi_stream_push_frame(const uint8_t *pcm,
                                                            size_t bytes,
                                                            bool is_speech)
{
    river_status_t status;

    if (!g_river_cloud.audio_bridge_open || pcm == NULL || bytes != g_river_cloud.frame_bytes) {
        return RIVER_ERR_ARG;
    }

    river_cloud_xiaozhi_check_pending_playback_stop();
    river_cloud_xiaozhi_check_window_timeout();
    river_cloud_log_time_ready_once();

    if (!g_river_cloud.stream_active &&
        river_playback_service_active() &&
        !river_cloud_xiaozhi_playback_allows_vad_open()) {
        g_river_cloud.xiaozhi_open_speech_frames = 0U;
        river_cloud_pre_roll_reset();
        return RIVER_OK;
    }

    river_cloud_pre_roll_store(pcm);
    if (!g_river_cloud.xiaozhi_window_active &&
        river_cloud_xiaozhi_idle_requires_wakeword()) {
        g_river_cloud.xiaozhi_open_speech_frames = 0U;
        return RIVER_OK;
    }

    if (!g_river_cloud.stream_active) {
        uint32_t pre_roll_frames_before_open;
        uint32_t read_index;
        uint32_t frame_index;

        if (!is_speech) {
            g_river_cloud.xiaozhi_open_speech_frames = 0U;
            return RIVER_OK;
        }

        if (g_river_cloud.xiaozhi_open_speech_frames < UINT32_MAX) {
            g_river_cloud.xiaozhi_open_speech_frames++;
        }
        if (g_river_cloud.xiaozhi_open_speech_frames < RIVER_CLOUD_XIAOZHI_OPEN_HOLD_FRAMES) {
            return RIVER_OK;
        }

        pre_roll_frames_before_open = g_river_cloud.pre_roll_count_frames;
        status = river_cloud_xiaozhi_open_session_and_listen();
        if (status != RIVER_OK) {
            g_river_cloud.stream_open_fail++;
            g_river_cloud.xiaozhi_open_speech_frames = 0U;
            river_cloud_log_stream_open_deferred_once(status);
            return status;
        }

        if (pre_roll_frames_before_open == g_river_cloud.pre_roll_capacity_frames) {
            read_index = g_river_cloud.pre_roll_write_index_frames;
        } else {
            read_index = 0U;
        }

        for (frame_index = 0U; frame_index < pre_roll_frames_before_open; ++frame_index) {
            const uint8_t *src = g_river_cloud.pre_roll_buffer +
                                 ((size_t)read_index * g_river_cloud.frame_bytes);

            status = river_cloud_xiaozhi_push_pcm(src, g_river_cloud.frame_bytes);
            if (status != RIVER_OK) {
                g_river_cloud.stream_feed_fail++;
                return status;
            }
            g_river_cloud.stream_feed_ok++;

            read_index++;
            if (read_index >= g_river_cloud.pre_roll_capacity_frames) {
                read_index = 0U;
            }
        }
        river_cloud_pre_roll_reset();

        if (pre_roll_frames_before_open == 0U) {
            status = river_cloud_xiaozhi_push_pcm(pcm, bytes);
            if (status != RIVER_OK) {
                g_river_cloud.stream_feed_fail++;
                return status;
            }
            g_river_cloud.stream_feed_ok++;
        }

        river_cloud_reset_stream_open_deferred_state();
        g_river_cloud.stream_active = true;
        g_river_cloud.xiaozhi_open_speech_frames = 0U;
        g_river_cloud.stream_open_ok++;
        g_river_cloud.silence_frames = 0U;
        g_river_cloud.stream_started_ms = rtos_time_get_current_system_time_ms();
        RIVER_LOGI("asr stream active: provider=%s pre_roll_frames=%lu",
                   river_cloud_asr_provider_name(),
                   (unsigned long)pre_roll_frames_before_open);
        river_runtime_stats_snapshot("asr_stream_active");
        river_cloud_xiaozhi_check_pending_playback_stop();
        return RIVER_OK;
    }

    status = river_cloud_xiaozhi_push_pcm(pcm, bytes);
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
            river_cloud_xiaozhi_finish_active_stream();
        }
    }

    river_cloud_xiaozhi_check_pending_playback_stop();
    return RIVER_OK;
}
#endif

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

#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    if (river_cloud_xiaozhi_enabled()) {
        return river_cloud_xiaozhi_stream_push_frame(pcm, bytes, is_speech);
    }
#endif

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
            river_runtime_stats_snapshot("asr_stream_active");
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
#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    if (river_cloud_xiaozhi_enabled()) {
        g_river_cloud.batch_submit_unsupported++;
        return RIVER_ERR_UNSUPPORTED;
    }
#endif
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
#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    {
        uint64_t now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
        uint64_t remaining_ms =
            (g_river_cloud.xiaozhi_window_active &&
             g_river_cloud.xiaozhi_window_deadline_ms > now_ms) ?
                (g_river_cloud.xiaozhi_window_deadline_ms - now_ms) :
                0U;
        RIVER_LOGI("xiaozhi runtime enabled=%s pump=%s session=%s listening=%s playback=%s stop_pending=%s window=%s followup_left_ms=%lu wake_admission=%s sid=%s pending_text=%s",
                   g_river_cloud.xiaozhi_enabled ? "yes" : "no",
                   g_river_cloud.xiaozhi_pump_started ? "running" : "off",
                   river_xiaozhi_session_open() ? "open" : "closed",
                   g_river_cloud.xiaozhi_listening ? "yes" : "no",
                   g_river_cloud.xiaozhi_playback_active ? "yes" : "no",
                   g_river_cloud.xiaozhi_tts_stop_pending ? "yes" : "no",
                   g_river_cloud.xiaozhi_window_active ? "yes" : "no",
                   (unsigned long)remaining_ms,
                   river_cloud_xiaozhi_idle_requires_wakeword() ? "wakeword" : "legacy_vad",
                   g_river_cloud.xiaozhi_session_id[0] != '\0' ? g_river_cloud.xiaozhi_session_id : "-",
                   g_river_cloud.xiaozhi_pending_text_valid ? g_river_cloud.xiaozhi_pending_text : "-");
    }
    RIVER_LOGI("xiaozhi uplink queue=%lu/%u dropped=%lu stop_pending=%s worker=%s",
               (unsigned long)river_audio_frame_ring_count(&g_river_cloud.xiaozhi_uplink_ring),
               (unsigned int)RIVER_CLOUD_XIAOZHI_UPLINK_RING_FRAMES,
               (unsigned long)g_river_cloud.xiaozhi_uplink_ring_dropped,
               g_river_cloud.xiaozhi_listen_stop_pending ? "yes" : "no",
               g_river_cloud.xiaozhi_uplink_started ? "running" : "off");
    RIVER_LOGI("xiaozhi downlink queue=%lu/%u dropped=%lu worker=%s sample=%luHz frame=%lums",
               (unsigned long)river_audio_frame_ring_count(&g_river_cloud.xiaozhi_downlink_ring),
               (unsigned int)RIVER_CLOUD_XIAOZHI_DOWNLINK_RING_FRAMES,
               (unsigned long)g_river_cloud.xiaozhi_downlink_ring_dropped,
               g_river_cloud.xiaozhi_downlink_started ? "running" : "off",
               (unsigned long)g_river_cloud.xiaozhi_downlink_sample_rate,
               (unsigned long)g_river_cloud.xiaozhi_downlink_frame_duration_ms);
#else
    RIVER_LOGI("xiaozhi runtime compiled=no");
#endif
    if (g_river_cloud.provider != NULL) {
        g_river_cloud.provider->dump_status();
    }
#if RIVER_CLOUD_BACKEND_IFLYTEK_ENABLED
    river_tts_iflytek_dump_status();
#endif
#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    river_xiaozhi_dump_status();
#endif
}

river_status_t river_cloud_adapter_begin_conversation_window(const char *source)
{
#if !RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    (void)source;
    return RIVER_ERR_UNSUPPORTED;
#else
    return river_cloud_xiaozhi_begin_conversation_window(source);
#endif
}

bool river_cloud_adapter_conversation_window_active(void)
{
#if !RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    return false;
#else
    return g_river_cloud.xiaozhi_enabled && g_river_cloud.xiaozhi_window_active;
#endif
}
