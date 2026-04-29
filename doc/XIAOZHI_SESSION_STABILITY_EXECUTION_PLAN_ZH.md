# XiaoZhi Session Stability Execution Plan

Status: active / playback late-audio tail suppression complete; pending board replay validation
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


### Step P: marked-tail 补齐 fully-heard 后再折叠 late last-meta

状态：已完成代码修复，待上板复测。

目标：

- 修复播完 `已帮你打开灯光。` 这类单段主回答后，final mark 已经到齐，但 `fully_heard_context` 尚未及时落账时，晚到 same-segment `is_last_segment=yes` 仍会把尾态挂住的问题。
- 让 late last-meta 折叠路径可以接受“已经完整播完、但只记到了 `marked_context`”的尾段，而不是只认已经落账的 `fully_heard_context`。
- 避免 session 一直卡到 `idle_timeout` 才退出 output turn。

范围：

- `components/river_cloud/river_cloud_xiaozhi_playback_terminal_ack.inc`

实现：

- 新增 `river_cloud_xiaozhi_promote_marked_tail_to_fully_heard(...)`：当 current segment 已空、same-segment `marked_context` 已对齐、且 `last_mark_ms >= expected_duration_ms` 时，把该尾段补记为 `fully_heard_context`。
- `river_cloud_xiaozhi_fold_late_last_segment_meta_for_fully_heard_tail(...)` 不再只接受已存在的 `fully_heard_context`；必要时先调用上述 helper 补齐 terminal truth，再走既有 fold/completed 收口。
- 新增 `xiaozhi playback late last meta synthesized fully-heard: ...` 日志，方便上板直接确认是否走到这条兜底路径。

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
- 在 `已帮你打开灯光。` 场景里，final mark 之后即使再收到同一 `segment_id is_last_segment=yes`，也不会再卡在 `prefetching/backend=owned_paused`。
- 日志至少出现 `xiaozhi playback late last meta folded: ...`；若 `fully_heard_context` 当时还未落账，还会出现 `xiaozhi playback late last meta synthesized fully-heard: ...`。
- 下一句说话能重新进入 ASR / follow-up，而不是只剩 VAD 日志直到 `xiaozhi session.end: ... idle_timeout`。


### Step Q: stale current-tail 迟到 last-meta 强制收口

状态：已完成代码修复，待上板复测。

目标：

- 修复 `final mark` 已到齐、但 current playback segment 仍残留在队列头时，晚到 same-segment `is_last_segment=yes` 仍无法收口的问题。
- 避免 runtime 已进入 `OWNED_PAUSED + WAITING_NEXT_SEGMENT`，output turn 仍被旧 current tail 卡住，导致后续说话只有 VAD 没有 ASR reopen。
- 把这类 stale current tail 直接折叠进 terminal completion，而不是继续走普通 `note_meta -> prefetch` 路径。

范围：

- `components/river_cloud/river_cloud_xiaozhi_playback_terminal_ack.inc`

实现：

- 新增 `river_cloud_xiaozhi_fold_late_last_segment_meta_for_current_tail(...)`，专门处理“current segment 还在，但已经是 stale tail”的迟到 terminal meta。
- 只有同时满足以下条件才强制收口：
  - 当前 `segment_id` 与 `audio.out.meta` 完全一致；
  - current segment 已经 started；
  - backend 已处于 `OWNED_PAUSED`；
  - supply 已是 `WAITING_NEXT_SEGMENT`；
  - `output_active=no`、`tts_stop_pending=no`、`rebuffer_pending=no`；
  - `last_mark_ms >= expected_duration_ms`（零时长尾段则要求 `last_mark_ms > 0`）。
- 命中后直接：
  - 把该 current tail 标记为 fully-heard；
  - 立即 `pop` 当前 stale segment；
  - 再复用既有 late-last-meta fold/completed 收口。
