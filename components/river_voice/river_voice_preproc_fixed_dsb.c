#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "os_wrapper.h"

#include "river/river_log.h"
#include "river/river_voice_board.h"
#include "river/river_voice_preproc.h"

#ifdef CONFIG_RIVER_WEBRTC_AECM_EXPERIMENT_EN
#include "river_voice_webrtc_aecm_adapter.h"
#endif

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.voice.preproc"

typedef struct {
    uint32_t frame_samples;
    uint32_t input_channels;
    uint32_t secondary_delay_samples;
    int16_t secondary_history[8];
    uint32_t history_count;
#ifdef CONFIG_RIVER_WEBRTC_AECM_EXPERIMENT_EN
    bool experiment_enabled;
    river_voice_webrtc_aecm_adapter_t aecm;
    river_voice_webrtc_aecm_adapter_stats_t aecm_stats;
    river_voice_webrtc_aecm_ref_state_t last_ref_state;
    int16_t *aecm_mic0;
    int16_t *aecm_mic1;
    uint32_t aec_frames_total;
    uint32_t aec_frames_used;
    uint32_t aec_frames_fallback;
    uint32_t aec_ref_missing;
    uint32_t aec_ref_idle;
    uint32_t aec_push_fail;
    uint32_t aec_pop_fail;
#endif
} river_voice_preproc_fixed_dsb_context_t;

void river_voice_preproc_fixed_dsb_close(river_voice_preproc_t *preproc);
void river_voice_preproc_fixed_dsb_dump_runtime_stats(const river_voice_preproc_t *preproc);

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

#ifdef CONFIG_RIVER_WEBRTC_AECM_EXPERIMENT_EN
    context->experiment_enabled =
        preproc->profile == RIVER_VOICE_PREPROC_PROFILE_FIXED_DSB_WEBRTC_AECM;
    context->last_ref_state = RIVER_VOICE_AECM_REF_STATE_MISSING;
    if (context->experiment_enabled) {
        river_voice_webrtc_aecm_ref_policy_t ref_policy;

        context->aecm_mic0 =
            (int16_t *)rtos_mem_zmalloc((uint32_t)(context->frame_samples * sizeof(int16_t)));
        context->aecm_mic1 =
            (int16_t *)rtos_mem_zmalloc((uint32_t)(context->frame_samples * sizeof(int16_t)));
        if (context->aecm_mic0 == 0 || context->aecm_mic1 == 0) {
            river_voice_preproc_fixed_dsb_close(preproc);
            return RIVER_ERR_NO_MEMORY;
        }

        if (!river_voice_webrtc_aecm_adapter_init(&context->aecm,
                                                  (int)profile->sample_rate,
                                                  (int16_t)CONFIG_RIVER_WEBRTC_AECM_ECHO_MODE,
                                                  (int16_t)CONFIG_RIVER_WEBRTC_AECM_DELAY_MS,
                                                  (int)context->frame_samples)) {
            river_voice_preproc_fixed_dsb_close(preproc);
            return RIVER_ERR_UNSUPPORTED;
        }

        ref_policy.enter_peak = (uint16_t)CONFIG_RIVER_WEBRTC_AECM_REF_ENTER_PEAK;
        ref_policy.exit_peak = (uint16_t)CONFIG_RIVER_WEBRTC_AECM_REF_EXIT_PEAK;
        ref_policy.stable_frames = (uint16_t)CONFIG_RIVER_WEBRTC_AECM_REF_STABLE_FRAMES;
        ref_policy.hangover_frames = (uint16_t)CONFIG_RIVER_WEBRTC_AECM_REF_HANGOVER_FRAMES;
        ref_policy.active_window_frames =
            (uint16_t)CONFIG_RIVER_WEBRTC_AECM_REF_ACTIVE_WINDOW_FRAMES;
        river_voice_webrtc_aecm_adapter_set_ref_policy(&context->aecm, &ref_policy);
    }
#endif

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
    const int16_t *src;
    int16_t *dst;
    size_t out_samples;
    size_t needed_bytes;
    uint32_t delay;
    uint32_t delay_index;

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

    (void)reference;
    (void)reference_bytes;

    src = (const int16_t *)input;
    dst = (int16_t *)output;
    out_samples = context->frame_samples;
    needed_bytes = out_samples * sizeof(int16_t);
    delay = context->secondary_delay_samples;
    delay_index = 0U;

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

