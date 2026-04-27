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

static bool river_cloud_xiaozhi_segment_gap_hold_already_paused(
    const river_cloud_xiaozhi_segment_gap_hold_view_t *view)
{
    return view != NULL &&
           !g_river_cloud.xiaozhi_playback_runtime_truth.stop_pending &&
           !g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_pending &&
           river_cloud_xiaozhi_playback_terminal_open() &&
           view->truth_view.backend_state ==
               RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_PAUSED &&
           view->truth_view.supply_kind ==
               RIVER_CLOUD_PLAYBACK_SUPPLY_WAITING_NEXT_SEGMENT;
}

static bool river_cloud_xiaozhi_maybe_pause_for_segment_gap(void)
{
    river_cloud_xiaozhi_segment_gap_hold_view_t hold_view;
    const river_cloud_xiaozhi_playback_context_truth_t *meta_context =
        &g_river_cloud.xiaozhi_playback_lineage_truth.meta_context;
    bool attached_hold = false;

    river_cloud_xiaozhi_capture_segment_gap_hold_view(&hold_view);
    if (river_cloud_xiaozhi_segment_gap_hold_already_paused(&hold_view)) {
        return true;
    }
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
    if (truth_view->supply_kind ==
        RIVER_CLOUD_PLAYBACK_SUPPLY_WAITING_NEXT_SEGMENT) {
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
