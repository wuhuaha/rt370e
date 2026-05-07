# Orvibo 语音客户端 Clean-Slate 重构计划

Status: active
Last Updated: 2026-05-07
Branch: `xiaozhi-client`
SDK Baseline: `/root/ameba-rtos`
External Protocol Baseline: XiaoZhi-compatible realtime server protocol

Latest Verified Slice:

- `Step H.xiaozhi-client.4` 已收紧 Orvibo uplink 发送背压和诊断。
- `river_orvibo_protocol_send_audio()` 现在只做非阻塞入队；`orvibo_uplink` sender task 负责实际 `ws_sendBinary()` 提交和短重试。
- uplink frame 带 `session_epoch`，WebSocket close/reopen 后旧会话残留音频不会误发到新 session。
- protocol status 输出 uplink task、queue depth、enqueue/drop/retry/fail；app status 使用 `uplink_enq` 表示 app 侧入队计数。
- 最新 `/root/ameba-rtos` SDK build 已通过。
- `Step H.xiaozhi-client.3` 已按 `~/xiaozhi-esp32` 的 speaking 边界收紧 TTS 下行链路。
- 服务端二进制下行音频现在只在 Orvibo 业务态为 `speaking` 时进入解码/播放；非 speaking 状态迟到音频会丢弃并计数。
- `tts start` 会重置下行 Opus decoder 并清理上一轮残留播放；`tts stop` 会受限等待播放缓存 drain，再把 Orvibo audio mode 切回 listening。
- playback status 现在输出 SDK buffer occupancy 和 drain 计数，便于板端判断尾音排空还是超时强制收口。
- 最新 `/root/ameba-rtos` SDK build 已通过。
- `Step H.xiaozhi-client.2` 已按 `~/xiaozhi-esp32` 二次校准 Orvibo access 激活语义。
- `activation.code` 现在只作为用户绑定提示；只有 `activation.challenge` 存在时才进入 `/activate` 轮询。
- access `ready` 已与 `websocket_configured` 拆分，待绑定/待激活时不会误开 WebSocket 音频通道。
- Wi-Fi ready 后会立即刷新 OTA/config；Wi-Fi 已连接但 access 未 ready 时每 10 秒周期性重刷，用户完成绑定后可自动进入 ready。
- 最新 `/root/ameba-rtos` SDK build 已通过。
- `Step H.xiaozhi-client.1` 已补齐 Orvibo-owned 接入层，使端侧具备烧录后直接按 XiaoZhi-compatible contract 连接服务器的必要闭环。
- 新增 Orvibo access 层负责 OTA/config、无序列号激活轮询、device/client identity 和 websocket url/token/version 应用。
- WebSocket open 现在对齐参考客户端：发送鉴权/协议/设备头，发送 hello，并在返回成功前等待 server hello；失败立即关闭并交给 Orvibo 状态机恢复。
- 语音交互闭环已覆盖 wake -> open -> server hello -> listen detect/start -> binary audio -> TTS start/stop -> barge-in abort。
- MCP 初期仍只暴露 `self.get_device_status` 和 `self.audio_speaker.set_volume`。
- 当前 VAD、KWS、KWS tensor dump、alignment replay、preproc/AEC/BF 仍保留并参与构建。
- 最新 `/root/ameba-rtos` SDK build 已通过。
- `Step G.xiaozhi-client.1` 已删除旧主干源码/头文件残留并收窄 Orvibo-only 配置面。
- 旧 `dialog / session coordinator / cloud adapter / split ASR-TTS / legacy xiaozhi transport / online_control` 已从 live tree 删除。
- Kconfig/prj.conf 不再暴露已删除 backend。
- 当前 VAD、KWS、KWS tensor dump、alignment replay、preproc/AEC/BF 仍保留并参与构建。
- 最新 `/root/ameba-rtos` SDK build 已通过。
- `Step C.xiaozhi-client.1` 已切换到可构建的 Orvibo voice client live graph。
- live CMake 已移除旧 `dialog_runtime / session_coordinator / cloud_adapter / provider / xiaozhi_ws` 主干。
- 当前 VAD、KWS、KWS tensor dump、alignment replay 仍保留并参与构建。
- 最新 `/root/ameba-rtos` SDK build 已通过。

## 0. 文档用途

这份计划是后续实现的工程控制文档，不是方向性备忘录。后续每个提交都应能回答：

- 本步改变了哪个边界
- 是否触碰 VAD/KWS 保护区
- 是否让旧主干继续进入 live CMake
- 是否能本地构建
- 板端要看哪些日志才能判断行为正确

每个实现切片必须同步更新：