#ifdef CONFIG_RIVER_WEBRTC_AECM_EXPERIMENT_EN
    if (context->experiment_enabled) {
        bool pushed = false;
        river_voice_webrtc_aecm_ref_state_t ref_state;

        context->aec_frames_total++;

        pushed = river_voice_webrtc_aecm_adapter_push_frame(&context->aecm,
                                                            src,
                                                            (int)out_samples,
                                                            (int)context->input_channels);
        if (!pushed) {
            context->aec_push_fail++;
            river_voice_webrtc_aecm_adapter_reset(&context->aecm);
            context->last_ref_state = RIVER_VOICE_AECM_REF_STATE_MISSING;
            context->aec_frames_fallback++;
            *output_bytes = needed_bytes;
            return RIVER_OK;
        }

        ref_state = river_voice_webrtc_aecm_adapter_ref_state(&context->aecm);
        if (ref_state != context->last_ref_state) {
            river_voice_webrtc_aecm_adapter_get_stats(&context->aecm, &context->aecm_stats);
            RIVER_LOGI("webrtc_aecm ref_state=%s peak=%u ratio_q15=%u pushed=%lu popped=%lu blocks=%lu resets=%lu",
                       river_voice_webrtc_aecm_adapter_ref_state_name(ref_state),
                       (unsigned int)context->aecm_stats.last_ref_peak,
                       (unsigned int)context->aecm_stats.ref_active_ratio_q15,
                       (unsigned long)context->aecm_stats.frames_pushed,
                       (unsigned long)context->aecm_stats.frames_popped,
                       (unsigned long)context->aecm_stats.blocks_processed,
                       (unsigned long)context->aecm_stats.resets);
            context->last_ref_state = ref_state;
        }

        if (ref_state != RIVER_VOICE_AECM_REF_STATE_ACTIVE) {
            if (ref_state == RIVER_VOICE_AECM_REF_STATE_IDLE) {
                context->aec_ref_idle++;
            } else {
                context->aec_ref_missing++;
            }
            while (river_voice_webrtc_aecm_adapter_output_ready(&context->aecm)) {
                if (!river_voice_webrtc_aecm_adapter_pop_frame(&context->aecm,
                                                               context->aecm_mic0,
                                                               context->aecm_mic1)) {
                    break;
                }
            }
            context->aec_frames_fallback++;
            *output_bytes = needed_bytes;
            return RIVER_OK;
        }

        if (!river_voice_webrtc_aecm_adapter_output_ready(&context->aecm) ||
            !river_voice_webrtc_aecm_adapter_pop_frame(&context->aecm,
                                                       context->aecm_mic0,
                                                       context->aecm_mic1)) {
            context->aec_pop_fail++;
            context->aec_frames_fallback++;
            *output_bytes = needed_bytes;
            return RIVER_OK;
        }

        for (size_t i = 0; i < out_samples; ++i) {
            int32_t mixed = (int32_t)context->aecm_mic0[i] + (int32_t)context->aecm_mic1[i];
            dst[i] = river_voice_preproc_clamp_q15(mixed / 2);
        }
        context->aec_frames_used++;
    }
#endif

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
#ifdef CONFIG_RIVER_WEBRTC_AECM_EXPERIMENT_EN
        if (context->experiment_enabled) {
            RIVER_LOGI("webrtc_aecm summary: used=%lu fallback=%lu ref_missing=%lu ref_idle=%lu push_fail=%lu pop_fail=%lu total=%lu",
                       (unsigned long)context->aec_frames_used,
                       (unsigned long)context->aec_frames_fallback,
                       (unsigned long)context->aec_ref_missing,
                       (unsigned long)context->aec_ref_idle,
                       (unsigned long)context->aec_push_fail,
                       (unsigned long)context->aec_pop_fail,
                       (unsigned long)context->aec_frames_total);
            river_voice_webrtc_aecm_adapter_deinit(&context->aecm);
        }
        if (context->aecm_mic1 != 0) {
            rtos_mem_free(context->aecm_mic1);
        }
        if (context->aecm_mic0 != 0) {
            rtos_mem_free(context->aecm_mic0);
        }
#endif
        rtos_mem_free(context);
        preproc->backend_ctx = 0;
    }
}

