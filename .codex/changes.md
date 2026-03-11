# Change Log

## Step 1
- Initialized external Ameba project `ameba-river`.
- Added project-level `AGENTS.md` and `.codex` workflow documents.
- Replaced default single-file example with layered directories for `core`, `voice`, `cloud`, `diag`, and `app`.
- Chose a custom staged skeleton instead of copying `speechmind` directly because `speechmind` is not exposed through the SDK `new-project -a` example path and would add too much bring-up risk for the first board-verification cycle.
- Added a first runnable feature set:
  - boot banner and status reporting
  - monitor command `river`
  - text echo path
  - simulated device control for `light`, `fan`, `curtain`, and `socket`
- Reserved stable interfaces for future local VAD, wake word, offline ASR, and real online control transport.

## Step 1.1
- Added a project-side CMake bootstrap to pre-generate `build_info.h` placeholders in `menuconfig/project_{ap,hp,lp}`.
- This works around an SDK parallel-build race seen on `RTL8730E`, where `wifi_tunnel_app` may compile before the SDK-generated `build_info.h` exists.

## Step 1.2
- Captured `RTL8730E` EVB board knowledge from the user-provided hardware guide into `.codex/knowledge.md`.
- Recorded bring-up-critical hardware facts for future implementation:
  - LOGUART and reset baseline
  - NOR/NAND and download implications
  - audio input and amplifier constraints
  - restricted GPIOs on `RTL8730EAM`
  - SWD and antenna rework notes
- Kept the original EVB PDF in `.codex` so project decisions can be traced back to the source document.

## Step 2
- Enabled the project-side audio framework and passthrough build options needed for board audio bring-up.
- Added `river_voice_echo_*` in `components/river_voice`:
  - `AudioRecord` capture from `AMIC1 + AMIC3`
  - `AudioTrack` playback to speaker
  - fixed `1000 ms` ring-buffer delay
  - short capture warm-up mute to reduce startup pop/noise
- Extended the `river` monitor command with:
  - `river audio start`
  - `river audio stop`
  - `river audio status`
  - `river audio echo <start|stop|status>`
- Kept the feature command-driven instead of auto-starting at boot so board validation can stay isolated and reversible.
- Verified the code path with a full local `RTL8730E` build after enabling audio framework support.
- Recorded the in-use board silk-screen as `EV730EA2 RO1` and kept the chip package / flash type as still-to-confirm hardware facts.

## Step 2.1
- Added serial-side audio echo diagnostics so capture and playback can be distinguished without changing the board wiring.
- Extended the monitor command with:
  - `river audio diag on`
  - `river audio diag off`
  - `river audio diag status`
- Added rolling `1 second` diagnostic logs from the echo task:
  - `cap_peak=[ch0,ch1]` for capture-side PCM peak
  - `play_peak=[ch0,ch1]` for delayed playback-side PCM peak
  - read/write success and failure counters
  - partial-read counter
- Exposed diagnostic state through `river audio status` so runtime state can be checked before and after starting the loop.
- Verified the diagnostic-enhanced build locally for `RTL8730E`.

## Step 2.2
- Added configurable boot-time audio echo autostart for board bring-up when monitor command injection is unavailable or unreliable.
- Added two project configs:
  - `CONFIG_RIVER_AUDIO_ECHO_AUTOSTART`
  - `CONFIG_RIVER_AUDIO_ECHO_DIAG_DEFAULT_ON`
- Enabled both in `prj.conf` for the current board-validation phase.
- Updated boot flow so `river_app_boot()`:
  - enables echo diagnostics at startup
  - starts the `1000 ms` delayed mic-to-speaker echo automatically
  - keeps printing `river` status after autostart so runtime state is visible from the boot log alone

## Step 2.3
- Fixed project Kconfig visibility for the `river` sources by including the generated `platform_autoconf.h` through `river_types.h`.
- This was required because the external-project compile flow passes the generated config header through include paths, not through per-file `-D CONFIG_*` flags.
- With this fix in place:
  - `CONFIG_RIVER_AUDIO_ECHO_AUTOSTART`
  - `CONFIG_RIVER_AUDIO_ECHO_DIAG_DEFAULT_ON`
  - `CONFIG_RIVER_DIAG_CMD_EN`
  - other `CONFIG_RIVER_*` switches
  now affect the compiled `river` code as intended.
- Fixed `river_diag_cmd.c` includes so the command module also builds when `CONFIG_RIVER_DIAG_CMD_EN` is truly enabled.

