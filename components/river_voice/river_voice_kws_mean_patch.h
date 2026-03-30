#ifndef RIVER_VOICE_KWS_MEAN_PATCH_H
#define RIVER_VOICE_KWS_MEAN_PATCH_H

#ifndef TFLITE_WITH_STABLE_ABI
#define TFLITE_WITH_STABLE_ABI 0
#endif

#ifndef TFLITE_USE_OPAQUE_DELEGATE
#define TFLITE_USE_OPAQUE_DELEGATE 0
#endif

#include "tensorflow/lite/micro/micro_common.h"

/* 项目侧的 MEAN 内核补丁：
 * 当前 Ameba/TFLM 组合在量化 MEAN 的 scratch buffer 上会出现 4 字节未对齐写入，
 * BC-ResNet 首次推理时会因此触发 data abort。这里注册一个仅修正对齐问题的替代实现，
 * 保持模型仍然使用原始 builtin MEAN。 */
TFLMRegistration river_voice_kws_RegisterPatchedMean();

#endif  // RIVER_VOICE_KWS_MEAN_PATCH_H
