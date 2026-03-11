#include <stdbool.h>
#include <stdio.h>

#include "river/river_board_rgb.h"

typedef struct {
    bool init_attempted;
    bool warning_printed;
    river_board_rgb_state_t current_state;
} river_board_rgb_context_t;

static river_board_rgb_context_t g_river_board_rgb;

river_status_t river_board_rgb_init(void)
{
#ifndef CONFIG_RIVER_BOARD_RGB_VAD_INDICATOR_EN
    return RIVER_ERR_UNSUPPORTED;
#else
    if (!g_river_board_rgb.warning_printed) {
        printf("[river][board] rgb indicator deferred: EV8730EA2 USER LED is passive RGB, not WS2812; confirm R25/R27/R31 population and LEDR/LEDG/LEDB GPIO mapping before runtime control\n");
        g_river_board_rgb.warning_printed = true;
    }
    g_river_board_rgb.init_attempted = true;
    return RIVER_ERR_UNSUPPORTED;
#endif
}

void river_board_rgb_set_state(river_board_rgb_state_t state)
{
#ifdef CONFIG_RIVER_BOARD_RGB_VAD_INDICATOR_EN
    g_river_board_rgb.current_state = state;
    (void)river_board_rgb_init();
#else
    (void)state;
#endif
}

const char *river_board_rgb_state_name(void)
{
    switch (g_river_board_rgb.current_state) {
    case RIVER_BOARD_RGB_STATE_BOOT:
        return "boot";
    case RIVER_BOARD_RGB_STATE_VAD_SILENCE:
        return "vad_silence";
    case RIVER_BOARD_RGB_STATE_VAD_SPEECH:
        return "vad_speech";
    case RIVER_BOARD_RGB_STATE_ERROR:
        return "error";
    case RIVER_BOARD_RGB_STATE_OFF:
    default:
        return "off";
    }
}
