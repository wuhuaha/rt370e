/* Orvibo server access: XiaoZhi-compatible OTA/activation and websocket config. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "httpc/httpc.h"
#include "lwip_netconf.h"
#include "os_wrapper.h"
#include "os_wrapper_memory.h"

#include "river/river_log.h"
#include "river/river_orvibo_access.h"
#include "river/river_orvibo_credentials.h"
#include "river/river_orvibo_protocol.h"
#include "river/river_wifi_station.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.orvibo.access"

#define RIVER_ORVIBO_ACCESS_URL_MAX             192U
#define RIVER_ORVIBO_ACCESS_HOST_MAX            96U
#define RIVER_ORVIBO_ACCESS_PATH_MAX            160U
#define RIVER_ORVIBO_ACCESS_TOKEN_MAX           256U
#define RIVER_ORVIBO_ACCESS_DEVICE_ID_MAX       32U
#define RIVER_ORVIBO_ACCESS_CLIENT_ID_MAX       48U
#define RIVER_ORVIBO_ACCESS_CODE_MAX            32U
#define RIVER_ORVIBO_ACCESS_MESSAGE_MAX         160U
#define RIVER_ORVIBO_ACCESS_ERROR_MAX           96U
#define RIVER_ORVIBO_ACCESS_HTTP_BODY_MAX       8192U
#define RIVER_ORVIBO_ACCESS_HTTP_TIMEOUT_SEC    12U
#define RIVER_ORVIBO_ACCESS_MAX_CHECK_ROUNDS    4U
#define RIVER_ORVIBO_ACCESS_ACTIVATE_RETRIES    10U
#define RIVER_ORVIBO_ACCESS_ACTIVATE_WAIT_MS    3000U

typedef struct {
    bool secure;
    char host[RIVER_ORVIBO_ACCESS_HOST_MAX];
    char path[RIVER_ORVIBO_ACCESS_PATH_MAX];
    uint16_t port;
} river_orvibo_access_url_t;

typedef struct {
    uint16_t status_code;
    size_t body_bytes;
    char *body;
} river_orvibo_http_response_t;

typedef struct {
    bool initialized;
    bool ready;
    bool identity_ready;
    bool websocket_configured;
    bool used_ota;
    bool activation_required;
    bool activation_done;
    bool activation_challenge_available;
    uint32_t attempts;
    uint32_t http_status;
    char ota_url[RIVER_ORVIBO_ACCESS_URL_MAX];
    char device_id[RIVER_ORVIBO_ACCESS_DEVICE_ID_MAX];
    char client_id[RIVER_ORVIBO_ACCESS_CLIENT_ID_MAX];
    char activation_code[RIVER_ORVIBO_ACCESS_CODE_MAX];
    char activation_message[RIVER_ORVIBO_ACCESS_MESSAGE_MAX];
    char last_error[RIVER_ORVIBO_ACCESS_ERROR_MAX];
} river_orvibo_access_context_t;

static river_orvibo_access_context_t g_river_orvibo_access;

static void river_orvibo_access_copy(char *dst, size_t dst_size, const char *src)
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

static void river_orvibo_access_set_error(const char *error)
{
    river_orvibo_access_copy(g_river_orvibo_access.last_error,
                             sizeof(g_river_orvibo_access.last_error),
                             error != NULL ? error : "-");
}

static uint32_t river_orvibo_access_fnv1a32(const uint8_t *data, size_t bytes, uint32_t seed)
{
    uint32_t hash = 2166136261UL ^ seed;
    size_t index;

    for (index = 0U; index < bytes; ++index) {
        hash ^= (uint32_t)data[index];
        hash *= 16777619UL;
    }
    return hash;
}

static bool river_orvibo_access_mac_valid(const uint8_t *mac)
{
    bool any_nonzero = false;
    bool any_not_ff = false;
    size_t index;

    if (mac == NULL) {
        return false;
    }
    for (index = 0U; index < 6U; ++index) {
        if (mac[index] != 0U) {
            any_nonzero = true;
        }
        if (mac[index] != 0xFFU) {
            any_not_ff = true;
        }
    }
    return any_nonzero && any_not_ff;
}

static bool river_orvibo_access_read_sta_mac(uint8_t mac[6])
{
    uint8_t *netif_mac;

    if (mac == NULL) {
        return false;
    }
    netif_mac = LwIP_GetMAC(NETIF_WLAN_STA_INDEX);
    if (!river_orvibo_access_mac_valid(netif_mac)) {
        return false;
    }
    memcpy(mac, netif_mac, 6U);
    return true;
}

static void river_orvibo_access_format_device_id(const uint8_t mac[6],
                                                 char *buffer,
                                                 size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U) {
        return;
    }
    if (!river_orvibo_access_mac_valid(mac)) {
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

static void river_orvibo_access_build_client_id_from_mac(const uint8_t mac[6],
                                                         char *buffer,
                                                         size_t buffer_size)
{
    uint8_t uuid[16];
    uint32_t hash_words[4];
    size_t index;

    if (buffer == NULL || buffer_size == 0U) {
        return;
    }
    if (!river_orvibo_access_mac_valid(mac)) {
        static uint8_t zero_mac[6] = {0U, 0U, 0U, 0U, 0U, 0U};
        mac = zero_mac;
    }
    hash_words[0] = river_orvibo_access_fnv1a32(mac, 6U, 0x13579BDFUL);
    hash_words[1] = river_orvibo_access_fnv1a32(mac, 6U, 0x2468ACE0UL);
    hash_words[2] = river_orvibo_access_fnv1a32(mac, 6U, 0x55AA11EEUL);
    hash_words[3] = river_orvibo_access_fnv1a32(mac, 6U, 0xA5A55A5AUL);
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

static bool river_orvibo_access_refresh_identity(void)
{
    uint8_t mac[6];
    char device_id[RIVER_ORVIBO_ACCESS_DEVICE_ID_MAX];
    char client_id[RIVER_ORVIBO_ACCESS_CLIENT_ID_MAX];

    if (!river_orvibo_access_read_sta_mac(mac)) {
        if (g_river_orvibo_access.device_id[0] == '\0') {
            static const uint8_t zero_mac[6] = {0U, 0U, 0U, 0U, 0U, 0U};

            river_orvibo_access_format_device_id(zero_mac,
                                                 g_river_orvibo_access.device_id,
                                                 sizeof(g_river_orvibo_access.device_id));
            river_orvibo_access_build_client_id_from_mac(
                zero_mac,
                g_river_orvibo_access.client_id,
                sizeof(g_river_orvibo_access.client_id));
        }
        g_river_orvibo_access.identity_ready = false;
        g_river_orvibo_access.ready = false;
        river_orvibo_access_set_error("sta_mac_unavailable");
        return false;
    }

    river_orvibo_access_format_device_id(mac, device_id, sizeof(device_id));
    river_orvibo_access_build_client_id_from_mac(mac, client_id, sizeof(client_id));
    if (strcmp(g_river_orvibo_access.device_id, device_id) != 0 ||
        strcmp(g_river_orvibo_access.client_id, client_id) != 0) {
        RIVER_LOGI("access identity refreshed: device_id=%s client_id=%s",
                   device_id,
                   client_id);
        river_orvibo_access_copy(g_river_orvibo_access.device_id,
                                 sizeof(g_river_orvibo_access.device_id),
                                 device_id);
        river_orvibo_access_copy(g_river_orvibo_access.client_id,
                                 sizeof(g_river_orvibo_access.client_id),
                                 client_id);
    }
    g_river_orvibo_access.identity_ready = true;
    return true;
}

static river_status_t river_orvibo_access_parse_http_url(const char *url,
                                                         river_orvibo_access_url_t *parsed)
{
    const char *host_start;
    const char *path_start;
    const char *host_end;
    const char *port_sep;
    size_t host_len;

    if (url == NULL || parsed == NULL) {
        return RIVER_ERR_ARG;
    }
    memset(parsed, 0, sizeof(*parsed));
    if (strncmp(url, "http://", 7U) == 0) {
        parsed->secure = false;
        parsed->port = 80U;
        host_start = url + 7U;
    } else if (strncmp(url, "https://", 8U) == 0) {
        parsed->secure = true;
        parsed->port = 443U;
        host_start = url + 8U;
    } else {
        return RIVER_ERR_ARG;
    }
    path_start = strchr(host_start, '/');
    host_end = path_start != NULL ? path_start : url + strlen(url);
    port_sep = memchr(host_start, ':', (size_t)(host_end - host_start));
    if (port_sep != NULL) {
        host_end = port_sep;
        parsed->port = (uint16_t)atoi(port_sep + 1);
    }
    host_len = (size_t)(host_end - host_start);
    if (host_len == 0U || host_len >= sizeof(parsed->host)) {
        return RIVER_ERR_ARG;
    }
    memcpy(parsed->host, host_start, host_len);
    parsed->host[host_len] = '\0';
    if (path_start == NULL || path_start[0] == '\0') {
        snprintf(parsed->path, sizeof(parsed->path), "%s", "/");
    } else if (snprintf(parsed->path, sizeof(parsed->path), "%s", path_start) >=
               (int)sizeof(parsed->path)) {
        return RIVER_ERR_ARG;
    }
    return RIVER_OK;
}

static uint16_t river_orvibo_access_response_status(const struct httpc_conn *conn)
{
    char tmp[8];
    size_t copy_len;

    if (conn == NULL || conn->response.status == NULL || conn->response.status_len == 0U) {
        return 0U;
    }
    copy_len = conn->response.status_len;
    if (copy_len >= sizeof(tmp)) {
        copy_len = sizeof(tmp) - 1U;
    }
    memcpy(tmp, conn->response.status, copy_len);
    tmp[copy_len] = '\0';
    return (uint16_t)atoi(tmp);
}

static river_status_t river_orvibo_access_http_request(const char *method,
                                                       const char *url,
                                                       const char *body,
                                                       river_orvibo_http_response_t *response)
{
    river_orvibo_access_url_t parsed;
    struct httpc_conn *conn = NULL;
    uint32_t total = 0U;
    river_status_t status = RIVER_ERR_IO;

    if (method == NULL || url == NULL || response == NULL) {
        return RIVER_ERR_ARG;
    }
    memset(response, 0, sizeof(*response));
    response->body = (char *)rtos_mem_zmalloc(RIVER_ORVIBO_ACCESS_HTTP_BODY_MAX);
    if (response->body == NULL) {
        return RIVER_ERR_NO_MEMORY;
    }
    if (river_orvibo_access_parse_http_url(url, &parsed) != RIVER_OK) {
        river_orvibo_access_set_error("http_url_invalid");
        goto exit;
    }

    conn = httpc_conn_new(parsed.secure ? HTTPC_SECURE_TLS : HTTPC_SECURE_NONE, NULL, NULL, NULL);
    if (conn == NULL) {
        river_orvibo_access_set_error("http_conn_new_failed");
        goto exit;
    }
    httpc_enable_ignore_content_len(conn);
    if (httpc_conn_connect(conn,
                           parsed.host,
                           parsed.port,
                           RIVER_ORVIBO_ACCESS_HTTP_TIMEOUT_SEC) != 0) {
        river_orvibo_access_set_error("http_connect_failed");
        goto exit;
    }
    if (httpc_request_write_header_start(conn,
                                         (char *)method,
                                         parsed.path,
                                         body != NULL ? (char *)"application/json" : NULL,
                                         body != NULL ? strlen(body) : 0U) != 0) {
        river_orvibo_access_set_error("http_header_start_failed");
        goto exit;
    }
    (void)httpc_request_write_header(conn, (char *)"Activation-Version", (char *)"1");
    (void)httpc_request_write_header(conn, (char *)"Device-Id", g_river_orvibo_access.device_id);
    (void)httpc_request_write_header(conn, (char *)"Client-Id", g_river_orvibo_access.client_id);
    (void)httpc_request_write_header(conn, (char *)"User-Agent", (char *)"orvibo-rtl8730e/0.1.0");
    (void)httpc_request_write_header(conn, (char *)"Accept-Language", (char *)"zh-CN");
    (void)httpc_request_write_header(conn, (char *)"Connection", (char *)"close");
    if (httpc_request_write_header_finish(conn) <= 0) {
        river_orvibo_access_set_error("http_header_finish_failed");
        goto exit;
    }
    if (body != NULL && body[0] != '\0' &&
        httpc_request_write_data(conn, (uint8_t *)body, strlen(body)) <= 0) {
        river_orvibo_access_set_error("http_body_send_failed");
        goto exit;
    }
    if (httpc_response_read_header(conn) != 0) {
        river_orvibo_access_set_error("http_response_header_failed");
        goto exit;
    }
    response->status_code = river_orvibo_access_response_status(conn);
    g_river_orvibo_access.http_status = response->status_code;
    while (total + 1U < RIVER_ORVIBO_ACCESS_HTTP_BODY_MAX) {
        int read_size = httpc_response_read_data(conn,
                                                 (uint8_t *)response->body + total,
                                                 RIVER_ORVIBO_ACCESS_HTTP_BODY_MAX - total - 1U);
        if (read_size <= 0) {
            break;
        }
        total += (uint32_t)read_size;
        if (conn->response.content_len != 0U && total >= conn->response.content_len) {
            break;
        }
    }
    response->body[total] = '\0';
    response->body_bytes = total;
    status = RIVER_OK;

exit:
    if (conn != NULL) {
        httpc_conn_close(conn);
        httpc_conn_free(conn);
    }
    if (status != RIVER_OK && response->body != NULL) {
        rtos_mem_free(response->body);
        response->body = NULL;
    }
    return status;
}

static void river_orvibo_access_free_response(river_orvibo_http_response_t *response)
{
    if (response != NULL && response->body != NULL) {
        rtos_mem_free(response->body);
        response->body = NULL;
    }
}

static char *river_orvibo_access_build_system_info_json(void)
{
    cJSON *root = NULL;
    cJSON *application = NULL;
    cJSON *board = NULL;
    char *printed = NULL;

    root = cJSON_CreateObject();
    application = cJSON_CreateObject();
    board = cJSON_CreateObject();
    if (root == NULL || application == NULL || board == NULL) {
        goto exit;
    }
    cJSON_AddNumberToObject(root, "version", 2);
    cJSON_AddStringToObject(root, "language", "zh-CN");
    cJSON_AddStringToObject(root, "mac_address", g_river_orvibo_access.device_id);
    cJSON_AddStringToObject(root, "uuid", g_river_orvibo_access.client_id);
    cJSON_AddStringToObject(root, "chip_model_name", "rtl8730e");
    cJSON_AddStringToObject(application, "name", "ameba-river");
    cJSON_AddStringToObject(application, "version", "0.1.0");
    cJSON_AddItemToObject(root, "application", application);
    application = NULL;
    cJSON_AddStringToObject(board, "type", "wifi");
    cJSON_AddStringToObject(board, "name", "orvibo-rtl8730e");
    cJSON_AddStringToObject(board, "ssid", river_wifi_station_ssid());
    cJSON_AddStringToObject(board, "mac", g_river_orvibo_access.device_id);
    cJSON_AddItemToObject(root, "board", board);
    board = NULL;
    printed = cJSON_PrintUnformatted(root);

exit:
    if (board != NULL) {
        cJSON_Delete(board);
    }
    if (application != NULL) {
        cJSON_Delete(application);
    }
    if (root != NULL) {
        cJSON_Delete(root);
    }
    return printed;
}

static river_status_t river_orvibo_access_apply_websocket_config(const cJSON *websocket)
{
    river_orvibo_protocol_config_t config;
    const cJSON *url_obj;
    const cJSON *token_obj;
    const cJSON *version_obj;

    if (!cJSON_IsObject(websocket)) {
        return RIVER_ERR_NOT_FOUND;
    }
    if (river_orvibo_protocol_get_config(&config) != RIVER_OK) {
        return RIVER_ERR_INVALID_STATE;
    }
    url_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)websocket, "url");
    token_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)websocket, "token");
    version_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)websocket, "version");
    if (cJSON_IsString(url_obj) && url_obj->valuestring != NULL && url_obj->valuestring[0] != '\0') {
        config.url = url_obj->valuestring;
    }
    if (cJSON_IsString(token_obj) && token_obj->valuestring != NULL) {
        config.token = token_obj->valuestring;
    }
    if (cJSON_IsNumber(version_obj) && version_obj->valueint > 0) {
        config.protocol_version = (uint16_t)version_obj->valueint;
    }
    if (river_orvibo_protocol_set_config(&config) != RIVER_OK) {
        return RIVER_ERR_IO;
    }
    g_river_orvibo_access.websocket_configured = true;
    return RIVER_OK;
}

static bool river_orvibo_access_static_config_usable(void)
{
    return RIVER_ORVIBO_WS_URL[0] != '\0' && RIVER_ORVIBO_WS_TOKEN[0] != '\0';
}

static void river_orvibo_access_parse_activation(const cJSON *root)
{
    const cJSON *activation;
    const cJSON *message;
    const cJSON *code;
    const cJSON *challenge;

    g_river_orvibo_access.activation_required = false;
    g_river_orvibo_access.activation_challenge_available = false;
    g_river_orvibo_access.activation_code[0] = '\0';
    g_river_orvibo_access.activation_message[0] = '\0';
    activation = cJSON_GetObjectItemCaseSensitive((cJSON *)root, "activation");
    if (!cJSON_IsObject(activation)) {
        return;
    }
    message = cJSON_GetObjectItemCaseSensitive((cJSON *)activation, "message");
    code = cJSON_GetObjectItemCaseSensitive((cJSON *)activation, "code");
    challenge = cJSON_GetObjectItemCaseSensitive((cJSON *)activation, "challenge");
    if (cJSON_IsString(message) && message->valuestring != NULL) {
        river_orvibo_access_copy(g_river_orvibo_access.activation_message,
                                 sizeof(g_river_orvibo_access.activation_message),
                                 message->valuestring);
    }
    if (cJSON_IsString(code) && code->valuestring != NULL) {
        river_orvibo_access_copy(g_river_orvibo_access.activation_code,
                                 sizeof(g_river_orvibo_access.activation_code),
                                 code->valuestring);
        g_river_orvibo_access.activation_required = true;
    }
    if (cJSON_IsString(challenge) && challenge->valuestring != NULL) {
        g_river_orvibo_access.activation_challenge_available = true;
        g_river_orvibo_access.activation_required = true;
    } else if (g_river_orvibo_access.activation_code[0] != '\0') {
        river_orvibo_access_set_error("activation_waiting_user");
    }
}

static void river_orvibo_access_update_ready(void)
{
    if (g_river_orvibo_access.activation_required) {
        g_river_orvibo_access.ready = false;
        return;
    }
    g_river_orvibo_access.ready = g_river_orvibo_access.websocket_configured &&
                                  g_river_orvibo_access.identity_ready;
}

static river_status_t river_orvibo_access_check_version_once(void)
{
    river_orvibo_http_response_t response;
    cJSON *root = NULL;
    cJSON *websocket;
    char *system_info = NULL;
    river_status_t status = RIVER_ERR_IO;

    system_info = river_orvibo_access_build_system_info_json();
    if (system_info == NULL) {
        river_orvibo_access_set_error("system_info_json_failed");
        return RIVER_ERR_NO_MEMORY;
    }
    status = river_orvibo_access_http_request("POST",
                                             g_river_orvibo_access.ota_url,
                                             system_info,
                                             &response);
    cJSON_free(system_info);
    if (status != RIVER_OK) {
        return status;
    }
    if (response.status_code != 200U) {
        river_orvibo_access_set_error("ota_status_not_200");
        river_orvibo_access_free_response(&response);
        return RIVER_ERR_IO;
    }
    root = cJSON_ParseWithLength(response.body, response.body_bytes);
    if (root == NULL) {
        river_orvibo_access_set_error("ota_json_parse_failed");
        river_orvibo_access_free_response(&response);
        return RIVER_ERR_IO;
    }
    river_orvibo_access_parse_activation(root);
    websocket = cJSON_GetObjectItemCaseSensitive(root, "websocket");
    if (cJSON_IsObject(websocket) &&
        river_orvibo_access_apply_websocket_config(websocket) == RIVER_OK) {
        g_river_orvibo_access.used_ota = true;
        RIVER_LOGI("OTA websocket config applied");
    }
    river_orvibo_access_update_ready();
    if (g_river_orvibo_access.ready) {
        g_river_orvibo_access.last_error[0] = '\0';
    }
    cJSON_Delete(root);
    river_orvibo_access_free_response(&response);
    return RIVER_OK;
}

static river_status_t river_orvibo_access_activate_once(void)
{
    char activation_url[RIVER_ORVIBO_ACCESS_URL_MAX + 16U];
    river_orvibo_http_response_t response;
    river_status_t status;

    if (snprintf(activation_url,
                 sizeof(activation_url),
                 "%s%s",
                 g_river_orvibo_access.ota_url,
                 g_river_orvibo_access.ota_url[strlen(g_river_orvibo_access.ota_url) - 1U] == '/' ?
                     "activate" :
                     "/activate") >= (int)sizeof(activation_url)) {
        river_orvibo_access_set_error("activation_url_too_long");
        return RIVER_ERR_ARG;
    }
    status = river_orvibo_access_http_request("POST", activation_url, "{}", &response);
    if (status != RIVER_OK) {
        return status;
    }
    g_river_orvibo_access.http_status = response.status_code;
    if (response.status_code == 200U) {
        g_river_orvibo_access.activation_done = true;
        g_river_orvibo_access.activation_required = false;
        g_river_orvibo_access.last_error[0] = '\0';
        status = RIVER_OK;
    } else if (response.status_code == 202U) {
        river_orvibo_access_set_error("activation_pending");
        status = RIVER_ERR_BUSY;
    } else {
        river_orvibo_access_set_error("activation_failed");
        status = RIVER_ERR_IO;
    }
    river_orvibo_access_free_response(&response);
    return status;
}

static river_status_t river_orvibo_access_run_activation(void)
{
    uint32_t retry;

    if (!g_river_orvibo_access.activation_required) {
        return RIVER_OK;
    }
    if (!g_river_orvibo_access.activation_challenge_available) {
        RIVER_LOGW("device activation waiting for user binding: code=%s message=%s",
                   g_river_orvibo_access.activation_code[0] != '\0' ?
                       g_river_orvibo_access.activation_code :
                       "-",
                   g_river_orvibo_access.activation_message[0] != '\0' ?
                       g_river_orvibo_access.activation_message :
                       "-");
        river_orvibo_access_set_error("activation_waiting_user");
        return RIVER_ERR_BUSY;
    }
    RIVER_LOGW("device activation required: code=%s message=%s",
               g_river_orvibo_access.activation_code[0] != '\0' ?
                   g_river_orvibo_access.activation_code :
                   "-",
               g_river_orvibo_access.activation_message[0] != '\0' ?
                   g_river_orvibo_access.activation_message :
                   "-");
    for (retry = 0U; retry < RIVER_ORVIBO_ACCESS_ACTIVATE_RETRIES; ++retry) {
        river_status_t status = river_orvibo_access_activate_once();
        if (status == RIVER_OK) {
            RIVER_LOGI("device activation accepted");
            return RIVER_OK;
        }
        if (status != RIVER_ERR_BUSY) {
            RIVER_LOGW("activation request failed: status=%d http=%lu error=%s",
                       (int)status,
                       (unsigned long)g_river_orvibo_access.http_status,
                       g_river_orvibo_access.last_error);
        }
        rtos_time_delay_ms(RIVER_ORVIBO_ACCESS_ACTIVATE_WAIT_MS);
    }
    return RIVER_ERR_BUSY;
}

river_status_t river_orvibo_access_init(void)
{
    if (g_river_orvibo_access.initialized) {
        return RIVER_OK;
    }
    memset(&g_river_orvibo_access, 0, sizeof(g_river_orvibo_access));
    river_orvibo_access_copy(g_river_orvibo_access.ota_url,
                             sizeof(g_river_orvibo_access.ota_url),
                             RIVER_ORVIBO_OTA_URL);
    (void)river_orvibo_access_refresh_identity();
    g_river_orvibo_access.websocket_configured = river_orvibo_access_static_config_usable();
    river_orvibo_access_update_ready();
    if (!g_river_orvibo_access.ready) {
        river_orvibo_access_set_error("waiting_ota_config");
    }
    g_river_orvibo_access.initialized = true;
    RIVER_LOGI("access identity: device_id=%s client_id=%s ota=%s static_ws=%s",
               g_river_orvibo_access.device_id,
               g_river_orvibo_access.client_id,
               g_river_orvibo_access.ota_url,
               river_orvibo_access_static_config_usable() ? "usable" : "needs_ota");
    return RIVER_OK;
}

river_status_t river_orvibo_access_refresh(void)
{
    uint32_t round;

    if (river_orvibo_access_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    if (!river_wifi_station_is_connected()) {
        river_orvibo_access_set_error("wifi_not_connected");
        return RIVER_ERR_BUSY;
    }
    if (!river_orvibo_access_refresh_identity()) {
        RIVER_LOGW("access refresh blocked: valid STA MAC unavailable");
        return RIVER_ERR_BUSY;
    }
    g_river_orvibo_access.attempts++;
    for (round = 0U; round < RIVER_ORVIBO_ACCESS_MAX_CHECK_ROUNDS; ++round) {
        river_status_t status = river_orvibo_access_check_version_once();
        if (status != RIVER_OK) {
            if (g_river_orvibo_access.ready) {
                RIVER_LOGW("OTA refresh failed but existing websocket config remains usable: status=%d error=%s",
                           (int)status,
                           g_river_orvibo_access.last_error);
                return RIVER_OK;
            }
            return status;
        }
        if (!g_river_orvibo_access.activation_required) {
            if (g_river_orvibo_access.ready) {
                return RIVER_OK;
            }
            river_orvibo_access_set_error("ota_missing_websocket");
            return RIVER_ERR_NOT_FOUND;
        }
        status = river_orvibo_access_run_activation();
        if (status != RIVER_OK) {
            return status;
        }
        g_river_orvibo_access.activation_required = false;
        river_orvibo_access_update_ready();
        if (g_river_orvibo_access.ready) {
            return RIVER_OK;
        }
    }
    river_orvibo_access_set_error("activation_check_exhausted");
    return g_river_orvibo_access.ready ? RIVER_OK : RIVER_ERR_BUSY;
}

bool river_orvibo_access_ready(void)
{
    return g_river_orvibo_access.ready;
}

const char *river_orvibo_access_device_id(void)
{
    (void)river_orvibo_access_init();
    return g_river_orvibo_access.device_id;
}

const char *river_orvibo_access_client_id(void)
{
    (void)river_orvibo_access_init();
    return g_river_orvibo_access.client_id;
}

river_status_t river_orvibo_access_get_status(river_orvibo_access_status_t *status)
{
    if (status == NULL) {
        return RIVER_ERR_ARG;
    }
    (void)river_orvibo_access_init();
    memset(status, 0, sizeof(*status));
    status->ready = g_river_orvibo_access.ready;
    status->identity_ready = g_river_orvibo_access.identity_ready;
    status->websocket_configured = g_river_orvibo_access.websocket_configured;
    status->used_ota = g_river_orvibo_access.used_ota;
    status->activation_required = g_river_orvibo_access.activation_required;
    status->activation_done = g_river_orvibo_access.activation_done;
    status->activation_challenge_available =
        g_river_orvibo_access.activation_challenge_available;
    status->device_id = g_river_orvibo_access.device_id;
    status->client_id = g_river_orvibo_access.client_id;
    status->ota_url = g_river_orvibo_access.ota_url;
    status->activation_code = g_river_orvibo_access.activation_code;
    status->activation_message = g_river_orvibo_access.activation_message;
    status->last_error = g_river_orvibo_access.last_error;
    status->attempts = g_river_orvibo_access.attempts;
    status->http_status = g_river_orvibo_access.http_status;
    return RIVER_OK;
}

void river_orvibo_access_dump_status(void)
{
    (void)river_orvibo_access_init();
    RIVER_LOGI("orvibo access: ready=%s identity=%s ws_config=%s used_ota=%s activation=%s challenge=%s done=%s attempts=%lu http=%lu device_id=%s client_id=%s ota=%s code=%s message=%s last_error=%s",
               g_river_orvibo_access.ready ? "yes" : "no",
               g_river_orvibo_access.identity_ready ? "ready" : "waiting_mac",
               g_river_orvibo_access.websocket_configured ? "yes" : "no",
               g_river_orvibo_access.used_ota ? "yes" : "no",
               g_river_orvibo_access.activation_required ? "required" : "none",
               g_river_orvibo_access.activation_challenge_available ? "yes" : "no",
               g_river_orvibo_access.activation_done ? "yes" : "no",
               (unsigned long)g_river_orvibo_access.attempts,
               (unsigned long)g_river_orvibo_access.http_status,
               g_river_orvibo_access.device_id,
               g_river_orvibo_access.client_id,
               g_river_orvibo_access.ota_url,
               g_river_orvibo_access.activation_code[0] != '\0' ?
                   g_river_orvibo_access.activation_code :
                   "-",
               g_river_orvibo_access.activation_message[0] != '\0' ?
                   g_river_orvibo_access.activation_message :
                   "-",
               g_river_orvibo_access.last_error[0] != '\0' ?
                   g_river_orvibo_access.last_error :
                   "-");
}
