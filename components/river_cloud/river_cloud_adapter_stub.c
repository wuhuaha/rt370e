#include <stdio.h>

#include "river/river_cloud.h"

river_status_t river_cloud_adapter_init(void)
{
    printf("[river][cloud] stub adapter init\n");
    printf("[river][cloud] real online provider will be added in a later step\n");
    return RIVER_OK;
}

river_status_t river_cloud_adapter_submit_text(const char *text)
{
    if (text == 0) {
        return RIVER_ERR_ARG;
    }

    printf("[river][cloud] echo=%s\n", text);
    return RIVER_OK;
}
