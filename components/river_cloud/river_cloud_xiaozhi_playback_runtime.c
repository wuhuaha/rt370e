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

static bool river_cloud_xiaozhi_playback_terminal_open(void)
{
    return g_river_cloud.xiaozhi_playback_terminal_state[0] == '\0';
}

static void river_cloud_xiaozhi_clear_playback_terminal_wait(void)
{
    g_river_cloud.xiaozhi_playback_terminal_waiting = false;
    g_river_cloud.xiaozhi_playback_terminal_wait_reason[0] = '\0';
}

static void river_cloud_xiaozhi_set_playback_terminal_wait(const char *reason)
{
    if (reason == NULL || reason[0] == '\0') {
        river_cloud_xiaozhi_clear_playback_terminal_wait();
        return;
    }

    g_river_cloud.xiaozhi_playback_terminal_waiting = true;
    river_cloud_xiaozhi_copy_optional_text(
        g_river_cloud.xiaozhi_playback_terminal_wait_reason,
        sizeof(g_river_cloud.xiaozhi_playback_terminal_wait_reason),
        reason);
}

static void river_cloud_xiaozhi_set_playback_terminal_state(const char *state,
                                                            const char *reason)
{
    river_cloud_xiaozhi_clear_playback_terminal_wait();
    river_cloud_xiaozhi_copy_optional_text(g_river_cloud.xiaozhi_playback_terminal_state,
                                           sizeof(g_river_cloud.xiaozhi_playback_terminal_state),
                                           state);
    river_cloud_xiaozhi_copy_optional_text(g_river_cloud.xiaozhi_playback_clear_reason,
                                           sizeof(g_river_cloud.xiaozhi_playback_clear_reason),
                                           reason);
}

static bool river_cloud_xiaozhi_playback_last_segment_observed(void)
{
    return g_river_cloud.xiaozhi_playback_last_segment &&
           g_river_cloud.xiaozhi_playback_segment_id[0] != '\0';
}

static river_cloud_xiaozhi_playback_segment_t *river_cloud_xiaozhi_current_playback_segment(void)
{
    if (g_river_cloud.xiaozhi_playback_segment_count == 0U) {
        return NULL;
    }

    return &g_river_cloud.xiaozhi_playback_segments
                [g_river_cloud.xiaozhi_playback_segment_head %
                 RIVER_CLOUD_XIAOZHI_PLAYBACK_SEGMENTS_MAX];
}

uint32_t river_cloud_xiaozhi_playback_queued_frames(void)
{
    uint32_t ready_frames;

    ready_frames = river_audio_frame_ring_count(&g_river_cloud.xiaozhi_downlink_ring);
    if (g_river_cloud.xiaozhi_downlink_retry_valid && ready_frames < UINT32_MAX) {
        ready_frames++;
    }
    return ready_frames;
}

bool river_cloud_xiaozhi_playback_output_active(void)
{
    return g_river_cloud.xiaozhi_playback_active || g_river_cloud.xiaozhi_tts_stop_pending;
}

bool river_cloud_xiaozhi_playback_has_work(void)
{
    return river_cloud_xiaozhi_playback_output_active() ||
           river_cloud_xiaozhi_playback_queued_frames() != 0U ||
           g_river_cloud.xiaozhi_playback_meta_valid;
}

static void river_cloud_xiaozhi_clear_downlink_starvation_watch(void)
{
    g_river_cloud.xiaozhi_downlink_starved_since_ms = 0U;
}

void river_cloud_xiaozhi_clear_playback_meta_state(void)
{
    g_river_cloud.xiaozhi_playback_meta_valid = false;
    g_river_cloud.xiaozhi_playback_started_reported = false;
    g_river_cloud.xiaozhi_playback_cleared_reported = false;
    g_river_cloud.xiaozhi_playback_completed_reported = false;
    g_river_cloud.xiaozhi_playback_last_segment = false;
    g_river_cloud.xiaozhi_playback_rebuffer_pending = false;
    g_river_cloud.xiaozhi_playback_rebuffer_count = 0U;
    g_river_cloud.xiaozhi_playback_segment_head = 0U;
    g_river_cloud.xiaozhi_playback_segment_count = 0U;
    g_river_cloud.xiaozhi_playback_expected_duration_ms = 0U;
    g_river_cloud.xiaozhi_playback_duplex_ready_seen = false;
    g_river_cloud.xiaozhi_playback_response_id[0] = '\0';
    g_river_cloud.xiaozhi_playback_id[0] = '\0';
    g_river_cloud.xiaozhi_playback_segment_id[0] = '\0';
    g_river_cloud.xiaozhi_playback_last_started_segment_id[0] = '\0';
    g_river_cloud.xiaozhi_playback_last_fully_heard_segment_id[0] = '\0';
    g_river_cloud.xiaozhi_playback_terminal_ack[0] = '\0';
    g_river_cloud.xiaozhi_playback_terminal_state[0] = '\0';
    g_river_cloud.xiaozhi_playback_clear_reason[0] = '\0';
    river_cloud_xiaozhi_clear_playback_terminal_wait();
    g_river_cloud.xiaozhi_playback_text[0] = '\0';
    memset(g_river_cloud.xiaozhi_playback_segments,
           0,
           sizeof(g_river_cloud.xiaozhi_playback_segments));
}

