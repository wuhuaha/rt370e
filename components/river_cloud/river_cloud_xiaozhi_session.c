/* 小智会话编排：处理唤醒准入、会话窗口、ASR/TTS 与回放衔接。 */
#include <stdio.h>
#include <string.h>

#include "river/river_log.h"
#include "river/river_playback_service.h"
#include "river/river_voice_profile.h"
#include "river/river_voice_runtime_policy.h"
#include "river/river_wifi_station.h"

#include "river_cloud_internal.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.cloud"

#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
bool river_cloud_xiaozhi_full_duplex_experiment_enabled(void)
{
#if defined(CONFIG_RIVER_XIAOZHI_FULL_DUPLEX_EXPERIMENT_EN) && \
    CONFIG_RIVER_XIAOZHI_FULL_DUPLEX_EXPERIMENT_EN
    return true;
#else
    return false;
#endif
}

void river_cloud_xiaozhi_get_duplex_ready_eval(river_voice_duplex_ready_eval_t *eval)
{
    river_voice_runtime_duplex_ready_eval(river_cloud_xiaozhi_full_duplex_experiment_enabled(),
                                          river_voice_profile_active_preproc(),
                                          eval);
}

bool river_cloud_xiaozhi_idle_requires_wakeword(void)
{
#if defined(CONFIG_RIVER_VOICE_CAPABILITY_KWS) && CONFIG_RIVER_VOICE_CAPABILITY_KWS
    return true;
#else
    return false;
#endif
}

bool river_cloud_xiaozhi_playback_allows_vad_open(void)
{
    river_voice_duplex_ready_eval_t eval;

    river_cloud_xiaozhi_get_duplex_ready_eval(&eval);
    if (eval.ready && g_river_cloud.xiaozhi_playback_active) {
        g_river_cloud.xiaozhi_playback_duplex_ready_seen = true;
    }
    return eval.ready;
}

bool river_cloud_xiaozhi_keep_local_round_on_tts_start(void)
{
    return river_cloud_xiaozhi_playback_allows_vad_open();
}

static void river_cloud_xiaozhi_copy_semantic_text(char *dst,
                                                   size_t dst_size,
                                                   const char *src)
{
    if (dst == NULL || dst_size == 0U) {
        return;
    }
    if (src == NULL || src[0] == '\0') {
        dst[0] = '\0';
        return;
    }

    snprintf(dst, dst_size, "%s", src);
}

static bool river_cloud_xiaozhi_update_semantic_text(char *dst,
                                                     size_t dst_size,
                                                     const char *src)
{
    char next_value[96];

    if (dst == NULL || dst_size == 0U) {
        return false;
    }

    if (src == NULL || src[0] == '\0') {
        if (dst[0] == '\0') {
            return false;
        }
        dst[0] = '\0';
        return true;
    }

    memset(next_value, 0, sizeof(next_value));
    snprintf(next_value, sizeof(next_value), "%s", src);
    if (strcmp(dst, next_value) == 0) {
        return false;
    }

    snprintf(dst, dst_size, "%s", next_value);
    return true;
}

static const char *river_cloud_xiaozhi_optional_bool_text(bool known, bool enabled)
{
    if (!known) {
        return "-";
    }
    return enabled ? "yes" : "no";
}

void river_cloud_xiaozhi_clear_pending_text(void)
{
    g_river_cloud.xiaozhi_pending_text_valid = false;
    g_river_cloud.xiaozhi_pending_text_finalized = false;
    g_river_cloud.xiaozhi_pending_text[0] = '\0';
}

void river_cloud_xiaozhi_clear_preview_state(void)
{
    g_river_cloud.xiaozhi_preview_speech_started = false;
    g_river_cloud.xiaozhi_preview_endpoint_candidate = false;
    g_river_cloud.xiaozhi_preview_final = false;
    g_river_cloud.xiaozhi_preview_audio_offset_ms = 0U;
    g_river_cloud.xiaozhi_preview_id[0] = '\0';
    g_river_cloud.xiaozhi_preview_text[0] = '\0';
    g_river_cloud.xiaozhi_preview_stable_prefix[0] = '\0';
    g_river_cloud.xiaozhi_preview_source[0] = '\0';
    g_river_cloud.xiaozhi_preview_endpoint_reason[0] = '\0';
}

