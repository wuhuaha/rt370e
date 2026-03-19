#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "basic_types.h"
#include "os_wrapper.h"

#include "river/river_interaction_diag.h"
#include "river/river_log.h"
#include "river/river_online_control.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.interaction.diag"

#define RIVER_INTERACTION_DIAG_TTS_TASK_STACK    (1024U * 6U)
#define RIVER_INTERACTION_DIAG_TTS_TASK_PRIORITY 3U

typedef enum {
    RIVER_INTERACTION_DIAG_ACTION_NONE = 0,
    RIVER_INTERACTION_DIAG_ACTION_TTS_TEST = 1,
    RIVER_INTERACTION_DIAG_ACTION_DEVICE = 2
} river_interaction_diag_action_t;

typedef struct {
    const char *intent_name;
    const char *match_text;
    river_interaction_diag_action_t action;
    const char *device_name;
    const char *device_action;
} river_interaction_diag_intent_t;

typedef struct {
    bool initialized;
    bool deferred_tts_worker_running;
    rtos_mutex_t lock;
    rtos_sema_t deferred_tts_signal;
    rtos_task_t deferred_tts_task;
    uint32_t route_total;
    uint32_t route_matched;
    uint32_t route_missed;
    uint32_t execute_ok_total;
    uint32_t execute_fail_total;
    uint32_t tts_test_exec_total;
    uint32_t device_exec_total;
    uint32_t reply_ok_total;
    uint32_t reply_fail_total;
    int last_status;
    int last_reply_status;
    bool deferred_tts_pending;
    char last_source[24];
    char last_sid[48];
    char last_text[96];
    char last_intent[48];
    char last_action[32];
    char last_reply[96];
    char deferred_tts_source[24];
    char deferred_tts_intent[48];
    char deferred_tts_text[192];
} river_interaction_diag_context_t;

static const char *const k_river_interaction_diag_tts_test_text =
    "这是一段TTS测试音，你可以用它来测试AEC等功能";

static const river_interaction_diag_intent_t k_river_interaction_diag_intents[] = {
    {"tts_test_debug", "调试", RIVER_INTERACTION_DIAG_ACTION_TTS_TEST, NULL, NULL},
    {"light_on_open", "打开灯", RIVER_INTERACTION_DIAG_ACTION_DEVICE, "light", "on"},
    {"light_on_short", "开灯", RIVER_INTERACTION_DIAG_ACTION_DEVICE, "light", "on"},
    {"light_off_close", "关闭灯", RIVER_INTERACTION_DIAG_ACTION_DEVICE, "light", "off"},
    {"light_off_short", "关灯", RIVER_INTERACTION_DIAG_ACTION_DEVICE, "light", "off"},
    {"fan_on_open", "打开风扇", RIVER_INTERACTION_DIAG_ACTION_DEVICE, "fan", "on"},
    {"fan_on_short", "开风扇", RIVER_INTERACTION_DIAG_ACTION_DEVICE, "fan", "on"},
    {"fan_off_close", "关闭风扇", RIVER_INTERACTION_DIAG_ACTION_DEVICE, "fan", "off"},
    {"fan_off_short", "关风扇", RIVER_INTERACTION_DIAG_ACTION_DEVICE, "fan", "off"},
    {"curtain_on_open", "打开窗帘", RIVER_INTERACTION_DIAG_ACTION_DEVICE, "curtain", "on"},
    {"curtain_on_pull", "拉开窗帘", RIVER_INTERACTION_DIAG_ACTION_DEVICE, "curtain", "on"},
    {"curtain_off_close", "关闭窗帘", RIVER_INTERACTION_DIAG_ACTION_DEVICE, "curtain", "off"},
    {"curtain_off_pull", "拉上窗帘", RIVER_INTERACTION_DIAG_ACTION_DEVICE, "curtain", "off"},
    {"socket_on_open", "打开插座", RIVER_INTERACTION_DIAG_ACTION_DEVICE, "socket", "on"},
    {"socket_on_short", "开插座", RIVER_INTERACTION_DIAG_ACTION_DEVICE, "socket", "on"},
    {"socket_off_close", "关闭插座", RIVER_INTERACTION_DIAG_ACTION_DEVICE, "socket", "off"},
    {"socket_off_short", "关插座", RIVER_INTERACTION_DIAG_ACTION_DEVICE, "socket", "off"},
};

