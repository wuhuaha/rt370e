# Orvibo 语音客户端 Clean-Slate 重构计划

Status: active
Last Updated: 2026-05-08
Branch: `xiaozhi-client`
SDK Baseline: `/root/ameba-rtos`
External Protocol Baseline: XiaoZhi-compatible realtime server protocol

Latest Verified Slice:

- `Step H.xiaozhi-client.36` 已增补 Orvibo 唤醒词误触诊断日志：
  - 静态复核 `capture -> preproc -> enhanced mono -> VAD -> river_voice_kws_submit_frame(...)` 链路后，当前没有直接证据表明 Orvibo 重构破坏了 KWS 推理输入路径或 detection gate 基本策略。
  - 现阶段更强的嫌疑来自当前实验性 KWS 部署参数本身偏激进：`CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=9517`、`CONFIG_RIVER_KWS_TRIGGER_HOLD_FRAMES=1`，比 `river_voice_kws.cc` 内部保守 fallback 默认值更容易因单次高分或短 gate 内峰值而触发。
  - `wakeword hit:` 现已补充 `gate_best_pm`、`thresh_pm`、`weak_pm`、`gate_infer`、`hits` 与 `mode`，用于判断触发来自正常阈值还是 gate fallback。
  - `kws gate close:` 现已补充 `thresh_pm`、`weak_pm` 与 `latched_trigger=yes|no`，用于判断每个 VAD gate 的最佳得分与是否已触发过。
  - `wake bridge:` 现已在 KWS 事件真正进入 Orvibo audio-service 上抛桥接时记录 `mode` 与 VAD 快照，便于排除上层重复消费。
  - 当前 `/root/ameba-rtos` 完整 build 已通过；下一步需要板端抓取新日志来判断是模型/阈值问题还是上层事件问题。
  - 本地 VAD、唤醒词/KWS 推理实现、tensor dump、alignment replay、board/local parity、AEC/BF 保持不变。
- `Step H.xiaozhi-client.35` 已收敛 Orvibo volume-only MCP 的参数校验与错误语义：
  - 再次对照 `~/xiaozhi-esp32` 后确认，参考端对 `tools/call` 的无效请求形态会返回 JSON-RPC `error.message`，而不是把这些请求错误包装为 `result.isError=true`。
  - 再次对照 `~/py-xiaozhi` 与 `~/xiaozhi-esp32-server` 后确认，当前服务端 `device_mcp` 调用方优先按 JSON-RPC error 处理工具调用失败，成功路径才消费 `result.content[0].text`。
  - 当前 Orvibo 分支此前对 `self.audio_speaker.set_volume` 会先把 `valueint` 强转为 `uint8_t`，这使负值参数存在回绕成高音量的真实行为风险。
  - 现已在 MCP 层先用有符号整数完成 `0..100` 校验，再写入本地音量；`missing name`、`missing volume`、越界音量和未知工具统一改为 JSON-RPC error 返回。
  - 协议层也已显式静默容忍可选应用层 `pong` 文本消息，避免未来若启用该心跳时引入无效日志噪声。
  - 当前 `/root/ameba-rtos` 完整 build 已通过；板侧运行日志仍受当前历史纯 `0x00` UART 会话状态阻塞。
  - 本地 VAD、唤醒词/KWS、tensor dump、alignment replay、board/local parity、AEC/BF 保持不变。
- `Step H.xiaozhi-client.34` 已补齐 Orvibo 基于 OTA 下发 WebSocket token 的重连刷新闭环：
  - 对照 `~/xiaozhi-esp32-server` 后确认，OTA 接口会按 `client_id|device_id|timestamp` 为 websocket 下发带时间戳的鉴权 token，WebSocket 服务端则按 `expire_seconds` 校验其有效期。
  - 当前 Orvibo 分支此前只在 `access_not_ready` 或 access-not-ready 周期刷新时重新拉取 OTA；如果设备已经 `ready=true`，但后续长时间运行后 token 过期，开声道路径会持续使用旧 token 重连，形成“ready 但鉴权永远失败”的隐性死路。
  - 现已在 app 开声道路径加入 OTA-aware 补偿：首次 `river_orvibo_protocol_open_audio_channel()` 失败后，如果当前 websocket 配置来自 OTA，则立即刷新一次 access 并同步再试一次开声道。
  - 静态 fallback WebSocket 配置不会触发这条自动刷新补偿，避免干扰本地免 OTA 的固定部署。
  - 当前 `/root/ameba-rtos` 完整 build 已通过；板侧运行日志仍受当前历史纯 `0x00` UART 会话状态阻塞。
  - 本地 VAD、唤醒词/KWS、tensor dump、alignment replay、board/local parity、AEC/BF 保持不变。
- `Step H.xiaozhi-client.33` 已收敛 Orvibo 非法 `server hello` 的快速失败路径：
  - 再次审计 `river_orvibo_wait_server_hello()` 与 `river_orvibo_parse_server_hello()` 后发现，当服务端 `hello` 缺失 `transport` 或返回非 `websocket` 值时，协议层虽然会立刻发出错误事件，但握手等待循环仍会继续空等完整 `hello` 超时窗口。
  - 这会让已经明确不兼容的服务端响应在 open 路径上表现成慢超时，既放大首连时延，也掩盖真实根因为 `hello_transport_invalid`。
  - 现已新增 `server_hello_rejected` 标志，非法 `transport` 会在解析阶段先置位，再保留现有错误事件与 `last_error`。
  - `river_orvibo_wait_server_hello()` 发现该标志后会立即返回 `RIVER_ERR_IO`，每次重新打开音频信道时也会同步复位 `server_hello_received` / `server_hello_rejected`。
  - 当前 `/root/ameba-rtos` 完整 build 已通过；板侧运行日志仍受当前历史纯 `0x00` UART 会话状态阻塞。
  - 本地 VAD、唤醒词/KWS、tensor dump、alignment replay、board/local parity、AEC/BF 保持不变。