void river_cloud_xiaozhi_clear_turn_semantics_state(void)
{
    g_river_cloud.xiaozhi_turn_accepted = false;
    g_river_cloud.xiaozhi_transport_barge_in_enabled_known = false;
    g_river_cloud.xiaozhi_transport_barge_in_enabled = false;
    g_river_cloud.xiaozhi_turn_id[0] = '\0';
    g_river_cloud.xiaozhi_accept_reason[0] = '\0';
    g_river_cloud.xiaozhi_input_state[0] = '\0';
    g_river_cloud.xiaozhi_output_state[0] = '\0';
    g_river_cloud.xiaozhi_semantic_fallback_reason[0] = '\0';
}

void river_cloud_xiaozhi_clear_playback_meta_state(void)
{
    g_river_cloud.xiaozhi_playback_meta_valid = false;
    g_river_cloud.xiaozhi_playback_started_reported = false;
    g_river_cloud.xiaozhi_playback_cleared_reported = false;
    g_river_cloud.xiaozhi_playback_completed_reported = false;
    g_river_cloud.xiaozhi_playback_last_segment = false;
    g_river_cloud.xiaozhi_playback_segment_head = 0U;
    g_river_cloud.xiaozhi_playback_segment_count = 0U;
    g_river_cloud.xiaozhi_playback_expected_duration_ms = 0U;
    g_river_cloud.xiaozhi_playback_duplex_ready_seen = false;
    g_river_cloud.xiaozhi_playback_response_id[0] = '\0';
    g_river_cloud.xiaozhi_playback_id[0] = '\0';
    g_river_cloud.xiaozhi_playback_segment_id[0] = '\0';
    g_river_cloud.xiaozhi_playback_last_started_segment_id[0] = '\0';
    g_river_cloud.xiaozhi_playback_last_fully_heard_segment_id[0] = '\0';
    g_river_cloud.xiaozhi_playback_terminal_ack[0] = '\0';
    g_river_cloud.xiaozhi_playback_clear_reason[0] = '\0';
    g_river_cloud.xiaozhi_playback_text[0] = '\0';
    memset(g_river_cloud.xiaozhi_playback_segments,
           0,
           sizeof(g_river_cloud.xiaozhi_playback_segments));
}

void river_cloud_xiaozhi_window_touch(uint32_t duration_ms, const char *reason)
{
    uint64_t now_ms;
    bool was_active;

    if (duration_ms == 0U) {
        return;
    }

    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    was_active = g_river_cloud.xiaozhi_window_active;
    g_river_cloud.xiaozhi_window_active = true;
    g_river_cloud.xiaozhi_window_deadline_ms = now_ms + (uint64_t)duration_ms;
    if (!was_active) {
        RIVER_LOGI("xiaozhi conversation window opened: source=%s mode=auto timeout_ms=%lu",
                   reason != NULL ? reason : "-",
                   (unsigned long)duration_ms);
    }
}

void river_cloud_xiaozhi_window_close(const char *reason)
{
    if (!g_river_cloud.xiaozhi_window_active && !river_xiaozhi_session_open()) {
        return;
    }

    g_river_cloud.xiaozhi_window_active = false;
    g_river_cloud.xiaozhi_window_deadline_ms = 0U;
    g_river_cloud.xiaozhi_open_speech_frames = 0U;
    g_river_cloud.xiaozhi_listen_stop_pending = false;
    if (g_river_cloud.xiaozhi_listening && river_xiaozhi_session_open()) {
        (void)river_cloud_xiaozhi_request_listen_stop();
    }
    g_river_cloud.xiaozhi_listening = false;
    river_cloud_pre_roll_reset();
    (void)river_cloud_xiaozhi_request_close_session();
    g_river_cloud.xiaozhi_session_id[0] = '\0';
    RIVER_LOGI("xiaozhi conversation window closed: reason=%s",
               reason != NULL ? reason : "-");
    river_cloud_request_state_sync(reason != NULL ? reason : "window_closed");
}

