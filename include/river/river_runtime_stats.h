#ifndef AMEBA_RIVER_RUNTIME_STATS_H
#define AMEBA_RIVER_RUNTIME_STATS_H

#include "river/river_types.h"

void river_runtime_stats_init(void);
void river_runtime_stats_snapshot(const char *reason);

#endif
