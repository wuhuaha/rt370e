/* 对话运行时真相源：统一吸收 cloud/playback/ASR 事实并派生交互状态。 */
#include <string.h>

#include "basic_types.h"
#include "os_wrapper.h"

#include "river/river_dialog_runtime.h"
#include "river/river_log.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.dialog"

typedef struct {
    bool initialized;
    bool cloud_runtime_available;
    bool local_playback_stream_owned;
    bool asr_error_recovering;
    bool local_playback_error_recovering;
    char local_playback_stream_name[32];
    river_playback_state_t playback_state;
    rtos_mutex_t lock;
    river_dialog_runtime_snapshot_t snapshot;
} river_dialog_runtime_context_t;

typedef enum {
    RIVER_DIALOG_RUNTIME_CLOUD_EVENT_BOOT_READY = 0,
    RIVER_DIALOG_RUNTIME_CLOUD_EVENT_WAKE_CONFIRMED,
    RIVER_DIALOG_RUNTIME_CLOUD_EVENT_ASR_SESSION_STARTED,
    RIVER_DIALOG_RUNTIME_CLOUD_EVENT_ASR_SESSION_CLOSED,
    RIVER_DIALOG_RUNTIME_CLOUD_EVENT_ASR_ERROR
} river_dialog_runtime_cloud_event_t;

static river_dialog_runtime_context_t g_river_dialog_runtime;

static void river_dialog_runtime_apply_cloud_snapshot_locked(
    const river_cloud_runtime_snapshot_t *snapshot);
static void river_dialog_runtime_publish_locked(const char *reason);
static bool river_dialog_runtime_output_turn_engaged_locked(void);
static const char *river_dialog_runtime_wakeword_block_reason_locked(void);
static void river_dialog_runtime_set_wake_admission_pending_locked(bool pending);
static void river_dialog_runtime_refresh_error_recovering_locked(void);

static bool river_dialog_runtime_lock(void)
{
    return g_river_dialog_runtime.lock != NULL &&
           rtos_mutex_take(g_river_dialog_runtime.lock, MUTEX_WAIT_TIMEOUT) == RTK_SUCCESS;
}

static void river_dialog_runtime_unlock(void)
{
    if (g_river_dialog_runtime.lock != NULL) {
        (void)rtos_mutex_give(g_river_dialog_runtime.lock);
    }
}

static void river_dialog_runtime_copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0U) {
        return;
    }

    if (src == NULL || src[0] == '\0') {
        dst[0] = '\0';
        return;
    }

    strncpy(dst, src, dst_size - 1U);
    dst[dst_size - 1U] = '\0';
}

static void river_dialog_runtime_refresh_error_recovering_locked(void)
{
    bool asr_error = g_river_dialog_runtime.asr_error_recovering;
    bool local_playback_error = g_river_dialog_runtime.local_playback_error_recovering;

    g_river_dialog_runtime.snapshot.error_recovering =
        asr_error || local_playback_error;
    if (asr_error && local_playback_error) {
        g_river_dialog_runtime.snapshot.error_kind = RIVER_DIALOG_ERROR_KIND_MIXED;
    } else if (asr_error) {
        g_river_dialog_runtime.snapshot.error_kind = RIVER_DIALOG_ERROR_KIND_ASR;
    } else if (local_playback_error) {
        g_river_dialog_runtime.snapshot.error_kind =
            RIVER_DIALOG_ERROR_KIND_LOCAL_PLAYBACK;
    } else {
        g_river_dialog_runtime.snapshot.error_kind = RIVER_DIALOG_ERROR_KIND_NONE;
    }
}

static bool river_dialog_runtime_is_dialog_playback_stream_locked(
    const river_playback_stream_config_t *config)
{
    return config != NULL && config->priority == RIVER_PLAYBACK_PRIO_TTS;
}

static bool river_dialog_runtime_matches_owned_playback_stream_locked(
    const river_playback_stream_config_t *config)
{
    if (!g_river_dialog_runtime.local_playback_stream_owned) {
        return false;
    }
    if (config == NULL || config->stream_name == NULL || config->stream_name[0] == '\0') {
        return true;
    }
    if (g_river_dialog_runtime.local_playback_stream_name[0] == '\0') {
        return true;
    }
    return strcmp(g_river_dialog_runtime.local_playback_stream_name, config->stream_name) == 0;
}

static bool river_dialog_runtime_capture_cloud_snapshot(
    river_cloud_runtime_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    return river_cloud_adapter_get_runtime_snapshot(snapshot) == RIVER_OK;
}

static const char *river_dialog_runtime_cloud_event_default_reason(
    river_dialog_runtime_cloud_event_t event)
{
    switch (event) {
    case RIVER_DIALOG_RUNTIME_CLOUD_EVENT_BOOT_READY:
        return "boot_ready";
    case RIVER_DIALOG_RUNTIME_CLOUD_EVENT_WAKE_CONFIRMED:
        return "wakeword_detected";
    case RIVER_DIALOG_RUNTIME_CLOUD_EVENT_ASR_SESSION_STARTED:
        return "asr_session_started";
    case RIVER_DIALOG_RUNTIME_CLOUD_EVENT_ASR_SESSION_CLOSED:
        return "asr_session_closed";
    case RIVER_DIALOG_RUNTIME_CLOUD_EVENT_ASR_ERROR:
        return "asr_error";
    default:
        return "cloud_event";
    }
}

