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
#include "river/river_voice_profile.h"
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
#define RIVER_XIAOZHI_LAST_TURN_ID_MAX     96U
#define RIVER_XIAOZHI_LAST_ACCEPT_REASON_MAX 32U
#define RIVER_XIAOZHI_LAST_EMOTION_MAX     32U
#define RIVER_XIAOZHI_LAST_PREVIEW_ID_MAX  64U
#define RIVER_XIAOZHI_LAST_PREVIEW_SOURCE_MAX 32U
#define RIVER_XIAOZHI_LAST_PREVIEW_REASON_MAX 64U
#define RIVER_XIAOZHI_LAST_RESPONSE_ID_MAX 96U
#define RIVER_XIAOZHI_LAST_PLAYBACK_ID_MAX 96U
#define RIVER_XIAOZHI_LAST_SEGMENT_ID_MAX  96U
#define RIVER_XIAOZHI_LAST_OUTPUT_LANE_MAX 32U
#define RIVER_XIAOZHI_LAST_OUTPUT_ROLE_MAX 32U
#define RIVER_XIAOZHI_LAST_PHRASE_ID_MAX   64U
#define RIVER_XIAOZHI_TURN_MODE_MAX        48U
#define RIVER_XIAOZHI_COLLAB_MODE_MAX      32U
#define RIVER_XIAOZHI_WIRE_PROFILE_MAX     64U
#define RIVER_XIAOZHI_PRODUCT_PROFILE_MAX  64U
#define RIVER_XIAOZHI_ACTIVATION_CODE_MAX  16U
#define RIVER_XIAOZHI_ACTIVATION_MESSAGE_MAX 192U
#define RIVER_XIAOZHI_ACTIVATION_CHALLENGE_MAX 192U
#define RIVER_XIAOZHI_MCP_RESPONSE_MAX     640U
#define RIVER_XIAOZHI_HTTP_RESPONSE_MAX    4096U

#define RIVER_XIAOZHI_HTTP_USER_AGENT      "ameba-river/xiaozhi"
#define RIVER_XIAOZHI_DISCOVERY_PATH_DEFAULT "/v1/realtime"
#define RIVER_XIAOZHI_OTA_RETRY_MIN_MS     5000U
#define RIVER_XIAOZHI_OTA_RETRY_MAX_MS     30000U
#define RIVER_XIAOZHI_WS_RECV_TIMEOUT_MS   10000U
#define RIVER_XIAOZHI_WS_SEND_TIMEOUT_MS   200U
#define RIVER_XIAOZHI_WS_CONNECT_TIMEOUT_MS 15000U
#define RIVER_XIAOZHI_WS_SEND_BLOCK_MS     0U
#define RIVER_XIAOZHI_WS_AUDIO_QUEUE_RESERVE 2U
#define RIVER_XIAOZHI_WS_QUEUE_LOG_INTERVAL_MS 1000U
#define RIVER_XIAOZHI_PREVIEW_LOG_INTERVAL_MS 250U
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
    bool last_barge_in_enabled;
    bool last_barge_in_enabled_known;
    bool last_preview_speech_started;
    bool last_preview_accept_ready;
    bool last_preview_endpoint_candidate;
    bool last_preview_final;
    bool last_playback_is_last_segment;
    bool discovery_voice_collaboration_advertised;
    bool discovery_server_endpoint_available;
    bool discovery_server_endpoint_enabled;
    bool discovery_preview_events_enabled;
    bool discovery_preview_speech_start;
    bool discovery_preview_partial;
    bool discovery_preview_accept_ready;
    bool discovery_preview_endpoint_candidate;
    bool discovery_playback_ack_enabled;
    bool discovery_playback_ack_started;
    bool discovery_playback_ack_mark;
    bool discovery_playback_ack_cleared;
    bool discovery_playback_ack_completed;
    uint64_t bootstrap_retry_after_ms;
    uint64_t bootstrap_cache_expire_at_ms;
    uint64_t last_backpressure_log_ms;
    uint64_t last_session_update_at_ms;
    uint64_t last_accept_at_ms;
    uint64_t last_preview_speech_start_at_ms;
    uint64_t last_preview_update_at_ms;
    uint64_t last_preview_log_at_ms;
    uint64_t last_accept_ready_at_ms;
    uint64_t last_endpoint_candidate_at_ms;
    uint64_t last_response_start_at_ms;
    uint64_t last_audio_out_meta_at_ms;
    uint32_t preview_update_events;
    uint32_t preview_logs_emitted;
    uint32_t preview_logs_suppressed;
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
    char last_session_state[RIVER_XIAOZHI_LAST_STATE_MAX];
    char last_input_state[RIVER_XIAOZHI_LAST_STATE_MAX];
    char last_output_state[RIVER_XIAOZHI_LAST_STATE_MAX];
    char last_turn_id[RIVER_XIAOZHI_LAST_TURN_ID_MAX];
    char last_accept_reason[RIVER_XIAOZHI_LAST_ACCEPT_REASON_MAX];
    char last_emotion[RIVER_XIAOZHI_LAST_EMOTION_MAX];
    char last_preview_id[RIVER_XIAOZHI_LAST_PREVIEW_ID_MAX];
    char last_preview_text[RIVER_XIAOZHI_LAST_TEXT_MAX];
    char last_preview_stable_prefix[RIVER_XIAOZHI_LAST_TEXT_MAX];
    char last_preview_source[RIVER_XIAOZHI_LAST_PREVIEW_SOURCE_MAX];
    char last_preview_reason[RIVER_XIAOZHI_LAST_PREVIEW_REASON_MAX];
    char last_accept_ready_reason[RIVER_XIAOZHI_LAST_PREVIEW_REASON_MAX];
    uint32_t last_preview_audio_offset_ms;
    char last_response_id[RIVER_XIAOZHI_LAST_RESPONSE_ID_MAX];
    char last_playback_id[RIVER_XIAOZHI_LAST_PLAYBACK_ID_MAX];
    char last_segment_id[RIVER_XIAOZHI_LAST_SEGMENT_ID_MAX];
    char last_playback_output_lane[RIVER_XIAOZHI_LAST_OUTPUT_LANE_MAX];
    char last_playback_output_role[RIVER_XIAOZHI_LAST_OUTPUT_ROLE_MAX];
    char last_playback_phrase_id[RIVER_XIAOZHI_LAST_PHRASE_ID_MAX];
    uint32_t last_playback_expected_duration_ms;
    char discovery_protocol_version[RIVER_XIAOZHI_WIRE_PROFILE_MAX];
    char discovery_subprotocol[RIVER_XIAOZHI_WIRE_PROFILE_MAX];
    char discovery_product_profile[RIVER_XIAOZHI_PRODUCT_PROFILE_MAX];
    char discovery_mainline_profile[RIVER_XIAOZHI_PRODUCT_PROFILE_MAX];
    char discovery_turn_mode[RIVER_XIAOZHI_TURN_MODE_MAX];
    char discovery_server_endpoint_mode[RIVER_XIAOZHI_COLLAB_MODE_MAX];
    char discovery_preview_events_mode[RIVER_XIAOZHI_COLLAB_MODE_MAX];
    char discovery_playback_ack_mode[RIVER_XIAOZHI_COLLAB_MODE_MAX];
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

