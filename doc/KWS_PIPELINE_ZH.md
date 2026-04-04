# Ameba River KWS 端到端说明

Date: 2026-03-23

本文档描述当前代码基线下，Ameba River 从板端音频前端到本地唤醒、再到会话准入的完整 KWS 流程。

这不是一份“模型介绍”，而是一份面向工程实现的运行时说明，重点回答下面几个问题：

- 前端到唤醒的完整链路是什么
- 时序是怎样的
- 哪些判断是实时做的，哪些判断要等整段 speech gate 结束才做
- 为什么会看到 `kws gate open`、`kws status`、`wakeword hit` 这类日志
- 唤醒后为什么原则上不该继续跑本地 KWS

维护约定：

- 每次修改 `KWS / VAD gate / wake admission / conversation window` 相关逻辑时，同步更新本文档
- 每次调整 `prj.conf` 中 KWS 参数时，同步更新本文档中的“当前配置”章节
- 如果运行时日志格式变化，也要同步更新“日志解读”章节

## 1. 文档范围

本文覆盖的代码范围：

- 启动与模块接线
  - `components/river_core/river_app.c`
- 前端初始化与事件分发
  - `components/river_voice/river_voice_frontend.c`
- 板端实时前端热路径
  - `components/river_voice/river_voice_vad_probe.c`
- 本地 KWS 运行时
  - `components/river_voice/river_voice_kws.cc`
  - `include/river/river_voice_kws.h`
- 唤醒事件准入与状态编排
  - `components/river_core/river_session_coordinator.c`
- 云端会话窗口准入
  - `components/river_cloud/river_cloud_adapter.c`
  - `components/river_cloud/river_cloud_xiaozhi_session.c`

不覆盖的内容：

- 模型训练细节
- DS-CNN 网络结构设计细节
- 云端 ASR/TTS 协议细节
- 非 KWS 路径的设备控制或对话路由细节

## 2. 一句话总览

当前 KWS 不是直接在原始双麦流上做识别，而是走这条链路：

`capture(16ms, 2ch) -> fixed_dsb 单声道增强 -> silero VAD -> VAD-gated KWS pre-roll/queue -> KWS worker -> log-mel -> DS-CNN -> wake event -> session coordinator -> xiaozhi conversation window`

这里有两个关键设计：

1. KWS 不再持续无条件吃全量音频，而是由 VAD 打开和关闭 speech gate。
2. KWS 不只做“实时强触发”，也会在整段 speech gate 结束后做一次“整段兜底判断”。

## 3. 架构分层

### 3.1 数据面

数据面只关心音频如何流动：

1. 采集双麦帧
2. 做固定波束形成/增强
3. 做 VAD
4. 将增强后的单声道帧送入 KWS
5. KWS 内部生成 log-mel 特征
6. 触发 DS-CNN 推理
7. 产出本地唤醒事件

### 3.2 控制面

控制面只关心“当前是否允许 KWS 工作”和“命中后如何准入会话”：

1. `interaction_state` 是否在 `wake_monitoring`
2. 是否已有 active conversation window
3. 命中后是否进入云端会话窗口
4. 云端是否满足 `wifi ready + time ready`
5. 唤醒后是否应该继续接受新的本地 wakeword

这两条线要分开理解。前者决定“音频如何处理”，后者决定“结果是否有效”。

## 4. 启动时序

当前启动顺序在 `components/river_core/river_app.c` 中比较清晰：

1. 初始化运行时统计、交互状态、播放服务
2. 初始化 Wi-Fi 站点
3. 初始化 cloud adapter
4. 初始化 session coordinator
5. 接线：
   - playback listener -> session coordinator
   - voice frontend event handler -> session coordinator
   - cloud asr result handler -> session coordinator
6. 初始化 voice frontend
7. 初始化在线控制、交互诊断
8. 如启用，自动启动 `vad_probe`
9. 将 `interaction_state` 切到 `wake_monitoring`

对应代码：