- `Step H.xiaozhi-client.32` 已收敛 Orvibo 远端关断后的本地 transport/context 清理路径：
  - 审计 `river_orvibo_ws_close_cb()`、Orvibo app 事件桥接和 state machine 后发现，远端主动关闭 WebSocket 时虽然会投递 `AUDIO_CHANNEL_CLOSED` 并把状态收回 `idle`，但状态动作本身不会再显式执行 `river_orvibo_protocol_close_audio_channel()`。
  - 这会让已经 closed 的 `wsclient` context 残留到下一次重连前，继续被主循环持有并进入空 transport 轮询，增加后续连接失败/重连问题的观测噪声。
  - 现已让 `CONNECTING` / `LISTENING` / `SPEAKING` / `RECOVERING` 在处理 `AUDIO_CHANNEL_CLOSED` 时统一附带 `RIVER_ORVIBO_ACTION_CLOSE_AUDIO_CHANNEL`，保证远端关断/timeout 关断后立即释放本地 websocket context。
  - 当前 `/root/ameba-rtos` 完整 build 已通过；板侧运行日志仍受当前历史纯 `0x00` UART 会话状态阻塞。
  - 本地 VAD、唤醒词/KWS、tensor dump、alignment replay、board/local parity、AEC/BF 保持不变。
- `Step H.xiaozhi-client.31` 已收敛 Orvibo `hello` 发送失败后的半开 WebSocket 清理路径：
  - 审计 `river_orvibo_protocol_open_audio_channel()` 后发现，`ws_connect_url()` 成功后若 `river_orvibo_send_hello()` 失败，当前代码会直接返回，但不会立即关闭新建的 websocket context。
  - 这会把 transport/session 的实际回收时机推迟到上层 recover path，留下一个短暂的半开会话窗口，也让 `session_epoch` / `sessions_opened` 的失败路径观测不够干净。
  - 现已在 `hello_send_failed` 分支中立即调用 `river_orvibo_close_context()`，与 `server_hello` 超时/失败后的清理路径保持一致。
  - 当前 `/root/ameba-rtos` 完整 build 已通过；板侧运行日志仍受当前历史纯 `0x00` UART 会话状态阻塞。
  - 本地 VAD、唤醒词/KWS、tensor dump、alignment replay、board/local parity、AEC/BF 保持不变。
- `Step H.xiaozhi-client.30` 已修正 Orvibo TTS 下行声道参数传递：
  - 审计 Orvibo 协议层、app 层和音频服务层后发现，protocol 已从 server hello 保存 `audio_params.channels`，但向 app 投递下行音频消息时被固定成 `1U`。
  - 这会在 XiaoZhi-compatible server 将 TTS 配置为非单声道时，用错误的 channels 打开 Opus decoder；当前默认 server 配置虽然是 `24000Hz/1ch/60ms`，但端侧不能静默丢失该 wire contract 字段。
  - `river_orvibo_protocol_event_t` 现已携带 `channels`，server hello 与 binary audio event 均传递 `server_channels`。
  - app 下行音频消息改为使用 protocol event 的 channels，只有字段缺省时才回退到 `1`。
  - audio service 现支持 `1ch/2ch` 下行 Opus；`2ch` 解码后显式下混为 mono，再进入当前重采样、双声道播放和 AEC reference 输出路径。
  - audio diag/status 输出已扩展为 `rate=server_rate/server_channels->playback_rate`，便于板端确认实际 decoder 参数。
  - 当前 `/root/ameba-rtos` 完整 build 已通过；板侧运行日志仍受当前历史纯 `0x00` UART 会话状态阻塞。
  - 本地 VAD、唤醒词/KWS、tensor dump、alignment replay、board/local parity、AEC/BF 保持不变。
- `Step H.xiaozhi-client.29` 已补齐 Orvibo 可选 XiaoZhi-compatible v2 激活 HMAC 路径：
  - 再次对照 `~/xiaozhi-esp32` 后确认，存在 serial number 的 ESP32 参考端会发送 `Activation-Version: 2`、`Serial-Number` header，并在 `/ota/activate` body 中提交根对象 `algorithm` / `serial_number` / `challenge` / `hmac`。
  - 对照 `~/py-xiaozhi` 后确认，HMAC 计算口径是对 challenge 使用配置中的原始字符串 key 做 HMAC-SHA256，并输出小写 hex。
  - 对照 `~/xiaozhi-esp32-server` 后确认，当前 manager-api `/ota/activate` 仍由 `Device-Id` 绑定状态决定 200/202，未强制校验 v2 payload，因此 Orvibo 默认仍保持已验证的 v1/no-serial 激活流。
  - 新增 `CONFIG_RIVER_ORVIBO_ACTIVATION_SERIAL_NUMBER` 与 `CONFIG_RIVER_ORVIBO_ACTIVATION_HMAC_KEY`；只有两者同时配置时才切到 v2/HMAC。
  - 默认配置为空时继续发送 `Activation-Version: 1` 与 `{}` body，不发送 `Serial-Number`，以保持当前本地 manager-api 绑定流兼容性。
  - `river_orvibo_access` 会保存 OTA 返回的 `activation.challenge`，状态/日志输出 `act_v`、`hmac=configured|none` 与 `serial`，但不输出 HMAC key。
  - 当前 `/root/ameba-rtos` 完整 build 已通过；板侧运行日志仍受当前历史纯 `0x00` UART 会话状态阻塞。
  - 本地 VAD、唤醒词/KWS、tensor dump、alignment replay、board/local parity、AEC/BF 保持不变。
