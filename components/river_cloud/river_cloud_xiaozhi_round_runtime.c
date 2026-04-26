/* 小智 round/window runtime：收口 listening/window/listen-stop/local-close 状态。 */
#include <string.h>

#include "river/river_log.h"
#include "river/river_runtime_stats.h"
#include "river/river_wifi_station.h"

#include "river_cloud_internal.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.cloud"

#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
static const char *river_cloud_xiaozhi_round_close_cause_name(
    river_cloud_xiaozhi_round_close_cause_t cause)
{
    switch (cause) {
    case RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_LOCAL_RESOLVED:
        return "local_resolved";
    case RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_SERVER_RESPONSE:
        return "server_response_started";
    case RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_SERVER_ENDPOINT:
        return "server_endpoint_pending";
    default:
        return "unknown";
    }
}

bool river_cloud_xiaozhi_listening_active(void)
{
    return g_river_cloud.xiaozhi_session_window_truth.listening;
}

bool river_cloud_xiaozhi_conversation_window_active(void)
{
    return g_river_cloud.xiaozhi_session_window_truth.window_active;
}

uint64_t river_cloud_xiaozhi_conversation_window_remaining_ms(uint64_t now_ms)
{
    if (!g_river_cloud.xiaozhi_session_window_truth.window_active ||
        g_river_cloud.xiaozhi_session_window_truth.window_deadline_ms <= now_ms) {
        return 0U;
    }

    return g_river_cloud.xiaozhi_session_window_truth.window_deadline_ms - now_ms;
}

bool river_cloud_xiaozhi_local_close_pending(void)
{
    return g_river_cloud.xiaozhi_session_window_truth.local_close_pending;
}

uint64_t river_cloud_xiaozhi_local_close_remaining_ms(uint64_t now_ms)
{
    if (!g_river_cloud.xiaozhi_session_window_truth.local_close_pending ||
        g_river_cloud.xiaozhi_session_window_truth.local_close_deadline_ms <= now_ms) {
        return 0U;
    }

    return g_river_cloud.xiaozhi_session_window_truth.local_close_deadline_ms - now_ms;
}

bool river_cloud_xiaozhi_listen_stop_pending(void)
{
    return g_river_cloud.xiaozhi_session_window_truth.listen_stop_pending;
}

void river_cloud_xiaozhi_apply_open_and_listen_session_policy(void)
{
    g_river_cloud.xiaozhi_session_window_truth.listening = true;
    g_river_cloud.xiaozhi_session_window_truth.listen_stop_pending = false;
}

void river_cloud_xiaozhi_apply_listen_stop_completion_round_policy(void)
{
    g_river_cloud.xiaozhi_session_window_truth.listen_stop_pending = false;
    if (g_river_cloud.xiaozhi_session_window_truth.local_close_pending) {
        g_river_cloud.xiaozhi_session_window_truth.listening = false;
        river_cloud_xiaozhi_apply_post_stop_result_round_policy();
        return;
    }

    river_cloud_xiaozhi_emit_session_closed();
    river_cloud_xiaozhi_round_finish(NULL);
    river_runtime_stats_snapshot("asr_stream_finish");
}

void river_cloud_xiaozhi_maybe_finalize_listen_stop(uint32_t queued_frames, size_t accum_bytes)
{
    river_status_t status;

    if (!g_river_cloud.xiaozhi_session_window_truth.listen_stop_pending || queued_frames != 0U || accum_bytes != 0U) {
        return;
    }

    if (river_xiaozhi_session_open() && g_river_cloud.xiaozhi_session_window_truth.listening) {
        status = river_cloud_xiaozhi_request_listen_stop();
        if (status != RIVER_OK) {
            return;
        }
    }

    river_cloud_xiaozhi_apply_listen_stop_completion_round_policy();
}

bool river_cloud_xiaozhi_uplink_keepalive_needed(uint32_t queued_frames)
{
    return queued_frames > 0U || g_river_cloud.xiaozhi_session_window_truth.listen_stop_pending;
}

