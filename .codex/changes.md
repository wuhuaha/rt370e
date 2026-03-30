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

## Step 4.21
- Added `river_voice_segment_buffer.*` as a reusable speech-segment cache module for the next online ASR step.
- `vad_probe` now writes enhanced mono frames into that segment buffer and emits:
  - ready-segment size
  - ready-segment duration
  - completed / dropped segment counters
- Added `river_voice_segment_sink.*` as a stable handoff boundary between buffered local speech segments and future online ASR transport.
- Current sink backend is `online_asr_stub`, so the buffered path is exercised without coupling the voice path to a cloud implementation yet.
- The default pure-VAD path now keeps buffered context around speech:
  - pre-roll `384 ms`
  - post-roll `768 ms`
  - max segment `8000 ms`
- Lowered `Silero` decision thresholds to favor recall over precision:
  - enter threshold `9000`
  - exit threshold `2500`
  - hangover `10`
  - EMA shift kept at `1` for fast reaction
- Reduced the pure-probe diagnostic window to about `96 ms` and added:
  - `vad_start`
  - `vad_end`
  - segment prebuffer / post-roll state
- Added a dedicated Chinese migration record:
  - `/.codex/silero_vad_migration_zh.md`
- The Chinese migration note now explains:
  - official upstream selection
  - host conversion path
  - device runtime integration
  - `RTL8730E SDK` compatibility fixes
  - flash profile changes
  - reproduction and audit procedure
    - `text=2355576`
    - `data=38868`
  - `bss=87680`
  - packaged app image `km0_km4_ca32_app.bin`: about `2.8 MB`

## Step 5.0
- Replaced the old cloud-ASR stub with a real provider framework under `components/river_cloud`.
- Added `river_wifi_station.*`:
  - STA auto-connect task
  - project-local temporary credentials in `include/river/river_wifi_credentials.h`
  - retry and status logging for field bring-up
- Expanded `include/river/river_cloud.h` from a text-stub surface into a provider-neutral ASR contract:
  - audio-open / audio-close
  - streaming frame push
  - batch segment submit
  - partial/final/error/session result callback
- Added `river_cloud_adapter.c`:
  - project-owned bridge between local VAD output and cloud providers
  - provider-neutral result fan-out back into `river_core`
  - streaming pre-roll / post-roll handling so provider code does not own local VAD policy
  - SNTP/UTC readiness gating for signed cloud requests
- Added provider-internal registry and ops boundary:
  - `river_asr_provider_internal.h`
  - `river_asr_provider_registry.c`
  - provider choice is no longer hardcoded in the adapter body
- Added the first real provider implementation:
  - `river_asr_iflytek_rtasr.c`
  - target service:
    - iFlytek RTASR LLM WebSocket API
  - implemented pieces:
    - query-string auth signing with `HMAC-SHA1 + Base64 + URL-encode`
    - WebSocket session open / binary PCM feed / finish
    - JSON result parsing through `cJSON`
    - partial/final/error/session callbacks
  - temporary credentials are stored in:
    - `include/river/river_asr_iflytek_credentials.h`
- Connected the local speech path to cloud ASR:
  - `vad_probe` now opens the cloud audio bridge
  - enhanced mono frames are pushed to the streaming bridge on every detector decision
  - `river_core` now logs:
    - session started
    - partial result
    - final result
    - error
    - session closed
- Kept non-streaming architecture in place for future providers:
  - `segment_buffer` still produces ready utterance segments
  - `segment_sink` now bridges those segments into `river_cloud_asr_batch_submit_segment()`
  - current iFlytek provider still reports `batch unsupported`
- Added Chinese integration / audit record:
  - `/.codex/iflytek_rtasr_integration_zh.md`

## Step 5.0.1
- Tightened the segment/batch accounting so unsupported provider batch upload is no longer misreported as a successful submit.
- `river_cloud_asr_batch_submit_segment()` now returns `RIVER_ERR_UNSUPPORTED` transparently.
- `river_voice_segment_sink` now tracks:
  - submitted segments
  - unsupported segments
  - real failures
- `vad_probe` diagnostics now distinguish:
  - `seg_unsupported`
  - `seg_fail`

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

## Step 4.19
- Triaged an external `TFLite Micro` initialization reference into project knowledge instead of applying it blindly.
- Recorded the parts that are directly useful for current `Silero VAD` bring-up:
  - schema-version guard
  - defensive `AllocateTensors()` failure handling
  - explicit `TensorArena` usage reporting
  - `float32 first, optimize later`
