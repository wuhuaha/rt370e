/* 讯飞 TTS WebSocket 接入：负责文本下发、音频接收与播放投递。 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "os_wrapper.h"
#include "websocket/libwsclient.h"
#include "websocket/wsclient_api.h"

#include "river/river_audio_frame_ring.h"
#include "river/river_log.h"
#include "river/river_playback_service.h"
#include "river/river_tts_iflytek_credentials.h"
#include "river/river_wifi_station.h"
#include "river_asr_provider_internal.h"
#include "river_tts_internal.h"
#include "river_ws_dispatch.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.cloud.tts"

#define RIVER_IFLYTEK_TTS_URL_MAX                  1024U
#define RIVER_IFLYTEK_TTS_DATE_MAX                 64U
#define RIVER_IFLYTEK_TTS_SIGNATURE_ORIGIN_MAX     256U
#define RIVER_IFLYTEK_TTS_SIGNATURE_B64_MAX        64U
#define RIVER_IFLYTEK_TTS_AUTH_ORIGIN_MAX          256U
#define RIVER_IFLYTEK_TTS_AUTH_B64_MAX             384U
#define RIVER_IFLYTEK_TTS_TEXT_B64_MAX             11000U
#define RIVER_IFLYTEK_TTS_REQUEST_MAX              12288U
#define RIVER_IFLYTEK_TTS_TEXT_MAX                 8000U
#define RIVER_IFLYTEK_TTS_TX_MAX                   12288U
#define RIVER_IFLYTEK_TTS_RX_MAX                   53248U
#define RIVER_IFLYTEK_TTS_QUEUE_MAX                4U
#define RIVER_IFLYTEK_TTS_SESSION_TIMEOUT_MS       30000U
#define RIVER_IFLYTEK_TTS_POLL_INTERVAL_MS         10U
#define RIVER_IFLYTEK_TTS_SAMPLE_RATE              16000U
#define RIVER_IFLYTEK_TTS_FRAME_MS                 16U
#define RIVER_IFLYTEK_TTS_MONO_FRAME_SAMPLES       256U
#define RIVER_IFLYTEK_TTS_MONO_FRAME_BYTES         (RIVER_IFLYTEK_TTS_MONO_FRAME_SAMPLES * 2U)
#define RIVER_IFLYTEK_TTS_STEREO_FRAME_BYTES       (RIVER_IFLYTEK_TTS_MONO_FRAME_BYTES * 2U)
#define RIVER_IFLYTEK_TTS_PLAYBACK_BUFFER_FRAMES   16U
#define RIVER_IFLYTEK_TTS_START_BUFFER_FRAMES      24U
#define RIVER_IFLYTEK_TTS_START_BUFFER_BYTES       (RIVER_IFLYTEK_TTS_MONO_FRAME_BYTES * RIVER_IFLYTEK_TTS_START_BUFFER_FRAMES)
#define RIVER_IFLYTEK_TTS_PCM_QUEUE_MS             1280U
#define RIVER_IFLYTEK_TTS_PCM_QUEUE_FRAMES         (RIVER_IFLYTEK_TTS_PCM_QUEUE_MS / RIVER_IFLYTEK_TTS_FRAME_MS)
#define RIVER_IFLYTEK_TTS_QUEUE_WAIT_MS            20U
#define RIVER_IFLYTEK_TTS_QUEUE_SPACE_RETRY_MAX    24U
#define RIVER_IFLYTEK_TTS_FEEDER_TASK_STACK        (1024U * 4U)
#define RIVER_IFLYTEK_TTS_FEEDER_TASK_PRIORITY     5U
#define RIVER_IFLYTEK_TTS_FEEDER_STOP_TIMEOUT_MS   12000U
#define RIVER_IFLYTEK_TTS_DRAIN_MARGIN_MS          320U
#define RIVER_IFLYTEK_TTS_DRAIN_MIN_MS             640U
#define RIVER_IFLYTEK_TTS_DRAIN_MAX_MS             2500U
#define RIVER_IFLYTEK_TTS_REF_HISTORY_MS           2048U
#define RIVER_IFLYTEK_TTS_PCM_QUEUE_STORAGE_BYTES  (RIVER_IFLYTEK_TTS_MONO_FRAME_BYTES * RIVER_IFLYTEK_TTS_PCM_QUEUE_FRAMES)
#define RIVER_IFLYTEK_TTS_RESOURCE_POOL_BYTES      (RIVER_IFLYTEK_TTS_RX_MAX + RIVER_IFLYTEK_TTS_PCM_QUEUE_STORAGE_BYTES)

typedef struct {
    bool initialized;
    bool running;
    bool playback_started;
    bool feeder_running;
    bool completed;
    bool ws_closed;
    bool saw_audio;
    bool input_done;
    bool stop_requested;
    bool interrupted;
    bool failed;
    int last_code;
    uint32_t sessions_started;
    uint32_t sessions_ok;
    uint32_t sessions_fail;
    uint32_t audio_chunks;
    uint32_t audio_bytes;
    uint32_t playback_write_ok;
    uint32_t playback_write_fail;
    uint32_t decode_fail;
    uint32_t open_fail;
    uint32_t send_fail;
    uint32_t timeout_fail;
    uint32_t queue_peak_bytes;
    uint32_t queue_overflow;
    uint32_t queue_starve_count;
    uint32_t playback_epoch;
    rtos_mutex_t lock;
    rtos_sema_t queue_ready;
    rtos_sema_t queue_space;
    rtos_task_t feeder_task;
    wsclient_context *wsclient;
    uint8_t *resource_block;
    size_t resource_block_bytes;
    uint8_t *decode_buffer;
    size_t decode_buffer_capacity;
    uint8_t *pcm_queue_storage;
    river_audio_frame_ring_t pcm_ring;
    size_t mono_pending_bytes;
    uint8_t mono_pending[RIVER_IFLYTEK_TTS_MONO_FRAME_BYTES];
    uint8_t stereo_frame[RIVER_IFLYTEK_TTS_STEREO_FRAME_BYTES];
    char last_sid[80];
    char last_error[128];
    char last_interrupt_reason[64];
    char last_text[96];
} river_iflytek_tts_context_t;

static river_iflytek_tts_context_t g_river_iflytek_tts;

static void river_tts_feeder_task(void *param);

static void river_tts_reset_binary_sema(rtos_sema_t sema)
{
    while (rtos_sema_take(sema, 0U) == RTK_SUCCESS) {
    }
}

static bool river_tts_session_interrupted(void)
{
    return g_river_iflytek_tts.stop_requested || g_river_iflytek_tts.interrupted;
}

static void river_tts_set_last_interrupt_reason(const char *reason)
{
    snprintf(g_river_iflytek_tts.last_interrupt_reason,
             sizeof(g_river_iflytek_tts.last_interrupt_reason),
             "%s",
             (reason != NULL && reason[0] != '\0') ? reason : "tts_interrupted");
}

static const char *river_tts_last_interrupt_reason(void)
{
    if (g_river_iflytek_tts.last_interrupt_reason[0] == '\0') {
        return "tts_interrupted";
    }
    return g_river_iflytek_tts.last_interrupt_reason;
}

static size_t river_tts_url_encode(const char *src, char *dst, size_t dst_size)
{
    static const char k_hex[] = "0123456789ABCDEF";
    size_t written;

    if (src == NULL || dst == NULL || dst_size == 0U) {
        return 0U;
    }

    written = 0U;
    while (*src != '\0' && (written + 1U) < dst_size) {
        unsigned char ch;

        ch = (unsigned char)(*src++);
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' ||
            ch == '.' || ch == '~') {
            dst[written++] = (char)ch;
        } else {
            if ((written + 3U) >= dst_size) {
                break;
            }
            dst[written++] = '%';
            dst[written++] = k_hex[(ch >> 4) & 0x0F];
            dst[written++] = k_hex[ch & 0x0F];
        }
    }

    dst[written] = '\0';
    return written;
}

static bool river_tts_build_rfc1123_date(char *buffer, size_t buffer_size)
{
    static const char *const k_weekdays[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const char *const k_months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    uint32_t utc_seconds;
    int64_t days;
    int64_t seconds_of_day;
    int64_t z;
    int64_t era;
    uint32_t doe;
    uint32_t yoe;
    uint32_t doy;
    uint32_t mp;
    int year;
    int month;
    int day;
    int weekday;
    int hour;
    int minute;
    int second;

    if (buffer == NULL || buffer_size == 0U) {
        return false;
    }

    utc_seconds = river_cloud_now_utc_seconds();
    if (utc_seconds == 0U) {
        return false;
    }

    days = (int64_t)utc_seconds / 86400LL;
    seconds_of_day = (int64_t)utc_seconds % 86400LL;
    weekday = (int)((days + 4LL) % 7LL);
    if (weekday < 0) {
        weekday += 7;
    }

    z = days + 719468LL;
    era = (z >= 0LL) ? (z / 146097LL) : ((z - 146096LL) / 146097LL);
    doe = (uint32_t)(z - era * 146097LL);
    yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
    year = (int)yoe + (int)(era * 400LL);
    doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
    mp = (5U * doy + 2U) / 153U;
    day = (int)(doy - (153U * mp + 2U) / 5U + 1U);
    month = (int)mp + 3;
    if (month > 12) {
        month -= 12;
    }
    year += (month <= 2) ? 1 : 0;

    hour = (int)(seconds_of_day / 3600LL);
    minute = (int)((seconds_of_day % 3600LL) / 60LL);
    second = (int)(seconds_of_day % 60LL);

    snprintf(buffer,
             buffer_size,
             "%s, %02d %s %04d %02d:%02d:%02d GMT",
             k_weekdays[weekday],
             day,
             k_months[month - 1],
             year,
             hour,
             minute,
             second);
    return true;
}

static river_status_t river_tts_build_auth_url(char *url, size_t url_size)
{
    unsigned char signature_raw[32];
    char date_text[RIVER_IFLYTEK_TTS_DATE_MAX];
    char signature_origin[RIVER_IFLYTEK_TTS_SIGNATURE_ORIGIN_MAX];
    char signature_b64[RIVER_IFLYTEK_TTS_SIGNATURE_B64_MAX];
    char auth_origin[RIVER_IFLYTEK_TTS_AUTH_ORIGIN_MAX];
    char auth_b64[RIVER_IFLYTEK_TTS_AUTH_B64_MAX];
    char auth_encoded[RIVER_IFLYTEK_TTS_AUTH_B64_MAX * 3U];
    char date_encoded[RIVER_IFLYTEK_TTS_DATE_MAX * 3U];
    char host_encoded[128];
    const mbedtls_md_info_t *md_info;
    size_t output_len;

    if (url == NULL || url_size == 0U) {
        return RIVER_ERR_ARG;
    }
    if (!river_tts_build_rfc1123_date(date_text, sizeof(date_text))) {
        return RIVER_ERR_BUSY;
    }

    snprintf(signature_origin,
             sizeof(signature_origin),
             "host: %s\ndate: %s\nGET %s HTTP/1.1",
             RIVER_IFLYTEK_TTS_HOST,
             date_text,
             RIVER_IFLYTEK_TTS_PATH);

    md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md_info == NULL) {
        return RIVER_ERR_UNSUPPORTED;
    }
    if (mbedtls_md_hmac(md_info,
                        (const unsigned char *)RIVER_IFLYTEK_TTS_API_SECRET,
                        strlen(RIVER_IFLYTEK_TTS_API_SECRET),
                        (const unsigned char *)signature_origin,
                        strlen(signature_origin),
                        signature_raw) != 0) {
        return RIVER_ERR_IO;
    }

    output_len = 0U;
    if (mbedtls_base64_encode((unsigned char *)signature_b64,
                              sizeof(signature_b64),
                              &output_len,
                              signature_raw,
                              sizeof(signature_raw)) != 0) {
        return RIVER_ERR_IO;
    }
    signature_b64[output_len] = '\0';

    snprintf(auth_origin,
             sizeof(auth_origin),
             "api_key=\"%s\", algorithm=\"hmac-sha256\", headers=\"host date request-line\", signature=\"%s\"",
             RIVER_IFLYTEK_TTS_API_KEY,
             signature_b64);

    output_len = 0U;
    if (mbedtls_base64_encode((unsigned char *)auth_b64,
                              sizeof(auth_b64),
                              &output_len,
                              (const unsigned char *)auth_origin,
                              strlen(auth_origin)) != 0) {
        return RIVER_ERR_IO;
    }
    auth_b64[output_len] = '\0';

    river_tts_url_encode(auth_b64, auth_encoded, sizeof(auth_encoded));
    river_tts_url_encode(date_text, date_encoded, sizeof(date_encoded));
    river_tts_url_encode(RIVER_IFLYTEK_TTS_HOST, host_encoded, sizeof(host_encoded));

    snprintf(url,
             url_size,
             "%s://%s%s?authorization=%s&date=%s&host=%s",
             RIVER_IFLYTEK_TTS_SCHEME,
             RIVER_IFLYTEK_TTS_HOST,
             RIVER_IFLYTEK_TTS_PATH,
             auth_encoded,
             date_encoded,
             host_encoded);
    return RIVER_OK;
}

static void river_tts_set_last_error(const char *message)
{
    snprintf(g_river_iflytek_tts.last_error,
             sizeof(g_river_iflytek_tts.last_error),
             "%s",
             message != NULL ? message : "-");
}

static void river_tts_set_last_text_preview(const char *text)
{
    if (text == NULL) {
        g_river_iflytek_tts.last_text[0] = '\0';
        return;
    }
    snprintf(g_river_iflytek_tts.last_text, sizeof(g_river_iflytek_tts.last_text), "%s", text);
}

static river_status_t river_tts_prepare_resource_pool(void)
{
    river_status_t status;

    if (g_river_iflytek_tts.resource_block != NULL &&
        g_river_iflytek_tts.decode_buffer != NULL &&
        g_river_iflytek_tts.pcm_queue_storage != NULL &&
        g_river_iflytek_tts.pcm_ring.initialized) {
        return RIVER_OK;
    }

    g_river_iflytek_tts.resource_block =
        (uint8_t *)rtos_mem_zmalloc((uint32_t)RIVER_IFLYTEK_TTS_RESOURCE_POOL_BYTES);
    if (g_river_iflytek_tts.resource_block == NULL) {
        return RIVER_ERR_NO_MEMORY;
    }

    g_river_iflytek_tts.resource_block_bytes = RIVER_IFLYTEK_TTS_RESOURCE_POOL_BYTES;
    g_river_iflytek_tts.decode_buffer = g_river_iflytek_tts.resource_block;
    g_river_iflytek_tts.decode_buffer_capacity = RIVER_IFLYTEK_TTS_RX_MAX;
    g_river_iflytek_tts.pcm_queue_storage =
        g_river_iflytek_tts.resource_block + RIVER_IFLYTEK_TTS_RX_MAX;

    status = river_audio_frame_ring_init_with_storage_ex(&g_river_iflytek_tts.pcm_ring,
                                                         g_river_iflytek_tts.pcm_queue_storage,
                                                         RIVER_IFLYTEK_TTS_PCM_QUEUE_STORAGE_BYTES,
                                                         RIVER_IFLYTEK_TTS_MONO_FRAME_BYTES,
                                                         RIVER_IFLYTEK_TTS_PCM_QUEUE_FRAMES,
                                                         RIVER_AUDIO_FRAME_RING_MODE_SPSC);
    if (status != RIVER_OK) {
        rtos_mem_free(g_river_iflytek_tts.resource_block);
        g_river_iflytek_tts.resource_block = NULL;
        g_river_iflytek_tts.resource_block_bytes = 0U;
        g_river_iflytek_tts.decode_buffer = NULL;
        g_river_iflytek_tts.decode_buffer_capacity = 0U;
        g_river_iflytek_tts.pcm_queue_storage = NULL;
        return status;
    }

    return RIVER_OK;
}

static river_status_t river_tts_ensure_decode_buffer(size_t capacity)
{
    if (capacity == 0U) {
        return RIVER_ERR_ARG;
    }
    if (river_tts_prepare_resource_pool() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    if (g_river_iflytek_tts.decode_buffer_capacity >= capacity &&
        g_river_iflytek_tts.decode_buffer != NULL) {
        return RIVER_OK;
    }

    return RIVER_ERR_NO_MEMORY;
}

static river_status_t river_tts_ensure_pcm_queue(void)
{
    return river_tts_prepare_resource_pool();
}

static void river_tts_queue_reset(void)
{
    river_audio_frame_ring_reset(&g_river_iflytek_tts.pcm_ring);
    g_river_iflytek_tts.mono_pending_bytes = 0U;
}

static size_t river_tts_queue_count(void)
{
    return (size_t)river_audio_frame_ring_count(&g_river_iflytek_tts.pcm_ring) *
           RIVER_IFLYTEK_TTS_MONO_FRAME_BYTES;
}

static river_status_t river_tts_queue_write(const uint8_t *pcm, size_t bytes)
{
    size_t offset;

    if (pcm == NULL || bytes == 0U) {
        return RIVER_OK;
    }
    if (river_tts_ensure_pcm_queue() != RIVER_OK) {
        river_tts_set_last_error("tts_pcm_queue_alloc_failed");
        return RIVER_ERR_NO_MEMORY;
    }
    offset = 0U;
    while (offset < bytes) {
        river_status_t status;
        size_t copy_bytes;

        copy_bytes = RIVER_IFLYTEK_TTS_MONO_FRAME_BYTES - g_river_iflytek_tts.mono_pending_bytes;
        if (copy_bytes > (bytes - offset)) {
            copy_bytes = bytes - offset;
        }

        memcpy(g_river_iflytek_tts.mono_pending + g_river_iflytek_tts.mono_pending_bytes,
               pcm + offset,
               copy_bytes);
        g_river_iflytek_tts.mono_pending_bytes += copy_bytes;
        offset += copy_bytes;

        if (g_river_iflytek_tts.mono_pending_bytes != RIVER_IFLYTEK_TTS_MONO_FRAME_BYTES) {
            continue;
        }

        status = river_audio_frame_ring_write(&g_river_iflytek_tts.pcm_ring,
                                              g_river_iflytek_tts.mono_pending);
        if (status == RIVER_ERR_NO_MEMORY) {
            uint32_t retry;

            for (retry = 0U; retry < RIVER_IFLYTEK_TTS_QUEUE_SPACE_RETRY_MAX; ++retry) {
                if (river_tts_session_interrupted()) {
                    river_tts_set_last_error("tts_interrupted");
                    return RIVER_ERR_BUSY;
                }

                (void)rtos_sema_take(g_river_iflytek_tts.queue_space,
                                     RIVER_IFLYTEK_TTS_QUEUE_WAIT_MS);
                status = river_audio_frame_ring_write(&g_river_iflytek_tts.pcm_ring,
                                                      g_river_iflytek_tts.mono_pending);
                if (status == RIVER_OK) {
                    break;
                }
            }
        }
        if (status != RIVER_OK) {
            g_river_iflytek_tts.queue_overflow++;
            river_tts_set_last_error("tts_pcm_queue_overflow");
            return status;
        }

        g_river_iflytek_tts.mono_pending_bytes = 0U;
        g_river_iflytek_tts.queue_peak_bytes =
            river_audio_frame_ring_peak_count(&g_river_iflytek_tts.pcm_ring) *
            RIVER_IFLYTEK_TTS_MONO_FRAME_BYTES;
        rtos_sema_give(g_river_iflytek_tts.queue_ready);
    }

    return RIVER_OK;
}

static river_status_t river_tts_queue_read_frame(uint8_t *buffer)
{
    if (buffer == NULL) {
        return RIVER_ERR_ARG;
    }

    return river_audio_frame_ring_read(&g_river_iflytek_tts.pcm_ring, buffer);
}

static river_status_t river_tts_prepare_playback(void)
{
    river_playback_stream_config_t config;

    if (river_playback_service_active()) {
        (void)river_playback_service_interrupt_stream_ex("tts_prepare_preempt");
    }

    memset(&config, 0, sizeof(config));
    config.stream_name = "iflytek_tts";
    config.priority = RIVER_PLAYBACK_PRIO_TTS;
    config.sample_rate = RIVER_IFLYTEK_TTS_SAMPLE_RATE;
    config.frame_ms = RIVER_IFLYTEK_TTS_FRAME_MS;
    config.playback_channels = 2U;
    config.bits_per_sample = 16U;
    config.playback_frame_bytes = RIVER_IFLYTEK_TTS_STEREO_FRAME_BYTES;
    config.buffer_frame_count = RIVER_IFLYTEK_TTS_PLAYBACK_BUFFER_FRAMES;
    config.volume_left = 0.85f;
    config.volume_right = 0.85f;
    config.reference_export = true;
    config.reference_channels = 1U;
    config.reference_frame_bytes = RIVER_IFLYTEK_TTS_MONO_FRAME_BYTES;
    config.reference_history_ms = RIVER_IFLYTEK_TTS_REF_HISTORY_MS;

    return river_playback_service_start_stream(&config);
}

static uint32_t river_tts_compute_drain_wait_ms(void)
{
    river_playback_service_stats_t stats;
    uint32_t bytes_per_ms;
    uint32_t wait_ms;

    river_playback_service_get_stats(&stats);
    bytes_per_ms = (RIVER_IFLYTEK_TTS_SAMPLE_RATE * 2U * 2U) / 1000U;
    if (bytes_per_ms == 0U || stats.track_buffer_bytes == 0U) {
        return RIVER_IFLYTEK_TTS_DRAIN_MIN_MS;
    }

    /*
     * Ameba's AudioTrack stop/flush path tends to cut the last tail if we stop
     * right after feeder completion. Budget for one extra track-sized chunk to
     * cover driver/DMA in-flight audio before issuing stop_stream().
     */
    wait_ms = (uint32_t)(((uint64_t)stats.track_buffer_bytes * 2ULL) / (uint64_t)bytes_per_ms);
    wait_ms += RIVER_IFLYTEK_TTS_DRAIN_MARGIN_MS;
    if (wait_ms < RIVER_IFLYTEK_TTS_DRAIN_MIN_MS) {
        wait_ms = RIVER_IFLYTEK_TTS_DRAIN_MIN_MS;
    }
    if (wait_ms > RIVER_IFLYTEK_TTS_DRAIN_MAX_MS) {
        wait_ms = RIVER_IFLYTEK_TTS_DRAIN_MAX_MS;
    }

    return wait_ms;
}

