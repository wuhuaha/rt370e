#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "river/river_interaction_state.h"
#include "river/river_log.h"
#include "river/river_voice_board.h"
#include "river/river_voice_experiment.h"
#include "river/river_voice_runtime_policy.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.voice.experiment"

typedef struct {
    uint32_t capability;
    river_voice_experiment_sidepath_handler_t handler;
    void *user_data;
} river_voice_experiment_sidepath_slot_t;

static river_voice_experiment_sidepath_slot_t g_river_voice_experiment_slots[3];

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

static bool river_voice_experiment_capability_accepts_frame(uint32_t capability,
                                                            river_voice_stage_t stage,
                                                            river_voice_experiment_domain_t domain)
{
    switch (capability) {
    case RIVER_VOICE_CAPABILITY_KWS:
        return stage == RIVER_VOICE_STAGE_WAKE &&
               domain == RIVER_VOICE_EXPERIMENT_DOMAIN_PREPROC_MONO;
    case RIVER_VOICE_CAPABILITY_DOA:
        return domain == RIVER_VOICE_EXPERIMENT_DOMAIN_CAPTURE_RAW;
    case RIVER_VOICE_CAPABILITY_WAKE_GUIDED_BF:
        return stage == RIVER_VOICE_STAGE_WAKE &&
               domain == RIVER_VOICE_EXPERIMENT_DOMAIN_CAPTURE_RAW;
    default:
        return false;
    }
}

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
    contract->requires_reference = river_voice_profile_has_capability(profile, RIVER_VOICE_CAPABILITY_AEC);
    contract->native_reference =
        river_voice_profile_has_capability(profile, RIVER_VOICE_CAPABILITY_NATIVE_CAPTURE_REF);
}

river_status_t river_voice_experiment_register_sidepath(
    uint32_t capability,
    river_voice_experiment_sidepath_handler_t handler,
    void *user_data)
{
    size_t index;

    if (handler == NULL || capability == 0U) {
        return RIVER_ERR_ARG;
    }

    for (index = 0U; index < (sizeof(g_river_voice_experiment_slots) /
                               sizeof(g_river_voice_experiment_slots[0])); ++index) {
        if (g_river_voice_experiment_slots[index].capability == 0U ||
            g_river_voice_experiment_slots[index].capability == capability) {
            g_river_voice_experiment_slots[index].capability = capability;
            g_river_voice_experiment_slots[index].handler = handler;
            g_river_voice_experiment_slots[index].user_data = user_data;
            return RIVER_OK;
        }
    }

    return RIVER_ERR_NO_MEMORY;
}

void river_voice_experiment_unregister_sidepath(uint32_t capability)
{
    size_t index;

    for (index = 0U; index < (sizeof(g_river_voice_experiment_slots) /
                               sizeof(g_river_voice_experiment_slots[0])); ++index) {
        if (g_river_voice_experiment_slots[index].capability == capability) {
            memset(&g_river_voice_experiment_slots[index], 0, sizeof(g_river_voice_experiment_slots[index]));
        }
    }
}

