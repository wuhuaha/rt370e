# RTL8730E EVB Knowledge

## Source
- Document: `UG0702 EV8730EA2 User Guide`
- File: `.codex/UG0702_EV8730EA2_User_Guide_EVB_v1.0(221906).pdf`
- Scope: evaluation board guidance for `RTL8730EAM` and `RTL8730EAH`
- Current board silk-screen reported by user: `EV730EA2 RO1`
- Working interpretation:
  - this is very likely the same `EV8730EA2` board family documented in the guide
  - `RO1` is treated as board revision `R01`
  - the silk-screen alone still does not prove whether the mounted main chip is `RTL8730EAM` or `RTL8730EAH`

## Board Baseline
- The EVB targets `RTL8730EAM` and `RTL8730EAH`.
- The board exposes rich peripherals relevant to this project:
  - Wi-Fi and Bluetooth
  - USB and USB-to-UART over USB-C
  - SD card
  - MIPI display
  - capacitive touch
  - audio input and output
- The EVB is Raspberry Pi 4 form-factor compatible.

## Memory And Download
- The EVB can work with both `NOR` and `NAND` flash layouts.
- If the main chip is `RTL8730EAM`, the default external flash in the guide is `GD25Q256EWIG` NOR flash.
- Current project bring-up should keep memory type explicit during flashing because `NOR` and `NAND` profiles are not interchangeable.
- The board supports USB download and LOGUART download.
- Current board runtime boot logs now confirm: `BOOT FROM NOR`.

## Serial And Bring-Up
- Default LOGUART format:
  - baudrate `1500000`
  - `8N1`
- USB-C can provide power and USB-to-UART capability.
- LOGUART default pins:
  - `PB23` as LOGUART RX
  - `PB24` as LOGUART TX
- Reset button is `CHIP_EN` (`K1`).

## Audio Hardware
- The EVB provides:
  - `4` AMIC inputs
  - `2` DMIC inputs
  - speaker and earphone output path
  - microphone acquisition through audio connectors
- Microphone devices called out in the guide:
  - AMIC: `SPH1642HT5H-1`
  - DMIC: `3SM222FMT1KA`
- The speaker amplifier is `AD52058`, a stereo class-D amplifier.
- Default audio PA power comes from `5V`.
- For external `12V` PA usage, the guide requires resistor rework:
  - remove `R50`
  - solder `R55`
  - then use `J3.1` and `J4.1`
- Wrong `12V` audio power wiring can damage the PC or USB adapter.

## SDK Voice Baseline
- SDK `aivoice` AFE explicitly supports these microphone geometries:
  - `AFE_1MIC`
  - `AFE_LINEAR_2MIC_30MM`
  - `AFE_LINEAR_2MIC_50MM`
  - `AFE_LINEAR_2MIC_70MM`
  - `AFE_CIRCLE_3MIC_50MM`
- SDK `speechmind` on `AmebaSmart` uses:
  - `AFE_CONFIG_ASR_DEFAULT_2MIC50MM()`
  - optional `SSL` on top of that geometry
- `speechmind` capture routing for the `EA` board is:
  - channel 0 -> `AMIC1`
  - channel 1 -> `AMIC3`
  - channel 2 -> `AMIC5`
- Working project interpretation for `ameba-river`:
  - the safest dual-mic starting point is `AMIC1 + AMIC3`
  - software geometry should start at `linear-2mic-50mm` to stay aligned with future `aivoice` AFE resources
  - `AMIC5` should stay reserved as an auxiliary raw tap until there is a concrete need for 3-channel dump, calibration, or debugging
- Current raw-array debug tuning in `ameba-river`:
  - analog mic boost currently set to `20dB` on `AMIC1 + AMIC3`
  - raw replay does not use beamforming yet
  - instead, it uses a focused two-mic mix plus lightweight AGC only for bring-up and listening tests
- Important limitation:
  - `50mm` is currently a software compatibility baseline, not a physically measured spacing from the user's exact board revision
  - if later SSL / beamforming accuracy matters, the actual microphone spacing and orientation should be measured and revalidated

## RF And Antenna
- The EVB defaults to external IPEX antennas.
- `IPEX1` is Bluetooth.
- `IPEX2` is Wi-Fi.
- To switch to PCB antennas, resistor rework is required:
  - Bluetooth: remove `R36`, solder `R38`
  - Wi-Fi: remove `R37`, solder `R39`

## Debug And SWD
- SWD is available.
- Default SWD pins:
  - `PA13` as `SWD_DAT`
  - `PA14` as `SWD_CLK`
- The guide explicitly mentions SWD and J-Link support.

## GPIO Constraints
- If the board uses `RTL8730EAM`, these GPIOs are not freely usable by default:
  - `PB25`
  - `PB26`
  - `PB27`
  - `PB28`
  - `PB30`
  - `PC0`
- Reusing the restricted pins may require resistor changes and may disable onboard SDIO-related circuits.
- Early project hardware planning should avoid those pins unless there is a clear board-rework plan.

