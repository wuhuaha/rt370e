# Voice Runtime Re-Architecture Execution Plan

Status: active
Last Updated: 2026-04-22
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

### 1.1 最新进展

- `Step 5.386`
  - `dialog runtime` 现在正式把 typed error source 外显成 snapshot 级别的
    `error_kind`
  - 新增 `river_dialog_error_kind_t`：
    - `NONE`
    - `ASR`
    - `LOCAL_PLAYBACK`
    - `MIXED`
  - `refresh_error_recovering_locked()` 现在统一同时派生：
    - `error_recovering`
    - `error_kind`
  - `dialog_runtime_dump_status()` 也已升级为：
    - `error=yes/no/<kind>`
  - 这一步继续把 `dialog runtime` 从聚合 bool 推进成 typed truth export
- `Step 5.385`
  - `dialog runtime` 现在只会在 local playback event 已确认属于当前
    dialog-owned stream 时，才在 `IDLE` 上清理：
    - `local_playback_stream_owned`
    - `local_playback_stream_name`
  - owner 清理从 `should_absorb` 判断之前移动到了判断之后
  - 这意味着 foreign playback stream 的 `IDLE` 事件不再先把 dialog runtime
    当前 owned stream tracking 擦掉
  - 这一步继续把 local playback ownership 真相收口到：
    - 只消费本 dialog stream 的事件
- `Step 5.384`
  - `dialog runtime` 已把内部 `error_recovering` 拆成两路 typed source：
    - `asr_error_recovering`
    - `local_playback_error_recovering`
  - 新增 `refresh_error_recovering_locked()`，对外导出的
    `snapshot.error_recovering` 现在统一由 typed source 聚合
  - cloud snapshot 一旦重新可用，会主动清掉
    `local_playback_error_recovering` shadow，避免 fallback 时代留下的本地
    playback error 挂在 cloud/dialog 真相上
  - 这一步继续把 `dialog runtime` 从 coarse error bool 推进成
    typed internal truth
- `Step 5.383`
  - `dialog runtime` 新增 `local_playback_shadow_drives_truth_locked()`
  - local playback event 在 cloud runtime 已可用时，仍会保留本地 playback
    诊断 shadow，但不再继续直接：
    - `refresh_playback`
    - 改写 `error_recovering`
    - 清理 `tts_interrupt_requested`
  - 这意味着本地 playback `RUNNING/IDLE/ERROR` 事件不再在 cloud/dialog
    真相已在场时：
    - 把其他来源的 `error_recovering` 误清掉
    - 或在 cloud recovery truth 尚未同步前，先把交互态误推成
      `error_recovering`
  - 这一步继续把 `dialog runtime` 收口到：
    - cloud truth 优先
    - local playback 仅在 cloud 不可用时兜底
- `Step 5.382`
  - voice runtime 的 generic playback AEC gate 现在也消费 dialog runtime
    的 playback-lane truth，而不再只看本地 playback-service `active`
  - 新增：
    - `dialog_snapshot_capture(...)`
    - `dialog_playback_lane_engaged(...)`
  - 当本地 playback-service 短暂 inactive，但 dialog runtime 仍认定：
    - playback lane engaged
    - 或 playback recovering
    - 或 playback turn active
    时，AEC path 不再立刻退回 `block_playback`
  - `restart_pending` 与 generic playback gate 现在共用同一份
    dialog snapshot
  - 这一步继续把 duplex/AEC 的播放占用判定收口到 dialog runtime 真相源
- `Step 5.381`
  - XiaoZhi playback backend truth 现在只对 owned stream 承认
    `backend_restart_pending`
  - `playback_backend_state()` 不再在 ownership 判定之前就无条件把 raw
    `RESTART_PENDING` 直接映射成本流 backend truth
  - foreign stream 的 restart/recover 不再污染 XiaoZhi 本流的 backend 判定
  - 这一步继续把 downlink/playback runtime 的 backend 真相收回到
    owned-stream scoped typed truth
- `Step 5.380`
  - voice runtime 的 `restart_pending` AEC gate 不再无条件依赖本地
    `playback_state`
  - 新增：
    - `restart_pending_quiet_phase(...)`
    - `restart_pending_requires_block()`
  - `restart_pending` 是否仍应硬阻塞，现在先看 dialog runtime snapshot：
    - playback lane 是否仍 engaged
    - backend truth 是否仍是 `restart_pending`
    - 是否已进入 `prefetching/rebuffering/waiting_segment` quiet recovery window
  - quiet recovery window 内，AEC path 不再因为本地 service 仍报
    `RESTART_PENDING` 就被提前 reset
  - 这一步继续把 duplex/AEC 恢复门控从 playback-service coarse state
    收回到 dialog/runtime 真相源，降低恢复空窗里的 reset / reopen 抖动
- `Step 5.379`
  - playback runtime 现在把 backend-aware 起播门限继续拆分成：
    - detached cold start
    - restart-pending restart
  - 新增 `downlink_start_threshold_for_backend(...)`
  - `RIVER_CLOUD_PLAYBACK_BACKEND_RESTART_PENDING` 重新起播时不再一律等待
    cold-start `start_frames`
  - restart-after-recover 现在复用 attached-resume 门限：
    - `min(start_frames, buffer_frames)`
  - 这一步继续把 playback start policy 从 coarse backend state 中解耦，
    避免 recover/restart 后再次回退成保守的冷启动排队
- `Step 5.378`
  - playback runtime 现在显式区分：
    - backend 仍保有 turn / restart 语义
    - backend 底层 stream 是否真的 attached
  - `RIVER_CLOUD_PLAYBACK_BACKEND_RESTART_PENDING` 不再被 stop/flush/abort
    边界误当成 attached stream
  - `pending_stop` 现在会把 `restart_pending` 与其他 non-attached backend
    统一按 queue drop / terminal ack / runtime reset 处理
  - `transport_reset` / `session_start` / `segment_gap_hold` 也不再对已经脱离
    硬件的 backend 再次 stop/flush
  - 这一步继续把 playback runtime 从 coarse playback-service state 中解耦，
    避免 `restart_pending` 重新污染 attached stop/start 语义
- `Step 5.377`
  - downlink/playback runtime 现在显式拆分：
    - cold start threshold
    - attached resume threshold
    - actual playback buffer budget
  - playback start 时会锁存本次真正下发给 playback service 的
    `buffer_frames`
  - attached hold / rebuffer resume 不再一律等待 cold-start `start_frames`
  - 当前 attached resume 门限统一变成：
    - `min(start_frames, buffer_frames)`
  - playback dump / paused-resume 日志现在会直接暴露：
    - `start`
    - `resume`
    - `buffer`
  - 这一步继续把 downlink/playback 恢复语义从“冷启动保守门限”拆成
    runtime-owned attached resume truth，减少 attach 后恢复时不必要的排队等待
- `Step 5.376`
  - playback rebuffer 恢复决策现在统一收口到
    `request_playback_rebuffer_recovery(...)`
  - attached recover-first 的适用范围扩大到：
    - `WRITE_FAILED`
    - `UPSTREAM_STARVED + CURRENT_SEGMENT`
    - `UPSTREAM_STARVED + WAITING_NEXT_SEGMENT`
  - `maybe_rebuffer_starved()` 不再默认先走 detached `stop_rebuffer`
  - timer-starved / write-failed 两条恢复路径现在共用同一套恢复选择与回退语义
  - 这一步继续压缩 residual `stop/start` 恢复分叉，把 current-segment
    starvation 也推进到 attached recovery-first
- `Step 5.375`
  - playback runtime / dialog runtime 新增 typed playback hold truth：
    - `RIVER_CLOUD_PLAYBACK_HOLD_SEGMENT_GAP`
  - cloud snapshot 现在会显式导出 `playback_hold_kind`
  - `owned_paused + waiting_segment + !rebuffer_pending` 不再继续隐式复用
    `recovering` 语义，而是被单独标成正常段间 hold
  - dialog runtime 现在不再把 `OWNED_PAUSED` 一律解释成
    `playback_recovering`
  - output continuity 继续保留，但恢复/错误语义不再被正常 segment-gap hold
    污染
  - 这一步开始把“attached hold 的含义”从 backend state 猜测，推进成
    dialog/runtime 可消费的 typed truth，为后续继续拆 residual
    stop/start 恢复分叉做准备
- `Step 5.374`
  - `segment_gap_pause` 现在优先走 attached flush-hold，而不是默认 detached
    stop
  - 当前进入 `waiting_next_segment` 且 queue 见底时：
    - 先尝试 `flush_stream_ex("xiaozhi_segment_gap_pause")`
    - flush 失败才回退到 detached stop
  - downlink worker 现在会在：
    - backend=`owned_paused`
    - refill `>= start_frames`
    时显式 resume 本地 playback runtime，而不是一直空等
  - transport reset / session start 也会先 stop 已 attach 的 owned backend，
    避免 segment-gap attached pause 在 reset 边界遗留悬挂 backend
  - 这一步开始把 segment-gap 从 detached stop/start 模型推进到 attached
    hold/resume 模型，继续削减段间晚到导致的 backend restart 抖动
- `Step 5.373`
  - `maybe_rebuffer_starved()` 不再在 `WAITING_NEXT_SEGMENT` 窗口里提前触发
    upstream-starved rebuffer
  - 当 runtime-owned supply truth 已明确进入 `waiting_next_segment` 时：
    - 不再因为 low-water / wait-ms 达标就走
      `xiaozhi_playback_starved -> stop/rebuffer`
    - 改为清掉 starvation watch，继续让 segment-gap 路径接手
  - 这一步继续把“段间晚到”从 generic upstream-starved rebuffer 中拆出去，
    避免当前段尾部低水位就被提前升级成 stop/start 风暴
- `Step 5.372`
  - `write_failed` 路径现在会把 runtime-owned supply truth 一起带入恢复决策
  - 当一次 `write_failed` 被归类为 `UPSTREAM_STARVED`，且当前 supply 已明确是
    `WAITING_NEXT_SEGMENT` 时：
    - 不再默认 detached `stop_rebuffer`
    - 改为优先 attached `service_recover`
  - `write_failed` 日志现在也会打印当前 supply kind，便于板端确认当前究竟是：
    - 当前段内断流
    - 段间晚到
    - terminal tail
  - 这一步继续把 `underrun/write_failed -> stop/start` 风暴里的“段间晚到”
    从统一 stop/restart 模型中拆出去，优先尝试 attached recovery
- `Step 5.371`
  - `audio.out.completed` ACK 已不再保留回退到“当前 playback meta 的
    response/playback 上下文”这条旧路径
  - completed 终态现在只消费 terminal last-segment lineage：
    - `response_id`
    - `playback_id`
    - `segment_id`
  - playback dump 也新增了 terminal-context 观测面，便于板端确认 completed
    当前究竟会回报哪一条 playback lineage
  - 这一步继续把 playback truth 链上的 completed 终态从 current-meta shadow
    收口到 runtime-owned terminal truth，减少 response/playback rollover 时的
    错 ACK 风险
- `Step 5.370`
  - XiaoZhi playback runtime 现在会把最后一个 fully-heard segment 的
    `response_id / playback_id / segment_id` 一起保存成 typed heard context
  - `audio.out.cleared` ACK 不再继续拿“当前 playback meta 的 response/playback
    上下文”去拼 cleared 事实，而是直接消费这份 fully-heard segment context
  - playback dump 也新增了 heard-context 观测面，便于板端确认：
    - 当前清理/截断要回报给服务侧的究竟是哪一条 playback lineage
  - 这一步继续把 playback truth 链上的 cleared 终态从 current-meta shadow
    收口到 runtime-owned heard-segment truth，减少新 response / 新 meta 覆盖旧
    context 时的错 ACK 风险
- `Step 5.369`
  - 已回灌 `/root/agent-server` 2026-04-21 主线语音进展到设备侧计划：
    - `Realtime Session Core` 的
      `input_state / output_state` 双轨状态已是服务侧主线现实
    - `server_endpoint` 已推进为 preview-capable runtime 的默认主路径候选
    - `internal/voice` 已真正拥有：
      - preview
      - endpoint
      - interruption
      - speech planning
      - playback truth
    - playback truth 主链已明确依赖：
      - `audio.out.started`
      - `audio.out.mark`
      - `audio.out.cleared`
      - `audio.out.completed`
    - 当前服务侧持续推进重点已收敛为：
      - `preview_first_partial / accept / interrupt_cutoff` 回归基线
      - `accepted_turn -> first_audio`
      - dedicated semantic judge lane
  - 这意味着端侧后续不再把服务侧 `S1`~`S4` 当成主阻塞前提，剩余重构优先级改为：
    - 先重建 downlink / playback 真相链与恢复模型
    - 再继续收口 `dialog runtime` 唯一真相源
    - 再把 duplex / capture / AEC gate 切到 runtime-ready truth
    - 最后再做更激进的 duck-first / keep-listening 行为优化
- `Step 5.368`
  - XiaoZhi transport 侧已移除冗余诊断 shadow
    `last_playback_meta_valid`
  - transport playback-meta 的 `valid=` 现在直接由 cached meta context
    推导：
    - `last_response_id`
    - `last_playback_id`
    - `last_segment_id`
  - `last_playback_is_last_segment` 继续只表示最近一条 `audio.out.meta` 的
    event-local fact，不再继续和 validity shadow 混用
  - 这一步继续把 transport 诊断层的 playback-meta validity 真相收口到已有
    的 typed meta context
- `Step 5.367`
  - XiaoZhi playback runtime 已移除残留的 coarse
    `xiaozhi_playback_meta_valid` shadow
  - 当前 playback meta 是否有效，现只由 typed segment context 提供：
    - `response_id`
    - `playback_id`
    - `segment_id`
  - `playback_current_meta_is_last_segment()` 与 playback dump 的 `valid=`
    都不再继续先读一个 coarse runtime bool
  - 这一步继续把 playback runtime 对“当前 meta 是否有效”的解释权，从 shadow
    bool 收口到 runtime-owned response/playback/segment context