#define RIVER_INTERACTION_DIAG_INTENT_COUNT \
    ((uint32_t)(sizeof(k_river_interaction_diag_intents) / sizeof(k_river_interaction_diag_intents[0])))

static river_interaction_diag_context_t g_river_interaction_diag;

static void river_interaction_diag_copy_text(char *dst, size_t dst_size, const char *src);
static bool river_interaction_diag_lock(void);
static void river_interaction_diag_unlock(bool locked);
static bool river_interaction_diag_should_defer_tts(const char *source);
static river_status_t river_interaction_diag_store_deferred_tts(const char *tts_text,
                                                                const char *source,
                                                                const char *intent_name);
static river_status_t river_interaction_diag_submit_tts_text(const char *tts_text,
                                                             const char *source,
                                                             const char *intent_name);
static void river_interaction_diag_deferred_tts_task(void *param);

static river_status_t river_interaction_diag_run_tts_test(const char *source)
{
    RIVER_LOGI("interaction diag execute: source=%s action=tts_test",
               source != NULL ? source : "-");
    return river_interaction_diag_submit_tts_text(k_river_interaction_diag_tts_test_text,
                                                  source,
                                                  "tts_test_debug");
}

static river_status_t river_interaction_diag_run_device(const char *device_name,
                                                        const char *action_name,
                                                        const char *source)
{
    RIVER_LOGI("interaction diag execute: source=%s action=device_control device=%s command=%s",
               source != NULL ? source : "-",
               device_name,
               action_name);
    return river_online_control_set_device(device_name, action_name);
}

static const char *river_interaction_diag_device_display_name(const char *device_name)
{
    if (device_name == NULL) {
        return "设备";
    }
    if (strcmp(device_name, "light") == 0) {
        return "灯";
    }
    if (strcmp(device_name, "fan") == 0) {
        return "风扇";
    }
    if (strcmp(device_name, "curtain") == 0) {
        return "窗帘";
    }
    if (strcmp(device_name, "socket") == 0) {
        return "插座";
    }
    return device_name;
}

static const char *river_interaction_diag_action_display_name(const char *action_name)
{
    if (action_name == NULL) {
        return "执行";
    }
    if (strcmp(action_name, "on") == 0) {
        return "打开";
    }
    if (strcmp(action_name, "off") == 0) {
        return "关闭";
    }
    if (strcmp(action_name, "toggle") == 0) {
        return "切换";
    }
    return action_name;
}

static river_status_t river_interaction_diag_emit_reply(const char *reply_text,
                                                        const char *source,
                                                        const char *intent_name)
{
    river_status_t status;
    bool locked;

    if ((reply_text == NULL) || (reply_text[0] == '\0')) {
        return RIVER_ERR_ARG;
    }

    RIVER_LOGI("interaction diag reply: source=%s intent=%s text=%s",
               source != NULL ? source : "-",
               intent_name != NULL ? intent_name : "-",
               reply_text);
    status = river_interaction_diag_submit_tts_text(reply_text, source, intent_name);

    locked = river_interaction_diag_lock();
    river_interaction_diag_copy_text(g_river_interaction_diag.last_reply,
                                     sizeof(g_river_interaction_diag.last_reply),
                                     reply_text);
    g_river_interaction_diag.last_reply_status = (int)status;
    if (status == RIVER_OK) {
        g_river_interaction_diag.reply_ok_total++;
    } else {
        g_river_interaction_diag.reply_fail_total++;
    }
    river_interaction_diag_unlock(locked);
    return status;
}

static bool river_interaction_diag_should_defer_tts(const char *source)
{
    return (source != NULL) && (strcmp(source, "asr_final") == 0);
}

