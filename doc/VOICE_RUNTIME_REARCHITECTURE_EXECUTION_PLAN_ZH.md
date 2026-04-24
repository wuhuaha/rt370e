# Voice Runtime Re-Architecture Execution Plan

Status: active
Last Updated: 2026-04-24
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

- `Step 5.489`
  - `river_cloud` 继续把 XiaoZhi playback tail / no-ref reopen 状态从散落的
    `g_river_cloud.xiaozhi_*` 裸字段收口到既有 playback runtime/gate truth
  - `tts_stop_deadline_ms` 归入：
    - `river_cloud_xiaozhi_playback_runtime_truth_t`
  - no-ref reopen guard/rearm/silence 状态归入：
    - `river_cloud_xiaozhi_playback_gate_truth_t`
  - `river_cloud_context_t` 不再保留散落的：
    - `xiaozhi_tts_stop_deadline_ms`
    - `xiaozhi_no_ref_reopen_guard_deadline_ms`
    - `xiaozhi_no_ref_reopen_silence_frames`
    - `xiaozhi_no_ref_reopen_rearm`
  - playback stop arm/cancel/poll、playback reset、no-ref reopen readiness 和
    runtime status dump 现在统一走 typed playback runtime/gate truth
  - 这一步继续把 `river_cloud` 从：
    - scattered playback tail and no-ref reopen fields
    推进到：
    - playback runtime truth + playback gate truth
  - 下一步继续聚焦：
    - downlink/playback 内部 query/view/export 边界，把直接读写进一步压到 owner 函数内
- `Step 5.488`
  - `river_cloud` 继续把 XiaoZhi session/window/local-close 状态从散落的
    `g_river_cloud.xiaozhi_*` 裸字段收口成显式 truth
  - 新增 session-window-owned truth：
    - `river_cloud_xiaozhi_session_window_truth_t`
  - `river_cloud_context_t` 不再保留散落的：
    - `xiaozhi_listening`
    - `xiaozhi_window_active`
    - `xiaozhi_listen_stop_pending`
    - `xiaozhi_local_close_pending`
    - `xiaozhi_window_deadline_ms`
    - `xiaozhi_local_close_deadline_ms`
  - listen-start / listen-stop completion、conversation window touch/close/abort、
    local-close defer/timeout、follow-up reopen gate 和 playback-side close/rebuffer
    guard 现在统一走 typed session window truth
  - 这一步继续把 `river_cloud` 从：
    - scattered dialog/session window flags and deadlines
    推进到：
    - explicit session window truth
  - 下一步继续聚焦：
    - no-ref reopen / TTS tail deadline 这些仍与 playback 卡顿、follow-up reopen
      稳定性直接相关的剩余运行态
- `Step 5.487`
  - `river_cloud` 继续把 XiaoZhi preview transcript / endpoint candidate 状态从散落的
    `g_river_cloud.xiaozhi_preview_*` 裸字段收口成显式 truth
  - 新增 preview-transcript-owned truth：
    - `river_cloud_xiaozhi_preview_transcript_truth_t`
  - `river_cloud_context_t` 不再保留散落的：
    - `xiaozhi_preview_speech_started`
    - `xiaozhi_preview_endpoint_candidate`
    - `xiaozhi_preview_final`
    - `xiaozhi_preview_audio_offset_ms`
    - `xiaozhi_preview_id`
    - `xiaozhi_preview_text`
    - `xiaozhi_preview_stable_prefix`
    - `xiaozhi_preview_source`
    - `xiaozhi_preview_endpoint_reason`
  - preview observation、preview clear、runtime dump 和 interrupt hint 现在统一走
    typed preview transcript truth
  - 这一步继续把 `river_cloud` 从：
    - scattered preview transcript / endpoint candidate bag
    推进到：
    - explicit preview transcript truth
  - 下一步继续聚焦：
    - remaining session/window/local-close/no-ref reopen 运行态，判断是否继续收口
      为 typed truth 或转入 query/export 边界整理
- `Step 5.486`
  - `river_cloud` 继续把 XiaoZhi pending transcript 状态从散落的
    `g_river_cloud.xiaozhi_pending_text*` 裸字段收口成显式 truth
  - 新增 pending-transcript-owned truth：
    - `river_cloud_xiaozhi_pending_transcript_truth_t`
  - `river_cloud_context_t` 不再保留散落的：
    - `xiaozhi_pending_text_valid`
    - `xiaozhi_pending_text_finalized`
    - `xiaozhi_pending_text`
  - STT observation、pending clear、finalize readiness、accepted-turn final emit、
    post-stop result policy、local-close defer 和 runtime dump 现在统一走 typed
    pending transcript truth
  - 这一步继续把 `river_cloud` 从：
    - scattered pending text validity/finalization/text bag
    推进到：
    - explicit pending transcript truth
  - 下一步继续聚焦：
    - preview transcript / endpoint candidate 这批服务端预览与端点观测状态
- `Step 5.485`
  - `river_cloud` 继续把 XiaoZhi endpoint soft-close 状态从散落的
    `g_river_cloud.xiaozhi_endpoint_soft_close_*` 裸字段收口成显式 truth
  - 新增 endpoint-soft-close-owned truth：
    - `river_cloud_xiaozhi_endpoint_soft_close_truth_t`
  - `river_cloud_context_t` 不再保留散落的：
    - `xiaozhi_endpoint_soft_close_pending`
    - `xiaozhi_endpoint_soft_close_deadline_ms`
    - `xiaozhi_endpoint_soft_close_reason`
  - endpoint soft-close 的 pending 查询、剩余时间、reason、clear、arm、
    timeout poll 现在统一走 typed endpoint soft-close truth
  - 这一步继续把 `river_cloud` 从：
    - scattered endpoint defer pending/deadline/reason bag
    推进到：
    - explicit endpoint soft-close truth
  - 下一步继续聚焦：
    - pending transcript / preview transcript 这批文本与端点观测状态，避免
      STT / preview / finalize 路径继续散读散写
- `Step 5.484`
  - `river_cloud` 继续把 XiaoZhi ASR round stats 从散落的
    `g_river_cloud.xiaozhi_asr_round_*` 裸字段收口成显式 truth
  - 新增 ASR-round-owned truth：
    - `river_cloud_xiaozhi_asr_round_truth_t`
  - `river_cloud_context_t` 不再保留散落的：
    - `xiaozhi_asr_round_id`
    - `xiaozhi_asr_round_active`
    - `xiaozhi_asr_round_started_ms`
    - `xiaozhi_asr_round_first_packet_ms`
    - `xiaozhi_asr_round_pre_roll_frames`
    - `xiaozhi_asr_round_packets_sent`
    - `xiaozhi_asr_round_partial_count`
    - `xiaozhi_asr_round_final_count`
    - `xiaozhi_asr_round_partial_seen`
    - `xiaozhi_asr_round_final_seen`
    - `xiaozhi_asr_round_busy_base`
    - `xiaozhi_asr_round_fail_base`
    - `xiaozhi_asr_round_stale_drop_base`
    - `xiaozhi_asr_round_ring_drop_base`
    - `xiaozhi_asr_round_close_reason`
  - ASR result emitted、round begin / packet-sent / finish、local-close defer、
    follow-up reopen 和 IO status 诊断现在统一走 typed ASR round truth
  - 这一步继续把 `river_cloud` 从：
    - scattered ASR round timing/counter/reason bag
    推进到：
    - explicit ASR round truth
  - 下一步继续聚焦：
    - pending / preview / endpoint soft-close 这批与 endpoint 延迟、响应慢和
      transcript 可观测性直接相关的状态
- `Step 5.483`
  - `river_cloud` 继续把 XiaoZhi control queue 状态从散落的
    `g_river_cloud.xiaozhi_control_*` 裸字段收口成显式 truth
  - 新增 control-queue-owned truth：
    - `river_cloud_xiaozhi_control_queue_truth_t`
  - `river_cloud_context_t` 不再保留散落的：
    - `xiaozhi_control_read_index`
    - `xiaozhi_control_write_index`
    - `xiaozhi_control_count`
    - `xiaozhi_control_high_watermark`
  - control queue 的入队、出队和 IO 诊断现在统一走 typed queue truth
  - 这一步继续把 `river_cloud` 从：
    - scattered control queue counters bag
    推进到：
    - explicit control queue truth
  - 下一步继续聚焦：
    - ASR round stats / preview text / endpoint soft-close 这些仍与响应慢、
      端点延迟和可观测性直接相关的 cloud-owned 状态
- `Step 5.482`
  - `river_cloud` 继续把 XiaoZhi io/uplink runtime 状态从散落的
    `g_river_cloud.xiaozhi_*` 裸字段收口成显式 truth
  - 新增 uplink-owned runtime truth：
    - `river_cloud_xiaozhi_uplink_runtime_truth_t`
  - `river_cloud_context_t` 不再保留散落的：
    - `xiaozhi_io_started`
    - `xiaozhi_open_speech_frames`
    - `xiaozhi_uplink_timestamp_ms`
    - `xiaozhi_uplink_ring_dropped`
    - `xiaozhi_uplink_busy_count`
    - `xiaozhi_uplink_fail_count`
    - `xiaozhi_uplink_stale_dropped`
    - `xiaozhi_uplink_busy_streak`
    - `xiaozhi_uplink_next_send_ms`
    - `xiaozhi_uplink_last_busy_log_ms`
    - `xiaozhi_uplink_accum_bytes`
    - `xiaozhi_uplink_retry_valid`
  - 这一步继续把 `river_cloud` 从：
    - scattered io/uplink backpressure bag
    推进到：
    - explicit uplink runtime truth
  - 下一步继续聚焦：
    - 评估 control queue / ASR round stats / preview text 这些剩余 cloud-owned
      状态是否继续收口成 typed truth，或者转向 typed query/export 边界
- `Step 5.481`
  - `river_cloud` 继续把 XiaoZhi transport/server audio format
    状态从散落的 `g_river_cloud.xiaozhi_server_*` 裸字段收口成显式 truth
  - 新增 transport-owned format truth：
    - `river_cloud_xiaozhi_server_audio_format_truth_t`
  - `river_cloud_context_t` 不再保留散落的：
    - `xiaozhi_server_sample_rate`
    - `xiaozhi_server_frame_duration_ms`
  - `adapter` / `session` / `playback_runtime` 里的：
    - default format init
    - server hello observation
    - decoder open success note
    - audio event sample-rate / frame-duration fallback
    现在都统一走 typed server audio format truth
  - 这一步继续把 `river_cloud` 从：
    - scattered server-format fallback bag
    推进到：
    - explicit server audio format truth
  - 下一步继续聚焦：
    - playback/downlink 范围已基本完成状态真相收口，继续评估是否转向
      `resource boundary + typed query/export`，或者进一步收口 XiaoZhi
      io/uplink worker 生命周期，减少 dialog/runtime 对 cloud transport
      执行细节的感知
- `Step 5.480`
  - `river_cloud` 继续把 XiaoZhi downlink worker / stream-format
    状态从散落的 `g_river_cloud.xiaozhi_downlink_*` 裸字段收口成显式 truth
  - 新增 downlink-owned stream truth：
    - `river_cloud_xiaozhi_downlink_stream_truth_t`
  - `river_cloud_context_t` 不再保留散落的：
    - `xiaozhi_downlink_started`
    - `xiaozhi_downlink_sample_rate`
    - `xiaozhi_downlink_frame_duration_ms`
  - `river_cloud_xiaozhi_playback_runtime.c` 里的：
    - frame-duration fallback
    - diag export 的 sample-rate / frame-duration / worker-started
    - backend start-playback 参数拼装
    - audio-event format note
    - downlink worker start gate / started 标记
    现在都统一走 typed downlink stream truth
  - 这一步继续把 `river_cloud` 从：
    - scattered downlink worker/format bag
    推进到：
    - explicit downlink stream truth
  - 下一步继续聚焦：
    - 评估 `server_sample_rate/frame_duration_ms` 与 decode-format fallback
      是否继续收口成 typed codec truth；否则就把剩余重构重点转向资源边界和更高层
      dialog/playback query 收敛
- `Step 5.479`
  - `river_cloud` 继续把 XiaoZhi playback segment queue
    状态从散落的 `g_river_cloud.xiaozhi_playback_*` 裸字段收口成显式 truth
  - 新增 playback-owned queue truth：
    - `river_cloud_xiaozhi_playback_segment_queue_truth_t`
  - `river_cloud_context_t` 不再保留散落的：
    - `xiaozhi_playback_segment_head`
    - `xiaozhi_playback_segment_count`
    - `xiaozhi_playback_segments[...]`
  - `river_cloud_xiaozhi_playback_runtime.c` 里的：
    - playback supply source queued-segment capture
    - current segment lookup / queue pop
    - playback status dump queued count
    - clear-meta reset / queue memset
    - `playback_note_meta(...)` 的 existing-slot scan / tail append /
      queue-full 判定
    现在都统一走 typed playback segment-queue truth
  - 这一步继续把 `river_cloud` 从：
    - scattered playback segment queue bag
    推进到：
    - explicit playback segment queue truth
  - 下一步继续聚焦：
    - 把 downlink format / worker lifecycle / start-stop
      这批剩余 coarse ownership 继续收口成更明确的 playback/downlink truth，
      让 session/runtime/policy 不再回读播放供给格式和 worker 执行态拼装结果
