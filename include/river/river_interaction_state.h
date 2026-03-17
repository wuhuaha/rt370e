#ifndef AMEBA_RIVER_INTERACTION_STATE_H
#define AMEBA_RIVER_INTERACTION_STATE_H

#include <stdint.h>

#include "river/river_types.h"

typedef enum {
    RIVER_INTERACTION_BOOTING = 0,
    RIVER_INTERACTION_IDLE,
    RIVER_INTERACTION_WAKE_MONITORING,
    RIVER_INTERACTION_WAKE_CONFIRMED,
    RIVER_INTERACTION_LISTENING,
    RIVER_INTERACTION_ASR_STREAMING,
    RIVER_INTERACTION_THINKING,
    RIVER_INTERACTION_SPEAKING,
    RIVER_INTERACTION_BARGE_IN_LISTENING,
    RIVER_INTERACTION_FOLLOW_UP,
    RIVER_INTERACTION_ERROR_RECOVERING
} river_interaction_state_t;

river_status_t river_interaction_state_init(void);
river_status_t river_interaction_state_set(river_interaction_state_t state, const char *reason);
river_interaction_state_t river_interaction_state_get(void);
const char *river_interaction_state_name(river_interaction_state_t state);
void river_interaction_state_dump_status(void);

#endif
