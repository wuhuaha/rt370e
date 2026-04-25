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
