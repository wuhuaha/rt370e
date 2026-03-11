#ifndef AMEBA_RIVER_VOICE_DETECTOR_H
#define AMEBA_RIVER_VOICE_DETECTOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "river/river_types.h"

typedef struct {
    uint32_t sample_rate;
    uint32_t frame_ms;
    uint32_t channels;
    uint32_t speech_on_peak;
    uint32_t speech_off_peak;
    uint32_t speech_on_avg_abs;
    uint32_t speech_off_avg_abs;
    uint32_t start_frames;
    uint32_t hold_frames;
    uint32_t speech_frames;
    uint32_t silence_frames;
    bool speech_active;
} river_voice_detector_t;

river_status_t river_voice_detector_open(river_voice_detector_t *detector);
bool river_voice_detector_process(river_voice_detector_t *detector,
                                  const uint8_t *buffer,
                                  size_t bytes,
                                  uint16_t *peak_out,
                                  uint16_t *avg_abs_out);
void river_voice_detector_close(river_voice_detector_t *detector);
const char *river_voice_detector_backend_name(void);
void river_voice_detector_dump_profile(void);

#endif
