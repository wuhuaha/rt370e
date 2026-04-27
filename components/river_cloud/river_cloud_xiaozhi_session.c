/* 小智会话编排：处理唤醒准入、会话窗口、ASR/TTS 与回放衔接。 */
#include <stdio.h>
#include <string.h>

#include "rtk_status.h"

#include "river/river_log.h"
#include "river/river_playback_service.h"
#include "river/river_runtime_stats.h"
#include "river/river_voice_profile.h"
#include "river/river_voice_runtime_policy.h"

#include "river_cloud_internal.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.cloud"

#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
bool river_cloud_xiaozhi_full_duplex_experiment_enabled(void)
{
#if defined(CONFIG_RIVER_XIAOZHI_FULL_DUPLEX_EXPERIMENT_EN) && \
    CONFIG_RIVER_XIAOZHI_FULL_DUPLEX_EXPERIMENT_EN
    return true;
#else
    return false;
#endif
}

void river_cloud_xiaozhi_get_duplex_ready_eval(river_voice_duplex_ready_eval_t *eval)
{
    river_voice_runtime_duplex_ready_eval(river_cloud_xiaozhi_full_duplex_experiment_enabled(),
                                          river_voice_profile_active_preproc(),
                                          eval);
}

static const char *river_cloud_xiaozhi_runtime_duplex_fallback_reason(
    const river_voice_duplex_ready_eval_t *eval)
{
    if (eval == NULL) {
        return "half_duplex_aec_blocked";
    }

    switch (eval->reason) {
    case RIVER_VOICE_DUPLEX_READY_EXPERIMENT_OFF:
        return "half_duplex_experiment_disabled";
    case RIVER_VOICE_DUPLEX_READY_PROFILE_NO_REF:
        return "half_duplex_no_playback_reference";
    case RIVER_VOICE_DUPLEX_READY_PLAYBACK_RESTART_PENDING:
        return "half_duplex_restart_pending";
    case RIVER_VOICE_DUPLEX_READY_REF_IDLE:
        return "half_duplex_ref_idle";
    case RIVER_VOICE_DUPLEX_READY_AEC_BLOCKED:
    default:
        return "half_duplex_aec_blocked";
    }
}

const char *river_cloud_xiaozhi_duplex_fallback_reason(
    const river_voice_duplex_ready_eval_t *eval)
{
    const char *policy_reason;

    policy_reason = river_xiaozhi_duplex_default_fallback_reason();
    if (policy_reason != NULL) {
        return policy_reason;
    }
    if (eval != NULL && eval->ready) {
        return NULL;
    }
    return river_cloud_xiaozhi_runtime_duplex_fallback_reason(eval);
}

bool river_cloud_xiaozhi_idle_requires_wakeword(void)
{
#if defined(CONFIG_RIVER_VOICE_CAPABILITY_KWS) && CONFIG_RIVER_VOICE_CAPABILITY_KWS
    return true;
#else
    return false;
#endif
}

static uint32_t river_cloud_xiaozhi_ms_to_frames(uint32_t duration_ms, uint32_t frame_ms)
{
    if (frame_ms == 0U || duration_ms == 0U) {
        return 0U;
    }

    return (duration_ms + frame_ms - 1U) / frame_ms;
}

static bool river_cloud_xiaozhi_io_owner(void)
{
    return g_river_cloud.xiaozhi_io_task != NULL &&
           rtos_task_handle_get() == g_river_cloud.xiaozhi_io_task;
}

static bool river_cloud_xiaozhi_control_lock(void)
{
    return g_river_cloud.xiaozhi_control_lock != NULL &&
           rtos_mutex_take(g_river_cloud.xiaozhi_control_lock,
                           MUTEX_WAIT_TIMEOUT) == RTK_SUCCESS;
}

static void river_cloud_xiaozhi_control_unlock(void)
{
    if (g_river_cloud.xiaozhi_control_lock != NULL) {
        (void)rtos_mutex_give(g_river_cloud.xiaozhi_control_lock);
    }
}

static void river_cloud_xiaozhi_init_control_request(
    river_cloud_xiaozhi_control_request_t *request,
    river_cloud_xiaozhi_control_op_t op,
    const char *arg)
{
    if (request == NULL) {
        return;
    }

    memset(request, 0, sizeof(*request));
    request->op = op;
    if (arg != NULL && arg[0] != '\0') {
        snprintf(request->arg, sizeof(request->arg), "%s", arg);
    }
}

static river_status_t river_cloud_xiaozhi_queue_control_request(
    const river_cloud_xiaozhi_control_request_t *request,
    uint32_t wait_ms)
{
    if (request == NULL) {
        return RIVER_ERR_ARG;
    }

    if (g_river_cloud.xiaozhi_control_ready == NULL ||
        g_river_cloud.xiaozhi_control_space == NULL) {
        return RIVER_ERR_BUSY;
    }

    if (rtos_sema_take(g_river_cloud.xiaozhi_control_space, wait_ms) != RTK_SUCCESS) {
        return RIVER_ERR_BUSY;
    }

    if (!river_cloud_xiaozhi_control_lock()) {
        (void)rtos_sema_give(g_river_cloud.xiaozhi_control_space);
        return RIVER_ERR_BUSY;
    }

    g_river_cloud.xiaozhi_control_queue
        [g_river_cloud.xiaozhi_control_queue_truth.write_index] = *request;
    g_river_cloud.xiaozhi_control_queue_truth.write_index++;
    if (g_river_cloud.xiaozhi_control_queue_truth.write_index >=
        RIVER_CLOUD_XIAOZHI_CONTROL_QUEUE_DEPTH) {
        g_river_cloud.xiaozhi_control_queue_truth.write_index = 0U;
    }
    if (g_river_cloud.xiaozhi_control_queue_truth.count < UINT32_MAX) {
        g_river_cloud.xiaozhi_control_queue_truth.count++;
    }
    if (g_river_cloud.xiaozhi_control_queue_truth.count >
        g_river_cloud.xiaozhi_control_queue_truth.high_watermark) {
        g_river_cloud.xiaozhi_control_queue_truth.high_watermark =
            g_river_cloud.xiaozhi_control_queue_truth.count;
    }
    river_cloud_xiaozhi_control_unlock();

    (void)rtos_sema_give(g_river_cloud.xiaozhi_control_ready);
    return RIVER_OK;
}

static uint32_t river_cloud_xiaozhi_uplink_busy_backoff_ms(uint32_t frame_ms,
                                                           uint32_t busy_streak)
{
    uint32_t delay_ms;
    uint32_t shift_count;
    uint32_t soft_cap_ms;

    /*
     * The websocket pump already runs at 5 ms cadence. Backing off from that
     * base keeps the uplink tail near real-time instead of letting the local
     * ring run far ahead of transport recovery.
     */
    delay_ms = RIVER_CLOUD_XIAOZHI_UPLINK_POLL_MS;
    if (delay_ms == 0U) {
        delay_ms = 1U;
    }

    shift_count = busy_streak > 0U ? (busy_streak - 1U) : 0U;
    if (shift_count > 3U) {
        shift_count = 3U;
    }

    while (shift_count > 0U && delay_ms < RIVER_CLOUD_XIAOZHI_UPLINK_BUSY_BACKOFF_MAX_MS) {
        delay_ms <<= 1U;
        shift_count--;
    }

    if (delay_ms > RIVER_CLOUD_XIAOZHI_UPLINK_BUSY_BACKOFF_MAX_MS) {
        delay_ms = RIVER_CLOUD_XIAOZHI_UPLINK_BUSY_BACKOFF_MAX_MS;
    }

    soft_cap_ms = frame_ms != 0U ? (frame_ms * 2U) : (RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS * 2U);
    if (soft_cap_ms != 0U && delay_ms > soft_cap_ms) {
        delay_ms = soft_cap_ms;
    }

    return delay_ms;
}

static void river_cloud_xiaozhi_note_uplink_burst(uint32_t frames)
{
    if (frames == 0U) {
        return;
    }

    if (frames > g_river_cloud.xiaozhi_uplink_runtime_truth.burst_max) {
        g_river_cloud.xiaozhi_uplink_runtime_truth.burst_max = frames;
    }
    if (g_river_cloud.xiaozhi_asr_round_truth.active &&
        frames > g_river_cloud.xiaozhi_asr_round_truth.burst_max) {
        g_river_cloud.xiaozhi_asr_round_truth.burst_max = frames;
    }
}

static bool river_cloud_xiaozhi_uplink_preview_warmup_active(void)
{
    uint32_t audio_ms;

    if (!g_river_cloud.xiaozhi_asr_round_truth.active) {
        return false;
    }
    if (RIVER_CLOUD_XIAOZHI_UPLINK_PREVIEW_WARMUP_MS == 0U) {
        return false;
    }
    if (g_river_cloud.xiaozhi_asr_round_truth.packets_sent >
        (UINT32_MAX / RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS)) {
        return false;
    }

    audio_ms = g_river_cloud.xiaozhi_asr_round_truth.packets_sent *
               RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS;
    return audio_ms < RIVER_CLOUD_XIAOZHI_UPLINK_PREVIEW_WARMUP_MS;
}

static uint32_t river_cloud_xiaozhi_uplink_drain_burst_limit(void)
{
    if (river_cloud_xiaozhi_uplink_preview_warmup_active() &&
        RIVER_CLOUD_XIAOZHI_UPLINK_PREVIEW_BURST_MAX >
            RIVER_CLOUD_XIAOZHI_UPLINK_DRAIN_BURST_MAX) {
        return RIVER_CLOUD_XIAOZHI_UPLINK_PREVIEW_BURST_MAX;
    }

    return RIVER_CLOUD_XIAOZHI_UPLINK_DRAIN_BURST_MAX;
}

static bool river_cloud_xiaozhi_uplink_can_bypass_due_for_preview(void)
{
    if (!river_cloud_xiaozhi_uplink_preview_warmup_active()) {
        return false;
    }

    return g_river_cloud.xiaozhi_uplink_runtime_truth.retry_valid ||
           river_cloud_xiaozhi_uplink_ready_frames() > 0U;
}

static bool river_cloud_xiaozhi_uplink_can_bypass_due_for_backlog(void)
{
    return g_river_cloud.xiaozhi_uplink_runtime_truth.retry_valid ||
           river_cloud_xiaozhi_uplink_ready_frames() > 1U;
}

static void river_cloud_xiaozhi_log_uplink_backpressure(uint64_t now_ms, uint32_t backoff_ms)
{
    if (g_river_cloud.xiaozhi_uplink_runtime_truth.last_busy_log_ms != 0U &&
        (now_ms - g_river_cloud.xiaozhi_uplink_runtime_truth.last_busy_log_ms) <
            RIVER_CLOUD_XIAOZHI_UPLINK_BUSY_LOG_INTERVAL_MS) {
        return;
    }

    g_river_cloud.xiaozhi_uplink_runtime_truth.last_busy_log_ms = now_ms;
    RIVER_LOGW("xiaozhi uplink backpressure: queued=%lu/%u busy=%lu streak=%lu backoff=%lums stale_drop=%lu",
               (unsigned long)river_cloud_xiaozhi_uplink_ready_frames(),
               (unsigned int)RIVER_CLOUD_XIAOZHI_UPLINK_RING_FRAMES,
               (unsigned long)g_river_cloud.xiaozhi_uplink_runtime_truth.busy_count,
               (unsigned long)g_river_cloud.xiaozhi_uplink_runtime_truth.busy_streak,
               (unsigned long)backoff_ms,
               (unsigned long)g_river_cloud.xiaozhi_uplink_runtime_truth.stale_dropped);
}

static bool river_cloud_xiaozhi_control_pending(void)
{
    return g_river_cloud.xiaozhi_control_ready != NULL &&
           rtos_sema_get_count(g_river_cloud.xiaozhi_control_ready) > 0U;
}

static bool river_cloud_xiaozhi_transport_active(void)
{
    if (!g_river_cloud.initialized || !g_river_cloud.xiaozhi_enabled) {
        return false;
    }

    return river_xiaozhi_session_open() || river_cloud_xiaozhi_listening_active();
}

bool river_cloud_xiaozhi_uplink_active(void)
{
    uint32_t queued_frames;

    if (!g_river_cloud.initialized || !g_river_cloud.xiaozhi_enabled ||
        !g_river_cloud.xiaozhi_uplink_ring.initialized) {
        return false;
    }

    queued_frames = river_cloud_xiaozhi_uplink_ready_frames();
    return river_cloud_xiaozhi_uplink_keepalive_needed(queued_frames);
}

river_status_t river_cloud_xiaozhi_apply_bridge_open_capture_policy(
    const river_cloud_asr_audio_desc_t *audio)
{
    uint32_t xiaozhi_pre_roll_frames;
    river_status_t status;

    if (audio == NULL) {
        return RIVER_ERR_ARG;
    }

    if (audio->sample_rate != RIVER_XIAOZHI_UPLINK_SAMPLE_RATE ||
        audio->channels != RIVER_XIAOZHI_UPLINK_CHANNELS ||
        audio->bits_per_sample != 16U) {
        return RIVER_ERR_UNSUPPORTED;
    }

    xiaozhi_pre_roll_frames = river_cloud_xiaozhi_ms_to_frames(
        RIVER_CLOUD_XIAOZHI_PRE_ROLL_MAX_MS,
        audio->frame_ms);
    if (xiaozhi_pre_roll_frames > 0U &&
        g_river_cloud.pre_roll_capacity_frames > xiaozhi_pre_roll_frames) {
        g_river_cloud.pre_roll_capacity_frames = xiaozhi_pre_roll_frames;
    }

    if (g_river_cloud.xiaozhi_uplink_ring.initialized &&
        g_river_cloud.xiaozhi_uplink_ring.frame_bytes !=
            RIVER_CLOUD_XIAOZHI_UPLINK_PCM_FRAME_MAX) {
        river_audio_frame_ring_deinit(&g_river_cloud.xiaozhi_uplink_ring);
    }
    if (!g_river_cloud.xiaozhi_uplink_ring.initialized) {
        status = river_audio_frame_ring_init_with_storage_ex(
            &g_river_cloud.xiaozhi_uplink_ring,
            g_river_cloud.xiaozhi_uplink_ring_storage,
            sizeof(g_river_cloud.xiaozhi_uplink_ring_storage),
            RIVER_CLOUD_XIAOZHI_UPLINK_PCM_FRAME_MAX,
            RIVER_CLOUD_XIAOZHI_UPLINK_RING_FRAMES,
            RIVER_AUDIO_FRAME_RING_MODE_SPSC);
        if (status != RIVER_OK) {
            return status;
        }
    }

    river_audio_frame_ring_reset(&g_river_cloud.xiaozhi_uplink_ring);
    g_river_cloud.xiaozhi_uplink_runtime_truth.ring_dropped = 0U;
    g_river_cloud.xiaozhi_uplink_runtime_truth.busy_count = 0U;
    g_river_cloud.xiaozhi_uplink_runtime_truth.fail_count = 0U;
    g_river_cloud.xiaozhi_uplink_runtime_truth.stale_dropped = 0U;
    g_river_cloud.xiaozhi_uplink_runtime_truth.timestamp_ms =
        (uint32_t)rtos_time_get_current_system_time_ms();
    g_river_cloud.xiaozhi_uplink_runtime_truth.next_send_ms = 0U;
    g_river_cloud.xiaozhi_uplink_runtime_truth.busy_streak = 0U;
    g_river_cloud.xiaozhi_uplink_runtime_truth.burst_max = 0U;
    g_river_cloud.xiaozhi_uplink_runtime_truth.last_busy_log_ms = 0U;
    g_river_cloud.xiaozhi_uplink_runtime_truth.accum_bytes = 0U;
    g_river_cloud.xiaozhi_uplink_runtime_truth.retry_valid = false;
    g_river_cloud.xiaozhi_uplink_runtime_truth.open_speech_frames = 0U;

    RIVER_LOGI("xiaozhi uplink audio: %luHz/%luch frame=%lums pcm=%luB",
               (unsigned long)audio->sample_rate,
               (unsigned long)audio->channels,
               (unsigned long)RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS,
               (unsigned long)RIVER_CLOUD_XIAOZHI_UPLINK_PCM_FRAME_MAX);
    RIVER_LOGI("xiaozhi listen gate: pre_roll_cap=%lums open_hold_frames=%u",
               (unsigned long)(g_river_cloud.pre_roll_capacity_frames * audio->frame_ms),
               (unsigned int)RIVER_CLOUD_XIAOZHI_OPEN_HOLD_FRAMES);
    return RIVER_OK;
}

