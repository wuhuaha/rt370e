/* 云端适配总控：管理 SNTP、ASR/TTS、会话窗口和与语音前端的桥接。 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lwip/apps/sntp.h"
#include "sntp/sntp_api.h"

#include "os_wrapper.h"

#include "river/river_audio_frame_ring.h"
#include "river/river_cloud.h"
#include "river/river_log.h"
#include "river/river_opus_codec.h"
#include "river/river_playback_service.h"
#include "river/river_runtime_stats.h"
#include "river/river_voice_kws.h"
#include "river/river_voice_profile.h"
#include "river/river_wifi_station.h"
#include "river/river_xiaozhi_credentials.h"
#include "river/river_xiaozhi_ws.h"
#include "river_cloud_internal.h"
#include "river_asr_provider_internal.h"
#include "river_tts_internal.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.cloud"

river_cloud_context_t g_river_cloud;

#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
static void river_cloud_xiaozhi_event_handler(const river_xiaozhi_event_t *event, void *user_data);
static river_status_t river_cloud_xiaozhi_send_uplink_packet(const uint8_t *pcm,
                                                             size_t pcm_bytes);
static bool river_cloud_xiaozhi_io_owner(void);
static river_status_t river_cloud_xiaozhi_control_execute(
    const river_cloud_xiaozhi_control_request_t *request);
static river_status_t river_cloud_xiaozhi_control_request(
    river_cloud_xiaozhi_control_op_t op,
    const char *arg);
static void river_cloud_xiaozhi_process_control_queue(void);
#endif

#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
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

static uint32_t river_cloud_xiaozhi_uplink_busy_backoff_ms(uint32_t frame_ms,
                                                           uint32_t busy_streak)
{
    uint32_t delay_ms;
    uint32_t shift_count;
    uint32_t soft_cap_ms;

    /*
     * The websocket pump is already running at 5 ms cadence. Starting BUSY
     * backoff at 40 ms leaves the uplink worker far behind real-time audio and
     * lets the local PCM ring fill before the transport has a chance to
     * recover. Grow from the poll cadence instead.
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

static void river_cloud_xiaozhi_log_uplink_backpressure(uint64_t now_ms, uint32_t backoff_ms)
{
    if (g_river_cloud.xiaozhi_uplink_last_busy_log_ms != 0U &&
        (now_ms - g_river_cloud.xiaozhi_uplink_last_busy_log_ms) <
            RIVER_CLOUD_XIAOZHI_UPLINK_BUSY_LOG_INTERVAL_MS) {
        return;
    }

    g_river_cloud.xiaozhi_uplink_last_busy_log_ms = now_ms;
    RIVER_LOGW("xiaozhi uplink backpressure: queued=%lu/%u busy=%lu streak=%lu backoff=%lums stale_drop=%lu",
               (unsigned long)river_cloud_xiaozhi_uplink_ready_frames(),
               (unsigned int)RIVER_CLOUD_XIAOZHI_UPLINK_RING_FRAMES,
               (unsigned long)g_river_cloud.xiaozhi_uplink_busy_count,
               (unsigned long)g_river_cloud.xiaozhi_uplink_busy_streak,
               (unsigned long)backoff_ms,
               (unsigned long)g_river_cloud.xiaozhi_uplink_stale_dropped);
}

static river_status_t river_cloud_xiaozhi_control_execute(
    const river_cloud_xiaozhi_control_request_t *request)
{
    river_status_t status = RIVER_OK;

    if (request == NULL) {
        return RIVER_ERR_ARG;
    }

    switch (request->op) {
    case RIVER_CLOUD_XIAOZHI_CTRL_OPEN_AND_LISTEN:
        if (!river_xiaozhi_session_open()) {
            status = river_xiaozhi_open_session();
            if (status != RIVER_OK) {
                return status;
            }
        }
        river_cloud_xiaozhi_copy_session_id_from_transport();
        if (!river_cloud_xiaozhi_listening_active()) {
            status = river_xiaozhi_send_listen_start(
                request->arg[0] != '\0' ? request->arg : "auto");
            if (status != RIVER_OK) {
                return status;
            }
        }
        return RIVER_OK;
    case RIVER_CLOUD_XIAOZHI_CTRL_LISTEN_STOP:
        if (river_cloud_xiaozhi_uplink_send_ready()) {
            return river_xiaozhi_send_listen_stop();
        }
        return RIVER_OK;
    case RIVER_CLOUD_XIAOZHI_CTRL_ABORT:
        if (!river_xiaozhi_session_open()) {
            return RIVER_OK;
        }
        return river_xiaozhi_send_abort(request->arg[0] != '\0' ? request->arg : NULL);
    case RIVER_CLOUD_XIAOZHI_CTRL_CLOSE_SESSION:
        river_xiaozhi_close_session();
        return RIVER_OK;
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
        return RIVER_ERR_ARG;
    }
}

static river_status_t river_cloud_xiaozhi_control_request(
    river_cloud_xiaozhi_control_op_t op,
    const char *arg)
{
    river_cloud_xiaozhi_control_request_t request;
    river_status_t result = RIVER_ERR_BUSY;
    rtos_sema_t completion = NULL;

    if (!g_river_cloud.initialized || !g_river_cloud.xiaozhi_enabled) {
        return RIVER_ERR_UNSUPPORTED;
    }

    memset(&request, 0, sizeof(request));
    request.op = op;
    if (arg != NULL && arg[0] != '\0') {
        snprintf(request.arg, sizeof(request.arg), "%s", arg);
    }

    if (river_cloud_xiaozhi_io_owner()) {
        return river_cloud_xiaozhi_control_execute(&request);
    }

    if (g_river_cloud.xiaozhi_control_ready == NULL ||
        g_river_cloud.xiaozhi_control_space == NULL) {
        return RIVER_ERR_BUSY;
    }

    if (rtos_sema_create_binary(&completion) != RTK_SUCCESS) {
        return RIVER_ERR_NO_MEMORY;
    }

    request.completion = completion;
    request.result_out = &result;
    if (rtos_sema_take(g_river_cloud.xiaozhi_control_space,
                       RIVER_CLOUD_XIAOZHI_CONTROL_WAIT_MS) != RTK_SUCCESS) {
        rtos_sema_delete(completion);
        return RIVER_ERR_BUSY;
    }

    if (!river_cloud_xiaozhi_control_lock()) {
        (void)rtos_sema_give(g_river_cloud.xiaozhi_control_space);
        rtos_sema_delete(completion);
        return RIVER_ERR_BUSY;
    }

    g_river_cloud.xiaozhi_control_queue[g_river_cloud.xiaozhi_control_write_index] = request;
    g_river_cloud.xiaozhi_control_write_index++;
    if (g_river_cloud.xiaozhi_control_write_index >=
        RIVER_CLOUD_XIAOZHI_CONTROL_QUEUE_DEPTH) {
        g_river_cloud.xiaozhi_control_write_index = 0U;
    }
    if (g_river_cloud.xiaozhi_control_count < UINT32_MAX) {
        g_river_cloud.xiaozhi_control_count++;
    }
    if (g_river_cloud.xiaozhi_control_count > g_river_cloud.xiaozhi_control_high_watermark) {
        g_river_cloud.xiaozhi_control_high_watermark = g_river_cloud.xiaozhi_control_count;
    }
    river_cloud_xiaozhi_control_unlock();

    (void)rtos_sema_give(g_river_cloud.xiaozhi_control_ready);
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

    memset(&request, 0, sizeof(request));
    request.op = op;
    request.played_duration_ms = played_duration_ms;
    if (arg != NULL && arg[0] != '\0') {
        snprintf(request.arg, sizeof(request.arg), "%s", arg);
    }
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
        return river_cloud_xiaozhi_control_execute(&request);
    }

    if (g_river_cloud.xiaozhi_control_ready == NULL ||
        g_river_cloud.xiaozhi_control_space == NULL) {
        return RIVER_ERR_BUSY;
    }
    if (rtos_sema_take(g_river_cloud.xiaozhi_control_space, 0U) != RTK_SUCCESS) {
        return RIVER_ERR_BUSY;
    }
    if (!river_cloud_xiaozhi_control_lock()) {
        (void)rtos_sema_give(g_river_cloud.xiaozhi_control_space);
        return RIVER_ERR_BUSY;
    }

    g_river_cloud.xiaozhi_control_queue[g_river_cloud.xiaozhi_control_write_index] = request;
    g_river_cloud.xiaozhi_control_write_index++;
    if (g_river_cloud.xiaozhi_control_write_index >=
        RIVER_CLOUD_XIAOZHI_CONTROL_QUEUE_DEPTH) {
        g_river_cloud.xiaozhi_control_write_index = 0U;
    }
    if (g_river_cloud.xiaozhi_control_count < UINT32_MAX) {
        g_river_cloud.xiaozhi_control_count++;
    }
    if (g_river_cloud.xiaozhi_control_count > g_river_cloud.xiaozhi_control_high_watermark) {
        g_river_cloud.xiaozhi_control_high_watermark = g_river_cloud.xiaozhi_control_count;
    }
    river_cloud_xiaozhi_control_unlock();

    (void)rtos_sema_give(g_river_cloud.xiaozhi_control_ready);
    return RIVER_OK;
}

static void river_cloud_xiaozhi_process_control_queue(void)
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

        request = g_river_cloud.xiaozhi_control_queue[g_river_cloud.xiaozhi_control_read_index];
        g_river_cloud.xiaozhi_control_read_index++;
        if (g_river_cloud.xiaozhi_control_read_index >=
            RIVER_CLOUD_XIAOZHI_CONTROL_QUEUE_DEPTH) {
            g_river_cloud.xiaozhi_control_read_index = 0U;
        }
        if (g_river_cloud.xiaozhi_control_count > 0U) {
            g_river_cloud.xiaozhi_control_count--;
        }
        river_cloud_xiaozhi_control_unlock();

        (void)rtos_sema_give(g_river_cloud.xiaozhi_control_space);
        status = river_cloud_xiaozhi_control_execute(&request);
        if (request.result_out != NULL) {
            *request.result_out = status;
        }
        if (request.completion != NULL) {
            (void)rtos_sema_give(request.completion);
        }
    }
}

static void river_cloud_xiaozhi_io_service_uplink(void)
{
    river_status_t status;
    bool can_send;
    uint32_t frame_ms;
    uint64_t now_ms;
    uint32_t drained_frames = 0U;

    if (!river_cloud_xiaozhi_uplink_active()) {
        g_river_cloud.xiaozhi_uplink_next_send_ms = 0U;
        g_river_cloud.xiaozhi_uplink_busy_streak = 0U;
        return;
    }

    can_send = river_cloud_xiaozhi_uplink_send_ready();
    if (can_send && g_river_cloud.xiaozhi_uplink_next_send_ms != 0U) {
        now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
        if (now_ms < g_river_cloud.xiaozhi_uplink_next_send_ms) {
            rtos_time_delay_ms(
                (uint32_t)(g_river_cloud.xiaozhi_uplink_next_send_ms - now_ms));
        }
    }

    can_send = river_cloud_xiaozhi_uplink_send_ready();
    if (can_send) {
        (void)river_cloud_xiaozhi_trim_uplink_stale_frames(
            RIVER_CLOUD_XIAOZHI_UPLINK_STALE_FRAMES_MAX);
    }

    frame_ms = RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS;
    while (drained_frames < RIVER_CLOUD_XIAOZHI_UPLINK_DRAIN_BURST_MAX) {
        if (!g_river_cloud.xiaozhi_uplink_retry_valid) {
            status = river_audio_frame_ring_read(&g_river_cloud.xiaozhi_uplink_ring,
                                                 g_river_cloud.xiaozhi_uplink_task_frame);
            if (status != RIVER_OK) {
                break;
            }
            g_river_cloud.xiaozhi_uplink_retry_valid = true;
        }

        can_send = river_cloud_xiaozhi_uplink_send_ready();
        if (!can_send) {
            g_river_cloud.xiaozhi_uplink_busy_streak = 0U;
            g_river_cloud.xiaozhi_uplink_next_send_ms = 0U;
            river_cloud_xiaozhi_maybe_finalize_listen_stop(
                river_cloud_xiaozhi_uplink_ready_frames(),
                g_river_cloud.xiaozhi_uplink_accum_bytes);
            return;
        }

        status = river_cloud_xiaozhi_send_uplink_packet(
            g_river_cloud.xiaozhi_uplink_task_frame,
            RIVER_CLOUD_XIAOZHI_UPLINK_PCM_FRAME_MAX);
        now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
        if (status == RIVER_OK) {
            g_river_cloud.xiaozhi_uplink_retry_valid = false;
            g_river_cloud.xiaozhi_uplink_busy_streak = 0U;
            g_river_cloud.xiaozhi_uplink_next_send_ms = 0U;
            drained_frames++;
            continue;
        }

        if (status == RIVER_ERR_BUSY) {
            uint32_t backoff_ms;

            g_river_cloud.xiaozhi_uplink_busy_count++;
            g_river_cloud.xiaozhi_uplink_busy_streak++;
            backoff_ms = river_cloud_xiaozhi_uplink_busy_backoff_ms(
                frame_ms,
                g_river_cloud.xiaozhi_uplink_busy_streak);
            g_river_cloud.xiaozhi_uplink_next_send_ms = now_ms + (uint64_t)backoff_ms;
            (void)river_cloud_xiaozhi_trim_uplink_stale_frames(
                RIVER_CLOUD_XIAOZHI_UPLINK_STALE_FRAMES_MAX);
            river_cloud_xiaozhi_log_uplink_backpressure(now_ms, backoff_ms);
            break;
        }

        g_river_cloud.xiaozhi_uplink_fail_count++;
        g_river_cloud.xiaozhi_uplink_busy_streak = 0U;
        RIVER_LOGW("xiaozhi uplink send failed: status=%d", status);
        if (frame_ms != 0U) {
            g_river_cloud.xiaozhi_uplink_next_send_ms = now_ms + (uint64_t)frame_ms;
        } else {
            g_river_cloud.xiaozhi_uplink_next_send_ms = now_ms + 1U;
        }
        break;
    }

    river_cloud_xiaozhi_maybe_finalize_listen_stop(
        river_cloud_xiaozhi_uplink_ready_frames(),
        g_river_cloud.xiaozhi_uplink_accum_bytes);
}

static void river_cloud_xiaozhi_io_task(void *arg)
{
    char endpoint_soft_close_reason[RIVER_CLOUD_XIAOZHI_PREVIEW_REASON_MAX];

    (void)arg;

    for (;;) {
        river_cloud_xiaozhi_process_control_queue();
        river_cloud_xiaozhi_refresh_turn_semantics("io_tick");
        river_cloud_xiaozhi_check_window_timeout();
        endpoint_soft_close_reason[0] = '\0';
        if (river_cloud_xiaozhi_poll_endpoint_soft_close_timeout(
                endpoint_soft_close_reason,
                sizeof(endpoint_soft_close_reason))) {
            river_cloud_xiaozhi_complete_active_stream_finish(
                RIVER_CLOUD_XIAOZHI_STREAM_FINISH_ENDPOINT_TIMEOUT,
                endpoint_soft_close_reason);
        }
        river_cloud_xiaozhi_check_local_close_timeout();

        if (river_cloud_xiaozhi_io_has_work()) {
            if (river_xiaozhi_session_open()) {
                (void)river_xiaozhi_poll(RIVER_CLOUD_XIAOZHI_IO_ACTIVE_MS);
            } else {
                rtos_time_delay_ms(RIVER_CLOUD_XIAOZHI_IO_ACTIVE_MS);
            }
            river_cloud_xiaozhi_refresh_turn_semantics("poll");
            river_cloud_xiaozhi_finalize_pending_text_if_turn_accepted("accept_reason");
            river_cloud_xiaozhi_process_control_queue();
            river_cloud_xiaozhi_io_service_uplink();
            river_cloud_xiaozhi_playback_check_pending_stop();
            endpoint_soft_close_reason[0] = '\0';
            if (river_cloud_xiaozhi_poll_endpoint_soft_close_timeout(
                    endpoint_soft_close_reason,
                    sizeof(endpoint_soft_close_reason))) {
                river_cloud_xiaozhi_complete_active_stream_finish(
                    RIVER_CLOUD_XIAOZHI_STREAM_FINISH_ENDPOINT_TIMEOUT,
                    endpoint_soft_close_reason);
            }
            river_cloud_xiaozhi_check_local_close_timeout();
            rtos_time_delay_ms(RIVER_CLOUD_XIAOZHI_IO_FAIRNESS_DELAY_MS);
            continue;
        }

        g_river_cloud.xiaozhi_uplink_next_send_ms = 0U;
        g_river_cloud.xiaozhi_uplink_busy_streak = 0U;
        rtos_time_delay_ms(RIVER_CLOUD_XIAOZHI_IO_IDLE_MS);
    }
}

static bool river_cloud_xiaozhi_prepare_control_queue(void)
{
    if (g_river_cloud.xiaozhi_control_lock != NULL &&
        g_river_cloud.xiaozhi_control_ready != NULL &&
        g_river_cloud.xiaozhi_control_space != NULL) {
        return true;
    }

    if (g_river_cloud.xiaozhi_control_lock == NULL &&
        rtos_mutex_create(&g_river_cloud.xiaozhi_control_lock) != RTK_SUCCESS) {
        return false;
    }
    if (g_river_cloud.xiaozhi_control_ready == NULL &&
        rtos_sema_create(&g_river_cloud.xiaozhi_control_ready,
                         0U,
                         RIVER_CLOUD_XIAOZHI_CONTROL_QUEUE_DEPTH) != RTK_SUCCESS) {
        rtos_mutex_delete(g_river_cloud.xiaozhi_control_lock);
        g_river_cloud.xiaozhi_control_lock = NULL;
        return false;
    }
    if (g_river_cloud.xiaozhi_control_space == NULL &&
        rtos_sema_create(&g_river_cloud.xiaozhi_control_space,
                         RIVER_CLOUD_XIAOZHI_CONTROL_QUEUE_DEPTH,
                         RIVER_CLOUD_XIAOZHI_CONTROL_QUEUE_DEPTH) != RTK_SUCCESS) {
        rtos_sema_delete(g_river_cloud.xiaozhi_control_ready);
        g_river_cloud.xiaozhi_control_ready = NULL;
        rtos_mutex_delete(g_river_cloud.xiaozhi_control_lock);
        g_river_cloud.xiaozhi_control_lock = NULL;
        return false;
    }

    return true;
}

static void river_cloud_xiaozhi_start_io_if_needed(void)
{
    if (!g_river_cloud.xiaozhi_enabled || g_river_cloud.xiaozhi_io_started) {
        return;
    }

    if (!river_cloud_xiaozhi_prepare_control_queue()) {
        RIVER_LOGW("xiaozhi control queue init failed");
        return;
    }

    if (rtos_task_create(&g_river_cloud.xiaozhi_io_task,
                         "river_xz_io",
                         river_cloud_xiaozhi_io_task,
                         NULL,
                         RIVER_CLOUD_XIAOZHI_IO_TASK_STACK,
                         RIVER_CLOUD_XIAOZHI_IO_TASK_PRIO) != RTK_SUCCESS) {
        RIVER_LOGW("xiaozhi io task create failed");
        return;
    }

    g_river_cloud.xiaozhi_io_started = true;
    RIVER_LOGI("xiaozhi io owner started");
}
#endif

static uint32_t river_cloud_system_utc_seconds(void)
{
    uint32_t sec = 0U;
    uint32_t usec = 0U;
    time_t now;

    sntp_get_system_time(&sec, &usec);
    if (sec >= RIVER_CLOUD_TIME_READY_EPOCH_MIN) {
        return sec;
    }

    time(&now);
    if (now >= (time_t)RIVER_CLOUD_TIME_READY_EPOCH_MIN) {
        return (uint32_t)now;
    }

    return 0U;
}

static bool river_cloud_system_time_ready(void)
{
    return river_cloud_system_utc_seconds() >= RIVER_CLOUD_TIME_READY_EPOCH_MIN;
}

static bool river_cloud_business_time_ready(void)
{
#if RIVER_CLOUD_BUSINESS_TIME_WAIT_REQUIRED
    return river_cloud_system_time_ready();
#else
    return true;
#endif
}

static uint32_t river_cloud_estimated_utc_seconds(void)
{
    uint32_t sec;

    sec = river_cloud_system_utc_seconds();
    if (sec != 0U) {
        return sec;
    }

    if (g_river_cloud.time_seeded_from_build) {
        uint64_t elapsed_ms;

        elapsed_ms = (uint64_t)rtos_time_get_current_system_time_ms() -
                     g_river_cloud.seeded_utc_rtos_ms;
        return g_river_cloud.seeded_utc_epoch + (uint32_t)(elapsed_ms / 1000ULL);
    }

    return 0U;
}

static bool river_cloud_is_leap_year(int year)
{
    return ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
}

static int river_cloud_month_from_abbrev(const char *month)
{
    static const char *const k_months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    int index;

    if (month == NULL) {
        return -1;
    }

    for (index = 0; index < 12; ++index) {
        if (strncmp(month, k_months[index], 3U) == 0) {
            return index + 1;
        }
    }

    return -1;
}

static uint32_t river_cloud_days_before_month(int year, int month)
{
    static const uint16_t k_days_before_month[] = {
        0U,   31U,  59U,  90U,  120U, 151U,
        181U, 212U, 243U, 273U, 304U, 334U
    };
    uint32_t days;

    if (month <= 0) {
        return 0U;
    }

    days = k_days_before_month[month - 1];
    if (month > 2 && river_cloud_is_leap_year(year)) {
        days++;
    }
    return days;
}

static time_t river_cloud_epoch_from_utc_components(int year,
                                                    int month,
                                                    int day,
                                                    int hour,
                                                    int minute,
                                                    int second)
{
    uint32_t days;
    int current_year;

    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 59) {
        return (time_t)0;
    }

    days = 0U;
    for (current_year = 1970; current_year < year; ++current_year) {
        days += river_cloud_is_leap_year(current_year) ? 366U : 365U;
    }
    days += river_cloud_days_before_month(year, month);
    days += (uint32_t)(day - 1);

    return (time_t)((days * 24U * 60U * 60U) +
                    ((uint32_t)hour * 60U * 60U) +
                    ((uint32_t)minute * 60U) +
                    (uint32_t)second);
}

static uint32_t river_cloud_ms_to_frames(uint32_t duration_ms, uint32_t frame_ms)
{
    if (duration_ms == 0U || frame_ms == 0U) {
        return 0U;
    }

    return (duration_ms + frame_ms - 1U) / frame_ms;
}

uint32_t river_cloud_now_utc_seconds(void)
{
    return river_cloud_system_utc_seconds();
}

bool river_cloud_utc_ready(void)
{
    return river_cloud_now_utc_seconds() >= RIVER_CLOUD_TIME_READY_EPOCH_MIN;
}

bool river_cloud_time_ready(void)
{
    return river_cloud_system_time_ready();
}

bool river_cloud_wake_admission_time_ready(void)
{
#if !RIVER_CLOUD_BUSINESS_TIME_WAIT_REQUIRED
    return true;
#else
    return river_cloud_estimated_utc_seconds() >= RIVER_CLOUD_TIME_READY_EPOCH_MIN;
#endif
}

void river_cloud_start_sntp_if_needed(void)
{
    if (g_river_cloud.sntp_started) {
        return;
    }

    sntp_setservername(0, RIVER_CLOUD_DEFAULT_SNTP_SERVER);
    sntp_set_update_interval(RIVER_CLOUD_SNTP_UPDATE_INTERVAL_MS);
    sntp_init();
    g_river_cloud.sntp_started = true;
    RIVER_LOGI("sntp init: server=%s interval_ms=%u",
               RIVER_CLOUD_DEFAULT_SNTP_SERVER,
               (unsigned int)RIVER_CLOUD_SNTP_UPDATE_INTERVAL_MS);
}

static void river_cloud_kick_sntp_on_network_ready(void)
{
    if (river_cloud_time_ready()) {
        river_cloud_log_time_ready_once();
        return;
    }

    if (g_river_cloud.sntp_started) {
        sntp_stop();
        g_river_cloud.sntp_started = false;
    }

    river_cloud_start_sntp_if_needed();
    RIVER_LOGI("sntp kick: network ready; request immediate sync");
}

void river_cloud_seed_time_from_build_if_needed(void)
{
    char month_text[4];
    int month;
    int day;
    int year;
    int hour;
    int minute;
    int second;
    time_t build_local_epoch;
    uint32_t seeded_utc_epoch;

    if (river_cloud_time_ready()) {
        return;
    }

    if (g_river_cloud.time_seeded_from_build) {
        return;
    }

    if (sscanf(__DATE__, "%3s %d %d", month_text, &day, &year) != 3) {
        return;
    }
    month_text[3] = '\0';
    if (sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second) != 3) {
        return;
    }

    month = river_cloud_month_from_abbrev(month_text);
    if (month < 1) {
        return;
    }

    build_local_epoch = river_cloud_epoch_from_utc_components(year,
                                                              month,
                                                              day,
                                                              hour,
                                                              minute,
                                                              second);
    if (build_local_epoch == (time_t)0) {
        return;
    }

    seeded_utc_epoch = (uint32_t)(build_local_epoch - RIVER_CLOUD_BUILD_TZ_OFFSET_SECONDS);
    if (seeded_utc_epoch < RIVER_CLOUD_TIME_READY_EPOCH_MIN) {
        return;
    }

    g_river_cloud.time_seeded_from_build = true;
    g_river_cloud.seeded_utc_epoch = seeded_utc_epoch;
    g_river_cloud.seeded_utc_rtos_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    RIVER_LOGI("seed utc estimate from build time: est_utc=%lu build_local=%s %s tz_offset_sec=%ld",
               (unsigned long)river_cloud_estimated_utc_seconds(),
               __DATE__,
               __TIME__,
               (long)RIVER_CLOUD_BUILD_TZ_OFFSET_SECONDS);
}

void river_cloud_log_time_ready_once(void)
{
    if (g_river_cloud.time_ready_announced) {
        return;
    }

    if (!river_cloud_time_ready()) {
        return;
    }

    g_river_cloud.time_ready_announced = true;
    RIVER_LOGI("sntp ready: utc=%lu", (unsigned long)river_cloud_now_utc_seconds());
}

void river_cloud_log_wake_admission_deferred_once(river_status_t status)
{
    bool wifi_connected;
    bool time_ready;

    wifi_connected = river_wifi_station_is_connected();
    time_ready = river_cloud_wake_admission_time_ready();
    if ((g_river_cloud.wake_admission_defer_status == (int)status) &&
        (g_river_cloud.wake_admission_defer_wifi_connected == wifi_connected) &&
        (g_river_cloud.wake_admission_defer_time_ready == time_ready)) {
        return;
    }

    g_river_cloud.wake_admission_defer_status = (int)status;
    g_river_cloud.wake_admission_defer_wifi_connected = wifi_connected;
    g_river_cloud.wake_admission_defer_time_ready = time_ready;
    RIVER_LOGI("wake admission deferred: provider=%s status=%d wifi=%s admission_time_ready=%s system_time_ready=%s",
               river_cloud_asr_provider_name(),
               (int)status,
               river_wifi_station_status_name(),
               time_ready ? "yes" : "no",
               river_cloud_time_ready() ? "yes" : "no");
}

void river_cloud_reset_wake_admission_deferred_state(void)
{
    g_river_cloud.wake_admission_defer_status = 0;
    g_river_cloud.wake_admission_defer_wifi_connected = false;
    g_river_cloud.wake_admission_defer_time_ready = false;
}

static void river_cloud_notify_result(const river_cloud_asr_result_t *result,
                                      void *user_data)
{
    (void)user_data;

    if (result == NULL) {
        return;
    }

    if (result->type == RIVER_CLOUD_ASR_EVENT_PARTIAL) {
        g_river_cloud.partial_results++;
    } else if (result->type == RIVER_CLOUD_ASR_EVENT_FINAL) {
        g_river_cloud.final_results++;
    } else if (result->type == RIVER_CLOUD_ASR_EVENT_ERROR) {
        g_river_cloud.error_results++;
    }

    if (result->text != NULL) {
        snprintf(g_river_cloud.last_text,
                 sizeof(g_river_cloud.last_text),
                 "%s",
                 result->text);
    }
    if (result->message != NULL) {
        snprintf(g_river_cloud.last_error,
                 sizeof(g_river_cloud.last_error),
                 "%s",
                 result->message);
    }

    if (g_river_cloud.result_handler != NULL) {
        g_river_cloud.result_handler(result, g_river_cloud.result_handler_user);
    }
}

void river_cloud_request_state_sync(const char *reason)
{
    if (g_river_cloud.state_sync_handler != NULL) {
        g_river_cloud.state_sync_handler(reason, g_river_cloud.state_sync_handler_user);
    }
}

#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
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
#endif

static const char *river_cloud_split_asr_provider_name(void)
{
    if (g_river_cloud.provider == NULL) {
        return "disabled";
    }
    return g_river_cloud.provider->provider_name();
}

static bool river_cloud_xiaozhi_enabled(void)
{
#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    return g_river_cloud.xiaozhi_enabled;
#else
    return false;
#endif
}

static const char *river_cloud_active_provider_name(void)
{
    return river_cloud_xiaozhi_enabled() ?
               RIVER_CLOUD_XIAOZHI_PROVIDER_NAME :
               river_cloud_split_asr_provider_name();
}

static void river_cloud_runtime_copy_text(char *dst, size_t dst_size, const char *src)
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

#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
void river_cloud_emit_asr_result(river_cloud_asr_event_type_t type,
                                 const char *text,
                                 const char *sid,
                                 const char *message,
                                 int code,
                                 bool is_final)
{
    river_cloud_asr_result_t result;

    memset(&result, 0, sizeof(result));
    result.type = type;
    result.provider_name = river_cloud_active_provider_name();
    result.text = text;
    result.sid = sid;
    result.message = message;
    result.code = code;
    result.is_final = is_final;
    if (river_cloud_xiaozhi_enabled()) {
        river_cloud_xiaozhi_note_asr_result_emitted(type);
    }
    river_cloud_notify_result(&result, &g_river_cloud);
}
#endif

#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
static river_status_t river_cloud_xiaozhi_send_uplink_packet(const uint8_t *pcm,
                                                             size_t pcm_bytes)
{
    river_status_t status;

    if (pcm == NULL || pcm_bytes == 0U) {
        return RIVER_ERR_ARG;
    }

    status = river_xiaozhi_send_audio(pcm, pcm_bytes, g_river_cloud.xiaozhi_uplink_timestamp_ms);
    if (status == RIVER_OK) {
        g_river_cloud.xiaozhi_uplink_timestamp_ms +=
            RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS;
        river_cloud_xiaozhi_round_note_packet_sent();
    }
    return status;
}

static void river_cloud_xiaozhi_event_handler(const river_xiaozhi_event_t *event, void *user_data)
{
    (void)user_data;

    if (event == NULL) {
        return;
    }

    river_cloud_xiaozhi_refresh_turn_semantics("event");

    switch (event->type) {
    case RIVER_XIAOZHI_EVENT_SERVER_HELLO:
        g_river_cloud.xiaozhi_server_sample_rate =
            event->sample_rate != 0U ? event->sample_rate : 16000U;
        g_river_cloud.xiaozhi_server_frame_duration_ms =
            event->frame_duration_ms != 0U ? event->frame_duration_ms :
                                             RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS;
        river_cloud_xiaozhi_copy_session_id_from_transport();
        break;
    case RIVER_XIAOZHI_EVENT_STT:
        river_cloud_xiaozhi_note_stt_observation(event);
        break;
    case RIVER_XIAOZHI_EVENT_INPUT_SPEECH_START:
    case RIVER_XIAOZHI_EVENT_INPUT_PREVIEW:
    case RIVER_XIAOZHI_EVENT_INPUT_ENDPOINT:
        river_cloud_xiaozhi_note_input_observation(event);
        break;
    case RIVER_XIAOZHI_EVENT_AUDIO_OUT_META:
        river_cloud_xiaozhi_note_audio_out_meta_observation(event);
        break;
    case RIVER_XIAOZHI_EVENT_LLM:
        river_cloud_xiaozhi_note_llm_observation(event);
        break;
    case RIVER_XIAOZHI_EVENT_TTS:
        river_cloud_xiaozhi_note_tts_observation(event);
        break;
    case RIVER_XIAOZHI_EVENT_AUDIO:
        (void)river_cloud_xiaozhi_playback_handle_audio_event(event);
        break;
    case RIVER_XIAOZHI_EVENT_SESSION_CLOSED:
        river_cloud_xiaozhi_note_session_closed_observation(event);
        break;
    case RIVER_XIAOZHI_EVENT_ERROR:
        river_cloud_xiaozhi_note_error_observation(event);
        break;
    default:
        break;
    }
}
#endif

static void river_cloud_log_stream_open_deferred_once(river_status_t status)
{
    bool wifi_connected;
    bool time_ready;

    wifi_connected = river_wifi_station_is_connected();
    time_ready = river_cloud_time_ready();

    if ((g_river_cloud.stream_open_defer_status == status) &&
        (g_river_cloud.stream_open_defer_wifi_connected == wifi_connected) &&
        (g_river_cloud.stream_open_defer_time_ready == time_ready)) {
        return;
    }

    g_river_cloud.stream_open_defer_status = status;
    g_river_cloud.stream_open_defer_wifi_connected = wifi_connected;
    g_river_cloud.stream_open_defer_time_ready = time_ready;
    RIVER_LOGI("asr stream deferred: provider=%s status=%d wifi=%s time_ready=%s",
               river_cloud_asr_provider_name(),
               status,
               river_wifi_station_status_name(),
               time_ready ? "yes" : "no");
}

static void river_cloud_reset_stream_open_deferred_state(void)
{
    g_river_cloud.stream_open_defer_status = 0;
    g_river_cloud.stream_open_defer_wifi_connected = false;
    g_river_cloud.stream_open_defer_time_ready = false;
}

void river_cloud_pre_roll_reset(void)
{
    g_river_cloud.pre_roll_count_frames = 0U;
    g_river_cloud.pre_roll_write_index_frames = 0U;
}

static void river_cloud_pre_roll_store(const uint8_t *pcm)
{
    uint8_t *dst;

    if (g_river_cloud.pre_roll_buffer == NULL || pcm == NULL ||
        g_river_cloud.pre_roll_capacity_frames == 0U || g_river_cloud.frame_bytes == 0U) {
        return;
    }

    dst = g_river_cloud.pre_roll_buffer +
          ((size_t)g_river_cloud.pre_roll_write_index_frames * g_river_cloud.frame_bytes);
    memcpy(dst, pcm, g_river_cloud.frame_bytes);

    g_river_cloud.pre_roll_write_index_frames++;
    if (g_river_cloud.pre_roll_write_index_frames >= g_river_cloud.pre_roll_capacity_frames) {
        g_river_cloud.pre_roll_write_index_frames = 0U;
    }
    if (g_river_cloud.pre_roll_count_frames < g_river_cloud.pre_roll_capacity_frames) {
        g_river_cloud.pre_roll_count_frames++;
    }
}

static river_status_t river_cloud_stream_open_and_flush(void)
{
    uint32_t read_index;
    uint32_t frame_index;
    uint32_t pre_roll_frames;
    river_status_t status;

    if (g_river_cloud.provider == NULL || !g_river_cloud.provider->supports_streaming()) {
        return RIVER_ERR_UNSUPPORTED;
    }
    if (!river_wifi_station_is_connected()) {
        return RIVER_ERR_BUSY;
    }

    river_cloud_start_sntp_if_needed();
    river_cloud_seed_time_from_build_if_needed();
    if (!river_cloud_business_time_ready()) {
        snprintf(g_river_cloud.last_error,
                 sizeof(g_river_cloud.last_error),
                 "%s",
                 "system utc not ready");
        return RIVER_ERR_BUSY;
    }

    pre_roll_frames = g_river_cloud.pre_roll_count_frames;
    status = g_river_cloud.provider->stream_open(&g_river_cloud.audio_desc);
    if (status != RIVER_OK) {
        return status;
    }

    if (pre_roll_frames == 0U) {
        return RIVER_OK;
    }

    if (pre_roll_frames == g_river_cloud.pre_roll_capacity_frames) {
        read_index = g_river_cloud.pre_roll_write_index_frames;
    } else {
        read_index = 0U;
    }

    for (frame_index = 0U; frame_index < pre_roll_frames; ++frame_index) {
        const uint8_t *src = g_river_cloud.pre_roll_buffer +
                             ((size_t)read_index * g_river_cloud.frame_bytes);

        status = g_river_cloud.provider->stream_feed(src, g_river_cloud.frame_bytes);
        if (status != RIVER_OK) {
            return status;
        }

        read_index++;
        if (read_index >= g_river_cloud.pre_roll_capacity_frames) {
            read_index = 0U;
        }
    }

    river_cloud_pre_roll_reset();
    return RIVER_OK;
}

static river_status_t river_cloud_stream_finish_active(void)
{
    river_status_t status;

    if (!g_river_cloud.stream_active || g_river_cloud.provider == NULL) {
        return RIVER_OK;
    }

    status = g_river_cloud.provider->stream_finish();
    if (status == RIVER_OK) {
        g_river_cloud.stream_close_ok++;
    } else {
        g_river_cloud.stream_close_fail++;
    }
    g_river_cloud.stream_active = false;
    g_river_cloud.silence_frames = 0U;
    g_river_cloud.stream_started_ms = 0U;
    river_cloud_pre_roll_reset();
    river_runtime_stats_snapshot("asr_stream_finish");
    return status;
}

river_status_t river_cloud_adapter_init(void)
{
#if RIVER_CLOUD_BACKEND_IFLYTEK_ENABLED
    bool split_backend_ready = false;
#endif
#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    bool xiaozhi_backend_ready = false;
#endif

    if (g_river_cloud.initialized) {
        return RIVER_OK;
    }

    memset(&g_river_cloud, 0, sizeof(g_river_cloud));
#if RIVER_CLOUD_BACKEND_IFLYTEK_ENABLED
    g_river_cloud.provider = river_cloud_provider_default();
    if (g_river_cloud.provider == NULL) {
        RIVER_LOGE("no online asr provider registered");
        return RIVER_ERR_UNSUPPORTED;
    }
    if (g_river_cloud.provider->init(river_cloud_notify_result, &g_river_cloud) != RIVER_OK) {
        RIVER_LOGE("asr provider init failed");
        return RIVER_ERR_UNSUPPORTED;
    }
    split_backend_ready = true;
#else
    g_river_cloud.provider = NULL;
#endif

    river_cloud_start_sntp_if_needed();
    river_cloud_seed_time_from_build_if_needed();
#if RIVER_CLOUD_BACKEND_IFLYTEK_ENABLED
    RIVER_LOGI("online asr provider init: %s stream=%s batch=%s",
               river_cloud_asr_provider_name(),
               river_cloud_asr_streaming_supported() ? "yes" : "no",
               river_cloud_asr_batch_supported() ? "yes" : "no");
    if (river_tts_iflytek_init() != RIVER_OK) {
        RIVER_LOGE("tts provider init failed");
        return RIVER_ERR_UNSUPPORTED;
    }
    RIVER_LOGI("online tts provider init: iflytek_ws speak=yes scheme=ws");
#endif
#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    if (river_xiaozhi_init() == RIVER_OK) {
        xiaozhi_backend_ready = true;
        g_river_cloud.xiaozhi_enabled = river_xiaozhi_configured();
        (void)river_xiaozhi_set_event_handler(river_cloud_xiaozhi_event_handler, &g_river_cloud);
        river_cloud_xiaozhi_start_io_if_needed();
        river_cloud_xiaozhi_playback_start_downlink_if_needed();
        RIVER_LOGI("realtime session backend init: xiaozhi configured=%s",
                   g_river_cloud.xiaozhi_enabled ? "yes" : "no");
    } else {
        RIVER_LOGW("xiaozhi session backend init failed; keep fallback split cloud path only");
    }
    g_river_cloud.xiaozhi_server_sample_rate = 16000U;
    g_river_cloud.xiaozhi_server_frame_duration_ms =
        RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS;
#endif
#if RIVER_CLOUD_BACKEND_IFLYTEK_ENABLED && RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    if (!split_backend_ready && !xiaozhi_backend_ready) {
#elif RIVER_CLOUD_BACKEND_IFLYTEK_ENABLED
    if (!split_backend_ready) {
#elif RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    if (!xiaozhi_backend_ready) {
#endif
#if RIVER_CLOUD_BACKEND_IFLYTEK_ENABLED || RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
        RIVER_LOGE("no cloud backend initialized");
        return RIVER_ERR_UNSUPPORTED;
    }
#endif
    g_river_cloud.initialized = true;
    return RIVER_OK;
}

river_status_t river_cloud_adapter_set_result_handler(river_cloud_asr_result_handler_t handler,
                                                      void *user_data)
{
    g_river_cloud.result_handler = handler;
    g_river_cloud.result_handler_user = user_data;
    return RIVER_OK;
}

river_status_t river_cloud_adapter_set_state_sync_handler(
    river_cloud_state_sync_handler_t handler,
    void *user_data)
{
    g_river_cloud.state_sync_handler = handler;
    g_river_cloud.state_sync_handler_user = user_data;
    return RIVER_OK;
}

river_status_t river_cloud_adapter_set_xiaozhi_config(const river_xiaozhi_config_t *config)
{
#if !RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    (void)config;
    return RIVER_ERR_UNSUPPORTED;
#else
    river_status_t status;

    if (config == NULL) {
        return RIVER_ERR_ARG;
    }

    if (!g_river_cloud.initialized) {
        status = river_cloud_adapter_init();
        if (status != RIVER_OK) {
            return status;
        }
    }

    if (g_river_cloud.audio_bridge_open || g_river_cloud.stream_active ||
        river_cloud_xiaozhi_listening_active() || river_cloud_xiaozhi_playback_has_work() ||
        river_xiaozhi_session_open()) {
        return RIVER_ERR_BUSY;
    }

    status = river_xiaozhi_set_config(config);
    if (status != RIVER_OK) {
        return status;
    }

    g_river_cloud.xiaozhi_enabled = river_xiaozhi_configured();
    if (g_river_cloud.xiaozhi_enabled) {
        river_cloud_xiaozhi_start_io_if_needed();
        river_cloud_xiaozhi_playback_start_downlink_if_needed();
    }

    river_cloud_xiaozhi_clear_pending_text();
    g_river_cloud.xiaozhi_session_id[0] = '\0';
    river_cloud_xiaozhi_reset_playback_state();
    RIVER_LOGI("realtime session backend refresh: xiaozhi configured=%s",
               g_river_cloud.xiaozhi_enabled ? "yes" : "no");
    return RIVER_OK;
#endif
}

void river_cloud_adapter_notify_network_ready(void)
{
    if (!g_river_cloud.initialized) {
        return;
    }

    river_cloud_seed_time_from_build_if_needed();
    river_cloud_kick_sntp_on_network_ready();
    river_cloud_log_time_ready_once();
    river_cloud_reset_stream_open_deferred_state();
    river_cloud_reset_wake_admission_deferred_state();
}

void river_cloud_adapter_notify_network_lost(void)
{
    if (!g_river_cloud.initialized) {
        return;
    }

    river_cloud_reset_stream_open_deferred_state();
    river_cloud_reset_wake_admission_deferred_state();

#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    if (river_cloud_xiaozhi_enabled()) {
        river_cloud_xiaozhi_apply_network_lost_terminal_policy();
        river_cloud_request_state_sync("network_lost");
        return;
    }
#endif

    if (g_river_cloud.stream_active) {
        (void)river_cloud_stream_finish_active();
    }
}

river_status_t river_cloud_adapter_submit_text(const char *text)
{
#if RIVER_CLOUD_BACKEND_IFLYTEK_ENABLED
    river_status_t status;
#endif

    if (text == NULL) {
        return RIVER_ERR_ARG;
    }
    if (!g_river_cloud.initialized) {
        return RIVER_ERR_UNSUPPORTED;
    }
    if (!river_wifi_station_is_connected()) {
        return RIVER_ERR_BUSY;
    }
    if (!river_cloud_business_time_ready()) {
        return RIVER_ERR_BUSY;
    }

#if !RIVER_CLOUD_BACKEND_IFLYTEK_ENABLED
    return RIVER_ERR_UNSUPPORTED;
#else
    RIVER_LOGI("tts submit text=%s", text);
    river_runtime_stats_snapshot("tts_submit_start");
    status = river_tts_iflytek_submit_text(text);
    if (status == RIVER_OK) {
        river_runtime_stats_snapshot("tts_submit_finish");
    } else if (status == RIVER_ERR_BUSY) {
        river_runtime_stats_snapshot("tts_submit_interrupt");
    } else {
        river_runtime_stats_snapshot("tts_submit_fail");
    }
    return status;
#endif
}

river_status_t river_cloud_adapter_interrupt_tts_with_reason(const char *reason)
{
#if RIVER_CLOUD_BACKEND_IFLYTEK_ENABLED || RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    river_status_t status;
#endif

#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    if (river_cloud_xiaozhi_enabled() &&
        ((status = river_cloud_xiaozhi_interrupt_tts(reason)) != RIVER_ERR_UNSUPPORTED)) {
        return status;
    }
#endif

#if !RIVER_CLOUD_BACKEND_IFLYTEK_ENABLED
    (void)reason;
    return RIVER_ERR_UNSUPPORTED;
#else
    status = river_tts_iflytek_request_stop_with_reason(reason);
    if (status == RIVER_OK) {
        RIVER_LOGI("tts interrupt requested: reason=%s", reason != NULL ? reason : "-");
    }
    return status;
#endif
}

river_status_t river_cloud_adapter_interrupt_tts(void)
{
    return river_cloud_adapter_interrupt_tts_with_reason(NULL);
}

const char *river_cloud_asr_provider_name(void)
{
    return river_cloud_active_provider_name();
}

bool river_cloud_asr_streaming_supported(void)
{
#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    if (river_cloud_xiaozhi_enabled()) {
        return true;
    }
#endif
    return (g_river_cloud.provider != NULL) &&
           g_river_cloud.provider->supports_streaming();
}

bool river_cloud_asr_batch_supported(void)
{
#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    if (river_cloud_xiaozhi_enabled()) {
        return false;
    }
#endif
    return (g_river_cloud.provider != NULL) &&
           g_river_cloud.provider->supports_batch();
}

river_status_t river_cloud_asr_audio_open(const river_cloud_asr_audio_desc_t *audio,
                                          uint32_t pre_roll_ms,
                                          uint32_t post_roll_ms)
{
    size_t pre_roll_bytes;
#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    river_status_t status;
#endif

    if (audio == NULL || audio->sample_rate == 0U || audio->frame_ms == 0U ||
        audio->channels == 0U || audio->bits_per_sample == 0U) {
        return RIVER_ERR_ARG;
    }

    river_cloud_asr_audio_close();
    g_river_cloud.audio_desc = *audio;
    g_river_cloud.frame_bytes =
        (size_t)((audio->sample_rate * audio->frame_ms) / 1000U) *
        audio->channels * (audio->bits_per_sample / 8U);
    g_river_cloud.pre_roll_capacity_frames =
        river_cloud_ms_to_frames(pre_roll_ms, audio->frame_ms);
    g_river_cloud.post_roll_frames =
        river_cloud_ms_to_frames(post_roll_ms, audio->frame_ms);

    if (g_river_cloud.pre_roll_capacity_frames > 0U) {
        pre_roll_bytes = (size_t)g_river_cloud.pre_roll_capacity_frames * g_river_cloud.frame_bytes;
        g_river_cloud.pre_roll_buffer = (uint8_t *)rtos_mem_zmalloc((uint32_t)pre_roll_bytes);
        if (g_river_cloud.pre_roll_buffer == NULL) {
            river_cloud_asr_audio_close();
            return RIVER_ERR_NO_MEMORY;
        }
    }

#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    if (river_cloud_xiaozhi_enabled()) {
        uint32_t xiaozhi_pre_roll_frames =
            river_cloud_ms_to_frames(RIVER_CLOUD_XIAOZHI_PRE_ROLL_MAX_MS, audio->frame_ms);

        if (audio->sample_rate != RIVER_XIAOZHI_UPLINK_SAMPLE_RATE ||
            audio->channels != RIVER_XIAOZHI_UPLINK_CHANNELS ||
            audio->bits_per_sample != 16U) {
            river_cloud_asr_audio_close();
            return RIVER_ERR_UNSUPPORTED;
        }

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
                river_cloud_asr_audio_close();
                return status;
            }
        }
        river_audio_frame_ring_reset(&g_river_cloud.xiaozhi_uplink_ring);
        g_river_cloud.xiaozhi_uplink_ring_dropped = 0U;
        g_river_cloud.xiaozhi_uplink_busy_count = 0U;
        g_river_cloud.xiaozhi_uplink_fail_count = 0U;
        g_river_cloud.xiaozhi_uplink_stale_dropped = 0U;
        g_river_cloud.xiaozhi_uplink_timestamp_ms =
            (uint32_t)rtos_time_get_current_system_time_ms();
        g_river_cloud.xiaozhi_uplink_next_send_ms = 0U;
        g_river_cloud.xiaozhi_uplink_busy_streak = 0U;
        g_river_cloud.xiaozhi_uplink_last_busy_log_ms = 0U;
        g_river_cloud.xiaozhi_uplink_accum_bytes = 0U;
        g_river_cloud.xiaozhi_uplink_retry_valid = false;
        g_river_cloud.xiaozhi_open_speech_frames = 0U;
        RIVER_LOGI("xiaozhi uplink audio: %luHz/%luch frame=%lums pcm=%luB",
                   (unsigned long)audio->sample_rate,
                   (unsigned long)audio->channels,
                   (unsigned long)RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS,
                   (unsigned long)RIVER_CLOUD_XIAOZHI_UPLINK_PCM_FRAME_MAX);
        RIVER_LOGI("xiaozhi listen gate: pre_roll_cap=%lums open_hold_frames=%u",
                   (unsigned long)(g_river_cloud.pre_roll_capacity_frames * audio->frame_ms),
                   (unsigned int)RIVER_CLOUD_XIAOZHI_OPEN_HOLD_FRAMES);
    }
#endif

    g_river_cloud.audio_bridge_open = true;
    RIVER_LOGI("asr bridge open: provider=%s %luHz/%luch/%lubit frame=%lums pre=%ums post=%ums",
               river_cloud_asr_provider_name(),
               (unsigned long)audio->sample_rate,
               (unsigned long)audio->channels,
               (unsigned long)audio->bits_per_sample,
               (unsigned long)audio->frame_ms,
               (unsigned int)pre_roll_ms,
               (unsigned int)post_roll_ms);
    return RIVER_OK;
}

void river_cloud_asr_audio_close(void)
{
#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    if (river_cloud_xiaozhi_enabled()) {
        if (g_river_cloud.stream_active) {
            river_cloud_xiaozhi_complete_active_stream_finish(
                RIVER_CLOUD_XIAOZHI_STREAM_FINISH_POST_ROLL,
                "bridge_close");
        }
        river_cloud_xiaozhi_apply_bridge_close_terminal_policy();
        river_opus_encoder_close(&g_river_cloud.xiaozhi_encoder);
        river_opus_decoder_close(&g_river_cloud.xiaozhi_decoder);
    } else {
        river_cloud_stream_finish_active();
    }
#else
    river_cloud_stream_finish_active();
#endif

    if (g_river_cloud.provider != NULL && !river_cloud_xiaozhi_enabled()) {
        g_river_cloud.provider->stream_poll(0U);
    }

    if (g_river_cloud.pre_roll_buffer != NULL) {
        rtos_mem_free(g_river_cloud.pre_roll_buffer);
        g_river_cloud.pre_roll_buffer = NULL;
    }

    g_river_cloud.audio_bridge_open = false;
    g_river_cloud.stream_active = false;
    g_river_cloud.pre_roll_capacity_frames = 0U;
    g_river_cloud.post_roll_frames = 0U;
    g_river_cloud.frame_bytes = 0U;
    g_river_cloud.silence_frames = 0U;
#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    g_river_cloud.xiaozhi_open_speech_frames = 0U;
#endif
    river_cloud_pre_roll_reset();
    memset(&g_river_cloud.audio_desc, 0, sizeof(g_river_cloud.audio_desc));
}

#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
static river_status_t river_cloud_xiaozhi_stream_push_frame(const uint8_t *pcm,
                                                            size_t bytes,
                                                            bool is_speech)
{
    river_status_t status;
    river_voice_duplex_ready_eval_t duplex_eval;
    const char *duplex_fallback_reason;

    if (!g_river_cloud.audio_bridge_open || pcm == NULL || bytes != g_river_cloud.frame_bytes) {
        return RIVER_ERR_ARG;
    }

    river_cloud_xiaozhi_playback_check_pending_stop();
    /*
     * Follow-up window timeout can cascade into websocket/session teardown.
     * Keep that off the real-time capture path; the dedicated xiaozhi I/O
     * owner already polls timeout state continuously and owns transport work.
     */
    river_cloud_log_time_ready_once();

    river_cloud_xiaozhi_get_duplex_ready_eval(&duplex_eval);
    if (!g_river_cloud.stream_active &&
        river_cloud_xiaozhi_capture_held_by_playback(&duplex_eval,
                                                     &duplex_fallback_reason)) {
        river_cloud_xiaozhi_note_semantic_fallback(duplex_fallback_reason);
        RIVER_LOGI("xiaozhi capture held during playback: fallback=%s duplex_default_on=%s duplex_ready=%s reason=%s aec=%s ref_state=%s ref_activity=%s ref_peak=%u ref_ratio_q15=%u",
                   duplex_fallback_reason,
                   river_xiaozhi_duplex_default_on_allowed() ? "yes" : "no",
                   duplex_eval.ready ? "yes" : "no",
                   river_voice_runtime_duplex_ready_reason_name(duplex_eval.reason),
                   river_voice_runtime_aec_gate_reason_name(duplex_eval.aec_reason),
                   river_reference_service_state_name(duplex_eval.reference_state),
                   river_voice_runtime_reference_activity_name(duplex_eval.reference_activity),
                   (unsigned int)duplex_eval.native_reference_peak,
                   (unsigned int)duplex_eval.native_reference_ratio_q15);
        g_river_cloud.xiaozhi_open_speech_frames = 0U;
        river_cloud_pre_roll_reset();
        return RIVER_OK;
    }

    river_cloud_pre_roll_store(pcm);
    if (!g_river_cloud.stream_active) {
        bool followup_opened = false;
        uint32_t pre_roll_frames_before_open;
        uint32_t read_index;
        uint32_t frame_index;

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
        river_cloud_xiaozhi_playback_check_pending_stop();
        return RIVER_OK;
    }

    status = river_cloud_xiaozhi_push_pcm(pcm, bytes);
    if (status != RIVER_OK) {
        g_river_cloud.stream_feed_fail++;
        return status;
    }
    g_river_cloud.stream_feed_ok++;

    if (is_speech) {
        river_cloud_xiaozhi_cancel_endpoint_soft_close("speech_resumed");
        g_river_cloud.silence_frames = 0U;
    } else {
        g_river_cloud.silence_frames++;
        if (g_river_cloud.silence_frames >= g_river_cloud.post_roll_frames &&
            (rtos_time_get_current_system_time_ms() - g_river_cloud.stream_started_ms) >=
                RIVER_CLOUD_STREAM_MIN_ACTIVE_MS) {
            if (river_cloud_xiaozhi_duplex_soft_endpoint_enabled()) {
                river_cloud_xiaozhi_arm_endpoint_soft_close("post_roll",
                                                            "local_silence");
            } else {
                river_cloud_xiaozhi_complete_active_stream_finish(
                    RIVER_CLOUD_XIAOZHI_STREAM_FINISH_POST_ROLL,
                    "local_silence");
            }
        }
    }

    river_cloud_xiaozhi_playback_check_pending_stop();
    return RIVER_OK;
}
#endif