static bool river_xiaozhi_negotiated_preview_events_enabled(void);
static const char *river_xiaozhi_negotiated_playback_ack_mode(void);
static const char *river_xiaozhi_preview_negotiation_reason(void);
static const char *river_xiaozhi_playback_ack_negotiation_reason(void);
static void river_xiaozhi_log_collaboration_negotiation(const char *trigger);

bool river_xiaozhi_full_duplex_default_on_enabled(void)
{
#if defined(CONFIG_RIVER_XIAOZHI_FULL_DUPLEX_DEFAULT_ON_EN) && \
    CONFIG_RIVER_XIAOZHI_FULL_DUPLEX_DEFAULT_ON_EN
    return true;
#else
    return false;
#endif
}

static bool river_xiaozhi_profile_supports_playback_reference(void)
{
    river_voice_preproc_profile_t profile;

    profile = river_voice_profile_active_preproc();
    return river_voice_profile_has_capability(profile, RIVER_VOICE_CAPABILITY_AEC) ||
           river_voice_profile_has_capability(profile, RIVER_VOICE_CAPABILITY_NATIVE_CAPTURE_REF);
}

const char *river_xiaozhi_duplex_default_fallback_reason(void)
{
#if !defined(CONFIG_RIVER_XIAOZHI_FULL_DUPLEX_EXPERIMENT_EN) || \
    !CONFIG_RIVER_XIAOZHI_FULL_DUPLEX_EXPERIMENT_EN
    return "half_duplex_experiment_disabled";
#else
    if (!river_xiaozhi_full_duplex_default_on_enabled()) {
        return "half_duplex_default_policy_disabled";
    }
    if (!river_xiaozhi_profile_supports_playback_reference()) {
        return "half_duplex_no_playback_reference";
    }
    if (!g_river_xiaozhi.discovery_voice_collaboration_advertised) {
        return "half_duplex_service_collaboration_unavailable";
    }
    if (!g_river_xiaozhi.discovery_server_endpoint_available) {
        return "half_duplex_service_endpoint_unavailable";
    }
    if (!g_river_xiaozhi.discovery_server_endpoint_enabled) {
        return "half_duplex_service_endpoint_disabled";
    }
    if (!river_xiaozhi_negotiated_preview_events_enabled()) {
        return "half_duplex_service_preview_unavailable";
    }
    if (river_xiaozhi_negotiated_playback_ack_mode() == NULL) {
        return "half_duplex_service_playback_ack_unavailable";
    }
    return NULL;
#endif
}

bool river_xiaozhi_duplex_default_on_allowed(void)
{
    return river_xiaozhi_duplex_default_fallback_reason() == NULL;
}

static bool river_xiaozhi_half_duplex_capability_enabled(void)
{
    return !river_xiaozhi_duplex_default_on_allowed();
}

static const char *river_xiaozhi_duplex_mode_name(void)
{
    return river_xiaozhi_half_duplex_capability_enabled() ?
               "half_duplex" :
               "full_duplex_experiment";
}

static void river_xiaozhi_copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0U) {
        return;
    }

    snprintf(dst, dst_size, "%s", src != NULL ? src : "");
}

static void river_xiaozhi_copy_optional_string(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0U) {
        return;
    }
    if (src == NULL || src[0] == '\0') {
        dst[0] = '\0';
        return;
    }

    river_xiaozhi_copy_string(dst, dst_size, src);
}

static const char *river_xiaozhi_dash_if_empty(const char *text)
{
    return (text != NULL && text[0] != '\0') ? text : "-";
}

static const char *river_xiaozhi_active_protocol_version(void)
{
    return g_river_xiaozhi.discovery_protocol_version[0] != '\0' ?
               g_river_xiaozhi.discovery_protocol_version :
               RIVER_XIAOZHI_REALTIME_PROTOCOL_VERSION;
}

static const char *river_xiaozhi_active_subprotocol(void)
{
    return g_river_xiaozhi.discovery_subprotocol[0] != '\0' ?
               g_river_xiaozhi.discovery_subprotocol :
               RIVER_XIAOZHI_REALTIME_SUBPROTOCOL;
}

static const char *river_xiaozhi_active_product_profile(void)
{
    return river_xiaozhi_dash_if_empty(g_river_xiaozhi.discovery_product_profile);
}

static const char *river_xiaozhi_active_mainline_profile(void)
{
    return river_xiaozhi_dash_if_empty(g_river_xiaozhi.discovery_mainline_profile);
}

