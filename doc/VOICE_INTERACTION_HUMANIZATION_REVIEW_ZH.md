# 语音交互人性化与智能化审查

日期：2026-04-28
分支：`agent-server-v2`
范围：

- `components/river_core/river_dialog_runtime.c`
- `components/river_cloud/river_cloud_xiaozhi_round_runtime.c`
- `components/river_cloud/river_cloud_xiaozhi_session.c`
- `components/river_cloud/river_cloud_xiaozhi_playback_runtime.c`
- `components/river_voice/river_voice_vad_probe.c`
- `components/river_core/river_session_coordinator.c`

## 1. 结论摘要

当前交互栈的主要优点是：

- 工程约束清晰，状态机拆分细。
- 日志较完整，便于板端追问题。
- 对尾态、重缓冲、transport close、late meta 这类复杂边界已有较强收口意识。

当前交互栈的主要问题是：

- 更偏向“状态正确性优先”，而不是“用户意图优先”。
- 用户在回答尾声、thinking、rebuffer 等边界时刻开口，系统倾向于丢弃这次意图，而不是保留并在窗口开放后继续处理。
- 唤醒词和打断机制缺少“强制接管”能力，导致用户没有稳定的自救入口。
- 失败恢复在系统内部做了很多动作，但对用户仍然偏静默，不够像一个会解释自身状态的助手。

一句话判断：

这套实现已经具备“能跑、能定位、能收口”的工程质量，但距离“像一个真正自然的语音助手”还差一层“意图保持、强制接管、自解释恢复”的交互逻辑。

## 2. 当前交互主链路

从当前实现看，主交互链路大致如下：

1. 本地 KWS 命中后，经 `river_dialog_wake_admission` 提交唤醒准入。
2. 云端打开 conversation window，并进入 listening / ASR streaming。
3. 本地 post-roll 或 server endpoint 协调结束一轮输入。
4. 云端进入 `thinking`，随后进入 `speaking`。
5. 播放阶段允许 follow-up reopen 或 barge-in，但受 output turn、lane、rebuffer、AEC/ref 可用性等条件限制。
6. 播放尾段结束后退出 output turn，重新回到 follow-up 或 wake-monitoring。

交互状态统一由 `river_dialog_runtime` 推导，核心状态包括：

- `wake_monitoring`
- `wake_confirmed`
- `asr_streaming`
- `thinking`
- `speaking`
- `barge_in_listening`
- `follow_up`
- `error_recovering`

这说明当前架构已经不是松散的回调拼接，而是一个显式状态推导系统。这是正确方向。

## 3. 主要问题与分析

### 3.1 用户在 output-turn guard 期间开口，意图容易被丢掉

相关位置：

- `components/river_cloud/river_cloud_xiaozhi_round_runtime.c`

关键现象：

- follow-up reopen 会被 `thinking`、`speaking`、`response_waiting_audio`、`playback_lane_engaged`、`playback_turn_active`、`playback_rebuffer_pending` 阻断。
- 被阻断时，`open_speech_frames` 会被清零。
- reopen 本身还依赖固定的 hold frames 门槛。

这意味着：

- 用户在“回答快播完时”提前开口；
- 或者在“系统看起来已经说完，但内部尾态还没收干净时”开口；
- 系统不会保留这次说话意图，而是直接丢弃。

从用户视角看，这就是：

- “我明明已经说了，系统却像没听到。”
- “必须再重复一遍。”

这不符合人性化交互。更合理的逻辑不是“没到可重开条件就清零”，而是“先记住用户已经开口，等安全窗口到来后立刻接上这次意图”。

### 3.2 wakeword 被整段 conversation window 硬阻断，缺少强制接管能力

相关位置：

- `components/river_core/river_dialog_runtime.c`
- `components/river_core/river_dialog_wake_admission.c`

当前逻辑会在以下情况下阻断 wakeword：

- `wake_admission_pending`
- `conversation_window_active`
- `cloud_local_close_pending`
- `cloud_listen_stop_pending`
- 任何非 `wake_monitoring` 态

这在工程上很保守，但不够智能。

真实用户心智是：

- “如果当前轮次卡住了，我应该能再喊一遍唤醒词把系统抢回来。”

现在的实现里，wakeword 更像普通入口，不是 override 入口。  
这会导致：

- 会话尾态卡住时，用户连“重新唤醒”都做不到；
- 只能等 timeout，或者反复试说下一句；
- 系统恢复手段完全掌握在内部状态机手里，不掌握在用户手里。

