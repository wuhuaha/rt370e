/* 对话运行时真相源：统一吸收 cloud/playback/ASR 事实并派生交互状态。 */
#include <string.h>

#include "basic_types.h"
#include "os_wrapper.h"

#include "river/river_dialog_runtime.h"
#include "river/river_log.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.dialog"

typedef struct {
    bool cloud_playback_active;
    bool lane_engaged;
    bool turn_active;
    bool rebuffer_pending;
    river_cloud_playback_backend_state_t backend_state_kind;
    river_cloud_playback_supply_kind_t supply_kind;
    river_cloud_playback_hold_kind_t hold_kind;
    bool terminal_closed;
    bool tts_stop_pending;
    bool terminal_waiting;
    river_cloud_playback_terminal_wait_kind_t terminal_wait_kind;
} river_dialog_runtime_cloud_playback_facts_t;

typedef struct {
    bool phase_known;
    river_cloud_playback_phase_t phase_kind;
    river_cloud_playback_terminal_state_t terminal_state_kind;
    river_cloud_playback_rebuffer_cause_t rebuffer_cause_kind;
    river_cloud_playback_recovery_path_t recovery_path_kind;
    river_cloud_playback_recovery_outcome_t recovery_outcome_kind;
    river_cloud_playback_start_policy_t start_policy_kind;
    char playback_terminal_reason[RIVER_CLOUD_RUNTIME_REASON_MAX];
    char playback_terminal_wait_reason[RIVER_CLOUD_RUNTIME_REASON_MAX];
    uint32_t start_frames;
    uint32_t prefetch_frames;
    bool start_cautious_history;
} river_dialog_runtime_cloud_playback_observe_t;

typedef struct {
    bool conversation_window_active;
    uint32_t conversation_window_remaining_ms;
    bool cloud_listening;
    bool cloud_stream_active;
    bool cloud_listen_stop_pending;
    bool cloud_local_close_pending;
    uint32_t local_close_remaining_ms;
} river_dialog_runtime_cloud_round_facts_t;

typedef struct {
    river_dialog_input_lane_t input_lane;
    river_dialog_output_lane_t output_lane;
} river_dialog_runtime_cloud_io_facts_t;

typedef struct {
    char input_state_text[RIVER_CLOUD_RUNTIME_STATE_MAX];
    char output_state_text[RIVER_CLOUD_RUNTIME_STATE_MAX];
} river_dialog_runtime_cloud_io_observe_t;

typedef struct {
    bool turn_accepted;
    bool barge_in_enabled_known;
    bool barge_in_enabled;
} river_dialog_runtime_cloud_session_facts_t;

typedef struct {
    char provider_name[RIVER_CLOUD_RUNTIME_PROVIDER_MAX];
    char session_id[RIVER_CLOUD_RUNTIME_ID_MAX];
    char turn_id[RIVER_CLOUD_RUNTIME_ID_MAX];
    char accept_reason[RIVER_CLOUD_RUNTIME_REASON_MAX];
} river_dialog_runtime_cloud_session_observe_t;

typedef struct {
    bool boot_ready;
    bool wake_confirmed;
    bool asr_session_active;
    bool wake_admission_pending;
    bool tts_interrupt_requested;
} river_dialog_runtime_control_facts_t;

typedef struct {
    bool error_recovering;
    river_dialog_error_kind_t error_kind;
    river_dialog_playback_owner_kind_t playback_owner_kind;
    bool playback_active;
    bool playback_recovering;
    river_interaction_state_t interaction_state;
    uint32_t transition_count;
    char reason[48];
} river_dialog_runtime_derived_facts_t;

typedef struct {
    bool initialized;
    bool cloud_runtime_available;
    bool local_playback_stream_owned;
    bool asr_error_recovering;
    bool local_playback_error_recovering;
    char local_playback_stream_name[32];
    river_playback_state_t playback_state;
    river_dialog_runtime_cloud_round_facts_t cloud_round_facts;
    river_dialog_runtime_cloud_io_facts_t cloud_io_facts;
    river_dialog_runtime_cloud_io_observe_t cloud_io_observe;
    river_dialog_runtime_cloud_session_facts_t cloud_session_facts;
    river_dialog_runtime_cloud_session_observe_t cloud_session_observe;
    river_dialog_runtime_cloud_playback_facts_t cloud_playback_facts;
    river_dialog_runtime_cloud_playback_observe_t cloud_playback_observe;
    river_dialog_runtime_control_facts_t control_facts;
    river_dialog_runtime_derived_facts_t derived_facts;
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

typedef enum {
    RIVER_DIALOG_RUNTIME_COMMIT_POLICY_PUBLISH_ALWAYS = 0,
    RIVER_DIALOG_RUNTIME_COMMIT_POLICY_SKIP_IF_CLOUD_DIALOG_STABLE
} river_dialog_runtime_commit_policy_t;

typedef struct {
    bool cloud_runtime_available;
    bool playback_active;
    bool playback_recovering;
    bool error_recovering;
    river_interaction_state_t interaction_state;
} river_dialog_runtime_commit_checkpoint_t;

typedef struct {
    bool runtime_available;
    river_dialog_runtime_cloud_round_facts_t round_facts;
    river_dialog_runtime_cloud_io_facts_t io_facts;
    river_dialog_runtime_cloud_io_observe_t io_observe;
    river_dialog_runtime_cloud_session_facts_t session_facts;
    river_dialog_runtime_cloud_session_observe_t session_observe;
    river_dialog_runtime_cloud_playback_facts_t playback_facts;
} river_dialog_runtime_cloud_import_t;

typedef struct {
    bool config_present;
    river_playback_state_t state;
    river_playback_priority_t priority;
    char stream_name[32];
} river_dialog_runtime_local_playback_import_t;

typedef struct {
    bool has_cloud_import;
    river_dialog_runtime_cloud_import_t cloud_import;
    bool has_cloud_playback_observe;
    river_dialog_runtime_cloud_playback_observe_t cloud_playback_observe;
    bool has_cloud_event;
    river_dialog_runtime_cloud_event_t cloud_event;
    char session_id[RIVER_CLOUD_RUNTIME_ID_MAX];
    bool has_local_playback_import;
    river_dialog_runtime_local_playback_import_t local_playback_import;
    river_dialog_runtime_commit_policy_t commit_policy;
    const char *reason;
} river_dialog_runtime_ingress_t;

static river_dialog_runtime_context_t g_river_dialog_runtime;

static void river_dialog_runtime_export_control_facts_to_snapshot_locked(void);
static void river_dialog_runtime_export_derived_facts_to_snapshot_locked(void);
static void river_dialog_runtime_import_cloud_snapshot_locked(
    const river_dialog_runtime_cloud_import_t *cloud_import);
static void river_dialog_runtime_import_cloud_playback_observe_locked(
    const river_dialog_runtime_cloud_playback_observe_t *cloud_playback_observe);
static void river_dialog_runtime_publish_locked(const char *reason);
static const char *river_dialog_runtime_wakeword_block_reason_locked(void);
static void river_dialog_runtime_set_wake_admission_pending_locked(bool pending);
static void river_dialog_runtime_refresh_error_recovering_locked(void);
static void river_dialog_runtime_reconcile_facts_locked(void);
static bool river_dialog_runtime_cloud_runtime_available_locked(void);
static river_dialog_input_lane_t river_dialog_runtime_parse_input_lane(
    const char *state);
static river_dialog_output_lane_t river_dialog_runtime_parse_output_lane(
    const char *state);
static river_interaction_state_t river_dialog_runtime_compute_interaction_state_locked(void);
static void river_dialog_runtime_finalize_commit_locked(
    const river_dialog_runtime_commit_checkpoint_t *before,
    river_dialog_runtime_commit_policy_t policy,
    const char *reason);
static void river_dialog_runtime_commit_ingress(
    const river_dialog_runtime_ingress_t *ingress);

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

    g_river_dialog_runtime.derived_facts.error_recovering =
        asr_error || local_playback_error;
    if (asr_error && local_playback_error) {
        g_river_dialog_runtime.derived_facts.error_kind = RIVER_DIALOG_ERROR_KIND_MIXED;
    } else if (asr_error) {
        g_river_dialog_runtime.derived_facts.error_kind = RIVER_DIALOG_ERROR_KIND_ASR;
    } else if (local_playback_error) {
        g_river_dialog_runtime.derived_facts.error_kind =
            RIVER_DIALOG_ERROR_KIND_LOCAL_PLAYBACK;
    } else {
        g_river_dialog_runtime.derived_facts.error_kind = RIVER_DIALOG_ERROR_KIND_NONE;
    }
    river_dialog_runtime_export_derived_facts_to_snapshot_locked();
}

