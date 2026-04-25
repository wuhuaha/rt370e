/* Playback lineage canonical-truth helpers. Included by
 * river_cloud_xiaozhi_playback_runtime.c to keep the single-truth
 * transition logic grouped while preserving private helper access. */
static const char *river_cloud_xiaozhi_playback_lineage_stage_name(
    river_cloud_xiaozhi_playback_lineage_stage_t stage)
{
    switch (stage) {
    case RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_RESPONSE_STARTED:
        return "response_started";
    case RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_META_OBSERVED:
        return "meta_observed";
    case RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_SEGMENT_STARTED:
        return "segment_started";
    case RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_MARK_QUEUED:
        return "mark_queued";
    case RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_CLEARED_QUEUED:
        return "cleared_queued";
    case RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_COMPLETED_QUEUED:
        return "completed_queued";
    case RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_LOCAL_CLEARED:
        return "local_cleared";
    case RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_LOCAL_COMPLETED:
        return "local_completed";
    case RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_NONE:
    default:
        return "none";
    }
}

static void river_cloud_xiaozhi_clear_playback_lineage_truth(void)
{
    memset(&g_river_cloud.xiaozhi_playback_lineage_truth,
           0,
           sizeof(g_river_cloud.xiaozhi_playback_lineage_truth));
}

static void river_cloud_xiaozhi_touch_playback_lineage(
    river_cloud_xiaozhi_playback_lineage_stage_t stage,
    const river_cloud_xiaozhi_playback_context_truth_t *context,
    uint32_t played_duration_ms,
    const char *reason)
{
    river_cloud_xiaozhi_playback_lineage_truth_t *lineage =
        &g_river_cloud.xiaozhi_playback_lineage_truth;

    lineage->stage = stage;
    lineage->updated_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    if (played_duration_ms != 0U) {
        lineage->last_mark_ms = played_duration_ms;
    }
    river_cloud_xiaozhi_copy_optional_text(lineage->reason,
                                           sizeof(lineage->reason),
                                           reason);

    switch (stage) {
    case RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_RESPONSE_STARTED:
        lineage->audio_wait_timeout_reported = false;
        river_cloud_xiaozhi_copy_playback_context_truth(&lineage->response_context,
                                                        context);
        break;
    case RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_META_OBSERVED:
        river_cloud_xiaozhi_copy_playback_context_truth(&lineage->meta_context,
                                                        context);
        if (lineage->response_context.response_id[0] == '\0' && context != NULL &&
            context->response_id[0] != '\0') {
            river_cloud_xiaozhi_copy_playback_context_truth(
                &lineage->response_context,
                context);
        }
        break;
    case RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_SEGMENT_STARTED:
        river_cloud_xiaozhi_copy_playback_context_truth(&lineage->started_context,
                                                        context);
        break;
    case RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_MARK_QUEUED:
        river_cloud_xiaozhi_copy_playback_context_truth(&lineage->marked_context,
                                                        context);
        break;
    case RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_CLEARED_QUEUED:
    case RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_LOCAL_CLEARED:
        river_cloud_xiaozhi_copy_playback_context_truth(&lineage->cleared_context,
                                                        context);
        break;
    case RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_COMPLETED_QUEUED:
    case RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_LOCAL_COMPLETED:
        river_cloud_xiaozhi_copy_playback_context_truth(&lineage->completed_context,
                                                        context);
        break;
    case RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_NONE:
    default:
        break;
    }
}

static void river_cloud_xiaozhi_touch_playback_lineage_from_segment(
    river_cloud_xiaozhi_playback_lineage_stage_t stage,
    const river_cloud_xiaozhi_playback_segment_t *segment,
    uint32_t played_duration_ms,
    const char *reason)
{
    river_cloud_xiaozhi_playback_context_truth_t context;

    if (segment == NULL) {
        return;
    }

    memset(&context, 0, sizeof(context));
    river_cloud_xiaozhi_copy_optional_text(context.response_id,
                                           sizeof(context.response_id),
                                           segment->response_id);
    river_cloud_xiaozhi_copy_optional_text(context.playback_id,
                                           sizeof(context.playback_id),
                                           segment->playback_id);
    river_cloud_xiaozhi_copy_optional_text(context.segment_id,
                                           sizeof(context.segment_id),
                                           segment->segment_id);
    river_cloud_xiaozhi_touch_playback_lineage(stage,
                                               &context,
                                               played_duration_ms,
                                               reason);
}

static bool river_cloud_xiaozhi_playback_lineage_started_for_meta(void)
{
    const river_cloud_xiaozhi_playback_lineage_truth_t *lineage =
        &g_river_cloud.xiaozhi_playback_lineage_truth;

    return river_cloud_xiaozhi_playback_same_segment_context(
        &lineage->started_context,
        &lineage->meta_context);
}

static bool river_cloud_xiaozhi_playback_lineage_started_any(void)
{
    return river_cloud_xiaozhi_playback_segment_context_valid_from_truth(
        &g_river_cloud.xiaozhi_playback_lineage_truth.started_context);
}

static bool river_cloud_xiaozhi_playback_lineage_cleared(void)
{
    const river_cloud_xiaozhi_playback_lineage_truth_t *lineage =
        &g_river_cloud.xiaozhi_playback_lineage_truth;
    river_cloud_xiaozhi_playback_lineage_stage_t stage =
        lineage->stage;

    return river_cloud_xiaozhi_playback_segment_context_valid_from_truth(
               &lineage->cleared_context) ||
           stage == RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_CLEARED_QUEUED ||
           stage == RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_LOCAL_CLEARED;
}

static bool river_cloud_xiaozhi_playback_lineage_completed(void)
{
    const river_cloud_xiaozhi_playback_lineage_truth_t *lineage =
        &g_river_cloud.xiaozhi_playback_lineage_truth;
    river_cloud_xiaozhi_playback_lineage_stage_t stage =
        lineage->stage;

    return river_cloud_xiaozhi_playback_response_context_valid_from_truth(
               &lineage->completed_context) ||
           stage == RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_COMPLETED_QUEUED ||
           stage == RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_LOCAL_COMPLETED;
}

static void river_cloud_xiaozhi_sync_playback_terminal_report_flags(void)
{
    g_river_cloud.xiaozhi_playback_terminal_truth.started_reported =
        river_cloud_xiaozhi_playback_lineage_started_for_meta();
    g_river_cloud.xiaozhi_playback_terminal_truth.cleared_reported =
        river_cloud_xiaozhi_playback_lineage_cleared();
    g_river_cloud.xiaozhi_playback_terminal_truth.completed_reported =
        river_cloud_xiaozhi_playback_lineage_completed();
}
