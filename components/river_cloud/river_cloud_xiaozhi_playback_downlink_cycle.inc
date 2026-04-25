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

static river_cloud_xiaozhi_downlink_playback_prepare_result_t
river_cloud_xiaozhi_prepare_downlink_playback(uint32_t queued_frames, uint64_t now_ms)
{
    river_cloud_xiaozhi_downlink_playback_prepare_result_t result = {
        .wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_PLAYBACK_NOT_READY,
        .status = RIVER_OK,
        .ready = false,
    };
    river_cloud_xiaozhi_playback_truth_view_t truth_view;
    uint32_t start_frames;
    river_status_t status;

    river_cloud_xiaozhi_capture_playback_truth_view(&truth_view);
    if (!river_cloud_xiaozhi_rebuffer_resume_ready(queued_frames,
                                                   now_ms,
                                                   truth_view.backend_state)) {
        result.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_REBUFFER_WAIT;
        return result;
    }
    if (g_river_cloud.xiaozhi_playback_runtime_truth.stop_pending &&
        truth_view.backend_state != RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_ACTIVE) {
        river_cloud_xiaozhi_playback_check_pending_stop();
        result.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_STOP_PENDING;
        return result;
    }
    if (truth_view.backend_state == RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_PAUSED) {
        if (!river_cloud_xiaozhi_maybe_resume_paused_playback(queued_frames,
                                                              &truth_view)) {
            result.wait_kind =
                RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_PAUSED_RESUME_WAIT;
            return result;
        }
        river_cloud_xiaozhi_capture_playback_truth_view(&truth_view);
    }
    if (truth_view.backend_state == RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_ACTIVE) {
        result.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_NONE;
        result.ready = true;
        return result;
    }
    if (truth_view.backend_state == RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_RECOVERING) {
        result.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_BACKEND_RECOVERING;
        return result;
    }

    start_frames = river_cloud_xiaozhi_downlink_start_threshold_for_backend(
        truth_view.backend_state);
    if (!g_river_cloud.xiaozhi_playback_runtime_truth.stop_pending &&
        queued_frames < start_frames) {
        result.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_START_THRESHOLD;
        return result;
    }

    status = river_cloud_xiaozhi_start_playback_if_needed(
        river_cloud_xiaozhi_downlink_sample_rate(),
        river_cloud_xiaozhi_downlink_frame_duration_ms(),
        g_river_cloud.xiaozhi_downlink_ring.frame_bytes);
    if (status != RIVER_OK) {
        result.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_PLAYBACK_START_FAILED;
        result.status = status;
        return result;
    }

    result.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_NONE;
    result.ready = true;
    return result;
}

static bool river_cloud_xiaozhi_downlink_active(void);

static river_cloud_xiaozhi_downlink_cycle_plan_t
river_cloud_xiaozhi_prepare_downlink_cycle_plan(void)
{
    river_cloud_xiaozhi_downlink_cycle_plan_t plan = {
        .step_result = RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_SLEEP_POLL,
        .wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_NOT_READY,
        .status = RIVER_OK,
        .queued_frames = 0U,
        .ready = false,
    };
    river_cloud_xiaozhi_downlink_playback_prepare_result_t prepare_result;
    uint32_t queued_frames;
    uint64_t now_ms;

    if (!river_cloud_xiaozhi_downlink_active()) {
        plan.step_result = RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_SLEEP_IDLE;
        plan.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_INACTIVE;
        return plan;
    }

    queued_frames = river_cloud_xiaozhi_playback_queued_frames();
    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    river_cloud_xiaozhi_update_playback_ack_progress();
    if (river_cloud_xiaozhi_maybe_rebuffer_starved(queued_frames, now_ms)) {
        plan.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_STARVED;
        return plan;
    }
    if (river_cloud_xiaozhi_maybe_pause_for_segment_gap()) {
        plan.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_SEGMENT_GAP;
        return plan;
    }
    if (queued_frames == 0U) {
        river_cloud_xiaozhi_playback_check_pending_stop();
        plan.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_EMPTY;
        return plan;
    }
    prepare_result = river_cloud_xiaozhi_prepare_downlink_playback(queued_frames,
                                                                   now_ms);
    if (!prepare_result.ready) {
        plan.wait_kind = prepare_result.wait_kind;
        plan.status = prepare_result.status;
        return plan;
    }

    plan.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_NONE;
    plan.queued_frames = queued_frames;
    plan.ready = true;
    return plan;
}

static river_cloud_xiaozhi_downlink_frame_acquire_result_t
river_cloud_xiaozhi_acquire_current_downlink_frame(void)
{
    river_cloud_xiaozhi_downlink_frame_acquire_result_t result = {
        .step_result = RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_SLEEP_POLL,
        .status = RIVER_OK,
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
        result.status = status;
        return result;
    }

    result.acquired = true;
    result.step_result = RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_CONTINUE;
    return result;
}

