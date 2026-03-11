#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "os_wrapper.h"

#include "river/river_voice_detector.h"

#define RIVER_SILERO_VAD_SAMPLE_RATE_HZ 16000U
#define RIVER_SILERO_VAD_FEED_SAMPLES 256U
#define RIVER_SILERO_VAD_WINDOW_SAMPLES 512U
#define RIVER_SILERO_VAD_CONTEXT_SAMPLES 64U
#define RIVER_SILERO_VAD_MODEL_INPUT_SAMPLES \
    (RIVER_SILERO_VAD_WINDOW_SAMPLES + RIVER_SILERO_VAD_CONTEXT_SAMPLES)

typedef struct {
    uint32_t frames_seen;
    uint32_t carried_samples;
    bool model_imported;
} river_voice_detector_silero_context_t;

river_status_t river_voice_detector_silero_open(river_voice_detector_t *detector)
{
    river_voice_detector_silero_context_t *context;

    if (detector == 0) {
        return RIVER_ERR_ARG;
    }
    if (detector->sample_rate != RIVER_SILERO_VAD_SAMPLE_RATE_HZ ||
        detector->input_frame_bytes != (RIVER_SILERO_VAD_FEED_SAMPLES * sizeof(int16_t))) {
        printf("[river][voice] silero_vad staged import expects 16kHz mono 256-sample frames\n");
        return RIVER_ERR_UNSUPPORTED;
    }

    context = (river_voice_detector_silero_context_t *)rtos_mem_zmalloc(sizeof(*context));
    if (context == 0) {
        return RIVER_ERR_NO_MEMORY;
    }

    detector->window_frame_bytes = RIVER_SILERO_VAD_WINDOW_SAMPLES * sizeof(int16_t);
    detector->backend_ctx = context;
    detector->staged_only = true;
    return RIVER_OK;
}

river_status_t river_voice_detector_silero_process(river_voice_detector_t *detector,
                                                   const uint8_t *input,
                                                   size_t input_bytes,
                                                   river_voice_detector_result_t *result)
{
    river_voice_detector_silero_context_t *context;

    if (detector == 0 || input == 0) {
        return RIVER_ERR_ARG;
    }
    if (input_bytes != detector->input_frame_bytes) {
        return RIVER_ERR_ARG;
    }

    context = (river_voice_detector_silero_context_t *)detector->backend_ctx;
    if (context == 0) {
        return RIVER_ERR_UNSUPPORTED;
    }

    context->frames_seen++;
    context->carried_samples += (uint32_t)(input_bytes / sizeof(int16_t));
    if (context->carried_samples >= RIVER_SILERO_VAD_WINDOW_SAMPLES) {
        context->carried_samples -= RIVER_SILERO_VAD_WINDOW_SAMPLES;
    }

    if (result != 0) {
        memset(result, 0, sizeof(*result));
        result->consumed_samples = RIVER_SILERO_VAD_FEED_SAMPLES;
    }

    return context->model_imported ? RIVER_OK : RIVER_ERR_UNSUPPORTED;
}

void river_voice_detector_silero_close(river_voice_detector_t *detector)
{
    river_voice_detector_silero_context_t *context;

    if (detector == 0) {
        return;
    }

    context = (river_voice_detector_silero_context_t *)detector->backend_ctx;
    if (context != 0) {
        rtos_mem_free(context);
    }
    detector->backend_ctx = 0;
}

void river_voice_detector_silero_dump_profile(void)
{
    printf("[river][voice] detector backend: silero_vad staged runtime=tflite_micro feed=256 samples window=512 samples context=64 samples model_input=576 samples model=silero_vad_16k_op15.onnx import=pending\n");
    printf("[river][voice] detector policy: migrate original model first, defer pruning/quantization until measured RAM/flash/latency pressure appears\n");
}
