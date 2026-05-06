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

static river_status_t river_xiaozhi_http_get_json(const char *url,
                                                  const char *auth_token,
                                                  const char *device_id,
                                                  const char *client_id,
                                                  char *response_out,
                                                  size_t response_out_size,
                                                  int *status_code_out)
{
    struct httpc_conn *conn = NULL;
    char host[RIVER_XIAOZHI_HTTP_HOST_MAX];
    char resource[RIVER_XIAOZHI_HTTP_RESOURCE_MAX];
    char auth_header[320];
    uint16_t port = 0U;
    uint8_t secure = HTTPC_SECURE_NONE;
    size_t total_read = 0U;
    river_status_t status = RIVER_ERR_IO;

    if (url == NULL || device_id == NULL || client_id == NULL || response_out == NULL ||
        response_out_size == 0U) {
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

    response_out[0] = '\0';
    auth_header[0] = '\0';
    if (status_code_out != NULL) {
        *status_code_out = 0;
    }
    if (auth_token != NULL && auth_token[0] != '\0') {
        if (strchr(auth_token, ' ') == NULL) {
            snprintf(auth_header, sizeof(auth_header), "Bearer %s", auth_token);
        } else {
            snprintf(auth_header, sizeof(auth_header), "%s", auth_token);
        }
    }

    conn = httpc_conn_new(secure, NULL, NULL, NULL);
    if (conn == NULL) {
        return RIVER_ERR_NO_MEMORY;
    }

    if (httpc_conn_connect(conn,
                           host,
                           port,
                           (uint32_t)RIVER_XIAOZHI_OTA_HTTP_TIMEOUT_SEC) != 0) {
        goto exit;
    }

    if (httpc_request_write_header_start(conn, (char *)"GET", resource, NULL, 0U) != 0) {
        goto exit;
    }
    if (httpc_request_write_header(conn, (char *)"Connection", (char *)"close") != 0 ||
        httpc_request_write_header(conn, (char *)"Device-Id", (char *)device_id) != 0 ||
        httpc_request_write_header(conn, (char *)"Client-Id", (char *)client_id) != 0 ||
        httpc_request_write_header(conn, (char *)"User-Agent", (char *)RIVER_XIAOZHI_HTTP_USER_AGENT) != 0 ||
        httpc_request_write_header(conn, (char *)"Accept", (char *)"application/json") != 0) {
        goto exit;
    }
    if (auth_header[0] != '\0' &&
        httpc_request_write_header(conn, (char *)"Authorization", auth_header) != 0) {
        goto exit;
    }
    if (httpc_request_write_header_finish(conn) <= 0) {
        goto exit;
    }
    if (httpc_response_read_header(conn) != 0) {
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

static river_status_t river_xiaozhi_build_discovery_url(const char *ws_url,
                                                        char *url_out,
                                                        size_t url_out_size)
{
    char base_url[RIVER_XIAOZHI_BASE_URL_MAX];
    char path[RIVER_XIAOZHI_PATH_MAX];
    char discovery_path[RIVER_XIAOZHI_PATH_MAX];
    const char *scheme_prefix = NULL;
    const char *host_start = NULL;
    int port = 0;
    int default_port = 0;
    size_t path_len;
    river_status_t status;

    if (ws_url == NULL || url_out == NULL || url_out_size == 0U) {
        return RIVER_ERR_ARG;
    }

    status = river_xiaozhi_parse_url(ws_url,
                                     base_url,
                                     sizeof(base_url),
                                     &port,
                                     path,
                                     sizeof(path));
    if (status != RIVER_OK) {
        return status;
    }

    if (strncmp(base_url, "wss://", 6U) == 0) {
        scheme_prefix = "https://";
        host_start = base_url + 6U;
        default_port = 443;
    } else if (strncmp(base_url, "ws://", 5U) == 0) {
        scheme_prefix = "http://";
        host_start = base_url + 5U;
        default_port = 80;
    } else {
        return RIVER_ERR_ARG;
    }

    snprintf(discovery_path, sizeof(discovery_path), "%s",
             RIVER_XIAOZHI_DISCOVERY_PATH_DEFAULT);
    if (path[0] != '\0') {
        snprintf(discovery_path, sizeof(discovery_path), "/%s", path);
        path_len = strlen(discovery_path);
        if (path_len > 3U && strcmp(discovery_path + path_len - 3U, "/ws") == 0) {
            discovery_path[path_len - 3U] = '\0';
            if (discovery_path[0] == '\0') {
                snprintf(discovery_path, sizeof(discovery_path), "%s",
                         RIVER_XIAOZHI_DISCOVERY_PATH_DEFAULT);
            }
        } else {
            snprintf(discovery_path, sizeof(discovery_path), "%s",
                     RIVER_XIAOZHI_DISCOVERY_PATH_DEFAULT);
        }
    }

    if (port == default_port || port == 0) {
        if (snprintf(url_out, url_out_size, "%s%s%s",
                     scheme_prefix,
                     host_start,
                     discovery_path) >= (int)url_out_size) {
            return RIVER_ERR_IO;
        }
    } else {
        if (snprintf(url_out, url_out_size, "%s%s:%d%s",
                     scheme_prefix,
                     host_start,
                     port,
                     discovery_path) >= (int)url_out_size) {
            return RIVER_ERR_IO;
        }
    }

    return RIVER_OK;
}

static river_status_t river_xiaozhi_parse_discovery_response(const char *json_text)
{
    cJSON *root = NULL;
    const cJSON *protocol_version_obj;
    const cJSON *subprotocol_obj;
    const cJSON *product_profile_obj;
    const cJSON *mainline_profile_obj;
    const cJSON *turn_mode_obj;
    const cJSON *features_obj;
    const cJSON *server_endpoint_obj;
    const cJSON *voice_collaboration_obj;
    const cJSON *preview_events_obj;
    const cJSON *playback_ack_obj;
    const cJSON *server_endpointing_obj;
    const cJSON *feature_preview_events_obj;
    const cJSON *feature_playback_ack_obj;
    const cJSON *available_obj;
    const cJSON *enabled_obj;
    const cJSON *mode_obj;
    const cJSON *speech_start_obj;
    const cJSON *partial_obj;
    const cJSON *accept_ready_obj;
    const cJSON *endpoint_candidate_obj;
    const cJSON *started_obj;
    const cJSON *mark_obj;
    const cJSON *cleared_obj;
    const cJSON *completed_obj;

    if (json_text == NULL || json_text[0] == '\0') {
        return RIVER_ERR_ARG;
    }

    root = cJSON_Parse(json_text);
    if (root == NULL) {
        return RIVER_ERR_IO;
    }

    river_xiaozhi_clear_discovery_profile();

    protocol_version_obj = cJSON_GetObjectItemCaseSensitive(root, "protocol_version");
    if (cJSON_IsString(protocol_version_obj) && protocol_version_obj->valuestring != NULL) {
        river_xiaozhi_copy_optional_string(g_river_xiaozhi.discovery_protocol_version,
                                           sizeof(g_river_xiaozhi.discovery_protocol_version),
                                           protocol_version_obj->valuestring);
    }

    subprotocol_obj = cJSON_GetObjectItemCaseSensitive(root, "subprotocol");
    if (cJSON_IsString(subprotocol_obj) && subprotocol_obj->valuestring != NULL) {
        river_xiaozhi_copy_optional_string(g_river_xiaozhi.discovery_subprotocol,
                                           sizeof(g_river_xiaozhi.discovery_subprotocol),
                                           subprotocol_obj->valuestring);
    }

    product_profile_obj = cJSON_GetObjectItemCaseSensitive(root, "product_profile");
    if (cJSON_IsString(product_profile_obj) && product_profile_obj->valuestring != NULL) {
        river_xiaozhi_copy_optional_string(g_river_xiaozhi.discovery_product_profile,
                                           sizeof(g_river_xiaozhi.discovery_product_profile),
                                           product_profile_obj->valuestring);
    }

    mainline_profile_obj = cJSON_GetObjectItemCaseSensitive(root, "mainline_profile");
    if (cJSON_IsString(mainline_profile_obj) && mainline_profile_obj->valuestring != NULL) {
        river_xiaozhi_copy_optional_string(g_river_xiaozhi.discovery_mainline_profile,
                                           sizeof(g_river_xiaozhi.discovery_mainline_profile),
                                           mainline_profile_obj->valuestring);
    }

    turn_mode_obj = cJSON_GetObjectItemCaseSensitive(root, "turn_mode");
    if (cJSON_IsString(turn_mode_obj) && turn_mode_obj->valuestring != NULL) {
        river_xiaozhi_copy_optional_string(g_river_xiaozhi.discovery_turn_mode,
                                           sizeof(g_river_xiaozhi.discovery_turn_mode),
                                           turn_mode_obj->valuestring);
    }

    features_obj = cJSON_GetObjectItemCaseSensitive(root, "features");
    if (cJSON_IsObject(features_obj)) {
        server_endpointing_obj =
            cJSON_GetObjectItemCaseSensitive((cJSON *)features_obj,
                                             "server_endpointing");
        feature_preview_events_obj =
            cJSON_GetObjectItemCaseSensitive((cJSON *)features_obj, "preview_events");
        feature_playback_ack_obj =
            cJSON_GetObjectItemCaseSensitive((cJSON *)features_obj, "playback_ack");

        if (cJSON_IsBool(server_endpointing_obj)) {
            g_river_xiaozhi.discovery_server_endpoint_available = true;
            g_river_xiaozhi.discovery_server_endpoint_enabled =
                cJSON_IsTrue(server_endpointing_obj);
            river_xiaozhi_copy_optional_string(
                g_river_xiaozhi.discovery_server_endpoint_mode,
                sizeof(g_river_xiaozhi.discovery_server_endpoint_mode),
                g_river_xiaozhi.discovery_server_endpoint_enabled ?
                    "server_endpoint_v1" :
                    "client_commit");
        }
        if (cJSON_IsBool(feature_preview_events_obj)) {
            g_river_xiaozhi.discovery_preview_events_enabled =
                cJSON_IsTrue(feature_preview_events_obj);
            river_xiaozhi_copy_optional_string(
                g_river_xiaozhi.discovery_preview_events_mode,
                sizeof(g_river_xiaozhi.discovery_preview_events_mode),
                g_river_xiaozhi.discovery_preview_events_enabled ? "preview_v1" :
                                                                  "disabled");
        }
        if (cJSON_IsBool(feature_playback_ack_obj)) {
            g_river_xiaozhi.discovery_playback_ack_enabled =
                cJSON_IsTrue(feature_playback_ack_obj);
            if (g_river_xiaozhi.discovery_playback_ack_enabled &&
                g_river_xiaozhi.discovery_playback_ack_mode[0] == '\0') {
                river_xiaozhi_copy_optional_string(
                    g_river_xiaozhi.discovery_playback_ack_mode,
                    sizeof(g_river_xiaozhi.discovery_playback_ack_mode),
                    "started_completed_v1");
            }
        }
    }

    server_endpoint_obj = cJSON_GetObjectItemCaseSensitive(root, "server_endpoint");
    if (cJSON_IsObject(server_endpoint_obj)) {
        available_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)server_endpoint_obj, "available");
        enabled_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)server_endpoint_obj, "enabled");
        mode_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)server_endpoint_obj, "mode");
        if (cJSON_IsBool(available_obj)) {
            g_river_xiaozhi.discovery_server_endpoint_available = cJSON_IsTrue(available_obj);
        }
        if (cJSON_IsBool(enabled_obj)) {
            g_river_xiaozhi.discovery_server_endpoint_enabled = cJSON_IsTrue(enabled_obj);
        }
        if (cJSON_IsString(mode_obj) && mode_obj->valuestring != NULL) {
            river_xiaozhi_copy_optional_string(g_river_xiaozhi.discovery_server_endpoint_mode,
                                               sizeof(g_river_xiaozhi.discovery_server_endpoint_mode),
                                               mode_obj->valuestring);
        }
    }

    voice_collaboration_obj = cJSON_GetObjectItemCaseSensitive(root, "voice_collaboration");
    if (cJSON_IsObject(voice_collaboration_obj)) {
        g_river_xiaozhi.discovery_voice_collaboration_advertised = true;

        preview_events_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)voice_collaboration_obj,
                                                              "preview_events");
        if (cJSON_IsObject(preview_events_obj)) {
            enabled_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)preview_events_obj, "enabled");
            speech_start_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)preview_events_obj,
                                                                "speech_start");
            partial_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)preview_events_obj, "partial");
            accept_ready_obj =
                cJSON_GetObjectItemCaseSensitive((cJSON *)preview_events_obj, "accept_ready");
            endpoint_candidate_obj =
                cJSON_GetObjectItemCaseSensitive((cJSON *)preview_events_obj,
                                                 "endpoint_candidate");
            mode_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)preview_events_obj, "mode");

            if (cJSON_IsBool(enabled_obj)) {
                g_river_xiaozhi.discovery_preview_events_enabled = cJSON_IsTrue(enabled_obj);
            }
            if (cJSON_IsBool(speech_start_obj)) {
                g_river_xiaozhi.discovery_preview_speech_start = cJSON_IsTrue(speech_start_obj);
            }
            if (cJSON_IsBool(partial_obj)) {
                g_river_xiaozhi.discovery_preview_partial = cJSON_IsTrue(partial_obj);
            }
            if (cJSON_IsBool(accept_ready_obj)) {
                g_river_xiaozhi.discovery_preview_accept_ready = cJSON_IsTrue(accept_ready_obj);
            }
            if (cJSON_IsBool(endpoint_candidate_obj)) {
                g_river_xiaozhi.discovery_preview_endpoint_candidate =
                    cJSON_IsTrue(endpoint_candidate_obj);
            }
            if (cJSON_IsString(mode_obj) && mode_obj->valuestring != NULL) {
                river_xiaozhi_copy_optional_string(g_river_xiaozhi.discovery_preview_events_mode,
                                                   sizeof(g_river_xiaozhi.discovery_preview_events_mode),
                                                   mode_obj->valuestring);
            }
        }

        playback_ack_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)voice_collaboration_obj,
                                                            "playback_ack");
        if (cJSON_IsObject(playback_ack_obj)) {
            enabled_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)playback_ack_obj, "enabled");
            started_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)playback_ack_obj, "started");
            mark_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)playback_ack_obj, "mark");
            cleared_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)playback_ack_obj, "cleared");
            completed_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)playback_ack_obj,
                                                             "completed");
            mode_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)playback_ack_obj, "mode");

            if (cJSON_IsBool(enabled_obj)) {
                g_river_xiaozhi.discovery_playback_ack_enabled = cJSON_IsTrue(enabled_obj);
            }
            if (cJSON_IsBool(started_obj)) {
                g_river_xiaozhi.discovery_playback_ack_started = cJSON_IsTrue(started_obj);
            }
            if (cJSON_IsBool(mark_obj)) {
                g_river_xiaozhi.discovery_playback_ack_mark = cJSON_IsTrue(mark_obj);
            }
            if (cJSON_IsBool(cleared_obj)) {
                g_river_xiaozhi.discovery_playback_ack_cleared = cJSON_IsTrue(cleared_obj);
            }
            if (cJSON_IsBool(completed_obj)) {
                g_river_xiaozhi.discovery_playback_ack_completed = cJSON_IsTrue(completed_obj);
            }
            if (cJSON_IsString(mode_obj) && mode_obj->valuestring != NULL) {
                river_xiaozhi_copy_optional_string(g_river_xiaozhi.discovery_playback_ack_mode,
                                                   sizeof(g_river_xiaozhi.discovery_playback_ack_mode),
                                                   mode_obj->valuestring);
            }
        }
    }

    if (g_river_xiaozhi.discovery_playback_ack_enabled &&
        strcmp(g_river_xiaozhi.discovery_playback_ack_mode,
               "started_completed_v1") == 0) {
        g_river_xiaozhi.discovery_playback_ack_started = true;
        g_river_xiaozhi.discovery_playback_ack_mark = false;
        g_river_xiaozhi.discovery_playback_ack_cleared = true;
        g_river_xiaozhi.discovery_playback_ack_completed = true;
    }

    cJSON_Delete(root);
    return RIVER_OK;
}

