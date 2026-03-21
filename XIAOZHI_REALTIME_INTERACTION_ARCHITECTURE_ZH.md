# 小智实时交互接入架构设计

## 1. 文档目的

本文档定义 `ameba-river` 在 `xiaozhi` 分支上的实时交互接入方案。核心目标不是“把 ESP32 应用搬到 Ameba”，而是：

- 保留当前已经稳定的 `river` 运行时
- 把小智服务器接入为新的实时会话层
- 复用现有 `PlaybackService / ReferenceService / InteractionState / barge-in`
- 保留当前 `Iflytek RTASR + Iflytek WS TTS` 作为回退基线

## 2. 设计结论

### 2.1 小智不是“另一个 ASR provider”

小智服务端协议是一个单会话、多事件的实时对话通道，天然承载：

- `stt`
- `llm`
- `tts`
- `mcp`
- `abort`
- binary audio

因此它在本项目中的定位应当是：

```text
XiaoZhi = Realtime Conversation Transport
```

而不是：

```text
XiaoZhi = another ASR provider + another TTS provider
```

### 2.2 保持运行时稳定优先

当前 `river` 已经拥有稳定的：

- `PlaybackService`
- `ReferenceService`
- `InteractionState`
- `frame-ring / frame-pool`
- 播放时插话打断链路

小智接入必须复用这些能力，而不是重写一套新的设备端应用框架。

## 3. 目标体验

目标交互体验：

- 单条 WebSocket 长连接完成整轮对话
- 设备端上行使用 `Opus 16k/1ch/60ms`
- 服务端下行按 `hello.audio_params` 动态适配采样率和帧长
- 播放中说话时，先本地打断播放，再向服务端发 `abort`
- 服务端返回的 `stt / llm / tts / mcp` 事件都能映射到当前运行时
- 保留当前 `Iflytek` 链路作为回退与对照

## 4. 协议映射

### 4.1 握手

设备端建立 WebSocket 连接后，应先发送 `hello`：

- headers:
  - `Authorization`
  - `Protocol-Version`
  - `Device-Id`
  - `Client-Id`
- body:
  - `type=hello`
  - `version`
  - `transport=websocket`
  - `features.mcp=true`
  - `audio_params={format=opus,sample_rate=16000,channels=1,frame_duration=60}`

服务端返回 `hello` 后，设备端记录：

- `session_id`
- 服务端下行 `audio_params`
- 能力字段与版本

### 4.2 文本消息

设备端上行：

- `listen start`
- `listen stop`
- `listen detect`
- `abort`
- `mcp`

服务端下行：

- `stt`
- `llm`
- `tts start`
- `tts stop`
- `tts sentence_start`
- `mcp`

### 4.3 二进制音频

- 上行：设备将单声道 16k PCM 编码为 `Opus`
- 下行：服务端以 `hello` 返回的音频参数下发二进制音频
- 默认以较新协议版本为优先，保留兼容旧版 header 的能力

## 5. 运行时模块拆分

建议模块边界如下：

### 5.1 `river_xiaozhi_ws`

职责：

- 建立 WebSocket 会话
- 发送 `hello / listen / abort / mcp`
- 接收文本事件和二进制音频
- 维护 `session_id / protocol version / audio params / stats`

### 5.2 `river_opus_codec`

职责：

- 上行 PCM -> Opus
- 下行 Opus -> PCM
- 按服务端参数动态适配解码侧

### 5.3 `river_xiaozhi_mcp_bridge`

职责：

- 把服务端 `mcp` 请求映射为本地设备控制
- 复用现有 `river_online_control` 能力

### 5.4 `river_cloud_adapter`

职责：

- 继续作为云层统一门面
- 根据后端选择调用：
  - 现有 `Iflytek split path`
  - 新增 `XiaoZhi realtime path`

## 6. 与现有运行时的映射

### 6.1 上行音频

```text
capture
-> fixed_dsb
-> mono PCM frame
-> Opus encode
-> XiaoZhi binary uplink
```

### 6.2 下行音频

```text
XiaoZhi binary downlink
-> Opus decode
-> PlaybackService
-> speaker
```

### 6.3 交互状态

- `listen start` 对应本地进入实时会话监听窗口
- `stt partial/final` 继续映射为现有 ASR 结果事件
- `tts start/stop/sentence_start` 驱动当前播放期状态
- `abort` 由本地 `barge-in` 或会话控制显式触发

### 6.4 插话

播放中用户说话时，应遵循：

1. 本地 `PlaybackService` 先停止/打断当前播放
2. `InteractionState` 切入 `barge_in_listening`
3. 向小智发送 `abort`
4. 保持同会话或按协议要求切换到新的监听阶段

## 7. MCP 设备控制映射

第一批映射直接复用当前设备控制面：

- `light.on/off/toggle`
- `fan.on/off/toggle`
- `curtain.on/off/toggle`
- `socket.on/off/toggle`

这意味着小智服务端不只是输出文本或语音，还能直接驱动板端现有控制能力。

## 8. 兼容与回退策略

必须保留以下边界：

- 小智接入失败时，当前 `Iflytek` 拆分链路仍可独立构建与验证
- 新增模块应尽量通过后端选择或 capability 开关接入
- 不允许让小智实现直接耦入底层捕获/播放基础设施

## 9. 验收标准

完成本架构的最低验收标准：

- `hello` 握手稳定
- `listen / abort / stt / tts / mcp` 文本消息收发正常
- Opus 上下行音频稳定
- 播放期插话可以先本地打断，再向服务端同步 `abort`
- 现有回退路径仍然可用
