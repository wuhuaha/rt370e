/* 运行时统计模块：定期采样任务占用、栈余量和堆水位。 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "portable.h"
#include "task.h"

#include "os_wrapper.h"

#include "river/river_log.h"
#include "river/river_runtime_stats.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.stats"

#define RIVER_RUNTIME_STATS_TOP_TASKS 3U

static bool g_river_runtime_stats_initialized;
static bool g_river_runtime_stats_lock_ready;
static rtos_mutex_t g_river_runtime_stats_lock;
static TaskStatus_t *g_river_runtime_stats_tasks;
static UBaseType_t g_river_runtime_stats_tasks_capacity;

static uint32_t river_runtime_stack_bytes(configSTACK_DEPTH_TYPE high_water_mark)
{
    return (uint32_t)high_water_mark * (uint32_t)sizeof(StackType_t);
}

static const TaskStatus_t *river_runtime_find_task(const TaskStatus_t *tasks,
                                                   UBaseType_t task_count,
                                                   const char *name)
{
    UBaseType_t index;

    if (tasks == NULL || name == NULL) {
        return NULL;
    }

    for (index = 0U; index < task_count; ++index) {
        if (strcmp(tasks[index].pcTaskName, name) == 0) {
            return &tasks[index];
        }
    }

    return NULL;
}

static void river_runtime_format_cpu_top(char *buffer,
                                         size_t buffer_size,
                                         const TaskStatus_t *tasks,
                                         UBaseType_t task_count,
                                         uint32_t total_runtime)
{
    const TaskStatus_t *top[RIVER_RUNTIME_STATS_TOP_TASKS] = {0};
    UBaseType_t index;
    size_t offset;

    if (buffer == NULL || buffer_size == 0U) {
        return;
    }

    if (tasks == NULL || task_count == 0U || total_runtime == 0U) {
        (void)snprintf(buffer, buffer_size, "warmup");
        return;
    }

    for (index = 0U; index < task_count; ++index) {
        const TaskStatus_t *candidate;
        UBaseType_t slot;

        candidate = &tasks[index];
        for (slot = 0U; slot < RIVER_RUNTIME_STATS_TOP_TASKS; ++slot) {
            if (top[slot] == NULL ||
                candidate->ulRunTimeCounter > top[slot]->ulRunTimeCounter) {
                UBaseType_t move;

                for (move = RIVER_RUNTIME_STATS_TOP_TASKS - 1U; move > slot; --move) {
                    top[move] = top[move - 1U];
                }
                top[slot] = candidate;
                break;
            }
        }
    }

    offset = 0U;
    for (index = 0U; index < RIVER_RUNTIME_STATS_TOP_TASKS; ++index) {
        uint32_t cpu_pct;
        int written;

        if (top[index] == NULL || top[index]->ulRunTimeCounter == 0U) {
            continue;
        }

        cpu_pct = (uint32_t)(((uint64_t)top[index]->ulRunTimeCounter * 100ULL) /
                             (uint64_t)total_runtime);
        written = snprintf(buffer + offset,
                           buffer_size - offset,
                           "%s%s:%lu%%/%luB",
                           offset == 0U ? "" : ",",
                           top[index]->pcTaskName,
                           (unsigned long)cpu_pct,
                           (unsigned long)river_runtime_stack_bytes(
                               top[index]->usStackHighWaterMark));
        if (written < 0) {
            break;
        }
        if ((size_t)written >= (buffer_size - offset)) {
            offset = buffer_size - 1U;
            break;
        }
        offset += (size_t)written;
    }

    if (offset == 0U) {
        (void)snprintf(buffer, buffer_size, "warmup");
    }
}

static TaskStatus_t *river_runtime_stats_ensure_task_buffer(UBaseType_t required_capacity)
{
    TaskStatus_t *new_tasks;
    UBaseType_t new_capacity;

    if (required_capacity == 0U) {
        return NULL;
    }

    if (g_river_runtime_stats_tasks != NULL &&
        g_river_runtime_stats_tasks_capacity >= required_capacity) {
        memset(g_river_runtime_stats_tasks,
               0,
               (size_t)g_river_runtime_stats_tasks_capacity * sizeof(TaskStatus_t));
        return g_river_runtime_stats_tasks;
    }

    new_capacity = required_capacity + 4U;
    new_tasks = (TaskStatus_t *)rtos_mem_calloc((uint32_t)new_capacity,
                                                (uint32_t)sizeof(TaskStatus_t));
    if (new_tasks == NULL) {
        return NULL;
    }

    if (g_river_runtime_stats_tasks != NULL) {
        rtos_mem_free(g_river_runtime_stats_tasks);
    }

    g_river_runtime_stats_tasks = new_tasks;
    g_river_runtime_stats_tasks_capacity = new_capacity;
    return g_river_runtime_stats_tasks;
}

void river_runtime_stats_init(void)
{
    if (!g_river_runtime_stats_lock_ready) {
        if (rtos_mutex_create(&g_river_runtime_stats_lock) == 0) {
            g_river_runtime_stats_lock_ready = true;
        } else {
            return;
        }
    }

    g_river_runtime_stats_initialized = true;
}

void river_runtime_stats_snapshot(const char *reason)
{
    uint32_t heap_free;
    uint32_t heap_min;
    const char *snapshot_reason;

    if (!g_river_runtime_stats_initialized) {
        return;
    }

    snapshot_reason = (reason != NULL && reason[0] != '\0') ? reason : "unspecified";
    heap_free = rtos_mem_get_free_heap_size();
    heap_min = rtos_mem_get_minimum_ever_free_heap_size();

    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        RIVER_LOGI("snapshot reason=%s heap_free=%lu heap_min=%lu scheduler=not_running",
                   snapshot_reason,
                   (unsigned long)heap_free,
                   (unsigned long)heap_min);
        return;
    }

    {
        UBaseType_t capacity;
        UBaseType_t task_count;
        TaskStatus_t *tasks;
        uint32_t total_runtime;
        char cpu_top[160];
        const TaskStatus_t *vad_task;
        const TaskStatus_t *cap_task;
        const TaskStatus_t *echo_task;

        if (!g_river_runtime_stats_lock_ready ||
            rtos_mutex_take(g_river_runtime_stats_lock, RTOS_MAX_TIMEOUT) != 0) {
            RIVER_LOGW("snapshot reason=%s heap_free=%lu heap_min=%lu task_stats=alloc_failed",
                       snapshot_reason,
                       (unsigned long)heap_free,
                       (unsigned long)heap_min);
            return;
        }

        capacity = uxTaskGetNumberOfTasks() + 4U;
        tasks = river_runtime_stats_ensure_task_buffer(capacity);
        if (tasks == NULL) {
            rtos_mutex_give(g_river_runtime_stats_lock);
            RIVER_LOGW("snapshot reason=%s heap_free=%lu heap_min=%lu task_stats=alloc_failed",
                       snapshot_reason,
                       (unsigned long)heap_free,
                       (unsigned long)heap_min);
            return;
        }

        total_runtime = 0U;
        task_count = uxTaskGetSystemState(tasks,
                                         g_river_runtime_stats_tasks_capacity,
                                         &total_runtime);
        river_runtime_format_cpu_top(cpu_top, sizeof(cpu_top), tasks, task_count, total_runtime);

        vad_task = river_runtime_find_task(tasks, task_count, "river_vad_probe");
        cap_task = river_runtime_find_task(tasks, task_count, "river_cap_drv");
        echo_task = river_runtime_find_task(tasks, task_count, "river_audio_echo");

        RIVER_LOGI("snapshot reason=%s heap_free=%lu heap_min=%lu tasks=%lu cpu_top=[%s] stack_free=[vad:%luB,cap:%luB,echo:%luB]",
                   snapshot_reason,
                   (unsigned long)heap_free,
                   (unsigned long)heap_min,
                   (unsigned long)task_count,
                   cpu_top,
                   (unsigned long)(vad_task != NULL ?
                       river_runtime_stack_bytes(vad_task->usStackHighWaterMark) : 0U),
                   (unsigned long)(cap_task != NULL ?
                       river_runtime_stack_bytes(cap_task->usStackHighWaterMark) : 0U),
                   (unsigned long)(echo_task != NULL ?
                       river_runtime_stack_bytes(echo_task->usStackHighWaterMark) : 0U));
        rtos_mutex_give(g_river_runtime_stats_lock);
    }
}
