/* 唤醒准入桥：串行化 wakeword -> cloud conversation_window 打开。 */
#include <stdio.h>
#include <string.h>

#include "os_wrapper.h"
#include "rtk_status.h"

#include "river/river_dialog_cloud_port.h"
#include "river/river_dialog_runtime.h"
#include "river/river_dialog_wake_admission.h"
#include "river/river_log.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.session"

#define RIVER_DIALOG_WAKE_ADMISSION_TASK_STACK (1024U * 8U)
#define RIVER_DIALOG_WAKE_ADMISSION_TASK_PRIO  3U
#define RIVER_DIALOG_WAKE_ADMISSION_RETRY_MS   250U

typedef struct {
    rtos_mutex_t lock;
    rtos_sema_t signal;
    rtos_task_t task;
    bool initialized;
    bool task_running;
    bool pending;
    bool deferred_logged;
    int confidence;
    char text[32];
} river_dialog_wake_admission_context_t;

static river_dialog_wake_admission_context_t g_river_dialog_wake_admission;

static bool river_dialog_wake_admission_lock(void)
{
    return g_river_dialog_wake_admission.lock != NULL &&
           rtos_mutex_take(g_river_dialog_wake_admission.lock,
                           MUTEX_WAIT_TIMEOUT) == RTK_SUCCESS;
}

static void river_dialog_wake_admission_unlock(void)
{
    if (g_river_dialog_wake_admission.lock != NULL) {
        (void)rtos_mutex_give(g_river_dialog_wake_admission.lock);
    }
}

static void river_dialog_wake_admission_clear_locked(void)
{
    g_river_dialog_wake_admission.pending = false;
    g_river_dialog_wake_admission.deferred_logged = false;
    g_river_dialog_wake_admission.confidence = 0;
    g_river_dialog_wake_admission.text[0] = '\0';
}

static river_status_t river_dialog_wake_admission_try_inline(const char *text,
                                                             int confidence,
                                                             const char *reason)
{
    river_status_t status = river_dialog_cloud_begin_conversation_window("wakeword");

    RIVER_LOGW("wake admission inline path: reason=%s text=%s confidence=%d status=%d",
               reason != NULL && reason[0] != '\0' ? reason : "-",
               text != NULL && text[0] != '\0' ? text : "-",
               confidence,
               (int)status);
    if (status == RIVER_OK) {
        RIVER_LOGI("wakeword admission accepted: text=%s confidence=%d",
                   text != NULL && text[0] != '\0' ? text : "-",
                   confidence);
    } else if (status == RIVER_ERR_BUSY) {
        RIVER_LOGW("wakeword inline admission deferred text=%s confidence=%d status=%d",
                   text != NULL && text[0] != '\0' ? text : "-",
                   confidence,
                   (int)status);
    } else {
        RIVER_LOGE("wakeword inline admission failed: status=%d text=%s confidence=%d",
                   (int)status,
                   text != NULL && text[0] != '\0' ? text : "-",
                   confidence);
    }
    return status;
}

