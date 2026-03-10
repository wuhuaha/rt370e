#include <stdio.h>

#include "river/river_app.h"

void app_example(void)
{
    if (river_app_boot() != RIVER_OK) {
        printf("[river] boot failed\n");
    }
}
