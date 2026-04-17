/* 回放参考服务：把播放流导出为 AEC/参考链路可消费的历史音频。 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "basic_types.h"
#include "os_wrapper.h"

#include "river/river_log.h"
#include "river/river_reference_service.h"
#include "river/river_voice_ref.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.refsvc"

typedef struct {
    bool initialized;
    rtos_mutex_t lock;
    river_reference_service_config_t config;
    river_reference_service_stats_t stats;
} river_reference_service_context_t;

static river_reference_service_context_t g_river_reference_service;

static uint32_t river_reference_service_now_ms(void)
{
    return (uint32_t)rtos_time_get_current_system_time_ms();
}

static void river_reference_service_copy_name(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0U) {
        return;
    }

    if (src == NULL || src[0] == '\0') {
        strncpy(dst, "-", dst_size - 1U);
    } else {
        strncpy(dst, src, dst_size - 1U);
    }
    dst[dst_size - 1U] = '\0';
}

static void river_reference_service_set_state_locked(river_reference_state_t state)
{
    g_river_reference_service.stats.state = state;
}

static void river_reference_service_reset_config_locked(void)
{
    memset(&g_river_reference_service.config, 0, sizeof(g_river_reference_service.config));
    g_river_reference_service.stats.sample_rate = 0U;
    g_river_reference_service.stats.frame_ms = 0U;
    g_river_reference_service.stats.channels = 0U;
    g_river_reference_service.stats.history_ms = 0U;
    river_reference_service_copy_name(g_river_reference_service.stats.stream_name,
                                      sizeof(g_river_reference_service.stats.stream_name),
                                      NULL);
    river_reference_service_copy_name(g_river_reference_service.stats.source_name,
                                      sizeof(g_river_reference_service.stats.source_name),
                                      NULL);
    g_river_reference_service.stats.last_open_ms = 0U;
    g_river_reference_service.stats.last_reset_ms = 0U;
    g_river_reference_service.stats.last_write_ms = 0U;
    g_river_reference_service.stats.last_read_ms = 0U;
}

river_status_t river_reference_service_init(void)
{
    if (g_river_reference_service.initialized) {
        return RIVER_OK;
    }

    memset(&g_river_reference_service, 0, sizeof(g_river_reference_service));
    if (rtos_mutex_create(&g_river_reference_service.lock) != RTK_SUCCESS) {
        return RIVER_ERR_NO_MEMORY;
    }

    g_river_reference_service.initialized = true;
    river_reference_service_reset_config_locked();
    g_river_reference_service.stats.state = RIVER_REFERENCE_IDLE;
    return RIVER_OK;
}

river_status_t river_reference_service_open(const river_reference_service_config_t *config)
{
    river_status_t status;
    uint32_t now_ms;

    if (config == NULL || config->sample_rate == 0U || config->frame_ms == 0U ||
        config->channels == 0U) {
        return RIVER_ERR_ARG;
    }

    status = river_reference_service_init();
    if (status != RIVER_OK) {
        return status;
    }

    if (rtos_mutex_take(g_river_reference_service.lock, MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
        return RIVER_ERR_BUSY;
    }

    if (river_voice_ref_is_open()) {
        river_voice_ref_close();
    }

    if (river_voice_ref_open(config->sample_rate,
                             config->frame_ms,
                             config->channels,
                             config->history_ms) != RIVER_OK) {
        river_reference_service_set_state_locked(RIVER_REFERENCE_ERROR);
        rtos_mutex_give(g_river_reference_service.lock);
        return RIVER_ERR_UNSUPPORTED;
    }

    g_river_reference_service.config = *config;
    g_river_reference_service.stats.sample_rate = config->sample_rate;
    g_river_reference_service.stats.frame_ms = config->frame_ms;
    g_river_reference_service.stats.channels = config->channels;
    g_river_reference_service.stats.history_ms = config->history_ms;
    g_river_reference_service.stats.open_count++;
    now_ms = river_reference_service_now_ms();
    river_reference_service_copy_name(g_river_reference_service.stats.stream_name,
                                      sizeof(g_river_reference_service.stats.stream_name),
                                      config->stream_name);
    river_reference_service_copy_name(g_river_reference_service.stats.source_name,
                                      sizeof(g_river_reference_service.stats.source_name),
                                      config->source_name);
    g_river_reference_service.stats.last_open_ms = now_ms;
    g_river_reference_service.stats.last_reset_ms = 0U;
    g_river_reference_service.stats.last_write_ms = 0U;
    g_river_reference_service.stats.last_read_ms = 0U;
    river_reference_service_set_state_locked(RIVER_REFERENCE_OPEN);

    rtos_mutex_give(g_river_reference_service.lock);
    return RIVER_OK;
}

void river_reference_service_reset(void)
{
    uint32_t now_ms;

    if (!g_river_reference_service.initialized) {
        return;
    }

    if (rtos_mutex_take(g_river_reference_service.lock, MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
        return;
    }

    if (river_voice_ref_is_open()) {
        river_voice_ref_reset();
        g_river_reference_service.stats.reset_count++;
        now_ms = river_reference_service_now_ms();
        g_river_reference_service.stats.last_reset_ms = now_ms;
        g_river_reference_service.stats.last_write_ms = 0U;
        g_river_reference_service.stats.last_read_ms = 0U;
        river_reference_service_set_state_locked(RIVER_REFERENCE_OPEN);
    }

    rtos_mutex_give(g_river_reference_service.lock);
}

void river_reference_service_close(void)
{
    if (!g_river_reference_service.initialized) {
        return;
    }

    if (rtos_mutex_take(g_river_reference_service.lock, MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
        return;
    }

    if (river_voice_ref_is_open()) {
        river_voice_ref_close();
        g_river_reference_service.stats.close_count++;
    }
    river_reference_service_reset_config_locked();
    river_reference_service_set_state_locked(RIVER_REFERENCE_IDLE);

    rtos_mutex_give(g_river_reference_service.lock);
}

river_status_t river_reference_service_write(const uint8_t *data, size_t bytes)
{
    river_status_t status;
    uint32_t now_ms;

    if (data == NULL || bytes == 0U) {
        return RIVER_ERR_ARG;
    }

    if (river_reference_service_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }

    if (rtos_mutex_take(g_river_reference_service.lock, MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
        return RIVER_ERR_BUSY;
    }

    if (!river_voice_ref_is_open()) {
        river_reference_service_set_state_locked(RIVER_REFERENCE_IDLE);
        rtos_mutex_give(g_river_reference_service.lock);
        return RIVER_ERR_NOT_FOUND;
    }

    status = river_voice_ref_push(data, bytes);
    now_ms = river_reference_service_now_ms();
    if (status == RIVER_OK) {
        g_river_reference_service.stats.write_ok++;
        g_river_reference_service.stats.last_write_ms = now_ms;
        river_reference_service_set_state_locked(RIVER_REFERENCE_OPEN);
    } else {
        g_river_reference_service.stats.write_fail++;
        river_reference_service_set_state_locked(RIVER_REFERENCE_ERROR);
    }

    rtos_mutex_give(g_river_reference_service.lock);
    return status;
}

river_status_t river_reference_service_read(uint8_t *data, size_t bytes)
{
    river_status_t status;
    uint32_t now_ms;

    if (data == NULL || bytes == 0U) {
        return RIVER_ERR_ARG;
    }

    if (river_reference_service_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }

    if (rtos_mutex_take(g_river_reference_service.lock, MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
        return RIVER_ERR_BUSY;
    }

    if (!river_voice_ref_is_open()) {
        g_river_reference_service.stats.read_miss++;
        river_reference_service_set_state_locked(RIVER_REFERENCE_IDLE);
        rtos_mutex_give(g_river_reference_service.lock);
        return RIVER_ERR_NOT_FOUND;
    }

    status = river_voice_ref_read(data, bytes);
    now_ms = river_reference_service_now_ms();
    if (status == RIVER_OK) {
        g_river_reference_service.stats.read_ok++;
        g_river_reference_service.stats.last_read_ms = now_ms;
        river_reference_service_set_state_locked(RIVER_REFERENCE_OPEN);
    } else if (status == RIVER_ERR_NOT_FOUND) {
        g_river_reference_service.stats.read_miss++;
        river_reference_service_set_state_locked(RIVER_REFERENCE_STARVED);
    } else {
        g_river_reference_service.stats.read_miss++;
        river_reference_service_set_state_locked(RIVER_REFERENCE_ERROR);
    }

    rtos_mutex_give(g_river_reference_service.lock);
    return status;
}

bool river_reference_service_is_open(void)
{
    return river_voice_ref_is_open();
}

river_reference_state_t river_reference_service_state(void)
{
    return g_river_reference_service.stats.state;
}

const char *river_reference_service_state_name(river_reference_state_t state)
{
    switch (state) {
    case RIVER_REFERENCE_IDLE:
        return "idle";
    case RIVER_REFERENCE_OPEN:
        return "open";
    case RIVER_REFERENCE_STARVED:
        return "starved";
    case RIVER_REFERENCE_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

const char *river_reference_service_backend_name(void)
{
    return river_voice_ref_backend_name();
}

void river_reference_service_get_stats(river_reference_service_stats_t *stats)
{
    river_voice_ref_stats_t ref_stats;

    if (stats == NULL) {
        return;
    }

    memset(stats, 0, sizeof(*stats));
    if (!g_river_reference_service.initialized) {
        stats->state = RIVER_REFERENCE_IDLE;
        return;
    }

    if (rtos_mutex_take(g_river_reference_service.lock, MUTEX_WAIT_TIMEOUT) != RTK_SUCCESS) {
        stats->state = RIVER_REFERENCE_ERROR;
        return;
    }

    *stats = g_river_reference_service.stats;
    rtos_mutex_give(g_river_reference_service.lock);

    memset(&ref_stats, 0, sizeof(ref_stats));
    river_voice_ref_get_stats(&ref_stats);
    stats->queue_frames = ref_stats.queue_frames;
    stats->queue_peak_frames = ref_stats.queue_peak_frames;
    stats->queue_capacity_frames = ref_stats.queue_capacity_frames;
    stats->dropped_frames = ref_stats.dropped_frames;
}

void river_reference_service_dump_profile(void)
{
    river_reference_service_stats_t stats;

    river_reference_service_get_stats(&stats);
    if (stats.state == RIVER_REFERENCE_IDLE) {
        RIVER_LOGI("playback ref: deferred backend=%s source=post-delay mono speaker feed state=%s",
                   river_reference_service_backend_name(),
                   river_reference_service_state_name(stats.state));
        return;
    }

    RIVER_LOGI("playback ref: %s %lu Hz %lums %luch history=%lums source=%s state=%s",
               river_reference_service_backend_name(),
               (unsigned long)stats.sample_rate,
               (unsigned long)stats.frame_ms,
               (unsigned long)stats.channels,
               (unsigned long)stats.history_ms,
               stats.source_name[0] != '\0' ? stats.source_name : "-",
               river_reference_service_state_name(stats.state));
}

void river_reference_service_dump_status(void)
{
    river_reference_service_stats_t stats;

    river_reference_service_get_stats(&stats);
    RIVER_LOGI("reference_service=%s stream=%s source=%s backend=%s writes=%lu/%lu reads=%lu misses=%lu opens=%lu closes=%lu resets=%lu queue=%lu/%lu peak=%lu dropped=%lu",
               river_reference_service_state_name(stats.state),
               stats.stream_name[0] != '\0' ? stats.stream_name : "-",
               stats.source_name[0] != '\0' ? stats.source_name : "-",
               river_reference_service_backend_name(),
               (unsigned long)stats.write_ok,
               (unsigned long)stats.write_fail,
               (unsigned long)stats.read_ok,
               (unsigned long)stats.read_miss,
               (unsigned long)stats.open_count,
               (unsigned long)stats.close_count,
               (unsigned long)stats.reset_count,
               (unsigned long)stats.queue_frames,
               (unsigned long)stats.queue_capacity_frames,
               (unsigned long)stats.queue_peak_frames,
               (unsigned long)stats.dropped_frames);
}
