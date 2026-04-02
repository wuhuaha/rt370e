# 当前分支 FP32 唤醒词模型部署门槛表

日期：2026-04-02

## 1. 目的

这份文档只回答一个问题：

- 在当前 `ameba-river` 分支、当前产品链路不大改的前提下，什么样的 `FP32` 唤醒词模型才有机会直接上板可用

这里的“当前环境”明确指：

- `fixed_dsb` 双麦前端
- `Silero VAD`
- `vad_probe` 驱动 `KWS`
- `Wi-Fi + XiaoZhi realtime`
- 当前主线工程配置，不切到“只做本地 KWS 验证”的精简 profile

如果算法侧交付的模型超出这里的门槛，就不应再按“直接替换模型、直接烧录验证”的预期推进，而应先切到专门的精简验证 profile。

## 2. 当前分支的已知基线

### 2.1 当前主线配置

见：

- [prj.conf](../prj.conf)

当前主线关键预算为：

- `KWS tensor arena = 192KB`
- `Silero VAD tensor arena = 192KB`
- `KWS worker stack = 8KB`
- `vad_probe task stack = 16KB`

对应代码位置：

- [components/river_voice/river_voice_kws.cc](../components/river_voice/river_voice_kws.cc)
- [components/river_voice/river_voice_detector_silero.cc](../components/river_voice/river_voice_detector_silero.cc)
- [components/river_voice/river_voice_vad_probe.c](../components/river_voice/river_voice_vad_probe.c)

### 2.2 当前主线运行时观测值

最近一轮稳定的 `int8` 基线板端日志显示：

- `boot_ready heap_free ≈ 185216B`
- `wifi_connected heap_free ≈ 132736B`

这两个数字非常重要，因为它们决定了：

- 当前完整产品链路下，`KWS` 相对现状还能再多吃掉多少内存
- 以及“纯 FP32”还有没有现实空间

### 2.3 已踩过的 FP32 失败基线

之前真实试过的一版纯 `FP32` 模型，已经留下了关键失败事实：

- 模型文件大小约 `133576B`
- `AllocateTensors()` 约需要 `501760B`

这组数字已经足够说明：

- 问题不是“文件只比 int8 大一点”
- 而是 `TFLM tensor arena` 需求直接冲到当前产品链路不可承受的量级

见：

- [doc/KWS_MIGRATION_REVIEW_ZH.md](./KWS_MIGRATION_REVIEW_ZH.md)

## 3. 先说结论

在当前完整环境下，`FP32` 模型想“直接可部署”，核心门槛不是 `.tflite` 文件大小，而是：

1. 模型格式必须完全兼容当前板端 KWS runtime
2. 模型结构必须完全落在当前 resolver 白名单内
3. `KWS AllocateTensors()` 所需 arena 必须压到当前链路能承受的区间
4. 上板后必须还能保留足够的 `heap` 余量，不能只做到“勉强启动”

可以直接拿来用的工程化判断是：

- `KWS arena 目标值：<= 224KB`
- `KWS arena 直接上板上限：<= 256KB`
- `KWS arena > 256KB`：不应再按当前完整环境直接部署
- `KWS arena > 288KB`：当前主线环境下直接拒绝

而之前已经试过的那种：

- `FP32 model ≈ 133KB`
- `KWS arena ≈ 501760B`

结论就是：

- 不属于“微调一下能上”
- 而是明显超出当前主线环境预算

## 4. 兼容性硬门槛表

下表里的“硬门槛”不是建议，而是当前分支直接依赖的约束。