static river_status_t river_tts_write_aligned_frame(const uint8_t *mono_frame)
{
    const int16_t *mono_samples;
    int16_t *stereo_samples;
    uint32_t index;

    mono_samples = (const int16_t *)mono_frame;
    stereo_samples = (int16_t *)g_river_iflytek_tts.stereo_frame;
    for (index = 0U; index < RIVER_IFLYTEK_TTS_MONO_FRAME_SAMPLES; ++index) {
        stereo_samples[index * 2U] = mono_samples[index];
        stereo_samples[index * 2U + 1U] = mono_samples[index];
    }

    if (g_river_iflytek_tts.playback_epoch == 0U ||
        river_playback_service_epoch() != g_river_iflytek_tts.playback_epoch) {
        river_tts_set_last_error(g_river_iflytek_tts.interrupted || g_river_iflytek_tts.stop_requested ?
                                     "tts_interrupted" :
                                     "playback_epoch_changed");
        return RIVER_ERR_BUSY;
    }

    if (river_playback_service_write(g_river_iflytek_tts.stereo_frame,
                                     RIVER_IFLYTEK_TTS_STEREO_FRAME_BYTES,
                                     mono_frame,
                                     RIVER_IFLYTEK_TTS_MONO_FRAME_BYTES,
                                     true) != RIVER_OK) {
        g_river_iflytek_tts.playback_write_fail++;
        river_tts_set_last_error("playback_write_failed");
        return RIVER_ERR_IO;
    }

    g_river_iflytek_tts.playback_write_ok++;
    return RIVER_OK;
}

