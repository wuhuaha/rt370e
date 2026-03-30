/* WebSocket 分发器私有接口。 */
#ifndef AMEBA_RIVER_WS_DISPATCH_H
#define AMEBA_RIVER_WS_DISPATCH_H

#include "websocket/libwsclient.h"

#include "river/river_types.h"

typedef void (*river_ws_dispatch_message_cb_t)(wsclient_context **wsclient,
                                               int data_len,
                                               enum opcode_type opcode,
                                               void *user_data);

typedef void (*river_ws_dispatch_close_cb_t)(wsclient_context *wsclient,
                                             void *user_data);

river_status_t river_ws_dispatch_init(void);
river_status_t river_ws_dispatch_register(wsclient_context *wsclient,
                                          river_ws_dispatch_message_cb_t message_cb,
                                          river_ws_dispatch_close_cb_t close_cb,
                                          void *user_data);
void river_ws_dispatch_unregister(wsclient_context *wsclient);

#endif
