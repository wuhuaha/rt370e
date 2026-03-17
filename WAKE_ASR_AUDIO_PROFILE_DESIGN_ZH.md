# 唤醒前后双 Profile 声学设计文档

## 1. 文档目的

本文档用于明确设备端在“唤醒前”和“唤醒后”两个阶段的声学前端设计原则、模块划分、配置差异与后续实现方向，目标是同时兼顾：

- 交互自然
- 唤醒准确率与召回率
- 现代大模型 ASR 的识别效果
- 设备端资源可控

本文档面向当前项目的双麦中控屏场景，重点围绕以下约束展开：

- 硬件阵列：`AMIC1 + AMIC3`
- 麦间距：`50mm`
- 使用场景：智能家居中控屏，用户以正前方近场、中场、远场口语交互为主
- 当前主链路：`capture -> fixed_dsb -> silero_vad -> streaming asr`

## 2. 设计结论

结论一：唤醒前和唤醒后必须使用不同的声学策略，不能共用一套完全相同的前端配置。

结论二：`VAD` 不应单独承担“是否唤醒设备”的最终决策。`VAD` 适合做语音存在检测、节能门控和切段，不适合单独判断“是不是在对设备说话”。

结论三：在当前双麦中控屏场景下，`Fixed Delay-and-Sum Beamforming (DSB)` 是非常适合作为唤醒后 ASR 主前端的方案。它低失真、低算力、稳定、对 ASR 友好。

结论四：`AEC` 不应该常开，应该只在设备正在播放音频、且存在真实有效的播放参考信号时动态启用。

## 3. 为什么要区分“唤醒前”和“唤醒后”

### 3.1 唤醒前的目标

唤醒前的核心目标不是“把音频识别得最漂亮”，而是：

- 不漏掉用户说话
- 尽可能早地发现交互意图
- 保持低误唤醒
- 控制设备端常驻算力与内存

因此唤醒前链路更强调：

- 高召回
- 低延迟
- 低复杂度
- 少做破坏性处理

### 3.2 唤醒后的目标

唤醒后要解决的问题不同，核心目标变成：

- 提升 ASR 内容准确率
- 保护语义完整性
- 允许自然停顿、口吃、修正说法
- 支持连续对话和打断

因此唤醒后链路更强调：

- 定向增强
- 低失真前处理
- 宽容型 endpointing
- 结合播放状态动态开启 AEC

## 4. 建议的总体架构

建议将设备端语音系统明确拆成两条链路：

### 4.1 Always-on 唤醒链

用途：

- 常驻监听
- 低成本判断“是否有语音”
- 进一步判断“是否在唤醒设备”

建议结构：

`capture -> 轻量预处理 -> VAD -> KWS(唤醒词) -> wake decision`

说明：

- `VAD` 负责检测“有无语音”
- `KWS` 负责判断“是不是在叫设备”
- 不建议仅靠 VAD 触发整机唤醒

### 4.2 唤醒后 ASR 链

用途：

- 进行高质量流式识别
- 保证语义完整和自然交互

建议结构：

`capture -> fixed_dsb -> optional_aec -> optional_light_ns -> asr_vad/endpointing -> streaming asr`

说明：

- `Fixed DSB` 作为默认前端增强
- `AEC` 仅在播放中且参考有效时动态开启
- `NS` 仅做轻量去稳态噪声，不做激进语音美化

## 5. 唤醒前 Profile 设计建议

### 5.1 唤醒前的处理原则

唤醒前应坚持：

- 尽量保留原始语音特征
- 尽量减少对弱起音、辅音的损伤
- 尽量不依赖复杂自适应算法

### 5.2 唤醒前推荐模块

- `HPF/DC remove`：开启
- `VAD`：开启
- `KWS`：开启
- `DSB`：可选
- `AEC`：默认关闭
- `NS`：关闭或极弱
- `AGC`：关闭或慢速弱 AGC

### 5.3 唤醒前是否启用 DSB

对于当前中控屏正前方说话场景，唤醒前可以考虑两种模式：

1. `raw dual-mic / single-mic` 保守模式
2. `fixed_dsb` 保守增强模式

