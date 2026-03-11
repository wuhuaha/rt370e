#ifndef AMEBA_RIVER_VOICE_DETECTOR_H
#define AMEBA_RIVER_VOICE_DETECTOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "river/river_types.h"

typedef struct river_voice_detector_ops river_voice_detector_ops_t;

typedef struct {
    bool decision_valid;
    bool is_speech;
    uint16_t speech_probability_q15;
    uint32_t consumed_samples;
} river_voice_detector_result_t;

typedef struct river_voice_detector {
    const river_voice_detector_ops_t *ops;
    void *backend_ctx;
    size_t input_frame_bytes;
    size_t window_frame_bytes;
    uint32_t sample_rate;
    uint32_t frame_ms;
    uint32_t input_channels;
    bool staged_only;
} river_voice_detector_t;

river_status_t river_voice_detector_open(river_voice_detector_t *detector);
river_status_t river_voice_detector_process(river_voice_detector_t *detector,
                                            const uint8_t *input,
                                            size_t input_bytes,
                                            river_voice_detector_result_t *result);
void river_voice_detector_close(river_voice_detector_t *detector);
size_t river_voice_detector_input_frame_bytes(const river_voice_detector_t *detector);
size_t river_voice_detector_window_frame_bytes(const river_voice_detector_t *detector);
const char *river_voice_detector_backend_name(void);
void river_voice_detector_dump_profile(void);

#endif
