#ifndef AMEBA_RIVER_VOICE_CAPTURE_H
#define AMEBA_RIVER_VOICE_CAPTURE_H

#include <stddef.h>
#include <stdint.h>

#include "river/river_types.h"

typedef struct {
    void *record;
    size_t frame_bytes;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t frame_ms;
    uint32_t frame_samples;
    int started;
} river_voice_capture_t;

river_status_t river_voice_capture_open(river_voice_capture_t *capture);
int32_t river_voice_capture_read(river_voice_capture_t *capture, void *buffer, size_t bytes);
void river_voice_capture_close(river_voice_capture_t *capture);
void river_voice_capture_dump_profile(void);

#endif
