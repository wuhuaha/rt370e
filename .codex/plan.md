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
2. Replace echo-only cloud stub with a real online control client abstraction and request flow.
3. Introduce audio front-end abstraction and connect local VAD callbacks.
4. Add wake word adapter and event bridge.
5. Add offline ASR adapter interface and routing model.
6. Build online/offline fusion coordinator with clear fallback rules.

## Current Step
- Step 1 in progress: skeleton, echo command, simulated device control, reserved speech interfaces.