- `river_app_boot()`：`components/river_core/river_app.c`
- `river_voice_frontend_init()`：`components/river_voice/river_voice_frontend.c`

## 5. 前端初始化时会打印什么

`river_voice_frontend_init()` 在启动时会把当前前端路径打印出来，主要是为了让运行时日志直接说明“本机当前实际工作路径”。

当前典型日志会说明：

- `frontend init: asr-first`
- `capture profile: 16000 Hz, 16ms, 2ch, AMIC1+AMIC3`
- `preproc backend: fixed_dsb`
- `detector backend: silero_vad`
- `kws backend: dscnn runtime=tflite_micro ...`
- `wake-stage validation path: capture -> fixed_dsb -> log_mel -> dscnn_kws -> wake event`
- `pure vad validation path: capture -> fixed_dsb -> silero -> stream/buffer bridge -> runtime logs`

要注意：

- 这些日志是“当前装配关系”和“当前验证路径”的说明
- 真正的运行时逻辑仍以 `vad_probe -> kws_submit_frame -> session coordinator` 的代码路径为准

## 6. 端到端时序

### 6.1 从采集到增强

当前板级音频路径是：

- 采样率：`16 kHz`
- 采集帧长：`16 ms`
- 采集通道：`2 ch`
- 主副麦：`AMIC1 + AMIC3`
- 前处理：`fixed_dsb`

`river_voice_vad_probe` 是当前板端实时热路径的总装配点。它做的事情是：

1. 从 capture 服务取一帧双麦 PCM
2. 可选读取 playback reference
3. 调 `river_voice_preproc_process()` 做增强
4. 调 `river_voice_detector_process()` 做 VAD
5. 把增强后的单声道帧送给 KWS
6. 同时继续走云端流式/诊断相关路径

关键入口在：

- `components/river_voice/river_voice_vad_probe.c:707`
- `components/river_voice/river_voice_vad_probe.c:732`
- `components/river_voice/river_voice_vad_probe.c:741`

### 6.2 从 VAD 到 KWS 提交

当前 KWS 的公开接口非常薄：

- `river_voice_kws_init()`
- `river_voice_kws_active()`
- `river_voice_kws_submit_frame(data, bytes, vad_valid, is_speech)`
- `river_voice_kws_dump_profile()`
- `river_voice_kws_dump_status()`

也就是说，VAD probe 并不需要了解 KWS 内部的 FFT、mel、推理或状态机，只负责提交：

- 一帧增强后 PCM
- 当前帧的 VAD 决策是否有效
- 当前帧是否被 VAD 判成 speech

这一步的意义是：KWS 的 gate 与队列策略是 KWS 自己内部实现的，而不是散落在 VAD probe 里。

## 7. KWS 的内部结构

当前 `river_voice_kws.cc` 可以按四块理解：

1. 特征前端
   - Hann 窗
   - FFT
   - Mel 滤波器组
   - log-mel
   - 归一化
2. 模型运行时
   - TFLite Micro interpreter
   - 输入输出量化参数
   - score 读取
3. Gate 与队列
   - pre-roll ring
   - input ring
   - worker task
4. 决策逻辑
   - 强阈值实时触发
   - gate 结束后的 fallback 触发
   - cooldown
   - interaction-state / conversation-window 抑制

## 8. KWS 的关键时间尺度

### 8.1 帧级时间

KWS 当前吃的不是任意大小的流，而是固定的 `16ms` 单声道帧：

- `RIVER_KWS_INPUT_FRAME_MS = 16`
- `RIVER_KWS_INPUT_FRAME_SAMPLES = 256`
- `RIVER_KWS_INPUT_FRAME_BYTES = 512`

这和 `vad_probe` 输出的增强单声道块是对齐的。

### 8.2 特征级时间

KWS 内部做 STFT / log-mel 的时间尺度是：

- FFT 窗长：`512 samples`，约 `32ms`
- hop：`160 samples`，约 `10ms`
- mel bins：`40`
- feature frames：`98`