void river_voice_experiment_submit_frame(river_voice_experiment_domain_t domain,
                                         const uint8_t *data,
                                         size_t bytes,
                                         uint32_t channels)
{
    river_voice_preproc_profile_t profile;
    river_voice_stage_t stage;
    const river_voice_board_array_profile_t *board_profile;
    river_voice_experiment_frame_t frame;
    uint32_t capability_mask;
    size_t index;

    if (data == NULL || bytes == 0U || channels == 0U) {
        return;
    }

    profile = river_voice_profile_active_preproc();
    stage = river_voice_runtime_stage();
    if (!river_voice_runtime_stage_enabled(profile, stage)) {
        return;
    }

    capability_mask = river_voice_profile_capability_mask(profile);
    if ((capability_mask & (RIVER_VOICE_CAPABILITY_KWS |
                            RIVER_VOICE_CAPABILITY_DOA |
                            RIVER_VOICE_CAPABILITY_WAKE_GUIDED_BF)) == 0U) {
        return;
    }

    board_profile = river_voice_board_array_profile();
    memset(&frame, 0, sizeof(frame));
    frame.profile = profile;
    frame.stage = stage;
    frame.domain = domain;
    frame.data = data;
    frame.bytes = bytes;
    frame.sample_rate = board_profile->sample_rate;
    frame.channels = channels;
    frame.frame_ms = board_profile->frame_ms;

    for (index = 0U; index < (sizeof(g_river_voice_experiment_slots) /
                               sizeof(g_river_voice_experiment_slots[0])); ++index) {
        uint32_t capability;

        capability = g_river_voice_experiment_slots[index].capability;
        if (capability == 0U || g_river_voice_experiment_slots[index].handler == NULL) {
            continue;
        }
        if ((capability_mask & capability) == 0U) {
            continue;
        }
        if (!river_voice_experiment_capability_accepts_frame(capability, stage, domain)) {
            continue;
        }
        (void)g_river_voice_experiment_slots[index].handler(capability,
                                                            &frame,
                                                            g_river_voice_experiment_slots[index].user_data);
    }
}

void river_voice_experiment_dump_profile(void)
{
    river_voice_preproc_profile_t profile;
    const river_voice_profile_config_t *profile_config;
    river_voice_aec_contract_t aec_contract;
    uint32_t capability_mask;

    profile = river_voice_profile_active_preproc();
    profile_config = river_voice_profile_active();
    capability_mask = river_voice_profile_capability_mask(profile);
    river_voice_experiment_get_aec_contract(profile, &aec_contract);

    RIVER_LOGI("experiment profile: name=%s stage_mask=%s|%s caps[aec=%s,kws=%s,doa=%s,wgbf=%s,native_ref=%s]",
               profile_config->name,
               (profile_config->stage_mask & RIVER_VOICE_STAGE_MASK_WAKE) != 0U ? "wake" : "-",
               (profile_config->stage_mask & RIVER_VOICE_STAGE_MASK_POST_WAKE) != 0U ? "post_wake" : "-",
               (capability_mask & RIVER_VOICE_CAPABILITY_AEC) != 0U ? "on" : "off",
               (capability_mask & RIVER_VOICE_CAPABILITY_KWS) != 0U ? "on" : "off",
               (capability_mask & RIVER_VOICE_CAPABILITY_DOA) != 0U ? "on" : "off",
               (capability_mask & RIVER_VOICE_CAPABILITY_WAKE_GUIDED_BF) != 0U ? "on" : "off",
               (capability_mask & RIVER_VOICE_CAPABILITY_NATIVE_CAPTURE_REF) != 0U ? "on" : "off");
    RIVER_LOGI("experiment aec contract: in_ch=%lu out_ch=%lu sample_rate=%lu frame_ms=%lu ref=%s native_ref=%s",
               (unsigned long)aec_contract.input_channels,
               (unsigned long)aec_contract.output_channels,
               (unsigned long)aec_contract.sample_rate,
               (unsigned long)aec_contract.frame_ms,
               aec_contract.requires_reference ? "required" : "not_required",
               aec_contract.native_reference ? "yes" : "no");
    if (capability_mask & (RIVER_VOICE_CAPABILITY_KWS |
                           RIVER_VOICE_CAPABILITY_DOA |
                           RIVER_VOICE_CAPABILITY_WAKE_GUIDED_BF)) {
        RIVER_LOGI("experiment sidepaths reserve runtime hooks through %s/%s frame domains for future modules",
                   river_voice_experiment_domain_name(RIVER_VOICE_EXPERIMENT_DOMAIN_CAPTURE_RAW),
                   river_voice_experiment_domain_name(RIVER_VOICE_EXPERIMENT_DOMAIN_PREPROC_MONO));
    } else {
        RIVER_LOGI("experiment sidepaths disabled by current profile/build flags; stable path remains direct");
    }
}
