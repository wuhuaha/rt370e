/* 轻量重启 breadcrumb：用备份寄存器记录最近交互阶段，辅助定位无 panic 的异常重启。 */
#ifndef AMEBA_RIVER_RESET_TRACE_H
#define AMEBA_RIVER_RESET_TRACE_H

#include "river/river_interaction_state.h"

void river_reset_trace_boot_init(void);
void river_reset_trace_mark(river_interaction_state_t state, const char *reason);
void river_reset_trace_dump_status(void);

#endif