void river_cloud_xiaozhi_cancel_playback_stop(void)
{
    g_river_cloud.xiaozhi_tts_stop_pending = false;
    g_river_cloud.xiaozhi_tts_stop_deadline_ms = 0U;
    river_cloud_xiaozhi_clear_playback_terminal_wait();
}

void river_cloud_xiaozhi_playback_note_duplex_ready(void)
{
    if (river_cloud_xiaozhi_playback_output_active()) {
        g_river_cloud.xiaozhi_playback_duplex_ready_seen = true;
    }
}

void river_cloud_xiaozhi_mark_playback_started(void)
{
    g_river_cloud.xiaozhi_playback_active = true;
    g_river_cloud.xiaozhi_playback_duplex_ready_seen = false;
    river_cloud_xiaozhi_clear_playback_terminal_wait();
    river_cloud_xiaozhi_cancel_playback_stop();
    g_river_cloud.xiaozhi_no_ref_reopen_rearm = false;
    g_river_cloud.xiaozhi_no_ref_reopen_silence_frames = 0U;
    g_river_cloud.xiaozhi_no_ref_reopen_guard_deadline_ms = 0U;
}

void river_cloud_xiaozhi_arm_playback_stop(uint32_t drain_ms)
{
    if (!g_river_cloud.xiaozhi_playback_active || drain_ms == 0U) {
        return;
    }

    g_river_cloud.xiaozhi_tts_stop_pending = true;
    g_river_cloud.xiaozhi_tts_stop_deadline_ms =
        (uint64_t)rtos_time_get_current_system_time_ms() + (uint64_t)drain_ms;
}

void river_cloud_xiaozhi_reset_playback_state(void)
{
    bool had_playback_activity = river_cloud_xiaozhi_playback_output_active();
    bool duplex_ready_seen = g_river_cloud.xiaozhi_playback_duplex_ready_seen;

    g_river_cloud.xiaozhi_playback_active = false;
    river_cloud_xiaozhi_cancel_playback_stop();
    river_cloud_xiaozhi_clear_downlink_starvation_watch();
    river_cloud_xiaozhi_clear_playback_terminal_wait();
    if (had_playback_activity && !duplex_ready_seen) {
        g_river_cloud.xiaozhi_no_ref_reopen_rearm = true;
        g_river_cloud.xiaozhi_no_ref_reopen_silence_frames = 0U;
        g_river_cloud.xiaozhi_no_ref_reopen_guard_deadline_ms =
            (uint64_t)rtos_time_get_current_system_time_ms() +
            (uint64_t)RIVER_CLOUD_XIAOZHI_NOREF_REOPEN_GUARD_MS;
        RIVER_LOGI("xiaozhi no_ref reopen guard armed: tail_ms=%u silence_frames=%u duplex_seen=no",
                   (unsigned int)RIVER_CLOUD_XIAOZHI_NOREF_REOPEN_GUARD_MS,
                   (unsigned int)RIVER_CLOUD_XIAOZHI_NOREF_REARM_SILENCE_FRAMES);
    } else if (!had_playback_activity || duplex_ready_seen) {
        g_river_cloud.xiaozhi_no_ref_reopen_rearm = false;
        g_river_cloud.xiaozhi_no_ref_reopen_silence_frames = 0U;
        g_river_cloud.xiaozhi_no_ref_reopen_guard_deadline_ms = 0U;
    }
    g_river_cloud.xiaozhi_playback_duplex_ready_seen = false;
}

void river_cloud_xiaozhi_reset_downlink_state(void)
{
    if (g_river_cloud.xiaozhi_downlink_ring.initialized) {
        river_audio_frame_ring_reset(&g_river_cloud.xiaozhi_downlink_ring);
    }
    g_river_cloud.xiaozhi_downlink_ring_dropped = 0U;
    g_river_cloud.xiaozhi_downlink_retry_valid = false;
    g_river_cloud.xiaozhi_playback_rebuffer_pending = false;
    g_river_cloud.xiaozhi_playback_rebuffer_count = 0U;
    river_cloud_xiaozhi_clear_downlink_starvation_watch();
    river_cloud_xiaozhi_clear_playback_terminal_wait();
}

static uint32_t river_cloud_xiaozhi_downlink_start_threshold_frames(void)
{
    if (g_river_cloud.xiaozhi_playback_rebuffer_pending) {
        return RIVER_CLOUD_XIAOZHI_DOWNLINK_REBUFFER_START_FRAMES;
    }
    return RIVER_CLOUD_XIAOZHI_DOWNLINK_START_FRAMES;
}