river_status_t river_cloud_asr_stream_push_frame(const uint8_t *pcm,
                                                 size_t bytes,
                                                 bool is_speech)
{
    river_status_t status;

    if (!g_river_cloud.audio_bridge_open || pcm == NULL || bytes != g_river_cloud.frame_bytes) {
        return RIVER_ERR_ARG;
    }
    if (!river_cloud_asr_streaming_supported()) {
        return RIVER_ERR_UNSUPPORTED;
    }

#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    if (river_cloud_xiaozhi_enabled()) {
        return river_cloud_xiaozhi_stream_push_frame(pcm, bytes, is_speech);
    }
#endif

    if (g_river_cloud.provider != NULL) {
        g_river_cloud.provider->stream_poll(0U);
    }

    river_cloud_log_time_ready_once();

    if (!g_river_cloud.stream_active) {
        river_cloud_pre_roll_store(pcm);
        if (!is_speech) {
            return RIVER_OK;
        }

        {
            uint32_t pre_roll_frames_before_open = g_river_cloud.pre_roll_count_frames;

            status = river_cloud_stream_open_and_flush();
            if (status != RIVER_OK) {
                g_river_cloud.stream_open_fail++;
                river_cloud_log_stream_open_deferred_once(status);
                return status;
            }

            /*
             * The current speech frame triggered stream activation. Feed it
             * immediately after opening so the provider sees real speech rather
             * than only historical pre-roll or silence.
             */
            status = g_river_cloud.provider->stream_feed(pcm, bytes);
            if (status == RIVER_ERR_BUSY) {
                return status;
            }
            if (status != RIVER_OK) {
                g_river_cloud.stream_feed_fail++;
                return status;
            }
            g_river_cloud.stream_feed_ok++;

            river_cloud_reset_stream_open_deferred_state();
            g_river_cloud.stream_active = true;
            g_river_cloud.stream_open_ok++;
            g_river_cloud.silence_frames = 0U;
            g_river_cloud.stream_started_ms = rtos_time_get_current_system_time_ms();
            RIVER_LOGI("asr stream active: provider=%s pre_roll_frames=%lu",
                       river_cloud_asr_provider_name(),
                       (unsigned long)pre_roll_frames_before_open);
            river_runtime_stats_snapshot("asr_stream_active");
            return RIVER_OK;
        }
    }

    status = g_river_cloud.provider->stream_feed(pcm, bytes);
    if (status == RIVER_ERR_BUSY) {
        return status;
    }
    if (status != RIVER_OK) {
        g_river_cloud.stream_feed_fail++;
        return status;
    }
    g_river_cloud.stream_feed_ok++;

    if (is_speech) {
        g_river_cloud.silence_frames = 0U;
    } else {
        g_river_cloud.silence_frames++;
        if (g_river_cloud.silence_frames >= g_river_cloud.post_roll_frames &&
            (rtos_time_get_current_system_time_ms() - g_river_cloud.stream_started_ms) >=
                RIVER_CLOUD_STREAM_MIN_ACTIVE_MS) {
            river_cloud_stream_finish_active();
        }
    }

    if (g_river_cloud.provider != NULL) {
        g_river_cloud.provider->stream_poll(0U);
    }
    return RIVER_OK;
}

