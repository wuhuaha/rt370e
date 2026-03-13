#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cJSON.h"
#include "lwip_netconf.h"
#include "lwip/netdb.h"
#include "wifi_api.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "mbedtls/md5.h"
#include "os_wrapper.h"
#include "websocket/libwsclient.h"
#include "websocket/wsclient_api.h"

#include "river/river_asr_iflytek_credentials.h"
#include "river/river_log.h"
#include "river_asr_provider_internal.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.cloud.iflytek"

#define RIVER_IFLYTEK_RTASR_URL_MAX            1024U
#define RIVER_IFLYTEK_RTASR_TEXT_MAX           256U
#define RIVER_IFLYTEK_RTASR_DESC_MAX           128U
#define RIVER_IFLYTEK_RTASR_SIGNATURE_RAW_MAX  20U
#define RIVER_IFLYTEK_RTASR_SIGNATURE_B64_MAX  64U
#define RIVER_IFLYTEK_RTASR_QUERY_MAX          768U
#define RIVER_IFLYTEK_RTASR_TX_MAX             512U
#define RIVER_IFLYTEK_RTASR_RX_MAX             1024U
#define RIVER_IFLYTEK_RTASR_QUEUE_MAX          3U

typedef struct {
    bool initialized;
    bool stream_open;
    wsclient_context *wsclient;
    river_cloud_asr_provider_result_cb_t callback;
    void *callback_user_data;
    river_cloud_asr_audio_desc_t active_audio;
    uint32_t sequence;
    uint32_t sessions_opened;
    uint32_t sessions_closed;
    uint32_t partial_results;
    uint32_t final_results;
    uint32_t error_results;
    uint32_t sent_audio_bytes;
    uint32_t sent_frames;
    uint32_t receive_messages;
    int last_code;
    char last_sid[80];
    char last_text[RIVER_IFLYTEK_RTASR_TEXT_MAX];
    char last_error[RIVER_IFLYTEK_RTASR_DESC_MAX];
} river_iflytek_rtasr_context_t;

static river_iflytek_rtasr_context_t g_river_iflytek_rtasr;

static size_t river_iflytek_url_encode(const char *src, char *dst, size_t dst_size)
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

static river_status_t river_iflytek_sign_llm(const char *appid,
                                             const char *ts,
                                             char *signature_b64,
                                             size_t signature_b64_size)
{
    unsigned char md5_output[16];
    unsigned char hmac_output[RIVER_IFLYTEK_RTASR_SIGNATURE_RAW_MAX];
    char base_string[128];
    const mbedtls_md_info_t *md_info;
    size_t signature_len;

    snprintf(base_string, sizeof(base_string), "%s%s", appid, ts);
    mbedtls_md5((const unsigned char *)base_string, strlen(base_string), md5_output);

    char md5_hex[33];
    for (int i = 0; i < 16; i++) {
        sprintf(md5_hex + (i * 2), "%02x", md5_output[i]);
    }

    md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    if (md_info == NULL) {
        return RIVER_ERR_UNSUPPORTED;
    }

    if (mbedtls_md_hmac(md_info,
                        (const unsigned char *)RIVER_IFLYTEK_RTASR_API_KEY,
                        strlen(RIVER_IFLYTEK_RTASR_API_KEY),
                        (const unsigned char *)md5_hex,
                        strlen(md5_hex),
                        hmac_output) != 0) {
        return RIVER_ERR_IO;
    }

    if (mbedtls_base64_encode((unsigned char *)signature_b64,
                              signature_b64_size,
                              &signature_len,
                              hmac_output,
                              sizeof(hmac_output)) != 0) {
        return RIVER_ERR_IO;
    }

    signature_b64[signature_len] = '\0';
    return RIVER_OK;
}

static river_status_t river_iflytek_build_query(char *query, size_t query_size)
{
    char ts_text[24];
    char signature_b64[RIVER_IFLYTEK_RTASR_SIGNATURE_B64_MAX];
    char signature_encoded[128];
    time_t now;

    now = time(NULL);
    if (now < 1700000000) {
        return RIVER_ERR_BUSY;
    }
    snprintf(ts_text, sizeof(ts_text), "%lu", (unsigned long)now);

    if (river_iflytek_sign_llm(RIVER_IFLYTEK_RTASR_APP_ID,
                                ts_text,
                                signature_b64,
                                sizeof(signature_b64)) != RIVER_OK) {
        return RIVER_ERR_IO;
    }

    river_iflytek_url_encode(signature_b64, signature_encoded, sizeof(signature_encoded));
    
    snprintf(query, query_size, "appid=%s&ts=%s&signa=%s",
             RIVER_IFLYTEK_RTASR_APP_ID, ts_text, signature_encoded);

    return RIVER_OK;
}

