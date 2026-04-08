# KWS INT8 / INT16 部署约束与选型限制

日期：2026-04-08

## 1. 目的

本文只回答一个问题：

- 为什么当前 `INT8 / INT16` 量化部署在 `RTL8730E` 上没有体现出预期收益，甚至会比现有 `FP32 debug` 更慢
- 这件事对后续算法训练、导出和模型选择到底施加了哪些硬约束

本文不是泛泛讨论量化理论，而是基于当前仓库、当前 SDK 和当前板端实测结果收敛出的工程结论。

## 2. 结论

先给结论：

- 当前 `INT8` 部署“数值上已经生效”，但“性能上没有获得对应的高性能 kernel 红利”。
- 当前 `INT8` 慢，不是因为量化接错，也不是前端接错，而是因为 `RTL8730E CA32 + TFLite Micro` 这条路径上，量化主算子大面积落在 reference kernel。
- 当前 `INT16` 更严格：它不只是“可能不快”，而是目前这套 KWS 应用链路根本没有放行 `INT16` 输入/输出 tensor，host 对拍工具也没有完整支持，所以它不是一个当前可交付的部署目标。
- 因此，对后续模型训练和选择，必须先接受一个现实：
  - 不能把“量化后理论更快”当成板端可用前提
  - 必须先满足当前 runtime / app / tooling 的约束，再谈模型质量和阈值

一句话概括：

- 现在真正限制模型上板的，不只是模型本身，而是“模型拓扑 x 当前板端 kernel 覆盖率 x 当前 KWS 接入链路能力”的乘积。

## 3. 当前板端已经确认的事实

### 3.1 INT8 部署正确，但实时性明显劣化

当前 `student_bc_resnet_tiny_v2_int8_debug` 已经完成：

- 板端集成
- 板端 / host 精确对拍
- 板端实测 `infer_us`

现有事实：

- 变体：`student_bc_resnet_tiny_v2_int8_debug`
- 输入输出：`int8 -> int8`
- shape：`1 x 40 x 101 x 1`
- 对拍通过：
  - `quant_parity: diff_bytes=0/4040`
  - `output_parity: bytes_equal=yes raw_equal=yes`
- 实测板端时延：
  - `infer_us ≈ 2.34s`

这已经可以排除：

- 量化 scale / zero-point 接错
- `input_raw` 填充错误
- 板端 `int8` requantize 错误
- 输出解释错误
- 模型文件和板端 blob 不一致

也就是说：

- 当前 INT8 的主要问题已经不是部署正确性，而是运行时性能路径

### 3.2 当前 FP32 并不快，但主卷积至少还有优化路径

当前 `student_bc_resnet_tiny_v2_fp32_debug` 板端实测约：

- `infer_us ≈ 675ms`

当前 `student_bc_resnet_nano_v2_fp32_debug` 板端实测约：

- `infer_us ≈ 278ms`

这两个结果说明：

- 结构变轻确实能明显改善时延
- 但即使是 nano FP32，也还远高于当前 `160ms` stride 预算

更关键的是：

- 现有 CA32 `FP32 conv` 仍能走 `Im2col + Gemm` 优化路径
- 所以“优化过的 FP32 主卷积”可能会比“reference INT8 整图”更快

### 3.3 当前 INT16 不是可直接部署目标

当前 KWS 应用层在初始化时明确只接受：

- `UInt8`
- `Int8`
- `Float32`

见：

- [components/river_voice/river_voice_kws.cc](../components/river_voice/river_voice_kws.cc)

其中 `effective_input_type` / `effective_output_type` 若不是上述三种，直接报：

- `kws tensor type unsupported`

因此：

- 即使 SDK 某些底层 kernel 有 `int16` 分支
- 当前 `ameba-river` 这条 KWS 业务接入链路也不会接受 `int16` 模型 I/O

同时当前 host 对拍工具：

- [tools/kws/replay_board_tensor_dump.py](../tools/kws/replay_board_tensor_dump.py)

也只支持：

- `int8`
- `uint8`
- `float32`

不支持：

- `int16`

所以当前 INT16 不是“待优化的可运行方案”，而是“端到端尚未接通的方案”。

## 4. 为什么 INT8 没有得到预期收益

## 4.1 CA32 的 INT8 `conv` 已被 SDK 明确强制退回 reference

这是当前最硬的证据。

见：

- `/root/ameba-rtos-1.2/component/tflite_micro/tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/conv.cc`

代码中明确写明：

