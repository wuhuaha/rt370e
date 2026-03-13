# Codebase Architecture & Performance Review

This document tracks architectural reviews, performance audits, and acoustic verification for the `ameba-river` project.

---

## [2026-03-13] Milestone: Full Asynchronous ASR Pipeline & IPC Protection

### 1. Architectural Impact
- **Decoupled Pipeline**: Re-architected the system from a monolithic loop to a **Producer-Consumer model**. Voice capture, VAD inference, and Cloud transport now run in separate tasks with varying priorities.
- **Boot Flow Control**: Implemented a **"Network Gate"** in `river_app.c`. Heavy audio/VAD tasks are now deferred until L3 (IP) and SNTP (Time) are stable. This prevents boot-time resource contention.
- **Async Cloud Worker**: The new `river_cloud_wk` task isolates blocking WebSocket/SSL calls from the real-time VAD processing loop.

### 2. Performance & Real-time Considerations
- **IPC Starvation Mitigation**: Identified that SSL handshakes (heavy RSA) were starving CA32-KM4 IPC interrupts. Resolved by temporary task priority lowering and dedicated Capture task (Prio 6).
- **Two-Level Ring Buffer**: 
    - Level 1: 128ms driver-level buffer to survive IRQ jitters.
    - Level 2: 2000ms application-level Ring Buffer in `river_cloud_adapter` to survive network handshakes without losing speech onset.
- **Memory Optimization**: Reduced VAD Arena to 192KB and streamlined WebSocket RX/TX buffers to 1KB/0.5KB, freeing up contiguous heap for SSL.

### 3. DSP & Acoustic State
- **AFE Bypass Path**: Added a zero-cost bypass mode in `river_voice_preproc_aivoice.c` to maximize system headroom during initial bring-up. 
- **Acoustic Continuity**: Confirmed that the Ring Buffer catch-up mechanism prevents "First-Word-Truncation" (FWT) during WebSocket establishment.

---
