#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "os_wrapper.h"
#include "river/river_log.h"
#include "river/river_voice_vad_reference.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.voice.vadref"

river_status_t river_voice_vad_reference_init(void)
{
    RIVER_LOGI("detector reference: aivoice_vad [DISABLED BY CONFIG]");
    return RIVER_OK;
}

/* Match exactly the declaration in river_voice_vad_reference.h: river_status_t river_voice_vad_reference_open(void) */
river_status_t river_voice_vad_reference_open(void)
{
    return RIVER_OK;
}

river_status_t river_voice_vad_reference_process(const uint8_t *input, size_t input_bytes)
{
    (void)input;
    (void)input_bytes;
    return RIVER_OK;
}

void river_voice_vad_reference_close(void)
{
}

void river_voice_vad_reference_get_status(river_voice_vad_reference_status_t *status)
{
    if (status) {
        memset(status, 0, sizeof(*status));
    }
}

river_status_t river_voice_vad_reference_feed(const uint8_t *pcm, size_t bytes)
{
    (void)pcm;
    (void)bytes;
    return RIVER_OK;
}

void river_voice_vad_reference_reset(void)
{
}

void river_voice_vad_reference_dump_status(void)
{
    RIVER_LOGI("  aivoice_vad_ref=disabled");
}

void river_voice_vad_reference_dump_profile(void)
{
    RIVER_LOGI("detector reference: disabled");
}

const char *river_voice_vad_reference_name(void)
{
    return "disabled";
}
