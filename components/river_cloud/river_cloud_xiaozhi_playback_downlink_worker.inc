static uint32_t river_cloud_xiaozhi_downlink_start_threshold_frames(void)
{
    return river_cloud_xiaozhi_current_start_gate().start_frames;
}

static uint32_t river_cloud_xiaozhi_playback_buffer_frame_budget(void)
{
    if (g_river_cloud.xiaozhi_playback_gate_truth.buffer_frames != 0U) {
        return g_river_cloud.xiaozhi_playback_gate_truth.buffer_frames;
    }

    return RIVER_CLOUD_XIAOZHI_PLAYBACK_BUFFER_FRAMES;
}

static uint32_t river_cloud_xiaozhi_downlink_attached_resume_threshold_frames(void)
{
    uint32_t start_frames = river_cloud_xiaozhi_downlink_start_threshold_frames();
    uint32_t buffer_frames = river_cloud_xiaozhi_playback_buffer_frame_budget();

    if (start_frames == 0U) {
        return buffer_frames;
    }
    if (buffer_frames == 0U) {
        return start_frames;
    }
    return buffer_frames < start_frames ? buffer_frames : start_frames;
}

static uint32_t river_cloud_xiaozhi_downlink_segment_gap_hold_frames(void)
{
    uint32_t resume_frames = river_cloud_xiaozhi_downlink_attached_resume_threshold_frames();
    uint32_t low_water_frames = river_cloud_xiaozhi_downlink_starved_low_water_frames();
    uint32_t hold_frames = 1U;

    if (resume_frames > 1U) {
        hold_frames = resume_frames - 1U;
    }
    if (low_water_frames != 0U && hold_frames > low_water_frames) {
        hold_frames = low_water_frames;
    }
    if (hold_frames == 0U) {
        hold_frames = 1U;
    }
    return hold_frames;
}

static uint32_t river_cloud_xiaozhi_downlink_start_threshold_for_backend(
    river_cloud_playback_backend_state_t backend_state)
{
    if (backend_state == RIVER_CLOUD_PLAYBACK_BACKEND_RESTART_PENDING) {
        return river_cloud_xiaozhi_downlink_attached_resume_threshold_frames();
    }

    return river_cloud_xiaozhi_downlink_start_threshold_frames();
}

static uint32_t river_cloud_xiaozhi_downlink_starved_rebuffer_ms(void)
{
    uint32_t frame_ms = river_cloud_xiaozhi_downlink_frame_duration_ms();
    uint32_t start_frames = river_cloud_xiaozhi_current_start_gate().start_frames;
    uint32_t wait_ms = RIVER_CLOUD_XIAOZHI_DOWNLINK_STARVED_REBUFFER_MS;
    uint32_t adaptive_wait_ms;

    if (frame_ms == 0U || start_frames == 0U) {
        return wait_ms;
    }

    adaptive_wait_ms = (start_frames * frame_ms) / 2U;
    if (adaptive_wait_ms > wait_ms) {
        wait_ms = adaptive_wait_ms;
    }
    if (wait_ms > 400U) {
        wait_ms = 400U;
    }
    return wait_ms;
}

static uint32_t river_cloud_xiaozhi_downlink_starved_low_water_frames(void)
{
    uint32_t frame_ms = river_cloud_xiaozhi_downlink_frame_duration_ms();
    uint32_t trigger_wait_ms = river_cloud_xiaozhi_downlink_starved_rebuffer_ms();
    uint32_t start_frames = river_cloud_xiaozhi_current_start_gate().start_frames;
    uint32_t low_water_frames = 1U;

    if (frame_ms != 0U) {
        low_water_frames = (trigger_wait_ms + frame_ms - 1U) / frame_ms;
    }
    if (low_water_frames == 0U) {
        low_water_frames = 1U;
    }
    if (start_frames > 1U && low_water_frames >= start_frames) {
        low_water_frames = start_frames - 1U;
    }
    return low_water_frames;
}

static bool river_cloud_xiaozhi_maybe_pause_for_segment_gap(void)
{
    river_cloud_xiaozhi_segment_gap_hold_view_t hold_view;
    const river_cloud_xiaozhi_playback_context_truth_t *meta_context =
        &g_river_cloud.xiaozhi_playback_lineage_truth.meta_context;
    bool attached_hold = false;

    river_cloud_xiaozhi_capture_segment_gap_hold_view(&hold_view);
    if (!hold_view.ready) {
        return false;
    }

    if (river_cloud_xiaozhi_hold_playback_for_segment_gap("xiaozhi_segment_gap_pause",
                                                          &attached_hold) != RIVER_OK) {
        return false;
    }

    RIVER_LOGI("xiaozhi playback segment gap hold: response_id=%s playback_id=%s segment_id=%s queued=%lu hold=%u phase=%s backend=%s mode=%s",
               meta_context->response_id[0] != '\0' ?
                   meta_context->response_id :
                   "-",
               meta_context->playback_id[0] != '\0' ?
                   meta_context->playback_id :
                   "-",
               meta_context->segment_id[0] != '\0' ?
                   meta_context->segment_id :
                   "-",
               (unsigned long)hold_view.queued_frames,
               (unsigned int)hold_view.hold_frames,
               river_cloud_playback_phase_name(
                   river_cloud_xiaozhi_playback_observed_phase_kind()),
               river_cloud_playback_backend_state_name(
                   hold_view.truth_view.backend_state),
               attached_hold ? "attached_recover" : "detached_stop");
    return true;
}