void river_cloud_xiaozhi_handle_transport_event(const river_xiaozhi_event_t *event)
{
    if (event == NULL) {
        return;
    }

    river_cloud_xiaozhi_refresh_turn_semantics("event");

    switch (event->type) {
    case RIVER_XIAOZHI_EVENT_SERVER_HELLO:
        river_cloud_xiaozhi_note_server_hello_observation(event);
        return;
    case RIVER_XIAOZHI_EVENT_STT:
        river_cloud_xiaozhi_note_stt_observation(event);
        return;
    case RIVER_XIAOZHI_EVENT_INPUT_SPEECH_START:
    case RIVER_XIAOZHI_EVENT_INPUT_PREVIEW:
    case RIVER_XIAOZHI_EVENT_INPUT_ACCEPT_READY:
    case RIVER_XIAOZHI_EVENT_INPUT_ENDPOINT:
        river_cloud_xiaozhi_note_input_observation(event);
        return;
    case RIVER_XIAOZHI_EVENT_AUDIO_OUT_META:
        river_cloud_xiaozhi_note_audio_out_meta_observation(event);
        return;
    case RIVER_XIAOZHI_EVENT_RESPONSE_START:
        river_cloud_xiaozhi_note_response_start_observation(event);
        return;
    case RIVER_XIAOZHI_EVENT_LLM:
        river_cloud_xiaozhi_note_llm_observation(event);
        return;
    case RIVER_XIAOZHI_EVENT_TTS:
        river_cloud_xiaozhi_note_tts_observation(event);
        return;
    case RIVER_XIAOZHI_EVENT_AUDIO:
        (void)river_cloud_xiaozhi_playback_handle_audio_event(event);
        return;
    case RIVER_XIAOZHI_EVENT_SESSION_CLOSED:
        river_cloud_xiaozhi_note_session_closed_observation(event);
        return;
    case RIVER_XIAOZHI_EVENT_ERROR:
        river_cloud_xiaozhi_note_error_observation(event);
        return;
    default:
        return;
    }
}

river_status_t river_cloud_xiaozhi_execute_control_transport(
    const river_cloud_xiaozhi_control_request_t *request)
{
    if (request == NULL) {
        return RIVER_ERR_ARG;
    }

    switch (request->op) {
    case RIVER_CLOUD_XIAOZHI_CTRL_OPEN_AND_LISTEN:
        return river_cloud_xiaozhi_execute_open_and_listen_transport(request->arg);

    case RIVER_CLOUD_XIAOZHI_CTRL_LISTEN_STOP:
    case RIVER_CLOUD_XIAOZHI_CTRL_ABORT:
    case RIVER_CLOUD_XIAOZHI_CTRL_CLOSE_SESSION:
        return river_cloud_xiaozhi_execute_session_control_transport(request->op,
                                                                     request->arg);

    case RIVER_CLOUD_XIAOZHI_CTRL_PLAYBACK_STARTED:
    case RIVER_CLOUD_XIAOZHI_CTRL_PLAYBACK_MARK:
    case RIVER_CLOUD_XIAOZHI_CTRL_PLAYBACK_CLEARED:
    case RIVER_CLOUD_XIAOZHI_CTRL_PLAYBACK_COMPLETED:
        return river_cloud_xiaozhi_execute_playback_control_transport(request);

    default:
        return RIVER_ERR_ARG;
    }
}

void river_cloud_xiaozhi_process_control_queue(void)
{
    river_cloud_xiaozhi_control_request_t request;
    river_status_t status;

    if (g_river_cloud.xiaozhi_control_ready == NULL ||
        g_river_cloud.xiaozhi_control_space == NULL) {
        return;
    }

    while (rtos_sema_take(g_river_cloud.xiaozhi_control_ready, 0U) == RTK_SUCCESS) {
        if (!river_cloud_xiaozhi_control_lock()) {
            (void)rtos_sema_give(g_river_cloud.xiaozhi_control_ready);
            return;
        }

        request = g_river_cloud.xiaozhi_control_queue
                      [g_river_cloud.xiaozhi_control_queue_truth.read_index];
        g_river_cloud.xiaozhi_control_queue_truth.read_index++;
        if (g_river_cloud.xiaozhi_control_queue_truth.read_index >=
            RIVER_CLOUD_XIAOZHI_CONTROL_QUEUE_DEPTH) {
            g_river_cloud.xiaozhi_control_queue_truth.read_index = 0U;
        }
        if (g_river_cloud.xiaozhi_control_queue_truth.count > 0U) {
            g_river_cloud.xiaozhi_control_queue_truth.count--;
        }
        river_cloud_xiaozhi_control_unlock();

        (void)rtos_sema_give(g_river_cloud.xiaozhi_control_space);
        status = river_cloud_xiaozhi_execute_control_transport(&request);
        if (request.result_out != NULL) {
            *request.result_out = status;
        }
        if (request.completion != NULL) {
            (void)rtos_sema_give(request.completion);
        }
    }
}

static river_status_t river_cloud_xiaozhi_control_request(
    river_cloud_xiaozhi_control_op_t op,
    const char *arg)
{
    river_cloud_xiaozhi_control_request_t request;
    river_status_t result = RIVER_ERR_BUSY;
    rtos_sema_t completion = NULL;
    river_status_t status;

    if (!g_river_cloud.initialized || !g_river_cloud.xiaozhi_enabled) {
        return RIVER_ERR_UNSUPPORTED;
    }

    river_cloud_xiaozhi_init_control_request(&request, op, arg);
    if (river_cloud_xiaozhi_io_owner()) {
        return river_cloud_xiaozhi_execute_control_transport(&request);
    }

    if (rtos_sema_create_binary(&completion) != RTK_SUCCESS) {
        return RIVER_ERR_NO_MEMORY;
    }

    request.completion = completion;
    request.result_out = &result;
    status = river_cloud_xiaozhi_queue_control_request(&request,
                                                       RIVER_CLOUD_XIAOZHI_CONTROL_WAIT_MS);
    if (status != RIVER_OK) {
        rtos_sema_delete(completion);
        return status;
    }

    if (rtos_sema_take(completion, RIVER_CLOUD_XIAOZHI_CONTROL_WAIT_MS) != RTK_SUCCESS) {
        rtos_sema_delete(completion);
        return RIVER_ERR_BUSY;
    }

    rtos_sema_delete(completion);
    return result;
}

river_status_t river_cloud_xiaozhi_control_request_async(
    river_cloud_xiaozhi_control_op_t op,
    const char *arg,
    const char *response_id,
    const char *playback_id,
    const char *segment_id,
    uint32_t played_duration_ms)
{
    river_cloud_xiaozhi_control_request_t request;

    if (!g_river_cloud.initialized || !g_river_cloud.xiaozhi_enabled) {
        return RIVER_ERR_UNSUPPORTED;
    }

    river_cloud_xiaozhi_init_control_request(&request, op, arg);
    request.played_duration_ms = played_duration_ms;
    if (response_id != NULL && response_id[0] != '\0') {
        snprintf(request.response_id, sizeof(request.response_id), "%s", response_id);
    }
    if (playback_id != NULL && playback_id[0] != '\0') {
        snprintf(request.playback_id, sizeof(request.playback_id), "%s", playback_id);
    }
    if (segment_id != NULL && segment_id[0] != '\0') {
        snprintf(request.segment_id, sizeof(request.segment_id), "%s", segment_id);
    }

    if (river_cloud_xiaozhi_io_owner()) {
        return river_cloud_xiaozhi_execute_control_transport(&request);
    }

    return river_cloud_xiaozhi_queue_control_request(&request, 0U);
}

river_status_t river_cloud_xiaozhi_request_open_and_listen(const char *mode)
{
    river_status_t status;

    status = river_cloud_xiaozhi_control_request(RIVER_CLOUD_XIAOZHI_CTRL_OPEN_AND_LISTEN,
                                                 mode);
    if (status == RIVER_OK) {
        river_cloud_xiaozhi_apply_open_and_listen_session_policy();
    }
    return status;
}

river_status_t river_cloud_xiaozhi_request_listen_stop(void)
{
    return river_cloud_xiaozhi_control_request(RIVER_CLOUD_XIAOZHI_CTRL_LISTEN_STOP, NULL);
}

river_status_t river_cloud_xiaozhi_request_abort(const char *reason)
{
    return river_cloud_xiaozhi_control_request(RIVER_CLOUD_XIAOZHI_CTRL_ABORT, reason);
}

river_status_t river_cloud_xiaozhi_request_close_session(void)
{
    return river_cloud_xiaozhi_control_request(RIVER_CLOUD_XIAOZHI_CTRL_CLOSE_SESSION, NULL);
}

static void river_cloud_xiaozhi_run_endpoint_local_close_housekeeping(void)
{
    char endpoint_soft_close_reason[RIVER_CLOUD_XIAOZHI_PREVIEW_REASON_MAX];

    endpoint_soft_close_reason[0] = '\0';
    if (river_cloud_xiaozhi_poll_endpoint_soft_close_timeout(
            endpoint_soft_close_reason,
            sizeof(endpoint_soft_close_reason))) {
        river_cloud_xiaozhi_complete_active_stream_finish(
            RIVER_CLOUD_XIAOZHI_STREAM_FINISH_ENDPOINT_TIMEOUT,
            endpoint_soft_close_reason);
    }

    river_cloud_xiaozhi_check_local_close_timeout();
}

void river_cloud_xiaozhi_run_io_tick_housekeeping(void)
{
    river_cloud_xiaozhi_refresh_turn_semantics("io_tick");
    river_cloud_xiaozhi_check_window_timeout();
    river_cloud_xiaozhi_run_endpoint_local_close_housekeeping();
}

void river_cloud_xiaozhi_note_server_hello_observation(const river_xiaozhi_event_t *event)
{
    if (event == NULL) {
        return;
    }

    g_river_cloud.xiaozhi_server_audio_format_truth.sample_rate =
        event->sample_rate != 0U ? event->sample_rate : 16000U;
    g_river_cloud.xiaozhi_server_audio_format_truth.frame_duration_ms =
        event->frame_duration_ms != 0U ? event->frame_duration_ms :
                                         RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS;
    river_cloud_xiaozhi_copy_session_id_from_transport();
}

void river_cloud_xiaozhi_copy_optional_text(char *dst,
                                            size_t dst_size,
                                            const char *src)
{
    if (dst == NULL || dst_size == 0U) {
        return;
    }
    if (src == NULL || src[0] == '\0') {
        dst[0] = '\0';
        return;
    }

    snprintf(dst, dst_size, "%s", src);
}

uint32_t river_cloud_xiaozhi_uplink_ready_frames(void)
{
    uint32_t ready_frames;

    ready_frames = river_audio_frame_ring_count(&g_river_cloud.xiaozhi_uplink_ring);
    if (g_river_cloud.xiaozhi_uplink_runtime_truth.retry_valid && ready_frames < UINT32_MAX) {
        ready_frames++;
    }
    return ready_frames;
}

uint32_t river_cloud_xiaozhi_trim_uplink_stale_frames(uint32_t keep_frames)
{
    uint32_t dropped = 0U;

    if (!g_river_cloud.xiaozhi_uplink_ring.initialized) {
        return 0U;
    }

    while (river_cloud_xiaozhi_uplink_ready_frames() > keep_frames) {
        if (river_audio_frame_ring_count(&g_river_cloud.xiaozhi_uplink_ring) == 0U) {
            break;
        }
        if (river_audio_frame_ring_read(&g_river_cloud.xiaozhi_uplink_ring,
                                        g_river_cloud.xiaozhi_uplink_drop_frame) != RIVER_OK) {
            break;
        }
        dropped++;
    }

    if (dropped != 0U) {
        g_river_cloud.xiaozhi_uplink_runtime_truth.stale_dropped += dropped;
    }
    return dropped;
}

static river_status_t river_cloud_xiaozhi_queue_uplink_packet(const uint8_t *pcm,
                                                              size_t pcm_bytes)
{
    river_status_t status;
    uint32_t keep_before_write;

    if (pcm == NULL || pcm_bytes == 0U || !g_river_cloud.xiaozhi_uplink_ring.initialized) {
        return RIVER_ERR_ARG;
    }

    /*
     * Keep the realtime uplink queue short. Once the transport falls behind,
     * older audio is less valuable than preserving a low-latency tail for live
     * ASR. This mirrors the reference xiaozhi client more closely than waiting
     * for the full 64-frame ring to overflow.
     */
    keep_before_write = (RIVER_CLOUD_XIAOZHI_UPLINK_STALE_FRAMES_MAX > 0U) ?
                            (RIVER_CLOUD_XIAOZHI_UPLINK_STALE_FRAMES_MAX - 1U) :
                            0U;
    (void)river_cloud_xiaozhi_trim_uplink_stale_frames(keep_before_write);

    status = river_audio_frame_ring_write(&g_river_cloud.xiaozhi_uplink_ring, pcm);
    if (status == RIVER_OK) {
        return RIVER_OK;
    }

    if (river_audio_frame_ring_read(&g_river_cloud.xiaozhi_uplink_ring,
                                    g_river_cloud.xiaozhi_uplink_drop_frame) != RIVER_OK) {
        return status;
    }

    status = river_audio_frame_ring_write(&g_river_cloud.xiaozhi_uplink_ring, pcm);
    if (status == RIVER_OK) {
        g_river_cloud.xiaozhi_uplink_runtime_truth.ring_dropped++;
        RIVER_LOGW("xiaozhi uplink ring overflow: dropped=%lu queued=%lu capacity=%u",
                   (unsigned long)g_river_cloud.xiaozhi_uplink_runtime_truth.ring_dropped,
                   (unsigned long)river_audio_frame_ring_count(&g_river_cloud.xiaozhi_uplink_ring),
                   (unsigned int)RIVER_CLOUD_XIAOZHI_UPLINK_RING_FRAMES);
    }
    return status;
}

