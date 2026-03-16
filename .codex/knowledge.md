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

## RGB Indicator
- The current board-side RGB status feature is not yet enabled on real hardware.
- Official EVB documentation indicates the board `USER LED` is a passive RGB LED circuit, not a `WS2812` serial LED.
- Important hardware note from the official guide:
  - if users want to use the `USER LED` circuit on the EVB, `R25`, `R27`, and `R31` should be populated with at least `470 ohm`
  - the related control pins should drive `LEDR`, `LEDG`, and `LEDB` low to light the LED
- Current project implication:
  - the earlier `LEDC + PA_9 + WS2812` assumption was wrong for this EVB family
  - runtime RGB indication is now intentionally deferred until the actual `LEDR/LEDG/LEDB` GPIO mapping is confirmed for the user's board population
- Current software status:
  - the project keeps the `river_board_rgb` interface so later wake / VAD / ASR states can still drive a visual indicator
  - current boot log now reports this path as deferred instead of falsely claiming it is ready

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
- SDK also exposes a standalone VAD flow:
  - interface: `aivoice_iface_vad_v1`
  - callback event: `AIVOICE_EVOUT_VAD`
  - message payload: `struct aivoice_evout_vad`
  - output semantics are edge-based only:
    - `status=1` means silence -> speech
    - `status=0` means speech -> silence
  - this is suitable as a diagnostic reference alongside `Silero`, but it does not provide a per-frame probability output
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

## Online ASR Notes

### iFlytek RTASR LLM Protocol
- The currently integrated online ASR endpoint is:
  - host: `office-api-ast-dx.iflyaisol.com`
  - path: `/ast/communicate/v1`
- Current query parameters in use:
  - `accessKeyId`
  - `appId`
  - `audio_encode=pcm_s16le`
  - `lang=autodialect`
  - `samplerate=16000`
  - `utc`
  - `uuid`
  - `signature`
- Signature convention currently used by the project:
  - URL-encode each key/value
  - sort by key
  - build `k=v&...`
  - HMAC-SHA1 with `APISecret`
  - Base64 result
  - URL-encode and append as `signature`

### iFlytek Response Parsing Caveats
- Do not assume every server text frame contains `code` and `desc`.
- In the current RTASR LLM flow, successful recognition frames may arrive without a numeric `code` field.
- `started` / `result` style control and result frames should be treated as success even when `code` is absent.
- The session-start frame is currently observed as:
  - `msg_type=action`
  - `data.action=started`
  - `data.sessionId=...`
- This means `started` is nested under `data.action`, not necessarily exposed as a top-level `action`.
- A parser that defaults `missing code -> -1` will produce false provider errors such as:
  - `asr provider=iflytek_rtasr error code=-1 sid= msg=`
- On Ameba SDK `wsclient`, fragmented server JSON may be surfaced to the callback with final opcode `CONTINUATION` rather than `TEXT_FRAME`.
- If the callback only accepts `TEXT_FRAME`, valid `started` / `partial` / `final` frames can be dropped silently.
- The provider should also surface non-ASR result frames such as `res_type=frc`, plus empty final results (`ls=true` but no text), instead of silently treating them as "no event".

### Ameba SDK WebSocket Caveats

## Fixed Delay-And-Sum Beamforming Notes

### Project Decision
- SDK-provided beamforming is not used in the current ASR mainline.
- Current software beamforming strategy is fixed delay-and-sum beamforming (`DSB`) implemented inside the local pre-processing stage.
- Primary goal:
  - maximize ASR accuracy for a smart-home control panel user speaking toward the front of the device
- Current physical/software assumptions:
  - array topology: linear 2-mic
  - microphones: `AMIC1 + AMIC3`
  - nominal spacing: `50mm`
  - target scene: broadside/front-facing speech toward the screen
  - default steering: broadside
  - current secondary delay: `0` samples

### Why DSB Was Chosen
- It is robust, simple, and predictable on constrained embedded platforms.
- For broadside speech and a symmetric 2-mic line array, the best fixed-delay starting point is often `0-sample delay + sum + scale`.
- This is more ASR-friendly than many more aggressive spatial algorithms because it tends to preserve speech timbre and avoid unstable adaptation artifacts.
- It is cheap enough to run continuously on the current platform without pulling large extra SRAM or CPU from the online-ASR path.

### Current Implementation Notes
- Runtime backend name:
  - `fixed_dsb`
- Current implementation file:
  - `components/river_voice/river_voice_preproc_aivoice.c`
