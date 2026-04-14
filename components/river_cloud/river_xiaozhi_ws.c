/* 小智 WebSocket 协议实现：处理 OTA bootstrap、消息收发与音频上行。 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "httpc/httpc.h"
#include "lwip/def.h"
#include "lwip_netconf.h"
#include "os_wrapper.h"
#include "websocket/libwsclient.h"
#include "websocket/wsclient_api.h"

#include "river/river_log.h"
#include "river/river_playback_service.h"
#include "river/river_wifi_station.h"
#include "river/river_xiaozhi_credentials.h"
#include "river/river_xiaozhi_ws.h"
#include "river_ws_dispatch.h"
#include "river_xiaozhi_mcp_bridge.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.cloud.xiaozhi"

#define RIVER_XIAOZHI_URL_MAX              192U
#define RIVER_XIAOZHI_OTA_URL_MAX          192U
#define RIVER_XIAOZHI_TOKEN_MAX            256U
#define RIVER_XIAOZHI_BASE_URL_MAX         128U
#define RIVER_XIAOZHI_HTTP_HOST_MAX        128U
#define RIVER_XIAOZHI_HTTP_RESOURCE_MAX    192U
#define RIVER_XIAOZHI_PATH_MAX             192U
#define RIVER_XIAOZHI_HEADER_FIELDS_MAX    512U
#define RIVER_XIAOZHI_DEVICE_ID_MAX        32U
#define RIVER_XIAOZHI_CLIENT_ID_MAX        48U
#define RIVER_XIAOZHI_SESSION_ID_MAX       96U
#define RIVER_XIAOZHI_LAST_TEXT_MAX        192U
#define RIVER_XIAOZHI_LAST_ERROR_MAX       128U
#define RIVER_XIAOZHI_LAST_TYPE_MAX        32U
#define RIVER_XIAOZHI_LAST_STATE_MAX       32U
#define RIVER_XIAOZHI_LAST_EMOTION_MAX     32U
#define RIVER_XIAOZHI_ACTIVATION_CODE_MAX  16U
#define RIVER_XIAOZHI_ACTIVATION_MESSAGE_MAX 192U
#define RIVER_XIAOZHI_ACTIVATION_CHALLENGE_MAX 192U
#define RIVER_XIAOZHI_MCP_RESPONSE_MAX     640U
#define RIVER_XIAOZHI_HTTP_RESPONSE_MAX    4096U

#define RIVER_XIAOZHI_HTTP_USER_AGENT      "ameba-river/xiaozhi"
#define RIVER_XIAOZHI_OTA_RETRY_MIN_MS     5000U
#define RIVER_XIAOZHI_OTA_RETRY_MAX_MS     30000U
#define RIVER_XIAOZHI_WS_RECV_TIMEOUT_MS   10000U
#define RIVER_XIAOZHI_WS_SEND_TIMEOUT_MS   200U
#define RIVER_XIAOZHI_WS_CONNECT_TIMEOUT_MS 15000U
#define RIVER_XIAOZHI_WS_SEND_BLOCK_MS     0U
#define RIVER_XIAOZHI_WS_AUDIO_QUEUE_RESERVE 2U
#define RIVER_XIAOZHI_WS_QUEUE_LOG_INTERVAL_MS 1000U
#define RIVER_XIAOZHI_POLL_LOCK_SLICE_MS   5U
#define RIVER_XIAOZHI_CONNECT_HEAP_RECLAIM_THRESHOLD (64U * 1024U)
typedef struct {
    uint16_t version;
    uint16_t type;
    uint32_t reserved;
    uint32_t timestamp;
    uint32_t payload_size;
    uint8_t payload[];
} __attribute__((packed)) river_xiaozhi_binary_v2_t;

typedef struct {
    uint8_t type;
    uint8_t reserved;
    uint16_t payload_size;
    uint8_t payload[];
} __attribute__((packed)) river_xiaozhi_binary_v3_t;

enum {
    RIVER_XIAOZHI_BINARY_V2_HEADER_BYTES = sizeof(river_xiaozhi_binary_v2_t),
    RIVER_XIAOZHI_BINARY_V3_HEADER_BYTES = sizeof(river_xiaozhi_binary_v3_t),
    /*
     * The current uplink path already encodes Opus into a fixed 512-byte
     * packet buffer in river_cloud_adapter. Keep the websocket framing
     * scratch sized to that contract instead of allocating per packet.
     */
    RIVER_XIAOZHI_BINARY_PAYLOAD_MAX = 512U,
    RIVER_XIAOZHI_BINARY_FRAME_MAX =
        RIVER_XIAOZHI_BINARY_PAYLOAD_MAX + RIVER_XIAOZHI_BINARY_V2_HEADER_BYTES
};

typedef struct {
    bool initialized;
    bool session_open;
    bool server_hello_received;
    bool ws_closed;
    bool config_ready;
    bool dialog_started;
    bool response_started;
    river_xiaozhi_config_t config;
    wsclient_context *wsclient;
    rtos_mutex_t lock;
    river_xiaozhi_event_handler_t event_handler;
    void *event_handler_user;
    uint32_t next_sequence;
    uint32_t server_sample_rate;
    uint32_t server_frame_duration_ms;
    uint32_t text_messages_rx;
    uint32_t text_messages_tx;
    uint32_t audio_messages_rx;
    uint32_t audio_messages_tx;
    uint32_t sessions_opened;
    uint32_t sessions_closed;
    uint32_t mcp_requests_rx;
    uint32_t mcp_responses_tx;
    uint32_t mcp_failures;
    uint32_t activation_timeout_ms;
    uint32_t bootstrap_failures;
    uint32_t send_backpressure_events;
    uint32_t send_backpressure_reserve_events;
    uint32_t send_backpressure_full_events;
    uint32_t send_queue_high_watermark;
    bool bootstrap_config_owned;
    bool activation_code_present;
    uint64_t bootstrap_retry_after_ms;
    uint64_t bootstrap_cache_expire_at_ms;
    uint64_t last_backpressure_log_ms;
    char url[RIVER_XIAOZHI_URL_MAX];
    char ota_url[RIVER_XIAOZHI_OTA_URL_MAX];
    char token[RIVER_XIAOZHI_TOKEN_MAX];
    char device_id[RIVER_XIAOZHI_DEVICE_ID_MAX];
    char client_id[RIVER_XIAOZHI_CLIENT_ID_MAX];
    char session_id[RIVER_XIAOZHI_SESSION_ID_MAX];
    char last_text[RIVER_XIAOZHI_LAST_TEXT_MAX];
    char last_error[RIVER_XIAOZHI_LAST_ERROR_MAX];
    char last_type[RIVER_XIAOZHI_LAST_TYPE_MAX];
    char last_state[RIVER_XIAOZHI_LAST_STATE_MAX];
    char last_emotion[RIVER_XIAOZHI_LAST_EMOTION_MAX];
    char activation_code[RIVER_XIAOZHI_ACTIVATION_CODE_MAX];
    char activation_message[RIVER_XIAOZHI_ACTIVATION_MESSAGE_MAX];
    char activation_challenge[RIVER_XIAOZHI_ACTIVATION_CHALLENGE_MAX];
    /*
     * Keep bootstrap/open-session scratch buffers in the long-lived context
     * instead of on small worker stacks. Wakeword admission can open a xiaozhi
     * session from river_wake_evt, and 4KB response/path/header locals were
     * corrupting the return path on CA32.
     */
    char bootstrap_response[RIVER_XIAOZHI_HTTP_RESPONSE_MAX];
    char open_base_url[RIVER_XIAOZHI_BASE_URL_MAX];
    char open_path[RIVER_XIAOZHI_PATH_MAX];
    char open_header_fields[RIVER_XIAOZHI_HEADER_FIELDS_MAX];
    char open_device_id[RIVER_XIAOZHI_DEVICE_ID_MAX];
    char open_client_id[RIVER_XIAOZHI_CLIENT_ID_MAX];
    uint8_t binary_frame[RIVER_XIAOZHI_BINARY_FRAME_MAX];
} river_xiaozhi_context_t;

static river_xiaozhi_context_t g_river_xiaozhi;

static bool river_xiaozhi_transport_lock(void)
{
    if (!g_river_xiaozhi.initialized || g_river_xiaozhi.lock == 0) {
        return false;
    }

    return rtos_mutex_take(g_river_xiaozhi.lock, MUTEX_WAIT_TIMEOUT) == RTK_SUCCESS;
}

static void river_xiaozhi_transport_unlock(bool locked)
{
    if (!locked) {
        return;
    }

    rtos_mutex_give(g_river_xiaozhi.lock);
}

static const char *river_xiaozhi_bool_text(bool value)
{
    return value ? "yes" : "no";
}

static void river_xiaozhi_copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0U) {
        return;
    }

    snprintf(dst, dst_size, "%s", src != NULL ? src : "");
}

static void river_xiaozhi_set_last_error(const char *error_text)
{
    river_xiaozhi_copy_string(g_river_xiaozhi.last_error,
                              sizeof(g_river_xiaozhi.last_error),
                              error_text);
}

static void river_xiaozhi_send_queue_snapshot_locked(uint32_t *ready_out,
                                                     uint32_t *recycle_out,
                                                     uint32_t *max_out,
                                                     uint32_t *stable_out)
{
    uint32_t ready = 0U;
    uint32_t recycle = 0U;
    uint32_t max = 0U;
    uint32_t stable = 0U;

    if (g_river_xiaozhi.wsclient != NULL) {
        if (g_river_xiaozhi.wsclient->ready_send_buf_num > 0) {
            ready = (uint32_t)g_river_xiaozhi.wsclient->ready_send_buf_num;
        }
        if (g_river_xiaozhi.wsclient->recycle_send_buf_num > 0) {
            recycle = (uint32_t)g_river_xiaozhi.wsclient->recycle_send_buf_num;
        }
        if (g_river_xiaozhi.wsclient->max_queue_size > 0) {
            max = (uint32_t)g_river_xiaozhi.wsclient->max_queue_size;
        }
        if (g_river_xiaozhi.wsclient->stable_buf_num > 0) {
            stable = (uint32_t)g_river_xiaozhi.wsclient->stable_buf_num;
        }
    }

    if (max != 0U) {
        if (ready > max) {
            ready = max;
        }
        if (recycle > max) {
            recycle = max;
        }
        if (stable > max) {
            stable = max;
        }
        if (ready > g_river_xiaozhi.send_queue_high_watermark) {
            g_river_xiaozhi.send_queue_high_watermark = ready;
        }
    }

    if (ready_out != NULL) {
        *ready_out = ready;
    }
    if (recycle_out != NULL) {
        *recycle_out = recycle;
    }
    if (max_out != NULL) {
        *max_out = max;
    }
    if (stable_out != NULL) {
        *stable_out = stable;
    }
}

static uint32_t river_xiaozhi_send_queue_soft_limit(uint32_t max, uint32_t reserve_slots)
{
    if (max == 0U) {
        return 0U;
    }
    if (reserve_slots >= max) {
        reserve_slots = max - 1U;
    }
    return max - reserve_slots;
}

static bool river_xiaozhi_send_queue_backpressured_locked(uint32_t reserve_slots,
                                                          const char *kind)
{
    uint32_t ready;
    uint32_t recycle;
    uint32_t max;
    uint32_t stable;
    uint32_t free_slots;
    uint32_t soft_limit;
    uint64_t now_ms;
    const char *reason = "hard_full";

    river_xiaozhi_send_queue_snapshot_locked(&ready, &recycle, &max, &stable);
    if (max == 0U) {
        return false;
    }

    if (reserve_slots >= max) {
        reserve_slots = max - 1U;
    }
    soft_limit = river_xiaozhi_send_queue_soft_limit(max, reserve_slots);
    if ((ready + reserve_slots) < max) {
        return false;
    }

    free_slots = ready < max ? (max - ready) : 0U;
    if (ready < max) {
        reason = "soft_reserve";
        g_river_xiaozhi.send_backpressure_reserve_events++;
    } else {
        g_river_xiaozhi.send_backpressure_full_events++;
    }

    g_river_xiaozhi.send_backpressure_events++;
    river_xiaozhi_set_last_error("send_queue_busy");

    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    if (g_river_xiaozhi.last_backpressure_log_ms == 0U ||
        (now_ms - g_river_xiaozhi.last_backpressure_log_ms) >=
            RIVER_XIAOZHI_WS_QUEUE_LOG_INTERVAL_MS) {
        g_river_xiaozhi.last_backpressure_log_ms = now_ms;
        RIVER_LOGW("xiaozhi ws backpressure: kind=%s reason=%s ready=%lu recycle=%lu max=%lu stable=%lu reserve=%lu free=%lu soft_limit=%lu",
                   kind != NULL ? kind : "-",
                   reason,
                   (unsigned long)ready,
                   (unsigned long)recycle,
                   (unsigned long)max,
                   (unsigned long)stable,
                   (unsigned long)reserve_slots,
                   (unsigned long)free_slots,
                   (unsigned long)soft_limit);
    }
    return true;
}

