static void river_xiaozhi_handle_realtime_session_update(const cJSON *payload)
{
    const cJSON *state_obj;
    const cJSON *input_state_obj;
    const cJSON *output_state_obj;
    const cJSON *turn_id_obj;
    const cJSON *accept_reason_obj;
    const cJSON *barge_in_enabled_obj;
    const char *state = NULL;
    const char *input_state = NULL;
    const char *output_state = NULL;
    const char *turn_id = NULL;
    const char *accept_reason = NULL;
    uint64_t now_ms;
    uint64_t previous_session_update_at_ms;
    bool turn_id_changed = false;
    bool accept_reason_changed = false;
    bool accept_latched = false;
    bool barge_in_enabled = false;
    bool barge_in_enabled_known = false;
    char since_prev_update_ms[16];
    char since_accept_ms[16];
    char accept_from_preview_start_ms[16];
    char accept_from_preview_update_ms[16];
    char accept_from_endpoint_ms[16];

    state_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "state");
    input_state_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "input_state");
    output_state_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "output_state");
    turn_id_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "turn_id");
    accept_reason_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "accept_reason");
    barge_in_enabled_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload,
                                                            "barge_in_enabled");
    if (cJSON_IsString(state_obj) && state_obj->valuestring != NULL) {
        state = state_obj->valuestring;
    }
    if (cJSON_IsString(input_state_obj) && input_state_obj->valuestring != NULL &&
        input_state_obj->valuestring[0] != '\0') {
        input_state = input_state_obj->valuestring;
    }
    if (cJSON_IsString(output_state_obj) && output_state_obj->valuestring != NULL &&
        output_state_obj->valuestring[0] != '\0') {
        output_state = output_state_obj->valuestring;
    }
    if (cJSON_IsString(turn_id_obj) && turn_id_obj->valuestring != NULL &&
        turn_id_obj->valuestring[0] != '\0') {
        turn_id = turn_id_obj->valuestring;
    }
    if (cJSON_IsString(accept_reason_obj) && accept_reason_obj->valuestring != NULL &&
        accept_reason_obj->valuestring[0] != '\0') {
        accept_reason = accept_reason_obj->valuestring;
    }
    if (cJSON_IsBool(barge_in_enabled_obj)) {
        barge_in_enabled_known = true;
        barge_in_enabled = cJSON_IsTrue(barge_in_enabled_obj);
    }

    now_ms = river_xiaozhi_now_ms();
    previous_session_update_at_ms = g_river_xiaozhi.last_session_update_at_ms;
    if (turn_id != NULL && turn_id[0] != '\0' &&
        strcmp(g_river_xiaozhi.last_turn_id, turn_id) != 0) {
        turn_id_changed = true;
    }
    if (accept_reason != NULL && accept_reason[0] != '\0' &&
        strcmp(g_river_xiaozhi.last_accept_reason, accept_reason) != 0) {
        accept_reason_changed = true;
    }

    river_xiaozhi_set_last_type("session.update");
    river_xiaozhi_set_last_state(state);
    if (state != NULL && state[0] != '\0') {
        river_xiaozhi_set_last_session_state(state);
    }
    if (input_state != NULL && input_state[0] != '\0') {
        river_xiaozhi_set_last_input_state(input_state);
    }
    if (output_state != NULL && output_state[0] != '\0') {
        river_xiaozhi_set_last_output_state(output_state);
    }
    if (turn_id != NULL && turn_id[0] != '\0') {
        river_xiaozhi_set_last_turn_id(turn_id);
    }
    if (accept_reason != NULL && accept_reason[0] != '\0') {
        if (g_river_xiaozhi.last_accept_at_ms == 0U || accept_reason_changed ||
            turn_id_changed) {
            g_river_xiaozhi.last_accept_at_ms = now_ms;
            accept_latched = true;
        }
        river_xiaozhi_set_last_accept_reason(accept_reason);
    }
    if (barge_in_enabled_known) {
        river_xiaozhi_set_last_barge_in_enabled(true, barge_in_enabled);
    }
    g_river_xiaozhi.last_session_update_at_ms = now_ms;
    if (turn_id == NULL && input_state == NULL && output_state == NULL &&
        accept_reason == NULL && !barge_in_enabled_known) {
        RIVER_LOGI("xiaozhi session.update semantic sparse: sid=%s state=%s response_started=%s",
                   g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
                   river_xiaozhi_dash_if_empty(state),
                   river_xiaozhi_bool_text(g_river_xiaozhi.response_started));
    }
    RIVER_LOGI("xiaozhi session.update: sid=%s state=%s input_state=%s output_state=%s barge_in_enabled=%s accept_reason=%s turn_id=%s response_started=%s since_prev_update_ms=%s since_accept_ms=%s",
               g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
               river_xiaozhi_dash_if_empty(state),
               river_xiaozhi_dash_if_empty(input_state),
               river_xiaozhi_dash_if_empty(output_state),
               river_xiaozhi_optional_bool_text(barge_in_enabled_known, barge_in_enabled),
               river_xiaozhi_dash_if_empty(accept_reason),
               river_xiaozhi_dash_if_empty(turn_id),
               river_xiaozhi_bool_text(g_river_xiaozhi.response_started),
               river_xiaozhi_format_elapsed_ms(since_prev_update_ms,
                                               sizeof(since_prev_update_ms),
                                               now_ms,
                                               previous_session_update_at_ms),
               river_xiaozhi_format_elapsed_ms(since_accept_ms,
                                               sizeof(since_accept_ms),
                                               now_ms,
                                               g_river_xiaozhi.last_accept_at_ms));
    if (accept_latched) {
        RIVER_LOGI("xiaozhi accept latched: sid=%s accept_reason=%s turn_id=%s preview_id=%s from_preview_start_ms=%s from_preview_update_ms=%s from_endpoint_ms=%s",
                   g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
                   river_xiaozhi_dash_if_empty(g_river_xiaozhi.last_accept_reason),
                   river_xiaozhi_dash_if_empty(g_river_xiaozhi.last_turn_id),
                   river_xiaozhi_dash_if_empty(g_river_xiaozhi.last_preview_id),
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
                                                   g_river_xiaozhi.last_endpoint_candidate_at_ms));
    }

    if (state != NULL && strcmp(state, "active") == 0 &&
        g_river_xiaozhi.response_started) {
        g_river_xiaozhi.response_started = false;
        river_xiaozhi_emit_tts_event("stop", NULL);
    }
}

