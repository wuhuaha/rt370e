# Verification

## Step 1
Build:
```bash
cd /root/ameba-river
source env.sh
ameba.py soc RTL8730E
ameba.py build
```

Runtime checks from monitor:
```text
river status
river echo hello from board
river device light on
river device fan toggle
river status
```

Expected behavior:
- boot log prints `ameba-river boot`
- `river status` prints local front-end mode and device states
- `river echo ...` prints the same text through the cloud stub path
- `river device ...` updates and prints device state