- `Step H.xiaozhi-client.28` 已收紧 Orvibo WebSocket 协议版本输入范围：
  - 对照 `~/xiaozhi-esp32-server` 后确认，当前直接 WebSocket 入站二进制消息会作为 raw Opus 进入 ASR 队列；只有 `?from=mqtt_gateway` 路径才解析 MQTT gateway 的 16 字节头。
  - 当前服务端 OTA 返回 websocket `url/token`，不返回 `websocket.version`，因此直连 XiaoZhi-compatible websocket 的安全默认仍应保持 raw/v1。
  - OTA `websocket.version` 现在只接受 `1..3`；超出范围的数值会被忽略并打印 warning。
  - `river_orvibo_protocol_set_config()` 也统一拒绝不支持的版本，并在写入全局配置前完成校验，避免 header/hello version 与实际音频 framing 分裂。
  - 本地 VAD、唤醒词/KWS、tensor dump、alignment replay、board/local parity、AEC/BF 保持不变。
- `Step H.xiaozhi-client.27` 已对齐 Orvibo WebSocket 默认握手子协议到 XiaoZhi 参考端行为：
  - 再次对照 `~/xiaozhi-esp32` 与 `~/py-xiaozhi` 后确认，参考端 WebSocket 连接设置 `Authorization`、`Protocol-Version`、`Device-Id`、`Client-Id`，但不请求 `Sec-WebSocket-Protocol`。
  - 对照 `~/xiaozhi-esp32-server` 后确认，当前服务端 `websockets.serve(...)` 未配置 subprotocol，不要求客户端提供 subprotocol。
  - Ameba SDK 原生握手在未显式设置 protocol 时会注入 `Sec-WebSocket-Protocol: chat, superchat`，与参考端默认行为不一致。
  - 现已将 `RIVER_ORVIBO_WS_SUBPROTOCOL` 默认值改为空字符串，并新增 `river_ws_handshake.c`，通过 `--wrap=ws_client_handshake` 在项目侧覆盖 SDK 默认握手。
  - 默认情况下 Orvibo 握手完全省略 `Sec-WebSocket-Protocol`；如果后续显式配置 `RIVER_ORVIBO_WS_SUBPROTOCOL`，仍保留发送对应 header 的能力。
  - `build.ninja` 与最终 AP image symbol 已确认 `river_ws_handshake.o`、`-Wl,--wrap=ws_client_handshake`、`__wrap_ws_client_handshake` 生效；当前 `/root/ameba-rtos` 完整 build 已通过。
  - 板侧运行日志仍受当前历史纯 `0x00` UART 会话状态阻塞。
  - 本地 VAD、唤醒词/KWS、tensor dump、alignment replay、board/local parity、AEC/BF 保持不变。
- `Step H.xiaozhi-client.26` 已修正 Orvibo client hello 的服务端 AEC 声明语义：
  - 再次对照 `~/xiaozhi-esp32` 后确认，参考端 WebSocket/MQTT hello 只在 `CONFIG_USE_SERVER_AEC` 下发送 `features.aec=true`。
  - `xiaozhi-esp32` 明确禁止 `CONFIG_USE_DEVICE_AEC` 与 `CONFIG_USE_SERVER_AEC` 同时启用，说明 `features.aec` 表达的是“请求服务端 AEC”，不是端侧本地 AEC/BF/native-ref 能力声明。
  - `~/py-xiaozhi` 与 `~/xiaozhi-esp32-server` 未发现把本地 AEC 能力映射为 client hello `features.aec` 的逻辑。
  - Orvibo hello 现在不再按本地 voice profile 自动声明 `features.aec`，日志显式输出 `server_aec=no`；MCP 仍只声明 volume-only 所需能力。
  - H.21 的 `listen_start.mode` 选择逻辑保留：仍按当前 voice profile 的 `AEC/NATIVE_CAPTURE_REF` 能力位在 `realtime` 与 `auto` 间选择。
  - 当前 `/root/ameba-rtos` 路径下的静态检查、harness 检查和完整 build 已通过；板侧运行日志仍受当前历史纯 `0x00` UART 会话状态阻塞。
  - 本地 VAD、唤醒词/KWS、tensor dump、alignment replay、board/local parity、AEC/BF 保持不变。
- `Step H.xiaozhi-client.25` 已收敛 Orvibo OTA/MCP 自描述元数据链路：
  - 再次对照 `~/xiaozhi-esp32`、`~/py-xiaozhi` 与 `~/xiaozhi-esp32-server` 后确认，当前服务端 OTA handler 会优先读取请求头中的 `device-model` / `application-version` / `firmware-version` 等字段，只有缺失时才回退到 body 的 `board.type` / `application.version`。
  - 当前 Orvibo 分支此前仍残留 `application.version=0.1.0`、`board.type=wifi`、`User-Agent=orvibo-rtl8730e/0.1.0`、MCP `serverInfo.version=0.1.0` 等占位值，会导致服务端长期误判本实验分支的真实版本和型号。
  - 现已新增统一的 `river_orvibo_build_info` helper，集中导出 `app name/version`、`compile_time`、`board name/type`、`chip model` 和 `User-Agent`。
  - OTA 请求头已补齐 `Device-Model`、`Application-Version`、`Firmware-Version` 等服务端优先使用的字段；OTA body 也同步统一 `model`、`application.*`、`board.*`、`chip_model_name` 与 `application.compile_time`。
  - MCP initialize 的 `serverInfo` 也已切到同一元数据来源，不再返回历史占位版本。
  - 当前 `/root/ameba-rtos` 路径下的静态检查、harness 检查和完整 build 已通过；板侧运行日志仍受当前历史纯 `0x00` UART 会话状态阻塞。
  - 本地 VAD、唤醒词/KWS、tensor dump、alignment replay、board/local parity、AEC/BF 保持不变。
