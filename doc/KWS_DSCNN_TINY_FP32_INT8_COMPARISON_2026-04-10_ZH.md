# `student_dscnn_tiny_v2` FP32 / INT8 对比结论

日期：2026-04-10

## 1. 直接结论

### 1.1 对应的 FP32 之前已经部署过

是的，`student_dscnn_tiny_v2_fp32_debug` 之前已经正式上板并做过对拍。

已有记录见：

- [RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_DSCNN_TINY_FP32_DEBUG_ZH.md](./RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_DSCNN_TINY_FP32_DEBUG_ZH.md)

当前 `student_dscnn_tiny_v2_int8_debug` 也已经在 `2026-04-10` 完成 exact parity：

- [RUNTIME_RESOURCE_PROFILE_2026-04-10_STUDENT_DSCNN_TINY_INT8_DEBUG_ZH.md](./RUNTIME_RESOURCE_PROFILE_2026-04-10_STUDENT_DSCNN_TINY_INT8_DEBUG_ZH.md)

### 1.2 同模型对比结果

在当前板端基线下，`INT8` 相比对应 `FP32` 的表现是：

- 内存占用显著下降
- 推理耗时明显上升

也就是说：

- `INT8` 的“省内存”效果已经成立
- `INT8` 的“算力加速”效果没有成立

### 1.3 当前 latest-SDK 上，INT8 计算优化仍未真正生效

从当前 `/root/ameba-rtos` 代码看，结论仍然和旧 SDK 路径一致：

- CA32 的 `int8 conv` 仍然被强制回退到 `reference_integer_ops::ConvPerChannel(...)`
- CA32 的 `int8 depthwise_conv` 仍然被强制回退到 `reference_integer_ops::DepthwiseConvPerChannel(...)`

所以当前看到的现象应理解为：

- 不是 `INT8` 数值链路没生效
- 而是 `INT8` 高性能 kernel 路径没有真正生效

## 2. FP32 / INT8 实测对比

| 项目 | FP32 | INT8 | 结论 |
| --- | ---: | ---: | --- |
| 变体 | `student_dscnn_tiny_v2_fp32_debug` | `student_dscnn_tiny_v2_int8_debug` | 同一模型家族 |
| 部署正确性 | 已确认 | 已确认 | 两边都已上板 |
| 对拍状态 | 已确认 | 已确认 | 两边都已证明部署正确 |
| 输入契约 | `1x40x101x1` | `1x40x101x1` | 一致 |
| runtime dtype | `float32 -> float32` | `int8 -> int8` | 只改 dtype |
| 典型 `infer_us` | `183988` | `492733` | INT8 更慢 |
| arena 实际使用 | `1168336 B` | `295764 B` | INT8 更省内存 |
| arena 相对 FP32 | `1.00x` | `0.253x` | INT8 约为 FP32 的 `25.3%` |

换算后：

- `INT8` 比对应 `FP32` 慢约 `2.68x`
- 绝对多出约 `308745 us`
- 但 `arena` 占用减少约 `74.7%`

## 3. 这说明什么

这组结果非常关键，因为它说明当前 `INT8` 路径的收益是不对称的：

### 3.1 生效的部分

- 模型 I/O 量化已生效
- `input_raw` / `output_raw` 路径已正确
- 板端 / host exact parity 已成立
- arena 占用显著下降

所以不能说：

- “INT8 没接上”
- “INT8 没起作用”

### 3.2 没生效的部分

当前没有兑现的是：

- `INT8` 计算性能优化

因为如果 INT8 高性能 kernel 真正生效，通常不会出现：

- 同一模型 INT8 比 FP32 慢 `2.68x`

这个结果本身已经足够说明：

- 当前主要瓶颈不在量化输入输出
- 而在 quantized kernel 执行路径

## 4. latest-SDK 源码证据

### 4.1 `conv.cc`

当前 latest-SDK 文件：

- `/root/ameba-rtos/component/tflite_micro/tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/conv.cc`

关键位置：

- `248-267`

当前代码直接写明：

- `The current CA32 int8 conv optimized path is not reliable ...`

随后直接调用：

- `tflite::reference_integer_ops::ConvPerChannel(...)`

这说明当前 CA32 `int8 conv` 并没有真正走优化后的 GEMM / im2col 主路径。

### 4.2 `depthwise_conv.cc`

当前 latest-SDK 文件：

- `/root/ameba-rtos/component/tflite_micro/tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/depthwise_conv.cc`

关键位置：

- `141-158`

当前代码直接写明：

- `The CA32 optimized int8 depthwise kernel is not reliable ...`

随后直接调用：

- `reference_integer_ops::DepthwiseConvPerChannel(...)`

这说明当前 `int8 depthwise` 也仍在 reference 路径。

## 5. 与“老 SDK 没生效”的关系

如果问题是：

- “当前 latest-SDK 是否已经像芯片同事说的那样，让 INT8 计算优化真正恢复”

那当前基于本机源码和实测的结论是：

- 没有证据支持这个说法
- 从我们当前实际在用的 `/root/ameba-rtos` 来看，至少对这条 wakeword 路径，`INT8 conv/depthwise` 仍然是 reference fallback

因此：

- 当前 `student_dscnn_tiny_v2_int8_debug` 比 `student_bc_resnet_tiny_v2_int8_debug` 快很多

更合理的解释是：

- DS-CNN tiny 模型结构本身更轻

而不是：

- latest-SDK 已经把 quantized compute kernel 真正修好了

## 6. 当前工程口径

现在对这个模型最准确的说法是：

- `student_dscnn_tiny_v2_int8_debug` 已经证明“INT8 部署正确且更省内存”
- 但它还没有证明“当前 latest-SDK 上 INT8 计算优化已恢复有效”
- 目前实测恰恰更支持相反结论：
  - 计算优化没有兑现
  - 只是模型更轻了

## 7. 当前建议

因此后续讨论应分成两条线：

1. 模型线：
   - 继续找更轻的 INT8 模型
   - 以当前 runtime 现实为前提，不要假设“量化自然会更快”

2. SDK / 芯片线：
   - 如果芯片侧坚持 latest-SDK 已修复 INT8 优化
   - 需要他们给出精确提交，或者直接指出：
     - 哪个 `conv.cc`
     - 哪个 `depthwise_conv.cc`
     - 哪段 patch
   - 否则目前以本机源码为准，结论仍然是：
     - 当前路径没有真正启用可用的 INT8 高性能 kernel
