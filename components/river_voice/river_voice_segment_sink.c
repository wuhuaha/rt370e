#include <stdio.h>
#include <string.h>

#include "river/river_voice_segment_sink.h"

typedef struct {
    uint32_t submitted_segments;
    uint32_t submit_failures;
    uint32_t last_segment_bytes;
    river_voice_segment_desc_t last_segment;
} river_voice_segment_sink_stub_t;

static river_voice_segment_sink_stub_t g_river_voice_segment_sink_stub;

river_status_t river_voice_segment_sink_submit(const uint8_t *data,
                                               size_t bytes,
                                               const river_voice_segment_desc_t *segment)
{
    if (data == NULL || bytes == 0U || segment == NULL) {
        g_river_voice_segment_sink_stub.submit_failures++;
        return RIVER_ERR_ARG;
    }

    g_river_voice_segment_sink_stub.submitted_segments++;
    g_river_voice_segment_sink_stub.last_segment_bytes = (uint32_t)bytes;
    memcpy(&g_river_voice_segment_sink_stub.last_segment,
           segment,
           sizeof(g_river_voice_segment_sink_stub.last_segment));
    return RIVER_OK;
}

const char *river_voice_segment_sink_name(void)
{
    return "online_asr_stub";
}

void river_voice_segment_sink_dump_profile(void)
{
    printf("[river][voice] segment sink: %s submit-only bytes=%lu segments=%lu fail=%lu\n",
           river_voice_segment_sink_name(),
           (unsigned long)g_river_voice_segment_sink_stub.last_segment_bytes,
           (unsigned long)g_river_voice_segment_sink_stub.submitted_segments,
           (unsigned long)g_river_voice_segment_sink_stub.submit_failures);
}
