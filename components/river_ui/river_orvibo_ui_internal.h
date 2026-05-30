/* Internal hooks for the Orvibo LVGL UI port. */
#ifndef AMEBA_RIVER_ORVIBO_UI_INTERNAL_H
#define AMEBA_RIVER_ORVIBO_UI_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "river/river_types.h"

typedef struct {
    char state[32];
    char emoji[32];
    char asr_text[192];
    char tts_text[192];
    char touch_summary[96];
} river_orvibo_ui_view_state_t;

river_status_t river_lvgl_port_start(void);
river_status_t river_lvgl_port_post_view(const river_orvibo_ui_view_state_t *view);
bool river_lvgl_port_ready(void);
bool river_st7102_mipi_panel_ready(void);
void river_st7102_mipi_flip(void *buffer);
river_status_t river_st7102_mipi_init(void);
void river_st7102_mipi_dump_status(void);
river_status_t river_touch_sitronix_probe_scan(char *summary, size_t summary_size);
bool river_touch_sitronix_probe_ready(void);
void river_touch_sitronix_probe_dump_status(void);

#endif
