#ifndef RIVER_VOICE_WEBRTC_AECM_ADAPTER_H
#define RIVER_VOICE_WEBRTC_AECM_ADAPTER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RIVER_VOICE_AECM_CHANNELS 2
#define RIVER_VOICE_AECM_FIFO_CAPACITY 640
#define RIVER_VOICE_AECM_MAX_BLOCK_SAMPLES 160

typedef struct {
	int16_t buffer[RIVER_VOICE_AECM_FIFO_CAPACITY];
	int head;
	int tail;
	int count;
} river_voice_aecm_sample_fifo_t;

typedef struct river_voice_webrtc_aecm_adapter {
	void *aec_instance[RIVER_VOICE_AECM_CHANNELS];
	river_voice_aecm_sample_fifo_t ref_in;
	river_voice_aecm_sample_fifo_t mic_in[RIVER_VOICE_AECM_CHANNELS];
	river_voice_aecm_sample_fifo_t mic_out[RIVER_VOICE_AECM_CHANNELS];
	int sample_rate;
	int frame_samples;
	int process_block_samples;
	int16_t ms_in_sndcard_buf;
	bool ready;
} river_voice_webrtc_aecm_adapter_t;

/**
 * @brief Initialize the AECM adapter.
 *
 * @param sample_rate       expected sample rate (8000 or 16000)
 * @param echo_mode         initial echo mode for WebRTC AECM
 * @param ms_in_sndcard_buf initial implementation delay hint
 * @param frame_samples     output frame size per channel (e.g., 256)
 */
bool river_voice_webrtc_aecm_adapter_init(river_voice_webrtc_aecm_adapter_t *adapter,
                                          int sample_rate,
                                          int16_t echo_mode,
                                          int16_t ms_in_sndcard_buf,
                                          int frame_samples);

void river_voice_webrtc_aecm_adapter_deinit(river_voice_webrtc_aecm_adapter_t *adapter);

/**
 * @brief Push an interleaved frame (mic0, mic1, ref, mic0, ...) into the adapter.
 *
 * @param frame_samples number of samples per channel in the frame
 * @param channels      number of interleaved channels (must be at least 3)
 */
bool river_voice_webrtc_aecm_adapter_push_frame(river_voice_webrtc_aecm_adapter_t *adapter,
                                                const int16_t *frames,
                                                int frame_samples,
                                                int channels);

/**
 * @brief Pop one ready frame of processed mic data.
 *
 * The caller must allocate two arrays sized to frame_samples.
 *
 * @return true when output arrays are filled, false otherwise.
 */
bool river_voice_webrtc_aecm_adapter_pop_frame(river_voice_webrtc_aecm_adapter_t *adapter,
                                               int16_t *out_mic0,
                                               int16_t *out_mic1);

/**
 * @brief Update the msInSndCardBuf parameter without reinitializing.
 */
void river_voice_webrtc_aecm_adapter_set_delay(river_voice_webrtc_aecm_adapter_t *adapter,
                                               int16_t ms_in_sndcard_buf);

void river_voice_webrtc_aecm_adapter_reset(river_voice_webrtc_aecm_adapter_t *adapter);

#ifdef __cplusplus
}
#endif

#endif /* RIVER_VOICE_WEBRTC_AECM_ADAPTER_H */
