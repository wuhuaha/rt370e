# Ameba River Plan

## Current Context

- Current working branch: `xiaozhi`
- Stable ASR baseline tag: `m3-asr-baseline-fixed-dsb`
- Stable ASR baseline commit: `e40e017`
- Stable flashable full-duplex tag: `m4-full-duplex-bargein-stable`
- Stable flashable full-duplex commit: `6290987`
- Stable XiaoZhi realtime integration tag: `xiaozhi-realtime-runs`
- Stable XiaoZhi realtime integration commit: `39c31d7`
- Preserved WebRTC AECM experiment assets commit: `6546a11`
- Preserved WebRTC AEC experiment snapshot commit: `b7684da`

Current stable product runtime chain:

- `capture -> fixed_dsb -> silero_vad -> streaming asr`

Current branch-level runtime additions already in progress:

- `PlaybackService`
- `ReferenceService`
- `InteractionStateManager`
- `InteractionDiag`
- `VoiceProfile`
- `RuntimePolicy`
- async `Iflytek TTS` worker
- playback-aware barge-in path
- `VoiceFramePool / PlaybackFramePool`
- experimental capability hooks for `AEC / KWS / DoA / wake-guided BF`

Current architecture/implementation documents:

- `AUDIO_DATAFLOW_QUEUE_ARCHITECTURE_ZH.md`
- `AUDIO_DATAFLOW_QUEUE_IMPLEMENTATION_PLAN_ZH.md`
- `VOICE_INTERACTION_REFACTOR_PROPOSAL_ZH.md`
- `XIAOZHI_REALTIME_INTERACTION_ARCHITECTURE_ZH.md`
- `XIAOZHI_INTEGRATION_IMPLEMENTATION_PLAN_ZH.md`
- `WAKE_WORD_XIAOZHI_SESSION_WINDOW_ARCHITECTURE_ZH.md`

## Current Product Direction

Build a maintainable `RTL8730E` voice stack that prioritizes:

- ASR accuracy
- natural dialogue turn-taking
- stable full-duplex interaction
- replaceable acoustic modules
- clean separation between stable product path and experimental profiles

For the current phase, the product path remains:

- `fixed_dsb` as the default ASR front-end
- `silero_vad` as the current VAD
- online streaming ASR as the primary recognition backend
- `Iflytek WS TTS` as the current debug / prompt playback backend

`AEC` remains an optional experimental capability, not part of the current default product mainline.

## Immediate Hotfix Track: BC-ResNet KWS Replacement

Status:

- in progress

Goal:

- replace the current baseline embedded wake-word model with `bc_resnet_best.tflite`
- keep board-side runtime changes minimal and reversible
- validate `wake -> XiaoZhi session` with the new model before returning to broader refactor work

Execution rule:

1. land a dedicated replacement plan document first
2. adapt the KWS runtime only where required:
   - `Add` op registration
   - model-driven input layout handling
3. replace the baseline embedded model asset
4. keep threshold permissive for current board-side validation
5. verify by full local build first, then by board wake/session logs

The current branch objective has moved in two steps:

- first: integrate `XiaoZhi` as a realtime conversation transport while preserving the existing split `ASR + TTS` cloud path as a flashable fallback baseline
- next: refactor admission and dialogue timing toward:
  - local wake-word admission
  - `XiaoZhi` conversation windows
  - less VAD-driven per-utterance session slicing

## XiaoZhi Realtime Integration Direction

`XiaoZhi` should be integrated as a single-session cloud dialogue transport, not as another copy of the current split-provider model.

The intended branch-level mapping is:

- transport/session:
  - one websocket session per conversation window
- uplink audio:
  - `capture -> fixed_dsb -> mono -> Opus -> XiaoZhi`
- downlink audio:
  - `XiaoZhi audio -> Opus decode -> PlaybackService -> speaker`
- text/events:
  - `stt / llm / tts / mcp` all come through one session
- interruption:
  - local barge-in still goes through `PlaybackService` first, then sends `abort`

The intended integration rule is:

- keep current `Iflytek RTASR + Iflytek WS TTS` path buildable as the reference path
- introduce `XiaoZhi` as a new realtime interaction path under the same `river` runtime
- reuse existing `PlaybackService / ReferenceService / InteractionState / frame-ring` infrastructure instead of re-importing an ESP32 application framework

## Current Reality Check

