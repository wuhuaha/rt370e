#include <string.h>

#include "river/river_cloud.h"
#include "river/river_log.h"
#include "river/river_voice_segment_sink.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.voice.segment"

typedef struct {
    uint32_t submitted_segments;
    uint32_t unsupported_segments;
    uint32_t submit_failures;
    uint32_t submitted_bytes;
    uint32_t last_segment_bytes;
    river_voice_segment_desc_t last_segment;
} river_voice_segment_sink_bridge_t;

static river_voice_segment_sink_bridge_t g_river_voice_segment_sink_bridge;

river_status_t river_voice_segment_sink_submit(const uint8_t *data,
                                               size_t bytes,
                                               const river_voice_segment_desc_t *segment)
{
    if (data == NULL || bytes == 0U || segment == NULL) {
        g_river_voice_segment_sink_bridge.submit_failures++;
        return RIVER_ERR_ARG;
    }

    switch (river_cloud_asr_batch_submit_segment(data, bytes, segment)) {
    case RIVER_OK:
        break;
    case RIVER_ERR_UNSUPPORTED:
        g_river_voice_segment_sink_bridge.unsupported_segments++;
        return RIVER_ERR_UNSUPPORTED;
    default:
        g_river_voice_segment_sink_bridge.submit_failures++;
        return RIVER_ERR_IO;
    }

    g_river_voice_segment_sink_bridge.submitted_segments++;
    g_river_voice_segment_sink_bridge.submitted_bytes += (uint32_t)bytes;
    g_river_voice_segment_sink_bridge.last_segment_bytes = (uint32_t)bytes;
    memcpy(&g_river_voice_segment_sink_bridge.last_segment,
           segment,
           sizeof(g_river_voice_segment_sink_bridge.last_segment));
    return RIVER_OK;
}

const char *river_voice_segment_sink_name(void)
{
    return "cloud_asr_batch_bridge";
}

void river_voice_segment_sink_dump_profile(void)
{
    RIVER_LOGI("segment sink: %s provider=%s stream=%s batch=%s total_bytes=%lu last_bytes=%lu segments=%lu unsupported=%lu fail=%lu",
               river_voice_segment_sink_name(),
               river_cloud_asr_provider_name(),
               river_cloud_asr_streaming_supported() ? "yes" : "no",
               river_cloud_asr_batch_supported() ? "yes" : "no",
               (unsigned long)g_river_voice_segment_sink_bridge.submitted_bytes,
               (unsigned long)g_river_voice_segment_sink_bridge.last_segment_bytes,
               (unsigned long)g_river_voice_segment_sink_bridge.submitted_segments,
               (unsigned long)g_river_voice_segment_sink_bridge.unsupported_segments,
               (unsigned long)g_river_voice_segment_sink_bridge.submit_failures);
}
