/* XiaoZhi preview receive-path handlers. Included by river_xiaozhi_ws.c so
 * the high-frequency preview state stays close to its private transport truth. */
static void river_xiaozhi_handle_realtime_input_speech_start(const cJSON *payload)
{
    const cJSON *preview_id_obj;
    const cJSON *audio_offset_ms_obj;
    const cJSON *source_obj;
    const char *preview_id = NULL;
    const char *source = NULL;
    uint64_t now_ms;
    uint32_t audio_offset_ms = 0U;
    char since_response_start_ms[16];
    char since_audio_meta_ms[16];

    preview_id_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "preview_id");
    audio_offset_ms_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "audio_offset_ms");
    source_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "source");
    if (cJSON_IsString(preview_id_obj) && preview_id_obj->valuestring != NULL &&
        preview_id_obj->valuestring[0] != '\0') {
        preview_id = preview_id_obj->valuestring;
    }
    if (cJSON_IsString(source_obj) && source_obj->valuestring != NULL &&
        source_obj->valuestring[0] != '\0') {
        source = source_obj->valuestring;
    }
    if (cJSON_IsNumber(audio_offset_ms_obj) && audio_offset_ms_obj->valuedouble > 0.0) {
        audio_offset_ms = (uint32_t)audio_offset_ms_obj->valuedouble;
    }

    now_ms = river_xiaozhi_now_ms();
    river_xiaozhi_set_last_type("input.speech.start");
    river_xiaozhi_prepare_preview_window(preview_id);
    river_xiaozhi_set_last_preview_id(preview_id);
    river_xiaozhi_set_last_preview_source(source);
    g_river_xiaozhi.last_preview_speech_started = true;
    g_river_xiaozhi.last_preview_audio_offset_ms = audio_offset_ms;
    g_river_xiaozhi.last_preview_speech_start_at_ms = now_ms;
    if (!river_xiaozhi_negotiated_preview_events_enabled()) {
        RIVER_LOGW("xiaozhi input.speech.start arrived before preview negotiation: sid=%s reason=%s preview_id=%s source=%s",
                   g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
                   river_xiaozhi_preview_negotiation_reason() != NULL ?
                       river_xiaozhi_preview_negotiation_reason() :
                       "-",
                   river_xiaozhi_dash_if_empty(preview_id),
                   river_xiaozhi_dash_if_empty(source));
    }
    if (preview_id == NULL) {
        RIVER_LOGW("xiaozhi input.speech.start missing preview_id: sid=%s source=%s audio_offset_ms=%lu",
                   g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
                   river_xiaozhi_dash_if_empty(source),
                   (unsigned long)audio_offset_ms);
    }

    RIVER_LOGI("xiaozhi input.speech.start: sid=%s preview_id=%s audio_offset_ms=%lu source=%s since_response_start_ms=%s since_audio_meta_ms=%s",
               g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
               river_xiaozhi_dash_if_empty(preview_id),
               (unsigned long)audio_offset_ms,
               river_xiaozhi_dash_if_empty(source),
               river_xiaozhi_format_elapsed_ms(since_response_start_ms,
                                               sizeof(since_response_start_ms),
                                               now_ms,
                                               g_river_xiaozhi.last_response_start_at_ms),
               river_xiaozhi_format_elapsed_ms(since_audio_meta_ms,
                                               sizeof(since_audio_meta_ms),
                                               now_ms,
                                               g_river_xiaozhi.last_audio_out_meta_at_ms));
    river_xiaozhi_emit_event(RIVER_XIAOZHI_EVENT_INPUT_SPEECH_START,
                             NULL,
                             NULL,
                             NULL,
                             preview_id,
                             NULL,
                             NULL,
                             source,
                             NULL,
                             NULL,
                             NULL,
                             0U,
                             0U,
                             0U,
                             audio_offset_ms,
                             0U,
                             NULL,
                             0U,
                             0U,
                             false,
                             false,
                             false);
}

