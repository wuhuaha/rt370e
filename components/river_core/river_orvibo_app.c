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
#include "river/river_orvibo_mcp_volume.h"
#include "river/river_orvibo_protocol.h"
#include "river/river_orvibo_state.h"
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
#define RIVER_ORVIBO_APP_QUEUE_DEPTH       32U
#define RIVER_ORVIBO_APP_POLL_MS           20U
#define RIVER_ORVIBO_APP_WIFI_LOG_MS       3000U
#define RIVER_ORVIBO_APP_ACCESS_RETRY_MS   10000U
#define RIVER_ORVIBO_APP_TTS_DRAIN_MS      900U
#define RIVER_ORVIBO_APP_AUDIO_PACKET_MAX  768U
#define RIVER_ORVIBO_RTOS_OK               0

typedef enum {
    RIVER_ORVIBO_APP_MSG_STATE_EVENT = 0,
    RIVER_ORVIBO_APP_MSG_AUDIO_UPLINK,
    RIVER_ORVIBO_APP_MSG_DOWNLINK_AUDIO,
    RIVER_ORVIBO_APP_MSG_CONNECT,
    RIVER_ORVIBO_APP_MSG_LISTEN_START,
    RIVER_ORVIBO_APP_MSG_LISTEN_STOP,
    RIVER_ORVIBO_APP_MSG_ABORT
} river_orvibo_app_msg_type_t;

