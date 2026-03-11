#ifndef AMEBA_RIVER_VOICE_PREPROC_H
#define AMEBA_RIVER_VOICE_PREPROC_H

#include <stddef.h>
#include <stdint.h>

#include "river/river_types.h"

typedef struct river_voice_preproc_ops river_voice_preproc_ops_t;

typedef struct river_voice_preproc {
    const river_voice_preproc_ops_t *ops;
    void *backend_ctx;
    size_t input_frame_bytes;
    size_t output_frame_bytes;
    uint32_t input_channels;
    uint32_t output_channels;
    uint32_t sample_rate;
    uint32_t frame_ms;
} river_voice_preproc_t;

river_status_t river_voice_preproc_open(river_voice_preproc_t *preproc);
river_status_t river_voice_preproc_process(river_voice_preproc_t *preproc,
                                           const uint8_t *input,
                                           size_t input_bytes,
                                           uint8_t *output,
                                           size_t output_capacity,
                                           size_t *output_bytes);
void river_voice_preproc_close(river_voice_preproc_t *preproc);
size_t river_voice_preproc_input_frame_bytes(const river_voice_preproc_t *preproc);
size_t river_voice_preproc_output_frame_bytes(const river_voice_preproc_t *preproc);
uint32_t river_voice_preproc_output_channels(const river_voice_preproc_t *preproc);
const char *river_voice_preproc_backend_name(void);
void river_voice_preproc_dump_profile(void);

#endif
