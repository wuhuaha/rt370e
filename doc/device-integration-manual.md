# 端侧接入手册：Orivbo Home AI Phase 1

日期：2026-05-30

## 当前可用服务

当前常驻实例：

```text
Public HTTP Base URL: http://101.33.235.154:8081
OTA URL:              http://101.33.235.154:8081/xiaozhi/ota/
WebSocket URL:        ws://101.33.235.154:8081/xiaozhi/v1/
```

健康检查：

```bash
curl http://101.33.235.154:8081/healthz
curl http://101.33.235.154:8081/readyz
curl http://101.33.235.154:8081/healthz/runtime
```

当前 Phase 1 能力：

- xiaozhi OTA。
- xiaozhi WebSocket 接入。
- 上行 raw Opus 语音流接收。
- Opus -> PCM16LE。
- ASR：共享研究项目 FunASR 服务。
- M1：暂时 mock，固定响应 `收到`。
- TTS：共享研究项目 Qwen TTS 服务。
- PCM16LE -> Opus，下行二进制音频回传。

当前限制：

- 暂不做鉴权，OTA token 为空。
- 暂不执行真实设备控制。
- M1 只返回 mock 文本 `收到`。
- 当前公开地址是 `ws://`；如果端侧强制要求 `wss://`，见“WSS 接入”。

## 接入总流程

```text
device
  -> GET/POST /xiaozhi/ota/
  -> read websocket.url
  -> connect ws://.../xiaozhi/v1/
  -> send hello
  -> receive server hello
  -> if server hello features.server_wake_confirm=true:
       send wake candidate JSON
       send candidate binary raw Opus frames
       wait wake.accepted / wake.rejected / wake.uncertain
       only continue command audio after wake.accepted
  -> else:
       send listen detect/start
       send binary raw Opus frames
       send listen stop
  -> receive stt event
  -> receive tts sentence_start
  -> receive llm event
  -> receive binary Opus audio frames
  -> receive tts stop
```

## HTTP Headers

推荐端侧在 OTA 和 WebSocket handshake 中携带：

```text
Device-Id: <device unique id>
Client-Id: <client id, usually same as device id>
Protocol-Version: 1
Authorization: Bearer <any value>
```

当前服务不校验 Authorization。`Device-Id` 和 `Client-Id` 会进入会话日志和 ASR 请求。

## OTA

请求：

```http
GET /xiaozhi/ota/ HTTP/1.1
Host: 101.33.235.154:8081
Device-Id: xiaozhi-device-001
Client-Id: xiaozhi-device-001
Protocol-Version: 1
```

也支持 `POST /xiaozhi/ota/`。

响应示例：

```json
{
  "server_time": {
    "timestamp": 1780122632001,
    "timezone_offset": 480
  },
  "firmware": {
    "version": "0.0.0",
    "url": ""
  },
  "websocket": {
    "url": "ws://101.33.235.154:8081/xiaozhi/v1/",
    "token": ""
  }
}
```

端侧应使用 `websocket.url` 建立连接。`websocket.token` 当前为空。

## WebSocket

路径：

```text
ws://101.33.235.154:8081/xiaozhi/v1/
```

服务端支持的消息类型：

- `hello`
- `listen`
- `abort`
- `mcp`，当前只返回 not implemented 状态

WebSocket text frame 使用 JSON。音频使用 WebSocket binary frame。

## Hello

端侧连接成功后先发送 `hello`：

```json
{
  "type": "hello",
  "version": 1,
  "transport": "websocket",
  "features": {
    "mcp": true
  },
  "audio_params": {
    "format": "opus",
    "sample_rate": 16000,
    "channels": 1,
    "frame_duration": 60
  }
}
```

服务端响应：