因此模型看到的输入是：

- 形状：`98 x 40 x 1`
- 时间跨度：约 `980ms` 的 hop 视角，再叠加首个窗长

### 8.3 运行时调度尺度

当前配置下：

- KWS 推理 stride：`4`
- 也就是一旦 `98` 帧特征窗准备好，之后大约每 `40ms` 做一次推理

### 8.4 Gate 级时间

当前 gate 相关参数：

- pre-roll：`320ms`
- cooldown：`1800ms`
- fallback gate 最短时长：`700ms`
- fallback gate 最长时长：`2500ms`
- fallback gate 最少推理次数：`4`

## 9. KWS 的数据面：实际处理过程

### 9.1 预备状态：`gate_open = false`

当 KWS 处于未打开 gate 状态时，每来一帧只做一件事：

- 把当前 `16ms` 帧写进 `pre_roll_ring`

此时不会立即进入模型推理。

这样做的目的：

- 静音期间不把整条 KWS 热路径一直压满
- 一旦 speech 开始，能把 speech 开始前的少量上下文一起送进去，避免字首被截掉

### 9.2 VAD 判定 speech 开始：打开 gate

当 `river_voice_kws_submit_frame()` 收到：

- `vad_valid == true`
- `is_speech == true`
- 且当前 `gate_open == false`

会做下面几件事：

1. 先继续把当前帧写入 pre-roll ring
2. 向 KWS worker 队列写一个 `RESET` 控制项
3. 把 pre-roll ring 里的所有帧依次 flush 到 worker 队列
4. 标记 `gate_open = true`
5. 记录 `gate_started_ms`
6. 打一条 `kws gate open` 日志

这里的关键点是：

- `RESET` 先于 pre-roll PCM 入队
- 这样每一段新的 speech gate 都从干净的特征状态开始
- pre-roll 会把刚才缓存的上下文补回来

### 9.3 Gate 打开后：持续送 live PCM

只要 gate 处于打开状态，并且 VAD 还在说“当前是 speech”，每来一帧就：

- 直接写入 `input_ring`

worker 线程 `river_kws` 独立从 `input_ring` 读数据，不阻塞 `vad_probe` 热路径。

### 9.4 Worker 如何把 PCM 变成可推理输入

worker 线程收到 PCM 后，按样本推进：

1. 样本进入 `sample_ring`
2. 当累计到 `512` 样本时，先做第一帧 mel
3. 后续每累计 `160` 个 hop 样本，再做一帧 mel
4. mel 帧写入 `log_mel_history`
5. 当 `log_mel_history` 达到 `98` 帧后，`window_ready = true`
6. 之后按 `stride=4` 触发一次 DS-CNN 推理

### 9.5 输入张量如何组织

在每次推理前，KWS 会：

1. 取最近 `98 x 40` 的 log-mel 历史
2. 找整窗最大 dB 作为 reference
3. 做相对 dB 裁剪到 `[-80dB, 0dB]`
4. 按固定均值和标准差做归一化
5. 根据模型输入类型写到 tensor：
   - `uint8`
   - `int8`
   - 或 `float32`

当前归一化参数：

- `mean = -42.1177063`
- `std = 17.5219841`

这一步决定了“前端特征是否和训练时匹配”，是 KWS 识别效果里最敏感的一段之一。

## 10. KWS 的控制面：什么时候允许识别

`river_voice_kws_detection_allowed()` 当前只有两个准入条件：

1. 不能已有 active conversation window
2. `interaction_state` 必须等于 `RIVER_INTERACTION_WAKE_MONITORING`

这意味着：

- 只有在“未唤醒、正在待唤醒”的状态下，本地 KWS 才被允许真正工作
- 一旦已经唤醒进入会话窗口，KWS 会被主动压制
- 如果上层状态不是 `wake_monitoring`，`river_voice_kws_submit_frame()` 会立即 `disarm` 并清空内部状态