river_status_t river_cloud_asr_batch_submit_segment(const uint8_t *pcm,
                                                    size_t bytes,
                                                    const river_voice_segment_desc_t *segment)
{
    river_status_t status;

    if (pcm == NULL || bytes == 0U || segment == NULL) {
        return RIVER_ERR_ARG;
    }
#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    if (river_cloud_xiaozhi_enabled()) {
        g_river_cloud.batch_submit_unsupported++;
        return RIVER_ERR_UNSUPPORTED;
    }
#endif
    if (g_river_cloud.provider == NULL) {
        return RIVER_ERR_UNSUPPORTED;
    }

    status = g_river_cloud.provider->batch_submit(pcm, bytes, segment);
    if (status == RIVER_OK) {
        g_river_cloud.batch_submit_ok++;
        return RIVER_OK;
    }
    if (status == RIVER_ERR_UNSUPPORTED) {
        g_river_cloud.batch_submit_unsupported++;
        return RIVER_ERR_UNSUPPORTED;
    }

    g_river_cloud.batch_submit_fail++;
    return status;
}

void river_cloud_adapter_dump_status(void)
{
    RIVER_LOGI("asr provider=%s stream=%s batch=%s wifi=%s bridge=%s time_ready=%s partial=%lu final=%lu err=%lu open_ok=%lu open_fail=%lu feed_ok=%lu feed_fail=%lu close_ok=%lu close_fail=%lu batch_ok=%lu batch_unsupported=%lu batch_fail=%lu last_text=%s last_err=%s",
               river_cloud_asr_provider_name(),
               river_cloud_asr_streaming_supported() ? "yes" : "no",
               river_cloud_asr_batch_supported() ? "yes" : "no",
               river_wifi_station_status_name(),
               g_river_cloud.audio_bridge_open ? "open" : "closed",
               river_cloud_time_ready() ? "yes" : "no",
               (unsigned long)g_river_cloud.partial_results,
               (unsigned long)g_river_cloud.final_results,
               (unsigned long)g_river_cloud.error_results,
               (unsigned long)g_river_cloud.stream_open_ok,
               (unsigned long)g_river_cloud.stream_open_fail,
               (unsigned long)g_river_cloud.stream_feed_ok,
               (unsigned long)g_river_cloud.stream_feed_fail,
               (unsigned long)g_river_cloud.stream_close_ok,
               (unsigned long)g_river_cloud.stream_close_fail,
               (unsigned long)g_river_cloud.batch_submit_ok,
               (unsigned long)g_river_cloud.batch_submit_unsupported,
               (unsigned long)g_river_cloud.batch_submit_fail,
               g_river_cloud.last_text[0] != '\0' ? g_river_cloud.last_text : "-",
               g_river_cloud.last_error[0] != '\0' ? g_river_cloud.last_error : "-");
#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    {
        uint64_t now_ms = (uint64_t)rtos_time_get_current_system_time_ms();

        river_cloud_xiaozhi_dump_session_status(now_ms);
        river_cloud_xiaozhi_dump_playback_status(now_ms);
        river_cloud_xiaozhi_dump_io_status();
    }
