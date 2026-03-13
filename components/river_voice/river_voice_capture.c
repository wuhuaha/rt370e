#include <stdbool.h>
#include <string.h>

#include "basic_types.h"
#include "audio/audio_control.h"
#include "audio/audio_record.h"
#include "os_wrapper.h"

#include "river/river_log.h"
#include "river/river_voice_board.h"
#include "river/river_voice_capture.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.voice.capture"

#define RIVER_VOICE_CAPTURE_RING_BUF_MS    800U
#define RIVER_VOICE_CAPTURE_THREAD_STACK   (1024U * 2U)
/* Priority 6: Above all other voice/app tasks to ensure IPC responsiveness */
#define RIVER_VOICE_CAPTURE_THREAD_PRIO    6U

typedef struct {
    uint8_t *data;
    size_t size;
    size_t write_ptr;
    size_t read_ptr;
    size_t count;
    rtos_sema_t sema;
    rtos_mutex_t lock;
} river_ring_buffer_t;

static struct {
    river_ring_buffer_t rb;
    rtos_task_t thread;
    bool running;
    river_voice_capture_t *active_capture;
} g_river_cap_internal;

static void river_ring_buffer_init(river_ring_buffer_t *rb, size_t size)
{
    rb->data = (uint8_t *)rtos_mem_zmalloc((uint32_t)size);
    rb->size = size;
    rb->write_ptr = 0;
    rb->read_ptr = 0;
    rb->count = 0;
    rtos_sema_create(&rb->sema, 0, 100);
    rtos_mutex_create(&rb->lock);
}

static void river_ring_buffer_write(river_ring_buffer_t *rb, const uint8_t *data, size_t len)
{
    rtos_mutex_take(rb->lock, RTOS_MAX_TIMEOUT);
    if ((rb->count + len) > rb->size) {
        size_t drop = (rb->count + len) - rb->size;
        rb->read_ptr = (rb->read_ptr + drop) % rb->size;
        rb->count -= drop;
    }

    size_t first_part = rb->size - rb->write_ptr;
    if (len <= first_part) {
        memcpy(rb->data + rb->write_ptr, data, len);
    } else {
        memcpy(rb->data + rb->write_ptr, data, first_part);
        memcpy(rb->data, data + first_part, len - first_part);
    }
    rb->write_ptr = (rb->write_ptr + len) % rb->size;
    rb->count += len;
    rtos_mutex_give(rb->lock);
    rtos_sema_give(rb->sema);
}

static size_t river_ring_buffer_read(river_ring_buffer_t *rb, uint8_t *data, size_t len)
{
    while (rb->count < len) {
        if (!g_river_cap_internal.running) return 0;
        if (rtos_sema_take(rb->sema, 500) != RTK_SUCCESS) {
            continue;
        }
    }

    rtos_mutex_take(rb->lock, RTOS_MAX_TIMEOUT);
    size_t first_part = rb->size - rb->read_ptr;
    if (len <= first_part) {
        memcpy(data, rb->data + rb->read_ptr, len);
    } else {
        memcpy(data, rb->data + rb->read_ptr, first_part);
        memcpy(data + first_part, rb->data, len - first_part);
    }
    rb->read_ptr = (rb->read_ptr + len) % rb->size;
    rb->count -= len;
    rtos_mutex_give(rb->lock);
    return len;
}

static void river_voice_capture_thread(void *param)
{
    river_voice_capture_t *capture = (river_voice_capture_t *)param;
    uint8_t *tmp_buf = (uint8_t *)rtos_mem_zmalloc((uint32_t)capture->frame_bytes);

    RIVER_LOGI("internal capture thread started (priority=%d)", RIVER_VOICE_CAPTURE_THREAD_PRIO);

    while (g_river_cap_internal.running) {
        int32_t ret = AudioRecord_Read((struct AudioRecord *)capture->record, tmp_buf, capture->frame_bytes, true);
        if (ret > 0) {
            river_ring_buffer_write(&g_river_cap_internal.rb, tmp_buf, (size_t)ret);
        } else {
            rtos_time_delay_ms(5);
        }
    }

    rtos_mem_free(tmp_buf);
    rtos_task_delete(NULL);
}