static void river_xiaozhi_handle_realtime_input_preview(const cJSON *payload)
{
    const cJSON *preview_id_obj;
    const cJSON *text_obj;
    const cJSON *stable_prefix_obj;
    const cJSON *is_final_obj;
    const cJSON *audio_offset_ms_obj;
    const char *preview_id = NULL;
    const char *text = NULL;
    const char *stable_prefix = NULL;
    uint64_t now_ms;
    uint32_t audio_offset_ms = 0U;
    bool is_final = false;
    bool stable_prefix_changed = false;
    bool log_preview_update = false;
    char since_speech_start_ms[16];
    char since_accept_ms[16];

    preview_id_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "preview_id");
    text_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "text");
    stable_prefix_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "stable_prefix");
    is_final_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "is_final");
    audio_offset_ms_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "audio_offset_ms");
    if (cJSON_IsString(preview_id_obj) && preview_id_obj->valuestring != NULL &&
        preview_id_obj->valuestring[0] != '\0') {
        preview_id = preview_id_obj->valuestring;
    }
    if (cJSON_IsString(text_obj) && text_obj->valuestring != NULL &&
        text_obj->valuestring[0] != '\0') {
        text = text_obj->valuestring;
    }
    if (cJSON_IsString(stable_prefix_obj) && stable_prefix_obj->valuestring != NULL &&
        stable_prefix_obj->valuestring[0] != '\0') {
        stable_prefix = stable_prefix_obj->valuestring;
    }
    if (cJSON_IsBool(is_final_obj)) {
        is_final = cJSON_IsTrue(is_final_obj);
    }
    if (cJSON_IsNumber(audio_offset_ms_obj) && audio_offset_ms_obj->valuedouble > 0.0) {
        audio_offset_ms = (uint32_t)audio_offset_ms_obj->valuedouble;
    }

    now_ms = river_xiaozhi_now_ms();
    river_xiaozhi_set_last_type("input.preview");
    river_xiaozhi_prepare_preview_window(preview_id);
    stable_prefix_changed =
        stable_prefix != NULL &&
        strcmp(g_river_xiaozhi.last_preview_stable_prefix, stable_prefix) != 0;
    river_xiaozhi_set_last_preview_id(preview_id);
    river_xiaozhi_set_last_preview_text(text);
    river_xiaozhi_set_last_preview_stable_prefix(stable_prefix);
    g_river_xiaozhi.last_preview_final = is_final;
    g_river_xiaozhi.last_preview_audio_offset_ms = audio_offset_ms;
    g_river_xiaozhi.last_preview_update_at_ms = now_ms;
    if (g_river_xiaozhi.preview_update_events < UINT32_MAX) {
        g_river_xiaozhi.preview_update_events++;
    }
    log_preview_update =
        river_xiaozhi_should_log_preview_update(now_ms,
                                                is_final,
                                                stable_prefix_changed);
    if (!river_xiaozhi_negotiated_preview_events_enabled()) {
        RIVER_LOGW("xiaozhi input.preview arrived before preview negotiation: sid=%s reason=%s preview_id=%s",
                   g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
                   river_xiaozhi_preview_negotiation_reason() != NULL ?
                       river_xiaozhi_preview_negotiation_reason() :
                       "-",
                   river_xiaozhi_dash_if_empty(preview_id));
    }
    if (preview_id == NULL || text == NULL) {
        RIVER_LOGW("xiaozhi input.preview sparse payload: sid=%s preview_id=%s text=%s stable_prefix=%s is_final=%s",
                   g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
                   river_xiaozhi_dash_if_empty(preview_id),
                   river_xiaozhi_dash_if_empty(text),
                   river_xiaozhi_dash_if_empty(stable_prefix),
                   river_xiaozhi_bool_text(is_final));
    }

    if (log_preview_update) {
        RIVER_LOGI("xiaozhi input.preview: sid=%s preview_id=%s text=%s stable_prefix=%s stable_changed=%s is_final=%s audio_offset_ms=%lu since_speech_start_ms=%s since_accept_ms=%s suppressed=%lu",
                   g_river_xiaozhi.session_id[0] != '\0' ?
                       g_river_xiaozhi.session_id :
                       "-",
                   river_xiaozhi_dash_if_empty(preview_id),
                   river_xiaozhi_dash_if_empty(text),
                   river_xiaozhi_dash_if_empty(stable_prefix),
                   river_xiaozhi_bool_text(stable_prefix_changed),
                   river_xiaozhi_bool_text(is_final),
                   (unsigned long)audio_offset_ms,
                   river_xiaozhi_format_elapsed_ms(since_speech_start_ms,
                                                   sizeof(since_speech_start_ms),
                                                   now_ms,
                                                   g_river_xiaozhi.last_preview_speech_start_at_ms),
                   river_xiaozhi_format_elapsed_ms(since_accept_ms,
                                                   sizeof(since_accept_ms),
                                                   now_ms,
                                                   g_river_xiaozhi.last_accept_at_ms),
                   (unsigned long)g_river_xiaozhi.preview_logs_suppressed);
    }
    river_xiaozhi_emit_event(RIVER_XIAOZHI_EVENT_INPUT_PREVIEW,
                             text,
                             NULL,
                             NULL,
                             preview_id,
                             stable_prefix,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             0U,
                             0U,
                             0U,
                             audio_offset_ms,
                             0U,
                             NULL,
                             0U,
                             0U,
                             false,
                             is_final,
                             false);
}

