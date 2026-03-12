#include <stdio.h>

#include "river/river_app.h"
#include "river/river_board_rgb.h"
#include "river/river_cloud.h"
#include "river/river_online_control.h"
#include "river/river_voice.h"
#include "river/river_voice_vad_reference.h"
#include "river/river_wifi_station.h"

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

static void river_app_on_cloud_asr_result(const river_cloud_asr_result_t *result,
                                          void *user_data)
{
    (void)user_data;

    if (result == NULL) {
        return;
    }

    switch (result->type) {
    case RIVER_CLOUD_ASR_EVENT_PARTIAL:
        printf("[river][asr][%s] partial sid=%s text=%s\n",
               result->provider_name != NULL ? result->provider_name : "-",
               result->sid != NULL ? result->sid : "-",
               result->text != NULL ? result->text : "-");
        break;
    case RIVER_CLOUD_ASR_EVENT_FINAL:
        printf("[river][asr][%s] final sid=%s text=%s\n",
               result->provider_name != NULL ? result->provider_name : "-",
               result->sid != NULL ? result->sid : "-",
               result->text != NULL ? result->text : "-");
        break;
    case RIVER_CLOUD_ASR_EVENT_ERROR:
        printf("[river][asr][%s] error code=%d sid=%s msg=%s\n",
               result->provider_name != NULL ? result->provider_name : "-",
               result->code,
               result->sid != NULL ? result->sid : "-",
               result->message != NULL ? result->message : "-");
        break;
    case RIVER_CLOUD_ASR_EVENT_SESSION_STARTED:
        printf("[river][asr][%s] session started sid=%s\n",
               result->provider_name != NULL ? result->provider_name : "-",
               result->sid != NULL ? result->sid : "-");
        break;
    case RIVER_CLOUD_ASR_EVENT_SESSION_CLOSED:
        printf("[river][asr][%s] session closed sid=%s\n",
               result->provider_name != NULL ? result->provider_name : "-",
               result->sid != NULL ? result->sid : "-");
        break;
    default:
        break;
    }
}

river_status_t river_app_boot(void)
{
    printf("[river] ameba-river boot\n");
    printf("[river] target=RTL8730E\n");

#ifdef CONFIG_RIVER_BOARD_RGB_VAD_INDICATOR_EN
    river_board_rgb_set_state(RIVER_BOARD_RGB_STATE_BOOT);
#endif

    river_voice_frontend_set_handler(river_app_on_voice_event);

    if (river_wifi_station_init() != RIVER_OK) {
#ifdef CONFIG_RIVER_BOARD_RGB_VAD_INDICATOR_EN
        river_board_rgb_set_state(RIVER_BOARD_RGB_STATE_ERROR);
#endif
        return RIVER_ERR_UNSUPPORTED;
    }

    if (river_cloud_adapter_init() != RIVER_OK) {
#ifdef CONFIG_RIVER_BOARD_RGB_VAD_INDICATOR_EN
        river_board_rgb_set_state(RIVER_BOARD_RGB_STATE_ERROR);
#endif
        return RIVER_ERR_UNSUPPORTED;
    }

    river_cloud_adapter_set_result_handler(river_app_on_cloud_asr_result, NULL);

    if (river_voice_frontend_init() != RIVER_OK) {
#ifdef CONFIG_RIVER_BOARD_RGB_VAD_INDICATOR_EN
        river_board_rgb_set_state(RIVER_BOARD_RGB_STATE_ERROR);
#endif
        return RIVER_ERR_UNSUPPORTED;
    }

    if (river_online_control_init() != RIVER_OK) {
#ifdef CONFIG_RIVER_BOARD_RGB_VAD_INDICATOR_EN
        river_board_rgb_set_state(RIVER_BOARD_RGB_STATE_ERROR);
#endif
        return RIVER_ERR_UNSUPPORTED;
    }

#ifdef CONFIG_RIVER_AUDIO_ECHO_DIAG_DEFAULT_ON
    river_voice_echo_set_diag_enabled(true);
    printf("[river][voice] boot audio echo diagnostics enabled\n");
#endif

#ifdef CONFIG_RIVER_VAD_PROBE_DIAG_DEFAULT_ON
    river_voice_vad_probe_set_diag_enabled(true);
    printf("[river][voice] boot vad probe diagnostics enabled\n");
#endif

#ifdef CONFIG_RIVER_VAD_PROBE_AUTOSTART
    printf("[river][voice] boot vad probe autostart enabled\n");
    if (river_voice_vad_probe_start() != RIVER_OK) {
        printf("[river][voice] boot vad probe autostart failed\n");
#ifdef CONFIG_RIVER_BOARD_RGB_VAD_INDICATOR_EN
        river_board_rgb_set_state(RIVER_BOARD_RGB_STATE_ERROR);
#endif
    }
#endif

#ifdef CONFIG_RIVER_AUDIO_ECHO_AUTOSTART
    printf("[river][voice] boot audio echo autostart enabled\n");
    if (river_voice_echo_start() != RIVER_OK) {
        printf("[river][voice] boot audio echo autostart failed\n");
#ifdef CONFIG_RIVER_BOARD_RGB_VAD_INDICATOR_EN
        river_board_rgb_set_state(RIVER_BOARD_RGB_STATE_ERROR);
#endif
    }
#endif

    river_app_print_status();
    return RIVER_OK;
}

void river_app_print_status(void)
{
    printf("[river] local_frontend=%s\n", river_voice_frontend_mode_name());
    printf("[river] local_preproc=%s\n", river_voice_preproc_backend_name());
    printf("[river] local_preproc_profile=%s\n", river_voice_preproc_profile_name());
    printf("[river] local_detector=%s\n", river_voice_detector_backend_name());
    printf("[river] local_detector_reference=%s\n", river_voice_vad_reference_name());
    printf("[river] local_playback_ref=%s\n", river_voice_ref_backend_name());
    printf("[river] local_segment_sink=%s\n", river_voice_segment_sink_name());
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
    river_voice_echo_dump_status();
    river_voice_vad_probe_dump_status();
    river_wifi_station_dump_status();
    river_cloud_adapter_dump_status();
    river_online_control_dump_status();
}
