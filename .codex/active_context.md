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
  - `5.182 playback recovering vs fatal fault split`
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
    - dialog runtime ingress now only treats `RIVER_PLAYBACK_ERROR` as fatal
      `error_recovering`, while recoverable write churn stays on the
      `playback_recovering` path
    - this gives the runtime truth source a real semantic split between
      recoverable rebuffer/restart churn and hard local playback faults
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
  - finish rebuilding terminal ACK completion and last-segment close semantics
    on top of the new recoverable playback state split
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
