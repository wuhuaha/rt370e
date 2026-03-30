/* 唤醒词后端：完成特征提取、TFLite Micro 推理和触发判定。 */
#include <math.h>
#include <new>
#include <limits.h>
#include <string.h>

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
#include "real_fft.h"
#include "river/river_audio_frame_ring.h"
#include "river/river_cloud.h"
#include "river/river_interaction_state.h"
#include "river/river_log.h"
#include "river/river_voice.h"
#include "river/river_voice_kws.h"
}

#if defined(CONFIG_RIVER_KWS_MODEL_VARIANT_ROUND6_TARGETED_EXPERIMENTAL)
#include "generated/xiaou_student_round6_targeted_int8_model_data.h"
#define RIVER_KWS_MODEL_DATA kws_model_round6_targeted
#define RIVER_KWS_MODEL_DATA_LEN kws_model_round6_targeted_len
#define RIVER_KWS_MODEL_VARIANT_NAME "round6_targeted_experimental"
#else
#include "generated/river_wake_word_model_data.h"
#define RIVER_KWS_MODEL_DATA kws_model
#define RIVER_KWS_MODEL_DATA_LEN kws_model_len
#define RIVER_KWS_MODEL_VARIANT_NAME "bc_resnet_best"
#endif

#include "river_voice_kws_mean_patch.h"

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/memory_helpers.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#ifndef CONFIG_RIVER_KWS_TENSOR_ARENA_KB
#define CONFIG_RIVER_KWS_TENSOR_ARENA_KB 96
#endif

#ifndef CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15
#define CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15 27852
#endif

#ifndef CONFIG_RIVER_KWS_TRIGGER_HOLD_FRAMES
#define CONFIG_RIVER_KWS_TRIGGER_HOLD_FRAMES 2
#endif

#ifndef CONFIG_RIVER_KWS_COOLDOWN_MS
#define CONFIG_RIVER_KWS_COOLDOWN_MS 1800
#endif

#ifndef CONFIG_RIVER_KWS_LOG_PERIOD_MS
#define CONFIG_RIVER_KWS_LOG_PERIOD_MS 1000
#endif

#ifndef CONFIG_RIVER_KWS_INFERENCE_STRIDE_FRAMES
#define CONFIG_RIVER_KWS_INFERENCE_STRIDE_FRAMES 2
#endif

#ifndef CONFIG_RIVER_KWS_VAD_PRE_ROLL_MS
#define CONFIG_RIVER_KWS_VAD_PRE_ROLL_MS 320
#endif

#ifndef CONFIG_RIVER_KWS_INPUT_QUEUE_FRAMES
#define CONFIG_RIVER_KWS_INPUT_QUEUE_FRAMES 64
#endif

#define RIVER_KWS_SAMPLE_RATE_HZ 16000U
#define RIVER_KWS_WINDOW_SAMPLES 512U
#define RIVER_KWS_HOP_SAMPLES 160U
#define RIVER_KWS_MEL_BINS 40U
#define RIVER_KWS_FEATURE_FRAMES 98U
#define RIVER_KWS_FEATURE_DB_MIN (-80.0f)
#define RIVER_KWS_FEATURE_MEAN (-42.1177063f)
#define RIVER_KWS_FEATURE_STD (17.5219841f)
#define RIVER_KWS_FMIN_HZ (20.0f)
#define RIVER_KWS_FMAX_HZ (8000.0f)
#define RIVER_KWS_HANN_PI (3.14159265358979323846f)
#define RIVER_KWS_MEL_POINT_COUNT (RIVER_KWS_MEL_BINS + 2U)
#define RIVER_KWS_FFT_BINS ((RIVER_KWS_WINDOW_SAMPLES / 2U) + 1U)
#define RIVER_KWS_TENSOR_ARENA_BYTES \
    ((uint32_t)CONFIG_RIVER_KWS_TENSOR_ARENA_KB * 1024U)
#define RIVER_KWS_OP_COUNT 8U
#define RIVER_KWS_EXPECTED_INPUT_VALUES \
    (RIVER_KWS_FEATURE_FRAMES * RIVER_KWS_MEL_BINS)
#define RIVER_KWS_ALLOCATION_ALIGNMENT 32U
#define RIVER_KWS_INPUT_FRAME_MS 16U
#define RIVER_KWS_INPUT_FRAME_SAMPLES \
    ((RIVER_KWS_SAMPLE_RATE_HZ / 1000U) * RIVER_KWS_INPUT_FRAME_MS)
#define RIVER_KWS_INPUT_FRAME_BYTES \
    (RIVER_KWS_INPUT_FRAME_SAMPLES * sizeof(int16_t))
#define RIVER_KWS_PRE_ROLL_FRAMES_RAW \
    (((uint32_t)CONFIG_RIVER_KWS_VAD_PRE_ROLL_MS + \
      RIVER_KWS_INPUT_FRAME_MS - 1U) / RIVER_KWS_INPUT_FRAME_MS)
#define RIVER_KWS_PRE_ROLL_FRAMES \
    ((RIVER_KWS_PRE_ROLL_FRAMES_RAW > 0U) ? \
         RIVER_KWS_PRE_ROLL_FRAMES_RAW : \
         1U)
#define RIVER_KWS_TASK_STACK (1024U * 8U)
#define RIVER_KWS_TASK_PRIORITY 4U
#define RIVER_KWS_TASK_IDLE_DELAY_MS 2U
#define RIVER_KWS_GATE_FALLBACK_THRESHOLD_PM 350U
#define RIVER_KWS_GATE_FALLBACK_MIN_MS 700U
#define RIVER_KWS_GATE_FALLBACK_MAX_MS 2500U
#define RIVER_KWS_GATE_FALLBACK_MIN_INFER 4U

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.voice.kws"

class river_voice_kws_op_resolver_t : public tflite::MicroOpResolver {
  public:
    river_voice_kws_op_resolver_t()
        : registrations_len_(0U), builtin_parsers_len_(0U)
    {
    }

    const TFLMRegistration *FindOp(tflite::BuiltinOperator op) const override
    {
        unsigned int index;

        if (op == tflite::BuiltinOperator_CUSTOM) {
            return NULL;
        }
        for (index = 0U; index < registrations_len_; ++index) {
            if (registrations_[index].builtin_code == (int32_t)op) {
                return &registrations_[index];
            }
        }
        return NULL;
    }

    const TFLMRegistration *FindOp(const char *op) const override
    {
        (void)op;
        return NULL;
    }

    tflite::TfLiteBridgeBuiltinParseFunction
    GetOpDataParser(tflite::BuiltinOperator op) const override
    {
        unsigned int index;

        for (index = 0U; index < builtin_parsers_len_; ++index) {
            if (builtin_codes_[index] == op) {
                return builtin_parsers_[index];
            }
        }
        return NULL;
    }

    TfLiteStatus AddQuantize()
    {
        return AddBuiltin(tflite::BuiltinOperator_QUANTIZE,
                          tflite::Register_QUANTIZE(), tflite::ParseQuantize);
    }

    TfLiteStatus AddPad()
    {
        return AddBuiltin(tflite::BuiltinOperator_PAD, tflite::Register_PAD(),
                          tflite::ParsePad);
    }

    TfLiteStatus AddAdd()
    {
        return AddBuiltin(tflite::BuiltinOperator_ADD, tflite::Register_ADD(),
                          tflite::ParseAdd);
    }

    TfLiteStatus AddConv2D()
    {
        return AddBuiltin(tflite::BuiltinOperator_CONV_2D,
                          tflite::Register_CONV_2D(), tflite::ParseConv2D);
    }

    TfLiteStatus AddDepthwiseConv2D()
    {
        return AddBuiltin(tflite::BuiltinOperator_DEPTHWISE_CONV_2D,
                          tflite::Register_DEPTHWISE_CONV_2D(),
                          tflite::ParseDepthwiseConv2D);
    }

    TfLiteStatus AddPatchedMean()
    {
        return AddBuiltin(tflite::BuiltinOperator_MEAN,
                          river_voice_kws_RegisterPatchedMean(),
                          tflite::ParseReducer);
    }

    TfLiteStatus AddFullyConnected()
    {
        return AddBuiltin(tflite::BuiltinOperator_FULLY_CONNECTED,
                          tflite::Register_FULLY_CONNECTED(),
                          tflite::ParseFullyConnected);
    }

    TfLiteStatus AddLogistic()
    {
        return AddBuiltin(tflite::BuiltinOperator_LOGISTIC,
                          tflite::Register_LOGISTIC(),
                          tflite::ParseLogistic);
    }

  private:
    TfLiteStatus AddBuiltin(tflite::BuiltinOperator op,
                            const TFLMRegistration &registration,
                            tflite::TfLiteBridgeBuiltinParseFunction parser)
    {
        TFLMRegistration *dst;

        if (registrations_len_ >= RIVER_KWS_OP_COUNT ||
            builtin_parsers_len_ >= RIVER_KWS_OP_COUNT) {
            return kTfLiteError;
        }
        if (FindOp(op) != NULL) {
            return kTfLiteError;
        }
        dst = &registrations_[registrations_len_++];
        *dst = registration;
        dst->builtin_code = (int32_t)op;
        dst->custom_name = NULL;
        builtin_codes_[builtin_parsers_len_] = op;
        builtin_parsers_[builtin_parsers_len_] = parser;
        ++builtin_parsers_len_;
        return kTfLiteOk;
    }

    TFLMRegistration registrations_[RIVER_KWS_OP_COUNT];
    tflite::BuiltinOperator builtin_codes_[RIVER_KWS_OP_COUNT];
    tflite::TfLiteBridgeBuiltinParseFunction
        builtin_parsers_[RIVER_KWS_OP_COUNT];
    unsigned int registrations_len_;
    unsigned int builtin_parsers_len_;
};

typedef enum {
    RIVER_KWS_QUEUE_ITEM_PCM = 0U,
    RIVER_KWS_QUEUE_ITEM_RESET = 1U
} river_voice_kws_queue_item_type_t;

typedef enum {
    RIVER_KWS_INPUT_LAYOUT_FRAMES_MELS = 0U,
    RIVER_KWS_INPUT_LAYOUT_MELS_FRAMES = 1U
} river_voice_kws_input_layout_t;

typedef struct {
    uint32_t type;
    uint8_t pcm[RIVER_KWS_INPUT_FRAME_BYTES];
} river_voice_kws_queue_item_t;