static void river_xiaozhi_handle_realtime_input_accept_ready(const cJSON *payload)
{
    const cJSON *preview_id_obj;
    const cJSON *reason_obj;
    const cJSON *audio_offset_ms_obj;
    const char *preview_id = NULL;
    const char *reason = NULL;
    uint64_t now_ms;
    uint32_t audio_offset_ms = 0U;
    char since_speech_start_ms[16];
    char since_preview_update_ms[16];

    preview_id_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "preview_id");
    reason_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "reason");
    audio_offset_ms_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "audio_offset_ms");
    if (cJSON_IsString(preview_id_obj) && preview_id_obj->valuestring != NULL &&
        preview_id_obj->valuestring[0] != '\0') {
        preview_id = preview_id_obj->valuestring;
    }
    if (cJSON_IsString(reason_obj) && reason_obj->valuestring != NULL &&
        reason_obj->valuestring[0] != '\0') {
        reason = reason_obj->valuestring;
    }
    if (cJSON_IsNumber(audio_offset_ms_obj) && audio_offset_ms_obj->valuedouble > 0.0) {
        audio_offset_ms = (uint32_t)audio_offset_ms_obj->valuedouble;
    }

    now_ms = river_xiaozhi_now_ms();
    river_xiaozhi_set_last_type("input.accept_ready");
    river_xiaozhi_prepare_preview_window(preview_id);
    river_xiaozhi_set_last_preview_id(preview_id);
    river_xiaozhi_copy_optional_string(g_river_xiaozhi.last_accept_ready_reason,
                                       sizeof(g_river_xiaozhi.last_accept_ready_reason),
                                       reason);
    g_river_xiaozhi.last_preview_accept_ready = true;
    g_river_xiaozhi.last_preview_audio_offset_ms = audio_offset_ms;
    g_river_xiaozhi.last_accept_ready_at_ms = now_ms;
    if (!river_xiaozhi_negotiated_preview_events_enabled()) {
        RIVER_LOGW("xiaozhi input.accept_ready arrived before preview negotiation: sid=%s reason=%s preview_id=%s",
                   g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
                   river_xiaozhi_preview_negotiation_reason() != NULL ?
                       river_xiaozhi_preview_negotiation_reason() :
                       "-",
                   river_xiaozhi_dash_if_empty(preview_id));
    }
    if (preview_id == NULL || reason == NULL) {
        RIVER_LOGW("xiaozhi input.accept_ready sparse payload: sid=%s preview_id=%s reason=%s audio_offset_ms=%lu",
                   g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
                   river_xiaozhi_dash_if_empty(preview_id),
                   river_xiaozhi_dash_if_empty(reason),
                   (unsigned long)audio_offset_ms);
    }

    RIVER_LOGI("xiaozhi input.accept_ready: sid=%s preview_id=%s reason=%s audio_offset_ms=%lu since_speech_start_ms=%s since_preview_update_ms=%s",
               g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
               river_xiaozhi_dash_if_empty(preview_id),
               river_xiaozhi_dash_if_empty(reason),
               (unsigned long)audio_offset_ms,
               river_xiaozhi_format_elapsed_ms(since_speech_start_ms,
                                               sizeof(since_speech_start_ms),
                                               now_ms,
                                               g_river_xiaozhi.last_preview_speech_start_at_ms),
               river_xiaozhi_format_elapsed_ms(since_preview_update_ms,
                                               sizeof(since_preview_update_ms),
                                               now_ms,
                                               g_river_xiaozhi.last_preview_update_at_ms));
    river_xiaozhi_emit_event(RIVER_XIAOZHI_EVENT_INPUT_ACCEPT_READY,
                             NULL,
                             NULL,
                             NULL,
                             preview_id,
                             NULL,
                             reason,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             0U,
                             0U,
                             0U,
                             audio_offset_ms,
                             0U,
                             NULL,
                             0U,
                             0U,
                             false,
                             false,
                             false);
}

