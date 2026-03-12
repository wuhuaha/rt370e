#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "basic_types.h"
#include "os_wrapper.h"

#include "river/river_log.h"
#include "river/river_voice_ref.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.voice.ref"

#define RIVER_VOICE_REF_DEFAULT_HISTORY_MS  1536U

typedef struct {
    bool opened;
    rtos_mutex_t lock;
    uint8_t *buffer;
    size_t buffer_bytes;
    size_t frame_bytes;
    size_t queued_bytes;
    size_t read_offset;
    size_t write_offset;
    uint32_t sample_rate;
    uint32_t frame_ms;
    uint32_t channels;
    uint32_t history_ms;
} river_voice_ref_context_t;

static river_voice_ref_context_t g_river_voice_ref;

static size_t river_voice_ref_min_size(size_t left, size_t right)
{
    return left < right ? left : right;
}

static void river_voice_ref_copy_in(size_t offset, const uint8_t *src, size_t bytes)
{
    size_t first_copy;

    first_copy = river_voice_ref_min_size(bytes, g_river_voice_ref.buffer_bytes - offset);
    memcpy(g_river_voice_ref.buffer + offset, src, first_copy);
    if (bytes > first_copy) {
        memcpy(g_river_voice_ref.buffer, src + first_copy, bytes - first_copy);
    }
}

static void river_voice_ref_copy_out(size_t offset, uint8_t *dst, size_t bytes)
{
    size_t first_copy;

    first_copy = river_voice_ref_min_size(bytes, g_river_voice_ref.buffer_bytes - offset);
    memcpy(dst, g_river_voice_ref.buffer + offset, first_copy);
    if (bytes > first_copy) {
        memcpy(dst + first_copy, g_river_voice_ref.buffer, bytes - first_copy);
    }
}

river_status_t river_voice_ref_open(uint32_t sample_rate,
                                    uint32_t frame_ms,
                                    uint32_t channels,
                                    uint32_t history_ms)
{
    uint32_t history_frames;

    if (sample_rate == 0U || frame_ms == 0U || channels == 0U) {
        return RIVER_ERR_ARG;
    }

    river_voice_ref_close();
    memset(&g_river_voice_ref, 0, sizeof(g_river_voice_ref));

    g_river_voice_ref.sample_rate = sample_rate;
    g_river_voice_ref.frame_ms = frame_ms;
    g_river_voice_ref.channels = channels;
    g_river_voice_ref.history_ms = history_ms == 0U ? RIVER_VOICE_REF_DEFAULT_HISTORY_MS : history_ms;
    g_river_voice_ref.frame_bytes = ((sample_rate * frame_ms) / 1000U) * channels * sizeof(int16_t);
    history_frames = (g_river_voice_ref.history_ms + (frame_ms - 1U)) / frame_ms;
    if (history_frames < 4U) {
        history_frames = 4U;
    }
    g_river_voice_ref.buffer_bytes = g_river_voice_ref.frame_bytes * history_frames;

    if (rtos_mutex_create(&g_river_voice_ref.lock) != RTK_SUCCESS) {
        return RIVER_ERR_NO_MEMORY;
    }

    g_river_voice_ref.buffer = (uint8_t *)rtos_mem_zmalloc((uint32_t)g_river_voice_ref.buffer_bytes);
    if (g_river_voice_ref.buffer == 0) {
        rtos_mutex_delete(g_river_voice_ref.lock);
        g_river_voice_ref.lock = 0;
        return RIVER_ERR_NO_MEMORY;
    }

    g_river_voice_ref.opened = true;
    return RIVER_OK;
}

