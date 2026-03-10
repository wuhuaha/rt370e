# RTL8730E EVB Knowledge

## Source
- Document: `UG0702 EV8730EA2 User Guide`
- File: `.codex/UG0702_EV8730EA2_User_Guide_EVB_v1.0(221906).pdf`
- Scope: evaluation board guidance for `RTL8730EAM` and `RTL8730EAH`

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
  - `RTL8730EAM` or `RTL8730EAH`
  - `NOR` or `NAND`
- Which audio input path will be used first:
  - AMIC
  - DMIC
  - earphone microphone path
- Whether the project will rely on external IPEX antennas or switch to onboard PCB antennas.