static const char *river_xiaozhi_optional_bool_text(bool known, bool value)
{
    if (!known) {
        return "-";
    }
    return river_xiaozhi_bool_text(value);
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

static void river_xiaozhi_set_last_session_state(const char *state_text)
{
    river_xiaozhi_copy_optional_string(g_river_xiaozhi.last_session_state,
                                       sizeof(g_river_xiaozhi.last_session_state),
                                       state_text);
}

static void river_xiaozhi_set_last_input_state(const char *state_text)
{
    river_xiaozhi_copy_optional_string(g_river_xiaozhi.last_input_state,
                                       sizeof(g_river_xiaozhi.last_input_state),
                                       state_text);
}

static void river_xiaozhi_set_last_output_state(const char *state_text)
{
    river_xiaozhi_copy_optional_string(g_river_xiaozhi.last_output_state,
                                       sizeof(g_river_xiaozhi.last_output_state),
                                       state_text);
}

static void river_xiaozhi_set_last_turn_id(const char *turn_id)
{
    river_xiaozhi_copy_optional_string(g_river_xiaozhi.last_turn_id,
                                       sizeof(g_river_xiaozhi.last_turn_id),
                                       turn_id);
}

static void river_xiaozhi_set_last_accept_reason(const char *reason)
{
    river_xiaozhi_copy_optional_string(g_river_xiaozhi.last_accept_reason,
                                       sizeof(g_river_xiaozhi.last_accept_reason),
                                       reason);
}

static void river_xiaozhi_set_last_barge_in_enabled(bool known, bool enabled)
{
    g_river_xiaozhi.last_barge_in_enabled_known = known;
    g_river_xiaozhi.last_barge_in_enabled = known && enabled;
}

static void river_xiaozhi_clear_last_preview_fields(void)
{
    g_river_xiaozhi.last_preview_speech_started = false;
    g_river_xiaozhi.last_preview_accept_ready = false;
    g_river_xiaozhi.last_preview_endpoint_candidate = false;
    g_river_xiaozhi.last_preview_final = false;
    g_river_xiaozhi.last_preview_audio_offset_ms = 0U;
    g_river_xiaozhi.last_preview_speech_start_at_ms = 0U;
    g_river_xiaozhi.last_preview_update_at_ms = 0U;
    g_river_xiaozhi.last_preview_log_at_ms = 0U;
    g_river_xiaozhi.last_accept_ready_at_ms = 0U;
    g_river_xiaozhi.last_endpoint_candidate_at_ms = 0U;
    g_river_xiaozhi.preview_update_events = 0U;
    g_river_xiaozhi.preview_logs_emitted = 0U;
    g_river_xiaozhi.preview_logs_suppressed = 0U;
    g_river_xiaozhi.last_preview_id[0] = '\0';
    g_river_xiaozhi.last_preview_text[0] = '\0';
    g_river_xiaozhi.last_preview_stable_prefix[0] = '\0';
    g_river_xiaozhi.last_preview_source[0] = '\0';
    g_river_xiaozhi.last_preview_reason[0] = '\0';
    g_river_xiaozhi.last_accept_ready_reason[0] = '\0';
}

static void river_xiaozhi_set_last_preview_id(const char *preview_id)
{
    river_xiaozhi_copy_optional_string(g_river_xiaozhi.last_preview_id,
                                       sizeof(g_river_xiaozhi.last_preview_id),
                                       preview_id);
}

static void river_xiaozhi_set_last_preview_text(const char *text)
{
    river_xiaozhi_copy_optional_string(g_river_xiaozhi.last_preview_text,
                                       sizeof(g_river_xiaozhi.last_preview_text),
                                       text);
}

static void river_xiaozhi_set_last_preview_stable_prefix(const char *text)
{
    river_xiaozhi_copy_optional_string(g_river_xiaozhi.last_preview_stable_prefix,
                                       sizeof(g_river_xiaozhi.last_preview_stable_prefix),
                                       text);
}

static void river_xiaozhi_set_last_preview_source(const char *source)
{
    river_xiaozhi_copy_optional_string(g_river_xiaozhi.last_preview_source,
                                       sizeof(g_river_xiaozhi.last_preview_source),
                                       source);
}

static void river_xiaozhi_set_last_preview_reason(const char *reason)
{
    river_xiaozhi_copy_optional_string(g_river_xiaozhi.last_preview_reason,
                                       sizeof(g_river_xiaozhi.last_preview_reason),
                                       reason);
}

static void river_xiaozhi_clear_last_playback_fields(void)
{
    g_river_xiaozhi.last_playback_is_last_segment = false;
    g_river_xiaozhi.last_playback_expected_duration_ms = 0U;
    g_river_xiaozhi.last_audio_out_meta_at_ms = 0U;
    g_river_xiaozhi.last_response_id[0] = '\0';
    g_river_xiaozhi.last_playback_id[0] = '\0';
    g_river_xiaozhi.last_segment_id[0] = '\0';
    g_river_xiaozhi.last_playback_output_lane[0] = '\0';
    g_river_xiaozhi.last_playback_output_role[0] = '\0';
    g_river_xiaozhi.last_playback_phrase_id[0] = '\0';
}

static bool river_xiaozhi_last_playback_meta_valid(void)
{
    return g_river_xiaozhi.last_response_id[0] != '\0' &&
           g_river_xiaozhi.last_playback_id[0] != '\0' &&
           g_river_xiaozhi.last_segment_id[0] != '\0';
}

static void river_xiaozhi_set_last_response_id(const char *response_id)
{
    river_xiaozhi_copy_optional_string(g_river_xiaozhi.last_response_id,
                                       sizeof(g_river_xiaozhi.last_response_id),
                                       response_id);
}

static void river_xiaozhi_set_last_playback_id(const char *playback_id)
{
    river_xiaozhi_copy_optional_string(g_river_xiaozhi.last_playback_id,
                                       sizeof(g_river_xiaozhi.last_playback_id),
                                       playback_id);
}

static void river_xiaozhi_set_last_segment_id(const char *segment_id)
{
    river_xiaozhi_copy_optional_string(g_river_xiaozhi.last_segment_id,
                                       sizeof(g_river_xiaozhi.last_segment_id),
                                       segment_id);
}

static void river_xiaozhi_set_last_playback_output_lane(const char *output_lane)
{
    river_xiaozhi_copy_optional_string(g_river_xiaozhi.last_playback_output_lane,
                                       sizeof(g_river_xiaozhi.last_playback_output_lane),
                                       output_lane);
}

static void river_xiaozhi_set_last_playback_output_role(const char *output_role)
{
    river_xiaozhi_copy_optional_string(g_river_xiaozhi.last_playback_output_role,
                                       sizeof(g_river_xiaozhi.last_playback_output_role),
                                       output_role);
}

static void river_xiaozhi_set_last_playback_phrase_id(const char *phrase_id)
{
    river_xiaozhi_copy_optional_string(g_river_xiaozhi.last_playback_phrase_id,
                                       sizeof(g_river_xiaozhi.last_playback_phrase_id),
                                       phrase_id);
}

static void river_xiaozhi_clear_discovery_profile(void)
{
    g_river_xiaozhi.discovery_voice_collaboration_advertised = false;
    g_river_xiaozhi.discovery_server_endpoint_available = false;
    g_river_xiaozhi.discovery_server_endpoint_enabled = false;
    g_river_xiaozhi.discovery_preview_events_enabled = false;
    g_river_xiaozhi.discovery_preview_speech_start = false;
    g_river_xiaozhi.discovery_preview_partial = false;
    g_river_xiaozhi.discovery_preview_accept_ready = false;
    g_river_xiaozhi.discovery_preview_endpoint_candidate = false;
    g_river_xiaozhi.discovery_playback_ack_enabled = false;
    g_river_xiaozhi.discovery_playback_ack_started = false;
    g_river_xiaozhi.discovery_playback_ack_mark = false;
    g_river_xiaozhi.discovery_playback_ack_cleared = false;
    g_river_xiaozhi.discovery_playback_ack_completed = false;
    g_river_xiaozhi.discovery_protocol_version[0] = '\0';
    g_river_xiaozhi.discovery_subprotocol[0] = '\0';
    g_river_xiaozhi.discovery_product_profile[0] = '\0';
    g_river_xiaozhi.discovery_mainline_profile[0] = '\0';
    g_river_xiaozhi.discovery_turn_mode[0] = '\0';
    g_river_xiaozhi.discovery_server_endpoint_mode[0] = '\0';
    g_river_xiaozhi.discovery_preview_events_mode[0] = '\0';
    g_river_xiaozhi.discovery_playback_ack_mode[0] = '\0';
}

static void river_xiaozhi_clear_last_session_update_fields(void)
{
    g_river_xiaozhi.last_session_update_at_ms = 0U;
    g_river_xiaozhi.last_accept_at_ms = 0U;
    g_river_xiaozhi.last_session_state[0] = '\0';
    g_river_xiaozhi.last_input_state[0] = '\0';
    g_river_xiaozhi.last_output_state[0] = '\0';
    g_river_xiaozhi.last_turn_id[0] = '\0';
    g_river_xiaozhi.last_accept_reason[0] = '\0';
    g_river_xiaozhi.last_barge_in_enabled_known = false;
    g_river_xiaozhi.last_barge_in_enabled = false;
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
    g_river_xiaozhi.last_response_start_at_ms = 0U;
    g_river_xiaozhi.session_id[0] = '\0';
    river_xiaozhi_clear_last_session_update_fields();
    river_xiaozhi_clear_last_preview_fields();
    river_xiaozhi_clear_last_playback_fields();
}

static void river_xiaozhi_reset_runtime_state(void)
{
    g_river_xiaozhi.session_open = false;
    g_river_xiaozhi.server_hello_received = false;
    g_river_xiaozhi.ws_closed = true;
    g_river_xiaozhi.next_sequence = 1U;
    river_xiaozhi_reset_dialog_state();
}

static uint64_t river_xiaozhi_now_ms(void)
{
    return (uint64_t)rtos_time_get_current_system_time_ms();
}

static const char *river_xiaozhi_format_elapsed_ms(char *buffer,
                                                   size_t buffer_size,
                                                   uint64_t now_ms,
                                                   uint64_t event_ms)
{
    if (buffer == NULL || buffer_size == 0U) {
        return "";
    }

    if (event_ms == 0U || now_ms == 0U || now_ms < event_ms) {
        snprintf(buffer, buffer_size, "-");
        return buffer;
    }

    snprintf(buffer, buffer_size, "%lu", (unsigned long)(now_ms - event_ms));
    return buffer;
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
                                     const char *preview_id,
                                     const char *stable_prefix,
                                     const char *reason,
                                     const char *source,
                                     const char *response_id,
                                     const char *playback_id,
                                     const char *segment_id,
                                     uint32_t sample_rate,
                                     uint32_t frame_duration_ms,
                                     uint32_t timestamp_ms,
                                     uint32_t audio_offset_ms,
                                     uint32_t expected_duration_ms,
                                     const uint8_t *binary_data,
                                     size_t binary_bytes,
                                     uint16_t binary_type,
                                     bool candidate,
                                     bool is_final,
                                     bool is_last_segment)
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
    event.preview_id = preview_id;
    event.stable_prefix = stable_prefix;
    event.reason = reason;
    event.source = source;
    event.response_id = response_id;
    event.playback_id = playback_id;
    event.segment_id = segment_id;
    if (type == RIVER_XIAOZHI_EVENT_AUDIO_OUT_META) {
        event.output_lane = g_river_xiaozhi.last_playback_output_lane[0] != '\0' ?
                                g_river_xiaozhi.last_playback_output_lane :
                                NULL;
        event.output_role = g_river_xiaozhi.last_playback_output_role[0] != '\0' ?
                                g_river_xiaozhi.last_playback_output_role :
                                NULL;
        event.phrase_id = g_river_xiaozhi.last_playback_phrase_id[0] != '\0' ?
                              g_river_xiaozhi.last_playback_phrase_id :
                              NULL;
    }
    event.sample_rate = sample_rate;
    event.frame_duration_ms = frame_duration_ms;
    event.timestamp_ms = timestamp_ms;
    event.audio_offset_ms = audio_offset_ms;
    event.expected_duration_ms = expected_duration_ms;
    event.binary_data = binary_data;
    event.binary_bytes = binary_bytes;
    event.binary_type = binary_type;
    event.candidate = candidate;
    event.is_final = is_final;
    event.is_last_segment = is_last_segment;

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
    river_xiaozhi_clear_discovery_profile();
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

static bool river_xiaozhi_client_supports_preview_events(void)
{
    return true;
}

static bool river_xiaozhi_client_supports_playback_ack_started(void)
{
    return true;
}

static bool river_xiaozhi_client_supports_playback_ack_mark(void)
{
    return false;
}

static bool river_xiaozhi_client_supports_playback_ack_cleared(void)
{
    return true;
}

static bool river_xiaozhi_client_supports_playback_ack_completed(void)
{
    return true;
}

static const char *river_xiaozhi_client_supported_playback_ack_mode(void)
{
    return "started_completed_v1";
}

static bool river_xiaozhi_discovery_preview_events_supported(void)
{
    return g_river_xiaozhi.discovery_preview_events_enabled &&
           strcmp(g_river_xiaozhi.discovery_preview_events_mode, "preview_v1") == 0;
}

static const char *river_xiaozhi_discovery_playback_ack_mode(void)
{
    if (!g_river_xiaozhi.discovery_playback_ack_enabled) {
        return NULL;
    }
    if (strcmp(g_river_xiaozhi.discovery_playback_ack_mode, "started_completed_v1") != 0) {
        return NULL;
    }
    return g_river_xiaozhi.discovery_playback_ack_mode;
}

static bool river_xiaozhi_negotiated_preview_events_enabled(void)
{
    return river_xiaozhi_client_supports_preview_events() &&
           river_xiaozhi_discovery_preview_events_supported();
}

static const char *river_xiaozhi_preview_negotiation_reason(void)
{
    if (!river_xiaozhi_client_supports_preview_events()) {
        return "client_preview_unsupported";
    }
    if (!g_river_xiaozhi.discovery_voice_collaboration_advertised) {
        return "service_voice_collaboration_missing";
    }
    if (!g_river_xiaozhi.discovery_preview_events_enabled) {
        return "service_preview_disabled";
    }
    if (strcmp(g_river_xiaozhi.discovery_preview_events_mode, "preview_v1") != 0) {
        return "service_preview_mode_mismatch";
    }
    return NULL;
}

static const char *river_xiaozhi_negotiated_playback_ack_mode(void)
{
    const char *client_mode;
    const char *server_mode;

    client_mode = river_xiaozhi_client_supported_playback_ack_mode();
    server_mode = river_xiaozhi_discovery_playback_ack_mode();
    if (client_mode == NULL || client_mode[0] == '\0' || server_mode == NULL ||
        server_mode[0] == '\0') {
        return NULL;
    }
    if (strcmp(client_mode, server_mode) != 0) {
        return NULL;
    }
    if (!river_xiaozhi_client_supports_playback_ack_started() ||
        !river_xiaozhi_client_supports_playback_ack_cleared() ||
        !river_xiaozhi_client_supports_playback_ack_completed()) {
        return NULL;
    }
    if (!g_river_xiaozhi.discovery_playback_ack_started ||
        !g_river_xiaozhi.discovery_playback_ack_cleared ||
        !g_river_xiaozhi.discovery_playback_ack_completed) {
        return NULL;
    }
    return client_mode;
}

static const char *river_xiaozhi_playback_ack_negotiation_reason(void)
{
    const char *client_mode;

    client_mode = river_xiaozhi_client_supported_playback_ack_mode();
    if (!g_river_xiaozhi.discovery_voice_collaboration_advertised) {
        return "service_voice_collaboration_missing";
    }
    if (client_mode == NULL || client_mode[0] == '\0') {
        return "client_playback_ack_mode_missing";
    }
    if (!g_river_xiaozhi.discovery_playback_ack_enabled) {
        return "service_playback_ack_disabled";
    }
    if (strcmp(g_river_xiaozhi.discovery_playback_ack_mode, client_mode) != 0) {
        return "service_playback_ack_mode_mismatch";
    }
    if (!river_xiaozhi_client_supports_playback_ack_started() ||
        !river_xiaozhi_client_supports_playback_ack_cleared() ||
        !river_xiaozhi_client_supports_playback_ack_completed()) {
        return "client_playback_ack_shape_incomplete";
    }
    if (!g_river_xiaozhi.discovery_playback_ack_started ||
        !g_river_xiaozhi.discovery_playback_ack_cleared ||
        !g_river_xiaozhi.discovery_playback_ack_completed) {
        return "service_playback_ack_shape_incomplete";
    }
    return NULL;
}

static void river_xiaozhi_log_collaboration_negotiation(const char *trigger)
{
    const char *preview_reason;
    const char *playback_ack_reason;
    const char *playback_ack_mode;

    preview_reason = river_xiaozhi_preview_negotiation_reason();
    playback_ack_reason = river_xiaozhi_playback_ack_negotiation_reason();
    playback_ack_mode = river_xiaozhi_negotiated_playback_ack_mode();
    RIVER_LOGI("xiaozhi collaboration gate: trigger=%s default_on=%s default_reason=%s voice_collaboration=%s server_endpoint=%s/%s mode=%s",
               trigger != NULL ? trigger : "-",
               river_xiaozhi_bool_text(river_xiaozhi_duplex_default_on_allowed()),
               river_xiaozhi_duplex_default_fallback_reason() != NULL ?
                   river_xiaozhi_duplex_default_fallback_reason() :
                   "-",
               river_xiaozhi_bool_text(g_river_xiaozhi.discovery_voice_collaboration_advertised),
               river_xiaozhi_bool_text(g_river_xiaozhi.discovery_server_endpoint_available),
               river_xiaozhi_bool_text(g_river_xiaozhi.discovery_server_endpoint_enabled),
               river_xiaozhi_dash_if_empty(g_river_xiaozhi.discovery_server_endpoint_mode));
    RIVER_LOGI("xiaozhi collaboration preview: trigger=%s client=%s service_enabled=%s service_mode=%s negotiated=%s reason=%s speech_start=%s partial=%s accept_ready=%s endpoint_candidate=%s",
               trigger != NULL ? trigger : "-",
               river_xiaozhi_bool_text(river_xiaozhi_client_supports_preview_events()),
               river_xiaozhi_bool_text(g_river_xiaozhi.discovery_preview_events_enabled),
               river_xiaozhi_dash_if_empty(g_river_xiaozhi.discovery_preview_events_mode),
               river_xiaozhi_bool_text(river_xiaozhi_negotiated_preview_events_enabled()),
               preview_reason != NULL ? preview_reason : "-",
               river_xiaozhi_bool_text(g_river_xiaozhi.discovery_preview_speech_start),
               river_xiaozhi_bool_text(g_river_xiaozhi.discovery_preview_partial),
               river_xiaozhi_bool_text(g_river_xiaozhi.discovery_preview_accept_ready),
               river_xiaozhi_bool_text(g_river_xiaozhi.discovery_preview_endpoint_candidate));
    RIVER_LOGI("xiaozhi collaboration playback_ack: trigger=%s client_mode=%s service_enabled=%s service_mode=%s negotiated=%s reason=%s started=%s mark=%s cleared=%s completed=%s",
               trigger != NULL ? trigger : "-",
               river_xiaozhi_dash_if_empty(river_xiaozhi_client_supported_playback_ack_mode()),
               river_xiaozhi_bool_text(g_river_xiaozhi.discovery_playback_ack_enabled),
               river_xiaozhi_dash_if_empty(g_river_xiaozhi.discovery_playback_ack_mode),
               playback_ack_mode != NULL ? playback_ack_mode : "-",
               playback_ack_reason != NULL ? playback_ack_reason : "-",
               river_xiaozhi_bool_text(g_river_xiaozhi.discovery_playback_ack_started),
               river_xiaozhi_bool_text(g_river_xiaozhi.discovery_playback_ack_mark),
               river_xiaozhi_bool_text(g_river_xiaozhi.discovery_playback_ack_cleared),
               river_xiaozhi_bool_text(g_river_xiaozhi.discovery_playback_ack_completed));
}

static bool river_xiaozhi_playback_ack_started_enabled(void)
{
    return river_xiaozhi_negotiated_playback_ack_mode() != NULL &&
           river_xiaozhi_client_supports_playback_ack_started() &&
           g_river_xiaozhi.discovery_playback_ack_started;
}

static bool river_xiaozhi_playback_ack_mark_enabled(void)
{
    return river_xiaozhi_negotiated_playback_ack_mode() != NULL &&
           river_xiaozhi_client_supports_playback_ack_mark() &&
           g_river_xiaozhi.discovery_playback_ack_mark;
}

static bool river_xiaozhi_playback_ack_cleared_enabled(void)
{
    return river_xiaozhi_negotiated_playback_ack_mode() != NULL &&
           river_xiaozhi_client_supports_playback_ack_cleared() &&
           g_river_xiaozhi.discovery_playback_ack_cleared;
}

static bool river_xiaozhi_playback_ack_completed_enabled(void)
{
    return river_xiaozhi_negotiated_playback_ack_mode() != NULL &&
           river_xiaozhi_client_supports_playback_ack_completed() &&
           g_river_xiaozhi.discovery_playback_ack_completed;
}

static void river_xiaozhi_prepare_preview_window(const char *preview_id)
{
    if (preview_id == NULL || preview_id[0] == '\0') {
        return;
    }

    if (strcmp(g_river_xiaozhi.last_preview_id, preview_id) != 0) {
        river_xiaozhi_clear_last_preview_fields();
        river_xiaozhi_set_last_preview_id(preview_id);
    }
}

static bool river_xiaozhi_should_log_preview_update(uint64_t now_ms,
                                                    bool is_final,
                                                    bool stable_prefix_changed)
{
    bool due = is_final || stable_prefix_changed ||
               g_river_xiaozhi.last_preview_log_at_ms == 0U ||
               (now_ms >= g_river_xiaozhi.last_preview_log_at_ms &&
                (now_ms - g_river_xiaozhi.last_preview_log_at_ms) >=
                    RIVER_XIAOZHI_PREVIEW_LOG_INTERVAL_MS);

    if (due) {
        g_river_xiaozhi.last_preview_log_at_ms = now_ms;
        if (g_river_xiaozhi.preview_logs_emitted < UINT32_MAX) {
            g_river_xiaozhi.preview_logs_emitted++;
        }
    } else if (g_river_xiaozhi.preview_logs_suppressed < UINT32_MAX) {
        g_river_xiaozhi.preview_logs_suppressed++;
    }
    return due;
}

#include "river_xiaozhi_ws_bootstrap_discovery.inc"

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

    RIVER_LOGI("xiaozhi transport ready: sample_rate=%lu frame_duration=%lums wire=%s subprotocol=%s product=%s mainline=%s",
               (unsigned long)g_river_xiaozhi.server_sample_rate,
               (unsigned long)g_river_xiaozhi.server_frame_duration_ms,
               river_xiaozhi_active_protocol_version(),
               river_xiaozhi_active_subprotocol(),
               river_xiaozhi_active_product_profile(),
               river_xiaozhi_active_mainline_profile());
    river_xiaozhi_emit_event(RIVER_XIAOZHI_EVENT_SERVER_HELLO,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             g_river_xiaozhi.server_sample_rate,
                             g_river_xiaozhi.server_frame_duration_ms,
                             0U,
                             0U,
                             0U,
                             NULL,
                             0U,
                             0U,
                             false,
                             false,
                             false);
}