- `Step 5.366`
  - cloud/dialog runtime snapshot 现在正式导出 `playback_turn_active`
  - `dialog_runtime` 新增 `playback_turn_retains_output_turn_locked()`：
    - lane 仍 occupied 时，继续只让 `rebuffering` 保留 output-turn ownership
    - lane 已释放但 runtime 仍声明 `playback_turn_active=yes` 时，只有
      `output_lane=speaking` 且未进入 suppress-speaking 的 terminal wait，
      才继续保留 speaking/output-turn
  - `output_turn_quiesced_locked()` 现在也显式要求
    `playback_turn_active=no`
  - 这一步继续把 core 对 output-turn 生命周期的解释权，从 coarse lane
    occupancy 收口到 runtime-owned retained-turn truth，减少 lane 已空但
    turn 尚未真正结束时的误判窗口
- `Step 5.365`
  - `playback_turn_active()` 已不再继续依赖 coarse
    `xiaozhi_playback_meta_valid` shadow
  - retained playback turn 现在改由：
    - playback lane engaged
    - terminal 仍 open 且 response/playback context 有效
    共同决定
  - `playback_note_meta()` 现在会在写入 queue / wait / terminal context 后立即刷新
    playback phase
  - 这一步继续把：
    - follow-up window
    - busy gate
    - interrupt / abort policy
    对 retained playback turn 的解释权从 coarse meta shadow 收口到
    response/terminal typed truth，并缩短 meta 到 runtime snapshot 的 stale phase
    窗口
- `Step 5.364`
  - XiaoZhi playback runtime 已移除残留的 coarse global
    `xiaozhi_playback_last_segment` shadow bool
  - 当前 meta 的 terminal 语义现在改由：
    - event-local `is_last_segment`
    - stored terminal last-segment context
    共同提供
  - `playback_note_meta()` 不再继续把 terminal 语义中转到额外的 runtime bool
  - queued segment 的 `is_last_segment` 现在直接写入 event fact；dump 中的
    `is_last_segment` 也改为从 typed terminal context 推导
  - 这一步继续把 playback runtime 的 terminal 语义从“单独缓存的布尔影子位”
    收口到 event-local fact 与 runtime-owned terminal context
- `Step 5.363`
  - XiaoZhi playback runtime 已把 downlink starvation/tail 的供给判定从
    coarse global `last_segment` shadow 中拆开，改成显式的 typed supply
    truth：
    - `CURRENT_SEGMENT`
    - `WAITING_NEXT_SEGMENT`
    - `TERMINAL_TAIL`
    - `NONE`
  - `maybe_rebuffer_starved()` 现在只会在 runtime 仍明确期待更多 downlink
    audio 时继续进入 upstream-starved rebuffer
  - terminal tail / 无供给窗口现在会直接清掉 starvation watch，不再继续让
    “最近 meta 的 last/non-last 阴影”主导 tail 行为
  - 这一步继续把 downlink/playback 的 starved/tail 真相从 global meta shadow
    收口到 queue-head、waiting-context、terminal-context 这些 runtime-owned
    typed truth，为后续继续移除残余 coarse `last_segment` shadow 做准备
- `Step 5.362`
  - playback runtime 已独立保存 waiting-segment context：
    - `response_id`
    - `playback_id`
    - `segment_id`
  - `WAITING_SEGMENT` 不再继续从：
    - `meta_valid`
    - `last_segment`
    这组 coarse global meta shadow 推导
  - non-terminal meta 会写入待续段上下文，terminal/invalid meta 会清掉它
  - 这一步继续把 playback phase/wait 真相从“最近 meta 阴影”收口到
    runtime-owned typed waiting context，为后续继续拆 queue-tail / starvation
    shadow 做准备
- `Step 5.361`
  - playback start-gate 的 predictive segment-prefetch 已直接读取
    queue 头段/当前待播 segment
  - `segment_prefetch_target_needed()` 不再继续从：
    - `meta_valid`
    - `last_segment`
    这组 coarse global meta shadow 重建“当前待播段”的 terminal 语义
  - 这一步继续把起播门限的判断依据收口到 runtime-owned segment queue truth，
    为后续继续拆 waiting-segment / prefetch shadow 做准备
- `Step 5.360`
  - playback runtime 已独立保存 terminal last-segment context：
    - `response_id`
    - `playback_id`
    - `segment_id`
  - `playback_completed_ready()` / `playback_completed_wait_kind()` 现在只认
    这份终段上下文，不再继续拿“最近一条 meta 当前挂着的 `segment_id`”充当
    最后一段真相
  - completed ACK 也会优先消费这份 terminal response/playback context
  - 这一步继续把 terminal/completed 真相从 coarse global meta shadow 收口到
    runtime-owned typed terminal context，为后续继续拆 terminal wait / prefetch
    shadow 做准备
- `Step 5.359`
  - playback ACK / terminal 路径已开始把 response-level 与 segment-level
    上下文拆开
  - `started/mark` ACK 现在直接消费当前 segment 上下文
  - `cleared/completed` terminal ACK 现在只要求 response-level 上下文，不再
    继续被全局 `segment_id/meta_valid` 绑死
  - 这一步继续把 playback runtime 的 ACK 真相从单个 shadow bool 收口到更细的
    typed context
- `Step 5.358`
  - `write_failed` 与 `upstream_starved` 两类 rebuffer 原因已开始走不同恢复策略
  - 本地 `WRITE_FAILED` 现在优先 attached `service_recover`
  - `UPSTREAM_STARVED` 仍优先 `stop_rebuffer` / fresh-start
  - 这一步开始把“本地播放设备写抖动”和“上游供给断档”从同一套默认
    `stop/start` 风暴里拆开
- `Step 5.357`
  - downlink worker 现在会先经过 rebuffer resume gate，再进入 backend-state
    分支
  - `rebuffer_resume_ready()` 已统一吸收两类恢复语义：
    - attached `owned_recovering` 直接 finish rebuffer
    - detached backend 保持 pending，后续走 fresh-start
  - loop 也会在 gate 之后重新抓取 backend truth，再决定 wait/start
  - 这一步修正了 attached recovering backend 在真正 resume 前就被短路卡住的
    结构问题
- `Step 5.356`
  - `playback_backend_state()` 不再在 `rebuffering` 期间无条件投影
    `owned_recovering`
  - 当 playback service 已 stop/detach 时，backend truth 现在会如实返回
    `detached`
  - 只有 XiaoZhi stream 仍真实 attached/owned 时，rebuffer phase 才继续投影成
    `owned_recovering`
  - 这一步修正了 rebuffer resume 的结构性卡死风险：
    - rebuffer phase 继续保留恢复语义
    - 但 downlink resume 已能重新看见“backend 已 detached，需要 fresh-start”
- `Step 5.355`
  - XiaoZhi playback runtime 已把“播放回合仍保留”和“downlink worker 仍需运行”
    显式拆成两条真相：
    - `playback_turn_active()`
    - `playback_has_work()`
  - `downlink_active()` 不再继续依赖粗粒度 `playback_lane_engaged`
  - `transport_active()` 也不再因为 playback lane/meta turn 仍存在就继续让
    IO loop 空转
  - `interrupt_tts()`、follow-up window timeout、config busy gate、
    playback abort/terminal policy 已改为显式消费 retained-turn truth
  - 这一步开始把 runtime / transport / downlink 对 playback 的消费界面正式
    分层，为后续继续把 worker/transport/runtime 真相收口到更少 helper 做准备
- `Step 5.354`
  - XiaoZhi playback runtime 现在会把 `rebuffering` 也视为 quiet window
  - `playback_allows_vad_open()` 与 `capture_held_by_playback()` 不再把
    重缓冲静默期继续投影成 generic playback hold
  - 这继续把“response 仍在继续，但当前无真实输出”的阶段从
    `playback_lane_engaged` 粗粒度语义里拆出来，减少 rebuffer 风暴期间的
    capture/VAD 误阻断
- `Step 5.353`
  - server `output_state=speaking` 不再在 `prefetching/idle` 阶段直接投影成
    cloud duplex 的 speaking output
  - `output_speaking_active()` 现在只会在 runtime phase 仍保留 output turn 时，
    才接受 transport-side speaking state
  - 这继续把 cloud duplex 的 speaking truth 从 transport-side 字符串收口到
    playback runtime 自己的 phase/media truth，减少首段预取窗口里
    soft-endpoint / uplink continuation 抢跑
- `Step 5.352`
  - `tts_start` 不再只凭 raw duplex/AEC fallback 就立即关闭本地 round
  - `apply_tts_start_round_policy()` 现在会先看 playback runtime 当前是否真的
    `capture_held`
  - 新增 `playback_started` round policy，把 half-duplex 关轮延后到 playback
    真正起播并开始 hold capture 的时刻
  - 这继续把 half-duplex 关轮从“预判将来会播”收口到“当前已经真实占用媒体/
    capture”的 runtime 真相，减少首段预取窗口提前断开 uplink
- `Step 5.351`
  - XiaoZhi playback runtime 现在会把 `prefetching` 也视为 quiet window
  - `playback_allows_vad_open()` 与 `capture_held_by_playback()` 不再把
    prefetch-only lane occupancy 继续投影成 generic playback hold
  - 这继续把“尚未真正出声、只是 runtime 正在预取”的阶段，从
    `playback_lane_engaged` 粗粒度语义里拆出来，避免 pre-start 窗口继续错误
    阻断 capture/VAD
- `Step 5.350`
  - XiaoZhi playback runtime 现在会把 `tts_stop_pending` 且本地已无真实输出的
    terminal tail wait 视为 quiet window
  - `playback_allows_vad_open()` 与 `capture_held_by_playback()` 现已同步消费这条
    truth，不再只把 `waiting_segment` 视作静默窗口
  - 这继续把 capture/VAD 的 reopen 语义从 lane engaged 收口到 playback
    runtime 自己的媒体输出真相
- `Step 5.349`
  - XiaoZhi playback runtime 已把 `tts_stop_pending` 之后的 detached residual
    queue 明确收口给 runtime 自己处理
  - pending-stop terminal close 现在会：
    - 主动丢弃 detached backend 上残留的 queued audio
    - 阻止 downlink worker 在 stop_pending 期间 fresh-start 非
      `owned_active` backend
  - 这继续把 terminal-stop 的解释权从 worker 循环里的偶然 restart 行为，
    收口到 playback runtime 自己的 terminal truth
- `Step 5.348`
  - XiaoZhi playback runtime 的 `playback_output_active` 已不再只凭
    `playing/draining` phase 就投影成“仍有真实有声输出”
  - 现在还要求 playback backend 仍是：
    - `owned_active`
  - 这继续把：
    - cloud `playback_active`
    - dialog/duplex 上看到的“还在播”
    - playback tail/reopen guard
    从 phase-only 推断，收口到 playback runtime 自己的 backend owner truth
- `Step 5.347`
  - XiaoZhi playback runtime 已把 `backend_attached` 与“媒体 active”语义
    明确拆开
  - stop/abort/pause control path 不再沿用模糊的 `backend_owned`
    命名
  - 这一步继续为后续 downlink/playback 行为判断打基础，减少 occupancy truth
    与 media truth 混用
- `Step 5.346`
  - `dialog_runtime` 的 managed playback-error 吸收与本地 playback no-op
    吸收，现在都改为依赖：
    - `cloud_runtime_available`
  - `owned_paused` 也被纳入受控 playback owner 过渡真相
  - 这继续把 core 对本地 playback 边缘事件的解释权，从 `phase_known`
    退化分支收口到 runtime snapshot owner truth
- `Step 5.345`
  - `dialog_runtime` 现在只会在拿不到 cloud runtime snapshot 时，才让
    `playback_lane_engaged` 在 `phase unknown` 窗口继续保留 output ownership
  - `phase unknown + lane occupied` 不再默认等价于可继续保留
    `speaking/output_turn`
  - 这继续把 output ownership 从 lane occupancy fallback 收口到 runtime
    snapshot owner truth
- `Step 5.344`
  - `dialog_runtime` 现在只会在拿不到 cloud runtime snapshot 时，才让
    本地 playback shadow 回填：
    - `playback_active`
    - `playback_recovering`
  - `phase unknown` 本身不再构成 local shadow 介入常态派生的理由
  - 这继续把 core 侧 playback 派生从本地 listener shadow 收口到 runtime
    snapshot owner truth
- `Step 5.343`
  - `dialog_runtime` 不再在 `phase unknown` 时给
    `tts_interrupt_requested` 保留本地 playback shadow 的旁路清理逻辑
  - cloud snapshot merge 与本地 playback idle 现在统一只依赖：
    - `output_turn_quiesced`
  - 这继续把 interrupt-clear policy 从 local edge signal 收口到 runtime
    自己的统一派生真相
- `Step 5.342`
  - XiaoZhi `backend_state` 已不再因为 phase 仍停在 `playing/draining`，
    就在 playback-service 已不活跃时继续投影 `owned_active`
  - `backend occupancy` 与 `media/output phase` 继续拆开，各自保持独立真相
  - 这继续减少了 downlink/playback 对 phase 反推本地 backend 仍 attached 的
    假设
- `Step 5.341`
  - `dialog_runtime` 已开始直接消费 `playback_backend_state_kind=owned_paused`
    作为受控 playback 过渡真相
  - 本地 `playback_error` 若发生在 XiaoZhi backend 自己的 pause/detach
    窗口，不再轻易被 core 放大成独立 `error_recovering`
  - 这让 `dialog runtime` 对本地 playback-service 边缘错误的解释继续收口到
    playback runtime owner truth
- `Step 5.340`
  - XiaoZhi playback backend truth 新增显式 `owned_paused`
  - `backend_state()` 现在不再只靠：
    - `river_playback_service_state_active(...)`
    - `xiaozhi_playback_active`
    拼接 ownership
  - 新 reducer 会同时吸收：
    - playback runtime phase
    - playback-service 当前 stream owner
  - downlink worker / start path 也开始把 `owned_paused` 视为显式 wait
    state，而不是继续误走 detached/foreign 的 fresh-start 分支
  - 这继续把 playback backend ownership 的解释权收回 playback runtime