static void river_dialog_runtime_apply_cloud_event_locked(
    river_dialog_runtime_cloud_event_t event,
    const char *sid)
{
    switch (event) {
    case RIVER_DIALOG_RUNTIME_CLOUD_EVENT_BOOT_READY:
        g_river_dialog_runtime.snapshot.boot_ready = true;
        g_river_dialog_runtime.asr_error_recovering = false;
        g_river_dialog_runtime.local_playback_error_recovering = false;
        g_river_dialog_runtime.snapshot.wake_admission_pending = false;
        g_river_dialog_runtime.snapshot.wake_confirmed = false;
        g_river_dialog_runtime.snapshot.tts_interrupt_requested = false;
        break;
    case RIVER_DIALOG_RUNTIME_CLOUD_EVENT_WAKE_CONFIRMED:
        g_river_dialog_runtime.snapshot.wake_admission_pending = false;
        g_river_dialog_runtime.snapshot.wake_confirmed = true;
        break;
    case RIVER_DIALOG_RUNTIME_CLOUD_EVENT_ASR_SESSION_STARTED:
        g_river_dialog_runtime.snapshot.asr_session_active = true;
        g_river_dialog_runtime.asr_error_recovering = false;
        g_river_dialog_runtime.snapshot.wake_confirmed = false;
        g_river_dialog_runtime.snapshot.tts_interrupt_requested = false;
        break;
    case RIVER_DIALOG_RUNTIME_CLOUD_EVENT_ASR_SESSION_CLOSED:
        g_river_dialog_runtime.snapshot.asr_session_active = false;
        g_river_dialog_runtime.snapshot.wake_confirmed = false;
        g_river_dialog_runtime.snapshot.tts_interrupt_requested = false;
        break;
    case RIVER_DIALOG_RUNTIME_CLOUD_EVENT_ASR_ERROR:
        g_river_dialog_runtime.snapshot.asr_session_active = false;
        g_river_dialog_runtime.asr_error_recovering = true;
        g_river_dialog_runtime.snapshot.wake_confirmed = false;
        g_river_dialog_runtime.snapshot.tts_interrupt_requested = false;
        break;
    default:
        break;
    }
    river_dialog_runtime_refresh_error_recovering_locked();

    if (sid != NULL && sid[0] != '\0') {
        river_dialog_runtime_copy_text(g_river_dialog_runtime.snapshot.session_id,
                                       sizeof(g_river_dialog_runtime.snapshot.session_id),
                                       sid);
    }
}

static void river_dialog_runtime_commit_cloud_event(
    river_dialog_runtime_cloud_event_t event,
    const char *sid,
    const char *reason)
{
    river_cloud_runtime_snapshot_t snapshot;
    bool have_snapshot = river_dialog_runtime_capture_cloud_snapshot(&snapshot);

    if (!river_dialog_runtime_lock()) {
        return;
    }

    river_dialog_runtime_apply_cloud_event_locked(event, sid);
    if (have_snapshot) {
        river_dialog_runtime_apply_cloud_snapshot_locked(&snapshot);
    }
    river_dialog_runtime_publish_locked(reason != NULL ? reason :
                                        river_dialog_runtime_cloud_event_default_reason(event));
    river_dialog_runtime_unlock();
}

static river_dialog_input_lane_t river_dialog_runtime_parse_input_lane(const char *state)
{
    if (state == NULL || state[0] == '\0') {
        return RIVER_DIALOG_INPUT_LANE_UNKNOWN;
    }
    if (strcmp(state, "idle") == 0) {
        return RIVER_DIALOG_INPUT_LANE_IDLE;
    }
    if (strcmp(state, "active") == 0) {
        return RIVER_DIALOG_INPUT_LANE_ACTIVE;
    }
    if (strcmp(state, "committed") == 0) {
        return RIVER_DIALOG_INPUT_LANE_COMMITTED;
    }
    return RIVER_DIALOG_INPUT_LANE_UNKNOWN;
}

static river_dialog_output_lane_t river_dialog_runtime_parse_output_lane(const char *state)
{
    if (state == NULL || state[0] == '\0') {
        return RIVER_DIALOG_OUTPUT_LANE_UNKNOWN;
    }
    if (strcmp(state, "idle") == 0) {
        return RIVER_DIALOG_OUTPUT_LANE_IDLE;
    }
    if (strcmp(state, "thinking") == 0) {
        return RIVER_DIALOG_OUTPUT_LANE_THINKING;
    }
    if (strcmp(state, "speaking") == 0) {
        return RIVER_DIALOG_OUTPUT_LANE_SPEAKING;
    }
    return RIVER_DIALOG_OUTPUT_LANE_UNKNOWN;
}

const char *river_dialog_input_lane_name(river_dialog_input_lane_t state)
{
    switch (state) {
    case RIVER_DIALOG_INPUT_LANE_IDLE:
        return "idle";
    case RIVER_DIALOG_INPUT_LANE_ACTIVE:
        return "active";
    case RIVER_DIALOG_INPUT_LANE_COMMITTED:
        return "committed";
    default:
        return "unknown";
    }
}

const char *river_dialog_output_lane_name(river_dialog_output_lane_t state)
{
    switch (state) {
    case RIVER_DIALOG_OUTPUT_LANE_IDLE:
        return "idle";
    case RIVER_DIALOG_OUTPUT_LANE_THINKING:
        return "thinking";
    case RIVER_DIALOG_OUTPUT_LANE_SPEAKING:
        return "speaking";
    default:
        return "unknown";
    }
}

const char *river_dialog_playback_owner_kind_name(river_dialog_playback_owner_kind_t kind)
{
    switch (kind) {
    case RIVER_DIALOG_PLAYBACK_OWNER_KIND_CLOUD:
        return "cloud";
    case RIVER_DIALOG_PLAYBACK_OWNER_KIND_LOCAL_FALLBACK:
        return "local_fallback";
    case RIVER_DIALOG_PLAYBACK_OWNER_KIND_NONE:
    default:
        return "none";
    }
}

const char *river_dialog_error_kind_name(river_dialog_error_kind_t kind)
{
    switch (kind) {
    case RIVER_DIALOG_ERROR_KIND_ASR:
        return "asr";
    case RIVER_DIALOG_ERROR_KIND_LOCAL_PLAYBACK:
        return "local_playback";
    case RIVER_DIALOG_ERROR_KIND_MIXED:
        return "mixed";
    case RIVER_DIALOG_ERROR_KIND_NONE:
    default:
        return "none";
    }
}

static bool river_dialog_runtime_cloud_runtime_available_locked(void)
{
    return g_river_dialog_runtime.cloud_runtime_available;
}

static bool river_dialog_runtime_local_playback_state_active_locked(void)
{
    return river_playback_service_state_active(g_river_dialog_runtime.playback_state);
}

static bool river_dialog_runtime_local_playback_state_recovering_locked(void)
{
    return g_river_dialog_runtime.playback_state == RIVER_PLAYBACK_RECOVERING ||
           g_river_dialog_runtime.playback_state == RIVER_PLAYBACK_RESTART_PENDING;
}

