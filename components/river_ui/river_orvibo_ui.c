#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "os_wrapper.h"

#include "river/river_log.h"
#include "river/river_orvibo_ui.h"

#include "river_orvibo_ui_internal.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.ui"

#define RIVER_UI_TASK_STACK       (1024U * 4U)
#define RIVER_UI_TASK_PRIORITY    4U
#define RIVER_UI_QUEUE_DEPTH      12U
#define RIVER_UI_RTOS_OK          0

typedef enum {
    RIVER_UI_MSG_STATE = 0,
    RIVER_UI_MSG_EMOJI,
    RIVER_UI_MSG_ASR,
    RIVER_UI_MSG_TTS,
    RIVER_UI_MSG_LLM,
    RIVER_UI_MSG_TOUCH_SCAN
} river_ui_msg_type_t;

typedef struct {
    river_ui_msg_type_t type;
    river_orvibo_state_t state;
    char text[192];
    char detail[64];
} river_ui_msg_t;

typedef struct {
    bool initialized;
    bool enabled;
    bool task_running;
    rtos_task_t task;
    rtos_queue_t queue;
    river_orvibo_state_t voice_state;
    river_orvibo_ui_view_state_t view;
    river_orvibo_ui_status_t status;
} river_ui_context_t;

static river_ui_context_t g_river_ui;

typedef struct {
    river_orvibo_state_t state;
    const char *emoji;
} river_ui_state_emoji_rule_t;

typedef struct {
    const char *emotion;
    const char *emoji;
} river_ui_emotion_emoji_rule_t;

static const river_ui_state_emoji_rule_t g_river_ui_state_emoji_rules[] = {
    {RIVER_ORVIBO_STATE_STARTING, "noto_cat_face_1f431"},
    {RIVER_ORVIBO_STATE_NETWORK_WAIT, "noto_smile_cat_1f638"},
    {RIVER_ORVIBO_STATE_IDLE, "noto_cat_face_1f431"},
    {RIVER_ORVIBO_STATE_CONNECTING, "noto_smile_cat_1f638"},
    {RIVER_ORVIBO_STATE_LISTENING, "noto_smiley_cat_1f63a"},
    {RIVER_ORVIBO_STATE_SPEAKING, "noto_joy_cat_1f639"},
    {RIVER_ORVIBO_STATE_RECOVERING, "noto_pouting_cat_1f63e"},
    {RIVER_ORVIBO_STATE_ERROR, "noto_scream_cat_1f640"},
};

static const river_ui_emotion_emoji_rule_t g_river_ui_emotion_emoji_rules[] = {
    {"relaxed", "noto_smiley_cat_1f63a"},
    {"neutral", "noto_smiley_cat_1f63a"},
    {"happy", "noto_joy_cat_1f639"},
    {"excited", "noto_joy_cat_1f639"},
    {"love", "noto_heart_eyes_cat_1f63b"},
    {"loving", "noto_heart_eyes_cat_1f63b"},
    {"thinking", "noto_smirk_cat_1f63c"},
    {"sad", "noto_crying_cat_1f63f"},
    {"crying", "noto_crying_cat_1f63f"},
    {"angry", "noto_pouting_cat_1f63e"},
    {"surprise", "noto_scream_cat_1f640"},
    {"surprised", "noto_scream_cat_1f640"},
    {"afraid", "noto_scream_cat_1f640"},
    {"fear", "noto_scream_cat_1f640"},
};

static char river_ui_ascii_tolower(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }
    return ch;
}

static bool river_ui_streq_ci(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return false;
    }
    while (*a != '\0' && *b != '\0') {
        if (river_ui_ascii_tolower(*a) != river_ui_ascii_tolower(*b)) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static size_t river_ui_utf8_char_len(uint8_t byte)
{
    if (byte < 0x80U) {
        return 1U;
    }
    if ((byte & 0xE0U) == 0xC0U) {
        return 2U;
    }
    if ((byte & 0xF0U) == 0xE0U) {
        return 3U;
    }
    if ((byte & 0xF8U) == 0xF0U) {
        return 4U;
    }
    return 0U;
}

static void river_ui_trim_utf8(char *text)
{
    size_t i = 0U;
    size_t valid = 0U;
    size_t len;

    if (text == NULL) {
        return;
    }
    len = strlen(text);
    while (i < len) {
        uint8_t byte = (uint8_t)text[i];
        size_t char_len = river_ui_utf8_char_len(byte);

        if (char_len == 0U || i + char_len > len) {
            break;
        }
        for (size_t j = 1U; j < char_len; j++) {
            if (((uint8_t)text[i + j] & 0xC0U) != 0x80U) {
                text[valid] = '\0';
                return;
            }
        }
        i += char_len;
        valid = i;
    }
    text[valid] = '\0';
}

static void river_ui_copy(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0U) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_size - 1U);
    dst[dst_size - 1U] = '\0';
    river_ui_trim_utf8(dst);
}

