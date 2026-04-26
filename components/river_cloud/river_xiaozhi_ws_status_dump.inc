void river_xiaozhi_dump_status(void)
{
    uint32_t ready = 0U;
    uint32_t recycle = 0U;
    uint32_t max = 0U;
    uint32_t stable = 0U;
    uint32_t bootstrap_refresh_in_ms = 0U;
    uint32_t audio_soft_limit = 0U;
    uint64_t now_ms = 0U;
    bool locked = false;
    char session_update_age_ms[16];
    char accept_age_ms[16];
    char preview_speech_start_age_ms[16];
    char preview_update_age_ms[16];
    char endpoint_age_ms[16];
    char response_start_age_ms[16];
    char audio_meta_age_ms[16];
    char accept_from_preview_start_ms[16];
    char accept_from_preview_update_ms[16];
    char accept_from_endpoint_ms[16];
    char response_from_accept_ms[16];
    char response_from_preview_update_ms[16];
    char response_from_endpoint_ms[16];
    char audio_meta_from_response_start_ms[16];
    char audio_meta_from_accept_ms[16];

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
    now_ms = river_xiaozhi_now_ms();

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
    RIVER_LOGI("xiaozhi session_lane_state=%s input_state=%s output_state=%s barge_in_enabled=%s turn_id=%s accept_reason=%s",
               g_river_xiaozhi.last_session_state[0] != '\0' ?
                   g_river_xiaozhi.last_session_state :
                   "-",
               g_river_xiaozhi.last_input_state[0] != '\0' ?
                   g_river_xiaozhi.last_input_state :
                   "-",
               g_river_xiaozhi.last_output_state[0] != '\0' ?
                   g_river_xiaozhi.last_output_state :
                   "-",
               river_xiaozhi_optional_bool_text(g_river_xiaozhi.last_barge_in_enabled_known,
                                                g_river_xiaozhi.last_barge_in_enabled),
               g_river_xiaozhi.last_turn_id[0] != '\0' ?
                   g_river_xiaozhi.last_turn_id :
                   "-",
               g_river_xiaozhi.last_accept_reason[0] != '\0' ?
                   g_river_xiaozhi.last_accept_reason :
                   "-");
    RIVER_LOGI("xiaozhi timing_age_ms session_update=%s accept=%s preview_speech_start=%s preview_update=%s endpoint_candidate=%s response_start=%s audio_meta=%s",
               river_xiaozhi_format_elapsed_ms(session_update_age_ms,
                                               sizeof(session_update_age_ms),
                                               now_ms,
                                               g_river_xiaozhi.last_session_update_at_ms),
               river_xiaozhi_format_elapsed_ms(accept_age_ms,
                                               sizeof(accept_age_ms),
                                               now_ms,
                                               g_river_xiaozhi.last_accept_at_ms),
               river_xiaozhi_format_elapsed_ms(preview_speech_start_age_ms,
                                               sizeof(preview_speech_start_age_ms),
                                               now_ms,
                                               g_river_xiaozhi.last_preview_speech_start_at_ms),
               river_xiaozhi_format_elapsed_ms(preview_update_age_ms,
                                               sizeof(preview_update_age_ms),
                                               now_ms,
                                               g_river_xiaozhi.last_preview_update_at_ms),
               river_xiaozhi_format_elapsed_ms(endpoint_age_ms,
                                               sizeof(endpoint_age_ms),
                                               now_ms,
                                               g_river_xiaozhi.last_endpoint_candidate_at_ms),
               river_xiaozhi_format_elapsed_ms(response_start_age_ms,
                                               sizeof(response_start_age_ms),
                                               now_ms,
                                               g_river_xiaozhi.last_response_start_at_ms),
               river_xiaozhi_format_elapsed_ms(audio_meta_age_ms,
                                               sizeof(audio_meta_age_ms),
                                               now_ms,
                                               g_river_xiaozhi.last_audio_out_meta_at_ms));
    RIVER_LOGI("xiaozhi timing_chain_ms accept_from_preview_start=%s accept_from_preview_update=%s accept_from_endpoint=%s response_from_accept=%s response_from_preview_update=%s response_from_endpoint=%s audio_meta_from_response_start=%s audio_meta_from_accept=%s",
               river_xiaozhi_format_elapsed_ms(accept_from_preview_start_ms,
                                               sizeof(accept_from_preview_start_ms),
                                               g_river_xiaozhi.last_accept_at_ms,
                                               g_river_xiaozhi.last_preview_speech_start_at_ms),
               river_xiaozhi_format_elapsed_ms(accept_from_preview_update_ms,
                                               sizeof(accept_from_preview_update_ms),
                                               g_river_xiaozhi.last_accept_at_ms,
                                               g_river_xiaozhi.last_preview_update_at_ms),
               river_xiaozhi_format_elapsed_ms(accept_from_endpoint_ms,
                                               sizeof(accept_from_endpoint_ms),
                                               g_river_xiaozhi.last_accept_at_ms,
                                               g_river_xiaozhi.last_endpoint_candidate_at_ms),
               river_xiaozhi_format_elapsed_ms(response_from_accept_ms,
                                               sizeof(response_from_accept_ms),
                                               g_river_xiaozhi.last_response_start_at_ms,
                                               g_river_xiaozhi.last_accept_at_ms),
               river_xiaozhi_format_elapsed_ms(response_from_preview_update_ms,
                                               sizeof(response_from_preview_update_ms),
                                               g_river_xiaozhi.last_response_start_at_ms,
                                               g_river_xiaozhi.last_preview_update_at_ms),
               river_xiaozhi_format_elapsed_ms(response_from_endpoint_ms,
                                               sizeof(response_from_endpoint_ms),
                                               g_river_xiaozhi.last_response_start_at_ms,
                                               g_river_xiaozhi.last_endpoint_candidate_at_ms),
               river_xiaozhi_format_elapsed_ms(audio_meta_from_response_start_ms,
                                               sizeof(audio_meta_from_response_start_ms),
                                               g_river_xiaozhi.last_audio_out_meta_at_ms,
                                               g_river_xiaozhi.last_response_start_at_ms),
               river_xiaozhi_format_elapsed_ms(audio_meta_from_accept_ms,
                                               sizeof(audio_meta_from_accept_ms),
                                               g_river_xiaozhi.last_audio_out_meta_at_ms,
                                               g_river_xiaozhi.last_accept_at_ms));
    RIVER_LOGI("xiaozhi preview_state=preview_id=%s speech_started=%s text=%s stable_prefix=%s is_final=%s endpoint_candidate=%s endpoint_reason=%s source=%s audio_offset_ms=%lu preview_updates=%lu preview_logs=%lu/%lu",
               g_river_xiaozhi.last_preview_id[0] != '\0' ?
                   g_river_xiaozhi.last_preview_id :
                   "-",
               river_xiaozhi_bool_text(g_river_xiaozhi.last_preview_speech_started),
               g_river_xiaozhi.last_preview_text[0] != '\0' ?
                   g_river_xiaozhi.last_preview_text :
                   "-",
               g_river_xiaozhi.last_preview_stable_prefix[0] != '\0' ?
                   g_river_xiaozhi.last_preview_stable_prefix :
                   "-",
               river_xiaozhi_bool_text(g_river_xiaozhi.last_preview_final),
               river_xiaozhi_bool_text(g_river_xiaozhi.last_preview_endpoint_candidate),
               g_river_xiaozhi.last_preview_reason[0] != '\0' ?
                   g_river_xiaozhi.last_preview_reason :
                   "-",
               g_river_xiaozhi.last_preview_source[0] != '\0' ?
                   g_river_xiaozhi.last_preview_source :
                   "-",
               (unsigned long)g_river_xiaozhi.last_preview_audio_offset_ms,
               (unsigned long)g_river_xiaozhi.preview_update_events,
               (unsigned long)g_river_xiaozhi.preview_logs_emitted,
               (unsigned long)g_river_xiaozhi.preview_logs_suppressed);
    RIVER_LOGI("xiaozhi playback_meta=response_id=%s playback_id=%s segment_id=%s expected_duration_ms=%lu is_last_segment=%s output_lane=%s output_role=%s phrase_id=%s valid=%s",
               g_river_xiaozhi.last_response_id[0] != '\0' ?
                   g_river_xiaozhi.last_response_id :
                   "-",
               g_river_xiaozhi.last_playback_id[0] != '\0' ?
                   g_river_xiaozhi.last_playback_id :
                   "-",
               g_river_xiaozhi.last_segment_id[0] != '\0' ?
                   g_river_xiaozhi.last_segment_id :
                   "-",
               (unsigned long)g_river_xiaozhi.last_playback_expected_duration_ms,
               river_xiaozhi_bool_text(g_river_xiaozhi.last_playback_is_last_segment),
               g_river_xiaozhi.last_playback_output_lane[0] != '\0' ?
                   g_river_xiaozhi.last_playback_output_lane :
                   "-",
               g_river_xiaozhi.last_playback_output_role[0] != '\0' ?
                   g_river_xiaozhi.last_playback_output_role :
                   "-",
               g_river_xiaozhi.last_playback_phrase_id[0] != '\0' ?
                   g_river_xiaozhi.last_playback_phrase_id :
                   "-",
               river_xiaozhi_bool_text(river_xiaozhi_last_playback_meta_valid()));
    RIVER_LOGI("xiaozhi discovery turn_mode=%s server_endpoint=%s/%s mode=%s voice_collaboration=%s preview_events=%s declared_preview=%s preview_reason=%s playback_ack=%s declared_playback_ack=%s playback_ack_reason=%s default_on=%s default_reason=%s",
               g_river_xiaozhi.discovery_turn_mode[0] != '\0' ?
                   g_river_xiaozhi.discovery_turn_mode :
                   "-",
               river_xiaozhi_bool_text(g_river_xiaozhi.discovery_server_endpoint_available),
               river_xiaozhi_bool_text(g_river_xiaozhi.discovery_server_endpoint_enabled),
               g_river_xiaozhi.discovery_server_endpoint_mode[0] != '\0' ?
                   g_river_xiaozhi.discovery_server_endpoint_mode :
                   "-",
               river_xiaozhi_bool_text(g_river_xiaozhi.discovery_voice_collaboration_advertised),
               river_xiaozhi_discovery_preview_events_supported() ? "yes" : "no",
               river_xiaozhi_negotiated_preview_events_enabled() ? "yes" : "no",
               river_xiaozhi_preview_negotiation_reason() != NULL ?
                   river_xiaozhi_preview_negotiation_reason() :
                   "-",
               river_xiaozhi_discovery_playback_ack_mode() != NULL ?
                   river_xiaozhi_discovery_playback_ack_mode() :
                   "-",
               river_xiaozhi_negotiated_playback_ack_mode() != NULL ?
                   river_xiaozhi_negotiated_playback_ack_mode() :
                   "-",
               river_xiaozhi_playback_ack_negotiation_reason() != NULL ?
                   river_xiaozhi_playback_ack_negotiation_reason() :
                   "-",
               river_xiaozhi_bool_text(river_xiaozhi_duplex_default_on_allowed()),
               river_xiaozhi_duplex_default_fallback_reason() != NULL ?
                   river_xiaozhi_duplex_default_fallback_reason() :
                   "-");
    if (g_river_xiaozhi.activation_message[0] != '\0') {
        RIVER_LOGI("xiaozhi activation_message=%s challenge_set=%s timeout_ms=%lu",
                   g_river_xiaozhi.activation_message,
                   river_xiaozhi_bool_text(g_river_xiaozhi.activation_challenge[0] != '\0'),
                   (unsigned long)g_river_xiaozhi.activation_timeout_ms);
    }
}