- `Step 5.339`
  - `dialog_runtime.snapshot.playback_active` 已继续从“lane occupied”收紧到
    playback runtime 的真实媒体/恢复事实
  - phase 已知时，`prefetching` / `waiting_segment` 不再因 lane 仍被占用
    就投影成 core 侧 `playback_active`
  - `playback_error -> managed recovery` 的吸收条件也同步缩窄，不再把
    generic lane occupancy 当作证据
  - 这继续减少了 core 对“媒体还没开始 / 已经进入静默 gap”状态的 active
    误判
- `Step 5.338`
  - `dialog_runtime` 的 output-turn 派生已继续从粗粒度
    `playback_lane_engaged` 收紧到“有媒体 backing 的 lane truth”
  - phase 已知时，当前只让 `rebuffering` 继续保留 output-turn ownership；
    `prefetching` / `waiting_segment` 不再仅因 lane occupied 就自动投影成
    `speaking`
  - 这继续减少了 dialog/core 对“lane 仍占用但实际上还没出声”状态的误判，
    让 `speaking` / `barge_in_listening` 更接近真实媒体输出
- `Step 5.337`
  - `dialog_runtime` 的本地 playback listener 入口已从双阶段收口成单次
    reducer
  - 同一次 reducer 内会连续完成：
    - dialog playback ownership 识别
    - cloud runtime snapshot merge
    - local playback shadow 更新
    - aggregate playback / interaction 派生
  - 这继续消除了“先写局部 ownership，再等后续 cloud snapshot / publish
    修正”的入口竞态，让 `dialog runtime` 更接近真正单点 reducer 真相源
- `Step 5.336`
  - XiaoZhi playback 的 `prefetch_segment` 起播门限现在不再只依赖
    历史 `meta_gap`
  - 当当前 `audio.out.meta` 已有效、当前段不是最后一段、且预测
    `prefetch_target_ms` 已明显高于基线预算时，也会直接进入：
    - `PREFETCH_SEGMENT`
  - 这让首次起播和段间恢复在尚未积累历史断供前，也能直接消费当前
    `expected_duration_ms` 建立更深的预取预算，从而继续减少
    `underrun/write_failed/rebuffer` 风暴
- `Step 5.335`
  - XiaoZhi downlink/playback 现在把残留的 `write_failed -> recover` 常态路径也
    收口到了统一 `stop/rebuffer`
  - downlink worker 在任意 `write_failed` 下都会先执行：
    - `stop_stream_ex(...)`
    - runtime-owned `rebuffer_pause`
  - 只有 stop 自身失败时，才会打印：
    - `xiaozhi playback stop rebuffer fallback to recover`
    并回退到 `river_playback_service_recover_stream_ex(...)`
  - 这一步继续把 playback backend 真相收回到 runtime 自己手里，让
    `RECOVERING` 进一步退成硬 stop 失败时的兜底语义
- `Step 5.334`
  - `dialog_runtime` 内部 local playback shadow 已进一步从三份缓存收成单一
    private `playback_state`
  - phase-unknown fallback 与 diagnostics dump 现在都按需从这条 private
    state 派生：
    - local active
    - local recovering
  - 这一步继续压缩了 runtime 内部对本地 playback shadow 的重复缓存，减少内部
    shadow 漂移，同时不改变对外 typed truth 面
- `Step 5.333`
  - playback runtime 现在会把本地 playback-service 的
    `RIVER_PLAYBACK_RECOVERING` 上推成 cloud-owned backend truth：
    - `RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_RECOVERING`
    - `owned_recovering`
  - XiaoZhi downlink worker 在 `owned_recovering` 时不再误把 backend 当成
    可 fresh-start 的 detached/foreign 状态
  - `dialog_runtime` 的 `playback_recovering` 常态也开始直接消费：
    - `owned_recovering`
    - `restart_pending`
    只把 local playback shadow 留给 `phase unknown` 退化兜底
  - 这一步继续把 recovering 语义从本地 playback listener shadow 推进到
    playback runtime 自己导出的 typed truth
- `Step 5.332`
  - XiaoZhi playback 在 `upstream_starved` 场景下已不再默认走本地
    `recover`
  - `upstream gap rebuffer` 与 starved `write_failed` 现在都会优先执行：
    - `stop_stream_ex(...)`
    - runtime-owned `rebuffer_pause`
  - 只有当本地 `stop_stream_ex(...)` 自身失败时，runtime 才会回退到
    `river_playback_service_recover_stream_ex(...)`
  - 这一步把常见“上游断粮”从 playback-service 的
    `RECOVERING -> flush/restart` 语义里拆开，让 residual 硬 `write_failed`
    才继续保留 recover 语义
- `Step 5.331`
  - `dialog_runtime` snapshot 现在已显式吸收：
    - `wake_admission_pending`
  - `wake_admission` bridge 会在：
    - 首次 queued
    - accepted
    - terminal failed
    时同步维护这条 runtime truth
  - wakeword gating 也开始直接消费 `wake_admission_pending`：
    - bridge 已有待处理 wake 时，KWS detection / wake admission 不再继续放行
      重复 wake
  - 这一步继续把 wake admission 从“bridge 内部重试队列的局部事实”推进到
    `dialog_runtime` 也显式持有的真相，从而减少重复检测和被动 coalesce
- `Step 5.330`
  - `session_coordinator` 已不再在 wakeword 事件上直接读取
    `river_voice_kws_wake_handoff_block_reason()`
  - `wake_admission` 提交入口现在统一裁决唤醒接力阻塞：
    - 先吸收 KWS 本地 debug / tensor-dump handoff blocker
    - 再吸收 `dialog_runtime` 的 wakeword admission blocker
  - wakeword 的：
    - `handoff blocked`
    - `queued`
    - `coalesced while pending`
    日志也已统一收口到 `wake_admission` 边界
  - 这一步继续把 wakeword 接力从“协调器上的前置私有判定 + 准入桥上的 runtime
    判定”收口到 core-owned 的单一提交/准入边界
- `Step 5.329`
  - `dialog_cloud_port` 上已无消费者的
    `conversation_window_active` 查询侧门已删除
  - app 装配层不再为 cloud port 注册这条 callback
  - 这意味着 `conversation_window` 读取真相继续彻底留在 `dialog_runtime`，
    而 `dialog_cloud_port` 更接近纯 command / ingress 边界
- `Step 5.328`
  - `dialog_runtime` 已开始统一导出 wakeword detection gating：
    - `wakeword_detection_block_reason`
    - `allows_wakeword_detection`
  - wakeword detection 与 wake admission 现在共享同一条内部阻断真相，
    不再分别维护：
    - KWS 自己的 `conversation_window + interaction_state` 判定
    - wake admission 的 runtime block reason
  - `river_voice_kws_detection_allowed()` 已改为直接消费 `dialog_runtime`
    导出的统一结论
  - 这一步继续把 wakeword 入口从分裂的 raw gating，收口到
    `dialog runtime` 真相源
- `Step 5.327`
  - `dialog_runtime` 现在新增统一的 `output_turn_quiesced` 派生，用来表示：
    - 当前 output turn 已真正静止
    - 已可安全清理 `tts_interrupt_requested`
  - 本地 playback idle 清 interrupt 与 cloud snapshot sync 清 interrupt
    现已复用同一 helper，不再各自拼一套 raw 条件
  - cloud sync 路径也已改成先 `refresh_playback`，再按新的 quiesced truth
    决定是否清 interrupt latch
  - 这一步继续把 interrupt-clear policy 从粗粒度边缘条件，收口到
    `dialog_runtime` 的统一派生真相
- `Step 5.326`
  - `dialog_runtime` 现在开始显式区分：
    - 本地有声 playback active
    - output turn 仍被占用，但当前只是 `waiting_segment` silent gap
  - `compute_playback_active_locked()` 不再只因
    `playback_lane_engaged=yes` 且 phase=`waiting_segment` 就继续把
    `playback_active` 维持为 `true`
  - interaction 派生与 `allows_barge_in_interrupt()` 现在改为消费新的
    `output_turn_engaged` 语义：
    - silent gap 不再伪装成 audible playback
    - 但 response 尚未结束时，barge-in interrupt 仍然可用
  - 这一步继续把 `dialog_runtime` 从 coarse playback-active 投影推进到更准确的
    “audible playback vs output-turn ownership” 分离模型
- `Step 5.325`
  - `waiting_segment` 不再只是一条显式 playback phase，也开始直接参与
    duplex / capture 语义
  - XiaoZhi playback runtime 在段间静默空窗时现在会：
    - 允许 `playback_allows_vad_open()`
    - 让 `capture_held_by_playback(...)` 直接返回 `false`
    - 清空对应的 fallback reason
  - 这让“response 尚未结束，但当前并无本地有声播放”的阶段，不再继续被粗暴归类为
    `half_duplex_aec_blocked`
  - 这一步继续把 `waiting_segment` 从“owner 诊断 truth”推进成“真正影响运行时
    行为的 owner truth”
- `Step 5.324`
  - playback owner 新增显式段间等待 phase：
    - `RIVER_CLOUD_PLAYBACK_PHASE_WAITING_SEGMENT`
    - `waiting_segment`
  - XiaoZhi downlink/playback runtime 现在会在“当前 segment 已播完，但下一段还未到达”
    时主动 pause backend，并把 phase 收口到 `waiting_segment`
  - known segment 已存在但音频尚未入 ring 时，phase 继续维持 `prefetching`，
    不再短暂掉回 `idle`
  - 这一步直接把 inter-segment 供给间隙从：
    - `idle`
    - `underrun`
    - `write_failed`
    之间的隐式漂移，收口到显式 typed playback truth
- `Step 5.323`
  - playback owner 的 `backend state` 已从“两个分裂的布尔投影”收口成公共
    typed truth：
    - `river_cloud_playback_backend_state_t`
    - `playback_backend_state_kind`
  - `dialog_runtime` 的公开 snapshot 已删除：
    - `playback_backend_owned`
    - `playback_backend_restart_pending`
    并改为直接吸收 `playback_backend_state_kind`
  - XiaoZhi downlink/playback runtime 的 backend ownership / restart-pending
    判断现在直接复用公共 enum，不再保留私有 backend-state 类型
  - 这一步继续把 downlink/playback backend ownership 真相从跨层布尔投影，
    收口到公共 owner typed truth
- `Step 5.322`
  - playback owner 的 `phase` 已从 XiaoZhi 私有 enum 提升成公共 typed truth：
    - `river_cloud_playback_phase_t`
    - `playback_phase_kind`
  - `dialog_runtime` 的公开 snapshot 已删除字符串：
    - `playback_phase`
    并改为直接吸收 `playback_phase_kind`
  - XiaoZhi downlink/playback runtime 的 `phase` 现在直接用公共 enum 维护，
    不再保留私有 playback-phase 类型
  - 这一步继续把 downlink/playback 的 phase 真相从私有 enum / 跨层文本字段，
    收口到公共 owner typed truth
- `Step 5.321`
  - playback owner 的 `terminal state` 已从内部字符串真相提升成公共
    typed truth：
    - `river_cloud_playback_terminal_state_t`
    - `playback_terminal_state_kind`
  - `dialog_runtime` 的公开 snapshot 已删除字符串：
    - `playback_terminal_state`
    并改为直接吸收 `playback_terminal_state_kind`
  - XiaoZhi downlink/playback runtime 的 terminal reset / ack 映射 /
    local fallback 已全部收口到公共 enum
  - 这一步继续把 downlink/playback 的 terminal 语义从内部字符串 / 跨层文本字段，
    收口到公共 owner typed truth
- `Step 5.320`
  - playback owner 的 `start policy` 已从 XiaoZhi 私有 enum 提升成公共
    typed truth：
    - `river_cloud_playback_start_policy_t`
    - `playback_start_policy_kind`
  - `dialog_runtime` 的公开 snapshot 已删除字符串：
    - `playback_start_policy`
    并改为直接吸收 `playback_start_policy_kind`
  - 这一步继续把 downlink/playback 的启动门限决策从私有 enum / 跨层文本字段，
    收口到公共 owner typed truth
- `Step 5.319`
  - playback owner 的 `rebuffer cause` 已从 XiaoZhi 私有 enum 提升成公共
    typed truth：
    - `river_cloud_playback_rebuffer_cause_t`
    - `playback_rebuffer_cause_kind`
  - `dialog_runtime` 的公开 snapshot 已删除字符串：
    - `playback_rebuffer_cause`
    并改为直接吸收 `playback_rebuffer_cause_kind`
  - 这一步继续把 downlink/playback 的恢复语义从私有 enum / 跨层文本原因，
    收口到公共 owner typed truth
- `Step 5.318`
  - `dialog_runtime` 的公开 snapshot 已删除：
    - `playback_state`
    - `playback_local_active`
    - `playback_local_recovering`
  - 本地 playback shadow 现在只保留在 runtime 内部，服务于：
    - stream ownership ingress
    - phase-missing fallback
    - diagnostics dump
  - 这一步继续把对外 `dialog runtime` 真相面收口到 owner typed truth，而不是把
    本地 playback listener shadow 暴露成 public truth
- `Step 5.317`
  - `dialog_runtime` 已把剩余 local playback shadow 的：
    - active fallback
    - recovering fallback
    收口到显式 `phase unknown` helper
  - `compute_playback_active_locked()` /
    `compute_playback_recovering_locked()` 不再直接混合 `playback_local_*` 与
    cloud/runtime truth
  - interrupt-clear 的 local shadow 阻断也已复用同一 fallback helper
  - 这一步继续把 local playback shadow 压回 phase-missing fallback，而不是常态
    派生输入
