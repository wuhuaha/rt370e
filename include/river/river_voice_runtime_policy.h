/* 语音运行时策略接口：集中评估 AEC 门控和系统就绪状态。 */
#ifndef AMEBA_RIVER_VOICE_RUNTIME_POLICY_H
#define AMEBA_RIVER_VOICE_RUNTIME_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#include "river/river_playback_service.h"
#include "river/river_reference_service.h"
#include "river/river_voice_profile.h"

typedef enum {
    RIVER_VOICE_REFERENCE_ACTIVITY_MISSING = 0,
    RIVER_VOICE_REFERENCE_ACTIVITY_IDLE,
    RIVER_VOICE_REFERENCE_ACTIVITY_ACTIVE
} river_voice_reference_activity_t;

typedef struct {
    river_voice_reference_activity_t activity;
    uint32_t frame_ms;
    uint32_t last_update_ms;
    uint32_t frames_seen;
    uint16_t peak;
    uint16_t active_ratio_q15;
    bool available;
} river_voice_native_reference_observation_t;

typedef enum {
    RIVER_VOICE_AEC_GATE_DISABLED = 0,
    RIVER_VOICE_AEC_GATE_BLOCKED_PLAYBACK,
    RIVER_VOICE_AEC_GATE_BLOCKED_PLAYBACK_RESTART_PENDING,
    RIVER_VOICE_AEC_GATE_BLOCKED_INTERACTION,
    RIVER_VOICE_AEC_GATE_BLOCKED_REFERENCE_PATH,
    RIVER_VOICE_AEC_GATE_BLOCKED_REF_MISSING,
    RIVER_VOICE_AEC_GATE_BLOCKED_REF_IDLE,
    RIVER_VOICE_AEC_GATE_ACTIVE
} river_voice_aec_gate_reason_t;

typedef enum {
    RIVER_VOICE_RUNTIME_PLAYBACK_OWNER_NONE = 0,
    RIVER_VOICE_RUNTIME_PLAYBACK_OWNER_ORVIBO
} river_voice_runtime_playback_owner_kind_t;

typedef enum {
    RIVER_VOICE_RUNTIME_ERROR_NONE = 0,
    RIVER_VOICE_RUNTIME_ERROR_RECOVERABLE,
    RIVER_VOICE_RUNTIME_ERROR_FATAL
} river_voice_runtime_error_kind_t;

typedef enum {
    RIVER_VOICE_RUNTIME_INTERACTION_BOOTING = 0,
    RIVER_VOICE_RUNTIME_INTERACTION_IDLE,
    RIVER_VOICE_RUNTIME_INTERACTION_WAKE_MONITORING,
    RIVER_VOICE_RUNTIME_INTERACTION_LISTENING,
    RIVER_VOICE_RUNTIME_INTERACTION_SPEAKING,
    RIVER_VOICE_RUNTIME_INTERACTION_BARGE_IN_LISTENING,
    RIVER_VOICE_RUNTIME_INTERACTION_ERROR_RECOVERING
} river_voice_runtime_interaction_state_t;

typedef struct {
    river_voice_aec_gate_reason_t reason;
    river_playback_state_t playback_state;
    river_voice_runtime_playback_owner_kind_t playback_owner_kind;
    river_voice_runtime_error_kind_t error_kind;
    river_voice_runtime_interaction_state_t interaction_state;
    river_reference_state_t reference_state;
    river_voice_preproc_profile_t profile;
    bool system_ready;
    bool active;
    bool uses_native_capture_ref;
    bool experimental_profile;
} river_voice_aec_gate_eval_t;

typedef enum {
    RIVER_VOICE_DUPLEX_READY_EXPERIMENT_OFF = 0,
    RIVER_VOICE_DUPLEX_READY_PROFILE_NO_REF,
    RIVER_VOICE_DUPLEX_READY_PLAYBACK_RESTART_PENDING,
    RIVER_VOICE_DUPLEX_READY_REF_IDLE,
    RIVER_VOICE_DUPLEX_READY_AEC_BLOCKED,
    RIVER_VOICE_DUPLEX_READY_READY
} river_voice_duplex_ready_reason_t;

typedef struct {
    river_voice_duplex_ready_reason_t reason;
    river_voice_aec_gate_reason_t aec_reason;
    river_playback_state_t playback_state;
    river_voice_runtime_playback_owner_kind_t playback_owner_kind;
    river_voice_runtime_error_kind_t error_kind;
    river_voice_runtime_interaction_state_t interaction_state;
    river_reference_state_t reference_state;
    river_voice_reference_activity_t reference_activity;
    river_voice_preproc_profile_t profile;
    uint32_t reference_queue_frames;
    uint32_t reference_queue_peak_frames;
    uint32_t reference_recent_window_ms;
    uint32_t reference_last_write_age_ms;
    uint32_t native_reference_frames_seen;
    uint16_t native_reference_peak;
    uint16_t native_reference_ratio_q15;
    bool duplex_experiment_enabled;
    bool profile_supports_playback_reference;
    bool native_reference_available;
    bool uses_native_capture_ref;
    bool ready;
} river_voice_duplex_ready_eval_t;

void river_voice_runtime_set_interaction_state(
    river_voice_runtime_interaction_state_t state);
void river_voice_runtime_set_playback_owner(
    river_voice_runtime_playback_owner_kind_t owner);
void river_voice_runtime_set_error_kind(river_voice_runtime_error_kind_t error_kind);
void river_voice_runtime_native_reference_reset(void);
void river_voice_runtime_native_reference_publish(
    river_voice_reference_activity_t activity,
    uint16_t peak,
    uint16_t active_ratio_q15,
    uint32_t frame_ms,
    uint32_t frames_seen);
void river_voice_runtime_native_reference_get(
    river_voice_native_reference_observation_t *observation);
void river_voice_runtime_aec_gate_eval_base(river_voice_preproc_profile_t profile,
                                            river_voice_aec_gate_eval_t *eval);
void river_voice_runtime_aec_gate_apply_reference(river_voice_aec_gate_eval_t *eval,
                                                  river_voice_reference_activity_t ref_activity);
void river_voice_runtime_duplex_ready_eval(bool duplex_experiment_enabled,
                                           river_voice_preproc_profile_t profile,
                                           river_voice_duplex_ready_eval_t *eval);
river_voice_stage_t river_voice_runtime_stage(void);
const char *river_voice_runtime_stage_name(river_voice_stage_t stage);
bool river_voice_runtime_stage_enabled(river_voice_preproc_profile_t profile,
                                       river_voice_stage_t stage);
const char *river_voice_runtime_playback_owner_kind_name(
    river_voice_runtime_playback_owner_kind_t owner);
const char *river_voice_runtime_error_kind_name(river_voice_runtime_error_kind_t error_kind);
const char *river_voice_runtime_interaction_state_name(
    river_voice_runtime_interaction_state_t state);
const char *river_voice_runtime_aec_gate_reason_name(river_voice_aec_gate_reason_t reason);
const char *river_voice_runtime_reference_activity_name(river_voice_reference_activity_t activity);
const char *river_voice_runtime_duplex_ready_reason_name(
    river_voice_duplex_ready_reason_t reason);

#endif