static void river_iflytek_emit_result(river_cloud_asr_event_type_t type,
                                      const char *text,
                                      const char *sid,
                                      const char *message,
                                      int code,
                                      bool is_final)
{
    river_cloud_asr_result_t result;

    memset(&result, 0, sizeof(result));
    result.type = type;
    result.provider_name = "iflytek_rtasr";
    result.text = text;
    result.sid = sid;
    result.message = message;
    result.sequence = g_river_iflytek_rtasr.sequence;
    result.code = code;
    result.is_final = is_final;

    if (g_river_iflytek_rtasr.callback != NULL) {
        g_river_iflytek_rtasr.callback(&result, g_river_iflytek_rtasr.callback_user_data);
    }
}

static void river_iflytek_extract_text(cJSON *st_obj, char *buffer, size_t buffer_size)
{
    cJSON *rt_array;
    cJSON *rt_item;

    buffer[0] = '\0';
    if (st_obj == NULL) {
        return;
    }

    rt_array = cJSON_GetObjectItemCaseSensitive(st_obj, "rt");
    if (!cJSON_IsArray(rt_array)) {
        return;
    }

    cJSON_ArrayForEach(rt_item, rt_array) {
        cJSON *ws_array;
        cJSON *ws_item;

        ws_array = cJSON_GetObjectItemCaseSensitive(rt_item, "ws");
        if (!cJSON_IsArray(ws_array)) {
            continue;
        }

        cJSON_ArrayForEach(ws_item, ws_array) {
            cJSON *cw_array;
            cJSON *cw_item;

            cw_array = cJSON_GetObjectItemCaseSensitive(ws_item, "cw");
            if (!cJSON_IsArray(cw_array)) {
                continue;
            }

            cJSON_ArrayForEach(cw_item, cw_array) {
                cJSON *word_obj;
                const char *word;

                word_obj = cJSON_GetObjectItemCaseSensitive(cw_item, "w");
                if (!cJSON_IsString(word_obj) || word_obj->valuestring == NULL) {
                    continue;
                }

                word = word_obj->valuestring;
                strncat(buffer,
                        word,
                        buffer_size - strlen(buffer) - 1U);
            }
        }
    }
}

