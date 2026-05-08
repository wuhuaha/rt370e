# 端侧语音运行时流畅性与实时性优化设计

日期：2026-04-28  
分支：`agent-server-v2`  
适用范围：

- `components/river_voice`
- `components/river_core`
- `components/river_cloud`
- `include/river`

## 1. 文档目标

本文档聚焦端侧项目最核心的系统目标：

- 语音交互不断音
- 用户说话被尽快接住
- 本地采集/VAD/KWS 不被云端控制或播放尾态拖慢
- 网络波动、播放恢复、协议补偿都不能反向伤害实时音频路径

当前项目已经在“状态正确性”和“复杂尾态收口”上做了大量工程工作，但从端侧实时系统视角看，还存在若干结构性问题。本文档的目标不是继续做单点 bug 归因，而是提出一套面向端侧实时性的架构收敛方案。

## 2. 当前问题总结

### 2.1 当前实现的优点

当前代码具备以下明显优点：

- 交互状态、播放状态、会话窗口状态已经显式化，不再是松散回调。
- XiaoZhi uplink/downlink/playback terminal/turn semantics 都有可观测真相源。
- 对播放尾态、late meta、transport close、rebuffer 的异常收口已经比早期实现强很多。
- 诊断日志非常完整，利于 bring-up 和板端定位。

这些都是后续做实时优化的基础。

### 2.2 当前实现的核心缺陷

从端侧实时系统角度，当前最大问题不是“功能缺”，而是“热路径被慢路径污染”。主要体现在：

1. 采集热路径仍会直接触发同步控制请求。
2. 同一控制队列同时承载高优先级控制和平滑性无关的 ACK 遥测。
3. KWS/VAD 热路径还会读带锁的 runtime gate。
4. 超时、follow-up、server-accept 等策略参数大量固定，偏保守。
5. 诊断日志默认过重，容易扰动实际调度与音频节奏。

一句话概括：

当前实现更像“一个具备实时链路的复杂状态机”，而不是“一个把实时链路隔离为第一优先级的端侧系统”。

## 3. 设计原则

后续优化必须遵守以下原则。

### 3.1 热路径绝不等待慢路径

以下路径定义为热路径：

- mic capture
- preproc
- VAD
- KWS
- realtime uplink enqueue

这些路径上禁止：

- 无限等待
- 依赖 websocket pump 完成的同步调用
- 创建/销毁内核对象
- 复杂格式化日志
- 读取会争锁的全局状态

### 3.2 控制正确性不能凌驾于采集连续性之上

如果系统必须在以下二者中选一个：

- 及时把一条控制请求完整发到云端
- 保证本地采集不断、VAD/KWS 不中断

必须优先后者。

控制请求可以延迟、重试、合并，音频热路径不能回滚。

### 3.3 低价值遥测不得阻塞高价值交互控制

下列消息不应与关键控制同优先级竞争：

- playback `mark`
- playback `started/completed/cleared` 遥测
- 细粒度状态统计

真正高优先级的是：

- `open_and_listen`
- `listen_stop`
- `abort`
- `close_session`

### 3.4 默认固件优先保实时，再保可观察性

默认运行配置应优先：

- 时延
- 连续性
- 调度稳定性

而不是默认打印大量诊断日志。  
深度调试时再主动切到高观测模式。

## 4. 当前代码中的结构性问题

### 4.1 采集热路径存在同步控制等待

当前 `inactive_stream_capture_policy()` 会在采集路径里尝试 reopen follow-up round。  
后续调用链会进入同步控制请求路径：

- `river_cloud_xiaozhi_maybe_start_followup_round(...)`
- `river_cloud_xiaozhi_start_followup_round(...)`
- `river_cloud_xiaozhi_open_session_and_listen(...)`
- `river_cloud_xiaozhi_request_open_and_listen(...)`
- `river_cloud_xiaozhi_control_request(...)`

问题在于：

- `control_request()` 会为每次请求创建 completion 信号量
- 入队后阻塞等待 I/O owner 执行完成
- 等待时间为 `RIVER_CLOUD_XIAOZHI_CONTROL_WAIT_MS`
- 当前该值是无限等待

这意味着：

- 只要 websocket transport、I/O task、控制队列、网络发送任一处抖动
- 采集侧就可能被拖住

这与端侧实时系统原则冲突。

### 4.2 单一控制队列混装 urgent control 与 ACK 遥测