- 新增 `xiaozhi playback late last meta consumed stale current tail: ...` 日志，并在通用 fold 日志里补充 `consumed_current=yes/no`，便于区分“空队列尾态”与“current stale tail”两类命中。

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
- 对用户 2026-04-28 14:30 这类日志：
  - final `played_duration_ms=1100` 后即使 current segment 还残留，也不会再长期停在 `wait_next=yes`；
  - 日志出现 `xiaozhi playback late last meta consumed stale current tail: ...` 或至少 `late last meta folded: ... consumed_current=yes`；
  - 后续说话会重新进入 ASR，而不是只剩 VAD `speech/silence` 直到超时。


### Step R: 多轮对话 stale output-turn 语音兜底

状态：已完成代码修复，待上板复测。

目标：

- 再补一条端侧兜底，避免多轮对话里 local playback/runtime truth 偶发残留时，follow-up reopen 被 `output_turn_guard` 长时间卡死。
- 在不破坏正常 `thinking/speaking/rebuffer` 保护的前提下，只对“服务端已不在输出、物理播放也不活跃、但本地 playback turn 仍残留”的异常状态做自恢复。
- 确保用户持续开口时，设备最终能回到可 reopen 的状态，而不是一直只剩 VAD 到 `idle_timeout`。

范围：

- `components/river_cloud/river_cloud_internal.h`
- `components/river_cloud/river_cloud_xiaozhi_round_runtime.c`

实现：

- 新增 `RIVER_CLOUD_XIAOZHI_STALE_OUTPUT_GUARD_MS=720` 兜底窗口。
- `maybe_start_followup_round(...)` 在 `output_turn_guard` 阻断时，不再一刀切地直接丢掉当前语音意图；会先判断是否命中“stale output-turn”：
  - `window_active=yes`
  - `stream_active=no`
  - `output_state` 既不是 `thinking` 也不是 `speaking`
  - `response_waiting_audio=no`
  - `playback_lane_engaged=yes`
  - `playback_turn_active=yes`
  - `playback_output_active=no`
  - `playback_rebuffer_pending=no`
- 若用户在上述异常态下持续说话超过 `720ms`：
  - 打印 `xiaozhi stale output guard armed: ...`
  - 若超时仍未恢复，再打印 `xiaozhi stale output guard forcing playback clear: ...`
  - 本地按 `xiaozhi_stale_output_guard` 触发一次 playback interrupt/clear
  - 然后立即重新评估 follow-up reopen，允许同一句话继续进入 ASR
- window close / abort / transport reset / listen reopen 时会同步清掉 guard deadline，避免旧 guard 污染新一轮会话。

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
- 正常的 `thinking/speaking/rebuffer` 期间，follow-up 仍被保护，不会因为短暂说话误清当前输出。
- 若再次出现“物理播放已结束，但 output turn 本地残留”的异常态，持续说话时应先看到：
  - `xiaozhi stale output guard armed: ...`
  - 若 720ms 后仍未恢复，再看到 `xiaozhi stale output guard forcing playback clear: ...`
- 触发兜底后，同一句 follow-up 能继续进入 ASR，而不是被迫等到 `idle_timeout`。


### Step S: terminal-only stale output-turn 兜底补强

状态：已完成代码修复，待上板复测。

目标：

- 覆盖另一类历史残留：`playback_lane` 已经松开，但 `playback_turn` 仍因 terminal truth 残留而保持打开，导致 follow-up reopen 继续被 `output_turn_guard` 阻断。
- 把 stale-output 语音兜底从“只处理 lane 残留”扩成“处理所有本地已不再输出、但 output turn 仍残留”的异常态。
- 继续保持对正常 `thinking/speaking/rebuffer` 的保护，不把兜底扩大成常态路径。

范围：

- `components/river_cloud/river_cloud_xiaozhi_round_runtime.c`

实现：

- 复查 Step R 后确认，旧 guard 仍有盲区：
  - 只有 `playback_lane_engaged=yes` 才会 arm stale-output guard；
  - 若 queue / wait-context 已经空了，但 `playback_turn_active=yes` 仍被 terminal truth 卡住，设备会继续只剩 VAD，没有 ASR reopen。
