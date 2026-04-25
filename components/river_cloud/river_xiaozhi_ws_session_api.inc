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