static void river_dialog_runtime_apply_local_playback_state_locked(
    river_playback_state_t state)
{
    g_river_dialog_runtime.playback_state = state;
}

static bool river_dialog_runtime_local_playback_shadow_drives_truth_locked(void)
{
    return !river_dialog_runtime_cloud_runtime_available_locked();
}

static bool river_dialog_runtime_local_playback_shadow_active_fallback_locked(void)
{
    if (!river_dialog_runtime_local_playback_shadow_drives_truth_locked()) {
        return false;
    }

    return river_dialog_runtime_local_playback_state_active_locked();
}

static bool river_dialog_runtime_local_playback_shadow_recovering_fallback_locked(void)
{
    if (!river_dialog_runtime_local_playback_shadow_drives_truth_locked()) {
        return false;
    }

    return river_dialog_runtime_local_playback_state_recovering_locked();
}

typedef struct {
    bool cloud_runtime_available;
    bool cloud_playback_active;
    bool lane_engaged;
    bool turn_active;
    bool rebuffer_pending;
    bool playback_recovering;
    bool phase_known;
    bool terminal_closed;
    bool terminal_waiting;
    bool tts_stop_pending;
    river_cloud_playback_phase_t phase_kind;
    river_cloud_playback_backend_state_t backend_state_kind;
    river_cloud_playback_hold_kind_t hold_kind;
    river_cloud_playback_terminal_wait_kind_t terminal_wait_kind;
    river_dialog_output_lane_t output_lane;
    bool local_shadow_active;
    bool local_shadow_recovering;
} river_dialog_runtime_playback_projection_t;

static void river_dialog_runtime_capture_playback_projection_locked(
    river_dialog_runtime_playback_projection_t *projection)
{
    const river_dialog_runtime_snapshot_t *snapshot = &g_river_dialog_runtime.snapshot;

    if (projection == NULL) {
        return;
    }

    memset(projection, 0, sizeof(*projection));
    projection->cloud_runtime_available =
        river_dialog_runtime_cloud_runtime_available_locked();
    projection->cloud_playback_active = snapshot->playback_cloud_active;
    projection->lane_engaged = snapshot->playback_lane_engaged;
    projection->turn_active = snapshot->playback_turn_active;
    projection->rebuffer_pending = snapshot->playback_rebuffer_pending;
    projection->playback_recovering = snapshot->playback_recovering;
    projection->phase_known = snapshot->playback_phase_known;
    projection->terminal_closed = snapshot->playback_terminal_closed;
    projection->terminal_waiting = snapshot->playback_terminal_waiting;
    projection->tts_stop_pending = snapshot->tts_stop_pending;
    projection->phase_kind = snapshot->playback_phase_kind;
    projection->backend_state_kind = snapshot->playback_backend_state_kind;
    projection->hold_kind = snapshot->playback_hold_kind;
    projection->terminal_wait_kind = snapshot->playback_terminal_wait_kind;
    projection->output_lane = snapshot->output_lane;
    projection->local_shadow_active =
        river_dialog_runtime_local_playback_shadow_active_fallback_locked();
    projection->local_shadow_recovering =
        river_dialog_runtime_local_playback_shadow_recovering_fallback_locked();
}

static bool river_dialog_runtime_playback_waiting_segment_from_projection(
    const river_dialog_runtime_playback_projection_t *projection)
{
    return projection != NULL && projection->phase_known &&
           projection->phase_kind == RIVER_CLOUD_PLAYBACK_PHASE_WAITING_SEGMENT;
}

static bool river_dialog_runtime_playback_segment_gap_hold_from_projection(
    const river_dialog_runtime_playback_projection_t *projection)
{
    return projection != NULL &&
           projection->hold_kind == RIVER_CLOUD_PLAYBACK_HOLD_SEGMENT_GAP;
}

static bool river_dialog_runtime_terminal_wait_suppresses_speaking_from_projection(
    const river_dialog_runtime_playback_projection_t *projection)
{
    if (projection == NULL) {
        return false;
    }

    switch (projection->terminal_wait_kind) {
    case RIVER_CLOUD_PLAYBACK_TERMINAL_WAIT_QUEUE_DRAIN:
    case RIVER_CLOUD_PLAYBACK_TERMINAL_WAIT_LAST_SEGMENT_TAIL:
        return true;
    default:
        return false;
    }
}

static river_dialog_playback_owner_kind_t
river_dialog_runtime_compute_playback_owner_kind_from_projection(
    const river_dialog_runtime_playback_projection_t *projection)
{
    if (projection == NULL) {
        return RIVER_DIALOG_PLAYBACK_OWNER_KIND_NONE;
    }

    if (projection->cloud_runtime_available &&
        (projection->cloud_playback_active || projection->lane_engaged ||
         projection->turn_active || projection->playback_recovering ||
         projection->terminal_waiting || projection->terminal_closed ||
         projection->hold_kind != RIVER_CLOUD_PLAYBACK_HOLD_NONE ||
         projection->tts_stop_pending)) {
        return RIVER_DIALOG_PLAYBACK_OWNER_KIND_CLOUD;
    }

    if (projection->local_shadow_active || projection->local_shadow_recovering) {
        return RIVER_DIALOG_PLAYBACK_OWNER_KIND_LOCAL_FALLBACK;
    }

    return RIVER_DIALOG_PLAYBACK_OWNER_KIND_NONE;
}

static bool river_dialog_runtime_compute_playback_recovering_from_projection(
    const river_dialog_runtime_playback_projection_t *projection)
{
    return projection != NULL &&
           (projection->rebuffer_pending ||
            projection->backend_state_kind ==
                RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_RECOVERING ||
            projection->backend_state_kind ==
                RIVER_CLOUD_PLAYBACK_BACKEND_RESTART_PENDING ||
            projection->local_shadow_recovering);
}

static bool river_dialog_runtime_playback_error_is_managed_recovery_locked(void)
{
    river_dialog_runtime_playback_projection_t projection;

    river_dialog_runtime_capture_playback_projection_locked(&projection);
    if (!projection.cloud_runtime_available) {
        return false;
    }

    return projection.rebuffer_pending ||
           projection.backend_state_kind ==
               RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_RECOVERING ||
           projection.backend_state_kind ==
               RIVER_CLOUD_PLAYBACK_BACKEND_RESTART_PENDING ||
           projection.cloud_playback_active;
}