- `Step H.xiaozhi-client.24` 已修复 Orvibo 静态 fallback WebSocket 对免鉴权 XiaoZhi-compatible server 的错误阻断：
  - 再次对照 `~/xiaozhi-esp32-server` 后确认，服务端只会在 `auth.enabled=true` 时要求 `Authorization`；关闭 auth 的本地 websocket 部署允许空 token。
  - 当前 Orvibo 分支此前把静态 fallback 可用性收紧为“`ws_url` 与 `ws_token` 都非空”，会错误拦住已显式配置 URL、但本就不需要 token 的本地免鉴权部署。
  - 当前默认 fallback URL 也已从历史硬编码地址收敛为空字符串，避免在 OTA/config 缺失时静默连到非目标服务器。
  - 现在只要显式配置了 `RIVER_ORVIBO_WS_URL` 就可把静态 websocket 视为可用；只有 auth-enabled 部署才需要额外配置 token。
  - 当前 `/root/ameba-rtos` 路径下的静态检查、harness 检查和完整 build 已通过；板侧运行日志仍受当前历史纯 `0x00` UART 会话状态阻塞。
  - 本地 VAD、唤醒词/KWS、tensor dump、alignment replay、board/local parity、AEC/BF 保持不变。
- `Step H.xiaozhi-client.23` 已对齐 Orvibo WebSocket 默认协议版本到 XiaoZhi 主链基线：
  - 再次对照 `~/xiaozhi-esp32`、`~/py-xiaozhi` 和 `~/xiaozhi-esp32-server` 后确认，当前 XiaoZhi WebSocket 主链默认协议版本是 `1`。
  - ESP32 与 Python 参考客户端在 version `1` 下都会直接发送原始 Opus 二进制帧；本地 Python 服务端 websocket 路径也会把入站 bytes 直接送入 Opus/VAD 处理，没有明显的 v2/v3 解包层。
  - 当前 Orvibo 分支此前默认 `protocol_version=3`，在 OTA/config 未显式返回 `websocket.version` 的部署上，会把 uplink Opus 额外包成 v3 frame，存在首轮语音交互直接失配的风险。
  - 现在默认值已改为 `1`，保证“直连 XiaoZhi-compatible websocket”这一基线场景默认走 raw Opus framing；如果某部署明确通过 OTA/config 返回 `version=2/3`，仍允许覆盖。
  - 当前 `/root/ameba-rtos` 路径下的静态检查、harness 检查和完整 build 已通过；板侧运行日志仍受当前历史纯 `0x00` UART 会话状态阻塞。
  - 本地 VAD、唤醒词/KWS、tensor dump、alignment replay、board/local parity、AEC/BF 保持不变。
- `Step H.xiaozhi-client.22` 已对齐 `listen detect.text` 与 XiaoZhi 服务端默认唤醒词语义：
  - 对照 `~/xiaozhi-esp32`、`~/py-xiaozhi` 和 `~/xiaozhi-esp32-server` 后确认，服务端会把 `listen detect.text` 当作“唤醒词或直接文本输入”处理；默认 `wakeup_words` 配置包含 `你好小智`，不包含当前本地 KWS 固定文本 `小欧管家`。
  - Orvibo app 现已把云侧 `listen detect.text` 收敛为 Orvibo-owned、可配置、默认兼容 XiaoZhi 服务端的标准唤醒词 `RIVER_ORVIBO_SERVER_WAKE_TEXT`，默认值为 `你好小智`。
  - 本地 KWS 文本、日志、tensor dump、alignment replay、board/local parity、VAD、AEC/BF 均保持不变。
  - 当前 `/root/ameba-rtos` 路径下的静态检查、harness 检查和完整 build 已通过；板侧运行日志仍受当前历史纯 `0x00` UART 会话状态阻塞。
- `Step H.xiaozhi-client.21` 已对齐 client hello / listen mode 语义与当前 Orvibo 音频能力：
  - 对照 `~/xiaozhi-esp32`、`~/py-xiaozhi` 和 `~/xiaozhi-esp32-server` 后确认，参考端不会把 `listen_start.mode` 固定写死为 `auto`；默认模式会根据双工/AEC 能力在 `auto` 与 `realtime` 之间切换。
  - 当前 Orvibo 实现此前一直固定发送 `mode=auto`，会让支持实时双工的 profile 走错本地 listening 策略。
  - Orvibo app 已改为按当前 voice profile 的 `AEC/NATIVE_CAPTURE_REF` 能力位选择 `listen_start.mode`。
  - 本步曾把同一能力位映射为 Orvibo hello `features.aec=true`；H.26 重新对照参考端后已确认该字段表达服务端 AEC 请求，并已移除该声明。
  - 当前 `/root/ameba-rtos` 路径下的静态检查、harness 检查、完整 build 和 reflash 已通过。
  - 但本轮板侧运行态再次落入历史上的纯 `0x00` UART 会话状态，导致 `client hello features: ...` 和 `listen start mode=...` 还未拿到实板日志闭环。
  - VAD、唤醒词/KWS、AEC/BF 算法实现、tensor dump、alignment replay、board/local parity、MCP volume-only 不变。
- `Step H.xiaozhi-client.20` 已通过实板验证，收敛 TTS 后 WebSocket close 与本地控制帧发送竞态：
  - H.19 实板验证已证明 server hello、listening uplink、TTS 下行播放可达，但一次 TTS 后服务端 close 会与本地 `tts_stop -> listen_start` 动作交错。
  - 对照 `~/xiaozhi-esp32`、`~/py-xiaozhi` 和 `~/xiaozhi-esp32-server` 后确认：服务端 `close_after_chat` 场景可能在 TTS stop 后 close，客户端不应把关闭态 channel 上的 `listen_start` 当作 recoverable protocol error。
  - Orvibo app 已为 wake/listen/abort 控制帧增加 channel-open guard；关闭态或发送竞态关闭时记录 `protocol_ctrl skip` 并投递 `AUDIO_CHANNEL_CLOSED` 收敛状态。
  - `/dev/ttyUSB0` 实板复验确认：server hello、TTS 播放、服务端 close、`skip protocol control: action=listen_start state=listening reason=channel_closed` 和 `protocol_ctrl=3/0 skip=1` 均符合预期；旧的 `listen_start:-4` recoverable-error 路径未复现。
  - VAD、唤醒词/KWS、KWS tensor dump、alignment replay、board/local parity、AEC/BF、MCP volume-only 不变。