static void river_xiaozhi_reclaim_heap_before_connect(void)
{
    uint32_t heap_free_before;
    uint32_t heap_free_after;

    heap_free_before = rtos_mem_get_free_heap_size();
    if (heap_free_before >= RIVER_XIAOZHI_CONNECT_HEAP_RECLAIM_THRESHOLD) {
        return;
    }

    if (!river_playback_service_release_idle_track_cache()) {
        return;
    }

    heap_free_after = rtos_mem_get_free_heap_size();
    RIVER_LOGW("xiaozhi preconnect reclaimed idle playback cache: heap_free=%lu->%lu threshold=%lu",
               (unsigned long)heap_free_before,
               (unsigned long)heap_free_after,
               (unsigned long)RIVER_XIAOZHI_CONNECT_HEAP_RECLAIM_THRESHOLD);
}

static bool river_xiaozhi_extract_six_digit_code(const char *text,
                                                 char *code_out,
                                                 size_t code_out_size)
{
    size_t index;

    if (text == NULL || code_out == NULL || code_out_size < 7U) {
        return false;
    }

    for (index = 0U; text[index] != '\0'; ++index) {
        size_t digit_count = 0U;
        size_t digit_index = index;

        if (text[index] < '0' || text[index] > '9') {
            continue;
        }
        if (index > 0U && text[index - 1U] >= '0' && text[index - 1U] <= '9') {
            continue;
        }

        while (text[digit_index] >= '0' && text[digit_index] <= '9' && digit_count < 7U) {
            digit_index++;
            digit_count++;
        }

        if (digit_count == 6U &&
            (text[digit_index] == '\0' || text[digit_index] < '0' || text[digit_index] > '9')) {
            memcpy(code_out, text + index, 6U);
            code_out[6] = '\0';
            return true;
        }
    }

    return false;
}

static void river_xiaozhi_update_activation_from_text(const char *text, const char *source)
{
    char code[7];

    if (text == NULL || text[0] == '\0') {
        return;
    }
    if ((source == NULL || strcmp(source, "ota_message") != 0) &&
        strstr(text, "绑定") == NULL &&
        strstr(text, "控制面板") == NULL &&
        strstr(text, "activation") == NULL) {
        return;
    }
    if (!river_xiaozhi_extract_six_digit_code(text, code, sizeof(code))) {
        return;
    }

    river_xiaozhi_copy_string(g_river_xiaozhi.activation_code,
                              sizeof(g_river_xiaozhi.activation_code),
                              code);
    river_xiaozhi_copy_string(g_river_xiaozhi.activation_message,
                              sizeof(g_river_xiaozhi.activation_message),
                              text);
    g_river_xiaozhi.activation_code_present = true;
    RIVER_LOGI("xiaozhi activation code detected: source=%s code=%s text=%s",
               source != NULL ? source : "-",
               g_river_xiaozhi.activation_code,
               g_river_xiaozhi.activation_message);
}

static void river_xiaozhi_clear_activation_state(void)
{
    g_river_xiaozhi.activation_code[0] = '\0';
    g_river_xiaozhi.activation_message[0] = '\0';
    g_river_xiaozhi.activation_challenge[0] = '\0';
    g_river_xiaozhi.activation_timeout_ms = 0U;
    g_river_xiaozhi.activation_code_present = false;
}

static void river_xiaozhi_set_last_type(const char *type_text)
{
    river_xiaozhi_copy_string(g_river_xiaozhi.last_type,
                              sizeof(g_river_xiaozhi.last_type),
                              type_text);
}

static void river_xiaozhi_set_last_text(const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }

    river_xiaozhi_copy_string(g_river_xiaozhi.last_text,
                              sizeof(g_river_xiaozhi.last_text),
                              text);
}

static void river_xiaozhi_set_last_state(const char *state_text)
{
    if (state_text == NULL || state_text[0] == '\0') {
        return;
    }

    river_xiaozhi_copy_string(g_river_xiaozhi.last_state,
                              sizeof(g_river_xiaozhi.last_state),
                              state_text);
}

static void river_xiaozhi_set_last_emotion(const char *emotion_text)
{
    if (emotion_text == NULL || emotion_text[0] == '\0') {
        return;
    }

    river_xiaozhi_copy_string(g_river_xiaozhi.last_emotion,
                              sizeof(g_river_xiaozhi.last_emotion),
                              emotion_text);
}

static void river_xiaozhi_reset_dialog_state(void)
{
    g_river_xiaozhi.dialog_started = false;
    g_river_xiaozhi.response_started = false;
    g_river_xiaozhi.session_id[0] = '\0';
}

static void river_xiaozhi_reset_runtime_state(void)
{
    g_river_xiaozhi.session_open = false;
    g_river_xiaozhi.server_hello_received = false;
    g_river_xiaozhi.ws_closed = true;
    g_river_xiaozhi.next_sequence = 1U;
    river_xiaozhi_reset_dialog_state();
}

static void river_xiaozhi_format_timestamp(char *buffer, size_t buffer_size)
{
    time_t now;
    struct tm tm_utc;
    struct tm *tm_ptr = NULL;

    if (buffer == NULL || buffer_size == 0U) {
        return;
    }

    now = time(NULL);
#if defined(_POSIX_THREAD_SAFE_FUNCTIONS)
    if (gmtime_r(&now, &tm_utc) != NULL) {
        tm_ptr = &tm_utc;
    }
#else
    {
        struct tm *tmp = gmtime(&now);
        if (tmp != NULL) {
            tm_utc = *tmp;
            tm_ptr = &tm_utc;
        }
    }
#endif

    if (tm_ptr == NULL) {
        snprintf(buffer, buffer_size, "1970-01-01T00:00:00Z");
        return;
    }

    snprintf(buffer,
             buffer_size,
             "%04d-%02d-%02dT%02d:%02d:%02dZ",
             tm_ptr->tm_year + 1900,
             tm_ptr->tm_mon + 1,
             tm_ptr->tm_mday,
             tm_ptr->tm_hour,
             tm_ptr->tm_min,
             tm_ptr->tm_sec);
}

static uint32_t river_xiaozhi_next_sequence(void)
{
    uint32_t sequence = g_river_xiaozhi.next_sequence;

    if (sequence == 0U) {
        sequence = 1U;
    }
    g_river_xiaozhi.next_sequence = sequence + 1U;
    if (g_river_xiaozhi.next_sequence == 0U) {
        g_river_xiaozhi.next_sequence = 1U;
    }
    return sequence;
}

static cJSON *river_xiaozhi_create_control_event(const char *type, cJSON **payload_out)
{
    cJSON *root = NULL;
    cJSON *payload = NULL;
    char timestamp[32];

    if (type == NULL) {
        return NULL;
    }

    root = cJSON_CreateObject();
    payload = cJSON_CreateObject();
    if (root == NULL || payload == NULL) {
        if (payload != NULL) {
            cJSON_Delete(payload);
        }
        if (root != NULL) {
            cJSON_Delete(root);
        }
        return NULL;
    }

    river_xiaozhi_format_timestamp(timestamp, sizeof(timestamp));
    cJSON_AddStringToObject(root, "type", type);
    if (g_river_xiaozhi.session_id[0] != '\0') {
        cJSON_AddStringToObject(root, "session_id", g_river_xiaozhi.session_id);
    }
    cJSON_AddNumberToObject(root, "seq", river_xiaozhi_next_sequence());
    cJSON_AddStringToObject(root, "ts", timestamp);
    cJSON_AddItemToObject(root, "payload", payload);
    if (payload_out != NULL) {
        *payload_out = payload;
    }
    return root;
}

static void river_xiaozhi_update_session_id_from_root(const cJSON *root)
{
    const cJSON *session_id_obj;

    if (root == NULL) {
        return;
    }

    session_id_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)root, "session_id");
    if (cJSON_IsString(session_id_obj) && session_id_obj->valuestring != NULL &&
        session_id_obj->valuestring[0] != '\0') {
        river_xiaozhi_copy_string(g_river_xiaozhi.session_id,
                                  sizeof(g_river_xiaozhi.session_id),
                                  session_id_obj->valuestring);
    }
}

static void river_xiaozhi_emit_event(river_xiaozhi_event_type_t type,
                                     const char *text,
                                     const char *state,
                                     const char *emotion,
                                     uint32_t sample_rate,
                                     uint32_t frame_duration_ms,
                                     uint32_t timestamp_ms,
                                     const uint8_t *binary_data,
                                     size_t binary_bytes,
                                     uint16_t binary_type)
{
    river_xiaozhi_event_t event;

    if (g_river_xiaozhi.event_handler == NULL) {
        return;
    }

    memset(&event, 0, sizeof(event));
    event.type = type;
    event.session_id = g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : NULL;
    event.text = text;
    event.state = state;
    event.emotion = emotion;
    event.sample_rate = sample_rate;
    event.frame_duration_ms = frame_duration_ms;
    event.timestamp_ms = timestamp_ms;
    event.binary_data = binary_data;
    event.binary_bytes = binary_bytes;
    event.binary_type = binary_type;

    g_river_xiaozhi.event_handler(&event, g_river_xiaozhi.event_handler_user);
}

static river_status_t river_xiaozhi_send_binary_frame(uint16_t type,
                                                      const uint8_t *payload,
                                                      size_t payload_bytes,
                                                      uint32_t timestamp_ms)
{
    river_status_t status = RIVER_ERR_IO;
    bool locked = false;
    (void)type;
    (void)timestamp_ms;

    if (payload == NULL || payload_bytes == 0U) {
        return RIVER_ERR_ARG;
    }

    locked = river_xiaozhi_transport_lock();
    if (!locked || g_river_xiaozhi.wsclient == NULL ||
        g_river_xiaozhi.wsclient->readyState != WSC_OPEN) {
        status = RIVER_ERR_BUSY;
        goto exit;
    }

    if (river_xiaozhi_send_queue_backpressured_locked(RIVER_XIAOZHI_WS_AUDIO_QUEUE_RESERVE,
                                                      "audio")) {
        status = RIVER_ERR_BUSY;
        goto exit;
    }

    if (ws_sendBinary((uint8_t *)payload, (int)payload_bytes, 1, g_river_xiaozhi.wsclient) == 0) {
        g_river_xiaozhi.audio_messages_tx++;
        status = RIVER_OK;
    } else {
        river_xiaozhi_set_last_error("binary_send_failed");
    }

exit:
    river_xiaozhi_transport_unlock(locked);
    return status;
}

static void river_xiaozhi_build_device_id(char *buffer, size_t buffer_size)
{
    uint8_t *mac;

    if (buffer == NULL || buffer_size == 0U) {
        return;
    }

    mac = LwIP_GetMAC(NETIF_WLAN_STA_INDEX);
    if (mac == NULL) {
        snprintf(buffer, buffer_size, "%s", "00:00:00:00:00:00");
        return;
    }

    snprintf(buffer,
             buffer_size,
             "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0],
             mac[1],
             mac[2],
             mac[3],
             mac[4],
             mac[5]);
}

static uint32_t river_xiaozhi_fnv1a32(const uint8_t *data, size_t bytes, uint32_t seed)
{
    size_t index;
    uint32_t hash = 2166136261UL ^ seed;

    for (index = 0U; index < bytes; ++index) {
        hash ^= (uint32_t)data[index];
        hash *= 16777619UL;
    }
    return hash;
}

static void river_xiaozhi_build_client_id(char *buffer, size_t buffer_size)
{
    uint8_t *mac;
    uint8_t uuid[16];
    uint32_t hash_words[4];
    size_t index;

    if (buffer == NULL || buffer_size == 0U) {
        return;
    }

    mac = LwIP_GetMAC(NETIF_WLAN_STA_INDEX);
    if (mac == NULL) {
        static const uint8_t k_zero_mac[6] = {0U, 0U, 0U, 0U, 0U, 0U};
        mac = (uint8_t *)k_zero_mac;
    }

    hash_words[0] = river_xiaozhi_fnv1a32(mac, 6U, 0x13579BDFUL);
    hash_words[1] = river_xiaozhi_fnv1a32(mac, 6U, 0x2468ACE0UL);
    hash_words[2] = river_xiaozhi_fnv1a32(mac, 6U, 0x55AA11EEUL);
    hash_words[3] = river_xiaozhi_fnv1a32(mac, 6U, 0xA5A55A5AUL);

    for (index = 0U; index < 4U; ++index) {
        uuid[index * 4U + 0U] = (uint8_t)((hash_words[index] >> 24) & 0xFFU);
        uuid[index * 4U + 1U] = (uint8_t)((hash_words[index] >> 16) & 0xFFU);
        uuid[index * 4U + 2U] = (uint8_t)((hash_words[index] >> 8) & 0xFFU);
        uuid[index * 4U + 3U] = (uint8_t)(hash_words[index] & 0xFFU);
    }

    uuid[6] = (uint8_t)((uuid[6] & 0x0FU) | 0x40U);
    uuid[8] = (uint8_t)((uuid[8] & 0x3FU) | 0x80U);

    snprintf(buffer,
             buffer_size,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             uuid[0],
             uuid[1],
             uuid[2],
             uuid[3],
             uuid[4],
             uuid[5],
             uuid[6],
             uuid[7],
             uuid[8],
             uuid[9],
             uuid[10],
             uuid[11],
             uuid[12],
             uuid[13],
             uuid[14],
             uuid[15]);
}

