#include <stdio.h>
#include <string.h>

#include "river/river_app.h"
#include "river/river_cloud.h"
#include "river/river_interaction_state.h"
#include "river/river_interaction_diag.h"
#include "river/river_log.h"
#include "river/river_online_control.h"
#include "river/river_playback_service.h"
#include "river/river_reference_service.h"
#include "river/river_runtime_stats.h"
#include "river/river_voice.h"
#include "river/river_voice_capture.h"
#include "river/river_voice_profile.h"
#include "river/river_wifi_station.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.app"

static char g_river_app_last_partial[192];
static bool g_river_app_asr_session_active;
static bool g_river_app_barge_in_interrupt_requested;

static bool river_app_playback_state_active(river_playback_state_t state)
{
    return river_playback_service_state_active(state);
}

static void river_app_sync_interaction_state(const char *reason)
{
    if (river_app_playback_state_active(river_playback_service_state())) {
        river_interaction_state_set(g_river_app_asr_session_active ?
                                        RIVER_INTERACTION_BARGE_IN_LISTENING :
                                        RIVER_INTERACTION_SPEAKING,
                                    reason);
        return;
    }

    river_interaction_state_set(g_river_app_asr_session_active ?
                                    RIVER_INTERACTION_ASR_STREAMING :
                                    RIVER_INTERACTION_WAKE_MONITORING,
                                reason);
}

static void river_app_on_playback_state(river_playback_state_t state,
                                        const river_playback_stream_config_t *config,
                                        void *user_data)
{
    (void)config;
    (void)user_data;

    if (state == RIVER_PLAYBACK_ERROR) {
        river_interaction_state_set(RIVER_INTERACTION_ERROR_RECOVERING, "playback_error");
        return;
    }

    river_app_sync_interaction_state("playback_state");
}

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

static void river_app_try_interrupt_playback_on_asr_text(const river_cloud_asr_result_t *result)
{
    river_interaction_state_t interaction_state;

    if (result == NULL || result->text == NULL || result->text[0] == '\0') {
        return;
    }
    if (!g_river_app_asr_session_active || g_river_app_barge_in_interrupt_requested) {
        return;
    }
    if (!river_app_playback_state_active(river_playback_service_state())) {
        return;
    }

    interaction_state = river_interaction_state_get();
    if (interaction_state != RIVER_INTERACTION_SPEAKING &&
        interaction_state != RIVER_INTERACTION_BARGE_IN_LISTENING) {
        return;
    }

    RIVER_LOGI("barge-in text confirmed during playback: state=%s sid=%s text=%s -> interrupt tts",
               river_interaction_state_name(interaction_state),
               result->sid != NULL ? result->sid : "-",
               result->text);
    if (river_cloud_adapter_interrupt_tts_with_reason("asr_text_confirmed") == RIVER_OK) {
        g_river_app_barge_in_interrupt_requested = true;
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
        if ((result->text != NULL) && (result->text[0] != '\0') &&
            (strcmp(g_river_app_last_partial, result->text) != 0)) {
            snprintf(g_river_app_last_partial,
                     sizeof(g_river_app_last_partial),
                     "%s",
                     result->text);
            RIVER_LOGI("asr provider=%s partial sid=%s text=%s",
                       result->provider_name != NULL ? result->provider_name : "-",
                       result->sid != NULL ? result->sid : "-",
                       result->text);
            river_app_try_interrupt_playback_on_asr_text(result);
        }
        break;
    case RIVER_CLOUD_ASR_EVENT_FINAL:
        g_river_app_last_partial[0] = '\0';
        RIVER_LOGI("asr provider=%s final sid=%s text=%s",
                   result->provider_name != NULL ? result->provider_name : "-",
                   result->sid != NULL ? result->sid : "-",
                   result->text != NULL ? result->text : "-");
        if ((result->text != NULL) && (result->text[0] != '\0')) {
            river_app_try_interrupt_playback_on_asr_text(result);
            (void)river_interaction_diag_route_text(result->text, "asr_final", result->sid);
        }
        break;
    case RIVER_CLOUD_ASR_EVENT_ERROR:
        g_river_app_asr_session_active = false;
        g_river_app_barge_in_interrupt_requested = false;
        g_river_app_last_partial[0] = '\0';
        RIVER_LOGE("asr provider=%s error code=%d sid=%s msg=%s",
                   result->provider_name != NULL ? result->provider_name : "-",
                   result->code,
                   result->sid != NULL ? result->sid : "-",
                   result->message != NULL ? result->message : "-");
        river_interaction_state_set(RIVER_INTERACTION_ERROR_RECOVERING, "asr_error");
        break;
    case RIVER_CLOUD_ASR_EVENT_SESSION_STARTED:
        g_river_app_asr_session_active = true;
        g_river_app_barge_in_interrupt_requested = false;
        g_river_app_last_partial[0] = '\0';
        RIVER_LOGI("asr provider=%s session started sid=%s",
                   result->provider_name != NULL ? result->provider_name : "-",
                   result->sid != NULL ? result->sid : "-");
        river_app_sync_interaction_state("asr_session_started");
        break;
    case RIVER_CLOUD_ASR_EVENT_SESSION_CLOSED:
        g_river_app_asr_session_active = false;
        g_river_app_barge_in_interrupt_requested = false;
        g_river_app_last_partial[0] = '\0';
        RIVER_LOGI("asr provider=%s session closed sid=%s",
                   result->provider_name != NULL ? result->provider_name : "-",
                   result->sid != NULL ? result->sid : "-");
        river_app_sync_interaction_state("asr_session_closed");
        (void)river_interaction_diag_flush_deferred();
        break;
    default:
        break;
    }
}