typedef struct {
    bool initialized;
    bool window_ready;
    bool resolver_constructed;
    bool tensor_arena_from_heap_types;
    bool gate_open;
    bool gate_triggered;
    uint32_t mel_frames_seen;
    uint32_t mel_history_count;
    uint32_t mel_history_write_index;
    uint32_t hop_samples_pending;
    uint32_t inference_stride_count;
    uint32_t inference_count;
    uint32_t hit_count;
    uint32_t trigger_count;
    uint32_t hit_streak;
    uint32_t last_confidence_q15;
    uint32_t gate_best_confidence_q15;
    uint32_t gate_inference_count;
    uint64_t gate_started_ms;
    uint64_t cooldown_until_ms;
    uint64_t last_status_log_ms;
    float last_score;
    float mel_band_norm[RIVER_KWS_MEL_BINS];
    uint16_t mel_start_bin[RIVER_KWS_MEL_BINS];
    uint16_t mel_center_bin[RIVER_KWS_MEL_BINS];
    uint16_t mel_end_bin[RIVER_KWS_MEL_BINS];
    float hann_window[RIVER_KWS_WINDOW_SAMPLES];
    float log_mel_history[RIVER_KWS_FEATURE_FRAMES][RIVER_KWS_MEL_BINS];
    float power_bins[RIVER_KWS_FFT_BINS];
    int16_t sample_ring[RIVER_KWS_WINDOW_SAMPLES];
    alignas(RIVER_KWS_ALLOCATION_ALIGNMENT)
        int16_t fft_input[RIVER_KWS_WINDOW_SAMPLES];
    alignas(RIVER_KWS_ALLOCATION_ALIGNMENT)
        int16_t fft_output[RIVER_KWS_WINDOW_SAMPLES + 2U];
    uint32_t sample_ring_write_index;
    uint32_t sample_ring_fill_count;
    struct RealFFT *real_fft;
    void *tensor_arena_allocation;
    uint8_t *tensor_arena;
    const tflite::Model *model;
    river_voice_kws_op_resolver_t op_resolver;
    alignas(alignof(tflite::MicroInterpreter))
        uint8_t interpreter_storage[sizeof(tflite::MicroInterpreter)];
    tflite::MicroInterpreter *interpreter;
    TfLiteTensor *input_tensor;
    TfLiteTensor *output_tensor;
    void *input_tensor_data;
    void *output_tensor_data;
    TfLiteType model_input_type;
    TfLiteType model_output_type;
    TfLiteType effective_input_type;
    TfLiteType effective_output_type;
    river_voice_kws_input_layout_t input_layout;
    uint32_t input_shape[4];
    float input_scale;
    int input_zero_point;
    float output_scale;
    int output_zero_point;
    bool tensor_data_drift_logged;
    bool task_running;
    bool task_stop_requested;
    rtos_task_t task;
    river_audio_frame_ring_t pre_roll_ring;
    uint32_t pre_roll_ring_dropped;
    uint32_t gate_open_count;
    uint32_t gate_close_count;
    uint32_t pre_roll_flush_count;
    uint8_t pre_roll_ring_storage[RIVER_KWS_INPUT_FRAME_BYTES *
                                  RIVER_KWS_PRE_ROLL_FRAMES];
    uint8_t pre_roll_frame[RIVER_KWS_INPUT_FRAME_BYTES];
    uint8_t pre_roll_drop_frame[RIVER_KWS_INPUT_FRAME_BYTES];
    river_audio_frame_ring_t input_ring;
    uint32_t input_ring_dropped;
    uint8_t input_ring_storage[sizeof(river_voice_kws_queue_item_t) *
                               CONFIG_RIVER_KWS_INPUT_QUEUE_FRAMES];
    river_voice_kws_queue_item_t input_task_item;
    river_voice_kws_queue_item_t input_drop_item;
} river_voice_kws_context_t;

static river_voice_kws_context_t *g_river_voice_kws;
static void *g_river_voice_kws_allocation;
static bool g_river_voice_kws_allocation_from_heap_types;
static const char g_river_voice_kws_text[] = "小欧管家";

static inline uint8_t river_voice_kws_clamp_u8(int value)
{
    if (value < 0) {
        return 0U;
    }
    if (value > 255) {
        return 255U;
    }
    return (uint8_t)value;
}

static inline int8_t river_voice_kws_clamp_i8(int value)
{
    if (value < -128) {
        return (int8_t)-128;
    }
    if (value > 127) {
        return (int8_t)127;
    }
    return (int8_t)value;
}

static inline int16_t river_voice_kws_clamp_i16(int32_t value)
{
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    return (int16_t)value;
}

static inline int32_t river_voice_kws_round_to_i32(float value)
{
    return (int32_t)(value >= 0.0f ? (value + 0.5f) : (value - 0.5f));
}

static const char *river_voice_kws_tensor_type_name(TfLiteType type)
{
    switch (type) {
    case kTfLiteFloat32:
        return "float32";
    case kTfLiteUInt8:
        return "uint8";
    case kTfLiteInt8:
        return "int8";
    case kTfLiteNoType:
        return "none";
    default:
        return "other";
    }
}

static const char *river_voice_kws_input_layout_name(
    river_voice_kws_input_layout_t layout)
{
    switch (layout) {
    case RIVER_KWS_INPUT_LAYOUT_FRAMES_MELS:
        return "frames_mels";
    case RIVER_KWS_INPUT_LAYOUT_MELS_FRAMES:
        return "mels_frames";
    default:
        return "unknown";
    }
}

static const tflite::Tensor *river_voice_kws_model_io_tensor(
    const tflite::Model *model,
    bool input);

static bool river_voice_kws_copy_schema_shape(
    const flatbuffers::Vector<int32_t> *shape,
    uint32_t dims_out[4],
    size_t *dim_count)
{
    size_t index;

    if (dim_count != NULL) {
        *dim_count = 0U;
    }
    if (dims_out != NULL) {
        memset(dims_out, 0, sizeof(uint32_t) * 4U);
    }
    if (shape == NULL || shape->size() == 0U || shape->size() > 4U) {
        return false;
    }

    for (index = 0U; index < shape->size(); ++index) {
        int32_t dim = shape->Get(index);

        if (dim < 0) {
            return false;
        }
        if (dims_out != NULL) {
            dims_out[index] = (uint32_t)dim;
        }
    }
    if (dim_count != NULL) {
        *dim_count = shape->size();
    }
    return true;
}

static TfLiteType river_voice_kws_model_tensor_type_name_to_tflite(
    tflite::TensorType type)
{
    switch (type) {
    case tflite::TensorType_FLOAT32:
        return kTfLiteFloat32;
    case tflite::TensorType_UINT8:
        return kTfLiteUInt8;
    case tflite::TensorType_INT8:
        return kTfLiteInt8;
    default:
        return kTfLiteNoType;
    }
}

static TfLiteType river_voice_kws_model_io_type(const tflite::Model *model,
                                                bool input)
{
    const tflite::Tensor *tensor;

    tensor = river_voice_kws_model_io_tensor(model, input);
    if (tensor == NULL) {
        return kTfLiteNoType;
    }

    return river_voice_kws_model_tensor_type_name_to_tflite(tensor->type());
}

static const tflite::Tensor *river_voice_kws_model_io_tensor(
    const tflite::Model *model,
    bool input)
{
    const tflite::SubGraph *subgraph;
    const flatbuffers::Vector<int32_t> *io_indices;
    const flatbuffers::Vector<flatbuffers::Offset<tflite::Tensor>> *tensors;
    int32_t tensor_index;

    if (model == NULL || model->subgraphs() == NULL ||
        model->subgraphs()->size() == 0U) {
        return NULL;
    }

    subgraph = model->subgraphs()->Get(0);
    if (subgraph == NULL) {
        return NULL;
    }

    io_indices = input ? subgraph->inputs() : subgraph->outputs();
    tensors = subgraph->tensors();
    if (io_indices == NULL || io_indices->size() == 0U || tensors == NULL) {
        return NULL;
    }

    tensor_index = io_indices->Get(0);
    if (tensor_index < 0 || (size_t)tensor_index >= tensors->size()) {
        return NULL;
    }

    return tensors->Get(static_cast<flatbuffers::uoffset_t>(tensor_index));
}

static bool river_voice_kws_schema_quant_params(const tflite::Model *model,
                                                bool input,
                                                float *scale,
                                                int *zero_point)
{
    const tflite::Tensor *tensor;
    const tflite::QuantizationParameters *quant;
    const flatbuffers::Vector<float> *scales;
    const flatbuffers::Vector<int64_t> *zero_points;
    int64_t zero_point64 = 0;

    tensor = river_voice_kws_model_io_tensor(model, input);
    if (tensor == NULL) {
        return false;
    }

    quant = tensor->quantization();
    if (quant == NULL) {
        return false;
    }

    scales = quant->scale();
    if (scales == NULL || scales->size() == 0U) {
        return false;
    }

    zero_points = quant->zero_point();
    if (zero_points != NULL && zero_points->size() > 0U) {
        zero_point64 = zero_points->Get(0);
    }

    if (zero_point64 < INT_MIN || zero_point64 > INT_MAX) {
        return false;
    }

    if (scale != NULL) {
        *scale = scales->Get(0);
    }
    if (zero_point != NULL) {
        *zero_point = (int)zero_point64;
    }
    return true;
}

static bool river_voice_kws_schema_io_shape(const tflite::Model *model,
                                            bool input,
                                            uint32_t dims_out[4],
                                            size_t *dim_count)
{
    const tflite::Tensor *tensor;

    tensor = river_voice_kws_model_io_tensor(model, input);
    if (tensor == NULL) {
        return false;
    }

    return river_voice_kws_copy_schema_shape(tensor->shape(), dims_out, dim_count);
}

static bool river_voice_kws_runtime_io_shape(const TfLiteTensor *tensor,
                                             uint32_t dims_out[4],
                                             size_t *dim_count)
{
    const TfLiteIntArray *dims;
    int index;

    if (dim_count != NULL) {
        *dim_count = 0U;
    }
    if (dims_out != NULL) {
        memset(dims_out, 0, sizeof(uint32_t) * 4U);
    }
    if (tensor == NULL || tensor->dims == NULL || tensor->dims->size <= 0 ||
        tensor->dims->size > 4) {
        return false;
    }

    dims = tensor->dims;
    for (index = 0; index < dims->size; ++index) {
        if (dims->data[index] < 0) {
            return false;
        }
        if (dims_out != NULL) {
            dims_out[index] = (uint32_t)dims->data[index];
        }
    }
    if (dim_count != NULL) {
        *dim_count = (size_t)dims->size;
    }
    return true;
}

static bool river_voice_kws_resolve_io_shape(const tflite::Model *model,
                                             const TfLiteTensor *tensor,
                                             bool input,
                                             uint32_t dims_out[4],
                                             size_t *dim_count,
                                             const char **source)
{
    if (source != NULL) {
        *source = "none";
    }
    if (river_voice_kws_schema_io_shape(model, input, dims_out, dim_count)) {
        if (source != NULL) {
            *source = "schema";
        }
        return true;
    }
    if (river_voice_kws_runtime_io_shape(tensor, dims_out, dim_count)) {
        if (source != NULL) {
            *source = "runtime";
        }
        return true;
    }
    return false;
}

static bool river_voice_kws_shape_element_count(const uint32_t *dims,
                                                size_t dim_count,
                                                size_t *element_count)
{
    size_t count = 1U;
    size_t index;

    if (element_count != NULL) {
        *element_count = 0U;
    }
    if (dims == NULL || dim_count == 0U) {
        return false;
    }

    for (index = 0U; index < dim_count; ++index) {
        if (dims[index] == 0U) {
            return false;
        }
        count *= (size_t)dims[index];
    }
    if (element_count != NULL) {
        *element_count = count;
    }
    return true;
}