- `Step 5.316`
  - cloud playback runtime 已把 terminal wait 从 generic bool/text 提升成
    typed truth：
    - `playback_terminal_wait_kind`
  - XiaoZhi playback owner 现在直接区分：
    - `await_last_segment_meta`
    - `await_segment_queue_drain`
    - `await_last_segment_tail`
  - `dialog_runtime` 已开始直接消费这条 truth，只在：
    - queue drain
    - last segment tail
    下压低 speaking 投影；`await_last_segment_meta` 不再被误判成已经进入尾段等待
  - 这一步继续把 core 的 terminal-wait 解释权收回 playback owner
- `Step 5.315`
  - 当 `playback_phase_known` 已成立时，`dialog_runtime` 不再让
    `playback_local_active` 阻断 interrupt latch 清理
  - local playback shadow 现在进一步退回到 `phase unknown` 兜底路径
  - 这一步继续压缩了 core 行为层对本地 playback listener shadow 的常态依赖
- `Step 5.314`
  - `dialog_runtime` 已删除对 app 侧 dialog playback stream 白名单注册的依赖
  - dialog playback ingress 现在直接按 `RIVER_PLAYBACK_PRIO_TTS` 识别
  - `river_app_boot()` 不再装配 `xiaozhi_tts` / `iflytek_tts` 白名单
  - 这一步继续把 dialog playback ownership truth 从 app wiring 收回 runtime
- `Step 5.313`
  - `dialog_runtime` 的 playback listener ingress 现在会先抓取当前 cloud
    runtime snapshot，再吸收本地 playback state callback
  - 这让：
    - `managed_recovery`
    - `tts_interrupt_requested` 清理
    - interaction 派生
    都基于同一时刻的 playback owner truth
  - 本地 `RIVER_PLAYBACK_*` callback 继续从 aggregate truth source 退回为：
    - local backend shadow
    - playback ingress signal
- `Step 5.312`
  - cloud playback runtime snapshot 已继续补齐 backend ownership truth：
    - `playback_backend_owned`
    - `playback_backend_restart_pending`
  - `dialog_runtime` 已开始直接消费这两条 typed truth
  - local `IDLE` 清 interrupt latch 与 managed recovery 分类也已开始受
    backend restart-pending 保护
  - 这让 core 继续从“本地 listener 瞬时状态”退回到“playback owner 输出真相”
- `Step 5.311`
  - `dialog_runtime` 已不再依赖 playback phase 字符串做行为判断
  - `"playing" / "draining" / "rebuffering"` 等文本现在只保留给诊断输出
  - playback active / recovering / managed-recovery 的派生继续只消费：
    - `playback_cloud_active`
    - `playback_lane_engaged`
    - `playback_rebuffer_pending`
    - `playback_phase_known`
    - `playback_terminal_closed`
  - 这让 core 行为层继续从字符串解释退回到 typed playback truth
- `Step 5.310`
  - cloud playback runtime snapshot 新增 typed bool：
    - `playback_phase_known`
    - `playback_terminal_closed`
  - `dialog_runtime` 已开始直接吸收并消费这两条真相
  - 这让 core 侧对：
    - `playback_phase[0] != '\0'`
    - `playback_terminal_state[0] != '\0'`
    的字符串二次解释开始退出主路径
  - `allows_barge_in_interrupt()` 与 runtime status dump 也同步开始转向
    typed playback truth
- `Step 5.309`
  - `dialog_runtime` 已删除公开的 playback-state 侧门：
    - `river_dialog_runtime_note_playback_state(...)`
  - 本地 playback 真相继续只保留：
    - `river_dialog_runtime_on_playback_state(...)`
    这条 listener ingress
  - local `RIVER_PLAYBACK_IDLE` 也不再无条件清掉 interrupt latch；
    若 cloud playback 真相仍显示 lane engaged / rebuffer / stop-pending /
    terminal-waiting，`tts_interrupt_requested` 会继续保留
  - 这一步继续把 interrupt clear policy 绑定到 aggregate playback truth，
    而不是本地 service 的瞬时 idle 边缘
- `Step 5.308`
  - `dialog_runtime` 已删除未再使用的外部错误入口：
    - `river_dialog_runtime_note_error(...)`
    - `river_dialog_runtime_clear_error(...)`
  - 这让 coarse `error_recovering` 不再能从真相源外部被任意抬起或清空
  - `dialog_runtime` 的错误语义现继续收口到：
    - playback state reducer
    - cloud/runtime snapshot sync
- `Step 5.307`
  - `dialog_runtime` 在 managed playback recovery 中不再提前清掉
    `tts_interrupt_requested`
  - 这让：
    - managed rebuffer/recovering
    - local interrupt in-flight latch
    在恢复窗口内保持一致，避免重复 barge-in interrupt
- `Step 5.306`
  - `dialog_runtime` 现在会把“managed playback recovery 中出现的本地
    `RIVER_PLAYBACK_ERROR`”吸收到 `playback_recovering`
  - 若 cloud playback truth 仍表明当前处于：
    - rebuffering
    - playback_cloud_active
    - playback_lane_engaged
    之一，则不再额外抬 `error_recovering`
  - 这一步直接针对日志中频繁出现的
    `speaking -> error_recovering -> speaking` 风暴做状态收口
- `Step 5.305`
  - `river_cloud_xiaozhi_dump_session_status()` 已改为先读取
    `river_cloud_runtime_snapshot_t`
  - session status dump 中的：
    - playback active/phase/rebuffer/stop-pending
    - local close/window remaining
    - turn semantics
    现统一消费 runtime snapshot 投影，而不是日志路径自己重拼
  - 这让 XiaoZhi session 诊断面也继续向 runtime 真相源收口
- `Step 5.304`
  - wake admission 的 pending/retry worker 已从 `session_coordinator` 抽到新的
    core-owned bridge：
    - `river_dialog_wake_admission`
  - 新 bridge 直接拥有：
    - runtime block-reason check
    - queued/coalesced/deferred/retry
    - worker unavailable 时的 inline fallback
  - `session_coordinator` 已删掉 wake admission 的 task/sema/mutex/pending 状态，
    继续退成更薄的 handoff + ASR text fanout shell
- `Step 5.303`
  - `dialog_cloud_port` 现在会在
    `begin_conversation_window()` 成功后，立即把 wake admission success
    同步写入 `dialog runtime`
  - `wakeword -> wakeword_detected` 的 reason 归一已收口到 port 内
  - `session_coordinator` 不再手工拼：
    - wake admission success
    - wake_confirmed runtime note
  - 这把 `cloud ingress success -> dialog truth update` 收成一次原子操作，
    继续削弱 coordinator 的 success-side bridge 角色
- `Step 5.302`
  - `dialog_cloud_port` 已补齐 `begin_conversation_window()`
  - wakeword worker / fallback 路径现已不再从 `session_coordinator` 直连
    `river_cloud_adapter_begin_conversation_window()`
  - wake admission 这条 core -> cloud ingress 现在开始与：
    - ASR stream open/push
    - batch submit
    - interrupt_tts
    共享同一条 stable port 边界
- `Step 5.301`
  - `dialog runtime` 已开始直接拥有本地 interrupt in-flight 真相：
    - `tts_interrupt_requested`
  - `dialog_cloud_port` 会在 `interrupt_tts_with_reason()` 成功后立即把这条事实
    写入 `dialog runtime`
  - `session_coordinator` 已删除本地：
    - `barge_in_interrupt_requested`
    - `state_lock`
  - ASR 文本触发的 barge-in interrupt 改由 `dialog runtime` policy +
    `dialog_cloud_port` 协同完成
- `Step 5.300`
  - `dialog runtime` 已开始直接吸收 XiaoZhi round/window typed truth：
    - `listen_stop_pending`
    - `local_close_pending`
    - `conversation_window_remaining_ms`
    - `local_close_remaining_ms`
  - wake admission block reason 已优先暴露 typed round cause：
    - `conversation_window_active`
    - `cloud_local_close_pending`
    - `cloud_listen_stop_pending`
  - cloud round 仍处于 listening / stream / stop-pending / local-close-pending /
    committed-input 之一时，`dialog runtime` 会继续维持 `asr_session_active`
- `Step 5.299`
  - `open_session_and_listen() / start_followup_round() /
    maybe_start_followup_round() / begin_conversation_window()` 已并入
    `river_cloud_xiaozhi_round_runtime.c`
  - 这让 round runtime 从“关停/超时 owner”继续扩展成“round 启停 owner”
- `Step 5.296`
  - `river_dialog_cloud_conversation_window_active()` 已优先读取
    `dialog runtime` snapshot，voice/KWS 侧 follow-up window 开始切向
    `dialog runtime` 真相
- `Step 5.297`
  - XiaoZhi 的 `listening/window/listen_stop/local_close` 辅助状态已从
    `river_cloud_xiaozhi_session.c` 拆到新的
    `river_cloud_xiaozhi_round_runtime.c`
- `Step 5.298`
  - `round close/reset/timeout/post-commit wait` 已继续并入
    `river_cloud_xiaozhi_round_runtime.c`
  - `xiaozhi_session.c` 不再内联维护：
    - `emit_session_started/closed`
    - `local_close timeout`
    - `transport reset`
    - `window timeout`
    - `post_commit_wait/local_close_defer` 拼装
- 当前下一焦点：
  - 继续删除 cloud-port / coordinator / voice 层剩余的旧查询侧门，优先清理：
    - 仍只是为 fallback 保留、但已无真实消费者的 runtime 查询接口
  - 继续让 follow-up reopen / wake admission retry / wakeword handoff 统一消费：
    - `wakeword_detection_block_reason`
    - `output_turn_engaged`
    而不是在各模块继续各自拼 coarse 条件
  - 继续把 follow-up / wake admission 对 output-turn 的阻断语义，
    与新的：
    - `output_turn_engaged`
    - `output_turn_quiesced`
    对齐，避免后续仍回退到 raw `interaction_state` / raw lane 判定
  - 继续检查 `dialog_runtime` 清 interrupt / speaking/follow-up 投影时，
    是否仍残留对：
      - raw `output_lane == speaking`
    - `playback_lane_engaged`
    的粗粒度依赖
  - 继续把 `waiting_segment` 的静默语义推广到更多上层派生：
    - interrupt clear
    - follow-up reopen
    - wake admission block / release 细节
  - 继续让 `dialog_runtime` / interaction / output lane 显式消费
    `waiting_segment`：
    - 区分“response 仍在继续”
    - 与“本地仍有有声 playback 正在输出”
    避免 silent gap 继续被上层粗粒度当成持续 speaking / capture-blocking
  - 继续压缩 `dialog_runtime` 内部 local playback shadow：
    - 只保留 stream ownership ingress 所需最小状态
    - 评估 `playback_local_active/recovering/state` 是否还能收成一个更小的
      private shadow 表达
  - 继续把 cloud/runtime snapshot 里的 playback 文本诊断字段向 typed truth 收口：
    - `playback_terminal_reason`
    - `playback_rebuffer_cause` 在 cloud public snapshot 中的剩余诊断角色
    - `playback_start_policy` 在 cloud public snapshot 中的剩余诊断角色
  - 继续把 downlink/playback 的恢复流程从“write failed -> stop/start reuse”整理成
    更稳定的 owner 状态机：
    - rebuffer enter
    - backend recover attempt
    - terminal wait / completion handoff
  - 继续排查 `dialog_runtime` 对本地 playback service 边缘事件的剩余依赖，
    把它们降级成 typed local signal，而不是 coarse interaction/error 推导入口
  - 继续把 playback terminal / interrupt clear / recovery policy 往
    playback runtime owner 输出的 typed truth 收口，减少 core 侧再做字符串/边缘重解释
  - 继续补齐更多 typed playback/runtime projection，逐步压缩：
    - terminal wait
    - rebuffer cause/history
    - local/backend ownership
    的 core-side 二次拼装
  - 回到 downlink/playback runtime，继续收紧：
    - `tts_stop_pending`
    - playback stop/reset
    - rebuffer/recovering
    的 owner 边界和状态投影
  - 继续减少 `dialog_runtime` 对本地 playback service 边缘事件的副作用扩散，
    让 interrupt/error/recovering 更稳地围绕 playback truth owner 收敛
  - 继续减少 `dialog_runtime` 对本地 playback service error 信号的粗粒度放大，
    优先以 cloud playback runtime 的 managed recovery truth 驱动交互态
  - 继续让 XiaoZhi status/diag 读取优先消费 runtime snapshot / typed helper，
    减少 session/adapter 侧重新解释 playback truth
  - 随后继续推进 downlink/playback runtime 重建，让 cloud playback /
    local playback / recovering/rebuffer 的 owner 边界彻底稳定

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
- `/root/agent-server` 当前主线已经不再停留在“协议预留 / 实验骨架”阶段，而是已具备：
  - 双轨 session core
  - preview-first + `server_endpoint`
  - early audio / speech planner overlap
  - playback-truth-driven heard-text / interruption / resume
- 服务侧当前优先级也已转为：
  - `preview_first_partial / accept / interrupt_cutoff` 的稳定回归
  - 压 `accepted_turn -> first_audio`
  - dedicated semantic judge lane
- 因此当前设备侧剩余重构排序不应再以“等待服务侧先补 S1~S4 主干能力”为前提，
  而应围绕：
  - playback truth 闭环
  - downlink/playback 恢复重建
  - `dialog runtime` 真相源收口
  - runtime-ready duplex gate
  重新排列

## 6. 风险与未知项

- 若 `river_core` 真相源建立过慢，仍会长期存在双状态机问题
- 若 downlink 重构过早展开，容易和当前服务端供给抖动混在一起
- 若只拆文件不改所有权，复杂度不会真正下降
- 某些播放 write error 仍可能是 SDK/驱动层行为，需要板端复现验证

## 7. 回灌后的端侧剩余优先级

### P0: downlink / playback 真相链与恢复模型

当前最优先的不是再扩更多 duplex 行为分支，而是先把端侧对“播到了哪里、
为什么停、接下来该等还是该重启”的解释权彻底收口。

原因：

