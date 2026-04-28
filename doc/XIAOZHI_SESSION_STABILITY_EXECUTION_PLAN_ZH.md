# XiaoZhi Session Stability Execution Plan

Status: active / device-side commit-race fix complete; pending board replay validation
Last Updated: 2026-04-28
Branch: `agent-server-v2`

## 1. 当前背景

- 当前主问题：
  - `wake -> XiaoZhi realtime session` 主链已经可运行，但当前仍需把
    `follow-up` 与同会话实时质量收口到更稳定的板端行为
- 已知稳定基线：
  - latest SDK `/root/ameba-rtos`
  - 项目 flash profile:
    - `board/rtl8730e/profiles/RTL8730E_NOR.rdev`
- 当前默认 SDK：
  - `/root/ameba-rtos`
- 与本计划强相关的现有文档：
  - `doc/XIAOZHI_INTEGRATION_IMPLEMENTATION_PLAN_ZH.md`
  - `doc/XIAOZHI_UPLINK_BACKPRESSURE_ANALYSIS_LATEST_SDK_FP32_ZH.md`
  - `.codex/verification.md`
  - `.codex/changes.md`

## 2. 目标

- 保持板端 `wake -> VAD/KWS -> XiaoZhi realtime session` 路径可连续多轮使用
- 验证 step `5.148` 的 `no_ref` follow-up reopen guard 是否真正消除了
  playback stop 后的假 reopen / 空 ASR round
- 在不混淆问题来源的前提下，把剩余问题收敛到明确类别：
  - `follow-up reopen policy`
  - `uplink backpressure / freshness shedding`
  - 其他控制面或运行时回归
- 保留足够日志和统计，让后续每一刀都能从板端日志解释清楚

## 3. 非目标

- 不替换当前 `KWS` / `VAD` 模型
- 不重写 `XiaoZhi` 协议层或整体 cloud adapter 架构
- 不把当前问题和新的大规模架构重构混在同一轮里
- 不切换默认 SDK 基线离开 `/root/ameba-rtos`

## 4. 约束 / Guardrails

- 不修改 `/root/ameba-rtos-1.2`
- 保留现有 per-round ASR stats、reopen guard logs、backpressure 统计
- 每一步都保持：
  - 可编译
  - 可回滚
  - 可板端验证
- follow-up 修复与 uplink 质量优化不要混成同一个提交，除非日志已经证明它们是同一根因

## 5. 已知事实

- step `5.147` 已把 XiaoZhi transport ownership 收口到单一 I/O owner，并增加 per-round ASR stats
- step `5.148` 已加入 `no_ref` reopen guard、silence rearm、和更高的
  follow-up speech hold 阈值
- 当前最需要先确认的是：
  - playback stop 后是否仍会在几十毫秒内错误 reopen
  - 若不再错误 reopen，真实 follow-up 是否还能正常开始新 round
- 现有 backpressure 分析已说明：
  - `send_queue_busy` 更像实时 freshness 保护，不是 KWS 部署错误
- 2026-04-13 的 Step A 自动验证已经确认：
  - latest SDK `/root/ameba-rtos` 构建通过
  - `river_flash.py` 刷板通过，结果为 `Finished PASS`
  - `river xiaozhi status` 已能稳定打印：
    - `xiaozhi no_ref reopen rearm=... silence=... guard_left_ms=... open_hold_frames=...`
  - `river xiaozhi bootstrap` / `connect` / `listen detect` 已能走到：
    - `server hello`
    - `stt partial/final`
    - `tts state=start/stop`
  - 但这轮自动验证里 `audio_rx` 仍为 `0`，没有拿到真实：
    - `playback start`
    - `playback stop`
    - `xiaozhi no_ref reopen guard armed`
    - `xiaozhi no_ref reopen rearmed after silence`
  - 所以当前结论是：
    - Step A 已部分完成
    - 最终 guard 行为仍需要真实语音/音频下行交互再验证