static bool river_voice_kws_detect_input_layout(
    const tflite::Model *model,
    const TfLiteTensor *input_tensor,
    river_voice_kws_input_layout_t *layout,
    uint32_t dims_out[4],
    const char **source)
{
    uint32_t dims[4];
    size_t dim_count = 0U;

    if (!river_voice_kws_resolve_io_shape(model,
                                          input_tensor,
                                          true,
                                          dims,
                                          &dim_count,
                                          source)) {
        return false;
    }

    if (dim_count != 4U || dims[0] != 1U || dims[3] != 1U) {
        return false;
    }
    if (dims[1] == RIVER_KWS_FEATURE_FRAMES &&
        dims[2] == RIVER_KWS_MEL_BINS) {
        *layout = RIVER_KWS_INPUT_LAYOUT_FRAMES_MELS;
    } else if (dims[1] == RIVER_KWS_MEL_BINS &&
               dims[2] == RIVER_KWS_FEATURE_FRAMES) {
        *layout = RIVER_KWS_INPUT_LAYOUT_MELS_FRAMES;
    } else {
        return false;
    }

    if (dims_out != NULL) {
        memcpy(dims_out, dims, sizeof(dims));
    }
    return true;
}

static bool river_voice_kws_runtime_quant_params_valid(const TfLiteTensor *tensor,
                                                       TfLiteType effective_type)
{
    if (tensor == NULL || tensor->params.scale <= 0.0f) {
        return false;
    }

    if (effective_type == kTfLiteUInt8) {
        return tensor->params.zero_point >= 0 && tensor->params.zero_point <= 255;
    }
    if (effective_type == kTfLiteInt8) {
        return tensor->params.zero_point >= -128 && tensor->params.zero_point <= 127;
    }

    return true;
}

static void river_voice_kws_resolve_quant_params(const tflite::Model *model,
                                                 const TfLiteTensor *tensor,
                                                 bool input,
                                                 TfLiteType effective_type,
                                                 float *scale,
                                                 int *zero_point,
                                                 const char **source)
{
    bool schema_ok = false;

    if (scale != NULL) {
        *scale = 0.0f;
    }
    if (zero_point != NULL) {
        *zero_point = 0;
    }
    if (source != NULL) {
        *source = "none";
    }

    if (effective_type != kTfLiteUInt8 && effective_type != kTfLiteInt8) {
        if (source != NULL) {
            *source = "float";
        }
        return;
    }

    schema_ok = river_voice_kws_schema_quant_params(model, input, scale, zero_point);
    if (schema_ok) {
        if (source != NULL) {
            *source = "schema";
        }
        return;
    }

    if (river_voice_kws_runtime_quant_params_valid(tensor, effective_type)) {
        if (scale != NULL) {
            *scale = tensor->params.scale;
        }
        if (zero_point != NULL) {
            *zero_point = tensor->params.zero_point;
        }
        if (source != NULL) {
            *source = "runtime";
        }
    }
}

static TfLiteType river_voice_kws_effective_tensor_type(const TfLiteTensor *tensor,
                                                        TfLiteType model_type)
{
    if (model_type == kTfLiteUInt8 || model_type == kTfLiteInt8 ||
        model_type == kTfLiteFloat32) {
        return model_type;
    }

    if (tensor != NULL &&
        (tensor->type == kTfLiteUInt8 || tensor->type == kTfLiteInt8 ||
         tensor->type == kTfLiteFloat32)) {
        return tensor->type;
    }

    return kTfLiteNoType;
}

static size_t river_voice_kws_tensor_storage_bytes(TfLiteType type)
{
    switch (type) {
    case kTfLiteFloat32:
        return sizeof(float);
    case kTfLiteUInt8:
    case kTfLiteInt8:
        return sizeof(uint8_t);
    default:
        return 0U;
    }
}

static bool river_voice_kws_tensor_bytes_sufficient(size_t bytes_field,
                                                    size_t element_count,
                                                    TfLiteType effective_type)
{
    size_t storage_bytes;

    if (bytes_field == 0U) {
        return true;
    }

    storage_bytes = river_voice_kws_tensor_storage_bytes(effective_type);
    if (storage_bytes == 0U) {
        return false;
    }

    /*
     * Some board-side TFLM ports surface tensor->bytes as element-count
     * instead of storage bytes. Accept both representations.
     */
    return bytes_field >= (element_count * storage_bytes) ||
           bytes_field >= element_count;
}

static void *river_voice_kws_align_ptr(void *ptr, size_t alignment)
{
    uintptr_t address;

    if (ptr == NULL || alignment == 0U) {
        return ptr;
    }

    address = (uintptr_t)ptr;
    address = (address + alignment - 1U) & ~(uintptr_t)(alignment - 1U);
    return (void *)address;
}

static void *river_voice_kws_alloc_aligned(size_t bytes,
                                           size_t alignment,
                                           bool *from_heap_types,
                                           void **allocation)
{
    void *raw;
    size_t total;

    if (allocation != NULL) {
        *allocation = NULL;
    }
    if (from_heap_types != NULL) {
        *from_heap_types = false;
    }
    if (bytes == 0U || alignment == 0U) {
        return NULL;
    }

    total = bytes + alignment - 1U;
    raw = rtos_heap_types_zmalloc(total, TYPE_DRAM);
    if (raw != NULL) {
        if (allocation != NULL) {
            *allocation = raw;
        }
        if (from_heap_types != NULL) {
            *from_heap_types = true;
        }
        return river_voice_kws_align_ptr(raw, alignment);
    }

    raw = rtos_mem_zmalloc(total);
    if (raw == NULL) {
        return NULL;
    }
    if (allocation != NULL) {
        *allocation = raw;
    }
    return river_voice_kws_align_ptr(raw, alignment);
}

static inline uint32_t river_voice_kws_score_threshold_q15(void)
{
    return (uint32_t)CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15;
}

static inline uint32_t river_voice_kws_gate_fallback_threshold_q15(void)
{
    uint32_t primary = river_voice_kws_score_threshold_q15();
    uint32_t fallback =
        (uint32_t)((RIVER_KWS_GATE_FALLBACK_THRESHOLD_PM * 32767U) / 1000U);

    if (fallback >= primary && primary > 0U) {
        fallback = primary - 1U;
    }
    return fallback;
}

static inline uint32_t river_voice_kws_confidence_to_permille(uint32_t confidence_q15)
{
    return (uint32_t)((confidence_q15 * 1000U) / 32767U);
}

static uint64_t river_voice_kws_gate_elapsed_ms(const river_voice_kws_context_t *context,
                                                uint64_t now_ms)
{
    if (context == NULL || context->gate_started_ms == 0U || now_ms <= context->gate_started_ms) {
        return 0U;
    }
    return now_ms - context->gate_started_ms;
}

static float river_voice_kws_hz_to_mel(float hz)
{
    const float f_sp = 200.0f / 3.0f;
    const float min_log_hz = 1000.0f;
    const float min_log_mel = min_log_hz / f_sp;
    const float logstep = logf(6.4f) / 27.0f;

    if (hz < min_log_hz) {
        return hz / f_sp;
    }
    return min_log_mel + logf(hz / min_log_hz) / logstep;
}

static float river_voice_kws_mel_to_hz(float mel)
{
    const float f_sp = 200.0f / 3.0f;
    const float min_log_hz = 1000.0f;
    const float min_log_mel = min_log_hz / f_sp;
    const float logstep = logf(6.4f) / 27.0f;

    if (mel < min_log_mel) {
        return mel * f_sp;
    }
    return min_log_hz * expf(logstep * (mel - min_log_mel));
}

static river_status_t river_voice_kws_register_ops(
    river_voice_kws_op_resolver_t *resolver)
{
    if (resolver == NULL) {
        return RIVER_ERR_ARG;
    }
    if (resolver->AddQuantize() != kTfLiteOk ||
        resolver->AddPad() != kTfLiteOk ||
        resolver->AddAdd() != kTfLiteOk ||
        resolver->AddConv2D() != kTfLiteOk ||
        resolver->AddDepthwiseConv2D() != kTfLiteOk ||
        resolver->AddPatchedMean() != kTfLiteOk ||
        resolver->AddFullyConnected() != kTfLiteOk ||
        resolver->AddLogistic() != kTfLiteOk) {
        return RIVER_ERR_UNSUPPORTED;
    }
    return RIVER_OK;
}

static void river_voice_kws_prepare_hann_window(river_voice_kws_context_t *context)
{
    uint32_t index;

    for (index = 0U; index < RIVER_KWS_WINDOW_SAMPLES; ++index) {
        context->hann_window[index] =
            0.5f - 0.5f *
                       cosf((2.0f * RIVER_KWS_HANN_PI * (float)index) /
                            (float)(RIVER_KWS_WINDOW_SAMPLES - 1U));
    }
}

static void river_voice_kws_prepare_mel_bands(river_voice_kws_context_t *context)
{
    float mel_points[RIVER_KWS_MEL_POINT_COUNT];
    float hz_points[RIVER_KWS_MEL_POINT_COUNT];
    const float mel_min = river_voice_kws_hz_to_mel(RIVER_KWS_FMIN_HZ);
    const float mel_max = river_voice_kws_hz_to_mel(RIVER_KWS_FMAX_HZ);
    const float mel_step =
        (mel_max - mel_min) / (float)(RIVER_KWS_MEL_POINT_COUNT - 1U);
    uint32_t index;

    for (index = 0U; index < RIVER_KWS_MEL_POINT_COUNT; ++index) {
        mel_points[index] = mel_min + mel_step * (float)index;
        hz_points[index] = river_voice_kws_mel_to_hz(mel_points[index]);
    }

    for (index = 0U; index < RIVER_KWS_MEL_BINS; ++index) {
        uint32_t start_bin;
        uint32_t center_bin;
        uint32_t end_bin;
        float left_hz;
        float center_hz;
        float right_hz;

        left_hz = hz_points[index];
        center_hz = hz_points[index + 1U];
        right_hz = hz_points[index + 2U];

        start_bin = (uint32_t)floorf(((float)(RIVER_KWS_WINDOW_SAMPLES + 1U) *
                                      left_hz) /
                                     (float)RIVER_KWS_SAMPLE_RATE_HZ);
        center_bin = (uint32_t)floorf(((float)(RIVER_KWS_WINDOW_SAMPLES + 1U) *
                                       center_hz) /
                                      (float)RIVER_KWS_SAMPLE_RATE_HZ);
        end_bin = (uint32_t)floorf(((float)(RIVER_KWS_WINDOW_SAMPLES + 1U) *
                                    right_hz) /
                                   (float)RIVER_KWS_SAMPLE_RATE_HZ);

        if (start_bin >= RIVER_KWS_FFT_BINS) {
            start_bin = RIVER_KWS_FFT_BINS - 1U;
        }
        if (center_bin <= start_bin) {
            center_bin = start_bin + 1U;
        }
        if (center_bin >= RIVER_KWS_FFT_BINS) {
            center_bin = RIVER_KWS_FFT_BINS - 1U;
        }
        if (end_bin <= center_bin) {
            end_bin = center_bin + 1U;
        }
        if (end_bin >= RIVER_KWS_FFT_BINS) {
            end_bin = RIVER_KWS_FFT_BINS - 1U;
        }

        context->mel_start_bin[index] = (uint16_t)start_bin;
        context->mel_center_bin[index] = (uint16_t)center_bin;
        context->mel_end_bin[index] = (uint16_t)end_bin;
        context->mel_band_norm[index] =
            2.0f / fmaxf(right_hz - left_hz, 1.0f);
    }
}