当前 XiaoZhi 控制队列同时承载：

- `OPEN_AND_LISTEN`
- `LISTEN_STOP`
- `ABORT`
- `CLOSE_SESSION`
- playback `STARTED`
- playback `MARK`
- playback `CLEARED`
- playback `COMPLETED`

这在逻辑上是方便的，但在实时性上有明显问题：

- `MARK` ACK 天生高频
- `OPEN_AND_LISTEN` 是直接影响用户响应时间的关键控制
- 两者竞争同一深度为 8 的队列，会造成优先级反转

结果就是：

- 低价值遥测可能推迟高价值会话控制
- 用户会感觉“下一句没立刻接住”

### 4.3 KWS/wakeword gate 仍依赖带锁 runtime 读取

当前 `river_dialog_runtime_get_voice_policy_view()` 已针对 capture 热路径做了非阻塞降级处理：

- 获取不到锁就返回 `RIVER_ERR_BUSY`
- 调用侧走保守策略，不阻塞热路径

但 wakeword gate 还不是这样：

- `river_voice_kws_detection_allowed()` 直接调用 `river_dialog_runtime_allows_wakeword_detection()`
- 后者内部获取 runtime mutex

这意味着 runtime reconciliation 一忙，KWS 热路径就会参与锁竞争。  
这类设计在 PC/服务器端不是大问题，在端侧实时系统里是错误方向。

### 4.4 固定时延参数过硬，损害短指令体验

当前项目有若干固定常量：

- `RIVER_CLOUD_STREAM_MIN_ACTIVE_MS = 1200ms`
- `RIVER_CLOUD_XIAOZHI_WAKE_WINDOW_FOLLOWUP_MS = 8000ms`
- `RIVER_CLOUD_XIAOZHI_POST_COMMIT_RESPONSE_WAIT_MS = 6000ms`
- `RIVER_CLOUD_XIAOZHI_SERVER_ACCEPT_FALLBACK_MS = 1800ms`
- `RIVER_CLOUD_XIAOZHI_NOREF_REOPEN_GUARD_MS = 480ms`

这些值有工程合理性，但对端侧实时体验偏保守。

典型问题：

- 用户一句很短的“开灯”，本地 post-roll 明明已经明确结束，但仍可能被 `MIN_ACTIVE_MS` 人为拖慢。
- 网络稳定时仍使用与弱网相同的 fallback 等待。
- 短 ACK 与长回答共用同一节奏策略。

这会让系统显得“稳，但慢半拍”。

### 4.5 诊断日志过重

当前默认路径上日志很密，包括：

- `playback ack mark sent`
- `vad state=speech/silence`
- `dialog_runtime interaction_transition`
- `xiaozhi session.update`
- `xiaozhi asr round finish`
- runtime stats snapshot

日志本身有价值，但端侧系统要警惕两个问题：

1. 串口输出本身耗时。
2. 大量 `snprintf` / `RIVER_LOGI` 会扰动任务调度和 cache 行为。

如果默认固件长期带着这类高密度日志运行，端侧时序就会被诊断系统污染。

### 4.6 barge-in 策略两头失衡

当前 barge-in 主要存在两个方向的问题：

- `no-ref` 下过于保守：长期停留在 duck-only
- partial 文本触发过于激进：一旦文本变化就可能中断

从端侧实时体验看，这会出现两种坏体验：

- “我在说话，但它就是不断”
- “我刚发出一点声音/partial，它就把 TTS 切了”

这不是实时链路带宽问题，而是控制策略没有分层。

## 5. 目标架构

目标架构的核心思想是：

把“音频热路径”与“云端控制/播放遥测/复杂状态收口”彻底隔离。

### 5.1 分层

建议把运行时明确分成四层：

#### Layer A：硬实时音频层

职责：

- capture
- preproc
- VAD
- KWS
- PCM frame accumulator
- uplink ring enqueue

要求：

- 无阻塞
- 不创建动态对象
- 只访问 lock-free 或只读快照
- 日志极少

#### Layer B：实时调度层

职责：

- uplink drain
- downlink drain
- playback write
- minimal scheduling / pacing

要求：

- 可短时 backoff
- 但不反向阻塞 Layer A

#### Layer C：控制编排层

职责：

- session open/close
- listen start/stop
- abort
- turn semantics refresh
- follow-up reopen decision

要求：

- 可以排队
- 可以重试
- 不能侵入 Layer A

#### Layer D：诊断与策略层

