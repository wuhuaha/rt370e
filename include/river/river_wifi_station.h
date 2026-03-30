/* Wi-Fi STA 管理接口。 */
#ifndef AMEBA_RIVER_WIFI_STATION_H
#define AMEBA_RIVER_WIFI_STATION_H

#include <stdbool.h>

#include "river/river_types.h"

river_status_t river_wifi_station_init(void);
bool river_wifi_station_is_connected(void);
const char *river_wifi_station_ssid(void);
const char *river_wifi_station_status_name(void);
void river_wifi_station_dump_status(void);

#endif