static river_status_t river_xiaozhi_send_session_start_internal(const char *wake_reason)
{
    cJSON *root = NULL;
    cJSON *payload = NULL;
    cJSON *input_audio = NULL;
    cJSON *output_audio = NULL;
    cJSON *capabilities = NULL;
    cJSON *playback_ack = NULL;
    const char *playback_ack_mode = NULL;
    river_status_t status;
    bool half_duplex = river_xiaozhi_half_duplex_capability_enabled();
    bool preview_events = river_xiaozhi_negotiated_preview_events_enabled();
    const char *duplex_default_reason = river_xiaozhi_duplex_default_fallback_reason();

    playback_ack_mode = river_xiaozhi_negotiated_playback_ack_mode();
    river_xiaozhi_log_collaboration_negotiation("session_start");

    root = river_xiaozhi_create_control_event("session.start", &payload);
    input_audio = cJSON_CreateObject();
    output_audio = cJSON_CreateObject();
    capabilities = cJSON_CreateObject();
    if (playback_ack_mode != NULL) {
        playback_ack = cJSON_CreateObject();
    }
    if (root == NULL || payload == NULL || input_audio == NULL || output_audio == NULL ||
        capabilities == NULL ||
        (playback_ack_mode != NULL && playback_ack == NULL)) {
        if (input_audio != NULL) {
            cJSON_Delete(input_audio);
        }
        if (output_audio != NULL) {
            cJSON_Delete(output_audio);
        }
        if (capabilities != NULL) {
            cJSON_Delete(capabilities);
        }
        if (playback_ack != NULL) {
            cJSON_Delete(playback_ack);
        }
        if (root != NULL) {
            cJSON_Delete(root);
        }
        return RIVER_ERR_NO_MEMORY;
    }

    cJSON_AddStringToObject(payload, "protocol_version", river_xiaozhi_active_protocol_version());
    cJSON_AddStringToObject(payload, "rtos_device_id", g_river_xiaozhi.device_id);
    cJSON_AddStringToObject(payload, "client_type", RIVER_XIAOZHI_REALTIME_CLIENT_TYPE);
    cJSON_AddStringToObject(payload,
                            "wake_reason",
                            (wake_reason != NULL && wake_reason[0] != '\0') ?
                                wake_reason :
                                "keyword");
    cJSON_AddStringToObject(payload, "mode_hint", "m1_single_command");

    cJSON_AddStringToObject(input_audio, "codec", RIVER_XIAOZHI_UPLINK_FORMAT);
    cJSON_AddNumberToObject(input_audio,
                            "sample_rate_hz",
                            g_river_xiaozhi.config.uplink_sample_rate);
    cJSON_AddNumberToObject(input_audio, "channels", g_river_xiaozhi.config.uplink_channels);
    cJSON_AddItemToObject(payload, "input_audio", input_audio);

    cJSON_AddStringToObject(output_audio, "codec", RIVER_XIAOZHI_UPLINK_FORMAT);
    cJSON_AddNumberToObject(output_audio,
                            "sample_rate_hz",
                            g_river_xiaozhi.config.uplink_sample_rate);
    cJSON_AddNumberToObject(output_audio, "channels", g_river_xiaozhi.config.uplink_channels);
    cJSON_AddItemToObject(payload, "output_audio", output_audio);

    cJSON_AddBoolToObject(capabilities, "binary_audio", true);
    cJSON_AddBoolToObject(capabilities, "text_input", false);
    cJSON_AddBoolToObject(capabilities, "image_input", false);
    cJSON_AddBoolToObject(capabilities, "half_duplex", half_duplex);
    cJSON_AddBoolToObject(capabilities, "local_wake_word", true);
    cJSON_AddBoolToObject(capabilities, "preview_events", preview_events);
    cJSON_AddBoolToObject(capabilities, "server_endpointing", false);
    if (playback_ack_mode != NULL && playback_ack_mode[0] != '\0') {
        cJSON_AddStringToObject(playback_ack, "mode", playback_ack_mode);
        cJSON_AddItemToObject(capabilities, "playback_ack", playback_ack);
        playback_ack = NULL;
    } else {
        cJSON_AddBoolToObject(capabilities, "playback_ack", false);
    }
    cJSON_AddItemToObject(payload, "capabilities", capabilities);

    status = river_xiaozhi_send_json_root(root);
    if (status == RIVER_OK) {
        g_river_xiaozhi.dialog_started = true;
        g_river_xiaozhi.response_started = false;
        RIVER_LOGI("xiaozhi session.start sent: wake_reason=%s mode_hint=m1_single_command device_id=%s client_id=%s wire=%s subprotocol=%s product=%s mainline=%s codec=%s duplex=%s half_duplex=%s default_on=%s default_reason=%s preview_events=%s server_endpointing=no playback_ack=%s discovery_voice_collaboration=%s",
                   (wake_reason != NULL && wake_reason[0] != '\0') ? wake_reason : "keyword",
                   g_river_xiaozhi.device_id,
                   g_river_xiaozhi.client_id,
                   river_xiaozhi_active_protocol_version(),
                   river_xiaozhi_active_subprotocol(),
                   river_xiaozhi_active_product_profile(),
                   river_xiaozhi_active_mainline_profile(),
                   RIVER_XIAOZHI_UPLINK_FORMAT,
                   river_xiaozhi_duplex_mode_name(),
                   river_xiaozhi_bool_text(half_duplex),
                   river_xiaozhi_bool_text(!half_duplex),
                   duplex_default_reason != NULL ? duplex_default_reason : "-",
                   river_xiaozhi_bool_text(preview_events),
                   playback_ack_mode != NULL ? playback_ack_mode : "-",
                   river_xiaozhi_bool_text(g_river_xiaozhi.discovery_voice_collaboration_advertised));
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
    const char *skip_reason = NULL;

    if (!g_river_xiaozhi.dialog_started) {
        return RIVER_OK;
    }
    if (g_river_xiaozhi.last_session_state[0] != '\0' &&
        strcmp(g_river_xiaozhi.last_session_state, "active") != 0) {
        skip_reason = "session_not_active";
    } else if (strcmp(g_river_xiaozhi.last_input_state, "committed") == 0) {
        skip_reason = "input_committed";
    } else if (strcmp(g_river_xiaozhi.last_output_state, "thinking") == 0 ||
               strcmp(g_river_xiaozhi.last_output_state, "speaking") == 0) {
        skip_reason = "output_busy";
    } else if (g_river_xiaozhi.response_started) {
        skip_reason = "response_started";
    }
    if (skip_reason != NULL) {
        RIVER_LOGI("xiaozhi audio.in.commit skipped: reason=%s skip=%s session_state=%s input_state=%s output_state=%s accept_reason=%s response_started=%s",
                   reason != NULL && reason[0] != '\0' ? reason : "-",
                   skip_reason,
                   g_river_xiaozhi.last_session_state[0] != '\0' ?
                       g_river_xiaozhi.last_session_state :
                       "-",
                   g_river_xiaozhi.last_input_state[0] != '\0' ?
                       g_river_xiaozhi.last_input_state :
                       "-",
                   g_river_xiaozhi.last_output_state[0] != '\0' ?
                       g_river_xiaozhi.last_output_state :
                       "-",
                   g_river_xiaozhi.last_accept_reason[0] != '\0' ?
                       g_river_xiaozhi.last_accept_reason :
                       "-",
                   river_xiaozhi_bool_text(g_river_xiaozhi.response_started));
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
                            "commit_reason",
                            (reason != NULL && reason[0] != '\0') ? reason :
                                                                    "client_endpoint");
    cJSON_AddStringToObject(payload,
                            "reason",
                            (reason != NULL && reason[0] != '\0') ? reason :
                                                                    "client_endpoint");
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

static river_status_t river_xiaozhi_send_audio_out_started_internal(const char *response_id,
                                                                    const char *playback_id,
                                                                    const char *segment_id)
{
    cJSON *root = NULL;
    cJSON *payload = NULL;

    if (!river_xiaozhi_playback_ack_started_enabled()) {
        return RIVER_ERR_UNSUPPORTED;
    }
    if (!g_river_xiaozhi.dialog_started || !river_xiaozhi_session_open()) {
        return RIVER_ERR_BUSY;
    }
    if (response_id == NULL || response_id[0] == '\0' || playback_id == NULL ||
        playback_id[0] == '\0' || segment_id == NULL || segment_id[0] == '\0') {
        return RIVER_ERR_ARG;
    }

    root = river_xiaozhi_create_control_event("audio.out.started", &payload);
    if (root == NULL || payload == NULL) {
        if (root != NULL) {
            cJSON_Delete(root);
        }
        return RIVER_ERR_NO_MEMORY;
    }

    cJSON_AddStringToObject(payload, "response_id", response_id);
    cJSON_AddStringToObject(payload, "playback_id", playback_id);
    cJSON_AddStringToObject(payload, "segment_id", segment_id);
    return river_xiaozhi_send_json_root(root);
}

static river_status_t river_xiaozhi_send_audio_out_mark_internal(const char *response_id,
                                                                 const char *playback_id,
                                                                 const char *segment_id,
                                                                 uint32_t played_duration_ms)
{
    cJSON *root = NULL;
    cJSON *payload = NULL;

    if (!river_xiaozhi_playback_ack_mark_enabled()) {
        return RIVER_ERR_UNSUPPORTED;
    }
    if (!g_river_xiaozhi.dialog_started || !river_xiaozhi_session_open()) {
        return RIVER_ERR_BUSY;
    }
    if (response_id == NULL || response_id[0] == '\0' || playback_id == NULL ||
        playback_id[0] == '\0' || segment_id == NULL || segment_id[0] == '\0') {
        return RIVER_ERR_ARG;
    }

    root = river_xiaozhi_create_control_event("audio.out.mark", &payload);
    if (root == NULL || payload == NULL) {
        if (root != NULL) {
            cJSON_Delete(root);
        }
        return RIVER_ERR_NO_MEMORY;
    }

    cJSON_AddStringToObject(payload, "response_id", response_id);
    cJSON_AddStringToObject(payload, "playback_id", playback_id);
    cJSON_AddStringToObject(payload, "segment_id", segment_id);
    cJSON_AddNumberToObject(payload, "played_duration_ms", played_duration_ms);
    return river_xiaozhi_send_json_root(root);
}

static river_status_t river_xiaozhi_send_audio_out_cleared_internal(
    const char *response_id,
    const char *playback_id,
    const char *cleared_after_segment_id,
    const char *reason)
{
    cJSON *root = NULL;
    cJSON *payload = NULL;

    if (!river_xiaozhi_playback_ack_cleared_enabled()) {
        return RIVER_ERR_UNSUPPORTED;
    }
    if (!g_river_xiaozhi.dialog_started || !river_xiaozhi_session_open()) {
        return RIVER_ERR_BUSY;
    }
    if (response_id == NULL || response_id[0] == '\0' || playback_id == NULL ||
        playback_id[0] == '\0' || cleared_after_segment_id == NULL ||
        cleared_after_segment_id[0] == '\0') {
        return RIVER_ERR_ARG;
    }

    root = river_xiaozhi_create_control_event("audio.out.cleared", &payload);
    if (root == NULL || payload == NULL) {
        if (root != NULL) {
            cJSON_Delete(root);
        }
        return RIVER_ERR_NO_MEMORY;
    }

    cJSON_AddStringToObject(payload, "response_id", response_id);
    cJSON_AddStringToObject(payload, "playback_id", playback_id);
    cJSON_AddStringToObject(payload, "cleared_after_segment_id", cleared_after_segment_id);
    if (reason != NULL && reason[0] != '\0') {
        cJSON_AddStringToObject(payload, "reason", reason);
    }
    return river_xiaozhi_send_json_root(root);
}

static river_status_t river_xiaozhi_send_audio_out_completed_internal(const char *response_id,
                                                                      const char *playback_id)
{
    cJSON *root = NULL;
    cJSON *payload = NULL;

    if (!river_xiaozhi_playback_ack_completed_enabled()) {
        return RIVER_ERR_UNSUPPORTED;
    }
    if (!g_river_xiaozhi.dialog_started || !river_xiaozhi_session_open()) {
        return RIVER_ERR_BUSY;
    }
    if (response_id == NULL || response_id[0] == '\0' || playback_id == NULL ||
        playback_id[0] == '\0') {
        return RIVER_ERR_ARG;
    }

    root = river_xiaozhi_create_control_event("audio.out.completed", &payload);
    if (root == NULL || payload == NULL) {
        if (root != NULL) {
            cJSON_Delete(root);
        }
        return RIVER_ERR_NO_MEMORY;
    }

    cJSON_AddStringToObject(payload, "response_id", response_id);
    cJSON_AddStringToObject(payload, "playback_id", playback_id);
    return river_xiaozhi_send_json_root(root);
}

#include "river_xiaozhi_ws_message_handlers.inc"

static void river_xiaozhi_ws_close_cb(wsclient_context *wsclient, void *user_data)
{
    char closed_sid[RIVER_XIAOZHI_SESSION_ID_MAX];

    (void)wsclient;
    (void)user_data;

    river_xiaozhi_copy_string(closed_sid, sizeof(closed_sid), g_river_xiaozhi.session_id);
    g_river_xiaozhi.ws_closed = true;
    g_river_xiaozhi.session_open = false;
    g_river_xiaozhi.server_hello_received = false;
    g_river_xiaozhi.dialog_started = false;
    g_river_xiaozhi.response_started = false;
    g_river_xiaozhi.sessions_closed++;
    river_xiaozhi_set_last_type("closed");
    river_xiaozhi_clear_last_preview_fields();
    river_xiaozhi_clear_last_playback_fields();
    RIVER_LOGI("xiaozhi websocket closed sid=%s",
               closed_sid[0] != '\0' ? closed_sid : "-");
    river_xiaozhi_emit_event(RIVER_XIAOZHI_EVENT_SESSION_CLOSED,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             0U,
                             0U,
                             0U,
                             0U,
                             0U,
                             NULL,
                             0U,
                             0U,
                             false,
                             false,
                             false);
    g_river_xiaozhi.session_id[0] = '\0';
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
        /*
         * Ameba ws_close() only sends a CLOSE frame and flips the state to
         * CLOSING. The actual socket / queue / mutex teardown still lives in
         * client_close(), so call it here before releasing the outer context.
         * When the peer already closed first, ws_poll()/ws_connect_url() have
         * already run client_close() and readyState is WSC_CLOSED, so skip it.
         */
        if (g_river_xiaozhi.wsclient->readyState == WSC_OPEN ||
            g_river_xiaozhi.wsclient->readyState == WSC_CONNECTING ||
            g_river_xiaozhi.wsclient->readyState == WSC_CLOSING) {
            g_river_xiaozhi.wsclient->fun_ops.client_close(g_river_xiaozhi.wsclient);
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

#include "river_xiaozhi_ws_public_api.inc"
