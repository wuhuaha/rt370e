#ifndef AMEBA_RIVER_VOICE_VAD_REFERENCE_H
#define AMEBA_RIVER_VOICE_VAD_REFERENCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "river/river_types.h"

typedef struct {
    bool opened;
    bool is_speech;
    uint32_t total_events;
    uint32_t total_speech_start;
    uint32_t total_speech_end;
    uint32_t total_feed_ok;
    uint32_t total_feed_fail;
    uint32_t last_offset_ms;
} river_voice_vad_reference_status_t;

river_status_t river_voice_vad_reference_open(void);
river_status_t river_voice_vad_reference_process(const uint8_t *input, size_t input_bytes);
void river_voice_vad_reference_close(void);
void river_voice_vad_reference_get_status(river_voice_vad_reference_status_t *status);
const char *river_voice_vad_reference_name(void);
void river_voice_vad_reference_dump_profile(void);

#endif
