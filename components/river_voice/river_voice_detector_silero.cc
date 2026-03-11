#include <new>

#include "basic_types.h"

#ifndef TFLITE_WITH_STABLE_ABI
#define TFLITE_WITH_STABLE_ABI 0
#endif

#ifndef TFLITE_USE_OPAQUE_DELEGATE
#define TFLITE_USE_OPAQUE_DELEGATE 0
#endif

#ifndef TFLITE_SINGLE_ROUNDING
#define TFLITE_SINGLE_ROUNDING 0
#endif

extern "C" {
#include "os_wrapper.h"
#include "river/river_voice_detector.h"
}

#include "generated/river_silero_vad_model_data.h"

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/memory_helpers.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#ifndef CONFIG_RIVER_SILERO_VAD_TENSOR_ARENA_KB
#define CONFIG_RIVER_SILERO_VAD_TENSOR_ARENA_KB 256
#endif

#ifndef CONFIG_RIVER_SILERO_VAD_SPEECH_THRESHOLD_Q15
#define CONFIG_RIVER_SILERO_VAD_SPEECH_THRESHOLD_Q15 16384
#endif

#define RIVER_SILERO_VAD_SAMPLE_RATE_HZ 16000U
#define RIVER_SILERO_VAD_FEED_SAMPLES 256U
#define RIVER_SILERO_VAD_WINDOW_SAMPLES 512U
#define RIVER_SILERO_VAD_CONTEXT_SAMPLES 64U
#define RIVER_SILERO_VAD_MODEL_INPUT_SAMPLES \
    (RIVER_SILERO_VAD_WINDOW_SAMPLES + RIVER_SILERO_VAD_CONTEXT_SAMPLES)
#define RIVER_SILERO_VAD_STATE_FLOATS 256U
#define RIVER_SILERO_VAD_OP_COUNT 16U
#define RIVER_SILERO_VAD_ARENA_BYTES \
    ((uint32_t)CONFIG_RIVER_SILERO_VAD_TENSOR_ARENA_KB * 1024U)

typedef tflite::MicroMutableOpResolver<RIVER_SILERO_VAD_OP_COUNT>
    river_silero_vad_op_resolver_t;

typedef struct {
    uint32_t frames_seen;
    uint32_t decisions_made;
    uint32_t speech_decisions;
    uint32_t invoke_failures;
    uint32_t carried_samples;
    uint32_t arena_size_bytes;
    uint32_t arena_used_bytes;
    bool model_imported;
    bool op_resolver_constructed;
    bool tensor_arena_from_heap_types;
    int16_t pending_window[RIVER_SILERO_VAD_WINDOW_SAMPLES];
    int16_t context_window[RIVER_SILERO_VAD_CONTEXT_SAMPLES];
    float recurrent_state[RIVER_SILERO_VAD_STATE_FLOATS];
    float next_state[RIVER_SILERO_VAD_STATE_FLOATS];
    float audio_input_buffer[RIVER_SILERO_VAD_MODEL_INPUT_SAMPLES];
    float probability_output_buffer[1];
    uint8_t *tensor_arena;
    river_silero_vad_op_resolver_t op_resolver;
    const tflite::Model *model;
    alignas(alignof(tflite::MicroInterpreter)) uint8_t interpreter_storage[sizeof(tflite::MicroInterpreter)];
    tflite::MicroInterpreter *interpreter;
    TfLiteEvalTensor *audio_input_eval_tensor;
    TfLiteEvalTensor *state_input_eval_tensor;
    TfLiteEvalTensor *prob_output_eval_tensor;
    TfLiteEvalTensor *state_output_eval_tensor;
    TfLiteTensor *audio_input_tensor;
    TfLiteTensor *state_input_tensor;
    TfLiteTensor *prob_output_tensor;
    TfLiteTensor *state_output_tensor;
} river_voice_detector_silero_context_t;