river_status_t river_cloud_xiaozhi_push_pcm(const uint8_t *pcm, size_t pcm_bytes)
{
    size_t frame_bytes;

    if (pcm == NULL || pcm_bytes == 0U || !g_river_cloud.xiaozhi_uplink_ring.initialized) {
        return RIVER_ERR_ARG;
    }

    frame_bytes = RIVER_CLOUD_XIAOZHI_UPLINK_PCM_FRAME_MAX;
    if ((g_river_cloud.xiaozhi_uplink_runtime_truth.accum_bytes + pcm_bytes) >
        sizeof(g_river_cloud.xiaozhi_uplink_accum)) {
        return RIVER_ERR_BUSY;
    }

    memcpy(g_river_cloud.xiaozhi_uplink_accum + g_river_cloud.xiaozhi_uplink_runtime_truth.accum_bytes,
           pcm,
           pcm_bytes);
    g_river_cloud.xiaozhi_uplink_runtime_truth.accum_bytes += pcm_bytes;

    while (g_river_cloud.xiaozhi_uplink_runtime_truth.accum_bytes >= frame_bytes) {
        river_status_t status;

        status = river_cloud_xiaozhi_queue_uplink_packet(g_river_cloud.xiaozhi_uplink_accum,
                                                         frame_bytes);
        if (status != RIVER_OK) {
            return status;
        }
        g_river_cloud.xiaozhi_uplink_runtime_truth.accum_bytes -= frame_bytes;
        if (g_river_cloud.xiaozhi_uplink_runtime_truth.accum_bytes > 0U) {
            memmove(g_river_cloud.xiaozhi_uplink_accum,
                    g_river_cloud.xiaozhi_uplink_accum + frame_bytes,
                    g_river_cloud.xiaozhi_uplink_runtime_truth.accum_bytes);
        }
    }

    return RIVER_OK;
}

void river_cloud_xiaozhi_run_uplink_io_once(void)
{
    river_status_t status;
    bool can_send;
    uint32_t frame_ms;
    uint64_t now_ms;
    uint32_t drained_frames = 0U;
    uint32_t burst_limit;

    if (!river_cloud_xiaozhi_uplink_active()) {
        g_river_cloud.xiaozhi_uplink_runtime_truth.next_send_ms = 0U;
        g_river_cloud.xiaozhi_uplink_runtime_truth.busy_streak = 0U;
        return;
    }

    can_send = river_cloud_xiaozhi_uplink_send_ready();
    if (can_send) {
        (void)river_cloud_xiaozhi_trim_uplink_stale_frames(
            RIVER_CLOUD_XIAOZHI_UPLINK_STALE_FRAMES_MAX);
    }

    frame_ms = RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS;
    while (true) {
        uint64_t due_ms;
        uint32_t ready_before_send;
        uint32_t capture_age_ms = 0U;
        uint64_t send_begin_ms;
        uint32_t send_duration_ms = 0U;
        bool preview_bypass_due = false;
        bool backlog_bypass_due = false;
        bool bypass_due = false;

        burst_limit = river_cloud_xiaozhi_uplink_drain_burst_limit();
        if (drained_frames >= burst_limit) {
            break;
        }

        now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
        due_ms = g_river_cloud.xiaozhi_uplink_runtime_truth.next_send_ms;
        if (due_ms != 0U && now_ms < due_ms) {
            preview_bypass_due = river_cloud_xiaozhi_uplink_can_bypass_due_for_preview();
            backlog_bypass_due = river_cloud_xiaozhi_uplink_can_bypass_due_for_backlog();
            bypass_due = preview_bypass_due || backlog_bypass_due;
        }
        if (due_ms != 0U && now_ms < due_ms && !bypass_due) {
            break;
        }

        if (!g_river_cloud.xiaozhi_uplink_runtime_truth.retry_valid) {
            status = river_audio_frame_ring_read(&g_river_cloud.xiaozhi_uplink_ring,
                                                 g_river_cloud.xiaozhi_uplink_task_frame);
            if (status != RIVER_OK) {
                break;
            }
            g_river_cloud.xiaozhi_uplink_runtime_truth.retry_valid = true;
        }

        can_send = river_cloud_xiaozhi_uplink_send_ready();
        if (!can_send) {
            g_river_cloud.xiaozhi_uplink_runtime_truth.busy_streak = 0U;
            g_river_cloud.xiaozhi_uplink_runtime_truth.next_send_ms = 0U;
            river_cloud_xiaozhi_maybe_finalize_listen_stop(
                river_cloud_xiaozhi_uplink_ready_frames(),
                g_river_cloud.xiaozhi_uplink_runtime_truth.accum_bytes);
            return;
        }

        ready_before_send = river_cloud_xiaozhi_uplink_ready_frames();
        if (ready_before_send > 0U) {
            uint64_t age_ms64 =
                ((uint64_t)(ready_before_send - 1U) * (uint64_t)frame_ms);
            if (age_ms64 > UINT32_MAX) {
                capture_age_ms = UINT32_MAX;
            } else {
                capture_age_ms = (uint32_t)age_ms64;
            }
        }
        send_begin_ms = (uint64_t)rtos_time_get_current_system_time_ms();
        status = river_cloud_xiaozhi_send_uplink_transport(
            g_river_cloud.xiaozhi_uplink_task_frame,
            RIVER_CLOUD_XIAOZHI_UPLINK_PCM_FRAME_MAX,
            g_river_cloud.xiaozhi_uplink_runtime_truth.timestamp_ms);
        now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
        if (now_ms >= send_begin_ms) {
            uint64_t duration_ms64 = now_ms - send_begin_ms;

            send_duration_ms = duration_ms64 > UINT32_MAX ? UINT32_MAX : (uint32_t)duration_ms64;
        }
        if (status == RIVER_OK) {
            uint64_t next_due_ms;

            g_river_cloud.xiaozhi_uplink_runtime_truth.timestamp_ms +=
                RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS;
            river_cloud_xiaozhi_round_note_packet_sent(now_ms,
                                                       ready_before_send,
                                                       capture_age_ms,
                                                       send_duration_ms);
            if (preview_bypass_due &&
                g_river_cloud.xiaozhi_asr_round_truth.preview_warmup_bypass_count <
                    UINT32_MAX) {
                g_river_cloud.xiaozhi_asr_round_truth.preview_warmup_bypass_count++;
            }
            g_river_cloud.xiaozhi_uplink_runtime_truth.retry_valid = false;
            g_river_cloud.xiaozhi_uplink_runtime_truth.busy_streak = 0U;
            next_due_ms = (due_ms != 0U && !bypass_due) ?
                              (due_ms + (uint64_t)frame_ms) :
                              (now_ms + (uint64_t)frame_ms);
            g_river_cloud.xiaozhi_uplink_runtime_truth.next_send_ms = next_due_ms;
            drained_frames++;
            continue;
        }

        if (status == RIVER_ERR_BUSY) {
            uint32_t backoff_ms;

            g_river_cloud.xiaozhi_uplink_runtime_truth.busy_count++;
            g_river_cloud.xiaozhi_uplink_runtime_truth.busy_streak++;
            backoff_ms = river_cloud_xiaozhi_uplink_busy_backoff_ms(
                frame_ms,
                g_river_cloud.xiaozhi_uplink_runtime_truth.busy_streak);
            g_river_cloud.xiaozhi_uplink_runtime_truth.next_send_ms = now_ms + (uint64_t)backoff_ms;
            (void)river_cloud_xiaozhi_trim_uplink_stale_frames(
                RIVER_CLOUD_XIAOZHI_UPLINK_STALE_FRAMES_MAX);
            river_cloud_xiaozhi_log_uplink_backpressure(now_ms, backoff_ms);
            break;
        }

        g_river_cloud.xiaozhi_uplink_runtime_truth.fail_count++;
        g_river_cloud.xiaozhi_uplink_runtime_truth.busy_streak = 0U;
        RIVER_LOGW("xiaozhi uplink send failed: status=%d", status);
        if (frame_ms != 0U) {
            g_river_cloud.xiaozhi_uplink_runtime_truth.next_send_ms = now_ms + (uint64_t)frame_ms;
        } else {
            g_river_cloud.xiaozhi_uplink_runtime_truth.next_send_ms = now_ms + 1U;
        }
        break;
    }

    river_cloud_xiaozhi_note_uplink_burst(drained_frames);
    river_cloud_xiaozhi_maybe_finalize_listen_stop(
        river_cloud_xiaozhi_uplink_ready_frames(),
        g_river_cloud.xiaozhi_uplink_runtime_truth.accum_bytes);
}

bool river_cloud_xiaozhi_io_has_work(void)
{
    return river_cloud_xiaozhi_control_pending() ||
           river_cloud_xiaozhi_transport_active() ||
           river_cloud_xiaozhi_uplink_active();
}

void river_cloud_xiaozhi_note_asr_result_emitted(river_cloud_asr_event_type_t type)
{
    if (type == RIVER_CLOUD_ASR_EVENT_PARTIAL) {
        g_river_cloud.xiaozhi_asr_round_truth.partial_seen = true;
        if (g_river_cloud.xiaozhi_asr_round_truth.partial_count < UINT32_MAX) {
            g_river_cloud.xiaozhi_asr_round_truth.partial_count++;
        }
    } else if (type == RIVER_CLOUD_ASR_EVENT_FINAL) {
        g_river_cloud.xiaozhi_asr_round_truth.final_seen = true;
        if (g_river_cloud.xiaozhi_asr_round_truth.final_count < UINT32_MAX) {
            g_river_cloud.xiaozhi_asr_round_truth.final_count++;
        }
    }
}

void river_cloud_xiaozhi_fill_runtime_snapshot(river_cloud_runtime_snapshot_t *snapshot)
{
    uint64_t now_ms;
    river_cloud_xiaozhi_turn_semantics_view_t semantics_view;

    if (snapshot == NULL) {
        return;
    }

    river_cloud_xiaozhi_capture_turn_semantics_view(&semantics_view);
    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    snapshot->conversation_window_active = river_cloud_xiaozhi_conversation_window_active();
    snapshot->conversation_window_remaining_ms =
        (uint32_t)river_cloud_xiaozhi_conversation_window_remaining_ms(now_ms);
    snapshot->listening = river_cloud_xiaozhi_listening_active();
    snapshot->listen_stop_pending = river_cloud_xiaozhi_listen_stop_pending();
    snapshot->local_close_pending = river_cloud_xiaozhi_local_close_pending();
    snapshot->local_close_remaining_ms =
        (uint32_t)river_cloud_xiaozhi_local_close_remaining_ms(now_ms);
    river_cloud_xiaozhi_fill_playback_runtime_snapshot(snapshot);
    snapshot->turn_accepted = semantics_view.accepted;
    snapshot->barge_in_enabled_known = semantics_view.barge_in_enabled_known;
    snapshot->barge_in_enabled = semantics_view.barge_in_enabled;
    river_cloud_xiaozhi_copy_optional_text(snapshot->session_id,
                                           sizeof(snapshot->session_id),
                                           semantics_view.session_id);
    river_cloud_xiaozhi_copy_optional_text(snapshot->turn_id,
                                           sizeof(snapshot->turn_id),
                                           semantics_view.turn_id);
    river_cloud_xiaozhi_copy_optional_text(snapshot->accept_reason,
                                           sizeof(snapshot->accept_reason),
                                           semantics_view.accept_reason);
    river_cloud_xiaozhi_copy_optional_text(snapshot->input_state,
                                           sizeof(snapshot->input_state),
                                           semantics_view.input_state);
    river_cloud_xiaozhi_copy_optional_text(snapshot->output_state,
                                           sizeof(snapshot->output_state),
                                           semantics_view.output_state);
}

void river_cloud_xiaozhi_dump_session_status(uint64_t now_ms)
{
    uint64_t endpoint_soft_close_left_ms;
    river_cloud_runtime_snapshot_t snapshot;
    river_cloud_xiaozhi_turn_semantics_view_t semantics_view;

    memset(&snapshot, 0, sizeof(snapshot));
    river_cloud_xiaozhi_fill_runtime_snapshot(&snapshot);
    river_cloud_xiaozhi_capture_turn_semantics_view(&semantics_view);
    endpoint_soft_close_left_ms = river_cloud_xiaozhi_endpoint_soft_close_remaining_ms(now_ms);

    RIVER_LOGI("xiaozhi runtime enabled=%s io=%s session=%s listening=%s playback=%s phase=%s rebuffer=%s/%s recovery_path=%s recovery_outcome=%s stop_pending=%s close_pending=%s window=%s followup_left_ms=%lu close_left_ms=%lu wake_admission=%s sid=%s pending_text=%s",
               g_river_cloud.xiaozhi_enabled ? "yes" : "no",
               g_river_cloud.xiaozhi_uplink_runtime_truth.io_started ? "running" : "off",
               river_xiaozhi_session_open() ? "open" : "closed",
               snapshot.listening ? "yes" : "no",
               snapshot.playback_active ? "yes" : "no",
               snapshot.playback_phase[0] != '\0' ? snapshot.playback_phase : "-",
               snapshot.playback_rebuffer_pending ? "yes" : "no",
               snapshot.playback_rebuffer_cause[0] != '\0' ? snapshot.playback_rebuffer_cause :
                                                             "-",
               snapshot.playback_recovery_path[0] != '\0' ?
                   snapshot.playback_recovery_path :
                   "-",
               snapshot.playback_recovery_outcome[0] != '\0' ?
                   snapshot.playback_recovery_outcome :
                   "-",
               snapshot.tts_stop_pending ? "yes" : "no",
               snapshot.local_close_pending ? "yes" : "no",
               snapshot.conversation_window_active ? "yes" : "no",
               (unsigned long)snapshot.conversation_window_remaining_ms,
               (unsigned long)snapshot.local_close_remaining_ms,
               river_cloud_xiaozhi_idle_requires_wakeword() ? "wakeword" : "legacy_vad",
               snapshot.session_id[0] != '\0' ? snapshot.session_id : "-",
               g_river_cloud.xiaozhi_pending_transcript_truth.valid ?
                   g_river_cloud.xiaozhi_pending_transcript_truth.text :
                   "-");
    RIVER_LOGI("xiaozhi preview preview_id=%s speech_started=%s text=%s stable_prefix=%s is_final=%s accept_ready=%s accept_reason=%s endpoint_candidate=%s endpoint_reason=%s source=%s audio_offset_ms=%lu",
               g_river_cloud.xiaozhi_preview_transcript_truth.preview_id[0] != '\0' ?
                   g_river_cloud.xiaozhi_preview_transcript_truth.preview_id :
                   "-",
               g_river_cloud.xiaozhi_preview_transcript_truth.speech_started ? "yes" :
                                                                                "no",
               g_river_cloud.xiaozhi_preview_transcript_truth.text[0] != '\0' ?
                   g_river_cloud.xiaozhi_preview_transcript_truth.text :
                   "-",
               g_river_cloud.xiaozhi_preview_transcript_truth.stable_prefix[0] != '\0' ?
                   g_river_cloud.xiaozhi_preview_transcript_truth.stable_prefix :
                   "-",
               g_river_cloud.xiaozhi_preview_transcript_truth.is_final ? "yes" : "no",
               g_river_cloud.xiaozhi_preview_transcript_truth.accept_ready ? "yes" : "no",
               g_river_cloud.xiaozhi_preview_transcript_truth.accept_ready_reason[0] != '\0' ?
                   g_river_cloud.xiaozhi_preview_transcript_truth.accept_ready_reason :
                   "-",
               g_river_cloud.xiaozhi_preview_transcript_truth.endpoint_candidate ? "yes" : "no",
               g_river_cloud.xiaozhi_preview_transcript_truth.endpoint_reason[0] != '\0' ?
                   g_river_cloud.xiaozhi_preview_transcript_truth.endpoint_reason :
                   "-",
               g_river_cloud.xiaozhi_preview_transcript_truth.source[0] != '\0' ?
                   g_river_cloud.xiaozhi_preview_transcript_truth.source :
                   "-",
               (unsigned long)g_river_cloud.xiaozhi_preview_transcript_truth.audio_offset_ms);
    RIVER_LOGI("xiaozhi endpoint_soft_close pending=%s reason=%s left_ms=%lu",
               river_cloud_xiaozhi_endpoint_soft_close_pending() ? "yes" : "no",
               river_cloud_xiaozhi_endpoint_soft_close_reason() != NULL ?
                   river_cloud_xiaozhi_endpoint_soft_close_reason() :
                   "-",
               (unsigned long)endpoint_soft_close_left_ms);
    RIVER_LOGI("xiaozhi turn_semantics accepted=%s accept_reason=%s turn_id=%s input_state=%s output_state=%s barge_in_enabled=%s fallback=%s",
               semantics_view.accepted ? "yes" : "no",
               semantics_view.accept_reason != NULL ? semantics_view.accept_reason : "-",
               semantics_view.turn_id != NULL ? semantics_view.turn_id : "-",
               semantics_view.input_state != NULL ? semantics_view.input_state : "-",
               semantics_view.output_state != NULL ? semantics_view.output_state : "-",
               semantics_view.barge_in_enabled_known ?
                   (semantics_view.barge_in_enabled ? "yes" : "no") :
                   "-",
               semantics_view.fallback_reason != NULL ? semantics_view.fallback_reason : "-");
}