static void river_voice_kws_reset_frontend(river_voice_kws_context_t *context)
{
    if (context == NULL) {
        return;
    }

    context->window_ready = false;
    context->mel_frames_seen = 0U;
    context->mel_history_count = 0U;
    context->mel_history_write_index = 0U;
    context->hop_samples_pending = 0U;
    context->inference_stride_count = 0U;
    context->hit_streak = 0U;
    context->last_confidence_q15 = 0U;
    context->gate_best_confidence_q15 = 0U;
    context->gate_inference_count = 0U;
    context->gate_started_ms = 0U;
    context->gate_triggered = false;
    context->last_score = 0.0f;
    context->sample_ring_write_index = 0U;
    context->sample_ring_fill_count = 0U;
    memset(context->log_mel_history, 0, sizeof(context->log_mel_history));
    memset(context->power_bins, 0, sizeof(context->power_bins));
    memset(context->sample_ring, 0, sizeof(context->sample_ring));
    memset(context->fft_input, 0, sizeof(context->fft_input));
    memset(context->fft_output, 0, sizeof(context->fft_output));
}

static bool river_voice_kws_detection_allowed(void)
{
    if (river_cloud_adapter_conversation_window_active()) {
        return false;
    }

    return river_interaction_state_get() == RIVER_INTERACTION_WAKE_MONITORING;
}

static void river_voice_kws_disarm(river_voice_kws_context_t *context,
                                   bool clear_pre_roll)
{
    if (context == NULL) {
        return;
    }

    context->gate_open = false;
    context->gate_triggered = false;
    river_voice_kws_reset_frontend(context);
    if (context->input_ring.initialized) {
        river_audio_frame_ring_reset(&context->input_ring);
    }
    if (clear_pre_roll && context->pre_roll_ring.initialized) {
        river_audio_frame_ring_reset(&context->pre_roll_ring);
    }
}

static void river_voice_kws_disarm_after_trigger(
    river_voice_kws_context_t *context)
{
    uint32_t last_confidence_q15;
    uint32_t gate_best_confidence_q15;
    uint32_t gate_inference_count;
    float last_score;

    if (context == NULL) {
        return;
    }

    last_confidence_q15 = context->last_confidence_q15;
    gate_best_confidence_q15 = context->gate_best_confidence_q15;
    gate_inference_count = context->gate_inference_count;
    last_score = context->last_score;

    river_voice_kws_disarm(context, true);
    context->gate_triggered = true;
    context->last_confidence_q15 = last_confidence_q15;
    context->gate_best_confidence_q15 = gate_best_confidence_q15;
    context->gate_inference_count = gate_inference_count;
    context->last_score = last_score;
}

static void river_voice_kws_emit_trigger(river_voice_kws_context_t *context,
                                         uint32_t confidence_q15,
                                         const char *mode)
{
    uint64_t now_ms;
    river_voice_event_t event;

    if (context == NULL) {
        return;
    }

    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    if (now_ms < context->cooldown_until_ms) {
        return;
    }

    context->last_confidence_q15 = confidence_q15;
    context->last_score = (float)confidence_q15 / 32767.0f;
    context->trigger_count++;
    context->cooldown_until_ms =
        now_ms + (uint64_t)CONFIG_RIVER_KWS_COOLDOWN_MS;
    context->hit_streak = 0U;
    context->gate_triggered = true;

    RIVER_LOGI("wakeword hit: text=%s score_pm=%lu q15=%lu triggers=%lu cooldown_ms=%u mode=%s",
               g_river_voice_kws_text,
               (unsigned long)river_voice_kws_confidence_to_permille(confidence_q15),
               (unsigned long)confidence_q15,
               (unsigned long)context->trigger_count,
               (unsigned int)CONFIG_RIVER_KWS_COOLDOWN_MS,
               mode != NULL ? mode : "threshold");

    memset(&event, 0, sizeof(event));
    event.type = RIVER_VOICE_EVENT_WAKEWORD;
    event.text = g_river_voice_kws_text;
    event.confidence = (int)confidence_q15;
    (void)river_voice_frontend_dispatch_event(&event);
    river_voice_kws_disarm_after_trigger(context);
}

static void river_voice_kws_push_sample(river_voice_kws_context_t *context,
                                        int16_t sample)
{
    context->sample_ring[context->sample_ring_write_index] = sample;
    context->sample_ring_write_index =
        (context->sample_ring_write_index + 1U) % RIVER_KWS_WINDOW_SAMPLES;
    if (context->sample_ring_fill_count < RIVER_KWS_WINDOW_SAMPLES) {
        context->sample_ring_fill_count++;
    }
    if (context->sample_ring_fill_count >= RIVER_KWS_WINDOW_SAMPLES) {
        context->hop_samples_pending++;
    }
}

static void river_voice_kws_capture_window(river_voice_kws_context_t *context)
{
    uint32_t start;
    uint32_t index;

    start = (context->sample_ring_write_index + RIVER_KWS_WINDOW_SAMPLES -
             context->sample_ring_fill_count) %
            RIVER_KWS_WINDOW_SAMPLES;
    for (index = 0U; index < RIVER_KWS_WINDOW_SAMPLES; ++index) {
        uint32_t ring_index = (start + index) % RIVER_KWS_WINDOW_SAMPLES;
        float scaled =
            (float)context->sample_ring[ring_index] * context->hann_window[index];
        context->fft_input[index] =
            river_voice_kws_clamp_i16(river_voice_kws_round_to_i32(scaled));
    }
}

static void river_voice_kws_store_mel_frame(river_voice_kws_context_t *context,
                                            const float *mel_frame)
{
    memcpy(context->log_mel_history[context->mel_history_write_index],
           mel_frame,
           sizeof(float) * RIVER_KWS_MEL_BINS);
    context->mel_history_write_index =
        (context->mel_history_write_index + 1U) % RIVER_KWS_FEATURE_FRAMES;
    if (context->mel_history_count < RIVER_KWS_FEATURE_FRAMES) {
        context->mel_history_count++;
    }
    context->mel_frames_seen++;
    if (context->mel_history_count == RIVER_KWS_FEATURE_FRAMES) {
        context->window_ready = true;
    }
}

static river_status_t river_voice_kws_compute_mel_frame(
    river_voice_kws_context_t *context)
{
    float mel_frame[RIVER_KWS_MEL_BINS];
    uint32_t band;

    river_voice_kws_capture_window(context);
    if (WebRtcSpl_RealForwardFFT(context->real_fft,
                                 context->fft_input,
                                 context->fft_output) != 0) {
        return RIVER_ERR_IO;
    }

    context->power_bins[0] =
        (float)context->fft_output[0] * (float)context->fft_output[0];
    for (band = 1U; band < (RIVER_KWS_FFT_BINS - 1U); ++band) {
        float real = (float)context->fft_output[band * 2U];
        float imag = (float)context->fft_output[band * 2U + 1U];
        context->power_bins[band] = real * real + imag * imag;
    }
    context->power_bins[RIVER_KWS_FFT_BINS - 1U] =
        (float)context->fft_output[RIVER_KWS_WINDOW_SAMPLES] *
        (float)context->fft_output[RIVER_KWS_WINDOW_SAMPLES];

    for (band = 0U; band < RIVER_KWS_MEL_BINS; ++band) {
        uint32_t bin;
        float energy = 0.0f;
        const uint32_t start_bin = context->mel_start_bin[band];
        const uint32_t center_bin = context->mel_center_bin[band];
        const uint32_t end_bin = context->mel_end_bin[band];

        for (bin = start_bin; bin < center_bin; ++bin) {
            float denom = (float)(center_bin - start_bin);
            float weight = denom > 0.0f ? ((float)(bin - start_bin) / denom) : 0.0f;
            energy += context->power_bins[bin] * weight;
        }
        for (bin = center_bin; bin <= end_bin; ++bin) {
            float denom = (float)(end_bin - center_bin);
            float weight = denom > 0.0f ? ((float)(end_bin - bin) / denom) : 0.0f;
            if (weight < 0.0f) {
                weight = 0.0f;
            }
            energy += context->power_bins[bin] * weight;
        }
        energy *= context->mel_band_norm[band];
        mel_frame[band] = 10.0f * log10f(fmaxf(energy, 1.0e-10f));
    }

    river_voice_kws_store_mel_frame(context, mel_frame);
    return RIVER_OK;
}

static void river_voice_kws_fill_input_tensor(river_voice_kws_context_t *context)
{
    uint32_t frame_index;
    uint32_t mel_index;
    float max_db = -1.0e9f;
    uint8_t *dst_u8 = NULL;
    int8_t *dst_i8 = NULL;
    float *dst_f32 = NULL;

    if (!context->tensor_data_drift_logged &&
        context->input_tensor != NULL &&
        context->output_tensor != NULL &&
        (context->input_tensor->data.data != context->input_tensor_data ||
         context->output_tensor->data.data != context->output_tensor_data)) {
        context->tensor_data_drift_logged = true;
        RIVER_LOGW("kws tensor data drift: runtime_input=%p cached_input=%p runtime_output=%p cached_output=%p; use cached",
                   (void *)context->input_tensor->data.data,
                   context->input_tensor_data,
                   (void *)context->output_tensor->data.data,
                   context->output_tensor_data);
    }

    if (context->effective_input_type == kTfLiteUInt8) {
        dst_u8 = (uint8_t *)context->input_tensor_data;
    } else if (context->effective_input_type == kTfLiteInt8) {
        dst_i8 = (int8_t *)context->input_tensor_data;
    } else {
        dst_f32 = (float *)context->input_tensor_data;
    }
    for (frame_index = 0U; frame_index < RIVER_KWS_FEATURE_FRAMES; ++frame_index) {
        uint32_t history_index =
            (context->mel_history_write_index + frame_index) %
            RIVER_KWS_FEATURE_FRAMES;
        for (mel_index = 0U; mel_index < RIVER_KWS_MEL_BINS; ++mel_index) {
            if (context->log_mel_history[history_index][mel_index] > max_db) {
                max_db = context->log_mel_history[history_index][mel_index];
            }
        }
    }

    if (context->input_layout == RIVER_KWS_INPUT_LAYOUT_FRAMES_MELS) {
        for (frame_index = 0U; frame_index < RIVER_KWS_FEATURE_FRAMES; ++frame_index) {
            uint32_t history_index =
                (context->mel_history_write_index + frame_index) %
                RIVER_KWS_FEATURE_FRAMES;
            for (mel_index = 0U; mel_index < RIVER_KWS_MEL_BINS; ++mel_index) {
                float relative_db =
                    context->log_mel_history[history_index][mel_index] - max_db;
                float normalized;
                int quantized;

                if (relative_db < RIVER_KWS_FEATURE_DB_MIN) {
                    relative_db = RIVER_KWS_FEATURE_DB_MIN;
                }
                if (relative_db > 0.0f) {
                    relative_db = 0.0f;
                }
                normalized =
                    (relative_db - RIVER_KWS_FEATURE_MEAN) / RIVER_KWS_FEATURE_STD;
                if (dst_u8 != NULL) {
                    quantized = (int)river_voice_kws_round_to_i32(normalized / context->input_scale) +
                                context->input_zero_point;
                    *dst_u8++ = river_voice_kws_clamp_u8(quantized);
                } else if (dst_i8 != NULL) {
                    quantized = (int)river_voice_kws_round_to_i32(normalized / context->input_scale) +
                                context->input_zero_point;
                    *dst_i8++ = river_voice_kws_clamp_i8(quantized);
                } else if (dst_f32 != NULL) {
                    *dst_f32++ = normalized;
                }
            }
        }
    } else {
        /* BC-ResNet 导出的 NHWC 输入是 [1, 40, 98, 1]，这里直接按目标布局写入，避免额外转置缓冲。 */
        for (mel_index = 0U; mel_index < RIVER_KWS_MEL_BINS; ++mel_index) {
            for (frame_index = 0U; frame_index < RIVER_KWS_FEATURE_FRAMES; ++frame_index) {
                uint32_t history_index =
                    (context->mel_history_write_index + frame_index) %
                    RIVER_KWS_FEATURE_FRAMES;
                float relative_db =
                    context->log_mel_history[history_index][mel_index] - max_db;
                float normalized;
                int quantized;

                if (relative_db < RIVER_KWS_FEATURE_DB_MIN) {
                    relative_db = RIVER_KWS_FEATURE_DB_MIN;
                }
                if (relative_db > 0.0f) {
                    relative_db = 0.0f;
                }
                normalized =
                    (relative_db - RIVER_KWS_FEATURE_MEAN) / RIVER_KWS_FEATURE_STD;
                if (dst_u8 != NULL) {
                    quantized = (int)river_voice_kws_round_to_i32(normalized / context->input_scale) +
                                context->input_zero_point;
                    *dst_u8++ = river_voice_kws_clamp_u8(quantized);
                } else if (dst_i8 != NULL) {
                    quantized = (int)river_voice_kws_round_to_i32(normalized / context->input_scale) +
                                context->input_zero_point;
                    *dst_i8++ = river_voice_kws_clamp_i8(quantized);
                } else if (dst_f32 != NULL) {
                    *dst_f32++ = normalized;
                }
            }
        }
    }
}

