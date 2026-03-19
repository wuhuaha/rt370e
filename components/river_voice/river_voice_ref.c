#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "basic_types.h"
#include "os_wrapper.h"

#include "river/river_audio_frame_ring.h"
#include "river/river_log.h"
#include "river/river_playback_frame_pool.h"
#include "river/river_voice_ref.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.voice.ref"

#define RIVER_VOICE_REF_DEFAULT_HISTORY_MS  1536U

typedef struct {
    bool opened;
    rtos_mutex_t lock;
    river_audio_frame_ring_t ring;
    uint8_t *discard_frame;
    size_t frame_bytes;
    uint32_t frame_capacity;
    uint32_t dropped_frames;
    uint32_t sample_rate;
    uint32_t frame_ms;
    uint32_t channels;
    uint32_t history_ms;
} river_voice_ref_context_t;

static river_voice_ref_context_t g_river_voice_ref;

river_status_t river_voice_ref_open(uint32_t sample_rate,
                                    uint32_t frame_ms,
                                    uint32_t channels,
                                    uint32_t history_ms)
{
    uint32_t history_frames;
    river_playback_frame_pool_ref_view_t pool_view;

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
    g_river_voice_ref.frame_capacity = history_frames;

    if (rtos_mutex_create(&g_river_voice_ref.lock) != RTK_SUCCESS) {
        return RIVER_ERR_NO_MEMORY;
    }

    memset(&pool_view, 0, sizeof(pool_view));
    if (river_playback_frame_pool_acquire_ref(g_river_voice_ref.frame_bytes,
                                              g_river_voice_ref.frame_capacity,
                                              &pool_view) != RIVER_OK) {
        rtos_mutex_delete(g_river_voice_ref.lock);
        g_river_voice_ref.lock = 0;
        return RIVER_ERR_NO_MEMORY;
    }

    if (river_audio_frame_ring_init_with_storage(&g_river_voice_ref.ring,
                                                 pool_view.ring_storage,
                                                 pool_view.ring_storage_bytes,
                                                 g_river_voice_ref.frame_bytes,
                                                 g_river_voice_ref.frame_capacity) != RIVER_OK) {
        rtos_mutex_delete(g_river_voice_ref.lock);
        g_river_voice_ref.lock = 0;
        return RIVER_ERR_NO_MEMORY;
    }

    g_river_voice_ref.discard_frame = pool_view.discard_frame;

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

    river_audio_frame_ring_reset(&g_river_voice_ref.ring);
    g_river_voice_ref.dropped_frames = 0U;

    rtos_mutex_give(g_river_voice_ref.lock);
}

void river_voice_ref_close(void)
{
    if (!g_river_voice_ref.opened) {
        return;
    }

    river_audio_frame_ring_deinit(&g_river_voice_ref.ring);

    if (g_river_voice_ref.lock != 0) {
        rtos_mutex_delete(g_river_voice_ref.lock);
        g_river_voice_ref.lock = 0;
    }
    river_playback_frame_pool_release_ref();

    memset(&g_river_voice_ref, 0, sizeof(g_river_voice_ref));
}

river_status_t river_voice_ref_push(const uint8_t *data, size_t bytes)
{
    if (!g_river_voice_ref.opened || data == 0 || bytes == 0U) {
        return RIVER_ERR_ARG;
    }
    if (bytes != g_river_voice_ref.frame_bytes) {
        return RIVER_ERR_ARG;
    }

    if (rtos_mutex_take(g_river_voice_ref.lock, MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
        return RIVER_ERR_BUSY;
    }

    {
        river_status_t status;

        status = river_audio_frame_ring_write(&g_river_voice_ref.ring, data);
        if (status == RIVER_ERR_NO_MEMORY) {
            if (g_river_voice_ref.discard_frame != 0 &&
                river_audio_frame_ring_read(&g_river_voice_ref.ring,
                                            g_river_voice_ref.discard_frame) == RIVER_OK) {
                g_river_voice_ref.dropped_frames++;
                status = river_audio_frame_ring_write(&g_river_voice_ref.ring, data);
                if ((g_river_voice_ref.dropped_frames & 0x3FU) == 1U) {
                    RIVER_LOGW("playback ref overflow: dropped=%lu frames=%lu capacity=%lu",
                               (unsigned long)g_river_voice_ref.dropped_frames,
                               (unsigned long)river_audio_frame_ring_count(&g_river_voice_ref.ring),
                               (unsigned long)g_river_voice_ref.frame_capacity);
                }
            }
        }
        rtos_mutex_give(g_river_voice_ref.lock);
        return status;
    }
}

river_status_t river_voice_ref_read(uint8_t *data, size_t bytes)
{
    if (!g_river_voice_ref.opened || data == 0 || bytes == 0U) {
        return RIVER_ERR_ARG;
    }
    if (bytes != g_river_voice_ref.frame_bytes) {
        return RIVER_ERR_ARG;
    }

    if (rtos_mutex_take(g_river_voice_ref.lock, MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
        memset(data, 0, bytes);
        return RIVER_ERR_BUSY;
    }

    if (river_audio_frame_ring_read(&g_river_voice_ref.ring, data) != RIVER_OK) {
        memset(data, 0, bytes);
        rtos_mutex_give(g_river_voice_ref.lock);
        return RIVER_ERR_NOT_FOUND;
    }

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

void river_voice_ref_get_stats(river_voice_ref_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }

    memset(stats, 0, sizeof(*stats));
    if (!g_river_voice_ref.opened) {
        return;
    }

    if (rtos_mutex_take(g_river_voice_ref.lock, MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
        return;
    }

    stats->opened = g_river_voice_ref.opened;
    stats->frame_bytes = g_river_voice_ref.frame_bytes;
    stats->sample_rate = g_river_voice_ref.sample_rate;
    stats->frame_ms = g_river_voice_ref.frame_ms;
    stats->channels = g_river_voice_ref.channels;
    stats->history_ms = g_river_voice_ref.history_ms;
    stats->queue_frames = g_river_voice_ref.ring.frame_count;
    stats->queue_peak_frames = g_river_voice_ref.ring.peak_frame_count;
    stats->queue_capacity_frames = g_river_voice_ref.frame_capacity;
    stats->dropped_frames = g_river_voice_ref.dropped_frames;

    rtos_mutex_give(g_river_voice_ref.lock);
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