bool river_cloud_xiaozhi_uplink_send_ready(void)
{
    return river_xiaozhi_session_open() && g_river_cloud.xiaozhi_session_window_truth.listening;
}

bool river_cloud_xiaozhi_should_defer_local_close(void)
{
    return g_river_cloud.xiaozhi_asr_round_truth.active &&
           !g_river_cloud.xiaozhi_asr_round_truth.partial_seen &&
           !g_river_cloud.xiaozhi_asr_round_truth.final_seen &&
           !g_river_cloud.xiaozhi_pending_transcript_truth.valid;
}

void river_cloud_xiaozhi_clear_local_close_defer(void)
{
    g_river_cloud.xiaozhi_session_window_truth.local_close_pending = false;
    g_river_cloud.xiaozhi_session_window_truth.local_close_deadline_ms = 0U;
}

void river_cloud_xiaozhi_arm_local_close_defer(void)
{
    if (g_river_cloud.xiaozhi_session_window_truth.local_close_pending) {
        return;
    }

    g_river_cloud.xiaozhi_session_window_truth.local_close_pending = true;
    g_river_cloud.xiaozhi_session_window_truth.local_close_deadline_ms =
        (uint64_t)rtos_time_get_current_system_time_ms() +
        (uint64_t)RIVER_CLOUD_XIAOZHI_LOCAL_CLOSE_DEFER_MS;
    RIVER_LOGI("xiaozhi local close deferred: wait_ms=%u",
               (unsigned int)RIVER_CLOUD_XIAOZHI_LOCAL_CLOSE_DEFER_MS);
}

void river_cloud_xiaozhi_prepare_post_commit_wait(void)
{
    g_river_cloud.xiaozhi_session_window_truth.listen_stop_pending = true;
    river_cloud_xiaozhi_window_touch(RIVER_CLOUD_XIAOZHI_POST_COMMIT_RESPONSE_WAIT_MS,
                                     "post_commit_wait");
    RIVER_LOGI("xiaozhi response wait armed after commit: timeout_ms=%u",
               (unsigned int)RIVER_CLOUD_XIAOZHI_POST_COMMIT_RESPONSE_WAIT_MS);
    if (river_cloud_xiaozhi_should_defer_local_close()) {
        river_cloud_xiaozhi_arm_local_close_defer();
    } else {
        river_cloud_xiaozhi_clear_local_close_defer();
    }
}

void river_cloud_xiaozhi_window_touch(uint32_t duration_ms, const char *reason)
{
    uint64_t now_ms;
    bool was_active;

    if (duration_ms == 0U) {
        return;
    }

    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    was_active = g_river_cloud.xiaozhi_session_window_truth.window_active;
    g_river_cloud.xiaozhi_session_window_truth.window_active = true;
    g_river_cloud.xiaozhi_session_window_truth.window_deadline_ms = now_ms + (uint64_t)duration_ms;
    if (!was_active) {
        RIVER_LOGI("xiaozhi conversation window opened: source=%s mode=auto timeout_ms=%lu",
                   reason != NULL ? reason : "-",
                   (unsigned long)duration_ms);
    }
}

void river_cloud_xiaozhi_window_close(const char *reason)
{
    if (!g_river_cloud.xiaozhi_session_window_truth.window_active && !river_xiaozhi_session_open()) {
        return;
    }

    g_river_cloud.xiaozhi_session_window_truth.window_active = false;
    g_river_cloud.xiaozhi_session_window_truth.window_deadline_ms = 0U;
    g_river_cloud.xiaozhi_uplink_runtime_truth.open_speech_frames = 0U;
    g_river_cloud.xiaozhi_session_window_truth.listen_stop_pending = false;
    if (g_river_cloud.xiaozhi_session_window_truth.listening && river_xiaozhi_session_open()) {
        (void)river_cloud_xiaozhi_request_listen_stop();
    }
    g_river_cloud.xiaozhi_session_window_truth.listening = false;
    river_cloud_pre_roll_reset();
    (void)river_cloud_xiaozhi_request_close_session();
    river_cloud_xiaozhi_clear_session_id();
    RIVER_LOGI("xiaozhi conversation window closed: reason=%s",
               reason != NULL ? reason : "-");
    river_cloud_request_state_sync(reason != NULL ? reason : "window_closed");
}