建议：

- 先做 A/B 测试
- 若 `fixed_dsb` 不降低唤醒召回，则可在唤醒前保留
- 若远场弱唤醒词被 DSB 破坏，则唤醒前应退回更轻的路径

## 6. 唤醒后 ASR Profile 设计建议

### 6.1 唤醒后处理目标

唤醒后链路要优先保证：

- 用户说的话尽量被完整送入 ASR
- 不要过早切断
- 不要过度失真
- 尽量提升正前方说话的信噪比

### 6.2 唤醒后推荐模块

- `Fixed DSB`：默认开启
- `AEC`：动态开启
- `NS`：轻量可配
- `AGC`：慎用
- `Streaming ASR`：持续送流

### 6.3 为什么默认推荐 Fixed DSB

对于当前项目的双麦阵列和中控屏应用场景，`Fixed DSB` 的主要优势是：

- 结构简单，稳定可控
- 算力消耗低
- 对 MCU 友好
- 不引入复杂自适应失稳问题
- 对现代 ASR 通常比激进降噪更友好
- 非常适合正前方主交互场景

当前基础形式：

- 阵列：`AMIC1 + AMIC3`
- 间距：`50mm`
- 默认延迟：`0 sample`
- 输出：单声道

后续可围绕 `delay_samples=0/1/2` 做小范围扫描调优。

## 7. AEC 的定位与启用条件

### 7.1 AEC 不能常开

AEC 只有在以下条件同时满足时才有价值：

- 设备正在播音
- 存在真实播放参考信号
- 参考信号稳定、时序可信

如果不满足上述条件，强行开启 AEC 会带来风险：

- 抑制用户语音
- 降低 VAD 触发概率
- 降低 ASR 可懂度
- 产生不可控失真

### 7.2 AEC 的推荐启用条件

建议至少满足：

- `playback_active == true`
- `reference_path_exists == true`
- `reference_signal_level > threshold`
- `reference_active_duration >= stable_window`

### 7.3 AEC 的推荐状态

建议把 AEC 运行态区分为：

- `disabled`
- `armed`
- `active`

说明：

- `disabled`：没有参考路径或场景不需要
- `armed`：已有参考路径，但当前播放能量不足
- `active`：参考有效且稳定，AEC 正在工作

## 8. NS 的使用建议

### 8.1 唤醒前

不建议使用激进 NS。

原因：

- 容易抹掉弱唤醒词特征
- 会损伤辅音、气音和弱起音
- 会降低远场召回

### 8.2 唤醒后

可考虑使用轻量 NS，但必须遵循：

- 以提升 ASR 为目的
- 只压稳态噪声
- 不追求“听起来更干净”
- 不允许明显语音失真

现代大模型 ASR 通常比端侧激进降噪更能处理复杂噪声，因此端侧 NS 应该保守。

## 9. VAD / Endpointing 的设计建议

### 9.1 VAD 的角色

建议明确 VAD 的职责：

- 语音存在检测
- 切段辅助
- 节能门控
- pre-roll / post-roll 控制

不建议让 VAD 单独承担：

- 是否唤醒设备
- 是否结束完整语义

### 9.2 ASR 场景下的 VAD 原则

ASR 场景下建议坚持：

- 极度宽容
- 包容口吃
- 保护语义完整性

推荐原则：

- `Min_Speech = 100ms`
- `Max_Silence = 600ms ~ 800ms`
- `Max_Duration = 10s ~ 15s`

### 9.3 为什么要宽容

真实中控屏场景里，用户常出现：

- 短指令：如“开灯”
- 停顿修正：如“把空调……调到 26 度”
- 连续补充：如“打开灯……客厅的”

如果本地 endpointing 太激进，交互会显得：

- 笨
- 打断用户
- 语义不完整
- ASR 虽然出字快，但整体效果差

## 10. 让交互更自然的关键机制

### 10.1 pre-roll

唤醒后必须保留 pre-roll，避免吃掉用户第一字。

建议范围：

- `300ms ~ 500ms`

### 10.2 follow-up window

建议唤醒一次后保持一个连续对话窗口，而不是每句话都重新唤醒。