- 现网体感中的“卡顿 / 断续 / 播放重来”首先来自板端的
  `underrun -> write_failed -> stop/start/rebuffer` 风暴，而不是服务侧主对话
  本身完全起不来。
- 服务侧 2026-04-21 主线已明确把：
  - `audio.out.started`
  - `audio.out.mark`
  - `audio.out.cleared`
  - `audio.out.completed`
  接进 heard-text / interruption / resume 真相链；如果端侧这条链不稳定，
  后续全双工行为都会建立在错误播放事实之上。

因此当前应优先继续推进：

- 让 playback runtime 成为 segment context、waiting/terminal/supply truth、
  rebuffer cause family 的唯一 owner
- 继续把 `write_failed` 压回“前置 starvation 没拦住时的剩余硬故障”
- 把 `response.start -> audio.out.meta -> started/mark/cleared/completed`
  串成同一条可验证的播放 lineage

### P1: `dialog runtime` 真相源彻底收口

服务侧现在已经有较清晰的 lane-state / accept / playback truth 模型；端侧若仍让
本地 listener shadow、coordinator 薄桥接、adapter edge 事件在常态路径里改写
交互真相，就会继续把服务侧 lane truth 稀释回本地启发式。

当前第二优先级应是：

- 继续删除 `dialog runtime` 常态路径里对 local playback shadow 的依赖
- 让 session/coordinator/voice 统一消费：
  - typed cloud runtime snapshot
  - typed playback runtime snapshot
- 把剩余“先写本地局部事实，再等后续 cloud sync 修正”的入口继续收成更少的
  typed reducer

目标不是“文档上只有一个 runtime”，而是让：

- output turn
- follow-up window
- interrupt clear
- wake/admission 协同

都不再被本地边缘事件抢写偏。

### P2: duplex / capture / AEC gate 切到 runtime-ready truth

当前板端日志里反复出现：

- `echo:0B`
- `ref_peak=0`

这说明“能不能真正边播边听”仍首先取决于声学前提和 runtime-ready gate，而不是
是否已经把 capability 字段协商出来。

因此第三优先级应是：

- 明确拆开：
  - audible playback
  - output-turn ownership
  - capture-held
  - reference-ready
  - quiet-window
- 继续让：
  - `prefetching`
  - `waiting_segment`
  - `rebuffering`
  - terminal silent tail
  这些阶段进入 runtime-owned quiet-window 语义，而不是重新掉回
  `lane occupied == playback hold`
- 最终把 duplex fallback / reopen / no-ref churn 都建立在 runtime-ready truth 上，
  而不是 profile/capability 级静态假设上

### P3: 统一 turn timeline 与板端回归基线

服务侧当前 P0/P1 正在压：

- `preview_first_partial / accept / interrupt_cutoff`
- `accepted_turn -> first_audio`

端侧若没有与之对齐的 timeline/lineage 观测面，就只能继续看分散日志，难以判断
卡顿是发生在 uplink、accept、response.start、首段 meta 还是本地起播。

因此第四优先级应是：

- 继续把：
  - `accept_reason`
  - `turn_id`
  - `response.start`
  - `audio.out.meta`
  - playback ACK
  串成同一条板端可回归 timeline
- 让 `dialog runtime` / playback runtime / transport dump 对齐同一套 turn /
  response / playback / segment lineage
- 为后续板端回归补齐与服务侧一致的里程碑：
  - preview first partial
  - accept
  - response start
  - first audio meta
  - started / mark / cleared / completed

### P4: 基于稳定真相链再做 duck-first / keep-listening

duck-first、delayed interrupt、speaking-time keep-listening 仍然重要，但它们现在
不应该排在 playback truth / runtime truth / runtime-ready gate 之前。

原因：

- 若 playback truth 还不稳，duck / interrupt 只会放大“其实没播 / 已停 /
  已静默 gap”这些误判
- 若 `dialog runtime` 还不是唯一真相源，near-end 行为优化会继续被本地局部
  state 抢写打回硬打断
- 若 AEC / reference 还未稳定，逻辑全双工只会更快放大回声误触发

因此行为优化当前应建立在前面三层收口之后，再进入：

- duck-first 本地仲裁
- speaking-time keep-listening
- 更自然的 soft interrupt / hard interrupt 分层

## 8. 执行切片

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
- `OPEN_AND_LISTEN` 的 transport control 执行壳也已继续从 adapter 收口：
  - 新增：
    - `river_cloud_xiaozhi_execute_open_and_listen_transport(...)`
  - session runtime 现统一负责：
    - `open_session`
    - transport session-id sync
    - `listen_start` 前的 listening truth gate
  - adapter 的 `RIVER_CLOUD_XIAOZHI_CTRL_OPEN_AND_LISTEN` 分支现只保留：
    - control dispatch 壳
    - runtime helper 调用
  - 这继续把 control path 上的 transport/session 真相从 adapter 执行体移入
    runtime
- `LISTEN_STOP / ABORT / CLOSE_SESSION` 这组 session-side transport control
  执行壳也已继续从 adapter 收口：
  - 新增：
    - `river_cloud_xiaozhi_execute_session_control_transport(...)`
  - session runtime 现统一负责：
    - `listen_stop` 的 uplink-ready gate
    - `abort` 的 session-open gate
    - local close 对应的 transport teardown
  - adapter 的对应 control 分支现只保留：
    - control dispatch 壳
    - runtime helper 调用
  - 这使 control path 上剩余的 session transport 真相继续从 adapter 执行体
    下沉到 runtime
- `PLAYBACK_STARTED / MARK / CLEARED / COMPLETED` 这组 playback ACK transport
  执行壳也已继续从 adapter 收口：
  - 新增：
    - `river_cloud_xiaozhi_execute_playback_control_transport(...)`
  - playback runtime 现统一负责：
    - 四类 playback ACK transport send
    - sent/fail 日志
    - negotiated ack mode / last error 投影
  - adapter 的对应 control 分支现只保留：
    - control dispatch 壳
    - playback runtime helper 调用
  - 这让 downlink/playback 路径上的 ACK transport 发送权进一步回到
    playback runtime，自身的 ACK 队列与 ACK 发送不再分散在两个模块
- XiaoZhi control path 的最后一层 op-type dispatch 壳也已继续从 adapter 收口：
  - 新增：
    - `river_cloud_xiaozhi_execute_control_transport(...)`
  - session runtime 现统一负责：
    - control-op switch
    - session-side control helper 路由
    - playback ACK control helper 路由
  - adapter 的 `river_cloud_xiaozhi_control_execute(...)` 现仅保留：
    - request 壳
    - runtime helper 调用
  - 这使 adapter 在 control path 上也达到了和 event path 类似的形态：
    - transport/control callback 壳
    - runtime 真相与 reducer 所有权
- XiaoZhi control queue 的提交与 drain 所有权也已继续从 adapter 收口：
  - session runtime 新增统一 reducer：
    - `river_cloud_xiaozhi_process_control_queue(...)`
    - `river_cloud_xiaozhi_control_request_async(...)`
    - sync `river_cloud_xiaozhi_control_request(...)`
  - session runtime 现统一负责：
    - request 初始化
    - sync/async queue submit
    - queue read/write index 推进
    - completion/result 回填
    - session-side request wrapper 入口
  - adapter 现不再直接维护：
    - control queue 写入
    - control queue drain
    - completion semaphore 交付
  - 这让 control path 在 dispatch 之外的队列所有权也继续回到 runtime，
    adapter 进一步逼近纯 I/O 调度壳
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
- STT pending-text 的观测入口也已继续下沉到 session runtime：
  - 新增 `river_cloud_xiaozhi_note_stt_observation(...)`
  - adapter 的 `RIVER_XIAOZHI_EVENT_STT` 分支不再直接写：
    - `xiaozhi_pending_text`
    - `xiaozhi_pending_text_valid`
    - `xiaozhi_pending_text_finalized`
  - partial ASR 发射与存量文本去重现统一收口到 runtime helper
- `LLM/TTS` transport observation 也已继续从 adapter 分支下沉到 session
  runtime：
  - 新增：
    - `river_cloud_xiaozhi_note_llm_observation(...)`
    - `river_cloud_xiaozhi_note_tts_observation(...)`
  - adapter 的 `RIVER_XIAOZHI_EVENT_LLM/TTS` 分支不再直接内联：
    - follow-up window touch
    - `tts_start` pending-text finalize
    - `tts sentence_start` 的 `last_text` 写入
    - `tts stop` round policy apply
  - 这进一步收紧了 `river_cloud_adapter.c` 的角色：
    - transport event 分发
    - runtime helper 调用
    - 避免继续持有 session truth mutation
- `audio_out_meta / session_closed / error` 这组 transport 观察也已继续从
  adapter 分支下沉到 runtime：
  - playback runtime 新增：
    - `river_cloud_xiaozhi_note_audio_out_meta_observation(...)`
  - session runtime 新增：
    - `river_cloud_xiaozhi_note_session_closed_observation(...)`
    - `river_cloud_xiaozhi_note_error_observation(...)`
  - adapter 的对应 transport 分支不再直接内联：
    - playback fact log projection
    - transport-closed terminal policy
    - `RIVER_CLOUD_ASR_EVENT_ERROR` 发射
  - 这使 adapter 更接近纯 transport 分发层，而 playback/session runtime
    分别继续收口各自的真相与终态语义
- `input_speech_start / input_preview / input_endpoint` 这组输入侧 preview
  glue 也已继续从 adapter 分支下沉到 session runtime：
  - 新增：
    - `river_cloud_xiaozhi_note_input_observation(...)`
  - adapter 的对应 transport 分支不再直接内联：
    - preview follow-up window touch
    - endpoint soft-close arm/cancel
    - preview interrupt hint
  - 这使输入侧 transport 事件也开始通过 grouped reducer 进入 session
    runtime，而不是在 adapter 中分散维护策略 glue
- `STT` 分支剩余的 follow-up window glue 也已继续从 adapter 收口：
  - `river_cloud_xiaozhi_note_stt_observation(...)` 现同时负责：
    - `stt` follow-up window touch
    - pending-text dedup / partial ASR emission
  - adapter 的 `RIVER_XIAOZHI_EVENT_STT` 分支不再在 runtime helper 之前再
    持有一次单独的 `window_touch(...)`
- `SERVER_HELLO` transport 分支也已继续从 adapter 收口：
  - 新增：
    - `river_cloud_xiaozhi_note_server_hello_observation(...)`
  - session runtime 现统一负责：
    - server sample-rate/frame-duration 同步
    - transport session-id 同步
  - adapter 的 `RIVER_XIAOZHI_EVENT_SERVER_HELLO` 分支不再直接写这些会话
    启动态事实
- XiaoZhi transport event callback 的最后一层 dispatch 壳也已继续从
  adapter 收口：
  - 新增：
    - `river_cloud_xiaozhi_handle_transport_event(...)`
  - session runtime 现统一负责：
    - 每个 transport event 到达时的
      `river_cloud_xiaozhi_refresh_turn_semantics("event")`
    - 整个 XiaoZhi transport event switch / reducer dispatch
  - adapter 的 `river_cloud_xiaozhi_event_handler(...)` 现仅保留：
    - transport callback 壳
    - runtime helper 调用
  - 这让 adapter 更接近纯 provider/transport 接线层，而 event-type 所有权
    继续稳定落在 runtime
- XiaoZhi I/O loop 上那层 session-housekeeping glue 也已继续从 adapter 收口：
  - 新增：
    - `river_cloud_xiaozhi_run_io_tick_housekeeping(...)`
    - `river_cloud_xiaozhi_run_post_poll_housekeeping(...)`
    - `river_cloud_xiaozhi_run_post_uplink_housekeeping(...)`
  - session runtime 现统一负责：
    - `io_tick` 的 turn-semantics refresh
    - window timeout / endpoint soft-close / local-close timeout 组合维护
    - poll 后 accepted-turn pending-text finalize
    - uplink 后 playback pending-stop 收尾
  - adapter 的 `river_cloud_xiaozhi_io_task(...)` 不再直接内联这些会话/播放
    维护判断，而是只保留：
    - control queue 调度
    - websocket poll
    - uplink service
    - fairness delay
  - 这继续把 I/O loop 中的“运行时真相维护”从 adapter 调度壳里抽离出来，
    让 runtime 统一拥有这些时序 reducer
- `accept_reason` 驱动的 pending-text finalize 判定也已继续从 adapter 收口：
  - 新增 `river_cloud_xiaozhi_finalize_pending_text_if_turn_accepted(...)`
  - adapter 的 XiaoZhi I/O poll 路径不再直接检查：
    - `xiaozhi_pending_text_valid`
    - `xiaozhi_pending_text_finalized`
  - runtime 侧现统一维护：
    - finalize eligibility
    - final commit
    - accept-only finalize 路径
- dialog runtime 依赖的云侧 XiaoZhi snapshot 投影也已继续从 adapter 收口：
  - 新增 `river_cloud_xiaozhi_fill_runtime_snapshot(...)`
  - `river_cloud_adapter_get_runtime_snapshot()` 不再逐项拼装：
    - playback / rebuffer / terminal wait
    - accept / barge-in 语义
    - session / turn / input-output state 文本
  - adapter 现在只负责 generic snapshot 初始化，再委托 runtime 填充
- adapter `dump_status()` 里的 session 真相诊断也已继续收口：
  - 新增 `river_cloud_xiaozhi_dump_session_status(...)`
  - runtime 现统一负责输出：
    - session/window/local-close/pending-text
    - preview
    - endpoint soft-close
    - turn semantics
  - adapter 诊断面开始只保留 generic 壳与 playback/downlink 统计
- adapter `dump_status()` 里的下行播放真相诊断也已继续收口：
  - 新增 `river_cloud_xiaozhi_dump_playback_status(...)`
  - playback runtime 现统一负责输出：
    - playback meta / terminal / tail-wait
    - duplex-ready / duplex-policy
    - no-ref reopen guard
    - downlink queue / rebuffer observation
  - adapter 诊断面进一步退化为 generic 壳与 control/uplink/ASR 队列统计