void river_cloud_xiaozhi_window_abort_local(const char *reason)
{
    bool should_log;

    should_log = g_river_cloud.xiaozhi_window_active ||
                 g_river_cloud.xiaozhi_listening ||
                 g_river_cloud.xiaozhi_listen_stop_pending ||
                 g_river_cloud.xiaozhi_open_speech_frames != 0U;

    g_river_cloud.xiaozhi_window_active = false;
    g_river_cloud.xiaozhi_window_deadline_ms = 0U;
    g_river_cloud.xiaozhi_open_speech_frames = 0U;
    g_river_cloud.xiaozhi_listen_stop_pending = false;
    river_cloud_pre_roll_reset();
    if (should_log) {
        RIVER_LOGW("xiaozhi conversation window aborted: reason=%s",
                   reason != NULL ? reason : "-");
    }
    river_cloud_request_state_sync(reason != NULL ? reason : "window_aborted");
}

void river_cloud_xiaozhi_copy_session_id_from_transport(void)
{
    const char *sid;

    sid = river_xiaozhi_session_id();
    if (sid != NULL && sid[0] != '\0') {
        snprintf(g_river_cloud.xiaozhi_session_id,
                 sizeof(g_river_cloud.xiaozhi_session_id),
                 "%s",
                 sid);
    }
}

const char *river_cloud_xiaozhi_current_sid(void)
{
    return g_river_cloud.xiaozhi_session_id[0] != '\0' ?
               g_river_cloud.xiaozhi_session_id :
               river_xiaozhi_session_id();
}

void river_cloud_xiaozhi_refresh_turn_semantics(const char *trigger)
{
    const char *turn_id = river_xiaozhi_last_turn_id();
    const char *accept_reason = river_xiaozhi_last_accept_reason();
    const char *input_state = river_xiaozhi_last_input_state();
    const char *output_state = river_xiaozhi_last_output_state();
    bool barge_in_known = river_xiaozhi_last_barge_in_enabled_known();
    bool barge_in_enabled = river_xiaozhi_last_barge_in_enabled();
    bool accepted_before = g_river_cloud.xiaozhi_turn_accepted;
    bool accept_reason_present = accept_reason != NULL && accept_reason[0] != '\0';
    bool turn_id_changed;
    bool accept_reason_changed = false;
    bool input_state_changed;
    bool output_state_changed;
    bool barge_in_changed;

    turn_id_changed = river_cloud_xiaozhi_update_semantic_text(g_river_cloud.xiaozhi_turn_id,
                                                               sizeof(g_river_cloud.xiaozhi_turn_id),
                                                               turn_id);
    if (accept_reason_present) {
        accept_reason_changed = river_cloud_xiaozhi_update_semantic_text(
            g_river_cloud.xiaozhi_accept_reason,
            sizeof(g_river_cloud.xiaozhi_accept_reason),
            accept_reason);
        g_river_cloud.xiaozhi_turn_accepted = true;
        if (strcmp(g_river_cloud.xiaozhi_semantic_fallback_reason,
                   "await_accept_reason") == 0) {
            g_river_cloud.xiaozhi_semantic_fallback_reason[0] = '\0';
        }
    }
    input_state_changed = river_cloud_xiaozhi_update_semantic_text(
        g_river_cloud.xiaozhi_input_state,
        sizeof(g_river_cloud.xiaozhi_input_state),
        input_state);
    output_state_changed = river_cloud_xiaozhi_update_semantic_text(
        g_river_cloud.xiaozhi_output_state,
        sizeof(g_river_cloud.xiaozhi_output_state),
        output_state);
    barge_in_changed =
        (g_river_cloud.xiaozhi_transport_barge_in_enabled_known != barge_in_known) ||
        (barge_in_known &&
         (g_river_cloud.xiaozhi_transport_barge_in_enabled != barge_in_enabled));
    g_river_cloud.xiaozhi_transport_barge_in_enabled_known = barge_in_known;
    g_river_cloud.xiaozhi_transport_barge_in_enabled = barge_in_known && barge_in_enabled;

    if (accept_reason_present &&
        (!accepted_before || accept_reason_changed || turn_id_changed ||
         input_state_changed || output_state_changed || barge_in_changed)) {
        RIVER_LOGI("xiaozhi turn accepted: trigger=%s accept_reason=%s turn_id=%s input_state=%s output_state=%s barge_in_enabled=%s",
                   trigger != NULL ? trigger : "-",
                   g_river_cloud.xiaozhi_accept_reason[0] != '\0' ?
                       g_river_cloud.xiaozhi_accept_reason :
                       "-",
                   g_river_cloud.xiaozhi_turn_id[0] != '\0' ?
                       g_river_cloud.xiaozhi_turn_id :
                       "-",
                   g_river_cloud.xiaozhi_input_state[0] != '\0' ?
                       g_river_cloud.xiaozhi_input_state :
                       "-",
                   g_river_cloud.xiaozhi_output_state[0] != '\0' ?
                       g_river_cloud.xiaozhi_output_state :
                       "-",
                   river_cloud_xiaozhi_optional_bool_text(
                       g_river_cloud.xiaozhi_transport_barge_in_enabled_known,
                       g_river_cloud.xiaozhi_transport_barge_in_enabled));
    }
}