- stale-output guard 现在改为只要求：
  - `window_active=yes`
  - `stream_active=no`
  - `output_state` 既不是 `thinking` 也不是 `speaking`
  - `response_waiting_audio=no`
  - `playback_turn_active=yes`
  - `playback_output_active=no`
  - `playback_rebuffer_pending=no`
- 因而同一条 `720ms` 语音兜底现在同时覆盖：
  - `playback_lane=yes` 的 stale lane / stale tail
  - `playback_lane=no` 但 `playback_turn=yes` 的 terminal-only stale turn
- guard 日志补充 `wait=yes/no`，便于从板端日志区分：
  - 仍卡在 wait-context / segment-gap
  - 还是已经没有 lane，只剩 terminal truth 残留

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
- 若再次复现“播完一轮 TTS 后后续说话完全无反应”：
  - 即使日志里已经看不到 `playback_lane=yes`，只剩 `playback_turn=yes`，也应先看到 `xiaozhi stale output guard armed: ... playback_lane=no playback_turn=yes ...`
  - 若异常态持续 `720ms`，应继续看到 `xiaozhi stale output guard forcing playback clear: ...`
  - 兜底触发后，同一句 follow-up 能继续 reopen ASR，而不是只剩 VAD 到 `idle_timeout`。


### Step T: empty-turn active-return 与 follow-up 断链自恢复

状态：已完成代码修复，待上板复测。

目标：

- 对齐服务侧已经确认存在的合法语义：某些 `accepted` 回合不会进入 `response.start/TTS`，而是会在无文本 / 空语音条件下直接回到 `active/idle`。
- 端侧不再把 `accepted` 写死为“必有 response.start”，避免在 empty-turn/silent-recovery 场景里把状态机卡死。
- 对 empty-turn active-return 后紧跟的一次 follow-up `transport_closed/EOF` 增加窄范围自恢复，保证多轮对话还能继续。

范围：

- `components/river_cloud/river_cloud_internal.h`
- `components/river_cloud/river_cloud_xiaozhi_round_runtime.c`
- `components/river_cloud/river_cloud_xiaozhi_session.c`

实现：

- 新增 `RIVER_CLOUD_XIAOZHI_ACCEPTED_RESPONSE_WATCHDOG_MS=6000`，在 `server committed input` 后 arm：
  - 若后续收到 `response.start`，立即清 watchdog；
  - 若 accepted 后长时间既没有 `response.start` 也没有 empty-turn active-return，则 watchdog timeout 后主动 abort 并重同步 state。
- 新增 empty-turn active-return 语义：
  - 当 `session_state=active` 且 `output_state=idle`，并且本轮 accepted watchdog 仍然有效、同时未观察到 `response.start` 音频链路时，端侧记录 `xiaozhi empty turn returned active: ...`；
  - 同步清 session-update cache / preview / turn semantics，重新 touch follow-up window，并建立 `empty_turn_recover_deadline_ms`。
- 新增 follow-up `transport_closed` auto-recover：
  - 仅当 `window_active=yes`、`empty_turn_recover_deadline_ms` 尚未过期、`stream_active=no`、`playback_turn_active=no`、Wi-Fi 仍在线时触发；
  - 端侧记录 `xiaozhi transport closed followup recover: action=reopen_listen ...`，随后直接 `open_session_and_listen()`；
  - 若 reopen 失败，则退回 `window_close("transport_recover_open_failed")`，避免无界重试。

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
- 当服务端把某轮 accepted 按空语音直接收回 `active/idle` 时，端侧应看到：
  - `xiaozhi empty turn returned active: ...`
- 若随后立刻发生一次 follow-up window 内的 `transport_closed/EOF`，且本地没有 active stream / playback turn，应看到：
  - `xiaozhi transport closed followup recover: action=reopen_listen ...`
  - 成功时继续看到 `xiaozhi transport closed followup recovered: ...`
