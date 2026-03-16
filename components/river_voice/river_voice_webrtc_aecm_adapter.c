#include "river_voice_webrtc_aecm_adapter.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "../../third_party/webrtc_aecm/aecm/echo_control_mobile.h"

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
					return false;
				}
				if (!river_voice_aecm_sample_fifo_push(&adapter->mic_out[idx], out_block[idx], block)) {
					return false;
				}
			}
		}
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
	if (!adapter || !adapter->ready || !frames || channels < 3) {
		return false;
	}

	for (int i = 0; i < frame_samples; ++i) {
		int base = i * channels;
		int16_t mic0 = frames[base + 0];
		int16_t mic1 = frames[base + 1];
		int16_t ref = frames[base + 2];

		if (!river_voice_aecm_sample_fifo_push(&adapter->mic_in[0], &mic0, 1) ||
		    !river_voice_aecm_sample_fifo_push(&adapter->mic_in[1], &mic1, 1) ||
		    !river_voice_aecm_sample_fifo_push(&adapter->ref_in, &ref, 1)) {
			return false;
		}
	}

	if (!river_voice_webrtc_aecm_adapter_process_blocks(adapter)) {
		river_voice_webrtc_aecm_adapter_reset(adapter);
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
		return false;
	}

	if (!river_voice_aecm_sample_fifo_pop(&adapter->mic_out[0], out_mic0, required) ||
	    !river_voice_aecm_sample_fifo_pop(&adapter->mic_out[1], out_mic1, required)) {
		return false;
	}

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
}