static bool river_dialog_runtime_compute_playback_active_from_projection(
    const river_dialog_runtime_playback_projection_t *projection)
{
    if (projection == NULL || projection->terminal_closed) {
        return false;
    }

    if (projection->cloud_playback_active || projection->playback_recovering ||
        river_dialog_runtime_playback_segment_gap_hold_from_projection(projection)) {
        return true;
    }

    if (!projection->local_shadow_active && !projection->cloud_playback_active &&
        !projection->playback_recovering && projection->terminal_waiting) {
        return false;
    }

    return projection->local_shadow_active;
}

static bool river_dialog_runtime_playback_turn_retains_output_turn_from_projection(
    const river_dialog_runtime_playback_projection_t *projection)
{
    if (projection == NULL || !projection->turn_active) {
        return false;
    }
    if (!projection->lane_engaged) {
        return projection->output_lane == RIVER_DIALOG_OUTPUT_LANE_SPEAKING &&
               !(projection->terminal_waiting &&
                 river_dialog_runtime_terminal_wait_suppresses_speaking_from_projection(
                     projection));
    }
    if (!projection->phase_known) {
        return !projection->cloud_runtime_available;
    }

    return projection->phase_kind == RIVER_CLOUD_PLAYBACK_PHASE_REBUFFERING;
}

static bool river_dialog_runtime_output_speaking_effective_from_projection(
    const river_dialog_runtime_playback_projection_t *projection,
    bool playback_active)
{
    if (projection == NULL ||
        projection->output_lane != RIVER_DIALOG_OUTPUT_LANE_SPEAKING ||
        projection->terminal_closed) {
        return false;
    }
    if (river_dialog_runtime_playback_waiting_segment_from_projection(projection) &&
        !projection->cloud_playback_active && !projection->playback_recovering) {
        return false;
    }
    if (projection->terminal_waiting &&
        river_dialog_runtime_terminal_wait_suppresses_speaking_from_projection(
            projection) &&
        !playback_active && !projection->playback_recovering) {
        return false;
    }
    if (!playback_active && !projection->playback_recovering &&
        !river_dialog_runtime_playback_turn_retains_output_turn_from_projection(
            projection)) {
        return false;
    }

    return true;
}

static bool river_dialog_runtime_output_turn_engaged_from_projection(
    const river_dialog_runtime_playback_projection_t *projection,
    bool playback_active)
{
    if (projection == NULL || projection->terminal_closed) {
        return false;
    }

    return playback_active ||
           river_dialog_runtime_playback_turn_retains_output_turn_from_projection(
               projection) ||
           river_dialog_runtime_output_speaking_effective_from_projection(
               projection,
               playback_active);
}

static void river_dialog_runtime_refresh_playback_locked(void)
{
    river_dialog_runtime_playback_projection_t projection;

    river_dialog_runtime_capture_playback_projection_locked(&projection);
    projection.playback_recovering =
        river_dialog_runtime_compute_playback_recovering_from_projection(&projection);
    g_river_dialog_runtime.snapshot.playback_recovering =
        projection.playback_recovering;
    g_river_dialog_runtime.snapshot.playback_active =
        river_dialog_runtime_compute_playback_active_from_projection(&projection);
    g_river_dialog_runtime.snapshot.playback_owner_kind =
        river_dialog_runtime_compute_playback_owner_kind_from_projection(
            &projection);
}

static bool river_dialog_runtime_output_turn_quiesced_locked(void)
{
    river_dialog_runtime_playback_projection_t projection;

    river_dialog_runtime_capture_playback_projection_locked(&projection);
    return !river_dialog_runtime_output_turn_engaged_from_projection(
               &projection,
               g_river_dialog_runtime.snapshot.playback_active) &&
           !projection.turn_active &&
           !projection.playback_recovering &&
           !projection.tts_stop_pending &&
           projection.backend_state_kind !=
               RIVER_CLOUD_PLAYBACK_BACKEND_RESTART_PENDING &&
           !projection.terminal_waiting;
}

static bool river_dialog_runtime_output_turn_engaged_locked(void)
{
    river_dialog_runtime_playback_projection_t projection;

    river_dialog_runtime_capture_playback_projection_locked(&projection);
    return river_dialog_runtime_output_turn_engaged_from_projection(
        &projection,
        g_river_dialog_runtime.snapshot.playback_active);
}

static bool river_dialog_runtime_cloud_round_active_locked(void)
{
    const river_dialog_runtime_snapshot_t *snapshot = &g_river_dialog_runtime.snapshot;

    return snapshot->cloud_listening ||
           snapshot->cloud_stream_active ||
           snapshot->cloud_listen_stop_pending ||
           snapshot->cloud_local_close_pending ||
           snapshot->input_lane == RIVER_DIALOG_INPUT_LANE_ACTIVE ||
           snapshot->input_lane == RIVER_DIALOG_INPUT_LANE_COMMITTED;
}

static bool river_dialog_runtime_tts_interrupt_inflight_locked(void)
{
    return g_river_dialog_runtime.snapshot.tts_stop_pending ||
           g_river_dialog_runtime.snapshot.tts_interrupt_requested;
}

static river_interaction_state_t river_dialog_runtime_compute_interaction_state_locked(void)
{
    const river_dialog_runtime_snapshot_t *snapshot = &g_river_dialog_runtime.snapshot;
    bool output_turn_engaged = river_dialog_runtime_output_turn_engaged_locked();

    if (!snapshot->boot_ready) {
        return RIVER_INTERACTION_BOOTING;
    }
    if (snapshot->error_recovering) {
        return RIVER_INTERACTION_ERROR_RECOVERING;
    }
    if (output_turn_engaged) {
        if (snapshot->asr_session_active || snapshot->input_lane == RIVER_DIALOG_INPUT_LANE_ACTIVE) {
            return RIVER_INTERACTION_BARGE_IN_LISTENING;
        }
        return RIVER_INTERACTION_SPEAKING;
    }
    if (snapshot->output_lane == RIVER_DIALOG_OUTPUT_LANE_THINKING) {
        return RIVER_INTERACTION_THINKING;
    }
    if (snapshot->asr_session_active || snapshot->input_lane == RIVER_DIALOG_INPUT_LANE_ACTIVE) {
        return RIVER_INTERACTION_ASR_STREAMING;
    }
    if (snapshot->wake_confirmed) {
        return RIVER_INTERACTION_WAKE_CONFIRMED;
    }
    if (snapshot->conversation_window_active) {
        return RIVER_INTERACTION_FOLLOW_UP;
    }
    return RIVER_INTERACTION_WAKE_MONITORING;
}

