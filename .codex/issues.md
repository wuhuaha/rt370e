# Issues And Risks

## Current (Active Investigation)
- **[WLAN-003]**: Handshake success rate vs server latency. While the local stack is stable, public cloud endpoints occasionally reset connections on the first burst upload.
- **[AIVOICE-001]**: AFE Bypass degrades noise-robustness. This is a temporary bring-up trade-off to prioritize SSL stability.

## Resolved (Closed)
- **[WLAN-001] Wi-Fi Join Race Condition**: Resolved by disabling SDK Fast-connect and implementing the Adoption Pattern in Step 5.
- **[WLAN-002] DHCP Timeout**: Resolved by increasing timeout to 25s and physical power-save locking.
- **[CLOUD-001] net_connect -82 (SSL Error)**: Resolved by isolating IPC starvation and increasing task stack to 12KB.
- **[CLOUD-002] Getaddrinfo Failure**: Resolved by decomposing full URLs into Host/Port/Path segments for libwsclient.

## Technical Debts
- Need to restore AFE processing once SSL memory footprint is further optimized.
- Consider moving Ring Buffer to PSRAM once dedicated PSRAM malloc is integrated.
