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

    RIVER_LOGI("xiaozhi init: ota_set=%s url_set=%s token_set=%s wire=%s subprotocol=%s product=%s mainline=%s protocol=%u mcp=%s device_id=%s client_id=%s",
               river_xiaozhi_bool_text(g_river_xiaozhi.ota_url[0] != '\0'),
               river_xiaozhi_bool_text(g_river_xiaozhi.url[0] != '\0'),
               river_xiaozhi_bool_text(g_river_xiaozhi.token[0] != '\0'),
               river_xiaozhi_active_protocol_version(),
               river_xiaozhi_active_subprotocol(),
               river_xiaozhi_active_product_profile(),
               river_xiaozhi_active_mainline_profile(),
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

    RIVER_LOGI("xiaozhi config updated: ota_set=%s url_set=%s token_set=%s wire=%s subprotocol=%s product=%s mainline=%s protocol=%u uplink=%luHz/%luch/%lums mcp=%s",
               river_xiaozhi_bool_text(g_river_xiaozhi.ota_url[0] != '\0'),
               river_xiaozhi_bool_text(g_river_xiaozhi.url[0] != '\0'),
               river_xiaozhi_bool_text(g_river_xiaozhi.token[0] != '\0'),
               river_xiaozhi_active_protocol_version(),
               river_xiaozhi_active_subprotocol(),
               river_xiaozhi_active_product_profile(),
               river_xiaozhi_active_mainline_profile(),
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

const char *river_xiaozhi_last_playback_output_lane(void)
{
    return (g_river_xiaozhi.initialized &&
            g_river_xiaozhi.last_playback_output_lane[0] != '\0') ?
               g_river_xiaozhi.last_playback_output_lane :
               NULL;
}

const char *river_xiaozhi_last_playback_output_role(void)
{
    return (g_river_xiaozhi.initialized &&
            g_river_xiaozhi.last_playback_output_role[0] != '\0') ?
               g_river_xiaozhi.last_playback_output_role :
               NULL;
}

const char *river_xiaozhi_last_playback_phrase_id(void)
{
    return (g_river_xiaozhi.initialized &&
            g_river_xiaozhi.last_playback_phrase_id[0] != '\0') ?
               g_river_xiaozhi.last_playback_phrase_id :
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