- `.codex/changes.md`
- `.codex/verification.md`
- 本计划的状态或下一步，如果计划发生变化

每个实现切片必须提交，提交信息默认使用清晰中文。

## 1. 重构判定

这个分支不再做“在现有 river 架构上兼容某个服务器”的增量接入。当前目标是把固件重建成 Orvibo 语音客户端主干。

第一版外部 wire contract 参考 `~/xiaozhi-esp32` 的设备端主干，但内部文件名、函数名、模块名和诊断命令不得继续使用 `xiaozhi` 或 `xz` 作为新命名前缀：

- `Application`
- `DeviceStateMachine`
- `AudioService`
- `Protocol`
- `McpServer`

当前仓库里可保留的不是旧主干，而是少数已经验证过的本地能力和板级适配。后续实现允许替换或删除现有 `river_core / river_cloud / playback / online_control` 里的大部分代码，只要每一步仍可构建、可验证。

命名原则：

- 新 public header 使用 `river_orvibo*.h`。
- 新 C 源文件使用 `river_orvibo*.c` 或 `orvibo_*.c`，按所在组件现有风格决定。
- 新 public 函数使用 `river_orvibo_*`。
- 新 internal static/helper 函数使用 `orvibo_*`。
- 新 task、queue、status、diag 名称使用 `orvibo_*`。
- `xiaozhi` 只允许出现在历史文档、兼容协议说明、删除清单或明确的 legacy adapter 名称中。
- 当前 git 分支名 `xiaozhi-client` 是历史分支名，不作为后续代码命名依据。

## 2. 不变量

这些规则在所有阶段都生效：

- Orvibo 主干只保留一个在线对话协议适配器；第一版适配 XiaoZhi-compatible realtime server protocol。
- 当前 VAD 和当前唤醒词识别是唯一硬保护的语音算法实现。
- KWS tensor dump、alignment replay、本地/板端 parity 能力不能被弱化。
- 新主干不能依赖旧 `ASR provider / TTS provider / dialog runtime / session coordinator` 生命周期。
- MCP 初期只保留音量控制。
- 不追求最小改动量；追求清晰主干、可验证行为和可控风险。
- 初版允许半双工稳定优先；AEC/BF/full-duplex 之后按新主干重建。

## 3. 硬保护边界

必须保留当前项目实现：

- VAD：
  - `include/river/river_voice_detector.h`
  - `components/river_voice/river_voice_detector.c`
  - `components/river_voice/river_voice_detector_silero.cc`
  - `components/river_voice/generated/river_silero_vad_model_data.*`
  - `components/river_voice/river_orvibo_audio_service.c` 对当前 VAD API 的主干使用
  - 旧 cloud/dialog VAD probe 不再是保护对象；当前只保留 `river_voice_vad_probe_stub.c`
- 唤醒词识别：
  - `include/river/river_voice_kws.h`
  - `components/river_voice/river_voice_kws.cc`
  - `components/river_voice/river_voice_kws_mean_patch.*`
  - `components/river_voice/generated/*kws*`
  - KWS tensor dump、alignment replay、本地/板端对比路径

允许做的修改：

- 给 VAD/KWS 增加更薄的新主干适配层。
- 解耦 VAD/KWS 对旧 `dialog_runtime`、旧 `session_coordinator`、旧 `cloud_adapter` 的依赖。
- 增加状态查询或事件回调，但不得改变算法输入输出语义。

禁止做的修改：

- 替换 Silero VAD 模型或后端实现。
- 替换 KWS 模型、前端特征契约、阈值口径或 tensor dump 语义。
- 删除 `river_voice_kws_request_tensor_dump_next()`、`river_voice_kws_dump_tensor_chunk()`、`river_voice_kws_run_alignment_sample()`、`river_voice_kws_dump_alignment_status()`。
- 删除板端与本地对齐所需的 dump / replay 路径。

## 4. 可推倒边界

这些内容不再作为兼容目标，可以删除、替换或重写：

- `Iflytek` ASR / TTS
- `ASR provider / TTS provider` 抽象
- `river_cloud_adapter`
- `river_cloud_asr_bridge_runtime`
- `river_dialog_runtime`
- `river_session_coordinator`
- `river_dialog_cloud_port`
- `river_dialog_wake_admission`
- `river_interaction_state` 旧状态模型
- 现有旧实时协议 playback recovery 大状态机
- 现有 `river_online_control` 中非音量控制能力
- 旧 light / fan / curtain / socket MCP 工具
- 为旧多后端架构服务的诊断命令

AEC、BF、播放服务、引用路径、音频队列可以复用实现片段，但不再是保护对象。如果保留它们，是因为它们适合新主干，不是因为迁移成本。

