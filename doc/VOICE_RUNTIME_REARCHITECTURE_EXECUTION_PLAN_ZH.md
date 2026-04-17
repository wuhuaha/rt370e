# Voice Runtime Re-Architecture Execution Plan

Status: active
Last Updated: 2026-04-17
Branch: `agent-server-v2`

## 1. 当前背景

- 当前主问题：
  - XiaoZhi 端到端主链已具备完整能力，但当前 runtime 仍存在系统性时序问题：
    - uplink 慢于实时，导致 preview / accept 延迟
    - downlink / playback 恢复抖动，导致卡顿和重缓冲风暴
  - 现有运行时边界不清：
    - `river_core` 不是唯一真相源
    - `river_voice` 与 `river_cloud` 曾长期双向耦合
    - `river_cloud_adapter.c` 同时承载 transport / policy / media / diagnostics
- 已知稳定基线：
  - SDK：`/root/ameba-rtos`
  - 分支：`agent-server-v2`
- 当前默认 SDK：
  - `/root/ameba-rtos`
- 与本计划强相关的现有文档：
  - [doc/VOICE_RUNTIME_ARCHITECTURE_REVIEW_ZH_2026-04-17.md](/root/ameba-river/doc/VOICE_RUNTIME_ARCHITECTURE_REVIEW_ZH_2026-04-17.md)
  - [doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md](/root/ameba-river/doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md)
  - [doc/XIAOZHI_SESSION_STABILITY_EXECUTION_PLAN_ZH.md](/root/ameba-river/doc/XIAOZHI_SESSION_STABILITY_EXECUTION_PLAN_ZH.md)

## 2. 目标

- 建立以 `river_core` 为唯一编排层的运行时架构
- 收紧 `river_voice` / `river_cloud` 边界，避免直接跨层调用
- 把 XiaoZhi uplink / downlink 从单体 adapter 中拆出清晰媒体模型
- 保留当前板端主链可运行，不做一次性大翻修
- 让每一步都能通过：
  - 静态检查
  - 编译
  - 板端日志验证

## 3. 非目标

- 不在本计划中一次性重写整个 XiaoZhi provider
- 不在本计划中替换现有 websocket 协议
- 不把所有全双工演进切片混成一个提交
- 不修改 `/root/ameba-rtos-1.2`

## 4. 约束 / Guardrails

- 保持 `wake -> realtime session -> TTS playback` 路径可回退
- 每一步都必须是：
  - 可编译
  - 可回滚
  - 可板端验证
- 优先修正所有权和媒体语义，再做大规模文件拆分
- 不为了“好看”引入过多抽象层或回调噪音

## 5. 已知事实

- 当前 `river_voice` 曾直接依赖 cloud：
  - `vad_probe`
  - `segment_sink`
  - `kws`
- 当前 `river_cloud_adapter.c` 体量约 `4k` 行，`river_xiaozhi_ws.c` 约 `4.9k` 行
- 当前最直接影响用户体验的设备侧问题是：
  - uplink pacing 慢于实时
  - playback write_failed / underrun 重缓冲风暴
- 当前服务端在 accept 后响应并不慢，慢点主要在设备 uplink 和首包 audio 到达前

## 6. 风险与未知项

- 若 `river_core` 真相源建立过慢，仍会长期存在双状态机问题
- 若 downlink 重构过早展开，容易和当前服务端供给抖动混在一起
- 若只拆文件不改所有权，复杂度不会真正下降
- 某些播放 write error 仍可能是 SDK/驱动层行为，需要板端复现验证

## 7. 执行切片

### Step A: 建立 core-owned cloud port，并修正 XiaoZhi uplink 媒体语义

目标：

- 先把 `river_voice -> river_cloud` 直接调用切断
- 同时把 uplink 从“单帧顺带发送”改为“有 in-flight/retry 语义的 bounded burst drain”

范围：

- [include/river/river_dialog_cloud_port.h](/root/ameba-river/include/river/river_dialog_cloud_port.h)
- [components/river_core/river_dialog_cloud_port.c](/root/ameba-river/components/river_core/river_dialog_cloud_port.c)
- [components/river_core/river_app.c](/root/ameba-river/components/river_core/river_app.c)
- [components/river_voice/river_voice_vad_probe.c](/root/ameba-river/components/river_voice/river_voice_vad_probe.c)
- [components/river_voice/river_voice_segment_sink.c](/root/ameba-river/components/river_voice/river_voice_segment_sink.c)
- [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc)
- [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)