- adapter `dump_status()` 里的 control/uplink/ASR round 诊断也已继续收口：
  - 新增 `river_cloud_xiaozhi_dump_io_status(...)`
  - session runtime 现统一负责输出：
    - control queue
    - uplink queue
    - ASR round summary
  - `river_cloud_xiaozhi_uplink_ready_frames()` 也已从 adapter 移入 runtime，
    让 transport 路径与诊断路径共享同一条 retry-aware uplink queue 真相
  - adapter 诊断面进一步退化为 generic 壳与 runtime helper 调用
- XiaoZhi I/O loop 的 active-poll 工作量判定也已继续从 adapter 收口：
  - 新增 runtime-owned reducer：
    - `river_cloud_xiaozhi_io_has_work(...)`
  - session runtime 统一归并：
    - control pending
    - transport active
    - uplink active
  - adapter I/O task 不再本地拼接这三类状态来判断是否继续 active poll
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
- XiaoZhi ASR partial/final 发射记账也已继续收口到 session runtime：
  - 新增 exported helper：
    - `river_cloud_xiaozhi_note_asr_result_emitted()`
  - 该 helper 现在统一拥有：
    - `partial_seen`
    - `partial_count`
    - `final_seen`
    - `final_count`
  - adapter `river_cloud_emit_asr_result()` 现在只保留：
    - generic ASR result payload 组装
    - runtime helper 调用
    - listener notify
- XiaoZhi uplink ingress / active-stream finish 语义也已继续收口到
  session runtime：
  - 新增 exported helper：
    - `river_cloud_xiaozhi_trim_uplink_stale_frames()`
    - `river_cloud_xiaozhi_push_pcm()`
    - `river_cloud_xiaozhi_complete_active_stream_finish()`
  - 这些 helper 现在统一拥有：
    - stale-tail trim
    - uplink ring overflow drop 记账
    - PCM accumulator packetization
    - padded flush on active-stream finish
    - finish 后的 `listen_stop` finalize bridge
  - adapter 现在只保留：
    - actual uplink transport send
    - I/O 调度与 runtime helper 调用
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
- XiaoZhi uplink I/O service 本体也已继续从 adapter 收口到 session runtime：
  - session runtime 新增统一 reducer：
    - `river_cloud_xiaozhi_run_uplink_io_once(...)`
    - `river_cloud_xiaozhi_send_uplink_transport(...)` 的 transport 壳调用
  - session runtime 现统一负责：
    - retry-preserved frame drain
    - send-ready gate 后的 paced send
    - busy backoff / backpressure log
    - stale trim
    - uplink timestamp 推进
    - round packet-sent accounting
  - adapter 的 `river_cloud_xiaozhi_io_task(...)` 现只保留：
    - websocket poll / control queue / uplink helper 的调度顺序
    - fairness delay
    - uplink transport send 壳
  - 这使 uplink data plane 上的 pacing 真相也回到 runtime，adapter 进一步逼近
    纯 transport/I/O 调度壳
- XiaoZhi playback backend refresh / bridge-close tail 也已继续从 adapter 收口：
  - playback runtime 新增 adapter-facing helper：
    - `river_cloud_xiaozhi_apply_playback_backend_refresh_policy(...)`
    - `river_cloud_xiaozhi_apply_bridge_close_playback_tail(...)`
  - playback runtime 现统一负责：
    - backend init / config refresh 时的 downlink worker bootstrap
    - backend refresh 时的 playback-state reset
    - bridge-close 时的 decoder teardown tail
  - adapter 的 XiaoZhi init / config-refresh / audio-close 路径现只保留：
    - session/transport 层动作
    - runtime helper 调用
  - 这继续缩小 adapter 在 playback/downlink 生命周期上的直接所有权，为后续
    把 terminal-close reducer 本身继续回收到 playback runtime 做准备
- XiaoZhi terminal-close 的 playback reducer 也已继续从 session/runtime 外层收口：
  - playback runtime 新增 grouped helper：
    - `river_cloud_xiaozhi_apply_terminal_playback_policy(...)`
  - playback runtime 现统一负责：
    - `transport_closed / network_lost / bridge_close` 三类 terminal cause 的
      playback abort gate
    - terminal decoder teardown tail
  - session runtime 的 terminal policy 分支现只保留：
    - session/window/round 的 terminal policy
    - playback runtime helper 调用
  - adapter 的 `river_cloud_asr_audio_close()` 现不再在 session runtime policy
    之后追加一层 playback bridge-close tail
  - 这让 terminal-close 路径上的 playback 收尾真相进一步收敛到同一个
    playback runtime reducer，而不再散落在 session runtime 与 adapter 外围
- XiaoZhi session reset / session-start 的 playback cleanup policy 也已继续从
  session runtime 收口：
  - playback runtime 新增 grouped helper：
    - `river_cloud_xiaozhi_apply_transport_reset_playback_policy()`
    - `river_cloud_xiaozhi_apply_session_start_playback_policy()`
  - playback runtime 现统一负责：
    - transport reset 时的 downlink reset
    - transport reset 时的 playback meta clear
    - session start 时的 playback meta clear
  - session runtime 的 transport reset / open-listen 路径现只保留：
    - session/window/preview/turn reset
    - playback runtime helper 调用
  - 这继续把 session 入口上的 playback/downlink cleanup 真相收回到
    playback runtime，为后续把 capture-path playback glue 一并移出 adapter
    做准备
- XiaoZhi capture path 上的 playback entry glue 也已继续从 adapter 收口：
  - playback runtime 新增 grouped helper：
    - `river_cloud_xiaozhi_apply_capture_entry_playback_policy()`
    - `river_cloud_xiaozhi_apply_capture_exit_playback_policy()`
  - playback runtime 现统一负责：
    - capture 入口的 pending-stop observation
    - duplex-held capture gate
    - fallback log
    - held-capture pre-roll reset
    - capture 退出后的 pending-stop observation
  - adapter 的 `river_cloud_xiaozhi_stream_push_frame(...)` 现只保留：
    - time-ready observation
    - pre-roll store / replay
    - followup round open
    - uplink PCM push
    - runtime helper 调用
  - 这继续缩小 adapter 在 realtime capture 路径上对 playback 真相的直接
    所有权，使 capture/playback 交界开始稳定落在 playback runtime
- XiaoZhi active-stream capture tail 也已继续从 adapter 收口到 session runtime：
  - session runtime 新增 grouped helper：
    - `river_cloud_xiaozhi_apply_active_stream_capture_policy(...)`
  - session runtime 现统一负责：
    - `speech_resumed` 的 endpoint-soft-close cancel
    - `silence_frames` 递进
    - post-roll/min-active gate
    - duplex soft-endpoint 与 local stream finish 的分发
  - adapter 的 `river_cloud_xiaozhi_stream_push_frame(...)` 现不再直接内联：
    - `cancel_endpoint_soft_close("speech_resumed")`
    - `silence_frames++`
    - `duplex_soft_endpoint_enabled()` 分支
    - `complete_active_stream_finish(...)`
  - 这继续把 capture path 上的 session truth 和 duplex close 语义从 adapter
    抽离出来，让 adapter 更接近纯推帧/调度壳
- XiaoZhi bridge-close capture teardown 也已继续从 adapter 收口到 session runtime：
  - session runtime 新增 grouped helper：
    - `river_cloud_xiaozhi_apply_bridge_close_capture_policy()`
  - session runtime 现统一负责：
    - bridge-close 时是否需要先完成 active stream finish
    - bridge-close capture finish 与 terminal policy 的时序
  - adapter 的 `river_cloud_asr_audio_close()` 现不再直接内联：
    - `complete_active_stream_finish(..., "bridge_close")`
    - bridge-close finish 后再接 terminal-policy 的顺序
  - 这继续压缩 adapter 在 audio-close / bridge-close 路径上的 session truth
    所有权，让 bridge teardown 更接近 runtime-owned reducer + transport shell
- XiaoZhi bridge-open capture/uplink init 也已继续从 adapter 收口到 session runtime：
  - session runtime 新增 grouped helper：
    - `river_cloud_xiaozhi_apply_bridge_open_capture_policy(...)`
  - session runtime 现统一负责：
    - uplink audio format gate
    - XiaoZhi pre-roll cap normalization
    - uplink ring init/reset 的 bridge-open 归一
    - uplink timestamp / retry / busy 计数器归零
    - bridge-open `uplink audio` / `listen gate` 日志
  - adapter 的 `river_cloud_asr_audio_open()` 现不再直接内联：
    - sample-rate/channel/bit-depth gate
    - `xiaozhi_uplink_ring` init/reset
    - `xiaozhi_uplink_*` timestamp/counter reset
    - `xiaozhi_open_speech_frames = 0`
  - adapter 的 `river_cloud_asr_audio_close()` 也不再额外带一个 XiaoZhi-only
    `xiaozhi_open_speech_frames` reset
  - 这继续把 audio-bridge open/close 路径上的 XiaoZhi capture truth 从
    adapter 收回 session runtime，让 adapter 更接近 generic bridge shell
- XiaoZhi `stream_push_frame()` 里的 capture/session reducer 也已继续从 adapter
  收口到 session runtime：
  - session runtime 新增 grouped helper：
    - `river_cloud_xiaozhi_apply_stream_push_capture_policy(...)`
  - session runtime 现统一负责：
    - inactive-stream 的 followup-open gate
    - pre-roll replay 到 uplink 的激活路径
    - active-stream 的逐帧 feed bookkeeping
    - `stream_active` 激活后的 `asr stream active` 日志与 stats snapshot
  - adapter 的 `river_cloud_xiaozhi_stream_push_frame(...)` 现不再直接内联：
    - pre-roll replay loop
    - `stream_feed_ok/fail` 递进
    - `stream_open_ok/fail` + `stream_started_ms` 激活切换
    - `apply_active_stream_capture_policy(...)`
  - 这继续把 realtime capture path 上的 session truth 从 adapter 收回
    runtime，使 adapter 在 XiaoZhi capture 主链上进一步逼近
    `playback entry/exit wrapper + runtime dispatch shell`
- XiaoZhi `stream_push_frame()` 上最后的 playback wrapper 也已继续从 adapter
  收口到 runtime：
  - session runtime 新增 grouped helper：
    - `river_cloud_xiaozhi_apply_capture_stream_policy(...)`
  - session runtime 现统一负责：
    - capture-entry playback gate
    - stream-push capture reducer dispatch
    - capture-exit playback tail
  - adapter 的 `river_cloud_xiaozhi_stream_push_frame(...)` 现不再直接内联：
    - `apply_capture_entry_playback_policy()`
    - `apply_capture_exit_playback_policy()`
  - 这让 XiaoZhi capture 推帧路径在 adapter 中进一步逼近纯壳形态：
    - 参数校验
    - runtime helper 调用
- 继续前移后，XiaoZhi capture 分支中仅剩的 adapter 本地 shim 也已移除：
  - 删除 `river_cloud_xiaozhi_stream_push_frame(...)`
  - `river_cloud_asr_stream_push_frame(...)` 现直接分派：
    - `river_cloud_xiaozhi_apply_capture_stream_policy(...)`
  - 这让 XiaoZhi capture 的 runtime truth 更明确：
    - adapter 只保留 generic bridge 入口
    - XiaoZhi provider-specific capture policy 不再在 adapter 内落一个额外壳层
- generic capture bridge lifecycle 也开始从 adapter 抽离：
  - 新增 `river_cloud_asr_bridge_runtime.c`
  - runtime helper 现负责基础 bridge state 的 prepare/reset：
    - `audio_desc`
    - `frame_bytes`
    - `pre_roll_buffer`
    - `pre_roll_capacity_frames`
    - `post_roll_frames`
    - `silence_frames`
  - adapter `river_cloud_asr_audio_open()/close()` 现只编排：
    - close old bridge
    - delegate runtime state prepare/reset
    - invoke backend-specific capture open/close policy
  - 这让 generic bridge lifecycle 和 provider policy 开始显式分层
- generic realtime capture reducer 也继续向同一 runtime 聚拢：
  - 迁入 `river_cloud_asr_bridge_runtime.c`：
    - `river_cloud_pre_roll_reset(...)`
    - `river_cloud_pre_roll_store(...)`
    - `river_cloud_stream_finish_active(...)`
  - adapter 现只残留一个 generic reducer：
    - `stream_open_and_flush()`
  - 这让 adapter 在 realtime capture 主链上的职责进一步逼近：
    - gate / dispatch / orchestration
    - 而不是具体状态变异
- 最后一个 generic realtime open reducer 也已迁出 adapter：
  - 迁入 `river_cloud_asr_bridge_runtime.c`：
    - `river_cloud_business_time_ready(...)`
    - `river_cloud_stream_open_and_flush(...)`
  - 这意味着 adapter 侧已不再持有 generic realtime capture reducer 的
    具体实现
  - generic capture 主链当前在 adapter 中已逼近纯壳：
    - 参数校验
    - runtime helper 编排
    - provider/backend 分发
- downlink/playback 恢复路径也开始收口到更明确的 runtime 语义：
  - `rebuffer_pending` 不再只是状态标记，而是 downlink worker 的恢复门控
  - write failure 后若 playback service 仍存活，worker 现会先等待
    adaptive refill threshold 达标，再继续写入
  - 这直接针对日志里的 recovery storm：
    - `write failed -> flush`
    - 紧接着立刻 retry 同帧
    - 但此时队列还没补够，容易再次失败

下一步焦点：

- 继续把 XiaoZhi 与 generic streaming path 之间的桥接边界做最终收口
- 继续审视 downlink/playback 的 starve/rebuffer 恢复策略，把 repeated
  stop/start 收束成更稳定的 runtime truth 与恢复路径
- 优先进入 downlink/playback 恢复重构，处理：
  - `write failed -> flush/rebuffer`
  - `upstream gap -> stop/start`
  - dialog runtime 与 playback runtime 对“recovering”语义的不一致
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