## 5. 目标架构

最终主链应收敛为：

```text
app/app_main.c
  -> orvibo_app
       -> orvibo_state_machine
       -> orvibo_audio_service
            -> current KWS
            -> current VAD
            -> Opus encode/decode
            -> board capture/playback adapter
       -> orvibo_protocol
            -> websocket transport
            -> hello/listen/abort/mcp
            -> binary audio
       -> orvibo_mcp_volume
       -> orvibo_diag
```

建议仍使用现有目录，但改变语义：

- `components/river_core`：
  - Orvibo 专用应用编排和设备状态机
  - 不再承载旧 provider 派生状态
- `components/river_voice`：
  - 保留当前 VAD/KWS
  - 提供 `orvibo_audio_service` 可直接调用的捕获、检测、唤醒适配
- `components/river_cloud`：
  - Orvibo realtime protocol、WebSocket transport、MCP volume
  - 不再承载多云层、多 provider 或旧对话运行时
- `components/river_diag`：
  - KWS/VAD 诊断必须保留
  - 新增面向 `orvibo_app / orvibo_audio_service / orvibo_protocol` 的状态 dump

## 6. 新模块契约

### 6.1 `orvibo_app`

职责：

- 初始化 board、Wi-Fi、音频服务、协议层、诊断。
- 持有设备状态机。
- 作为跨线程事件的唯一业务裁决点。
- 将 KWS/VAD/protocol/playback 事件转换成状态机事件。

输入事件：

- `NETWORK_READY`
- `NETWORK_LOST`
- `WAKE_DETECTED`
- `USER_SPEECH_STARTED`
- `USER_SPEECH_ENDED`
- `SERVER_HELLO`
- `SERVER_TTS_STARTED`
- `SERVER_TTS_FINISHED`
- `SERVER_ERROR`
- `AUDIO_ERROR`

输出动作：

- `protocol.open_audio_channel`
- `protocol.send_start_listening`
- `protocol.send_stop_listening`
- `protocol.send_wake_word_detected`
- `protocol.send_abort_speaking`
- `audio.start_kws`
- `audio.start_listening`
- `audio.start_playback`
- `audio.stop_playback`

禁止依赖：

- `river_dialog_runtime.h`
- `river_dialog_cloud_port.h`
- `river_session_coordinator.h`
- `river_cloud_adapter.h`
- `river_interaction_state.h`

质量要求：

- 状态变化必须有单行确定性日志：`orvibo state: old -> new reason=...`
- 每个外部事件必须能在日志中追踪到一次处理结果。
- 业务状态只能由 `orvibo_app` 或 `orvibo_state_machine` 改变，其他模块只发事件。

### 6.2 `orvibo_state_machine`

目标状态：

- `starting`
- `network_wait`
- `idle`
- `connecting`
- `listening`
- `speaking`
- `recovering`
- `error`

核心转换：

```text
starting -> network_wait
network_wait -> idle              on NETWORK_READY
idle -> connecting                on WAKE_DETECTED
connecting -> listening           on AUDIO_CHANNEL_OPENED
listening -> speaking             on SERVER_TTS_STARTED
speaking -> listening             on USER_SPEECH_STARTED + abort_sent
speaking -> idle                  on SERVER_TTS_FINISHED
listening -> idle                 on LISTEN_TIMEOUT or USER_SPEECH_ENDED + committed
any -> network_wait               on NETWORK_LOST
any -> recovering                 on recoverable error
recovering -> idle                on recovery done
any -> error                      on fatal error
```

状态不变量：

- `idle`：KWS 应启用；Orvibo audio channel 可关闭。
- `connecting`：KWS 可暂停；正在打开 Orvibo audio channel。
- `listening`：VAD 和上行编码开启；下行播放未占用 speaker。
- `speaking`：下行播放开启；VAD 可用于 barge-in；KWS 不参与重复唤醒。
- `recovering`：停止上行、停止播放、关闭或重建协议连接。

质量要求：

- 非法转换必须记录并拒绝。
- 状态机不得直接执行网络或音频 IO，只返回下一步 action。
- 后续单测或 host-side 状态表验证应优先覆盖该模块。

### 6.3 `orvibo_audio_service`

职责：

- 管理 capture、KWS、VAD、Opus encode、Opus decode、playback。
- 对 `orvibo_app` 暴露事件：wake、speech start/end、audio packet ready、playback completed、audio error。
- 对 `orvibo_protocol` 暴露上行 Opus packet 队列。
- 接收 `orvibo_protocol` 下行 Opus packet 并送播放。

