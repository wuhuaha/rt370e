/* 实验侧路接口：给调试/实验模块保留帧级挂接点。 */
#ifndef AMEBA_RIVER_VOICE_EXPERIMENT_H
#define AMEBA_RIVER_VOICE_EXPERIMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "river/river_types.h"
#include "river/river_voice_profile.h"

typedef enum {
    RIVER_VOICE_EXPERIMENT_DOMAIN_CAPTURE_RAW = 0,
    RIVER_VOICE_EXPERIMENT_DOMAIN_PREPROC_MONO,
    RIVER_VOICE_EXPERIMENT_DOMAIN_REFERENCE
} river_voice_experiment_domain_t;

typedef struct {
    river_voice_preproc_profile_t profile;
    river_voice_stage_t stage;
    river_voice_experiment_domain_t domain;
    const uint8_t *data;
    size_t bytes;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t frame_ms;
} river_voice_experiment_frame_t;

typedef struct {
    river_voice_preproc_profile_t profile;
    uint32_t sample_rate;
    uint32_t frame_ms;
    uint32_t input_channels;
    uint32_t output_channels;
    bool requires_reference;
    bool native_reference;
} river_voice_aec_contract_t;

typedef river_status_t (*river_voice_experiment_sidepath_handler_t)(
    uint32_t capability,
    const river_voice_experiment_frame_t *frame,
    void *user_data);

void river_voice_experiment_get_aec_contract(river_voice_preproc_profile_t profile,
                                             river_voice_aec_contract_t *contract);
river_status_t river_voice_experiment_register_sidepath(
    uint32_t capability,
    river_voice_experiment_sidepath_handler_t handler,
    void *user_data);
void river_voice_experiment_unregister_sidepath(uint32_t capability);
void river_voice_experiment_submit_frame(river_voice_experiment_domain_t domain,
                                         const uint8_t *data,
                                         size_t bytes,
                                         uint32_t channels);
void river_voice_experiment_dump_profile(void);
const char *river_voice_experiment_domain_name(river_voice_experiment_domain_t domain);

#endif
