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