内部建议任务：

- `orvibo_audio_capture_task`
  - 读取 board PCM
  - idle 时喂 KWS
  - listening/speaking 时喂 VAD
  - listening 时写 PCM staging queue
- `orvibo_audio_codec_task`
  - 60ms PCM 聚合
  - Opus encode
  - Opus decode
  - 必须记录 encode/decode 失败计数
- `orvibo_audio_playback_task`
  - 下行 PCM 播放
  - 记录 underrun、write failure、queue depth

上行契约：

- 输入 PCM：16kHz、mono、16-bit。
- Opus frame：60ms。
- 初版不静默丢帧；队列满返回 busy 并计数。
- 每个上行 packet 记录 timestamp 或 monotonic sequence。

下行契约：

- 下行音频参数以 server hello 的 `audio_params` 为准。
- Opus decoder 参数变化时必须重建 decoder。
- 输出采样率不匹配时必须显式记录 resample path；如果暂未实现 resample，应 fail closed 并记录 unsupported。

KWS 接入契约：

- 使用 `river_voice_kws_init()` 初始化。
- 使用 `river_voice_kws_submit_frame()` 喂入符合现有 KWS 前端契约的 frame。
- 保留 `river_voice_kws_wake_handoff_block_reason()` 用于诊断，但新主干不得让它依赖旧 runtime。
- 保留所有 dump / alignment API。

VAD 接入契约：

- 使用 `river_voice_detector_open()`、`river_voice_detector_process()`、`river_voice_detector_close()`。
- `speech_probability_raw_q15` 和 `speech_probability_q15` 必须进入诊断快照。
- VAD 判定状态要有去抖策略，但原始 decision 必须可观测。

### 6.4 `orvibo_protocol`

职责：

- 管理 Orvibo realtime WebSocket 连接；第一版 wire contract 兼容 XiaoZhi server。
- 发送 hello/listen/abort/mcp/audio。
- 接收 server hello、JSON events、binary audio。
- 只向 `orvibo_app` 和 `orvibo_audio_service` 发事件，不直接改变设备状态。

统一接口：

- `river_orvibo_protocol_init`
- `river_orvibo_protocol_set_config`
- `river_orvibo_protocol_set_event_handler`
- `river_orvibo_protocol_open_audio_channel`
- `river_orvibo_protocol_close_audio_channel`
- `river_orvibo_protocol_poll`
- `river_orvibo_protocol_send_audio`
- `river_orvibo_protocol_send_wake_word_detected`
- `river_orvibo_protocol_send_start_listening`
- `river_orvibo_protocol_send_stop_listening`
- `river_orvibo_protocol_send_abort_speaking`
- `river_orvibo_protocol_send_mcp_message`
- `river_orvibo_protocol_dump_status`

WebSocket headers：

- `Authorization`
- `Protocol-Version`
- `Device-Id`
- `Client-Id`

`hello`：

- `type=hello`
- `version`
- `transport=websocket`
- `features.mcp=true`
- `audio_params.format=opus`
- `audio_params.sample_rate=16000`
- `audio_params.channels=1`
- `audio_params.frame_duration=60`

二进制音频：

- v1：裸 Opus payload
- v2：`version/type/reserved/timestamp/payload_size/payload`
- v3：`type/reserved/payload_size/payload`

禁止迁移：

- 旧 `conversation window`
- 旧 `cloud runtime truth`
- 旧 playback recovery 大状态机
- 旧 provider callback 适配层

### 6.5 `orvibo_mcp_volume`

职责：

- 仅处理音量工具。
- 输出 MCP JSON-RPC response。
- 不依赖 `river_online_control` 的 light/fan/curtain/socket 模型。

允许工具：

- `self.get_device_status`
- `self.audio_speaker.set_volume`

所有非音量工具：

- 返回 JSON-RPC success envelope + `isError=true`，或明确 protocol-level unsupported。
- 必须记录工具名和 unsupported 原因。

## 7. 并发与所有权

所有权规则：

- `orvibo_app` 拥有业务状态。
- `orvibo_protocol` 拥有 WebSocket 句柄和 transport lock。
- `orvibo_audio_service` 拥有 capture、codec、playback 队列。
- VAD/KWS 模块拥有自己的模型上下文和诊断状态。

跨线程通信：

- 只通过 queue/event/semaphore。
- 禁止从 protocol callback 直接调用播放或状态转换。
- 禁止从 audio callback 直接调用 WebSocket send；只能写上行队列或投递事件。

队列原则：

- queue depth 使用 RTL8730E 资源重新估算。
- 初期稳定优先，允许较高延迟。
- 队列满必须显式计数和日志，不允许 silent overwrite。
- 队列 high watermark 必须进入 status dump。

