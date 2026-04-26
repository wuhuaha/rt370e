# XiaoZhi Session Stability Execution Plan

Status: active
Last Updated: 2026-04-13
Branch: `kws`

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

## 编写建议

- 后续更新此计划时，优先刷新 “已知事实 / 风险 / 当前下一刀”，不要把它写成历史流水账。
- 只有在当前主目标切换时，才把它从 `.codex/active_plans.md` 的 primary plan 移走。