static void river_iflytek_handle_text_message(const char *json, int json_len)
{
    cJSON *root;
    cJSON *action_obj;
    cJSON *code_obj;
    cJSON *desc_obj;
    cJSON *sid_obj;
    cJSON *data_obj;
    cJSON *cn_obj;
    cJSON *st_obj;
    cJSON *ls_obj;
    cJSON *type_obj;
    int code;
    bool is_final;
    char text[RIVER_IFLYTEK_RTASR_TEXT_MAX];
    char message[RIVER_IFLYTEK_RTASR_DESC_MAX];

    root = cJSON_ParseWithLength(json, (size_t)json_len);
    if (root == NULL) {
        snprintf(g_river_iflytek_rtasr.last_error,
                 sizeof(g_river_iflytek_rtasr.last_error),
                 "%s",
                 "json parse failed");
        river_iflytek_emit_result(RIVER_CLOUD_ASR_EVENT_ERROR,
                                  NULL,
                                  g_river_iflytek_rtasr.last_sid,
                                  g_river_iflytek_rtasr.last_error,
                                  -1,
                                  false);
        return;
    }

    action_obj = cJSON_GetObjectItemCaseSensitive(root, "action");
    code_obj = cJSON_GetObjectItemCaseSensitive(root, "code");
    desc_obj = cJSON_GetObjectItemCaseSensitive(root, "desc");
    sid_obj = cJSON_GetObjectItemCaseSensitive(root, "sid");
    code = cJSON_IsNumber(code_obj) ? code_obj->valueint : -1;
    message[0] = '\0';
    if (cJSON_IsString(desc_obj) && desc_obj->valuestring != NULL) {
        snprintf(message, sizeof(message), "%s", desc_obj->valuestring);
        snprintf(g_river_iflytek_rtasr.last_error,
                 sizeof(g_river_iflytek_rtasr.last_error),
                 "%s",
                 desc_obj->valuestring);
    }
    if (cJSON_IsString(sid_obj) && sid_obj->valuestring != NULL) {
        snprintf(g_river_iflytek_rtasr.last_sid,
                 sizeof(g_river_iflytek_rtasr.last_sid),
                 "%s",
                 sid_obj->valuestring);
    }

    if (code != 0) {
        g_river_iflytek_rtasr.error_results++;
        river_iflytek_emit_result(RIVER_CLOUD_ASR_EVENT_ERROR,
                                  NULL,
                                  g_river_iflytek_rtasr.last_sid,
                                  message,
                                  code,
                                  false);
        cJSON_Delete(root);
        return;
    }

    if (cJSON_IsString(action_obj) && action_obj->valuestring != NULL &&
        strcmp(action_obj->valuestring, "started") == 0) {
        river_iflytek_emit_result(RIVER_CLOUD_ASR_EVENT_SESSION_STARTED,
                                  NULL,
                                  g_river_iflytek_rtasr.last_sid,
                                  message,
                                  code,
                                  false);
        cJSON_Delete(root);
        return;
    }

    data_obj = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsObject(data_obj)) {
        cJSON_Delete(root);
        return;
    }

    cn_obj = cJSON_GetObjectItemCaseSensitive(data_obj, "cn");
    st_obj = cJSON_IsObject(cn_obj) ?
             cJSON_GetObjectItemCaseSensitive(cn_obj, "st") : NULL;
    ls_obj = cJSON_GetObjectItemCaseSensitive(data_obj, "ls");
    type_obj = cJSON_IsObject(st_obj) ?
               cJSON_GetObjectItemCaseSensitive(st_obj, "type") : NULL;
    is_final = cJSON_IsTrue(ls_obj) ||
               (cJSON_IsNumber(type_obj) && type_obj->valueint == 0);

    text[0] = '\0';
    river_iflytek_extract_text(st_obj, text, sizeof(text));
    if (text[0] != '\0') {
        snprintf(g_river_iflytek_rtasr.last_text,
                 sizeof(g_river_iflytek_rtasr.last_text),
                 "%s",
                 text);
        if (is_final) {
            g_river_iflytek_rtasr.final_results++;
            river_iflytek_emit_result(RIVER_CLOUD_ASR_EVENT_FINAL,
                                      g_river_iflytek_rtasr.last_text,
                                      g_river_iflytek_rtasr.last_sid,
                                      message,
                                      code,
                                      true);
        } else {
            g_river_iflytek_rtasr.partial_results++;
            river_iflytek_emit_result(RIVER_CLOUD_ASR_EVENT_PARTIAL,
                                      g_river_iflytek_rtasr.last_text,
                                      g_river_iflytek_rtasr.last_sid,
                                      message,
                                      code,
                                      false);
        }
    }

    cJSON_Delete(root);
}

static void river_iflytek_ws_message_cb(wsclient_context **wsclient,
                                        int data_len,
                                        enum opcode_type opcode)
{
    char *payload;

    (void)wsclient;
    if (opcode != TEXT_FRAME || g_river_iflytek_rtasr.wsclient == NULL ||
        g_river_iflytek_rtasr.wsclient->receivedData == NULL || data_len <= 0) {
        return;
    }

    payload = (char *)rtos_mem_zmalloc((uint32_t)data_len + 1U);
    if (payload == NULL) {
        return;
    }
    memcpy(payload, g_river_iflytek_rtasr.wsclient->receivedData, (size_t)data_len);
    payload[data_len] = '\0';
    g_river_iflytek_rtasr.receive_messages++;
    river_iflytek_handle_text_message(payload, data_len);
    rtos_mem_free(payload);
}

static void river_iflytek_close_context(bool emit_close_event)
{
    uint32_t wait_loops;

    if (g_river_iflytek_rtasr.wsclient != NULL &&
        g_river_iflytek_rtasr.wsclient->readyState == WSC_OPEN) {
        ws_close(&g_river_iflytek_rtasr.wsclient);
        for (wait_loops = 0U; wait_loops < 10U; ++wait_loops) {
            ws_poll(100, &g_river_iflytek_rtasr.wsclient);
            if (g_river_iflytek_rtasr.wsclient == NULL ||
                g_river_iflytek_rtasr.wsclient->readyState == WSC_CLOSED) {
                break;
            }
        }
    }

    if (g_river_iflytek_rtasr.wsclient != NULL) {
        ws_free(g_river_iflytek_rtasr.wsclient);
        g_river_iflytek_rtasr.wsclient = NULL;
    }

    if (g_river_iflytek_rtasr.stream_open) {
        g_river_iflytek_rtasr.stream_open = false;
        g_river_iflytek_rtasr.sessions_closed++;
        if (emit_close_event) {
            river_iflytek_emit_result(RIVER_CLOUD_ASR_EVENT_SESSION_CLOSED,
                                      NULL,
                                      g_river_iflytek_rtasr.last_sid,
                                      NULL,
                                      0,
                                      true);
        }
    }
}

