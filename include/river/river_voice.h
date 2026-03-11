#ifndef AMEBA_RIVER_VOICE_H
#define AMEBA_RIVER_VOICE_H

#include <stdbool.h>

#include "river/river_types.h"

typedef enum {
    RIVER_VOICE_MODE_VAD_ONLY = 0,
    RIVER_VOICE_MODE_VAD_WAKEWORD = 1
} river_voice_mode_t;

typedef enum {
    RIVER_VOICE_EVENT_VAD_START = 0,
    RIVER_VOICE_EVENT_VAD_END = 1,
    RIVER_VOICE_EVENT_WAKEWORD = 2,
    RIVER_VOICE_EVENT_OFFLINE_ASR = 3
} river_voice_event_type_t;

typedef struct {
    river_voice_event_type_t type;
    const char *text;
    int confidence;
} river_voice_event_t;

typedef void (*river_voice_event_handler_t)(const river_voice_event_t *event);

river_status_t river_voice_frontend_init(void);
void river_voice_frontend_set_handler(river_voice_event_handler_t handler);
const char *river_voice_frontend_mode_name(void);
const char *river_voice_preproc_backend_name(void);
const char *river_voice_ref_backend_name(void);
river_status_t river_voice_echo_start(void);
river_status_t river_voice_echo_stop(void);
bool river_voice_echo_is_running(void);
const char *river_voice_echo_status_name(void);
void river_voice_echo_set_diag_enabled(bool enabled);
bool river_voice_echo_diag_enabled(void);
void river_voice_echo_dump_status(void);
river_status_t river_voice_speaker_test_start(void);
river_status_t river_voice_speaker_test_stop(void);
bool river_voice_speaker_test_is_running(void);
const char *river_voice_speaker_test_status_name(void);
void river_voice_speaker_test_set_diag_enabled(bool enabled);
bool river_voice_speaker_test_diag_enabled(void);
void river_voice_speaker_test_dump_status(void);

#endif
