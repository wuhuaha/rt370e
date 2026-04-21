# Codex Active Context

This file is the canonical volatile context for Codex-facing work in this
repository. Update it when the working branch, active objective, SDK baseline,
or top-of-tree verification target changes.

## Active Working Set

- Current working branch: `agent-server-v2`
- Active SDK baseline: `/root/ameba-rtos`
- Active build command:
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'`
- Active flash command:
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 /root/ameba-river/tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'`
- Active monitor command:
  - `python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000`
- Latest landed step:
  - `5.364 remove coarse playback last-segment shadow bool`
- Latest workflow sync:
  - future `git commit` messages in this repository should use clear Chinese
    descriptions by default
- Latest planning sync:
  - newest landed runtime-ownership slice:
    - XiaoZhi playback runtime 已移除残留的 coarse global
      `xiaozhi_playback_last_segment` shadow bool
    - 当前 meta 的 terminal 语义现在改由：
      - event-local `is_last_segment`
      - stored terminal last-segment context
      共同提供
    - queued segment 的 `is_last_segment` 也已直接写入 event fact，不再经过
      额外 runtime shadow bool 中转
  - newest landed runtime-ownership slice:
    - XiaoZhi playback runtime 已显式抽出 typed downlink supply truth：
      - `CURRENT_SEGMENT`
      - `WAITING_NEXT_SEGMENT`
      - `TERMINAL_TAIL`
      - `NONE`
    - `maybe_rebuffer_starved()` 不再直接读取 coarse global
      `xiaozhi_playback_last_segment`
    - terminal tail / 无供给真相窗口现在会直接清掉 starvation watch，只让
      “当前段仍在消费”或“明确等待下一段”继续进入 upstream-starved 判定
  - newest landed runtime-ownership slice:
    - playback runtime 已独立保存 waiting-segment context：
      - `response_id`
      - `playback_id`
      - `segment_id`
    - `WAITING_SEGMENT` 不再由 `meta_valid + !last_segment` 这组 global shadow
      推导
    - non-terminal meta 会写入待续段上下文，terminal/invalid meta 会清掉它
  - newest landed runtime-ownership slice:
    - playback start-gate 的 predictive segment-prefetch 已直接读取
      queue 头段/当前待播 segment
    - `segment_prefetch_target_needed()` 不再继续把
      `meta_valid/last_segment` 当成“当前待播段”真相
    - 这继续把起播门限策略从 coarse global meta shadow 收口到
      runtime-owned segment queue truth
  - newest landed runtime-ownership slice:
    - playback runtime 已独立保存 terminal last-segment context：
      - `response_id`
      - `playback_id`
      - `segment_id`
    - `completed_ready` / terminal wait 不再把“当前最近一条 meta 的
      segment_id”当成最后一段真相
    - completed ACK 也会优先消费这份 terminal response/playback context，
      进一步减少 terminal close 被全局 shadow 扰动
  - newest landed runtime-ownership slice:
    - playback ACK / terminal 路径已开始拆分：
      - response-level context
      - segment-level context
    - `started/mark` 不再被全局 `meta_valid` 粗短路
    - `cleared/completed` 也不再错误依赖当前 `segment_id` 仍然挂在全局 meta 上
  - newest landed runtime-ownership slice:
    - `write_failed` rebuffer 不再默认走同一套 `stop_rebuffer`
    - 本地写链路失败现在优先 attached `service_recover`
    - 上游 starved 仍优先 stop/fresh-start，避免把两类问题继续折叠成同一套
      stop/restart 风暴
  - newest landed runtime-ownership slice:
    - downlink worker 现在会先让 rebuffer gate 决定 attached resume /
      detached fresh-start
    - `OWNED_RECOVERING` 不再在真正 `finish_playback_rebuffer()` 前就把 loop
      提前短路
    - rebuffer 恢复链现在会在 gate 后刷新一次 backend truth，再进入 start/wait
      分支
  - newest landed runtime-ownership slice:
    - `playback_backend_state()` 不再只因 phase 仍是 `rebuffering` 就无条件
      投影 `owned_recovering`
    - rebuffer phase 与 backend attached/owner truth 继续拆开
    - stop-after-rebuffer 之后若 backend 已 detached，downlink resume 现在能
      正确识别 fresh-start 条件
  - newest landed runtime-ownership slice:
    - XiaoZhi playback runtime 已显式拆出：
      - `playback_turn_active()`
      - `playback_has_work()`
    - retained playback turn 与 downlink worker live-ness 不再共用
      `lane_engaged/meta_valid` 这套粗粒度判定
    - downlink worker / transport poll / interrupt/config/window gate 现在各自
      消费对应层级的 playback truth
  - newest landed runtime-ownership slice:
    - XiaoZhi playback runtime 现在会把 `rebuffering` 也视为 quiet window
    - 重缓冲静默期不再因为 `lane_engaged` 继续错误 hold capture/VAD
    - 这继续把“response 仍在继续，但当前无真实输出”的阶段从 generic
      playback-held 语义里拆出来
  - newest landed runtime-ownership slice:
    - server `output_state=speaking` 不再在 `prefetching/idle` 阶段直接投影成
      cloud duplex 的 speaking output
    - `output_speaking_active()` 现在只会在 runtime phase 仍保留 output turn 时，
      才接受 transport-side speaking state
    - 这继续减少了首段预取窗口里 soft-endpoint / uplink continuation 被
      transport 语义抢跑的问题
  - newest landed runtime-ownership slice:
    - `tts_start` 不再只凭预判的 duplex/AEC fallback 就立即关闭本地 round
    - 现在只有当 playback runtime 当前确实 `capture_held=yes` 时，才会执行
      server-response close
    - `playback_started` 也新增了对应 policy，把 half-duplex 关轮延后到真实
      起播并开始 hold capture 的时刻
  - newest landed runtime-ownership slice:
    - XiaoZhi playback runtime 现在会把 `prefetching` 也视为 quiet window
    - `playback_allows_vad_open()` 与 `capture_held_by_playback()` 不再把
      prefetch-only lane occupancy 当成 generic playback hold
    - 这继续把 pre-start 窗口从粗粒度 `lane_engaged` 语义里拆出来，避免尚未
      出声就继续卡住 capture/VAD
  - newest landed runtime-ownership slice:
    - `tts_stop_pending` 后只要本地已无真实有声输出，playback runtime 现在就会把
      该窗口视为 quiet window
    - `playback_allows_vad_open()` 与 `capture_held_by_playback()` 不再让这种
      terminal tail wait 继续走 generic playback-held 语义
    - 这继续把 capture hold 从 lane occupancy 收口到真实媒体输出真相
  - newest landed runtime-ownership slice:
    - `tts_stop_pending` 建立后，detached backend 上残留的 downlink 队列
      不再由 worker 间接复活
    - playback runtime 现在会主动丢弃这部分 detached residual audio，并继续走
      terminal completed 收口
    - downlink worker 也不再在 stop_pending 期间 fresh-start 非
      `owned_active` backend
  - newest landed runtime-ownership slice:
    - XiaoZhi playback 的 `playback_output_active` 不再只由
      `playing/draining` phase 直接投影
    - 现在要求 phase 仍位于 output-active 窗口，且 backend 仍保持
      `owned_active`
    - 这继续把 cloud/dialog/duplex 看到的“还在播”真相从 phase-only 收口到
      playback runtime 自己的 backend owner truth
  - newest landed runtime-ownership slice:
    - XiaoZhi playback runtime 已把 `backend_attached` 与“媒体 active”语义
      明确拆开
    - stop/abort/pause 等 control path 不再沿用模糊的 `backend_owned`
      命名
  - newest landed runtime-ownership slice:
    - `dialog_runtime` 的 managed playback-error 吸收与本地 no-op 吸收，
      现在都改为依赖 `cloud_runtime_available`
    - `owned_paused` 也被视为受控 playback owner 过渡真相
  - newest landed runtime-ownership slice:
    - `dialog_runtime` 的 `playback_lane_engaged` 不再只因
      `phase unknown` 就默认保留 output ownership
    - lane fallback 现在也只在拿不到 cloud runtime snapshot 时才保留
  - newest landed runtime-ownership slice:
    - `dialog_runtime` 现在只会在拿不到 cloud runtime snapshot 时，才让
      本地 playback shadow 回填 `playback_active/recovering`
    - `phase unknown` 本身不再构成 local shadow 介入常态派生的理由
  - newest landed runtime-ownership slice:
    - `dialog_runtime` 不再在 `phase unknown` 下让本地 playback shadow
      旁路清理 `tts_interrupt_requested`
    - cloud snapshot merge 与本地 playback idle 都统一收口到
      `output_turn_quiesced` 这条派生真相
  - newest landed runtime-ownership slice:
    - XiaoZhi `backend_state` 不再因为 phase 仍是 `playing/draining`，
      就在 playback-service 已不活跃时继续投影 `owned_active`
    - backend occupancy 与 media/output phase 继续拆开，各自保持独立真相
  - newest landed runtime-ownership slice:
    - `dialog_runtime` 已开始把 `playback_backend_state_kind=owned_paused`
      视为 managed playback transition
    - 本地 `playback_error` 若落在 XiaoZhi backend 自己的 pause/detach 窗口，
      不再轻易放大成独立 `error_recovering`
  - newest landed runtime-ownership slice:
    - XiaoZhi playback backend truth 新增 `owned_paused`
    - `backend_state()` 现在会同时吸收：
      - playback runtime phase
      - playback-service 当前 stream owner
    - downlink/start 不再把“本地 backend 还在 pause/detach 过渡”直接当成
      detached/foreign 去 fresh-start
  - newest landed runtime-ownership slice:
    - `dialog_runtime.snapshot.playback_active` 在 phase 已知时不再因
      `playback_lane_engaged=yes` 自动保持为 `true`
    - `prefetching` / `waiting_segment` 这类无真实媒体输出的阶段不再投影成
      core 侧 playback-active
    - `playback_error` 的 managed-recovery 判定也不再把 generic lane
      occupancy 当成证据
  - newest landed runtime-ownership slice:
    - `dialog_runtime` 的 output turn 不再因
      `playback_lane_engaged=yes` 就自动视为还在“说话”
    - phase 已知时，当前只让：
      - `playback_active`
      - `playback_recovering`
      - `rebuffering`
      继续保留 output-turn ownership
    - `prefetching` / `waiting_segment` 这类无媒体 backing 的 lane 占用不再
      直接投影成 `speaking` / `barge_in_listening`
  - newest landed runtime-ownership slice:
    - `dialog_runtime` 的本地 playback 入口不再分成：
      - 一次只更新 `local_playback_stream_owned`
      - 再单独一次吸收 cloud snapshot + local playback state
    - 本地 playback listener 现在在同一 reducer 内完成：
      - ownership 识别
      - cloud snapshot merge
      - local playback shadow 更新
      - aggregate interaction 派生
    - 这继续减少了 `dialog runtime` 里“局部状态先抢跑，再等后续
      cloud sync 修正”的窗口
  - newest landed runtime-ownership slice:
    - `prefetch_segment` 起播门限现在不再只依赖历史 `meta_gap`
    - 当当前 `audio.out.meta` 已有效、当前段不是最后一段、且预测
      `prefetch_target_ms` 已明显高于基线预算时，也会直接进入
      `PREFETCH_SEGMENT`
    - 这让首次起播和段间恢复在尚未积累历史断供前，也能直接消费当前
      `expected_duration_ms` 建立更保守的预取预算
  - newest landed runtime-ownership slice:
    - XiaoZhi downlink `write_failed` 现在统一先走：
      - `stop_stream_ex(...)`
      - runtime-owned `rebuffer_pause`
    - 不再按 `recover_cause` 把普通 `write_failed` 默认分流回
      playback-service `recover`
    - `recover_stream_ex(...)` 现只保留为 stop 失败时的兜底语义
    - 这一步继续把 playback backend 的恢复入口收口到单一 `stop/rebuffer`
      真相链
  - newest landed runtime-ownership slice:
    - `dialog_runtime` 内部 local playback shadow 已从：
      - `playback_state`
      - `playback_local_active`
      - `playback_local_recovering`
      收成单一 private `playback_state`
    - phase-unknown fallback 与 diagnostics dump 现在都按需从
      `playback_state` 派生 active/recovering
    - 这一步继续压缩了 runtime 内部重复缓存的本地 playback 事实，减少内部阴影态
      漂移
  - newest landed runtime-ownership slice:
    - playback runtime 现在会把本地 playback-service 的
      `RIVER_PLAYBACK_RECOVERING` 上推成 cloud-owned backend truth：
      - `RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_RECOVERING`
    - XiaoZhi downlink worker 在 `owned_recovering` 时不再误走 fresh start
    - `dialog_runtime` 的 `playback_recovering` 常态也开始直接消费：
      - `owned_recovering`
      - `restart_pending`
    - 这一步继续把 recovering 语义从本地 playback listener shadow 推进到
      playback runtime 自己导出的 typed truth
  - newest landed runtime-ownership slice:
    - XiaoZhi playback 在 `upstream_starved` 场景下已不再默认走同轨
      `recover`
    - `upstream gap rebuffer` 与 starved `write_failed` 现在都会优先执行：
      - `stop_stream_ex(...)`
      - runtime-owned `rebuffer_pause`
    - 只有在本地 stop 自身失败时，才会回退到 playback-service recover
    - 这一步把常见上游断粮从本地 `RECOVERING/flush-restart` 语义里拆开，
      让 residual 硬 `write_failed` 才继续保留 recover 语义
  - newest landed runtime-ownership slice:
    - `dialog_runtime` 现在已显式吸收 `wake_admission_pending`
    - wakeword detection / admission gating 会直接把
      `wake_admission_pending` 当成阻断真相
    - `wake_admission` bridge 在 queued / accepted / failed 转移上同步维护这条
      runtime truth
    - 这一步继续把 wake admission 从“bridge 内部局部 pending/retry 状态”
      推进到“runtime 也显式知道已有待处理 wake”，从而减少重复检测和被动 coalesce
  - newest landed runtime-ownership slice:
    - `session_coordinator` 已不再直接读取
      `river_voice_kws_wake_handoff_block_reason()`
    - `wake_admission_submit()` 现在会先吸收 KWS handoff debug blocker，
      再吸收 `dialog_runtime` 的 wakeword admission blocker
    - wakeword 的 blocked / queued / coalesced 日志也已统一收口到
      `wake_admission` 入口
    - 这一步继续把唤醒接力从“协调器先做一层 KWS 私有判定、准入桥再做一层
      runtime 判定”，收口成 core-owned 的单一提交边界
  - newest landed runtime-ownership slice:
    - `dialog_cloud_port` 上已无消费者的 `conversation_window_active`
      侧门已被删除
    - app 装配层也已同步移除对应 callback wiring
    - `conversation_window` 的读取真相现继续只归 `dialog_runtime` 所有
    - 这一步继续压缩了 cloud-port 上残留的旧查询侧门，让 port 更接近纯 command
      ingress 边界
  - newest landed runtime-ownership slice:
    - wakeword detection 与 wake admission 现在共享同一条
      `dialog_runtime` 阻断真相
    - `dialog_runtime` 已导出：
      - `river_dialog_runtime_wakeword_detection_block_reason()`
      - `river_dialog_runtime_allows_wakeword_detection()`
    - `river_voice_kws_detection_allowed()` 已不再自行拼
      `conversation_window + interaction_state`
    - 这一步继续把 wakeword gating 从 voice/KWS 层的 raw 条件判断，
      收口到 `dialog runtime` 的统一结论
  - newest landed runtime-ownership slice:
    - `dialog_runtime` 现在已把 `tts_interrupt_requested` 的清理条件统一收口到：
      - `output_turn_quiesced`
    - 本地 playback idle 回调与 cloud snapshot sync 不再各自拼一套 raw 清理条件
    - cloud sync 现在也会先刷新 playback 派生，再决定是否清 interrupt latch
    - 这一步继续把 interrupt-clear policy 从分裂的边缘条件判断，
      收口到同一个 runtime owner 派生
  - newest landed runtime-ownership slice:
    - `dialog_runtime` 现在已显式区分：
      - 有声 playback active
      - output turn 仍被占用、但当前处于 silent gap
    - `waiting_segment` 不再仅因 `playback_lane_engaged` 就继续把
      `playback_active` 维持为 `true`
    - `river_dialog_runtime_allows_barge_in_interrupt()` 与 interaction 派生
      已改为依赖新的 output-turn-engaged 语义，而不是把 silent gap 伪装成
      `playback_active`
    - 这一步继续把 `dialog runtime` 从“用错误的 active 投影维持 barge-in”推进到
      “显式区分 audible playback 与 response turn ownership”
  - newest landed runtime-ownership slice:
    - `waiting_segment` 不再只是一条诊断/phase 真相，也开始直接参与 duplex /
      capture 语义
    - XiaoZhi playback runtime 现在会把段间静默视为：
      - response 尚在继续
      - 但本地 VAD / capture / soft-endpoint 已可重新打开
    - `river_cloud_xiaozhi_playback_allows_vad_open()` 在 `waiting_segment`
      时直接返回 `true`
    - `river_cloud_xiaozhi_capture_held_by_playback(...)` 在
      `waiting_segment` 时直接返回 `false`，并清空 fallback reason
    - 这一步继续把 downlink/playback 从“lane 仍 engaged 就继续 hold capture”的
      粗粒度语义，推进到“显式区分段间静默和真正的播放/AEC 阻断”
  - newest landed runtime-ownership slice:
    - playback owner 新增了显式段间等待 phase：
      - `RIVER_CLOUD_PLAYBACK_PHASE_WAITING_SEGMENT`
      - `waiting_segment`
    - XiaoZhi downlink/playback runtime 现在会在“当前 segment 已播完、下一段尚未到达”
      时主动 pause backend，并把 phase 收口到 `waiting_segment`
    - 这让 inter-segment 供给间隙不再默认继续滑入：
      - `underrun`
      - `playback_write_failed`
      - `rebuffer requested`
      这条本地恢复路径
    - 这一步继续把 downlink/playback 的恢复模型从“被动等本地故障”推进到
      “先显式承认上游段间等待”
  - newest landed runtime-ownership slice:
    - playback owner 的 `backend state` 已从两个跨层布尔投影收口成公共
      typed truth：
      - `river_cloud_playback_backend_state_t`
      - `playback_backend_state_kind`
    - `dialog_runtime` 的公开 snapshot 已删除：
      - `playback_backend_owned`
      - `playback_backend_restart_pending`
      并改为直接吸收 `playback_backend_state_kind`
    - XiaoZhi downlink/playback runtime 的 backend ownership / restart-pending
      判断现在直接用公共 enum 维护，不再保留私有 backend-state 类型
    - 这一步继续把 downlink/playback backend ownership 真相从跨层布尔投影，
      收口到公共 owner typed truth
  - newest landed runtime-ownership slice:
    - playback owner 的 `phase` 已从 XiaoZhi 私有 enum 提升成公共
      typed truth：
      - `river_cloud_playback_phase_t`
      - `playback_phase_kind`
    - `dialog_runtime` 的公开 snapshot 已删除字符串：
      - `playback_phase`
      并改为直接吸收 `playback_phase_kind`
    - XiaoZhi downlink/playback runtime 的 `phase` 现在直接用公共 enum 维护，
      不再保留私有 playback-phase 类型
    - 这一步继续把 downlink/playback 的 phase 真相从私有 enum / 跨层文本字段，
      收口到公共 owner typed truth
  - newest landed runtime-ownership slice:
    - playback owner 的 `terminal state` 已从内部字符串真相提升成公共
      typed truth：
      - `river_cloud_playback_terminal_state_t`
      - `playback_terminal_state_kind`
    - `dialog_runtime` 的公开 snapshot 已删除字符串：
      - `playback_terminal_state`
      并改为直接吸收 `playback_terminal_state_kind`
    - XiaoZhi downlink/playback runtime 的 terminal reset / ack 映射 /
      local fallback 已全部收口到公共 enum
    - 这一步继续把 downlink/playback 的 terminal 语义从内部字符串与跨层文本
      投影，收口到公共 typed truth
  - newest landed runtime-ownership slice:
    - playback owner 的 `start policy` 已从 XiaoZhi 私有 enum 提升成公共
      typed truth：
      - `river_cloud_playback_start_policy_t`
      - `playback_start_policy_kind`
    - `dialog_runtime` 的公开 snapshot 已删除字符串：
      - `playback_start_policy`
      并改为直接吸收 `playback_start_policy_kind`
    - 这一步继续把 downlink/playback 的启动门限决策从私有 owner enum 和跨层文本
      收口到公共 typed truth
  - previous runtime-ownership slice:
    - playback owner 的 `rebuffer cause` 已从 XiaoZhi 私有 enum 提升成公共
      typed truth：
      - `river_cloud_playback_rebuffer_cause_t`
      - `playback_rebuffer_cause_kind`
    - `dialog_runtime` 的公开 snapshot 已删除字符串：
      - `playback_rebuffer_cause`
      并改为直接吸收 `playback_rebuffer_cause_kind`
    - 这一步继续把 downlink/playback 的恢复语义从私有 owner enum 和跨层文本字段
      收口到公共 typed truth
  - previous runtime-ownership slice:
    - `dialog_runtime` 的公开 snapshot 已删除：
      - `playback_state`
      - `playback_local_active`
      - `playback_local_recovering`
    - 本地 playback shadow 现在只保留在 runtime 内部，用于：
      - stream ownership ingress
      - phase-missing fallback
      - diagnostics dump
    - 这一步继续把对外 `dialog runtime` 真相面收口到 typed owner truth，而把
      local playback shadow 明确降为内部实现细节
  - previous runtime-ownership slice:
    - `dialog_runtime` 已把剩余 local playback shadow 的：
      - active fallback
      - recovering fallback
      收口到显式 `phase unknown` helper
    - `compute_playback_active_locked()` /
      `compute_playback_recovering_locked()` 不再直接把
      `playback_local_*` 混入 phase-known 常态派生
    - interrupt-clear 的 local shadow 阻断路径也已复用同一 fallback helper
    - 这一步继续把 local playback shadow 压回：
      - purely-diagnostic local signal
      - phase-missing fallback
  - previous runtime-ownership slice:
    - cloud playback runtime 已把 terminal wait 从 generic bool/text 提升成
      typed truth：
      - `playback_terminal_wait_kind`
    - XiaoZhi owner 直接输出：
      - `await_last_segment_meta`
      - `await_segment_queue_drain`
      - `await_last_segment_tail`
    - `dialog_runtime` 已开始直接消费这条 typed wait truth，只把：
      - queue drain
      - last segment tail
      当成真正的尾段等待；`await_last_segment_meta` 不再被误压成尾段静默
      投影
    - 这一步继续减少了 core 对 generic `terminal_waiting` 的过度二次解释
  - previous runtime-ownership slice:
    - 当 `playback_phase_known` 已成立时，`dialog_runtime` 不再让
      `playback_local_active` 阻断 interrupt latch 清理
    - local playback shadow 现在进一步退回到 `phase unknown` 的兜底路径
    - 这一步继续减少了 core 行为层对本地 playback listener shadow 的常态依赖
  - previous runtime-ownership slice:
    - `dialog_runtime` 已删除 app 侧 dialog playback stream 注册依赖
    - dialog playback ingress 现在直接按 `RIVER_PLAYBACK_PRIO_TTS` 识别
    - `river_app_boot()` 不再装配 `xiaozhi_tts` / `iflytek_tts` 白名单
    - 这一步继续把 dialog playback ownership truth 从 app wiring 收回 runtime
  - previous runtime-ownership slice:
    - `dialog_runtime` 的 playback ingress 现在会先抓取当前 cloud runtime
      snapshot，再吸收本地 playback state callback
    - `managed recovery` / `interrupt latch clear` / interaction 派生现在都基于
      同一时刻的 playback owner truth，而不是前一次残留的 cloud playback 视图
    - 这一步继续削弱了本地 `RIVER_PLAYBACK_*` listener 作为 aggregate truth
      source 的地位
  - created a dedicated runtime re-architecture track for the recurring
    device-side latency / playback churn issues:
    - architecture review:
      - `doc/VOICE_RUNTIME_ARCHITECTURE_REVIEW_ZH_2026-04-17.md`
    - active execution plan:
      - `doc/VOICE_RUNTIME_REARCHITECTURE_EXECUTION_PLAN_ZH.md`
  - first landed slice on that plan:
    - added a core-owned dialog cloud port so `river_voice` no longer calls
      `river_cloud_*` directly
    - hardened XiaoZhi uplink pacing with:
      - retry-preserved in-flight frame ownership
      - bounded burst drain
      - round-level pacing metrics
  - second landed slice on that plan:
    - introduced a core-owned `dialog runtime` truth source
    - moved boot / cloud-state / playback / ASR interaction-state derivation
      behind that runtime
    - removed the old `session_coordinator` coarse phase truth source
    - added a generic cloud runtime snapshot export so `river_core` no longer
      reaches into XiaoZhi runtime details to derive state
  - third landed slice on that plan:
    - extracted XiaoZhi downlink / playback media logic into a dedicated
      `river_cloud_xiaozhi_playback_runtime.c`
    - moved playback meta ownership, ACK progress, rebuffer/retry handling,
      decoder/downlink worker logic, and playback start policy behind exported
      runtime APIs
    - `river_cloud_adapter.c` now consumes the playback runtime boundary
      instead of embedding duplicated playback/downlink implementations
  - fourth landed slice on that plan:
    - playback runtime now owns:
      - `output_active`
      - `has_work`
      - `abort`
      terminal semantics for XiaoZhi playback
    - adapter close/interrupt/config-refresh branches no longer assemble
      playback stop/reset logic from raw flags and ring/meta checks
  - fifth landed slice on that plan:
    - playback runtime now also owns the remaining playback-local helper set:
      - playback meta clear
      - stop cancel / stop arm
      - playback start / reset
      - downlink reset
      - duplex-ready playback note
    - `river_cloud_xiaozhi_session.c` no longer implements playback-owned
      reset/meta/stop helpers
    - follow-up timeout and XiaoZhi interrupt admission now key off runtime
      playback predicates instead of direct raw playback fields
  - sixth landed slice on that plan:
    - playback runtime now distinguishes a sustained upstream supply gap from a
      later hard write failure one step earlier
    - empty-downlink starvation is converted into a controlled
      `xiaozhi_playback_starved` rebuffer path after a bounded timeout instead
      of waiting for the AudioTrack path to fail first
    - starvation watch state is reset together with playback/downlink runtime
      restart points so the recovery path stays deterministic
  - seventh landed slice on that plan:
    - playback service now exposes an explicit `recovering` state instead of
      collapsing write-path churn into fatal `playback_error`
  - latest landed runtime-ownership slice:
    - session runtime now owns the full XiaoZhi transport-event dispatch shell:
      - per-event `refresh_turn_semantics("event")`
      - grouped event switch / reducer routing
    - adapter `river_cloud_xiaozhi_event_handler(...)` is now reduced to a
      transport callback shim that only forwards `river_xiaozhi_event_t`
      objects into runtime
  - previous runtime-ownership slice:
    - cloud playback runtime snapshot 已继续补齐 backend ownership truth：
      - `playback_backend_owned`
      - `playback_backend_restart_pending`
    - `dialog_runtime` 已开始直接消费这两条 typed truth
    - local `IDLE` 清 interrupt latch 与 managed recovery 分类都已开始受
      backend restart-pending 保护
    - 这一步继续降低了 core 对本地 playback listener 瞬时边缘事件的依赖
  - previous runtime-ownership slice:
    - `dialog_runtime` 已不再依赖 playback phase 字符串做行为判断
    - `"playing" / "draining" / "rebuffering"` 这些文本现在只保留给日志投影
    - playback active / recovering / managed-recovery 派生已进一步只消费 typed
      playback truth
    - 这意味着 core 行为层已继续远离：
      - `strcmp(playback_phase, "...")`
      这类 owner 外字符串解释
  - previous runtime-ownership slice:
    - cloud playback runtime snapshot 新增 typed bool：
      - `playback_phase_known`
      - `playback_terminal_closed`
    - `dialog_runtime` 现在直接吸收并消费这两条 playback owner truth
    - core 侧对：
      - `playback_phase[0] != '\0'`
      - `playback_terminal_state[0] != '\0'`
      这类字符串侧推已开始退场
    - `allows_barge_in_interrupt()` 与 runtime status dump 也已开始转向 typed
      playback truth
  - previous runtime-ownership slice:
    - `dialog_runtime` 已删除公开的 playback-state 侧门：
      - `river_dialog_runtime_note_playback_state(...)`
    - 本地 playback 真相现在继续只经由：
      - `river_dialog_runtime_on_playback_state(...)`
      这条 listener ingress 进入 runtime
    - 本地 `RIVER_PLAYBACK_IDLE` 也不再无条件清掉：
      - `tts_interrupt_requested`
    - 若 cloud playback 真相仍显示 lane engaged / rebuffer / stop-pending /
      terminal-waiting，interrupt latch 会继续保留，直到聚合 playback truth
      真正收口
    - 这一步继续压缩了“本地 playback service 瞬时边缘事件直接改写 runtime”的范围
  - previous runtime-ownership slice:
    - `dialog_runtime` 已删除未再使用的外部错误入口：
      - `river_dialog_runtime_note_error(...)`
      - `river_dialog_runtime_clear_error(...)`
    - coarse `error_recovering` 语义不再允许从 runtime 外部被随意直接改写
    - 后续应继续：
      - 收紧 local playback edge event 对 runtime 聚合态的影响
      - 继续让 playback/downlink owner 自己输出 typed truth
  - previous runtime-ownership slice:
    - `dialog_runtime` 现在在 managed playback recovery 中保留
      `tts_interrupt_requested`
    - 本地 `RIVER_PLAYBACK_ERROR` 若只是 playback runtime 管理下的恢复路径，
      不会再把 interrupt in-flight latch 提前清空
    - 这进一步避免了 recovery 窗口里的重复 barge-in interrupt
    - 下一步应继续：
      - 让更多本地 playback service 边缘信号只作为 playback runtime 内部细节
      - 继续把 stop/reset/rebuffer 的 coarse 解释从 core 侧挤掉
  - previous runtime-ownership slice:
    - `dialog_runtime` 现在会把“受 cloud playback runtime 管理的本地
      playback_error”吸收到 `playback_recovering`
    - 若 cloud playback truth 已表明当前仍在：
      - rebuffering
      - playback_cloud_active
      - playback_lane_engaged
      之一，则不再额外把交互态抬成 `error_recovering`
    - `river_dialog_runtime_on_playback_state()` 也不再对每次
      `RIVER_PLAYBACK_ERROR` 无条件叠加：
      - `note_error("playback_error")`
    - 这一步直接针对日志里频繁出现的：
      - `speaking -> error_recovering -> speaking`
      状态风暴做收口
    - 该步之后，`speaking -> error_recovering -> speaking` 这类风暴已继续被压缩
  - previous runtime-ownership slice:
    - `river_cloud_xiaozhi_dump_session_status()` 现在会先读取
      `river_cloud_runtime_snapshot_t`
    - session status dump 中的：
      - playback active/phase/rebuffer/tts_stop_pending
      - local_close/window remaining
      - turn semantics
      已开始统一走 runtime snapshot 投影，而不是日志路径自己重拼
    - `xiaozhi_session.c` 已去掉对：
      - `g_river_cloud.xiaozhi_tts_stop_pending`
      的最后一处 session-side 直读
    - 该步之后，XiaoZhi session 诊断面也已开始贴近 runtime snapshot 真相源
  - previous runtime-ownership slice:
    - wake admission 的 queued/coalesced/deferred/retry worker 已从
      `session_coordinator` 抽到新的 core-owned bridge：
      - `river_dialog_wake_admission`
    - 新 bridge 直接拥有：
      - block-reason 检查
      - pending/retry worker
      - inline fallback
    - `session_coordinator` 已不再持有：
      - wake admission task
      - sema/mutex
      - pending/deferred/confidence/text
    - 这一步继续把 coordinator 压回：
      - wake handoff gate
      - ASR 文本 fanout
      的薄壳
    - 该步之后，coordinator 中的 wake admission worker 已完成下沉，后续主焦点
      已切回 downlink/playback runtime
  - previous runtime-ownership slice:
    - `dialog_cloud_port` 现在会在
      `begin_conversation_window(source)` 成功后，立即把 wake admission success
      同步写入 `dialog runtime`
    - source -> runtime reason 的归一已集中在 port 内：
      - `wakeword -> wakeword_detected`
    - `session_coordinator` 已删除 wake admission success 路径上的手工：
      - `river_dialog_runtime_note_wake_confirmed_with_cloud_state(...)`
    - 这让：
      - cloud begin_conversation_window
      - dialog runtime wake_confirmed note
      成为同一条 core-owned 原子 ingress
    - 该步之后，wake admission success side effects 已原子化，为继续下沉
      worker/runtime ownership 铺平了边界
  - previous runtime-ownership slice:
    - `dialog_cloud_port` 已补齐：
      - `begin_conversation_window(const char *source)`
    - `river_app` 已将 wake admission 入口绑定到 concrete adapter，但
      `session_coordinator` 本身不再直接调用
      `river_cloud_adapter_begin_conversation_window()`
    - wakeword worker / fallback 路径现在统一通过：
      - `river_dialog_cloud_begin_conversation_window("wakeword")`
    - 这让 core 层剩余的 wake admission cloud ingress 也开始与：
      - ASR audio open/push
      - batch submit
      - interrupt_tts
      共享同一条 stable port 边界
    - 该步之后，wake admission ingress 已统一走 stable port 边界，为后续继续
      收口成功 side effects 和 worker shell 做准备
  - previous runtime-ownership slice:
    - `dialog runtime` 现在继续吸收本地 `interrupt_tts` in-flight 真相：
      - snapshot 新增 `tts_interrupt_requested`
      - `allows_barge_in_interrupt()` 现同时检查：
        - `tts_stop_pending`
        - `tts_interrupt_requested`
    - `dialog_cloud_port` 在 interrupt 成功后会立刻把本地 interrupt 请求锁存进
      `dialog runtime`，减少 cloud stop-pending 回流前的重复中断窗口
    - `session_coordinator` 已删除：
      - `barge_in_interrupt_requested`
      - `state_lock`
    - ASR 文本触发的 barge-in interrupt 改为消费 `dialog_cloud_port` +
      `dialog runtime` policy，不再由 coordinator 自己维护本地 latch
  - previous runtime-ownership slice:
    - `dialog runtime` 现在继续直接吸收 XiaoZhi round/window typed truth：
      - `listen_stop_pending`
      - `local_close_pending`
      - `conversation_window_remaining_ms`
      - `local_close_remaining_ms`
    - cloud round 仍处于：
      - listening
      - stream active
      - listen-stop pending
      - local-close pending
      - input committed/active
      之一时，`dialog runtime` 会继续维持 `asr_session_active`
    - wakeword admission block reason 已开始优先输出：
      - `conversation_window_active`
      - `cloud_local_close_pending`
      - `cloud_listen_stop_pending`
    - `dialog runtime` status dump 也直接打印 stop-pending / local-close /
      round/window remaining truth，便于后续继续收口 coordinator bridge
  - previous runtime-ownership slice:
    - XiaoZhi round runtime 已继续接管 round 启动入口：
      - `open_session_and_listen()`
      - `start_followup_round()`
      - `maybe_start_followup_round()`
      - `begin_conversation_window()`
    - `wake admission` / `follow-up reopen` / `open-listen` 的 round 启停路径
      不再在 `xiaozhi_session.c` 内联维护
    - 下一步应继续把 round/window typed truth 更直接投影到
      `dialog runtime`，减少 core 再从 cloud snapshot 做二次解释
  - previous runtime-ownership slice:
    - XiaoZhi round runtime 继续接管：
      - open-and-listen session policy
      - post-commit wait / local-close defer
      - local round close / timeout
      - transport reset / window timeout
      - ASR session started / closed emit
    - `xiaozhi_session.c` 进一步只保留：
      - transport control
      - semantic/turn state
      - uplink/capture 主流程
    - 下一步应继续把：
      - wake admission
      - follow-up reopen
      - open/listen round 启停写路径
      收成更少的 round runtime / dialog runtime typed 入口
  - previous runtime-ownership slice:
    - `listening/window/listen_stop/local_close` 这组 XiaoZhi round 辅助状态
      已从 `river_cloud_xiaozhi_session.c` 拆到新的
      `river_cloud_xiaozhi_round_runtime.c`
    - `xiaozhi_session.c` 继续收缩为：
      - transport event reducer
      - semantic/turn state
      - uplink 主流程
    - 这为后续把 round/window 真相继续接到 dialog runtime 铺平边界
  - previous runtime-ownership slice:
    - `river_dialog_cloud_conversation_window_active()` 已优先读取
      `dialog runtime` snapshot
    - voice/KWS 侧 follow-up window 判定开始由 dialog runtime 对外提供
    - cloud adapter callback 退为 runtime 不可用时的 fallback
  - older runtime-ownership slice:
    - session 已不再直读：
      - `xiaozhi_playback_rebuffer_pending`
      - `xiaozhi_playback_active`
    - transport reset / session start / playback start 这些路径对
      `no_ref reopen` 的 reset 也统一走 playback runtime
    - `xiaozhi_session.c` 进一步只消费 typed playback helper
  - older runtime-ownership slice:
    - `endpoint_soft_close` 状态机已并入 playback runtime
    - session 现在只通过 typed helper 读取：
      - pending
      - remaining_ms
      - reason
    - session_close / transport_reset 这类路径也不再手工清空这组字段
  - older runtime-ownership slice:
    - `no-ref reopen/open_hold` gating now sits with playback runtime:
      - `open_hold_frames_required()`
      - `playback_followup_reopen_ready()`
    - `maybe_start_followup_round()` no longer carries the reopen guard/rearm
      state machine inline
    - this removes another cross-file split-brain between playback state writes
      and session-side reinterpretation
  - older runtime-ownership slice:
    - playback runtime now also owns:
      - `duplex_soft_endpoint_enabled()`
      - `duplex_speaking_uplink_continuation_active()`
      - local `output_speaking_active()`
    - this keeps speaking-output / soft-endpoint continuation semantics on the
      downlink runtime side instead of in `xiaozhi_session.c`
  - older runtime-ownership slice:
    - playback-side duplex admission helpers now live in
      `river_cloud_xiaozhi_playback_runtime.c`:
      - `playback_allows_vad_open()`
      - `capture_held_by_playback()`
      - `apply_tts_start_round_policy()`
    - `xiaozhi_session.c` no longer owns those playback/downlink rules
    - this keeps AEC/VAD admission and `tts_start` round-close policy attached
      to the downlink runtime truth owner
  - older runtime-ownership slice:
    - session_coordinator no longer reads dialog-runtime snapshots directly for:
      - wakeword admission
      - barge-in interrupt
    - those checks are now exposed as typed dialog-runtime policy helpers,
      keeping the admission rules inside the truth source itself
    - this removes another class of coordinator-side rule reconstruction
  - older runtime-ownership slice:
    - cloud ASR lifecycle no longer needs session_coordinator as a truth bridge
    - app now fans out ASR results so:
      - dialog runtime absorbs lifecycle truth directly
      - session_coordinator keeps only text/barge-in/diag side effects
    - this removes another coordinator-owned cloud->dialog bridge and keeps the
      lifecycle truth closer to dialog runtime itself
  - older runtime-ownership slice:
    - XiaoZhi playback runtime now directly fills playback-owned fields in
      `river_cloud_runtime_snapshot_t`
    - `river_cloud_xiaozhi_session.c` no longer hand-assembles playback phase /
      rebuffer / terminal / start-gate fields inline
    - this keeps playback snapshot projection ownership aligned with:
      - playback status dump ownership
      - playback phase/rebuffer/start-gate runtime truth ownership
  - earlier runtime-ownership slice:
    - dialog runtime boot / wake / ASR cloud-backed ingress now shares one
      internal typed reducer instead of five handwritten
      capture-mutate-publish paths
    - new internal reducer helpers now own:
      - local fact application
      - cloud snapshot absorption
      - publish reason selection
    - this further reduces truth-source drift inside dialog runtime itself and
      keeps cloud-backed ingress aligned with the long-term reducer architecture
  - earlier runtime-ownership slice:
    - XiaoZhi playback start-gate snapshot is now exported through:
      - `river_cloud_runtime_snapshot_t`
      - `river_dialog_runtime_snapshot_t`
    - session snapshot fill no longer needs to reconstruct that gate; it simply
      projects the runtime-owned snapshot fields upward:
      - `playback_start_policy`
      - `playback_start_frames`
      - `playback_prefetch_frames`
      - `playback_start_cautious_history`
    - dialog runtime status now prints the active gate truth directly, so board
      diagnostics can correlate:
      - playback phase
      - rebuffer cause
      - current start/prefetch gate
      from one runtime chain
  - prior runtime-ownership slice:
    - XiaoZhi playback start gate is now stored as a runtime-owned snapshot
      instead of being re-derived independently at each read site
    - runtime now splits start-gate ownership into:
      - `build_start_gate`
      - `refresh_start_gate`
      - `current_start_gate`
    - snapshot refresh is now wired into gate-affecting state transitions:
      - meta clear
      - downlink/playback reset
      - rebuffer enter/finish
      - clean-segment streak clear
      - playback meta prefetch update
      - downlink frame-duration update
    - this keeps worker/start/rebuffer diagnostics on the same gate truth and
      removes one more class of “同一时刻不同调用点各自现算出不同门限”的 drift
  - prior runtime-ownership slice:
    - XiaoZhi downlink start gating now comes from one explicit typed prefetch
      policy helper instead of being split across scattered threshold
      heuristics:
      - `baseline`
      - `rebuffer_fast`
      - `segment_prefetch`
      - `starved_prefetch`
    - runtime now computes and reuses one typed start-gate result:
      - `start_frames`
      - `prefetch_frames`
      - `cautious_history`
    - `audio.out.meta` segment cadence can now raise the start threshold even
      outside the active `UPSTREAM_STARVED` path, which directly targets the
      current long-segment / slow-supply / segment-boundary playback churn
    - board diagnostics now print the same typed gate on:
      - status
      - prefetch
      - upstream-gap rebuffer
      - playback start
      - rebuffer requested
  - earlier runtime-ownership slice:
    - XiaoZhi playback segment truth now also records whether a segment has
      already gone through local rebuffer:
      - `rebuffered`
    - active rebuffer history `streak` is no longer cleared by any segment that
      merely finishes after recover
    - only a fully-heard clean segment clears that `streak`, while recovered
      segments now explicitly preserve it with:
      - `reason=segment_recovered`
    - this keeps the start-threshold history aligned with actual uninterrupted
      playback stability instead of “recover 后勉强播完也算稳定”
  - earlier runtime-ownership slice:
    - XiaoZhi playback runtime now separates:
      - cumulative rebuffer diagnostics
      - active restart-threshold history
      through:
      - `xiaozhi_playback_rebuffer_count`
      - `xiaozhi_playback_rebuffer_streak`
    - start-threshold inflation now keys off the active `streak` instead of the
      cumulative total, and a fully-heard segment clears that `streak`
    - playback diagnostics now log both:
      - `rebuffer_total`
      - `streak`
    - playback-service public control wrappers for:
      - `stop`
      - `interrupt`
      - `flush`
      - `recover`
      now propagate the real underlying control result to callers
    - this removes one more false-success path in the downlink recovery model
      and prevents old rebuffer history from permanently biasing later
      playback starts within the same response
  - previous runtime-ownership slice:
    - playback service now exposes explicit recover semantics:
      - `recover_stream_ex`
      - `recover_stream`
    - XiaoZhi playback runtime now routes both:
      - `upstream_starved`
      - residual `write_failed`
      rebuffer recovery through `recover`, instead of mixing `stop` and `flush`
    - this reduces one more source of stop/start churn on recoverable playback
      gaps
  - previous runtime-ownership slice:
    - dialog runtime now translates local playback-service callbacks into typed
      local truth:
      - `playback_local_active`
      - `playback_local_recovering`
    - aggregate playback derivation now consumes:
      - cloud playback phase
      - local typed playback truth
      - lane / rebuffer truth
      instead of branching directly on raw `RIVER_PLAYBACK_*` enums in the
      reducer path
    - `dialog_runtime_dump_status()` now exposes both aggregate playback truth
      and local playback truth for board-side diagnosis
  - earlier runtime-ownership slice:
    - dialog runtime local playback ingress now consumes only app-registered
      dialog stream names instead of all `RIVER_PLAYBACK_PRIO_TTS` streams
    - `river_app` currently registers:
      - `xiaozhi_tts`
      - `iflytek_tts`
    - dialog runtime now latches the exact owned stream name so cleared-config
      terminal callbacks stay scoped to the same stream until `IDLE`
  - prior runtime-ownership slice:
    - session runtime now also owns the XiaoZhi I/O-loop housekeeping reducers:
      - `io_tick` turn/window maintenance
      - post-poll accepted-turn finalize glue
      - post-uplink playback/endpoint/local-close housekeeping
    - adapter `river_cloud_xiaozhi_io_task(...)` now keeps only poll/uplink
      scheduling order instead of mutating those session/playback truths inline
  - latest landed runtime-ownership slice:
    - session runtime now owns the XiaoZhi `OPEN_AND_LISTEN` transport helper:
      - session open
      - session-id sync from transport cache
      - `listen_start` gating against runtime listening truth
    - adapter `river_cloud_xiaozhi_control_execute(...)` now forwards that
      control op instead of directly mutating those transport/session facts
  - newest landed runtime-ownership slice:
    - session runtime now also owns the remaining session-side transport
      control reducer for:
      - `listen_stop`
      - `abort`
      - `close_session`
    - adapter `river_cloud_xiaozhi_control_execute(...)` now keeps fewer
      transport/session branches and continues shrinking toward a dispatch-only
      shell
  - latest landed runtime-ownership slice:
    - playback runtime now owns the `PLAYBACK_*` ACK transport reducer for:
      - `started`
      - `mark`
      - `cleared`
      - `completed`
    - adapter `river_cloud_xiaozhi_control_execute(...)` no longer directly
      sends playback ACKs and continues shrinking toward a pure control-dispatch
      shell
  - newest landed runtime-ownership slice:
    - session runtime now owns the full XiaoZhi control-op dispatch shell,
      routing:
      - session-side transport control
      - playback ACK transport control
    - adapter `river_cloud_xiaozhi_control_execute(...)` is now reduced to a
      one-line runtime call, matching the earlier event-dispatch shrink pattern
    - dialog runtime ingress now only treats `RIVER_PLAYBACK_ERROR` as fatal
      `error_recovering`, while recoverable write churn stays on the
      `playback_recovering` path
    - this gives the runtime truth source a real semantic split between
      recoverable rebuffer/restart churn and hard local playback faults
  - newest landed runtime-ownership slice:
    - session runtime now also owns the XiaoZhi control-request queue path:
      - sync request submission
      - async request submission
      - queue drain and completion signaling
      - session-side request wrapper entrypoints
    - adapter `river_cloud_xiaozhi_io_task(...)` now only invokes the
      runtime-owned queue drain helper instead of directly maintaining queue
      read/write/completion semantics
  - newest landed runtime-ownership slice:
    - session runtime now also owns the XiaoZhi uplink I/O service path:
      - retry-preserved frame drain
      - send-ready gating
      - busy backoff / backpressure logging
      - uplink timestamp advance
      - round packet-sent accounting
    - adapter `river_cloud_xiaozhi_io_task(...)` now only schedules the
      runtime uplink helper, and adapter uplink sending is reduced to the thin
      transport shell:
      - `river_cloud_xiaozhi_send_uplink_transport(...)`
  - newest landed runtime-ownership slice:
    - playback runtime now also owns the remaining adapter-facing playback
      backend lifecycle glue for:
      - backend refresh / downlink worker bootstrap
      - playback-state reset on backend refresh
      - bridge-close decoder tail teardown
    - adapter XiaoZhi init / config-refresh path now only calls:
      - `river_cloud_xiaozhi_apply_playback_backend_refresh_policy(...)`
  - newest landed runtime-ownership slice:
    - playback runtime now also owns the grouped terminal-close playback
      reducer for:
      - `transport_closed`
      - `network_lost`
      - `bridge_close`
    - session runtime terminal branches no longer inline playback abort gating,
      and adapter `river_cloud_asr_audio_close()` no longer appends a separate
      playback bridge-close tail after calling the session runtime policy
  - newest landed runtime-ownership slice:
    - playback runtime now also owns the session-entry playback cleanup
      reducers for:
      - transport reset
      - session start
    - `river_cloud_xiaozhi_session.c` no longer directly calls:
      - `river_cloud_xiaozhi_reset_downlink_state()`
      - `river_cloud_xiaozhi_clear_playback_meta_state()`
      from its reset/open branches
    - grouped runtime-owned helpers now normalize those entry points:
      - `river_cloud_xiaozhi_apply_transport_reset_playback_policy()`
      - `river_cloud_xiaozhi_apply_session_start_playback_policy()`
  - newest landed runtime-ownership slice:
    - playback runtime now also owns the capture-entry playback policy for:
      - pending-stop observation on capture ingress
      - duplex-held capture gate
      - fallback logging and held-capture pre-roll reset
      - capture-exit pending-stop observation
    - adapter `river_cloud_xiaozhi_stream_push_frame(...)` now only calls:
      - `river_cloud_xiaozhi_apply_capture_entry_playback_policy()`
      - `river_cloud_xiaozhi_apply_capture_exit_playback_policy()`
  - newest landed runtime-ownership slice:
    - session runtime now also owns the active-stream capture tail reducer for:
      - `speech_resumed` endpoint-soft-close cancel
      - silence/post-roll progression
      - duplex soft-endpoint vs local stream-finish dispatch
    - adapter `river_cloud_xiaozhi_stream_push_frame(...)` no longer directly
      mutates `silence_frames` or branches on
      `duplex_soft_endpoint_enabled()` after PCM push
  - newest landed runtime-ownership slice:
    - session runtime now also owns the bridge-close capture reducer for:
      - active-stream finish on bridge close
      - bridge-close terminal-policy sequencing
    - adapter `river_cloud_asr_audio_close()` now only calls:
      - `river_cloud_xiaozhi_apply_bridge_close_capture_policy()`
      before its encoder/generic bridge teardown
  - newest landed runtime-ownership slice:
    - session runtime now also owns the bridge-open capture/uplink reducer for:
      - uplink audio format validation
      - XiaoZhi pre-roll cap normalization
      - uplink ring lifecycle normalization
      - uplink timestamp / retry / busy-metric reset
      - bridge-open listen-gate logging
    - adapter `river_cloud_asr_audio_open()` now only calls:
      - `river_cloud_xiaozhi_apply_bridge_open_capture_policy()`
      around its generic bridge allocation path
    - adapter `river_cloud_asr_audio_close()` no longer carries a leftover
      XiaoZhi-only `xiaozhi_open_speech_frames` reset
  - newest landed runtime-ownership slice:
    - session runtime now also owns the XiaoZhi stream-push capture reducer for:
      - inactive-stream followup-open branching
      - pre-roll replay into uplink on stream activation
      - active-stream feed bookkeeping
      - stream activation log / stats transition
    - adapter `river_cloud_xiaozhi_stream_push_frame(...)` now only wraps:
      - capture-entry playback policy
      - runtime-owned stream-push capture policy
      - capture-exit playback policy
    - generic bridge helpers needed by that reducer are now exported on the
      internal boundary instead of staying adapter-local:
      - `river_cloud_log_stream_open_deferred_once(...)`
      - `river_cloud_reset_stream_open_deferred_state()`
      - `river_cloud_pre_roll_store(...)`
  - newest landed runtime-ownership slice:
    - XiaoZhi playback runtime now owns an explicit typed playback-backend
      truth for local backend ownership:
      - `detached`
      - `owned_active`
      - `foreign_active`
      - `restart_pending`
    - downlink start/rebuffer/pending-stop/abort paths now consume that typed
      backend state instead of scattering raw checks across:
      - `xiaozhi_playback_active`
      - `river_playback_service_active()`
      - `RIVER_PLAYBACK_RESTART_PENDING`
    - this prevents XiaoZhi cleanup/recovery paths from treating a foreign
      active playback stream as XiaoZhi-owned, and gives logs a direct
      ownership signal for future board traces
  - newest landed runtime-ownership slice:
    - dialog runtime now only absorbs dialog-related local playback-service
      streams:
      - current rule: `RIVER_PLAYBACK_PRIO_TTS`
      - shared `audio_echo` / debug playback no longer enters dialog truth
    - dialog runtime also keeps the owned local playback latch through
      cleared-config terminal transitions until `RIVER_PLAYBACK_IDLE`, so the
      local playback ingress still closes cleanly after:
      - `RECOVERING`
      - `RESTART_PENDING`
      - `IDLE`
  - newest landed runtime-ownership slice:
    - XiaoZhi downlink runtime now records explicit last-supply timing for
      playback starvation truth:
      - `xiaozhi_downlink_last_supply_ms`
    - upstream-starvation rebuffering is now triggered from:
      - low-water queued frames
      - adaptive supply-gap timing
      instead of waiting only for `queued=0`
    - the downlink worker no longer clears starvation watch state on every
      non-zero queue/write loop, which lets runtime keep a continuous starvation
      observation window before `write_failed`
  - newest landed runtime-ownership slice:
    - XiaoZhi playback runtime now treats low-water supply-gap `write_failed`
      events as `upstream_starved` recovery instead of always forcing local
      `flush/restart`
    - the write-failure branch now explicitly selects:
      - `recovery=stop`
      - `recovery=flush`
      and logs the chosen path together with:
      - `queued`
      - `low`
      - `supply_gap_ms`
  - previous landed runtime-ownership slice:
    - dialog runtime now directly owns the local playback-service listener
      ingress:
      - `river_dialog_runtime_on_playback_state(...)`
    - `river_app` now registers that callback straight into:
      - `river_playback_service_register_listener(...)`
    - `session_coordinator` no longer owns:
      - `river_session_coordinator_on_playback_state(...)`
    - this removes another coordinator-side truth bridge so local playback
      facts now enter dialog runtime without an intermediate owner
  - previous landed runtime-ownership slice:
    - dialog runtime now exports an atomic wake-admission fusion API:
      - `river_dialog_runtime_note_wake_confirmed_with_cloud_state(...)`
    - `session_coordinator` wake admission success paths now call that single
      dialog-runtime entrypoint instead of explicitly sequencing:
      - `note_wake_confirmed(...)`
      - `sync_cloud_state("wakeword_detected")`
    - the explicit wakeword cloud-sync bridge has been removed from
      coordinator
  - previous landed runtime-ownership slice:
    - cloud adapter now exports provider capability:
      - `river_cloud_adapter_runtime_self_sync_active()`
    - XiaoZhi session runtime now self-publishes state sync after:
      - `asr_error`
      - `asr_session_started`
      - `asr_session_closed`
    - `session_coordinator` only keeps the ASR lifecycle cloud-snapshot pull as
      a fallback for providers that do not self-sync runtime truth
  - previous landed runtime-ownership slice:
    - dialog runtime now directly owns the cloud state-sync ingress callback:
      - `river_dialog_runtime_on_cloud_state_sync(...)`
    - `river_app` no longer bridges cloud-state sync just to forward it into
      dialog runtime
    - `session_coordinator` playback listener no longer re-pulls the cloud
      runtime snapshot on every local playback-service state change
    - the stale `river_session_coordinator_sync_interaction_state(...)` bridge
      has been removed
  - previous landed runtime-ownership slice:
    - XiaoZhi playback runtime now proactively triggers cloud state sync when:
      - `playback_phase` changes
      - or rebuffer cause changes without a phase transition
    - this starts removing another remaining indirection where outer
      `session_coordinator` playback-state callbacks had to pull a fresh cloud
      snapshot just to let dialog/core observe playback-runtime truth updates
  - previous landed runtime-ownership slice:
    - XiaoZhi downlink restart gating now differentiates:
      - `upstream_starved`
      - `write_failed`
      instead of forcing both recovery classes through the same prefetch-sized
      refill wait
    - runtime downlink diagnostics now also print the current `start`
      threshold, so the effective restart gate can be checked directly on the
      board
  - previous landed runtime-ownership slice:
    - XiaoZhi playback runtime now also owns an explicit `rebuffer_cause`
      truth:
      - `upstream_starved`
      - `write_failed`
    - cloud/dialog runtime snapshot path now exports:
      - `playback_rebuffer_cause`
    - playback phase / rebuffer / session-status diagnostics now print that
      cause directly, which removes another remaining layer of log-only
      inference from the downlink recovery model
  - previous landed runtime-ownership slice:
    - dialog runtime `playback_recovering` is now also phase-first:
      - if `playback_phase` is known, recovery truth comes from phase/rebuffer
      - playback-service `recovering/restart_pending` only remain as phase-miss
        fallback
    - this blocks another residual leak where bottom-layer restart states could
      override an already-stable runtime phase truth
  - previous landed runtime-ownership slice:
    - dialog runtime now suppresses playback-state publishes when:
      - `playback_phase` is already known
      - and the new playback-service state does not change effective playback
        or interaction facts
    - this reduces residual upper-layer churn during recovery windows after
      phase-first truth is already stable
  - previous landed runtime-ownership slice:
    - dialog runtime playback derivation now treats XiaoZhi playback phase as
      the first-class truth:
      - `playback_recovering` prefers `rebuffering`
      - `playback_active` trusts phase before service/bool compatibility
    - XiaoZhi cloud runtime snapshot also now exports:
      - `playback_active = playback_output_active`
      so upper layers no longer ingest the old raw active bit
  - previous landed runtime-ownership slice:
    - XiaoZhi playback runtime now owns an explicit `playback_phase` truth:
      - `idle`
      - `prefetching`
      - `playing`
      - `rebuffering`
      - `draining`
    - playback lane helpers, cloud snapshot export, and dialog runtime dump now
      consume that phase instead of re-guessing playback occupancy from
      scattered booleans and playback-service activity
  - previous landed runtime-ownership slice:
    - XiaoZhi downlink runtime now treats `rebuffer_pending` as a real write
      resume gate:
      - wait for adaptive queued-frame threshold
      - only clear rebuffer after the threshold is satisfied
    - this closes a gap where write-failure recovery could flush the playback
      service and then immediately retry the same frame without enough refill
  - previous landed runtime-ownership slice:
    - generic ASR bridge runtime now also owns the last generic realtime
      open reducer:
      - `river_cloud_business_time_ready(...)`
      - `river_cloud_stream_open_and_flush(...)`
    - adapter no longer defines any generic realtime capture reducer body;
      it now only orchestrates:
      - validation
      - runtime helper calls
      - backend dispatch
  - previous landed runtime-ownership slice:
    - generic ASR bridge runtime now also owns the shared realtime reducer
      helpers for:
      - pre-roll reset
      - pre-roll store
      - active-stream finish
    - adapter now only keeps:
      - generic `stream_open_and_flush()` reducer
      - high-level bridge orchestration
  - previous landed runtime-ownership slice:
    - generic ASR bridge runtime now owns the base capture bridge state
      allocation/reset for:
      - `audio_desc`
      - `frame_bytes`
      - `pre_roll_buffer`
      - `pre_roll_capacity_frames`
      - `post_roll_frames`
      - `silence_frames`
    - adapter `river_cloud_asr_audio_open()/close()` now delegates to:
      - `river_cloud_prepare_audio_bridge_state(...)`
      - `river_cloud_reset_audio_bridge_state()`
  - previous landed runtime-ownership slice:
    - XiaoZhi capture dispatch no longer uses an adapter-local stream-push
      shim:
      - removed `river_cloud_xiaozhi_stream_push_frame(...)`
      - `river_cloud_asr_stream_push_frame(...)` now directly calls
        `river_cloud_xiaozhi_apply_capture_stream_policy(...)`
    - adapter is reduced further to:
      - shared bridge argument validation
      - streaming support guard
      - backend branch to runtime
  - previous landed runtime-ownership slice:
    - session runtime now also owns the last playback wrapper around the
      XiaoZhi capture push path:
      - capture-entry playback gate
      - capture-exit playback tail
    - adapter `river_cloud_xiaozhi_stream_push_frame(...)` is now reduced to:
      - input validation
      - `river_cloud_xiaozhi_apply_capture_stream_policy(...)`
  - eighth landed slice on that plan:
    - terminal `completed` is now gated by `last_segment observed + fully
      heard`, not only by a transient local drain point
    - `tts_stop_pending` no longer resets playback runtime state early when the
      local player goes idle before the final tail has actually been observed
    - this narrows the false-completion window that previously let late
      segments arrive after the device had already decided the response was
      completed
  - ninth landed slice on that plan:
    - local-only clear no longer fabricates terminal `cleared` truth when the
      board never actually queued `audio.out.cleared`
    - terminal-tail waiting is now exported as runtime-owned state all the way
      through cloud snapshot -> dialog runtime snapshot
    - runtime dumps can now distinguish ordinary stop-pending playback from
      “waiting for final tail/meta before terminal completion”
  - tenth landed slice on that plan:
    - playback runtime now separates:
      - local terminal outcome
      - protocol terminal ACK truth
    - local-only terminal outcomes now explicitly surface as:
      - `local_completed`
      - `local_cleared`
    - cloud snapshot and dialog runtime snapshot now both export:
      - `playback_terminal_state`
      - `playback_terminal_reason`
    - adapter/dialog diagnostics now show:
      - terminal state
      - terminal ACK
      independently
  - eleventh landed slice on that plan:
    - adapter terminal-close branches now use one playback-runtime
      cause reducer instead of locally assembling:
      - `clear_reason`
      - `stream_reason`
      - `interrupt_stream`
    - typed runtime abort causes now cover:
      - `interrupt`
      - `transport_closed`
      - `network_lost`
      - `bridge_close`
    - playback runtime now emits one normalized abort fact with:
      - cause
      - clear reason
      - stream reason
      - interrupt mode
      - entry playback/work state
  - twelfth landed slice on that plan:
    - the remaining local playback data-plane fatal path
      `frame_oversize` now also routes through the same playback-runtime typed
      abort reducer
    - downlink worker no longer hand-assembles:
      - finalize_cleared
      - reset_downlink
      - stop_stream
      - reset_playback
      for that fatal path
    - this leaves the playback runtime with one common terminal-close entry for
      both:
      - transport-side aborts
      - local fatal downlink faults
  - thirteenth landed slice on that plan:
    - `dialog runtime` no longer treats late `output_lane=speaking` as
      sufficient truth after local playback has already entered a terminal
      outcome
    - local terminal close and terminal-tail wait are now consumed directly in
      the interaction-state derivation path
    - playback-active truth is now re-derived through the same terminal-aware
      reducer on both:
      - cloud snapshot sync
      - playback-service ingress
  - fourteenth landed slice on that plan:
    - `session coordinator` no longer bypasses `dialog runtime` by checking
      playback-service local activity directly during barge-in admission
    - barge-in text confirmation now consumes one runtime-owned predicate:
      - playback is locally interruptible
      - playback has not already entered a terminal state
  - fifteenth landed slice on that plan:
    - XiaoZhi downlink write-fail recovery now tries same-track
      `flush/restart` first instead of immediately tearing down the whole
      playback stream
    - playback service can now recover from `RIVER_PLAYBACK_RECOVERING` back
      to `RIVER_PLAYBACK_RUNNING`
    - stop/start is retained only as the fallback path when same-track recover
      fails
  - latest landed slice on that plan:
    - session runtime now exports adapter-facing getters for:
      - `listening`
      - `conversation_window_active`
      - `conversation_window_remaining_ms`
    - adapter transport-active, config busy guard, status dump, and public
      runtime snapshot/window getters now consume those exported session facts
      instead of directly reading `xiaozhi_listening/xiaozhi_window_active`
    - `open_and_listen` success-time `listening=true` normalization is now also
      applied by the exported request-success session policy, so adapter
      transport execution no longer owns that state write
    - adapter dump/uplink diagnostics now also consume exported runtime facts
      for:
      - `local_close_pending`
      - `local_close_remaining_ms`
      - `listen_stop_pending`
      instead of directly reading those session fields
    - preview observation helper ownership is now also session-runtime-owned:
      - `river_cloud_xiaozhi_note_preview_observation(...)`
      - `river_cloud_xiaozhi_copy_optional_text(...)`
      so adapter no longer implements preview-state mutation helpers locally
  - sixteenth landed slice on that plan:
    - local round-close truth has started moving out of
      `river_cloud_adapter.c` into `river_cloud_xiaozhi_session.c`
    - `round_finish` pacing truth now lives with other XiaoZhi session
      lifecycle ownership
    - adapter `local_close_resolved / server_response_started` paths now upload
      only typed round-close causes into session runtime
  - seventeenth landed slice on that plan:
    - `endpoint soft close / local close defer` helper ownership has also
      started moving out of the adapter and into XiaoZhi session runtime
    - adapter now consumes exported session-runtime helpers for:
      - endpoint soft-close arm/cancel
      - local-close defer arm/check
      - duplex-speaking uplink continuation predicate
  - eighteenth landed slice on that plan:
    - `active stream finish` state commit and `endpoint soft-close timeout`
      decision now also live in XiaoZhi session runtime
    - adapter keeps only the transport tail that flushes the last accumulator
      frame and finalizes `listen_stop`
    - local-close/server-response branches now forward typed causes directly
      instead of routing through adapter-local wrappers
  - nineteenth landed slice on that plan:
    - downlink / playback local buffering was rebuilt around a larger jitter
      budget:
      - downlink ring `96`
      - start threshold `16`
      - rebuffer threshold `28`
      - playback target/fallback buffer `12/8`
    - playback runtime now derives adaptive prefetch truth from
      `audio.out.meta` arrival cadence:
      - `playback_last_meta_gap_ms`
      - `playback_prefetch_target_ms`
    - starvation-triggered rebuffer now waits against that adaptive target
      instead of a small fixed timeout, and adapter diagnostics now print:
      - `target_ms`
      - `meta_gap_ms`
      - `rebuffer_count`
  - twentieth landed slice on that plan:
    - recoverable playback churn is now exported as runtime truth end-to-end:
      - cloud snapshot exports `playback_rebuffer_pending`
      - dialog runtime stores `playback_cloud_active`
      - dialog runtime derives `playback_recovering`
    - `dialog runtime` playback-active truth now stays engaged across
      same-track recover / rebuffer churn instead of inferring “stopped” from
      a transient local active gap
    - `river_playback_service_state_active()` now also treats
      `RIVER_PLAYBACK_RECOVERING` as active, so AEC/VAD and interaction truth
      no longer flap on recover-first restart
  - twenty-first landed slice on that plan:
    - recover-first restart failure no longer escalates into
      `RIVER_PLAYBACK_ERROR` before the runtime can try a normal fresh start
  - twenty-second landed slice on that plan:
    - AEC/duplex evaluation now treats `RIVER_PLAYBACK_RESTART_PENDING` as its
      own runtime reason instead of collapsing it into generic
      `ref_missing/ref_idle`
    - the voice runtime now exports:
      - `RIVER_VOICE_AEC_GATE_BLOCKED_PLAYBACK_RESTART_PENDING`
      - `RIVER_VOICE_DUPLEX_READY_PLAYBACK_RESTART_PENDING`
    - XiaoZhi fallback logs now surface:
      - `half_duplex_restart_pending`
  - twenty-third landed slice on that plan:
    - XiaoZhi `tts_start` keep-open / round-close policy now lives in session
      runtime instead of the adapter
    - adapter TTS-start handling now only finalizes pending text, calls the
      exported runtime helper, and keeps the playback-stop cancel tail glue
  - twenty-fourth landed slice on that plan:
    - the now-redundant `keep_local_round_on_tts_start` predicate was removed
      after `tts_start` policy had already been centralized in session runtime
    - the session-runtime TTS-start surface is now a single exported policy
      helper
  - latest landed slice on that plan:
    - XiaoZhi `SERVER_HELLO` transport observation has also moved out of
      `river_cloud_adapter.c` into session runtime
    - adapter `RIVER_XIAOZHI_EVENT_SERVER_HELLO` handling now only dispatches
      to:
      - `river_cloud_xiaozhi_note_server_hello_observation(...)`
    - session runtime now owns:
      - server sample-rate truth
      - server frame-duration truth
      - session-id sync from transport cache
  - twenty-fifth landed slice on that plan:
    - XiaoZhi `tts_stop` round-close policy now lives in session runtime
    - adapter TTS-stop handling now only calls the exported runtime helper
  - twenty-sixth landed slice on that plan:
    - XiaoZhi local-close `reopen_overlap` policy now lives in session runtime
    - adapter reopen path now only calls the exported overlap helper
  - twenty-seventh landed slice on that plan:
    - XiaoZhi `LLM` local-close policy now lives in session runtime
    - adapter `RIVER_XIAOZHI_EVENT_LLM` branch now only calls the exported
      runtime helper
  - twenty-eighth landed slice on that plan:
    - XiaoZhi `post_stop_result` local-close policy now lives in session runtime
    - adapter listen-stop completion path now only calls the exported runtime
      helper
  - twenty-ninth landed slice on that plan:
    - XiaoZhi `transport_closed` terminal cleanup policy now lives in session runtime
    - adapter `RIVER_XIAOZHI_EVENT_SESSION_CLOSED` branch now only calls the
      exported runtime helper
  - thirtieth landed slice on that plan:
    - XiaoZhi `network_lost` terminal cleanup policy now lives in session runtime
    - adapter `river_cloud_adapter_notify_network_lost()` path now only calls
      the exported runtime helper for terminal cleanup
    - fixed-dsb AECM summary now counts:
      - `restart_pending`
      separately from generic playback-disabled/reference-idle churn
  - thirty-first landed slice on that plan:
    - XiaoZhi `bridge_close` terminal cleanup policy now lives in session runtime
    - adapter `river_cloud_asr_audio_close()` path now only calls the exported
      runtime helper for terminal cleanup, while keeping close-session and
      codec-close tail actions in the adapter
  - thirty-second landed slice on that plan:
    - XiaoZhi `listen_stop` completion round-close policy now lives in session runtime
    - adapter `river_cloud_xiaozhi_finalize_listen_stop_if_ready()` now only
      keeps uplink-drained / `listen_stop` transport gating and calls the
      exported runtime helper for the completion round policy
  - thirty-third landed slice on that plan:
    - XiaoZhi ASR round lifecycle now lives in session runtime
    - adapter no longer defines local:
      - `round_begin`
      - `round_note_packet_sent`
      implementations
  - thirty-fourth landed slice on that plan:
    - XiaoZhi TTS interrupt policy now lives in session runtime
    - adapter `river_cloud_adapter_interrupt_tts_with_reason(...)` now only
      keeps provider dispatch and calls exported runtime helper
  - thirty-fifth landed slice on that plan:
    - XiaoZhi follow-up reopen round-start policy now lives in session runtime
    - adapter reopen-open path now only computes pre-roll count and delegates
      follow-up round-start policy to runtime
  - thirty-sixth landed slice on that plan:
    - XiaoZhi `network_lost` / `bridge_close` terminal-policy helpers now own
      close-session tail actions
    - adapter terminal paths no longer append local close-session requests
  - thirty-seventh landed slice on that plan:
    - XiaoZhi idle reopen gate now lives in session runtime
    - adapter capture path no longer directly decides:
      - wakeword-window reopen eligibility
      - no-ref reopen rearm / guard gating
      - open-hold frame accumulation thresholds
  - thirty-eighth landed slice on that plan:
    - XiaoZhi listen-stop completion policy now lives in session runtime
    - adapter uplink service path now only samples drain state and delegates:
      - queued uplink frame count
      - pending accum buffer bytes
  - thirty-ninth landed slice on that plan:
    - XiaoZhi listen-stop reopen busy gate now lives in session runtime
    - adapter reopen-open path no longer directly reads
      `xiaozhi_listen_stop_pending` before delegating to runtime
  - fortieth landed slice on that plan:
    - XiaoZhi open-and-listen success policy now clears stop intent in session
      runtime
    - adapter `CTRL_OPEN_AND_LISTEN` path no longer directly clears
      `xiaozhi_listen_stop_pending`
  - forty-first landed slice on that plan:
    - XiaoZhi uplink keepalive gate now lives in session runtime
    - adapter `uplink_active()` no longer directly folds
      `xiaozhi_listen_stop_pending`
  - forty-second landed slice on that plan:
    - XiaoZhi uplink send-ready gate now lives in session runtime
    - adapter control/uplink 路径不再重复拼接：
      - `river_xiaozhi_session_open() && g_river_cloud.xiaozhi_listening`
  - twenty-third landed slice on that plan:
    - cloud/runtime now export a first-class playback-lane engagement fact:
      - `playback_lane_engaged`
    - XiaoZhi playback runtime owns the reducer for that fact by folding:
      - playback output active
      - rebuffer pending
      - playback-service active states
    - adapter transport/capture gating and dialog runtime playback derivation
      now consume that exported lane truth instead of reassembling engagement
      from raw local flags
  - twenty-fourth landed slice on that plan:
    - XiaoZhi `open_hold_frames_required` and `no_ref_reopen_ready` helper
      ownership has moved out of `river_cloud_adapter.c` into session runtime
    - the session helper now directly consumes `playback_lane_engaged`, so
      `no_ref` reopen no longer depends on adapter-local call ordering to avoid
      reopening during an occupied playback lane
    - adapter capture-open flow now only calls exported helpers for those two
      policy decisions
  - twenty-fifth landed slice on that plan:
    - `capture held during playback` has also started moving behind a
      session-runtime helper:
      - `river_cloud_xiaozhi_capture_held_by_playback(...)`
    - that helper now owns the combined reducer:
      - `playback_lane_engaged`
      - `duplex_fallback_reason`
    - adapter capture path now consumes the helper and no longer reconstructs
      that predicate locally
    - playback service now tears down the failed track and returns to
      restartable `IDLE` with a dedicated `recover fallback` path
    - XiaoZhi downlink recovery no longer forces an extra redundant local stop
      after that fallback, reducing fatal/error noise on the rebuffer path
  - twenty-second landed slice on that plan:
    - playback service now exposes one explicit intermediate state:
      - `RIVER_PLAYBACK_RESTART_PENDING`
    - this keeps the rest of the stack on the “recoverable playback still
      engaged” path after a broken track is torn down, without pretending the
      track is still running
    - XiaoZhi downlink worker now explicitly fresh-starts from:
      - `IDLE`
      - `RESTART_PENDING`
      so the new engaged state does not stall restart
  - aligned the device-side duplex roadmap to the 2026-04-16
    `/root/agent-server` protocol/architecture docs:
    - preview-aware input events
    - playback-truth ACKs
    - discovery + `session.start.capabilities` collaboration negotiation
  - re-prioritized the next device slice after reviewing the latest
    `/root/agent-server` playback-truth commits:
    - `2a2c9cf`
    - `dd10dff`
    - `d0d81ee`
    - `46aef68`
    - `74a9c6d`
  - conclusion:
    - `segment_mark_v1` now depends on the full
      `audio.out.started/mark/cleared/completed` truth chain, so device work had
      to land `C5` before returning to `5.168`
  - `5.168` is now landed:
    - XiaoZhi speaking-time duplex decisions are gated by runtime-truthful
      `duplex_ready`, not only static profile capability
    - runtime duplex evaluation now combines:
      - duplex experiment switch
      - active profile capability
      - reference service state / recent activity
      - AEC gate result
    - the next implementation slice is:
      - `5.169` speaking-time local endpoint softening
  - `5.169` is now landed:
    - duplex-ready speaking rounds now treat `input.endpoint` and short local
      silence as hint/defer signals instead of immediate hard local close
    - the device now exposes `endpoint_soft_close pending/reason/left_ms`
      runtime status for board validation
    - the next implementation slice is:
      - `5.170` speaking-time uplink continuation
  - `5.170` is now landed:
    - an expired `endpoint_soft_close` no longer resolves into local
      `audio.in.commit` while the duplex-ready output lane is still effectively
      speaking
    - speaking-time local silence can therefore keep `listening=yes` and the
      uplink round alive across pause gaps without repeated local stop/reopen
    - once output leaves the speaking lane, the pending close reuses the
      existing commit/stop pipeline
    - the next implementation slice is:
      - `5.171` duck-first interruption policy
  - `5.171` is now landed:
    - local speaking-time near-end speech no longer jumps straight from
      “qualifying VAD hit” to hard interrupt
    - the device now ducks first, releases on short/noisy speech, and only
      escalates to hard interrupt after sustained evidence
    - the next implementation slice is:
      - `5.172` board-profile duplex-ready acoustic baseline
  - `5.172` is now landed:
    - the branch build now selects a dedicated `duplex-ready experimental`
      board profile:
      - `fixed_dsb_webrtc_aecm`
      - `CONFIG_RIVER_XIAOZHI_FULL_DUPLEX_EXPERIMENT_EN=y`
    - native-capture-ref duplex gating no longer trusts playback state alone;
      it now consumes actual AECM ref telemetry:
      - `ref_activity`
      - `ref_peak`
      - `ref_ratio_q15`
      - freshness age
    - `vad_probe` now measures `ref_peak` from capture `ch3` when the active
      profile uses native reference, so board logs and local barge-in
      arbitration can observe the real far-end reference source
  - `5.173` is now landed:
    - the branch now separates:
      - duplex experiment compiled in
      - default-on policy enabled
      - per-session permission to advertise `half_duplex=false`
    - `session.start` default duplex advertisement now requires the full
      device + service matrix:
      - active profile supports playback reference
      - discovery advertises `voice_collaboration`
      - server endpoint is available and enabled
      - `preview_events` negotiation succeeds
      - `playback_ack.mode=segment_mark_v1` negotiation succeeds
    - speaking-time keep-open / capture-hold / status logs now share the same
      richer fallback reason family, including:
      - `half_duplex_default_policy_disabled`
      - `half_duplex_service_collaboration_unavailable`
      - `half_duplex_service_endpoint_unavailable`
      - `half_duplex_service_endpoint_disabled`
      - `half_duplex_service_preview_unavailable`
      - `half_duplex_service_playback_ack_unavailable`
    - the next focus is:
      - board regression of the 5.173 default-on matrix on the duplex-ready
        experimental profile
  - `5.173A` is now landed:
    - this slice is logging-only; no duplex behavior or negotiation policy was
      changed
    - the device now emits explicit collaboration snapshots at:
      - discovery refresh
      - `session.start`
      - discovery-refresh failure fallback
    - the device now warns on:
      - sparse `session.update` semantics
      - turn semantics arriving before accept
      - sparse / invalid preview payloads
      - preview events arriving before negotiation
      - playback-ack send failures with negotiated mode + last error
    - the next focus is:
      - use `5.173A` logs to validate the updated server-side collaboration
        chain once the service changes are available
  - `5.173B` is now landed:
    - this slice is also logging-only; no duplex policy or turn-state behavior
      was changed
    - XiaoZhi transport now latches and preserves accept-turn semantics across
      sparse `session.update` payloads within a round until the explicit
      session-update cache clear path runs for the next listen round
    - board logs now expose timing correlation across:
      - `input.speech.start`
      - `input.preview`
      - `input.endpoint`
      - `session.update.accept_reason`
      - `response.start`
      - `audio.out.meta`
    - `river xiaozhi status` now exports:
      - `timing_age_ms`
      - `timing_chain_ms`
    - the next focus is:
      - use `5.173A` + `5.173B` logs to validate the updated server-side
        collaboration chain once the service changes are available
  - `5.174` is now landed:
    - `no_ref` / half-duplex fallback playback no longer uses the same
      aggressive local barge-in thresholds as duplex-ready playback
    - local interruption in that path now requires stronger sustained evidence:
      - higher peak / ratio / margin gates
      - duck `3` hit frames
      - interrupt `12` hit frames
    - XiaoZhi downlink/playback buffering is now biased toward continuity:
      - ring `32`
      - start watermark `12`
      - playback buffer `6`
      - compact buffer `4`
    - the next focus is:
      - flash `5.174` to board and regress the original "未识别到有效语音 /
        只播片段" scenario against the current half-duplex service deployment
  - `5.175` is now landed:
    - local XiaoZhi playback gain is back to unity:
      - `5/2 -> 1/1`
    - strict `no_ref` barge-in no longer escalates to a hard interrupt:
      - the board now keeps ducking but logs
        `mode=no_ref_duck_only`
    - `write_failed` no longer clears playback state immediately:
      - the current frame is retained for retry
      - playback ACK timing pauses while rebuffering
      - resume now waits for a deeper `18`-frame watermark
    - the next focus is:
      - flash `5.175` to board and regress the original clipping /
        `write_failed` / short-playback scenario against the current
        half-duplex service deployment
