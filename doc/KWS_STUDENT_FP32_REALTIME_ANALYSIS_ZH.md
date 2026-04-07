# Student FP32 实时性分析

日期：2026-04-07

## 1. 目的

这份文档只回答一个问题：

- 当前 `student_bc_resnet_tiny_v2_fp32_debug` 为什么在 RTL8730E 板端明显不具备实时性
- 哪些因素是主因，哪些只是次要因素
- 如果要继续推进，这条线应该怎样优化

这里的“实时性”不是抽象概念，而是指：

- 唤醒词检测结果能否在用户正常说话节奏内返回
- KWS 不会持续欠账、积压队列
- 唤醒后的 ASR/TTS 交互不被明显拖慢

## 2. 结论

结论先说清楚：

- 当前 `student_bc_resnet_tiny_v2_fp32_debug` 的实时性问题，主因不是内存，也不是串口，也不是阈值，而是模型图本身对当前 `RTL8730E + TFLite Micro FP32` 运行时过重。
- 当前板端实测 `infer_us ≈ 675 ms`，而现网配置的推理节拍预算只有 `160 ms`。这意味着 KWS worker 天然处于持续欠账状态。
- 当前 student FP32 图相比历史参考板端友好模型，计算规模和激活开销都明显膨胀，已经超过这条产品链路可以承受的实时上限。
- 这条 FP32 变体当前适合作为“部署正确性 / 量化偏差 / 板端与本机对拍”调试模型，不适合作为当前全链路 live 交互运行模型。
- 若目标是让 student 家族真正上板实时运行，优先方向应是切回 `INT8` 正式交付变体，并同步推动算法侧做结构级瘦身，而不是继续在当前 FP32 图上做小修小补。

## 3. 当前板端事实

### 3.1 当前编进固件的是 student FP32 debug 变体

见：

- [prj.conf](../prj.conf)
- [components/river_voice/river_voice_kws.cc](../components/river_voice/river_voice_kws.cc)

当前配置启用了：

- `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_TINY_V2_FP32_DEBUG=y`

对应板端 KWS 变体名：

- `student_bc_resnet_tiny_v2_fp32_debug`

### 3.2 当前 student 板端契约

见：

- [components/river_voice/river_voice_kws.cc](../components/river_voice/river_voice_kws.cc)
- `/root/kws-trainint/artifacts/exports/student_bc_resnet_tiny_v2/frontend_contract.json`
- `/root/kws-trainint/artifacts/exports/student_bc_resnet_tiny_v2/deployment_summary.json`

当前 student FP32 板端关键契约是：

- 输入 shape：`[1, 40, 101, 1]`
- `40` mel bins
- `101` 帧
- `n_fft = 400`
- `hop = 160`
- `center = yes`
- `natural_log`
- `per_clip_mean_std_normalize`
- 输入 / 输出 dtype：`float32`

这已经不是历史参考链路的：

- `40 x 98`
- `db_relative_max`
- 全局均值方差归一化

### 3.3 当前板端实时性实测值

近期日志多次出现：

- `kws infer slow: ... us=675xxx`
- `kws perf: infer_us[last≈675ms avg≈675ms max≈675ms]`

这是核心事实。

当前固件配置的推理节拍见：

- [prj.conf](../prj.conf)

配置为：

- `CONFIG_RIVER_KWS_INFERENCE_STRIDE_FRAMES=16`

在当前 `hop = 10 ms` 契约下，等价于：

- 每 `160 ms` 允许触发一次推理

所以现状是：

- 实测推理时延约 `675 ms`
- 节拍预算约 `160 ms`
- 超预算约 `4.2x`

仅从这组数字就足以解释为什么唤醒链路会明显拖慢。

### 3.4 算法 bundle 自身声明的预算

见：

- `/root/kws-trainint/artifacts/exports/student_bc_resnet_tiny_v2/board_runbook.md`
- `/root/kws-trainint/artifacts/exports/student_bc_resnet_tiny_v2/deployment_summary.json`

算法交付物里声明的目标预算是：

- `board_memory_budget_kb = 768`
- `cpu_peak_budget_ms = 30.0`