static river_status_t river_interaction_diag_store_deferred_tts(const char *tts_text,
                                                                const char *source,
                                                                const char *intent_name)
{
    bool locked;

    if ((tts_text == NULL) || (tts_text[0] == '\0')) {
        return RIVER_ERR_ARG;
    }

    locked = river_interaction_diag_lock();
    g_river_interaction_diag.deferred_tts_pending = true;
    river_interaction_diag_copy_text(g_river_interaction_diag.deferred_tts_text,
                                     sizeof(g_river_interaction_diag.deferred_tts_text),
                                     tts_text);
    river_interaction_diag_copy_text(g_river_interaction_diag.deferred_tts_source,
                                     sizeof(g_river_interaction_diag.deferred_tts_source),
                                     source);
    river_interaction_diag_copy_text(g_river_interaction_diag.deferred_tts_intent,
                                     sizeof(g_river_interaction_diag.deferred_tts_intent),
                                     intent_name);
    river_interaction_diag_unlock(locked);

    RIVER_LOGI("interaction diag defer tts: source=%s intent=%s text=%s",
               source != NULL ? source : "-",
               intent_name != NULL ? intent_name : "-",
               tts_text);
    return RIVER_OK;
}

static river_status_t river_interaction_diag_submit_tts_text(const char *tts_text,
                                                             const char *source,
                                                             const char *intent_name)
{
    if ((tts_text == NULL) || (tts_text[0] == '\0')) {
        return RIVER_ERR_ARG;
    }

    if (river_interaction_diag_should_defer_tts(source)) {
        return river_interaction_diag_store_deferred_tts(tts_text, source, intent_name);
    }

    return river_online_control_echo(tts_text);
}

static void river_interaction_diag_note_reply_status(const char *reply_text,
                                                     river_status_t status)
{
    bool locked;

    locked = river_interaction_diag_lock();
    river_interaction_diag_copy_text(g_river_interaction_diag.last_reply,
                                     sizeof(g_river_interaction_diag.last_reply),
                                     reply_text);
    g_river_interaction_diag.last_reply_status = (int)status;
    if (status == RIVER_OK) {
        g_river_interaction_diag.reply_ok_total++;
    } else {
        g_river_interaction_diag.reply_fail_total++;
    }
    river_interaction_diag_unlock(locked);
}

static void river_interaction_diag_deferred_tts_task(void *param)
{
    char tts_text[sizeof(g_river_interaction_diag.deferred_tts_text)];
    char source[sizeof(g_river_interaction_diag.deferred_tts_source)];
    char intent_name[sizeof(g_river_interaction_diag.deferred_tts_intent)];

    (void)param;

    while (1) {
        if (rtos_sema_take(g_river_interaction_diag.deferred_tts_signal,
                           RTOS_MAX_TIMEOUT) != RTK_SUCCESS) {
            continue;
        }

        while (1) {
            bool locked;
            river_status_t status;

            locked = river_interaction_diag_lock();
            if (!g_river_interaction_diag.deferred_tts_pending) {
                river_interaction_diag_unlock(locked);
                break;
            }

            river_interaction_diag_copy_text(tts_text, sizeof(tts_text), g_river_interaction_diag.deferred_tts_text);
            river_interaction_diag_copy_text(source, sizeof(source), g_river_interaction_diag.deferred_tts_source);
            river_interaction_diag_copy_text(intent_name, sizeof(intent_name), g_river_interaction_diag.deferred_tts_intent);
            g_river_interaction_diag.deferred_tts_pending = false;
            g_river_interaction_diag.deferred_tts_text[0] = '\0';
            g_river_interaction_diag.deferred_tts_source[0] = '\0';
            g_river_interaction_diag.deferred_tts_intent[0] = '\0';
            river_interaction_diag_unlock(locked);

            RIVER_LOGI("interaction diag async deferred tts: source=%s intent=%s text=%s",
                       source[0] != '\0' ? source : "-",
                       intent_name[0] != '\0' ? intent_name : "-",
                       tts_text);

            status = river_online_control_echo(tts_text);
            river_interaction_diag_note_reply_status(tts_text, status);
        }
    }
}