#else
    RIVER_LOGI("xiaozhi runtime compiled=no");
#endif
    if (g_river_cloud.provider != NULL) {
        g_river_cloud.provider->dump_status();
    }
#if RIVER_CLOUD_BACKEND_IFLYTEK_ENABLED
    river_tts_iflytek_dump_status();
#endif
#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    river_xiaozhi_dump_status();
#endif
}

river_status_t river_cloud_adapter_begin_conversation_window(const char *source)
{
#if !RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    (void)source;
    return RIVER_ERR_UNSUPPORTED;
#else
    return river_cloud_xiaozhi_begin_conversation_window(source);
#endif
}

bool river_cloud_adapter_conversation_window_active(void)
{
#if !RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    return false;
#else
    return g_river_cloud.xiaozhi_enabled &&
           river_cloud_xiaozhi_conversation_window_active();
#endif
}

river_status_t river_cloud_adapter_get_runtime_snapshot(river_cloud_runtime_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return RIVER_ERR_ARG;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->available = g_river_cloud.initialized;
    river_cloud_runtime_copy_text(snapshot->provider_name,
                                  sizeof(snapshot->provider_name),
                                  river_cloud_active_provider_name());
    snapshot->stream_active = g_river_cloud.stream_active;
    snapshot->listening = g_river_cloud.stream_active;

#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    if (river_cloud_xiaozhi_enabled()) {
        river_cloud_xiaozhi_fill_runtime_snapshot(snapshot);
    }
#endif

    return RIVER_OK;
}
