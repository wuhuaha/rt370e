# Student INT8 板端实时性实测分析

日期：2026-04-07

## 1. 目的

这份文档只回答一个问题：

- `student_bc_resnet_tiny_v2` 已经做了 `INT8` 量化，为什么在 `RTL8730E` 板端实时性还是几乎没有改善，甚至看起来比之前 `FP32 debug` 还差

这里讨论的是已经完成：

- 固件集成
- 板端/本机精确对拍
- 板端实测 `infer_us`

之后的结论，不再是“预估”，而是基于当前板端真实运行结果。

## 2. 结论

先说结论：

- 当前 `student INT8` 部署是正确的，板端和本机结果已经严格一致。
- 但当前 `student INT8` 在板端并没有获得预期中的实时性收益，实测 `infer_us` 约为 `2.34s`，远高于此前 student `FP32 debug` 的 `~675ms`。
- 这不说明“INT8 量化没有价值”，而说明：
  - 当前 `RTL8730E` 这条 `TFLite Micro + CA32` 运行路径上，`INT8` 主算子没有走高效优化 kernel
  - 当前 student 模型拓扑本身也没有变轻，仍然是高成本图
- 所以当前现象的根因不是“量化没生效”，而是：
  - `INT8` 数值格式生效了
  - 但板端没有拿到与之匹配的高效 `INT8 kernel`
  - 反而退回到了更慢的 reference 实现

一句话概括：

- 现在慢，不是因为 `INT8` 这个方向错了
- 而是因为当前板端实际跑的不是“优化后的 INT8”，而是“reference INT8 + 高成本 student 图”

## 3. 当前板端事实

本轮实测已经明确：

- 板端变体：`student_bc_resnet_tiny_v2_int8_debug`
- 输入输出：`int8 -> int8`
- 输入 shape：`1 x 40 x 101 x 1`
- 板端 / 本机严格对拍通过：
  - `quant_parity: diff_bytes=0/4040`
  - `output_parity: bytes_equal=yes raw_equal=yes`
  - board / host 都得到：
    - `raw=-28`
    - `score=0.390625`

所以首先可以排除：

- 量化参数接错
- 输入张量填充错误
- 板端 `int8` requantize 错误
- 输出反量化错误
- 模型文件和板端嵌入 blob 不一致

当前问题已经可以直接收敛到：

- 板端 runtime 性能路径
- 模型结构成本

而不是部署正确性。

## 4. 为什么 INT8 反而没有带来明显收益

## 4.1 第一主因：CA32 的 INT8 `conv` 已被 SDK 明确强制退回 reference kernel

这是当前最关键的直接证据。

在 SDK 的 CA32 `conv` kernel 实现里：