static river_status_t river_tts_feed_ready_audio(void)
{
    uint8_t mono_frame[RIVER_IFLYTEK_TTS_MONO_FRAME_BYTES];

    while (1) {
        river_status_t status;

        status = river_tts_queue_read_frame(mono_frame);
        if (status == RIVER_ERR_NOT_FOUND) {
            return RIVER_OK;
        }
        if (status != RIVER_OK) {
            river_tts_set_last_error("tts_pcm_queue_read_failed");
            return status;
        }

        status = river_tts_write_aligned_frame(mono_frame);
        if (status != RIVER_OK) {
            return status;
        }

        rtos_sema_give(g_river_iflytek_tts.queue_space);
    }

    return RIVER_OK;
}

static river_status_t river_tts_flush_pending_audio(void)
{
    if (g_river_iflytek_tts.mono_pending_bytes == 0U) {
        return RIVER_OK;
    }

    memset(g_river_iflytek_tts.mono_pending + g_river_iflytek_tts.mono_pending_bytes,
           0,
           RIVER_IFLYTEK_TTS_MONO_FRAME_BYTES - g_river_iflytek_tts.mono_pending_bytes);
    g_river_iflytek_tts.mono_pending_bytes = RIVER_IFLYTEK_TTS_MONO_FRAME_BYTES;
    if (river_tts_write_aligned_frame(g_river_iflytek_tts.mono_pending) != RIVER_OK) {
        return RIVER_ERR_IO;
    }
    g_river_iflytek_tts.mono_pending_bytes = 0U;
    return RIVER_OK;
}

