/* 项目统一日志实现：提供时间戳、等级过滤和可选的二级输出。 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "basic_types.h"
#include "os_wrapper_mutex.h"
#include "os_wrapper_time.h"

#include "river/river_log.h"

#define RIVER_LOG_LINE_MAX 1024U

typedef struct {
    river_log_level_t level;
    rtos_mutex_t mutex;
    bool mutex_ready;
    river_log_sink_t secondary_sink;
} river_log_context_t;

static river_log_context_t g_river_log = {
    .level = RIVER_LOG_LEVEL_DEFAULT
};

static char river_log_level_letter(river_log_level_t level)
{
    switch (level) {
    case RIVER_LOG_LEVEL_ERROR:
        return 'E';
    case RIVER_LOG_LEVEL_WARN:
        return 'W';
    case RIVER_LOG_LEVEL_INFO:
        return 'I';
    case RIVER_LOG_LEVEL_DEBUG:
        return 'D';
    default:
        return '?';
    }
}

static void river_log_trim_newline(char *message)
{
    size_t length;

    if (message == NULL) {
        return;
    }

    length = strlen(message);
    while (length > 0U &&
           (message[length - 1U] == '\n' || message[length - 1U] == '\r')) {
        message[length - 1U] = '\0';
        length--;
    }
}

static void river_log_ensure_mutex(void)
{
    if (!g_river_log.mutex_ready && g_river_log.mutex == NULL) {
        if (rtos_mutex_create(&g_river_log.mutex) == RTK_SUCCESS) {
            g_river_log.mutex_ready = true;
        }
    }
}

uint32_t river_log_timestamp_ms(void)
{
    return rtos_time_get_current_system_time_ms();
}

river_log_level_t river_log_get_level(void)
{
    return g_river_log.level;
}

void river_log_set_level(river_log_level_t level)
{
    int level_value;

    level_value = (int)level;
    if (level_value < (int)RIVER_LOG_LEVEL_ERROR) {
        g_river_log.level = RIVER_LOG_LEVEL_ERROR;
    } else if (level_value > (int)RIVER_LOG_LEVEL_DEBUG) {
        g_river_log.level = RIVER_LOG_LEVEL_DEBUG;
    } else {
        g_river_log.level = (river_log_level_t)level_value;
    }
}

bool river_log_level_enabled(river_log_level_t level)
{
    return level <= g_river_log.level;
}

const char *river_log_level_name(river_log_level_t level)
{
    switch (level) {
    case RIVER_LOG_LEVEL_ERROR:
        return "ERROR";
    case RIVER_LOG_LEVEL_WARN:
        return "WARN";
    case RIVER_LOG_LEVEL_INFO:
        return "INFO";
    case RIVER_LOG_LEVEL_DEBUG:
        return "DEBUG";
    default:
        return "UNKNOWN";
    }
}

river_status_t river_log_set_secondary_sink(const river_log_sink_t *sink)
{
    if (sink == NULL) {
        memset(&g_river_log.secondary_sink, 0, sizeof(g_river_log.secondary_sink));
        return RIVER_OK;
    }
    if (sink->write == NULL) {
        return RIVER_ERR_ARG;
    }

    g_river_log.secondary_sink = *sink;
    return RIVER_OK;
}

void river_log_vwrite(river_log_level_t level,
                      const char *tag,
                      const char *fmt,
                      va_list args)
{
    char message[RIVER_LOG_LINE_MAX];
    uint32_t timestamp_ms;
    const char *safe_tag;

    if (!river_log_level_enabled(level) || fmt == NULL) {
        return;
    }

    safe_tag = (tag != NULL && tag[0] != '\0') ? tag : "river";
    (void)vsnprintf(message, sizeof(message), fmt, args);
    river_log_trim_newline(message);
    timestamp_ms = river_log_timestamp_ms();

    /* 统一在这里串行化输出，避免多任务日志互相穿插。 */
    river_log_ensure_mutex();
    if (g_river_log.mutex_ready) {
        (void)rtos_mutex_take(g_river_log.mutex, MUTEX_WAIT_TIMEOUT);
    }

    printf("[%010lu][%c][%s] %s\n",
           (unsigned long)timestamp_ms,
           river_log_level_letter(level),
           safe_tag,
           message);

    if (g_river_log.secondary_sink.write != NULL) {
        g_river_log.secondary_sink.write(g_river_log.secondary_sink.user_data,
                                         level,
                                         timestamp_ms,
                                         safe_tag,
                                         message);
    }

    if (g_river_log.mutex_ready) {
        (void)rtos_mutex_give(g_river_log.mutex);
    }
}

void river_log_write(river_log_level_t level,
                     const char *tag,
                     const char *fmt,
                     ...)
{
    va_list args;

    va_start(args, fmt);
    river_log_vwrite(level, tag, fmt, args);
    va_end(args);
}
