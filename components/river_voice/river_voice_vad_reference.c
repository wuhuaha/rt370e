#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "os_wrapper.h"

#include "aivoice_interface.h"

#include "river/river_log.h"
#include "river/river_voice_board.h"
#include "river/river_voice_vad_reference.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.voice.vadref"

typedef struct {
    const struct rtk_aivoice_iface *iface;
    void *handle;
    size_t input_frame_bytes;
    bool opened;
    bool is_speech;
    uint32_t total_events;
    uint32_t total_speech_start;
    uint32_t total_speech_end;
    uint32_t total_feed_ok;
    uint32_t total_feed_fail;
    uint32_t last_offset_ms;
} river_voice_vad_reference_context_t;

static river_voice_vad_reference_context_t g_river_voice_vad_reference;

static int river_voice_vad_reference_callback(void *user_data,
                                              enum aivoice_out_event_type event_type,
                                              const void *msg,
                                              int len)
{
    river_voice_vad_reference_context_t *context;
    const struct aivoice_evout_vad *vad_out;

    (void)len;
    context = (river_voice_vad_reference_context_t *)user_data;
    if (context == 0 || event_type != AIVOICE_EVOUT_VAD || msg == 0) {
        return 0;
    }

    vad_out = (const struct aivoice_evout_vad *)msg;
    context->total_events++;
    context->last_offset_ms = vad_out->offset_ms;
    if (vad_out->status != 0) {
        context->is_speech = true;
        context->total_speech_start++;
    } else {
        context->is_speech = false;
        context->total_speech_end++;
    }

    return 0;
}

river_status_t river_voice_vad_reference_open(void)
{
#ifndef CONFIG_RIVER_AIVOICE_VAD_REFERENCE_EN
    return RIVER_ERR_UNSUPPORTED;
#else
    const river_voice_board_array_profile_t *profile;
    struct aivoice_config config;
    struct afe_config afe_param;
    struct vad_config vad_param;
    struct aivoice_sdk_config common_param;

    if (g_river_voice_vad_reference.opened) {
        return RIVER_OK;
    }

    memset(&g_river_voice_vad_reference, 0, sizeof(g_river_voice_vad_reference));
    profile = river_voice_board_array_profile();

    memset(&config, 0, sizeof(config));
    afe_param = (struct afe_config)AFE_CONFIG_ASR_DEFAULT_1MIC();
    afe_param.sample_rate = (int)profile->sample_rate;
    afe_param.frame_size = (int)((profile->sample_rate * profile->frame_ms) / 1000U);
    afe_param.ref_num = 0;
    afe_param.enable_aec = false;
    afe_param.enable_ns = false;
    afe_param.enable_agc = false;
    afe_param.enable_ssl = false;
    afe_param.enable_res = false;

    vad_param = (struct vad_config)VAD_CONFIG_DEFAULT();
    common_param = (struct aivoice_sdk_config)AIVOICE_SDK_CONFIG_DEFAULT();
    common_param.timeout = 5;

    config.afe = &afe_param;
    config.vad = &vad_param;
    config.common = &common_param;

    g_river_voice_vad_reference.iface = &aivoice_iface_vad_v1;
    g_river_voice_vad_reference.handle = g_river_voice_vad_reference.iface->create(&config);
    if (g_river_voice_vad_reference.handle == 0) {
        RIVER_LOGW("sdk_vad reference create failed");
        return RIVER_ERR_UNSUPPORTED;
    }

    rtk_aivoice_register_callback(g_river_voice_vad_reference.handle,
                                  river_voice_vad_reference_callback,
                                  &g_river_voice_vad_reference);
    g_river_voice_vad_reference.input_frame_bytes =
        ((profile->sample_rate * profile->frame_ms) / 1000U) * sizeof(int16_t);
    g_river_voice_vad_reference.opened = true;
    return RIVER_OK;
#endif
}

river_status_t river_voice_vad_reference_process(const uint8_t *input, size_t input_bytes)
{
#ifndef CONFIG_RIVER_AIVOICE_VAD_REFERENCE_EN
    (void)input;
    (void)input_bytes;
    return RIVER_ERR_UNSUPPORTED;
#else
    if (!g_river_voice_vad_reference.opened || g_river_voice_vad_reference.handle == 0) {
        return RIVER_ERR_UNSUPPORTED;
    }
    if (input == 0 || input_bytes != g_river_voice_vad_reference.input_frame_bytes) {
        g_river_voice_vad_reference.total_feed_fail++;
        return RIVER_ERR_ARG;
    }
    if (g_river_voice_vad_reference.iface->feed(g_river_voice_vad_reference.handle,
                                                (char *)input,
                                                (int)input_bytes) != 0) {
        g_river_voice_vad_reference.total_feed_fail++;
        return RIVER_ERR_IO;
    }

    g_river_voice_vad_reference.total_feed_ok++;
    return RIVER_OK;
#endif
}

void river_voice_vad_reference_close(void)
{
#ifdef CONFIG_RIVER_AIVOICE_VAD_REFERENCE_EN
    if (g_river_voice_vad_reference.iface != 0 && g_river_voice_vad_reference.handle != 0) {
        g_river_voice_vad_reference.iface->destroy(g_river_voice_vad_reference.handle);
    }
#endif
    memset(&g_river_voice_vad_reference, 0, sizeof(g_river_voice_vad_reference));
}

void river_voice_vad_reference_get_status(river_voice_vad_reference_status_t *status)
{
    if (status == 0) {
        return;
    }

    status->opened = g_river_voice_vad_reference.opened;
    status->is_speech = g_river_voice_vad_reference.is_speech;
    status->total_events = g_river_voice_vad_reference.total_events;
    status->total_speech_start = g_river_voice_vad_reference.total_speech_start;
    status->total_speech_end = g_river_voice_vad_reference.total_speech_end;
    status->total_feed_ok = g_river_voice_vad_reference.total_feed_ok;
    status->total_feed_fail = g_river_voice_vad_reference.total_feed_fail;
    status->last_offset_ms = g_river_voice_vad_reference.last_offset_ms;
}

const char *river_voice_vad_reference_name(void)
{
#ifdef CONFIG_RIVER_AIVOICE_VAD_REFERENCE_EN
    return "aivoice_vad_v1_ref";
#else
    return "disabled";
#endif
}

void river_voice_vad_reference_dump_profile(void)
{
#ifdef CONFIG_RIVER_AIVOICE_VAD_REFERENCE_EN
    RIVER_LOGI("detector reference: aivoice_vad_v1 diagnostic-only sensitivity=mid left_margin=300ms right_margin=160ms min_speech=200ms feed=256 samples");
#else
    RIVER_LOGI("detector reference: disabled");
#endif
}