- 2026-04-13 的后续板端验证已经进一步确认：
  - fragmented websocket downlink 修复后，`river xiaozhi connect` /
    `listen detect` 已能得到真实下行音频
  - 已观察到：
    - `playback start: stream=xiaozhi_tts ...`
    - `playback stop: stream=xiaozhi_tts ...`
    - `xiaozhi no_ref reopen guard armed: tail_ms=480 silence_frames=6`
    - `audio_rx > 0`
  - 因而当前 Step A 的剩余问题已缩小为：
    - 还需要继续确认
      `xiaozhi no_ref reopen rearmed after silence: ...`
    - 而不是继续排查下行 transport / playback 是否工作
- 2026-04-13 的最新本地收尾修复已经加入：
  - 保持现有 `listen_stop` 触发时机不变
  - 把本地 `session_closed` / per-round finish 延后到：
    - `post_stop_result`
    - `llm`
    - `tts_start`
    - `timeout`
    - `reopen_overlap`
  - 新增板端观察点：
    - `xiaozhi local close deferred: wait_ms=2000`
    - `xiaozhi local close resolved: trigger=...`
    - `river xiaozhi status` 中的 `close_pending` / `close_left_ms`
- 2026-04-13 的最新建连时延优化已经加入：
  - `open_session()` 不再在每次唤醒时无条件同步执行 HTTP bootstrap
  - 成功 bootstrap 后会缓存 `ws url/token`，默认 TTL 为 `10 min`
  - 只在以下情况才会重新同步 bootstrap：
    - 当前没有可用 `ws url`
    - 当前 `ws url/token` 来自 bootstrap 且缓存已过期
  - 板端新增观察点：
    - `xiaozhi bootstrap cache hit: refresh_in_ms=...`
    - `river xiaozhi status` 中的
      `bootstrap_owned=... bootstrap_refresh_in_ms=...`

## 6. 风险与未知项

- `no_ref` guard 可能仍然不够严格，尾音/残留近端语音仍可能触发空 round
- `no_ref` guard 也可能过严，导致真实 follow-up 被明显延迟或漏开
- 即使 follow-up reopen 正常，uplink `busy / stale_drop` 仍可能单独影响体验
- bootstrap cache TTL 也可能和服务端 token 生命周期不完全一致
- 某些问题只能在真实板端时序下出现，单靠代码阅读无法排除

## 7. 执行切片

### Step A: 验证 no_ref follow-up guard

目标：

- 在真实板端确认 step `5.148` 的目标是否达成

范围：

- 当前固件运行验证
- 如需修复，再限制在：
  - `components/river_cloud/river_cloud_adapter.c`
  - `components/river_cloud/river_cloud_internal.h`
  - `components/river_cloud/river_cloud_xiaozhi_session.c`

完成标准：

- 日志出现：
  - `xiaozhi no_ref reopen guard armed: ...`
  - `xiaozhi no_ref reopen rearmed after silence: ...`
- playback stop 后不再立刻进入空 round
- 合法 follow-up 仍然能进入：
  - `asr provider=xiaozhi_realtime session started`
  - `xiaozhi asr round begin: ...`

验证：

```bash
cd /root/ameba-river
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 /root/ameba-river/tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000
```

期望结果：

- `Build done`
- `Finished PASS`
- `no_ref` reopen guard 按 step `5.148` 的日志设计工作
- 当前结果：
  - 已确认 build / flash / connect / detect 路径可用
  - 已确认真实 downlink playback 与 guard arm：
    - `audio_rx > 0`
    - `playback start`
    - `playback stop`
    - `xiaozhi no_ref reopen guard armed`
  - 尚未确认：
    - `xiaozhi no_ref reopen rearmed after silence`

### Step B: 若仍有假 reopen，最小化收紧本地 follow-up policy

目标：

- 只针对 residual tail / false reopen 做本地策略修复，不把 uplink 质量问题混进来

范围：

- `components/river_cloud/river_cloud_adapter.c`
- `components/river_cloud/river_cloud_internal.h`
- `components/river_cloud/river_cloud_xiaozhi_session.c`

