/* Orvibo application orchestration: owns business state and cross-thread events. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "os_wrapper.h"

#include "ameba_soc.h"

#include "river/river_log.h"
#include "river/river_orvibo_access.h"
#include "river/river_orvibo_app.h"
#include "river/river_orvibo_audio_service.h"
#include "river/river_orvibo_credentials.h"
#include "river/river_orvibo_mcp_volume.h"
#include "river/river_orvibo_protocol.h"
#include "river/river_orvibo_state.h"
#include "river/river_orvibo_ui.h"
#include "river/river_playback_service.h"
#include "river/river_reference_service.h"
#include "river/river_runtime_stats.h"
#include "river/river_voice_board.h"
#include "river/river_voice_capture.h"
#include "river/river_voice_detector.h"
#include "river/river_voice_kws.h"
#include "river/river_voice_preproc.h"
#include "river/river_voice_profile.h"
#include "river/river_wifi_station.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.orvibo.app"

#define RIVER_ORVIBO_APP_TASK_STACK        (1024U * 12U)
#define RIVER_ORVIBO_APP_TASK_PRIORITY     5U
#define RIVER_ORVIBO_APP_CONTROL_QUEUE_DEPTH 24U
#define RIVER_ORVIBO_APP_AUDIO_QUEUE_DEPTH 64U
#define RIVER_ORVIBO_APP_AUDIO_BUDGET_PER_TICK 16U
#define RIVER_ORVIBO_APP_POLL_MS           20U
#define RIVER_ORVIBO_APP_WIFI_LOG_MS       3000U
#define RIVER_ORVIBO_APP_ACCESS_RETRY_MS   10000U
#define RIVER_ORVIBO_APP_TTS_DRAIN_MS      3000U
#define RIVER_ORVIBO_APP_AUDIO_PACKET_MAX  1536U
#define RIVER_ORVIBO_APP_SERVER_TEXT_MAX   192U
#define RIVER_ORVIBO_APP_SERVER_TEXT_DETAIL_MAX 48U
#define RIVER_ORVIBO_APP_CONNECT_BACKOFF_MIN_MS 1000U
#define RIVER_ORVIBO_APP_CONNECT_BACKOFF_MAX_MS 30000U
#define RIVER_ORVIBO_APP_CONNECT_BACKOFF_STREAK_CAP 6U
#define RIVER_ORVIBO_RTOS_OK               0

typedef enum {
    RIVER_ORVIBO_APP_MSG_STATE_EVENT = 0,
    RIVER_ORVIBO_APP_MSG_AUDIO_UPLINK,
    RIVER_ORVIBO_APP_MSG_DOWNLINK_AUDIO,
    RIVER_ORVIBO_APP_MSG_SERVER_TEXT,
    RIVER_ORVIBO_APP_MSG_CONNECT,
    RIVER_ORVIBO_APP_MSG_ACCESS_REFRESH,
    RIVER_ORVIBO_APP_MSG_LISTEN_START,
    RIVER_ORVIBO_APP_MSG_LISTEN_STOP,
    RIVER_ORVIBO_APP_MSG_ABORT
} river_orvibo_app_msg_type_t;

typedef enum {
    RIVER_ORVIBO_APP_SERVER_TEXT_SENTENCE_START = 0,
    RIVER_ORVIBO_APP_SERVER_TEXT_STT,
    RIVER_ORVIBO_APP_SERVER_TEXT_LLM
} river_orvibo_app_server_text_kind_t;

typedef struct {
    river_orvibo_app_msg_type_t type;
    river_orvibo_event_t event;
    river_orvibo_app_server_text_kind_t server_text_kind;
    char reason[48];
    char text[RIVER_ORVIBO_APP_SERVER_TEXT_MAX];
    char detail[RIVER_ORVIBO_APP_SERVER_TEXT_DETAIL_MAX];
    uint32_t timestamp_ms;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t frame_duration_ms;
    size_t bytes;
    uint8_t data[RIVER_ORVIBO_APP_AUDIO_PACKET_MAX];
} river_orvibo_app_msg_t;

typedef struct {
    bool booted;
    bool task_running;
    bool wifi_ready_reported;
    bool connect_retry_pending;
    rtos_task_t task;
    rtos_queue_t control_queue;
    rtos_queue_t audio_queue;
    uint32_t posted;
    uint32_t post_fail;
    uint32_t control_posted;
    uint32_t control_post_fail;
    uint32_t audio_posted;
    uint32_t audio_post_fail;
    uint32_t audio_drop_oldest;
    uint32_t audio_queue_flushes;
    uint32_t handled;
    uint32_t uplink_sent;
    uint32_t uplink_busy;
    uint32_t uplink_oversize;
    uint32_t downlink_ok;
    uint32_t downlink_fail;
    uint32_t downlink_dropped;
    uint32_t downlink_oversize;
    uint32_t access_refresh_ok;
    uint32_t access_refresh_fail;
    uint32_t protocol_control_ok;
    uint32_t protocol_control_fail;
    uint32_t protocol_control_skip;
    uint32_t connect_open_ok;
    uint32_t connect_open_fail;
    uint32_t connect_fail_streak;
    uint32_t connect_retry_posted;
    uint32_t connect_backoff_suppressed;
    uint32_t next_connect_retry_ms;
    uint32_t last_wifi_log_ms;
    uint32_t last_access_retry_ms;
    char wake_text[64];
    char last_event[48];
    char last_server_text_kind[32];
    char last_server_text[RIVER_ORVIBO_APP_SERVER_TEXT_MAX];
    char last_server_detail[RIVER_ORVIBO_APP_SERVER_TEXT_DETAIL_MAX];
    char last_error[96];
} river_orvibo_app_context_t;

static river_orvibo_app_context_t g_river_orvibo_app;

static void river_orvibo_app_copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0U) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_size - 1U);
    dst[dst_size - 1U] = '\0';
}

static bool river_orvibo_app_msg_is_audio(const river_orvibo_app_msg_t *msg)
{
    if (msg == NULL) {
        return false;
    }
    if (msg->type == RIVER_ORVIBO_APP_MSG_AUDIO_UPLINK ||
        msg->type == RIVER_ORVIBO_APP_MSG_DOWNLINK_AUDIO) {
        return true;
    }
    if (msg->type == RIVER_ORVIBO_APP_MSG_STATE_EVENT &&
        (msg->event == RIVER_ORVIBO_EVENT_SERVER_TTS_STARTED ||
         msg->event == RIVER_ORVIBO_EVENT_SERVER_TTS_FINISHED)) {
        return true;
    }
    return false;
}

static const char *river_orvibo_app_server_text_kind_name(
    river_orvibo_app_server_text_kind_t kind)
{
    switch (kind) {
    case RIVER_ORVIBO_APP_SERVER_TEXT_SENTENCE_START:
        return "server_sentence_start";
    case RIVER_ORVIBO_APP_SERVER_TEXT_STT:
        return "server_stt";
    case RIVER_ORVIBO_APP_SERVER_TEXT_LLM:
        return "server_llm";
    default:
        return "server_text";
    }
}

static river_status_t river_orvibo_app_post_audio(const river_orvibo_app_msg_t *msg)
{
    river_orvibo_app_msg_t dropped;

    if (msg == NULL || g_river_orvibo_app.audio_queue == NULL) {
        return RIVER_ERR_INVALID_STATE;
    }
    if (rtos_queue_send(g_river_orvibo_app.audio_queue, (void *)msg, 0U) ==
        RIVER_ORVIBO_RTOS_OK) {
        g_river_orvibo_app.audio_posted++;
        g_river_orvibo_app.posted++;
        return RIVER_OK;
    }

    if (rtos_queue_receive(g_river_orvibo_app.audio_queue, &dropped, 0U) ==
        RIVER_ORVIBO_RTOS_OK) {
        g_river_orvibo_app.audio_drop_oldest++;
        if (rtos_queue_send(g_river_orvibo_app.audio_queue, (void *)msg, 0U) ==
            RIVER_ORVIBO_RTOS_OK) {
            g_river_orvibo_app.audio_posted++;
            g_river_orvibo_app.posted++;
            return RIVER_OK;
        }
    }

    g_river_orvibo_app.audio_post_fail++;
    g_river_orvibo_app.post_fail++;
    return RIVER_ERR_BUSY;
}

static river_status_t river_orvibo_app_post_control(const river_orvibo_app_msg_t *msg)
{
    if (msg == NULL || g_river_orvibo_app.control_queue == NULL) {
        return RIVER_ERR_INVALID_STATE;
    }
    if (rtos_queue_send(g_river_orvibo_app.control_queue, (void *)msg, 0U) !=
        RIVER_ORVIBO_RTOS_OK) {
        g_river_orvibo_app.control_post_fail++;
        g_river_orvibo_app.post_fail++;
        return RIVER_ERR_BUSY;
    }
    g_river_orvibo_app.control_posted++;
    g_river_orvibo_app.posted++;
    return RIVER_OK;
}

static void river_orvibo_app_clear_audio_queue(const char *reason)
{
    river_orvibo_app_msg_t dropped;
    uint32_t drained = 0U;

    if (g_river_orvibo_app.audio_queue == NULL) {
        return;
    }

    while (rtos_queue_receive(g_river_orvibo_app.audio_queue, &dropped, 0U) ==
           RIVER_ORVIBO_RTOS_OK) {
        drained++;
    }

    if (drained == 0U) {
        return;
    }

    g_river_orvibo_app.audio_queue_flushes++;
    RIVER_LOGI("audio queue cleared: reason=%s frames=%lu",
               reason != NULL ? reason : "-",
               (unsigned long)drained);
}

static river_status_t river_orvibo_app_post(const river_orvibo_app_msg_t *msg)
{
    if (river_orvibo_app_msg_is_audio(msg)) {
        return river_orvibo_app_post_audio(msg);
    }
    return river_orvibo_app_post_control(msg);
}

static void river_orvibo_app_post_state(river_orvibo_event_t event, const char *reason)
{
    river_orvibo_app_msg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.type = RIVER_ORVIBO_APP_MSG_STATE_EVENT;
    msg.event = event;
    river_orvibo_app_copy_text(msg.reason, sizeof(msg.reason), reason);
    (void)river_orvibo_app_post(&msg);
}

static bool river_orvibo_app_time_reached(uint32_t now_ms, uint32_t target_ms)
{
    return (int32_t)(now_ms - target_ms) >= 0;
}

static uint32_t river_orvibo_app_connect_backoff_ms(uint32_t fail_streak)
{
    uint32_t backoff_ms = RIVER_ORVIBO_APP_CONNECT_BACKOFF_MIN_MS;
    uint32_t steps = fail_streak > 0U ? fail_streak - 1U : 0U;

    while (steps > 0U && backoff_ms < RIVER_ORVIBO_APP_CONNECT_BACKOFF_MAX_MS) {
        backoff_ms *= 2U;
        if (backoff_ms > RIVER_ORVIBO_APP_CONNECT_BACKOFF_MAX_MS) {
            backoff_ms = RIVER_ORVIBO_APP_CONNECT_BACKOFF_MAX_MS;
        }
        steps--;
    }
    return backoff_ms;
}

static void river_orvibo_app_clear_connect_retry(const char *reason)
{
    if (g_river_orvibo_app.connect_retry_pending ||
        g_river_orvibo_app.connect_fail_streak != 0U) {
        RIVER_LOGI("connect retry cleared: reason=%s streak=%lu",
                   reason != NULL ? reason : "-",
                   (unsigned long)g_river_orvibo_app.connect_fail_streak);
    }
    g_river_orvibo_app.connect_retry_pending = false;
    g_river_orvibo_app.connect_fail_streak = 0U;
    g_river_orvibo_app.next_connect_retry_ms = 0U;
}

static void river_orvibo_app_schedule_connect_retry(const char *reason, river_status_t status)
{
    uint32_t now_ms = (uint32_t)rtos_time_get_current_system_time_ms();
    uint32_t backoff_ms;

    g_river_orvibo_app.connect_open_fail++;
    if (g_river_orvibo_app.connect_fail_streak <
        RIVER_ORVIBO_APP_CONNECT_BACKOFF_STREAK_CAP) {
        g_river_orvibo_app.connect_fail_streak++;
    }
    backoff_ms = river_orvibo_app_connect_backoff_ms(
        g_river_orvibo_app.connect_fail_streak);
    g_river_orvibo_app.connect_retry_pending = true;
    g_river_orvibo_app.next_connect_retry_ms = now_ms + backoff_ms;
    snprintf(g_river_orvibo_app.last_error,
             sizeof(g_river_orvibo_app.last_error),
             "%s:%d",
             reason != NULL ? reason : "connect",
             (int)status);
    RIVER_LOGW("connect retry scheduled: reason=%s status=%d streak=%lu backoff=%lums",
               reason != NULL ? reason : "-",
               (int)status,
               (unsigned long)g_river_orvibo_app.connect_fail_streak,
               (unsigned long)backoff_ms);
}

static void river_orvibo_audio_event_handler(const river_orvibo_audio_event_t *event,
                                             void *user_data)
{
    river_orvibo_app_msg_t msg;

    (void)user_data;
    if (event == NULL) {
        return;
    }
    memset(&msg, 0, sizeof(msg));
    switch (event->type) {
    case RIVER_ORVIBO_AUDIO_EVENT_WAKE_DETECTED:
        msg.type = RIVER_ORVIBO_APP_MSG_STATE_EVENT;
        msg.event = RIVER_ORVIBO_EVENT_WAKE_DETECTED;
        river_orvibo_app_copy_text(msg.reason,
                                   sizeof(msg.reason),
                                   event->text != NULL && event->text[0] != '\0' ?
                                       event->text :
                                       "wake_word");
        break;
    case RIVER_ORVIBO_AUDIO_EVENT_SPEECH_STARTED:
        msg.type = RIVER_ORVIBO_APP_MSG_STATE_EVENT;
        msg.event = RIVER_ORVIBO_EVENT_USER_SPEECH_STARTED;
        river_orvibo_app_copy_text(msg.reason, sizeof(msg.reason), "vad_start");
        break;
    case RIVER_ORVIBO_AUDIO_EVENT_SPEECH_ENDED:
        msg.type = RIVER_ORVIBO_APP_MSG_STATE_EVENT;
        msg.event = RIVER_ORVIBO_EVENT_USER_SPEECH_ENDED;
        river_orvibo_app_copy_text(msg.reason, sizeof(msg.reason), "vad_end");
        break;
    case RIVER_ORVIBO_AUDIO_EVENT_UPLINK_PACKET:
        if (event->packet == NULL || event->packet_bytes == 0U) {
            return;
        }
        if (event->packet_bytes > sizeof(msg.data)) {
            g_river_orvibo_app.uplink_oversize++;
            RIVER_LOGW("drop oversize uplink opus packet: bytes=%lu max=%lu",
                       (unsigned long)event->packet_bytes,
                       (unsigned long)sizeof(msg.data));
            return;
        }
        msg.type = RIVER_ORVIBO_APP_MSG_AUDIO_UPLINK;
        msg.timestamp_ms = event->timestamp_ms;
        msg.bytes = event->packet_bytes;
        memcpy(msg.data, event->packet, event->packet_bytes);
        break;
    case RIVER_ORVIBO_AUDIO_EVENT_ERROR:
        msg.type = RIVER_ORVIBO_APP_MSG_STATE_EVENT;
        msg.event = RIVER_ORVIBO_EVENT_ERROR_RECOVERABLE;
        river_orvibo_app_copy_text(msg.reason, sizeof(msg.reason), "audio_error");
        break;
    default:
        return;
    }
    (void)river_orvibo_app_post(&msg);
}

static void river_orvibo_protocol_event_handler(const river_orvibo_protocol_event_t *event,
                                                void *user_data)
{
    river_orvibo_app_msg_t msg;

    (void)user_data;
    if (event == NULL) {
        return;
    }
    memset(&msg, 0, sizeof(msg));
    switch (event->type) {
    case RIVER_ORVIBO_PROTOCOL_EVENT_AUDIO_CHANNEL_OPENED:
        msg.type = RIVER_ORVIBO_APP_MSG_STATE_EVENT;
        msg.event = RIVER_ORVIBO_EVENT_AUDIO_CHANNEL_OPENED;
        river_orvibo_app_copy_text(msg.reason, sizeof(msg.reason), "audio_channel_opened");
        break;
    case RIVER_ORVIBO_PROTOCOL_EVENT_SERVER_HELLO:
        msg.type = RIVER_ORVIBO_APP_MSG_STATE_EVENT;
        msg.event = RIVER_ORVIBO_EVENT_SERVER_HELLO;
        river_orvibo_app_copy_text(msg.reason, sizeof(msg.reason), "server_hello");
        break;
    case RIVER_ORVIBO_PROTOCOL_EVENT_AUDIO_CHANNEL_CLOSED:
        msg.type = RIVER_ORVIBO_APP_MSG_STATE_EVENT;
        msg.event = RIVER_ORVIBO_EVENT_AUDIO_CHANNEL_CLOSED;
        river_orvibo_app_copy_text(msg.reason, sizeof(msg.reason), "channel_closed");
        break;
    case RIVER_ORVIBO_PROTOCOL_EVENT_TTS_START:
        msg.type = RIVER_ORVIBO_APP_MSG_STATE_EVENT;
        msg.event = RIVER_ORVIBO_EVENT_SERVER_TTS_STARTED;
        river_orvibo_app_copy_text(msg.reason, sizeof(msg.reason), "tts_start");
        break;
    case RIVER_ORVIBO_PROTOCOL_EVENT_TTS_STOP:
        msg.type = RIVER_ORVIBO_APP_MSG_STATE_EVENT;
        msg.event = RIVER_ORVIBO_EVENT_SERVER_TTS_FINISHED;
        river_orvibo_app_copy_text(msg.reason, sizeof(msg.reason), "tts_stop");
        break;
    case RIVER_ORVIBO_PROTOCOL_EVENT_TTS_SENTENCE_START:
        msg.type = RIVER_ORVIBO_APP_MSG_SERVER_TEXT;
        msg.server_text_kind = RIVER_ORVIBO_APP_SERVER_TEXT_SENTENCE_START;
        river_orvibo_app_copy_text(msg.text, sizeof(msg.text), event->text);
        break;
    case RIVER_ORVIBO_PROTOCOL_EVENT_STT_TEXT:
        msg.type = RIVER_ORVIBO_APP_MSG_SERVER_TEXT;
        msg.server_text_kind = RIVER_ORVIBO_APP_SERVER_TEXT_STT;
        river_orvibo_app_copy_text(msg.text, sizeof(msg.text), event->text);
        break;
    case RIVER_ORVIBO_PROTOCOL_EVENT_LLM_EMOTION:
        msg.type = RIVER_ORVIBO_APP_MSG_SERVER_TEXT;
        msg.server_text_kind = RIVER_ORVIBO_APP_SERVER_TEXT_LLM;
        river_orvibo_app_copy_text(msg.text, sizeof(msg.text), event->text);
        river_orvibo_app_copy_text(msg.detail, sizeof(msg.detail), event->state);
        break;
    case RIVER_ORVIBO_PROTOCOL_EVENT_AUDIO_PACKET:
        if (event->audio_data == NULL || event->audio_bytes == 0U) {
            return;
        }
        if (event->audio_bytes > sizeof(msg.data)) {
            g_river_orvibo_app.downlink_oversize++;
            RIVER_LOGW("drop oversize downlink opus packet: bytes=%lu max=%lu",
                       (unsigned long)event->audio_bytes,
                       (unsigned long)sizeof(msg.data));
            return;
        }
        msg.type = RIVER_ORVIBO_APP_MSG_DOWNLINK_AUDIO;
        msg.sample_rate = event->sample_rate;
        msg.channels = event->channels != 0U ? event->channels : 1U;
        msg.frame_duration_ms = event->frame_duration_ms;
        msg.timestamp_ms = event->timestamp_ms;
        msg.bytes = event->audio_bytes;
        memcpy(msg.data, event->audio_data, event->audio_bytes);
        break;
    case RIVER_ORVIBO_PROTOCOL_EVENT_ERROR:
        msg.type = RIVER_ORVIBO_APP_MSG_STATE_EVENT;
        msg.event = RIVER_ORVIBO_EVENT_ERROR_RECOVERABLE;
        river_orvibo_app_copy_text(msg.reason, sizeof(msg.reason), "protocol_error");
        break;
    case RIVER_ORVIBO_PROTOCOL_EVENT_REBOOT_REQUEST:
        RIVER_LOGW("server requested reboot");
        System_Reset();
        return;
    default:
        return;
    }
    (void)river_orvibo_app_post(&msg);
}

static river_status_t river_orvibo_app_refresh_access(const char *reason);

static bool river_orvibo_app_access_refresh_retry_allowed(void)
{
    river_orvibo_access_status_t access_status;

    if (river_orvibo_access_get_status(&access_status) != RIVER_OK) {
        return false;
    }
    return access_status.used_ota;
}

static const char *river_orvibo_app_listen_mode(void)
{
    river_voice_preproc_profile_t profile = river_voice_profile_active_preproc();

    if (river_voice_profile_has_capability(profile, RIVER_VOICE_CAPABILITY_AEC) ||
        river_voice_profile_has_capability(profile,
                                           RIVER_VOICE_CAPABILITY_NATIVE_CAPTURE_REF)) {
        return "realtime";
    }
    return "auto";
}

static const char *river_orvibo_app_server_wake_text(void)
{
    if (RIVER_ORVIBO_SERVER_WAKE_TEXT[0] != '\0') {
        return RIVER_ORVIBO_SERVER_WAKE_TEXT;
    }
    return "你好小智";
}

static void river_orvibo_app_mark_protocol_control_skipped(const char *action,
                                                           const char *reason)
{
    const char *action_name = action != NULL ? action : "protocol_control";
    const char *skip_reason = reason != NULL ? reason : "channel_closed";
    river_orvibo_state_t state = river_orvibo_state_machine_current();

    g_river_orvibo_app.protocol_control_skip++;
    snprintf(g_river_orvibo_app.last_error,
             sizeof(g_river_orvibo_app.last_error),
             "%s:%s",
             action_name,
             skip_reason);
    RIVER_LOGW("skip protocol control: action=%s state=%s reason=%s",
               action_name,
               river_orvibo_state_name(state),
               skip_reason);
    if (state == RIVER_ORVIBO_STATE_CONNECTING ||
        state == RIVER_ORVIBO_STATE_LISTENING ||
        state == RIVER_ORVIBO_STATE_SPEAKING) {
        river_orvibo_app_post_state(RIVER_ORVIBO_EVENT_AUDIO_CHANNEL_CLOSED,
                                    "control_channel_closed");
    }
}

static void river_orvibo_app_record_protocol_control(const char *action,
                                                     river_status_t status,
                                                     bool recover_on_fail)
{
    const char *action_name = action != NULL ? action : "protocol_control";

    if (status == RIVER_OK) {
        g_river_orvibo_app.protocol_control_ok++;
        return;
    }

    if (status == RIVER_ERR_BUSY && !river_orvibo_protocol_audio_channel_open()) {
        river_orvibo_app_mark_protocol_control_skipped(action, "send_channel_closed");
        return;
    }

    g_river_orvibo_app.protocol_control_fail++;
    snprintf(g_river_orvibo_app.last_error,
             sizeof(g_river_orvibo_app.last_error),
             "%s:%d",
             action_name,
             (int)status);
    RIVER_LOGW("protocol control failed: action=%s status=%d recover=%s",
               action_name,
               (int)status,
               recover_on_fail ? "yes" : "no");
    if (recover_on_fail &&
        river_orvibo_state_machine_current() != RIVER_ORVIBO_STATE_RECOVERING &&
        river_orvibo_state_machine_current() != RIVER_ORVIBO_STATE_ERROR) {
        river_orvibo_app_post_state(RIVER_ORVIBO_EVENT_ERROR_RECOVERABLE, action_name);
    }
}

static bool river_orvibo_app_protocol_control_channel_open(const char *action)
{
    if (river_orvibo_protocol_audio_channel_open()) {
        return true;
    }

    river_orvibo_app_mark_protocol_control_skipped(action, "channel_closed");
    return false;
}

static void river_orvibo_app_apply_actions(uint32_t actions)
{
    if ((actions & RIVER_ORVIBO_ACTION_PREPARE_TTS_PLAYBACK) != 0U) {
        river_orvibo_audio_service_prepare_tts_playback();
    }
    if ((actions & RIVER_ORVIBO_ACTION_STOP_PLAYBACK) != 0U) {
        river_orvibo_audio_service_stop_playback("orvibo_state_action");
    }
    if ((actions & RIVER_ORVIBO_ACTION_WAIT_PLAYBACK_IDLE) != 0U) {
        (void)river_orvibo_audio_service_wait_playback_idle(RIVER_ORVIBO_APP_TTS_DRAIN_MS);
    }
    if ((actions & RIVER_ORVIBO_ACTION_AUDIO_IDLE) != 0U) {
        (void)river_orvibo_audio_service_set_mode(RIVER_ORVIBO_AUDIO_MODE_IDLE);
    }
    if ((actions & RIVER_ORVIBO_ACTION_AUDIO_LISTENING) != 0U) {
        (void)river_orvibo_audio_service_set_mode(RIVER_ORVIBO_AUDIO_MODE_LISTENING);
    }
    if ((actions & RIVER_ORVIBO_ACTION_AUDIO_SPEAKING) != 0U) {
        (void)river_orvibo_audio_service_set_mode(RIVER_ORVIBO_AUDIO_MODE_SPEAKING);
    }
    if ((actions & RIVER_ORVIBO_ACTION_ENABLE_BARGE_IN) != 0U) {
        river_orvibo_audio_service_set_barge_in_enabled(true);
    }
    if ((actions & RIVER_ORVIBO_ACTION_DISABLE_BARGE_IN) != 0U) {
        river_orvibo_audio_service_set_barge_in_enabled(false);
    }
    if ((actions & RIVER_ORVIBO_ACTION_OPEN_AUDIO_CHANNEL) != 0U) {
        river_status_t status = river_orvibo_access_ready() ?
                                    RIVER_OK :
                                    river_orvibo_app_refresh_access("open_audio_channel");
        uint32_t now_ms = (uint32_t)rtos_time_get_current_system_time_ms();
        bool backoff_suppressed = false;
        bool refreshed_after_open_fail = false;

        if (g_river_orvibo_app.connect_retry_pending &&
            g_river_orvibo_app.next_connect_retry_ms != 0U &&
            !river_orvibo_app_time_reached(now_ms,
                                           g_river_orvibo_app.next_connect_retry_ms)) {
            uint32_t wait_ms = g_river_orvibo_app.next_connect_retry_ms - now_ms;
            g_river_orvibo_app.connect_backoff_suppressed++;
            snprintf(g_river_orvibo_app.last_error,
                     sizeof(g_river_orvibo_app.last_error),
                     "connect_backoff:%lu",
                     (unsigned long)wait_ms);
            RIVER_LOGW("connect suppressed by backoff: wait=%lums streak=%lu",
                       (unsigned long)wait_ms,
                       (unsigned long)g_river_orvibo_app.connect_fail_streak);
            river_orvibo_app_post_state(RIVER_ORVIBO_EVENT_ERROR_RECOVERABLE,
                                        "connect_backoff");
            backoff_suppressed = true;
            status = RIVER_ERR_BUSY;
        }
        if (status == RIVER_OK) {
            status = river_orvibo_protocol_open_audio_channel();
            if (status != RIVER_OK &&
                river_orvibo_app_access_refresh_retry_allowed()) {
                river_status_t refresh_status =
                    river_orvibo_app_refresh_access("open_audio_channel_retry");

                if (refresh_status == RIVER_OK) {
                    refreshed_after_open_fail = true;
                    RIVER_LOGW("retry open audio channel after OTA websocket refresh");
                    status = river_orvibo_protocol_open_audio_channel();
                }
            }
        }
        if (status != RIVER_OK) {
            if (!backoff_suppressed) {
                river_orvibo_app_schedule_connect_retry("open_audio_channel", status);
                river_orvibo_app_post_state(RIVER_ORVIBO_EVENT_ERROR_RECOVERABLE,
                                            "open_audio_channel_failed");
            }
        } else {
            g_river_orvibo_app.connect_open_ok++;
            river_orvibo_app_clear_connect_retry("open_audio_channel_ok");
            if (refreshed_after_open_fail) {
                RIVER_LOGI("open audio channel recovered after access refresh");
            }
        }
    }
    if ((actions & RIVER_ORVIBO_ACTION_CLOSE_AUDIO_CHANNEL) != 0U) {
        river_orvibo_protocol_close_audio_channel();
    }
    if ((actions & RIVER_ORVIBO_ACTION_SEND_WAKE_DETECTED) != 0U) {
        if (river_orvibo_app_protocol_control_channel_open("send_wake_detected")) {
            river_status_t status = river_orvibo_protocol_send_wake_word_detected(
                river_orvibo_app_server_wake_text());
            RIVER_LOGI("server wake detect text=%s local_wake=%s",
                       river_orvibo_app_server_wake_text(),
                       g_river_orvibo_app.wake_text[0] != '\0' ?
                           g_river_orvibo_app.wake_text :
                           "-");
            river_orvibo_app_record_protocol_control("send_wake_detected", status, true);
        }
    }
    if ((actions & RIVER_ORVIBO_ACTION_STOP_LISTENING) != 0U) {
        if (river_orvibo_app_protocol_control_channel_open("listen_stop")) {
            river_status_t status = river_orvibo_protocol_send_stop_listening();
            river_orvibo_app_record_protocol_control("listen_stop", status, true);
        }
    }
    if ((actions & RIVER_ORVIBO_ACTION_ABORT_WAKE_WORD) != 0U) {
        if (river_orvibo_app_protocol_control_channel_open("abort_wake_word")) {
            river_status_t status =
                river_orvibo_protocol_send_abort_speaking("wake_word_detected");
            river_orvibo_app_record_protocol_control("abort_wake_word", status, true);
        }
    }
    if ((actions & RIVER_ORVIBO_ACTION_ABORT_SPEAKING) != 0U) {
        if (river_orvibo_app_protocol_control_channel_open("abort_speaking")) {
            river_status_t status = river_orvibo_protocol_send_abort_speaking(NULL);
            river_orvibo_app_record_protocol_control("abort_speaking", status, true);
        }
    }
    if ((actions & RIVER_ORVIBO_ACTION_START_LISTENING) != 0U) {
        if (river_orvibo_app_protocol_control_channel_open("listen_start")) {
            const char *listen_mode = river_orvibo_app_listen_mode();
            river_status_t status =
                river_orvibo_protocol_send_start_listening(listen_mode);
            RIVER_LOGI("listen start mode=%s", listen_mode);
            river_orvibo_app_record_protocol_control("listen_start", status, true);
        }
    }
}

static river_status_t river_orvibo_app_refresh_access(const char *reason)
{
    river_status_t status = river_orvibo_access_refresh();

    g_river_orvibo_app.last_access_retry_ms =
        (uint32_t)rtos_time_get_current_system_time_ms();
    if (status == RIVER_OK) {
        g_river_orvibo_app.access_refresh_ok++;
        RIVER_LOGI("access refresh ok: reason=%s", reason != NULL ? reason : "-");
    } else {
        g_river_orvibo_app.access_refresh_fail++;
        snprintf(g_river_orvibo_app.last_error,
                 sizeof(g_river_orvibo_app.last_error),
                 "access_refresh:%d",
                 (int)status);
        RIVER_LOGW("access refresh failed: reason=%s status=%d",
                   reason != NULL ? reason : "-",
                   (int)status);
        river_orvibo_access_dump_status();
    }
    return status;
}

static void river_orvibo_app_handle_state_event(river_orvibo_event_t event, const char *reason)
{
    river_orvibo_transition_t transition;

    if (event == RIVER_ORVIBO_EVENT_WAKE_DETECTED) {
        river_orvibo_app_copy_text(g_river_orvibo_app.wake_text,
                                   sizeof(g_river_orvibo_app.wake_text),
                                   reason != NULL && reason[0] != '\0' ? reason : "小欧管家");
    }
    transition = river_orvibo_state_machine_dispatch(event, reason);
    (void)river_orvibo_ui_set_state(transition.new_state);
    river_orvibo_app_copy_text(g_river_orvibo_app.last_event,
                               sizeof(g_river_orvibo_app.last_event),
                               river_orvibo_event_name(event));
    if (event == RIVER_ORVIBO_EVENT_NETWORK_READY) {
        (void)river_orvibo_app_refresh_access(reason);
    }
    river_orvibo_app_apply_actions(transition.actions);
    if (event == RIVER_ORVIBO_EVENT_AUDIO_CHANNEL_CLOSED ||
        event == RIVER_ORVIBO_EVENT_NETWORK_LOST ||
        event == RIVER_ORVIBO_EVENT_ERROR_RECOVERABLE ||
        event == RIVER_ORVIBO_EVENT_ERROR_FATAL) {
        river_orvibo_app_clear_audio_queue(river_orvibo_event_name(event));
    }
    if (transition.changed && transition.new_state == RIVER_ORVIBO_STATE_RECOVERING) {
        river_orvibo_app_post_state(RIVER_ORVIBO_EVENT_RECOVERY_DONE, "recoverable_error_closed");
    }
}

static void river_orvibo_app_handle_message(const river_orvibo_app_msg_t *msg)
{
    river_status_t status;

    if (msg == NULL) {
        return;
    }
    g_river_orvibo_app.handled++;
    switch (msg->type) {
    case RIVER_ORVIBO_APP_MSG_STATE_EVENT:
        river_orvibo_app_handle_state_event(msg->event, msg->reason);
        break;
    case RIVER_ORVIBO_APP_MSG_AUDIO_UPLINK:
        status = river_orvibo_protocol_send_audio(msg->data, msg->bytes, msg->timestamp_ms);
        if (status == RIVER_OK) {
            g_river_orvibo_app.uplink_sent++;
        } else if (status == RIVER_ERR_BUSY) {
            g_river_orvibo_app.uplink_busy++;
        } else {
            snprintf(g_river_orvibo_app.last_error,
                     sizeof(g_river_orvibo_app.last_error),
                     "uplink:%d",
                     (int)status);
        }
        break;
    case RIVER_ORVIBO_APP_MSG_DOWNLINK_AUDIO:
        if (river_orvibo_state_machine_current() != RIVER_ORVIBO_STATE_SPEAKING) {
            g_river_orvibo_app.downlink_dropped++;
            RIVER_LOGW("drop downlink audio outside speaking: state=%s bytes=%lu ts=%lu",
                       river_orvibo_state_name(river_orvibo_state_machine_current()),
                       (unsigned long)msg->bytes,
                       (unsigned long)msg->timestamp_ms);
            break;
        }
        status = river_orvibo_audio_service_handle_downlink(msg->data,
                                                            msg->bytes,
                                                            msg->sample_rate,
                                                            msg->channels,
                                                            msg->frame_duration_ms);
        if (status == RIVER_OK) {
            g_river_orvibo_app.downlink_ok++;
        } else {
            g_river_orvibo_app.downlink_fail++;
        }
        break;
    case RIVER_ORVIBO_APP_MSG_SERVER_TEXT: {
        const char *kind_name =
            river_orvibo_app_server_text_kind_name(msg->server_text_kind);

        river_orvibo_app_copy_text(g_river_orvibo_app.last_event,
                                   sizeof(g_river_orvibo_app.last_event),
                                   kind_name);
        river_orvibo_app_copy_text(g_river_orvibo_app.last_server_text_kind,
                                   sizeof(g_river_orvibo_app.last_server_text_kind),
                                   kind_name);
        river_orvibo_app_copy_text(g_river_orvibo_app.last_server_text,
                                   sizeof(g_river_orvibo_app.last_server_text),
                                   msg->text);
        river_orvibo_app_copy_text(g_river_orvibo_app.last_server_detail,
                                   sizeof(g_river_orvibo_app.last_server_detail),
                                   msg->detail);
        if (msg->server_text_kind == RIVER_ORVIBO_APP_SERVER_TEXT_LLM) {
            (void)river_orvibo_ui_set_llm_emotion(
                g_river_orvibo_app.last_server_detail,
                g_river_orvibo_app.last_server_text);
            RIVER_LOGI("server llm: emotion=%s text=%s",
                       g_river_orvibo_app.last_server_detail[0] != '\0' ?
                           g_river_orvibo_app.last_server_detail :
                           "-",
                       g_river_orvibo_app.last_server_text[0] != '\0' ?
                           g_river_orvibo_app.last_server_text :
                           "-");
        } else {
            if (msg->server_text_kind == RIVER_ORVIBO_APP_SERVER_TEXT_STT) {
                (void)river_orvibo_ui_set_asr_text(g_river_orvibo_app.last_server_text);
            } else if (msg->server_text_kind ==
                       RIVER_ORVIBO_APP_SERVER_TEXT_SENTENCE_START) {
                (void)river_orvibo_ui_set_tts_text(g_river_orvibo_app.last_server_text);
            }
            RIVER_LOGI("server text: kind=%s text=%s",
                       kind_name,
                       g_river_orvibo_app.last_server_text[0] != '\0' ?
                           g_river_orvibo_app.last_server_text :
                           "-");
        }
        break;
    }
    case RIVER_ORVIBO_APP_MSG_CONNECT:
        river_orvibo_app_handle_state_event(RIVER_ORVIBO_EVENT_WAKE_DETECTED, "diag_connect");
        break;
    case RIVER_ORVIBO_APP_MSG_ACCESS_REFRESH:
        (void)river_orvibo_app_refresh_access("diag_refresh");
        break;
    case RIVER_ORVIBO_APP_MSG_LISTEN_START:
        status = river_orvibo_protocol_send_start_listening("manual");
        river_orvibo_app_record_protocol_control("diag_listen_start", status, false);
        (void)river_orvibo_audio_service_set_mode(RIVER_ORVIBO_AUDIO_MODE_LISTENING);
        break;
    case RIVER_ORVIBO_APP_MSG_LISTEN_STOP:
        status = river_orvibo_protocol_send_stop_listening();
        river_orvibo_app_record_protocol_control("diag_listen_stop", status, false);
        break;
    case RIVER_ORVIBO_APP_MSG_ABORT:
        status = river_orvibo_protocol_send_abort_speaking("diag_abort");
        river_orvibo_app_record_protocol_control("diag_abort", status, false);
        river_orvibo_audio_service_stop_playback("diag_abort");
        break;
    default:
        break;
    }
}

static void river_orvibo_app_check_wifi(void)
{
    uint32_t now_ms;

    if (river_wifi_station_is_connected()) {
        if (!g_river_orvibo_app.wifi_ready_reported) {
            g_river_orvibo_app.wifi_ready_reported = true;
            river_orvibo_app_post_state(RIVER_ORVIBO_EVENT_NETWORK_READY, "wifi_connected");
            return;
        }
        now_ms = (uint32_t)rtos_time_get_current_system_time_ms();
        if (!river_orvibo_access_ready() &&
            (g_river_orvibo_app.last_access_retry_ms == 0U ||
             now_ms - g_river_orvibo_app.last_access_retry_ms >=
                 RIVER_ORVIBO_APP_ACCESS_RETRY_MS)) {
            (void)river_orvibo_app_refresh_access("periodic_wifi_ready");
        }
        return;
    }
    if (g_river_orvibo_app.wifi_ready_reported) {
        g_river_orvibo_app.wifi_ready_reported = false;
        river_orvibo_app_clear_connect_retry("wifi_lost");
        river_orvibo_app_post_state(RIVER_ORVIBO_EVENT_NETWORK_LOST, "wifi_lost");
    }
    now_ms = (uint32_t)rtos_time_get_current_system_time_ms();
    if (g_river_orvibo_app.last_wifi_log_ms == 0U ||
        now_ms - g_river_orvibo_app.last_wifi_log_ms >= RIVER_ORVIBO_APP_WIFI_LOG_MS) {
        g_river_orvibo_app.last_wifi_log_ms = now_ms;
        RIVER_LOGI("waiting wifi: status=%s ssid=%s",
                   river_wifi_station_status_name(),
                   river_wifi_station_ssid());
        river_wifi_station_dump_status();
    }
}

static void river_orvibo_app_check_connect_retry(void)
{
    uint32_t now_ms;

    if (!g_river_orvibo_app.connect_retry_pending ||
        g_river_orvibo_app.next_connect_retry_ms == 0U) {
        return;
    }
    if (river_orvibo_state_machine_current() != RIVER_ORVIBO_STATE_IDLE ||
        !river_wifi_station_is_connected() ||
        !river_orvibo_access_ready()) {
        return;
    }

    now_ms = (uint32_t)rtos_time_get_current_system_time_ms();
    if (!river_orvibo_app_time_reached(now_ms,
                                       g_river_orvibo_app.next_connect_retry_ms)) {
        return;
    }

    g_river_orvibo_app.connect_retry_pending = false;
    g_river_orvibo_app.next_connect_retry_ms = 0U;
    g_river_orvibo_app.connect_retry_posted++;
    RIVER_LOGI("connect retry due: streak=%lu wake=%s",
               (unsigned long)g_river_orvibo_app.connect_fail_streak,
               g_river_orvibo_app.wake_text[0] != '\0' ?
                   g_river_orvibo_app.wake_text :
                   "-");
    river_orvibo_app_post_state(
        RIVER_ORVIBO_EVENT_WAKE_DETECTED,
        g_river_orvibo_app.wake_text[0] != '\0' ?
            g_river_orvibo_app.wake_text :
            "connect_retry");
}

static void river_orvibo_app_drain_queues(void)
{
    river_orvibo_app_msg_t msg;
    uint32_t audio_budget;

    while (rtos_queue_receive(g_river_orvibo_app.control_queue, &msg, 0U) ==
           RIVER_ORVIBO_RTOS_OK) {
        river_orvibo_app_handle_message(&msg);
    }

    for (audio_budget = 0U;
         audio_budget < RIVER_ORVIBO_APP_AUDIO_BUDGET_PER_TICK;
         ++audio_budget) {
        if (rtos_queue_receive(g_river_orvibo_app.audio_queue, &msg, 0U) !=
            RIVER_ORVIBO_RTOS_OK) {
            break;
        }
        river_orvibo_app_handle_message(&msg);
        if (rtos_queue_message_waiting(g_river_orvibo_app.control_queue) > 0U) {
            break;
        }
    }
}

static void river_orvibo_app_task(void *param)
{
    (void)param;
    g_river_orvibo_app.task_running = true;
    river_orvibo_app_post_state(RIVER_ORVIBO_EVENT_BOOT, "task_start");
    while (true) {
        river_orvibo_app_check_wifi();
        river_orvibo_app_check_connect_retry();
        (void)river_orvibo_protocol_poll(RIVER_ORVIBO_APP_POLL_MS);
        river_orvibo_app_drain_queues();
        rtos_time_delay_ms(RIVER_ORVIBO_APP_POLL_MS);
    }
}

river_status_t river_orvibo_app_boot(void)
{
    if (g_river_orvibo_app.booted) {
        return RIVER_OK;
    }

    memset(&g_river_orvibo_app, 0, sizeof(g_river_orvibo_app));
    RIVER_LOGI("orvibo client boot target=RTL8730E");
    river_runtime_stats_init();
    river_orvibo_state_machine_init(RIVER_ORVIBO_STATE_STARTING);
    (void)river_orvibo_ui_start();
    if (rtos_queue_create(&g_river_orvibo_app.control_queue,
                          RIVER_ORVIBO_APP_CONTROL_QUEUE_DEPTH,
                          sizeof(river_orvibo_app_msg_t)) != RIVER_ORVIBO_RTOS_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    if (rtos_queue_create(&g_river_orvibo_app.audio_queue,
                          RIVER_ORVIBO_APP_AUDIO_QUEUE_DEPTH,
                          sizeof(river_orvibo_app_msg_t)) != RIVER_ORVIBO_RTOS_OK) {
        rtos_queue_delete(g_river_orvibo_app.control_queue);
        g_river_orvibo_app.control_queue = NULL;
        return RIVER_ERR_NO_MEMORY;
    }
    if (river_playback_service_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    if (river_reference_service_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    if (river_wifi_station_init() != RIVER_OK) {
        return RIVER_ERR_UNSUPPORTED;
    }
    if (river_orvibo_protocol_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    if (river_orvibo_access_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    if (river_orvibo_protocol_set_event_handler(river_orvibo_protocol_event_handler, NULL) !=
        RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    if (river_orvibo_audio_service_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    if (river_orvibo_audio_service_set_event_handler(river_orvibo_audio_event_handler, NULL) !=
        RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    if (river_orvibo_audio_service_start() != RIVER_OK) {
        return RIVER_ERR_UNSUPPORTED;
    }
    if (rtos_task_create(&g_river_orvibo_app.task,
                         "orvibo_app",
                         river_orvibo_app_task,
                         NULL,
                         RIVER_ORVIBO_APP_TASK_STACK,
                         RIVER_ORVIBO_APP_TASK_PRIORITY) != RIVER_ORVIBO_RTOS_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    g_river_orvibo_app.booted = true;
    river_orvibo_app_print_status();
    river_runtime_stats_snapshot("orvibo_boot_ready");
    return RIVER_OK;
}

void river_orvibo_app_print_status(void)
{
    uint32_t control_depth = g_river_orvibo_app.control_queue != NULL ?
                                 rtos_queue_message_waiting(
                                     g_river_orvibo_app.control_queue) :
                                 0U;
    uint32_t audio_depth = g_river_orvibo_app.audio_queue != NULL ?
                               rtos_queue_message_waiting(
                                   g_river_orvibo_app.audio_queue) :
                               0U;

    RIVER_LOGI("orvibo app: state=%s wifi=%s audio_max=%u ctl_q=%lu/%u aud_q=%lu/%u posted=%lu fail=%lu ctl=%lu/%lu aud=%lu/%lu aud_drop_oldest=%lu aud_flush=%lu handled=%lu uplink_enq=%lu busy=%lu downlink=%lu/%lu dropped=%lu oversize=%lu/%lu last_event=%s last_error=%s",
               river_orvibo_state_name(river_orvibo_state_machine_current()),
               river_wifi_station_status_name(),
               (unsigned int)RIVER_ORVIBO_APP_AUDIO_PACKET_MAX,
               (unsigned long)control_depth,
               (unsigned int)RIVER_ORVIBO_APP_CONTROL_QUEUE_DEPTH,
               (unsigned long)audio_depth,
               (unsigned int)RIVER_ORVIBO_APP_AUDIO_QUEUE_DEPTH,
               (unsigned long)g_river_orvibo_app.posted,
               (unsigned long)g_river_orvibo_app.post_fail,
               (unsigned long)g_river_orvibo_app.control_posted,
               (unsigned long)g_river_orvibo_app.control_post_fail,
               (unsigned long)g_river_orvibo_app.audio_posted,
               (unsigned long)g_river_orvibo_app.audio_post_fail,
               (unsigned long)g_river_orvibo_app.audio_drop_oldest,
               (unsigned long)g_river_orvibo_app.audio_queue_flushes,
               (unsigned long)g_river_orvibo_app.handled,
               (unsigned long)g_river_orvibo_app.uplink_sent,
               (unsigned long)g_river_orvibo_app.uplink_busy,
               (unsigned long)g_river_orvibo_app.downlink_ok,
               (unsigned long)g_river_orvibo_app.downlink_fail,
               (unsigned long)g_river_orvibo_app.downlink_dropped,
               (unsigned long)g_river_orvibo_app.uplink_oversize,
               (unsigned long)g_river_orvibo_app.downlink_oversize,
               g_river_orvibo_app.last_event[0] != '\0' ?
                   g_river_orvibo_app.last_event :
                   "-",
               g_river_orvibo_app.last_error[0] != '\0' ?
                   g_river_orvibo_app.last_error :
                   "-");
    RIVER_LOGI("orvibo server text: kind=%s text=%s detail=%s",
               g_river_orvibo_app.last_server_text_kind[0] != '\0' ?
                   g_river_orvibo_app.last_server_text_kind :
                   "-",
               g_river_orvibo_app.last_server_text[0] != '\0' ?
                   g_river_orvibo_app.last_server_text :
                   "-",
               g_river_orvibo_app.last_server_detail[0] != '\0' ?
                   g_river_orvibo_app.last_server_detail :
                   "-");
    RIVER_LOGI("orvibo access refresh=%lu/%lu protocol_ctrl=%lu/%lu skip=%lu wake_text=%s",
               (unsigned long)g_river_orvibo_app.access_refresh_ok,
               (unsigned long)g_river_orvibo_app.access_refresh_fail,
               (unsigned long)g_river_orvibo_app.protocol_control_ok,
               (unsigned long)g_river_orvibo_app.protocol_control_fail,
               (unsigned long)g_river_orvibo_app.protocol_control_skip,
               g_river_orvibo_app.wake_text[0] != '\0' ? g_river_orvibo_app.wake_text : "-");
    RIVER_LOGI("orvibo connect: ok=%lu fail=%lu retry=%s streak=%lu next=%lu posted=%lu suppressed=%lu",
               (unsigned long)g_river_orvibo_app.connect_open_ok,
               (unsigned long)g_river_orvibo_app.connect_open_fail,
               g_river_orvibo_app.connect_retry_pending ? "pending" : "idle",
               (unsigned long)g_river_orvibo_app.connect_fail_streak,
               (unsigned long)g_river_orvibo_app.next_connect_retry_ms,
               (unsigned long)g_river_orvibo_app.connect_retry_posted,
               (unsigned long)g_river_orvibo_app.connect_backoff_suppressed);
    RIVER_LOGI("local_preproc=%s profile=%s detector=%s board=%s",
               river_voice_preproc_backend_name(),
               river_voice_preproc_profile_name(),
               river_voice_detector_backend_name(),
               river_voice_board_array_profile()->board_name);
    river_orvibo_protocol_dump_status();
    river_orvibo_access_dump_status();
    river_orvibo_audio_service_dump_status();
    river_orvibo_mcp_volume_dump_status();
    river_orvibo_ui_dump_status();
    river_wifi_station_dump_status();
}

void river_orvibo_app_request_connect(void)
{
    river_orvibo_app_msg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.type = RIVER_ORVIBO_APP_MSG_CONNECT;
    (void)river_orvibo_app_post(&msg);
}

void river_orvibo_app_request_access_refresh(void)
{
    river_orvibo_app_msg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.type = RIVER_ORVIBO_APP_MSG_ACCESS_REFRESH;
    (void)river_orvibo_app_post(&msg);
}

void river_orvibo_app_request_listen_start(void)
{
    river_orvibo_app_msg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.type = RIVER_ORVIBO_APP_MSG_LISTEN_START;
    (void)river_orvibo_app_post(&msg);
}

void river_orvibo_app_request_listen_stop(void)
{
    river_orvibo_app_msg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.type = RIVER_ORVIBO_APP_MSG_LISTEN_STOP;
    (void)river_orvibo_app_post(&msg);
}

void river_orvibo_app_request_abort(void)
{
    river_orvibo_app_msg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.type = RIVER_ORVIBO_APP_MSG_ABORT;
    (void)river_orvibo_app_post(&msg);
}
