#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "river/river_voice_board.h"
#include "river/river_voice_detector.h"

#define RIVER_VOICE_DETECTOR_ON_PEAK       1200U
#define RIVER_VOICE_DETECTOR_OFF_PEAK      480U
#define RIVER_VOICE_DETECTOR_ON_AVG_ABS    180U
#define RIVER_VOICE_DETECTOR_OFF_AVG_ABS   72U
#define RIVER_VOICE_DETECTOR_START_MS      32U
#define RIVER_VOICE_DETECTOR_HOLD_MS       240U

static uint16_t river_voice_detector_abs16(int32_t value)
{
    if (value < 0) {
        value = -value;
    }
    if (value > 32767) {
        value = 32767;
    }
    return (uint16_t)value;
}

river_status_t river_voice_detector_open(river_voice_detector_t *detector)
{
    const river_voice_board_array_profile_t *profile;

    if (detector == 0) {
        return RIVER_ERR_ARG;
    }

    memset(detector, 0, sizeof(*detector));
    profile = river_voice_board_array_profile();
    detector->sample_rate = profile->sample_rate;
    detector->frame_ms = profile->frame_ms;
    detector->channels = 1U;
    detector->speech_on_peak = RIVER_VOICE_DETECTOR_ON_PEAK;
    detector->speech_off_peak = RIVER_VOICE_DETECTOR_OFF_PEAK;
    detector->speech_on_avg_abs = RIVER_VOICE_DETECTOR_ON_AVG_ABS;
    detector->speech_off_avg_abs = RIVER_VOICE_DETECTOR_OFF_AVG_ABS;
    detector->start_frames = (RIVER_VOICE_DETECTOR_START_MS + profile->frame_ms - 1U) / profile->frame_ms;
    detector->hold_frames = (RIVER_VOICE_DETECTOR_HOLD_MS + profile->frame_ms - 1U) / profile->frame_ms;
    if (detector->start_frames == 0U) {
        detector->start_frames = 1U;
    }
    if (detector->hold_frames == 0U) {
        detector->hold_frames = 1U;
    }
    return RIVER_OK;
}

bool river_voice_detector_process(river_voice_detector_t *detector,
                                  const uint8_t *buffer,
                                  size_t bytes,
                                  uint16_t *peak_out,
                                  uint16_t *avg_abs_out)
{
    const int16_t *samples;
    size_t sample_count;
    size_t index;
    uint32_t sum_abs;
    uint16_t peak;
    uint16_t avg_abs;
    bool speech_frame;
    bool silence_frame;

    if (detector == 0 || buffer == 0 || bytes < sizeof(int16_t)) {
        return false;
    }

    samples = (const int16_t *)buffer;
    sample_count = bytes / sizeof(int16_t);
    sum_abs = 0U;
    peak = 0U;

    for (index = 0; index < sample_count; ++index) {
        uint16_t abs_sample;

        abs_sample = river_voice_detector_abs16(samples[index]);
        sum_abs += abs_sample;
        if (abs_sample > peak) {
            peak = abs_sample;
        }
    }

    avg_abs = sample_count == 0U ? 0U : (uint16_t)(sum_abs / sample_count);
    if (peak_out != 0) {
        *peak_out = peak;
    }
    if (avg_abs_out != 0) {
        *avg_abs_out = avg_abs;
    }

    speech_frame = (peak >= detector->speech_on_peak) || (avg_abs >= detector->speech_on_avg_abs);
    silence_frame = (peak <= detector->speech_off_peak) && (avg_abs <= detector->speech_off_avg_abs);

    if (!detector->speech_active) {
        if (speech_frame) {
            detector->speech_frames++;
            if (detector->speech_frames >= detector->start_frames) {
                detector->speech_active = true;
                detector->speech_frames = detector->start_frames;
                detector->silence_frames = 0U;
            }
        } else {
            detector->speech_frames = 0U;
        }
        return detector->speech_active;
    }

    if (silence_frame) {
        detector->silence_frames++;
        if (detector->silence_frames >= detector->hold_frames) {
            detector->speech_active = false;
            detector->speech_frames = 0U;
            detector->silence_frames = detector->hold_frames;
        }
    } else {
        detector->silence_frames = 0U;
    }

    return detector->speech_active;
}

void river_voice_detector_close(river_voice_detector_t *detector)
{
    if (detector == 0) {
        return;
    }

    memset(detector, 0, sizeof(*detector));
}

const char *river_voice_detector_backend_name(void)
{
    return "energy_vad";
}

void river_voice_detector_dump_profile(void)
{
    printf("[river][voice] detector backend: %s start=%luf hold=%luf on_peak=%lu off_peak=%lu on_avg=%lu off_avg=%lu\n",
           river_voice_detector_backend_name(),
           (unsigned long)river_voice_board_array_profile()->frame_ms == 0U ? 0UL :
               (unsigned long)((RIVER_VOICE_DETECTOR_START_MS + river_voice_board_array_profile()->frame_ms - 1U) /
                               river_voice_board_array_profile()->frame_ms),
           (unsigned long)river_voice_board_array_profile()->frame_ms == 0U ? 0UL :
               (unsigned long)((RIVER_VOICE_DETECTOR_HOLD_MS + river_voice_board_array_profile()->frame_ms - 1U) /
                               river_voice_board_array_profile()->frame_ms),
           (unsigned long)RIVER_VOICE_DETECTOR_ON_PEAK,
           (unsigned long)RIVER_VOICE_DETECTOR_OFF_PEAK,
           (unsigned long)RIVER_VOICE_DETECTOR_ON_AVG_ABS,
           (unsigned long)RIVER_VOICE_DETECTOR_OFF_AVG_ABS);
}
