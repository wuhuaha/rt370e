/* 固定延时求和预处理实现，可按运行时策略接入 WebRTC AECM。 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "os_wrapper.h"

#include "river/river_log.h"
#include "river/river_voice_board.h"
#include "river/river_voice_preproc.h"
#include "river/river_voice_profile.h"
#include "river/river_voice_runtime_policy.h"

#ifdef CONFIG_RIVER_WEBRTC_AECM_EXPERIMENT_EN
#include "river_voice_webrtc_aecm_adapter.h"
#endif

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.voice.preproc"

typedef struct {
    uint32_t frame_samples;
    uint32_t frame_ms;
    uint32_t input_channels;
    uint32_t secondary_delay_samples;
    int16_t secondary_history[8];
    uint32_t history_count;
#ifdef CONFIG_RIVER_WEBRTC_AECM_EXPERIMENT_EN
    struct {
        int16_t *buffer;
        uint32_t capacity;
        uint32_t head;
        uint32_t tail;
        uint32_t count;
    } dsb_aligned_fifo;
    bool experiment_enabled;
    bool aligned_stream_primed;
    river_voice_webrtc_aecm_adapter_t aecm;
    river_voice_webrtc_aecm_adapter_stats_t aecm_stats;
    river_voice_webrtc_aecm_ref_state_t last_ref_state;
    river_voice_aec_gate_reason_t last_gate_reason;
    bool aec_gate_system_armed;
    int16_t *aecm_mic0;
    int16_t *aecm_mic1;
    uint32_t aec_frames_total;
    uint32_t aec_frames_used;
    uint32_t aec_frames_fallback;
    uint32_t aec_gate_transitions;
    uint32_t aec_gate_disabled;
    uint32_t aec_gate_block_playback;
    uint32_t aec_gate_block_interaction;
    uint32_t aec_gate_block_reference_path;
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

#ifdef CONFIG_RIVER_WEBRTC_AECM_EXPERIMENT_EN
static bool river_voice_preproc_sample_fifo_init(
    river_voice_preproc_fixed_dsb_context_t *context,
    uint32_t capacity_samples)
{
    context->dsb_aligned_fifo.buffer =
        (int16_t *)rtos_mem_zmalloc((uint32_t)(capacity_samples * sizeof(int16_t)));
    if (context->dsb_aligned_fifo.buffer == 0) {
        return false;
    }
    context->dsb_aligned_fifo.capacity = capacity_samples;
    context->dsb_aligned_fifo.head = 0U;
    context->dsb_aligned_fifo.tail = 0U;
    context->dsb_aligned_fifo.count = 0U;
    return true;
}

static void river_voice_preproc_sample_fifo_reset(
    river_voice_preproc_fixed_dsb_context_t *context)
{
    context->dsb_aligned_fifo.head = 0U;
    context->dsb_aligned_fifo.tail = 0U;
    context->dsb_aligned_fifo.count = 0U;
}

static bool river_voice_preproc_sample_fifo_push(
    river_voice_preproc_fixed_dsb_context_t *context,
    const int16_t *samples,
    uint32_t count)
{
    if (context->dsb_aligned_fifo.buffer == 0 ||
        context->dsb_aligned_fifo.count + count > context->dsb_aligned_fifo.capacity) {
        return false;
    }

    for (uint32_t i = 0; i < count; ++i) {
        context->dsb_aligned_fifo.buffer[context->dsb_aligned_fifo.tail] = samples[i];
        context->dsb_aligned_fifo.tail =
            (context->dsb_aligned_fifo.tail + 1U) % context->dsb_aligned_fifo.capacity;
    }
    context->dsb_aligned_fifo.count += count;
    return true;
}

static bool river_voice_preproc_sample_fifo_pop(
    river_voice_preproc_fixed_dsb_context_t *context,
    int16_t *samples,
    uint32_t count)
{
    if (context->dsb_aligned_fifo.buffer == 0 ||
        context->dsb_aligned_fifo.count < count) {
        return false;
    }

    for (uint32_t i = 0; i < count; ++i) {
        samples[i] = context->dsb_aligned_fifo.buffer[context->dsb_aligned_fifo.head];
        context->dsb_aligned_fifo.head =
            (context->dsb_aligned_fifo.head + 1U) % context->dsb_aligned_fifo.capacity;
    }
    context->dsb_aligned_fifo.count -= count;
    return true;
}

static void river_voice_preproc_reset_experiment_path(
    river_voice_preproc_fixed_dsb_context_t *context)
{
    river_voice_webrtc_aecm_adapter_reset(&context->aecm);
    river_voice_preproc_sample_fifo_reset(context);
    context->aligned_stream_primed = false;
    context->last_ref_state = RIVER_VOICE_AECM_REF_STATE_MISSING;
    context->aec_gate_system_armed = false;
    river_voice_runtime_native_reference_reset();
}

static river_voice_reference_activity_t river_voice_preproc_ref_activity_from_adapter(
    river_voice_webrtc_aecm_ref_state_t state)
{
    switch (state) {
    case RIVER_VOICE_AECM_REF_STATE_ACTIVE:
        return RIVER_VOICE_REFERENCE_ACTIVITY_ACTIVE;
    case RIVER_VOICE_AECM_REF_STATE_IDLE:
        return RIVER_VOICE_REFERENCE_ACTIVITY_IDLE;
    case RIVER_VOICE_AECM_REF_STATE_MISSING:
    default:
        return RIVER_VOICE_REFERENCE_ACTIVITY_MISSING;
    }
}

static void river_voice_preproc_publish_native_ref_observation(
    river_voice_preproc_fixed_dsb_context_t *context,
    river_voice_reference_activity_t ref_activity)
{
    if (context == 0) {
        return;
    }

    river_voice_webrtc_aecm_adapter_get_stats(&context->aecm, &context->aecm_stats);
    river_voice_runtime_native_reference_publish(ref_activity,
                                                 context->aecm_stats.last_ref_peak,
                                                 context->aecm_stats.ref_active_ratio_q15,
                                                 context->frame_ms,
                                                 context->aecm_stats.ref_frames_seen);
}

static void river_voice_preproc_note_gate_reason(
    river_voice_preproc_fixed_dsb_context_t *context,
    const river_voice_aec_gate_eval_t *gate_eval,
    river_voice_reference_activity_t ref_activity)
{
    if (context == 0 || gate_eval == 0) {
        return;
    }

    switch (gate_eval->reason) {
    case RIVER_VOICE_AEC_GATE_DISABLED:
        context->aec_gate_disabled++;
        break;
    case RIVER_VOICE_AEC_GATE_BLOCKED_PLAYBACK:
        context->aec_gate_block_playback++;
        break;
    case RIVER_VOICE_AEC_GATE_BLOCKED_INTERACTION:
        context->aec_gate_block_interaction++;
        break;
    case RIVER_VOICE_AEC_GATE_BLOCKED_REFERENCE_PATH:
        context->aec_gate_block_reference_path++;
        break;
    case RIVER_VOICE_AEC_GATE_BLOCKED_REF_IDLE:
        context->aec_ref_idle++;
        break;
    case RIVER_VOICE_AEC_GATE_BLOCKED_REF_MISSING:
        context->aec_ref_missing++;
        break;
    case RIVER_VOICE_AEC_GATE_ACTIVE:
        break;
    default:
        break;
    }

    if (gate_eval->reason != context->last_gate_reason) {
        context->aec_gate_transitions++;
        RIVER_LOGI("webrtc_aecm gate=%s playback=%s interaction=%s ref_path=%s ref_activity=%s native_ref=%u",
                   river_voice_runtime_aec_gate_reason_name(gate_eval->reason),
                   river_playback_service_state_name(gate_eval->playback_state),
                   river_interaction_state_name(gate_eval->interaction_state),
                   river_reference_service_state_name(gate_eval->reference_state),
                   river_voice_runtime_reference_activity_name(ref_activity),
                   gate_eval->uses_native_capture_ref ? 1U : 0U);
        context->last_gate_reason = gate_eval->reason;
    }
}
#endif

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
    context->frame_ms = profile->frame_ms;
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
    river_voice_runtime_native_reference_reset();
    context->last_ref_state = RIVER_VOICE_AECM_REF_STATE_MISSING;
    context->last_gate_reason = RIVER_VOICE_AEC_GATE_DISABLED;
    context->aec_gate_system_armed = false;
    if (context->experiment_enabled) {
        river_voice_webrtc_aecm_ref_policy_t ref_policy;

        if (!river_voice_preproc_sample_fifo_init(context, context->frame_samples * 4U)) {
            river_voice_preproc_fixed_dsb_close(preproc);
            return RIVER_ERR_NO_MEMORY;
        }
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
        river_voice_reference_activity_t ref_activity;
        river_voice_aec_gate_eval_t gate_eval;

        context->aec_frames_total++;

        river_voice_runtime_aec_gate_eval_base(preproc->profile, &gate_eval);
        if (!gate_eval.system_ready) {
            river_voice_preproc_note_gate_reason(context,
                                                 &gate_eval,
                                                 RIVER_VOICE_REFERENCE_ACTIVITY_MISSING);
            if (context->aec_gate_system_armed) {
                river_voice_preproc_reset_experiment_path(context);
            }
            *output_bytes = needed_bytes;
            return RIVER_OK;
        }
        if (!context->aec_gate_system_armed) {
            river_voice_preproc_reset_experiment_path(context);
            context->aec_gate_system_armed = true;
        }

        if (!river_voice_preproc_sample_fifo_push(context, dst, (uint32_t)out_samples)) {
            context->aec_push_fail++;
            river_voice_preproc_reset_experiment_path(context);
            memset(dst, 0, needed_bytes);
            *output_bytes = needed_bytes;
            return RIVER_OK;
        }

        pushed = river_voice_webrtc_aecm_adapter_push_frame(&context->aecm,
                                                            src,
                                                            (int)out_samples,
                                                            (int)context->input_channels);
        if (!pushed) {
            context->aec_push_fail++;
            river_voice_preproc_reset_experiment_path(context);
            context->aec_frames_fallback++;
            memset(dst, 0, needed_bytes);
            *output_bytes = needed_bytes;
            return RIVER_OK;
        }

        ref_state = river_voice_webrtc_aecm_adapter_ref_state(&context->aecm);
        ref_activity = river_voice_preproc_ref_activity_from_adapter(ref_state);
        river_voice_preproc_publish_native_ref_observation(context, ref_activity);
        river_voice_runtime_aec_gate_apply_reference(&gate_eval, ref_activity);
        river_voice_preproc_note_gate_reason(context, &gate_eval, ref_activity);
        if (ref_state != context->last_ref_state) {
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

        if (!context->aligned_stream_primed) {
            if (!river_voice_webrtc_aecm_adapter_output_ready(&context->aecm)) {
                memset(dst, 0, needed_bytes);
                *output_bytes = needed_bytes;
                return RIVER_OK;
            }
            context->aligned_stream_primed = true;
            RIVER_LOGI("webrtc_aecm aligned stream primed: frame=%lu samples block=%d",
                       (unsigned long)out_samples,
                       context->aecm.process_block_samples);
        }

        if (!river_voice_preproc_sample_fifo_pop(context, dst, (uint32_t)out_samples)) {
            context->aec_pop_fail++;
            river_voice_preproc_reset_experiment_path(context);
            memset(dst, 0, needed_bytes);
            *output_bytes = needed_bytes;
            return RIVER_OK;
        }

        if (!river_voice_webrtc_aecm_adapter_output_ready(&context->aecm) ||
            !river_voice_webrtc_aecm_adapter_pop_frame(&context->aecm,
                                                       context->aecm_mic0,
                                                       context->aecm_mic1)) {
            context->aec_pop_fail++;
            river_voice_preproc_reset_experiment_path(context);
            memset(dst, 0, needed_bytes);
            *output_bytes = needed_bytes;
            return RIVER_OK;
        }

        if (!gate_eval.active) {
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
            RIVER_LOGI("webrtc_aecm summary: used=%lu fallback=%lu disabled=%lu block_playback=%lu block_interaction=%lu block_ref_path=%lu ref_missing=%lu ref_idle=%lu gate_transitions=%lu push_fail=%lu pop_fail=%lu total=%lu",
                       (unsigned long)context->aec_frames_used,
                       (unsigned long)context->aec_frames_fallback,
                       (unsigned long)context->aec_gate_disabled,
                       (unsigned long)context->aec_gate_block_playback,
                       (unsigned long)context->aec_gate_block_interaction,
                       (unsigned long)context->aec_gate_block_reference_path,
                       (unsigned long)context->aec_ref_missing,
                       (unsigned long)context->aec_ref_idle,
                       (unsigned long)context->aec_gate_transitions,
                       (unsigned long)context->aec_push_fail,
                       (unsigned long)context->aec_pop_fail,
                       (unsigned long)context->aec_frames_total);
            river_voice_webrtc_aecm_adapter_deinit(&context->aecm);
            river_voice_runtime_native_reference_reset();
        }
        if (context->dsb_aligned_fifo.buffer != 0) {
            rtos_mem_free(context->dsb_aligned_fifo.buffer);
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
    RIVER_LOGI("preproc aecm stats: gate=%s ref_state=%s ratio_q15=%u peak=%u zero_streak=%u aligned=%u pushed=%lu popped=%lu blocks=%lu in_fail=%lu proc_fail=%lu underrun=%lu resets=%lu used=%lu fallback=%lu gate_transitions=%lu",
               river_voice_runtime_aec_gate_reason_name(context->last_gate_reason),
               river_voice_webrtc_aecm_adapter_ref_state_name(context->aecm_stats.ref_state),
               (unsigned int)context->aecm_stats.ref_active_ratio_q15,
               (unsigned int)context->aecm_stats.last_ref_peak,
               (unsigned int)context->aecm_stats.ref_zero_streak,
               context->aligned_stream_primed ? 1U : 0U,
               (unsigned long)context->aecm_stats.frames_pushed,
               (unsigned long)context->aecm_stats.frames_popped,
               (unsigned long)context->aecm_stats.blocks_processed,
               (unsigned long)context->aecm_stats.input_push_failures,
               (unsigned long)context->aecm_stats.process_failures,
               (unsigned long)context->aecm_stats.output_underruns,
               (unsigned long)context->aecm_stats.resets,
               (unsigned long)context->aec_frames_used,
               (unsigned long)context->aec_frames_fallback,
               (unsigned long)context->aec_gate_transitions);
#else
    (void)preproc;
#endif
}

void river_voice_preproc_fixed_dsb_dump_profile(void)
{
    const river_voice_board_array_profile_t *profile;
    river_voice_preproc_profile_t active_profile;
    const char *profile_name;

    profile = river_voice_board_array_profile();
    active_profile = river_voice_profile_active_preproc();
    profile_name = river_voice_profile_name(active_profile);
    if (active_profile == RIVER_VOICE_PREPROC_PROFILE_FIXED_DSB_WEBRTC_AECM) {
#ifdef CONFIG_RIVER_WEBRTC_AECM_EXPERIMENT_EN
        RIVER_LOGI("preproc backend: fixed_dsb + webrtc_aecm [experimental native-3ch-ref]");
        RIVER_LOGI("preproc dsb+aec: profile=%s mode=dual_mic_aec_then_dsb_mix primary=%s secondary=%s spacing=%lumm delay_samples=%u ref=native_capture_ch3 echo_mode=%u aec_delay_ms=%u stream_delay=%lums gate=playback_state+interaction_state+ref_activity",
                   profile_name,
                   river_voice_board_mic_name(profile->primary_mic),
                   river_voice_board_mic_name(profile->secondary_mic),
                   (unsigned long)profile->mic_spacing_mm,
                   (unsigned int)CONFIG_RIVER_VOICE_DSB_SECONDARY_DELAY_SAMPLES,
                   (unsigned int)CONFIG_RIVER_WEBRTC_AECM_ECHO_MODE,
                   (unsigned int)CONFIG_RIVER_WEBRTC_AECM_DELAY_MS,
                   (unsigned long)profile->frame_ms);
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