static void river_interaction_diag_copy_text(char *dst, size_t dst_size, const char *src)
{
    if ((dst == NULL) || (dst_size == 0U)) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    strncpy(dst, src, dst_size - 1U);
    dst[dst_size - 1U] = '\0';
}

static bool river_interaction_diag_lock(void)
{
    if (g_river_interaction_diag.initialized) {
        return rtos_mutex_take(g_river_interaction_diag.lock, MUTEX_WAIT_TIMEOUT) == RTK_SUCCESS;
    }
    return false;
}

static void river_interaction_diag_unlock(bool locked)
{
    if (locked && g_river_interaction_diag.initialized) {
        (void)rtos_mutex_give(g_river_interaction_diag.lock);
    }
}

static void river_interaction_diag_record_base(const char *source,
                                               const char *sid,
                                               const char *text)
{
    bool locked;

    locked = river_interaction_diag_lock();
    g_river_interaction_diag.route_total++;
    river_interaction_diag_copy_text(g_river_interaction_diag.last_source,
                                     sizeof(g_river_interaction_diag.last_source),
                                     source);
    river_interaction_diag_copy_text(g_river_interaction_diag.last_sid,
                                     sizeof(g_river_interaction_diag.last_sid),
                                     sid);
    river_interaction_diag_copy_text(g_river_interaction_diag.last_text,
                                     sizeof(g_river_interaction_diag.last_text),
                                     text);
    river_interaction_diag_copy_text(g_river_interaction_diag.last_reply,
                                     sizeof(g_river_interaction_diag.last_reply),
                                     NULL);
    g_river_interaction_diag.last_reply_status = RIVER_OK;
    river_interaction_diag_unlock(locked);
}

static void river_interaction_diag_record_result(const river_interaction_diag_intent_t *intent,
                                                 river_status_t status)
{
    bool locked;

    locked = river_interaction_diag_lock();
    if (intent != NULL) {
        g_river_interaction_diag.route_matched++;
        river_interaction_diag_copy_text(g_river_interaction_diag.last_intent,
                                         sizeof(g_river_interaction_diag.last_intent),
                                         intent->intent_name);
        river_interaction_diag_copy_text(g_river_interaction_diag.last_action,
                                         sizeof(g_river_interaction_diag.last_action),
                                         intent->action == RIVER_INTERACTION_DIAG_ACTION_TTS_TEST ?
                                             "tts_test" :
                                             intent->action == RIVER_INTERACTION_DIAG_ACTION_DEVICE ?
                                                 "device_control" :
                                                 "unknown");
        if (intent->action == RIVER_INTERACTION_DIAG_ACTION_TTS_TEST) {
            g_river_interaction_diag.tts_test_exec_total++;
        } else if (intent->action == RIVER_INTERACTION_DIAG_ACTION_DEVICE) {
            g_river_interaction_diag.device_exec_total++;
        }
    } else {
        g_river_interaction_diag.route_missed++;
        river_interaction_diag_copy_text(g_river_interaction_diag.last_intent,
                                         sizeof(g_river_interaction_diag.last_intent),
                                         "-");
        river_interaction_diag_copy_text(g_river_interaction_diag.last_action,
                                         sizeof(g_river_interaction_diag.last_action),
                                         "-");
    }
    if (status == RIVER_OK) {
        g_river_interaction_diag.execute_ok_total++;
    } else {
        g_river_interaction_diag.execute_fail_total++;
    }
    g_river_interaction_diag.last_status = (int)status;
    river_interaction_diag_unlock(locked);
}