- Recorded the parts that need project-specific correction:
  - current project uses `MicroMutableOpResolver`, not `AllOpsResolver`
  - current runtime runs on `RTL8730E` `CA32`, so generic Cortex-M FPU flags are not directly applicable
  - generic `SCB_InvalidateDCache_by_Addr()` examples are not the current CA32 cache API
  - dynamic tensor-arena allocation needs allocator-level alignment scrutiny rather than static-array assumptions

## Step 4.20
- Switched the default validation runtime from echo replay to a pure detector probe path:
  - added `components/river_voice/river_voice_vad_probe.c`
  - added new public control APIs in `include/river/river_voice.h`
  - added `river audio probe <start|stop|status>` monitor handling
- Changed default boot mode:
  - disabled `CONFIG_RIVER_AUDIO_ECHO_AUTOSTART`
  - enabled `CONFIG_RIVER_VAD_PROBE_AUTOSTART`
  - selected `CONFIG_RIVER_VAD_PROBE_DIAG_DEFAULT_ON`
  - switched preproc validation from `asr_barge_in_aec` to `asr_mainline`
- Probe-path design:
  - `capture -> aivoice_afe(asr_mainline) -> silero + sdk_vad_ref -> serial diagnostics`
  - no speaker playback
  - no delay ring
  - no playback reference
  - no `AEC` in the validation path
- Raised diagnostic cadence from roughly `1s` windows to about `240ms` windows in the probe runtime so short utterances are easier to catch in serial logs.

## Step 4.22
- Added a shared project-owned logging abstraction:
  - `include/river/river_log.h`
  - `components/river_common/river_log.c`
  - `components/river_common/CMakeLists.txt`
- Logging policy is now layered instead of direct `printf` in product modules:
  - `ERROR`
  - `WARN`
  - `INFO`
  - `DEBUG`
- The default runtime level is now `INFO` through:
  - `Kconfig`
  - `CONFIG_RIVER_LOG_LEVEL_INFO=y` in `prj.conf`
- Log lines now carry a monotonic millisecond timestamp and stable tag:
  - format:
    - `[%010lu][<level>][<tag>] ...`
- `vad_probe` logging policy is now split into:
  - `INFO` only when VAD state changes
  - `DEBUG` for high-rate probe and segment diagnostics
- The new logger keeps a secondary sink abstraction for future:
  - file persistence
  - upload / relay
  - alternate transports
  These sinks are not implemented yet, but the boundary is now explicit.
- Refactored key voice/cloud modules to use the shared logger so background runtime output is more readable and easier to filter during bring-up.

## Step 4.23
- Hardened the diagnostic SDK VAD side path against heap exhaustion during online-ASR bring-up:
  - added `CONFIG_RIVER_AIVOICE_VAD_REFERENCE_MIN_FREE_HEAP_KB`
  - defaulted it to `320KB`
- `river_voice_vad_reference_open()` now checks current free heap before creating `aivoice_iface_vad_v1`.
- If free heap is below the configured floor, the SDK VAD reference is skipped with a clear warning instead of triggering a low-level `Malloc failed` path inside the vendor library.
- Updated upper-layer logs so the behavior is explicit:
  - `sdk_vad reference auto-disabled; keep silero-only decision logging`
- This keeps:
  - `Silero VAD`
  - pure VAD probe
  - cloud ASR bridge
  available even when the optional SDK comparison path is too expensive for the current heap budget.

## Step 4.24
- Hardened `vad_probe` against a second large heap consumer in the batch-segment path.
- Root cause:
  - `river_voice_vad_probe_prepare_buffers()` always opened `segment_buffer`
  - the current `iflytek_rtasr` provider is `stream=yes batch=no`
  - so the `8s` segment buffer was being allocated even though the provider could not consume it
  - this matched the runtime `Malloc failed ... xWantedSize:256064`
- Updated behavior:
  - if the active cloud provider does not support batch ASR, `vad_probe` now disables `segment_buffer` and keeps only the streaming bridge active
  - if a future provider does support batch, `vad_probe` now also checks free heap headroom before opening the batch segment buffer
  - if heap headroom is insufficient, batch buffering is skipped with a clear warning and the system continues in stream-only mode
