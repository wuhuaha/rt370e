/* Orvibo client state machine. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "river/river_log.h"
#include "river/river_orvibo_state.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.orvibo.state"

static river_orvibo_state_t g_river_orvibo_state = RIVER_ORVIBO_STATE_STARTING;

const char *river_orvibo_state_name(river_orvibo_state_t state)
{
    switch (state) {
    case RIVER_ORVIBO_STATE_STARTING:
        return "starting";
    case RIVER_ORVIBO_STATE_NETWORK_WAIT:
        return "network_wait";
    case RIVER_ORVIBO_STATE_IDLE:
        return "idle";
    case RIVER_ORVIBO_STATE_CONNECTING:
        return "connecting";
    case RIVER_ORVIBO_STATE_LISTENING:
        return "listening";
    case RIVER_ORVIBO_STATE_SPEAKING:
        return "speaking";
    case RIVER_ORVIBO_STATE_RECOVERING:
        return "recovering";
    case RIVER_ORVIBO_STATE_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

const char *river_orvibo_event_name(river_orvibo_event_t event)
{
    switch (event) {
    case RIVER_ORVIBO_EVENT_BOOT:
        return "boot";
    case RIVER_ORVIBO_EVENT_NETWORK_READY:
        return "network_ready";
    case RIVER_ORVIBO_EVENT_NETWORK_LOST:
        return "network_lost";
    case RIVER_ORVIBO_EVENT_WAKE_DETECTED:
        return "wake_detected";
    case RIVER_ORVIBO_EVENT_AUDIO_CHANNEL_OPENED:
        return "audio_channel_opened";
    case RIVER_ORVIBO_EVENT_AUDIO_CHANNEL_CLOSED:
        return "audio_channel_closed";
    case RIVER_ORVIBO_EVENT_USER_SPEECH_STARTED:
        return "user_speech_started";
    case RIVER_ORVIBO_EVENT_USER_SPEECH_ENDED:
        return "user_speech_ended";
    case RIVER_ORVIBO_EVENT_SERVER_HELLO:
        return "server_hello";
    case RIVER_ORVIBO_EVENT_SERVER_TTS_STARTED:
        return "server_tts_started";
    case RIVER_ORVIBO_EVENT_SERVER_TTS_FINISHED:
        return "server_tts_finished";
    case RIVER_ORVIBO_EVENT_RECOVERY_DONE:
        return "recovery_done";
    case RIVER_ORVIBO_EVENT_ERROR_RECOVERABLE:
        return "error_recoverable";
    case RIVER_ORVIBO_EVENT_ERROR_FATAL:
        return "error_fatal";
    default:
        return "unknown";
    }
}

void river_orvibo_state_machine_init(river_orvibo_state_t initial_state)
{
    g_river_orvibo_state = initial_state;
}

river_orvibo_state_t river_orvibo_state_machine_current(void)
{
    return g_river_orvibo_state;
}

static river_orvibo_transition_t river_orvibo_transition_make(
    river_orvibo_state_t old_state,
    river_orvibo_state_t new_state,
    river_orvibo_event_t event,
    uint32_t actions)
{
    river_orvibo_transition_t transition;

    transition.old_state = old_state;
    transition.new_state = new_state;
    transition.event = event;
    transition.actions = actions;
    transition.changed = old_state != new_state;
    return transition;
}

river_orvibo_transition_t river_orvibo_state_machine_dispatch(river_orvibo_event_t event,
                                                              const char *reason)
{
    river_orvibo_state_t old_state = g_river_orvibo_state;
    river_orvibo_state_t new_state = old_state;
    uint32_t actions = RIVER_ORVIBO_ACTION_NONE;

    if (event == RIVER_ORVIBO_EVENT_NETWORK_LOST) {
        new_state = RIVER_ORVIBO_STATE_NETWORK_WAIT;
        actions = RIVER_ORVIBO_ACTION_CLOSE_AUDIO_CHANNEL |
                  RIVER_ORVIBO_ACTION_AUDIO_IDLE |
                  RIVER_ORVIBO_ACTION_STOP_PLAYBACK;
    } else if (event == RIVER_ORVIBO_EVENT_ERROR_FATAL) {
        new_state = RIVER_ORVIBO_STATE_ERROR;
        actions = RIVER_ORVIBO_ACTION_CLOSE_AUDIO_CHANNEL |
                  RIVER_ORVIBO_ACTION_AUDIO_IDLE |
                  RIVER_ORVIBO_ACTION_STOP_PLAYBACK;
    } else if (event == RIVER_ORVIBO_EVENT_ERROR_RECOVERABLE) {
        new_state = RIVER_ORVIBO_STATE_RECOVERING;
        actions = RIVER_ORVIBO_ACTION_CLOSE_AUDIO_CHANNEL |
                  RIVER_ORVIBO_ACTION_AUDIO_IDLE |
                  RIVER_ORVIBO_ACTION_STOP_PLAYBACK;
    } else {
        switch (old_state) {
        case RIVER_ORVIBO_STATE_STARTING:
            if (event == RIVER_ORVIBO_EVENT_BOOT) {
                new_state = RIVER_ORVIBO_STATE_NETWORK_WAIT;
                actions = RIVER_ORVIBO_ACTION_AUDIO_IDLE;
            } else if (event == RIVER_ORVIBO_EVENT_NETWORK_READY) {
                new_state = RIVER_ORVIBO_STATE_IDLE;
                actions = RIVER_ORVIBO_ACTION_AUDIO_IDLE;
            }
            break;
        case RIVER_ORVIBO_STATE_NETWORK_WAIT:
            if (event == RIVER_ORVIBO_EVENT_NETWORK_READY) {
                new_state = RIVER_ORVIBO_STATE_IDLE;
                actions = RIVER_ORVIBO_ACTION_AUDIO_IDLE;
            }
            break;
        case RIVER_ORVIBO_STATE_IDLE:
            if (event == RIVER_ORVIBO_EVENT_WAKE_DETECTED) {
                new_state = RIVER_ORVIBO_STATE_CONNECTING;
                actions = RIVER_ORVIBO_ACTION_OPEN_AUDIO_CHANNEL;
            } else if (event == RIVER_ORVIBO_EVENT_AUDIO_CHANNEL_OPENED) {
                new_state = RIVER_ORVIBO_STATE_LISTENING;
                actions = RIVER_ORVIBO_ACTION_AUDIO_LISTENING |
                          RIVER_ORVIBO_ACTION_SEND_WAKE_DETECTED |
                          RIVER_ORVIBO_ACTION_START_LISTENING |
                          RIVER_ORVIBO_ACTION_DISABLE_BARGE_IN;
            }
            break;
        case RIVER_ORVIBO_STATE_CONNECTING:
            if (event == RIVER_ORVIBO_EVENT_AUDIO_CHANNEL_OPENED ||
                event == RIVER_ORVIBO_EVENT_SERVER_HELLO) {
                new_state = RIVER_ORVIBO_STATE_LISTENING;
                actions = RIVER_ORVIBO_ACTION_AUDIO_LISTENING |
                          RIVER_ORVIBO_ACTION_SEND_WAKE_DETECTED |
                          RIVER_ORVIBO_ACTION_START_LISTENING |
                          RIVER_ORVIBO_ACTION_DISABLE_BARGE_IN;
            } else if (event == RIVER_ORVIBO_EVENT_AUDIO_CHANNEL_CLOSED) {
                new_state = RIVER_ORVIBO_STATE_IDLE;
                actions = RIVER_ORVIBO_ACTION_CLOSE_AUDIO_CHANNEL |
                          RIVER_ORVIBO_ACTION_AUDIO_IDLE;
            }
            break;
        case RIVER_ORVIBO_STATE_LISTENING:
            if (event == RIVER_ORVIBO_EVENT_WAKE_DETECTED) {
                new_state = RIVER_ORVIBO_STATE_LISTENING;
                actions = RIVER_ORVIBO_ACTION_ABORT_WAKE_WORD |
                          RIVER_ORVIBO_ACTION_START_LISTENING |
                          RIVER_ORVIBO_ACTION_AUDIO_LISTENING |
                          RIVER_ORVIBO_ACTION_DISABLE_BARGE_IN;
            } else if (event == RIVER_ORVIBO_EVENT_SERVER_TTS_STARTED) {
                new_state = RIVER_ORVIBO_STATE_SPEAKING;
                actions = RIVER_ORVIBO_ACTION_PREPARE_TTS_PLAYBACK |
                          RIVER_ORVIBO_ACTION_AUDIO_SPEAKING |
                          RIVER_ORVIBO_ACTION_ENABLE_BARGE_IN;
            } else if (event == RIVER_ORVIBO_EVENT_AUDIO_CHANNEL_CLOSED) {
                new_state = RIVER_ORVIBO_STATE_IDLE;
                actions = RIVER_ORVIBO_ACTION_CLOSE_AUDIO_CHANNEL |
                          RIVER_ORVIBO_ACTION_AUDIO_IDLE;
            }
            break;
        case RIVER_ORVIBO_STATE_SPEAKING:
            if (event == RIVER_ORVIBO_EVENT_WAKE_DETECTED) {
                new_state = RIVER_ORVIBO_STATE_LISTENING;
                actions = RIVER_ORVIBO_ACTION_ABORT_WAKE_WORD |
                          RIVER_ORVIBO_ACTION_AUDIO_LISTENING |
                          RIVER_ORVIBO_ACTION_START_LISTENING |
                          RIVER_ORVIBO_ACTION_STOP_PLAYBACK |
                          RIVER_ORVIBO_ACTION_DISABLE_BARGE_IN;
            } else if (event == RIVER_ORVIBO_EVENT_SERVER_TTS_FINISHED) {
                new_state = RIVER_ORVIBO_STATE_LISTENING;
                actions = RIVER_ORVIBO_ACTION_WAIT_PLAYBACK_IDLE |
                          RIVER_ORVIBO_ACTION_AUDIO_LISTENING |
                          RIVER_ORVIBO_ACTION_START_LISTENING |
                          RIVER_ORVIBO_ACTION_DISABLE_BARGE_IN;
            } else if (event == RIVER_ORVIBO_EVENT_AUDIO_CHANNEL_CLOSED) {
                new_state = RIVER_ORVIBO_STATE_IDLE;
                actions = RIVER_ORVIBO_ACTION_CLOSE_AUDIO_CHANNEL |
                          RIVER_ORVIBO_ACTION_AUDIO_IDLE |
                          RIVER_ORVIBO_ACTION_STOP_PLAYBACK |
                          RIVER_ORVIBO_ACTION_DISABLE_BARGE_IN;
            }
            break;
        case RIVER_ORVIBO_STATE_RECOVERING:
            if (event == RIVER_ORVIBO_EVENT_RECOVERY_DONE ||
                event == RIVER_ORVIBO_EVENT_AUDIO_CHANNEL_CLOSED) {
                new_state = RIVER_ORVIBO_STATE_IDLE;
                actions = RIVER_ORVIBO_ACTION_CLOSE_AUDIO_CHANNEL |
                          RIVER_ORVIBO_ACTION_AUDIO_IDLE;
            }
            break;
        case RIVER_ORVIBO_STATE_ERROR:
            break;
        default:
            break;
        }
    }

    if (new_state != old_state) {
        RIVER_LOGI("orvibo state: %s -> %s reason=%s event=%s actions=0x%lx",
                   river_orvibo_state_name(old_state),
                   river_orvibo_state_name(new_state),
                   reason != NULL ? reason : "-",
                   river_orvibo_event_name(event),
                   (unsigned long)actions);
        g_river_orvibo_state = new_state;
    } else if (actions != RIVER_ORVIBO_ACTION_NONE) {
        RIVER_LOGI("orvibo action: state=%s event=%s reason=%s actions=0x%lx",
                   river_orvibo_state_name(old_state),
                   river_orvibo_event_name(event),
                   reason != NULL ? reason : "-",
                   (unsigned long)actions);
    } else {
        RIVER_LOGW("orvibo state ignore: state=%s event=%s reason=%s",
                   river_orvibo_state_name(old_state),
                   river_orvibo_event_name(event),
                   reason != NULL ? reason : "-");
    }

    return river_orvibo_transition_make(old_state, new_state, event, actions);
}
