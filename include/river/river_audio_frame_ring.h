#ifndef AMEBA_RIVER_AUDIO_FRAME_RING_H
#define AMEBA_RIVER_AUDIO_FRAME_RING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "os_wrapper.h"

#include "river/river_types.h"

typedef enum {
    RIVER_AUDIO_FRAME_RING_MODE_LOCKED = 0,
    RIVER_AUDIO_FRAME_RING_MODE_SPSC = 1
} river_audio_frame_ring_mode_t;

typedef struct {
    bool initialized;
    bool owns_storage;
    river_audio_frame_ring_mode_t mode;
    rtos_mutex_t lock;
    uint8_t *storage;
    size_t storage_bytes;
    size_t frame_bytes;
    uint32_t frame_capacity;
    uint32_t frame_count;
    uint32_t read_index;
    uint32_t write_index;
    uint32_t peak_frame_count;
    uint32_t spsc_read_cursor;
    uint32_t spsc_write_cursor;
} river_audio_frame_ring_t;

river_status_t river_audio_frame_ring_init_ex(river_audio_frame_ring_t *ring,
                                              size_t frame_bytes,
                                              uint32_t frame_capacity,
                                              river_audio_frame_ring_mode_t mode);
river_status_t river_audio_frame_ring_init(river_audio_frame_ring_t *ring,
                                           size_t frame_bytes,
                                           uint32_t frame_capacity);
river_status_t river_audio_frame_ring_init_with_storage_ex(river_audio_frame_ring_t *ring,
                                                           void *storage,
                                                           size_t storage_bytes,
                                                           size_t frame_bytes,
                                                           uint32_t frame_capacity,
                                                           river_audio_frame_ring_mode_t mode);
river_status_t river_audio_frame_ring_init_with_storage(river_audio_frame_ring_t *ring,
                                                        void *storage,
                                                        size_t storage_bytes,
                                                        size_t frame_bytes,
                                                        uint32_t frame_capacity);
void river_audio_frame_ring_deinit(river_audio_frame_ring_t *ring);
void river_audio_frame_ring_reset(river_audio_frame_ring_t *ring);
uint32_t river_audio_frame_ring_count(river_audio_frame_ring_t *ring);
uint32_t river_audio_frame_ring_peak_count(river_audio_frame_ring_t *ring);
river_status_t river_audio_frame_ring_write(river_audio_frame_ring_t *ring, const uint8_t *frame);
river_status_t river_audio_frame_ring_read(river_audio_frame_ring_t *ring, uint8_t *frame);

#endif