static river_status_t river_tts_start_feeder(void)
{
    g_river_iflytek_tts.feeder_running = true;
    if (rtos_task_create(&g_river_iflytek_tts.feeder_task,
                         "river_tts_feed",
                         river_tts_feeder_task,
                         NULL,
                         RIVER_IFLYTEK_TTS_FEEDER_TASK_STACK,
                         RIVER_IFLYTEK_TTS_FEEDER_TASK_PRIORITY) != RTK_SUCCESS) {
        g_river_iflytek_tts.feeder_running = false;
        return RIVER_ERR_NO_MEMORY;
    }

    return RIVER_OK;
}

static void river_tts_request_feeder_stop(void)
{
    g_river_iflytek_tts.stop_requested = true;
    g_river_iflytek_tts.input_done = true;
    g_river_iflytek_tts.playback_epoch = 0U;
    river_tts_queue_reset();
    rtos_sema_give(g_river_iflytek_tts.queue_ready);
    rtos_sema_give(g_river_iflytek_tts.queue_space);
}

static bool river_tts_wait_feeder_stopped(uint32_t timeout_ms)
{
    uint32_t start_ms;

    start_ms = rtos_time_get_current_system_time_ms();
    while (g_river_iflytek_tts.feeder_running) {
        rtos_sema_give(g_river_iflytek_tts.queue_ready);
        if ((rtos_time_get_current_system_time_ms() - start_ms) > timeout_ms) {
            return false;
        }
        rtos_time_delay_ms(10U);
    }

    return true;
}