static river_status_t river_voice_kws_run_inference(
    river_voice_kws_context_t *context)
{
    float score;

    river_voice_kws_fill_input_tensor(context);
    if (context->interpreter->Invoke() != kTfLiteOk) {
        return RIVER_ERR_IO;
    }

    if (context->effective_output_type == kTfLiteUInt8) {
        const uint8_t *output_u8 = (const uint8_t *)context->output_tensor_data;

        score = ((float)output_u8[0] -
                 (float)context->output_zero_point) *
                context->output_scale;
    } else if (context->effective_output_type == kTfLiteInt8) {
        const int8_t *output_i8 = (const int8_t *)context->output_tensor_data;

        score = ((float)output_i8[0] -
                 (float)context->output_zero_point) *
                context->output_scale;
    } else {
        const float *output_f32 = (const float *)context->output_tensor_data;

        score = output_f32[0];
    }
    if (score < 0.0f) {
        score = 0.0f;
    } else if (score > 1.0f) {
        score = 1.0f;
    }

    context->last_score = score;
    context->last_confidence_q15 =
        (uint32_t)river_voice_kws_round_to_i32(score * 32767.0f);
    context->inference_count++;
    context->gate_inference_count++;
    if (context->last_confidence_q15 > context->gate_best_confidence_q15) {
        context->gate_best_confidence_q15 = context->last_confidence_q15;
    }

    if (context->last_confidence_q15 >= river_voice_kws_score_threshold_q15()) {
        context->hit_streak++;
        context->hit_count++;
    } else {
        context->hit_streak = 0U;
    }

    return RIVER_OK;
}

static void river_voice_kws_maybe_emit_trigger(river_voice_kws_context_t *context)
{
    uint64_t now_ms;
    uint64_t gate_elapsed_ms;
    uint32_t fallback_threshold_q15;

    if (context->gate_triggered) {
        return;
    }
    if (!river_voice_kws_detection_allowed()) {
        return;
    }

    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    if (context->hit_streak >= (uint32_t)CONFIG_RIVER_KWS_TRIGGER_HOLD_FRAMES &&
        context->last_confidence_q15 >= river_voice_kws_score_threshold_q15()) {
        river_voice_kws_emit_trigger(context, context->last_confidence_q15, "threshold");
        return;
    }

    if (context->gate_open) {
        return;
    }

    fallback_threshold_q15 = river_voice_kws_gate_fallback_threshold_q15();
    if (context->gate_best_confidence_q15 < fallback_threshold_q15 ||
        context->gate_inference_count < RIVER_KWS_GATE_FALLBACK_MIN_INFER) {
        return;
    }

    gate_elapsed_ms = river_voice_kws_gate_elapsed_ms(context, now_ms);
    if (gate_elapsed_ms < RIVER_KWS_GATE_FALLBACK_MIN_MS ||
        gate_elapsed_ms > RIVER_KWS_GATE_FALLBACK_MAX_MS) {
        return;
    }

    river_voice_kws_emit_trigger(context,
                                 context->gate_best_confidence_q15,
                                 "gate_fallback");
}

static void river_voice_kws_log_status(river_voice_kws_context_t *context)
{
    uint64_t now_ms;
    uint64_t cooldown_left_ms = 0U;
    uint32_t score_permille;
    uint32_t threshold_permille;
    uint32_t fallback_threshold_permille;
    uint32_t queue_count;
    uint32_t queue_peak;
    uint32_t pre_roll_count;
    uint32_t pre_roll_peak;
    uint32_t gate_best_permille;

    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    if (context->last_status_log_ms != 0U &&
        (now_ms - context->last_status_log_ms) <
            (uint64_t)CONFIG_RIVER_KWS_LOG_PERIOD_MS) {
        return;
    }
    context->last_status_log_ms = now_ms;
    if (now_ms < context->cooldown_until_ms) {
        cooldown_left_ms = context->cooldown_until_ms - now_ms;
    }
    score_permille =
        river_voice_kws_confidence_to_permille(context->last_confidence_q15);
    threshold_permille =
        river_voice_kws_confidence_to_permille(river_voice_kws_score_threshold_q15());
    fallback_threshold_permille =
        river_voice_kws_confidence_to_permille(river_voice_kws_gate_fallback_threshold_q15());
    gate_best_permille =
        river_voice_kws_confidence_to_permille(context->gate_best_confidence_q15);
    queue_count = river_audio_frame_ring_count(&context->input_ring);
    queue_peak = river_audio_frame_ring_peak_count(&context->input_ring);
    pre_roll_count = river_audio_frame_ring_count(&context->pre_roll_ring);
    pre_roll_peak = river_audio_frame_ring_peak_count(&context->pre_roll_ring);

    RIVER_LOGI("kws status: gate=%s ready=%s score_pm=%lu gate_best_pm=%lu thresh_pm=%lu weak_pm=%lu streak=%lu/%u hits=%lu triggers=%lu cooldown_left_ms=%lu window=%lu/%u infer=%lu gate_infer=%lu queue=%lu/%u peak=%lu dropped=%lu pre=%lu/%u pre_peak=%lu pre_dropped=%lu opens=%lu closes=%lu",
               context->gate_open ? "open" : "closed",
               context->window_ready ? "yes" : "no",
               (unsigned long)score_permille,
               (unsigned long)gate_best_permille,
               (unsigned long)threshold_permille,
               (unsigned long)fallback_threshold_permille,
               (unsigned long)context->hit_streak,
               (unsigned int)CONFIG_RIVER_KWS_TRIGGER_HOLD_FRAMES,
               (unsigned long)context->hit_count,
               (unsigned long)context->trigger_count,
               (unsigned long)cooldown_left_ms,
               (unsigned long)context->mel_history_count,
               (unsigned int)RIVER_KWS_FEATURE_FRAMES,
               (unsigned long)context->inference_count,
               (unsigned long)context->gate_inference_count,
               (unsigned long)queue_count,
               (unsigned int)CONFIG_RIVER_KWS_INPUT_QUEUE_FRAMES,
               (unsigned long)queue_peak,
               (unsigned long)context->input_ring_dropped,
               (unsigned long)pre_roll_count,
               (unsigned int)RIVER_KWS_PRE_ROLL_FRAMES,
               (unsigned long)pre_roll_peak,
               (unsigned long)context->pre_roll_ring_dropped,
               (unsigned long)context->gate_open_count,
               (unsigned long)context->gate_close_count);
}

static river_status_t river_voice_kws_process_samples(
    river_voice_kws_context_t *context,
    const int16_t *samples,
    size_t count)
{
    size_t index;

    if (context == NULL || samples == NULL) {
        return RIVER_ERR_ARG;
    }

    for (index = 0U; index < count; ++index) {
        river_voice_kws_push_sample(context, samples[index]);
        if (context->mel_frames_seen == 0U &&
            context->sample_ring_fill_count == RIVER_KWS_WINDOW_SAMPLES) {
            context->hop_samples_pending = 0U;
            if (river_voice_kws_compute_mel_frame(context) != RIVER_OK) {
                return RIVER_ERR_IO;
            }
        }
        while (context->mel_frames_seen > 0U &&
               context->sample_ring_fill_count >= RIVER_KWS_WINDOW_SAMPLES &&
               context->hop_samples_pending >= RIVER_KWS_HOP_SAMPLES) {
            context->hop_samples_pending -= RIVER_KWS_HOP_SAMPLES;
            if (river_voice_kws_compute_mel_frame(context) != RIVER_OK) {
                return RIVER_ERR_IO;
            }
            if (context->window_ready) {
                context->inference_stride_count++;
                if (context->inference_stride_count >=
                    (uint32_t)CONFIG_RIVER_KWS_INFERENCE_STRIDE_FRAMES) {
                    context->inference_stride_count = 0U;
                    if (river_voice_kws_run_inference(context) != RIVER_OK) {
                        return RIVER_ERR_IO;
                    }
                    river_voice_kws_maybe_emit_trigger(context);
                    river_voice_kws_log_status(context);
                }
            }
        }
    }

    return RIVER_OK;
}

static river_status_t river_voice_kws_enqueue_item(
    river_voice_kws_context_t *context,
    const river_voice_kws_queue_item_t *item)
{
    river_status_t status;

    if (context == NULL || item == NULL || !context->input_ring.initialized) {
        return RIVER_ERR_ARG;
    }

    status = river_audio_frame_ring_write(
        &context->input_ring,
        reinterpret_cast<const uint8_t *>(item));
    if (status == RIVER_OK) {
        return RIVER_OK;
    }
    if (status != RIVER_ERR_NO_MEMORY) {
        return status;
    }

    if (river_audio_frame_ring_read(&context->input_ring,
                                    reinterpret_cast<uint8_t *>(&context->input_drop_item)) !=
        RIVER_OK) {
        return RIVER_ERR_BUSY;
    }

    status = river_audio_frame_ring_write(
        &context->input_ring,
        reinterpret_cast<const uint8_t *>(item));
    if (status != RIVER_OK) {
        return status;
    }

    if (context->input_drop_item.type == RIVER_KWS_QUEUE_ITEM_PCM) {
        context->input_ring_dropped++;
    } else {
        RIVER_LOGW("kws queue dropped control item: type=%lu",
                   (unsigned long)context->input_drop_item.type);
    }
    return RIVER_OK;
}