完成标准：

- 空 round 现象消失或显著收敛
- 真实 follow-up 不被明显破坏
- 日志足以说明修复点落在 reopen policy，而不是其他路径

验证：

```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
```

期望结果：

- 文档入口保持一致
- 后续代码步骤再按 Step A 的 build/flash/monitor 验证

### Step C: 若 follow-up 正常，单独观察 uplink 质量

目标：

- 把剩余问题限定在 realtime uplink freshness / backpressure，而不是 reopen policy

范围：

- 当前日志和已有 per-round stats
- 如需后续代码改动，再限制在：
  - `components/river_cloud/river_cloud_adapter.c`
  - `components/river_cloud/river_xiaozhi_ws.c`

完成标准：

- 能明确回答剩余主瓶颈是否来自：
  - `busy_count`
  - `stale_drop`
  - `q_peak`
  - `bp`
- 若继续修改 uplink，也必须和 follow-up 修复分开提交

验证：

```bash
cd /root/ameba-river
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000
```

期望结果：

- 日志足以区分 follow-up reopen 与 uplink quality 问题


### Step D: 对齐服务侧首音频失败回 active 语义

目标：

- 当服务侧在 `response.start` 后因首音频失败返回 `session.update state=active / output_state=idle` 且没有 `audio.out.meta` 时，端侧终止本地 response-audio wait。
- 保留真正悬挂场景下的 `5s response_audio_timeout` recovery，避免服务完全无回包时设备卡死。

范围：

- `components/river_cloud/river_cloud_internal.h`
- `components/river_cloud/river_cloud_xiaozhi_playback_terminal_ack.inc`
- `components/river_cloud/river_cloud_xiaozhi_session.c`

完成标准：

- 收到服务侧 `active/idle` no-meta 回退时打印：
  - `xiaozhi response audio abandoned`
  - `xiaozhi response audio wait cleared`
- 此路径不发送 `audio.out.started / mark / completed`。
- 此路径不再等待 5s 后执行 `response_audio_timeout` abort/close。
- 若服务侧既不回 active/idle 也不发 `audio.out.meta`，现有 5s timeout recovery 仍然生效。

验证：

```bash
cd /root/ameba-river
git diff --check
python3 tools/diag/check_codex_harness.py
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
```

期望结果：

- `git diff --check` 无输出
- `check_codex_harness: all checks passed`
- latest SDK 构建输出 `Build done`

### Step E: 透传服务侧 audio.out.meta 输出通道事实

状态：已完成（Step 5.525）

目标：

- 消费服务侧新增的 `output_lane` / `output_role` / `phrase_id` 字段。
- 只把这些字段作为服务事实记录和状态暴露，不在端侧重建
  `fast_launch/main_dialogue` 规划器。

范围：

- `include/river/river_xiaozhi_ws.h`
- `components/river_cloud/river_xiaozhi_ws*.inc`
- `components/river_cloud/river_cloud_xiaozhi_playback_*.inc`

完成标准：

- `audio.out.meta` 解析并打印：
  - `output_lane=...`
  - `output_role=...`
  - `phrase_id=...`
- transport event 与 playback truth 均能保留这三个字段。
- `river xiaozhi status` / playback status 可展示最近一次 meta 的字段。

验证：

```bash
cd /root/ameba-river
git diff --check
python3 tools/diag/check_codex_harness.py
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
```

期望结果：

- `git diff --check` 无输出
- `check_codex_harness: all checks passed`
- latest SDK 构建输出 `Build done`

### Step F: 补齐 uplink freshness 遥测

状态：已完成（Step 5.525）

目标：

- 在 per-round ASR finish 日志里补齐上行健康指标：
  - send interval p50/p95
  - 估算 capture-to-send age p50/p95
  - backlog p95
  - websocket send 调用耗时 p50/p95
- 用固定小样本窗口实现，不引入堆分配或高频日志。

范围：

- `components/river_cloud/river_cloud_internal.h`
- `components/river_cloud/river_cloud_xiaozhi_session.c`

