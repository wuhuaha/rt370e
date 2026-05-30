/* Orvibo local UI facade. */
#ifndef AMEBA_RIVER_ORVIBO_UI_H
#define AMEBA_RIVER_ORVIBO_UI_H

#include "river/river_orvibo_state.h"
#include "river/river_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool enabled;
    bool task_running;
    bool lvgl_ready;
    bool panel_ready;
    bool touch_ready;
    uint32_t posted;
    uint32_t post_fail;
    uint32_t handled;
    uint32_t touch_scan_ok;
    uint32_t touch_scan_fail;
    char state[32];
    char emoji[32];
    char asr_text[192];
    char tts_text[192];
    char touch_summary[96];
    char last_error[96];
} river_orvibo_ui_status_t;

river_status_t river_orvibo_ui_init(void);
river_status_t river_orvibo_ui_start(void);
river_status_t river_orvibo_ui_set_state(river_orvibo_state_t state);
river_status_t river_orvibo_ui_set_emoji(const char *emoji);
river_status_t river_orvibo_ui_set_asr_text(const char *text);
river_status_t river_orvibo_ui_set_tts_text(const char *text);
river_status_t river_orvibo_ui_set_llm_emotion(const char *emotion, const char *text);
river_status_t river_orvibo_ui_get_status(river_orvibo_ui_status_t *status);
void river_orvibo_ui_dump_status(void);
void river_orvibo_ui_touch_scan(void);

#ifdef __cplusplus
}
#endif

#endif