static void river_tts_feeder_task(void *param)
{
    bool playback_opened;
    river_status_t feed_status;

    (void)param;
    playback_opened = false;

    while (!g_river_iflytek_tts.stop_requested) {
        size_t queued_bytes;

        queued_bytes = river_tts_queue_count();
        if (!playback_opened &&
            (queued_bytes >= RIVER_IFLYTEK_TTS_START_BUFFER_BYTES ||
             ((g_river_iflytek_tts.input_done || g_river_iflytek_tts.completed || g_river_iflytek_tts.ws_closed) &&
              (queued_bytes > 0U || g_river_iflytek_tts.mono_pending_bytes > 0U)))) {
            if (river_tts_prepare_playback() != RIVER_OK) {
                river_tts_set_last_error("tts_playback_start_failed");
                g_river_iflytek_tts.failed = true;
                break;
            }
            playback_opened = true;
            g_river_iflytek_tts.playback_started = true;
            g_river_iflytek_tts.playback_epoch = river_playback_service_epoch();
        }

        if (playback_opened) {
            feed_status = river_tts_feed_ready_audio();
            if (feed_status != RIVER_OK) {
                if (feed_status == RIVER_ERR_BUSY &&
                    (river_tts_session_interrupted() ||
                     (g_river_iflytek_tts.playback_epoch != 0U &&
                      river_playback_service_epoch() != g_river_iflytek_tts.playback_epoch))) {
                    break;
                }
                river_tts_set_last_error("tts_playback_feed_failed");
                g_river_iflytek_tts.failed = true;
                break;
            }
        }

        queued_bytes = river_tts_queue_count();
        if ((g_river_iflytek_tts.input_done || g_river_iflytek_tts.completed || g_river_iflytek_tts.ws_closed) &&
            queued_bytes == 0U) {
            break;
        }

        if (queued_bytes == 0U) {
            if (playback_opened &&
                !g_river_iflytek_tts.input_done &&
                !g_river_iflytek_tts.completed &&
                !g_river_iflytek_tts.ws_closed &&
                !g_river_iflytek_tts.stop_requested) {
                g_river_iflytek_tts.queue_starve_count++;
            }
            rtos_sema_take(g_river_iflytek_tts.queue_ready, RIVER_IFLYTEK_TTS_QUEUE_WAIT_MS);
        }
    }

    if (!g_river_iflytek_tts.failed &&
        !river_tts_session_interrupted() &&
        playback_opened &&
        river_tts_flush_pending_audio() != RIVER_OK) {
        river_tts_set_last_error("tts_playback_flush_failed");
        g_river_iflytek_tts.failed = true;
    } else if (!playback_opened) {
        g_river_iflytek_tts.mono_pending_bytes = 0U;
    } else if (river_tts_session_interrupted()) {
        g_river_iflytek_tts.mono_pending_bytes = 0U;
    }

    g_river_iflytek_tts.input_done = true;
    g_river_iflytek_tts.feeder_running = false;
    g_river_iflytek_tts.playback_epoch = 0U;
    rtos_sema_give(g_river_iflytek_tts.queue_ready);
    rtos_sema_give(g_river_iflytek_tts.queue_space);
    rtos_task_delete(NULL);
}

static void river_tts_ws_message_cb(wsclient_context **wsclient,
                                    int data_len,
                                    enum opcode_type opcode,
                                    void *user_data)
{
    cJSON *root;
    cJSON *code_obj;
    cJSON *message_obj;
    cJSON *sid_obj;
    cJSON *data_obj;
    cJSON *audio_obj;
    cJSON *status_obj;
    int code;
    const char *message_text;
    const char *audio_b64;
    size_t decode_capacity;
    uint8_t *decoded_audio;
    size_t decoded_audio_len;

    (void)wsclient;
    (void)opcode;
    (void)user_data;

    if (river_tts_session_interrupted() ||
        g_river_iflytek_tts.wsclient == NULL ||
        g_river_iflytek_tts.wsclient->receivedData == NULL ||
        data_len <= 0) {
        return;
    }

    root = cJSON_ParseWithLength((const char *)g_river_iflytek_tts.wsclient->receivedData, (size_t)data_len);
    if (root == NULL) {
        g_river_iflytek_tts.decode_fail++;
        river_tts_set_last_error("response_json_parse_failed");
        g_river_iflytek_tts.failed = true;
        g_river_iflytek_tts.completed = true;
        g_river_iflytek_tts.input_done = true;
        rtos_sema_give(g_river_iflytek_tts.queue_ready);
        return;
    }

    code_obj = cJSON_GetObjectItemCaseSensitive(root, "code");
    message_obj = cJSON_GetObjectItemCaseSensitive(root, "message");
    sid_obj = cJSON_GetObjectItemCaseSensitive(root, "sid");
    code = cJSON_IsNumber(code_obj) ? code_obj->valueint : -1;
    message_text = cJSON_IsString(message_obj) ? message_obj->valuestring : NULL;
    g_river_iflytek_tts.last_code = code;

    if (cJSON_IsString(sid_obj) && sid_obj->valuestring != NULL) {
        snprintf(g_river_iflytek_tts.last_sid,
                 sizeof(g_river_iflytek_tts.last_sid),
                 "%s",
                 sid_obj->valuestring);
    }

    if (code != 0) {
        river_tts_set_last_error(message_text != NULL ? message_text : "tts_error");
        g_river_iflytek_tts.failed = true;
        g_river_iflytek_tts.completed = true;
        g_river_iflytek_tts.input_done = true;
        rtos_sema_give(g_river_iflytek_tts.queue_ready);
        cJSON_Delete(root);
        return;
    }

    data_obj = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsObject(data_obj)) {
        cJSON_Delete(root);
        return;
    }

    status_obj = cJSON_GetObjectItemCaseSensitive(data_obj, "status");
    audio_obj = cJSON_GetObjectItemCaseSensitive(data_obj, "audio");
    audio_b64 = cJSON_IsString(audio_obj) ? audio_obj->valuestring : NULL;

    if (audio_b64 != NULL && audio_b64[0] != '\0') {
        decode_capacity = ((strlen(audio_b64) + 3U) / 4U) * 3U + 4U;
        if (river_tts_ensure_decode_buffer(decode_capacity) != RIVER_OK) {
            river_tts_set_last_error("tts_decode_alloc_failed");
            g_river_iflytek_tts.failed = true;
            g_river_iflytek_tts.completed = true;
            g_river_iflytek_tts.input_done = true;
            rtos_sema_give(g_river_iflytek_tts.queue_ready);
            cJSON_Delete(root);
            return;
        }
        decoded_audio = g_river_iflytek_tts.decode_buffer;

        decoded_audio_len = 0U;
        if (mbedtls_base64_decode(decoded_audio,
                                  decode_capacity,
                                  &decoded_audio_len,
                                  (const unsigned char *)audio_b64,
                                  strlen(audio_b64)) != 0) {
            g_river_iflytek_tts.decode_fail++;
            river_tts_set_last_error("tts_audio_base64_decode_failed");
            g_river_iflytek_tts.failed = true;
            g_river_iflytek_tts.completed = true;
            g_river_iflytek_tts.input_done = true;
            rtos_sema_give(g_river_iflytek_tts.queue_ready);
            cJSON_Delete(root);
            return;
        }

        g_river_iflytek_tts.audio_chunks++;
        g_river_iflytek_tts.audio_bytes += (uint32_t)decoded_audio_len;
        g_river_iflytek_tts.saw_audio = true;
        if (river_tts_queue_write(decoded_audio, decoded_audio_len) != RIVER_OK) {
            g_river_iflytek_tts.failed = true;
            g_river_iflytek_tts.completed = true;
            g_river_iflytek_tts.input_done = true;
            rtos_sema_give(g_river_iflytek_tts.queue_ready);
            cJSON_Delete(root);
            return;
        }
    }

    if (cJSON_IsNumber(status_obj) && status_obj->valueint == 2) {
        g_river_iflytek_tts.completed = true;
        g_river_iflytek_tts.input_done = true;
        rtos_sema_give(g_river_iflytek_tts.queue_ready);
    }

    cJSON_Delete(root);
}