这就是“唤醒后就与服务器做实时交互，不再继续做本地 KWS 准入”的实际代码依据。

但要注意一个工程上的细节：

- “本地命中 wakeword”
- “session coordinator 已经受理”
- “conversation window 真正打开”

这三件事不是同一个时刻。

在 `wakeword hit` 到 `conversation window active` 之间，存在一个很短的准入过渡区。此时系统主要依赖：

- KWS 自己的 `cooldown`
- 命中后的 `disarm_after_trigger()`
- session coordinator 的 `queued / ignored / coalesced`

来抑制重复唤醒，而不是依赖 `interaction_state` 立即变化。

## 11. 实时判断逻辑

### 11.1 强触发路径：`threshold`

这是最直接的路径：

1. 每次模型推理后得到 `last_confidence_q15`
2. 如果分数 `>= CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15`
3. 连续命中次数 `hit_streak >= CONFIG_RIVER_KWS_TRIGGER_HOLD_FRAMES`
4. 且当前满足 `detection_allowed`
5. 立即触发 `wakeword hit ... mode=threshold`

当前配置下：

- 主阈值：`21299 q15`
- 约等于 `650 pm`
- hold：`1`

也就是说，当前只要单次推理分数超过主阈值，就允许立即触发。

### 11.2 为什么这叫“实时判断”

因为它不需要等到整段 speech 结束，只要当前推理点分数已经足够高，就能马上发出 wakeword 事件。

这条路径的优点：

- 响应最快

它的风险：

- 如果某些设备上 score 峰值不高，但整段语音整体像 wakeword，可能会漏检

## 12. 整段判断逻辑

### 12.1 为什么需要整段判断

真实设备上，常见情况不是“完全没进模型”，而是：

- speech gate 是完整的
- 整体时长也合理
- 但瞬时峰值没冲过主阈值

如果只靠“实时强触发”，这类 utterance 会被漏掉。

因此现在增加了 gate 结束后的 fallback 判断。

### 12.2 `gate_fallback` 的触发条件

当前逻辑是：

1. 当前 gate 已经关闭
2. 整段里没有触发过强阈值命中
3. `gate_best_confidence_q15 >= fallback_threshold_q15`
4. `gate_inference_count >= 4`
5. gate 总时长在 `[700ms, 2500ms]`
6. 当前仍满足 `detection_allowed`

满足后会触发：

- `wakeword hit ... mode=gate_fallback`

当前 fallback 阈值不是 `prj.conf` 配出来的，而是代码常量：

- `350 pm`
- 换算为 `11468 q15`

同时它会被限制为始终小于主阈值。

### 12.3 这条逻辑的意义

这条逻辑不是为了“无限放宽”，而是为了表达这样一种经验：

- 一段完整的 speech gate
- 时长合适
- 推理次数足够
- 最佳分数达到弱门槛

这类 utterance 即使没有出现特别尖锐的瞬时峰值，也可以被判作有效 wakeword。

## 13. Gate 的打开、关闭与触发后的行为

### 13.1 `kws gate open`

表示：

- VAD 判定 speech 开始
- KWS 已经把 pre-roll 刷入队列
- 新的一段 speech gate 正式开始

### 13.2 `kws gate close`

表示：

- VAD 判定当前 speech 已结束
- 不再继续向该 gate 中送 live PCM
- 但 worker 队列中剩余的帧仍可能继续被处理

因此：

- `gate close` 不等于“立即不再推理”
- 它只表示“producer 侧停止为该 gate 继续送新帧”

### 13.3 触发后的 `disarm`

一旦 KWS 发出有效 `wakeword hit`：

1. 设置 cooldown
2. 打日志
3. 通过 `river_voice_frontend_dispatch_event()` 向上发事件
4. 调 `river_voice_kws_disarm_after_trigger()`

`disarm_after_trigger()` 会：

- 清掉 `input_ring`
- 复位前端状态
- 清掉 pre-roll
- 保留最后一次触发相关的统计字段