bool river_cloud_xiaozhi_turn_accepted(void)
{
    return g_river_cloud.xiaozhi_turn_accepted &&
           g_river_cloud.xiaozhi_accept_reason[0] != '\0';
}

void river_cloud_xiaozhi_note_semantic_fallback(const char *reason)
{
    river_voice_duplex_ready_eval_t duplex_eval;

    if (reason == NULL || reason[0] == '\0') {
        return;
    }
    if (strcmp(g_river_cloud.xiaozhi_semantic_fallback_reason, reason) == 0) {
        return;
    }

    river_cloud_xiaozhi_get_duplex_ready_eval(&duplex_eval);
    river_cloud_xiaozhi_copy_semantic_text(g_river_cloud.xiaozhi_semantic_fallback_reason,
                                           sizeof(g_river_cloud.xiaozhi_semantic_fallback_reason),
                                           reason);
    RIVER_LOGW("xiaozhi fallback: reason=%s accepted=%s accept_reason=%s input_state=%s output_state=%s duplex_ready=%s duplex_reason=%s sid=%s",
               g_river_cloud.xiaozhi_semantic_fallback_reason,
               river_cloud_xiaozhi_turn_accepted() ? "yes" : "no",
               g_river_cloud.xiaozhi_accept_reason[0] != '\0' ?
                   g_river_cloud.xiaozhi_accept_reason :
                   "-",
               g_river_cloud.xiaozhi_input_state[0] != '\0' ?
                   g_river_cloud.xiaozhi_input_state :
                   "-",
               g_river_cloud.xiaozhi_output_state[0] != '\0' ?
                   g_river_cloud.xiaozhi_output_state :
                   "-",
               duplex_eval.ready ? "yes" : "no",
               river_voice_runtime_duplex_ready_reason_name(duplex_eval.reason),
               river_cloud_xiaozhi_current_sid() != NULL ? river_cloud_xiaozhi_current_sid() : "-");
}

void river_cloud_xiaozhi_finalize_pending_text(const char *trigger)
{
    river_cloud_xiaozhi_refresh_turn_semantics(trigger);

    if (!g_river_cloud.xiaozhi_pending_text_valid || g_river_cloud.xiaozhi_pending_text_finalized ||
        g_river_cloud.xiaozhi_pending_text[0] == '\0') {
        return;
    }

    if (!river_cloud_xiaozhi_turn_accepted()) {
        river_cloud_xiaozhi_note_semantic_fallback("await_accept_reason");
        RIVER_LOGI("xiaozhi pending text waits for accepted turn: trigger=%s text=%s",
                   trigger != NULL ? trigger : "-",
                   g_river_cloud.xiaozhi_pending_text);
        return;
    }

    g_river_cloud.xiaozhi_pending_text_finalized = true;
    RIVER_LOGI("xiaozhi accepted turn final text: trigger=%s accept_reason=%s turn_id=%s text=%s",
               trigger != NULL ? trigger : "-",
               g_river_cloud.xiaozhi_accept_reason[0] != '\0' ?
                   g_river_cloud.xiaozhi_accept_reason :
                   "-",
               g_river_cloud.xiaozhi_turn_id[0] != '\0' ?
                   g_river_cloud.xiaozhi_turn_id :
                   "-",
               g_river_cloud.xiaozhi_pending_text);
    river_cloud_emit_asr_result(RIVER_CLOUD_ASR_EVENT_FINAL,
                                g_river_cloud.xiaozhi_pending_text,
                                river_cloud_xiaozhi_current_sid(),
                                NULL,
                                0,
                                true);
}

