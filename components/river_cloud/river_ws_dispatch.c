/* WebSocket 分发器：把底层回调路由到具体云端连接实例。 */
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "os_wrapper.h"
#include "websocket/wsclient_api.h"

#include "river/river_log.h"
#include "river_ws_dispatch.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.cloud.ws"

#define RIVER_WS_DISPATCH_SLOT_COUNT 4U

typedef struct {
    wsclient_context *wsclient;
    river_ws_dispatch_message_cb_t message_cb;
    river_ws_dispatch_close_cb_t close_cb;
    void *user_data;
} river_ws_dispatch_slot_t;

typedef struct {
    bool initialized;
    rtos_mutex_t lock;
    river_ws_dispatch_slot_t slots[RIVER_WS_DISPATCH_SLOT_COUNT];
} river_ws_dispatch_context_t;

static river_ws_dispatch_context_t g_river_ws_dispatch;

static int river_ws_dispatch_find_slot_locked(wsclient_context *wsclient)
{
    uint32_t index;

    for (index = 0U; index < RIVER_WS_DISPATCH_SLOT_COUNT; ++index) {
        if (g_river_ws_dispatch.slots[index].wsclient == wsclient) {
            return (int)index;
        }
    }

    return -1;
}

static void river_ws_dispatch_message_router(wsclient_context **wsclient,
                                             int data_len,
                                             enum opcode_type opcode)
{
    river_ws_dispatch_message_cb_t callback = NULL;
    void *user_data = NULL;
    int slot_index;

    if (wsclient == NULL || *wsclient == NULL) {
        return;
    }

    if (rtos_mutex_take(g_river_ws_dispatch.lock, MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
        return;
    }

    slot_index = river_ws_dispatch_find_slot_locked(*wsclient);
    if (slot_index >= 0) {
        callback = g_river_ws_dispatch.slots[slot_index].message_cb;
        user_data = g_river_ws_dispatch.slots[slot_index].user_data;
    }

    rtos_mutex_give(g_river_ws_dispatch.lock);

    if (callback != NULL) {
        callback(wsclient, data_len, opcode, user_data);
    }
}

static void river_ws_dispatch_close_router(wsclient_context *wsclient)
{
    river_ws_dispatch_close_cb_t callback = NULL;
    void *user_data = NULL;
    int slot_index;

    if (wsclient == NULL) {
        return;
    }

    if (rtos_mutex_take(g_river_ws_dispatch.lock, MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
        return;
    }

    slot_index = river_ws_dispatch_find_slot_locked(wsclient);
    if (slot_index >= 0) {
        callback = g_river_ws_dispatch.slots[slot_index].close_cb;
        user_data = g_river_ws_dispatch.slots[slot_index].user_data;
    }

    rtos_mutex_give(g_river_ws_dispatch.lock);

    if (callback != NULL) {
        callback(wsclient, user_data);
    }
}

river_status_t river_ws_dispatch_init(void)
{
    if (g_river_ws_dispatch.initialized) {
        return RIVER_OK;
    }

    memset(&g_river_ws_dispatch, 0, sizeof(g_river_ws_dispatch));
    if (rtos_mutex_create(&g_river_ws_dispatch.lock) != RTK_SUCCESS) {
        return RIVER_ERR_NO_MEMORY;
    }

    ws_dispatch(river_ws_dispatch_message_router);
    ws_dispatch_close(river_ws_dispatch_close_router);
    g_river_ws_dispatch.initialized = true;
    return RIVER_OK;
}

river_status_t river_ws_dispatch_register(wsclient_context *wsclient,
                                          river_ws_dispatch_message_cb_t message_cb,
                                          river_ws_dispatch_close_cb_t close_cb,
                                          void *user_data)
{
    uint32_t index;
    int empty_index;

    if (wsclient == NULL || message_cb == NULL) {
        return RIVER_ERR_ARG;
    }
    if (river_ws_dispatch_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    if (rtos_mutex_take(g_river_ws_dispatch.lock, MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
        return RIVER_ERR_BUSY;
    }

    empty_index = -1;
    for (index = 0U; index < RIVER_WS_DISPATCH_SLOT_COUNT; ++index) {
        if (g_river_ws_dispatch.slots[index].wsclient == wsclient) {
            g_river_ws_dispatch.slots[index].message_cb = message_cb;
            g_river_ws_dispatch.slots[index].close_cb = close_cb;
            g_river_ws_dispatch.slots[index].user_data = user_data;
            rtos_mutex_give(g_river_ws_dispatch.lock);
            return RIVER_OK;
        }
        if (empty_index < 0 && g_river_ws_dispatch.slots[index].wsclient == NULL) {
            empty_index = (int)index;
        }
    }

    if (empty_index < 0) {
        rtos_mutex_give(g_river_ws_dispatch.lock);
        RIVER_LOGE("dispatcher slots exhausted");
        return RIVER_ERR_BUSY;
    }

    g_river_ws_dispatch.slots[empty_index].wsclient = wsclient;
    g_river_ws_dispatch.slots[empty_index].message_cb = message_cb;
    g_river_ws_dispatch.slots[empty_index].close_cb = close_cb;
    g_river_ws_dispatch.slots[empty_index].user_data = user_data;
    rtos_mutex_give(g_river_ws_dispatch.lock);
    return RIVER_OK;
}

void river_ws_dispatch_unregister(wsclient_context *wsclient)
{
    int slot_index;

    if (!g_river_ws_dispatch.initialized || wsclient == NULL) {
        return;
    }
    if (rtos_mutex_take(g_river_ws_dispatch.lock, MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
        return;
    }

    slot_index = river_ws_dispatch_find_slot_locked(wsclient);
    if (slot_index >= 0) {
        memset(&g_river_ws_dispatch.slots[slot_index], 0, sizeof(g_river_ws_dispatch.slots[slot_index]));
    }

    rtos_mutex_give(g_river_ws_dispatch.lock);
}