static const char *river_iflytek_provider_name(void)
{
    return "iflytek_rtasr";
}

static bool river_iflytek_supports_streaming(void)
{
    return true;
}

static bool river_iflytek_supports_batch(void)
{
    return false;
}

static river_status_t river_iflytek_init(river_cloud_asr_provider_result_cb_t callback,
                                         void *user_data)
{
    memset(&g_river_iflytek_rtasr, 0, sizeof(g_river_iflytek_rtasr));
    g_river_iflytek_rtasr.callback = callback;
    g_river_iflytek_rtasr.callback_user_data = user_data;
    g_river_iflytek_rtasr.initialized = true;
    ws_dispatch(river_iflytek_ws_message_cb);
    return RIVER_OK;
}

static void river_iflytek_deinit(void)
{
    river_iflytek_close_context(false);
    memset(&g_river_iflytek_rtasr, 0, sizeof(g_river_iflytek_rtasr));
}

static river_status_t river_iflytek_stream_open(const river_cloud_asr_audio_desc_t *audio)
{
    char query[RIVER_IFLYTEK_RTASR_QUERY_MAX];
    char full_url[RIVER_IFLYTEK_RTASR_URL_MAX];
    struct hostent *server_host;

    if (audio == NULL) {
        return RIVER_ERR_ARG;
    }
    if (LwIP_Check_Connectivity(NETIF_WLAN_STA_INDEX) != CONNECTION_VALID) {
        return RIVER_ERR_BUSY;
    }

    server_host = gethostbyname(RIVER_IFLYTEK_RTASR_HOST);
    if (server_host == NULL) {
        RIVER_LOGW("DNS resolution failed for %s", RIVER_IFLYTEK_RTASR_HOST);
        return RIVER_ERR_IO;
    }
    RIVER_LOGI("DNS resolved %s to %u.%u.%u.%u",
               RIVER_IFLYTEK_RTASR_HOST,
               (unsigned char)server_host->h_addr_list[0][0],
               (unsigned char)server_host->h_addr_list[0][1],
               (unsigned char)server_host->h_addr_list[0][2],
               (unsigned char)server_host->h_addr_list[0][3]);

    river_iflytek_close_context(false);

    /* Settle time for network stack */
    rtos_time_delay_ms(500);

    if (river_iflytek_build_query(query, sizeof(query)) != RIVER_OK) {
        return RIVER_ERR_IO;
    }

    /* 
     * Rebuild a simple ws:// URL.
     * We'll use ws_connect_url but ensure it's formatted as ws://host:80/path?query
     */
    snprintf(full_url, sizeof(full_url), "ws://%s:80%s?%s", 
             RIVER_IFLYTEK_RTASR_HOST, RIVER_IFLYTEK_RTASR_PATH, query);

    g_river_iflytek_rtasr.wsclient =
        create_wsclient(full_url, 0, NULL, NULL,
                        RIVER_IFLYTEK_RTASR_TX_MAX,
                        RIVER_IFLYTEK_RTASR_RX_MAX,
                        RIVER_IFLYTEK_RTASR_QUEUE_MAX);
    if (g_river_iflytek_rtasr.wsclient == NULL) {
        return RIVER_ERR_NO_MEMORY;
    }

    ws_setsockopt_timeout(10000U, 10000U, 15000U);

    RIVER_LOGI("initiating ws connection via %s", full_url);
    if (ws_connect_url(g_river_iflytek_rtasr.wsclient) < 0) {
        river_iflytek_close_context(false);
        snprintf(g_river_iflytek_rtasr.last_error,
                 sizeof(g_river_iflytek_rtasr.last_error),
                 "ws connect failed");
        return RIVER_ERR_IO;
    }

    g_river_iflytek_rtasr.active_audio = *audio;
    g_river_iflytek_rtasr.sequence++;
    g_river_iflytek_rtasr.stream_open = true;
    g_river_iflytek_rtasr.sessions_opened++;
    g_river_iflytek_rtasr.sent_audio_bytes = 0U;
    g_river_iflytek_rtasr.sent_frames = 0U;
    g_river_iflytek_rtasr.last_sid[0] = '\0';
    g_river_iflytek_rtasr.last_text[0] = '\0';
    g_river_iflytek_rtasr.last_error[0] = '\0';
    RIVER_LOGI("stream open success: seq=%lu", (unsigned long)g_river_iflytek_rtasr.sequence);
    return RIVER_OK;
}