### 3.3 barge-in 在最需要打断的窗口里仍偏保守

相关位置：

- `components/river_core/river_dialog_runtime.c`
- `components/river_voice/river_voice_vad_probe.c`
- `components/river_core/river_session_coordinator.c`

当前 barge-in 由两类信号触发：

- 近端 VAD/near-end speech
- 云端 partial/final ASR 文本

但限制条件较多：

- `allows_barge_in_interrupt()` 要求 `asr_session_active && output_turn_engaged`
- no-ref 模式下 near-end speech 会退化成 `duck_only`
- partial 文本一旦变化且非空，就可能触发 TTS interrupt

这里有两个相反的问题同时存在：

1. 在 no-ref / 尾态 / 半双工窗口下，用户真正想打断时，系统可能只 duck 不 interrupt。
2. 在 partial 文本路径上，系统又可能过于激进，短 partial 抖动就触发 interrupt。

这说明当前策略还不是“稳定的人类式打断”，而是多个低层信号的硬门限叠加。

### 3.4 恢复逻辑偏系统视角，缺少用户可感知的自解释

相关位置：

- `components/river_cloud/river_cloud_xiaozhi_session.c`
- `components/river_cloud/river_cloud_internal.h`

当前 `response_audio_timeout` 恢复路径已经考虑：

- interrupt 当前播放
- close local round
- close conversation window
- local retry prompt

但本地 retry prompt 默认被关闭：

- `RIVER_CLOUD_XIAOZHI_LOCAL_RETRY_PROMPT_ENABLED == 0`

这样做的工程理由是成立的：避免本地提示音频路径阻塞 capture/VAD。  
但从交互视角看，会形成一个问题：

- 系统内部知道自己超时了、恢复了、关闭了；
- 用户只会感知到“突然没声了，然后没反应”。

也就是说，系统在做恢复，但没有把恢复意图表达给用户。

### 3.5 交互参数大量写死，缺乏上下文自适应

相关位置：

- `components/river_cloud/river_cloud_internal.h`
- `components/river_voice/river_voice_vad_probe.c`

典型常量包括：

- follow-up window：`8000ms`
- post-commit response wait：`6000ms`
- local close defer：`2000ms`
- server accept fallback：`1800ms`
- no-ref reopen guard：`480ms`
- barge-in cooldown：`1200ms`

这些常量在 bring-up 阶段是必要的，但放在长期交互里会显得机械。

举例：

- 短确认句“好，已帮你打开灯光”与长解释句不该共用同一套 follow-up 节奏。
- 网络慢时，`server_accept_fallback_ms` 应该和最近 RTT / `response.start` 延迟相关。
- no-ref 场景下 reopen guard 是否放大，应该与最近一次 playback tail 稳定性挂钩。

如果这些参数永远固定，就很难做到“像人一样顺手”。

## 4. 与“人性化/智能化”目标的偏差

如果按用户体验目标来定义，一个更自然的语音助手至少应满足：

### 4.1 用户意图优先

用户一旦开始说话，系统首先要考虑：

- 这次说话是否应该被保留？
- 是否应该延迟处理，而不是丢弃？

当前实现对边界时刻的意图保留不足。

### 4.2 用户具备强制接管权

当内部状态机卡住或判断偏保守时，用户应能通过：

- 再次唤醒
- 明确打断
- 清晰提示后的重说

来重新主导对话。

当前实现里，这种权力偏弱。

### 4.3 恢复动作应可解释

如果系统决定：

- 不接受这句话
- 中止当前回答
- 关闭当前会话窗口
- 让用户重说

就应该让用户知道，而不是只在内部日志里发生。

### 4.4 策略应按上下文变化

短答复、长答复、弱网、no-ref、半双工尾态、真打断、误触发，都应有不同策略。  
当前实现已经具备很多状态输入，但输出策略还不够分层。

## 5. 优化建议

以下建议按优先级排序。

### P0：实现“说话意图保留”，不要在 guard 期间直接丢掉

建议新增一层显式意图状态，例如：

- `speech_pending_during_output_turn`
- `interrupt_candidate_pending`
- `pending_speech_started_ms`
- `pending_speech_pre_roll_snapshot`

建议行为：

- 当用户在 `thinking`、`response_waiting_audio`、`tail drain`、`rebuffer recovering` 期间开口，不要清零。
- 先记住这次起说事件和 pre-roll。
- 一旦 output-turn guard 解除，直接用保存的 pre-roll 进入 reopen。