- `Step 5.478`
  - `river_cloud` 继续把 XiaoZhi playback/downlink 的 start-gate /
    starvation-watch / retry / ring-overflow
    状态从散落的 `g_river_cloud.xiaozhi_*` 裸字段收口成显式 truth
  - 新增 playback/downlink truth：
    - `river_cloud_xiaozhi_playback_gate_truth_t`
    - `river_cloud_xiaozhi_downlink_runtime_truth_t`
  - `river_cloud_context_t` 不再保留散落的：
    - `xiaozhi_playback_start_policy`
    - `xiaozhi_playback_start_frames`
    - `xiaozhi_playback_prefetch_frames`
    - `xiaozhi_playback_buffer_frames`
    - `xiaozhi_playback_start_cautious_history`
    - `xiaozhi_downlink_ring_dropped`
    - `xiaozhi_downlink_retry_valid`
    - `xiaozhi_downlink_starved_since_ms`
    - `xiaozhi_downlink_last_supply_ms`
  - `river_cloud_xiaozhi_playback_runtime.c` 里的：
    - start-gate store / refresh / current-gate / diag capture
    - playback queued-frames / has-work / buffer-budget 计算
    - rebuffer keep-retry / inline-recover / write-success 生命周期
    - reset-downlink / pending-stop drop / starvation-watch 维护
    - upstream-starved 判定 / write-failed starved 推断
    - downlink ring overflow / last-supply 时间戳更新
    现在都统一走 typed playback-gate / downlink-runtime truth
  - 这一步继续把 `river_cloud` 从：
    - scattered gate/watch/retry thresholds bag
    推进到：
    - explicit playback gate truth
    - explicit downlink runtime truth
  - 下一步继续聚焦：
    - 把 downlink format / segment queue / worker lifecycle
      这批剩余 coarse ownership 继续收口成更明确的 playback/downlink truth，
      让 session/runtime/policy 不再回读云端播放供给细节和 worker 态拼装结果
- `Step 5.477`
  - `river_cloud` 继续把 XiaoZhi playback 的 execution / recovery
    状态从散落的 `g_river_cloud.xiaozhi_playback_*` 裸字段收口成显式 runtime truth
  - 新增 playback-owned runtime truth：
    - `river_cloud_xiaozhi_playback_runtime_truth_t`
  - `river_cloud_context_t` 不再保留散落的：
    - `xiaozhi_playback_active`
    - `xiaozhi_playback_phase`
    - `xiaozhi_playback_rebuffer_cause`
    - `xiaozhi_playback_recovery_path`
    - `xiaozhi_playback_recovery_outcome`
    - `xiaozhi_playback_rebuffer_pending`
    - `xiaozhi_tts_stop_pending`
    - `xiaozhi_playback_rebuffer_count`
    - `xiaozhi_playback_rebuffer_streak`
  - `river_cloud_xiaozhi_playback_runtime.c` 里的：
    - playback physical-active / phase / rebuffer-cause / recovery-path /
      recovery-outcome accessors
    - start-gate / prefetch / segment-gap hold / diag / rebuffer-observe capture
    - reset / started / stop / rebuffer / resume / pending-stop 生命周期
    - playback-start / prefetch / rebuffer / pending-stop / ack-progress 日志与判断
    现在都统一走 typed playback runtime truth
  - 这一步继续把 `river_cloud` 从：
    - scattered playback execution/recovery bag
    推进到：
    - explicit playback runtime truth
    - shared typed execution/recovery access boundary
  - 下一步继续聚焦：
    - 把 start-gate / starvation watch / retry / buffer threshold
      这批剩余 coarse ownership 继续收口成更明确的 playback/downlink truth，
      让 session/runtime/policy 不再回读 downlink 恢复细节和阈值拼装结果
- `Step 5.476`
  - `river_cloud` 继续把 XiaoZhi playback 的 meta / terminal / context
    状态从散落的 `g_river_cloud.xiaozhi_playback_*` 裸字段收口成显式 truth
  - 新增 playback-owned truth：
    - `river_cloud_xiaozhi_playback_context_truth_t`
    - `river_cloud_xiaozhi_playback_meta_truth_t`
    - `river_cloud_xiaozhi_playback_terminal_truth_t`
  - `river_cloud_context_t` 不再保留散落的：
    - response/playback/segment id
    - text / expected_duration_ms / meta_gap / prefetch_target / last_meta_ms
    - started/cleared/completed reported
    - wait / last-segment / last-fully-heard / terminal ack/state 相关裸字段
  - `river_cloud_xiaozhi_playback_runtime.c` 里的：
    - `river_cloud_xiaozhi_playback_note_meta(...)`
    - `river_cloud_xiaozhi_try_queue_playback_started_ack(...)`
    - `river_cloud_xiaozhi_try_queue_playback_cleared_ack(...)`
    - `river_cloud_xiaozhi_try_queue_playback_completed_ack(...)`
    - `river_cloud_xiaozhi_playback_finalize_cleared(...)`
    - segment-gap hold / prefetch / playback-start 日志路径
    现在都统一走 typed playback truth
  - 这一步继续把 `river_cloud` 从：
    - scattered playback terminal/meta/context bag
    推进到：
    - explicit playback-owned truth
    - shared typed context/meta/terminal access boundary
  - 下一步继续聚焦：
    - 把 `rebuffer/phase/recovery` 这批剩余 coarse state 也收口成 playback/downlink
      truth，让 adapter / session / dialog 不再回读恢复细节和粗粒度 backend 状态
- `Step 5.475`
  - `river_cloud` 先把 XiaoZhi `turn semantics` 从散落的
    `g_river_cloud.xiaozhi_*` 裸字段收口成显式子状态与窄视图
  - 新增显式 cloud-owned 真相与读取边界：
    - `river_cloud_xiaozhi_turn_semantics_state_t`
    - `river_cloud_xiaozhi_turn_semantics_view_t`
    - `river_cloud_xiaozhi_capture_turn_semantics_view(...)`
    - `river_cloud_xiaozhi_clear_session_id(...)`
  - `river_cloud_xiaozhi_fill_runtime_snapshot(...)`、
    `river_cloud_xiaozhi_dump_session_status(...)`、
    `river_cloud_xiaozhi_turn_accepted(...)`、
    `river_cloud_xiaozhi_note_semantic_fallback(...)`、
    `river_cloud_xiaozhi_commit_pending_text_finalization(...)`
    现在都统一消费 typed turn-semantics view
  - `river_cloud_xiaozhi_playback_runtime.c` 的输出 speaking 判定也不再直读：
    - `g_river_cloud.xiaozhi_output_state`
  - `adapter` / `round_runtime` 的 session-id 清理统一走：
    - `river_cloud_xiaozhi_clear_session_id(...)`
  - 这一步继续把 `river_cloud` 从：
    - scattered cloud turn-semantics bag
    推进到：
    - explicit XiaoZhi turn-semantics truth
    - typed turn-semantics export/view boundary
  - 下一步继续聚焦：
    - 把 XiaoZhi downlink/playback 剩余散落的 supply / wait / terminal /
      segment-gap 观测状态继续收口成更明确的 playback/downlink truth，
      让 session/runtime/policy 不再回读裸字段和字符串拼装结果
- `Step 5.474`
  - `dialog runtime` 开始把外部模块从整份 `snapshot` 读取中解耦，先收口
    `river_voice_runtime_policy.c` 对 `dialog snapshot` 的直接策略解释
  - 新增窄化对外真相：
    - `river_dialog_runtime_voice_policy_view_t`
    - `river_dialog_runtime_get_voice_policy_view(...)`
    - `river_dialog_runtime_capture_voice_policy_view_locked(...)`
  - `river_voice_runtime_policy.c` 不再获取整份：
    - `river_dialog_runtime_snapshot_t`
    - `river_dialog_runtime_get_snapshot(...)`
    来判断：
    - cloud playback engaged
    - quiet window
    - restart pending block
  - AEC / duplex policy 现在只消费 voice-policy 相关最小字段，而不是继续耦合
    dialog snapshot 的宽字段集合
  - 这一步继续把 `dialog runtime` 从：
    - external snapshot-shaped policy dependency
    推进到：
    - typed dialog voice-policy view
  - 下一步继续聚焦：
    - 继续检查其他外部 snapshot 消费点，优先把真正参与策略判断的调用点继续替换为
      typed view / typed query，而把 snapshot 留给状态转储和兼容诊断
- `Step 5.473`
  - `dialog runtime` 继续把剩余 `control_facts -> snapshot` 导出边界收口成
    typed control export view，并把命名拉齐到
    `*_state_to_snapshot_locked(...)`
  - 新增 control snapshot export carrier：
    - `river_dialog_runtime_control_export_view_t`
    - `river_dialog_runtime_capture_control_export_view_locked(...)`
    - `river_dialog_runtime_apply_control_export_view_to_snapshot_locked(...)`
    - `river_dialog_runtime_export_control_state_to_snapshot_locked(...)`
  - 原先直接从 `control_facts` 抄字段到 snapshot 的：
    - `river_dialog_runtime_export_control_facts_to_snapshot_locked(...)`
    已退出
  - `wake_admission`、`tts_interrupt_requested`、cloud-event 控制刷新、local
    playback fallback 控制刷新、`reconcile_facts_locked(...)` 初始化路径现在都统一
    走 shared control export view
  - 这一步继续把 `dialog runtime` 从：
    - control snapshot field copy
    推进到：
    - typed control export projection
  - 下一步继续聚焦：
    - 评估是否把当前 `runtime/control/cloud` 三条 snapshot export 入口继续合成
      更高一层的顶层 projector，进一步压缩 snapshot 作为内部桥接缓存的角色
- `Step 5.472`
  - `dialog runtime` 继续把 cloud-owned 的 `round/io/session/playback`
    snapshot 导出边界收口成 typed `cloud export view`
  - 新增 cloud export carrier：
    - `river_dialog_runtime_round_export_view_t`
    - `river_dialog_runtime_io_export_view_t`
    - `river_dialog_runtime_session_export_view_t`
    - `river_dialog_runtime_cloud_playback_export_view_t`
    - `river_dialog_runtime_cloud_export_view_t`
    - `river_dialog_runtime_capture_cloud_export_view_locked(...)`
    - `river_dialog_runtime_apply_cloud_export_view_to_snapshot_locked(...)`
    - `river_dialog_runtime_export_cloud_state_to_snapshot_locked(...)`
  - 原先分别直写 snapshot 的四组 helper 已收口为 shared `capture + apply`：
    - `river_dialog_runtime_export_round_facts_to_snapshot_locked(...)`
    - `river_dialog_runtime_export_io_facts_to_snapshot_locked(...)`
    - `river_dialog_runtime_export_session_facts_to_snapshot_locked(...)`
    - `river_dialog_runtime_export_playback_facts_to_snapshot_locked(...)`
  - `river_dialog_runtime_import_cloud_snapshot_locked(...)` 现在只通过：
    - `river_dialog_runtime_export_cloud_state_to_snapshot_locked(...)`
    导出 cloud-owned snapshot 区域
  - cloud event 的 `session_id` 更新路径也不再单独直写 session snapshot，而是统一
    复用 shared cloud export view
  - 这一步继续把 `dialog runtime` 从：
    - split cloud snapshot field-copy helpers
    推进到：
    - typed cloud export projection
  - 下一步继续聚焦：
    - 把剩余 `control_facts -> snapshot` 也推进成 typed export carrier，尽量让
      dialog runtime 内只保留 apply-only 的 snapshot 投影边界
- `Step 5.471`
  - `dialog runtime` 继续把 publish side 的 snapshot 导出边界收口成显式
    export view
  - 新增 publish snapshot export carrier：
    - `river_dialog_runtime_publish_export_view_t`
    - `river_dialog_runtime_capture_publish_export_view_locked(...)`
    - `river_dialog_runtime_apply_publish_export_view_to_snapshot_locked(...)`
  - `river_dialog_runtime_export_publish_state_to_snapshot_locked(...)`
    不再直接从 `publish_state` 抄字段到 snapshot，而是统一走：
    - `capture + apply`
  - 当前 runtime-state snapshot 导出已两侧对齐：
    - playback/error -> explicit export view
    - publish state -> explicit export view
  - 这一步继续把 `dialog runtime` 从：
    - publish snapshot field copy from runtime truth
    推进到：
    - typed publish export view
  - 下一步继续聚焦：
    - 继续检查 `control_facts` / `round/io/session` 这几组 snapshot export 是否也要
      继续引入 typed export carrier，最终让 snapshot 导出 helper 统一成 apply-only
      语义
- `Step 5.470`
  - `dialog runtime` 继续把 playback/error 的 snapshot 导出边界收口成显式
    export view
  - 新增 playback/error snapshot export carrier：
    - `river_dialog_runtime_playback_error_export_view_t`
    - `river_dialog_runtime_capture_playback_error_export_view_locked(...)`
    - `river_dialog_runtime_apply_playback_error_export_view_to_snapshot_locked(...)`
  - 原先直接从 `error_truth` / `playback_truth` 抄字段到 snapshot 的 helper 已收口为：
    - `river_dialog_runtime_export_playback_error_state_to_snapshot_locked(...)`
    并统一走：
    - `capture + apply`
  - `refresh_error_recovering_locked(...)`、
    `refresh_playback_locked(...)`、
    `export_runtime_state_to_snapshot_locked(...)`
    现在都通过 shared export view 刷新 snapshot
  - 这一步继续把 `dialog runtime` 从：
    - snapshot field-by-field copy from runtime truth
    推进到：
    - typed playback/error export view
  - 下一步继续聚焦：
    - 继续检查 publish/export 侧是否也要采用同样的 export-view 模式，进一步让
      snapshot 导出 helper 只保留 apply 语义