static bool river_dialog_runtime_is_dialog_playback_stream_locked(
    const river_dialog_runtime_local_playback_import_t *local_playback_import)
{
    return local_playback_import != NULL &&
           local_playback_import->config_present &&
           local_playback_import->priority == RIVER_PLAYBACK_PRIO_TTS;
}

static bool river_dialog_runtime_matches_owned_playback_stream_locked(
    const river_dialog_runtime_local_playback_import_t *local_playback_import)
{
    if (!g_river_dialog_runtime.local_playback_stream_owned) {
        return false;
    }
    if (local_playback_import == NULL || !local_playback_import->config_present ||
        local_playback_import->stream_name[0] == '\0') {
        return true;
    }
    if (g_river_dialog_runtime.local_playback_stream_name[0] == '\0') {
        return true;
    }
    return strcmp(g_river_dialog_runtime.local_playback_stream_name,
                  local_playback_import->stream_name) == 0;
}

static bool river_dialog_runtime_capture_cloud_snapshot(
    river_dialog_runtime_cloud_import_t *cloud_import,
    river_dialog_runtime_cloud_playback_observe_t *cloud_playback_observe)
{
    river_cloud_runtime_snapshot_t snapshot;

    if (cloud_import == NULL || cloud_playback_observe == NULL) {
        return false;
    }

    memset(cloud_import, 0, sizeof(*cloud_import));
    memset(cloud_playback_observe, 0, sizeof(*cloud_playback_observe));
    memset(&snapshot, 0, sizeof(snapshot));
    if (river_cloud_adapter_get_runtime_snapshot(&snapshot) != RIVER_OK) {
        return false;
    }

    cloud_import->runtime_available = snapshot.available;
    cloud_import->round_facts.conversation_window_active =
        snapshot.conversation_window_active;
    cloud_import->round_facts.conversation_window_remaining_ms =
        snapshot.conversation_window_remaining_ms;
    cloud_import->round_facts.cloud_listening = snapshot.listening;
    cloud_import->round_facts.cloud_stream_active = snapshot.stream_active;
    cloud_import->round_facts.cloud_listen_stop_pending =
        snapshot.listen_stop_pending;
    cloud_import->round_facts.cloud_local_close_pending =
        snapshot.local_close_pending;
    cloud_import->round_facts.local_close_remaining_ms =
        snapshot.local_close_remaining_ms;
    cloud_import->playback_facts.cloud_playback_active = snapshot.playback_active;
    cloud_import->playback_facts.lane_engaged = snapshot.playback_lane_engaged;
    cloud_import->playback_facts.turn_active = snapshot.playback_turn_active;
    cloud_import->playback_facts.rebuffer_pending =
        snapshot.playback_rebuffer_pending;
    cloud_import->playback_facts.backend_state_kind =
        snapshot.playback_backend_state_kind;
    cloud_import->playback_facts.supply_kind = snapshot.playback_supply_kind;
    cloud_import->playback_facts.hold_kind = snapshot.playback_hold_kind;
    cloud_import->playback_facts.terminal_closed =
        snapshot.playback_terminal_closed;
    cloud_import->playback_facts.tts_stop_pending = snapshot.tts_stop_pending;
    cloud_import->playback_facts.terminal_waiting =
        snapshot.playback_terminal_waiting;
    cloud_import->playback_facts.terminal_wait_kind =
        snapshot.playback_terminal_wait_kind;
    cloud_import->session_facts.turn_accepted = snapshot.turn_accepted;
    cloud_import->session_facts.barge_in_enabled_known =
        snapshot.barge_in_enabled_known;
    cloud_import->session_facts.barge_in_enabled = snapshot.barge_in_enabled;
    river_dialog_runtime_copy_text(cloud_import->session_observe.provider_name,
                                   sizeof(cloud_import->session_observe.provider_name),
                                   snapshot.provider_name);
    river_dialog_runtime_copy_text(cloud_import->session_observe.session_id,
                                   sizeof(cloud_import->session_observe.session_id),
                                   snapshot.session_id);
    river_dialog_runtime_copy_text(cloud_import->session_observe.turn_id,
                                   sizeof(cloud_import->session_observe.turn_id),
                                   snapshot.turn_id);
    river_dialog_runtime_copy_text(cloud_import->session_observe.accept_reason,
                                   sizeof(cloud_import->session_observe.accept_reason),
                                   snapshot.accept_reason);
    river_dialog_runtime_copy_text(cloud_import->io_observe.input_state_text,
                                   sizeof(cloud_import->io_observe.input_state_text),
                                   snapshot.input_state);
    river_dialog_runtime_copy_text(cloud_import->io_observe.output_state_text,
                                   sizeof(cloud_import->io_observe.output_state_text),
                                   snapshot.output_state);
    cloud_import->io_facts.input_lane =
        river_dialog_runtime_parse_input_lane(snapshot.input_state);
    cloud_import->io_facts.output_lane =
        river_dialog_runtime_parse_output_lane(snapshot.output_state);
    cloud_playback_observe->phase_known = snapshot.playback_phase_known;
    cloud_playback_observe->phase_kind = snapshot.playback_phase_kind;
    cloud_playback_observe->terminal_state_kind =
        snapshot.playback_terminal_state_kind;
    cloud_playback_observe->rebuffer_cause_kind =
        snapshot.playback_rebuffer_cause_kind;
    cloud_playback_observe->recovery_path_kind =
        snapshot.playback_recovery_path_kind;
    cloud_playback_observe->recovery_outcome_kind =
        snapshot.playback_recovery_outcome_kind;
    cloud_playback_observe->start_policy_kind =
        snapshot.playback_start_policy_kind;
    river_dialog_runtime_copy_text(
        cloud_playback_observe->playback_terminal_reason,
        sizeof(cloud_playback_observe->playback_terminal_reason),
        snapshot.playback_terminal_reason);
    river_dialog_runtime_copy_text(
        cloud_playback_observe->playback_terminal_wait_reason,
        sizeof(cloud_playback_observe->playback_terminal_wait_reason),
        snapshot.playback_terminal_wait_reason);
    cloud_playback_observe->start_frames = snapshot.playback_start_frames;
    cloud_playback_observe->prefetch_frames = snapshot.playback_prefetch_frames;
    cloud_playback_observe->start_cautious_history =
        snapshot.playback_start_cautious_history;
    return true;
}

