static void river_cloud_xiaozhi_set_playback_terminal_ack(const char *ack,
                                                          const char *reason)
{
    river_cloud_playback_terminal_state_t state =
        river_cloud_xiaozhi_playback_terminal_state_from_ack(ack);

    river_cloud_xiaozhi_copy_optional_text(
                                           g_river_cloud.xiaozhi_playback_terminal_truth.ack,
                                           sizeof(g_river_cloud.xiaozhi_playback_terminal_truth.ack),
                                           ack);
    river_cloud_xiaozhi_set_playback_terminal_state(state, reason);
    river_cloud_xiaozhi_sync_playback_terminal_report_flags();
}

static const char *river_cloud_xiaozhi_playback_abort_cause_name(
    river_cloud_xiaozhi_playback_abort_cause_t cause)
{
    switch (cause) {
    case RIVER_CLOUD_XIAOZHI_PLAYBACK_ABORT_INTERRUPT:
        return "interrupt";
    case RIVER_CLOUD_XIAOZHI_PLAYBACK_ABORT_TRANSPORT_CLOSED:
        return "transport_closed";
    case RIVER_CLOUD_XIAOZHI_PLAYBACK_ABORT_NETWORK_LOST:
        return "network_lost";
    case RIVER_CLOUD_XIAOZHI_PLAYBACK_ABORT_BRIDGE_CLOSE:
        return "bridge_close";
    case RIVER_CLOUD_XIAOZHI_PLAYBACK_ABORT_FRAME_OVERSIZE:
        return "frame_oversize";
    default:
        return "unknown";
    }
}

static const char *river_cloud_xiaozhi_playback_abort_clear_reason(
    river_cloud_xiaozhi_playback_abort_cause_t cause,
    const char *detail_reason)
{
    if (cause == RIVER_CLOUD_XIAOZHI_PLAYBACK_ABORT_INTERRUPT) {
        return (detail_reason != NULL && detail_reason[0] != '\0') ?
                   detail_reason :
                   "xiaozhi_interrupt";
    }

    return river_cloud_xiaozhi_playback_abort_cause_name(cause);
}

static const char *river_cloud_xiaozhi_playback_abort_stream_reason(
    river_cloud_xiaozhi_playback_abort_cause_t cause,
    const char *detail_reason)
{
    if (cause == RIVER_CLOUD_XIAOZHI_PLAYBACK_ABORT_INTERRUPT) {
        return (detail_reason != NULL && detail_reason[0] != '\0') ?
                   detail_reason :
                   "xiaozhi_interrupt";
    }
    switch (cause) {
    case RIVER_CLOUD_XIAOZHI_PLAYBACK_ABORT_TRANSPORT_CLOSED:
        return "xiaozhi_transport_closed";
    case RIVER_CLOUD_XIAOZHI_PLAYBACK_ABORT_NETWORK_LOST:
        return "xiaozhi_network_lost";
    case RIVER_CLOUD_XIAOZHI_PLAYBACK_ABORT_BRIDGE_CLOSE:
        return "xiaozhi_bridge_close";
    case RIVER_CLOUD_XIAOZHI_PLAYBACK_ABORT_FRAME_OVERSIZE:
        return "xiaozhi_downlink_frame_oversize";
    default:
        return "xiaozhi_playback_abort";
    }
}

static bool river_cloud_xiaozhi_playback_abort_interrupt_stream(
    river_cloud_xiaozhi_playback_abort_cause_t cause)
{
    return cause == RIVER_CLOUD_XIAOZHI_PLAYBACK_ABORT_INTERRUPT;
}

#include "river_cloud_xiaozhi_playback_terminal_ack.inc"

