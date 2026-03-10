#ifndef AMEBA_RIVER_ONLINE_CONTROL_H
#define AMEBA_RIVER_ONLINE_CONTROL_H

#include "river/river_types.h"

river_status_t river_online_control_init(void);
river_status_t river_online_control_echo(const char *text);
river_status_t river_online_control_set_device(const char *device_name, const char *action_name);
void river_online_control_dump_status(void);

#endif