这一步的直接目的，是避免同一段 backlog 被重复命中。

## 14. 唤醒事件如何进入会话

### 14.1 KWS 只负责发 wakeword 事件

KWS 命中后并不直接开 websocket，也不直接操作云端会话。它只发一个：

- `RIVER_VOICE_EVENT_WAKEWORD`

### 14.2 session coordinator 负责准入

`river_session_coordinator_on_voice_event()` 收到 `RIVER_VOICE_EVENT_WAKEWORD` 后，会走：

1. `river_session_schedule_wakeword()`
2. 检查当前是否已有 active conversation window
3. 检查 `interaction_state` 是否仍是 `wake_monitoring`
4. 如果允许，则把 wakeword 放入 `river_wake_evt` worker

worker 再调用：

- `river_cloud_adapter_begin_conversation_window("wakeword")`

### 14.3 为什么要多一个 wakeword worker

因为 wake 之后开会话这件事会涉及：

- Wi-Fi 状态
- 时间状态
- 网络 bootstrap
- session open

这些都不应塞回音频热路径或前端事件回调里。

所以现在是：

- 音频热路径发事件
- session coordinator 排队
- 独立 worker 做 conversation-window admission

## 15. conversation window 的准入条件

`river_cloud_xiaozhi_begin_conversation_window()` 当前会检查：

1. cloud 已初始化且 `xiaozhi` 已启用
2. Wi-Fi 已连接
3. 时间已就绪
   - 必要时先启动 SNTP
   - 不就绪则返回 busy
4. 若 session 尚未打开，则先 `river_xiaozhi_open_session()`
5. 若尚未 listening，则发 `listen_start("auto")`
6. 标记 conversation window active
7. 重置 pre-roll / listen_stop_pending 等状态

因此，命中 KWS 不等于一定立刻进入会话：

- 本地 wake 检测与云端 conversation window 准入是两个阶段

## 16. 当前配置

当前主要 KWS 配置来自 `prj.conf`：

| 项目 | 当前值 | 含义 |
| --- | --- | --- |
| `CONFIG_RIVER_KWS_TENSOR_ARENA_KB` | `688` | TFLM arena 大小 |
| `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15` | `1024` | 主触发阈值，约 `31 pm` |
| `CONFIG_RIVER_KWS_TRIGGER_HOLD_FRAMES` | `1` | 主阈值命中所需连续帧数 |
| `CONFIG_RIVER_KWS_COOLDOWN_MS` | `1800` | 命中后冷却时间 |
| `CONFIG_RIVER_KWS_LOG_PERIOD_MS` | `5000` | 状态日志周期 |
| `CONFIG_RIVER_KWS_INFERENCE_STRIDE_FRAMES` | `8` | 窗准备好后每 8 个 mel 帧推一次 |
| `CONFIG_RIVER_KWS_VAD_PRE_ROLL_MS` | `320` | speech 开始前保留的 pre-roll |
| `CONFIG_RIVER_KWS_PRE_ROLL_FLUSH_MAX_FRAMES` | `16` | gate 打开后最多保留的 pre-roll 帧数 |
| `CONFIG_RIVER_KWS_INPUT_QUEUE_FRAMES` | `64` | producer -> KWS worker 队列深度 |

当前硬编码但同样重要的参数在 `river_voice_kws.cc`：

| 项目 | 当前值 | 含义 |
| --- | --- | --- |
| sample rate | `16000` | 模型与前端采样率 |
| FFT window | `512` samples | 约 `32ms` |
| FFT hop | `160` samples | 约 `10ms` |
| mel bins | `40` | log-mel 维度 |
| feature frames | `98` | 模型时序长度 |
| fallback threshold floor | `400 pm` | 代码里的整段兜底弱阈值下限 |
| effective weak threshold | `~31 pm` | 当主阈值低于 floor 时，运行时会夹到 `primary - 1` |
| fallback min ms | `700ms` | 整段兜底最短 gate |
| fallback max ms | `2500ms` | 整段兜底最长 gate |
| fallback min infer | `4` | 整段兜底至少推理次数 |