static river_status_t river_xiaozhi_refresh_discovery_profile(void)
{
    char discovery_url[RIVER_XIAOZHI_URL_MAX];
    int status_code = 0;
    river_status_t status;

    if (g_river_xiaozhi.url[0] == '\0') {
        return RIVER_ERR_UNSUPPORTED;
    }

    status = river_xiaozhi_build_discovery_url(g_river_xiaozhi.url,
                                               discovery_url,
                                               sizeof(discovery_url));
    if (status != RIVER_OK) {
        return status;
    }

    status = river_xiaozhi_http_get_json(discovery_url,
                                         g_river_xiaozhi.token,
                                         g_river_xiaozhi.open_device_id,
                                         g_river_xiaozhi.open_client_id,
                                         g_river_xiaozhi.bootstrap_response,
                                         sizeof(g_river_xiaozhi.bootstrap_response),
                                         &status_code);
    if (status != RIVER_OK) {
        return status;
    }
    if (status_code != 200) {
        return RIVER_ERR_IO;
    }

    status = river_xiaozhi_parse_discovery_response(g_river_xiaozhi.bootstrap_response);
    if (status != RIVER_OK) {
        return status;
    }

    RIVER_LOGI("xiaozhi discovery ready: url=%s wire=%s subprotocol=%s product=%s mainline=%s turn_mode=%s server_endpoint=%s/%s voice_collaboration=%s preview_events=%s mode=%s playback_ack=%s mode=%s",
               discovery_url,
               river_xiaozhi_active_protocol_version(),
               river_xiaozhi_active_subprotocol(),
               river_xiaozhi_active_product_profile(),
               river_xiaozhi_active_mainline_profile(),
               river_xiaozhi_dash_if_empty(g_river_xiaozhi.discovery_turn_mode),
               river_xiaozhi_bool_text(g_river_xiaozhi.discovery_server_endpoint_available),
               river_xiaozhi_bool_text(g_river_xiaozhi.discovery_server_endpoint_enabled),
               river_xiaozhi_bool_text(g_river_xiaozhi.discovery_voice_collaboration_advertised),
               river_xiaozhi_bool_text(river_xiaozhi_discovery_preview_events_supported()),
               river_xiaozhi_dash_if_empty(g_river_xiaozhi.discovery_preview_events_mode),
               river_xiaozhi_bool_text(river_xiaozhi_discovery_playback_ack_mode() != NULL),
               river_xiaozhi_dash_if_empty(g_river_xiaozhi.discovery_playback_ack_mode));
    river_xiaozhi_log_collaboration_negotiation("discovery_refresh");
    return RIVER_OK;
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
    const char *new_url = NULL;
    bool ws_url_changed = false;

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

    new_url = url_obj->valuestring;
    ws_url_changed = (strcmp(g_river_xiaozhi.url, new_url) != 0);
    river_xiaozhi_copy_string(g_river_xiaozhi.url, sizeof(g_river_xiaozhi.url), new_url);
    river_xiaozhi_copy_string(g_river_xiaozhi.token,
                              sizeof(g_river_xiaozhi.token),
                              cJSON_IsString(token_obj) && token_obj->valuestring != NULL ?
                                  token_obj->valuestring :
                                  "");
    if (ws_url_changed) {
        river_xiaozhi_clear_discovery_profile();
    }

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