river_status_t river_voice_capture_open(river_voice_capture_t *capture)
{
    const river_voice_board_array_profile_t *profile;
    AudioRecordConfig record_config;

    if (capture == 0) {
        return RIVER_ERR_ARG;
    }

    memset(capture, 0, sizeof(*capture));
    profile = river_voice_board_array_profile();

    capture->sample_rate = profile->sample_rate;
    capture->channels = profile->capture_channels;
    capture->frame_ms = profile->frame_ms;
    capture->frame_samples = (profile->sample_rate * profile->frame_ms) / 1000U;
    capture->frame_bytes = capture->frame_samples * capture->channels * sizeof(int16_t);

    AudioControl_SetMicUsage(AUDIO_CAPTURE_USAGE_AMIC);
    AudioControl_SetChannelMicCategory(0, profile->primary_mic);
    AudioControl_SetMicBstGain(profile->primary_mic, profile->primary_mic_gain);
    if (profile->capture_channels > 1U) {
        AudioControl_SetChannelMicCategory(1, profile->secondary_mic);
        AudioControl_SetMicBstGain(profile->secondary_mic, profile->secondary_mic_gain);
    }

    capture->record = AudioRecord_Create();
    if (capture->record == 0) {
        return RIVER_ERR_UNSUPPORTED;
    }

    record_config.sample_rate = capture->sample_rate;
    record_config.channel_count = capture->channels;
    record_config.format = AUDIO_FORMAT_PCM_16_BIT;
    record_config.device = DEVICE_IN_MIC;
    record_config.buffer_bytes = (uint32_t)capture->frame_bytes * 4U;
    
    if (AudioRecord_Init((struct AudioRecord *)capture->record, &record_config, AUDIO_INPUT_FLAG_NONE) != 0) {
        river_voice_capture_close(capture);
        return RIVER_ERR_UNSUPPORTED;
    }

    AudioRecord_SetParameters((struct AudioRecord *)capture->record, "cap_mode=no_afe_pure_data");

    if (AudioRecord_Start((struct AudioRecord *)capture->record) != 0) {
        river_voice_capture_close(capture);
        return RIVER_ERR_UNSUPPORTED;
    }

    river_ring_buffer_init(&g_river_cap_internal.rb, 
                           (capture->frame_bytes * RIVER_VOICE_CAPTURE_RING_BUF_MS) / capture->frame_ms);
    
    g_river_cap_internal.running = true;
    g_river_cap_internal.active_capture = capture;

    if (rtos_task_create(&g_river_cap_internal.thread, "river_cap_drv", river_voice_capture_thread, 
                         capture, RIVER_VOICE_CAPTURE_THREAD_STACK, RIVER_VOICE_CAPTURE_THREAD_PRIO) != RTK_SUCCESS) {
        river_voice_capture_close(capture);
        return RIVER_ERR_IO;
    }

    capture->started = 1;
    return RIVER_OK;
}

int32_t river_voice_capture_read(river_voice_capture_t *capture, void *buffer, size_t bytes)
{
    (void)capture;
    if (!g_river_cap_internal.running) {
        return -1;
    }

    return (int32_t)river_ring_buffer_read(&g_river_cap_internal.rb, (uint8_t *)buffer, bytes);
}

void river_voice_capture_close(river_voice_capture_t *capture)
{
    if (capture == 0) {
        return;
    }

    g_river_cap_internal.running = false;
    rtos_time_delay_ms(100);

    if (capture->record != 0) {
        if (capture->started) {
            AudioRecord_Stop((struct AudioRecord *)capture->record);
            capture->started = 0;
        }
        AudioRecord_Destroy((struct AudioRecord *)capture->record);
        capture->record = 0;
    }
}

void river_voice_capture_dump_profile(void)
{
    const river_voice_board_array_profile_t *profile;

    profile = river_voice_board_array_profile();
    RIVER_LOGI("capture profile: %lu Hz, %lums, %luch, %s+%s",
               (unsigned long)profile->sample_rate,
               (unsigned long)profile->frame_ms,
               (unsigned long)profile->capture_channels,
               river_voice_board_mic_name(profile->primary_mic),
               river_voice_board_mic_name(profile->secondary_mic));
}