static void river_xiaozhi_refresh_identifiers(void)
{
    river_xiaozhi_build_device_id(g_river_xiaozhi.device_id, sizeof(g_river_xiaozhi.device_id));
    river_xiaozhi_build_client_id(g_river_xiaozhi.client_id, sizeof(g_river_xiaozhi.client_id));
}

static uint32_t river_xiaozhi_bootstrap_cache_remaining_ms(void)
{
    uint64_t now_ms;

    if (!g_river_xiaozhi.bootstrap_config_owned ||
        g_river_xiaozhi.bootstrap_cache_expire_at_ms == 0U) {
        return 0U;
    }

    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    if (now_ms >= g_river_xiaozhi.bootstrap_cache_expire_at_ms) {
        return 0U;
    }

    return (uint32_t)(g_river_xiaozhi.bootstrap_cache_expire_at_ms - now_ms);
}

static bool river_xiaozhi_bootstrap_cache_valid(void)
{
    return g_river_xiaozhi.bootstrap_config_owned &&
           g_river_xiaozhi.url[0] != '\0' &&
           river_xiaozhi_bootstrap_cache_remaining_ms() > 0U;
}

static void river_xiaozhi_bootstrap_cache_mark_success(void)
{
    g_river_xiaozhi.bootstrap_config_owned = true;
    g_river_xiaozhi.bootstrap_cache_expire_at_ms =
        (uint64_t)rtos_time_get_current_system_time_ms() +
        (uint64_t)RIVER_XIAOZHI_BOOTSTRAP_CACHE_TTL_MS;
}

static void river_xiaozhi_bootstrap_cache_disable_auto_refresh(void)
{
    g_river_xiaozhi.bootstrap_config_owned = false;
    g_river_xiaozhi.bootstrap_cache_expire_at_ms = 0U;
}

static void river_xiaozhi_bootstrap_cache_forget_runtime_credentials(void)
{
    river_xiaozhi_bootstrap_cache_disable_auto_refresh();
    g_river_xiaozhi.url[0] = '\0';
    g_river_xiaozhi.token[0] = '\0';
    river_xiaozhi_clear_activation_state();
}

static uint32_t river_xiaozhi_bootstrap_retry_remaining_ms(void)
{
    uint64_t now_ms;

    if (g_river_xiaozhi.bootstrap_retry_after_ms == 0U) {
        return 0U;
    }

    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    if (now_ms >= g_river_xiaozhi.bootstrap_retry_after_ms) {
        return 0U;
    }

    return (uint32_t)(g_river_xiaozhi.bootstrap_retry_after_ms - now_ms);
}

static void river_xiaozhi_note_bootstrap_failure(const char *error_text,
                                                 int status_code,
                                                 const char *body)
{
    uint32_t backoff_ms;
    uint32_t shift;

    if (error_text != NULL && error_text[0] != '\0') {
        river_xiaozhi_set_last_error(error_text);
    }

    shift = g_river_xiaozhi.bootstrap_failures;
    if (shift > 3U) {
        shift = 3U;
    }
    backoff_ms = (uint32_t)(RIVER_XIAOZHI_OTA_RETRY_MIN_MS << shift);
    if (backoff_ms > RIVER_XIAOZHI_OTA_RETRY_MAX_MS) {
        backoff_ms = RIVER_XIAOZHI_OTA_RETRY_MAX_MS;
    }

    g_river_xiaozhi.bootstrap_failures++;
    g_river_xiaozhi.bootstrap_retry_after_ms =
        (uint64_t)rtos_time_get_current_system_time_ms() + (uint64_t)backoff_ms;

    RIVER_LOGW("xiaozhi ota bootstrap failed: status=%d retry_in_ms=%lu device_id=%s client_id=%s body=%s",
               status_code,
               (unsigned long)backoff_ms,
               g_river_xiaozhi.device_id[0] != '\0' ? g_river_xiaozhi.device_id : "-",
               g_river_xiaozhi.client_id[0] != '\0' ? g_river_xiaozhi.client_id : "-",
               body != NULL && body[0] != '\0' ? body : "-");
}

static void river_xiaozhi_clear_bootstrap_backoff(void)
{
    g_river_xiaozhi.bootstrap_failures = 0U;
    g_river_xiaozhi.bootstrap_retry_after_ms = 0U;
}

static river_status_t river_xiaozhi_parse_http_url(const char *url,
                                                   char *host_out,
                                                   size_t host_out_size,
                                                   uint16_t *port_out,
                                                   char *resource_out,
                                                   size_t resource_out_size,
                                                   uint8_t *secure_out)
{
    const char *scheme_start;
    const char *host_start;
    const char *host_end;
    const char *path_start;
    const char *port_sep;
    size_t host_len;
    uint16_t default_port;

    if (url == NULL || host_out == NULL || port_out == NULL || resource_out == NULL ||
        secure_out == NULL || host_out_size == 0U || resource_out_size == 0U) {
        return RIVER_ERR_ARG;
    }

    if (strncmp(url, "http://", 7U) == 0) {
        scheme_start = url + 7U;
        *secure_out = HTTPC_SECURE_NONE;
        default_port = 80U;
    } else if (strncmp(url, "https://", 8U) == 0) {
        scheme_start = url + 8U;
        *secure_out = HTTPC_SECURE_TLS;
        default_port = 443U;
    } else {
        return RIVER_ERR_ARG;
    }

    host_start = scheme_start;
    path_start = strchr(host_start, '/');
    host_end = path_start != NULL ? path_start : (url + strlen(url));
    port_sep = memchr(host_start, ':', (size_t)(host_end - host_start));
    if (port_sep != NULL) {
        host_end = port_sep;
        *port_out = (uint16_t)atoi(port_sep + 1);
    } else {
        *port_out = default_port;
    }

    host_len = (size_t)(host_end - host_start);
    if (host_len == 0U ||
        snprintf(host_out, host_out_size, "%.*s", (int)host_len, host_start) >= (int)host_out_size) {
        return RIVER_ERR_IO;
    }

    if (path_start != NULL) {
        if (snprintf(resource_out, resource_out_size, "%s", path_start) >= (int)resource_out_size) {
            return RIVER_ERR_IO;
        }
    } else {
        snprintf(resource_out, resource_out_size, "/");
    }

    return RIVER_OK;
}

static river_status_t river_xiaozhi_parse_url(const char *url,
                                              char *base_url,
                                              size_t base_url_size,
                                              int *port_out,
                                              char *path_out,
                                              size_t path_out_size)
{
    const char *scheme_start;
    const char *host_start;
    const char *host_end;
    const char *path_start;
    const char *port_sep;
    const char *scheme_text = NULL;
    int default_port = 0;
    size_t host_len;

    if (url == NULL || base_url == NULL || port_out == NULL || path_out == NULL ||
        base_url_size == 0U || path_out_size == 0U) {
        return RIVER_ERR_ARG;
    }

    if (strncmp(url, "ws://", 5U) == 0) {
        scheme_text = "ws";
        scheme_start = url + 5U;
        default_port = 80;
    } else if (strncmp(url, "wss://", 6U) == 0) {
        scheme_text = "wss";
        scheme_start = url + 6U;
        default_port = 443;
    } else {
        return RIVER_ERR_ARG;
    }

    host_start = scheme_start;
    path_start = strchr(host_start, '/');
    host_end = path_start != NULL ? path_start : url + strlen(url);
    port_sep = memchr(host_start, ':', (size_t)(host_end - host_start));
    if (port_sep != NULL) {
        host_end = port_sep;
        *port_out = atoi(port_sep + 1);
    } else {
        *port_out = default_port;
    }

    host_len = (size_t)(host_end - host_start);
    if (host_len == 0U || snprintf(base_url,
                                   base_url_size,
                                   "%s://%.*s",
                                   scheme_text,
                                   (int)host_len,
                                   host_start) >= (int)base_url_size) {
        return RIVER_ERR_IO;
    }

    if (path_start != NULL && path_start[1] != '\0') {
        if (snprintf(path_out, path_out_size, "%s", path_start + 1) >= (int)path_out_size) {
            return RIVER_ERR_IO;
        }
    } else {
        path_out[0] = '\0';
    }

    return RIVER_OK;
}

static river_status_t river_xiaozhi_build_headers(char *buffer,
                                                  size_t buffer_size,
                                                  char *device_id,
                                                  size_t device_id_size,
                                                  char *client_id,
                                                  size_t client_id_size)
{
    char auth_header[320];
    int written;

    if (buffer == NULL || buffer_size == 0U || device_id == NULL || client_id == NULL) {
        return RIVER_ERR_ARG;
    }

    river_xiaozhi_build_device_id(device_id, device_id_size);
    river_xiaozhi_build_client_id(client_id, client_id_size);

    auth_header[0] = '\0';
    if (g_river_xiaozhi.token[0] != '\0') {
        if (strchr(g_river_xiaozhi.token, ' ') == NULL) {
            snprintf(auth_header,
                     sizeof(auth_header),
                     "Authorization: Bearer %s\r\n",
                     g_river_xiaozhi.token);
        } else {
            snprintf(auth_header,
                     sizeof(auth_header),
                     "Authorization: %s\r\n",
                     g_river_xiaozhi.token);
        }
    }

    written = snprintf(buffer,
                       buffer_size,
                       "%s"
                       "Device-Id: %s\r\n"
                       "Client-Id: %s\r\n",
                       auth_header,
                       device_id,
                       client_id);
    if (written < 0 || (size_t)written >= buffer_size) {
        return RIVER_ERR_IO;
    }

    return RIVER_OK;
}

static river_status_t river_xiaozhi_http_post_json(const char *url,
                                                   const char *payload_json,
                                                   char *response_out,
                                                   size_t response_out_size,
                                                   int *status_code_out)
{
    struct httpc_conn *conn = NULL;
    char host[RIVER_XIAOZHI_HTTP_HOST_MAX];
    char resource[RIVER_XIAOZHI_HTTP_RESOURCE_MAX];
    char device_id[RIVER_XIAOZHI_DEVICE_ID_MAX];
    char client_id[RIVER_XIAOZHI_CLIENT_ID_MAX];
    uint16_t port = 0U;
    uint8_t secure = HTTPC_SECURE_NONE;
    size_t payload_len = 0U;
    size_t total_read = 0U;
    river_status_t status = RIVER_ERR_IO;

    if (url == NULL || payload_json == NULL || response_out == NULL || response_out_size == 0U) {
        return RIVER_ERR_ARG;
    }

    status = river_xiaozhi_parse_http_url(url,
                                          host,
                                          sizeof(host),
                                          &port,
                                          resource,
                                          sizeof(resource),
                                          &secure);
    if (status != RIVER_OK) {
        return status;
    }

    river_xiaozhi_build_device_id(device_id, sizeof(device_id));
    river_xiaozhi_build_client_id(client_id, sizeof(client_id));
    river_xiaozhi_copy_string(g_river_xiaozhi.device_id,
                              sizeof(g_river_xiaozhi.device_id),
                              device_id);
    river_xiaozhi_copy_string(g_river_xiaozhi.client_id,
                              sizeof(g_river_xiaozhi.client_id),
                              client_id);
    payload_len = strlen(payload_json);
    response_out[0] = '\0';
    if (status_code_out != NULL) {
        *status_code_out = 0;
    }

    conn = httpc_conn_new(secure, NULL, NULL, NULL);
    if (conn == NULL) {
        return RIVER_ERR_NO_MEMORY;
    }

    if (httpc_conn_connect(conn,
                           host,
                           port,
                           (uint32_t)RIVER_XIAOZHI_OTA_HTTP_TIMEOUT_SEC) != 0) {
        river_xiaozhi_set_last_error("xiaozhi_ota_connect_failed");
        goto exit;
    }

    if (httpc_request_write_header_start(conn,
                                         (char *)"POST",
                                         resource,
                                         (char *)"application/json",
                                         payload_len) != 0) {
        river_xiaozhi_set_last_error("xiaozhi_ota_header_start_failed");
        goto exit;
    }
    if (httpc_request_write_header(conn, (char *)"Connection", (char *)"close") != 0 ||
        httpc_request_write_header(conn, (char *)"Activation-Version", (char *)"1") != 0 ||
        httpc_request_write_header(conn, (char *)"Device-Id", device_id) != 0 ||
        httpc_request_write_header(conn, (char *)"Client-Id", client_id) != 0 ||
        httpc_request_write_header(conn, (char *)"User-Agent", (char *)RIVER_XIAOZHI_HTTP_USER_AGENT) != 0 ||
        httpc_request_write_header(conn, (char *)"Accept-Language", (char *)"zh-CN") != 0) {
        river_xiaozhi_set_last_error("xiaozhi_ota_header_write_failed");
        goto exit;
    }
    if (httpc_request_write_header_finish(conn) <= 0) {
        river_xiaozhi_set_last_error("xiaozhi_ota_header_finish_failed");
        goto exit;
    }
    if (payload_len > 0U &&
        httpc_request_write_data(conn, (uint8_t *)payload_json, payload_len) <= 0) {
        river_xiaozhi_set_last_error("xiaozhi_ota_body_write_failed");
        goto exit;
    }

    if (httpc_response_read_header(conn) != 0) {
        river_xiaozhi_set_last_error("xiaozhi_ota_response_header_failed");
        goto exit;
    }

    if (status_code_out != NULL && conn->response.status != NULL) {
        *status_code_out = atoi(conn->response.status);
    }

    while (total_read < (response_out_size - 1U)) {
        int read_size;

        read_size = httpc_response_read_data(conn,
                                             (uint8_t *)(response_out + total_read),
                                             response_out_size - total_read - 1U);
        if (read_size <= 0) {
            break;
        }
        total_read += (size_t)read_size;
        response_out[total_read] = '\0';

        if (conn->response.content_len != 0U &&
            total_read >= conn->response.content_len) {
            break;
        }
    }

    status = RIVER_OK;

exit:
    if (conn != NULL) {
        httpc_conn_close(conn);
        httpc_conn_free(conn);
    }
    return status;
}