static bool river_silero_vad_tensor_shape_matches(const TfLiteTensor *tensor,
                                                  int dims_size,
                                                  int dim0,
                                                  int dim1,
                                                  int dim2)
{
    if (tensor == NULL || tensor->dims == NULL || tensor->dims->size != dims_size) {
        return false;
    }
    if (tensor->dims->data[0] != dim0 || tensor->dims->data[1] != dim1) {
        return false;
    }
    if (dims_size == 3 && tensor->dims->data[2] != dim2) {
        return false;
    }
    return true;
}

static void river_silero_vad_dump_tensor(const char *prefix,
                                         size_t index,
                                         const TfLiteTensor *tensor)
{
    int dim0 = -1;
    int dim1 = -1;
    int dim2 = -1;
    int dims_size = -1;

    if (tensor != NULL && tensor->dims != NULL) {
        dims_size = tensor->dims->size;
        if (dims_size > 0) {
            dim0 = tensor->dims->data[0];
        }
        if (dims_size > 1) {
            dim1 = tensor->dims->data[1];
        }
        if (dims_size > 2) {
            dim2 = tensor->dims->data[2];
        }
    }

    printf("[river][voice] silero_vad %s[%lu]: ptr=%p type=%d dims=%d [%d,%d,%d] name=%s\n",
           prefix,
           (unsigned long)index,
           (const void *)tensor,
           tensor != NULL ? tensor->type : -1,
           dims_size,
           dim0,
           dim1,
           dim2,
           (tensor != NULL && tensor->name != NULL) ? tensor->name : "(null)");
    if (tensor != NULL) {
        printf("[river][voice] silero_vad %s[%lu] data=%p bytes=%lu\n",
               prefix,
               (unsigned long)index,
               (const void *)tensor->data.data,
               (unsigned long)tensor->bytes);
    }
}

static void river_silero_vad_dump_interpreter_io(tflite::MicroInterpreter *interpreter)
{
    size_t index;

    if (interpreter == NULL) {
        return;
    }

    printf("[river][voice] silero_vad interpreter io: inputs=%lu outputs=%lu\n",
           (unsigned long)interpreter->inputs_size(),
           (unsigned long)interpreter->outputs_size());

    for (index = 0; index < interpreter->inputs_size(); ++index) {
        river_silero_vad_dump_tensor("input", index, interpreter->input(index));
    }

    for (index = 0; index < interpreter->outputs_size(); ++index) {
        river_silero_vad_dump_tensor("output", index, interpreter->output(index));
    }
}

static TfLiteTensor *river_silero_vad_find_input_tensor(tflite::MicroInterpreter *interpreter,
                                                        int dims_size,
                                                        int dim0,
                                                        int dim1,
                                                        int dim2)
{
    size_t index;

    if (interpreter == NULL) {
        return NULL;
    }

    for (index = 0; index < interpreter->inputs_size(); ++index) {
        TfLiteTensor *tensor = interpreter->input(index);
        if (river_silero_vad_tensor_shape_matches(tensor, dims_size, dim0, dim1, dim2)) {
            return tensor;
        }
    }

    return NULL;
}

static TfLiteTensor *river_silero_vad_find_output_tensor(tflite::MicroInterpreter *interpreter,
                                                         int dims_size,
                                                         int dim0,
                                                         int dim1,
                                                         int dim2)
{
    size_t index;

    if (interpreter == NULL) {
        return NULL;
    }

    for (index = 0; index < interpreter->outputs_size(); ++index) {
        TfLiteTensor *tensor = interpreter->output(index);
        if (river_silero_vad_tensor_shape_matches(tensor, dims_size, dim0, dim1, dim2)) {
            return tensor;
        }
    }

    return NULL;
}

