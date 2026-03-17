#include "river_voice_webrtc_aecm_adapter.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "../../third_party/webrtc_aecm/aecm/echo_control_mobile.h"

#define RIVER_VOICE_AECM_REF_ENTER_PEAK_DEFAULT       128U
#define RIVER_VOICE_AECM_REF_EXIT_PEAK_DEFAULT        64U
#define RIVER_VOICE_AECM_REF_STABLE_FRAMES_DEFAULT    3U
#define RIVER_VOICE_AECM_REF_HANGOVER_FRAMES_DEFAULT  8U
#define RIVER_VOICE_AECM_REF_WINDOW_FRAMES_DEFAULT    8U

static void river_voice_aecm_sample_fifo_init(river_voice_aecm_sample_fifo_t *fifo)
{
	fifo->head = 0;
	fifo->tail = 0;
	fifo->count = 0;
}

static bool river_voice_aecm_sample_fifo_push(river_voice_aecm_sample_fifo_t *fifo, const int16_t *data, int count)
{
	if (fifo->count + count > RIVER_VOICE_AECM_FIFO_CAPACITY) {
		return false;
	}

	for (int i = 0; i < count; ++i) {
		fifo->buffer[fifo->tail] = data[i];
		fifo->tail = (fifo->tail + 1) % RIVER_VOICE_AECM_FIFO_CAPACITY;
	}

	fifo->count += count;
	return true;
}

static bool river_voice_aecm_sample_fifo_pop(river_voice_aecm_sample_fifo_t *fifo, int16_t *out, int count)
{
	if (fifo->count < count) {
		return false;
	}

	for (int i = 0; i < count; ++i) {
		out[i] = fifo->buffer[fifo->head];
		fifo->head = (fifo->head + 1) % RIVER_VOICE_AECM_FIFO_CAPACITY;
	}

	fifo->count -= count;
	return true;
}

static int river_voice_aecm_sample_fifo_available(const river_voice_aecm_sample_fifo_t *fifo)
{
	return fifo->count;
}

static uint16_t river_voice_webrtc_aecm_abs16(int value)
{
	if (value < 0) {
		value = -value;
	}
	if (value > 32767) {
		value = 32767;
	}
	return (uint16_t)value;
}

static uint16_t river_voice_webrtc_aecm_measure_ref_peak(const int16_t *frames, int frame_samples, int channels)
{
	uint16_t peak = 0;

	for (int i = 0; i < frame_samples; ++i) {
		uint16_t value = river_voice_webrtc_aecm_abs16(frames[(i * channels) + 2]);
		if (value > peak) {
			peak = value;
		}
	}
	return peak;
}

static void river_voice_webrtc_aecm_ref_policy_defaults(river_voice_webrtc_aecm_ref_policy_t *policy)
{
	policy->enter_peak = RIVER_VOICE_AECM_REF_ENTER_PEAK_DEFAULT;
	policy->exit_peak = RIVER_VOICE_AECM_REF_EXIT_PEAK_DEFAULT;
	policy->stable_frames = RIVER_VOICE_AECM_REF_STABLE_FRAMES_DEFAULT;
	policy->hangover_frames = RIVER_VOICE_AECM_REF_HANGOVER_FRAMES_DEFAULT;
	policy->active_window_frames = RIVER_VOICE_AECM_REF_WINDOW_FRAMES_DEFAULT;
}

static void river_voice_webrtc_aecm_ref_policy_normalize(river_voice_webrtc_aecm_ref_policy_t *policy)
{
	if (policy->enter_peak < policy->exit_peak) {
		policy->enter_peak = policy->exit_peak;
	}
	if (policy->stable_frames == 0U) {
		policy->stable_frames = 1U;
	}
	if (policy->hangover_frames == 0U) {
		policy->hangover_frames = 1U;
	}
	if (policy->active_window_frames == 0U) {
		policy->active_window_frames = 1U;
	}
	if (policy->active_window_frames > RIVER_VOICE_AECM_REF_ACTIVITY_WINDOW_MAX) {
		policy->active_window_frames = RIVER_VOICE_AECM_REF_ACTIVITY_WINDOW_MAX;
	}
}