| 项目 | 直接上板要求 | 推荐值 | 超线结论 | 说明 |
| --- | --- | --- | --- | --- |
| TFLite 格式 | 必须是真正纯 `FP32` | 全图 `float32` | 只要是 hybrid，直接拒绝 | 当前板端不接受“float IO + int8 权重”的 hybrid 路线 |
| 输入 tensor type | `float32` | `float32` | 非 `float32/int8/uint8` 直接拒绝；FP32 路线要求 `float32` | 当前 KWS runtime 虽支持多种 IO，但 FP32 路线必须保持语义单一 |
| 输出 tensor type | `float32` | `float32` | 非 `float32/int8/uint8` 直接拒绝 | FP32 路线推荐单概率输出 |
| 输入 shape | 必须被板端识别 | `[1,40,98,1]` | 非 `[1,40,98,1]` / `[1,98,40,1]` 直接拒绝 | 当前板端只接受这两种四维布局 |
| 输出 shape | 单标量 | `[1,1,1,1]` | 输出元素数不等于 `1` 直接拒绝 | 当前 KWS 是单唤醒词单分数模型 |
| 输出语义 | `0..1` 概率 | `sigmoid/logistic` 后单概率 | 原始 logits 直接拒绝 | 板端会把 `float32` 输出直接当概率并裁剪到 `[0,1]` |
| 算子集合 | 必须落在当前白名单内 | 只用当前主线路径已有算子 | 出现额外算子直接拒绝 | 不是模型问题，是当前 resolver 没注册就起不来 |
| schema 版本 | 必须匹配当前板端 `TFLITE_SCHEMA_VERSION` | 与当前 SDK 一致 | 不一致直接拒绝 | 否则 `GetModel()` / schema 检查失败 |
| custom op | 不允许 | 无 | 直接拒绝 | 当前 resolver 不支持 custom op |
| legacy 依赖 | 不允许依赖 `MEAN/FULLY_CONNECTED` legacy compat | 无 | 直接拒绝 | 当前主线默认未打开 legacy compat |

## 5. 当前分支可接受的算子白名单

当前 KWS resolver 主线路径实际注册的是：

- `QUANTIZE`
- `PAD`
- `ADD`
- `CONV_2D`
- `DEPTHWISE_CONV_2D`
- `AVERAGE_POOL_2D`
- `LOGISTIC`

注意：

- 对“纯 FP32 模型”来说，理论上不应该再需要 `QUANTIZE`
- `MEAN` / `FULLY_CONNECTED` 属于 legacy compat 侧路，不应作为当前 FP32 交付依赖
- `RESHAPE` / `MUL` / `SOFTMAX` / `DEQUANTIZE` / `CONCATENATION` / custom op 都不应出现

算法侧如果给出超出这个白名单的图结构，就不是“模型参数要不要再压一压”的问题，而是：

- 板端代码需要先改 resolver
- 改完还要重新验证 arena、runtime 正确性和算子实现稳定性

那就已经不属于“当前环境直接部署”

## 6. 输入输出结构硬约束

### 6.1 输入结构

当前板端只接受：

- `[1,40,98,1]`
- 或 `[1,98,40,1]`

其中推荐固定为：

- `[1,40,98,1]`

原因：

- 当前板端稳定日志已经在这个布局上跑通
- 可以减少训练侧、导出侧、板端侧对“是否需要转置”的歧义

### 6.2 输入语义

输入不是原始 PCM，也不是频谱图 PNG，而是：

- `fixed_dsb` 输出的单通道音频
- `16kHz`
- `FFT=512`
- `hop=160`
- `40` 维 log-mel
- `98` 帧窗口
- 再做全局 `mean/std` 归一化

算法侧如果训练/导出不是按这套前端语义来的，即使模型能加载，也不能按“直接可用”判断。

### 6.3 输出结构

当前板端要求输出是：

- 单个标量
- 表示唤醒词命中概率

推荐固定为：

- `sigmoid/logistic` 后的单输出概率

不建议交付：

- 两类 softmax
- 多类关键词分类头
- 未经过 sigmoid 的 logit

因为当前板端逻辑会把 `float32` 输出直接当作概率解释。

## 7. 内存门槛推导

### 7.1 为什么看 `arena`，不只看模型文件大小

`.tflite` 文件大小只反映：

- 权重和图结构的静态体积

而板端真正爆掉的往往是：

- 中间激活
- 临时 buffer
- im2col / scratch
- `AllocateTensors()` 需要的连续 arena

所以在当前分支里，真正的一票否决指标是：

- `KWS AllocateTensors()` 需要多少 `tensor arena`

### 7.2 从当前主线基线反推可接受上限

当前 `int8` 基线的 `wifi_connected heap_free` 约为：

- `132736B`

如果把“Wi-Fi 连上以后至少还要保留的安全余量”定义为：

- 最低可接受余量：`64KB = 65536B`
- 推荐余量：`96KB = 98304B`

那么，相对当前 `int8` KWS，还能再增加的额外内存预算分别是：

- 最低可接受额外预算：
  - `132736 - 65536 = 67200B`
- 推荐额外预算：
  - `132736 - 98304 = 34432B`

