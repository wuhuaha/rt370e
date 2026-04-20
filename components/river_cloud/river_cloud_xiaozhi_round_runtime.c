/* 小智 round/window runtime：收口 listening/window/listen-stop/local-close 状态。 */
#include "river/river_log.h"
#include "river/river_runtime_stats.h"

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
    default:
        return "unknown";
    }
}

bool river_cloud_xiaozhi_listening_active(void)
{
    return g_river_cloud.xiaozhi_listening;
}

bool river_cloud_xiaozhi_conversation_window_active(void)
{
    return g_river_cloud.xiaozhi_window_active;
}

uint64_t river_cloud_xiaozhi_conversation_window_remaining_ms(uint64_t now_ms)
{
    if (!g_river_cloud.xiaozhi_window_active ||
        g_river_cloud.xiaozhi_window_deadline_ms <= now_ms) {
        return 0U;
    }

    return g_river_cloud.xiaozhi_window_deadline_ms - now_ms;
}

bool river_cloud_xiaozhi_local_close_pending(void)
{
    return g_river_cloud.xiaozhi_local_close_pending;
}

uint64_t river_cloud_xiaozhi_local_close_remaining_ms(uint64_t now_ms)
{
    if (!g_river_cloud.xiaozhi_local_close_pending ||
        g_river_cloud.xiaozhi_local_close_deadline_ms <= now_ms) {
        return 0U;
    }

    return g_river_cloud.xiaozhi_local_close_deadline_ms - now_ms;
}

bool river_cloud_xiaozhi_listen_stop_pending(void)
{
    return g_river_cloud.xiaozhi_listen_stop_pending;
}

void river_cloud_xiaozhi_apply_open_and_listen_session_policy(void)
{
    g_river_cloud.xiaozhi_listening = true;
    g_river_cloud.xiaozhi_listen_stop_pending = false;
}