static void river_cloud_xiaozhi_pop_playback_segment(void)
{
    river_cloud_xiaozhi_playback_segment_t *segment =
        river_cloud_xiaozhi_current_playback_segment();

    if (segment == NULL) {
        return;
    }

    memset(segment, 0, sizeof(*segment));
    g_river_cloud.xiaozhi_playback_segment_queue_truth.head =
        (g_river_cloud.xiaozhi_playback_segment_queue_truth.head + 1U) %
        RIVER_CLOUD_XIAOZHI_PLAYBACK_SEGMENTS_MAX;
    if (g_river_cloud.xiaozhi_playback_segment_queue_truth.count > 0U) {
        g_river_cloud.xiaozhi_playback_segment_queue_truth.count--;
    }
}

static void river_cloud_xiaozhi_note_playback_rebuffer(
    uint64_t paused_at_ms,
    river_cloud_playback_rebuffer_cause_t cause)
{
    river_cloud_xiaozhi_playback_segment_t *segment =
        river_cloud_xiaozhi_current_playback_segment();
    river_cloud_playback_rebuffer_cause_t old_cause =
        river_cloud_xiaozhi_playback_rebuffer_cause();
    bool old_pending = g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_pending;

    if (segment != NULL && segment->valid && segment->started && segment->paused_at_ms == 0U) {
        segment->paused_at_ms = paused_at_ms;
        segment->rebuffered = true;
    }
    g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_cause = cause;
    g_river_cloud.xiaozhi_playback_runtime_truth.recovery_outcome =
        RIVER_CLOUD_PLAYBACK_RECOVERY_OUTCOME_MANAGED_REBUFFER;
    g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_pending = true;
    if (g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_count < UINT32_MAX) {
        g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_count++;
    }
    if (g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_streak < UINT32_MAX) {
        g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_streak++;
    }
    (void)river_cloud_xiaozhi_refresh_start_gate();
    if (!river_cloud_xiaozhi_refresh_playback_phase("note_rebuffer") &&
        (!old_pending || old_cause != cause)) {
        river_cloud_request_state_sync("note_rebuffer");
    }
}

static void river_cloud_xiaozhi_set_playback_recovery_path(
    river_cloud_playback_recovery_path_t path,
    const char *reason)
{
    river_cloud_playback_recovery_path_t old_path =
        g_river_cloud.xiaozhi_playback_runtime_truth.recovery_path;

    g_river_cloud.xiaozhi_playback_runtime_truth.recovery_path = path;
    if (old_path != path) {
        river_cloud_request_state_sync(reason != NULL && reason[0] != '\0' ? reason :
                                                                              "playback_recovery_path");
    }
}

static void river_cloud_xiaozhi_set_playback_recovery_outcome(
    river_cloud_playback_recovery_outcome_t outcome,
    const char *reason)
{
    river_cloud_playback_recovery_outcome_t old_outcome =
        g_river_cloud.xiaozhi_playback_runtime_truth.recovery_outcome;

    g_river_cloud.xiaozhi_playback_runtime_truth.recovery_outcome = outcome;
    if (old_outcome != outcome) {
        river_cloud_request_state_sync(reason != NULL && reason[0] != '\0' ? reason :
                                                                              "playback_recovery_outcome");
    }
}

static void river_cloud_xiaozhi_finish_playback_rebuffer(uint64_t resumed_at_ms)
{
    river_cloud_xiaozhi_playback_segment_t *segment =
        river_cloud_xiaozhi_current_playback_segment();
    river_cloud_playback_rebuffer_cause_t cause =
        river_cloud_xiaozhi_playback_rebuffer_cause();

    if (segment != NULL && segment->valid && segment->started && segment->paused_at_ms != 0U &&
        resumed_at_ms > segment->paused_at_ms) {
        segment->started_at_ms += resumed_at_ms - segment->paused_at_ms;
        RIVER_LOGW("xiaozhi playback rebuffer resumed: cause=%s pause_ms=%lu queued=%lu total=%lu streak=%lu",
                   river_cloud_playback_rebuffer_cause_name(cause),
                   (unsigned long)(resumed_at_ms - segment->paused_at_ms),
                   (unsigned long)river_cloud_xiaozhi_playback_queued_frames(),
                   (unsigned long)g_river_cloud.xiaozhi_playback_runtime_truth
                       .rebuffer_count,
                   (unsigned long)g_river_cloud.xiaozhi_playback_runtime_truth
                       .rebuffer_streak);
        segment->paused_at_ms = 0U;
    }

    g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_cause =
        RIVER_CLOUD_PLAYBACK_REBUFFER_CAUSE_NONE;
    g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_pending = false;
    river_cloud_xiaozhi_clear_downlink_starvation_watch();
    (void)river_cloud_xiaozhi_refresh_start_gate();
    river_cloud_xiaozhi_refresh_playback_phase("finish_rebuffer");
}

