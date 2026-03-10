# ameba-river

External Ameba RTOS project for `RTL8730E`, targeting a smart voice home-control device.

Current phase:
- bootable project skeleton
- monitor command based echo
- simulated home device control
- reserved interfaces for local VAD, wake word, offline ASR, and future online/offline fusion

Build:
```bash
cd /root/ameba-river
source env.sh
ameba.py soc RTL8730E
ameba.py build
```

Initial run checks:
```text
river status
river echo hello
river device light on
river device fan toggle
```

Process records are maintained under `.codex/`.