- 若 accepted 后长时间没有 `response.start`，也没有回到 active/idle，应看到：
  - `xiaozhi accepted response watchdog timeout: ...`
- 多轮对话不再因为“accepted 但没有 response.start”这条服务端合法语义而永久失活。


### Step ZA: same-segment promotion in-place upgrade

状态：已完成代码修复，待上板复测。

目标：

- 对齐服务侧语义：同一个 `response_id + playback_id + segment_id` 再收到 `audio.out.meta`，且只是 `is_last_segment: false -> true` 时，要按原 segment 的元数据升级处理，不能当成新 segment。
- 避免这类 promotion 再次触发 `prefetching / recover / segment gap hold / stop`。

范围：

- `components/river_cloud/river_cloud_xiaozhi_playback_terminal_ack.inc`

实现：

- 在 `river_cloud_xiaozhi_playback_note_meta()` 中，将以下条件识别为 same-segment promotion：
  - segment 已存在且上下文完全相同
  - `segment->is_last_segment == false`
  - `event->is_last_segment == true`
- 命中后不再走普通新分段路径：
  - 不再执行普通 `prefetch` / `refresh_playback_phase("note_meta")`
  - 只更新原 segment 的 `expected_duration_ms / text / is_last_segment`
  - 并刷新 `last_segment_context`
- 若该 promotion 同时命中“当前 tail 已播到 expected mark”的安全窗口：
  - 继续优先走 `river_cloud_xiaozhi_fold_late_last_segment_meta_for_current_tail()`
  - 直接本地折叠闭环结束
- 因此只有 `segment_id` 真变化时，端侧才进入真正的新分段切换逻辑。

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
- same-segment false->true promotion 被按原 segment 元数据升级处理。
- 这类 promotion 不再触发额外 `prefetching / recover / segment gap hold / stop`。
- 若 mark 已覆盖 expected duration，则 promotion 可直接本地闭环。

### Step Z: late last-meta upgrade fold

状态：已完成代码修复，待上板复测。

目标：

- 收掉同一 `segment_id` 先按 non-last 发布、尾段播完后又迟到补成 last 时的本地 playback 抖动。
- 避免 `_0002` 这类 stale tail 被迟到 meta 重新带回 `recover / gap hold / stop`。

范围：

- `components/river_cloud/river_cloud_xiaozhi_playback_terminal_ack.inc`

实现：

- 在 `river_cloud_xiaozhi_playback_note_meta()` 里增加 late same-segment last-upgrade 快路：
  - 仅命中“已有有效 segment、旧状态不是 last、新事件把同一 `segment_id` 升级成 last”的情况
  - 并继续复用 `river_cloud_xiaozhi_fold_late_last_segment_meta_for_current_tail()` 作为安全门
- 只有当当前 tail：
  - 已经 started
  - mark 已达到 expected duration
  - backend 正处于 paused / waiting-next-segment 的安全窗口
  - 才直接把这次迟到升级折叠成 fully-heard/last
- 命中后不再走普通 `note_meta` 重发布路径，从而避免把已经播完的 stale tail 再次暴露给 recover/gap hold 状态机。

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
- 同一 `segment_id` 的迟到 `non-last -> last` 升级被直接折叠。
- 尾段播完后不再因为这次补报进入额外 `playback recover` / `segment gap hold`。
- playback completed/cleared 路径继续保持闭环。

### Step Y: preview warmup catch-up

状态：已完成代码修复，待上板复测。

目标：

- 收掉 preview startup 阶段“uplink 已经落后，但端侧仍严格 20 ms 一拍一发”的人为 backlog。
- 给首个 partial / refresh 一个受控的追平机会，优先改善 accept 前的 preview/uplink realtime ratio。

范围：

- `components/river_cloud/river_cloud_internal.h`
- `components/river_cloud/river_cloud_xiaozhi_session.c`

实现：

- 保持 steady-state 常态 drain 不变：
  - `RIVER_CLOUD_XIAOZHI_UPLINK_DRAIN_BURST_MAX` 仍为 `1`