static void river_dialog_runtime_export_round_facts_to_snapshot_locked(void)
{
    g_river_dialog_runtime.snapshot.conversation_window_active =
        g_river_dialog_runtime.cloud_round_facts.conversation_window_active;
    g_river_dialog_runtime.snapshot.conversation_window_remaining_ms =
        g_river_dialog_runtime.cloud_round_facts.conversation_window_remaining_ms;
    g_river_dialog_runtime.snapshot.cloud_listening =
        g_river_dialog_runtime.cloud_round_facts.cloud_listening;
    g_river_dialog_runtime.snapshot.cloud_stream_active =
        g_river_dialog_runtime.cloud_round_facts.cloud_stream_active;
    g_river_dialog_runtime.snapshot.cloud_listen_stop_pending =
        g_river_dialog_runtime.cloud_round_facts.cloud_listen_stop_pending;
    g_river_dialog_runtime.snapshot.cloud_local_close_pending =
        g_river_dialog_runtime.cloud_round_facts.cloud_local_close_pending;
    g_river_dialog_runtime.snapshot.local_close_remaining_ms =
        g_river_dialog_runtime.cloud_round_facts.local_close_remaining_ms;
}

static void river_dialog_runtime_export_io_facts_to_snapshot_locked(void)
{
    river_dialog_runtime_copy_text(g_river_dialog_runtime.snapshot.input_state_text,
                                   sizeof(g_river_dialog_runtime.snapshot.input_state_text),
                                   g_river_dialog_runtime.cloud_io_observe.input_state_text);
    river_dialog_runtime_copy_text(g_river_dialog_runtime.snapshot.output_state_text,
                                   sizeof(g_river_dialog_runtime.snapshot.output_state_text),
                                   g_river_dialog_runtime.cloud_io_observe.output_state_text);
    g_river_dialog_runtime.snapshot.input_lane =
        g_river_dialog_runtime.cloud_io_facts.input_lane;
    g_river_dialog_runtime.snapshot.output_lane =
        g_river_dialog_runtime.cloud_io_facts.output_lane;
}

static void river_dialog_runtime_export_session_facts_to_snapshot_locked(void)
{
    g_river_dialog_runtime.snapshot.turn_accepted =
        g_river_dialog_runtime.cloud_session_facts.turn_accepted;
    g_river_dialog_runtime.snapshot.barge_in_enabled_known =
        g_river_dialog_runtime.cloud_session_facts.barge_in_enabled_known;
    g_river_dialog_runtime.snapshot.barge_in_enabled =
        g_river_dialog_runtime.cloud_session_facts.barge_in_enabled;
    river_dialog_runtime_copy_text(g_river_dialog_runtime.snapshot.provider_name,
                                   sizeof(g_river_dialog_runtime.snapshot.provider_name),
                                   g_river_dialog_runtime.cloud_session_observe.provider_name);
    river_dialog_runtime_copy_text(g_river_dialog_runtime.snapshot.session_id,
                                   sizeof(g_river_dialog_runtime.snapshot.session_id),
                                   g_river_dialog_runtime.cloud_session_observe.session_id);
    river_dialog_runtime_copy_text(g_river_dialog_runtime.snapshot.turn_id,
                                   sizeof(g_river_dialog_runtime.snapshot.turn_id),
                                   g_river_dialog_runtime.cloud_session_observe.turn_id);
    river_dialog_runtime_copy_text(g_river_dialog_runtime.snapshot.accept_reason,
                                   sizeof(g_river_dialog_runtime.snapshot.accept_reason),
                                   g_river_dialog_runtime.cloud_session_observe.accept_reason);
}

static void river_dialog_runtime_export_control_facts_to_snapshot_locked(void)
{
    g_river_dialog_runtime.snapshot.boot_ready =
        g_river_dialog_runtime.control_facts.boot_ready;
    g_river_dialog_runtime.snapshot.wake_confirmed =
        g_river_dialog_runtime.control_facts.wake_confirmed;
    g_river_dialog_runtime.snapshot.asr_session_active =
        g_river_dialog_runtime.control_facts.asr_session_active;
    g_river_dialog_runtime.snapshot.wake_admission_pending =
        g_river_dialog_runtime.control_facts.wake_admission_pending;
    g_river_dialog_runtime.snapshot.tts_interrupt_requested =
        g_river_dialog_runtime.control_facts.tts_interrupt_requested;
}

static void river_dialog_runtime_export_derived_facts_to_snapshot_locked(void)
{
    g_river_dialog_runtime.snapshot.error_recovering =
        g_river_dialog_runtime.derived_facts.error_recovering;
    g_river_dialog_runtime.snapshot.error_kind =
        g_river_dialog_runtime.derived_facts.error_kind;
    g_river_dialog_runtime.snapshot.playback_owner_kind =
        g_river_dialog_runtime.derived_facts.playback_owner_kind;
    g_river_dialog_runtime.snapshot.playback_active =
        g_river_dialog_runtime.derived_facts.playback_active;
    g_river_dialog_runtime.snapshot.playback_recovering =
        g_river_dialog_runtime.derived_facts.playback_recovering;
    g_river_dialog_runtime.snapshot.interaction_state =
        g_river_dialog_runtime.derived_facts.interaction_state;
    g_river_dialog_runtime.snapshot.transition_count =
        g_river_dialog_runtime.derived_facts.transition_count;
    river_dialog_runtime_copy_text(g_river_dialog_runtime.snapshot.reason,
                                   sizeof(g_river_dialog_runtime.snapshot.reason),
                                   g_river_dialog_runtime.derived_facts.reason);
}

static void river_dialog_runtime_export_playback_facts_to_snapshot_locked(void)
{
    g_river_dialog_runtime.snapshot.playback_cloud_active =
        g_river_dialog_runtime.cloud_playback_facts.cloud_playback_active;
    g_river_dialog_runtime.snapshot.playback_lane_engaged =
        g_river_dialog_runtime.cloud_playback_facts.lane_engaged;
    g_river_dialog_runtime.snapshot.playback_turn_active =
        g_river_dialog_runtime.cloud_playback_facts.turn_active;
    g_river_dialog_runtime.snapshot.playback_rebuffer_pending =
        g_river_dialog_runtime.cloud_playback_facts.rebuffer_pending;
    g_river_dialog_runtime.snapshot.playback_phase_known =
        g_river_dialog_runtime.cloud_playback_observe.phase_known;
    g_river_dialog_runtime.snapshot.playback_phase_kind =
        g_river_dialog_runtime.cloud_playback_observe.phase_kind;
    g_river_dialog_runtime.snapshot.playback_backend_state_kind =
        g_river_dialog_runtime.cloud_playback_facts.backend_state_kind;
    g_river_dialog_runtime.snapshot.playback_supply_kind =
        g_river_dialog_runtime.cloud_playback_facts.supply_kind;
    g_river_dialog_runtime.snapshot.playback_hold_kind =
        g_river_dialog_runtime.cloud_playback_facts.hold_kind;
    g_river_dialog_runtime.snapshot.playback_terminal_closed =
        g_river_dialog_runtime.cloud_playback_facts.terminal_closed;
    g_river_dialog_runtime.snapshot.tts_stop_pending =
        g_river_dialog_runtime.cloud_playback_facts.tts_stop_pending;
    g_river_dialog_runtime.snapshot.playback_terminal_waiting =
        g_river_dialog_runtime.cloud_playback_facts.terminal_waiting;
    g_river_dialog_runtime.snapshot.playback_terminal_wait_kind =
        g_river_dialog_runtime.cloud_playback_facts.terminal_wait_kind;
    g_river_dialog_runtime.snapshot.playback_terminal_state_kind =
        g_river_dialog_runtime.cloud_playback_observe.terminal_state_kind;
    g_river_dialog_runtime.snapshot.playback_rebuffer_cause_kind =
        g_river_dialog_runtime.cloud_playback_observe.rebuffer_cause_kind;
    g_river_dialog_runtime.snapshot.playback_recovery_path_kind =
        g_river_dialog_runtime.cloud_playback_observe.recovery_path_kind;
    g_river_dialog_runtime.snapshot.playback_recovery_outcome_kind =
        g_river_dialog_runtime.cloud_playback_observe.recovery_outcome_kind;
    g_river_dialog_runtime.snapshot.playback_start_policy_kind =
        g_river_dialog_runtime.cloud_playback_observe.start_policy_kind;
    river_dialog_runtime_copy_text(
        g_river_dialog_runtime.snapshot.playback_terminal_reason,
        sizeof(g_river_dialog_runtime.snapshot.playback_terminal_reason),
        g_river_dialog_runtime.cloud_playback_observe.playback_terminal_reason);
    river_dialog_runtime_copy_text(
        g_river_dialog_runtime.snapshot.playback_terminal_wait_reason,
        sizeof(g_river_dialog_runtime.snapshot.playback_terminal_wait_reason),
        g_river_dialog_runtime.cloud_playback_observe.playback_terminal_wait_reason);
    g_river_dialog_runtime.snapshot.playback_start_frames =
        g_river_dialog_runtime.cloud_playback_observe.start_frames;
    g_river_dialog_runtime.snapshot.playback_prefetch_frames =
        g_river_dialog_runtime.cloud_playback_observe.prefetch_frames;
    g_river_dialog_runtime.snapshot.playback_start_cautious_history =
        g_river_dialog_runtime.cloud_playback_observe.start_cautious_history;
}

