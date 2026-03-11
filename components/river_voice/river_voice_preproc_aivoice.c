#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "os_wrapper.h"

#include "aivoice_interface.h"

#include "river/river_voice_board.h"
#include "river/river_voice_preproc.h"

typedef struct {
    const struct rtk_aivoice_iface *iface;
    void *handle;
    uint8_t output_buffer[256U * sizeof(int16_t)];
    size_t output_bytes;
    bool output_ready;
} river_voice_preproc_aivoice_context_t;

__attribute__((weak)) afe_ns_mode_e AFE_NS_SIGNAL_SET(void)
{
    return AFE_NS_SIGNAL;
}

static int river_voice_preproc_aivoice_callback(void *user_data,
                                                enum aivoice_out_event_type event_type,
                                                const void *msg,
                                                int len)
{
    river_voice_preproc_aivoice_context_t *context;
    const struct aivoice_evout_afe *afe_out;
    size_t bytes_to_copy;

    (void)len;
    context = (river_voice_preproc_aivoice_context_t *)user_data;
    if (context == 0 || event_type != AIVOICE_EVOUT_AFE || msg == 0) {
        return 0;
    }

    afe_out = (const struct aivoice_evout_afe *)msg;
    if (afe_out->data == 0 || afe_out->ch_num <= 0) {
        return 0;
    }

    bytes_to_copy = (size_t)afe_out->ch_num * 256U * sizeof(int16_t);
    if (bytes_to_copy > sizeof(context->output_buffer)) {
        bytes_to_copy = sizeof(context->output_buffer);
    }

    memcpy(context->output_buffer, afe_out->data, bytes_to_copy);
    context->output_bytes = bytes_to_copy;
    context->output_ready = true;
    return 0;
}

river_status_t river_voice_preproc_aivoice_open(river_voice_preproc_t *preproc)
{
    const river_voice_board_array_profile_t *profile;
    river_voice_preproc_aivoice_context_t *context;
    struct aivoice_config config;
    struct afe_config afe_param;
    struct aivoice_sdk_config common_param;

    if (preproc == 0) {
        return RIVER_ERR_ARG;
    }

    profile = river_voice_board_array_profile();
    context = (river_voice_preproc_aivoice_context_t *)rtos_mem_zmalloc(sizeof(*context));
    if (context == 0) {
        return RIVER_ERR_NO_MEMORY;
    }

    memset(&config, 0, sizeof(config));
    afe_param = (struct afe_config)AFE_CONFIG_ASR_DEFAULT_2MIC50MM();
    afe_param.mic_array = AFE_LINEAR_2MIC_50MM;
    afe_param.ref_num = 0;
    afe_param.sample_rate = (int)profile->sample_rate;
    afe_param.frame_size = (int)((profile->sample_rate * profile->frame_ms) / 1000U);
    afe_param.enable_aec = false;
    afe_param.enable_ns = true;
    afe_param.enable_agc = true;
    afe_param.enable_ssl = false;
    afe_param.ns_mode = AFE_NS_SIGNAL_SET();
    afe_param.ns_aggressive_mode = AFE_NS_AGGR_MID;
    afe_param.ns_cost_mode = AFE_NS_COST_HIGH;
    afe_param.agc_fixed_gain = 9;
    common_param = (struct aivoice_sdk_config)AIVOICE_SDK_CONFIG_DEFAULT();
    common_param.timeout = 5;

    config.afe = &afe_param;
    config.common = &common_param;

    context->iface = &aivoice_iface_afe_v1;
    context->handle = context->iface->create(&config);
    if (context->handle == 0) {
        rtos_mem_free(context);
        printf("[river][voice] aivoice AFE create failed\n");
        return RIVER_ERR_UNSUPPORTED;
    }

    rtk_aivoice_register_callback(context->handle, river_voice_preproc_aivoice_callback, context);
    preproc->backend_ctx = context;
    return RIVER_OK;
}

river_status_t river_voice_preproc_aivoice_process(river_voice_preproc_t *preproc,
                                                   const uint8_t *input,
                                                   size_t input_bytes,
                                                   uint8_t *output,
                                                   size_t output_capacity,
                                                   size_t *output_bytes)
{
    river_voice_preproc_aivoice_context_t *context;
    int feed_ret;

    if (preproc == 0 || input == 0 || output == 0 || output_bytes == 0) {
        return RIVER_ERR_ARG;
    }

    context = (river_voice_preproc_aivoice_context_t *)preproc->backend_ctx;
    if (context == 0 || context->handle == 0) {
        return RIVER_ERR_UNSUPPORTED;
    }

    context->output_ready = false;
    context->output_bytes = 0U;
    feed_ret = context->iface->feed(context->handle, (char *)input, (int)input_bytes);
    if (feed_ret != 0) {
        return RIVER_ERR_IO;
    }

    if (!context->output_ready || context->output_bytes == 0U) {
        return RIVER_ERR_IO;
    }

    if (context->output_bytes > output_capacity) {
        return RIVER_ERR_NO_MEMORY;
    }

    memcpy(output, context->output_buffer, context->output_bytes);
    *output_bytes = context->output_bytes;
    return RIVER_OK;
}

void river_voice_preproc_aivoice_close(river_voice_preproc_t *preproc)
{
    river_voice_preproc_aivoice_context_t *context;

    if (preproc == 0) {
        return;
    }

    context = (river_voice_preproc_aivoice_context_t *)preproc->backend_ctx;
    if (context == 0) {
        return;
    }

    if (context->iface != 0 && context->handle != 0) {
        context->iface->destroy(context->handle);
        context->handle = 0;
    }

    rtos_mem_free(context);
    preproc->backend_ctx = 0;
}

void river_voice_preproc_aivoice_dump_profile(void)
{
    const river_voice_board_array_profile_t *profile;

    profile = river_voice_board_array_profile();
    printf("[river][voice] preproc backend: aivoice_afe %s %lu Hz %lums in=%luch out=1ch\n",
           profile->aivoice_geometry_name,
           (unsigned long)profile->sample_rate,
           (unsigned long)profile->frame_ms,
           (unsigned long)profile->capture_channels);
    printf("[river][voice] preproc afe: aec=off ns=on(mid) agc=on(fixed=9dB) ssl=off ref=0\n");
}
