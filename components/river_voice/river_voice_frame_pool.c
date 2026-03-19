#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "os_wrapper.h"

#include "river/river_voice_frame_pool.h"

typedef struct {
    uint8_t *block;
    size_t block_bytes;
} river_voice_frame_pool_context_t;

static river_voice_frame_pool_context_t g_river_voice_frame_pool;

static size_t river_voice_frame_pool_total_bytes(const river_voice_frame_pool_layout_t *layout)
{
    if (layout == NULL) {
        return 0U;
    }

    return layout->capture_bytes +
           layout->enhanced_bytes +
           layout->reference_bytes +
           layout->playback_ref_bytes;
}

river_status_t river_voice_frame_pool_acquire(const river_voice_frame_pool_layout_t *layout,
                                              river_voice_frame_pool_view_t *view)
{
    size_t total_bytes;
    uint8_t *cursor;

    if (layout == NULL || view == NULL) {
        return RIVER_ERR_ARG;
    }

    total_bytes = river_voice_frame_pool_total_bytes(layout);
    if (layout->capture_bytes == 0U || layout->enhanced_bytes == 0U || total_bytes == 0U) {
        return RIVER_ERR_ARG;
    }

    if (g_river_voice_frame_pool.block != NULL &&
        g_river_voice_frame_pool.block_bytes < total_bytes) {
        rtos_mem_free(g_river_voice_frame_pool.block);
        memset(&g_river_voice_frame_pool, 0, sizeof(g_river_voice_frame_pool));
    }

    if (g_river_voice_frame_pool.block == NULL) {
        g_river_voice_frame_pool.block = (uint8_t *)rtos_mem_zmalloc((uint32_t)total_bytes);
        if (g_river_voice_frame_pool.block == NULL) {
            return RIVER_ERR_NO_MEMORY;
        }
        g_river_voice_frame_pool.block_bytes = total_bytes;
    }

    memset(g_river_voice_frame_pool.block, 0, total_bytes);
    memset(view, 0, sizeof(*view));

    cursor = g_river_voice_frame_pool.block;
    view->capture_buffer = cursor;
    cursor += layout->capture_bytes;

    view->enhanced_buffer = cursor;
    cursor += layout->enhanced_bytes;

    if (layout->reference_bytes > 0U) {
        view->reference_buffer = cursor;
        cursor += layout->reference_bytes;
    }
    if (layout->playback_ref_bytes > 0U) {
        view->playback_ref_buffer = cursor;
        cursor += layout->playback_ref_bytes;
    }

    view->total_bytes = total_bytes;
    return RIVER_OK;
}

void river_voice_frame_pool_release(void)
{
    if (g_river_voice_frame_pool.block != NULL && g_river_voice_frame_pool.block_bytes > 0U) {
        memset(g_river_voice_frame_pool.block, 0, g_river_voice_frame_pool.block_bytes);
    }
}

void river_voice_frame_pool_trim(void)
{
    if (g_river_voice_frame_pool.block != NULL) {
        rtos_mem_free(g_river_voice_frame_pool.block);
    }
    memset(&g_river_voice_frame_pool, 0, sizeof(g_river_voice_frame_pool));
}

size_t river_voice_frame_pool_bytes(void)
{
    return g_river_voice_frame_pool.block_bytes;
}
