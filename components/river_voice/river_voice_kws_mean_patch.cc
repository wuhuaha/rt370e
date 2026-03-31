#include "river_voice_kws_mean_patch.h"

#include <stdint.h>
#include <string.h>

#include "tensorflow/lite/micro/kernels/kernel_util.h"
#include "tensorflow/lite/micro/kernels/reduce.h"

namespace {

struct river_voice_kws_mean_patch_op_data {
    tflite::OpDataReduce reduce;
    TfLiteReducerParams params;
};

static void *river_voice_kws_mean_patch_init(TfLiteContext *context,
                                             const char *buffer,
                                             size_t length)
{
    constexpr size_t kOpDataAlignment =
        alignof(river_voice_kws_mean_patch_op_data);
    river_voice_kws_mean_patch_op_data *patch_data;
    void *raw_allocation;
    const TfLiteReducerParams *params;
    uintptr_t aligned_address;

    (void)length;
    raw_allocation = context->AllocatePersistentBuffer(
        context, sizeof(*patch_data) + kOpDataAlignment - 1U);
    if (raw_allocation == NULL) {
        return NULL;
    }

    aligned_address =
        (reinterpret_cast<uintptr_t>(raw_allocation) + kOpDataAlignment - 1U) &
        ~(uintptr_t)(kOpDataAlignment - 1U);
    patch_data = reinterpret_cast<river_voice_kws_mean_patch_op_data *>(
        aligned_address);
    memset(patch_data, 0, sizeof(*patch_data));

    params = reinterpret_cast<const TfLiteReducerParams *>(buffer);
    if (params != NULL) {
        patch_data->params = *params;
    }
    return patch_data;
}

static TfLiteStatus river_voice_kws_mean_patch_prepare(TfLiteContext *context,
                                                       TfLiteNode *node)
{
    river_voice_kws_mean_patch_op_data *patch_data;

    TF_LITE_ENSURE(context, context != NULL);
    TF_LITE_ENSURE(context, node != NULL);
    patch_data =
        static_cast<river_voice_kws_mean_patch_op_data *>(node->user_data);
    TF_LITE_ENSURE(context, patch_data != NULL);
    return tflite::PrepareMeanOrSumHelper(context, node, &patch_data->reduce);
}

static TfLiteStatus river_voice_kws_mean_patch_eval(TfLiteContext *context,
                                                    TfLiteNode *node)
{
    river_voice_kws_mean_patch_op_data *patch_data;
    void *saved_builtin_data;
    TfLiteStatus status;

    TF_LITE_ENSURE(context, context != NULL);
    TF_LITE_ENSURE(context, node != NULL);
    patch_data =
        static_cast<river_voice_kws_mean_patch_op_data *>(node->user_data);
    TF_LITE_ENSURE(context, patch_data != NULL);

    saved_builtin_data = node->builtin_data;
    node->builtin_data = &patch_data->params;
    status = tflite::EvalMeanHelper(context, node, &patch_data->reduce);
    node->builtin_data = saved_builtin_data;
    return status;
}

}  // namespace

TFLMRegistration river_voice_kws_RegisterPatchedMean()
{
    return tflite::micro::RegisterOp(river_voice_kws_mean_patch_init,
                                     river_voice_kws_mean_patch_prepare,
                                     river_voice_kws_mean_patch_eval);
}
