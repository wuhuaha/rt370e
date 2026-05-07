/* Orvibo voice client application entry. */
#ifndef AMEBA_RIVER_ORVIBO_APP_H
#define AMEBA_RIVER_ORVIBO_APP_H

#include "river/river_types.h"

river_status_t river_orvibo_app_boot(void);
void river_orvibo_app_print_status(void);
void river_orvibo_app_request_connect(void);
void river_orvibo_app_request_access_refresh(void);
void river_orvibo_app_request_listen_start(void);
void river_orvibo_app_request_listen_stop(void);
void river_orvibo_app_request_abort(void);

#endif