typedef struct {
    river_orvibo_app_msg_type_t type;
    river_orvibo_event_t event;
    char reason[48];
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
    rtos_task_t task;
    rtos_queue_t queue;
    uint32_t posted;
    uint32_t post_fail;
    uint32_t handled;
    uint32_t uplink_sent;
    uint32_t uplink_busy;
    uint32_t downlink_ok;
    uint32_t downlink_fail;
    uint32_t downlink_dropped;
    uint32_t access_refresh_ok;
    uint32_t access_refresh_fail;
    uint32_t last_wifi_log_ms;
    uint32_t last_access_retry_ms;
    char wake_text[64];
    char last_event[48];
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

static river_status_t river_orvibo_app_post(const river_orvibo_app_msg_t *msg)
{
    if (msg == NULL || g_river_orvibo_app.queue == NULL) {
        return RIVER_ERR_INVALID_STATE;
    }
    if (rtos_queue_send(g_river_orvibo_app.queue, (void *)msg, 0U) != RIVER_ORVIBO_RTOS_OK) {
        g_river_orvibo_app.post_fail++;
        return RIVER_ERR_BUSY;
    }
    g_river_orvibo_app.posted++;
    return RIVER_OK;
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
        if (event->packet == NULL || event->packet_bytes == 0U ||
            event->packet_bytes > sizeof(msg.data)) {
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
    case RIVER_ORVIBO_PROTOCOL_EVENT_AUDIO_PACKET:
        if (event->audio_data == NULL || event->audio_bytes == 0U ||
            event->audio_bytes > sizeof(msg.data)) {
            return;
        }
        msg.type = RIVER_ORVIBO_APP_MSG_DOWNLINK_AUDIO;
        msg.sample_rate = event->sample_rate;
        msg.channels = 1U;
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
        if (status == RIVER_OK) {
            status = river_orvibo_protocol_open_audio_channel();
        }
        if (status != RIVER_OK) {
            snprintf(g_river_orvibo_app.last_error,
                     sizeof(g_river_orvibo_app.last_error),
                     "open_audio_channel:%d",
                     (int)status);
            river_orvibo_app_post_state(RIVER_ORVIBO_EVENT_ERROR_RECOVERABLE,
                                        "open_audio_channel_failed");
        }
    }
    if ((actions & RIVER_ORVIBO_ACTION_CLOSE_AUDIO_CHANNEL) != 0U) {
        river_orvibo_protocol_close_audio_channel();
    }
    if ((actions & RIVER_ORVIBO_ACTION_SEND_WAKE_DETECTED) != 0U) {
        (void)river_orvibo_protocol_send_wake_word_detected(
            g_river_orvibo_app.wake_text[0] != '\0' ? g_river_orvibo_app.wake_text : "小欧管家");
    }
    if ((actions & RIVER_ORVIBO_ACTION_START_LISTENING) != 0U) {
        (void)river_orvibo_protocol_send_start_listening("auto");
    }
    if ((actions & RIVER_ORVIBO_ACTION_STOP_LISTENING) != 0U) {
        (void)river_orvibo_protocol_send_stop_listening();
    }
    if ((actions & RIVER_ORVIBO_ACTION_ABORT_SPEAKING) != 0U) {
        (void)river_orvibo_protocol_send_abort_speaking("wake_word_detected");
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
    river_orvibo_app_copy_text(g_river_orvibo_app.last_event,
                               sizeof(g_river_orvibo_app.last_event),
                               river_orvibo_event_name(event));
    if (event == RIVER_ORVIBO_EVENT_NETWORK_READY) {
        (void)river_orvibo_app_refresh_access(reason);
    }
    river_orvibo_app_apply_actions(transition.actions);
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
    case RIVER_ORVIBO_APP_MSG_CONNECT:
        river_orvibo_app_handle_state_event(RIVER_ORVIBO_EVENT_WAKE_DETECTED, "diag_connect");
        break;
    case RIVER_ORVIBO_APP_MSG_LISTEN_START:
        (void)river_orvibo_protocol_send_start_listening("manual");
        (void)river_orvibo_audio_service_set_mode(RIVER_ORVIBO_AUDIO_MODE_LISTENING);
        break;
    case RIVER_ORVIBO_APP_MSG_LISTEN_STOP:
        (void)river_orvibo_protocol_send_stop_listening();
        break;
    case RIVER_ORVIBO_APP_MSG_ABORT:
        (void)river_orvibo_protocol_send_abort_speaking("diag_abort");
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
        river_orvibo_app_post_state(RIVER_ORVIBO_EVENT_NETWORK_LOST, "wifi_lost");
    }
    now_ms = (uint32_t)rtos_time_get_current_system_time_ms();
    if (g_river_orvibo_app.last_wifi_log_ms == 0U ||
        now_ms - g_river_orvibo_app.last_wifi_log_ms >= RIVER_ORVIBO_APP_WIFI_LOG_MS) {
        g_river_orvibo_app.last_wifi_log_ms = now_ms;
        RIVER_LOGI("waiting wifi: status=%s ssid=%s",
                   river_wifi_station_status_name(),
                   river_wifi_station_ssid());
    }
}

static void river_orvibo_app_task(void *param)
{
    river_orvibo_app_msg_t msg;

    (void)param;
    g_river_orvibo_app.task_running = true;
    river_orvibo_app_post_state(RIVER_ORVIBO_EVENT_BOOT, "task_start");
    while (true) {
        river_orvibo_app_check_wifi();
        (void)river_orvibo_protocol_poll(RIVER_ORVIBO_APP_POLL_MS);
        while (rtos_queue_receive(g_river_orvibo_app.queue, &msg, 0U) == RIVER_ORVIBO_RTOS_OK) {
            river_orvibo_app_handle_message(&msg);
        }
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
    if (rtos_queue_create(&g_river_orvibo_app.queue,
                          RIVER_ORVIBO_APP_QUEUE_DEPTH,
                          sizeof(river_orvibo_app_msg_t)) != RIVER_ORVIBO_RTOS_OK) {
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
    RIVER_LOGI("orvibo app: state=%s wifi=%s queue=%lu posted=%lu fail=%lu handled=%lu uplink=%lu busy=%lu downlink=%lu/%lu dropped=%lu last_event=%s last_error=%s",
               river_orvibo_state_name(river_orvibo_state_machine_current()),
               river_wifi_station_status_name(),
               (unsigned long)(g_river_orvibo_app.queue != NULL ?
                   rtos_queue_message_waiting(g_river_orvibo_app.queue) :
                   0U),
               (unsigned long)g_river_orvibo_app.posted,
               (unsigned long)g_river_orvibo_app.post_fail,
               (unsigned long)g_river_orvibo_app.handled,
               (unsigned long)g_river_orvibo_app.uplink_sent,
               (unsigned long)g_river_orvibo_app.uplink_busy,
               (unsigned long)g_river_orvibo_app.downlink_ok,
               (unsigned long)g_river_orvibo_app.downlink_fail,
               (unsigned long)g_river_orvibo_app.downlink_dropped,
               g_river_orvibo_app.last_event[0] != '\0' ?
                   g_river_orvibo_app.last_event :
                   "-",
               g_river_orvibo_app.last_error[0] != '\0' ?
                   g_river_orvibo_app.last_error :
                   "-");
    RIVER_LOGI("orvibo access refresh=%lu/%lu wake_text=%s",
               (unsigned long)g_river_orvibo_app.access_refresh_ok,
               (unsigned long)g_river_orvibo_app.access_refresh_fail,
               g_river_orvibo_app.wake_text[0] != '\0' ? g_river_orvibo_app.wake_text : "-");
    RIVER_LOGI("local_preproc=%s profile=%s detector=%s board=%s",
               river_voice_preproc_backend_name(),
               river_voice_preproc_profile_name(),
               river_voice_detector_backend_name(),
               river_voice_board_array_profile()->board_name);
    river_orvibo_protocol_dump_status();
    river_orvibo_access_dump_status();
    river_orvibo_audio_service_dump_status();
    river_orvibo_mcp_volume_dump_status();
    river_wifi_station_dump_status();
}

void river_orvibo_app_request_connect(void)
{
    river_orvibo_app_msg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.type = RIVER_ORVIBO_APP_MSG_CONNECT;
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
