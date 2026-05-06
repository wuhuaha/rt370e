static river_cloud_xiaozhi_playback_segment_t *river_cloud_xiaozhi_current_playback_segment(void)
{
    if (g_river_cloud.xiaozhi_playback_segment_queue_truth.count == 0U) {
        return NULL;
    }

    return &g_river_cloud.xiaozhi_playback_segment_queue_truth
                .segments[g_river_cloud.xiaozhi_playback_segment_queue_truth.head %
                          RIVER_CLOUD_XIAOZHI_PLAYBACK_SEGMENTS_MAX];
}

static const char *river_cloud_xiaozhi_playback_supply_kind_name(
    river_cloud_playback_supply_kind_t supply_kind)
{
    return river_cloud_playback_supply_kind_name(supply_kind);
}

static const char *river_cloud_xiaozhi_downlink_wait_kind_name(
    river_cloud_xiaozhi_downlink_wait_kind_t wait_kind)
{
    switch (wait_kind) {
    case RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_NONE:
        return "none";
    case RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_INACTIVE:
        return "inactive";
    case RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_NOT_READY:
        return "not_ready";
    case RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_ACQUIRE_MISS:
        return "acquire_miss";
    case RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_WRITE_FAILED:
        return "write_failed";
    case RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_STARVED:
        return "starved";
    case RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_SEGMENT_GAP:
        return "segment_gap";
    case RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_EMPTY:
        return "empty";
    case RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_PLAYBACK_NOT_READY:
        return "playback_not_ready";
    case RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_STEP_POLICY:
        return "step_policy";
    case RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_REBUFFER_WAIT:
        return "rebuffer_wait";
    case RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_STOP_PENDING:
        return "stop_pending";
    case RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_PAUSED_RESUME_WAIT:
        return "paused_resume_wait";
    case RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_BACKEND_RECOVERING:
        return "backend_recovering";
    case RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_START_THRESHOLD:
        return "start_threshold";
    case RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_PLAYBACK_START_FAILED:
        return "playback_start_failed";
    default:
        return "unknown";
    }
}

static const char *river_cloud_xiaozhi_downlink_cycle_outcome_name(
    river_cloud_xiaozhi_downlink_cycle_outcome_t outcome)
{
    switch (outcome) {
    case RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_NONE:
        return "none";
    case RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_INACTIVE:
        return "inactive";
    case RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_NOT_READY:
        return "not_ready";
    case RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_ACQUIRE_MISS:
        return "acquire_miss";
    case RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_WRITE_OK:
        return "write_ok";
    case RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_WRITE_RECOVERED:
        return "write_recovered";
    case RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_WRITE_FAILED:
        return "write_failed";
    case RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_ABORTED:
        return "aborted";
    case RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_STEP_POLICY:
        return "step_policy";
    default:
        return "unknown";
    }
}

static river_cloud_playback_supply_kind_t
river_cloud_xiaozhi_compute_playback_supply_kind_from_source(
    const river_cloud_xiaozhi_playback_supply_source_t *source)
{
    if (source == NULL) {
        return RIVER_CLOUD_PLAYBACK_SUPPLY_NONE;
    }

    if (source->segment_count != 0U) {
        return RIVER_CLOUD_PLAYBACK_SUPPLY_CURRENT_SEGMENT;
    }
    if (river_cloud_xiaozhi_compute_playback_waiting_next_segment_from_source(source)) {
        return RIVER_CLOUD_PLAYBACK_SUPPLY_WAITING_NEXT_SEGMENT;
    }
    if (source->last_segment_observed) {
        return RIVER_CLOUD_PLAYBACK_SUPPLY_TERMINAL_TAIL;
    }
    return RIVER_CLOUD_PLAYBACK_SUPPLY_NONE;
}

static river_cloud_playback_supply_kind_t __attribute__((unused))
river_cloud_xiaozhi_playback_supply_kind(void)
{
    river_cloud_xiaozhi_playback_supply_source_t source;

    river_cloud_xiaozhi_capture_playback_supply_source(&source);
    return river_cloud_xiaozhi_compute_playback_supply_kind_from_source(&source);
}

