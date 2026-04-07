# Student INT8 实时性预估与上板建议

日期：2026-04-07

## 1. 目的

这份文档只回答一个问题：

- 当前 `student_bc_resnet_tiny_v2` 的 `INT8 TFLite` 是否值得接到 `RTL8730E` 板端继续推进
- 在还没有该模型板端实测 `infer_us` 的前提下，本地能给出什么量级的实时性预估
- 如果要上板，第一轮应该按什么方式接，才能最大限度隔离“模型问题”和“适配问题”

这里的口径与：

- [KWS_STUDENT_FP32_REALTIME_ANALYSIS_ZH.md](./KWS_STUDENT_FP32_REALTIME_ANALYSIS_ZH.md)

保持一致，但必须明确：

- 本文核心是“本地预估”
- 不是该 `INT8` 模型已经拿到板端实时结论

## 2. 结论

先说结论：

- `student INT8` 明显比当前 `student FP32 debug` 更值得上板。
- 但它是否“已经足够实时”不能直接乐观下结论，因为模型拓扑没有变，变的只是量化形式和 kernel 路径。
- 从当前本地证据看，`INT8` 大概率能把当前 `FP32` 板端 `≈675 ms` 的单次 `Invoke()` 明显压下来，但仍然很可能高于当前 `160 ms` 的 stride 预算。
- 更稳妥的预估区间是：
  - 乐观区间：`~190-220 ms`
  - 中性区间：`~220-300 ms`
  - 保守区间：`~300-400 ms`
- 也就是说：
  - 它很可能比 `FP32` 好很多
  - 但未必已经好到“可以直接替换主链 live 交互模型”
- 因此更合理的工程建议是：
  - 值得上板
  - 但应该作为并行 `INT8 debug` 变体推进
  - 继续保留现有 `FP32` 板端/本地对拍链路
  - 先做部署正确性、kernel 稳定性、`infer_us` 实测
  - 再决定是否进入主链替换讨论

## 3. 当前已知前提

### 3.1 INT8 / FP32 是同一 student 家族同一拓扑

本地检查显示，当前 bundle 中：

- `model.int8.tflite`
- `model.fp32.tflite`

在算子结构上基本一致。

本地用 `tf.lite.Interpreter` 读取结果：

- `INT8`：
  - 输入：`[1, 40, 101, 1]`
  - 输出：`[1, 1, 1, 1]`
  - dtype：`int8 -> int8`
  - 量化参数：
    - input scale=`0.0312671810`, zero_point=`-46`
    - output scale=`0.00390625`, zero_point=`-128`
- `FP32`：
  - 输入：`[1, 40, 101, 1]`
  - 输出：`[1, 1, 1, 1]`
  - dtype：`float32 -> float32`

两者算子统计分别是：

- `INT8`：`CONV_2D x14`, `LOGISTIC x11`, `MUL x10`, `ADD x4`, `AVERAGE_POOL_2D x1`
- `FP32`：`CONV_2D x14`, `LOGISTIC x11`, `MUL x10`, `ADD x4`, `AVERAGE_POOL_2D x1`

这意味着：

- 量化没有改变图结构主成本
- `INT8` 能改善的，是：
  - 张量字节数
  - kernel 执行路径
  - 内存带宽压力
- 量化不能改变的，是：
  - 高分辨率大特征图
  - dense `CONV_2D`
  - 大量 `LOGISTIC + MUL`
  - 整体 MAC 规模

### 3.2 当前 student INT8 契约没有比 FP32 更“轻”

见：

- `/root/kws-trainint/artifacts/exports/student_bc_resnet_tiny_v2/frontend_contract.json`
- `/root/kws-trainint/artifacts/exports/student_bc_resnet_tiny_v2/board_runbook.md`

当前 `INT8` 契约仍然是：

- `40 x 101`
- `25 ms / 10 ms`
- `n_fft = 400`
- `center = true`
- `natural_log`
- `per_clip_mean_std_normalize`

所以：

- 它没有回到历史板端友好的 `40 x 98`
- 也没有切回早期 `depthwise` 风格

换句话说：

- 当前 `INT8` 的提升空间主要来自“数值表示和 kernel”
- 不是来自“结构已经明显瘦身”

### 3.3 当前参考的板端 FP32 实测是 `≈675 ms`

见：

- [KWS_STUDENT_FP32_REALTIME_ANALYSIS_ZH.md](./KWS_STUDENT_FP32_REALTIME_ANALYSIS_ZH.md)

