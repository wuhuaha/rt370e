#ifndef AMEBA_RIVER_ASR_PROVIDER_INTERNAL_H
#define AMEBA_RIVER_ASR_PROVIDER_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "river/river_cloud.h"

typedef void (*river_cloud_asr_provider_result_cb_t)(const river_cloud_asr_result_t *result,
                                                     void *user_data);

typedef struct {
    const char *(*provider_name)(void);
    bool (*supports_streaming)(void);
    bool (*supports_batch)(void);
    river_status_t (*init)(river_cloud_asr_provider_result_cb_t callback, void *user_data);
    void (*deinit)(void);
    river_status_t (*stream_open)(const river_cloud_asr_audio_desc_t *audio);
    river_status_t (*stream_feed)(const uint8_t *pcm, size_t bytes);
    river_status_t (*stream_finish)(void);
    river_status_t (*stream_poll)(uint32_t timeout_ms);
    bool (*stream_active)(void);
    river_status_t (*batch_submit)(const uint8_t *pcm,
                                   size_t bytes,
                                   const river_voice_segment_desc_t *segment);
    void (*dump_status)(void);
} river_cloud_asr_provider_ops_t;

extern const river_cloud_asr_provider_ops_t g_river_cloud_iflytek_rtasr_ops;

const river_cloud_asr_provider_ops_t *river_cloud_provider_lookup(const char *name);
const river_cloud_asr_provider_ops_t *river_cloud_provider_default(void);
uint32_t river_cloud_now_utc_seconds(void);
bool river_cloud_utc_ready(void);

#endif
