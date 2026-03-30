#include "river_voice_kws_mean_patch.h"

#include <math.h>
#include <new>
#include <limits>
#include <stdint.h>

#include "tensorflow/lite/kernels/kernel_util.h"
#include "tensorflow/lite/micro/kernels/kernel_util.h"
#include "tensorflow/lite/micro/kernels/reduce.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_utils.h"

namespace {

template <typename T>
static T river_voice_kws_mean_patch_clamp(int32_t value)
{
    if (value < (int32_t)std::numeric_limits<T>::min()) {
        value = (int32_t)std::numeric_limits<T>::min();
    } else if (value > (int32_t)std::numeric_limits<T>::max()) {
        value = (int32_t)std::numeric_limits<T>::max();
    }
    return (T)value;
}

static bool river_voice_kws_mean_patch_resolve_axes(const int *axis_data,
                                                    int axis_count,
                                                    int rank,
                                                    int *axes_out,
                                                    int *axes_len_out)
{
    int axis_index;
    int out_len = 0;

    if (axes_len_out != NULL) {
        *axes_len_out = 0;
    }
    if (axis_data == NULL || axes_out == NULL || rank <= 0 || axis_count <= 0) {
        return false;
    }

    for (axis_index = 0; axis_index < axis_count; ++axis_index) {
        int axis = axis_data[axis_index];
        int dedupe_index;
        bool duplicate = false;

        if (axis < 0) {
            axis += rank;
        }
        if (axis < 0 || axis >= rank) {
            return false;
        }
        for (dedupe_index = 0; dedupe_index < out_len; ++dedupe_index) {
            if (axes_out[dedupe_index] == axis) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            axes_out[out_len++] = axis;
        }
    }

    if (out_len == 2 && axes_out[0] > axes_out[1]) {
        int tmp = axes_out[0];
        axes_out[0] = axes_out[1];
        axes_out[1] = tmp;
    }
    if (axes_len_out != NULL) {
        *axes_len_out = out_len;
    }
    return out_len > 0;
}

template <typename T>
static T river_voice_kws_mean_patch_quantize(int32_t centered_sum,
                                             int32_t count,
                                             float input_scale,
                                             int output_zero_point,
                                             float output_scale)
{
    float mean_real;
    int32_t quantized;

    if (count <= 0 || output_scale <= 0.0f) {
        return (T)output_zero_point;
    }

    mean_real = ((float)centered_sum * input_scale) / (float)count;
    quantized = (int32_t)lroundf(mean_real / output_scale) + output_zero_point;
    return river_voice_kws_mean_patch_clamp<T>(quantized);
}

template <typename T>
static TfLiteStatus river_voice_kws_mean_patch_eval_quantized(
    TfLiteContext *context,
    TfLiteNode *node)
{
    const TfLiteEvalTensor *input;
    const TfLiteEvalTensor *axis;
    TfLiteEvalTensor *output;
    const tflite::OpDataReduce *op_data;
    const TfLiteReducerParams *params;
    const T *input_data;
    T *output_data;
    int axes[2];
    int axes_len = 0;
    int axis0;
    int axis1;
    int n;
    int h;
    int w;
    int c;
    int b;
    int hi;
    int wi;
    int ci;
    int32_t centered_sum;

    input = tflite::micro::GetEvalInput(context, node, 0);
    axis = tflite::micro::GetEvalInput(context, node, 1);
    output = tflite::micro::GetEvalOutput(context, node, 0);
    op_data = static_cast<const tflite::OpDataReduce *>(node->user_data);
    params = reinterpret_cast<const TfLiteReducerParams *>(node->builtin_data);

    TF_LITE_ENSURE(context, input != NULL);
    TF_LITE_ENSURE(context, axis != NULL);
    TF_LITE_ENSURE(context, output != NULL);
    TF_LITE_ENSURE(context, op_data != NULL);
    TF_LITE_ENSURE(context, params != NULL);
    TF_LITE_ENSURE_EQ(context, axis->type, kTfLiteInt32);
    TF_LITE_ENSURE_EQ(context, input->dims->size, 4);

    input_data = tflite::micro::GetTensorData<T>(input);
    output_data = tflite::micro::GetTensorData<T>(output);
    TF_LITE_ENSURE(context, input_data != NULL);
    TF_LITE_ENSURE(context, output_data != NULL);

    TF_LITE_ENSURE(
        context,
        river_voice_kws_mean_patch_resolve_axes(
            tflite::micro::GetTensorData<int>(axis),
            tflite::ElementCount(*axis->dims), input->dims->size, axes,
            &axes_len));

    n = input->dims->data[0];
    h = input->dims->data[1];
    w = input->dims->data[2];
    c = input->dims->data[3];
    axis0 = axes[0];
    axis1 = axes_len > 1 ? axes[1] : -1;

    if (axes_len == 1 && params->keep_dims && axis0 == 2) {
        TF_LITE_ENSURE_EQ(context, output->dims->size, 4);
        TF_LITE_ENSURE_EQ(context, output->dims->data[0], n);
        TF_LITE_ENSURE_EQ(context, output->dims->data[1], h);
        TF_LITE_ENSURE_EQ(context, output->dims->data[2], 1);
        TF_LITE_ENSURE_EQ(context, output->dims->data[3], c);
        for (b = 0; b < n; ++b) {
            for (hi = 0; hi < h; ++hi) {
                for (ci = 0; ci < c; ++ci) {
                    centered_sum = 0;
                    for (wi = 0; wi < w; ++wi) {
                        const int input_offset =
                            (((b * h) + hi) * w + wi) * c + ci;
                        centered_sum +=
                            (int32_t)input_data[input_offset] -
                            op_data->input_zp;
                    }
                    output_data[(b * h + hi) * c + ci] =
                        river_voice_kws_mean_patch_quantize<T>(
                            centered_sum, w, op_data->input_scale,
                            op_data->output_zp, op_data->output_scale);
                }
            }
        }
        return kTfLiteOk;
    }

    if (axes_len == 1 && params->keep_dims && axis0 == 1) {
        TF_LITE_ENSURE_EQ(context, output->dims->size, 4);
        TF_LITE_ENSURE_EQ(context, output->dims->data[0], n);
        TF_LITE_ENSURE_EQ(context, output->dims->data[1], 1);
        TF_LITE_ENSURE_EQ(context, output->dims->data[2], w);
        TF_LITE_ENSURE_EQ(context, output->dims->data[3], c);
        for (b = 0; b < n; ++b) {
            for (wi = 0; wi < w; ++wi) {
                for (ci = 0; ci < c; ++ci) {
                    centered_sum = 0;
                    for (hi = 0; hi < h; ++hi) {
                        const int input_offset =
                            (((b * h) + hi) * w + wi) * c + ci;
                        centered_sum +=
                            (int32_t)input_data[input_offset] -
                            op_data->input_zp;
                    }
                    output_data[(b * w + wi) * c + ci] =
                        river_voice_kws_mean_patch_quantize<T>(
                            centered_sum, h, op_data->input_scale,
                            op_data->output_zp, op_data->output_scale);
                }
            }
        }
        return kTfLiteOk;
    }

    if (axes_len == 2 && axis0 == 1 && axis1 == 2) {
        const int count = h * w;

        if (params->keep_dims) {
            TF_LITE_ENSURE_EQ(context, output->dims->size, 4);
            TF_LITE_ENSURE_EQ(context, output->dims->data[0], n);
            TF_LITE_ENSURE_EQ(context, output->dims->data[1], 1);
            TF_LITE_ENSURE_EQ(context, output->dims->data[2], 1);
            TF_LITE_ENSURE_EQ(context, output->dims->data[3], c);
        } else {
            TF_LITE_ENSURE_EQ(context, output->dims->size, 2);
            TF_LITE_ENSURE_EQ(context, output->dims->data[0], n);
            TF_LITE_ENSURE_EQ(context, output->dims->data[1], c);
        }

        for (b = 0; b < n; ++b) {
            for (ci = 0; ci < c; ++ci) {
                centered_sum = 0;
                for (hi = 0; hi < h; ++hi) {
                    for (wi = 0; wi < w; ++wi) {
                        const int input_offset =
                            (((b * h) + hi) * w + wi) * c + ci;
                        centered_sum +=
                            (int32_t)input_data[input_offset] -
                            op_data->input_zp;
                    }
                }
                output_data[b * c + ci] = river_voice_kws_mean_patch_quantize<T>(
                    centered_sum, count, op_data->input_scale,
                    op_data->output_zp, op_data->output_scale);
            }
        }
        return kTfLiteOk;
    }

    MicroPrintf("river kws mean patch got unsupported reduce pattern");
    return kTfLiteError;
}

static void *river_voice_kws_mean_patch_init(TfLiteContext *context,
                                             const char *buffer,
                                             size_t length)
{
    void *op_data;

    (void)buffer;
    (void)length;
    op_data = context->AllocatePersistentBuffer(
        context, sizeof(tflite::OpDataReduce));
    return new (op_data) tflite::OpDataReduce();
}

static TfLiteStatus river_voice_kws_mean_patch_prepare(TfLiteContext *context,
                                                       TfLiteNode *node)
{
    TF_LITE_ENSURE(context, context != NULL);
    TF_LITE_ENSURE(context, node != NULL);
    return tflite::PrepareMeanOrSumHelper(
        context, node, static_cast<tflite::OpDataReduce *>(node->user_data));
}

static TfLiteStatus river_voice_kws_mean_patch_eval(TfLiteContext *context,
                                                    TfLiteNode *node)
{
    const TfLiteEvalTensor *input;

    TF_LITE_ENSURE(context, context != NULL);
    TF_LITE_ENSURE(context, node != NULL);
    input = tflite::micro::GetEvalInput(context, node, 0);
    TF_LITE_ENSURE(context, input != NULL);

    switch (input->type) {
    case kTfLiteInt8:
        return river_voice_kws_mean_patch_eval_quantized<int8_t>(context,
                                                                 node);
    case kTfLiteInt16:
        return river_voice_kws_mean_patch_eval_quantized<int16_t>(context,
                                                                  node);
    default:
        MicroPrintf("river kws mean patch only supports int8/int16");
        return kTfLiteError;
    }
}

}  // namespace

TFLMRegistration river_voice_kws_RegisterPatchedMean()
{
    return tflite::micro::RegisterOp(river_voice_kws_mean_patch_init,
                                     river_voice_kws_mean_patch_prepare,
                                     river_voice_kws_mean_patch_eval);
}