river_status_t river_app_boot(void)
{
    RIVER_LOGI("ameba-river boot");
    RIVER_LOGI("target=RTL8730E");

    river_runtime_stats_init();
    if (river_interaction_state_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    if (river_playback_service_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    river_interaction_state_set(RIVER_INTERACTION_BOOTING, "boot_begin");
    river_playback_service_register_listener(river_app_on_playback_state, NULL);

    river_voice_frontend_set_handler(river_app_on_voice_event);

    if (river_wifi_station_init() != RIVER_OK) {
        return RIVER_ERR_UNSUPPORTED;
    }

    if (river_cloud_adapter_init() != RIVER_OK) {
        return RIVER_ERR_UNSUPPORTED;
    }

    river_cloud_adapter_set_result_handler(river_app_on_cloud_asr_result, NULL);

    if (river_voice_frontend_init() != RIVER_OK) {
        return RIVER_ERR_UNSUPPORTED;
    }

    if (river_online_control_init() != RIVER_OK) {
        return RIVER_ERR_UNSUPPORTED;
    }
    if (river_interaction_diag_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
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
    }
#endif

#ifdef CONFIG_RIVER_AUDIO_ECHO_AUTOSTART
    RIVER_LOGI("boot audio echo autostart enabled");
    if (river_voice_echo_start() != RIVER_OK) {
        RIVER_LOGE("boot audio echo autostart failed");
    }
#endif

    river_app_sync_interaction_state("boot_ready");
    river_app_print_status();
    river_runtime_stats_snapshot("boot_ready");
    return RIVER_OK;
}

void river_app_print_status(void)
{
    const char *profile_name;
    const river_voice_profile_config_t *profile;

    profile = river_voice_profile_active();
    profile_name = river_voice_preproc_profile_name();
    RIVER_LOGI("local_frontend=%s", river_voice_frontend_mode_name());
    RIVER_LOGI("local_preproc=%s", river_voice_preproc_backend_name());
    RIVER_LOGI("local_preproc_profile=%s", profile_name);
    RIVER_LOGI("local_detector=%s", river_voice_detector_backend_name());
    if (profile->uses_native_capture_ref) {
        RIVER_LOGI("local_aec_ref=native_capture_ch3");
    } else {
        RIVER_LOGI("local_playback_ref=%s", river_reference_service_backend_name());
    }
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
    river_interaction_state_dump_status();
    river_playback_service_dump_status();
    river_voice_capture_dump_status();
    river_reference_service_dump_status();
    river_voice_echo_dump_status();
    river_voice_vad_probe_dump_status();
    river_wifi_station_dump_status();
    river_cloud_adapter_dump_status();
    river_online_control_dump_status();
    river_interaction_diag_dump_status();
}