## Step 2.4
- Narrowed the board echo path to a lower-risk validation profile after runtime logs showed the digital chain was healthy but the replayed content was dominated by noise.
- Changed the echo profile to:
  - mono capture/playback
  - `AMIC3` only
  - lower speaker volume
  - lower mic boost
  - capture high-pass filter enabled
  - simple noise gate before the delayed replay buffer
- The goal of this step is to determine whether the EVB can produce an intelligible delayed voice replay before revisiting multi-mic raw playback.

## Step 2.5
- Added a direct speaker playback self-test based on the SDK `aplay` / `AudioTrack` path, so speaker output can be validated without involving microphone capture.
- Added `river_voice_speaker_test_*` in `components/river_voice`:
  - fixed dual-mono PCM playback to `DEVICE_OUT_SPEAKER`
  - repeating board-audible test pattern:
    - `1000 Hz` for `400 ms`
    - `200 ms` silence
    - `1500 Hz` for `400 ms`
    - `1000 ms` silence
  - lightweight serial diagnostics for playback progress
- Updated boot flow so this playback self-test can autostart independently of the echo path.
- Switched current `prj.conf` bring-up defaults from echo autostart to speaker-test autostart to isolate the analog output chain first.
- Recorded the board flash type as runtime-confirmed `NOR` from the boot log.

## Step 2.5.1
- Raised the direct speaker self-test output gain after the first board run proved playback existed but was too quiet for reliable evaluation.
- Aligned hardware volume with the SDK `aplay` default level range and increased PCM tone amplitude while keeping the same playback pattern.

## Step 2.6
- The direct speaker playback self-test is now board-proven, so the default bring-up path is switched back to mic-to-speaker echo.
- Current `prj.conf` defaults now:
  - enable echo autostart
  - enable echo diagnostics by default
  - disable speaker self-test autostart
- Raised echo playback hardware volume so delayed replay is easier to evaluate on the already-proven speaker path.
- Added an explicit echo gain log line at boot to keep runtime settings visible in serial output.

## Step 2.6.1
- Runtime diagnostics showed a healthy capture path and nonzero delayed playback PCM, but the board still produced no audible echo.
- To align echo with the already-proven direct playback route, the echo output path now uses:
  - `AMIC3` mono capture
  - mono delay buffer
  - dual-mono stereo speaker playback
  - explicit `AudioTrack_SetVolume(1.0, 1.0)`
- This step isolates whether the previous silence came from mono-track playback format mismatch rather than from capture failure.

## Step 2.6.2
- The dual-mono echo path became audible on the board, but replay loudness was still too low for practical evaluation.
- Raised echo playback output again:
  - hardware playback volume from `0.45` to `0.60`
  - added saturating PCM replay gain of `x4` before stereo duplication
- This keeps the microphone route unchanged and only increases delayed replay loudness.

## Step 2.7
- Reviewed SDK `aivoice` and `speechmind` before changing the board path:
  - `aivoice` explicitly supports `AFE_LINEAR_2MIC_30MM`, `50MM`, and `70MM`
  - `speechmind` on `AmebaSmart` uses `AFE_CONFIG_ASR_DEFAULT_2MIC50MM()`
  - the `EA` board routing in `speechmind` maps the first dual-mic pair to `AMIC1 + AMIC3`
  - `AMIC5` is also configured there as an extra raw channel
- Added an explicit board-array abstraction in `river_voice_board.*` so microphone routing and future AFE geometry stay out of the echo task itself.
- Current board baseline recorded in code:
  - board family: `EV8730EA2/EV730EA2`
  - active array: `linear-2mic-50mm`
  - active capture pair: `AMIC1 + AMIC3`
  - reserved auxiliary raw mic: `AMIC5`
- Refactored the echo path to validate dual-mic capture without forcing an early beamforming implementation:
  - capture is now `2 ch`
  - raw dual-mic PCM is downmixed to mono for the delay ring
  - delayed mono PCM is expanded back to dual-mono speaker playback
  - diagnostics keep reporting both capture channels independently
- Verified the new dual-mic-array build locally for `RTL8730E`.

## Step 2.8
- Tuned the raw dual-mic echo path for farther speech pickup and more useful board-side listening before AFE integration.
- Raised the board-array analog mic boost:
  - `AMIC1` from `15dB` to `20dB`
  - `AMIC3` from `15dB` to `20dB`
- Replaced simple `50/50` dual-mic averaging with a frame-local focused mix:
  - if one microphone is significantly stronger, mix weights become `3:1`
  - otherwise the path still behaves like a near-average mix
- Added lightweight per-frame AGC on the mixed mono debug signal:
  - target peak `6000`
  - max gain `x8`
