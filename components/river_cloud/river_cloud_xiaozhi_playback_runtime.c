/* XiaoZhi downlink / playback runtime: queueing, rebuffer, ACK progress, and worker ownership. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "os_wrapper.h"
#include "rtk_status.h"

#include "river/river_log.h"
#include "river/river_playback_service.h"
#include "river_cloud_internal.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.cloud"

#define RIVER_CLOUD_XIAOZHI_PLAYBACK_GAIN_NUM 1
#define RIVER_CLOUD_XIAOZHI_PLAYBACK_GAIN_DEN 1
#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
typedef struct {
    river_cloud_playback_start_policy_t policy;
    uint32_t start_frames;
    uint32_t prefetch_frames;
    bool cautious_history;
} river_cloud_xiaozhi_playback_start_gate_t;

typedef struct {
    river_playback_state_t state;
    bool active;
    bool owned_stream;
} river_cloud_xiaozhi_playback_service_view_t;

typedef struct {
    bool stop_pending;
    bool rebuffer_pending;
    bool physical_active;
    bool waiting_next_segment;
    uint32_t queued_frames;
    uint32_t segment_count;
} river_cloud_xiaozhi_playback_phase_source_t;

typedef struct {
    river_cloud_xiaozhi_playback_service_view_t service_view;
    bool stop_pending;
    bool rebuffer_pending;
    bool physical_active;
    bool waiting_next_segment;
} river_cloud_xiaozhi_playback_backend_source_t;

typedef struct {
    bool wait_context_valid;
    bool last_segment_observed;
    uint32_t segment_count;
} river_cloud_xiaozhi_playback_supply_source_t;

typedef struct {
    size_t mono_bytes;
    size_t stereo_bytes;
    bool frame_too_large;
} river_cloud_xiaozhi_downlink_write_view_t;

static int16_t river_cloud_xiaozhi_playback_sat16(int32_t value)
{
    if (value > 32767) {
        return 32767;
    }
    if (value < -32768) {
        return -32768;
    }
    return (int16_t)value;
}

static uint32_t river_cloud_xiaozhi_downlink_start_threshold_frames(void);
static uint32_t river_cloud_xiaozhi_playback_buffer_frame_budget(void);
static uint32_t river_cloud_xiaozhi_downlink_attached_resume_threshold_frames(void);
static uint32_t river_cloud_xiaozhi_downlink_segment_gap_hold_frames(void);
static uint32_t river_cloud_xiaozhi_downlink_starved_low_water_frames(void);
static uint32_t river_cloud_xiaozhi_downlink_start_threshold_for_backend(
    river_cloud_playback_backend_state_t backend_state);
static river_status_t river_cloud_xiaozhi_write_current_downlink_frame_audio(
    const river_cloud_xiaozhi_downlink_write_view_t *view);
static bool river_cloud_xiaozhi_playback_last_fully_heard_context_valid(void);
static bool river_cloud_xiaozhi_playback_last_segment_observed(void);
static void river_cloud_xiaozhi_capture_playback_supply_source(
    river_cloud_xiaozhi_playback_supply_source_t *source);
static bool river_cloud_xiaozhi_compute_playback_waiting_next_segment_from_source(
    const river_cloud_xiaozhi_playback_supply_source_t *source);
static river_status_t river_cloud_xiaozhi_stop_playback_for_rebuffer(const char *reason);
static void river_cloud_xiaozhi_note_playback_rebuffer(
    uint64_t paused_at_ms,
    river_cloud_playback_rebuffer_cause_t cause);
static void river_cloud_xiaozhi_set_playback_recovery_path(
    river_cloud_playback_recovery_path_t path,
    const char *reason);
static void river_cloud_xiaozhi_set_playback_recovery_outcome(
    river_cloud_playback_recovery_outcome_t outcome,
    const char *reason);
static void river_cloud_xiaozhi_downlink_expand_stereo(const uint8_t *mono_frame,
                                                       size_t mono_bytes);
static void river_cloud_xiaozhi_try_start_current_playback_segment(uint64_t start_ms);
static void river_cloud_xiaozhi_update_playback_ack_progress(void);

static bool river_cloud_xiaozhi_playback_physical_active(void)
{
    return g_river_cloud.xiaozhi_playback_runtime_truth.active;
}

static void river_cloud_xiaozhi_capture_playback_service_view(
    river_cloud_xiaozhi_playback_service_view_t *view)
{
    river_playback_service_stats_t stats;

    if (view == NULL) {
        return;
    }

    memset(view, 0, sizeof(*view));
    river_playback_service_get_stats(&stats);
    view->state = stats.state;
    view->active = river_playback_service_state_active(stats.state);
    view->owned_stream =
        view->active &&
        strcmp(stats.stream_name, RIVER_CLOUD_XIAOZHI_TTS_STREAM_NAME) == 0;
}

static river_cloud_playback_backend_state_t
river_cloud_xiaozhi_compute_playback_backend_state_from_source(
    const river_cloud_xiaozhi_playback_backend_source_t *source)
{
    if (source == NULL) {
        return RIVER_CLOUD_PLAYBACK_BACKEND_DETACHED;
    }

    if (!source->service_view.active) {
        return RIVER_CLOUD_PLAYBACK_BACKEND_DETACHED;
    }
    if (source->service_view.owned_stream) {
        if (source->service_view.state == RIVER_PLAYBACK_RESTART_PENDING) {
            return RIVER_CLOUD_PLAYBACK_BACKEND_RESTART_PENDING;
        }
        if (source->service_view.state == RIVER_PLAYBACK_RECOVERING ||
            source->rebuffer_pending) {
            return RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_RECOVERING;
        }
        if (source->physical_active || source->stop_pending) {
            return RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_ACTIVE;
        }
        return RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_PAUSED;
    }
    return RIVER_CLOUD_PLAYBACK_BACKEND_FOREIGN_ACTIVE;
}

static void river_cloud_xiaozhi_capture_playback_backend_source(
    river_cloud_xiaozhi_playback_backend_source_t *source)
{
    river_cloud_xiaozhi_playback_supply_source_t supply_source;

    if (source == NULL) {
        return;
    }

    memset(source, 0, sizeof(*source));
    river_cloud_xiaozhi_capture_playback_supply_source(&supply_source);
    river_cloud_xiaozhi_capture_playback_service_view(&source->service_view);
    source->stop_pending = g_river_cloud.xiaozhi_playback_runtime_truth.stop_pending;
    source->rebuffer_pending =
        g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_pending;
    source->physical_active = river_cloud_xiaozhi_playback_physical_active();
    source->waiting_next_segment =
        river_cloud_xiaozhi_compute_playback_waiting_next_segment_from_source(
            &supply_source);
}

static river_cloud_playback_backend_state_t
river_cloud_xiaozhi_playback_backend_state(void)
{
    river_cloud_xiaozhi_playback_backend_source_t source;

    river_cloud_xiaozhi_capture_playback_backend_source(&source);
    return river_cloud_xiaozhi_compute_playback_backend_state_from_source(&source);
}

static bool river_cloud_xiaozhi_playback_backend_source_output_active(
    const river_cloud_xiaozhi_playback_backend_source_t *source)
{
    return source != NULL && (source->physical_active || source->stop_pending);
}

static bool river_cloud_xiaozhi_playback_backend_stream_attached(
    river_cloud_playback_backend_state_t state)
{
    return state == RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_ACTIVE ||
           state == RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_PAUSED ||
           state == RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_RECOVERING;
}

static bool river_cloud_xiaozhi_playback_backend_output_active(
    river_cloud_playback_backend_state_t state)
{
    return state == RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_ACTIVE;
}

static river_cloud_playback_hold_kind_t
river_cloud_xiaozhi_playback_hold_kind_from_source(
    const river_cloud_xiaozhi_playback_backend_source_t *source)
{
    river_cloud_playback_backend_state_t backend_state;

    if (source == NULL) {
        return RIVER_CLOUD_PLAYBACK_HOLD_NONE;
    }

    backend_state =
        river_cloud_xiaozhi_compute_playback_backend_state_from_source(source);
    if (backend_state == RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_PAUSED &&
        !source->rebuffer_pending && source->waiting_next_segment) {
        return RIVER_CLOUD_PLAYBACK_HOLD_SEGMENT_GAP;
    }

    return RIVER_CLOUD_PLAYBACK_HOLD_NONE;
}

static river_cloud_playback_hold_kind_t __attribute__((unused))
river_cloud_xiaozhi_playback_hold_kind(void)
{
    river_cloud_xiaozhi_playback_backend_source_t source;

    river_cloud_xiaozhi_capture_playback_backend_source(&source);
    return river_cloud_xiaozhi_playback_hold_kind_from_source(&source);
}

river_cloud_playback_phase_t river_cloud_xiaozhi_playback_phase(void)
{
    return g_river_cloud.xiaozhi_playback_runtime_truth.phase;
}

river_cloud_playback_rebuffer_cause_t
river_cloud_xiaozhi_playback_rebuffer_cause(void)
{
    return g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_cause;
}

static river_cloud_playback_recovery_path_t
river_cloud_xiaozhi_playback_recovery_path(void)
{
    return g_river_cloud.xiaozhi_playback_runtime_truth.recovery_path;
}

static river_cloud_playback_recovery_outcome_t
river_cloud_xiaozhi_playback_recovery_outcome(void)
{
    return g_river_cloud.xiaozhi_playback_runtime_truth.recovery_outcome;
}

bool river_cloud_xiaozhi_playback_rebuffer_pending(void)
{
    return g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_pending;
}

static void river_cloud_xiaozhi_clear_playback_context_truth(
    river_cloud_xiaozhi_playback_context_truth_t *context)
{
    if (context == NULL) {
        return;
    }

    context->response_id[0] = '\0';
    context->playback_id[0] = '\0';
    context->segment_id[0] = '\0';
}

static bool river_cloud_xiaozhi_playback_response_context_valid_from_truth(
    const river_cloud_xiaozhi_playback_context_truth_t *context)
{
    return context != NULL && context->response_id[0] != '\0' &&
           context->playback_id[0] != '\0';
}

static bool river_cloud_xiaozhi_playback_segment_context_valid_from_truth(
    const river_cloud_xiaozhi_playback_context_truth_t *context)
{
    return river_cloud_xiaozhi_playback_response_context_valid_from_truth(context) &&
           context->segment_id[0] != '\0';
}

static void river_cloud_xiaozhi_copy_playback_context_truth(
    river_cloud_xiaozhi_playback_context_truth_t *dst,
    const river_cloud_xiaozhi_playback_context_truth_t *src)
{
    if (dst == NULL) {
        return;
    }

    if (src == NULL) {
        river_cloud_xiaozhi_clear_playback_context_truth(dst);
        return;
    }

    river_cloud_xiaozhi_copy_optional_text(dst->response_id,
                                           sizeof(dst->response_id),
                                           src->response_id);
    river_cloud_xiaozhi_copy_optional_text(dst->playback_id,
                                           sizeof(dst->playback_id),
                                           src->playback_id);
    river_cloud_xiaozhi_copy_optional_text(dst->segment_id,
                                           sizeof(dst->segment_id),
                                           src->segment_id);
}

static bool river_cloud_xiaozhi_playback_same_segment_context(
    const river_cloud_xiaozhi_playback_context_truth_t *lhs,
    const river_cloud_xiaozhi_playback_context_truth_t *rhs)
{
    return river_cloud_xiaozhi_playback_segment_context_valid_from_truth(lhs) &&
           river_cloud_xiaozhi_playback_segment_context_valid_from_truth(rhs) &&
           strcmp(lhs->response_id, rhs->response_id) == 0 &&
           strcmp(lhs->playback_id, rhs->playback_id) == 0 &&
           strcmp(lhs->segment_id, rhs->segment_id) == 0;
}

static bool river_cloud_xiaozhi_playback_wait_context_valid(void)
{
    return river_cloud_xiaozhi_playback_segment_context_valid_from_truth(
        &g_river_cloud.xiaozhi_playback_terminal_truth.wait_context);
}

static void river_cloud_xiaozhi_capture_playback_supply_source(
    river_cloud_xiaozhi_playback_supply_source_t *source)
{
    if (source == NULL) {
        return;
    }

    memset(source, 0, sizeof(*source));
    source->wait_context_valid = river_cloud_xiaozhi_playback_wait_context_valid();
    source->last_segment_observed =
        river_cloud_xiaozhi_playback_last_segment_observed();
    source->segment_count =
        g_river_cloud.xiaozhi_playback_segment_queue_truth.count;
}

static bool river_cloud_xiaozhi_compute_playback_waiting_next_segment_from_source(
    const river_cloud_xiaozhi_playback_supply_source_t *source)
{
    return source != NULL && source->wait_context_valid && source->segment_count == 0U;
}

static bool __attribute__((unused)) river_cloud_xiaozhi_playback_waiting_next_segment(void)
{
    river_cloud_xiaozhi_playback_supply_source_t source;

    river_cloud_xiaozhi_capture_playback_supply_source(&source);
    return river_cloud_xiaozhi_compute_playback_waiting_next_segment_from_source(
        &source);
}

static void river_cloud_xiaozhi_capture_playback_phase_source(
    river_cloud_xiaozhi_playback_phase_source_t *source)
{
    river_cloud_xiaozhi_playback_supply_source_t supply_source;

    if (source == NULL) {
        return;
    }

    memset(source, 0, sizeof(*source));
    river_cloud_xiaozhi_capture_playback_supply_source(&supply_source);
    source->stop_pending = g_river_cloud.xiaozhi_playback_runtime_truth.stop_pending;
    source->rebuffer_pending =
        g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_pending;
    source->physical_active = river_cloud_xiaozhi_playback_physical_active();
    source->queued_frames = river_cloud_xiaozhi_playback_queued_frames();
    source->segment_count = supply_source.segment_count;
    source->waiting_next_segment =
        river_cloud_xiaozhi_compute_playback_waiting_next_segment_from_source(
            &supply_source);
}

static river_cloud_playback_phase_t river_cloud_xiaozhi_compute_playback_phase_from_source(
    const river_cloud_xiaozhi_playback_phase_source_t *source)
{
    if (source == NULL) {
        return RIVER_CLOUD_PLAYBACK_PHASE_IDLE;
    }

    if (source->stop_pending) {
        return RIVER_CLOUD_PLAYBACK_PHASE_DRAINING;
    }
    if (source->rebuffer_pending) {
        return RIVER_CLOUD_PLAYBACK_PHASE_REBUFFERING;
    }
    if (source->physical_active) {
        return RIVER_CLOUD_PLAYBACK_PHASE_PLAYING;
    }
    if (source->queued_frames != 0U || source->segment_count != 0U) {
        return RIVER_CLOUD_PLAYBACK_PHASE_PREFETCHING;
    }
    if (source->waiting_next_segment) {
        return RIVER_CLOUD_PLAYBACK_PHASE_WAITING_SEGMENT;
    }
    return RIVER_CLOUD_PLAYBACK_PHASE_IDLE;
}

static bool river_cloud_xiaozhi_refresh_playback_phase(const char *reason)
{
    river_cloud_xiaozhi_playback_phase_source_t source;
    river_cloud_playback_phase_t old_phase =
        g_river_cloud.xiaozhi_playback_runtime_truth.phase;

    river_cloud_xiaozhi_capture_playback_phase_source(&source);
    river_cloud_playback_phase_t new_phase =
        river_cloud_xiaozhi_compute_playback_phase_from_source(&source);

    if (old_phase == new_phase) {
        return false;
    }

    g_river_cloud.xiaozhi_playback_runtime_truth.phase = new_phase;
    RIVER_LOGI("xiaozhi playback phase: %s -> %s reason=%s queued=%lu segments=%lu wait_next=%s physical=%s backend=%s rebuffer=%s cause=%s stop=%s",
               river_cloud_playback_phase_name(old_phase),
               river_cloud_playback_phase_name(new_phase),
               reason != NULL && reason[0] != '\0' ? reason : "-",
               (unsigned long)source.queued_frames,
               (unsigned long)source.segment_count,
               source.waiting_next_segment ? "yes" : "no",
               source.physical_active ? "yes" : "no",
               river_cloud_playback_backend_state_name(
                   river_cloud_xiaozhi_playback_backend_state()),
               source.rebuffer_pending ? "yes" : "no",
               river_cloud_playback_rebuffer_cause_name(
                   river_cloud_xiaozhi_playback_rebuffer_cause()),
               source.stop_pending ? "yes" : "no");
    river_cloud_request_state_sync(reason != NULL && reason[0] != '\0' ? reason :
                                                                       "playback_phase");
    return true;
}

static bool river_cloud_xiaozhi_playback_terminal_open(void)
{
    return g_river_cloud.xiaozhi_playback_terminal_truth.state_kind ==
           RIVER_CLOUD_PLAYBACK_TERMINAL_STATE_NONE;
}

static const char *river_cloud_playback_terminal_wait_kind_name(
    river_cloud_playback_terminal_wait_kind_t kind)
{
    switch (kind) {
    case RIVER_CLOUD_PLAYBACK_TERMINAL_WAIT_LAST_SEGMENT_META:
        return "await_last_segment_meta";
    case RIVER_CLOUD_PLAYBACK_TERMINAL_WAIT_QUEUE_DRAIN:
        return "await_segment_queue_drain";
    case RIVER_CLOUD_PLAYBACK_TERMINAL_WAIT_LAST_SEGMENT_TAIL:
        return "await_last_segment_tail";
    default:
        return NULL;
    }
}

static void river_cloud_xiaozhi_clear_playback_terminal_wait(void)
{
    g_river_cloud.xiaozhi_playback_terminal_truth.waiting = false;
    g_river_cloud.xiaozhi_playback_terminal_truth.wait_kind =
        RIVER_CLOUD_PLAYBACK_TERMINAL_WAIT_NONE;
    g_river_cloud.xiaozhi_playback_terminal_truth.wait_reason[0] = '\0';
}

static void river_cloud_xiaozhi_set_playback_terminal_wait(
    river_cloud_playback_terminal_wait_kind_t kind)
{
    const char *reason = river_cloud_playback_terminal_wait_kind_name(kind);

    if (kind == RIVER_CLOUD_PLAYBACK_TERMINAL_WAIT_NONE || reason == NULL ||
        reason[0] == '\0') {
        river_cloud_xiaozhi_clear_playback_terminal_wait();
        return;
    }

    g_river_cloud.xiaozhi_playback_terminal_truth.waiting = true;
    g_river_cloud.xiaozhi_playback_terminal_truth.wait_kind = kind;
    river_cloud_xiaozhi_copy_optional_text(
        g_river_cloud.xiaozhi_playback_terminal_truth.wait_reason,
        sizeof(g_river_cloud.xiaozhi_playback_terminal_truth.wait_reason),
        reason);
}

static void river_cloud_xiaozhi_set_playback_terminal_state(
    river_cloud_playback_terminal_state_t state,
    const char *reason)
{
    river_cloud_xiaozhi_clear_playback_terminal_wait();
    g_river_cloud.xiaozhi_playback_terminal_truth.state_kind = state;
    river_cloud_xiaozhi_copy_optional_text(
                                           g_river_cloud.xiaozhi_playback_terminal_truth.clear_reason,
                                           sizeof(g_river_cloud.xiaozhi_playback_terminal_truth.clear_reason),
                                           reason);
}

static river_cloud_playback_terminal_state_t
river_cloud_xiaozhi_playback_terminal_state_from_ack(const char *ack)
{
    if (ack == NULL || ack[0] == '\0') {
        return RIVER_CLOUD_PLAYBACK_TERMINAL_STATE_NONE;
    }

    if (strcmp(ack, "completed") == 0) {
        return RIVER_CLOUD_PLAYBACK_TERMINAL_STATE_COMPLETED;
    }

    if (strcmp(ack, "cleared") == 0) {
        return RIVER_CLOUD_PLAYBACK_TERMINAL_STATE_CLEARED;
    }

    return RIVER_CLOUD_PLAYBACK_TERMINAL_STATE_NONE;
}

static bool river_cloud_xiaozhi_playback_last_segment_observed(void)
{
    return river_cloud_xiaozhi_playback_segment_context_valid_from_truth(
        &g_river_cloud.xiaozhi_playback_terminal_truth.last_segment_context);
}

static bool river_cloud_xiaozhi_playback_response_context_valid(void)
{
    return river_cloud_xiaozhi_playback_response_context_valid_from_truth(
        &g_river_cloud.xiaozhi_playback_meta_truth.current_context);
}

static bool river_cloud_xiaozhi_playback_segment_context_valid(void)
{
    return river_cloud_xiaozhi_playback_segment_context_valid_from_truth(
        &g_river_cloud.xiaozhi_playback_meta_truth.current_context);
}

static bool river_cloud_xiaozhi_playback_current_meta_is_last_segment(void)
{
    if (!river_cloud_xiaozhi_playback_segment_context_valid() ||
        !river_cloud_xiaozhi_playback_last_segment_observed()) {
        return false;
    }

    return river_cloud_xiaozhi_playback_same_segment_context(
        &g_river_cloud.xiaozhi_playback_meta_truth.current_context,
        &g_river_cloud.xiaozhi_playback_terminal_truth.last_segment_context);
}

static void river_cloud_xiaozhi_clear_playback_wait_context(void)
{
    river_cloud_xiaozhi_clear_playback_context_truth(
        &g_river_cloud.xiaozhi_playback_terminal_truth.wait_context);
}

static void river_cloud_xiaozhi_store_playback_wait_context(void)
{
    river_cloud_xiaozhi_copy_playback_context_truth(
        &g_river_cloud.xiaozhi_playback_terminal_truth.wait_context,
        &g_river_cloud.xiaozhi_playback_meta_truth.current_context);
}

static void river_cloud_xiaozhi_store_playback_last_segment_context(void)
{
    river_cloud_xiaozhi_copy_playback_context_truth(
        &g_river_cloud.xiaozhi_playback_terminal_truth.last_segment_context,
        &g_river_cloud.xiaozhi_playback_meta_truth.current_context);
}

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
    uint64_t meta_gap_ms;
    uint64_t reopen_guard_left_ms;
    uint32_t rebuffer_count;
    uint32_t rebuffer_streak;
    bool downlink_started;
    bool terminal_closed;
    bool terminal_waiting;
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
    bool inline_success;
    bool managed_rebuffer;
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
    uint32_t queued_frames;
    bool ready;
} river_cloud_xiaozhi_downlink_cycle_plan_t;

typedef struct {
    river_cloud_xiaozhi_downlink_task_step_result_t step_result;
    bool acquired;
} river_cloud_xiaozhi_downlink_frame_acquire_result_t;

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

static bool river_cloud_xiaozhi_rebuffer_prefers_service_recover(
    river_cloud_playback_rebuffer_cause_t cause,
    river_cloud_playback_supply_kind_t supply_kind);
static bool river_cloud_xiaozhi_write_failed_prefers_starved_rebuffer(
    uint32_t queued_frames,
    uint64_t now_ms,
    uint64_t *supply_gap_ms_out);
static river_cloud_xiaozhi_rebuffer_recovery_result_t
river_cloud_xiaozhi_request_playback_rebuffer_recovery(
    const char *reason,
    river_cloud_playback_rebuffer_cause_t cause,
    river_cloud_playback_supply_kind_t supply_kind);

static const char *river_cloud_xiaozhi_playback_recovery_path_label(
    river_cloud_playback_recovery_path_t path,
    const char *fallback)
{
    const char *name = river_cloud_playback_recovery_path_name(path);

    if (name != NULL) {
        return name;
    }

    return fallback != NULL ? fallback : "-";
}

static const char *river_cloud_xiaozhi_playback_recovery_outcome_label(
    river_cloud_playback_recovery_outcome_t outcome,
    const char *fallback)
{
    const char *name = river_cloud_playback_recovery_outcome_name(outcome);

    if (name != NULL) {
        return name;
    }

    return fallback != NULL ? fallback : "-";
}

static bool river_cloud_xiaozhi_downlink_retry_frame_pending(void)
{
    return g_river_cloud.xiaozhi_downlink_runtime_truth.retry_valid;
}

static void river_cloud_xiaozhi_keep_current_downlink_frame_for_retry(void)
{
    g_river_cloud.xiaozhi_downlink_runtime_truth.retry_valid = true;
}

static void river_cloud_xiaozhi_consume_current_downlink_frame(void)
{
    g_river_cloud.xiaozhi_downlink_runtime_truth.retry_valid = false;
}

uint32_t river_cloud_xiaozhi_playback_queued_frames(void)
{
    uint32_t ready_frames;

    ready_frames = river_audio_frame_ring_count(&g_river_cloud.xiaozhi_downlink_ring);
    if (river_cloud_xiaozhi_downlink_retry_frame_pending() &&
        ready_frames < UINT32_MAX) {
        ready_frames++;
    }
    return ready_frames;
}

bool river_cloud_xiaozhi_playback_output_active(void)
{
    river_cloud_xiaozhi_playback_truth_view_t truth_view;

    river_cloud_xiaozhi_capture_playback_truth_view(&truth_view);
    return truth_view.output_active;
}

bool river_cloud_xiaozhi_playback_lane_engaged(void)
{
    river_cloud_xiaozhi_playback_truth_view_t truth_view;

    river_cloud_xiaozhi_capture_playback_truth_view(&truth_view);
    return river_cloud_xiaozhi_playback_lane_engaged_from_truth_view(&truth_view);
}

bool river_cloud_xiaozhi_playback_turn_active(void)
{
    river_cloud_xiaozhi_playback_truth_view_t truth_view;

    river_cloud_xiaozhi_capture_playback_truth_view(&truth_view);
    return river_cloud_xiaozhi_playback_turn_active_from_truth_view(&truth_view);
}

bool river_cloud_xiaozhi_playback_has_work(void)
{
    return river_cloud_xiaozhi_playback_queued_frames() != 0U ||
           river_cloud_xiaozhi_downlink_retry_frame_pending() ||
           river_cloud_xiaozhi_playback_output_active() ||
           g_river_cloud.xiaozhi_playback_runtime_truth.stop_pending ||
           g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_pending;
}

static void river_cloud_xiaozhi_clear_followup_reopen_state(void)
{
    g_river_cloud.xiaozhi_playback_gate_truth.no_ref_reopen_rearm = false;
    g_river_cloud.xiaozhi_playback_gate_truth.no_ref_reopen_silence_frames = 0U;
    g_river_cloud.xiaozhi_playback_gate_truth.no_ref_reopen_guard_deadline_ms = 0U;
}

static bool river_cloud_xiaozhi_playback_quiet_window_allows_vad_open(void)
{
    river_cloud_xiaozhi_playback_gate_view_t gate_view;

    river_cloud_xiaozhi_capture_playback_gate_view(&gate_view);
    return river_cloud_xiaozhi_playback_quiet_window_from_gate_view(&gate_view);
}

bool river_cloud_xiaozhi_playback_allows_vad_open(void)
{
    river_voice_duplex_ready_eval_t eval;
    const char *fallback_reason;

    if (river_cloud_xiaozhi_playback_quiet_window_allows_vad_open()) {
        return true;
    }

    river_cloud_xiaozhi_get_duplex_ready_eval(&eval);
    fallback_reason = river_cloud_xiaozhi_duplex_fallback_reason(&eval);
    if (fallback_reason == NULL) {
        river_cloud_xiaozhi_playback_note_duplex_ready();
    }
    return fallback_reason == NULL;
}

bool river_cloud_xiaozhi_capture_held_by_playback(
    const river_voice_duplex_ready_eval_t *eval,
    const char **fallback_reason)
{
    river_cloud_xiaozhi_playback_gate_view_t gate_view;
    const char *resolved_reason = river_cloud_xiaozhi_duplex_fallback_reason(eval);

    river_cloud_xiaozhi_capture_playback_gate_view(&gate_view);
    if (river_cloud_xiaozhi_playback_quiet_window_from_gate_view(&gate_view)) {
        if (fallback_reason != NULL) {
            *fallback_reason = NULL;
        }
        return false;
    }

    if (fallback_reason != NULL) {
        *fallback_reason = resolved_reason;
    }

    return river_cloud_xiaozhi_playback_lane_engaged_from_truth_view(
               &gate_view.truth_view) &&
           resolved_reason != NULL;
}

void river_cloud_xiaozhi_apply_tts_start_round_policy(void)
{
    river_voice_duplex_ready_eval_t duplex_eval;
    const char *fallback_reason;
    bool capture_held;
    river_cloud_xiaozhi_playback_backend_source_t playback_source;
    river_cloud_playback_backend_state_t backend_state;

    river_cloud_xiaozhi_get_duplex_ready_eval(&duplex_eval);
    capture_held = river_cloud_xiaozhi_capture_held_by_playback(&duplex_eval,
                                                                &fallback_reason);
    river_cloud_xiaozhi_capture_playback_backend_source(&playback_source);
    backend_state = river_cloud_xiaozhi_compute_playback_backend_state_from_source(
        &playback_source);

    if (!capture_held) {
        RIVER_LOGI("xiaozhi tts_start keeps local round open: capture_held=no fallback=%s duplex_default_on=%s duplex_ready=%s reason=%s aec=%s playback=%s/%s error=%s ref_state=%s ref_activity=%s ref_peak=%u ref_ratio_q15=%u phase=%s backend=%s stream=%s stop_pending=%s close_pending=%s",
                   fallback_reason != NULL ? fallback_reason : "-",
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
                   (unsigned int)duplex_eval.native_reference_ratio_q15,
                   river_cloud_playback_phase_name(
                       river_cloud_xiaozhi_playback_observed_phase_kind()),
                   river_cloud_playback_backend_state_name(backend_state),
                   g_river_cloud.stream_active ? "yes" : "no",
                   g_river_cloud.xiaozhi_session_window_truth.listen_stop_pending ? "yes" : "no",
                   g_river_cloud.xiaozhi_session_window_truth.local_close_pending ? "yes" : "no");
        return;
    }

    river_cloud_xiaozhi_note_semantic_fallback(fallback_reason);
    RIVER_LOGI("xiaozhi tts_start falls back to round close: capture_held=yes fallback=%s duplex_default_on=%s duplex_ready=%s reason=%s aec=%s playback=%s/%s error=%s ref_state=%s ref_activity=%s ref_peak=%u ref_ratio_q15=%u ref_queue=%lu/%lu ref_age_ms=%lu phase=%s backend=%s",
               fallback_reason,
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
               (unsigned int)duplex_eval.native_reference_ratio_q15,
               (unsigned long)duplex_eval.reference_queue_frames,
               (unsigned long)duplex_eval.reference_queue_peak_frames,
               (unsigned long)duplex_eval.reference_last_write_age_ms,
               river_cloud_playback_phase_name(
                   river_cloud_xiaozhi_playback_observed_phase_kind()),
               river_cloud_playback_backend_state_name(backend_state));
    river_cloud_xiaozhi_close_local_round_for_cause(
        RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_SERVER_RESPONSE,
        "tts_start");
}

static void river_cloud_xiaozhi_apply_playback_started_round_policy(void)
{
    river_voice_duplex_ready_eval_t duplex_eval;
    const char *fallback_reason;
    river_cloud_xiaozhi_playback_backend_source_t playback_source;
    river_cloud_playback_backend_state_t backend_state;

    if (!g_river_cloud.stream_active && !g_river_cloud.xiaozhi_session_window_truth.listen_stop_pending &&
        !g_river_cloud.xiaozhi_session_window_truth.local_close_pending) {
        return;
    }

    river_cloud_xiaozhi_get_duplex_ready_eval(&duplex_eval);
    if (!river_cloud_xiaozhi_capture_held_by_playback(&duplex_eval,
                                                      &fallback_reason)) {
        return;
    }

    river_cloud_xiaozhi_capture_playback_backend_source(&playback_source);
    backend_state = river_cloud_xiaozhi_compute_playback_backend_state_from_source(
        &playback_source);
    river_cloud_xiaozhi_note_semantic_fallback(fallback_reason);
    RIVER_LOGI("xiaozhi playback_started falls back to round close: capture_held=yes fallback=%s duplex_default_on=%s duplex_ready=%s reason=%s aec=%s playback=%s/%s error=%s ref_state=%s ref_activity=%s ref_peak=%u ref_ratio_q15=%u phase=%s backend=%s stream=%s stop_pending=%s close_pending=%s",
               fallback_reason,
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
               (unsigned int)duplex_eval.native_reference_ratio_q15,
               river_cloud_playback_phase_name(
                   river_cloud_xiaozhi_playback_observed_phase_kind()),
               river_cloud_playback_backend_state_name(backend_state),
               g_river_cloud.stream_active ? "yes" : "no",
               g_river_cloud.xiaozhi_session_window_truth.listen_stop_pending ? "yes" : "no",
               g_river_cloud.xiaozhi_session_window_truth.local_close_pending ? "yes" : "no");
    river_cloud_xiaozhi_close_local_round_for_cause(
        RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_SERVER_RESPONSE,
        "playback_started");
}

static bool river_cloud_xiaozhi_output_speaking_active(void)
{
    river_cloud_xiaozhi_playback_truth_view_t truth_view;
    river_cloud_xiaozhi_turn_semantics_view_t semantics_view;

    river_cloud_xiaozhi_capture_playback_truth_view(&truth_view);

    if (truth_view.output_active) {
        return true;
    }

    river_cloud_xiaozhi_capture_turn_semantics_view(&semantics_view);
    if (semantics_view.output_state == NULL ||
        strcmp(semantics_view.output_state, "speaking") != 0) {
        return false;
    }

    return river_cloud_xiaozhi_playback_truth_view_retains_output_turn(
        &truth_view);
}

bool river_cloud_xiaozhi_duplex_soft_endpoint_enabled(void)
{
    return river_cloud_xiaozhi_output_speaking_active() &&
           river_cloud_xiaozhi_playback_allows_vad_open();
}

bool river_cloud_xiaozhi_duplex_speaking_uplink_continuation_active(void)
{
    return g_river_cloud.stream_active &&
           river_cloud_xiaozhi_duplex_soft_endpoint_enabled();
}

bool river_cloud_xiaozhi_endpoint_soft_close_pending(void)
{
    return g_river_cloud.xiaozhi_endpoint_soft_close_truth.pending;
}

uint64_t river_cloud_xiaozhi_endpoint_soft_close_remaining_ms(uint64_t now_ms)
{
    if (!g_river_cloud.xiaozhi_endpoint_soft_close_truth.pending ||
        g_river_cloud.xiaozhi_endpoint_soft_close_truth.deadline_ms <= now_ms) {
        return 0U;
    }

    return g_river_cloud.xiaozhi_endpoint_soft_close_truth.deadline_ms - now_ms;
}

const char *river_cloud_xiaozhi_endpoint_soft_close_reason(void)
{
    return g_river_cloud.xiaozhi_endpoint_soft_close_truth.reason[0] != '\0' ?
               g_river_cloud.xiaozhi_endpoint_soft_close_truth.reason :
               NULL;
}

void river_cloud_xiaozhi_clear_endpoint_soft_close_state(void)
{
    g_river_cloud.xiaozhi_endpoint_soft_close_truth.pending = false;
    g_river_cloud.xiaozhi_endpoint_soft_close_truth.deadline_ms = 0U;
    g_river_cloud.xiaozhi_endpoint_soft_close_truth.reason[0] = '\0';
}

void river_cloud_xiaozhi_cancel_endpoint_soft_close(const char *trigger)
{
    if (!river_cloud_xiaozhi_endpoint_soft_close_pending()) {
        return;
    }

    RIVER_LOGI("xiaozhi deferred local close cancelled: trigger=%s reason=%s",
               trigger != NULL ? trigger : "-",
               river_cloud_xiaozhi_endpoint_soft_close_reason() != NULL ?
                   river_cloud_xiaozhi_endpoint_soft_close_reason() :
                   "-");
    river_cloud_xiaozhi_clear_endpoint_soft_close_state();
}

void river_cloud_xiaozhi_note_interrupt_hint(const char *trigger, const char *reason)
{
    if (!river_cloud_xiaozhi_duplex_soft_endpoint_enabled()) {
        return;
    }

    RIVER_LOGI("xiaozhi interrupt hint: trigger=%s reason=%s preview_id=%s stream=%s playback_physical=%s phase=%s backend=%s",
               trigger != NULL ? trigger : "-",
               reason != NULL ? reason : "-",
               g_river_cloud.xiaozhi_preview_transcript_truth.preview_id[0] != '\0' ?
                   g_river_cloud.xiaozhi_preview_transcript_truth.preview_id :
                   "-",
               g_river_cloud.stream_active ? "yes" : "no",
               river_cloud_xiaozhi_playback_physical_active() ? "yes" : "no",
               river_cloud_playback_phase_name(
                   river_cloud_xiaozhi_playback_phase()),
               river_cloud_playback_backend_state_name(
                   river_cloud_xiaozhi_playback_backend_state()));
}

void river_cloud_xiaozhi_arm_endpoint_soft_close(const char *trigger, const char *reason)
{
    if (!river_cloud_xiaozhi_duplex_soft_endpoint_enabled() ||
        !g_river_cloud.stream_active ||
        g_river_cloud.xiaozhi_session_window_truth.listen_stop_pending ||
        river_cloud_xiaozhi_endpoint_soft_close_pending()) {
        return;
    }

    river_cloud_xiaozhi_copy_optional_text(
        g_river_cloud.xiaozhi_endpoint_soft_close_truth.reason,
        sizeof(g_river_cloud.xiaozhi_endpoint_soft_close_truth.reason),
        reason);
    g_river_cloud.xiaozhi_endpoint_soft_close_truth.pending = true;
    g_river_cloud.xiaozhi_endpoint_soft_close_truth.deadline_ms =
        (uint64_t)rtos_time_get_current_system_time_ms() +
        (uint64_t)RIVER_CLOUD_XIAOZHI_ENDPOINT_SOFT_CLOSE_DEFER_MS;
    RIVER_LOGI("xiaozhi hint-only endpoint: trigger=%s reason=%s wait_ms=%u stream=%s playback_physical=%s phase=%s backend=%s",
               trigger != NULL ? trigger : "-",
               river_cloud_xiaozhi_endpoint_soft_close_reason() != NULL ?
                   river_cloud_xiaozhi_endpoint_soft_close_reason() :
                   "-",
               (unsigned int)RIVER_CLOUD_XIAOZHI_ENDPOINT_SOFT_CLOSE_DEFER_MS,
               g_river_cloud.stream_active ? "yes" : "no",
               river_cloud_xiaozhi_playback_physical_active() ? "yes" : "no",
               river_cloud_playback_phase_name(
                   river_cloud_xiaozhi_playback_phase()),
               river_cloud_playback_backend_state_name(
                   river_cloud_xiaozhi_playback_backend_state()));
}

bool river_cloud_xiaozhi_poll_endpoint_soft_close_timeout(char *reason, size_t reason_size)
{
    uint64_t now_ms;

    if (!river_cloud_xiaozhi_endpoint_soft_close_pending()) {
        return false;
    }

    if (!g_river_cloud.stream_active || g_river_cloud.xiaozhi_session_window_truth.listen_stop_pending) {
        river_cloud_xiaozhi_clear_endpoint_soft_close_state();
        return false;
    }

    if (river_cloud_xiaozhi_duplex_speaking_uplink_continuation_active()) {
        return false;
    }

    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    if (river_cloud_xiaozhi_endpoint_soft_close_remaining_ms(now_ms) != 0U) {
        return false;
    }

    river_cloud_xiaozhi_copy_optional_text(
        reason,
        reason_size,
        river_cloud_xiaozhi_endpoint_soft_close_reason() != NULL ?
            river_cloud_xiaozhi_endpoint_soft_close_reason() :
            "server_endpoint_candidate");
    RIVER_LOGI("xiaozhi deferred local close resolved: trigger=endpoint_soft_close_timeout reason=%s",
               reason != NULL && reason[0] != '\0' ? reason : "-");
    return true;
}

uint32_t river_cloud_xiaozhi_open_hold_frames_required(void)
{
    if (!g_river_cloud.xiaozhi_session_window_truth.window_active ||
        river_cloud_xiaozhi_playback_allows_vad_open()) {
        return RIVER_CLOUD_XIAOZHI_OPEN_HOLD_FRAMES;
    }

    return RIVER_CLOUD_XIAOZHI_NOREF_OPEN_HOLD_FRAMES;
}

bool river_cloud_xiaozhi_playback_followup_reopen_ready(bool is_speech)
{
    uint64_t now_ms;
    bool guard_active;

    if (!g_river_cloud.xiaozhi_session_window_truth.window_active ||
        river_cloud_xiaozhi_playback_allows_vad_open()) {
        return true;
    }

    if (river_cloud_xiaozhi_playback_lane_engaged()) {
        return false;
    }

    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    guard_active =
        g_river_cloud.xiaozhi_playback_gate_truth.no_ref_reopen_guard_deadline_ms != 0U &&
        now_ms < g_river_cloud.xiaozhi_playback_gate_truth.no_ref_reopen_guard_deadline_ms;

    if (g_river_cloud.xiaozhi_playback_gate_truth.no_ref_reopen_rearm) {
        if (is_speech) {
            g_river_cloud.xiaozhi_playback_gate_truth.no_ref_reopen_silence_frames = 0U;
        } else if (g_river_cloud.xiaozhi_playback_gate_truth.no_ref_reopen_silence_frames <
                   RIVER_CLOUD_XIAOZHI_NOREF_REARM_SILENCE_FRAMES) {
            g_river_cloud.xiaozhi_playback_gate_truth.no_ref_reopen_silence_frames++;
        }

        if (!guard_active &&
            g_river_cloud.xiaozhi_playback_gate_truth.no_ref_reopen_silence_frames >=
                RIVER_CLOUD_XIAOZHI_NOREF_REARM_SILENCE_FRAMES) {
            g_river_cloud.xiaozhi_playback_gate_truth.no_ref_reopen_rearm = false;
            g_river_cloud.xiaozhi_playback_gate_truth.no_ref_reopen_silence_frames = 0U;
            g_river_cloud.xiaozhi_playback_gate_truth.no_ref_reopen_guard_deadline_ms = 0U;
            RIVER_LOGI("xiaozhi no_ref reopen rearmed after silence: frames=%u",
                       (unsigned int)RIVER_CLOUD_XIAOZHI_NOREF_REARM_SILENCE_FRAMES);
            return true;
        }
    }

    if (!guard_active) {
        g_river_cloud.xiaozhi_playback_gate_truth.no_ref_reopen_guard_deadline_ms = 0U;
    }

    return !guard_active && !g_river_cloud.xiaozhi_playback_gate_truth.no_ref_reopen_rearm;
}

static void river_cloud_xiaozhi_clear_downlink_starvation_watch(void)
{
    g_river_cloud.xiaozhi_downlink_runtime_truth.starved_since_ms = 0U;
}

static bool river_cloud_xiaozhi_rebuffer_prefetch_target_needed(void)
{
    return g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_pending &&
           river_cloud_xiaozhi_playback_rebuffer_cause() ==
               RIVER_CLOUD_PLAYBACK_REBUFFER_CAUSE_UPSTREAM_STARVED;
}

static uint32_t river_cloud_xiaozhi_playback_prefetch_target_frames(uint32_t frame_ms)
{
    if (frame_ms == 0U ||
        g_river_cloud.xiaozhi_playback_meta_truth.prefetch_target_ms == 0U) {
        return 0U;
    }

    return (g_river_cloud.xiaozhi_playback_meta_truth.prefetch_target_ms + frame_ms - 1U) /
           frame_ms;
}

static const river_cloud_xiaozhi_playback_segment_t *
river_cloud_xiaozhi_playback_prefetch_target_segment(void)
{
    river_cloud_xiaozhi_playback_segment_t *segment =
        river_cloud_xiaozhi_current_playback_segment();

    if (segment == NULL || !segment->valid || segment->segment_id[0] == '\0') {
        return NULL;
    }

    return segment;
}

static bool river_cloud_xiaozhi_segment_prefetch_target_needed(uint32_t frame_ms)
{
    const river_cloud_xiaozhi_playback_segment_t *segment;
    uint32_t base_budget_ms;

    if (frame_ms == 0U ||
        g_river_cloud.xiaozhi_playback_meta_truth.prefetch_target_ms == 0U) {
        return false;
    }

    base_budget_ms = frame_ms * RIVER_CLOUD_XIAOZHI_DOWNLINK_START_FRAMES;
    if (g_river_cloud.xiaozhi_playback_meta_truth.last_meta_gap_ms > base_budget_ms) {
        return true;
    }

    if (river_cloud_xiaozhi_playback_output_active()) {
        return false;
    }

    /* When the segment that would start next is not terminal, treat a clearly
     * longer expected duration as a predictive prefetch budget even before any
     * historical meta-gap has been observed. */
    segment = river_cloud_xiaozhi_playback_prefetch_target_segment();
    if (segment == NULL || segment->is_last_segment) {
        return false;
    }

    return g_river_cloud.xiaozhi_playback_meta_truth.prefetch_target_ms > base_budget_ms;
}