void river_cloud_xiaozhi_dump_io_status(void)
{
    RIVER_LOGI("xiaozhi control queue=%lu/%u peak=%lu owner=%s",
               (unsigned long)g_river_cloud.xiaozhi_control_queue_truth.count,
               (unsigned int)RIVER_CLOUD_XIAOZHI_CONTROL_QUEUE_DEPTH,
               (unsigned long)g_river_cloud.xiaozhi_control_queue_truth.high_watermark,
               g_river_cloud.xiaozhi_uplink_runtime_truth.io_started ? "running" : "off");
    RIVER_LOGI("xiaozhi uplink queue=%lu/%u dropped=%lu stale_drop=%lu busy=%lu fail=%lu burst_max=%lu stop_pending=%s owner=%s",
               (unsigned long)river_cloud_xiaozhi_uplink_ready_frames(),
               (unsigned int)RIVER_CLOUD_XIAOZHI_UPLINK_RING_FRAMES,
               (unsigned long)g_river_cloud.xiaozhi_uplink_runtime_truth.ring_dropped,
               (unsigned long)g_river_cloud.xiaozhi_uplink_runtime_truth.stale_dropped,
               (unsigned long)g_river_cloud.xiaozhi_uplink_runtime_truth.busy_count,
               (unsigned long)g_river_cloud.xiaozhi_uplink_runtime_truth.fail_count,
               (unsigned long)g_river_cloud.xiaozhi_uplink_runtime_truth.burst_max,
               river_cloud_xiaozhi_listen_stop_pending() ? "yes" : "no",
               g_river_cloud.xiaozhi_uplink_runtime_truth.io_started ? "running" : "off");
    RIVER_LOGI("xiaozhi asr round id=%lu active=%s pre_roll_frames=%lu packets=%lu burst_max=%lu partial=%lu final=%lu close_reason=%s",
               (unsigned long)g_river_cloud.xiaozhi_asr_round_truth.id,
               g_river_cloud.xiaozhi_asr_round_truth.active ? "yes" : "no",
               (unsigned long)g_river_cloud.xiaozhi_asr_round_truth.pre_roll_frames,
               (unsigned long)g_river_cloud.xiaozhi_asr_round_truth.packets_sent,
               (unsigned long)g_river_cloud.xiaozhi_asr_round_truth.burst_max,
               (unsigned long)g_river_cloud.xiaozhi_asr_round_truth.partial_count,
               (unsigned long)g_river_cloud.xiaozhi_asr_round_truth.final_count,
               g_river_cloud.xiaozhi_asr_round_truth.close_reason[0] != '\0' ?
                   g_river_cloud.xiaozhi_asr_round_truth.close_reason :
                   "-");
}

void river_cloud_xiaozhi_note_stt_observation(const river_xiaozhi_event_t *event)
{
    char next_text[RIVER_CLOUD_XIAOZHI_TEXT_MAX];

    river_cloud_xiaozhi_window_touch(RIVER_CLOUD_XIAOZHI_WAKE_WINDOW_FOLLOWUP_MS, "stt");

    if (event == NULL || event->text == NULL || event->text[0] == '\0') {
        return;
    }

    river_cloud_xiaozhi_copy_optional_text(next_text, sizeof(next_text), event->text);
    if (next_text[0] == '\0') {
        return;
    }
    if (g_river_cloud.xiaozhi_pending_transcript_truth.valid &&
        strcmp(g_river_cloud.xiaozhi_pending_transcript_truth.text, next_text) == 0) {
        return;
    }

    river_cloud_xiaozhi_copy_optional_text(
        g_river_cloud.xiaozhi_pending_transcript_truth.text,
        sizeof(g_river_cloud.xiaozhi_pending_transcript_truth.text),
        next_text);
    g_river_cloud.xiaozhi_pending_transcript_truth.valid = true;
    g_river_cloud.xiaozhi_pending_transcript_truth.finalized = false;
    river_cloud_emit_asr_result(RIVER_CLOUD_ASR_EVENT_PARTIAL,
                                g_river_cloud.xiaozhi_pending_transcript_truth.text,
                                river_cloud_xiaozhi_current_sid(),
                                NULL,
                                0,
                                false);
}

void river_cloud_xiaozhi_note_preview_observation(const river_xiaozhi_event_t *event)
{
    if (event == NULL) {
        return;
    }

    if (event->preview_id != NULL && event->preview_id[0] != '\0' &&
        strcmp(g_river_cloud.xiaozhi_preview_transcript_truth.preview_id,
               event->preview_id) != 0) {
        river_cloud_xiaozhi_clear_preview_state();
    }

    river_cloud_xiaozhi_copy_optional_text(
        g_river_cloud.xiaozhi_preview_transcript_truth.preview_id,
        sizeof(g_river_cloud.xiaozhi_preview_transcript_truth.preview_id),
        event->preview_id);
    if (event->text != NULL && event->text[0] != '\0') {
        river_cloud_xiaozhi_copy_optional_text(
            g_river_cloud.xiaozhi_preview_transcript_truth.text,
            sizeof(g_river_cloud.xiaozhi_preview_transcript_truth.text),
            event->text);
    }
    if (event->stable_prefix != NULL && event->stable_prefix[0] != '\0') {
        river_cloud_xiaozhi_copy_optional_text(
            g_river_cloud.xiaozhi_preview_transcript_truth.stable_prefix,
            sizeof(g_river_cloud.xiaozhi_preview_transcript_truth.stable_prefix),
            event->stable_prefix);
    }
    if (event->source != NULL && event->source[0] != '\0') {
        river_cloud_xiaozhi_copy_optional_text(
            g_river_cloud.xiaozhi_preview_transcript_truth.source,
            sizeof(g_river_cloud.xiaozhi_preview_transcript_truth.source),
            event->source);
    }
    if (event->reason != NULL && event->reason[0] != '\0' &&
        event->type == RIVER_XIAOZHI_EVENT_INPUT_ACCEPT_READY) {
        river_cloud_xiaozhi_copy_optional_text(
            g_river_cloud.xiaozhi_preview_transcript_truth.accept_ready_reason,
            sizeof(g_river_cloud.xiaozhi_preview_transcript_truth.accept_ready_reason),
            event->reason);
    } else if (event->reason != NULL && event->reason[0] != '\0') {
        river_cloud_xiaozhi_copy_optional_text(
            g_river_cloud.xiaozhi_preview_transcript_truth.endpoint_reason,
            sizeof(g_river_cloud.xiaozhi_preview_transcript_truth.endpoint_reason),
            event->reason);
    }
    if (event->audio_offset_ms != 0U) {
        g_river_cloud.xiaozhi_preview_transcript_truth.audio_offset_ms = event->audio_offset_ms;
    }

    switch (event->type) {
    case RIVER_XIAOZHI_EVENT_INPUT_SPEECH_START:
        g_river_cloud.xiaozhi_preview_transcript_truth.speech_started = true;
        break;
    case RIVER_XIAOZHI_EVENT_INPUT_PREVIEW:
        g_river_cloud.xiaozhi_preview_transcript_truth.is_final = event->is_final;
        break;
    case RIVER_XIAOZHI_EVENT_INPUT_ACCEPT_READY:
        g_river_cloud.xiaozhi_preview_transcript_truth.accept_ready = true;
        break;
    case RIVER_XIAOZHI_EVENT_INPUT_ENDPOINT:
        g_river_cloud.xiaozhi_preview_transcript_truth.endpoint_candidate = event->candidate;
        break;
    default:
        break;
    }
}

void river_cloud_xiaozhi_note_input_observation(const river_xiaozhi_event_t *event)
{
    if (event == NULL) {
        return;
    }

    switch (event->type) {
    case RIVER_XIAOZHI_EVENT_INPUT_SPEECH_START:
        river_cloud_xiaozhi_window_touch(RIVER_CLOUD_XIAOZHI_WAKE_WINDOW_FOLLOWUP_MS,
                                         "input_speech_start");
        river_cloud_xiaozhi_note_preview_observation(event);
        river_cloud_xiaozhi_cancel_endpoint_soft_close("input_speech_start");
        river_cloud_xiaozhi_note_interrupt_hint("input_speech_start",
                                                "server_preview_speech_start");
        return;
    case RIVER_XIAOZHI_EVENT_INPUT_PREVIEW:
        river_cloud_xiaozhi_window_touch(RIVER_CLOUD_XIAOZHI_WAKE_WINDOW_FOLLOWUP_MS,
                                         "input_preview");
        river_cloud_xiaozhi_note_preview_observation(event);
        if (event->text != NULL && event->text[0] != '\0') {
            river_cloud_xiaozhi_cancel_endpoint_soft_close("input_preview");
        }
        return;
    case RIVER_XIAOZHI_EVENT_INPUT_ACCEPT_READY:
        river_cloud_xiaozhi_window_touch(RIVER_CLOUD_XIAOZHI_WAKE_WINDOW_FOLLOWUP_MS,
                                         "input_accept_ready");
        river_cloud_xiaozhi_note_preview_observation(event);
        return;
    case RIVER_XIAOZHI_EVENT_INPUT_ENDPOINT:
        river_cloud_xiaozhi_window_touch(RIVER_CLOUD_XIAOZHI_WAKE_WINDOW_FOLLOWUP_MS,
                                         "input_endpoint");
        river_cloud_xiaozhi_note_preview_observation(event);
        if (event->candidate) {
            river_cloud_xiaozhi_arm_endpoint_soft_close("input_endpoint",
                                                        event->reason != NULL &&
                                                                event->reason[0] != '\0' ?
                                                            event->reason :
                                                            "server_endpoint_candidate");
        } else {
            river_cloud_xiaozhi_cancel_endpoint_soft_close("input_endpoint_clear");
        }
        return;
    default:
        return;
    }
}

void river_cloud_xiaozhi_note_llm_observation(const river_xiaozhi_event_t *event)
{
    if (event == NULL) {
        return;
    }

    river_cloud_xiaozhi_window_touch(RIVER_CLOUD_XIAOZHI_WAKE_WINDOW_FOLLOWUP_MS, "llm");
    if (event->emotion != NULL || event->text != NULL) {
        RIVER_LOGI("xiaozhi llm emotion=%s text=%s",
                   event->emotion != NULL ? event->emotion : "-",
                   event->text != NULL ? event->text : "-");
    }
    river_cloud_xiaozhi_apply_llm_round_policy();
}

void river_cloud_xiaozhi_note_tts_observation(const river_xiaozhi_event_t *event)
{
    if (event == NULL || event->state == NULL) {
        return;
    }

    if (strcmp(event->state, "start") == 0) {
        river_cloud_xiaozhi_window_touch(RIVER_CLOUD_XIAOZHI_WAKE_WINDOW_FOLLOWUP_MS,
                                         "tts_start");
        river_cloud_xiaozhi_finalize_pending_text("tts_start");
        river_cloud_xiaozhi_apply_tts_start_round_policy();
        river_cloud_xiaozhi_cancel_playback_stop();
        return;
    }

    if (strcmp(event->state, "sentence_start") == 0) {
        river_cloud_xiaozhi_window_touch(RIVER_CLOUD_XIAOZHI_WAKE_WINDOW_FOLLOWUP_MS,
                                         "tts_sentence");
        if (event->text != NULL && event->text[0] != '\0') {
            snprintf(g_river_cloud.last_text,
                     sizeof(g_river_cloud.last_text),
                     "%s",
                     event->text);
        }
        RIVER_LOGI("xiaozhi tts sentence_start: %s", event->text != NULL ? event->text : "-");
        return;
    }

    if (strcmp(event->state, "stop") == 0) {
        river_cloud_xiaozhi_apply_tts_stop_round_policy();
    }
}

void river_cloud_xiaozhi_note_session_closed_observation(const river_xiaozhi_event_t *event)
{
    const char *sid = "-";

    if (event != NULL && event->session_id != NULL && event->session_id[0] != '\0') {
        sid = event->session_id;
    } else if (river_cloud_xiaozhi_current_sid() != NULL) {
        sid = river_cloud_xiaozhi_current_sid();
    }

    RIVER_LOGW("xiaozhi transport closed: sid=%s window=%s stream=%s playback=%s",
               sid,
               river_cloud_xiaozhi_conversation_window_active() ? "yes" : "no",
               g_river_cloud.stream_active ? "yes" : "no",
               river_cloud_xiaozhi_playback_output_active() ? "yes" : "no");
    river_cloud_xiaozhi_apply_transport_closed_terminal_policy();
}

