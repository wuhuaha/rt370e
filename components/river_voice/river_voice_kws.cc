/* 唤醒词后端：完成特征提取、TFLite Micro 推理和触发判定。 */
#include <math.h>
#include <new>
#include <limits.h>
#include <stdio.h>
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

#if defined(CONFIG_RIVER_KWS_MODEL_VARIANT_FP32_EXPERIMENTAL)
#include "generated/bc_resnet_v3_fp32_model_data.h"
#define RIVER_KWS_MODEL_DATA kws_model_fp32
#define RIVER_KWS_MODEL_DATA_LEN kws_model_fp32_len
#define RIVER_KWS_MODEL_VARIANT_NAME "bc_resnet_v3_fp32_experimental"
#elif defined(CONFIG_RIVER_KWS_MODEL_VARIANT_ROUND6_TARGETED_EXPERIMENTAL)
#include "generated/xiaou_student_round6_targeted_int8_model_data.h"
#define RIVER_KWS_MODEL_DATA kws_model_round6_targeted
#define RIVER_KWS_MODEL_DATA_LEN kws_model_round6_targeted_len
#define RIVER_KWS_MODEL_VARIANT_NAME "round6_targeted_experimental"
#else
#include "generated/river_wake_word_model_data.h"
#define RIVER_KWS_MODEL_DATA kws_model
#define RIVER_KWS_MODEL_DATA_LEN kws_model_len
#define RIVER_KWS_MODEL_VARIANT_NAME "bc_resnet_v3_production_final_v2"
#endif

#ifndef CONFIG_RIVER_KWS_MEAN_PATCH_EN
#define CONFIG_RIVER_KWS_MEAN_PATCH_EN 0
#endif

#ifndef CONFIG_RIVER_KWS_LEGACY_MODEL_COMPAT_EN
#define CONFIG_RIVER_KWS_LEGACY_MODEL_COMPAT_EN 0
#endif

#if CONFIG_RIVER_KWS_MEAN_PATCH_EN
#include "river_voice_kws_mean_patch.h"
#endif

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/kernels/internal/tensor_ctypes.h"
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

#ifndef CONFIG_RIVER_KWS_DIAG_VERBOSE_EN
#define CONFIG_RIVER_KWS_DIAG_VERBOSE_EN 0
#endif

#ifndef CONFIG_RIVER_KWS_DIAG_LOG_EVERY_INFER
#define CONFIG_RIVER_KWS_DIAG_LOG_EVERY_INFER 0
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
#define RIVER_KWS_BASE_OP_COUNT 7U
#if CONFIG_RIVER_KWS_LEGACY_MODEL_COMPAT_EN
#define RIVER_KWS_LEGACY_OP_COUNT 2U
#else
#define RIVER_KWS_LEGACY_OP_COUNT 0U
#endif
#define RIVER_KWS_OP_COUNT \
    (RIVER_KWS_BASE_OP_COUNT + RIVER_KWS_LEGACY_OP_COUNT)
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
#define RIVER_KWS_TASK_STACK (1024U * 12U)
#define RIVER_KWS_TASK_PRIORITY 5U
#define RIVER_KWS_TASK_WAIT_MS 100U
#define RIVER_KWS_PRE_ROLL_FLUSH_MAX_FRAMES 8U
#define RIVER_KWS_DIAG_PROBE_COUNT 4U
#define RIVER_KWS_TENSOR_DUMP_HEX_CHUNK_BYTES 64U
#define RIVER_KWS_TENSOR_DUMP_MAX_INPUT_BYTES \
    (RIVER_KWS_EXPECTED_INPUT_VALUES * sizeof(float))
#define RIVER_KWS_TENSOR_DUMP_MAX_OUTPUT_BYTES 64U
/* V3 final docs recommend 0.4 as the high-sensitivity operating point. Keep
 * gate fallback no weaker than that documented floor. */
#define RIVER_KWS_GATE_FALLBACK_THRESHOLD_PM 400U
#define RIVER_KWS_GATE_FALLBACK_MIN_MS 700U
#define RIVER_KWS_GATE_FALLBACK_MAX_MS 2500U
#define RIVER_KWS_GATE_FALLBACK_MIN_INFER 4U
#define RIVER_KWS_SLOW_INFER_WARN_US 10000ULL
#define RIVER_KWS_SLOW_INFER_ALERT_US 20000ULL

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

    TfLiteStatus AddAveragePool2D()
    {
        return AddBuiltin(tflite::BuiltinOperator_AVERAGE_POOL_2D,
                          tflite::Register_AVERAGE_POOL_2D(),
                          tflite::ParsePool);
    }

#if CONFIG_RIVER_KWS_LEGACY_MODEL_COMPAT_EN
    TfLiteStatus AddMean()
    {
        return AddBuiltin(tflite::BuiltinOperator_MEAN, tflite::Register_MEAN(),
                          tflite::ParseReducer);
    }

#if CONFIG_RIVER_KWS_MEAN_PATCH_EN
    TfLiteStatus AddPatchedMean()
    {
        return AddBuiltin(tflite::BuiltinOperator_MEAN,
                          river_voice_kws_RegisterPatchedMean(),
                          tflite::ParseReducer);
    }
#endif

    TfLiteStatus AddFullyConnected()
    {
        return AddBuiltin(tflite::BuiltinOperator_FULLY_CONNECTED,
                          tflite::Register_FULLY_CONNECTED(),
                          tflite::ParseFullyConnected);
    }
#endif

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
    RIVER_KWS_QUEUE_ITEM_PCM = 0U
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
    bool pre_roll_ring_storage_from_heap_types;
    bool input_ring_storage_from_heap_types;
    bool tensor_dump_feature_from_heap_types;
    bool tensor_dump_input_from_heap_types;
    bool tensor_dump_output_from_heap_types;
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
    uint64_t last_infer_us;
    uint64_t max_infer_us;
    uint64_t infer_total_us;
    uint32_t slow_infer_warn_count;
    uint32_t slow_infer_alert_count;
    uint32_t arena_used_bytes;
    uint32_t arena_slack_bytes;
    uint32_t init_heap_before_bytes;
    uint32_t init_heap_after_bytes;
    uint32_t init_heap_min_bytes;
    float last_score;
    float last_feature_min;
    float last_feature_max;
    float last_feature_mean;
    float last_feature_max_db;
    uint32_t last_feature_hash;
    bool last_feature_hash_valid;
    uint32_t same_feature_hash_streak;
    int32_t last_raw_output_scalar;
    bool last_raw_output_valid;
    uint32_t same_raw_output_streak;
    uint32_t last_input_hash;
    bool last_input_hash_valid;
    uint32_t same_input_hash_streak;
    int32_t last_input_value_min;
    int32_t last_input_value_max;
    int32_t last_input_value_mean_milli;
    int32_t last_input_probe_values[RIVER_KWS_DIAG_PROBE_COUNT];
    bool tensor_dump_armed;
    bool tensor_dump_feature_valid;
    bool tensor_dump_snapshot_ready;
    bool local_debug_mode;
    uint32_t tensor_dump_request_count;
    uint32_t tensor_dump_last_seq;
    uint32_t tensor_dump_last_infer;
    uint32_t tensor_dump_capture_seq;
    uint32_t tensor_dump_capture_infer;
    uint32_t tensor_dump_capture_feat_hash;
    uint32_t tensor_dump_capture_input_hash;
    uint32_t tensor_dump_capture_confidence_q15;
    int32_t tensor_dump_capture_raw_output_scalar;
    bool tensor_dump_capture_gate_open;
    float tensor_dump_capture_score;
    size_t tensor_dump_feature_bytes_captured;
    size_t tensor_dump_input_bytes_captured;
    size_t tensor_dump_output_bytes_captured;
    size_t tensor_dump_feature_bytes_reserved;
    size_t tensor_dump_input_bytes_reserved;
    size_t tensor_dump_output_bytes_reserved;
    size_t pre_roll_ring_storage_bytes;
    size_t input_ring_storage_bytes;
    float mel_band_norm[RIVER_KWS_MEL_BINS];
    uint16_t mel_start_bin[RIVER_KWS_MEL_BINS];
    uint16_t mel_center_bin[RIVER_KWS_MEL_BINS];
    uint16_t mel_end_bin[RIVER_KWS_MEL_BINS];
    float hann_window[RIVER_KWS_WINDOW_SAMPLES];
    float log_mel_history[RIVER_KWS_FEATURE_FRAMES][RIVER_KWS_MEL_BINS];
    float *tensor_dump_feature_tensor;
    uint8_t *tensor_dump_input_tensor;
    uint8_t *tensor_dump_output_tensor;
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
    void *pre_roll_ring_storage_allocation;
    void *input_ring_storage_allocation;
    void *tensor_dump_feature_allocation;
    void *tensor_dump_input_allocation;
    void *tensor_dump_output_allocation;
    uint8_t *tensor_arena;
    uint8_t *pre_roll_ring_storage;
    uint8_t *input_ring_storage;
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
    size_t input_tensor_bytes_resolved;
    size_t output_tensor_bytes_resolved;
    float input_scale;
    int input_zero_point;
    float output_scale;
    int output_zero_point;
    bool tensor_data_drift_logged;
    bool task_running;
    bool task_stop_requested;
    bool input_ready_created;
    bool reset_pending;
    rtos_task_t task;
    rtos_sema_t input_ready;
    river_audio_frame_ring_t pre_roll_ring;
    uint32_t pre_roll_ring_dropped;
    uint32_t gate_open_count;
    uint32_t gate_close_count;
    uint32_t pre_roll_flush_count;
    uint32_t pre_roll_trim_count;
    uint32_t pre_roll_trimmed_frames;
    uint8_t pre_roll_frame[RIVER_KWS_INPUT_FRAME_BYTES];
    uint8_t pre_roll_drop_frame[RIVER_KWS_INPUT_FRAME_BYTES];
    river_audio_frame_ring_t input_ring;
    uint32_t input_ring_dropped;
    uint32_t input_trim_count;
    uint32_t input_trimmed_frames;
    uint64_t last_input_trim_log_ms;
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

