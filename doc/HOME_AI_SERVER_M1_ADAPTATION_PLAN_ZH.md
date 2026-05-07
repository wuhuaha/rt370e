# home_ai_server M1 端侧适配计划

Status: active
Last Updated: 2026-05-07
Branch: `home-ai`

## 1. 当前背景

- 当前主问题：`home-ai` 分支要从旧 `agent-server`/XiaoZhi 兼容路径切到 `/root/home_ai_server` 的 M1 服务端。
- 已知稳定基线：当前端侧已有 WebSocket JSON control + binary PCM 下行播放状态机，但默认协议仍偏旧 `agent-server.realtime.v0` / `rtos-ws-v0`，并保留 preview、server endpoint、segment mark 等高级协作假设。
- 当前默认 SDK：`/root/ameba-rtos`。
- 与本计划强相关的现有文档：
  - `/root/home_ai_server/doc/api/rtos-device-protocol-current.md`
  - `/root/home_ai_server/doc/api/rtos-agent-server-migration-guide.md`
  - `doc/VOICE_RUNTIME_REARCHITECTURE_EXECUTION_PLAN_ZH.md`

## 2. 目标

- 端侧默认协议与 `home_ai_server` M1 current contract 对齐：
  - `protocol_version=rtos-smart-home-v1`
  - `subprotocol=agent-server.smart-home.realtime.v1`
  - `turn_mode=client_wakeup_client_commit`
- 保留 discovery 覆盖能力，并能解析 `features` / `voice_collaboration` 的 M1 协商结果。
- `session.start` 默认发送本项目简化 payload，同时保留服务端兼容字段可读性。
- 上行继续走 binary PCM16LE，端侧本地 endpoint 后显式发送 `audio.in.commit`。
- 下行继续复用现有播放状态机，并把 `audio.out.meta.duration_ms` 映射为端侧 `expected_duration_ms`。
- playback ACK 收缩到 `started_completed_v1`：只发送 `audio.out.started`、`audio.out.completed`、`audio.out.cleared`，不再发送 `audio.out.mark`。

## 3. 非目标

- 不重命名整个 `xiaozhi` 目录和 public API。
- 不引入新的服务端代码到本仓库。
- 不实现 M2/M3、多轮同连接复用、preview/server endpoint 或细粒度 segment mark。
- 不移除 wakeword board/local 比对和调试基础设施。

## 4. 约束 / Guardrails

- 项目侧代码、文档和记录留在本仓库；服务端只作为只读协议来源。
- 默认 SDK 与验证命令使用 `/root/ameba-rtos`。
- 改动优先收敛在 `include/river/river_xiaozhi_credentials.h`、`components/river_cloud/river_xiaozhi_ws*` 和必要的 Codex harness 文档。
- 每个可验证切片更新 `.codex/changes.md`、`.codex/verification.md` 并用中文提交。

## 5. 已知事实

- `home_ai_server` discovery endpoint 为 `GET /v1/realtime`，WebSocket endpoint 为 `/v1/realtime/ws`。
- 服务端当前 subprotocol 为 `agent-server.smart-home.realtime.v1`。
- 服务端当前 `turn_mode=client_wakeup_client_commit`，`preview_events=false`，`server_endpointing=false`。
- 服务端当前 playback ACK mode 为 `started_completed_v1`，不支持 `audio.out.mark`。
- 服务端 `audio.out.meta` 当前使用 `duration_ms` 表示段音频时长，不是旧端侧优先读取的 `expected_duration_ms`。

## 6. 风险与未知项

- 板端默认连接当前部署的 `home_ai_server`：
  `ws://101.33.235.154:8081/v1/realtime/ws`；若网络环境不同，可通过
  `river xiaozhi set <url>` 覆盖。
- `home_ai_server` M1 当前是单 turn 连接；端侧现有 follow-up 多轮窗口需要按“每轮重新 session.start/commit”方式观察，若服务端拒绝同连接二轮，需要后续独立切片改为每次唤醒新建连接。
- 服务端当前默认可能 text-only 或依赖本地 TTS cache；板端联调需要确认服务端已启用可下发 PCM 的 TTS/cache 路径。

## 7. 执行切片

### Step A: M1 协议最小切换

Status: implemented, waiting board validation

目标：