- Reduced the pre-AGC noise gate threshold from `1024` to `256` so moderate-distance speech is less likely to be dropped entirely.
- Reduced fixed playback PCM gain from `x4` to `x2` because gain is now moved earlier into the adaptive mix stage.
- Verified the tuned build locally for `RTL8730E`.

## Step 3.0
- Started the first `RTL8730E`-appropriate AFE integration step without coupling the application to `speechmind`.
- Added `river_voice_capture.*`:
  - owns raw microphone-array capture only
  - uses board metadata from `river_voice_board.*`
  - currently outputs `16 kHz`, `16 ms`, `2 ch`, `PCM16`, matching AIVoice AFE input requirements
- Added `river_voice_preproc.*`:
  - defines a backend-neutral enhancement boundary
  - owns preproc frame metadata and backend context
  - is designed to keep future `self_dsp` or `TFLite Micro` replacement local to this layer
- Added `river_voice_preproc_aivoice.c`:
  - directly integrates SDK `aivoice_iface_afe_v1`
  - uses `AFE_CONFIG_ASR_DEFAULT_2MIC50MM()` as the starting point because this matches SDK `speechmind` on `AmebaSmart`
  - overrides the runtime policy for the current stage:
    - `ref_num = 0`
    - `enable_aec = false`
    - `enable_ns = false`
    - `enable_agc = true`
    - `enable_ssl = false`
- Updated board metadata so the voice frame cadence is `16 ms` instead of `20 ms`, matching `256 samples @ 16 kHz`.
- Refactored `river_voice_echo.c` into a debug sink on top of the new front-end:
  - input is now raw dual-mic capture
  - enhancement is now done by the AFE backend
  - delayed replay uses enhanced mono PCM expanded to dual-mono speaker output
  - diagnostics now separate:
    - raw capture health
    - preproc success/failure
    - playback write health
- Enabled the SDK-side voice resources needed by the current and next stages in `prj.conf`:
  - `AIVOICE`
  - `AFE 2MIC50MM`
  - baseline `VAD/KWS/ASR` resources for later steps
- Verified the refactored AFE-only build locally for `RTL8730E`.

## Step 3.1
- Tuned the `AFE-only` replay path for board-side intelligibility before adding VAD or AEC.
- Updated the SDK AFE runtime policy:
  - `NS` enabled
  - `NS` aggressiveness kept low to reduce noise without over-distorting speech
  - AFE fixed AGC gain raised from the default profile to `15 dB`
- Raised the board-side listening gain:
  - capture volume from `0x28` to `0x30`
  - speaker hardware volume from `0.65` to `0.80`
- Added a replay-only post-AFE adaptive gain stage in `river_voice_echo.c`:
  - target peak `12000`
  - max gain `x4`
  - silence gate `96`
- Added `afe_peak` to the serial diagnostics so the project can now distinguish:
  - raw mic energy
  - enhanced AFE output energy
  - final playback energy
- Verified the tuned build locally for `RTL8730E`.

## Step 4.7
- Integrated the verified `Silero VAD` artifact into the on-device `RTL8730E` runtime instead of leaving the detector in staged mode only.
- Added host-side model-data generation:
  - `tools/silero_vad/generate_model_data.py`
  - generated embedded model files:
    - `components/river_voice/generated/river_silero_vad_model_data.h`
    - `components/river_voice/generated/river_silero_vad_model_data.cc`
- Replaced the staged C detector shim with a real `TFLite Micro` runtime implementation:
  - `components/river_voice/river_voice_detector_silero.cc`
  - embeds `silero_vad_16k_b1_fp32.tflite`
  - uses official streaming semantics:
    - `256`-sample feed
    - `512`-sample decision window
    - `64`-sample rolling context
    - recurrent state `2 x 1 x 128`
- Added project configs for first board-side tuning:
  - `CONFIG_RIVER_SILERO_VAD_TENSOR_ARENA_KB=256`
  - `CONFIG_RIVER_SILERO_VAD_SPEECH_THRESHOLD_Q15=16384`
- Integrated detector execution into the active debug pipeline:
  - echo task now runs `detector_process()` on the enhanced mono frame before replay-only AGC/warmup changes it
  - boot/runtime diagnostics now report:
    - `vad_prob_q15`
    - `vad=speech|silence`
    - `vad_decisions`
    - `vad_speech`
    - `det_ok`
    - `det_fail`
- Kept detector output observational in this step:
  - no playback gating
  - no ASR routing decision yet
  - goal is first on-device `Silero` visibility, not policy coupling
- Added local C++-side compatibility handling instead of patching the SDK:
  - define missing `TFLITE_*` feature macros expected by the current SDK header set
  - suppress `unused-parameter` only for the local `river_voice` target so `TFLite Micro` headers compile cleanly under project `-Werror`
