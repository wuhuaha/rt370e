/* 会话协调器：衔接唤醒、云端会话、播放状态和交互状态机。 */
#include <stdio.h>
#include <string.h>

#include "basic_types.h"
#include "os_wrapper.h"

#include "river/river_cloud.h"
#include "river/river_interaction_diag.h"
#include "river/river_interaction_state.h"
#include "river/river_log.h"
#include "river/river_playback_service.h"
#include "river_session_coordinator.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.session"

#define RIVER_SESSION_WAKEWORD_TASK_STACK (1024U * 8U)
#define RIVER_SESSION_WAKEWORD_TASK_PRIO  3U
#define RIVER_SESSION_WAKEWORD_RETRY_MS   250U

typedef struct {
    rtos_mutex_t lock;
    rtos_sema_t signal;
    rtos_task_t task;
    bool task_running;
    bool pending;
    bool deferred_logged;
    int confidence;
    char text[32];
} river_session_wakeword_context_t;

typedef struct {
    bool initialized;
    char last_partial[192];
    bool asr_session_active;
    bool barge_in_interrupt_requested;
    river_session_wakeword_context_t wakeword;
} river_session_coordinator_context_t;

static river_session_coordinator_context_t g_river_session_coordinator;

static bool river_session_wakeword_lock(void)
{
    return g_river_session_coordinator.wakeword.lock != NULL &&
           rtos_mutex_take(g_river_session_coordinator.wakeword.lock,
                           MUTEX_WAIT_TIMEOUT) == RTK_SUCCESS;
}

static void river_session_wakeword_unlock(void)
{
    if (g_river_session_coordinator.wakeword.lock != NULL) {
        (void)rtos_mutex_give(g_river_session_coordinator.wakeword.lock);
    }
}

static void river_session_wakeword_clear_locked(void)
{
    g_river_session_coordinator.wakeword.pending = false;
    g_river_session_coordinator.wakeword.deferred_logged = false;
    g_river_session_coordinator.wakeword.confidence = 0;
    g_river_session_coordinator.wakeword.text[0] = '\0';
}

static bool river_session_playback_state_active(river_playback_state_t state)
{
    return river_playback_service_state_active(state);
}

void river_session_coordinator_sync_interaction_state(const char *reason)
{
    if (river_session_playback_state_active(river_playback_service_state())) {
        river_interaction_state_set(g_river_session_coordinator.asr_session_active ?
                                        RIVER_INTERACTION_BARGE_IN_LISTENING :
                                        RIVER_INTERACTION_SPEAKING,
                                    reason);
        return;
    }

    river_interaction_state_set(g_river_session_coordinator.asr_session_active ?
                                    RIVER_INTERACTION_ASR_STREAMING :
                                    (river_cloud_adapter_conversation_window_active() ?
                                         RIVER_INTERACTION_FOLLOW_UP :
                                         RIVER_INTERACTION_WAKE_MONITORING),
                                reason);
}

