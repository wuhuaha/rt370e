void river_cloud_xiaozhi_dump_playback_status(uint64_t now_ms)
{
    river_voice_duplex_ready_eval_t duplex_eval;
    river_cloud_xiaozhi_playback_diag_view_t diag_view;
    river_cloud_xiaozhi_playback_meta_truth_t *meta_truth =
        &g_river_cloud.xiaozhi_playback_meta_truth;
    river_cloud_xiaozhi_playback_terminal_truth_t *terminal_truth =
        &g_river_cloud.xiaozhi_playback_terminal_truth;
    river_cloud_xiaozhi_playback_lineage_truth_t *lineage_truth =
        &g_river_cloud.xiaozhi_playback_lineage_truth;

    river_cloud_xiaozhi_capture_playback_diag_view(now_ms, &diag_view);
    river_cloud_xiaozhi_sync_playback_terminal_report_flags();
    river_cloud_xiaozhi_get_duplex_ready_eval(&duplex_eval);
    RIVER_LOGI("xiaozhi playback_meta response_id=%s playback_id=%s segment_id=%s output_lane=%s output_role=%s phrase_id=%s text=%s expected_duration_ms=%lu is_last_segment=%s started_ack=%s completed_ack=%s valid=%s ref_enabled=%s",
               lineage_truth->meta_context.response_id[0] != '\0' ?
                   lineage_truth->meta_context.response_id :
                   "-",
               lineage_truth->meta_context.playback_id[0] != '\0' ?
                   lineage_truth->meta_context.playback_id :
                   "-",
               lineage_truth->meta_context.segment_id[0] != '\0' ?
                   lineage_truth->meta_context.segment_id :
                   "-",
               meta_truth->output_lane[0] != '\0' ? meta_truth->output_lane : "-",
               meta_truth->output_role[0] != '\0' ? meta_truth->output_role : "-",
               meta_truth->phrase_id[0] != '\0' ? meta_truth->phrase_id : "-",
               meta_truth->text[0] != '\0' ? meta_truth->text : "-",
               (unsigned long)meta_truth->expected_duration_ms,
               river_cloud_xiaozhi_playback_current_meta_is_last_segment() ? "yes" : "no",
               terminal_truth->started_reported ? "yes" : "no",
               terminal_truth->completed_reported ? "yes" : "no",
               river_cloud_xiaozhi_playback_segment_context_valid() ? "yes" : "no",
               river_playback_service_reference_enabled() ? "yes" : "no");
    RIVER_LOGI("xiaozhi playback_terminal phase=%s hold=%s state=%s ack=%s reason=%s wait=%s wait_reason=%s queued_segments=%lu last_started=%s last_fully_heard=%s",
               river_cloud_playback_phase_name(diag_view.observe_view.phase_kind),
               river_cloud_playback_hold_kind_name(diag_view.truth_view.hold_kind),
               diag_view.terminal_state_kind !=
                       RIVER_CLOUD_PLAYBACK_TERMINAL_STATE_NONE ?
                   river_cloud_playback_terminal_state_name(
                       diag_view.terminal_state_kind) :
                   "-",
               terminal_truth->ack[0] != '\0' ? terminal_truth->ack : "-",
               terminal_truth->clear_reason[0] != '\0' ?
                   terminal_truth->clear_reason :
                   "-",
               diag_view.terminal_waiting ? "yes" : "no",
               terminal_truth->wait_reason[0] != '\0' ?
                   terminal_truth->wait_reason :
                   "-",
               (unsigned long)g_river_cloud.xiaozhi_playback_segment_queue_truth.count,
               lineage_truth->started_context.segment_id[0] != '\0' ?
                   lineage_truth->started_context.segment_id :
                   "-",
               lineage_truth->fully_heard_context.segment_id[0] != '\0' ?
                   lineage_truth->fully_heard_context.segment_id :
                   "-");
    RIVER_LOGI("xiaozhi playback_heard_context response_id=%s playback_id=%s segment_id=%s valid=%s",
               lineage_truth->fully_heard_context.response_id[0] != '\0' ?
                   lineage_truth->fully_heard_context.response_id :
                   "-",
               lineage_truth->fully_heard_context.playback_id[0] != '\0' ?
                   lineage_truth->fully_heard_context.playback_id :
                   "-",
               lineage_truth->fully_heard_context.segment_id[0] != '\0' ?
                   lineage_truth->fully_heard_context.segment_id :
                   "-",
               river_cloud_xiaozhi_playback_last_fully_heard_context_valid() ? "yes" :
                                                                                "no");
    RIVER_LOGI("xiaozhi playback_terminal_context response_id=%s playback_id=%s segment_id=%s valid=%s",
               lineage_truth->last_segment_context.response_id[0] != '\0' ?
                   lineage_truth->last_segment_context.response_id :
                   "-",
               lineage_truth->last_segment_context.playback_id[0] != '\0' ?
                   lineage_truth->last_segment_context.playback_id :
                   "-",
               lineage_truth->last_segment_context.segment_id[0] != '\0' ?
                   lineage_truth->last_segment_context.segment_id :
                   "-",
               river_cloud_xiaozhi_playback_last_segment_observed() ? "yes" : "no");
    RIVER_LOGI("xiaozhi playback_lineage stage=%s response_id=%s meta=%s started=%s mark=%s/%lums cleared=%s completed=%s reason=%s",
               river_cloud_xiaozhi_playback_lineage_stage_name(lineage_truth->stage),
               lineage_truth->response_context.response_id[0] != '\0' ?
                   lineage_truth->response_context.response_id :
                   "-",
               lineage_truth->meta_context.segment_id[0] != '\0' ?
                   lineage_truth->meta_context.segment_id :
                   "-",
               lineage_truth->started_context.segment_id[0] != '\0' ?
                   lineage_truth->started_context.segment_id :
                   "-",
               lineage_truth->marked_context.segment_id[0] != '\0' ?
                   lineage_truth->marked_context.segment_id :
                   "-",
               (unsigned long)lineage_truth->last_mark_ms,
               lineage_truth->cleared_context.segment_id[0] != '\0' ?
                   lineage_truth->cleared_context.segment_id :
                   "-",
               lineage_truth->completed_context.playback_id[0] != '\0' ?
                   lineage_truth->completed_context.playback_id :
                   "-",
               lineage_truth->reason[0] != '\0' ? lineage_truth->reason : "-");
    RIVER_LOGI("xiaozhi duplex_ready=%s reason=%s aec=%s playback=%s/%s error=%s ref_state=%s ref_activity=%s ref_peak=%u ref_ratio_q15=%u ref_queue=%lu/%lu ref_age_ms=%lu playback_physical=%s backend=%s duplex_seen=%s",
               duplex_eval.ready ? "yes" : "no",
               river_voice_runtime_duplex_ready_reason_name(duplex_eval.reason),
               river_voice_runtime_aec_gate_reason_name(duplex_eval.aec_reason),
               river_playback_service_state_name(duplex_eval.playback_state),
               river_dialog_playback_owner_kind_name(
                   duplex_eval.dialog_playback_owner_kind),
               river_dialog_error_kind_name(duplex_eval.dialog_error_kind),
               river_reference_service_state_name(duplex_eval.reference_state),
               river_voice_runtime_reference_activity_name(duplex_eval.reference_activity),
               (unsigned int)duplex_eval.native_reference_peak,
               (unsigned int)duplex_eval.native_reference_ratio_q15,
               (unsigned long)duplex_eval.reference_queue_frames,
               (unsigned long)duplex_eval.reference_queue_peak_frames,
               (unsigned long)duplex_eval.reference_last_write_age_ms,
               river_cloud_xiaozhi_playback_physical_active() ? "yes" : "no",
               river_cloud_playback_backend_state_name(
                   diag_view.truth_view.backend_state),
               terminal_truth->duplex_ready_seen ? "yes" : "no");
    RIVER_LOGI("xiaozhi duplex_policy default_on=%s default_reason=%s voice_collaboration=%s server_endpoint=%s/%s preview_events=%s playback_ack=%s",
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
    RIVER_LOGI("xiaozhi no_ref reopen rearm=%s silence=%lu/%u guard_left_ms=%lu open_hold_frames=%u",
               g_river_cloud.xiaozhi_playback_gate_truth.no_ref_reopen_rearm ? "yes" : "no",
               (unsigned long)g_river_cloud.xiaozhi_playback_gate_truth.no_ref_reopen_silence_frames,
               (unsigned int)RIVER_CLOUD_XIAOZHI_NOREF_REARM_SILENCE_FRAMES,
               (unsigned long)diag_view.reopen_guard_left_ms,
               (unsigned int)diag_view.open_hold_frames);
    RIVER_LOGI("xiaozhi downlink queue=%lu/%u dropped=%lu worker=%s wait=%s/%lums outcome=%s cycle_status=%d sample=%luHz frame=%lums start=%u resume=%u buffer=%u policy=%s cautious=%s target_ms=%lu prefetch_frames=%u meta_gap_ms=%lu supply=%s phase=%s backend=%s hold=%s rebuffer=%s/%s recovery_path=%s recovery_outcome=%s rebuffer_total=%lu streak=%lu",
               (unsigned long)diag_view.queued_frames,
               (unsigned int)RIVER_CLOUD_XIAOZHI_DOWNLINK_RING_FRAMES,
               (unsigned long)diag_view.ring_dropped,
               diag_view.downlink_started ? "running" : "off",
               river_cloud_xiaozhi_downlink_wait_kind_name(
                   diag_view.downlink_wait_kind),
               (unsigned long)diag_view.downlink_wait_delay_ms,
               river_cloud_xiaozhi_downlink_cycle_outcome_name(
                   diag_view.downlink_cycle_outcome),
               (int)diag_view.downlink_cycle_status,
               (unsigned long)diag_view.sample_rate,
               (unsigned long)diag_view.frame_duration_ms,
               (unsigned int)diag_view.start_gate.start_frames,
               (unsigned int)diag_view.resume_frames,
               (unsigned int)diag_view.buffer_budget_frames,
               river_cloud_playback_start_policy_name(diag_view.start_gate.policy),
               diag_view.start_gate.cautious_history ? "yes" : "no",
               (unsigned long)diag_view.prefetch_target_ms,
               (unsigned int)diag_view.start_gate.prefetch_frames,
               (unsigned long)diag_view.meta_gap_ms,
               river_cloud_xiaozhi_playback_supply_kind_name(
                   diag_view.truth_view.supply_kind),
               river_cloud_playback_phase_name(diag_view.observe_view.phase_kind),
               river_cloud_playback_backend_state_name(
                   diag_view.truth_view.backend_state),
               river_cloud_playback_hold_kind_name(diag_view.truth_view.hold_kind),
               diag_view.truth_view.rebuffer_pending ? "yes" : "no",
               river_cloud_playback_rebuffer_cause_name(
                   diag_view.rebuffer_cause_kind),
               river_cloud_xiaozhi_playback_recovery_path_label(
                   diag_view.observe_view.recovery_path_kind,
                   "-"),
               river_cloud_xiaozhi_playback_recovery_outcome_label(
                   diag_view.observe_view.recovery_outcome_kind,
                   "-"),
               (unsigned long)diag_view.rebuffer_count,
               (unsigned long)diag_view.rebuffer_streak);
}

void river_cloud_xiaozhi_fill_playback_runtime_snapshot(
    river_cloud_runtime_snapshot_t *snapshot)
{
    river_cloud_xiaozhi_playback_diag_view_t diag_view;

    if (snapshot == NULL) {
        return;
    }

    river_cloud_xiaozhi_capture_playback_diag_view(0U, &diag_view);
    snapshot->playback_active = diag_view.truth_view.output_active;
    snapshot->playback_lane_engaged =
        river_cloud_xiaozhi_playback_lane_engaged_from_truth_view(
            &diag_view.truth_view);
    snapshot->playback_turn_active =
        river_cloud_xiaozhi_playback_turn_active_from_truth_view(
            &diag_view.truth_view);
    snapshot->playback_rebuffer_pending = diag_view.truth_view.rebuffer_pending;
    snapshot->playback_phase_known = diag_view.observe_view.phase_known;
    snapshot->playback_backend_state_kind = diag_view.truth_view.backend_state;
    snapshot->playback_supply_kind = diag_view.truth_view.supply_kind;
    snapshot->playback_hold_kind = diag_view.truth_view.hold_kind;
    snapshot->playback_terminal_closed = diag_view.terminal_closed;
    snapshot->playback_terminal_waiting = diag_view.terminal_waiting;
    snapshot->playback_phase_kind = diag_view.observe_view.phase_kind;
    snapshot->playback_terminal_wait_kind = diag_view.terminal_wait_kind;
    snapshot->playback_terminal_state_kind = diag_view.terminal_state_kind;
    snapshot->playback_rebuffer_cause_kind = diag_view.rebuffer_cause_kind;
    snapshot->playback_recovery_path_kind =
        diag_view.observe_view.recovery_path_kind;
    snapshot->playback_recovery_outcome_kind =
        diag_view.observe_view.recovery_outcome_kind;
    snapshot->playback_start_policy_kind = diag_view.start_gate.policy;
    snapshot->tts_stop_pending = diag_view.tts_stop_pending;
    river_cloud_xiaozhi_copy_optional_text(snapshot->playback_phase,
                                           sizeof(snapshot->playback_phase),
                                           river_cloud_playback_phase_name(
                                               diag_view.observe_view.phase_kind));
    river_cloud_xiaozhi_copy_optional_text(
        snapshot->playback_rebuffer_cause,
        sizeof(snapshot->playback_rebuffer_cause),
        diag_view.rebuffer_cause_kind != RIVER_CLOUD_PLAYBACK_REBUFFER_CAUSE_NONE ?
            river_cloud_playback_rebuffer_cause_name(
                diag_view.rebuffer_cause_kind) :
            NULL);
    river_cloud_xiaozhi_copy_optional_text(
        snapshot->playback_recovery_path,
        sizeof(snapshot->playback_recovery_path),
        diag_view.observe_view.recovery_path_kind !=
                RIVER_CLOUD_PLAYBACK_RECOVERY_PATH_NONE ?
            river_cloud_playback_recovery_path_name(
                diag_view.observe_view.recovery_path_kind) :
            NULL);
    river_cloud_xiaozhi_copy_optional_text(
        snapshot->playback_recovery_outcome,
        sizeof(snapshot->playback_recovery_outcome),
        diag_view.observe_view.recovery_outcome_kind !=
                RIVER_CLOUD_PLAYBACK_RECOVERY_OUTCOME_NONE ?
            river_cloud_playback_recovery_outcome_name(
                diag_view.observe_view.recovery_outcome_kind) :
            NULL);
    river_cloud_xiaozhi_copy_optional_text(
        snapshot->playback_start_policy,
        sizeof(snapshot->playback_start_policy),
        diag_view.start_gate.start_frames != 0U ?
            river_cloud_playback_start_policy_name(
                diag_view.start_gate.policy) :
            NULL);
    river_cloud_xiaozhi_copy_optional_text(snapshot->playback_terminal_state,
                                           sizeof(snapshot->playback_terminal_state),
                                           diag_view.terminal_state_kind !=
                                                   RIVER_CLOUD_PLAYBACK_TERMINAL_STATE_NONE ?
                                               river_cloud_playback_terminal_state_name(
                                                   diag_view.terminal_state_kind) :
                                               NULL);
    river_cloud_xiaozhi_copy_optional_text(snapshot->playback_terminal_reason,
                                           sizeof(snapshot->playback_terminal_reason),
                                           g_river_cloud.xiaozhi_playback_terminal_truth.clear_reason);
    river_cloud_xiaozhi_copy_optional_text(snapshot->playback_terminal_wait_reason,
                                           sizeof(snapshot->playback_terminal_wait_reason),
                                           g_river_cloud.xiaozhi_playback_terminal_truth.wait_reason);
    snapshot->playback_start_frames = diag_view.start_frames;
    snapshot->playback_prefetch_frames = diag_view.prefetch_frames;
    snapshot->playback_start_cautious_history = diag_view.start_cautious_history;
}

void river_cloud_xiaozhi_clear_playback_meta_state(void)
{
    river_cloud_xiaozhi_clear_playback_lineage_truth();
    g_river_cloud.xiaozhi_playback_terminal_truth.started_reported = false;
    g_river_cloud.xiaozhi_playback_terminal_truth.cleared_reported = false;
    g_river_cloud.xiaozhi_playback_terminal_truth.completed_reported = false;
    g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_cause =
        RIVER_CLOUD_PLAYBACK_REBUFFER_CAUSE_NONE;
    g_river_cloud.xiaozhi_playback_runtime_truth.recovery_path =
        RIVER_CLOUD_PLAYBACK_RECOVERY_PATH_NONE;
    g_river_cloud.xiaozhi_playback_runtime_truth.recovery_outcome =
        RIVER_CLOUD_PLAYBACK_RECOVERY_OUTCOME_NONE;
    g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_pending = false;
    g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_count = 0U;
    g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_streak = 0U;
    g_river_cloud.xiaozhi_playback_segment_queue_truth.head = 0U;
    g_river_cloud.xiaozhi_playback_segment_queue_truth.count = 0U;
    g_river_cloud.xiaozhi_playback_meta_truth.expected_duration_ms = 0U;
    g_river_cloud.xiaozhi_playback_meta_truth.last_meta_gap_ms = 0U;
    g_river_cloud.xiaozhi_playback_meta_truth.prefetch_target_ms = 0U;
    g_river_cloud.xiaozhi_playback_gate_truth.buffer_frames = 0U;
    g_river_cloud.xiaozhi_playback_meta_truth.last_meta_ms = 0U;
    g_river_cloud.xiaozhi_playback_terminal_truth.duplex_ready_seen = false;
    river_cloud_xiaozhi_clear_playback_context_truth(
        &g_river_cloud.xiaozhi_playback_meta_truth.current_context);
    river_cloud_xiaozhi_clear_playback_wait_context();
    river_cloud_xiaozhi_clear_playback_context_truth(
        &g_river_cloud.xiaozhi_playback_terminal_truth.last_segment_context);
    river_cloud_xiaozhi_clear_playback_context_truth(
        &g_river_cloud.xiaozhi_playback_terminal_truth.last_fully_heard_context);
    g_river_cloud.xiaozhi_playback_terminal_truth.last_started_segment_id[0] = '\0';
    g_river_cloud.xiaozhi_playback_terminal_truth.ack[0] = '\0';
    g_river_cloud.xiaozhi_playback_terminal_truth.state_kind =
        RIVER_CLOUD_PLAYBACK_TERMINAL_STATE_NONE;
    g_river_cloud.xiaozhi_playback_terminal_truth.clear_reason[0] = '\0';
    river_cloud_xiaozhi_clear_playback_terminal_wait();
    g_river_cloud.xiaozhi_playback_meta_truth.text[0] = '\0';
    g_river_cloud.xiaozhi_playback_meta_truth.output_lane[0] = '\0';
    g_river_cloud.xiaozhi_playback_meta_truth.output_role[0] = '\0';
    g_river_cloud.xiaozhi_playback_meta_truth.phrase_id[0] = '\0';
    (void)river_cloud_xiaozhi_refresh_start_gate();
    memset(g_river_cloud.xiaozhi_playback_segment_queue_truth.segments,
           0,
           sizeof(g_river_cloud.xiaozhi_playback_segment_queue_truth.segments));
    river_cloud_xiaozhi_refresh_playback_phase("clear_meta");
}

bool river_cloud_xiaozhi_apply_capture_entry_playback_policy(void)
{
    river_voice_duplex_ready_eval_t duplex_eval;
    const char *duplex_fallback_reason;

    river_cloud_xiaozhi_playback_check_pending_stop();
    if (g_river_cloud.stream_active) {
        return false;
    }

    river_cloud_xiaozhi_get_duplex_ready_eval(&duplex_eval);
    if (!river_cloud_xiaozhi_capture_held_by_playback(&duplex_eval,
                                                      &duplex_fallback_reason)) {
        return false;
    }

    river_cloud_xiaozhi_note_semantic_fallback(duplex_fallback_reason);
    RIVER_LOGI("xiaozhi capture held during playback: fallback=%s duplex_default_on=%s duplex_ready=%s reason=%s aec=%s playback=%s/%s error=%s ref_state=%s ref_activity=%s ref_peak=%u ref_ratio_q15=%u",
               duplex_fallback_reason,
               river_xiaozhi_duplex_default_on_allowed() ? "yes" : "no",
               duplex_eval.ready ? "yes" : "no",
               river_voice_runtime_duplex_ready_reason_name(duplex_eval.reason),
               river_voice_runtime_aec_gate_reason_name(duplex_eval.aec_reason),
               river_playback_service_state_name(duplex_eval.playback_state),
               river_dialog_playback_owner_kind_name(
                   duplex_eval.dialog_playback_owner_kind),
               river_dialog_error_kind_name(duplex_eval.dialog_error_kind),
               river_reference_service_state_name(duplex_eval.reference_state),
               river_voice_runtime_reference_activity_name(duplex_eval.reference_activity),
               (unsigned int)duplex_eval.native_reference_peak,
               (unsigned int)duplex_eval.native_reference_ratio_q15);
    g_river_cloud.xiaozhi_uplink_runtime_truth.open_speech_frames = 0U;
    river_cloud_pre_roll_reset();
    return true;
}

void river_cloud_xiaozhi_apply_capture_exit_playback_policy(void)
{
    river_cloud_xiaozhi_playback_check_pending_stop();
}

void river_cloud_xiaozhi_apply_transport_reset_playback_policy(void)
{
    river_cloud_xiaozhi_playback_truth_view_t truth_view;

    river_cloud_xiaozhi_capture_playback_truth_view(&truth_view);
    if (river_cloud_xiaozhi_playback_backend_stream_attached(
            truth_view.backend_state)) {
        (void)river_playback_service_stop_stream_ex("xiaozhi_transport_reset");
    }
    river_cloud_xiaozhi_reset_downlink_state();
    river_cloud_xiaozhi_clear_playback_meta_state();
    river_cloud_xiaozhi_clear_followup_reopen_state();
}

void river_cloud_xiaozhi_apply_session_start_playback_policy(void)
{
    river_cloud_xiaozhi_playback_truth_view_t truth_view;

    river_cloud_xiaozhi_capture_playback_truth_view(&truth_view);
    if (river_cloud_xiaozhi_playback_backend_stream_attached(
            truth_view.backend_state)) {
        (void)river_playback_service_stop_stream_ex("xiaozhi_session_start");
    }
    river_cloud_xiaozhi_clear_playback_meta_state();
    river_cloud_xiaozhi_clear_followup_reopen_state();
}

void river_cloud_xiaozhi_cancel_playback_stop(void)
{
    g_river_cloud.xiaozhi_playback_runtime_truth.stop_pending = false;
    g_river_cloud.xiaozhi_playback_runtime_truth.tts_stop_deadline_ms = 0U;
    river_cloud_xiaozhi_clear_playback_terminal_wait();
    river_cloud_xiaozhi_refresh_playback_phase("cancel_stop");
}

void river_cloud_xiaozhi_playback_note_duplex_ready(void)
{
    river_cloud_xiaozhi_playback_truth_view_t truth_view;

    river_cloud_xiaozhi_capture_playback_truth_view(&truth_view);
    if (truth_view.output_active) {
        g_river_cloud.xiaozhi_playback_terminal_truth.duplex_ready_seen = true;
    }
}

void river_cloud_xiaozhi_mark_playback_started(void)
{
    g_river_cloud.xiaozhi_playback_runtime_truth.active = true;
    g_river_cloud.xiaozhi_playback_terminal_truth.duplex_ready_seen = false;
    river_cloud_xiaozhi_clear_playback_terminal_wait();
    river_cloud_xiaozhi_cancel_playback_stop();
    river_cloud_xiaozhi_clear_followup_reopen_state();
    river_cloud_xiaozhi_refresh_playback_phase("playback_started");
    river_cloud_xiaozhi_apply_playback_started_round_policy();
}

void river_cloud_xiaozhi_arm_playback_stop(uint32_t drain_ms)
{
    if (!river_cloud_xiaozhi_playback_physical_active() || drain_ms == 0U) {
        return;
    }

    g_river_cloud.xiaozhi_playback_runtime_truth.stop_pending = true;
    g_river_cloud.xiaozhi_playback_runtime_truth.tts_stop_deadline_ms =
        (uint64_t)rtos_time_get_current_system_time_ms() + (uint64_t)drain_ms;
    river_cloud_xiaozhi_refresh_playback_phase("arm_stop");
}

void river_cloud_xiaozhi_reset_playback_state(void)
{
    river_cloud_xiaozhi_playback_truth_view_t truth_view;
    bool had_playback_activity;
    bool duplex_ready_seen =
        g_river_cloud.xiaozhi_playback_terminal_truth.duplex_ready_seen;

    river_cloud_xiaozhi_capture_playback_truth_view(&truth_view);
    had_playback_activity = truth_view.output_active;
    g_river_cloud.xiaozhi_playback_runtime_truth.active = false;
    g_river_cloud.xiaozhi_playback_gate_truth.buffer_frames = 0U;
    river_cloud_xiaozhi_cancel_playback_stop();
    river_cloud_xiaozhi_clear_downlink_starvation_watch();
    river_cloud_xiaozhi_clear_playback_terminal_wait();
    if (had_playback_activity && !duplex_ready_seen) {
        g_river_cloud.xiaozhi_playback_gate_truth.no_ref_reopen_rearm = true;
        g_river_cloud.xiaozhi_playback_gate_truth.no_ref_reopen_silence_frames = 0U;
        g_river_cloud.xiaozhi_playback_gate_truth.no_ref_reopen_guard_deadline_ms =
            (uint64_t)rtos_time_get_current_system_time_ms() +
            (uint64_t)RIVER_CLOUD_XIAOZHI_NOREF_REOPEN_GUARD_MS;
        RIVER_LOGI("xiaozhi no_ref reopen guard armed: tail_ms=%u silence_frames=%u duplex_seen=no",
                   (unsigned int)RIVER_CLOUD_XIAOZHI_NOREF_REOPEN_GUARD_MS,
                   (unsigned int)RIVER_CLOUD_XIAOZHI_NOREF_REARM_SILENCE_FRAMES);
    } else if (!had_playback_activity || duplex_ready_seen) {
        river_cloud_xiaozhi_clear_followup_reopen_state();
    }
    g_river_cloud.xiaozhi_playback_terminal_truth.duplex_ready_seen = false;
    (void)river_cloud_xiaozhi_refresh_start_gate();
    river_cloud_xiaozhi_refresh_playback_phase("reset_playback_state");
}

static void river_cloud_xiaozhi_mark_playback_backend_paused(const char *reason)
{
    g_river_cloud.xiaozhi_playback_runtime_truth.active = false;
    river_cloud_xiaozhi_cancel_playback_stop();
    river_cloud_xiaozhi_clear_downlink_starvation_watch();
    river_cloud_xiaozhi_clear_playback_terminal_wait();
    river_cloud_xiaozhi_clear_followup_reopen_state();
    g_river_cloud.xiaozhi_playback_terminal_truth.duplex_ready_seen = false;
    (void)river_cloud_xiaozhi_refresh_start_gate();
    river_cloud_xiaozhi_refresh_playback_phase(reason);
}

static void river_cloud_xiaozhi_pause_playback_for_segment_gap(void)
{
    river_cloud_xiaozhi_mark_playback_backend_paused("segment_gap_pause");
}

static void river_cloud_xiaozhi_pause_playback_for_rebuffer(void)
{
    river_cloud_xiaozhi_mark_playback_backend_paused("rebuffer_pause");
}

static river_status_t river_cloud_xiaozhi_hold_playback_for_segment_gap(const char *reason,
                                                                        bool *attached_hold)
{
    river_cloud_xiaozhi_playback_truth_view_t truth_view;
    river_status_t status;
    bool attached = false;

    river_cloud_xiaozhi_capture_playback_truth_view(&truth_view);
    if (truth_view.backend_state == RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_PAUSED &&
        river_cloud_xiaozhi_playback_terminal_open() &&
        truth_view.supply_kind == RIVER_CLOUD_PLAYBACK_SUPPLY_WAITING_NEXT_SEGMENT) {
        if (attached_hold != NULL) {
            *attached_hold = true;
        }
        return RIVER_OK;
    }

    status = river_playback_service_recover_stream_ex(reason);
    if (status == RIVER_OK) {
        attached = true;
    } else {
        status = river_playback_service_stop_stream_ex(reason);
    }

    if (status == RIVER_OK) {
        river_cloud_xiaozhi_pause_playback_for_segment_gap();
    }
    if (attached_hold != NULL) {
        *attached_hold = attached;
    }
    return status;
}

static river_status_t river_cloud_xiaozhi_stop_playback_for_rebuffer(const char *reason)
{
    river_status_t status = river_playback_service_stop_stream_ex(reason);

    if (status == RIVER_OK) {
        river_cloud_xiaozhi_pause_playback_for_rebuffer();
    }
    return status;
}

void river_cloud_xiaozhi_reset_downlink_state(void)
{
    river_cloud_xiaozhi_reset_downlink_ring_runtime();
    g_river_cloud.xiaozhi_downlink_runtime_truth.ring_dropped = 0U;
    g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_cause =
        RIVER_CLOUD_PLAYBACK_REBUFFER_CAUSE_NONE;
    g_river_cloud.xiaozhi_playback_runtime_truth.recovery_path =
        RIVER_CLOUD_PLAYBACK_RECOVERY_PATH_NONE;
    g_river_cloud.xiaozhi_playback_runtime_truth.recovery_outcome =
        RIVER_CLOUD_PLAYBACK_RECOVERY_OUTCOME_NONE;
    g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_pending = false;
    g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_count = 0U;
    g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_streak = 0U;
    river_cloud_xiaozhi_clear_downlink_starvation_watch();
    river_cloud_xiaozhi_clear_playback_terminal_wait();
    (void)river_cloud_xiaozhi_refresh_start_gate();
    river_cloud_xiaozhi_refresh_playback_phase("reset_downlink_state");
}

static void river_cloud_xiaozhi_drop_nonattached_pending_stop_audio(
    river_cloud_playback_backend_state_t backend_state)
{
    uint32_t queued_frames = river_cloud_xiaozhi_playback_queued_frames();

    if (queued_frames == 0U) {
        return;
    }

    river_cloud_xiaozhi_reset_downlink_ring_runtime();
    RIVER_LOGI("xiaozhi pending stop drops non-attached queued audio: queued=%lu backend=%s phase=%s rebuffer=%s/%s",
               (unsigned long)queued_frames,
               river_cloud_playback_backend_state_name(backend_state),
               river_cloud_playback_phase_name(
                   river_cloud_xiaozhi_playback_phase()),
               g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_pending ? "yes" :
                                                                               "no",
               river_cloud_playback_rebuffer_cause_name(
                   river_cloud_xiaozhi_playback_rebuffer_cause()));
    river_cloud_xiaozhi_refresh_playback_phase("pending_stop_drop_queue");
}