static bool river_cloud_xiaozhi_maybe_rebuffer_starved(uint32_t queued_frames, uint64_t now_ms)
{
    river_cloud_xiaozhi_playback_recovery_plan_t recovery_plan;
    river_cloud_xiaozhi_playback_rebuffer_observe_view_t observe;
    river_cloud_xiaozhi_managed_rebuffer_request_t rebuffer_request;
    uint64_t supply_gap_ms = 0U;
    uint64_t wait_ms;
    uint32_t trigger_wait_ms;
    river_cloud_playback_recovery_path_t recovery_path =
        RIVER_CLOUD_PLAYBACK_RECOVERY_PATH_STOP_REBUFFER;

    river_cloud_xiaozhi_capture_playback_truth_view(&recovery_plan.truth_view);
    if (!recovery_plan.truth_view.output_active ||
        g_river_cloud.xiaozhi_playback_runtime_truth.stop_pending ||
        g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_pending ||
        recovery_plan.truth_view.backend_state !=
            RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_ACTIVE) {
        river_cloud_xiaozhi_clear_downlink_starvation_watch();
        return false;
    }
    if (!river_cloud_xiaozhi_playback_supply_expects_more_audio(
            recovery_plan.truth_view.supply_kind)) {
        river_cloud_xiaozhi_clear_downlink_starvation_watch();
        return false;
    }
    if (recovery_plan.truth_view.supply_kind ==
        RIVER_CLOUD_PLAYBACK_SUPPLY_WAITING_NEXT_SEGMENT) {
        river_cloud_xiaozhi_clear_downlink_starvation_watch();
        return false;
    }

    recovery_plan.low_water_frames =
        river_cloud_xiaozhi_downlink_starved_low_water_frames();
    if (queued_frames > recovery_plan.low_water_frames) {
        river_cloud_xiaozhi_clear_downlink_starvation_watch();
        return false;
    }

    if (g_river_cloud.xiaozhi_downlink_runtime_truth.last_supply_ms != 0U &&
        now_ms > g_river_cloud.xiaozhi_downlink_runtime_truth.last_supply_ms) {
        supply_gap_ms =
            now_ms - g_river_cloud.xiaozhi_downlink_runtime_truth.last_supply_ms;
    }
    if (g_river_cloud.xiaozhi_downlink_runtime_truth.starved_since_ms == 0U ||
        (g_river_cloud.xiaozhi_downlink_runtime_truth.last_supply_ms != 0U &&
         g_river_cloud.xiaozhi_downlink_runtime_truth.starved_since_ms <
             g_river_cloud.xiaozhi_downlink_runtime_truth.last_supply_ms)) {
        g_river_cloud.xiaozhi_downlink_runtime_truth.starved_since_ms =
            g_river_cloud.xiaozhi_downlink_runtime_truth.last_supply_ms != 0U ?
                g_river_cloud.xiaozhi_downlink_runtime_truth.last_supply_ms :
                now_ms;
    }

    wait_ms = now_ms - g_river_cloud.xiaozhi_downlink_runtime_truth.starved_since_ms;
    trigger_wait_ms = river_cloud_xiaozhi_downlink_starved_rebuffer_ms();
    if (wait_ms < trigger_wait_ms) {
        return false;
    }

    river_cloud_xiaozhi_capture_playback_recovery_plan(
        &recovery_plan,
        RIVER_CLOUD_PLAYBACK_REBUFFER_CAUSE_UPSTREAM_STARVED,
        queued_frames,
        now_ms);
    river_cloud_xiaozhi_capture_managed_playback_rebuffer_request(
        "xiaozhi_playback_starved",
        false,
        false,
        &rebuffer_request);
    recovery_path = recovery_plan.recovery_path;
    river_cloud_xiaozhi_start_managed_playback_rebuffer_request(
        &rebuffer_request,
        &recovery_plan,
        now_ms,
        &recovery_path);
    river_cloud_xiaozhi_capture_playback_rebuffer_observe_view(&recovery_plan,
                                                               recovery_path,
                                                               &observe);

    RIVER_LOGW("xiaozhi playback upstream gap rebuffer: cause=%s wait_ms=%lu supply_gap_ms=%lu trigger_ms=%u queued=%lu low=%u start=%u policy=%s cautious=%s target_ms=%u total=%lu streak=%lu recovery=%s",
               river_cloud_playback_rebuffer_cause_name(observe.cause),
               (unsigned long)wait_ms,
               (unsigned long)supply_gap_ms,
               (unsigned int)trigger_wait_ms,
               (unsigned long)observe.queued_frames,
               (unsigned int)observe.low_water_frames,
               (unsigned int)observe.start_gate.start_frames,
               river_cloud_playback_start_policy_name(observe.start_gate.policy),
               observe.start_gate.cautious_history ? "yes" : "no",
               (unsigned int)observe.prefetch_target_ms,
               (unsigned long)observe.total_rebuffers,
               (unsigned long)observe.rebuffer_streak,
               river_cloud_xiaozhi_playback_recovery_path_label(
                   observe.recovery_path,
                   "stop_rebuffer"));
    (void)river_cloud_xiaozhi_execute_managed_playback_rebuffer_request(
        &rebuffer_request,
        &recovery_plan);
    river_cloud_xiaozhi_clear_downlink_starvation_watch();
    return true;
}

static bool river_cloud_xiaozhi_write_failed_prefers_starved_rebuffer(uint32_t queued_frames,
                                                                      uint64_t now_ms,
                                                                      uint64_t *supply_gap_ms_out)
{
    uint32_t frame_ms = river_cloud_xiaozhi_downlink_frame_duration_ms();
    uint32_t low_water_frames = river_cloud_xiaozhi_downlink_starved_low_water_frames();
    uint64_t supply_gap_ms = 0U;
    uint64_t queued_budget_ms;

    if (supply_gap_ms_out != NULL) {
        *supply_gap_ms_out = 0U;
    }
    if (queued_frames == 0U || queued_frames > low_water_frames || frame_ms == 0U ||
        g_river_cloud.xiaozhi_downlink_runtime_truth.last_supply_ms == 0U ||
        now_ms <= g_river_cloud.xiaozhi_downlink_runtime_truth.last_supply_ms) {
        return false;
    }

    supply_gap_ms =
        now_ms - g_river_cloud.xiaozhi_downlink_runtime_truth.last_supply_ms;
    queued_budget_ms = (uint64_t)queued_frames * (uint64_t)frame_ms;
    if (supply_gap_ms_out != NULL) {
        *supply_gap_ms_out = supply_gap_ms;
    }
    return supply_gap_ms >= queued_budget_ms;
}

static bool river_cloud_xiaozhi_rebuffer_prefers_service_recover(
    river_cloud_playback_rebuffer_cause_t cause,
    river_cloud_playback_supply_kind_t supply_kind)
{
    if (cause == RIVER_CLOUD_PLAYBACK_REBUFFER_CAUSE_WRITE_FAILED) {
        return true;
    }

    return cause == RIVER_CLOUD_PLAYBACK_REBUFFER_CAUSE_UPSTREAM_STARVED &&
           river_cloud_xiaozhi_playback_supply_expects_more_audio(supply_kind);
}