- `Step 5.469`
  - `dialog runtime` 继续把剩余 `derived_facts` 混合状态拆成显式
    `error_truth` / `playback_truth`
  - 删除：
    - `river_dialog_runtime_derived_facts_t`
    - `g_river_dialog_runtime.derived_facts`
  - 新增显式 runtime-owned truth：
    - `river_dialog_runtime_error_truth_t`
    - `river_dialog_runtime_playback_truth_t`
    - `g_river_dialog_runtime.error_truth`
    - `g_river_dialog_runtime.playback_truth`
  - 错误与播放刷新路径现在分别只维护对应 truth：
    - `refresh_error_recovering_locked(...)` 只更新 `error_truth`
    - `refresh_playback_locked(...)` 只更新 `playback_truth`
    - `export_playback_error_facts_to_snapshot_locked(...)` 再统一把两组 truth
      投影到 snapshot
  - 这一步继续把 `dialog runtime` 从：
    - residual derived state bag
    推进到：
    - explicit error truth
    - explicit playback truth
  - 下一步继续聚焦：
    - 继续检查 `snapshot.playback_*` / `snapshot.error_*` 导出 helper 是否还要继续
      从“snapshot field copy”推进到更明确的 typed export carrier，减少 snapshot
      本身被长期当成内部桥接缓存
- `Step 5.468`
  - `dialog runtime` 继续把 `snapshot export` 的 publish/runtime 边界收口成
    更明确的 typed helper
  - 原先混合导出 `publish_state + derived_facts` 的：
    - `river_dialog_runtime_export_derived_facts_to_snapshot_locked(...)`
    已拆成：
    - `river_dialog_runtime_export_playback_error_facts_to_snapshot_locked(...)`
    - `river_dialog_runtime_export_publish_state_to_snapshot_locked(...)`
    - `river_dialog_runtime_export_runtime_state_to_snapshot_locked(...)`
  - 各调用点现在按真实 ownership 只刷新需要的 snapshot 区域：
    - playback/error 变更路径只导出：
      - `error_recovering`
      - `error_kind`
      - `playback_owner_kind`
      - `playback_active`
      - `playback_recovering`
    - publish/reason 变更路径只导出：
      - `interaction_state`
      - `transition_count`
      - `reason`
    - boot / cloud-event 这类同时影响两边的路径才走：
      - `river_dialog_runtime_export_runtime_state_to_snapshot_locked(...)`
  - 这一步继续把 `dialog runtime` 从：
    - mixed snapshot export helper
    推进到：
    - explicit publish export boundary
    - explicit playback/error export boundary
  - 下一步继续聚焦：
    - 继续检查 `derived_facts` 剩余字段是否也要继续从“混合缓存包”推进到更明确
      的 typed runtime state / export carrier，减少名称与职责错位
- `Step 5.467`
  - `dialog runtime` 继续把 interaction publish 的 `state/count/reason`
    收口成统一 `publish_state`
  - `river_dialog_runtime_publish_observe_t` 已提升为：
    - `river_dialog_runtime_publish_state_t`
    并统一持有：
    - `interaction_state`
    - `transition_count`
    - `reason`
  - `derived_facts` 已不再承载 interaction publish 状态：
    - 删除：
      - `derived_facts.interaction_state`
      - `derived_facts.transition_count`
    - `snapshot.interaction_state` / `snapshot.transition_count` /
      `snapshot.reason` 现在统一从 `publish_state` 导出
  - `publish_locked(...)` 不再手工做：
    - `transition_count++`
    - `interaction_state = next_state`
    而是统一应用 typed publish view
  - 这一步继续把 `dialog runtime` 从：
    - interaction publish state <- derived cache
    推进到：
    - explicit publish state
  - 下一步继续聚焦：
    - 继续把 `export_derived_facts_to_snapshot_locked(...)` 收窄成纯
      playback/error export，评估是否要把剩余 derived/export 路径再拆成更明确
      的 typed snapshot export helper
- `Step 5.466`
  - `dialog runtime` 继续把 interaction publish 的 `reason` 边界收口成 typed
    publish reason view
  - 新增统一 reason publish carrier：
    - `river_dialog_runtime_publish_observe_t`
    - `river_dialog_runtime_publish_reason_view_t`
    - `river_dialog_runtime_capture_publish_reason_view_locked(...)`
    - `river_dialog_runtime_apply_publish_reason_view_locked(...)`
  - `derived_facts.reason` 已退出 runtime 逻辑路径：
    - `snapshot.reason` 现在从独立 `publish_observe.reason` 导出
    - `publish_locked(...)` 不再反向读取 `derived_facts.reason`
    - `finalize_commit_locked(...)`、`init(...)`、
      `note_tts_interrupt_requested(...)`
      也统一走 shared reason view
  - 这一步继续把 `dialog runtime` 从：
    - reason publish / snapshot export <- derived cache back-dependency
    推进到：
    - explicit publish reason boundary
  - 下一步继续聚焦：
    - 把 `interaction_state` / `transition_count` 也从 `derived_facts`
      收到显式 publish state，切断 publish state 对 derived cache 的最后回读
- `Step 5.465`
  - `dialog runtime` 继续把 interaction publish 的 `previous -> next`
    transition 收口成 typed publish view
  - 新增统一 publish capture：
    - `river_dialog_runtime_interaction_publish_view_t`
    - `river_dialog_runtime_capture_interaction_publish_view_locked(...)`
  - 删除单用途 helper：
    - `river_dialog_runtime_compute_interaction_state_locked(...)`
  - `river_dialog_runtime_publish_locked(...)` 不再手工做：
    - next-state 计算
    - state-changed 判定
    - transition_count 递增
    而是统一从 publish view 读取：
    - `previous_state`
    - `next_state`
    - `state_changed`
  - 这一步继续把 `dialog runtime` 从：
    - interaction publish transition branching
    推进到：
    - single publish view
- `Step 5.464`
  - `dialog runtime` 继续把 commit checkpoint 的 `before/after` 判定收口到
    raw interaction evaluation
  - 新增统一 checkpoint 转换 helper：
    - `river_dialog_runtime_capture_commit_checkpoint_from_interaction_eval(...)`
  - `river_dialog_runtime_capture_commit_checkpoint_locked(...)` 不再回读：
    - `derived_facts.playback_active`
    - `derived_facts.playback_recovering`
    - `derived_facts.error_recovering`
    - `derived_facts.interaction_state`
    而是统一从同一次 interaction evaluation 派生
  - `river_dialog_runtime_commit_ingress(...)` 与
    `river_dialog_runtime_finalize_commit_locked(...)`
    的 `before/after` checkpoint 现在都使用 shared raw checkpoint capture
  - 这一步继续把 `dialog runtime` 从：
    - commit publish gating <- derived checkpoint cache
    推进到：
    - raw interaction-eval checkpoint view
- `Step 5.463`
  - `dialog runtime` 继续把 interaction 的 `projection -> state/policy`
    评估链收口成单一 typed evaluation
  - 新增统一 interaction evaluation：
    - `river_dialog_runtime_interaction_eval_t`
    - `river_dialog_runtime_capture_interaction_eval_locked(...)`
  - `river_dialog_runtime_interaction_projection_t` 不再回填：
    - `interaction_state`
    并且 `error_recovering` 也不再从：
    - `derived_facts.error_recovering`
    反向读取，而是直接从 runtime raw error flags 派生
  - `river_dialog_runtime_compute_interaction_state_locked(...)`、
    `river_dialog_runtime_cloud_round_active_locked(...)`、
    `river_dialog_runtime_wakeword_block_reason_locked(...)`、
    `river_dialog_runtime_allows_barge_in_interrupt(...)`
    现在统一复用 shared interaction evaluation
  - 这一步继续把 `dialog runtime` 从：
    - split interaction-state / policy checks
    推进到：
    - single interaction evaluation view
- `Step 5.462`
  - `dialog runtime` 继续把 playback 的 `projection -> truth -> output_turn`
    评估链收口成单一 typed evaluation
  - 新增统一 playback evaluation：
    - `river_dialog_runtime_playback_eval_t`
    - `river_dialog_runtime_capture_playback_eval_locked(...)`
    - `river_dialog_runtime_output_turn_quiesced_from_playback_eval(...)`
  - `river_dialog_runtime_capture_interaction_projection_locked(...)`
    不再从 `derived_facts` 反向读取：
    - `playback_active`
    - `playback_recovering`
    而是从同一次 playback evaluation 的 truth 直接派生
  - `river_dialog_runtime_refresh_playback_locked(...)`、
    `river_dialog_runtime_output_turn_quiesced_locked(...)`、
    `river_dialog_runtime_playback_error_is_managed_recovery_locked(...)`
    现在都统一读取 shared playback evaluation
  - 这一步继续把 `dialog runtime` 从：
    - split playback projection/truth/output-turn evaluation
    推进到：
    - single playback evaluation view
- `Step 5.461`
  - `dialog runtime` 继续把 cloud snapshot 的 playback 导入收口成单一
    typed import
  - 新增统一 playback import：
    - `river_dialog_runtime_cloud_playback_import_t`
  - `river_dialog_runtime_cloud_import_t` 不再只携带：
    - `playback_facts`
    而是统一携带：
    - `playback.facts`
    - `playback.observe`
  - `river_dialog_runtime_capture_cloud_snapshot(...)`
    不再额外输出独立的 `cloud_playback_observe`
  - `river_dialog_runtime_import_cloud_snapshot_locked(...)`
    现在一次性导入：
    - `cloud_playback_facts`
    - `cloud_playback_observe`
  - `river_dialog_runtime_ingress_t` 与 commit 流程不再并行携带：
    - `has_cloud_playback_observe`
    - `cloud_playback_observe`
  - 这一步继续把 `dialog runtime` 从：
    - split cloud-playback facts/observe ingress transport
    推进到：
    - single cloud-playback import
- `Step 5.460`
  - `downlink/playback runtime` 继续把 playback 对外观测导出收口成 typed
    diagnostics view
  - 新增统一 diagnostics capture：
    - `river_cloud_xiaozhi_playback_diag_view_t`
    - `river_cloud_xiaozhi_capture_playback_diag_view(...)`
  - `river_cloud_xiaozhi_dump_playback_status(...)` 与
    `river_cloud_xiaozhi_fill_playback_runtime_snapshot(...)`
    现在共享同一份 playback 采样结果，而不是分别再去拼：
    - truth view
    - observe view
    - start gate
    - queue/rebuffer counters
  - 新增统一 recovery outcome 文本边界 helper：
    - `river_cloud_xiaozhi_playback_recovery_outcome_label(...)`
  - `downlink queue` 日志与 runtime snapshot 的 recovery path/outcome、
    start gate、rebuffer cause、queue counters 现在都从 diagnostics view 读取
  - 这一步继续把 `downlink/playback runtime` 从：
    - duplicated playback export sampling
    推进到：
    - single playback diagnostics capture view
- `Step 5.459`
  - `downlink/playback runtime` 继续把 rebuffer recovery path 的本地真相源收口成 typed path
  - `river_cloud_xiaozhi_playback_rebuffer_observe_view_t` 不再保存
    字符串 `recover_path`，而是保存：
    - `river_cloud_playback_recovery_path_t recovery_path`
  - 删除仅为字符串路径服务的中间结果：
    - `river_cloud_xiaozhi_managed_rebuffer_recovery_result_t`
  - managed rebuffer 执行链现在统一复用：
    - `river_cloud_xiaozhi_rebuffer_recovery_result_t`
  - `river_cloud_xiaozhi_request_playback_rebuffer_recovery(...)`
    不再返回：
    - `river_status_t + recover_path_out`
    而是直接返回 typed recovery result
  - `write_failed`、`inline recover request`、`managed rebuffer request`、
    `upstream gap rebuffer` 现在都在本地传递：
    - `river_cloud_playback_recovery_path_t`
    只在日志边界通过：
    - `river_cloud_xiaozhi_playback_recovery_path_label(...)`
    转成字符串
  - 这一步继续把 `downlink/playback runtime` 从：
    - string-based recovery-path propagation
    推进到：
    - typed recovery-path truth + log-boundary translation
- `Step 5.458`
  - `downlink/playback runtime` 继续把 managed rebuffer 执行结果收口成 typed result
  - 新增统一 managed rebuffer recovery result：
    - `river_cloud_xiaozhi_managed_rebuffer_recovery_result_t`
  - `river_cloud_xiaozhi_execute_managed_playback_rebuffer_recovery(...)`
    不再返回：
    - `river_status_t + recover_path_out`
    的松散组合，而是统一返回 typed result
  - `river_cloud_xiaozhi_execute_managed_playback_rebuffer_request(...)`
    现在也直接返回 typed result，并统一使用：
    - `result.status`
    - `result.recover_path`
    处理 fresh-start fallback 日志
  - `write_failed` 与 `upstream starved` 调用侧不再把 managed execute 结果拆成：
    - return status
    - mutable recover_path out-param
  - 这一步继续把 `downlink/playback runtime` 从：
    - split managed-rebuffer status/path result handling
    推进到：
    - single typed managed-rebuffer result
