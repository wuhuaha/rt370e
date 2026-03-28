# 唤醒词模型迁移复盘与当前计划

## 1. 目标与范围

本次迁移的目标是把本地唤醒词模型从 trainer 项目导出并稳定部署到 `ameba-river` 板端，使系统从：

- 纯 `legacy_vad` 直接拉起云端 ASR

迁移到：

- 本地 `KWS` 命中
- 板端打开 `wakeword` admission
- 再进入小智会话窗口与后续对话

本复盘覆盖两部分仓库：

- 板端集成仓库：`/root/ameba-river`
- 模型训练/导出仓库：`/root/wake-word-trainer`

## 2. 迁移过程概览

本次迁移并不是单一问题，而是一条连续的问题链：

1. 最初板端 `KWS` 起不来，系统一直回退到 `legacy_vad`
2. 增加精确日志后，发现模型 tensor 类型与板端假设不一致
3. 进一步发现 trainer 导出的浮点模型其实是 hybrid TFLite，不被当前 `TFLite Micro` 端口支持
4. 再进一步发现 trainer 默认量化导出是 `uint8` IO，而当前 Ameba CA32 优化 kernel 更匹配 `int8` IO
5. 切回新的 `int8` 模型后，`KWS` 初始化终于成功，但运行期又在 CA32 优化 `conv` 的 `im2col` 路径发生越界崩溃
6. 目前最新版本已经修复该 `im2col` 越界问题，并重新编译完成，等待板端刷机验证

换句话说，这次迁移经历了：

- 模型格式问题
- trainer 导出策略问题
- 板端 `TFLM` runtime 元数据问题
- 板端音频 profile/内存预算问题
- 板端 CA32 优化 kernel 本身的实现 bug

## 3. 迁移过程中遇到过的问题

### 3.1 板端最初一直没有真正进入 KWS 路径

最早日志表现为：

- `caps[...,kws=off,...]`
- `kws init failed status=-3`
- `wake_admission=legacy_vad`

这说明后续小智会话虽然能打开，但不是唤醒词触发，而是普通 `VAD` 直接开流。

### 3.2 板端最初对 KWS tensor 类型的假设过于刚性

最早 KWS 代码默认按：

- 输入 `uint8`
- 输出 `uint8`

来处理模型 tensor。

但后续日志出现过：

- `kws tensor type unsupported: input type=1 output type=1 expected=3`

也就是板端 runtime 看到的是 `float32`，而代码只接受 `uint8`。这直接导致初始化失败。

### 3.3 板端 TFLM runtime 元数据与模型 schema 不一致

后续调试中发现，当前板端这版 `TFLite Micro` 并不总是能提供完全可信的 runtime 元数据，出现过：

- runtime tensor type 与 flatbuffer schema type 不一致
- `tensor->bytes` 看起来像“元素数”，而不是“真实字节数”
- runtime quant params 打印出异常值

这导致仅依赖 runtime tensor 元信息做判断会产生误判，甚至误把可用模型判成不可用。

### 3.4 为了验证 KWS，一度切到了 3ch native-ref + WebRTC AECM 实验 profile

中间一版曾默认启用了 `3ch native-ref` 实验 profile，结果导致：

- 运行时堆大幅下降
- `river_cap_drv` 分配失败
- Wi-Fi bring-up 期间不稳定

这属于“为了验证 KWS 却同时引入了 AEC/多通道实验链路”的耦合问题。

### 3.5 KWS 初始化阶段曾因诊断代码访问不稳定字段导致崩溃

为了更快定位问题，早期一度在初始化时读取 `TfLiteTensor` 的更多字段做诊断；板端 `TFLM` 对这些字段的可用性并不完全稳定，曾引发初始化期 `Data abort` 风险。后续已经把诊断路径收缩到更安全的字段集合。

### 3.6 trainer 的“纯浮点导出”实际上不是纯浮点

trainer 最初默认会在非量化导出场景仍开启：

```python
converter.optimizations = [tf.lite.Optimize.DEFAULT]
```

