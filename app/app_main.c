/* SDK 示例入口：把外部工程启动流程接到 Orvibo 应用主入口。 */
#include <stdio.h>

#include "river/river_orvibo_app.h"

void app_example(void)
{
    if (river_orvibo_app_boot() != RIVER_OK) {
        printf("[river] boot failed\n");
    }
}