- Added `CONFIG_RIVER_VAD_PROBE_SEGMENT_MIN_FREE_HEAP_KB` with default `64KB`
- Runtime status / diagnostics now expose whether the segment path is:
  - `enabled`
  - or `disabled`
- This preserves the architecture for future non-streaming ASR while removing a large, currently unnecessary heap allocation from the live `iflytek_rtasr` path.

## Step 4.25
- Hardened STA Wi-Fi bring-up against repeated auth / 4-way / busy failures observed while connecting to `Keeu`.
- Updated [river_wifi_station.c](/root/ameba-river/components/river_cloud/river_wifi_station.c) to use a multi-strategy connect policy:
  - try the most compatible path first: `SSID + password` only
  - if scan data is available, retry with scan-bound `BSSID + channel + security`
  - if the scanned AP reports `WPA2/WPA3 mixed`, also try a final `WPA2 AES` compatibility fallback
- The scan candidate selector now prefers `2.4G` APs over `5G` when the SSID is duplicated across bands, which is better aligned with embedded voice bring-up and home-router compatibility.
- Added explicit disconnect-and-idle waiting between retries so the driver has time to leave transitional join states before the next attempt.
- Failure logs now show the strategy name, decoded error reason, and current join state to make router-compatibility issues easier to identify on the next board run.
- Follow-up hardening:
  - explicitly disable SDK fast-connect in addition to SDK auto-reconnect
  - when `wifi_connect()` returns `-RTK_ERR_BUSY`, do not immediately declare failure; instead wait for the in-flight join flow to complete and adopt the connection if it succeeds
  - if the driver reaches `RTW_JOINSTATUS_SUCCESS` before IPv4 is assigned, the app now requests DHCP and completes the join instead of disconnecting and restarting
- This addresses the observed case where the SDK background path already printed `[$]wifi connected` but the app still treated the attempt as failed and tore it down.

## Step 4.26
- Selectively adopted the `REVIEW.md` guidance around Wi-Fi lifecycle ownership and startup race avoidance.
- Additional STA state-machine hardening in [river_wifi_station.c](/root/ameba-river/components/river_cloud/river_wifi_station.c):
  - disable SDK fast-connect before WLAN init, not only after the STA task starts
  - disable SDK LPS during bring-up to reduce auth / association instability while debugging connectivity
  - wait for the join state machine to return to an idle/disconnected state before launching a site survey or a fresh connect attempt
  - if a scan candidate is available, prefer `scan_exact` first and keep `basic` as a later fallback, instead of always trying the unconstrained path first
  - retry a busy site-survey once after waiting for driver idle
- Rationale:
  - the previous order still allowed driver-level background activity to race with the app's own connect flow
  - using the scanned `BSSID + channel + security` first is more deterministic for home-router debugging than restarting from the least constrained path each time
- Added root-level [TIPS.md](/root/ameba-river/TIPS.md) as the first-stop runtime troubleshooting guide for recurring serial-log signatures such as `auth_fail`, `busy`, and provider bring-up issues.
- Added a dedicated active Wi-Fi issue record in [.codex/issues.md](/root/ameba-river/.codex/issues.md) so transient field failures can be tracked with symptoms, mitigations, and closure criteria.
\n## Step 5\n- **Asynchronous Architecture Overhaul**: Implemented river_cloud_wk worker and Ring Buffer system.\n- **IPC Protection**: Task priority tuning to resolve net_connect -82 and IPC timeouts.\n- **Embedded Hardening**: Decomposed WS URL init and Multi-AP failover.\n- **Memory Audit**: Integrated vPortGetHeapStats for fragmentation tracking.

## Step 5.1
- Switched to branch `xiaozhi` and ran a full `RTL8730E` build at commit `43737ec`.
- This step is verification-only:
  - no firmware source files were changed
  - the goal was to confirm that the current `xiaozhi` branch still produces complete images
- Full build completed successfully and produced:
  - `build_RTL8730E/km4_boot_all.bin`
  - `build_RTL8730E/km0_km4_ca32_app.bin`
  - `build_RTL8730E/ota_all.bin`
- Current image sizes from this run:
  - `km4_boot_all.bin`: `51872`
  - `km0_km4_ca32_app.bin`: `3605856`
  - `ota_all.bin`: `3605888`