完成标准：

- `river_voice` 内不再直接引用 `river_cloud_*`
- `river_app` 统一注册 cloud port
- uplink ready queue 显式包含 retry frame
- uplink worker 支持 bounded burst drain
- `BUSY/失败` 时当前帧不会隐式丢失
- `asr round finish` 日志直接暴露 pacing 指标：
  - `audio_ms`
  - `realtime_gap_ms`
  - `pace_pct`

验证：

```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'river_cloud_' components/river_voice
rg -n 'river_dialog_cloud_port|river_dialog_cloud_' \
  include/river/river_dialog_cloud_port.h \
  components/river_core/river_dialog_cloud_port.c \
  components/river_core/river_app.c \
  components/river_voice/river_voice_vad_probe.c \
  components/river_voice/river_voice_segment_sink.c \
  components/river_voice/river_voice_kws.cc
rg -n 'UPLINK_DRAIN_BURST_MAX|uplink_retry_valid|audio_ms=|realtime_gap_ms=|pace_pct=' \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_adapter.c \
  components/river_cloud/river_cloud_xiaozhi_session.c
```

期望结果：

- harness 输出：
  - `check_codex_harness: all checks passed`
- `git diff --check` 无格式错误
- build 输出包含：
  - `Build done`
- `components/river_voice` 下不再出现 `river_cloud_` 直接调用
- 新 port 接口和 uplink pacing 指标均可静态 grep 到

### Step B: 建立 `dialog runtime` 真相源

状态：

- 已落地（2026-04-17，Step 5.177）

目标：

- 让 `river_core` 显式维护：
  - session
  - input lane
  - output lane
  - playback lane
  - duplex fallback
- `interaction_state` 退化成只读视图

范围：

- `components/river_core/`
- `include/river/`

完成标准：

- 不再主要依赖 playback/asr 回调反推 coarse `phase`
- `turn_id / accept_reason / input_state / output_state` 有统一落点

已完成事实：

- 新增 `river_dialog_runtime`，由 `river_core` 统一维护：
  - boot
  - wake confirmed
  - ASR lifecycle
  - playback lifecycle
  - input/output lane
  - 派生 interaction state
- `river_app` 不再直接写 `interaction_state`
- `session_coordinator` 不再维护本地 `phase` / `asr_session_active` 真相源
- `river_cloud_adapter` 暴露通用 runtime snapshot，`river_core` 通过稳定接口
  吸收 provider/session/lane 事实

下一步焦点：

- 进入 Step C / Step D 的组合推进：
  - 先把 XiaoZhi downlink / playback 从单体 adapter 中继续抽出成清晰的
    runtime/media engine
  - 再重建 `write_failed / underrun / rebuffer` 恢复模型，避免当前 stop/start
    风暴反复打穿播放链

### Step C: 拆分 XiaoZhi adapter 的 transport / protocol / runtime / media

状态：

- 已部分落地（2026-04-17，Step 5.178）

目标：

- 减少单体 `river_cloud_adapter.c` / `river_xiaozhi_ws.c`
- 明确 transport、协议解析、provider runtime、uplink、downlink 边界

范围：

- `components/river_cloud/`

完成标准：

- transport 不直接操纵高层 turn policy
- uplink/downlink 有独立 engine 文件

已完成事实：

- 新增独立媒体运行时：
  - `components/river_cloud/river_cloud_xiaozhi_playback_runtime.c`
- XiaoZhi downlink / playback 的首个大块媒体职责已从
  `river_cloud_adapter.c` 抽离出去：
  - playback meta / segment queue
  - playback ACK progress
  - rebuffer / retry frame
  - decoder 准备
  - downlink worker
  - playback start / restart
- `river_cloud_adapter.c` 现已改为通过共享内部接口调用该媒体运行时，
  不再保留同一套 playback/downlink 本地重复实现
- playback runtime 已继续吸收下行播放终态语义：
  - `playback_output_active`
  - `playback_has_work`
  - `playback_abort`
- adapter 中 `transport_closed / network_lost / interrupt / bridge_close`
  已改为调用统一 playback runtime 终止入口，而不是手工拼接：
  - finalize
  - reset downlink
  - stop/interrupt stream
  - reset playback
