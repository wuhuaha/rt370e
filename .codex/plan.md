# Ameba River Plan

## Goal
Build a maintainable `RTL8730E` `ASR-first` voice home-control application that prioritizes wake-word and ASR quality, while keeping `AEC` and `VAD` replaceable by self-developed models later.

## Architecture Direction
1. `river_core`
   - app orchestration
   - state and intent routing
   - future dialogue/session coordinator
2. `river_voice`
   - `ASR-first` acoustic front-end
   - `AEC` as a pluggable barge-in capability
   - `VAD/KWS` as replaceable model adapters
3. `river_cloud`
   - online speech / NLU / home-control transport
   - backend provider switch point
4. `river_diag`
   - monitor commands
   - test injection and observability

## Step Plan
1. Bootstrap project, add `.codex` workflow, and create monitor echo + device-control skeleton.
2. Add a board-level mic-to-speaker audio echo path with fixed delay so the audio hardware chain can be validated independently.
3. Split the local voice path into `capture -> preproc -> detector -> router`, and land an `AFE-only` backend that is suitable for `RTL8730E`.
4. Refocus the product to `ASR-first` only and delete voice-side code paths that do not serve wake-word, ASR, or home-control.
5. Raise `AEC` priority and land it behind a stable `preproc` adapter so SDK `aivoice` can be replaced by a self-developed backend later.
6. Replace SDK `VAD` with `Silero VAD`, and keep a strict migration / reproduction record under `.codex`.
7. Add wake-word detection on top of the enhanced mono output instead of on raw array PCM.
8. Add beamforming / spatial metadata abstraction without coupling app logic to a specific SDK or model vendor.
9. Replace the cloud stub with a real online control client abstraction and request flow.
10. Keep `VAD/AEC/KWS` interfaces stable so future self-developed `DSP/TFLite Micro` models can be swapped in without touching app logic.

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
- Step 3.4 completed: the active local front-end strategy is pivoted back to `ASR-first`:
  - the running AIVoice policy now uses `AFE_FOR_ASR`
  - the default profile again centers on wake-word / ASR quality instead of communication echo control
  - runtime `AEC/ref` is no longer part of the main path; playback reference is kept only as staged infrastructure for a later optional barge-in profile
  - current tuning follows the SDK `ASR 2mic50mm` baseline: `SSL on`, `NS off`, `fixed AGC 10 dB`
- Step 4.0 planned: reset the roadmap around the user's final product priorities:
  - `ASR-first` only; remove voice-side code that does not contribute to wake-word / ASR / home-control
  - move `AEC` ahead of `VAD/KWS` in implementation priority, but keep it behind a replaceable backend boundary
  - stop using SDK `VAD` and migrate directly to `Silero VAD`
  - record the full `Silero VAD` migration process in a dedicated reproducibility document:
    - `/.codex/silero_vad_porting.md`
- Step 4.1 completed: voice-side cleanup and `ASR + AEC` minimal skeleton are now landed:
  - removed the old `speaker_test` code path from the build and boot flow
  - removed SDK `VAD/KWS/ASR` menu resources from the current project config because they are not the target direction
  - replaced the old ambiguous preproc state with explicit profiles:
    - `asr_mainline`
    - `asr_barge_in_aec`
  - current default profile is now `asr_barge_in_aec`, still based on `AFE_FOR_ASR`
  - the runtime logs now describe the real product direction instead of reporting `vad` before any detector exists
- Step 4.2 completed: `Silero VAD` migration staging has started:
  - added a first-class `river_voice_detector` boundary instead of letting future VAD logic leak into `echo` or `app`
  - staged `Silero VAD` as the default detector backend
  - fixed the first runtime choice to `TensorFlow Lite Micro` because `RTL8730E` SDK already ships it and this matches the later self-developed model direction
  - kept the detector runtime non-invasive for now:
    - no actual model blob is imported in this step
    - no runtime gating is added to the audio path in this step
  - recorded the first migration rule in `/.codex/silero_vad_porting.md`:
    - migrate the original model first
    - do not prune or quantize until measured resource pressure appears
- Step 4.3 completed: official `Silero VAD` upstream is now pinned into the repository:
  - downloaded official upstream repo and pinned commit `0dd0d85ee86b1f9d178dc26a04e60e90de26a80f`
  - selected official `silero_vad_16k_op15.onnx` as the first conversion source
  - vendored that exact artifact into `third_party/silero_vad/upstream/`
  - recorded the official streaming contract:
    - `512-sample` logical window
    - `64-sample` context
    - recurrent state `2 x batch x 128`
