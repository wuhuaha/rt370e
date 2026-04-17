# Voice Runtime Architecture Review

Date: 2026-04-17
Branch: `agent-server-v2`

## 1. 背景

当前板端主链已经具备：

- `wake -> VAD/KWS -> realtime ASR`
- `LLM 流式文本 -> TTS 流式音频 -> 本地播放`
- `follow-up`
- `barge-in`
- 部分 preview / endpoint / playback ACK 协作能力

但最新板端日志同时暴露了两个高频问题：

- 响应慢：
  - `input.speech.start -> first input.preview` 明显慢于实时
  - `asr round duration_ms` 明显大于 `packets * 20ms`
- 播放卡顿：
  - 多次 `underrun`
  - 多次 `playback write failed`
  - 多次 `rebuffer requested`

这说明当前问题已经不是“功能缺失”，而是“运行时所有权和媒体调度模型不够稳定”。

## 2. 当前架构诊断

### 2.1 `river_core` 还不是唯一编排者

当前真正复杂的运行时状态主要散落在：

- [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)

而 [components/river_core/river_session_coordinator.c](/root/ameba-river/components/river_core/river_session_coordinator.c)
主要维护一个粗粒度 `phase`，更多是在根据回调和局部状态反推运行态，不是唯一真相源。

结果：

- 本地交互 phase
- 云端 `input_state/output_state`
- playback 状态
- duplex fallback
- turn accept 语义

同时存在多套真相，边界不清。

### 2.2 `river_voice` 与 `river_cloud` 之前是双向耦合

在本轮修改前，`river_voice` 里的多个模块直接调用 cloud 接口：

- `vad_probe` 直接开关云端 ASR / push frame / interrupt TTS
- `segment_sink` 直接投递到 cloud batch bridge
- `kws` 直接读取 cloud conversation window

同时 `river_cloud` 又直接读取 voice runtime policy。

结果：

- `river_voice` 不能独立测试
- 本地前端、会话编排、provider 选择无法解耦
- 未来做在线 / 离线融合时会继续放大复杂度

### 2.3 媒体平面与控制平面混在同一单体文件

当前 XiaoZhi 相关的这些职责基本都在同一个 adapter 中：

- 协议协商
- turn 语义缓存
- uplink pacing
- downlink buffering
- playback ACK
- local close / soft close
- duplex fallback
- 日志与诊断

结果：

- 一次局部补丁很容易改变别的运行时路径
- 很难证明单轮 turn 中每一帧音频的命运
- 很难区分“服务端慢”“设备上行慢”“设备播放恢复抖动”

### 2.4 当前状态机偏视图化，不足以做强约束

目前很多状态同步仍是：

- reason string
- 临时日志
- 回调后反推 phase

这对排障有帮助，但对运行时正确性约束很弱。

## 3. 问题根因

### 3.1 上行慢的根因

当前 uplink worker 的历史实现问题：

- 每轮最多只发一帧
- 先从 ring 取帧，再尝试发送
- `BUSY/失败` 时没有完整 in-flight 保帧语义
- `listen_stop` 完成判定没有把待重试帧算入 ready queue

这会导致：

- 发音频慢于实时
- 轻度调度抖动就放大成 preview / accept 延迟
- 某些发送失败变成隐式掉帧

### 3.2 播放卡顿的根因

当前下行播放虽然已有 rebuffer 机制，但模型仍然是：

- 网络帧到达
- 立即尝试写 `AudioTrack`
- 写失败就 stop / restart

这导致：

- 网络到包抖动和本地写入失败相互放大
- 小抖动也会进入 `write_failed -> stop -> rebuffer -> restart`
- 本地无法区分“服务端供给断续”和“设备播放设备层错误”

### 3.3 架构层根因

真正的根因不是某个阈值，而是：

- 缺少唯一 `dialog runtime`
- 缺少独立 uplink / downlink media engine
- 缺少结构化 event / command 接口
- 缺少 turn timeline 级可度量指标

## 4. 新架构目标

### 4.1 分层目标

新的运行时架构应明确拆成五层：

1. `app`
   - 只做启动、装配、诊断入口
2. `river_core`
   - 唯一会话编排层
   - 维护 `dialog runtime` 真相
   - 处理 event -> command
3. `river_voice`
   - 只负责 capture / preproc / VAD / KWS / playback / reference
   - 只上报事实，不再直接操纵具体云端 provider