void river_cloud_xiaozhi_note_error_observation(const river_xiaozhi_event_t *event)
{
    const char *message = NULL;

    if (event != NULL) {
        if (event->reason != NULL && event->reason[0] != '\0') {
            message = event->reason;
        } else if (event->state != NULL && event->state[0] != '\0') {
            message = event->state;
        } else if (event->text != NULL && event->text[0] != '\0') {
            message = event->text;
        }
    }

    if (message == NULL || message[0] == '\0') {
        message = river_xiaozhi_last_error();
    }
    if (message == NULL || message[0] == '\0') {
        message = "xiaozhi_transport_error";
    }

    river_cloud_emit_asr_result(RIVER_CLOUD_ASR_EVENT_ERROR,
                                NULL,
                                river_cloud_xiaozhi_current_sid(),
                                message,
                                -1,
                                true);
    river_cloud_request_state_sync("asr_error");
}

void river_cloud_xiaozhi_apply_tts_stop_round_policy(void)
{
    river_cloud_xiaozhi_finalize_pending_text("tts_stop");
    river_cloud_xiaozhi_close_local_round_for_cause(
        RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_LOCAL_RESOLVED,
        "tts_stop");
    river_cloud_xiaozhi_window_touch(
        RIVER_CLOUD_XIAOZHI_POST_TTS_SILENCE_CLOSE_MS,
        "tts_stop");
    river_cloud_xiaozhi_arm_playback_stop(RIVER_CLOUD_XIAOZHI_PLAYBACK_DRAIN_MS);
}

void river_cloud_xiaozhi_apply_reopen_overlap_round_policy(void)
{
    river_cloud_xiaozhi_note_round_finish_request("reopen_overlap");
    river_cloud_xiaozhi_close_local_round_for_cause(
        RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_LOCAL_RESOLVED,
        "reopen_overlap");
}

void river_cloud_xiaozhi_apply_llm_round_policy(void)
{
    river_cloud_xiaozhi_finalize_pending_text("llm");
    river_cloud_xiaozhi_close_local_round_for_cause(
        RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_LOCAL_RESOLVED,
        "llm");
}

void river_cloud_xiaozhi_apply_post_stop_result_round_policy(void)
{
    if (g_river_cloud.xiaozhi_pending_transcript_truth.finalized ||
        g_river_cloud.xiaozhi_asr_round_truth.final_seen) {
        river_cloud_xiaozhi_close_local_round_for_cause(
            RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_LOCAL_RESOLVED,
            "post_stop_result");
    }
}

void river_cloud_xiaozhi_apply_transport_closed_terminal_policy(void)
{
    river_cloud_xiaozhi_finalize_pending_text("transport_closed");
    river_cloud_xiaozhi_round_finish("transport_closed");
    river_cloud_xiaozhi_window_abort_local("transport_closed");
    river_cloud_xiaozhi_apply_terminal_playback_policy(
        RIVER_CLOUD_XIAOZHI_PLAYBACK_ABORT_TRANSPORT_CLOSED);
    river_cloud_xiaozhi_reset_transport_state(true);
    river_cloud_xiaozhi_check_window_timeout();
}

void river_cloud_xiaozhi_apply_network_lost_terminal_policy(void)
{
    river_cloud_xiaozhi_apply_terminal_playback_policy(
        RIVER_CLOUD_XIAOZHI_PLAYBACK_ABORT_NETWORK_LOST);
    river_cloud_xiaozhi_round_finish("network_lost");
    river_cloud_xiaozhi_reset_transport_state(false);
    (void)river_cloud_xiaozhi_request_close_session();
}

void river_cloud_xiaozhi_apply_bridge_close_capture_policy(void)
{
    if (g_river_cloud.stream_active) {
        river_cloud_xiaozhi_complete_active_stream_finish(
            RIVER_CLOUD_XIAOZHI_STREAM_FINISH_POST_ROLL,
            "bridge_close");
    }

    river_cloud_xiaozhi_apply_bridge_close_terminal_policy();
}

void river_cloud_xiaozhi_apply_bridge_close_terminal_policy(void)
{
    river_cloud_xiaozhi_apply_terminal_playback_policy(
        RIVER_CLOUD_XIAOZHI_PLAYBACK_ABORT_BRIDGE_CLOSE);
    river_cloud_xiaozhi_round_finish("bridge_close");
    river_cloud_xiaozhi_reset_transport_state(false);
    (void)river_cloud_xiaozhi_request_close_session();
}

static river_status_t river_cloud_xiaozhi_apply_inactive_stream_capture_policy(
    const uint8_t *pcm,
    size_t bytes,
    bool is_speech,
    bool *activated)
{
    river_status_t status;
    bool followup_opened = false;
    uint32_t pre_roll_frames_before_open;
    uint32_t read_index;
    uint32_t frame_index;

    if (activated != NULL) {
        *activated = false;
    }

    river_cloud_pre_roll_store(pcm);
    pre_roll_frames_before_open = g_river_cloud.pre_roll_count_frames;
    status = river_cloud_xiaozhi_maybe_start_followup_round(is_speech,
                                                            pre_roll_frames_before_open,
                                                            &followup_opened);
    if (status != RIVER_OK) {
        g_river_cloud.stream_open_fail++;
        river_cloud_log_stream_open_deferred_once(status);
        return status;
    }
    if (!followup_opened) {
        return RIVER_OK;
    }

    if (pre_roll_frames_before_open == g_river_cloud.pre_roll_capacity_frames) {
        read_index = g_river_cloud.pre_roll_write_index_frames;
    } else {
        read_index = 0U;
    }

    for (frame_index = 0U; frame_index < pre_roll_frames_before_open; ++frame_index) {
        const uint8_t *src = g_river_cloud.pre_roll_buffer +
                             ((size_t)read_index * g_river_cloud.frame_bytes);

        status = river_cloud_xiaozhi_push_pcm(src, g_river_cloud.frame_bytes);
        if (status != RIVER_OK) {
            g_river_cloud.stream_feed_fail++;
            return status;
        }
        g_river_cloud.stream_feed_ok++;

        read_index++;
        if (read_index >= g_river_cloud.pre_roll_capacity_frames) {
            read_index = 0U;
        }
    }
    river_cloud_pre_roll_reset();

    if (pre_roll_frames_before_open == 0U) {
        status = river_cloud_xiaozhi_push_pcm(pcm, bytes);
        if (status != RIVER_OK) {
            g_river_cloud.stream_feed_fail++;
            return status;
        }
        g_river_cloud.stream_feed_ok++;
    }

    river_cloud_reset_stream_open_deferred_state();
    g_river_cloud.stream_active = true;
    g_river_cloud.stream_open_ok++;
    g_river_cloud.silence_frames = 0U;
    g_river_cloud.stream_started_ms = rtos_time_get_current_system_time_ms();
    RIVER_LOGI("asr stream active: provider=%s pre_roll_frames=%lu",
               river_cloud_asr_provider_name(),
               (unsigned long)pre_roll_frames_before_open);
    river_runtime_stats_snapshot("asr_stream_active");

    if (activated != NULL) {
        *activated = true;
    }
    return RIVER_OK;
}

river_status_t river_cloud_xiaozhi_apply_stream_push_capture_policy(
    const uint8_t *pcm,
    size_t bytes,
    bool is_speech,
    bool *capture_exit_needed)
{
    river_status_t status;

    if (capture_exit_needed != NULL) {
        *capture_exit_needed = false;
    }

    /*
     * Follow-up window timeout can cascade into websocket/session teardown.
     * Keep that off the real-time capture path; the dedicated xiaozhi I/O
     * owner already polls timeout state continuously and owns transport work.
     */
    river_cloud_log_time_ready_once();

    if (!g_river_cloud.stream_active) {
        return river_cloud_xiaozhi_apply_inactive_stream_capture_policy(pcm,
                                                                        bytes,
                                                                        is_speech,
                                                                        capture_exit_needed);
    }

    status = river_cloud_xiaozhi_push_pcm(pcm, bytes);
    if (status != RIVER_OK) {
        g_river_cloud.stream_feed_fail++;
        return status;
    }
    g_river_cloud.stream_feed_ok++;

    river_cloud_xiaozhi_apply_active_stream_capture_policy(is_speech);
    if (capture_exit_needed != NULL) {
        *capture_exit_needed = true;
    }
    return RIVER_OK;
}

river_status_t river_cloud_xiaozhi_apply_capture_stream_policy(const uint8_t *pcm,
                                                               size_t bytes,
                                                               bool is_speech)
{
    river_status_t status;
    bool capture_exit_needed = false;

    if (river_cloud_xiaozhi_apply_capture_entry_playback_policy()) {
        return RIVER_OK;
    }

    status = river_cloud_xiaozhi_apply_stream_push_capture_policy(pcm,
                                                                  bytes,
                                                                  is_speech,
                                                                  &capture_exit_needed);
    if (status != RIVER_OK) {
        return status;
    }
    if (capture_exit_needed) {
        river_cloud_xiaozhi_apply_capture_exit_playback_policy();
    }
    return RIVER_OK;
}

void river_cloud_xiaozhi_note_round_finish_request(const char *reason)
{
    if (!g_river_cloud.xiaozhi_asr_round_truth.active ||
        reason == NULL ||
        reason[0] == '\0') {
        return;
    }

    snprintf(g_river_cloud.xiaozhi_asr_round_truth.close_reason,
             sizeof(g_river_cloud.xiaozhi_asr_round_truth.close_reason),
             "%s",
             reason);
}

river_status_t river_cloud_xiaozhi_interrupt_tts(const char *reason)
{
    river_status_t status = RIVER_ERR_UNSUPPORTED;

    if (!(river_cloud_xiaozhi_playback_turn_active() ||
          river_cloud_xiaozhi_listening_active() ||
          river_xiaozhi_session_open())) {
        return RIVER_ERR_UNSUPPORTED;
    }

    status = RIVER_OK;
    if (river_cloud_xiaozhi_playback_turn_active()) {
        status = river_cloud_xiaozhi_playback_abort_for_cause(
            RIVER_CLOUD_XIAOZHI_PLAYBACK_ABORT_INTERRUPT,
            reason);
    }
    if (river_xiaozhi_session_open()) {
        (void)river_cloud_xiaozhi_request_abort(reason);
    }
    RIVER_LOGI("tts interrupt requested: reason=%s", reason != NULL ? reason : "-");
    return status;
}

void river_cloud_xiaozhi_round_begin(uint32_t pre_roll_frames)
{
    g_river_cloud.xiaozhi_asr_round_truth.id++;
    g_river_cloud.xiaozhi_asr_round_truth.active = true;
    g_river_cloud.xiaozhi_asr_round_truth.started_ms =
        (uint32_t)rtos_time_get_current_system_time_ms();
    g_river_cloud.xiaozhi_asr_round_truth.first_packet_ms = 0U;
    g_river_cloud.xiaozhi_asr_round_truth.pre_roll_frames = pre_roll_frames;
    g_river_cloud.xiaozhi_asr_round_truth.packets_sent = 0U;
    g_river_cloud.xiaozhi_asr_round_truth.partial_count = 0U;
    g_river_cloud.xiaozhi_asr_round_truth.final_count = 0U;
    g_river_cloud.xiaozhi_asr_round_truth.partial_seen = false;
    g_river_cloud.xiaozhi_asr_round_truth.final_seen = false;
    g_river_cloud.xiaozhi_asr_round_truth.busy_base =
        g_river_cloud.xiaozhi_uplink_runtime_truth.busy_count;
    g_river_cloud.xiaozhi_asr_round_truth.fail_base =
        g_river_cloud.xiaozhi_uplink_runtime_truth.fail_count;
    g_river_cloud.xiaozhi_asr_round_truth.stale_drop_base =
        g_river_cloud.xiaozhi_uplink_runtime_truth.stale_dropped;
    g_river_cloud.xiaozhi_asr_round_truth.ring_drop_base =
        g_river_cloud.xiaozhi_uplink_runtime_truth.ring_dropped;
    g_river_cloud.xiaozhi_asr_round_truth.burst_max = 0U;
    g_river_cloud.xiaozhi_asr_round_truth.preview_warmup_done_ms = 0U;
    g_river_cloud.xiaozhi_asr_round_truth.preview_warmup_bypass_count = 0U;
    memset(g_river_cloud.xiaozhi_asr_round_truth.uplink_send_interval_samples,
           0,
           sizeof(g_river_cloud.xiaozhi_asr_round_truth.uplink_send_interval_samples));
    memset(g_river_cloud.xiaozhi_asr_round_truth.uplink_capture_age_samples,
           0,
           sizeof(g_river_cloud.xiaozhi_asr_round_truth.uplink_capture_age_samples));
    memset(g_river_cloud.xiaozhi_asr_round_truth.uplink_backlog_samples,
           0,
           sizeof(g_river_cloud.xiaozhi_asr_round_truth.uplink_backlog_samples));
    memset(g_river_cloud.xiaozhi_asr_round_truth.uplink_send_duration_samples,
           0,
           sizeof(g_river_cloud.xiaozhi_asr_round_truth.uplink_send_duration_samples));
    g_river_cloud.xiaozhi_asr_round_truth.uplink_metric_count = 0U;
    g_river_cloud.xiaozhi_asr_round_truth.uplink_metric_cursor = 0U;
    g_river_cloud.xiaozhi_asr_round_truth.last_packet_sent_ms = 0U;
    g_river_cloud.xiaozhi_asr_round_truth.close_reason[0] = '\0';
    RIVER_LOGI("xiaozhi asr round begin: id=%lu sid=%s pre_roll_frames=%lu",
               (unsigned long)g_river_cloud.xiaozhi_asr_round_truth.id,
               river_cloud_xiaozhi_current_sid() != NULL ?
                   river_cloud_xiaozhi_current_sid() :
                   "-",
               (unsigned long)pre_roll_frames);
}

static uint32_t river_cloud_xiaozhi_round_percentile(
    const uint32_t *samples,
    uint32_t sample_count,
    uint32_t percentile)
{
    uint32_t sorted[RIVER_CLOUD_XIAOZHI_UPLINK_METRIC_SAMPLES];
    uint32_t count;
    uint32_t i;
    uint32_t j;
    uint32_t target;

    if (samples == NULL || sample_count == 0U) {
        return 0U;
    }

    count = sample_count;
    if (count > RIVER_CLOUD_XIAOZHI_UPLINK_METRIC_SAMPLES) {
        count = RIVER_CLOUD_XIAOZHI_UPLINK_METRIC_SAMPLES;
    }
    memcpy(sorted, samples, count * sizeof(uint32_t));
    for (i = 1U; i < count; ++i) {
        uint32_t key = sorted[i];
        j = i;
        while (j > 0U && sorted[j - 1U] > key) {
            sorted[j] = sorted[j - 1U];
            j--;
        }
        sorted[j] = key;
    }
    if (percentile >= 100U) {
        return sorted[count - 1U];
    }
    target = (uint32_t)(((uint64_t)(count - 1U) * (uint64_t)percentile + 50ULL) / 100ULL);
    if (target >= count) {
        target = count - 1U;
    }
    return sorted[target];
}