static uint32_t river_cloud_xiaozhi_downlink_frame_duration_ms(void)
{
    if (g_river_cloud.xiaozhi_downlink_stream_truth.frame_duration_ms != 0U) {
        return g_river_cloud.xiaozhi_downlink_stream_truth.frame_duration_ms;
    }
    return RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS;
}

static uint32_t river_cloud_xiaozhi_downlink_sample_rate(void)
{
    return g_river_cloud.xiaozhi_downlink_stream_truth.sample_rate;
}

static bool river_cloud_xiaozhi_downlink_worker_started(void)
{
    return g_river_cloud.xiaozhi_downlink_stream_truth.worker_started;
}

static void river_cloud_xiaozhi_note_downlink_supply(void)
{
    g_river_cloud.xiaozhi_downlink_runtime_truth.last_supply_ms =
        (uint64_t)rtos_time_get_current_system_time_ms();
}

static void river_cloud_xiaozhi_reset_downlink_ring_runtime(void)
{
    if (g_river_cloud.xiaozhi_downlink_ring.initialized) {
        river_audio_frame_ring_reset(&g_river_cloud.xiaozhi_downlink_ring);
    }
    river_cloud_xiaozhi_consume_current_downlink_frame();
    g_river_cloud.xiaozhi_downlink_runtime_truth.last_supply_ms = 0U;
    river_cloud_xiaozhi_clear_downlink_starvation_watch();
}

