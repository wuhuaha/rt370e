#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
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
#include "river_ws_dispatch.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.cloud.iflytek"

#define RIVER_IFLYTEK_RTASR_URL_MAX            1024U
#define RIVER_IFLYTEK_RTASR_TEXT_MAX           2048U
#define RIVER_IFLYTEK_RTASR_DESC_MAX           128U
#define RIVER_IFLYTEK_RTASR_SIGNATURE_RAW_MAX  20U
#define RIVER_IFLYTEK_RTASR_SIGNATURE_B64_MAX  64U
#define RIVER_IFLYTEK_RTASR_UTC_MAX            40U
#define RIVER_IFLYTEK_RTASR_UUID_MAX           40U
#define RIVER_IFLYTEK_RTASR_QUERY_MAX          768U
#define RIVER_IFLYTEK_RTASR_PATH_QUERY_MAX     896U
#define RIVER_IFLYTEK_RTASR_TX_MAX             2048U
/*
 * RTASR may return multi-kilobyte result frames for longer utterances.
 * Keep the websocket RX budget above the observed server payload size so
 * frames are parsed instead of being discarded by wsclient_api.c.
 */
#define RIVER_IFLYTEK_RTASR_RX_MAX             12288U
#define RIVER_IFLYTEK_RTASR_QUEUE_MAX          32U
#define RIVER_IFLYTEK_RTASR_SEND_BLOCK_MS      200U
#define RIVER_IFLYTEK_RTASR_CHUNK_BYTES        1280U
#define RIVER_IFLYTEK_RTASR_LOCAL_QUEUE_SLOTS  32U
#define RIVER_IFLYTEK_RTASR_SEND_INTERVAL_MS   40U
#define RIVER_IFLYTEK_RTASR_OPEN_READY_WAIT_MS 1200U

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
    uint32_t local_chunks_queued;
    uint32_t local_chunks_dropped;
    uint8_t staged_audio[RIVER_IFLYTEK_RTASR_CHUNK_BYTES];
    size_t staged_audio_bytes;
    uint8_t local_queue[RIVER_IFLYTEK_RTASR_LOCAL_QUEUE_SLOTS][RIVER_IFLYTEK_RTASR_CHUNK_BYTES];
    uint16_t local_queue_len[RIVER_IFLYTEK_RTASR_LOCAL_QUEUE_SLOTS];
    uint8_t local_queue_head;
    uint8_t local_queue_tail;
    uint8_t local_queue_count;
    uint32_t last_send_ms;
    bool session_started;
    int last_code;
    char last_sid[80];
    char last_text[RIVER_IFLYTEK_RTASR_TEXT_MAX];
    char last_error[RIVER_IFLYTEK_RTASR_DESC_MAX];
    char last_session_id[80];
} river_iflytek_rtasr_context_t;

static river_iflytek_rtasr_context_t g_river_iflytek_rtasr;

typedef struct {
    const char *key;
    const char *value;
} river_iflytek_query_param_t;

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

static int river_iflytek_query_param_compare(const void *lhs, const void *rhs)
{
    const river_iflytek_query_param_t *left;
    const river_iflytek_query_param_t *right;

    left = (const river_iflytek_query_param_t *)lhs;
    right = (const river_iflytek_query_param_t *)rhs;
    return strcmp(left->key, right->key);
}