当前 `KWS arena` 是：

- `192KB = 196608B`

于是可以直接得到：

- 推荐 `KWS arena` 上限：
  - `196608 + 34432 ≈ 231040B`
  - 向下取整到工程值：`224KB`
- 直接上板可接受上限：
  - `196608 + 67200 ≈ 263808B`
  - 向下取整到工程值：`256KB`

这就是本文后面所有门槛表的来源。

## 8. FP32 可部署门槛表

### 8.1 当前完整产品链路下的硬门槛

| 指标 | 推荐目标 | 直接上板上限 | 超线结论 | 备注 |
| --- | --- | --- | --- | --- |
| `KWS AllocateTensors()` arena | `<= 224KB` | `<= 256KB` | `> 256KB` 不应直接上当前主线 | 这是最关键的一条 |
| `.tflite` 文件大小 | `<= 80KB` | `<= 100KB` 且必须同时给出 arena 证明 | `> 100KB` 不应按“直接可用”判断 | 文件大小只是次级筛选，不是最终准入 |
| `boot_ready heap_free` | `>= 128KB` | `>= 96KB` | `< 96KB` 进入高风险区 | 这里看的是替换 FP32 后的实测板端日志 |
| `wifi_connected heap_free` | `>= 96KB` | `>= 64KB` | `< 64KB` 直接拒绝 | 当前产品链路必须经得住连网 |
| 一次唤醒后 `heap_min` | `>= 80KB` | `>= 64KB` | `< 64KB` 直接拒绝 | 否则后续会话或 debug 很容易出堆故障 |
| 唤醒后第一轮会话前无 `Malloc failed` / IPC 超时 | 必须满足 | 必须满足 | 任一出现即拒绝 | 说明不是“还能优化”，而是当前配置已经不稳 |

### 8.2 如何理解 `.tflite` 文件大小门槛

这里给出文件大小门槛，是为了让算法侧在导出前就有一个快速筛选标准，但必须强调：

- 文件大小不是一票通过条件
- arena 才是一票通过条件

建议这样使用：

| `.tflite` 大小 | 工程判断 |
| --- | --- |
| `<= 80KB` | 可以继续看 arena，通常有希望直接上当前主线 |
| `81KB ~ 100KB` | 必须附带可信的 arena 测量证明，否则不应直接上板 |
| `101KB ~ 120KB` | 不建议按当前主线直接部署，先走精简验证 profile |
| `> 120KB` | 当前主线直接拒绝 |

这条线来自已经踩过的经验：

- `133576B` 的纯 FP32 模型最终对应了约 `501760B` arena

所以对当前分支来说，`> 120KB` 的纯 FP32 模型已经非常不友好。

## 9. 算法侧必须交付的“直接可用”材料

如果目标真的是：

- 算法调完模型
- 板端尽量一次替换成功

那么算法侧交付不能只给一个 `.tflite` 文件，至少还要给下面这份交付包。

### 9.1 模型与导出物

- 最终 `.tflite` 文件
- 对应训练 commit id
- 对应导出脚本 commit id
- 模型文件 `SHA256`
- 导出命令行

### 9.2 结构摘要

- 输入 type
- 输出 type
- 输入 shape
- 输出 shape
- 全部算子列表
- 是否存在量化参数
- 是否存在 `QUANTIZE/DEQUANTIZE` 节点

### 9.3 数值与阈值摘要

- 训练侧验证集正样本分数分布
- 训练侧验证集 hard-negative 分数分布
- 推荐阈值
- 推荐阈值对应的：
  - `FRR`
  - `FAR`
  - 正样本 `P05 / P50 / P95`
  - 负样本 `P95 / P99`

### 9.4 内存与部署摘要

- `.tflite` 文件大小
- 估计或实测 `AllocateTensors()` arena
- 如果没有 TFLM 实测，至少给出：
  - 各层输出 shape
  - 最大激活张量 shape
  - 最大通道宽度
  - 是否存在早期大通道残差块

### 9.5 交付缺项的处理原则

如果算法侧没有给出：

- arena 证明
- 算子白名单证明
- 阈值推导

则这份模型只能按：

- “候选模型，待板端摸底”

处理，不能按：

- “直接可用模型”

处理。

## 10. 算法设计建议

如果目标是当前主线直接部署纯 `FP32`，算法侧要优先优化的不是“精度数字看起来漂亮”，而是下面这些更影响板端可部署性的因素。

