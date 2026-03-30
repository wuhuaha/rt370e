/* 通用音频帧环形缓冲：统一处理采集、播放和参考流的帧级排队。 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "basic_types.h"
#include "os_wrapper.h"

#include "river/river_audio_frame_ring.h"

static bool river_audio_frame_ring_lock(river_audio_frame_ring_t *ring)
{
    if (ring == NULL || !ring->initialized) {
        return false;
    }

    if (ring->mode == RIVER_AUDIO_FRAME_RING_MODE_SPSC) {
        return true;
    }

    return rtos_mutex_take(ring->lock, MUTEX_WAIT_TIMEOUT) == RTK_SUCCESS;
}

static void river_audio_frame_ring_unlock(river_audio_frame_ring_t *ring, bool locked)
{
    if (ring == NULL || !locked) {
        return;
    }

    if (ring->mode == RIVER_AUDIO_FRAME_RING_MODE_SPSC) {
        return;
    }

    rtos_mutex_give(ring->lock);
}

static uint32_t river_audio_frame_ring_spsc_count(const river_audio_frame_ring_t *ring)
{
    uint32_t read_cursor;
    uint32_t write_cursor;

    if (ring == NULL) {
        return 0U;
    }

    read_cursor = __atomic_load_n(&ring->spsc_read_cursor, __ATOMIC_ACQUIRE);
    write_cursor = __atomic_load_n(&ring->spsc_write_cursor, __ATOMIC_ACQUIRE);
    return write_cursor - read_cursor;
}

static river_status_t river_audio_frame_ring_init_internal(river_audio_frame_ring_t *ring,
                                                           uint8_t *storage,
                                                           size_t storage_bytes,
                                                           size_t frame_bytes,
                                                           uint32_t frame_capacity,
                                                           bool owns_storage,
                                                           river_audio_frame_ring_mode_t mode)
{
    uint64_t required_bytes;

    if (ring == NULL || storage == NULL || frame_bytes == 0U || frame_capacity == 0U) {
        return RIVER_ERR_ARG;
    }

    if (ring->initialized) {
        if (ring->frame_bytes == frame_bytes && ring->frame_capacity == frame_capacity &&
            ring->storage != NULL && ring->mode == mode) {
            return RIVER_OK;
        }
        return RIVER_ERR_BUSY;
    }

    memset(ring, 0, sizeof(*ring));
    if (mode == RIVER_AUDIO_FRAME_RING_MODE_LOCKED) {
        if (rtos_mutex_create(&ring->lock) != RTK_SUCCESS) {
            return RIVER_ERR_NO_MEMORY;
        }
    }

    required_bytes = (uint64_t)frame_bytes * (uint64_t)frame_capacity;
    if (required_bytes == 0U || required_bytes > SIZE_MAX || storage_bytes < (size_t)required_bytes) {
        if (mode == RIVER_AUDIO_FRAME_RING_MODE_LOCKED) {
            rtos_mutex_delete(ring->lock);
        }
        memset(ring, 0, sizeof(*ring));
        return RIVER_ERR_ARG;
    }

    ring->storage = storage;
    ring->storage_bytes = storage_bytes;
    ring->frame_bytes = frame_bytes;
    ring->frame_capacity = frame_capacity;
    ring->owns_storage = owns_storage;
    ring->mode = mode;
    ring->initialized = true;
    return RIVER_OK;
}

river_status_t river_audio_frame_ring_init_ex(river_audio_frame_ring_t *ring,
                                              size_t frame_bytes,
                                              uint32_t frame_capacity,
                                              river_audio_frame_ring_mode_t mode)
{
    uint64_t storage_bytes;
    uint8_t *storage;
    river_status_t status;

    if (ring == NULL || frame_bytes == 0U || frame_capacity == 0U) {
        return RIVER_ERR_ARG;
    }

    if (ring->initialized) {
        if (ring->frame_bytes == frame_bytes && ring->frame_capacity == frame_capacity &&
            ring->storage != NULL) {
            return RIVER_OK;
        }
        return RIVER_ERR_BUSY;
    }

    storage_bytes = (uint64_t)frame_bytes * (uint64_t)frame_capacity;
    if (storage_bytes == 0U || storage_bytes > UINT32_MAX) {
        return RIVER_ERR_ARG;
    }

    storage = (uint8_t *)rtos_mem_zmalloc((uint32_t)storage_bytes);
    if (storage == NULL) {
        return RIVER_ERR_NO_MEMORY;
    }

    status = river_audio_frame_ring_init_internal(ring,
                                                  storage,
                                                  (size_t)storage_bytes,
                                                  frame_bytes,
                                                  frame_capacity,
                                                  true,
                                                  mode);
    if (status != RIVER_OK) {
        rtos_mem_free(storage);
        return status;
    }

    return RIVER_OK;
}

river_status_t river_audio_frame_ring_init(river_audio_frame_ring_t *ring,
                                           size_t frame_bytes,
                                           uint32_t frame_capacity)
{
    return river_audio_frame_ring_init_ex(ring,
                                          frame_bytes,
                                          frame_capacity,
                                          RIVER_AUDIO_FRAME_RING_MODE_LOCKED);
}

river_status_t river_audio_frame_ring_init_with_storage_ex(river_audio_frame_ring_t *ring,
                                                           void *storage,
                                                           size_t storage_bytes,
                                                           size_t frame_bytes,
                                                           uint32_t frame_capacity,
                                                           river_audio_frame_ring_mode_t mode)
{
    return river_audio_frame_ring_init_internal(ring,
                                                (uint8_t *)storage,
                                                storage_bytes,
                                                frame_bytes,
                                                frame_capacity,
                                                false,
                                                mode);
}

river_status_t river_audio_frame_ring_init_with_storage(river_audio_frame_ring_t *ring,
                                                        void *storage,
                                                        size_t storage_bytes,
                                                        size_t frame_bytes,
                                                        uint32_t frame_capacity)
{
    return river_audio_frame_ring_init_with_storage_ex(ring,
                                                       storage,
                                                       storage_bytes,
                                                       frame_bytes,
                                                       frame_capacity,
                                                       RIVER_AUDIO_FRAME_RING_MODE_LOCKED);
}

void river_audio_frame_ring_deinit(river_audio_frame_ring_t *ring)
{
    if (ring == NULL || !ring->initialized) {
        return;
    }

    if (ring->owns_storage && ring->storage != NULL) {
        rtos_mem_free(ring->storage);
    }
    if (ring->mode == RIVER_AUDIO_FRAME_RING_MODE_LOCKED && ring->lock != 0) {
        rtos_mutex_delete(ring->lock);
    }
    memset(ring, 0, sizeof(*ring));
}

void river_audio_frame_ring_reset(river_audio_frame_ring_t *ring)
{
    bool locked;

    locked = river_audio_frame_ring_lock(ring);
    if (!locked) {
        return;
    }

    if (ring->mode == RIVER_AUDIO_FRAME_RING_MODE_SPSC) {
        __atomic_store_n(&ring->spsc_read_cursor, 0U, __ATOMIC_RELEASE);
        __atomic_store_n(&ring->spsc_write_cursor, 0U, __ATOMIC_RELEASE);
        __atomic_store_n(&ring->peak_frame_count, 0U, __ATOMIC_RELEASE);
    } else {
        ring->frame_count = 0U;
        ring->read_index = 0U;
        ring->write_index = 0U;
        ring->peak_frame_count = 0U;
    }
    river_audio_frame_ring_unlock(ring, locked);
}

uint32_t river_audio_frame_ring_count(river_audio_frame_ring_t *ring)
{
    bool locked;
    uint32_t count;

    if (ring == NULL || !ring->initialized) {
        return 0U;
    }

    if (ring->mode == RIVER_AUDIO_FRAME_RING_MODE_SPSC) {
        return river_audio_frame_ring_spsc_count(ring);
    }

    locked = river_audio_frame_ring_lock(ring);
    if (!locked) {
        return 0U;
    }

    count = ring->frame_count;
    river_audio_frame_ring_unlock(ring, locked);
    return count;
}

uint32_t river_audio_frame_ring_peak_count(river_audio_frame_ring_t *ring)
{
    bool locked;
    uint32_t peak_count;

    if (ring == NULL || !ring->initialized) {
        return 0U;
    }

    if (ring->mode == RIVER_AUDIO_FRAME_RING_MODE_SPSC) {
        return __atomic_load_n(&ring->peak_frame_count, __ATOMIC_ACQUIRE);
    }

    locked = river_audio_frame_ring_lock(ring);
    if (!locked) {
        return 0U;
    }

    peak_count = ring->peak_frame_count;
    river_audio_frame_ring_unlock(ring, locked);
    return peak_count;
}

river_status_t river_audio_frame_ring_write(river_audio_frame_ring_t *ring, const uint8_t *frame)
{
    bool locked;
    uint8_t *dst;
    uint32_t queued_frames;

    if (ring == NULL || frame == NULL || !ring->initialized) {
        return RIVER_ERR_ARG;
    }

    if (ring->mode == RIVER_AUDIO_FRAME_RING_MODE_SPSC) {
        uint32_t read_cursor;
        uint32_t write_cursor;

        read_cursor = __atomic_load_n(&ring->spsc_read_cursor, __ATOMIC_ACQUIRE);
        write_cursor = __atomic_load_n(&ring->spsc_write_cursor, __ATOMIC_RELAXED);
        queued_frames = write_cursor - read_cursor;
        if (queued_frames >= ring->frame_capacity) {
            return RIVER_ERR_NO_MEMORY;
        }

        dst = ring->storage + (((size_t)(write_cursor % ring->frame_capacity)) * ring->frame_bytes);
        memcpy(dst, frame, ring->frame_bytes);
        write_cursor++;
        __atomic_store_n(&ring->spsc_write_cursor, write_cursor, __ATOMIC_RELEASE);
        queued_frames++;
        if (queued_frames > __atomic_load_n(&ring->peak_frame_count, __ATOMIC_RELAXED)) {
            __atomic_store_n(&ring->peak_frame_count, queued_frames, __ATOMIC_RELEASE);
        }
        return RIVER_OK;
    }

    locked = river_audio_frame_ring_lock(ring);
    if (!locked) {
        return RIVER_ERR_BUSY;
    }

    if (ring->frame_count >= ring->frame_capacity) {
        river_audio_frame_ring_unlock(ring, locked);
        return RIVER_ERR_NO_MEMORY;
    }

    dst = ring->storage + ((size_t)ring->write_index * ring->frame_bytes);
    memcpy(dst, frame, ring->frame_bytes);
    ring->write_index = (ring->write_index + 1U) % ring->frame_capacity;
    ring->frame_count++;
    if (ring->frame_count > ring->peak_frame_count) {
        ring->peak_frame_count = ring->frame_count;
    }

    river_audio_frame_ring_unlock(ring, locked);
    return RIVER_OK;
}

river_status_t river_audio_frame_ring_read(river_audio_frame_ring_t *ring, uint8_t *frame)
{
    bool locked;
    const uint8_t *src;

    if (ring == NULL || frame == NULL || !ring->initialized) {
        return RIVER_ERR_ARG;
    }

    if (ring->mode == RIVER_AUDIO_FRAME_RING_MODE_SPSC) {
        uint32_t read_cursor;
        uint32_t write_cursor;

        read_cursor = __atomic_load_n(&ring->spsc_read_cursor, __ATOMIC_RELAXED);
        write_cursor = __atomic_load_n(&ring->spsc_write_cursor, __ATOMIC_ACQUIRE);
        if (write_cursor == read_cursor) {
            return RIVER_ERR_NOT_FOUND;
        }

        src = ring->storage + (((size_t)(read_cursor % ring->frame_capacity)) * ring->frame_bytes);
        memcpy(frame, src, ring->frame_bytes);
        __atomic_store_n(&ring->spsc_read_cursor, read_cursor + 1U, __ATOMIC_RELEASE);
        return RIVER_OK;
    }

    locked = river_audio_frame_ring_lock(ring);
    if (!locked) {
        return RIVER_ERR_BUSY;
    }

    if (ring->frame_count == 0U) {
        river_audio_frame_ring_unlock(ring, locked);
        return RIVER_ERR_NOT_FOUND;
    }

    src = ring->storage + ((size_t)ring->read_index * ring->frame_bytes);
    memcpy(frame, src, ring->frame_bytes);
    ring->read_index = (ring->read_index + 1U) % ring->frame_capacity;
    ring->frame_count--;

    river_audio_frame_ring_unlock(ring, locked);
    return RIVER_OK;
}
