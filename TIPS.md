# Real-time Debugging & Optimization Tips (Ameba-River)

This file contains tactical advice and immediate troubleshooting steps based on real-time log analysis and board bring-up observations.

---

## [2026-03-12] Wi-Fi Connection Phase 5: Authentication Stalemate

### 1. Diagnosis: Consistent `auth_fail (-4109)`
**Status**: The SNTP/UTC barrier is REMOVED via build-time seeding. `BUSY (-3)` is GONE.
**Current Blocker**: Every connection attempt to `Keeu` fails with `-4109`.

### 2. Tactical Advice

#### A. The "Ghost Character" Check (Extreme Priority)
- **Check**: Open `include/river/river_wifi_credentials.h` in a hex editor or use `cat -A`.
- **Reason**: In some development environments, copying a password from a chat tool or document can introduce a **Zero-Width Space** or a trailing `\r` (carriage return).
- **Action**: Manually delete the password string and re-type it character by character.

#### B. AP Security Lockdown
- **Observation**: Scan shows `sec=wpa2_aes(0x400004)`.
- **Action**: Check if the router has **"PMF" (Protected Management Frames)** enabled or set to "Required". Some older Ameba SDK snapshots have issues with PMF-Required networks.
- **Action**: Check if there is an **IP/MAC Filtering** or "White List" on the router that is actively rejecting the board.

#### C. Manual Connect Test via Monitor
- **Action**: If you have `river audio` commands, add a `river wifi connect <ssid> <pass>` command.
- **Reason**: This allows testing a different password string at runtime without re-flashing the image.

---

## Acoustic & VAD Tuning Tips
... (omitted)