- playback runtime 已继续吸收剩余 playback helper 所属权：
  - `clear_playback_meta_state`
  - `cancel_playback_stop`
  - `playback_note_duplex_ready`
  - `mark_playback_started`
  - `arm_playback_stop`
  - `reset_playback_state`
  - `reset_downlink_state`
- `river_cloud_xiaozhi_session.c` 已不再实现 playback 自身的
  reset/meta/stop helper
- `followup_timeout` 与 XiaoZhi `interrupt` 分支现在都通过 runtime
  playback predicate 判断媒体工作量，而不是直接拼原始播放字段
- playback runtime 已开始把“上游供给断粮”从硬 `write_failed` 恢复里拆开：
  - 新增 starvation 超时预算：
    - `RIVER_CLOUD_XIAOZHI_DOWNLINK_STARVED_REBUFFER_MS`
  - 新增 runtime-owned starvation watch：
    - `xiaozhi_downlink_starved_since_ms`
  - sustained `queued=0` gap 现在会在达到阈值后主动进入
    `xiaozhi_playback_starved` rebuffer 路径，而不是继续空跑到
    AudioTrack 硬失败
  - 已知最后一段已播完时不会误进入该 starvation rebuffer 路径
- playback service / dialog runtime 入口已继续把 recoverable churn 与 fatal
  playback fault 拆开：
  - 新增显式状态：
    - `RIVER_PLAYBACK_RECOVERING`
  - `AudioTrack_Write` 失败现在先进入 `recovering`，不再直接把 dialog
    runtime 推进到 fatal `playback_error`
  - `session_coordinator` 现在会区分：
    - `playback_recovering`
    - `playback_error`
    - `playback_state`
  - 当前 fatal playback 语义被收窄到 start/init/flush-restart 等真正本地
    无法继续的路径
- terminal `completed` 已继续从“本地 drain 成功”改为“最后一段已观察且已完整听完”：
  - 新增显式判定：
    - `river_cloud_xiaozhi_playback_last_segment_observed()`
    - `river_cloud_xiaozhi_playback_completed_ready()`
  - `tts_stop_pending` 在 `service inactive + queued=0` 时不再立刻关闭
    terminal；若最后一段尚未观察/听完，会继续等待 late tail
  - `audio.out.completed` 现在绑定：
    - last segment observed
    - local segment queue drained
    - `last_fully_heard_segment_id == last_segment_id`
- `cleared` 与 terminal-tail waiting 也已继续收口到 runtime 真相：
  - `audio.out.cleared` 现在只有在板端真的排队了 cleared ACK 时才算
    terminal truth；clear-before-start 不再伪造 `cleared`
  - 新增 runtime-owned tail-wait 状态：
    - `playback_terminal_waiting`
    - `playback_terminal_wait_reason`
  - terminal-tail waiting 已通过：
    - cloud runtime snapshot
    - dialog runtime snapshot
    暴露给真相源和诊断层
  - 当前等待原因显式区分：
    - `await_last_segment_meta`
    - `await_segment_queue_drain`
    - `await_last_segment_tail`

下一步焦点：

- 继续把 interrupt / local-clear / network-loss 的 terminal close policy
  从 adapter 分支逻辑收口成更少的 runtime-owned cause
- 继续把 XiaoZhi session / turn transport 语义从 adapter 中拆分出来
- 让 adapter 进一步退化为 provider 生命周期与高层策略装配层

### Step D: 重建 downlink / playback 恢复模型

目标：

- 把“服务端供给抖动”和“本地设备写入错误”区分开
- 降低 `write_failed -> stop/start` 风暴

范围：

- `components/river_cloud/`
- `components/river_voice/river_playback_service.c`

完成标准：

- 不再把一次写失败直接放大成整流重启
- rebuffer 策略可观测、可度量

### Step E: 统一 turn timeline 与板端验证

目标：

- 每轮 turn 形成统一 timing chain
- 快速区分问题位于：
  - uplink
  - accept/thinking
  - first audio
  - playback

范围：

- `components/river_core/`
- `components/river_cloud/`
- `components/river_diag/`

完成标准：

- `river xiaozhi status` 能输出统一时间链路快照
- 板端日志可直接定位慢点和卡顿来源

## 编写建议

- 优先推进“唯一真相源”和“媒体语义收口”，不要先做纯文件拆分。
- 每一步都要在 `.codex/changes.md`、`.codex/verification.md` 和 active context 中留下闭环记录。
