#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lwip_netconf.h"
#include "os_wrapper.h"

#include "river/river_cloud.h"
#include "river/river_log.h"
#include "river/river_wifi_station.h"
#include "river_asr_provider_internal.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.cloud"

#define RIVER_CLOUD_RING_BUFFER_BYTES  (16000 * 1 * 2 * 2) // 16k, 1ch, 2s
#define RIVER_CLOUD_WORKER_STACK       (1024U * 8U)
#define RIVER_CLOUD_WORKER_PRIO        3U

typedef struct {
    uint8_t *data;
    size_t size;
    size_t write_ptr;
    size_t read_ptr;
    size_t count;
    rtos_mutex_t lock;
    rtos_sema_t data_ready;
} river_cloud_ring_buffer_t;

typedef struct {
    bool initialized;
    bool sntp_started;
    bool stream_active;
    bool stream_request_open;
    bool is_speech_active;
    rtos_task_t worker_thread;
    river_cloud_ring_buffer_t rb;
    river_cloud_asr_result_handler_t result_handler;
    void *result_handler_user;
    const river_cloud_asr_provider_ops_t *provider;
    river_cloud_asr_audio_desc_t audio_desc;
} river_cloud_context_t;

static river_cloud_context_t g_river_cloud;

/* --- Ring Buffer --- */
static void river_cloud_rb_init(size_t size) {
    g_river_cloud.rb.data = (uint8_t *)rtos_mem_zmalloc((uint32_t)size);
    g_river_cloud.rb.size = size;
    g_river_cloud.rb.write_ptr = 0;
    g_river_cloud.rb.read_ptr = 0;
    g_river_cloud.rb.count = 0;
    rtos_mutex_create(&g_river_cloud.rb.lock);
    rtos_sema_create(&g_river_cloud.rb.data_ready, 0, 100);
}

static void river_cloud_rb_write(const uint8_t *data, size_t len) {
    rtos_mutex_take(g_river_cloud.rb.lock, RTOS_MAX_TIMEOUT);
    if ((g_river_cloud.rb.count + len) > g_river_cloud.rb.size) {
        size_t drop = (g_river_cloud.rb.count + len) - g_river_cloud.rb.size;
        g_river_cloud.rb.read_ptr = (g_river_cloud.rb.read_ptr + drop) % g_river_cloud.rb.size;
        g_river_cloud.rb.count -= drop;
    }
    size_t first_part = g_river_cloud.rb.size - g_river_cloud.rb.write_ptr;
    if (len <= first_part) memcpy(g_river_cloud.rb.data + g_river_cloud.rb.write_ptr, data, len);
    else {
        memcpy(g_river_cloud.rb.data + g_river_cloud.rb.write_ptr, data, first_part);
        memcpy(g_river_cloud.rb.data, data + first_part, len - first_part);
    }
    g_river_cloud.rb.write_ptr = (g_river_cloud.rb.write_ptr + len) % g_river_cloud.rb.size;
    g_river_cloud.rb.count += len;
    rtos_mutex_give(g_river_cloud.rb.lock);
    rtos_sema_give(g_river_cloud.rb.data_ready);
}

static size_t river_cloud_rb_read(uint8_t *data, size_t len) {
    rtos_mutex_take(g_river_cloud.rb.lock, RTOS_MAX_TIMEOUT);
    if (g_river_cloud.rb.count < len) { rtos_mutex_give(g_river_cloud.rb.lock); return 0; }
    size_t first_part = g_river_cloud.rb.size - g_river_cloud.rb.read_ptr;
    if (len <= first_part) memcpy(data, g_river_cloud.rb.data + g_river_cloud.rb.read_ptr, len);
    else {
        memcpy(data, g_river_cloud.rb.data + g_river_cloud.rb.read_ptr, first_part);
        memcpy(data + first_part, g_river_cloud.rb.data, len - first_part);
    }
    g_river_cloud.rb.read_ptr = (g_river_cloud.rb.read_ptr + len) % g_river_cloud.rb.size;
    g_river_cloud.rb.count -= len;
    rtos_mutex_give(g_river_cloud.rb.lock);
    return len;
}

bool river_cloud_utc_ready(void) {
    return time(NULL) > 1700000000;
}