static river_cloud_xiaozhi_downlink_cycle_outcome_t
river_cloud_xiaozhi_downlink_write_cycle_outcome(
    const river_cloud_xiaozhi_downlink_frame_write_result_t *write_result)
{
    if (write_result == NULL) {
        return RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_STEP_POLICY;
    }
    if (write_result->aborted) {
        return RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_ABORTED;
    }
    if (write_result->write_failed) {
        return write_result->frame_consumed ?
                   RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_WRITE_RECOVERED :
                   RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_WRITE_FAILED;
    }
    if (write_result->frame_consumed) {
        return RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_WRITE_OK;
    }
    return RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_STEP_POLICY;
}

static river_cloud_xiaozhi_downlink_task_cycle_result_t
river_cloud_xiaozhi_process_downlink_task_cycle(void)
{
    river_cloud_xiaozhi_downlink_task_cycle_result_t result = {
        .step_result = RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_CONTINUE,
        .status = RIVER_OK,
        .outcome = RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_NONE,
    };

    result.cycle_plan = river_cloud_xiaozhi_prepare_downlink_cycle_plan();
    if (!result.cycle_plan.ready) {
        result.status = result.cycle_plan.status;
        result.outcome =
            result.cycle_plan.wait_kind ==
                    RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_INACTIVE ?
                RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_INACTIVE :
                RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_NOT_READY;
        result.step_result = result.cycle_plan.step_result;
        return result;
    }

    result.acquire_result = river_cloud_xiaozhi_acquire_current_downlink_frame();
    if (!result.acquire_result.acquired) {
        result.status = result.acquire_result.status;
        result.outcome = RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_ACQUIRE_MISS;
        result.step_result = result.acquire_result.step_result;
        return result;
    }

    result.write_result = river_cloud_xiaozhi_write_current_downlink_frame_step(
        result.cycle_plan.queued_frames);
    result.status = result.write_result.status;
    result.outcome =
        river_cloud_xiaozhi_downlink_write_cycle_outcome(&result.write_result);
    result.step_result = result.write_result.step_result;
    return result;
}

static river_cloud_xiaozhi_downlink_task_wait_plan_t
river_cloud_xiaozhi_build_downlink_task_wait_plan(
    const river_cloud_xiaozhi_downlink_task_cycle_result_t *cycle_result)
{
    river_cloud_xiaozhi_downlink_task_wait_plan_t wait_plan = {
        .sleep = false,
        .delay_ms = 0U,
        .status = RIVER_OK,
        .wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_NONE,
    };

    if (cycle_result == NULL) {
        return wait_plan;
    }

    wait_plan.status = cycle_result->status;
    if (cycle_result->step_result ==
        RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_SLEEP_IDLE) {
        wait_plan.sleep = true;
        wait_plan.delay_ms = RIVER_CLOUD_XIAOZHI_DOWNLINK_IDLE_MS;
        wait_plan.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_INACTIVE;
    } else if (cycle_result->step_result ==
               RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STEP_SLEEP_POLL) {
        wait_plan.sleep = true;
        wait_plan.delay_ms = RIVER_CLOUD_XIAOZHI_DOWNLINK_POLL_MS;
        if (!cycle_result->cycle_plan.ready) {
            wait_plan.wait_kind =
                cycle_result->cycle_plan.wait_kind !=
                        RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_NONE ?
                    cycle_result->cycle_plan.wait_kind :
                    RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_NOT_READY;
        } else if (!cycle_result->acquire_result.acquired) {
            wait_plan.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_ACQUIRE_MISS;
        } else if (cycle_result->write_result.write_failed) {
            wait_plan.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_WRITE_FAILED;
        } else {
            wait_plan.wait_kind = RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_STEP_POLICY;
        }
    }

    return wait_plan;
}

static void river_cloud_xiaozhi_finish_downlink_task_cycle(
    const river_cloud_xiaozhi_downlink_task_cycle_result_t *cycle_result)
{
    river_cloud_xiaozhi_downlink_task_wait_plan_t wait_plan =
        river_cloud_xiaozhi_build_downlink_task_wait_plan(cycle_result);

    g_river_cloud.xiaozhi_downlink_runtime_truth.last_wait_kind =
        wait_plan.wait_kind;
    g_river_cloud.xiaozhi_downlink_runtime_truth.last_wait_delay_ms =
        wait_plan.delay_ms;
    g_river_cloud.xiaozhi_downlink_runtime_truth.last_cycle_status =
        wait_plan.status;
    g_river_cloud.xiaozhi_downlink_runtime_truth.last_cycle_outcome =
        cycle_result != NULL ? cycle_result->outcome :
                               RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_NONE;
    if (wait_plan.sleep) {
        rtos_time_delay_ms(wait_plan.delay_ms);
    }
}