- `Step 5.457`
  - `downlink/playback runtime` 继续把 rebuffer recovery path 的 preferred/fallback 执行收口成 typed attempt/result
  - 新增统一 recovery attempt/result：
    - `river_cloud_xiaozhi_rebuffer_recovery_attempt_t`
    - `river_cloud_xiaozhi_rebuffer_recovery_result_t`
    - `river_cloud_xiaozhi_capture_rebuffer_recovery_attempt(...)`
    - `river_cloud_xiaozhi_execute_rebuffer_recovery_attempt(...)`
  - 新增统一 path executor：
    - `river_cloud_xiaozhi_execute_rebuffer_recovery_path(...)`
  - `river_cloud_xiaozhi_request_playback_rebuffer_recovery(...)` 不再手写：
    - `prefer_service_recover`
    - `service_recover -> stop_rebuffer fallback`
    - `stop_rebuffer -> service_recover fallback`
    三段变量翻译和执行分支
  - recovery fallback 日志现在统一归一成：
    - `xiaozhi playback recovery fallback: from=... to=...`
  - 这一步继续把 `downlink/playback runtime` 从：
    - split preferred/fallback recovery path orchestration
    推进到：
    - typed rebuffer recovery attempt/result
- `Step 5.456`
  - `downlink/playback runtime` 继续把 rebuffer 请求日志的公共观测字段收口成 typed observe view
  - 新增统一 rebuffer observe view：
    - `river_cloud_xiaozhi_playback_rebuffer_observe_view_t`
    - `river_cloud_xiaozhi_capture_playback_rebuffer_observe_view(...)`
  - `write_failed rebuffer request`、`inline service recover request`、`upstream gap rebuffer`
    日志现在统一从 observe view 读取：
    - `cause`
    - `supply`
    - `phase`
    - `backend`
    - `start_gate`
    - `queued/low_water`
    - `prefetch_target_ms`
    - `rebuffer_count/streak`
    - `recover_path`
  - `rebuffer` 相关日志不再分散从全局和 plan 混合拉取同一组字段
  - 这一步继续把 `downlink/playback runtime` 从：
    - duplicated rebuffer log field assembly
    推进到：
    - single typed rebuffer observe view
- `Step 5.455`
  - `downlink/playback runtime` 继续把 managed rebuffer 执行收口成 typed request executor
  - 新增统一 managed request 描述与执行入口：
    - `river_cloud_xiaozhi_managed_rebuffer_request_t`
    - `river_cloud_xiaozhi_capture_managed_playback_rebuffer_request(...)`
    - `river_cloud_xiaozhi_start_managed_playback_rebuffer_request(...)`
    - `river_cloud_xiaozhi_execute_managed_playback_rebuffer_request(...)`
  - `river_cloud_xiaozhi_write_failed_followup_t` 不再直接暴露
    `force_stop_rebuffer`，而是携带完整 `rebuffer_request`
  - `write_failed` 与 `upstream starved` 两条链路现在统一走：
    - typed request capture
    - managed rebuffer start
    - caller-specific request log
    - shared recovery execute / fallback log
  - 删除旧的 write-failed 专用 wrapper：
    - `river_cloud_xiaozhi_handle_write_failed_managed_rebuffer(...)`
  - 这一步继续把 `downlink/playback runtime` 从：
    - per-caller managed-rebuffer begin/execute/fallback orchestration
    推进到：
    - shared typed managed-rebuffer request executor
- `Step 5.454`
  - `downlink/playback runtime` 继续把 write-failed 恢复链收口成 typed follow-up
  - 新增统一 follow-up 描述：
    - `river_cloud_xiaozhi_write_failed_followup_t`
    - `river_cloud_xiaozhi_capture_write_failed_followup(...)`
  - `river_cloud_xiaozhi_handle_playback_write_failed(...)` 不再手写三段
    `inline_result -> managed_rebuffer/log/force_stop` 分支翻译：
    - `inline_success`
    - `managed_rebuffer`
    - `force_stop_rebuffer`
    - `request_log`
    现在都先通过 typed follow-up 收口，再统一执行 managed rebuffer
  - 这一步继续把 `downlink/playback runtime` 从：
    - split inline-result follow-up branching
    推进到：
    - single typed write-failed follow-up
- `Step 5.453`
  - `dialog runtime` 继续把 playback raw projection 与派生 truth 解耦
  - 删除 `playback projection` 对已派生 truth 的反向依赖：
    - `river_dialog_runtime_playback_projection_t` 不再携带
      `playback_recovering`
    - `river_dialog_runtime_capture_playback_projection_locked(...)`
      不再回填 `derived_facts->playback_recovering`
  - 新增统一 playback truth helper：
    - `river_dialog_runtime_playback_truth_t`
    - `river_dialog_runtime_capture_playback_truth_from_projection(...)`
  - `dialog runtime` 现在统一通过 raw projection -> playback truth 链路派生：
    - `playback_active`
    - `playback_recovering`
    - `playback_owner_kind`
    - `output_turn_quiesced` 的 recovering 判定
  - 删除旧的分散 truth helper：
    - `river_dialog_runtime_compute_playback_recovering_from_projection(...)`
    - `river_dialog_runtime_compute_playback_active_from_projection(...)`
    - `river_dialog_runtime_compute_playback_owner_kind_from_projection(...)`
  - 这一步继续把 `dialog runtime` 从：
    - playback projection <- derived truth backfill
    推进到：
    - raw playback projection + single playback truth helper
- `Step 5.452`
  - `dialog runtime` 继续把 local playback import 从分散 prepare/apply 收口成显式 plan
  - 删除旧的两段式 helper：
    - `river_dialog_runtime_prepare_local_playback_import_locked(...)`
    - `river_dialog_runtime_apply_local_playback_import_locked(...)`
    - `river_dialog_runtime_apply_local_playback_state_locked(...)`
  - 新增统一 import plan：
    - `river_dialog_runtime_local_playback_import_plan_t`
    - `river_dialog_runtime_prepare_local_playback_import_plan_locked(...)`
    - `river_dialog_runtime_apply_local_playback_import_plan_locked(...)`
  - `dialog runtime` 现在先生成 local playback import plan，再在 cloud import
    落地后统一应用：
    - stream ownership claim/release
    - playback state 写入
    - error recovery / interrupt clear
  - 这一步继续把 `dialog runtime` 从：
    - split local-playback prepare/apply mutation boundary
    推进到：
    - single typed local-playback import plan
- `Step 5.451`
  - `dialog runtime` 继续收口 local playback shadow 的分散判定 helper
  - 删除分散 helper：
    - `local_playback_state_active_locked(...)`
    - `local_playback_state_recovering_locked(...)`
    - `local_playback_shadow_drives_truth_locked(...)`
    - `local_playback_shadow_active_fallback_locked(...)`
    - `local_playback_shadow_recovering_fallback_locked(...)`
  - 新增统一 shadow view：
    - `river_dialog_runtime_local_playback_shadow_view_t`
    - `river_dialog_runtime_capture_local_playback_shadow_view_locked(...)`
  - `dialog runtime` 现在统一通过 shadow view 驱动：
    - playback projection 的 local fallback 注入
    - local playback import 的 truth-drive gating
    - runtime dump 的 local playback 观测
  - 这一步继续把 `dialog runtime` 从：
    - scattered local playback shadow predicates
    推进到：
    - single local shadow view
- `Step 5.450`
  - `downlink/playback` 继续收口 ready-cycle 的单层 executor 壳
  - 删除中间 helper：
    - `river_cloud_xiaozhi_execute_ready_downlink_cycle(...)`
  - `river_cloud_xiaozhi_process_downlink_task_cycle(...)` 现在直接接管：
    - ready cycle 下的 current-frame acquire
    - write-step dispatch
  - `downlink cycle processor` 现在直接按：
    - `cycle_plan.ready`
    - frame acquired / not acquired
    - `write_current_downlink_frame_step(...)`
    决定后续 task-step
  - 这一步继续把 downlink/playback 从：
    - single-layer ready-cycle executor shell
    推进到：
    - direct cycle-processor ownership
- `Step 5.449`
  - `downlink/playback` 继续把当前帧写入执行收口成单一 write-step helper
  - 删除中间 typed result：
    - `river_cloud_xiaozhi_downlink_frame_write_result_t`
  - `river_cloud_xiaozhi_write_current_downlink_frame_step(...)` 现在直接返回：
    - `river_cloud_xiaozhi_downlink_task_step_result_t`
  - `river_cloud_xiaozhi_execute_ready_downlink_cycle(...)` 不再负责：
    - write-result -> task-step 的二次翻译
    - post-write completion dispatch
  - 单帧写入 helper 现在统一接管：
    - frame oversize abort -> continue
    - write_failed recovery outcome -> continue / sleep poll
    - write success 后的 segment start / ack progress / pending-stop 收尾
  - 这一步继续把 downlink/playback 从：
    - split write-result + post-write completion contract
    推进到：
    - single write-step outcome
- `Step 5.448`
  - `downlink/playback` 继续把 `acquire_current_downlink_frame(...)`
    的 ready 判定从二值枚举收口成布尔返回
  - 删除中间 typed result：
    - `river_cloud_xiaozhi_downlink_frame_acquire_result_t`
  - `river_cloud_xiaozhi_acquire_current_downlink_frame(...)` 现在直接返回：
    - `bool`
  - `river_cloud_xiaozhi_execute_ready_downlink_cycle(...)` 不再负责：
    - acquire-result -> sleep/continue 的枚举翻译
  - 这一步继续把 downlink/playback 从：
    - binary frame-acquire contract
    推进到：
    - direct acquire predicate
- `Step 5.447`
  - `downlink/playback` 继续把 `prepare_downlink_playback(...)`
    的 ready 判定从二值枚举收口成布尔返回
  - 删除中间 typed result：
    - `river_cloud_xiaozhi_downlink_prepare_result_t`
  - `river_cloud_xiaozhi_prepare_downlink_playback(...)` 现在直接返回：
    - `bool`
  - `river_cloud_xiaozhi_prepare_downlink_cycle_plan(...)` 不再负责：
    - `prepare_result` -> ready boolean 的翻译
  - 这一步继续把 downlink/playback 从：
    - binary prepare-result contract
    推进到：
    - direct ready predicate
- `Step 5.446`
  - `downlink/playback` 继续把 prepare 阶段从 `result + out param`
    收口成单一 cycle plan
  - 删除中间 typed result：
    - `river_cloud_xiaozhi_downlink_cycle_result_t`
  - 新增统一 plan：
    - `river_cloud_xiaozhi_downlink_cycle_plan_t`
  - 新增统一 helper：
    - `river_cloud_xiaozhi_prepare_downlink_cycle_plan(...)`
  - `river_cloud_xiaozhi_process_downlink_task_cycle(...)` 现在只按：
    - `cycle_plan.ready`
    - `cycle_plan.step_result`
    - `cycle_plan.queued_frames`
    决定后续执行
  - 这一步继续把 downlink/playback 从：
    - cycle result + queued_frames_out split contract
    推进到：
    - single cycle plan truth
- `Step 5.445`
  - `downlink/playback` 继续把 ready downlink cycle 的执行结果直接归一到
    worker task-step
  - 删除中间 typed result：
    - `river_cloud_xiaozhi_downlink_execute_result_t`
  - `river_cloud_xiaozhi_execute_ready_downlink_cycle(...)` 现在直接返回：
    - `river_cloud_xiaozhi_downlink_task_step_result_t`
  - `river_cloud_xiaozhi_process_downlink_task_cycle(...)` 不再负责：
    - execute-result -> task-step 的二次翻译
  - 该 helper 链现在统一接管：
    - acquire miss -> sleep poll
    - write retry -> sleep poll
    - write abort/progress -> continue
  - 这一步继续把 downlink/playback 从：
    - ready-cycle local result fan-out
    推进到：
    - direct task-step outcome
- `Step 5.444`
  - `downlink/playback` 继续把 downlink worker 的 task-step 调度壳从主循环中
    收口成单一 cycle processor
  - 新增本地 typed result：
    - `river_cloud_xiaozhi_downlink_task_step_result_t`
  - 新增统一 helper：
    - `river_cloud_xiaozhi_process_downlink_task_cycle(...)`
    - `river_cloud_xiaozhi_finish_downlink_task_cycle(...)`
  - 该 helper 现在统一接管：
    - prepare/excute result fan-in
    - idle delay dispatch
    - poll delay dispatch
  - `downlink task` 现在只保留：
    - endless loop
    - call `process_downlink_task_cycle(...)`
    - call `finish_downlink_task_cycle(...)`
- `Step 5.443`
  - `downlink/playback` 继续把 ready 状态下的 downlink cycle 执行从主循环中
    收口成统一 executor
  - 新增本地 typed result：
    - `river_cloud_xiaozhi_downlink_execute_result_t`
  - 新增统一 helper：
    - `river_cloud_xiaozhi_execute_ready_downlink_cycle(...)`
  - 该 helper 现在统一接管：
    - current-frame acquire dispatch
    - frame write dispatch
    - write-aborted immediate continue
    - write-retry poll gating
    - post-write completion dispatch
  - `downlink task` 在 prepare-ready 之后现在只按 typed execute result 决定：
    - poll delay
    - continue