static river_status_t river_voice_kws_enqueue_pcm(
    river_voice_kws_context_t *context,
    const uint8_t *data,
    size_t bytes)
{
    river_voice_kws_queue_item_t item;

    if (context == NULL || data == NULL || bytes != RIVER_KWS_INPUT_FRAME_BYTES) {
        return RIVER_ERR_ARG;
    }

    memset(&item, 0, sizeof(item));
    item.type = RIVER_KWS_QUEUE_ITEM_PCM;
    memcpy(item.pcm, data, bytes);
    return river_voice_kws_enqueue_item(context, &item);
}

static river_status_t river_voice_kws_enqueue_reset(
    river_voice_kws_context_t *context)
{
    river_voice_kws_queue_item_t item;

    if (context == NULL) {
        return RIVER_ERR_ARG;
    }

    memset(&item, 0, sizeof(item));
    item.type = RIVER_KWS_QUEUE_ITEM_RESET;
    return river_voice_kws_enqueue_item(context, &item);
}

static river_status_t river_voice_kws_store_pre_roll_frame(
    river_voice_kws_context_t *context,
    const uint8_t *data,
    size_t bytes)
{
    river_status_t status;

    if (context == NULL || data == NULL || bytes != RIVER_KWS_INPUT_FRAME_BYTES ||
        !context->pre_roll_ring.initialized) {
        return RIVER_ERR_ARG;
    }

    status = river_audio_frame_ring_write(&context->pre_roll_ring, data);
    if (status == RIVER_OK) {
        return RIVER_OK;
    }
    if (status != RIVER_ERR_NO_MEMORY) {
        return status;
    }

    if (river_audio_frame_ring_read(&context->pre_roll_ring,
                                    context->pre_roll_drop_frame) != RIVER_OK) {
        return RIVER_ERR_BUSY;
    }

    status = river_audio_frame_ring_write(&context->pre_roll_ring, data);
    if (status != RIVER_OK) {
        return status;
    }

    context->pre_roll_ring_dropped++;
    return RIVER_OK;
}

static river_status_t river_voice_kws_flush_pre_roll(
    river_voice_kws_context_t *context)
{
    river_status_t status;
    uint32_t flushed = 0U;

    if (context == NULL || !context->pre_roll_ring.initialized) {
        return RIVER_ERR_ARG;
    }

    while (true) {
        status = river_audio_frame_ring_read(&context->pre_roll_ring,
                                             context->pre_roll_frame);
        if (status == RIVER_ERR_NOT_FOUND) {
            break;
        }
        if (status != RIVER_OK) {
            return status;
        }

        status = river_voice_kws_enqueue_pcm(context,
                                             context->pre_roll_frame,
                                             sizeof(context->pre_roll_frame));
        if (status != RIVER_OK) {
            return status;
        }
        flushed++;
    }

    if (flushed > 0U) {
        context->pre_roll_flush_count++;
    }
    return RIVER_OK;
}

static void river_voice_kws_task(void *arg)
{
    river_voice_kws_context_t *context =
        (river_voice_kws_context_t *)arg;

    if (context == NULL) {
        rtos_task_delete(NULL);
        return;
    }

    while (true) {
        river_status_t status;

        if (context->task_stop_requested &&
            river_audio_frame_ring_count(&context->input_ring) == 0U) {
            break;
        }

        status = river_audio_frame_ring_read(&context->input_ring,
                                             reinterpret_cast<uint8_t *>(&context->input_task_item));
        if (status == RIVER_OK) {
            if (context->input_task_item.type == RIVER_KWS_QUEUE_ITEM_RESET) {
                river_voice_kws_reset_frontend(context);
                continue;
            }
            if (context->input_task_item.type != RIVER_KWS_QUEUE_ITEM_PCM) {
                RIVER_LOGW("kws worker unknown item: type=%lu",
                           (unsigned long)context->input_task_item.type);
                continue;
            }

            status = river_voice_kws_process_samples(
                context,
                reinterpret_cast<const int16_t *>(context->input_task_item.pcm),
                RIVER_KWS_INPUT_FRAME_SAMPLES);
            if (status != RIVER_OK) {
                RIVER_LOGW("kws worker process failed: status=%d", (int)status);
            }
            continue;
        }

        if (status != RIVER_ERR_NOT_FOUND) {
            RIVER_LOGW("kws worker ring read failed: status=%d", (int)status);
        }
        rtos_time_delay_ms(RIVER_KWS_TASK_IDLE_DELAY_MS);
    }

    context->task_running = false;
    context->task = 0;
    rtos_task_delete(NULL);
}

