/* Opus 编解码器封装，屏蔽底层库句柄和参数细节。 */
#ifndef AMEBA_RIVER_OPUS_CODEC_H
#define AMEBA_RIVER_OPUS_CODEC_H

#include <stddef.h>
#include <stdint.h>

#include "river/river_types.h"

typedef struct {
    void *handle;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t frame_duration_ms;
    size_t pcm_frame_bytes;
} river_opus_encoder_t;

typedef struct {
    void *handle;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t frame_duration_ms;
    size_t pcm_frame_capacity_bytes;
} river_opus_decoder_t;

river_status_t river_opus_encoder_open(river_opus_encoder_t *encoder,
                                       uint32_t sample_rate,
                                       uint32_t channels,
                                       uint32_t frame_duration_ms,
                                       int bitrate_bps);
void river_opus_encoder_close(river_opus_encoder_t *encoder);
river_status_t river_opus_encode(const river_opus_encoder_t *encoder,
                                 const int16_t *pcm,
                                 size_t pcm_bytes,
                                 uint8_t *packet,
                                 size_t packet_capacity,
                                 size_t *packet_bytes_out);

river_status_t river_opus_decoder_open(river_opus_decoder_t *decoder,
                                       uint32_t sample_rate,
                                       uint32_t channels,
                                       uint32_t frame_duration_ms);
void river_opus_decoder_close(river_opus_decoder_t *decoder);
river_status_t river_opus_decode(const river_opus_decoder_t *decoder,
                                 const uint8_t *packet,
                                 size_t packet_bytes,
                                 int16_t *pcm,
                                 size_t pcm_capacity_bytes,
                                 size_t *pcm_bytes_out);

#endif