The branch has already moved beyond the old “AEC-only experiment” framing.

What is now technically true:

1. XiaoZhi session transport is already functionally integrated
   - OTA bootstrap, websocket hello, `stt / llm / tts / mcp`, `Opus` uplink/downlink, and local-first `abort` mapping are already landed on branch `xiaozhi`

2. The main experience gap is no longer protocol bring-up
   - the current path still behaves too much like VAD-driven short listen segments
   - perceived realtime remains worse than native XiaoZhi clients
   - downlink playback still needs further stabilization to fully support the intended conversation model

3. The next major work is not “tune AEC first”
   - it is to move from VAD-driven per-utterance session slicing toward:
     - local wake-word admission
     - post-wake conversation windows
     - cleaner `auto -> later realtime` XiaoZhi semantics

## Preserved Experimental Assets

The following assets remain intentionally preserved, but are not part of the default product path:

- `components/river_voice/river_voice_webrtc_aecm_adapter.c`
- `components/river_voice/river_voice_webrtc_aecm_adapter.h`
- `third_party/webrtc_aecm/`
- `WEBRTC_AECM_RIVER_接入记录.md`

These assets are treated as optional experiments only.

## Non-Goals

The following are explicitly out of scope for the immediate plan:

- replacing `Silero VAD`
- changing the online ASR provider
- bringing SDK AEC/BF back into the runtime path
- redesigning product dialogue semantics before the runtime is stable
- merging any experimental AEC path into the default profile prematurely

## Current Engineering Principles

1. Stable product path first
   - `fixed_dsb -> silero_vad -> asr` remains the reference chain.

2. Data plane and control plane must be separated
   - audio frames flow through bounded preallocated buffers
   - start/stop/interrupt/duck go through explicit control paths

3. Real-time path should avoid uncontrolled allocation
   - no high-frequency `malloc/free` in the hot audio path

4. Use the right queue model for the right boundary
   - `ping-pong` for DMA edges
   - `SPSC ring` for audio frame handoff
   - `command queue / mailbox` for control actions

5. Full-duplex behavior must be explainable from logs
   - playback state
   - reference state
   - VAD state
   - ASR session state
   - interruption cause

## Target Architecture

The target runtime should converge toward:

- hardware edge:
  - `DMA ping-pong`
- capture path:
  - `CaptureTask -> fixed-frame VoiceRing -> VoicePipelineTask`
- playback path:
  - `TTS decode -> fixed-frame PlaybackRing -> PlaybackFeederTask -> AudioTrack`
- reference path:
  - `PlaybackFeederTask -> RefRing`
- control path:
  - `TurnManager / PlaybackCommandQueue / InteractionStateManager`

The intended rule is:

- audio data moves through fixed-size frame buffers
- interaction behavior moves through explicit state transitions and commands

## Phase Plan

### Phase 1: Stabilize the Current Mainline

Goal:

- make the current async TTS + ASR + barge-in path reliable enough for continued development

Tasks:

- reduce TTS websocket / queue memory peak
- keep TTS network receive and playback feeding decoupled
- prevent large websocket frame handling from duplicating payload allocations
- keep playback-time ASR session start working
- make barge-in interruption behavior deterministic enough for validation
- keep stats/logging useful but not overly noisy

Deliverable:

- a stable board-usable full-duplex debug chain

Exit criteria:

- no recurring heap-collapse on normal TTS playback
- TTS can either finish or be interrupted predictably
- playback-time ASR start remains reproducible

### Phase 2: Unify Playback Control

Goal:

- stop scattering playback stop/interrupt logic across modules

Tasks:

- define explicit playback commands:
  - `start`
  - `stop`
  - `interrupt`
  - `flush`
  - `duck`
- introduce `PlaybackEpoch`
- centralize interruption handling in playback control
- align reference lifecycle with playback lifecycle

Deliverable:

- one authoritative playback control surface

Exit criteria:

- playback interruption never depends on ad-hoc local stop calls only
- old queued audio is not consumed after interrupt

### Phase 3: Normalize the Audio Data Plane

Goal:

- move mixed byte-buffer logic toward fixed-frame audio buffers

Tasks:

- convert capture ring from byte ring to fixed-frame ring
- convert TTS PCM queue toward fixed-frame slots
- convert reference distribution toward frame-based transport
- keep the algorithm chain itself mostly direct:
  - `frame -> preproc -> vad -> asr feed`