static bool river_cloud_xiaozhi_playback_supply_expects_more_audio(
    river_cloud_playback_supply_kind_t supply_kind)
{
    return supply_kind == RIVER_CLOUD_PLAYBACK_SUPPLY_CURRENT_SEGMENT ||
           supply_kind == RIVER_CLOUD_PLAYBACK_SUPPLY_WAITING_NEXT_SEGMENT;
}

typedef struct {
    river_cloud_xiaozhi_playback_backend_source_t backend_source;
    river_cloud_xiaozhi_playback_supply_source_t supply_source;
    river_cloud_playback_backend_state_t backend_state;
    river_cloud_playback_supply_kind_t supply_kind;
    river_cloud_playback_hold_kind_t hold_kind;
    uint32_t queued_frames;
    bool tts_stop_pending;
    bool rebuffer_pending;
    bool output_active;
} river_cloud_xiaozhi_playback_truth_view_t;

typedef struct {
    bool phase_known;
    river_cloud_playback_phase_t phase_kind;
    river_cloud_playback_recovery_path_t recovery_path_kind;
    river_cloud_playback_recovery_outcome_t recovery_outcome_kind;
} river_cloud_xiaozhi_playback_observe_view_t;

typedef struct {
    river_cloud_xiaozhi_playback_truth_view_t truth_view;
    river_cloud_xiaozhi_playback_observe_view_t observe_view;
    river_cloud_xiaozhi_playback_start_gate_t start_gate;
    river_cloud_playback_rebuffer_cause_t rebuffer_cause_kind;
    river_cloud_playback_terminal_wait_kind_t terminal_wait_kind;
    river_cloud_playback_terminal_state_t terminal_state_kind;
    uint32_t queued_frames;
    uint32_t resume_frames;
    uint32_t buffer_budget_frames;
    uint32_t open_hold_frames;
    uint32_t sample_rate;
    uint32_t frame_duration_ms;
    uint32_t prefetch_target_ms;
    uint32_t start_frames;
    uint32_t prefetch_frames;
    uint64_t ring_dropped;
    uint32_t downlink_wait_delay_ms;
    river_status_t downlink_cycle_status;
    uint64_t meta_gap_ms;
    uint64_t reopen_guard_left_ms;
    uint32_t rebuffer_count;
    uint32_t rebuffer_streak;
    river_cloud_xiaozhi_downlink_wait_kind_t downlink_wait_kind;
    river_cloud_xiaozhi_downlink_cycle_outcome_t downlink_cycle_outcome;
    bool downlink_started;
    bool terminal_closed;
    bool terminal_waiting;
    bool duplex_ready_seen;
    bool start_cautious_history;
    bool tts_stop_pending;
} river_cloud_xiaozhi_playback_diag_view_t;

typedef struct {
    river_cloud_xiaozhi_playback_truth_view_t truth_view;
    uint32_t queued_frames;
    uint32_t hold_frames;
    bool ready;
} river_cloud_xiaozhi_segment_gap_hold_view_t;

typedef struct {
    river_cloud_xiaozhi_playback_truth_view_t truth_view;
    bool terminal_waiting;
    river_cloud_playback_terminal_wait_kind_t terminal_wait_kind;
    bool tts_stop_pending;
} river_cloud_xiaozhi_playback_gate_view_t;

typedef struct {
    river_cloud_xiaozhi_playback_truth_view_t truth_view;
    river_cloud_xiaozhi_playback_start_gate_t start_gate;
    river_cloud_playback_rebuffer_cause_t cause;
    river_cloud_playback_recovery_path_t recovery_path;
    uint32_t queued_frames;
    uint32_t low_water_frames;
    uint64_t supply_gap_ms;
    bool inline_recover_allowed;
} river_cloud_xiaozhi_playback_recovery_plan_t;

typedef struct {
    river_cloud_playback_rebuffer_cause_t cause;
    river_cloud_playback_supply_kind_t supply_kind;
    river_cloud_playback_phase_t phase_kind;
    river_cloud_playback_backend_state_t backend_state;
    river_cloud_xiaozhi_playback_start_gate_t start_gate;
    uint32_t queued_frames;
    uint32_t low_water_frames;
    uint32_t prefetch_target_ms;
    uint32_t total_rebuffers;
    uint32_t rebuffer_streak;
    river_cloud_playback_recovery_path_t recovery_path;
} river_cloud_xiaozhi_playback_rebuffer_observe_view_t;

