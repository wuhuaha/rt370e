/*
 * HP/KM4 compatibility symbols for SDK LVGL demo objects.
 *
 * River owns the actual ST7102/LVGL display port on AP. CONFIG_LVGL_ENABLE is
 * global in the SDK build, so HP also links unused SDK demo objects that expect
 * display_mode_* symbols. Keep those symbols as HP-side no-ops instead of
 * linking the SDK display-mode layer or board display code into HP.
 */

#include <stdbool.h>
#include <stdint.h>

#include "ameba_soc.h"
#include "display_mode_setting.h"

static display_mode_callback_t *g_river_hp_display_mode_callback;
static bool g_river_hp_display_mode_init_logged;
static bool g_river_hp_display_mode_flip_logged;

bool display_mode_init(int32_t color_depth)
{
    if (!g_river_hp_display_mode_init_logged) {
        DiagPrintf("[river.ui.hp] sdk display_mode_init no-op color_depth=%ld\r\n",
                   (long)color_depth);
        g_river_hp_display_mode_init_logged = true;
    }
    return true;
}

void display_mode_set_callback(display_mode_callback_t *callback)
{
    g_river_hp_display_mode_callback = callback;
}

void display_mode_flip_buffer(uint8_t *buffer)
{
    (void)buffer;

    if (!g_river_hp_display_mode_flip_logged) {
        DiagPrintf("[river.ui.hp] sdk display_mode_flip_buffer no-op\r\n");
        g_river_hp_display_mode_flip_logged = true;
    }

    if (g_river_hp_display_mode_callback && g_river_hp_display_mode_callback->vblank_handler) {
        g_river_hp_display_mode_callback->vblank_handler();
    }
}