void river_cloud_xiaozhi_window_abort_local(const char *reason)
{
    bool should_log;

    should_log = g_river_cloud.xiaozhi_session_window_truth.window_active ||
                 g_river_cloud.xiaozhi_session_window_truth.listening ||
                 g_river_cloud.xiaozhi_session_window_truth.listen_stop_pending ||
                 g_river_cloud.xiaozhi_uplink_runtime_truth.open_speech_frames != 0U;

    g_river_cloud.xiaozhi_session_window_truth.window_active = false;
    g_river_cloud.xiaozhi_session_window_truth.window_deadline_ms = 0U;
    g_river_cloud.xiaozhi_uplink_runtime_truth.open_speech_frames = 0U;
    g_river_cloud.xiaozhi_session_window_truth.listen_stop_pending = false;
    river_cloud_pre_roll_reset();
    if (should_log) {
        RIVER_LOGW("xiaozhi conversation window aborted: reason=%s",
                   reason != NULL ? reason : "-");
    }
    river_cloud_request_state_sync(reason != NULL ? reason : "window_aborted");
}

void river_cloud_xiaozhi_emit_session_started(void)
{
    if (!g_river_cloud.xiaozhi_session_window_truth.listening) {
        return;
    }

    river_cloud_emit_asr_result(RIVER_CLOUD_ASR_EVENT_SESSION_STARTED,
                                NULL,
                                river_cloud_xiaozhi_current_sid(),
                                NULL,
                                0,
                                false);
    river_cloud_request_state_sync("asr_session_started");
}

void river_cloud_xiaozhi_emit_session_closed(void)
{
    if (!g_river_cloud.xiaozhi_session_window_truth.listening && !g_river_cloud.xiaozhi_session_window_truth.local_close_pending) {
        return;
    }

    g_river_cloud.xiaozhi_session_window_truth.listening = false;
    river_cloud_xiaozhi_clear_local_close_defer();
    river_cloud_xiaozhi_clear_endpoint_soft_close_state();
    river_cloud_emit_asr_result(RIVER_CLOUD_ASR_EVENT_SESSION_CLOSED,
                                NULL,
                                river_cloud_xiaozhi_current_sid(),
                                NULL,
                                0,
                                false);
    river_cloud_request_state_sync("asr_session_closed");
}