- `upstream_starved` 现已优先走：
  - `stop/rebuffer`
  - runtime-owned detached restart
  而不再默认先走同轨 `flush/restart`
- residual 硬 `write_failed` 继续走 playback-service 的 recover-first
  路径，并仅在 recover 失败时退回 fresh start
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
- rebuffer 本身也已开始从“仅 pending/no-pending”收口为显式原因真相：
  - 新增 runtime truth：
    - `playback_rebuffer_cause`
  - 当前已区分：
    - `upstream_starved`
    - `write_failed`
  - cloud runtime snapshot / dialog runtime snapshot 与诊断日志都已直接导出
    该 cause，后续不再需要从：
    - `underrun`
    - `write failed`
    - `rebuffer requested`
    多条离散日志反推同一次恢复
- downlink 再起播门限也已开始真正消费这条 cause truth：
  - `upstream_starved` 仍可等待 prefetch-sized 回填，避免上游断粮时短队列
    反复打穿
  - `write_failed` 不再无条件继承同一套 upstream-prefetch 等待预算，而是
    回到更紧的本地 rebuffer 门限
  - runtime downlink dump 现在也会直接打印当前 `start` threshold，便于板端
    确认是哪一类恢复策略在生效
- playback runtime 对上层的 truth 发布也已开始从“被动拉取”转成“主动同步”：
  - playback phase 变化时，runtime 现在会直接：
    - `river_cloud_request_state_sync(...)`
  - rebuffer cause 变化但 phase 未变化时，也会主动同步一次
  - 这开始去掉一条残余绕路：
    - 外层先收到 playback-service state
    - 再反向拉 cloud snapshot
    才能让 dialog/core 看见 playback-runtime 新真相
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
- `river xiaozhi status` 的 playback/downlink 诊断投影也已进一步收口到
  playback runtime：
  - 新增 runtime-owned dump helper：
    - `river_cloud_xiaozhi_dump_playback_status(...)`
  - 由 playback runtime 统一导出：
    - playback meta / terminal
    - duplex-ready / duplex policy
    - no-ref reopen guard
    - downlink queue / rebuffer
  - adapter 不再自行拼接这些下行/播放真相字段
- playback/downlink 运行态也开始从布尔散点收口为显式 phase truth：
  - 新增 runtime phase：
    - `idle`
    - `prefetching`
    - `playing`
    - `rebuffering`
    - `draining`
  - `playback_output_active` / `playback_lane_engaged` 现在优先消费这条
    phase truth，而不是继续从：
    - `playback_active`
    - `tts_stop_pending`
    - playback-service active state
    临时拼接
  - cloud runtime snapshot 与 dialog runtime dump 也已开始直接透出：
    - `playback_phase`
  - 这为后续把 `dialog runtime` / `session runtime` / playback worker
    全部统一到同一条下行真相源上打了基础
- 上层 dialog/runtime 派生也已开始切到 phase-first：
  - XiaoZhi cloud snapshot 导出的 `playback_active` 现在对齐为：
    - `playback_output_active`
    而不是旧的 raw active bit
  - dialog runtime 现在优先消费：
    - `playback_phase`
    再在 phase 缺席时才回退到 service/bool 兼容逻辑
  - 这进一步压缩了 recovery/drain 空窗里“上层看到的 playback 真相”
    与 playback runtime 自身真相之间的偏差
- dialog runtime 对底层 playback-service 状态变化的消费也已开始去抖：
  - 当 `playback_phase` 已知且有效 playback/dialog 事实未变化时，
    `note_playback_state()` 不再触发一次空 publish
  - 这减少了 recovery 窗口里“底层状态抖一下，上层也被拍一下”的残余噪声
- dialog runtime 的 `playback_recovering` 也已同步切到 phase-first：
  - phase 已知时，不再让：
    - `RIVER_PLAYBACK_RECOVERING`
    - `RIVER_PLAYBACK_RESTART_PENDING`
    直接主导上层 recovering 判断
  - playback-service recovery state 只保留为 phase 缺席时的兼容兜底
- playback truth 的 cloud->core ingress 也已继续收口到 dialog runtime：
  - 新增 direct callback：
    - `river_dialog_runtime_on_cloud_state_sync(...)`
  - `river_app` 现在直接注册该回调到：
    - `river_cloud_adapter_set_state_sync_handler(...)`
  - app 不再充当 cloud-state-sync 的转发桥
- `session_coordinator_on_playback_state(...)` 这条本地 playback listener
  也已进一步瘦身：
  - 不再在每次 playback-service 状态变化后额外调用
    `river_dialog_runtime_sync_cloud_state(...)`
  - 该 listener 现在只吸收：
    - 本地 playback-service truth
    - 本地 hard playback error
  - cloud/playback runtime 自身 truth 变化改由 runtime 主动发布 state sync
- 这条本地 playback listener 入口也已最终完成收口：
  - 新增：
    - `river_dialog_runtime_on_playback_state(...)`
  - `river_app` 现在直接把它注册到：
    - `river_playback_service_register_listener(...)`
  - `session_coordinator_on_playback_state(...)` 已删除
  - 这意味着 playback-service -> core 的本地事实入口也已和
    cloud-state-sync 一样，直接进入 dialog runtime，而不是再经过 coordinator
- `session_coordinator` 中一部分 ASR 生命周期 cloud-sync bridge 也已继续收口：
  - XiaoZhi session runtime 现在会在以下生命周期事件后主动发布 state sync：
    - `asr_error`
    - `asr_session_started`
    - `asr_session_closed`
  - 随后 `dialog runtime` 已继续接管这部分原子融合入口，`session_coordinator`
    不再保留这些 ASR 生命周期的显式 `sync_cloud_state(...)` bridge
- wake admission 这条 bridge 也已继续向 `dialog runtime` 原子入口收口：
  - 新增：
    - `river_dialog_runtime_note_wake_confirmed_with_cloud_state(...)`
  - `session_coordinator` 的 wake admission 成功路径现在只调用这一个
    dialog-runtime API，不再显式串联：
    - `note_wake_confirmed(...)`
    - `sync_cloud_state("wakeword_detected")`
- boot / ASR lifecycle 也已进一步完成相同模式的收口：
  - 新增 `dialog runtime` 原子入口：
    - `river_dialog_runtime_mark_boot_ready_with_cloud_state(...)`
    - `river_dialog_runtime_note_asr_session_started_with_cloud_state(...)`
    - `river_dialog_runtime_note_asr_session_closed_with_cloud_state(...)`
    - `river_dialog_runtime_note_asr_error_with_cloud_state(...)`
  - `river_app` 与 `session_coordinator` 已不再保留显式
    `sync_cloud_state(...)` bridge
  - 上一切片引入的临时 provider capability API
    `river_cloud_adapter_runtime_self_sync_active()` 也随之删除
- 进一步地，旧的 local-only boot/wake/ASR `dialog_runtime` 入口也已删除，
  避免同一类 truth 同时保留“旧直写入口 + 新融合入口”两套表面
- 在此基础上，这几条 cloud-backed boot/wake/ASR 入口在 dialog runtime
  内部也已进一步收口到单一 typed reducer：
  - 新增内部 event：
    - `BOOT_READY`
    - `WAKE_CONFIRMED`
    - `ASR_SESSION_STARTED`
    - `ASR_SESSION_CLOSED`
    - `ASR_ERROR`
  - 新增统一 helper：
    - `apply_cloud_event_locked(...)`
    - `commit_cloud_event(...)`
  - 这意味着 dialog runtime 内部不再保留五条几乎同构的：
    - 抓 cloud snapshot
    - 写局部事实
    - publish
    路径，进一步逼近真正 reducer-only 的真相源结构
- 进一步地，cloud ASR lifecycle 事件现在也已不再依赖
  `session_coordinator` 做桥接：
  - app 当前直接把 cloud ASR result fanout 给：
    - `dialog runtime`
    - `session_coordinator`
  - 其中 `dialog runtime` 直接吸收：
    - `SESSION_STARTED`
    - `SESSION_CLOSED`
    - `ERROR`
  - `session_coordinator` 仅保留：
    - partial/final 文本日志
    - barge-in interrupt
    - diag flush
  - 这继续删除一条 coordinator-owned 的 cloud->dialog truth bridge
- 再往前一步，`session_coordinator` 对 dialog-runtime snapshot 的规则解读也已
  开始被 typed helper 取代：
  - wakeword admission 当前改为调用：
    - `wakeword_admission_block_reason()`
  - barge-in interrupt 当前改为调用：
    - `allows_barge_in_interrupt()`
  - coordinator 不再为了这两条策略去拉整份 snapshot 并自行解释：
    - `conversation_window_active`
    - `interaction_state`
    - `playback_active`
    - `playback_terminal_state`
    - `asr_session_active`
  - 这继续把 admission/interrupt 规则收回 dialog runtime 这个真相源内部
- playback/downlink 真相源也继续向前收口：
  - 下面 3 个 helper 已从 `river_cloud_xiaozhi_session.c` 移入
    `river_cloud_xiaozhi_playback_runtime.c`：
    - `playback_allows_vad_open()`
    - `capture_held_by_playback()`
    - `apply_tts_start_round_policy()`
  - 这意味着：
    - 播放期间 VAD/AEC 准入规则由 playback runtime 持有
    - `tts_start` 触发时“保留本地 round 还是回退到 half-duplex close”的判定
      也不再散落在 session 文件中
  - `xiaozhi_session.c` 进一步退化为 transport/session 编排层，而不是下行播放
    判定层
- 同一方向再推进一步，soft-endpoint / uplink continuation 语义也已移到
  playback runtime：
  - `duplex_soft_endpoint_enabled()`
  - `duplex_speaking_uplink_continuation_active()`
  - `output_speaking_active()`
  - 这继续减少 `xiaozhi_session.c` 对“server speaking + playback active”
    组合语义的持有，改由 downlink runtime 解释
- 继续下推后，`no-ref reopen/open_hold` 这组也开始统一到 playback runtime：
  - `open_hold_frames_required()`
  - `playback_followup_reopen_ready(bool is_speech)`
  - `maybe_start_followup_round()` 不再直接实现 reopen guard/rearm 细节
  - 这把此前“runtime 写 reopen 状态、session 再单独解释”的 split-brain 继续收口
- `endpoint_soft_close` 这组延迟关闭状态机也已并入 playback runtime：
  - `clear/cancel/note_interrupt_hint/arm/poll`
  - 并补了 typed read helper：
    - `pending()`
    - `remaining_ms()`
    - `reason()`
  - `xiaozhi_session.c` 不再：
    - 直接实现这组状态机
    - 直接读/清零这组内部字段
  - 这让 soft-endpoint 与 speaking-uplink continuation 的配套状态也回到同一个
    downlink truth owner
- 再往前推进一步后，session 对 playback 辅助状态的残余直读继续被削掉：
  - runtime status dump 通过 `playback_rebuffer_pending()` 读取 rebuffer
    状态
  - transport-closed 观测通过 `playback_output_active()` 读取播放活跃态
  - transport reset 不再手工清 `no_ref reopen` 这组字段
  - 这组 reset 现统一收口到 playback runtime：
    - `apply_transport_reset_playback_policy()`
    - `apply_session_start_playback_policy()`
    - `mark_playback_started()`
    - `reset_playback_state()`
  - 这样 `no_ref reopen` 从“状态解释在 runtime、reset 却散在 session”
    继续推进到同一 owner
- 再下一步，voice 侧 follow-up window 的观测也开始通过 dialog runtime 出口：
  - `river_dialog_cloud_conversation_window_active()` 优先读取
    `river_dialog_runtime_get_snapshot()`
  - 使用 `snapshot.conversation_window_active` 作为对外真相
  - cloud adapter 暂时保留为 fallback，避免 runtime 未初始化时失能
  - 这让 voice/KWS 不再把“窗口是否打开”直接绑死在 cloud adapter 回调上
- 再继续一步，XiaoZhi 自身的 round/window 辅助状态也开始从大文件拆出：
  - 新建 `river_cloud_xiaozhi_round_runtime.c`
  - 先承接：
    - `listening`
    - `conversation_window`
    - `listen_stop`
    - `local_close defer`
  - `river_cloud_xiaozhi_session.c` 不再内联实现这组 helper/state machine
  - 这一步的目的不是立刻改变行为，而是先把 owner 边界从
    `xiaozhi_session.c` 里剥离出来，方便下一阶段继续把 round/window 真相
    向 dialog runtime 收口
- playback runtime 内部对“本地播放后端是否仍属于 XiaoZhi”的真相也已继续收口：
  - 新增 typed backend state：
    - `detached`
    - `owned_active`
    - `foreign_active`
    - `restart_pending`
  - downlink worker、rebuffer、pending-stop、abort 现都统一消费这条
    backend truth，而不是分别混用：
    - `xiaozhi_playback_active`
    - `river_playback_service_active()`
    - `RIVER_PLAYBACK_RESTART_PENDING`
  - 这修正了一个重要边界问题：
    - XiaoZhi cleanup/recovery path 不再把“外部占用的活跃播放流”误当成
      自己的后端去 stop/interrupt
  - 诊断日志现在也直接暴露 backend ownership，便于后续板端继续区分：
    - XiaoZhi-owned churn
    - foreign takeover
    - restart-pending recovery
- dialog runtime 对共享 playback-service 的本地入口归属也已继续收紧：
  - 新增本地 owned latch：
    - `local_playback_stream_owned`
  - 当前只吸收对话相关流：
    - `RIVER_PLAYBACK_PRIO_TTS`
  - 一旦已吸收 owned dialog stream，本地入口会继续跟踪其 cleared-config
    终态直到：
    - `RIVER_PLAYBACK_IDLE`
  - 这修正了另一个上层真相泄漏点：
    - `audio_echo` 这类共享 playback-service debug stream
      不再污染 dialog runtime 的 playback / interaction 派生
    - 同时不会因为终态回调里 `config == NULL` 而丢掉已拥有 TTS 流的收尾
