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

#ifdef CONFIG_RIVER_SPEAKER_TEST_DIAG_DEFAULT_ON
    river_voice_speaker_test_set_diag_enabled(true);
    printf("[river][voice] boot speaker playback diagnostics enabled\n");
#endif

#ifdef CONFIG_RIVER_SPEAKER_TEST_AUTOSTART
    printf("[river][voice] boot speaker playback autostart enabled\n");
    if (river_voice_speaker_test_start() != RIVER_OK) {
        printf("[river][voice] boot speaker playback autostart failed\n");
    }
#endif

#ifdef CONFIG_RIVER_AUDIO_ECHO_DIAG_DEFAULT_ON
    river_voice_echo_set_diag_enabled(true);
    printf("[river][voice] boot audio echo diagnostics enabled\n");
#endif

#ifdef CONFIG_RIVER_AUDIO_ECHO_AUTOSTART
    printf("[river][voice] boot audio echo autostart enabled\n");
    if (river_voice_echo_start() != RIVER_OK) {
        printf("[river][voice] boot audio echo autostart failed\n");
    }
#endif

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
    river_voice_speaker_test_dump_status();
    river_voice_echo_dump_status();
    river_online_control_dump_status();
}