typedef enum {
    RIVER_CLOUD_XIAOZHI_INLINE_RECOVER_NOT_ATTEMPTED = 0,
    RIVER_CLOUD_XIAOZHI_INLINE_RECOVER_SUCCEEDED,
    RIVER_CLOUD_XIAOZHI_INLINE_RECOVER_SERVICE_FAILED,
    RIVER_CLOUD_XIAOZHI_INLINE_RECOVER_REPLAY_FAILED
} river_cloud_xiaozhi_inline_recover_result_t;

typedef enum {
    RIVER_CLOUD_XIAOZHI_WRITE_FAILED_FOLLOWUP_NONE = 0,
    RIVER_CLOUD_XIAOZHI_WRITE_FAILED_FOLLOWUP_INLINE_REPLAY_CONSUMED,
    RIVER_CLOUD_XIAOZHI_WRITE_FAILED_FOLLOWUP_MANAGED_REBUFFER
} river_cloud_xiaozhi_write_failed_followup_kind_t;

typedef struct {
    const char *recover_reason;
    bool keep_retry_frame;
    bool force_stop_rebuffer;
} river_cloud_xiaozhi_managed_rebuffer_request_t;

typedef struct {
    const char *reason;
    river_cloud_playback_rebuffer_cause_t cause;
    river_cloud_playback_supply_kind_t supply_kind;
    river_cloud_playback_recovery_path_t requested_path;
    river_cloud_playback_recovery_path_t fallback_path;
} river_cloud_xiaozhi_rebuffer_recovery_attempt_t;

typedef struct {
    river_status_t status;
    river_cloud_playback_recovery_path_t applied_path;
} river_cloud_xiaozhi_rebuffer_recovery_result_t;

typedef struct {
    river_cloud_xiaozhi_write_failed_followup_kind_t kind;
    river_cloud_xiaozhi_inline_recover_result_t inline_result;
    const char *request_log;
    river_cloud_xiaozhi_managed_rebuffer_request_t rebuffer_request;
} river_cloud_xiaozhi_write_failed_followup_t;

typedef struct {
    river_cloud_xiaozhi_playback_recovery_plan_t recovery_plan;
    const char *recover_reason;
    river_cloud_playback_recovery_path_t recovery_path;
    uint64_t now_ms;
    size_t mono_bytes;
    size_t stereo_bytes;
} river_cloud_xiaozhi_write_failed_recovery_view_t;

typedef enum {
    RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_CONTINUE = 0,
    RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_SLEEP_POLL,
    RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_SLEEP_IDLE
} river_cloud_xiaozhi_downlink_task_step_result_t;

typedef struct {
    river_cloud_xiaozhi_downlink_task_step_result_t step_result;
    river_cloud_xiaozhi_write_failed_followup_kind_t followup_kind;
    river_cloud_xiaozhi_inline_recover_result_t inline_result;
    river_cloud_playback_recovery_path_t recovery_path;
    river_status_t recovery_status;
    bool frame_consumed;
} river_cloud_xiaozhi_write_failed_recovery_result_t;

typedef struct {
    river_cloud_xiaozhi_downlink_task_step_result_t step_result;
    river_cloud_xiaozhi_downlink_wait_kind_t wait_kind;
    river_status_t status;
    uint32_t queued_frames;
    bool ready;
} river_cloud_xiaozhi_downlink_cycle_plan_t;

typedef struct {
    river_cloud_xiaozhi_downlink_wait_kind_t wait_kind;
    river_status_t status;
    bool ready;
} river_cloud_xiaozhi_downlink_playback_prepare_result_t;

typedef struct {
    river_cloud_xiaozhi_downlink_task_step_result_t step_result;
    river_status_t status;
    bool acquired;
} river_cloud_xiaozhi_downlink_frame_acquire_result_t;