- Verified a full local `RTL8730E` build after runtime integration.
- Captured first resource baseline from the successful build:
  - embedded `.tflite` artifact size: about `1.2 MB`
  - `target_img2_ap.axf` size: about `14 MB`
  - `target_img2_ap.axf` sections:
    - `text=2355576`
    - `data=38868`
    - `bss=87680`
  - packaged app image `km0_km4_ca32_app.bin`: about `2.8 MB`

## Step 4.8
- Copied the `RTL8730E NOR` device profile into the project and turned it into a project-owned flash profile flow.
- Added project-side profile sources under `board/rtl8730e/profiles/`:
  - `RTL8730E_NOR.sdk.json`: decrypted SDK stock baseline
  - `RTL8730E_NOR.json`: project development profile source
  - `RTL8730E_NOR.rdev`: encrypted profile generated from the project JSON
- Added profile tooling:
  - `tools/generate_rdev.py`: regenerate encrypted `.rdev` from project JSON
  - `tools/river_flash.py`: project-owned flash wrapper that prefers the local profile over the SDK profile
- The development NOR profile expands the app package download range:
  - SDK stock: `0x08040000-0x08300000`
  - project dev: `0x08040000-0x08600000`
- Why this was required:
  - current `km0_km4_ca32_app.bin` size: `2929664`
  - SDK stock slot size: `2883584`
  - overflow: `46080`
- The new profile is explicitly marked as development-only because it consumes the OTA2 area for extra app space.

## Step 3.1.1
- Reverted the experimental detector-gated replay step after user feedback showed this direction was not wanted for the current phase.
- Kept the architecture prepared for future `VAD`, but restored the active runtime path to:
  - `capture -> preproc -> replay`
- Retuned the AFE/replay gain policy to reduce idle noise without introducing a new detector stage:
  - AFE fixed AGC gain reduced from `15 dB` to `9 dB`
  - AFE `NS` aggressiveness raised from `low` to `mid`
  - replay-side post-AGC reduced from `target12000/maxx4` to `target9000/maxx2`
- replay-side post-AGC floor raised from `96` to `192`
- frames below the post-AGC floor are now muted instead of being replayed as background hiss
- Verified the retuned build locally for `RTL8730E`.

## Step 3.2
- Added `river_voice_ref.*` as an independent playback-reference staging layer for future `AEC`.
- Current reference design:
  - source: delayed mono frame that is actually sent toward speaker playback
  - transport: project-side ring buffer
  - consumer boundary: `river_voice_preproc_process(preproc, mic, ref, ...)`
- Widened `river_voice_preproc` so the active backend interface can already accept optional reference PCM without binding application logic to SDK-specific `AEC` details.
- Kept `aivoice_afe` in `AFE-only` mode for this step:
  - reference is staged and observable
  - `AEC` is still disabled
- Extended echo diagnostics with playback-reference counters so the next `AEC` step can be validated from serial:
  - `ref_read_ok`
  - `ref_read_miss`
  - `ref_write_ok`
  - `ref_write_fail`
- Verified the staged-reference build locally for `RTL8730E`.

## Step 3.3
- Enabled `AEC` on top of the already-staged playback-reference path instead of changing the application-layer voice pipeline.
- Updated `river_voice_preproc_aivoice.c` so the active AIVoice feed frame is now interleaved as:
  - `mic0`
  - `mic1`
  - `playback_ref`
- Switched the runtime AFE policy to a communication-oriented profile:
  - `AFE_FOR_COM`
  - `enable_aec = true`
  - `ref_num = 1`

## Step 4.2
- Added `river_voice_detector.*` as a first-class local detector boundary for the upcoming `Silero VAD` migration.
- Added `river_voice_detector_silero.c` as the staged default backend:
  - current step validates interface shape and frame organization only
  - current step does not yet import or run the real model blob
- Fixed the detector-side cadence and window assumptions in code and docs:
  - input from `preproc`: mono `PCM16`, `16 kHz`, `256 samples / 16 ms`
  - staged detector window: `512 samples / 32 ms`
- Enabled `TFLite Micro` in project config so the runtime direction is explicit from the start.
- Extended boot/status logs so the current detector backend is visible:
  - `local_detector=silero_vad`
  - detector profile dump now reports `runtime=tflite_micro`
- Updated the dedicated `Silero` porting record with the first hard decisions:
  - migrate the original model first
  - do not prune or quantize in the first migration step
  - only compress later if actual flash / RAM / latency measurements require it