## Step 5.2
- On branch `DS-CNN`, completed a focused migration assessment of the DS-CNN training stack under `/root/kws-training-pro` against the current `ameba-river` board runtime contract.
- Added [DSCNN_KWS_TRAINING_PRO_MIGRATION_REPORT_ZH.md](/root/ameba-river/DSCNN_KWS_TRAINING_PRO_MIGRATION_REPORT_ZH.md) as the formal report for this review.
- Core conclusion captured in the report:
  - the DS-CNN architecture and teacher-student methodology are worth reusing
  - the existing student weights should **not** be treated as directly deployable on the current board
  - the biggest blocker is feature-contract mismatch, not network topology
- High-signal findings recorded:
  - `/root/kws-training-pro` contains both an old `40x101` path and a newer `40x98` path
  - the newer `train_dscnn_v2.py` still trains with `extract_from_array()`, which does not match the current board-side `river_voice_kws.cc` frontend exactly
  - the current KD loss in `train_dscnn_v2.py` is not a robust production-quality formulation for a binary wakeword student
  - the repo's validation script still validates the older `dscnn_v1` path and cannot be used as the final board deployment gate
- The report also includes a phased landing plan:
  - freeze board feature truth
  - import architecture only
  - rebuild the student training chain under the current board contract
  - unify evaluation
  - export int8 TFLite and validate on board

## Step 5.3
- On branch `DS-CNN`, completed a second migration assessment focused on `/root/river-openwakeword-lab` as the current external OpenWakeWord + DS-CNN training workspace.
- Added [RIVER_OPENWAKEWORD_LAB_MIGRATION_REPORT_ZH.md](/root/ameba-river/RIVER_OPENWAKEWORD_LAB_MIGRATION_REPORT_ZH.md) as the formal report for this review.
- Core judgment captured in the report:
  - `/root/river-openwakeword-lab` has substantially higher migration value than `/root/kws-training-pro`
  - the board-aligned student feature chain, student trainer, and int8 export path are worth reusing directly
  - the current workspace is engineering-mature but still not deployment-ready in model quality
- High-signal findings recorded:
  - the student extractor in `river-openwakeword-lab` now matches the current board `98x40 log-mel` contract much more closely than the older `kws-training-pro` implementation
  - `round6` student artifacts prove `train -> int8 tflite -> packaging` is already reproducible
  - the current best student remains `no-deploy` because board negative FPR is still too high
  - `assistant / KD / student verifier` are still documented but not actually implemented as runnable scripts
  - the current exported student includes `PAD`, while the current `DS-CNN` branch board resolver does not yet register `AddPad()`
- The report also clarifies the recommended role split:
  - keep `river-openwakeword-lab` as the external training lab
  - treat the host teacher as reference/mining infrastructure
  - treat the board-aligned DS-CNN student path as the real deployment direction

## Step 5.4
- On branch `DS-CNN`, replaced the stale architecture-refactor plan in `plan.md` with a branch-specific execution plan that puts full firmware build verification ahead of any further DS-CNN runtime migration.
- Recorded the current decision order in `plan.md`:
  - Phase 0: full build verification
  - Phase 1: review binary size and integration risk
  - Phase 2: only then choose whether to import runtime assets from `/root/river-openwakeword-lab`
- Captured the current runtime constraint directly in the plan:
  - current board KWS contract is `98x40` streaming log-mel
  - `/root/river-openwakeword-lab` remains the preferred external training lab
  - current exported student may require `PAD` support before runtime integration
- Executed the standard full build for the current `DS-CNN` branch and confirmed the build baseline is healthy.
- Verified current top-level image artifacts after the successful build:
  - `build_RTL8730E/km4_boot_all.bin`: `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin`: `3605856`
  - `build_RTL8730E/ota_all.bin`: `3605888`

## Step 5.5
- Completed `Phase 1: Build Result Review` on branch `DS-CNN` after the successful full build verification.
- High-signal review findings:
  - current image sizes are identical to the last verified `xiaozhi` full-build baseline, so there is no new binary-size inflation on this branch
  - `git diff --stat xiaozhi..DS-CNN` shows the branch currently differs only in documentation and planning files, not in runtime source code
  - the app image still exceeds the SDK stock NOR app range and therefore still depends on the project-owned development flash profile
  - current app placement math:
    - start address `0x08040000`
    - end address `0x083B0560`
    - overflow against stock `0x08300000`: `722272` bytes
  - the current KWS runtime op resolver still registers only:
    - `Quantize`
    - `Conv2D`
    - `DepthwiseConv2D`
    - `Mean`
    - `FullyConnected`
    - `Logistic`
