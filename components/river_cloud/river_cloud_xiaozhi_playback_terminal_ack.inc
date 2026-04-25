/* Playback terminal ACK and lineage context update path. Included by
 * river_cloud_xiaozhi_playback_runtime.c so high-frequency ACK
 * progress stays grouped around the canonical playback lineage truth. */
static void river_cloud_xiaozhi_mark_segment_fully_heard(
    const river_cloud_xiaozhi_playback_segment_t *segment)
{
    if (segment == NULL || !segment->valid || segment->segment_id[0] == '\0') {
        return;
    }

    river_cloud_xiaozhi_copy_optional_text(
        g_river_cloud.xiaozhi_playback_lineage_truth.fully_heard_context.response_id,
        sizeof(g_river_cloud.xiaozhi_playback_lineage_truth.fully_heard_context.response_id),
        segment->response_id);
    river_cloud_xiaozhi_copy_optional_text(
        g_river_cloud.xiaozhi_playback_lineage_truth.fully_heard_context.playback_id,
        sizeof(g_river_cloud.xiaozhi_playback_lineage_truth.fully_heard_context.playback_id),
        segment->playback_id);
    river_cloud_xiaozhi_copy_optional_text(
        g_river_cloud.xiaozhi_playback_lineage_truth.fully_heard_context.segment_id,
        sizeof(g_river_cloud.xiaozhi_playback_lineage_truth.fully_heard_context.segment_id),
        segment->segment_id);
    river_cloud_xiaozhi_copy_optional_text(
        g_river_cloud.xiaozhi_playback_terminal_truth.last_fully_heard_context.response_id,
        sizeof(g_river_cloud.xiaozhi_playback_terminal_truth.last_fully_heard_context.response_id),
        g_river_cloud.xiaozhi_playback_lineage_truth.fully_heard_context.response_id);
    river_cloud_xiaozhi_copy_optional_text(
        g_river_cloud.xiaozhi_playback_terminal_truth.last_fully_heard_context.playback_id,
        sizeof(g_river_cloud.xiaozhi_playback_terminal_truth.last_fully_heard_context.playback_id),
        g_river_cloud.xiaozhi_playback_lineage_truth.fully_heard_context.playback_id);
    river_cloud_xiaozhi_copy_optional_text(
        g_river_cloud.xiaozhi_playback_terminal_truth.last_fully_heard_context.segment_id,
        sizeof(g_river_cloud.xiaozhi_playback_terminal_truth.last_fully_heard_context.segment_id),
        g_river_cloud.xiaozhi_playback_lineage_truth.fully_heard_context.segment_id);
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
        &g_river_cloud.xiaozhi_playback_lineage_truth.fully_heard_context,
        &g_river_cloud.xiaozhi_playback_lineage_truth.last_segment_context);
}

static bool river_cloud_xiaozhi_playback_last_fully_heard_context_valid(void)
{
    return river_cloud_xiaozhi_playback_segment_context_valid_from_truth(
        &g_river_cloud.xiaozhi_playback_lineage_truth.fully_heard_context);
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
            &g_river_cloud.xiaozhi_playback_lineage_truth.fully_heard_context,
            &g_river_cloud.xiaozhi_playback_lineage_truth.last_segment_context)) {
        return RIVER_CLOUD_PLAYBACK_TERMINAL_WAIT_LAST_SEGMENT_TAIL;
    }

    return RIVER_CLOUD_PLAYBACK_TERMINAL_WAIT_NONE;
}

