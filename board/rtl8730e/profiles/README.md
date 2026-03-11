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
  - this is the file used by the project flash wrapper

## Current policy
- board flash type: `NOR`
- current profile purpose: development
- current change from SDK stock:
  - keep bootloader slot unchanged
  - expand `km0_km4_ca32_app.bin` download range from `0x08040000-0x08300000`
    to `0x08040000-0x08600000`

## Why
The SDK stock NOR profile only allocates `0x2C0000` bytes for the combined app package.
The current `ameba-river` image already exceeds that range, so flashing fails before download.

The development profile reclaims the OTA2 region for a larger single-slot app package.

## Warning
This is a development-only flash profile.

It intentionally consumes the SDK OTA2 area for extra app space.
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
```
