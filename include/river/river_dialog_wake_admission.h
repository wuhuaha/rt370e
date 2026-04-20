/* Wake admission bridge: queue wakeword handoff and open cloud conversation. */
#ifndef AMEBA_RIVER_DIALOG_WAKE_ADMISSION_H
#define AMEBA_RIVER_DIALOG_WAKE_ADMISSION_H

#include "river/river_types.h"

river_status_t river_dialog_wake_admission_init(void);
river_status_t river_dialog_wake_admission_submit(const char *text, int confidence);

#endif