static void river_cloud_xiaozhi_round_note_uplink_metrics(uint64_t now_ms,
                                                          uint32_t backlog_frames,
                                                          uint32_t capture_age_ms,
                                                          uint32_t send_duration_ms)
{
    uint32_t send_interval_ms = 0U;
    uint32_t slot;
    river_cloud_xiaozhi_asr_round_truth_t *round_truth =
        &g_river_cloud.xiaozhi_asr_round_truth;

    if (!round_truth->active) {
        return;
    }
    if (round_truth->last_packet_sent_ms != 0U && now_ms >= round_truth->last_packet_sent_ms) {
        uint64_t delta_ms = now_ms - round_truth->last_packet_sent_ms;
        send_interval_ms = delta_ms > UINT32_MAX ? UINT32_MAX : (uint32_t)delta_ms;
    }
    round_truth->last_packet_sent_ms = now_ms;
    slot = round_truth->uplink_metric_cursor % RIVER_CLOUD_XIAOZHI_UPLINK_METRIC_SAMPLES;
    round_truth->uplink_send_interval_samples[slot] = send_interval_ms;
    round_truth->uplink_capture_age_samples[slot] = capture_age_ms;
    round_truth->uplink_backlog_samples[slot] = backlog_frames;
    round_truth->uplink_send_duration_samples[slot] = send_duration_ms;
    if (round_truth->uplink_metric_count < RIVER_CLOUD_XIAOZHI_UPLINK_METRIC_SAMPLES) {
        round_truth->uplink_metric_count++;
    }
    round_truth->uplink_metric_cursor++;
}

void river_cloud_xiaozhi_round_note_packet_sent(uint64_t now_ms,
                                                uint32_t backlog_frames,
                                                uint32_t capture_age_ms,
                                                uint32_t send_duration_ms)
{
    uint32_t audio_ms;

    if (!g_river_cloud.xiaozhi_asr_round_truth.active) {
        return;
    }

    if (g_river_cloud.xiaozhi_asr_round_truth.first_packet_ms == 0U) {
        g_river_cloud.xiaozhi_asr_round_truth.first_packet_ms =
            (uint32_t)now_ms;
    }
    if (g_river_cloud.xiaozhi_asr_round_truth.packets_sent < UINT32_MAX) {
        g_river_cloud.xiaozhi_asr_round_truth.packets_sent++;
    }
    if (g_river_cloud.xiaozhi_asr_round_truth.preview_warmup_done_ms == 0U &&
        RIVER_CLOUD_XIAOZHI_UPLINK_PREVIEW_WARMUP_MS != 0U &&
        g_river_cloud.xiaozhi_asr_round_truth.packets_sent <=
            (UINT32_MAX / RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS)) {
        audio_ms = g_river_cloud.xiaozhi_asr_round_truth.packets_sent *
                   RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS;
        if (audio_ms >= RIVER_CLOUD_XIAOZHI_UPLINK_PREVIEW_WARMUP_MS &&
            now_ms >= g_river_cloud.xiaozhi_asr_round_truth.started_ms) {
            uint64_t done_ms = now_ms - g_river_cloud.xiaozhi_asr_round_truth.started_ms;

            g_river_cloud.xiaozhi_asr_round_truth.preview_warmup_done_ms =
                done_ms > UINT32_MAX ? UINT32_MAX : (uint32_t)done_ms;
        }
    }
    if (backlog_frames > g_river_cloud.xiaozhi_asr_round_truth.burst_max) {
        g_river_cloud.xiaozhi_asr_round_truth.burst_max = backlog_frames;
    }
    river_cloud_xiaozhi_round_note_uplink_metrics(now_ms,
                                                  backlog_frames,
                                                  capture_age_ms,
                                                  send_duration_ms);
}

static const char *river_cloud_xiaozhi_stream_finish_cause_name(
    river_cloud_xiaozhi_stream_finish_cause_t cause)
{
    switch (cause) {
    case RIVER_CLOUD_XIAOZHI_STREAM_FINISH_POST_ROLL:
        return "post_roll";
    case RIVER_CLOUD_XIAOZHI_STREAM_FINISH_ENDPOINT_TIMEOUT:
        return "endpoint_soft_close_timeout";
    default:
        return "unknown";
    }
}

static bool river_cloud_xiaozhi_server_endpoint_candidate_pending(void)
{
    return river_xiaozhi_discovery_server_endpoint_enabled() &&
           river_xiaozhi_preview_events_negotiated() &&
           g_river_cloud.xiaozhi_preview_transcript_truth.endpoint_candidate &&
           g_river_cloud.xiaozhi_preview_transcript_truth.preview_id[0] != '\0';
}

void river_cloud_xiaozhi_apply_active_stream_capture_policy(bool is_speech)
{
    if (!g_river_cloud.stream_active) {
        return;
    }

    if (is_speech) {
        river_cloud_xiaozhi_cancel_endpoint_soft_close("speech_resumed");
        g_river_cloud.silence_frames = 0U;
        return;
    }

    g_river_cloud.silence_frames++;
    if (g_river_cloud.silence_frames < g_river_cloud.post_roll_frames ||
        (rtos_time_get_current_system_time_ms() - g_river_cloud.stream_started_ms) <
            RIVER_CLOUD_STREAM_MIN_ACTIVE_MS) {
        return;
    }

    if (river_cloud_xiaozhi_duplex_soft_endpoint_enabled()) {
        river_cloud_xiaozhi_arm_endpoint_soft_close("post_roll", "local_silence");
    } else {
        river_cloud_xiaozhi_complete_active_stream_finish(
            RIVER_CLOUD_XIAOZHI_STREAM_FINISH_POST_ROLL,
            "local_silence");
    }
}

void river_cloud_xiaozhi_commit_active_stream_finish_for_cause(
    river_cloud_xiaozhi_stream_finish_cause_t cause,
    const char *detail_reason)
{
    const char *cause_name = river_cloud_xiaozhi_stream_finish_cause_name(cause);

    if (!g_river_cloud.stream_active) {
        return;
    }

    RIVER_LOGI("xiaozhi active stream finish: cause=%s trigger=%s partial=%s final=%s",
               cause_name,
               detail_reason != NULL && detail_reason[0] != '\0' ? detail_reason : cause_name,
               g_river_cloud.xiaozhi_asr_round_truth.partial_seen ? "yes" : "no",
               g_river_cloud.xiaozhi_asr_round_truth.final_seen ? "yes" : "no");
    river_cloud_xiaozhi_clear_endpoint_soft_close_state();
    river_cloud_xiaozhi_note_round_finish_request("post_roll");
    river_cloud_xiaozhi_finalize_pending_text("post_roll");
    river_cloud_xiaozhi_prepare_post_commit_wait();
    g_river_cloud.stream_active = false;
    g_river_cloud.silence_frames = 0U;
    g_river_cloud.stream_started_ms = 0U;
    g_river_cloud.xiaozhi_uplink_runtime_truth.open_speech_frames = 0U;
    g_river_cloud.xiaozhi_uplink_runtime_truth.accum_bytes = 0U;
    g_river_cloud.xiaozhi_uplink_runtime_truth.retry_valid = false;
    g_river_cloud.xiaozhi_uplink_runtime_truth.next_send_ms = 0U;
    g_river_cloud.xiaozhi_uplink_runtime_truth.busy_streak = 0U;
    river_cloud_pre_roll_reset();
}

static river_status_t river_cloud_xiaozhi_flush_accumulator_padded(void)
{
    size_t frame_bytes;
    river_status_t status;

    if (!g_river_cloud.xiaozhi_uplink_ring.initialized ||
        g_river_cloud.xiaozhi_uplink_runtime_truth.accum_bytes == 0U) {
        return RIVER_OK;
    }

    frame_bytes = RIVER_CLOUD_XIAOZHI_UPLINK_PCM_FRAME_MAX;
    if (g_river_cloud.xiaozhi_uplink_runtime_truth.accum_bytes < frame_bytes) {
        memset(g_river_cloud.xiaozhi_uplink_accum + g_river_cloud.xiaozhi_uplink_runtime_truth.accum_bytes,
               0,
               frame_bytes - g_river_cloud.xiaozhi_uplink_runtime_truth.accum_bytes);
    }
    status = river_cloud_xiaozhi_queue_uplink_packet(g_river_cloud.xiaozhi_uplink_accum,
                                                     frame_bytes);
    g_river_cloud.xiaozhi_uplink_runtime_truth.accum_bytes = 0U;
    return status;
}

void river_cloud_xiaozhi_complete_active_stream_finish(
    river_cloud_xiaozhi_stream_finish_cause_t cause,
    const char *detail_reason)
{
    const char *cause_name = river_cloud_xiaozhi_stream_finish_cause_name(cause);
    const char *reason =
        detail_reason != NULL && detail_reason[0] != '\0' ? detail_reason : cause_name;

    if (!g_river_cloud.stream_active) {
        return;
    }

    if (river_cloud_xiaozhi_server_endpoint_candidate_pending()) {
        RIVER_LOGI("xiaozhi server endpoint candidate suppresses local audio.in.commit: cause=%s trigger=%s preview_id=%s endpoint_reason=%s",
                   cause_name,
                   reason,
                   g_river_cloud.xiaozhi_preview_transcript_truth.preview_id,
                   g_river_cloud.xiaozhi_preview_transcript_truth.endpoint_reason[0] != '\0' ?
                       g_river_cloud.xiaozhi_preview_transcript_truth.endpoint_reason :
                       "-");
        river_cloud_xiaozhi_note_round_finish_request("server_endpoint_candidate");
        river_cloud_xiaozhi_close_local_round_for_cause(
            RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_SERVER_ENDPOINT,
            "server_endpoint_candidate");
        return;
    }

    (void)river_cloud_xiaozhi_flush_accumulator_padded();
    river_cloud_xiaozhi_commit_active_stream_finish_for_cause(cause, detail_reason);
    river_cloud_xiaozhi_maybe_finalize_listen_stop(
        river_cloud_xiaozhi_uplink_ready_frames(),
        g_river_cloud.xiaozhi_uplink_runtime_truth.accum_bytes);
}

static void river_cloud_xiaozhi_copy_semantic_text(char *dst,
                                                   size_t dst_size,
                                                   const char *src)
{
    if (dst == NULL || dst_size == 0U) {
        return;
    }
    if (src == NULL || src[0] == '\0') {
        dst[0] = '\0';
        return;
    }

    snprintf(dst, dst_size, "%s", src);
}

static bool river_cloud_xiaozhi_update_semantic_text(char *dst,
                                                     size_t dst_size,
                                                     const char *src)
{
    char next_value[96];

    if (dst == NULL || dst_size == 0U) {
        return false;
    }

    if (src == NULL || src[0] == '\0') {
        if (dst[0] == '\0') {
            return false;
        }
        dst[0] = '\0';
        return true;
    }

    memset(next_value, 0, sizeof(next_value));
    snprintf(next_value, sizeof(next_value), "%s", src);
    if (strcmp(dst, next_value) == 0) {
        return false;
    }

    snprintf(dst, dst_size, "%s", next_value);
    return true;
}

static const char *river_cloud_xiaozhi_optional_bool_text(bool known, bool enabled)
{
    if (!known) {
        return "-";
    }
    return enabled ? "yes" : "no";
}

void river_cloud_xiaozhi_clear_pending_text(void)
{
    g_river_cloud.xiaozhi_pending_transcript_truth.valid = false;
    g_river_cloud.xiaozhi_pending_transcript_truth.finalized = false;
    g_river_cloud.xiaozhi_pending_transcript_truth.text[0] = '\0';
}

void river_cloud_xiaozhi_clear_preview_state(void)
{
    g_river_cloud.xiaozhi_preview_transcript_truth.speech_started = false;
    g_river_cloud.xiaozhi_preview_transcript_truth.accept_ready = false;
    g_river_cloud.xiaozhi_preview_transcript_truth.endpoint_candidate = false;
    g_river_cloud.xiaozhi_preview_transcript_truth.is_final = false;
    g_river_cloud.xiaozhi_preview_transcript_truth.audio_offset_ms = 0U;
    g_river_cloud.xiaozhi_preview_transcript_truth.preview_id[0] = '\0';
    g_river_cloud.xiaozhi_preview_transcript_truth.text[0] = '\0';
    g_river_cloud.xiaozhi_preview_transcript_truth.stable_prefix[0] = '\0';
    g_river_cloud.xiaozhi_preview_transcript_truth.source[0] = '\0';
    g_river_cloud.xiaozhi_preview_transcript_truth.accept_ready_reason[0] = '\0';
    g_river_cloud.xiaozhi_preview_transcript_truth.endpoint_reason[0] = '\0';
}

void river_cloud_xiaozhi_clear_turn_semantics_state(void)
{
    g_river_cloud.xiaozhi_turn_semantics.accepted = false;
    g_river_cloud.xiaozhi_turn_semantics.barge_in_enabled_known = false;
    g_river_cloud.xiaozhi_turn_semantics.barge_in_enabled = false;
    g_river_cloud.xiaozhi_turn_semantics.turn_id[0] = '\0';
    g_river_cloud.xiaozhi_turn_semantics.accept_reason[0] = '\0';
    g_river_cloud.xiaozhi_turn_semantics.input_state[0] = '\0';
    g_river_cloud.xiaozhi_turn_semantics.output_state[0] = '\0';
    g_river_cloud.xiaozhi_turn_semantics.fallback_reason[0] = '\0';
}

void river_cloud_xiaozhi_clear_session_id(void)
{
    g_river_cloud.xiaozhi_turn_semantics.session_id[0] = '\0';
}

void river_cloud_xiaozhi_capture_turn_semantics_view(
    river_cloud_xiaozhi_turn_semantics_view_t *view)
{
    river_cloud_xiaozhi_turn_semantics_state_t *state = &g_river_cloud.xiaozhi_turn_semantics;

    if (view == NULL) {
        return;
    }

    memset(view, 0, sizeof(*view));
    view->accepted = state->accepted && state->accept_reason[0] != '\0';
    view->barge_in_enabled_known = state->barge_in_enabled_known;
    view->barge_in_enabled = state->barge_in_enabled;
    view->session_id = state->session_id[0] != '\0' ? state->session_id : NULL;
    view->turn_id = state->turn_id[0] != '\0' ? state->turn_id : NULL;
    view->accept_reason = state->accept_reason[0] != '\0' ? state->accept_reason : NULL;
    view->input_state = state->input_state[0] != '\0' ? state->input_state : NULL;
    view->output_state = state->output_state[0] != '\0' ? state->output_state : NULL;
    view->fallback_reason = state->fallback_reason[0] != '\0' ? state->fallback_reason : NULL;
}

void river_cloud_xiaozhi_copy_session_id_from_transport(void)
{
    const char *sid;

    sid = river_xiaozhi_session_id();
    if (sid != NULL && sid[0] != '\0') {
        snprintf(g_river_cloud.xiaozhi_turn_semantics.session_id,
                 sizeof(g_river_cloud.xiaozhi_turn_semantics.session_id),
                 "%s",
                 sid);
    }
}

const char *river_cloud_xiaozhi_current_sid(void)
{
    return g_river_cloud.xiaozhi_turn_semantics.session_id[0] != '\0' ?
               g_river_cloud.xiaozhi_turn_semantics.session_id :
               river_xiaozhi_session_id();
}

static bool river_cloud_xiaozhi_input_state_server_committed(const char *input_state)
{
    return input_state != NULL && strcmp(input_state, "committed") == 0;
}

