#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "opus.h"

#include "river/river_log.h"
#include "river/river_opus_codec.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.cloud.opus"

static bool river_opus_frame_duration_supported(uint32_t frame_duration_ms)
{
    return frame_duration_ms == 10U || frame_duration_ms == 20U ||
           frame_duration_ms == 40U || frame_duration_ms == 60U;
}

static size_t river_opus_pcm_bytes(uint32_t sample_rate,
                                   uint32_t channels,
                                   uint32_t frame_duration_ms)
{
    return ((size_t)sample_rate * (size_t)frame_duration_ms / 1000U) *
           (size_t)channels * sizeof(int16_t);
}

river_status_t river_opus_encoder_open(river_opus_encoder_t *encoder,
                                       uint32_t sample_rate,
                                       uint32_t channels,
                                       uint32_t frame_duration_ms,
                                       int bitrate_bps)
{
    OpusEncoder *handle;
    int err = OPUS_OK;
    (void)bitrate_bps;

    if (encoder == NULL || sample_rate == 0U || channels == 0U ||
        !river_opus_frame_duration_supported(frame_duration_ms)) {
        return RIVER_ERR_ARG;
    }

    memset(encoder, 0, sizeof(*encoder));
    handle = opus_encoder_create((opus_int32)sample_rate,
                                 (int)channels,
                                 OPUS_APPLICATION_VOIP,
                                 &err);
    if (handle == NULL || err != OPUS_OK) {
        RIVER_LOGE("opus encoder create failed: err=%d", err);
        return RIVER_ERR_UNSUPPORTED;
    }

    encoder->handle = handle;
    encoder->sample_rate = sample_rate;
    encoder->channels = channels;
    encoder->frame_duration_ms = frame_duration_ms;
    encoder->pcm_frame_bytes = river_opus_pcm_bytes(sample_rate, channels, frame_duration_ms);
    RIVER_LOGI("opus encoder open: %luHz/%luch frame=%lums bytes=%lu app=voip defaults=sdk",
               (unsigned long)sample_rate,
               (unsigned long)channels,
               (unsigned long)frame_duration_ms,
               (unsigned long)encoder->pcm_frame_bytes);
    return RIVER_OK;
}

void river_opus_encoder_close(river_opus_encoder_t *encoder)
{
    if (encoder == NULL) {
        return;
    }
    if (encoder->handle != NULL) {
        opus_encoder_destroy((OpusEncoder *)encoder->handle);
    }
    memset(encoder, 0, sizeof(*encoder));
}

river_status_t river_opus_encode(const river_opus_encoder_t *encoder,
                                 const int16_t *pcm,
                                 size_t pcm_bytes,
                                 uint8_t *packet,
                                 size_t packet_capacity,
                                 size_t *packet_bytes_out)
{
    int encoded_bytes;
    int frame_size_samples;

    if (packet_bytes_out != NULL) {
        *packet_bytes_out = 0U;
    }
    if (encoder == NULL || encoder->handle == NULL || pcm == NULL || packet == NULL ||
        packet_capacity == 0U || pcm_bytes != encoder->pcm_frame_bytes) {
        return RIVER_ERR_ARG;
    }

    frame_size_samples = (int)((encoder->sample_rate * encoder->frame_duration_ms) / 1000U);
    encoded_bytes = opus_encode((OpusEncoder *)encoder->handle,
                                pcm,
                                frame_size_samples,
                                packet,
                                (opus_int32)packet_capacity);
    if (encoded_bytes <= 0) {
        RIVER_LOGW("opus encode failed: ret=%d", encoded_bytes);
        return RIVER_ERR_IO;
    }

    if (packet_bytes_out != NULL) {
        *packet_bytes_out = (size_t)encoded_bytes;
    }
    return RIVER_OK;
}

river_status_t river_opus_decoder_open(river_opus_decoder_t *decoder,
                                       uint32_t sample_rate,
                                       uint32_t channels,
                                       uint32_t frame_duration_ms)
{
    OpusDecoder *handle;
    int err = OPUS_OK;

    if (decoder == NULL || sample_rate == 0U || channels == 0U ||
        !river_opus_frame_duration_supported(frame_duration_ms)) {
        return RIVER_ERR_ARG;
    }

    memset(decoder, 0, sizeof(*decoder));
    handle = opus_decoder_create((opus_int32)sample_rate, (int)channels, &err);
    if (handle == NULL || err != OPUS_OK) {
        RIVER_LOGE("opus decoder create failed: err=%d", err);
        return RIVER_ERR_UNSUPPORTED;
    }

    decoder->handle = handle;
    decoder->sample_rate = sample_rate;
    decoder->channels = channels;
    decoder->frame_duration_ms = frame_duration_ms;
    decoder->pcm_frame_capacity_bytes = river_opus_pcm_bytes(sample_rate, channels, frame_duration_ms);
    return RIVER_OK;
}

void river_opus_decoder_close(river_opus_decoder_t *decoder)
{
    if (decoder == NULL) {
        return;
    }
    if (decoder->handle != NULL) {
        opus_decoder_destroy((OpusDecoder *)decoder->handle);
    }
    memset(decoder, 0, sizeof(*decoder));
}

river_status_t river_opus_decode(const river_opus_decoder_t *decoder,
                                 const uint8_t *packet,
                                 size_t packet_bytes,
                                 int16_t *pcm,
                                 size_t pcm_capacity_bytes,
                                 size_t *pcm_bytes_out)
{
    int decoded_samples;
    int max_frame_size_samples;

    if (pcm_bytes_out != NULL) {
        *pcm_bytes_out = 0U;
    }
    if (decoder == NULL || decoder->handle == NULL || packet == NULL || packet_bytes == 0U ||
        pcm == NULL || pcm_capacity_bytes < decoder->pcm_frame_capacity_bytes) {
        return RIVER_ERR_ARG;
    }

    max_frame_size_samples = (int)((decoder->sample_rate * decoder->frame_duration_ms) / 1000U);
    decoded_samples = opus_decode((OpusDecoder *)decoder->handle,
                                  packet,
                                  (opus_int32)packet_bytes,
                                  pcm,
                                  max_frame_size_samples,
                                  0);
    if (decoded_samples <= 0) {
        RIVER_LOGW("opus decode failed: ret=%d", decoded_samples);
        return RIVER_ERR_IO;
    }

    if (pcm_bytes_out != NULL) {
        *pcm_bytes_out = (size_t)decoded_samples *
                         (size_t)decoder->channels *
                         sizeof(int16_t);
    }
    return RIVER_OK;
}
