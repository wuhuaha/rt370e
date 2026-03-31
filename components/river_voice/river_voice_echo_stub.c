/* 音频 echo 调试桩实现：关闭板级 loopback 调试路径时保持接口稳定。 */
#include "river/river_log.h"
#include "river/river_voice.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.voice.echo"

river_status_t river_voice_echo_start(void)
{
    return RIVER_ERR_UNSUPPORTED;
}

river_status_t river_voice_echo_stop(void)
{
    return RIVER_ERR_UNSUPPORTED;
}

bool river_voice_echo_is_running(void)
{
    return false;
}

const char *river_voice_echo_status_name(void)
{
    return "compiled_out";
}

void river_voice_echo_set_diag_enabled(bool enabled)
{
    (void)enabled;
}

bool river_voice_echo_diag_enabled(void)
{
    return false;
}

void river_voice_echo_dump_status(void)
{
    RIVER_LOGI("audio_echo=compiled=no");
    RIVER_LOGI("audio_echo_diag=off");
}
