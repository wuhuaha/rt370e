#ifndef AMEBA_RIVER_VOICE_PROFILE_H
#define AMEBA_RIVER_VOICE_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    RIVER_VOICE_PREPROC_PROFILE_ASR_MAINLINE = 0,
    RIVER_VOICE_PREPROC_PROFILE_FIXED_DSB_WEBRTC_AECM = 1
} river_voice_preproc_profile_t;

typedef struct {
    river_voice_preproc_profile_t preproc_profile;
    const char *name;
    uint32_t capture_channels;
    const char *capture_audio_record_params;
    bool uses_native_capture_ref;
    bool experimental;
} river_voice_profile_config_t;

river_voice_preproc_profile_t river_voice_profile_active_preproc(void);
const river_voice_profile_config_t *river_voice_profile_active(void);
const river_voice_profile_config_t *river_voice_profile_get(river_voice_preproc_profile_t profile);
const char *river_voice_profile_name(river_voice_preproc_profile_t profile);
uint32_t river_voice_profile_capture_channels(river_voice_preproc_profile_t profile);
const char *river_voice_profile_capture_audio_record_params(river_voice_preproc_profile_t profile);
bool river_voice_profile_uses_native_capture_ref(river_voice_preproc_profile_t profile);
bool river_voice_profile_is_experimental(river_voice_preproc_profile_t profile);

#endif