static void river_voice_webrtc_aecm_ref_activity_reset(river_voice_webrtc_aecm_adapter_t *adapter)
{
	adapter->ref_state = RIVER_VOICE_AECM_REF_STATE_MISSING;
	adapter->last_ref_peak = 0U;
	adapter->ref_above_enter_streak = 0U;
	adapter->ref_below_exit_streak = 0U;
	adapter->ref_zero_streak = 0U;
	adapter->ref_window_count = 0U;
	adapter->ref_window_active_count = 0U;
	adapter->ref_window_index = 0U;
	memset(adapter->ref_window_flags, 0, sizeof(adapter->ref_window_flags));
}

static void river_voice_webrtc_aecm_ref_window_push(river_voice_webrtc_aecm_adapter_t *adapter, uint8_t active)
{
	uint16_t window = adapter->ref_policy.active_window_frames;

	if (adapter->ref_window_count < window) {
		adapter->ref_window_flags[adapter->ref_window_index] = active;
		adapter->ref_window_active_count += active;
		adapter->ref_window_count++;
		adapter->ref_window_index = (uint16_t)((adapter->ref_window_index + 1U) % window);
		return;
	}

	adapter->ref_window_active_count -= adapter->ref_window_flags[adapter->ref_window_index];
	adapter->ref_window_flags[adapter->ref_window_index] = active;
	adapter->ref_window_active_count += active;
	adapter->ref_window_index = (uint16_t)((adapter->ref_window_index + 1U) % window);
}

static void river_voice_webrtc_aecm_update_ref_state(river_voice_webrtc_aecm_adapter_t *adapter, uint16_t ref_peak)
{
	bool above_enter;
	bool below_exit;
	uint8_t active_sample;

	adapter->ref_frames_seen++;
	adapter->last_ref_peak = ref_peak;
	above_enter = ref_peak >= adapter->ref_policy.enter_peak;
	below_exit = ref_peak < adapter->ref_policy.exit_peak;
	active_sample = (uint8_t)(ref_peak >= adapter->ref_policy.exit_peak ? 1U : 0U);
	river_voice_webrtc_aecm_ref_window_push(adapter, active_sample);

	if (ref_peak == 0U) {
		if (adapter->ref_zero_streak < 0xFFFFU) {
			adapter->ref_zero_streak++;
		}
	} else {
		adapter->ref_zero_streak = 0U;
	}

	if (above_enter) {
		if (adapter->ref_above_enter_streak < 0xFFFFU) {
			adapter->ref_above_enter_streak++;
		}
	} else {
		adapter->ref_above_enter_streak = 0U;
	}

	if (below_exit) {
		if (adapter->ref_below_exit_streak < 0xFFFFU) {
			adapter->ref_below_exit_streak++;
		}
		} else {
			adapter->ref_below_exit_streak = 0U;
		}

	if (adapter->ref_zero_streak >= adapter->ref_policy.hangover_frames) {
		adapter->ref_state = RIVER_VOICE_AECM_REF_STATE_MISSING;
		adapter->ref_above_enter_streak = 0U;
		adapter->ref_below_exit_streak = 0U;
		return;
	}

	if (adapter->ref_state == RIVER_VOICE_AECM_REF_STATE_MISSING && ref_peak > 0U) {
		adapter->ref_state = RIVER_VOICE_AECM_REF_STATE_IDLE;
	}

	if (adapter->ref_state != RIVER_VOICE_AECM_REF_STATE_ACTIVE) {
		if (adapter->ref_above_enter_streak >= adapter->ref_policy.stable_frames) {
			adapter->ref_state = RIVER_VOICE_AECM_REF_STATE_ACTIVE;
			adapter->ref_state_entered_active++;
			adapter->ref_below_exit_streak = 0U;
		}
		return;
	}

	if (adapter->ref_below_exit_streak >= adapter->ref_policy.hangover_frames) {
		adapter->ref_state = RIVER_VOICE_AECM_REF_STATE_IDLE;
		adapter->ref_state_exited_active++;
		adapter->ref_above_enter_streak = 0U;
	}
}