## Step 4.3
- Downloaded the official `Silero VAD` upstream repository and pinned commit `0dd0d85ee86b1f9d178dc26a04e60e90de26a80f`.
- Vendored the exact first conversion input into the project:
  - `third_party/silero_vad/upstream/silero_vad_16k_op15.onnx`
- Added upstream metadata and checksum tracking in:
  - `third_party/silero_vad/upstream/METADATA.md`
- Tightened the staged detector logs so the runtime now reports the selected official source model and the official streaming contract baseline.
  - `NS mid`
  - `adaptive AGC + fixed 5 dB`
  - `RES mid`
  - `SSL off`
- Kept the project-side front-end boundary unchanged:
  - `river_voice_capture` still owns raw dual-mic capture
  - `river_voice_ref` still owns the playback reference ring
  - `river_voice_preproc_process(preproc, mic, ref, ...)` remains the only place where SDK `AEC` is bound
- Updated boot-time logs so the running profile clearly states:
  - `capture dual-mic + 1ch ref -> AEC/AFE 1ch`
  - `audio echo ref ... aec=on`
- Verified the `AEC`-enabled build locally for `RTL8730E`.

## Step 3.4
- Pivoted the active front-end strategy from `COM/AEC` back to `ASR-first`, because the project goal is now explicitly to maximize wake-word and ASR quality.
- Updated `river_voice_preproc_aivoice.c` so the running AIVoice policy is now:
  - `AFE_FOR_ASR`
  - `ref_num = 0`
  - `enable_aec = false`
  - `enable_ns = false`
  - `enable_agc = true`
  - `enable_ssl = true`
  - `agc_fixed_gain = 10 dB`
  - `enable_adaptive_agc = false`
- Kept the playback-reference packing code in place as optional infrastructure, but removed it from the active default runtime path.
- Updated `river_voice_echo.c` so the debug replay path now reflects the real running profile:
  - `capture dual-mic -> ASR-AFE 1ch -> delayed dual-mono replay`
  - `audio echo ref ... aec=staged-off`
- Verified the `ASR-first` build locally for `RTL8730E`.

## Step 4.0
- Reset the implementation roadmap around the user's final product priorities:
  - only `ASR-first`
  - raise `AEC` priority
  - stop planning around SDK `VAD`
  - migrate directly to `Silero VAD`
- Added a dedicated reproducibility document target for `Silero VAD` migration:
  - `/.codex/silero_vad_porting.md`
- Declared that future self-developed `AEC/VAD` must plug into existing stable interfaces instead of leaking SDK-specific assumptions upward.

## Step 4.1
- Removed the old direct speaker self-test implementation from the active codebase:
  - deleted `components/river_voice/river_voice_speaker_test.c`
  - removed related declarations, build entries, boot hooks, and Kconfig items
- Simplified the project config so current voice resources reflect the actual roadmap:
  - kept `AIVOICE + AFE 2MIC50MM`
  - removed SDK `VAD`
  - removed SDK `KWS`
  - removed SDK `ASR` resource selection
- Added explicit preproc profiles on the stable `river_voice_preproc` boundary:
  - `asr_mainline`
  - `asr_barge_in_aec`
- Set the current default profile to `asr_barge_in_aec`, but kept the backend policy on the `AFE_FOR_ASR` side so the product remains `ASR-first`.
- Cleaned up runtime logs and status output so they now describe:
  - `asr-first` frontend intent
  - current preproc backend
  - current preproc profile
  - `Silero VAD` still pending

## Step 4.4
- Built a dedicated host-side Silero conversion environment under:
  - `/root/ameba-river/.venv-silero-convert`
- Pinned the first host conversion toolchain in that environment:
  - `onnx`
  - `onnxruntime`
  - `onnxsim`
  - `onnxoptimizer`
  - `onnx-graphsurgeon`
  - `onnx2tf`
  - `tensorflow`
  - `tf_keras`
- Corrected the staged detector contract and documentation:
  - `512` is the logical current window
  - `64` is the rolling context
  - `576` is the real official model input tensor
- Added a reproducible ONNX inspection tool:
  - `tools/silero_vad/extract_onnx_manifest.py`
- Added a generated manifest for the vendored official source model:
  - `third_party/silero_vad/upstream/silero_vad_16k_op15_manifest.json`
- Proved that direct `onnx2tf` conversion is currently unstable for the pinned `op15` graph:
  - direct probe fails at `wa/model/stft/Conv`
  - a manual transpose repair gets past `STFT` but then fails at `wa/model/decoder/Squeeze`
- Based on the official published `tinygrad` skeleton, the next migration path is now explicitly:
  - rebuild the official network structure in host-side `Keras`
  - load weights from the official ONNX graph
  - export that reconstructed model to `.tflite`