static inline bool river_voice_kws_diag_verbose_enabled(void)
{
    return CONFIG_RIVER_KWS_DIAG_VERBOSE_EN != 0;
}

static inline bool river_voice_kws_diag_log_every_infer_enabled(void)
{
    return CONFIG_RIVER_KWS_DIAG_LOG_EVERY_INFER != 0;
}

static inline bool river_voice_kws_diag_should_log_streak(uint32_t streak)
{
    return streak > 0U &&
           (streak <= 4U || (streak & (streak - 1U)) == 0U);
}

static uint32_t river_voice_kws_fnv1a32(const uint8_t *data, size_t bytes)
{
    size_t index;
    uint32_t hash = 2166136261UL;

    if (data == NULL) {
        return 0U;
    }

    for (index = 0U; index < bytes; ++index) {
        hash ^= (uint32_t)data[index];
        hash *= 16777619UL;
    }
    return hash;
}

static uint32_t river_voice_kws_fnv1a32_update(uint32_t hash,
                                               const uint8_t *data,
                                               size_t bytes)
{
    size_t index;

    if (data == NULL) {
        return hash;
    }

    for (index = 0U; index < bytes; ++index) {
        hash ^= (uint32_t)data[index];
        hash *= 16777619UL;
    }
    return hash;
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

static const char *river_voice_kws_tensor_dump_buffer_name(
    river_voice_kws_tensor_dump_buffer_t buffer)
{
    switch (buffer) {
    case RIVER_VOICE_KWS_TENSOR_DUMP_FEATURE_F32:
        return "feat_f32";
    case RIVER_VOICE_KWS_TENSOR_DUMP_INPUT_RAW:
        return "input_raw";
    case RIVER_VOICE_KWS_TENSOR_DUMP_OUTPUT_RAW:
        return "output_raw";
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

static const char *river_voice_kws_allocation_type_name(
    TfLiteAllocationType type)
{
    switch (type) {
    case kTfLiteMmapRo:
        return "mmap_ro";
    case kTfLiteArenaRw:
        return "arena_rw";
    case kTfLiteArenaRwPersistent:
        return "arena_rw_persist";
    case kTfLiteDynamic:
        return "dynamic";
    case kTfLitePersistentRo:
        return "persistent_ro";
    case kTfLiteCustom:
        return "custom";
    case kTfLiteNonCpu:
        return "non_cpu";
    case kTfLiteVariantObject:
        return "variant";
    default:
        return "unknown";
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

static void *river_voice_kws_tensor_data_ptr(TfLiteTensor *tensor,
                                             TfLiteType effective_type)
{
    if (tensor == NULL) {
        return NULL;
    }

    switch (effective_type) {
    case kTfLiteUInt8:
        return (void *)tflite::GetTensorData<uint8_t>(tensor);
    case kTfLiteInt8:
        return (void *)tflite::GetTensorData<int8_t>(tensor);
    case kTfLiteFloat32:
        return (void *)tflite::GetTensorData<float>(tensor);
    default:
        return NULL;
    }
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

static void river_voice_kws_free_allocation(void *allocation,
                                            bool from_heap_types)
{
    if (allocation == NULL) {
        return;
    }
    if (from_heap_types) {
        rtos_heap_types_free(allocation);
    } else {
        rtos_mem_free(allocation);
    }
}

static river_status_t river_voice_kws_alloc_runtime_buffer(
    size_t bytes,
    uint8_t **buffer_out,
    void **allocation_out,
    bool *from_heap_types_out)
{
    void *aligned;

    if (buffer_out == NULL || allocation_out == NULL || from_heap_types_out == NULL ||
        bytes == 0U) {
        return RIVER_ERR_ARG;
    }

    *buffer_out = NULL;
    *allocation_out = NULL;
    *from_heap_types_out = false;
    aligned = river_voice_kws_alloc_aligned(bytes,
                                            RIVER_KWS_ALLOCATION_ALIGNMENT,
                                            from_heap_types_out,
                                            allocation_out);
    if (aligned == NULL) {
        return RIVER_ERR_NO_MEMORY;
    }
    *buffer_out = (uint8_t *)aligned;
    return RIVER_OK;
}

static size_t river_voice_kws_tensor_dump_reserved_bytes(
    const river_voice_kws_context_t *context)
{
    if (context == NULL) {
        return 0U;
    }

    return context->tensor_dump_feature_bytes_reserved +
           context->tensor_dump_input_bytes_reserved +
           context->tensor_dump_output_bytes_reserved;
}

static river_status_t river_voice_kws_ensure_tensor_dump_buffers(
    river_voice_kws_context_t *context)
{
    river_status_t status;
    uint8_t *buffer = NULL;

    if (context == NULL) {
        return RIVER_ERR_ARG;
    }

    if (context->tensor_dump_feature_tensor == NULL) {
        status = river_voice_kws_alloc_runtime_buffer(
            context->tensor_dump_feature_bytes_reserved,
            &buffer,
            &context->tensor_dump_feature_allocation,
            &context->tensor_dump_feature_from_heap_types);
        if (status != RIVER_OK) {
            return status;
        }
        context->tensor_dump_feature_tensor = (float *)buffer;
    }
    if (context->tensor_dump_input_tensor == NULL) {
        status = river_voice_kws_alloc_runtime_buffer(
            context->tensor_dump_input_bytes_reserved,
            &context->tensor_dump_input_tensor,
            &context->tensor_dump_input_allocation,
            &context->tensor_dump_input_from_heap_types);
        if (status != RIVER_OK) {
            return status;
        }
    }
    if (context->tensor_dump_output_tensor == NULL) {
        status = river_voice_kws_alloc_runtime_buffer(
            context->tensor_dump_output_bytes_reserved,
            &context->tensor_dump_output_tensor,
            &context->tensor_dump_output_allocation,
            &context->tensor_dump_output_from_heap_types);
        if (status != RIVER_OK) {
            return status;
        }
    }

    return RIVER_OK;
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

static void river_voice_kws_log_perf_status(
    const river_voice_kws_context_t *context)
{
    uint32_t heap_free;
    uint32_t heap_min;
    uint64_t avg_infer_us;

    if (context == NULL) {
        return;
    }

    heap_free = rtos_mem_get_free_heap_size();
    heap_min = rtos_mem_get_minimum_ever_free_heap_size();
    avg_infer_us = context->inference_count == 0U ?
                       0U :
                       (context->infer_total_us / (uint64_t)context->inference_count);

    RIVER_LOGI("kws perf: infer_us[last=%llu avg=%llu max=%llu warn=%lu alert=%lu] heap[now=%lu min=%lu init=%lu->%lu min_init=%lu] mem[arena=%lu/%uKB slack=%lu ctx=%lu pre=%lu queue=%lu dump=%lu] queue[frames=%u stride=%u]",
               (unsigned long long)context->last_infer_us,
               (unsigned long long)avg_infer_us,
               (unsigned long long)context->max_infer_us,
               (unsigned long)context->slow_infer_warn_count,
               (unsigned long)context->slow_infer_alert_count,
               (unsigned long)heap_free,
               (unsigned long)heap_min,
               (unsigned long)context->init_heap_before_bytes,
               (unsigned long)context->init_heap_after_bytes,
               (unsigned long)context->init_heap_min_bytes,
               (unsigned long)context->arena_used_bytes,
               (unsigned int)CONFIG_RIVER_KWS_TENSOR_ARENA_KB,
               (unsigned long)context->arena_slack_bytes,
               (unsigned long)sizeof(*context),
               (unsigned long)context->pre_roll_ring_storage_bytes,
               (unsigned long)context->input_ring_storage_bytes,
               (unsigned long)river_voice_kws_tensor_dump_reserved_bytes(context),
               (unsigned int)CONFIG_RIVER_KWS_INPUT_QUEUE_FRAMES,
               (unsigned int)CONFIG_RIVER_KWS_INFERENCE_STRIDE_FRAMES);
}

static void river_voice_kws_capture_input_diag(river_voice_kws_context_t *context)
{
    static const uint32_t kProbeIndices[RIVER_KWS_DIAG_PROBE_COUNT] = {
        0U,
        39U,
        RIVER_KWS_EXPECTED_INPUT_VALUES / 2U,
        RIVER_KWS_EXPECTED_INPUT_VALUES - 1U
    };
    size_t storage_bytes;
    size_t total_bytes;
    uint32_t hash;
    uint32_t index;
    int32_t value_min = INT_MAX;
    int32_t value_max = INT_MIN;
    int64_t value_sum = 0;

    if (context == NULL || context->input_tensor_data == NULL) {
        return;
    }

    storage_bytes =
        river_voice_kws_tensor_storage_bytes(context->effective_input_type);
    if (storage_bytes == 0U) {
        return;
    }

    total_bytes = (size_t)RIVER_KWS_EXPECTED_INPUT_VALUES * storage_bytes;
    hash = river_voice_kws_fnv1a32((const uint8_t *)context->input_tensor_data,
                                   total_bytes);
    if (context->last_input_hash_valid && context->last_input_hash == hash) {
        context->same_input_hash_streak++;
    } else {
        context->same_input_hash_streak = 1U;
    }
    context->last_input_hash = hash;
    context->last_input_hash_valid = true;

    if (context->effective_input_type == kTfLiteUInt8) {
        const uint8_t *src = (const uint8_t *)context->input_tensor_data;

        for (index = 0U; index < RIVER_KWS_EXPECTED_INPUT_VALUES; ++index) {
            int32_t value = (int32_t)src[index];

            if (value < value_min) {
                value_min = value;
            }
            if (value > value_max) {
                value_max = value;
            }
            value_sum += value;
        }
        for (index = 0U; index < RIVER_KWS_DIAG_PROBE_COUNT; ++index) {
            context->last_input_probe_values[index] =
                (int32_t)src[kProbeIndices[index]];
        }
    } else if (context->effective_input_type == kTfLiteInt8) {
        const int8_t *src = (const int8_t *)context->input_tensor_data;

        for (index = 0U; index < RIVER_KWS_EXPECTED_INPUT_VALUES; ++index) {
            int32_t value = (int32_t)src[index];

            if (value < value_min) {
                value_min = value;
            }
            if (value > value_max) {
                value_max = value;
            }
            value_sum += value;
        }
        for (index = 0U; index < RIVER_KWS_DIAG_PROBE_COUNT; ++index) {
            context->last_input_probe_values[index] =
                (int32_t)src[kProbeIndices[index]];
        }
    } else if (context->effective_input_type == kTfLiteFloat32) {
        const float *src = (const float *)context->input_tensor_data;

        for (index = 0U; index < RIVER_KWS_EXPECTED_INPUT_VALUES; ++index) {
            int32_t milli =
                river_voice_kws_round_to_i32(src[index] * 1000.0f);

            if (milli < value_min) {
                value_min = milli;
            }
            if (milli > value_max) {
                value_max = milli;
            }
            value_sum += milli;
        }
        for (index = 0U; index < RIVER_KWS_DIAG_PROBE_COUNT; ++index) {
            context->last_input_probe_values[index] =
                river_voice_kws_round_to_i32(
                    src[kProbeIndices[index]] * 1000.0f);
        }
    } else {
        return;
    }

    context->last_input_value_min = value_min;
    context->last_input_value_max = value_max;
    context->last_input_value_mean_milli =
        (int32_t)(value_sum / (int64_t)RIVER_KWS_EXPECTED_INPUT_VALUES);
}

static void river_voice_kws_log_inference_diag(river_voice_kws_context_t *context)
{
    bool should_log = false;

    if (!river_voice_kws_diag_verbose_enabled() || context == NULL) {
        return;
    }

    if (river_voice_kws_diag_log_every_infer_enabled()) {
        should_log = true;
    } else if (context->inference_count <= 4U ||
               context->last_confidence_q15 >=
                   river_voice_kws_gate_fallback_threshold_q15() ||
               river_voice_kws_diag_should_log_streak(
                   context->same_feature_hash_streak) ||
               river_voice_kws_diag_should_log_streak(
                   context->same_raw_output_streak) ||
               river_voice_kws_diag_should_log_streak(
                   context->same_input_hash_streak)) {
        should_log = true;
    }

    if (!should_log) {
        return;
    }

    RIVER_LOGI("kws diag: infer=%lu gate=%s out_type=%s raw=%ld score=%.6f q15=%lu same=[raw:%lu feat:%lu input:%lu] feat_hash=0x%08lx input_hash=0x%08lx max_db_milli=%ld feat[min_milli=%ld max_milli=%ld mean_milli=%ld] input[min=%ld max=%ld mean_milli=%ld probes=%ld,%ld,%ld,%ld]",
               (unsigned long)context->inference_count,
               context->gate_open ? "open" : "closed",
               river_voice_kws_tensor_type_name(context->effective_output_type),
               (long)context->last_raw_output_scalar,
               (double)context->last_score,
               (unsigned long)context->last_confidence_q15,
               (unsigned long)context->same_raw_output_streak,
               (unsigned long)context->same_feature_hash_streak,
               (unsigned long)context->same_input_hash_streak,
               (unsigned long)context->last_feature_hash,
               (unsigned long)context->last_input_hash,
               (long)river_voice_kws_round_to_i32(
                   context->last_feature_max_db * 1000.0f),
               (long)river_voice_kws_round_to_i32(
                   context->last_feature_min * 1000.0f),
               (long)river_voice_kws_round_to_i32(
                   context->last_feature_max * 1000.0f),
               (long)river_voice_kws_round_to_i32(
                   context->last_feature_mean * 1000.0f),
               (long)context->last_input_value_min,
               (long)context->last_input_value_max,
               (long)context->last_input_value_mean_milli,
               (long)context->last_input_probe_values[0],
               (long)context->last_input_probe_values[1],
               (long)context->last_input_probe_values[2],
               (long)context->last_input_probe_values[3]);
}

static size_t river_voice_kws_tensor_dump_chunk_count(size_t bytes)
{
    size_t chunk_count =
        (bytes + (size_t)RIVER_KWS_TENSOR_DUMP_HEX_CHUNK_BYTES - 1U) /
        (size_t)RIVER_KWS_TENSOR_DUMP_HEX_CHUNK_BYTES;

    return chunk_count == 0U ? 1U : chunk_count;
}

static void river_voice_kws_log_hex_chunk(const char *label,
                                          uint32_t seq,
                                          const uint8_t *data,
                                          size_t bytes,
                                          uint32_t chunk_index)
{
    size_t offset;
    size_t chunk_bytes;
    size_t total_chunks;
    size_t index;
    char hex[(RIVER_KWS_TENSOR_DUMP_HEX_CHUNK_BYTES * 2U) + 1U];

    if (label == NULL || data == NULL || chunk_index == 0U) {
        return;
    }

    total_chunks = river_voice_kws_tensor_dump_chunk_count(bytes);
    if ((size_t)chunk_index > total_chunks) {
        return;
    }

    offset = ((size_t)chunk_index - 1U) *
             (size_t)RIVER_KWS_TENSOR_DUMP_HEX_CHUNK_BYTES;
    chunk_bytes = bytes > offset ? (bytes - offset) : 0U;
    if (chunk_bytes > (size_t)RIVER_KWS_TENSOR_DUMP_HEX_CHUNK_BYTES) {
        chunk_bytes = (size_t)RIVER_KWS_TENSOR_DUMP_HEX_CHUNK_BYTES;
    }

    for (index = 0U; index < chunk_bytes; ++index) {
        (void)snprintf(&hex[index * 2U],
                       sizeof(hex) - (index * 2U),
                       "%02x",
                       data[offset + index]);
    }
    hex[chunk_bytes * 2U] = '\0';

    RIVER_LOGI("kws tensor dump %s: seq=%lu chunk=%lu/%lu hex=%s",
               label,
               (unsigned long)seq,
               (unsigned long)chunk_index,
               (unsigned long)total_chunks,
               hex);
}

static void river_voice_kws_tensor_dump_snapshot_reset(
    river_voice_kws_context_t *context)
{
    if (context == NULL) {
        return;
    }

    context->tensor_dump_snapshot_ready = false;
    context->tensor_dump_capture_seq = 0U;
    context->tensor_dump_capture_infer = 0U;
    context->tensor_dump_capture_feat_hash = 0U;
    context->tensor_dump_capture_input_hash = 0U;
    context->tensor_dump_capture_confidence_q15 = 0U;
    context->tensor_dump_capture_raw_output_scalar = 0;
    context->tensor_dump_capture_gate_open = false;
    context->tensor_dump_capture_score = 0.0f;
    context->tensor_dump_feature_bytes_captured = 0U;
    context->tensor_dump_input_bytes_captured = 0U;
    context->tensor_dump_output_bytes_captured = 0U;
}

static void river_voice_kws_tensor_dump_log_begin_meta(
    const river_voice_kws_context_t *context)
{
    if (context == NULL || !context->tensor_dump_snapshot_ready) {
        return;
    }

    RIVER_LOGI("kws tensor dump begin: seq=%lu infer=%lu gate=%s in_type=%s out_type=%s layout=%s shape=%lu,%lu,%lu,%lu feat_bytes=%lu input_bytes=%lu output_bytes=%lu",
               (unsigned long)context->tensor_dump_capture_seq,
               (unsigned long)context->tensor_dump_capture_infer,
               context->tensor_dump_capture_gate_open ? "open" : "closed",
               river_voice_kws_tensor_type_name(context->effective_input_type),
               river_voice_kws_tensor_type_name(context->effective_output_type),
               river_voice_kws_input_layout_name(context->input_layout),
               (unsigned long)context->input_shape[0],
               (unsigned long)context->input_shape[1],
               (unsigned long)context->input_shape[2],
               (unsigned long)context->input_shape[3],
               (unsigned long)context->tensor_dump_feature_bytes_captured,
               (unsigned long)context->tensor_dump_input_bytes_captured,
               (unsigned long)context->tensor_dump_output_bytes_captured);
    RIVER_LOGI("kws tensor dump meta: seq=%lu feat_hash=0x%08lx input_hash=0x%08lx raw=%ld score=%.6f q15=%lu in_scale=%.9f in_zp=%ld out_scale=%.9f out_zp=%ld",
               (unsigned long)context->tensor_dump_capture_seq,
               (unsigned long)context->tensor_dump_capture_feat_hash,
               (unsigned long)context->tensor_dump_capture_input_hash,
               (long)context->tensor_dump_capture_raw_output_scalar,
               (double)context->tensor_dump_capture_score,
               (unsigned long)context->tensor_dump_capture_confidence_q15,
               (double)context->input_scale,
               (long)context->input_zero_point,
               (double)context->output_scale,
               (long)context->output_zero_point);
}

static void river_voice_kws_capture_exact_tensors(river_voice_kws_context_t *context)
{
    uint32_t seq;
    size_t input_bytes;
    size_t output_bytes;

    if (context == NULL || !context->tensor_dump_armed ||
        !context->tensor_dump_feature_valid || context->input_tensor == NULL ||
        context->output_tensor == NULL || context->input_tensor_data == NULL ||
        context->output_tensor_data == NULL ||
        context->tensor_dump_feature_tensor == NULL ||
        context->tensor_dump_input_tensor == NULL ||
        context->tensor_dump_output_tensor == NULL) {
        return;
    }

    input_bytes = context->input_tensor_bytes_resolved;
    output_bytes = context->output_tensor_bytes_resolved;
    if (input_bytes == 0U || output_bytes == 0U ||
        input_bytes > context->tensor_dump_input_bytes_reserved ||
        output_bytes > context->tensor_dump_output_bytes_reserved) {
        RIVER_LOGE("kws tensor dump aborted: input_bytes=%lu output_bytes=%lu caps=[%lu,%lu]",
                   (unsigned long)input_bytes,
                   (unsigned long)output_bytes,
                   (unsigned long)context->tensor_dump_input_bytes_reserved,
                   (unsigned long)context->tensor_dump_output_bytes_reserved);
        context->tensor_dump_armed = false;
        context->tensor_dump_feature_valid = false;
        river_voice_kws_tensor_dump_snapshot_reset(context);
        return;
    }

    seq = context->tensor_dump_request_count + 1U;
    context->tensor_dump_request_count = seq;
    context->tensor_dump_last_seq = seq;
    context->tensor_dump_last_infer = context->inference_count;
    context->tensor_dump_capture_seq = seq;
    context->tensor_dump_capture_infer = context->inference_count;
    context->tensor_dump_capture_feat_hash = context->last_feature_hash;
    context->tensor_dump_capture_input_hash = context->last_input_hash;
    context->tensor_dump_capture_raw_output_scalar =
        context->last_raw_output_scalar;
    context->tensor_dump_capture_gate_open = context->gate_open;
    context->tensor_dump_capture_score = context->last_score;
    context->tensor_dump_capture_confidence_q15 =
        context->last_confidence_q15;
    context->tensor_dump_feature_bytes_captured =
        context->tensor_dump_feature_bytes_reserved;
    context->tensor_dump_input_bytes_captured = input_bytes;
    context->tensor_dump_output_bytes_captured = output_bytes;
    (void)memcpy(context->tensor_dump_input_tensor,
                 context->input_tensor_data,
                 input_bytes);
    (void)memcpy(context->tensor_dump_output_tensor,
                 context->output_tensor_data,
                 output_bytes);
    context->tensor_dump_snapshot_ready = true;
    context->tensor_dump_armed = false;
    context->tensor_dump_feature_valid = false;

    RIVER_LOGI("kws tensor dump captured: seq=%lu infer=%lu feat_chunks=%lu input_chunks=%lu output_chunks=%lu",
               (unsigned long)seq,
               (unsigned long)context->inference_count,
               (unsigned long)river_voice_kws_tensor_dump_chunk_count(
                   context->tensor_dump_feature_bytes_captured),
               (unsigned long)river_voice_kws_tensor_dump_chunk_count(
                   context->tensor_dump_input_bytes_captured),
               (unsigned long)river_voice_kws_tensor_dump_chunk_count(
                   context->tensor_dump_output_bytes_captured));
}

static river_status_t river_voice_kws_tensor_dump_buffer_view(
    const river_voice_kws_context_t *context,
    river_voice_kws_tensor_dump_buffer_t buffer,
    const uint8_t **data_out,
    size_t *bytes_out)
{
    if (context == NULL || data_out == NULL || bytes_out == NULL) {
        return RIVER_ERR_ARG;
    }

    switch (buffer) {
    case RIVER_VOICE_KWS_TENSOR_DUMP_FEATURE_F32:
        *data_out = (const uint8_t *)context->tensor_dump_feature_tensor;
        *bytes_out = context->tensor_dump_feature_bytes_captured;
        return RIVER_OK;
    case RIVER_VOICE_KWS_TENSOR_DUMP_INPUT_RAW:
        *data_out = context->tensor_dump_input_tensor;
        *bytes_out = context->tensor_dump_input_bytes_captured;
        return RIVER_OK;
    case RIVER_VOICE_KWS_TENSOR_DUMP_OUTPUT_RAW:
        *data_out = context->tensor_dump_output_tensor;
        *bytes_out = context->tensor_dump_output_bytes_captured;
        return RIVER_OK;
    default:
        return RIVER_ERR_ARG;
    }
}

static const char *river_voice_kws_wake_handoff_block_reason_locked(
    const river_voice_kws_context_t *context)
{
    if (context == NULL || !context->initialized) {
        return NULL;
    }
    if (context->local_debug_mode) {
        return "local_debug";
    }
    if (context->tensor_dump_snapshot_ready) {
        return "tensor_dump_ready";
    }
    return NULL;
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
        resolver->AddAveragePool2D() != kTfLiteOk ||
        resolver->AddLogistic() != kTfLiteOk) {
        return RIVER_ERR_UNSUPPORTED;
    }

#if CONFIG_RIVER_KWS_LEGACY_MODEL_COMPAT_EN
#if CONFIG_RIVER_KWS_MEAN_PATCH_EN
        resolver->AddPatchedMean() != kTfLiteOk ||
#else
        resolver->AddMean() != kTfLiteOk ||
#endif
        resolver->AddFullyConnected() != kTfLiteOk) {
        return RIVER_ERR_UNSUPPORTED;
    }
#endif
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

static void river_voice_kws_drain_input_signal(
    river_voice_kws_context_t *context)
{
    if (context == NULL || !context->input_ready_created) {
        return;
    }

    while (rtos_sema_take(context->input_ready, 0U) == RTK_SUCCESS) {
    }
}

static void river_voice_kws_signal_worker(
    river_voice_kws_context_t *context)
{
    if (context == NULL || !context->input_ready_created) {
        return;
    }

    (void)rtos_sema_give(context->input_ready);
}

static uint32_t river_voice_kws_input_trim_high_water_frames(void)
{
    uint32_t high_water =
        ((uint32_t)CONFIG_RIVER_KWS_INPUT_QUEUE_FRAMES * 3U) / 4U;

    if (high_water == 0U) {
        high_water = 1U;
    }
    return high_water;
}

static uint32_t river_voice_kws_input_trim_target_frames(void)
{
    uint32_t target = (uint32_t)CONFIG_RIVER_KWS_INPUT_QUEUE_FRAMES / 3U;
    uint32_t high_water = river_voice_kws_input_trim_high_water_frames();

    if (target >= high_water) {
        target = (high_water > 0U) ? (high_water - 1U) : 0U;
    }
    return target;
}

static void river_voice_kws_log_input_trim(
    river_voice_kws_context_t *context,
    uint32_t queue_before_trim,
    uint32_t queue_after_trim,
    uint32_t dropped,
    uint32_t target_frames)
{
    uint64_t now_ms;

    if (context == NULL || dropped == 0U) {
        return;
    }

    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    if (context->last_input_trim_log_ms != 0U &&
        (now_ms - context->last_input_trim_log_ms) <
            (uint64_t)CONFIG_RIVER_KWS_LOG_PERIOD_MS) {
        return;
    }

    context->last_input_trim_log_ms = now_ms;
    RIVER_LOGW("kws input trim: dropped=%lu queue=%lu->%lu target=%lu",
               (unsigned long)dropped,
               (unsigned long)queue_before_trim,
               (unsigned long)queue_after_trim,
               (unsigned long)target_frames);
}

static void river_voice_kws_disarm(river_voice_kws_context_t *context,
                                   bool clear_pre_roll)
{
    if (context == NULL) {
        return;
    }

    context->gate_open = false;
    context->gate_triggered = false;
    context->reset_pending = false;
    river_voice_kws_reset_frontend(context);
    if (context->input_ring.initialized) {
        river_audio_frame_ring_reset(&context->input_ring);
    }
    if (clear_pre_roll && context->pre_roll_ring.initialized) {
        river_audio_frame_ring_reset(&context->pre_roll_ring);
    }
    river_voice_kws_drain_input_signal(context);
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

static river_status_t river_voice_kws_fill_input_tensor(
    river_voice_kws_context_t *context)
{
    uint32_t frame_index;
    uint32_t mel_index;
    uint32_t feature_hash = 2166136261UL;
    float max_db = -1.0e9f;
    float feature_min = 1.0e9f;
    float feature_max = -1.0e9f;
    double feature_sum = 0.0;
    uint8_t *dst_u8 = NULL;
    int8_t *dst_i8 = NULL;
    float *dst_f32 = NULL;
    float *dump_feature_dst = NULL;

    if (context == NULL || context->input_tensor_data == NULL) {
        return RIVER_ERR_ARG;
    }

    if (context->effective_input_type == kTfLiteUInt8) {
        dst_u8 = (uint8_t *)context->input_tensor_data;
    } else if (context->effective_input_type == kTfLiteInt8) {
        dst_i8 = (int8_t *)context->input_tensor_data;
    } else {
        dst_f32 = (float *)context->input_tensor_data;
    }
    if (context->tensor_dump_armed &&
        context->tensor_dump_feature_tensor != NULL) {
        dump_feature_dst = context->tensor_dump_feature_tensor;
        context->tensor_dump_feature_valid = false;
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
                int32_t normalized_milli;
                int quantized;

                if (relative_db < RIVER_KWS_FEATURE_DB_MIN) {
                    relative_db = RIVER_KWS_FEATURE_DB_MIN;
                }
                if (relative_db > 0.0f) {
                    relative_db = 0.0f;
                }
                normalized =
                    (relative_db - RIVER_KWS_FEATURE_MEAN) / RIVER_KWS_FEATURE_STD;
                if (normalized < feature_min) {
                    feature_min = normalized;
                }
                if (normalized > feature_max) {
                    feature_max = normalized;
                }
                feature_sum += (double)normalized;
                normalized_milli =
                    river_voice_kws_round_to_i32(normalized * 1000.0f);
                feature_hash = river_voice_kws_fnv1a32_update(
                    feature_hash,
                    (const uint8_t *)&normalized_milli,
                    sizeof(normalized_milli));
                if (dump_feature_dst != NULL) {
                    *dump_feature_dst++ = normalized;
                }
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
                int32_t normalized_milli;
                int quantized;

                if (relative_db < RIVER_KWS_FEATURE_DB_MIN) {
                    relative_db = RIVER_KWS_FEATURE_DB_MIN;
                }
                if (relative_db > 0.0f) {
                    relative_db = 0.0f;
                }
                normalized =
                    (relative_db - RIVER_KWS_FEATURE_MEAN) / RIVER_KWS_FEATURE_STD;
                if (normalized < feature_min) {
                    feature_min = normalized;
                }
                if (normalized > feature_max) {
                    feature_max = normalized;
                }
                feature_sum += (double)normalized;
                normalized_milli =
                    river_voice_kws_round_to_i32(normalized * 1000.0f);
                feature_hash = river_voice_kws_fnv1a32_update(
                    feature_hash,
                    (const uint8_t *)&normalized_milli,
                    sizeof(normalized_milli));
                if (dump_feature_dst != NULL) {
                    *dump_feature_dst++ = normalized;
                }
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

    context->last_feature_max_db = max_db;
    context->last_feature_min = feature_min;
    context->last_feature_max = feature_max;
    context->last_feature_mean =
        (float)(feature_sum / (double)RIVER_KWS_EXPECTED_INPUT_VALUES);
    if (context->last_feature_hash_valid &&
        context->last_feature_hash == feature_hash) {
        context->same_feature_hash_streak++;
    } else {
        context->same_feature_hash_streak = 1U;
    }
    context->last_feature_hash = feature_hash;
    context->last_feature_hash_valid = true;
    if (context->tensor_dump_armed && dump_feature_dst != NULL) {
        context->tensor_dump_feature_valid = true;
    }
    river_voice_kws_capture_input_diag(context);

    return RIVER_OK;
}

static river_status_t river_voice_kws_run_inference(
    river_voice_kws_context_t *context)
{
    float score;
    int32_t raw_scalar = 0;
    uint64_t start_us;
    uint64_t elapsed_us;

    if (river_voice_kws_fill_input_tensor(context) != RIVER_OK) {
        return RIVER_ERR_IO;
    }
    start_us = rtos_time_get_current_system_time_us();
    if (context->interpreter->Invoke() != kTfLiteOk) {
        return RIVER_ERR_IO;
    }
    elapsed_us = rtos_time_get_current_system_time_us() - start_us;

    if (context->effective_output_type == kTfLiteUInt8) {
        const uint8_t *output_u8 = (const uint8_t *)context->output_tensor_data;

        raw_scalar = (int32_t)output_u8[0];
        score = ((float)output_u8[0] -
                 (float)context->output_zero_point) *
                context->output_scale;
    } else if (context->effective_output_type == kTfLiteInt8) {
        const int8_t *output_i8 = (const int8_t *)context->output_tensor_data;

        raw_scalar = (int32_t)output_i8[0];
        score = ((float)output_i8[0] -
                 (float)context->output_zero_point) *
                context->output_scale;
    } else {
        const float *output_f32 = (const float *)context->output_tensor_data;

        raw_scalar = river_voice_kws_round_to_i32(output_f32[0] * 1000.0f);
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
    context->last_infer_us = elapsed_us;
    context->infer_total_us += elapsed_us;
    if (elapsed_us > context->max_infer_us) {
        context->max_infer_us = elapsed_us;
    }
    if (elapsed_us >= RIVER_KWS_SLOW_INFER_WARN_US) {
        context->slow_infer_warn_count++;
    }
    if (elapsed_us >= RIVER_KWS_SLOW_INFER_ALERT_US) {
        context->slow_infer_alert_count++;
    }
    context->inference_count++;
    context->gate_inference_count++;
    if (context->last_raw_output_valid &&
        context->last_raw_output_scalar == raw_scalar) {
        context->same_raw_output_streak++;
    } else {
        context->same_raw_output_streak = 1U;
    }
    context->last_raw_output_scalar = raw_scalar;
    context->last_raw_output_valid = true;
    if (context->last_confidence_q15 > context->gate_best_confidence_q15) {
        context->gate_best_confidence_q15 = context->last_confidence_q15;
    }

    river_voice_kws_log_inference_diag(context);
    river_voice_kws_capture_exact_tensors(context);

    if (elapsed_us >= RIVER_KWS_SLOW_INFER_ALERT_US) {
        RIVER_LOGW("kws infer slow: infer=%lu us=%llu queue=%lu/%u gate=%s score_pm=%lu",
                   (unsigned long)context->inference_count,
                   (unsigned long long)elapsed_us,
                   (unsigned long)river_audio_frame_ring_count(&context->input_ring),
                   (unsigned int)CONFIG_RIVER_KWS_INPUT_QUEUE_FRAMES,
                   context->gate_open ? "open" : "closed",
                   (unsigned long)river_voice_kws_confidence_to_permille(
                       context->last_confidence_q15));
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

    RIVER_LOGI("kws status: gate=%s ready=%s score_pm=%lu gate_best_pm=%lu thresh_pm=%lu weak_pm=%lu streak=%lu/%u hits=%lu triggers=%lu cooldown_left_ms=%lu window=%lu/%u infer=%lu gate_infer=%lu queue=%lu/%u peak=%lu dropped=%lu trim_ops=%lu trim_drop=%lu pre=%lu/%u pre_peak=%lu pre_dropped=%lu opens=%lu closes=%lu last_raw=%ld same=[r:%lu f:%lu i:%lu] last_feat_hash=0x%08lx last_input_hash=0x%08lx",
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
               (unsigned long)context->input_trim_count,
               (unsigned long)context->input_trimmed_frames,
               (unsigned long)pre_roll_count,
               (unsigned int)RIVER_KWS_PRE_ROLL_FRAMES,
               (unsigned long)pre_roll_peak,
               (unsigned long)context->pre_roll_ring_dropped,
               (unsigned long)context->gate_open_count,
               (unsigned long)context->gate_close_count,
               (long)context->last_raw_output_scalar,
               (unsigned long)context->same_raw_output_streak,
               (unsigned long)context->same_feature_hash_streak,
               (unsigned long)context->same_input_hash_streak,
               (unsigned long)context->last_feature_hash,
               (unsigned long)context->last_input_hash);
    river_voice_kws_log_perf_status(context);
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

static uint32_t river_voice_kws_clear_input_queue(
    river_voice_kws_context_t *context)
{
    uint32_t pcm_items_cleared = 0U;

    if (context == NULL || !context->input_ring.initialized) {
        return 0U;
    }

    pcm_items_cleared = river_audio_frame_ring_count(&context->input_ring);
    if (pcm_items_cleared > 0U) {
        river_audio_frame_ring_reset(&context->input_ring);
        context->input_ring_dropped += pcm_items_cleared;
    }

    return pcm_items_cleared;
}

static river_status_t river_voice_kws_trim_input_backlog(
    river_voice_kws_context_t *context)
{
    river_status_t status;
    uint32_t queue_count;
    uint32_t queue_before_trim;
    uint32_t target_frames;
    uint32_t dropped = 0U;

    if (context == NULL || !context->input_ring.initialized) {
        return RIVER_ERR_ARG;
    }

    queue_count = river_audio_frame_ring_count(&context->input_ring);
    if (queue_count < river_voice_kws_input_trim_high_water_frames()) {
        return RIVER_OK;
    }

    queue_before_trim = queue_count;
    target_frames = river_voice_kws_input_trim_target_frames();
    while (queue_count > target_frames) {
        status = river_audio_frame_ring_read(
            &context->input_ring,
            reinterpret_cast<uint8_t *>(&context->input_drop_item));
        if (status == RIVER_ERR_NOT_FOUND) {
            break;
        }
        if (status != RIVER_OK) {
            return status;
        }
        queue_count--;
        dropped++;
        context->input_ring_dropped++;
    }

    if (dropped == 0U) {
        return RIVER_OK;
    }

    context->input_trim_count++;
    context->input_trimmed_frames += dropped;
    river_voice_kws_log_input_trim(context,
                                   queue_before_trim,
                                   queue_count,
                                   dropped,
                                   target_frames);
    return RIVER_OK;
}

static river_status_t river_voice_kws_enqueue_pcm(
    river_voice_kws_context_t *context,
    const uint8_t *data,
    size_t bytes)
{
    river_voice_kws_queue_item_t item;
    river_status_t status;

    if (context == NULL || data == NULL || bytes != RIVER_KWS_INPUT_FRAME_BYTES) {
        return RIVER_ERR_ARG;
    }

    memset(&item, 0, sizeof(item));
    item.type = RIVER_KWS_QUEUE_ITEM_PCM;
    memcpy(item.pcm, data, bytes);
    status = river_voice_kws_trim_input_backlog(context);
    if (status != RIVER_OK) {
        return status;
    }
    status = river_audio_frame_ring_write(
        &context->input_ring,
        reinterpret_cast<const uint8_t *>(&item));
    if (status == RIVER_OK) {
        river_voice_kws_signal_worker(context);
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
    context->input_ring_dropped++;

    status = river_audio_frame_ring_write(
        &context->input_ring,
        reinterpret_cast<const uint8_t *>(&item));
    if (status != RIVER_OK) {
        return status;
    }

    river_voice_kws_signal_worker(context);
    return RIVER_OK;
}

static river_status_t river_voice_kws_enqueue_reset(
    river_voice_kws_context_t *context)
{
    uint32_t cleared_pcm_items;

    if (context == NULL) {
        return RIVER_ERR_ARG;
    }

    cleared_pcm_items = river_voice_kws_clear_input_queue(context);
    context->reset_pending = true;
    river_voice_kws_signal_worker(context);

    if (cleared_pcm_items > 0U) {
        RIVER_LOGI("kws gate rearm cleared stale queue: pcm=%lu",
                   (unsigned long)cleared_pcm_items);
    }
    return RIVER_OK;
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
    uint32_t pending_frames;
    uint32_t trim_frames = 0U;
    uint32_t flush_limit_frames;

    if (context == NULL || !context->pre_roll_ring.initialized) {
        return RIVER_ERR_ARG;
    }

    pending_frames = river_audio_frame_ring_count(&context->pre_roll_ring);
    flush_limit_frames = RIVER_KWS_PRE_ROLL_FLUSH_MAX_FRAMES;
    if (flush_limit_frames == 0U) {
        flush_limit_frames = 1U;
    }
    if (pending_frames > flush_limit_frames) {
        trim_frames = pending_frames - flush_limit_frames;
        while (trim_frames > 0U) {
            status = river_audio_frame_ring_read(&context->pre_roll_ring,
                                                 context->pre_roll_drop_frame);
            if (status != RIVER_OK) {
                return status;
            }
            trim_frames--;
            context->pre_roll_ring_dropped++;
            context->pre_roll_trimmed_frames++;
        }
        context->pre_roll_trim_count++;
        RIVER_LOGI("kws pre-roll trim: dropped=%lu keep=%u/%u",
                   (unsigned long)(pending_frames - flush_limit_frames),
                   (unsigned int)flush_limit_frames,
                   (unsigned int)RIVER_KWS_PRE_ROLL_FRAMES);
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
        bool did_work = false;

        if (context->task_stop_requested &&
            !context->reset_pending &&
            river_audio_frame_ring_count(&context->input_ring) == 0U) {
            break;
        }

        while (true) {
            if (context->reset_pending) {
                context->reset_pending = false;
                river_voice_kws_reset_frontend(context);
                did_work = true;
                continue;
            }

            status = river_audio_frame_ring_read(
                &context->input_ring,
                reinterpret_cast<uint8_t *>(&context->input_task_item));
            if (status != RIVER_OK) {
                break;
            }
            did_work = true;
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
        }

        if (status != RIVER_ERR_NOT_FOUND) {
            RIVER_LOGW("kws worker ring read failed: status=%d", (int)status);
        }
        if (context->task_stop_requested &&
            !context->reset_pending &&
            river_audio_frame_ring_count(&context->input_ring) == 0U) {
            break;
        }
        if (!did_work) {
            if (context->input_ready_created) {
                (void)rtos_sema_take(context->input_ready, RIVER_KWS_TASK_WAIT_MS);
            } else {
                rtos_time_delay_ms(RIVER_KWS_TASK_WAIT_MS);
            }
        }
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
    g_river_voice_kws->init_heap_before_bytes = rtos_mem_get_free_heap_size();
    RIVER_LOGI("kws init plan: heap_free=%lu ctx=%luB arena=%uKB model=%luB align=%u",
               (unsigned long)g_river_voice_kws->init_heap_before_bytes,
               (unsigned long)sizeof(*g_river_voice_kws),
               (unsigned int)CONFIG_RIVER_KWS_TENSOR_ARENA_KB,
               (unsigned long)RIVER_KWS_MODEL_DATA_LEN,
               (unsigned int)RIVER_KWS_ALLOCATION_ALIGNMENT);

    g_river_voice_kws->real_fft = WebRtcSpl_CreateRealFFT(9);
    if (g_river_voice_kws->real_fft == NULL) {
        river_voice_kws_free_allocation(g_river_voice_kws_allocation,
                                        g_river_voice_kws_allocation_from_heap_types);
        g_river_voice_kws_allocation = NULL;
        g_river_voice_kws_allocation_from_heap_types = false;
        g_river_voice_kws = NULL;
        return RIVER_ERR_NO_MEMORY;
    }

    river_voice_kws_prepare_hann_window(g_river_voice_kws);
    river_voice_kws_prepare_mel_bands(g_river_voice_kws);
    RIVER_LOGI("kws init stage: fft_ready heap_free=%lu",
               (unsigned long)rtos_mem_get_free_heap_size());

    g_river_voice_kws->tensor_arena =
        (uint8_t *)river_voice_kws_alloc_aligned(RIVER_KWS_TENSOR_ARENA_BYTES,
                                                 RIVER_KWS_ALLOCATION_ALIGNMENT,
                                                 &g_river_voice_kws->tensor_arena_from_heap_types,
                                                 &g_river_voice_kws->tensor_arena_allocation);
    if (g_river_voice_kws->tensor_arena == NULL) {
        RIVER_LOGE("kws tensor arena alloc failed: arena=%uKB heap_free=%lu",
                   (unsigned int)CONFIG_RIVER_KWS_TENSOR_ARENA_KB,
                   (unsigned long)rtos_mem_get_free_heap_size());
        WebRtcSpl_FreeRealFFT(g_river_voice_kws->real_fft);
        river_voice_kws_free_allocation(g_river_voice_kws_allocation,
                                        g_river_voice_kws_allocation_from_heap_types);
        g_river_voice_kws_allocation = NULL;
        g_river_voice_kws_allocation_from_heap_types = false;
        g_river_voice_kws = NULL;
        return RIVER_ERR_NO_MEMORY;
    }
    RIVER_LOGI("kws init stage: arena_ready heap_free=%lu",
               (unsigned long)rtos_mem_get_free_heap_size());

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
    g_river_voice_kws->arena_used_bytes =
        (uint32_t)g_river_voice_kws->interpreter->arena_used_bytes();
    if (g_river_voice_kws->arena_used_bytes < RIVER_KWS_TENSOR_ARENA_BYTES) {
        g_river_voice_kws->arena_slack_bytes =
            RIVER_KWS_TENSOR_ARENA_BYTES - g_river_voice_kws->arena_used_bytes;
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
    RIVER_LOGI("kws io binding: preserve_all=%s input_idx=%ld type=%s alloc=%s bytes=%lu raw=%p dims=%p var=%d output_idx=%ld type=%s alloc=%s bytes=%lu raw=%p dims=%p var=%d arena_used=%lu arena_slack=%lu",
               g_river_voice_kws->interpreter->preserve_all_tensors() ? "yes" : "no",
               (long)g_river_voice_kws->interpreter->inputs().Get(0),
               river_voice_kws_tensor_type_name(g_river_voice_kws->input_tensor->type),
               river_voice_kws_allocation_type_name(g_river_voice_kws->input_tensor->allocation_type),
               (unsigned long)g_river_voice_kws->input_tensor->bytes,
               g_river_voice_kws->input_tensor->data.raw,
               (void *)g_river_voice_kws->input_tensor->dims,
               g_river_voice_kws->input_tensor->is_variable ? 1 : 0,
               (long)g_river_voice_kws->interpreter->outputs().Get(0),
               river_voice_kws_tensor_type_name(g_river_voice_kws->output_tensor->type),
               river_voice_kws_allocation_type_name(g_river_voice_kws->output_tensor->allocation_type),
               (unsigned long)g_river_voice_kws->output_tensor->bytes,
               g_river_voice_kws->output_tensor->data.raw,
               (void *)g_river_voice_kws->output_tensor->dims,
               g_river_voice_kws->output_tensor->is_variable ? 1 : 0,
               (unsigned long)g_river_voice_kws->arena_used_bytes,
               (unsigned long)g_river_voice_kws->arena_slack_bytes);
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
    input_tensor_data =
        river_voice_kws_tensor_data_ptr(g_river_voice_kws->input_tensor,
                                        g_river_voice_kws->effective_input_type);
    output_tensor_data =
        river_voice_kws_tensor_data_ptr(g_river_voice_kws->output_tensor,
                                        g_river_voice_kws->effective_output_type);
    if (input_tensor_data == NULL || output_tensor_data == NULL) {
        RIVER_LOGE("kws tensor data invalid: input_data=%p output_data=%p input_raw=%p output_raw=%p input_alloc=%s output_alloc=%s input_bytes=%lu output_bytes=%lu input_idx=%ld output_idx=%ld arena_used=%lu arena_slack=%lu",
                   input_tensor_data,
                   output_tensor_data,
                   g_river_voice_kws->input_tensor->data.raw,
                   g_river_voice_kws->output_tensor->data.raw,
                   river_voice_kws_allocation_type_name(g_river_voice_kws->input_tensor->allocation_type),
                   river_voice_kws_allocation_type_name(g_river_voice_kws->output_tensor->allocation_type),
                   (unsigned long)g_river_voice_kws->input_tensor->bytes,
                   (unsigned long)g_river_voice_kws->output_tensor->bytes,
                   (long)g_river_voice_kws->interpreter->inputs().Get(0),
                   (long)g_river_voice_kws->interpreter->outputs().Get(0),
                   (unsigned long)g_river_voice_kws->arena_used_bytes,
                   (unsigned long)g_river_voice_kws->arena_slack_bytes);
        status = RIVER_ERR_UNSUPPORTED;
        goto fail;
    }
    g_river_voice_kws->input_tensor_data = (void *)input_tensor_data;
    g_river_voice_kws->output_tensor_data = (void *)output_tensor_data;
    g_river_voice_kws->input_tensor_bytes_resolved = input_bytes_min;
    g_river_voice_kws->output_tensor_bytes_resolved = output_bytes_min;
    g_river_voice_kws->tensor_dump_feature_bytes_reserved =
        (size_t)RIVER_KWS_EXPECTED_INPUT_VALUES * sizeof(float);
    g_river_voice_kws->tensor_dump_input_bytes_reserved = input_bytes_min;
    g_river_voice_kws->tensor_dump_output_bytes_reserved = output_bytes_min;
    g_river_voice_kws->pre_roll_ring_storage_bytes =
        (size_t)RIVER_KWS_INPUT_FRAME_BYTES * (size_t)RIVER_KWS_PRE_ROLL_FRAMES;
    g_river_voice_kws->input_ring_storage_bytes =
        sizeof(river_voice_kws_queue_item_t) *
        (size_t)CONFIG_RIVER_KWS_INPUT_QUEUE_FRAMES;
    RIVER_LOGI("kws init runtime buffers: heap_free=%lu pre=%luB queue=%luB dump_lazy=%luB stack=%uB",
               (unsigned long)rtos_mem_get_free_heap_size(),
               (unsigned long)g_river_voice_kws->pre_roll_ring_storage_bytes,
               (unsigned long)g_river_voice_kws->input_ring_storage_bytes,
               (unsigned long)river_voice_kws_tensor_dump_reserved_bytes(
                   g_river_voice_kws),
               (unsigned int)RIVER_KWS_TASK_STACK);
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

    status = river_voice_kws_alloc_runtime_buffer(
        g_river_voice_kws->pre_roll_ring_storage_bytes,
        &g_river_voice_kws->pre_roll_ring_storage,
        &g_river_voice_kws->pre_roll_ring_storage_allocation,
        &g_river_voice_kws->pre_roll_ring_storage_from_heap_types);
    if (status != RIVER_OK) {
        RIVER_LOGE("kws pre-roll storage alloc failed: bytes=%lu status=%d",
                   (unsigned long)g_river_voice_kws->pre_roll_ring_storage_bytes,
                   (int)status);
        goto fail;
    }
    status = river_voice_kws_alloc_runtime_buffer(
        g_river_voice_kws->input_ring_storage_bytes,
        &g_river_voice_kws->input_ring_storage,
        &g_river_voice_kws->input_ring_storage_allocation,
        &g_river_voice_kws->input_ring_storage_from_heap_types);
    if (status != RIVER_OK) {
        RIVER_LOGE("kws worker storage alloc failed: bytes=%lu status=%d",
                   (unsigned long)g_river_voice_kws->input_ring_storage_bytes,
                   (int)status);
        goto fail;
    }
    RIVER_LOGI("kws init stage: queue_storage_ready heap_free=%lu",
               (unsigned long)rtos_mem_get_free_heap_size());

    status = river_audio_frame_ring_init_with_storage_ex(
        &g_river_voice_kws->pre_roll_ring,
        g_river_voice_kws->pre_roll_ring_storage,
        g_river_voice_kws->pre_roll_ring_storage_bytes,
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
        g_river_voice_kws->input_ring_storage_bytes,
        sizeof(river_voice_kws_queue_item_t),
        CONFIG_RIVER_KWS_INPUT_QUEUE_FRAMES,
        RIVER_AUDIO_FRAME_RING_MODE_LOCKED);
    if (status != RIVER_OK) {
        RIVER_LOGE("kws worker ring init failed: status=%d frame_bytes=%u frames=%u",
                   (int)status,
                   (unsigned int)sizeof(river_voice_kws_queue_item_t),
                   (unsigned int)CONFIG_RIVER_KWS_INPUT_QUEUE_FRAMES);
        goto fail;
    }

    if (rtos_sema_create_binary(&g_river_voice_kws->input_ready) != RTK_SUCCESS) {
        RIVER_LOGE("create kws worker signal failed");
        status = RIVER_ERR_NO_MEMORY;
        goto fail;
    }
    g_river_voice_kws->input_ready_created = true;
    RIVER_LOGI("kws init stage: signal_ready heap_free=%lu",
               (unsigned long)rtos_mem_get_free_heap_size());

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
    RIVER_LOGI("kws init stage: task_ready heap_free=%lu",
               (unsigned long)rtos_mem_get_free_heap_size());
    g_river_voice_kws->init_heap_after_bytes = rtos_mem_get_free_heap_size();
    g_river_voice_kws->init_heap_min_bytes =
        rtos_mem_get_minimum_ever_free_heap_size();

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
    RIVER_LOGI("kws alloc: ctx=%p ctx_raw=%p arena=%p arena_raw=%p align=%u input_bytes=%lu output_bytes=%lu arena_used=%luB arena_slack=%luB",
               (void *)g_river_voice_kws,
               g_river_voice_kws_allocation,
               (void *)g_river_voice_kws->tensor_arena,
               g_river_voice_kws->tensor_arena_allocation,
               (unsigned int)RIVER_KWS_ALLOCATION_ALIGNMENT,
               (unsigned long)g_river_voice_kws->input_tensor->bytes,
               (unsigned long)g_river_voice_kws->output_tensor->bytes,
               (unsigned long)g_river_voice_kws->arena_used_bytes,
               (unsigned long)g_river_voice_kws->arena_slack_bytes);
    RIVER_LOGI("kws tensor data: input=%p output=%p",
               input_tensor_data,
               output_tensor_data);
    RIVER_LOGI("kws fft buffers: in=%p out=%p align=%u",
               (void *)g_river_voice_kws->fft_input,
               (void *)g_river_voice_kws->fft_output,
               (unsigned int)RIVER_KWS_ALLOCATION_ALIGNMENT);
    RIVER_LOGI("kws memory plan: heap_init=%lu->%lu min=%lu ctx=%luB pre=%luB queue=%luB dump=%luB",
               (unsigned long)g_river_voice_kws->init_heap_before_bytes,
               (unsigned long)g_river_voice_kws->init_heap_after_bytes,
               (unsigned long)g_river_voice_kws->init_heap_min_bytes,
               (unsigned long)sizeof(*g_river_voice_kws),
               (unsigned long)g_river_voice_kws->pre_roll_ring_storage_bytes,
               (unsigned long)g_river_voice_kws->input_ring_storage_bytes,
               (unsigned long)river_voice_kws_tensor_dump_reserved_bytes(
                   g_river_voice_kws));
    RIVER_LOGI("kws worker: priority=%u stack=%uB queue=%u frame=%uB wake=event wait_ms=%u pre_roll_flush=%u trim=%u->%u",
               (unsigned int)RIVER_KWS_TASK_PRIORITY,
               (unsigned int)RIVER_KWS_TASK_STACK,
               (unsigned int)CONFIG_RIVER_KWS_INPUT_QUEUE_FRAMES,
               (unsigned int)RIVER_KWS_INPUT_FRAME_BYTES,
               (unsigned int)RIVER_KWS_TASK_WAIT_MS,
               (unsigned int)RIVER_KWS_PRE_ROLL_FLUSH_MAX_FRAMES,
               (unsigned int)river_voice_kws_input_trim_high_water_frames(),
               (unsigned int)river_voice_kws_input_trim_target_frames());
    return RIVER_OK;

fail:
    if (g_river_voice_kws != NULL) {
        uint32_t wait_count;

        if (g_river_voice_kws->task_running) {
            g_river_voice_kws->task_stop_requested = true;
            river_voice_kws_signal_worker(g_river_voice_kws);
            for (wait_count = 0U; wait_count < 100U; ++wait_count) {
                if (!g_river_voice_kws->task_running) {
                    break;
                }
                rtos_time_delay_ms(10U);
            }
        }
        if (g_river_voice_kws->input_ready_created) {
            river_voice_kws_drain_input_signal(g_river_voice_kws);
            rtos_sema_delete(g_river_voice_kws->input_ready);
            g_river_voice_kws->input_ready_created = false;
        }
        if (g_river_voice_kws->input_ring.initialized) {
            river_audio_frame_ring_deinit(&g_river_voice_kws->input_ring);
        }
        if (g_river_voice_kws->pre_roll_ring.initialized) {
            river_audio_frame_ring_deinit(&g_river_voice_kws->pre_roll_ring);
        }
        river_voice_kws_free_allocation(
            g_river_voice_kws->input_ring_storage_allocation,
            g_river_voice_kws->input_ring_storage_from_heap_types);
        river_voice_kws_free_allocation(
            g_river_voice_kws->pre_roll_ring_storage_allocation,
            g_river_voice_kws->pre_roll_ring_storage_from_heap_types);
        river_voice_kws_free_allocation(
            g_river_voice_kws->tensor_dump_output_allocation,
            g_river_voice_kws->tensor_dump_output_from_heap_types);
        river_voice_kws_free_allocation(
            g_river_voice_kws->tensor_dump_input_allocation,
            g_river_voice_kws->tensor_dump_input_from_heap_types);
        river_voice_kws_free_allocation(
            g_river_voice_kws->tensor_dump_feature_allocation,
            g_river_voice_kws->tensor_dump_feature_from_heap_types);
        if (g_river_voice_kws->interpreter != NULL) {
            g_river_voice_kws->interpreter->~MicroInterpreter();
        }
        if (g_river_voice_kws->resolver_constructed) {
            g_river_voice_kws->op_resolver.~river_voice_kws_op_resolver_t();
        }
        if (g_river_voice_kws->tensor_arena != NULL) {
            river_voice_kws_free_allocation(
                g_river_voice_kws->tensor_arena_allocation,
                g_river_voice_kws->tensor_arena_from_heap_types);
        }
        if (g_river_voice_kws->real_fft != NULL) {
            WebRtcSpl_FreeRealFFT(g_river_voice_kws->real_fft);
        }
        river_voice_kws_free_allocation(g_river_voice_kws_allocation,
                                        g_river_voice_kws_allocation_from_heap_types);
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

    RIVER_LOGI("kws backend: runtime=tflite_micro input=%lux%lux%lu log_mel sr=16k fft=512 hop=160 arena=%uKB model=%luB variant=%s stride=%u threshold_q15=%u hold=%u cooldown_ms=%u gate=vad pre_roll_ms=%u pre_roll_flush=%u queue=%u trim=%u->%u",
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
               (unsigned int)RIVER_KWS_PRE_ROLL_FLUSH_MAX_FRAMES,
               (unsigned int)CONFIG_RIVER_KWS_INPUT_QUEUE_FRAMES,
               (unsigned int)river_voice_kws_input_trim_high_water_frames(),
               (unsigned int)river_voice_kws_input_trim_target_frames());
    RIVER_LOGI("kws frontend: source=fixed_dsb_mono feature=log_mel bins=40 frames=98 norm=global(mean_milli=%ld,std_milli=%lu) wake_text=%s",
               (long)mean_milli,
               (unsigned long)std_milli,
               g_river_voice_kws_text);
}

extern "C" void river_voice_kws_dump_status(void)
{
    size_t feat_chunks;
    size_t input_chunks;
    size_t output_chunks;
    const char *handoff_block_reason;

    if (g_river_voice_kws == NULL) {
        RIVER_LOGI("kws status: closed");
        return;
    }

    g_river_voice_kws->last_status_log_ms = 0U;
    river_voice_kws_log_status(g_river_voice_kws);

    feat_chunks = g_river_voice_kws->tensor_dump_feature_bytes_captured == 0U ?
                      0U :
                      river_voice_kws_tensor_dump_chunk_count(
                          g_river_voice_kws->tensor_dump_feature_bytes_captured);
    input_chunks = g_river_voice_kws->tensor_dump_input_bytes_captured == 0U ?
                       0U :
                       river_voice_kws_tensor_dump_chunk_count(
                           g_river_voice_kws->tensor_dump_input_bytes_captured);
    output_chunks = g_river_voice_kws->tensor_dump_output_bytes_captured == 0U ?
                        0U :
                        river_voice_kws_tensor_dump_chunk_count(
                            g_river_voice_kws->tensor_dump_output_bytes_captured);
    RIVER_LOGI("kws tensor dump status: armed=%s ready=%s last_seq=%lu last_infer=%lu capture_seq=%lu capture_infer=%lu chunks=[feat:%lu input:%lu output:%lu]",
               g_river_voice_kws->tensor_dump_armed ? "yes" : "no",
               g_river_voice_kws->tensor_dump_snapshot_ready ? "yes" : "no",
               (unsigned long)g_river_voice_kws->tensor_dump_last_seq,
               (unsigned long)g_river_voice_kws->tensor_dump_last_infer,
               (unsigned long)g_river_voice_kws->tensor_dump_capture_seq,
               (unsigned long)g_river_voice_kws->tensor_dump_capture_infer,
               (unsigned long)feat_chunks,
               (unsigned long)input_chunks,
               (unsigned long)output_chunks);
    handoff_block_reason =
        river_voice_kws_wake_handoff_block_reason_locked(g_river_voice_kws);
    RIVER_LOGI("kws debug status: local_only=%s wake_handoff=%s reason=%s",
               g_river_voice_kws->local_debug_mode ? "yes" : "no",
               handoff_block_reason != NULL ? "blocked" : "normal",
               handoff_block_reason != NULL ? handoff_block_reason : "-");
}

extern "C" void river_voice_kws_set_local_debug_mode(bool enabled)
{
    if (g_river_voice_kws == NULL || !g_river_voice_kws->initialized) {
        RIVER_LOGI("kws debug local_only: enabled=%s initialized=no",
                   enabled ? "yes" : "no");
        return;
    }

    g_river_voice_kws->local_debug_mode = enabled;
    RIVER_LOGI("kws debug local_only: enabled=%s note=wakeword_still_runs_cloud_handoff=%s",
               enabled ? "yes" : "no",
               enabled ? "suppressed" : "enabled");
}

extern "C" bool river_voice_kws_local_debug_mode_enabled(void)
{
    return g_river_voice_kws != NULL &&
           g_river_voice_kws->initialized &&
           g_river_voice_kws->local_debug_mode;
}

extern "C" const char *river_voice_kws_wake_handoff_block_reason(void)
{
    return river_voice_kws_wake_handoff_block_reason_locked(g_river_voice_kws);
}

extern "C" river_status_t river_voice_kws_request_tensor_dump_next(void)
{
    river_status_t status;

    if (g_river_voice_kws == NULL || !g_river_voice_kws->initialized) {
        return RIVER_ERR_INVALID_STATE;
    }

    status = river_voice_kws_ensure_tensor_dump_buffers(g_river_voice_kws);
    if (status != RIVER_OK) {
        RIVER_LOGE("kws tensor dump buffer alloc failed: status=%d reserve=%lu",
                   (int)status,
                   (unsigned long)river_voice_kws_tensor_dump_reserved_bytes(
                       g_river_voice_kws));
        return status;
    }

    g_river_voice_kws->tensor_dump_armed = true;
    g_river_voice_kws->tensor_dump_feature_valid = false;
    river_voice_kws_tensor_dump_snapshot_reset(g_river_voice_kws);
    RIVER_LOGI("kws tensor dump armed: mode=next");
    return RIVER_OK;
}

extern "C" void river_voice_kws_cancel_tensor_dump(void)
{
    if (g_river_voice_kws == NULL) {
        RIVER_LOGI("kws tensor dump status: closed");
        return;
    }

    g_river_voice_kws->tensor_dump_armed = false;
    g_river_voice_kws->tensor_dump_feature_valid = false;
    RIVER_LOGI("kws tensor dump armed: mode=off");
}

extern "C" void river_voice_kws_clear_tensor_dump(void)
{
    if (g_river_voice_kws == NULL) {
        RIVER_LOGI("kws tensor dump status: closed");
        return;
    }

    g_river_voice_kws->tensor_dump_armed = false;
    g_river_voice_kws->tensor_dump_feature_valid = false;
    river_voice_kws_tensor_dump_snapshot_reset(g_river_voice_kws);
    RIVER_LOGI("kws tensor dump snapshot cleared");
}

extern "C" void river_voice_kws_dump_tensor_meta(void)
{
    size_t feat_chunks;
    size_t input_chunks;
    size_t output_chunks;

    if (g_river_voice_kws == NULL || !g_river_voice_kws->initialized) {
        RIVER_LOGI("kws tensor dump status: closed");
        return;
    }
    if (!g_river_voice_kws->tensor_dump_snapshot_ready) {
        RIVER_LOGI("kws tensor dump snapshot: ready=no");
        return;
    }

    river_voice_kws_tensor_dump_log_begin_meta(g_river_voice_kws);
    feat_chunks = g_river_voice_kws->tensor_dump_feature_bytes_captured == 0U ?
                      0U :
                      river_voice_kws_tensor_dump_chunk_count(
                          g_river_voice_kws->tensor_dump_feature_bytes_captured);
    input_chunks = g_river_voice_kws->tensor_dump_input_bytes_captured == 0U ?
                       0U :
                       river_voice_kws_tensor_dump_chunk_count(
                           g_river_voice_kws->tensor_dump_input_bytes_captured);
    output_chunks = g_river_voice_kws->tensor_dump_output_bytes_captured == 0U ?
                        0U :
                        river_voice_kws_tensor_dump_chunk_count(
                            g_river_voice_kws->tensor_dump_output_bytes_captured);
    RIVER_LOGI("kws tensor dump snapshot: seq=%lu infer=%lu chunks=[feat:%lu input:%lu output:%lu]",
               (unsigned long)g_river_voice_kws->tensor_dump_capture_seq,
               (unsigned long)g_river_voice_kws->tensor_dump_capture_infer,
               (unsigned long)feat_chunks,
               (unsigned long)input_chunks,
               (unsigned long)output_chunks);
}

extern "C" river_status_t river_voice_kws_dump_tensor_chunk(
    river_voice_kws_tensor_dump_buffer_t buffer,
    uint32_t chunk_index)
{
    const uint8_t *data = NULL;
    size_t bytes = 0U;
    size_t total_chunks;
    river_status_t status;

    if (g_river_voice_kws == NULL || !g_river_voice_kws->initialized) {
        return RIVER_ERR_INVALID_STATE;
    }
    if (!g_river_voice_kws->tensor_dump_snapshot_ready || chunk_index == 0U) {
        return RIVER_ERR_INVALID_STATE;
    }

    status = river_voice_kws_tensor_dump_buffer_view(g_river_voice_kws,
                                                     buffer,
                                                     &data,
                                                     &bytes);
    if (status != RIVER_OK || data == NULL || bytes == 0U) {
        return status != RIVER_OK ? status : RIVER_ERR_INVALID_STATE;
    }

    total_chunks = river_voice_kws_tensor_dump_chunk_count(bytes);
    if ((size_t)chunk_index > total_chunks) {
        RIVER_LOGE("kws tensor dump chunk invalid: label=%s index=%lu total=%lu",
                   river_voice_kws_tensor_dump_buffer_name(buffer),
                   (unsigned long)chunk_index,
                   (unsigned long)total_chunks);
        return RIVER_ERR_ARG;
    }

    river_voice_kws_log_hex_chunk(river_voice_kws_tensor_dump_buffer_name(buffer),
                                  g_river_voice_kws->tensor_dump_capture_seq,
                                  data,
                                  bytes,
                                  chunk_index);
    return RIVER_OK;
}