static void river_cloud_xiaozhi_clear_response_audio_wait_if_returned_active(
    const river_cloud_xiaozhi_turn_semantics_state_t *state,
    const char *trigger)
{
    const char *session_state = river_xiaozhi_last_session_state();

    if (session_state == NULL || strcmp(session_state, "active") != 0) {
        return;
    }
    if (state == NULL || strcmp(state->output_state, "idle") != 0) {
        return;
    }

    if (river_cloud_xiaozhi_note_response_audio_abandoned("server_returned_active_no_audio")) {
        RIVER_LOGI("xiaozhi response audio wait cleared: trigger=%s session_state=%s output_state=%s",
                   trigger != NULL ? trigger : "-",
                   session_state,
                   (state != NULL && state->output_state[0] != '\0') ? state->output_state : "-");
    }
}

static uint32_t river_cloud_xiaozhi_prompt_tone_step(uint32_t frequency_hz,
                                                     uint32_t sample_rate_hz)
{
    if (frequency_hz == 0U || sample_rate_hz == 0U) {
        return 0U;
    }
    return (uint32_t)(((uint64_t)frequency_hz << 32) / (uint64_t)sample_rate_hz);
}

static void river_cloud_xiaozhi_fill_prompt_tone(int16_t *mono,
                                                 size_t samples,
                                                 uint32_t *phase,
                                                 uint32_t phase_step)
{
    size_t index;
    uint32_t current_phase;
    const int16_t amplitude = 3800;

    if (mono == NULL || phase == NULL || samples == 0U) {
        return;
    }

    current_phase = *phase;
    for (index = 0U; index < samples; ++index) {
        mono[index] = ((current_phase & 0x80000000UL) != 0U) ? amplitude : (int16_t)(-amplitude);
        current_phase += phase_step;
    }
    *phase = current_phase;
}

bool river_cloud_xiaozhi_play_local_retry_prompt(const char *reason)
{
    enum {
        RIVER_XIAOZHI_LOCAL_PROMPT_SAMPLE_RATE = 16000U,
        RIVER_XIAOZHI_LOCAL_PROMPT_FRAME_MS = 20U,
        RIVER_XIAOZHI_LOCAL_PROMPT_FRAMES = 4U,
        RIVER_XIAOZHI_LOCAL_PROMPT_COOLDOWN_MS = 15000U
    };
    static uint64_t last_prompt_ms = 0U;
    river_playback_stream_config_t config;
    uint64_t now_ms;
    uint32_t frame_samples;
    uint32_t frame_bytes;
    uint32_t mono_step_a;
    uint32_t mono_step_b;
    uint32_t tone_phase = 0U;
    uint32_t frame_index;
    int16_t mono_frame[(RIVER_XIAOZHI_LOCAL_PROMPT_SAMPLE_RATE *
                        RIVER_XIAOZHI_LOCAL_PROMPT_FRAME_MS) /
                       1000U];
    int16_t stereo_frame[(RIVER_XIAOZHI_LOCAL_PROMPT_SAMPLE_RATE *
                          RIVER_XIAOZHI_LOCAL_PROMPT_FRAME_MS) /
                         1000U * 2U];

    if (RIVER_CLOUD_XIAOZHI_LOCAL_RETRY_PROMPT_ENABLED == 0U) {
        RIVER_LOGW("xiaozhi local fallback prompt skipped: reason=%s disabled=yes stream=%s listening=%s playback=%s",
                   reason != NULL ? reason : "-",
                   g_river_cloud.stream_active ? "yes" : "no",
                   g_river_cloud.xiaozhi_session_window_truth.listening ? "yes" : "no",
                   river_playback_service_active() ? "yes" : "no");
        return false;
    }

    if (g_river_cloud.stream_active ||
        g_river_cloud.xiaozhi_session_window_truth.listening ||
        g_river_cloud.xiaozhi_session_window_truth.listen_stop_pending ||
        g_river_cloud.xiaozhi_session_window_truth.local_close_pending) {
        RIVER_LOGW("xiaozhi local fallback prompt skipped: reason=%s unsafe_dialog_state stream=%s listening=%s listen_stop=%s local_close=%s",
                   reason != NULL ? reason : "-",
                   g_river_cloud.stream_active ? "yes" : "no",
                   g_river_cloud.xiaozhi_session_window_truth.listening ? "yes" : "no",
                   g_river_cloud.xiaozhi_session_window_truth.listen_stop_pending ? "yes" : "no",
                   g_river_cloud.xiaozhi_session_window_truth.local_close_pending ? "yes" : "no");
        return false;
    }

    if (river_playback_service_active()) {
        RIVER_LOGW("xiaozhi local fallback prompt skipped: reason=%s playback_active=yes",
                   reason != NULL ? reason : "-");
        return false;
    }

    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    if (last_prompt_ms != 0U && now_ms >= last_prompt_ms &&
        (now_ms - last_prompt_ms) < RIVER_XIAOZHI_LOCAL_PROMPT_COOLDOWN_MS) {
        return false;
    }

    frame_samples =
        (RIVER_XIAOZHI_LOCAL_PROMPT_SAMPLE_RATE * RIVER_XIAOZHI_LOCAL_PROMPT_FRAME_MS) /
        1000U;
    frame_bytes = frame_samples * sizeof(int16_t) * 2U;
    memset(&config, 0, sizeof(config));
    config.stream_name = "xiaozhi_local_retry";
    config.priority = RIVER_PLAYBACK_PRIO_TTS;
    config.sample_rate = RIVER_XIAOZHI_LOCAL_PROMPT_SAMPLE_RATE;
    config.frame_ms = RIVER_XIAOZHI_LOCAL_PROMPT_FRAME_MS;
    config.playback_channels = 2U;
    config.bits_per_sample = 16U;
    config.playback_frame_bytes = frame_bytes;
    config.buffer_frame_count = 4U;
    config.volume_left = 0.8f;
    config.volume_right = 0.8f;
    config.reference_export = true;
    config.reference_channels = 1U;
    config.reference_frame_bytes = frame_samples * sizeof(int16_t);
    config.reference_history_ms = 200U;

    if (river_playback_service_start_stream(&config) != RIVER_OK) {
        RIVER_LOGW("xiaozhi local fallback prompt skipped: reason=%s playback_start_failed",
                   reason != NULL ? reason : "-");
        return false;
    }

    mono_step_a = river_cloud_xiaozhi_prompt_tone_step(660U, RIVER_XIAOZHI_LOCAL_PROMPT_SAMPLE_RATE);
    mono_step_b = river_cloud_xiaozhi_prompt_tone_step(880U, RIVER_XIAOZHI_LOCAL_PROMPT_SAMPLE_RATE);
    for (frame_index = 0U; frame_index < RIVER_XIAOZHI_LOCAL_PROMPT_FRAMES; ++frame_index) {
        uint32_t step = (frame_index < 2U) ? mono_step_a : mono_step_b;
        size_t sample_index;

        river_cloud_xiaozhi_fill_prompt_tone(mono_frame, frame_samples, &tone_phase, step);
        for (sample_index = 0U; sample_index < frame_samples; ++sample_index) {
            stereo_frame[sample_index * 2U] = mono_frame[sample_index];
            stereo_frame[sample_index * 2U + 1U] = mono_frame[sample_index];
        }
        if (river_playback_service_write((const uint8_t *)stereo_frame,
                                         frame_bytes,
                                         (const uint8_t *)mono_frame,
                                         frame_samples * sizeof(int16_t),
                                         true) != RIVER_OK) {
            RIVER_LOGW("xiaozhi local fallback prompt write failed: reason=%s frame=%lu",
                       reason != NULL ? reason : "-",
                       (unsigned long)frame_index);
            (void)river_playback_service_stop_stream_ex("local_retry_prompt_write_failed");
            return false;
        }
    }

    (void)river_playback_service_stop_stream_ex("local_retry_prompt");
    last_prompt_ms = now_ms;
    RIVER_LOGI("xiaozhi local fallback prompt played: reason=%s stream=xiaozhi_local_retry frames=%u ref=yes",
               reason != NULL ? reason : "-",
               RIVER_XIAOZHI_LOCAL_PROMPT_FRAMES);
    return true;
}

static void river_cloud_xiaozhi_close_local_round_after_server_commit(
    const river_cloud_xiaozhi_turn_semantics_state_t *state,
    const char *trigger)
{
    if (state == NULL || !state->accepted ||
        !river_cloud_xiaozhi_input_state_server_committed(state->input_state)) {
        return;
    }
    if (!g_river_cloud.stream_active &&
        !g_river_cloud.xiaozhi_session_window_truth.listening &&
        !g_river_cloud.xiaozhi_session_window_truth.listen_stop_pending &&
        !g_river_cloud.xiaozhi_session_window_truth.local_close_pending) {
        return;
    }

    RIVER_LOGI("xiaozhi server committed input; closing local round without audio.in.commit: trigger=%s accept_reason=%s input_state=%s output_state=%s stream=%s listening=%s",
               trigger != NULL ? trigger : "-",
               state->accept_reason[0] != '\0' ? state->accept_reason : "-",
               state->input_state[0] != '\0' ? state->input_state : "-",
               state->output_state[0] != '\0' ? state->output_state : "-",
               g_river_cloud.stream_active ? "yes" : "no",
               g_river_cloud.xiaozhi_session_window_truth.listening ? "yes" : "no");
    river_cloud_xiaozhi_close_local_round_for_cause(
        RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_SERVER_RESPONSE,
        "server_endpoint_accept");
}

void river_cloud_xiaozhi_refresh_turn_semantics(const char *trigger)
{
    river_cloud_xiaozhi_turn_semantics_state_t *state = &g_river_cloud.xiaozhi_turn_semantics;
    const char *turn_id = river_xiaozhi_last_turn_id();
    const char *accept_reason = river_xiaozhi_last_accept_reason();
    const char *input_state = river_xiaozhi_last_input_state();
    const char *output_state = river_xiaozhi_last_output_state();
    bool barge_in_known = river_xiaozhi_last_barge_in_enabled_known();
    bool barge_in_enabled = river_xiaozhi_last_barge_in_enabled();
    bool accepted_before = state->accepted;
    bool accept_reason_present = accept_reason != NULL && accept_reason[0] != '\0';
    bool turn_id_changed;
    bool accept_reason_changed = false;
    bool input_state_changed;
    bool output_state_changed;
    bool barge_in_changed;

    turn_id_changed = river_cloud_xiaozhi_update_semantic_text(state->turn_id,
                                                               sizeof(state->turn_id),
                                                               turn_id);
    if (accept_reason_present) {
        accept_reason_changed = river_cloud_xiaozhi_update_semantic_text(
            state->accept_reason,
            sizeof(state->accept_reason),
            accept_reason);
        state->accepted = true;
        if (strcmp(state->fallback_reason, "await_accept_reason") == 0) {
            state->fallback_reason[0] = '\0';
        }
    }
    input_state_changed = river_cloud_xiaozhi_update_semantic_text(
        state->input_state,
        sizeof(state->input_state),
        input_state);
    output_state_changed = river_cloud_xiaozhi_update_semantic_text(
        state->output_state,
        sizeof(state->output_state),
        output_state);
    barge_in_changed = (state->barge_in_enabled_known != barge_in_known) ||
                       (barge_in_known && (state->barge_in_enabled != barge_in_enabled));
    state->barge_in_enabled_known = barge_in_known;
    state->barge_in_enabled = barge_in_known && barge_in_enabled;

    if (accept_reason_present &&
        (!accepted_before || accept_reason_changed || turn_id_changed ||
         input_state_changed || output_state_changed || barge_in_changed)) {
        RIVER_LOGI("xiaozhi turn accepted: trigger=%s accept_reason=%s turn_id=%s input_state=%s output_state=%s barge_in_enabled=%s",
                   trigger != NULL ? trigger : "-",
                   state->accept_reason[0] != '\0' ? state->accept_reason : "-",
                   state->turn_id[0] != '\0' ? state->turn_id : "-",
                   state->input_state[0] != '\0' ? state->input_state : "-",
                   state->output_state[0] != '\0' ? state->output_state : "-",
                   river_cloud_xiaozhi_optional_bool_text(state->barge_in_enabled_known,
                                                          state->barge_in_enabled));
    } else if (!accept_reason_present &&
               (turn_id_changed || input_state_changed || output_state_changed ||
                barge_in_changed)) {
        RIVER_LOGI("xiaozhi turn semantics updated before accept: trigger=%s turn_id=%s input_state=%s output_state=%s barge_in_enabled=%s",
                   trigger != NULL ? trigger : "-",
                   state->turn_id[0] != '\0' ? state->turn_id : "-",
                   state->input_state[0] != '\0' ? state->input_state : "-",
                   state->output_state[0] != '\0' ? state->output_state : "-",
                   river_cloud_xiaozhi_optional_bool_text(state->barge_in_enabled_known,
                                                          state->barge_in_enabled));
    }

    river_cloud_xiaozhi_close_local_round_after_server_commit(state, trigger);
    river_cloud_xiaozhi_clear_response_audio_wait_if_returned_active(state, trigger);
}

bool river_cloud_xiaozhi_turn_accepted(void)
{
    river_cloud_xiaozhi_turn_semantics_view_t semantics_view;

    river_cloud_xiaozhi_capture_turn_semantics_view(&semantics_view);
    return semantics_view.accepted;
}

void river_cloud_xiaozhi_note_semantic_fallback(const char *reason)
{
    river_voice_duplex_ready_eval_t duplex_eval;
    river_cloud_xiaozhi_turn_semantics_state_t *state = &g_river_cloud.xiaozhi_turn_semantics;
    river_cloud_xiaozhi_turn_semantics_view_t semantics_view;
    const char *duplex_default_reason;
    bool duplex_default_on;

    if (reason == NULL || reason[0] == '\0') {
        return;
    }
    if (strcmp(state->fallback_reason, reason) == 0) {
        return;
    }

    river_cloud_xiaozhi_get_duplex_ready_eval(&duplex_eval);
    duplex_default_reason = river_xiaozhi_duplex_default_fallback_reason();
    duplex_default_on = duplex_default_reason == NULL;
    river_cloud_xiaozhi_copy_semantic_text(state->fallback_reason,
                                           sizeof(state->fallback_reason),
                                           reason);
    river_cloud_xiaozhi_capture_turn_semantics_view(&semantics_view);
    RIVER_LOGW("xiaozhi fallback: reason=%s accepted=%s accept_reason=%s input_state=%s output_state=%s duplex_default_on=%s duplex_default_reason=%s duplex_ready=%s duplex_reason=%s playback=%s/%s error=%s sid=%s",
               semantics_view.fallback_reason != NULL ? semantics_view.fallback_reason : "-",
               semantics_view.accepted ? "yes" : "no",
               semantics_view.accept_reason != NULL ? semantics_view.accept_reason : "-",
               semantics_view.input_state != NULL ? semantics_view.input_state : "-",
               semantics_view.output_state != NULL ? semantics_view.output_state : "-",
               duplex_default_on ? "yes" : "no",
               duplex_default_reason != NULL ? duplex_default_reason : "-",
               duplex_eval.ready ? "yes" : "no",
               river_voice_runtime_duplex_ready_reason_name(duplex_eval.reason),
               river_playback_service_state_name(duplex_eval.playback_state),
               river_dialog_playback_owner_kind_name(
                   duplex_eval.dialog_playback_owner_kind),
               river_dialog_error_kind_name(duplex_eval.dialog_error_kind),
               river_cloud_xiaozhi_current_sid() != NULL ? river_cloud_xiaozhi_current_sid() : "-");
}