收益：

- 明显减少“我说了但系统没理我”的主观感受。
- 比继续修零散尾态更直接改善体验。

### P0：给 wakeword 增加 force-override 语义

建议区分两类 wakeword：

- normal wakeword：普通唤醒
- override wakeword：在特定状态下强制接管

建议允许 override 的场景：

- `follow_up`
- `thinking`
- `cloud_listen_stop_pending`
- `cloud_local_close_pending`
- playback terminal waiting
- response audio timeout recovery 窗口

建议行为：

- 收到 override wakeword 后，先 abort 当前 output/input tail
- 清掉僵持窗口
- 重新 open and listen

收益：

- 用户获得明确的“抢回控制权”手段。
- 就算内部某条尾态路径仍有 bug，用户也不必被动等待 timeout。

### P1：把 barge-in 改成三段式，而不是“duck 或 interrupt”的二元逻辑

建议引入三阶段：

1. `duck`
2. `pending_interrupt`
3. `commit_interrupt`

具体建议：

- near-end speech 首先进入 duck
- 若持续时长或稳定性达到阈值，再进入 pending
- pending 期间若 partial/final 文本稳定确认，再执行真实 interrupt
- no-ref 下不应永远停留在 duck-only，应允许在持续稳定说话时升级到 commit interrupt

收益：

- 降低误打断概率
- 也避免“明明一直在打断却永远打不断”

### P1：partial 文本触发 interrupt 需要稳定性门槛

当前逻辑对 partial 过于直接。建议增加至少一个条件：

- 同一 partial 连续出现 N 次
- 或持续超过 X ms
- 或长度超过最小字数/音节阈值

也可以引入：

- `partial_interrupt_confidence`
- `partial_interrupt_stability_count`

收益：

- 降低噪声、口吃、短词造成的误打断。

### P1：恢复时给用户一个低成本、非阻塞的自解释反馈

本地完整提示语音未必安全，但可以先上更便宜的反馈：

- 简短 earcon
- 单次提示音
- 灯效变化
- 极短本地提示词

如果后续证明本地 AudioTrack 路径稳定，再恢复更完整的 retry prompt。

收益：

- 用户能理解“系统没死，是在让我再说一遍”。

### P2：把交互参数做成 profile 或动态策略

建议把以下参数逐步从常量变成策略：

- reopen hold frames
- no-ref reopen guard
- follow-up window
- server accept fallback
- response audio wait timeout
- barge-in interrupt hit frames

可基于的上下文包括：

- 最近 3 轮 response.start 延迟
- 最近是否发生 rebuffer
- 是否 no-ref
- 当前 output role 是短 ACK 还是主回答
- 当前 session 是否连续 follow-up

收益：

- 交互从“固定动作”变成“按场景调节”。

## 6. 推荐落地顺序

建议不要同时大改全部逻辑，优先按下列顺序推进：

### Step A

实现 `output_turn_guard` 期间的说话意图保留。  
这是最直接影响“第二句没反应”的点。

### Step B

实现 wakeword force override。  
这是最直接提升可控性的点。

### Step C

重构 barge-in 为三段式。  
把 no-ref duck-only 和 partial 激进 interrupt 两端同时收敛。

### Step D

引入恢复反馈的最小可用实现。  
先上 earcon / 极短提示，而不是完整本地 TTS。

### Step E

把关键时间常量 profile 化或动态化。  
这是体验进一步打磨阶段的工作。

## 7. 建议增加的诊断指标

为了验证“是否更人性化”，建议增加这些指标：

- 用户在 output-turn guard 期间开口的次数
- 被保留并成功 reopen 的次数
- 被 guard 直接丢弃的次数
- wakeword override 成功接管次数
- no-ref 下 duck 后升级为 interrupt 的次数
- partial interrupt 触发次数 / 误触发回退次数
- response timeout 后用户是否在 3 秒内重新说话

这些指标比单纯看 state transition 更能反映真实交互质量。

## 8. 最终判断

当前实现已经完成了“复杂实时状态机的工程收口”，这是重要基础。  
下一阶段不该继续只盯着单个尾态 bug，而应开始引入更高层的交互原则：

- 保留用户意图
- 允许用户强制接管
- 系统恢复要能自解释
- 策略按上下文动态调整

如果只继续修传输尾态，系统会越来越稳定；  
但如果不补这层交互原则，用户仍会觉得它“不够像一个真正会对话的助手”。