static void river_xiaozhi_handle_realtime_input_endpoint(const cJSON *payload)
{
    const cJSON *preview_id_obj;
    const cJSON *candidate_obj;
    const cJSON *reason_obj;
    const cJSON *audio_offset_ms_obj;
    const char *preview_id = NULL;
    const char *reason = NULL;
    uint64_t now_ms;
    uint32_t audio_offset_ms = 0U;
    bool candidate = false;
    char since_speech_start_ms[16];
    char since_preview_update_ms[16];

    preview_id_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "preview_id");
    candidate_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "candidate");
    reason_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "reason");
    audio_offset_ms_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "audio_offset_ms");
    if (cJSON_IsString(preview_id_obj) && preview_id_obj->valuestring != NULL &&
        preview_id_obj->valuestring[0] != '\0') {
        preview_id = preview_id_obj->valuestring;
    }
    if (cJSON_IsString(reason_obj) && reason_obj->valuestring != NULL &&
        reason_obj->valuestring[0] != '\0') {
        reason = reason_obj->valuestring;
    }
    if (cJSON_IsBool(candidate_obj)) {
        candidate = cJSON_IsTrue(candidate_obj);
    }
    if (cJSON_IsNumber(audio_offset_ms_obj) && audio_offset_ms_obj->valuedouble > 0.0) {
        audio_offset_ms = (uint32_t)audio_offset_ms_obj->valuedouble;
    }

    now_ms = river_xiaozhi_now_ms();
    river_xiaozhi_set_last_type("input.endpoint");
    river_xiaozhi_prepare_preview_window(preview_id);
    river_xiaozhi_set_last_preview_id(preview_id);
    river_xiaozhi_set_last_preview_reason(reason);
    g_river_xiaozhi.last_preview_endpoint_candidate = candidate;
    g_river_xiaozhi.last_preview_audio_offset_ms = audio_offset_ms;
    if (candidate) {
        g_river_xiaozhi.last_endpoint_candidate_at_ms = now_ms;
    }
    if (!river_xiaozhi_negotiated_preview_events_enabled()) {
        RIVER_LOGW("xiaozhi input.endpoint arrived before preview negotiation: sid=%s reason=%s preview_id=%s",
                   g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
                   river_xiaozhi_preview_negotiation_reason() != NULL ?
                       river_xiaozhi_preview_negotiation_reason() :
                       "-",
                   river_xiaozhi_dash_if_empty(preview_id));
    }
    if (preview_id == NULL || reason == NULL) {
        RIVER_LOGW("xiaozhi input.endpoint sparse payload: sid=%s preview_id=%s candidate=%s reason=%s audio_offset_ms=%lu",
                   g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
                   river_xiaozhi_dash_if_empty(preview_id),
                   river_xiaozhi_bool_text(candidate),
                   river_xiaozhi_dash_if_empty(reason),
                   (unsigned long)audio_offset_ms);
    }

    RIVER_LOGI("xiaozhi input.endpoint: sid=%s preview_id=%s candidate=%s reason=%s audio_offset_ms=%lu since_speech_start_ms=%s since_preview_update_ms=%s",
               g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
               river_xiaozhi_dash_if_empty(preview_id),
               river_xiaozhi_bool_text(candidate),
               river_xiaozhi_dash_if_empty(reason),
               (unsigned long)audio_offset_ms,
               river_xiaozhi_format_elapsed_ms(since_speech_start_ms,
                                               sizeof(since_speech_start_ms),
                                               now_ms,
                                               g_river_xiaozhi.last_preview_speech_start_at_ms),
               river_xiaozhi_format_elapsed_ms(since_preview_update_ms,
                                               sizeof(since_preview_update_ms),
                                               now_ms,
                                               g_river_xiaozhi.last_preview_update_at_ms));
    river_xiaozhi_emit_event(RIVER_XIAOZHI_EVENT_INPUT_ENDPOINT,
                             NULL,
                             NULL,
                             NULL,
                             preview_id,
                             NULL,
                             reason,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             0U,
                             0U,
                             0U,
                             audio_offset_ms,
                             0U,
                             NULL,
                             0U,
                             0U,
                             candidate,
                             false,
                             false);
}
