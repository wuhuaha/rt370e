# Voice Runtime Re-Architecture Execution Plan

Status: active
Last Updated: 2026-04-18
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
- XiaoZhi session-runtime 也已开始继续吸收 adapter 可见的会话事实读路径：
  - 新增 exported getter：
    - `river_cloud_xiaozhi_listening_active()`
    - `river_cloud_xiaozhi_conversation_window_active()`
    - `river_cloud_xiaozhi_conversation_window_remaining_ms(...)`
  - adapter 侧以下入口已改为消费这些 runtime truth，而不是直接读
    `xiaozhi_listening/xiaozhi_window_active`：
    - transport active
    - config busy guard
    - transport-closed diagnostics
    - status dump
    - public runtime snapshot / conversation-window getter
- `open_and_listen` 成功态的会话归一也已继续从 adapter 执行体下沉：
  - 新增 exported runtime policy：
    - `river_cloud_xiaozhi_apply_open_and_listen_session_policy()`
  - adapter 现在只在 request wrapper 成功后调用该 policy，不再在 transport
    executor 内直接写 `xiaozhi_listening = true`
  - session 侧 follow-up / wake admission 调用点也不再各自手工补一次该
    policy，避免成功态归一分散在多个入口
- adapter 侧剩余的 session 诊断读取也已继续收口：
  - 新增 exported getter：
    - `river_cloud_xiaozhi_local_close_pending()`
    - `river_cloud_xiaozhi_local_close_remaining_ms(...)`
    - `river_cloud_xiaozhi_listen_stop_pending()`
  - `river_cloud_adapter_dump_status()` 不再直接读：
    - `xiaozhi_local_close_pending`
    - `xiaozhi_local_close_deadline_ms`
    - `xiaozhi_listen_stop_pending`
    这些状态，而是消费 runtime 导出的只读事实
- preview observation 的 helper 所有权也已继续从 adapter 移走：
  - `river_cloud_xiaozhi_note_preview_observation(...)`
  - `river_cloud_xiaozhi_copy_optional_text(...)`
  现都由 session runtime 提供实现
  - adapter 的 `input_speech_start / input_preview / input_endpoint` 分支
    继续只负责 transport event 分发，不再持有 preview-state helper body
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
- downlink worker 的本地致命数据面异常也已继续收口到同一条 typed
  playback abort 入口：
  - `frame_oversize`
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
- terminal close 现在也已继续拆成“两条真相线”：
  - playback runtime 新增独立本地终态结果：
    - `playback_terminal_state`
  - 这条线和协议 ACK 真相：
    - `playback_terminal_ack`
    彻底分离
  - 典型本地终态结果现在包括：
    - `local_completed`
    - `local_cleared`
  - 因而以下场景不再被混成“已 truthful completed/cleared”：
    - `audio.out.completed` 排队失败但本地已经播完
    - clear-before-start 或没有 `last_fully_heard_segment_id` 的本地清理
  - 该终态结果也已通过：
    - cloud runtime snapshot
    - dialog runtime snapshot
    暴露给 core 真相源和诊断日志
- adapter 侧分散的 terminal close 参数拼装也已继续收口：
  - playback runtime 新增统一 typed cause reducer
  - 目前已覆盖的入口包括：
    - `interrupt`
    - `transport_closed`
    - `network_lost`
    - `bridge_close`
  - adapter 不再自行拼：
    - `clear_reason`
    - `stream_reason`
    - `interrupt_stream`
    三元组，而是只上传 `cause + detail_reason`
  - 这让后续 terminal policy 调整可以稳定落在 playback runtime 一处
    完成，而不是继续在 adapter 分支里复制终态规则