建议初始队列：

- capture PCM staging：至少覆盖 600ms。
- uplink Opus queue：至少覆盖 2s。
- downlink Opus queue：至少覆盖 2s。
- playback PCM queue：至少覆盖 500ms。

这些值不是最终调参结果；实现时必须在日志里打印实际配置。

## 8. 日志与诊断标准

必须提供：

- `orvibo app status`
  - state
  - last event
  - last error
  - uptime
- `orvibo protocol status`
  - configured/open
  - session_id
  - ws rx/tx counts
  - text rx/tx counts
  - audio rx/tx counts
  - backpressure counts
  - last server audio params
- `orvibo audio status`
  - capture running
  - kws active
  - vad active
  - encode/decode counts
  - queue depth/high watermark
  - dropped/busy counts
  - playback underrun/write failure counts
- `kws status`
  - 保留现有输出
- `vad status`
  - backend
  - frame/window
  - last probability
  - last decision

日志要求：

- 状态转换使用 `INFO`。
- 可恢复异常使用 `WARN`。
- 数据损坏、协议不兼容、无法继续的 IO 错误使用 `ERROR`。
- 高频日志必须限频。
- 音频 frame 级日志默认关闭，只能通过诊断开关开启。

## 9. 构建图控制

最终 live CMake 编译图中不应包含：

- `river_cloud_adapter.c`
- `river_cloud_asr_bridge_runtime.c`
- `river_asr_provider_registry.c`
- `river_asr_iflytek_rtasr.c`
- `river_tts_iflytek_ws.c`
- `river_dialog_runtime.c`
- `river_session_coordinator.c`
- `river_dialog_cloud_port.c`
- `river_dialog_wake_admission.c`

最终新主干不应 include：

- `river_dialog_runtime.h`
- `river_dialog_cloud_port.h`
- `river_interaction_state.h`
- `river_online_control.h`，除非只剩音量实现且命名已收敛

保留编译：

- `river_voice_detector.c`
- `river_voice_detector_silero.cc`
- `river_voice_kws.cc`
- `river_voice_kws_mean_patch.cc`，当配置需要时
- KWS/VAD generated model data
- KWS/VAD 诊断命令

每个阶段都要用 `rg` 检查 live CMake 源列表，而不是只检查源码是否存在。

## 10. 质量门禁

每个实现提交至少执行：

```bash
cd /root/ameba-river
git diff --check
python3 tools/diag/check_codex_harness.py
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

触碰 repo-level harness 文件时，`check_codex_harness.py` 必须通过。

触碰 VAD/KWS 时，额外执行：

```bash
cd /root/ameba-river
rg -n "river_voice_kws_request_tensor_dump_next|river_voice_kws_dump_tensor_chunk|river_voice_kws_run_alignment_sample|river_voice_kws_dump_alignment_status" \
  include/river/river_voice_kws.h components/river_voice/river_voice_kws.cc components/river_diag/river_diag_cmd.c
rg -n "river_voice_detector_open|river_voice_detector_process|river_voice_detector_backend_name" \
  include/river/river_voice_detector.h components/river_voice
```

期望：

- KWS dump/alignment API 仍存在。
- VAD public API 仍存在。
- build 成功。

触碰协议层时，额外检查：

```bash
cd /root/ameba-river
rg -n "Authorization|Protocol-Version|Device-Id|Client-Id|type.*hello|audio_params|send_abort|send_audio" \
  components/river_cloud include/river
```

触碰 MCP 时，额外检查：

```bash
cd /root/ameba-river
rg -n "light|fan|curtain|socket|self.audio_speaker.set_volume|set_volume" \
  components include