- Review conclusion:
  - the current `DS-CNN` branch is still a valid build-stable / flash-stable baseline as long as the project custom profile and `tools/river_flash.py` are used
  - future migration of student assets from `/root/river-openwakeword-lab` must first solve the op-compatibility gate if the imported model requires `PAD`
- Updated `plan.md` accordingly:
  - `Phase 1` marked completed
  - `Phase 2` marked current, with the explicit rule that only minimal runtime integration is allowed from this baseline

## Step 5.6
- Completed `Phase 2: Runtime Integration Decision` on branch `DS-CNN` and chose the lowest-risk landing path:
  - keep the existing baseline embedded KWS model as a selectable fallback
  - import the `/root/river-openwakeword-lab` round6 targeted DS-CNN student as the default experimental model for this branch
  - solve only the minimum runtime compatibility gap needed for that student
- Added KWS model-variant selection to `Kconfig`:
  - `RIVER_KWS_MODEL_VARIANT_BASELINE`
  - `RIVER_KWS_MODEL_VARIANT_ROUND6_TARGETED_EXPERIMENTAL`
- Set `prj.conf` on branch `DS-CNN` to select:
  - `CONFIG_RIVER_KWS_MODEL_VARIANT_ROUND6_TARGETED_EXPERIMENTAL=y`
- Imported the exported round6 targeted student into project-owned generated assets:
  - `components/river_voice/generated/xiaou_student_round6_targeted_int8_model_data.h`
  - `components/river_voice/generated/xiaou_student_round6_targeted_int8_metadata.json`
- Updated `components/river_voice/river_voice_kws.cc` so the board runtime now:
  - conditionally selects baseline vs round6 student at compile time
  - registers `AddPad()` for the experimental student path
  - increases KWS resolver capacity from `6` to `7`
  - logs the active model variant string at runtime
- Full `RTL8730E` build passed with the experimental student selected by default.
- Verified landing results:
  - the final firmware now contains the string `round6_targeted_experimental`
  - image sizes became:
    - `km4_boot_all.bin`: `51872`
    - `km0_km4_ca32_app.bin`: `3573088`
    - `ota_all.bin`: `3573120`
  - compared with the previous baseline build, the app image shrank by `32768` bytes because the imported experimental student is smaller than the old embedded model
  - despite the size reduction, the app image still exceeds the SDK stock NOR app range, so this branch still depends on the project-owned flash profile and `tools/river_flash.py`
- Explicit decision retained in docs:
  - this is an engineering smoke / board-validation landing, not a deploy-ready model-quality decision

## Step 5.7
- Reorganized repository-level Markdown entrypoints to reduce root-directory clutter on branch `DS-CNN`.
- Created `doc/` as the new home for summary, report, design, migration-review, and historical note files.
- Moved the following classes of root docs into `doc/`:
  - migration assessments
  - architecture / refactor proposals
  - frontend / KWS / AEC / VAD chain notes
  - historical milestone and pitfall summaries
  - project status snapshots
- Kept ongoing root entry files in place:
  - `README.md`
  - `plan.md`
  - `build.md`
  - `AGENTS.md`
  - `TIPS.md`
  - `REVIEW.md`
- Added `doc/README.md` as the index for the relocated report/archive documents.
- Refreshed ongoing root docs to match the current `DS-CNN` branch state:
  - `README.md` now reflects the current branch, DS-CNN runtime objective, validated image sizes, flash constraints, and the new `doc/` layout
  - `plan.md` now points to the migration reports under `doc/`
  - `build.md` now explicitly documents that the current app image still requires the project-owned flash profile even when using the official GUI tool
- Refreshed `doc/PROJECT_STATUS_ZH.md` so it no longer describes the old `xiaozhi` branch as the current baseline and instead captures the present `DS-CNN` branch status.
- Chose not to delete any relocated summary Markdown in this step because each moved file still has traceability or comparison value; only location and indexing were cleaned up.

## Step 5.8
- Tightened the `DS-CNN` branch around the intended runtime business flow:
  - local wake word
  - XiaoZhi realtime session after wake
  - VAD-assisted audio bridge
  - no local online text/TTS debug injection compiled by default
