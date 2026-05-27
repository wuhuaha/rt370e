# RTL8730E Flash Profiles

This directory is the project-owned source of truth for `RTL8730E` flash profiles.

## Files
- `RTL8730E_NOR.sdk.json`
  - decrypted copy of the SDK stock NOR profile
  - kept only for comparison and traceability
- `RTL8730E_NOR.json`
  - project development NOR profile source
  - this is the JSON file to edit
- `RTL8730E_NOR.rdev`
  - encrypted profile generated from `RTL8730E_NOR.json`
  - this is the file used by the project flash wrapper when `-m nor` is selected
- `RTL8730E_NAND.sdk.json`
  - decrypted copy of the SDK stock NAND profile
  - kept only for comparison and traceability
- `RTL8730E_NAND.json`
  - project NAND profile source for the current hardware
  - this is the JSON file to edit for NAND hardware
- `RTL8730E_NAND.rdev`
  - encrypted profile generated from `RTL8730E_NAND.json`
  - this is the default file used by the project flash wrapper for the current hardware

## Current policy
- board flash type: `NAND`
- current profile purpose: current hardware bring-up
- current NAND change from SDK stock:
  - keep bootloader slot unchanged
  - expand `km0_km4_ca32_app.bin` download range from `0x00040000-0x00300000`
    to `0x00040000-0x00C40000`
- current NOR change from SDK stock:
  - keep bootloader slot unchanged
  - expand `km0_km4_ca32_app.bin` download range from `0x08040000-0x08300000`
    to `0x08040000-0x08600000`

## Why
The SDK stock NOR and NAND profiles only allocate `0x2C0000` bytes for the
combined app package. The current `ameba-river` image already exceeds that
range, so flashing fails before download when a stock profile is used.

The project profiles reclaim the OTA2 region for a larger single-slot app package.

For the current NAND hardware, `tools/river_flash.py` defaults to NAND. The
explicit form `tools/river_flash.py -m nand` is equivalent. NAND mode also runs the SDK
Flash tool from a temporary copy with a larger retry budget. This absorbs
intermittent GD5F1GM7U NAND program timeouts without modifying the SDK checkout.

Old NOR hardware must be flashed with `tools/river_flash.py -m nor`; otherwise
the wrapper intentionally uses the current NAND profile.

## Warning
These are development-only flash profiles.

They intentionally consume the SDK OTA2 area for extra app space.
That is acceptable for current local bring-up, but it means:
- do not rely on OTA2 with this profile
- if future OTA or dual-slot update is required, flash layout and boot assumptions must be reworked together
- `.rdev` changes alone do not redefine the firmware-side flash layout in the SDK

## Regeneration
After editing `RTL8730E_NOR.json`, regenerate the encrypted `.rdev`:

```bash
cd /root/ameba-river
python3 tools/generate_rdev.py \
  --input board/rtl8730e/profiles/RTL8730E_NOR.json \
  --output board/rtl8730e/profiles/RTL8730E_NOR.rdev

python3 tools/generate_rdev.py \
  --input board/rtl8730e/profiles/RTL8730E_NAND.json \
  --output board/rtl8730e/profiles/RTL8730E_NAND.rdev
```
