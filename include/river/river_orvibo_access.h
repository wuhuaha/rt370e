/* Orvibo server access and activation contract. */
#ifndef AMEBA_RIVER_ORVIBO_ACCESS_H
#define AMEBA_RIVER_ORVIBO_ACCESS_H

#include <stdbool.h>
#include <stdint.h>

#include "river/river_types.h"

typedef struct {
    bool ready;
    bool identity_ready;
    bool websocket_configured;
    bool used_ota;
    bool activation_required;
    bool activation_done;
    bool activation_challenge_available;
    const char *device_id;
    const char *client_id;
    const char *ota_url;
    const char *activation_code;
    const char *activation_message;
    const char *last_error;
    uint32_t attempts;
    uint32_t http_status;
} river_orvibo_access_status_t;

river_status_t river_orvibo_access_init(void);
river_status_t river_orvibo_access_refresh(void);
bool river_orvibo_access_ready(void);
const char *river_orvibo_access_device_id(void);
const char *river_orvibo_access_client_id(void);
river_status_t river_orvibo_access_get_status(river_orvibo_access_status_t *status);
void river_orvibo_access_dump_status(void);

#endif
