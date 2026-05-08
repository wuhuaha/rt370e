/* Orvibo realtime WebSocket protocol implementation. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "lwip/def.h"
#include "lwip_netconf.h"
#include "os_wrapper.h"
#include "websocket/libwsclient.h"
#include "websocket/wsclient_api.h"

#include "river/river_log.h"
#include "river/river_orvibo_access.h"
#include "river/river_orvibo_credentials.h"
#include "river/river_orvibo_mcp_volume.h"
#include "river/river_orvibo_protocol.h"
#include "river/river_wifi_station.h"
#include "river_ws_dispatch.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.orvibo.protocol"

#define RIVER_ORVIBO_URL_MAX           192U
#define RIVER_ORVIBO_TOKEN_MAX         256U
#define RIVER_ORVIBO_BASE_URL_MAX      128U
#define RIVER_ORVIBO_PATH_MAX          192U
#define RIVER_ORVIBO_HEADERS_MAX       640U
#define RIVER_ORVIBO_SUBPROTOCOL_MAX   48U
#define RIVER_ORVIBO_DEVICE_ID_MAX     32U
#define RIVER_ORVIBO_CLIENT_ID_MAX     48U
#define RIVER_ORVIBO_SESSION_ID_MAX    96U
#define RIVER_ORVIBO_LAST_TEXT_MAX     192U
#define RIVER_ORVIBO_LAST_ERROR_MAX    128U
#define RIVER_ORVIBO_MCP_RESPONSE_MAX  768U
#define RIVER_ORVIBO_BINARY_PAYLOAD_MAX 1536U
#define RIVER_ORVIBO_POLL_LOCK_SLICE_MS 5U
#define RIVER_ORVIBO_WS_RECV_TIMEOUT_MS 10000U
#define RIVER_ORVIBO_WS_SEND_TIMEOUT_MS 200U
#define RIVER_ORVIBO_WS_CONNECT_TIMEOUT_MS 15000U
#define RIVER_ORVIBO_WS_HELLO_TIMEOUT_MS 10000U
#define RIVER_ORVIBO_WS_HELLO_POLL_MS 20U
#define RIVER_ORVIBO_WS_CHANNEL_TIMEOUT_MS 120000U
#define RIVER_ORVIBO_WS_SEND_BLOCK_MS 0U
#define RIVER_ORVIBO_UPLINK_QUEUE_DEPTH 24U
#define RIVER_ORVIBO_UPLINK_TASK_STACK (1024U * 6U)
#define RIVER_ORVIBO_UPLINK_TASK_PRIORITY 4U
#define RIVER_ORVIBO_UPLINK_RETRY_COUNT 3U
#define RIVER_ORVIBO_UPLINK_RETRY_DELAY_MS 20U
#define RIVER_ORVIBO_RTOS_OK 0

typedef struct {
    uint16_t version;
    uint16_t type;
    uint32_t reserved;
    uint32_t timestamp;
    uint32_t payload_size;
    uint8_t payload[];
} __attribute__((packed)) river_orvibo_binary_v2_t;

typedef struct {
    uint8_t type;
    uint8_t reserved;
    uint16_t payload_size;
    uint8_t payload[];
} __attribute__((packed)) river_orvibo_binary_v3_t;

typedef struct {
    uint32_t session_epoch;
    uint32_t timestamp_ms;
    size_t bytes;
    uint8_t payload[RIVER_ORVIBO_BINARY_PAYLOAD_MAX];
} river_orvibo_uplink_frame_t;

typedef struct {
    bool initialized;
    bool uplink_task_running;
    bool session_open;
    bool server_hello_received;
    bool server_hello_rejected;
    bool ws_closed;
    river_orvibo_protocol_config_t config;
    wsclient_context *wsclient;
    rtos_mutex_t lock;
    rtos_queue_t uplink_queue;
    rtos_task_t uplink_task;
    river_orvibo_protocol_event_handler_t event_handler;
    void *event_handler_user;
    uint32_t session_epoch;
    uint32_t server_sample_rate;
    uint32_t server_channels;
    uint32_t server_frame_duration_ms;
    uint32_t text_rx;
    uint32_t text_tx;
    uint32_t audio_rx;
    uint32_t audio_tx;
    uint32_t tts_sentence_rx;
    uint32_t stt_rx;
    uint32_t llm_rx;
    uint32_t audio_enqueued;
    uint32_t audio_queue_drop_oldest;
    uint32_t audio_queue_full;
    uint32_t audio_drop_closed;
    uint32_t audio_drop_stale;
    uint32_t audio_send_retry;
    uint32_t audio_send_fail;
    uint32_t poll_cycles;
    uint32_t close_events;
    uint32_t channel_timeouts;
    uint32_t last_incoming_ms;
    uint32_t sessions_opened;
    uint32_t sessions_closed;
    uint32_t errors;
    char url[RIVER_ORVIBO_URL_MAX];
    char token[RIVER_ORVIBO_TOKEN_MAX];
    char base_url[RIVER_ORVIBO_BASE_URL_MAX];
    char path[RIVER_ORVIBO_PATH_MAX];
    char headers[RIVER_ORVIBO_HEADERS_MAX];
    char websocket_subprotocol[RIVER_ORVIBO_SUBPROTOCOL_MAX];
    char device_id[RIVER_ORVIBO_DEVICE_ID_MAX];
    char client_id[RIVER_ORVIBO_CLIENT_ID_MAX];
    char session_id[RIVER_ORVIBO_SESSION_ID_MAX];
    char last_text[RIVER_ORVIBO_LAST_TEXT_MAX];
    char last_error[RIVER_ORVIBO_LAST_ERROR_MAX];
    uint8_t binary_frame[sizeof(river_orvibo_binary_v2_t) + RIVER_ORVIBO_BINARY_PAYLOAD_MAX];
} river_orvibo_protocol_context_t;

static river_orvibo_protocol_context_t g_river_orvibo_protocol;

static void river_orvibo_uplink_task(void *param);
static bool river_orvibo_channel_open_locked(void);

static bool river_orvibo_protocol_version_supported(uint16_t version)
{
    return version >= 1U && version <= 3U;
}

static void river_orvibo_copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0U) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_size - 1U);
    dst[dst_size - 1U] = '\0';
}

static void river_orvibo_set_last_error(const char *error)
{
    river_orvibo_copy_text(g_river_orvibo_protocol.last_error,
                           sizeof(g_river_orvibo_protocol.last_error),
                           error != NULL ? error : "-");
    g_river_orvibo_protocol.errors++;
}

static bool river_orvibo_transport_lock(void)
{
    return g_river_orvibo_protocol.initialized &&
           g_river_orvibo_protocol.lock != NULL &&
           rtos_mutex_recursive_take(g_river_orvibo_protocol.lock, MUTEX_WAIT_TIMEOUT) ==
               RIVER_ORVIBO_RTOS_OK;
}

static void river_orvibo_transport_unlock(bool locked)
{
    if (locked) {
        rtos_mutex_recursive_give(g_river_orvibo_protocol.lock);
    }
}

static void river_orvibo_emit_event(river_orvibo_protocol_event_type_t type,
                                    const char *text,
                                    const char *state,
                                    const char *reason,
                                    const cJSON *mcp_payload,
                                    const uint8_t *audio_data,
                                    size_t audio_bytes,
                                    uint32_t sample_rate,
                                    uint32_t channels,
                                    uint32_t frame_duration_ms,
                                    uint32_t timestamp_ms)
{
    river_orvibo_protocol_event_t event;

    if (g_river_orvibo_protocol.event_handler == NULL) {
        return;
    }

    memset(&event, 0, sizeof(event));
    event.type = type;
    event.session_id = g_river_orvibo_protocol.session_id[0] != '\0' ?
                           g_river_orvibo_protocol.session_id :
                           NULL;
    event.text = text;
    event.state = state;
    event.reason = reason;
    event.mcp_payload = mcp_payload;
    event.audio_data = audio_data;
    event.audio_bytes = audio_bytes;
    event.sample_rate = sample_rate;
    event.channels = channels;
    event.frame_duration_ms = frame_duration_ms;
    event.timestamp_ms = timestamp_ms;
    g_river_orvibo_protocol.event_handler(&event,
                                          g_river_orvibo_protocol.event_handler_user);
}

static river_status_t river_orvibo_parse_url(const char *url,
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
    if (host_len == 0U ||
        snprintf(base_url, base_url_size, "%s://%.*s", scheme_text, (int)host_len, host_start) >=
            (int)base_url_size) {
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

static river_status_t river_orvibo_build_headers(void)
{
    char auth_header[320];
    int written;

    river_orvibo_copy_text(g_river_orvibo_protocol.device_id,
                           sizeof(g_river_orvibo_protocol.device_id),
                           river_orvibo_access_device_id());
    river_orvibo_copy_text(g_river_orvibo_protocol.client_id,
                           sizeof(g_river_orvibo_protocol.client_id),
                           river_orvibo_access_client_id());
    auth_header[0] = '\0';
    if (g_river_orvibo_protocol.token[0] != '\0') {
        if (strchr(g_river_orvibo_protocol.token, ' ') == NULL) {
            snprintf(auth_header,
                     sizeof(auth_header),
                     "Authorization: Bearer %s\r\n",
                     g_river_orvibo_protocol.token);
        } else {
            snprintf(auth_header,
                     sizeof(auth_header),
                     "Authorization: %s\r\n",
                     g_river_orvibo_protocol.token);
        }
    }
    written = snprintf(g_river_orvibo_protocol.headers,
                       sizeof(g_river_orvibo_protocol.headers),
                       "%s"
                       "Protocol-Version: %u\r\n"
                       "Device-Id: %s\r\n"
                       "Client-Id: %s\r\n",
                       auth_header,
                       (unsigned int)g_river_orvibo_protocol.config.protocol_version,
                       g_river_orvibo_protocol.device_id,
                       g_river_orvibo_protocol.client_id);
    return (written < 0 || (size_t)written >= sizeof(g_river_orvibo_protocol.headers)) ?
               RIVER_ERR_IO :
               RIVER_OK;
}

static river_status_t river_orvibo_send_json_root(cJSON *root)
{
    char *json;
    int json_len;
    river_status_t status = RIVER_ERR_IO;
    bool locked;

    if (root == NULL) {
        return RIVER_ERR_NO_MEMORY;
    }
    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        return RIVER_ERR_NO_MEMORY;
    }
    json_len = (int)strlen(json);

    locked = river_orvibo_transport_lock();
    if (!locked || g_river_orvibo_protocol.wsclient == NULL ||
        g_river_orvibo_protocol.wsclient->readyState != WSC_OPEN) {
        status = RIVER_ERR_BUSY;
        goto exit;
    }
    if (ws_send(json, json_len, 1, g_river_orvibo_protocol.wsclient) != 0) {
        river_orvibo_set_last_error("json_send_failed");
        goto exit;
    }
    g_river_orvibo_protocol.text_tx++;
    status = RIVER_OK;

exit:
    river_orvibo_transport_unlock(locked);
    cJSON_free(json);
    return status;
}

static river_status_t river_orvibo_send_hello(void)
{
    cJSON *root;
    cJSON *features;
    cJSON *audio_params;

    root = cJSON_CreateObject();
    features = cJSON_CreateObject();
    audio_params = cJSON_CreateObject();
    if (root == NULL || features == NULL || audio_params == NULL) {
        if (audio_params != NULL) {
            cJSON_Delete(audio_params);
        }
        if (features != NULL) {
            cJSON_Delete(features);
        }
        if (root != NULL) {
            cJSON_Delete(root);
        }
        return RIVER_ERR_NO_MEMORY;
    }
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddNumberToObject(root, "version", g_river_orvibo_protocol.config.protocol_version);
    cJSON_AddBoolToObject(features, "mcp", g_river_orvibo_protocol.config.enable_mcp);
    cJSON_AddItemToObject(root, "features", features);
    cJSON_AddStringToObject(root, "transport", "websocket");
    cJSON_AddStringToObject(audio_params, "format", RIVER_ORVIBO_AUDIO_FORMAT);
    cJSON_AddNumberToObject(audio_params,
                            "sample_rate",
                            g_river_orvibo_protocol.config.uplink_sample_rate);
    cJSON_AddNumberToObject(audio_params,
                            "channels",
                            g_river_orvibo_protocol.config.uplink_channels);
    cJSON_AddNumberToObject(audio_params,
                            "frame_duration",
                            g_river_orvibo_protocol.config.uplink_frame_duration_ms);
    cJSON_AddItemToObject(root, "audio_params", audio_params);
    RIVER_LOGI("client hello features: mcp=%s server_aec=no",
               g_river_orvibo_protocol.config.enable_mcp ? "yes" : "no");
    return river_orvibo_send_json_root(root);
}

static void river_orvibo_parse_server_hello(const cJSON *root)
{
    const cJSON *transport_obj;
    const cJSON *session_id_obj;
    const cJSON *audio_params;
    const cJSON *sample_rate_obj;
    const cJSON *channels_obj;
    const cJSON *frame_duration_obj;

    transport_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)root, "transport");
    if (!cJSON_IsString(transport_obj) || transport_obj->valuestring == NULL ||
        strcmp(transport_obj->valuestring, "websocket") != 0) {
        g_river_orvibo_protocol.server_hello_rejected = true;
        river_orvibo_set_last_error("hello_transport_invalid");
        river_orvibo_emit_event(RIVER_ORVIBO_PROTOCOL_EVENT_ERROR,
                                NULL,
                                NULL,
                                "hello_transport_invalid",
                                NULL,
                                NULL,
                                0U,
                                0U,
                                0U,
                                0U,
                                0U);
        return;
    }

    session_id_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)root, "session_id");
    if (cJSON_IsString(session_id_obj) && session_id_obj->valuestring != NULL) {
        river_orvibo_copy_text(g_river_orvibo_protocol.session_id,
                               sizeof(g_river_orvibo_protocol.session_id),
                               session_id_obj->valuestring);
    }
    audio_params = cJSON_GetObjectItemCaseSensitive((cJSON *)root, "audio_params");
    if (cJSON_IsObject(audio_params)) {
        sample_rate_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)audio_params, "sample_rate");
        channels_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)audio_params, "channels");
        frame_duration_obj =
            cJSON_GetObjectItemCaseSensitive((cJSON *)audio_params, "frame_duration");
        if (cJSON_IsNumber(sample_rate_obj) && sample_rate_obj->valueint > 0) {
            g_river_orvibo_protocol.server_sample_rate = (uint32_t)sample_rate_obj->valueint;
        }
        if (cJSON_IsNumber(channels_obj) && channels_obj->valueint > 0) {
            g_river_orvibo_protocol.server_channels = (uint32_t)channels_obj->valueint;
        }
        if (cJSON_IsNumber(frame_duration_obj) && frame_duration_obj->valueint > 0) {
            g_river_orvibo_protocol.server_frame_duration_ms =
                (uint32_t)frame_duration_obj->valueint;
        }
    }
    g_river_orvibo_protocol.server_hello_received = true;
    RIVER_LOGI("server hello: sid=%s audio=%luHz/%luch/%lums",
               g_river_orvibo_protocol.session_id[0] != '\0' ?
                   g_river_orvibo_protocol.session_id :
                   "-",
               (unsigned long)g_river_orvibo_protocol.server_sample_rate,
               (unsigned long)g_river_orvibo_protocol.server_channels,
               (unsigned long)g_river_orvibo_protocol.server_frame_duration_ms);
    river_orvibo_emit_event(RIVER_ORVIBO_PROTOCOL_EVENT_SERVER_HELLO,
                            NULL,
                            NULL,
                            NULL,
                            NULL,
                            NULL,
                            0U,
                            g_river_orvibo_protocol.server_sample_rate,
                            g_river_orvibo_protocol.server_channels,
                            g_river_orvibo_protocol.server_frame_duration_ms,
                            0U);
    river_orvibo_emit_event(RIVER_ORVIBO_PROTOCOL_EVENT_AUDIO_CHANNEL_OPENED,
                            NULL,
                            NULL,
                            NULL,
                            NULL,
                            NULL,
                            0U,
                            g_river_orvibo_protocol.server_sample_rate,
                            g_river_orvibo_protocol.server_channels,
                            g_river_orvibo_protocol.server_frame_duration_ms,
                            0U);
}

static void river_orvibo_update_session_id(const cJSON *root)
{
    const cJSON *session_id_obj;

    session_id_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)root, "session_id");
    if (cJSON_IsString(session_id_obj) && session_id_obj->valuestring != NULL &&
        session_id_obj->valuestring[0] != '\0') {
        river_orvibo_copy_text(g_river_orvibo_protocol.session_id,
                               sizeof(g_river_orvibo_protocol.session_id),
                               session_id_obj->valuestring);
    }
}

static void river_orvibo_handle_mcp_message(const cJSON *root)
{
    const cJSON *payload;
    char response_json[RIVER_ORVIBO_MCP_RESPONSE_MAX];

    payload = cJSON_GetObjectItemCaseSensitive((cJSON *)root, "payload");
    if (!cJSON_IsObject(payload)) {
        river_orvibo_set_last_error("mcp_payload_invalid");
        return;
    }
    river_orvibo_emit_event(RIVER_ORVIBO_PROTOCOL_EVENT_MCP_REQUEST,
                            NULL,
                            NULL,
                            NULL,
                            payload,
                            NULL,
                            0U,
                            0U,
                            0U,
                            0U,
                            0U);
    if (river_orvibo_mcp_volume_handle(payload, response_json, sizeof(response_json)) == RIVER_OK &&
        response_json[0] != '\0') {
        (void)river_orvibo_protocol_send_mcp_message(response_json);
    }
}

static void river_orvibo_handle_text_message(const char *json_text, int json_len)
{
    cJSON *root;
    cJSON *type_obj;
    cJSON *state_obj;
    cJSON *text_obj;
    const char *type;
    const char *state = NULL;
    const char *text = NULL;

    root = cJSON_ParseWithLength(json_text, (size_t)json_len);
    if (root == NULL) {
        river_orvibo_set_last_error("json_parse_failed");
        river_orvibo_emit_event(RIVER_ORVIBO_PROTOCOL_EVENT_ERROR,
                                NULL,
                                NULL,
                                "json_parse_failed",
                                NULL,
                                NULL,
                                0U,
                                0U,
                                0U,
                                0U,
                                0U);
        return;
    }

    type_obj = cJSON_GetObjectItemCaseSensitive(root, "type");
    type = cJSON_IsString(type_obj) ? type_obj->valuestring : NULL;
    g_river_orvibo_protocol.text_rx++;
    if (type == NULL) {
        river_orvibo_set_last_error("json_missing_type");
        cJSON_Delete(root);
        return;
    }
    river_orvibo_update_session_id(root);

    if (strcmp(type, "hello") == 0) {
        river_orvibo_parse_server_hello(root);
        cJSON_Delete(root);
        return;
    }

    state_obj = cJSON_GetObjectItemCaseSensitive(root, "state");
    text_obj = cJSON_GetObjectItemCaseSensitive(root, "text");
    state = cJSON_IsString(state_obj) ? state_obj->valuestring : NULL;
    text = cJSON_IsString(text_obj) ? text_obj->valuestring : NULL;
    if (text != NULL) {
        river_orvibo_copy_text(g_river_orvibo_protocol.last_text,
                               sizeof(g_river_orvibo_protocol.last_text),
                               text);
    }

    if (strcmp(type, "tts") == 0) {
        if (state != NULL && strcmp(state, "start") == 0) {
            river_orvibo_emit_event(RIVER_ORVIBO_PROTOCOL_EVENT_TTS_START,
                                    text,
                                    state,
                                    NULL,
                                    NULL,
                                    NULL,
                                    0U,
                                    0U,
                                    0U,
                                    0U,
                                    0U);
        } else if (state != NULL && strcmp(state, "sentence_start") == 0) {
            g_river_orvibo_protocol.tts_sentence_rx++;
            river_orvibo_emit_event(RIVER_ORVIBO_PROTOCOL_EVENT_TTS_SENTENCE_START,
                                    text,
                                    state,
                                    NULL,
                                    NULL,
                                    NULL,
                                    0U,
                                    0U,
                                    0U,
                                    0U,
                                    0U);
        } else if (state != NULL && strcmp(state, "stop") == 0) {
            river_orvibo_emit_event(RIVER_ORVIBO_PROTOCOL_EVENT_TTS_STOP,
                                    text,
                                    state,
                                    NULL,
                                    NULL,
                                    NULL,
                                    0U,
                                    0U,
                                    0U,
                                    0U,
                                    0U);
        } else {
            RIVER_LOGI("tts event: state=%s text=%s",
                       state != NULL ? state : "-",
                       text != NULL ? text : "-");
        }
    } else if (strcmp(type, "stt") == 0) {
        g_river_orvibo_protocol.stt_rx++;
        RIVER_LOGI("server stt: text=%s",
                   text != NULL ? text : "-");
        river_orvibo_emit_event(RIVER_ORVIBO_PROTOCOL_EVENT_STT_TEXT,
                                text,
                                "stt",
                                NULL,
                                NULL,
                                NULL,
                                0U,
                                0U,
                                0U,
                                0U,
                                0U);
    } else if (strcmp(type, "llm") == 0) {
        const cJSON *emotion_obj = cJSON_GetObjectItemCaseSensitive(root, "emotion");
        const char *emotion =
            cJSON_IsString(emotion_obj) && emotion_obj->valuestring != NULL ?
                emotion_obj->valuestring :
                NULL;

        g_river_orvibo_protocol.llm_rx++;
        RIVER_LOGI("server llm: emotion=%s text=%s",
                   emotion != NULL ? emotion : "-",
                   text != NULL ? text : "-");
        river_orvibo_emit_event(RIVER_ORVIBO_PROTOCOL_EVENT_LLM_EMOTION,
                                text,
                                emotion,
                                NULL,
                                NULL,
                                NULL,
                                0U,
                                0U,
                                0U,
                                0U,
                                0U);
    } else if (strcmp(type, "mcp") == 0) {
        river_orvibo_handle_mcp_message(root);
    } else if (strcmp(type, "pong") == 0) {
        /* Application-level pong is optional; accept it without affecting state. */
    } else if (strcmp(type, "system") == 0) {
        const cJSON *command_obj = cJSON_GetObjectItemCaseSensitive(root, "command");
        const char *command = cJSON_IsString(command_obj) ? command_obj->valuestring : NULL;
        RIVER_LOGW("system command: %s", command != NULL ? command : "-");
        if (command != NULL && strcmp(command, "reboot") == 0) {
            river_orvibo_emit_event(RIVER_ORVIBO_PROTOCOL_EVENT_REBOOT_REQUEST,
                                    NULL,
                                    NULL,
                                    "server_reboot",
                                    NULL,
                                    NULL,
                                    0U,
                                    0U,
                                    0U,
                                    0U,
                                    0U);
        }
    } else if (strcmp(type, "alert") == 0) {
        RIVER_LOGW("server alert: state=%s text=%s",
                   state != NULL ? state : "-",
                   text != NULL ? text : "-");
    } else if (strcmp(type, "error") == 0) {
        river_orvibo_emit_event(RIVER_ORVIBO_PROTOCOL_EVENT_ERROR,
                                text,
                                state,
                                "server_error",
                                NULL,
                                NULL,
                                0U,
                                0U,
                                0U,
                                0U,
                                0U);
    } else {
        RIVER_LOGI("ignore message: type=%s state=%s", type, state != NULL ? state : "-");
    }

    cJSON_Delete(root);
}