完成标准：

- 每个 ASR round 结束时都能看到上述统计。
- 统计只用于诊断，不改变当前 20 ms pacing / stale-drop 行为。

### Step G: 补齐 no-ref 泄漏提示与安全本地兜底

状态：已收敛（Step 5.525 完成诊断；Step 5.526 暂停默认本地提示音，待板端非阻塞验证后再打开）

目标：

- 在没有 playback reference、但播放/回放链路仍可能影响上行时，输出更直接的事实日志。
- 当服务侧 response 首音频失败或超时导致没有可播放音频时，播放一个短本地兜底提示音：
  - 不伪造 `audio.out.meta`
  - 不发送 playback ACK
  - 通过 playback service + reference export 进入现有 AEC / uplink 抑制路径

范围：

- `components/river_cloud/river_cloud_internal.h`
- `components/river_cloud/river_cloud_xiaozhi_session.c`
- `components/river_cloud/river_cloud_xiaozhi_playback_*.inc`

完成标准：

- no-ref 相关日志能说明当前 ref 缺失/泄漏风险。
- 默认不启动本地兜底播放；本地兜底重新打开前必须证明 AudioTrack
  start/write 不会阻塞 capture/VAD 消费。
- 本地兜底不污染 playback lineage / terminal ACK truth。

### Step H: 阻断 response pending 期间的 follow-up reopen

状态：已完成（Step 5.526）

目标：

- 服务端已 accepted 且 `output_state=thinking` 时，端侧不得因为本地 VAD
  speech 立即重开 follow-up ASR round。
- `response.start` 后、尚未观察到首个 `audio.out.meta` 前，也不得重开
  follow-up ASR round。
- 若此期间检测到 speech，只记录一次 response-pending block 诊断，并清零
  open-hold 计数，避免 response 尚未完成时进入重叠 ASR。

范围：

- `components/river_cloud/river_cloud_internal.h`
- `components/river_cloud/river_cloud_xiaozhi_round_runtime.c`

完成标准：

- 2026-04-26 日志中的模式不再出现：
  - `server_endpoint_accept` 后约百毫秒内立刻 `asr round begin: id=2`
  - no-audio abandoned 前 interaction 仍被新 round 推回 `asr_streaming`
- 若用户在 response pending 期间说话，日志显示：
  - `xiaozhi followup reopen blocked: reason=response_pending ...`
- no-audio 路径默认不会再触发 `xiaozhi_local_retry` 本地播放，从而避免
  capture ring overflow 连锁故障。

## 编写建议

- 后续更新此计划时，优先刷新 “已知事实 / 风险 / 当前下一刀”，不要把它写成历史流水账。
- 只有在当前主目标切换时，才把它从 `.codex/active_plans.md` 的 primary plan 移走。
### Step I: server endpoint candidate 后禁发本地 commit

状态：已完成代码修复，待上板复测。

目标：

- 修复 no-audio 连续 follow-up 日志中残留的一次 `turn_not_ready` / `audio.in.commit is accepted only while the session is active`。

范围：

- `components/river_cloud/river_cloud_xiaozhi_session.c`
- `components/river_cloud/river_cloud_xiaozhi_round_runtime.c`
- `components/river_cloud/river_xiaozhi_ws.c`
- `components/river_cloud/river_cloud_internal.h`

实现：

- 若本轮已经收到 `input.endpoint candidate=yes` 且 server endpoint negotiation 可用，端侧 post-roll 不再发送本地 `audio.in.commit`。
- 本地 ASR round 以 `server_endpoint_candidate` 收口，等待服务端 `server_endpoint` accept。
- wire-level commit 入口增加 session/input/output/response 状态 guard，防止 stale commit 请求在服务已进入 thinking/speaking 后发出。

验证：