- `Step H.xiaozhi-client.19` 已修复实板首连后的 Orvibo audio task 栈溢出：
  - `/dev/ttyUSB0` 实板烧录已通过，Flash tool 使用 `/root/ameba-rtos` 与项目本地 `RTL8730E_NOR.rdev`。
  - 板端日志已确认真实 MAC 身份、WebSocket 连接和 XiaoZhi-compatible server hello：`24000Hz/1ch/60ms`。
  - 进入 listening 后暴露 `STACK OVERFLOW - TaskName(orvibo_audio)`。
  - `orvibo_audio` task stack 已从 18 KB 提升到 32 KB，并在 audio open/status 日志输出 `task_stack=`。
  - 重新 build/flash/monitor 已通过：`mode=listening` 下 `enc=` 持续增长，未再次出现 `STACK OVERFLOW - TaskName(orvibo_audio)`。
  - `river orvibo status` 已确认 `task_stack=32768`、真实设备身份、OTA-derived WebSocket URL、24 kHz 服务端音频、24 kHz 到 48 kHz 播放适配和 MCP volume-only 工具。
  - 后续单独处理一个新暴露的运行时问题：一次 TTS 后 WebSocket close 会让 app 回到 `idle`，需要补齐下一轮 listening channel 的重建策略。
- `Step H.xiaozhi-client.18` 已把 XiaoZhi-compatible 服务器文本语义接回 Orvibo 事件面：
  - `tts sentence_start`、`stt`、`llm` 从只记日志升级为显式 Orvibo protocol/app 事件。
  - protocol status 会显示最新 server text 与 `tts_sentence_rx` / `stt_rx` / `llm_rx` 计数。
  - app status 会显示 `last_server_text_kind` / `last_server_text` / `last_server_text_detail`，便于板端确认服务端返回的文本和情绪。
  - 未触碰 VAD、KWS、KWS tensor dump、alignment replay、board/local parity、AEC/BF、MCP volume-only、OTA/WS 连接和 TTS 复入边界。
  - 最新 `/root/ameba-rtos` SDK build 已通过。
- `Step H.xiaozhi-client.17` 已收敛 Orvibo 分支 SDK 默认入口：
  - active executable defaults 在未显式设置 `AMEBA_SDK_ROOT` 时统一指向 `/root/ameba-rtos`。
  - 覆盖 `components/river_cloud/CMakeLists.txt`、`tools/river_flash.py`、`tools/generate_rdev.py` 和 `env.bat`。
  - harness 新增 active SDK default 检查，防止后续静默回退 `/root/ameba-rtos-1.2`。
  - 协议、VAD、KWS、KWS tensor dump、alignment replay、board/local parity、AEC/BF 运行时逻辑不变。
  - 最新 `/root/ameba-rtos` SDK build 已通过。
- `Step H.xiaozhi-client.16` 已硬化 Orvibo access 真实设备身份 ready 判定：
  - access `ready` 现在要求 `websocket_configured` 与有效 STA MAC 身份同时成立。
  - `river_orvibo_access_refresh()` 在 OTA/config 前刷新真实 STA MAC 生成的 `Device-Id` / `Client-Id`。
  - 若 Wi-Fi/netif 仍未提供有效 STA MAC，则记录 `sta_mac_unavailable` 并阻止 OTA/WS 鉴权使用全零身份。
  - access status 输出 `identity=ready|waiting_mac`，便于板端确认烧录后使用的真实身份。
  - VAD、KWS、KWS tensor dump、alignment replay、board/local parity、AEC/BF 保护区不变。
  - 最新 `/root/ameba-rtos` SDK build 已通过。
- `Step H.xiaozhi-client.15` 已补齐 Orvibo WebSocket 入站超时恢复：
  - 协议层记录最近一次入站 WebSocket text/binary 消息时间。
  - `river_orvibo_protocol_poll()` 对 120 秒无入站消息的僵尸通道主动关闭，并投递 `AUDIO_CHANNEL_CLOSED`。
  - protocol status 输出 `timeout=` 和 `incoming_age=age/limit`，便于板端确认通道活性。
  - VAD、KWS、KWS tensor dump、alignment replay、board/local parity、AEC/BF 保护区不变。
- 最新 `/root/ameba-rtos` SDK build 已通过。
- `Step H.xiaozhi-client.14` 已补齐 Orvibo listening 复入与唤醒打断闭环：
  - `tts_stop` 后先 drain playback，再恢复 listening，并重新发送 `listen start`，确保多轮对话服务端继续收音。
  - speaking 期间 VAD speech-start barge-in 发送无 reason 的 generic `abort`，随后重新 `listen start`。
  - wake word barge-in 使用独立 `ABORT_WAKE_WORD` action，发送 `reason=wake_word_detected`。
  - speaking + barge-in enabled 时放开当前 KWS detection gate，让现有唤醒词实现可参与 TTS 期间打断。
  - VAD、KWS 模型/阈值/tensor dump/alignment replay/board-local parity、AEC/BF 保护区不变。
