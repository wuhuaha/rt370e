#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "os_wrapper.h"

#include "river/river_playback_frame_pool.h"

typedef struct {
    uint8_t *ref_block;
    size_t ref_block_bytes;
} river_playback_frame_pool_context_t;

static river_playback_frame_pool_context_t g_river_playback_frame_pool;

river_status_t river_playback_frame_pool_acquire_ref(size_t frame_bytes,
                                                     uint32_t frame_capacity,
                                                     river_playback_frame_pool_ref_view_t *view)
{
    uint64_t ring_bytes64;
    uint64_t total_bytes64;
    size_t ring_bytes;
    size_t total_bytes;

    if (frame_bytes == 0U || frame_capacity == 0U || view == NULL) {
        return RIVER_ERR_ARG;
    }

    ring_bytes64 = (uint64_t)frame_bytes * (uint64_t)frame_capacity;
    total_bytes64 = ring_bytes64 + (uint64_t)frame_bytes;
    if (ring_bytes64 == 0U || total_bytes64 > UINT32_MAX) {
        return RIVER_ERR_ARG;
    }

    ring_bytes = (size_t)ring_bytes64;
    total_bytes = (size_t)total_bytes64;

    if (g_river_playback_frame_pool.ref_block != NULL &&
        g_river_playback_frame_pool.ref_block_bytes < total_bytes) {
        rtos_mem_free(g_river_playback_frame_pool.ref_block);
        memset(&g_river_playback_frame_pool, 0, sizeof(g_river_playback_frame_pool));
    }

    if (g_river_playback_frame_pool.ref_block == NULL) {
        g_river_playback_frame_pool.ref_block = (uint8_t *)rtos_mem_zmalloc((uint32_t)total_bytes);
        if (g_river_playback_frame_pool.ref_block == NULL) {
            return RIVER_ERR_NO_MEMORY;
        }
        g_river_playback_frame_pool.ref_block_bytes = total_bytes;
    }

    memset(g_river_playback_frame_pool.ref_block, 0, total_bytes);
    memset(view, 0, sizeof(*view));
    view->ring_storage = g_river_playback_frame_pool.ref_block;
    view->discard_frame = g_river_playback_frame_pool.ref_block + ring_bytes;
    view->ring_storage_bytes = ring_bytes;
    view->total_bytes = total_bytes;
    return RIVER_OK;
}

void river_playback_frame_pool_release_ref(void)
{
    if (g_river_playback_frame_pool.ref_block != NULL &&
        g_river_playback_frame_pool.ref_block_bytes > 0U) {
        memset(g_river_playback_frame_pool.ref_block, 0, g_river_playback_frame_pool.ref_block_bytes);
    }
}

void river_playback_frame_pool_trim(void)
{
    if (g_river_playback_frame_pool.ref_block != NULL) {
        rtos_mem_free(g_river_playback_frame_pool.ref_block);
    }
    memset(&g_river_playback_frame_pool, 0, sizeof(g_river_playback_frame_pool));
}

size_t river_playback_frame_pool_bytes(void)
{
    return g_river_playback_frame_pool.ref_block_bytes;
}
