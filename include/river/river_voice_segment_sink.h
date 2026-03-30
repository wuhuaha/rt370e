/* 语音分段下游接口：把完成的音频片段交给具体上传或处理后端。 */
#ifndef AMEBA_RIVER_VOICE_SEGMENT_SINK_H
#define AMEBA_RIVER_VOICE_SEGMENT_SINK_H

#include <stddef.h>
#include <stdint.h>

#include "river/river_types.h"

typedef struct {
    uint32_t sample_rate;
    uint32_t frame_ms;
    uint32_t pre_roll_ms;
    uint32_t post_roll_ms;
    uint32_t segment_ms;
    uint32_t completed_segments;
    uint32_t dropped_segments;
} river_voice_segment_desc_t;

river_status_t river_voice_segment_sink_submit(const uint8_t *data,
                                               size_t bytes,
                                               const river_voice_segment_desc_t *segment);
const char *river_voice_segment_sink_name(void);
void river_voice_segment_sink_dump_profile(void);

#endif