static void river_interaction_diag_record_direct_action(const char *intent_name,
                                                        river_interaction_diag_action_t action,
                                                        const char *source,
                                                        const char *text,
                                                        river_status_t status)
{
    bool locked;

    locked = river_interaction_diag_lock();
    river_interaction_diag_copy_text(g_river_interaction_diag.last_source,
                                     sizeof(g_river_interaction_diag.last_source),
                                     source);
    river_interaction_diag_copy_text(g_river_interaction_diag.last_sid,
                                     sizeof(g_river_interaction_diag.last_sid),
                                     NULL);
    river_interaction_diag_copy_text(g_river_interaction_diag.last_text,
                                     sizeof(g_river_interaction_diag.last_text),
                                     text);
    river_interaction_diag_copy_text(g_river_interaction_diag.last_intent,
                                     sizeof(g_river_interaction_diag.last_intent),
                                     intent_name);
    river_interaction_diag_copy_text(g_river_interaction_diag.last_action,
                                     sizeof(g_river_interaction_diag.last_action),
                                     action == RIVER_INTERACTION_DIAG_ACTION_TTS_TEST ?
                                         "tts_test" :
                                         action == RIVER_INTERACTION_DIAG_ACTION_DEVICE ?
                                             "device_control" :
                                             "unknown");
    if (action == RIVER_INTERACTION_DIAG_ACTION_TTS_TEST) {
        g_river_interaction_diag.tts_test_exec_total++;
    } else if (action == RIVER_INTERACTION_DIAG_ACTION_DEVICE) {
        g_river_interaction_diag.device_exec_total++;
    }
    river_interaction_diag_copy_text(g_river_interaction_diag.last_reply,
                                     sizeof(g_river_interaction_diag.last_reply),
                                     NULL);
    g_river_interaction_diag.last_reply_status = RIVER_OK;
    if (status == RIVER_OK) {
        g_river_interaction_diag.execute_ok_total++;
    } else {
        g_river_interaction_diag.execute_fail_total++;
    }
    g_river_interaction_diag.last_status = (int)status;
    river_interaction_diag_unlock(locked);
}

static const river_interaction_diag_intent_t *
river_interaction_diag_match_intent(const char *text)
{
    uint32_t index;

    if ((text == NULL) || (text[0] == '\0')) {
        return NULL;
    }

    for (index = 0; index < RIVER_INTERACTION_DIAG_INTENT_COUNT; ++index) {
        if (strstr(text, k_river_interaction_diag_intents[index].match_text) != NULL) {
            return &k_river_interaction_diag_intents[index];
        }
    }

    return NULL;
}

static const river_interaction_diag_intent_t *
river_interaction_diag_find_intent_by_name(const char *intent_name)
{
    uint32_t index;

    if ((intent_name == NULL) || (intent_name[0] == '\0')) {
        return NULL;
    }

    for (index = 0; index < RIVER_INTERACTION_DIAG_INTENT_COUNT; ++index) {
        if (strcmp(k_river_interaction_diag_intents[index].intent_name, intent_name) == 0) {
            return &k_river_interaction_diag_intents[index];
        }
    }

    return NULL;
}

river_status_t river_interaction_diag_init(void)
{
    if (g_river_interaction_diag.initialized) {
        return RIVER_OK;
    }

    memset(&g_river_interaction_diag, 0, sizeof(g_river_interaction_diag));
    if (rtos_mutex_create(&g_river_interaction_diag.lock) != RTK_SUCCESS) {
        return RIVER_ERR_NO_MEMORY;
    }
    if (rtos_sema_create_binary(&g_river_interaction_diag.deferred_tts_signal) != RTK_SUCCESS) {
        rtos_mutex_delete(g_river_interaction_diag.lock);
        memset(&g_river_interaction_diag, 0, sizeof(g_river_interaction_diag));
        return RIVER_ERR_NO_MEMORY;
    }

    g_river_interaction_diag.initialized = true;
    g_river_interaction_diag.last_status = RIVER_OK;
    g_river_interaction_diag.last_reply_status = RIVER_OK;
    g_river_interaction_diag.deferred_tts_pending = false;
    g_river_interaction_diag.deferred_tts_worker_running = true;
    river_interaction_diag_copy_text(g_river_interaction_diag.last_intent,
                                     sizeof(g_river_interaction_diag.last_intent),
                                     "-");
    river_interaction_diag_copy_text(g_river_interaction_diag.last_action,
                                     sizeof(g_river_interaction_diag.last_action),
                                     "-");
    if (rtos_task_create(&g_river_interaction_diag.deferred_tts_task,
                         "river_diag_tts",
                         river_interaction_diag_deferred_tts_task,
                         NULL,
                         RIVER_INTERACTION_DIAG_TTS_TASK_STACK,
                         RIVER_INTERACTION_DIAG_TTS_TASK_PRIORITY) != RTK_SUCCESS) {
        g_river_interaction_diag.deferred_tts_worker_running = false;
        g_river_interaction_diag.initialized = false;
        rtos_sema_delete(g_river_interaction_diag.deferred_tts_signal);
        rtos_mutex_delete(g_river_interaction_diag.lock);
        memset(&g_river_interaction_diag, 0, sizeof(g_river_interaction_diag));
        return RIVER_ERR_NO_MEMORY;
    }
    RIVER_LOGI("interaction diag init: intents=%lu features=asr_route,device_control,tts_test,deferred_tts_async",
               (unsigned long)RIVER_INTERACTION_DIAG_INTENT_COUNT);
    return RIVER_OK;
}