导致导出的 `model.tflite` 实际上是：

- `float32` 输入/输出
- `int8` 权重

即 hybrid TFLite。板端直接报：

```text
Hybrid models are not supported on TFLite Micro.
```

这不是模型训练本身的问题，而是导出策略错误。

### 3.7 切到真正纯 FP32 模型后，又遇到了 arena 明显不足

trainer 修复 hybrid 问题后，真正纯 FP32 模型体积上升到约 `133576B`。板端继续测试时又发现：

- `AllocateTensors()` 需要约 `501760B`
- 当前板端分配给 KWS 的 arena 远远不够

因此“纯 FP32 模型”在当前板端内存预算下不可行。

### 3.8 trainer 默认量化导出采用 `uint8` IO，不匹配当前 CA32 优化 kernel

进一步检查 trainer 与板端 kernel 后发现：

- trainer 量化导出默认是 `uint8` 输入/输出
- 当前 Ameba CA32 优化 `conv/fully_connected` kernel 支持的是：
  - `int8`
  - `int16`
  - `float32`

这意味着之前的“量化模型能导出”并不等于“量化模型能在当前板端稳定运行”。

### 3.9 切换到新的 `int8` 模型后，KWS 初始化成功，但运行期又在 CA32 优化 conv 崩溃

最新一轮切回新的 `int8` 模型后，日志已经能看到：

- `model_in=int8 model_out=int8 effective_in=int8 effective_out=int8`
- `wake_admission=wakeword`
- `kws backend ... model=62968B arena=160KB`

说明模型加载与初始化已经成功。

但随后在运行期发生新的 `Data abort`。符号化后定位到：

- `tflite::optimized_ops::ExtractPatchIntoBufferColumn<signed char>`

也就是当前 Ameba CA32 优化 `conv` 的 `im2col` 路径。

## 4. 做过的主要尝试

### 4.1 板端增加了更细粒度的 KWS 初始化与运行日志

包括但不限于：

- tensor runtime/model/effective type
- quant 参数来源
- tensor data 指针
- tensor bytes
- FFT buffer 对齐
- arena 大小与模型大小

这一步非常关键，因为如果没有精确日志，就无法把“模型坏了”和“板端适配坏了”区分开。

### 4.2 板端 KWS 代码从只支持 `uint8` 扩展到支持多种 tensor IO

在 [components/river_voice/river_voice_kws.cc](components/river_voice/river_voice_kws.cc) 中，KWS 已扩展为支持：

- `float32`
- `uint8`
- `int8`

同时量化参数解析改成：

- schema 优先
- runtime 兜底

这是因为当前板端 runtime 元数据并不总是可信。

### 4.3 对 runtime 元数据做了容错

做过的兼容性修正包括：

- `tensor->bytes` 支持“元素数”与“真实字节数”两种解释
- quant params 优先从 schema 取
- 不再过度依赖 runtime type

### 4.4 清理了与 KWS 无关但会干扰板测的路径

做过的收缩包括：

- 默认 profile 从 `3ch native-ref + AECM` 实验链路退回稳定的 `2ch asr_mainline`
- 播放阶段在不安全 profile 下不强行走 playback ref
- conversation window 改为异步 worker 打开，避免在音频实时线程里直接做云端控制动作
- capture ring buffer 适当放大，减轻 Wi-Fi bring-up 与高负载下的短时溢出

### 4.5 深入检查了 trainer 项目，排除“模型训练本身坏了”的可能

trainer 侧已确认：

- Keras 模型与量化 TFLite 在 host 侧精度基本一致
- 之前主要问题不在模型训练数值，而在导出格式

相关说明见：

- [../wake-word-trainer/KWS_TFLITE_EXPORT_FIX_ZH.md](../wake-word-trainer/KWS_TFLITE_EXPORT_FIX_ZH.md)

### 4.6 trainer 导出链已经修复两类核心问题

#### 4.6.1 非量化导出不再默认生成 hybrid

现在默认非量化导出会保留纯 FP32。