void river_voice_preproc_fixed_dsb_dump_runtime_stats(const river_voice_preproc_t *preproc)
{
#ifdef CONFIG_RIVER_WEBRTC_AECM_EXPERIMENT_EN
    river_voice_preproc_fixed_dsb_context_t *context;

    if (preproc == 0) {
        return;
    }

    context = (river_voice_preproc_fixed_dsb_context_t *)preproc->backend_ctx;
    if (context == 0 || !context->experiment_enabled) {
        return;
    }

    river_voice_webrtc_aecm_adapter_get_stats(&context->aecm, &context->aecm_stats);
    RIVER_LOGI("preproc aecm stats: ref_state=%s ratio_q15=%u peak=%u pushed=%lu popped=%lu blocks=%lu in_fail=%lu proc_fail=%lu underrun=%lu resets=%lu used=%lu fallback=%lu",
               river_voice_webrtc_aecm_adapter_ref_state_name(context->aecm_stats.ref_state),
               (unsigned int)context->aecm_stats.ref_active_ratio_q15,
               (unsigned int)context->aecm_stats.last_ref_peak,
               (unsigned long)context->aecm_stats.frames_pushed,
               (unsigned long)context->aecm_stats.frames_popped,
               (unsigned long)context->aecm_stats.blocks_processed,
               (unsigned long)context->aecm_stats.input_push_failures,
               (unsigned long)context->aecm_stats.process_failures,
               (unsigned long)context->aecm_stats.output_underruns,
               (unsigned long)context->aecm_stats.resets,
               (unsigned long)context->aec_frames_used,
               (unsigned long)context->aec_frames_fallback);
#else
    (void)preproc;
#endif
}

void river_voice_preproc_fixed_dsb_dump_profile(void)
{
    const river_voice_board_array_profile_t *profile;
    const char *profile_name;

    profile = river_voice_board_array_profile();
    profile_name = river_voice_preproc_profile_name();
    if (strcmp(profile_name, "fixed_dsb_webrtc_aecm") == 0) {
#ifdef CONFIG_RIVER_WEBRTC_AECM_EXPERIMENT_EN
        RIVER_LOGI("preproc backend: fixed_dsb + webrtc_aecm [experimental native-3ch-ref]");
        RIVER_LOGI("preproc dsb+aec: profile=%s mode=fixed_delay_and_sum_then_aec primary=%s secondary=%s spacing=%lumm delay_samples=%u ref=native_capture_ch3 echo_mode=%u aec_delay_ms=%u",
                   profile_name,
                   river_voice_board_mic_name(profile->primary_mic),
                   river_voice_board_mic_name(profile->secondary_mic),
                   (unsigned long)profile->mic_spacing_mm,
                   (unsigned int)CONFIG_RIVER_VOICE_DSB_SECONDARY_DELAY_SAMPLES,
                   (unsigned int)CONFIG_RIVER_WEBRTC_AECM_ECHO_MODE,
                   (unsigned int)CONFIG_RIVER_WEBRTC_AECM_DELAY_MS);
        RIVER_LOGI("preproc aec gate: enter_peak=%u exit_peak=%u stable=%u hangover=%u window=%u",
                   (unsigned int)CONFIG_RIVER_WEBRTC_AECM_REF_ENTER_PEAK,
                   (unsigned int)CONFIG_RIVER_WEBRTC_AECM_REF_EXIT_PEAK,
                   (unsigned int)CONFIG_RIVER_WEBRTC_AECM_REF_STABLE_FRAMES,
                   (unsigned int)CONFIG_RIVER_WEBRTC_AECM_REF_HANGOVER_FRAMES,
                   (unsigned int)CONFIG_RIVER_WEBRTC_AECM_REF_ACTIVE_WINDOW_FRAMES);
#else
        RIVER_LOGI("preproc backend: fixed_dsb [software beamformer]");
        RIVER_LOGW("experimental profile selected but WebRTC AECM experiment assets are disabled at build time; runtime falls back to fixed_dsb");
#endif
    } else {
        RIVER_LOGI("preproc backend: fixed_dsb [software beamformer]");
        RIVER_LOGI("preproc dsb: profile=%s mode=fixed_delay_and_sum primary=%s secondary=%s spacing=%lumm delay_samples=%u output=mono focus=broadside/asr",
                   profile_name,
                   river_voice_board_mic_name(profile->primary_mic),
                   river_voice_board_mic_name(profile->secondary_mic),
                   (unsigned long)profile->mic_spacing_mm,
                   (unsigned int)CONFIG_RIVER_VOICE_DSB_SECONDARY_DELAY_SAMPLES);
    }
}
