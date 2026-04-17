/* 语音运行时策略实现：根据交互态和参考活动决定能力门控。 */
#include "river/river_voice_runtime_policy.h"

#include <string.h>

#include "os_wrapper.h"

static bool river_voice_runtime_interaction_allows_aec(river_interaction_state_t state)
{
    return state == RIVER_INTERACTION_SPEAKING ||
           state == RIVER_INTERACTION_BARGE_IN_LISTENING ||
           state == RIVER_INTERACTION_ASR_STREAMING;
}

static bool river_voice_runtime_profile_supports_playback_reference(
    river_voice_preproc_profile_t profile)
{
    return river_voice_profile_has_capability(profile, RIVER_VOICE_CAPABILITY_AEC) ||
           river_voice_profile_has_capability(profile, RIVER_VOICE_CAPABILITY_NATIVE_CAPTURE_REF);
}

static uint32_t river_voice_runtime_reference_recent_window_ms(
    const river_reference_service_stats_t *stats)
{
    uint32_t window_ms;
    uint32_t frame_ms = 0U;

    if (stats != NULL) {
        frame_ms = stats->frame_ms;
    }

    window_ms = frame_ms == 0U ? 240U : frame_ms * 12U;
    if (window_ms < 160U) {
        window_ms = 160U;
    }
    if (window_ms > 480U) {
        window_ms = 480U;
    }
    return window_ms;
}

static river_voice_reference_activity_t river_voice_runtime_reference_activity_from_stats(
    const river_reference_service_stats_t *stats,
    uint32_t *last_write_age_ms_out,
    uint32_t *recent_window_ms_out)
{
    uint32_t now_ms;
    uint32_t recent_window_ms;
    uint32_t last_write_age_ms = 0U;
    bool has_recent_write = false;

    if (stats == NULL) {
        if (last_write_age_ms_out != NULL) {
            *last_write_age_ms_out = 0U;
        }
        if (recent_window_ms_out != NULL) {
            *recent_window_ms_out = 0U;
        }
        return RIVER_VOICE_REFERENCE_ACTIVITY_MISSING;
    }

    recent_window_ms = river_voice_runtime_reference_recent_window_ms(stats);
    now_ms = (uint32_t)rtos_time_get_current_system_time_ms();
    if (stats->last_write_ms != 0U) {
        last_write_age_ms =
            now_ms >= stats->last_write_ms ? (now_ms - stats->last_write_ms) : 0U;
        has_recent_write = last_write_age_ms <= recent_window_ms;
    }

    if (last_write_age_ms_out != NULL) {
        *last_write_age_ms_out = last_write_age_ms;
    }
    if (recent_window_ms_out != NULL) {
        *recent_window_ms_out = recent_window_ms;
    }

    if (stats->state == RIVER_REFERENCE_OPEN &&
        (stats->queue_frames > 0U || has_recent_write)) {
        return RIVER_VOICE_REFERENCE_ACTIVITY_ACTIVE;
    }

    if (stats->write_ok == 0U && stats->queue_peak_frames == 0U &&
        stats->last_write_ms == 0U) {
        return RIVER_VOICE_REFERENCE_ACTIVITY_MISSING;
    }

    return RIVER_VOICE_REFERENCE_ACTIVITY_IDLE;
}

river_voice_stage_t river_voice_runtime_stage(void)
{
    switch (river_interaction_state_get()) {
    case RIVER_INTERACTION_BOOTING:
    case RIVER_INTERACTION_IDLE:
    case RIVER_INTERACTION_WAKE_MONITORING:
        return RIVER_VOICE_STAGE_WAKE;
    case RIVER_INTERACTION_WAKE_CONFIRMED:
    case RIVER_INTERACTION_LISTENING:
    case RIVER_INTERACTION_ASR_STREAMING:
    case RIVER_INTERACTION_THINKING:
    case RIVER_INTERACTION_SPEAKING:
    case RIVER_INTERACTION_BARGE_IN_LISTENING:
    case RIVER_INTERACTION_FOLLOW_UP:
    case RIVER_INTERACTION_ERROR_RECOVERING:
    default:
        return RIVER_VOICE_STAGE_POST_WAKE;
    }
}