static void river_orvibo_handle_binary_message(const uint8_t *data, size_t data_len)
{
    const uint8_t *payload = data;
    size_t payload_bytes = data_len;
    uint32_t timestamp_ms = 0U;

    if (data == NULL || data_len == 0U) {
        return;
    }
    if (g_river_orvibo_protocol.config.protocol_version == 2U &&
        data_len >= sizeof(river_orvibo_binary_v2_t)) {
        const river_orvibo_binary_v2_t *frame = (const river_orvibo_binary_v2_t *)data;
        uint32_t declared_bytes = lwip_ntohl(frame->payload_size);
        payload = frame->payload;
        payload_bytes = declared_bytes;
        timestamp_ms = lwip_ntohl(frame->timestamp);
        if (sizeof(river_orvibo_binary_v2_t) + payload_bytes > data_len) {
            river_orvibo_set_last_error("binary_v2_size_invalid");
            return;
        }
    } else if (g_river_orvibo_protocol.config.protocol_version == 3U &&
               data_len >= sizeof(river_orvibo_binary_v3_t)) {
        const river_orvibo_binary_v3_t *frame = (const river_orvibo_binary_v3_t *)data;
        payload = frame->payload;
        payload_bytes = (size_t)lwip_ntohs(frame->payload_size);
        if (sizeof(river_orvibo_binary_v3_t) + payload_bytes > data_len) {
            river_orvibo_set_last_error("binary_v3_size_invalid");
            return;
        }
    }

    g_river_orvibo_protocol.audio_rx++;
    river_orvibo_emit_event(RIVER_ORVIBO_PROTOCOL_EVENT_AUDIO_PACKET,
                            NULL,
                            NULL,
                            NULL,
                            NULL,
                            payload,
                            payload_bytes,
                            g_river_orvibo_protocol.server_sample_rate,
                            g_river_orvibo_protocol.server_channels,
                            g_river_orvibo_protocol.server_frame_duration_ms,
                            timestamp_ms);
}

