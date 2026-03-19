#ifndef AMEBA_RIVER_VOICE_RUNTIME_POLICY_H
#define AMEBA_RIVER_VOICE_RUNTIME_POLICY_H

#include <stdbool.h>

#include "river/river_interaction_state.h"
#include "river/river_playback_service.h"
#include "river/river_reference_service.h"
#include "river/river_voice_profile.h"

typedef enum {
    RIVER_VOICE_REFERENCE_ACTIVITY_MISSING = 0,
    RIVER_VOICE_REFERENCE_ACTIVITY_IDLE,
    RIVER_VOICE_REFERENCE_ACTIVITY_ACTIVE
} river_voice_reference_activity_t;

typedef enum {
    RIVER_VOICE_AEC_GATE_DISABLED = 0,
    RIVER_VOICE_AEC_GATE_BLOCKED_PLAYBACK,
    RIVER_VOICE_AEC_GATE_BLOCKED_INTERACTION,
    RIVER_VOICE_AEC_GATE_BLOCKED_REFERENCE_PATH,
    RIVER_VOICE_AEC_GATE_BLOCKED_REF_MISSING,
    RIVER_VOICE_AEC_GATE_BLOCKED_REF_IDLE,
    RIVER_VOICE_AEC_GATE_ACTIVE
} river_voice_aec_gate_reason_t;

typedef struct {
    river_voice_aec_gate_reason_t reason;
    river_playback_state_t playback_state;
    river_interaction_state_t interaction_state;
    river_reference_state_t reference_state;
    river_voice_preproc_profile_t profile;
    bool system_ready;
    bool active;
    bool uses_native_capture_ref;
    bool experimental_profile;
} river_voice_aec_gate_eval_t;

void river_voice_runtime_aec_gate_eval_base(river_voice_preproc_profile_t profile,
                                            river_voice_aec_gate_eval_t *eval);
void river_voice_runtime_aec_gate_apply_reference(river_voice_aec_gate_eval_t *eval,
                                                  river_voice_reference_activity_t ref_activity);
river_voice_stage_t river_voice_runtime_stage(void);
const char *river_voice_runtime_stage_name(river_voice_stage_t stage);
bool river_voice_runtime_stage_enabled(river_voice_preproc_profile_t profile,
                                       river_voice_stage_t stage);
const char *river_voice_runtime_aec_gate_reason_name(river_voice_aec_gate_reason_t reason);
const char *river_voice_runtime_reference_activity_name(river_voice_reference_activity_t activity);

#endif