void river_cloud_xiaozhi_playback_check_pending_stop(void)
{
    river_cloud_xiaozhi_playback_truth_view_t truth_view;
    uint64_t now_ms;

    river_cloud_xiaozhi_update_playback_ack_progress();
    if (!g_river_cloud.xiaozhi_playback_runtime_truth.stop_pending) {
        river_cloud_xiaozhi_clear_playback_terminal_wait();
        return;
    }

    river_cloud_xiaozhi_capture_playback_truth_view(&truth_view);
    if (!river_cloud_xiaozhi_playback_backend_stream_attached(
            truth_view.backend_state)) {
        river_cloud_xiaozhi_drop_nonattached_pending_stop_audio(
            truth_view.backend_state);
        if (!river_cloud_xiaozhi_try_queue_playback_completed_ack()) {
            return;
        }
        g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_pending = false;
        river_cloud_xiaozhi_reset_playback_state();
        return;
    }

    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    if (now_ms < g_river_cloud.xiaozhi_playback_runtime_truth.tts_stop_deadline_ms) {
        return;
    }

    (void)river_playback_service_stop_stream_ex("xiaozhi_tts_stop");
    if (!river_cloud_xiaozhi_try_queue_playback_completed_ack()) {
        return;
    }
    river_cloud_xiaozhi_reset_playback_state();
}

void river_cloud_xiaozhi_apply_playback_backend_refresh_policy(void)
{
    if (g_river_cloud.xiaozhi_enabled) {
        river_cloud_xiaozhi_playback_start_downlink_if_needed();
    }

    river_cloud_xiaozhi_reset_playback_state();
}

void river_cloud_xiaozhi_apply_terminal_playback_policy(
    river_cloud_xiaozhi_playback_abort_cause_t cause)
{
    if (river_cloud_xiaozhi_playback_turn_active()) {
        (void)river_cloud_xiaozhi_playback_abort_for_cause(cause, NULL);
    }

    river_opus_decoder_close(&g_river_cloud.xiaozhi_decoder);
}

river_status_t river_cloud_xiaozhi_playback_abort_for_cause(
    river_cloud_xiaozhi_playback_abort_cause_t cause,
    const char *detail_reason)
{
    river_cloud_xiaozhi_playback_truth_view_t truth_view;
    river_status_t status = RIVER_OK;
    bool had_work = river_cloud_xiaozhi_playback_turn_active();
    bool stream_was_attached;
    const char *clear_reason =
        river_cloud_xiaozhi_playback_abort_clear_reason(cause, detail_reason);
    const char *stream_reason =
        river_cloud_xiaozhi_playback_abort_stream_reason(cause, detail_reason);
    bool interrupt_stream =
        river_cloud_xiaozhi_playback_abort_interrupt_stream(cause);

    river_cloud_xiaozhi_capture_playback_truth_view(&truth_view);
    stream_was_attached = river_cloud_xiaozhi_playback_backend_stream_attached(
        truth_view.backend_state);

    if (had_work) {
        river_cloud_xiaozhi_playback_finalize_cleared(clear_reason);
        river_cloud_xiaozhi_reset_downlink_state();
    }

    if (stream_was_attached) {
        const char *resolved_reason =
            (stream_reason != NULL && stream_reason[0] != '\0') ?
                stream_reason :
                "xiaozhi_playback_abort";

        status = interrupt_stream ?
                     river_playback_service_interrupt_stream_ex(resolved_reason) :
                     river_playback_service_stop_stream_ex(resolved_reason);
    }

    RIVER_LOGI("xiaozhi playback abort: cause=%s clear_reason=%s stream_reason=%s interrupt=%s had_work=%s stream_attached=%s phase=%s hold=%s backend=%s",
               river_cloud_xiaozhi_playback_abort_cause_name(cause),
               clear_reason != NULL && clear_reason[0] != '\0' ? clear_reason : "-",
               stream_reason != NULL && stream_reason[0] != '\0' ? stream_reason : "-",
               interrupt_stream ? "yes" : "no",
               had_work ? "yes" : "no",
               stream_was_attached ? "yes" : "no",
               river_cloud_playback_phase_name(
                   river_cloud_xiaozhi_playback_observed_phase_kind()),
               river_cloud_playback_hold_kind_name(truth_view.hold_kind),
               river_cloud_playback_backend_state_name(truth_view.backend_state));
    river_cloud_xiaozhi_reset_playback_state();
    return status;
}