static river_status_t river_silero_vad_register_ops(river_silero_vad_op_resolver_t *resolver)
{
    if (resolver == NULL) {
        return RIVER_ERR_ARG;
    }
    if (resolver->AddReshape() != kTfLiteOk ||
        resolver->AddMirrorPad() != kTfLiteOk ||
        resolver->AddConv2D() != kTfLiteOk ||
        resolver->AddStridedSlice() != kTfLiteOk ||
        resolver->AddSquare() != kTfLiteOk ||
        resolver->AddAdd() != kTfLiteOk ||
        resolver->AddSqrt() != kTfLiteOk ||
        resolver->AddPad() != kTfLiteOk ||
        resolver->AddFullyConnected() != kTfLiteOk ||
        resolver->AddSplit() != kTfLiteOk ||
        resolver->AddLogistic() != kTfLiteOk ||
        resolver->AddMul() != kTfLiteOk ||
        resolver->AddTanh() != kTfLiteOk ||
        resolver->AddRelu() != kTfLiteOk ||
        resolver->AddMean() != kTfLiteOk ||
        resolver->AddPack() != kTfLiteOk) {
        return RIVER_ERR_UNSUPPORTED;
    }
    return RIVER_OK;
}

static void river_silero_vad_prepare_input(const int16_t *context_window,
                                           const int16_t *pending_window,
                                           float *audio_input)
{
    uint32_t index;

    for (index = 0; index < RIVER_SILERO_VAD_CONTEXT_SAMPLES; ++index) {
        audio_input[index] = (float)context_window[index] / 32768.0f;
    }
    for (index = 0; index < RIVER_SILERO_VAD_WINDOW_SAMPLES; ++index) {
        audio_input[RIVER_SILERO_VAD_CONTEXT_SAMPLES + index] =
            (float)pending_window[index] / 32768.0f;
    }
}

static void river_silero_vad_free_tensor_arena(river_voice_detector_silero_context_t *context)
{
    if (context == NULL || context->tensor_arena == NULL) {
        return;
    }

    if (context->tensor_arena_from_heap_types) {
        rtos_heap_types_free(context->tensor_arena);
    } else {
        rtos_mem_free(context->tensor_arena);
    }

    context->tensor_arena = NULL;
    context->tensor_arena_from_heap_types = false;
}

static bool river_silero_vad_tensor_buffer_ready(const TfLiteTensor *tensor, size_t min_bytes)
{
    if (tensor == NULL || tensor->type != kTfLiteFloat32 || tensor->data.data == NULL) {
        return false;
    }

    if (tensor->bytes != 0 && tensor->bytes < min_bytes) {
        return false;
    }

    return true;
}

static bool river_silero_vad_eval_tensor_buffer_ready(const TfLiteEvalTensor *tensor,
                                                      size_t min_bytes)
{
    size_t tensor_bytes = 0;

    if (tensor == NULL || tensor->type != kTfLiteFloat32 || tensor->data.data == NULL) {
        return false;
    }

    if (tensor->dims != NULL &&
        tflite::TfLiteEvalTensorByteLength(tensor, &tensor_bytes) == kTfLiteOk) {
        if (tensor_bytes < min_bytes) {
            return false;
        }
    }

    return true;
}

static void river_silero_vad_patch_tensor_buffer(TfLiteTensor *tensor,
                                                 TfLiteEvalTensor *eval_tensor,
                                                 void *buffer)
{
    if (eval_tensor != NULL && eval_tensor->data.data == NULL) {
        eval_tensor->data.data = buffer;
    }

    if (tensor != NULL && tensor->data.data == NULL) {
        if (eval_tensor != NULL && eval_tensor->data.data != NULL) {
            tensor->data.data = eval_tensor->data.data;
        } else {
            tensor->data.data = buffer;
        }
    }

    if (tensor != NULL && tensor->dims == NULL && eval_tensor != NULL && eval_tensor->dims != NULL) {
        tensor->dims = eval_tensor->dims;
    }
}

