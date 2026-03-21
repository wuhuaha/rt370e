# Ameba River Plan

## Current Context

- Current working branch: `xiaozhi`
- Stable ASR baseline tag: `m3-asr-baseline-fixed-dsb`
- Stable ASR baseline commit: `e40e017`
- Stable flashable full-duplex tag: `m4-full-duplex-bargein-stable`
- Stable flashable full-duplex commit: `6290987`
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

The current branch objective is now to add `XiaoZhi` as a realtime conversation transport while preserving the existing split `ASR + TTS` cloud path as a flashable fallback baseline.

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

1. Playback-time ASR start is partially working
   - logs already show `speaking -> barge_in_listening`
   - logs already show ASR sessions can start while TTS is active

2. The system is still not architecturally clean enough
   - playback interruption is not yet fully unified under one control surface
   - data-plane buffering models are still mixed
   - TTS / websocket / playback memory peaks are still too high

3. The next major work is not “tune AEC first”
   - it is to stabilize the audio data plane and control plane
   - then make barge-in and future AEC/KWS/DoA plug into a clean runtime

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

1. board-validate the XiaoZhi session handshake and confirm the session remains stable on device
2. verify realtime uplink/downlink audio against the current flashable Iflytek fallback baseline
3. confirm playback-time barge-in still interrupts locally first and propagates upstream `abort`
4. validate `MCP` device control against the existing local online-control surface
5. decide from measured heap/queue/runtime data whether any post-validation optimization is still justified

## Current Reference Split

Use the following mental split while implementing the branch:

- stable fallback baseline:
  - current flashable `Iflytek RTASR + Iflytek WS TTS`
- active integration branch objective:
  - `XiaoZhi realtime session + Opus + MCP`
- preserved experiments:
  - `WebRTC AECM / future acoustic modules`
2. re-check heap low-water mark and long-run stability under mixed ASR/TTS playback
3. confirm whether playback underrun or capture/reference overflow changed after pool + `SPSC` adoption
4. validate the new profile/stage/capability logs against actual runtime behavior
5. only after that decide whether any further optimization is still justified

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
- future AEC/KWS/DoA integration can happen without re-breaking the base voice path

## Current Recommendation

Proceed with the branch as an audio-runtime stabilization and refactor branch first, not as an AEC-tuning branch.

Use the preserved WebRTC AECM work only as an optional future experiment after:

- playback/control-plane unification is complete
- frame contracts are normalized
- memory peaks are under control
- the base full-duplex runtime is stable on board
