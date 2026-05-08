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
  - `Step H.xiaozhi-client.28 收紧 Orvibo WebSocket 协议版本输入范围`
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

## Latest Verified Slice

- `Step H.xiaozhi-client.28` tightens Orvibo websocket protocol-version input:
  - reference comparison against `~/xiaozhi-esp32-server` confirmed direct websocket binary messages are treated as raw Opus unless the connection is explicitly from the MQTT gateway path.
  - the current server OTA response emits websocket `url/token` and does not emit `websocket.version`, so Orvibo should stay on the raw/v1 default unless a supported version is explicitly configured.
  - OTA websocket version parsing now accepts only `1..3`; unsupported numeric versions are logged and ignored.
  - `river_orvibo_protocol_set_config()` also rejects unsupported versions before mutating global config, preventing header/hello version from diverging from implemented audio framing.
  - local VAD, wake-word/KWS, tensor dump, alignment replay, board/local parity, and AEC/BF implementation remain unchanged.
- `Step H.xiaozhi-client.27` aligns the default Orvibo WebSocket handshake with XiaoZhi reference clients:
  - reference comparison against `~/xiaozhi-esp32` and `~/py-xiaozhi` confirmed they set auth/protocol/device headers but do not request `Sec-WebSocket-Protocol`.
  - reference comparison against `~/xiaozhi-esp32-server` confirmed the current server does not configure required websocket subprotocols.
  - the Ameba SDK would otherwise inject `Sec-WebSocket-Protocol: chat, superchat` whenever no protocol is explicitly configured.
  - `RIVER_ORVIBO_WS_SUBPROTOCOL` now defaults to an empty string, and a project-side `--wrap=ws_client_handshake` shim omits `Sec-WebSocket-Protocol` when the configured subprotocol is empty.
  - explicitly configured subprotocols remain supported through the existing Orvibo config path; connect/status logs show `ws_subprotocol=-` for the default no-subprotocol state.
  - `build.ninja` and AP image symbol checks confirm `river_ws_handshake.o`, `-Wl,--wrap=ws_client_handshake`, and `__wrap_ws_client_handshake` are present; latest-SDK build passed on `/root/ameba-rtos`.
  - board runtime confirmation remains blocked by the current historical `/dev/ttyUSB0` pure-`0x00` session state.
  - local VAD, wake-word/KWS, tensor dump, alignment replay, board/local parity, and AEC/BF implementation remain unchanged.
- `Step H.xiaozhi-client.26` corrects Orvibo client hello `features.aec` semantics:
  - reference comparison against `~/xiaozhi-esp32` confirmed WebSocket/MQTT hello only sends `features.aec=true` under `CONFIG_USE_SERVER_AEC`.
  - `xiaozhi-esp32` rejects simultaneous device-side AEC and server-side AEC at compile time, so this field represents a server-AEC request rather than local device AEC/BF/native-ref capability.
  - Orvibo hello no longer derives `features.aec` from the active local voice profile; logs now state `server_aec=no`.
  - `listen_start.mode` remains selected from the local voice-profile capability bits, preserving the H.21 listening-mode fix without misreporting local AEC as server AEC.
  - static checks, harness check, and latest-SDK build have all passed on `/root/ameba-rtos`; board runtime confirmation remains blocked by the current historical `/dev/ttyUSB0` pure-`0x00` session state.
  - local VAD, wake-word/KWS, tensor dump, alignment replay, board/local parity, and AEC/BF implementation remain unchanged.
- `Step H.xiaozhi-client.25` converges Orvibo OTA/MCP self-description metadata onto a single source:
  - reference comparison against `~/xiaozhi-esp32`, `~/py-xiaozhi`, and `~/xiaozhi-esp32-server` confirmed the OTA handler prefers request headers such as `device-model` and `application-version`, then falls back to body `board.type` / `application.version`.
  - the current Orvibo branch previously still exported scattered placeholder values such as `application.version=0.1.0`, `board.type=wifi`, and MCP `serverInfo.version=0.1.0`, which could mislead OTA model/version matching and long-term server-side device profiling.
  - a new Orvibo-owned build metadata helper now provides a single source for `app name/version`, `compile_time`, `board name/type`, `chip model`, and `User-Agent`.
  - OTA requests now export the same metadata in both headers and JSON body, including the server-preferred header keys `Device-Model`, `Application-Version`, `Firmware-Version`, and related aliases.
  - MCP initialize now reports the same Orvibo app name/version instead of a stale hard-coded version string.
  - static checks have passed; harness/build verification is the active top-of-tree target on `/root/ameba-rtos`; board runtime confirmation remains blocked by the current historical `/dev/ttyUSB0` pure-`0x00` session state.
  - local VAD, wake-word/KWS, tensor dump, alignment replay, board/local parity, and AEC/BF implementation remain unchanged.
