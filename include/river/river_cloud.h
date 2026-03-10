#ifndef AMEBA_RIVER_CLOUD_H
#define AMEBA_RIVER_CLOUD_H

#include "river/river_types.h"

river_status_t river_cloud_adapter_init(void);
river_status_t river_cloud_adapter_submit_text(const char *text);

#endif
