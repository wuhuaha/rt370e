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
    bool local_playback_stream_owned;
    rtos_mutex_t lock;
    river_dialog_runtime_snapshot_t snapshot;
} river_dialog_runtime_context_t;

static river_dialog_runtime_context_t g_river_dialog_runtime;

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

static bool river_dialog_runtime_is_dialog_playback_stream(
    const river_playback_stream_config_t *config)
{
    return config != NULL && config->priority == RIVER_PLAYBACK_PRIO_TTS;
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

static bool river_dialog_runtime_playback_terminal_closed_locked(void)
{
    return g_river_dialog_runtime.snapshot.playback_terminal_state[0] != '\0';
}

static bool river_dialog_runtime_playback_phase_known_locked(void)
{
    return g_river_dialog_runtime.snapshot.playback_phase[0] != '\0';
}

static bool river_dialog_runtime_playback_phase_equals_locked(const char *phase)
{
    return phase != NULL && phase[0] != '\0' &&
           strcmp(g_river_dialog_runtime.snapshot.playback_phase, phase) == 0;
}

static bool river_dialog_runtime_playback_phase_engaged_locked(void)
{
    return river_dialog_runtime_playback_phase_known_locked() &&
           !river_dialog_runtime_playback_phase_equals_locked("idle");
}

static bool river_dialog_runtime_playback_phase_output_active_locked(void)
{
    return river_dialog_runtime_playback_phase_equals_locked("playing") ||
           river_dialog_runtime_playback_phase_equals_locked("draining");
}

static bool river_dialog_runtime_playback_phase_recovering_locked(void)
{
    return river_dialog_runtime_playback_phase_equals_locked("rebuffering");
}

static bool river_dialog_runtime_compute_playback_recovering_locked(void)
{
    if (river_dialog_runtime_playback_phase_recovering_locked()) {
        return true;
    }

    if (river_dialog_runtime_playback_phase_known_locked()) {
        return g_river_dialog_runtime.snapshot.playback_rebuffer_pending;
    }

    return g_river_dialog_runtime.snapshot.playback_state == RIVER_PLAYBACK_RECOVERING ||
           g_river_dialog_runtime.snapshot.playback_state ==
               RIVER_PLAYBACK_RESTART_PENDING ||
           g_river_dialog_runtime.snapshot.playback_rebuffer_pending;
}

static bool river_dialog_runtime_compute_playback_active_locked(void)
{
    bool service_active =
        river_playback_service_state_active(g_river_dialog_runtime.snapshot.playback_state);
    bool cloud_playback_active = g_river_dialog_runtime.snapshot.playback_cloud_active;
    bool lane_engaged = g_river_dialog_runtime.snapshot.playback_lane_engaged;
    bool recovering = g_river_dialog_runtime.snapshot.playback_recovering;
    bool phase_known = river_dialog_runtime_playback_phase_known_locked();
    bool phase_output_active = river_dialog_runtime_playback_phase_output_active_locked();
    bool phase_engaged = river_dialog_runtime_playback_phase_engaged_locked();

    if (river_dialog_runtime_playback_terminal_closed_locked()) {
        return false;
    }

    if (phase_output_active || phase_engaged || recovering) {
        return true;
    }

    if (!service_active && !cloud_playback_active && !lane_engaged && !recovering &&
        g_river_dialog_runtime.snapshot.playback_terminal_waiting) {
        return false;
    }

    if (phase_known) {
        return false;
    }

    return service_active || cloud_playback_active || lane_engaged || recovering;
}

static void river_dialog_runtime_refresh_playback_locked(void)
{
    g_river_dialog_runtime.snapshot.playback_recovering =
        river_dialog_runtime_compute_playback_recovering_locked();
    g_river_dialog_runtime.snapshot.playback_active =
        river_dialog_runtime_compute_playback_active_locked();
}

static bool river_dialog_runtime_output_speaking_effective_locked(void)
{
    const river_dialog_runtime_snapshot_t *snapshot = &g_river_dialog_runtime.snapshot;

    if (snapshot->output_lane != RIVER_DIALOG_OUTPUT_LANE_SPEAKING) {
        return false;
    }
    if (river_dialog_runtime_playback_terminal_closed_locked()) {
        return false;
    }
    if (snapshot->playback_terminal_waiting &&
        !snapshot->playback_active &&
        !snapshot->playback_recovering) {
        return false;
    }

    return true;
}

static river_interaction_state_t river_dialog_runtime_compute_interaction_state_locked(void)
{
    const river_dialog_runtime_snapshot_t *snapshot = &g_river_dialog_runtime.snapshot;
    bool speaking_output = river_dialog_runtime_output_speaking_effective_locked();
    bool playback_engaged = snapshot->playback_active || speaking_output;

    if (!snapshot->boot_ready) {
        return RIVER_INTERACTION_BOOTING;
    }
    if (snapshot->error_recovering) {
        return RIVER_INTERACTION_ERROR_RECOVERING;
    }
    if (playback_engaged) {
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

static void river_dialog_runtime_apply_cloud_snapshot_locked(
    const river_cloud_runtime_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    g_river_dialog_runtime.snapshot.conversation_window_active =
        snapshot->conversation_window_active;
    g_river_dialog_runtime.snapshot.cloud_listening = snapshot->listening;
    g_river_dialog_runtime.snapshot.cloud_stream_active = snapshot->stream_active;
    g_river_dialog_runtime.snapshot.playback_cloud_active = snapshot->playback_active;
    g_river_dialog_runtime.snapshot.playback_lane_engaged = snapshot->playback_lane_engaged;
    g_river_dialog_runtime.snapshot.playback_rebuffer_pending =
        snapshot->playback_rebuffer_pending;
    g_river_dialog_runtime.snapshot.tts_stop_pending = snapshot->tts_stop_pending;
    g_river_dialog_runtime.snapshot.playback_terminal_waiting =
        snapshot->playback_terminal_waiting;
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
    river_dialog_runtime_copy_text(g_river_dialog_runtime.snapshot.playback_phase,
                                   sizeof(g_river_dialog_runtime.snapshot.playback_phase),
                                   snapshot->playback_phase);
    river_dialog_runtime_copy_text(
        g_river_dialog_runtime.snapshot.playback_rebuffer_cause,
        sizeof(g_river_dialog_runtime.snapshot.playback_rebuffer_cause),
        snapshot->playback_rebuffer_cause);
    river_dialog_runtime_copy_text(g_river_dialog_runtime.snapshot.playback_terminal_state,
                                   sizeof(g_river_dialog_runtime.snapshot.playback_terminal_state),
                                   snapshot->playback_terminal_state);
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
    g_river_dialog_runtime.snapshot.input_lane =
        river_dialog_runtime_parse_input_lane(snapshot->input_state);
    g_river_dialog_runtime.snapshot.output_lane =
        river_dialog_runtime_parse_output_lane(snapshot->output_state);
    river_dialog_runtime_refresh_playback_locked();
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
    g_river_dialog_runtime.snapshot.playback_state = RIVER_PLAYBACK_IDLE;
    g_river_dialog_runtime.snapshot.interaction_state = RIVER_INTERACTION_BOOTING;
    river_dialog_runtime_copy_text(g_river_dialog_runtime.snapshot.reason,
                                   sizeof(g_river_dialog_runtime.snapshot.reason),
                                   "boot_begin");
    (void)river_interaction_state_set(RIVER_INTERACTION_BOOTING, "boot_begin");
    return RIVER_OK;
}

void river_dialog_runtime_mark_boot_ready_with_cloud_state(const char *reason)
{
    river_cloud_runtime_snapshot_t snapshot;
    bool have_snapshot = river_dialog_runtime_capture_cloud_snapshot(&snapshot);

    if (!river_dialog_runtime_lock()) {
        return;
    }

    g_river_dialog_runtime.snapshot.boot_ready = true;
    g_river_dialog_runtime.snapshot.wake_confirmed = false;
    if (have_snapshot) {
        river_dialog_runtime_apply_cloud_snapshot_locked(&snapshot);
    }
    river_dialog_runtime_publish_locked(reason != NULL ? reason : "boot_ready");
    river_dialog_runtime_unlock();
}

void river_dialog_runtime_note_wake_confirmed_with_cloud_state(const char *reason)
{
    river_cloud_runtime_snapshot_t snapshot;
    bool have_snapshot = river_dialog_runtime_capture_cloud_snapshot(&snapshot);
    if (!river_dialog_runtime_lock()) {
        return;
    }

    g_river_dialog_runtime.snapshot.wake_confirmed = true;
    if (have_snapshot) {
        river_dialog_runtime_apply_cloud_snapshot_locked(&snapshot);
    }
    river_dialog_runtime_publish_locked(reason != NULL ? reason : "wakeword_detected");
    river_dialog_runtime_unlock();
}

void river_dialog_runtime_note_asr_session_started_with_cloud_state(const char *sid,
                                                                    const char *reason)
{
    river_cloud_runtime_snapshot_t snapshot;
    bool have_snapshot = river_dialog_runtime_capture_cloud_snapshot(&snapshot);

    if (!river_dialog_runtime_lock()) {
        return;
    }

    g_river_dialog_runtime.snapshot.asr_session_active = true;
    g_river_dialog_runtime.snapshot.error_recovering = false;
    g_river_dialog_runtime.snapshot.wake_confirmed = false;
    if (sid != NULL && sid[0] != '\0') {
        river_dialog_runtime_copy_text(g_river_dialog_runtime.snapshot.session_id,
                                       sizeof(g_river_dialog_runtime.snapshot.session_id),
                                       sid);
    }
    if (have_snapshot) {
        river_dialog_runtime_apply_cloud_snapshot_locked(&snapshot);
    }
    river_dialog_runtime_publish_locked(reason != NULL ? reason : "asr_session_started");
    river_dialog_runtime_unlock();
}

void river_dialog_runtime_note_asr_session_closed_with_cloud_state(const char *sid,
                                                                   const char *reason)
{
    river_cloud_runtime_snapshot_t snapshot;
    bool have_snapshot = river_dialog_runtime_capture_cloud_snapshot(&snapshot);

    if (!river_dialog_runtime_lock()) {
        return;
    }

    g_river_dialog_runtime.snapshot.asr_session_active = false;
    g_river_dialog_runtime.snapshot.wake_confirmed = false;
    if (sid != NULL && sid[0] != '\0') {
        river_dialog_runtime_copy_text(g_river_dialog_runtime.snapshot.session_id,
                                       sizeof(g_river_dialog_runtime.snapshot.session_id),
                                       sid);
    }
    if (have_snapshot) {
        river_dialog_runtime_apply_cloud_snapshot_locked(&snapshot);
    }
    river_dialog_runtime_publish_locked(reason != NULL ? reason : "asr_session_closed");
    river_dialog_runtime_unlock();
}

void river_dialog_runtime_note_asr_error_with_cloud_state(const char *sid, const char *reason)
{
    river_cloud_runtime_snapshot_t snapshot;
    bool have_snapshot = river_dialog_runtime_capture_cloud_snapshot(&snapshot);

    if (!river_dialog_runtime_lock()) {
        return;
    }

    g_river_dialog_runtime.snapshot.asr_session_active = false;
    g_river_dialog_runtime.snapshot.error_recovering = true;
    g_river_dialog_runtime.snapshot.wake_confirmed = false;
    if (sid != NULL && sid[0] != '\0') {
        river_dialog_runtime_copy_text(g_river_dialog_runtime.snapshot.session_id,
                                       sizeof(g_river_dialog_runtime.snapshot.session_id),
                                       sid);
    }
    if (have_snapshot) {
        river_dialog_runtime_apply_cloud_snapshot_locked(&snapshot);
    }
    river_dialog_runtime_publish_locked(reason != NULL ? reason : "asr_error");
    river_dialog_runtime_unlock();
}

void river_dialog_runtime_note_playback_state(river_playback_state_t state, const char *reason)
{
    bool prev_playback_active;
    bool prev_playback_recovering;
    bool prev_error_recovering;
    river_interaction_state_t prev_interaction_state;
    river_interaction_state_t next_interaction_state;

    if (!river_dialog_runtime_lock()) {
        return;
    }

    prev_playback_active = g_river_dialog_runtime.snapshot.playback_active;
    prev_playback_recovering = g_river_dialog_runtime.snapshot.playback_recovering;
    prev_error_recovering = g_river_dialog_runtime.snapshot.error_recovering;
    prev_interaction_state = g_river_dialog_runtime.snapshot.interaction_state;
    g_river_dialog_runtime.snapshot.playback_state = state;
    river_dialog_runtime_refresh_playback_locked();
    if (state != RIVER_PLAYBACK_ERROR) {
        g_river_dialog_runtime.snapshot.error_recovering = false;
    }
    next_interaction_state = river_dialog_runtime_compute_interaction_state_locked();
    if (river_dialog_runtime_playback_phase_known_locked() &&
        prev_playback_active == g_river_dialog_runtime.snapshot.playback_active &&
        prev_playback_recovering == g_river_dialog_runtime.snapshot.playback_recovering &&
        prev_error_recovering == g_river_dialog_runtime.snapshot.error_recovering &&
        prev_interaction_state == next_interaction_state) {
        if (reason != NULL && reason[0] != '\0') {
            river_dialog_runtime_copy_text(g_river_dialog_runtime.snapshot.reason,
                                           sizeof(g_river_dialog_runtime.snapshot.reason),
                                           reason);
        }
        river_dialog_runtime_unlock();
        return;
    }
    river_dialog_runtime_publish_locked(reason != NULL ? reason : "playback_state");
    river_dialog_runtime_unlock();
}

void river_dialog_runtime_on_playback_state(river_playback_state_t state,
                                            const river_playback_stream_config_t *config,
                                            void *user_data)
{
    bool should_absorb = false;
    const char *reason;

    (void)user_data;

    if (!river_dialog_runtime_lock()) {
        return;
    }

    if (river_dialog_runtime_is_dialog_playback_stream(config)) {
        g_river_dialog_runtime.local_playback_stream_owned = true;
        should_absorb = true;
    } else if (g_river_dialog_runtime.local_playback_stream_owned) {
        should_absorb = true;
    }

    if (state == RIVER_PLAYBACK_IDLE) {
        g_river_dialog_runtime.local_playback_stream_owned = false;
    }
    river_dialog_runtime_unlock();

    if (!should_absorb) {
        return;
    }

    reason = "playback_state";
    if (state == RIVER_PLAYBACK_RECOVERING ||
        state == RIVER_PLAYBACK_RESTART_PENDING) {
        reason = "playback_recovering";
    } else if (state == RIVER_PLAYBACK_ERROR) {
        reason = "playback_error";
    }

    river_dialog_runtime_note_playback_state(state, reason);
    if (state == RIVER_PLAYBACK_ERROR) {
        river_dialog_runtime_note_error("playback_error");
    }
}

void river_dialog_runtime_note_error(const char *reason)
{
    if (!river_dialog_runtime_lock()) {
        return;
    }

    g_river_dialog_runtime.snapshot.error_recovering = true;
    river_dialog_runtime_publish_locked(reason != NULL ? reason : "runtime_error");
    river_dialog_runtime_unlock();
}

void river_dialog_runtime_clear_error(const char *reason)
{
    if (!river_dialog_runtime_lock()) {
        return;
    }

    g_river_dialog_runtime.snapshot.error_recovering = false;
    river_dialog_runtime_publish_locked(reason != NULL ? reason : "error_cleared");
    river_dialog_runtime_unlock();
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

    if (river_dialog_runtime_get_snapshot(&snapshot) != RIVER_OK) {
        return;
    }

    RIVER_LOGI("dialog_runtime interaction=%s input_lane=%s output_lane=%s asr=%s playback=%s cloud_playback=%s/%s lane=%s recovering=%s/%s terminal=%s/%s tail_wait=%s/%s window=%s wake_confirmed=%s error=%s turn_id=%s accept_reason=%s reason=%s transitions=%lu",
               river_interaction_state_name(snapshot.interaction_state),
               river_dialog_input_lane_name(snapshot.input_lane),
               river_dialog_output_lane_name(snapshot.output_lane),
               snapshot.asr_session_active ? "yes" : "no",
               snapshot.playback_active ? "yes" : "no",
               snapshot.playback_cloud_active ? "yes" : "no",
               snapshot.playback_phase[0] != '\0' ? snapshot.playback_phase : "-",
               snapshot.playback_lane_engaged ? "yes" : "no",
               snapshot.playback_recovering ? "yes" : "no",
               snapshot.playback_rebuffer_cause[0] != '\0' ?
                   snapshot.playback_rebuffer_cause :
                   "-",
               snapshot.playback_terminal_state[0] != '\0' ?
                   snapshot.playback_terminal_state :
                   "-",
               snapshot.playback_terminal_reason[0] != '\0' ?
                   snapshot.playback_terminal_reason :
                   "-",
               snapshot.playback_terminal_waiting ? "yes" : "no",
               snapshot.playback_terminal_wait_reason[0] != '\0' ?
                   snapshot.playback_terminal_wait_reason :
                   "-",
               snapshot.conversation_window_active ? "open" : "closed",
               snapshot.wake_confirmed ? "yes" : "no",
               snapshot.error_recovering ? "yes" : "no",
               snapshot.turn_id[0] != '\0' ? snapshot.turn_id : "-",
               snapshot.accept_reason[0] != '\0' ? snapshot.accept_reason : "-",
               snapshot.reason[0] != '\0' ? snapshot.reason : "-",
               (unsigned long)snapshot.transition_count);
}
