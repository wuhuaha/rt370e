#ifndef AMEBA_RIVER_VOICE_SEGMENT_BUFFER_H
#define AMEBA_RIVER_VOICE_SEGMENT_BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "river/river_types.h"

typedef struct {
    uint32_t sample_rate;
    uint32_t frame_ms;
    size_t frame_bytes;
    uint32_t pre_roll_ms;
    uint32_t post_roll_ms;
    uint32_t max_segment_ms;
} river_voice_segment_buffer_config_t;

typedef struct {
    bool active;
    bool ready;
    uint32_t active_frames;
    uint32_t ready_frames;
    uint32_t ready_bytes;
    uint32_t prebuffered_frames;
    uint32_t post_roll_frames_left;
    uint32_t segments_started;
    uint32_t segments_completed;
    uint32_t segments_dropped;
} river_voice_segment_buffer_status_t;

typedef struct {
    river_voice_segment_buffer_config_t config;
    uint8_t *pre_roll_buffer;
    uint8_t *segment_buffer;
    uint32_t pre_roll_capacity_frames;
    uint32_t pre_roll_count_frames;
    uint32_t pre_roll_write_index_frames;
    uint32_t post_roll_frames;
    uint32_t post_roll_frames_left;
    uint32_t max_segment_bytes;
    uint32_t segment_bytes;
    uint32_t ready_bytes;
    uint32_t active_frames;
    uint32_t ready_frames;
    uint32_t segments_started;
    uint32_t segments_completed;
    uint32_t segments_dropped;
    bool active;
    bool ready;
    bool pre_roll_buffer_heap_types;
    bool segment_buffer_heap_types;
} river_voice_segment_buffer_t;

river_status_t river_voice_segment_buffer_open(river_voice_segment_buffer_t *buffer,
                                               const river_voice_segment_buffer_config_t *config);
river_status_t river_voice_segment_buffer_push(river_voice_segment_buffer_t *buffer,
                                               const uint8_t *frame,
                                               size_t frame_bytes,
                                               bool is_speech);
void river_voice_segment_buffer_get_status(const river_voice_segment_buffer_t *buffer,
                                           river_voice_segment_buffer_status_t *status);
const uint8_t *river_voice_segment_buffer_ready_data(const river_voice_segment_buffer_t *buffer);
size_t river_voice_segment_buffer_ready_bytes(const river_voice_segment_buffer_t *buffer);
void river_voice_segment_buffer_release_ready(river_voice_segment_buffer_t *buffer);
void river_voice_segment_buffer_close(river_voice_segment_buffer_t *buffer);

#endif