- Step 4.4 completed: host-side `Silero` conversion bring-up is now reproducible and the first direct path has been de-risked:
  - created a dedicated host conversion venv under the project instead of mixing conversion tools into the SDK environment
  - installed and pinned the first `onnx/onnxruntime/onnx2tf/tensorflow` conversion stack
  - corrected the actual official ONNX input contract:
    - current chunk `512`
    - rolling context `64`
    - real model input tensor `576`
  - verified that direct `onnx2tf` on the vendored `op15` graph is not yet stable:
    - base failure: `wa/model/stft/Conv`
    - after manual graph-specific transpose fixes: `wa/model/decoder/Squeeze`
- Step 4.5 completed: source-artifact hygiene and reconstruction scaffolding are now in place:
  - detected that host conversion tooling had mutated the vendored ONNX in place
  - restored `third_party/silero_vad/upstream/silero_vad_16k_op15.onnx` from the pinned upstream checkout
  - added a staging tool so future conversion runs always operate on a temporary copy instead of the vendored source
  - added a reconstruction-oriented tensor extractor that emits:
    - source tensor metadata
    - decoder `LSTM` tensors after the ONNX slice/concat layout
- Step 4.6 completed: the first direct embedded detector artifact now exists and is numerically verified:
  - rebuilt the pinned official ONNX in `TensorFlow` from extracted weights instead of forcing the old graph through `onnx2tf`
  - matched ONNX numerically at batch `1`:
    - output max abs diff `1.56e-08`
    - state max abs diff `1.67e-06`
  - exported a batch=`1` `TFLite` artifact:
    - `third_party/silero_vad/generated/silero_vad_16k_b1_fp32.tflite`
- Step 4.7 completed: the verified `Silero` artifact now runs inside the device-side detector backend:
  - `river_voice_detector_silero.cc` now embeds and executes `silero_vad_16k_b1_fp32.tflite` through `TFLite Micro`
  - the echo task now feeds enhanced mono `16 ms` frames into the detector while preserving detector/replay separation
  - diagnostics now expose `vad_prob_q15`, `vad=speech|silence`, and detector success/failure counters
  - first build-time resource baseline is now recorded before any compression decision:
    - `.tflite` artifact about `1.2 MB`
  - packaged app image about `2.8 MB`
  - `target_img2_ap.axf` text about `2.36 MB`
- Step 4.8 completed: project-owned flash profiles are now in place for the oversized development image:
  - copied the stock `RTL8730E NOR` profile into the project in decrypted form for traceability
  - created a development single-slot NOR profile that expands the combined app package range to `0x08600000`
  - added `tools/river_flash.py` so flashing can use the project profile without patching the SDK
  - added `tools/generate_rdev.py` so the encrypted `.rdev` stays reproducible from project JSON
- Step 4.9 completed: first real board-side `Silero` boot crash is now fixed at the detector runtime boundary:
  - root cause was an unconstructed `tflite::MicroMutableOpResolver` stored inside a zero-initialized C struct
  - fixed by explicit placement construction / destruction in `river_voice_detector_silero.cc`
  - also fixed tensor-arena free symmetry for both DRAM-typed and generic heap allocation fallbacks
- Step 4.10 completed: detector tensor binding is now aligned with the real exported `TFLite` artifact:
  - confirmed the batch=`1` artifact uses interpreter order:
    - input 0 = recurrent state
    - input 1 = audio `[1,576]`
    - output 0 = probability `[1,1]`
    - output 1 = next recurrent state
  - updated device-side binding to follow that order directly and emit a tensor inventory dump on any future mismatch
- Next recommended step:
  - flash the new build with the project flash wrapper and confirm `Silero` runtime now reaches `runtime ready`
  - capture near-field, far-field, and silence diagnostics using the new `vad_*` counters
  - measure whether `256 KB` tensor arena is sufficient under sustained runtime
  - only then decide whether compression is necessary
  - keep `aivoice AEC` inside the `asr_barge_in_aec` preproc profile while detector migration continues
- Step 4.11 completed: board runtime compatibility is now adjusted to the actual `RTL8730E` SDK `TFLite Micro` behavior:
  - on-device tensor `dims/name` metadata turned out to be unusable for this model even though tensor structs and types are valid
  - detector binding now validates fixed I/O order plus tensor buffer readiness instead of requiring runtime shape metadata
  - full `RTL8730E` image rebuild after this fix passed locally
- Step 4.12 completed: detector bring-up now works around missing top-level tensor buffers in the SDK runtime:
  - board logs showed all four `Silero` I/O tensors with:
    - valid pointers
    - valid `float32` type
    - correct `bytes`
    - but `data=NULL`
  - detector runtime now preserves eval tensors and patches fallback buffers into both eval and persistent tensor views when the SDK leaves those buffers unset
  - full local image rebuild after this workaround passed
- Next recommended step:
  - flash the newly rebuilt image and confirm the detector now reaches `silero_vad runtime ready`
  - if it does, collect silence / near-field / far-field `vad_prob_q15` diagnostics
  - if it still fails, inspect whether invoke-time tensor contents or arena pressure, not binding, is the next blocker