- Added two compile-time gates in `Kconfig`:
  - `RIVER_CLOUD_TEXT_DEBUG_EN`
  - `RIVER_INTERACTION_DIAG_EN`
- Set both gates to `n` in `prj.conf` so the default `DS-CNN` branch firmware no longer carries:
  - direct `river echo` / `river tts` cloud text/TTS debug injection
  - local `river interaction ...` text router / tts test / deferred-tts worker
- Switched `components/river_core/CMakeLists.txt` to compile either:
  - `river_interaction_diag.c` when enabled
  - `river_interaction_diag_stub.c` when disabled
- Added `components/river_core/river_interaction_diag_stub.c` so the runtime interface remains stable while the heavy local interaction debug implementation is excluded from the image.
- Updated `components/river_diag/river_diag_cmd.c` so debug-only monitor commands are compile-gated:
  - `river echo`
  - `river tts`
  - `river interaction ...`
- Kept board-useful commands intact:
  - `river status`
  - `river xiaozhi ...`
  - `river playback ...`
  - `river audio ...`
  - `river device ...`
- Updated `components/river_cloud/river_online_control.c` so:
  - device control remains available for XiaoZhi MCP tool execution
  - `river_online_control_echo()` becomes a stub when cloud text debug is disabled
  - status logs show whether text debug was compiled or stubbed
- Verified the full `RTL8730E` build after the gating changes.
- Measured size impact:
  - `build_RTL8730E/km0_km4_ca32_app.bin` shrank from `3573088` to `3564896`
  - `build_RTL8730E/ota_all.bin` shrank from `3573120` to `3564928`
  - app delta for this step: `-8192` bytes
- Measured representative object-level impact:
  - `river_interaction_diag.o` was about `80K`; replaced by `river_interaction_diag_stub.o` about `7.8K`
  - `river_diag_cmd.o` dropped from about `35K` to about `29K`
- Updated `README.md` and `plan.md` so the current repo entrypoints now reflect:
  - the wake -> XiaoZhi realtime target flow
  - the VAD-assisted runtime expectation
  - the new debug-gating state and current image sizes

## Step 5.9
- Checked whether the repository still contains a backup file named `plan.md.bk` before deciding whether to merge or delete it.
- Verified that:
  - `/root/ameba-river/plan.md.bk` does not exist
  - a full repository search found no `plan.md.bk` and no relevant `*.bk` backup artifact
  - `git log --all --name-only -- plan.md.bk` returned no history, so this backup file is not part of the current tracked repository history
- Conclusion:
  - there is no in-repo `plan.md.bk` left to merge or delete
  - the current authoritative execution plan remains `plan.md`
  - the latest `plan.md` already subsumes the useful planning state:
    - DS-CNN experimental runtime landing
    - wake -> XiaoZhi realtime target flow
    - VAD-assisted audio bridge
    - compile-time gating for online text/TTS debug injection

## Step 5.10
- Isolated the board crash investigation away from the experimental DS-CNN student model by switching the default KWS embedded variant back to the repository baseline model in `prj.conf`.
- Kept the product path unchanged for this step:
  - local KWS still enabled
  - VAD probe still active
  - wake -> XiaoZhi realtime path still intact
  - only the embedded wake-word model variant changed
- Verified the generated build configs now select:
  - `CONFIG_RIVER_KWS_MODEL_VARIANT_BASELINE=y`
  - `# CONFIG_RIVER_KWS_MODEL_VARIANT_ROUND6_TARGETED_EXPERIMENTAL is not set`