- Primary active execution plan:
  - `doc/VOICE_RUNTIME_REARCHITECTURE_EXECUTION_PLAN_ZH.md`
- Latest runtime-owned adapter shrink slice:
  - adapter capture/I/O paths now delegate XiaoZhi uplink ingress and
    stream-finish closure to runtime:
    - `river_cloud_xiaozhi_trim_uplink_stale_frames(...)`
    - `river_cloud_xiaozhi_push_pcm(...)`
    - `river_cloud_xiaozhi_complete_active_stream_finish(...)`
  - adapter no longer owns XiaoZhi:
    - uplink stale-tail trimming
    - PCM accumulator framing
    - padded flush on active-stream finish
  - the next focus remains:
    - continue moving remaining XiaoZhi runtime truth/projection helpers out
      of `river_cloud_adapter.c`

## Current Runtime Focus

- Keep the board-side `wake -> VAD/KWS -> native realtime session` path usable.
- The current branch build now defaults to the dedicated duplex experiment
  profile while still preserving runtime fallback gates:
  - `fixed_dsb_webrtc_aecm`
  - native `2mic + ref(ch3)` capture
  - XiaoZhi full-duplex experiment advertisement on
- Replace the old XiaoZhi wire contract with direct `rtos-ws-v0` transport while
  keeping the existing upper cloud state machine temporarily stable.
