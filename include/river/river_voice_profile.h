#ifndef AMEBA_RIVER_VOICE_PROFILE_H
#define AMEBA_RIVER_VOICE_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    RIVER_VOICE_PREPROC_PROFILE_ASR_MAINLINE = 0,
    RIVER_VOICE_PREPROC_PROFILE_FIXED_DSB_WEBRTC_AECM = 1
} river_voice_preproc_profile_t;

typedef enum {
    RIVER_VOICE_STAGE_WAKE = 0,
    RIVER_VOICE_STAGE_POST_WAKE = 1
} river_voice_stage_t;

enum {
    RIVER_VOICE_STAGE_MASK_WAKE = 1U << 0,
    RIVER_VOICE_STAGE_MASK_POST_WAKE = 1U << 1
};

enum {
    RIVER_VOICE_CAPABILITY_AEC = 1U << 0,
    RIVER_VOICE_CAPABILITY_KWS = 1U << 1,
    RIVER_VOICE_CAPABILITY_DOA = 1U << 2,
    RIVER_VOICE_CAPABILITY_WAKE_GUIDED_BF = 1U << 3,
    RIVER_VOICE_CAPABILITY_NATIVE_CAPTURE_REF = 1U << 4
};

typedef struct {
    river_voice_preproc_profile_t preproc_profile;
    const char *name;
    uint32_t capture_channels;
    const char *capture_audio_record_params;
    bool uses_native_capture_ref;
    bool experimental;
    uint32_t stage_mask;
    uint32_t capability_mask;
} river_voice_profile_config_t;

river_voice_preproc_profile_t river_voice_profile_active_preproc(void);
const river_voice_profile_config_t *river_voice_profile_active(void);
const river_voice_profile_config_t *river_voice_profile_get(river_voice_preproc_profile_t profile);
const char *river_voice_profile_name(river_voice_preproc_profile_t profile);
uint32_t river_voice_profile_capture_channels(river_voice_preproc_profile_t profile);
const char *river_voice_profile_capture_audio_record_params(river_voice_preproc_profile_t profile);
bool river_voice_profile_uses_native_capture_ref(river_voice_preproc_profile_t profile);
bool river_voice_profile_is_experimental(river_voice_preproc_profile_t profile);
uint32_t river_voice_profile_stage_mask(river_voice_preproc_profile_t profile);
uint32_t river_voice_profile_capability_mask(river_voice_preproc_profile_t profile);
bool river_voice_profile_stage_enabled(river_voice_preproc_profile_t profile, river_voice_stage_t stage);
bool river_voice_profile_has_capability(river_voice_preproc_profile_t profile, uint32_t capability);

#endif