## Step 4.5
- Found and corrected a source-hygiene issue during host-side migration:
  - the vendored `silero_vad_16k_op15.onnx` had been modified in place by local tooling
  - restored it from the pinned upstream checkout so the repository copy is again the official baseline
- Added a guarded staging tool:
  - `tools/silero_vad/stage_conversion_source.py`
  - purpose: always copy the vendored ONNX into a temporary conversion path before running host tooling
- Added a reconstruction-oriented extractor:
  - `tools/silero_vad/extract_reconstruction_tensors.py`
  - purpose: emit source tensor metadata plus derived decoder `LSTM` tensors after the ONNX slice/concat layout
- Confirmed one important canonical-layout detail from the restored official ONNX:
  - `model.decoder.rnn.weight_ih`
  - `model.decoder.rnn.weight_hh`
  - `model.decoder.rnn.bias_ih`
  - `model.decoder.rnn.bias_hh`
  are top-level initializers in the real upstream artifact
- Added a generated reconstruction manifest:
  - `third_party/silero_vad/upstream/silero_vad_16k_op15_reconstruction_manifest.json`
- This step intentionally does not add a `.tflite` model yet.
  - it narrows the migration by proving that the pinned official source can now be treated as immutable
  - and that the next `Keras` reconstruction step has a reproducible tensor map to start from

## Step 4.6
- Added `tools/silero_vad/rebuild_tf_silero_vad.py`:
  - rebuilds the pinned official `Silero VAD` graph in `TensorFlow`
  - uses an explicit ONNX-style decoder `LSTM` implementation to avoid gate-order ambiguity
  - exports a batch=`1` `TFLite` artifact
- Verified the reconstructed TensorFlow model against official ONNX:
  - output max abs diff `1.5599653124809265e-08`
  - state max abs diff `1.6689300537109375e-06`
- Added generated artifacts:
  - `third_party/silero_vad/generated/silero_vad_16k_b1_fp32.tflite`
  - `third_party/silero_vad/generated/silero_vad_16k_b1_fp32_verification.json`
  - `third_party/silero_vad/generated/METADATA.md`
- This is the first project-side `Silero VAD` detector artifact that is both:
  - directly derived from the pinned official upstream model
  - numerically checked against the original ONNX

## Step 4.9
- Root-caused the first board-side `Silero VAD` boot crash after flashing the oversized image:
  - the fault occurred inside `river_voice_detector_silero_open`
  - the crash address mapped into `tflite::MicroMutableOpResolver<16>::AddBuiltin`
  - the actual bug was object lifetime, not model size or flash layout
- Fixed `river_voice_detector_silero.cc` so C++ runtime objects are constructed and destroyed correctly:
  - explicitly placement-construct `op_resolver` before registering kernels
  - explicitly destroy `op_resolver` on every early-return and close path
  - make tensor-arena freeing symmetric for both allocation backends
- Revalidated the code-side fix locally:
  - `river_voice_detector_silero.o` rebuilds successfully with `CCACHE_DISABLE=1`
  - the earlier build interruption was due to host `ccache` permission on `/run/user/0`, not due to this source change

## Step 4.10
- Collected the next board-side runtime failure after the constructor fix:
  - detector now gets past the previous data abort
  - boot log reaches `silero_vad tensor binding failed`
- Cross-checked the exported `.tflite` artifact in the host conversion venv:
  - input 0: `serving_default_state:0` `[2,1,128]` `float32`
  - input 1: `serving_default_input:0` `[1,576]` `float32`
  - output 0: `PartitionedCall:0` `[1,1]` `float32`
  - output 1: `PartitionedCall:1` `[2,1,128]` `float32`
- Updated the device runtime binding logic accordingly:
  - bind inputs and outputs by the pinned artifact's actual interpreter index order first
  - keep shape-based fallback only as a secondary path
  - accept scalar-like probability outputs as either `[1]` or `[1,1]`
  - dump the interpreter I/O tensor inventory on binding failure so future mismatches are immediately visible on serial logs
- Revalidated the changed detector component locally:
  - `CCACHE_DISABLE=1 cmake --build ... --target river_voice_target_img2_ap` passes

## Step 4.11
- Collected the next board-side tensor inventory from the real `RTL8730E` SDK `TFLite Micro` runtime:
  - `inputs=2`, `outputs=2` are reported correctly
  - tensor structs are non-null
  - tensor `type=float32` is still valid
  - but tensor `dims` and `name` metadata are not usable on-device for this model
- Updated `river_voice_detector_silero.cc` to stop depending on tensor shape metadata at runtime:
  - trust the pinned interpreter I/O order first
  - validate tensor buffer availability through:
    - `data` pointer
    - `bytes` lower bound
    - element type
  - keep failure logs for `data` and `bytes` so future SDK/runtime differences remain diagnosable
