#ifndef RIVER_SDK_DEBUG_OVERRIDES_H
#define RIVER_SDK_DEBUG_OVERRIDES_H

#include "ameba_soc.h"

/*
 * The SDK's SHELL_TASK_STACK_BASIC_SIZE symbol has no prompt, so it cannot be
 * raised from prj.conf in an external project. Keep the override local to this
 * debug branch and apply it only to the AP monitor target.
 */
#ifdef CONFIG_SHELL_TASK_STACK_BASIC_SIZE
#undef CONFIG_SHELL_TASK_STACK_BASIC_SIZE
#endif
#define CONFIG_SHELL_TASK_STACK_BASIC_SIZE 8192

#endif