static bool river_iflytek_build_utc(char *utc_text, size_t utc_text_size)
{
    uint32_t utc_seconds;
    int64_t local_seconds;
    int64_t days;
    int64_t seconds_of_day;
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int64_t march_based;

    if (utc_text == NULL || utc_text_size == 0U) {
        return false;
    }

    utc_seconds = river_cloud_now_utc_seconds();
    if (utc_seconds == 0U) {
        return false;
    }

    local_seconds = (int64_t)utc_seconds + (8LL * 60LL * 60LL);
    days = local_seconds / 86400LL;
    seconds_of_day = local_seconds % 86400LL;
    if (seconds_of_day < 0LL) {
        seconds_of_day += 86400LL;
        days -= 1LL;
    }

    days += 719468LL;
    march_based = (days >= 0) ? days : (days - 146096LL);
    {
        int64_t era = march_based / 146097LL;
        unsigned int day_of_era = (unsigned int)(days - (era * 146097LL));
        unsigned int year_of_era =
            (day_of_era - day_of_era / 1460U + day_of_era / 36524U - day_of_era / 146096U) / 365U;
        unsigned int day_of_year =
            day_of_era - (365U * year_of_era + year_of_era / 4U - year_of_era / 100U);
        unsigned int month_prime = (5U * day_of_year + 2U) / 153U;

        day = (int)(day_of_year - (153U * month_prime + 2U) / 5U + 1U);
        month = (int)month_prime + 3;
        if (month > 12) {
            month -= 12;
        }
        year = (int)(year_of_era + era * 400LL + (month <= 2 ? 1 : 0));
    }

    hour = (int)(seconds_of_day / 3600LL);
    minute = (int)((seconds_of_day % 3600LL) / 60LL);
    second = (int)(seconds_of_day % 60LL);

    snprintf(utc_text,
             utc_text_size,
             "%04d-%02d-%02dT%02d:%02d:%02d+0800",
             year,
             month,
             day,
             hour,
             minute,
             second);
    return true;
}

static void river_iflytek_build_uuid(char *uuid_text, size_t uuid_text_size)
{
    uint32_t utc_now;
    uint32_t tick_now;

    if (uuid_text == NULL || uuid_text_size == 0U) {
        return;
    }

    utc_now = river_cloud_now_utc_seconds();
    tick_now = rtos_time_get_current_system_time_ms();
    snprintf(uuid_text,
             uuid_text_size,
             "%08" PRIx32 "%08" PRIx32 "%08" PRIx32 "%08" PRIx32,
             utc_now,
             tick_now,
             g_river_iflytek_rtasr.sequence + 1U,
             (utc_now ^ tick_now ^ 0x5a5aa5a5U));
}