- 只在 preview warmup 窗口内引入有上限的 catch-up：
  - `RIVER_CLOUD_XIAOZHI_UPLINK_PREVIEW_BURST_MAX` 从 `1` 提到 `3`
  - 仅当 ASR round 仍处于 preview warmup，且队列里确实还有后续帧时，才允许临时放宽 burst
- 成功发包后，如果 warmup 期间仍有 backlog：
  - 本地本轮 drain 会临时 bypass 一次 20 ms 帧间 pacing，继续追发下一帧
  - 同时累加 `preview_warmup_bypass_count`，方便后续和 `burst_max/backlog/first_partial` 对照
- 这样可以把启动阶段半拍到几拍的本地排队更快消掉，但不会把 steady-state uplink 改成长期突发发送。

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
- `preview_warmup_bypass_count` 在有启动积压的轮次不再长期为 `0`。
- `burst_max` 在 warmup 轮次可以上升到 `2~3`，但 steady-state 不长期突发。
- 首个 preview partial 更早到达，accept 前 uplink backlog 指标下降。

### Step X: endpoint soft-close relax

状态：已完成代码修复，待上板复测。

目标：

- 收掉“preview 还在追平，但端侧 320 ms hint-only local close 已经先超时”的问题。
- 给 server endpoint candidate 之后的 refresh / finalize 多留一轮补救空间，避免端侧过早结束 active stream。

范围：

- `components/river_cloud/river_cloud_internal.h`
- `components/river_cloud/river_cloud_xiaozhi_session.c`

实现：

- 将 `RIVER_CLOUD_XIAOZHI_ENDPOINT_SOFT_CLOSE_DEFER_MS` 从 `320` 提高到 `960`
  - 保持它仍显著小于 `RIVER_CLOUD_XIAOZHI_LOCAL_CLOSE_DEFER_MS=2000`
  - 目标是只放宽 hint-only endpoint close，不把 silent fallback 拖成长期等待
- `input_preview` 现在只要出现以下任一真实文本进展，就撤销 endpoint soft-close：
  - `event->text` 非空
  - `event->stable_prefix` 非空
- 这样即使 preview payload 还没把最终文本放进 `text`，只要稳定前缀已经开始长出来，也不会继续沿用旧的 soft-close 倒计时。

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
- `endpoint_soft_close_timeout` 触发频率下降。
- preview 仍在改善的轮次，不再轻易被端侧本地 close 抢先截断。
- 正常确实已经说完的轮次，仍能在可接受时延内结束，不出现明显挂起。

### Step W: no-ref reopen hold tighten

状态：已完成代码修复，待上板复测。

目标：

- 先从端侧入口收掉 false accept，优先削弱播放尾边 stale residual 重开 follow-up round 的概率。
- 命中你给的 turn3 模式：服务侧只有 120 ms 音频也被 accepted；端侧当前 no-ref reopen 门槛正好也是 120 ms，存在直接共振。

范围：

- `components/river_cloud/river_cloud_internal.h`

实现：

- 保持默认 duplex/quiet-window 路径不变：
  - `RIVER_CLOUD_XIAOZHI_OPEN_HOLD_FRAMES` 仍为 `2`
- 只收紧 no-ref reopen 路径：
  - `RIVER_CLOUD_XIAOZHI_NOREF_OPEN_HOLD_FRAMES` 从 `6` 提到 `12`
  - 也就是从 120 ms 连续语音提高到 240 ms 连续语音才允许重开 ASR
- 这样播放尾边百毫秒级 residual 即使还被 VAD 短暂打成 speech，也不会立刻重开 follow-up round。

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
- 120 ms 左右播放尾边 residual 不再轻易触发一轮新的 follow-up ASR。
- `accepted 后无 response.start` 的 silent/stale turn 数量应先下降。
- 正常完整 follow-up 语音仍然能进入 ASR，不应整体失灵。

### Step V: playback next-segment publish-order fix

