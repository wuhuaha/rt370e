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
    uint8_t *feed_buffer;
    size_t feed_buffer_bytes;
    uint32_t frame_samples;
    uint32_t input_channels;
    uint32_t reference_channels;
    bool reference_enabled;
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

static bool river_voice_preproc_aivoice_policy_uses_reference(void)
{
    return false;
}

static void river_voice_preproc_aivoice_apply_active_policy(struct afe_config *afe_param,
                                                            const river_voice_board_array_profile_t *profile)
{
    *afe_param = (struct afe_config)AFE_CONFIG_ASR_DEFAULT_2MIC50MM();
    afe_param->mic_array = AFE_LINEAR_2MIC_50MM;
    afe_param->sample_rate = (int)profile->sample_rate;
    afe_param->frame_size = (int)((profile->sample_rate * profile->frame_ms) / 1000U);

    /* Keep the active path aligned to wake-word / ASR tuning.
     * Barge-in AEC remains a later optional profile instead of the default. */
    afe_param->afe_mode = AFE_FOR_ASR;
    afe_param->ref_num = 0;
    afe_param->enable_aec = false;
    afe_param->enable_ns = false;
    afe_param->enable_agc = true;
    afe_param->enable_ssl = true;
    afe_param->enable_res = false;
    afe_param->agc_fixed_gain = 10;
    afe_param->enable_adaptive_agc = false;
}

static void river_voice_preproc_aivoice_pack_frame(river_voice_preproc_aivoice_context_t *context,
                                                   const uint8_t *input,
                                                   const uint8_t *reference)
{
    const int16_t *input_samples;
    const int16_t *reference_samples;
    int16_t *feed_samples;
    size_t frame_index;
    uint32_t channel_index;

    input_samples = (const int16_t *)input;
    reference_samples = (const int16_t *)reference;
    feed_samples = (int16_t *)context->feed_buffer;

    for (frame_index = 0; frame_index < context->frame_samples; ++frame_index) {
        for (channel_index = 0; channel_index < context->input_channels; ++channel_index) {
            *feed_samples++ = *input_samples++;
        }
        for (channel_index = 0; channel_index < context->reference_channels; ++channel_index) {
            *feed_samples++ = *reference_samples++;
        }
    }
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
    river_voice_preproc_aivoice_apply_active_policy(&afe_param, profile);
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

    preproc->reference_enabled = river_voice_preproc_aivoice_policy_uses_reference();
    preproc->reference_channels = preproc->reference_enabled ? 1U : 0U;
    preproc->reference_frame_bytes = preproc->reference_enabled ? preproc->output_frame_bytes : 0U;
    preproc->feed_frame_bytes = preproc->input_frame_bytes + preproc->reference_frame_bytes;

    context->frame_samples = (uint32_t)afe_param.frame_size;
    context->input_channels = preproc->input_channels;
    context->reference_channels = preproc->reference_channels;
    context->reference_enabled = preproc->reference_enabled;
    context->feed_buffer_bytes = preproc->feed_frame_bytes;
    if (context->reference_enabled) {
        context->feed_buffer = (uint8_t *)rtos_mem_zmalloc((uint32_t)context->feed_buffer_bytes);
        if (context->feed_buffer == 0) {
            context->iface->destroy(context->handle);
            rtos_mem_free(context);
            printf("[river][voice] aivoice AFE feed buffer alloc failed\n");
            return RIVER_ERR_NO_MEMORY;
        }
    }

    rtk_aivoice_register_callback(context->handle, river_voice_preproc_aivoice_callback, context);
    preproc->backend_ctx = context;
    return RIVER_OK;
}

river_status_t river_voice_preproc_aivoice_process(river_voice_preproc_t *preproc,
                                                   const uint8_t *input,
                                                   size_t input_bytes,
                                                   const uint8_t *reference,
                                                   size_t reference_bytes,
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
    if (input_bytes != preproc->input_frame_bytes) {
        return RIVER_ERR_ARG;
    }

    context->output_ready = false;
    context->output_bytes = 0U;
    if (context->reference_enabled) {
        if (reference == 0 || reference_bytes != preproc->reference_frame_bytes || context->feed_buffer == 0) {
            return RIVER_ERR_ARG;
        }
        river_voice_preproc_aivoice_pack_frame(context, input, reference);
        feed_ret = context->iface->feed(context->handle,
                                        (char *)context->feed_buffer,
                                        (int)context->feed_buffer_bytes);
    } else {
        (void)reference;
        (void)reference_bytes;
        feed_ret = context->iface->feed(context->handle, (char *)input, (int)input_bytes);
    }
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

    if (context->feed_buffer != 0) {
        rtos_mem_free(context->feed_buffer);
        context->feed_buffer = 0;
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
    printf("[river][voice] preproc afe: mode=asr aec=off ns=off agc=on(fixed=10dB) ssl=on ref=staged-off\n");
}