void river_cloud_xiaozhi_emit_session_started(void)
{
    if (!g_river_cloud.xiaozhi_listening) {
        return;
    }

    river_cloud_emit_asr_result(RIVER_CLOUD_ASR_EVENT_SESSION_STARTED,
                                NULL,
                                river_cloud_xiaozhi_current_sid(),
                                NULL,
                                0,
                                false);
}

void river_cloud_xiaozhi_emit_session_closed(void)
{
    if (!g_river_cloud.xiaozhi_listening && !g_river_cloud.xiaozhi_local_close_pending) {
        return;
    }

    g_river_cloud.xiaozhi_listening = false;
    g_river_cloud.xiaozhi_local_close_pending = false;
    g_river_cloud.xiaozhi_local_close_deadline_ms = 0U;
    g_river_cloud.xiaozhi_endpoint_soft_close_pending = false;
    g_river_cloud.xiaozhi_endpoint_soft_close_deadline_ms = 0U;
    g_river_cloud.xiaozhi_endpoint_soft_close_reason[0] = '\0';
    river_cloud_emit_asr_result(RIVER_CLOUD_ASR_EVENT_SESSION_CLOSED,
                                NULL,
                                river_cloud_xiaozhi_current_sid(),
                                NULL,
                                0,
                                false);
}

/* Keep XiaoZhi local runtime ownership here so adapter paths express causes, not field-by-field resets. */
void river_cloud_xiaozhi_cancel_playback_stop(void)
{
    g_river_cloud.xiaozhi_tts_stop_pending = false;
    g_river_cloud.xiaozhi_tts_stop_deadline_ms = 0U;
}

void river_cloud_xiaozhi_mark_playback_started(void)
{
    g_river_cloud.xiaozhi_playback_active = true;
    g_river_cloud.xiaozhi_playback_duplex_ready_seen = false;
    river_cloud_xiaozhi_cancel_playback_stop();
    g_river_cloud.xiaozhi_no_ref_reopen_rearm = false;
    g_river_cloud.xiaozhi_no_ref_reopen_silence_frames = 0U;
    g_river_cloud.xiaozhi_no_ref_reopen_guard_deadline_ms = 0U;
}

void river_cloud_xiaozhi_arm_playback_stop(uint32_t drain_ms)
{
    if (!g_river_cloud.xiaozhi_playback_active || drain_ms == 0U) {
        return;
    }

    g_river_cloud.xiaozhi_tts_stop_pending = true;
    g_river_cloud.xiaozhi_tts_stop_deadline_ms =
        (uint64_t)rtos_time_get_current_system_time_ms() + (uint64_t)drain_ms;
}

void river_cloud_xiaozhi_reset_playback_state(void)
{
    bool had_playback_activity =
        g_river_cloud.xiaozhi_playback_active || g_river_cloud.xiaozhi_tts_stop_pending;
    bool duplex_ready_seen = g_river_cloud.xiaozhi_playback_duplex_ready_seen;

    g_river_cloud.xiaozhi_playback_active = false;
    river_cloud_xiaozhi_cancel_playback_stop();
    if (had_playback_activity && !duplex_ready_seen) {
        g_river_cloud.xiaozhi_no_ref_reopen_rearm = true;
        g_river_cloud.xiaozhi_no_ref_reopen_silence_frames = 0U;
        g_river_cloud.xiaozhi_no_ref_reopen_guard_deadline_ms =
            (uint64_t)rtos_time_get_current_system_time_ms() +
            (uint64_t)RIVER_CLOUD_XIAOZHI_NOREF_REOPEN_GUARD_MS;
        RIVER_LOGI("xiaozhi no_ref reopen guard armed: tail_ms=%u silence_frames=%u duplex_seen=no",
                   (unsigned int)RIVER_CLOUD_XIAOZHI_NOREF_REOPEN_GUARD_MS,
                   (unsigned int)RIVER_CLOUD_XIAOZHI_NOREF_REARM_SILENCE_FRAMES);
    } else if (!had_playback_activity || duplex_ready_seen) {
        g_river_cloud.xiaozhi_no_ref_reopen_rearm = false;
        g_river_cloud.xiaozhi_no_ref_reopen_silence_frames = 0U;
        g_river_cloud.xiaozhi_no_ref_reopen_guard_deadline_ms = 0U;
    }
    g_river_cloud.xiaozhi_playback_duplex_ready_seen = false;
}