extern "C" river_status_t river_voice_kws_init(void)
{
    river_status_t status;
    size_t input_value_count;
    size_t input_elements;
    size_t output_elements;
    size_t input_bytes_min;
    size_t output_bytes_min;
    uintptr_t fft_input_alignment;
    uintptr_t fft_output_alignment;
    const void *input_tensor_data;
    const void *output_tensor_data;
    const char *input_quant_source;
    const char *output_quant_source;
    const char *input_shape_source;
    const char *output_shape_source;
    uint32_t output_shape[4];
    size_t output_dim_count = 0U;
    size_t output_value_count = 0U;

    if (g_river_voice_kws != NULL) {
        return RIVER_OK;
    }

    g_river_voice_kws = (river_voice_kws_context_t *)river_voice_kws_alloc_aligned(
        sizeof(*g_river_voice_kws),
        RIVER_KWS_ALLOCATION_ALIGNMENT,
        &g_river_voice_kws_allocation_from_heap_types,
        &g_river_voice_kws_allocation);
    if (g_river_voice_kws == NULL) {
        return RIVER_ERR_NO_MEMORY;
    }
    memset(g_river_voice_kws, 0, sizeof(*g_river_voice_kws));

    g_river_voice_kws->real_fft = WebRtcSpl_CreateRealFFT(9);
    if (g_river_voice_kws->real_fft == NULL) {
        if (g_river_voice_kws_allocation_from_heap_types) {
            rtos_heap_types_free(g_river_voice_kws_allocation);
        } else {
            rtos_mem_free(g_river_voice_kws_allocation);
        }
        g_river_voice_kws_allocation = NULL;
        g_river_voice_kws_allocation_from_heap_types = false;
        g_river_voice_kws = NULL;
        return RIVER_ERR_NO_MEMORY;
    }

    river_voice_kws_prepare_hann_window(g_river_voice_kws);
    river_voice_kws_prepare_mel_bands(g_river_voice_kws);

    g_river_voice_kws->tensor_arena =
        (uint8_t *)river_voice_kws_alloc_aligned(RIVER_KWS_TENSOR_ARENA_BYTES,
                                                 RIVER_KWS_ALLOCATION_ALIGNMENT,
                                                 &g_river_voice_kws->tensor_arena_from_heap_types,
                                                 &g_river_voice_kws->tensor_arena_allocation);
    if (g_river_voice_kws->tensor_arena == NULL) {
        WebRtcSpl_FreeRealFFT(g_river_voice_kws->real_fft);
        if (g_river_voice_kws_allocation_from_heap_types) {
            rtos_heap_types_free(g_river_voice_kws_allocation);
        } else {
            rtos_mem_free(g_river_voice_kws_allocation);
        }
        g_river_voice_kws_allocation = NULL;
        g_river_voice_kws_allocation_from_heap_types = false;
        g_river_voice_kws = NULL;
        return RIVER_ERR_NO_MEMORY;
    }

    g_river_voice_kws->model = tflite::GetModel(RIVER_KWS_MODEL_DATA);
    if (g_river_voice_kws->model == NULL ||
        g_river_voice_kws->model->version() != TFLITE_SCHEMA_VERSION) {
        RIVER_LOGE("kws model/schema unsupported: model=%p model_version=%d schema=%d model_bytes=%lu",
                   (void *)g_river_voice_kws->model,
                   g_river_voice_kws->model != NULL ? g_river_voice_kws->model->version() : -1,
                   (int)TFLITE_SCHEMA_VERSION,
                   (unsigned long)RIVER_KWS_MODEL_DATA_LEN);
        status = RIVER_ERR_UNSUPPORTED;
        goto fail;
    }

    new (&g_river_voice_kws->op_resolver) river_voice_kws_op_resolver_t();
    g_river_voice_kws->resolver_constructed = true;
    status = river_voice_kws_register_ops(&g_river_voice_kws->op_resolver);
    if (status != RIVER_OK) {
        RIVER_LOGE("kws op resolver registration failed: status=%d op_capacity=%u",
                   (int)status,
                   (unsigned int)RIVER_KWS_OP_COUNT);
        goto fail;
    }

    g_river_voice_kws->interpreter = new (&g_river_voice_kws->interpreter_storage)
        tflite::MicroInterpreter(g_river_voice_kws->model,
                                 g_river_voice_kws->op_resolver,
                                 g_river_voice_kws->tensor_arena,
                                 RIVER_KWS_TENSOR_ARENA_BYTES);
    if (g_river_voice_kws->interpreter->AllocateTensors() != kTfLiteOk) {
        RIVER_LOGE("kws AllocateTensors failed: arena=%uKB model=%luB",
                   (unsigned int)CONFIG_RIVER_KWS_TENSOR_ARENA_KB,
                   (unsigned long)RIVER_KWS_MODEL_DATA_LEN);
        status = RIVER_ERR_NO_MEMORY;
        goto fail;
    }

    g_river_voice_kws->input_tensor = g_river_voice_kws->interpreter->input(0);
    g_river_voice_kws->output_tensor = g_river_voice_kws->interpreter->output(0);
    if (g_river_voice_kws->input_tensor == NULL ||
        g_river_voice_kws->output_tensor == NULL) {
        RIVER_LOGE("kws tensor type unsupported: input=%p type=%d output=%p type=%d expected=%d/%d",
                   (void *)g_river_voice_kws->input_tensor,
                   g_river_voice_kws->input_tensor != NULL ? (int)g_river_voice_kws->input_tensor->type : -1,
                   (void *)g_river_voice_kws->output_tensor,
                   g_river_voice_kws->output_tensor != NULL ? (int)g_river_voice_kws->output_tensor->type : -1,
                   (int)kTfLiteUInt8,
                   (int)kTfLiteFloat32);
        status = RIVER_ERR_UNSUPPORTED;
        goto fail;
    }
    g_river_voice_kws->model_input_type =
        river_voice_kws_model_io_type(g_river_voice_kws->model, true);
    g_river_voice_kws->model_output_type =
        river_voice_kws_model_io_type(g_river_voice_kws->model, false);
    g_river_voice_kws->effective_input_type =
        river_voice_kws_effective_tensor_type(g_river_voice_kws->input_tensor,
                                              g_river_voice_kws->model_input_type);
    g_river_voice_kws->effective_output_type =
        river_voice_kws_effective_tensor_type(g_river_voice_kws->output_tensor,
                                              g_river_voice_kws->model_output_type);
    if ((g_river_voice_kws->effective_input_type != kTfLiteUInt8 &&
         g_river_voice_kws->effective_input_type != kTfLiteInt8 &&
         g_river_voice_kws->effective_input_type != kTfLiteFloat32) ||
        (g_river_voice_kws->effective_output_type != kTfLiteUInt8 &&
         g_river_voice_kws->effective_output_type != kTfLiteInt8 &&
         g_river_voice_kws->effective_output_type != kTfLiteFloat32)) {
        RIVER_LOGE("kws tensor type unsupported: runtime_in=%d runtime_out=%d model_in=%d model_out=%d effective_in=%d effective_out=%d",
                   (int)g_river_voice_kws->input_tensor->type,
                   (int)g_river_voice_kws->output_tensor->type,
                   (int)g_river_voice_kws->model_input_type,
                   (int)g_river_voice_kws->model_output_type,
                   (int)g_river_voice_kws->effective_input_type,
                   (int)g_river_voice_kws->effective_output_type);
        status = RIVER_ERR_UNSUPPORTED;
        goto fail;
    }

    river_voice_kws_resolve_quant_params(g_river_voice_kws->model,
                                         g_river_voice_kws->input_tensor,
                                         true,
                                         g_river_voice_kws->effective_input_type,
                                         &g_river_voice_kws->input_scale,
                                         &g_river_voice_kws->input_zero_point,
                                         &input_quant_source);
    river_voice_kws_resolve_quant_params(g_river_voice_kws->model,
                                         g_river_voice_kws->output_tensor,
                                         false,
                                         g_river_voice_kws->effective_output_type,
                                         &g_river_voice_kws->output_scale,
                                         &g_river_voice_kws->output_zero_point,
                                         &output_quant_source);
    if (((g_river_voice_kws->effective_input_type == kTfLiteUInt8 ||
          g_river_voice_kws->effective_input_type == kTfLiteInt8) &&
         g_river_voice_kws->input_scale <= 0.0f) ||
        ((g_river_voice_kws->effective_output_type == kTfLiteUInt8 ||
          g_river_voice_kws->effective_output_type == kTfLiteInt8) &&
         g_river_voice_kws->output_scale <= 0.0f)) {
        RIVER_LOGE("kws quant params invalid: in_src=%s out_src=%s input_scale_u6=%ld input_zp=%ld output_scale_u6=%ld output_zp=%ld runtime_input_scale_u6=%ld runtime_input_zp=%ld runtime_output_scale_u6=%ld runtime_output_zp=%ld",
                   input_quant_source,
                   output_quant_source,
                   (long)lroundf(g_river_voice_kws->input_scale * 1000000.0f),
                   (long)g_river_voice_kws->input_zero_point,
                   (long)lroundf(g_river_voice_kws->output_scale * 1000000.0f),
                   (long)g_river_voice_kws->output_zero_point,
                   (long)lroundf(g_river_voice_kws->input_tensor->params.scale * 1000000.0f),
                   (long)g_river_voice_kws->input_tensor->params.zero_point,
                   (long)lroundf(g_river_voice_kws->output_tensor->params.scale * 1000000.0f),
                   (long)g_river_voice_kws->output_tensor->params.zero_point);
        status = RIVER_ERR_UNSUPPORTED;
        goto fail;
    }
    if (!river_voice_kws_detect_input_layout(g_river_voice_kws->model,
                                             g_river_voice_kws->input_tensor,
                                             &g_river_voice_kws->input_layout,
                                             g_river_voice_kws->input_shape,
                                             &input_shape_source)) {
        RIVER_LOGE("kws input shape unsupported: runtime_in=%s model_in=%s input_bytes=%lu",
                   river_voice_kws_tensor_type_name(g_river_voice_kws->input_tensor->type),
                   river_voice_kws_tensor_type_name(g_river_voice_kws->model_input_type),
                   (unsigned long)g_river_voice_kws->input_tensor->bytes);
        status = RIVER_ERR_UNSUPPORTED;
        goto fail;
    }
    if (!river_voice_kws_resolve_io_shape(g_river_voice_kws->model,
                                          g_river_voice_kws->output_tensor,
                                          false,
                                          output_shape,
                                          &output_dim_count,
                                          &output_shape_source) ||
        !river_voice_kws_shape_element_count(output_shape,
                                             output_dim_count,
                                             &output_value_count) ||
        output_value_count != 1U) {
        RIVER_LOGE("kws output shape unsupported: runtime_out=%s model_out=%s output_bytes=%lu",
                   river_voice_kws_tensor_type_name(g_river_voice_kws->output_tensor->type),
                   river_voice_kws_tensor_type_name(g_river_voice_kws->model_output_type),
                   (unsigned long)g_river_voice_kws->output_tensor->bytes);
        status = RIVER_ERR_UNSUPPORTED;
        goto fail;
    }

    input_value_count = RIVER_KWS_EXPECTED_INPUT_VALUES;
    input_elements = input_value_count;
    output_elements = 1U;
    input_bytes_min =
        input_elements *
        river_voice_kws_tensor_storage_bytes(g_river_voice_kws->effective_input_type);
    output_bytes_min =
        output_elements *
        river_voice_kws_tensor_storage_bytes(g_river_voice_kws->effective_output_type);
    input_tensor_data = (const void *)g_river_voice_kws->input_tensor->data.data;
    output_tensor_data = (const void *)g_river_voice_kws->output_tensor->data.data;
    if (input_tensor_data == NULL || output_tensor_data == NULL) {
        RIVER_LOGE("kws tensor data invalid: input_data=%p output_data=%p input_bytes=%lu output_bytes=%lu",
                   input_tensor_data,
                   output_tensor_data,
                   (unsigned long)g_river_voice_kws->input_tensor->bytes,
                   (unsigned long)g_river_voice_kws->output_tensor->bytes);
        status = RIVER_ERR_UNSUPPORTED;
        goto fail;
    }
    g_river_voice_kws->input_tensor_data = (void *)input_tensor_data;
    g_river_voice_kws->output_tensor_data = (void *)output_tensor_data;
    if (!river_voice_kws_tensor_bytes_sufficient(g_river_voice_kws->input_tensor->bytes,
                                                 input_elements,
                                                 g_river_voice_kws->effective_input_type) ||
        !river_voice_kws_tensor_bytes_sufficient(g_river_voice_kws->output_tensor->bytes,
                                                 output_elements,
                                                 g_river_voice_kws->effective_output_type)) {
        RIVER_LOGW("kws tensor bytes unusual: runtime_in=%s runtime_out=%s model_in=%s model_out=%s effective_in=%s effective_out=%s input_bytes=%lu expect_elem=%lu expect_bytes=%lu input_data=%p output_bytes=%lu expect_elem=%lu expect_bytes=%lu output_data=%p; continue",
                   river_voice_kws_tensor_type_name(g_river_voice_kws->input_tensor->type),
                   river_voice_kws_tensor_type_name(g_river_voice_kws->output_tensor->type),
                   river_voice_kws_tensor_type_name(g_river_voice_kws->model_input_type),
                   river_voice_kws_tensor_type_name(g_river_voice_kws->model_output_type),
                   river_voice_kws_tensor_type_name(g_river_voice_kws->effective_input_type),
                   river_voice_kws_tensor_type_name(g_river_voice_kws->effective_output_type),
                   (unsigned long)g_river_voice_kws->input_tensor->bytes,
                   (unsigned long)input_elements,
                   (unsigned long)input_bytes_min,
                   input_tensor_data,
                   (unsigned long)g_river_voice_kws->output_tensor->bytes,
                   (unsigned long)output_elements,
                   (unsigned long)output_bytes_min,
                   output_tensor_data);
    }

    fft_input_alignment =
        (uintptr_t)g_river_voice_kws->fft_input &
        (uintptr_t)(RIVER_KWS_ALLOCATION_ALIGNMENT - 1U);
    fft_output_alignment =
        (uintptr_t)g_river_voice_kws->fft_output &
        (uintptr_t)(RIVER_KWS_ALLOCATION_ALIGNMENT - 1U);
    if (fft_input_alignment != 0U || fft_output_alignment != 0U) {
        RIVER_LOGE("kws fft buffer alignment invalid: fft_in=%p mod=%lu fft_out=%p mod=%lu required=%u",
                   (void *)g_river_voice_kws->fft_input,
                   (unsigned long)fft_input_alignment,
                   (void *)g_river_voice_kws->fft_output,
                   (unsigned long)fft_output_alignment,
                   (unsigned int)RIVER_KWS_ALLOCATION_ALIGNMENT);
        status = RIVER_ERR_UNSUPPORTED;
        goto fail;
    }

    status = river_audio_frame_ring_init_with_storage_ex(
        &g_river_voice_kws->pre_roll_ring,
        g_river_voice_kws->pre_roll_ring_storage,
        sizeof(g_river_voice_kws->pre_roll_ring_storage),
        RIVER_KWS_INPUT_FRAME_BYTES,
        RIVER_KWS_PRE_ROLL_FRAMES,
        RIVER_AUDIO_FRAME_RING_MODE_LOCKED);
    if (status != RIVER_OK) {
        RIVER_LOGE("kws pre-roll ring init failed: status=%d frame_bytes=%u frames=%u",
                   (int)status,
                   (unsigned int)RIVER_KWS_INPUT_FRAME_BYTES,
                   (unsigned int)RIVER_KWS_PRE_ROLL_FRAMES);
        goto fail;
    }

    status = river_audio_frame_ring_init_with_storage_ex(
        &g_river_voice_kws->input_ring,
        g_river_voice_kws->input_ring_storage,
        sizeof(g_river_voice_kws->input_ring_storage),
        sizeof(river_voice_kws_queue_item_t),
        CONFIG_RIVER_KWS_INPUT_QUEUE_FRAMES,
        RIVER_AUDIO_FRAME_RING_MODE_SPSC);
    if (status != RIVER_OK) {
        RIVER_LOGE("kws worker ring init failed: status=%d frame_bytes=%u frames=%u",
                   (int)status,
                   (unsigned int)sizeof(river_voice_kws_queue_item_t),
                   (unsigned int)CONFIG_RIVER_KWS_INPUT_QUEUE_FRAMES);
        goto fail;
    }

    if (rtos_task_create(&g_river_voice_kws->task,
                         "river_kws",
                         river_voice_kws_task,
                         g_river_voice_kws,
                         RIVER_KWS_TASK_STACK,
                         RIVER_KWS_TASK_PRIORITY) != RTK_SUCCESS) {
        RIVER_LOGE("create kws worker task failed");
        status = RIVER_ERR_NO_MEMORY;
        goto fail;
    }
    g_river_voice_kws->task_running = true;

    g_river_voice_kws->initialized = true;
    RIVER_LOGI("kws tensor io: runtime_in=%s runtime_out=%s model_in=%s model_out=%s effective_in=%s effective_out=%s",
               river_voice_kws_tensor_type_name(g_river_voice_kws->input_tensor->type),
               river_voice_kws_tensor_type_name(g_river_voice_kws->output_tensor->type),
               river_voice_kws_tensor_type_name(g_river_voice_kws->model_input_type),
               river_voice_kws_tensor_type_name(g_river_voice_kws->model_output_type),
               river_voice_kws_tensor_type_name(g_river_voice_kws->effective_input_type),
               river_voice_kws_tensor_type_name(g_river_voice_kws->effective_output_type));
    RIVER_LOGI("kws quant: in_src=%s scale_u6=%ld zp=%ld out_src=%s scale_u6=%ld zp=%ld",
               input_quant_source,
               (long)lroundf(g_river_voice_kws->input_scale * 1000000.0f),
               (long)g_river_voice_kws->input_zero_point,
               output_quant_source,
               (long)lroundf(g_river_voice_kws->output_scale * 1000000.0f),
               (long)g_river_voice_kws->output_zero_point);
    RIVER_LOGI("kws input shape: src=%s dims=[%lu,%lu,%lu,%lu] layout=%s",
               input_shape_source,
               (unsigned long)g_river_voice_kws->input_shape[0],
               (unsigned long)g_river_voice_kws->input_shape[1],
               (unsigned long)g_river_voice_kws->input_shape[2],
               (unsigned long)g_river_voice_kws->input_shape[3],
               river_voice_kws_input_layout_name(g_river_voice_kws->input_layout));
    RIVER_LOGI("kws output shape: src=%s dims=[%lu,%lu,%lu,%lu] values=%lu",
               output_shape_source,
               (unsigned long)output_shape[0],
               (unsigned long)output_shape[1],
               (unsigned long)output_shape[2],
               (unsigned long)output_shape[3],
               (unsigned long)output_value_count);
    RIVER_LOGI("kws alloc: ctx=%p ctx_raw=%p arena=%p arena_raw=%p align=%u input_bytes=%lu output_bytes=%lu",
               (void *)g_river_voice_kws,
               g_river_voice_kws_allocation,
               (void *)g_river_voice_kws->tensor_arena,
               g_river_voice_kws->tensor_arena_allocation,
               (unsigned int)RIVER_KWS_ALLOCATION_ALIGNMENT,
               (unsigned long)g_river_voice_kws->input_tensor->bytes,
               (unsigned long)g_river_voice_kws->output_tensor->bytes);
    RIVER_LOGI("kws tensor data: input=%p output=%p",
               input_tensor_data,
               output_tensor_data);
    RIVER_LOGI("kws fft buffers: in=%p out=%p align=%u",
               (void *)g_river_voice_kws->fft_input,
               (void *)g_river_voice_kws->fft_output,
               (unsigned int)RIVER_KWS_ALLOCATION_ALIGNMENT);
    RIVER_LOGI("kws worker: priority=%u stack=%uB queue=%u frame=%uB",
               (unsigned int)RIVER_KWS_TASK_PRIORITY,
               (unsigned int)RIVER_KWS_TASK_STACK,
               (unsigned int)CONFIG_RIVER_KWS_INPUT_QUEUE_FRAMES,
               (unsigned int)RIVER_KWS_INPUT_FRAME_BYTES);
    return RIVER_OK;

fail:
    if (g_river_voice_kws != NULL) {
        uint32_t wait_count;

        if (g_river_voice_kws->task_running) {
            g_river_voice_kws->task_stop_requested = true;
            for (wait_count = 0U; wait_count < 100U; ++wait_count) {
                if (!g_river_voice_kws->task_running) {
                    break;
                }
                rtos_time_delay_ms(10U);
            }
        }
        if (g_river_voice_kws->input_ring.initialized) {
            river_audio_frame_ring_deinit(&g_river_voice_kws->input_ring);
        }
        if (g_river_voice_kws->pre_roll_ring.initialized) {
            river_audio_frame_ring_deinit(&g_river_voice_kws->pre_roll_ring);
        }
        if (g_river_voice_kws->interpreter != NULL) {
            g_river_voice_kws->interpreter->~MicroInterpreter();
        }
        if (g_river_voice_kws->resolver_constructed) {
            g_river_voice_kws->op_resolver.~river_voice_kws_op_resolver_t();
        }
        if (g_river_voice_kws->tensor_arena != NULL) {
            if (g_river_voice_kws->tensor_arena_from_heap_types) {
                rtos_heap_types_free(g_river_voice_kws->tensor_arena_allocation);
            } else {
                rtos_mem_free(g_river_voice_kws->tensor_arena_allocation);
            }
        }
        if (g_river_voice_kws->real_fft != NULL) {
            WebRtcSpl_FreeRealFFT(g_river_voice_kws->real_fft);
        }
        if (g_river_voice_kws_allocation_from_heap_types) {
            rtos_heap_types_free(g_river_voice_kws_allocation);
        } else {
            rtos_mem_free(g_river_voice_kws_allocation);
        }
        g_river_voice_kws_allocation = NULL;
        g_river_voice_kws_allocation_from_heap_types = false;
        g_river_voice_kws = NULL;
    }
    return status;
}