void river_cloud_xiaozhi_close_local_round_for_cause(
    river_cloud_xiaozhi_round_close_cause_t cause,
    const char *detail_reason)
{
    const char *cause_name = river_cloud_xiaozhi_round_close_cause_name(cause);
    const char *resolved_reason =
        (detail_reason != NULL && detail_reason[0] != '\0') ? detail_reason : cause_name;

    switch (cause) {
    case RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_LOCAL_RESOLVED:
        if (!g_river_cloud.xiaozhi_session_window_truth.local_close_pending ||
            g_river_cloud.xiaozhi_session_window_truth.listen_stop_pending) {
            return;
        }

        RIVER_LOGI("xiaozhi local round close: cause=%s trigger=%s partial=%s final=%s",
                   cause_name,
                   resolved_reason,
                   g_river_cloud.xiaozhi_asr_round_truth.partial_seen ? "yes" : "no",
                   g_river_cloud.xiaozhi_asr_round_truth.final_seen ? "yes" : "no");
        river_cloud_xiaozhi_emit_session_closed();
        river_cloud_xiaozhi_round_finish(resolved_reason);
        river_runtime_stats_snapshot("asr_stream_finish");
        return;

    case RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_SERVER_RESPONSE:
    case RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_SERVER_ENDPOINT:
        if (!g_river_cloud.stream_active &&
            !g_river_cloud.xiaozhi_session_window_truth.listening &&
            !g_river_cloud.xiaozhi_session_window_truth.listen_stop_pending &&
            !g_river_cloud.xiaozhi_session_window_truth.local_close_pending) {
            return;
        }

        RIVER_LOGI("xiaozhi local round close: cause=%s trigger=%s stream=%s stop_pending=%s close_pending=%s",
                   cause_name,
                   resolved_reason,
                   g_river_cloud.stream_active ? "yes" : "no",
                   g_river_cloud.xiaozhi_session_window_truth.listen_stop_pending ? "yes" : "no",
                   g_river_cloud.xiaozhi_session_window_truth.local_close_pending ? "yes" : "no");
        g_river_cloud.stream_active = false;
        g_river_cloud.silence_frames = 0U;
        g_river_cloud.stream_started_ms = 0U;
        g_river_cloud.xiaozhi_uplink_runtime_truth.open_speech_frames = 0U;
        g_river_cloud.xiaozhi_session_window_truth.listen_stop_pending = false;
        river_cloud_xiaozhi_clear_local_close_defer();
        river_cloud_xiaozhi_clear_endpoint_soft_close_state();
        g_river_cloud.xiaozhi_uplink_runtime_truth.accum_bytes = 0U;
        g_river_cloud.xiaozhi_uplink_runtime_truth.retry_valid = false;
        g_river_cloud.xiaozhi_uplink_runtime_truth.next_send_ms = 0U;
        g_river_cloud.xiaozhi_uplink_runtime_truth.busy_streak = 0U;
        river_audio_frame_ring_reset(&g_river_cloud.xiaozhi_uplink_ring);
        river_cloud_pre_roll_reset();

        river_cloud_xiaozhi_emit_session_closed();
        river_cloud_xiaozhi_round_finish(resolved_reason);
        river_runtime_stats_snapshot("asr_stream_finish");
        return;

    default:
        return;
    }
}

void river_cloud_xiaozhi_check_local_close_timeout(void)
{
    uint64_t now_ms;

    if (!g_river_cloud.xiaozhi_session_window_truth.local_close_pending ||
        g_river_cloud.xiaozhi_session_window_truth.local_close_deadline_ms == 0U) {
        return;
    }

    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    if (now_ms < g_river_cloud.xiaozhi_session_window_truth.local_close_deadline_ms) {
        return;
    }

    river_cloud_xiaozhi_close_local_round_for_cause(
        RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_LOCAL_RESOLVED,
        "timeout");
}

void river_cloud_xiaozhi_reset_transport_state(bool emit_session_closed)
{
    if (emit_session_closed) {
        river_cloud_xiaozhi_emit_session_closed();
    } else {
        g_river_cloud.xiaozhi_session_window_truth.listening = false;
    }

    g_river_cloud.stream_active = false;
    g_river_cloud.silence_frames = 0U;
    g_river_cloud.stream_started_ms = 0U;
    g_river_cloud.xiaozhi_uplink_runtime_truth.open_speech_frames = 0U;
    g_river_cloud.xiaozhi_session_window_truth.window_active = false;
    g_river_cloud.xiaozhi_session_window_truth.window_deadline_ms = 0U;
    g_river_cloud.xiaozhi_session_window_truth.listen_stop_pending = false;
    g_river_cloud.xiaozhi_uplink_runtime_truth.accum_bytes = 0U;
    g_river_cloud.xiaozhi_uplink_runtime_truth.retry_valid = false;
    g_river_cloud.xiaozhi_uplink_runtime_truth.next_send_ms = 0U;
    g_river_cloud.xiaozhi_uplink_runtime_truth.busy_streak = 0U;
    river_cloud_xiaozhi_clear_local_close_defer();
    river_cloud_xiaozhi_clear_endpoint_soft_close_state();
    river_audio_frame_ring_reset(&g_river_cloud.xiaozhi_uplink_ring);
    river_cloud_xiaozhi_apply_transport_reset_playback_policy();
    river_cloud_xiaozhi_clear_pending_text();
    river_cloud_xiaozhi_clear_preview_state();
    river_cloud_xiaozhi_clear_turn_semantics_state();
    river_cloud_pre_roll_reset();
    river_cloud_xiaozhi_clear_session_id();
}

