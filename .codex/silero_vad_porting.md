# Silero VAD Porting Record

## Purpose
This document is the single reproducibility record for migrating `Silero VAD` into `ameba-river`.

It must be updated during every migration step so the port can be rebuilt later without relying on memory.

## Scope
- target board: `RTL8730E`
- product direction: `ASR-first`
- detector boundary:
  - input: enhanced mono PCM from `river_voice_preproc`
  - output: stable `VAD` events for later `KWS/ASR`
- backend policy:
  - first bring-up can use a third-party model
  - final architecture must allow replacement with a self-developed model

## Repro Checklist
1. Upstream source
   - repository:
   - tag / commit:
   - local archive checksum:
2. Model choice
   - exact model file:
   - input sample rate:
   - required frame / hop:
   - input normalization rule:
   - output semantics:
3. Conversion / export
   - host environment:
   - tool versions:
   - export command:
   - post-processing command:
   - generated artifact checksum:
4. Embedded runtime choice
   - runtime backend:
   - why selected:
   - expected RAM:
   - expected flash:
   - expected per-frame latency:
5. Integration boundary
   - detector API file:
   - preproc output format:
   - buffering strategy:
   - timestamp strategy:
6. Validation
   - near-field test result:
   - far-field test result:
   - non-speech false trigger result:
   - continuous speech segmentation result:
7. Issues
   - unresolved problem:
   - workaround:
   - next action:

## Rules
- Every import, conversion, and threshold decision must be recorded here.
- If a temporary script is used, its path and invocation must be recorded here.
- If a model file is regenerated, checksum and command must be updated here immediately.
- This file is required for future re-porting and for later replacement by a self-developed `VAD`.