static void river_cloud_xiaozhi_capture_rebuffer_recovery_attempt(
    const char *reason,
    river_cloud_playback_rebuffer_cause_t cause,
    river_cloud_playback_supply_kind_t supply_kind,
    river_cloud_xiaozhi_rebuffer_recovery_attempt_t *attempt)
{
    bool prefer_service_recover;

    if (attempt == NULL) {
        return;
    }

    memset(attempt, 0, sizeof(*attempt));
    prefer_service_recover =
        river_cloud_xiaozhi_rebuffer_prefers_service_recover(cause, supply_kind);
    attempt->reason = reason;
    attempt->cause = cause;
    attempt->supply_kind = supply_kind;
    attempt->requested_path =
        prefer_service_recover ? RIVER_CLOUD_PLAYBACK_RECOVERY_PATH_SERVICE_RECOVER :
                                 RIVER_CLOUD_PLAYBACK_RECOVERY_PATH_STOP_REBUFFER;
    attempt->fallback_path =
        prefer_service_recover ? RIVER_CLOUD_PLAYBACK_RECOVERY_PATH_STOP_REBUFFER :
                                 RIVER_CLOUD_PLAYBACK_RECOVERY_PATH_SERVICE_RECOVER;
}

static river_status_t river_cloud_xiaozhi_execute_rebuffer_recovery_path(
    river_cloud_playback_recovery_path_t path,
    const char *reason)
{
    switch (path) {
    case RIVER_CLOUD_PLAYBACK_RECOVERY_PATH_SERVICE_RECOVER:
        return river_playback_service_recover_stream_ex(reason);

    case RIVER_CLOUD_PLAYBACK_RECOVERY_PATH_STOP_REBUFFER:
    default:
        return river_cloud_xiaozhi_stop_playback_for_rebuffer(reason);
    }
}

static void river_cloud_xiaozhi_execute_rebuffer_recovery_attempt(
    const river_cloud_xiaozhi_rebuffer_recovery_attempt_t *attempt,
    river_cloud_xiaozhi_rebuffer_recovery_result_t *result)
{
    if (result == NULL) {
        return;
    }

    memset(result, 0, sizeof(*result));
    result->status = RIVER_ERR_ARG;
    result->applied_path = RIVER_CLOUD_PLAYBACK_RECOVERY_PATH_STOP_REBUFFER;
    if (attempt == NULL) {
        return;
    }

    result->applied_path = attempt->requested_path;
    result->status = river_cloud_xiaozhi_execute_rebuffer_recovery_path(
        attempt->requested_path,
        attempt->reason);
    if (result->status == RIVER_OK) {
        return;
    }

    RIVER_LOGW("xiaozhi playback recovery fallback: from=%s to=%s cause=%s supply=%s status=%d",
               river_cloud_xiaozhi_playback_recovery_path_label(
                   attempt->requested_path,
                   "unknown"),
               river_cloud_xiaozhi_playback_recovery_path_label(
                   attempt->fallback_path,
                   "unknown"),
               river_cloud_playback_rebuffer_cause_name(attempt->cause),
               river_cloud_xiaozhi_playback_supply_kind_name(
                   attempt->supply_kind),
               (int)result->status);
    result->applied_path = attempt->fallback_path;
    result->status = river_cloud_xiaozhi_execute_rebuffer_recovery_path(
        attempt->fallback_path,
        attempt->reason);
}

static river_cloud_xiaozhi_rebuffer_recovery_result_t
river_cloud_xiaozhi_request_playback_rebuffer_recovery(
    const char *reason,
    river_cloud_playback_rebuffer_cause_t cause,
    river_cloud_playback_supply_kind_t supply_kind)
{
    river_cloud_xiaozhi_rebuffer_recovery_attempt_t attempt;
    river_cloud_xiaozhi_rebuffer_recovery_result_t result;

    memset(&result, 0, sizeof(result));
    result.status = RIVER_ERR_ARG;
    result.applied_path = RIVER_CLOUD_PLAYBACK_RECOVERY_PATH_STOP_REBUFFER;
    river_cloud_xiaozhi_capture_rebuffer_recovery_attempt(reason,
                                                          cause,
                                                          supply_kind,
                                                          &attempt);
    river_cloud_xiaozhi_execute_rebuffer_recovery_attempt(&attempt, &result);
    river_cloud_xiaozhi_set_playback_recovery_path(result.applied_path,
                                                   "playback_recovery_path");
    return result;
}

static bool river_cloud_xiaozhi_rebuffer_resume_ready(uint32_t queued_frames,
                                                      uint64_t now_ms,
                                                      river_cloud_playback_backend_state_t
                                                          backend_state)
{
    uint32_t resume_frames;

    if (!g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_pending ||
        g_river_cloud.xiaozhi_playback_runtime_truth.stop_pending) {
        return true;
    }

    resume_frames = river_cloud_xiaozhi_downlink_attached_resume_threshold_frames();
    if (queued_frames < resume_frames) {
        return false;
    }

    if (backend_state == RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_ACTIVE ||
        backend_state == RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_RECOVERING) {
        river_cloud_xiaozhi_finish_playback_rebuffer(now_ms);
    }
    return true;
}