river_status_t river_interaction_diag_submit_tts_test(const char *source)
{
    river_status_t status;

    if (river_interaction_diag_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }

    status = river_interaction_diag_run_tts_test(source);
    river_interaction_diag_record_direct_action("tts_test_debug",
                                                RIVER_INTERACTION_DIAG_ACTION_TTS_TEST,
                                                source,
                                                k_river_interaction_diag_tts_test_text,
                                                status);
    return status;
}

river_status_t river_interaction_diag_execute_device(const char *device_name,
                                                     const char *action_name,
                                                     const char *source)
{
    river_status_t status;
    char text[48];

    if ((device_name == NULL) || (action_name == NULL)) {
        return RIVER_ERR_ARG;
    }
    if (river_interaction_diag_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }

    status = river_interaction_diag_run_device(device_name, action_name, source);
    snprintf(text, sizeof(text), "%s %s", device_name, action_name);
    river_interaction_diag_record_direct_action("device_control",
                                                RIVER_INTERACTION_DIAG_ACTION_DEVICE,
                                                source,
                                                text,
                                                status);
    return status;
}

river_status_t river_interaction_diag_invoke_intent(const char *intent_name,
                                                    const char *source)
{
    const river_interaction_diag_intent_t *intent;
    river_status_t status;

    if (river_interaction_diag_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }

    intent = river_interaction_diag_find_intent_by_name(intent_name);
    if (intent == NULL) {
        return RIVER_ERR_NOT_FOUND;
    }

    if (intent->action == RIVER_INTERACTION_DIAG_ACTION_TTS_TEST) {
        status = river_interaction_diag_run_tts_test(source);
    } else if (intent->action == RIVER_INTERACTION_DIAG_ACTION_DEVICE) {
        status = river_interaction_diag_run_device(intent->device_name,
                                                   intent->device_action,
                                                   source);
        if (status == RIVER_OK) {
            char reply[64];

            snprintf(reply,
                     sizeof(reply),
                     "好的，已%s%s",
                     river_interaction_diag_action_display_name(intent->device_action),
                     river_interaction_diag_device_display_name(intent->device_name));
            (void)river_interaction_diag_emit_reply(reply, source, intent->intent_name);
        }
    } else {
        status = RIVER_ERR_UNSUPPORTED;
    }

    river_interaction_diag_record_direct_action(intent->intent_name,
                                                intent->action,
                                                source,
                                                intent->match_text,
                                                status);
    return status;
}