- 最新 `/root/ameba-rtos` SDK build 已通过。
- `Step H.xiaozhi-client.13` 已补齐 Orvibo TTS 下行播放采样率适配：
  - 服务端 Opus 仍按 hello 给定的 `sample_rate` / `frame_duration` 解码。
  - 解码后的 mono PCM 会在 Orvibo audio service 内转换到选定的播放采样率后再扩成 stereo。
  - 当前 XiaoZhi-compatible 服务端 `24000Hz/60ms` TTS 会进入 `48000Hz/60ms` 播放路径，因为 Ameba 输出 policy 不列 24 kHz。
  - audio diag/status 现在暴露 `rs=converted/bypass/fail` 和 `rate=server->playback`。
  - VAD、KWS、KWS tensor dump、alignment replay、board/local parity、AEC/BF 保护区不变。
- 最新 `/root/ameba-rtos` SDK build 已通过。
- `Step H.xiaozhi-client.12` 首次加入 Orvibo WebSocket subprotocol 配置并避免依赖 SDK 隐式 `chat, superchat` fallback；该默认策略已由 H.27 覆盖为“默认无子协议”。
- 现阶段 `RIVER_ORVIBO_WS_SUBPROTOCOL` 默认空字符串，默认握手省略 `Sec-WebSocket-Protocol`；显式配置时仍可发送对应子协议。
- connect 日志和 protocol status 输出 `ws_subprotocol=-`，便于板端确认当前默认无子协议握手。
- 最新 `/root/ameba-rtos` SDK build 已通过。
- `Step H.xiaozhi-client.5` 已隔离 Orvibo app 控制事件队列与音频事件队列。
- state/connect/listen/abort 等控制消息不再与 uplink/downlink audio 共享队列容量。
- app 主循环先处理 control queue，再按预算处理 audio queue；audio queue 满时 drop-oldest 保实时，控制事件不被音频突发淹没。
- app status 输出 `ctl_q`、`aud_q`、control/audio posted/fail 和 `aud_drop_oldest`。
- 最新 `/root/ameba-rtos` SDK build 已通过。
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
listening -> listening            on WAKE_DETECTED + wake_abort + listen_start
speaking -> listening             on USER_SPEECH_STARTED + generic_abort + listen_start
speaking -> listening             on WAKE_DETECTED + wake_abort + listen_start
speaking -> listening             on SERVER_TTS_FINISHED + playback_drain + listen_start
any -> network_wait               on NETWORK_LOST
any -> recovering                 on recoverable error
recovering -> idle                on recovery done
any -> error                      on fatal error
```

状态不变量：

- `idle`：KWS 应启用；Orvibo audio channel 可关闭。
- `connecting`：KWS 可暂停；正在打开 Orvibo audio channel。
- `listening`：VAD 和上行编码开启；下行播放未占用 speaker。
- `speaking`：下行播放开启；VAD 可用于 generic barge-in；barge-in enabled 时当前 KWS 可用于 wake-word abort。
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
- `Step H.xiaozhi-client.5`：
  - app 内部拆为 control queue 与 audio queue，控制消息不再被音频包挤占。
  - 主循环优先 drain control queue，再按固定预算处理 audio queue。
  - audio queue 满时 drop-oldest，保障实时性并避免控制消息丢失。
  - app status 输出 control/audio queue 深度和 post/drop 计数。
- `Step H.xiaozhi-client.6`：
  - wake detected、listen start、listen stop、abort speaking 等协议控制帧不再忽略发送失败。
  - 自动状态机路径上的关键控制帧失败会记录 `last_error` / `protocol_control_fail` 并进入 recoverable recovery。
  - 诊断命令只记录失败，不主动触发恢复，避免手动测试命令改变主运行态。
  - app status 输出 `protocol_ctrl=ok/fail`，用于板端确认控制帧发送可靠性。
- `Step H.xiaozhi-client.7`：
  - transport mutex 改为 recursive mutex，支持 `ws_poll()` 回调内同步发送 MCP volume 回复而不自锁。
  - hello 等待与常规 poll 都通过同一 transport lock 调用 `ws_poll()`，避免 uplink sender task 与 SDK `wsclient` 并发访问。
  - WebSocket close callback 记录 `transport_closed` / `close_events` 并继续投递 `AUDIO_CHANNEL_CLOSED`。
  - protocol status 输出 `poll` / `close_evt`，用于板端确认 poll 活性和远端断开次数。
- `Step H.xiaozhi-client.8`：
  - `open_audio_channel` 失败后进入 1s 起步、30s 封顶的指数退避，并保留一次 pending wake retry。
  - 退避期间新的 open action 被抑制并走 recoverable recovery，避免服务器不可达时 tight-loop。
  - pending retry 只在 `idle + Wi-Fi connected + access ready` 时到期触发。
  - 成功打开通道或 Wi-Fi lost 会清空 retry/backoff 状态。
  - app status 输出 `orvibo connect: ok/fail retry/streak/next/posted/suppressed`。
- `Step H.xiaozhi-client.9`：
  - TTS playback buffer 从 12 帧提升到 16 帧，吸收常见服务端下行 burst。
  - 每个 downlink Opus 包解码后、写入 playback 前检查 playback SDK buffer occupancy。
  - 当下一帧会超过 85% 高水位时丢弃该下行帧并返回 busy，避免 app 任务被 AudioTrack 写入阻塞。
  - audio diag/status 输出 `bp_drop`、`bp_high`、`buf=buffered/size`。
- `Step H.xiaozhi-client.10`：
  - 新增 `river_orvibo_app_request_access_refresh()`，诊断入口仍通过 Orvibo app 控制队列执行。
  - app 消息 `RIVER_ORVIBO_APP_MSG_ACCESS_REFRESH` 调用既有 access refresh 路径并标记 reason=`diag_refresh`。
  - `river orvibo refresh` 可在烧录后手动触发 OTA/config 或 activation polling 刷新，便于绑定完成后立即验证 access ready。
  - 诊断 help/usage 同步加入 `refresh`。
- `Step H.xiaozhi-client.11`：
  - 主机侧 OTA/config 探测确认当前服务返回 `wss://api.tenclass.net/xiaozhi/v1/`，WebSocket hello 返回 `opus/24000Hz/1ch/60ms`。
  - protocol binary payload、app audio message payload、audio service Opus packet 上限从 768B 扩到 1536B。
  - app 层 oversized uplink/downlink packet 不再静默丢弃，改为计数并打印日志。
  - protocol/app/audio 诊断输出 `payload_max`、`audio_max`、`packet_max` 和 `oversize=up/down`。