4. `river_cloud`
   - 只负责 provider transport / protocol / command adaptation
   - 不再承载全部 turn policy
5. `river_diag`
   - 只读 runtime / media engine / transport 快照

### 4.2 运行时目标

核心运行时应该至少显式维护：

- session state
- input lane state
- output lane state
- playback lane state
- duplex mode / fallback reason
- current turn id / preview id / response id / playback id
- uplink/downlink media engine 状态

### 4.3 接口目标

模块之间只通过两类接口通信：

- typed event
  - `WAKEWORD_DETECTED`
  - `INPUT_SPEECH_START`
  - `INPUT_ENDPOINT_HINT`
  - `PLAYBACK_WRITE_FAILED`
  - `UPLINK_RETRY_ARMED`
- typed command
  - `OPEN_AND_LISTEN`
  - `COMMIT_INPUT`
  - `INTERRUPT_OUTPUT`
  - `REQUEST_PLAYBACK_ACK`

避免继续依赖：

- 直接跨层函数调用
- 只传 `reason` 字符串的软同步

## 5. 建议的新模块形态

### 5.1 `river_core`

建议逐步引入：

- `river_dialog_runtime.[ch]`
  - 唯一真相源
- `river_dialog_events.[ch]`
  - 结构化事件定义
- `river_dialog_ports.[ch]`
  - voice / cloud 的稳定 port
- `river_turn_coordinator.[ch]`
  - 负责 turn 级状态迁移

### 5.2 `river_cloud`

建议把当前单体 XiaoZhi adapter 拆成：

- `river_xiaozhi_transport`
  - websocket / reconnect / raw message ingress
- `river_xiaozhi_protocol`
  - encode / decode / capability / message mapping
- `river_xiaozhi_uplink_engine`
  - accumulate / queue / in-flight / retry / stale trim / pacing
- `river_xiaozhi_downlink_engine`
  - network ingress / jitter buffer / playback scheduling / rebuffer
- `river_xiaozhi_session_runtime`
  - provider-specific cached ids / ack context

### 5.3 `river_voice`

保留现有能力，但只产出：

- wakeword event
- vad event
- PCM frame / segment
- playback state
- reference telemetry

不再直接依赖具体 cloud API。

## 6. 迁移顺序

### 第一阶段：先收边界，不改大行为

目标：

- 去掉 `river_voice -> river_cloud` 直连
- 为后续 core-owned runtime 铺稳定 port

本轮已落地：

- 新增 [include/river/river_dialog_cloud_port.h](/root/ameba-river/include/river/river_dialog_cloud_port.h)
- 新增 [components/river_core/river_dialog_cloud_port.c](/root/ameba-river/components/river_core/river_dialog_cloud_port.c)
- `river_app` 统一注册 cloud port
- `river_voice` 不再直接 include / 调用 `river_cloud.*`

### 第二阶段：先修 uplink 的媒体语义

目标：

- 一帧 uplink 音频只能有一个明确命运：
  - queued
  - in-flight
  - retry_pending
  - dropped_with_reason
  - sent

本轮已开始落地：

- uplink ready queue 显式计入 retry frame
- uplink worker 支持 burst drain
- `BUSY/失败` 时保留当前帧
- round finish 日志增加：
  - `audio_ms`
  - `realtime_gap_ms`
  - `pace_pct`

### 第三阶段：建立 `dialog runtime`

目标：

- 让 `river_core` 成为唯一真相源
- `interaction_state` 退化成视图而不是主状态

### 第四阶段：重建 downlink / playback 模型

目标：

- 把网络抖动、解码、写播放设备三段分开
- 避免每次写失败都整流 stop/start

### 第五阶段：统一观测与验证

目标：

- 每轮统一输出：
  - `speech_start`
  - `first_preview`
  - `endpoint`
  - `accept`
  - `response.start`
  - `first_audio`
  - `playback_start`
  - `playback_end`

## 7. 结论

当前最需要避免的，是继续把局部补丁堆进
[components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)。

如果不先拆出：

- core-owned runtime
- stable voice/cloud ports
- independent uplink/downlink engines

那么本轮把 uplink 慢、播放卡顿压下去之后，后续仍会在：

- follow-up
- barge-in
- duplex fallback
- preview / endpoint
- playback ACK

这些路径上反复出现新的时序问题。