static river_status_t river_cloud_xiaozhi_ensure_downlink_ring(size_t frame_bytes)
{
    if (g_river_cloud.xiaozhi_downlink_ring.initialized &&
        g_river_cloud.xiaozhi_downlink_ring.frame_bytes != frame_bytes) {
        river_audio_frame_ring_deinit(&g_river_cloud.xiaozhi_downlink_ring);
    }
    if (g_river_cloud.xiaozhi_downlink_ring.initialized) {
        return RIVER_OK;
    }

    return river_audio_frame_ring_init_with_storage_ex(
        &g_river_cloud.xiaozhi_downlink_ring,
        g_river_cloud.xiaozhi_downlink_ring_storage,
        sizeof(g_river_cloud.xiaozhi_downlink_ring_storage),
        frame_bytes,
        RIVER_CLOUD_XIAOZHI_DOWNLINK_RING_FRAMES,
        RIVER_AUDIO_FRAME_RING_MODE_SPSC);
}

static river_status_t river_cloud_xiaozhi_write_downlink_frame_latest(
    const uint8_t *mono_frame)
{
    river_status_t status;

    status = river_audio_frame_ring_write(&g_river_cloud.xiaozhi_downlink_ring,
                                          mono_frame);
    if (status == RIVER_OK) {
        river_cloud_xiaozhi_note_downlink_supply();
        return RIVER_OK;
    }

    if (river_audio_frame_ring_read(&g_river_cloud.xiaozhi_downlink_ring,
                                    g_river_cloud.xiaozhi_downlink_drop_frame) != RIVER_OK) {
        return status;
    }

    status = river_audio_frame_ring_write(&g_river_cloud.xiaozhi_downlink_ring,
                                          mono_frame);
    if (status != RIVER_OK) {
        return status;
    }

    g_river_cloud.xiaozhi_downlink_runtime_truth.ring_dropped++;
    RIVER_LOGW("xiaozhi downlink ring overflow: dropped=%lu queued=%lu capacity=%u",
               (unsigned long)g_river_cloud.xiaozhi_downlink_runtime_truth.ring_dropped,
               (unsigned long)river_audio_frame_ring_count(
                   &g_river_cloud.xiaozhi_downlink_ring),
               (unsigned int)RIVER_CLOUD_XIAOZHI_DOWNLINK_RING_FRAMES);
    river_cloud_xiaozhi_note_downlink_supply();
    return RIVER_OK;
}

