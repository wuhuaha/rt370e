# Codex Active Context

This file is the canonical volatile context for Codex-facing work in this
repository. Update it when the working branch, active objective, SDK baseline,
or top-of-tree verification target changes.

## Active Working Set

- Current working branch: `xiaozhi-client`
- Active SDK baseline: `/root/ameba-rtos`
- Active build command:
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'`
- Active flash command:
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 /root/ameba-river/tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'`
- Active monitor command:
  - `python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000`
- Latest landed step:
  - `Step H.xiaozhi-client.6 收敛 Orvibo 协议控制帧失败恢复（SDK 构建通过）`
- Current active objective:
  - Rebuild this branch as an Orvibo voice client mainline. The first external wire contract remains XiaoZhi-compatible, but code/file/function naming and runtime ownership are Orvibo-owned.
- Active plan:
  - `doc/ORVIBO_CLIENT_REARCH_EXECUTION_PLAN_ZH.md`
- Active plan index:
  - `.codex/active_plans.md`

## Current Architecture

- `app/` only boots `river_orvibo_app_boot()`.
- `components/river_core/` contains Orvibo app orchestration and the Orvibo state machine.
- `components/river_cloud/` contains Orvibo protocol/MCP transport plus shared Wi-Fi/WebSocket/Opus helpers.
- `components/river_voice/` contains current capture, preproc, Silero VAD, KWS, AEC/BF, playback, and reference services.
- `components/river_diag/` exposes Orvibo, audio, playback, KWS tensor dump, and KWS alignment diagnostics.

## Protected Implementation Boundaries

- Preserve current Silero VAD implementation:
  - `include/river/river_voice_detector.h`
  - `components/river_voice/river_voice_detector.c`
  - `components/river_voice/river_voice_detector_silero.cc`
  - `components/river_voice/generated/river_silero_vad_model_data.*`
- Preserve current wake-word implementation and board/local parity tooling:
  - `include/river/river_voice_kws.h`
  - `components/river_voice/river_voice_kws.cc`
  - `components/river_voice/generated/*kws*`
  - KWS tensor dump, chunk dump, alignment replay, feature/input/output comparison paths
- Preserve current local preproc/AEC/BF implementation unless a future step provides an equal or stronger replacement:
  - `components/river_voice/river_voice_preproc*.c`
  - `components/river_voice/river_voice_webrtc_aecm_adapter.*`
  - `third_party/webrtc_aecm/`

## Latest Verified Slice

- `Step H.xiaozhi-client.6` hardens Orvibo protocol control-frame failures:
  - wake detected, listen start, listen stop, and abort speaking actions now record protocol send status.
  - automatic state-machine control failures update `last_error`, increment `protocol_control_fail`, and post recoverable error events.
  - diagnostic/manual listen and abort commands record failures without forcing app recovery.
  - app status reports `protocol_ctrl=ok/fail` for board-side validation.
- `Step H.xiaozhi-client.5` separates Orvibo app control and audio queues:
  - state/connect/listen/abort control messages no longer share capacity with uplink/downlink audio packets.
  - app task drains control first, then audio with a bounded per-tick budget.
  - audio queue uses drop-oldest on pressure, preserving control-event reachability during TTS/downlink bursts.
  - app status reports `ctl_q`, `aud_q`, control/audio posted/fail, and `aud_drop_oldest`.
- `Step H.xiaozhi-client.4` hardens uplink backpressure and diagnostics:
  - `river_orvibo_protocol_send_audio()` now queues Opus uplink frames instead of synchronously entering the SDK WebSocket send queue.
  - `orvibo_uplink` sender task owns actual `ws_sendBinary()` submission with bounded short retry.
  - each queued frame carries `session_epoch`, so close/reopen cannot leak old-session audio into a new session.
  - protocol status reports uplink task, queue depth, enqueue/drop/retry/fail counters; app status labels the app-side count as `uplink_enq`.
- `Step H.xiaozhi-client.3` hardens TTS/downlink runtime behavior against the XiaoZhi-compatible reference client:
  - server binary downlink audio is only decoded while Orvibo business state is `speaking`.
  - `tts start` resets the downlink decoder and stops stale playback before accepting a new response.
  - `tts stop` waits for a bounded playback drain before returning Orvibo audio mode to listening, then stops playback even on timeout.
  - playback status now exposes SDK buffer occupancy and drain counters for board-side validation.
- `Step H.xiaozhi-client.2` hardens first-boot access against the XiaoZhi-compatible server contract:
  - `activation.code` is treated as a user-binding prompt, not as a direct `/activate` trigger.
  - `/activate` polling is gated by `activation.challenge`, matching the reference no-serial-number flow.
  - access `ready` is now distinct from `websocket_configured`; pending activation prevents opening the audio channel.
  - Wi-Fi ready primes OTA/config immediately, and connected-but-not-ready access retries every 10 seconds so binding completion can be picked up without another wake.
- `Step H.xiaozhi-client.1` makes the Orvibo-owned mainline flash-connectable against the XiaoZhi-compatible server contract:
  - OTA/config POST uses `Activation-Version` / `Device-Id` / `Client-Id` headers and applies server websocket url/token/version.
  - activation polling supports the no-serial-number payload flow used by the reference client.
  - WebSocket open sends auth/protocol/device/client headers, sends hello, and waits for server hello before reporting success.
  - wake/listen/abort/TTS/barge-in state transitions are closed around the Orvibo state machine.
  - MCP remains volume-only (`self.get_device_status`, `self.audio_speaker.set_volume`).
- Current VAD, KWS, KWS tensor dump, alignment replay, and board/local parity paths remain preserved.
- Verification passed:
  - Orvibo/XiaoZhi-compatible protocol/access grep
  - access activation/periodic-refresh grep
  - Orvibo TTS/downlink/playback-drain grep
  - Orvibo uplink queue/session-epoch grep
  - Orvibo app control/audio queue grep
  - Orvibo protocol control failure grep
  - MCP volume-only grep
  - protected VAD/KWS API grep
  - `git diff --check`
  - `python3 tools/diag/check_codex_harness.py`
  - `/root/ameba-rtos` SDK build with `Build done`

## Next Engineering Slice

- Continue Orvibo mainline behavior hardening:
  - protocol error recovery
  - downlink playback/backpressure board validation
  - OTA activation UX/log capture on real board
  - MCP volume-only end-to-end validation on server call
  - wake/listen/speak/barge-in state-machine verification

## Workflow Notes

- Future `git commit` messages in this repository should use clear Chinese descriptions by default.
- Do not edit user-owned review files unless explicitly required:
  - `TIPS.md`
  - `REVIEW.md`
  - `.codex/issues.md`