static bool river_voice_webrtc_aecm_reinit_instances(river_voice_webrtc_aecm_adapter_t *adapter)
{
	for (int idx = 0; idx < RIVER_VOICE_AECM_CHANNELS; ++idx) {
		AecmConfig config;

		if (!adapter->aec_instance[idx]) {
			return false;
		}
		if (WebRtcAecm_Init(adapter->aec_instance[idx], adapter->sample_rate) != 0) {
			return false;
		}
		config.cngMode = AecmTrue;
		config.echoMode = adapter->echo_mode;
		if (WebRtcAecm_set_config(adapter->aec_instance[idx], config) != 0) {
			return false;
		}
	}

	return true;
}

static void river_voice_webrtc_aecm_adapter_update_max_fifo(river_voice_webrtc_aecm_adapter_t *adapter)
{
	uint32_t ref_depth = (uint32_t)river_voice_aecm_sample_fifo_available(&adapter->ref_in);
	uint32_t mic0_in_depth = (uint32_t)river_voice_aecm_sample_fifo_available(&adapter->mic_in[0]);
	uint32_t mic1_in_depth = (uint32_t)river_voice_aecm_sample_fifo_available(&adapter->mic_in[1]);
	uint32_t mic0_out_depth = (uint32_t)river_voice_aecm_sample_fifo_available(&adapter->mic_out[0]);
	uint32_t mic1_out_depth = (uint32_t)river_voice_aecm_sample_fifo_available(&adapter->mic_out[1]);

	if (ref_depth > adapter->max_ref_fifo) {
		adapter->max_ref_fifo = ref_depth;
	}
	if (mic0_in_depth > adapter->max_mic_in_fifo[0]) {
		adapter->max_mic_in_fifo[0] = mic0_in_depth;
	}
	if (mic1_in_depth > adapter->max_mic_in_fifo[1]) {
		adapter->max_mic_in_fifo[1] = mic1_in_depth;
	}
	if (mic0_out_depth > adapter->max_mic_out_fifo[0]) {
		adapter->max_mic_out_fifo[0] = mic0_out_depth;
	}
	if (mic1_out_depth > adapter->max_mic_out_fifo[1]) {
		adapter->max_mic_out_fifo[1] = mic1_out_depth;
	}
}

static bool river_voice_webrtc_aecm_adapter_process_blocks(river_voice_webrtc_aecm_adapter_t *adapter)
{
	int block = adapter->process_block_samples;
	int16_t ref_block[RIVER_VOICE_AECM_MAX_BLOCK_SAMPLES];
	int16_t mic_block[RIVER_VOICE_AECM_CHANNELS][RIVER_VOICE_AECM_MAX_BLOCK_SAMPLES];
	int16_t out_block[RIVER_VOICE_AECM_CHANNELS][RIVER_VOICE_AECM_MAX_BLOCK_SAMPLES];

	while (river_voice_aecm_sample_fifo_available(&adapter->ref_in) >= block &&
	       river_voice_aecm_sample_fifo_available(&adapter->mic_in[0]) >= block &&
	       river_voice_aecm_sample_fifo_available(&adapter->mic_in[1]) >= block) {
		if (!river_voice_aecm_sample_fifo_pop(&adapter->ref_in, ref_block, block) ||
		    !river_voice_aecm_sample_fifo_pop(&adapter->mic_in[0], mic_block[0], block) ||
		    !river_voice_aecm_sample_fifo_pop(&adapter->mic_in[1], mic_block[1], block)) {
			break;
		}

		for (int idx = 0; idx < RIVER_VOICE_AECM_CHANNELS; ++idx) {
			if (adapter->aec_instance[idx]) {
				if (WebRtcAecm_BufferFarend(adapter->aec_instance[idx], ref_block, block) != 0) {
					adapter->process_failures++;
					return false;
				}
			}
		}

		for (int idx = 0; idx < RIVER_VOICE_AECM_CHANNELS; ++idx) {
			if (adapter->aec_instance[idx]) {
				int status = WebRtcAecm_Process(adapter->aec_instance[idx],
				                                mic_block[idx],
				                                NULL,
				                                out_block[idx],
				                                block,
				                                adapter->ms_in_sndcard_buf);
				if (status != 0) {
					adapter->process_failures++;
					return false;
				}
				if (!river_voice_aecm_sample_fifo_push(&adapter->mic_out[idx], out_block[idx], block)) {
					adapter->process_failures++;
					return false;
				}
			}
		}

		adapter->blocks_processed++;
		river_voice_webrtc_aecm_adapter_update_max_fifo(adapter);
	}

	return true;
}

