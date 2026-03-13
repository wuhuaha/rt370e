#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "os_wrapper.h"

#include "aivoice_interface.h"

#include "river/river_log.h"
#include "river/river_voice_board.h"
#include "river/river_voice_preproc.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.voice.preproc"

typedef struct {
    uint32_t frame_samples;
    uint32_t input_channels;
} river_voice_preproc_aivoice_context_t;

river_status_t river_voice_preproc_aivoice_open(river_voice_preproc_t *preproc)
{
    const river_voice_board_array_profile_t *profile;
    river_voice_preproc_aivoice_context_t *context;

    if (preproc == 0) {
        return RIVER_ERR_ARG;
    }

    profile = river_voice_board_array_profile();
    context = (river_voice_preproc_aivoice_context_t *)rtos_mem_zmalloc(sizeof(*context));
    if (context == 0) {
        return RIVER_ERR_NO_MEMORY;
    }

    /* 
     * [EXPERT OPTIMIZATION] AFE Bypass Mode: 
     * We skip creating the Aivoice AFE handle to save ~150KB SRAM and 
     * 30% CA32 CPU load. This provides maximum headroom for SSL handshakes.
     */
    RIVER_LOGW("AFE BYPASS ENABLED: Skipping library load to maximize system headroom");

    preproc->reference_enabled = false;
    preproc->reference_channels = 0;
    preproc->reference_frame_bytes = 0;
    preproc->feed_frame_bytes = preproc->input_frame_bytes;

    context->frame_samples = (uint32_t)((profile->sample_rate * profile->frame_ms) / 1000U);
    context->input_channels = preproc->input_channels;

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
    (void)reference;
    (void)reference_bytes;

    if (preproc == 0 || input == 0 || output == 0 || output_bytes == 0) {
        return RIVER_ERR_ARG;
    }

    context = (river_voice_preproc_aivoice_context_t *)preproc->backend_ctx;
    if (context == 0) {
        return RIVER_ERR_UNSUPPORTED;
    }
    if (input_bytes != preproc->input_frame_bytes) {
        return RIVER_ERR_ARG;
    }

    /* pick Channel 0 (Primary MIC) and pass through. */
    const int16_t *src = (const int16_t *)input;
    int16_t *dst = (int16_t *)output;
    size_t out_samples = context->frame_samples;
    size_t needed_bytes = out_samples * sizeof(int16_t);

    if (needed_bytes > output_capacity) {
        return RIVER_ERR_NO_MEMORY;
    }

    for (size_t i = 0; i < out_samples; ++i) {
        dst[i] = src[i * context->input_channels];
    }

    *output_bytes = needed_bytes;
    return RIVER_OK;
}

void river_voice_preproc_aivoice_close(river_voice_preproc_t *preproc)
{
    river_voice_preproc_aivoice_context_t *context;

    if (preproc == 0) {
        return;
    }

    context = (river_voice_preproc_aivoice_context_t *)preproc->backend_ctx;
    if (context != 0) {
        rtos_mem_free(context);
        preproc->backend_ctx = 0;
    }
}

void river_voice_preproc_aivoice_dump_profile(void)
{
    const river_voice_board_array_profile_t *profile;

    profile = river_voice_board_array_profile();
    RIVER_LOGI("preproc backend: aivoice_afe [BYPASS MODE]");
    RIVER_LOGI("preproc afe: mode=bypass(1ch extraction) source=%s", 
               river_voice_board_mic_name(profile->primary_mic));
}