- XiaoZhi `tts_start` 的 keep-open / round-close 语义也已继续收口到
  session runtime：
  - 新增 exported helper：
    - `river_cloud_xiaozhi_apply_tts_start_round_policy()`
  - 该 helper 负责：
    - duplex ready 判定
    - keep-open logging
    - fallback close / semantic fallback 记录
  - adapter 现在只保留：
    - finalize pending text
    - 调用 runtime helper
    - cancel playback stop 的 transport 尾部胶水
  - 该 helper 现已成为唯一的 session-runtime TTS-start policy surface，
    相关冗余 predicate 已删除
  - 后续若要继续瘦身 adapter，可把这条 `tts_start` 分支的剩余胶水
    再拆成更细的 session-runtime/transport 边界
- XiaoZhi `tts_stop` 的 round-close 语义也已继续收口到 session runtime：
  - 新增 exported helper：
    - `river_cloud_xiaozhi_apply_tts_stop_round_policy()`
  - 该 helper 负责：
    - finalize pending text
    - local round close
    - post-TTS silence window touch
    - playback stop arm
  - adapter 现在只剩下调用这个 runtime helper，不再自己拼 stop-path
    policy body
- XiaoZhi local-close `reopen_overlap` 语义也已继续收口到 session runtime：
  - 新增 exported helper：
    - `river_cloud_xiaozhi_apply_reopen_overlap_round_policy()`
  - 该 helper 负责：
    - 记录 round finish request
    - 触发 local round close
  - adapter reopen 分支现在只剩下调用这个 overlap helper
- XiaoZhi `LLM` terminal local-close 语义也已继续收口到 session runtime：
  - 新增 exported helper：
    - `river_cloud_xiaozhi_apply_llm_round_policy()`
  - 该 helper 负责：
    - finalize pending text
    - 触发 local round close
  - adapter `RIVER_XIAOZHI_EVENT_LLM` 分支现在只剩下调用这个 runtime
    helper
- XiaoZhi `post_stop_result` local-close 语义也已继续收口到 session runtime：
  - 新增 exported helper：
    - `river_cloud_xiaozhi_apply_post_stop_result_round_policy()`
  - 该 helper 负责：
    - 根据已完成的 pending text / final 事实决定 local round close
  - adapter listen-stop completion path 现在只剩下调用这个 runtime helper
- XiaoZhi `transport_closed` terminal cleanup 语义也已继续收口到
  session runtime：
  - 新增 exported helper：
    - `river_cloud_xiaozhi_apply_transport_closed_terminal_policy()`
  - 该 helper 负责：
    - finalize pending text
    - round finish
    - local window abort
    - playback abort
    - transport state reset
  - adapter `RIVER_XIAOZHI_EVENT_SESSION_CLOSED` 分支现在只剩下调用这个
    runtime helper
- XiaoZhi `network_lost` terminal cleanup 语义也已继续收口到
  session runtime：
  - 新增 exported helper：
    - `river_cloud_xiaozhi_apply_network_lost_terminal_policy()`
  - 该 helper 负责：
    - playback abort
    - round finish
    - transport state reset
  - adapter `river_cloud_adapter_notify_network_lost()` 现在只剩下调用这个
    runtime helper，并保留 close-session/state-sync 的 transport 尾部动作
- XiaoZhi `bridge_close` terminal cleanup 语义也已继续收口到
  session runtime：
  - 新增 exported helper：
    - `river_cloud_xiaozhi_apply_bridge_close_terminal_policy()`
  - 该 helper 负责：
    - playback abort
    - round finish
    - transport state reset
  - adapter `river_cloud_asr_audio_close()` 现在只剩下：
    - active-stream finish
    - close-session
    - Opus codec close
    这些桥接/transport 尾部动作
- XiaoZhi `listen_stop` completion round-close 语义也已继续收口到
  session runtime：
  - 新增 exported helper：
    - `river_cloud_xiaozhi_apply_listen_stop_completion_round_policy()`
  - 该 helper 负责：
    - clear `listen_stop_pending`
    - local-close pending 时转交 `post_stop_result`
    - 否则统一完成 `session_closed / round_finish`
  - adapter `river_cloud_xiaozhi_finalize_listen_stop_if_ready()` 现在只剩下：
    - uplink drained 判定
    - `listen_stop` transport request
    这些 transport 尾部动作