static river_status_t river_xiaozhi_parse_bootstrap_response(const char *json_text)
{
    cJSON *root = NULL;
    const cJSON *websocket_obj;
    const cJSON *activation_obj;
    const cJSON *url_obj;
    const cJSON *token_obj;
    const cJSON *message_obj;
    const cJSON *code_obj;
    const cJSON *challenge_obj;
    const cJSON *timeout_obj;

    if (json_text == NULL || json_text[0] == '\0') {
        return RIVER_ERR_ARG;
    }

    root = cJSON_Parse(json_text);
    if (root == NULL) {
        river_xiaozhi_set_last_error("xiaozhi_ota_json_parse_failed");
        return RIVER_ERR_IO;
    }

    websocket_obj = cJSON_GetObjectItemCaseSensitive(root, "websocket");
    if (!cJSON_IsObject(websocket_obj)) {
        cJSON_Delete(root);
        river_xiaozhi_set_last_error("xiaozhi_ota_websocket_missing");
        return RIVER_ERR_UNSUPPORTED;
    }

    url_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)websocket_obj, "url");
    token_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)websocket_obj, "token");
    if (!cJSON_IsString(url_obj) || url_obj->valuestring == NULL || url_obj->valuestring[0] == '\0') {
        cJSON_Delete(root);
        river_xiaozhi_set_last_error("xiaozhi_ota_websocket_url_missing");
        return RIVER_ERR_UNSUPPORTED;
    }

    river_xiaozhi_copy_string(g_river_xiaozhi.url, sizeof(g_river_xiaozhi.url), url_obj->valuestring);
    river_xiaozhi_copy_string(g_river_xiaozhi.token,
                              sizeof(g_river_xiaozhi.token),
                              cJSON_IsString(token_obj) && token_obj->valuestring != NULL ?
                                  token_obj->valuestring :
                                  "");

    river_xiaozhi_clear_activation_state();
    activation_obj = cJSON_GetObjectItemCaseSensitive(root, "activation");
    if (cJSON_IsObject(activation_obj)) {
        message_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)activation_obj, "message");
        code_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)activation_obj, "code");
        challenge_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)activation_obj, "challenge");
        timeout_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)activation_obj, "timeout_ms");

        if (cJSON_IsString(message_obj) && message_obj->valuestring != NULL) {
            river_xiaozhi_copy_string(g_river_xiaozhi.activation_message,
                                      sizeof(g_river_xiaozhi.activation_message),
                                      message_obj->valuestring);
            river_xiaozhi_update_activation_from_text(message_obj->valuestring, "ota_message");
        }
        if (cJSON_IsString(code_obj) && code_obj->valuestring != NULL) {
            river_xiaozhi_copy_string(g_river_xiaozhi.activation_code,
                                      sizeof(g_river_xiaozhi.activation_code),
                                      code_obj->valuestring);
            g_river_xiaozhi.activation_code_present = (g_river_xiaozhi.activation_code[0] != '\0');
        }
        if (cJSON_IsString(challenge_obj) && challenge_obj->valuestring != NULL) {
            river_xiaozhi_copy_string(g_river_xiaozhi.activation_challenge,
                                      sizeof(g_river_xiaozhi.activation_challenge),
                                      challenge_obj->valuestring);
        }
        if (cJSON_IsNumber(timeout_obj)) {
            g_river_xiaozhi.activation_timeout_ms = (uint32_t)timeout_obj->valuedouble;
        }
    }

    g_river_xiaozhi.config_ready =
        (g_river_xiaozhi.url[0] != '\0') || (g_river_xiaozhi.ota_url[0] != '\0');
    cJSON_Delete(root);
    return RIVER_OK;
}

static river_status_t river_xiaozhi_send_json_root(cJSON *root)
{
    char *json = NULL;
    int json_len;
    river_status_t status = RIVER_ERR_IO;
    bool locked = false;

    if (root == NULL) {
        if (root != NULL) {
            cJSON_Delete(root);
        }
        return RIVER_ERR_BUSY;
    }

    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        river_xiaozhi_set_last_error("json_build_failed");
        return RIVER_ERR_NO_MEMORY;
    }

    json_len = (int)strlen(json);
    locked = river_xiaozhi_transport_lock();
    if (!locked || g_river_xiaozhi.wsclient == NULL ||
        g_river_xiaozhi.wsclient->readyState != WSC_OPEN) {
        status = RIVER_ERR_BUSY;
        goto exit;
    }

    if (river_xiaozhi_send_queue_backpressured_locked(0U, "json")) {
        status = RIVER_ERR_BUSY;
        goto exit;
    }

    if (ws_send(json, json_len, 1, g_river_xiaozhi.wsclient) != 0) {
        river_xiaozhi_set_last_error("json_send_failed");
        goto exit;
    }

    g_river_xiaozhi.text_messages_tx++;
    status = RIVER_OK;

exit:
    river_xiaozhi_transport_unlock(locked);
    cJSON_free(json);
    return status;
}

static void river_xiaozhi_add_session_id(cJSON *root)
{
    if (root == NULL) {
        return;
    }

    if (g_river_xiaozhi.session_id[0] != '\0') {
        cJSON_AddStringToObject(root, "session_id", g_river_xiaozhi.session_id);
    }
}

static void river_xiaozhi_emit_transport_ready(void)
{
    g_river_xiaozhi.server_sample_rate = 16000U;
    g_river_xiaozhi.server_frame_duration_ms =
        g_river_xiaozhi.config.uplink_frame_duration_ms != 0U ?
            g_river_xiaozhi.config.uplink_frame_duration_ms :
            RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS;
    g_river_xiaozhi.server_hello_received = true;
    g_river_xiaozhi.session_open = true;
    g_river_xiaozhi.ws_closed = false;
    g_river_xiaozhi.last_error[0] = '\0';
    g_river_xiaozhi.sessions_opened++;
    river_xiaozhi_set_last_type("hello");

    RIVER_LOGI("xiaozhi transport ready: sample_rate=%lu frame_duration=%lums subprotocol=%s",
               (unsigned long)g_river_xiaozhi.server_sample_rate,
               (unsigned long)g_river_xiaozhi.server_frame_duration_ms,
               RIVER_XIAOZHI_REALTIME_SUBPROTOCOL);
    river_xiaozhi_emit_event(RIVER_XIAOZHI_EVENT_SERVER_HELLO,
                             NULL,
                             NULL,
                             NULL,
                             g_river_xiaozhi.server_sample_rate,
                             g_river_xiaozhi.server_frame_duration_ms,
                             0U,
                             NULL,
                             0U,
                             0U);
}

static river_status_t river_xiaozhi_send_session_start_internal(const char *wake_reason)
{
    cJSON *root = NULL;
    cJSON *payload = NULL;
    cJSON *device = NULL;
    cJSON *audio = NULL;
    cJSON *session = NULL;
    cJSON *capabilities = NULL;
    river_status_t status;

    root = river_xiaozhi_create_control_event("session.start", &payload);
    device = cJSON_CreateObject();
    audio = cJSON_CreateObject();
    session = cJSON_CreateObject();
    capabilities = cJSON_CreateObject();
    if (root == NULL || payload == NULL || device == NULL || audio == NULL ||
        session == NULL || capabilities == NULL) {
        if (device != NULL) {
            cJSON_Delete(device);
        }
        if (audio != NULL) {
            cJSON_Delete(audio);
        }
        if (session != NULL) {
            cJSON_Delete(session);
        }
        if (capabilities != NULL) {
            cJSON_Delete(capabilities);
        }
        if (root != NULL) {
            cJSON_Delete(root);
        }
        return RIVER_ERR_NO_MEMORY;
    }

    cJSON_AddStringToObject(payload, "protocol_version", RIVER_XIAOZHI_REALTIME_PROTOCOL_VERSION);

    cJSON_AddStringToObject(device, "device_id", g_river_xiaozhi.device_id);
    cJSON_AddStringToObject(device, "client_type", RIVER_XIAOZHI_REALTIME_CLIENT_TYPE);
    cJSON_AddItemToObject(payload, "device", device);

    cJSON_AddStringToObject(audio, "codec", RIVER_XIAOZHI_UPLINK_FORMAT);
    cJSON_AddNumberToObject(audio,
                            "sample_rate_hz",
                            g_river_xiaozhi.config.uplink_sample_rate);
    cJSON_AddNumberToObject(audio, "channels", g_river_xiaozhi.config.uplink_channels);
    cJSON_AddItemToObject(payload, "audio", audio);

    cJSON_AddStringToObject(session, "mode", "voice");
    cJSON_AddStringToObject(session,
                            "wake_reason",
                            (wake_reason != NULL && wake_reason[0] != '\0') ?
                                wake_reason :
                                "keyword");
    cJSON_AddBoolToObject(session, "client_can_end", true);
    cJSON_AddBoolToObject(session, "server_can_end", true);
    cJSON_AddItemToObject(payload, "session", session);

    cJSON_AddBoolToObject(capabilities, "text_input", true);
    cJSON_AddBoolToObject(capabilities, "image_input", false);
    cJSON_AddBoolToObject(capabilities, "half_duplex", true);
    cJSON_AddBoolToObject(capabilities, "local_wake_word", true);
    cJSON_AddItemToObject(payload, "capabilities", capabilities);

    status = river_xiaozhi_send_json_root(root);
    if (status == RIVER_OK) {
        g_river_xiaozhi.dialog_started = true;
        g_river_xiaozhi.response_started = false;
        RIVER_LOGI("xiaozhi session.start sent: wake_reason=%s device_id=%s client_id=%s codec=%s",
                   (wake_reason != NULL && wake_reason[0] != '\0') ? wake_reason : "keyword",
                   g_river_xiaozhi.device_id,
                   g_river_xiaozhi.client_id,
                   RIVER_XIAOZHI_UPLINK_FORMAT);
    }
    return status;
}

static river_status_t river_xiaozhi_ensure_dialog_started(const char *wake_reason)
{
    if (!river_xiaozhi_session_open()) {
        river_xiaozhi_set_last_error("xiaozhi_transport_not_open");
        return RIVER_ERR_BUSY;
    }
    if (g_river_xiaozhi.dialog_started) {
        return RIVER_OK;
    }
    return river_xiaozhi_send_session_start_internal(wake_reason);
}

static river_status_t river_xiaozhi_send_audio_commit_internal(const char *reason)
{
    cJSON *root = NULL;
    cJSON *payload = NULL;

    if (!g_river_xiaozhi.dialog_started) {
        return RIVER_OK;
    }

    root = river_xiaozhi_create_control_event("audio.in.commit", &payload);
    if (root == NULL || payload == NULL) {
        if (root != NULL) {
            cJSON_Delete(root);
        }
        return RIVER_ERR_NO_MEMORY;
    }

    cJSON_AddStringToObject(payload,
                            "reason",
                            (reason != NULL && reason[0] != '\0') ? reason : "end_of_speech");
    return river_xiaozhi_send_json_root(root);
}

static river_status_t river_xiaozhi_send_text_input_internal(const char *text)
{
    cJSON *root = NULL;
    cJSON *payload = NULL;

    if (text == NULL || text[0] == '\0') {
        return RIVER_ERR_ARG;
    }

    root = river_xiaozhi_create_control_event("text.in", &payload);
    if (root == NULL || payload == NULL) {
        if (root != NULL) {
            cJSON_Delete(root);
        }
        return RIVER_ERR_NO_MEMORY;
    }

    cJSON_AddStringToObject(payload, "text", text);
    return river_xiaozhi_send_json_root(root);
}