- 当前 CA32 的 `int8 conv optimized path` 对当前 wakeword 模型“不可靠”
- 因此直接执行 `tflite::reference_integer_ops::ConvPerChannel(...)`

这意味着当前板端真正执行的不是：

- 优化的 CA32 INT8 conv

而是：

- reference INT8 conv

这件事本身就足以解释：

- 为什么张量更小了
- 但推理反而更慢

## 4.2 CA32 的 INT8 `depthwise_conv` 同样被强制退回 reference

见：

- `/root/ameba-rtos-1.2/component/tflite_micro/tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/depthwise_conv.cc`

里面同样明确写明：

- 当前 CA32 optimized INT8 depthwise kernel 对当前 wakeword 模型“不可靠”
- 因此执行 `reference_integer_ops::DepthwiseConvPerChannel(...)`

这说明当前量化 runtime 的问题不是局部算子，而是：

- quantized 主卷积族整体缺乏可信的高性能路径

## 4.3 INT8 / INT16 的逐元素算子也基本都在 reference 路径

当前 student 模型的关键算子组合是：

- `CONV_2D`
- `LOGISTIC`
- `MUL`
- `ADD`
- `AVERAGE_POOL_2D`

而现有 SDK / TFLM 路径里：

- `mul.cc`
  - `int8/int16` 走 `EvalMulQuantizedReference(...)`
- `logistic.cc`
  - `int8/int16` 走 `reference_integer_ops::Logistic(...)`
- `add.cc`
  - `int8` 走 `reference_integer_ops::Add(...)`
  - `int16` 走 `reference_ops::Add(...)`
- `pooling.cc`
  - `int8/int16` 都走通用 quantized pooling 逻辑

所以当前量化路径不是“只有 conv 慢”，而是：

- 主卷积慢
- 大量门控和逐元素运算也没有进入专门高性能实现

而 student 家族模型恰恰又大量使用：

- `LOGISTIC + MUL`

这会把量化本应带来的收益继续吃掉。

## 4.4 当前 student 图本身并没有变轻

当前 `INT8` 和 `FP32` student 模型共享同一拓扑风格：

- 前端仍是 `40 x 101`
- 高分辨率特征图保留时间较长
- 仍然包含双位数 `CONV_2D`
- 仍然包含大量 `LOGISTIC + MUL`

也就是说：

- 量化改变的是 dtype
- 没有改变高成本结构本身

所以当前板端面对的其实是：

- “高成本图”
- 乘上
- “reference quantized kernel”

这组合天然不可能接近理论上的量化加速预期。

## 5. INT16 为什么当前不应作为目标

## 5.1 业务接入链路没有放行 INT16 I/O

当前 KWS 业务代码只接受：

- `uint8`
- `int8`
- `float32`

所以只要模型是：

- `int16 input`
- 或 `int16 output`

当前初始化就会直接失败，不是慢不慢的问题。

## 5.2 当前 host 对拍工具也没有 INT16 支撑

现有对拍工具只支持：

- `int8`
- `uint8`
- `float32`

这意味着即使临时把板端勉强接通，也会失去现有最关键的：

- 板端 / host exact parity 机制

而这正是你前面明确要求必须保留的调试基线。

## 5.3 从 kernel 路径看，INT16 也没有性能前景

即使只看 SDK 内核实现，不考虑 app 和工具链限制：

- `conv.cc` 的 `kTfLiteInt16` 路径也是 `reference_integer_ops::ConvPerChannel(...)`
- `depthwise_conv.cc` 的 `kTfLiteInt16` 路径也是 `reference_integer_ops::DepthwiseConvPerChannel(...)`
- `logistic.cc` / `add.cc` / `mul.cc` 的 `int16` 也没有 CA32 专用高性能路径

因此：

- INT16 当前既没有端到端接入能力
- 也没有“明显优于 INT8”的 runtime 依据

对当前项目来说，INT16 应视为：

- 暂不支持
- 暂不建议作为训练交付目标

## 6. 后续模型训练与选择的硬限制

下面这些限制条件，不是建议，而是当前板端现实决定的约束。

### 6.1 量化目标只能优先选 INT8，不能选 INT16

当前量化交付目标如果不是 `INT8`，而是 `INT16`，就会同时撞上三层限制：

- 应用层 I/O 不支持
- host 对拍工具不支持
- SDK quantized kernel 也没有性能优势证据

所以当前阶段的硬约束是：

- 量化交付只接受 `INT8`
- `INT16` 不进入当前板端交付清单

### 6.2 模型 op 集必须收敛在当前已接通 resolver 子集内

