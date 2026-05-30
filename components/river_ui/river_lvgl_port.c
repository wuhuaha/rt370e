#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ameba_soc.h"
#include "lvgl.h"
#include "os_wrapper.h"

#include "river/river_log.h"
#include "river/river_types.h"

#include "assets/noto_cat_lvgl/river_noto_cat_anim.h"
#include "river_orvibo_ui_internal.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.ui.lvgl"

#define RIVER_LVGL_WIDTH       480U
#define RIVER_LVGL_HEIGHT      480U
#define RIVER_LVGL_TASK_STACK  (1024U * 24U)
#define RIVER_LVGL_TASK_PRIO   3U
#define RIVER_LVGL_QUEUE_DEPTH 4U
#define RIVER_LVGL_RTOS_OK     0

LV_FONT_DECLARE(river_lv_font_zh_16)

typedef struct {
    bool started;
    bool ready;
    bool screen_ready;
    rtos_task_t task;
    rtos_queue_t queue;
    lv_display_t *display;
    lv_obj_t *state_label;
    lv_obj_t *emoji_anim;
    lv_obj_t *emoji_label;
    lv_obj_t *asr_label;
    lv_obj_t *tts_label;
    lv_obj_t *touch_label;
    const river_noto_cat_anim_t *current_anim;
    river_orvibo_ui_view_state_t pending;
    uint8_t *buf1;
    uint8_t *buf2;
    uint32_t view_updates;
    uint32_t dropped_updates;
} river_lvgl_context_t;

static river_lvgl_context_t g_lvgl;

static void river_lvgl_copy(char *dst, size_t dst_size, const char *src)
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
}

static void river_lvgl_flush(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    (void)area;
    if (lv_display_flush_is_last(display)) {
        river_st7102_mipi_flip(px_map);
    }
    lv_display_flush_ready(display);
}

static uint32_t river_lvgl_tick_ms(void)
{
    return (uint32_t)rtos_time_get_current_system_time_ms();
}

static void river_lvgl_label_set(lv_obj_t *label, const char *prefix, const char *text)
{
    char line[240];

    if (label == NULL) {
        return;
    }
    snprintf(line, sizeof(line), "%s%s", prefix != NULL ? prefix : "", text != NULL ? text : "-");
    lv_label_set_text(label, line);
}

static void river_lvgl_apply_emoji(const char *emoji)
{
    const river_noto_cat_anim_t *anim = river_noto_cat_anim_resolve(emoji);

    if (anim == NULL) {
        anim = river_noto_cat_anim_default();
    }
    if (g_lvgl.emoji_anim != NULL && anim != g_lvgl.current_anim) {
        lv_animimg_set_src(g_lvgl.emoji_anim, (const void **)anim->frames, anim->frame_count);
        lv_animimg_set_duration(g_lvgl.emoji_anim, anim->duration_ms);
        lv_animimg_set_repeat_count(g_lvgl.emoji_anim, LV_ANIM_REPEAT_INFINITE);
        lv_animimg_start(g_lvgl.emoji_anim);
        g_lvgl.current_anim = anim;
    }
    river_lvgl_label_set(g_lvgl.emoji_label, "CAT ", anim != NULL ? anim->caption : "-");
}

static void river_lvgl_apply_view(const river_orvibo_ui_view_state_t *view)
{
    if (view == NULL || !g_lvgl.screen_ready) {
        return;
    }
    river_lvgl_label_set(g_lvgl.state_label, "STATE ", view->state);
    river_lvgl_apply_emoji(view->emoji);
    river_lvgl_label_set(g_lvgl.asr_label, "ASR ", view->asr_text);
    river_lvgl_label_set(g_lvgl.tts_label, "TTS ", view->tts_text);
    river_lvgl_label_set(g_lvgl.touch_label, "TOUCH ", view->touch_summary);
    g_lvgl.view_updates++;
}

static lv_obj_t *river_lvgl_add_label(lv_obj_t *parent,
                                      const lv_font_t *font,
                                      lv_color_t color,
                                      int32_t width)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

static void river_lvgl_create_screen(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_t *panel;
    const lv_font_t *text_font = &river_lv_font_zh_16;

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101418), 0);
    lv_obj_set_style_pad_all(screen, 20, 0);

    panel = lv_obj_create(screen);
    lv_obj_set_size(panel, 440, 440);
    lv_obj_center(panel);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x182028), 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x405060), 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, 14, 0);
    lv_obj_set_style_pad_row(panel, 8, 0);

    g_lvgl.state_label = river_lvgl_add_label(panel,
                                              &lv_font_montserrat_20,
                                              lv_color_hex(0x99AABB),
                                              400);

    g_lvgl.emoji_anim = lv_animimg_create(panel);
    lv_obj_set_size(g_lvgl.emoji_anim,
                    RIVER_NOTO_CAT_ANIM_WIDTH,
                    RIVER_NOTO_CAT_ANIM_HEIGHT);
    lv_obj_set_style_bg_opa(g_lvgl.emoji_anim, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_lvgl.emoji_anim, 0, 0);
    lv_obj_set_style_pad_all(g_lvgl.emoji_anim, 0, 0);

    g_lvgl.emoji_label = river_lvgl_add_label(panel,
                                              &lv_font_montserrat_14,
                                              lv_color_hex(0xF0C75E),
                                              400);
    lv_obj_set_style_text_align(g_lvgl.emoji_label, LV_TEXT_ALIGN_CENTER, 0);
    g_lvgl.asr_label = river_lvgl_add_label(panel,
                                            text_font,
                                            lv_color_hex(0xE6EDF3),
                                            400);
    g_lvgl.tts_label = river_lvgl_add_label(panel,
                                            text_font,
                                            lv_color_hex(0xB6E3C6),
                                            400);
    g_lvgl.touch_label = river_lvgl_add_label(panel,
                                              &lv_font_montserrat_14,
                                              lv_color_hex(0x8EA0B2),
                                              400);
    g_lvgl.screen_ready = true;
    river_lvgl_apply_view(&g_lvgl.pending);
}

