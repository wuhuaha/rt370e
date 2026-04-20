/* 小智 round/window runtime：收口 listening/window/listen-stop/local-close 状态。 */
#include "river/river_log.h"
#include "river/river_runtime_stats.h"

#include "river_cloud_internal.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.cloud"

#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
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
#endif