- Integration boundary:
  - `capture (2ch PCM16) -> fixed_dsb (1ch PCM16) -> silero_vad -> online ASR`
- The current implementation is intentionally fixed-point and lightweight:
  - take primary sample
  - take secondary sample
  - optionally apply a fixed integer-sample delay to the secondary path
  - sum both channels
  - divide by 2 with saturation

### Practical Engineering Notes
- The user-provided theory note describing CMSIS-DSP acceleration is directionally useful, but the current production path runs on the `CA32` application side, not a Cortex-M-only DSP micro-kernel path.
- The current project does not need a large external beamforming library for DSB.
- If future tuning shows off-axis users are common, the next tuning knob should be:
  - `secondary delay in integer samples`
- If future tuning shows room echo dominates interaction, DSB should stay as the spatial front-end, and AEC should be added only when a real playback reference is available.

### References Worth Keeping
- `Optimum Array Processing: Part IV of Detection, Estimation, and Modulation Theory` by Harry L. Van Trees
- `pyroomacoustics` for offline array/room simulation before firmware tuning
- User-provided DSB implementation note summarizing:
  - DSB simplicity
  - 40mm-60mm mic spacing guidance
  - broadside fixed-sum suitability for low-cost smart-home voice terminals
- Ameba SDK `wsclient` has a bounded internal send queue.
- If audio frames are pushed too aggressively, the SDK emits:
  - `ws_sendData: ERROR: Not get usable buffer, Please enlarge max_queue_size!`
- Practical implications for this project:
  - do not send every 16ms capture frame directly
  - stage audio locally
  - aggregate to larger chunks
  - pace sending at a stable interval
- When a VAD transition opens the cloud stream, the current triggering speech frame must also be sent immediately after `stream_open()`.
- If only pre-roll silence is flushed and the triggering speech frame is omitted, the provider may close the session before any recognisable speech reaches the server.
  - treat temporary send backlog as `busy`, not immediately as fatal IO
- Even after Wi-Fi and WebSocket are healthy, missing queue pacing alone is enough to produce:
  - VAD works
  - stream opens
  - but no ASR result returns

## iFlytek RTASR LLM Integration Notes
- The current official "实时语音转写大模型" document is not the same protocol as the older `rtasr.xfyun.cn` service.
- Current official request target:
  - `wss://office-api-ast-dx.iflyaisol.com/ast/communicate/v1?{query}`
- Current handshake parameters should include:
  - `appId`
  - `accessKeyId`
  - `uuid`
  - `utc`
  - `audio_encode`
  - `lang`
  - `samplerate`
  - `signature`
- Current signature rule:
  - exclude `signature`
  - sort all params by key ascending
  - URL-encode key and value separately
  - join as `k=v&...`
  - sign that `baseString` with `HmacSHA1(accessKeySecret)`
  - Base64-encode the HMAC output
- Current audio recommendation from the official doc:
  - `16kHz`
  - `16bit`
  - `mono`
  - `pcm`
  - recommended pacing: `40ms -> 1280 bytes`
- Current service-side end frame:
  - `{"end": true, "sessionId": "..."}`
- Important SDK integration pitfall on Ameba:
  - Realtek `create_wsclient()` does not accept a full `ws://host/path?query` string as the `url` argument
  - `url` must be only `ws://host` or `wss://host`
  - `path` and query should be passed separately
  - otherwise the SDK tries to resolve `host/path?...` as a hostname and `getaddrinfo` fails
- Current iFlytek error code notes most relevant to this project:
  - `35014`: UTC/time drift too large
  - `35030`: signature expired or repeated; repeated `uuid` under same time can trigger it
  - `100002`: signature error
  - `100012`: UTC deviation too large
  - `100016`: `accessKeyId` error
  - `100020`: `appId` and `accessKeyId` mismatch

## TFLite Micro Initialization Notes
- A useful standing principle for this project:
  - `make it work first, then make it fast`
  - for `Silero VAD`, this means:
    - first migrate and validate the original `float32` model
    - then measure `arena / heap / flash / latency`
    - only after real pressure appears, evaluate quantization or pruning
- Good ideas worth keeping from generic TFLM initialization templates:
  - explicit schema-version checking before runtime
  - explicit reporting of actual `TensorArena` usage
  - defensive logging around interpreter creation and `AllocateTensors()`
  - treating model runtime bring-up as a resource-measurement step, not only a functional step