```bash
cd /root/ameba-river
git diff --check
python3 tools/diag/check_codex_harness.py
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

期望：

- `Build done`。
- 上板日志中不再出现 `audio.in.commit is accepted only while the session is active`。
- no-audio 服务响应仍以 `response audio abandoned` 收口，不启动本地 fallback prompt。
### Step J: audio.out.meta 后播放/采集状态机收口

状态：已完成代码修复，待上板复测。

目标：

- 修复 2026-04-27 日志中服务端已进入 `speaking` 并下发 `audio.out.meta` 后，端侧仍由 `note_meta` 触发 `asr_streaming` / 空 ASR round 的问题。
- 避免 AudioTrack 启动路径卡顿时阻塞 VAD/capture consumer，导致 `capture frame ring overflow` 持续增长。
- native capture reference 配置下减少播放启动对 playback reference export 的额外耦合。

范围：

- `components/river_core/river_dialog_runtime.c`
- `components/river_cloud/river_cloud_xiaozhi_round_runtime.c`
- `components/river_cloud/river_cloud_xiaozhi_playback_downlink_cycle.inc`
- `components/river_cloud/river_cloud_internal.h`
- `components/river_voice/river_playback_service.c`

实现：

- dialog runtime 在 `output_state=speaking` 且 playback lane engaged 时保留输出轮次，避免 `note_meta` 将状态推回 ASR。
- follow-up reopen 在 speaking playback 未物理 active 或 AEC 未 ready 时阻断并清零 open-hold。
- XiaoZhi downlink task 优先级低于 VAD/capture consumer，播放启动异常不应饿死采集消费者。
- native capture reference profile 下 TTS playback 使用 `no_ref` 启动，避免重复 reference export。
- playback backend prepare / `AudioTrack_Start` 前增加定位日志。

验证：

```bash
cd /root/ameba-river
git diff --check
python3 tools/diag/check_codex_harness.py
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

期望：

- `Build done`。
- `audio.out.meta` 后不再出现 `interaction_state: thinking -> asr_streaming reason=note_meta`。
- 不再出现空 ASR round：`duration_ms=4 audio_ms=0 packets=0`。
- `playback_start_prepare` 后不再持续刷 `capture frame ring overflow`。

### Step K: 采集热路径非阻塞防护

状态：已完成代码修复，待上板复测。

目标：

- 继续收口 2026-04-27 日志中 `audio.out.meta` 后播放启动异常引发的端侧连锁问题。
- 即使 `AudioTrack_Start/Write`、playback reference 维护、dialog runtime reconciliation 或 runtime stats 采样短时卡顿，VAD/capture consumer 也不能被同步锁等待拖死。
- reference / AEC / duplex readiness 在锁忙时应降级为 no-ref/aec-blocked，而不是阻塞采集任务。

范围：

- `components/river_common/river_runtime_stats.c`
- `components/river_core/river_dialog_runtime.c`
- `components/river_voice/river_playback_service.c`
- `components/river_voice/river_reference_service.c`
- `components/river_voice/river_voice_ref.c`
- `components/river_voice/river_voice_runtime_policy.c`
- `components/river_voice/river_voice_vad_probe.c`

实现：

- playback stats getter 使用 try-lock，锁忙时返回已有 snapshot。
- reference service read/stats 与底层 playback-reference ring read/stats 使用 try-lock，锁忙时清零参考帧并返回 busy/空统计。
- native capture reference publish/get 使用 try-lock，锁忙时丢弃本帧观测或返回 unavailable。
- dialog runtime voice-policy view 使用 try-lock，锁忙时让 AEC policy 走保守降级。
- runtime stats snapshot 使用 try-lock，避免 VAD 状态变更日志卡住采集线程。
- VAD barge-in duck 控制使用非阻塞 playback control；release 失败时保留本地 active 标志，后续继续重试。

验证：

