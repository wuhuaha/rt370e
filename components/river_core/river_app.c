#include <stdio.h>

#include "river/river_app.h"
#include "river/river_cloud.h"
#include "river/river_online_control.h"
#include "river/river_voice.h"

static void river_app_on_voice_event(const river_voice_event_t *event)
{
    if (event == 0) {
        return;
    }

    printf("[river][voice] event=%d confidence=%d\n", event->type, event->confidence);
    if (event->text != 0) {
        printf("[river][voice] text=%s\n", event->text);
    }
}

river_status_t river_app_boot(void)
{
    printf("[river] ameba-river boot\n");
    printf("[river] target=RTL8730E\n");

    river_voice_frontend_set_handler(river_app_on_voice_event);

    if (river_voice_frontend_init() != RIVER_OK) {
        return RIVER_ERR_UNSUPPORTED;
    }

    if (river_cloud_adapter_init() != RIVER_OK) {
        return RIVER_ERR_UNSUPPORTED;
    }

    if (river_online_control_init() != RIVER_OK) {
        return RIVER_ERR_UNSUPPORTED;
    }

    river_app_print_status();
    return RIVER_OK;
}

void river_app_print_status(void)
{
    printf("[river] local_frontend=%s\n", river_voice_frontend_mode_name());
#ifdef CONFIG_RIVER_OFFLINE_ASR_RESERVED
    printf("[river] offline_asr=reserved\n");
#else
    printf("[river] offline_asr=disabled\n");
#endif
#ifdef CONFIG_RIVER_ONLINE_CONTROL_EN
    printf("[river] online_control=enabled\n");
#else
    printf("[river] online_control=disabled\n");
#endif
    river_online_control_dump_status();
}