static river_status_t river_xiaozhi_send_session_update_interrupt(void)
{
    cJSON *root = NULL;
    cJSON *payload = NULL;

    if (!g_river_xiaozhi.dialog_started) {
        return RIVER_OK;
    }

    root = river_xiaozhi_create_control_event("session.update", &payload);
    if (root == NULL || payload == NULL) {
        if (root != NULL) {
            cJSON_Delete(root);
        }
        return RIVER_ERR_NO_MEMORY;
    }

    cJSON_AddBoolToObject(payload, "interrupt", true);
    return river_xiaozhi_send_json_root(root);
}

static river_status_t river_xiaozhi_send_session_end_internal(const char *reason,
                                                              const char *message)
{
    cJSON *root = NULL;
    cJSON *payload = NULL;

    if (!g_river_xiaozhi.dialog_started || !river_xiaozhi_session_open()) {
        return RIVER_OK;
    }

    root = river_xiaozhi_create_control_event("session.end", &payload);
    if (root == NULL || payload == NULL) {
        if (root != NULL) {
            cJSON_Delete(root);
        }
        return RIVER_ERR_NO_MEMORY;
    }

    cJSON_AddStringToObject(payload,
                            "reason",
                            (reason != NULL && reason[0] != '\0') ? reason : "client_stop");
    if (message != NULL && message[0] != '\0') {
        cJSON_AddStringToObject(payload, "message", message);
    }
    return river_xiaozhi_send_json_root(root);
}

static void river_xiaozhi_emit_tts_event(const char *state, const char *text)
{
    river_xiaozhi_set_last_type("tts");
    river_xiaozhi_set_last_state(state);
    if (text != NULL && text[0] != '\0') {
        river_xiaozhi_set_last_text(text);
    }
    river_xiaozhi_emit_event(RIVER_XIAOZHI_EVENT_TTS,
                             text,
                             state,
                             NULL,
                             0U,
                             0U,
                             0U,
                             NULL,
                             0U,
                             0U);
}

static void river_xiaozhi_handle_realtime_session_update(const cJSON *payload)
{
    const cJSON *state_obj;
    const char *state = NULL;

    state_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "state");
    if (cJSON_IsString(state_obj) && state_obj->valuestring != NULL) {
        state = state_obj->valuestring;
    }

    river_xiaozhi_set_last_type("session.update");
    river_xiaozhi_set_last_state(state);
    RIVER_LOGI("xiaozhi session.update: sid=%s state=%s response_started=%s",
               g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
               state != NULL ? state : "-",
               river_xiaozhi_bool_text(g_river_xiaozhi.response_started));

    if (state != NULL && strcmp(state, "active") == 0 &&
        g_river_xiaozhi.response_started) {
        g_river_xiaozhi.response_started = false;
        river_xiaozhi_emit_tts_event("stop", NULL);
    }
}

static void river_xiaozhi_handle_realtime_response_start(const cJSON *payload)
{
    const cJSON *response_id_obj;
    const char *response_id = NULL;

    response_id_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "response_id");
    if (cJSON_IsString(response_id_obj) && response_id_obj->valuestring != NULL) {
        response_id = response_id_obj->valuestring;
    }

    g_river_xiaozhi.response_started = true;
    river_xiaozhi_set_last_type("response.start");
    RIVER_LOGI("xiaozhi response.start: sid=%s response_id=%s",
               g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
               response_id != NULL ? response_id : "-");
    river_xiaozhi_emit_tts_event("start", NULL);
}

static void river_xiaozhi_handle_realtime_response_chunk(const cJSON *payload)
{
    const cJSON *delta_type_obj;
    const cJSON *text_obj;
    const char *delta_type = NULL;
    const char *text = NULL;

    delta_type_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "delta_type");
    text_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "text");
    if (cJSON_IsString(delta_type_obj) && delta_type_obj->valuestring != NULL) {
        delta_type = delta_type_obj->valuestring;
    }
    if (cJSON_IsString(text_obj) && text_obj->valuestring != NULL) {
        text = text_obj->valuestring;
    }

    river_xiaozhi_set_last_type("response.chunk");
    if (delta_type != NULL && strcmp(delta_type, "text") != 0) {
        RIVER_LOGI("xiaozhi response.chunk ignore: sid=%s delta_type=%s",
                   g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
                   delta_type);
        return;
    }
    if (text == NULL || text[0] == '\0') {
        return;
    }

    if (!g_river_xiaozhi.response_started) {
        g_river_xiaozhi.response_started = true;
        river_xiaozhi_emit_tts_event("start", NULL);
    }

    RIVER_LOGI("xiaozhi response.chunk: sid=%s text=%s",
               g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
               text);
    river_xiaozhi_emit_tts_event("sentence_start", text);
}

static void river_xiaozhi_handle_realtime_session_end(const cJSON *payload)
{
    const cJSON *reason_obj;
    const cJSON *message_obj;
    char ended_sid[RIVER_XIAOZHI_SESSION_ID_MAX];
    const char *reason = NULL;
    const char *message = NULL;

    river_xiaozhi_copy_string(ended_sid, sizeof(ended_sid), g_river_xiaozhi.session_id);

    reason_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "reason");
    message_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "message");
    if (cJSON_IsString(reason_obj) && reason_obj->valuestring != NULL) {
        reason = reason_obj->valuestring;
    }
    if (cJSON_IsString(message_obj) && message_obj->valuestring != NULL) {
        message = message_obj->valuestring;
    }

    river_xiaozhi_set_last_type("session.end");
    river_xiaozhi_set_last_state(reason);
    if (message != NULL && message[0] != '\0') {
        river_xiaozhi_set_last_text(message);
    }
    if (g_river_xiaozhi.response_started) {
        g_river_xiaozhi.response_started = false;
        river_xiaozhi_emit_tts_event("stop", NULL);
    }
    g_river_xiaozhi.dialog_started = false;
    g_river_xiaozhi.session_id[0] = '\0';

    RIVER_LOGI("xiaozhi session.end: sid=%s reason=%s message=%s",
               ended_sid[0] != '\0' ? ended_sid : "-",
               reason != NULL ? reason : "-",
               message != NULL ? message : "-");
    river_xiaozhi_emit_event(RIVER_XIAOZHI_EVENT_SESSION_CLOSED,
                             NULL,
                             NULL,
                             NULL,
                             0U,
                             0U,
                             0U,
                             NULL,
                             0U,
                             0U);
}

static void river_xiaozhi_handle_realtime_error(const cJSON *payload)
{
    const cJSON *code_obj;
    const cJSON *message_obj;
    const char *code = NULL;
    const char *message = NULL;

    code_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "code");
    message_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "message");
    if (cJSON_IsString(code_obj) && code_obj->valuestring != NULL) {
        code = code_obj->valuestring;
    }
    if (cJSON_IsString(message_obj) && message_obj->valuestring != NULL) {
        message = message_obj->valuestring;
    }

    river_xiaozhi_set_last_type("error");
    river_xiaozhi_set_last_state(code);
    river_xiaozhi_set_last_error(message != NULL ? message : code);
    RIVER_LOGW("xiaozhi error: sid=%s code=%s message=%s",
               g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
               code != NULL ? code : "-",
               message != NULL ? message : "-");
    river_xiaozhi_emit_event(RIVER_XIAOZHI_EVENT_ERROR,
                             NULL,
                             NULL,
                             NULL,
                             0U,
                             0U,
                             0U,
                             NULL,
                             0U,
                             0U);
}

static void river_xiaozhi_handle_stt_message(const cJSON *root)
{
    const cJSON *text_obj;
    const char *text = NULL;

    text_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)root, "text");
    if (cJSON_IsString(text_obj) && text_obj->valuestring != NULL) {
        text = text_obj->valuestring;
    }

    river_xiaozhi_set_last_type("stt");
    river_xiaozhi_set_last_text(text);
    river_xiaozhi_update_activation_from_text(text, "stt");
    RIVER_LOGI("stt sid=%s text=%s",
               g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
               text != NULL ? text : "-");
    river_xiaozhi_emit_event(RIVER_XIAOZHI_EVENT_STT,
                             text,
                             NULL,
                             NULL,
                             0U,
                             0U,
                             0U,
                             NULL,
                             0U,
                             0U);
}

static void river_xiaozhi_handle_llm_message(const cJSON *root)
{
    const cJSON *text_obj;
    const cJSON *emotion_obj;
    const char *text = NULL;
    const char *emotion = NULL;

    text_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)root, "text");
    emotion_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)root, "emotion");
    if (cJSON_IsString(text_obj) && text_obj->valuestring != NULL) {
        text = text_obj->valuestring;
    }
    if (cJSON_IsString(emotion_obj) && emotion_obj->valuestring != NULL) {
        emotion = emotion_obj->valuestring;
    }

    river_xiaozhi_set_last_type("llm");
    river_xiaozhi_set_last_text(text);
    river_xiaozhi_set_last_emotion(emotion);
    river_xiaozhi_update_activation_from_text(text, "llm");
    RIVER_LOGI("llm sid=%s emotion=%s text=%s",
               g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
               emotion != NULL ? emotion : "-",
               text != NULL ? text : "-");
    river_xiaozhi_emit_event(RIVER_XIAOZHI_EVENT_LLM,
                             text,
                             NULL,
                             emotion,
                             0U,
                             0U,
                             0U,
                             NULL,
                             0U,
                             0U);
}

static void river_xiaozhi_handle_tts_message(const cJSON *root)
{
    const cJSON *state_obj;
    const cJSON *text_obj;
    const char *state = NULL;
    const char *text = NULL;

    state_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)root, "state");
    text_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)root, "text");
    if (cJSON_IsString(state_obj) && state_obj->valuestring != NULL) {
        state = state_obj->valuestring;
    }
    if (cJSON_IsString(text_obj) && text_obj->valuestring != NULL) {
        text = text_obj->valuestring;
    }

    river_xiaozhi_set_last_type("tts");
    river_xiaozhi_set_last_state(state);
    river_xiaozhi_set_last_text(text);
    river_xiaozhi_update_activation_from_text(text, "tts");
    RIVER_LOGI("tts sid=%s state=%s text=%s",
               g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
               state != NULL ? state : "-",
               text != NULL ? text : "-");
    river_xiaozhi_emit_event(RIVER_XIAOZHI_EVENT_TTS,
                             text,
                             state,
                             NULL,
                             0U,
                             0U,
                             0U,
                             NULL,
                             0U,
                             0U);
}

static void river_xiaozhi_handle_system_message(const cJSON *root)
{
    const cJSON *command_obj;
    const char *command = NULL;

    command_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)root, "command");
    if (cJSON_IsString(command_obj) && command_obj->valuestring != NULL) {
        command = command_obj->valuestring;
    }

    river_xiaozhi_set_last_type("system");
    river_xiaozhi_set_last_text(command);
    river_xiaozhi_update_activation_from_text(command, "system");
    RIVER_LOGI("system sid=%s command=%s",
               g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
               command != NULL ? command : "-");
}

static void river_xiaozhi_handle_alert_message(const cJSON *root)
{
    const cJSON *status_obj;
    const cJSON *message_obj;
    const cJSON *emotion_obj;
    const char *status = NULL;
    const char *message = NULL;
    const char *emotion = NULL;

    status_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)root, "status");
    message_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)root, "message");
    emotion_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)root, "emotion");
    if (cJSON_IsString(status_obj) && status_obj->valuestring != NULL) {
        status = status_obj->valuestring;
    }
    if (cJSON_IsString(message_obj) && message_obj->valuestring != NULL) {
        message = message_obj->valuestring;
    }
    if (cJSON_IsString(emotion_obj) && emotion_obj->valuestring != NULL) {
        emotion = emotion_obj->valuestring;
    }

    river_xiaozhi_set_last_type("alert");
    river_xiaozhi_set_last_state(status);
    river_xiaozhi_set_last_text(message);
    river_xiaozhi_set_last_emotion(emotion);
    river_xiaozhi_update_activation_from_text(message, "alert");
    RIVER_LOGI("alert sid=%s status=%s emotion=%s message=%s",
               g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
               status != NULL ? status : "-",
               emotion != NULL ? emotion : "-",
               message != NULL ? message : "-");
}

static void river_xiaozhi_handle_mcp_message(const cJSON *root)
{
    const cJSON *payload_obj;
    char response_json[RIVER_XIAOZHI_MCP_RESPONSE_MAX];
    river_status_t status;

    payload_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)root, "payload");
    g_river_xiaozhi.mcp_requests_rx++;
    river_xiaozhi_set_last_type("mcp");
    river_xiaozhi_emit_event(RIVER_XIAOZHI_EVENT_MCP,
                             NULL,
                             NULL,
                             NULL,
                             0U,
                             0U,
                             0U,
                             NULL,
                             0U,
                             0U);

    if (!g_river_xiaozhi.config.enable_mcp) {
        RIVER_LOGW("mcp payload received but mcp is disabled");
        return;
    }
    if (!cJSON_IsObject(payload_obj)) {
        RIVER_LOGW("mcp payload missing or invalid");
        g_river_xiaozhi.mcp_failures++;
        return;
    }

    status = river_xiaozhi_mcp_bridge_handle(payload_obj,
                                             response_json,
                                             sizeof(response_json));
    if (status == RIVER_ERR_UNSUPPORTED) {
        return;
    }
    if (status != RIVER_OK) {
        g_river_xiaozhi.mcp_failures++;
        river_xiaozhi_set_last_error("mcp_bridge_failed");
        return;
    }

    status = river_xiaozhi_send_mcp_payload(response_json);
    if (status == RIVER_OK) {
        g_river_xiaozhi.mcp_responses_tx++;
    } else {
        g_river_xiaozhi.mcp_failures++;
        river_xiaozhi_set_last_error("mcp_response_send_failed");
    }
}