状态：已完成代码修复，待上板复测。

目标：

- 收口 2026-04-28 17:12 新日志里的新主卡点：fast-launch `_0001` 收尾切到 main-dialogue `_0002` 时，started-ack 因 next segment 上下文半初始化而丢 `playback_id/segment_id`。
- 避免 `_0002` 无法进入 started/mark/fully-heard，最终把整轮 playback 挂到 `idle_timeout` 才被 transport close 收尸。

范围：

- `components/river_cloud/river_cloud_xiaozhi_playback_terminal_ack.inc`

实现：

- 复盘发现 `river_cloud_xiaozhi_playback_note_meta(...)` 在新建 segment slot 时，旧逻辑会先：
  - `segment->valid = true`
  - `g_river_cloud.xiaozhi_playback_segment_queue_truth.count++`
  - 然后才写入 `response_id/playback_id/segment_id/text/expected_duration_ms/is_last_segment`
- 这会让 downlink worker 在当前 head 恰好被 pop 的边界上，把一个“已经发布但尚未填完 ids”的 next slot 当作 current segment 使用，于是打出：
  - `xiaozhi playback ack started send failed: status=-1 response_id=resp_... playback_id=- segment_id=-`
- 现在把 publish 顺序改成：
  - 先 `memset` tail slot
  - 填完 `response_id/playback_id/segment_id/text/expected_duration_ms/is_last_segment`
  - 最后才 `segment->valid = true` 并 `count++`
- 这样 `_0001 -> _0002` 交接时，worker 只能看到“尚未发布的空 slot”或“已经完整填好的 next segment”，不会再读到 partial ids。

验证：

```bash
cd /root/ameba-river
git diff --check
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

期望：

- `Build done`。
- `_0002` 的 `audio.out.meta` 后，不再出现：
  - `xiaozhi playback ack started send failed: status=-1 ... playback_id=- segment_id=-`
  - `xiaozhi playback ack started queue failed: status=-1 ... playback_id=- segment_id=-`
- 应继续看到 `_0002` 的 started / mark 推进：
  - `xiaozhi playback ack started sent: ... segment_id=..._0002`
  - `xiaozhi playback ack mark sent: ... segment_id=..._0002`
- transport close 时 `last_fully_heard` 应推进到最后一段，不再长期停在 `_0001`。

### Step U: playback late completed audio drop

状态：已完成代码修复，待上板复测。

目标：

- 收口 2026-04-28 16:20 日志里的新主问题：最后一个 segment 已经 final mark 完成后，迟到 audio frame 仍反复取消 stop，导致 playback 在 `draining/playing` 之间抖动。
- 避免异常拉长的 playback 尾态继续污染 follow-up turn 时序，并放大后续 preview/accept/service-error 风险。

范围：

- `components/river_cloud/river_cloud_xiaozhi_playback_downlink_worker.inc`

实现：

- 新增 `river_cloud_xiaozhi_should_drop_late_completed_audio()`：
  - 仅在 `playback_terminal_open()`、`stop_pending=yes`、`playback_completed_ready()` 同时成立时触发；
  - 此时若再收到迟到 audio frame，端侧直接丢弃，不再写入 downlink ring，不再触发 `cancel_playback_stop()`。
- drop 时打印：
  - `xiaozhi late completed audio dropped: ...`
- drop 后立即重跑一次：
  - `maybe_complete_terminal_playback_after_progress("late_completed_audio")`
  - 确保 terminal-complete 收口还能继续推进到 idle。

验证：

```bash
cd /root/ameba-river
git diff --check
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

期望：

- `Build done`。
- 最后一个 segment 的 final mark 后，不再反复看到：
  - `draining -> playing reason=cancel_stop queued=1 segments=0`
  - `playing -> draining reason=arm_stop queued=1 segments=0`
- 若仍有迟到尾帧，应直接看到：
  - `xiaozhi late completed audio dropped: ...`
- playback 应更快回到 `idle`，后续 follow-up 不再被尾态抖动拉坏。
