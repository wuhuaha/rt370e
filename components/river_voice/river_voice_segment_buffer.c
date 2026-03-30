/* 语音分段缓冲实现：围绕 VAD 生命周期组织前滚/后滚音频片段。 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "os_wrapper.h"

#include "river/river_voice_segment_buffer.h"

static uint32_t river_voice_segment_buffer_ms_to_frames(uint32_t duration_ms,
                                                        uint32_t frame_ms)
{
    if (duration_ms == 0U || frame_ms == 0U) {
        return 0U;
    }

    return (duration_ms + frame_ms - 1U) / frame_ms;
}

static void *river_voice_segment_buffer_alloc(uint32_t size, bool *from_heap_types)
{
    void *buffer;

    buffer = rtos_heap_types_zmalloc(size, TYPE_DRAM);
    if (buffer != NULL) {
        if (from_heap_types != NULL) {
            *from_heap_types = true;
        }
        return buffer;
    }

    buffer = rtos_mem_zmalloc(size);
    if (from_heap_types != NULL) {
        *from_heap_types = false;
    }
    return buffer;
}

static void river_voice_segment_buffer_free(void *buffer, bool from_heap_types)
{
    if (buffer == NULL) {
        return;
    }

    if (from_heap_types) {
        rtos_heap_types_free(buffer);
    } else {
        rtos_mem_free(buffer);
    }
}

static void river_voice_segment_buffer_pre_roll_store(river_voice_segment_buffer_t *buffer,
                                                      const uint8_t *frame)
{
    uint8_t *dst;

    if (buffer == NULL || frame == NULL || buffer->pre_roll_capacity_frames == 0U) {
        return;
    }

    dst = buffer->pre_roll_buffer +
          ((size_t)buffer->pre_roll_write_index_frames * buffer->config.frame_bytes);
    memcpy(dst, frame, buffer->config.frame_bytes);

    buffer->pre_roll_write_index_frames++;
    if (buffer->pre_roll_write_index_frames >= buffer->pre_roll_capacity_frames) {
        buffer->pre_roll_write_index_frames = 0U;
    }

    if (buffer->pre_roll_count_frames < buffer->pre_roll_capacity_frames) {
        buffer->pre_roll_count_frames++;
    }
}

static river_status_t river_voice_segment_buffer_append(river_voice_segment_buffer_t *buffer,
                                                        const uint8_t *frame)
{
    uint8_t *dst;

    if (buffer == NULL || frame == NULL) {
        return RIVER_ERR_ARG;
    }
    if ((buffer->segment_bytes + buffer->config.frame_bytes) > buffer->max_segment_bytes) {
        buffer->segments_dropped++;
        buffer->active = false;
        buffer->ready = false;
        buffer->segment_bytes = 0U;
        buffer->ready_bytes = 0U;
        buffer->active_frames = 0U;
        buffer->ready_frames = 0U;
        buffer->post_roll_frames_left = 0U;
        buffer->pre_roll_count_frames = 0U;
        buffer->pre_roll_write_index_frames = 0U;
        return RIVER_ERR_NO_MEMORY;
    }

    dst = buffer->segment_buffer + buffer->segment_bytes;
    memcpy(dst, frame, buffer->config.frame_bytes);
    buffer->segment_bytes += (uint32_t)buffer->config.frame_bytes;
    buffer->active_frames++;
    return RIVER_OK;
}

static river_status_t river_voice_segment_buffer_start_segment(river_voice_segment_buffer_t *buffer,
                                                               const uint8_t *frame)
{
    uint32_t frame_index;
    uint32_t read_index;
    river_status_t status;

    if (buffer == NULL || frame == NULL) {
        return RIVER_ERR_ARG;
    }

    if (buffer->ready) {
        buffer->segments_dropped++;
        buffer->ready = false;
        buffer->ready_bytes = 0U;
        buffer->ready_frames = 0U;
    }

    buffer->segment_bytes = 0U;
    buffer->ready_bytes = 0U;
    buffer->active_frames = 0U;
    buffer->ready_frames = 0U;
    buffer->active = true;
    buffer->post_roll_frames_left = buffer->post_roll_frames;
    buffer->segments_started++;

    if (buffer->pre_roll_count_frames > 0U) {
        if (buffer->pre_roll_count_frames == buffer->pre_roll_capacity_frames) {
            read_index = buffer->pre_roll_write_index_frames;
        } else {
            read_index = 0U;
        }

        for (frame_index = 0U; frame_index < buffer->pre_roll_count_frames; ++frame_index) {
            const uint8_t *src = buffer->pre_roll_buffer +
                                 ((size_t)read_index * buffer->config.frame_bytes);
            status = river_voice_segment_buffer_append(buffer, src);
            if (status != RIVER_OK) {
                return status;
            }

            read_index++;
            if (read_index >= buffer->pre_roll_capacity_frames) {
                read_index = 0U;
            }
        }
    }

    return river_voice_segment_buffer_append(buffer, frame);
}

static void river_voice_segment_buffer_finalize_segment(river_voice_segment_buffer_t *buffer)
{
    if (buffer == NULL || !buffer->active) {
        return;
    }

    buffer->ready = true;
    buffer->ready_bytes = buffer->segment_bytes;
    buffer->ready_frames = buffer->active_frames;
    buffer->segments_completed++;
    buffer->active = false;
    buffer->post_roll_frames_left = 0U;
    buffer->pre_roll_count_frames = 0U;
    buffer->pre_roll_write_index_frames = 0U;
}

river_status_t river_voice_segment_buffer_open(river_voice_segment_buffer_t *buffer,
                                               const river_voice_segment_buffer_config_t *config)
{
    uint32_t pre_roll_bytes;

    if (buffer == NULL || config == NULL || config->frame_bytes == 0U ||
        config->frame_ms == 0U || config->sample_rate == 0U) {
        return RIVER_ERR_ARG;
    }

    memset(buffer, 0, sizeof(*buffer));
    buffer->config = *config;
    buffer->pre_roll_capacity_frames =
        river_voice_segment_buffer_ms_to_frames(config->pre_roll_ms, config->frame_ms);
    buffer->post_roll_frames =
        river_voice_segment_buffer_ms_to_frames(config->post_roll_ms, config->frame_ms);
    buffer->max_segment_bytes =
        river_voice_segment_buffer_ms_to_frames(config->max_segment_ms, config->frame_ms) *
        (uint32_t)config->frame_bytes;

    if (buffer->max_segment_bytes == 0U) {
        return RIVER_ERR_ARG;
    }

    pre_roll_bytes = buffer->pre_roll_capacity_frames * (uint32_t)config->frame_bytes;
    if (pre_roll_bytes > 0U) {
        buffer->pre_roll_buffer =
            (uint8_t *)river_voice_segment_buffer_alloc(pre_roll_bytes,
                                                        &buffer->pre_roll_buffer_heap_types);
        if (buffer->pre_roll_buffer == NULL) {
            river_voice_segment_buffer_close(buffer);
            return RIVER_ERR_NO_MEMORY;
        }
    }

    buffer->segment_buffer =
        (uint8_t *)river_voice_segment_buffer_alloc(buffer->max_segment_bytes,
                                                    &buffer->segment_buffer_heap_types);
    if (buffer->segment_buffer == NULL) {
        river_voice_segment_buffer_close(buffer);
        return RIVER_ERR_NO_MEMORY;
    }

    return RIVER_OK;
}

river_status_t river_voice_segment_buffer_push(river_voice_segment_buffer_t *buffer,
                                               const uint8_t *frame,
                                               size_t frame_bytes,
                                               bool is_speech)
{
    river_status_t status;

    if (buffer == NULL || frame == NULL || frame_bytes != buffer->config.frame_bytes) {
        return RIVER_ERR_ARG;
    }

    if (!buffer->active) {
        if (is_speech) {
            status = river_voice_segment_buffer_start_segment(buffer, frame);
            if (status != RIVER_OK) {
                return status;
            }
        } else {
            river_voice_segment_buffer_pre_roll_store(buffer, frame);
        }
        return RIVER_OK;
    }

    status = river_voice_segment_buffer_append(buffer, frame);
    if (status != RIVER_OK) {
        return status;
    }

    if (is_speech) {
        buffer->post_roll_frames_left = buffer->post_roll_frames;
        return RIVER_OK;
    }

    if (buffer->post_roll_frames_left > 0U) {
        buffer->post_roll_frames_left--;
    }
    if (buffer->post_roll_frames_left == 0U) {
        river_voice_segment_buffer_finalize_segment(buffer);
    }

    return RIVER_OK;
}

void river_voice_segment_buffer_get_status(const river_voice_segment_buffer_t *buffer,
                                           river_voice_segment_buffer_status_t *status)
{
    if (status == NULL) {
        return;
    }

    memset(status, 0, sizeof(*status));
    if (buffer == NULL) {
        return;
    }

    status->active = buffer->active;
    status->ready = buffer->ready;
    status->active_frames = buffer->active_frames;
    status->ready_frames = buffer->ready_frames;
    status->ready_bytes = buffer->ready_bytes;
    status->prebuffered_frames = buffer->pre_roll_count_frames;
    status->post_roll_frames_left = buffer->post_roll_frames_left;
    status->segments_started = buffer->segments_started;
    status->segments_completed = buffer->segments_completed;
    status->segments_dropped = buffer->segments_dropped;
}

const uint8_t *river_voice_segment_buffer_ready_data(const river_voice_segment_buffer_t *buffer)
{
    if (buffer == NULL || !buffer->ready) {
        return NULL;
    }
    return buffer->segment_buffer;
}

size_t river_voice_segment_buffer_ready_bytes(const river_voice_segment_buffer_t *buffer)
{
    if (buffer == NULL || !buffer->ready) {
        return 0U;
    }
    return buffer->ready_bytes;
}

void river_voice_segment_buffer_release_ready(river_voice_segment_buffer_t *buffer)
{
    if (buffer == NULL) {
        return;
    }

    buffer->ready = false;
    buffer->ready_bytes = 0U;
    buffer->ready_frames = 0U;
    buffer->segment_bytes = 0U;
    buffer->active_frames = 0U;
}

void river_voice_segment_buffer_close(river_voice_segment_buffer_t *buffer)
{
    if (buffer == NULL) {
        return;
    }

    river_voice_segment_buffer_free(buffer->segment_buffer, buffer->segment_buffer_heap_types);
    river_voice_segment_buffer_free(buffer->pre_roll_buffer,
                                    buffer->pre_roll_buffer_heap_types);
    memset(buffer, 0, sizeof(*buffer));
}
