#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "basic_types.h"
#include "os_wrapper.h"

#include "river/river_interaction_state.h"
#include "river/river_log.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.interaction"

typedef struct {
    bool initialized;
    rtos_mutex_t lock;
    river_interaction_state_t state;
    uint32_t transition_count;
    char reason[48];
} river_interaction_state_context_t;

static river_interaction_state_context_t g_river_interaction_state;

river_status_t river_interaction_state_init(void)
{
    if (g_river_interaction_state.initialized) {
        return RIVER_OK;
    }

    memset(&g_river_interaction_state, 0, sizeof(g_river_interaction_state));
    if (rtos_mutex_create(&g_river_interaction_state.lock) != RTK_SUCCESS) {
        return RIVER_ERR_NO_MEMORY;
    }

    g_river_interaction_state.initialized = true;
    g_river_interaction_state.state = RIVER_INTERACTION_BOOTING;
    strncpy(g_river_interaction_state.reason,
            "boot",
            sizeof(g_river_interaction_state.reason) - 1U);
    return RIVER_OK;
}

river_status_t river_interaction_state_set(river_interaction_state_t state, const char *reason)
{
    if (river_interaction_state_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }

    if (rtos_mutex_take(g_river_interaction_state.lock, MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
        return RIVER_ERR_BUSY;
    }

    if (g_river_interaction_state.state != state) {
        RIVER_LOGI("interaction_state: %s -> %s reason=%s",
                   river_interaction_state_name(g_river_interaction_state.state),
                   river_interaction_state_name(state),
                   reason != NULL ? reason : "-");
        g_river_interaction_state.state = state;
        g_river_interaction_state.transition_count++;
    }
    if (reason != NULL && reason[0] != '\0') {
        strncpy(g_river_interaction_state.reason,
                reason,
                sizeof(g_river_interaction_state.reason) - 1U);
        g_river_interaction_state.reason[sizeof(g_river_interaction_state.reason) - 1U] = '\0';
    }

    rtos_mutex_give(g_river_interaction_state.lock);
    return RIVER_OK;
}

river_interaction_state_t river_interaction_state_get(void)
{
    return g_river_interaction_state.state;
}

const char *river_interaction_state_name(river_interaction_state_t state)
{
    switch (state) {
    case RIVER_INTERACTION_BOOTING:
        return "booting";
    case RIVER_INTERACTION_IDLE:
        return "idle";
    case RIVER_INTERACTION_WAKE_MONITORING:
        return "wake_monitoring";
    case RIVER_INTERACTION_WAKE_CONFIRMED:
        return "wake_confirmed";
    case RIVER_INTERACTION_LISTENING:
        return "listening";
    case RIVER_INTERACTION_ASR_STREAMING:
        return "asr_streaming";
    case RIVER_INTERACTION_THINKING:
        return "thinking";
    case RIVER_INTERACTION_SPEAKING:
        return "speaking";
    case RIVER_INTERACTION_BARGE_IN_LISTENING:
        return "barge_in_listening";
    case RIVER_INTERACTION_FOLLOW_UP:
        return "follow_up";
    case RIVER_INTERACTION_ERROR_RECOVERING:
        return "error_recovering";
    default:
        return "unknown";
    }
}

void river_interaction_state_dump_status(void)
{
    river_interaction_state_t state;
    uint32_t transition_count;
    char reason[sizeof(g_river_interaction_state.reason)];

    memset(reason, 0, sizeof(reason));
    state = RIVER_INTERACTION_BOOTING;
    transition_count = 0U;

    if (g_river_interaction_state.initialized &&
        rtos_mutex_take(g_river_interaction_state.lock, MUTEX_WAIT_TIMEOUT) == RTK_SUCCESS) {
        state = g_river_interaction_state.state;
        transition_count = g_river_interaction_state.transition_count;
        memcpy(reason, g_river_interaction_state.reason, sizeof(reason));
        rtos_mutex_give(g_river_interaction_state.lock);
    }

    RIVER_LOGI("interaction_state=%s transitions=%lu reason=%s",
               river_interaction_state_name(state),
               (unsigned long)transition_count,
               reason[0] != '\0' ? reason : "-");
}