- 进一步地，这条本地 playback ingress 现在已从“按 priority 粗分类”收紧到
  “按 app 注册的 dialog stream 白名单”：
  - 新增：
    - `river_dialog_runtime_register_playback_stream(...)`
  - `river_app` 启动时当前注册：
    - `xiaozhi_tts`
    - `iflytek_tts`
  - dialog runtime 现在会锁存已拥有的精确 `stream_name`，并且只继续吸收同一
    stream 的 cleared-config 终态直到：
    - `RIVER_PLAYBACK_IDLE`
  - 这意味着未来即使出现其他共享 `TTS` 优先级流，也不会再因为 priority
    相同就混入 dialog runtime 的 playback / interaction truth
- 再进一步，dialog runtime 对本地 playback-service coarse state 的剩余依赖也已
  收口到 ingress 翻译层：
  - 新增本地 typed truth：
    - `playback_local_active`
    - `playback_local_recovering`
  - 新增入口 helper：
    - `river_dialog_runtime_apply_local_playback_state_locked(...)`
  - `playback_active` / `playback_recovering` 的 aggregate reducer 现在消费：
    - cloud playback phase
    - local typed playback truth
    - playback lane / rebuffer pending
  - 而不再在常态 reducer 内部继续直接分支判断：
    - `RIVER_PLAYBACK_RECOVERING`
    - `RIVER_PLAYBACK_RESTART_PENDING`
    - `river_playback_service_state_active(...)`
  - status dump 也已同步导出 local playback truth，便于板端继续区分：
    - 本地 playback callback 还在不在
    - 还是 cloud playback phase 已完全主导上层真相
- XiaoZhi downlink 对“上游断供”的恢复入口也已继续前移到 runtime 真相：
  - 新增：
    - `xiaozhi_downlink_last_supply_ms`
  - starvation helper 现不再只在 `queued=0` 时才开始判断，而是结合：
    - low-water queued frames
    - adaptive starvation wait
    - last successful supply gap
    触发 `upstream_starved` rebuffer
  - downlink worker 也不再在每次非零队列 / 成功写入循环后立即清掉 starvation
    watch，因此 runtime 可以在真正 `write_failed` 前保留连续的断供观察窗口
  - 这条切片的目标就是把日志里一部分：
    - `underrun -> write_failed -> flush/restart`
    提前改造成更可控的：
    - `upstream_starved -> rebuffer`
- 进一步地，`write_failed` 本身也已开始被压缩成“剩余硬故障”语义：
  - 新增：
    - `river_cloud_xiaozhi_write_failed_prefers_starved_rebuffer(...)`
  - 当 `write_failed` 发生时，runtime 现会先看：
    - low-water queue
    - supply gap
    - 当前队列预算
  - 若该失败已经明显符合上游断供语义，则直接走：
    - `upstream_starved`
    - `stop/rebuffer`
    而不是继续统一走：
    - `flush/restart`
  - 这进一步减少了“同一个供给断档既先记为 starvation，又在写入点再被当成
    本地写链路故障”的语义折叠
- 再下一步，downlink/playback 的 recoverable 恢复动作也已继续收口：
  - playback service 新增显式 recover 语义：
    - `river_playback_service_recover_stream_ex(...)`
  - recover 会先显式推进到：
    - `RIVER_PLAYBACK_RECOVERING`
    再复用现有 in-place flush/restart 路径，并在失败时保留：
    - `RIVER_PLAYBACK_RESTART_PENDING`
    兜底
  - XiaoZhi runtime 现在把 recover 保留给：
    - residual `write_failed`
    - 本地 `stop/rebuffer` 失败后的兜底恢复
  - `upstream_starved` 常态路径已改为：
    - `stop_stream_ex(...)`
    - runtime-owned `rebuffer_pending`
    - 达到 start gate 后再正常 fresh start
  - 这让常见上游断粮不再被重新折叠回本地 flush/restart，同时仍保留
    playback-service recover 处理剩余硬故障
- playback-service 的 `RECOVERING` 语义也已进一步上推到 playback runtime：
  - 新增公共 backend truth：
    - `RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_RECOVERING`
  - `dialog_runtime` 的 `playback_recovering` 常态现在会直接消费：
    - `playback_rebuffer_pending`
    - `owned_recovering`
    - `restart_pending`
  - 这继续压缩了 core 对本地 coarse playback state 的 recovering 依赖，把
    local shadow 更明确地收敛到 phase-unknown fallback
- `dialog_runtime` 内部 local playback shadow 也已继续收口：
  - internal context 不再重复缓存：
    - `playback_local_active`
    - `playback_local_recovering`
  - 当前只保留：
    - `playback_state`
    作为最小 private shadow，再由 helper 按需派生 active/recovering
  - 这继续减少了 runtime 内部阴影态自相复制的漂移面
- 再往前一步，XiaoZhi downlink/playback 的 rebuffer 真相也已继续拆细：
  - `xiaozhi_playback_rebuffer_count` 继续保留为累计诊断计数
  - 新增：
    - `xiaozhi_playback_rebuffer_streak`
  - start-threshold 抬高逻辑现在只消费 active history：
    - `streak`
    而不再把累计总次数直接当成未来所有 restart 的门槛放大器
  - 当某个 segment 被完整播完时，runtime 会显式清掉该 `streak`，把
    “已经稳定播完整段”的事实转成后续更紧的起播门限
  - 这意味着后续板端日志终于能区分：
    - 当前 response 到现在总共 rebuffer 了多少次
    - 当前 start gate 仍是否被连续 recover 历史抬高
  - 对应日志语义也同步改成：
    - `rebuffer_total`
    - `streak`
- 在此基础上，`streak` 的清零条件也已继续收紧到真正的稳定播放事实：
  - playback segment 新增：
    - `rebuffered`
  - 某段一旦经历过本地 rebuffer，就不会因为“最终播完了”而立刻把
    `streak` 清零
  - 只有：
    - 完整播完
    - 且该段本身没经历 rebuffer
    的 clean segment，才会清掉 `streak`
  - recovered segment 现在会显式记录：
    - `reason=segment_recovered`
    并保留 active history
  - clean segment 则显式记录：
    - `reason=clean_segment`
    并真正释放这段恢复历史
  - 这让 `streak` 更接近“连续恢复历史尚未被稳定播放打断”的 typed truth
- playback service 的 control wrapper 也已继续纠正一层假成功语义：
  - `stop/interrupt/flush/recover` 现在不再吞掉底层 control 的返回值后统一
    回 `RIVER_OK`
  - 这让上层 finally 可以正确观察：
    - recover 是否真的原地成功
    - 还是已经掉到 `RESTART_PENDING` / `ERR_BUSY`
  - 也让 downlink runtime 里原本保留的 `recover_status` fallback 诊断终于
    成为真实可触发的观察点，而不是永远打印不到的死分支
- 再往前一步，XiaoZhi downlink 的起播门限也已继续从分散 heuristics 收口成
  一个 typed prefetch/start-gate policy：
  - 新增 policy：
    - `baseline`
    - `rebuffer_fast`
    - `segment_prefetch`
    - `starved_prefetch`
  - runtime 统一计算：
    - `start_frames`
    - `prefetch_frames`
    - `cautious_history`
    不再把这几类判断分散在多个 helper 里独立拼接
  - `audio.out.meta` 的 segment cadence 现在也能直接触发
    `segment_prefetch`，因此“长段/慢供给”场景不再只能靠：
    - base start frames
    - rebuffer streak
    两个粗粒度信号
  - status / prefetch / upstream-gap rebuffer / playback start / rebuffer
    requested 现统一打印同一条 typed gate 语义：
    - `policy`
    - `cautious`
    - `prefetch_frames`
- 在此基础上，typed start-gate 也已继续从“内部现算 helper”收口成 runtime
  snapshot 真相：
  - runtime 现区分：
    - `build_start_gate()`
    - `refresh_start_gate()`
    - `current_start_gate()`
  - 当前 gate 会写入 playback runtime 自身状态：
    - `start_policy`
    - `start_frames`
    - `prefetch_frames`
    - `cautious_history`
  - 影响 gate 的状态变更点现在都会主动刷新 snapshot，例如：
    - meta clear
    - downlink/playback reset
    - rebuffer enter/finish
    - clean segment 清掉 `streak`
    - meta prefetch 目标更新
    - frame-duration 变化
  - 这让 worker / start / status / rebuffer diagnostics 读取的是同一份
    runtime-owned gate truth，而不是各自拿原始字段重建
- 这组 start-gate snapshot 现已继续上推到 cloud/dialog runtime 的公开真相：
  - cloud runtime snapshot 新增：
    - `playback_start_policy`
    - `playback_start_frames`
    - `playback_prefetch_frames`
    - `playback_start_cautious_history`
  - dialog runtime snapshot 同步新增同名字段
  - XiaoZhi session fill path 不再重算 gate，而是直接投影 playback runtime
    已锁存的 snapshot
  - dialog runtime dump 现在也直接打印：
    - `start_gate`
    - `prefetch`
    - `cautious`
  - 这让上层 runtime/status 在观察：
    - `playback_phase`
    - `rebuffer_cause`
    的同时，也能看到当前真正生效的起播门限链路
- 再进一步，cloud runtime snapshot 里属于 playback/downlink 的字段也已开始
  由 playback runtime 自己投影：
  - 新增 helper：
    - `fill_playback_runtime_snapshot(...)`
  - `session.c` 不再手工拼接：
    - playback phase
    - rebuffer cause
    - terminal state
    - start-gate fields
  - 这继续减少 `session runtime` 对 playback 内部细节的代管，让：
    - playback truth
    - playback snapshot projection
    - playback status diagnostics
    三者 ownership 更一致

下一步焦点：

- 继续把本地 `flush/restart` 收窄到更少的真正硬故障场景，并评估是否还能把
  某些 stop/restart 恢复再进一步收敛成更轻量的原地 resume 语义
- 继续把 `write_failed` 路径收紧成“前置 starvation 没拦住时的剩余硬故障”，
  逐步压缩本地 flush/restart 在常态恢复路径中的占比
- 继续把这组已上推的 start-gate 真相从“诊断可见”推进到“上层 typed 恢复语义
  可消费”，例如评估：
  - dialog/session 是否需要直接感知当前 gate 偏保守的原因
  - follow-up / playback tail 策略是否要消费这条门限真相
- 继续检查 `segment_prefetch` 是否还需要纳入更多段级真相，例如：
  - 当前 segment 的恢复历史
  - 相邻 meta 到达抖动
  - 尾段/末段的保守放宽
- 继续把 dialog runtime 的本地 playback 入口从“app 注册白名单”推进到更少
  手工注册、更强 provider/runtime typed ownership truth，避免未来 provider
  扩展时白名单再次扩散到 app 装配层
- 继续检查 `priority == TTS` 这层 dialog playback ingress 识别是否还需要再
  继续下沉到 provider/runtime typed ownership helper，避免未来如果出现：
  - 非对话类 TTS
  - 多 provider 并行 TTS
  时重新把判定逻辑扩散回 core/app
- 继续把 local playback shadow 从剩余行为路径里剥离：
  - interrupt clear
  - interaction fallback
  - error/recovery 兜底
  最终只保留在 `playback phase unknown` 的退化模式下
- 继续把 `dialog_runtime` 里剩余“playback listener 先改本地态、再等后续
  cloud sync 修正”的路径改成同一次 reducer 内完成 truth merge，避免：
  - stale cloud snapshot
  - local edge 抢跑 aggregate 派生
- 继续评估 dialog runtime 是否还能把最后保留的 `playback_state` 诊断字段也
  进一步退化成 purely-diagnostic shadow，避免后续代码再次把它当回派生真相
- 继续检查 `dialog runtime` 内部是否还存在“先写局部事实，再补抓 cloud
  snapshot”的重复模式，进一步收成更少的 reducer 入口；boot/wake/ASR
  云端融合入口已完成一轮收口，后续继续看 error/playback 相关入口
- 继续把 boot/wake/asr/playback 这几类入口统一成更少的 typed reducer，
  让 dialog runtime 真正成为唯一显式真相源
- 继续检查 `session_coordinator` 是否还保留其他只做“转发本地事实到
  dialog runtime”的薄桥接入口，能删则删，不能删则继续收敛成 typed reducer
- 继续把 downlink/playback 侧剩余对旧 playback-service coarse state 的兼容
  依赖压缩到 phase 缺席兜底
- 继续把 playback runtime 内部剩余直接依赖 raw playback-service state 的
  判定压缩到 backend-truth helper 或更上层 phase truth：
  - 减少 worker/start path 上零散 `state/active` 读取
  - 让“是否需要 restart / 是否只是 foreign active”不再散落
- 继续把 `session_coordinator` 中剩余的其他非 playback 非 wake 非 ASR 的
  交互桥审视一遍，让 dialog runtime 最终只通过：
  - direct cloud sync ingress
  - local playback/asr/wake 事实入口
  吸收状态，而不是再由 coordinator 混合“写本地 truth + 触发一次云端重拉”
- 继续把 `playback_rebuffer_cause` 从“已导出诊断真相”推进到“上层可消费的
  typed 恢复语义”，让 dialog/session 不再只知道“正在恢复”，还知道：
  - 是上游供给断档
  - 还是本地写链路失败
- 继续把 dialog/core 中剩余的 playback lane / output lane 派生去掉
  对旧 service-active 兼容路径的常态依赖，只把它保留为 phase 缺席兜底
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
- 当前下一步继续聚焦：
  - 继续检查 `restart_pending` 是否仍在 duplex / capture / AEC gate 上被过度
    当成“播放占用中”
  - 继续评估 detached fresh-start 与 attached resume 门限是否还需进一步统一成
    更精确的 typed start policy
  - 继续把 restart churn 收口到真正的硬故障，而不是常态恢复

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

## 9. 编写建议

- 优先推进“唯一真相源”和“媒体语义收口”，不要先做纯文件拆分。
- 每一步都要在 `.codex/changes.md`、`.codex/verification.md` 和 active context 中留下闭环记录。
