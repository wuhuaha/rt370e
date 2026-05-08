/* Orvibo-owned firmware metadata exposed to OTA/MCP and diagnostics. */
#ifndef AMEBA_RIVER_ORVIBO_BUILD_INFO_H
#define AMEBA_RIVER_ORVIBO_BUILD_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

const char *river_orvibo_build_info_app_name(void);
const char *river_orvibo_build_info_app_version(void);
const char *river_orvibo_build_info_compile_time(void);
const char *river_orvibo_build_info_board_name(void);
const char *river_orvibo_build_info_board_type(void);
const char *river_orvibo_build_info_chip_model_name(void);
const char *river_orvibo_build_info_user_agent(void);
void river_orvibo_build_info_dump_status(void);

#ifdef __cplusplus
}
#endif

#endif