### 10.1 优先压 `arena`，不是只压文件

优先做：

- 降低早期层通道数
- 降低中间激活峰值
- 避免宽残差块叠加
- 降低临时大张量峰值

不要只盯着：

- 参数量
- `.tflite` 文件大小

### 10.2 优先使用当前已跑通的结构族

优先选择：

- 当前主线已知白名单内的 `Conv2D / DWConv / Add / AvgPool / Logistic`

避免为了追一点离线精度，引入：

- 新算子
- 多输出头
- softmax 多分类头
- 需要额外 reshape / concat / mul 的复杂尾部

### 10.3 输出头保持单概率

最推荐的尾部形式是：

- 单标量
- `sigmoid/logistic`
- 概率范围稳定落在 `[0,1]`

这样板端的阈值解释和日志都最直接。

## 11. 一次上板前的准入清单

算法侧只要有一项过不了，就不要按“当前主线直接部署”推进。

### 11.1 导出前自检

- 是不是纯 `FP32`
- 输入是不是 `float32`
- 输出是不是 `float32`
- 输入 shape 是不是 `[1,40,98,1]`
- 输出是不是单标量
- 算子是不是只落在当前白名单
- `.tflite` 大小是不是 `<= 100KB`

### 11.2 导出后自检

- 有没有 `QUANTIZE/DEQUANTIZE`
- 有没有量化参数残留
- 有没有多输出
- 输出是不是已经是概率，不是 logit
- 推荐阈值是否已经推导完成

### 11.3 上板准入线

- 预估或实测 `KWS arena <= 256KB`
- 预估或实测替换后 `wifi_connected heap_free >= 64KB`
- 推荐阈值和 hard-negative 结果已一并交付

## 12. 首轮上板验收线

第一次烧录后，只看下面这些日志，不要先急着听效果。

### 12.1 必须出现

- `kws tensor io: runtime_in=float32 runtime_out=float32 model_in=float32 model_out=float32 effective_in=float32 effective_out=float32`
- `kws input shape: ... dims=[1,40,98,1] ...`
  - 或 `[1,98,40,1]`
- `kws output shape: ... values=1`
- `kws backend: ... arena=...KB model=...B ...`

### 12.2 绝不能出现

- `kws model/schema unsupported`
- `kws AllocateTensors failed`
- `kws tensor type unsupported`
- `kws input shape unsupported`
- `kws output shape unsupported`
- `Malloc failed`

### 12.3 实测资源门槛

- `boot_ready heap_free >= 96KB`
- `wifi_connected heap_free >= 64KB`
- 一次唤醒 + 一次 follow-up 窗口后 `heap_min >= 64KB`

只要其中任一不满足，就不要把问题描述成“阈值还要再调一调”，而应先回到：

- 模型结构
- arena
- 产品链路预算

重新评估。

## 13. 快速判定示例

### 示例 A

- `.tflite = 72KB`
- `arena = 208KB`
- 单概率 `float32` 输出
- 白名单算子

结论：

- 可以按当前主线直接部署

### 示例 B

- `.tflite = 94KB`
- `arena = 248KB`
- 单概率 `float32` 输出
- 白名单算子

结论：

- 可以尝试直接部署
- 但必须关注 `wifi_connected heap_free` 是否还能保住 `64KB`

### 示例 C

- `.tflite = 108KB`
- `arena = 272KB`

结论：

- 不应按当前主线直接部署
- 应先切到精简验证 profile

### 示例 D

- `.tflite = 133KB`
- `arena = 501760B`

结论：

- 当前主线直接拒绝
- 不属于“再调阈值”能解决的范围

## 14. 最终建议

如果目标是：

- 当前分支
- 不大改产品链路
- 模型替换后直接可用

那么算法侧应把纯 `FP32` 模型收敛到下面这组目标：

- 纯 `FP32`
- 输入：`float32 [1,40,98,1]`
- 输出：单标量 `float32` 概率
- 只使用当前板端白名单算子
- `.tflite <= 80KB`，最好不要超过 `100KB`
- `KWS AllocateTensors() arena <= 224KB`
- 最多不要超过 `256KB`

只要 `arena` 还在 `300KB+`，尤其是接近之前那种 `500KB` 量级，就不应该继续按“当前主线直接部署 FP32”推进。
