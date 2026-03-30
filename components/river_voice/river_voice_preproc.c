/* 预处理抽象层：统一调度不同的波束形成/AEC 预处理后端。 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "river/river_voice_board.h"
#include "river/river_voice_preproc.h"
#include "river/river_voice_profile.h"

struct river_voice_preproc_ops {
    const char *name;
    river_status_t (*open)(river_voice_preproc_t *preproc);
    river_status_t (*process)(river_voice_preproc_t *preproc,
                              const uint8_t *input,
                              size_t input_bytes,
                              const uint8_t *reference,
                              size_t reference_bytes,
                               uint8_t *output,
                               size_t output_capacity,
                               size_t *output_bytes);
    void (*close)(river_voice_preproc_t *preproc);
    void (*dump_profile)(void);
    void (*dump_runtime_stats)(const river_voice_preproc_t *preproc);
};

river_status_t river_voice_preproc_fixed_dsb_open(river_voice_preproc_t *preproc);
river_status_t river_voice_preproc_fixed_dsb_process(river_voice_preproc_t *preproc,
                                                     const uint8_t *input,
                                                     size_t input_bytes,
                                                     const uint8_t *reference,
                                                     size_t reference_bytes,
                                                     uint8_t *output,
                                                     size_t output_capacity,
                                                     size_t *output_bytes);
void river_voice_preproc_fixed_dsb_close(river_voice_preproc_t *preproc);
void river_voice_preproc_fixed_dsb_dump_profile(void);
void river_voice_preproc_fixed_dsb_dump_runtime_stats(const river_voice_preproc_t *preproc);

static const river_voice_preproc_ops_t g_river_voice_preproc_ops = {
    .name = "fixed_dsb",
    .open = river_voice_preproc_fixed_dsb_open,
    .process = river_voice_preproc_fixed_dsb_process,
    .close = river_voice_preproc_fixed_dsb_close,
    .dump_profile = river_voice_preproc_fixed_dsb_dump_profile,
    .dump_runtime_stats = river_voice_preproc_fixed_dsb_dump_runtime_stats
};

river_status_t river_voice_preproc_open(river_voice_preproc_t *preproc)
{
    const river_voice_board_array_profile_t *profile;
    river_voice_preproc_profile_t active_profile;
    uint32_t frame_samples;

    if (preproc == 0) {
        return RIVER_ERR_ARG;
    }

    memset(preproc, 0, sizeof(*preproc));
    profile = river_voice_board_array_profile();
    active_profile = river_voice_profile_active_preproc();
    frame_samples = (profile->sample_rate * profile->frame_ms) / 1000U;
    preproc->ops = &g_river_voice_preproc_ops;
    preproc->sample_rate = profile->sample_rate;
    preproc->frame_ms = profile->frame_ms;
    preproc->profile = active_profile;
    preproc->input_channels = river_voice_profile_capture_channels(active_profile);
    preproc->output_channels = 1U;
    preproc->reference_channels = 0U;
    preproc->input_frame_bytes = frame_samples * preproc->input_channels * sizeof(int16_t);
    preproc->output_frame_bytes = frame_samples * preproc->output_channels * sizeof(int16_t);
    preproc->reference_frame_bytes = 0U;
    preproc->feed_frame_bytes = preproc->input_frame_bytes;
    preproc->reference_enabled = false;
    return preproc->ops->open(preproc);
}

river_status_t river_voice_preproc_process(river_voice_preproc_t *preproc,
                                           const uint8_t *input,
                                           size_t input_bytes,
                                           const uint8_t *reference,
                                           size_t reference_bytes,
                                           uint8_t *output,
                                           size_t output_capacity,
                                           size_t *output_bytes)
{
    if (preproc == 0 || preproc->ops == 0 || preproc->ops->process == 0) {
        return RIVER_ERR_ARG;
    }

    return preproc->ops->process(preproc,
                                 input,
                                 input_bytes,
                                 reference,
                                 reference_bytes,
                                 output,
                                 output_capacity,
                                 output_bytes);
}

void river_voice_preproc_close(river_voice_preproc_t *preproc)
{
    if (preproc == 0 || preproc->ops == 0 || preproc->ops->close == 0) {
        return;
    }

    preproc->ops->close(preproc);
    preproc->backend_ctx = 0;
}

size_t river_voice_preproc_input_frame_bytes(const river_voice_preproc_t *preproc)
{
    return preproc == 0 ? 0U : preproc->input_frame_bytes;
}

size_t river_voice_preproc_output_frame_bytes(const river_voice_preproc_t *preproc)
{
    return preproc == 0 ? 0U : preproc->output_frame_bytes;
}

size_t river_voice_preproc_reference_frame_bytes(const river_voice_preproc_t *preproc)
{
    return preproc == 0 ? 0U : preproc->reference_frame_bytes;
}

size_t river_voice_preproc_feed_frame_bytes(const river_voice_preproc_t *preproc)
{
    return preproc == 0 ? 0U : preproc->feed_frame_bytes;
}

uint32_t river_voice_preproc_output_channels(const river_voice_preproc_t *preproc)
{
    return preproc == 0 ? 0U : preproc->output_channels;
}

uint32_t river_voice_preproc_reference_channels(const river_voice_preproc_t *preproc)
{
    return preproc == 0 ? 0U : preproc->reference_channels;
}

bool river_voice_preproc_reference_enabled(const river_voice_preproc_t *preproc)
{
    return preproc != 0 && preproc->reference_enabled;
}

const char *river_voice_preproc_backend_name(void)
{
    return g_river_voice_preproc_ops.name;
}

const char *river_voice_preproc_profile_name(void)
{
    return river_voice_profile_name(river_voice_profile_active_preproc());
}

void river_voice_preproc_dump_profile(void)
{
    if (g_river_voice_preproc_ops.dump_profile != 0) {
        g_river_voice_preproc_ops.dump_profile();
    }
}

void river_voice_preproc_dump_runtime_stats(const river_voice_preproc_t *preproc)
{
    if (preproc == 0 || preproc->ops == 0 || preproc->ops->dump_runtime_stats == 0) {
        return;
    }

    preproc->ops->dump_runtime_stats(preproc);
}