- `Step H.xiaozhi-client.24` aligns static fallback websocket readiness with XiaoZhi-compatible unauthenticated server deployments:
  - reference comparison against `~/xiaozhi-esp32-server` confirmed the server only enforces `Authorization` when `auth.enabled=true`; auth-disabled local deployments legally accept an empty websocket token.
  - the current Orvibo branch previously treated static fallback as usable only when both `ws_url` and `ws_token` were non-empty, which incorrectly blocked explicit local websocket deployments that intentionally disable auth.
  - the fallback websocket URL default is now empty, so the branch no longer silently directs to the old hardcoded endpoint when OTA/config is absent.
  - explicit fallback `ws_url` is now enough to mark static websocket config usable; token remains optional and is only needed for auth-enabled targets.
  - static checks, harness check, and latest-SDK build have all passed on `/root/ameba-rtos`; board runtime confirmation remains blocked by the current historical `/dev/ttyUSB0` pure-`0x00` session state.
  - local VAD, wake-word/KWS, tensor dump, alignment replay, board/local parity, and AEC/BF implementation remain unchanged.
- `Step H.xiaozhi-client.23` aligns the default Orvibo websocket protocol version with the XiaoZhi baseline:
  - reference comparison across `~/xiaozhi-esp32`, `~/py-xiaozhi`, and `~/xiaozhi-esp32-server` confirmed that the live XiaoZhi websocket baseline defaults to protocol version `1`.
  - both the ESP32 and Python reference clients send websocket binary audio as raw Opus packets under version `1`, while the local Python server websocket path forwards inbound bytes directly into Opus/VAD handling without a visible v2/v3 unwrap stage.
  - the current Orvibo branch previously defaulted to protocol version `3`, which risks wrapping uplink Opus in a v3 binary envelope when OTA/config does not explicitly override `websocket.version`.
  - the Orvibo default is now version `1`, so direct websocket interop falls back to raw-Opus framing by default; OTA/config can still override the version if a deployment explicitly returns `2` or `3`.
  - static checks, harness check, and latest-SDK build have all passed on `/root/ameba-rtos`.
  - board runtime confirmation is still blocked by the current historical `/dev/ttyUSB0` pure-`0x00` session state, so raw-Opus first-turn runtime proof is not yet captured on board.
  - local VAD, wake-word/KWS, tensor dump, alignment replay, board/local parity, and AEC/BF implementation remain unchanged.
- `Step H.xiaozhi-client.22` normalizes cloud-facing wake text for XiaoZhi-compatible servers:
  - reference comparison confirmed the current branch preserves a local KWS hit text `小欧管家`, while the target server default wakeup-word handling expects `listen detect.text` values aligned with configured wake words such as `你好小智`.
  - Orvibo app now decouples the protected local KWS text from the cloud-facing `listen detect.text`, using the new configurable `RIVER_ORVIBO_SERVER_WAKE_TEXT` and defaulting it to `你好小智`.
  - static checks, harness check, and latest-SDK build have all passed on `/root/ameba-rtos`.
  - board runtime confirmation is still blocked by the current historical `/dev/ttyUSB0` pure-`0x00` session state, so the expected `server wake detect text` log is not yet captured on board.
  - local VAD, wake-word/KWS, tensor dump, alignment replay, board/local parity, and AEC/BF implementation remain unchanged.
- `Step H.xiaozhi-client.21` aligns client hello/listen-mode semantics with the active Orvibo voice profile:
  - reference comparison confirmed XiaoZhi clients do not hardcode post-wake listening mode; they switch between `auto` and `realtime` based on duplex/AEC capability.
  - Orvibo app now selects `listen_start.mode` from current voice-profile capability bits instead of always sending `auto`.
  - the H.21 attempt to map local AEC/native capture-reference capability into hello `features.aec=true` has been superseded by H.26 because the reference protocol uses that field for server-side AEC requests.
  - static checks, harness check, latest-SDK build, and reflash have all passed on `/root/ameba-rtos`.
  - board runtime confirmation is still blocked by the current historical `/dev/ttyUSB0` pure-`0x00` session state, so the expected `client hello features` / `listen start mode` logs are not yet captured on board.
  - VAD, wake-word/KWS, tensor dump, alignment replay, board/local parity, AEC/BF implementation, Opus framing, and MCP volume-only logic are unchanged.