static void river_xiaozhi_handle_text_message(const char *json_text, int json_len)
{
    cJSON *root;
    cJSON *type_obj;
    cJSON *payload_obj;
    const char *type_text;

    root = cJSON_ParseWithLength(json_text, (size_t)json_len);
    if (root == NULL) {
        river_xiaozhi_set_last_error("json_parse_failed");
        river_xiaozhi_emit_event(RIVER_XIAOZHI_EVENT_ERROR,
                                 NULL,
                                 NULL,
                                 NULL,
                                 0U,
                                 0U,
                                 0U,
                                 NULL,
                                 0U,
                                 0U);
        return;
    }

    type_obj = cJSON_GetObjectItemCaseSensitive(root, "type");
    payload_obj = cJSON_GetObjectItemCaseSensitive(root, "payload");
    type_text = cJSON_IsString(type_obj) ? type_obj->valuestring : NULL;
    g_river_xiaozhi.text_messages_rx++;

    if (type_text == NULL) {
        river_xiaozhi_set_last_error("json_missing_type");
        cJSON_Delete(root);
        river_xiaozhi_emit_event(RIVER_XIAOZHI_EVENT_ERROR,
                                 NULL,
                                 NULL,
                                 NULL,
                                 0U,
                                 0U,
                                 0U,
                                 NULL,
                                 0U,
                                 0U);
        return;
    }

    river_xiaozhi_update_session_id_from_root(root);

    if (strcmp(type_text, "session.update") == 0 && cJSON_IsObject(payload_obj)) {
        river_xiaozhi_handle_realtime_session_update(payload_obj);
    } else if (strcmp(type_text, "response.start") == 0 && cJSON_IsObject(payload_obj)) {
        river_xiaozhi_handle_realtime_response_start(payload_obj);
    } else if (strcmp(type_text, "response.chunk") == 0 && cJSON_IsObject(payload_obj)) {
        river_xiaozhi_handle_realtime_response_chunk(payload_obj);
    } else if (strcmp(type_text, "session.end") == 0 && cJSON_IsObject(payload_obj)) {
        river_xiaozhi_handle_realtime_session_end(payload_obj);
    } else if (strcmp(type_text, "error") == 0 && cJSON_IsObject(payload_obj)) {
        river_xiaozhi_handle_realtime_error(payload_obj);
    } else if (strcmp(type_text, "hello") == 0) {
        river_xiaozhi_emit_transport_ready();
    } else if (strcmp(type_text, "stt") == 0) {
        river_xiaozhi_handle_stt_message(root);
    } else if (strcmp(type_text, "llm") == 0) {
        river_xiaozhi_handle_llm_message(root);
    } else if (strcmp(type_text, "tts") == 0) {
        river_xiaozhi_handle_tts_message(root);
    } else if (strcmp(type_text, "mcp") == 0) {
        river_xiaozhi_handle_mcp_message(root);
    } else if (strcmp(type_text, "system") == 0) {
        river_xiaozhi_handle_system_message(root);
    } else if (strcmp(type_text, "alert") == 0) {
        river_xiaozhi_handle_alert_message(root);
    } else {
        river_xiaozhi_set_last_type(type_text);
        RIVER_LOGI("ignore xiaozhi message type=%s", type_text);
    }

    cJSON_Delete(root);
}

static void river_xiaozhi_handle_binary_message(const uint8_t *data, size_t data_len)
{
    if (data == NULL || data_len == 0U) {
        return;
    }

    g_river_xiaozhi.audio_messages_rx++;
    river_xiaozhi_set_last_type("audio");
    river_xiaozhi_emit_event(RIVER_XIAOZHI_EVENT_AUDIO,
                             NULL,
                             NULL,
                             NULL,
                             g_river_xiaozhi.server_sample_rate,
                             g_river_xiaozhi.server_frame_duration_ms,
                             0U,
                             data,
                             data_len,
                             RIVER_XIAOZHI_BINARY_PCM16);
}

static bool river_xiaozhi_payload_looks_like_json(const uint8_t *data, size_t data_len)
{
    size_t index = 0U;

    if (data == NULL || data_len == 0U) {
        return false;
    }

    while (index < data_len) {
        uint8_t ch = data[index];

        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            index++;
            continue;
        }

        return ch == '{' || ch == '[';
    }

    return false;
}

static bool river_xiaozhi_payload_is_valid_json(const uint8_t *data, size_t data_len)
{
    cJSON *root;

    if (!river_xiaozhi_payload_looks_like_json(data, data_len)) {
        return false;
    }

    root = cJSON_ParseWithLength((const char *)data, data_len);
    if (root == NULL) {
        return false;
    }

    cJSON_Delete(root);
    return true;
}

static void river_xiaozhi_ws_message_cb(wsclient_context **wsclient,
                                        int data_len,
                                        enum opcode_type opcode,
                                        void *user_data)
{
    const uint8_t *data;
    bool json_guess;

    (void)wsclient;
    (void)user_data;

    if (g_river_xiaozhi.wsclient == NULL ||
        g_river_xiaozhi.wsclient->receivedData == NULL ||
        data_len <= 0) {
        return;
    }

    data = (const uint8_t *)g_river_xiaozhi.wsclient->receivedData;
    json_guess = river_xiaozhi_payload_looks_like_json(data, (size_t)data_len);

    if (opcode == BINARY_FRAME) {
        river_xiaozhi_handle_binary_message(data, (size_t)data_len);
        return;
    }

    /*
     * Ameba wsclient reports the opcode of the final fragment when a server
     * message is reassembled. For fragmented XiaoZhi audio, that means the
     * callback may receive the full binary payload with final opcode
     * CONTINUATION instead of BINARY_FRAME. Distinguish reassembled JSON from
     * reassembled binary by looking at the payload prefix.
     */
    if (opcode == CONTINUATION) {
        if (json_guess && river_xiaozhi_payload_is_valid_json(data, (size_t)data_len)) {
            river_xiaozhi_handle_text_message((const char *)data, data_len);
        } else {
            river_xiaozhi_handle_binary_message(data, (size_t)data_len);
        }
        return;
    }

    river_xiaozhi_handle_text_message((const char *)data, data_len);
}