static bool river_cloud_xiaozhi_maybe_resume_paused_playback(
    uint32_t queued_frames,
    const river_cloud_xiaozhi_playback_truth_view_t *truth_view)
{
    uint32_t start_frames;
    uint32_t resume_frames;

    if (truth_view == NULL ||
        g_river_cloud.xiaozhi_playback_runtime_truth.stop_pending ||
        !river_cloud_xiaozhi_playback_terminal_open() ||
        truth_view->backend_state != RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_PAUSED) {
        return false;
    }

    start_frames = river_cloud_xiaozhi_downlink_start_threshold_frames();
    resume_frames = river_cloud_xiaozhi_downlink_attached_resume_threshold_frames();
    if (queued_frames < resume_frames) {
        return false;
    }

    river_cloud_xiaozhi_mark_playback_started();
    river_cloud_xiaozhi_clear_downlink_starvation_watch();
    RIVER_LOGI("xiaozhi playback paused backend resumed: queued=%lu resume=%u start=%u buffer=%u phase=%s backend=%s",
               (unsigned long)queued_frames,
               (unsigned int)resume_frames,
               (unsigned int)start_frames,
               (unsigned int)river_cloud_xiaozhi_playback_buffer_frame_budget(),
               river_cloud_playback_phase_name(
                   river_cloud_xiaozhi_playback_observed_phase_kind()),
               river_cloud_playback_backend_state_name(truth_view->backend_state));
    return true;
}

static void river_cloud_xiaozhi_try_queue_playback_started_ack(void);
static void river_cloud_xiaozhi_try_queue_playback_mark_ack(uint32_t played_duration_ms);

static void river_cloud_xiaozhi_try_start_current_playback_segment(uint64_t start_ms)
{
    river_cloud_xiaozhi_playback_segment_t *segment =
        river_cloud_xiaozhi_current_playback_segment();

    if (segment == NULL || segment->started || !segment->valid ||
        !river_cloud_xiaozhi_playback_terminal_open()) {
        return;
    }

    segment->started = true;
    segment->started_at_ms = start_ms;
    segment->paused_at_ms = 0U;
    segment->started_ack_reported = false;
    segment->last_mark_ms = 0U;
    river_cloud_xiaozhi_try_queue_playback_started_ack();
}

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

static river_status_t river_cloud_xiaozhi_try_start_playback(uint32_t sample_rate,
                                                             uint32_t frame_duration_ms,
                                                             size_t mono_bytes,
                                                             uint32_t buffer_frame_count,
                                                             bool reference_export,
                                                             uint32_t reference_history_ms)
{
    river_playback_stream_config_t config;

    memset(&config, 0, sizeof(config));
    config.stream_name = RIVER_CLOUD_XIAOZHI_TTS_STREAM_NAME;
    config.priority = RIVER_PLAYBACK_PRIO_TTS;
    config.sample_rate = sample_rate;
    config.frame_ms = frame_duration_ms;
    config.playback_channels = 2U;
    config.bits_per_sample = 16U;
    config.playback_frame_bytes = mono_bytes * 2U;
    config.buffer_frame_count = buffer_frame_count;
    config.volume_left = 1.0f;
    config.volume_right = 1.0f;
    config.reference_export = reference_export;
    config.reference_channels = reference_export ? 1U : 0U;
    config.reference_frame_bytes = reference_export ? mono_bytes : 0U;
    config.reference_history_ms = reference_export ? reference_history_ms : 0U;
    return river_playback_service_start_stream(&config);
}

static river_status_t river_cloud_xiaozhi_start_playback_if_needed(uint32_t sample_rate,
                                                                   uint32_t frame_duration_ms,
                                                                   size_t mono_bytes)
{
    river_cloud_xiaozhi_playback_truth_view_t truth_view;
    river_status_t status;
    river_cloud_xiaozhi_playback_start_gate_t start_gate =
        river_cloud_xiaozhi_current_start_gate();
    uint32_t buffer_frames = RIVER_CLOUD_XIAOZHI_PLAYBACK_BUFFER_FRAMES;
    uint64_t now_ms;
    const bool reference_export = river_cloud_xiaozhi_playback_allows_vad_open();
    const char *mode = reference_export ? "ref" : "no_ref";

    river_cloud_xiaozhi_capture_playback_truth_view(&truth_view);

    if (truth_view.backend_state == RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_ACTIVE) {
        return RIVER_OK;
    }
    if (truth_view.backend_state == RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_PAUSED) {
        return RIVER_ERR_BUSY;
    }
    if (truth_view.backend_state == RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_RECOVERING) {
        return RIVER_ERR_BUSY;
    }
    if (sample_rate == 0U || frame_duration_ms == 0U || mono_bytes == 0U) {
        return RIVER_ERR_ARG;
    }
    if (truth_view.backend_state == RIVER_CLOUD_PLAYBACK_BACKEND_FOREIGN_ACTIVE) {
        if (river_playback_service_interrupt_stream_ex("xiaozhi_tts_takeover") != RIVER_OK &&
            river_playback_service_stop_stream_ex("xiaozhi_tts_takeover") != RIVER_OK) {
            return RIVER_ERR_BUSY;
        }
    }

    status = river_cloud_xiaozhi_try_start_playback(sample_rate,
                                                    frame_duration_ms,
                                                    mono_bytes,
                                                    buffer_frames,
                                                    reference_export,
                                                    reference_export ?
                                                        RIVER_CLOUD_XIAOZHI_PLAYBACK_REF_HISTORY_MS :
                                                        0U);
    if (status != RIVER_OK) {
        mode = "compact";
        buffer_frames = RIVER_CLOUD_XIAOZHI_PLAYBACK_BUFFER_FRAMES_FALLBACK;
        RIVER_LOGW("xiaozhi playback start retry: status=%d -> compact mode no_ref buffer_frames=%u",
                   (int)status,
                   (unsigned int)RIVER_CLOUD_XIAOZHI_PLAYBACK_BUFFER_FRAMES_FALLBACK);
        status = river_cloud_xiaozhi_try_start_playback(
            sample_rate,
            frame_duration_ms,
            mono_bytes,
            buffer_frames,
            false,
            0U);
        if (status != RIVER_OK) {
            return RIVER_ERR_BUSY;
        }
    }

    g_river_cloud.xiaozhi_playback_gate_truth.buffer_frames = buffer_frames;
    RIVER_LOGI("xiaozhi playback start: %luHz frame=%lums mono=%luB queued=%lu start=%u policy=%s cautious=%s target_ms=%u prefetch_frames=%u mode=%s buffer=%u gain=%d/%d from_backend=%s rebuffer=%s/%s",
               (unsigned long)sample_rate,
               (unsigned long)frame_duration_ms,
               (unsigned long)mono_bytes,
               (unsigned long)river_cloud_xiaozhi_playback_queued_frames(),
               (unsigned int)start_gate.start_frames,
               river_cloud_playback_start_policy_name(start_gate.policy),
               start_gate.cautious_history ? "yes" : "no",
               (unsigned int)g_river_cloud.xiaozhi_playback_meta_truth.prefetch_target_ms,
               (unsigned int)start_gate.prefetch_frames,
               mode,
               (unsigned int)buffer_frames,
               RIVER_CLOUD_XIAOZHI_PLAYBACK_GAIN_NUM,
               RIVER_CLOUD_XIAOZHI_PLAYBACK_GAIN_DEN,
               river_cloud_playback_backend_state_name(truth_view.backend_state),
               g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_pending ? "yes" :
                                                                               "no",
               river_cloud_playback_rebuffer_cause_name(
                   river_cloud_xiaozhi_playback_rebuffer_cause()));
    river_cloud_xiaozhi_mark_playback_started();
    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    river_cloud_xiaozhi_finish_playback_rebuffer(now_ms);
    river_cloud_xiaozhi_clear_downlink_starvation_watch();
    return RIVER_OK;
}