static bool river_orvibo_payload_looks_like_json(const uint8_t *data, size_t data_len)
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

static bool river_orvibo_payload_valid_json(const uint8_t *data, size_t data_len)
{
    cJSON *root;

    if (!river_orvibo_payload_looks_like_json(data, data_len)) {
        return false;
    }
    root = cJSON_ParseWithLength((const char *)data, data_len);
    if (root == NULL) {
        return false;
    }
    cJSON_Delete(root);
    return true;
}

static void river_orvibo_ws_message_cb(wsclient_context **wsclient,
                                       int data_len,
                                       enum opcode_type opcode,
                                       void *user_data)
{
    const uint8_t *data;

    (void)wsclient;
    (void)user_data;
    if (g_river_orvibo_protocol.wsclient == NULL ||
        g_river_orvibo_protocol.wsclient->receivedData == NULL ||
        data_len <= 0) {
        return;
    }
    g_river_orvibo_protocol.last_incoming_ms =
        (uint32_t)rtos_time_get_current_system_time_ms();
    data = (const uint8_t *)g_river_orvibo_protocol.wsclient->receivedData;
    if (opcode == BINARY_FRAME) {
        river_orvibo_handle_binary_message(data, (size_t)data_len);
    } else if (opcode == CONTINUATION &&
               !river_orvibo_payload_valid_json(data, (size_t)data_len)) {
        river_orvibo_handle_binary_message(data, (size_t)data_len);
    } else {
        river_orvibo_handle_text_message((const char *)data, data_len);
    }
}

