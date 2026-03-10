# Issues And Risks

## Current
- The online control path is still a stub. No real backend transport is connected in Step 1.
- `speechmind` has not been integrated yet. This step only prepares the architecture needed to absorb it safely.
- Board-side compile and runtime behavior are not yet validated on `RTL8730E`; user verification is required after this commit.
- The SDK project generator cannot directly scaffold `component/application/speechmind` as an external example template, so reuse will need selective code import instead of one-shot cloning.

## Mitigation
- Keep all online provider logic behind `river_cloud_adapter_*`.
- Keep all local speech entry points behind `river_voice_frontend_*`.
- Use monitor commands first so board bring-up is independent from microphone pipeline risk.
- Reuse `speechmind` incrementally once Step 1 board validation is stable.
- Use project-side `build_info.h` bootstrap headers so `RTL8730E` parallel builds do not depend on SDK generation order.