```

期望：

- live 工具只剩音量。
- 非音量命中只允许存在于历史文档、删除清单或 unsupported 测试。

## 11. 板端验证矩阵

### Boot

步骤：

```text
1. flash 后重启。
2. 观察 boot log。
3. 执行 status dump。
```

通过标准：

- 进入 `idle`。
- KWS init 成功。
- VAD backend 可 dump。
- 未启动旧 provider/dialog/cloud adapter。

### Wake

步骤：

```text
1. idle 状态说唤醒词。
2. 观察 wake event。
3. 观察状态机进入 connecting/listening。
```

通过标准：

- KWS 触发日志包含当前模型/阈值。
- 没有丢失 KWS dump/alignment 能力。
- 状态转换唯一且可追踪。

### Uplink

步骤：

```text
1. 唤醒后说 2~3 秒语音。
2. 观察 encode/send 计数。
3. 观察 ws backpressure。
```

通过标准：

- Opus 60ms packet 持续发送。
- 上行队列没有 silent drop。
- busy/backpressure 有计数。

### Downlink

步骤：

```text
1. 触发服务端回答。
2. 观察 binary audio rx。
3. 观察 decode/playback。
```

通过标准：

- 按 server hello 的音频参数解码。
- 播放可听。
- decode failure、underrun、write failure 为 0 或有明确解释。

### Barge-in

步骤：

```text
1. 服务端播放期间说话。
2. 观察 VAD speech start。
3. 观察本地停止播放和 abort 发送。
```

通过标准：

- 本地播放先停止。
- `abort` 发送一次。
- 状态进入 listening。
- 不出现重复 abort 风暴。

### MCP Volume

步骤：

```text
1. 服务端调用音量设置。
2. 服务端调用一个非音量工具。
```

通过标准：

- 音量设置生效。
- 非音量工具返回 unsupported。
- 没有 light/fan/curtain/socket 实际控制路径。

### Soak

步骤：

```text
1. 连续 30 分钟 idle + 多轮唤醒 + 多轮播放 + 插话。
2. 每 5 分钟 status dump。
```

通过标准：

- heap 无持续下降趋势。
- queue high watermark 可解释。
- ws reconnect 可恢复。
- 没有旧 runtime 状态残留日志。

## 12. 风险控制

主要风险：

- VAD/KWS 旧依赖没有先解耦，导致新主干被旧 runtime 反向污染。
- 协议层照搬了旧 XiaoZhi legacy adapter 的复杂 session/playback runtime。
- 音频链路过早追求 full-duplex，导致首版无法稳定闭环。
- 删除旧文件过早，导致难以定位行为差异。

控制策略：

- 先解耦 VAD/KWS，再切 live CMake。
- 协议层只迁移 wire contract，不迁移旧 runtime truth。
- 首版先做 wake -> listen -> response -> playback 的半双工闭环。
- 删除旧文件分两步：先移出 live CMake，构建和板端验证通过后再清理源文件。
- 每一步保留可观测计数，避免靠体感判断。

## 13. 执行切片

### Step A: 强化 Clean-Slate 计划

状态：

- 已完成到 Step A.xiaozhi-client.2。

目标：

- 把计划从“激进重构”收紧成“只有 VAD/KWS 受保护，其余可推倒”的执行合同。

验证：

```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
```

期望结果：

- `check_codex_harness: all checks passed`

### Step A2: 补齐质量控制计划

目标：

- 把本计划升级成后续实现可直接使用的工程控制文档。

范围：

- `doc/ORVIBO_CLIENT_REARCH_EXECUTION_PLAN_ZH.md`
- `.codex/active_context.md`
- `.codex/changes.md`
- `.codex/verification.md`

完成标准：

- 模块契约明确。
- 构建图控制明确。
- 日志、队列、并发、质量门禁明确。
- 板端验证矩阵明确。

验证：

```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
```

期望结果：

- `check_codex_harness: all checks passed`

### Step A3: 切换未来命名为 Orvibo

状态：

- 当前步骤。

目标：

- 把活跃计划文件、未来模块名和未来 public function/header 命名从 `xiaozhi`/`xz`
  切换为 `orvibo`。

范围：

- `doc/ORVIBO_CLIENT_REARCH_EXECUTION_PLAN_ZH.md`
- `.codex/active_context.md`
- `.codex/active_plans.md`
- `.codex/changes.md`
- `.codex/verification.md`

完成标准：

- active plan 指向 `doc/ORVIBO_CLIENT_REARCH_EXECUTION_PLAN_ZH.md`。
- 新模块契约使用 `orvibo_app / orvibo_state_machine /
  orvibo_audio_service / orvibo_protocol / orvibo_mcp_volume`。
- 未来 public API 命名使用 `river_orvibo_*`。
- `xiaozhi` 只作为外部兼容协议、历史记录或 legacy adapter 名称出现。

验证：

```bash
cd /root/ameba-river
rg -n "ORVIBO_CLIENT_REARCH_EXECUTION_PLAN|Orvibo|orvibo_app|orvibo_state_machine|orvibo_audio_service|orvibo_protocol|orvibo_mcp_volume|river_orvibo|XiaoZhi-compatible" \
  doc/ORVIBO_CLIENT_REARCH_EXECUTION_PLAN_ZH.md \
  .codex/active_context.md \
  .codex/active_plans.md \
  .codex/changes.md