static void river_orvibo_ws_close_cb(wsclient_context *wsclient, void *user_data)
{
    (void)wsclient;
    (void)user_data;
    g_river_orvibo_protocol.ws_closed = true;
    g_river_orvibo_protocol.session_open = false;
    g_river_orvibo_protocol.server_hello_received = false;
    g_river_orvibo_protocol.last_incoming_ms = 0U;
    g_river_orvibo_protocol.close_events++;
    g_river_orvibo_protocol.sessions_closed++;
    river_orvibo_set_last_error("transport_closed");
    RIVER_LOGI("websocket closed: sid=%s",
               g_river_orvibo_protocol.session_id[0] != '\0' ?
                   g_river_orvibo_protocol.session_id :
                   "-");
    river_orvibo_emit_event(RIVER_ORVIBO_PROTOCOL_EVENT_AUDIO_CHANNEL_CLOSED,
                            NULL,
                            NULL,
                            "transport_closed",
                            NULL,
                            NULL,
                            0U,
                            0U,
                            0U,
                            0U,
                            0U);
}

static bool river_orvibo_close_context(void)
{
    bool locked;
    bool was_open = false;

    locked = river_orvibo_transport_lock();
    if (locked && g_river_orvibo_protocol.wsclient != NULL) {
        was_open = g_river_orvibo_protocol.session_open ||
                   g_river_orvibo_protocol.wsclient->readyState == WSC_OPEN;
        river_ws_dispatch_unregister(g_river_orvibo_protocol.wsclient);
        if (g_river_orvibo_protocol.wsclient->readyState == WSC_OPEN) {
            ws_close(&g_river_orvibo_protocol.wsclient);
        }
        if (g_river_orvibo_protocol.wsclient->readyState == WSC_OPEN ||
            g_river_orvibo_protocol.wsclient->readyState == WSC_CONNECTING ||
            g_river_orvibo_protocol.wsclient->readyState == WSC_CLOSING) {
            g_river_orvibo_protocol.wsclient->fun_ops.client_close(g_river_orvibo_protocol.wsclient);
        }
        ws_free(g_river_orvibo_protocol.wsclient);
        g_river_orvibo_protocol.wsclient = NULL;
    }
    g_river_orvibo_protocol.session_open = false;
    g_river_orvibo_protocol.server_hello_received = false;
    g_river_orvibo_protocol.last_incoming_ms = 0U;
    g_river_orvibo_protocol.session_epoch++;
    river_orvibo_transport_unlock(locked);
    return was_open;
}

static river_status_t river_orvibo_poll_once(uint32_t timeout_ms)
{
    bool locked;
    bool closed;

    locked = river_orvibo_transport_lock();
    if (!locked) {
        return RIVER_ERR_BUSY;
    }
    if (g_river_orvibo_protocol.wsclient == NULL) {
        river_orvibo_transport_unlock(locked);
        return RIVER_ERR_INVALID_STATE;
    }

    ws_poll((int)timeout_ms, &g_river_orvibo_protocol.wsclient);
    g_river_orvibo_protocol.poll_cycles++;
    closed = g_river_orvibo_protocol.ws_closed;
    river_orvibo_transport_unlock(locked);

    return closed ? RIVER_ERR_IO : RIVER_OK;
}

static bool river_orvibo_server_hello_channel_closed(void)
{
    bool locked;
    bool closed = true;

    locked = river_orvibo_transport_lock();
    if (locked) {
        closed = g_river_orvibo_protocol.wsclient == NULL ||
                 g_river_orvibo_protocol.wsclient->readyState != WSC_OPEN ||
                 g_river_orvibo_protocol.ws_closed;
    }
    river_orvibo_transport_unlock(locked);
    return closed;
}

