#include "river/river_app.h"
#include "river/river_board_rgb.h"
#include "river/river_cloud.h"
#include "river/river_log.h"
#include "river/river_online_control.h"
#include "river/river_voice.h"
#include "river/river_voice_vad_reference.h"
#include "river/river_wifi_station.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.app"

static void river_app_on_voice_event(const river_voice_event_t *event)
{
    if (event == 0) {
        return;
    }

    RIVER_LOGD("voice event=%d confidence=%d", event->type, event->confidence);
    if (event->text != 0) {
        RIVER_LOGD("voice text=%s", event->text);
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
        RIVER_LOGD("asr provider=%s partial sid=%s text=%s",
                   result->provider_name != NULL ? result->provider_name : "-",
                   result->sid != NULL ? result->sid : "-",
                   result->text != NULL ? result->text : "-");
        break;
    case RIVER_CLOUD_ASR_EVENT_FINAL:
        RIVER_LOGI("asr provider=%s final sid=%s text=%s",
                   result->provider_name != NULL ? result->provider_name : "-",
                   result->sid != NULL ? result->sid : "-",
                   result->text != NULL ? result->text : "-");
        break;
    case RIVER_CLOUD_ASR_EVENT_ERROR:
        RIVER_LOGE("asr provider=%s error code=%d sid=%s msg=%s",
                   result->provider_name != NULL ? result->provider_name : "-",
                   result->code,
                   result->sid != NULL ? result->sid : "-",
                   result->message != NULL ? result->message : "-");
        break;
    case RIVER_CLOUD_ASR_EVENT_SESSION_STARTED:
        RIVER_LOGI("asr provider=%s session started sid=%s",
                   result->provider_name != NULL ? result->provider_name : "-",
                   result->sid != NULL ? result->sid : "-");
        break;
    case RIVER_CLOUD_ASR_EVENT_SESSION_CLOSED:
        RIVER_LOGI("asr provider=%s session closed sid=%s",
                   result->provider_name != NULL ? result->provider_name : "-",
                   result->sid != NULL ? result->sid : "-");
        break;
    default:
        break;
    }
}

river_status_t river_app_boot(void)
{
    RIVER_LOGI("ameba-river boot");
    RIVER_LOGI("target=RTL8730E");

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
    RIVER_LOGI("boot audio echo diagnostics enabled");
#endif

#ifdef CONFIG_RIVER_VAD_PROBE_DIAG_DEFAULT_ON
    river_voice_vad_probe_set_diag_enabled(true);
    RIVER_LOGI("boot vad probe diagnostics enabled");
#endif

#ifdef CONFIG_RIVER_VAD_PROBE_AUTOSTART
    RIVER_LOGI("boot vad probe autostart enabled");
    if (river_voice_vad_probe_start() != RIVER_OK) {
        RIVER_LOGE("boot vad probe autostart failed");
#ifdef CONFIG_RIVER_BOARD_RGB_VAD_INDICATOR_EN
        river_board_rgb_set_state(RIVER_BOARD_RGB_STATE_ERROR);
#endif
    }
#endif

#ifdef CONFIG_RIVER_AUDIO_ECHO_AUTOSTART
    RIVER_LOGI("boot audio echo autostart enabled");
    if (river_voice_echo_start() != RIVER_OK) {
        RIVER_LOGE("boot audio echo autostart failed");
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
    RIVER_LOGI("local_frontend=%s", river_voice_frontend_mode_name());
    RIVER_LOGI("local_preproc=%s", river_voice_preproc_backend_name());
    RIVER_LOGI("local_preproc_profile=%s", river_voice_preproc_profile_name());
    RIVER_LOGI("local_detector=%s", river_voice_detector_backend_name());
    RIVER_LOGI("local_detector_reference=%s", river_voice_vad_reference_name());
    RIVER_LOGI("local_playback_ref=%s", river_voice_ref_backend_name());
    RIVER_LOGI("local_segment_sink=%s", river_voice_segment_sink_name());
#ifdef CONFIG_RIVER_OFFLINE_ASR_RESERVED
    RIVER_LOGI("offline_asr=reserved");
#else
    RIVER_LOGI("offline_asr=disabled");
#endif
#ifdef CONFIG_RIVER_ONLINE_CONTROL_EN
    RIVER_LOGI("online_control=enabled");
#else
    RIVER_LOGI("online_control=disabled");
#endif
    river_voice_echo_dump_status();
    river_voice_vad_probe_dump_status();
    river_wifi_station_dump_status();
    river_cloud_adapter_dump_status();
    river_online_control_dump_status();
}
