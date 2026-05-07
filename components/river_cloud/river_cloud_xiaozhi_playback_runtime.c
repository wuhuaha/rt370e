/* XiaoZhi downlink / playback runtime: queueing, rebuffer, ACK progress, and worker ownership. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "os_wrapper.h"
#include "rtk_status.h"

#include "river/river_log.h"
#include "river/river_playback_service.h"
#include "river/river_voice_profile.h"
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
static uint32_t river_cloud_xiaozhi_downlink_frame_duration_ms(void);
static river_status_t river_cloud_xiaozhi_ensure_downlink_ring(size_t frame_bytes);
static river_status_t river_cloud_xiaozhi_write_downlink_frame_latest(
    const uint8_t *mono_frame);
static uint32_t river_cloud_xiaozhi_downlink_start_threshold_for_backend(
    river_cloud_playback_backend_state_t backend_state);
static river_status_t river_cloud_xiaozhi_write_current_downlink_frame_audio(
    const river_cloud_xiaozhi_downlink_write_view_t *view);
static bool river_cloud_xiaozhi_playback_last_fully_heard_context_valid(void);
static bool river_cloud_xiaozhi_playback_completed_ready(void);
static bool river_cloud_xiaozhi_playback_last_segment_observed(void);
static bool river_cloud_xiaozhi_current_last_segment_audio_locally_complete(
    uint32_t queued_frames);
static river_cloud_xiaozhi_playback_segment_t *river_cloud_xiaozhi_current_playback_segment(void);
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
static bool river_cloud_xiaozhi_try_queue_playback_completed_ack(void);
static bool river_cloud_xiaozhi_maybe_finish_zero_duration_tail_on_drain(
    uint32_t queued_frames,
    const char *trigger);
static void river_cloud_xiaozhi_maybe_complete_terminal_playback_after_progress(
    const char *trigger);

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

#include "river_cloud_xiaozhi_playback_lineage.inc"

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
        &g_river_cloud.xiaozhi_playback_lineage_truth.last_segment_context);
}

static bool river_cloud_xiaozhi_current_last_segment_audio_locally_complete(
    uint32_t queued_frames)
{
    river_cloud_xiaozhi_playback_segment_t *segment =
        river_cloud_xiaozhi_current_playback_segment();
    river_cloud_xiaozhi_playback_context_truth_t segment_context;
    uint32_t frame_ms;
    uint64_t local_duration_ms;

    if (segment == NULL || !segment->valid || !segment->started ||
        !segment->is_last_segment || segment->expected_duration_ms == 0U ||
        !river_cloud_xiaozhi_playback_last_segment_observed()) {
        return false;
    }

    river_cloud_xiaozhi_clear_playback_context_truth(&segment_context);
    river_cloud_xiaozhi_copy_optional_text(segment_context.response_id,
                                           sizeof(segment_context.response_id),
                                           segment->response_id);
    river_cloud_xiaozhi_copy_optional_text(segment_context.playback_id,
                                           sizeof(segment_context.playback_id),
                                           segment->playback_id);
    river_cloud_xiaozhi_copy_optional_text(segment_context.segment_id,
                                           sizeof(segment_context.segment_id),
                                           segment->segment_id);
    if (!river_cloud_xiaozhi_playback_same_segment_context(
            &segment_context,
            &g_river_cloud.xiaozhi_playback_lineage_truth.last_segment_context)) {
        return false;
    }

    frame_ms = river_cloud_xiaozhi_downlink_frame_duration_ms();
    if (frame_ms == 0U) {
        return false;
    }

    local_duration_ms = (uint64_t)segment->pushed_duration_ms +
                        (uint64_t)queued_frames * (uint64_t)frame_ms;
    return local_duration_ms >= (uint64_t)segment->expected_duration_ms;
}

static bool river_cloud_xiaozhi_playback_response_context_valid(void)
{
    return river_cloud_xiaozhi_playback_response_context_valid_from_truth(
        &g_river_cloud.xiaozhi_playback_lineage_truth.meta_context);
}

static bool river_cloud_xiaozhi_playback_segment_context_valid(void)
{
    return river_cloud_xiaozhi_playback_segment_context_valid_from_truth(
        &g_river_cloud.xiaozhi_playback_lineage_truth.meta_context);
}

static bool river_cloud_xiaozhi_playback_current_meta_is_last_segment(void)
{
    if (!river_cloud_xiaozhi_playback_segment_context_valid() ||
        !river_cloud_xiaozhi_playback_last_segment_observed()) {
        return false;
    }

    return river_cloud_xiaozhi_playback_same_segment_context(
        &g_river_cloud.xiaozhi_playback_lineage_truth.meta_context,
        &g_river_cloud.xiaozhi_playback_lineage_truth.last_segment_context);
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
        &g_river_cloud.xiaozhi_playback_lineage_truth.meta_context);
}

static void river_cloud_xiaozhi_clear_playback_last_segment_context(void)
{
    river_cloud_xiaozhi_clear_playback_context_truth(
        &g_river_cloud.xiaozhi_playback_lineage_truth.last_segment_context);
    river_cloud_xiaozhi_clear_playback_context_truth(
        &g_river_cloud.xiaozhi_playback_terminal_truth.last_segment_context);
}

static void river_cloud_xiaozhi_store_playback_last_segment_context(void)
{
    river_cloud_xiaozhi_copy_playback_context_truth(
        &g_river_cloud.xiaozhi_playback_lineage_truth.last_segment_context,
        &g_river_cloud.xiaozhi_playback_lineage_truth.meta_context);
    river_cloud_xiaozhi_copy_playback_context_truth(
        &g_river_cloud.xiaozhi_playback_terminal_truth.last_segment_context,
        &g_river_cloud.xiaozhi_playback_lineage_truth.last_segment_context);
}

#include "river_cloud_xiaozhi_playback_runtime_views.inc"

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

static void river_cloud_xiaozhi_clear_downlink_accum(void)
{
    g_river_cloud.xiaozhi_downlink_runtime_truth.accum_bytes = 0U;
}

static size_t river_cloud_xiaozhi_downlink_pcm_frame_bytes(uint32_t sample_rate,
                                                           uint32_t frame_duration_ms)
{
    uint64_t frame_bytes;

    if (sample_rate == 0U) {
        sample_rate = 16000U;
    }
    if (frame_duration_ms == 0U) {
        frame_duration_ms = RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS;
    }

    frame_bytes = ((uint64_t)sample_rate * (uint64_t)frame_duration_ms *
                   sizeof(int16_t)) /
                  1000U;
    if (frame_bytes == 0U || frame_bytes > SIZE_MAX) {
        return 0U;
    }
    return (size_t)frame_bytes;
}

static river_status_t river_cloud_xiaozhi_flush_downlink_accum_if_needed(
    size_t frame_bytes,
    const char *reason)
{
    river_status_t status;
    uint32_t sample_rate;
    uint32_t frame_duration_ms;
    size_t accum_bytes =
        g_river_cloud.xiaozhi_downlink_runtime_truth.accum_bytes;

    if (accum_bytes == 0U) {
        return RIVER_OK;
    }

    if (frame_bytes == 0U && g_river_cloud.xiaozhi_downlink_ring.initialized) {
        frame_bytes = g_river_cloud.xiaozhi_downlink_ring.frame_bytes;
    }
    if (frame_bytes == 0U) {
        sample_rate = g_river_cloud.xiaozhi_downlink_stream_truth.sample_rate;
        if (sample_rate == 0U) {
            sample_rate =
                g_river_cloud.xiaozhi_server_audio_format_truth.sample_rate;
        }
        frame_duration_ms =
            g_river_cloud.xiaozhi_downlink_stream_truth.frame_duration_ms;
        if (frame_duration_ms == 0U) {
            frame_duration_ms =
                g_river_cloud.xiaozhi_server_audio_format_truth.frame_duration_ms;
        }
        frame_bytes = river_cloud_xiaozhi_downlink_pcm_frame_bytes(sample_rate,
                                                                   frame_duration_ms);
    }

    if (frame_bytes == 0U || frame_bytes > RIVER_CLOUD_XIAOZHI_DOWNLINK_PCM_BYTES_MAX ||
        accum_bytes > frame_bytes) {
        RIVER_LOGW("xiaozhi downlink accum dropped: reason=%s accum=%lu frame=%lu",
                   reason != NULL ? reason : "-",
                   (unsigned long)accum_bytes,
                   (unsigned long)frame_bytes);
        river_cloud_xiaozhi_clear_downlink_accum();
        return RIVER_ERR_ARG;
    }

    status = river_cloud_xiaozhi_ensure_downlink_ring(frame_bytes);
    if (status != RIVER_OK) {
        RIVER_LOGW("xiaozhi downlink accum flush failed: reason=%s ensure_status=%d accum=%lu frame=%lu",
                   reason != NULL ? reason : "-",
                   (int)status,
                   (unsigned long)accum_bytes,
                   (unsigned long)frame_bytes);
        river_cloud_xiaozhi_clear_downlink_accum();
        return status;
    }

    memset(g_river_cloud.xiaozhi_downlink_accum + accum_bytes,
           0,
           frame_bytes - accum_bytes);
    status = river_cloud_xiaozhi_write_downlink_frame_latest(
        g_river_cloud.xiaozhi_downlink_accum);
    if (status == RIVER_OK) {
        RIVER_LOGI("xiaozhi downlink accum flushed: reason=%s accum=%lu padded=%lu frame=%lu",
                   reason != NULL ? reason : "-",
                   (unsigned long)accum_bytes,
                   (unsigned long)(frame_bytes - accum_bytes),
                   (unsigned long)frame_bytes);
    } else {
        RIVER_LOGW("xiaozhi downlink accum flush failed: reason=%s write_status=%d accum=%lu frame=%lu",
                   reason != NULL ? reason : "-",
                   (int)status,
                   (unsigned long)accum_bytes,
                   (unsigned long)frame_bytes);
    }
    river_cloud_xiaozhi_clear_downlink_accum();
    return status;
}

static void river_cloud_xiaozhi_reset_downlink_ring_runtime(void)
{
    if (g_river_cloud.xiaozhi_downlink_ring.initialized) {
        river_audio_frame_ring_reset(&g_river_cloud.xiaozhi_downlink_ring);
    }
    river_cloud_xiaozhi_consume_current_downlink_frame();
    g_river_cloud.xiaozhi_downlink_runtime_truth.last_supply_ms = 0U;
    g_river_cloud.xiaozhi_downlink_runtime_truth.last_wait_delay_ms = 0U;
    g_river_cloud.xiaozhi_downlink_runtime_truth.last_wait_kind =
        RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_NONE;
    river_cloud_xiaozhi_clear_downlink_accum();
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

    /*
     * Long single-segment replies often arrive as "non-last first, promote to
     * last later". Do not make startup wait for the full expected duration
     * here, or the user pays >1 s of silence before TTS even begins.
     */
    if (gate.policy == RIVER_CLOUD_PLAYBACK_START_POLICY_PREFETCH_SEGMENT &&
        gate.start_frames > RIVER_CLOUD_XIAOZHI_DOWNLINK_SEGMENT_START_CAP_FRAMES) {
        gate.start_frames = RIVER_CLOUD_XIAOZHI_DOWNLINK_SEGMENT_START_CAP_FRAMES;
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

static bool river_cloud_xiaozhi_playback_terminal_close_deferred(
    const river_cloud_xiaozhi_playback_truth_view_t *truth_view)
{
    /* Keep the terminal logically open until the backend drain completes so
     * zero-duration fast-launch ACK tails do not reopen ASR semantics early. */
    return truth_view != NULL &&
           !river_cloud_xiaozhi_playback_terminal_open() &&
           (truth_view->output_active || truth_view->tts_stop_pending);
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
    view->downlink_wait_delay_ms =
        g_river_cloud.xiaozhi_downlink_runtime_truth.last_wait_delay_ms;
    view->downlink_cycle_status =
        g_river_cloud.xiaozhi_downlink_runtime_truth.last_cycle_status;
    view->downlink_wait_kind =
        g_river_cloud.xiaozhi_downlink_runtime_truth.last_wait_kind;
    view->downlink_cycle_outcome =
        g_river_cloud.xiaozhi_downlink_runtime_truth.last_cycle_outcome;
    view->meta_gap_ms = meta_truth->last_meta_gap_ms;
    view->rebuffer_count = g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_count;
    view->rebuffer_streak =
        g_river_cloud.xiaozhi_playback_runtime_truth.rebuffer_streak;
    view->downlink_started = river_cloud_xiaozhi_downlink_worker_started();
    view->tts_stop_pending = view->truth_view.tts_stop_pending;
    view->duplex_ready_seen = terminal_truth->duplex_ready_seen;
    view->terminal_closed =
        !river_cloud_xiaozhi_playback_terminal_close_deferred(&view->truth_view) &&
        !river_cloud_xiaozhi_playback_terminal_open();
    view->terminal_state_kind =
        view->terminal_closed ? terminal_truth->state_kind :
                                RIVER_CLOUD_PLAYBACK_TERMINAL_STATE_NONE;
    view->terminal_waiting = terminal_truth->waiting;
    view->start_cautious_history =
        g_river_cloud.xiaozhi_playback_gate_truth.cautious_history;
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

static const char *river_cloud_xiaozhi_write_failed_followup_kind_name(
    river_cloud_xiaozhi_write_failed_followup_kind_t kind)
{
    switch (kind) {
    case RIVER_CLOUD_XIAOZHI_WRITE_FAILED_FOLLOWUP_INLINE_REPLAY_CONSUMED:
        return "inline_replay_consumed";
    case RIVER_CLOUD_XIAOZHI_WRITE_FAILED_FOLLOWUP_MANAGED_REBUFFER:
        return "managed_rebuffer";
    case RIVER_CLOUD_XIAOZHI_WRITE_FAILED_FOLLOWUP_NONE:
    default:
        return "none";
    }
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

static river_cloud_xiaozhi_rebuffer_recovery_result_t
river_cloud_xiaozhi_execute_write_failed_managed_rebuffer_followup(
    const river_cloud_xiaozhi_write_failed_followup_t *followup,
    river_cloud_xiaozhi_write_failed_recovery_view_t *recovery_view)
{
    river_cloud_xiaozhi_rebuffer_recovery_result_t result = {
        .status = RIVER_ERR_ARG,
        .applied_path = RIVER_CLOUD_PLAYBACK_RECOVERY_PATH_STOP_REBUFFER,
    };

    if (followup == NULL || recovery_view == NULL ||
        followup->kind !=
            RIVER_CLOUD_XIAOZHI_WRITE_FAILED_FOLLOWUP_MANAGED_REBUFFER) {
        return result;
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
    result = river_cloud_xiaozhi_execute_managed_playback_rebuffer_request(
        &followup->rebuffer_request,
        &recovery_view->recovery_plan);
    recovery_view->recovery_path = result.applied_path;
    return result;
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
    followup->kind = RIVER_CLOUD_XIAOZHI_WRITE_FAILED_FOLLOWUP_NONE;
    followup->inline_result = inline_result;
    switch (inline_result) {
    case RIVER_CLOUD_XIAOZHI_INLINE_RECOVER_SUCCEEDED:
        followup->kind =
            RIVER_CLOUD_XIAOZHI_WRITE_FAILED_FOLLOWUP_INLINE_REPLAY_CONSUMED;
        return;

    case RIVER_CLOUD_XIAOZHI_INLINE_RECOVER_REPLAY_FAILED:
        followup->kind =
            RIVER_CLOUD_XIAOZHI_WRITE_FAILED_FOLLOWUP_MANAGED_REBUFFER;
        followup->request_log =
            "xiaozhi playback rebuffer requested after inline replay fallback";
        river_cloud_xiaozhi_capture_managed_playback_rebuffer_request(
            recover_reason,
            true,
            true,
            &followup->rebuffer_request);
        return;

    case RIVER_CLOUD_XIAOZHI_INLINE_RECOVER_SERVICE_FAILED:
        followup->kind =
            RIVER_CLOUD_XIAOZHI_WRITE_FAILED_FOLLOWUP_MANAGED_REBUFFER;
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
        followup->kind =
            RIVER_CLOUD_XIAOZHI_WRITE_FAILED_FOLLOWUP_MANAGED_REBUFFER;
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

static river_cloud_xiaozhi_write_failed_recovery_result_t
river_cloud_xiaozhi_handle_playback_write_failed(
    uint32_t queued_frames,
    const river_cloud_xiaozhi_downlink_write_view_t *write_view)
{
    river_cloud_xiaozhi_write_failed_recovery_result_t result = {
        .step_result = RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_SLEEP_POLL,
        .followup_kind = RIVER_CLOUD_XIAOZHI_WRITE_FAILED_FOLLOWUP_NONE,
        .inline_result = RIVER_CLOUD_XIAOZHI_INLINE_RECOVER_NOT_ATTEMPTED,
        .recovery_path = RIVER_CLOUD_PLAYBACK_RECOVERY_PATH_NONE,
        .recovery_status = RIVER_OK,
        .frame_consumed = false,
    };
    river_cloud_xiaozhi_inline_recover_result_t inline_result;
    river_cloud_xiaozhi_write_failed_followup_t followup;
    river_cloud_xiaozhi_write_failed_recovery_view_t recovery_view;
    river_cloud_xiaozhi_rebuffer_recovery_result_t managed_result;

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
    result.followup_kind = followup.kind;
    result.inline_result = followup.inline_result;
    result.recovery_path = recovery_view.recovery_path;
    if (followup.kind ==
        RIVER_CLOUD_XIAOZHI_WRITE_FAILED_FOLLOWUP_INLINE_REPLAY_CONSUMED) {
        river_cloud_xiaozhi_consume_current_downlink_frame();
        result.frame_consumed = true;
        result.step_result = RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_CONTINUE;
        RIVER_LOGI("xiaozhi playback write_failed recovery typed: followup=%s inline=%d status=%d recovery=%s consumed=%s",
                   river_cloud_xiaozhi_write_failed_followup_kind_name(
                       result.followup_kind),
                   (int)result.inline_result,
                   (int)result.recovery_status,
                   river_cloud_xiaozhi_playback_recovery_path_label(
                       result.recovery_path,
                       "stop_rebuffer"),
                   result.frame_consumed ? "yes" : "no");
        return result;
    }
    managed_result = river_cloud_xiaozhi_execute_write_failed_managed_rebuffer_followup(
        &followup,
        &recovery_view);
    result.recovery_status = managed_result.status;
    result.recovery_path = recovery_view.recovery_path;
    RIVER_LOGI("xiaozhi playback write_failed recovery typed: followup=%s inline=%d status=%d recovery=%s consumed=%s",
               river_cloud_xiaozhi_write_failed_followup_kind_name(
                   result.followup_kind),
               (int)result.inline_result,
               (int)result.recovery_status,
               river_cloud_xiaozhi_playback_recovery_path_label(
                   result.recovery_path,
                   "stop_rebuffer"),
               result.frame_consumed ? "yes" : "no");
    return result;
}

static void river_cloud_xiaozhi_finish_successful_downlink_frame_write(void)
{
    uint64_t now_ms;
    river_cloud_xiaozhi_playback_segment_t *segment;
    uint32_t frame_duration_ms;

    river_cloud_xiaozhi_consume_current_downlink_frame();
    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    river_cloud_xiaozhi_try_start_current_playback_segment(now_ms);
    segment = river_cloud_xiaozhi_current_playback_segment();
    frame_duration_ms = river_cloud_xiaozhi_downlink_frame_duration_ms();
    if (segment != NULL && segment->valid && segment->started &&
        frame_duration_ms != 0U) {
        if (segment->pushed_duration_ms <= UINT32_MAX - frame_duration_ms) {
            segment->pushed_duration_ms += frame_duration_ms;
        } else {
            segment->pushed_duration_ms = UINT32_MAX;
        }
    }
    river_cloud_xiaozhi_update_playback_ack_progress();
    river_cloud_xiaozhi_maybe_complete_terminal_playback_after_progress(
        "downlink_write");
    river_cloud_xiaozhi_playback_check_pending_stop();
}

static river_cloud_xiaozhi_downlink_frame_write_result_t
river_cloud_xiaozhi_write_current_downlink_frame_step(uint32_t queued_frames)
{
    river_cloud_xiaozhi_downlink_frame_write_result_t result = {
        .step_result = RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_CONTINUE,
        .status = RIVER_OK,
        .recovery_status = RIVER_OK,
        .recovery_followup_kind = RIVER_CLOUD_XIAOZHI_WRITE_FAILED_FOLLOWUP_NONE,
        .recovery_path = RIVER_CLOUD_PLAYBACK_RECOVERY_PATH_NONE,
        .frame_consumed = false,
        .write_failed = false,
        .aborted = false,
    };
    river_cloud_xiaozhi_downlink_write_view_t write_view;
    river_cloud_xiaozhi_write_failed_recovery_result_t failed_result;
    river_status_t write_status;
    river_cloud_playback_backend_state_t backend_state;

    river_cloud_xiaozhi_capture_current_downlink_write_view(&write_view);
    if (write_view.frame_too_large) {
        (void)river_cloud_xiaozhi_playback_abort_for_cause(
            RIVER_CLOUD_XIAOZHI_PLAYBACK_ABORT_FRAME_OVERSIZE,
            NULL);
        result.status = RIVER_ERR_ARG;
        result.aborted = true;
        return result;
    }

    backend_state = river_cloud_xiaozhi_playback_backend_state();
    if (backend_state != RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_ACTIVE) {
        river_cloud_xiaozhi_keep_current_downlink_frame_for_retry();
        river_cloud_xiaozhi_playback_check_pending_stop();
        result.step_result = RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_SLEEP_POLL;
        return result;
    }

    write_status = river_cloud_xiaozhi_write_current_downlink_frame_audio(&write_view);
    if (write_status != RIVER_OK) {
        failed_result = river_cloud_xiaozhi_handle_playback_write_failed(
            queued_frames,
            &write_view);
        result.status = write_status;
        result.write_failed = true;
        result.frame_consumed = failed_result.frame_consumed;
        result.step_result = failed_result.step_result;
        result.recovery_status = failed_result.recovery_status;
        result.recovery_followup_kind = failed_result.followup_kind;
        result.recovery_path = failed_result.recovery_path;
        return result;
    }

    river_cloud_xiaozhi_finish_successful_downlink_frame_write();
    result.frame_consumed = true;
    if (g_river_cloud.xiaozhi_downlink_runtime_truth.startup_burst_frames_left != 0U) {
        g_river_cloud.xiaozhi_downlink_runtime_truth.startup_burst_frames_left--;
    }
    return result;
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

#include "river_cloud_xiaozhi_playback_public_policy.inc"

#include "river_cloud_xiaozhi_playback_downlink_worker.inc"
#endif
