#include "river_cloud_xiaozhi_playback_rebuffer_recovery.inc"

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

#include "river_cloud_xiaozhi_playback_abort_policy.inc"

static void river_cloud_xiaozhi_maybe_complete_terminal_playback_after_progress(
    const char *trigger)
{
    river_cloud_xiaozhi_playback_truth_view_t truth_view;

    if (!river_cloud_xiaozhi_playback_terminal_open()) {
        return;
    }
    (void)river_cloud_xiaozhi_maybe_finish_zero_duration_tail_on_drain(
        river_cloud_xiaozhi_playback_queued_frames(),
        trigger);
    if (!river_cloud_xiaozhi_playback_completed_ready()) {
        return;
    }

    river_cloud_xiaozhi_capture_playback_truth_view(&truth_view);
    if (river_cloud_xiaozhi_playback_backend_stream_attached(
            truth_view.backend_state)) {
        if (!truth_view.tts_stop_pending) {
            river_cloud_xiaozhi_arm_playback_stop(
                RIVER_CLOUD_XIAOZHI_PLAYBACK_DRAIN_MS);
        }
        return;
    }

    if (!river_cloud_xiaozhi_try_queue_playback_completed_ack()) {
        return;
    }
    RIVER_LOGI("xiaozhi playback terminal completed after progress: trigger=%s drain_ms=%u",
               trigger != NULL ? trigger : "-",
               0U);
}

#include "river_cloud_xiaozhi_playback_downlink_cycle.inc"

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

static bool river_cloud_xiaozhi_should_drop_late_completed_audio(void)
{
    river_cloud_xiaozhi_playback_truth_view_t truth_view;

    if (!river_cloud_xiaozhi_playback_terminal_open() ||
        !g_river_cloud.xiaozhi_playback_runtime_truth.stop_pending ||
        !river_cloud_xiaozhi_playback_completed_ready()) {
        return false;
    }

    river_cloud_xiaozhi_capture_playback_truth_view(&truth_view);
    RIVER_LOGW("xiaozhi late completed audio dropped: queued=%lu phase=%s backend=%s stop_pending=yes",
               (unsigned long)river_cloud_xiaozhi_playback_queued_frames(),
               river_cloud_playback_phase_name(
                   river_cloud_xiaozhi_playback_observed_phase_kind()),
               river_cloud_playback_backend_state_name(truth_view.backend_state));
    river_cloud_xiaozhi_maybe_complete_terminal_playback_after_progress(
        "late_completed_audio");
    return true;
}

river_status_t river_cloud_xiaozhi_playback_handle_audio_event(
    const river_xiaozhi_event_t *event)
{
    const uint8_t *mono_frame;
    size_t mono_bytes;
    uint32_t sample_rate;
    uint32_t frame_duration_ms;
    size_t frame_bytes;
    size_t offset;
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
    if (river_cloud_xiaozhi_should_drop_late_completed_audio()) {
        return RIVER_OK;
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

    frame_bytes = river_cloud_xiaozhi_downlink_pcm_frame_bytes(sample_rate,
                                                               frame_duration_ms);
    if (frame_bytes == 0U || frame_bytes > RIVER_CLOUD_XIAOZHI_DOWNLINK_PCM_BYTES_MAX) {
        RIVER_LOGW("xiaozhi downlink pcm frame bytes invalid: sample_rate=%lu frame_ms=%lu frame_bytes=%lu chunk_bytes=%lu",
                   (unsigned long)sample_rate,
                   (unsigned long)frame_duration_ms,
                   (unsigned long)frame_bytes,
                   (unsigned long)mono_bytes);
        return RIVER_ERR_ARG;
    }

    status = river_cloud_xiaozhi_ensure_downlink_ring(frame_bytes);
    if (status != RIVER_OK) {
        return status;
    }

    river_cloud_xiaozhi_note_downlink_stream_format(sample_rate,
                                                    frame_duration_ms);
    (void)river_cloud_xiaozhi_refresh_start_gate();
    offset = 0U;
    while (offset < mono_bytes) {
        size_t remaining = mono_bytes - offset;
        size_t copy_bytes = frame_bytes;
        uint8_t *frame_buf = g_river_cloud.xiaozhi_downlink_accum;

        if ((g_river_cloud.xiaozhi_downlink_runtime_truth.accum_bytes + remaining) <
            frame_bytes) {
            memcpy(frame_buf + g_river_cloud.xiaozhi_downlink_runtime_truth.accum_bytes,
                   mono_frame + offset,
                   remaining);
            g_river_cloud.xiaozhi_downlink_runtime_truth.accum_bytes += remaining;
            offset = mono_bytes;
            break;
        }

        if (g_river_cloud.xiaozhi_downlink_runtime_truth.accum_bytes > 0U) {
            copy_bytes = frame_bytes -
                         g_river_cloud.xiaozhi_downlink_runtime_truth.accum_bytes;
            memcpy(frame_buf + g_river_cloud.xiaozhi_downlink_runtime_truth.accum_bytes,
                   mono_frame + offset,
                   copy_bytes);
            offset += copy_bytes;
            g_river_cloud.xiaozhi_downlink_runtime_truth.accum_bytes = 0U;
        } else {
            frame_buf = (uint8_t *)(mono_frame + offset);
            offset += frame_bytes;
        }

        status = river_cloud_xiaozhi_write_downlink_frame_latest(frame_buf);
        if (status != RIVER_OK) {
            return status;
        }
    }

    if (river_cloud_xiaozhi_playback_terminal_open()) {
        river_cloud_xiaozhi_cancel_playback_stop();
    }
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
        river_cloud_xiaozhi_downlink_task_cycle_result_t cycle_result =
            river_cloud_xiaozhi_process_downlink_task_cycle();
        river_cloud_xiaozhi_finish_downlink_task_cycle(&cycle_result);
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