static void river_session_wakeword_worker(void *param)
{
    char wake_text[sizeof(g_river_session_coordinator.wakeword.text)];
    int confidence;
    river_status_t status;
    bool log_deferred;

    (void)param;
    wake_text[0] = '\0';
    confidence = 0;

    for (;;) {
        if (rtos_sema_take(g_river_session_coordinator.wakeword.signal,
                           MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
            continue;
        }

        for (;;) {
            if (!river_session_wakeword_lock()) {
                break;
            }
            if (!g_river_session_coordinator.wakeword.pending) {
                river_session_wakeword_unlock();
                break;
            }

            snprintf(wake_text,
                     sizeof(wake_text),
                     "%s",
                     g_river_session_coordinator.wakeword.text);
            confidence = g_river_session_coordinator.wakeword.confidence;
            river_session_wakeword_unlock();

            status = river_cloud_adapter_begin_conversation_window("wakeword");
            if (status == RIVER_OK) {
                if (river_session_wakeword_lock()) {
                    river_session_wakeword_clear_locked();
                    river_session_wakeword_unlock();
                }
                river_interaction_state_set(RIVER_INTERACTION_WAKE_CONFIRMED,
                                            "wakeword_detected");
                continue;
            }

            if (status != RIVER_ERR_BUSY) {
                if (river_session_wakeword_lock()) {
                    river_session_wakeword_clear_locked();
                    river_session_wakeword_unlock();
                }
                RIVER_LOGE("wakeword admission failed: status=%d text=%s confidence=%d",
                           (int)status,
                           wake_text[0] != '\0' ? wake_text : "-",
                           confidence);
                continue;
            }

            log_deferred = false;
            if (river_session_wakeword_lock()) {
                if (g_river_session_coordinator.wakeword.pending &&
                    !g_river_session_coordinator.wakeword.deferred_logged) {
                    g_river_session_coordinator.wakeword.deferred_logged = true;
                    log_deferred = true;
                }
                river_session_wakeword_unlock();
            }
            if (log_deferred) {
                RIVER_LOGW("wakeword admission deferred; retry pending text=%s confidence=%d status=%d",
                           wake_text[0] != '\0' ? wake_text : "-",
                           confidence,
                           (int)status);
            }
            rtos_time_delay_ms(RIVER_SESSION_WAKEWORD_RETRY_MS);
        }
    }
}

static river_status_t river_session_wakeword_worker_init(void)
{
    memset(&g_river_session_coordinator.wakeword,
           0,
           sizeof(g_river_session_coordinator.wakeword));

    if (rtos_mutex_create(&g_river_session_coordinator.wakeword.lock) != RTK_SUCCESS) {
        return RIVER_ERR_NO_MEMORY;
    }
    if (rtos_sema_create_binary(&g_river_session_coordinator.wakeword.signal) != RTK_SUCCESS) {
        rtos_mutex_delete(g_river_session_coordinator.wakeword.lock);
        g_river_session_coordinator.wakeword.lock = NULL;
        return RIVER_ERR_NO_MEMORY;
    }
    g_river_session_coordinator.wakeword.task_running = true;
    if (rtos_task_create(&g_river_session_coordinator.wakeword.task,
                         "river_wake_evt",
                         river_session_wakeword_worker,
                         NULL,
                         RIVER_SESSION_WAKEWORD_TASK_STACK,
                         RIVER_SESSION_WAKEWORD_TASK_PRIO) != RTK_SUCCESS) {
        g_river_session_coordinator.wakeword.task_running = false;
        rtos_sema_delete(g_river_session_coordinator.wakeword.signal);
        g_river_session_coordinator.wakeword.signal = NULL;
        rtos_mutex_delete(g_river_session_coordinator.wakeword.lock);
        g_river_session_coordinator.wakeword.lock = NULL;
        return RIVER_ERR_NO_MEMORY;
    }
    return RIVER_OK;
}

static river_status_t river_session_schedule_wakeword(const river_voice_event_t *event)
{
    river_interaction_state_t state;

    if (event == NULL) {
        return RIVER_ERR_ARG;
    }
    if (!g_river_session_coordinator.wakeword.task_running ||
        g_river_session_coordinator.wakeword.signal == NULL ||
        g_river_session_coordinator.wakeword.lock == NULL) {
        return RIVER_ERR_NOT_FOUND;
    }

    state = river_interaction_state_get();
    if (river_cloud_adapter_conversation_window_active()) {
        RIVER_LOGI("wakeword ignored: conversation window already active text=%s confidence=%d",
                   event->text != NULL ? event->text : "-",
                   event->confidence);
        return RIVER_OK;
    }
    if (state != RIVER_INTERACTION_WAKE_MONITORING) {
        RIVER_LOGI("wakeword ignored: interaction_state=%s text=%s confidence=%d",
                   river_interaction_state_name(state),
                   event->text != NULL ? event->text : "-",
                   event->confidence);
        return RIVER_OK;
    }
    if (!river_session_wakeword_lock()) {
        return RIVER_ERR_BUSY;
    }

    if (g_river_session_coordinator.wakeword.pending) {
        if (event->confidence >= g_river_session_coordinator.wakeword.confidence) {
            snprintf(g_river_session_coordinator.wakeword.text,
                     sizeof(g_river_session_coordinator.wakeword.text),
                     "%s",
                     event->text != NULL ? event->text : "");
            g_river_session_coordinator.wakeword.confidence = event->confidence;
        }
        g_river_session_coordinator.wakeword.deferred_logged = false;
        river_session_wakeword_unlock();
        RIVER_LOGI("wakeword coalesced while pending text=%s confidence=%d",
                   event->text != NULL ? event->text : "-",
                   event->confidence);
        return RIVER_OK;
    }

    snprintf(g_river_session_coordinator.wakeword.text,
             sizeof(g_river_session_coordinator.wakeword.text),
             "%s",
             event->text != NULL ? event->text : "");
    g_river_session_coordinator.wakeword.confidence = event->confidence;
    g_river_session_coordinator.wakeword.pending = true;
    g_river_session_coordinator.wakeword.deferred_logged = false;
    river_session_wakeword_unlock();

    RIVER_LOGI("wakeword queued text=%s confidence=%d",
               event->text != NULL ? event->text : "-",
               event->confidence);

    if (rtos_sema_give(g_river_session_coordinator.wakeword.signal) != RTK_SUCCESS) {
        return RIVER_ERR_BUSY;
    }
    return RIVER_OK;
}

static void river_session_try_interrupt_playback_on_asr_text(
    const river_cloud_asr_result_t *result)
{
    river_interaction_state_t interaction_state;

    if (result == NULL || result->text == NULL || result->text[0] == '\0') {
        return;
    }
    if (!g_river_session_coordinator.asr_session_active ||
        g_river_session_coordinator.barge_in_interrupt_requested) {
        return;
    }
    if (!river_session_playback_state_active(river_playback_service_state())) {
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
        g_river_session_coordinator.barge_in_interrupt_requested = true;
    }
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
        g_river_session_coordinator.asr_session_active = false;
        g_river_session_coordinator.barge_in_interrupt_requested = false;
        g_river_session_coordinator.last_partial[0] = '\0';
        RIVER_LOGE("asr provider=%s error code=%d sid=%s msg=%s",
                   result->provider_name != NULL ? result->provider_name : "-",
                   result->code,
                   result->sid != NULL ? result->sid : "-",
                   result->message != NULL ? result->message : "-");
        river_interaction_state_set(RIVER_INTERACTION_ERROR_RECOVERING, "asr_error");
        break;
    case RIVER_CLOUD_ASR_EVENT_SESSION_STARTED:
        g_river_session_coordinator.asr_session_active = true;
        g_river_session_coordinator.barge_in_interrupt_requested = false;
        g_river_session_coordinator.last_partial[0] = '\0';
        RIVER_LOGI("asr provider=%s session started sid=%s",
                   result->provider_name != NULL ? result->provider_name : "-",
                   result->sid != NULL ? result->sid : "-");
        river_session_coordinator_sync_interaction_state("asr_session_started");
        break;
    case RIVER_CLOUD_ASR_EVENT_SESSION_CLOSED:
        g_river_session_coordinator.asr_session_active = false;
        g_river_session_coordinator.barge_in_interrupt_requested = false;
        g_river_session_coordinator.last_partial[0] = '\0';
        RIVER_LOGI("asr provider=%s session closed sid=%s",
                   result->provider_name != NULL ? result->provider_name : "-",
                   result->sid != NULL ? result->sid : "-");
        river_session_coordinator_sync_interaction_state("asr_session_closed");
        (void)river_interaction_diag_flush_deferred();
        break;
    default:
        break;
    }
}

void river_session_coordinator_on_playback_state(
    river_playback_state_t state,
    const river_playback_stream_config_t *config,
    void *user_data)
{
    (void)config;
    (void)user_data;

    if (state == RIVER_PLAYBACK_ERROR) {
        river_interaction_state_set(RIVER_INTERACTION_ERROR_RECOVERING, "playback_error");
        return;
    }

    river_session_coordinator_sync_interaction_state("playback_state");
}

void river_session_coordinator_on_voice_event(const river_voice_event_t *event)
{
    if (event == NULL) {
        return;
    }

    switch (event->type) {
    case RIVER_VOICE_EVENT_WAKEWORD:
        if (river_session_schedule_wakeword(event) == RIVER_OK) {
            break;
        }
        RIVER_LOGW("wakeword worker unavailable; falling back to inline admission");
        if (river_cloud_adapter_begin_conversation_window("wakeword") == RIVER_OK) {
            river_interaction_state_set(RIVER_INTERACTION_WAKE_CONFIRMED,
                                        "wakeword_detected");
        }
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
    if (river_session_wakeword_worker_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    g_river_session_coordinator.initialized = true;
    return RIVER_OK;
}
