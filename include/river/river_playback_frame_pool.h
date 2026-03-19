#ifndef AMEBA_RIVER_PLAYBACK_FRAME_POOL_H
#define AMEBA_RIVER_PLAYBACK_FRAME_POOL_H

#include <stddef.h>
#include <stdint.h>

#include "river/river_types.h"

typedef struct {
    uint8_t *ring_storage;
    uint8_t *discard_frame;
    size_t ring_storage_bytes;
    size_t total_bytes;
} river_playback_frame_pool_ref_view_t;

river_status_t river_playback_frame_pool_acquire_ref(size_t frame_bytes,
                                                     uint32_t frame_capacity,
                                                     river_playback_frame_pool_ref_view_t *view);
void river_playback_frame_pool_release_ref(void);
void river_playback_frame_pool_trim(void);
size_t river_playback_frame_pool_bytes(void);

#endif