static uint32_t river_cloud_xiaozhi_server_audio_sample_rate(void)
{
    return g_river_cloud.xiaozhi_server_audio_format_truth.sample_rate;
}

static uint32_t river_cloud_xiaozhi_server_audio_frame_duration_ms(void)
{
    return g_river_cloud.xiaozhi_server_audio_format_truth.frame_duration_ms;
}

static void river_cloud_xiaozhi_note_downlink_stream_format(uint32_t sample_rate,
                                                            uint32_t frame_duration_ms)
{
    g_river_cloud.xiaozhi_downlink_stream_truth.sample_rate = sample_rate;
    g_river_cloud.xiaozhi_downlink_stream_truth.frame_duration_ms =
        frame_duration_ms;
}

static void river_cloud_xiaozhi_note_server_audio_format(uint32_t sample_rate,
                                                         uint32_t frame_duration_ms)
{
    g_river_cloud.xiaozhi_server_audio_format_truth.sample_rate = sample_rate;
    g_river_cloud.xiaozhi_server_audio_format_truth.frame_duration_ms =
        frame_duration_ms;
}

static uint32_t river_cloud_xiaozhi_downlink_max_start_frames(void)
{
    if (RIVER_CLOUD_XIAOZHI_DOWNLINK_RING_FRAMES <= 4U) {
        return RIVER_CLOUD_XIAOZHI_DOWNLINK_RING_FRAMES;
    }
    return RIVER_CLOUD_XIAOZHI_DOWNLINK_RING_FRAMES - 4U;
}

static void river_cloud_xiaozhi_store_start_gate(
    const river_cloud_xiaozhi_playback_start_gate_t *gate)
{
    if (gate == NULL) {
        return;
    }

    g_river_cloud.xiaozhi_playback_gate_truth.policy = gate->policy;
    g_river_cloud.xiaozhi_playback_gate_truth.start_frames = gate->start_frames;
    g_river_cloud.xiaozhi_playback_gate_truth.prefetch_frames = gate->prefetch_frames;
    g_river_cloud.xiaozhi_playback_gate_truth.cautious_history =
        gate->cautious_history;
}

static river_cloud_xiaozhi_playback_start_gate_t river_cloud_xiaozhi_build_start_gate(void)
{
    river_cloud_xiaozhi_playback_start_gate_t gate;
    uint32_t frame_ms = river_cloud_xiaozhi_downlink_frame_duration_ms();
    uint32_t max_start_frames = river_cloud_xiaozhi_downlink_max_start_frames();

    memset(&gate, 0, sizeof(gate));
    gate.policy = RIVER_CLOUD_PLAYBACK_START_POLICY_BASELINE;
    gate.start_frames = g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_pending ?
                            RIVER_CLOUD_XIAOZHI_DOWNLINK_REBUFFER_START_FRAMES :
                            RIVER_CLOUD_XIAOZHI_DOWNLINK_START_FRAMES;
    gate.prefetch_frames = river_cloud_xiaozhi_playback_prefetch_target_frames(frame_ms);

    if (g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_pending) {
        gate.policy = RIVER_CLOUD_PLAYBACK_START_POLICY_REBUFFER_FAST;
        if (river_cloud_xiaozhi_rebuffer_prefetch_target_needed()) {
            gate.policy = RIVER_CLOUD_PLAYBACK_START_POLICY_PREFETCH_STARVED;
        } else if (river_cloud_xiaozhi_segment_prefetch_target_needed(frame_ms)) {
            gate.policy = RIVER_CLOUD_PLAYBACK_START_POLICY_PREFETCH_SEGMENT;
        }
    } else if (river_cloud_xiaozhi_segment_prefetch_target_needed(frame_ms)) {
        gate.policy = RIVER_CLOUD_PLAYBACK_START_POLICY_PREFETCH_SEGMENT;
    }

    if ((gate.policy == RIVER_CLOUD_PLAYBACK_START_POLICY_PREFETCH_STARVED ||
         gate.policy == RIVER_CLOUD_PLAYBACK_START_POLICY_PREFETCH_SEGMENT) &&
        gate.prefetch_frames > gate.start_frames) {
        gate.start_frames = gate.prefetch_frames;
    }

    if (g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_streak > 1U) {
        uint32_t extra_frames =
            (g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_streak - 1U) *
            RIVER_CLOUD_XIAOZHI_DOWNLINK_REBUFFER_EXTRA_FRAMES;

        gate.cautious_history = true;
        if (gate.start_frames >= max_start_frames) {
            extra_frames = 0U;
        } else if (extra_frames > max_start_frames - gate.start_frames) {
            extra_frames = max_start_frames - gate.start_frames;
        }
        gate.start_frames += extra_frames;
    }

    if (gate.start_frames > max_start_frames) {
        gate.start_frames = max_start_frames;
    }
    return gate;
}

static river_cloud_xiaozhi_playback_start_gate_t river_cloud_xiaozhi_refresh_start_gate(void)
{
    river_cloud_xiaozhi_playback_start_gate_t gate = river_cloud_xiaozhi_build_start_gate();

    river_cloud_xiaozhi_store_start_gate(&gate);
    return gate;
}

static river_cloud_xiaozhi_playback_start_gate_t river_cloud_xiaozhi_current_start_gate(void)
{
    river_cloud_xiaozhi_playback_start_gate_t gate;

    memset(&gate, 0, sizeof(gate));
    if (g_river_cloud.xiaozhi_playback_gate_truth.start_frames == 0U) {
        return river_cloud_xiaozhi_refresh_start_gate();
    }

    gate.policy = g_river_cloud.xiaozhi_playback_gate_truth.policy;
    gate.start_frames = g_river_cloud.xiaozhi_playback_gate_truth.start_frames;
    gate.prefetch_frames = g_river_cloud.xiaozhi_playback_gate_truth.prefetch_frames;
    gate.cautious_history =
        g_river_cloud.xiaozhi_playback_gate_truth.cautious_history;
    return gate;
}

static void river_cloud_xiaozhi_capture_playback_diag_view(
    uint64_t now_ms,
    river_cloud_xiaozhi_playback_diag_view_t *view)
{
    river_cloud_xiaozhi_playback_meta_truth_t *meta_truth =
        &g_river_cloud.xiaozhi_playback_meta_truth;
    river_cloud_xiaozhi_playback_terminal_truth_t *terminal_truth =
        &g_river_cloud.xiaozhi_playback_terminal_truth;

    if (view == NULL) {
        return;
    }

    memset(view, 0, sizeof(*view));
    river_cloud_xiaozhi_capture_playback_truth_view(&view->truth_view);
    river_cloud_xiaozhi_capture_playback_observe_view(&view->observe_view);
    view->start_gate = river_cloud_xiaozhi_current_start_gate();
    view->rebuffer_cause_kind = river_cloud_xiaozhi_playback_rebuffer_cause();
    view->terminal_wait_kind = terminal_truth->wait_kind;
    view->terminal_state_kind = terminal_truth->state_kind;
    view->queued_frames = view->truth_view.queued_frames;
    view->resume_frames = river_cloud_xiaozhi_downlink_attached_resume_threshold_frames();
    view->buffer_budget_frames = river_cloud_xiaozhi_playback_buffer_frame_budget();
    view->open_hold_frames = river_cloud_xiaozhi_open_hold_frames_required();
    view->sample_rate = river_cloud_xiaozhi_downlink_sample_rate();
    view->frame_duration_ms = river_cloud_xiaozhi_downlink_frame_duration_ms();
    view->prefetch_target_ms = meta_truth->prefetch_target_ms;
    view->start_frames = g_river_cloud.xiaozhi_playback_gate_truth.start_frames;
    view->prefetch_frames =
        g_river_cloud.xiaozhi_playback_gate_truth.prefetch_frames;
    view->ring_dropped = g_river_cloud.xiaozhi_downlink_runtime_truth.ring_dropped;
    view->meta_gap_ms = meta_truth->last_meta_gap_ms;
    view->rebuffer_count = g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_count;
    view->rebuffer_streak =
        g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_streak;
    view->downlink_started = river_cloud_xiaozhi_downlink_worker_started();
    view->terminal_closed = !river_cloud_xiaozhi_playback_terminal_open();
    view->terminal_waiting = terminal_truth->waiting;
    view->start_cautious_history =
        g_river_cloud.xiaozhi_playback_gate_truth.cautious_history;
    view->tts_stop_pending = view->truth_view.tts_stop_pending;
    if (g_river_cloud.xiaozhi_playback_gate_truth.no_ref_reopen_guard_deadline_ms > now_ms) {
        view->reopen_guard_left_ms =
            g_river_cloud.xiaozhi_playback_gate_truth.no_ref_reopen_guard_deadline_ms - now_ms;
    }
}

static void river_cloud_xiaozhi_capture_playback_recovery_plan(
    river_cloud_xiaozhi_playback_recovery_plan_t *plan,
    river_cloud_playback_rebuffer_cause_t requested_cause,
    uint32_t queued_frames,
    uint64_t now_ms)
{
    if (plan == NULL) {
        return;
    }

    memset(plan, 0, sizeof(*plan));
    plan->cause = requested_cause;
    plan->queued_frames = queued_frames;
    plan->low_water_frames = river_cloud_xiaozhi_downlink_starved_low_water_frames();
    if (requested_cause == RIVER_CLOUD_PLAYBACK_REBUFFER_CAUSE_WRITE_FAILED &&
        river_cloud_xiaozhi_write_failed_prefers_starved_rebuffer(
            queued_frames,
            now_ms,
            &plan->supply_gap_ms)) {
        plan->cause = RIVER_CLOUD_PLAYBACK_REBUFFER_CAUSE_UPSTREAM_STARVED;
    }

    river_cloud_xiaozhi_capture_playback_truth_view(&plan->truth_view);
    plan->recovery_path =
        river_cloud_xiaozhi_rebuffer_prefers_service_recover(
            plan->cause,
            plan->truth_view.supply_kind) ?
            RIVER_CLOUD_PLAYBACK_RECOVERY_PATH_SERVICE_RECOVER :
            RIVER_CLOUD_PLAYBACK_RECOVERY_PATH_STOP_REBUFFER;
    plan->inline_recover_allowed =
        plan->recovery_path == RIVER_CLOUD_PLAYBACK_RECOVERY_PATH_SERVICE_RECOVER &&
        plan->cause == RIVER_CLOUD_PLAYBACK_REBUFFER_CAUSE_WRITE_FAILED;
    plan->start_gate = river_cloud_xiaozhi_current_start_gate();
}

static void river_cloud_xiaozhi_refresh_playback_recovery_plan_after_rebuffer_note(
    river_cloud_xiaozhi_playback_recovery_plan_t *plan)
{
    if (plan == NULL) {
        return;
    }

    plan->start_gate = river_cloud_xiaozhi_current_start_gate();
}

static void river_cloud_xiaozhi_begin_managed_playback_rebuffer(
    river_cloud_xiaozhi_playback_recovery_plan_t *plan,
    uint64_t now_ms,
    bool keep_retry_frame)
{
    if (plan == NULL) {
        return;
    }

    if (keep_retry_frame) {
        river_cloud_xiaozhi_keep_current_downlink_frame_for_retry();
    }
    river_cloud_xiaozhi_note_playback_rebuffer(now_ms, plan->cause);
    river_cloud_xiaozhi_refresh_playback_recovery_plan_after_rebuffer_note(plan);
}