static void river_cloud_xiaozhi_pop_playback_segment(void)
{
    river_cloud_xiaozhi_playback_segment_t *segment =
        river_cloud_xiaozhi_current_playback_segment();

    if (segment == NULL) {
        return;
    }

    memset(segment, 0, sizeof(*segment));
    g_river_cloud.xiaozhi_playback_segment_head =
        (g_river_cloud.xiaozhi_playback_segment_head + 1U) %
        RIVER_CLOUD_XIAOZHI_PLAYBACK_SEGMENTS_MAX;
    if (g_river_cloud.xiaozhi_playback_segment_count > 0U) {
        g_river_cloud.xiaozhi_playback_segment_count--;
    }
}

static void river_cloud_xiaozhi_note_playback_rebuffer(uint64_t paused_at_ms)
{
    river_cloud_xiaozhi_playback_segment_t *segment =
        river_cloud_xiaozhi_current_playback_segment();

    if (segment != NULL && segment->valid && segment->started && segment->paused_at_ms == 0U) {
        segment->paused_at_ms = paused_at_ms;
    }
    g_river_cloud.xiaozhi_playback_rebuffer_pending = true;
    if (g_river_cloud.xiaozhi_playback_rebuffer_count < UINT32_MAX) {
        g_river_cloud.xiaozhi_playback_rebuffer_count++;
    }
}

static void river_cloud_xiaozhi_finish_playback_rebuffer(uint64_t resumed_at_ms)
{
    river_cloud_xiaozhi_playback_segment_t *segment =
        river_cloud_xiaozhi_current_playback_segment();

    if (segment != NULL && segment->valid && segment->started && segment->paused_at_ms != 0U &&
        resumed_at_ms > segment->paused_at_ms) {
        segment->started_at_ms += resumed_at_ms - segment->paused_at_ms;
        RIVER_LOGW("xiaozhi playback rebuffer resumed: pause_ms=%lu queued=%lu count=%lu",
                   (unsigned long)(resumed_at_ms - segment->paused_at_ms),
                   (unsigned long)river_cloud_xiaozhi_playback_queued_frames(),
                   (unsigned long)g_river_cloud.xiaozhi_playback_rebuffer_count);
        segment->paused_at_ms = 0U;
    }

    g_river_cloud.xiaozhi_playback_rebuffer_pending = false;
    river_cloud_xiaozhi_clear_downlink_starvation_watch();
}

static bool river_cloud_xiaozhi_maybe_rebuffer_starved(uint64_t now_ms)
{
    uint64_t wait_ms;

    if (!g_river_cloud.xiaozhi_playback_active || g_river_cloud.xiaozhi_tts_stop_pending ||
        g_river_cloud.xiaozhi_playback_rebuffer_pending || !river_playback_service_active()) {
        river_cloud_xiaozhi_clear_downlink_starvation_watch();
        return false;
    }
    if (g_river_cloud.xiaozhi_playback_last_segment &&
        river_cloud_xiaozhi_current_playback_segment() == NULL) {
        river_cloud_xiaozhi_clear_downlink_starvation_watch();
        return false;
    }

    if (g_river_cloud.xiaozhi_downlink_starved_since_ms == 0U) {
        g_river_cloud.xiaozhi_downlink_starved_since_ms = now_ms;
        return false;
    }

    wait_ms = now_ms - g_river_cloud.xiaozhi_downlink_starved_since_ms;
    if (wait_ms < RIVER_CLOUD_XIAOZHI_DOWNLINK_STARVED_REBUFFER_MS) {
        return false;
    }

    river_cloud_xiaozhi_note_playback_rebuffer(now_ms);
    RIVER_LOGW("xiaozhi playback upstream gap rebuffer: wait_ms=%lu queued=0 start=%u count=%lu",
               (unsigned long)wait_ms,
               (unsigned int)river_cloud_xiaozhi_downlink_start_threshold_frames(),
               (unsigned long)g_river_cloud.xiaozhi_playback_rebuffer_count);
    (void)river_playback_service_stop_stream_ex("xiaozhi_playback_starved");
    river_cloud_xiaozhi_clear_downlink_starvation_watch();
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
    river_cloud_xiaozhi_copy_optional_text(g_river_cloud.xiaozhi_playback_terminal_ack,
                                           sizeof(g_river_cloud.xiaozhi_playback_terminal_ack),
                                           ack);
    river_cloud_xiaozhi_set_playback_terminal_state(ack, reason);
    g_river_cloud.xiaozhi_playback_cleared_reported =
        ack != NULL && strcmp(ack, "cleared") == 0;
    g_river_cloud.xiaozhi_playback_completed_reported =
        ack != NULL && strcmp(ack, "completed") == 0;
}

static void river_cloud_xiaozhi_mark_segment_fully_heard(
    const river_cloud_xiaozhi_playback_segment_t *segment)
{
    if (segment == NULL || !segment->valid || segment->segment_id[0] == '\0') {
        return;
    }

    river_cloud_xiaozhi_copy_optional_text(
        g_river_cloud.xiaozhi_playback_last_fully_heard_segment_id,
        sizeof(g_river_cloud.xiaozhi_playback_last_fully_heard_segment_id),
        segment->segment_id);
}

