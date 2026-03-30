/* WebRTC AECM 适配层私有类型与接口。 */
#ifndef RIVER_VOICE_WEBRTC_AECM_ADAPTER_H
#define RIVER_VOICE_WEBRTC_AECM_ADAPTER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RIVER_VOICE_AECM_CHANNELS 2
#define RIVER_VOICE_AECM_FIFO_CAPACITY 2048
#define RIVER_VOICE_AECM_MAX_BLOCK_SAMPLES 160
#define RIVER_VOICE_AECM_REF_ACTIVITY_WINDOW_MAX 32

typedef enum {
	RIVER_VOICE_AECM_REF_STATE_MISSING = 0,
	RIVER_VOICE_AECM_REF_STATE_IDLE = 1,
	RIVER_VOICE_AECM_REF_STATE_ACTIVE = 2
} river_voice_webrtc_aecm_ref_state_t;

typedef struct river_voice_webrtc_aecm_ref_policy {
	uint16_t enter_peak;
	uint16_t exit_peak;
	uint16_t stable_frames;
	uint16_t hangover_frames;
	uint16_t active_window_frames;
} river_voice_webrtc_aecm_ref_policy_t;

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
	int16_t echo_mode;
	uint32_t frames_pushed;
	uint32_t frames_popped;
	uint32_t blocks_processed;
	uint32_t input_push_failures;
	uint32_t process_failures;
	uint32_t output_underruns;
	uint32_t resets;
	uint32_t samples_pushed;
	uint32_t samples_popped;
	uint32_t max_ref_fifo;
	uint32_t max_mic_in_fifo[RIVER_VOICE_AECM_CHANNELS];
	uint32_t max_mic_out_fifo[RIVER_VOICE_AECM_CHANNELS];
	river_voice_webrtc_aecm_ref_policy_t ref_policy;
	river_voice_webrtc_aecm_ref_state_t ref_state;
	uint16_t last_ref_peak;
	uint16_t ref_above_enter_streak;
	uint16_t ref_below_exit_streak;
	uint16_t ref_zero_streak;
	uint16_t ref_window_count;
	uint16_t ref_window_active_count;
	uint16_t ref_window_index;
	uint8_t ref_window_flags[RIVER_VOICE_AECM_REF_ACTIVITY_WINDOW_MAX];
	uint32_t ref_frames_seen;
	uint32_t ref_state_entered_active;
	uint32_t ref_state_exited_active;
	bool ready;
} river_voice_webrtc_aecm_adapter_t;

typedef struct river_voice_webrtc_aecm_adapter_stats {
	uint32_t frames_pushed;
	uint32_t frames_popped;
	uint32_t blocks_processed;
	uint32_t input_push_failures;
	uint32_t process_failures;
	uint32_t output_underruns;
	uint32_t resets;
	uint32_t samples_pushed;
	uint32_t samples_popped;
	uint32_t ref_fifo_depth;
	uint32_t mic_in_fifo_depth[RIVER_VOICE_AECM_CHANNELS];
	uint32_t mic_out_fifo_depth[RIVER_VOICE_AECM_CHANNELS];
	uint32_t max_ref_fifo;
	uint32_t max_mic_in_fifo[RIVER_VOICE_AECM_CHANNELS];
	uint32_t max_mic_out_fifo[RIVER_VOICE_AECM_CHANNELS];
	uint32_t primed_output;
	uint16_t last_ref_peak;
	uint16_t ref_active_ratio_q15;
	uint16_t ref_above_enter_streak;
	uint16_t ref_below_exit_streak;
	uint16_t ref_zero_streak;
	uint32_t ref_frames_seen;
	uint32_t ref_state_entered_active;
	uint32_t ref_state_exited_active;
	river_voice_webrtc_aecm_ref_state_t ref_state;
} river_voice_webrtc_aecm_adapter_stats_t;

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

void river_voice_webrtc_aecm_adapter_get_stats(const river_voice_webrtc_aecm_adapter_t *adapter,
                                               river_voice_webrtc_aecm_adapter_stats_t *stats);

void river_voice_webrtc_aecm_adapter_set_ref_policy(river_voice_webrtc_aecm_adapter_t *adapter,
                                                    const river_voice_webrtc_aecm_ref_policy_t *policy);

river_voice_webrtc_aecm_ref_state_t river_voice_webrtc_aecm_adapter_ref_state(
    const river_voice_webrtc_aecm_adapter_t *adapter);

const char *river_voice_webrtc_aecm_adapter_ref_state_name(
    river_voice_webrtc_aecm_ref_state_t state);

bool river_voice_webrtc_aecm_adapter_output_ready(const river_voice_webrtc_aecm_adapter_t *adapter);

/*
 * The adapter exposes a delayed-but-contiguous stream contract:
 * - input is accepted in main-pipeline frames, e.g. 256 samples
 * - internal processing runs in WebRTC AECM 10ms blocks, e.g. 160 samples
 * - output is only considered ready once at least one full main frame of
 *   processed contiguous samples has been accumulated
 *
 * In practice, for a 256-sample main frame and 160-sample AECM block, the
 * first full output frame becomes available after the second pushed frame.
 */

#ifdef __cplusplus
}
#endif

#endif /* RIVER_VOICE_WEBRTC_AECM_ADAPTER_H */