python3 tools/diag/check_codex_harness.py
```

期望结果：

- active plan 和命名规则均已切到 Orvibo。
- harness 检查通过。

### Step B: 解耦受保护的 VAD/KWS

状态：

- 已完成 KWS 对旧 `dialog_runtime / interaction_state` 的直接 include
  解耦。
- VAD public API 未触碰，Silero VAD 仍按原实现保留。
- 后续 `orvibo_audio_service` 应使用
  `river_voice_kws_set_detection_gate()` 显式控制 idle/post-wake 阶段 KWS
  是否接收检测。

目标：

- 让当前 VAD/KWS 能被新 `orvibo_audio_service` 调用，而不依赖旧
  `dialog_runtime / session_coordinator / cloud_adapter`。

范围：

- `components/river_voice/`
- `include/river/river_voice_detector.h`
- `include/river/river_voice_kws.h`
- `components/river_diag/river_diag_cmd.c`

完成标准：

- KWS/VAD 的核心实现和诊断仍在。
- KWS/VAD public API 不再强依赖旧对话运行时。
- KWS parity / tensor dump / alignment replay 能力未删减。

验证：

```bash
cd /root/ameba-river
rg -n "river_dialog_runtime|river_session_coordinator|river_cloud_adapter" \
  components/river_voice include/river/river_voice*.h
rg -n "river_voice_kws_request_tensor_dump_next|river_voice_kws_dump_tensor_chunk|river_voice_kws_run_alignment_sample|river_voice_kws_dump_alignment_status" \
  include/river/river_voice_kws.h components/river_voice/river_voice_kws.cc components/river_diag/river_diag_cmd.c
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

期望结果：

- 第一个 `rg` 不再命中新主干必须依赖的 VAD/KWS 文件。
- 第二个 `rg` 仍命中 KWS dump/alignment API。
- build 成功。

### Step C: 建立新的最小编译主干

目标：

- 新增或替换为 Orvibo 专用 `orvibo_app / orvibo_state_machine /
  orvibo_audio_service / orvibo_protocol` 骨架，并让 live CMake 先切到新主干。

范围：

- `app/app_main.c`
- `components/river_core/`
- `components/river_cloud/`
- `components/river_voice/`
- `components/*/CMakeLists.txt`
- `include/river/`

完成标准：

- 固件能构建并启动到新 `orvibo_app`。
- 旧 `dialog_runtime / session_coordinator / cloud_adapter / provider` 不再进入 live CMake 编译图。
- KWS/VAD 仍进入编译图。
- boot log 出现新设备状态机。

验证：

```bash
cd /root/ameba-river
rg -n "river_cloud_adapter.c|river_dialog_runtime.c|river_session_coordinator.c|river_asr_iflytek_rtasr.c|river_tts_iflytek_ws.c" \
  components/*/CMakeLists.txt
git diff --check
python3 tools/diag/check_codex_harness.py
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

期望结果：

- `rg` 不命中 live CMake 源列表。
- 静态检查通过。
- build 成功。

### Step D: 重建 Orvibo 协议层

目标：

- 按 `xiaozhi-esp32` 的 `Protocol/WebsocketProtocol` wire contract
  实现 Orvibo realtime protocol adapter。

范围：

- `components/river_cloud/`
- `include/river/river_orvibo*.h`

完成标准：

- WebSocket 可连接。
- `hello` 可发送并解析 server hello。
- `listen / abort / mcp / binary audio` 有统一出口。
- 不引入旧 `cloud runtime truth`。

验证：

```bash
cd /root/ameba-river
rg -n "Authorization|Protocol-Version|Device-Id|Client-Id|type.*hello|audio_params|send_abort|send_audio" \
  components/river_cloud include/river
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

期望结果：

- 协议关键字段存在。
- build 成功。
- 上板后可看到 websocket connect 和 server hello。

### Step E: 重建 AudioService 风格音频链路

目标：

- 用当前 VAD/KWS 接入新的音频服务，上下行通过 Opus packet queue 与 protocol 对接。

范围：

- `components/river_voice/`
- `components/river_cloud/`
- `include/river/`

完成标准：

- KWS 驱动 wake event。
- VAD 驱动 speech state / barge-in。
- 上行 PCM 聚合为 60ms Opus。
- 下行 Opus 解码播放。
- 队列状态可诊断。

验证：

```bash
cd /root/ameba-river
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000
```

期望结果：

- build 成功。
- 唤醒后可完成一轮上行和下行播放。
- 播放中说话触发 abort。

### Step F: 收缩 MCP 为 volume-only

目标：

- 只保留 Orvibo MCP 音量控制。

范围：

- `components/river_cloud/`
- `include/river/`

完成标准：