当前 KWS resolver 已注册的核心算子集合是：

- `QUANTIZE`
- `PAD`
- `ADD`
- `CONV_2D`
- `DEPTHWISE_CONV_2D`
- `AVERAGE_POOL_2D`
- `MUL`
- `LOGISTIC`

这意味着：

- 后续模型训练和导出，不能随意引入新的 op
- 一旦引入新的 op，就会先变成集成问题，而不是模型问题

因此当前交付硬限制是：

- 新模型必须先证明其 op 集完全落在当前 resolver 能力范围内

### 6.3 即使 op 集合法，也不能默认具备实时性

当前最容易误判的一点是：

- “op 都支持了” 不等于 “op 都快”

对当前 `RTL8730E CA32 + TFLM` 路径来说，真正重要的是：

- 这些 op 是否有可靠的量化优化 kernel

当前实情是：

- `INT8/INT16 conv` 没有可靠优化路径
- `INT8/INT16 depthwise` 没有可靠优化路径
- `MUL / LOGISTIC / ADD / AVG_POOL` 也基本是 reference 风格

所以后续模型选择时，必须把下面这条当成硬限制：

- 不要选择“严重依赖 reference quantized kernel 才能跑起来”的模型

### 6.4 必须避免大量 `LOGISTIC + MUL` 门控结构

当前 student 家族最不适合这条板端 runtime 的一个结构特征就是：

- 高频出现 `LOGISTIC + MUL`

因为这意味着：

- 不是只有卷积在算
- 大量逐元素门控也在 reference 路径里消耗时间

所以当前训练和选型限制应明确成：

- 优先选择没有或极少 `LOGISTIC + MUL` 门控的结构
- 如果必须用门控，数量必须显著少于当前 student 家族

### 6.5 必须尽快降采样，不能长期停留在 `40x101` 高分辨率阶段

当前 student 家族还有一个根本问题：

- 高分辨率特征图保留得太久

这对板端意味着：

- activation 大
- scratch 大
- 访存重
- 参考 kernel 更吃亏

因此当前训练和选型限制应明确成：

- 模型必须在前几层就明显降低时间维或频率维
- 不能长时间在 `40x101` 或接近这个尺度上堆大量通道和卷积

### 6.6 必须把“板端实时性”当成一票否决条件，而不是事后优化项

当前现网 KWS 推理节拍是：

- `stride=16`
- `hop=10ms`
- 理论预算约 `160ms`

工程上真正可接受的模型，不应只是“略低于 160ms”，而应留出明显余量。

建议约束分层如下：

- `>160ms`
  - 直接判定为不能进入 live 主链
- `80ms ~ 160ms`
  - 只能作为继续优化候选，不能替换主链
- `<=80ms`
  - 才有资格进入下一轮 live 交互验证

这是一条工程约束，不是算法论文指标。

### 6.7 必须保留 FP32 bundle 作为量化对照基线

当前经验已经证明：

- 量化板测若没有 FP32 对照，很容易把模型问题和部署问题混在一起

所以后续交付硬限制应明确成：

- 算法交付量产候选时，必须同时给：
  - `FP32 TFLite`
  - `INT8 TFLite`
  - `frontend_contract.json`
  - `threshold_profiles.json`
  - `golden_debug_pack`

否则就不满足当前板端排障要求。

## 7. 对算法同事的直接要求

如果后续模型是为了当前 `RTL8730E` 板端实际部署，而不是只做离线指标展示，那么建议直接把要求收敛成下面几条：

1. 不要再给 `INT16` 作为当前主交付格式。
2. 量化交付优先 `INT8`，并同时保留同版本 `FP32`。
3. 模型结构要避免大量 `LOGISTIC + MUL` 门控。
4. 必须尽早下采样，避免在 `40x101` 高分辨率阶段堆很多卷积和通道。
5. 不能只看离线 `cpu_peak_budget_ms`，必须把板端 `infer_us` 当成真正验收指标。
6. 任何新模型都必须先通过板端 / host exact parity，再谈阈值、误唤醒和召回。

## 8. 当前最务实的工程结论

当前最务实的结论不是：

- “继续把 INT8/INT16 当成自然会更快的方向”

而是：

- 在当前 runtime 不变的前提下，后续模型必须主动适配这条板端的 kernel 现实

具体来说：

- `INT8` 可以继续做，但只能面向“更轻、更少门控、更早降采样”的模型
- `INT16` 当前不进入候选
- 若算法坚持保留当前这类高成本 student 拓扑，则必须先修 runtime quantized kernel，再讨论量化部署收益