river_status_t river_interaction_diag_route_text(const char *text,
                                                const char *source,
                                                const char *sid)
{
    const river_interaction_diag_intent_t *intent;
    river_status_t status;

    if ((text == NULL) || (text[0] == '\0')) {
        return RIVER_ERR_ARG;
    }
    if (river_interaction_diag_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }

    river_interaction_diag_record_base(source, sid, text);
    intent = river_interaction_diag_match_intent(text);
    if (intent == NULL) {
        river_interaction_diag_record_result(NULL, RIVER_ERR_NOT_FOUND);
        return RIVER_ERR_NOT_FOUND;
    }

    if (intent->action == RIVER_INTERACTION_DIAG_ACTION_TTS_TEST) {
        status = river_interaction_diag_run_tts_test(source);
    } else if (intent->action == RIVER_INTERACTION_DIAG_ACTION_DEVICE) {
        status = river_interaction_diag_run_device(intent->device_name,
                                                   intent->device_action,
                                                   source);
        if (status == RIVER_OK) {
            char reply[64];

            snprintf(reply,
                     sizeof(reply),
                     "好的，已%s%s",
                     river_interaction_diag_action_display_name(intent->device_action),
                     river_interaction_diag_device_display_name(intent->device_name));
            (void)river_interaction_diag_emit_reply(reply, source, intent->intent_name);
        }
    } else {
        status = RIVER_ERR_UNSUPPORTED;
    }

    river_interaction_diag_record_result(intent, status);
    RIVER_LOGI("interaction diag matched: source=%s sid=%s intent=%s text=%s status=%d",
               source != NULL ? source : "-",
               sid != NULL ? sid : "-",
               intent->intent_name,
               text,
               (int)status);
    return status;
}

river_status_t river_interaction_diag_flush_deferred(void)
{
    char tts_text[sizeof(g_river_interaction_diag.deferred_tts_text)];
    char source[sizeof(g_river_interaction_diag.deferred_tts_source)];
    char intent_name[sizeof(g_river_interaction_diag.deferred_tts_intent)];
    bool locked;

    if (river_interaction_diag_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }

    locked = river_interaction_diag_lock();
    if (!g_river_interaction_diag.deferred_tts_pending) {
        river_interaction_diag_unlock(locked);
        return RIVER_ERR_NOT_FOUND;
    }

    river_interaction_diag_copy_text(tts_text, sizeof(tts_text), g_river_interaction_diag.deferred_tts_text);
    river_interaction_diag_copy_text(source, sizeof(source), g_river_interaction_diag.deferred_tts_source);
    river_interaction_diag_copy_text(intent_name, sizeof(intent_name), g_river_interaction_diag.deferred_tts_intent);
    river_interaction_diag_unlock(locked);

    RIVER_LOGI("interaction diag schedule deferred tts: source=%s intent=%s text=%s",
               source[0] != '\0' ? source : "-",
               intent_name[0] != '\0' ? intent_name : "-",
               tts_text);
    if (rtos_sema_give(g_river_interaction_diag.deferred_tts_signal) != RTK_SUCCESS) {
        return RIVER_ERR_BUSY;
    }
    return RIVER_OK;
}

