# Latest-SDK FP32 XiaoZhi Uplink Backpressure Analysis

## 背景

本结论基于 latest SDK `/root/ameba-rtos` 上运行的
`student_bc_resnet_tiny_v2_fp32_debug` 变体。

在此前步骤里，已经确认两件事：

1. 板端/本机对拍链路是成立的。
   `student_bc_resnet_tiny_v2_fp32_debug` 的板端张量 dump 与主机重放严格一致，
   说明当前端侧 KWS 部署不是“模型适配错误”。
2. 真实运行链路也是成立的。
   已观察到完整的 `wakeword -> websocket connect -> ASR -> LLM -> TTS -> playback stop`
   链路。

因此，这里分析的 `xiaozhi ws backpressure` / `send_queue_busy`，应视为
云端实时上行质量问题，而不是 KWS 模型接入错误。

## 实机现象

在 latest-SDK FP32 实机运行中，串口看到如下特征：

- 唤醒成功：
  - `wakeword hit: text=小欧管家 score_pm=271 q15=8912`
- 云端会话成功建立：
  - `Connected to websocket server`
  - `server hello: sid=8665a824`
- ASR/TTS 实际运行：
  - `asr provider=xiaozhi_realtime session started`
  - `tts ... state=sentence_start`
  - `playback start: stream=xiaozhi_tts`
  - `playback stop: stream=xiaozhi_tts`
- 但同一会话中反复出现：
  - `xiaozhi ws backpressure: kind=audio reason=soft_reserve ready=14 recycle=0 max=16 stable=12 reserve=2 free=2 soft_limit=14`
  - `xiaozhi uplink backpressure: queued=.../64 busy=... streak=... backoff=... stale_drop=...`
  - `last_err=send_queue_busy`

同时，状态快照仍显示：

- `xiaozhi session=yes hello=yes`
- `txq=14/16 recycle=0 stable=12 audio_soft_limit=14`
- `reserve_bp` 持续增长，而 `full_bp` 可以保持为 `0`
- `audio_rx` 非零
- `playback_service ... starts=6 stops=6`

这说明问题并不是“链路彻底失败”，而是“上行音频在拥塞时被主动限流并丢旧帧”，
但会话整体仍可继续。

## 机制拆解

### 1. websocket 音频上行在 `ready >= 14` 时会触发项目侧软限流

项目侧 websocket 队列最大深度已是 `16`，音频发送仍保留 `2` 个槽位：

- `RIVER_XIAOZHI_WS_QUEUE_MAX = 16`
- `RIVER_XIAOZHI_WS_AUDIO_QUEUE_RESERVE = 2`

发送前会检查：

- 如果 `(ready + reserve) >= max`，则直接判定 backpressure
- 此时设置 `last_error = "send_queue_busy"`
- 并打印：
  - `xiaozhi ws backpressure: kind=audio reason=%s ready=%lu recycle=%lu max=%lu stable=%lu reserve=%lu free=%lu soft_limit=%lu`

因此，对于音频上行来说，真正的软阈值不是 `16`，而是：

- `soft_limit = max - reserve = 14`

这意味着：

- `ready=14/16` 时，并不是 websocket 队列已经“硬满”
- 而是项目主动为 JSON/control/MCP 保留 `2` 个槽位，不再让音频继续挤占
- 只有 `ready=16/16` 时，才属于真正的 `hard_full`

所以用户现场看到的 `ready=14/16` 高水位，本质上首先是
`soft_reserve` 命中，而不是 SDK 队列彻底塞满。

### 2. `ready` / `recycle` / `stable` 三个数的语义不能混看

结合 latest SDK websocket 实现，当前几个计数分别代表：

- `ready`
  - 已经排队等待发送的 buffer 数量
- `recycle`
  - 已发送完成、可被复用的空 buffer 数量
- `stable`
  - SDK 希望保留在 recycle 池里的空 buffer 上限

因此：

- `ready=14 recycle=0` 的直接含义是“当前有 14 个待发送 buffer，暂时没有空闲可复用 buffer”
- 它不等于“队列已经 16/16 全占满”
- `stable=12` 也不等于“允许 ready 到 12 就停”

`stable` 只影响 SDK 在发送完成后，是把 buffer 留在 recycle 池里，还是直接
`free` 掉；它不改变项目侧的音频软限流阈值 `14`。

### 3. 当前发送策略是非阻塞优先，不等队列腾挪

连接建立后，项目显式设置了：

- `ws_set_senddata_block_time(0)`
- `ws_multisend_opts(..., 12)`

代码注释写得很明确：

- 一旦发送队列接近满，项目侧宁可尽早返回 `BUSY`
- 也不愿在 transport mutex 上长时间阻塞

这里的 `12` 不是 backpressure 阈值，而是“预热并保留多少个可回收发送
buffer”。此前 `1` 的策略更容易在 burst speech 下反复 `malloc/free`；现在把
它调到 `12`，是为了先收敛 allocator 抖动，再继续观察真正的拥塞位置。

### 4. uplink worker 在 `BUSY` 时会退避，并主动裁掉旧帧