- Rebuilt the full `RTL8730E` image successfully after this compatibility fix.
- Current expected next board behavior:
  - detector should advance past `silero_vad tensor binding failed`
  - next useful milestone log is `silero_vad runtime ready`

## Step 4.12
- Collected the next board-side `Silero` failure from the updated build:
  - tensor structs are present
  - tensor `bytes` are non-zero and match the expected payload sizes
  - but top-level `TfLiteTensor.data` is still `NULL` for all model I/O on this SDK/runtime snapshot
- Root-caused the remaining bring-up blocker:
  - the current `RTL8730E` `TFLite Micro` runtime can expose valid I/O tensor handles while leaving their top-level `data` pointers unset
  - this means detector binding cannot rely on `interpreter->input()/output()` alone even after `AllocateTensors()`
- Updated `river_voice_detector_silero.cc` to add an SDK-compatibility fallback:
  - construct the interpreter with `preserve_all_tensors=true`
  - fetch the corresponding `TfLiteEvalTensor` handles for the pinned I/O order
  - patch missing I/O buffers with detector-owned fallback storage:
    - audio input -> `audio_input_buffer`
    - state input -> `recurrent_state`
    - probability output -> `probability_output_buffer`
    - state output -> `next_state`
  - mirror eval-tensor buffers back into the persistent `TfLiteTensor` views when the SDK leaves them empty
- Revalidated after the fallback-buffer change:
  - `CCACHE_DISABLE=1 cmake --build /root/ameba-river/build_RTL8730E/build --parallel --target river_voice_target_img2_ap` passed
  - full `RTL8730E` rebuild also passed locally

## Step 4.13
- Collected the next board-side `Silero` log after fallback-buffer patching:
  - top-level persistent tensors now show the expected:
    - `dims`
    - `data`
    - `bytes`
  - detector still failed during open
- Root cause refinement:
  - the remaining open-time guard was still validating `TfLiteEvalTensor` byte lengths
  - that check is stricter than what the runtime actually needs for `Invoke()`
  - on this SDK snapshot it can reject a usable interpreter state even when the persistent I/O views are already valid
- Updated `river_voice_detector_silero.cc` again:
  - relaxed eval-tensor validation to require only:
    - non-null eval tensor handle
    - `float32` type
    - non-null `data`
  - kept the stronger payload-size checks only on the persistent tensors that the detector actually reads and writes directly
- Revalidated after the guard relaxation:
  - `CCACHE_DISABLE=1 cmake --build /root/ameba-river/build_RTL8730E/build --parallel --target river_voice_target_img2_ap` passed
  - full image rebuild was started immediately after the targeted rebuild

## Step 4.14
- Collected the next board-side `Silero` log from commit `8a7036f`:
  - persistent I/O tensors now report valid:
    - `dims`
    - `data`
    - `bytes`
  - detector still fails in open before `runtime ready`
- Root cause refinement:
  - the remaining blocker is no longer persistent-tensor binding
  - `eval tensor` availability is still SDK-specific and should not be treated as a hard prerequisite for detector open
  - the detector runtime path itself reads and writes through the persistent tensors
- Updated `river_voice_detector_silero.cc`:
  - keep best-effort eval-tensor acquisition and patching
  - downgrade eval-tensor validation from hard failure to diagnostic warning:
    - log `silero_vad eval tensor state degraded: ...` when eval views are missing or incomplete
  - keep hard open-time rejection only for persistent tensors that the detector actually uses directly
- Revalidated after the change:
  - targeted `river_voice_target_img2_ap` rebuild passed locally

## Knowledge Update
- Added an external-reference section to `.codex/knowledge.md` covering three project families relevant to future voice work:
  - Realtek `ambd_arduino` `micro_speech`
  - Google `tflite-micro` `micro_speech`
  - ARM `ML-embedded-evaluation-kit`
- Recorded not just the links but the intended usage boundary for each:
  - which one is best for Ameba-specific audio / TFLM integration
  - which one is best for upstream pipeline architecture
  - which one is best kept as an optimization reference instead of direct reusable code

## Step 4.15
- Collected the next board-side `Silero` log from the image built at `2026-03-11 16:44:00`:
  - persistent tensors now report valid:
    - shapes
    - non-null `data`
    - expected payload sizes
  - but all four top-level tensors still report `type=0`
  - detector still fails in open before `runtime ready`