static river_status_t river_orvibo_wait_server_hello(void)
{
    uint32_t start_ms = (uint32_t)rtos_time_get_current_system_time_ms();

    while (!g_river_orvibo_protocol.server_hello_received) {
        uint32_t now_ms;
        river_status_t status;

        if (river_orvibo_server_hello_channel_closed()) {
            river_orvibo_set_last_error("server_hello_channel_closed");
            return RIVER_ERR_IO;
        }
        if (g_river_orvibo_protocol.server_hello_rejected) {
            river_orvibo_set_last_error("server_hello_rejected");
            return RIVER_ERR_IO;
        }

        status = river_orvibo_poll_once(RIVER_ORVIBO_WS_HELLO_POLL_MS);
        if (status != RIVER_OK) {
            river_orvibo_set_last_error("server_hello_poll_failed");
            return status == RIVER_ERR_INVALID_STATE ? RIVER_ERR_IO : status;
        }
        now_ms = (uint32_t)rtos_time_get_current_system_time_ms();
        if (now_ms - start_ms >= RIVER_ORVIBO_WS_HELLO_TIMEOUT_MS) {
            river_orvibo_set_last_error("server_hello_timeout");
            return RIVER_ERR_IO;
        }
    }

    return RIVER_OK;
}

static bool river_orvibo_channel_timeout_reached(uint32_t now_ms, uint32_t *age_ms_out)
{
    bool locked;
    bool timed_out = false;
    uint32_t age_ms = 0U;

    locked = river_orvibo_transport_lock();
    if (locked &&
        river_orvibo_channel_open_locked() &&
        g_river_orvibo_protocol.last_incoming_ms != 0U) {
        age_ms = now_ms - g_river_orvibo_protocol.last_incoming_ms;
        timed_out = age_ms >= RIVER_ORVIBO_WS_CHANNEL_TIMEOUT_MS;
    }
    river_orvibo_transport_unlock(locked);
    if (age_ms_out != NULL) {
        *age_ms_out = age_ms;
    }
    return timed_out;
}

static river_status_t river_orvibo_close_channel_on_timeout(void)
{
    uint32_t now_ms = (uint32_t)rtos_time_get_current_system_time_ms();
    uint32_t age_ms = 0U;
    bool was_open;

    if (!river_orvibo_channel_timeout_reached(now_ms, &age_ms)) {
        return RIVER_OK;
    }

    g_river_orvibo_protocol.channel_timeouts++;
    river_orvibo_set_last_error("channel_timeout");
    RIVER_LOGE("channel timeout: age=%lums limit=%ums sid=%s",
               (unsigned long)age_ms,
               (unsigned int)RIVER_ORVIBO_WS_CHANNEL_TIMEOUT_MS,
               g_river_orvibo_protocol.session_id[0] != '\0' ?
                   g_river_orvibo_protocol.session_id :
                   "-");
    was_open = river_orvibo_close_context();
    if (was_open) {
        g_river_orvibo_protocol.sessions_closed++;
        river_orvibo_emit_event(RIVER_ORVIBO_PROTOCOL_EVENT_AUDIO_CHANNEL_CLOSED,
                                NULL,
                                NULL,
                                "channel_timeout",
                                NULL,
                                NULL,
                                0U,
                                0U,
                                0U,
                                0U,
                                0U);
    }
    return RIVER_ERR_IO;
}