extern "C" bool river_voice_kws_active(void)
{
    return g_river_voice_kws != NULL && g_river_voice_kws->initialized;
}

extern "C" river_status_t river_voice_kws_submit_frame(const uint8_t *data,
                                                       size_t bytes,
                                                       bool vad_valid,
                                                       bool is_speech)
{
    river_status_t status;
    river_voice_kws_context_t *context = g_river_voice_kws;
    uint32_t pre_roll_frames;
    uint32_t gate_best_permille;
    uint32_t queue_count;

    if (context == NULL || !context->initialized) {
        return RIVER_ERR_NOT_FOUND;
    }
    if (data == NULL || bytes != RIVER_KWS_INPUT_FRAME_BYTES) {
        return RIVER_ERR_ARG;
    }

    if (!river_voice_kws_detection_allowed()) {
        river_voice_kws_disarm(context, true);
        return RIVER_OK;
    }

    if (context->gate_triggered) {
        if (vad_valid && !is_speech) {
            context->gate_triggered = false;
            if (context->gate_open) {
                context->gate_open = false;
                context->gate_close_count++;
                gate_best_permille =
                    (uint32_t)((context->gate_best_confidence_q15 * 1000U) / 32767U);
                queue_count = river_audio_frame_ring_count(&context->input_ring);
                RIVER_LOGI("kws gate close: gate_best_pm=%lu gate_infer=%lu queue=%lu/%u dropped=%lu",
                           (unsigned long)gate_best_permille,
                           (unsigned long)context->gate_inference_count,
                           (unsigned long)queue_count,
                           (unsigned int)CONFIG_RIVER_KWS_INPUT_QUEUE_FRAMES,
                           (unsigned long)context->input_ring_dropped);
            }
            return river_voice_kws_store_pre_roll_frame(context, data, bytes);
        }
        return RIVER_OK;
    }

    if (!context->gate_open) {
        status = river_voice_kws_store_pre_roll_frame(context, data, bytes);
        if (status != RIVER_OK) {
            return status;
        }
        if (!vad_valid || !is_speech) {
            return RIVER_OK;
        }

        pre_roll_frames = river_audio_frame_ring_count(&context->pre_roll_ring);
        status = river_voice_kws_enqueue_reset(context);
        if (status != RIVER_OK) {
            return status;
        }
        status = river_voice_kws_flush_pre_roll(context);
        if (status != RIVER_OK) {
            return status;
        }

        context->gate_open = true;
        context->gate_started_ms = (uint64_t)rtos_time_get_current_system_time_ms();
        context->gate_open_count++;
        queue_count = river_audio_frame_ring_count(&context->input_ring);
        RIVER_LOGI("kws gate open: pre_roll=%lu/%u queue=%lu/%u",
                   (unsigned long)pre_roll_frames,
                   (unsigned int)RIVER_KWS_PRE_ROLL_FRAMES,
                   (unsigned long)queue_count,
                   (unsigned int)CONFIG_RIVER_KWS_INPUT_QUEUE_FRAMES);
        return RIVER_OK;
    }

    if (vad_valid && !is_speech) {
        context->gate_open = false;
        context->gate_close_count++;
        gate_best_permille =
            (uint32_t)((context->gate_best_confidence_q15 * 1000U) / 32767U);
        queue_count = river_audio_frame_ring_count(&context->input_ring);
        RIVER_LOGI("kws gate close: gate_best_pm=%lu gate_infer=%lu queue=%lu/%u dropped=%lu",
                   (unsigned long)gate_best_permille,
                   (unsigned long)context->gate_inference_count,
                   (unsigned long)queue_count,
                   (unsigned int)CONFIG_RIVER_KWS_INPUT_QUEUE_FRAMES,
                   (unsigned long)context->input_ring_dropped);
        return river_voice_kws_store_pre_roll_frame(context, data, bytes);
    }

    return river_voice_kws_enqueue_pcm(context, data, bytes);
}

extern "C" void river_voice_kws_dump_profile(void)
{
    int32_t mean_milli = (int32_t)lroundf(RIVER_KWS_FEATURE_MEAN * 1000.0f);
    uint32_t std_milli = (uint32_t)lroundf(RIVER_KWS_FEATURE_STD * 1000.0f);
    unsigned long input_dim1 =
        (g_river_voice_kws != NULL && g_river_voice_kws->input_shape[1] != 0U) ?
            (unsigned long)g_river_voice_kws->input_shape[1] :
            (unsigned long)RIVER_KWS_FEATURE_FRAMES;
    unsigned long input_dim2 =
        (g_river_voice_kws != NULL && g_river_voice_kws->input_shape[2] != 0U) ?
            (unsigned long)g_river_voice_kws->input_shape[2] :
            (unsigned long)RIVER_KWS_MEL_BINS;
    unsigned long input_dim3 =
        (g_river_voice_kws != NULL && g_river_voice_kws->input_shape[3] != 0U) ?
            (unsigned long)g_river_voice_kws->input_shape[3] :
            1UL;

    RIVER_LOGI("kws backend: runtime=tflite_micro input=%lux%lux%lu log_mel sr=16k fft=512 hop=160 arena=%uKB model=%luB variant=%s stride=%u threshold_q15=%u hold=%u cooldown_ms=%u gate=vad pre_roll_ms=%u queue=%u",
               input_dim1,
               input_dim2,
               input_dim3,
               (unsigned int)CONFIG_RIVER_KWS_TENSOR_ARENA_KB,
               (unsigned long)RIVER_KWS_MODEL_DATA_LEN,
               RIVER_KWS_MODEL_VARIANT_NAME,
               (unsigned int)CONFIG_RIVER_KWS_INFERENCE_STRIDE_FRAMES,
               (unsigned int)CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15,
               (unsigned int)CONFIG_RIVER_KWS_TRIGGER_HOLD_FRAMES,
               (unsigned int)CONFIG_RIVER_KWS_COOLDOWN_MS,
               (unsigned int)CONFIG_RIVER_KWS_VAD_PRE_ROLL_MS,
               (unsigned int)CONFIG_RIVER_KWS_INPUT_QUEUE_FRAMES);
    RIVER_LOGI("kws frontend: source=fixed_dsb_mono feature=log_mel bins=40 frames=98 norm=global(mean_milli=%ld,std_milli=%lu) wake_text=%s",
               (long)mean_milli,
               (unsigned long)std_milli,
               g_river_voice_kws_text);
}

extern "C" void river_voice_kws_dump_status(void)
{
    if (g_river_voice_kws == NULL) {
        RIVER_LOGI("kws status: closed");
        return;
    }
    river_voice_kws_log_status(g_river_voice_kws);
}