- Verified the built CA32 image now embeds `baseline_embedded` instead of `round6_targeted_experimental`.
- Built `RTL8730E` successfully after the switch.
- Captured updated image sizes after the isolation change:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3597664`
  - `build_RTL8730E/ota_all.bin` = `3597696`
- Compared with the prior experimental-model build:
  - app image increased by `32768` bytes
  - this size increase is expected for this isolation step and is smaller risk than continuing to ship the model variant currently implicated by the crash trace
- Current conclusion:
  - the next board flash is now a high-signal A/B check
  - if the crash disappears with `baseline_embedded`, the experimental round6 model/runtime combination is the primary suspect
  - if the crash persists, investigation should continue inside the general KWS runtime path rather than in XiaoZhi session logic

## Step 5.11
- Reduced the XiaoZhi downlink playback buffer sizing again to lower CA32 heap pressure when TTS playback starts.
- Updated [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h):
  - `RIVER_CLOUD_XIAOZHI_PLAYBACK_BUFFER_FRAMES: 6 -> 3`
  - `RIVER_CLOUD_XIAOZHI_PLAYBACK_BUFFER_FRAMES_FALLBACK: 4 -> 2`
- Left the rest of the XiaoZhi realtime path unchanged:
  - wakeword admission
  - websocket/session setup
  - uplink audio framing
  - downlink ring depth and playback start threshold
- Rationale:
  - the latest successful wake -> XiaoZhi session log showed the new blocker is not recognition or websocket setup
  - the failure moved to CA32 playback start, with `river_xz_down` hitting `Malloc failed ... xWantedSize:46144`
  - the playback service multiplies the track minimum buffer by `buffer_frame_count`, so shrinking these constants is the direct low-risk lever for this allocation
- Verified a full `RTL8730E` build after the buffer reduction.
- Current packaged image sizes remained:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3597664`
  - `build_RTL8730E/ota_all.bin` = `3597696`
- Expected runtime effect:
  - first playback attempt now uses about half the prior frame multiplier
  - compact fallback is now tighter again if the first playback open still cannot fit

## Step 5.12
- Added a new execution-oriented refactor document at `doc/PROJECT_REFACTOR_EXECUTION_PLAN_ZH.md`.
- This document is intentionally different from the existing architecture blueprints:
  - it is based on the current `DS-CNN` codebase and recent board/runtime logs
  - it prioritizes work by `correctness -> memory/hot path -> boundaries -> size/clean code`
  - it turns the current review findings into phased, stepwise work that can be committed and verified incrementally
- Captured the current top-priority refactor targets:
  - wakeword deferred admission should become retryable instead of lossy
  - time-ready semantics should be unified
  - XiaoZhi uplink should stop allocating per packet on the hot path
  - playback and pre-roll memory budgeting should become explicit
  - `river_voice -> river_cloud` direct dependencies should be removed in later phases
  - the current large files should be split by façade / policy / runtime / diagnostics responsibility
- Updated `doc/README.md` so the new refactor execution plan is discoverable from:
  - `当前优先阅读`
  - `架构与实现`
- Kept this step documentation-only on purpose:
  - no source code behavior changed
  - no firmware image content changed
  - this step exists to establish the refactor baseline before rewriting `plan.md`

## Step 5.13
- Rewrote the root `plan.md` so it now reflects the actual branch objective: staged refactoring of the current runtime rather than the older migration/smoke narrative.
- The new `plan.md` now aligns with `doc/PROJECT_REFACTOR_EXECUTION_PLAN_ZH.md` and captures:
  - the current baseline chain
  - current runtime/architecture risks
  - explicit guardrails for refactor steps
  - a phased execution order from correctness to memory to boundaries to size cleanup
  - the immediate next code step: wake admission retry plus time-ready semantics cleanup
- Removed outdated guidance from the old root plan that no longer matched the current branch state:
  - experimental model migration framing
  - old smoke-first execution path
  - stale phase history that had already served as historical record in `.codex`
- Kept this step documentation-only:
  - no runtime source changed
  - no binary content changed
  - this step exists to make the root project entrypoint consistent before starting code refactors

## Step 5.14
- Completed the first code refactor step under `Phase 1`, focused on wake admission correctness rather than structural file splitting.
- Updated `components/river_core/river_session_coordinator.c` so wakeword admission is no longer lossy when the cloud side is temporarily unavailable:
  - a pending wakeword is no longer cleared before admission succeeds
  - `RIVER_ERR_BUSY` now keeps the wake event pending and retries from the wake worker every `250 ms`
  - non-retryable admission failures still clear the pending wake and log an error
  - deferred admission logging is rate-limited so retries do not spam the log on every poll
- Updated `components/river_cloud/river_cloud_adapter.c` and `components/river_cloud/river_cloud_internal.h` to make the cloud-side time semantics explicit for this path:
  - `river_cloud_time_ready()` remains the strict system-time / SNTP-ready gate
  - new internal `river_cloud_wake_admission_time_ready()` uses the best available wake-admission time source, including build-seeded UTC estimate
  - added a deduplicated `wake admission deferred` log path that reports:
    - `wifi`
    - `admission_time_ready`
    - `system_time_ready`