- Important adaptations for current `ameba-river`:
  - the project already uses `MicroMutableOpResolver`, not `AllOpsResolver`
  - this is the correct direction for the current image budget and should be preserved
  - current runtime already prints actual arena usage from `interpreter->arena_used_bytes()`
  - current `Silero` runtime also checks model schema compatibility before opening
- Important caution for memory alignment:
  - many generic examples use:
    - `alignas(16) static uint8_t tensor_arena[...]`
  - in this project, the tensor arena is allocated dynamically from Realtek RTOS heaps
  - that means alignment should be validated against the allocator behavior, not assumed just because a static example used `alignas`
  - if future instability suggests alignment risk, add an explicit runtime alignment check for the allocated arena pointer
- Important caution for CPU/FPU guidance:
  - generic Cortex-M advice like `-mfpu=fpv5-sp-d16` does not apply directly to the current `RTL8730E` CA32 application core
  - current project build uses the CA32 toolchain path and hard-float configuration already provided by the SDK
- Important caution for cache examples:
  - generic `SCB_InvalidateDCache_by_Addr()` examples are conceptually relevant
  - but they are not drop-in APIs for the current `CA32` runtime
  - for this board, cache/DMA consistency must follow the Ameba CA32 path and SDK guidance
- Current project position:
  - no immediate change is required from this reference
  - it is best used as:
    - a checklist for defensive runtime validation
    - a reminder to keep resource reporting and alignment scrutiny in the bring-up path
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

## Float32-First Silero Guidance For RTL8730E
- Reference value:
  - high as an engineering strategy note
  - medium as direct implementation guidance
- Core recommendation worth keeping:
  - `make it work before make it fast` is the right policy for current `Silero VAD` migration
  - first stabilize:
    - model import
    - `TFLite Micro` runtime
    - board-side audio / task / cache behavior
  - only then decide whether quantization or pruning is necessary

### Directly Applicable To `ameba-river`
- Keep `Float32` as the first-stage deployment target.
  - this matches the current project decision
  - it avoids mixing model-compression error with runtime-integration bugs
- Convert microphone `int16` PCM to normalized `float32` before `Invoke()`.
  - current `Silero` detector already follows this rule
- Keep recurrent state as `float32` and update it with plain `memcpy`.
  - current `Silero` detector already follows this rule
- Measure real on-device cost before compressing.
  - required metrics:
    - flash size
    - tensor arena usage
    - runtime latency
    - steady-state heap headroom
- Budget tensor arena generously during bring-up, then shrink later.
  - current starting point is `256KB`

### Important RTL8730E-Specific Corrections
- Do not copy `Cortex-M` FPU flags into the current `CA32` path.
  - current `RTL8730E` `AP/CA32` build uses:
    - `-mcpu=cortex-a32`
    - `-mfpu=neon`
    - `-mfloat-abi=hard`
  - `-mfpu=fpv5-sp-d16` applies to `KM4`, not to the current `Silero` runtime path
- Do not copy `SCB_InvalidateDCache_by_Addr()` examples directly.
  - the cache-consistency warning is absolutely relevant
  - but the actual maintenance API must follow the `CA32` / Realtek SDK path, not a generic `Cortex-M` snippet
- Do not switch to `AllOpsResolver`.
  - current project should keep a minimal `MicroMutableOpResolver`
  - resolver bloat increases memory pressure without helping current bring-up
- Do not treat a simple latency threshold as a release decision by itself.
  - final acceptability depends on:
    - concurrent `AIVoice` load
    - Wi-Fi load
    - long-run stability
    - total voice-pipeline latency

### Current Project Implications
- This reference supports the existing decision:
  - migrate original `Silero VAD` first
  - defer quantization / pruning
- This reference does not directly solve the current `RTL8730E` blocker:
  - current issue is SDK-specific `TFLite Micro` tensor compatibility
  - not model precision or quantization drift
- The next high-value additions for current `Silero` work are:
  - `Invoke()` latency instrumentation
  - tensor-arena usage reporting
  - task-stack watermark reporting
  - CA32-appropriate DMA/cache-consistency checks

## Future Reference Intake Rule
- For future external materials, record them directly into `.codex/knowledge.md` after triage.
- Default triage format:
  - `useful as-is`
  - `useful with RTL8730E-specific corrections`
  - `not suitable for direct reuse`
- Prefer storing actionable conclusions over preserving raw prose.

## iFlytek RTASR LLM Notes
- Source:
  - official doc used for the current integration:
    - `https://www.xfyun.cn/doc/spark/asr_llm/rtasr_llm.html`