#### 4.6.2 量化导出默认从 `uint8` IO 改为 `int8` IO

这是为了匹配当前 Ameba CA32 优化 kernel 的支持范围。

### 4.7 板端当前已经切回新的 `int8` 模型

当前板端嵌入的模型头文件是：

- [components/river_voice/generated/river_wake_word_model_data.h](components/river_voice/generated/river_wake_word_model_data.h)

当前状态是：

- 模型：`model_int8.tflite`
- 大小：`62968B`
- KWS arena：`160KB`

## 5. 已确认的矛盾点

### 5.1 host 侧模型正常，不代表板端 runtime 一定正常

曾经一度出现：

- host 侧读模型为量化模型，shape/type/quant 都正常
- 板端 runtime 却报告 `float32` / `bytes` 异常 / quant 异常

这说明不能把板端 runtime 元数据直接当成“模型真实定义”。

### 5.2 “纯 FP32”并不一定比 INT8 更稳

直觉上纯浮点似乎更简单，但实际遇到的是：

- 修复 hybrid 后，纯 FP32 模型虽然语义更清晰
- 但在当前板端内存预算下根本跑不起来

所以“为了回避量化兼容性而切 FP32”在当前硬件上不可持续。

### 5.3 “模型初始化成功”不等于“运行期链路没问题”

最新一轮已经能看到：

- `KWS` 初始化成功
- `wake_admission=wakeword`

但运行期仍会在第一层 `conv` 的优化 `im2col` 路径崩溃。这说明加载成功只是第一步，kernel 运行期正确性同样是独立问题。

### 5.4 业务问题与底层问题一度纠缠在一起

迁移期间同时存在：

- KWS 起不来
- playback underrun / ref overflow
- conversation window 行为不稳定
- 实验 AEC profile 带来的额外内存压力

如果不先把问题拆层，很容易把“播放问题”误当成“唤醒词问题”。

## 6. 当前困难点

### 6.1 当前最核心的困难点

目前最大的难点不是 trainer，不是模型训练，也不是 KWS 初始化，而是：

- **Ameba CA32 优化 `conv` kernel 的 `im2col` 实现存在真实越界 bug**

已确认的问题是：

- `ExtractPatchIntoBufferColumn()` 的 `single_buffer_length` 传参错误
- 当前代码把它当成 `output_depth`
- 但真正应该是 `kheight * kwidth * input_depth`

对于第一层卷积：

- 输入：`[1, 98, 40, 1]`
- 卷积核：`[64, 10, 4, 1]`
- 输出：`[1, 49, 20, 64]`

当前 kernel 申请的 scratch 大小按 patch 展平长度算是对的，但写入地址步长按 `64` 走，实际需要按 `40` 走，最终把总写入跨度扩大到了错误的范围，导致越界。

### 6.2 板端内存预算仍然紧

尽管 INT8 路线已经明显比纯 FP32 更可行，但板端剩余 heap 仍然不算宽裕，后续还需要注意：

- Wi-Fi bring-up
- 云端会话
- 播放链路
- 诊断线程

是否会进一步挤压 KWS 运行余量。

### 6.3 播放链路仍有独立稳定性问题

虽然这不是本次 KWS 初始化的根因，但在联调阶段反复出现过：

- `playback ref overflow`
- `underrun`
- `playback write failed`

它会干扰对“唤醒之后的交互行为”的判断，因此后续仍要单独收口。

## 7. 当前状态

截至本次复盘，状态可以总结为：

### 7.1 已解决的部分

- trainer 已修复 hybrid 导出问题
- trainer 已把默认量化导出从 `uint8` IO 改为 `int8` IO
- 板端 KWS 已支持 `float32 / uint8 / int8`
- 板端已改为 schema-first 的 quant/type 解析
- 板端已回到稳定的 `2ch asr_mainline`
- 新 `int8` 模型已可在板端完成 KWS 初始化
- `wake_admission=wakeword` 已出现

### 7.2 最新确认的阻塞点

- 运行期在 CA32 优化 `conv` 的 `im2col` 路径越界崩溃

