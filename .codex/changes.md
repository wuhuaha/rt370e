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
