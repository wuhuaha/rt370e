/* 会话协调器：衔接唤醒、云端会话、播放状态和交互状态机。 */
#include <stdio.h>
#include <string.h>

#include "river/river_cloud.h"
#include "river/river_dialog_cloud_port.h"
#include "river/river_dialog_runtime.h"
#include "river/river_dialog_wake_admission.h"
#include "river/river_interaction_diag.h"
#include "river/river_log.h"
#include "river/river_voice_kws.h"
#include "river_session_coordinator.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.session"

typedef struct {
    bool initialized;
    char last_partial[192];
} river_session_coordinator_context_t;

static river_session_coordinator_context_t g_river_session_coordinator;

static void river_session_try_interrupt_playback_on_asr_text(
    const river_cloud_asr_result_t *result)
{
    if (result == NULL || result->text == NULL || result->text[0] == '\0') {
        return;
    }
    if (!river_dialog_runtime_allows_barge_in_interrupt()) {
        return;
    }

    RIVER_LOGI("barge-in text confirmed during playback: sid=%s text=%s -> interrupt tts",
               result->sid != NULL ? result->sid : "-",
               result->text);
    (void)river_dialog_cloud_interrupt_tts_with_reason("asr_text_confirmed");
}

void river_session_coordinator_on_cloud_asr_result(const river_cloud_asr_result_t *result,
                                                   void *user_data)
{
    (void)user_data;

    if (result == NULL) {
        return;
    }

    switch (result->type) {
    case RIVER_CLOUD_ASR_EVENT_PARTIAL:
        if ((result->text != NULL) && (result->text[0] != '\0') &&
            (strcmp(g_river_session_coordinator.last_partial, result->text) != 0)) {
            snprintf(g_river_session_coordinator.last_partial,
                     sizeof(g_river_session_coordinator.last_partial),
                     "%s",
                     result->text);
            RIVER_LOGI("asr provider=%s partial sid=%s text=%s",
                       result->provider_name != NULL ? result->provider_name : "-",
                       result->sid != NULL ? result->sid : "-",
                       result->text);
            river_session_try_interrupt_playback_on_asr_text(result);
        }
        break;
    case RIVER_CLOUD_ASR_EVENT_FINAL:
        g_river_session_coordinator.last_partial[0] = '\0';
        RIVER_LOGI("asr provider=%s final sid=%s text=%s",
                   result->provider_name != NULL ? result->provider_name : "-",
                   result->sid != NULL ? result->sid : "-",
                   result->text != NULL ? result->text : "-");
        if ((result->text != NULL) && (result->text[0] != '\0')) {
            river_session_try_interrupt_playback_on_asr_text(result);
            (void)river_interaction_diag_route_text(result->text, "asr_final", result->sid);
        }
        break;
    case RIVER_CLOUD_ASR_EVENT_ERROR:
        g_river_session_coordinator.last_partial[0] = '\0';
        RIVER_LOGE("asr provider=%s error code=%d sid=%s msg=%s",
                   result->provider_name != NULL ? result->provider_name : "-",
                   result->code,
                   result->sid != NULL ? result->sid : "-",
                   result->message != NULL ? result->message : "-");
        break;
    case RIVER_CLOUD_ASR_EVENT_SESSION_STARTED:
        g_river_session_coordinator.last_partial[0] = '\0';
        RIVER_LOGI("asr provider=%s session started sid=%s",
                   result->provider_name != NULL ? result->provider_name : "-",
                   result->sid != NULL ? result->sid : "-");
        break;
    case RIVER_CLOUD_ASR_EVENT_SESSION_CLOSED:
        g_river_session_coordinator.last_partial[0] = '\0';
        RIVER_LOGI("asr provider=%s session closed sid=%s",
                   result->provider_name != NULL ? result->provider_name : "-",
                   result->sid != NULL ? result->sid : "-");
        (void)river_interaction_diag_flush_deferred();
        break;
    default:
        break;
    }
}

void river_session_coordinator_on_voice_event(const river_voice_event_t *event)
{
    const char *block_reason;

    if (event == NULL) {
        return;
    }

    switch (event->type) {
    case RIVER_VOICE_EVENT_WAKEWORD:
        block_reason = river_voice_kws_wake_handoff_block_reason();
        if (block_reason != NULL) {
            RIVER_LOGW("wakeword handoff held: reason=%s text=%s confidence=%d",
                       block_reason,
                       event->text != NULL ? event->text : "-",
                       event->confidence);
            break;
        }
        (void)river_dialog_wake_admission_submit(event->text, event->confidence);
        break;
    default:
        RIVER_LOGD("voice event=%d confidence=%d", event->type, event->confidence);
        if (event->text != NULL) {
            RIVER_LOGD("voice text=%s", event->text);
        }
        break;
    }
}

river_status_t river_session_coordinator_init(void)
{
    if (g_river_session_coordinator.initialized) {
        return RIVER_OK;
    }

    memset(&g_river_session_coordinator, 0, sizeof(g_river_session_coordinator));
    if (river_dialog_wake_admission_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    g_river_session_coordinator.initialized = true;
    return RIVER_OK;
}