#include "river_xiaozhi_ws_preview_handlers.inc"

static void river_xiaozhi_handle_realtime_audio_out_meta(const cJSON *payload)
{
    const cJSON *response_id_obj;
    const cJSON *playback_id_obj;
    const cJSON *segment_id_obj;
    const cJSON *text_obj;
    const cJSON *expected_duration_ms_obj;
    const cJSON *is_last_segment_obj;
    const char *response_id = NULL;
    const char *playback_id = NULL;
    const char *segment_id = NULL;
    const char *text = NULL;
    uint64_t now_ms;
    uint32_t expected_duration_ms = 0U;
    bool is_last_segment = false;
    char meta_from_response_start_ms[16];
    char meta_from_accept_ms[16];
    char meta_from_preview_update_ms[16];
    char meta_from_endpoint_ms[16];

    response_id_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "response_id");
    playback_id_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "playback_id");
    segment_id_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "segment_id");
    text_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "text");
    expected_duration_ms_obj =
        cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "expected_duration_ms");
    is_last_segment_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload,
                                                           "is_last_segment");
    if (cJSON_IsString(response_id_obj) && response_id_obj->valuestring != NULL &&
        response_id_obj->valuestring[0] != '\0') {
        response_id = response_id_obj->valuestring;
    }
    if (cJSON_IsString(playback_id_obj) && playback_id_obj->valuestring != NULL &&
        playback_id_obj->valuestring[0] != '\0') {
        playback_id = playback_id_obj->valuestring;
    }
    if (cJSON_IsString(segment_id_obj) && segment_id_obj->valuestring != NULL &&
        segment_id_obj->valuestring[0] != '\0') {
        segment_id = segment_id_obj->valuestring;
    }
    if (cJSON_IsString(text_obj) && text_obj->valuestring != NULL &&
        text_obj->valuestring[0] != '\0') {
        text = text_obj->valuestring;
    }
    if (cJSON_IsNumber(expected_duration_ms_obj) &&
        expected_duration_ms_obj->valuedouble > 0.0) {
        expected_duration_ms = (uint32_t)expected_duration_ms_obj->valuedouble;
    }
    if (cJSON_IsBool(is_last_segment_obj)) {
        is_last_segment = cJSON_IsTrue(is_last_segment_obj);
    }

    now_ms = river_xiaozhi_now_ms();
    river_xiaozhi_set_last_type("audio.out.meta");
    river_xiaozhi_clear_last_playback_fields();
    river_xiaozhi_set_last_response_id(response_id);
    river_xiaozhi_set_last_playback_id(playback_id);
    river_xiaozhi_set_last_segment_id(segment_id);
    g_river_xiaozhi.last_playback_expected_duration_ms = expected_duration_ms;
    g_river_xiaozhi.last_playback_is_last_segment = is_last_segment;
    g_river_xiaozhi.last_audio_out_meta_at_ms = now_ms;

    RIVER_LOGI("xiaozhi audio.out.meta: sid=%s response_id=%s playback_id=%s segment_id=%s expected_duration_ms=%lu is_last_segment=%s text=%s from_response_start_ms=%s from_accept_ms=%s from_preview_update_ms=%s from_endpoint_ms=%s",
               g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
               river_xiaozhi_dash_if_empty(response_id),
               river_xiaozhi_dash_if_empty(playback_id),
               river_xiaozhi_dash_if_empty(segment_id),
               (unsigned long)expected_duration_ms,
               river_xiaozhi_bool_text(is_last_segment),
               river_xiaozhi_dash_if_empty(text),
               river_xiaozhi_format_elapsed_ms(meta_from_response_start_ms,
                                               sizeof(meta_from_response_start_ms),
                                               now_ms,
                                               g_river_xiaozhi.last_response_start_at_ms),
               river_xiaozhi_format_elapsed_ms(meta_from_accept_ms,
                                               sizeof(meta_from_accept_ms),
                                               now_ms,
                                               g_river_xiaozhi.last_accept_at_ms),
               river_xiaozhi_format_elapsed_ms(meta_from_preview_update_ms,
                                               sizeof(meta_from_preview_update_ms),
                                               now_ms,
                                               g_river_xiaozhi.last_preview_update_at_ms),
               river_xiaozhi_format_elapsed_ms(meta_from_endpoint_ms,
                                               sizeof(meta_from_endpoint_ms),
                                               now_ms,
                                               g_river_xiaozhi.last_endpoint_candidate_at_ms));
    river_xiaozhi_emit_event(RIVER_XIAOZHI_EVENT_AUDIO_OUT_META,
                             text,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             response_id,
                             playback_id,
                             segment_id,
                             0U,
                             0U,
                             0U,
                             0U,
                             expected_duration_ms,
                             NULL,
                             0U,
                             0U,
                             false,
                             false,
                             is_last_segment);
}