void river_cloud_xiaozhi_reset_downlink_state(void)
{
    if (g_river_cloud.xiaozhi_downlink_ring.initialized) {
        river_audio_frame_ring_reset(&g_river_cloud.xiaozhi_downlink_ring);
    }
    g_river_cloud.xiaozhi_downlink_ring_dropped = 0U;
}

void river_cloud_xiaozhi_reset_transport_state(bool emit_session_closed)
{
    if (emit_session_closed) {
        river_cloud_xiaozhi_emit_session_closed();
    } else {
        g_river_cloud.xiaozhi_listening = false;
    }

    g_river_cloud.stream_active = false;
    g_river_cloud.silence_frames = 0U;
    g_river_cloud.stream_started_ms = 0U;
    g_river_cloud.xiaozhi_open_speech_frames = 0U;
    g_river_cloud.xiaozhi_window_active = false;
    g_river_cloud.xiaozhi_window_deadline_ms = 0U;
    g_river_cloud.xiaozhi_listen_stop_pending = false;
    g_river_cloud.xiaozhi_uplink_accum_bytes = 0U;
    g_river_cloud.xiaozhi_uplink_next_send_ms = 0U;
    g_river_cloud.xiaozhi_uplink_busy_streak = 0U;
    g_river_cloud.xiaozhi_local_close_pending = false;
    g_river_cloud.xiaozhi_local_close_deadline_ms = 0U;
    g_river_cloud.xiaozhi_endpoint_soft_close_pending = false;
    g_river_cloud.xiaozhi_endpoint_soft_close_deadline_ms = 0U;
    g_river_cloud.xiaozhi_endpoint_soft_close_reason[0] = '\0';
    river_audio_frame_ring_reset(&g_river_cloud.xiaozhi_uplink_ring);
    river_cloud_xiaozhi_reset_downlink_state();
    river_cloud_xiaozhi_clear_pending_text();
    river_cloud_xiaozhi_clear_preview_state();
    river_cloud_xiaozhi_clear_turn_semantics_state();
    river_cloud_xiaozhi_clear_playback_meta_state();
    river_cloud_pre_roll_reset();
    g_river_cloud.xiaozhi_no_ref_reopen_rearm = false;
    g_river_cloud.xiaozhi_no_ref_reopen_silence_frames = 0U;
    g_river_cloud.xiaozhi_no_ref_reopen_guard_deadline_ms = 0U;
    g_river_cloud.xiaozhi_session_id[0] = '\0';
}

void river_cloud_xiaozhi_check_window_timeout(void)
{
    uint64_t now_ms;

    if (!g_river_cloud.xiaozhi_window_active ||
        g_river_cloud.xiaozhi_window_deadline_ms == 0U) {
        return;
    }

    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    if (now_ms < g_river_cloud.xiaozhi_window_deadline_ms) {
        return;
    }

    if (g_river_cloud.stream_active || g_river_cloud.xiaozhi_playback_active ||
        g_river_cloud.xiaozhi_tts_stop_pending ||
        river_audio_frame_ring_count(&g_river_cloud.xiaozhi_downlink_ring) != 0U) {
        return;
    }

    river_cloud_xiaozhi_window_close("followup_timeout");
}

river_status_t river_cloud_xiaozhi_open_session_and_listen(void)
{
    river_status_t status;

    status = river_cloud_xiaozhi_request_open_and_listen("auto");
    if (status != RIVER_OK) {
        return status;
    }
    river_xiaozhi_clear_session_update_cache();
    river_cloud_xiaozhi_copy_session_id_from_transport();

    /*
     * A fresh follow-up listen/asr round must re-arm the conversation window.
     * Otherwise the shorter post-TTS tail timer can expire while the user has
     * already started the next utterance, causing the websocket to close
     * immediately after this ASR round finishes.
     */
    river_cloud_xiaozhi_window_touch(RIVER_CLOUD_XIAOZHI_WAKE_WINDOW_FOLLOWUP_MS,
                                     "asr_session_start");
    river_cloud_xiaozhi_clear_pending_text();
    river_cloud_xiaozhi_clear_preview_state();
    river_cloud_xiaozhi_clear_turn_semantics_state();
    river_cloud_xiaozhi_clear_playback_meta_state();
    river_cloud_xiaozhi_emit_session_started();
    return RIVER_OK;
}