Deliverable:

- uniform frame-based audio transport across major runtime boundaries

Exit criteria:

- capture / playback / reference all use explicit frame contracts
- queue depth, overflow, underrun become directly measurable

### Phase 4: Resource Pool and Low-Overhead Optimization

Goal:

- reduce runtime fragmentation and copy overhead after the frame model is stable

Status:

- done for the current flashable build
- completed in code:
  - frame ring now supports externally provided storage and `SPSC` mode where appropriate
  - capture path now keeps persistent per-open buffers outside the realtime thread
  - `VoiceFramePool` now owns VAD-probe scratch buffers
  - `PlaybackFramePool` now owns playback-reference ring backing storage
  - `TTS` now keeps a persistent session resource block for decode + PCM queue storage
  - `TTS PCM queue` now runs on the explicit `SPSC` ring path

Tasks:

- add `VoiceFramePool`
- add `PlaybackFramePool`
- convert suitable `SPSC` paths to lock-free or near-lock-free rings
- remove remaining high-frequency heap churn in hot paths

Deliverable:

- lower jitter, lower fragmentation, more predictable heap behavior

Exit criteria:

- hot paths no longer rely on repeated dynamic allocation
- long-run stability improves under mixed ASR/TTS use

### Phase 5: Expansion Hooks for AEC / KWS / DoA

Goal:

- prepare clean interfaces for future modules without polluting the stable path

Status:

- done for the current flashable build
- completed in code:
  - formal experimental `AEC` input/output contract is now queryable through `river_voice_experiment`
  - wake-stage and post-wake-stage boundaries are now explicit in runtime policy
  - side-path access is now reserved through a real frame-hook interface for `KWS / DoA / wake-guided beamforming`
  - capability selection is now explicit through profile capability masks and build flags

Tasks:

- define formal input/output contracts for experimental `AEC`
- define wake-stage and post-wake-stage profile boundaries
- reserve side-path access for:
  - `KWS`
  - `DoA`
  - wake-guided beamforming
- keep each capability selectable behind an explicit profile or build flag

Deliverable:

- a runtime that can accept new acoustic modules without rewriting playback/capture plumbing

Exit criteria:

- future algorithm integration happens through interfaces, not invasive rewiring

### Phase 6: XiaoZhi Realtime Session Integration

Goal:

- integrate XiaoZhi server as a first-class realtime conversation backend on top of the current stable duplex runtime

Status:

- implementation-complete on branch `xiaozhi`
- protocol/session layer, `Opus` uplink/downlink, playback/barge-in mapping, and `MCP` bridge have landed
- remaining work is now board validation, long-run stability checks, and parameter tightening

Tasks:

- add a dedicated `XiaoZhi` session transport layer
- support websocket `hello / listen / abort / stt / tts / mcp`
- add `Opus 16k/1ch/60ms` uplink
- support server-driven downlink audio params and decode path
- map `stt / tts / mcp` events into current `InteractionState / PlaybackService / OnlineControl`
- preserve the current Iflytek split path as an explicit fallback/backend baseline

Deliverable:

- a board-usable realtime XiaoZhi interaction path with text, audio, barge-in, and device-control integration

Exit criteria:

- XiaoZhi session handshake is stable on board
- realtime uplink/downlink audio runs through the existing runtime without breaking the stable fallback path
- playback-time barge-in can interrupt local playback and propagate `abort` upstream
- server-side `MCP` requests can drive the existing local device control surface

### Phase 7: Wake-Word Admission and Conversation Window Refactor

Goal:

- reduce cloud idle cost and improve perceived realtime by replacing VAD-driven per-utterance listen slicing with:
  - local wake-word admission
  - XiaoZhi session windows
  - a cleaner post-wake `auto` dialogue loop

Status:

- implementation-started on branch `xiaozhi`
- Phase A minimal KWS loop is now landed in code:
  - wake-stage `fixed_dsb` mono sidepath now feeds a board-side `log-mel` frontend
  - exported DS-CNN wake-word model is now integrated through `TFLite Micro`
  - wake-word hits now emit `RIVER_VOICE_EVENT_WAKEWORD` into the existing frontend/app event path
  - board-side `TFLite Micro` runtime compatibility hardening is now landed:
    - runtime/model tensor-type divergence is handled explicitly
    - tensor byte-count vs element-count divergence no longer tears down the sidepath
    - quantized-model scale/zero-point now resolve from flatbuffer schema first, with runtime tensor params only as fallback
    - wake-word admission no longer runs cloud-side actions directly from the audio sidepath thread
  - current work has passed build validation; board tuning and threshold calibration remain pending