static void river_dialog_runtime_capture_local_playback_import(
    river_dialog_runtime_local_playback_import_t *local_playback_import,
    river_playback_state_t state,
    const river_playback_stream_config_t *config)
{
    if (local_playback_import == NULL) {
        return;
    }

    memset(local_playback_import, 0, sizeof(*local_playback_import));
    local_playback_import->state = state;
    if (config == NULL) {
        return;
    }

    local_playback_import->config_present = true;
    local_playback_import->priority = config->priority;
    river_dialog_runtime_copy_text(local_playback_import->stream_name,
                                   sizeof(local_playback_import->stream_name),
                                   config->stream_name);
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
        g_river_dialog_runtime.control_facts.boot_ready = true;
        g_river_dialog_runtime.asr_error_recovering = false;
        g_river_dialog_runtime.local_playback_error_recovering = false;
        g_river_dialog_runtime.control_facts.wake_admission_pending = false;
        g_river_dialog_runtime.control_facts.wake_confirmed = false;
        g_river_dialog_runtime.control_facts.tts_interrupt_requested = false;
        break;
    case RIVER_DIALOG_RUNTIME_CLOUD_EVENT_WAKE_CONFIRMED:
        g_river_dialog_runtime.control_facts.wake_admission_pending = false;
        g_river_dialog_runtime.control_facts.wake_confirmed = true;
        break;
    case RIVER_DIALOG_RUNTIME_CLOUD_EVENT_ASR_SESSION_STARTED:
        g_river_dialog_runtime.control_facts.asr_session_active = true;
        g_river_dialog_runtime.asr_error_recovering = false;
        g_river_dialog_runtime.control_facts.wake_confirmed = false;
        g_river_dialog_runtime.control_facts.tts_interrupt_requested = false;
        break;
    case RIVER_DIALOG_RUNTIME_CLOUD_EVENT_ASR_SESSION_CLOSED:
        g_river_dialog_runtime.control_facts.asr_session_active = false;
        g_river_dialog_runtime.control_facts.wake_confirmed = false;
        g_river_dialog_runtime.control_facts.tts_interrupt_requested = false;
        break;
    case RIVER_DIALOG_RUNTIME_CLOUD_EVENT_ASR_ERROR:
        g_river_dialog_runtime.control_facts.asr_session_active = false;
        g_river_dialog_runtime.asr_error_recovering = true;
        g_river_dialog_runtime.control_facts.wake_confirmed = false;
        g_river_dialog_runtime.control_facts.tts_interrupt_requested = false;
        break;
    default:
        break;
    }
    river_dialog_runtime_refresh_error_recovering_locked();

    if (sid != NULL && sid[0] != '\0') {
        river_dialog_runtime_copy_text(g_river_dialog_runtime.cloud_session_observe.session_id,
                                       sizeof(g_river_dialog_runtime.cloud_session_observe.session_id),
                                       sid);
        river_dialog_runtime_export_session_facts_to_snapshot_locked();
    }
    river_dialog_runtime_export_control_facts_to_snapshot_locked();
    river_dialog_runtime_export_derived_facts_to_snapshot_locked();
}