```json
{
  "type": "hello",
  "version": 1,
  "transport": "websocket",
  "session_id": "session_1780122708_000003",
  "audio_params": {
    "format": "opus",
    "sample_rate": 16000,
    "channels": 1,
    "frame_duration": 60
  },
  "features": {
    "mcp": true,
    "listen_modes": ["auto", "manual", "realtime"],
    "server_endpointing_modes": ["auto", "realtime"],
    "m1_enabled": true,
    "m1_mode": "mock",
    "asr_audio_implemented": true,
    "tts_audio_implemented": true
  }
}
```

端侧后续消息可以带上 `session_id`。当前服务不强制校验该字段，但建议带上。

## 音频格式

标准推荐：

```text
format: opus
sample_rate: 16000
channels: 1
frame_duration: 60
```

要求：

- WebSocket binary frame 中发送 raw Opus packet。
- 不要发送 Ogg Opus。
- 不要发送 WebM/Matroska。
- 不要发送 RTP header。
- 每个 binary frame 对应一个 raw Opus frame。

服务端内部会将 raw Opus 解码为：

```text
codec: pcm16le
sample_rate_hz: 16000
channels: 1
```

调试模式也支持 `pcm16le`：

```json
{
  "audio_params": {
    "format": "pcm16le",
    "sample_rate": 16000,
    "channels": 1,
    "frame_duration": 60
  }
}
```

真实小智设备建议使用 `opus`。

## Listen

开始收音：

```json
{
  "type": "listen",
  "session_id": "session_1780122708_000003",
  "state": "start",
  "mode": "manual"
}
```

服务端响应：

```json
{
  "type": "session.update",
  "session_id": "session_1780122708_000003",
  "payload": {
    "state": "listening",
    "mode": "manual"
  }
}
```

然后端侧连续发送 binary raw Opus frames。

停止收音并触发 ASR：

```json
{
  "type": "listen",
  "session_id": "session_1780122708_000003",
  "state": "stop"
}
```

也可以用于纯文本调试：

```json
{
  "type": "listen",
  "session_id": "session_1780122708_000003",
  "state": "detect",
  "text": "打开客厅灯"
}
```

随后发送：

```json
{
  "type": "listen",
  "session_id": "session_1780122708_000003",
  "state": "stop"
}
```

纯文本调试会绕过音频 ASR。

## 服务端下行事件

ASR 完成后：

```json
{
  "type": "stt",
  "session_id": "session_1780122708_000003",
  "turn_id": "turn_1780122708...",
  "text": "打开客厅灯。"
}
```

TTS 开始：

```json
{
  "type": "tts",
  "session_id": "session_1780122708_000003",
  "turn_id": "turn_1780122708...",
  "state": "sentence_start",
  "text": "收到"
}
```

LLM/M1 文本：

```json
{
  "type": "llm",
  "session_id": "session_1780122708_000003",
  "turn_id": "turn_1780122708...",
  "text": "收到",
  "emotion": "neutral"
}
```

随后服务端发送多个 WebSocket binary frames，下行格式与 hello 协商一致。标准场景为 raw Opus frames。

结束：

```json
{
  "type": "tts",
  "session_id": "session_1780122708_000003",
  "turn_id": "turn_1780122708...",
  "state": "stop"
}
```

端侧播放策略：

- 收到 `tts sentence_start` 后可以准备播放。
- 收到 binary frame 后按 Opus frame 解码播放。
- 收到 `tts stop` 后结束本轮播放。

## Abort

端侧打断：

```json
{
  "type": "abort",
  "session_id": "session_1780122708_000003",
  "reason": "user_abort"
}
```

服务端响应：

```json
{
  "type": "tts",
  "session_id": "session_1780122708_000003",
  "state": "stop"
}
```

## 错误事件

错误响应形态：

```json
{
  "type": "error",
  "session_id": "session_1780122708_000003",
  "turn_id": "turn_1780122708...",
  "code": "asr_failed",
  "message": "..."
}
```

常见错误：