- Current highest-priority runtime cleanup is now:
  - continue shrinking terminal ambiguity by aligning local-clear /
    data-plane terminal close / playback terminal truth around explicit
    runtime-owned facts
  - continue moving terminal-cause derivation behind the playback runtime so
    `dialog runtime` consumes exported truth instead of reconstructing terminal
    meaning from stop/active flags
  - keep shrinking `river_cloud_adapter.c` by separating provider lifecycle /
    policy from playback/session media engines
  - preserve the already-landed `dialog runtime` as the only
    interaction-state source while the playback rebuild moves forward
- Keep the landed collaboration baseline explicit and conservative:
  - accepted-turn is confirmed only from `session.update.accept_reason`
  - preview events remain observation-only
  - playback metadata remains playback-fact-only
  - stale `session.update` turn semantics are cleared before a new listen round
  - explicit fallback reasons are logged when the device stays on the current
    half-duplex/client-commit path
- Validate the collaboration baseline on board:
  - websocket subprotocol `agent-server.realtime.v0`
  - `ws://101.33.235.154:8080/v1/realtime/ws`
  - SDK handshake sends only one valid `Sec-WebSocket-Protocol`
  - SDK handshake strings remain NUL-terminated after copy into wsclient
  - SDK plain-`ws` connect path reports real `connect` errors instead of
    collapsing them into `Sending handshake failed`
  - discovery-backed `session.start.capabilities` negotiation
  - preview-aware input observation parsing and status exposure
  - full `segment_mark_v1` playback-truth ACK chain:
    - `audio.out.started`
    - `audio.out.mark`
    - `audio.out.cleared`
    - `audio.out.completed`
  - accepted-turn / fallback semantics alignment
  - `session.start` / `audio.in.commit` / `text.in`
  - PCM16 uplink and PCM16 downlink