- Root cause refinement:
  - on this `RTL8730E` SDK snapshot, top-level `TfLiteTensor` / `TfLiteEvalTensor` views can arrive as `kTfLiteNoType`
    even when the buffers themselves are valid and usable
  - the remaining blocker is the detector's type guard, not tensor ownership or byte length
- Updated `river_voice_detector_silero.cc`:
  - treat `kTfLiteNoType` as a compatible transient SDK state for `Silero` top-level I/O binding
  - normalize both persistent and eval tensor type fields to `kTfLiteFloat32` during patch-up
  - keep hard rejection only for:
    - null tensor handle
    - null data pointer
    - insufficient payload bytes
- Next expected milestone:
  - boot should advance past `silero_vad tensor binding failed`
  - next useful log should be `silero_vad runtime ready: ...`

## Knowledge Update
- Triaged an additional external `Float32-first Silero deployment` reference into `.codex/knowledge.md`.
- Recorded the parts that are valid for current `RTL8730E` work:
  - keep `Float32` first
  - measure real arena / latency / heap before compression
  - keep `int16 -> float32` normalization and simple state copy
- Recorded the parts that need correction for this project:
  - `CA32` uses `-mfpu=neon -mfloat-abi=hard`, not `KM4`'s `fpv5-sp-d16`
  - cache-maintenance guidance is relevant, but `Cortex-M` APIs are not drop-in for current `CA32`
  - `AllOpsResolver` is not preferred for current embedded budget
- Added a standing rule:
  - future external references should be triaged directly into `.codex/knowledge.md`
  - store actionable conclusions rather than raw prose

## Tag Update
- Added milestone note:
  - `.codex/tags/m1-silero-vad-runtime-ready.md`
- Purpose of this tag note:
  - freeze the first verified `RTL8730E` milestone where:
    - `Silero VAD` reaches `runtime ready`
    - on-device detector decisions are produced
    - the project is ready to move from runtime bring-up into policy tuning

## Step 4.16
- Added a board RGB indicator module:
  - `include/river/river_board_rgb.h`
  - `components/river_voice/river_board_rgb.c`
- Initial implementation attempted to reuse the SDK `LEDC` / `WS2812` example path.
- Added project config:
  - `CONFIG_RIVER_BOARD_RGB_VAD_INDICATOR_EN`
- Enabled the RGB VAD indicator in `prj.conf`.
- Wired state changes into the current voice runtime:
  - app boot sets `boot`
  - echo task sets `silence` once running
  - detector decision toggles `silence` / `speech`
  - detector/open/autostart failures set `error`
  - echo stop sets `off`
- Recorded the board assumption and risk boundary in `.codex/knowledge.md` so later schematic confirmation or pin remap can be traced cleanly.

## Step 4.17
- Collected board feedback after the first RGB/VAD-status experiment:
  - `Silero VAD` was active, but status flickered between `speech` and `silence` too aggressively for a stable indicator
  - the RGB LED never lit even though the software path initialized
- Refined the `Silero VAD` post-processing in `river_voice_detector_silero.cc`:
  - lowered speech-enter threshold from `16384` to `12000`
  - added explicit speech-exit threshold `4500`
  - added hangover of `8` detector decisions
  - added simple EMA smoothing with shift `2`
  - changed exported `speech_probability_q15` to the smoothed probability so diagnostics match the actual decision logic
- Replaced the earlier `WS2812` assumption in `river_board_rgb.c` with a deferred board-status stub.
- Root cause for the RGB mismatch:
  - official EVB documentation indicates `USER LED` is a passive RGB circuit using `LEDR/LEDG/LEDB`, not a serial `WS2812`
  - documentation also says `R25`, `R27`, and `R31` should be populated to use the `USER LED` circuit
- Current runtime behavior:
  - the board RGB API remains in place for future use
  - boot now prints a one-time deferred warning instead of a false `rgb indicator ready`

## Step 4.18
- Added a diagnostic-only SDK VAD reference path:
  - `include/river/river_voice_vad_reference.h`
  - `components/river_voice/river_voice_vad_reference.c`
- Reference path policy:
  - uses `aivoice_iface_vad_v1`
  - feeds the same post-AFE enhanced mono `256-sample / 16ms` audio that `Silero` sees
  - does not affect the main `Silero` decision or any future product logic
  - exists only to compare detector behavior in the same runtime input chain
- Extended `Silero` detector diagnostics:
  - added raw probability export alongside the smoothed decision probability
  - diagnostics can now distinguish:
    - raw model output
    - smoothed / hysteresis decision probability
    - SDK VAD event state
- Updated board RGB handling:
  - runtime no longer assumes `PA_9 + WS2812`
  - current board RGB path remains intentionally deferred until `LEDR/LEDG/LEDB` mapping is confirmed