static river_status_t river_iflytek_stream_feed(const uint8_t *pcm, size_t bytes)
{
    if (!g_river_iflytek_rtasr.stream_open || g_river_iflytek_rtasr.wsclient == NULL ||
        pcm == NULL || bytes == 0U) {
        return RIVER_ERR_ARG;
    }
    if (ws_sendBinary((uint8_t *)pcm, (int)bytes, 1, g_river_iflytek_rtasr.wsclient) != 0) {
        return RIVER_ERR_IO;
    }

    g_river_iflytek_rtasr.sent_audio_bytes += (uint32_t)bytes;
    g_river_iflytek_rtasr.sent_frames++;
    return RIVER_OK;
}

static river_status_t river_iflytek_stream_finish(void)
{
    static const char k_end_frame[] = "{\"end\": true}";

    if (!g_river_iflytek_rtasr.stream_open || g_river_iflytek_rtasr.wsclient == NULL) {
        return RIVER_OK;
    }

    if (ws_send((char *)k_end_frame,
                strlen(k_end_frame),
                1,
                g_river_iflytek_rtasr.wsclient) != 0) {
        river_iflytek_close_context(false);
        return RIVER_ERR_IO;
    }

    ws_poll(400, &g_river_iflytek_rtasr.wsclient);
    ws_poll(400, &g_river_iflytek_rtasr.wsclient);
    river_iflytek_close_context(true);
    return RIVER_OK;
}

static river_status_t river_iflytek_stream_poll(uint32_t timeout_ms)
{
    if (!g_river_iflytek_rtasr.stream_open || g_river_iflytek_rtasr.wsclient == NULL) {
        return RIVER_OK;
    }
    ws_poll((int)timeout_ms, &g_river_iflytek_rtasr.wsclient);
    if (g_river_iflytek_rtasr.wsclient == NULL ||
        g_river_iflytek_rtasr.wsclient->readyState == WSC_CLOSED) {
        river_iflytek_close_context(true);
    }
    return RIVER_OK;
}

static bool river_iflytek_stream_active(void)
{
    return g_river_iflytek_rtasr.stream_open &&
           g_river_iflytek_rtasr.wsclient != NULL &&
           g_river_iflytek_rtasr.wsclient->readyState == WSC_OPEN;
}

static river_status_t river_iflytek_batch_submit(const uint8_t *pcm,
                                                 size_t bytes,
                                                 const river_voice_segment_desc_t *segment)
{
    (void)pcm;
    (void)bytes;
    (void)segment;
    return RIVER_ERR_UNSUPPORTED;
}

static void river_iflytek_dump_status(void)
{
    RIVER_LOGI("stream=%s seq=%lu opened=%lu closed=%lu partial=%lu final=%lu error=%lu recv=%lu audio_bytes=%lu frames=%lu sid=%s last_text=%s last_err=%s",
               river_iflytek_stream_active() ? "open" : "closed",
               (unsigned long)g_river_iflytek_rtasr.sequence,
               (unsigned long)g_river_iflytek_rtasr.sessions_opened,
               (unsigned long)g_river_iflytek_rtasr.sessions_closed,
               (unsigned long)g_river_iflytek_rtasr.partial_results,
               (unsigned long)g_river_iflytek_rtasr.final_results,
               (unsigned long)g_river_iflytek_rtasr.error_results,
               (unsigned long)g_river_iflytek_rtasr.receive_messages,
               (unsigned long)g_river_iflytek_rtasr.sent_audio_bytes,
               (unsigned long)g_river_iflytek_rtasr.sent_frames,
               g_river_iflytek_rtasr.last_sid[0] != '\0' ? g_river_iflytek_rtasr.last_sid : "-",
               g_river_iflytek_rtasr.last_text[0] != '\0' ? g_river_iflytek_rtasr.last_text : "-",
               g_river_iflytek_rtasr.last_error[0] != '\0' ? g_river_iflytek_rtasr.last_error : "-");
}

const river_cloud_asr_provider_ops_t g_river_cloud_iflytek_rtasr_ops = {
    .provider_name = river_iflytek_provider_name,
    .supports_streaming = river_iflytek_supports_streaming,
    .supports_batch = river_iflytek_supports_batch,
    .init = river_iflytek_init,
    .deinit = river_iflytek_deinit,
    .stream_open = river_iflytek_stream_open,
    .stream_feed = river_iflytek_stream_feed,
    .stream_finish = river_iflytek_stream_finish,
    .stream_poll = river_iflytek_stream_poll,
    .stream_active = river_iflytek_stream_active,
    .batch_submit = river_iflytek_batch_submit,
    .dump_status = river_iflytek_dump_status,
};