bool river_voice_webrtc_aecm_adapter_init(river_voice_webrtc_aecm_adapter_t *adapter,
                                          int sample_rate,
                                          int16_t echo_mode,
                                          int16_t ms_in_sndcard_buf,
                                          int frame_samples)
{
	if (!adapter) {
		return false;
	}

	memset(adapter, 0, sizeof(*adapter));
	adapter->sample_rate = sample_rate;
	adapter->frame_samples = frame_samples;
	adapter->process_block_samples = sample_rate / 100;
	adapter->ms_in_sndcard_buf = ms_in_sndcard_buf;
	adapter->echo_mode = echo_mode;
	river_voice_webrtc_aecm_ref_policy_defaults(&adapter->ref_policy);
	river_voice_webrtc_aecm_ref_policy_normalize(&adapter->ref_policy);
	river_voice_webrtc_aecm_ref_activity_reset(adapter);
	if (adapter->process_block_samples <= 0 ||
	    adapter->process_block_samples > RIVER_VOICE_AECM_MAX_BLOCK_SAMPLES) {
		return false;
	}

	int last_created = -1;
	for (int idx = 0; idx < RIVER_VOICE_AECM_CHANNELS; ++idx) {
		adapter->aec_instance[idx] = WebRtcAecm_Create();
		if (!adapter->aec_instance[idx]) {
			break;
		}

		if (WebRtcAecm_Init(adapter->aec_instance[idx], sample_rate) != 0) {
			break;
		}

			AecmConfig config;
			config.cngMode = AecmTrue;
			config.echoMode = echo_mode;
		if (WebRtcAecm_set_config(adapter->aec_instance[idx], config) != 0) {
			break;
		}

		last_created = idx;
	}

	if (last_created != (RIVER_VOICE_AECM_CHANNELS - 1)) {
		river_voice_webrtc_aecm_adapter_deinit(adapter);
		return false;
	}

	river_voice_aecm_sample_fifo_init(&adapter->ref_in);
	river_voice_aecm_sample_fifo_init(&adapter->mic_in[0]);
	river_voice_aecm_sample_fifo_init(&adapter->mic_in[1]);
	river_voice_aecm_sample_fifo_init(&adapter->mic_out[0]);
	river_voice_aecm_sample_fifo_init(&adapter->mic_out[1]);
	river_voice_webrtc_aecm_adapter_update_max_fifo(adapter);

	adapter->ready = true;
	return true;
}

void river_voice_webrtc_aecm_adapter_deinit(river_voice_webrtc_aecm_adapter_t *adapter)
{
	if (!adapter) {
		return;
	}

	for (int idx = 0; idx < RIVER_VOICE_AECM_CHANNELS; ++idx) {
		if (adapter->aec_instance[idx]) {
			WebRtcAecm_Free(adapter->aec_instance[idx]);
			adapter->aec_instance[idx] = NULL;
		}
	}

	adapter->ready = false;
}