static river_cloud_xiaozhi_downlink_playback_prepare_result_t
river_cloud_xiaozhi_prepare_downlink_playback(uint32_t queued_frames, uint64_t now_ms)
{
    river_cloud_xiaozhi_downlink_playback_prepare_result_t result = {
        .wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_PLAYBACK_NOT_READY,
        .status = RIVER_OK,
        .ready = false,
    };
    river_cloud_xiaozhi_playback_truth_view_t truth_view;
    uint32_t start_frames;
    river_status_t status;

    river_cloud_xiaozhi_capture_playback_truth_view(&truth_view);
    if (!river_cloud_xiaozhi_rebuffer_resume_ready(queued_frames,
                                                   now_ms,
                                                   truth_view.backend_state)) {
        result.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_REBUFFER_WAIT;
        return result;
    }
    if (g_river_cloud.xiaozhi_playback_runtime_truth.stop_pending &&
        truth_view.backend_state != RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_ACTIVE) {
        river_cloud_xiaozhi_playback_check_pending_stop();
        result.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_STOP_PENDING;
        return result;
    }
    if (truth_view.backend_state == RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_PAUSED) {
        if (!river_cloud_xiaozhi_maybe_resume_paused_playback(queued_frames,
                                                              &truth_view)) {
            result.wait_kind =
                RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_PAUSED_RESUME_WAIT;
            return result;
        }
        river_cloud_xiaozhi_capture_playback_truth_view(&truth_view);
    }
    if (truth_view.backend_state == RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_ACTIVE) {
        result.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_NONE;
        result.ready = true;
        return result;
    }
    if (truth_view.backend_state == RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_RECOVERING) {
        result.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_BACKEND_RECOVERING;
        return result;
    }

    start_frames = river_cloud_xiaozhi_downlink_start_threshold_for_backend(
        truth_view.backend_state);
    if (!g_river_cloud.xiaozhi_playback_runtime_truth.stop_pending &&
        queued_frames < start_frames) {
        result.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_START_THRESHOLD;
        return result;
    }

    status = river_cloud_xiaozhi_start_playback_if_needed(
        river_cloud_xiaozhi_downlink_sample_rate(),
        river_cloud_xiaozhi_downlink_frame_duration_ms(),
        g_river_cloud.xiaozhi_downlink_ring.frame_bytes);
    if (status != RIVER_OK) {
        result.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_PLAYBACK_START_FAILED;
        result.status = status;
        return result;
    }

    result.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_NONE;
    result.ready = true;
    return result;
}

static bool river_cloud_xiaozhi_downlink_active(void);

static river_cloud_xiaozhi_downlink_cycle_plan_t
river_cloud_xiaozhi_prepare_downlink_cycle_plan(void)
{
    river_cloud_xiaozhi_downlink_cycle_plan_t plan = {
        .step_result = RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_SLEEP_POLL,
        .wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_NOT_READY,
        .status = RIVER_OK,
        .queued_frames = 0U,
        .ready = false,
    };
    river_cloud_xiaozhi_downlink_playback_prepare_result_t prepare_result;
    uint32_t queued_frames;
    uint64_t now_ms;

    if (!river_cloud_xiaozhi_downlink_active()) {
        plan.step_result = RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_SLEEP_IDLE;
        plan.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_INACTIVE;
        return plan;
    }

    queued_frames = river_cloud_xiaozhi_playback_queued_frames();
    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    river_cloud_xiaozhi_update_playback_ack_progress();
    if (river_cloud_xiaozhi_maybe_rebuffer_starved(queued_frames, now_ms)) {
        plan.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_STARVED;
        return plan;
    }
    if (river_cloud_xiaozhi_maybe_pause_for_segment_gap()) {
        plan.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_SEGMENT_GAP;
        return plan;
    }
    if (queued_frames == 0U) {
        river_cloud_xiaozhi_playback_check_pending_stop();
        plan.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_EMPTY;
        return plan;
    }
    prepare_result = river_cloud_xiaozhi_prepare_downlink_playback(queued_frames,
                                                                   now_ms);
    if (!prepare_result.ready) {
        plan.wait_kind = prepare_result.wait_kind;
        plan.status = prepare_result.status;
        return plan;
    }

    plan.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_NONE;
    plan.queued_frames = queued_frames;
    plan.ready = true;
    return plan;
}