而当前板端实测已经是：

- arena 实际使用约 `4.7 MB`
- `Invoke()` 时延约 `675 ms`

两边差距不是一个量级。

## 4. `infer_us` 到底测了什么

这是判断根因时非常关键的一点。

见：

- [components/river_voice/river_voice_kws.cc](../components/river_voice/river_voice_kws.cc)

板端计时逻辑是：

1. 先执行 `river_voice_kws_fill_input_tensor(context)`
2. 再记录 `start_us`
3. 调用 `context->interpreter->Invoke()`
4. 结束后计算 `elapsed_us`

这意味着：

- `infer_us` 统计的是推理主过程，也就是 `Invoke()`
- 它不包含 `fill_input_tensor()` 的前端张量填充成本

所以当前 `≈675 ms` 的主要问题不是“前端算得慢”，而是：

- 模型本体在当前运行时太慢

前端仍然有成本，但不是压倒性主因。

## 5. 为什么它这么慢

## 5.1 第一主因：图结构回退到了板端不友好的方向

当前 student FP32 图的算子集合是：

- `ADD`
- `AVERAGE_POOL_2D`
- `CONV_2D`
- `LOGISTIC`
- `MUL`

而历史板端友好参考模型的算子集合是：

- `PAD`
- `CONV_2D`
- `DEPTHWISE_CONV_2D`
- `ADD`
- `AVERAGE_POOL_2D`
- `LOGISTIC`

最关键的差异不在“多了一个 `MUL`”本身，而在于结构风格变了：

- 历史参考图：更偏向早期下采样 + depthwise-separable 路线
- 当前 student 图：大量高分辨率 `CONV_2D`，并在大特征图上反复做 `LOGISTIC + MUL`

这对桌面推理不一定是大问题，但对当前板端 `FP32 + TFLM` 来说非常不友好。

## 5.2 第二主因：高分辨率阶段保留得太久

从 TFLite 图可以直接看出：

- student 输入是 `1 x 40 x 101 x 1`
- 前几层长期停留在 `40 x 101 x 24`
- 之后才降到 `40 x 51`
- 再降到 `40 x 26`

也就是说：

- 在很长一段图里，时间维和频率维都保持得很大
- 且通道数已经扩到 `24 / 48`

这意味着：

- 大量 3x3 dense conv 都发生在大特征图上
- 中间激活张量也非常大

而历史参考 FP32 图的早期特征图很快就变成：

- `20 x 49`

这对板端算力和缓存都友好得多。

## 5.3 第三主因：大量 `LOGISTIC + MUL` 门控开销

当前 student 图中，除卷积之外，还包含多组：

- `LOGISTIC`
- `MUL`

其效果本质上接近：

- `SiLU / Swish` 一类门控激活

问题在于：

- `sigmoid` 本身就是昂贵浮点运算
- 这些操作发生在较大的 feature map 上
- 当前 TFLM 路径没有把这类组合做成高效 fused kernel

所以它不是唯一主因，但会显著加重时延尾部。

## 5.4 第四主因：大激活 + 大 arena 带来的内存带宽压力

当前 student FP32 的运行期信息表明：

- 模型文件约 `411560 B`
- arena 已用约 `4709152 B`

这意味着：

- 初始化虽然已经成功
- 但 `Invoke()` 过程中要处理的大中间激活和临时 tensor 明显更多

这会带来：

- 更差的 cache locality
- 更高的内存读写成本
- 更明显的板端浮点 kernel 性能下降

所以“内存够了”只是说明：

- 模型终于能跑起来

它不代表：

- 模型已经能实时跑

## 5.5 定量对比：粗略 MAC 规模

基于导出图结构做粗略卷积 MAC 估算，得到：

| 模型 | 粗略卷积 MAC |
| --- | ---: |
| `student_bc_resnet_tiny_v2_fp32_debug` | `≈ 180.4M` |
| 历史 `bc_resnet_v3_fp32` 参考图 | `≈ 13.5M` |
| 比值 | `≈ 13.3x` |

这个估算不是精确 profiler，但已经足够说明方向：