- `Step 5.442`
  - `downlink/playback` 继续把 downlink worker 的前置调度执行从主循环中
    收口成统一 cycle helper
  - 新增本地 typed result：
    - `river_cloud_xiaozhi_downlink_cycle_result_t`
  - 新增统一 helper：
    - `river_cloud_xiaozhi_prepare_downlink_cycle(...)`
  - 该 helper 现在统一接管：
    - downlink active gating
    - playback ACK progress refresh before cycle
    - rebuffer-starved gating
    - segment-gap pause gating
    - empty-queue pending-stop check
    - playback prepare dispatch
  - `downlink task` 在 acquire/write 之前现在只按 typed cycle result 决定：
    - idle delay + continue
    - poll delay + continue
    - ready
- `Step 5.441`
  - `downlink/playback` 继续把单帧写成功后的收尾执行从 downlink task 主循环中
    收口成统一 helper
  - 新增统一 helper：
    - `river_cloud_xiaozhi_complete_written_downlink_frame(...)`
  - 该 helper 现在统一接管：
    - segment start latch on progress
    - playback ACK progress refresh
    - pending-stop check after progress
  - `downlink task` 在 frame write 成功后现在只保留：
    - call `complete_written_downlink_frame(...)`
- `Step 5.440`
  - `downlink/playback` 继续把当前帧获取执行从 downlink task 主循环中
    收口成统一 helper
  - 新增本地 typed result：
    - `river_cloud_xiaozhi_downlink_frame_acquire_result_t`
  - 新增统一 helper：
    - `river_cloud_xiaozhi_acquire_current_downlink_frame(...)`
  - 该 helper 现在统一接管：
    - retry-valid fast path
    - downlink ring read
    - ring read failure后的 pending-stop check
  - `downlink task` 在 prepare 之后、write 之前现在只按 typed frame-acquire result
    决定：
    - delay + continue
    - ready
- `Step 5.439`
  - `downlink/playback` 继续把 playback 准备阶段从 downlink task 主循环中
    收口成统一 helper
  - 新增本地 typed result：
    - `river_cloud_xiaozhi_downlink_prepare_result_t`
  - 新增统一 helper：
    - `river_cloud_xiaozhi_prepare_downlink_playback(...)`
  - 该 helper 现在统一接管：
    - rebuffer resume readiness gating
    - pending-stop before-active gating
    - paused backend resume path
    - recovering backend stall
    - start-threshold / start-playback gating
  - `downlink task` 在读 ring / 写 frame 前现在只按 typed prepare result 决定：
    - delay + continue
    - ready
- `Step 5.438`
  - `downlink/playback` 继续把单帧写入执行从 downlink task 主循环中收口成
    统一 helper
  - 新增本地 typed result：
    - `river_cloud_xiaozhi_downlink_frame_write_result_t`
  - 新增统一 helper：
    - `river_cloud_xiaozhi_write_current_downlink_frame(...)`
  - 该 helper 现在统一接管：
    - frame oversize abort
    - stereo expand
    - playback service write
    - write_failed handler dispatch
    - retry-valid clear on success
  - `downlink task` 现在只按 typed frame-write result 决定：
    - continue
    - delay + continue
    - progress
- `Step 5.437`
  - `downlink/playback` 继续把 `write_failed` 从 downlink task 主循环里
    收口成单入口 handler
  - 新增统一 helper：
    - `river_cloud_xiaozhi_handle_playback_write_failed(...)`
  - `write_failed` 的：
    - recovery plan capture
    - initial failure log
    - inline recover dispatch
    - managed rebuffer fallback dispatch
    现在都由该 helper 接管
  - `downlink task` 在 `river_playback_service_write(...)` 失败后现在只保留：
    - call handler
    - delay + continue on non-inline recovery
  - 这一步继续把 downlink/playback 从：
    - write_failed local orchestration
    推进到：
    - single write-failure entrypoint
- `Step 5.436`
  - `downlink/playback` 继续把 `write_failed` 的恢复执行从 downlink task
    主循环里抽成专用 helper
  - 新增本地 typed result：
    - `river_cloud_xiaozhi_inline_recover_result_t`
  - 新增统一 helper：
    - `river_cloud_xiaozhi_try_write_failed_inline_recover(...)`
    - `river_cloud_xiaozhi_handle_write_failed_managed_rebuffer(...)`
    - `river_cloud_xiaozhi_log_write_failed_rebuffer_request(...)`
  - `write_failed` 主分支现在只保留：
    - capture `recovery_plan`
    - choose `recover_reason`
    - dispatch inline result / fallback executor
  - 这一步继续把 downlink/playback 从：
    - nested write_failed recovery control flow
    推进到：
    - typed inline result + dedicated executors
- `Step 5.435`
  - `downlink/playback` 继续把 managed rebuffer 的进入与执行边界从多个
    局部分支里收口成共享 helper
  - 新增统一 helper：
    - `river_cloud_xiaozhi_begin_managed_playback_rebuffer(...)`
    - `river_cloud_xiaozhi_execute_managed_playback_rebuffer_recovery(...)`
  - `maybe_rebuffer_starved(...)` 现在与 `write_failed` 的 managed rebuffer
    升级路径共用同一条：
    - rebuffer state enter
    - retry frame retain
    - actual recovery execute
  - `write_failed` 的以下三条路径不再各自手写状态变更：
    - inline replay fallback
    - inline recover fallback
    - non-inline direct rebuffer
  - 这一步继续把 downlink/playback 从：
    - scattered managed-rebuffer transition mutations
    推进到：
    - shared transition helpers
- `Step 5.434`
  - `downlink/playback` 继续把 recovery observability 从 `path only` 推进到
    `path + outcome`
  - 新增公共枚举：
    - `river_cloud_playback_recovery_outcome_t`
  - `cloud runtime snapshot` 现在额外导出：
    - `playback_recovery_outcome_kind`
    - `playback_recovery_outcome`
  - XiaoZhi playback runtime 现在会把：
    - inline `service_recover + replay` 成功
      记为 `inline_replay`
    - 任意 `note_playback_rebuffer(...)`
      记为 `managed_rebuffer`
  - `dialog runtime` 也同步镜像：
    - `playback_recovery_outcome_kind`
  - XiaoZhi / dialog runtime dump 现在都会直接打印：
    - `recovery_outcome=...`
  - 这一步继续把恢复真相从：
    - 只能知道走了哪条 path
    推进到：
    - 还能知道这次恢复最终是 inline 自愈还是已经升级成 managed rebuffer
- `Step 5.433`
  - `downlink/playback` 继续把 `playback_recovery_path` 推进成当前 response 内
    稳定可见的最近恢复动作观测
  - 新增统一 helper：
    - `river_cloud_xiaozhi_set_playback_recovery_path(...)`
  - `request_playback_rebuffer_recovery(...)` 不再只在 managed rebuffer 路径里
    独自维护 recovery path；`write_failed` 的 inline `service_recover` 成功、
    以及随后直接回退 `stop_rebuffer` 的分支，现在也都会写入同一条恢复路径真相
  - `finish_playback_rebuffer()` 不再清空 `playback_recovery_path`，因此
    `cloud runtime snapshot` / `dialog runtime snapshot` 会在当前 response
    生命周期内保留最近一次实际恢复路径，而不是恢复一结束就丢
  - XiaoZhi playback status dump 现在也开始打印：
    - `recovery_path=...`
  - 这一步继续把 `recovery_path` 从：
    - 仅覆盖 managed rebuffer 的临时分支产物
    推进成：
    - inline recover / direct stop fallback / managed rebuffer 共用的最近恢复动作观测
- `Step 5.432`
  - `downlink/playback` 继续把 actual recovery path 提升成 cloud/dialog 可见的
    typed observability
  - 新增公共枚举：
    - `river_cloud_playback_recovery_path_t`
  - `cloud runtime snapshot` 现在额外导出：
    - `playback_recovery_path_kind`
    - `playback_recovery_path`
  - XiaoZhi playback runtime 新增持久观测字段：
    - `xiaozhi_playback_recovery_path`
    并在 `request_playback_rebuffer_recovery(...)` 里按 fallback 后的实际路径写入
  - playback observe view / dialog playback observe 现在都会镜像这条路径；
    `dialog runtime dump` 与 `xiaozhi runtime dump` 也开始打印
    `recovery_path`
  - 这一步继续把：
    - internal recovery plan
    - cloud/dialog exported observability
    接到同一条路径真相上，避免外层只能靠 `rebuffer_pending + cause`
    组合反推当前恢复链
- `Step 5.431`
  - `downlink/playback` 继续把 `write_failed` / `starved` 恢复决策收口成统一
    typed recovery plan
  - 新增内部结构：
    - `river_cloud_xiaozhi_playback_recovery_plan_t`
    - `river_cloud_xiaozhi_playback_recovery_path_t`
  - `river_cloud_xiaozhi_capture_playback_recovery_plan(...)` 现在会一次性锁存：
    - `cause`
    - `supply_kind`
    - `start_gate`
    - `low_water_frames`
    - `supply_gap_ms`
    - `recovery_path`
    - `inline_recover_allowed`
  - `maybe_rebuffer_starved(...)` 与 `write_failed` 分支现在统一消费这份
    recovery plan，不再各自散读一组局部变量重建恢复决策
  - `request_playback_rebuffer_recovery(...)` 现在会在 fallback 后返回实际采用的
    recovery path，不再把：
    - `service_recover -> stop_rebuffer`
    - `stop_rebuffer -> service_recover`
    这种回退链错误打印成初始偏好路径
  - 这一步继续把 downlink/playback 恢复链从：
    - scattered local decisions
    - stale fallback diagnostics
    推进到：
    - single recovery plan
    - actual-path logging
- `Step 5.430`
  - `dialog runtime` 继续把 playback terminal 文本观测从 `session_observe`
    中拆出去
  - `river_dialog_runtime_cloud_session_observe_t` 不再承载：
    - `playback_terminal_reason`
    - `playback_terminal_wait_reason`
  - 上述字段现在统一并入：
    - `river_dialog_runtime_cloud_playback_observe_t`
  - `capture_cloud_snapshot(...)` 现在把 playback terminal/wait 文本直接写入
    `cloud_playback_observe`
  - `export_session_facts_to_snapshot_locked()` 不再导出 playback terminal 文本；
    这些字段改由 `export_playback_facts_to_snapshot_locked()` 从
    `cloud_playback_observe` 统一导出
  - 这一步继续把：
    - session 元数据 observe
    - playback terminal observe
    从结构边界上拆开，避免 playback 文本原因链继续寄存在 session observe
- `Step 5.429`
  - `dialog runtime` 继续把 playback 里的 observe-only 字段从 typed facts 中拆出去
  - `river_dialog_runtime_cloud_playback_facts_t` 不再承载：
    - `terminal_state_kind`
    - `rebuffer_cause_kind`
    - `start_policy_kind`
    - `start_frames`
    - `prefetch_frames`
    - `start_cautious_history`
  - 上述字段现在统一并入：
    - `river_dialog_runtime_cloud_playback_observe_t`
  - `capture_cloud_snapshot(...)` 现在把这些字段直接写入
    `cloud_playback_observe`
  - `export_playback_facts_to_snapshot_locked()` 现在也改为从
    `cloud_playback_observe` 导出：
    - terminal state
    - rebuffer cause
    - start gate / prefetch / cautious
  - 这一步继续把：
    - `cloud_playback_facts`
    - `cloud_playback_observe`
    的边界压实成“typed truth vs observe-only metadata”，避免 start-gate /
    rebuffer/terminal 观测字段重新混进 dialog playback 语义事实
- `Step 5.428`
  - `dialog runtime` 继续把 `io/session` 里的纯观测字段从 typed facts 中拆出去
  - `river_dialog_runtime_cloud_io_facts_t` 现在只承载：
    - `input_lane`
    - `output_lane`
  - 新增 `river_dialog_runtime_cloud_io_observe_t`，独立承载：
    - `input_state_text`
    - `output_state_text`
  - `river_dialog_runtime_cloud_session_facts_t` 现在只承载：
    - `turn_accepted`
    - `barge_in_enabled_known`
    - `barge_in_enabled`
  - 新增 `river_dialog_runtime_cloud_session_observe_t`，独立承载：
    - `provider_name`
    - `session_id`
    - `turn_id`
    - `accept_reason`
    - `playback_terminal_reason`
    - `playback_terminal_wait_reason`
  - `capture_cloud_snapshot(...)` / `import_cloud_snapshot_locked(...)` /
    snapshot export 现在都按 `facts` / `observe` 两条链分别搬运 `io/session`
    数据
  - `apply_cloud_event_locked(...)` 更新 sid 时也改写到
    `cloud_session_observe.session_id`
  - 这一步继续把：
    - dialog runtime 可驱动行为的 typed truth
    - 用于日志/调试/镜像的文本元数据
    从 `io/session` 结构里拆开，减少内部 facts 再次夹带 observe-only 文本字段
