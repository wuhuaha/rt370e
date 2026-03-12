#include <stdbool.h>
#include <string.h>

#include "audio/audio_control.h"
#include "audio/audio_record.h"

#include "river/river_log.h"
#include "river/river_voice_board.h"
#include "river/river_voice_capture.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.voice.capture"

river_status_t river_voice_capture_open(river_voice_capture_t *capture)
{
    const river_voice_board_array_profile_t *profile;
    AudioRecordConfig record_config;

    if (capture == 0) {
        return RIVER_ERR_ARG;
    }

    memset(capture, 0, sizeof(*capture));
    profile = river_voice_board_array_profile();

    capture->sample_rate = profile->sample_rate;
    capture->channels = profile->capture_channels;
    capture->frame_ms = profile->frame_ms;
    capture->frame_samples = (profile->sample_rate * profile->frame_ms) / 1000U;
    capture->frame_bytes = capture->frame_samples * capture->channels * sizeof(int16_t);

    AudioControl_SetMicUsage(AUDIO_CAPTURE_USAGE_AMIC);
    AudioControl_SetChannelMicCategory(0, profile->primary_mic);
    AudioControl_SetMicBstGain(profile->primary_mic, profile->primary_mic_gain);
    if (profile->capture_channels > 1U) {
        AudioControl_SetChannelMicCategory(1, profile->secondary_mic);
        AudioControl_SetMicBstGain(profile->secondary_mic, profile->secondary_mic_gain);
    }
    if (profile->aux_mic_reserved) {
        AudioControl_SetChannelMicCategory(2, profile->aux_mic);
        AudioControl_SetMicBstGain(profile->aux_mic, profile->aux_mic_gain);
    }

    capture->record = AudioRecord_Create();
    if (capture->record == 0) {
        RIVER_LOGE("create AudioRecord failed");
        return RIVER_ERR_UNSUPPORTED;
    }

    record_config.sample_rate = capture->sample_rate;
    record_config.channel_count = capture->channels;
    record_config.format = AUDIO_FORMAT_PCM_16_BIT;
    record_config.device = DEVICE_IN_MIC;
    record_config.buffer_bytes = (uint32_t)capture->frame_bytes;
    if (AudioRecord_Init((struct AudioRecord *)capture->record, &record_config, AUDIO_INPUT_FLAG_NONE) != 0) {
        RIVER_LOGE("AudioRecord_Init failed");
        river_voice_capture_close(capture);
        return RIVER_ERR_UNSUPPORTED;
    }

    AudioRecord_SetParameters((struct AudioRecord *)capture->record, "cap_mode=no_afe_pure_data");

    if (AudioRecord_Start((struct AudioRecord *)capture->record) != 0) {
        RIVER_LOGE("AudioRecord_Start failed");
        river_voice_capture_close(capture);
        return RIVER_ERR_UNSUPPORTED;
    }

    capture->started = 1;
    return RIVER_OK;
}

int32_t river_voice_capture_read(river_voice_capture_t *capture, void *buffer, size_t bytes)
{
    if (capture == 0 || capture->record == 0 || buffer == 0 || bytes == 0U) {
        return -1;
    }

    return AudioRecord_Read((struct AudioRecord *)capture->record, buffer, bytes, true);
}

void river_voice_capture_close(river_voice_capture_t *capture)
{
    if (capture == 0) {
        return;
    }

    if (capture->record != 0) {
        if (capture->started) {
            AudioRecord_Stop((struct AudioRecord *)capture->record);
            capture->started = 0;
        }
        AudioRecord_Destroy((struct AudioRecord *)capture->record);
        capture->record = 0;
    }
}

void river_voice_capture_dump_profile(void)
{
    const river_voice_board_array_profile_t *profile;

    profile = river_voice_board_array_profile();
    RIVER_LOGI("capture profile: %lu Hz, %lums, %luch, %s+%s",
               (unsigned long)profile->sample_rate,
               (unsigned long)profile->frame_ms,
               (unsigned long)profile->capture_channels,
               river_voice_board_mic_name(profile->primary_mic),
               river_voice_board_mic_name(profile->secondary_mic));
}