static void river_dialog_runtime_publish_locked(const char *reason)
{
    river_interaction_state_t next_state;

    next_state = river_dialog_runtime_compute_interaction_state_locked();
    if (g_river_dialog_runtime.snapshot.interaction_state != next_state) {
        g_river_dialog_runtime.snapshot.transition_count++;
    }
    g_river_dialog_runtime.snapshot.interaction_state = next_state;
    if (reason != NULL && reason[0] != '\0') {
        river_dialog_runtime_copy_text(g_river_dialog_runtime.snapshot.reason,
                                       sizeof(g_river_dialog_runtime.snapshot.reason),
                                       reason);
    }
    (void)river_interaction_state_set(next_state,
                                      g_river_dialog_runtime.snapshot.reason[0] != '\0' ?
                                          g_river_dialog_runtime.snapshot.reason :
                                          reason);
}

static const char *river_dialog_runtime_local_playback_state_name(
    river_playback_state_t state)
{
    switch (state) {
    case RIVER_PLAYBACK_IDLE:
        return "idle";
    case RIVER_PLAYBACK_RUNNING:
        return "running";
    case RIVER_PLAYBACK_RECOVERING:
        return "recovering";
    case RIVER_PLAYBACK_RESTART_PENDING:
        return "restart_pending";
    case RIVER_PLAYBACK_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

static void river_dialog_runtime_apply_cloud_snapshot_locked(
    const river_cloud_runtime_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    g_river_dialog_runtime.cloud_runtime_available = snapshot->available;
    if (g_river_dialog_runtime.cloud_runtime_available) {
        g_river_dialog_runtime.local_playback_error_recovering = false;
    }
    g_river_dialog_runtime.snapshot.conversation_window_active =
        snapshot->conversation_window_active;
    g_river_dialog_runtime.snapshot.conversation_window_remaining_ms =
        snapshot->conversation_window_remaining_ms;
    g_river_dialog_runtime.snapshot.cloud_listening = snapshot->listening;
    g_river_dialog_runtime.snapshot.cloud_stream_active = snapshot->stream_active;
    g_river_dialog_runtime.snapshot.cloud_listen_stop_pending =
        snapshot->listen_stop_pending;
    g_river_dialog_runtime.snapshot.cloud_local_close_pending =
        snapshot->local_close_pending;
    g_river_dialog_runtime.snapshot.local_close_remaining_ms =
        snapshot->local_close_remaining_ms;
    g_river_dialog_runtime.snapshot.playback_cloud_active = snapshot->playback_active;
    g_river_dialog_runtime.snapshot.playback_lane_engaged = snapshot->playback_lane_engaged;
    g_river_dialog_runtime.snapshot.playback_turn_active = snapshot->playback_turn_active;
    g_river_dialog_runtime.snapshot.playback_rebuffer_pending =
        snapshot->playback_rebuffer_pending;
    g_river_dialog_runtime.snapshot.playback_phase_known =
        snapshot->playback_phase_known;
    g_river_dialog_runtime.snapshot.playback_phase_kind =
        snapshot->playback_phase_kind;
    g_river_dialog_runtime.snapshot.playback_backend_state_kind =
        snapshot->playback_backend_state_kind;
    g_river_dialog_runtime.snapshot.playback_hold_kind =
        snapshot->playback_hold_kind;
    g_river_dialog_runtime.snapshot.playback_terminal_closed =
        snapshot->playback_terminal_closed;
    g_river_dialog_runtime.snapshot.tts_stop_pending = snapshot->tts_stop_pending;
    g_river_dialog_runtime.snapshot.playback_terminal_waiting =
        snapshot->playback_terminal_waiting;
    g_river_dialog_runtime.snapshot.playback_terminal_wait_kind =
        snapshot->playback_terminal_wait_kind;
    g_river_dialog_runtime.snapshot.playback_terminal_state_kind =
        snapshot->playback_terminal_state_kind;
    g_river_dialog_runtime.snapshot.playback_rebuffer_cause_kind =
        snapshot->playback_rebuffer_cause_kind;
    g_river_dialog_runtime.snapshot.playback_start_policy_kind =
        snapshot->playback_start_policy_kind;
    g_river_dialog_runtime.snapshot.turn_accepted = snapshot->turn_accepted;
    g_river_dialog_runtime.snapshot.barge_in_enabled_known =
        snapshot->barge_in_enabled_known;
    g_river_dialog_runtime.snapshot.barge_in_enabled = snapshot->barge_in_enabled;
    river_dialog_runtime_copy_text(g_river_dialog_runtime.snapshot.provider_name,
                                   sizeof(g_river_dialog_runtime.snapshot.provider_name),
                                   snapshot->provider_name);
    river_dialog_runtime_copy_text(g_river_dialog_runtime.snapshot.session_id,
                                   sizeof(g_river_dialog_runtime.snapshot.session_id),
                                   snapshot->session_id);
    river_dialog_runtime_copy_text(g_river_dialog_runtime.snapshot.turn_id,
                                   sizeof(g_river_dialog_runtime.snapshot.turn_id),
                                   snapshot->turn_id);
    river_dialog_runtime_copy_text(g_river_dialog_runtime.snapshot.accept_reason,
                                   sizeof(g_river_dialog_runtime.snapshot.accept_reason),
                                   snapshot->accept_reason);
    river_dialog_runtime_copy_text(
        g_river_dialog_runtime.snapshot.playback_terminal_reason,
        sizeof(g_river_dialog_runtime.snapshot.playback_terminal_reason),
        snapshot->playback_terminal_reason);
    river_dialog_runtime_copy_text(
        g_river_dialog_runtime.snapshot.playback_terminal_wait_reason,
        sizeof(g_river_dialog_runtime.snapshot.playback_terminal_wait_reason),
        snapshot->playback_terminal_wait_reason);
    river_dialog_runtime_copy_text(g_river_dialog_runtime.snapshot.input_state_text,
                                   sizeof(g_river_dialog_runtime.snapshot.input_state_text),
                                   snapshot->input_state);
    river_dialog_runtime_copy_text(g_river_dialog_runtime.snapshot.output_state_text,
                                   sizeof(g_river_dialog_runtime.snapshot.output_state_text),
                                   snapshot->output_state);
    g_river_dialog_runtime.snapshot.playback_start_frames =
        snapshot->playback_start_frames;
    g_river_dialog_runtime.snapshot.playback_prefetch_frames =
        snapshot->playback_prefetch_frames;
    g_river_dialog_runtime.snapshot.playback_start_cautious_history =
        snapshot->playback_start_cautious_history;
    g_river_dialog_runtime.snapshot.input_lane =
        river_dialog_runtime_parse_input_lane(snapshot->input_state);
    g_river_dialog_runtime.snapshot.output_lane =
        river_dialog_runtime_parse_output_lane(snapshot->output_state);
    river_dialog_runtime_refresh_error_recovering_locked();
    if (river_dialog_runtime_cloud_round_active_locked()) {
        g_river_dialog_runtime.snapshot.asr_session_active = true;
    }
    river_dialog_runtime_refresh_playback_locked();
    if (river_dialog_runtime_output_turn_quiesced_locked()) {
        g_river_dialog_runtime.snapshot.tts_interrupt_requested = false;
    }
}

static void river_dialog_runtime_set_wake_admission_pending_locked(bool pending)
{
    g_river_dialog_runtime.snapshot.wake_admission_pending = pending;
}

river_status_t river_dialog_runtime_init(void)
{
    if (g_river_dialog_runtime.initialized) {
        return RIVER_OK;
    }

    memset(&g_river_dialog_runtime, 0, sizeof(g_river_dialog_runtime));
    if (rtos_mutex_create(&g_river_dialog_runtime.lock) != RTK_SUCCESS) {
        return RIVER_ERR_NO_MEMORY;
    }

    g_river_dialog_runtime.initialized = true;
    river_dialog_runtime_apply_local_playback_state_locked(RIVER_PLAYBACK_IDLE);
    g_river_dialog_runtime.snapshot.interaction_state = RIVER_INTERACTION_BOOTING;
    river_dialog_runtime_copy_text(g_river_dialog_runtime.snapshot.reason,
                                   sizeof(g_river_dialog_runtime.snapshot.reason),
                                   "boot_begin");
    (void)river_interaction_state_set(RIVER_INTERACTION_BOOTING, "boot_begin");
    return RIVER_OK;
}

void river_dialog_runtime_mark_boot_ready_with_cloud_state(const char *reason)
{
    river_dialog_runtime_commit_cloud_event(RIVER_DIALOG_RUNTIME_CLOUD_EVENT_BOOT_READY,
                                            NULL,
                                            reason);
}

void river_dialog_runtime_note_wake_confirmed_with_cloud_state(const char *reason)
{
    river_dialog_runtime_commit_cloud_event(RIVER_DIALOG_RUNTIME_CLOUD_EVENT_WAKE_CONFIRMED,
                                            NULL,
                                            reason);
}

void river_dialog_runtime_note_asr_session_started_with_cloud_state(const char *sid,
                                                                    const char *reason)
{
    river_dialog_runtime_commit_cloud_event(
        RIVER_DIALOG_RUNTIME_CLOUD_EVENT_ASR_SESSION_STARTED,
        sid,
        reason);
}

void river_dialog_runtime_note_asr_session_closed_with_cloud_state(const char *sid,
                                                                   const char *reason)
{
    river_dialog_runtime_commit_cloud_event(
        RIVER_DIALOG_RUNTIME_CLOUD_EVENT_ASR_SESSION_CLOSED,
        sid,
        reason);
}

void river_dialog_runtime_note_asr_error_with_cloud_state(const char *sid, const char *reason)
{
    river_dialog_runtime_commit_cloud_event(RIVER_DIALOG_RUNTIME_CLOUD_EVENT_ASR_ERROR,
                                            sid,
                                            reason);
}

void river_dialog_runtime_on_cloud_asr_result(const river_cloud_asr_result_t *result,
                                              void *user_data)
{
    (void)user_data;

    if (result == NULL) {
        return;
    }

    switch (result->type) {
    case RIVER_CLOUD_ASR_EVENT_SESSION_STARTED:
        river_dialog_runtime_note_asr_session_started_with_cloud_state(
            result->sid,
            "asr_session_started");
        break;
    case RIVER_CLOUD_ASR_EVENT_SESSION_CLOSED:
        river_dialog_runtime_note_asr_session_closed_with_cloud_state(
            result->sid,
            "asr_session_closed");
        break;
    case RIVER_CLOUD_ASR_EVENT_ERROR:
        river_dialog_runtime_note_asr_error_with_cloud_state(result->sid, "asr_error");
        break;
    default:
        break;
    }
}

void river_dialog_runtime_note_wake_admission_pending(void)
{
    if (!g_river_dialog_runtime.initialized) {
        return;
    }
    if (!river_dialog_runtime_lock()) {
        return;
    }

    river_dialog_runtime_set_wake_admission_pending_locked(true);
    river_dialog_runtime_unlock();
}

void river_dialog_runtime_clear_wake_admission_pending(void)
{
    if (!g_river_dialog_runtime.initialized) {
        return;
    }
    if (!river_dialog_runtime_lock()) {
        return;
    }

    river_dialog_runtime_set_wake_admission_pending_locked(false);
    river_dialog_runtime_unlock();
}

void river_dialog_runtime_note_tts_interrupt_requested(const char *reason)
{
    if (!river_dialog_runtime_lock()) {
        return;
    }

    if (!g_river_dialog_runtime.snapshot.tts_interrupt_requested) {
        g_river_dialog_runtime.snapshot.tts_interrupt_requested = true;
    }
    if (reason != NULL && reason[0] != '\0') {
        river_dialog_runtime_copy_text(g_river_dialog_runtime.snapshot.reason,
                                       sizeof(g_river_dialog_runtime.snapshot.reason),
                                       reason);
    }
    river_dialog_runtime_unlock();
}

static void river_dialog_runtime_reduce_local_playback_event(
    river_playback_state_t state,
    const river_playback_stream_config_t *config,
    const char *reason)
{
    river_cloud_runtime_snapshot_t cloud_snapshot;
    bool have_cloud_snapshot = river_dialog_runtime_capture_cloud_snapshot(&cloud_snapshot);
    bool should_absorb = false;
    bool prev_playback_active;
    bool prev_playback_recovering;
    bool prev_error_recovering;
    bool managed_recovery = false;
    river_interaction_state_t prev_interaction_state;
    river_interaction_state_t next_interaction_state;
    const char *effective_reason = reason;

    if (!river_dialog_runtime_lock()) {
        return;
    }

    if (river_dialog_runtime_is_dialog_playback_stream_locked(config)) {
        g_river_dialog_runtime.local_playback_stream_owned = true;
        river_dialog_runtime_copy_text(g_river_dialog_runtime.local_playback_stream_name,
                                       sizeof(g_river_dialog_runtime.local_playback_stream_name),
                                       config->stream_name);
        should_absorb = true;
    } else if (river_dialog_runtime_matches_owned_playback_stream_locked(config)) {
        should_absorb = true;
    }

    if (!should_absorb) {
        river_dialog_runtime_unlock();
        return;
    }
    if (state == RIVER_PLAYBACK_IDLE) {
        g_river_dialog_runtime.local_playback_stream_owned = false;
        g_river_dialog_runtime.local_playback_stream_name[0] = '\0';
    }

    prev_playback_active = g_river_dialog_runtime.snapshot.playback_active;
    prev_playback_recovering = g_river_dialog_runtime.snapshot.playback_recovering;
    prev_error_recovering = g_river_dialog_runtime.snapshot.error_recovering;
    prev_interaction_state = g_river_dialog_runtime.snapshot.interaction_state;
    if (have_cloud_snapshot) {
        river_dialog_runtime_apply_cloud_snapshot_locked(&cloud_snapshot);
    }
    river_dialog_runtime_apply_local_playback_state_locked(state);

    if (river_dialog_runtime_local_playback_shadow_drives_truth_locked()) {
        river_dialog_runtime_refresh_playback_locked();
        if (state == RIVER_PLAYBACK_ERROR) {
            managed_recovery = river_dialog_runtime_playback_error_is_managed_recovery_locked();
            if (managed_recovery) {
                g_river_dialog_runtime.local_playback_error_recovering = false;
                effective_reason = "playback_recovering";
            } else {
                g_river_dialog_runtime.local_playback_error_recovering = true;
            }
        } else {
            g_river_dialog_runtime.local_playback_error_recovering = false;
        }
        river_dialog_runtime_refresh_error_recovering_locked();
        if ((state == RIVER_PLAYBACK_IDLE &&
             river_dialog_runtime_output_turn_quiesced_locked()) ||
            (state == RIVER_PLAYBACK_ERROR && !managed_recovery)) {
            g_river_dialog_runtime.snapshot.tts_interrupt_requested = false;
        }
    }
    next_interaction_state = river_dialog_runtime_compute_interaction_state_locked();
    if (river_dialog_runtime_cloud_runtime_available_locked() &&
        prev_playback_active == g_river_dialog_runtime.snapshot.playback_active &&
        prev_playback_recovering == g_river_dialog_runtime.snapshot.playback_recovering &&
        prev_error_recovering == g_river_dialog_runtime.snapshot.error_recovering &&
        prev_interaction_state == next_interaction_state) {
        if (effective_reason != NULL && effective_reason[0] != '\0') {
            river_dialog_runtime_copy_text(g_river_dialog_runtime.snapshot.reason,
                                           sizeof(g_river_dialog_runtime.snapshot.reason),
                                           effective_reason);
        }
        river_dialog_runtime_unlock();
        return;
    }
    river_dialog_runtime_publish_locked(effective_reason != NULL ? effective_reason :
                                                            "playback_state");
    river_dialog_runtime_unlock();
}

void river_dialog_runtime_on_playback_state(river_playback_state_t state,
                                            const river_playback_stream_config_t *config,
                                            void *user_data)
{
    const char *reason;

    (void)user_data;

    reason = "playback_state";
    if (state == RIVER_PLAYBACK_RECOVERING ||
        state == RIVER_PLAYBACK_RESTART_PENDING) {
        reason = "playback_recovering";
    } else if (state == RIVER_PLAYBACK_ERROR) {
        reason = "playback_error";
    }

    river_dialog_runtime_reduce_local_playback_event(state, config, reason);
}

void river_dialog_runtime_on_cloud_state_sync(const char *reason, void *user_data)
{
    (void)user_data;
    river_dialog_runtime_sync_cloud_state(reason);
}

void river_dialog_runtime_sync_cloud_state(const char *reason)
{
    river_cloud_runtime_snapshot_t snapshot;

    if (!river_dialog_runtime_capture_cloud_snapshot(&snapshot)) {
        return;
    }
    if (!river_dialog_runtime_lock()) {
        return;
    }

    river_dialog_runtime_apply_cloud_snapshot_locked(&snapshot);
    river_dialog_runtime_publish_locked(reason != NULL ? reason : "cloud_state_sync");
    river_dialog_runtime_unlock();
}

static const char *river_dialog_runtime_wakeword_block_reason_locked(void)
{
    if (g_river_dialog_runtime.snapshot.wake_admission_pending) {
        return "wake_admission_pending";
    }
    if (g_river_dialog_runtime.snapshot.conversation_window_active) {
        return "conversation_window_active";
    }
    if (g_river_dialog_runtime.snapshot.cloud_local_close_pending) {
        return "cloud_local_close_pending";
    }
    if (g_river_dialog_runtime.snapshot.cloud_listen_stop_pending) {
        return "cloud_listen_stop_pending";
    }
    if (g_river_dialog_runtime.snapshot.interaction_state !=
        RIVER_INTERACTION_WAKE_MONITORING) {
        return river_interaction_state_name(
            g_river_dialog_runtime.snapshot.interaction_state);
    }
    return NULL;
}

const char *river_dialog_runtime_wakeword_detection_block_reason(void)
{
    const char *reason;

    if (!g_river_dialog_runtime.initialized) {
        return "runtime_uninitialized";
    }
    if (!river_dialog_runtime_lock()) {
        return "runtime_busy";
    }

    reason = river_dialog_runtime_wakeword_block_reason_locked();
    river_dialog_runtime_unlock();
    return reason;
}

bool river_dialog_runtime_allows_wakeword_detection(void)
{
    return river_dialog_runtime_wakeword_detection_block_reason() == NULL;
}

const char *river_dialog_runtime_wakeword_admission_block_reason(void)
{
    return river_dialog_runtime_wakeword_detection_block_reason();
}

bool river_dialog_runtime_allows_barge_in_interrupt(void)
{
    bool allowed = false;

    if (!g_river_dialog_runtime.initialized) {
        return false;
    }
    if (!river_dialog_runtime_lock()) {
        return false;
    }

    allowed = g_river_dialog_runtime.snapshot.asr_session_active &&
              river_dialog_runtime_output_turn_engaged_locked() &&
              !river_dialog_runtime_tts_interrupt_inflight_locked() &&
              !g_river_dialog_runtime.snapshot.playback_terminal_closed &&
              (g_river_dialog_runtime.snapshot.interaction_state ==
                   RIVER_INTERACTION_SPEAKING ||
               g_river_dialog_runtime.snapshot.interaction_state ==
                   RIVER_INTERACTION_BARGE_IN_LISTENING);

    river_dialog_runtime_unlock();
    return allowed;
}

river_interaction_state_t river_dialog_runtime_interaction_state(void)
{
    return g_river_dialog_runtime.snapshot.interaction_state;
}

river_status_t river_dialog_runtime_get_snapshot(river_dialog_runtime_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return RIVER_ERR_ARG;
    }
    if (!g_river_dialog_runtime.initialized) {
        return RIVER_ERR_NOT_FOUND;
    }
    if (!river_dialog_runtime_lock()) {
        return RIVER_ERR_BUSY;
    }

    *snapshot = g_river_dialog_runtime.snapshot;
    river_dialog_runtime_unlock();
    return RIVER_OK;
}

void river_dialog_runtime_dump_status(void)
{
    river_dialog_runtime_snapshot_t snapshot;
    bool playback_local_active;
    bool playback_local_recovering;
    river_playback_state_t playback_state;

    if (!g_river_dialog_runtime.initialized) {
        return;
    }
    if (!river_dialog_runtime_lock()) {
        return;
    }

    snapshot = g_river_dialog_runtime.snapshot;
    playback_state = g_river_dialog_runtime.playback_state;
    playback_local_active = river_dialog_runtime_local_playback_state_active_locked();
    playback_local_recovering =
        river_dialog_runtime_local_playback_state_recovering_locked();
    river_dialog_runtime_unlock();

    RIVER_LOGI("dialog_runtime interaction=%s input_lane=%s output_lane=%s asr=%s wake_admission=%s playback=%s local_playback=%s/%s cloud_playback=%s/%s owner=%s phase_known=%s backend_state=%s hold=%s start_gate=%s/%lu prefetch=%lu cautious=%s lane=%s turn=%s recovering=%s/%s local_recovering=%s terminal_closed=%s terminal=%s/%s terminal_wait=%s/%s interrupt=%s stop_pending=%s local_close=%s/%lu window=%s/%lu wake_confirmed=%s error=%s/%s turn_id=%s accept_reason=%s reason=%s transitions=%lu",
               river_interaction_state_name(snapshot.interaction_state),
               river_dialog_input_lane_name(snapshot.input_lane),
               river_dialog_output_lane_name(snapshot.output_lane),
               snapshot.asr_session_active ? "yes" : "no",
               snapshot.wake_admission_pending ? "yes" : "no",
               snapshot.playback_active ? "yes" : "no",
               playback_local_active ? "yes" : "no",
               river_dialog_runtime_local_playback_state_name(playback_state),
               snapshot.playback_cloud_active ? "yes" : "no",
               snapshot.playback_phase_known ?
                   river_cloud_playback_phase_name(snapshot.playback_phase_kind) :
                   "-",
               river_dialog_playback_owner_kind_name(snapshot.playback_owner_kind),
               snapshot.playback_phase_known ? "yes" : "no",
               river_cloud_playback_backend_state_name(
                   snapshot.playback_backend_state_kind),
               river_cloud_playback_hold_kind_name(snapshot.playback_hold_kind),
               river_cloud_playback_start_policy_name(snapshot.playback_start_policy_kind),
               (unsigned long)snapshot.playback_start_frames,
               (unsigned long)snapshot.playback_prefetch_frames,
               snapshot.playback_start_cautious_history ? "yes" : "no",
               snapshot.playback_lane_engaged ? "yes" : "no",
               snapshot.playback_turn_active ? "yes" : "no",
               snapshot.playback_recovering ? "yes" : "no",
               river_cloud_playback_rebuffer_cause_name(
                   snapshot.playback_rebuffer_cause_kind),
               playback_local_recovering ? "yes" : "no",
               snapshot.playback_terminal_closed ? "yes" : "no",
               snapshot.playback_terminal_state_kind !=
                       RIVER_CLOUD_PLAYBACK_TERMINAL_STATE_NONE ?
                   river_cloud_playback_terminal_state_name(
                       snapshot.playback_terminal_state_kind) :
                   "-",
               snapshot.playback_terminal_reason[0] != '\0' ?
                   snapshot.playback_terminal_reason :
                   "-",
               snapshot.playback_terminal_waiting ? "yes" : "no",
               snapshot.playback_terminal_wait_reason[0] != '\0' ?
                   snapshot.playback_terminal_wait_reason :
                   "-",
               snapshot.tts_interrupt_requested ? "yes" : "no",
               snapshot.cloud_listen_stop_pending ? "yes" : "no",
               snapshot.cloud_local_close_pending ? "yes" : "no",
               (unsigned long)snapshot.local_close_remaining_ms,
               snapshot.conversation_window_active ? "open" : "closed",
               (unsigned long)snapshot.conversation_window_remaining_ms,
               snapshot.wake_confirmed ? "yes" : "no",
               snapshot.error_recovering ? "yes" : "no",
               river_dialog_error_kind_name(snapshot.error_kind),
               snapshot.turn_id[0] != '\0' ? snapshot.turn_id : "-",
               snapshot.accept_reason[0] != '\0' ? snapshot.accept_reason : "-",
               snapshot.reason[0] != '\0' ? snapshot.reason : "-",
               (unsigned long)snapshot.transition_count);
}
