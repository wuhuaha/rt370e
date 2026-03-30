/* River 应用主入口与状态输出接口。 */
#ifndef AMEBA_RIVER_APP_H
#define AMEBA_RIVER_APP_H

#include "river/river_types.h"

river_status_t river_app_boot(void);
void river_app_print_status(void);

#endif