static void river_xiaozhi_handle_realtime_response_start(const cJSON *payload)
{
    const cJSON *response_id_obj;
    const char *response_id = NULL;
    uint64_t now_ms;
    char response_from_accept_ms[16];
    char response_from_preview_start_ms[16];
    char response_from_preview_update_ms[16];
    char response_from_endpoint_ms[16];

    response_id_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "response_id");
    if (cJSON_IsString(response_id_obj) && response_id_obj->valuestring != NULL) {
        response_id = response_id_obj->valuestring;
    }

    now_ms = river_xiaozhi_now_ms();
    g_river_xiaozhi.response_started = true;
    river_xiaozhi_set_last_type("response.start");
    river_xiaozhi_clear_last_playback_fields();
    river_xiaozhi_set_last_response_id(response_id);
    g_river_xiaozhi.last_response_start_at_ms = now_ms;
    RIVER_LOGI("xiaozhi response.start: sid=%s response_id=%s from_accept_ms=%s from_preview_start_ms=%s from_preview_update_ms=%s from_endpoint_ms=%s",
               g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
               response_id != NULL ? response_id : "-",
               river_xiaozhi_format_elapsed_ms(response_from_accept_ms,
                                               sizeof(response_from_accept_ms),
                                               now_ms,
                                               g_river_xiaozhi.last_accept_at_ms),
               river_xiaozhi_format_elapsed_ms(response_from_preview_start_ms,
                                               sizeof(response_from_preview_start_ms),
                                               now_ms,
                                               g_river_xiaozhi.last_preview_speech_start_at_ms),
               river_xiaozhi_format_elapsed_ms(response_from_preview_update_ms,
                                               sizeof(response_from_preview_update_ms),
                                               now_ms,
                                               g_river_xiaozhi.last_preview_update_at_ms),
               river_xiaozhi_format_elapsed_ms(response_from_endpoint_ms,
                                               sizeof(response_from_endpoint_ms),
                                               now_ms,
                                               g_river_xiaozhi.last_endpoint_candidate_at_ms));
    river_xiaozhi_emit_event(RIVER_XIAOZHI_EVENT_RESPONSE_START,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             NULL,
                             response_id,
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
    river_xiaozhi_emit_tts_event("start", NULL);
}

