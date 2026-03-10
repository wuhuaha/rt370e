# Verification

## Step 1
Build:
```bash
cd /root/ameba-river
source env.sh
ameba.py soc RTL8730E
ameba.py build -p
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

## Step 1.2
Reference baseline captured:
- Verified and recorded EVB defaults needed for bring-up and future hardware adaptation:
  - LOGUART `1500000 8N1`
  - USB and LOGUART download paths
  - NOR/NAND coexistence on EVB
  - audio path, amplifier, and `12V` safety note
  - `RTL8730EAM` GPIO restrictions
- Source document retained in `.codex` for traceability.

## Step 2
Build:
```bash
cd /root/ameba-river
source env.sh
ameba.py soc RTL8730E
ameba.py build -p
```

Runtime checks from monitor:
```text
river status
river audio status
river audio start
```

Manual check:
- Speak into the microphone array after `river audio start`.
- Wait about `1 second`.
- Confirm the captured voice is replayed from the speaker with an obvious fixed delay.
- Keep the speaker away from the microphones during this test to avoid strong acoustic feedback.

Stop and inspect:
```text
river audio stop
river audio status
river status
```

Expected behavior:
- `river audio start` prints the selected audio profile and reports `audio echo started`
- `river audio status` reports `audio_echo=running` while active
- Voice is replayed with approximately `1000 ms` delay
- `river audio stop` stops the loop and `river audio status` returns `audio_echo=stopped`

## Step 2.1
Build:
```bash
cd /root/ameba-river
source env.sh
ameba.py soc RTL8730E
ameba.py build -p
```

Runtime checks from monitor:
```text
river audio diag status
river audio diag on
river audio start
```

Manual check:
- Speak into the microphone array for at least `2-3 seconds`.
- Watch the repeating `[river][voice][diag] ...` line.
- After checking, stop the path:

```text
river audio stop
river audio diag off
river audio status
```

Expected behavior:
- `river audio diag on` changes status output to `audio_echo_diag=on`
- While echo is running, the board prints one diagnostic line about every `1 second`
- `read_ok` and `write_ok` continue increasing while the loop is healthy

Quick interpretation:
- `cap_peak` stays near `0` while speaking:
  - likely no useful microphone capture on the selected channels
- `cap_peak` is nonzero and `play_peak` becomes nonzero after the delay window:
  - capture, ring-buffer delay, and digital playback feed are all active
- `cap_peak` and `play_peak` both look healthy but no sound is heard:
  - likely analog output route, mute, amplifier, or board-level speaker path problem
- `read_fail` or `write_fail` increases:
  - treat this as an SDK/audio-driver issue before changing mic or speaker routing