void river_cloud_xiaozhi_check_window_timeout(void)
{
    uint64_t now_ms;

    if (!g_river_cloud.xiaozhi_session_window_truth.window_active ||
        g_river_cloud.xiaozhi_session_window_truth.window_deadline_ms == 0U) {
        return;
    }

    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    if (now_ms < g_river_cloud.xiaozhi_session_window_truth.window_deadline_ms) {
        return;
    }

    if (g_river_cloud.stream_active || river_cloud_xiaozhi_playback_turn_active()) {
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
    river_cloud_xiaozhi_apply_session_start_playback_policy();
    river_cloud_xiaozhi_emit_session_started();
    return RIVER_OK;
}

river_status_t river_cloud_xiaozhi_start_followup_round(uint32_t pre_roll_frames)
{
    river_status_t status;

    if (!river_xiaozhi_session_open()) {
        river_cloud_xiaozhi_window_abort_local("followup_transport_unavailable");
        river_cloud_xiaozhi_check_window_timeout();
        return RIVER_ERR_BUSY;
    }

    if (river_cloud_xiaozhi_local_close_pending()) {
        river_cloud_xiaozhi_apply_reopen_overlap_round_policy();
    }

    status = river_cloud_xiaozhi_open_session_and_listen();
    if (status != RIVER_OK) {
        return status;
    }
    if (g_river_cloud.xiaozhi_asr_round_truth.active) {
        river_cloud_xiaozhi_round_finish("reopen_overlap");
    }
    river_cloud_xiaozhi_round_begin(pre_roll_frames);
    return RIVER_OK;
}

river_status_t river_cloud_xiaozhi_maybe_start_followup_round(bool is_speech,
                                                              uint32_t pre_roll_frames,
                                                              bool *opened)
{
    river_status_t status;
    uint32_t open_hold_frames;

    if (opened != NULL) {
        *opened = false;
    }

    if (river_cloud_xiaozhi_listen_stop_pending()) {
        return RIVER_ERR_BUSY;
    }

    if ((g_river_cloud.xiaozhi_turn_semantics.accepted &&
         strcmp(g_river_cloud.xiaozhi_turn_semantics.output_state, "thinking") == 0) ||
        (g_river_cloud.xiaozhi_playback_lineage_truth.stage ==
             RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_RESPONSE_STARTED &&
         g_river_cloud.xiaozhi_playback_lineage_truth.meta_context.response_id[0] == '\0')) {
        g_river_cloud.xiaozhi_uplink_runtime_truth.open_speech_frames = 0U;
        if (is_speech &&
            !g_river_cloud.xiaozhi_session_window_truth.followup_response_pending_reported) {
            g_river_cloud.xiaozhi_session_window_truth.followup_response_pending_reported = true;
            RIVER_LOGI("xiaozhi followup reopen blocked: reason=response_pending accepted=%s output_state=%s response_wait=%s sid=%s",
                       g_river_cloud.xiaozhi_turn_semantics.accepted ? "yes" : "no",
                       g_river_cloud.xiaozhi_turn_semantics.output_state[0] != '\0' ?
                           g_river_cloud.xiaozhi_turn_semantics.output_state :
                           "-",
                       g_river_cloud.xiaozhi_playback_lineage_truth.stage ==
                               RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_RESPONSE_STARTED ?
                           "yes" :
                           "no",
                       river_cloud_xiaozhi_current_sid() != NULL ?
                           river_cloud_xiaozhi_current_sid() :
                           "-");
        }
        return RIVER_OK;
    }
    g_river_cloud.xiaozhi_session_window_truth.followup_response_pending_reported = false;

    if (!river_cloud_xiaozhi_conversation_window_active() &&
        river_cloud_xiaozhi_idle_requires_wakeword()) {
        g_river_cloud.xiaozhi_uplink_runtime_truth.open_speech_frames = 0U;
        return RIVER_OK;
    }

    if (!river_cloud_xiaozhi_playback_followup_reopen_ready(is_speech)) {
        g_river_cloud.xiaozhi_uplink_runtime_truth.open_speech_frames = 0U;
        return RIVER_OK;
    }

    open_hold_frames = river_cloud_xiaozhi_open_hold_frames_required();
    if (!is_speech) {
        g_river_cloud.xiaozhi_uplink_runtime_truth.open_speech_frames = 0U;
        return RIVER_OK;
    }

    if (g_river_cloud.xiaozhi_uplink_runtime_truth.open_speech_frames < UINT32_MAX) {
        g_river_cloud.xiaozhi_uplink_runtime_truth.open_speech_frames++;
    }
    if (g_river_cloud.xiaozhi_uplink_runtime_truth.open_speech_frames < open_hold_frames) {
        return RIVER_OK;
    }

    status = river_cloud_xiaozhi_start_followup_round(pre_roll_frames);
    if (status != RIVER_OK) {
        g_river_cloud.xiaozhi_uplink_runtime_truth.open_speech_frames = 0U;
        return status;
    }

    g_river_cloud.xiaozhi_uplink_runtime_truth.open_speech_frames = 0U;
    if (opened != NULL) {
        *opened = true;
    }
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
               river_cloud_xiaozhi_listening_active() ? "yes" : "no",
               river_cloud_xiaozhi_conversation_window_active() ? "open" : "closed",
               river_cloud_xiaozhi_current_sid() != NULL ? river_cloud_xiaozhi_current_sid() : "-");
    RIVER_LOGI("xiaozhi wake admission policy: source=%s default_on=%s default_reason=%s voice_collaboration=%s server_endpoint=%s/%s preview_events=%s playback_ack=%s",
               reason,
               river_xiaozhi_duplex_default_on_allowed() ? "yes" : "no",
               river_xiaozhi_duplex_default_fallback_reason() != NULL ?
                   river_xiaozhi_duplex_default_fallback_reason() :
                   "-",
               river_xiaozhi_discovery_voice_collaboration_advertised() ? "yes" : "no",
               river_xiaozhi_discovery_server_endpoint_available() ? "yes" : "no",
               river_xiaozhi_discovery_server_endpoint_enabled() ? "yes" : "no",
               river_xiaozhi_preview_events_negotiated() ? "yes" : "no",
               river_xiaozhi_playback_ack_mode_negotiated() != NULL ?
                   river_xiaozhi_playback_ack_mode_negotiated() :
                   "-");

    was_listening = river_cloud_xiaozhi_listening_active();
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
    if (!was_listening && river_cloud_xiaozhi_listening_active()) {
        RIVER_LOGI("xiaozhi wake admission listen_start sent: source=%s sid=%s",
                   reason,
                   river_cloud_xiaozhi_current_sid() != NULL ? river_cloud_xiaozhi_current_sid() : "-");
    }

    river_cloud_xiaozhi_window_touch(RIVER_CLOUD_XIAOZHI_WAKE_WINDOW_FOLLOWUP_MS, reason);
    g_river_cloud.xiaozhi_uplink_runtime_truth.open_speech_frames = 0U;
    river_cloud_pre_roll_reset();
    RIVER_LOGI("xiaozhi wake admission ready: source=%s listening=%s window=%s sid=%s",
               reason,
               river_cloud_xiaozhi_listening_active() ? "yes" : "no",
               river_cloud_xiaozhi_conversation_window_active() ? "open" : "closed",
               river_cloud_xiaozhi_current_sid() != NULL ? river_cloud_xiaozhi_current_sid() : "-");
    return RIVER_OK;
}
#endif