static void river_xiaozhi_handle_realtime_response_chunk(const cJSON *payload)
{
    const cJSON *delta_type_obj;
    const cJSON *text_obj;
    const char *delta_type = NULL;
    const char *text = NULL;

    delta_type_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "delta_type");
    text_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "text");
    if (cJSON_IsString(delta_type_obj) && delta_type_obj->valuestring != NULL) {
        delta_type = delta_type_obj->valuestring;
    }
    if (cJSON_IsString(text_obj) && text_obj->valuestring != NULL) {
        text = text_obj->valuestring;
    }

    river_xiaozhi_set_last_type("response.chunk");
    if (delta_type != NULL && strcmp(delta_type, "text") != 0) {
        RIVER_LOGI("xiaozhi response.chunk ignore: sid=%s delta_type=%s",
                   g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
                   delta_type);
        return;
    }
    if (text == NULL || text[0] == '\0') {
        return;
    }

    if (!g_river_xiaozhi.response_started) {
        g_river_xiaozhi.response_started = true;
        river_xiaozhi_emit_tts_event("start", NULL);
    }

    RIVER_LOGI("xiaozhi response.chunk: sid=%s text=%s",
               g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
               text);
    river_xiaozhi_emit_tts_event("sentence_start", text);
}

static void river_xiaozhi_handle_realtime_session_end(const cJSON *payload)
{
    const cJSON *reason_obj;
    const cJSON *message_obj;
    char ended_sid[RIVER_XIAOZHI_SESSION_ID_MAX];
    const char *reason = NULL;
    const char *message = NULL;

    river_xiaozhi_copy_string(ended_sid, sizeof(ended_sid), g_river_xiaozhi.session_id);

    reason_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "reason");
    message_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "message");
    if (cJSON_IsString(reason_obj) && reason_obj->valuestring != NULL) {
        reason = reason_obj->valuestring;
    }
    if (cJSON_IsString(message_obj) && message_obj->valuestring != NULL) {
        message = message_obj->valuestring;
    }

    river_xiaozhi_set_last_type("session.end");
    river_xiaozhi_set_last_state(reason);
    if (message != NULL && message[0] != '\0') {
        river_xiaozhi_set_last_text(message);
    }
    if (g_river_xiaozhi.response_started) {
        g_river_xiaozhi.response_started = false;
        river_xiaozhi_emit_tts_event("stop", NULL);
    }
    g_river_xiaozhi.dialog_started = false;
    river_xiaozhi_clear_last_preview_fields();
    river_xiaozhi_clear_last_playback_fields();
    g_river_xiaozhi.session_id[0] = '\0';

    RIVER_LOGI("xiaozhi session.end: sid=%s reason=%s message=%s",
               ended_sid[0] != '\0' ? ended_sid : "-",
               reason != NULL ? reason : "-",
               message != NULL ? message : "-");
    river_xiaozhi_emit_event(RIVER_XIAOZHI_EVENT_SESSION_CLOSED,
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
}

