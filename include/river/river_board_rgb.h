#ifndef AMEBA_RIVER_BOARD_RGB_H
#define AMEBA_RIVER_BOARD_RGB_H

#include "river/river_types.h"

typedef enum {
    RIVER_BOARD_RGB_STATE_OFF = 0,
    RIVER_BOARD_RGB_STATE_BOOT = 1,
    RIVER_BOARD_RGB_STATE_VAD_SILENCE = 2,
    RIVER_BOARD_RGB_STATE_VAD_SPEECH = 3,
    RIVER_BOARD_RGB_STATE_ERROR = 4
} river_board_rgb_state_t;

river_status_t river_board_rgb_init(void);
void river_board_rgb_set_state(river_board_rgb_state_t state);
const char *river_board_rgb_state_name(void);

#endif