建议范围：

- `5s ~ 8s`

收益：

- 更自然
- 用户可连续多轮表达
- 降低交互摩擦

### 10.3 barge-in

如果设备会播报 TTS 或提示音，建议支持打断：

- 播放中用户说“停一下”
- 立即切回上行识别

这时才真正需要动态 AEC。

## 11. 推荐的状态机

建议至少定义以下状态：

1. `Idle`
2. `WakeListening`
3. `WakeConfirmed`
4. `DialogActive`
5. `Playback`
6. `FollowUpWindow`

推荐逻辑：

- `Idle / WakeListening`
  - 轻量 VAD + KWS
- `WakeConfirmed`
  - 打开高质量 ASR 链，带 pre-roll
- `DialogActive`
  - DSB 开，AEC 按需动态开
- `Playback`
  - 如果支持打断，维持 playback ref
- `FollowUpWindow`
  - 若用户继续说话，无需重新唤醒

## 12. 当前项目建议的落地路线

基于当前项目现状，建议分阶段推进：

### 阶段一：稳住当前 ASR 主链

目标：

- `capture -> fixed_dsb -> silero_vad -> streaming asr`

重点：

- 固化资源基线
- 调优 `Silero VAD`
- 调优 `DSB delay_samples`

### 阶段二：增加独立的唤醒前 Profile

目标：

- 引入独立 `wake_profile`
- 把“唤醒前”和“ASR后”配置彻底拆开

重点：

- 保守前处理
- 引入 `KWS`
- 做召回率 / 误唤醒对比

### 阶段三：引入动态 AEC

目标：

- `AEC` 不进主链常开
- 只在 `Playback + valid ref` 条件下激活

重点：

- 参考路径时序校准
- reference-active 滞回判定
- AEC 独立实验 profile

## 13. 当前建议的默认配置

### 13.1 唤醒前默认建议

- `DSB`：可选，建议 A/B
- `AEC`：关
- `NS`：关或极弱
- `AGC`：关或慢速弱 AGC
- `VAD`：高召回配置
- `KWS`：开

### 13.2 唤醒后默认建议

- `DSB`：开
- `AEC`：仅 playback-active + ref-valid 时开
- `NS`：轻量可配
- `AGC`：慎用
- `VAD`：宽容型 endpointing
- `ASR`：流式持续送流

## 14. 风险与注意事项

### 14.1 唤醒前处理过重的风险

- 误伤唤醒词
- 远场召回下降
- 辅音和弱起音损伤

### 14.2 唤醒后处理过重的风险

- 语音失真
- 语义边界被破坏
- 大模型 ASR 反而更差

### 14.3 AEC 常开的风险

- 无真实参考时抑制用户语音
- 远场更容易不触发
- ASR 空文本或错误文本增多

## 15. 本文档对应的工程建议

后续实现时建议显式维护：

- `wake_profile`
- `asr_profile`
- `playback_state`
- `reference_state`

并把以下信息写入日志：

- 当前 profile
- 当前是否播放
- playback ref 是否存在
- ref 能量是否超过阈值
- 当前 AEC 状态
- 当前 DSB 参数
- 当前 VAD 参数

## 16. 参考

### 16.1 固定延迟求和波束成形

- [FIXED_DSB_BEAMFORMING_ZH.md](/root/ameba-river/FIXED_DSB_BEAMFORMING_ZH.md)

### 16.2 项目整体演进

- [ARCHITECTURE_OPTIMIZATION_ZH.md](/root/ameba-river/ARCHITECTURE_OPTIMIZATION_ZH.md)

### 16.3 当前项目知识沉淀

- [.codex/knowledge.md](/root/ameba-river/.codex/knowledge.md)

## 17. 最终建议

对于当前项目，建议明确采用以下长期路线：

- 唤醒前：轻量 `VAD + KWS`
- 唤醒后：`Fixed DSB + 宽容型 VAD + Streaming ASR`
- 播放中：`AEC` 只在真实播放参考有效时动态启用

这条路线最有机会同时兼顾：

- 交互自然度
- ASR 准确率
- 唤醒召回
- 误触发控制
- 嵌入式资源约束