static river_cloud_xiaozhi_downlink_frame_acquire_result_t
river_cloud_xiaozhi_acquire_current_downlink_frame(void)
{
    river_cloud_xiaozhi_downlink_frame_acquire_result_t result = {
        .step_result = RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_SLEEP_POLL,
        .status = RIVER_OK,
        .acquired = false,
    };
    river_status_t status;

    if (river_cloud_xiaozhi_downlink_retry_frame_pending()) {
        result.acquired = true;
        result.step_result = RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_CONTINUE;
        return result;
    }

    status = river_audio_frame_ring_read(&g_river_cloud.xiaozhi_downlink_ring,
                                         g_river_cloud.xiaozhi_downlink_task_frame);
    if (status != RIVER_OK) {
        river_cloud_xiaozhi_playback_check_pending_stop();
        result.status = status;
        return result;
    }

    result.acquired = true;
    result.step_result = RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_CONTINUE;
    return result;
}

static river_cloud_xiaozhi_downlink_cycle_outcome_t
river_cloud_xiaozhi_downlink_write_cycle_outcome(
    const river_cloud_xiaozhi_downlink_frame_write_result_t *write_result)
{
    if (write_result == NULL) {
        return RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_STEP_POLICY;
    }
    if (write_result->aborted) {
        return RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_ABORTED;
    }
    if (write_result->write_failed) {
        return write_result->frame_consumed ?
                   RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_WRITE_RECOVERED :
                   RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_WRITE_FAILED;
    }
    if (write_result->frame_consumed) {
        return RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_WRITE_OK;
    }
    return RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_STEP_POLICY;
}

static river_cloud_xiaozhi_downlink_task_cycle_result_t
river_cloud_xiaozhi_process_downlink_task_cycle(void)
{
    river_cloud_xiaozhi_downlink_task_cycle_result_t result = {
        .step_result = RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_CONTINUE,
        .status = RIVER_OK,
        .outcome = RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_NONE,
    };

    result.cycle_plan = river_cloud_xiaozhi_prepare_downlink_cycle_plan();
    if (!result.cycle_plan.ready) {
        result.status = result.cycle_plan.status;
        result.outcome =
            result.cycle_plan.wait_kind ==
                    RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_INACTIVE ?
                RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_INACTIVE :
                RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_NOT_READY;
        result.step_result = result.cycle_plan.step_result;
        return result;
    }

    result.acquire_result = river_cloud_xiaozhi_acquire_current_downlink_frame();
    if (!result.acquire_result.acquired) {
        result.status = result.acquire_result.status;
        result.outcome = RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_ACQUIRE_MISS;
        result.step_result = result.acquire_result.step_result;
        return result;
    }

    result.write_result = river_cloud_xiaozhi_write_current_downlink_frame_step(
        result.cycle_plan.queued_frames);
    result.status = result.write_result.status;
    result.outcome =
        river_cloud_xiaozhi_downlink_write_cycle_outcome(&result.write_result);
    result.step_result = result.write_result.step_result;
    return result;
}

static river_cloud_xiaozhi_downlink_task_wait_plan_t
river_cloud_xiaozhi_build_downlink_task_wait_plan(
    const river_cloud_xiaozhi_downlink_task_cycle_result_t *cycle_result)
{
    river_cloud_xiaozhi_downlink_task_wait_plan_t wait_plan = {
        .sleep = false,
        .delay_ms = 0U,
        .status = RIVER_OK,
        .wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_NONE,
    };

    if (cycle_result == NULL) {
        return wait_plan;
    }

    wait_plan.status = cycle_result->status;
    if (cycle_result->step_result ==
        RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_SLEEP_IDLE) {
        wait_plan.sleep = true;
        wait_plan.delay_ms = RIVER_CLOUD_XIAOZHI_DOWNLINK_IDLE_MS;
        wait_plan.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_INACTIVE;
    } else if (cycle_result->step_result ==
               RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_SLEEP_POLL) {
        wait_plan.sleep = true;
        wait_plan.delay_ms = RIVER_CLOUD_XIAOZHI_DOWNLINK_POLL_MS;
        if (!cycle_result->cycle_plan.ready) {
            wait_plan.wait_kind =
                cycle_result->cycle_plan.wait_kind !=
                        RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_NONE ?
                    cycle_result->cycle_plan.wait_kind :
                    RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_NOT_READY;
        } else if (!cycle_result->acquire_result.acquired) {
            wait_plan.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_ACQUIRE_MISS;
        } else if (cycle_result->write_result.write_failed) {
            wait_plan.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_WRITE_FAILED;
        } else {
            wait_plan.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_STEP_POLICY;
        }
    }

    return wait_plan;
}

static void river_cloud_xiaozhi_finish_downlink_task_cycle(
    const river_cloud_xiaozhi_downlink_task_cycle_result_t *cycle_result)
{
    river_cloud_xiaozhi_downlink_task_wait_plan_t wait_plan =
        river_cloud_xiaozhi_build_downlink_task_wait_plan(cycle_result);

    g_river_cloud.xiaozhi_downlink_runtime_truth.last_wait_kind =
        wait_plan.wait_kind;
    g_river_cloud.xiaozhi_downlink_runtime_truth.last_wait_delay_ms =
        wait_plan.delay_ms;
    g_river_cloud.xiaozhi_downlink_runtime_truth.last_cycle_status =
        wait_plan.status;
    g_river_cloud.xiaozhi_downlink_runtime_truth.last_cycle_outcome =
        cycle_result != NULL ? cycle_result->outcome :
                               RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_NONE;
    if (wait_plan.sleep) {
        rtos_time_delay_ms(wait_plan.delay_ms);
    }
}