static bool river_cloud_xiaozhi_playback_completed_ready(void)
{
    if (!river_cloud_xiaozhi_playback_last_segment_observed()) {
        return false;
    }
    if (river_cloud_xiaozhi_current_playback_segment() != NULL) {
        return false;
    }
    if (g_river_cloud.xiaozhi_playback_last_fully_heard_segment_id[0] == '\0') {
        return false;
    }

    return strcmp(g_river_cloud.xiaozhi_playback_last_fully_heard_segment_id,
                  g_river_cloud.xiaozhi_playback_segment_id) == 0;
}

static const char *river_cloud_xiaozhi_playback_completed_wait_reason(void)
{
    river_cloud_xiaozhi_playback_segment_t *segment;

    if (!river_cloud_xiaozhi_playback_last_segment_observed()) {
        return "await_last_segment_meta";
    }

    segment = river_cloud_xiaozhi_current_playback_segment();
    if (segment != NULL) {
        return "await_segment_queue_drain";
    }
    if (g_river_cloud.xiaozhi_playback_last_fully_heard_segment_id[0] == '\0') {
        return "await_last_segment_tail";
    }
    if (strcmp(g_river_cloud.xiaozhi_playback_last_fully_heard_segment_id,
               g_river_cloud.xiaozhi_playback_segment_id) != 0) {
        return "await_last_segment_tail";
    }

    return NULL;
}

void river_cloud_xiaozhi_playback_note_meta(const river_xiaozhi_event_t *event)
{
    river_cloud_xiaozhi_playback_segment_t *segment = NULL;
    uint32_t index;
    uint32_t tail_index;

    if (event == NULL) {
        return;
    }

    if ((event->response_id != NULL && event->response_id[0] != '\0' &&
         strcmp(g_river_cloud.xiaozhi_playback_response_id, event->response_id) != 0) ||
        (event->playback_id != NULL && event->playback_id[0] != '\0' &&
         strcmp(g_river_cloud.xiaozhi_playback_id, event->playback_id) != 0)) {
        river_cloud_xiaozhi_clear_playback_meta_state();
    }

    river_cloud_xiaozhi_copy_optional_text(g_river_cloud.xiaozhi_playback_response_id,
                                           sizeof(g_river_cloud.xiaozhi_playback_response_id),
                                           event->response_id);
    river_cloud_xiaozhi_copy_optional_text(g_river_cloud.xiaozhi_playback_id,
                                           sizeof(g_river_cloud.xiaozhi_playback_id),
                                           event->playback_id);
    river_cloud_xiaozhi_copy_optional_text(g_river_cloud.xiaozhi_playback_segment_id,
                                           sizeof(g_river_cloud.xiaozhi_playback_segment_id),
                                           event->segment_id);
    if (event->text != NULL && event->text[0] != '\0') {
        river_cloud_xiaozhi_copy_optional_text(g_river_cloud.xiaozhi_playback_text,
                                               sizeof(g_river_cloud.xiaozhi_playback_text),
                                               event->text);
    }
    if (event->expected_duration_ms != 0U) {
        g_river_cloud.xiaozhi_playback_expected_duration_ms = event->expected_duration_ms;
    }
    g_river_cloud.xiaozhi_playback_last_segment = event->is_last_segment;
    g_river_cloud.xiaozhi_playback_meta_valid =
        g_river_cloud.xiaozhi_playback_response_id[0] != '\0' &&
        g_river_cloud.xiaozhi_playback_id[0] != '\0' &&
        g_river_cloud.xiaozhi_playback_segment_id[0] != '\0';
    g_river_cloud.xiaozhi_playback_started_reported =
        g_river_cloud.xiaozhi_playback_last_started_segment_id[0] != '\0' &&
        strcmp(g_river_cloud.xiaozhi_playback_last_started_segment_id,
               g_river_cloud.xiaozhi_playback_segment_id) == 0;
    g_river_cloud.xiaozhi_playback_cleared_reported =
        strcmp(g_river_cloud.xiaozhi_playback_terminal_ack, "cleared") == 0;
    g_river_cloud.xiaozhi_playback_completed_reported =
        strcmp(g_river_cloud.xiaozhi_playback_terminal_ack, "completed") == 0;

    for (index = 0U; index < g_river_cloud.xiaozhi_playback_segment_count; ++index) {
        uint32_t slot =
            (g_river_cloud.xiaozhi_playback_segment_head + index) %
            RIVER_CLOUD_XIAOZHI_PLAYBACK_SEGMENTS_MAX;
        river_cloud_xiaozhi_playback_segment_t *candidate =
            &g_river_cloud.xiaozhi_playback_segments[slot];

        if (candidate->valid &&
            strcmp(candidate->segment_id, g_river_cloud.xiaozhi_playback_segment_id) == 0) {
            segment = candidate;
            break;
        }
    }

    if (segment == NULL) {
        if (g_river_cloud.xiaozhi_playback_segment_count >=
            RIVER_CLOUD_XIAOZHI_PLAYBACK_SEGMENTS_MAX) {
            RIVER_LOGW("xiaozhi playback segment queue full: playback_id=%s segment_id=%s queued=%lu",
                       g_river_cloud.xiaozhi_playback_id[0] != '\0' ?
                           g_river_cloud.xiaozhi_playback_id :
                           "-",
                       g_river_cloud.xiaozhi_playback_segment_id[0] != '\0' ?
                           g_river_cloud.xiaozhi_playback_segment_id :
                           "-",
                       (unsigned long)g_river_cloud.xiaozhi_playback_segment_count);
            return;
        }
        tail_index =
            (g_river_cloud.xiaozhi_playback_segment_head +
             g_river_cloud.xiaozhi_playback_segment_count) %
            RIVER_CLOUD_XIAOZHI_PLAYBACK_SEGMENTS_MAX;
        segment = &g_river_cloud.xiaozhi_playback_segments[tail_index];
        memset(segment, 0, sizeof(*segment));
        segment->valid = true;
        g_river_cloud.xiaozhi_playback_segment_count++;
    }

    river_cloud_xiaozhi_copy_optional_text(segment->response_id,
                                           sizeof(segment->response_id),
                                           g_river_cloud.xiaozhi_playback_response_id);
    river_cloud_xiaozhi_copy_optional_text(segment->playback_id,
                                           sizeof(segment->playback_id),
                                           g_river_cloud.xiaozhi_playback_id);
    river_cloud_xiaozhi_copy_optional_text(segment->segment_id,
                                           sizeof(segment->segment_id),
                                           g_river_cloud.xiaozhi_playback_segment_id);
    if (g_river_cloud.xiaozhi_playback_text[0] != '\0') {
        river_cloud_xiaozhi_copy_optional_text(segment->text,
                                               sizeof(segment->text),
                                               g_river_cloud.xiaozhi_playback_text);
    }
    segment->expected_duration_ms = g_river_cloud.xiaozhi_playback_expected_duration_ms;
    segment->is_last_segment = g_river_cloud.xiaozhi_playback_last_segment;
}