- `Step 5.427`
  - `dialog runtime` 继续把 cloud snapshot ingress 从扁平字段包收口成与内部事实一致的分层载荷
  - `river_dialog_runtime_cloud_import_t` 不再平铺：
    - round/window/listening 字段
    - input/output io 字段
    - session/turn/meta 字段
    - playback semantic 字段
  - 现在它直接按内部真相结构分成：
    - `round_facts`
    - `io_facts`
    - `session_facts`
    - `playback_facts`
  - `river_dialog_runtime_capture_cloud_snapshot(...)` 现在直接把 cloud runtime
    snapshot 填充到这些 typed facts 载荷里
  - `river_dialog_runtime_import_cloud_snapshot_locked(...)` 也改为整块吸收：
    - `cloud_round_facts`
    - `cloud_io_facts`
    - `cloud_session_facts`
    - `cloud_playback_facts`
    不再逐字段搬运
  - 这一步继续把：
    - dialog runtime ingress/import 结构
    - runtime 内部 facts 结构
    两层收口到同一模型，减少 reducer/import 边界继续维护第二套扁平协议
- `Step 5.426`
  - `dialog runtime` 继续把 cloud snapshot 输入层里的 playback 观测与语义事实拆开
  - `river_dialog_runtime_cloud_import_t` 不再承载：
    - `playback_phase_known`
    - `playback_phase_kind`
  - `river_dialog_runtime_ingress_t` 新增独立 observe 载荷：
    - `has_cloud_playback_observe`
    - `cloud_playback_observe`
  - 新增统一 snapshot 捕获入口：
    - `river_dialog_runtime_capture_cloud_snapshot(...)`
    它会一次性拆出：
    - `cloud_import`
    - `cloud_playback_observe`
  - 新增独立导入路径：
    - `river_dialog_runtime_import_cloud_playback_observe_locked(...)`
  - `commit_cloud_event` / `reduce_local_playback_event` / `sync_cloud_state`
    现在都会把 cloud playback phase 观测值作为独立载荷送入 reducer/import
  - 这一步继续把：
    - dialog runtime cloud import semantic facts
    - playback phase observability
    在 ingress/import 边界彻底拆开，避免 phase 观测字段重新混回内部语义结构
- `Step 5.425`
  - `cloud playback runtime` 继续把 phase 观测值从 truth 链里彻底拆出去
  - 新增独立观测结构：
    - `river_cloud_xiaozhi_playback_observe_view_t`
    - `river_cloud_xiaozhi_capture_playback_observe_view(...)`
  - 新增统一观测读取 helper：
    - `river_cloud_xiaozhi_playback_observed_phase_kind()`
  - `river_cloud_xiaozhi_playback_backend_source_t` 不再承载：
    - `phase`
  - `river_cloud_xiaozhi_fill_playback_runtime_snapshot(...)` 现在同时读取：
    - `truth_view`
    - `observe_view`
  - `playback_phase_known`
    - `playback_phase_kind`
    - `playback_phase` 文本
    现在统一改为从 `observe_view` 导出，不再从
    `truth_view.backend_source.phase` 间接读取
  - 这一步继续把：
    - typed playback truth
    - playback phase observability
    两条内部链路拆开，并把日志链也从 truth 结构里解耦
- `Step 5.424`
  - `dialog runtime` 继续清理“语义事实”和“观测字段”的边界
  - `river_dialog_runtime_cloud_playback_facts_t` 不再承载：
    - `phase_known`
    - `phase_kind`
  - 新增独立观测结构：
    - `river_dialog_runtime_cloud_playback_observe_t`
  - cloud snapshot 的 `playback_phase_known/playback_phase_kind` 现在单独存放到：
    - `cloud_playback_observe`
  - `river_dialog_runtime_export_playback_facts_to_snapshot_locked()`
    改为从 `cloud_playback_observe` 导出 phase 观测值
  - 这一步继续把：
    - typed playback semantics
    - playback phase observability
    这两类数据分层，避免 phase 再次混回语义结构中
- `Step 5.423`
  - `dialog runtime` 继续缩减内部 projection 对 coarse playback phase 的依赖
  - `river_dialog_runtime_playback_projection_t` 不再镜像：
    - `phase_known`
    - `phase_kind`
  - `river_dialog_runtime_capture_playback_projection_locked(...)` 不再把 cloud
    playback phase 塞进 projection 内部语义
  - `river_dialog_runtime_playback_turn_retains_output_turn_from_projection(...)`
    只保留：
    - `!cloud_runtime_available -> true`
    这个保守 fallback
  - 它不再通过：
    - `!phase_known`
    来决定是否保留 output turn
  - 这一步继续把：
    - phase 保留在 cloud/dialog snapshot 里做观测
    - projection 内部决策只读 typed playback truth 与 runtime availability
    这条边界压实
- `Step 5.422`
  - `cloud playback runtime` 继续把内部播放语义从 coarse phase 解耦
  - `river_cloud_xiaozhi_playback_backend_source_t` 现在额外镜像：
    - `stop_pending`
    - `rebuffer_pending`
    - `physical_active`
    - `waiting_next_segment`
  - `river_cloud_xiaozhi_compute_playback_backend_state_from_source(...)`
    不再读取：
    - `phase == REBUFFERING`
    - `phase_is_output_active(...)`
    现在改为直接使用底层 truth 判定 recovering / owned_active / owned_paused
  - `river_cloud_xiaozhi_playback_hold_kind_from_source(...)` 不再依赖
    `phase == WAITING_SEGMENT`
  - 现在改为直接读取：
    - `waiting_next_segment`
    - `rebuffer_pending`
  - `river_cloud_xiaozhi_playback_output_active()` 与
    `river_cloud_xiaozhi_output_speaking_active()` 也统一改读 typed helper：
    - `river_cloud_xiaozhi_playback_backend_source_output_active(...)`
    - `river_cloud_xiaozhi_playback_truth_view_retains_output_turn(...)`
  - 这一步继续把 phase 从“内部决策输入”降级为“观测值”，进一步压缩：
    - backend_state
    - hold_kind
    - output_active
    - speaking retain-output-turn
    对 coarse phase 的依赖
- `Step 5.421`
  - `cloud playback runtime` 继续清理导出链中对 coarse playback phase 的直接依赖
  - `river_cloud_xiaozhi_playback_truth_view_t` 现在额外镜像：
    - `queued_frames`
    - `tts_stop_pending`
    - `rebuffer_pending`
  - 新增统一 typed helper：
    - `river_cloud_xiaozhi_playback_supply_engages_lane(...)`
    - `river_cloud_xiaozhi_playback_lane_engaged_from_truth_view(...)`
    - `river_cloud_xiaozhi_playback_turn_active_from_truth_view(...)`
  - `river_cloud_xiaozhi_playback_lane_engaged()` 不再直接读取：
    - `phase != IDLE`
  - `river_cloud_xiaozhi_playback_turn_active()` 不再通过
    `lane_engaged <- phase` 间接重建 turn 语义
  - `river_cloud_xiaozhi_capture_held_by_playback(...)` 与
    `river_cloud_xiaozhi_fill_playback_runtime_snapshot(...)` 也统一改读同一套
    typed helper，不再在导出路径上回退到 `phase != IDLE`
  - 这一步继续把：
    - cloud runtime 负责导出 lane / turn / capture-held 所需的 typed playback truth
    - dialog runtime 只镜像导出的 truth
    - voice/runtime policy 只消费镜像后的 truth
    这条职责边界压实
- `Step 5.420`
  - `dialog runtime` 继续去掉内部派生逻辑对 coarse playback phase 的直接依赖
  - `river_dialog_runtime_playback_projection_t` 现在显式镜像：
    - `supply_kind`
  - `river_dialog_runtime_playback_waiting_segment_from_projection(...)` 不再读取
    `phase_kind == WAITING_SEGMENT`，而是直接消费：
    - `playback_supply_kind == WAITING_NEXT_SEGMENT`
  - `river_dialog_runtime_playback_turn_retains_output_turn_from_projection(...)`
    也不再用 `phase_kind == REBUFFERING` 判断是否保留 output turn
  - 现在改为复用 typed helper：
    - `river_dialog_runtime_playback_turn_recovering_from_projection(...)`
    - 读取：
      - `rebuffer_pending`
      - `backend_state_kind == OWNED_RECOVERING`
      - `backend_state_kind == RESTART_PENDING`
  - 同时保留原有保守兜底：
    - `phase_unknown && !cloud_runtime_available`
    仍走本地 shadow fallback，避免 cloud snapshot 缺席时引入行为回归
  - 这一步继续把 `dialog runtime` 从“根据 phase 重建 waiting/recovering 语义”
    推进到“只消费 playback runtime 已导出的 typed truth”
- `Step 5.419`
  - playback runtime 继续把 detached quiet-window 的最后一段 phase fallback
    替换成 typed supply truth
  - 新增公开枚举：
    - `river_cloud_playback_supply_kind_t`
    - `river_cloud_playback_supply_kind_name(...)`
  - `river_cloud_runtime_snapshot_t` 与 `river_dialog_runtime_snapshot_t` 现在都显式导出：
    - `playback_supply_kind`
  - `dialog runtime` 的 cloud import / playback facts / exported snapshot 也同步镜像
    了这条 supply truth，`dialog_runtime dump` 新增 `supply=...`
  - `river_cloud_xiaozhi_playback_quiet_window_from_gate_view()` 在
    `BACKEND_DETACHED` 场景下不再依赖：
    - `PREFETCHING`
    - `REBUFFERING`
    - `WAITING_SEGMENT`
    这些 coarse phase
  - 它现在改为直接读取：
    - `CURRENT_SEGMENT`
    - `WAITING_NEXT_SEGMENT`
    - `TERMINAL_TAIL`
    这些 typed supply truth 来识别 detached 的静默窗口
  - `river_voice_runtime_dialog_playback_quiet_window()` 也同步改成读取
    dialog snapshot 的 `playback_supply_kind`，去掉 detached quiet-window 的
    phase 兜底
  - 这一步继续把：
    - playback runtime 负责导出供给语义
    - dialog runtime 只镜像这条真相
    - voice runtime 只消费镜像后的 typed truth
    这条边界压实，进一步减少 quiet-window / restart-pending 对 coarse phase 的常态依赖
- `Step 5.418`
  - XiaoZhi downlink / playback 继续把 `quiet_window` / `capture_held` 从 coarse
    playback phase 收口到 typed playback truth
  - playback runtime 新增：
    - `river_cloud_xiaozhi_playback_gate_view_t`
    - `river_cloud_xiaozhi_capture_playback_gate_view(...)`
    - `river_cloud_xiaozhi_playback_quiet_window_from_gate_view(...)`
  - `playback_quiet_window_allows_vad_open()` 现在优先读取：
    - `backend_state`
    - `hold_kind`
    - `terminal_wait_kind`
    - `tts_stop_pending`
    - `output_active`
    而不再直接把整类 `PREFETCHING/REBUFFERING/WAITING_SEGMENT` 全部视为静默窗口
  - `capture_held_by_playback(...)` 也改为复用同一 gate truth，避免 adapter 再次
    自己重建 quiet-window 语义
  - `river_voice_runtime_restart_pending_requires_block(...)` 现在同步改成基于
    dialog snapshot 的 typed playback truth 做判定，不再依赖独立的 coarse
    `restart_pending_quiet_phase(...)`
  - 当前仅在 `DETACHED` 的残余过渡态继续保留 phase 兜底，这是因为 supply-side
    waiting truth 还没有完全显式导出；后续可以继续把这部分 phase fallback 也替换掉
  - 这一步继续把：
    - cloud runtime 的 quiet-window
    - capture-held during playback
    - voice runtime 的 restart-pending AEC 阻断
    三处语义收口到同一套 playback truth 上，减少模块间各自重猜播放占用状态
- `Step 5.417`
  - XiaoZhi downlink / playback 继续把 `segment_gap_hold` 从固定阈值收紧为动态低水位
  - 固定的 `1 frame` hold 常量已经移除，当前 hold 阈值改由
    `river_cloud_xiaozhi_downlink_segment_gap_hold_frames()` 统一派生：
    - `attached_resume_threshold_frames - 1`
    - 再受 `starved_low_water_frames` 约束
    - 至少保持 `1 frame`
  - 这让 worker 能在“仍低于 attached resume 门槛、但已接近尾部 underrun 风险”的
    区间里更早进入 segment-gap hold，而不是等到只剩固定 `1 frame`
  - 同时 hold 阈值始终尽量低于 attached resume 门槛，避免 hold 之后下一轮立刻
    满足 resume 条件而自解
  - 这一步继续把段间恢复从硬编码 `1 frame` 猜测推进成依赖 runtime
    start/resume 真相的动态 low-water hold
- `Step 5.416`
  - XiaoZhi downlink / playback 继续收窄 `segment_gap_hold` 的破坏边界
  - `river_cloud_xiaozhi_hold_playback_for_segment_gap(...)` 的 attached 路径现在不再
    走 destructive `river_playback_service_flush_stream_ex(...)`，而是改为优先
    `river_playback_service_recover_stream_ex(...)`
  - 这继续保留现有的 attached hold / `OWNED_PAUSED -> maybe_resume_paused_playback()`
    恢复链，但不再为常态的 next-segment 等待重置 reference/AEC 历史
  - hold 日志也同步改为：
    - `attached_recover`
    - `detached_stop`
  - 这一步继续把段间等待从“借 flush 实现暂停”收紧到“轻量 recover 实现 attached
    hold”，减少段间抖动时 reference/AEC 被不必要打断的概率