typedef struct {
    river_cloud_xiaozhi_downlink_task_step_result_t step_result;
    river_status_t status;
    river_status_t recovery_status;
    river_cloud_xiaozhi_write_failed_followup_kind_t recovery_followup_kind;
    river_cloud_playback_recovery_path_t recovery_path;
    bool frame_consumed;
    bool write_failed;
    bool aborted;
} river_cloud_xiaozhi_downlink_frame_write_result_t;

typedef struct {
    river_cloud_xiaozhi_downlink_task_step_result_t step_result;
    river_status_t status;
    river_cloud_xiaozhi_downlink_cycle_outcome_t outcome;
    river_cloud_xiaozhi_downlink_cycle_plan_t cycle_plan;
    river_cloud_xiaozhi_downlink_frame_acquire_result_t acquire_result;
    river_cloud_xiaozhi_downlink_frame_write_result_t write_result;
} river_cloud_xiaozhi_downlink_task_cycle_result_t;

typedef struct {
    bool sleep;
    uint32_t delay_ms;
    river_status_t status;
    river_cloud_xiaozhi_downlink_wait_kind_t wait_kind;
} river_cloud_xiaozhi_downlink_task_wait_plan_t;

static void river_cloud_xiaozhi_capture_playback_truth_view(
    river_cloud_xiaozhi_playback_truth_view_t *view)
{
    if (view == NULL) {
        return;
    }

    memset(view, 0, sizeof(*view));
    river_cloud_xiaozhi_capture_playback_backend_source(&view->backend_source);
    river_cloud_xiaozhi_capture_playback_supply_source(&view->supply_source);
    view->backend_state = river_cloud_xiaozhi_compute_playback_backend_state_from_source(
        &view->backend_source);
    view->supply_kind = river_cloud_xiaozhi_compute_playback_supply_kind_from_source(
        &view->supply_source);
    view->hold_kind =
        river_cloud_xiaozhi_playback_hold_kind_from_source(&view->backend_source);
    view->queued_frames = river_cloud_xiaozhi_playback_queued_frames();
    view->tts_stop_pending = g_river_cloud.xiaozhi_playback_runtime_truth.stop_pending;
    view->rebuffer_pending =
        g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_pending;
    view->output_active =
        river_cloud_xiaozhi_playback_backend_source_output_active(
            &view->backend_source) &&
        river_cloud_xiaozhi_playback_backend_output_active(view->backend_state);
}

static void river_cloud_xiaozhi_capture_playback_observe_view(
    river_cloud_xiaozhi_playback_observe_view_t *view)
{
    if (view == NULL) {
        return;
    }

    memset(view, 0, sizeof(*view));
    view->phase_known = true;
    view->phase_kind = river_cloud_xiaozhi_playback_phase();
    view->recovery_path_kind = river_cloud_xiaozhi_playback_recovery_path();
    view->recovery_outcome_kind = river_cloud_xiaozhi_playback_recovery_outcome();
}

static river_cloud_playback_phase_t river_cloud_xiaozhi_playback_observed_phase_kind(void)
{
    return river_cloud_xiaozhi_playback_phase();
}

static void river_cloud_xiaozhi_capture_playback_rebuffer_observe_view(
    const river_cloud_xiaozhi_playback_recovery_plan_t *recovery_plan,
    river_cloud_playback_recovery_path_t recovery_path,
    river_cloud_xiaozhi_playback_rebuffer_observe_view_t *view)
{
    if (recovery_plan == NULL || view == NULL) {
        return;
    }

    memset(view, 0, sizeof(*view));
    view->cause = recovery_plan->cause;
    view->supply_kind = recovery_plan->truth_view.supply_kind;
    view->phase_kind = river_cloud_xiaozhi_playback_observed_phase_kind();
    view->backend_state = recovery_plan->truth_view.backend_state;
    view->start_gate = recovery_plan->start_gate;
    view->queued_frames = recovery_plan->queued_frames;
    view->low_water_frames = recovery_plan->low_water_frames;
    view->prefetch_target_ms =
        g_river_cloud.xiaozhi_playback_meta_truth.prefetch_target_ms;
    view->total_rebuffers = g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_count;
    view->rebuffer_streak =
        g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_streak;
    view->recovery_path = recovery_path;
}

