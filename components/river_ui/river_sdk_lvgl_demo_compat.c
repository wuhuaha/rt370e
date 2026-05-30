/*
 * Project-owned compatibility symbols for SDK LVGL demo objects.
 *
 * The River UI uses river_lvgl_port.c and river_st7102_mipi.c directly. Some
 * SDK LVGL demo objects are still linked by the SDK CMake files and reference
 * display_mode_* helpers from the SDK display-mode layer. Do not pull that
 * layer in for this board-specific port; satisfy the unused symbols here.
 */

#include <stdbool.h>
#include <stdint.h>

#include "ameba.h"
#include "display_mode_setting.h"

#include "river/river_log.h"

#include "river_orvibo_ui_internal.h"

#define LOG_TAG "RiverLvglCompat"

static display_mode_callback_t *g_river_display_mode_callback;
static bool g_river_display_mode_warned;

bool display_mode_init(int32_t color_depth)
{
    RIVER_LOGW("sdk display_mode_init compatibility path, color_depth=%ld",
               (long)color_depth);
    return river_st7102_mipi_panel_ready();
}

void display_mode_set_callback(display_mode_callback_t *callback)
{
    g_river_display_mode_callback = callback;
}

void display_mode_flip_buffer(uint8_t *buffer)
{
    if (!g_river_display_mode_warned) {
        RIVER_LOGW("sdk display_mode_flip_buffer compatibility path");
        g_river_display_mode_warned = true;
    }

    if (buffer) {
        river_st7102_mipi_flip(buffer);
    }

    if (g_river_display_mode_callback && g_river_display_mode_callback->vblank_handler) {
        g_river_display_mode_callback->vblank_handler();
    }
}
