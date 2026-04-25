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