extern "C" river_status_t river_voice_detector_silero_open(river_voice_detector_t *detector)
{
    river_voice_detector_silero_context_t *context;
    river_status_t status;

    if (detector == NULL) {
        return RIVER_ERR_ARG;
    }
    if (detector->sample_rate != RIVER_SILERO_VAD_SAMPLE_RATE_HZ ||
        detector->input_frame_bytes != (RIVER_SILERO_VAD_FEED_SAMPLES * sizeof(int16_t))) {
        printf("[river][voice] silero_vad expects 16kHz mono 256-sample frames\n");
        return RIVER_ERR_UNSUPPORTED;
    }

    context = (river_voice_detector_silero_context_t *)rtos_mem_zmalloc(sizeof(*context));
    if (context == NULL) {
        return RIVER_ERR_NO_MEMORY;
    }

    context->arena_size_bytes = RIVER_SILERO_VAD_ARENA_BYTES;
    context->tensor_arena =
        (uint8_t *)rtos_heap_types_zmalloc(context->arena_size_bytes, TYPE_DRAM);
    context->tensor_arena_from_heap_types = (context->tensor_arena != NULL);
    if (context->tensor_arena == NULL) {
        context->tensor_arena = (uint8_t *)rtos_mem_zmalloc(context->arena_size_bytes);
    }
    if (context->tensor_arena == NULL) {
        rtos_mem_free(context);
        return RIVER_ERR_NO_MEMORY;
    }

    context->model = tflite::GetModel(g_river_silero_vad_model_data);
    if (context->model == NULL || context->model->version() != TFLITE_SCHEMA_VERSION) {
        river_silero_vad_free_tensor_arena(context);
        rtos_mem_free(context);
        printf("[river][voice] silero_vad model schema mismatch\n");
        return RIVER_ERR_UNSUPPORTED;
    }

    new (&context->op_resolver) river_silero_vad_op_resolver_t();
    context->op_resolver_constructed = true;

    status = river_silero_vad_register_ops(&context->op_resolver);
    if (status != RIVER_OK) {
        context->op_resolver.~river_silero_vad_op_resolver_t();
        river_silero_vad_free_tensor_arena(context);
        rtos_mem_free(context);
        printf("[river][voice] silero_vad op registration failed\n");
        return status;
    }

    context->interpreter = new (context->interpreter_storage)
        tflite::MicroInterpreter(context->model,
                                 context->op_resolver,
                                 context->tensor_arena,
                                 context->arena_size_bytes,
                                 NULL,
                                 NULL,
                                 true);
    if (context->interpreter->AllocateTensors() != kTfLiteOk) {
        context->interpreter->~MicroInterpreter();
        context->op_resolver.~river_silero_vad_op_resolver_t();
        river_silero_vad_free_tensor_arena(context);
        rtos_mem_free(context);
        printf("[river][voice] silero_vad AllocateTensors failed\n");
        return RIVER_ERR_NO_MEMORY;
    }

    if (context->interpreter->inputs_size() >= 2) {
        context->state_input_tensor = context->interpreter->input(0);
        context->audio_input_tensor = context->interpreter->input(1);
    } else {
        context->audio_input_tensor =
            river_silero_vad_find_input_tensor(context->interpreter, 2, 1, 576, 0);
        context->state_input_tensor =
            river_silero_vad_find_input_tensor(context->interpreter, 3, 2, 1, 128);
    }

    if (context->interpreter->outputs_size() >= 2) {
        context->prob_output_tensor = context->interpreter->output(0);
        context->state_output_tensor = context->interpreter->output(1);
    } else {
        context->prob_output_tensor =
            river_silero_vad_find_output_tensor(context->interpreter, 2, 1, 1, 0);
        context->state_output_tensor =
            river_silero_vad_find_output_tensor(context->interpreter, 3, 2, 1, 128);
    }

    if (context->interpreter->inputs_size() >= 2) {
        context->state_input_eval_tensor =
            context->interpreter->GetTensor(context->interpreter->inputs().Get(0));
        context->audio_input_eval_tensor =
            context->interpreter->GetTensor(context->interpreter->inputs().Get(1));
    }
    if (context->interpreter->outputs_size() >= 2) {
        context->prob_output_eval_tensor =
            context->interpreter->GetTensor(context->interpreter->outputs().Get(0));
        context->state_output_eval_tensor =
            context->interpreter->GetTensor(context->interpreter->outputs().Get(1));
    }

    river_silero_vad_patch_tensor_buffer(context->audio_input_tensor,
                                         context->audio_input_eval_tensor,
                                         context->audio_input_buffer);
    river_silero_vad_patch_tensor_buffer(context->state_input_tensor,
                                         context->state_input_eval_tensor,
                                         context->recurrent_state);
    river_silero_vad_patch_tensor_buffer(context->prob_output_tensor,
                                         context->prob_output_eval_tensor,
                                         context->probability_output_buffer);
    river_silero_vad_patch_tensor_buffer(context->state_output_tensor,
                                         context->state_output_eval_tensor,
                                         context->next_state);

    if (context->audio_input_tensor == NULL ||
        context->state_input_tensor == NULL ||
        context->prob_output_tensor == NULL ||
        context->state_output_tensor == NULL ||
        context->audio_input_eval_tensor == NULL ||
        context->state_input_eval_tensor == NULL ||
        context->prob_output_eval_tensor == NULL ||
        context->state_output_eval_tensor == NULL ||
        !river_silero_vad_tensor_buffer_ready(
            context->audio_input_tensor,
            RIVER_SILERO_VAD_MODEL_INPUT_SAMPLES * sizeof(float)) ||
        !river_silero_vad_tensor_buffer_ready(
            context->state_input_tensor,
            RIVER_SILERO_VAD_STATE_FLOATS * sizeof(float)) ||
        !river_silero_vad_tensor_buffer_ready(
            context->prob_output_tensor,
            sizeof(float)) ||
        !river_silero_vad_tensor_buffer_ready(
            context->state_output_tensor,
            RIVER_SILERO_VAD_STATE_FLOATS * sizeof(float)) ||
        !river_silero_vad_eval_tensor_buffer_ready(
            context->audio_input_eval_tensor,
            RIVER_SILERO_VAD_MODEL_INPUT_SAMPLES * sizeof(float)) ||
        !river_silero_vad_eval_tensor_buffer_ready(
            context->state_input_eval_tensor,
            RIVER_SILERO_VAD_STATE_FLOATS * sizeof(float)) ||
        !river_silero_vad_eval_tensor_buffer_ready(
            context->prob_output_eval_tensor,
            sizeof(float)) ||
        !river_silero_vad_eval_tensor_buffer_ready(
            context->state_output_eval_tensor,
            RIVER_SILERO_VAD_STATE_FLOATS * sizeof(float))) {
        river_silero_vad_dump_interpreter_io(context->interpreter);
        context->interpreter->~MicroInterpreter();
        context->op_resolver.~river_silero_vad_op_resolver_t();
        river_silero_vad_free_tensor_arena(context);
        rtos_mem_free(context);
        printf("[river][voice] silero_vad tensor binding failed\n");
        return RIVER_ERR_UNSUPPORTED;
    }

    context->arena_used_bytes = (uint32_t)context->interpreter->arena_used_bytes();
    context->model_imported = true;

    detector->window_frame_bytes = RIVER_SILERO_VAD_WINDOW_SAMPLES * sizeof(int16_t);
    detector->backend_ctx = context;
    detector->staged_only = false;

    printf("[river][voice] silero_vad runtime ready: model=silero_vad_16k_b1_fp32.tflite arena=%luKB used=%luB threshold_q15=%u\n",
           (unsigned long)(context->arena_size_bytes / 1024U),
           (unsigned long)context->arena_used_bytes,
           (unsigned int)CONFIG_RIVER_SILERO_VAD_SPEECH_THRESHOLD_Q15);
    return RIVER_OK;
}