- Phase B/C minimum conversation-window refactor is now landed in code:
  - wake-word hits now open a XiaoZhi post-wake `auto` conversation window
  - idle VAD no longer owns cloud listen admission outside an active conversation window
  - post-wake state now falls back through `FOLLOW_UP` before returning to `WAKE_MONITORING`
  - conversation-window visibility is now exposed through runtime status logs
  - current work has passed build validation; board semantics and timeout tuning remain pending
- Phase D policy hardening is now partially landed in code:
  - playback start no longer exports reference by default on profiles without native ref / AEC capability
  - idle VAD is now suppressed while TTS playback is active on profiles that cannot support safe playback-time listen admission
  - capture buffering headroom has been increased to reduce transient overflow during bring-up / networking pressure
  - current work has passed build validation; board-side policy tuning remains pending

Tasks:

- integrate a local KWS backend on top of the current `fixed_dsb` mono path
- add board-side `log-mel` feature extraction for the KWS model
- define explicit wake-stage and post-wake-stage runtime ownership:
  - wake stage:
    - local KWS owns admission
  - post-wake stage:
    - XiaoZhi session window owns dialogue continuity
- keep `silero_vad` in post-wake stage as an auxiliary module for:
  - upload gating
  - endpoint assistance
  - barge-in
  - session-window timeout
- first land `KWS -> XiaoZhi auto conversation window`
- defer `KWS -> XiaoZhi realtime conversation window` until playback/AEC readiness is proven

Deliverable:

- a board-usable wake-word-first dialogue runtime that feels closer to native XiaoZhi behavior while keeping cloud resource usage bounded

Exit criteria:

- idle state no longer depends on cloud-side session admission
- wake-word hit reliably opens a post-wake conversation window
- empty short listen segments are significantly reduced
- short utterances are less likely to lose the first word than in the current VAD-driven model

Implementation phases:

- Phase A:
  - board-side KWS minimum loop
  - status:
    - implementation-complete
    - build-validated
  - completed scope:
    - import exported wake-word model into firmware tree
    - add board-side `log-mel` feature extraction on top of the `fixed_dsb` mono wake-stage sidepath
    - add `TFLite Micro` DS-CNN inference with a minimal op resolver
    - add wake-word threshold / hold / cooldown / log-period build knobs
    - emit wake-word events and app-level detection logs
  - remaining follow-up:
    - board-side score distribution capture
    - threshold / cooldown tuning
    - false accept / false reject validation under real speaker playback and room noise
    - verify on-device behavior across board-side `TFLite Micro` tensor metadata variants after runtime hardening

- Phase B:
  - wake admission ownership refactor
  - status:
    - implementation-complete
    - build-validated
  - scope:
    - switch idle admission from VAD-driven short listen slicing to local wake-word-first admission
    - keep `silero_vad` active as an auxiliary module instead of the primary session boundary owner

- Phase C:
  - XiaoZhi post-wake conversation window
  - status:
    - implementation-complete
    - build-validated
  - scope:
    - open a post-wake `auto` conversation window after KWS hit
    - keep dialogue continuity across short pauses without reopening a new short listen segment each time

- Phase D:
  - barge-in / timeout / close-window policy
  - status:
    - implementation-in-progress
    - build-validated for the current code wave
  - scope:
    - define post-wake timeout, silence close, and playback-time interruption rules on top of the new window model
  - current landed subset:
    - do not open playback reference paths by default on profiles without `AEC` / native ref support
    - do not let playback-state VAD reopen cloud ASR while local TTS is active on profiles that cannot safely support duplex admission

- Phase E:
  - board tuning and acceptance validation
  - status:
    - pending
  - scope:
    - collect KWS hit/miss logs
    - tune runtime thresholds
    - confirm idle cloud cost reduction and improved perceived realtime

## Immediate Work Breakdown

### Task A

Keep the current `fixed_dsb` ASR baseline intact and independently testable.

Status:

- done for current refactor wave
- mainline still stays on `capture -> fixed_dsb -> silero_vad -> streaming asr`