static bool river_cloud_xiaozhi_pending_text_ready_for_finalize(void)
{
    return g_river_cloud.xiaozhi_pending_transcript_truth.valid &&
           !g_river_cloud.xiaozhi_pending_transcript_truth.finalized &&
           g_river_cloud.xiaozhi_pending_transcript_truth.text[0] != '\0';
}

static void river_cloud_xiaozhi_commit_pending_text_finalization(const char *trigger)
{
    river_cloud_xiaozhi_turn_semantics_view_t semantics_view;

    g_river_cloud.xiaozhi_pending_transcript_truth.finalized = true;
    river_cloud_xiaozhi_capture_turn_semantics_view(&semantics_view);
    RIVER_LOGI("xiaozhi accepted turn final text: trigger=%s accept_reason=%s turn_id=%s text=%s",
               trigger != NULL ? trigger : "-",
               semantics_view.accept_reason != NULL ? semantics_view.accept_reason : "-",
               semantics_view.turn_id != NULL ? semantics_view.turn_id : "-",
               g_river_cloud.xiaozhi_pending_transcript_truth.text);
    river_cloud_emit_asr_result(RIVER_CLOUD_ASR_EVENT_FINAL,
                                g_river_cloud.xiaozhi_pending_transcript_truth.text,
                                river_cloud_xiaozhi_current_sid(),
                                NULL,
                                0,
                                true);
}

static void river_cloud_xiaozhi_finalize_pending_text_if_turn_accepted_after_refresh(
    const char *trigger)
{
    if (!river_cloud_xiaozhi_pending_text_ready_for_finalize()) {
        return;
    }

    if (!river_cloud_xiaozhi_turn_accepted()) {
        return;
    }

    river_cloud_xiaozhi_commit_pending_text_finalization(trigger);
}

void river_cloud_xiaozhi_finalize_pending_text_if_turn_accepted(const char *trigger)
{
    river_cloud_xiaozhi_refresh_turn_semantics(trigger);
    river_cloud_xiaozhi_finalize_pending_text_if_turn_accepted_after_refresh(trigger);
}

static void river_cloud_xiaozhi_recover_response_audio_timeout(const char *trigger)
{
    RIVER_LOGW("xiaozhi response audio timeout recovery: trigger=%s action=abort_close session=%s listening=%s stream=%s window=%s input_state=%s output_state=%s",
               trigger != NULL ? trigger : "-",
               river_xiaozhi_session_open() ? "open" : "closed",
               g_river_cloud.xiaozhi_session_window_truth.listening ? "yes" : "no",
               g_river_cloud.stream_active ? "yes" : "no",
               g_river_cloud.xiaozhi_session_window_truth.window_active ? "open" : "closed",
               g_river_cloud.xiaozhi_turn_semantics.input_state[0] != '\0' ?
                   g_river_cloud.xiaozhi_turn_semantics.input_state :
                   "-",
               g_river_cloud.xiaozhi_turn_semantics.output_state[0] != '\0' ?
                   g_river_cloud.xiaozhi_turn_semantics.output_state :
                   "-");

    (void)river_cloud_xiaozhi_interrupt_tts("response_audio_timeout");
    river_cloud_xiaozhi_close_local_round_for_cause(
        RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_SERVER_RESPONSE,
        "response_audio_timeout");
    river_cloud_xiaozhi_window_close("response_audio_timeout");
    (void)river_cloud_xiaozhi_play_local_retry_prompt("response_audio_timeout");
}

void river_cloud_xiaozhi_run_post_poll_housekeeping(void)
{
    river_cloud_xiaozhi_refresh_turn_semantics("poll");
    if (river_cloud_xiaozhi_check_response_audio_timeout()) {
        river_cloud_xiaozhi_recover_response_audio_timeout("poll");
        return;
    }
    river_cloud_xiaozhi_finalize_pending_text_if_turn_accepted_after_refresh(
        "accept_reason");
}

void river_cloud_xiaozhi_run_post_uplink_housekeeping(void)
{
    if (river_cloud_xiaozhi_check_response_audio_timeout()) {
        river_cloud_xiaozhi_recover_response_audio_timeout("uplink");
        return;
    }
    river_cloud_xiaozhi_playback_check_pending_stop();
    river_cloud_xiaozhi_run_endpoint_local_close_housekeeping();
}

void river_cloud_xiaozhi_finalize_pending_text(const char *trigger)
{
    river_cloud_xiaozhi_refresh_turn_semantics(trigger);

    if (!river_cloud_xiaozhi_pending_text_ready_for_finalize()) {
        return;
    }

    if (!river_cloud_xiaozhi_turn_accepted()) {
        river_cloud_xiaozhi_note_semantic_fallback("await_accept_reason");
        RIVER_LOGI("xiaozhi pending text waits for accepted turn: trigger=%s text=%s",
                   trigger != NULL ? trigger : "-",
                   g_river_cloud.xiaozhi_pending_transcript_truth.text);
        return;
    }

    river_cloud_xiaozhi_commit_pending_text_finalization(trigger);
}

void river_cloud_xiaozhi_round_finish(const char *reason)
{
    uint32_t now_ms;
    uint32_t duration_ms;
    uint32_t first_packet_delay_ms = 0U;
    uint32_t audio_ms = 0U;
    uint32_t realtime_gap_ms = 0U;
    uint32_t pace_pct = 0U;
    uint32_t uplink_count = g_river_cloud.xiaozhi_asr_round_truth.uplink_metric_count;
    uint32_t send_interval_p50 = 0U;
    uint32_t send_interval_p95 = 0U;
    uint32_t capture_age_p50 = 0U;
    uint32_t capture_age_p95 = 0U;
    uint32_t backlog_p95 = 0U;
    uint32_t send_duration_p50 = 0U;
    uint32_t send_duration_p95 = 0U;
    uint32_t busy_delta;
    uint32_t fail_delta;
    uint32_t stale_delta;
    uint32_t ring_drop_delta;
    const char *close_reason;

    if (!g_river_cloud.xiaozhi_asr_round_truth.active) {
        return;
    }

    now_ms = (uint32_t)rtos_time_get_current_system_time_ms();
    duration_ms = now_ms - g_river_cloud.xiaozhi_asr_round_truth.started_ms;
    if (g_river_cloud.xiaozhi_asr_round_truth.first_packet_ms != 0U &&
        g_river_cloud.xiaozhi_asr_round_truth.first_packet_ms >=
            g_river_cloud.xiaozhi_asr_round_truth.started_ms) {
        first_packet_delay_ms =
            g_river_cloud.xiaozhi_asr_round_truth.first_packet_ms -
            g_river_cloud.xiaozhi_asr_round_truth.started_ms;
    }
    if (g_river_cloud.xiaozhi_asr_round_truth.packets_sent <=
        (UINT32_MAX / RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS)) {
        audio_ms = g_river_cloud.xiaozhi_asr_round_truth.packets_sent *
                   RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS;
    } else {
        audio_ms = UINT32_MAX;
    }
    if (duration_ms > audio_ms) {
        realtime_gap_ms = duration_ms - audio_ms;
    }
    if (duration_ms != 0U) {
        pace_pct = (uint32_t)(((uint64_t)audio_ms * 100ULL) / (uint64_t)duration_ms);
    }
    if (uplink_count > 0U) {
        send_interval_p50 = river_cloud_xiaozhi_round_percentile(
            g_river_cloud.xiaozhi_asr_round_truth.uplink_send_interval_samples,
            uplink_count,
            50U);
        send_interval_p95 = river_cloud_xiaozhi_round_percentile(
            g_river_cloud.xiaozhi_asr_round_truth.uplink_send_interval_samples,
            uplink_count,
            95U);
        capture_age_p50 = river_cloud_xiaozhi_round_percentile(
            g_river_cloud.xiaozhi_asr_round_truth.uplink_capture_age_samples,
            uplink_count,
            50U);
        capture_age_p95 = river_cloud_xiaozhi_round_percentile(
            g_river_cloud.xiaozhi_asr_round_truth.uplink_capture_age_samples,
            uplink_count,
            95U);
        backlog_p95 = river_cloud_xiaozhi_round_percentile(
            g_river_cloud.xiaozhi_asr_round_truth.uplink_backlog_samples,
            uplink_count,
            95U);
        send_duration_p50 = river_cloud_xiaozhi_round_percentile(
            g_river_cloud.xiaozhi_asr_round_truth.uplink_send_duration_samples,
            uplink_count,
            50U);
        send_duration_p95 = river_cloud_xiaozhi_round_percentile(
            g_river_cloud.xiaozhi_asr_round_truth.uplink_send_duration_samples,
            uplink_count,
            95U);
    }
    busy_delta = g_river_cloud.xiaozhi_uplink_runtime_truth.busy_count -
                 g_river_cloud.xiaozhi_asr_round_truth.busy_base;
    fail_delta = g_river_cloud.xiaozhi_uplink_runtime_truth.fail_count -
                 g_river_cloud.xiaozhi_asr_round_truth.fail_base;
    stale_delta = g_river_cloud.xiaozhi_uplink_runtime_truth.stale_dropped -
                  g_river_cloud.xiaozhi_asr_round_truth.stale_drop_base;
    ring_drop_delta = g_river_cloud.xiaozhi_uplink_runtime_truth.ring_dropped -
                      g_river_cloud.xiaozhi_asr_round_truth.ring_drop_base;
    close_reason = (reason != NULL && reason[0] != '\0') ?
                       reason :
                       (g_river_cloud.xiaozhi_asr_round_truth.close_reason[0] != '\0' ?
                            g_river_cloud.xiaozhi_asr_round_truth.close_reason :
                            "-");

    RIVER_LOGI("xiaozhi asr round finish: id=%lu sid=%s reason=%s duration_ms=%lu audio_ms=%lu realtime_gap_ms=%lu pace_pct=%lu first_packet_delay_ms=%lu pre_roll_frames=%lu packets=%lu burst_max=%lu preview_warmup[target_ms=%u done_ms=%lu bypass=%lu] busy=%lu fail=%lu stale_drop=%lu ring_drop=%lu partial=%lu final=%lu seen[partial=%s final=%s] uplink_ms[send_interval_p50=%lu send_interval_p95=%lu capture_age_p50=%lu capture_age_p95=%lu backlog_p95=%lu send_duration_p50=%lu send_duration_p95=%lu]",
               (unsigned long)g_river_cloud.xiaozhi_asr_round_truth.id,
               river_cloud_xiaozhi_current_sid() != NULL ?
                   river_cloud_xiaozhi_current_sid() :
                   "-",
               close_reason,
               (unsigned long)duration_ms,
               (unsigned long)audio_ms,
               (unsigned long)realtime_gap_ms,
               (unsigned long)pace_pct,
               (unsigned long)first_packet_delay_ms,
               (unsigned long)g_river_cloud.xiaozhi_asr_round_truth.pre_roll_frames,
               (unsigned long)g_river_cloud.xiaozhi_asr_round_truth.packets_sent,
               (unsigned long)g_river_cloud.xiaozhi_asr_round_truth.burst_max,
               (unsigned int)RIVER_CLOUD_XIAOZHI_UPLINK_PREVIEW_WARMUP_MS,
               (unsigned long)g_river_cloud.xiaozhi_asr_round_truth.preview_warmup_done_ms,
               (unsigned long)g_river_cloud.xiaozhi_asr_round_truth.preview_warmup_bypass_count,
               (unsigned long)busy_delta,
               (unsigned long)fail_delta,
               (unsigned long)stale_delta,
               (unsigned long)ring_drop_delta,
               (unsigned long)g_river_cloud.xiaozhi_asr_round_truth.partial_count,
               (unsigned long)g_river_cloud.xiaozhi_asr_round_truth.final_count,
               g_river_cloud.xiaozhi_asr_round_truth.partial_seen ? "yes" : "no",
               g_river_cloud.xiaozhi_asr_round_truth.final_seen ? "yes" : "no",
               (unsigned long)send_interval_p50,
               (unsigned long)send_interval_p95,
               (unsigned long)capture_age_p50,
               (unsigned long)capture_age_p95,
               (unsigned long)backlog_p95,
               (unsigned long)send_duration_p50,
               (unsigned long)send_duration_p95);

    g_river_cloud.xiaozhi_asr_round_truth.active = false;
    g_river_cloud.xiaozhi_asr_round_truth.started_ms = 0U;
    g_river_cloud.xiaozhi_asr_round_truth.first_packet_ms = 0U;
    g_river_cloud.xiaozhi_asr_round_truth.pre_roll_frames = 0U;
    g_river_cloud.xiaozhi_asr_round_truth.packets_sent = 0U;
    g_river_cloud.xiaozhi_asr_round_truth.partial_count = 0U;
    g_river_cloud.xiaozhi_asr_round_truth.final_count = 0U;
    g_river_cloud.xiaozhi_asr_round_truth.partial_seen = false;
    g_river_cloud.xiaozhi_asr_round_truth.final_seen = false;
    g_river_cloud.xiaozhi_asr_round_truth.burst_max = 0U;
    g_river_cloud.xiaozhi_asr_round_truth.preview_warmup_done_ms = 0U;
    g_river_cloud.xiaozhi_asr_round_truth.preview_warmup_bypass_count = 0U;
    g_river_cloud.xiaozhi_asr_round_truth.close_reason[0] = '\0';
}

river_status_t river_cloud_xiaozhi_execute_open_and_listen_transport(
    const char *mode)
{
    river_status_t status;
    const char *listen_mode =
        (mode != NULL && mode[0] != '\0') ? mode : "auto";

    if (!river_xiaozhi_session_open()) {
        status = river_xiaozhi_open_session();
        if (status != RIVER_OK) {
            return status;
        }
    }

    river_cloud_xiaozhi_copy_session_id_from_transport();
    if (!river_cloud_xiaozhi_listening_active()) {
        status = river_xiaozhi_send_listen_start(listen_mode);
        if (status != RIVER_OK) {
            return status;
        }
    }

    return RIVER_OK;
}

river_status_t river_cloud_xiaozhi_execute_session_control_transport(
    river_cloud_xiaozhi_control_op_t op,
    const char *arg)
{
    switch (op) {
    case RIVER_CLOUD_XIAOZHI_CTRL_LISTEN_STOP:
        if (river_cloud_xiaozhi_uplink_send_ready()) {
            return river_xiaozhi_send_listen_stop();
        }
        return RIVER_OK;

    case RIVER_CLOUD_XIAOZHI_CTRL_ABORT:
        if (!river_xiaozhi_session_open()) {
            return RIVER_OK;
        }
        return river_xiaozhi_send_abort(
            (arg != NULL && arg[0] != '\0') ? arg : NULL);

    case RIVER_CLOUD_XIAOZHI_CTRL_CLOSE_SESSION:
        river_xiaozhi_close_session();
        return RIVER_OK;

    default:
        return RIVER_ERR_UNSUPPORTED;
    }
}

#endif