- `Step H.xiaozhi-client.12`：
  - 首次新增 `RIVER_ORVIBO_WS_SUBPROTOCOL`，用于避免依赖 Ameba SDK 隐式 `chat, superchat` fallback。
  - 该步的历史默认 `chat` 已由 H.27 覆盖为默认空字符串。
  - 现阶段默认握手省略 `Sec-WebSocket-Protocol`；显式配置 subprotocol 时仍可通过同一 Orvibo config 路径发送。
  - connect/status 日志输出 `ws_subprotocol=-` 表示默认无子协议。
- `Step H.xiaozhi-client.13`：
  - 下行 Opus 按 server hello 解码后，本地 PCM 会适配到 Ameba 播放支持的采样率。
  - 当前服务端 `24000Hz/60ms` TTS 会转为 `48000Hz/60ms` 播放/reference 帧。
  - playback 活跃期间如果下行格式变化，会停止并用新格式重启 `orvibo_tts` stream。
  - audio diag/status 输出 `rs=converted/bypass/fail` 和 `rate=server->playback`。
- `Step H.xiaozhi-client.14`：
  - `tts_stop` 后重新发送 `listen start`，使下一轮对话不会停在本地 listening 但服务端未重新收音的状态。
  - VAD speech-start barge-in 使用无 reason 的 generic `abort`；wake-word barge-in 使用 `reason=wake_word_detected`。
  - speaking + barge-in enabled 时放开当前 KWS detection gate，并把 runtime interaction 标记为 `barge_in_listening`。
  - 离开 speaking 会关闭 barge-in gate；受保护的 VAD/KWS/AEC/BF 实现不变。
- `Step H.xiaozhi-client.15`：
  - 协议层记录最近一次入站 WebSocket 消息时间，对齐参考客户端 120 秒 channel timeout。
  - `river_orvibo_protocol_poll()` 会关闭超时通道并投递 `AUDIO_CHANNEL_CLOSED`，避免本地停在僵尸 listening/speaking。
  - protocol status 输出 `timeout=` 与 `incoming_age=age/limit`。
- `Step H.xiaozhi-client.16`：
  - access ready 增加真实 STA MAC 身份门控，避免启动早期全零 MAC 进入 OTA/WS 鉴权。
  - OTA/config 刷新前重新生成 `Device-Id` / `Client-Id`；无有效 STA MAC 时记录 `sta_mac_unavailable` 并返回 busy。
  - access status 输出 `identity=ready|waiting_mac`。
- `Step H.xiaozhi-client.17`：
  - active project CMake/tools/env 默认 SDK 收敛到 `/root/ameba-rtos`，保留 `AMEBA_SDK_ROOT` 覆盖。
  - harness 增加 active SDK default 检查，避免 Orvibo 主线可复现性回退到旧 SDK。
- `Step H.xiaozhi-client.19`：
  - 实板验证已证明 OTA/WS/hello 可以到达 XiaoZhi-compatible 服务器。
  - 首次 listening 暴露 `orvibo_audio` 任务栈不足，已将 audio task stack 提高到 32 KB。
  - audio open/status 输出实际 `task_stack=`，便于后续板端复验。
  - 复验已通过：rebuild、reflash、monitor 后未复现 `orvibo_audio` 栈溢出，`enc=` 计数持续增长，并完成一次服务端 TTS 下行和播放。
- `Step H.xiaozhi-client.20`：
  - 对齐参考端/服务端通道语义：TTS 后服务端 close 是合法 channel 生命周期事件，不应把本地后续控制帧发送失败升级为协议恢复错误。
  - wake/listen/abort 控制帧发送前检查 channel-open；关闭态或发送竞态关闭按 `protocol_ctrl skip` 统计，并投递 `AUDIO_CHANNEL_CLOSED` 收敛状态。
  - app status 暴露 `protocol_ctrl=ok/fail skip=n`。
  - 实板验证已通过：TTS 后服务端 close 触发 `skip protocol control`，状态回到 `idle`，不再出现旧的 `listen_start:-4` recoverable error。
- `Step H.xiaozhi-client.21`：
  - 对齐默认 listening mode 语义：按当前 voice profile 的 `AEC/NATIVE_CAPTURE_REF` 能力，在 `realtime` 与 `auto` 间选择 `listen_start.mode`，不再固定写死 `auto`。
  - 该步中按本地 profile 声明 hello `features.aec` 的做法已由 H.26 修正；当前分支不请求服务端 AEC。
- `Step H.xiaozhi-client.26`：
  - 对齐参考端服务端 AEC 语义：`features.aec` 只应表达 server-side AEC 请求，不应由本地 device-side AEC/BF/native-ref 能力自动触发。
  - Orvibo hello 不再发送 `features.aec`，日志输出 `server_aec=no`。
  - 保留本地 voice profile 驱动的 `listen_start.mode`，不回退 H.21 的 listening-mode 修正。