当前同一 student 家族 `FP32 debug` 板端实测已经明确：

- `infer_us ≈ 675 ms`

当前工程配置下：

- `CONFIG_RIVER_KWS_INFERENCE_STRIDE_FRAMES=16`
- `hop = 10 ms`

对应预算是：

- `160 ms`

所以，`INT8` 能否值得上板，本质上要看：

- 它相对 `675 ms` 能压下来多少

## 4. 本地证据

## 4.1 模型体积已经明显缩小

本地检查结果：

| 文件 | 大小 |
| --- | ---: |
| `model.int8.tflite` | `124392 B` |
| `model.fp32.tflite` | `411560 B` |
| 比值 | `0.302x` |

这说明：

- 权重静态体积约缩小到 `30%`

但必须注意：

- `.tflite` 文件大小不是运行期 `arena`
- 它只能说明静态体积缩了，不等于实时性一定按同样比例提升

## 4.2 同图下，张量总字节量约缩小到 `25%`

本地用 `tf.lite.Interpreter(..., experimental_preserve_all_tensors=True)` 读取 tensor 详情后，得到：

| 模型 | tensor_count | total_tensor_bytes |
| --- | ---: | ---: |
| `INT8` | `69` | `2,800,070 B` |
| `FP32` | `69` | `11,194,508 B` |
| 比值 | `1.00x` | `0.250x` |

这组数字非常重要，因为它说明：

- 图没有变
- 张量生命周期复杂度也没有本质变
- 但同形状 tensor 的字节负担大约缩到了 `1/4`

这对板端的直接意义是：

- cache locality 更好
- 内存带宽压力更小
- `im2col` / scratch / activation 访问成本应明显下降

## 4.3 最大中间张量仍然很大

两路模型最大的中间 tensor 都出现在：

- `40 x 51 x 48`
- `40 x 101 x 24`

只不过 dtype 不同：

- `INT8` 最大单 tensor 约 `97,920 B`
- `FP32` 最大单 tensor 约 `391,680 B`

这说明：

- `INT8` 减轻了内存压力
- 但高分辨率 feature map 本身并没有消失

因此：

- `INT8` 不会像“换了更小结构的模型”那样发生数量级改善

## 4.4 Host 侧相对速度比：乐观参考约 `3.54x`

在本地 x86 主机上，用 `tf.lite.Interpreter` 单线程、默认 delegate 路径做 200 次空输入基准，得到：

| 模型 | mean | p50 | p95 |
| --- | ---: | ---: | ---: |
| `INT8` | `0.906 ms` | `0.860 ms` | `1.084 ms` |
| `FP32` | `3.205 ms` | `3.022 ms` | `4.288 ms` |
| 相对加速 | `3.54x` | - | - |

这个结果只能说明：

- 在 host 优化路径下，`INT8` 的相对执行速度明显优于 `FP32`

不能直接说明：

- 板端一定也有 `3.54x`

原因很直接：

- x86 上用了 XNNPACK / SIMD 优化
- 板端是 `RTL8730E + TFLM`，kernel、cache、SIMD、内存层级都不同

## 4.5 Host 侧保守速度比：参考 kernel 约 `1.69x`

为避免只看 delegate 路径，又用 `BUILTIN_REF` 参考 resolver 做了保守基准，得到：

| 模型 | mean | p50 | p95 |
| --- | ---: | ---: | ---: |
| `INT8` | `37.973 ms` | `38.102 ms` | `39.722 ms` |
| `FP32` | `64.229 ms` | `63.593 ms` | `67.334 ms` |
| 相对加速 | `1.69x` | - | - |

这组数据更适合做“下界参考”，因为它说明：

- 即使不依赖强 delegate，`INT8` 也比 `FP32` 快
- 但改善未必到 `3x+`

所以当前比较合理的做法不是押单点，而是拿：

- `1.69x`
- 到
- `3.54x`

作为预估边界

## 4.6 量化误差总体可接受，但阈值余量不算宽

见：

- `/root/kws-trainint/artifacts/exports/student_bc_resnet_tiny_v2/parity_preview.json`
- `/root/kws-trainint/artifacts/exports/student_bc_resnet_tiny_v2/threshold_profiles.json`

当前 bundle 给出的量化一致性摘要是：

- `PyTorch vs INT8 TFLite mean_abs_diff = 0.005270`
- `PyTorch vs INT8 TFLite max_abs_diff = 0.032627`

其中一个正例样本出现：

- PyTorch：`0.532627`
- INT8 TFLite：`0.500000`