void river_cloud_xiaozhi_playback_note_meta(const river_xiaozhi_event_t *event)
{
    river_cloud_xiaozhi_playback_segment_t *segment = NULL;
    river_cloud_xiaozhi_playback_meta_truth_t *meta_truth =
        &g_river_cloud.xiaozhi_playback_meta_truth;
    river_cloud_xiaozhi_playback_lineage_truth_t *lineage_truth =
        &g_river_cloud.xiaozhi_playback_lineage_truth;
    const river_cloud_xiaozhi_playback_context_truth_t *meta_context;
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
         lineage_truth->meta_context.response_id[0] != '\0' &&
         strcmp(lineage_truth->meta_context.response_id, event->response_id) != 0) ||
        (event->playback_id != NULL && event->playback_id[0] != '\0' &&
         lineage_truth->meta_context.playback_id[0] != '\0' &&
         strcmp(lineage_truth->meta_context.playback_id, event->playback_id) != 0)) {
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
    river_cloud_xiaozhi_touch_playback_lineage(
        RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_META_OBSERVED,
        &meta_truth->current_context,
        0U,
        NULL);
    meta_context = &lineage_truth->meta_context;
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
    river_cloud_xiaozhi_sync_playback_terminal_report_flags();

    for (index = 0U;
         index < g_river_cloud.xiaozhi_playback_segment_queue_truth.count;
         ++index) {
        uint32_t slot =
            (g_river_cloud.xiaozhi_playback_segment_queue_truth.head + index) %
            RIVER_CLOUD_XIAOZHI_PLAYBACK_SEGMENTS_MAX;
        river_cloud_xiaozhi_playback_segment_t *candidate =
            &g_river_cloud.xiaozhi_playback_segment_queue_truth.segments[slot];

        if (candidate->valid &&
            strcmp(candidate->segment_id, meta_context->segment_id) == 0) {
            segment = candidate;
            break;
        }
    }

    if (segment == NULL) {
        if (g_river_cloud.xiaozhi_playback_segment_queue_truth.count >=
            RIVER_CLOUD_XIAOZHI_PLAYBACK_SEGMENTS_MAX) {
            RIVER_LOGW("xiaozhi playback segment queue full: playback_id=%s segment_id=%s queued=%lu",
                       meta_context->playback_id[0] != '\0' ?
                           meta_context->playback_id :
                           "-",
                       meta_context->segment_id[0] != '\0' ?
                           meta_context->segment_id :
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
                                           meta_context->response_id);
    river_cloud_xiaozhi_copy_optional_text(segment->playback_id,
                                           sizeof(segment->playback_id),
                                           meta_context->playback_id);
    river_cloud_xiaozhi_copy_optional_text(segment->segment_id,
                                           sizeof(segment->segment_id),
                                           meta_context->segment_id);
    if (meta_truth->text[0] != '\0') {
        river_cloud_xiaozhi_copy_optional_text(segment->text,
                                               sizeof(segment->text),
                                               meta_truth->text);
    }
    segment->expected_duration_ms = meta_truth->expected_duration_ms;
    segment->is_last_segment = event->is_last_segment;
    start_gate = river_cloud_xiaozhi_refresh_start_gate();

    RIVER_LOGI("xiaozhi playback prefetch: segment_id=%s expected_ms=%lu meta_gap_ms=%lu target_ms=%lu policy=%s cautious=%s start=%u queued=%lu rebuffer=%s/%s rebuffer_total=%lu streak=%lu",
               meta_context->segment_id[0] != '\0' ?
                   meta_context->segment_id :
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

void river_cloud_xiaozhi_note_response_start_observation(const river_xiaozhi_event_t *event)
{
    river_cloud_xiaozhi_playback_context_truth_t context;

    if (event == NULL) {
        return;
    }

    river_cloud_xiaozhi_clear_playback_lineage_truth();
    memset(&context, 0, sizeof(context));
    river_cloud_xiaozhi_copy_optional_text(context.response_id,
                                           sizeof(context.response_id),
                                           event->response_id);
    river_cloud_xiaozhi_touch_playback_lineage(
        RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_RESPONSE_STARTED,
        &context,
        0U,
        NULL);
}

bool river_cloud_xiaozhi_check_response_audio_timeout(void)
{
    river_cloud_xiaozhi_playback_lineage_truth_t *lineage =
        &g_river_cloud.xiaozhi_playback_lineage_truth;
    uint64_t now_ms;
    uint64_t wait_ms;

    if (lineage->stage != RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_RESPONSE_STARTED ||
        lineage->audio_wait_timeout_reported ||
        lineage->updated_ms == 0U ||
        lineage->meta_context.response_id[0] != '\0') {
        return false;
    }

    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    if (now_ms < lineage->updated_ms) {
        return false;
    }

    wait_ms = now_ms - lineage->updated_ms;
    if (wait_ms < RIVER_CLOUD_XIAOZHI_RESPONSE_AUDIO_WAIT_TIMEOUT_MS) {
        return false;
    }

    lineage->audio_wait_timeout_reported = true;
    RIVER_LOGW("xiaozhi response audio wait timeout: response_id=%s wait_ms=%lu input_state=%s output_state=%s accepted=%s playback=%s queued=%lu",
               lineage->response_context.response_id[0] != '\0' ?
                   lineage->response_context.response_id :
                   "-",
               (unsigned long)wait_ms,
               g_river_cloud.xiaozhi_turn_semantics.input_state[0] != '\0' ?
                   g_river_cloud.xiaozhi_turn_semantics.input_state :
                   "-",
               g_river_cloud.xiaozhi_turn_semantics.output_state[0] != '\0' ?
                   g_river_cloud.xiaozhi_turn_semantics.output_state :
                   "-",
               g_river_cloud.xiaozhi_turn_semantics.accepted ? "yes" : "no",
               river_cloud_xiaozhi_playback_output_active() ? "active" : "idle",
               (unsigned long)river_cloud_xiaozhi_playback_queued_frames());
    return true;
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
        river_cloud_xiaozhi_touch_playback_lineage_from_segment(
            RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_SEGMENT_STARTED,
            segment,
            0U,
            NULL);
        river_cloud_xiaozhi_copy_optional_text(
            g_river_cloud.xiaozhi_playback_terminal_truth.last_started_segment_id,
            sizeof(g_river_cloud.xiaozhi_playback_terminal_truth.last_started_segment_id),
            segment->segment_id);
        river_cloud_xiaozhi_sync_playback_terminal_report_flags();
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
        river_cloud_xiaozhi_touch_playback_lineage_from_segment(
            RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_MARK_QUEUED,
            segment,
            played_duration_ms,
            NULL);
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
        g_river_cloud.xiaozhi_playback_lineage_truth.fully_heard_context.response_id;
    playback_id =
        g_river_cloud.xiaozhi_playback_lineage_truth.fully_heard_context.playback_id;
    segment_id =
        g_river_cloud.xiaozhi_playback_lineage_truth.fully_heard_context.segment_id;
    status = river_cloud_xiaozhi_control_request_async(
        RIVER_CLOUD_XIAOZHI_CTRL_PLAYBACK_CLEARED,
        reason,
        response_id,
        playback_id,
        segment_id,
        0U);
    if (status == RIVER_OK) {
        river_cloud_xiaozhi_touch_playback_lineage(
            RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_CLEARED_QUEUED,
            &g_river_cloud.xiaozhi_playback_lineage_truth.fully_heard_context,
            0U,
            reason);
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

    if (!river_cloud_xiaozhi_playback_lineage_started_any()) {
        river_cloud_xiaozhi_set_playback_terminal_state(
            RIVER_CLOUD_PLAYBACK_TERMINAL_STATE_LOCAL_COMPLETED,
            NULL);
        river_cloud_xiaozhi_touch_playback_lineage(
            RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_LOCAL_COMPLETED,
            &g_river_cloud.xiaozhi_playback_lineage_truth.last_segment_context,
            0U,
            NULL);
        river_cloud_xiaozhi_sync_playback_terminal_report_flags();
        return true;
    }
    if (!river_cloud_xiaozhi_playback_last_segment_observed()) {
        river_cloud_xiaozhi_set_playback_terminal_state(
            RIVER_CLOUD_PLAYBACK_TERMINAL_STATE_LOCAL_COMPLETED,
            NULL);
        river_cloud_xiaozhi_touch_playback_lineage(
            RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_LOCAL_COMPLETED,
            &g_river_cloud.xiaozhi_playback_lineage_truth.last_segment_context,
            0U,
            NULL);
        river_cloud_xiaozhi_sync_playback_terminal_report_flags();
        return true;
    }
    response_id = g_river_cloud.xiaozhi_playback_lineage_truth.last_segment_context.response_id;
    playback_id = g_river_cloud.xiaozhi_playback_lineage_truth.last_segment_context.playback_id;
    if (response_id[0] == '\0' || playback_id[0] == '\0') {
        river_cloud_xiaozhi_set_playback_terminal_state(
            RIVER_CLOUD_PLAYBACK_TERMINAL_STATE_LOCAL_COMPLETED,
            NULL);
        river_cloud_xiaozhi_touch_playback_lineage(
            RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_LOCAL_COMPLETED,
            &g_river_cloud.xiaozhi_playback_lineage_truth.last_segment_context,
            0U,
            NULL);
        river_cloud_xiaozhi_sync_playback_terminal_report_flags();
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
        river_cloud_xiaozhi_touch_playback_lineage(
            RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_COMPLETED_QUEUED,
            &g_river_cloud.xiaozhi_playback_lineage_truth.last_segment_context,
            0U,
            NULL);
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
        river_cloud_xiaozhi_touch_playback_lineage(
            RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_LOCAL_COMPLETED,
            &g_river_cloud.xiaozhi_playback_lineage_truth.last_segment_context,
            0U,
            NULL);
        river_cloud_xiaozhi_sync_playback_terminal_report_flags();
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

    if (g_river_cloud.xiaozhi_playback_lineage_truth.fully_heard_context.segment_id[0] !=
        '\0') {
        cleared_sent = river_cloud_xiaozhi_try_queue_playback_cleared_ack(reason);
    }
    if (cleared_sent) {
        river_cloud_xiaozhi_set_playback_terminal_ack("cleared", reason);
    } else {
        river_cloud_xiaozhi_set_playback_terminal_state(
            RIVER_CLOUD_PLAYBACK_TERMINAL_STATE_LOCAL_CLEARED,
            reason);
        river_cloud_xiaozhi_touch_playback_lineage(
            RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_LOCAL_CLEARED,
            &g_river_cloud.xiaozhi_playback_lineage_truth.fully_heard_context,
            0U,
            reason);
        river_cloud_xiaozhi_sync_playback_terminal_report_flags();
        RIVER_LOGI("xiaozhi playback clear kept local only: reason=%s last_fully_heard=%s",
                   reason != NULL && reason[0] != '\0' ? reason : "-",
                   g_river_cloud.xiaozhi_playback_lineage_truth.fully_heard_context
                               .segment_id[0] != '\0' ?
                       g_river_cloud.xiaozhi_playback_lineage_truth.fully_heard_context
                           .segment_id :
                       "-");
    }
}