const char *river_voice_runtime_stage_name(river_voice_stage_t stage)
{
    switch (stage) {
    case RIVER_VOICE_STAGE_WAKE:
        return "wake";
    case RIVER_VOICE_STAGE_POST_WAKE:
        return "post_wake";
    default:
        return "unknown";
    }
}

bool river_voice_runtime_stage_enabled(river_voice_preproc_profile_t profile,
                                       river_voice_stage_t stage)
{
    return river_voice_profile_stage_enabled(profile, stage);
}

void river_voice_runtime_aec_gate_eval_base(river_voice_preproc_profile_t profile,
                                            river_voice_aec_gate_eval_t *eval)
{
    const river_voice_profile_config_t *profile_config;

    if (eval == 0) {
        return;
    }

    profile_config = river_voice_profile_get(profile);
    eval->profile = profile;
    eval->playback_state = river_playback_service_state();
    eval->interaction_state = river_interaction_state_get();
    eval->reference_state = river_reference_service_state();
    eval->uses_native_capture_ref = profile_config->uses_native_capture_ref;
    eval->experimental_profile = profile_config->experimental;
    eval->system_ready = false;
    eval->active = false;

    if (!profile_config->experimental) {
        eval->reason = RIVER_VOICE_AEC_GATE_DISABLED;
        return;
    }

    if (!river_playback_service_state_active(eval->playback_state)) {
        eval->reason = RIVER_VOICE_AEC_GATE_BLOCKED_PLAYBACK;
        return;
    }

    if (!river_voice_runtime_interaction_allows_aec(eval->interaction_state)) {
        eval->reason = RIVER_VOICE_AEC_GATE_BLOCKED_INTERACTION;
        return;
    }

    if (!profile_config->uses_native_capture_ref &&
        eval->reference_state != RIVER_REFERENCE_OPEN) {
        eval->reason = RIVER_VOICE_AEC_GATE_BLOCKED_REFERENCE_PATH;
        return;
    }

    eval->reason = RIVER_VOICE_AEC_GATE_BLOCKED_REF_MISSING;
    eval->system_ready = true;
}

void river_voice_runtime_aec_gate_apply_reference(river_voice_aec_gate_eval_t *eval,
                                                  river_voice_reference_activity_t ref_activity)
{
    if (eval == 0 || !eval->system_ready) {
        return;
    }

    switch (ref_activity) {
    case RIVER_VOICE_REFERENCE_ACTIVITY_ACTIVE:
        eval->reason = RIVER_VOICE_AEC_GATE_ACTIVE;
        eval->active = true;
        break;
    case RIVER_VOICE_REFERENCE_ACTIVITY_IDLE:
        eval->reason = RIVER_VOICE_AEC_GATE_BLOCKED_REF_IDLE;
        eval->active = false;
        break;
    case RIVER_VOICE_REFERENCE_ACTIVITY_MISSING:
    default:
        eval->reason = RIVER_VOICE_AEC_GATE_BLOCKED_REF_MISSING;
        eval->active = false;
        break;
    }
}

