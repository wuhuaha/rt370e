/* Experiment sidepath stub for the Orvibo-only live graph. */
#include <string.h>

#include "river/river_log.h"
#include "river/river_voice_board.h"
#include "river/river_voice_experiment.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.voice.experiment"

void river_voice_experiment_get_aec_contract(river_voice_preproc_profile_t profile,
                                             river_voice_aec_contract_t *contract)
{
    const river_voice_board_array_profile_t *board_profile;
    const river_voice_profile_config_t *profile_config;

    if (contract == NULL) {
        return;
    }
    board_profile = river_voice_board_array_profile();
    profile_config = river_voice_profile_get(profile);
    memset(contract, 0, sizeof(*contract));
    contract->profile = profile;
    contract->sample_rate = board_profile->sample_rate;
    contract->frame_ms = board_profile->frame_ms;
    contract->input_channels = profile_config->capture_channels;
    contract->output_channels = 1U;
    contract->requires_reference =
        river_voice_profile_has_capability(profile, RIVER_VOICE_CAPABILITY_AEC);
    contract->native_reference =
        river_voice_profile_has_capability(profile, RIVER_VOICE_CAPABILITY_NATIVE_CAPTURE_REF);
}

river_status_t river_voice_experiment_register_sidepath(
    uint32_t capability,
    river_voice_experiment_sidepath_handler_t handler,
    void *user_data)
{
    (void)capability;
    (void)handler;
    (void)user_data;
    return RIVER_ERR_UNSUPPORTED;
}

void river_voice_experiment_unregister_sidepath(uint32_t capability)
{
    (void)capability;
}

void river_voice_experiment_submit_frame(river_voice_experiment_domain_t domain,
                                         const uint8_t *data,
                                         size_t bytes,
                                         uint32_t channels)
{
    (void)domain;
    (void)data;
    (void)bytes;
    (void)channels;
}

void river_voice_experiment_dump_profile(void)
{
    RIVER_LOGI("voice experiment sidepath: compiled=stub owner=orvibo");
}

const char *river_voice_experiment_domain_name(river_voice_experiment_domain_t domain)
{
    switch (domain) {
    case RIVER_VOICE_EXPERIMENT_DOMAIN_CAPTURE_RAW:
        return "capture_raw";
    case RIVER_VOICE_EXPERIMENT_DOMAIN_PREPROC_MONO:
        return "preproc_mono";
    case RIVER_VOICE_EXPERIMENT_DOMAIN_REFERENCE:
        return "reference";
    default:
        return "unknown";
    }
}