static river_status_t river_iflytek_sign_request(const river_iflytek_query_param_t *params,
                                                 size_t param_count,
                                             char *signature_b64,
                                             size_t signature_b64_size)
{
    unsigned char hmac_output[RIVER_IFLYTEK_RTASR_SIGNATURE_RAW_MAX];
    char base_string[RIVER_IFLYTEK_RTASR_QUERY_MAX];
    const mbedtls_md_info_t *md_info;
    size_t signature_len;
    size_t index;
    size_t offset;

    if (params == NULL || param_count == 0U || signature_b64 == NULL || signature_b64_size == 0U) {
        return RIVER_ERR_ARG;
    }

    offset = 0U;
    for (index = 0U; index < param_count; ++index) {
        char encoded_key[96];
        char encoded_value[256];
        int written;

        river_iflytek_url_encode(params[index].key, encoded_key, sizeof(encoded_key));
        river_iflytek_url_encode(params[index].value, encoded_value, sizeof(encoded_value));

        written = snprintf(base_string + offset,
                           sizeof(base_string) - offset,
                           "%s%s=%s",
                           (index == 0U) ? "" : "&",
                           encoded_key,
                           encoded_value);
        if (written <= 0 || (size_t)written >= (sizeof(base_string) - offset)) {
            return RIVER_ERR_IO;
        }
        offset += (size_t)written;
    }

    md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    if (md_info == NULL) {
        return RIVER_ERR_UNSUPPORTED;
    }

    if (mbedtls_md_hmac(md_info,
                        (const unsigned char *)RIVER_IFLYTEK_RTASR_ACCESS_KEY_SECRET,
                        strlen(RIVER_IFLYTEK_RTASR_ACCESS_KEY_SECRET),
                        (const unsigned char *)base_string,
                        strlen(base_string),
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
    river_iflytek_query_param_t params[7];
    char utc_text[RIVER_IFLYTEK_RTASR_UTC_MAX];
    char uuid_text[RIVER_IFLYTEK_RTASR_UUID_MAX];
    char signature_b64[RIVER_IFLYTEK_RTASR_SIGNATURE_B64_MAX];
    char signature_encoded[128];
    size_t index;
    int written;

    if (query == NULL || query_size == 0U) {
        return RIVER_ERR_ARG;
    }
    if (!river_iflytek_build_utc(utc_text, sizeof(utc_text))) {
        return RIVER_ERR_BUSY;
    }

    river_iflytek_build_uuid(uuid_text, sizeof(uuid_text));

    params[0].key = "accessKeyId";
    params[0].value = RIVER_IFLYTEK_RTASR_ACCESS_KEY_ID;
    params[1].key = "appId";
    params[1].value = RIVER_IFLYTEK_RTASR_APP_ID;
    params[2].key = "audio_encode";
    params[2].value = RIVER_IFLYTEK_RTASR_AUDIO_ENC;
    params[3].key = "lang";
    params[3].value = RIVER_IFLYTEK_RTASR_LANG;
    params[4].key = "samplerate";
    params[4].value = RIVER_IFLYTEK_RTASR_SAMPLERATE;
    params[5].key = "utc";
    params[5].value = utc_text;
    params[6].key = "uuid";
    params[6].value = uuid_text;

    qsort(params, sizeof(params) / sizeof(params[0]), sizeof(params[0]), river_iflytek_query_param_compare);

    if (river_iflytek_sign_request(params,
                                   sizeof(params) / sizeof(params[0]),
                                   signature_b64,
                                   sizeof(signature_b64)) != RIVER_OK) {
        return RIVER_ERR_IO;
    }

    river_iflytek_url_encode(signature_b64, signature_encoded, sizeof(signature_encoded));

    query[0] = '\0';
    for (index = 0U; index < (sizeof(params) / sizeof(params[0])); ++index) {
        char encoded_key[96];
        char encoded_value[256];

        river_iflytek_url_encode(params[index].key, encoded_key, sizeof(encoded_key));
        river_iflytek_url_encode(params[index].value, encoded_value, sizeof(encoded_value));
        written = snprintf(query + strlen(query),
                           query_size - strlen(query),
                           "%s%s=%s",
                           (index == 0U) ? "" : "&",
                           encoded_key,
                           encoded_value);
        if (written <= 0 || (size_t)written >= (query_size - strlen(query))) {
            return RIVER_ERR_IO;
        }
    }

    written = snprintf(query + strlen(query),
                       query_size - strlen(query),
                       "&signature=%s",
                       signature_encoded);
    if (written <= 0 || (size_t)written >= (query_size - strlen(query))) {
        return RIVER_ERR_IO;
    }

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

static bool river_iflytek_json_looks_structured(const char *json, int json_len)
{
    int index;

    if (json == NULL || json_len <= 0) {
        return false;
    }

    for (index = 0; index < json_len; ++index) {
        char ch = json[index];

        if (ch == ' ' || ch == '\r' || ch == '\n' || ch == '\t') {
            continue;
        }
        return ch == '{' || ch == '[';
    }

    return false;
}

static void river_iflytek_handle_text_message(const char *json, int json_len)
{
    cJSON *root;
    cJSON *action_obj;
    cJSON *msg_type_obj;
    cJSON *res_type_obj;
    cJSON *code_obj;
    cJSON *desc_obj;
    cJSON *sid_obj;
    cJSON *data_obj;
    cJSON *data_action_obj;
    cJSON *data_desc_obj;
    cJSON *data_detail_obj;
    cJSON *cn_obj;
    cJSON *st_obj;
    cJSON *ls_obj;
    cJSON *type_obj;
    const char *action_text;
    const char *data_action_text;
    const char *msg_type_text;
    const char *res_type_text;
    const char *data_desc_text;
    const char *data_detail_text;
    int code;
    bool is_final;
    bool has_code;
    bool success_without_code;
    char text[RIVER_IFLYTEK_RTASR_TEXT_MAX];
    char message[RIVER_IFLYTEK_RTASR_DESC_MAX];

    if (!river_iflytek_json_looks_structured(json, json_len)) {
        return;
    }

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
    msg_type_obj = cJSON_GetObjectItemCaseSensitive(root, "msg_type");
    res_type_obj = cJSON_GetObjectItemCaseSensitive(root, "res_type");
    code_obj = cJSON_GetObjectItemCaseSensitive(root, "code");
    desc_obj = cJSON_GetObjectItemCaseSensitive(root, "desc");
    sid_obj = cJSON_GetObjectItemCaseSensitive(root, "sid");
    action_text = cJSON_IsString(action_obj) ? action_obj->valuestring : NULL;
    msg_type_text = cJSON_IsString(msg_type_obj) ? msg_type_obj->valuestring : NULL;
    res_type_text = cJSON_IsString(res_type_obj) ? res_type_obj->valuestring : NULL;
    has_code = cJSON_IsNumber(code_obj);
    code = has_code ? code_obj->valueint : 0;
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

    data_obj = cJSON_GetObjectItemCaseSensitive(root, "data");
    data_action_obj = cJSON_IsObject(data_obj) ?
        cJSON_GetObjectItemCaseSensitive(data_obj, "action") : NULL;
    data_desc_obj = cJSON_IsObject(data_obj) ?
        cJSON_GetObjectItemCaseSensitive(data_obj, "desc") : NULL;
    data_detail_obj = cJSON_IsObject(data_obj) ?
        cJSON_GetObjectItemCaseSensitive(data_obj, "detail") : NULL;
    data_action_text = cJSON_IsString(data_action_obj) ? data_action_obj->valuestring : NULL;
    data_desc_text = cJSON_IsString(data_desc_obj) ? data_desc_obj->valuestring : NULL;
    data_detail_text = cJSON_IsString(data_detail_obj) ? data_detail_obj->valuestring : NULL;
    success_without_code =
        (!has_code) &&
        ((action_text != NULL && strcmp(action_text, "started") == 0) ||
         (data_action_text != NULL && strcmp(data_action_text, "started") == 0) ||
         (msg_type_text != NULL && strcmp(msg_type_text, "action") == 0) ||
         (msg_type_text != NULL && strcmp(msg_type_text, "result") == 0) ||
         (res_type_text != NULL && strcmp(res_type_text, "result") == 0) ||
         (res_type_text != NULL && strcmp(res_type_text, "asr") == 0) ||
         cJSON_IsObject(data_obj));

    if (has_code && code != 0) {
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

    if (!has_code && !success_without_code) {
        cJSON_Delete(root);
        return;
    }

    if ((res_type_text != NULL && strcmp(res_type_text, "frc") == 0) ||
        (action_text != NULL && strcmp(action_text, "error") == 0) ||
        (data_action_text != NULL && strcmp(data_action_text, "error") == 0)) {
        const char *error_text = NULL;

        if (message[0] != '\0') {
            error_text = message;
        } else if (data_desc_text != NULL && data_desc_text[0] != '\0') {
            error_text = data_desc_text;
        } else if (data_detail_text != NULL && data_detail_text[0] != '\0') {
            error_text = data_detail_text;
        } else {
            error_text = "iflytek returned frc/error action";
        }

        snprintf(g_river_iflytek_rtasr.last_error,
                 sizeof(g_river_iflytek_rtasr.last_error),
                 "%s",
                 error_text);
        g_river_iflytek_rtasr.error_results++;
        river_iflytek_emit_result(RIVER_CLOUD_ASR_EVENT_ERROR,
                                  NULL,
                                  g_river_iflytek_rtasr.last_session_id[0] != '\0' ?
                                      g_river_iflytek_rtasr.last_session_id :
                                      g_river_iflytek_rtasr.last_sid,
                                  g_river_iflytek_rtasr.last_error,
                                  has_code ? code : -2,
                                  false);
        cJSON_Delete(root);
        return;
    }

    if ((action_text != NULL && strcmp(action_text, "started") == 0) ||
        (data_action_text != NULL && strcmp(data_action_text, "started") == 0) ||
        (msg_type_text != NULL && strcmp(msg_type_text, "started") == 0) ||
        ((msg_type_text != NULL && strcmp(msg_type_text, "action") == 0) &&
         (data_action_text != NULL && strcmp(data_action_text, "started") == 0))) {
        cJSON *data_obj_started;
        cJSON *session_id_obj;

        data_obj_started = data_obj;
        session_id_obj = cJSON_IsObject(data_obj_started) ?
                         cJSON_GetObjectItemCaseSensitive(data_obj_started, "sessionId") : NULL;
        if (cJSON_IsString(session_id_obj) && session_id_obj->valuestring != NULL) {
            snprintf(g_river_iflytek_rtasr.last_session_id,
                     sizeof(g_river_iflytek_rtasr.last_session_id),
                     "%s",
                     session_id_obj->valuestring);
        }
        g_river_iflytek_rtasr.session_started = true;
        river_iflytek_emit_result(RIVER_CLOUD_ASR_EVENT_SESSION_STARTED,
                                  NULL,
                                  g_river_iflytek_rtasr.last_session_id[0] != '\0' ?
                                      g_river_iflytek_rtasr.last_session_id :
                                      g_river_iflytek_rtasr.last_sid,
                                  message,
                                  code,
                                  false);
        cJSON_Delete(root);
        return;
    }

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
               (cJSON_IsNumber(type_obj) && type_obj->valueint == 0) ||
               (cJSON_IsString(type_obj) && type_obj->valuestring != NULL &&
                strcmp(type_obj->valuestring, "0") == 0);

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
    } else if (is_final) {
        g_river_iflytek_rtasr.last_text[0] = '\0';
        g_river_iflytek_rtasr.final_results++;
        river_iflytek_emit_result(RIVER_CLOUD_ASR_EVENT_FINAL,
                                  "",
                                  g_river_iflytek_rtasr.last_sid,
                                  message,
                                  code,
                                  true);
    }

    cJSON_Delete(root);
}

static void river_iflytek_ws_message_cb(wsclient_context **wsclient,
                                        int data_len,
                                        enum opcode_type opcode)
{
    (void)wsclient;
    if (g_river_iflytek_rtasr.wsclient == NULL ||
        g_river_iflytek_rtasr.wsclient->receivedData == NULL || data_len <= 0) {
        return;
    }

    /*
     * Ameba's wsclient may surface fragmented server JSON messages with the
     * final opcode reported as CONTINUATION instead of TEXT_FRAME. Accept both
     * so that "started", partial, and final result payloads are not dropped.
     */
    if (opcode != TEXT_FRAME && opcode != CONTINUATION) {
        return;
    }

    g_river_iflytek_rtasr.receive_messages++;
    river_iflytek_handle_text_message((const char *)g_river_iflytek_rtasr.wsclient->receivedData,
                                      data_len);
}

static void river_iflytek_ws_dispatch_message(wsclient_context **wsclient,
                                              int data_len,
                                              enum opcode_type opcode,
                                              void *user_data)
{
    (void)user_data;
    river_iflytek_ws_message_cb(wsclient, data_len, opcode);
}

static void river_iflytek_close_context(bool emit_close_event)
{
    uint32_t wait_loops;

    if (g_river_iflytek_rtasr.wsclient != NULL) {
        river_ws_dispatch_unregister(g_river_iflytek_rtasr.wsclient);
    }

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
        g_river_iflytek_rtasr.staged_audio_bytes = 0U;
        g_river_iflytek_rtasr.local_queue_head = 0U;
        g_river_iflytek_rtasr.local_queue_tail = 0U;
        g_river_iflytek_rtasr.local_queue_count = 0U;
        g_river_iflytek_rtasr.session_started = false;
        g_river_iflytek_rtasr.sessions_closed++;
        if (emit_close_event) {
            river_iflytek_emit_result(RIVER_CLOUD_ASR_EVENT_SESSION_CLOSED,
                                      NULL,
                                      g_river_iflytek_rtasr.last_session_id[0] != '\0' ?
                                          g_river_iflytek_rtasr.last_session_id :
                                          g_river_iflytek_rtasr.last_sid,
                                      NULL,
                                      0,
                                      true);
        }
    }
}

static bool river_iflytek_local_queue_push(const uint8_t *audio, uint16_t bytes)
{
    uint8_t slot;

    if (audio == NULL || bytes == 0U || bytes > RIVER_IFLYTEK_RTASR_CHUNK_BYTES) {
        return false;
    }
    if (g_river_iflytek_rtasr.local_queue_count >= RIVER_IFLYTEK_RTASR_LOCAL_QUEUE_SLOTS) {
        g_river_iflytek_rtasr.local_chunks_dropped++;
        snprintf(g_river_iflytek_rtasr.last_error,
                 sizeof(g_river_iflytek_rtasr.last_error),
                 "%s",
                 "local audio queue full");
        return false;
    }

    slot = g_river_iflytek_rtasr.local_queue_tail;
    memcpy(g_river_iflytek_rtasr.local_queue[slot], audio, bytes);
    g_river_iflytek_rtasr.local_queue_len[slot] = bytes;
    g_river_iflytek_rtasr.local_queue_tail =
        (uint8_t)((g_river_iflytek_rtasr.local_queue_tail + 1U) % RIVER_IFLYTEK_RTASR_LOCAL_QUEUE_SLOTS);
    g_river_iflytek_rtasr.local_queue_count++;
    g_river_iflytek_rtasr.local_chunks_queued++;
    return true;
}

static river_status_t river_iflytek_drain_local_queue(bool force_flush)
{
    uint32_t now_ms;
    uint32_t elapsed_ms;

    if (!g_river_iflytek_rtasr.stream_open || g_river_iflytek_rtasr.wsclient == NULL) {
        return RIVER_ERR_BUSY;
    }

    ws_poll(0, &g_river_iflytek_rtasr.wsclient);

    while (g_river_iflytek_rtasr.local_queue_count > 0U) {
        if (g_river_iflytek_rtasr.wsclient == NULL ||
            g_river_iflytek_rtasr.wsclient->readyState != WSC_OPEN) {
            return RIVER_ERR_IO;
        }

        if (!force_flush &&
            g_river_iflytek_rtasr.wsclient->ready_send_buf_num >=
                (g_river_iflytek_rtasr.wsclient->max_queue_size / 2)) {
            return RIVER_OK;
        }

        now_ms = rtos_time_get_current_system_time_ms();
        elapsed_ms = now_ms - g_river_iflytek_rtasr.last_send_ms;
        if (!force_flush && g_river_iflytek_rtasr.last_send_ms != 0U &&
            elapsed_ms < RIVER_IFLYTEK_RTASR_SEND_INTERVAL_MS) {
            return RIVER_OK;
        }

        if (ws_sendBinary(g_river_iflytek_rtasr.local_queue[g_river_iflytek_rtasr.local_queue_head],
                          (int)g_river_iflytek_rtasr.local_queue_len[g_river_iflytek_rtasr.local_queue_head],
                          1,
                          g_river_iflytek_rtasr.wsclient) != 0) {
            snprintf(g_river_iflytek_rtasr.last_error,
                     sizeof(g_river_iflytek_rtasr.last_error),
                     "%s",
                     "ws binary send backlog");
            return force_flush ? RIVER_ERR_IO : RIVER_ERR_BUSY;
        }

        g_river_iflytek_rtasr.sent_audio_bytes +=
            g_river_iflytek_rtasr.local_queue_len[g_river_iflytek_rtasr.local_queue_head];
        g_river_iflytek_rtasr.sent_frames++;
        g_river_iflytek_rtasr.local_queue_head =
            (uint8_t)((g_river_iflytek_rtasr.local_queue_head + 1U) % RIVER_IFLYTEK_RTASR_LOCAL_QUEUE_SLOTS);
        g_river_iflytek_rtasr.local_queue_count--;
        g_river_iflytek_rtasr.last_send_ms = now_ms;
        ws_poll(force_flush ? 20 : 5, &g_river_iflytek_rtasr.wsclient);
    }

    return RIVER_OK;
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
    return river_ws_dispatch_init();
}

static void river_iflytek_deinit(void)
{
    river_iflytek_close_context(false);
    memset(&g_river_iflytek_rtasr, 0, sizeof(g_river_iflytek_rtasr));
}

static river_status_t river_iflytek_stream_open(const river_cloud_asr_audio_desc_t *audio)
{
    char query[RIVER_IFLYTEK_RTASR_QUERY_MAX];
    char base_url[RIVER_IFLYTEK_RTASR_URL_MAX];
    char path_query[RIVER_IFLYTEK_RTASR_PATH_QUERY_MAX];
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

    snprintf(base_url,
             sizeof(base_url),
             "%s://%s",
             RIVER_IFLYTEK_RTASR_SCHEME,
             RIVER_IFLYTEK_RTASR_HOST);
    snprintf(path_query,
             sizeof(path_query),
             "%s?%s",
             RIVER_IFLYTEK_RTASR_PATH,
             query);

    g_river_iflytek_rtasr.wsclient =
        create_wsclient(base_url,
                        RIVER_IFLYTEK_RTASR_PORT,
                        path_query,
                        NULL,
                        RIVER_IFLYTEK_RTASR_TX_MAX,
                        RIVER_IFLYTEK_RTASR_RX_MAX,
                        RIVER_IFLYTEK_RTASR_QUEUE_MAX);
    if (g_river_iflytek_rtasr.wsclient == NULL) {
        return RIVER_ERR_NO_MEMORY;
    }

    if (river_ws_dispatch_register(g_river_iflytek_rtasr.wsclient,
                                   river_iflytek_ws_dispatch_message,
                                   NULL,
                                   NULL) != RIVER_OK) {
        river_iflytek_close_context(false);
        snprintf(g_river_iflytek_rtasr.last_error,
                 sizeof(g_river_iflytek_rtasr.last_error),
                 "register ws dispatch failed");
        return RIVER_ERR_BUSY;
    }

    ws_setsockopt_timeout(10000U, 10000U, 15000U);
    ws_set_senddata_block_time(RIVER_IFLYTEK_RTASR_SEND_BLOCK_MS);
    ws_multisend_opts(g_river_iflytek_rtasr.wsclient, (RIVER_IFLYTEK_RTASR_QUEUE_MAX * 3U) / 4U);

    RIVER_LOGI("initiating ws connection via %s%s", base_url, path_query);
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
    g_river_iflytek_rtasr.local_chunks_queued = 0U;
    g_river_iflytek_rtasr.local_chunks_dropped = 0U;
    g_river_iflytek_rtasr.staged_audio_bytes = 0U;
    g_river_iflytek_rtasr.local_queue_head = 0U;
    g_river_iflytek_rtasr.local_queue_tail = 0U;
    g_river_iflytek_rtasr.local_queue_count = 0U;
    g_river_iflytek_rtasr.last_send_ms = 0U;
    g_river_iflytek_rtasr.session_started = false;
    g_river_iflytek_rtasr.last_sid[0] = '\0';
    g_river_iflytek_rtasr.last_session_id[0] = '\0';
    g_river_iflytek_rtasr.last_text[0] = '\0';
    g_river_iflytek_rtasr.last_error[0] = '\0';
    {
        uint32_t waited_ms = 0U;
        while (!g_river_iflytek_rtasr.session_started &&
               waited_ms < RIVER_IFLYTEK_RTASR_OPEN_READY_WAIT_MS &&
               g_river_iflytek_rtasr.wsclient != NULL &&
               g_river_iflytek_rtasr.wsclient->readyState == WSC_OPEN) {
            ws_poll(50, &g_river_iflytek_rtasr.wsclient);
            waited_ms += 50U;
        }
        if (!g_river_iflytek_rtasr.session_started) {
            RIVER_LOGW("session started ack not observed within %ums; continue with ws-open send mode",
                       (unsigned int)RIVER_IFLYTEK_RTASR_OPEN_READY_WAIT_MS);
        }
    }
    RIVER_LOGI("stream open success: seq=%lu", (unsigned long)g_river_iflytek_rtasr.sequence);
    return RIVER_OK;
}

static river_status_t river_iflytek_stream_feed(const uint8_t *pcm, size_t bytes)
{
    size_t copy_bytes;

    if (!g_river_iflytek_rtasr.stream_open || g_river_iflytek_rtasr.wsclient == NULL ||
        pcm == NULL || bytes == 0U) {
        return RIVER_ERR_ARG;
    }

    while (bytes > 0U) {
        copy_bytes = RIVER_IFLYTEK_RTASR_CHUNK_BYTES - g_river_iflytek_rtasr.staged_audio_bytes;
        if (copy_bytes > bytes) {
            copy_bytes = bytes;
        }

        memcpy(g_river_iflytek_rtasr.staged_audio + g_river_iflytek_rtasr.staged_audio_bytes,
               pcm,
               copy_bytes);
        g_river_iflytek_rtasr.staged_audio_bytes += copy_bytes;
        pcm += copy_bytes;
        bytes -= copy_bytes;

        if (g_river_iflytek_rtasr.staged_audio_bytes >= RIVER_IFLYTEK_RTASR_CHUNK_BYTES) {
            if (!river_iflytek_local_queue_push(g_river_iflytek_rtasr.staged_audio,
                                                (uint16_t)RIVER_IFLYTEK_RTASR_CHUNK_BYTES)) {
                return RIVER_ERR_BUSY;
            }
            g_river_iflytek_rtasr.staged_audio_bytes = 0U;
            {
                river_status_t send_status = river_iflytek_drain_local_queue(false);
                if (send_status != RIVER_OK) {
                    return send_status;
                }
            }
        }
    }

    return RIVER_OK;
}

static river_status_t river_iflytek_stream_finish(void)
{
    char end_frame[160];

    if (!g_river_iflytek_rtasr.stream_open || g_river_iflytek_rtasr.wsclient == NULL) {
        return RIVER_OK;
    }

    if (g_river_iflytek_rtasr.staged_audio_bytes > 0U) {
        if (!river_iflytek_local_queue_push(g_river_iflytek_rtasr.staged_audio,
                                            (uint16_t)g_river_iflytek_rtasr.staged_audio_bytes)) {
            river_iflytek_close_context(false);
            return RIVER_ERR_IO;
        }
        g_river_iflytek_rtasr.staged_audio_bytes = 0U;
    }

    if (river_iflytek_drain_local_queue(true) != RIVER_OK) {
        river_iflytek_close_context(false);
        return RIVER_ERR_IO;
    }

    if (g_river_iflytek_rtasr.last_session_id[0] != '\0') {
        snprintf(end_frame,
                 sizeof(end_frame),
                 "{\"end\":true,\"sessionId\":\"%s\"}",
                 g_river_iflytek_rtasr.last_session_id);
    } else {
        snprintf(end_frame, sizeof(end_frame), "{\"end\":true}");
    }

    if (ws_send((char *)end_frame,
                strlen(end_frame),
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
    (void)river_iflytek_drain_local_queue(false);
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
    RIVER_LOGI("stream=%s seq=%lu opened=%lu closed=%lu partial=%lu final=%lu error=%lu recv=%lu audio_bytes=%lu frames=%lu local_q=%u queued=%lu dropped=%lu sid=%s last_text=%s last_err=%s",
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
               (unsigned int)g_river_iflytek_rtasr.local_queue_count,
               (unsigned long)g_river_iflytek_rtasr.local_chunks_queued,
               (unsigned long)g_river_iflytek_rtasr.local_chunks_dropped,
               g_river_iflytek_rtasr.last_sid[0] != '\0' ? g_river_iflytek_rtasr.last_sid :
               (g_river_iflytek_rtasr.last_session_id[0] != '\0' ? g_river_iflytek_rtasr.last_session_id : "-"),
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