static void river_xiaozhi_handle_realtime_error(const cJSON *payload)
{
    const cJSON *code_obj;
    const cJSON *message_obj;
    const char *code = NULL;
    const char *message = NULL;

    code_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "code");
    message_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "message");
    if (cJSON_IsString(code_obj) && code_obj->valuestring != NULL) {
        code = code_obj->valuestring;
    }
    if (cJSON_IsString(message_obj) && message_obj->valuestring != NULL) {
        message = message_obj->valuestring;
    }

    river_xiaozhi_set_last_type("error");
    river_xiaozhi_set_last_state(code);
    river_xiaozhi_set_last_error(message != NULL ? message : code);
    RIVER_LOGW("xiaozhi error: sid=%s code=%s message=%s",
               g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
               code != NULL ? code : "-",
               message != NULL ? message : "-");
    river_xiaozhi_emit_event(RIVER_XIAOZHI_EVENT_ERROR,
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
}

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

static void river_xiaozhi_handle_text_message(const char *json_text, int json_len)
{
    cJSON *root;
    cJSON *type_obj;
    cJSON *payload_obj;
    const char *type_text;

    root = cJSON_ParseWithLength(json_text, (size_t)json_len);
    if (root == NULL) {
        river_xiaozhi_set_last_error("json_parse_failed");
        river_xiaozhi_emit_event(RIVER_XIAOZHI_EVENT_ERROR,
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
        return;
    }

    type_obj = cJSON_GetObjectItemCaseSensitive(root, "type");
    payload_obj = cJSON_GetObjectItemCaseSensitive(root, "payload");
    type_text = cJSON_IsString(type_obj) ? type_obj->valuestring : NULL;
    g_river_xiaozhi.text_messages_rx++;

    if (type_text == NULL) {
        river_xiaozhi_set_last_error("json_missing_type");
        cJSON_Delete(root);
        river_xiaozhi_emit_event(RIVER_XIAOZHI_EVENT_ERROR,
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
        return;
    }

    river_xiaozhi_update_session_id_from_root(root);

    if ((strcmp(type_text, "session.update") == 0 ||
         strcmp(type_text, "input.speech.start") == 0 ||
         strcmp(type_text, "input.preview") == 0 ||
         strcmp(type_text, "input.endpoint") == 0 ||
         strcmp(type_text, "audio.out.meta") == 0 ||
         strcmp(type_text, "response.start") == 0 ||
         strcmp(type_text, "response.chunk") == 0 ||
         strcmp(type_text, "session.end") == 0 ||
         strcmp(type_text, "error") == 0) &&
        !cJSON_IsObject(payload_obj)) {
        RIVER_LOGW("xiaozhi message payload invalid: sid=%s type=%s payload_object=no",
                   g_river_xiaozhi.session_id[0] != '\0' ? g_river_xiaozhi.session_id : "-",
                   type_text);
    } else if (strcmp(type_text, "session.update") == 0 && cJSON_IsObject(payload_obj)) {
        river_xiaozhi_handle_realtime_session_update(payload_obj);
    } else if (strcmp(type_text, "input.speech.start") == 0 && cJSON_IsObject(payload_obj)) {
        river_xiaozhi_handle_realtime_input_speech_start(payload_obj);
    } else if (strcmp(type_text, "input.preview") == 0 && cJSON_IsObject(payload_obj)) {
        river_xiaozhi_handle_realtime_input_preview(payload_obj);
    } else if (strcmp(type_text, "input.endpoint") == 0 && cJSON_IsObject(payload_obj)) {
        river_xiaozhi_handle_realtime_input_endpoint(payload_obj);
    } else if (strcmp(type_text, "audio.out.meta") == 0 && cJSON_IsObject(payload_obj)) {
        river_xiaozhi_handle_realtime_audio_out_meta(payload_obj);
    } else if (strcmp(type_text, "response.start") == 0 && cJSON_IsObject(payload_obj)) {
        river_xiaozhi_handle_realtime_response_start(payload_obj);
    } else if (strcmp(type_text, "response.chunk") == 0 && cJSON_IsObject(payload_obj)) {
        river_xiaozhi_handle_realtime_response_chunk(payload_obj);
    } else if (strcmp(type_text, "session.end") == 0 && cJSON_IsObject(payload_obj)) {
        river_xiaozhi_handle_realtime_session_end(payload_obj);
    } else if (strcmp(type_text, "error") == 0 && cJSON_IsObject(payload_obj)) {
        river_xiaozhi_handle_realtime_error(payload_obj);
    } else if (strcmp(type_text, "hello") == 0) {
        river_xiaozhi_emit_transport_ready();
    } else if (strcmp(type_text, "stt") == 0) {
        river_xiaozhi_handle_stt_message(root);
    } else if (strcmp(type_text, "llm") == 0) {
        river_xiaozhi_handle_llm_message(root);
    } else if (strcmp(type_text, "tts") == 0) {
        river_xiaozhi_handle_tts_message(root);
    } else if (strcmp(type_text, "mcp") == 0) {
        river_xiaozhi_handle_mcp_message(root);
    } else if (strcmp(type_text, "system") == 0) {
        river_xiaozhi_handle_system_message(root);
    } else if (strcmp(type_text, "alert") == 0) {
        river_xiaozhi_handle_alert_message(root);
    } else {
        river_xiaozhi_set_last_type(type_text);
        RIVER_LOGI("ignore xiaozhi message type=%s", type_text);
    }

    cJSON_Delete(root);
}

static void river_xiaozhi_handle_binary_message(const uint8_t *data, size_t data_len)
{
    if (data == NULL || data_len == 0U) {
        return;
    }

    g_river_xiaozhi.audio_messages_rx++;
    river_xiaozhi_set_last_type("audio");
    river_xiaozhi_emit_event(RIVER_XIAOZHI_EVENT_AUDIO,
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
                             g_river_xiaozhi.server_sample_rate,
                             g_river_xiaozhi.server_frame_duration_ms,
                             0U,
                             0U,
                             0U,
                             data,
                             data_len,
                             RIVER_XIAOZHI_BINARY_PCM16,
                             false,
                             false,
                             false);
}

static bool river_xiaozhi_payload_looks_like_json(const uint8_t *data, size_t data_len)
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

static bool river_xiaozhi_payload_is_valid_json(const uint8_t *data, size_t data_len)
{
    cJSON *root;

    if (!river_xiaozhi_payload_looks_like_json(data, data_len)) {
        return false;
    }

    root = cJSON_ParseWithLength((const char *)data, data_len);
    if (root == NULL) {
        return false;
    }

    cJSON_Delete(root);
    return true;
}

static void river_xiaozhi_ws_message_cb(wsclient_context **wsclient,
                                        int data_len,
                                        enum opcode_type opcode,
                                        void *user_data)
{
    const uint8_t *data;
    bool json_guess;

    (void)wsclient;
    (void)user_data;

    if (g_river_xiaozhi.wsclient == NULL ||
        g_river_xiaozhi.wsclient->receivedData == NULL ||
        data_len <= 0) {
        return;
    }

    data = (const uint8_t *)g_river_xiaozhi.wsclient->receivedData;
    json_guess = river_xiaozhi_payload_looks_like_json(data, (size_t)data_len);

    if (opcode == BINARY_FRAME) {
        river_xiaozhi_handle_binary_message(data, (size_t)data_len);
        return;
    }

    /*
     * Ameba wsclient reports the opcode of the final fragment when a server
     * message is reassembled. For fragmented XiaoZhi audio, that means the
     * callback may receive the full binary payload with final opcode
     * CONTINUATION instead of BINARY_FRAME. Distinguish reassembled JSON from
     * reassembled binary by looking at the payload prefix.
     */
    if (opcode == CONTINUATION) {
        if (json_guess && river_xiaozhi_payload_is_valid_json(data, (size_t)data_len)) {
            river_xiaozhi_handle_text_message((const char *)data, data_len);
        } else {
            river_xiaozhi_handle_binary_message(data, (size_t)data_len);
        }
        return;
    }

    river_xiaozhi_handle_text_message((const char *)data, data_len);
}