static bool river_cloud_xiaozhi_playback_supply_engages_lane(
    river_cloud_playback_supply_kind_t supply_kind)
{
    return supply_kind == RIVER_CLOUD_PLAYBACK_SUPPLY_CURRENT_SEGMENT ||
           supply_kind == RIVER_CLOUD_PLAYBACK_SUPPLY_WAITING_NEXT_SEGMENT;
}

static bool river_cloud_xiaozhi_playback_lane_engaged_from_truth_view(
    const river_cloud_xiaozhi_playback_truth_view_t *view)
{
    return view != NULL &&
           (view->tts_stop_pending || view->rebuffer_pending ||
            view->output_active || view->queued_frames != 0U ||
            river_cloud_xiaozhi_playback_supply_engages_lane(view->supply_kind));
}

static bool river_cloud_xiaozhi_playback_turn_active_from_truth_view(
    const river_cloud_xiaozhi_playback_truth_view_t *view)
{
    return river_cloud_xiaozhi_playback_lane_engaged_from_truth_view(view) ||
           (river_cloud_xiaozhi_playback_terminal_open() &&
            !river_cloud_xiaozhi_playback_terminal_closed() &&
            river_cloud_xiaozhi_playback_response_context_valid());
}

static bool river_cloud_xiaozhi_playback_truth_view_retains_output_turn(
    const river_cloud_xiaozhi_playback_truth_view_t *view)
{
    return view != NULL &&
           (view->output_active || view->rebuffer_pending ||
            view->supply_kind ==
                RIVER_CLOUD_PLAYBACK_SUPPLY_WAITING_NEXT_SEGMENT);
}

static void river_cloud_xiaozhi_capture_segment_gap_hold_view(
    river_cloud_xiaozhi_segment_gap_hold_view_t *view)
{
    if (view == NULL) {
        return;
    }

    memset(view, 0, sizeof(*view));
    river_cloud_xiaozhi_capture_playback_truth_view(&view->truth_view);
    view->queued_frames = river_cloud_xiaozhi_playback_queued_frames();
    view->hold_frames = river_cloud_xiaozhi_downlink_segment_gap_hold_frames();
    view->ready =
        view->truth_view.output_active &&
        !g_river_cloud.xiaozhi_playback_runtime_truth.stop_pending &&
        !g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_pending &&
        view->truth_view.supply_kind ==
            RIVER_CLOUD_PLAYBACK_SUPPLY_WAITING_NEXT_SEGMENT &&
        view->queued_frames <= view->hold_frames &&
        river_cloud_xiaozhi_playback_backend_stream_attached(
            view->truth_view.backend_state);
}

static void river_cloud_xiaozhi_capture_playback_gate_view(
    river_cloud_xiaozhi_playback_gate_view_t *view)
{
    if (view == NULL) {
        return;
    }

    memset(view, 0, sizeof(*view));
    river_cloud_xiaozhi_capture_playback_truth_view(&view->truth_view);
    view->terminal_waiting = g_river_cloud.xiaozhi_playback_terminal_truth.waiting;
    view->terminal_wait_kind = g_river_cloud.xiaozhi_playback_terminal_truth.wait_kind;
    view->tts_stop_pending = g_river_cloud.xiaozhi_playback_runtime_truth.stop_pending;
}

static bool river_cloud_xiaozhi_playback_quiet_window_from_gate_view(
    const river_cloud_xiaozhi_playback_gate_view_t *view)
{
    if (view == NULL || view->truth_view.output_active) {
        return false;
    }

    if (view->tts_stop_pending) {
        return true;
    }
    if (view->terminal_waiting &&
        view->terminal_wait_kind != RIVER_CLOUD_PLAYBACK_TERMINAL_WAIT_NONE) {
        return true;
    }
    if (view->truth_view.hold_kind != RIVER_CLOUD_PLAYBACK_HOLD_NONE) {
        return true;
    }

    switch (view->truth_view.backend_state) {
    case RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_RECOVERING:
    case RIVER_CLOUD_PLAYBACK_BACKEND_RESTART_PENDING:
        return true;
    case RIVER_CLOUD_PLAYBACK_BACKEND_DETACHED:
        return view->truth_view.supply_kind != RIVER_CLOUD_PLAYBACK_SUPPLY_NONE;
    default:
        return false;
    }
}