- 当前 student 图的理论计算规模远高于历史参考板端图

而历史参考 FP32 板端日志曾经大约是：

- `≈ 179 ms / infer`

当前 student FP32 板端是：

- `≈ 675 ms / infer`

时间没有按 `13x` 线性放大，是因为：

- kernel 类型不同
- cache / memory 访问模式不同
- depthwise 与 dense conv 的实现路径不同

但量级关系完全一致：

- 当前 student 图显著更重

## 6. 哪些因素不是主因

为了避免误判，需要明确排除几个常见方向。

### 6.1 不是 arena 不够

这条线前面已经验证过：

- arena 从 `688 KB` 拉到 `8192 KB` 后，模型初始化已经成功
- 当前问题不是 `AllocateTensors()` 失败
- 当前问题是 `Invoke()` 过慢

### 6.2 `101` 帧不是主因

从 `98` 到 `101` 帧，只增加约 `3%` 输入长度。

这会带来一些额外负担，但不可能把时延从：

- `~179 ms`

抬到：

- `~675 ms`

所以：

- `101` 帧可能值得优化
- 但它不是主因

### 6.3 阈值不是主因

当前阈值配置偏低，会影响：

- 误触发率
- 触发次数

但它不会改变单次 `Invoke()` 时延。

### 6.4 串口和日志不是主因

串口长日志会干扰调试体验，但当前 `infer_us` 是板端内部计时，不是串口统计值。

所以：

- 串口噪声会影响观测
- 但不解释 `675 ms` 本身

## 7. 为什么它会表现成“唤醒后系统反应迟缓”

这和当前 KWS worker 的工作方式直接相关。

见：

- [components/river_voice/river_voice_kws.cc](../components/river_voice/river_voice_kws.cc)

KWS 处理流程是：

- 每来一批采样，推进 mel 帧
- 满足 stride 后触发一次推理
- 推理完成后才继续处理后续队列

当前实际状态是：

- 每 `160 ms` 理论上允许触发一次推理
- 但每次推理要 `675 ms`

结果就是：

- worker 天然持续欠账
- 队列只能不断积压
- 实际的命中判定明显滞后于用户说话时刻

这会直接带来两个用户侧现象：

1. 唤醒命中明显滞后
2. “唤醒词 + 命令”连说时，ASR 往往赶不上，后续会话表现像“唤醒了但没反应”

所以前面看到的：

- 唤醒成功
- 但 ASR / TTS 不稳定
- 甚至用户体感像“没声音”

根因并不一定在云端，而是：

- 本地唤醒判定已经晚了很多

## 8. 当前 student FP32 的定位应该是什么

结合当前导出物和板端实测，当前 student FP32 更合理的定位是：

- 用于板端 / 本机对拍
- 用于验证部署正确性
- 用于对比量化偏差

而不应把它视为：

- 当前产品链路里的 live 交互运行模型

算法交付说明本身也明确写了：

- `FP32 TFLite 主要用于调试量化损失`
- `默认交付模型仍以 INT8 为准`

所以如果要继续验证 student 家族的真实可用性，下一步应优先切到：

- `student INT8`

而不是继续拿当前 FP32 debug 变体做交互体验判断。

## 9. 可以怎样优化

优化建议分三层。

## 9.1 立即可执行的策略

### 9.1.1 不再把当前 student FP32 当作 live 交互模型

建议定位为：

- 板端 / 本机对拍专用
- 量化偏差分析专用

实时交互验证应优先换到：

- `student INT8` 正式交付变体

### 9.1.2 给模型 bring-up 增加实时性准入门槛

后续模型首次上板时，不应只检查：

- 能否初始化
- parity 是否通过

还必须同时检查：

- `infer_us`
- stride 预算
- arena used
- 队列峰值
- 实际唤醒滞后

建议最少分两档验收：

- `FP32 debug` 调试档：
  - `infer_us <= 150~200 ms`
- `live 交付` 档：
  - `infer_us <= 120 ms`

如果还想贴近算法 bundle 自己声明的预算，则目标应进一步收紧到：