static const char *river_ui_state_name(river_orvibo_state_t state)
{
    return river_orvibo_state_name(state);
}

static const char *river_ui_state_emoji(river_orvibo_state_t state)
{
    for (size_t i = 0U; i < sizeof(g_river_ui_state_emoji_rules) /
                              sizeof(g_river_ui_state_emoji_rules[0]); ++i) {
        if (g_river_ui_state_emoji_rules[i].state == state) {
            return g_river_ui_state_emoji_rules[i].emoji;
        }
    }
    return "noto_cat_face_1f431";
}

static const char *river_ui_emotion_emoji(const char *emotion)
{
    if (emotion == NULL || emotion[0] == '\0') {
        return NULL;
    }
    if (strncmp(emotion, "noto_", 5) == 0) {
        return emotion;
    }
    for (size_t i = 0U; i < sizeof(g_river_ui_emotion_emoji_rules) /
                              sizeof(g_river_ui_emotion_emoji_rules[0]); ++i) {
        if (river_ui_streq_ci(emotion, g_river_ui_emotion_emoji_rules[i].emotion)) {
            return g_river_ui_emotion_emoji_rules[i].emoji;
        }
    }
    return NULL;
}

static void river_ui_sync_status(void)
{
    g_river_ui.status.enabled = g_river_ui.enabled;
    g_river_ui.status.task_running = g_river_ui.task_running;
#if defined(CONFIG_RIVER_UI_LVGL_EN) && CONFIG_RIVER_UI_LVGL_EN
    g_river_ui.status.lvgl_ready = river_lvgl_port_ready();
    g_river_ui.status.panel_ready = river_st7102_mipi_panel_ready();
#else
    g_river_ui.status.lvgl_ready = false;
    g_river_ui.status.panel_ready = false;
#endif
#if defined(CONFIG_RIVER_UI_TOUCH_PROBE_EN) && CONFIG_RIVER_UI_TOUCH_PROBE_EN
    g_river_ui.status.touch_ready = river_touch_sitronix_probe_ready();
#else
    g_river_ui.status.touch_ready = false;
#endif
    river_ui_copy(g_river_ui.status.state,
                  sizeof(g_river_ui.status.state),
                  g_river_ui.view.state);
    river_ui_copy(g_river_ui.status.emoji,
                  sizeof(g_river_ui.status.emoji),
                  g_river_ui.view.emoji);
    river_ui_copy(g_river_ui.status.asr_text,
                  sizeof(g_river_ui.status.asr_text),
                  g_river_ui.view.asr_text);
    river_ui_copy(g_river_ui.status.tts_text,
                  sizeof(g_river_ui.status.tts_text),
                  g_river_ui.view.tts_text);
    river_ui_copy(g_river_ui.status.touch_summary,
                  sizeof(g_river_ui.status.touch_summary),
                  g_river_ui.view.touch_summary);
}

static river_status_t river_ui_post(const river_ui_msg_t *msg)
{
#if defined(CONFIG_RIVER_UI_EN) && CONFIG_RIVER_UI_EN
    if (msg == NULL || !g_river_ui.initialized || g_river_ui.queue == NULL) {
        return RIVER_ERR_INVALID_STATE;
    }
    if (rtos_queue_send(g_river_ui.queue, (void *)msg, 0U) != RIVER_UI_RTOS_OK) {
        g_river_ui.status.post_fail++;
        return RIVER_ERR_BUSY;
    }
    g_river_ui.status.posted++;
    return RIVER_OK;
#else
    (void)msg;
    return RIVER_OK;
#endif
}