static void river_cloud_xiaozhi_try_queue_playback_started_ack(void)
{
    river_status_t status;
    river_cloud_xiaozhi_playback_segment_t *segment =
        river_cloud_xiaozhi_current_playback_segment();

    if (!g_river_cloud.xiaozhi_playback_meta_valid || segment == NULL || !segment->valid ||
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
            g_river_cloud.xiaozhi_playback_last_started_segment_id,
            sizeof(g_river_cloud.xiaozhi_playback_last_started_segment_id),
            segment->segment_id);
        g_river_cloud.xiaozhi_playback_started_reported =
            strcmp(g_river_cloud.xiaozhi_playback_segment_id, segment->segment_id) == 0;
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

    if (!g_river_cloud.xiaozhi_playback_meta_valid ||
        g_river_cloud.xiaozhi_playback_last_fully_heard_segment_id[0] == '\0' ||
        !river_cloud_xiaozhi_playback_terminal_open()) {
        return false;
    }

    status = river_cloud_xiaozhi_control_request_async(
        RIVER_CLOUD_XIAOZHI_CTRL_PLAYBACK_CLEARED,
        reason,
        g_river_cloud.xiaozhi_playback_response_id,
        g_river_cloud.xiaozhi_playback_id,
        g_river_cloud.xiaozhi_playback_last_fully_heard_segment_id,
        0U);
    if (status == RIVER_OK) {
        RIVER_LOGI("xiaozhi playback ack cleared queued: response_id=%s playback_id=%s cleared_after_segment_id=%s reason=%s",
                   g_river_cloud.xiaozhi_playback_response_id[0] != '\0' ?
                       g_river_cloud.xiaozhi_playback_response_id :
                       "-",
                   g_river_cloud.xiaozhi_playback_id[0] != '\0' ?
                       g_river_cloud.xiaozhi_playback_id :
                       "-",
                   g_river_cloud.xiaozhi_playback_last_fully_heard_segment_id[0] != '\0' ?
                       g_river_cloud.xiaozhi_playback_last_fully_heard_segment_id :
                       "-",
                   reason != NULL && reason[0] != '\0' ? reason : "-");
        return true;
    } else {
        RIVER_LOGW("xiaozhi playback ack cleared queue failed: status=%d response_id=%s playback_id=%s cleared_after_segment_id=%s reason=%s",
                   (int)status,
                   g_river_cloud.xiaozhi_playback_response_id[0] != '\0' ?
                       g_river_cloud.xiaozhi_playback_response_id :
                       "-",
                   g_river_cloud.xiaozhi_playback_id[0] != '\0' ?
                       g_river_cloud.xiaozhi_playback_id :
                       "-",
                   g_river_cloud.xiaozhi_playback_last_fully_heard_segment_id[0] != '\0' ?
                       g_river_cloud.xiaozhi_playback_last_fully_heard_segment_id :
                       "-",
                   reason != NULL && reason[0] != '\0' ? reason : "-");
        return false;
    }
}