extern "C" river_status_t river_voice_detector_silero_process(
    river_voice_detector_t *detector,
    const uint8_t *input,
    size_t input_bytes,
    river_voice_detector_result_t *result)
{
    river_voice_detector_silero_context_t *context;
    const int16_t *samples;
    uint32_t input_samples;
    uint32_t probability_q15;
    float probability;

    if (detector == NULL || input == NULL) {
        return RIVER_ERR_ARG;
    }
    if (input_bytes != detector->input_frame_bytes) {
        return RIVER_ERR_ARG;
    }

    context = (river_voice_detector_silero_context_t *)detector->backend_ctx;
    if (context == NULL || !context->model_imported) {
        return RIVER_ERR_UNSUPPORTED;
    }

    if (result != NULL) {
        memset(result, 0, sizeof(*result));
        result->consumed_samples = RIVER_SILERO_VAD_FEED_SAMPLES;
    }

    input_samples = (uint32_t)(input_bytes / sizeof(int16_t));
    samples = (const int16_t *)input;
    memcpy(context->pending_window + context->carried_samples,
           samples,
           input_bytes);
    context->carried_samples += input_samples;
    context->frames_seen++;

    if (context->carried_samples < RIVER_SILERO_VAD_WINDOW_SAMPLES) {
        return RIVER_OK;
    }

    river_silero_vad_prepare_input(context->context_window,
                                   context->pending_window,
                                   context->audio_input_tensor->data.f);
    memcpy(context->state_input_tensor->data.f,
           context->recurrent_state,
           sizeof(context->recurrent_state));

    if (context->interpreter->Invoke() != kTfLiteOk) {
        context->invoke_failures++;
        context->carried_samples = 0U;
        memcpy(context->context_window,
               context->pending_window + (RIVER_SILERO_VAD_WINDOW_SAMPLES - RIVER_SILERO_VAD_CONTEXT_SAMPLES),
               RIVER_SILERO_VAD_CONTEXT_SAMPLES * sizeof(int16_t));
        return RIVER_ERR_IO;
    }

    memcpy(context->next_state,
           context->state_output_tensor->data.f,
           sizeof(context->next_state));
    memcpy(context->recurrent_state,
           context->next_state,
           sizeof(context->recurrent_state));
    memcpy(context->context_window,
           context->pending_window + (RIVER_SILERO_VAD_WINDOW_SAMPLES - RIVER_SILERO_VAD_CONTEXT_SAMPLES),
           RIVER_SILERO_VAD_CONTEXT_SAMPLES * sizeof(int16_t));
    context->carried_samples = 0U;

    probability = context->prob_output_tensor->data.f[0];
    if (probability < 0.0f) {
        probability = 0.0f;
    } else if (probability > 1.0f) {
        probability = 1.0f;
    }
    probability_q15 = (uint32_t)(probability * 32767.0f + 0.5f);
    context->decisions_made++;

    if (result != NULL) {
        result->decision_valid = true;
        result->speech_probability_q15 = (uint16_t)probability_q15;
        result->is_speech = probability_q15 >= (uint32_t)CONFIG_RIVER_SILERO_VAD_SPEECH_THRESHOLD_Q15;
        if (result->is_speech) {
            context->speech_decisions++;
        }
    }

    return RIVER_OK;
}