- XiaoZhi ASR round lifecycle 语义也已继续收口到 session runtime：
  - 新增 exported helper：
    - `river_cloud_xiaozhi_round_begin()`
    - `river_cloud_xiaozhi_round_note_packet_sent()`
  - 这些 helper 现在统一拥有：
    - round begin
    - first packet timestamp
    - packet_sent 计数
  - adapter 现在只在 uplink-send 和 reopen-open 路径调用它们，不再定义本地实现
- XiaoZhi TTS interrupt 语义也已收口到 session runtime：
  - 新增 exported helper：
    - `river_cloud_xiaozhi_interrupt_tts()`
  - 它现在统一拥有：
    - 是否存在可中断的 playback/session work
    - interrupt 导致的 playback abort
    - interrupt 导致的 transport abort request
    - interrupt 请求日志
  - adapter `river_cloud_adapter_interrupt_tts_with_reason()` 现在只保留
    provider dispatch，不再本地决定 XiaoZhi interrupt 语义
- XiaoZhi follow-up reopen round-start 语义也已收口到 session runtime：
  - 新增 exported helper：
    - `river_cloud_xiaozhi_start_followup_round()`
  - 它现在统一拥有：
    - `followup_transport_unavailable` 本地窗口 abort
    - `reopen_overlap` 本地 close 终态衔接
    - overlap round finish 与新 round begin 的时序
  - adapter reopen-open 路径现在只保留：
    - pre-roll frame 数量计算
    - pre-roll replay / current frame replay
    - stream_open 计数与日志
- XiaoZhi `network_lost` / `bridge_close` terminal close-session 尾动作也已
  收口到 session runtime：
  - terminal-policy helper 现在统一拥有：
    - terminal state 收敛
    - transport reset
    - `request_close_session()` 尾动作
  - adapter 对应路径现在只保留：
    - runtime helper 调用
    - 非 terminal-policy 的外围清理或 state sync
- XiaoZhi idle reopen gate 也已进一步收口到 session runtime：
  - runtime helper 现在统一拥有：
    - wakeword-window reopen eligibility 判断
    - no-ref reopen rearm / guard gating
    - open-hold speech-frame accumulation 阈值
  - adapter reopen-open 路径现在只保留：
    - `listen_stop_pending` busy guard
    - pre-roll frame 数量读取
    - pre-roll replay / current frame push
    - stream_open 计数与日志
- XiaoZhi listen-stop completion 也已收口到 session runtime：
  - runtime helper 现在统一拥有：
    - uplink drain-complete 判定
    - `request_listen_stop()` 时序
    - listen-stop completion round policy
  - adapter uplink/stream-finish 路径现在只保留：
    - queued uplink frame 数采样
    - pending accum bytes 采样
    - runtime helper 调用
- XiaoZhi reopen-time `listen_stop_pending` busy gate 也已收口到 session runtime：
  - runtime reopen helper 现在统一拥有：
    - listen-stop drain pending 时的 reopen busy gate
  - adapter reopen-open 路径现在不再直接读取：
    - `xiaozhi_listen_stop_pending`
- XiaoZhi `open_and_listen` 成功后的 stop-intent clear 也已收口到 session runtime：
  - runtime open/listen success policy 现在统一拥有：
    - follow-up reopen success 后的 stop-intent clear
    - wake admission success 后的 stop-intent clear
  - adapter `CTRL_OPEN_AND_LISTEN` transport 路径现在只保留：
    - open session / send listen_start
- XiaoZhi uplink keepalive gate 也已收口到 session runtime：
  - runtime helper 现在统一拥有：
    - queued uplink drain 后因 stop-intent 维持 I/O 活跃的 keepalive gate
  - adapter `uplink_active()` 现在只保留：
    - queued uplink frame 数采样
