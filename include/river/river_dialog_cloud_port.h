/* 对话云端端口：由 core 注册具体实现，voice 侧只依赖稳定调用面。 */
#ifndef AMEBA_RIVER_DIALOG_CLOUD_PORT_H
#define AMEBA_RIVER_DIALOG_CLOUD_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "river/river_types.h"
#include "river/river_voice_segment_sink.h"

typedef struct {
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t bits_per_sample;
    uint32_t frame_ms;
    const char *encoding;
} river_dialog_asr_audio_desc_t;

typedef struct {
    const char *(*provider_name)(void);
    bool (*asr_streaming_supported)(void);
    bool (*asr_batch_supported)(void);
    river_status_t (*asr_audio_open)(const river_dialog_asr_audio_desc_t *audio,
                                     uint32_t pre_roll_ms,
                                     uint32_t post_roll_ms);
    void (*asr_audio_close)(void);
    river_status_t (*asr_stream_push_frame)(const uint8_t *pcm,
                                            size_t bytes,
                                            bool is_speech);
    river_status_t (*asr_batch_submit_segment)(const uint8_t *pcm,
                                               size_t bytes,
                                               const river_voice_segment_desc_t *segment);
    river_status_t (*begin_conversation_window)(const char *source);
    river_status_t (*interrupt_tts_with_reason)(const char *reason);
} river_dialog_cloud_port_t;

river_status_t river_dialog_cloud_port_register(const river_dialog_cloud_port_t *port);
bool river_dialog_cloud_port_registered(void);
const char *river_dialog_cloud_provider_name(void);
bool river_dialog_cloud_asr_streaming_supported(void);
bool river_dialog_cloud_asr_batch_supported(void);
river_status_t river_dialog_cloud_asr_audio_open(const river_dialog_asr_audio_desc_t *audio,
                                                 uint32_t pre_roll_ms,
                                                 uint32_t post_roll_ms);
void river_dialog_cloud_asr_audio_close(void);
river_status_t river_dialog_cloud_asr_stream_push_frame(const uint8_t *pcm,
                                                        size_t bytes,
                                                        bool is_speech);
river_status_t river_dialog_cloud_asr_batch_submit_segment(
    const uint8_t *pcm,
    size_t bytes,
    const river_voice_segment_desc_t *segment);
river_status_t river_dialog_cloud_begin_conversation_window(const char *source);
river_status_t river_dialog_cloud_interrupt_tts_with_reason(const char *reason);

#endif
