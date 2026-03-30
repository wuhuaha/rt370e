/* 语音参考数据接口：管理回放参考流的打开、写入和读取。 */
#ifndef AMEBA_RIVER_VOICE_REF_H
#define AMEBA_RIVER_VOICE_REF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "river/river_types.h"

typedef struct {
    bool opened;
    size_t frame_bytes;
    uint32_t sample_rate;
    uint32_t frame_ms;
    uint32_t channels;
    uint32_t history_ms;
    uint32_t queue_frames;
    uint32_t queue_peak_frames;
    uint32_t queue_capacity_frames;
    uint32_t dropped_frames;
} river_voice_ref_stats_t;

river_status_t river_voice_ref_open(uint32_t sample_rate,
                                    uint32_t frame_ms,
                                    uint32_t channels,
                                    uint32_t history_ms);
void river_voice_ref_reset(void);
void river_voice_ref_close(void);
river_status_t river_voice_ref_push(const uint8_t *data, size_t bytes);
river_status_t river_voice_ref_read(uint8_t *data, size_t bytes);
bool river_voice_ref_is_open(void);
const char *river_voice_ref_backend_name(void);
void river_voice_ref_get_stats(river_voice_ref_stats_t *stats);
void river_voice_ref_dump_profile(void);

#endif