- `Step H.xiaozhi-client.27`：
  - 对齐参考端 WebSocket 握手：默认不发送 `Sec-WebSocket-Protocol`。
  - 新增项目侧 `river_ws_handshake.c`，通过 `--wrap=ws_client_handshake` 覆盖 SDK 会注入 `chat, superchat` 的默认握手。
  - `RIVER_ORVIBO_WS_SUBPROTOCOL` 默认空字符串；显式配置时仍保留发送具体 subprotocol 的能力。
  - wrapper 保留 Host/Upgrade/Connection/Sec-WebSocket-Key/Sec-WebSocket-Version/custom headers，并按有效长度处理 SDK setter 写入的字段。
- `Step H.xiaozhi-client.28`：
  - OTA `websocket.version` 只接受 `1..3`，异常数值不再写入 Orvibo protocol config。
  - `river_orvibo_protocol_set_config()` 在更新全局配置前校验版本，避免不支持的 version 造成 hello/header 与 audio framing 不一致。
  - 继续保持 direct websocket 默认 raw/v1，以匹配当前 `xiaozhi-esp32-server` 的直接入站 bytes 处理路径。
- `Step H.xiaozhi-client.25`：
  - 新增统一 `river_orvibo_build_info` helper，集中导出 Orvibo app/version/compile_time/board/chip/user-agent，避免 OTA/MCP/诊断多处散落硬编码。
  - OTA 请求头补齐 `Device-Model`、`Model`、`Application-Version`、`App-Version`、`Firmware-Version`、`Device-Version`，对齐 `xiaozhi-esp32-server` OTA handler 的优先读取路径。
  - OTA body 统一输出顶层 `model`、`application.name/version/compile_time`、`board.name/type`、`chip_model_name`。
  - MCP initialize 的 `serverInfo.name/version` 改为复用同一 build info 来源。
  - access init/status 日志补充 app/version/board/user-agent，便于板侧核对 OTA 上报身份。

验证：

```bash
cd /root/ameba-river
rg -n "PREPARE_TTS_PLAYBACK|WAIT_PLAYBACK_IDLE|drop downlink audio outside speaking|river_playback_service_wait_idle|drain_count|buffered_bytes|RIVER_ORVIBO_UPLINK_QUEUE_DEPTH|orvibo_uplink|session_epoch|uplink_enq|RIVER_ORVIBO_APP_CONTROL_QUEUE_DEPTH|control_queue|audio_queue|aud_drop_oldest|record_protocol_control|protocol_ctrl|protocol_control_skip|skip protocol control|send_channel_closed|recursive_take|poll_once|close_evt|CONNECT_BACKOFF|check_connect_retry|orvibo connect|DOWNLINK_BUFFER_HIGH_WATER|bp_drop|ACCESS_REFRESH|diag_refresh|orvibo <status|connect|refresh|PACKET_MAX|PAYLOAD_MAX|AUDIO_PACKET_MAX|oversize|payload_max|audio_max|RIVER_ORVIBO_WS_SUBPROTOCOL|ws_subprotocol|downlink_playback_pcm|resample_mono|downlink playback rate|ABORT_WAKE_WORD|abort_wake_word|send_abort_speaking\\(NULL\\)|BARGE_IN_LISTENING|orvibo_barge_in|CHANNEL_TIMEOUT|last_incoming_ms|incoming_age|channel_timeout|identity_ready|sta_mac_unavailable|access identity refreshed|RIVER_ORVIBO_AUDIO_TASK_STACK|task_stack=|/root/ameba-rtos|AMEBA_SDK_ROOT" \
  include components
rg -n "river_orvibo_app_listen_mode|send_start_listening\\(|features, \\\"aec\\\"|RIVER_VOICE_CAPABILITY_AEC|RIVER_VOICE_CAPABILITY_NATIVE_CAPTURE_REF" \
  components/river_core/river_orvibo_app.c \
  components/river_cloud/river_orvibo_protocol.c
rg -n 'ameba-rtos-1\.2|/root/ameba-rtos|AMEBA_SDK_ROOT|RIVER_SDK_ROOT' \
  env.sh env.bat components/river_cloud/CMakeLists.txt \
  tools/river_flash.py tools/generate_rdev.py tools/diag/check_codex_harness.py
git diff --check
python3 tools/diag/check_codex_harness.py
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

期望结果：

- TTS/downlink/playback drain、uplink queue/session-epoch、app control/audio queue、protocol control failure/skip recovery、WebSocket poll/send serialization、connect retry/backoff、TTS playback backpressure、manual access refresh、Opus payload envelope/oversize diagnostics、默认无 WebSocket subprotocol 握手、downlink playback sample-rate adapter、listening 复入/abort reason/speaking KWS gate、channel timeout、access identity gate、active SDK default、audio task stack，以及按当前 voice profile 选择 `listen_start.mode`、hello 不误报 server AEC 的关键路径存在。
- 静态检查、harness 检查和 SDK build 成功。

## 14. 下一步

Step H 已完成接入、激活、hello/listen/abort/TTS 下行、最小 MCP、TTS 播放边界硬化、uplink 发送背压保护、app 控制/音频队列隔离、协议控制帧失败恢复/skip 收敛、WebSocket poll/send 串行化、连接失败 backoff、TTS 播放背压防护、手动 access refresh 诊断入口、24k/60ms TTS payload envelope 扩容、默认无 WebSocket subprotocol 握手、24k->48k 播放采样率适配、listening 复入/唤醒词打断闭环、WebSocket 入站超时恢复、access 真实身份门控、active SDK default 收敛、OTA/MCP 自描述元数据统一收敛，以及 hello 服务端 AEC 声明语义修正，并已通过实板推进到 XiaoZhi-compatible server hello、listening uplink、TTS 下行播放、H.19 栈修复验证和 H.20 TTS-close 竞态收敛验证。下一步优先完成板侧 OTA 身份、默认无 subprotocol 握手和 `server_aec=no` hello 日志复验，并继续收敛下行采样率协商与 v2/v3 包络兼容的剩余静态风险。