- `self.audio_speaker.set_volume` 可用。
- 非音量工具返回 unsupported。
- live 代码不再暴露 light/fan/curtain/socket 控制。

验证：

```bash
cd /root/ameba-river
rg -n "light|fan|curtain|socket|self.audio_speaker.set_volume|set_volume" \
  components include
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

期望结果：

- 非音量工具只出现在历史文档、删除候选或 unsupported 测试。
- build 成功。

### Step G: 删除旧主干残留

目标：

- 清理不再进入新主干的旧文件、头文件和诊断入口，避免后续误用。

范围：

- `components/river_core/`
- `components/river_cloud/`
- `include/river/`
- `components/river_diag/`

完成标准：

- 旧 provider/dialog/cloud adapter/session coordinator/split ASR-TTS/legacy xiaozhi 源文件已从 live tree 删除。
- 旧 public 头文件已删除且不再被 include。
- Kconfig/prj.conf 不再暴露已删除 backend。
- README/build/active context 指向 Orvibo 主干。
- VAD/KWS/preproc/AEC/BF 保护区仍在源码树和 live CMake 中。

验证：

```bash
cd /root/ameba-river
rg -n "iflytek|asr_provider|tts_provider|river_cloud_adapter|dialog_runtime|session_coordinator" \
  app components include CMakeLists.txt Kconfig prj.conf
git diff --check
python3 tools/diag/check_codex_harness.py
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

期望结果：

- 只剩历史文档、删除清单或明确 unsupported 测试命中。
- 静态检查和 build 成功。

### Step H: 对齐 XiaoZhi-Compatible 接入与语音运行时

目标：

- 补齐烧录后直连 XiaoZhi-compatible 服务器所需的 access、鉴权、WebSocket hello/listen/abort、TTS 下行和最小 MCP。
- 保持内部命名和运行时 ownership 为 Orvibo。
- 不替换当前 VAD/KWS/AEC/BF。

已完成：

- `Step H.xiaozhi-client.1`：
  - Orvibo access 层负责 OTA/config、device/client identity、websocket url/token/version 应用。
  - WebSocket handshake 带 `Authorization`、`Protocol-Version`、`Device-Id`、`Client-Id`。
  - open 后发送 hello 并等待 server hello。
  - MCP 只暴露 `self.get_device_status` 和 `self.audio_speaker.set_volume`。
- `Step H.xiaozhi-client.2`：
  - `activation.code` 只作为用户绑定提示。
  - 只有存在 `activation.challenge` 才进入 `/activate` 轮询。
  - Wi-Fi ready 后立即刷新 OTA/config，未 ready 时周期重试。
- `Step H.xiaozhi-client.3`：
  - TTS start 前 reset decoder/清残留 playback。
  - 下行二进制音频只在 Orvibo `speaking` 状态解码播放。
  - TTS stop 后受限等待 playback drain，再恢复 listening；超时也强制停止 playback。
  - playback 诊断输出 buffer occupancy 与 drain 成功/超时计数。
- `Step H.xiaozhi-client.4`：
  - `river_orvibo_protocol_send_audio()` 改为非阻塞入队，避免 app/audio 链路被 SDK WebSocket 发送队列瞬时 busy 影响。
  - 新增 `orvibo_uplink` sender task，负责实际 `ws_sendBinary()`、短重试和发送失败统计。
  - uplink frame 使用 `session_epoch` 防止 close/reopen 竞态下旧 session 音频泄漏到新 session。
  - app/protocol status 输出 `uplink_enq`、队列深度、drop/retry/fail 计数。

验证：

```bash
cd /root/ameba-river
rg -n "PREPARE_TTS_PLAYBACK|WAIT_PLAYBACK_IDLE|drop downlink audio outside speaking|river_playback_service_wait_idle|drain_count|buffered_bytes|RIVER_ORVIBO_UPLINK_QUEUE_DEPTH|orvibo_uplink|session_epoch|uplink_enq" \
  include components
git diff --check
python3 tools/diag/check_codex_harness.py
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

期望结果：

- TTS/downlink/playback drain 和 uplink queue/session-epoch 关键路径存在。
- 静态检查、harness 检查和 SDK build 成功。

## 14. 下一步

Step H 已完成接入、激活、hello/listen/abort/TTS 下行、最小 MCP、TTS 播放边界硬化和 uplink 发送背压保护，并通过 `/root/ameba-rtos` 构建验证。下一步进入板端实测和剩余运行时质量收敛：优先验证烧录后 Wi-Fi/OTA/绑定/WebSocket/hello/TTS/MCP volume-only 全链路日志，再继续处理下行播放背压、协议错误恢复和板端诊断可观测性。
