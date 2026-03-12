# Real-time Debugging & Optimization Tips (Ameba-River)

This file contains tactical advice and immediate troubleshooting steps based on real-time log analysis and board bring-up observations.

---

## [2026-03-12] Wi-Fi Connection Race Condition & Busy Conflicts

### 1. Diagnosis: The "Phantom" Fast-Connect
**Observation**: Even with `sdk fast connect disabled`, the log shows `wifi retry for fast connect.`.
**Root Cause**: The RTL8730E SDK often has a background thread (or logic in `wifi_fast_connect.c`) that reads the last successful AP info from Flash and attempts a join before the application-layer task starts. This causes `RTK_ERR_BUSY (-3)` when the `river_wifi_station` task tries to initiate its own connection.

### 2. Immediate Tactical Advice

#### A. Force-Clear Flash Config (Nuclear Option)
If the SDK keeps "remembering" a bad or conflicting AP, use the following to clear the Flash sector dedicated to Wi-Fi settings:
- **Action**: In your initialization code (one-time), call `erase_wifi_config()`.
- **Reason**: This prevents the SDK from ever finding "fast-connect" candidates in Flash.

#### B. Disable Power Management (IPS/LPS) during Join
**Observation**: `[WLAN-A] IPS in/out` transitions are happening during authentication/association.
- **Action**:
  - already adopted in code: `wifi_set_lps_enable(RTW_FALSE)` during STA bring-up
  - `IPS` has no equivalent public runtime API on the current RTL8730E SDK snapshot, so it remains a board/SDK-level troubleshooting direction rather than an app-layer toggle
- **Reason**: Entering Power Save mode during WPA2 handshakes often leads to `auth_fail (-4109)` or `assoc_fail (-4111)` due to missed packets.

#### C. Handle `BUSY (-3)` with Hard Disconnect
**Observation**: The app waits for the existing flow, but then the SDK disconnects it immediately.
- **Action**: If `wifi_connect` returns `-3`, wait for a maximum of 3-5 seconds. If IPv4 is not assigned within that window, call `wifi_disconnect()` to force-reset the driver state before the next retry.

#### D. Verify BSSID/Security Mismatch
**Observation**: `scan_exact` strategy failed with `auth_fail`.
- **Action**: Ensure the router's security type (WPA2-AES vs WPA3) is correctly identified by the scan. If the router uses "Mixed Mode", try forcing `RTW_SECURITY_WPA2_AES_PSK` instead of relying on the auto-scan result.

#### E. Prefer Scan-Bound Connect First
**Observation**: If a valid scan candidate already exists, trying a generic `SSID + password` path first may cause extra background scans or authentication churn.
- **Action**: Prefer `BSSID + channel + security` (`scan_exact`) first, keep `basic` as a fallback only.
- **Reason**: This makes the bring-up sequence deterministic and easier to correlate with the router's association logs.

#### F. Read the Current App-Owned Wi-Fi State
When inspecting logs, look for these lines first:
- `sdk fast connect pre-disabled before wlan init`
- `sdk autoreconnect disabled; river owns reconnect policy`
- `sdk lps disabled during bring-up`
- `connect strategy=scan_exact ...`

If they are missing, the board is likely not running the latest Wi-Fi fix.

---

## Acoustic & VAD Tuning Tips

### 1. VAD Hangover Coordination
- **Tip**: Set `CONFIG_RIVER_SILERO_VAD_HANGOVER_FRAMES` to `30-50` (500-800ms) to prevent sentence truncation in Chinese.
- **Context**: Chinese fricatives at the end of words have very low energy; Silero might cut them off too early for iFlytek ASR to finalize correctly.

---