static river_status_t river_lvgl_display_init(void)
{
    size_t buffer_size = RIVER_LVGL_WIDTH * RIVER_LVGL_HEIGHT * (LV_COLOR_DEPTH / 8U);

    if (river_st7102_mipi_init() != RIVER_OK) {
        return RIVER_ERR_IO;
    }
    g_lvgl.buf1 = (uint8_t *)malloc(buffer_size);
    g_lvgl.buf2 = (uint8_t *)malloc(buffer_size);
    if (g_lvgl.buf1 == NULL || g_lvgl.buf2 == NULL) {
        return RIVER_ERR_NO_MEMORY;
    }
    lv_init();
    lv_tick_set_cb(river_lvgl_tick_ms);
    g_lvgl.display = lv_display_create(RIVER_LVGL_WIDTH, RIVER_LVGL_HEIGHT);
    if (g_lvgl.display == NULL) {
        return RIVER_ERR_NO_MEMORY;
    }
    lv_display_set_buffers(g_lvgl.display,
                           g_lvgl.buf1,
                           g_lvgl.buf2,
                           buffer_size,
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(g_lvgl.display, river_lvgl_flush);
    river_lvgl_create_screen();
    g_lvgl.ready = true;
    return RIVER_OK;
}

static void river_lvgl_task(void *param)
{
    river_orvibo_ui_view_state_t view;
    river_status_t status;

    (void)param;
    status = river_lvgl_display_init();
    if (status != RIVER_OK) {
        RIVER_LOGW("lvgl display init failed: status=%d", (int)status);
    } else {
        RIVER_LOGI("lvgl ready: %lux%lu depth=%d",
                   (unsigned long)RIVER_LVGL_WIDTH,
                   (unsigned long)RIVER_LVGL_HEIGHT,
                   LV_COLOR_DEPTH);
    }

    while (true) {
        while (g_lvgl.queue != NULL &&
               rtos_queue_receive(g_lvgl.queue, &view, 0U) == RIVER_LVGL_RTOS_OK) {
            g_lvgl.pending = view;
            if (g_lvgl.ready) {
                river_lvgl_apply_view(&g_lvgl.pending);
            }
        }
        if (g_lvgl.ready) {
            uint32_t wait_ms = lv_timer_handler();
            if (wait_ms == LV_NO_TIMER_READY || wait_ms > LV_DEF_REFR_PERIOD) {
                wait_ms = LV_DEF_REFR_PERIOD;
            }
            rtos_time_delay_ms(wait_ms);
        } else {
            rtos_time_delay_ms(100);
        }
    }
}

river_status_t river_lvgl_port_start(void)
{
    if (g_lvgl.started) {
        return RIVER_OK;
    }
    memset(&g_lvgl, 0, sizeof(g_lvgl));
    river_lvgl_copy(g_lvgl.pending.state, sizeof(g_lvgl.pending.state), "starting");
    river_lvgl_copy(g_lvgl.pending.emoji, sizeof(g_lvgl.pending.emoji), ":boot:");
    river_lvgl_copy(g_lvgl.pending.asr_text, sizeof(g_lvgl.pending.asr_text), "-");
    river_lvgl_copy(g_lvgl.pending.tts_text, sizeof(g_lvgl.pending.tts_text), "-");
    river_lvgl_copy(g_lvgl.pending.touch_summary,
                    sizeof(g_lvgl.pending.touch_summary),
                    "not_scanned");
    if (rtos_queue_create(&g_lvgl.queue,
                          RIVER_LVGL_QUEUE_DEPTH,
                          sizeof(river_orvibo_ui_view_state_t)) != RIVER_LVGL_RTOS_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    if (rtos_task_create(&g_lvgl.task,
                         "river_lvgl",
                         river_lvgl_task,
                         NULL,
                         RIVER_LVGL_TASK_STACK,
                         RIVER_LVGL_TASK_PRIO) != RIVER_LVGL_RTOS_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    g_lvgl.started = true;
    return RIVER_OK;
}

river_status_t river_lvgl_port_post_view(const river_orvibo_ui_view_state_t *view)
{
    river_orvibo_ui_view_state_t dropped;

    if (view == NULL) {
        return RIVER_ERR_ARG;
    }
    if (g_lvgl.queue == NULL) {
        g_lvgl.pending = *view;
        return RIVER_OK;
    }
    if (rtos_queue_send(g_lvgl.queue, (void *)view, 0U) == RIVER_LVGL_RTOS_OK) {
        return RIVER_OK;
    }
    if (rtos_queue_receive(g_lvgl.queue, &dropped, 0U) == RIVER_LVGL_RTOS_OK &&
        rtos_queue_send(g_lvgl.queue, (void *)view, 0U) == RIVER_LVGL_RTOS_OK) {
        g_lvgl.dropped_updates++;
        return RIVER_OK;
    }
    g_lvgl.dropped_updates++;
    return RIVER_ERR_BUSY;
}

bool river_lvgl_port_ready(void)
{
    return g_lvgl.ready;
}
