#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "os_wrapper.h"

#include "river/river_log.h"
#include "river/river_voice_board.h"
#include "river/river_voice_preproc.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.voice.preproc"

typedef struct {
    uint32_t frame_samples;
    uint32_t input_channels;
    uint32_t secondary_delay_samples;
    int16_t secondary_history[8];
    uint32_t history_count;
} river_voice_preproc_fixed_dsb_context_t;

static int16_t river_voice_preproc_clamp_q15(int32_t value)
{
    if (value > 32767) {
        return 32767;
    }
    if (value < -32768) {
        return -32768;
    }
    return (int16_t)value;
}

river_status_t river_voice_preproc_fixed_dsb_open(river_voice_preproc_t *preproc)
{
    const river_voice_board_array_profile_t *profile;
    river_voice_preproc_fixed_dsb_context_t *context;

    if (preproc == 0) {
        return RIVER_ERR_ARG;
    }

    profile = river_voice_board_array_profile();
    context = (river_voice_preproc_fixed_dsb_context_t *)rtos_mem_zmalloc(sizeof(*context));
    if (context == 0) {
        return RIVER_ERR_NO_MEMORY;
    }

    preproc->reference_enabled = false;
    preproc->reference_channels = 0;
    preproc->reference_frame_bytes = 0;
    preproc->feed_frame_bytes = preproc->input_frame_bytes;

    context->frame_samples = (uint32_t)((profile->sample_rate * profile->frame_ms) / 1000U);
    context->input_channels = preproc->input_channels;
    context->secondary_delay_samples = (uint32_t)CONFIG_RIVER_VOICE_DSB_SECONDARY_DELAY_SAMPLES;
    if (context->secondary_delay_samples > (sizeof(context->secondary_history) / sizeof(context->secondary_history[0]))) {
        context->secondary_delay_samples = (sizeof(context->secondary_history) / sizeof(context->secondary_history[0]));
    }
    context->history_count = context->secondary_delay_samples;
    memset(context->secondary_history, 0, sizeof(context->secondary_history));

    preproc->backend_ctx = context;
    return RIVER_OK;
}

river_status_t river_voice_preproc_fixed_dsb_process(river_voice_preproc_t *preproc,
                                                     const uint8_t *input,
                                                     size_t input_bytes,
                                                     const uint8_t *reference,
                                                     size_t reference_bytes,
                                                     uint8_t *output,
                                                     size_t output_capacity,
                                                     size_t *output_bytes)
{
    river_voice_preproc_fixed_dsb_context_t *context;
    (void)reference;
    (void)reference_bytes;

    if (preproc == 0 || input == 0 || output == 0 || output_bytes == 0) {
        return RIVER_ERR_ARG;
    }

    context = (river_voice_preproc_fixed_dsb_context_t *)preproc->backend_ctx;
    if (context == 0) {
        return RIVER_ERR_UNSUPPORTED;
    }
    if (input_bytes != preproc->input_frame_bytes) {
        return RIVER_ERR_ARG;
    }

    const int16_t *src = (const int16_t *)input;
    int16_t *dst = (int16_t *)output;
    size_t out_samples = context->frame_samples;
    size_t needed_bytes = out_samples * sizeof(int16_t);
    uint32_t delay = context->secondary_delay_samples;
    uint32_t delay_index = 0U;

    if (needed_bytes > output_capacity) {
        return RIVER_ERR_NO_MEMORY;
    }

    for (size_t i = 0; i < out_samples; ++i) {
        int16_t primary = src[i * context->input_channels];
        int16_t secondary = src[(i * context->input_channels) + 1U];
        int16_t aligned_secondary;
        int32_t mixed;

        if (delay > 0U) {
            aligned_secondary = context->secondary_history[delay_index];
            context->secondary_history[delay_index] = secondary;
            delay_index++;
            if (delay_index >= delay) {
                delay_index = 0U;
            }
        } else {
            aligned_secondary = secondary;
        }

        mixed = (int32_t)primary + (int32_t)aligned_secondary;
        dst[i] = river_voice_preproc_clamp_q15(mixed / 2);
    }

    *output_bytes = needed_bytes;
    return RIVER_OK;
}

void river_voice_preproc_fixed_dsb_close(river_voice_preproc_t *preproc)
{
    river_voice_preproc_fixed_dsb_context_t *context;

    if (preproc == 0) {
        return;
    }

    context = (river_voice_preproc_fixed_dsb_context_t *)preproc->backend_ctx;
    if (context != 0) {
        rtos_mem_free(context);
        preproc->backend_ctx = 0;
    }
}

void river_voice_preproc_fixed_dsb_dump_profile(void)
{
    const river_voice_board_array_profile_t *profile;

    profile = river_voice_board_array_profile();
    RIVER_LOGI("preproc backend: fixed_dsb [software beamformer]");
    RIVER_LOGI("preproc dsb: mode=fixed_delay_and_sum primary=%s secondary=%s spacing=%lumm delay_samples=%u output=mono focus=broadside/asr",
               river_voice_board_mic_name(profile->primary_mic),
               river_voice_board_mic_name(profile->secondary_mic),
               (unsigned long)profile->mic_spacing_mm,
               (unsigned int)CONFIG_RIVER_VOICE_DSB_SECONDARY_DELAY_SAMPLES);
}
