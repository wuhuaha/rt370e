# Ameba River Architectural Refactor Plan

Date: 2026-03-23

## Refactor Intent

This refactor is not a cosmetic cleanup. The target is a top-level architectural correction of the project so that it behaves like a durable real-time voice product codebase rather than an accumulated prototype.

Reference style:

- thin application entrypoint
- explicit session ownership
- policy separated from transport
- runtime-critical paths isolated from orchestration
- observable state transitions
- small modules with single responsibility and predictable coupling

The architectural benchmark is closer to projects such as `livekit/agents`: session-centric orchestration, strong runtime boundaries, and clear control/data-plane separation.

Companion documents:

- `ARCHITECTURE_REFACTOR_BLUEPRINT_ZH.md`
- `REFACTOR_TODO_ZH.md`
- `KWS_PIPELINE_ZH.md`

## Current Diagnosis

The project already contains strong functional building blocks, but the codebase still shows several signs of architectural drift:

1. Top-level orchestration logic is too easy to accumulate in app-facing modules.
2. Cloud transport, conversation-window policy, uplink/downlink workers, and ASR bridge state are still concentrated in very large modules.
3. Voice-path code mixes hard real-time data movement, DSP/KWS runtime, diagnostics, and experiment hooks in ways that make performance reasoning harder than it should be.
4. The project has useful runtime logs, but ownership boundaries are not yet sharp enough for every log line to map cleanly to one subsystem owner.
5. Some interfaces are still callback-shaped around historical growth rather than around stable domain concepts.

This means performance bugs, memory regressions, and behavioral regressions are still more expensive to localize than they should be.

## Architectural North Star

The target structure is:

### 1. Bootstrap Layer

Owns only:

- process boot order
- service initialization
- dependency wiring
- top-level status dump

Must not own:

- wake admission policy
- ASR/playback synchronization
- conversation session lifecycle

### 2. Session Coordination Layer

Owns only:

- interaction-state transitions
- wakeword admission sequencing
- ASR session open/close semantics
- playback interruption and barge-in policy

Must not own:

- low-level transport details
- DSP/KWS implementation
- UI or hardware initialization

### 3. Voice Runtime Layer

Owns only:

- capture / preproc / detector / KWS data plane
- runtime queueing and gating
- latency-sensitive worker execution

Must not own:

- conversation policy
- cloud session policy

### 4. Cloud Session Layer

Owns only:

- provider session lifecycle
- conversation window policy
- uplink/downlink transport workers
- provider-specific state machines
- ASR/TTS bridge behavior

Must not own:

- app interaction policy
- wake admission decisions

### 5. Capability/Policy Layer

Owns only:

- product behavior toggles
- profile/capability selection
- experiment gating

It should be possible to inspect one module and understand whether a behavior is transport policy, voice runtime policy, or product policy.

## Completed In This Wave

### Wave 1: Extract Runtime Session Coordination

Completed:

- extracted runtime orchestration from `components/river_core/river_app.c`
- introduced `components/river_core/river_session_coordinator.c`
- introduced private coordinator interface `components/river_core/river_session_coordinator.h`
- rewired app boot to register coordinator callbacks instead of owning wake/ASR/playback policy directly
- rebuilt full firmware successfully after the extraction

Result:

- `river_app.c` is reduced to a thin bootstrap/wiring role
- session policy now has an explicit owner
- future refactors can move faster without using `river_app.c` as a shared dumping ground

## Refactor Roadmap

### Phase A: Session Boundary Hardening

Status: in progress

Goals:

- keep `river_app.c` as a pure bootstrap module
- keep all wake / ASR / playback coordination in the session coordinator
- prevent new policy logic from leaking back into bootstrap code

Acceptance:

- no new runtime policy branches added to `river_app.c`
- all interaction-state transitions originate from explicit subsystem owners

### Phase B: Cloud Adapter Decomposition

Status: next

Primary target:

- `components/river_cloud/river_cloud_adapter.c`

Current issue:

- one file still owns provider runtime, conversation windowing, pre-roll, transport workers, diagnostics, and public adapter API

Planned split:

- `river_cloud_session_policy.*`
  owns conversation window lifecycle, wake/follow-up timing, provider-independent session policy
- `river_cloud_xiaozhi_runtime.*`
  owns xiaozhi transport runtime, worker tasks, uplink/downlink ring handling, websocket event processing
- `river_cloud_asr_bridge.*`
  owns provider-facing audio open/push/close flow and ASR result fanout

Acceptance:

- provider runtime and product policy become separable
- cloud adapter public API becomes a façade, not the implementation sink

### Phase C: Voice Runtime Decomposition

Status: planned

Primary targets:

- `components/river_voice/river_voice_kws.cc`
- `components/river_voice/river_voice_vad_probe.c`

Planned split:

- KWS frontend/runtime worker
- KWS gate + pre-roll queue management
- VAD probe diagnostics/reporting
- experiment sidepath routing

Acceptance:

- hot-path runtime code can be profiled without wading through diagnostics and orchestration logic
- queueing behavior and feature-generation behavior have clear ownership
- the living KWS runtime reference in `KWS_PIPELINE_ZH.md` stays aligned with code and logs

### Phase D: Public Interface Cleanup

Status: planned

Goals:

- distinguish public headers from component-private headers consistently
- reduce accidental public API exposure
- standardize callback signatures around domain events rather than historical convenience

Acceptance:

- internal modules no longer appear under `include/river/` unless they are stable public contracts
- private headers are local to their owning component

### Phase E: Reliability and Performance Audit

Status: planned

Goals:

- eliminate duplicated state ownership
- review queue sizing and memory residency
- audit retry / close / reset paths
- ensure repeated wake-session-open-close cycles do not fragment heap or duplicate work

Acceptance:

- steady-state memory behavior is observable
- repeated session cycles do not reintroduce wake storms, duplicate opens, or backlog growth

## Immediate Next Slice

The next highest-value refactor slice is `river_cloud_adapter.c`.

Reason:

- it is still the largest concentration of mixed concerns
- it sits on the fault line between transport, policy, and runtime workers
- its current size makes reopen bugs, memory pressure, and session-edge regressions harder to reason about than necessary

Immediate implementation goal:

- extract conversation-window policy and provider runtime into separate internal modules while keeping the existing public API stable

## Current Wave TODO

- establish a cloud-internal contract file so context ownership is explicit
- extract xiaozhi conversation-window and session policy out of `river_cloud_adapter.c`
- keep `river_cloud_adapter.c` as the external API façade and provider bridge entrypoint
- rebuild full firmware after the slice lands

## Engineering Guardrails

During the remaining refactor waves:

1. No large-file rewrite without a clear ownership split.
2. No behavior-preserving cleanup that does not improve boundaries, observability, or runtime cost.
3. Every new module must have a crisp owner and a short responsibility statement.
4. Public interfaces should remain stable unless the replacement is materially better and the migration is done in the same wave.
5. Every architecture slice must compile before the next slice starts.

## Success Criteria

This refactor is successful only if the project becomes measurably easier to evolve:

- smaller modules
- clearer ownership
- more predictable runtime behavior
- less cross-module hidden coupling
- easier debugging from logs
- easier future performance work without destabilizing product behavior

The standard is not "cleaner code". The standard is "production-grade structure that remains maintainable under ongoing feature pressure".