extern "C" void river_voice_detector_silero_close(river_voice_detector_t *detector)
{
    river_voice_detector_silero_context_t *context;

    if (detector == NULL) {
        return;
    }

    context = (river_voice_detector_silero_context_t *)detector->backend_ctx;
    if (context != NULL) {
        if (context->interpreter != NULL) {
            context->interpreter->~MicroInterpreter();
            context->interpreter = NULL;
        }
        if (context->op_resolver_constructed) {
            context->op_resolver.~river_silero_vad_op_resolver_t();
            context->op_resolver_constructed = false;
        }
        river_silero_vad_free_tensor_arena(context);
        rtos_mem_free(context);
    }
    detector->backend_ctx = NULL;
}

extern "C" void river_voice_detector_silero_dump_profile(void)
{
    printf("[river][voice] detector backend: silero_vad runtime=tflite_micro feed=256 samples window=512 samples context=64 samples model_input=576 samples model=silero_vad_16k_b1_fp32.tflite threshold_q15=%u arena=%uKB\n",
           (unsigned int)CONFIG_RIVER_SILERO_VAD_SPEECH_THRESHOLD_Q15,
           (unsigned int)CONFIG_RIVER_SILERO_VAD_TENSOR_ARENA_KB);
    printf("[river][voice] detector policy: direct official-model migration is complete; compression stays deferred until on-device flash/heap/latency data requires it\n");
}
