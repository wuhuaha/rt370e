# Ameba River Plan

## Goal
Build a maintainable `RTL8730E` voice home-control application that starts with online control and cleanly evolves into online/offline hybrid speech interaction.

## Architecture Direction
1. `river_core`
   - app orchestration
   - state and intent routing
   - future dialogue/session coordinator
2. `river_voice`
   - local VAD
   - local wake word
   - future offline ASR adapter
3. `river_cloud`
   - online speech / NLU / home-control transport
   - future backend provider switch point
4. `river_diag`
   - monitor commands
   - test injection and observability

## Step Plan
1. Bootstrap project, add `.codex` workflow, and create monitor echo + device-control skeleton.
2. Add a board-level mic-to-speaker audio echo path with fixed delay so the audio hardware chain can be validated independently.
3. Replace echo-only cloud stub with a real online control client abstraction and request flow.
4. Promote the local audio path into a reusable front-end abstraction and connect VAD callbacks.
5. Add wake word adapter and event bridge.
6. Add offline ASR adapter interface and routing model.
7. Build online/offline fusion coordinator with clear fallback rules.

## Current Step
- Step 2 completed: command-driven board audio echo bring-up is implemented and builds for `RTL8730E`.
- Step 2.1 completed: serial diagnostics are available to separate capture-side failure from playback-side failure during board bring-up.
- Step 2.2 completed: boot-time echo autostart is enabled so board audio can be validated even when monitor command registration is not usable on the target.
- Step 2.3 completed: `river` project Kconfig symbols now propagate into the compiled sources through `platform_autoconf.h`.
- Step 2.4 completed: mono `AMIC3` echo narrowed the mic side, but runtime results still did not produce clean speech.
- Step 2.5 in progress: isolate the speaker path with a direct `AudioTrack` playback self-test based on the SDK `aplay` flow.
- Next recommended step depends on the speaker self-test result:
  - if direct playback is clean, continue by refining capture routing and reintroducing echo on top of the proven speaker path
  - if direct playback is silent or still noisy, stop changing software architecture and focus on board output routing, amplifier path, and speaker wiring