- `Step 5.415`
  - XiaoZhi downlink / playback 继续收紧纯 `write_failed` 的瞬时恢复延迟
  - 当 cause 仍是 `WRITE_FAILED` 且 recovery=`service_recover` 时，worker 现在会在
    inline `river_playback_service_recover_stream_ex(...)` 成功后，同一轮立即重写
    当前 frame，而不是留到下一次 poll 再通过 `retry_valid` 重放
  - `xiaozhi_downlink_retry_valid` 现在只在真正进入 recover fallback / inline replay
    fallback / 非 inline rebuffer 时才置位；recover 成功且即时重写成功的路径不再污染
    retry / rebuffer 状态
  - 新增诊断日志：
    - `xiaozhi playback inline recover replay succeeded`
    - `xiaozhi playback rebuffer requested after inline replay fallback`
  - 这一步继续把 downlink write-fail recovery 从“recover 成功但仍要再等一轮 worker”
    收紧到“recover 成功即刻重写当前帧”，进一步缩小一次 poll 周期带来的播放卡顿窗口，
    也减少了瞬时写失败把后续 start-gate / streak 错抬高的概率
- `Step 5.414`
  - XiaoZhi downlink / playback 继续拆分 `write_failed` 的 recover 与 rebuffer
    语义
  - 纯 `WRITE_FAILED` 且 recovery=`service_recover` 的路径现在不再立刻：
    - `note_playback_rebuffer(...)`
    - 进入 `rebuffer_pending`
    - 累加 `rebuffer_streak`
  - worker 现在先尝试 inline `river_playback_service_recover_stream_ex(...)`；
    只有 recover 失败并回退到 `stop_rebuffer` 时，才正式登记 rebuffer
  - 新增诊断日志：
    - `xiaozhi playback service recover requested`
    - `xiaozhi playback rebuffer requested after recover fallback`
  - 这一步继续把 downlink write-fail recovery 从“瞬时写失败立即升级成 rebuffer”
    收紧到“先尝试 service recover，只有 recover 失败才进入 rebuffer”，降低
    `rebuffer_pending/streak/start_gate` 被误污染的概率
- `Step 5.413`
  - playback service 继续拆分 `flush` / `recover` 的破坏边界：
    - `river_playback_service_restart_started_track_locked(...)`
  - `river_playback_service_flush_locked(...)` 现在继续保留 destructive flush 语义：
    - reset reference
    - restart track
  - `river_playback_service_recover_locked(...)` 不再复用 flush 路径，而是直接重启
    track，不再在 transient `playback_write_failed` recover 成功时清空
    reference/AEC 历史
  - recover 成功后仍回到 `RIVER_PLAYBACK_RUNNING`；如果 restart 失败，则继续保持
    原有回退：
    - `close_locked(true)`
    - `RIVER_PLAYBACK_RESTART_PENDING`
  - 这一步把 downlink write-fail recovery 的 blast radius 从
    “flush track + reset reference” 收紧到“优先仅恢复 track”，为后续继续重建
    rebuffer / recovery policy 提供更稳定的 AEC 连续性
- `Step 5.412`
  - XiaoZhi downlink / playback 继续重建 segment-gap 恢复链：
    - `river_cloud_xiaozhi_segment_gap_hold_view_t`
  - `river_cloud_xiaozhi_maybe_pause_for_segment_gap()` 现在不再等到
    `queued_frames == 0` 才触发，而是先捕获 `truth_view + queued_frames`，
    当 supply 已进入 `WAITING_NEXT_SEGMENT` 且 queued 只剩 1 帧时，提前执行
    segment-gap hold
  - worker 主循环现在在 zero-queue 兜底前先处理低水位 hold，避免硬件先
    underrun 再进入 `write_failed / rebuffer`
  - 这一步把 segment-gap 恢复从“硬件先 underrun、再 write_failed/rebuffer”
    前移到“接近队尾时主动挂起”，减少一个放大抖动的卡顿入口，为后续继续重建
    downlink/playback 恢复路径提供更稳定的恢复边界
- `Step 5.411`
  - dialog runtime 继续把 residual control / derived state 从 exported snapshot
    中剥离：
    - `river_dialog_runtime_control_facts_t`
    - `river_dialog_runtime_derived_facts_t`
    - `river_dialog_runtime_export_control_facts_to_snapshot_locked(...)`
    - `river_dialog_runtime_export_derived_facts_to_snapshot_locked(...)`
  - 以下 residual dialog state 现在改由 internal facts 持有，再统一镜像到
    exported snapshot：
    - `boot_ready / wake_confirmed / asr_session_active`
    - `wake_admission_pending / tts_interrupt_requested`
    - `error_recovering / error_kind`
    - `playback_active / playback_recovering / playback_owner_kind`
    - `interaction_state / transition_count / reason`
  - `capture_playback_projection_locked(...)` 与
    `capture_interaction_projection_locked(...)` 现在改读 internal
    control/derived facts，不再把 exported snapshot 当作内部 dialog state 真相源
  - 这一步把 dialog runtime 内部 reducer / projection / publish 对 exported
    snapshot 的依赖进一步压缩到 export/get/dump 边界，为后续继续重建
    downlink/playback 恢复路径提供稳定的单一 dialog 真相源
- `Step 5.410`
  - dialog runtime 继续把 turn/session/terminal metadata 从 exported snapshot 中剥离：
    - `river_dialog_runtime_cloud_session_facts_t`
    - `g_river_dialog_runtime.cloud_session_facts`
    - `river_dialog_runtime_export_session_facts_to_snapshot_locked(...)`
  - `apply_cloud_event_locked(...)` 里的 `sid` 更新现在改为写入 internal
    session facts，不再直接修改 exported snapshot
  - `import_cloud_snapshot_locked(...)` 现在先写 internal session facts，再统一镜像到
    exported snapshot
  - 这一步继续把 dialog runtime 推进成唯一真相源，进一步减少
    “修改 exported snapshot 就是在修改内部真相” 的残留路径，为后续继续剥离
    boot/wake/asr/error 等 residual state 做准备
- `Step 5.409`
  - dialog runtime 继续把 interaction/raw facts 从 exported snapshot 中剥离：
    - `river_dialog_runtime_cloud_round_facts_t`
    - `river_dialog_runtime_cloud_io_facts_t`
    - `river_dialog_runtime_export_round_facts_to_snapshot_locked(...)`
    - `river_dialog_runtime_export_io_facts_to_snapshot_locked(...)`
  - `capture_interaction_projection_locked(...)` 现在改为直接读取 internal
    `cloud_round_facts` 与 `cloud_io_facts`，不再从 exported snapshot 的
    `conversation/window/cloud_*` 与 `input/output_lane` 原始字段散读
  - `capture_playback_projection_locked(...)` 的 `output_lane` 现在也改为读取
    internal `cloud_io_facts`
  - `import_cloud_snapshot_locked(...)` 现在先写 round/io facts，再统一镜像到
    exported snapshot
  - 这一步继续把 dialog runtime 推进成唯一真相源，进一步明确
    “内部 round/io raw facts” 与 “对外 snapshot export” 的分层，为后续继续剥离
    turn/session metadata 做准备
- `Step 5.408`
  - dialog runtime 开始把内部 playback raw facts 与 exported snapshot 拆层：
    - `river_dialog_runtime_cloud_playback_facts_t`
    - `g_river_dialog_runtime.cloud_playback_facts`
    - `river_dialog_runtime_export_playback_facts_to_snapshot_locked(...)`
  - `capture_playback_projection_locked(...)` 现在改为直接读取 internal
    playback facts，而不再从 `snapshot.playback_*` 原始字段散读
  - `import_cloud_snapshot_locked(...)` 现在先写入 internal playback facts，
    再统一镜像到 exported snapshot
  - 这一步继续把 dialog runtime 推进成唯一真相源，开始把
    “内部 raw playback facts” 与 “对外 snapshot export” 显式拆层，为后续继续剥离
    turn/input/output raw facts 做准备
- `Step 5.407`
  - dialog runtime 继续把入口层收口成统一的 dialog-owned ingress reducer：
    - `river_dialog_runtime_local_playback_import_t`
    - `river_dialog_runtime_ingress_t`
    - `river_dialog_runtime_capture_local_playback_import(...)`
    - `river_dialog_runtime_commit_ingress(...)`
  - `river_dialog_runtime_commit_cloud_event(...)` /
    `river_dialog_runtime_sync_cloud_state(...)` /
    `river_dialog_runtime_reduce_local_playback_event(...)`
    现在都只负责组装 dialog-owned ingress，再走同一条 reducer 提交路径
  - playback listener 输入不再把外部
    `river_playback_stream_config_t` 直接带进 reducer 提交逻辑，而是先复制为
    dialog-owned `local_playback_import`
  - 统一 ingress 保持既有 reducer 顺序不变：
    - cloud-event：apply event -> import cloud -> reconcile -> finalize
    - local-playback：resolve ownership -> import cloud -> apply local shadow ->
      reconcile -> finalize
  - 这一步继续把 dialog runtime 推进成唯一 dialog 真相源，进一步压缩入口层对
    callback / adapter 外部结构的直接依赖，为后续继续拆 raw-fact 与 derived-truth
    边界做准备
- `Step 5.406`
  - dialog runtime 继续把 cloud ingress 收口成 dialog-owned import carrier：
    - `river_dialog_runtime_cloud_import_t`
    - `river_dialog_runtime_capture_cloud_import(...)`
  - `river_dialog_runtime_capture_cloud_import(...)` 现在是 dialog runtime 内
    唯一直接接触 `river_cloud_runtime_snapshot_t` 的入口；它会把 adapter runtime
    snapshot 复制为 dialog 自己拥有的 raw-fact 载体
  - `import_cloud_snapshot_locked(...)` 现在改为只消费
    `river_dialog_runtime_cloud_import_t`，不再把
    `river_cloud_runtime_snapshot_t` 直接作为 reducer 输入
  - `commit_cloud_event(...)` / `sync_cloud_state(...)` /
    `reduce_local_playback_event(...)` 现在统一显式走：
    - capture cloud import
    - import facts
    - reconcile facts
    - finalize commit
  - 这一步继续把 dialog runtime 推进成唯一 dialog 真相源，进一步切断 reducer
    与 cloud adapter export snapshot 结构之间的直接耦合，为下一步继续收紧
    typed ingress / reducer 分层做准备
- `Step 5.405`
  - dialog runtime 已把 cloud snapshot 路径从单个混合 helper 拆成显式两阶段：
    - `import_cloud_snapshot_locked(...)`
    - `reconcile_facts_locked(...)`
  - `import_cloud_snapshot_locked(...)` 现在只负责导入 cloud raw facts：
    - availability
    - round/window/playback raw fields
    - input/output lane text 与 parsed lane
  - `reconcile_facts_locked(...)` 现在统一负责 dialog 侧收敛：
    - clear local playback error shadow when cloud runtime is available
    - refresh error truth
    - latch cloud-round-backed `asr_session_active`
    - refresh playback truth
    - clear quiesced `tts_interrupt_requested`
  - `commit_cloud_event(...)` / `sync_cloud_state(...)` /
    `reduce_local_playback_event(...)` 现在都显式走：
    - import facts
    - reconcile facts
    - finalize commit
  - 这一步继续把 dialog runtime 入口推进成更清楚的 reducer 管线，为下一步把
    raw cloud snapshot 再收成更明确的 dialog import carrier 做准备
- `Step 5.404`
  - dialog runtime 现在为 cloud event / cloud sync / local playback reducer
    引入统一的 `commit checkpoint + commit policy`
  - 新增：
    - `river_dialog_runtime_commit_policy_t`
    - `river_dialog_runtime_commit_checkpoint_t`
  - 新增统一辅助层：
    - `capture_commit_checkpoint_locked(...)`
    - `commit_checkpoint_changed(...)`
    - `finalize_commit_locked(...)`
  - `commit_cloud_event(...)` 与 `sync_cloud_state(...)` 现在都改为通过同一
    `finalize_commit_locked(...)` 完成提交
  - `reduce_local_playback_event(...)` 不再单独手工保存：
    - `prev_playback_active`
    - `prev_playback_recovering`
    - `prev_error_recovering`
    - `prev_interaction_state`
    再在尾部拼接专属 publish gating
  - 这一步开始把 dialog runtime 的 reducer/commit 边界也推进成
    “single checkpoint -> single publish policy”，为下一步继续拆
    cloud import / reducer / publish 分层做准备
- `Step 5.403`
  - dialog runtime 继续为输入侧 / 会话侧派生引入内部
    `interaction_projection`
  - `capture_interaction_projection_locked(...)` 会一次性锁存：
    - boot / error / asr / wake / window
    - cloud close / listen-stop
    - `tts_interrupt_requested`
    - input/output lane
    - 当前 interaction state
    - 复用的 playback projection
  - 以下派生现在开始复用同一份 interaction projection，而不再散读
    `g_river_dialog_runtime.snapshot`：
    - `cloud_round_active`
    - `tts_interrupt_inflight`
    - `interaction_state`
    - `wakeword_block_reason`
    - `allows_barge_in_interrupt`
  - 这一步开始把 dialog runtime 输入侧 / 会话侧真相也推进成
    “single interaction projection -> admission / interaction truth”
