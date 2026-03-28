#include "river/river_interaction_diag.h"
#include "river/river_log.h"
#include "river/river_online_control.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.interaction.diag"

river_status_t river_interaction_diag_init(void)
{
    return RIVER_OK;
}

river_status_t river_interaction_diag_route_text(const char *text,
                                                 const char *source,
                                                 const char *sid)
{
    (void)text;
    (void)source;
    (void)sid;
    return RIVER_ERR_NOT_FOUND;
}

river_status_t river_interaction_diag_submit_tts_test(const char *source)
{
    (void)source;
    return RIVER_ERR_UNSUPPORTED;
}

river_status_t river_interaction_diag_invoke_intent(const char *intent_name,
                                                    const char *source)
{
    (void)intent_name;
    (void)source;
    return RIVER_ERR_UNSUPPORTED;
}

river_status_t river_interaction_diag_execute_device(const char *device_name,
                                                     const char *action_name,
                                                     const char *source)
{
    (void)source;
    return river_online_control_set_device(device_name, action_name);
}

river_status_t river_interaction_diag_flush_deferred(void)
{
    return RIVER_OK;
}

void river_interaction_diag_dump_status(void)
{
    RIVER_LOGI("interaction_diag=compiled=no");
}

void river_interaction_diag_dump_intents(void)
{
    RIVER_LOGI("interaction_diag_intents=compiled=no");
}