static void river_tts_ws_close_cb(wsclient_context *wsclient, void *user_data)
{
    (void)wsclient;
    (void)user_data;
    g_river_iflytek_tts.ws_closed = true;
    g_river_iflytek_tts.input_done = true;
    rtos_sema_give(g_river_iflytek_tts.queue_ready);
}

static void river_tts_close_context(void)
{
    if (g_river_iflytek_tts.wsclient != NULL) {
        river_ws_dispatch_unregister(g_river_iflytek_tts.wsclient);
        if (g_river_iflytek_tts.wsclient->readyState == WSC_OPEN) {
            ws_close(&g_river_iflytek_tts.wsclient);
        }
    }

    if (g_river_iflytek_tts.wsclient != NULL) {
        ws_free(g_river_iflytek_tts.wsclient);
        g_river_iflytek_tts.wsclient = NULL;
    }
}

static river_status_t river_tts_send_request(const char *text)
{
    unsigned char text_b64[RIVER_IFLYTEK_TTS_TEXT_B64_MAX];
    char request[RIVER_IFLYTEK_TTS_REQUEST_MAX];
    size_t text_b64_len;
    int request_len;

    text_b64_len = 0U;
    if (mbedtls_base64_encode(text_b64,
                              sizeof(text_b64),
                              &text_b64_len,
                              (const unsigned char *)text,
                              strlen(text)) != 0) {
        return RIVER_ERR_IO;
    }
    text_b64[text_b64_len] = '\0';

    request_len = snprintf(request,
                           sizeof(request),
                           "{\"common\":{\"app_id\":\"%s\"},"
                           "\"business\":{\"aue\":\"%s\",\"auf\":\"%s\",\"vcn\":\"%s\",\"speed\":%d,\"volume\":%d,\"pitch\":%d,\"tte\":\"%s\"},"
                           "\"data\":{\"status\":2,\"text\":\"%s\"}}",
                           RIVER_IFLYTEK_TTS_APP_ID,
                           RIVER_IFLYTEK_TTS_AUE,
                           RIVER_IFLYTEK_TTS_AUF,
                           RIVER_IFLYTEK_TTS_VCN,
                           RIVER_IFLYTEK_TTS_SPEED,
                           RIVER_IFLYTEK_TTS_VOLUME,
                           RIVER_IFLYTEK_TTS_PITCH,
                           RIVER_IFLYTEK_TTS_TTE,
                           text_b64);
    if (request_len <= 0 || (size_t)request_len >= sizeof(request)) {
        return RIVER_ERR_ARG;
    }

    if (ws_send(request, request_len, 1, g_river_iflytek_tts.wsclient) != 0) {
        g_river_iflytek_tts.send_fail++;
        return RIVER_ERR_IO;
    }

    return RIVER_OK;
}

river_status_t river_tts_iflytek_init(void)
{
    if (g_river_iflytek_tts.initialized) {
        return RIVER_OK;
    }
    memset(&g_river_iflytek_tts, 0, sizeof(g_river_iflytek_tts));
    if (rtos_mutex_create(&g_river_iflytek_tts.lock) != RTK_SUCCESS) {
        return RIVER_ERR_NO_MEMORY;
    }
    if (rtos_sema_create_binary(&g_river_iflytek_tts.queue_ready) != RTK_SUCCESS) {
        rtos_mutex_delete(g_river_iflytek_tts.lock);
        return RIVER_ERR_NO_MEMORY;
    }
    if (rtos_sema_create_binary(&g_river_iflytek_tts.queue_space) != RTK_SUCCESS) {
        rtos_sema_delete(g_river_iflytek_tts.queue_ready);
        rtos_mutex_delete(g_river_iflytek_tts.lock);
        return RIVER_ERR_NO_MEMORY;
    }
    if (river_ws_dispatch_init() != RIVER_OK) {
        rtos_sema_delete(g_river_iflytek_tts.queue_space);
        rtos_sema_delete(g_river_iflytek_tts.queue_ready);
        rtos_mutex_delete(g_river_iflytek_tts.lock);
        return RIVER_ERR_NO_MEMORY;
    }
    if (river_tts_prepare_resource_pool() != RIVER_OK) {
        rtos_sema_delete(g_river_iflytek_tts.queue_space);
        rtos_sema_delete(g_river_iflytek_tts.queue_ready);
        rtos_mutex_delete(g_river_iflytek_tts.lock);
        memset(&g_river_iflytek_tts, 0, sizeof(g_river_iflytek_tts));
        return RIVER_ERR_NO_MEMORY;
    }
    g_river_iflytek_tts.initialized = true;
    return RIVER_OK;
}