- 让端侧默认能完成 `home_ai_server` M1 握手、`session.start`、binary audio、`audio.in.commit`、短音频下行和最小 playback ACK。

范围：

- `include/river/river_xiaozhi_credentials.h`
- `components/river_cloud/river_xiaozhi_ws.c`
- `components/river_cloud/river_xiaozhi_ws_bootstrap_discovery.inc`
- `components/river_cloud/river_xiaozhi_ws_message_handlers.inc`
- `components/river_cloud/river_cloud_xiaozhi_playback_terminal_ack.inc`

完成标准：

- 默认协议常量切到 `rtos-smart-home-v1` / `agent-server.smart-home.realtime.v1`。
- discovery 可从 `features.server_endpointing=false` 和 `voice_collaboration.playback_ack.mode=started_completed_v1` 得到正确协商结果。
- `session.start` 发送 M1 简化 payload，并声明 `preview_events=false`、`server_endpointing=false`、`playback_ack=true`。
- `audio.out.meta.duration_ms` 能驱动现有播放完成逻辑。
- 端侧不再队列或发送 `audio.out.mark`。

验证：

```bash
cd /root/ameba-river
rg -n "rtos-smart-home-v1|agent-server.smart-home.realtime.v1|started_completed_v1|server_endpointing|duration_ms|audio.out.mark" \
  include/river/river_xiaozhi_credentials.h \
  components/river_cloud/river_xiaozhi_ws.c \
  components/river_cloud/river_xiaozhi_ws_bootstrap_discovery.inc \
  components/river_cloud/river_xiaozhi_ws_message_handlers.inc \
  components/river_cloud/river_cloud_xiaozhi_playback_terminal_ack.inc
git diff --check
python3 tools/diag/check_codex_harness.py
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

期望结果：

- `rg` 能看到 M1 协议常量、M1 capabilities、`duration_ms` 兼容解析和 mark ACK 禁用路径。
- 静态检查通过。
- 最新 SDK build 成功。

实现记录：

- 默认 wire profile 已切到 `rtos-smart-home-v1` /
  `agent-server.smart-home.realtime.v1`，MCP 默认关闭。
- discovery 已解析 M1 顶层 `features`，并把
  `voice_collaboration.playback_ack.mode=started_completed_v1` 展开为
  started/cleared/completed 可用、mark 不可用。
- `session.start` 已发送 home_ai 简化 payload：
  `rtos_device_id`、`client_type`、`wake_reason`、`mode_hint`、
  `input_audio`、`output_audio` 与 `capabilities`。
- `audio.in.commit` 已增加 `commit_reason`，同时保留 `reason` 作为兼容字段。
- `audio.out.meta.duration_ms` 已作为 `expected_duration_ms` fallback。
- 播放状态机保留本地 segment progress，但当 mark ACK 未协商时不再排队
  `audio.out.mark` wire event。
- 2026-05-06 新增 M1 下行裸 PCM 重分帧适配：
  - `/root/home_ai_server` 当前在 `audio.out.meta` 后直接发送裸 `pcm16le`
    websocket binary，chunk 大小不保证等于 20 ms 播放帧。
  - 端侧下行 worker 现在会按协商的 `sample_rate + frame_duration_ms`
    把任意大小 binary chunk 重切成固定播放帧逐帧入队。
  - 不足一帧的尾巴先缓存到本地 accum；在 meta lineage 切换或
    `output_state=idle` 终态闭合前再补零 flush 成最后一帧，避免“服务端
    已 speaking 但板端无声”以及 segment 尾包静默丢失。
- 2026-05-07 更新 cached-response 终段误判 rebuffer 保护：
  - 当前 `cached_response` 短句在端侧成功起播后，服务端往往已经把整段音频
    很快下推完；此时 software ring 见底并不代表“上游还会继续补音频”。
  - 端侧现在按 segment 统计 `pushed_duration_ms`，并把本地队列里尚未写入
    后端的尾音帧也计入可播放预算；若
    `pushed_duration_ms + queued_frames * frame_ms` 已覆盖当前最后一段的
    `expected_duration_ms`，则不再触发 `upstream_starved` recover。
  - 目标是避免播放尾部出现 `AudioTrack_Flush / tx_close / recreate`
    造成的短促噪音、后半句截断和 output-turn 卡死，并为“首轮播完后再次
    唤醒”恢复正常闭环。
- 2026-05-07 禁用 M1 TTS AudioTrack 复用：
  - 11:02 上板日志显示前一轮 rebuffer 风暴已经消失，但短
    cached-response 起播仍显示 `reuse=yes`，随后出现裸 `underrun`，用户
    体感为“抱歉”重复播放且后面内容被截断。
  - Ameba 当前 `AudioTrack_Flush` 明确不支持；端侧若复用同一个
    AudioTrack，就不能证明上一轮短 TTS 的 SDK/硬件缓冲已经被清干净。
  - 播放配置新增 `disable_track_reuse`，XiaoZhi M1 TTS 固定请求新
    AudioTrack，预期板端日志变为 `reuse=no` / `playback_start_new`。
- 2026-05-07 改为预填充后启动 M1 TTS 播放：
  - 11:43/11:44 最新上板日志已经显示
    `playback start backend call: stream=xiaozhi_tts ref=no reuse=no`，说明
    old-track 复用路径已经被切断。
  - 但同批日志继续出现
    `AudioTrack_SetStartThresholdBytes not supported`，随后仍报裸
    `underrun`；同时起播前本地已有 `queued=72` 帧，足够覆盖
    `expected_duration_ms=1440` 的整句短 TTS。
  - 这说明当前主问题不是服务端供给不足，而是播放服务在 Ameba 忽略
    start-threshold 的情况下仍过早 `AudioTrack_Start()`，硬件在真正写满前
    就开始跑。
  - 播放配置新增 `defer_start_until_prefilled`，XiaoZhi M1 TTS 现在先
    prepare track，再累计 `AudioTrack_Write()` 成功写入字节；达到 track
    buffer 阈值后才真正 `AudioTrack_Start()`。
  - 预期板端日志应出现
    `playback start: ... deferred=yes` 和
    `playback deferred start: ... prefetched=15360B threshold=15360B`，且
    `ameba_audio_stream_tx_start` 出现在 deferred-start 日志之后，不再出现裸
    `underrun`。
- 2026-05-06 新增播放完成后的 stale output / ASR 投影收口：
  - 17:30 上板日志显示 TTS 已 `draining -> idle` 且已发送
    `audio.out.completed`，但 dialog runtime 仍从 `barge_in_listening`
    回到 `asr_streaming`，后续唤醒/跟进被输出回合残留挡住。
  - 端侧现在不再把 `input_state=committed` 当作活跃 ASR round；已
    terminal-closed 的 playback response context 也不再保留
    `playback_turn_active`。
  - stale output guard 可覆盖服务端 sparse `output_state=thinking/speaking`
    或残留 playback turn；只要本地没有真实 playback active、没有 rebuffer、
    没有等待音频响应，就允许计时后强制清理 stale output turn。

### Step B: 板端联调收口

目标：

- 用真实板端和 `home_ai_server` 实例验证 wakeword -> commit -> response audio -> ACK 的首轮闭环。

范围：

- 只在 Step A 后根据日志做窄修。

完成标准：

- `river xiaozhi status` 显示 smart-home subprotocol、client commit、preview/server endpoint disabled、playback ACK mode `started_completed_v1`。
- 唤醒后本地静音 endpoint 触发 `audio.in.commit`，服务端返回 `accept_reason=client_audio_in_commit`。
- 收到 `audio.out.meta` 与 binary audio 后播放完整，并回传 `started` 与 `completed`。

验证：

```text
服务端运行 /root/home_ai_server：
  cd /root/home_ai_server
  HOST=0.0.0.0 PORT=8081 bash scripts/run-local.sh

板端配置目标：
  river xiaozhi set ws://101.33.235.154:8081/v1/realtime/ws -
  river xiaozhi status

唤醒后说一句 M1 智能家居命令，例如“打开厨房灯”。
```

期望结果：

- 服务端日志出现 `session_started`、`commit_received`、`accept_reason=client_audio_in_commit`、`audio_segment_meta`、`playback_ack`。
- 板端日志出现 `xiaozhi session.start sent`、`xiaozhi audio.in.commit sent`、`xiaozhi accept latched`、`xiaozhi audio.out.meta`、`xiaozhi playback ack started/completed sent`。
