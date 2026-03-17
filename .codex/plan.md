# Ameba River Plan

## Current Context

- Current working branch: `debug/webrtc-aec`
- Stable ASR baseline tag: `m3-asr-baseline-fixed-dsb`
- Stable ASR baseline commit: `e40e017`
- Preserved WebRTC AECM experiment assets commit: `6546a11`
- Current WebRTC AEC experiment snapshot commit: `b7684da`

Current stable mainline runtime chain:

- `capture -> fixed_dsb -> silero_vad -> streaming asr`

Current branch-level refactor additions:

- `PlaybackService`
- `ReferenceService`
- `InteractionStateManager`
- `VoiceProfile`
- `RuntimePolicy`

Current preserved experiment assets:

- `components/river_voice/river_voice_webrtc_aecm_adapter.c`
- `components/river_voice/river_voice_webrtc_aecm_adapter.h`
- `third_party/webrtc_aecm/`
- `WEBRTC_AECM_RIVER_接入记录.md`

These assets are intentionally preserved, but they are not part of the current stable runtime chain.

## Product Direction

Build a maintainable `RTL8730E` voice stack that prioritizes:

- ASR accuracy
- natural dialogue turn-taking
- replaceable acoustic modules
- clean separation between stable product path and experimental acoustic profiles

For the current phase, the product path remains:

- `fixed_dsb` as the default ASR front-end
- `silero_vad` as the current VAD
- online streaming ASR as the primary recognition backend

`AEC` is treated as an optional barge-in capability, not a default always-on front-end stage.

## AEC Experiment Goal

Land a WebRTC-based `AEC` experiment that can be evaluated professionally without destabilizing the current `fixed_dsb` ASR baseline.

The target behavior is:

- when native `capture ch3 ref` is unavailable or inactive:
  - stay on the existing stable `fixed_dsb` path
- when native `capture ch3 ref` is present, aligned, and active:
  - route audio through `AEC + DSB`

This experiment must be isolated behind an explicit experimental profile or build-time switch. It must not silently change the default ASR path.

## Non-Goals

The following are explicitly out of scope for this branch:

- replacing `Silero VAD`
- changing the online ASR provider
- introducing SDK AEC/BF back into the runtime path
- changing product-layer dialogue logic
- landing unverified AEC behavior into the default ASR profile

## Constraints

### Hardware / Runtime Constraints

- board microphone array: `AMIC1 + AMIC3`
- spacing: `50mm`
- frame cadence in current voice path: `16ms @ 16kHz = 256 samples`
- WebRTC AECM processing cadence: `10ms @ 16kHz = 160 samples`

### Architectural Constraints

- `river_voice_preproc` remains the only valid insertion boundary for AEC in the current architecture
- `river_voice_vad_probe` must remain primarily a VAD / ASR validation path, not become a dumping ground for AEC-specific logic
- `fixed_dsb` mainline must remain intact and independently testable

## Engineering Principles

1. Stable product path first
   - `fixed_dsb -> silero_vad -> asr` remains the reference chain.

2. AEC as an explicit experiment
   - no hidden runtime mode changes in the default profile.

3. Strict timing correctness before acoustic tuning
   - solve `10ms AECM` and `16ms main frame` alignment before evaluating quality.

4. Real reference only
   - AEC is only meaningful when driven by a real native reference with stable timing.

5. Observability before optimization
   - add counters and state logs before making subjective tuning decisions.

## Implementation Plan

### Phase 0: Freeze the Reference Baseline

Goal:

- keep the current `fixed_dsb` ASR path as the reference implementation for all A/B comparisons

Tasks:

- confirm the default preproc backend remains `fixed_dsb`
- confirm the current `Silero VAD` and ASR path continue to work without `AEC`
- keep existing far-field VAD tuning separate from AEC acceptance criteria

Deliverable:

- a reproducible no-AEC baseline for objective comparison

Exit criteria:

- current default path still produces stable ASR on board

### Phase 1: Define an Explicit Experimental AEC Profile

Goal:

- create a dedicated profile or build switch for WebRTC AECM experiments

Tasks:

- keep `fixed_dsb` as the mainline product backend name
- define a distinct experimental profile such as:
  - `fixed_dsb_webrtc_aecm`
- ensure logs always reflect the real active profile

Deliverable:

- clear separation between product path and experiment path

Exit criteria:

- no ambiguity in runtime logs or configuration about whether AEC is active

### Phase 2: Solve 10ms / 16ms Frame Alignment

Goal:

- guarantee that AECM output is time-aligned with the current main pipeline frame contract

Tasks:

- document the frame contract:
  - input to main pipeline: `256 samples`
  - AECM internal step: `160 samples`
- redesign adapter buffering so that:
  - input frames can be sliced deterministically into AECM blocks
  - output frames are reconstructed without mixing stale and current semantic frame boundaries
- add instrumentation for:
  - input samples pushed
  - output samples popped
  - FIFO depth
  - underrun
  - overrun
  - dropped blocks

Design requirement:

- the adapter must make it explicit whether output corresponds to:
  - exact-current frame
  - delayed-but-contiguous frame

Deliverable:

- a hardened AECM adapter with deterministic framing behavior

Exit criteria:

- no frame-boundary ambiguity remains in the experiment chain

### Phase 3: Build a Real Playback Reference Validity Model

Goal:

- prevent AEC from toggling on/off based on single-frame noise

Tasks:

- define reference states:
  - `ref_missing`
  - `ref_present_idle`
  - `ref_present_active`
- add runtime indicators:
  - peak
  - RMS or average energy
  - active ratio in a sliding window
- implement hysteresis:
  - enter threshold
  - exit threshold
  - minimum active duration
  - hangover before disable
- forbid full AEC reset on every short inactive gap

Deliverable:

- stable AEC gating based on real playback activity, not single-frame spikes

Exit criteria:

- AEC no longer flaps during short TTS pauses or brief playback silence gaps

### Phase 4: Integrate AEC into `river_voice_preproc`

Goal:

- insert AEC only at the preproc boundary, keeping the rest of the chain unchanged

Tasks:

- converge the experimental profile to native `capture(3ch) = mic0 + mic1 + ref`
- process microphone channels with WebRTC AECM before beamforming
- keep `fixed_dsb` as the downstream spatial combine step
- define precise behavior for four cases:
  - no ref path
  - ref path exists but inactive
  - ref active and valid
  - ref path degraded or adapter failure

Required fallback policy:

- on any AEC experiment failure, fall back to plain `fixed_dsb`
- never block the ASR chain on AEC failure

Deliverable:

- experimental `AEC + DSB` preproc path with safe fallback

Exit criteria:

- AEC failure does not break ASR or VAD

Status:

- completed on the current branch
- native `capture(3ch) = mic0 + mic1 + ref` experiment path is integrated
- runtime gate now depends on playback state, interaction state, and reference activity

### Phase 5: Keep `vad_probe` Clean

Goal:

- avoid contaminating the main VAD validation path with AEC-only assumptions

Tasks:

- expose enough reference diagnostics for AEC experiments
- do not hardwire `vad_probe` into a permanently ref-dependent mode
- keep `vad_probe` usable for:
  - raw VAD validation
  - DSB validation
  - ASR stream validation

Deliverable:

- `vad_probe` remains a general validation path rather than an AEC-specialized test harness

Exit criteria:

- VAD/ASR debugging remains possible even when AEC is disabled

Status:

- partially completed
- `vad_probe` remains usable as the main validation path, but board-side AEC validation is still pending

### Phase 6: Add AEC-Focused Observability

Goal:

- make AEC quality and failure modes visible in runtime logs

Tasks:

- log profile and AEC state transitions
- log adapter stats:
  - blocks_in
  - blocks_out
  - fifo_depth
  - underrun
  - overrun
  - reset_count
- log reference stats:
  - peak
  - active ratio
  - entered_active_count
  - exited_active_count
- log preproc fallback reason when experiment path is bypassed

Deliverable:

- actionable logs for AEC diagnosis

Exit criteria:

- every AEC bypass or fallback is explainable from logs

### Phase 7: Acoustic Evaluation Matrix

Goal:

- evaluate AEC using controlled scenarios instead of ad-hoc impressions

Scenarios:

1. no playback, near-field speech
2. no playback, far-field speech
3. playback active, no user speech
4. playback active, user barge-in near-field
5. playback active, user barge-in far-field
6. playback active with short pause in TTS
7. playback active with bursty system prompt tones

Metrics:

- VAD trigger rate
- ASR final accuracy
- ASR empty-result rate
- false cut / early endpoint rate
- subjective barge-in responsiveness
- heap and CPU deltas versus baseline

Deliverable:

- A/B table:
  - baseline `fixed_dsb`
  - experimental `webrtc_aecm + fixed_dsb`

Exit criteria:

- experiment has objective evidence, not just anecdotal preference

### Phase 8: Decision Gate

Goal:

- decide whether WebRTC AECM is good enough to continue, needs redesign, or should be abandoned

Decision outcomes:

1. keep as experiment only
2. continue tuning for productization
3. replace with another AEC approach

Promotion criteria:

- no ASR regression in no-playback cases
- improved or at least acceptable playback-interruption cases
- stable runtime without frame corruption
- acceptable resource overhead

## Immediate Work Breakdown

### Task A

Create an explicit experimental profile and keep default `fixed_dsb` untouched.

### Task B

Refactor the existing `webrtc_aecm_adapter` into a deterministic `160 <-> 256` framing module with visible stats.

### Task C

Implement reference-active hysteresis:

- enter threshold
- exit threshold
- stable window
- hangover

### Task D

Integrate the experimental path into `river_voice_preproc` with guaranteed fallback to `fixed_dsb`.

### Task E

Add logs and counters for:

- AEC state
- adapter health
- reference health
- fallback reasons

### Task F

Run the full acoustic evaluation matrix and compare against the `m3-asr-baseline-fixed-dsb` baseline.

## Current Immediate Focus

1. keep the stable `fixed_dsb` baseline untouched
2. validate the current gated WebRTC AECM experiment on board
3. decide whether WebRTC AECM is worth continuing before moving into larger wake/profile/beamforming refactors

## Risks

### Risk 1: Frame Misalignment

If `AECM` output is not strictly aligned, it will damage:

- VAD timing
- ASR boundary quality
- perceived responsiveness

This is the highest priority technical risk.

### Risk 2: Reference Flapping

If AEC enable/disable is based on single-frame peak detection, the adaptive state will never stabilize.

### Risk 3: Over-processing

Aggressive AEC in no-playback or weak-ref scenes can degrade ASR more than it helps.

### Risk 4: Validation Contamination

If `vad_probe` becomes AEC-specific, future debugging of VAD / DSB / ASR will become slower and less reliable.

## Success Criteria

This branch is successful only if all of the following hold:

- default mainline `fixed_dsb` path remains intact
- experimental `AEC` path is explicitly selectable
- `10ms / 16ms` alignment is deterministic
- AEC activation uses hysteresis and stable reference logic
- no-playback ASR is not worse than the baseline
- playback-interruption scenarios become measurably better or at least technically explainable

## Current Recommendation

Proceed with WebRTC AECM only as an isolated experiment on `debug/webrtc-aec`.

Do not merge any AEC changes into the default runtime chain until:

- frame alignment is proven correct
- reference gating is stable
- A/B acoustic results are documented against the `m3-asr-baseline-fixed-dsb` baseline