- Updated `components/river_cloud/river_cloud_xiaozhi_session.c` so XiaoZhi wake admission now:
  - starts SNTP and seeds build time as before
  - allows wake admission to proceed when build-seeded UTC estimate is available even if system time is not yet SNTP-ready
  - logs once when wake admission proceeds using the build-seeded UTC estimate
  - resets the deferred-state tracker after successful admission or network lifecycle changes
- Practical effect of this step:
  - a wakeword hit that happens before SNTP convergence should no longer be dropped permanently
  - the board can now hold the wake event and enter XiaoZhi once the transient busy condition clears
  - logs now make it explicit whether the blocker was Wi-Fi, strict system time, or whether wake admission proceeded on the build-seeded estimate
- Verified a full local `RTL8730E` build after the change.
- Image sizes after this step remained:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3597664`
  - `build_RTL8730E/ota_all.bin` = `3597696`
- Image size remained unchanged because this step only reworked existing control flow and logging; it did not add new large runtime assets.

## Step 5.15
- Completed the next `Phase 1` hot-path cleanup in the XiaoZhi transport layer, scoped narrowly to per-packet heap churn on uplink websocket binary framing.
- Updated [components/river_cloud/river_xiaozhi_ws.c](/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c) so `river_xiaozhi_send_binary_frame()` no longer calls `rtos_mem_malloc/free` for protocol `v2/v3` audio frames:
  - added a long-lived transport scratch buffer in `g_river_xiaozhi`
  - reused that buffer for websocket binary frame header + payload assembly
  - kept raw-payload passthrough behavior for non-`v2/v3` protocol handling
- Added an explicit framed-payload size guard tied to the current project contract:
  - max framed XiaoZhi uplink payload is now enforced as `512B`
  - oversize payloads fail fast with `binary_payload_too_large` instead of falling into hidden heap growth or fragmentation
- Why this step matters:
  - XiaoZhi uplink audio runs on a steady hot path
  - per-frame heap allocation here adds avoidable fragmentation and latency risk on embedded targets
  - this keeps the optimization local to the websocket framing layer before later memory-budget work on playback/downlink
- Verified a full local `RTL8730E` build after the change.
- Image sizes after this step remained:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3597664`
  - `build_RTL8730E/ota_all.bin` = `3597696`
- Image size remained unchanged because the step replaces transient heap usage with a small fixed in-context scratch buffer inside already-allocated runtime state.

## Step 5.16
- Completed the next `Phase 1` memory-budget cleanup on the playback path, focused on the XiaoZhi downlink/TTS startup failure that previously attempted a `~46KB` AudioTrack allocation in `river_xz_down`.
- Updated [components/river_voice/river_playback_service.c](/root/ameba-river/components/river_voice/river_playback_service.c) so playback buffer sizing now matches the service API semantics:
  - `buffer_frame_count` is treated as the target number of application playback frames
  - `AudioTrack_GetMinBufferBytes()` is treated as the SDK minimum whole-track buffer budget
  - final track buffer bytes now use:
    - `max(min_buffer_bytes, playback_frame_bytes * buffer_frame_count)`
  - the old behavior incorrectly multiplied the SDK minimum buffer by `buffer_frame_count`, which over-allocated playback memory when `min_buffer_bytes` was already larger than a single app frame
- Added a startup log for playback streams so the actual runtime budget is now visible on board:
  - stream name
  - sample rate / frame duration
  - per-frame bytes
  - SDK `min` bytes
  - application `target` bytes
  - final `track` bytes
  - whether playback reference export is enabled
- Updated [include/river/river_playback_service.h](/root/ameba-river/include/river/river_playback_service.h) to document the intended meaning of `buffer_frame_count`, so the service contract is explicit in code rather than hidden in implementation.
- Why this step matters:
  - the previous XiaoZhi downlink failure aligned with `15360B * 3 = 46080B`, meaning the service was tripling the SDK minimum whole-track buffer
  - this change preserves the SDK minimum while removing the extra amplification
  - the fix is generic for all playback-service users, but it directly targets the observed XiaoZhi downlink memory failure
- Verified a full local `RTL8730E` build after the change.
- Image sizes after this step remained:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3597664`
  - `build_RTL8730E/ota_all.bin` = `3597696`
- Image size remained unchanged because this step only corrected runtime buffer sizing math and logging; it did not add new assets or large static buffers.