当 `river_xiaozhi_send_audio()` 返回 `RIVER_ERR_BUSY` 后，
`river_cloud_xiaozhi_uplink_task()` 会做三件事：

1. `busy_count++`
2. 指数退避，退避窗口大致为：
   - 第 1 次 busy: `40 ms`
   - 第 2 次 busy: `80 ms`
   - 第 3 次及以上: `160 ms`
3. 裁掉 uplink ring 中的旧帧，只保留 `6` 帧：
   - `RIVER_CLOUD_XIAOZHI_UPLINK_STALE_FRAMES_MAX = 6`

这意味着当前实现优先保证“新鲜语音”，而不是“完整语音”。
一旦链路一段时间发不出去，就会通过 `stale_drop` 丢弃旧音频。

### 5. `16 ms` 采集桥接到 `20 ms` Opus，上线瞬间存在启动突发

当前 ASR bridge 输入是 `16 ms` 帧，而 XiaoZhi 上行 Opus 编码是 `20 ms` 帧。
因此项目先把 `16 ms` PCM 累积到 `20 ms` 再编码发送。

更关键的是，session 刚打开时会先把 pre-roll 一次性回灌：

- `RIVER_CLOUD_XIAOZHI_PRE_ROLL_MAX_MS = 256`
- 当前帧长是 `16 ms`
- 因此最多会先冲入约 `16` 个 `16 ms` PCM 帧

按 `16 kHz / mono / 16bit / 20 ms = 640 B` 计算：

- `16 * 512 B = 8192 B`
- 这会在打开会话时立刻形成 `12` 个完整的 `20 ms` uplink packet
- 还会残留 `512 B` 累积尾巴等待下一帧补齐

这不是 websocket 队列直接爆掉，因为中间还有项目自己的 `64` 帧 uplink ring；
但它会显著增加“会话刚打开的前几百毫秒内，发送端持续贴近 `14/16`
音频软阈值”的概率。

换句话说，`ready=14/16` 的高水位，通常是几件事叠加后的结果：

1. 会话打开瞬间先回灌 pre-roll
2. `16 ms` 采集帧需要聚合成 `20 ms` Opus 帧
3. WLAN/TLS 某个短窗口里发送排空速度慢于生产速度
4. 项目为了给控制消息保留 `2` 个槽位，在 `ready=14` 就主动拒绝继续塞音频

## 结论

当前 latest-SDK FP32 上看到的 `send_queue_busy`，更准确地说是：

- 发生在 `wakeword` 之后、`云端实时上行` 这一层
- 是项目显式设计的“新鲜度优先”限流机制在 `soft_reserve(14/16)` 处被触发
- 不是 KWS 模型部署错误
- 大多数情况下也不是 websocket 队列彻底溢出失控

新增诊断项后，可以更明确地区分两类情况：

- `reserve_bp` 增长而 `full_bp=0`
  - 说明压力主要停在项目预留的 `2` 个控制槽位之前
- `full_bp` 也持续增长
  - 才说明 SDK send queue 真正跑到了 `16/16` 的硬满状态

从已观察到的行为看，它目前属于：

- `功能可用`
- `实时质量仍有压力`

而不是：

- `功能阻断`
- `模型不可上板`

## 对后续调试的意义

后续任何唤醒词模型调试，都应继续保留板端/本机对拍流程。

原因是：

- KWS 对拍已经能证明模型前端和推理输入输出是否一致
- 本文分析的问题发生在 KWS 之后的云端音频上行链路
- 如果不保留对拍流程，后续很容易把“上行拥塞导致的体验问题”误判成“模型适配错误”

## 后续优化方向

建议按风险从低到高评估：

1. 先看新增计数，而不是继续猜。
   重点观察：
   - `audio_soft_limit`
   - `q_peak`
   - `reserve_bp`
   - `full_bp`
   - `stale_drop`
2. 先削峰，再决定是否改 reserve。
   最值得优先验证的是：
   - pre-roll 不要一次性全部灌入 uplink，而是按 `20 ms` 节拍平滑释放
3. 当前先不动 `RIVER_XIAOZHI_WS_AUDIO_QUEUE_RESERVE = 2`。
   原因是：
   - 这 `2` 个槽位目前承担的是 JSON/control/MCP 留白
   - 如果没证明 `full_bp` 真在增长，贸然降 reserve 只会把控制消息也拖进竞争
4. 如果 `reserve_bp` 仍高，再看 transport 调度。
   当前 `pump` 侧 `ws_poll(20 ms)` 与 uplink 发送共享 transport lock，
   若后续数据证明真正瓶颈在 poll/发送调度，再评估更细的拆分方案。

## 关键代码位置

- websocket 队列上限与保留槽位：
  - `include/river/river_xiaozhi_credentials.h`
  - `components/river_cloud/river_xiaozhi_ws.c`
- backpressure 判定与 `send_queue_busy`：
  - `components/river_cloud/river_xiaozhi_ws.c`
- uplink busy 退避与 `stale_drop`：
  - `components/river_cloud/river_cloud_adapter.c`
- session 打开后 pre-roll 回灌：
  - `components/river_cloud/river_cloud_adapter.c`
