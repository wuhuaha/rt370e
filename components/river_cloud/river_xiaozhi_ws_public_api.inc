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
        if (strcmp(g_river_xiaozhi.url, config->url) != 0) {
            river_xiaozhi_clear_discovery_profile();
        }
        river_xiaozhi_copy_string(g_river_xiaozhi.url, sizeof(g_river_xiaozhi.url), config->url);
    }
    if (config->token != NULL) {
        manual_token_override = true;
        if (strcmp(g_river_xiaozhi.token, config->token) != 0) {
            river_xiaozhi_clear_discovery_profile();
        }
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

const char *river_xiaozhi_last_session_state(void)
{
    return (g_river_xiaozhi.initialized &&
            g_river_xiaozhi.last_session_state[0] != '\0') ?
               g_river_xiaozhi.last_session_state :
               NULL;
}

const char *river_xiaozhi_last_input_state(void)
{
    return (g_river_xiaozhi.initialized && g_river_xiaozhi.last_input_state[0] != '\0') ?
               g_river_xiaozhi.last_input_state :
               NULL;
}

const char *river_xiaozhi_last_output_state(void)
{
    return (g_river_xiaozhi.initialized &&
            g_river_xiaozhi.last_output_state[0] != '\0') ?
               g_river_xiaozhi.last_output_state :
               NULL;
}

const char *river_xiaozhi_last_turn_id(void)
{
    return (g_river_xiaozhi.initialized && g_river_xiaozhi.last_turn_id[0] != '\0') ?
               g_river_xiaozhi.last_turn_id :
               NULL;
}

const char *river_xiaozhi_last_accept_reason(void)
{
    return (g_river_xiaozhi.initialized &&
            g_river_xiaozhi.last_accept_reason[0] != '\0') ?
               g_river_xiaozhi.last_accept_reason :
               NULL;
}

bool river_xiaozhi_last_barge_in_enabled_known(void)
{
    return g_river_xiaozhi.initialized &&
           g_river_xiaozhi.last_barge_in_enabled_known;
}

bool river_xiaozhi_last_barge_in_enabled(void)
{
    return g_river_xiaozhi.initialized &&
           g_river_xiaozhi.last_barge_in_enabled_known &&
           g_river_xiaozhi.last_barge_in_enabled;
}

bool river_xiaozhi_discovery_voice_collaboration_advertised(void)
{
    return g_river_xiaozhi.initialized &&
           g_river_xiaozhi.discovery_voice_collaboration_advertised;
}

bool river_xiaozhi_discovery_server_endpoint_available(void)
{
    return g_river_xiaozhi.initialized &&
           g_river_xiaozhi.discovery_server_endpoint_available;
}

bool river_xiaozhi_discovery_server_endpoint_enabled(void)
{
    return g_river_xiaozhi.initialized &&
           g_river_xiaozhi.discovery_server_endpoint_enabled;
}

bool river_xiaozhi_preview_events_negotiated(void)
{
    return g_river_xiaozhi.initialized &&
           river_xiaozhi_negotiated_preview_events_enabled();
}

const char *river_xiaozhi_playback_ack_mode_negotiated(void)
{
    if (!g_river_xiaozhi.initialized) {
        return NULL;
    }
    return river_xiaozhi_negotiated_playback_ack_mode();
}

void river_xiaozhi_clear_session_update_cache(void)
{
    if (!g_river_xiaozhi.initialized) {
        return;
    }

    river_xiaozhi_clear_last_session_update_fields();
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
    status = river_xiaozhi_refresh_discovery_profile();
    if (status != RIVER_OK) {
        RIVER_LOGW("xiaozhi discovery refresh failed; using compatibility baseline: status=%d ws=%s",
                   (int)status,
                   g_river_xiaozhi.url);
        river_xiaozhi_log_collaboration_negotiation("discovery_refresh_failed");
    }

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

river_status_t river_xiaozhi_send_audio_out_started(const char *response_id,
                                                    const char *playback_id,
                                                    const char *segment_id)
{
    return river_xiaozhi_send_audio_out_started_internal(response_id,
                                                         playback_id,
                                                         segment_id);
}

river_status_t river_xiaozhi_send_audio_out_mark(const char *response_id,
                                                 const char *playback_id,
                                                 const char *segment_id,
                                                 uint32_t played_duration_ms)
{
    return river_xiaozhi_send_audio_out_mark_internal(response_id,
                                                      playback_id,
                                                      segment_id,
                                                      played_duration_ms);
}

river_status_t river_xiaozhi_send_audio_out_cleared(const char *response_id,
                                                    const char *playback_id,
                                                    const char *cleared_after_segment_id,
                                                    const char *reason)
{
    return river_xiaozhi_send_audio_out_cleared_internal(response_id,
                                                         playback_id,
                                                         cleared_after_segment_id,
                                                         reason);
}

river_status_t river_xiaozhi_send_audio_out_completed(const char *response_id,
                                                      const char *playback_id)
{
    return river_xiaozhi_send_audio_out_completed_internal(response_id, playback_id);
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

#include "river_xiaozhi_ws_status_dump.inc"