void river_interaction_diag_dump_status(void)
{
    uint32_t route_total;
    uint32_t route_matched;
    uint32_t route_missed;
    uint32_t execute_ok_total;
    uint32_t execute_fail_total;
    uint32_t tts_test_exec_total;
    uint32_t device_exec_total;
    uint32_t reply_ok_total;
    uint32_t reply_fail_total;
    int last_status;
    int last_reply_status;
    bool deferred_tts_pending;
    bool deferred_tts_worker_running;
    char last_source[sizeof(g_river_interaction_diag.last_source)];
    char last_sid[sizeof(g_river_interaction_diag.last_sid)];
    char last_text[sizeof(g_river_interaction_diag.last_text)];
    char last_intent[sizeof(g_river_interaction_diag.last_intent)];
    char last_action[sizeof(g_river_interaction_diag.last_action)];
    char last_reply[sizeof(g_river_interaction_diag.last_reply)];
    char deferred_tts_text[sizeof(g_river_interaction_diag.deferred_tts_text)];

    route_total = 0U;
    route_matched = 0U;
    route_missed = 0U;
    execute_ok_total = 0U;
    execute_fail_total = 0U;
    tts_test_exec_total = 0U;
    device_exec_total = 0U;
    reply_ok_total = 0U;
    reply_fail_total = 0U;
    last_status = RIVER_OK;
    last_reply_status = RIVER_OK;
    deferred_tts_pending = false;
    deferred_tts_worker_running = false;
    last_source[0] = '\0';
    last_sid[0] = '\0';
    last_text[0] = '\0';
    last_intent[0] = '\0';
    last_action[0] = '\0';
    last_reply[0] = '\0';
    deferred_tts_text[0] = '\0';

    if (g_river_interaction_diag.initialized) {
        bool locked;

        locked = river_interaction_diag_lock();
        route_total = g_river_interaction_diag.route_total;
        route_matched = g_river_interaction_diag.route_matched;
        route_missed = g_river_interaction_diag.route_missed;
        execute_ok_total = g_river_interaction_diag.execute_ok_total;
        execute_fail_total = g_river_interaction_diag.execute_fail_total;
        tts_test_exec_total = g_river_interaction_diag.tts_test_exec_total;
        device_exec_total = g_river_interaction_diag.device_exec_total;
        reply_ok_total = g_river_interaction_diag.reply_ok_total;
        reply_fail_total = g_river_interaction_diag.reply_fail_total;
        last_status = g_river_interaction_diag.last_status;
        last_reply_status = g_river_interaction_diag.last_reply_status;
        deferred_tts_pending = g_river_interaction_diag.deferred_tts_pending;
        deferred_tts_worker_running = g_river_interaction_diag.deferred_tts_worker_running;
        memcpy(last_source, g_river_interaction_diag.last_source, sizeof(last_source));
        memcpy(last_sid, g_river_interaction_diag.last_sid, sizeof(last_sid));
        memcpy(last_text, g_river_interaction_diag.last_text, sizeof(last_text));
        memcpy(last_intent, g_river_interaction_diag.last_intent, sizeof(last_intent));
        memcpy(last_action, g_river_interaction_diag.last_action, sizeof(last_action));
        memcpy(last_reply, g_river_interaction_diag.last_reply, sizeof(last_reply));
        memcpy(deferred_tts_text,
               g_river_interaction_diag.deferred_tts_text,
               sizeof(deferred_tts_text));
        river_interaction_diag_unlock(locked);
    }

    RIVER_LOGI("interaction_diag=enabled intents=%lu routed=%lu matched=%lu missed=%lu exec_ok=%lu exec_fail=%lu tts_test=%lu device_exec=%lu reply_ok=%lu reply_fail=%lu deferred_tts=%s deferred_worker=%s last_intent=%s last_action=%s last_status=%d last_reply_status=%d last_source=%s last_sid=%s last_text=%s last_reply=%s deferred_text=%s",
               (unsigned long)RIVER_INTERACTION_DIAG_INTENT_COUNT,
               (unsigned long)route_total,
               (unsigned long)route_matched,
               (unsigned long)route_missed,
               (unsigned long)execute_ok_total,
               (unsigned long)execute_fail_total,
               (unsigned long)tts_test_exec_total,
               (unsigned long)device_exec_total,
               (unsigned long)reply_ok_total,
               (unsigned long)reply_fail_total,
               deferred_tts_pending ? "yes" : "no",
               deferred_tts_worker_running ? "running" : "off",
               last_intent[0] != '\0' ? last_intent : "-",
               last_action[0] != '\0' ? last_action : "-",
               last_status,
               last_reply_status,
               last_source[0] != '\0' ? last_source : "-",
               last_sid[0] != '\0' ? last_sid : "-",
               last_text[0] != '\0' ? last_text : "-",
               last_reply[0] != '\0' ? last_reply : "-",
               deferred_tts_text[0] != '\0' ? deferred_tts_text : "-");
}

void river_interaction_diag_dump_intents(void)
{
    uint32_t index;

    RIVER_LOGI("interaction_diag_intents:");
    for (index = 0; index < RIVER_INTERACTION_DIAG_INTENT_COUNT; ++index) {
        const river_interaction_diag_intent_t *intent;

        intent = &k_river_interaction_diag_intents[index];
        RIVER_LOGI("  name=%s match=%s action=%s device=%s command=%s",
                   intent->intent_name,
                   intent->match_text,
                   intent->action == RIVER_INTERACTION_DIAG_ACTION_TTS_TEST ?
                       "tts_test" :
                       intent->action == RIVER_INTERACTION_DIAG_ACTION_DEVICE ?
                           "device_control" :
                           "unknown",
                   intent->device_name != NULL ? intent->device_name : "-",
                   intent->device_action != NULL ? intent->device_action : "-");
    }
}