river_status_t river_tts_iflytek_submit_text(const char *text)
{
    char base_url[128];
    char path_query[RIVER_IFLYTEK_TTS_URL_MAX];
    char auth_url[RIVER_IFLYTEK_TTS_URL_MAX];
    const char *handshake_path;
    const char *query_string;
    uint32_t start_ms;
    river_status_t status;

    if (text == NULL || text[0] == '\0') {
        return RIVER_ERR_ARG;
    }
    if (strlen(text) >= RIVER_IFLYTEK_TTS_TEXT_MAX) {
        return RIVER_ERR_ARG;
    }
    if (!river_wifi_station_is_connected()) {
        return RIVER_ERR_BUSY;
    }
    if (!river_cloud_utc_ready()) {
        return RIVER_ERR_BUSY;
    }
    status = river_tts_iflytek_init();
    if (status != RIVER_OK) {
        return status;
    }

    if (rtos_mutex_take(g_river_iflytek_tts.lock, MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
        return RIVER_ERR_BUSY;
    }
    if (g_river_iflytek_tts.running) {
        rtos_mutex_give(g_river_iflytek_tts.lock);
        return RIVER_ERR_BUSY;
    }

    g_river_iflytek_tts.running = true;
    g_river_iflytek_tts.playback_started = false;
    g_river_iflytek_tts.feeder_running = false;
    g_river_iflytek_tts.completed = false;
    g_river_iflytek_tts.ws_closed = false;
    g_river_iflytek_tts.saw_audio = false;
    g_river_iflytek_tts.input_done = false;
    g_river_iflytek_tts.stop_requested = false;
    g_river_iflytek_tts.interrupted = false;
    g_river_iflytek_tts.failed = false;
    g_river_iflytek_tts.audio_chunks = 0U;
    g_river_iflytek_tts.audio_bytes = 0U;
    g_river_iflytek_tts.playback_write_ok = 0U;
    g_river_iflytek_tts.playback_write_fail = 0U;
    g_river_iflytek_tts.queue_peak_bytes = 0U;
    g_river_iflytek_tts.queue_overflow = 0U;
    g_river_iflytek_tts.queue_starve_count = 0U;
    g_river_iflytek_tts.last_code = 0;
    g_river_iflytek_tts.playback_epoch = 0U;
    g_river_iflytek_tts.mono_pending_bytes = 0U;
    g_river_iflytek_tts.last_sid[0] = '\0';
    g_river_iflytek_tts.last_error[0] = '\0';
    g_river_iflytek_tts.last_interrupt_reason[0] = '\0';
    river_tts_set_last_text_preview(text);
    g_river_iflytek_tts.sessions_started++;
    rtos_mutex_give(g_river_iflytek_tts.lock);

    river_tts_reset_binary_sema(g_river_iflytek_tts.queue_ready);
    river_tts_reset_binary_sema(g_river_iflytek_tts.queue_space);

    if (river_tts_ensure_pcm_queue() != RIVER_OK) {
        river_tts_set_last_error("tts_pcm_queue_alloc_failed");
        goto submit_fail;
    }
    river_tts_queue_reset();

    status = river_tts_build_auth_url(auth_url, sizeof(auth_url));
    if (status != RIVER_OK) {
        river_tts_set_last_error("tts_auth_url_build_failed");
        goto submit_fail;
    }

    /* Ameba websocket SDK always formats the request line as "GET /%s HTTP/1.1".
     * Keep the signed path as "/v2/tts", but pass "v2/tts?..." here so the actual
     * handshake request line remains "GET /v2/tts?..." instead of "//v2/tts?...".
     */
    handshake_path = RIVER_IFLYTEK_TTS_PATH;
    if (handshake_path[0] == '/') {
        handshake_path++;
    }
    query_string = strstr(auth_url, "?");

    snprintf(base_url, sizeof(base_url), "%s://%s", RIVER_IFLYTEK_TTS_SCHEME, RIVER_IFLYTEK_TTS_HOST);
    snprintf(path_query,
             sizeof(path_query),
             "%s?%s",
             handshake_path,
             query_string != NULL ? query_string + 1 : "");

    g_river_iflytek_tts.wsclient =
        create_wsclient(base_url,
                        RIVER_IFLYTEK_TTS_PORT,
                        path_query,
                        NULL,
                        RIVER_IFLYTEK_TTS_TX_MAX,
                        RIVER_IFLYTEK_TTS_RX_MAX,
                        RIVER_IFLYTEK_TTS_QUEUE_MAX);
    if (g_river_iflytek_tts.wsclient == NULL) {
        river_tts_set_last_error("tts_wsclient_create_failed");
        goto submit_fail;
    }

    if (river_ws_dispatch_register(g_river_iflytek_tts.wsclient,
                                   river_tts_ws_message_cb,
                                   river_tts_ws_close_cb,
                                   NULL) != RIVER_OK) {
        river_tts_set_last_error("tts_ws_dispatch_register_failed");
        goto submit_fail;
    }

    ws_multisend_opts(g_river_iflytek_tts.wsclient, 1);
    if (ws_connect_url(g_river_iflytek_tts.wsclient) < 0) {
        g_river_iflytek_tts.open_fail++;
        river_tts_set_last_error("tts_ws_connect_failed");
        goto submit_fail;
    }

    if (river_tts_start_feeder() != RIVER_OK) {
        river_tts_set_last_error("tts_feeder_start_failed");
        goto submit_fail;
    }

    if (river_tts_send_request(text) != RIVER_OK) {
        river_tts_set_last_error("tts_request_send_failed");
        goto submit_fail;
    }

    start_ms = rtos_time_get_current_system_time_ms();
    while (!g_river_iflytek_tts.completed && !g_river_iflytek_tts.ws_closed) {
        if (river_tts_session_interrupted()) {
            break;
        }
        ws_poll((int)RIVER_IFLYTEK_TTS_POLL_INTERVAL_MS, &g_river_iflytek_tts.wsclient);
        if (river_tts_session_interrupted()) {
            break;
        }
        if (g_river_iflytek_tts.failed) {
            goto submit_fail;
        }
        if ((rtos_time_get_current_system_time_ms() - start_ms) > RIVER_IFLYTEK_TTS_SESSION_TIMEOUT_MS) {
            g_river_iflytek_tts.timeout_fail++;
            river_tts_set_last_error("tts_session_timeout");
            goto submit_fail;
        }
    }

    river_tts_close_context();
    g_river_iflytek_tts.input_done = true;
    rtos_sema_give(g_river_iflytek_tts.queue_ready);

    if (!river_tts_wait_feeder_stopped(RIVER_IFLYTEK_TTS_FEEDER_STOP_TIMEOUT_MS)) {
        river_tts_set_last_error("tts_feeder_stop_timeout");
        goto submit_fail;
    }
    if (river_tts_session_interrupted()) {
        if (g_river_iflytek_tts.playback_started) {
            river_playback_service_interrupt_stream_ex(river_tts_last_interrupt_reason());
            g_river_iflytek_tts.playback_started = false;
        }
        g_river_iflytek_tts.running = false;
        RIVER_LOGI("tts interrupted sid=%s text=%s",
                   g_river_iflytek_tts.last_sid[0] != '\0' ? g_river_iflytek_tts.last_sid : "-",
                   g_river_iflytek_tts.last_text[0] != '\0' ? g_river_iflytek_tts.last_text : "-");
        return RIVER_ERR_BUSY;
    }
    if (g_river_iflytek_tts.failed) {
        goto submit_fail;
    }
    if (!g_river_iflytek_tts.completed) {
        river_tts_set_last_error("tts_session_closed_early");
        goto submit_fail;
    }
    if (!g_river_iflytek_tts.saw_audio) {
        river_tts_set_last_error("tts_no_audio");
        goto submit_fail;
    }

    if (g_river_iflytek_tts.playback_started) {
        rtos_time_delay_ms(river_tts_compute_drain_wait_ms());
        river_playback_service_stop_stream_ex("tts_complete");
        g_river_iflytek_tts.playback_started = false;
    }
    g_river_iflytek_tts.sessions_ok++;
    g_river_iflytek_tts.running = false;
    RIVER_LOGI("tts speak success sid=%s chunks=%lu audio_bytes=%lu text=%s",
               g_river_iflytek_tts.last_sid[0] != '\0' ? g_river_iflytek_tts.last_sid : "-",
               (unsigned long)g_river_iflytek_tts.audio_chunks,
               (unsigned long)g_river_iflytek_tts.audio_bytes,
               g_river_iflytek_tts.last_text[0] != '\0' ? g_river_iflytek_tts.last_text : "-");
    return RIVER_OK;

submit_fail:
    {
        bool interrupted_before_stop = river_tts_session_interrupted();

        river_tts_close_context();
        river_tts_request_feeder_stop();
        river_tts_wait_feeder_stopped(RIVER_IFLYTEK_TTS_FEEDER_STOP_TIMEOUT_MS);
        if (interrupted_before_stop) {
            if (g_river_iflytek_tts.playback_started) {
                river_playback_service_interrupt_stream_ex(river_tts_last_interrupt_reason());
                g_river_iflytek_tts.playback_started = false;
            }
            g_river_iflytek_tts.running = false;
            RIVER_LOGI("tts interrupted sid=%s text=%s",
                       g_river_iflytek_tts.last_sid[0] != '\0' ? g_river_iflytek_tts.last_sid : "-",
                       g_river_iflytek_tts.last_text[0] != '\0' ? g_river_iflytek_tts.last_text : "-");
            return RIVER_ERR_BUSY;
        }
        if (g_river_iflytek_tts.playback_started) {
            river_playback_service_stop_stream_ex("tts_submit_fail");
            g_river_iflytek_tts.playback_started = false;
        }
        g_river_iflytek_tts.sessions_fail++;
        g_river_iflytek_tts.running = false;
        RIVER_LOGW("tts speak failed sid=%s err=%s chunks=%lu audio_bytes=%lu completed=%s ws_closed=%s playback_started=%s",
                   g_river_iflytek_tts.last_sid[0] != '\0' ? g_river_iflytek_tts.last_sid : "-",
                   g_river_iflytek_tts.last_error[0] != '\0' ? g_river_iflytek_tts.last_error : "-",
                   (unsigned long)g_river_iflytek_tts.audio_chunks,
                   (unsigned long)g_river_iflytek_tts.audio_bytes,
                   g_river_iflytek_tts.completed ? "yes" : "no",
                   g_river_iflytek_tts.ws_closed ? "yes" : "no",
                   g_river_iflytek_tts.playback_started ? "yes" : "no");
        return RIVER_ERR_IO;
    }
}

void river_tts_iflytek_dump_status(void)
{
    RIVER_LOGI("tts provider=iflytek_ws running=%s sid=%s code=%d ok=%lu fail=%lu audio_chunks=%lu audio_bytes=%lu play_ok=%lu play_fail=%lu decode_fail=%lu open_fail=%lu send_fail=%lu timeout_fail=%lu q_cur=%lu q_peak=%lu q_overflow=%lu q_starve=%lu feeder=%s last_text=%s last_err=%s last_interrupt=%s",
               g_river_iflytek_tts.running ? "yes" : "no",
               g_river_iflytek_tts.last_sid[0] != '\0' ? g_river_iflytek_tts.last_sid : "-",
               g_river_iflytek_tts.last_code,
               (unsigned long)g_river_iflytek_tts.sessions_ok,
               (unsigned long)g_river_iflytek_tts.sessions_fail,
               (unsigned long)g_river_iflytek_tts.audio_chunks,
               (unsigned long)g_river_iflytek_tts.audio_bytes,
               (unsigned long)g_river_iflytek_tts.playback_write_ok,
               (unsigned long)g_river_iflytek_tts.playback_write_fail,
               (unsigned long)g_river_iflytek_tts.decode_fail,
               (unsigned long)g_river_iflytek_tts.open_fail,
               (unsigned long)g_river_iflytek_tts.send_fail,
               (unsigned long)g_river_iflytek_tts.timeout_fail,
               (unsigned long)river_tts_queue_count(),
               (unsigned long)g_river_iflytek_tts.queue_peak_bytes,
               (unsigned long)g_river_iflytek_tts.queue_overflow,
               (unsigned long)g_river_iflytek_tts.queue_starve_count,
               g_river_iflytek_tts.feeder_running ? "running" : "idle",
               g_river_iflytek_tts.last_text[0] != '\0' ? g_river_iflytek_tts.last_text : "-",
               g_river_iflytek_tts.last_error[0] != '\0' ? g_river_iflytek_tts.last_error : "-",
               g_river_iflytek_tts.last_interrupt_reason[0] != '\0' ? g_river_iflytek_tts.last_interrupt_reason : "-");
}

river_status_t river_tts_iflytek_request_stop_with_reason(const char *reason)
{
    if (!g_river_iflytek_tts.initialized) {
        return RIVER_ERR_NOT_FOUND;
    }

    if (rtos_mutex_take(g_river_iflytek_tts.lock, MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
        return RIVER_ERR_BUSY;
    }

    if (!g_river_iflytek_tts.running) {
        rtos_mutex_give(g_river_iflytek_tts.lock);
        return RIVER_ERR_NOT_FOUND;
    }

    g_river_iflytek_tts.stop_requested = true;
    g_river_iflytek_tts.interrupted = true;
    g_river_iflytek_tts.input_done = true;
    g_river_iflytek_tts.ws_closed = true;
    g_river_iflytek_tts.playback_epoch = 0U;
    river_tts_set_last_interrupt_reason(reason);
    river_tts_set_last_error("tts_interrupted");
    rtos_mutex_give(g_river_iflytek_tts.lock);

    river_tts_queue_reset();
    if (g_river_iflytek_tts.playback_started) {
        river_playback_service_interrupt_stream_ex(river_tts_last_interrupt_reason());
        g_river_iflytek_tts.playback_started = false;
    }
    rtos_sema_give(g_river_iflytek_tts.queue_ready);
    rtos_sema_give(g_river_iflytek_tts.queue_space);
    return RIVER_OK;
}

river_status_t river_tts_iflytek_request_stop(void)
{
    return river_tts_iflytek_request_stop_with_reason(NULL);
}
