/* 对话云端端口：把 voice/runtime 与具体 cloud adapter 解绑。 */
#include <string.h>

#include "river/river_dialog_cloud_port.h"

typedef struct {
    bool registered;
    river_dialog_cloud_port_t port;
} river_dialog_cloud_port_context_t;

static river_dialog_cloud_port_context_t g_river_dialog_cloud_port;

river_status_t river_dialog_cloud_port_register(const river_dialog_cloud_port_t *port)
{
    if (port == NULL) {
        return RIVER_ERR_ARG;
    }

    memset(&g_river_dialog_cloud_port, 0, sizeof(g_river_dialog_cloud_port));
    g_river_dialog_cloud_port.port = *port;
    g_river_dialog_cloud_port.registered = true;
    return RIVER_OK;
}

bool river_dialog_cloud_port_registered(void)
{
    return g_river_dialog_cloud_port.registered;
}

const char *river_dialog_cloud_provider_name(void)
{
    if (g_river_dialog_cloud_port.registered &&
        g_river_dialog_cloud_port.port.provider_name != NULL) {
        return g_river_dialog_cloud_port.port.provider_name();
    }
    return "-";
}

bool river_dialog_cloud_asr_streaming_supported(void)
{
    return g_river_dialog_cloud_port.registered &&
           g_river_dialog_cloud_port.port.asr_streaming_supported != NULL &&
           g_river_dialog_cloud_port.port.asr_streaming_supported();
}

bool river_dialog_cloud_asr_batch_supported(void)
{
    return g_river_dialog_cloud_port.registered &&
           g_river_dialog_cloud_port.port.asr_batch_supported != NULL &&
           g_river_dialog_cloud_port.port.asr_batch_supported();
}

river_status_t river_dialog_cloud_asr_audio_open(const river_dialog_asr_audio_desc_t *audio,
                                                 uint32_t pre_roll_ms,
                                                 uint32_t post_roll_ms)
{
    if (audio == NULL) {
        return RIVER_ERR_ARG;
    }
    if (!g_river_dialog_cloud_port.registered ||
        g_river_dialog_cloud_port.port.asr_audio_open == NULL) {
        return RIVER_ERR_UNSUPPORTED;
    }

    return g_river_dialog_cloud_port.port.asr_audio_open(audio, pre_roll_ms, post_roll_ms);
}

void river_dialog_cloud_asr_audio_close(void)
{
    if (g_river_dialog_cloud_port.registered &&
        g_river_dialog_cloud_port.port.asr_audio_close != NULL) {
        g_river_dialog_cloud_port.port.asr_audio_close();
    }
}

river_status_t river_dialog_cloud_asr_stream_push_frame(const uint8_t *pcm,
                                                        size_t bytes,
                                                        bool is_speech)
{
    if (pcm == NULL || bytes == 0U) {
        return RIVER_ERR_ARG;
    }
    if (!g_river_dialog_cloud_port.registered ||
        g_river_dialog_cloud_port.port.asr_stream_push_frame == NULL) {
        return RIVER_ERR_UNSUPPORTED;
    }

    return g_river_dialog_cloud_port.port.asr_stream_push_frame(pcm, bytes, is_speech);
}

river_status_t river_dialog_cloud_asr_batch_submit_segment(
    const uint8_t *pcm,
    size_t bytes,
    const river_voice_segment_desc_t *segment)
{
    if (pcm == NULL || bytes == 0U || segment == NULL) {
        return RIVER_ERR_ARG;
    }
    if (!g_river_dialog_cloud_port.registered ||
        g_river_dialog_cloud_port.port.asr_batch_submit_segment == NULL) {
        return RIVER_ERR_UNSUPPORTED;
    }

    return g_river_dialog_cloud_port.port.asr_batch_submit_segment(pcm, bytes, segment);
}

river_status_t river_dialog_cloud_interrupt_tts_with_reason(const char *reason)
{
    if (reason == NULL || reason[0] == '\0') {
        return RIVER_ERR_ARG;
    }
    if (!g_river_dialog_cloud_port.registered ||
        g_river_dialog_cloud_port.port.interrupt_tts_with_reason == NULL) {
        return RIVER_ERR_UNSUPPORTED;
    }

    return g_river_dialog_cloud_port.port.interrupt_tts_with_reason(reason);
}

bool river_dialog_cloud_conversation_window_active(void)
{
    return g_river_dialog_cloud_port.registered &&
           g_river_dialog_cloud_port.port.conversation_window_active != NULL &&
           g_river_dialog_cloud_port.port.conversation_window_active();
}