| code | 含义 | 端侧处理 |
|---|---|---|
| `invalid_json` | JSON text frame 无法解析 | 修正消息格式 |
| `unsupported_message` | 不支持的消息类型 | 检查 type |
| `invalid_listen_state` | listen state 非法 | 使用 start/detect/stop |
| `audio_decode_failed` | Opus 解码失败 | 检查是否 raw Opus、采样率、帧长 |
| `asr_failed` | ASR 失败或无文本 | 可重试本轮语音 |
| `tts_failed` | TTS 失败 | 可只展示文本 |
| `tts_audio_encode_failed` | 下行音频编码失败 | 可只展示文本 |

## WSS 接入

当前公开常驻服务是：

```text
ws://101.33.235.154:8081/xiaozhi/v1/
```

如果端侧或固件强制要求 `wss://`，有两种方式：

1. 推荐：Nginx/Ingress 终止 TLS。
   - 外部暴露：`https://voice.example.com`
   - 反代到：`http://127.0.0.1:8081`
   - 设置服务环境变量：

```text
HOME_AI_PUBLIC_BASE_URL=https://voice.example.com
```

OTA 会返回：

```text
wss://voice.example.com/xiaozhi/v1/
```

2. Go 服务直连 TLS。
   - 设置：

```text
HOME_AI_TLS_CERT_FILE=/path/server.crt
HOME_AI_TLS_KEY_FILE=/path/server.key
HOME_AI_PUBLIC_BASE_URL=https://voice.example.com:8443
HOME_AI_HTTP_ADDR=0.0.0.0:8443
```

生产设备应使用可信证书。自签证书只适合本地 E2E。

## 标准小智客户端对接方式

这一节面向标准小智客户端或小智 ESP32 固件。

### 1. 配置 OTA 地址

将小智客户端的 OTA/服务发现地址配置为：

```text
http://101.33.235.154:8081/xiaozhi/ota/
```

如果固件配置项区分 HTTP endpoint 和 WebSocket endpoint，优先配置 OTA；让客户端从 OTA 响应中读取 `websocket.url`。

### 2. 确认 OTA 响应

客户端启动后应先请求 OTA，并拿到：

```json
{
  "websocket": {
    "url": "ws://101.33.235.154:8081/xiaozhi/v1/",
    "token": ""
  }
}
```

`token` 为空是预期行为。当前阶段不做鉴权。

### 3. WebSocket 握手

标准小智客户端应连接：

```text
ws://101.33.235.154:8081/xiaozhi/v1/
```

推荐握手 headers：

```text
Device-Id: <设备唯一 ID>
Client-Id: <设备唯一 ID>
Protocol-Version: 1
Authorization: Bearer <可为空或任意调试 token>
```

### 4. Hello 音频参数

标准小智客户端应声明 raw Opus：

```json
{
  "type": "hello",
  "version": 1,
  "transport": "websocket",
  "audio_params": {
    "format": "opus",
    "sample_rate": 16000,
    "channels": 1,
    "frame_duration": 60
  }
}
```

如果客户端默认是 16kHz mono 60ms raw Opus，则无需改音频编码层。

### 5. 上行音频

服务端声明 `features.server_wake_confirm=true`、`wake_candidate_upload=true` 且
`wake_upload_modes` 包含 `candidate` 时，端侧先发送候选唤醒事件：

```json
{
  "type": "wake",
  "state": "candidate",
  "wake_id": "wake-unique-id",
  "trigger_source": "local_kws",
  "keyword_hint": "你好小智",
  "client_confidence": 0.8
}
```

随后发送候选 raw Opus binary frames，并等待服务端返回
`wake.accepted` / `wake.rejected` / `wake.uncertain`。只有 `accepted` 后才继续上传用户指令音频；
`rejected` 应停止上传并回到 idle；`uncertain` 只短暂继续上传候选音频。

旧服务端未声明上述能力时，标准小智客户端在 `listen start` 后发送 raw Opus binary frames：

