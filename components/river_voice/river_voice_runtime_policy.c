#include "river/river_voice_runtime_policy.h"

static bool river_voice_runtime_interaction_allows_aec(river_interaction_state_t state)
{
    return state == RIVER_INTERACTION_SPEAKING ||
           state == RIVER_INTERACTION_BARGE_IN_LISTENING ||
           state == RIVER_INTERACTION_ASR_STREAMING;
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
