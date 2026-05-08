/* Orvibo build/runtime metadata for OTA self-description and MCP identity. */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "build_info.h"

#include "river/river_log.h"
#include "river/river_orvibo_build_info.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.orvibo.build"

#define RIVER_ORVIBO_BUILD_INFO_APP_NAME_STR         "orvibo-rtl8730e"
#define RIVER_ORVIBO_BUILD_INFO_BOARD_NAME_STR       "orvibo-rtl8730e"
#define RIVER_ORVIBO_BUILD_INFO_BOARD_TYPE_STR       "orvibo-rtl8730e"
#define RIVER_ORVIBO_BUILD_INFO_CHIP_MODEL_NAME_STR  "rtl8730e"
#define RIVER_ORVIBO_BUILD_INFO_VERSION_MAX          32U
#define RIVER_ORVIBO_BUILD_INFO_COMPILE_TIME_MAX     32U
#define RIVER_ORVIBO_BUILD_INFO_USER_AGENT_MAX       64U

typedef struct {
    bool initialized;
    char app_version[RIVER_ORVIBO_BUILD_INFO_VERSION_MAX];
    char compile_time[RIVER_ORVIBO_BUILD_INFO_COMPILE_TIME_MAX];
    char user_agent[RIVER_ORVIBO_BUILD_INFO_USER_AGENT_MAX];
} river_orvibo_build_info_context_t;

static river_orvibo_build_info_context_t g_river_orvibo_build_info;

static void river_orvibo_build_info_copy(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0U) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_size - 1U);
    dst[dst_size - 1U] = '\0';
}

static void river_orvibo_build_info_init(void)
{
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;

    if (g_river_orvibo_build_info.initialized) {
        return;
    }

    memset(&g_river_orvibo_build_info, 0, sizeof(g_river_orvibo_build_info));
    if (sscanf(RTL_FW_COMPILE_TIME,
               "%d-%d-%d %d:%d:%d",
               &year,
               &month,
               &day,
               &hour,
               &minute,
               &second) == 6) {
        snprintf(g_river_orvibo_build_info.app_version,
                 sizeof(g_river_orvibo_build_info.app_version),
                 "%04d.%02d.%02d.%02d%02d%02d",
                 year,
                 month,
                 day,
                 hour,
                 minute,
                 second);
        snprintf(g_river_orvibo_build_info.compile_time,
                 sizeof(g_river_orvibo_build_info.compile_time),
                 "%04d-%02d-%02dT%02d:%02d:%02dZ",
                 year,
                 month,
                 day,
                 hour,
                 minute,
                 second);
    } else if (sscanf(RTL_FW_COMPILE_DATE, "%d-%d-%d", &year, &month, &day) == 3) {
        snprintf(g_river_orvibo_build_info.app_version,
                 sizeof(g_river_orvibo_build_info.app_version),
                 "%04d.%02d.%02d.000000",
                 year,
                 month,
                 day);
        snprintf(g_river_orvibo_build_info.compile_time,
                 sizeof(g_river_orvibo_build_info.compile_time),
                 "%04d-%02d-%02dT00:00:00Z",
                 year,
                 month,
                 day);
    } else {
        river_orvibo_build_info_copy(g_river_orvibo_build_info.app_version,
                                     sizeof(g_river_orvibo_build_info.app_version),
                                     "1.0.0");
        river_orvibo_build_info_copy(g_river_orvibo_build_info.compile_time,
                                     sizeof(g_river_orvibo_build_info.compile_time),
                                     "1970-01-01T00:00:00Z");
    }
    snprintf(g_river_orvibo_build_info.user_agent,
             sizeof(g_river_orvibo_build_info.user_agent),
             "%s/%s",
             RIVER_ORVIBO_BUILD_INFO_APP_NAME_STR,
             g_river_orvibo_build_info.app_version);
    g_river_orvibo_build_info.initialized = true;
}

const char *river_orvibo_build_info_app_name(void)
{
    return RIVER_ORVIBO_BUILD_INFO_APP_NAME_STR;
}

const char *river_orvibo_build_info_app_version(void)
{
    river_orvibo_build_info_init();
    return g_river_orvibo_build_info.app_version;
}

const char *river_orvibo_build_info_compile_time(void)
{
    river_orvibo_build_info_init();
    return g_river_orvibo_build_info.compile_time;
}

const char *river_orvibo_build_info_board_name(void)
{
    return RIVER_ORVIBO_BUILD_INFO_BOARD_NAME_STR;
}

const char *river_orvibo_build_info_board_type(void)
{
    return RIVER_ORVIBO_BUILD_INFO_BOARD_TYPE_STR;
}

const char *river_orvibo_build_info_chip_model_name(void)
{
    return RIVER_ORVIBO_BUILD_INFO_CHIP_MODEL_NAME_STR;
}

const char *river_orvibo_build_info_user_agent(void)
{
    river_orvibo_build_info_init();
    return g_river_orvibo_build_info.user_agent;
}

void river_orvibo_build_info_dump_status(void)
{
    river_orvibo_build_info_init();
    RIVER_LOGI("orvibo build: app=%s version=%s board=%s type=%s chip=%s compile_time=%s user_agent=%s",
               river_orvibo_build_info_app_name(),
               g_river_orvibo_build_info.app_version,
               river_orvibo_build_info_board_name(),
               river_orvibo_build_info_board_type(),
               river_orvibo_build_info_chip_model_name(),
               g_river_orvibo_build_info.compile_time,
               g_river_orvibo_build_info.user_agent);
}