bool river_voice_webrtc_aecm_adapter_push_frame(river_voice_webrtc_aecm_adapter_t *adapter,
                                                const int16_t *frames,
                                                int frame_samples,
                                                int channels)
{
	uint16_t ref_peak;

	if (!adapter || !adapter->ready || !frames || channels < 3) {
		return false;
	}

	ref_peak = river_voice_webrtc_aecm_measure_ref_peak(frames, frame_samples, channels);
	river_voice_webrtc_aecm_update_ref_state(adapter, ref_peak);

	for (int i = 0; i < frame_samples; ++i) {
		int base = i * channels;
		int16_t mic0 = frames[base + 0];
		int16_t mic1 = frames[base + 1];
		int16_t ref = frames[base + 2];

		if (!river_voice_aecm_sample_fifo_push(&adapter->mic_in[0], &mic0, 1) ||
		    !river_voice_aecm_sample_fifo_push(&adapter->mic_in[1], &mic1, 1) ||
		    !river_voice_aecm_sample_fifo_push(&adapter->ref_in, &ref, 1)) {
			adapter->input_push_failures++;
			return false;
		}
	}

	adapter->frames_pushed++;
	adapter->samples_pushed += (uint32_t)frame_samples;
	river_voice_webrtc_aecm_adapter_update_max_fifo(adapter);

	if (!river_voice_webrtc_aecm_adapter_process_blocks(adapter)) {
		return false;
	}

	return true;
}

bool river_voice_webrtc_aecm_adapter_pop_frame(river_voice_webrtc_aecm_adapter_t *adapter,
                                               int16_t *out_mic0,
                                               int16_t *out_mic1)
{
	if (!adapter || !adapter->ready) {
		return false;
	}

	int required = adapter->frame_samples;
	if (river_voice_aecm_sample_fifo_available(&adapter->mic_out[0]) < required ||
	    river_voice_aecm_sample_fifo_available(&adapter->mic_out[1]) < required) {
		adapter->output_underruns++;
		return false;
	}

	if (!river_voice_aecm_sample_fifo_pop(&adapter->mic_out[0], out_mic0, required) ||
	    !river_voice_aecm_sample_fifo_pop(&adapter->mic_out[1], out_mic1, required)) {
		adapter->output_underruns++;
		return false;
	}

	adapter->frames_popped++;
	adapter->samples_popped += (uint32_t)required;
	river_voice_webrtc_aecm_adapter_update_max_fifo(adapter);

	return true;
}

void river_voice_webrtc_aecm_adapter_set_delay(river_voice_webrtc_aecm_adapter_t *adapter,
                                               int16_t ms_in_sndcard_buf)
{
	if (!adapter) {
		return;
	}
	adapter->ms_in_sndcard_buf = ms_in_sndcard_buf;
}

void river_voice_webrtc_aecm_adapter_reset(river_voice_webrtc_aecm_adapter_t *adapter)
{
	if (!adapter) {
		return;
	}

	river_voice_aecm_sample_fifo_init(&adapter->ref_in);
	river_voice_aecm_sample_fifo_init(&adapter->mic_in[0]);
	river_voice_aecm_sample_fifo_init(&adapter->mic_in[1]);
	river_voice_aecm_sample_fifo_init(&adapter->mic_out[0]);
	river_voice_aecm_sample_fifo_init(&adapter->mic_out[1]);
	river_voice_webrtc_aecm_ref_activity_reset(adapter);
	adapter->ready = river_voice_webrtc_aecm_reinit_instances(adapter);
	adapter->resets++;
	river_voice_webrtc_aecm_adapter_update_max_fifo(adapter);
}