void river_voice_ref_reset(void)
{
    if (!g_river_voice_ref.opened || g_river_voice_ref.lock == 0) {
        return;
    }

    if (rtos_mutex_take(g_river_voice_ref.lock, MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
        return;
    }

    memset(g_river_voice_ref.buffer, 0, g_river_voice_ref.buffer_bytes);
    g_river_voice_ref.queued_bytes = 0U;
    g_river_voice_ref.read_offset = 0U;
    g_river_voice_ref.write_offset = 0U;

    rtos_mutex_give(g_river_voice_ref.lock);
}

void river_voice_ref_close(void)
{
    if (!g_river_voice_ref.opened) {
        return;
    }

    if (g_river_voice_ref.buffer != 0) {
        rtos_mem_free(g_river_voice_ref.buffer);
        g_river_voice_ref.buffer = 0;
    }

    if (g_river_voice_ref.lock != 0) {
        rtos_mutex_delete(g_river_voice_ref.lock);
        g_river_voice_ref.lock = 0;
    }

    memset(&g_river_voice_ref, 0, sizeof(g_river_voice_ref));
}

river_status_t river_voice_ref_push(const uint8_t *data, size_t bytes)
{
    if (!g_river_voice_ref.opened || data == 0 || bytes == 0U) {
        return RIVER_ERR_ARG;
    }
    if (bytes != g_river_voice_ref.frame_bytes || bytes > g_river_voice_ref.buffer_bytes) {
        return RIVER_ERR_ARG;
    }

    if (rtos_mutex_take(g_river_voice_ref.lock, MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
        return RIVER_ERR_BUSY;
    }

    while (g_river_voice_ref.queued_bytes + bytes > g_river_voice_ref.buffer_bytes) {
        g_river_voice_ref.read_offset =
            (g_river_voice_ref.read_offset + g_river_voice_ref.frame_bytes) % g_river_voice_ref.buffer_bytes;
        g_river_voice_ref.queued_bytes -= g_river_voice_ref.frame_bytes;
    }

    river_voice_ref_copy_in(g_river_voice_ref.write_offset, data, bytes);
    g_river_voice_ref.write_offset = (g_river_voice_ref.write_offset + bytes) % g_river_voice_ref.buffer_bytes;
    g_river_voice_ref.queued_bytes += bytes;

    rtos_mutex_give(g_river_voice_ref.lock);
    return RIVER_OK;
}

river_status_t river_voice_ref_read(uint8_t *data, size_t bytes)
{
    if (!g_river_voice_ref.opened || data == 0 || bytes == 0U) {
        return RIVER_ERR_ARG;
    }
    if (bytes != g_river_voice_ref.frame_bytes || bytes > g_river_voice_ref.buffer_bytes) {
        return RIVER_ERR_ARG;
    }

    if (rtos_mutex_take(g_river_voice_ref.lock, MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
        memset(data, 0, bytes);
        return RIVER_ERR_BUSY;
    }

    if (g_river_voice_ref.queued_bytes < bytes) {
        memset(data, 0, bytes);
        rtos_mutex_give(g_river_voice_ref.lock);
        return RIVER_ERR_NOT_FOUND;
    }

    river_voice_ref_copy_out(g_river_voice_ref.read_offset, data, bytes);
    g_river_voice_ref.read_offset = (g_river_voice_ref.read_offset + bytes) % g_river_voice_ref.buffer_bytes;
    g_river_voice_ref.queued_bytes -= bytes;

    rtos_mutex_give(g_river_voice_ref.lock);
    return RIVER_OK;
}

bool river_voice_ref_is_open(void)
{
    return g_river_voice_ref.opened;
}

const char *river_voice_ref_backend_name(void)
{
    return "playback_ring";
}

void river_voice_ref_dump_profile(void)
{
    if (!g_river_voice_ref.opened) {
        RIVER_LOGI("playback ref: deferred backend=%s source=post-delay mono speaker feed",
                   river_voice_ref_backend_name());
        return;
    }

    RIVER_LOGI("playback ref: %s %lu Hz %lums %luch history=%lums source=post-delay mono speaker feed",
               river_voice_ref_backend_name(),
               (unsigned long)g_river_voice_ref.sample_rate,
               (unsigned long)g_river_voice_ref.frame_ms,
               (unsigned long)g_river_voice_ref.channels,
               (unsigned long)g_river_voice_ref.history_ms);
}