uint32_t river_cloud_now_utc_seconds(void) {
    return (uint32_t)time(NULL);
}

static void river_cloud_worker_task(void *param) {
    (void)param;
    uint8_t *frame_buf = (uint8_t *)rtos_mem_zmalloc(640);
    RIVER_LOGI("asr worker thread started");
    while (1) {
        if (g_river_cloud.stream_request_open && !g_river_cloud.stream_active) {
            if (g_river_cloud.provider->stream_open(&g_river_cloud.audio_desc) == RIVER_OK) {
                g_river_cloud.stream_active = true;
                RIVER_LOGI("worker: stream opened");
            } else {
                g_river_cloud.stream_request_open = false;
                rtos_time_delay_ms(2000);
            }
        }
        if (g_river_cloud.stream_active) {
            size_t read = river_cloud_rb_read(frame_buf, 640);
            if (read > 0) {
                g_river_cloud.provider->stream_feed(frame_buf, read);
                g_river_cloud.provider->stream_poll(0);
            } else if (!g_river_cloud.is_speech_active) {
                g_river_cloud.provider->stream_finish();
                g_river_cloud.stream_active = false;
                g_river_cloud.stream_request_open = false;
            } else rtos_sema_take(g_river_cloud.rb.data_ready, 50);
        } else rtos_sema_take(g_river_cloud.rb.data_ready, 500);
    }
}

river_status_t river_cloud_adapter_init(void) {
    memset(&g_river_cloud, 0, sizeof(g_river_cloud));
    g_river_cloud.provider = &g_river_cloud_iflytek_rtasr_ops;
    river_cloud_rb_init(RIVER_CLOUD_RING_BUFFER_BYTES);
    rtos_task_create(&g_river_cloud.worker_thread, "river_cloud_wk", river_cloud_worker_task, NULL, 1024 * 8, 3);
    g_river_cloud.provider->init(g_river_cloud.result_handler, g_river_cloud.result_handler_user);
    g_river_cloud.initialized = true;
    return RIVER_OK;
}

river_status_t river_cloud_asr_stream_push_frame(const uint8_t *pcm, size_t bytes, bool is_speech) {
    (void)bytes;
    if (!g_river_cloud.initialized) return RIVER_ERR_BUSY;
    g_river_cloud.is_speech_active = is_speech;
    if (is_speech) {
        river_cloud_rb_write(pcm, 640);
        if (!g_river_cloud.stream_active && !g_river_cloud.stream_request_open) {
            if (river_wifi_station_is_connected()) {
                g_river_cloud.stream_request_open = true;
                rtos_sema_give(g_river_cloud.rb.data_ready);
            }
        }
    } else if (g_river_cloud.stream_active) river_cloud_rb_write(pcm, 640);
    return RIVER_OK;
}

river_status_t river_cloud_asr_audio_open(const river_cloud_asr_audio_desc_t *audio, uint32_t pre, uint32_t max) {
    g_river_cloud.audio_desc = *audio;
    (void)pre; (void)max;
    return RIVER_OK;
}

river_status_t river_cloud_adapter_set_result_handler(river_cloud_asr_result_handler_t handler, void *user_data) {
    g_river_cloud.result_handler = handler; g_river_cloud.result_handler_user = user_data;
    return RIVER_OK;
}

void river_cloud_asr_audio_close(void) { g_river_cloud.is_speech_active = false; }
river_status_t river_cloud_asr_batch_submit_segment(const uint8_t *p, size_t b, const river_voice_segment_desc_t *s) { (void)p; (void)b; (void)s; return RIVER_ERR_UNSUPPORTED; }
const char *river_cloud_asr_provider_name(void) { return "iflytek"; }
bool river_cloud_asr_streaming_supported(void) { return true; }
bool river_cloud_asr_batch_supported(void) { return false; }
void river_cloud_adapter_dump_status(void) {}
river_status_t river_cloud_adapter_submit_text(const char *t) { (void)t; return RIVER_OK; }
void river_cloud_seed_time_from_build_if_needed(void) {}
void river_cloud_log_stream_open_deferred_once(river_status_t s) { (void)s; }
void river_cloud_log_time_ready_once(void) {}