river_status_t river_orvibo_protocol_init(void)
{
    if (g_river_orvibo_protocol.initialized) {
        return RIVER_OK;
    }
    memset(&g_river_orvibo_protocol, 0, sizeof(g_river_orvibo_protocol));
    if (rtos_mutex_recursive_create(&g_river_orvibo_protocol.lock) != RIVER_ORVIBO_RTOS_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    if (rtos_queue_create(&g_river_orvibo_protocol.uplink_queue,
                          RIVER_ORVIBO_UPLINK_QUEUE_DEPTH,
                          sizeof(river_orvibo_uplink_frame_t)) != RIVER_ORVIBO_RTOS_OK) {
        rtos_mutex_recursive_delete(g_river_orvibo_protocol.lock);
        g_river_orvibo_protocol.lock = NULL;
        return RIVER_ERR_NO_MEMORY;
    }
    if (rtos_task_create(&g_river_orvibo_protocol.uplink_task,
                         "orvibo_uplink",
                         river_orvibo_uplink_task,
                         NULL,
                         RIVER_ORVIBO_UPLINK_TASK_STACK,
                         RIVER_ORVIBO_UPLINK_TASK_PRIORITY) != RIVER_ORVIBO_RTOS_OK) {
        rtos_queue_delete(g_river_orvibo_protocol.uplink_queue);
        g_river_orvibo_protocol.uplink_queue = NULL;
        rtos_mutex_recursive_delete(g_river_orvibo_protocol.lock);
        g_river_orvibo_protocol.lock = NULL;
        return RIVER_ERR_NO_MEMORY;
    }
    g_river_orvibo_protocol.initialized = true;
    river_orvibo_copy_text(g_river_orvibo_protocol.url,
                           sizeof(g_river_orvibo_protocol.url),
                           RIVER_ORVIBO_WS_URL);
    river_orvibo_copy_text(g_river_orvibo_protocol.token,
                           sizeof(g_river_orvibo_protocol.token),
                           RIVER_ORVIBO_WS_TOKEN);
    river_orvibo_copy_text(g_river_orvibo_protocol.websocket_subprotocol,
                           sizeof(g_river_orvibo_protocol.websocket_subprotocol),
                           RIVER_ORVIBO_WS_SUBPROTOCOL);
    g_river_orvibo_protocol.config.url = g_river_orvibo_protocol.url;
    g_river_orvibo_protocol.config.token = g_river_orvibo_protocol.token;
    g_river_orvibo_protocol.config.websocket_subprotocol =
        g_river_orvibo_protocol.websocket_subprotocol;
    g_river_orvibo_protocol.config.protocol_version = RIVER_ORVIBO_PROTOCOL_VERSION;
    g_river_orvibo_protocol.config.enable_mcp = RIVER_ORVIBO_ENABLE_MCP != 0;
    g_river_orvibo_protocol.config.uplink_sample_rate = RIVER_ORVIBO_AUDIO_SAMPLE_RATE;
    g_river_orvibo_protocol.config.uplink_channels = RIVER_ORVIBO_AUDIO_CHANNELS;
    g_river_orvibo_protocol.config.uplink_frame_duration_ms =
        RIVER_ORVIBO_AUDIO_FRAME_DURATION_MS;
    g_river_orvibo_protocol.server_sample_rate = RIVER_ORVIBO_AUDIO_SAMPLE_RATE;
    g_river_orvibo_protocol.server_channels = RIVER_ORVIBO_AUDIO_CHANNELS;
    g_river_orvibo_protocol.server_frame_duration_ms = RIVER_ORVIBO_AUDIO_FRAME_DURATION_MS;
    return RIVER_OK;
}

river_status_t river_orvibo_protocol_get_config(river_orvibo_protocol_config_t *config)
{
    if (config == NULL) {
        return RIVER_ERR_ARG;
    }
    if (river_orvibo_protocol_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    *config = g_river_orvibo_protocol.config;
    config->url = g_river_orvibo_protocol.url;
    config->token = g_river_orvibo_protocol.token;
    config->websocket_subprotocol = g_river_orvibo_protocol.websocket_subprotocol;
    return RIVER_OK;
}

river_status_t river_orvibo_protocol_set_config(const river_orvibo_protocol_config_t *config)
{
    uint16_t protocol_version;

    if (config == NULL) {
        return RIVER_ERR_ARG;
    }
    if (river_orvibo_protocol_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    protocol_version = config->protocol_version != 0U ?
                           config->protocol_version :
                           RIVER_ORVIBO_PROTOCOL_VERSION;
    if (!river_orvibo_protocol_version_supported(protocol_version)) {
        return RIVER_ERR_ARG;
    }
    if (config->url != NULL) {
        river_orvibo_copy_text(g_river_orvibo_protocol.url,
                               sizeof(g_river_orvibo_protocol.url),
                               config->url);
    }
    if (config->token != NULL) {
        river_orvibo_copy_text(g_river_orvibo_protocol.token,
                               sizeof(g_river_orvibo_protocol.token),
                               config->token);
    }
    if (config->websocket_subprotocol != NULL) {
        river_orvibo_copy_text(g_river_orvibo_protocol.websocket_subprotocol,
                               sizeof(g_river_orvibo_protocol.websocket_subprotocol),
                               config->websocket_subprotocol);
    }
    g_river_orvibo_protocol.config = *config;
    g_river_orvibo_protocol.config.url = g_river_orvibo_protocol.url;
    g_river_orvibo_protocol.config.token = g_river_orvibo_protocol.token;
    g_river_orvibo_protocol.config.websocket_subprotocol =
        g_river_orvibo_protocol.websocket_subprotocol;
    g_river_orvibo_protocol.config.protocol_version = protocol_version;
    if (g_river_orvibo_protocol.config.uplink_sample_rate == 0U) {
        g_river_orvibo_protocol.config.uplink_sample_rate = RIVER_ORVIBO_AUDIO_SAMPLE_RATE;
    }
    if (g_river_orvibo_protocol.config.uplink_channels == 0U) {
        g_river_orvibo_protocol.config.uplink_channels = RIVER_ORVIBO_AUDIO_CHANNELS;
    }
    if (g_river_orvibo_protocol.config.uplink_frame_duration_ms == 0U) {
        g_river_orvibo_protocol.config.uplink_frame_duration_ms =
            RIVER_ORVIBO_AUDIO_FRAME_DURATION_MS;
    }
    return RIVER_OK;
}

river_status_t river_orvibo_protocol_set_event_handler(
    river_orvibo_protocol_event_handler_t handler,
    void *user_data)
{
    if (river_orvibo_protocol_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    g_river_orvibo_protocol.event_handler = handler;
    g_river_orvibo_protocol.event_handler_user = user_data;
    return RIVER_OK;
}

river_status_t river_orvibo_protocol_open_audio_channel(void)
{
    int port;
    river_status_t status;

    if (river_orvibo_protocol_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    if (!river_wifi_station_is_connected()) {
        river_orvibo_set_last_error("wifi_not_connected");
        return RIVER_ERR_BUSY;
    }
    if (!river_orvibo_access_ready()) {
        river_orvibo_set_last_error("access_not_ready");
        return RIVER_ERR_BUSY;
    }
    if (g_river_orvibo_protocol.session_open &&
        g_river_orvibo_protocol.wsclient != NULL &&
        g_river_orvibo_protocol.wsclient->readyState == WSC_OPEN) {
        return RIVER_OK;
    }

    river_orvibo_close_context();
    g_river_orvibo_protocol.session_id[0] = '\0';
    g_river_orvibo_protocol.last_error[0] = '\0';
    memset(g_river_orvibo_protocol.base_url, 0, sizeof(g_river_orvibo_protocol.base_url));
    memset(g_river_orvibo_protocol.path, 0, sizeof(g_river_orvibo_protocol.path));
    status = river_orvibo_parse_url(g_river_orvibo_protocol.url,
                                    g_river_orvibo_protocol.base_url,
                                    sizeof(g_river_orvibo_protocol.base_url),
                                    &port,
                                    g_river_orvibo_protocol.path,
                                    sizeof(g_river_orvibo_protocol.path));
    if (status != RIVER_OK) {
        river_orvibo_set_last_error("url_parse_failed");
        return status;
    }
    status = river_orvibo_build_headers();
    if (status != RIVER_OK) {
        river_orvibo_set_last_error("headers_build_failed");
        return status;
    }
    status = river_ws_dispatch_init();
    if (status != RIVER_OK) {
        river_orvibo_set_last_error("ws_dispatch_init_failed");
        return status;
    }

    g_river_orvibo_protocol.wsclient =
        create_wsclient(g_river_orvibo_protocol.base_url,
                        port,
                        g_river_orvibo_protocol.path,
                        NULL,
                        RIVER_ORVIBO_WS_TX_MAX,
                        RIVER_ORVIBO_WS_RX_MAX,
                        RIVER_ORVIBO_WS_QUEUE_MAX);
    if (g_river_orvibo_protocol.wsclient == NULL) {
        river_orvibo_set_last_error("wsclient_create_failed");
        return RIVER_ERR_NO_MEMORY;
    }
    if (g_river_orvibo_protocol.websocket_subprotocol[0] != '\0' &&
        ws_handshake_header_set_protocol(
            g_river_orvibo_protocol.wsclient,
            g_river_orvibo_protocol.websocket_subprotocol,
            (int)(strlen(g_river_orvibo_protocol.websocket_subprotocol) + 1U)) != 0) {
        river_orvibo_set_last_error("subprotocol_set_failed");
        river_orvibo_close_context();
        return RIVER_ERR_IO;
    }
    if (ws_handshake_set_header_fields(g_river_orvibo_protocol.wsclient,
                                       g_river_orvibo_protocol.headers,
                                       (int)(strlen(g_river_orvibo_protocol.headers) + 1U)) != 0) {
        river_orvibo_set_last_error("headers_set_failed");
        river_orvibo_close_context();
        return RIVER_ERR_IO;
    }
    if (river_ws_dispatch_register(g_river_orvibo_protocol.wsclient,
                                   river_orvibo_ws_message_cb,
                                   river_orvibo_ws_close_cb,
                                   NULL) != RIVER_OK) {
        river_orvibo_set_last_error("ws_dispatch_register_failed");
        river_orvibo_close_context();
        return RIVER_ERR_BUSY;
    }
    ws_setsockopt_timeout(RIVER_ORVIBO_WS_RECV_TIMEOUT_MS,
                          RIVER_ORVIBO_WS_SEND_TIMEOUT_MS,
                          RIVER_ORVIBO_WS_CONNECT_TIMEOUT_MS);
    ws_set_senddata_block_time(RIVER_ORVIBO_WS_SEND_BLOCK_MS);
    ws_multisend_opts(g_river_orvibo_protocol.wsclient, RIVER_ORVIBO_WS_STABLE_BUF_NUM);
    RIVER_LOGI("connecting: url=%s base=%s path=%s protocol=%u ws_subprotocol=%s device_id=%s client_id=%s",
               g_river_orvibo_protocol.url,
               g_river_orvibo_protocol.base_url,
               g_river_orvibo_protocol.path,
               (unsigned int)g_river_orvibo_protocol.config.protocol_version,
               g_river_orvibo_protocol.websocket_subprotocol[0] != '\0' ?
                   g_river_orvibo_protocol.websocket_subprotocol :
                   "-",
               g_river_orvibo_protocol.device_id,
               g_river_orvibo_protocol.client_id);
    if (ws_connect_url(g_river_orvibo_protocol.wsclient) < 0) {
        river_orvibo_set_last_error("ws_connect_failed");
        river_orvibo_close_context();
        return RIVER_ERR_IO;
    }
    g_river_orvibo_protocol.session_open = true;
    g_river_orvibo_protocol.ws_closed = false;
    g_river_orvibo_protocol.server_hello_received = false;
    g_river_orvibo_protocol.server_hello_rejected = false;
    g_river_orvibo_protocol.last_incoming_ms =
        (uint32_t)rtos_time_get_current_system_time_ms();
    g_river_orvibo_protocol.session_epoch++;
    g_river_orvibo_protocol.sessions_opened++;
    river_orvibo_emit_event(RIVER_ORVIBO_PROTOCOL_EVENT_CONNECTED,
                            NULL,
                            NULL,
                            NULL,
                            NULL,
                            NULL,
                            0U,
                            0U,
                            0U,
                            0U,
                            0U);
    status = river_orvibo_send_hello();
    if (status != RIVER_OK) {
        river_orvibo_set_last_error("hello_send_failed");
        river_orvibo_close_context();
        return status;
    }
    status = river_orvibo_wait_server_hello();
    if (status != RIVER_OK) {
        river_orvibo_close_context();
        return status;
    }
    return RIVER_OK;
}

void river_orvibo_protocol_close_audio_channel(void)
{
    river_orvibo_close_context();
}

bool river_orvibo_protocol_audio_channel_open(void)
{
    uint32_t now_ms = (uint32_t)rtos_time_get_current_system_time_ms();

    if (river_orvibo_channel_timeout_reached(now_ms, NULL)) {
        return false;
    }
    return g_river_orvibo_protocol.session_open &&
           g_river_orvibo_protocol.wsclient != NULL &&
           g_river_orvibo_protocol.wsclient->readyState == WSC_OPEN;
}

river_status_t river_orvibo_protocol_poll(uint32_t timeout_ms)
{
    uint32_t remaining_ms;
    river_status_t last_status = RIVER_OK;

    if (!g_river_orvibo_protocol.initialized || g_river_orvibo_protocol.wsclient == NULL) {
        return RIVER_OK;
    }
    remaining_ms = timeout_ms == 0U ? 1U : timeout_ms;
    while (remaining_ms > 0U && g_river_orvibo_protocol.wsclient != NULL) {
        uint32_t slice_ms = remaining_ms > RIVER_ORVIBO_POLL_LOCK_SLICE_MS ?
                                RIVER_ORVIBO_POLL_LOCK_SLICE_MS :
                                remaining_ms;
        last_status = river_orvibo_poll_once(slice_ms);
        if (last_status == RIVER_ERR_INVALID_STATE) {
            return RIVER_OK;
        }
        if (last_status != RIVER_OK) {
            break;
        }
        if (remaining_ms <= slice_ms) {
            break;
        }
        remaining_ms -= slice_ms;
    }
    if (last_status == RIVER_OK) {
        last_status = river_orvibo_close_channel_on_timeout();
    }
    return last_status;
}

static bool river_orvibo_channel_open_locked(void)
{
    return g_river_orvibo_protocol.session_open &&
           g_river_orvibo_protocol.wsclient != NULL &&
           g_river_orvibo_protocol.wsclient->readyState == WSC_OPEN;
}

static river_status_t river_orvibo_send_audio_immediate(const uint8_t *payload,
                                                        size_t bytes,
                                                        uint32_t timestamp_ms,
                                                        uint32_t session_epoch)
{
    const uint8_t *send_payload = payload;
    size_t send_bytes = bytes;
    bool locked;
    river_status_t status = RIVER_ERR_IO;

    if (payload == NULL || bytes == 0U) {
        return RIVER_ERR_ARG;
    }
    locked = river_orvibo_transport_lock();
    if (!locked || !river_orvibo_channel_open_locked()) {
        status = RIVER_ERR_BUSY;
        goto exit;
    }
    if (g_river_orvibo_protocol.session_epoch != session_epoch) {
        status = RIVER_ERR_INVALID_STATE;
        goto exit;
    }
    if (g_river_orvibo_protocol.config.protocol_version == 2U) {
        river_orvibo_binary_v2_t *frame;
        if (bytes > RIVER_ORVIBO_BINARY_PAYLOAD_MAX) {
            status = RIVER_ERR_ARG;
            goto exit;
        }
        frame = (river_orvibo_binary_v2_t *)g_river_orvibo_protocol.binary_frame;
        frame->version = lwip_htons(2U);
        frame->type = lwip_htons(0U);
        frame->reserved = 0U;
        frame->timestamp = lwip_htonl(timestamp_ms);
        frame->payload_size = lwip_htonl((uint32_t)bytes);
        memcpy(frame->payload, payload, bytes);
        send_payload = g_river_orvibo_protocol.binary_frame;
        send_bytes = sizeof(*frame) + bytes;
    } else if (g_river_orvibo_protocol.config.protocol_version == 3U) {
        river_orvibo_binary_v3_t *frame;
        if (bytes > RIVER_ORVIBO_BINARY_PAYLOAD_MAX) {
            status = RIVER_ERR_ARG;
            goto exit;
        }
        frame = (river_orvibo_binary_v3_t *)g_river_orvibo_protocol.binary_frame;
        frame->type = 0U;
        frame->reserved = 0U;
        frame->payload_size = lwip_htons((uint16_t)bytes);
        memcpy(frame->payload, payload, bytes);
        send_payload = g_river_orvibo_protocol.binary_frame;
        send_bytes = sizeof(*frame) + bytes;
    }
    if (ws_sendBinary((uint8_t *)send_payload,
                      (int)send_bytes,
                      1,
                      g_river_orvibo_protocol.wsclient) == 0) {
        g_river_orvibo_protocol.audio_tx++;
        status = RIVER_OK;
    } else {
        status = RIVER_ERR_BUSY;
    }

exit:
    river_orvibo_transport_unlock(locked);
    return status;
}

river_status_t river_orvibo_protocol_send_audio(const uint8_t *payload,
                                                size_t bytes,
                                                uint32_t timestamp_ms)
{
    river_orvibo_uplink_frame_t frame;
    bool locked;
    bool channel_open = false;
    uint32_t session_epoch = 0U;

    if (payload == NULL || bytes == 0U || bytes > sizeof(frame.payload)) {
        return RIVER_ERR_ARG;
    }
    if (!g_river_orvibo_protocol.initialized || g_river_orvibo_protocol.uplink_queue == NULL) {
        return RIVER_ERR_INVALID_STATE;
    }

    locked = river_orvibo_transport_lock();
    if (locked) {
        channel_open = river_orvibo_channel_open_locked();
        session_epoch = g_river_orvibo_protocol.session_epoch;
    }
    river_orvibo_transport_unlock(locked);
    if (!channel_open) {
        g_river_orvibo_protocol.audio_drop_closed++;
        return RIVER_ERR_BUSY;
    }

    memset(&frame, 0, sizeof(frame));
    frame.session_epoch = session_epoch;
    frame.timestamp_ms = timestamp_ms;
    frame.bytes = bytes;
    memcpy(frame.payload, payload, bytes);
    if (rtos_queue_send(g_river_orvibo_protocol.uplink_queue, &frame, 0U) ==
        RIVER_ORVIBO_RTOS_OK) {
        g_river_orvibo_protocol.audio_enqueued++;
        return RIVER_OK;
    }

    if (rtos_queue_receive(g_river_orvibo_protocol.uplink_queue, &frame, 0U) ==
        RIVER_ORVIBO_RTOS_OK) {
        river_orvibo_uplink_frame_t replacement;

        g_river_orvibo_protocol.audio_queue_drop_oldest++;
        memset(&replacement, 0, sizeof(replacement));
        replacement.session_epoch = session_epoch;
        replacement.timestamp_ms = timestamp_ms;
        replacement.bytes = bytes;
        memcpy(replacement.payload, payload, bytes);
        if (rtos_queue_send(g_river_orvibo_protocol.uplink_queue, &replacement, 0U) ==
            RIVER_ORVIBO_RTOS_OK) {
            g_river_orvibo_protocol.audio_enqueued++;
            return RIVER_OK;
        }
    }

    g_river_orvibo_protocol.audio_queue_full++;
    return RIVER_ERR_BUSY;
}

static void river_orvibo_uplink_task(void *param)
{
    river_orvibo_uplink_frame_t frame;

    (void)param;
    g_river_orvibo_protocol.uplink_task_running = true;
    while (true) {
        bool channel_open = false;
        uint32_t session_epoch = 0U;
        bool locked;
        river_status_t status = RIVER_ERR_BUSY;
        uint32_t attempt;

        if (rtos_queue_receive(g_river_orvibo_protocol.uplink_queue,
                               &frame,
                               0xFFFFFFFFU) != RIVER_ORVIBO_RTOS_OK) {
            continue;
        }

        locked = river_orvibo_transport_lock();
        if (locked) {
            channel_open = river_orvibo_channel_open_locked();
            session_epoch = g_river_orvibo_protocol.session_epoch;
        }
        river_orvibo_transport_unlock(locked);

        if (!channel_open) {
            g_river_orvibo_protocol.audio_drop_closed++;
            continue;
        }
        if (frame.session_epoch != session_epoch) {
            g_river_orvibo_protocol.audio_drop_stale++;
            continue;
        }

        for (attempt = 0U; attempt < RIVER_ORVIBO_UPLINK_RETRY_COUNT; ++attempt) {
            status = river_orvibo_send_audio_immediate(frame.payload,
                                                       frame.bytes,
                                                       frame.timestamp_ms,
                                                       frame.session_epoch);
            if (status == RIVER_OK) {
                break;
            }
            if (status == RIVER_ERR_INVALID_STATE) {
                g_river_orvibo_protocol.audio_drop_stale++;
                break;
            }
            if ((attempt + 1U) < RIVER_ORVIBO_UPLINK_RETRY_COUNT) {
                g_river_orvibo_protocol.audio_send_retry++;
                rtos_time_delay_ms(RIVER_ORVIBO_UPLINK_RETRY_DELAY_MS);
            }
        }
        if (status != RIVER_OK && status != RIVER_ERR_INVALID_STATE) {
            g_river_orvibo_protocol.audio_send_fail++;
            river_orvibo_set_last_error("audio_send_failed");
        }
    }
}

static void river_orvibo_add_session_id(cJSON *root)
{
    if (root != NULL && g_river_orvibo_protocol.session_id[0] != '\0') {
        cJSON_AddStringToObject(root, "session_id", g_river_orvibo_protocol.session_id);
    }
}

river_status_t river_orvibo_protocol_send_wake_word_detected(const char *text)
{
    cJSON *root = cJSON_CreateObject();

    if (root == NULL) {
        return RIVER_ERR_NO_MEMORY;
    }
    river_orvibo_add_session_id(root);
    cJSON_AddStringToObject(root, "type", "listen");
    cJSON_AddStringToObject(root, "state", "detect");
    cJSON_AddStringToObject(root, "text", text != NULL && text[0] != '\0' ? text : "你好小智");
    return river_orvibo_send_json_root(root);
}

river_status_t river_orvibo_protocol_send_start_listening(const char *mode)
{
    cJSON *root = cJSON_CreateObject();

    if (root == NULL) {
        return RIVER_ERR_NO_MEMORY;
    }
    river_orvibo_add_session_id(root);
    cJSON_AddStringToObject(root, "type", "listen");
    cJSON_AddStringToObject(root, "state", "start");
    cJSON_AddStringToObject(root, "mode", mode != NULL ? mode : "auto");
    return river_orvibo_send_json_root(root);
}

river_status_t river_orvibo_protocol_send_stop_listening(void)
{
    cJSON *root = cJSON_CreateObject();

    if (root == NULL) {
        return RIVER_ERR_NO_MEMORY;
    }
    river_orvibo_add_session_id(root);
    cJSON_AddStringToObject(root, "type", "listen");
    cJSON_AddStringToObject(root, "state", "stop");
    return river_orvibo_send_json_root(root);
}

river_status_t river_orvibo_protocol_send_abort_speaking(const char *reason)
{
    cJSON *root = cJSON_CreateObject();

    if (root == NULL) {
        return RIVER_ERR_NO_MEMORY;
    }
    river_orvibo_add_session_id(root);
    cJSON_AddStringToObject(root, "type", "abort");
    if (reason != NULL && reason[0] != '\0') {
        cJSON_AddStringToObject(root, "reason", reason);
    }
    return river_orvibo_send_json_root(root);
}

river_status_t river_orvibo_protocol_send_mcp_message(const char *payload_json)
{
    cJSON *root;
    cJSON *payload;

    if (payload_json == NULL || payload_json[0] == '\0') {
        return RIVER_ERR_ARG;
    }
    payload = cJSON_Parse(payload_json);
    if (payload == NULL) {
        return RIVER_ERR_ARG;
    }
    root = cJSON_CreateObject();
    if (root == NULL) {
        cJSON_Delete(payload);
        return RIVER_ERR_NO_MEMORY;
    }
    river_orvibo_add_session_id(root);
    cJSON_AddStringToObject(root, "type", "mcp");
    cJSON_AddItemToObject(root, "payload", payload);
    return river_orvibo_send_json_root(root);
}

void river_orvibo_protocol_dump_status(void)
{
    uint32_t uplink_depth = g_river_orvibo_protocol.uplink_queue != NULL ?
                                rtos_queue_message_waiting(
                                    g_river_orvibo_protocol.uplink_queue) :
                                0U;
    uint32_t now_ms = (uint32_t)rtos_time_get_current_system_time_ms();
    uint32_t incoming_age_ms = g_river_orvibo_protocol.last_incoming_ms != 0U ?
                                   now_ms - g_river_orvibo_protocol.last_incoming_ms :
                                   0U;

    RIVER_LOGI("orvibo protocol: open=%s hello=%s sid=%s url=%s proto=%u ws_subprotocol=%s payload_max=%u text=%lu/%lu audio=%lu/%lu uplink_task=%s q=%lu/%u enq=%lu drop_oldest=%lu full=%lu closed=%lu stale=%lu retry=%lu fail=%lu poll=%lu close_evt=%lu timeout=%lu incoming_age=%lums/%ums sessions=%lu/%lu errors=%lu last_error=%s server_audio=%luHz/%luch/%lums last_text=%s",
               river_orvibo_protocol_audio_channel_open() ? "yes" : "no",
               g_river_orvibo_protocol.server_hello_received ? "yes" : "no",
               g_river_orvibo_protocol.session_id[0] != '\0' ?
                   g_river_orvibo_protocol.session_id :
                   "-",
               g_river_orvibo_protocol.url,
               (unsigned int)g_river_orvibo_protocol.config.protocol_version,
               g_river_orvibo_protocol.websocket_subprotocol[0] != '\0' ?
                   g_river_orvibo_protocol.websocket_subprotocol :
                   "-",
               (unsigned int)RIVER_ORVIBO_BINARY_PAYLOAD_MAX,
               (unsigned long)g_river_orvibo_protocol.text_tx,
               (unsigned long)g_river_orvibo_protocol.text_rx,
               (unsigned long)g_river_orvibo_protocol.audio_tx,
               (unsigned long)g_river_orvibo_protocol.audio_rx,
               g_river_orvibo_protocol.uplink_task_running ? "yes" : "no",
               (unsigned long)uplink_depth,
               (unsigned int)RIVER_ORVIBO_UPLINK_QUEUE_DEPTH,
               (unsigned long)g_river_orvibo_protocol.audio_enqueued,
               (unsigned long)g_river_orvibo_protocol.audio_queue_drop_oldest,
               (unsigned long)g_river_orvibo_protocol.audio_queue_full,
               (unsigned long)g_river_orvibo_protocol.audio_drop_closed,
               (unsigned long)g_river_orvibo_protocol.audio_drop_stale,
               (unsigned long)g_river_orvibo_protocol.audio_send_retry,
               (unsigned long)g_river_orvibo_protocol.audio_send_fail,
               (unsigned long)g_river_orvibo_protocol.poll_cycles,
               (unsigned long)g_river_orvibo_protocol.close_events,
               (unsigned long)g_river_orvibo_protocol.channel_timeouts,
               (unsigned long)incoming_age_ms,
               (unsigned int)RIVER_ORVIBO_WS_CHANNEL_TIMEOUT_MS,
               (unsigned long)g_river_orvibo_protocol.sessions_opened,
               (unsigned long)g_river_orvibo_protocol.sessions_closed,
               (unsigned long)g_river_orvibo_protocol.errors,
               g_river_orvibo_protocol.last_error[0] != '\0' ?
                   g_river_orvibo_protocol.last_error :
                   "-",
               (unsigned long)g_river_orvibo_protocol.server_sample_rate,
               (unsigned long)g_river_orvibo_protocol.server_channels,
               (unsigned long)g_river_orvibo_protocol.server_frame_duration_ms,
               g_river_orvibo_protocol.last_text[0] != '\0' ?
                   g_river_orvibo_protocol.last_text :
                   "-");
}
