/* Orvibo branch VAD probe stub: VAD is owned by river_orvibo_audio_service. */
#include "river/river_log.h"
#include "river/river_voice.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.voice.probe"

river_status_t river_voice_vad_probe_start(void)
{
    return RIVER_ERR_UNSUPPORTED;
}

river_status_t river_voice_vad_probe_stop(void)
{
    return RIVER_OK;
}

bool river_voice_vad_probe_is_running(void)
{
    return false;
}

const char *river_voice_vad_probe_status_name(void)
{
    return "orvibo_audio_service";
}

void river_voice_vad_probe_set_diag_enabled(bool enabled)
{
    (void)enabled;
}

bool river_voice_vad_probe_diag_enabled(void)
{
    return false;
}

void river_voice_vad_probe_dump_status(void)
{
    RIVER_LOGI("audio_vad_probe=orvibo_audio_service");
    RIVER_LOGI("audio_vad_probe_diag=use river orvibo audio");
}
