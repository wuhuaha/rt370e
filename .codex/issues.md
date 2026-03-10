# Issues And Risks

## Current
- The online control path is still a stub. No real backend transport is connected in Step 1.
- `speechmind` has not been integrated yet. This step only prepares the architecture needed to absorb it safely.
- The SDK project generator cannot directly scaffold `component/application/speechmind` as an external example template, so reuse will need selective code import instead of one-shot cloning.
- The exact board population is still not recorded in-project:
  - board silk-screen is known: `EV730EA2 RO1`
  - main chip still unknown: `RTL8730EAM` vs `RTL8730EAH`
  - `NOR` vs `NAND`
- Audio hardware choices are not fixed yet:
  - AMIC vs DMIC as the first local front-end path
  - whether external `12V` PA mode is needed
- Some EVB GPIOs may be unavailable on `RTL8730EAM` without resistor changes.
- Step 2 audio echo currently assumes the EVB microphone routing used by `speechmind` on `AmebaSmart`:
  - channel 0 -> `AMIC1`
  - channel 1 -> `AMIC3`
- The current echo path has no AEC, AGC, or VAD in the loop. If speaker volume is high or the speaker is too close to the microphones, audible feedback is expected.
- Playback is currently pinned to `DEVICE_OUT_SPEAKER`. If the actual board route is earphone-only or uses a different amplifier path, the device selection may need adjustment.
- The new serial diagnostics show PCM activity and read/write health, but they do not prove the analog speaker path is electrically correct.
- On the user's current board setup, the boot log is visible but the custom `river` monitor command is reported as unknown at runtime.

## Mitigation
- Keep all online provider logic behind `river_cloud_adapter_*`.
- Keep all local speech entry points behind `river_voice_frontend_*`.
- Use monitor commands first so board bring-up is independent from microphone pipeline risk.
- Reuse `speechmind` incrementally once Step 1 board validation is stable.
- Use project-side `build_info.h` bootstrap headers so `RTL8730E` parallel builds do not depend on SDK generation order.
- Keep board constraints in `.codex/knowledge.md` and avoid restricted pins until the exact board variant is confirmed.
- Keep flash type explicit in build and flash instructions.
- Default software assumptions to `5V` audio power until a deliberate `12V` hardware rework plan exists.
- Keep the audio echo path behind explicit monitor commands so mic/speaker validation can be started and stopped without rebooting.
- If Step 2 playback is wrong or silent, validate the hardware route before changing higher-level voice architecture:
  - speaker vs earphone output
  - `AMIC1/AMIC3` vs another mic pair
  - board amplifier mute or power state
- Use `river audio diag on` before changing routes:
  - `cap_peak` near zero while speaking usually means the selected microphone path is wrong or inactive
  - nonzero `cap_peak` plus nonzero `play_peak` usually means digital capture and delayed transfer are working, so remaining suspicion shifts to output routing, mute, amplifier, or board wiring
- Use SDK audio tools for single-side isolation when needed:
  - `aplay` to validate speaker playback without microphone capture
  - `arecord` to validate microphone routing or SDK-native record-then-play flow
- During the current bring-up phase, avoid depending on monitor commands for echo control:
  - autostart echo at boot
  - keep diagnostics enabled by default
  - revisit monitor command registration after the audio route is proven
