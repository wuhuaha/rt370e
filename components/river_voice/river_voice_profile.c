/* 语音配置档表：定义各实验档位的通道、能力与阶段掩码。 */
#include <stddef.h>

#include "platform_autoconf.h"

#include "river/river_voice_profile.h"

#ifndef CONFIG_RIVER_VOICE_CAPABILITY_AEC_EXPERIMENT
#define CONFIG_RIVER_VOICE_CAPABILITY_AEC_EXPERIMENT 1
#endif

#ifndef CONFIG_RIVER_VOICE_CAPABILITY_KWS
#define CONFIG_RIVER_VOICE_CAPABILITY_KWS 0
#endif

#ifndef CONFIG_RIVER_VOICE_CAPABILITY_DOA
#define CONFIG_RIVER_VOICE_CAPABILITY_DOA 0
#endif

#ifndef CONFIG_RIVER_VOICE_CAPABILITY_WAKE_GUIDED_BF
#define CONFIG_RIVER_VOICE_CAPABILITY_WAKE_GUIDED_BF 0
#endif

#define RIVER_VOICE_PROFILE_STAGE_MASK_DEFAULT \
    (RIVER_VOICE_STAGE_MASK_WAKE | RIVER_VOICE_STAGE_MASK_POST_WAKE)

#define RIVER_VOICE_PROFILE_COMMON_CAPABILITIES \
    ((CONFIG_RIVER_VOICE_CAPABILITY_KWS ? RIVER_VOICE_CAPABILITY_KWS : 0U) | \
     (CONFIG_RIVER_VOICE_CAPABILITY_DOA ? RIVER_VOICE_CAPABILITY_DOA : 0U) | \
     (CONFIG_RIVER_VOICE_CAPABILITY_WAKE_GUIDED_BF ? RIVER_VOICE_CAPABILITY_WAKE_GUIDED_BF : 0U))

static const river_voice_profile_config_t g_river_voice_profiles[] = {
    {
        .preproc_profile = RIVER_VOICE_PREPROC_PROFILE_ASR_MAINLINE,
        .name = "asr_mainline",
        .capture_channels = 2U,
        .capture_audio_record_params = "cap_mode=no_afe_pure_data",
        .uses_native_capture_ref = false,
        .experimental = false,
        .stage_mask = RIVER_VOICE_PROFILE_STAGE_MASK_DEFAULT,
        .capability_mask = RIVER_VOICE_PROFILE_COMMON_CAPABILITIES
    },
    {
        .preproc_profile = RIVER_VOICE_PREPROC_PROFILE_FIXED_DSB_WEBRTC_AECM,
        .name = "fixed_dsb_webrtc_aecm",
        .capture_channels = 3U,
        .capture_audio_record_params = "ref_channel=2;cap_mode=no_afe_pure_data",
        .uses_native_capture_ref = true,
        .experimental = true,
        .stage_mask = RIVER_VOICE_PROFILE_STAGE_MASK_DEFAULT,
        .capability_mask = RIVER_VOICE_PROFILE_COMMON_CAPABILITIES |
                           (CONFIG_RIVER_VOICE_CAPABILITY_AEC_EXPERIMENT ?
                                RIVER_VOICE_CAPABILITY_AEC : 0U) |
                           RIVER_VOICE_CAPABILITY_NATIVE_CAPTURE_REF
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

uint32_t river_voice_profile_stage_mask(river_voice_preproc_profile_t profile)
{
    return river_voice_profile_get(profile)->stage_mask;
}

uint32_t river_voice_profile_capability_mask(river_voice_preproc_profile_t profile)
{
    return river_voice_profile_get(profile)->capability_mask;
}

bool river_voice_profile_stage_enabled(river_voice_preproc_profile_t profile, river_voice_stage_t stage)
{
    uint32_t stage_mask;

    stage_mask = river_voice_profile_stage_mask(profile);
    if (stage == RIVER_VOICE_STAGE_WAKE) {
        return (stage_mask & RIVER_VOICE_STAGE_MASK_WAKE) != 0U;
    }
    return (stage_mask & RIVER_VOICE_STAGE_MASK_POST_WAKE) != 0U;
}

bool river_voice_profile_has_capability(river_voice_preproc_profile_t profile, uint32_t capability)
{
    return (river_voice_profile_capability_mask(profile) & capability) != 0U;
}
