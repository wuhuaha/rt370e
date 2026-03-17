#include <stddef.h>

#include "river/river_voice_profile.h"

static const river_voice_profile_config_t g_river_voice_profiles[] = {
    {
        .preproc_profile = RIVER_VOICE_PREPROC_PROFILE_ASR_MAINLINE,
        .name = "asr_mainline",
        .capture_channels = 2U,
        .capture_audio_record_params = "cap_mode=no_afe_pure_data",
        .uses_native_capture_ref = false,
        .experimental = false
    },
    {
        .preproc_profile = RIVER_VOICE_PREPROC_PROFILE_FIXED_DSB_WEBRTC_AECM,
        .name = "fixed_dsb_webrtc_aecm",
        .capture_channels = 3U,
        .capture_audio_record_params = "ref_channel=2;cap_mode=no_afe_pure_data",
        .uses_native_capture_ref = true,
        .experimental = true
    }
};

river_voice_preproc_profile_t river_voice_profile_active_preproc(void)
{
#ifdef CONFIG_RIVER_VOICE_PREPROC_PROFILE_FIXED_DSB_WEBRTC_AECM
    return RIVER_VOICE_PREPROC_PROFILE_FIXED_DSB_WEBRTC_AECM;
#else
    return RIVER_VOICE_PREPROC_PROFILE_ASR_MAINLINE;
#endif
}

const river_voice_profile_config_t *river_voice_profile_get(river_voice_preproc_profile_t profile)
{
    size_t index;

    for (index = 0U; index < (sizeof(g_river_voice_profiles) / sizeof(g_river_voice_profiles[0])); ++index) {
        if (g_river_voice_profiles[index].preproc_profile == profile) {
            return &g_river_voice_profiles[index];
        }
    }

    return &g_river_voice_profiles[0];
}

const river_voice_profile_config_t *river_voice_profile_active(void)
{
    return river_voice_profile_get(river_voice_profile_active_preproc());
}

const char *river_voice_profile_name(river_voice_preproc_profile_t profile)
{
    return river_voice_profile_get(profile)->name;
}

uint32_t river_voice_profile_capture_channels(river_voice_preproc_profile_t profile)
{
    return river_voice_profile_get(profile)->capture_channels;
}

const char *river_voice_profile_capture_audio_record_params(river_voice_preproc_profile_t profile)
{
    return river_voice_profile_get(profile)->capture_audio_record_params;
}

bool river_voice_profile_uses_native_capture_ref(river_voice_preproc_profile_t profile)
{
    return river_voice_profile_get(profile)->uses_native_capture_ref;
}

bool river_voice_profile_is_experimental(river_voice_preproc_profile_t profile)
{
    return river_voice_profile_get(profile)->experimental;
}