- XiaoZhi uplink send-ready gate 也已收口到 session runtime：
  - runtime helper 现在统一拥有：
    - transport session open + listening 的 uplink send-ready predicate
  - adapter control/uplink 路径现在统一调用：
    - `river_cloud_xiaozhi_uplink_send_ready()`

下一步焦点：

- 继续把 remaining reopen / interrupt / close-session 触发路径的 terminal
  ownership 收口进同一个 runtime-owned cause family
- 让 `dialog runtime` / `session coordinator` 后续优先消费 runtime 导出的
  terminal truth，而不是继续依赖 `tts_stop_pending + playback_active`
  组合猜测终态
- `dialog runtime` 已开始显式消费：
  - `playback_terminal_state`
  - `playback_terminal_waiting`
  来抑制 stale `output_lane=speaking` 对交互态的误导
- `session coordinator` 的 barge-in interrupt gate 已切到
  `dialog runtime` snapshot，不再回退依赖 playback service 的局部 active
  状态
- XiaoZhi local round close 已开始从 adapter 收口到 session runtime：
  - `local_resolved`
  - `server_response_started`
  - `round_finish`
- `endpoint soft close / local close defer` 的状态辅助函数也已开始迁入
  session runtime
- `active stream finish` 的 state commit 与 `endpoint soft-close timeout`
  判定也已迁入 session runtime；adapter 只保留 transport tail
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

已完成事实（进行中）：

- `write_failed` 现已先走同轨 `flush/restart` 恢复，再在失败时退回
  `stop/start`
- playback service 已可从 `RIVER_PLAYBACK_RECOVERING` 重新回到
  `RIVER_PLAYBACK_RUNNING`
- 下行本地预取/重缓冲门限已按当前板端问题重建：
  - downlink ring：`32 -> 96`
  - 首播起播门限：`12 -> 16`
  - 重缓冲再起播门限：`18 -> 28`
  - playback target/fallback buffer：`6/4 -> 12/8`
- playback runtime 现已把 `audio.out.meta` 到达节奏纳入同一条 runtime
  真相：
  - 记录：
    - `playback_last_meta_ms`
    - `playback_last_meta_gap_ms`
    - `playback_prefetch_target_ms`
  - `prefetch_target_ms` 取：
    - 当前 segment `expected_duration_ms`
    - 最近一次 `meta_gap_ms`
    中较大者，再附加固定 margin，并受 ring 可播放预算上限约束
- `upstream gap` 触发的 starvation rebuffer 不再只看一个小固定等待值：
  - 当前等待窗口会跟随 `prefetch_target_ms` / 起播门限自适应抬高
  - repeated rebuffer 会继续抬高再起播门限，避免短队列反复打穿
- `river xiaozhi status` / 运行时诊断现在可以直接观察到新的下行真相：
  - `target_ms`
  - `meta_gap_ms`
  - `rebuffer_count`
- recoverable playback churn 已继续从“局部 active 抖动”提升为显式 runtime
  真相：
  - cloud runtime snapshot 新增：
    - `playback_rebuffer_pending`
  - dialog runtime snapshot 新增：
    - `playback_cloud_active`
    - `playback_rebuffer_pending`
    - `playback_recovering`
  - `playback_active` 现已由：
    - service active
    - cloud playback active
    - runtime recovering
    共同派生，而不是只依赖瞬时本地活跃位
- local playback service 也已同步收紧语义：
  - `RIVER_PLAYBACK_RECOVERING` 现在仍计入 active playback lane
  - 同轨 recover-first 的 flush/restart 不再被 VAD/AEC/dialog runtime 当成
    一次真实停播
- recover-first 的 fallback 归因也已继续下压：
  - 当 `RIVER_PLAYBACK_RECOVERING` 下的同轨 restart 失败时，playback
    service 不再先上报 fatal `RIVER_PLAYBACK_ERROR`
  - 当前行为改为：
    - 释放失败 track
    - 回到可 fresh-start 的 `IDLE`
    - 由下行 runtime 保留 `rebuffer_pending` 并等待下一次正常 start
  - XiaoZhi downlink runtime 也不再在这条支路上额外调用一次冗余 `stop`