## 17. 时序示例

下面给一个典型的一次成功唤醒时序：

```text
boot
  -> river_app_boot()
  -> river_voice_frontend_init()
  -> river_voice_kws_init()
  -> interaction_state = wake_monitoring

steady state (silence)
  -> capture 16ms frame
  -> fixed_dsb
  -> silero vad = silence
  -> KWS only stores pre-roll

user starts speaking wakeword
  -> vad_valid = true, is_speech = true
  -> KWS gate open
  -> enqueue RESET
  -> flush pre-roll
  -> enqueue live PCM
  -> worker builds mel frames
  -> window_ready = true
  -> run inference every stride=4

if score crosses main threshold
  -> wakeword hit mode=threshold
else if whole gate ends with enough best score / duration / infer count
  -> wakeword hit mode=gate_fallback

after hit
  -> frontend dispatches RIVER_VOICE_EVENT_WAKEWORD
  -> session coordinator queues wakeword
  -> wakeword worker tries begin_conversation_window("wakeword")
  -> xiaozhi session open + listen_start
  -> interaction_state leaves wake_monitoring
  -> local KWS detection_allowed() becomes false
```

## 18. 实时与整段判断的区别

这是最容易混淆的地方。

### 18.1 实时判断

实时判断看的是：

- “此刻这一次推理”的 score 是否已经足够高

特点：

- 延迟最低
- 对局部峰值依赖更强

### 18.2 整段判断

整段判断看的是：

- 这整段 speech gate 的最佳分数
- gate 持续时长是否像一个完整唤醒词
- 整段中实际做了几次推理

特点：

- 响应略慢
- 对“完整但不尖锐”的 utterance 更友好

两者并不是二选一，而是串联关系：

1. 先尝试实时强触发
2. 如果没成功，再在 gate 结束后给整段一次兜底机会

## 19. 为什么日志里会看到这些字段

### 19.1 `kws gate open: pre_roll=X/Y queue=A/B`

表示：

- 打开新 speech gate 时，pre-roll 刷了多少帧
- 当前 worker 队列里已有多少项目

### 19.2 `kws gate close: gate_best_pm=... gate_infer=...`

表示：

- 这一整段 speech gate 的最佳分数
- 这一整段里实际跑了多少次推理

### 19.3 `kws status: ...`

这是周期性运行状态快照，重要字段含义如下：

- `gate`
  - `open` 表示当前仍在 speech gate 内
- `ready`
  - 是否已经积满 `98` 帧特征窗
- `score_pm`
  - 最近一次推理分数
- `gate_best_pm`
  - 当前或最近一段 gate 的最高分
- `thresh_pm`
  - 主阈值
- `weak_pm`
  - fallback 弱阈值
- `streak`
  - 当前连续命中次数 / 要求次数
- `hits`
  - 超过主阈值的累计次数
- `triggers`
  - 成功触发次数
- `window`
  - 当前 mel 历史窗是否装满
- `infer`
  - 累计推理次数
- `gate_infer`
  - 当前 gate 内推理次数
- `queue`
  - KWS worker 输入队列占用
- `dropped`
  - worker 输入队列丢弃次数
- `pre`
  - pre-roll 当前占用
- `pre_dropped`
  - pre-roll 环形覆盖旧帧次数
- `opens / closes`
  - speech gate 打开关闭计数

### 19.4 `wakeword hit: ... mode=...`

`mode` 决定触发来源：

- `threshold`
  - 实时强触发
- `gate_fallback`
  - 整段兜底触发

### 19.5 `wakeword queued / ignored / coalesced / admission deferred`

这些日志已经不属于 KWS 本身，而属于 session coordinator：

- `queued`
  - 已进入准入队列
- `ignored`
  - 当前状态不允许新的 wake
- `coalesced`
  - 队列中已有 pending wake，本次只做合并