## Power Notes
- General development without heavy display or audio load can use normal USB power.
- If MIPI or audio is enabled, the guide recommends ensuring stronger power delivery, preferably USB-C to USB-C.

## Project Implications
- Keep `PB23` and `PB24` reserved for bring-up and logging unless there is a deliberate remap plan.
- Keep flash type explicit in project notes and flashing commands.
- Preserve the software split between `river_voice` and `river_cloud`:
  - local VAD and wake-word can map cleanly to AMIC or DMIC later
  - online control stays independent from board-level audio wiring
- Avoid selecting restricted `RTL8730EAM` GPIOs for future wake button, amplifier control, or sensor expansion before the board BOM and resistor population are confirmed.
- Treat audio power as a hardware safety boundary; software docs should call out the `5V` default and the `12V` rework requirement.

## Open Questions To Confirm
- The exact board population in use:
  - board silk-screen is now known as `EV730EA2 RO1`
  - main chip still needs confirmation as `RTL8730EAM` or `RTL8730EAH`
- Which audio input path will be used first:
  - AMIC
  - DMIC
  - earphone microphone path
- Whether the project will rely on external IPEX antennas or switch to onboard PCB antennas.

## External Reference Projects

### 1. Realtek `ambd_arduino` `micro_speech`
- Repository:
  - `https://github.com/ambiot/ambd_arduino`
- Why it matters:
  - this is still a Realtek-maintained Ameba-family code path
  - even though it is packaged as `Arduino`, it reflects Realtek's own `TFLite Micro` and audio integration choices
- Highest-value takeaways for `ameba-river`:
  - how Realtek wires `TFLite Micro` interpreter setup into an Ameba project
  - how microphone data is buffered and fed into the inference loop
  - how an Ameba-targeted `TensorArena` is budgeted in practice
- How to use it in this project:
  - treat it as an Ameba-specific bring-up reference, not as drop-in code
  - compare its microphone buffering and inference cadence with:
    - `river_voice_capture`
    - `river_voice_detector_silero`
- Cautions:
  - it is not a direct `RTL8730E RTOS external-project` template
  - board support, SDK wrappers, and class structure will differ from current `ameba-river`
  - use it for sequencing and resource-allocation ideas, not for blind copy/paste

### 2. Google `tflite-micro` `micro_speech`
- Repository:
  - `https://github.com/tensorflow/tflite-micro/tree/main/tensorflow/lite/micro/examples/micro_speech`
- Why it matters:
  - this is the cleanest upstream reference for:
    - `TFLite Micro` interpreter lifecycle
    - audio-frame ingestion
    - rolling feature / window update logic
- Highest-value takeaways for `ameba-river`:
  - keep hardware capture and model runtime clearly separated
  - use stable sliding-window logic instead of ad hoc frame stitching
  - maintain a deterministic infer loop that only consumes fully formed model windows
- Direct relevance to current `Silero VAD` work:
  - the project's `256-sample` feed and `512 + 64` rolling input assembly are conceptually similar to `micro_speech`'s staged feature-window updates
  - this upstream example is a good architecture reference for:
    - future `VAD`
    - later `KWS`
    - self-developed `TFLite` models
- Cautions:
  - `micro_speech` is a tiny KWS example, not a production audio front-end
  - it is useful mainly for pipeline shape and buffering discipline, not for direct acoustic quality decisions

### 3. ARM `ML-embedded-evaluation-kit`
- Repository:
  - `https://github.com/ARM-software/ML-embedded-evaluation-kit`
- Why it matters:
  - it is a strong reference for embedded ML system architecture and optimization on ARM cores
  - it is especially useful for:
    - modular audio front-end design
    - clean separation of preprocessing, feature extraction, model execution, and post-processing
    - performance-oriented deployment thinking
- Expected value for `ameba-river`:
  - medium to high as an architecture and optimization reference
  - lower as a direct code-reuse source for the current `RTL8730E CA32 + Realtek SDK + AIVoice + TFLM` stack
- How to use it in this project:
  - use it to review:
    - KWS / ASR pipeline decomposition
    - performance instrumentation
    - optimized model-runner structure
  - do not assume its implementation can be transplanted directly into the current Realtek build without adaptation
- Cautions:
  - `CMSIS-NN` and ARM-optimized paths are highly valuable, but they are not automatically a drop-in win on the current project stack
  - for `RTL8730E`, first priority remains:
    - getting the real board runtime stable
    - then measuring latency / memory / arena usage
    - only then deciding whether deeper ARM-specific optimization is needed

## External Reference Priority
- `Highest priority`:
  - Realtek `ambd_arduino micro_speech`
  - Google `tflite-micro micro_speech`
- `Conditional priority`:
  - ARM `ML-embedded-evaluation-kit`
- Working rule for future implementation:
  - prefer Realtek examples for Ameba-specific driver and bring-up behavior
  - prefer upstream `tflite-micro` examples for model-runtime architecture
  - use ARM ML kit mainly when optimization or later `KWS/ASR` pipeline refinement becomes the bottleneck