职责：

- 统计
- 日志
- 调参
- 复杂恢复解释

要求：

- 默认轻量
- 调试时增强

## 6. 关键设计改造

### 6.1 把控制请求从同步 RPC 改成异步 intent

#### 当前问题

当前 reopen / open_and_listen 是“采集路径发起 + 等待完成”的同步模型。

#### 目标设计

改成“采集路径只写 intent，I/O owner 异步消费执行”的模型。

建议新增结构：

- `river_cloud_xiaozhi_control_intent_t`
- `river_cloud_xiaozhi_intent_queue`

intent 只表达：

- 我要 open_and_listen
- 我要 listen_stop
- 我要 abort
- 我要 close_session

采集路径行为：

- 只 enqueue intent
- 若 intent 已存在，则合并/覆盖，不重复创建
- 立即返回，不等待 transport 成功

I/O owner 行为：

- 在自身 loop 中消费 intent
- 执行 transport
- 再更新状态真相与 runtime sync

#### 直接收益

- capture/VAD 热路径不再依赖 I/O task 调度完成
- 网络卡顿不再拖住本地采集
- follow-up reopen 不再把本地音频链路绑定到 websocket 时序

### 6.2 控制面分级：urgent queue 与 telemetry queue 分离

建议拆成两个队列：

#### urgent control queue

只允许：

- `OPEN_AND_LISTEN`
- `LISTEN_STOP`
- `ABORT`
- `CLOSE_SESSION`

特点：

- 深度小
- 允许 coalesce
- 优先级最高

#### telemetry queue

只允许：

- `PLAYBACK_STARTED`
- `PLAYBACK_MARK`
- `PLAYBACK_CLEARED`
- `PLAYBACK_COMPLETED`

特点：

- 可以丢弃、合并、降采样
- 不得反压 urgent control

#### 进一步建议

- `PLAYBACK_MARK` 改为“仅保留最新 mark”模型
- 不要求每个 80ms 样本都进入 transport
- 若 telemetry queue 拥堵，只保留 terminal close 所需的关键事实

### 6.3 建立 lock-free runtime snapshot

目标：

- KWS
- VAD
- AEC gate
- duplex ready eval

这些热路径都不应读取带锁 runtime。

#### 方案

由 `river_dialog_runtime` 维护一个：

- 单写多读 snapshot
- 只包含热路径真正需要的 gate 字段

例如：

- `wakeword_detection_allowed`
- `wakeword_override_allowed`
- `barge_in_interrupt_allowed`
- `output_turn_engaged`
- `playback_owner_kind`
- `playback_recovering`
- `tts_stop_pending`

更新方式：

- runtime publish 时一次性写入 snapshot

读取方式：

- 热路径直接 copy snapshot
- 不再获取 runtime mutex

### 6.4 重构 follow-up reopen：先保留意图，再异步重开

当前 reopen 更像“当下是否满足条件，满足就立刻开，否则放弃”。

目标应改为：

- 用户一旦开始说话，先保留起说意图
- guard 解除后再异步发起 reopen

建议新增：

- `pending_followup_reopen`
- `pending_followup_started_ms`
- `pending_followup_pre_roll_frames`
- `pending_followup_reason`

行为：

- 热路径探测到用户说话，但当前 output turn 仍未释放时，先记住
- output turn guard 一解除，由 I/O/control 层发起 reopen intent
- 这样用户不必重复说一遍

这项改动兼顾：

- 交互体验
- 实时路径隔离

### 6.5 动态化时延参数

需要从固定常量升级为策略值的参数包括：

- `stream_min_active_ms`
- `server_accept_fallback_ms`
- `post_commit_response_wait_ms`
- `followup_window_ms`
- `noref_reopen_guard_ms`
- `barge_in_interrupt_hit_frames`

建议先从 profile 化开始，而不是一步到位做复杂自适应。

#### 初期 profile

- `short_command_profile`
- `normal_dialogue_profile`
- `weak_network_profile`
- `noref_half_duplex_profile`

#### 后续可引入动态输入

- 最近 3 轮 `response.start` 延迟
- 最近 3 轮是否发生 rebuffer
- 当前 output role 是否短 ACK
- 当前 transport busy/fail 统计

### 6.6 重做日志体系：默认轻量，调试增强

建议将日志分三级：

#### Level 0：release default

保留：

- 严重错误
- 关键状态切换
- 一轮会话摘要

关闭或降频：

