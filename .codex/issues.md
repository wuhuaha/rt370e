# Issues And Risks

## Current
- The online control path is still a stub. No real backend transport is connected in Step 1.
- `speechmind` has not been integrated yet. This step only prepares the architecture needed to absorb it safely.
- The SDK project generator cannot directly scaffold `component/application/speechmind` as an external example template, so reuse will need selective code import instead of one-shot cloning.
- The exact board population is still not recorded in-project:
  - `RTL8730EAM` vs `RTL8730EAH`
  - `NOR` vs `NAND`
- Audio hardware choices are not fixed yet:
  - AMIC vs DMIC as the first local front-end path
  - whether external `12V` PA mode is needed
- Some EVB GPIOs may be unavailable on `RTL8730EAM` without resistor changes.

## Mitigation
- Keep all online provider logic behind `river_cloud_adapter_*`.
- Keep all local speech entry points behind `river_voice_frontend_*`.
- Use monitor commands first so board bring-up is independent from microphone pipeline risk.
- Reuse `speechmind` incrementally once Step 1 board validation is stable.
- Use project-side `build_info.h` bootstrap headers so `RTL8730E` parallel builds do not depend on SDK generation order.
- Keep board constraints in `.codex/knowledge.md` and avoid restricted pins until the exact board variant is confirmed.
- Keep flash type explicit in build and flash instructions.
- Default software assumptions to `5V` audio power until a deliberate `12V` hardware rework plan exists.