static void river_dialog_runtime_commit_cloud_event(
    river_dialog_runtime_cloud_event_t event,
    const char *sid,
    const char *reason)
{
    river_dialog_runtime_ingress_t ingress;

    memset(&ingress, 0, sizeof(ingress));
    ingress.has_cloud_import = river_dialog_runtime_capture_cloud_snapshot(
        &ingress.cloud_import,
        &ingress.cloud_playback_observe);
    ingress.has_cloud_playback_observe = ingress.has_cloud_import;
    ingress.has_cloud_event = true;
    ingress.cloud_event = event;
    ingress.commit_policy = RIVER_DIALOG_RUNTIME_COMMIT_POLICY_PUBLISH_ALWAYS;
    ingress.reason = reason;
    river_dialog_runtime_copy_text(ingress.session_id,
                                   sizeof(ingress.session_id),
                                   sid);
    river_dialog_runtime_commit_ingress(&ingress);
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

typedef struct {
    bool cloud_runtime_available;
    bool cloud_playback_active;
    bool lane_engaged;
    bool turn_active;
    bool rebuffer_pending;
    bool terminal_closed;
    bool terminal_waiting;
    bool tts_stop_pending;
    river_cloud_playback_backend_state_t backend_state_kind;
    river_cloud_playback_supply_kind_t supply_kind;
    river_cloud_playback_hold_kind_t hold_kind;
    river_cloud_playback_terminal_wait_kind_t terminal_wait_kind;
    river_dialog_output_lane_t output_lane;
    bool local_shadow_active;
    bool local_shadow_recovering;
} river_dialog_runtime_playback_projection_t;

typedef struct {
    river_playback_state_t state;
    bool active;
    bool recovering;
    bool drives_truth;
    bool active_fallback;
    bool recovering_fallback;
} river_dialog_runtime_local_playback_shadow_view_t;

typedef struct {
    bool should_absorb;
    bool claim_stream_ownership;
    bool release_stream_ownership;
    river_playback_state_t state;
    char stream_name[32];
} river_dialog_runtime_local_playback_import_plan_t;

typedef struct {
    bool active;
    bool recovering;
    river_dialog_playback_owner_kind_t owner_kind;
} river_dialog_runtime_playback_truth_t;

static void river_dialog_runtime_capture_local_playback_shadow_view_locked(
    river_dialog_runtime_local_playback_shadow_view_t *view)
{
    if (view == NULL) {
        return;
    }

    memset(view, 0, sizeof(*view));
    view->state = g_river_dialog_runtime.playback_state;
    view->active = river_playback_service_state_active(view->state);
    view->recovering =
        view->state == RIVER_PLAYBACK_RECOVERING ||
        view->state == RIVER_PLAYBACK_RESTART_PENDING;
    view->drives_truth = !river_dialog_runtime_cloud_runtime_available_locked();
    view->active_fallback = view->drives_truth && view->active;
    view->recovering_fallback = view->drives_truth && view->recovering;
}

typedef struct {
    river_dialog_runtime_playback_projection_t playback;
    bool boot_ready;
    bool error_recovering;
    bool asr_session_active;
    bool wake_confirmed;
    bool wake_admission_pending;
    bool conversation_window_active;
    bool cloud_listening;
    bool cloud_stream_active;
    bool cloud_listen_stop_pending;
    bool cloud_local_close_pending;
    bool playback_active;
    bool playback_recovering;
    bool playback_terminal_closed;
    bool tts_interrupt_requested;
    bool output_turn_engaged;
    river_dialog_input_lane_t input_lane;
    river_dialog_output_lane_t output_lane;
    river_interaction_state_t interaction_state;
} river_dialog_runtime_interaction_projection_t;

static void river_dialog_runtime_capture_playback_projection_locked(
    river_dialog_runtime_playback_projection_t *projection)
{
    const river_dialog_runtime_cloud_playback_facts_t *playback_facts =
        &g_river_dialog_runtime.cloud_playback_facts;
    const river_dialog_runtime_cloud_io_facts_t *io_facts =
        &g_river_dialog_runtime.cloud_io_facts;
    river_dialog_runtime_local_playback_shadow_view_t shadow_view;

    if (projection == NULL) {
        return;
    }

    memset(projection, 0, sizeof(*projection));
    river_dialog_runtime_capture_local_playback_shadow_view_locked(&shadow_view);
    projection->cloud_runtime_available =
        river_dialog_runtime_cloud_runtime_available_locked();
    projection->cloud_playback_active = playback_facts->cloud_playback_active;
    projection->lane_engaged = playback_facts->lane_engaged;
    projection->turn_active = playback_facts->turn_active;
    projection->rebuffer_pending = playback_facts->rebuffer_pending;
    projection->terminal_closed = playback_facts->terminal_closed;
    projection->terminal_waiting = playback_facts->terminal_waiting;
    projection->tts_stop_pending = playback_facts->tts_stop_pending;
    projection->backend_state_kind = playback_facts->backend_state_kind;
    projection->supply_kind = playback_facts->supply_kind;
    projection->hold_kind = playback_facts->hold_kind;
    projection->terminal_wait_kind = playback_facts->terminal_wait_kind;
    projection->output_lane = io_facts->output_lane;
    projection->local_shadow_active = shadow_view.active_fallback;
    projection->local_shadow_recovering = shadow_view.recovering_fallback;
}

static bool river_dialog_runtime_playback_waiting_segment_from_projection(
    const river_dialog_runtime_playback_projection_t *projection)
{
    return projection != NULL &&
           projection->supply_kind ==
               RIVER_CLOUD_PLAYBACK_SUPPLY_WAITING_NEXT_SEGMENT;
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

static void river_dialog_runtime_capture_playback_truth_from_projection(
    const river_dialog_runtime_playback_projection_t *projection,
    river_dialog_runtime_playback_truth_t *truth)
{
    if (truth == NULL) {
        return;
    }

    memset(truth, 0, sizeof(*truth));
    if (projection == NULL) {
        return;
    }

    truth->recovering =
        projection->rebuffer_pending ||
        projection->backend_state_kind ==
            RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_RECOVERING ||
        projection->backend_state_kind ==
            RIVER_CLOUD_PLAYBACK_BACKEND_RESTART_PENDING ||
        projection->local_shadow_recovering;
    if (!projection->terminal_closed) {
        if (projection->cloud_playback_active || truth->recovering ||
            river_dialog_runtime_playback_segment_gap_hold_from_projection(projection)) {
            truth->active = true;
        } else if (projection->local_shadow_active ||
                   !projection->terminal_waiting) {
            truth->active = projection->local_shadow_active;
        }
    }
    if (projection->cloud_runtime_available &&
        (projection->cloud_playback_active || projection->lane_engaged ||
         projection->turn_active || truth->recovering ||
         projection->terminal_waiting || projection->terminal_closed ||
         projection->hold_kind != RIVER_CLOUD_PLAYBACK_HOLD_NONE ||
         projection->tts_stop_pending)) {
        truth->owner_kind = RIVER_DIALOG_PLAYBACK_OWNER_KIND_CLOUD;
    } else if (projection->local_shadow_active ||
               projection->local_shadow_recovering) {
        truth->owner_kind = RIVER_DIALOG_PLAYBACK_OWNER_KIND_LOCAL_FALLBACK;
    }
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

static bool river_dialog_runtime_playback_turn_recovering_from_projection(
    const river_dialog_runtime_playback_projection_t *projection)
{
    return projection != NULL &&
           (projection->rebuffer_pending ||
            projection->backend_state_kind ==
                RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_RECOVERING ||
            projection->backend_state_kind ==
                RIVER_CLOUD_PLAYBACK_BACKEND_RESTART_PENDING);
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
    if (!projection->cloud_runtime_available) {
        return true;
    }
    return river_dialog_runtime_playback_turn_recovering_from_projection(
        projection);
}

static bool river_dialog_runtime_output_speaking_effective_from_projection(
    const river_dialog_runtime_playback_projection_t *projection,
    bool playback_active,
    bool playback_recovering)
{
    if (projection == NULL ||
        projection->output_lane != RIVER_DIALOG_OUTPUT_LANE_SPEAKING ||
        projection->terminal_closed) {
        return false;
    }
    if (river_dialog_runtime_playback_waiting_segment_from_projection(projection) &&
        !projection->cloud_playback_active && !playback_recovering) {
        return false;
    }
    if (projection->terminal_waiting &&
        river_dialog_runtime_terminal_wait_suppresses_speaking_from_projection(
            projection) &&
        !playback_active && !playback_recovering) {
        return false;
    }
    if (!playback_active && !playback_recovering &&
        !river_dialog_runtime_playback_turn_retains_output_turn_from_projection(
            projection)) {
        return false;
    }

    return true;
}

static bool river_dialog_runtime_output_turn_engaged_from_projection(
    const river_dialog_runtime_playback_projection_t *projection,
    bool playback_active,
    bool playback_recovering)
{
    if (projection == NULL || projection->terminal_closed) {
        return false;
    }

    return playback_active ||
           river_dialog_runtime_playback_turn_retains_output_turn_from_projection(
               projection) ||
           river_dialog_runtime_output_speaking_effective_from_projection(
               projection,
               playback_active,
               playback_recovering);
}

static void river_dialog_runtime_capture_interaction_projection_locked(
    river_dialog_runtime_interaction_projection_t *projection)
{
    const river_dialog_runtime_control_facts_t *control_facts =
        &g_river_dialog_runtime.control_facts;
    const river_dialog_runtime_derived_facts_t *derived_facts =
        &g_river_dialog_runtime.derived_facts;
    const river_dialog_runtime_cloud_round_facts_t *round_facts =
        &g_river_dialog_runtime.cloud_round_facts;
    const river_dialog_runtime_cloud_io_facts_t *io_facts =
        &g_river_dialog_runtime.cloud_io_facts;

    if (projection == NULL) {
        return;
    }

    memset(projection, 0, sizeof(*projection));
    river_dialog_runtime_capture_playback_projection_locked(&projection->playback);
    projection->boot_ready = control_facts->boot_ready;
    projection->error_recovering = derived_facts->error_recovering;
    projection->asr_session_active = control_facts->asr_session_active;
    projection->wake_confirmed = control_facts->wake_confirmed;
    projection->wake_admission_pending = control_facts->wake_admission_pending;
    projection->conversation_window_active = round_facts->conversation_window_active;
    projection->cloud_listening = round_facts->cloud_listening;
    projection->cloud_stream_active = round_facts->cloud_stream_active;
    projection->cloud_listen_stop_pending = round_facts->cloud_listen_stop_pending;
    projection->cloud_local_close_pending = round_facts->cloud_local_close_pending;
    projection->playback_active = derived_facts->playback_active;
    projection->playback_recovering = derived_facts->playback_recovering;
    projection->playback_terminal_closed = projection->playback.terminal_closed;
    projection->tts_interrupt_requested = control_facts->tts_interrupt_requested;
    projection->input_lane = io_facts->input_lane;
    projection->output_lane = io_facts->output_lane;
    projection->interaction_state = derived_facts->interaction_state;
    projection->output_turn_engaged =
        river_dialog_runtime_output_turn_engaged_from_projection(
            &projection->playback,
            projection->playback_active,
            projection->playback_recovering);
}

static bool river_dialog_runtime_tts_interrupt_inflight_from_projection(
    const river_dialog_runtime_interaction_projection_t *projection)
{
    return projection != NULL &&
           (projection->playback.tts_stop_pending ||
            projection->tts_interrupt_requested);
}

static void river_dialog_runtime_refresh_playback_locked(void)
{
    river_dialog_runtime_playback_projection_t projection;
    river_dialog_runtime_playback_truth_t truth;

    river_dialog_runtime_capture_playback_projection_locked(&projection);
    river_dialog_runtime_capture_playback_truth_from_projection(&projection, &truth);
    g_river_dialog_runtime.derived_facts.playback_recovering = truth.recovering;
    g_river_dialog_runtime.derived_facts.playback_active = truth.active;
    g_river_dialog_runtime.derived_facts.playback_owner_kind = truth.owner_kind;
    river_dialog_runtime_export_derived_facts_to_snapshot_locked();
}

static bool river_dialog_runtime_output_turn_quiesced_locked(void)
{
    river_dialog_runtime_playback_projection_t projection;
    river_dialog_runtime_playback_truth_t truth;

    river_dialog_runtime_capture_playback_projection_locked(&projection);
    river_dialog_runtime_capture_playback_truth_from_projection(&projection, &truth);
    return !river_dialog_runtime_output_turn_engaged_from_projection(
               &projection,
               g_river_dialog_runtime.derived_facts.playback_active,
               truth.recovering) &&
           !projection.turn_active &&
           !truth.recovering &&
           !projection.tts_stop_pending &&
           projection.backend_state_kind !=
               RIVER_CLOUD_PLAYBACK_BACKEND_RESTART_PENDING &&
           !projection.terminal_waiting;
}

static bool river_dialog_runtime_cloud_round_active_locked(void)
{
    river_dialog_runtime_interaction_projection_t projection;

    river_dialog_runtime_capture_interaction_projection_locked(&projection);
    return projection.cloud_listening ||
           projection.cloud_stream_active ||
           projection.cloud_listen_stop_pending ||
           projection.cloud_local_close_pending ||
           projection.input_lane == RIVER_DIALOG_INPUT_LANE_ACTIVE ||
           projection.input_lane == RIVER_DIALOG_INPUT_LANE_COMMITTED;
}

static river_interaction_state_t river_dialog_runtime_compute_interaction_state_locked(void)
{
    river_dialog_runtime_interaction_projection_t projection;

    river_dialog_runtime_capture_interaction_projection_locked(&projection);
    if (!projection.boot_ready) {
        return RIVER_INTERACTION_BOOTING;
    }
    if (projection.error_recovering) {
        return RIVER_INTERACTION_ERROR_RECOVERING;
    }
    if (projection.output_turn_engaged) {
        if (projection.asr_session_active ||
            projection.input_lane == RIVER_DIALOG_INPUT_LANE_ACTIVE) {
            return RIVER_INTERACTION_BARGE_IN_LISTENING;
        }
        return RIVER_INTERACTION_SPEAKING;
    }
    if (projection.output_lane == RIVER_DIALOG_OUTPUT_LANE_THINKING) {
        return RIVER_INTERACTION_THINKING;
    }
    if (projection.asr_session_active ||
        projection.input_lane == RIVER_DIALOG_INPUT_LANE_ACTIVE) {
        return RIVER_INTERACTION_ASR_STREAMING;
    }
    if (projection.wake_confirmed) {
        return RIVER_INTERACTION_WAKE_CONFIRMED;
    }
    if (projection.conversation_window_active) {
        return RIVER_INTERACTION_FOLLOW_UP;
    }
    return RIVER_INTERACTION_WAKE_MONITORING;
}

static void river_dialog_runtime_capture_commit_checkpoint_locked(
    river_dialog_runtime_commit_checkpoint_t *checkpoint,
    bool derive_interaction_state)
{
    if (checkpoint == NULL) {
        return;
    }

    memset(checkpoint, 0, sizeof(*checkpoint));
    checkpoint->cloud_runtime_available =
        river_dialog_runtime_cloud_runtime_available_locked();
    checkpoint->playback_active = g_river_dialog_runtime.derived_facts.playback_active;
    checkpoint->playback_recovering =
        g_river_dialog_runtime.derived_facts.playback_recovering;
    checkpoint->error_recovering = g_river_dialog_runtime.derived_facts.error_recovering;
    checkpoint->interaction_state =
        derive_interaction_state ? river_dialog_runtime_compute_interaction_state_locked() :
                                   g_river_dialog_runtime.derived_facts.interaction_state;
}

static bool river_dialog_runtime_commit_checkpoint_changed(
    const river_dialog_runtime_commit_checkpoint_t *before,
    const river_dialog_runtime_commit_checkpoint_t *after)
{
    if (before == NULL || after == NULL) {
        return true;
    }

    return before->playback_active != after->playback_active ||
           before->playback_recovering != after->playback_recovering ||
           before->error_recovering != after->error_recovering ||
           before->interaction_state != after->interaction_state;
}

static void river_dialog_runtime_finalize_commit_locked(
    const river_dialog_runtime_commit_checkpoint_t *before,
    river_dialog_runtime_commit_policy_t policy,
    const char *reason)
{
    river_dialog_runtime_commit_checkpoint_t after;

    if (policy == RIVER_DIALOG_RUNTIME_COMMIT_POLICY_SKIP_IF_CLOUD_DIALOG_STABLE &&
        before != NULL) {
        river_dialog_runtime_capture_commit_checkpoint_locked(&after, true);
        if (after.cloud_runtime_available &&
            !river_dialog_runtime_commit_checkpoint_changed(before, &after)) {
            if (reason != NULL && reason[0] != '\0') {
                river_dialog_runtime_copy_text(g_river_dialog_runtime.derived_facts.reason,
                                               sizeof(g_river_dialog_runtime.derived_facts.reason),
                                               reason);
                river_dialog_runtime_export_derived_facts_to_snapshot_locked();
            }
            return;
        }
    }

    river_dialog_runtime_publish_locked(reason);
}

static const char *river_dialog_runtime_ingress_default_reason(
    const river_dialog_runtime_ingress_t *ingress)
{
    if (ingress == NULL) {
        return "dialog_runtime";
    }
    if (ingress->reason != NULL && ingress->reason[0] != '\0') {
        return ingress->reason;
    }
    if (ingress->has_cloud_event) {
        return river_dialog_runtime_cloud_event_default_reason(
            ingress->cloud_event);
    }
    if (ingress->has_local_playback_import) {
        return "playback_state";
    }
    if (ingress->has_cloud_import) {
        return "cloud_state_sync";
    }
    return "dialog_runtime";
}

static void river_dialog_runtime_publish_locked(const char *reason)
{
    river_interaction_state_t next_state;

    next_state = river_dialog_runtime_compute_interaction_state_locked();
    if (g_river_dialog_runtime.derived_facts.interaction_state != next_state) {
        g_river_dialog_runtime.derived_facts.transition_count++;
    }
    g_river_dialog_runtime.derived_facts.interaction_state = next_state;
    if (reason != NULL && reason[0] != '\0') {
        river_dialog_runtime_copy_text(g_river_dialog_runtime.derived_facts.reason,
                                       sizeof(g_river_dialog_runtime.derived_facts.reason),
                                       reason);
    }
    river_dialog_runtime_export_derived_facts_to_snapshot_locked();
    (void)river_interaction_state_set(next_state,
                                      g_river_dialog_runtime.derived_facts.reason[0] != '\0' ?
                                          g_river_dialog_runtime.derived_facts.reason :
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

static void river_dialog_runtime_import_cloud_snapshot_locked(
    const river_dialog_runtime_cloud_import_t *cloud_import)
{
    if (cloud_import == NULL) {
        return;
    }

    g_river_dialog_runtime.cloud_runtime_available = cloud_import->runtime_available;
    g_river_dialog_runtime.cloud_round_facts = cloud_import->round_facts;
    g_river_dialog_runtime.cloud_io_facts = cloud_import->io_facts;
    g_river_dialog_runtime.cloud_io_observe = cloud_import->io_observe;
    g_river_dialog_runtime.cloud_session_facts = cloud_import->session_facts;
    g_river_dialog_runtime.cloud_session_observe = cloud_import->session_observe;
    g_river_dialog_runtime.cloud_playback_facts = cloud_import->playback_facts;
    river_dialog_runtime_export_round_facts_to_snapshot_locked();
    river_dialog_runtime_export_io_facts_to_snapshot_locked();
    river_dialog_runtime_export_session_facts_to_snapshot_locked();
    river_dialog_runtime_export_playback_facts_to_snapshot_locked();
}

static void river_dialog_runtime_import_cloud_playback_observe_locked(
    const river_dialog_runtime_cloud_playback_observe_t *cloud_playback_observe)
{
    if (cloud_playback_observe == NULL) {
        return;
    }

    g_river_dialog_runtime.cloud_playback_observe = *cloud_playback_observe;
    river_dialog_runtime_export_playback_facts_to_snapshot_locked();
}

static bool river_dialog_runtime_prepare_local_playback_import_plan_locked(
    const river_dialog_runtime_local_playback_import_t *local_playback_import,
    river_dialog_runtime_local_playback_import_plan_t *plan)
{
    bool dialog_stream = false;

    if (plan == NULL) {
        return false;
    }

    memset(plan, 0, sizeof(*plan));
    if (local_playback_import == NULL) {
        return false;
    }

    dialog_stream =
        river_dialog_runtime_is_dialog_playback_stream_locked(local_playback_import);
    if (!dialog_stream &&
        !river_dialog_runtime_matches_owned_playback_stream_locked(
            local_playback_import)) {
        return false;
    }

    plan->should_absorb = true;
    plan->claim_stream_ownership = dialog_stream;
    plan->release_stream_ownership =
        local_playback_import->state == RIVER_PLAYBACK_IDLE;
    plan->state = local_playback_import->state;
    if (plan->claim_stream_ownership) {
        river_dialog_runtime_copy_text(plan->stream_name,
                                       sizeof(plan->stream_name),
                                       local_playback_import->stream_name);
    }
    return true;
}

static void river_dialog_runtime_apply_local_playback_import_plan_locked(
    const river_dialog_runtime_local_playback_import_plan_t *plan,
    const char **effective_reason)
{
    bool drives_truth = false;
    bool managed_recovery = false;

    if (plan == NULL || !plan->should_absorb) {
        return;
    }

    if (plan->claim_stream_ownership) {
        g_river_dialog_runtime.local_playback_stream_owned = true;
        river_dialog_runtime_copy_text(g_river_dialog_runtime.local_playback_stream_name,
                                       sizeof(g_river_dialog_runtime.local_playback_stream_name),
                                       plan->stream_name);
    }
    if (plan->release_stream_ownership) {
        g_river_dialog_runtime.local_playback_stream_owned = false;
        g_river_dialog_runtime.local_playback_stream_name[0] = '\0';
    }
    g_river_dialog_runtime.playback_state = plan->state;
    drives_truth = !river_dialog_runtime_cloud_runtime_available_locked();

    if (!drives_truth) {
        return;
    }

    if (plan->state == RIVER_PLAYBACK_ERROR) {
        managed_recovery = river_dialog_runtime_playback_error_is_managed_recovery_locked();
        if (managed_recovery) {
            g_river_dialog_runtime.local_playback_error_recovering = false;
            *effective_reason = "playback_recovering";
        } else {
            g_river_dialog_runtime.local_playback_error_recovering = true;
        }
    } else {
        g_river_dialog_runtime.local_playback_error_recovering = false;
    }
    if (plan->state == RIVER_PLAYBACK_ERROR && !managed_recovery) {
        g_river_dialog_runtime.control_facts.tts_interrupt_requested = false;
        river_dialog_runtime_export_control_facts_to_snapshot_locked();
    }
}

static void river_dialog_runtime_reconcile_facts_locked(void)
{
    if (river_dialog_runtime_cloud_runtime_available_locked()) {
        g_river_dialog_runtime.local_playback_error_recovering = false;
    }
    river_dialog_runtime_refresh_error_recovering_locked();
    if (river_dialog_runtime_cloud_round_active_locked()) {
        g_river_dialog_runtime.control_facts.asr_session_active = true;
    }
    river_dialog_runtime_refresh_playback_locked();
    if (river_dialog_runtime_output_turn_quiesced_locked()) {
        g_river_dialog_runtime.control_facts.tts_interrupt_requested = false;
    }
    river_dialog_runtime_export_control_facts_to_snapshot_locked();
}

static void river_dialog_runtime_commit_ingress(
    const river_dialog_runtime_ingress_t *ingress)
{
    river_dialog_runtime_commit_checkpoint_t commit_before;
    river_dialog_runtime_local_playback_import_plan_t local_playback_plan;
    const char *effective_reason;
    bool have_commit_before = false;
    bool have_local_playback_plan = false;

    if (ingress == NULL) {
        return;
    }
    if (!river_dialog_runtime_lock()) {
        return;
    }

    effective_reason = river_dialog_runtime_ingress_default_reason(ingress);
    if (ingress->has_local_playback_import &&
        !river_dialog_runtime_prepare_local_playback_import_plan_locked(
            &ingress->local_playback_import,
            &local_playback_plan)) {
        river_dialog_runtime_unlock();
        return;
    }
    have_local_playback_plan = ingress->has_local_playback_import;
    if (ingress->commit_policy ==
        RIVER_DIALOG_RUNTIME_COMMIT_POLICY_SKIP_IF_CLOUD_DIALOG_STABLE) {
        river_dialog_runtime_capture_commit_checkpoint_locked(&commit_before, false);
        have_commit_before = true;
    }
    if (ingress->has_cloud_event) {
        river_dialog_runtime_apply_cloud_event_locked(
            ingress->cloud_event,
            ingress->session_id[0] != '\0' ? ingress->session_id : NULL);
    }
    if (ingress->has_cloud_import) {
        river_dialog_runtime_import_cloud_snapshot_locked(&ingress->cloud_import);
    }
    if (ingress->has_cloud_playback_observe) {
        river_dialog_runtime_import_cloud_playback_observe_locked(
            &ingress->cloud_playback_observe);
    }
    if (have_local_playback_plan) {
        river_dialog_runtime_apply_local_playback_import_plan_locked(
            &local_playback_plan,
            &effective_reason);
    }
    if (!ingress->has_cloud_event && !ingress->has_cloud_import &&
        !ingress->has_cloud_playback_observe &&
        !ingress->has_local_playback_import) {
        river_dialog_runtime_unlock();
        return;
    }

    river_dialog_runtime_reconcile_facts_locked();
    river_dialog_runtime_finalize_commit_locked(
        have_commit_before ? &commit_before : NULL,
        ingress->commit_policy,
        effective_reason);
    river_dialog_runtime_unlock();
}

static void river_dialog_runtime_set_wake_admission_pending_locked(bool pending)
{
    g_river_dialog_runtime.control_facts.wake_admission_pending = pending;
    river_dialog_runtime_export_control_facts_to_snapshot_locked();
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
    g_river_dialog_runtime.playback_state = RIVER_PLAYBACK_IDLE;
    g_river_dialog_runtime.derived_facts.interaction_state = RIVER_INTERACTION_BOOTING;
    river_dialog_runtime_copy_text(g_river_dialog_runtime.derived_facts.reason,
                                   sizeof(g_river_dialog_runtime.derived_facts.reason),
                                   "boot_begin");
    river_dialog_runtime_export_control_facts_to_snapshot_locked();
    river_dialog_runtime_export_derived_facts_to_snapshot_locked();
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

    if (!g_river_dialog_runtime.control_facts.tts_interrupt_requested) {
        g_river_dialog_runtime.control_facts.tts_interrupt_requested = true;
    }
    if (reason != NULL && reason[0] != '\0') {
        river_dialog_runtime_copy_text(g_river_dialog_runtime.derived_facts.reason,
                                       sizeof(g_river_dialog_runtime.derived_facts.reason),
                                       reason);
    }
    river_dialog_runtime_export_control_facts_to_snapshot_locked();
    river_dialog_runtime_export_derived_facts_to_snapshot_locked();
    river_dialog_runtime_unlock();
}

static void river_dialog_runtime_reduce_local_playback_event(
    river_playback_state_t state,
    const river_playback_stream_config_t *config,
    const char *reason)
{
    river_dialog_runtime_ingress_t ingress;

    memset(&ingress, 0, sizeof(ingress));
    ingress.has_cloud_import = river_dialog_runtime_capture_cloud_snapshot(
        &ingress.cloud_import,
        &ingress.cloud_playback_observe);
    ingress.has_cloud_playback_observe = ingress.has_cloud_import;
    ingress.has_local_playback_import = true;
    ingress.commit_policy =
        RIVER_DIALOG_RUNTIME_COMMIT_POLICY_SKIP_IF_CLOUD_DIALOG_STABLE;
    ingress.reason = reason;
    river_dialog_runtime_capture_local_playback_import(&ingress.local_playback_import,
                                                       state,
                                                       config);
    river_dialog_runtime_commit_ingress(&ingress);
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
    river_dialog_runtime_ingress_t ingress;

    memset(&ingress, 0, sizeof(ingress));
    ingress.has_cloud_import = river_dialog_runtime_capture_cloud_snapshot(
        &ingress.cloud_import,
        &ingress.cloud_playback_observe);
    ingress.has_cloud_playback_observe = ingress.has_cloud_import;
    if (!ingress.has_cloud_import) {
        return;
    }
    ingress.commit_policy = RIVER_DIALOG_RUNTIME_COMMIT_POLICY_PUBLISH_ALWAYS;
    ingress.reason = reason;
    river_dialog_runtime_commit_ingress(&ingress);
}

static const char *river_dialog_runtime_wakeword_block_reason_locked(void)
{
    river_dialog_runtime_interaction_projection_t projection;

    river_dialog_runtime_capture_interaction_projection_locked(&projection);
    if (projection.wake_admission_pending) {
        return "wake_admission_pending";
    }
    if (projection.conversation_window_active) {
        return "conversation_window_active";
    }
    if (projection.cloud_local_close_pending) {
        return "cloud_local_close_pending";
    }
    if (projection.cloud_listen_stop_pending) {
        return "cloud_listen_stop_pending";
    }
    if (projection.interaction_state != RIVER_INTERACTION_WAKE_MONITORING) {
        return river_interaction_state_name(projection.interaction_state);
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
    river_dialog_runtime_interaction_projection_t projection;

    if (!g_river_dialog_runtime.initialized) {
        return false;
    }
    if (!river_dialog_runtime_lock()) {
        return false;
    }

    river_dialog_runtime_capture_interaction_projection_locked(&projection);
    allowed = projection.asr_session_active &&
              projection.output_turn_engaged &&
              !river_dialog_runtime_tts_interrupt_inflight_from_projection(
                  &projection) &&
              !projection.playback_terminal_closed &&
              (projection.interaction_state ==
                   RIVER_INTERACTION_SPEAKING ||
               projection.interaction_state ==
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
    river_dialog_runtime_local_playback_shadow_view_t shadow_view;

    if (!g_river_dialog_runtime.initialized) {
        return;
    }
    if (!river_dialog_runtime_lock()) {
        return;
    }

    snapshot = g_river_dialog_runtime.snapshot;
    river_dialog_runtime_capture_local_playback_shadow_view_locked(&shadow_view);
    river_dialog_runtime_unlock();

    RIVER_LOGI("dialog_runtime interaction=%s input_lane=%s output_lane=%s asr=%s wake_admission=%s playback=%s local_playback=%s/%s cloud_playback=%s/%s owner=%s phase_known=%s backend_state=%s supply=%s hold=%s start_gate=%s/%lu prefetch=%lu cautious=%s lane=%s turn=%s recovering=%s/%s recovery_path=%s recovery_outcome=%s local_recovering=%s terminal_closed=%s terminal=%s/%s terminal_wait=%s/%s interrupt=%s stop_pending=%s local_close=%s/%lu window=%s/%lu wake_confirmed=%s error=%s/%s turn_id=%s accept_reason=%s reason=%s transitions=%lu",
               river_interaction_state_name(snapshot.interaction_state),
               river_dialog_input_lane_name(snapshot.input_lane),
               river_dialog_output_lane_name(snapshot.output_lane),
               snapshot.asr_session_active ? "yes" : "no",
               snapshot.wake_admission_pending ? "yes" : "no",
               snapshot.playback_active ? "yes" : "no",
               shadow_view.active ? "yes" : "no",
               river_dialog_runtime_local_playback_state_name(shadow_view.state),
               snapshot.playback_cloud_active ? "yes" : "no",
               snapshot.playback_phase_known ?
                   river_cloud_playback_phase_name(snapshot.playback_phase_kind) :
                   "-",
               river_dialog_playback_owner_kind_name(snapshot.playback_owner_kind),
               snapshot.playback_phase_known ? "yes" : "no",
               river_cloud_playback_backend_state_name(
                   snapshot.playback_backend_state_kind),
               river_cloud_playback_supply_kind_name(snapshot.playback_supply_kind),
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
               river_cloud_playback_recovery_path_name(
                   snapshot.playback_recovery_path_kind) != NULL ?
                   river_cloud_playback_recovery_path_name(
                       snapshot.playback_recovery_path_kind) :
                   "-",
               river_cloud_playback_recovery_outcome_name(
                   snapshot.playback_recovery_outcome_kind) != NULL ?
                   river_cloud_playback_recovery_outcome_name(
                       snapshot.playback_recovery_outcome_kind) :
                   "-",
               shadow_view.recovering ? "yes" : "no",
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