- `30~50 ms`

## 9.2 板端侧可以做，但收益有限

### 9.2.1 增加分阶段耗时统计

建议单独统计：

- `compute_mel_frame`
- `fill_input_tensor`
- `Invoke`
- queue backlog

这样后续新模型上板时，可以第一时间知道瓶颈到底是：

- 前端
- 输入拷贝
- 还是模型图本身

### 9.2.2 临时增大 stride 只适合作为 debug 止血

理论上，为了不欠账，当前 student FP32 需要的 stride 已经接近：

- `675 / 10ms ≈ 68` 帧

这在交互上几乎不可接受。

所以：

- 再增大 stride 只能暂时减少排队
- 不能作为最终方案

### 9.2.3 继续加大 arena 没意义

arena 只解决：

- 能不能初始化

当前已经明确能初始化，所以再加 arena 不会解决实时性问题。

## 9.3 算法侧真正有效的方向

如果目标是“student 家族要在 RTL8730E 全链路实时运行”，那优化重点必须放在图结构，而不是只做阈值或前端微调。

### 9.3.1 把早期下采样放回来

当前最值得做的改动是：

- 第一层或前两层就把时间维压下去
- 尽量避免长时间停留在 `40 x 101`

目标不是机械回到历史图，而是尽快把大 feature map 缩小到：

- `~50` 帧
- 甚至更低

### 9.3.2 回到 depthwise-separable 路线

当前 student 图大部分是 dense `CONV_2D`。

对当前板端来说，更合理的方向是：

- `DEPTHWISE_CONV_2D + 1x1 CONV`

原因很直接：

- MAC 显著下降
- 激活读写压力更小
- 更接近历史已验证可部署路径

### 9.3.3 缩小通道数

当前 student 结构配置是：

- `base_channels = 24`
- 中间层扩到 `48`

对当前板端，建议优先尝试：

- `24 -> 16`
- 最大通道 `48 -> 24/32`

这类修改通常比“仅减少 3 帧输入长度”有效得多。

### 9.3.4 缩减 block 数

当前 `block_count = 4`。

建议直接尝试：

- `4 -> 3`
- 必要时 `4 -> 2`

先验证实时性是否回到可接受区间，再看精度回退是否值得。

### 9.3.5 减少大特征图上的 `LOGISTIC + MUL`

如果算法仍然想保留门控思路，建议：

- 不要在高分辨率大 feature map 上反复做 `sigmoid + mul`
- 尽量把这类门控留到降采样之后
- 或换成板端更友好的激活形式，例如 `ReLU / ReLU6`

### 9.3.6 `101 -> 98` 可以做，但只应作为次级优化

如果后续确实已经完成结构瘦身，再考虑把输入帧数从：

- `101 -> 98`

作为小幅收尾优化是合理的。

但必须明确：

- 这条改动不会单独把 `675 ms` 拉回实时范围

## 10. 建议的后续推进顺序

如果现在要继续推进这条 student 线，建议按下面顺序：

1. 保留当前 FP32 debug 变体，继续作为 parity / 部署验证工具
2. 立即评估 `student INT8` 在当前板端的 `infer_us`
3. 如果 INT8 仍明显超预算，再要求算法侧做结构级瘦身
4. 只有在 `infer_us` 回到可接受区间后，才重新讨论阈值、误唤醒与 live 交互体验

不要反过来做：

- 先调阈值
- 先调 pre-roll
- 先讨论现场误报

在当前时延量级下，这些都不是主矛盾。

## 11. 一句话结论

当前 student FP32 慢，不是因为“板端还没调好”，也不是因为“101 帧写死了”，而是因为它本质上是一个：

- 高分辨率大特征图
- dense conv 为主
- 带大量 `LOGISTIC + MUL` 门控
- 运行在 TFLM FP32 路径上的调试图

对于当前 RTL8730E 全链路环境，这个组合已经明显越过实时上限。

如果目标是继续用 student 家族上板，最现实的路线是：

- 短期切 `INT8`
- 中期做结构瘦身
- FP32 只保留为部署正确性和量化误差对拍工具
