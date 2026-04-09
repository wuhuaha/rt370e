# Change Log

## Step 5.130
- Validated the latest-SDK `student_dscnn_tiny_v2_int8_debug` alignment cleanup change on real hardware after flashing the rebuilt image from `/root/ameba-rtos`.
- In [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - alignment cleanup now skips the final `river_voice_kws_disarm(context, true)` only when the replay already succeeded, the KWS worker is idle, and the gate is fully cleared
  - the normal runtime KWS path is unchanged; this only affects the debug-only `river kws align run` success cleanup path
- Board validation on `2026-04-09` proved the previous hang point moved:
  - `river kws debug local on` at boot successfully blocked cloud handoff during false wakes
  - `river audio probe stop` prevented live probe traffic from racing the compiled-sample replay
  - `river kws align run` now reaches all of these later markers on board:
    - `kws align replay captured: seq=1 infer=2 score=0.296875 q15=9728`
    - `kws align cleanup: status=0 emit_dump=yes local_only_restore=yes`
    - `kws align cleanup: disarm skipped worker already idle snapshot=ready`
    - `kws align cleanup: worker idle wait status=0`
    - `kws align cleanup: disarm tensor dump begin`
    - `kws align cleanup: disarm tensor dump done`
    - `kws align cleanup: local debug restored=yes`
- Newly confirmed remaining blockers:
  - the final `kws align replay done: ...` line is still missing
  - a manual follow-up `river kws dump meta` still does not execute after that run
  - the board then falls into repeated `[INIC-A] Dev api ipc timeout: cur id 0x1 ...` logs, so the shell is still not fully returning
- Additional board finding from the same run:
  - on a clean boot without early `local debug`, this INT8 model false-triggers very early on live audio:
    - `infer=1 score_pm=304`
    - `wakeword hit: ... score_pm=304 q15=9984`
  - with the configured threshold at `278`, the current debug image is not stable enough to leave live probe traffic running during alignment work

## Step 5.129
- Trimmed the `river kws align run` UART output one step further for slow/fragile INT8 bring-up while preserving the full manual parity path.
- In [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - removed the automatic `begin/meta/snapshot` summary emission from `river kws align run`
  - kept the captured snapshot itself intact, so the existing manual commands remain the parity path:
    - `river kws dump meta`
    - `river kws dump chunk feat_f32 ...`
    - `river kws dump chunk input_raw ...`
    - `river kws dump chunk output_raw ...`
- Rationale:
  - board validation in Step `5.128` proved the command now reaches `kws align replay captured: ...`
  - but the shell still blocked immediately after the auto-emitted snapshot summary
  - reducing `align run` to preserve snapshot without auto-printing summary is the smallest behavior change that keeps the existing board/local parity mechanism intact
- Removed the now-unused helper `river_voice_kws_log_tensor_dump_snapshot_summary(...)`.
- Verified this step with a full latest-SDK rebuild against `/root/ameba-rtos`; the build completed with `Build done`.

## Step 5.128
- Re-flashed the `ace9300` latest-SDK image after the user power-cycled the board and successfully restored the PL2303 UART node inside WSL.
- A host-side serial-node issue had to be corrected first:
  - because `/dev/ttyUSB0` was missing, earlier redirections had created a regular file at that path
  - reattached USB device `4-4` with `usbipd.exe attach --wsl --busid 4-4`
  - confirmed the kernel still exposed `ttyUSB0` in `/sys/class/tty/ttyUSB0`
  - removed the bogus `/dev/ttyUSB0` file and recreated the proper character device node from kernel major/minor `188:0`
- Flash then succeeded normally with `Finished PASS`.
- Re-ran the latest-SDK DS-CNN tiny INT8 parity command path:
  - `river kws debug local on`
  - `river audio probe stop`
  - `river kws align run`
- New observed narrowing on `2026-04-09`:
  - trigger-side path fully returns now:
    - `kws trigger dispatch done: ...`
    - `kws trigger post-disarm: gate=latched ...`
  - replay loop also completes its feed and tail phases:
    - `kws align replay feed done: trigger=no infer=2 snapshot=ready`
    - `kws align replay tail done: gate=cleared infer=2 snapshot=ready`
    - `kws align replay wait idle status=0`
  - snapshot summary now fully prints:
    - `kws align replay captured: seq=1 infer=2 score=0.296875 q15=9728`
    - `kws tensor dump begin: ...`
    - `kws tensor dump meta: ...`
    - `kws tensor dump snapshot: seq=1 infer=2 chunks=[feat:253 input:64 output:1]`
  - immediately after that, runtime prints:
    - `[INIC-E] WIFI TRX IPC 4 timeout`
- The command still never reaches:
  - `kws align cleanup: ...`
  - `kws align replay done: ...`
- A follow-up `river kws dump meta` still only echoes at UART and does not execute, confirming the shell remains blocked inside the original `river kws align run`.
- Current narrowed conclusion:
  - the remaining hang is now specifically between the last line of `river_voice_kws_log_tensor_dump_snapshot_summary(...)` and the first outer cleanup log
  - the next diagnostic step should instrument the end of snapshot-summary emission and the first statement after it

## Step 5.127
- Added a second-stage INT8 alignment diagnostic slice to narrow the remaining hang that still occurs after trigger-side `disarm`.
- In [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - added `kws trigger dispatch done: ...` immediately after wakeword event dispatch returns
  - added `kws trigger post-disarm: ...` immediately after `river_voice_kws_disarm_after_trigger(...)` returns
  - added alignment replay markers for the first post-trigger phase:
    - `kws align replay trigger observed: frame=... infer=... snapshot=...`
    - `kws align replay first post-trigger queue wait begin: ...`
    - `kws align replay first post-trigger queue wait done: ...`
  - added end-of-replay phase markers:
    - `kws align replay feed done: ...`
    - `kws align replay tail done: ...`
    - `kws align replay wait idle begin/status=...`
- Verified this step with a full latest-SDK rebuild against `/root/ameba-rtos`; the build completed with `Build done`.

## Step 5.126
- Re-flashed the latest-SDK `student_dscnn_tiny_v2_int8_debug` image that includes the cleanup-stage diagnostics and re-ran the preserved board/local parity command path on `2026-04-09`.
- Flashing required the known serial recovery sequence before download mode:
  - send `ESC+CRLF`
  - send `reboot uartburn`
  - flash with `tools/river_flash.py`
  - result: `Finished PASS`
- After flashing, the board did not immediately resume normal runtime logs on its own; sending a plain `reboot` restored the application runtime, after which UART logs resumed normally.
- Reproduced the alignment path with:
  - `river kws debug local on`
  - `river audio probe stop`
  - `river kws align run`
- New observed behavior:
  - the initial alignment disarm path completed fully before replay start
  - replay progressed into real INT8 inference:
    - `infer=1 score=0.242188 q15=7936`
    - `infer=2 score=0.296875 q15=9728`
  - tensor snapshot capture is working during replay:
    - `kws tensor dump captured: seq=1 infer=2 ...`
  - local-debug wake suppression is working:
    - `wakeword handoff held: reason=local_debug ...`
  - after the trigger, a second `kws disarm` also completed all currently instrumented substeps:
    - `frontend reset done`
    - `input ring reset done`
    - `pre-roll ring reset done`
    - `input signal drained`
- But the command still never reached the later alignment-stage markers:
  - no `kws align replay captured: ...`
  - no `kws align cleanup: ...`
  - no `kws align replay done: ...`
- A follow-up `river kws dump meta` was only echoed by UART and produced no command execution output, which shows the CLI remained blocked inside the original `river kws align run`.
- Current narrowed conclusion:
  - the remaining hang is no longer in the outer cleanup block entry
  - it happens earlier, after trigger-side `river_voice_kws_disarm_after_trigger()` finishes, but before `river_voice_kws_run_alignment_sample()` reaches the post-feed snapshot/cleanup section
  - likely next suspects are the replay loop's post-trigger frame submission / queue-room checks or another no-log ring/mutex call immediately after the second disarm

## Step 5.125
- Added alignment-cleanup diagnostics for the latest-SDK `student_dscnn_tiny_v2_int8_debug` replay path so the remaining post-snapshot stall can be localized without changing the normal wakeword runtime flow.
- In [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - added timeout diagnostics in `river_voice_kws_wait_for_worker_idle(...)` to print:
    - `reset_pending`
    - `worker_processing`
    - pending input-ring frame count
    - timeout budget
  - added step-by-step logs inside `river_voice_kws_disarm(...)` to show whether cleanup reaches:
    - frontend reset
    - input-ring reset
    - pre-roll-ring reset
    - input-signal drain
  - added cleanup-stage logs in `river_voice_kws_run_alignment_sample(...)` to show whether alignment teardown reaches:
    - `disarm done`
    - `worker idle wait status=...`
    - tensor-dump disarm begin/done
    - local-debug restore
- Verified this step with a full latest-SDK rebuild against `/root/ameba-rtos`; the build completed with `Build done`.

## Step 5.124
- Re-tried the latest-SDK `student_dscnn_tiny_v2_int8_debug` board validation after the user power-cycled the board and reattached the PL2303 USB serial device.
- New observed state on `2026-04-09`:
  - the board no longer stayed in the earlier pure-`0x00` boot-failure state
  - passive UART capture immediately showed normal runtime KWS/VAD logs again
  - the active image characteristics still matched the DS-CNN tiny INT8 debug variant:
    - `out_type=int8`
    - `threshold_pm=278`
    - `arena=295764/2048KB`
    - `infer_us` around `491-493 ms`
- Ran the preserved-snapshot alignment workflow again:
  - `river kws debug local on`
  - `river audio probe stop`
  - `river kws align status`
  - `river kws align run`
- The board now progressed through the intended summary-based parity path:
  - `kws align replay captured: seq=1 infer=12 score=0.296875 q15=9728`
  - `kws tensor dump begin: ... in_type=int8 out_type=int8 ...`
  - `kws tensor dump meta: ... in_scale=0.029209241 in_zp=-9 out_scale=0.003906250 out_zp=-128`
  - `kws tensor dump snapshot: seq=1 infer=12 chunks=[feat:253 input:64 output:1]`
- But the run still did not fully return to an interactive shell:
  - `kws align replay done: dump=preserved ...` did not appear
  - after the snapshot line, the board started repeating `IPC Get Semaphore Timeout`
  - a follow-up `river kws dump meta` input was echoed by UART but produced no executed dump response
- Current conclusion:
  - the preserved-snapshot summary change is effective on board
  - the previous UART-flood failure is no longer the first blocker after this power cycle
  - another stall remains after snapshot capture, so board/local parity still cannot be completed for this INT8 image yet

## Step 5.123
- Revalidated the latest-SDK `student_dscnn_tiny_v2_int8_debug` board bring-up on `2026-04-09` after the preserved-snapshot diagnostic change.
- Flash path remained healthy:
  - sent `reboot uartburn` over `/dev/ttyUSB0`
  - flashed with `tools/river_flash.py` against `AMEBA_SDK_ROOT=/root/ameba-rtos`
  - AmebaFlash finished with `Finished PASS`
- Board runtime still failed before any KWS parity work could begin:
  - passive UART capture on `/dev/ttyUSB0` produced continuous `0x00`
  - official `monitor.py --debug` connected successfully, sent `AT+LIST`, and still received only repeated `0x00`
  - no normal boot markers appeared, including no `File System Init Success`, no `ameba-river boot`, and no KWS init logs
- This confirms the current blocker is still the latest-SDK INT8 image failing to enter a readable runtime / monitor state on board, not the new manual-chunk parity workflow.

## Step 5.122
- Refined the preserved `river kws align run` diagnostic path for the latest-SDK `student_dscnn_tiny_v2_int8_debug` bring-up.
- In [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - changed `river kws align run` to emit only tensor-dump `begin/meta/summary` after a successful replay capture
  - kept the captured snapshot alive after `align run` success so the existing `river kws dump meta` and `river kws dump chunk ...` commands can fetch data incrementally
  - only clear the preserved snapshot on alignment failure
- This keeps the board/local parity mechanism unchanged while avoiding the UART flood caused by auto-printing hundreds of dump chunks in one burst.
- Verified this step with a full latest-SDK rebuild after the change.

## Step 5.121
- Hardened the preserved `river kws align run` diagnostic path for slow KWS variants without changing the normal wakeword runtime path.
- In `components/river_voice/river_voice_kws.cc`:
  - added alignment-only queue throttling so the compiled-sample replay no longer keeps feeding frames unchecked while a slow model is still draining the worker queue
  - widened the alignment idle/snapshot wait windows for latest-SDK slow-model bring-up
- Kept the existing board/host parity mechanism intact:
  - `river kws align run`
  - `river kws dump meta`
  - `river kws dump chunk ...`
  - `tools/kws/replay_board_tensor_dump.py`
- Verified this step with a full latest-SDK rebuild after the change.

## Step 5.120
- Added a parallel `student_dscnn_tiny_v2_int8_debug` wakeword variant without removing any of the preserved board/host parity infrastructure.
- Generated and embedded the algorithm bundle's `student_dscnn_tiny_v2` INT8 TFLite model as:
  - `components/river_voice/generated/student_dscnn_tiny_v2_int8_model_data.h`
- Extended KWS model selection so the new DS-CNN tiny INT8 bundle can reuse the same `40x101`, centered log-mel, tensor-dump, align-replay, and host-replay workflow already used by prior debug variants.
- Switched the active project configuration to:
  - `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_DSCNN_TINY_V2_INT8_DEBUG=y`
  - `CONFIG_RIVER_KWS_TENSOR_ARENA_KB=2048`
  - `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=9125`
- Updated `env.sh` so the repository helper now defaults to the latest SDK checkout at `/root/ameba-rtos`.
- Verified a full `RTL8730E` build against `/root/ameba-rtos` completed successfully and produced:
  - `build_RTL8730E/km0_km4_ca32_app.bin`
  - `build_RTL8730E/build/project_ap/image/ap_image_all.bin`

## Step 5.119
- Persisted the SDK baseline rule in `AGENTS.md`.
- Future work should default to the latest SDK checkout at `/root/ameba-rtos` for build, flash, validation, and wakeword-model bring-up unless the user explicitly requests another SDK tree.

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

## Step 5.17
- Added a temporary board-bring-up tuning step to make wake-word validation much easier while the current embedded KWS model is still weak.
- Updated [prj.conf](/root/ameba-river/prj.conf) only:
  - lowered `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15` from `21299` to `8192`
  - added an inline comment marking this as a temporary permissive threshold for board-side wake-path validation
- Kept scope intentionally narrow:
  - no KWS runtime code changed
  - no model asset changed
  - no hold/cooldown/stride/gate queue parameter changed
- Why this step matters:
  - current field logs show wake hits are possible but the model is not robust enough for comfortable board-side iteration
  - lowering the primary score threshold also lowers the fallback gate threshold automatically through the existing runtime derivation in `river_voice_kws.cc`
  - this gives a fast bring-up path for validating wake -> XiaoZhi connect -> ASR/TTS session flow before spending more time on model quality
- Verified a full local `RTL8730E` build after the config change.
- Image sizes after this step remained:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3597664`
  - `build_RTL8730E/ota_all.bin` = `3597696`
- Image size remained unchanged because this step only adjusts a config threshold and does not alter runtime assets or large static allocations.

## Step 5.18
- Added concise Chinese comments across the project-owned source tree to improve code navigation without turning the code into comment noise.
- Scope of this step:
  - covered all manually maintained `*.c` / `*.cc` / `*.h` files under:
    - `app/`
    - `components/river_common`
    - `components/river_core`
    - `components/river_diag`
    - `components/river_cloud`
    - `components/river_voice`
    - `include/river`
  - excluded:
    - `components/river_voice/generated/*`
    - `CMakeLists.txt`
    - SDK code under `/root/ameba-rtos-1.2`
- Comment style used in this step:
  - file-level Chinese summaries for each project-owned source/header file
  - a small number of focused inline comments on non-obvious orchestration paths
  - no line-by-line explanatory noise on self-explanatory code
- Practical effect:
  - each module now states its responsibility at file entry
  - internal/private headers and public headers are easier to scan when tracing responsibilities across `core`, `cloud`, and `voice`
  - complex boot/log/diag dispatch paths now have a few explicit intent comments where they help most
- Verified a full local `RTL8730E` build after the comment-only change.
- Image sizes after this step remained:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3597664`
  - `build_RTL8730E/ota_all.bin` = `3597696`
- Image size remained unchanged because this step only adds source comments and does not alter compiled behavior or runtime assets.

## Step 5.19
- Fixed the next XiaoZhi runtime memory failure on the uplink path after wake/session open had already succeeded.
- Observed board failure before this step:
  - websocket connect succeeded
  - `server hello` arrived
  - ASR streaming entered
  - then `river_xz_up` failed with:
    - `Malloc failed ... [xWantedSize:8320]`
    - followed by `INIC-E WIFI TRX IPC 4 timeout`
- Root cause:
  - XiaoZhi websocket transport was created with `RIVER_XIAOZHI_WS_TX_MAX=8192`
  - Ameba SDK `wsclient` dynamically allocates `tx_buf_len + 16` per queued send buffer item
  - under low free heap, the next uplink queue expansion in `river_xz_up` tried to allocate another `~8 KB` send buffer and failed
- Updated [include/river/river_xiaozhi_credentials.h](/root/ameba-river/include/river/river_xiaozhi_credentials.h) only:
  - lowered `RIVER_XIAOZHI_WS_TX_MAX` from `8192` to `1024`
  - lowered `RIVER_XIAOZHI_WS_QUEUE_MAX` from `16` to `4`
  - documented why the reduced values are still sufficient for the current project contract:
    - uplink Opus/binary frames are already bounded well below `512 B` payload plus framing
    - current hello/listen/abort JSON and MCP envelopes remain within the reduced headroom
- Why this step matters:
  - this directly removes the exact `~8 KB` dynamic allocation class seen in the crash log
  - it also caps worst-case websocket send-queue growth so heap pressure fails earlier and smaller instead of fragmenting late
  - scope stays narrow because no cloud/session logic or protocol payload shape was changed
- Verified a full local `RTL8730E` build after the config change.
- Image sizes after this step remained:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3597664`
  - `build_RTL8730E/ota_all.bin` = `3597696`
- Image size remained unchanged because this step only tightens runtime websocket buffer limits and does not add code or assets.

## Step 5.20
- Fixed the next XiaoZhi follow-up failure mode after the earlier uplink heap fix.
- Observed board failure before this step:
  - wake and websocket connect succeeded
  - ASR stream entered
  - later logs showed capture-side backlog and transport send pressure:
    - `capture frame ring overflow: dropped=...`
    - `ws_sendData: ERROR: Not get usable buffer, Please enlarge max_queue_size!`
    - `xiaozhi uplink send failed: status=-6`
- Root cause addressed in this step:
  - the XiaoZhi cloud path still allowed follow-up reopening logic to run from the VAD/audio processing thread
  - if the websocket transport had already been closed or become unavailable, that thread could fall into a synchronous reopen path at the wrong layer
  - this is exactly the kind of stall that starves capture consumption and turns into `capture frame ring overflow`
  - keeping the conversation window open after transport loss also allowed stale follow-up state to keep pushing toward the websocket queue instead of failing closed
- Updated project code in:
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- What changed:
  - added a local-only `xiaozhi` conversation-window abort helper that resets window/follow-up state without re-entering websocket close logic
  - this keeps the websocket close callback path deadlock-safe while still letting the cloud layer fail closed immediately on transport loss
  - on `RIVER_XIAOZHI_EVENT_SESSION_CLOSED`, the cloud adapter now:
    - logs the transport-close context
    - aborts the local conversation window first
    - emits the ASR `session_closed` signal with the corrected interaction-state ordering
    - clears cached uplink/downlink/session state so stale follow-up traffic does not linger
  - during follow-up auto-open, if the transport is already unavailable, the VAD path now aborts the window and returns `RIVER_ERR_BUSY` instead of trying to synchronously reconnect from the audio thread
- Why this step matters:
  - it prevents transport recovery from being driven by the real-time audio/VAD path
  - it removes the state leak where a dead XiaoZhi websocket could leave follow-up logic active long enough to back up capture and websocket send queues
  - it preserves the intended explicit recovery path:
    - first wakeword opens a session from the wake worker
    - stale follow-up does not try to do network recovery from the VAD worker
- Verified a full local `RTL8730E` build after the fix.
- Image sizes after this step remained:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3597664`
  - `build_RTL8730E/ota_all.bin` = `3597696`
- Image size remained unchanged because this step only tightens state/flow control and does not add assets or enlarge static buffers.

## Step 5.21
- Landed the dedicated BC-ResNet wake-word replacement plan before touching runtime code.
- Added [doc/KWS_BC_RESNET_REPLACEMENT_PLAN_ZH.md](/root/ameba-river/doc/KWS_BC_RESNET_REPLACEMENT_PLAN_ZH.md) to record:
  - why `bc_resnet_best.tflite` cannot be dropped in directly
  - the exact two runtime blockers:
    - missing `Add` op registration
    - input layout mismatch between current `98x40x1` write order and BC-ResNet `40x98x1`
  - the chosen minimal-change replacement strategy
  - validation focus and remaining risks
- Updated both plan trackers:
  - [plan.md](/root/ameba-river/plan.md)
  - [.codex/plan.md](/root/ameba-river/.codex/plan.md)
- Why this step matters:
  - it freezes the replacement contract before code churn starts
  - it prevents the runtime implementation from drifting into ad hoc model-specific fixes
- it makes the next implementation commit auditable against a concrete plan
- No product code or embedded assets changed in this step.

## Step 5.22
- Replaced the current baseline embedded wake-word asset with `bc_resnet_best.tflite` from `kws-training-pro`.
- Updated [components/river_voice/generated/river_wake_word_model_data.h](/root/ameba-river/components/river_voice/generated/river_wake_word_model_data.h):
  - regenerated the baseline-slot model data from `models/bc_resnet_ultra/bc_resnet_best.tflite`
  - kept the existing symbol names `kws_model` and `kws_model_len`
  - new embedded model size is `56024B`
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - increased `RIVER_KWS_OP_COUNT` from `7` to `8`
  - registered `Add` in the TFLM resolver so BC-ResNet residual paths are supported
  - added model-driven input-shape parsing with schema-first, runtime-fallback behavior
  - accepted both board-supported layouts:
    - `[1, 98, 40, 1]`
    - `[1, 40, 98, 1]`
  - added direct `mels_frames` write order support so BC-ResNet does not need an extra transpose buffer
  - updated profile logs to print the real embedded input shape and active variant name
- Updated [components/river_voice/river_voice_frontend.c](/root/ameba-river/components/river_voice/river_voice_frontend.c):
  - changed the wake-stage validation log from model-specific `dscnn_kws` wording to neutral `local_kws`
- Why this step matters:
  - the runtime is no longer hard-wired to a single `98x40x1` model layout
  - BC-ResNet can now be loaded without resolver failure on missing `Add`
  - the model replacement stayed confined to the existing baseline slot without expanding Kconfig complexity
- Verified a full local `RTL8730E` build after the replacement.
- New image sizes after this step:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3593568`
  - `build_RTL8730E/ota_all.bin` = `3593600`
- Image size decreased relative to the previous baseline because the BC-ResNet asset is smaller than the previous embedded wake-word model.

## Step 5.23
- Added a project knowledge note for the resolved WSL2 flashing issue:
  - [knowledge/WSL2_FLASH_TROUBLESHOOTING_ZH.md](/root/ameba-river/knowledge/WSL2_FLASH_TROUBLESHOOTING_ZH.md)
- Captured the concrete conclusion from this incident:
  - `0xE8 Address error` was not caused by the current firmware size exceeding the project flash profile
  - the real risk area is WSL2 serial-device visibility and whether the project-owned flash profile is actually being used
- Documented the verified size facts used during diagnosis:
  - current `km0_km4_ca32_app.bin` is `3593568B`
  - current `ota_all.bin` is `3593600B`
  - both still fit inside the project app range `0x08040000-0x08600000`
- Recorded the WSL2-specific operational guidance:
  - do not assume `COM3 -> /dev/ttyS2` is usable just because the numbering matches
  - prefer `usbipd-win` plus `/dev/ttyUSB0` or `/dev/ttyACM0`
  - always flash through `python3 tools/river_flash.py ...`
- This step is documentation-only and does not change firmware code or binary assets.

## Step 5.24
- Tightened XiaoZhi idle-admission policy so an enabled KWS build remains wakeword-gated even if the local KWS runtime is temporarily inactive.
- Updated [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c):
  - `river_cloud_xiaozhi_idle_requires_wakeword()` now keys off build capability, not runtime `river_voice_kws_active()`
  - this removes the legacy fallback where idle pure-VAD speech could reopen XiaoZhi from the audio path when local KWS init/runtime was down
- Updated [components/river_voice/river_voice_frontend.c](/root/ameba-river/components/river_voice/river_voice_frontend.c):
  - corrected the boot warning text to state that idle VAD admission stays wakeword-gated while local KWS is inactive
  - this keeps logs aligned with the new policy instead of falsely claiming that legacy VAD admission remains enabled
- Why this step matters:
  - fixes the observed runtime where a single speech burst in `wake_monitoring` repeatedly printed:
    - `xiaozhi conversation window aborted: reason=followup_transport_unavailable`
  - prevents pure VAD from repeatedly trying to open a XiaoZhi follow-up session before any real wakeword/session window exists
  - keeps the intended contract intact:
    - first turn must come from wakeword admission
    - follow-up auto-open only happens inside an already-opened conversation window
- Verified a full local `RTL8730E` build after the fix.
- Image sizes after this step remained:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3593568`
  - `build_RTL8730E/ota_all.bin` = `3593600`

## Step 5.25
- Fixed the current `bc_resnet` wake path boot failure by increasing the KWS tensor arena in [prj.conf](/root/ameba-river/prj.conf):
  - `CONFIG_RIVER_KWS_TENSOR_ARENA_KB` from `160` to `192`
- Why this step was necessary:
  - the board log showed KWS never initialized after boot:
    - `Failed to resize buffer. Requested: 159744, available 152920, missing: 6824`
    - `kws AllocateTensors failed: arena=160KB model=56024B`
  - once `AllocateTensors` fails, local KWS is inactive, so no wakeword can ever be detected and XiaoZhi will never enter the websocket connect path from idle wake monitoring
- Why this is the minimal fix:
  - no SDK source was modified
  - no model asset or operator set was changed in this step
  - the failure was a straightforward tensor-arena capacity miss after the model swap, so the first correction is to size the arena for the real model footprint
- Verified a full local `RTL8730E` build after the config bump.
- Image sizes after this step remained:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3593568`
  - `build_RTL8730E/ota_all.bin` = `3593600`

## Step 5.26
- Fixed the `bc_resnet_best` wake path crash that happened on the first real KWS inference after `kws gate open`.
- Added project-side patched quantized `MEAN` support:
  - [components/river_voice/river_voice_kws_mean_patch.h](/root/ameba-river/components/river_voice/river_voice_kws_mean_patch.h)
  - [components/river_voice/river_voice_kws_mean_patch.cc](/root/ameba-river/components/river_voice/river_voice_kws_mean_patch.cc)
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - include the patched `MEAN` registration
  - replace the fixed `MicroMutableOpResolver` usage with a local resolver that can register the project-side patched builtin `MEAN`
  - keep the rest of the BC-ResNet operator set unchanged
- Updated [components/river_voice/CMakeLists.txt](/root/ameba-river/components/river_voice/CMakeLists.txt):
  - build the new patched `MEAN` source only when KWS capability is enabled
- Why this step was necessary:
  - the board log showed KWS boot was successful, but the first real inference crashed immediately after:
    - `kws gate open: ...`
    - `Data abort with Data Fault Status Register 0x00001a11`
  - symbolication placed the fault inside TensorFlow Lite Micro quantized `MEAN`
  - the failing CA32 store target was unaligned, so the safest fix was to keep SDK sources untouched and replace only the project's KWS-side `MEAN` registration path
- Why this is the chosen fix:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no wakeword model asset was changed again
  - the patch is scoped to the known BC-ResNet reduction patterns used by the current model
- Verified a full local `RTL8730E` build after the fix.
- Image sizes after this step:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3601760`
  - `build_RTL8730E/ota_all.bin` = `3601792`

## Step 5.27
- Added a training-side KWS export contract for future embedded-friendly wakeword models:
  - [knowledge/KWS_MODEL_EXPORT_CONTRACT_ZH.md](/root/ameba-river/knowledge/KWS_MODEL_EXPORT_CONTRACT_ZH.md)
- This document is intended to be handed directly to the model-training team.
- The contract makes the deployment boundary explicit:
  - fixed input/output expectations for `Ameba River`
  - int8-only export requirement
  - operator whitelist and blacklist
  - explicit prohibition on post-export graph rewriting
  - required delivery report fields and acceptance gates
- Why this step matters:
  - the current `bc_resnet_best.tflite` is usable, but it forced the firmware side to add a patched `MEAN`
- long-term binary size and maintenance are better served by a native export type that avoids `MEAN`
- this reduces ambiguity when handing requirements to the training side
- This step is documentation-only and does not change firmware code or binary assets.

## Step 5.28
- Fixed the boot-time KWS init regression introduced by the project-side patched `MEAN` op.
- Updated [components/river_voice/river_voice_kws_mean_patch.cc](/root/ameba-river/components/river_voice/river_voice_kws_mean_patch.cc):
  - simplified `river_voice_kws_mean_patch_prepare()`
  - removed the project-side tensor prevalidation from `prepare`
  - delegated `prepare` directly to SDK `tflite::PrepareMeanOrSumHelper(...)`
- Why this step was necessary:
  - the latest board boot log proved KWS never initialized, so wakeword could never trigger:
    - `axis->type != kTfLiteInt32 (0 != 2)`
    - `Node MEAN ... failed to prepare`
    - `kws AllocateTensors failed: arena=192KB model=56024B`
  - this failure happened in the patched op `prepare` path before any real KWS inference started
- Why this is the minimal fix:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no model asset was changed again
  - the project keeps the scoped patched `MEAN` eval path, but reuses the SDK's compatible `prepare` logic instead of trying to duplicate it locally
- Verified a full local `RTL8730E` build after the fix.
- Image sizes after this step remained:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3601760`
  - `build_RTL8730E/ota_all.bin` = `3601792`

## Step 5.29
- Fixed the next KWS runtime failure after boot init recovered: patched `MEAN` eval no longer depends on `node->builtin_data` staying valid.
- Updated [components/river_voice/river_voice_kws_mean_patch.cc](/root/ameba-river/components/river_voice/river_voice_kws_mean_patch.cc):
  - introduced a project-local patched op-data wrapper around `tflite::OpDataReduce`
  - cached `ReducerOptions.keep_dims` during `init`
  - switched `prepare` to pass the embedded `OpDataReduce` to SDK `PrepareMeanOrSumHelper(...)`
  - switched `eval` to use the cached `keep_dims` instead of reading `TfLiteReducerParams` from `node->builtin_data`
  - tightened axis resolution to reject reduce patterns with more than `2` axes instead of risking local buffer overwrite
- Why this step was necessary:
  - after Step `5.28`, the board log showed KWS could boot and open the gate, but actual inference still failed in the patched `MEAN` eval path:
    - `params != NULL was not true`
    - `Node MEAN ... failed to invoke`
    - `kws worker process failed: status=-6`
  - this proved the remaining blocker had moved from `prepare` to runtime `eval`
- Why this is the minimal fix:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no model asset was changed again
  - the patch keeps reusing SDK reduce prepare logic while only caching the single reducer flag that the project-side eval actually needs
- Verified a full local `RTL8730E` build after the fix.
- Image sizes after this step remained:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3601760`
  - `build_RTL8730E/ota_all.bin` = `3601792`

## Step 5.30
- Fixed the remaining KWS runtime failure where patched `MEAN` rejected the live `bc_resnet_best` reduce pattern as unsupported.
- Reviewed the embedded wakeword model topology from [components/river_voice/generated/river_wake_word_model_data.h](/root/ameba-river/components/river_voice/generated/river_wake_word_model_data.h):
  - all model `MEAN` nodes are normal reductions on axes `[2]`, `[1]`, or `[1,2]`
  - the blocker was not a new operator pattern in the model
  - the blocker was the project-side patch matching too narrowly on `keep_dims`
- Updated [components/river_voice/river_voice_kws_mean_patch.cc](/root/ameba-river/components/river_voice/river_voice_kws_mean_patch.cc):
  - changed patched `MEAN` eval matching from `axis + keep_dims` to `axis + output shape`
  - explicitly supports the observed model output layouts:
    - `axis=2` with rank-`4` output `[N,H,1,C]`
    - `axis=2` with rank-`3` output `[N,H,C]`
    - `axis=1` with rank-`4` output `[N,1,W,C]`
    - `axis=1` with rank-`3` output `[N,W,C]`
    - `axes=1,2` with rank-`4` output `[N,1,1,C]`
    - `axes=1,2` with rank-`2` output `[N,C]`
  - expanded the unsupported-pattern log so any remaining mismatch now prints:
    - `axes_len`
    - `axis0`
    - `axis1`
    - `keep_dims`
    - `in_rank`
    - `out_rank`
- Why this step was necessary:
  - the latest board log showed KWS could now boot and run, but each real inference still failed with:
    - `river kws mean patch got unsupported reduce pattern`
    - `Node MEAN (number 3) failed to invoke`
    - `kws worker process failed: status=-6`
  - model inspection showed op `3` is a standard `MEAN(axis=[2])`, so the patch needed to recognize the real output layout instead of rejecting it
- Why this is the minimal fix:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no model asset was changed again
  - the fix stays scoped to the project's patched KWS `MEAN` eval path
- Verified a full local `RTL8730E` build after the fix.
- Image sizes after this step remained:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3601760`
  - `build_RTL8730E/ota_all.bin` = `3601792`

## Step 5.31
- Fixed the next KWS runtime crash after Step `5.30`: patched `MEAN` op-data is now returned to TFLM on an explicitly aligned address.
- Updated [components/river_voice/river_voice_kws_mean_patch.cc](/root/ameba-river/components/river_voice/river_voice_kws_mean_patch.cc):
  - changed `river_voice_kws_mean_patch_init()` to over-allocate by `alignof(river_voice_kws_mean_patch_op_data) - 1`
  - manually aligned the returned `user_data` pointer before storing the patched `OpDataReduce`
  - kept the patched `MEAN` logic otherwise unchanged
- Why this step was necessary:
  - the March 31, 2026 board log showed the previous `unsupported reduce pattern` failure was gone, but the first real KWS inference still crashed with:
    - `Data abort with Data Fault Status Register 0x00000221`
    - fault PC `0x6035f448`
  - symbolication mapped the fault to [components/river_voice/river_voice_kws_mean_patch.cc](/root/ameba-river/components/river_voice/river_voice_kws_mean_patch.cc):194, where the patched int8 `MEAN` path loads `output_scale`
  - the board register dump showed `R11 = 0x6067b663`, and disassembly proved `R11` is the patched `node->user_data` pointer, so the crash was caused by a VFP float load from an unaligned op-data address
- Why this is the minimal fix:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no model asset was changed again
  - the change is scoped to the project-side patched `MEAN` init path and directly addresses the observed unaligned `user_data` root cause
- Verified a full local `RTL8730E` build after the fix.
- Image sizes after this step remained:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3601760`
  - `build_RTL8730E/ota_all.bin` = `3601792`

## Step 5.32
- Replaced the project-side patched `MEAN` eval loop with the SDK's own `tflite::EvalMeanHelper(...)`, while still keeping the project-local fix for missing reducer params.
- Updated [components/river_voice/river_voice_kws_mean_patch.cc](/root/ameba-river/components/river_voice/river_voice_kws_mean_patch.cc):
  - removed the project-side int8/int16 `MEAN` math loop
  - kept the explicitly aligned patched op-data allocation from Step `5.31`
  - expanded patched op-data from `OpDataReduce + keep_dims` to `OpDataReduce + TfLiteReducerParams`
  - cached the full reducer params during `init`
  - temporarily reattached cached `TfLiteReducerParams` to `node->builtin_data` during `eval`
  - delegated runtime execution to SDK `tflite::EvalMeanHelper(...)`
- Why this step was necessary:
  - the latest March 31 board logs proved the old `unsupported reduce pattern` issue was gone, but real KWS inference still crashed inside the project-side eval path:
    - `Data abort with Data Fault Status Register 0x00000221`
    - fault PC moved to the patched `MEAN` eval body (`0x6035f474`)
    - register dumps still showed `node->user_data` corruption symptoms during `MEAN`
  - model inspection also showed the live `MEAN` topology is standard and already supported by the SDK reduce kernel, so keeping a custom math loop was unnecessary risk
- Why this is the minimal fix:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no model asset was changed again
  - the project-side patch now only repairs reducer metadata lifetime/alignment and leaves actual `MEAN` execution to the upstream kernel
- Verified a full local `RTL8730E` build after the fix.
- Image sizes after this step changed to:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3593568`
  - `build_RTL8730E/ota_all.bin` = `3593600`

## Step 5.33
- Captured the full `KWS MEAN` bring-up pitfalls, confirmed facts, and next-step decision in a standalone project document instead of leaving them fragmented across monitor logs and commit history.
- Added [doc/KWS_MEAN_OPERATOR_POSTMORTEM_ZH.md](/root/ameba-river/doc/KWS_MEAN_OPERATOR_POSTMORTEM_ZH.md):
  - summarizes the observed failure sequence from `params != NULL was not true` to the final SDK-side `QuantizedMeanOrSum(...)` data abort
  - separates confirmed facts from high-confidence inference
  - records why the team should stop spending the main effort on old-model `MEAN` repair
  - documents the integration stance for the upcoming no-`MEAN` model
- Updated [doc/README.md](/root/ameba-river/doc/README.md):
  - indexed the new `KWS MEAN` postmortem so it remains discoverable as part of migration / review material
- Why this step was necessary:
  - the March 31 board logs proved the current patched / SDK-delegated `MEAN` path still dies inside upstream quantized reduce scratch handling, so the important next action is to preserve the lessons and switch strategy before integrating the next model
  - without one consolidated document, the exact traps are easy to repeat when the no-`MEAN` model lands
- Why this is the minimal fix:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no runtime code or model asset was changed again
  - this step only records hard-won troubleshooting knowledge and sets up the next branch cleanly

## Step 5.34
- Isolated non-mainline voice code behind explicit build switches so the prep branch stays closer to the upcoming no-`MEAN` product path, with less dead code and fewer debug-only strings in the default image.
- Updated [Kconfig](/root/ameba-river/Kconfig):
  - added `CONFIG_RIVER_AUDIO_ECHO_DEBUG_EN`, default `n`
  - made `CONFIG_RIVER_AUDIO_ECHO_AUTOSTART` and `CONFIG_RIVER_AUDIO_ECHO_DIAG_DEFAULT_ON` depend on the echo-debug switch
  - added `CONFIG_RIVER_KWS_MEAN_PATCH_EN`, default `n`, for legacy-model-only `MEAN` adaptation code
- Updated [prj.conf](/root/ameba-river/prj.conf):
  - explicitly disables board audio echo debug build
  - explicitly disables the dedicated `WebRTC AECM` experiment build
  - explicitly disables the temporary KWS `MEAN` patch in this prep branch
  - keeps comments explaining that this branch is preparing for the incoming no-`MEAN` model rather than preserving the old troubleshooting path
- Updated [components/river_voice/CMakeLists.txt](/root/ameba-river/components/river_voice/CMakeLists.txt):
  - builds `river_voice_echo.c` only when `CONFIG_RIVER_AUDIO_ECHO_DEBUG_EN=y`
  - otherwise builds [components/river_voice/river_voice_echo_stub.c](/root/ameba-river/components/river_voice/river_voice_echo_stub.c) to keep interfaces stable
  - builds true `WebRTC AECM` experiment sources only when `CONFIG_RIVER_WEBRTC_AECM_EXPERIMENT_EN=y`
  - keeps only the FFT pieces always compiled because KWS still depends on `real_fft.h` / related code
  - builds `river_voice_kws_mean_patch.cc` only when `CONFIG_RIVER_KWS_MEAN_PATCH_EN=y`
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - isolates the patched `MEAN` registration behind `CONFIG_RIVER_KWS_MEAN_PATCH_EN`
  - adds the normal SDK `Register_MEAN()` path as the default resolver behavior
- Updated [components/river_diag/river_diag_cmd.c](/root/ameba-river/components/river_diag/river_diag_cmd.c) and [components/river_voice/river_voice_frontend.c](/root/ameba-river/components/river_voice/river_voice_frontend.c):
  - hides echo-only help text, commands, and startup hints when echo debug is compiled out
  - keeps `river audio probe ...` available in the default build
- Updated [components/river_voice/river_voice_preproc_fixed_dsb.c](/root/ameba-river/components/river_voice/river_voice_preproc_fixed_dsb.c):
  - wraps `WebRTC AECM`-only helpers so the lighter default build does not drag experiment-only code through the mainline preproc path
- Why this step was necessary:
  - the branch objective has shifted from rescuing the old `MEAN` model to preparing a clean landing zone for a replacement model with no `MEAN` operator
  - keeping echo debug, temporary `MEAN` adaptation, and dedicated `AECM` experiment code in the default image made the binary larger and the default code path harder to read
  - the generated configs now prove all three prep-branch gates are off by default:
    - `# CONFIG_RIVER_AUDIO_ECHO_DEBUG_EN is not set`
    - `# CONFIG_RIVER_WEBRTC_AECM_EXPERIMENT_EN is not set`
    - `# CONFIG_RIVER_KWS_MEAN_PATCH_EN is not set`
- Why this is the minimal fix:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - debug / experiment code is not deleted; it is compiled only when explicitly re-enabled
  - the mainline interfaces stay stable through the echo stub and existing voice abstractions
- Verified a full local `RTL8730E` build after the isolation changes.
- Verified the current build no longer compiles `river_voice_kws_mean_patch.cc`.
- Image sizes after this step changed to:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3556704`
  - `build_RTL8730E/ota_all.bin` = `3556736`
- Relative to Step `5.32`, the main app images dropped by `36864` bytes.

## Step 5.35
- Migrated the prep branch to the new no-`MEAN` KWS model and tightened the default KWS resolver so the mainline image only carries operators required by the current embedded model.
- Updated [components/river_voice/generated/river_wake_word_model_data.h](/root/ameba-river/components/river_voice/generated/river_wake_word_model_data.h):
  - replaced the embedded baseline asset with `bc_resnet_epoch1_debug.tflite`
  - preserved the exported symbol names `kws_model` / `kws_model_len`
  - new embedded model size is `54104` bytes
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - changed the default embedded model variant name to `bc_resnet_epoch1_debug`
  - added `AVERAGE_POOL_2D` registration for the new model
  - split resolver capacity into:
    - always-on mainline ops for the no-`MEAN` model
    - legacy-only `MEAN` / `FULLY_CONNECTED` compatibility ops behind a build switch
  - kept the default build path free of legacy KWS compatibility registrations
- Updated [Kconfig](/root/ameba-river/Kconfig) and [prj.conf](/root/ameba-river/prj.conf):
  - added `CONFIG_RIVER_KWS_LEGACY_MODEL_COMPAT_EN`, default `n`
  - made `CONFIG_RIVER_KWS_MEAN_PATCH_EN` depend on the new legacy-compat switch
  - explicitly kept `CONFIG_RIVER_KWS_LEGACY_MODEL_COMPAT_EN=n` in the prep branch and left the `MEAN` patch path unreachable in the default build
- Why this step was necessary:
  - the new model removes the problematic `MEAN` op entirely, but the runtime still needed `AVERAGE_POOL_2D` support to boot the graph
  - after the branch objective shifted to the no-`MEAN` model, leaving legacy KWS op registrations in the default resolver would keep unused code paths and strings in the mainline image
  - isolating legacy KWS compatibility at build time keeps future troubleshooting options without polluting the default product path
- Why this is the minimal fix:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - the old-model compatibility path is not deleted; it is explicitly opt-in
  - the mainline runtime now matches the actual operator set of the embedded no-`MEAN` model
- Verified a full local `RTL8730E` build after the migration and resolver cleanup.
- Verified generated configs keep legacy KWS compatibility compiled out by default:
  - `# CONFIG_RIVER_KWS_LEGACY_MODEL_COMPAT_EN is not set`
- Verified the current default build does not compile `river_voice_kws_mean_patch.cc`.
- Verified board flash and serial boot after the migration:
  - KWS init completed with `dims=[1,40,98,1]`, output `values=1`, and `model=54104B variant=bc_resnet_epoch1_debug`
  - `audio_echo=compiled=no` still proves the earlier non-mainline voice isolation remains effective
  - Wi-Fi association, DHCP, and SNTP all completed normally after reboot
  - observed live KWS runtime with no legacy-op failures:
    - `kws gate open`
    - `kws gate close`
    - `wakeword hit: text=小欧管家`
    - `wakeword queued`
  - observed wakeword-to-cloud handoff still works:
    - `xiaozhi connecting`
    - `Connected to websocket server`
    - `server hello: sid=...`
  - no `Node MEAN ...`, no `unsupported reduce pattern`, and no data-abort signature appeared during boot and init observation
- Image sizes after this step are:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3560800`
  - `build_RTL8730E/ota_all.bin` = `3560832`
- Relative to Step `5.34`, the main app images increased by `4096` bytes because of the new embedded model and its required op set, while the default build still keeps legacy KWS compatibility code compiled out.

## Step 5.36
- Fixed the post-session capture overflow on the no-`MEAN` KWS prep branch by moving follow-up timeout teardown fully out of the real-time capture path.
- Updated [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c):
  - kept `river_cloud_xiaozhi_check_window_timeout()` owned by the dedicated `river_xz_pump` task
  - removed the same timeout check from `river_cloud_xiaozhi_stream_push_frame()`, which is called synchronously from `river_vad_probe`
  - documented why timeout-driven websocket/session teardown must not run in the capture hot path
- Root-cause summary:
  - user logs showed `capture frame ring overflow` growing at roughly one frame per `16 ms`, which matches `river_vad_probe` fully stalling rather than merely slowing down
  - the overflow started shortly after `asr_session_closed -> follow_up`, aligning with the follow-up timeout window rather than with KWS or Wi-Fi bring-up
  - the xiaozhi timeout path can close the websocket/session, and that transport work is not acceptable inside the frame-by-frame capture consumer path
- Why this is the minimal fix:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no protocol behavior was changed; only timeout-teardown ownership moved back to the existing pump task that already polls xiaozhi state
  - the real-time audio path keeps its existing logic and simply stops performing potentially blocking timeout teardown inline
- Verified a full local `RTL8730E` build after the change.
- Verified board flash and serial runtime after the fix:
  - booted and stayed in the normal `wake_monitoring` state
  - `river xiaozhi bootstrap` completed and populated the xiaozhi runtime config
  - `river xiaozhi connect` completed and reached `server hello: sid=...`
  - after `river xiaozhi listen start`, `river xiaozhi listen stop`, and `river xiaozhi disconnect`, the board remained healthy with no repeated `capture frame ring overflow`
  - `river status` still reported `capture_service=running ... queue=0/100 ... dropped=0`
- Image sizes after this step remain:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3560800`
  - `build_RTL8730E/ota_all.bin` = `3560832`

## Step 5.37
- Tightened the xiaozhi follow-up reopen path after a more precise root-cause pass on the persistent overflow log.
- Updated [components/river_cloud/river_xiaozhi_ws.c](/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c):
  - added explicit websocket timeout policy for the xiaozhi transport:
    - receive timeout `10000 ms`
    - send timeout `200 ms`
    - connect timeout `15000 ms`
    - send queue block time `200 ms`
  - applied these settings before `ws_connect_url()`
  - rationale: SDK websocket defaults leave `send_block_time=30000 ms` and no socket send timeout, which is unacceptable when reopen control frames share the same transport as buffered audio
- Updated [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c):
  - added a fast `RIVER_ERR_BUSY` guard while `xiaozhi_listen_stop_pending` is still true
  - this serializes `listen stop` completion and the next `listen start`, so follow-up speech cannot re-enter the reopen path while the previous stream is still draining
- Refined root-cause summary:
  - the failing user log does not match follow-up timeout expiry; it stalls about `1.6 s` after renewed speech, exactly when the `100`-frame capture ring fills
  - that means `river_vad_probe` stops consuming immediately after post-session speech begins
  - the most credible blocking point is synchronous reopen work (`listen start`) colliding with residual xiaozhi websocket send backlog from the just-closed stream
- Why this is the minimal fix:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - the transport contract did not change; only timeout bounds and stop/start serialization were added around the existing xiaozhi session flow
  - the real-time path now prefers bounded `BUSY` backpressure over unbounded blocking
- Verified:
  - full local `RTL8730E` build still succeeds
  - image sizes remain unchanged:
    - `build_RTL8730E/km4_boot_all.bin` = `51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin` = `3560800`
    - `build_RTL8730E/ota_all.bin` = `3560832`
- Verification blockers on the current bench:
  - after reboot, the board associated to `ORVIBO`, but `xiaozhi bootstrap` hit `gethostbyname` failure on that network, so the exact online wakeword/follow-up path could not be re-run end-to-end
  - an additional auto-enter-download-mode timeout prevented immediately reflashing the second refinement from the current shell session

## Step 5.38
- Hardened the no-`MEAN` KWS runtime tensor binding on the input side to keep the current prep branch in a checkpointable usable state before the next serial round.
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - added a typed tensor-data accessor based on `tensorflow/lite/kernels/internal/tensor_ctypes.h`
  - replaced direct reads of `tensor->data.data` with typed pointer resolution for init-time input/output data capture
  - added a narrow runtime resync path that refreshes only `interpreter->input(0)` and its backing buffer before filling the KWS input tensor
  - changed input fill to return status so inference exits cleanly if runtime input binding is invalid
- Root-cause summary for this step:
  - failing field logs showed `kws tensor data drift: runtime_input=0x25262627 ...`, which means the cached view of the runtime input binding could become stale or nonsensical after init
  - a broader attempt that also refreshed runtime tensors after `Invoke()` caused a CA32 data abort in this SDK, so this step intentionally limits resync to the pre-inference input side only
  - the output binding remains cached on purpose because the safer goal here is to eliminate obvious stale-input writes without reintroducing the post-`Invoke()` crash
- Why this is the minimal fix:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no KWS model contract was changed; this is runtime binding hardening only
  - the change is isolated to the no-`MEAN` KWS path and leaves the already-isolated non-mainline voice code untouched
- Verified locally:
  - full `RTL8730E` build succeeds
  - image sizes remain unchanged:
    - `build_RTL8730E/km4_boot_all.bin` = `51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin` = `3560800`
    - `build_RTL8730E/ota_all.bin` = `3560832`
- Board verification is deferred to the next user-driven serial session:
  - confirm no new data abort
  - confirm the old `kws tensor data drift` warning no longer appears
  - confirm wakeword score is no longer pinned at the previously observed `140 pm` failure mode

## Step 5.39
- Closed the board-verification loop for the recent no-`MEAN` KWS runtime hardening and reclassified the remaining bench issue away from KWS.
- Verified from user-provided serial logs on the tagged prep branch:
  - boot is stable with no CA32 `Data abort`
  - KWS metadata is correct at runtime:
    - `kws quant: ... zp=-3 ... zp=-128`
    - `kws input shape: ... dims=[1,40,98,1]`
    - `kws output shape: ... dims=[1,1,1,1]`
    - `model=54104B variant=bc_resnet_epoch1_debug`
  - the old stale-binding symptom is gone:
    - no `kws tensor data drift`
  - the old no-wake failure is gone:
    - `wakeword hit: text=小欧管家 score_pm=265 q15=8704`
  - the previous follow-up/open-path stall is also gone:
    - no `capture frame ring overflow`
    - no KWS queue saturation / fast-growing drop counters
    - follow-up and barge-in reopened ASR successfully multiple times
    - conversation window closed cleanly on `followup_timeout`
- Remaining issue after this verification:
  - TTS playback still shows intermittent `underrun`
  - one observed `xiaozhi playback write failed: mono=960B stereo=1920B` pushed the interaction state through `error_recovering`, although the system recovered automatically
- Conclusion of this step:
  - current branch is now board-proven as `KWS wake + xiaozhi session + follow-up reopen` usable
  - the next debug target is playback buffer / write scheduling under TTS, not KWS model migration, tensor binding, or xiaozhi follow-up reopen correctness

## Step 5.40
- Fixed a newly exposed intermittent CA32 crash in the no-`MEAN` KWS prep branch by removing runtime re-fetch of `interpreter->input(0)` from the KWS worker path.
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - stopped calling `river_voice_kws_sync_runtime_tensors()` during every inference input fill
  - kept the init-time typed tensor-data resolution, but made runtime inference use only the already-cached `input_tensor_data`
  - reduced `river_voice_kws_fill_input_tensor()` to a simple cached-pointer validity check
- Root-cause summary:
  - the new user crash log hit `0x60354ab8`, which resolves to `tflite::GetTensorData<float>(TfLiteTensor*)`
  - call chain:
    - `river_voice_kws_task()`
    - `river_voice_kws_sync_runtime_tensors()`
    - `interpreter->input(0)` / `GetTensorData<float>()`
  - the fault happened before wake, right after Wi-Fi association, proving the remaining unstable path was the runtime KWS worker polling `input(0)`, not follow-up reopen, TTS, or cloud control
  - this confirms the Ameba/TFLM port does not safely support repeated runtime `input(0)` access from the worker thread
- Why this is the minimal fix:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no model contract or thresholds were changed
  - the change only removes the unsafe runtime rebind path; init-time tensor discovery remains intact
- Verified locally:
  - full `RTL8730E` build succeeds
  - image sizes remain unchanged:
    - `build_RTL8730E/km4_boot_all.bin` = `51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin` = `3560800`
    - `build_RTL8730E/ota_all.bin` = `3560832`
- Next board verification target:
  - confirm the early `Data abort` after Wi-Fi connect no longer appears
  - confirm wake still works after removing the unsafe runtime rebind

## Step 5.41
- Fixed a follow-up window state-sync gap that could leave the board unable to wake again after the first completed xiaozhi turn.
- Updated [include/river/river_cloud.h](/root/ameba-river/include/river/river_cloud.h):
  - added a lightweight cloud state-sync callback registration API
- Updated [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h):
  - stored the state-sync callback in cloud runtime context
  - exposed an internal helper so xiaozhi session paths can request a state recompute without depending on `river_core`
- Updated [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c):
  - implemented the new state-sync callback registration
  - added `river_cloud_request_state_sync(...)`
  - requested a state recompute on the direct `network_lost` teardown path
- Updated [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c):
  - requested a state recompute after `river_cloud_xiaozhi_window_close(...)`
  - requested a state recompute after `river_cloud_xiaozhi_window_abort_local(...)`
- Updated [components/river_core/river_app.c](/root/ameba-river/components/river_core/river_app.c):
  - wired the cloud state-sync callback to `river_session_coordinator_sync_interaction_state(...)` through a local wrapper
- Root-cause summary:
  - the user serial log showed `xiaozhi conversation window closed: reason=followup_timeout`, but there was no matching `interaction_state: follow_up -> wake_monitoring`
  - KWS detection is only allowed when `interaction_state == wake_monitoring`, so after the first turn the board could remain in a stale post-wake state and keep reporting `ready=no`
  - VAD still seeing later speech in that condition explains the observed symptom: capture stayed alive, but the second wakeword was ignored because the interaction state never re-armed KWS
- Why this is the minimal fix:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - `river_cloud` still does not directly depend on `river_core`; it only emits a generic "recompute state now" callback
  - the fix covers both normal follow-up timeout close and abnormal local-abort teardown paths that can otherwise leave the same stale state behind
- Verified locally:
  - full `RTL8730E` build succeeds
  - image sizes remain unchanged:
    - `build_RTL8730E/km4_boot_all.bin` = `51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin` = `3560800`
    - `build_RTL8730E/ota_all.bin` = `3560832`
- Next board verification target:
  - after xiaozhi reply playback drains and the window times out, confirm:
    - `xiaozhi conversation window closed: reason=followup_timeout`
    - `interaction_state: follow_up -> wake_monitoring reason=followup_timeout`
  - then trigger a second wake without rebooting and confirm:
    - `wakeword hit: text=小欧管家`
    - `wakeword queued text=小欧管家`

## Step 5.42
- Updated [plan.md](/root/ameba-river/plan.md) so the branch plan matches the current real priority:
  - the wake rearm regression is already fixed and verified
  - the current top issue is repeated playback causing heap collapse and intermittent `underrun`
  - the immediate next step is now explicitly `AudioTrack` reuse plus playback heap instrumentation
- Updated [include/river/river_playback_service.h](/root/ameba-river/include/river/river_playback_service.h):
  - added playback stats counters for track lifecycle:
    - `track_create_count`
    - `track_reuse_count`
    - `track_destroy_count`
- Updated [components/river_voice/river_playback_service.c](/root/ameba-river/components/river_voice/river_playback_service.c):
  - added a cached prepared-track state so compatible playback sessions can reuse an existing `AudioTrack`
  - changed normal `stop` / `interrupt` handling to stop and flush the active stream while keeping the compatible track object alive for the next playback start
  - kept destructive release for incompatible reconfiguration or failed restart/init/start paths
  - added playback heap snapshots around start and stop:
    - `playback_start_prepare`
    - `playback_start_new`
    - `playback_start_reuse`
    - `playback_stop_prepare`
    - `playback_stop_cached`
  - extended playback logs so board logs now show whether a start used reuse:
    - `reuse=yes|no`
  - extended `river_playback_service_dump_status()` to expose track lifecycle counters as `track=create/reuse/destroy`
- Root-cause / design summary for this step:
  - recent user logs showed wake rearm was already fixed, but repeated xiaozhi turns drove `heap_free` from about `79KB` down to about `8KB`
  - the most suspicious local lifecycle was `river_playback_service_start_stream()`, which recreated and destroyed `AudioTrack` for every TTS turn
  - this step intentionally avoids changing SDK code or weakening admission / KWS gates; it only narrows the playback lifecycle and improves diagnostics
- Why this is the minimal change:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - playback reuse is limited to config-compatible starts; incompatible or failed paths still destroy and recreate the track cleanly
  - the new heap snapshots make it possible to validate or falsify the playback-leak hypothesis directly from serial logs
- Verified locally:
  - full `RTL8730E` build succeeds
  - output images remain unchanged:
    - `build_RTL8730E/km4_boot_all.bin` = `51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin` = `3560800`
    - `build_RTL8730E/ota_all.bin` = `3560832`
- Next board verification target:
  - trigger at least `3` consecutive xiaozhi wake -> ASR -> TTS -> follow-up-timeout cycles
  - confirm later TTS starts show `reuse=yes`
  - compare playback snapshots before and after each turn to verify `heap_free` no longer collapses across turns
  - confirm whether `underrun` frequency decreases or at least correlates with low-heap snapshots

## Step 5.43
- Updated [plan.md](/root/ameba-river/plan.md) again so the active branch plan matches the newest user logs:
  - playback reuse remains relevant, but the immediate blocker is now `KWS` queue saturation and control-item loss
  - the top priority is now `KWS` queue integrity before further playback tuning
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - changed the KWS input ring from `RIVER_AUDIO_FRAME_RING_MODE_SPSC` to `RIVER_AUDIO_FRAME_RING_MODE_LOCKED`
  - removed the old generic enqueue path that treated PCM and control items the same under overflow
  - added `river_voice_kws_clear_input_queue(...)` so gate-reset can explicitly drain stale backlog before rearming
  - changed `river_voice_kws_enqueue_reset(...)` to clear queued stale items before writing a fresh `RESET`
  - changed `river_voice_kws_enqueue_pcm(...)` so a full queue preserves an older control item instead of discarding it in favor of new PCM
  - added a new info log:
    - `kws gate rearm cleared stale queue: pcm=%lu ctrl=%lu`
- Root-cause summary:
  - the new user logs showed `queue=40/40` pinned, rapidly rising `dropped`, repeated `kws queue dropped control item: type=1`, and `river_kws` CPU climbing very high
  - `river_voice_kws.cc` initialized the worker input ring as `SPSC`, but the producer overflow path also performed `river_audio_frame_ring_read(...)` to evict old items
  - that violates the ring contract and makes the gate/reset control flow unreliable exactly in the failure mode seen on the board
  - once `RESET` control items are dropped under backlog, the worker can stay busy chewing stale PCM and never cleanly rearm for the next gate/open cycle
- Why this is the minimal change:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no KWS model, thresholds, features, or wake text were changed
  - the fix is limited to queue correctness and overflow policy, which is the narrowest local explanation for the observed logs
- Verified locally:
  - full `RTL8730E` build succeeds
  - output images remain:
    - `build_RTL8730E/km4_boot_all.bin` = `51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin` = `3560800`
    - `build_RTL8730E/ota_all.bin` = `3560832`
- Next board verification target:
  - confirm `kws queue dropped control item: type=1` disappears
  - confirm gate transitions no longer leave the queue pinned at `40/40` with fast-growing drop counters
  - confirm `kws gate rearm cleared stale queue: ...` appears when backlog has to be drained
  - then re-check whether wake hits recover or whether the next blocker is now the front-end audio amplitude / clipping path

## Step 5.44
- Saved the pre-refactor local workspace into git stash instead of committing user-owned files:
  - `stash@{0}: On kws-no-mean-model: pre-refactor-branch-worktree-backup-20260331`
  - this preserves local `TIPS.md` / `.env` changes without mixing them into the refactor history
- Created and switched to a new branch from the current fix baseline:
  - source branch: `prep/kws-no-mean-model`
  - source commit: `31bf4dd`
  - new branch: `refactor`
- Updated [plan.md](/root/ameba-river/plan.md) for the new branch:
  - changed the branch marker to `refactor`
  - made the branch objective explicit: behavior-preserving cleanup and refactor before more feature churn
  - promoted `Boundary Cleanup` to in-progress on this branch
  - added a dedicated refactor track with the first slicing priority:
    - `components/river_cloud/river_cloud_adapter.c`
    - `components/river_core/river_app.c`
    - `components/river_voice/river_voice_kws.cc`
  - changed the immediate next step from runtime bug validation to the first refactor slice on `river_cloud_adapter.c`
- Why this is the right branch kickoff:
  - the repository already has several runtime fixes landed, but the remaining work is increasingly constrained by large-file coupling rather than by a single missing feature
  - creating a dedicated `refactor` branch keeps structural cleanup separate from the hotfix line
  - stashing `TIPS.md` / `.env` avoids polluting branch history with user-owned review material or environment-local state
- Scope of this step:
  - no runtime code path changed
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - this step is only the branch/bootstrap and refactor-plan handoff
- Next implementation target on `refactor`:
  - first inspect and split `components/river_cloud/river_cloud_adapter.c`
  - move provider/session glue toward smaller implementation units while keeping the external `river_cloud` contract stable

## Step 5.45
- Updated [plan.md](/root/ameba-river/plan.md) to switch the active branch theme from generic refactor-first to performance-first:
  - the current top priority is now explicit KWS realtime throughput and queue-backlog control
  - the immediate next step is no longer cloud-structure slicing, but KWS worker wakeup and pre-roll burst mitigation
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - raised the KWS worker priority from `4` to `5` so the consumer is less likely to be starved by same-priority producer-side work
  - replaced the old idle poll delay path with event-driven wakeup using a worker semaphore
  - drained the worker signal when KWS is disarmed so stale wakeups do not keep the worker spinning on an empty queue
  - kept signaling on both PCM enqueue and RESET enqueue so the worker can react immediately without waiting for a periodic poll
  - changed pre-roll replay on gate-open from "flush everything" to "keep only the most recent limited window and flush that"
  - capped the gate-open pre-roll burst with:
    - `RIVER_KWS_PRE_ROLL_FLUSH_MAX_FRAMES = 8`
  - added a new log when older pre-roll history is intentionally trimmed:
    - `kws pre-roll trim: dropped=%lu keep=%u/%u`
  - extended boot/profile logs so runtime now shows:
    - worker wake model = `event`
    - worker wait budget
    - pre-roll flush cap
- Root-cause / performance summary:
  - user logs showed `queue=40/40` saturation, fast-growing drop counters, and `river_kws` CPU rising sharply before reliable wake returned
  - after the previous control-item fix, the next clear bottlenecks were:
    - gate-open pre-roll being flushed as a burst into the KWS queue
    - the KWS worker still relying on an empty-queue poll/sleep loop instead of prompt event wakeup
    - producer and consumer operating at the same task priority
  - this combination is a classic realtime backlog amplifier: burst enqueue reduces headroom, equal-priority scheduling delays catch-up, and polling adds avoidable latency under light load
- Why this is the right first performance slice:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no wakeword model, thresholds, or feature extraction math were changed
  - the step only changes scheduling, wakeup behavior, and backlog policy in the hottest local queue
  - this directly targets realtime latency and queue occupancy before broader cloud/playback optimization
- Expected effect:
  - lower queue occupancy immediately after gate-open
  - fewer long-lived `queue=40/40` plateaus
  - less idle CPU waste from the KWS worker
  - better probability that fresh speech frames are processed before they become stale backlog
- Next board verification target:
  - confirm the boot/profile logs show:
    - `kws worker: ... wake=event ... pre_roll_flush=8`
    - `kws backend: ... pre_roll_flush=8 ...`
  - confirm `kws gate open` is no longer followed by near-immediate queue saturation
  - confirm `kws pre-roll trim: ...` appears when gate-open would otherwise replay too much backlog
  - re-check whether wake reliability improves under repeated short speech bursts

## Step 5.46
- Updated [plan.md](/root/ameba-river/plan.md) for the second KWS performance slice:
  - the event-driven worker / pre-roll-cap step is now treated as the completed first cut
  - the active focus is now separating reset semantics from queued PCM backlog and adding proactive overload trimming
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - removed `RESET` as a queued KWS input item and replaced it with a dedicated `reset_pending` signal
  - changed the worker loop so pending reset is consumed before reading more queued PCM, which gives reset semantics priority over backlog
  - changed gate rearm queue clearing from item-by-item drain to queue-count + reset, which is cheaper and avoids spending extra CPU clearing stale PCM
  - added proactive PCM backlog trimming:
    - when the input queue reaches the high-water mark, the oldest PCM is dropped until the queue falls back to a lower target
    - with the current `CONFIG_RIVER_KWS_INPUT_QUEUE_FRAMES=40`, the runtime trim policy is `30 -> 13`
  - kept the existing single-frame overflow fallback, but removed the old control-item preservation branch because control and PCM no longer share the queue
  - added new observability for overload control:
    - `kws input trim: dropped=... queue=...->... target=...`
    - `kws status: ... trim_ops=... trim_drop=...`
    - `kws worker: ... trim=30->13`
    - `kws backend: ... trim=30->13`
- Root-cause / performance summary:
  - after Step 5.45, the worker wakeup path was better, but user logs still showed `queue=40/40` plateaus, high `river_kws` CPU, and stale PCM backlog dominating the queue
  - as long as reset shared the same queue with PCM, aggressive trimming risked damaging control ordering
  - separating reset semantics makes it safe to trim old PCM harder, which is the correct realtime tradeoff for this stage
- Expected effect:
  - reset is no longer blocked behind queued PCM backlog
  - the queue can recover from saturation faster instead of remaining pinned at `40/40`
  - consumer time is spent more on fresh speech frames and less on stale backlog
  - logs can now distinguish generic drop growth from intentional high-water trim behavior
- Scope / guardrails:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no KWS model, thresholds, or feature-extraction math changed in this step
  - this slice stays strictly inside the KWS hot path and overload policy
- Local verification snapshot:
  - full `RTL8730E` rebuild passed after this change
  - output images remained:
    - `build_RTL8730E/km4_boot_all.bin 51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
    - `build_RTL8730E/ota_all.bin 3560832`

## Step 5.53
- Switched the default baseline KWS asset from `bc_resnet_epoch1_debug.tflite` to `/root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_v3_production.tflite`.
- Updated [components/river_voice/generated/river_wake_word_model_data.h](/root/ameba-river/components/river_voice/generated/river_wake_word_model_data.h):
  - regenerated the embedded `kws_model` payload from `bc_resnet_v3_production.tflite`
  - kept the exported symbol names `kws_model` / `kws_model_len`
  - embedded model size remains `54104` bytes
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - changed the default runtime variant name from `bc_resnet_epoch1_debug` to `bc_resnet_v3_production`
- Deployment notes adopted from `/root/kws-training-pro/models/bc_resnet_iteration3/DEPLOYMENT_REPORT_FINAL.md`:
  - input shape remains `[1, 40, 98, 1]`
  - output shape remains `[1, 1, 1, 1]`
  - whitelist ops remain `PAD, CONV_2D, DEPTHWISE_CONV_2D, ADD, AVERAGE_POOL_2D, LOGISTIC`
  - no `MEAN` op is reintroduced, so the current mainline resolver stays valid
- Why this step is intentionally narrow:
  - the new production model keeps the same size and operator envelope as the current baseline, so this change can stay focused on model payload replacement rather than reopening runtime compatibility work
  - schema-driven quant parsing in the existing runtime will pick up the new input quantization automatically at boot
- Local verification snapshot:
  - full `RTL8730E` rebuild passed after this change
  - output images remained:
    - `build_RTL8730E/km4_boot_all.bin 51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
    - `build_RTL8730E/ota_all.bin 3560832`
  - `strings build_RTL8730E/km0_km4_ca32_app.bin` contains `bc_resnet_v3_production`
  - `strings build_RTL8730E/km0_km4_ca32_app.bin` no longer contains `bc_resnet_epoch1_debug`

## Step 5.47
- Updated [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h):
  - added compile-time macro `RIVER_CLOUD_BUSINESS_TIME_WAIT_REQUIRED`
  - the macro is derived from `RIVER_CLOUD_BACKEND_IFLYTEK_ENABLED`, so only builds that include the Iflytek split ASR/TTS path keep the strict time-ready gate
- Updated [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c):
  - split "system time is actually ready" from "the current build must wait for time before business may proceed"
  - added `river_cloud_business_time_ready()`
  - changed generic cloud business gates to use the new compile-time policy:
    - stream-open path waits for UTC only when Iflytek backend is compiled in
    - direct TTS submit path waits for UTC only when Iflytek backend is compiled in
  - kept SNTP start/kick behavior unchanged so XiaoZhi-only builds still sync time opportunistically, but are no longer blocked on it
- Updated [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c):
  - wake admission still kicks/seeds SNTP, but it no longer blocks on time when Iflytek ASR/TTS is absent
  - the "build-seeded utc estimate" log is now only emitted when a build actually requires time gating
- Behavior summary:
  - `CONFIG_RIVER_CLOUD_BACKEND_XIAOZHI_REALTIME=y` and `CONFIG_RIVER_CLOUD_BACKEND_IFLYTEK_SPLIT=n`:
    - wake/business admission no longer waits for `sntp ready`
  - `CONFIG_RIVER_CLOUD_BACKEND_IFLYTEK_SPLIT=y`:
    - original UTC/time-ready gate remains intact for Iflytek auth/signature flows
- Scope / guardrails:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - SNTP init/kick was not removed; only the business-side wait gate was isolated
- Local verification snapshot:
  - full `RTL8730E` rebuild passed after this change
  - output images remained:
    - `build_RTL8730E/km4_boot_all.bin 51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
    - `build_RTL8730E/ota_all.bin 3560832`

## Step 5.48
- Updated [plan.md](/root/ameba-river/plan.md):
  - recorded the new playback-stability sub-goal: when `xiaozhi` is about to reconnect under heap pressure, idle playback cache must yield to the connect path
- Updated [include/river/river_playback_service.h](/root/ameba-river/include/river/river_playback_service.h):
  - added `river_playback_service_release_idle_track_cache()` as a narrow playback-service contract for reclaiming cached `AudioTrack` resources only when playback is already idle
- Updated [components/river_voice/river_playback_service.c](/root/ameba-river/components/river_voice/river_playback_service.c):
  - implemented `river_playback_service_release_idle_track_cache()`
  - the helper only releases the cached track when all of the following are true:
    - playback service is initialized
    - state is `RIVER_PLAYBACK_IDLE`
    - a cached `AudioTrack` still exists
    - no track is started and no reference export is owned
  - this keeps the hot-path behavior unchanged while letting higher-priority flows reclaim memory from noncritical cache
- Updated [components/river_cloud/river_xiaozhi_ws.c](/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c):
  - added a low-heap preconnect guard for `river_xiaozhi_open_session()`
  - before OTA bootstrap / websocket connect, if free heap is below `64KB`, the connect path now tries to release idle playback cache first
  - if reclaim happens, logs now expose the exact heap change:
    - `xiaozhi preconnect reclaimed idle playback cache: heap_free=...->... threshold=65536`
- Root-cause / realtime summary:
  - the user-provided failure log showed `playback_stop_cached` left the system around `60KB` free, then the next wake-driven `xiaozhi` connect from `river_wake_evt` fell to `704B` free and died on a `640B` allocation
  - that is the wrong priority order for a realtime assistant:
    - a reusable playback cache is optional
    - the next session open is critical
  - this step explicitly flips that priority under memory pressure so optional playback reuse cannot block the next dialogue session
- Expected effect:
  - repeated wake -> TTS -> follow-up timeout -> wake cycles should no longer fail in `river_wake_evt` just because an idle playback cache is still occupying heap
  - the failure cascade:
    - `Malloc failed. Core:[CA32], Task:[river_wake_evt], ...`
    - followed by transport instability such as `WIFI TRX IPC 4 timeout`
    should be materially less likely or disappear in the reproduced scenario
  - when reclaim happens, the next TTS may recreate its `AudioTrack` instead of reusing it; this is an intentional tradeoff in favor of availability
- Scope / guardrails:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - websocket protocol, queue sizing, and ASR/TTS business logic were not changed in this step
  - the change only affects low-heap admission behavior before a fresh `xiaozhi` session open
- Local verification snapshot:
  - full `RTL8730E` rebuild passed after this change
  - output images remained:
    - `build_RTL8730E/km4_boot_all.bin 51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
    - `build_RTL8730E/ota_all.bin 3560832`

## Step 5.49
- Updated [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c):
  - re-armed the XiaoZhi follow-up conversation window inside `river_cloud_xiaozhi_open_session_and_listen()`
  - each fresh listen / ASR round now refreshes the window to `RIVER_CLOUD_XIAOZHI_WAKE_WINDOW_FOLLOWUP_MS`
  - added an inline comment documenting the exact failure mode this prevents:
    - the shorter post-TTS tail timer could expire while the user had already started the next utterance, so the websocket closed immediately after that ASR round finished
- Root-cause / session-contract summary:
  - after `tts stop`, the current code intentionally shortens the window to `RIVER_CLOUD_XIAOZHI_POST_TTS_SILENCE_CLOSE_MS`
  - if follow-up speech starts near the end of that shorter window, ASR can reopen and run normally, but the old deadline remains in force
  - then once that ASR round ends, `followup_timeout` fires almost immediately and closes the websocket even though the user just engaged a valid next turn
  - refreshing the window on listen/asr start fixes that contract mismatch without weakening the post-TTS idle close behavior
- Expected effect:
  - in logs like the user-provided case, a second `asr provider=xiaozhi_realtime session started sid=...` during follow-up should no longer be followed almost immediately by websocket close just because the prior post-TTS deadline had already expired
  - the websocket should remain open long enough for the second round to receive normal `stt/llm/tts` traffic, unless a real transport/server issue occurs
- Scope / guardrails:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no ASR/VAD thresholds, playback parameters, or transport buffer sizes changed in this step
  - only the follow-up window timing contract was adjusted
- Local verification snapshot:
  - full `RTL8730E` rebuild passed after this change
  - output images remained:
    - `build_RTL8730E/km4_boot_all.bin 51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
    - `build_RTL8730E/ota_all.bin 3560832`

## Step 5.50
- Updated [plan.md](/root/ameba-river/plan.md):
  - added a dedicated structural-refactor track for session state ownership
  - recorded that the next cleanup focus is `follow_up / listening / speaking` contract tightening rather than another blind hot-path tweak
- Updated [components/river_core/river_session_coordinator.c](/root/ameba-river/components/river_core/river_session_coordinator.c):
  - introduced an internal `river_session_phase_t` state machine for:
    - `booting`
    - `wake_monitoring`
    - `wake_confirmed`
    - `asr_streaming`
    - `follow_up`
    - `speaking`
    - `barge_in_listening`
    - `error_recovering`
  - added a dedicated coordinator `state_lock` so runtime session flags and phase transitions stop being open-coded writes spread across callbacks
  - centralized runtime `interaction_state` writes behind one helper:
    - `river_session_apply_phase_locked()`
    - public `interaction_state` is now derived from coordinator phase, instead of each callback directly calling `river_interaction_state_set(...)`
  - made `river_session_coordinator_sync_interaction_state()` compute a target phase from current runtime facts:
    - playback active
    - ASR session active
    - cloud conversation window active
  - added transition validation / warning logs inside the coordinator so future session-contract cleanup can see illegal or surprising jumps immediately
  - updated wakeword admission to check the coordinator-owned phase instead of re-reading external interaction state
  - updated barge-in TTS interruption gating to also read coordinator phase, so both state writes and critical state reads now stay within the same control-plane owner
  - updated wakeword success, ASR session start/close, ASR error, and playback error paths to transition through the coordinator phase machine
- Structural intent / why this matters:
  - this step does not yet redesign the XiaoZhi follow-up contract
  - it first fixes the control-plane shape so the project has a single runtime owner for interaction-state transitions
  - that mirrors the stronger device-state ownership seen in `xiaozhi-esp32`, without yet rewriting transport or audio behavior
- Expected effect:
  - future `follow_up` and `barge_in` work now has one place to tighten state transitions instead of auditing multiple callbacks again
  - wakeword admission and runtime state sync now share the same coordinator-owned phase source, reducing hidden divergence between “what logs say” and “what callbacks think the state is”
  - if the system still exhibits odd state jumps, new warning logs from the phase machine should make them easier to isolate
- Scope / guardrails:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no audio thresholds, queue sizes, or XiaoZhi wire protocol behavior changed in this step
  - this is a control-plane refactor only; behavior is intended to remain functionally equivalent
- Local verification snapshot:
  - full `RTL8730E` rebuild passed after this change
  - output images remained:
    - `build_RTL8730E/km4_boot_all.bin 51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
    - `build_RTL8730E/ota_all.bin 3560832`

## Step 5.51
- Updated [plan.md](/root/ameba-river/plan.md):
  - added a dedicated `Phase 4.2: XiaoZhi Runtime Ownership`
  - recorded that the next structural step is to pull `listen_start / listen_stop / follow_up rearm` behind the session module as well
- Updated [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h):
  - declared shared `xiaozhi` runtime helper APIs for:
    - pending-text reset
    - playback stop arm/cancel/reset
    - downlink reset
    - transport-local runtime reset
- Updated [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c):
  - moved `xiaozhi` local runtime ownership further into the session submodule
  - added shared helpers:
    - `river_cloud_xiaozhi_clear_pending_text()`
    - `river_cloud_xiaozhi_cancel_playback_stop()`
    - `river_cloud_xiaozhi_mark_playback_started()`
    - `river_cloud_xiaozhi_arm_playback_stop()`
    - `river_cloud_xiaozhi_reset_playback_state()`
    - `river_cloud_xiaozhi_reset_downlink_state()`
    - `river_cloud_xiaozhi_reset_transport_state()`
  - reused the new pending-text helper from `river_cloud_xiaozhi_open_session_and_listen()`
  - added a brief ownership comment so future cleanup stays anchored in this file
- Updated [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c):
  - removed duplicated adapter-local helpers for playback/downlink reset
  - switched playback start / write-fail / stop-deadline handling to session-owned helpers
  - switched `tts start` / `tts stop` handling to session-owned playback helpers
  - switched `transport_closed`, `network_lost`, and `river_cloud_asr_audio_close()` to the shared `river_cloud_xiaozhi_reset_transport_state(...)` cleanup path
  - kept behavior-specific choices explicit:
    - `transport_closed` still emits `session_closed`
    - `network_lost` and `audio_close` still do not synthesize that event
- Structural intent / why this matters:
  - this step keeps the protocol and hot path unchanged
  - it reduces the number of places that manually manipulate the same `xiaozhi` runtime bits
  - it follows the same general lesson seen in `xiaozhi-esp32`: event handlers should describe causes, while state ownership should stay centralized
- Expected effect:
  - transport-loss and audio-close cleanup paths are less likely to drift out of sync over time
  - future refactors around `listen_stop_pending` and follow-up rearm now have one helper surface to extend instead of several copied reset blocks
  - runtime status after teardown should be more consistently cleared in one pass
- Local verification snapshot:
  - full `RTL8730E` rebuild passed after this change
  - output images remained:
    - `build_RTL8730E/km4_boot_all.bin 51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
    - `build_RTL8730E/ota_all.bin 3560832`

## Step 5.52
- Updated [components/river_voice/river_playback_service.c](/root/ameba-river/components/river_voice/river_playback_service.c):
  - included `audio_control.h` in the playback service path
  - added `river_playback_service_prepare_output_locked()` so each playback stream start now explicitly:
    - un-mutes playback
    - un-mutes the amplifier
    - sets hardware playback volume to `1.0 / 1.0`
  - kept this in the shared playback service so all speaker-bound playback streams benefit, not only one backend
- Updated [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c):
  - added a XiaoZhi playback saturation helper
  - changed downlink mono->stereo expansion to apply a `2x` software PCM gain before writing to `AudioTrack`
  - extended the playback-start log to expose the active `gain=2/1`
- Updated [components/river_cloud/river_tts_iflytek_ws.c](/root/ameba-river/components/river_cloud/river_tts_iflytek_ws.c):
  - raised `iflytek_tts` stream volume from `0.85` to `1.00`
- Behavioral intent:
  - the previous XiaoZhi path had already reached `AudioTrack_SetVolume(..., 1.0, 1.0)`, so further loudness increase required changes below or alongside the track-volume layer
  - this step therefore boosts loudness in two places:
    - hardware playback volume
    - XiaoZhi PCM amplitude, with int16 saturation to avoid wraparound
- Expected effect:
  - XiaoZhi TTS should be noticeably louder on the board, not just slightly louder
  - Iflytek TTS also stops leaving `15%` of stream-side volume unused
  - clipping risk is bounded by saturation rather than integer overflow
- Local verification snapshot:
  - full `RTL8730E` rebuild passed after this change
  - output images remained:
    - `build_RTL8730E/km4_boot_all.bin 51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
    - `build_RTL8730E/ota_all.bin 3560832`

## Step 5.53
- Added repo-owned KWS export tooling so model deployment can be fixed without editing `/root/kws-training-pro`:
  - [tools/kws/export_bc_resnet_tflite.py](/root/ameba-river/tools/kws/export_bc_resnet_tflite.py)
    - exports `BC-ResNet` checkpoints to `float32` or `int8` TFLite
    - keeps the current board-compatible operator set
    - uses board-aligned frontend features from `river_kws_features.py` for `int8` representative calibration instead of random Gaussian tensors
  - [tools/kws/embed_tflite_model.py](/root/ameba-river/tools/kws/embed_tflite_model.py)
    - converts a `.tflite` into the existing `#pragma once` + `static const` header format used by the firmware
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - changed the default embedded-model variant string from `bc_resnet_v3_production` to `bc_resnet_v3_production_fp32`
  - this makes serial-side runtime identification explicit for the current board validation step
- Regenerated [components/river_voice/generated/river_wake_word_model_data.h](/root/ameba-river/components/river_voice/generated/river_wake_word_model_data.h):
  - embedded a freshly exported float32 model from `/root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_best.pth`
  - embedded model length is now `77848` bytes
- Fix intent:
  - the current board failure strongly points to bad `int8` post-training calibration, not to the frontend path or wake phrase
  - this step therefore establishes a clean float32 on-board baseline first, while also landing a repo-owned calibrated `int8` export path for the next iteration
- Local and board-side verification snapshot:
  - float32 export passed:
    - `/tmp/bc_resnet_v3_production_fp32.tflite 77848`
    - input/output dtype: `float32`
  - full `RTL8730E` rebuild passed after embedding the float32 model
  - output images became:
    - `build_RTL8730E/km4_boot_all.bin 51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin 3585376`
    - `build_RTL8730E/ota_all.bin 3585408`
  - binary verification passed:
    - `strings build_RTL8730E/km0_km4_ca32_app.bin` contains `bc_resnet_v3_production_fp32`
  - board flash passed on `/dev/ttyUSB0` at `1500000` baud using project flash wrapper
  - post-flash serial command verification passed:
    - `river status` returned normal runtime status from the flashed board
  - remaining gap:
    - this run attached monitor after the reset window, so the boot-time `kws backend: ... variant=bc_resnet_v3_production_fp32` line was not captured in the same turn

## Step 5.54
- Switched the embedded wake-word model from the failed float32 baseline to a calibrated int8 deployment:
  - regenerated [components/river_voice/generated/river_wake_word_model_data.h](/root/ameba-river/components/river_voice/generated/river_wake_word_model_data.h)
  - the embedded model now comes from `/tmp/bc_resnet_v3_production_int8_cal.tflite`
  - exported model size is `54104` bytes
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - changed the runtime variant string from `bc_resnet_v3_production_fp32` to `bc_resnet_v3_production_int8_cal`
  - keeps serial-side model identification explicit after the int8 swap
- Updated [components/river_core/river_app.c](/root/ameba-river/components/river_core/river_app.c):
  - included `river_voice_kws_dump_status()` in `river_app_print_status()`
  - `river status` now exposes a direct KWS runtime line instead of forcing boot-log timing or incidental speech logs
- Fix intent:
  - the float32 board attempt failed at boot on `2026-04-01 12:48:29` with `kws AllocateTensors failed: arena=192KB model=77848B`
  - the immediate goal of this step was therefore to return to a board-fit model while preserving deterministic runtime observability
- Local and board-side verification snapshot:
  - corrected int8 export passed with representative calibration from real feature manifests:
    - `/tmp/bc_resnet_v3_production_int8_cal.tflite 54104`
    - exporter reported `representative_samples=256`
    - input quantization changed to approximately `scale=0.0179046784 zp=-7`
    - output quantization remained `scale=0.00390625 zp=-128`
  - rebuild passed after the int8 embed and status-path change
  - output images remained:
    - `build_RTL8730E/km4_boot_all.bin 51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
    - `build_RTL8730E/ota_all.bin 3560832`
  - binary verification passed:
    - `strings build_RTL8730E/km0_km4_ca32_app.bin` contains `bc_resnet_v3_production_int8_cal`
  - board flash passed on `/dev/ttyUSB0` at `1500000` baud
  - live runtime verification passed:
    - ambient speech produced `kws gate open` / `kws gate close` logs before the status-path patch was reflashed
    - after reflashing the status-path patch, `river status` on `2026-04-01 13:02:18` printed `river.voice.kws] kws status: ...`
    - no `AllocateTensors failed` line appeared in the int8-cal board runs
    - runtime stayed at `tasks=17`, matching an active KWS task rather than the earlier float32 fallback case

## Step 5.55
- Replaced the previous calibrated-int8 model identity with the algorithm team's final production deployment configuration from `/root/kws-training-pro/models/bc_resnet_iteration3/DEPLOYMENT_REPORT_V3_FINAL.md`.
- Updated [prj.conf](/root/ameba-river/prj.conf):
  - changed `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15` from the temporary permissive `8192` to `19660`
  - this matches the report's factory-default balanced threshold of `0.6`
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - changed the runtime model variant string from `bc_resnet_v3_production_int8_cal` to `bc_resnet_v3_production_final`
  - raised the gate fallback floor from `350 pm` to `400 pm` so fallback admission is not weaker than the report's documented high-sensitivity operating point
- Regenerated [components/river_voice/generated/river_wake_word_model_data.h](/root/ameba-river/components/river_voice/generated/river_wake_word_model_data.h):
  - re-embedded the model from `/root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_v3_production_final_v2.tflite`
  - upstream `final_v2` and `final` artifacts are byte-identical:
    - SHA-256 `4e7f368f67f9ba7ad98e1b037c1e447f3228dca43305a03e8b5cf46a533523d6`
    - size `54104` bytes
- Deployment intent:
  - keep the board on the algorithm team's final production model contract
  - align the firmware threshold with the report's recommended factory default instead of the earlier low validation threshold
  - preserve explicit runtime observability through the production-final variant string
- Local and board-side verification snapshot:
  - deployment report confirmed the same tensor contract as the final production drop:
    - input `int8`, scale `0.01790468`, zero-point `-7`
    - output `int8`, scale `0.00390625`, zero-point `-128`
  - rebuild passed after the production-final embed
  - output images remained:
    - `build_RTL8730E/km4_boot_all.bin 51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
    - `build_RTL8730E/ota_all.bin 3560832`
  - binary verification passed:
    - `strings build_RTL8730E/km0_km4_ca32_app.bin` contains `bc_resnet_v3_production_final`
    - no `bc_resnet_v3_production_int8_cal` string remained in the image
  - board flash passed on `/dev/ttyUSB0` at `1500000` baud on `2026-04-01 14:10`
  - post-flash monitor still reproduces the existing SDK-side issue:
    - connection succeeds
    - command-list discovery times out with `Failed to get cmd list: Get cmd list expired`
    - this did not block rebuild or flash, but it limited same-turn runtime command verification

## Step 5.56
- Forced deployment of the current upstream export result from `/root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_v3_production_final_v2.tflite` per user instruction, even though the exporter's parity gate reported mismatches.
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - changed the runtime model variant string from `bc_resnet_v3_production_final` to `bc_resnet_v3_production_final_v2`
- Regenerated [components/river_voice/generated/river_wake_word_model_data.h](/root/ameba-river/components/river_voice/generated/river_wake_word_model_data.h):
  - re-embedded the current export artifact from `/root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_v3_production_final_v2.tflite`
  - embedded artifact properties at deployment time:
    - size `54104` bytes
    - MD5 `0cd2c03ec46888ff0506a9e41ab7a33f`
    - SHA-256 `19fa4dca80ff4355b9de6da242789aabb16abed63820b2a3bd00a4c979a70a0b`
- Recorded upstream export caveat explicitly for deployment traceability:
  - exporter wrote `/root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_v3_production_final_v2.tflite.meta.json`
  - parity gate failed after writing the model:
    - `pt_vs_tflite mean_abs=0.054256`
    - agreement `114/128` at threshold `0.4`
    - agreement `121/128` at threshold `0.6`
    - agreement `123/128` at threshold `0.8`
  - threshold derivation emitted by the exporter:
    - `0.4 -> raw_ge=-25 raw_nearest=-26`
    - `0.6 -> raw_ge=26 raw_nearest=26`
    - `0.8 -> raw_ge=77 raw_nearest=77`
- Rebuilt and reflashed the Ameba image set with the forced-export model:
  - build completed successfully with unchanged image sizes:
    - `build_RTL8730E/km4_boot_all.bin 51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
    - `build_RTL8730E/ota_all.bin 3560832`
  - binary verification passed:
    - `strings build_RTL8730E/km0_km4_ca32_app.bin` contains `bc_resnet_v3_production_final_v2`
  - board flash passed on `2026-04-01 15:20` after reattaching the USB serial adapter into WSL
- Deployment notes:
  - initial flash attempt failed only because `/dev/ttyUSB0` was absent from WSL
  - host-side USB/IP inspection found the board on `BUSID 3-4` as `Prolific PL2303GC USB Serial COM Port (COM3)`
  - after `usbipd.exe attach --wsl --busid 3-4`, `/dev/ttyUSB0` reappeared and flash completed with `Finished PASS`
  - post-flash serial connection succeeded, but this turn did not capture a fresh boot banner because the monitor attached after the reset window

## Step 5.57
- Added [tools/kws/compare_triplet_kws.py](/root/ameba-river/tools/kws/compare_triplet_kws.py) to do offline triage on real `rtl8730e-board` WAVs:
  - loads the production checkpoint with the exporter's internal `TorchExportBCResNet`
  - runs the exported `bc_resnet_v3_production_final_v2.tflite`
  - compares the current training-side Python frontend against a board-faithful host replay path
- The new board-faithful host replay path intentionally mirrors current board C behavior more closely than `river_kws_features.py`:
  - applies the same Hann formula as [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc)
  - rounds the windowed samples back to `int16` before FFT, matching `river_voice_kws_capture_window()`
  - uses the same WebRTC fixed-point FFT implementation from `third_party/webrtc_aecm/aecm/{real_fft.c,complex_fft.c,signal_processing_library.cc}`
  - replays the same mel-band weighting and normalization contract as the board path
- Real-board WAV triage used three recorded samples from `/root/kws-dataset-pro-blueprint/data/augmented_final`:
  - positive wake word:
    - `device_recordings___positive___positive__pos_neutral_mid_001__小欧管家__take001__rtl8730e-board__rec-1774343625242-44fbbee1.wav`
  - hard negative near-homophone:
    - `device_recordings___negative___hard_negative__hn_shang_jia_001__小欧商家__take001__rtl8730e-board__rec-1774403309113-d87271e2.wav`
  - verifier-style context negative containing the wake phrase:
    - `device_recordings___negative___verifier_negative__vn_context_005__这是谁家的小欧管家__take001__rtl8730e-board__rec-1774401769981-9199eabe.wav`
- Observed comparison results:
  - positive wake word:
    - training-vs-board feature diff `mean_abs=0.002765`, `max_abs=0.026696`
    - PT score `0.828093` vs board-faithful PT score `0.827219`
    - TFLite score `0.843750 (raw=88)` vs board-faithful TFLite `0.855469 (raw=91)`
  - hard negative `小欧商家`:
    - training-vs-board feature diff `mean_abs=0.001837`, `max_abs=0.019436`
    - PT score `0.001380` vs board-faithful PT score `0.001369`
    - TFLite score `0.007812 (raw=-126)` vs board-faithful TFLite `0.015625 (raw=-124)`
  - context negative `这是谁家的小欧管家`:
    - training-vs-board feature diff `mean_abs=0.000637`, `max_abs=0.012912`
    - PT score `0.413958` vs board-faithful PT score `0.413964`
    - TFLite score `0.425781 (raw=-19)` vs board-faithful TFLite `0.414062 (raw=-22)`
- Diagnostic conclusion from this step:
  - current training-side feature extractor and board-faithful feature replay are close enough that frontend drift is not the primary explanation for the on-board repeated `0.375/raw=-32` behavior
  - the larger residual is still on the exported TFLite side rather than on the frontend side

## Step 5.58
- Added explicit board-side KWS inference diagnostics so repeated-confidence cases can be localized to a concrete stage instead of guessing from `score_pm` alone.
- Updated [Kconfig](/root/ameba-river/Kconfig):
  - added `CONFIG_RIVER_KWS_DIAG_VERBOSE_EN`
  - added `CONFIG_RIVER_KWS_DIAG_LOG_EVERY_INFER`
- Updated [prj.conf](/root/ameba-river/prj.conf):
  - enabled `CONFIG_RIVER_KWS_DIAG_VERBOSE_EN=y` for the current board-debug phase
  - kept `CONFIG_RIVER_KWS_DIAG_LOG_EVERY_INFER` disabled so log volume stays manageable unless a full per-inference dump is explicitly needed
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - added a pre-quantized feature hash over the normalized `40x98` feature window
  - added a quantized input tensor hash over the exact tensor bytes passed to TFLM
  - captured raw output scalar before score clamping:
    - `int8/uint8` models log the actual raw tensor value
    - `float32` models log the raw output in `milli`
  - tracked repeated-value streaks for:
    - raw output
    - pre-quantized feature hash
    - quantized input hash
  - added `kws diag: ...` logs that emit on:
    - the first few inferences
    - high-confidence frames
    - repeated-pattern streak milestones
  - extended `kws status: ...` to expose the last inference snapshot using explicit `last_*` labels so gate-reset idle states do not look like current inference values
- Live board observation from the new diagnostics on `2026-04-01 16:13:22`:
  - `kws diag: infer=2 gate=open out_type=int8 raw=52 score=0.703125 q15=23039 same=[raw:2 feat:1 input:1] feat_hash=0x94fb99dc input_hash=0x3cab68fc ...`
  - immediately followed by:
    - `wakeword hit: text=小欧管家 score_pm=703 q15=23039`
- Diagnostic conclusion from this step:
  - `same=[raw:2 feat:1 input:1]` means the current inference and the previous inference did **not** reuse the same features or the same quantized input tensor, but they **did** produce the same raw model output
  - that rules out a simple “frontend没变 / tensor没更新 / 阈值设错” explanation for this captured case
  - the repeated confidence is now much more likely to come from model/output-side collapse or overly coarse output behavior under nearby input windows
- Residual runtime note from the same live session:
  - right after the wake-path handoff, CA32 logged `Malloc failed. Core:[CA32], Task:[river_wake_evt], [free heap size: 1280] [xWantedSize:1408]`
  - this is separate from the KWS raw-repeat diagnosis, but it is worth tracking because it can destabilize post-wake behavior

## Step 5.59
- Reworked the board-side exact-tensor dump path from an unsolicited UART log burst into a pull-style snapshot transport, because the previous stream-based approach was not robust enough for this board/adapter combination.
- The immediate trigger for this step was a fresh raw serial capture after reset:
  - `/tmp/tty_capture.bin` contained `2092` bytes of `0x00`
  - that means the serial path can reach a bad “readable but only zero bytes” state even without `pyserial` monitor framing, so continuing to depend on hundreds of unsolicited dump lines would remain fragile
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - added an in-RAM KWS dump snapshot that captures, for the next armed inference only:
    - exact float32 feature tensor
    - exact raw input tensor bytes passed to TFLM
    - exact raw output tensor bytes returned by TFLM
    - snapshot-specific hashes, raw output scalar, score, q15, and inference sequence
  - replaced the old `dump next` behavior so it now only arms snapshot capture and logs a compact `kws tensor dump captured: ...` summary when the snapshot is ready
  - kept the existing `begin/meta/chunk` log format for host compatibility, but now emits those lines only on explicit query
  - extended KWS dump status with:
    - `armed=yes/no`
    - `ready=yes/no`
    - `capture_seq`
    - `capture_infer`
    - per-buffer chunk counts
- Updated [include/river/river_voice_kws.h](/root/ameba-river/include/river/river_voice_kws.h):
  - added public dump-buffer enum values for `feat_f32`, `input_raw`, and `output_raw`
  - added APIs to:
    - clear a cached snapshot
    - print snapshot metadata
    - print one selected chunk by label and index
- Updated [components/river_diag/river_diag_cmd.c](/root/ameba-river/components/river_diag/river_diag_cmd.c):
  - extended monitor commands to:
    - `river kws dump next`
    - `river kws dump off`
    - `river kws dump clear`
    - `river kws dump status`
    - `river kws dump meta`
    - `river kws dump chunk <feat_f32|input_raw|output_raw> <index>`
  - added argument validation so invalid chunk labels or indices fail immediately instead of silently producing unusable output
- Kept [tools/kws/replay_board_tensor_dump.py](/root/ameba-river/tools/kws/replay_board_tensor_dump.py) compatible by preserving the same `kws tensor dump begin/meta/chunk` line grammar; the only behavioral change is that logs are now pulled in smaller operator-controlled steps instead of dumped all at once.
- Local verification on `2026-04-02`:
  - full `RTL8730E` build passed
  - final image sizes were:
    - `build_RTL8730E/km4_boot_all.bin 51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin 3568992`
    - `build_RTL8730E/ota_all.bin 3569024`
  - board flash passed with `Finished PASS` at `2026-04-02 09:48:52`

## Step 5.60
- Added an explicit `no-cloud` KWS debug mode so the current board-side tensor replay workflow can be isolated from XiaoZhi session startup.
- Trigger for this step:
  - exact KWS dump capture was already succeeding on the board
  - but the post-wake handoff immediately entered `river_wake_evt`, hit a CA32 heap failure, and then blocked later `meta/chunk` retrieval:
    - `Malloc failed. Core:[CA32], Task:[river_wake_evt], [free heap size: 256] [xWantedSize:1408]`
    - later `IPC Get Semaphore Timeout`
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - added runtime `local_debug_mode`
  - added `river_voice_kws_wake_handoff_block_reason()` so KWS can explicitly tell the session layer why wake handoff must be suppressed
  - extended `river_voice_kws_dump_status()` with a dedicated debug line:
    - `local_only=yes/no`
    - `wake_handoff=blocked/normal`
    - `reason=local_debug|tensor_dump_ready|-`
  - kept the previous automatic `tensor_dump_ready` protection, so a captured snapshot still blocks handoff even if the operator forgot to toggle local debug first
- Updated [components/river_core/river_session_coordinator.c](/root/ameba-river/components/river_core/river_session_coordinator.c):
  - before scheduling wakeword follow-up, it now checks the KWS-side block reason
  - when blocked, it prints a clear reasoned log:
    - `wakeword handoff held: reason=local_debug ...`
    - or `wakeword handoff held: reason=tensor_dump_ready ...`
  - in this state the wakeword event is still detected and logged, but no XiaoZhi conversation window is opened
- Updated [components/river_diag/river_diag_cmd.c](/root/ameba-river/components/river_diag/river_diag_cmd.c):
  - added:
    - `river kws debug local on`
    - `river kws debug local off`
    - `river kws debug local status`
  - these are intentionally separate from `river xiaozhi disable`:
    - `xiaozhi disable` mutates runtime cloud config
    - `kws debug local on` is a focused board-debug guard that preserves the configured backend but suppresses wake-triggered cloud handoff
- Updated [include/river/river_voice_kws.h](/root/ameba-river/include/river/river_voice_kws.h) with the new KWS debug control/query APIs.
- Outcome for the current debug phase:
  - wakeword, feature extraction, TFLM inference, and tensor dump capture still run normally
  - but the board no longer needs to contact XiaoZhi before `river kws dump meta/chunk ...` can be pulled back
  - this keeps the three-way comparison workflow stable:
    - training-side
    - host TFLite replay
    - exact board tensor dump

## Step 5.61
- Reduced the board-side station credentials to a single configured AP as requested.
- Updated [include/river/river_wifi_credentials.h](/root/ameba-river/include/river/river_wifi_credentials.h):
  - primary SSID changed from `WLL2G` to `river`
  - primary password changed to `wobuzhidao`
  - secondary SSID/password cleared to empty strings
- Why clearing the secondary entry is enough:
  - [components/river_cloud/river_wifi_station.c](/root/ameba-river/components/river_cloud/river_wifi_station.c) already ignores empty SSIDs in `river_wifi_station_add_credential()`
  - so this change collapses the runtime credential set from `2` entries to `1` without touching the station state machine
- Expected runtime effect after flashing:
  - boot log changes from `autoconnect init: ap_count=2 primary=...` to `ap_count=1 primary=river`
  - scan/connect rotation no longer falls back to `ORVIBO`
  - all reconnect attempts stay pinned to the single configured SSID `river`

## Step 5.62
- Reverted the temporary TCP host-debug transport after the board/host bring-up attempt failed to produce a stable, trustworthy debug path.
- Revert scope:
  - removed the project-side TCP diag client and host helper
  - removed the extra `river tcpdiag ...` monitor surface
  - removed the dedicated TCP diag Kconfig/project config switches
  - restored the previous app boot path so the project returns to the serial/KWS debug baseline
- Why this step was taken:
  - the TCP path added another uncontrolled variable to wakeword debugging
  - the recent board-side wakeword issue is still a local KWS/feature/inference problem first
  - continuing to carry the TCP transport would mix infrastructure risk with model/runtime diagnosis
- The active working baseline after this revert is again:
  - single-AP Wi-Fi on `river`
  - local KWS debug mode available
  - pull-based tensor dump workflow available
  - no TCP host-debug transport in the firmware

## Step 5.63
- Added a dedicated board-side wakeword false-trigger investigation document:
  - [doc/KWS_BOARD_FALSE_WAKE_DEBUG_CHECKLIST_ZH.md](/root/ameba-river/doc/KWS_BOARD_FALSE_WAKE_DEBUG_CHECKLIST_ZH.md)
- This document consolidates the currently scattered evidence into one project-owned reference:
  - symptom evolution from the early `different input, same output` phase to the later `variable raw/score` phase
  - deployment issues already encountered on the model/export/runtime path
  - the current prioritized suspicion list
  - a staged analysis plan with explicit stop/continue gates
  - multiple debug methods and their individual exit mechanisms
  - the serial-debugging pitfalls already observed in practice
- The intent of this step is process control rather than code change:
  - future KWS diagnosis should follow the staged checklist
  - transport, cloud handoff, and experimental branches should no longer be mixed into the main false-trigger investigation path

## Step 5.64
- Added a dedicated FP32 wake-word deployment gate document for the current full product profile:
  - [doc/KWS_FP32_DEPLOYMENT_GATES_ZH.md](/root/ameba-river/doc/KWS_FP32_DEPLOYMENT_GATES_ZH.md)
- The document turns the previous qualitative conclusion into a concrete deployment contract for the algorithm/export side:
  - hard compatibility gates:
    - pure `FP32`, no hybrid
    - single-probability output
    - supported input shapes only
    - current resolver op whitelist only
  - memory gates derived from the current board baseline:
    - current mainline `KWS arena = 192KB`
    - current mainline `Silero VAD arena = 192KB`
    - recent full-profile runtime baseline:
      - `boot_ready heap_free ≈ 185216B`
      - `wifi_connected heap_free ≈ 132736B`
    - recommended direct-deploy FP32 KWS arena target:
      - `<= 224KB`
    - direct-deploy upper bound on the current full profile:
      - `<= 256KB`
    - beyond that, the model should no longer be treated as “swap-and-flash directly usable” on this branch
- The new document also distinguishes between:
  - secondary screening by `.tflite` file size
  - primary acceptance by actual `AllocateTensors()` arena demand
- It further defines the algorithm-side delivery package required for a one-shot board bring-up:
  - model/export provenance
  - IO shape/type summary
  - operator summary
  - threshold derivation
  - arena evidence
- Purpose of this step:
  - prevent future FP32 discussions from collapsing into “file size looks small enough”
  - give the training/export side a clear, board-derived target before producing the next model

## Step 5.65
- Added a dedicated board-memory explainer document:
  - [doc/RTL8730E_MEMORY_LAYERING_EXPLAINER_ZH.md](/root/ameba-river/doc/RTL8730E_MEMORY_LAYERING_EXPLAINER_ZH.md)
- The document consolidates the recent memory-capacity discussion into one project-owned reference and explicitly explains why:
  - the board can physically have `64MB` DRAM
  - while the current `CA32` runtime still only reports `100~200KB` of free heap
- The new document breaks the problem into the concrete layers that matter for model deployment:
  - physical DRAM capacity
  - current firmware-visible layout window
  - `CA32` carveout
  - static section occupancy
  - heap registration through `heap_5`
  - runtime free heap
  - largest-contiguous-free-block vs total free bytes
- It also includes a text memory-layer diagram tied back to the current project evidence:
  - boot log `0x60800000` vs `0x64000000`
  - `CA32_BL3_DRAM_NS` `4MB` carveout in the SDK layout
  - CA32 linker-script heap derivation via `__psram_heap_buffer_*`
  - runtime heap stats from the project
  - current KWS/VAD `TYPE_DRAM` arena pressure
- Purpose of this step:
  - stop future discussions from mixing “physical memory size” with “current application free heap”
  - provide a single reference that can be reused in KWS/FP32 deployment reviews

## Step 5.66
- Added a dedicated RTL8730E memory-layout options document:
  - [doc/RTL8730E_MEMORY_LAYOUT_OPTIONS_ZH.md](/root/ameba-river/doc/RTL8730E_MEMORY_LAYOUT_OPTIONS_ZH.md)
- This document consolidates the recent layout-adjustment discussion into one project-owned reference and answers:
  - whether the current branch should consider enlarging CA32-visible DRAM
  - why the answer is “yes, but not as the first response to the current FP32 model issue”
- The new document ties the recommendation back to current platform facts:
  - physical DRAM detection reaches `0x64000000`
  - current layout still caps `PSRAM_END` at `0x60800000`
  - current CA32 non-secure carveout is only `4MB`
  - CA32 heap is derived from the tail of that carveout
  - KM4 still has an explicit PSRAM heap-extend concept in the SDK
- It also adds a concrete option comparison table, including:
  - no-layout-change / module-trim validation path
  - in-window reshuffle path
  - conservative expansion path
  - `aivoice`-style expansion path
  - aggressive near-64MB expansion path
- For each path the document records:
  - expected benefit
  - platform risk level
  - recommended usage stage
  - verification focus
  - stop/rollback conditions
- Purpose of this step:
  - separate short-term model diagnosis from mid-term platform-capacity planning
  - provide a reusable decision document before any SDK-level memory-layout work is started

## Step 5.67
- Switched the board KWS experiment profile from the int8 baseline model to the pure-FP32 model:
  - `/root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_v3_fp32.tflite`
  - embedded as `components/river_voice/generated/bc_resnet_v3_fp32_model_data.h`
- Chose the first on-board FP32 deployment threshold from existing board recordings instead of reusing the int8 threshold:
  - deduped board recordings used for selection: `29` positive, `89` negative
  - zero-false-positive operating point on that set: `0.5217425823`
  - deployed threshold: `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=17096`
- Deliberately removed the most obvious KWS-side allocator pressure before judging CPU viability:
  - expanded `CONFIG_RIVER_KWS_TENSOR_ARENA_KB` from the old operating range to allow a much larger FP32 arena
  - set the current experimental arena to `768KB`
  - increased KWS worker stack to `12KB`
  - increased KWS input queue depth to `64` frames
- Refactored KWS runtime memory ownership so the large-but-simple buffers are no longer permanently embedded in the context object:
  - pre-roll ring backing storage now uses runtime allocation
  - input queue backing storage now uses runtime allocation
  - exact-tensor dump buffers now use lazy allocation only when the dump path is armed
- Added explicit FP32-oriented performance and memory observability:
  - `kws alloc` now prints `arena_used` and `arena_slack`
  - `kws memory plan` now prints init-time heap before/after, context size, queue/pre-roll/dump reservation sizes
  - periodic `kws perf` now prints last/avg/max inference time, slow-infer counters, heap watermark, and queue policy
  - runtime snapshot logs now include `river_kws` stack free bytes
  - slow inference warnings now trigger when single inference cost crosses `10ms` and `20ms`
- Kept the full product image enabled for this first FP32 viability pass:
  - VAD, XiaoZhi cloud path, and the rest of the shipping runtime are still present
  - the goal of this step is not “FP32 in a stripped lab image”, but “can FP32 survive in the actual product boot profile after relieving the obvious KWS memory bottleneck”
- Verified this step with a full local `RTL8730E` build on `2026-04-02`.
- Observed build artifacts after the successful build:
  - `build_RTL8730E/build/project_hp/image/km4_boot_all.bin` `51K`
  - `build_RTL8730E/build/project_lp/image/km0_image2_all.bin` `92K`
  - `build_RTL8730E/build/project_hp/image/km4_image2_all.bin` `371K`
  - `build_RTL8730E/build/project_ap/image/ap_image_all.bin` `3.0M`
  - `build_RTL8730E/km0_km4_ca32_app.bin` `3.5M`

## Step 5.68
- The first FP32 boot attempt exposed the next concrete limit after the memory expansion work:
  - boot failed before KWS runtime came up
  - CA32 reported `Malloc failed ... xWantedSize:786560`
  - this corresponds to the first large FP32 tensor-arena allocation attempt at `768KB`
- Treated that log as evidence that:
  - the platform no longer fails at the old `100~200KB` heap scale
  - but `768KB` is too aggressive for the current early-boot DRAM allocation window once allocator overhead is included
- Adjusted the FP32 experiment profile from `768KB` down to `688KB` so the board still gets a large FP32 arena while leaving explicit headroom for:
  - KWS queue backing storage
  - KWS pre-roll storage
  - KWS worker task creation
- Added finer-grained KWS init allocation logs so the next boot can distinguish these phases directly:
  - overall init plan before the arena allocation
  - post-FFT / pre-arena state
  - explicit `tensor arena alloc failed` log if the large block still cannot be reserved
  - post-arena state
  - runtime-buffer reservation summary
  - queue-storage / signal / task creation checkpoints
- Purpose of this step:
  - move from “one coarse malloc fail” to a staged boot-time memory trace
  - get the FP32 profile to boot so CPU latency can be judged from real `kws perf` logs rather than guessed from static model size

## Step 5.69
- Narrowed the current FP32 bring-up blocker from generic “tensor binding failed” to a more specific TFLM I/O allocation question.
- Added a new boot-time `kws io binding` log in [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc) that now prints:
  - `preserve_all`
  - runtime input/output tensor indices
  - runtime input/output tensor types
  - input/output `allocation_type`
  - input/output `bytes`
  - input/output raw data pointers
  - input/output dims pointers
  - input/output variable flags
  - `arena_used` and `arena_slack`
- Expanded the existing `kws tensor data invalid` failure log so it now preserves the same binding metadata at the exact failure point:
  - `input_data` / `output_data`
  - `input_raw` / `output_raw`
  - `input_alloc` / `output_alloc`
  - `input_idx` / `output_idx`
  - `arena_used` / `arena_slack`
- This makes the next board run able to distinguish at least these cases directly from one log capture:
  - wrapper exists and arena binding exists
  - wrapper exists but raw pointer is still null
  - runtime tensor is marked `dynamic`
  - failure correlates with an unexpectedly tiny or saturated arena plan
- In parallel, verified the current FP32 model itself on host:
  - `/root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_v3_fp32.tflite`
  - size `77848` bytes
  - sha256 `a61bdefb5bbc20c406128bb4d7619e7ea1649737672ac453e5335907e8bd0635`
  - full TF Lite host runtime can allocate it successfully
  - host runtime reports input `index=0`, output `index=76`, both `float32`
- Current conclusion after this step:
  - the FP32 flatbuffer is not obviously corrupt at the host-runtime level
  - the next board-side suspect remains TFLM runtime I/O buffer population or wrapper binding, not simple file corruption
- Verified this step with a full local `RTL8730E` build on `2026-04-03`.

## Step 5.70
- Implemented a conservative `RTL8730E` SDK memory-layout expansion to increase the current project's `CA32` heap without jumping directly to the larger `aivoice` layout.
- Added a repository-tracked SDK patch tool:
  - `tools/sdk/apply_rtl8730e_memory_layout_patch.py`
- Applied the SDK patch to `/root/ameba-rtos-1.2` with these effective values:
  - `PSRAM_END`: `0x60800000 -> 0x60C00000`
  - `CA32_BL3_DRAM_NS`: `0x60300000 ~ 0x60700000 -> 0x60300000 ~ 0x60B00000`
  - `KM4_DRAM_HEAP_EXT`: `0x60700000 ~ 0x60800000 -> 0x60B00000 ~ 0x60C00000`
  - `hal_platform.h` `PSRAM_END`: `0x60800000 -> 0x60C00000`
- Kept this step deliberately conservative so the next board run can answer one question clearly:
  - whether the current `FP32 KWS + Silero VAD` coexistence failure is primarily caused by the `CA32` carveout being too small
- Added a dedicated project document that explains the SDK-side edits and why only these files were changed:
  - `doc/RTL8730E_SDK_MEMORY_LAYOUT_CHANGES_ZH.md`
- Verified the patched SDK layout with:
  - `python3 tools/sdk/apply_rtl8730e_memory_layout_patch.py --check`
  - a full local `RTL8730E` rebuild on `2026-04-03`, which completed successfully across `ATF + CA32 + KM4 + KM0`

## Step 5.71
- The post-layout-expansion board log on `2026-04-03` shows the current blocker has moved away from memory:
  - `kws init plan: heap_free=4966336`
  - `boot_ready heap_free=3817792`
  - the earlier `Malloc failed ... xWantedSize:105536` no longer appears
  - FP32 KWS now completes init and produces varying scores, hashes, and tensor diagnostics
- The same board log exposed two immediate runtime limits instead:
  - wake scores only peaked around `q15=2157` / `score=0.0658`, far below the prior product threshold `q15=17096`
  - FP32 inference costs about `177ms`, which let the KWS queue grow to about `27/64` with the earlier `stride=4`
- Updated the current board smoke-test profile in `prj.conf`:
  - `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=1600`
  - `CONFIG_RIVER_KWS_INFERENCE_STRIDE_FRAMES=16`
- Kept both values explicitly marked in `prj.conf` as temporary smoke-test settings, not product tuning.
- Goal of this step:
  - prove or disprove that the end-to-end wakeword path can fire on board now that the memory ceiling is no longer the blocker
  - reduce scheduler pressure enough that the current FP32 profile can still be judged meaningfully before changing model or frontend behavior again
- Verified this step with a full local `RTL8730E` rebuild on `2026-04-03`, which completed successfully with `Build done`.

## Step 5.72
- The `2026-04-03 12:00:08` board log shows the lower smoke threshold and larger heap are active, but wake still does not trigger:
  - `threshold_q15=1600`
  - `thresh_pm=48`
  - `stride=16`
  - `kws pre-roll trim: dropped=12 keep=8/20`
  - observed gate-best scores only reached about `11pm`
- Treated that log as a timing-coverage problem, not another threshold problem:
  - `stride=16` reduced queue growth, but it sampled too sparsely on the slow `~178ms` FP32 path
  - each gate was trimming the configured `320ms` pre-roll from `20` frames down to only `8`, which likely discarded the wake phrase onset
  - VAD gate hold time remained short for a slow board path that often needs more than one aligned inference per utterance
- Implemented the next timing-debug profile:
  - `CONFIG_RIVER_KWS_INFERENCE_STRIDE_FRAMES=8`
  - `CONFIG_RIVER_SILERO_VAD_HANGOVER_FRAMES=18`
  - added new Kconfig/project config `CONFIG_RIVER_KWS_PRE_ROLL_FLUSH_MAX_FRAMES`
  - set `CONFIG_RIVER_KWS_PRE_ROLL_FLUSH_MAX_FRAMES=16` in `prj.conf`
- Updated `river_voice_kws.cc` so pre-roll flush depth is no longer hard-coded at `8`; it now follows the new project config and still reports the active value in the existing backend/profile logs.
- Goal of this step:
  - keep queue pressure under control while restoring enough temporal coverage to catch the wake phrase on board
  - test whether the current FP32 model can trigger once gate duration and pre-roll preservation are no longer the dominant bottlenecks

## Step 5.73
- The subsequent board log on `2026-04-03 13:34:47` already proved the current smoke profile can wake on board:
  - `wakeword hit: text=小欧家 score_pm=61 q15=2009`
  - `wakeword queued text=小欧管家 confidence=2009`
  - `interaction_state: wake_monitoring -> wake_confirmed`
  - `asr provider=xiaozhi_realtime session started`
- This step does not retune wake behavior again. It reduces serial noise while adding targeted visibility for transient runtime spikes inside `river_voice_kws.cc`.
- Added a compact peak-window logger for KWS:
  - new `kws peak:` line reports both instantaneous and window-peak values for:
    - `score_pm`
    - `gate_best_pm`
    - `infer_us`
    - queue depth
    - pre-roll depth
    - heap low-water mark
  - logs are emitted only when a meaningful new peak appears or on wake trigger
  - repeated peak logs are rate-limited with a minimum spacing of `500 ms`
- Replaced per-inference `kws infer slow:` spam with a throttled form:
  - still requires the existing slow-alert level
  - repeats only after the configured log interval or when latency worsens by at least `5000 us`
- Slowed the periodic KWS heartbeat in `prj.conf`:
  - `CONFIG_RIVER_KWS_LOG_PERIOD_MS: 2000 -> 5000`
- Expanded periodic `kws perf:` output so each heartbeat also preserves the last window's peak timing and queue context:
  - `infer_us ... win=...`
  - `heap ... win_low=...`
  - `queue ... win_peak=...`
  - `score[win_pm=... gate_best_pm=...]`
- Tightened the peak-reason logic so `infer` peaks are based on a real new latency peak, not on whether a previous slow log happened to print.
- Goal of this step:
  - keep background logs sparse enough for long serial captures
  - still expose the short-lived queue, latency, and score spikes that explain misses or regressions
- Verified this step with a full local `RTL8730E` rebuild on `2026-04-03`, which completed successfully with `Build done`.

## Step 5.74
- The first board run with the new peak logger on `2026-04-03 14:47:54` confirmed the mechanism works:
  - `kws peak: reason=score ...`
  - `wakeword hit: ...`
  - `kws peak: reason=trigger ...`
- That same log showed the remaining noise issue clearly:
  - a successful hit could still emit two back-to-back peak lines for the same window
  - the second `reason=trigger` line did not add materially new runtime data because `wakeword hit:` already marks the trigger
- Refined `river_voice_kws.cc` so trigger-forced peak logging now deduplicates against a just-printed peak line:
  - if a peak log was emitted within the minimum peak-log interval, the trigger path now only commits the peak snapshot internally
  - the separate `wakeword hit:` line remains unchanged
  - future peak detection still sees the updated trigger/score/infer baseline and does not repeatedly rediscover the same window
- Added a small internal helper to commit the current peak snapshot so logging and suppressed-trigger bookkeeping share one path.
- Goal of this step:
  - preserve the new transient diagnostics
  - remove the last obvious duplicate line during successful wake events
- Verified this step with a full local `RTL8730E` rebuild on `2026-04-03`, which completed successfully with `Build done`.

## Step 5.75
- The next board log on `2026-04-03 15:06:34` verified that duplicate `reason=trigger` peak logs are gone, but it exposed a remaining statistics bug:
  - later `kws peak: reason=queue ...` lines could still carry forward the previous window's `score_pm` / `gate_best_pm`
  - this happened because the next peak window was seeded from the last committed inference instead of starting clean
- Refined the KWS peak-window reset logic:
  - after a peak snapshot is committed, the next window now resets:
    - `infer_us = 0`
    - `score_q15 = 0`
    - `gate_best_q15 = 0`
  - queue depth, pre-roll depth, and heap low-water still restart from the current runtime baseline
- Goal of this step:
  - make each `kws peak` line describe only the current window
  - stop old wake scores from contaminating later queue-only or heap-only peak reports
- Verified this step with a full local `RTL8730E` rebuild on `2026-04-03`, which completed successfully with `Build done`.
- Flashed the rebuilt image to `/dev/ttyUSB0` on `2026-04-03`; `python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor` finished with `PASS`.

## Step 5.76
- Added a lightweight reset breadcrumb in `river_core` using SDK backup registers `BKUP_REG1-3`:
  - the app now records the latest interaction state and an 8-byte reason prefix on every interaction-state update
  - the next boot prints the previous recorded state together with `BOOT_Reason()`
  - this gives a project-side trace for resets that do not emit a panic or exception log
- Kept the implementation board-oriented and low-risk:
  - no SDK source changes
  - only reads/writes backup registers that already survive `system reset` / watchdog-class resets
  - status dump now includes the currently armed reset trace
- Board verification on `2026-04-03`:
  - full local `RTL8730E` rebuild completed with `Build done`
  - flashed to `/dev/ttyUSB0` and the flash tool finished with `PASS`
  - after issuing a manual `reboot` from monitor, boot log showed:
    - `KM4 BOOT REASON 400: APSYS`
    - `reset trace previous: boot_reason=0x0400 state=wake_monitoring reason8=network_ uptime_ds=62`
- Outcome of this step:
  - spontaneous resets can now be correlated with the last interaction phase visible to project code
  - this is specifically aimed at diagnosing the earlier no-panic reboot after follow-up timeout

## Step 5.77
- Addressed the `xiaozhi` websocket/uplink congestion path inside the project without patching the external SDK tree.
- Tightened websocket-side backpressure handling in `river_xiaozhi_ws.c`:
  - increased `RIVER_XIAOZHI_WS_QUEUE_MAX` from `4` to `8`
  - switched `ws_set_senddata_block_time()` to non-blocking for `xiaozhi`
  - added queue-watermark checks before `ws_sendBinary()` / `ws_send()`
  - audio uplink now reserves `2` queue slots so control JSON is less likely to be starved by audio bursts
  - added throttled diagnostics:
    - `xiaozhi ws backpressure: kind=... ready=... recycle=... max=...`
    - `xiaozhi_dump_status()` now prints websocket queue depth / peak / backpressure counters
- Added `xiaozhi uplink` retreat policy in `river_cloud_adapter.c`:
  - exponential backoff up to `160 ms` on `RIVER_ERR_BUSY`
  - trims stale queued uplink PCM down to `6` frames so ASR prefers fresh speech over delayed backlog
  - rate-limited runtime log:
    - `xiaozhi uplink backpressure: queued=... busy=... streak=... backoff=... stale_drop=...`
  - status dump now exposes:
    - uplink ring overflow drops
    - stale-drop count from congestion trimming
    - busy/fail counters
- Reset the new congestion bookkeeping when a fresh xiaozhi uplink session starts or transport state is torn down.
- Verified on `2026-04-03`:
  - full local `RTL8730E` rebuild completed successfully with `Build done`
  - flashed to `/dev/ttyUSB0`; `python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor` finished with `PASS`
  - live monitor capture after flashing showed:
    - the new project-side backpressure logs firing
    - no repeated SDK-side `WSCLIENT ERROR] ws_sendData: ERROR: Not get usable buffer...`
    - no sampled `xiaozhi playback write failed`
    - `river.voice.probe ... stream_busy=0` throughout the captured interaction window
- Current assessment:
  - websocket congestion still exists at the transport level, but it is now surfaced earlier and handled in a controlled way
  - the previous failure amplification path from queue-full -> SDK error spam -> playback error recovery is materially reduced

## Step 5.78
- Added a new long-horizon resource-constraint document for model research on the current `RTL8730E` board:
  - `doc/RTL8730E_LONG_TERM_MODEL_CONSTRAINTS_ZH.md`
- The new document is intentionally written for algorithm pre-research rather than for the current branch's one-off debugging:
  - separates physical hardware limits from the current project's conservative layout
  - separates structural platform limits from current software-policy bottlenecks
  - explains what parts of today's constraints are likely to move if the project later expands the memory layout or changes the runtime profile
- Captured the current board/resource picture with concrete numbers tied to the present branch:
  - physical external memory `64MB`
  - physical NOR flash `32MB`
  - current visible DRAM/PSRAM layout window `12MB`
  - current `CA32_BL3_DRAM_NS` carveout `8MB`
  - current `CA32` heap buffer `0x004DE000` (`5,103,616 bytes`)
  - current app package size `3,601,760 bytes`
  - current development flash-profile headroom `2,427,552 bytes`
  - current sampled runtime `heap_free` / `heap_min` values from the `2026-04-03` board log
- Added explicit guidance for model planning instead of only restating raw resources:
  - recommended additional flash budget for a new model
  - recommended additional runtime working-set budget
  - compute-budget targets for always-on VAD/KWS and for larger local models
  - guidance on when a new model should be treated as requiring a larger `CA32` carveout or a dedicated runtime profile
- Included two long-term engineering judgments that are directly relevant to the algorithm team:
  - replacing unsupported ops such as `MEAN` is usually preferable to board-side op-porting unless measured accuracy loss is materially unacceptable
  - `Cortex-A32 + NEON` means future `KleidiAI`-style CPU kernel optimization could help, but it does not remove flash / heap / cache-consistency / concurrency limits by itself

## Step 5.79
- Reviewed the two local `river_voice` changes before bringing the tree back to a clean git state.
- Kept the defensive tensor-name guard in `components/river_voice/river_voice_detector_silero.cc`:
  - the dump helper now uses a fallback name when `TF_LITE_STATIC_MEMORY` is enabled
  - in the current build it is effectively a no-op, but it avoids touching `tensor->name` if a future consistent static-memory build is introduced
- Rejected and removed the attempted `TF_LITE_STATIC_MEMORY` enable from `components/river_voice/CMakeLists.txt`:
  - enabling that macro only for `river_voice` is not safe because `TfLiteTensor` / related TFLM structs change layout under the macro
  - the SDK `tensorflow-microlite` static library was not being switched in lockstep, so keeping the define only in this component would risk an ABI mismatch
- Verified the cleaned state with a full local `RTL8730E` rebuild on `2026-04-03`; the rebuild completed successfully with `Build done`.
- Committed the review cleanup as a focused git step, then created branch `agent_server` from the cleaned result for later self-hosted-server debugging.

## Step 5.80
- Hardened the algorithm-side KWS evaluation flow in `/root/kws-training-pro` to eliminate train/validation leakage from repeated device-recording augmentations.
- Added `/root/kws-training-pro/kws_data_split.py`:
  - derives a stable origin key for each sample
  - uses `rec-*` recording ids for `device_recordings`
  - strips augmentation hash suffixes for non-device sources
  - performs grouped train/val splitting by `source + label + origin`
  - emits split summaries and asserts zero overlap
- Replaced `train_v2.py::prepare_data()` random per-sample splitting with grouped no-leakage splitting:
  - keeps all variants of one original recording on exactly one side
  - adds explicit `seed` handling and prints grouped split stats
  - this automatically hardens all training scripts that import `prepare_data` from `train_v2.py`
- Reworked `validate_final.py` so it no longer sweeps the full `device_recordings` pool:
  - it now derives a strict grouped holdout from the manifest
  - prints holdout bucket stats before evaluation
  - keeps threshold sweep behavior while making the evaluated set leakage-free
- Verified the new split logic on `device_recordings`:
  - `train_items=5250`, `val_items=600`
  - `train_groups=105`, `val_groups=12`
  - `shared_groups=0`

## Step 5.81
- Added a board-side KWS alignment replay path that reuses the existing exact tensor dump mechanism but makes the input deterministic.
- Chose the smallest practical implementation instead of a generic filesystem WAV player:
  - a single compiled-in mono `16 kHz / PCM16` wake-word sample
  - generated once from a real board recording
  - replayed through the same `river_voice_kws_submit_frame()` path used by live audio
- Added `tools/kws/generate_alignment_sample_header.py`:
  - converts one mono `16 kHz` WAV into `components/river_voice/generated/river_kws_alignment_sample_data.h`
  - prepends `20` synthetic silence frames (`320 ms`) so the KWS gate opens with deterministic pre-roll rather than with a tightly trimmed wake-word clip
- Added new board KWS debug APIs in `include/river/river_voice_kws.h` and `components/river_voice/river_voice_kws.cc`:
  - `river_voice_kws_dump_alignment_status()`
  - `river_voice_kws_run_alignment_sample(bool emit_dump)`
- The new replay path is intentionally guarded so the dump is not polluted by live mic input:
  - it refuses to run while `river audio probe` is still active
  - it refuses to run unless the interaction state is back in idle wake-monitoring
  - it drains the KWS worker queue before replay starts
  - it temporarily forces `local_debug_mode` so wake-word replay does not hand off into cloud dialogue
- Added worker-idle / snapshot-wait helpers in `river_voice_kws.cc` so replay can:
  - start from a clean KWS frontend state
  - pace frames in real time (`16 ms` per frame) instead of overflowing the `64`-frame KWS queue
  - wait for the first exact tensor snapshot before auto-dumping it
- Added automatic full dump emission after replay capture:
  - `kws tensor dump begin`
  - `kws tensor dump meta`
  - all `feat_f32`, `input_raw`, and `output_raw` chunks
  - then local debug / queue state is restored and the snapshot is cleared so normal wake-word handoff is not left blocked after the debug run
- Extended the serial diag command with:
  - `river kws align status`
  - `river kws align run`
- Verified on `2026-04-04`:
  - regenerated the compiled alignment sample header successfully
  - full local `RTL8730E` rebuild completed successfully with `Build done`

## Step 5.82
- Fixed the immediate board-side usability gap in the new KWS alignment flow: `river kws align run` no longer assumes local KWS was already initialized at boot.
- Root cause from the `2026-04-04` board log:
  - `river kws align status` showed `kws=closed probe=stopped interaction=wake_monitoring detection=ready`
  - so the failure was not the probe/interation guard; it was the closed KWS runtime itself
- Updated `components/river_voice/river_voice_kws.cc`:
  - `river_voice_kws_dump_alignment_status()` now prints an explicit hint when the KWS runtime is closed
  - `river_voice_kws_run_alignment_sample()` now performs lazy `river_voice_kws_init()` before replay when needed
  - lazy-init success and failure are logged explicitly:
    - `kws align lazy init: current_state=closed`
    - `kws align lazy init ok`
    - `kws align lazy init failed: status=...`
- Updated `components/river_diag/river_diag_cmd.c`:
  - `river kws align run` now prints the exact `river_status_t` failure code instead of the previous generic precondition hint
  - this makes the next board run actionable even if lazy init still fails for a deeper reason such as memory pressure
- Verified on `2026-04-04`:
  - full local `RTL8730E` rebuild completed successfully with `Build done`

## Step 5.83
- Fixed the actual KWS init regression behind the failed `river kws align run` attempts on `2026-04-04`.
- Root cause was confirmed from the local build artifacts, not guessed:
  - the SDK `tensorflow-microlite` library is compiled with `-DTF_LITE_STATIC_MEMORY`
  - `components/river_voice/river_voice_kws.cc` and `components/river_voice/river_voice_detector_silero.cc` were being compiled without that macro
  - this made board-side `TfLiteTensor` field access ABI-incompatible in those translation units
  - the resulting symptom matched the board log exactly:
    - `type=none alloc=unknown raw=0x0 dims=0x0`
    - `kws tensor data invalid`
    - `kws align lazy init failed: status=-3`
- Updated `components/river_voice/CMakeLists.txt`:
  - added `TF_LITE_STATIC_MEMORY` as a source-level compile definition for:
    - `river_voice_kws.cc`
    - `river_voice_detector_silero.cc`
  - kept the scope narrow so only the TFLM-facing translation units adopt the SDK tensor ABI
- Verified on `2026-04-04`:
  - regenerated build metadata and completed a full local `RTL8730E` rebuild with `Build done`
  - confirmed in `build_RTL8730E/build/compile_commands.json` that both KWS/VAD translation units now compile with `-DTF_LITE_STATIC_MEMORY`

## Step 5.84
- Tightened the board-side KWS alignment replay flow so `river kws align run` now keeps the best replay frame instead of dumping the first inference after gate-open.
- Root cause from the successful `2026-04-04` board replay log:
  - the replay path was using the generic one-shot `river_voice_kws_request_tensor_dump_next()`
  - the loop also stopped submitting frames as soon as the first snapshot became ready
  - this made the dump lock onto the early low-score frame:
    - `kws align replay captured: ... score=0.002818 q15=92`
  - while the same replay later reached the actual wake-word peak:
    - `wakeword hit: ... score_pm=910 q15=29835`
- Updated `components/river_voice/river_voice_kws.cc`:
  - added an internal tensor-dump mode split:
    - `next` keeps existing one-shot behavior for normal `river kws dump next`
    - `align_best` is used only by alignment replay and overwrites the snapshot when replay score improves
  - assigned a stable dump sequence when arming so one alignment run keeps one snapshot id even if the best frame is updated multiple times
  - extended tensor-dump status / capture logs to include the active dump mode for easier serial-side diagnosis
  - changed `river_voice_kws_run_alignment_sample()` to:
    - arm `align_best` instead of `next`
    - submit the full compiled sample plus all configured tail-silence frames
    - wait for worker idle at the end of replay instead of stopping on the first ready snapshot
    - fail explicitly if the replay finishes without producing any snapshot
- Verified on `2026-04-04`:
  - full local `RTL8730E` rebuild completed successfully with `Build done`

## Step 5.85
- Added an explicit host-side verification procedure for the now-corrected KWS alignment replay artifact.
- Recorded the key `2026-04-04` board-side success values that host replay must match:
  - `feat_hash=0x63dd772f`
  - `input_hash=0x3ec7297e`
  - `raw=911`
  - `score=0.910520`
  - `q15=29835`
  - `output_raw hex=df17693f`
- Documented one important operator constraint from the first host replay attempt:
  - the temporary file `/tmp/kws_align_dump_20260404_140825.log` is not a valid replay artifact
  - it only contains `feat_f32 chunk=1..179/245`
  - it contains no `input_raw` or `output_raw`
  - `tools/kws/replay_board_tensor_dump.py` therefore fails with `dump seq=1 incomplete: feat_f32, input_raw, output_raw`
- Added a stable capture-and-replay workflow to `.codex/verification.md` so the next run produces one complete monitor log and uses the same FP32 model variant as the board:
  - capture the entire `river kws align run` UART stream to a file until `kws align replay done: dump=emitted ...`
  - replay that file with `tools/kws/replay_board_tensor_dump.py`
  - override the script default model with `/root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_v3_fp32.tflite`, because the board runtime is using `bc_resnet_v3_fp32_experimental`, not the production-final quantized path

## Step 5.86
- Ran a board-state diagnostic using the user's required monitor path:
  - `ameba.py monitor -p /dev/ttyUSB0 -b 1500000`
- Confirmed the current blocker is no longer the host replay tool or the Step `5.84` board-side peak-frame capture logic.
- Actual monitor observations on `2026-04-04`:
  - standard monitor session connects successfully to `/dev/ttyUSB0` at `1500000`
  - the monitor's initial `AT+LIST` probe times out with `Failed to get cmd list: Get cmd list expired`
  - when rerun as `-reset -debug`, the same monitor shows only raw `0x00` bytes on RX
  - after the tool sends both:
    - `AT+LIST\r\n`
    - `reboot\r\n`
    there is still no printable board response, no `BOOT-I`, and no `ROM:[`
- Additional checks performed against the same board/session:
  - direct serial writes of:
    - `\\r`
    - `river kws align status\\r`
    - Realtek sync `ESC + \\r\\n`
  produced no readable monitor output
- Conclusion from this step:
  - the board is not currently in a usable interactive monitor state for `river kws align run`
  - because the RX stream is only `0x00`, no valid tensor dump can be captured, so host-side exact replay cannot proceed yet
  - this is a board/runtime-state blocker, not a host tooling mismatch

## Step 5.87
- Rechecked the board with a raw serial terminal at the same user-confirmed baudrate `1500000` and confirmed the shell is in fact interactive.
- New direct serial evidence from `2026-04-04`:
  - sending a bare `\\r` returns `#`
  - `river kws align status` responds with:
    - `kws align sample: source=compiled_pcm frame_samples=256 frames=145 duration_ms=2320 ...`
    - `kws align guard: kws=ready probe=running interaction=wake_monitoring detection=ready worker=idle snapshot=empty local_only=no`
- Captured one full live alignment replay log to:
  - `/tmp/kws_align_full_20260404_live.log`
- The captured board log is complete for host replay:
  - `kws align replay start:` present once
  - `kws tensor dump begin:` present once
  - `kws tensor dump meta:` present once
  - `input_raw chunk=1/245 ... 245/245`
  - `output_raw chunk=1/1`
  - `kws align replay done: dump=emitted`
- Root-caused the remaining host mismatch:
  - the board dump's `feat_f32` stream is correct
  - the board dump's `input_hash` equals the FNV hash of the raw `feat_f32` bytes:
    - `0x3ec7297e`
  - but the emitted `input_raw` stream itself hashes to a different value:
    - `0x0ea3e26b`
  - the first float of emitted `input_raw` is `df17693f`, which is the model output scalar, so this `input_raw` stream is not a faithful export of the real input tensor bytes
- Updated `tools/kws/replay_board_tensor_dump.py` to add a narrow float32 fallback:
  - if `input_raw` does not match `input_hash`
  - but raw `feat_f32` bytes do match `input_hash`
  - then replay uses `feat_f32` bytes as the effective input and reports `source=feat_f32_fallback`
- Verified on the captured live log:
  - `board_hash: feature=0x63dd772f input=0x3ec7297e`
  - `host_hash: ... effective_input=0x3ec7297e source=feat_f32_fallback`
  - `board_output: raw=911 score=0.910520 q15=29835`
  - `output_parity: bytes_equal=yes raw_equal=yes`
- This completes the host-side exact replay verification path for the current FP32 alignment artifact, while also documenting that the current board-emitted `input_raw` stream is anomalous.

## Step 5.88
- Fixed the board-side FP32 tensor dump capture point so `input_raw` is snapshotted before `Invoke()` can mutate or reuse the interpreter input buffer.
- Refactored exact dump capture into two phases inside `components/river_voice/river_voice_kws.cc`:
  - added dedicated staging buffers for the feature tensor and input tensor
  - populated those staging buffers during `river_voice_kws_fill_input_tensor()`
  - copied staging buffers into the final dump snapshot only when `river_voice_kws_capture_exact_tensors()` decides to capture the current inference
- Updated tensor-dump memory accounting and teardown accordingly:
  - reserved bytes now include both staging and final snapshot storage for feature/input
  - failure cleanup frees the new staging allocations
- Verified on `2026-04-04`:
  - full local `RTL8730E` rebuild completed successfully with `Build done`
  - flashed the new image to `/dev/ttyUSB0` at `1500000` using `tools/river_flash.py`
  - captured a fresh live alignment dump to `/tmp/kws_align_full.log`
  - board dump key values remained stable:
    - `feat_hash=0x63dd772f`
    - `input_hash=0x3ec7297e`
    - `raw=911`
    - `score=0.910520`
    - `q15=29835`
    - `output_raw hex=df17693f`
  - the corruption is gone:
    - `feat_f32 chunk=1/245` and `input_raw chunk=1/245` are byte-identical
    - `output_raw chunk=1/1` remains separate and equal to `df17693f`
  - host replay now consumes the real board-exported input tensor again:
    - `host_hash: ... logged_input=0x3ec7297e effective_input=0x3ec7297e source=input_raw`
    - `quant_parity: diff_bytes=0/15680 first_diff=[]`
    - `output_parity: bytes_equal=yes raw_equal=yes`

## Step 5.89
- Added a new Chinese runtime profiling document:
  - `doc/RUNTIME_RESOURCE_PROFILE_2026-04-04_ZH.md`
- The document consolidates the current local wakeup solution and implementation path using the actual code split:
  - interaction/window close -> `wake_monitoring`
  - `fixed_dsb` mono frontend
  - `silero VAD`
  - VAD-gated `bc_resnet_v3_fp32_experimental` KWS on TFLite Micro
  - wakeword queueing into XiaoZhi cloud reconnect
- Based on the user-provided `2026-04-04 15:42:06.287` to `15:42:20.844` runtime window, the document records:
  - overall CPU and heap averages / peaks
  - VAD task CPU and stack margin
  - KWS inference average / peak latency, queue buildup, pre-roll usage, and memory breakdown
  - interaction/cloud timing from wake hit to session queueing and websocket connect
  - stack margins for `vad`, `cap`, `kws`, and the `echo` zero-headroom risk
- Key quantitative conclusions captured in the document:
  - dual-core average busy rate in the sample window is about `9.0%`
  - sampled current free heap is stable around `3.52 MiB`
  - KWS average inference time is `178.764 ms`, above the `128 ms` stride budget by `50.764 ms` (`+39.7%`)
  - observed KWS queue peak reaches `32 / 64`
  - KWS reserved working set is about `816.4 KiB`
  - `echo` task stack free is `0 B` in both snapshots
- The document explicitly separates:
  - firmware-direct counters
  - values derived from the short sample window
  so the report does not overclaim long-run averages that are not actually present in the logs.

## Step 5.90
- Tuned the current wakeword operating point for higher recall on board by lowering
  `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15` in `prj.conf` from `1600` to `1024`.
- This moves the main trigger threshold from about `48 pm` down to about `31 pm`.
- Because the code-side fallback weak-threshold floor is hardcoded at `400 pm` but
  clamped to `primary - 1` when the primary threshold is lower, the effective weak
  threshold also drops with this change instead of staying at `400 pm`.
- Raised XiaoZhi downlink playback soft gain in
  `components/river_cloud/river_cloud_adapter.c` from `2/1` to `5/2` so cloud TTS
  output is louder without changing SDK-side speaker-volume plumbing.
- Updated `doc/KWS_PIPELINE_ZH.md` so the documented live KWS configuration matches
  the current firmware:
  - arena `688 KB`
  - primary threshold `1024`
  - stride `8`
  - queue `64`
  - pre-roll flush `16`
  - effective weak threshold behavior under the runtime clamp
- Rebuilt the firmware, reflashed the board successfully, and confirmed from the
  boot log that the new KWS threshold is live on device.

## Step 5.91
- Reviewed the new board wake logs after Step 5.90 and found the main blocker was
  no longer the threshold itself:
  - the eventual hit reached `score_pm=237`, far above the active `31 pm`
    threshold
  - but the FP32 worker stayed around `179-180 ms` per inference while running
    at stride `8`, so the queue kept growing into the `40+ / 64` range
  - the board started trimming queued audio (`kws input trim: dropped=27`), and
    the effective wake hit arrived late, after repeated user retries
- Retuned the runtime for freshness instead of lowering the threshold further:
  - changed `CONFIG_RIVER_KWS_INFERENCE_STRIDE_FRAMES` from `8` back to `16`
- Kept the lower threshold from Step 5.90 in place, because the logs show the
  current misses were dominated by stale inference / backlog rather than by a
  peak score barely missing the threshold.
- Updated `doc/KWS_PIPELINE_ZH.md` so the current-config table matches the new
  stride value.
- Rebuilt and reflashed the board, then confirmed from the boot log that the
  live runtime now reports `queue[frames=64 stride=16]`.

## Step 5.92
- After restoring `stride=16`, the next board logs showed the runtime backlog
  problem was materially improved:
  - queue stayed around `8-18 / 64`
  - no new `kws input trim` storm appeared during the sampled wake attempts
  - but natural wake attempts still topped out around `gate_best_pm=8` to
    `gate_best_pm=13`, which remained below the active `31 pm` threshold
- Lowered `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15` from `1024` to `384` so the
  live threshold moved from about `31 pm` down to about `11 pm` while keeping
  `CONFIG_RIVER_KWS_INFERENCE_STRIDE_FRAMES=16`.
- Updated `doc/KWS_PIPELINE_ZH.md` again so the documented current config now
  matches the latest board-tuned values:
  - threshold `384` (`~11 pm`)
  - stride `16`
- The new board log validates that this lower threshold is active and does
  improve real wake hits:
  - runtime status shows `thresh_pm=11 weak_pm=11`
  - a wake attempt reached `score_pm=28` and triggered immediately
  - the board entered the XiaoZhi wake flow and subsequent cloud/TTS session
    successfully
- During this step, firmware flashing at `1500000` intermittently failed with
  the existing `b'\\xe2'` transfer error on the large image. The successful
  deployment for this step used a lower flash baud only for programming; the
  normal debug monitor baud remains unchanged.

## Step 5.93
- Saved the current repository state as a clean milestone snapshot after the
  recent wake-word timing and threshold tuning work.
- Verified the worktree was already clean before snapshotting, so no source
  cleanup or rollback was needed.
- Added a focused archival commit for the snapshot record and tagged the current
  `refactor` baseline as `m7-realtime-wake-threshold-tuned`.
- The goal of this step is repository hygiene:
  - preserve a stable return point for later wake-word experiments
  - keep the worktree clean before the next board-debug cycle

## Step 5.94
- Added a persistent repository rule to [AGENTS.md](/root/ameba-river/AGENTS.md)
  for future wakeword-model debugging:
  - keep the existing board-side vs local comparison and parity code paths
  - do not delete or weaken tensor dump / alignment replay / comparison hooks
    just to speed up model bring-up
  - require an equivalent or stronger validation path before any future
    replacement of that infrastructure
- This step is a collaboration and debugging-discipline safeguard only.
- No firmware logic, serial-debug settings, model selection, or board runtime
  behavior was changed in this step.

## Step 5.95
- Added a parallel KWS model variant
  `student_bc_resnet_tiny_v2_fp32_debug` in
  [Kconfig](/root/ameba-river/Kconfig) for board/local parity work without
  replacing the committed mainline model selection.
- Imported the algorithm-side FP32 debug bundle as
  [student_bc_resnet_tiny_v2_fp32_model_data.h](/root/ameba-river/components/river_voice/generated/student_bc_resnet_tiny_v2_fp32_model_data.h).
- Extended
  [river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc)
  to support variant-specific frontend/runtime contracts:
  - the existing baseline, current FP32, and round6 branches keep the legacy
    `40x98`, `n_fft=512`, relative-dB normalization path
  - the new student debug branch uses the exported `40x101`, `n_fft=400`,
    centered STFT, natural-log, per-clip mean/std frontend
  - registered `MUL` and added a student-only TFLM `RfftFloat` path while
    keeping the existing WebRTC FFT path for the current chain
  - generalized profile logging so board/local parity output now reports the
    actual selected frontend contract instead of hardcoded `fft=512` /
    `frames=98`
- Kept the committed deployment default on
  `CONFIG_RIVER_KWS_MODEL_VARIANT_FP32_EXPERIMENTAL=y` and added an explicit
  `prj.conf` line to keep
  `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_TINY_V2_FP32_DEBUG`
  disabled in the default image.
- This step does not alter serial-debug commands and does not remove any
  existing board/local comparison tooling; it only adds a selectable parallel
  debug variant.

## Step 5.96
- Ran a board-side exact tensor parity session over the existing serial debug
  flow, without changing the serial monitor settings or removing any parity
  hooks.
- The boot log from the flashed firmware proved the board is currently running
  the committed mainline FP32 branch, not the parallel student branch:
  - variant=`bc_resnet_v3_fp32_experimental`
  - input shape=`1x40x98x1`
  - frontend=`fft=512`, `frames=98`, `center=no`
  - model size=`77848B`
- Executed the existing parity path on board:
  - `river kws debug local on`
  - `river audio probe stop`
  - `river kws dump next`
  - `river kws align run`
- The board emitted a complete exact tensor dump for alignment replay
  `seq=2 infer=3` with:
  - `feat_hash=0xb89e7474`
  - `input_hash=0x97354d89`
  - `raw=25`
  - `score=0.025023`
- Replayed that same dumped tensor locally against
  `/root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_v3_fp32.tflite`
  and verified:
  - feature hash matches
  - input hash matches
  - quant parity is exact (`diff_bytes=0/15680`)
  - output `raw` matches exactly (`25`)
  - float output bytes differ by only the first byte, with host score
    `0.025000` vs board score `0.025023`
- Restored the board to the normal debug state after the run:
  - `river kws debug local off`
  - `river audio probe start`
- Conclusion of this step:
  - the existing board/local exact-tensor parity path is working
  - the currently flashed firmware is not the student FP32 debug variant, so
    this parity result validates the mainline FP32 chain only
  - student-model parity on board requires reflashing the student build first

## Step 5.97
- Switched the committed KWS model selection in [prj.conf](/root/ameba-river/prj.conf)
  from `CONFIG_RIVER_KWS_MODEL_VARIANT_FP32_EXPERIMENTAL=y` to
  `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_TINY_V2_FP32_DEBUG=y`
  so the next board run can debug the new student FP32 deployment directly.
- Kept the existing board/local parity tooling, tensor dump path, and serial
  debug command flow unchanged; this step only changes which already-integrated
  model variant is compiled into the image.
- Rebuilt the full `RTL8730E` image set successfully with the student FP32
  debug variant selected.
- Verified the produced app image embeds
  `student_bc_resnet_tiny_v2_fp32_debug` and the new student-frontend logging
  string `log=natural norm=per_clip_mean_std`, confirming this is not just a
  stale rebuild of the previous `bc_resnet_v3_fp32_experimental` image.
- Produced new firmware artifacts:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `4019552`
  - `build_RTL8730E/ota_all.bin` = `4019584`
- This step stops at compile validation only. Flashing and board-side exact
  parity on the student branch should be handled next as a separate runtime
  verification step.

## Step 5.98
- Extended the project-owned SDK layout helper
  [tools/sdk/apply_rtl8730e_memory_layout_patch.py](/root/ameba-river/tools/sdk/apply_rtl8730e_memory_layout_patch.py)
  with a new `aivoice_ca32_17mb` variant for large-model debug bring-up.
- This variant expands the external SDK runtime layout to:
  - `PSRAM_END = 0x61500000`
  - `CA32_BL3_DRAM_NS = 0x60300000 ~ 0x61400000` (`17MB`)
  - `KM4_DRAM_HEAP_EXT = 0x61400000 ~ 0x61500000` (`1MB`)
- Verified the rebuilt CA32 image now exposes
  `__psram_heap_buffer_size__ = 0x00d78000` (about `13.47 MiB`), and the
  board runtime correspondingly showed early `heap_free` around `13.5 MiB`.
- That larger layout alone did not fix the student FP32 model:
  - the board still failed `AllocateTensors`
  - the failure remained `Requested: 4700160, available 695604`
  - this proved the direct blocker was the KWS tensor arena cap, not the total
    CA32 heap size or the existing serial/parity tooling
- Raised the project Kconfig limit for `RIVER_KWS_TENSOR_ARENA_KB` from
  `2048` to `16384` in [Kconfig](/root/ameba-river/Kconfig) so large-model
  debug builds are configurable from the project side.
- Increased the current student FP32 debug deployment arena in
  [prj.conf](/root/ameba-river/prj.conf) from `688KB` to `8192KB`.
- Rebuilt and reflashed the student FP32 debug firmware with the larger arena
  while preserving the existing board/local comparison path and serial command
  flow.
- Board verification after reflashing showed the student FP32 chain now
  initializes and runs:
  - `kws align guard: kws=ready`
  - `kws perf: mem[arena=4709152/8192KB slack=3679456 ...]`
  - `river kws align run` completed and emitted tensor dump chunks instead of
    failing in `AllocateTensors`
  - first replayed inference produced `raw=371`, `score=0.371203`,
    `q15=12163`, and a held local-debug wakeword hit
- This step keeps the previously implemented board/local tensor dump and
  parity hooks intact, so later model debugging can continue to isolate model
  quality from deployment/adaptation mistakes.

## Step 5.99
- Hardened the host-side tensor replay tool
  [tools/kws/replay_board_tensor_dump.py](/root/ameba-river/tools/kws/replay_board_tensor_dump.py)
  for the preserved board/local KWS parity workflow, without changing board
  serial commands or dump emission.
- The replay tool now:
  - tolerates malformed dump chunks in the monitor transcript instead of
    aborting the whole parse immediately
  - accepts `float32` debug dumps whose `input_raw` stream is truncated by
    serial wrapping, as long as the recorded `input_hash` proves the board fed
    the same raw bytes as `feat_f32`
  - prints the exact `float32` output scalar in addition to the rounded
    `raw/score` presentation so parity conclusions are not confused by display
    precision
- Verified that the currently embedded board model header
  [components/river_voice/generated/student_bc_resnet_tiny_v2_fp32_model_data.h](/root/ameba-river/components/river_voice/generated/student_bc_resnet_tiny_v2_fp32_model_data.h)
  is byte-identical to the algorithm export
  `/root/kws-trainint/artifacts/exports/student_bc_resnet_tiny_v2/model.fp32.tflite`:
  - size `411560`
  - sha256 `2f21649bfbbbf69aae7f0fd1dc4ef318702cfd44c36fc1221c06ddcc215a4c51`
- Replayed the preserved board dump from
  `/tmp/kws_student_fp32_debug_replay.clean.log` against that exact host model
  and confirmed deployment parity for the student FP32 debug branch:
  - board `feature_hash=0x7ce0b11d`
  - board/effective input hash `0xd52f011c`
  - board exact output `0.371203`
  - host exact output `0.371203`
  - `output_parity: bytes_equal=yes raw_equal=yes`
- Conclusion of this step:
  - the new `student_bc_resnet_tiny_v2 FP32` board deployment is correct for
    the captured sample
- current board/local mismatch risk is no longer in model embedding or TFLM
  input adaptation for this path
- the remaining issues to investigate, if any, are model behavior /
  thresholding / runtime interaction rather than this deployment chain

## Step 5.100
- Added a new onboarding and operating guide for board/local KWS deployment
  parity work:
  [doc/KWS_BOARD_HOST_PARITY_DEBUG_GUIDE_ZH.md](/root/ameba-river/doc/KWS_BOARD_HOST_PARITY_DEBUG_GUIDE_ZH.md)
- The new guide consolidates the currently scattered knowledge into one place:
  - what the board/local parity mechanism proves
  - which board commands belong to the preserved debug flow
  - how `align run`, `dump next`, tensor dump, and host replay fit together
  - how to confirm the board-embedded model and host `.tflite` are identical
  - how to interpret `feat_f32`, `input_raw`, `output_raw`, `score`, `exact`,
    and `bytes_equal`
  - which conclusions are allowed before and after parity passes
  - common pitfalls that previously caused wasted debugging cycles
- Updated [doc/README.md](/root/ameba-river/doc/README.md) so the new guide is
  discoverable from the documentation index and prioritized alongside the
  existing KWS / frontend references.
- This step intentionally does not change board code, serial flow, or the
  parity mechanisms themselves. It packages the existing proven workflow into a
  reusable handoff document for future model bring-up.

## Step 5.101
- Added a focused realtime-analysis document for the current
  `student_bc_resnet_tiny_v2_fp32_debug` branch:
  [doc/KWS_STUDENT_FP32_REALTIME_ANALYSIS_ZH.md](/root/ameba-river/doc/KWS_STUDENT_FP32_REALTIME_ANALYSIS_ZH.md)
- The new document consolidates the current investigation into one place:
  - the measured board-side symptom: `infer_us` stays around `675 ms`
  - the current runtime budget implied by `stride=16` and `hop=10 ms`
  - why the bottleneck is model `Invoke()` cost rather than serial flow,
    tensor-arena initialization, or threshold tuning
  - the current frontend / model contract used by the student FP32 debug path
  - the structural reasons the graph is unfriendly to `RTL8730E + TFLM FP32`
  - why `101 -> 98` frames is only a secondary optimization lever
  - the recommended next steps: keep FP32 for parity debug, evaluate `INT8`,
    and ask the algorithm side to reduce graph cost structurally
- Updated [doc/README.md](/root/ameba-river/doc/README.md) so this analysis is
  discoverable from the main documentation index and grouped with the existing
  KWS bring-up / parity material.
- This step is documentation-only:
  - no board code was changed
  - no serial debug command flow was modified
  - the preserved board/local parity tooling remains the baseline for future
    model deployment investigation

## Step 5.102
- Added a same-caliber local realtime-estimate and board bring-up recommendation
  document for the `student_bc_resnet_tiny_v2` INT8 bundle:
  [doc/KWS_STUDENT_INT8_REALTIME_ESTIMATE_ZH.md](/root/ameba-river/doc/KWS_STUDENT_INT8_REALTIME_ESTIMATE_ZH.md)
- The new document grounds the INT8 recommendation in concrete local evidence:
  - exact bundle contract and quantization parameters from the algorithm export
  - host-side operator inventory showing the INT8 / FP32 models share the same
    graph shape and differ mainly by tensor dtype and kernel path
  - file-size comparison: `124392B` INT8 vs `411560B` FP32
  - interpreter tensor-byte comparison: `2,800,070B` INT8 vs `11,194,508B`
    FP32
  - host-side single-thread relative runtime checks:
    - optimized path about `3.54x` faster for INT8
    - builtin reference path about `1.69x` faster for INT8
  - bundle-side parity / threshold / board-reference metrics and export-gate
    status
- Based on those inputs, the document records a local board-side estimate and
  recommendation:
  - INT8 is clearly more worth boarding than the current student FP32 debug
    path
  - expected board `infer_us` is likely improved substantially but still not
    yet safe to assume it beats the current `160ms` stride budget
  - first smoke should preserve the existing FP32 parity tooling, use a
    parallel INT8 debug variant, start with a large arena, and gate decisions
    on `infer_us`, queue growth, heap headroom, and int8-kernel stability
- Updated [doc/README.md](/root/ameba-river/doc/README.md) so the INT8
  estimate is discoverable alongside the existing FP32 realtime and parity
  debugging documents.
- This step is documentation-only:
  - no firmware code was changed
  - no serial debug command flow was modified
  - the existing board/local deployment-parity mechanism remains the required
    baseline before any INT8 board-effect conclusions are accepted

## Step 5.103
- Added a parallel board-debug KWS variant for the algorithm export
  `student_bc_resnet_tiny_v2` INT8 bundle without replacing the preserved FP32
  debug path:
  - [Kconfig](/root/ameba-river/Kconfig) now exposes
    `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_TINY_V2_INT8_DEBUG`
  - [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc)
    now wires that variant to the exported INT8 TFLite blob while reusing the
    same `40x101`, centered log-mel, per-clip norm frontend contract as the
    student FP32 debug path
  - [prj.conf](/root/ameba-river/prj.conf) switches the active deployment
    build to the INT8 debug variant, starts from a `2048KB` arena, and uses
    the bundle threshold `q15=9444`
- Added the embedded model header generated from the algorithm bundle:
  [components/river_voice/generated/student_bc_resnet_tiny_v2_int8_model_data.h](/root/ameba-river/components/river_voice/generated/student_bc_resnet_tiny_v2_int8_model_data.h)
- Built, flashed, and exercised the INT8 firmware on the board while keeping
  the existing board/local parity workflow intact:
  - boot log confirms
    `variant=student_bc_resnet_tiny_v2_int8_debug`
  - runtime tensor contract confirms `input=int8`, `output=int8`,
    `shape=1x40x101x1`
  - the preserved `river kws debug local on` + `river audio probe stop` +
    `river kws align run` flow still emits `feat_f32`, `input_raw`, and
    `output_raw` chunks for host replay
- Board/local parity passed for the INT8 path:
  - host replay from
    [tools/kws/replay_board_tensor_dump.py](/root/ameba-river/tools/kws/replay_board_tensor_dump.py)
    reported `quant_parity: diff_bytes=0/4040`
  - replay also reported `output_parity: bytes_equal=yes raw_equal=yes`
  - board and host both produced `raw=-28`, `score=0.390625`, `q15=12800` on
    the captured alignment sample
- The deployment path is therefore correct, but realtime is not:
  - board `infer_us` on this INT8 variant is about `2.34s`
  - queue trimming still occurs aggressively before inference
  - this means the current student INT8 graph is board-correct but far from
    usable realtime on `RTL8730E`
- Restored the board to normal runtime after the test by turning `local_only`
  back off and restarting `audio probe`, so the board is not left in the
  parity-only state.

## Step 5.104
- Added a focused post-measurement analysis document for the current student
  INT8 board result:
  [doc/KWS_STUDENT_INT8_BOARD_REALTIME_ANALYSIS_ZH.md](/root/ameba-river/doc/KWS_STUDENT_INT8_BOARD_REALTIME_ANALYSIS_ZH.md)
- This document records the actual root cause behind the poor board-side INT8
  realtime result, instead of leaving the older estimate as the latest story:
  - deployment parity is already correct, so the issue is no longer in
    quantization wiring or board adaptation
  - current board `infer_us` is about `2.34s`, much worse than the earlier
    student FP32 `~675ms`
  - the key runtime reason is in the SDK CA32 kernels:
    - the INT8 `conv` path is explicitly forced to
      `reference_integer_ops::ConvPerChannel(...)`
    - the INT8 `depthwise` path is explicitly forced to
      `reference_integer_ops::DepthwiseConvPerChannel(...)`
    - student-heavy `MUL`, `LOGISTIC`, and `ADD` are also still on reference
      style paths
  - meanwhile the CA32 FP32 `conv` path still keeps an optimized
    `Im2col + cpu_backend_gemm::Gemm` implementation
  - so the current result is not “INT8 quantization is useless”, but “this
    board is currently running a heavy student graph on reference INT8
    kernels”
- Updated [doc/README.md](/root/ameba-river/doc/README.md) so the new INT8
  board-realtime postmortem is discoverable next to the existing FP32
  realtime and INT8 estimate documents.
- This step is documentation-only:
  - no firmware code was changed
  - no serial debug mechanism was changed
  - it preserves the existing parity workflow as the baseline deployment proof

## Step 5.105
- Switched the active deployment build back from the temporary student INT8
  debug variant to the preserved student FP32 debug variant in
  [prj.conf](/root/ameba-river/prj.conf):
  - `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_TINY_V2_FP32_DEBUG=y`
  - `# CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_TINY_V2_INT8_DEBUG is not set`
  - kept the large `8192KB` arena and the last known usable FP32 measurement
    settings (`stride=16`, `queue=64`, `pre_roll_flush=16`, `threshold_q15=384`)
- Rebuilt and reflashed the board with the FP32 metrics build, then captured a
  fresh boot/runtime log at `/tmp/kws_student_fp32_debug.log`.
- Measured the current board-side student FP32 resource profile while keeping
  the existing serial and parity workflow intact:
  - boot log confirms
    `variant=student_bc_resnet_tiny_v2_fp32_debug`
  - boot log confirms the student contract
    `input=float32`, `output=float32`, `shape=1x40x101x1`
  - KWS init uses `arena_used=4709152B`, `arena_slack=3679456B`
  - `align` replay measured `infer_us=675892`
  - restored live runtime measured `infer_us=675354`
  - cumulative board average after two inferences is `675623us`
  - live queue peak reached `43/64`
- Replayed the captured board tensor dump on host against the exact FP32
  bundle:
  - `quant_parity: diff_bytes=0/16160`
  - `output_parity: bytes_equal=yes raw_equal=yes`
  - board and host both produced `raw=371`, `score=0.371203` on the captured
    alignment sample
- Added a dedicated dated performance record:
  [doc/RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_FP32_DEBUG_ZH.md](/root/ameba-river/doc/RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_FP32_DEBUG_ZH.md)
  which records:
  - the exact measurement commands
  - bundle checklist vs board actuals
  - KWS init memory plan and heap snapshots
  - `align` and live inference latency
  - the reminder that this build keeps a deliberately low debug threshold and
    should not be used for quality conclusions
- Updated [doc/README.md](/root/ameba-river/doc/README.md) so the new FP32
  board-profile document is indexed alongside the existing parity and
  realtime-analysis material.

## Step 5.106
- Added a parallel `student_bc_resnet_nano_v2_fp32_debug` board-debug variant
  without disturbing the preserved student/tiny parity workflow:
  - [Kconfig](/root/ameba-river/Kconfig) now exposes
    `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_NANO_V2_FP32_DEBUG`
  - [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc)
    now maps that variant to the exported nano FP32 bundle and keeps the same
    `40x101`, `fft=400`, centered log-mel, per-clip normalization contract
  - this keeps the existing board/local parity tooling applicable to the nano
    branch instead of introducing a parallel debug mechanism
- Imported the generated nano FP32 model header into the repository so the
  firmware build stays reproducible:
  [components/river_voice/generated/student_bc_resnet_nano_v2_fp32_model_data.h](/root/ameba-river/components/river_voice/generated/student_bc_resnet_nano_v2_fp32_model_data.h)
- Switched the active deployment-test build in
  [prj.conf](/root/ameba-river/prj.conf) to the nano FP32 debug branch:
  - `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_NANO_V2_FP32_DEBUG=y`
  - `CONFIG_RIVER_KWS_TENSOR_ARENA_KB=4096`
  - `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=8851`
  - preserved the already-validated parity-friendly scheduling settings
    (`stride=16`, `pre_roll_flush=16`, `queue=64`)
- Rebuilt the full `RTL8730E` firmware successfully after the nano-variant
  integration:
  - `river_voice_kws.o` compiled with the new symbol path
  - the full image build completed with `Build done`
- This step is integration-only:
  - no board flash yet
  - no serial-debug flow changes
  - parity confirmation and board performance measurement are handled next

## Step 5.107
- Completed the first full board deployment validation for
  `student_bc_resnet_nano_v2_fp32_debug` while preserving the existing
  board/local comparison workflow:
  - captured a fresh nano FP32 boot/runtime log at `/tmp/kws_nano_fp32_debug.log`
  - boot facts confirm the intended deployment contract:
    - `variant=student_bc_resnet_nano_v2_fp32_debug`
    - `runtime_in=float32 runtime_out=float32`
    - `shape=[1,40,101,1]`
    - `fft=400 hop=160 center=yes`
    - `threshold_q15=8851`
  - init memory facts from the board log show:
    - `arena_used=3139392B`
    - `arena_slack=1054912B`
    - `boot_ready heap_free=9641216B`
- Verified that the board-embedded model is byte-identical to the algorithm
  bundle FP32 `.tflite`:
  - header bytes = model bytes = `108828`
  - both hashes are
    `4ff052777e4796db44d5899e94c15eb62c3d24431edcc065d9388671c4df3945`
- Ran the preserved standard parity path on the board without changing the
  serial workflow:
  - `river kws debug local on`
  - `river audio probe stop`
  - `river kws align run`
  - `river kws debug local off`
  - `river audio probe start`
- The nano FP32 alignment sample proves the deployment path is working:
  - board `infer_us=277865`
  - board `raw=363 score=0.363446 q15=11909`
  - board produced a wake hit on the compiled sample
  - `feature hash=0x7ce0b11d`
  - `input hash=0xd52f011c`
- Hardened the host replay tool for future FP32 exact-parity work without
  touching the board-side debug path:
  - [tools/kws/replay_board_tensor_dump.py](/root/ameba-river/tools/kws/replay_board_tensor_dump.py)
    now supports `--builtin-ref`
  - this forces the host-side TFLite replay onto the builtin reference
    resolver to avoid false `bytes_equal=no` reports caused by host delegates
  - the script now also prints `host_runtime: builtin_ref=yes|no` and explains
    the retry path when a float32 dump mismatches only under delegate mode
- Replayed the captured nano dump on host against the exact FP32 bundle and
  confirmed exact parity under the new stable host mode:
  - `quant_parity: diff_bytes=0/16160`
  - `output_parity: bytes_equal=yes raw_equal=yes`
  - board and host both produced `raw=363`
  - board and host exact output both decode to `0.363446`
- Confirmed the board is restored to the normal runtime path after parity
  testing:
  - subsequent live logs no longer show `wakeword handoff held: reason=local_debug`
  - the board resumes normal `wakeword queued` and
    `xiaozhi conversation window opened` behavior
  - a later live sample still measured `infer_us=277681`, which is consistent
    with the `align` sample latency level
- Added two documentation updates so future model bring-up can reuse the same
  path reliably:
  - new board profile:
    [doc/RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_NANO_FP32_DEBUG_ZH.md](/root/ameba-river/doc/RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_NANO_FP32_DEBUG_ZH.md)
  - updated parity guide:
    [doc/KWS_BOARD_HOST_PARITY_DEBUG_GUIDE_ZH.md](/root/ameba-river/doc/KWS_BOARD_HOST_PARITY_DEBUG_GUIDE_ZH.md)
    now explicitly recommends `--builtin-ref` for FP32 exact parity
- Updated [doc/README.md](/root/ameba-river/doc/README.md) so the nano FP32
  runtime profile is indexed beside the existing student/tiny and INT8
  records.

## Step 5.108
- Added a focused constraint document that explains, with current board and
  SDK evidence, why `INT8`/`INT16` deployment is not automatically a realtime
  win on the current `RTL8730E` path:
  [doc/KWS_INT8_INT16_DEPLOYMENT_CONSTRAINTS_ZH.md](/root/ameba-river/doc/KWS_INT8_INT16_DEPLOYMENT_CONSTRAINTS_ZH.md)
- This document consolidates three layers of evidence into one place:
  - board facts already measured in this repo:
    - `student_bc_resnet_tiny_v2_int8_debug` parity is correct but
      `infer_us` is about `2.34s`
    - `student_bc_resnet_tiny_v2_fp32_debug` is about `675ms`
    - `student_bc_resnet_nano_v2_fp32_debug` is about `278ms`
  - current CA32 SDK kernel reality:
    - `int8 conv` is explicitly forced to
      `reference_integer_ops::ConvPerChannel(...)`
    - `int8 depthwise` is explicitly forced to
      `reference_integer_ops::DepthwiseConvPerChannel(...)`
    - `int8/int16` `MUL`, `LOGISTIC`, `ADD`, and pooling remain reference-style
      or generic paths
  - current project-side deployment-chain reality:
    - KWS app currently accepts only `uint8`, `int8`, and `float32` tensor I/O
    - current host replay tool likewise supports only `uint8`, `int8`, and
      `float32`
    - therefore `INT16` is not just “probably slow”, but “not an accepted
      deployment target on the current app/tooling path”
- The new document translates these facts into explicit downstream rules for
  algorithm training and candidate selection:
  - no `INT16` delivery for the current board path
  - `INT8` remains the only quantized delivery target worth considering
  - future models must stay within the currently integrated resolver subset
  - future candidates must reduce high-resolution compute early and avoid
    heavy `LOGISTIC + MUL` gating
  - board `infer_us`, not offline budget declarations alone, is the real
    acceptance gate
  - exact board/host parity remains mandatory before any threshold or quality
    discussion
- Updated [doc/README.md](/root/ameba-river/doc/README.md) so this new
  constraint document is indexed next to the existing parity, FP32, and INT8
  runtime records.

## Step 5.109
- Added a parallel DS-CNN tiny FP32 debug deployment variant without touching
  the mainline production KWS path:
  - [components/river_voice/generated/student_dscnn_tiny_v2_fp32_model_data.h](/root/ameba-river/components/river_voice/generated/student_dscnn_tiny_v2_fp32_model_data.h)
  - [Kconfig](/root/ameba-river/Kconfig)
  - [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc)
  - [prj.conf](/root/ameba-river/prj.conf)
- The new variant keeps the already-validated student debug contract intact:
  - `40x101`
  - `fft=400`
  - `hop=160`
  - `center=yes`
  - `per_clip_mean_std`
  - `float32 -> float32`
- Preserved the existing board / host parity workflow rather than introducing
  a special-case path for DS-CNN:
  - `river kws debug local on`
  - `river audio probe stop`
  - `river kws align run`
  - host replay through
    [tools/kws/replay_board_tensor_dump.py](/root/ameba-river/tools/kws/replay_board_tensor_dump.py)
- First boot with `1024KB` arena failed at `AllocateTensors`; the DS-CNN tiny
  FP32 path now uses a debug-only `2048KB` arena so bring-up can proceed to
  parity and runtime measurement.
- Rebuilt, flashed, and verified the new DS-CNN tiny FP32 variant boots
  successfully:
  - `variant=student_dscnn_tiny_v2_fp32_debug`
  - `input shape=[1,40,101,1]`
  - `runtime_in=float32 runtime_out=float32`
  - `arena_used=1168336B`
  - `arena_slack=928816B`
- Verified the embedded board model bytes match the algorithm bundle exactly:
  - header bytes `12376`
  - model bytes `12376`
  - shared `sha256=836b18e8c315c9142f5744bfa0183a35e4d011019c2526e33f86dd3da166c7c7`
- Ran the preserved `align` parity flow on board and replayed the resulting
  dump on host against the exact FP32 bundle:
  - board `raw=297 score=0.296720 q15=9723`
  - host `raw=297 exact=0.296720`
  - `feature hash=0xf6cf59f0`
  - `effective input hash=0x8aa04513`
  - `output_parity: bytes_equal=no raw_equal=yes`
- The DS-CNN tiny dump lost `input_raw` chunks `146` and `147` in serial
  capture, but the replay tool correctly fell back to `feat_f32` because the
  reconstructed feature bytes reproduced the board input hash exactly. This is
  treated as a serial dump completeness issue, not a deployment mismatch.
- Captured the first board-side runtime profile for this candidate:
  - `infer_us[last=183988 avg=183969 max=183988]`
  - `stride=16`, so the current debug cadence still has margin to the next
    `256ms` infer slot
  - still about `10.22x` slower than the bundle `18ms` CPU budget
- Added the first DS-CNN tiny FP32 board profile document:
  - [doc/RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_DSCNN_TINY_FP32_DEBUG_ZH.md](/root/ameba-river/doc/RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_DSCNN_TINY_FP32_DEBUG_ZH.md)
- Updated [doc/README.md](/root/ameba-river/doc/README.md) so the new DS-CNN
  tiny runtime profile is indexed beside the existing student bring-up and
  constraint records.

## Step 5.110
- Added a parallel DS-CNN small FP32 debug deployment variant so the next
  DS-CNN candidate can be brought up without touching the mainline KWS path:
  - [components/river_voice/generated/student_dscnn_small_v2_fp32_model_data.h](/root/ameba-river/components/river_voice/generated/student_dscnn_small_v2_fp32_model_data.h)
  - [Kconfig](/root/ameba-river/Kconfig)
  - [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc)
  - [prj.conf](/root/ameba-river/prj.conf)
- Kept the existing student debug frontend / parity contract unchanged:
  - `40x101`
  - `fft=400`
  - `hop=160`
  - `center=yes`
  - `per_clip_mean_std`
  - `float32 -> float32`
- Rebuilt, flashed, and verified the new DS-CNN small FP32 variant boots
  successfully:
  - `variant=student_dscnn_small_v2_fp32_debug`
  - `input shape=[1,40,101,1]`
  - `runtime_in=float32 runtime_out=float32`
  - `arena_used=1945648B`
  - `arena_slack=151504B`
- Verified the embedded board model bytes match the algorithm bundle exactly:
  - header bytes `23600`
  - model bytes `23600`
  - shared `sha256=e0a2bedd32801d3d05ca4b0b3369137bedba990d28e02b5253696dc021e56925`
- Reused the preserved board / host parity path instead of introducing a new
  special-case debug flow:
  - `river kws debug local on`
  - `river audio probe stop`
  - `river kws align run`
  - host replay through
    [tools/kws/replay_board_tensor_dump.py](/root/ameba-river/tools/kws/replay_board_tensor_dump.py)
- The board-side `align` sample triggered correctly on this model:
  - board `raw=333 score=0.333065 q15=10914`
  - threshold `q15=10534`
  - wake hit recorded on board
- Host replay against the exact FP32 bundle confirms deployment correctness:
  - `feature hash=0xf6cf59f0`
  - `effective input hash=0x8aa04513`
  - board `raw=333 exact=0.333065`
  - host `raw=333 exact=0.333065`
  - `output_parity: bytes_equal=no raw_equal=yes`
- One full-capture quirk was observed:
  - the serial log did not emit an explicit `kws tensor dump end:` line
  - but `input_raw` still reached `253/253`, `output_raw` was present, and host
    replay succeeded
  - this is treated as a serial logging quirk, not a deployment mismatch
- Captured the board-side runtime profile for this candidate:
  - `infer_us[last=389384 avg=389256 max=389454]`
  - about `11.12x` slower than the bundle `35ms` CPU budget
  - about `2.12x` slower than the already-tested `student_dscnn_tiny_v2_fp32_debug`
  - `arena_used=1945648B`, about `4.95x` above the bundle `384KB` memory budget
- Added the DS-CNN small FP32 board profile and model-selection recommendation:
  - [doc/RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_DSCNN_SMALL_FP32_DEBUG_ZH.md](/root/ameba-river/doc/RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_DSCNN_SMALL_FP32_DEBUG_ZH.md)
- Updated [doc/README.md](/root/ameba-river/doc/README.md) so the new DS-CNN
  small runtime profile is indexed next to the existing student KWS records.
- Re-checked whether "latest ADK" actually makes `INT8 / INT16` usable, without
  disturbing the current in-use dirty SDK tree:
  - current in-use SDK `/root/ameba-rtos-1.2` at
    `8624cbeccf840c929db1624e05cc5b681024a3bf`
  - clean latest `release/v1.2` clone `/tmp/ameba-rtos-1.2-latest` at
    `8ef72a545c384ec439eef9a200baf4f569e21a73`
  - both still point `component/tflite_micro` at
    `dbda29aa7240ad14cf21cf3636ff2792a05ddcc1`
- Also checked the locally available upstream refs already present in the SDK:
  - SDK `origin/master` is `2def66020a2bd6b37894e8dc8341c49130ca8405`
  - `tflite_micro origin/main` is `8b38d3dac9ea733e93ad73c2b637ef1a28753fb3`
  - but the key quantization files still show no diff versus `dbda29a`:
    - `conv.cc`
    - `depthwise_conv.cc`
    - `reduce_common.cc`
    - `im2col_utils.h`
- Captured the practical implication in a new document:
  - [doc/KWS_LATEST_ADK_QUANTIZATION_RECHECK_ZH.md](/root/ameba-river/doc/KWS_LATEST_ADK_QUANTIZATION_RECHECK_ZH.md)
  - the document separates three states that must not be conflated:
    - clean official `release/v1.2`
    - locally fetched official `origin/master / origin/main`
    - current dirty SDK with local correctness patches
- The re-check makes the current engineering status explicit:
  - current official refs visible on this machine do not prove that the INT8
    optimized CA32 path is fixed
  - current proven INT8 board correctness still depends on the existing local
    SDK patches that preserve board/host parity
  - current `ameba-river` KWS integration still rejects `INT16` at app/tooling
    level, so `INT16` is not a deployable target yet even if some lower-layer
    kernels exist
- Updated [doc/README.md](/root/ameba-river/doc/README.md) so this latest ADK
  quantization re-check is indexed next to the existing INT8 / INT16
  constraint and runtime-analysis documents.
- Added project-owned SDK path override support so `ameba-river` can build and
  flash against a clean SDK tree without touching `/root/ameba-rtos-1.2`:
  - [env.sh](/root/ameba-river/env.sh)
  - [tools/river_flash.py](/root/ameba-river/tools/river_flash.py)
  - [tools/generate_rdev.py](/root/ameba-river/tools/generate_rdev.py)
  - [components/river_cloud/CMakeLists.txt](/root/ameba-river/components/river_cloud/CMakeLists.txt)
- Switched the active deployment retry to the clean-SDK INT8 student bundle in
  [prj.conf](/root/ameba-river/prj.conf):
  - `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_TINY_V2_INT8_DEBUG=y`
  - `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=9444`
  - kept the preserved debug arena / stride / pre-roll / queue settings so the
    retry did not disturb the existing board/host parity mechanism
- Verified the clean SDK build path works end to end:
  - SDK root `/tmp/ameba-rtos-1.2-latest`
  - SDK commit `8ef72a545c384ec439eef9a200baf4f569e21a73`
  - project build completed with `Build done`
- Reflashed the board with the clean-SDK INT8 image using the existing serial
  workflow:
  - board first had to be switched into download mode via the preserved
    `reboot uartburn` monitor command
  - `tools/river_flash.py` then completed with `Finished PASS`
- Captured the critical clean-SDK board symptom and recorded it in
  [doc/KWS_LATEST_ADK_QUANTIZATION_RECHECK_ZH.md](/root/ameba-river/doc/KWS_LATEST_ADK_QUANTIZATION_RECHECK_ZH.md):
  - after successful flash, the board did not enter normal `ameba-river`
    runtime / monitor text logging
  - raw serial only showed command echo or continuous `0x00` bytes
  - clean SDK `monitor.py --debug` sent `AT+LIST` but received no monitor list,
    only repeated `00 / 00 00 / 00 00 00 / 00 00 00 00`
- The clean-SDK INT8 retry therefore tightened the engineering conclusion:
  - this is not just "INT8 still lacks speedup"
  - on the clean official SDK path, this student INT8 image does not yet reach
    the minimum bar of booting into a normal runtime state that can be used for
    board/host parity
- Ran a clean-SDK FP32 control experiment to separate "quantization issue" from
  "broader clean-SDK runtime issue":
  - switched [prj.conf](/root/ameba-river/prj.conf) back to
    `student_bc_resnet_tiny_v2_fp32_debug`
  - restored the last known usable FP32 settings:
    - `CONFIG_RIVER_KWS_TENSOR_ARENA_KB=8192`
    - `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=384`
- Rebuilt and reflashed the clean-SDK FP32 control image successfully:
  - build completed with `Build done`
  - flash completed with `Finished PASS`
- The FP32 control image showed the same post-flash failure signature as the
  clean-SDK INT8 retry:
  - immediate serial degradation into continuous `0x00` bytes
  - no `ameba-river boot` text log
  - no usable runtime / monitor state for board-host parity
- This control result materially changed the conclusion:
  - the current clean-SDK failure is not isolated to INT8 kernels
  - the repo still depends on broader dirty-SDK runtime compatibility changes
    beyond the quantization correctness patches already documented
- Extended the project-owned memory layout helper so it can target arbitrary SDK
  clones without touching the primary dirty SDK:
  - [tools/sdk/apply_rtl8730e_memory_layout_patch.py](/root/ameba-river/tools/sdk/apply_rtl8730e_memory_layout_patch.py)
    now accepts `--sdk-root`
  - this allows the clean SDK clone under `/tmp/ameba-rtos-1.2-latest` to be
    checked/patched in a controlled way
- Verified the memory-layout delta explicitly before retrying the clean SDK:
  - dirty SDK `/root/ameba-rtos-1.2` reports
    `--variant aivoice_ca32_17mb --check` as `applied`
  - clean SDK `/tmp/ameba-rtos-1.2-latest` initially reports the same check as
    `not-applied`
  - applying the patch to the clean clone reports:
    `sdk_root=/tmp/ameba-rtos-1.2-latest variant=aivoice_ca32_17mb layout=changed hal=changed`
- Rebuilt and reflashed the clean-SDK FP32 control image after applying the
  memory layout patch to the clean SDK clone:
  - build still completed with `Build done`
  - flash still completed with `Finished PASS`
- Captured the post-flash serial state for 20s and confirmed the failure
  signature did not improve:
  - the capture file contains the normal `script` header followed by continuous
    `0x00` bytes
  - there is still no `ameba-river boot` text log or usable monitor/runtime
    output
- This narrows the root-cause boundary again:
  - the clean-SDK runtime failure is not explained by the missing 17MB memory
    layout patch alone
  - the remaining suspects are other dirty-SDK runtime deltas, especially the
    TFLM submodule, `component/aivoice`, and possibly additional platform/runtime
    changes outside the layout patch
- Performed a tighter clean-SDK retry by overlaying only the dirty SDK's local
  `tflite_micro` patch set onto the clean SDK clone:
  - patched files in the clean clone:
    - `tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/conv.cc`
    - `tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/depthwise_conv.cc`
    - `tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/im2col_utils.h`
    - `tensorflow/lite/micro/kernels/reduce_common.cc`
  - patch magnitude matched the dirty SDK local diff:
    - `4 files changed, 114 insertions(+), 47 deletions(-)`
- Rebuilt and reflashed the clean-SDK FP32 control image with:
  - clean SDK memory layout patch still applied
  - dirty TFLM local patch overlaid
- Board result did not improve:
  - build completed with `Build done`
  - flash completed with `Finished PASS`
  - 20-second raw serial capture still contained only the `script` header
    followed by continuous `0x00`
  - `od -An -tx1 -j 160 -N 64 /tmp/kws_student_fp32_clean_sdk_tflm_patch.log`
    still prints repeated `00`
- This further narrows the causality:
  - dirty SDK's local TFLM patch set is not sufficient by itself to restore a
    normal clean-SDK runtime
  - the higher-priority remaining suspects are now the broader `component/aivoice`
    delta and/or other non-TFLM runtime compatibility changes in the dirty SDK
- Scanned the clean SDK clone's actual submodule heads to avoid chasing
  misleading gitlink metadata:
  - `component/audio` actual HEAD is `e6de3cc` and matches dirty SDK
  - `component/application/speechmind` actual HEAD is `b70cfe9` and matches
    dirty SDK
  - `component/ui` actual HEAD is `f5a5325` and matches dirty SDK
  - only `component/aivoice` actual HEAD was still different:
    - clean clone `739ba4e`
    - dirty SDK `2809414`
- Performed a clean-SDK retry with `component/aivoice` aligned to the dirty SDK
  commit while keeping the earlier 17MB memory layout patch:
  - reverted the temporary clean-clone TFLM overlay
  - switched clean-clone `component/aivoice` to `280941488cb122f608d271d0c52a274e3c33a8ec`
  - rebuilt the same FP32 control image and reflashed the board
- Board result still did not improve:
  - build completed with `Build done`
  - flash completed with `Finished PASS`
  - 20-second raw serial capture still contained only continuous `0x00`
  - `od -An -tx1 -j 160 -N 64 /tmp/kws_student_fp32_clean_sdk_aivoice_commit.log`
    still prints repeated `00`
- Current engineering conclusion is now tighter:
  - the clean-SDK runtime failure is not fixed by any one of the visible
    high-signal deltas already tested individually:
    - 17MB memory layout patch
    - dirty local TFLM patch set
    - dirty `component/aivoice` commit
  - remaining root causes are now more likely to involve deeper/runtime-wide
    clean-vs-dirty differences rather than a single obvious KWS-related patch
- Took a snapshot of the last clean-SDK image set that still reproduced the
  `0x00` failure:
  - `/tmp/clean_sdk_aivoice_align_snapshot`
- Rebuilt the same FP32 control configuration against the dirty SDK baseline
  and captured a second image snapshot:
  - `/tmp/dirty_sdk_fp32_control_snapshot`
- Compared clean-vs-dirty images at the artifact level and found the divergence
  is system-wide, not app-only:
  - `km4_boot_all.bin`
    - size: `51872` vs `51872`
    - hash: different
    - first observed byte difference from `cmp -l`: byte `10119`
  - `km0_image2_all.bin`
    - size: `94208` vs `94208`
    - hash: different
    - first observed byte difference from `cmp -l`: byte `41`
  - `km4_image2_all.bin`
    - size: clean `380064`, dirty `379136`
  - `ap_image_all.bin`
    - size: clean `3558496`, dirty `3538016`
  - `km0_km4_ca32_app.bin`
    - size: clean `4040960`, dirty `4019552`
- This materially shifts the debugging frame:
  - the clean-vs-dirty split is not limited to the KWS app payload
  - even early-chain images differ, so the no-log / `0x00` failure now points
    more strongly at boot-chain / image-generation / platform-runtime divergence
    than at a single model-side patch

## Step 5.111
- Switched the latest-SDK retry target to the user-provided upstream checkout:
  - `/root/ameba-rtos`
  - branch `master`
  - commit `d9800ffc6fe3754d1c275eecdfb3f2e0991dbb49`
- Synced only the SDK-side changes that are still relevant to the preserved
  `student_bc_resnet_tiny_v2_fp32_debug` control build:
  - patched top-level latest-SDK files:
    - `component/network/websocket/wsclient_api.c`
    - `component/soc/amebasmart/fwlib/include/hal_platform.h`
    - `component/soc/amebasmart/project/ameba_layout.ld`
    - `component/soc/usrcfg/amebasmart/ameba_flashcfg.c`
  - overlaid the current local `tflite_micro` patch set into
    `/root/ameba-rtos/component/tflite_micro`:
    - `tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/conv.cc`
    - `tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/depthwise_conv.cc`
    - `tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/im2col_utils.h`
    - `tensorflow/lite/micro/kernels/reduce_common.cc`
- Deliberately did not carry over the old `component/aivoice` demo patch set in
  this step:
  - current `ameba-river` wake path no longer depends on
    `examples/speechmind_demo/platform/ameba_dsp`
  - latest `master` aivoice tree has already diverged structurally from the
    dirty `release/v1.2` patch base
  - leaving it out keeps this retry focused on the current KWS control path
- Identified a latest-SDK environment difference that would otherwise look like
  a build failure:
  - `/root/ameba-rtos/env.sh` defines `ameba.py` as a shell alias
  - non-interactive `bash -lc` does not expand that alias by default
  - the correct scripted entrypoint is therefore
    `python /root/ameba-rtos/ameba.py ...`
- Confirmed the latest SDK now builds the preserved FP32 control image all the
  way through packaging:
  - build ended with `Build done`
  - produced image sizes:
    - `ap_image_all.bin = 3542112`
    - `km4_image2_all.bin = 380448`
    - `km0_image2_all.bin = 94208`
    - `km0_km4_ca32_app.bin = 4024960`
    - `km4_boot_all.bin = 51872`
  - captured image hashes for later board/runtime comparison:
    - `ap_image_all.bin`
      `2f9d35ca635bcad71060403d725a5e706c5ea0455f2e303ca51f5c65543cd508`
    - `km4_image2_all.bin`
      `47a518787efff594b981836fcbb9042b41c242850d6616cfd9d6727e215d95c7`
    - `km0_image2_all.bin`
      `328d80657d333dba15ac0efc72eec8e3c6552631014c0d4a6e204d29cb8395dd`
    - `km0_km4_ca32_app.bin`
      `469ea7db132addff59b0900b2f3bdfc18519415e6c3e1ba9c51d656a0e17b6fd`
    - `km4_boot_all.bin`
      `faf20ef92b919df5b82dfa08dc31ae6c7f20da7cd5d701905a5cb7969b4e1ab6`
- This step proves the latest upstream SDK plus the currently required river
  patches is buildable for the preserved FP32 debug path.
- This step does not yet prove board runtime is normal; flash + serial
  validation is still needed to compare against the previous `0x00` failure
  mode.

## Step 5.112
- Flashed the latest-SDK FP32 control image built in Step `5.111` to the board
  through `/dev/ttyUSB0`:
  - entered UART burn with `reboot uartburn`
  - flashed with `AMEBA_SDK_ROOT=/root/ameba-rtos`
  - flash ended with `Finished PASS`
- Captured a fresh 20-second raw boot UART log immediately after flashing:
  - `/tmp/kws_latest_sdk_fp32_boot.log`
- The runtime result is still abnormal and matches the previously observed
  latest-SDK failure signature:
  - after the `script` header, `od -An -tx1 -j 160 -N 64` shows only `00`
  - removing all `0x00` bytes leaves only the `script` start/end wrapper text
  - no normal boot markers appear:
    - no `File System Init Success`
    - no `ameba-river boot`
    - no `kws init`
- This closes the loop on the current latest-SDK retry:
  - latest upstream SDK plus the currently required river patches can compile
    and flash
  - but it still fails at runtime before any normal boot log becomes visible
- therefore the remaining blocker is still runtime / boot-chain divergence,
  not the ability to generate or download the FP32 image

## Step 5.113
- Rebuilt the same preserved FP32 control firmware against the known dirty-SDK
  baseline for a control comparison:
  - `AMEBA_SDK_ROOT=/root/ameba-rtos-1.2`
  - build completed with `Build done`
- Reflashed that dirty-SDK control image to the same board:
  - flash again ended with `Finished PASS`
- Captured a fresh 20-second raw boot UART log after the dirty-SDK reflash:
  - `/tmp/kws_dirty_sdk_fp32_boot.log`
- Unexpectedly, the dirty-SDK control image now shows the same abnormal UART
  symptom as the latest-SDK image in this board session:
  - `od -An -tx1 -j 160 -N 64` still shows only `00`
  - removing all `0x00` bytes again leaves only the `script` wrapper text
  - no `File System Init Success`, `ameba-river boot`, or `kws init`
- Performed one additional minimal UART probe on top of the dirty-SDK image:
  - sent only `ESC + CRLF`
  - captured `/tmp/kws_serial_probe_after_esc.log`
  - probe result was still continuous `0x00` without any shell/banner text
- This changes the interpretation of the current verification session:
  - Step `5.112` still proves the latest-SDK image reproduces the `0x00` boot
    failure on real hardware
  - but the board can no longer serve as a clean immediate control after the
    dirty-SDK reflash also enters the same `0x00` state
  - therefore the current session now indicates either:
    - board state / reset state / UART state has become abnormal, or
    - the current rebuilt dirty-SDK image no longer restores the previously
      known-good runtime on this board
- Immediate conclusion for this step:
  - the latest-vs-dirty runtime comparison is now blocked by board/session
    state, not by lack of a flashable control build

## Step 5.114
- After the user power-cycled the board, the USB-serial device had detached from
  WSL:
  - `/dev/ttyUSB0` disappeared
  - Windows still showed the PL2303 adapter as shared on `BUSID 4-4`
- Re-attached the USB serial device into WSL with `usbipd.exe attach --wsl` and
  recovered `/dev/ttyUSB0`.
- Probed the board immediately after the power cycle without reflashing:
  - no UART output appeared
  - but, importantly, the board was no longer stuck in continuous `0x00`
- Re-established the dirty-SDK control path on this fresh board state:
  - reflashed the dirty-SDK FP32 control firmware
  - initial 20-second passive capture was silent
  - after a minimal `ESC + CRLF` probe, the board resumed normal runtime logs
  - recovered dirty-SDK runtime evidence is in:
    - `/tmp/kws_dirty_sdk_esc_after_powercycle.log`
  - those logs include live KWS/cloud activity such as:
    - `wakeword hit: text=小欧管家`
    - `xiaozhi ota bootstrap ok`
    - `Connected to websocket server`
- Reflashed the latest-SDK FP32 control firmware on the same recovered board:
  - flash again ended with `Finished PASS`
  - the subsequent 20-second raw UART capture showed normal runtime text rather
    than `0x00`
  - latest-SDK runtime evidence is in:
    - `/tmp/kws_latest_sdk_fp32_boot_after_powercycle.log`
  - captured lines include:
    - `Closing the Connection with websocket server`
    - `river.cloud`
    - `river.interaction`
- A follow-up `ESC + CRLF` probe on the latest-SDK image then returned to
  silence, but still did not reproduce `0x00`.
- This materially changes the current conclusion:
  - the earlier latest-SDK `0x00` result is not a stable reproduction after a
    power-cycle recovery
  - board/session state is a major variable in the prior failure captures
- with the board freshly recovered, the latest-SDK FP32 image is now observed
  running and emitting normal application logs

## Step 5.115
- Continued from the recovered latest-SDK board state and validated the
  preserved board/host parity path end-to-end on the actual `master` SDK build.
- Switched from raw `cat /dev/ttyUSB0` capture to the official Ameba monitor for
  interactive work:
  - `python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000`
  - this reliably returned the `>` prompt and accepted monitor commands, while
    the raw capture path was too unstable for command/response validation
- Verified latest-SDK board command responsiveness through the preserved KWS
  debug interface:
  - `river kws debug local status`
  - `river kws align status`
  - board reported:
    - `local_only=yes`
    - `probe=stopped`
    - compiled align sample `frames=145 duration_ms=2320`
- Re-ran the preserved latest-SDK align dump flow under monitor log mode:
  - monitor log file:
    - `/tmp/kws_latest_monitor_logdir/ttyUSB0_20260409_135610.txt`
  - command sequence:
    - `river kws debug local on`
    - `river audio probe stop`
    - `river kws align run`
- Latest-SDK board-side align result is healthy and deterministic:
  - `seq=2`
  - `infer=7`
  - `raw=371`
  - `score=0.371203`
  - `q15=12163`
  - `feat_hash=0x7ce0b11d`
  - `input_hash=0xd52f011c`
  - full tensor dump completed:
    - `feat_chunks=253`
    - `input_chunks=253`
    - `output_chunks=1`
- Replayed that exact latest-SDK board dump on host against the preserved FP32
  bundle:
  - model:
    - `/root/kws-trainint/artifacts/exports/student_bc_resnet_tiny_v2/model.fp32.tflite`
  - host replay result:
    - `board_meta: input_type=float32 output_type=float32 shape=(1, 40, 101, 1)`
    - `board_hash: feature=0x7ce0b11d input=0xd52f011c`
    - `host_hash: feature=0x7ce0b11d logged_input=0xd52f011c effective_input=0xd52f011c`
    - `quant_parity: diff_bytes=0/16160`
    - `board_output: raw=371 score=0.371203 exact=0.371203 q15=12163`
    - `host_output: raw=371 score=0.371000 exact=0.371203`
    - `output_parity: bytes_equal=yes raw_equal=yes`
- Restored the board to a normal runtime state after the parity check:
  - `river kws debug local off`
  - `river audio probe start`
  - board confirmed:
    - `local_only=no`
    - `wake_handoff=normal`
    - `vad probe started`
- This step establishes the most important current fact:
  - on the recovered board, the latest-SDK FP32 deployment is not only runnable
    but also board/host parity-correct through the preserved debug workflow

## Step 5.116
- Continued on the same latest-SDK FP32 board image after Step `5.115` and
  validated the real online wake-to-cloud runtime path instead of stopping at
  tensor parity only.
- Reattached the official Ameba monitor to the live board:
  - `python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000`
  - confirmed the board stayed responsive and in normal runtime mode
- Observed a real wakeword trigger on the latest-SDK FP32 image through the
  normal runtime path:
  - `wakeword hit: text=小欧管家 score_pm=271 q15=8912 triggers=10`
  - `wakeword queued text=小欧家 confidence=8912`
  - `kws debug status` remained:
    - `local_only=no`
    - `wake_handoff=normal`
- Observed the complete cloud handoff open successfully after wake:
  - `xiaozhi ota bootstrap ok`
  - `xiaozhi connecting`
  - `Connected to websocket server`
  - `server hello: sid=8665a824`
  - `interaction_state: wake_monitoring -> wake_confirmed`
- Observed live ASR and dialogue traffic on that same session:
  - `asr provider=xiaozhi_realtime session started sid=8665a824`
  - partial/final STT logs were emitted
  - LLM response logs were emitted
- Observed the board-side TTS playback path actually start and stop on the
  latest-SDK FP32 image:
  - `tts ... state=sentence_start text=没听清呢，`
  - `playback start: stream=xiaozhi_tts rate=24000Hz frame=20ms ...`
  - `xiaozhi playback start: 24000Hz frame=20ms mono=960B queued=8 mode=no_ref gain=5/2`
  - `ameba_audio_stream_tx_start`
  - `tts ... state=stop`
  - `playback stop: stream=xiaozhi_tts epoch=11`
- Captured a runtime status snapshot during the live session:
  - `interaction_state=asr_streaming`
  - `playback_service=idle ... starts=6 stops=6`
  - `asr provider=xiaozhi_realtime ... wifi=connected`
  - `xiaozhi session=yes hello=yes`
- Current caveat remains visible but is not a functional blocker for this step:
  - repeated `xiaozhi uplink backpressure`
  - `last_err=send_queue_busy`
  - despite that, wake, websocket connect, ASR, LLM, TTS start, and playback
    stop all completed in the same session
- This step adds the runtime conclusion missing from Step `5.115`:
  - on the recovered latest-SDK board, the FP32 student debug variant is not
    only parity-correct, but also alive on the real wakeword -> cloud ASR ->
    TTS playback chain

## Step 5.117
- Investigated the remaining latest-SDK FP32 runtime caveat without changing
  code or UART behavior:
  - repeated `xiaozhi ws backpressure`
  - repeated `xiaozhi uplink backpressure`
  - runtime status ending in `last_err=send_queue_busy`
- Traced the issue through the actual project code path and confirmed the
  problem is in the cloud uplink path after wakeword, not in the KWS deploy
  path:
  - websocket queue gate:
    - `RIVER_XIAOZHI_WS_QUEUE_MAX = 8`
    - `RIVER_XIAOZHI_WS_AUDIO_QUEUE_RESERVE = 2`
    - effective audio backpressure threshold is therefore `ready >= 6`
  - websocket transport is intentionally configured non-blocking:
    - `ws_set_senddata_block_time(0)`
    - `ws_multisend_opts(..., 1)`
  - when busy occurs, the uplink worker:
    - increments `busy_count`
    - applies exponential backoff up to `160 ms`
    - trims stale uplink audio down to `6` frames
- Verified an important interpretation point from the live status snapshot:
  - `q_peak=6` matches the design-side clamp exactly and is not evidence that
    the websocket queue truly ran away beyond the intended soft ceiling
- Confirmed the current implementation also has a startup burst factor on the
  XiaoZhi path:
  - wake/open uses `256 ms` pre-roll
  - bridge input is `16 ms`
  - uplink Opus packetization is `20 ms`
  - opening a session can therefore inject roughly `12` full uplink packets
    into the project-side ring before live streaming settles into steady state
- Wrote the analysis into a dedicated project document so later model bring-up
  can distinguish:
  - KWS parity/deployment problems
  - cloud uplink congestion problems
- New document:
  - `doc/XIAOZHI_UPLINK_BACKPRESSURE_ANALYSIS_LATEST_SDK_FP32_ZH.md`
- The resulting conclusion for the current board baseline is:
  - latest-SDK FP32 is functionally alive
  - `send_queue_busy` is presently a realtime quality/freshness-protection
    issue in the XiaoZhi uplink path, not a blocker proving model deployment is
    wrong

## Step 5.118
- Added a new project-level status snapshot document that consolidates the
  current branch, SDK baselines, implemented runtime chain, verified model
  matrix, preserved parity workflow, active issues, and next-step priorities:
  - `doc/PROJECT_STATUS_SNAPSHOT_2026-04-09_ZH.md`
- This document is intentionally broader than the older
  `doc/PROJECT_STATUS_ZH.md`:
  - it reflects the current `kws` branch instead of the historical `DS-CNN`
    staging state
  - it includes the now-verified latest-SDK runtime and parity conclusions
  - it explicitly records the preserved board/host parity workflow as a
    non-negotiable project asset
  - it summarizes the tested model matrix across:
    - `student_bc_resnet_tiny_v2_fp32_debug`
    - `student_bc_resnet_tiny_v2_int8_debug`
    - `student_bc_resnet_nano_v2_fp32_debug`
    - `student_dscnn_tiny_v2_fp32_debug`
    - `student_dscnn_small_v2_fp32_debug`
  - it captures the current project-level problem map:
    - student FP32 realtime insufficiency
    - INT8 gain not materializing on current runtime
    - XiaoZhi uplink backpressure
    - local `16ms` vs uplink `20ms` cadence mismatch as a structural issue
- The document also makes the current overall engineering state explicit:
  - the project already has a runnable end-to-end chain
  - the main blockers are now runtime quality and deployability, not basic
    bring-up