static void river_ui_apply_message(const river_ui_msg_t *msg)
{
    if (msg == NULL) {
        return;
    }
    g_river_ui.status.handled++;
    switch (msg->type) {
    case RIVER_UI_MSG_STATE:
        g_river_ui.voice_state = msg->state;
        river_ui_copy(g_river_ui.view.state,
                      sizeof(g_river_ui.view.state),
                      river_ui_state_name(msg->state));
        river_ui_copy(g_river_ui.view.emoji,
                      sizeof(g_river_ui.view.emoji),
                      river_ui_state_emoji(msg->state));
        break;
    case RIVER_UI_MSG_EMOJI:
        river_ui_copy(g_river_ui.view.emoji, sizeof(g_river_ui.view.emoji), msg->text);
        break;
    case RIVER_UI_MSG_ASR:
        river_ui_copy(g_river_ui.view.asr_text,
                      sizeof(g_river_ui.view.asr_text),
                      msg->text);
        break;
    case RIVER_UI_MSG_TTS:
        river_ui_copy(g_river_ui.view.tts_text,
                      sizeof(g_river_ui.view.tts_text),
                      msg->text);
        break;
    case RIVER_UI_MSG_LLM: {
        const char *emoji = river_ui_emotion_emoji(msg->detail);

        if (emoji == NULL) {
            emoji = river_ui_state_emoji(g_river_ui.voice_state);
        }
        river_ui_copy(g_river_ui.view.emoji,
                      sizeof(g_river_ui.view.emoji),
                      emoji);
        if (msg->text[0] != '\0') {
            river_ui_copy(g_river_ui.view.tts_text,
                          sizeof(g_river_ui.view.tts_text),
                          msg->text);
        }
        break;
    }
    case RIVER_UI_MSG_TOUCH_SCAN:
#if defined(CONFIG_RIVER_UI_TOUCH_PROBE_EN) && CONFIG_RIVER_UI_TOUCH_PROBE_EN
        if (river_touch_sitronix_probe_scan(g_river_ui.view.touch_summary,
                                            sizeof(g_river_ui.view.touch_summary)) ==
            RIVER_OK) {
            g_river_ui.status.touch_scan_ok++;
        } else {
            g_river_ui.status.touch_scan_fail++;
        }
#else
        river_ui_copy(g_river_ui.view.touch_summary,
                      sizeof(g_river_ui.view.touch_summary),
                      "disabled");
#endif
        break;
    default:
        break;
    }
    river_ui_sync_status();
#if defined(CONFIG_RIVER_UI_LVGL_EN) && CONFIG_RIVER_UI_LVGL_EN
    if (river_lvgl_port_post_view(&g_river_ui.view) != RIVER_OK) {
        river_ui_copy(g_river_ui.status.last_error,
                      sizeof(g_river_ui.status.last_error),
                      "lvgl_post_failed");
    }
#endif
}

static void river_ui_task(void *param)
{
    river_ui_msg_t msg;

    (void)param;
    g_river_ui.task_running = true;
    river_ui_sync_status();
    while (true) {
        if (rtos_queue_receive(g_river_ui.queue, &msg, 100U) == RIVER_UI_RTOS_OK) {
            river_ui_apply_message(&msg);
        }
    }
}