```text
listen start JSON
binary opus frame 1
binary opus frame 2
...
listen stop JSON
```

注意：

- 每个 WebSocket binary message 是一个 raw Opus packet。
- 不要把多个 Opus packet 拼在一个 binary message 里。
- 不要加 Ogg/WebM/RTP 容器。
- 不要把 JSON 和音频放在同一个 frame。

### 6. 下行播放

客户端收到：

```text
tts sentence_start JSON
llm JSON
binary opus frame 1
binary opus frame 2
...
tts stop JSON
```

播放 binary frames 即可。下行也是 raw Opus packet。

### 7. 标准客户端验收

标准小智客户端接入成功的最低验收：

- OTA 成功返回 websocket URL。
- WebSocket 连接成功。
- hello 后收到 server hello。
- 说一句“打开客厅灯”后，服务端日志出现 `stt` 文本。
- 客户端收到 `llm.text = 收到`。
- 客户端能播放下行音频。
- 本轮最终收到 `tts stop`。

## 调试命令

检查服务：

```bash
curl http://101.33.235.154:8081/healthz
curl http://101.33.235.154:8081/readyz
curl http://101.33.235.154:8081/healthz/runtime
```

检查 OTA：

```bash
curl http://101.33.235.154:8081/xiaozhi/ota/
```

运行端到端测试：

```bash
BASE_URL=http://101.33.235.154:8081 \
AUDIO_FORMAT=opus \
INPUT_MODE=tts \
INPUT_TEXT=打开客厅灯 \
EXPECT_STT=打开客厅灯 \
EXPECT_REPLY=收到 \
TIMEOUT_S=60 \
HOME_AI_TTS_VOICE_IDENTITY_VERSION=20260508a \
HOME_AI_TTS_VOICE_REFERENCE_AUDIO=/etc/agent-server/voices/qwen/seed_companion_20260508a/ref.wav \
HOME_AI_TTS_VOICE_REFERENCE_TEXT=已为你打开空调，26度三挡风。 \
scripts/e2e-phase1.sh
```

查看服务日志：

```bash
tail -f .runtime/logs/home-ai-server-8081.log
```

查看进程：

```bash
cat .runtime/tmp/home-ai-server-8081.pid
ps -fp "$(cat .runtime/tmp/home-ai-server-8081.pid)"
```

停止服务：

```bash
kill "$(cat .runtime/tmp/home-ai-server-8081.pid)"
```

## 端侧排障

### OTA 可访问，但 WebSocket 连接失败

检查：

- 是否连接 OTA 返回的 `websocket.url`。
- 防火墙是否放通 `8081`。
- 客户端是否强制要求 `wss://`。

如果强制 WSS，需要加 TLS 反代或启用 Go 直连 TLS。

### WebSocket hello 失败

检查 hello JSON：

- `type` 必须是 `hello`。
- `audio_params.format` 推荐 `opus`。
- `sample_rate` 必须是 `16000`。
- `channels` 必须是 `1`。
- `frame_duration` 推荐 `60`。

### ASR 失败

检查：

- 上行是否 raw Opus。
- 是否误发 Ogg/WebM/RTP。
- 每个 binary frame 是否只包含一个 Opus packet。
- 新 wake 协议下是否先发送 `type=wake,state=candidate`，并且只在 `wake.accepted` 后继续上传用户指令音频。
- 旧 listen 协议下是否在 `listen start` 后发送音频。
- 是否发送了 `listen stop`。

### 有文本但没声音

检查：

- 是否收到 binary frames。
- 是否把 binary frames 当 raw Opus 解码。
- 是否等到了 `tts stop`。
- 客户端播放器采样率是否按 Opus 解码后的实际输出处理。

### 收到 `收到` 是正常的吗

正常。Phase 1 的 M1 还没有接完整规则引擎和真实设备控制，当前固定 mock 回复是：

```text
收到
```

后续接入外部 M1 后，才会返回真实语义解析和控制结果。
