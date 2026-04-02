/* 项目级诊断入口：统一封装远程调试初始化与命令执行。 */
#ifndef AMEBA_RIVER_DIAG_H
#define AMEBA_RIVER_DIAG_H

#include "basic_types.h"

#include "river/river_types.h"

#ifdef __cplusplus
extern "C" {
#endif

river_status_t river_diag_init(void);
void river_diag_dump_status(void);
u32 river_diag_execute_command_line(const char *line);

#ifdef __cplusplus
}
#endif

#endif
