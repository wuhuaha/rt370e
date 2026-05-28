/* 板级语音拓扑实现：集中固化当前硬件的双麦阵列参数。 */
#include <stdbool.h>

#include "ameba_soc.h"
#include "audio/audio_control.h"

#include "river/river_log.h"
#include "river/river_voice_board.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.voice.board"

static const river_voice_board_array_profile_t g_river_voice_board_array_profile = {
    .board_name = "Orvibo-RTL8730E-PDM",
    .geometry_name = "pdm-2mic-pa-data2",
    .mic_spacing_mm = 50U,
    .sample_rate = 16000U,
    .frame_ms = 16U,
    .capture_channels = 2U,
    .capture_usage = AUDIO_CAPTURE_USAGE_DMIC,
    .primary_mic = AUDIO_DMIC5,
    .secondary_mic = AUDIO_DMIC6,
    .aux_mic = AUDIO_DMIC3,
    .primary_mic_gain = AUDIO_MICBST_GAIN_0DB,
    .secondary_mic_gain = AUDIO_MICBST_GAIN_0DB,
    .aux_mic_gain = AUDIO_MICBST_GAIN_0DB,
    .aux_mic_reserved = false
};

const river_voice_board_array_profile_t *river_voice_board_array_profile(void)
{
    return &g_river_voice_board_array_profile;
}

const char *river_voice_board_mic_name(uint32_t mic_category)
{
    switch (mic_category) {
    case AUDIO_AMIC1:
        return "AMIC1";
    case AUDIO_AMIC2:
        return "AMIC2";
    case AUDIO_AMIC3:
        return "AMIC3";
    case AUDIO_AMIC4:
        return "AMIC4";
    case AUDIO_AMIC5:
        return "AMIC5";
    case AUDIO_DMIC1:
        return "DMIC1";
    case AUDIO_DMIC2:
        return "DMIC2";
    case AUDIO_DMIC3:
        return "DMIC3";
    case AUDIO_DMIC4:
        return "DMIC4";
    case AUDIO_DMIC5:
        return "DMIC5";
    case AUDIO_DMIC6:
        return "DMIC6";
    case AUDIO_DMIC7:
        return "DMIC7";
    case AUDIO_DMIC8:
        return "DMIC8";
    default:
        return "MIC?";
    }
}

const char *river_voice_board_mic_gain_name(uint32_t mic_gain)
{
    switch (mic_gain) {
    case AUDIO_MICBST_GAIN_0DB:
        return "0dB";
    case AUDIO_MICBST_GAIN_5DB:
        return "5dB";
    case AUDIO_MICBST_GAIN_10DB:
        return "10dB";
    case AUDIO_MICBST_GAIN_15DB:
        return "15dB";
    case AUDIO_MICBST_GAIN_20DB:
        return "20dB";
    case AUDIO_MICBST_GAIN_25DB:
        return "25dB";
    case AUDIO_MICBST_GAIN_30DB:
        return "30dB";
    case AUDIO_MICBST_GAIN_35DB:
        return "35dB";
    case AUDIO_MICBST_GAIN_40DB:
        return "40dB";
    default:
        return "?dB";
    }
}

const char *river_voice_board_capture_usage_name(uint32_t capture_usage)
{
    switch (capture_usage) {
    case AUDIO_CAPTURE_USAGE_AMIC:
        return "AMIC";
    case AUDIO_CAPTURE_USAGE_DMIC:
        return "DMIC";
    case AUDIO_CAPTURE_USAGE_DMIC_REF_AMIC:
        return "DMIC_REF_AMIC";
    default:
        return "MIC_USAGE?";
    }
}

void river_voice_board_apply_capture_pinmux(void)
{
    const river_voice_board_array_profile_t *profile;

    profile = river_voice_board_array_profile();
    if (profile->capture_usage != AUDIO_CAPTURE_USAGE_DMIC) {
        return;
    }

#if defined(CONFIG_AMEBASMART) && defined(PINMUX_FUNCTION_DMIC)
    Pinmux_Config(_PA_2, PINMUX_FUNCTION_DMIC);
    Pinmux_Config(_PA_3, PINMUX_FUNCTION_DMIC);
    Pinmux_Config(_PA_4, PINMUX_FUNCTION_DMIC);
    Pinmux_Config(_PA_5, PINMUX_FUNCTION_DMIC);
    Pinmux_Config(_PA_14, PINMUX_FUNCTION_DMIC);
    RIVER_LOGI("capture dmic pinmux applied: group=pa_alt pins=PA2,PA3,PA4,PA5,PA14");
#else
    RIVER_LOGW("capture dmic pinmux skipped: unsupported target");
#endif
}

void river_voice_board_dump_array_profile(void)
{
    const river_voice_board_array_profile_t *profile;

    profile = river_voice_board_array_profile();
    RIVER_LOGI("board array: %s %s usage=%s primary=%s secondary=%s spacing=%lumm",
               profile->board_name,
               profile->geometry_name,
               river_voice_board_capture_usage_name(profile->capture_usage),
               river_voice_board_mic_name(profile->primary_mic),
               river_voice_board_mic_name(profile->secondary_mic),
               (unsigned long)profile->mic_spacing_mm);
    if (profile->aux_mic_reserved) {
        RIVER_LOGI("board array aux: %s reserved for future beamforming/raw tap experiments",
                   river_voice_board_mic_name(profile->aux_mic));
    }
}
