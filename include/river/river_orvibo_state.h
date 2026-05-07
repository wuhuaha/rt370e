/* Orvibo client state machine public contract. */
#ifndef AMEBA_RIVER_ORVIBO_STATE_H
#define AMEBA_RIVER_ORVIBO_STATE_H

#include "river/river_types.h"

typedef enum {
    RIVER_ORVIBO_STATE_STARTING = 0,
    RIVER_ORVIBO_STATE_NETWORK_WAIT,
    RIVER_ORVIBO_STATE_IDLE,
    RIVER_ORVIBO_STATE_CONNECTING,
    RIVER_ORVIBO_STATE_LISTENING,
    RIVER_ORVIBO_STATE_SPEAKING,
    RIVER_ORVIBO_STATE_RECOVERING,
    RIVER_ORVIBO_STATE_ERROR
} river_orvibo_state_t;

typedef enum {
    RIVER_ORVIBO_EVENT_BOOT = 0,
    RIVER_ORVIBO_EVENT_NETWORK_READY,
    RIVER_ORVIBO_EVENT_NETWORK_LOST,
    RIVER_ORVIBO_EVENT_WAKE_DETECTED,
    RIVER_ORVIBO_EVENT_AUDIO_CHANNEL_OPENED,
    RIVER_ORVIBO_EVENT_AUDIO_CHANNEL_CLOSED,
    RIVER_ORVIBO_EVENT_USER_SPEECH_STARTED,
    RIVER_ORVIBO_EVENT_USER_SPEECH_ENDED,
    RIVER_ORVIBO_EVENT_SERVER_HELLO,
    RIVER_ORVIBO_EVENT_SERVER_TTS_STARTED,
    RIVER_ORVIBO_EVENT_SERVER_TTS_FINISHED,
    RIVER_ORVIBO_EVENT_RECOVERY_DONE,
    RIVER_ORVIBO_EVENT_ERROR_RECOVERABLE,
    RIVER_ORVIBO_EVENT_ERROR_FATAL
} river_orvibo_event_t;

typedef enum {
    RIVER_ORVIBO_ACTION_NONE = 0,
    RIVER_ORVIBO_ACTION_OPEN_AUDIO_CHANNEL = 1U << 0,
    RIVER_ORVIBO_ACTION_CLOSE_AUDIO_CHANNEL = 1U << 1,
    RIVER_ORVIBO_ACTION_START_LISTENING = 1U << 2,
    RIVER_ORVIBO_ACTION_STOP_LISTENING = 1U << 3,
    RIVER_ORVIBO_ACTION_SEND_WAKE_DETECTED = 1U << 4,
    RIVER_ORVIBO_ACTION_ABORT_SPEAKING = 1U << 5,
    RIVER_ORVIBO_ACTION_AUDIO_IDLE = 1U << 6,
    RIVER_ORVIBO_ACTION_AUDIO_LISTENING = 1U << 7,
    RIVER_ORVIBO_ACTION_AUDIO_SPEAKING = 1U << 8,
    RIVER_ORVIBO_ACTION_STOP_PLAYBACK = 1U << 9,
    RIVER_ORVIBO_ACTION_ENABLE_BARGE_IN = 1U << 10,
    RIVER_ORVIBO_ACTION_DISABLE_BARGE_IN = 1U << 11
} river_orvibo_action_mask_t;

typedef struct {
    river_orvibo_state_t old_state;
    river_orvibo_state_t new_state;
    river_orvibo_event_t event;
    uint32_t actions;
    bool changed;
} river_orvibo_transition_t;

void river_orvibo_state_machine_init(river_orvibo_state_t initial_state);
river_orvibo_transition_t river_orvibo_state_machine_dispatch(river_orvibo_event_t event,
                                                              const char *reason);
river_orvibo_state_t river_orvibo_state_machine_current(void);
const char *river_orvibo_state_name(river_orvibo_state_t state);
const char *river_orvibo_event_name(river_orvibo_event_t event);

#endif