这说明：

- 量化不是完全无损
- 但目前还看不出“量化已经把模型彻底做坏”

同时当前默认档阈值是：

- `threshold_probability = 0.288222`
- `threshold_q15 = 9444`
- `threshold_output_int8 = -54`

因此更实际的理解应是：

- `INT8` 值得试板
- 但阈值 margin 并不算特别宽
- 上板后仍要重新看真实 `score` 分布和误唤醒情况

## 4.7 当前 bundle 自己也没有把它包装成“直接量产可上”

从导出物本身可以看到：

- `export gate passed = False`
- 声明预算：
  - `board_memory_budget_kb = 768`
  - `cpu_peak_budget_ms = 30.0`
- 当前 board reference：
  - `board_holdout_recall = 0.857143`
  - `board_fa_per_hour_at_target_recall = 606.617647`

同时 bundle 自己也明确写了：

- 首次 bring-up 建议先用更小模型确认链路
- 误触偏多时优先切 `recall_900`

这说明算法侧自己的定位也是：

- 这是值得试板的候选
- 但不是“已经闭眼替换主链”的状态

## 5. 本地对板端的实时性预估

## 5.1 预估方法

这里不直接编造板端时延，而是用两条边界去夹：

1. 当前 student FP32 板端实测：
   - `≈675 ms`
2. 当前 host 侧 INT8/FP32 相对加速：
   - 保守：`1.69x`
   - 乐观：`3.54x`

于是可以得到：

- 保守预估：
  - `675 / 1.69 ≈ 399 ms`
- 乐观预估：
  - `675 / 3.54 ≈ 191 ms`

所以第一结论是：

- `INT8` 预估区间大约在 `190~400 ms`

## 5.2 更实用的工程判断区间

只给 `190~400 ms` 太宽，不利于做接板决策。

结合当前图结构、内存压力下降幅度和板端场景，更实用的工程判断是：

- 乐观区间：`190~220 ms`
  - 前提：板端 int8 conv/kernel 路径确实吃到明显收益
- 中性区间：`220~300 ms`
  - 这是当前最值得作为默认预期的区间
- 保守区间：`300~400 ms`
  - 前提：kernel 收益有限，图仍主要受大 feature map 和门控链拖累

换句话说：

- 我认为它大概率不会还停在 `675 ms`
- 但我也不认为仅靠量化就能很有把握落进 `<=160 ms`

## 5.3 对当前主链实时性的含义

当前主链 stride 预算是：

- `160 ms`

所以按上面的预估：

- 若 `INT8 <= 160 ms`
  - 才能开始认真讨论“直接进入 live 主链”
- 若 `160~250 ms`
  - 说明比 `FP32` 好很多，但仍属于边缘或欠账状态
- 若 `>250 ms`
  - 说明结构问题仍是主矛盾，INT8 只是缓解，不是解决

因此当前最稳妥的预判是：

- `student INT8` 更像“值得上板继续验证的强候选”
- 而不是“本地已经足够证明它会实时”

## 6. 对板端内存的本地预估

## 6.1 从 FP32 已知 arena 反推

当前 student FP32 板端已经实测：

- `arena_used ≈ 4,709,152 B`

如果按本地观察到的总 tensor 字节比例：

- `INT8 / FP32 ≈ 0.250`

做线性近似，则：

- `4,709,152 x 0.250 ≈ 1,177,000 B`

如果按 `.tflite` 文件大小比例：

- `124,392 / 411,560 ≈ 0.302`

再做一个更保守的近似，则：

- `4,709,152 x 0.302 ≈ 1,423,000 B`

因此比较合理的本地预估范围是：

- `INT8 arena` 大概率仍在 `1.2~1.4 MB`

## 6.2 这意味着什么

这意味着：

- 它虽然会比 `FP32` 小很多
- 但大概率仍明显高于当前 bundle 声明的 `768 KB`
- 也显著高于当前主链常见的 `192 KB`

所以第一轮 INT8 试板不要过早抠极限内存，应直接按 bring-up 思路做：

- 初始 arena 建议：`1536 KB`
- 更稳妥的首轮 smoke：`2048 KB`

先确认：

- `AllocateTensors()` 稳定
- `Invoke()` 能持续运行
- 没有历史 `int8 im2col` 崩溃

然后再谈压缩 arena。

## 7. 值不值得上板

## 7.1 值得，但不是替主链的方式上

我的判断是：

- 值得上板

原因：