- [conv.cc](/root/ameba-rtos-1.2/component/tflite_micro/tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/conv.cc#L220)

可以看到 INT8 路径里有明确注释：

- 当前 CA32 的 `int8 conv optimized path` 对当前唤醒词模型“不可靠”
- 因此直接使用 `reference_integer_ops::ConvPerChannel(...)`

也就是说，当前板端真正执行的不是：

- 优化过的 CA32 INT8 GEMM / im2col 路径

而是：

- TFLM reference integer conv

这件事足以解释为什么：

- `INT8` 虽然张量更小
- 但整体推理仍然可能比之前更慢

因为运行时主耗时并没有进入高效实现。

## 4.2 第二主因：CA32 的 INT8 `depthwise` 也被 SDK 强制退回 reference kernel

同样的情况在 depthwise kernel 里也存在：

- [depthwise_conv.cc](/root/ameba-rtos-1.2/component/tflite_micro/tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/depthwise_conv.cc#L141)

这里同样写明：

- 当前 CA32 的 optimized INT8 depthwise kernel 对当前 wakeword 模型“不可靠”
- 因此使用 `reference_integer_ops::DepthwiseConvPerChannel(...)`

虽然当前 student 模型主集合里没有历史那种大量 depthwise block，但这说明一个更大的事实：

- 当前 CA32 INT8 优化路径整体并不成熟
- 至少在这条 wakeword 模型线上，SDK 维护者已经主动选择“保正确，放弃性能”

## 4.3 第三主因：student 图里除卷积外，大量逐元素算子也基本都是 reference 路径

当前 student 图的关键算子统计是：

- `CONV_2D x14`
- `LOGISTIC x11`
- `MUL x10`
- `ADD x4`
- `AVERAGE_POOL_2D x1`

而当前 TFLM 实现里：

- [mul.cc](/root/ameba-rtos-1.2/component/tflite_micro/tensorflow/lite/micro/kernels/mul.cc#L45)
  的量化 `MUL` 走的是 `EvalMulQuantizedReference(...)`
- [logistic.cc](/root/ameba-rtos-1.2/component/tflite_micro/tensorflow/lite/micro/kernels/logistic.cc#L78)
  的 `int8 logistic` 走的是 `reference_integer_ops::Logistic(...)`
- [add.cc](/root/ameba-rtos-1.2/component/tflite_micro/tensorflow/lite/micro/kernels/add.cc#L113)
  的 `int8 add` 走的是 `reference_integer_ops::Add(...)`
- [pooling.cc](/root/ameba-rtos-1.2/component/tflite_micro/tensorflow/lite/micro/kernels/pooling.cc#L33)
  的 average pool 也不是 CA32 专用高性能路径

而当前 student 图恰恰又包含多组：

- `LOGISTIC + MUL`

这种门控结构。

所以现在的运行现实是：

- 不只是主卷积慢
- 连大量辅助逐元素算子也没有拿到强优化

这会把量化本该带来的收益进一步吃掉。

## 4.4 第四主因：当前 student INT8 的图结构根本没有变轻

`INT8` 和 `FP32` 当前是同一 student 家族同一拓扑，只有 dtype 不同。

现有记录已经确认它们共享相同的高成本结构：

- `CONV_2D x14`
- `LOGISTIC x11`
- `MUL x10`
- `ADD x4`
- `AVERAGE_POOL_2D x1`

同时前端契约仍然是：

- `40 x 101`
- `n_fft = 400`
- `hop = 160`
- `center = true`
- `natural_log`
- `per_clip_mean_std`

所以量化并没有带来：

- 更早下采样
- 更小 feature map
- 更少层数
- 更少门控
- 更少 MAC

它带来的只是：

- 更小权重
- 更小激活
- 更低内存带宽压力

但当前板端最大的瓶颈是：

- “高成本结构”乘上“reference INT8 kernel”

这就决定了它不可能像桌面侧那样自然变快很多。

## 4.5 第五主因：之前的本地预估成立前提，在板端并不成立

之前对 INT8 的乐观判断，主要来自两类本地证据：

- x86 上 `XNNPACK / SIMD` 的更快路径
- 或至少 host 上的相对速度比

但当前板端实际运行环境是：

- `RTL8730E CA32`
- `TFLite Micro`
- 当前 wakeword 线上 INT8 主算子退回 reference

所以 host 上的：

- `INT8 比 FP32 快 1.69x`
- 或 `INT8 比 FP32 快 3.54x`

这些结论并不能直接迁移到当前板端。

真正板端生效的是另一条不等式：

- host 上“INT8 更快”
- 不代表 board 上“INT8 kernel 也更快”

## 5. 为什么它甚至比当前 FP32 看起来更差

这个现象也是可以解释的。

此前 student `FP32 debug` 虽然慢，但它的主耗时 `conv` 在 CA32 上仍有专门优化实现：

- [conv.cc](/root/ameba-rtos-1.2/component/tflite_micro/tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/conv.cc#L71)

可以看到 FP32 `ConvFloat(...)` 仍然会走：

- `optimized_ops::Im2col(...)`
- `cpu_backend_gemm::Gemm(...)`

也就是：

- FP32 这边虽然数据更大
- 但至少主算子仍有 CA32 专用优化

而当前 INT8：

- 主 `conv` 退回 reference
- 大量 `LOGISTIC / MUL / ADD` 也仍是 reference

因此会出现一个很反直觉但完全合理的结果：

- “优化过的 FP32 主卷积”
- 可能比
- “reference 的 INT8 整图”
- 更快

所以这次并不是量化把模型“变慢了”，而是：

- 板端 runtime 对 FP32 更友好
- 对当前 INT8 这组算子组合反而更不友好

## 6. 这说明接下来该优化什么

当前结论非常明确，优化优先级应该这样排：

1. 不要继续纠结阈值、串口、pre-roll、101 帧这些二级问题  
   当前 `2.34s` 的主要矛盾不在这些地方。

2. 如果想让这条 INT8 线真正有实时性，先解决 runtime kernel 路径  
   也就是先确认：
   - CA32 的 INT8 `conv` 优化路径能否修好并重新启用
   - 当前模型是否会触发该路径的已知错误
   - 若不能快速修复，是否需要换更成熟的 INT8 backend

3. 如果 runtime 侧短期修不好，就必须让算法侧继续降结构成本  
   因为现在的 student 图仍然太重，主要方向是：
   - 更早下采样
   - 减少 dense `CONV_2D`
   - 减少大特征图上的门控
   - 优先使用更板端友好的 block

4. 继续保留当前 INT8 / FP32 双变体与对拍链路  
   当前最大价值不是“体验已经好”，而是：
   - 部署问题已经被排除
   - 后面看到的性能问题就是模型 / runtime 问题本身

## 7. 最终结论

当前 `student INT8` 板端几乎没有拿到预期中的实时性提升，根本原因不是：

- 量化参数错了
- 板端接入错了
- 串口日志拖慢了
- 101 帧写死了

而是：

- 当前 student 图依然很重
- 当前 `RTL8730E CA32` 的 INT8 主算子没有走高效优化实现
- 其中最关键的是 `conv` 已被 SDK 明确强制退回 reference kernel

所以当前最准确的判断应该是：

- `INT8` 方向本身没有错
- 但当前这版 `student INT8` 在这套 board runtime 上，暂时没有站在“优化 INT8 kernel”这条路上
- 它实际跑的是“reference INT8”

这也是为什么它没有体现出预期的实时性收益。