static void river_xiaozhi_ws_close_cb(wsclient_context *wsclient, void *user_data)
{
    (void)wsclient;
    (void)user_data;

    g_river_xiaozhi.ws_closed = true;
    g_river_xiaozhi.session_open = false;
    g_river_xiaozhi.server_hello_received = false;
    g_river_xiaozhi.dialog_started = false;
    g_river_xiaozhi.response_started = false;
    g_river_xiaozhi.sessions_closed++;
    river_xiaozhi_set_last_type("closed");
    RIVER_LOGI("xiaozhi websocket closed sid=%s",
               g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-");
    river_xiaozhi_emit_event(RIVER_XIAOZHI_EVENT_SESSION_CLOSED,
                             NULL,
                             NULL,
                             NULL,
                             0U,
                             0U,
                             0U,
                             NULL,
                             0U,
                             0U);
}

static void river_xiaozhi_close_context(bool reset_session_id)
{
    bool locked = false;

    locked = river_xiaozhi_transport_lock();
    if (locked && g_river_xiaozhi.wsclient != NULL) {
        river_ws_dispatch_unregister(g_river_xiaozhi.wsclient);
        if (g_river_xiaozhi.wsclient->readyState == WSC_OPEN) {
            ws_close(&g_river_xiaozhi.wsclient);
        }
    }
    if (locked && g_river_xiaozhi.wsclient != NULL) {
        ws_free(g_river_xiaozhi.wsclient);
        g_river_xiaozhi.wsclient = NULL;
    }
    river_xiaozhi_transport_unlock(locked);

    river_xiaozhi_reset_runtime_state();
    if (reset_session_id) {
        g_river_xiaozhi.session_id[0] = '\0';
    }
}

river_status_t river_xiaozhi_init(void)
{
    if (g_river_xiaozhi.initialized) {
        return RIVER_OK;
    }

    memset(&g_river_xiaozhi, 0, sizeof(g_river_xiaozhi));
    if (rtos_mutex_create(&g_river_xiaozhi.lock) != RTK_SUCCESS) {
        return RIVER_ERR_NO_MEMORY;
    }

    g_river_xiaozhi.server_sample_rate = 16000U;
    g_river_xiaozhi.server_frame_duration_ms = RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS;
    g_river_xiaozhi.config.protocol_version = (uint16_t)RIVER_XIAOZHI_PROTOCOL_VERSION;
    g_river_xiaozhi.config.enable_mcp = (RIVER_XIAOZHI_ENABLE_MCP != 0);
    g_river_xiaozhi.config.uplink_sample_rate = RIVER_XIAOZHI_UPLINK_SAMPLE_RATE;
    g_river_xiaozhi.config.uplink_channels = RIVER_XIAOZHI_UPLINK_CHANNELS;
    g_river_xiaozhi.config.uplink_frame_duration_ms = RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS;
    river_xiaozhi_copy_string(g_river_xiaozhi.ota_url,
                              sizeof(g_river_xiaozhi.ota_url),
                              RIVER_XIAOZHI_OTA_URL);
    river_xiaozhi_copy_string(g_river_xiaozhi.url, sizeof(g_river_xiaozhi.url), RIVER_XIAOZHI_URL);
    river_xiaozhi_copy_string(g_river_xiaozhi.token, sizeof(g_river_xiaozhi.token), RIVER_XIAOZHI_TOKEN);
    g_river_xiaozhi.config.ota_url = g_river_xiaozhi.ota_url;
    g_river_xiaozhi.config.url = g_river_xiaozhi.url;
    g_river_xiaozhi.config.token = g_river_xiaozhi.token;
    g_river_xiaozhi.config_ready =
        (g_river_xiaozhi.url[0] != '\0') || (g_river_xiaozhi.ota_url[0] != '\0');
    river_xiaozhi_refresh_identifiers();
    g_river_xiaozhi.initialized = true;

    RIVER_LOGI("xiaozhi init: ota_set=%s url_set=%s token_set=%s wire=%s protocol=%u mcp=%s device_id=%s client_id=%s",
               river_xiaozhi_bool_text(g_river_xiaozhi.ota_url[0] != '\0'),
               river_xiaozhi_bool_text(g_river_xiaozhi.url[0] != '\0'),
               river_xiaozhi_bool_text(g_river_xiaozhi.token[0] != '\0'),
               RIVER_XIAOZHI_REALTIME_PROTOCOL_VERSION,
               (unsigned int)g_river_xiaozhi.config.protocol_version,
               river_xiaozhi_bool_text(g_river_xiaozhi.config.enable_mcp),
               g_river_xiaozhi.device_id,
               g_river_xiaozhi.client_id);
    return RIVER_OK;
}

river_status_t river_xiaozhi_get_config(river_xiaozhi_config_t *config)
{
    if (config == NULL) {
        return RIVER_ERR_ARG;
    }
    if (!g_river_xiaozhi.initialized) {
        river_status_t init_status = river_xiaozhi_init();
        if (init_status != RIVER_OK) {
            return init_status;
        }
    }

    memset(config, 0, sizeof(*config));
    *config = g_river_xiaozhi.config;
    config->ota_url = g_river_xiaozhi.ota_url;
    config->url = g_river_xiaozhi.url;
    config->token = g_river_xiaozhi.token;
    return RIVER_OK;
}

river_status_t river_xiaozhi_set_config(const river_xiaozhi_config_t *config)
{
    bool ota_changed = false;
    bool manual_url_override = false;
    bool manual_token_override = false;

    if (!g_river_xiaozhi.initialized) {
        river_status_t init_status = river_xiaozhi_init();
        if (init_status != RIVER_OK) {
            return init_status;
        }
    }
    if (config == NULL) {
        return RIVER_ERR_ARG;
    }

    if (config->ota_url != NULL) {
        ota_changed = strcmp(g_river_xiaozhi.ota_url, config->ota_url) != 0;
        river_xiaozhi_copy_string(g_river_xiaozhi.ota_url,
                                  sizeof(g_river_xiaozhi.ota_url),
                                  config->ota_url);
    }
    if (config->url != NULL) {
        manual_url_override = true;
        river_xiaozhi_copy_string(g_river_xiaozhi.url, sizeof(g_river_xiaozhi.url), config->url);
    }
    if (config->token != NULL) {
        manual_token_override = true;
        river_xiaozhi_copy_string(g_river_xiaozhi.token, sizeof(g_river_xiaozhi.token), config->token);
    }
    if (config->protocol_version != 0U) {
        g_river_xiaozhi.config.protocol_version = config->protocol_version;
    }
    g_river_xiaozhi.config.enable_mcp = config->enable_mcp;
    if (config->uplink_sample_rate != 0U) {
        g_river_xiaozhi.config.uplink_sample_rate = config->uplink_sample_rate;
    }
    if (config->uplink_channels != 0U) {
        g_river_xiaozhi.config.uplink_channels = config->uplink_channels;
    }
    if (config->uplink_frame_duration_ms != 0U) {
        g_river_xiaozhi.config.uplink_frame_duration_ms = config->uplink_frame_duration_ms;
    }

    g_river_xiaozhi.config.ota_url = g_river_xiaozhi.ota_url;
    g_river_xiaozhi.config.url = g_river_xiaozhi.url;
    g_river_xiaozhi.config.token = g_river_xiaozhi.token;
    if (manual_url_override || manual_token_override) {
        river_xiaozhi_bootstrap_cache_disable_auto_refresh();
        river_xiaozhi_clear_activation_state();
    } else if (ota_changed && g_river_xiaozhi.bootstrap_config_owned) {
        river_xiaozhi_bootstrap_cache_forget_runtime_credentials();
        RIVER_LOGI("xiaozhi bootstrap cache invalidated after ota url update");
    }
    g_river_xiaozhi.config_ready =
        (g_river_xiaozhi.url[0] != '\0') || (g_river_xiaozhi.ota_url[0] != '\0');
    river_xiaozhi_refresh_identifiers();

    RIVER_LOGI("xiaozhi config updated: ota_set=%s url_set=%s token_set=%s wire=%s protocol=%u uplink=%luHz/%luch/%lums mcp=%s",
               river_xiaozhi_bool_text(g_river_xiaozhi.ota_url[0] != '\0'),
               river_xiaozhi_bool_text(g_river_xiaozhi.url[0] != '\0'),
               river_xiaozhi_bool_text(g_river_xiaozhi.token[0] != '\0'),
               RIVER_XIAOZHI_REALTIME_PROTOCOL_VERSION,
               (unsigned int)g_river_xiaozhi.config.protocol_version,
               (unsigned long)g_river_xiaozhi.config.uplink_sample_rate,
               (unsigned long)g_river_xiaozhi.config.uplink_channels,
               (unsigned long)g_river_xiaozhi.config.uplink_frame_duration_ms,
               river_xiaozhi_bool_text(g_river_xiaozhi.config.enable_mcp));
    return RIVER_OK;
}

river_status_t river_xiaozhi_set_event_handler(river_xiaozhi_event_handler_t handler,
                                               void *user_data)
{
    if (!g_river_xiaozhi.initialized) {
        river_status_t init_status = river_xiaozhi_init();
        if (init_status != RIVER_OK) {
            return init_status;
        }
    }

    g_river_xiaozhi.event_handler = handler;
    g_river_xiaozhi.event_handler_user = user_data;
    return RIVER_OK;
}

bool river_xiaozhi_configured(void)
{
    return g_river_xiaozhi.initialized && g_river_xiaozhi.config_ready;
}

const char *river_xiaozhi_session_id(void)
{
    if (!g_river_xiaozhi.initialized || g_river_xiaozhi.session_id[0] == '\0') {
        return NULL;
    }
    return g_river_xiaozhi.session_id;
}

const char *river_xiaozhi_last_text(void)
{
    return (g_river_xiaozhi.initialized && g_river_xiaozhi.last_text[0] != '\0') ?
               g_river_xiaozhi.last_text :
               NULL;
}

const char *river_xiaozhi_last_state(void)
{
    return (g_river_xiaozhi.initialized && g_river_xiaozhi.last_state[0] != '\0') ?
               g_river_xiaozhi.last_state :
               NULL;
}

const char *river_xiaozhi_last_emotion(void)
{
    return (g_river_xiaozhi.initialized && g_river_xiaozhi.last_emotion[0] != '\0') ?
               g_river_xiaozhi.last_emotion :
               NULL;
}

const char *river_xiaozhi_last_error(void)
{
    return (g_river_xiaozhi.initialized && g_river_xiaozhi.last_error[0] != '\0') ?
               g_river_xiaozhi.last_error :
               NULL;
}

const char *river_xiaozhi_activation_code(void)
{
    return (g_river_xiaozhi.initialized && g_river_xiaozhi.activation_code[0] != '\0') ?
               g_river_xiaozhi.activation_code :
               NULL;
}

const char *river_xiaozhi_activation_message(void)
{
    return (g_river_xiaozhi.initialized && g_river_xiaozhi.activation_message[0] != '\0') ?
               g_river_xiaozhi.activation_message :
               NULL;
}

const char *river_xiaozhi_activation_challenge(void)
{
    return (g_river_xiaozhi.initialized && g_river_xiaozhi.activation_challenge[0] != '\0') ?
               g_river_xiaozhi.activation_challenge :
               NULL;
}

river_status_t river_xiaozhi_bootstrap(void)
{
    int status_code = 0;
    river_status_t status;
    static const char k_payload[] =
        "{\"board\":{\"type\":\"RTL8730E\"},\"application\":{\"name\":\"ameba-river\",\"version\":\"1.1\"}}";

    if (!g_river_xiaozhi.initialized) {
        status = river_xiaozhi_init();
        if (status != RIVER_OK) {
            return status;
        }
    }
    if (g_river_xiaozhi.ota_url[0] == '\0') {
        river_xiaozhi_set_last_error("xiaozhi_ota_url_not_configured");
        return RIVER_ERR_UNSUPPORTED;
    }
    if (!river_wifi_station_is_connected()) {
        river_xiaozhi_set_last_error("wifi_not_connected");
        return RIVER_ERR_BUSY;
    }
    river_xiaozhi_refresh_identifiers();
    if (river_xiaozhi_bootstrap_retry_remaining_ms() > 0U) {
        river_xiaozhi_set_last_error("xiaozhi_ota_backoff_active");
        return RIVER_ERR_BUSY;
    }

    status = river_xiaozhi_http_post_json(g_river_xiaozhi.ota_url,
                                          k_payload,
                                          g_river_xiaozhi.bootstrap_response,
                                          sizeof(g_river_xiaozhi.bootstrap_response),
                                          &status_code);
    if (status != RIVER_OK) {
        river_xiaozhi_note_bootstrap_failure(river_xiaozhi_last_error(), status_code, "transport_error");
        return status;
    }
    if (status_code != 200) {
        river_xiaozhi_note_bootstrap_failure("xiaozhi_ota_http_status_invalid",
                                             status_code,
                                             g_river_xiaozhi.bootstrap_response);
        return RIVER_ERR_IO;
    }

    status = river_xiaozhi_parse_bootstrap_response(g_river_xiaozhi.bootstrap_response);
    if (status != RIVER_OK) {
        river_xiaozhi_note_bootstrap_failure(river_xiaozhi_last_error(),
                                             status_code,
                                             g_river_xiaozhi.bootstrap_response);
        return status;
    }

    river_xiaozhi_clear_bootstrap_backoff();
    river_xiaozhi_bootstrap_cache_mark_success();
    RIVER_LOGI("xiaozhi ota bootstrap ok: ota=%s ws=%s token_set=%s cache_ttl_ms=%lu device_id=%s client_id=%s activation_code=%s challenge_set=%s",
               g_river_xiaozhi.ota_url,
               g_river_xiaozhi.url,
               river_xiaozhi_bool_text(g_river_xiaozhi.token[0] != '\0'),
               (unsigned long)RIVER_XIAOZHI_BOOTSTRAP_CACHE_TTL_MS,
               g_river_xiaozhi.device_id,
               g_river_xiaozhi.client_id,
               g_river_xiaozhi.activation_code[0] != '\0' ?
                   g_river_xiaozhi.activation_code :
                   "-",
               river_xiaozhi_bool_text(g_river_xiaozhi.activation_challenge[0] != '\0'));
    if (g_river_xiaozhi.activation_message[0] != '\0') {
        RIVER_LOGI("xiaozhi activation message: %s", g_river_xiaozhi.activation_message);
    }
    if (g_river_xiaozhi.activation_challenge[0] != '\0') {
        RIVER_LOGW("xiaozhi activation challenge received; device-side challenge activation is not implemented on ameba-river");
    }
    return RIVER_OK;
}

river_status_t river_xiaozhi_open_session(void)
{
    int port;
    river_status_t status;
    uint32_t bootstrap_cache_remaining_ms = 0U;
    bool bootstrap_refresh_needed = false;
    char handshake_protocol[] = RIVER_XIAOZHI_REALTIME_SUBPROTOCOL;

    if (!g_river_xiaozhi.initialized) {
        status = river_xiaozhi_init();
        if (status != RIVER_OK) {
            return status;
        }
    }
    if (!river_wifi_station_is_connected()) {
        river_xiaozhi_set_last_error("wifi_not_connected");
        return RIVER_ERR_BUSY;
    }
    if (g_river_xiaozhi.session_open &&
        g_river_xiaozhi.wsclient != NULL &&
        g_river_xiaozhi.wsclient->readyState == WSC_OPEN) {
        return RIVER_OK;
    }
    river_xiaozhi_reclaim_heap_before_connect();
    if (g_river_xiaozhi.ota_url[0] != '\0') {
        bootstrap_cache_remaining_ms = river_xiaozhi_bootstrap_cache_remaining_ms();
        bootstrap_refresh_needed =
            (g_river_xiaozhi.url[0] == '\0') ||
            (g_river_xiaozhi.bootstrap_config_owned && !river_xiaozhi_bootstrap_cache_valid());
        if (!bootstrap_refresh_needed && river_xiaozhi_bootstrap_cache_valid()) {
            RIVER_LOGI("xiaozhi bootstrap cache hit: refresh_in_ms=%lu device_id=%s client_id=%s",
                       (unsigned long)bootstrap_cache_remaining_ms,
                       g_river_xiaozhi.device_id[0] != '\0' ? g_river_xiaozhi.device_id : "-",
                       g_river_xiaozhi.client_id[0] != '\0' ? g_river_xiaozhi.client_id : "-");
        } else if (bootstrap_refresh_needed) {
            status = river_xiaozhi_bootstrap();
            if (status != RIVER_OK && g_river_xiaozhi.url[0] == '\0') {
                return status;
            }
            if (status != RIVER_OK) {
                RIVER_LOGW("xiaozhi bootstrap refresh failed; using stale ws config: status=%d last_err=%s",
                           (int)status,
                           river_xiaozhi_last_error() != NULL ? river_xiaozhi_last_error() : "-");
            }
        }
    }
    if (g_river_xiaozhi.url[0] == '\0') {
        river_xiaozhi_set_last_error("xiaozhi_url_not_configured");
        return RIVER_ERR_UNSUPPORTED;
    }

    river_xiaozhi_close_context(false);
    g_river_xiaozhi.session_id[0] = '\0';
    g_river_xiaozhi.last_error[0] = '\0';
    g_river_xiaozhi.send_backpressure_events = 0U;
    g_river_xiaozhi.send_backpressure_reserve_events = 0U;
    g_river_xiaozhi.send_backpressure_full_events = 0U;
    g_river_xiaozhi.send_queue_high_watermark = 0U;
    g_river_xiaozhi.last_backpressure_log_ms = 0U;
    memset(g_river_xiaozhi.open_base_url, 0, sizeof(g_river_xiaozhi.open_base_url));
    memset(g_river_xiaozhi.open_path, 0, sizeof(g_river_xiaozhi.open_path));
    memset(g_river_xiaozhi.open_header_fields, 0, sizeof(g_river_xiaozhi.open_header_fields));
    memset(g_river_xiaozhi.open_device_id, 0, sizeof(g_river_xiaozhi.open_device_id));
    memset(g_river_xiaozhi.open_client_id, 0, sizeof(g_river_xiaozhi.open_client_id));

    status = river_xiaozhi_parse_url(g_river_xiaozhi.url,
                                     g_river_xiaozhi.open_base_url,
                                     sizeof(g_river_xiaozhi.open_base_url),
                                     &port,
                                     g_river_xiaozhi.open_path,
                                     sizeof(g_river_xiaozhi.open_path));
    if (status != RIVER_OK) {
        river_xiaozhi_set_last_error("xiaozhi_url_parse_failed");
        return status;
    }

    river_xiaozhi_reset_runtime_state();
    status = river_xiaozhi_build_headers(g_river_xiaozhi.open_header_fields,
                                         sizeof(g_river_xiaozhi.open_header_fields),
                                         g_river_xiaozhi.open_device_id,
                                         sizeof(g_river_xiaozhi.open_device_id),
                                         g_river_xiaozhi.open_client_id,
                                         sizeof(g_river_xiaozhi.open_client_id));
    if (status != RIVER_OK) {
        river_xiaozhi_set_last_error("xiaozhi_headers_build_failed");
        return status;
    }
    river_xiaozhi_copy_string(g_river_xiaozhi.device_id,
                              sizeof(g_river_xiaozhi.device_id),
                              g_river_xiaozhi.open_device_id);
    river_xiaozhi_copy_string(g_river_xiaozhi.client_id,
                              sizeof(g_river_xiaozhi.client_id),
                              g_river_xiaozhi.open_client_id);

    status = river_ws_dispatch_init();
    if (status != RIVER_OK) {
        river_xiaozhi_set_last_error("ws_dispatch_init_failed");
        return status;
    }

    g_river_xiaozhi.wsclient =
        create_wsclient(g_river_xiaozhi.open_base_url,
                        port,
                        g_river_xiaozhi.open_path,
                        NULL,
                        RIVER_XIAOZHI_WS_TX_MAX,
                        RIVER_XIAOZHI_WS_RX_MAX,
                        RIVER_XIAOZHI_WS_QUEUE_MAX);
    if (g_river_xiaozhi.wsclient == NULL) {
        river_xiaozhi_set_last_error("xiaozhi_wsclient_create_failed");
        return RIVER_ERR_NO_MEMORY;
    }

    if (ws_handshake_header_set_protocol(g_river_xiaozhi.wsclient,
                                         handshake_protocol,
                                         (int)(strlen(handshake_protocol) + 1U)) != 0) {
        river_xiaozhi_set_last_error("xiaozhi_protocol_set_failed");
        river_xiaozhi_close_context(false);
        return RIVER_ERR_IO;
    }

    if (ws_handshake_set_header_fields(g_river_xiaozhi.wsclient,
                                       g_river_xiaozhi.open_header_fields,
                                       (int)(strlen(g_river_xiaozhi.open_header_fields) + 1U)) != 0) {
        river_xiaozhi_set_last_error("xiaozhi_header_fields_set_failed");
        river_xiaozhi_close_context(false);
        return RIVER_ERR_IO;
    }

    if (river_ws_dispatch_register(g_river_xiaozhi.wsclient,
                                   river_xiaozhi_ws_message_cb,
                                   river_xiaozhi_ws_close_cb,
                                   NULL) != RIVER_OK) {
        river_xiaozhi_set_last_error("xiaozhi_ws_dispatch_register_failed");
        river_xiaozhi_close_context(false);
        return RIVER_ERR_BUSY;
    }

    /*
     * Keep wsclient enqueue non-blocking here. Once the send queue is near
     * full, project-side backpressure returns BUSY early so the pump task can
     * keep polling and draining instead of being pinned behind this mutex.
     */
    ws_setsockopt_timeout(RIVER_XIAOZHI_WS_RECV_TIMEOUT_MS,
                          RIVER_XIAOZHI_WS_SEND_TIMEOUT_MS,
                          RIVER_XIAOZHI_WS_CONNECT_TIMEOUT_MS);
    ws_set_senddata_block_time(RIVER_XIAOZHI_WS_SEND_BLOCK_MS);
    ws_multisend_opts(g_river_xiaozhi.wsclient, RIVER_XIAOZHI_WS_STABLE_BUF_NUM);
    RIVER_LOGI("xiaozhi connecting: url=%s wire=%s protocol=%u device_id=%s client_id=%s txq=%u stable=%u reserve=%u",
               g_river_xiaozhi.url,
               RIVER_XIAOZHI_REALTIME_PROTOCOL_VERSION,
               (unsigned int)g_river_xiaozhi.config.protocol_version,
               g_river_xiaozhi.open_device_id,
               g_river_xiaozhi.open_client_id,
               (unsigned int)RIVER_XIAOZHI_WS_QUEUE_MAX,
               (unsigned int)RIVER_XIAOZHI_WS_STABLE_BUF_NUM,
               (unsigned int)RIVER_XIAOZHI_WS_AUDIO_QUEUE_RESERVE);
    if (ws_connect_url(g_river_xiaozhi.wsclient) < 0) {
        river_xiaozhi_set_last_error("xiaozhi_ws_connect_failed");
        river_xiaozhi_close_context(false);
        return RIVER_ERR_IO;
    }

    river_xiaozhi_emit_transport_ready();
    return RIVER_OK;
}

void river_xiaozhi_close_session(void)
{
    if (!g_river_xiaozhi.initialized) {
        return;
    }

    (void)river_xiaozhi_send_session_end_internal("client_stop", NULL);
    river_xiaozhi_close_context(false);
}

bool river_xiaozhi_session_open(void)
{
    return g_river_xiaozhi.session_open &&
           g_river_xiaozhi.wsclient != NULL &&
           g_river_xiaozhi.wsclient->readyState == WSC_OPEN;
}

river_status_t river_xiaozhi_poll(uint32_t timeout_ms)
{
    uint32_t remaining_ms;
    bool locked;
    bool keep_polling = true;

    if (!g_river_xiaozhi.initialized || g_river_xiaozhi.wsclient == NULL) {
        return RIVER_OK;
    }

    remaining_ms = timeout_ms;
    if (remaining_ms == 0U) {
        remaining_ms = 1U;
    }

    while (keep_polling) {
        uint32_t slice_ms = remaining_ms;

        if (slice_ms > RIVER_XIAOZHI_POLL_LOCK_SLICE_MS) {
            slice_ms = RIVER_XIAOZHI_POLL_LOCK_SLICE_MS;
        }

        locked = river_xiaozhi_transport_lock();
        if (!locked || g_river_xiaozhi.wsclient == NULL) {
            river_xiaozhi_transport_unlock(locked);
            return locked ? RIVER_OK : RIVER_ERR_BUSY;
        }

        ws_poll((int)slice_ms, &g_river_xiaozhi.wsclient);
        keep_polling = g_river_xiaozhi.wsclient != NULL &&
                       g_river_xiaozhi.wsclient->readyState == WSC_OPEN &&
                       remaining_ms > slice_ms;
        river_xiaozhi_transport_unlock(locked);

        if (remaining_ms <= slice_ms) {
            break;
        }
        remaining_ms -= slice_ms;
    }

    return RIVER_OK;
}

river_status_t river_xiaozhi_send_listen_start(const char *mode)
{
    (void)mode;
    return river_xiaozhi_ensure_dialog_started("keyword");
}

river_status_t river_xiaozhi_send_listen_stop(void)
{
    return river_xiaozhi_send_audio_commit_internal("end_of_speech");
}

river_status_t river_xiaozhi_send_listen_detect(const char *text)
{
    river_status_t status;

    status = river_xiaozhi_ensure_dialog_started("text");
    if (status != RIVER_OK) {
        return status;
    }
    return river_xiaozhi_send_text_input_internal(text);
}

river_status_t river_xiaozhi_send_abort(const char *reason)
{
    (void)reason;
    return river_xiaozhi_send_session_update_interrupt();
}

river_status_t river_xiaozhi_send_audio(const uint8_t *payload,
                                        size_t bytes,
                                        uint32_t timestamp_ms)
{
    return river_xiaozhi_send_binary_frame(RIVER_XIAOZHI_BINARY_PCM16,
                                           payload,
                                           bytes,
                                           timestamp_ms);
}

river_status_t river_xiaozhi_send_mcp_payload(const char *payload_json)
{
    cJSON *root = NULL;
    cJSON *payload = NULL;

    if (payload_json == NULL || payload_json[0] == '\0') {
        return RIVER_ERR_ARG;
    }

    payload = cJSON_Parse(payload_json);
    if (payload == NULL) {
        river_xiaozhi_set_last_error("mcp_payload_json_invalid");
        return RIVER_ERR_ARG;
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
        cJSON_Delete(payload);
        return RIVER_ERR_NO_MEMORY;
    }

    river_xiaozhi_add_session_id(root);
    cJSON_AddStringToObject(root, "type", "mcp");
    cJSON_AddItemToObject(root, "payload", payload);
    return river_xiaozhi_send_json_root(root);
}

void river_xiaozhi_dump_status(void)
{
    uint32_t ready = 0U;
    uint32_t recycle = 0U;
    uint32_t max = 0U;
    uint32_t stable = 0U;
    uint32_t bootstrap_refresh_in_ms = 0U;
    uint32_t audio_soft_limit = 0U;
    bool locked = false;

    if (!g_river_xiaozhi.initialized) {
        RIVER_LOGI("xiaozhi session=uninitialized");
        return;
    }

    locked = river_xiaozhi_transport_lock();
    if (locked) {
        river_xiaozhi_send_queue_snapshot_locked(&ready, &recycle, &max, &stable);
        river_xiaozhi_transport_unlock(locked);
    }
    audio_soft_limit = river_xiaozhi_send_queue_soft_limit(max,
                                                           RIVER_XIAOZHI_WS_AUDIO_QUEUE_RESERVE);
    bootstrap_refresh_in_ms = river_xiaozhi_bootstrap_cache_remaining_ms();

    RIVER_LOGI("xiaozhi session=%s hello=%s configured=%s ota_set=%s url_set=%s token_set=%s wire=%s protocol=%u mcp=%s sid=%s device_id=%s client_id=%s retry_in_ms=%lu bootstrap_owned=%s bootstrap_refresh_in_ms=%lu server_audio=%luHz/%lums text_rx=%lu text_tx=%lu audio_rx=%lu audio_tx=%lu opened=%lu closed=%lu mcp_rx=%lu mcp_tx=%lu mcp_fail=%lu txq=%lu/%lu recycle=%lu stable=%lu audio_soft_limit=%lu q_peak=%lu bp=%lu reserve_bp=%lu full_bp=%lu activation_code=%s last_type=%s last_state=%s last_emotion=%s last_text=%s last_err=%s",
               river_xiaozhi_bool_text(g_river_xiaozhi.session_open),
               river_xiaozhi_bool_text(g_river_xiaozhi.server_hello_received),
               river_xiaozhi_bool_text(g_river_xiaozhi.config_ready),
               river_xiaozhi_bool_text(g_river_xiaozhi.ota_url[0] != '\0'),
               river_xiaozhi_bool_text(g_river_xiaozhi.url[0] != '\0'),
               river_xiaozhi_bool_text(g_river_xiaozhi.token[0] != '\0'),
               RIVER_XIAOZHI_REALTIME_PROTOCOL_VERSION,
               (unsigned int)g_river_xiaozhi.config.protocol_version,
               river_xiaozhi_bool_text(g_river_xiaozhi.config.enable_mcp),
               g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
               g_river_xiaozhi.device_id[0] != '\0' ? g_river_xiaozhi.device_id : "-",
               g_river_xiaozhi.client_id[0] != '\0' ? g_river_xiaozhi.client_id : "-",
               (unsigned long)river_xiaozhi_bootstrap_retry_remaining_ms(),
               river_xiaozhi_bool_text(g_river_xiaozhi.bootstrap_config_owned),
               (unsigned long)bootstrap_refresh_in_ms,
               (unsigned long)g_river_xiaozhi.server_sample_rate,
               (unsigned long)g_river_xiaozhi.server_frame_duration_ms,
               (unsigned long)g_river_xiaozhi.text_messages_rx,
               (unsigned long)g_river_xiaozhi.text_messages_tx,
               (unsigned long)g_river_xiaozhi.audio_messages_rx,
               (unsigned long)g_river_xiaozhi.audio_messages_tx,
               (unsigned long)g_river_xiaozhi.sessions_opened,
               (unsigned long)g_river_xiaozhi.sessions_closed,
               (unsigned long)g_river_xiaozhi.mcp_requests_rx,
               (unsigned long)g_river_xiaozhi.mcp_responses_tx,
               (unsigned long)g_river_xiaozhi.mcp_failures,
               (unsigned long)ready,
               (unsigned long)max,
               (unsigned long)recycle,
               (unsigned long)stable,
               (unsigned long)audio_soft_limit,
               (unsigned long)g_river_xiaozhi.send_queue_high_watermark,
               (unsigned long)g_river_xiaozhi.send_backpressure_events,
               (unsigned long)g_river_xiaozhi.send_backpressure_reserve_events,
               (unsigned long)g_river_xiaozhi.send_backpressure_full_events,
               g_river_xiaozhi.activation_code[0] != '\0' ? g_river_xiaozhi.activation_code : "-",
               g_river_xiaozhi.last_type[0] != '\0' ? g_river_xiaozhi.last_type : "-",
               g_river_xiaozhi.last_state[0] != '\0' ? g_river_xiaozhi.last_state : "-",
               g_river_xiaozhi.last_emotion[0] != '\0' ? g_river_xiaozhi.last_emotion : "-",
               g_river_xiaozhi.last_text[0] != '\0' ? g_river_xiaozhi.last_text : "-",
               g_river_xiaozhi.last_error[0] != '\0' ? g_river_xiaozhi.last_error : "-");
    if (g_river_xiaozhi.activation_message[0] != '\0') {
        RIVER_LOGI("xiaozhi activation_message=%s challenge_set=%s timeout_ms=%lu",
                   g_river_xiaozhi.activation_message,
                   river_xiaozhi_bool_text(g_river_xiaozhi.activation_challenge[0] != '\0'),
                   (unsigned long)g_river_xiaozhi.activation_timeout_ms);
    }
}