static void river_cloud_xiaozhi_capture_managed_playback_rebuffer_request(
    const char *recover_reason,
    bool keep_retry_frame,
    bool force_stop_rebuffer,
    river_cloud_xiaozhi_managed_rebuffer_request_t *request)
{
    if (request == NULL) {
        return;
    }

    memset(request, 0, sizeof(*request));
    request->recover_reason = recover_reason;
    request->keep_retry_frame = keep_retry_frame;
    request->force_stop_rebuffer = force_stop_rebuffer;
}

static river_cloud_xiaozhi_rebuffer_recovery_result_t
river_cloud_xiaozhi_execute_managed_playback_rebuffer_recovery(
    const char *reason,
    const river_cloud_xiaozhi_playback_recovery_plan_t *plan,
    bool force_stop_rebuffer)
{
    river_cloud_xiaozhi_rebuffer_recovery_result_t result;

    memset(&result, 0, sizeof(result));
    result.status = RIVER_ERR_ARG;
    result.applied_path = RIVER_CLOUD_PLAYBACK_RECOVERY_PATH_STOP_REBUFFER;
    if (plan == NULL) {
        return result;
    }

    if (force_stop_rebuffer) {
        river_cloud_xiaozhi_set_playback_recovery_path(
            RIVER_CLOUD_PLAYBACK_RECOVERY_PATH_STOP_REBUFFER,
            "playback_recovery_path");
        result.status = river_cloud_xiaozhi_stop_playback_for_rebuffer(reason);
        return result;
    }

    result = river_cloud_xiaozhi_request_playback_rebuffer_recovery(
        reason,
        plan->cause,
        plan->truth_view.supply_kind);
    return result;
}

static void river_cloud_xiaozhi_start_managed_playback_rebuffer_request(
    const river_cloud_xiaozhi_managed_rebuffer_request_t *request,
    river_cloud_xiaozhi_playback_recovery_plan_t *recovery_plan,
    uint64_t now_ms,
    river_cloud_playback_recovery_path_t *recovery_path_io)
{
    if (request == NULL || recovery_plan == NULL) {
        return;
    }

    river_cloud_xiaozhi_begin_managed_playback_rebuffer(recovery_plan,
                                                        now_ms,
                                                        request->keep_retry_frame);
    if (recovery_path_io != NULL && request->force_stop_rebuffer) {
        *recovery_path_io = RIVER_CLOUD_PLAYBACK_RECOVERY_PATH_STOP_REBUFFER;
    }
}

static river_cloud_xiaozhi_rebuffer_recovery_result_t
river_cloud_xiaozhi_execute_managed_playback_rebuffer_request(
    const river_cloud_xiaozhi_managed_rebuffer_request_t *request,
    const river_cloud_xiaozhi_playback_recovery_plan_t *recovery_plan)
{
    river_cloud_xiaozhi_rebuffer_recovery_result_t result;

    memset(&result, 0, sizeof(result));
    result.status = RIVER_ERR_ARG;
    result.applied_path = RIVER_CLOUD_PLAYBACK_RECOVERY_PATH_STOP_REBUFFER;
    if (request == NULL || recovery_plan == NULL) {
        return result;
    }

    result = river_cloud_xiaozhi_execute_managed_playback_rebuffer_recovery(
        request->recover_reason != NULL ? request->recover_reason :
                                          "xiaozhi_playback_rebuffer",
        recovery_plan,
        request->force_stop_rebuffer);
    if (result.status != RIVER_OK) {
        RIVER_LOGW("xiaozhi playback recover fallback to fresh start: cause=%s supply=%s recovery=%s",
                   river_cloud_playback_rebuffer_cause_name(
                       recovery_plan->cause),
                   river_cloud_xiaozhi_playback_supply_kind_name(
                       recovery_plan->truth_view.supply_kind),
                   river_cloud_xiaozhi_playback_recovery_path_label(
                       result.applied_path,
                       "stop_rebuffer"));
    }

    return result;
}

