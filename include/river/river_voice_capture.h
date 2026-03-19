#ifndef AMEBA_RIVER_VOICE_CAPTURE_H
#define AMEBA_RIVER_VOICE_CAPTURE_H

#include <stdbool.h>
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

typedef struct {
    bool running;
    size_t frame_bytes;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t frame_ms;
    uint32_t queue_frames;
    uint32_t queue_peak_frames;
    uint32_t queue_capacity_frames;
    uint32_t dropped_frames;
    uint32_t read_ok;
    uint32_t read_wait_timeout;
} river_voice_capture_stats_t;

river_status_t river_voice_capture_open(river_voice_capture_t *capture);
int32_t river_voice_capture_read(river_voice_capture_t *capture, void *buffer, size_t bytes);
void river_voice_capture_close(river_voice_capture_t *capture);
void river_voice_capture_get_stats(river_voice_capture_stats_t *stats);
void river_voice_capture_dump_profile(void);
void river_voice_capture_dump_status(void);

#endif