- `Step 5.402`
  - dialog runtime 现在为 playback 派生引入内部
    `playback_projection`
  - `capture_playback_projection_locked(...)` 会一次性锁存 dialog runtime 当前计算
    playback truth 所需的投影输入，包括：
    - cloud playback facts
    - backend / phase / hold / terminal wait
    - local playback shadow fallback
    - output lane
  - 以下派生现在都开始复用同一份 projection，而不再散读
    `g_river_dialog_runtime.snapshot`：
    - `playback_owner_kind`
    - `playback_recovering`
    - `playback_active`
    - `output_turn_engaged`
    - `output_turn_quiesced`
  - 这一步开始把 dialog runtime 的 playback truth 从“多字段散读派生”
    收口成“single projection -> playback/output truth”
- `Step 5.401`
  - XiaoZhi playback runtime 继续把 control-path 收口到显式
    `playback_truth_view`
  - 以下边界现在都改为先捕获 truth-view，再消费同一份
    `backend/phase/hold/output_active` 真相：
    - `apply_transport_reset_playback_policy()`
    - `apply_session_start_playback_policy()`
    - `playback_note_duplex_ready()`
    - `reset_playback_state()`
    - `playback_check_pending_stop()`
    - `playback_abort_for_cause()`
    - `start_playback_if_needed()`
  - `playback_abort` 日志现在也直接打印 truth-view 里的
    `phase/hold/backend`
  - 这一步继续减少 control-path 与 worker-path 对 playback backend 的分叉判定
- `Step 5.400`
  - XiaoZhi playback runtime 继续把观测面收口到显式 `playback_truth_view`
  - `playback_truth_view` 现在额外派生 `hold_kind`，使执行面和观测面共用同一份
    phase/backend/hold/supply 真相
  - `dump_playback_status()` 现在改为一次性捕获 truth-view，并在
    `playback_terminal` / `downlink` 诊断日志里复用：
    - `phase`
    - `hold`
    - `backend`
    - `supply`
  - `fill_playback_runtime_snapshot()` 现在也直接消费同一份 truth-view，而不再
    单独重建 backend-source
  - 这一步继续减少“执行面按一套 truth 决策、诊断面按另一套 helper 读取”的漂移
- `Step 5.399`
  - XiaoZhi playback runtime 继续把 downlink worker 的核心分支收口到显式
    `playback_truth_view`
  - `maybe_resume_paused_playback()` 现在改为消费已捕获的 truth-view，而不再
    在函数内部重新读取 `phase/backend`
  - downlink worker 主循环在单次决策窗口内开始复用同一份 truth-view，用于：
    - `rebuffer_resume_ready`
    - `tts_stop_pending`
    - `paused -> resume`
    - `needs_start / recovering / start_threshold`
  - `playback write failed -> rebuffer` 路径现在也复用同一份 truth-view 来驱动：
    - `supply_kind`
    - `phase/backend` 诊断日志
    - recovery path 选择
  - 这一步继续把 downlink worker 从“循环内多 helper 重读”推进成
    “single worker decision -> single truth-view”
- `Step 5.398`
  - XiaoZhi playback runtime 现在为 recovery 关键分支引入显式
    `playback_truth_view`
  - `capture_playback_truth_view(...)` 会一次性锁存并派生：
    - `backend_source`
    - `supply_source`
    - `backend_state`
    - `supply_kind`
    - `output_active`
  - `maybe_pause_for_segment_gap()` 与 `maybe_rebuffer_starved()` 现在都开始复用
    同一份 truth-view，而不再各自分别重读 `phase` / `backend` / `supply`
  - `segment_gap_hold` 关键日志也改成直接使用 truth-view 里的
    `phase/backend`
  - 这一步不改变 recovery 判定结论，只继续把
    `segment_gap_hold / upstream_starved_rebuffer` 推进成
    “显式 truth-view -> recovery decision”的单向派生
- `Step 5.397`
  - XiaoZhi playback runtime 现在为 supply 判定引入显式
    `playback_supply_source`
  - `capture_playback_supply_source(...)` 会一次性锁存：
    - `wait_context_valid`
    - `last_segment_observed`
    - `segment_count`
  - `compute_playback_waiting_next_segment_from_source(...)` 与
    `compute_playback_supply_kind_from_source(...)` 现在只消费这份 source，
    不再在 supply 判定里分别重读 wait-context / segment_count / terminal-tail
  - 以下路径开始复用同一份 supply-source：
    - `playback_waiting_next_segment()`
    - `playback_supply_kind()`
    - `capture_playback_phase_source()`
  - 这一步不改变 `CURRENT_SEGMENT / WAITING_NEXT_SEGMENT / TERMINAL_TAIL`
    的判定结论，只继续把 playback supply 推进成
    “显式 supply source -> waiting/supply truth”的单向派生
- `Step 5.396`
  - XiaoZhi playback runtime 现在为 backend 派生引入显式
    `playback_backend_source`
  - `capture_playback_backend_source(...)` 会一次性锁存：
    - `service_view.state`
    - `service_view.active`
    - `service_view.owned_stream`
    - `phase`
  - `compute_playback_backend_state_from_source(...)` 现在只消费这份 source，
    不再在 backend 派生过程中分别读取 service stats 与 phase
  - 关键消费点也开始复用同一份 source：
    - `playback_backend_state()`
    - `playback_output_active()`
    - `playback_hold_kind()`
    - `fill_playback_runtime_snapshot()`
  - `tts_start` / `playback_started` fallback 关键日志也改成复用同一份
    phase/backend source，减少 phase/backend 双读导致的诊断读偏斜
  - 这一步不改变 backend / output-active / hold 判定结论，只继续把
    playback backend 推进成“显式 backend source -> backend truth”的单向派生
- `Step 5.395`
  - XiaoZhi playback runtime 现在为 phase 派生引入显式
    `playback_phase_source`
  - `capture_playback_phase_source(...)` 会一次性锁存：
    - `stop_pending`
    - `rebuffer_pending`
    - `physical_active`
    - `queued_frames`
    - `segment_count`
    - `waiting_next_segment`
  - `compute_playback_phase_from_source(...)` 现在只消费这份 source，不再在
    phase 派生过程中散落读取全局态
  - `refresh_playback_phase(...)` 也直接复用同一份 source 打日志，观测面新增：
    - `segments`
    - `wait_next`
  - 这一步不改变 phase 判定结论，只继续把 playback phase 推进成
    “显式 physical + queue/segment source -> phase”的单向派生
- `Step 5.394`
  - XiaoZhi playback runtime 现在把本文件内 residual 的
    `xiaozhi_playback_active` 读取统一收口到
    `river_cloud_xiaozhi_playback_physical_active()`
  - `compute_playback_phase()` 与 `arm_playback_stop()` 现在都显式消费这份
    named physical truth，而不再裸读 shadow 成员
  - `playback phase`、`interrupt hint`、`hint-only endpoint`、`duplex dump`
    诊断日志现在把字段明确标成：
    - `playback_physical`
    并补齐：
    - `phase`
    - `backend`
  - 这一步不改变 phase / backend 判定逻辑本身，只继续把 playback runtime
    内“物理播放中”和“语义播放占用中”拆开命名，减少板端日志再把单个
    `playback=yes/no` 字段误当成 semantic truth
- `Step 5.393`
  - XiaoZhi playback runtime 的两处策略层判断：
    - `playback_has_work()`
    - `segment_prefetch_target_needed()`
    现在不再直接消费
    `g_river_cloud.xiaozhi_playback_active`
  - 两者现在统一改为依赖
    `river_cloud_xiaozhi_playback_output_active()`
  - 这意味着：
    - downlink 唤醒条件
    - segment predictive prefetch 的抑制条件
    开始统一跟随 runtime `phase + backend` typed truth
  - 这一步继续把 playback runtime 的策略层从 coarse shadow bool 收口到
    semantic output-active truth，降低 foreign/recovering/non-owned backend
    窗口对本流 start/prefetch 策略的污染
- `Step 5.392`
  - XiaoZhi playback runtime 的两个 recovery 分支：
    - `maybe_pause_for_segment_gap()`
    - `maybe_rebuffer_starved()`
    现在不再直接消费
    `g_river_cloud.xiaozhi_playback_active`
  - 两者现在统一改为依赖
    `river_cloud_xiaozhi_playback_output_active()`
  - 这意味着 playback recovery 的触发前提开始显式跟随：
    - runtime `phase`
    - typed `backend_state`
    而不是继续跟随局部影子布尔
  - 这一步继续把 downlink/playback recovery 的真相边界收口到
    runtime-owned semantic truth，减少恢复空窗里因为 shadow bool 抖动带来的
    hold / rebuffer 分支误跳过
- `Step 5.391`
  - `voice runtime` 的 `restart_pending` hard block 现在优先消费
    `dialog runtime` 导出的 typed backend truth
  - `river_voice_runtime_restart_pending_requires_block(...)` 现在只在
    dialog snapshot 缺失时，才回退使用 raw
    `playback_state == RIVER_PLAYBACK_RESTART_PENDING`
  - 一旦 dialog snapshot 在场，就直接按 snapshot 的：
    - cloud owner
    - backend=`restart_pending`
    - quiet-phase 过滤
    决定是否继续 hard block
  - 这一步继续把 AEC restart gate 从“先看物理 playback service state”
    推进到“先看 dialog/runtime backend 真相，raw state 仅做缺失兜底”
- `Step 5.390`
  - `voice runtime` 在 `uses_native_capture_ref` 且 native reference 尚未可用时，
    不再重新读取 raw `playback_state_active()` 去区分：
    - `ref_idle`
    - `ref_missing`
  - 现在只要前面的 AEC playback gate 已经接受当前路径，就统一把这类空窗解释成：
    - `RIVER_VOICE_REFERENCE_ACTIVITY_IDLE`
  - 这一步不改 duplex ready 的 ready/not-ready 结论，只收紧 reference
    activity 的语义边界，避免在评估尾部再次从物理 playback state 反推语义
- `Step 5.389`
  - `voice runtime` 的 AEC/duplex 评估结构现在显式携带 `dialog runtime`
    导出的 typed truth：
    - `playback_owner_kind`
    - `error_kind`
  - `river_voice_runtime_aec_gate_eval_base(...)` 在抓取 dialog snapshot 时，
    会同步锁存这两项并继续透传到 `river_voice_runtime_duplex_ready_eval(...)`
  - `preproc` 的 `webrtc_aecm gate=...` 迁移日志现在直接打印：
    - raw `playback_state`
    - typed `playback_owner_kind`
    - typed `error_kind`
  - XiaoZhi duplex/fallback 关键日志也同步补齐这三项
  - 这一步先不继续改 gate 判定逻辑，而是先把 AEC/preproc/duplex 的诊断面与
    `dialog runtime` typed truth 对齐，为后续继续收紧 residual raw
    playback-state 语义依赖提供板端可观测基础
- `Step 5.388`
  - voice runtime 现在开始显式消费 `dialog runtime` 导出的
    `playback_owner_kind`
  - dialog snapshot 兜底保持 playback engaged 的 helper 现在明确只接受：
    - `playback_owner_kind == CLOUD`
    - 且仍有：
      - `playback_lane_engaged`
      - 或 `playback_recovering`
      - 或 `playback_turn_active`
  - dedicated `restart_pending` hard block 与 generic dialog-playback
    fallback 现在都复用这条显式 cloud-owner 判定
  - 本地 playback-service state 继续保留为“物理播放是否存在”的底层真相；
    这一步只把 dialog snapshot fallback 从隐式布尔组合推进成 typed
    ownership 消费
- `Step 5.387`
  - `dialog runtime` 现在正式把 playback ownership truth 外显成 snapshot
    级别的 `playback_owner_kind`
  - 新增 `river_dialog_playback_owner_kind_t`：
    - `NONE`
    - `CLOUD`
    - `LOCAL_FALLBACK`
  - `refresh_playback_locked()` 现在统一同时派生：
    - `playback_active`
    - `playback_recovering`
    - `playback_owner_kind`
  - `dialog_runtime_dump_status()` 也已新增：
    - `owner=<kind>`
  - 这一步继续把 `dialog runtime` 从“依赖外部反推当前由谁驱动 playback
    truth”推进成“直接导出 typed playback ownership truth”
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

- 继续把当前仍直接暴露为 `river_cloud_runtime_snapshot_t` 的 raw cloud 输入，
  收成更明确的 dialog import carrier，减少 dialog runtime 长期直接依赖
  transport/export snapshot 结构
- 继续检查 `commit_cloud_event(...)` / `sync_cloud_state(...)` /
  `reduce_local_playback_event(...)` 是否还能进一步共用更少的 reducer 入口，
  让入口函数只保留：
  - 写入 facts
  - 选择 commit policy
- 继续把 `dialog runtime` 里的 cloud import / reconcile / commit 三层边界压实：
  - 让 raw import carrier 更稳定
  - 让 reconcile helper 只负责 dialog truth 收敛
  - 让 commit helper 只负责 publish policy
- 继续检查 `dialog runtime` 内是否还能把 cloud-event / cloud-sync /
  local-playback 进一步收口成更少的 typed reducer，减少：
  - stale cloud snapshot
  - local edge 抢跑 aggregate 派生
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