void river_voice_webrtc_aecm_adapter_get_stats(const river_voice_webrtc_aecm_adapter_t *adapter,
                                               river_voice_webrtc_aecm_adapter_stats_t *stats)
{
	if (!adapter || !stats) {
		return;
	}

	memset(stats, 0, sizeof(*stats));
	stats->frames_pushed = adapter->frames_pushed;
	stats->frames_popped = adapter->frames_popped;
	stats->blocks_processed = adapter->blocks_processed;
	stats->input_push_failures = adapter->input_push_failures;
	stats->process_failures = adapter->process_failures;
	stats->output_underruns = adapter->output_underruns;
	stats->resets = adapter->resets;
	stats->samples_pushed = adapter->samples_pushed;
	stats->samples_popped = adapter->samples_popped;
	stats->ref_fifo_depth = (uint32_t)river_voice_aecm_sample_fifo_available(&adapter->ref_in);
	stats->mic_in_fifo_depth[0] = (uint32_t)river_voice_aecm_sample_fifo_available(&adapter->mic_in[0]);
	stats->mic_in_fifo_depth[1] = (uint32_t)river_voice_aecm_sample_fifo_available(&adapter->mic_in[1]);
	stats->mic_out_fifo_depth[0] = (uint32_t)river_voice_aecm_sample_fifo_available(&adapter->mic_out[0]);
	stats->mic_out_fifo_depth[1] = (uint32_t)river_voice_aecm_sample_fifo_available(&adapter->mic_out[1]);
	stats->max_ref_fifo = adapter->max_ref_fifo;
	stats->max_mic_in_fifo[0] = adapter->max_mic_in_fifo[0];
	stats->max_mic_in_fifo[1] = adapter->max_mic_in_fifo[1];
	stats->max_mic_out_fifo[0] = adapter->max_mic_out_fifo[0];
	stats->max_mic_out_fifo[1] = adapter->max_mic_out_fifo[1];
	stats->primed_output = river_voice_webrtc_aecm_adapter_output_ready(adapter) ? 1U : 0U;
	stats->last_ref_peak = adapter->last_ref_peak;
	stats->ref_active_ratio_q15 = adapter->ref_window_count == 0U
	                                  ? 0U
	                                  : (uint16_t)((adapter->ref_window_active_count * 32767U) /
	                                               adapter->ref_window_count);
	stats->ref_above_enter_streak = adapter->ref_above_enter_streak;
	stats->ref_below_exit_streak = adapter->ref_below_exit_streak;
	stats->ref_zero_streak = adapter->ref_zero_streak;
	stats->ref_frames_seen = adapter->ref_frames_seen;
	stats->ref_state_entered_active = adapter->ref_state_entered_active;
	stats->ref_state_exited_active = adapter->ref_state_exited_active;
	stats->ref_state = adapter->ref_state;
}

void river_voice_webrtc_aecm_adapter_set_ref_policy(river_voice_webrtc_aecm_adapter_t *adapter,
                                                    const river_voice_webrtc_aecm_ref_policy_t *policy)
{
	if (!adapter || !policy) {
		return;
	}

	adapter->ref_policy = *policy;
	river_voice_webrtc_aecm_ref_policy_normalize(&adapter->ref_policy);
	river_voice_webrtc_aecm_ref_activity_reset(adapter);
}

river_voice_webrtc_aecm_ref_state_t river_voice_webrtc_aecm_adapter_ref_state(
    const river_voice_webrtc_aecm_adapter_t *adapter)
{
	if (!adapter) {
		return RIVER_VOICE_AECM_REF_STATE_MISSING;
	}

	return adapter->ref_state;
}

const char *river_voice_webrtc_aecm_adapter_ref_state_name(
    river_voice_webrtc_aecm_ref_state_t state)
{
	switch (state) {
	case RIVER_VOICE_AECM_REF_STATE_MISSING:
		return "missing";
	case RIVER_VOICE_AECM_REF_STATE_IDLE:
		return "idle";
	case RIVER_VOICE_AECM_REF_STATE_ACTIVE:
		return "active";
	default:
		return "unknown";
	}
}

bool river_voice_webrtc_aecm_adapter_output_ready(const river_voice_webrtc_aecm_adapter_t *adapter)
{
	if (!adapter || !adapter->ready) {
		return false;
	}

	return river_voice_aecm_sample_fifo_available(&adapter->mic_out[0]) >= adapter->frame_samples &&
	       river_voice_aecm_sample_fifo_available(&adapter->mic_out[1]) >= adapter->frame_samples;
}
