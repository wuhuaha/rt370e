/* Segment sink stub for the Orvibo live graph. */
#include "river/river_log.h"
#include "river/river_voice_segment_sink.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.voice.segment"

river_status_t river_voice_segment_sink_submit(const uint8_t *data,
                                               size_t bytes,
                                               const river_voice_segment_desc_t *segment)
{
    (void)data;
    (void)bytes;
    (void)segment;
    return RIVER_ERR_UNSUPPORTED;
}

const char *river_voice_segment_sink_name(void)
{
    return "orvibo_stream_only";
}

void river_voice_segment_sink_dump_profile(void)
{
    RIVER_LOGI("segment sink: %s", river_voice_segment_sink_name());
}
