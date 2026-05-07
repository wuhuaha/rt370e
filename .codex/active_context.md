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
  - `Step H.xiaozhi-client.16 硬化 Orvibo access 真实设备身份 ready 判定（SDK 构建通过）`
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

- `Step H.xiaozhi-client.16` hardens Orvibo access identity before XiaoZhi-compatible auth:
  - access `ready` now requires both websocket config and a valid refreshed STA MAC identity.
  - `river_orvibo_access_refresh()` refreshes Device-Id/Client-Id from the runtime STA MAC before OTA/config.
  - unavailable or invalid STA MAC records `sta_mac_unavailable` and blocks access refresh instead of using an all-zero identity for OTA/WebSocket auth.
  - access status reports `identity=ready|waiting_mac`.
  - VAD, KWS, KWS parity tooling, AEC, and BF paths are unchanged.
- `Step H.xiaozhi-client.15` adds XiaoZhi-compatible inbound channel timeout handling:
  - protocol tracks the last inbound WebSocket message timestamp.
  - `river_orvibo_protocol_poll()` closes channels that have no inbound messages for 120 seconds and emits `AUDIO_CHANNEL_CLOSED`.
  - protocol status reports `timeout` and `incoming_age=age/limit`.
  - VAD, KWS, KWS parity tooling, AEC, and BF paths are unchanged.
- `Step H.xiaozhi-client.14` aligns Orvibo listening re-entry and abort semantics with the XiaoZhi-compatible reference:
  - `tts_stop` now drains playback, returns to listening, and sends a fresh `listen start` for the next turn.
  - VAD speech-start barge-in sends generic `abort` without a reason, then re-enters listening.
  - wake-word barge-in uses a separate `abort_wake_word` action and sends `reason=wake_word_detected`.
  - speaking + barge-in enabled opens the existing KWS detection gate so current wake-word implementation can participate during TTS.
  - VAD, KWS model/thresholds/parity tooling, AEC, and BF implementations are unchanged.
- `Step H.xiaozhi-client.13` adapts XiaoZhi-compatible TTS downlink audio to an Ameba playback-supported rate:
  - server Opus is still decoded with the sample rate and frame duration from server hello.
  - decoded mono PCM is converted to the selected playback rate before stereo expansion and reference export.
  - 24 kHz server TTS is routed to 48 kHz playback because the current Ameba output policy does not list 24 kHz.
  - audio diag/status exposes `rs=converted/bypass/fail` and `rate=server->playback`.
  - VAD, KWS, KWS parity tooling, AEC, and BF paths are unchanged.
- `Step H.xiaozhi-client.12` removes an SDK-default WebSocket handshake dependency:
  - `RIVER_ORVIBO_WS_SUBPROTOCOL` defaults to `chat`, the subprotocol already accepted by the current XiaoZhi-compatible server.
  - `river_orvibo_protocol_open_audio_channel()` explicitly calls `ws_handshake_header_set_protocol()` before `ws_connect_url()`.
  - the call passes a NUL-inclusive length because the Ameba SDK setter copies bytes without appending a terminator.
  - protocol connect/status logs now show `ws_subprotocol`.
- `Step H.xiaozhi-client.11` expands the Orvibo Opus payload envelope for the current XiaoZhi-compatible server contract:
  - host-side OTA/WebSocket probing reached `wss://api.tenclass.net/xiaozhi/v1/` and received server hello with `opus/24000Hz/1ch/60ms`.
  - protocol binary payload, app audio message payload, and audio-service Opus packet limits are now `1536U`.
  - oversized uplink/downlink packets are counted and logged; status exposes `payload_max`, `audio_max`, `packet_max`, and `oversize=up/down`.
- `Step H.xiaozhi-client.10` adds an app-owned manual access refresh path for board-side binding validation:
  - `river orvibo refresh` posts a control message to the Orvibo app thread.
  - app handling calls the existing access refresh path with reason `diag_refresh`.
  - this allows OTA/config and activation polling to be refreshed immediately after server-side binding without rebooting or waiting for the periodic refresh.
- `Step H.xiaozhi-client.9` hardens Orvibo TTS downlink playback backpressure:
  - TTS playback buffer uses 16 frames to absorb common bursty downlink.
  - each decoded downlink frame checks playback SDK buffer occupancy before writing.
  - frames that would exceed 85% high-water are dropped with `bp_drop` diagnostics instead of blocking the app task.
  - audio status reports backpressure drops, high-water, and playback buffer occupancy.
- `Step H.xiaozhi-client.8` backs off failed Orvibo audio-channel opens:
  - failed `open_audio_channel` schedules capped exponential backoff and keeps one pending wake retry.
  - backoff suppresses immediate repeated open attempts and returns the app through recoverable recovery.
  - retry only posts from `idle` when Wi-Fi is connected and access is ready.
  - successful open or Wi-Fi loss clears the pending retry/backoff state.
- `Step H.xiaozhi-client.7` serializes Orvibo WebSocket poll/send access:
  - protocol transport lock is a recursive mutex so `ws_poll()` callbacks can synchronously send MCP volume replies without self-deadlock.
  - hello wait and steady-state poll call `ws_poll()` through the same transport lock used by uplink sender task.
  - remote WebSocket close records `transport_closed`, increments `close_events`, and still posts `AUDIO_CHANNEL_CLOSED`.
  - protocol status reports `poll` and `close_evt` counters.
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
  - Orvibo WebSocket poll/send serialization grep
  - Orvibo connect retry/backoff grep
  - Orvibo TTS playback backpressure grep
  - Orvibo manual access refresh grep
  - Orvibo Opus payload envelope / oversize diagnostics grep
  - host-side OTA + WebSocket hello probe
  - Orvibo WebSocket subprotocol grep
  - Orvibo downlink playback sample-rate adapter grep
  - Orvibo listen re-entry / abort reason / speaking KWS gate grep
  - Orvibo channel timeout grep
  - Orvibo access identity grep
  - MCP volume-only grep
  - protected VAD/KWS API grep
  - `git diff --check`
  - `python3 tools/diag/check_codex_harness.py`
  - `/root/ameba-rtos` SDK build with `Build done`

## Next Engineering Slice

- Continue Orvibo mainline behavior hardening:
  - OTA activation UX/log capture on real board
  - MCP volume-only end-to-end validation on server call
  - board-side multi-turn wake/listen/speak/barge-in verification

## Workflow Notes

- Future `git commit` messages in this repository should use clear Chinese descriptions by default.
- Do not edit user-owned review files unless explicitly required:
  - `TIPS.md`
  - `REVIEW.md`
  - `.codex/issues.md`