- playback service / downlink 之间的“engaged but needs a new start”边界也已显式化：
  - 新增播放状态：
    - `RIVER_PLAYBACK_RESTART_PENDING`
  - recover fallback 现在不再直接落到普通 `IDLE`，而是进入该中间态
  - 该状态语义是：
    - 当前播放链仍属于一次 recoverable playback churn
    - 本地坏 track 已拆掉
    - 需要下一次正常 `start_stream()` fresh-start
  - dialog runtime / session coordinator 已把它归入统一
    `playback_recovering` 家族
  - XiaoZhi downlink worker 也已显式把：
    - `IDLE`
    - `RESTART_PENDING`
    视为需要 fresh-start 的本地状态，而不是被新的 active 语义卡死
- AEC / duplex gate 也已开始与该状态对齐，不再把它误折叠为通用
  `ref_missing/ref_idle`：
  - 新增 AEC gate reason：
    - `RIVER_VOICE_AEC_GATE_BLOCKED_PLAYBACK_RESTART_PENDING`
  - 新增 duplex ready reason：
    - `RIVER_VOICE_DUPLEX_READY_PLAYBACK_RESTART_PENDING`
  - XiaoZhi fallback 原因现在可直接打印：
    - `half_duplex_restart_pending`
  - fixed-dsb AECM 统计也新增：
    - `restart_pending`
    计数，便于板端确认恢复空窗是否仍在被误判成参考链缺失
- cloud/runtime 也已开始显式导出“播放链仍占用”的统一真相，而不是让
  adapter / core 各自拼接：
  - 新增 runtime truth：
    - `playback_lane_engaged`
  - XiaoZhi playback runtime 现统一归并：
    - playback output active
    - rebuffer pending
    - playback-service active states
  - adapter transport active / capture-held policy 与 dialog runtime playback
    派生现在都改为消费这条统一 truth
- adapter 中剩余的 `open_hold` / `no_ref reopen` helper ownership 也已开始
  下沉到 session runtime：
  - `river_cloud_xiaozhi_open_hold_frames_required()`
  - `river_cloud_xiaozhi_no_ref_reopen_ready()`
  - `no_ref_reopen_ready()` 现在会直接消费：
    - `playback_lane_engaged`
    从 helper 内部拒绝仍被占用的播放链重开口
- `capture held during playback` 这条高频 capture 判定也已开始 helper 化：
  - 新增：
    - `river_cloud_xiaozhi_capture_held_by_playback(...)`
  - 该 helper 内部统一归并：
    - `playback_lane_engaged`
    - `duplex_fallback_reason`
  - adapter 现在只负责消费结果并记录日志

下一步焦点：

- 继续检查 reference-service / duplex gate 是否仍会在
  `restart_pending` 期间误触发：
  - capture reopen
  - no-ref fallback churn
  - follow-up / speaking 抖动
- 继续把“播放链占用”和“参考链已就绪”从更高层 runtime / cloud bridge
  语义中完全拆开，避免 recovery 空窗重新被其他模块折叠成：
  - `playback inactive`
  - `reference missing`
- 继续把 adapter 中剩余的 capture-open / no-ref reopen 决策下沉到
  session/playback runtime helper，避免 adapter 同时掌握：
  - duplex fallback reason
  - reopen rearm guard
  - playback lane occupied truth
- 继续把 `capture held during playback` 这类高频分支也改造成 typed helper /
  reducer，让 adapter 从“判定者”进一步退化成“调用者”
- 继续审视 `tts_start keep/fallback` 这类仍在 adapter 的 duplex gate 决策，
  让它们也与 session/runtime helper 采用同一 ownership 模式
- 让 `dialog runtime` / `session coordinator` / cloud bridge 最终都只消费
  一条统一的 playback-runtime 真相，而不是再从局部 active/idle 信号二次猜测

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