- `admission deferred`
  - 本地命中了，但云端窗口暂时开不起来，通常因为 Wi-Fi / time / transport 尚未 ready

## 20. 关于 `kws tensor data drift`

运行时如果看到：

- `kws tensor data drift: runtime_input=... cached_input=...`

它表示：

- TFLM 运行时暴露出来的 tensor data 指针与初始化时缓存的指针不一致

当前实现策略是：

1. 在 `AllocateTensors()` 后记录一份输入输出 data 指针
2. 运行时如果发现 tensor 的 data 指针变化，只打一次 warning
3. 继续使用初始化后验证过字节数和类型的缓存指针

这条日志本身不是 wake 判定逻辑的一部分，但它是一个非常重要的运行时一致性诊断信号。

## 21. 当前实现的工程性取舍

### 21.1 为什么先 VAD 再 KWS

因为持续全量跑本地 KWS 会显著放大：

- CPU 占用
- 队列长度
- 堆驻留压力
- backlog 风险

VAD-gated KWS 的核心价值是把：

- “待机静音成本”
- “真正识别成本”

分开。

### 21.2 为什么还要保留 pre-roll

因为只在 VAD 判 speech 后才开始送帧，会天然截掉字首。

pre-roll 的作用就是：

- 把 speech 开始前的一小段上下文一起补回来

### 21.3 为什么唤醒后要禁止本地 KWS

因为唤醒后系统的关注点已经变成：

- 云端实时交互
- follow-up
- 播放/打断/会话窗口

此时继续把本地 KWS 当作准入入口，容易带来：

- 重复唤醒
- 同段 backlog 重复命中
- 会话窗口抖动
- 额外算力消耗

所以现在的设计目标是：

- `wake_monitoring` 才做本地 KWS
- 非 `wake_monitoring` 一律不做新的本地 wake 准入

## 22. 读日志时的优先观察顺序

如果要判断“这次为什么没有唤醒”，建议按下面顺序看：

1. 有没有 `vad state=speech`
   - 没有，先看前端/VAD，不看 KWS
2. 有没有 `kws gate open`
   - 没有，说明没进入 KWS gate
3. `kws status` 的 `ready` 是否变成 `yes`
   - 没有，说明特征窗没积满
4. `score_pm / gate_best_pm` 到了多少
   - 低于 `weak_pm`，说明声学匹配本身就不够
   - 介于 `weak_pm` 与 `thresh_pm`，更像 fallback 是否成立的问题
5. 有没有 `wakeword hit`
   - 没有，再看 `gate_infer / gate duration / detection_allowed`
6. 有 `wakeword hit` 但没进会话
   - 看 `wakeword queued / admission deferred / conversation window`

## 23. 当前版本下最重要的理解

当前 KWS 不是一个“单点瞬时分类器”，而是一个两阶段决策系统：

1. VAD 先决定这段音频值不值得让 KWS 工作
2. KWS 再在这段 gate 内做：
   - 实时强触发
   - 整段 fallback 触发

最后，session coordinator 再决定：

3. 这次本地 wake 是否能升级成真正的 conversation window

因此，一次“未唤醒”可能发生在三层中的任意一层：

- 前端/VAD 层
- KWS 判定层
- 会话准入层

排查时不能把这三层混在一起。

## 24. 后续维护要求

以后如果发生以下变更，必须更新本文档：

- `prj.conf` 中任一 KWS 参数变化
- `river_voice_kws_submit_frame()` 的 gate 逻辑变化
- `river_voice_kws_maybe_emit_trigger()` 的强触发 / fallback 条件变化
- `river_voice_kws_detection_allowed()` 的准入条件变化
- `river_session_schedule_wakeword()` 的 ignore / queue / coalesce 规则变化
- `river_cloud_xiaozhi_begin_conversation_window()` 的准入条件变化

建议每次修改后至少同步检查这几节：

- “关键时间尺度”
- “实时判断逻辑”
- “整段判断逻辑”
- “当前配置”
- “日志解读”
