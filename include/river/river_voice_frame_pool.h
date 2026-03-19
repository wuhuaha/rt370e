#ifndef AMEBA_RIVER_VOICE_FRAME_POOL_H
#define AMEBA_RIVER_VOICE_FRAME_POOL_H

#include <stddef.h>
#include <stdint.h>

#include "river/river_types.h"

typedef struct {
    size_t capture_bytes;
    size_t enhanced_bytes;
    size_t reference_bytes;
    size_t playback_ref_bytes;
} river_voice_frame_pool_layout_t;

typedef struct {
    uint8_t *capture_buffer;
    uint8_t *enhanced_buffer;
    uint8_t *reference_buffer;
    uint8_t *playback_ref_buffer;
    size_t total_bytes;
} river_voice_frame_pool_view_t;

river_status_t river_voice_frame_pool_acquire(const river_voice_frame_pool_layout_t *layout,
                                              river_voice_frame_pool_view_t *view);
void river_voice_frame_pool_release(void);
void river_voice_frame_pool_trim(void);
size_t river_voice_frame_pool_bytes(void);

#endif
