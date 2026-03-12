#ifndef AMEBA_RIVER_LOG_H
#define AMEBA_RIVER_LOG_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "platform_autoconf.h"
#include "river/river_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RIVER_LOG_LEVEL_ERROR = 0,
    RIVER_LOG_LEVEL_WARN = 1,
    RIVER_LOG_LEVEL_INFO = 2,
    RIVER_LOG_LEVEL_DEBUG = 3
} river_log_level_t;

typedef void (*river_log_sink_write_t)(void *user_data,
                                       river_log_level_t level,
                                       uint32_t timestamp_ms,
                                       const char *tag,
                                       const char *message);

typedef struct {
    river_log_sink_write_t write;
    void *user_data;
} river_log_sink_t;

uint32_t river_log_timestamp_ms(void);
river_log_level_t river_log_get_level(void);
void river_log_set_level(river_log_level_t level);
bool river_log_level_enabled(river_log_level_t level);
const char *river_log_level_name(river_log_level_t level);
river_status_t river_log_set_secondary_sink(const river_log_sink_t *sink);
void river_log_vwrite(river_log_level_t level,
                      const char *tag,
                      const char *fmt,
                      va_list args);
void river_log_write(river_log_level_t level,
                     const char *tag,
                     const char *fmt,
                     ...);

#ifdef __cplusplus
}
#endif

#if defined(CONFIG_RIVER_LOG_LEVEL_ERROR)
#define RIVER_LOG_LEVEL_DEFAULT RIVER_LOG_LEVEL_ERROR
#elif defined(CONFIG_RIVER_LOG_LEVEL_WARN)
#define RIVER_LOG_LEVEL_DEFAULT RIVER_LOG_LEVEL_WARN
#elif defined(CONFIG_RIVER_LOG_LEVEL_DEBUG)
#define RIVER_LOG_LEVEL_DEFAULT RIVER_LOG_LEVEL_DEBUG
#else
#define RIVER_LOG_LEVEL_DEFAULT RIVER_LOG_LEVEL_INFO
#endif

#ifndef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river"
#endif

#define RIVER_LOGE(fmt, ...) \
    river_log_write(RIVER_LOG_LEVEL_ERROR, RIVER_LOG_TAG, fmt, ##__VA_ARGS__)
#define RIVER_LOGW(fmt, ...) \
    river_log_write(RIVER_LOG_LEVEL_WARN, RIVER_LOG_TAG, fmt, ##__VA_ARGS__)
#define RIVER_LOGI(fmt, ...) \
    river_log_write(RIVER_LOG_LEVEL_INFO, RIVER_LOG_TAG, fmt, ##__VA_ARGS__)
#define RIVER_LOGD(fmt, ...) \
    river_log_write(RIVER_LOG_LEVEL_DEBUG, RIVER_LOG_TAG, fmt, ##__VA_ARGS__)

#endif