static void river_cloud_xiaozhi_prepare_decoder_if_needed(uint32_t sample_rate,
                                                          uint32_t frame_duration_ms)
{
    if (sample_rate == 0U) {
        sample_rate = 16000U;
    }
    if (frame_duration_ms == 0U) {
        frame_duration_ms = RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS;
    }

    if (g_river_cloud.xiaozhi_decoder.handle != NULL &&
        g_river_cloud.xiaozhi_decoder.sample_rate == sample_rate &&
        g_river_cloud.xiaozhi_decoder.frame_duration_ms == frame_duration_ms &&
        g_river_cloud.xiaozhi_decoder.channels == 1U) {
        return;
    }

    river_opus_decoder_close(&g_river_cloud.xiaozhi_decoder);
    if (river_opus_decoder_open(&g_river_cloud.xiaozhi_decoder,
                                sample_rate,
                                1U,
                                frame_duration_ms) == RIVER_OK) {
        river_cloud_xiaozhi_note_server_audio_format(sample_rate,
                                                     frame_duration_ms);
    }
}

river_status_t river_cloud_xiaozhi_playback_handle_audio_event(
    const river_xiaozhi_event_t *event)
{
    const uint8_t *mono_frame;
    size_t mono_bytes;
    uint32_t sample_rate;
    uint32_t frame_duration_ms;
    river_status_t status;

    if (event == NULL || event->binary_data == NULL || event->binary_bytes == 0U) {
        return RIVER_ERR_ARG;
    }

    sample_rate = event->sample_rate != 0U ? event->sample_rate :
                                           river_cloud_xiaozhi_server_audio_sample_rate();
    frame_duration_ms =
        event->frame_duration_ms != 0U ? event->frame_duration_ms :
                                         river_cloud_xiaozhi_server_audio_frame_duration_ms();
    if (sample_rate == 0U) {
        sample_rate = 16000U;
    }
    if (frame_duration_ms == 0U) {
        frame_duration_ms = RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS;
    }

    if (event->binary_type == RIVER_XIAOZHI_BINARY_PCM16) {
        if ((event->binary_bytes % sizeof(int16_t)) != 0U) {
            return RIVER_ERR_ARG;
        }
        mono_frame = event->binary_data;
        mono_bytes = event->binary_bytes;
    } else {
        river_cloud_xiaozhi_prepare_decoder_if_needed(sample_rate, frame_duration_ms);
        if (g_river_cloud.xiaozhi_decoder.handle == NULL) {
            return RIVER_ERR_UNSUPPORTED;
        }

        if (river_opus_decode(&g_river_cloud.xiaozhi_decoder,
                              event->binary_data,
                              event->binary_bytes,
                              g_river_cloud.xiaozhi_downlink_mono,
                              sizeof(g_river_cloud.xiaozhi_downlink_mono),
                              &mono_bytes) != RIVER_OK) {
            return RIVER_ERR_IO;
        }
        mono_frame = (const uint8_t *)g_river_cloud.xiaozhi_downlink_mono;
        sample_rate = g_river_cloud.xiaozhi_decoder.sample_rate;
        frame_duration_ms = g_river_cloud.xiaozhi_decoder.frame_duration_ms;
    }

    status = river_cloud_xiaozhi_ensure_downlink_ring(mono_bytes);
    if (status != RIVER_OK) {
        return status;
    }

    river_cloud_xiaozhi_note_downlink_stream_format(sample_rate,
                                                    frame_duration_ms);
    (void)river_cloud_xiaozhi_refresh_start_gate();

    status = river_cloud_xiaozhi_write_downlink_frame_latest(mono_frame);

    river_cloud_xiaozhi_cancel_playback_stop();
    return status;
}

static void river_cloud_xiaozhi_downlink_expand_stereo(const uint8_t *mono_frame,
                                                       size_t mono_bytes)
{
    size_t sample_count;
    size_t index;
    const int16_t *mono;
    int16_t sample;

    mono = (const int16_t *)mono_frame;
    sample_count = mono_bytes / sizeof(int16_t);
    for (index = 0U; index < sample_count; ++index) {
        sample = river_cloud_xiaozhi_playback_sat16(
            ((int32_t)mono[index] * (int32_t)RIVER_CLOUD_XIAOZHI_PLAYBACK_GAIN_NUM) /
            (int32_t)RIVER_CLOUD_XIAOZHI_PLAYBACK_GAIN_DEN);
        g_river_cloud.xiaozhi_downlink_stereo[index * 2U] = sample;
        g_river_cloud.xiaozhi_downlink_stereo[(index * 2U) + 1U] = sample;
    }
}

static bool river_cloud_xiaozhi_downlink_active(void)
{
    if (!g_river_cloud.initialized || !g_river_cloud.xiaozhi_enabled) {
        return false;
    }

    return river_cloud_xiaozhi_playback_has_work();
}

static void river_cloud_xiaozhi_downlink_task(void *arg)
{
    (void)arg;

    for (;;) {
        river_cloud_xiaozhi_downlink_task_cycle_result_t cycle_result =
            river_cloud_xiaozhi_process_downlink_task_cycle();
        river_cloud_xiaozhi_finish_downlink_task_cycle(&cycle_result);
    }
}

void river_cloud_xiaozhi_playback_start_downlink_if_needed(void)
{
    if (!g_river_cloud.xiaozhi_enabled ||
        river_cloud_xiaozhi_downlink_worker_started()) {
        return;
    }

    if (rtos_task_create(&g_river_cloud.xiaozhi_downlink_task,
                         "river_xz_down",
                         river_cloud_xiaozhi_downlink_task,
                         NULL,
                         RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STACK,
                         RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_PRIO) != RTK_SUCCESS) {
        RIVER_LOGW("xiaozhi downlink task create failed");
        return;
    }

    g_river_cloud.xiaozhi_downlink_stream_truth.worker_started = true;
    RIVER_LOGI("xiaozhi downlink worker started");
}