### 7.3 最新已完成但尚未板测验证的修复

已在下列文件中修复 `im2col` 的列步长错误：

- [../ameba-rtos-1.2/component/tflite_micro/tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/im2col_utils.h](../ameba-rtos-1.2/component/tflite_micro/tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/im2col_utils.h)

并已完成新固件编译。当前待刷写验证的产物是：

- [build_RTL8730E/km0_km4_ca32_app.bin](build_RTL8730E/km0_km4_ca32_app.bin)
- [build_RTL8730E/ota_all.bin](build_RTL8730E/ota_all.bin)

构建时间戳为：

- `2026-03-23 11:11`

## 8. 解决方案与后续计划

### 8.1 第一优先级：验证最新 `im2col` 修复

目标：

- 刷 `11:11` 新固件
- 验证不再出现 `Data abort`
- 验证 `wake_admission` 仍保持 `wakeword`
- 验证首次 `KWS` 推理不再在第一层 `conv` 崩溃

重点观察日志：

- `kws tensor io ... effective_in=int8 effective_out=int8`
- `kws backend ... model=62968B arena=160KB`
- 不应再出现 `ExtractPatchIntoBufferColumn<int8>` 相关崩溃

### 8.2 第二优先级：确认是否真正出现 `wakeword hit`

如果 `im2col` 修复后系统稳定，需要继续确认：

- 是否真的出现本地 `wakeword detected`
- 是否由 KWS 命中打开会话，而不是旧的 `legacy_vad`
- 命中阈值、hold、cooldown 是否需要板端微调

### 8.3 第三优先级：收敛播放与会话窗口行为

在 KWS 稳定后，需要继续收敛：

- playback underrun
- ref overflow
- barge-in 与 follow-up 状态切换
- 会话窗口的打开/关闭时机

### 8.4 预备降级方案

如果最新修复后 CA32 优化 `conv` 仍不稳定，降级路线如下：

1. 临时禁用 KWS 路径中的 CA32 优化 `conv`
2. 强制该模型走 `reference_integer_ops::ConvPerChannel`
3. 先验证功能正确性，再决定是否回头修复或替换优化 kernel

这条路线的代价是性能可能下降，但它能快速把“功能正确性问题”和“优化 kernel 问题”彻底拆开。

## 9. 对当前局面的判断

从全链路看，当前最重要的结论有三条：

1. **模型训练本身不是主因。**  
   trainer 产物在 host 侧精度与结构都没有显示出“模型坏了”的证据。

2. **真正困难点在部署格式与板端 runtime/kernel。**  
   这次遇到的主要问题是：
   - hybrid 导出
   - `uint8` IO 不匹配
   - runtime 元数据不可信
   - CA32 优化 `conv` 的 `im2col` 越界

3. **迁移已经从“完全起不来”推进到“初始化成功，只剩运行期 kernel 正确性问题”。**  
   这说明方向是收敛的，当前已经不再是“摸黑排查”，而是明确的底层 bug 修复与板测确认阶段。

## 10. 相关文件

### 板端

- [components/river_voice/river_voice_kws.cc](components/river_voice/river_voice_kws.cc)
- [components/river_voice/generated/river_wake_word_model_data.h](components/river_voice/generated/river_wake_word_model_data.h)
- [components/river_core/river_app.c](components/river_core/river_app.c)
- [components/river_cloud/river_cloud_adapter.c](components/river_cloud/river_cloud_adapter.c)
- [prj.conf](prj.conf)
- [Kconfig](Kconfig)

### trainer

- [../wake-word-trainer/scripts/export.py](../wake-word-trainer/scripts/export.py)
- [../wake-word-trainer/src/export/tflite.py](../wake-word-trainer/src/export/tflite.py)
- [../wake-word-trainer/src/export/quantize.py](../wake-word-trainer/src/export/quantize.py)
- [../wake-word-trainer/KWS_TFLITE_EXPORT_FIX_ZH.md](../wake-word-trainer/KWS_TFLITE_EXPORT_FIX_ZH.md)