```bash
cd /root/ameba-river
git diff --check
python3 tools/diag/check_codex_harness.py
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

期望：

- `Build done`。
- `audio.out.meta` 后不再出现 `interaction_state: thinking -> asr_streaming reason=note_meta`。
- 不再出现空 ASR round：`duration_ms=4 audio_ms=0 packets=0`。
- 播放启动或 reference 维护异常时，端侧不再持续刷 `capture frame ring overflow`。
- 若 AEC/reference 暂不可用，表现为 no-ref/aec-blocked 降级，而不是采集任务阻塞。

### Step L: zero-duration ACK 播放尾态与交互诊断收口

状态：已完成代码修复，待上板复测。

目标：

- 修复 zero-duration fast-launch ACK 在 DAC 仍处于 drain / stop-pending 时，dialog runtime 已把 output turn 视为结束、交互态提前退回 `asr_streaming` 的问题。
- 保证 `terminal_closed` 不会在 `physical_active` 或 `tts_stop_pending` 期间直接打掉 `output_turn_engaged`。
- 在交互态切换日志中补足 `terminal_closed / playback_active / tts_stop_pending / duplex_ready_seen`，方便对齐 AEC/no-ref 尾态。

范围：

- `components/river_cloud/river_cloud_xiaozhi_playback_runtime.c`
- `components/river_cloud/river_cloud_xiaozhi_playback_runtime_views.inc`
- `components/river_cloud/river_cloud_xiaozhi_playback_public_policy.inc`
- `components/river_core/river_dialog_runtime.c`
- `include/river/river_cloud.h`
- `include/river/river_dialog_runtime.h`

实现：

- playback terminal 对外 closed 判定改为“terminal 已完成且 backend 已不再 output-active，且不再 stop-pending”后才成立。
- zero-duration fast-launch ACK 的 local-completed/terminal-close 对 dialog runtime 可见性延后到 DAC drain 结束后，避免播放尾巴期间提前释放 output turn。
- dialog runtime 的交互态切换前新增结构化日志，输出 `terminal_closed`、`playback_active`、`tts_stop_pending` 和 `duplex_ready_seen`。

验证：

```bash
cd /root/ameba-river
git diff --check
python3 tools/diag/check_codex_harness.py
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

期望：

- `Build done`。
- zero-duration ACK 尾态不再出现 `interaction_state: barge_in_listening -> asr_streaming reason=arm_stop` 早于物理播放停止。
- 新增 `dialog_runtime interaction_transition: ... terminal_closed=... playback_active=... tts_stop_pending=... duplex_ready_seen=...` 日志。
- `half_duplex_aec_blocked` / `capture held during playback` 若仍出现，其前序交互态日志应仍保持 output turn engaged，而不是提前回到 ASR。

### Step M: invalid-voice 播放尾态卡死与 transport-close 状态发布收口

状态：已完成代码修复，待上板复测。

目标：

- 修复服务端播放 `未识别到有效语音。` 后，端侧播放尾态卡在 `barge_in_listening`、后续说话无响应的问题。
- 防止 zero-duration/短尾巴 ACK 在 terminal completed 后，被晚到 downlink audio 重新取消 `tts_stop_pending` 并拉回 `playing`。
- 修正 transport close 时 `session_closed` 的发布顺序，避免用旧 `previewing` / `stream_active` 语义错误回到 `asr_streaming`。

范围：

- `components/river_cloud/river_cloud_xiaozhi_playback_downlink_worker.inc`
- `components/river_cloud/river_cloud_xiaozhi_round_runtime.c`

实现：

- playback completed 改为 backend stream 已退出 attached/active 后再真正 queue/sent；attached 期间只 arm stop，不提前 completed。
- downlink audio 写入后仅在 playback terminal 仍 open 时才取消 `tts_stop_pending`，避免 completed 尾态被晚到音频重新打开。
- `reset_transport_state()` 先清空 stream、preview、turn semantics，再按需要发 `session_closed`，让状态发布看到的是清理后的 truth。

验证：

