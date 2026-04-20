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
  - `5.294 move endpoint_soft_close state machine into playback runtime`
- Latest workflow sync:
  - future `git commit` messages in this repository should use clear Chinese
    descriptions by default
- Latest planning sync:
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
  - newest landed runtime-ownership slice:
    - `endpoint_soft_close` 状态机已并入 playback runtime
    - session 现在只通过 typed helper 读取：
      - pending
      - remaining_ms
      - reason
    - session_close / transport_reset 这类路径也不再手工清空这组字段
  - previous runtime-ownership slice:
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
