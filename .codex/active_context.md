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
  - `5.173A supplement XiaoZhi collaboration debug logs`
- Latest workflow sync:
  - future `git commit` messages in this repository should use clear Chinese
    descriptions by default
- Latest planning sync:
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
- Primary active execution plan:
  - `doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md`

## Current Runtime Focus

- Keep the board-side `wake -> VAD/KWS -> native realtime session` path usable.
- The current branch build now defaults to the dedicated duplex experiment
  profile while still preserving runtime fallback gates:
  - `fixed_dsb_webrtc_aecm`
  - native `2mic + ref(ch3)` capture
  - XiaoZhi full-duplex experiment advertisement on
- Replace the old XiaoZhi wire contract with direct `rtos-ws-v0` transport while
  keeping the existing upper cloud state machine temporarily stable.
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
  - `doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md`
- Keep the XiaoZhi stability plan as a supporting baseline while duplex work
  continues:
  - `doc/XIAOZHI_SESSION_STABILITY_EXECUTION_PLAN_ZH.md`
- The next device-side duplex code slices are now explicitly staged as:
  - board regression and log validation of the landed `5.173` / `5.173A`
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
