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
3. Split the local voice path into `capture -> preproc -> detector -> router`, and land an `AFE-only` backend that is suitable for `RTL8730E`.
4. Add VAD on top of the enhanced single-channel output instead of on raw array PCM.
5. Add playback reference routing and then enable AEC on the same front-end interface.
6. Add beamforming / spatial metadata abstraction without coupling app logic to a specific SDK.
7. Replace the cloud stub with a real online control client abstraction and request flow.
8. Add wake word and offline ASR adapters behind stable local interfaces.
9. Build online/offline fusion and preserve a clean path to self-developed `DSP/TFLite Micro` replacement.

## Current Step
- Step 2 completed: command-driven board audio echo bring-up is implemented and builds for `RTL8730E`.
- Step 2.1 completed: serial diagnostics are available to separate capture-side failure from playback-side failure during board bring-up.
- Step 2.2 completed: boot-time echo autostart is enabled so board audio can be validated even when monitor command registration is not usable on the target.
- Step 2.3 completed: `river` project Kconfig symbols now propagate into the compiled sources through `platform_autoconf.h`.
- Step 2.4 completed: mono `AMIC3` echo narrowed the mic side, but runtime results still did not produce clean speech.
- Step 2.5 completed: direct `AudioTrack` speaker playback is proven on the user's board.
- Step 2.6 in progress: reintroduce echo on top of the proven speaker path and continue narrowing microphone routing / raw capture quality.
- Step 2.6 completed: mono `AMIC3` echo became audible after aligning playback format and gain with the proven speaker path.
- Step 2.7 completed: the board voice path is now aligned with SDK `speechmind` / `aivoice` dual-mic baseline:
  - board array metadata is modeled explicitly
  - current array geometry is `linear-2mic-50mm`
  - echo capture now uses `AMIC1 + AMIC3`
  - delayed replay uses a downmixed mono debug path so future beamforming / AFE integration can replace the mix stage cleanly
- Step 2.8 completed: the raw dual-mic debug path is tuned for farther speech pickup before AFE integration:
  - `AMIC1 + AMIC3` boost raised from `15dB` to `20dB`
  - dual-mic mix is no longer plain averaging; it now biases toward the stronger mic each frame
  - a lightweight per-frame AGC lifts low-level speech before replay
  - noise gate is lowered so farther speech is less likely to be muted
- Step 3.0 completed: the local voice path is now refactored for `RTL8730E` AIVoice AFE integration:
  - added `river_voice_capture` as a reusable raw-array capture layer
  - added `river_voice_preproc` as a backend-neutral enhancement boundary
  - added `river_voice_preproc_aivoice` using SDK `aivoice_iface_afe_v1`
  - aligned the board frame size to `256 samples / 16 ms`, which is the AIVoice-required input cadence
  - switched the debug echo path from raw dual-mic mix replay to `dual-mic capture -> AFE enhanced mono -> delayed dual-mono replay`
  - kept `AEC` disabled for now because there is still no dedicated playback reference path
- Step 3.1 completed: the AFE-only replay path is tuned for clearer debug listening before VAD/AEC integration:
  - AFE `NS` is enabled in low-aggressive mode
  - AFE fixed AGC gain is raised to `15 dB`
  - echo replay now applies a light post-AFE adaptive gain stage
  - diagnostics now expose `afe_peak` to separate AFE output strength from raw capture and playback gain
- Step 3.1.1 completed: the experimental detector gate is rolled back and the AFE-only path is retuned for lower idle noise:
  - the runtime path returns to `capture -> preproc -> replay`
  - AFE fixed AGC is reduced from `15 dB` to `9 dB`
  - AFE `NS` aggressiveness is raised from `low` to `mid`
  - replay-side post-AGC is tightened from `target12000/maxx4/floor96` to `target9000/maxx2/floor192`
  - frames below the replay floor are muted directly instead of being replayed as idle hiss
- Step 3.2 completed: playback-reference plumbing is now staged for future `AEC` without changing the current `AFE-only` runtime policy:
  - added an independent `river_voice_ref` ring buffer for speaker-reference PCM
  - the active echo task now publishes the actual delayed mono playback frame into that reference ring
  - `river_voice_preproc` is widened to accept optional reference audio on the same stable interface that future `AEC` will use
  - `AEC` remains disabled in this step, so the current backend still behaves as `AFE-only`
- Step 3.3 completed: `AEC` is now enabled through the existing `preproc(mic, ref)` boundary:
  - `river_voice_preproc_aivoice` now packs `AMIC1 + AMIC3 + playback_ref` into the SDK AIVoice feed frame
  - the active AFE policy is switched from `ASR`-style enhancement to `COM`-style enhancement with `AEC + NS + adaptive AGC`
  - the application still only knows `capture -> preproc -> replay`; SDK `AEC` details remain local to the preproc backend
  - diagnostics from Step `3.2` are kept so the new reference-driven path can be validated before any beamforming or VAD work
- Next recommended step:
  - validate that the `AEC` path reduces near-end replay noise and speaker leakage without killing far-field speech
  - if reference timing is stable, add an explicit `beamforming-ready` mode boundary inside `preproc`
  - keep `VAD` as a separate later step; do not couple it back into the current `AEC` debug loop
