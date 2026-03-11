#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "river/river_voice_board.h"
#include "river/river_voice_detector.h"

struct river_voice_detector_ops {
    const char *name;
    river_status_t (*open)(river_voice_detector_t *detector);
    river_status_t (*process)(river_voice_detector_t *detector,
                              const uint8_t *input,
                              size_t input_bytes,
                              river_voice_detector_result_t *result);
    void (*close)(river_voice_detector_t *detector);
    void (*dump_profile)(void);
};

river_status_t river_voice_detector_silero_open(river_voice_detector_t *detector);
river_status_t river_voice_detector_silero_process(river_voice_detector_t *detector,
                                                   const uint8_t *input,
                                                   size_t input_bytes,
                                                   river_voice_detector_result_t *result);
void river_voice_detector_silero_close(river_voice_detector_t *detector);
void river_voice_detector_silero_dump_profile(void);

static const river_voice_detector_ops_t g_river_voice_detector_ops = {
    .name = "silero_vad",
    .open = river_voice_detector_silero_open,
    .process = river_voice_detector_silero_process,
    .close = river_voice_detector_silero_close,
    .dump_profile = river_voice_detector_silero_dump_profile
};

river_status_t river_voice_detector_open(river_voice_detector_t *detector)
{
    const river_voice_board_array_profile_t *profile;

    if (detector == 0) {
        return RIVER_ERR_ARG;
    }

    memset(detector, 0, sizeof(*detector));
    profile = river_voice_board_array_profile();
    detector->ops = &g_river_voice_detector_ops;
    detector->sample_rate = profile->sample_rate;
    detector->frame_ms = profile->frame_ms;
    detector->input_channels = 1U;
    detector->input_frame_bytes = ((profile->sample_rate * profile->frame_ms) / 1000U) *
                                  detector->input_channels * sizeof(int16_t);
    return detector->ops->open(detector);
}

river_status_t river_voice_detector_process(river_voice_detector_t *detector,
                                            const uint8_t *input,
                                            size_t input_bytes,
                                            river_voice_detector_result_t *result)
{
    if (detector == 0 || detector->ops == 0 || detector->ops->process == 0) {
        return RIVER_ERR_ARG;
    }

    return detector->ops->process(detector, input, input_bytes, result);
}

void river_voice_detector_close(river_voice_detector_t *detector)
{
    if (detector == 0 || detector->ops == 0 || detector->ops->close == 0) {
        return;
    }

    detector->ops->close(detector);
    detector->backend_ctx = 0;
}

size_t river_voice_detector_input_frame_bytes(const river_voice_detector_t *detector)
{
    return detector == 0 ? 0U : detector->input_frame_bytes;
}

size_t river_voice_detector_window_frame_bytes(const river_voice_detector_t *detector)
{
    return detector == 0 ? 0U : detector->window_frame_bytes;
}

const char *river_voice_detector_backend_name(void)
{
    return g_river_voice_detector_ops.name;
}

void river_voice_detector_dump_profile(void)
{
    if (g_river_voice_detector_ops.dump_profile != 0) {
        g_river_voice_detector_ops.dump_profile();
    }
}