river_status_t river_cloud_xiaozhi_begin_conversation_window(const char *source)
{
    river_status_t status;
    const char *reason = (source != NULL && source[0] != '\0') ? source : "-";
    bool was_listening;

    if (!g_river_cloud.initialized || !g_river_cloud.xiaozhi_enabled) {
        return RIVER_ERR_UNSUPPORTED;
    }

    river_cloud_start_sntp_if_needed();
    river_cloud_seed_time_from_build_if_needed();
    if (!river_wifi_station_is_connected()) {
        river_cloud_log_wake_admission_deferred_once(RIVER_ERR_BUSY);
        return RIVER_ERR_BUSY;
    }
    if (!river_cloud_wake_admission_time_ready()) {
        river_cloud_log_wake_admission_deferred_once(RIVER_ERR_BUSY);
        return RIVER_ERR_BUSY;
    }
    river_cloud_reset_wake_admission_deferred_state();
    if (RIVER_CLOUD_BUSINESS_TIME_WAIT_REQUIRED &&
        !river_cloud_time_ready() &&
        g_river_cloud.time_seeded_from_build &&
        !g_river_cloud.wake_admission_estimate_announced) {
        g_river_cloud.wake_admission_estimate_announced = true;
        RIVER_LOGI("wake admission proceeding with build-seeded utc estimate");
    }

    RIVER_LOGI("xiaozhi wake admission begin: source=%s session=%s listening=%s window=%s sid=%s",
               reason,
               river_xiaozhi_session_open() ? "open" : "closed",
               g_river_cloud.xiaozhi_listening ? "yes" : "no",
               g_river_cloud.xiaozhi_window_active ? "open" : "closed",
               river_cloud_xiaozhi_current_sid() != NULL ? river_cloud_xiaozhi_current_sid() : "-");

    was_listening = g_river_cloud.xiaozhi_listening;
    status = river_cloud_xiaozhi_request_open_and_listen("auto");
    if (status != RIVER_OK) {
        if (!river_xiaozhi_session_open()) {
            RIVER_LOGW("xiaozhi wake admission open_session failed: source=%s status=%d last_err=%s",
                       reason,
                       (int)status,
                       river_xiaozhi_last_error() != NULL ? river_xiaozhi_last_error() : "-");
        } else {
            RIVER_LOGW("xiaozhi wake admission listen_start failed: source=%s status=%d sid=%s last_err=%s",
                       reason,
                       (int)status,
                       river_cloud_xiaozhi_current_sid() != NULL ? river_cloud_xiaozhi_current_sid() : "-",
                       river_xiaozhi_last_error() != NULL ? river_xiaozhi_last_error() : "-");
        }
        return status;
    }
    river_xiaozhi_clear_session_update_cache();
    river_cloud_xiaozhi_copy_session_id_from_transport();
    RIVER_LOGI("xiaozhi wake admission transport ready: source=%s sid=%s",
               reason,
               river_cloud_xiaozhi_current_sid() != NULL ? river_cloud_xiaozhi_current_sid() : "-");
    if (!was_listening && g_river_cloud.xiaozhi_listening) {
        RIVER_LOGI("xiaozhi wake admission listen_start sent: source=%s sid=%s",
                   reason,
                   river_cloud_xiaozhi_current_sid() != NULL ? river_cloud_xiaozhi_current_sid() : "-");
    }

    river_cloud_xiaozhi_window_touch(RIVER_CLOUD_XIAOZHI_WAKE_WINDOW_FOLLOWUP_MS, reason);
    g_river_cloud.xiaozhi_open_speech_frames = 0U;
    river_cloud_pre_roll_reset();
    g_river_cloud.xiaozhi_listen_stop_pending = false;
    RIVER_LOGI("xiaozhi wake admission ready: source=%s listening=%s window=%s sid=%s",
               reason,
               g_river_cloud.xiaozhi_listening ? "yes" : "no",
               g_river_cloud.xiaozhi_window_active ? "open" : "closed",
               river_cloud_xiaozhi_current_sid() != NULL ? river_cloud_xiaozhi_current_sid() : "-");
    return RIVER_OK;
}
#endif