- 高频 playback mark
- 每次 VAD state 切换
- 高频 prefetch/phase 细节
- runtime stats snapshot

#### Level 1：field diag

用于现场问题复现：

- 开启关键状态流
- 开启少量时延指标

#### Level 2：bring-up deep diag

用于协议或边界问题深挖：

- 全部详细日志
- 高频统计

原则：

默认固件必须跑在 Level 0 或接近 Level 0。

### 6.7 barge-in 改为三段式

建议状态：

- `duck_only`
- `interrupt_candidate`
- `interrupt_committed`

规则建议：

- near-end speech 先触发 duck
- 若持续时长或稳定性满足门槛，再进入 candidate
- 若 partial/final 文本稳定确认，或者持续近端语音超阈值，再 commit interrupt
- no-ref 下不再永久 suppress，只是提高 commit 条件

这样能避免当前两端失衡：

- 不会一听到一点近端就激进 interrupt
- 也不会在 no-ref 下永远只能降音量

## 7. 调度与优先级建议

### 7.1 优先级原则

优先级应体现依赖关系：

- 采集/VAD/KWS > uplink/downlink 调度 > playback 恢复/遥测 > 诊断

如果上层逻辑仍依赖下层完成，则不能设成同优先级再配合阻塞等待。

### 7.2 当前建议

在完成“热路径去同步等待”之前，不建议仅靠调优 task priority 试图解决问题。  
优先级调整只能缓解，不能解决结构性等待问题。

正确顺序是：

1. 先把同步等待挪出热路径
2. 再微调任务优先级和时间片

## 8. 建议新增指标

为了判断实时优化是否有效，建议增加以下指标：

### 音频热路径指标

- capture 连续无阻塞运行时间
- VAD loop max iteration jitter
- KWS gate 读取失败/降级次数
- 采集路径内 intent enqueue 次数

### 控制面指标

- urgent control queue 深度/峰值
- telemetry queue 深度/峰值
- urgent control 平均等待时长
- `open_and_listen` 从 intent 到真正 send 的时延

### uplink 指标

- ring ready 帧数分布
- stale trim 次数
- send busy backoff 分布
- 真实用户音频被丢弃时长

### 交互指标

- 用户起说到 reopen intent 生成时延
- 用户起说到 first uplink send 时延
- barge-in duck 到 interrupt commit 时延
- 超时恢复后用户 3 秒内再次开口的成功率

## 9. 落地阶段规划

### Phase 1：实时安全化

目标：

- 热路径零同步等待
- urgent/telemetry 分队列
- wakeword/KWS gate 快照化

产出：

- capture/VAD 路径不再调用同步 control request
- `river_dialog_runtime` 提供 lock-free gate snapshot

### Phase 2：交互实时性提升

目标：

- follow-up reopen 改为意图保留 + 异步重开
- barge-in 改成三段式
- 缩短短命令场景下的收口延迟

产出：

- 第二句更容易接上
- 打断更稳定

### Phase 3：参数策略化

目标：

- 固定常量变 profile
- 再逐步变动态策略

### Phase 4：诊断体系分级

目标：

- 默认固件轻量日志
- 调试固件增强可观测性

## 10. 风险与兼容性

### 10.1 风险

- 控制请求异步化后，旧代码里依赖“请求返回即成功”的假设会失效。
- 状态机中的 accepted/listening/window 变更时序可能需要重新校准。
- playback ACK 遥测降采样后，服务端若隐式依赖高频 mark，需要先确认协议容忍度。

### 10.2 兼容性原则

- 协议不变，先改端侧编排方式。
- 优先保持外部行为一致，再内部去同步化。
- 先确保会话控制可靠，再对 ACK 遥测做降采样。

## 11. 最终建议

对端侧项目，最重要的不是“所有状态都尽可能即时收口”，而是：

- 本地采集绝不被拖住
- 用户一开口就被尽快接住
- 网络/云端/播放异常不能反向污染热路径

因此，当前项目后续优化的主线不应继续围绕“单个尾态 bug”展开，而应切换到以下主线：

1. 热路径去同步等待
2. 控制面分级
3. runtime gate 快照化
4. 交互意图保留
5. 参数策略化
6. 默认日志轻量化

如果这六项落实，端侧语音交互的流畅性、实时性和稳定性会一起提升；  
如果只继续堆叠状态补丁，系统会越来越复杂，但未必越来越“快”和“顺”。