static void river_dialog_wake_admission_worker(void *param)
{
    char wake_text[sizeof(g_river_dialog_wake_admission.text)];
    int confidence;
    river_status_t status;
    bool log_deferred;

    (void)param;
    wake_text[0] = '\0';
    confidence = 0;

    for (;;) {
        if (rtos_sema_take(g_river_dialog_wake_admission.signal,
                           MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
            continue;
        }

        for (;;) {
            if (!river_dialog_wake_admission_lock()) {
                break;
            }
            if (!g_river_dialog_wake_admission.pending) {
                river_dialog_wake_admission_unlock();
                break;
            }

            snprintf(wake_text,
                     sizeof(wake_text),
                     "%s",
                     g_river_dialog_wake_admission.text);
            confidence = g_river_dialog_wake_admission.confidence;
            river_dialog_wake_admission_unlock();

            status = river_dialog_cloud_begin_conversation_window("wakeword");
            if (status == RIVER_OK) {
                if (river_dialog_wake_admission_lock()) {
                    river_dialog_wake_admission_clear_locked();
                    river_dialog_wake_admission_unlock();
                }
                RIVER_LOGI("wakeword admission accepted: text=%s confidence=%d",
                           wake_text[0] != '\0' ? wake_text : "-",
                           confidence);
                continue;
            }

            if (status != RIVER_ERR_BUSY) {
                if (river_dialog_wake_admission_lock()) {
                    river_dialog_wake_admission_clear_locked();
                    river_dialog_wake_admission_unlock();
                }
                RIVER_LOGE("wakeword admission failed: status=%d text=%s confidence=%d",
                           (int)status,
                           wake_text[0] != '\0' ? wake_text : "-",
                           confidence);
                continue;
            }

            log_deferred = false;
            if (river_dialog_wake_admission_lock()) {
                if (g_river_dialog_wake_admission.pending &&
                    !g_river_dialog_wake_admission.deferred_logged) {
                    g_river_dialog_wake_admission.deferred_logged = true;
                    log_deferred = true;
                }
                river_dialog_wake_admission_unlock();
            }
            if (log_deferred) {
                RIVER_LOGW("wakeword admission deferred; retry pending text=%s confidence=%d status=%d",
                           wake_text[0] != '\0' ? wake_text : "-",
                           confidence,
                           (int)status);
            }
            rtos_time_delay_ms(RIVER_DIALOG_WAKE_ADMISSION_RETRY_MS);
        }
    }
}

river_status_t river_dialog_wake_admission_init(void)
{
    if (g_river_dialog_wake_admission.initialized) {
        return RIVER_OK;
    }

    memset(&g_river_dialog_wake_admission, 0, sizeof(g_river_dialog_wake_admission));
    if (rtos_mutex_create(&g_river_dialog_wake_admission.lock) != RTK_SUCCESS) {
        return RIVER_ERR_NO_MEMORY;
    }
    if (rtos_sema_create_binary(&g_river_dialog_wake_admission.signal) != RTK_SUCCESS) {
        rtos_mutex_delete(g_river_dialog_wake_admission.lock);
        g_river_dialog_wake_admission.lock = NULL;
        return RIVER_ERR_NO_MEMORY;
    }
    g_river_dialog_wake_admission.task_running = true;
    if (rtos_task_create(&g_river_dialog_wake_admission.task,
                         "river_wake_evt",
                         river_dialog_wake_admission_worker,
                         NULL,
                         RIVER_DIALOG_WAKE_ADMISSION_TASK_STACK,
                         RIVER_DIALOG_WAKE_ADMISSION_TASK_PRIO) != RTK_SUCCESS) {
        g_river_dialog_wake_admission.task_running = false;
        rtos_sema_delete(g_river_dialog_wake_admission.signal);
        g_river_dialog_wake_admission.signal = NULL;
        rtos_mutex_delete(g_river_dialog_wake_admission.lock);
        g_river_dialog_wake_admission.lock = NULL;
        return RIVER_ERR_NO_MEMORY;
    }
    g_river_dialog_wake_admission.initialized = true;
    return RIVER_OK;
}

river_status_t river_dialog_wake_admission_submit(const char *text, int confidence)
{
    const char *block_reason;
    const char *wake_text = text != NULL ? text : "";

    block_reason = river_dialog_runtime_wakeword_admission_block_reason();
    if (block_reason != NULL) {
        RIVER_LOGI("wakeword ignored: reason=%s text=%s confidence=%d",
                   block_reason,
                   wake_text[0] != '\0' ? wake_text : "-",
                   confidence);
        return RIVER_OK;
    }

    if (!g_river_dialog_wake_admission.task_running ||
        g_river_dialog_wake_admission.signal == NULL ||
        g_river_dialog_wake_admission.lock == NULL) {
        return river_dialog_wake_admission_try_inline(wake_text,
                                                      confidence,
                                                      "worker_unavailable");
    }
    if (!river_dialog_wake_admission_lock()) {
        return river_dialog_wake_admission_try_inline(wake_text, confidence, "lock_busy");
    }

    if (g_river_dialog_wake_admission.pending) {
        if (confidence >= g_river_dialog_wake_admission.confidence) {
            snprintf(g_river_dialog_wake_admission.text,
                     sizeof(g_river_dialog_wake_admission.text),
                     "%s",
                     wake_text);
            g_river_dialog_wake_admission.confidence = confidence;
        }
        g_river_dialog_wake_admission.deferred_logged = false;
        river_dialog_wake_admission_unlock();
        RIVER_LOGI("wakeword coalesced while pending text=%s confidence=%d",
                   wake_text[0] != '\0' ? wake_text : "-",
                   confidence);
        return RIVER_OK;
    }

    snprintf(g_river_dialog_wake_admission.text,
             sizeof(g_river_dialog_wake_admission.text),
             "%s",
             wake_text);
    g_river_dialog_wake_admission.confidence = confidence;
    g_river_dialog_wake_admission.pending = true;
    g_river_dialog_wake_admission.deferred_logged = false;
    river_dialog_wake_admission_unlock();

    RIVER_LOGI("wakeword queued text=%s confidence=%d",
               wake_text[0] != '\0' ? wake_text : "-",
               confidence);

    if (rtos_sema_give(g_river_dialog_wake_admission.signal) != RTK_SUCCESS) {
        RIVER_LOGW("wakeword signal already pending; keep queued text=%s confidence=%d",
                   wake_text[0] != '\0' ? wake_text : "-",
                   confidence);
    }
    return RIVER_OK;
}