```bash
cd /root/ameba-river
git diff --check
python3 tools/diag/check_codex_harness.py
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

期望：

- `Build done`。
- 不再出现 `playback phase: draining -> playing reason=cancel_stop queued=1 segments=0`。
- 复现服务端 `未识别到有效语音。` 后，播放结束后再次说话可重新触发 follow-up / ASR。
- transport close 时不再出现带旧 `previewing` 输入语义的 `... -> asr_streaming reason=playback_state`。


### Step N: invalid-voice ACK completed 去重与 lane-engaged / transport-close 状态收口

状态：已完成代码修复，待上板复测。

目标：

- 修复短 ACK 尾态在 backend 真正 detach 前重复发送 `audio.out.completed` 的问题。
- 保证 `audio.out.meta` 已到但 playback 尚未物理 active、以及 stop-pending / drain 尚未 detach 的窗口里，dialog runtime 继续把 output turn 视为 engaged。
- 修复 transport close 瞬间仍携带旧 `previewing` / accepted truth 发布交互态与 poll 日志的问题。

范围：

- `components/river_cloud/river_cloud_xiaozhi_playback_terminal_ack.inc`
- `components/river_core/river_dialog_runtime.c`
- `components/river_cloud/river_cloud_xiaozhi_session.c`

实现：

- `river_cloud_xiaozhi_try_queue_playback_completed_ack()` 在 `completed_reported` 已成立时直接早退，并在 `COMPLETED_QUEUED` lineage touch 后立刻同步 terminal report flags，压住重复 completed ACK re-entry。
- `river_dialog_runtime_output_turn_engaged_from_projection()` 现在把 `lane_engaged` 直接视为 output-turn engaged 的充分条件，覆盖 prefetch、waiting-segment、draining 等物理播放与逻辑播放短时错位窗口。
- `river_cloud_xiaozhi_apply_transport_closed_terminal_policy()` 在 stop playback 前先清 session-update cache、preview state、turn semantics，并主动请求一次 `transport_closed_preclear` state sync。

验证：

```bash
cd /root/ameba-river
git diff --check
python3 tools/diag/check_codex_harness.py
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

期望：

- `Build done`。
- 同一 `playback_id` 只出现一次 `xiaozhi playback ack completed queued/sent`。
- `audio.out.meta` 到真实 `playback start` 之间，不再出现 `... -> asr_streaming reason=playback_state`。
- drain 尾态 `tts_stop_pending=yes` 时，不再提前退回 `asr_streaming`。
- `transport_closed` 后，不再残留旧 `input_state=previewing` 的 `xiaozhi turn accepted: trigger=poll ...`。


### Step O: late last-meta 尾态折叠与 paused tail 收口

状态：已完成代码修复，待上板复测。

目标：

- 修复服务端对同一 `segment_id` 晚到补发 `is_last_segment=yes` 时，端侧把它误当成新 segment 再次入队的问题。
- 避免 reply 尾段已经 fully-heard 后，playback 仍卡在 `owned_paused/prefetching`，导致后续 reopen 一直进不来。
- 让这类 paused stale tail 直接折叠到 terminal completion，而不是拖到 session idle-timeout。

范围：

- `components/river_cloud/river_cloud_xiaozhi_playback_terminal_ack.inc`

实现：

- 新增 late-last-meta fold 路径：当 `audio.out.meta` 是同一 `segment_id`、该 segment 已 fully-heard、且当前 segment queue 头已空时，不再分配新的 playback segment。
- 对这种 late terminalization，直接更新 `last_segment_context` / terminal lineage。
- 若 backend 只是 `OWNED_PAUSED` 挂着 stale queued tail，则 reset downlink ring、停止 `xiaozhi_late_last_meta` backend，并立即尝试 completed 收口。

验证：

```bash
cd /root/ameba-river
git diff --check
python3 tools/diag/check_codex_harness.py
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

期望：

- `Build done`。
- 最后一段 final mark 之后，即使服务端再补同 `segment_id is_last_segment=yes`，也不会再把 playback 卡在 `prefetching/backend=owned_paused`。
- 播放完 `我没听清，请再说一遍。` 后，后续再说话可重新进入 ASR。
- 不再一直拖到 `xiaozhi session.end: ... idle_timeout` 才退出 output turn。