static void river_cloud_xiaozhi_log_write_failed_rebuffer_request(
    const char *message,
    const river_cloud_xiaozhi_playback_recovery_plan_t *recovery_plan,
    river_cloud_playback_recovery_path_t recovery_path)
{
    river_cloud_xiaozhi_playback_rebuffer_observe_view_t observe;

    if (recovery_plan == NULL) {
        return;
    }

    river_cloud_xiaozhi_capture_playback_rebuffer_observe_view(recovery_plan,
                                                               recovery_path,
                                                               &observe);
    RIVER_LOGW("%s: cause=%s supply=%s phase=%s backend=%s queued=%lu low=%u start=%u policy=%s cautious=%s target_ms=%u total=%lu streak=%lu recovery=%s",
               message != NULL ? message : "xiaozhi playback rebuffer requested",
               river_cloud_playback_rebuffer_cause_name(observe.cause),
               river_cloud_xiaozhi_playback_supply_kind_name(observe.supply_kind),
               river_cloud_playback_phase_name(observe.phase_kind),
               river_cloud_playback_backend_state_name(observe.backend_state),
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
}

static void river_cloud_xiaozhi_execute_write_failed_managed_rebuffer_followup(
    const river_cloud_xiaozhi_write_failed_followup_t *followup,
    river_cloud_xiaozhi_write_failed_recovery_view_t *recovery_view)
{
    if (followup == NULL || recovery_view == NULL || !followup->managed_rebuffer) {
        return;
    }

    river_cloud_xiaozhi_start_managed_playback_rebuffer_request(
        &followup->rebuffer_request,
        &recovery_view->recovery_plan,
        recovery_view->now_ms,
        &recovery_view->recovery_path);
    river_cloud_xiaozhi_log_write_failed_rebuffer_request(
        followup->request_log,
        &recovery_view->recovery_plan,
        recovery_view->recovery_path);
    (void)river_cloud_xiaozhi_execute_managed_playback_rebuffer_request(
        &followup->rebuffer_request,
        &recovery_view->recovery_plan);
}

static river_cloud_xiaozhi_inline_recover_result_t
river_cloud_xiaozhi_try_write_failed_inline_recover(
    const char *recover_reason,
    const river_cloud_xiaozhi_playback_recovery_plan_t *recovery_plan,
    const river_cloud_xiaozhi_downlink_write_view_t *write_view,
    river_cloud_playback_recovery_path_t recovery_path)
{
    river_cloud_xiaozhi_playback_rebuffer_observe_view_t observe;

    if (recovery_plan == NULL || write_view == NULL ||
        !recovery_plan->inline_recover_allowed) {
        return RIVER_CLOUD_XIAOZHI_INLINE_RECOVER_NOT_ATTEMPTED;
    }

    river_cloud_xiaozhi_capture_playback_rebuffer_observe_view(recovery_plan,
                                                               recovery_path,
                                                               &observe);
    RIVER_LOGW("xiaozhi playback service recover requested: cause=%s supply=%s phase=%s backend=%s queued=%lu low=%u start=%u policy=%s cautious=%s target_ms=%u total=%lu streak=%lu recovery=%s",
               river_cloud_playback_rebuffer_cause_name(observe.cause),
               river_cloud_xiaozhi_playback_supply_kind_name(observe.supply_kind),
               river_cloud_playback_phase_name(observe.phase_kind),
               river_cloud_playback_backend_state_name(observe.backend_state),
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
    if (river_playback_service_recover_stream_ex(recover_reason) != RIVER_OK) {
        return RIVER_CLOUD_XIAOZHI_INLINE_RECOVER_SERVICE_FAILED;
    }

    river_cloud_xiaozhi_set_playback_recovery_path(
        RIVER_CLOUD_PLAYBACK_RECOVERY_PATH_SERVICE_RECOVER,
        "playback_recovery_path");
    if (river_cloud_xiaozhi_write_current_downlink_frame_audio(write_view) != RIVER_OK) {
        return RIVER_CLOUD_XIAOZHI_INLINE_RECOVER_REPLAY_FAILED;
    }

    river_cloud_xiaozhi_set_playback_recovery_outcome(
        RIVER_CLOUD_PLAYBACK_RECOVERY_OUTCOME_INLINE_REPLAY,
        "playback_recovery_outcome");
    RIVER_LOGI("xiaozhi playback inline recover replay succeeded: cause=%s supply=%s phase=%s backend=%s queued=%lu low=%u recovery=%s",
               river_cloud_playback_rebuffer_cause_name(observe.cause),
               river_cloud_xiaozhi_playback_supply_kind_name(observe.supply_kind),
               river_cloud_playback_phase_name(observe.phase_kind),
               river_cloud_playback_backend_state_name(observe.backend_state),
               (unsigned long)observe.queued_frames,
               (unsigned int)observe.low_water_frames,
               river_cloud_xiaozhi_playback_recovery_path_label(
                   observe.recovery_path,
                   "stop_rebuffer"));
    return RIVER_CLOUD_XIAOZHI_INLINE_RECOVER_SUCCEEDED;
}

static void river_cloud_xiaozhi_capture_write_failed_followup(
    river_cloud_xiaozhi_inline_recover_result_t inline_result,
    const char *recover_reason,
    river_cloud_xiaozhi_write_failed_followup_t *followup)
{
    if (followup == NULL) {
        return;
    }

    memset(followup, 0, sizeof(*followup));
    switch (inline_result) {
    case RIVER_CLOUD_XIAOZHI_INLINE_RECOVER_SUCCEEDED:
        followup->inline_success = true;
        return;

    case RIVER_CLOUD_XIAOZHI_INLINE_RECOVER_REPLAY_FAILED:
        followup->managed_rebuffer = true;
        followup->request_log =
            "xiaozhi playback rebuffer requested after inline replay fallback";
        river_cloud_xiaozhi_capture_managed_playback_rebuffer_request(
            recover_reason,
            true,
            true,
            &followup->rebuffer_request);
        return;

    case RIVER_CLOUD_XIAOZHI_INLINE_RECOVER_SERVICE_FAILED:
        followup->managed_rebuffer = true;
        followup->request_log =
            "xiaozhi playback rebuffer requested after recover fallback";
        river_cloud_xiaozhi_capture_managed_playback_rebuffer_request(
            recover_reason,
            true,
            true,
            &followup->rebuffer_request);
        return;

    case RIVER_CLOUD_XIAOZHI_INLINE_RECOVER_NOT_ATTEMPTED:
    default:
        followup->managed_rebuffer = true;
        followup->request_log = "xiaozhi playback rebuffer requested";
        river_cloud_xiaozhi_capture_managed_playback_rebuffer_request(
            recover_reason,
            true,
            false,
            &followup->rebuffer_request);
        return;
    }
}

static void river_cloud_xiaozhi_capture_current_downlink_write_view(
    river_cloud_xiaozhi_downlink_write_view_t *view)
{
    if (view == NULL) {
        return;
    }

    memset(view, 0, sizeof(*view));
    view->mono_bytes = g_river_cloud.xiaozhi_downlink_ring.frame_bytes;
    view->stereo_bytes = view->mono_bytes * 2U;
    view->frame_too_large = view->stereo_bytes >
                            sizeof(g_river_cloud.xiaozhi_downlink_stereo);
}

static river_status_t river_cloud_xiaozhi_write_current_downlink_frame_audio(
    const river_cloud_xiaozhi_downlink_write_view_t *view)
{
    if (view == NULL || view->mono_bytes == 0U || view->frame_too_large) {
        return RIVER_ERR_ARG;
    }

    river_cloud_xiaozhi_downlink_expand_stereo(g_river_cloud.xiaozhi_downlink_task_frame,
                                               view->mono_bytes);
    return river_playback_service_write(
        (const uint8_t *)g_river_cloud.xiaozhi_downlink_stereo,
        view->stereo_bytes,
        g_river_cloud.xiaozhi_downlink_task_frame,
        view->mono_bytes,
        true);
}

static void river_cloud_xiaozhi_capture_write_failed_recovery_view(
    uint32_t queued_frames,
    const river_cloud_xiaozhi_downlink_write_view_t *write_view,
    river_cloud_xiaozhi_write_failed_recovery_view_t *view)
{
    if (view == NULL) {
        return;
    }

    memset(view, 0, sizeof(*view));
    view->recover_reason = "xiaozhi_playback_write_failed";
    view->recovery_path = RIVER_CLOUD_PLAYBACK_RECOVERY_PATH_STOP_REBUFFER;
    view->now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    if (write_view != NULL) {
        view->mono_bytes = write_view->mono_bytes;
        view->stereo_bytes = write_view->stereo_bytes;
    }

    river_cloud_xiaozhi_capture_playback_recovery_plan(
        &view->recovery_plan,
        RIVER_CLOUD_PLAYBACK_REBUFFER_CAUSE_WRITE_FAILED,
        queued_frames,
        view->now_ms);
    if (view->recovery_plan.cause ==
        RIVER_CLOUD_PLAYBACK_REBUFFER_CAUSE_UPSTREAM_STARVED) {
        view->recover_reason = "xiaozhi_playback_starved_write";
    }
    view->recovery_path = view->recovery_plan.recovery_path;
}

static void river_cloud_xiaozhi_log_write_failed_recovery_view(
    const river_cloud_xiaozhi_write_failed_recovery_view_t *view)
{
    if (view == NULL) {
        return;
    }

    RIVER_LOGW("xiaozhi playback write failed: cause=%s supply=%s phase=%s backend=%s mono=%luB stereo=%luB queued=%lu low=%u supply_gap_ms=%lu recovery=%s",
               river_cloud_playback_rebuffer_cause_name(
                   view->recovery_plan.cause),
               river_cloud_xiaozhi_playback_supply_kind_name(
                   view->recovery_plan.truth_view.supply_kind),
               river_cloud_playback_phase_name(
                   river_cloud_xiaozhi_playback_observed_phase_kind()),
               river_cloud_playback_backend_state_name(
                   view->recovery_plan.truth_view.backend_state),
               (unsigned long)view->mono_bytes,
               (unsigned long)view->stereo_bytes,
               (unsigned long)view->recovery_plan.queued_frames,
               (unsigned int)view->recovery_plan.low_water_frames,
               (unsigned long)view->recovery_plan.supply_gap_ms,
               river_cloud_xiaozhi_playback_recovery_path_label(
                   view->recovery_path,
                   "stop_rebuffer"));
}

static bool river_cloud_xiaozhi_handle_playback_write_failed(
    uint32_t queued_frames,
    const river_cloud_xiaozhi_downlink_write_view_t *write_view)
{
    river_cloud_xiaozhi_inline_recover_result_t inline_result;
    river_cloud_xiaozhi_write_failed_followup_t followup;
    river_cloud_xiaozhi_write_failed_recovery_view_t recovery_view;

    river_cloud_xiaozhi_capture_write_failed_recovery_view(queued_frames,
                                                           write_view,
                                                           &recovery_view);
    river_cloud_xiaozhi_clear_downlink_starvation_watch();
    river_cloud_xiaozhi_log_write_failed_recovery_view(&recovery_view);

    inline_result = river_cloud_xiaozhi_try_write_failed_inline_recover(
        recovery_view.recover_reason,
        &recovery_view.recovery_plan,
        write_view,
        recovery_view.recovery_path);
    river_cloud_xiaozhi_capture_write_failed_followup(inline_result,
                                                      recovery_view.recover_reason,
                                                      &followup);
    if (followup.inline_success) {
        river_cloud_xiaozhi_consume_current_downlink_frame();
        return true;
    }
    river_cloud_xiaozhi_execute_write_failed_managed_rebuffer_followup(
        &followup,
        &recovery_view);
    return false;
}

static void river_cloud_xiaozhi_finish_successful_downlink_frame_write(void)
{
    uint64_t now_ms;

    river_cloud_xiaozhi_consume_current_downlink_frame();
    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    river_cloud_xiaozhi_try_start_current_playback_segment(now_ms);
    river_cloud_xiaozhi_update_playback_ack_progress();
    river_cloud_xiaozhi_playback_check_pending_stop();
}

static river_cloud_xiaozhi_downlink_task_step_result_t
river_cloud_xiaozhi_write_current_downlink_frame_step(uint32_t queued_frames)
{
    river_cloud_xiaozhi_downlink_write_view_t write_view;

    river_cloud_xiaozhi_capture_current_downlink_write_view(&write_view);
    if (write_view.frame_too_large) {
        (void)river_cloud_xiaozhi_playback_abort_for_cause(
            RIVER_CLOUD_XIAOZHI_PLAYBACK_ABORT_FRAME_OVERSIZE,
            NULL);
        return RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_CONTINUE;
    }

    if (river_cloud_xiaozhi_write_current_downlink_frame_audio(&write_view) != RIVER_OK) {
        return river_cloud_xiaozhi_handle_playback_write_failed(
                   queued_frames,
                   &write_view) ?
                   RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_CONTINUE :
                   RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_SLEEP_POLL;
    }

    river_cloud_xiaozhi_finish_successful_downlink_frame_write();
    return RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_CONTINUE;
}

river_status_t river_cloud_xiaozhi_execute_playback_control_transport(
    const river_cloud_xiaozhi_control_request_t *request)
{
    river_status_t status;

    if (request == NULL) {
        return RIVER_ERR_ARG;
    }

    switch (request->op) {
    case RIVER_CLOUD_XIAOZHI_CTRL_PLAYBACK_STARTED:
        status = river_xiaozhi_send_audio_out_started(request->response_id,
                                                      request->playback_id,
                                                      request->segment_id);
        if (status == RIVER_OK) {
            RIVER_LOGI("xiaozhi playback ack started sent: response_id=%s playback_id=%s segment_id=%s",
                       request->response_id[0] != '\0' ? request->response_id : "-",
                       request->playback_id[0] != '\0' ? request->playback_id : "-",
                       request->segment_id[0] != '\0' ? request->segment_id : "-");
        } else {
            RIVER_LOGW("xiaozhi playback ack started send failed: status=%d response_id=%s playback_id=%s segment_id=%s negotiated_mode=%s last_err=%s",
                       (int)status,
                       request->response_id[0] != '\0' ? request->response_id : "-",
                       request->playback_id[0] != '\0' ? request->playback_id : "-",
                       request->segment_id[0] != '\0' ? request->segment_id : "-",
                       river_xiaozhi_playback_ack_mode_negotiated() != NULL ?
                           river_xiaozhi_playback_ack_mode_negotiated() :
                           "-",
                       river_xiaozhi_last_error() != NULL ? river_xiaozhi_last_error() : "-");
        }
        return status;

    case RIVER_CLOUD_XIAOZHI_CTRL_PLAYBACK_MARK:
        status = river_xiaozhi_send_audio_out_mark(request->response_id,
                                                   request->playback_id,
                                                   request->segment_id,
                                                   request->played_duration_ms);
        if (status == RIVER_OK) {
            RIVER_LOGI("xiaozhi playback ack mark sent: response_id=%s playback_id=%s segment_id=%s played_duration_ms=%lu",
                       request->response_id[0] != '\0' ? request->response_id : "-",
                       request->playback_id[0] != '\0' ? request->playback_id : "-",
                       request->segment_id[0] != '\0' ? request->segment_id : "-",
                       (unsigned long)request->played_duration_ms);
        } else {
            RIVER_LOGW("xiaozhi playback ack mark send failed: status=%d response_id=%s playback_id=%s segment_id=%s played_duration_ms=%lu negotiated_mode=%s last_err=%s",
                       (int)status,
                       request->response_id[0] != '\0' ? request->response_id : "-",
                       request->playback_id[0] != '\0' ? request->playback_id : "-",
                       request->segment_id[0] != '\0' ? request->segment_id : "-",
                       (unsigned long)request->played_duration_ms,
                       river_xiaozhi_playback_ack_mode_negotiated() != NULL ?
                           river_xiaozhi_playback_ack_mode_negotiated() :
                           "-",
                       river_xiaozhi_last_error() != NULL ? river_xiaozhi_last_error() : "-");
        }
        return status;

    case RIVER_CLOUD_XIAOZHI_CTRL_PLAYBACK_CLEARED:
        status = river_xiaozhi_send_audio_out_cleared(request->response_id,
                                                      request->playback_id,
                                                      request->segment_id,
                                                      request->arg);
        if (status == RIVER_OK) {
            RIVER_LOGI("xiaozhi playback ack cleared sent: response_id=%s playback_id=%s cleared_after_segment_id=%s reason=%s",
                       request->response_id[0] != '\0' ? request->response_id : "-",
                       request->playback_id[0] != '\0' ? request->playback_id : "-",
                       request->segment_id[0] != '\0' ? request->segment_id : "-",
                       request->arg[0] != '\0' ? request->arg : "-");
        } else {
            RIVER_LOGW("xiaozhi playback ack cleared send failed: status=%d response_id=%s playback_id=%s cleared_after_segment_id=%s reason=%s negotiated_mode=%s last_err=%s",
                       (int)status,
                       request->response_id[0] != '\0' ? request->response_id : "-",
                       request->playback_id[0] != '\0' ? request->playback_id : "-",
                       request->segment_id[0] != '\0' ? request->segment_id : "-",
                       request->arg[0] != '\0' ? request->arg : "-",
                       river_xiaozhi_playback_ack_mode_negotiated() != NULL ?
                           river_xiaozhi_playback_ack_mode_negotiated() :
                           "-",
                       river_xiaozhi_last_error() != NULL ? river_xiaozhi_last_error() : "-");
        }
        return status;

    case RIVER_CLOUD_XIAOZHI_CTRL_PLAYBACK_COMPLETED:
        status = river_xiaozhi_send_audio_out_completed(request->response_id,
                                                        request->playback_id);
        if (status == RIVER_OK) {
            RIVER_LOGI("xiaozhi playback ack completed sent: response_id=%s playback_id=%s",
                       request->response_id[0] != '\0' ? request->response_id : "-",
                       request->playback_id[0] != '\0' ? request->playback_id : "-");
        } else {
            RIVER_LOGW("xiaozhi playback ack completed send failed: status=%d response_id=%s playback_id=%s negotiated_mode=%s last_err=%s",
                       (int)status,
                       request->response_id[0] != '\0' ? request->response_id : "-",
                       request->playback_id[0] != '\0' ? request->playback_id : "-",
                       river_xiaozhi_playback_ack_mode_negotiated() != NULL ?
                           river_xiaozhi_playback_ack_mode_negotiated() :
                           "-",
                       river_xiaozhi_last_error() != NULL ? river_xiaozhi_last_error() : "-");
        }
        return status;

    default:
        return RIVER_ERR_UNSUPPORTED;
    }
}

void river_cloud_xiaozhi_dump_playback_status(uint64_t now_ms)
{
    river_voice_duplex_ready_eval_t duplex_eval;
    river_cloud_xiaozhi_playback_diag_view_t diag_view;
    river_cloud_xiaozhi_playback_meta_truth_t *meta_truth =
        &g_river_cloud.xiaozhi_playback_meta_truth;
    river_cloud_xiaozhi_playback_terminal_truth_t *terminal_truth =
        &g_river_cloud.xiaozhi_playback_terminal_truth;

    river_cloud_xiaozhi_capture_playback_diag_view(now_ms, &diag_view);
    river_cloud_xiaozhi_get_duplex_ready_eval(&duplex_eval);
    RIVER_LOGI("xiaozhi playback_meta response_id=%s playback_id=%s segment_id=%s text=%s expected_duration_ms=%lu is_last_segment=%s started_ack=%s completed_ack=%s valid=%s",
               meta_truth->current_context.response_id[0] != '\0' ?
                   meta_truth->current_context.response_id :
                   "-",
               meta_truth->current_context.playback_id[0] != '\0' ?
                   meta_truth->current_context.playback_id :
                   "-",
               meta_truth->current_context.segment_id[0] != '\0' ?
                   meta_truth->current_context.segment_id :
                   "-",
               meta_truth->text[0] != '\0' ? meta_truth->text : "-",
               (unsigned long)meta_truth->expected_duration_ms,
               river_cloud_xiaozhi_playback_current_meta_is_last_segment() ? "yes" : "no",
               terminal_truth->started_reported ? "yes" : "no",
               terminal_truth->completed_reported ? "yes" : "no",
               river_cloud_xiaozhi_playback_segment_context_valid() ? "yes" : "no");
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
               terminal_truth->last_started_segment_id[0] != '\0' ?
                   terminal_truth->last_started_segment_id :
                   "-",
               terminal_truth->last_fully_heard_context.segment_id[0] != '\0' ?
                   terminal_truth->last_fully_heard_context.segment_id :
                   "-");
    RIVER_LOGI("xiaozhi playback_heard_context response_id=%s playback_id=%s segment_id=%s valid=%s",
               terminal_truth->last_fully_heard_context.response_id[0] != '\0' ?
                   terminal_truth->last_fully_heard_context.response_id :
                   "-",
               terminal_truth->last_fully_heard_context.playback_id[0] != '\0' ?
                   terminal_truth->last_fully_heard_context.playback_id :
                   "-",
               terminal_truth->last_fully_heard_context.segment_id[0] != '\0' ?
                   terminal_truth->last_fully_heard_context.segment_id :
                   "-",
               river_cloud_xiaozhi_playback_last_fully_heard_context_valid() ? "yes" :
                                                                                "no");
    RIVER_LOGI("xiaozhi playback_terminal_context response_id=%s playback_id=%s segment_id=%s valid=%s",
               terminal_truth->last_segment_context.response_id[0] != '\0' ?
                   terminal_truth->last_segment_context.response_id :
                   "-",
               terminal_truth->last_segment_context.playback_id[0] != '\0' ?
                   terminal_truth->last_segment_context.playback_id :
                   "-",
               terminal_truth->last_segment_context.segment_id[0] != '\0' ?
                   terminal_truth->last_segment_context.segment_id :
                   "-",
               river_cloud_xiaozhi_playback_last_segment_observed() ? "yes" : "no");
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
    RIVER_LOGI("xiaozhi downlink queue=%lu/%u dropped=%lu worker=%s sample=%luHz frame=%lums start=%u resume=%u buffer=%u policy=%s cautious=%s target_ms=%lu prefetch_frames=%u meta_gap_ms=%lu supply=%s phase=%s backend=%s hold=%s rebuffer=%s/%s recovery_path=%s recovery_outcome=%s rebuffer_total=%lu streak=%lu",
               (unsigned long)diag_view.queued_frames,
               (unsigned int)RIVER_CLOUD_XIAOZHI_DOWNLINK_RING_FRAMES,
               (unsigned long)diag_view.ring_dropped,
               diag_view.downlink_started ? "running" : "off",
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
    river_status_t status;
    bool attached = false;

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
               g_river_cloud.xiaozhi_playback_meta_truth.current_context.response_id[0] !=
                       '\0' ?
                   g_river_cloud.xiaozhi_playback_meta_truth.current_context.response_id :
                   "-",
               g_river_cloud.xiaozhi_playback_meta_truth.current_context.playback_id[0] !=
                       '\0' ?
                   g_river_cloud.xiaozhi_playback_meta_truth.current_context.playback_id :
                   "-",
               g_river_cloud.xiaozhi_playback_meta_truth.current_context.segment_id[0] !=
                       '\0' ?
                   g_river_cloud.xiaozhi_playback_meta_truth.current_context.segment_id :
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
    g_river_cloud.xiaozhi_playback_terminal_truth.cleared_reported =
        state == RIVER_CLOUD_PLAYBACK_TERMINAL_STATE_CLEARED;
    g_river_cloud.xiaozhi_playback_terminal_truth.completed_reported =
        state == RIVER_CLOUD_PLAYBACK_TERMINAL_STATE_COMPLETED;
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

static void river_cloud_xiaozhi_mark_segment_fully_heard(
    const river_cloud_xiaozhi_playback_segment_t *segment)
{
    if (segment == NULL || !segment->valid || segment->segment_id[0] == '\0') {
        return;
    }

    river_cloud_xiaozhi_copy_optional_text(
        g_river_cloud.xiaozhi_playback_terminal_truth.last_fully_heard_context.response_id,
        sizeof(g_river_cloud.xiaozhi_playback_terminal_truth.last_fully_heard_context.response_id),
        segment->response_id);
    river_cloud_xiaozhi_copy_optional_text(
        g_river_cloud.xiaozhi_playback_terminal_truth.last_fully_heard_context.playback_id,
        sizeof(g_river_cloud.xiaozhi_playback_terminal_truth.last_fully_heard_context.playback_id),
        segment->playback_id);
    river_cloud_xiaozhi_copy_optional_text(
        g_river_cloud.xiaozhi_playback_terminal_truth.last_fully_heard_context.segment_id,
        sizeof(g_river_cloud.xiaozhi_playback_terminal_truth.last_fully_heard_context.segment_id),
        segment->segment_id);
    if (!g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_pending &&
        g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_streak != 0U) {
        if (segment->rebuffered) {
            RIVER_LOGI("xiaozhi playback rebuffer streak preserved: segment_id=%s total=%lu streak=%lu reason=segment_recovered",
                       segment->segment_id,
                       (unsigned long)g_river_cloud.xiaozhi_playback_runtime_truth
                           .rebuffer_count,
                       (unsigned long)g_river_cloud.xiaozhi_playback_runtime_truth
                           .rebuffer_streak);
        } else {
            RIVER_LOGI("xiaozhi playback rebuffer streak cleared: segment_id=%s total=%lu streak=%lu reason=clean_segment",
                       segment->segment_id,
                       (unsigned long)g_river_cloud.xiaozhi_playback_runtime_truth
                           .rebuffer_count,
                       (unsigned long)g_river_cloud.xiaozhi_playback_runtime_truth
                           .rebuffer_streak);
            g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_streak = 0U;
            (void)river_cloud_xiaozhi_refresh_start_gate();
        }
    }
}

static bool river_cloud_xiaozhi_playback_completed_ready(void)
{
    if (!river_cloud_xiaozhi_playback_last_segment_observed()) {
        return false;
    }
    if (river_cloud_xiaozhi_current_playback_segment() != NULL) {
        return false;
    }
    return river_cloud_xiaozhi_playback_same_segment_context(
        &g_river_cloud.xiaozhi_playback_terminal_truth.last_fully_heard_context,
        &g_river_cloud.xiaozhi_playback_terminal_truth.last_segment_context);
}

static bool river_cloud_xiaozhi_playback_last_fully_heard_context_valid(void)
{
    return river_cloud_xiaozhi_playback_segment_context_valid_from_truth(
        &g_river_cloud.xiaozhi_playback_terminal_truth.last_fully_heard_context);
}

static river_cloud_playback_terminal_wait_kind_t
river_cloud_xiaozhi_playback_completed_wait_kind(void)
{
    river_cloud_xiaozhi_playback_segment_t *segment;

    if (!river_cloud_xiaozhi_playback_last_segment_observed()) {
        return RIVER_CLOUD_PLAYBACK_TERMINAL_WAIT_LAST_SEGMENT_META;
    }

    segment = river_cloud_xiaozhi_current_playback_segment();
    if (segment != NULL) {
        return RIVER_CLOUD_PLAYBACK_TERMINAL_WAIT_QUEUE_DRAIN;
    }
    if (!river_cloud_xiaozhi_playback_same_segment_context(
            &g_river_cloud.xiaozhi_playback_terminal_truth.last_fully_heard_context,
            &g_river_cloud.xiaozhi_playback_terminal_truth.last_segment_context)) {
        return RIVER_CLOUD_PLAYBACK_TERMINAL_WAIT_LAST_SEGMENT_TAIL;
    }

    return RIVER_CLOUD_PLAYBACK_TERMINAL_WAIT_NONE;
}

void river_cloud_xiaozhi_playback_note_meta(const river_xiaozhi_event_t *event)
{
    river_cloud_xiaozhi_playback_segment_t *segment = NULL;
    river_cloud_xiaozhi_playback_meta_truth_t *meta_truth =
        &g_river_cloud.xiaozhi_playback_meta_truth;
    river_cloud_xiaozhi_playback_terminal_truth_t *terminal_truth =
        &g_river_cloud.xiaozhi_playback_terminal_truth;
    river_cloud_xiaozhi_playback_start_gate_t start_gate;
    uint64_t now_ms;
    uint32_t index;
    uint32_t tail_index;
    uint32_t prefetch_target_ms;
    uint32_t frame_ms = river_cloud_xiaozhi_downlink_frame_duration_ms();

    if (event == NULL) {
        return;
    }

    if ((event->response_id != NULL && event->response_id[0] != '\0' &&
         strcmp(meta_truth->current_context.response_id, event->response_id) != 0) ||
        (event->playback_id != NULL && event->playback_id[0] != '\0' &&
         strcmp(meta_truth->current_context.playback_id, event->playback_id) != 0)) {
        river_cloud_xiaozhi_clear_playback_meta_state();
    }

    river_cloud_xiaozhi_copy_optional_text(meta_truth->current_context.response_id,
                                           sizeof(meta_truth->current_context.response_id),
                                           event->response_id);
    river_cloud_xiaozhi_copy_optional_text(meta_truth->current_context.playback_id,
                                           sizeof(meta_truth->current_context.playback_id),
                                           event->playback_id);
    river_cloud_xiaozhi_copy_optional_text(meta_truth->current_context.segment_id,
                                           sizeof(meta_truth->current_context.segment_id),
                                           event->segment_id);
    if (event->text != NULL && event->text[0] != '\0') {
        river_cloud_xiaozhi_copy_optional_text(meta_truth->text,
                                               sizeof(meta_truth->text),
                                               event->text);
    }
    if (event->expected_duration_ms != 0U) {
        meta_truth->expected_duration_ms = event->expected_duration_ms;
    }
    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    prefetch_target_ms = meta_truth->expected_duration_ms;
    if (prefetch_target_ms == 0U) {
        prefetch_target_ms = frame_ms * RIVER_CLOUD_XIAOZHI_DOWNLINK_START_FRAMES;
    }
    if (meta_truth->last_meta_ms != 0U && now_ms > meta_truth->last_meta_ms) {
        uint64_t meta_gap_ms = now_ms - meta_truth->last_meta_ms;

        if (meta_gap_ms > UINT32_MAX) {
            meta_gap_ms = UINT32_MAX;
        }
        meta_truth->last_meta_gap_ms = (uint32_t)meta_gap_ms;
        if (meta_truth->last_meta_gap_ms > prefetch_target_ms) {
            prefetch_target_ms = meta_truth->last_meta_gap_ms;
        }
    } else {
        meta_truth->last_meta_gap_ms = 0U;
    }
    if (prefetch_target_ms <= UINT32_MAX - RIVER_CLOUD_XIAOZHI_DOWNLINK_PREFETCH_MARGIN_MS) {
        prefetch_target_ms += RIVER_CLOUD_XIAOZHI_DOWNLINK_PREFETCH_MARGIN_MS;
    } else {
        prefetch_target_ms = UINT32_MAX;
    }
    if (frame_ms != 0U) {
        uint32_t max_prefetch_ms = river_cloud_xiaozhi_downlink_max_start_frames() * frame_ms;

        if (prefetch_target_ms > max_prefetch_ms) {
            prefetch_target_ms = max_prefetch_ms;
        }
    }
    meta_truth->prefetch_target_ms = prefetch_target_ms;
    meta_truth->last_meta_ms = now_ms;
    if (river_cloud_xiaozhi_playback_segment_context_valid()) {
        if (event->is_last_segment) {
            river_cloud_xiaozhi_clear_playback_wait_context();
            river_cloud_xiaozhi_store_playback_last_segment_context();
        } else {
            river_cloud_xiaozhi_store_playback_wait_context();
        }
    } else {
        river_cloud_xiaozhi_clear_playback_wait_context();
    }
    terminal_truth->started_reported =
        terminal_truth->last_started_segment_id[0] != '\0' &&
        strcmp(terminal_truth->last_started_segment_id,
               meta_truth->current_context.segment_id) == 0;
    terminal_truth->cleared_reported =
        terminal_truth->state_kind ==
        RIVER_CLOUD_PLAYBACK_TERMINAL_STATE_CLEARED;
    terminal_truth->completed_reported =
        terminal_truth->state_kind ==
        RIVER_CLOUD_PLAYBACK_TERMINAL_STATE_COMPLETED;

    for (index = 0U;
         index < g_river_cloud.xiaozhi_playback_segment_queue_truth.count;
         ++index) {
        uint32_t slot =
            (g_river_cloud.xiaozhi_playback_segment_queue_truth.head + index) %
            RIVER_CLOUD_XIAOZHI_PLAYBACK_SEGMENTS_MAX;
        river_cloud_xiaozhi_playback_segment_t *candidate =
            &g_river_cloud.xiaozhi_playback_segment_queue_truth.segments[slot];

        if (candidate->valid &&
            strcmp(candidate->segment_id, meta_truth->current_context.segment_id) == 0) {
            segment = candidate;
            break;
        }
    }

    if (segment == NULL) {
        if (g_river_cloud.xiaozhi_playback_segment_queue_truth.count >=
            RIVER_CLOUD_XIAOZHI_PLAYBACK_SEGMENTS_MAX) {
            RIVER_LOGW("xiaozhi playback segment queue full: playback_id=%s segment_id=%s queued=%lu",
                       meta_truth->current_context.playback_id[0] != '\0' ?
                           meta_truth->current_context.playback_id :
                           "-",
                       meta_truth->current_context.segment_id[0] != '\0' ?
                           meta_truth->current_context.segment_id :
                           "-",
                       (unsigned long)g_river_cloud.xiaozhi_playback_segment_queue_truth
                           .count);
            return;
        }
        tail_index =
            (g_river_cloud.xiaozhi_playback_segment_queue_truth.head +
             g_river_cloud.xiaozhi_playback_segment_queue_truth.count) %
            RIVER_CLOUD_XIAOZHI_PLAYBACK_SEGMENTS_MAX;
        segment =
            &g_river_cloud.xiaozhi_playback_segment_queue_truth.segments[tail_index];
        memset(segment, 0, sizeof(*segment));
        segment->valid = true;
        g_river_cloud.xiaozhi_playback_segment_queue_truth.count++;
    }

    river_cloud_xiaozhi_copy_optional_text(segment->response_id,
                                           sizeof(segment->response_id),
                                           meta_truth->current_context.response_id);
    river_cloud_xiaozhi_copy_optional_text(segment->playback_id,
                                           sizeof(segment->playback_id),
                                           meta_truth->current_context.playback_id);
    river_cloud_xiaozhi_copy_optional_text(segment->segment_id,
                                           sizeof(segment->segment_id),
                                           meta_truth->current_context.segment_id);
    if (meta_truth->text[0] != '\0') {
        river_cloud_xiaozhi_copy_optional_text(segment->text,
                                               sizeof(segment->text),
                                               meta_truth->text);
    }
    segment->expected_duration_ms = meta_truth->expected_duration_ms;
    segment->is_last_segment = event->is_last_segment;
    start_gate = river_cloud_xiaozhi_refresh_start_gate();

    RIVER_LOGI("xiaozhi playback prefetch: segment_id=%s expected_ms=%lu meta_gap_ms=%lu target_ms=%lu policy=%s cautious=%s start=%u queued=%lu rebuffer=%s/%s rebuffer_total=%lu streak=%lu",
               meta_truth->current_context.segment_id[0] != '\0' ?
                   meta_truth->current_context.segment_id :
                   "-",
               (unsigned long)meta_truth->expected_duration_ms,
               (unsigned long)meta_truth->last_meta_gap_ms,
               (unsigned long)meta_truth->prefetch_target_ms,
               river_cloud_playback_start_policy_name(start_gate.policy),
               start_gate.cautious_history ? "yes" : "no",
               (unsigned int)start_gate.start_frames,
               (unsigned long)river_cloud_xiaozhi_playback_queued_frames(),
               g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_pending ? "yes" :
                                                                               "no",
               river_cloud_playback_rebuffer_cause_name(
                   river_cloud_xiaozhi_playback_rebuffer_cause()),
               (unsigned long)g_river_cloud.xiaozhi_playback_runtime_truth
                   .rebuffer_count,
               (unsigned long)g_river_cloud.xiaozhi_playback_runtime_truth
                   .rebuffer_streak);
    river_cloud_xiaozhi_refresh_playback_phase("note_meta");
}

void river_cloud_xiaozhi_note_audio_out_meta_observation(const river_xiaozhi_event_t *event)
{
    if (event == NULL) {
        return;
    }

    river_cloud_xiaozhi_window_touch(RIVER_CLOUD_XIAOZHI_WAKE_WINDOW_FOLLOWUP_MS,
                                     "audio_out_meta");
    river_cloud_xiaozhi_playback_note_meta(event);
    RIVER_LOGI("xiaozhi playback fact observed: response_id=%s playback_id=%s segment_id=%s text=%s expected_duration_ms=%lu is_last_segment=%s accepted=%s",
               event->response_id != NULL ? event->response_id : "-",
               event->playback_id != NULL ? event->playback_id : "-",
               event->segment_id != NULL ? event->segment_id : "-",
               event->text != NULL ? event->text : "-",
               (unsigned long)event->expected_duration_ms,
               event->is_last_segment ? "yes" : "no",
               river_cloud_xiaozhi_turn_accepted() ? "yes" : "no");
}

static void river_cloud_xiaozhi_try_queue_playback_started_ack(void)
{
    river_status_t status;
    river_cloud_xiaozhi_playback_segment_t *segment =
        river_cloud_xiaozhi_current_playback_segment();

    if (segment == NULL || !segment->valid || segment->response_id[0] == '\0' ||
        segment->playback_id[0] == '\0' || segment->segment_id[0] == '\0' ||
        !segment->started || segment->started_ack_reported ||
        !river_cloud_xiaozhi_playback_terminal_open()) {
        return;
    }

    status = river_cloud_xiaozhi_control_request_async(
        RIVER_CLOUD_XIAOZHI_CTRL_PLAYBACK_STARTED,
        NULL,
        segment->response_id,
        segment->playback_id,
        segment->segment_id,
        0U);
    if (status == RIVER_OK) {
        segment->started_ack_reported = true;
        river_cloud_xiaozhi_copy_optional_text(
            g_river_cloud.xiaozhi_playback_terminal_truth.last_started_segment_id,
            sizeof(g_river_cloud.xiaozhi_playback_terminal_truth.last_started_segment_id),
            segment->segment_id);
        g_river_cloud.xiaozhi_playback_terminal_truth.started_reported =
            strcmp(g_river_cloud.xiaozhi_playback_meta_truth.current_context.segment_id,
                   segment->segment_id) == 0;
        RIVER_LOGI("xiaozhi playback ack started queued: response_id=%s playback_id=%s segment_id=%s",
                   segment->response_id,
                   segment->playback_id,
                   segment->segment_id);
    } else {
        RIVER_LOGW("xiaozhi playback ack started queue failed: status=%d response_id=%s playback_id=%s segment_id=%s",
                   (int)status,
                   segment->response_id[0] != '\0' ? segment->response_id : "-",
                   segment->playback_id[0] != '\0' ? segment->playback_id : "-",
                   segment->segment_id[0] != '\0' ? segment->segment_id : "-");
    }
}

static void river_cloud_xiaozhi_try_queue_playback_mark_ack(uint32_t played_duration_ms)
{
    river_status_t status;
    river_cloud_xiaozhi_playback_segment_t *segment =
        river_cloud_xiaozhi_current_playback_segment();

    if (segment == NULL || !segment->valid || !segment->started ||
        !segment->started_ack_reported || !river_cloud_xiaozhi_playback_terminal_open()) {
        return;
    }

    if (played_duration_ms == 0U) {
        return;
    }
    if (segment->expected_duration_ms != 0U &&
        played_duration_ms > segment->expected_duration_ms) {
        played_duration_ms = segment->expected_duration_ms;
    }
    if (played_duration_ms <= segment->last_mark_ms) {
        return;
    }
    if (segment->last_mark_ms != 0U &&
        played_duration_ms != segment->expected_duration_ms &&
        (played_duration_ms - segment->last_mark_ms) <
            RIVER_CLOUD_XIAOZHI_PLAYBACK_MARK_INTERVAL_MS) {
        return;
    }

    status = river_cloud_xiaozhi_control_request_async(
        RIVER_CLOUD_XIAOZHI_CTRL_PLAYBACK_MARK,
        NULL,
        segment->response_id,
        segment->playback_id,
        segment->segment_id,
        played_duration_ms);
    if (status == RIVER_OK) {
        segment->last_mark_ms = played_duration_ms;
    } else {
        RIVER_LOGW("xiaozhi playback ack mark queue failed: status=%d response_id=%s playback_id=%s segment_id=%s played_duration_ms=%lu",
                   (int)status,
                   segment->response_id[0] != '\0' ? segment->response_id : "-",
                   segment->playback_id[0] != '\0' ? segment->playback_id : "-",
                   segment->segment_id[0] != '\0' ? segment->segment_id : "-",
                   (unsigned long)played_duration_ms);
    }
}

static bool river_cloud_xiaozhi_try_queue_playback_cleared_ack(const char *reason)
{
    river_status_t status;
    const char *response_id;
    const char *playback_id;
    const char *segment_id;

    if (!river_cloud_xiaozhi_playback_last_fully_heard_context_valid() ||
        !river_cloud_xiaozhi_playback_terminal_open()) {
        return false;
    }

    response_id =
        g_river_cloud.xiaozhi_playback_terminal_truth.last_fully_heard_context.response_id;
    playback_id =
        g_river_cloud.xiaozhi_playback_terminal_truth.last_fully_heard_context.playback_id;
    segment_id =
        g_river_cloud.xiaozhi_playback_terminal_truth.last_fully_heard_context.segment_id;
    status = river_cloud_xiaozhi_control_request_async(
        RIVER_CLOUD_XIAOZHI_CTRL_PLAYBACK_CLEARED,
        reason,
        response_id,
        playback_id,
        segment_id,
        0U);
    if (status == RIVER_OK) {
        RIVER_LOGI("xiaozhi playback ack cleared queued: response_id=%s playback_id=%s cleared_after_segment_id=%s reason=%s",
                   response_id[0] != '\0' ? response_id : "-",
                   playback_id[0] != '\0' ? playback_id : "-",
                   segment_id[0] != '\0' ? segment_id : "-",
                   reason != NULL && reason[0] != '\0' ? reason : "-");
        return true;
    } else {
        RIVER_LOGW("xiaozhi playback ack cleared queue failed: status=%d response_id=%s playback_id=%s cleared_after_segment_id=%s reason=%s",
                   (int)status,
                   response_id[0] != '\0' ? response_id : "-",
                   playback_id[0] != '\0' ? playback_id : "-",
                   segment_id[0] != '\0' ? segment_id : "-",
                   reason != NULL && reason[0] != '\0' ? reason : "-");
        return false;
    }
}

static void river_cloud_xiaozhi_update_playback_ack_progress(void)
{
    uint64_t now_ms;
    uint64_t next_segment_start_ms;
    river_cloud_xiaozhi_playback_segment_t *segment;

    if (!river_cloud_xiaozhi_playback_terminal_open()) {
        return;
    }
    if (g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_pending) {
        return;
    }

    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    while (true) {
        uint32_t played_duration_ms;

        segment = river_cloud_xiaozhi_current_playback_segment();
        if (segment == NULL || !segment->valid || !segment->started) {
            return;
        }
        river_cloud_xiaozhi_try_queue_playback_started_ack();
        if (!segment->started_ack_reported) {
            return;
        }
        if (now_ms <= segment->started_at_ms) {
            return;
        }

        played_duration_ms = (uint32_t)(now_ms - segment->started_at_ms);
        if (segment->expected_duration_ms != 0U &&
            played_duration_ms > segment->expected_duration_ms) {
            played_duration_ms = segment->expected_duration_ms;
        }
        river_cloud_xiaozhi_try_queue_playback_mark_ack(played_duration_ms);

        if (segment->expected_duration_ms == 0U ||
            played_duration_ms < segment->expected_duration_ms) {
            return;
        }

        river_cloud_xiaozhi_mark_segment_fully_heard(segment);
        next_segment_start_ms = segment->started_at_ms + segment->expected_duration_ms;
        river_cloud_xiaozhi_pop_playback_segment();
        if (river_cloud_xiaozhi_current_playback_segment() == NULL) {
            return;
        }
        if (next_segment_start_ms > now_ms) {
            next_segment_start_ms = now_ms;
        }
        river_cloud_xiaozhi_try_start_current_playback_segment(next_segment_start_ms);
    }
}

static bool river_cloud_xiaozhi_try_queue_playback_completed_ack(void)
{
    river_status_t status;
    river_cloud_xiaozhi_playback_segment_t *segment;
    const char *response_id;
    const char *playback_id;
    uint64_t now_ms;
    river_cloud_playback_terminal_wait_kind_t wait_kind;

    if (!river_cloud_xiaozhi_playback_terminal_open()) {
        river_cloud_xiaozhi_clear_playback_terminal_wait();
        return true;
    }

    river_cloud_xiaozhi_update_playback_ack_progress();
    if (!river_cloud_xiaozhi_playback_completed_ready()) {
        wait_kind = river_cloud_xiaozhi_playback_completed_wait_kind();
        river_cloud_xiaozhi_set_playback_terminal_wait(wait_kind);
        return false;
    }
    river_cloud_xiaozhi_clear_playback_terminal_wait();
    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    while ((segment = river_cloud_xiaozhi_current_playback_segment()) != NULL &&
           segment->valid) {
        uint32_t final_mark_ms = segment->expected_duration_ms;

        if (!segment->started) {
            river_cloud_xiaozhi_try_start_current_playback_segment(now_ms);
        }
        river_cloud_xiaozhi_try_queue_playback_started_ack();
        if (final_mark_ms == 0U) {
            final_mark_ms = segment->last_mark_ms;
        }
        if (final_mark_ms != 0U) {
            river_cloud_xiaozhi_try_queue_playback_mark_ack(final_mark_ms);
        }
        river_cloud_xiaozhi_mark_segment_fully_heard(segment);
        river_cloud_xiaozhi_pop_playback_segment();
        if (final_mark_ms != 0U && now_ms <= (UINT64_MAX - final_mark_ms)) {
            now_ms += final_mark_ms;
        }
    }

    if (g_river_cloud.xiaozhi_playback_terminal_truth.last_started_segment_id[0] == '\0') {
        river_cloud_xiaozhi_set_playback_terminal_state(
            RIVER_CLOUD_PLAYBACK_TERMINAL_STATE_LOCAL_COMPLETED,
            NULL);
        return true;
    }
    if (!river_cloud_xiaozhi_playback_last_segment_observed()) {
        river_cloud_xiaozhi_set_playback_terminal_state(
            RIVER_CLOUD_PLAYBACK_TERMINAL_STATE_LOCAL_COMPLETED,
            NULL);
        return true;
    }
    response_id = g_river_cloud.xiaozhi_playback_terminal_truth.last_segment_context.response_id;
    playback_id = g_river_cloud.xiaozhi_playback_terminal_truth.last_segment_context.playback_id;
    if (response_id[0] == '\0' || playback_id[0] == '\0') {
        river_cloud_xiaozhi_set_playback_terminal_state(
            RIVER_CLOUD_PLAYBACK_TERMINAL_STATE_LOCAL_COMPLETED,
            NULL);
        return true;
    }

    status = river_cloud_xiaozhi_control_request_async(
        RIVER_CLOUD_XIAOZHI_CTRL_PLAYBACK_COMPLETED,
        NULL,
        response_id,
        playback_id,
        NULL,
        0U);
    if (status == RIVER_OK) {
        RIVER_LOGI("xiaozhi playback ack completed queued: response_id=%s playback_id=%s",
                   response_id[0] != '\0' ? response_id : "-",
                   playback_id[0] != '\0' ? playback_id : "-");
    } else {
        RIVER_LOGW("xiaozhi playback ack completed queue failed: status=%d response_id=%s playback_id=%s",
                   (int)status,
                   response_id[0] != '\0' ? response_id : "-",
                   playback_id[0] != '\0' ? playback_id : "-");
        river_cloud_xiaozhi_set_playback_terminal_state(
            RIVER_CLOUD_PLAYBACK_TERMINAL_STATE_LOCAL_COMPLETED,
            NULL);
        return true;
    }
    river_cloud_xiaozhi_set_playback_terminal_ack("completed", NULL);
    return true;
}

void river_cloud_xiaozhi_playback_finalize_cleared(const char *reason)
{
    river_cloud_xiaozhi_playback_segment_t *segment;
    uint64_t now_ms;
    uint32_t final_mark_ms;
    bool cleared_sent = false;

    if (!river_cloud_xiaozhi_playback_terminal_open()) {
        return;
    }

    river_cloud_xiaozhi_update_playback_ack_progress();
    segment = river_cloud_xiaozhi_current_playback_segment();
    if (segment != NULL && segment->valid && segment->started) {
        river_cloud_xiaozhi_try_queue_playback_started_ack();
        now_ms = segment->paused_at_ms != 0U ? segment->paused_at_ms :
                                               (uint64_t)rtos_time_get_current_system_time_ms();
        final_mark_ms = now_ms > segment->started_at_ms ?
                            (uint32_t)(now_ms - segment->started_at_ms) :
                            segment->last_mark_ms;
        if (segment->expected_duration_ms != 0U &&
            final_mark_ms > segment->expected_duration_ms) {
            final_mark_ms = segment->expected_duration_ms;
        }
        if (final_mark_ms != 0U) {
            river_cloud_xiaozhi_try_queue_playback_mark_ack(final_mark_ms);
        }
        if (segment->expected_duration_ms != 0U &&
            final_mark_ms >= segment->expected_duration_ms) {
            river_cloud_xiaozhi_mark_segment_fully_heard(segment);
            river_cloud_xiaozhi_pop_playback_segment();
        }
    }

    if (g_river_cloud.xiaozhi_playback_terminal_truth.last_fully_heard_context.segment_id[0] !=
        '\0') {
        cleared_sent = river_cloud_xiaozhi_try_queue_playback_cleared_ack(reason);
    }
    if (cleared_sent) {
        river_cloud_xiaozhi_set_playback_terminal_ack("cleared", reason);
    } else {
        river_cloud_xiaozhi_set_playback_terminal_state(
            RIVER_CLOUD_PLAYBACK_TERMINAL_STATE_LOCAL_CLEARED,
            reason);
        RIVER_LOGI("xiaozhi playback clear kept local only: reason=%s last_fully_heard=%s",
                   reason != NULL && reason[0] != '\0' ? reason : "-",
                   g_river_cloud.xiaozhi_playback_terminal_truth.last_fully_heard_context
                               .segment_id[0] != '\0' ?
                       g_river_cloud.xiaozhi_playback_terminal_truth.last_fully_heard_context
                           .segment_id :
                       "-");
    }
}

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

static bool
river_cloud_xiaozhi_prepare_downlink_playback(uint32_t queued_frames, uint64_t now_ms)
{
    river_cloud_xiaozhi_playback_truth_view_t truth_view;
    uint32_t start_frames;

    river_cloud_xiaozhi_capture_playback_truth_view(&truth_view);
    if (!river_cloud_xiaozhi_rebuffer_resume_ready(queued_frames,
                                                   now_ms,
                                                   truth_view.backend_state)) {
        return false;
    }
    if (g_river_cloud.xiaozhi_playback_runtime_truth.stop_pending &&
        truth_view.backend_state != RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_ACTIVE) {
        river_cloud_xiaozhi_playback_check_pending_stop();
        return false;
    }
    if (truth_view.backend_state == RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_PAUSED) {
        if (!river_cloud_xiaozhi_maybe_resume_paused_playback(queued_frames,
                                                              &truth_view)) {
            return false;
        }
        river_cloud_xiaozhi_capture_playback_truth_view(&truth_view);
    }
    if (truth_view.backend_state == RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_ACTIVE) {
        return true;
    }
    if (truth_view.backend_state == RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_RECOVERING) {
        return false;
    }

    start_frames = river_cloud_xiaozhi_downlink_start_threshold_for_backend(
        truth_view.backend_state);
    if (!g_river_cloud.xiaozhi_playback_runtime_truth.stop_pending &&
        queued_frames < start_frames) {
        return false;
    }

    return river_cloud_xiaozhi_start_playback_if_needed(
               river_cloud_xiaozhi_downlink_sample_rate(),
               river_cloud_xiaozhi_downlink_frame_duration_ms(),
               g_river_cloud.xiaozhi_downlink_ring.frame_bytes) == RIVER_OK ?
               true :
               false;
}

static bool river_cloud_xiaozhi_downlink_active(void);

static river_cloud_xiaozhi_downlink_cycle_plan_t
river_cloud_xiaozhi_prepare_downlink_cycle_plan(void)
{
    river_cloud_xiaozhi_downlink_cycle_plan_t plan = {
        .step_result = RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_SLEEP_POLL,
        .queued_frames = 0U,
        .ready = false,
    };
    uint32_t queued_frames;
    uint64_t now_ms;

    if (!river_cloud_xiaozhi_downlink_active()) {
        plan.step_result = RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_SLEEP_IDLE;
        return plan;
    }

    queued_frames = river_cloud_xiaozhi_playback_queued_frames();
    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    river_cloud_xiaozhi_update_playback_ack_progress();
    if (river_cloud_xiaozhi_maybe_rebuffer_starved(queued_frames, now_ms)) {
        return plan;
    }
    if (river_cloud_xiaozhi_maybe_pause_for_segment_gap()) {
        return plan;
    }
    if (queued_frames == 0U) {
        river_cloud_xiaozhi_playback_check_pending_stop();
        return plan;
    }
    if (!river_cloud_xiaozhi_prepare_downlink_playback(queued_frames, now_ms)) {
        return plan;
    }

    plan.queued_frames = queued_frames;
    plan.ready = true;
    return plan;
}

static river_cloud_xiaozhi_downlink_frame_acquire_result_t
river_cloud_xiaozhi_acquire_current_downlink_frame(void)
{
    river_cloud_xiaozhi_downlink_frame_acquire_result_t result = {
        .step_result = RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_SLEEP_POLL,
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
        return result;
    }

    result.acquired = true;
    result.step_result = RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_CONTINUE;
    return result;
}

static river_cloud_xiaozhi_downlink_task_step_result_t
river_cloud_xiaozhi_process_downlink_task_cycle(void)
{
    river_cloud_xiaozhi_downlink_cycle_plan_t cycle_plan;
    river_cloud_xiaozhi_downlink_frame_acquire_result_t acquire_result;

    cycle_plan = river_cloud_xiaozhi_prepare_downlink_cycle_plan();
    if (!cycle_plan.ready) {
        return cycle_plan.step_result;
    }

    acquire_result = river_cloud_xiaozhi_acquire_current_downlink_frame();
    if (!acquire_result.acquired) {
        return acquire_result.step_result;
    }

    return river_cloud_xiaozhi_write_current_downlink_frame_step(
        cycle_plan.queued_frames);
}

static void river_cloud_xiaozhi_finish_downlink_task_cycle(
    river_cloud_xiaozhi_downlink_task_step_result_t step_result)
{
    if (step_result == RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_SLEEP_IDLE) {
        rtos_time_delay_ms(RIVER_CLOUD_XIAOZHI_DOWNLINK_IDLE_MS);
        return;
    }
    if (step_result == RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_SLEEP_POLL) {
        rtos_time_delay_ms(RIVER_CLOUD_XIAOZHI_DOWNLINK_POLL_MS);
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
        river_cloud_xiaozhi_finish_downlink_task_cycle(
            river_cloud_xiaozhi_process_downlink_task_cycle());
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
#endif