void river_cloud_xiaozhi_apply_listen_stop_completion_round_policy(void)
{
    g_river_cloud.xiaozhi_listen_stop_pending = false;
    if (g_river_cloud.xiaozhi_local_close_pending) {
        g_river_cloud.xiaozhi_listening = false;
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

    if (!g_river_cloud.xiaozhi_listen_stop_pending || queued_frames != 0U || accum_bytes != 0U) {
        return;
    }

    if (river_xiaozhi_session_open() && g_river_cloud.xiaozhi_listening) {
        status = river_cloud_xiaozhi_request_listen_stop();
        if (status != RIVER_OK) {
            return;
        }
    }

    river_cloud_xiaozhi_apply_listen_stop_completion_round_policy();
}

bool river_cloud_xiaozhi_uplink_keepalive_needed(uint32_t queued_frames)
{
    return queued_frames > 0U || g_river_cloud.xiaozhi_listen_stop_pending;
}

bool river_cloud_xiaozhi_uplink_send_ready(void)
{
    return river_xiaozhi_session_open() && g_river_cloud.xiaozhi_listening;
}

bool river_cloud_xiaozhi_should_defer_local_close(void)
{
    return g_river_cloud.xiaozhi_asr_round_active &&
           !g_river_cloud.xiaozhi_asr_round_partial_seen &&
           !g_river_cloud.xiaozhi_asr_round_final_seen &&
           !g_river_cloud.xiaozhi_pending_text_valid;
}

void river_cloud_xiaozhi_clear_local_close_defer(void)
{
    g_river_cloud.xiaozhi_local_close_pending = false;
    g_river_cloud.xiaozhi_local_close_deadline_ms = 0U;
}

void river_cloud_xiaozhi_arm_local_close_defer(void)
{
    if (g_river_cloud.xiaozhi_local_close_pending) {
        return;
    }

    g_river_cloud.xiaozhi_local_close_pending = true;
    g_river_cloud.xiaozhi_local_close_deadline_ms =
        (uint64_t)rtos_time_get_current_system_time_ms() +
        (uint64_t)RIVER_CLOUD_XIAOZHI_LOCAL_CLOSE_DEFER_MS;
    RIVER_LOGI("xiaozhi local close deferred: wait_ms=%u",
               (unsigned int)RIVER_CLOUD_XIAOZHI_LOCAL_CLOSE_DEFER_MS);
}

void river_cloud_xiaozhi_prepare_post_commit_wait(void)
{
    g_river_cloud.xiaozhi_listen_stop_pending = true;
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
    river_cloud_request_state_sync("asr_session_started");
}

void river_cloud_xiaozhi_emit_session_closed(void)
{
    if (!g_river_cloud.xiaozhi_listening && !g_river_cloud.xiaozhi_local_close_pending) {
        return;
    }

    g_river_cloud.xiaozhi_listening = false;
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
        if (!g_river_cloud.xiaozhi_local_close_pending ||
            g_river_cloud.xiaozhi_listen_stop_pending) {
            return;
        }

        RIVER_LOGI("xiaozhi local round close: cause=%s trigger=%s partial=%s final=%s",
                   cause_name,
                   resolved_reason,
                   g_river_cloud.xiaozhi_asr_round_partial_seen ? "yes" : "no",
                   g_river_cloud.xiaozhi_asr_round_final_seen ? "yes" : "no");
        river_cloud_xiaozhi_emit_session_closed();
        river_cloud_xiaozhi_round_finish(resolved_reason);
        river_runtime_stats_snapshot("asr_stream_finish");
        return;

    case RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_SERVER_RESPONSE:
        if (!g_river_cloud.stream_active &&
            !g_river_cloud.xiaozhi_listen_stop_pending &&
            !g_river_cloud.xiaozhi_local_close_pending) {
            return;
        }

        RIVER_LOGI("xiaozhi local round close: cause=%s trigger=%s stream=%s stop_pending=%s close_pending=%s",
                   cause_name,
                   resolved_reason,
                   g_river_cloud.stream_active ? "yes" : "no",
                   g_river_cloud.xiaozhi_listen_stop_pending ? "yes" : "no",
                   g_river_cloud.xiaozhi_local_close_pending ? "yes" : "no");
        g_river_cloud.stream_active = false;
        g_river_cloud.silence_frames = 0U;
        g_river_cloud.stream_started_ms = 0U;
        g_river_cloud.xiaozhi_open_speech_frames = 0U;
        g_river_cloud.xiaozhi_listen_stop_pending = false;
        river_cloud_xiaozhi_clear_local_close_defer();
        river_cloud_xiaozhi_clear_endpoint_soft_close_state();
        g_river_cloud.xiaozhi_uplink_accum_bytes = 0U;
        g_river_cloud.xiaozhi_uplink_retry_valid = false;
        g_river_cloud.xiaozhi_uplink_next_send_ms = 0U;
        g_river_cloud.xiaozhi_uplink_busy_streak = 0U;
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

    if (!g_river_cloud.xiaozhi_local_close_pending ||
        g_river_cloud.xiaozhi_local_close_deadline_ms == 0U) {
        return;
    }

    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    if (now_ms < g_river_cloud.xiaozhi_local_close_deadline_ms) {
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
    g_river_cloud.xiaozhi_uplink_retry_valid = false;
    g_river_cloud.xiaozhi_uplink_next_send_ms = 0U;
    g_river_cloud.xiaozhi_uplink_busy_streak = 0U;
    river_cloud_xiaozhi_clear_local_close_defer();
    river_cloud_xiaozhi_clear_endpoint_soft_close_state();
    river_audio_frame_ring_reset(&g_river_cloud.xiaozhi_uplink_ring);
    river_cloud_xiaozhi_apply_transport_reset_playback_policy();
    river_cloud_xiaozhi_clear_pending_text();
    river_cloud_xiaozhi_clear_preview_state();
    river_cloud_xiaozhi_clear_turn_semantics_state();
    river_cloud_pre_roll_reset();
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

    if (g_river_cloud.stream_active || river_cloud_xiaozhi_playback_has_work()) {
        return;
    }

    river_cloud_xiaozhi_window_close("followup_timeout");
}
#endif