- Current project relevance:
  - `high`
  - this is the protocol baseline for the first real online-ASR provider in `ameba-river`
- Key protocol points extracted for current implementation:
  - WebSocket endpoint:
    - host `office-api-ast-dx.iflyaisol.com`
    - path `/ast/communicate/v1`
  - authentication:
    - query-string based
    - sorted parameters
    - `HMAC-SHA1`
    - `Base64`
    - URL-encoded signature
  - current audio contract:
    - `16 kHz`
    - `mono`
    - `16-bit`
    - `pcm_s16le`
  - result parsing:
    - transcript text is nested under:
      - `data.cn.st.rt[].ws[].cw[].w`
    - final result can be inferred from:
      - `data.ls == true`
      - or `data.cn.st.type == 0`
- Project-specific implications:
  - provider code should stay inside `components/river_cloud`
  - signing and session setup should not leak into `river_voice`
  - local VAD / segment policy must remain provider-neutral
- Current implementation status in this repository:
  - streaming provider is implemented
  - batch/non-streaming provider upload is not yet implemented for iFlytek
  - result callback path is already provider-neutral, so later vendors can reuse the same app-facing shape

## Fixed Delay-And-Sum Beamforming Notes
- Decision:
  - remove SDK BF/AEC from the current ASR mainline
  - use software `fixed delay-and-sum beamforming` as the only spatial front-end
- Why this fits the current product stage:
  - goal is ASR accuracy first, not maximum acoustic sophistication
  - current device is a smart-home control panel with the user usually facing the screen
  - the existing array is a `2-mic linear array` with `50 mm` spacing
- Current engineering assumptions:
  - microphones: `AMIC1 + AMIC3`
  - sample rate: `16 kHz`
  - frame size: `16 ms`
  - target direction: front-facing `broadside`
  - current fixed delay: `0 sample`
- Algorithm shape kept in code:
  - read two channels
  - optionally delay the secondary mic by a small integer-sample offset
  - sum the aligned channels
  - divide by `2`
  - saturate to `PCM16`
- Why DSB is preferred here:
  - lower distortion risk than black-box beamformers for ASR-oriented bring-up
  - predictable resource cost on `RTL8730E`
  - easy to A/B against `single-mic` or future `AEC` paths
- Practical acoustic guidance worth keeping:
  - `40 mm - 60 mm` spacing is a good target range for low-cost smart-home panels
  - DSB is especially strong when the talker is mostly in front of the device
  - broadside zero-delay DSB is the simplest robust starting point before any directional tuning
- Important future rule:
  - do not enable AEC on the ASR path unless there is a real and trustworthy playback reference
  - fake or zero-filled references can damage ASR input instead of helping it
- Useful external references for future tuning:
  - Harry L. Van Trees, `Optimum Array Processing`
  - `pyroomacoustics` for offline room/array simulation
  - ARM `CMSIS-DSP` style optimized vector-add/scaling patterns as implementation inspiration, even if the current target is not a Cortex-M-only pipeline

## WebRTC AECM Experimental Integration Notes
- Current repository status:
  - `WebRTC AECM` assets may be preserved in-tree for later experiments
  - they should not be treated as the active mainline front-end unless explicitly re-integrated
- Mainline reminder:
  - current stable ASR path is still:
    - `capture -> fixed_dsb -> silero_vad -> online ASR`
- Key constraint:
  - current mainline frame size is `16 ms / 256 samples @ 16 kHz`
  - `WebRTC AECM` processes `10 ms / 160 samples @ 16 kHz`
- Important implication:
  - a naive `256-sample input -> internal 160-sample AEC blocks -> try pop 256 samples` adapter can break frame alignment
  - this is risky for:
    - `Silero VAD`
    - ASR segment boundaries
    - per-frame diagnostics
- Recommended integration rule:
  - keep `AECM` as a separate experimental profile or explicit feature gate
  - do not wire it directly into the default `vad_probe` / `asr_mainline` path
  - solve strict frame alignment first
  - only then evaluate acoustic benefit
- Recommended runtime gating rule:
  - enable AEC only when playback reference is truly present and stable
  - use hysteresis / stable windows
  - do not toggle AEC on/off by a single-frame peak threshold
  - avoid resetting AEC state every time one quiet reference frame appears
- Architectural rule:
  - `vad_probe` should stay focused on validating:
    - capture
    - preproc
    - VAD
    - ASR uplink
  - it should not become the permanent dumping ground for experimental AEC control logic