void river_voice_runtime_duplex_ready_eval(bool duplex_experiment_enabled,
                                           river_voice_preproc_profile_t profile,
                                           river_voice_duplex_ready_eval_t *eval)
{
    river_reference_service_stats_t ref_stats;
    river_voice_aec_gate_eval_t aec_eval;
    river_voice_reference_activity_t ref_activity;

    if (eval == NULL) {
        return;
    }

    memset(eval, 0, sizeof(*eval));
    memset(&ref_stats, 0, sizeof(ref_stats));
    memset(&aec_eval, 0, sizeof(aec_eval));

    river_reference_service_get_stats(&ref_stats);
    river_voice_runtime_aec_gate_eval_base(profile, &aec_eval);

    eval->profile = profile;
    eval->duplex_experiment_enabled = duplex_experiment_enabled;
    eval->profile_supports_playback_reference =
        river_voice_runtime_profile_supports_playback_reference(profile);
    eval->playback_state = aec_eval.playback_state;
    eval->interaction_state = aec_eval.interaction_state;
    eval->reference_state = ref_stats.state;
    eval->reference_queue_frames = ref_stats.queue_frames;
    eval->reference_queue_peak_frames = ref_stats.queue_peak_frames;
    eval->uses_native_capture_ref = aec_eval.uses_native_capture_ref;

    if (!duplex_experiment_enabled) {
        eval->reason = RIVER_VOICE_DUPLEX_READY_EXPERIMENT_OFF;
        eval->aec_reason = aec_eval.reason;
        return;
    }

    if (!eval->profile_supports_playback_reference) {
        eval->reason = RIVER_VOICE_DUPLEX_READY_PROFILE_NO_REF;
        eval->aec_reason = aec_eval.reason;
        return;
    }

    if (aec_eval.uses_native_capture_ref) {
        ref_activity = river_playback_service_state_active(aec_eval.playback_state) ?
                           RIVER_VOICE_REFERENCE_ACTIVITY_ACTIVE :
                           RIVER_VOICE_REFERENCE_ACTIVITY_MISSING;
        eval->reference_recent_window_ms = 0U;
        eval->reference_last_write_age_ms = 0U;
    } else {
        ref_activity = river_voice_runtime_reference_activity_from_stats(
            &ref_stats,
            &eval->reference_last_write_age_ms,
            &eval->reference_recent_window_ms);
    }
    eval->reference_activity = ref_activity;

    river_voice_runtime_aec_gate_apply_reference(&aec_eval, ref_activity);
    eval->aec_reason = aec_eval.reason;
    if (aec_eval.active) {
        eval->reason = RIVER_VOICE_DUPLEX_READY_READY;
        eval->ready = true;
        return;
    }

    if (!aec_eval.uses_native_capture_ref &&
        (ref_stats.state != RIVER_REFERENCE_OPEN ||
         ref_activity != RIVER_VOICE_REFERENCE_ACTIVITY_ACTIVE)) {
        eval->reason = RIVER_VOICE_DUPLEX_READY_REF_IDLE;
        return;
    }

    eval->reason = RIVER_VOICE_DUPLEX_READY_AEC_BLOCKED;
}

const char *river_voice_runtime_aec_gate_reason_name(river_voice_aec_gate_reason_t reason)
{
    switch (reason) {
    case RIVER_VOICE_AEC_GATE_DISABLED:
        return "disabled";
    case RIVER_VOICE_AEC_GATE_BLOCKED_PLAYBACK:
        return "playback_inactive";
    case RIVER_VOICE_AEC_GATE_BLOCKED_INTERACTION:
        return "interaction_blocked";
    case RIVER_VOICE_AEC_GATE_BLOCKED_REFERENCE_PATH:
        return "reference_path_blocked";
    case RIVER_VOICE_AEC_GATE_BLOCKED_REF_MISSING:
        return "ref_missing";
    case RIVER_VOICE_AEC_GATE_BLOCKED_REF_IDLE:
        return "ref_idle";
    case RIVER_VOICE_AEC_GATE_ACTIVE:
        return "active";
    default:
        return "unknown";
    }
}

const char *river_voice_runtime_reference_activity_name(river_voice_reference_activity_t activity)
{
    switch (activity) {
    case RIVER_VOICE_REFERENCE_ACTIVITY_MISSING:
        return "missing";
    case RIVER_VOICE_REFERENCE_ACTIVITY_IDLE:
        return "idle";
    case RIVER_VOICE_REFERENCE_ACTIVITY_ACTIVE:
        return "active";
    default:
        return "unknown";
    }
}

const char *river_voice_runtime_duplex_ready_reason_name(
    river_voice_duplex_ready_reason_t reason)
{
    switch (reason) {
    case RIVER_VOICE_DUPLEX_READY_EXPERIMENT_OFF:
        return "experiment_off";
    case RIVER_VOICE_DUPLEX_READY_PROFILE_NO_REF:
        return "profile_no_ref";
    case RIVER_VOICE_DUPLEX_READY_REF_IDLE:
        return "ref_idle";
    case RIVER_VOICE_DUPLEX_READY_AEC_BLOCKED:
        return "aec_blocked";
    case RIVER_VOICE_DUPLEX_READY_READY:
        return "ready";
    default:
        return "unknown";
    }
}