### Task B

Stabilize the async `Iflytek TTS` worker path:

- reduce memory peak
- keep queueing bounded
- preserve playback completeness

Status:

- done for the current flashable build
- `TTS websocket receive -> fixed-frame PCM queue -> feeder -> PlaybackService` is in place
- websocket long-frame duplication and repeated hot-path heap churn have been reduced
- `TTS` decode buffer and PCM queue backing storage now come from one persistent resource block
- `TTS PCM queue` now runs through the explicit `SPSC` frame-ring path

### Task C

Make playback-time interruption authoritative:

- ASR-session-start during playback should be able to interrupt or duck playback through one path

Status:

- done for the current control surface
- `PlaybackService` now exposes centralized `stop / interrupt / flush / duck`
- playback epoch is now the authoritative stale-audio invalidation mechanism

### Task D

Refactor capture / playback / reference toward fixed-frame queue contracts.

Status:

- done for the current flashable build
- capture ring: fixed-frame
- TTS PCM queue: fixed-frame
- playback reference transport: fixed-frame
- frame-ring transport now also supports external backing storage so later pools do not need queue rewrites

### Task E

Add persistent observability for:

- queue peak
- queue overflow
- underrun / overrun
- interruption source
- playback epoch transitions

Status:

- done for the current flashable build
- playback status now includes `epoch / epoch_adv / control reason / interrupt reason`
- capture / reference / TTS status now expose queue depth and peak / overflow-style diagnostics
- VAD diagnostics now include `ref_peak`
- playback control operations emit explicit logs with reason propagation

### Task F

Document and validate resource baseline after each structural step.

Status:

- done for the current flashable build
- `river status` now includes playback / capture / reference / TTS queue-state visibility
- `river status` also emits `diag_status` runtime snapshot for quick board-side baseline capture
- follow-up board validation still remains necessary, but the baseline capture path is now in place

## Current Immediate Focus

1. preserve the current flashable XiaoZhi milestone as the branch reference point
2. board-validate and tune the newly landed local wake-word backend on top of the current `fixed_dsb` mono path
3. refactor XiaoZhi dialogue timing from per-utterance VAD slicing toward a post-wake conversation window
4. use `auto` mode first; only consider `realtime` after playback/AEC readiness is proven
5. continue tracking downlink playback stability because conversation-window quality depends on it

## Current Reference Split

Use the following mental split while implementing the branch:

- stable fallback baseline:
  - current flashable `Iflytek RTASR + Iflytek WS TTS`
- active integration branch objective:
  - `XiaoZhi realtime session + Opus + MCP`
  - then `local KWS admission + XiaoZhi conversation window`
- preserved experiments:
  - `WebRTC AECM / future acoustic modules`

## Main Risks

### Risk 1: Memory Collapse Under Full-Duplex Load

If websocket payload buffering, TTS queueing, playback buffers, and reference export stack up incorrectly, the heap can collapse during normal user flows.

### Risk 2: Partial Full-Duplex Illusion

The system may appear to “support barge-in” because ASR starts during playback, while still failing to give way in a product-acceptable manner.

### Risk 3: Buffer Model Fragmentation

If capture, TTS, playback, and reference all keep different buffer semantics, future AEC/KWS/DoA integration will become slower and riskier.

### Risk 4: Premature Optimization

If lock-free queues and resource pools are introduced before the frame model is stable, debugging cost will rise sharply.

## Success Criteria

This branch is successful only if all of the following hold:

- default mainline `fixed_dsb` path remains intact
- playback-time ASR can start reliably
- playback interruption behavior is deterministic and explainable
- hot paths avoid uncontrolled heap churn
- major runtime boundaries use explicit, bounded frame contracts
- XiaoZhi session transport remains buildable and flashable
- future AEC/KWS/DoA integration can happen without re-breaking the base voice path
- local wake-word admission can be introduced without tearing down the current runtime layering

## Current Recommendation

Proceed with the branch as:

- first: a stable XiaoZhi transport branch
- next: a wake-word-first dialogue-runtime branch
- not: an AEC-tuning-first branch

Use the preserved WebRTC AECM work only as an optional future experiment after:

- playback/control-plane unification is complete
- frame contracts are normalized
- memory peaks are under control
- the base full-duplex runtime is stable on board
- the wake-word admission and conversation-window runtime has been validated on board