- `Step H.xiaozhi-client.20` is verified on board after H.19 board validation exposed a TTS/close race:
  - reference behavior checked against `~/xiaozhi-esp32`, `~/py-xiaozhi`, and `~/xiaozhi-esp32-server`.
  - XiaoZhi-compatible servers may close the WebSocket after TTS in `close_after_chat` paths; the client must not treat a closed channel as a protocol-control failure while trying to send another `listen_start`.
  - Orvibo app now guards wake/listen/abort control frames with `river_orvibo_protocol_audio_channel_open()`.
  - if the channel closes before or during control-frame send, Orvibo records `protocol_ctrl skip`, posts `AUDIO_CHANNEL_CLOSED` for state convergence, and avoids recoverable-error escalation.
  - `/dev/ttyUSB0` flash passed; monitor confirmed server hello, TTS playback, close-race skip log, `protocol_ctrl=3/0 skip=1`, and convergence to `idle` without the old `listen_start:-4` recoverable-error path.
  - VAD, wake-word/KWS, tensor dump, alignment replay, board/local parity, AEC/BF, audio codec, and MCP volume-only logic are unchanged.

- `Step H.xiaozhi-client.19` fixes the real-board Orvibo audio task stack overflow:
  - `/dev/ttyUSB0` flash passed with `/root/ameba-rtos`.
  - board monitor confirmed real MAC identity, WebSocket connection, and server hello from `wss://api.tenclass.net/xiaozhi/v1/`.
  - the first wake/listening path then hit `STACK OVERFLOW - TaskName(orvibo_audio)`.
  - `orvibo_audio` task stack is increased from 18 KB to 32 KB and logs now expose `task_stack=`.
  - rebuilt with `/root/ameba-rtos`, reflashed successfully, and validated `mode=listening` with increasing `enc=` counters without another `STACK OVERFLOW`.
  - `river orvibo status` confirmed `task_stack=32768`, real `device_id=8c:bd:37:49:a6:3c`, OTA-derived WebSocket URL, MCP volume-only tools, 24 kHz server audio, and 24 kHz to 48 kHz playback adaptation.
  - follow-up runtime issue observed for a later step: after one server TTS, the WebSocket closed and the app recovered to `idle` instead of re-establishing the next listening channel.

- `Step H.xiaozhi-client.18` aligns the Orvibo runtime with XiaoZhi-compatible server text semantics:
  - `tts sentence_start`、`stt`、`llm` 现在都会进入 Orvibo protocol/app 事件流，而不是只打日志。
  - protocol status 记录最近一次 server text payload，以及 `tts_sentence_rx` / `stt_rx` / `llm_rx` 计数。
  - app status 记录 `last_server_text_kind` / `last_server_text` / `last_server_text_detail`，便于板端直接核对文本与情绪。
  - 未触碰 VAD、KWS、AEC/BF、MCP volume-only、OTA/WS 连接和 TTS 复入边界。
  - 最新 `/root/ameba-rtos` SDK build 已通过。

- `Step H.xiaozhi-client.17` aligns executable project SDK defaults with the branch baseline:
  - `components/river_cloud/CMakeLists.txt`, `tools/river_flash.py`, `tools/generate_rdev.py`, and `env.bat` now default to `/root/ameba-rtos` when `AMEBA_SDK_ROOT` is not explicitly set.
  - `tools/diag/check_codex_harness.py` now checks those active defaults so future Orvibo/XiaoZhi-compatible work cannot silently fall back to `/root/ameba-rtos-1.2`.
  - No protocol, VAD, KWS, KWS parity, AEC, BF, or audio runtime behavior changed.
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
- `Step H.xiaozhi-client.12` first removed dependence on the SDK's implicit `chat, superchat` fallback by adding an explicit Orvibo subprotocol config and diagnostics; this has been superseded by H.27:
  - the default is now no websocket subprotocol, matching XiaoZhi reference clients.
  - the Orvibo config path still supports an explicitly configured `RIVER_ORVIBO_WS_SUBPROTOCOL`.
  - protocol connect/status logs show `ws_subprotocol=-` for the default no-subprotocol state.
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
  - run harness + latest-SDK build for H.25 metadata convergence
  - board-side confirmation that OTA requests now expose the intended Orvibo model/version identity
  - board-side validation that version-1 default yields direct raw-Opus websocket interop against the target XiaoZhi-compatible server
  - OTA activation UX/log capture on real board
  - MCP volume-only end-to-end validation on server call
  - board-side multi-turn wake/listen/speak/barge-in verification

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

## Workflow Notes

- Future `git commit` messages in this repository should use clear Chinese descriptions by default.
- Do not edit user-owned review files unless explicitly required:
  - `TIPS.md`
  - `REVIEW.md`
  - `.codex/issues.md`