- Drive current multi-step work from:
  - `doc/VOICE_RUNTIME_REARCHITECTURE_EXECUTION_PLAN_ZH.md`
- Keep the duplex execution plan as a supporting protocol/runtime baseline:
  - `doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md`
- Keep the XiaoZhi stability plan as a supporting baseline while duplex work
  continues:
  - `doc/XIAOZHI_SESSION_STABILITY_EXECUTION_PLAN_ZH.md`
- The next device-side duplex code slices are now explicitly staged as:
  - board regression and log validation of the landed `5.175` playback
    stabilization on the current half-duplex service path
  - after that, return to the landed `5.173` / `5.173A` / `5.173B`
    negotiation, sparse-semantics, and playback-ack diagnostics against the
    next server-side collaboration update
- Preserve the project flash profile:
  - `board/rtl8730e/profiles/RTL8730E_NOR.rdev`

## Start Here

- `AGENTS.md`
- `README.md`
- `build.md`
- `.codex/active_plans.md`
- `doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md`
- `doc/XIAOZHI_SESSION_STABILITY_EXECUTION_PLAN_ZH.md`
- the latest sections at the top of `.codex/changes.md` and `.codex/verification.md`
- `doc/README.md` for historical design and investigation documents

## Volatile-Context Rules

- Keep branch-specific or dated status snapshots out of `README.md` and
  `build.md`; those files should stay stable.
- Root `plan.md` is currently a historical `refactor`-branch snapshot, not the
  canonical active plan for today's branch.
- For larger work items, add or update an execution plan in `doc/` and
  register it in `.codex/active_plans.md`.
- After changing repo-level Codex harness files, run:
  - `python3 tools/diag/check_codex_harness.py`