static void river_cloud_xiaozhi_update_playback_ack_progress(void)
{
    uint64_t now_ms;
    uint64_t next_segment_start_ms;
    river_cloud_xiaozhi_playback_segment_t *segment;

    if (!g_river_cloud.xiaozhi_playback_meta_valid ||
        !river_cloud_xiaozhi_playback_terminal_open()) {
        return;
    }
    if (g_river_cloud.xiaozhi_playback_rebuffer_pending) {
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
    uint64_t now_ms;
    const char *wait_reason;

    if (!g_river_cloud.xiaozhi_playback_meta_valid ||
        !river_cloud_xiaozhi_playback_terminal_open()) {
        river_cloud_xiaozhi_clear_playback_terminal_wait();
        return true;
    }

    river_cloud_xiaozhi_update_playback_ack_progress();
    if (!river_cloud_xiaozhi_playback_completed_ready()) {
        wait_reason = river_cloud_xiaozhi_playback_completed_wait_reason();
        river_cloud_xiaozhi_set_playback_terminal_wait(wait_reason);
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

    if (g_river_cloud.xiaozhi_playback_last_started_segment_id[0] == '\0') {
        river_cloud_xiaozhi_set_playback_terminal_state("local_completed", NULL);
        return true;
    }

    status = river_cloud_xiaozhi_control_request_async(
        RIVER_CLOUD_XIAOZHI_CTRL_PLAYBACK_COMPLETED,
        NULL,
        g_river_cloud.xiaozhi_playback_response_id,
        g_river_cloud.xiaozhi_playback_id,
        NULL,
        0U);
    if (status == RIVER_OK) {
        RIVER_LOGI("xiaozhi playback ack completed queued: response_id=%s playback_id=%s",
                   g_river_cloud.xiaozhi_playback_response_id[0] != '\0' ?
                       g_river_cloud.xiaozhi_playback_response_id :
                       "-",
                   g_river_cloud.xiaozhi_playback_id[0] != '\0' ?
                       g_river_cloud.xiaozhi_playback_id :
                       "-");
    } else {
        RIVER_LOGW("xiaozhi playback ack completed queue failed: status=%d response_id=%s playback_id=%s",
                   (int)status,
                   g_river_cloud.xiaozhi_playback_response_id[0] != '\0' ?
                       g_river_cloud.xiaozhi_playback_response_id :
                       "-",
                   g_river_cloud.xiaozhi_playback_id[0] != '\0' ?
                       g_river_cloud.xiaozhi_playback_id :
                       "-");
        river_cloud_xiaozhi_set_playback_terminal_state("local_completed", NULL);
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

    if (!g_river_cloud.xiaozhi_playback_meta_valid ||
        !river_cloud_xiaozhi_playback_terminal_open()) {
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

    if (g_river_cloud.xiaozhi_playback_last_fully_heard_segment_id[0] != '\0') {
        cleared_sent = river_cloud_xiaozhi_try_queue_playback_cleared_ack(reason);
    }
    if (cleared_sent) {
        river_cloud_xiaozhi_set_playback_terminal_ack("cleared", reason);
    } else {
        river_cloud_xiaozhi_set_playback_terminal_state("local_cleared", reason);
        RIVER_LOGI("xiaozhi playback clear kept local only: reason=%s last_fully_heard=%s",
                   reason != NULL && reason[0] != '\0' ? reason : "-",
                   g_river_cloud.xiaozhi_playback_last_fully_heard_segment_id[0] != '\0' ?
                       g_river_cloud.xiaozhi_playback_last_fully_heard_segment_id :
                       "-");
    }
}

void river_cloud_xiaozhi_playback_check_pending_stop(void)
{
    uint64_t now_ms;

    river_cloud_xiaozhi_update_playback_ack_progress();
    if (!g_river_cloud.xiaozhi_tts_stop_pending) {
        river_cloud_xiaozhi_clear_playback_terminal_wait();
        return;
    }

    if (!river_playback_service_active()) {
        if (river_cloud_xiaozhi_playback_queued_frames() != 0U) {
            river_cloud_xiaozhi_clear_playback_terminal_wait();
            return;
        }
        if (!river_cloud_xiaozhi_try_queue_playback_completed_ack()) {
            return;
        }
        g_river_cloud.xiaozhi_playback_rebuffer_pending = false;
        river_cloud_xiaozhi_reset_playback_state();
        return;
    }

    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    if (now_ms < g_river_cloud.xiaozhi_tts_stop_deadline_ms) {
        return;
    }

    (void)river_playback_service_stop_stream_ex("xiaozhi_tts_stop");
    if (!river_cloud_xiaozhi_try_queue_playback_completed_ack()) {
        return;
    }
    river_cloud_xiaozhi_reset_playback_state();
}

river_status_t river_cloud_xiaozhi_playback_abort(const char *clear_reason,
                                                  const char *stream_reason,
                                                  bool interrupt_stream)
{
    river_status_t status = RIVER_OK;

    if (river_cloud_xiaozhi_playback_has_work()) {
        river_cloud_xiaozhi_playback_finalize_cleared(clear_reason);
        river_cloud_xiaozhi_reset_downlink_state();
    }

    if (river_playback_service_active()) {
        const char *resolved_reason =
            (stream_reason != NULL && stream_reason[0] != '\0') ?
                stream_reason :
                "xiaozhi_playback_abort";

        status = interrupt_stream ?
                     river_playback_service_interrupt_stream_ex(resolved_reason) :
                     river_playback_service_stop_stream_ex(resolved_reason);
    }

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
    river_status_t status;
    uint32_t buffer_frames = RIVER_CLOUD_XIAOZHI_PLAYBACK_BUFFER_FRAMES;
    uint32_t start_frames = river_cloud_xiaozhi_downlink_start_threshold_frames();
    uint64_t now_ms;
    const bool reference_export = river_cloud_xiaozhi_playback_allows_vad_open();
    const char *mode = reference_export ? "ref" : "no_ref";

    if (g_river_cloud.xiaozhi_playback_active && river_playback_service_active()) {
        return RIVER_OK;
    }
    if (sample_rate == 0U || frame_duration_ms == 0U || mono_bytes == 0U) {
        return RIVER_ERR_ARG;
    }
    if (river_playback_service_active() && !g_river_cloud.xiaozhi_playback_active) {
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

    RIVER_LOGI("xiaozhi playback start: %luHz frame=%lums mono=%luB queued=%lu start=%u mode=%s buffer=%u gain=%d/%d rebuffer=%s",
               (unsigned long)sample_rate,
               (unsigned long)frame_duration_ms,
               (unsigned long)mono_bytes,
               (unsigned long)river_cloud_xiaozhi_playback_queued_frames(),
               (unsigned int)start_frames,
               mode,
               (unsigned int)buffer_frames,
               RIVER_CLOUD_XIAOZHI_PLAYBACK_GAIN_NUM,
               RIVER_CLOUD_XIAOZHI_PLAYBACK_GAIN_DEN,
               g_river_cloud.xiaozhi_playback_rebuffer_pending ? "yes" : "no");
    river_cloud_xiaozhi_mark_playback_started();
    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    river_cloud_xiaozhi_finish_playback_rebuffer(now_ms);
    river_cloud_xiaozhi_clear_downlink_starvation_watch();
    return RIVER_OK;
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
        g_river_cloud.xiaozhi_server_sample_rate = sample_rate;
        g_river_cloud.xiaozhi_server_frame_duration_ms = frame_duration_ms;
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
                                           g_river_cloud.xiaozhi_server_sample_rate;
    frame_duration_ms =
        event->frame_duration_ms != 0U ? event->frame_duration_ms :
                                         g_river_cloud.xiaozhi_server_frame_duration_ms;
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

    if (g_river_cloud.xiaozhi_downlink_ring.initialized &&
        g_river_cloud.xiaozhi_downlink_ring.frame_bytes != mono_bytes) {
        river_audio_frame_ring_deinit(&g_river_cloud.xiaozhi_downlink_ring);
    }
    if (!g_river_cloud.xiaozhi_downlink_ring.initialized) {
        status = river_audio_frame_ring_init_with_storage_ex(
            &g_river_cloud.xiaozhi_downlink_ring,
            g_river_cloud.xiaozhi_downlink_ring_storage,
            sizeof(g_river_cloud.xiaozhi_downlink_ring_storage),
            mono_bytes,
            RIVER_CLOUD_XIAOZHI_DOWNLINK_RING_FRAMES,
            RIVER_AUDIO_FRAME_RING_MODE_SPSC);
        if (status != RIVER_OK) {
            return status;
        }
    }

    g_river_cloud.xiaozhi_downlink_sample_rate = sample_rate;
    g_river_cloud.xiaozhi_downlink_frame_duration_ms = frame_duration_ms;

    status = river_audio_frame_ring_write(&g_river_cloud.xiaozhi_downlink_ring, mono_frame);
    if (status != RIVER_OK) {
        if (river_audio_frame_ring_read(&g_river_cloud.xiaozhi_downlink_ring,
                                        g_river_cloud.xiaozhi_downlink_drop_frame) == RIVER_OK &&
            river_audio_frame_ring_write(&g_river_cloud.xiaozhi_downlink_ring, mono_frame) ==
                RIVER_OK) {
            g_river_cloud.xiaozhi_downlink_ring_dropped++;
            RIVER_LOGW("xiaozhi downlink ring overflow: dropped=%lu queued=%lu capacity=%u",
                       (unsigned long)g_river_cloud.xiaozhi_downlink_ring_dropped,
                       (unsigned long)river_audio_frame_ring_count(&g_river_cloud.xiaozhi_downlink_ring),
                       (unsigned int)RIVER_CLOUD_XIAOZHI_DOWNLINK_RING_FRAMES);
            status = RIVER_OK;
        }
    }

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

    return river_cloud_xiaozhi_playback_queued_frames() > 0U ||
           river_cloud_xiaozhi_playback_output_active();
}

static void river_cloud_xiaozhi_downlink_task(void *arg)
{
    river_status_t status;
    size_t mono_bytes;
    size_t stereo_bytes;
    uint32_t queued_frames;
    uint32_t start_frames;
    uint64_t now_ms;

    (void)arg;

    for (;;) {
        if (!river_cloud_xiaozhi_downlink_active()) {
            rtos_time_delay_ms(RIVER_CLOUD_XIAOZHI_DOWNLINK_IDLE_MS);
            continue;
        }

        queued_frames = river_cloud_xiaozhi_playback_queued_frames();
        if (queued_frames == 0U) {
            now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
            river_cloud_xiaozhi_update_playback_ack_progress();
            if (river_cloud_xiaozhi_maybe_rebuffer_starved(now_ms)) {
                rtos_time_delay_ms(RIVER_CLOUD_XIAOZHI_DOWNLINK_POLL_MS);
                continue;
            }
            river_cloud_xiaozhi_playback_check_pending_stop();
            rtos_time_delay_ms(RIVER_CLOUD_XIAOZHI_DOWNLINK_POLL_MS);
            continue;
        }

        river_cloud_xiaozhi_clear_downlink_starvation_watch();

        if (!g_river_cloud.xiaozhi_playback_active || !river_playback_service_active()) {
            start_frames = river_cloud_xiaozhi_downlink_start_threshold_frames();
            if (!g_river_cloud.xiaozhi_tts_stop_pending && queued_frames < start_frames) {
                rtos_time_delay_ms(RIVER_CLOUD_XIAOZHI_DOWNLINK_POLL_MS);
                continue;
            }

            status = river_cloud_xiaozhi_start_playback_if_needed(
                g_river_cloud.xiaozhi_downlink_sample_rate,
                g_river_cloud.xiaozhi_downlink_frame_duration_ms,
                g_river_cloud.xiaozhi_downlink_ring.frame_bytes);
            if (status != RIVER_OK) {
                rtos_time_delay_ms(RIVER_CLOUD_XIAOZHI_DOWNLINK_POLL_MS);
                continue;
            }
        }

        if (!g_river_cloud.xiaozhi_downlink_retry_valid) {
            status = river_audio_frame_ring_read(&g_river_cloud.xiaozhi_downlink_ring,
                                                 g_river_cloud.xiaozhi_downlink_task_frame);
            if (status != RIVER_OK) {
                river_cloud_xiaozhi_playback_check_pending_stop();
                rtos_time_delay_ms(RIVER_CLOUD_XIAOZHI_DOWNLINK_POLL_MS);
                continue;
            }
        }

        mono_bytes = g_river_cloud.xiaozhi_downlink_ring.frame_bytes;
        stereo_bytes = mono_bytes * 2U;
        if (stereo_bytes > sizeof(g_river_cloud.xiaozhi_downlink_stereo)) {
            river_cloud_xiaozhi_playback_finalize_cleared("frame_oversize");
            river_cloud_xiaozhi_reset_downlink_state();
            (void)river_playback_service_stop_stream_ex("xiaozhi_downlink_frame_oversize");
            river_cloud_xiaozhi_reset_playback_state();
            continue;
        }

        river_cloud_xiaozhi_downlink_expand_stereo(g_river_cloud.xiaozhi_downlink_task_frame,
                                                   mono_bytes);
        if (river_playback_service_write((const uint8_t *)g_river_cloud.xiaozhi_downlink_stereo,
                                         stereo_bytes,
                                         g_river_cloud.xiaozhi_downlink_task_frame,
                                         mono_bytes,
                                         true) != RIVER_OK) {
            now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
            river_cloud_xiaozhi_note_playback_rebuffer(now_ms);
            g_river_cloud.xiaozhi_downlink_retry_valid = true;
            river_cloud_xiaozhi_clear_downlink_starvation_watch();
            RIVER_LOGW("xiaozhi playback write failed: mono=%luB stereo=%luB",
                       (unsigned long)mono_bytes,
                       (unsigned long)stereo_bytes);
            RIVER_LOGW("xiaozhi playback rebuffer requested: queued=%lu start=%u count=%lu",
                       (unsigned long)queued_frames,
                       (unsigned int)river_cloud_xiaozhi_downlink_start_threshold_frames(),
                       (unsigned long)g_river_cloud.xiaozhi_playback_rebuffer_count);
            (void)river_playback_service_stop_stream_ex("xiaozhi_playback_write_failed");
            rtos_time_delay_ms(RIVER_CLOUD_XIAOZHI_DOWNLINK_POLL_MS);
            continue;
        }

        g_river_cloud.xiaozhi_downlink_retry_valid = false;
        river_cloud_xiaozhi_clear_downlink_starvation_watch();
        now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
        river_cloud_xiaozhi_try_start_current_playback_segment(now_ms);
        river_cloud_xiaozhi_update_playback_ack_progress();
        river_cloud_xiaozhi_playback_check_pending_stop();
    }
}

void river_cloud_xiaozhi_playback_start_downlink_if_needed(void)
{
    if (!g_river_cloud.xiaozhi_enabled || g_river_cloud.xiaozhi_downlink_started) {
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

    g_river_cloud.xiaozhi_downlink_started = true;
    RIVER_LOGI("xiaozhi downlink worker started");
}
#endif