- 相对 `FP32`，它是当前最有现实意义的继续方向
- 当前量化误差总体可接受
- 模型静态体积与张量字节量都已显著下降
- bundle 默认也把 `INT8` 作为正式交付形态，`FP32` 更偏调试

但同时必须加上限制条件：

- 不值得直接替换主链
- 不值得跳过现有对拍机制直接做体验结论

## 7.2 当前更像“并行 bring-up 候选”，不是“直接主链候选”

更准确地说，它现在的定位应是：

- `并行 INT8 bring-up / parity / realtime probe 变体`

而不是：

- `主链默认唤醒模型`

因为当前还有三类风险没有实测清掉：

1. 实时性风险
   - 可能仍高于 `160 ms`
2. 内存风险
   - 可能仍需要 `>1 MB` arena
3. kernel 稳定性风险
   - 历史上 `int8 conv im2col` 路径出现过板端崩溃

## 8. 上板建议

## 8.1 变体策略

建议做法：

- 新建并行 `student INT8 debug` 变体
- 不替换现有主链
- 不删除现有 `student FP32 debug` 对拍变体

原因：

- `FP32` 仍然是排查“量化问题 vs 部署问题”的关键参照
- `INT8` 要解决的是“更接近可部署”的问题
- 两条链路目标不同，不能互相替代

## 8.2 第一轮上板配置建议

第一轮推荐：

- arena：`1536 KB` 起步，必要时 `2048 KB`
- 保持当前 `40x101` 前端契约，严禁偷回 `40x98`
- resolver 至少包含：
  - `ADD`
  - `AVERAGE_POOL_2D`
  - `CONV_2D`
  - `LOGISTIC`
  - `MUL`
- 输入 / 输出按 bundle 使用：
  - `int8`
  - input scale=`0.0312671810`
  - input zero_point=`-46`
  - output scale=`0.00390625`
  - output zero_point=`-128`

阈值第一轮建议：

- 默认：
  - `default_target_recall`
  - `threshold_output_int8 = -54`
  - `threshold_q15 = 9444`
- 保守 fallback：
  - `recall_900`
  - `threshold_output_int8 = -50`
  - `threshold_q15 = 9981`

## 8.3 第一轮验证顺序

顺序建议：

1. 先做本地 / 板端张量对拍
2. 再做 `local_only` 或最小会话链路 smoke
3. 再测 `infer_us`
4. 最后才看真实误唤醒 / 体验

第一轮板测必须优先回答这几个问题：

- `AllocateTensors()` 是否稳定
- 是否出现 `int8 conv` 相关异常或 `Data abort`
- `infer_us` 实测到底在什么区间
- queue 是否持续欠账
- `wifi_connected` 后 heap 余量是否还能接受

## 8.4 决策门槛

第一轮 INT8 试板后，建议按下面的门槛判断：

| 实测结果 | 结论 | 下一步 |
| --- | --- | --- |
| `infer_us <= 160 ms` 且稳定 | 有机会继续推进主链试跑 | 再看误唤醒、ASR/TTS 交互 |
| `160 < infer_us <= 250 ms` | 适合继续 debug，不适合直接主链替换 | 先保留并行变体，必要时调 stride / queue 只做验证 |
| `250 < infer_us <= 400 ms` | 量化有帮助，但结构问题仍明显 | 不要先调阈值，先考虑更小模型或结构瘦身 |
| `infer_us > 400 ms` | INT8 收益不足 | 基本不值得继续在该结构上耗板测时间 |
| 出现 `int8 im2col` / conv 崩溃 | 先解决 runtime 稳定性 | 暂停讨论效果和阈值 |

## 9. 对算法同事的具体建议

如果算法同事要继续支持这条 `INT8` 线，我建议反馈得非常具体：

- 目前 `INT8` 值得上板，但它解决的是“比 FP32 更接近可部署”，不是“结构已经足够轻”
- 当前拓扑没有变，`dense conv + 大特征图 + 多组 LOGISTIC/MUL` 仍是主成本
- 如果板端实测落在 `200~300 ms`，不要把主要精力先放在阈值上
- 真正有效的继续优化方向仍然是：
  - 更早下采样
  - 降通道
  - 减 block
  - 减少大特征图上的门控
  - 必要时做更小的 INT8 student

## 10. 一句话结论

当前 `student INT8` 明显比 `student FP32 debug` 更值得上板，但本地证据更支持它是：

- 一个应当保留现有对拍链路、按并行 debug 变体推进的强候选

而不是：

- 一个已经可以直接替主链的实时模型