river_status_t river_orvibo_ui_init(void)
{
#if defined(CONFIG_RIVER_UI_EN) && CONFIG_RIVER_UI_EN
    if (g_river_ui.initialized) {
        return RIVER_OK;
    }
    memset(&g_river_ui, 0, sizeof(g_river_ui));
    g_river_ui.enabled = true;
    g_river_ui.voice_state = RIVER_ORVIBO_STATE_STARTING;
    river_ui_copy(g_river_ui.view.state, sizeof(g_river_ui.view.state), "starting");
    river_ui_copy(g_river_ui.view.emoji,
                  sizeof(g_river_ui.view.emoji),
                  river_ui_state_emoji(g_river_ui.voice_state));
    river_ui_copy(g_river_ui.view.asr_text, sizeof(g_river_ui.view.asr_text), "-");
    river_ui_copy(g_river_ui.view.tts_text, sizeof(g_river_ui.view.tts_text), "-");
    river_ui_copy(g_river_ui.view.touch_summary,
                  sizeof(g_river_ui.view.touch_summary),
                  "not_scanned");
    if (rtos_queue_create(&g_river_ui.queue,
                          RIVER_UI_QUEUE_DEPTH,
                          sizeof(river_ui_msg_t)) != RIVER_UI_RTOS_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    g_river_ui.initialized = true;
    river_ui_sync_status();
    return RIVER_OK;
#else
    memset(&g_river_ui, 0, sizeof(g_river_ui));
    return RIVER_OK;
#endif
}

river_status_t river_orvibo_ui_start(void)
{
#if defined(CONFIG_RIVER_UI_EN) && CONFIG_RIVER_UI_EN
    river_status_t status = river_orvibo_ui_init();

    if (status != RIVER_OK) {
        return status;
    }
#if defined(CONFIG_RIVER_UI_LVGL_EN) && CONFIG_RIVER_UI_LVGL_EN
    status = river_lvgl_port_start();
    if (status != RIVER_OK) {
        river_ui_copy(g_river_ui.status.last_error,
                      sizeof(g_river_ui.status.last_error),
                      "lvgl_start_failed");
        RIVER_LOGW("lvgl port start failed: status=%d", (int)status);
    }
#endif
    if (!g_river_ui.task_running) {
        if (rtos_task_create(&g_river_ui.task,
                             "orvibo_ui",
                             river_ui_task,
                             NULL,
                             RIVER_UI_TASK_STACK,
                             RIVER_UI_TASK_PRIORITY) != RIVER_UI_RTOS_OK) {
            return RIVER_ERR_NO_MEMORY;
        }
    }
#if defined(CONFIG_RIVER_UI_LVGL_EN) && CONFIG_RIVER_UI_LVGL_EN
    (void)river_lvgl_port_post_view(&g_river_ui.view);
#endif
    RIVER_LOGI("ui started: lvgl=%s touch_probe=%s",
#if defined(CONFIG_RIVER_UI_LVGL_EN) && CONFIG_RIVER_UI_LVGL_EN
               "on",
#else
               "off",
#endif
#if defined(CONFIG_RIVER_UI_TOUCH_PROBE_EN) && CONFIG_RIVER_UI_TOUCH_PROBE_EN
               "on"
#else
               "off"
#endif
    );
    return RIVER_OK;
#else
    return RIVER_OK;
#endif
}

river_status_t river_orvibo_ui_set_state(river_orvibo_state_t state)
{
    river_ui_msg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.type = RIVER_UI_MSG_STATE;
    msg.state = state;
    return river_ui_post(&msg);
}

river_status_t river_orvibo_ui_set_emoji(const char *emoji)
{
    river_ui_msg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.type = RIVER_UI_MSG_EMOJI;
    river_ui_copy(msg.text, sizeof(msg.text), emoji);
    return river_ui_post(&msg);
}

river_status_t river_orvibo_ui_set_asr_text(const char *text)
{
    river_ui_msg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.type = RIVER_UI_MSG_ASR;
    river_ui_copy(msg.text, sizeof(msg.text), text);
    return river_ui_post(&msg);
}

river_status_t river_orvibo_ui_set_tts_text(const char *text)
{
    river_ui_msg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.type = RIVER_UI_MSG_TTS;
    river_ui_copy(msg.text, sizeof(msg.text), text);
    return river_ui_post(&msg);
}

river_status_t river_orvibo_ui_set_llm_emotion(const char *emotion, const char *text)
{
    river_ui_msg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.type = RIVER_UI_MSG_LLM;
    river_ui_copy(msg.detail, sizeof(msg.detail), emotion);
    river_ui_copy(msg.text, sizeof(msg.text), text);
    return river_ui_post(&msg);
}

river_status_t river_orvibo_ui_get_status(river_orvibo_ui_status_t *status)
{
    if (status == NULL) {
        return RIVER_ERR_ARG;
    }
    river_ui_sync_status();
    *status = g_river_ui.status;
    return RIVER_OK;
}

void river_orvibo_ui_dump_status(void)
{
    river_ui_sync_status();
    RIVER_LOGI("ui: en=%s task=%s lvgl=%s panel=%s touch=%s post=%lu/%lu handled=%lu touch_scan=%lu/%lu state=%s emoji=%s error=%s",
               g_river_ui.status.enabled ? "yes" : "no",
               g_river_ui.status.task_running ? "yes" : "no",
               g_river_ui.status.lvgl_ready ? "ready" : "no",
               g_river_ui.status.panel_ready ? "ready" : "no",
               g_river_ui.status.touch_ready ? "ready" : "no",
               (unsigned long)g_river_ui.status.posted,
               (unsigned long)g_river_ui.status.post_fail,
               (unsigned long)g_river_ui.status.handled,
               (unsigned long)g_river_ui.status.touch_scan_ok,
               (unsigned long)g_river_ui.status.touch_scan_fail,
               g_river_ui.status.state[0] != '\0' ? g_river_ui.status.state : "-",
               g_river_ui.status.emoji[0] != '\0' ? g_river_ui.status.emoji : "-",
               g_river_ui.status.last_error[0] != '\0' ?
                   g_river_ui.status.last_error :
                   "-");
    RIVER_LOGI("ui text: asr=%s tts=%s touch=%s",
               g_river_ui.status.asr_text[0] != '\0' ?
                   g_river_ui.status.asr_text :
                   "-",
               g_river_ui.status.tts_text[0] != '\0' ?
                   g_river_ui.status.tts_text :
                   "-",
               g_river_ui.status.touch_summary[0] != '\0' ?
                   g_river_ui.status.touch_summary :
                   "-");
#if defined(CONFIG_RIVER_UI_LVGL_EN) && CONFIG_RIVER_UI_LVGL_EN
    river_st7102_mipi_dump_status();
#endif
#if defined(CONFIG_RIVER_UI_TOUCH_PROBE_EN) && CONFIG_RIVER_UI_TOUCH_PROBE_EN
    river_touch_sitronix_probe_dump_status();
#endif
}

void river_orvibo_ui_touch_scan(void)
{
    river_ui_msg_t msg;

    memset(&msg, 0, sizeof(msg));
    msg.type = RIVER_UI_MSG_TOUCH_SCAN;
    (void)river_ui_post(&msg);
}
