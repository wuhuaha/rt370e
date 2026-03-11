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

## Step 2.2
Build and flash:
```bash
cd /root/ameba-river
source env.sh
ameba.py soc RTL8730E
ameba.py build -p
ameba.py flash -p /dev/ttyUSB0 -b 1500000 -m nor
```

Boot-time check from monitor:
- No manual `river` command is required in this step.
- Wait for boot to finish and look for these lines:

```text
[river][voice] boot audio echo diagnostics enabled
[river][voice] boot audio echo autostart enabled
[river][voice] audio echo config: 16000 Hz, 2 ch, 1000 ms delay, AMIC1+AMIC3 -> speaker
[river][voice] audio echo started
```

Manual check:
- Speak into the microphone array after boot completes.
- Wait about `1 second`.
- Watch the repeating `[river][voice][diag] ...` line and listen for delayed replay.

Expected behavior:
- `audio_echo=running` appears in the boot-time status dump
- diagnostics print automatically about every `1 second`
- no shell interaction is required to trigger the loop

## Step 2.3
Build:
```bash
cd /root/ameba-river
source env.sh
ameba.py soc RTL8730E
ameba.py build -p
```

Expected build result:
- the build succeeds with `CONFIG_RIVER_*` options taking effect in `river_app.c`, `river_voice_frontend.c`, and `river_diag_cmd.c`
- the boot-time echo autostart path is no longer compiled out accidentally

## Step 2.4
Build and flash:
```bash
cd /root/ameba-river
source env.sh
ameba.py soc RTL8730E
ameba.py build -p
ameba.py flash -p /dev/ttyUSB0 -b 1500000 -m nor
```

Boot-time expectation:
```text
[river][voice] audio echo config: 16000 Hz, 1 ch, 1000 ms delay, AMIC3 mono -> speaker
```

Manual check:
- After boot, speak close to the board microphone path used by `AMIC3`.
- Wait about `1 second`.
- Compare the result with the previous build:
  - whether idle speaker hiss/noise is reduced
  - whether delayed speech becomes distinguishable

Expected diagnostics:
- low-level background peaks may still exist, but the replayed idle noise should be reduced by the software gate
- speech should drive `cap_peak` above the gate threshold and appear on `play_peak` about `1 second` later

## Step 2.5
Build and flash:
```bash
cd /root/ameba-river
source env.sh
ameba.py soc RTL8730E
ameba.py build -p
ameba.py flash -p /dev/ttyUSB0 -b 1500000 -m nor
```

Boot-time expectation:
```text
[river][voice] boot speaker playback diagnostics enabled
[river][voice] boot speaker playback autostart enabled
[river][voice] speaker test config: 16000 Hz, 2 ch, 16-bit, dual-mono tone -> speaker
[river][voice] speaker test gain: hw=0.60 sw=1.00 amplitude=16000
[river][voice] speaker test pattern: 1000Hz 400ms, gap 200ms, 1500Hz 400ms, gap 1000ms
[river][voice] speaker test started
```

Expected repeating serial diagnostics:
```text
[river][voice][spk] segment=tone_a freq=1000Hz peak=6000 write_ok=...
[river][voice][spk] segment=gap_b freq=0Hz peak=0 write_ok=...
```

Manual check:
- No `river` shell command is required in this step.
- After boot completes, listen for a repeating pattern:
  - a medium-pitch beep
  - short silence
  - a higher-pitch beep
  - longer silence
- This pattern should repeat continuously until reset or power-off.

Interpretation:
- The repeating beep is clean and recognizable:
  - the direct speaker playback path is proven
  - remaining echo issues should be traced to capture routing or echo processing, not basic playback hardware

## Step 2.6
Build and flash:
```bash
cd /root/ameba-river
source env.sh
ameba.py soc RTL8730E
ameba.py build -p
ameba.py flash -p /dev/ttyUSB0 -b 1500000 -m nor
```

Boot-time expectation:
```text
[river][voice] boot audio echo diagnostics enabled
[river][voice] boot audio echo autostart enabled
[river][voice] audio echo config: 16000 Hz, 1 ch, 1000 ms delay, AMIC3 mono -> speaker
[river][voice] audio echo gain: hw=0.45 cap=0x20 gate=1024 mic=AMIC3 micbst=5dB
[river][voice] audio echo started
```

Expected diagnostics:
```text
[river][voice][diag] cap_peak=[...,0] play_peak=[...,0] read_ok=50 write_ok=50 read_fail=0 write_fail=0 partial=0
```

Manual check:
- No shell command is required.
- After boot completes, speak close to the microphone path for `2-3 seconds`.
- Wait about `1 second` for the delayed replay.
- Compare with previous results:
  - whether delayed speech is now audible
  - whether idle noise is acceptable
  - whether speech is still buried in noise

## Step 3.0
Build and flash:
```bash
cd /root/ameba-river
source env.sh
ameba.py soc RTL8730E
ameba.py build -p
ameba.py flash -p /dev/ttyUSB0 -b 1500000 -m nor
ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Boot-time expectation:
```text
[river][voice] board array: EV8730EA2/EV730EA2 linear-2mic-50mm primary=AMIC1 secondary=AMIC3 spacing=50mm
[river][voice] capture profile: 16000 Hz, 16ms, 2ch, AMIC1+AMIC3
[river][voice] preproc backend: aivoice_afe AFE_LINEAR_2MIC_50MM 16000 Hz 16ms in=2ch out=1ch
[river] local_preproc=aivoice_afe
[river][voice] boot audio echo diagnostics enabled
[river][voice] boot audio echo autostart enabled
[river][voice] audio echo config: 16000 Hz capture dual-mic -> AFE 1ch -> 16000 Hz playback dual-mono, 1000 ms delay, AMIC1+AMIC3 -> speaker
[river][voice] audio echo gain: hw=0.65 sw=1.00 pcm=x2 cap=0x28 preproc=aivoice_afe
[river][voice] audio echo started
```

Expected diagnostics:
```text
[river][voice][diag] cap_peak=[..., ...] play_peak=[..., ...] read_ok=... proc_ok=... write_ok=... read_fail=... proc_fail=... write_fail=... partial_read=... partial_proc=...
```

Manual check:
- No shell command is required.
- Speak at about `20-30 cm` first, then test again at about `0.5-1.0 m`.
- Wait about `1 second` for delayed replay.
- Compare with the previous raw-array echo:
  - whether speech is more intelligible
  - whether background hiss/noise is lower
  - whether `proc_ok` remains stable and `proc_fail` stays `0`

Interpretation:
- `cap_peak` is active but `proc_fail` grows:
  - treat this as an AFE integration issue before changing board routing
- `cap_peak` is active, `proc_ok` is stable, and `play_peak` is active:
  - the full `capture -> AFE -> delayed replay` chain is healthy
- replay is still poor even when the diagnostics look healthy:
  - next step should be VAD plus playback-reference plumbing, not more raw-mix tuning

## Step 3.1
Build and flash:
```bash
cd /root/ameba-river
source env.sh
ameba.py soc RTL8730E
ameba.py build -p
ameba.py flash -p /dev/ttyUSB0 -b 1500000 -m nor
ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Boot-time expectation:
```text
[river][voice] preproc afe: aec=off ns=on(low) agc=on(fixed=15dB) ssl=off ref=0
[river][voice] audio echo gain: hw=0.80 sw=1.00 pcm=x2 post_agc=target12000/maxx4 gate=96 cap=0x30 preproc=aivoice_afe
```

Expected diagnostics:
```text
[river][voice][diag] cap_peak=[..., ...] afe_peak=... play_peak=[..., ...] read_ok=... proc_ok=... write_ok=... read_fail=... proc_fail=... write_fail=... partial_read=... partial_proc=...
```

Manual check:
- Speak at `20-30 cm`, then at `0.5-1.0 m`.
- Compare with the previous AFE-only build:
  - whether replay loudness is higher
  - whether far-field speech is easier to distinguish
  - whether idle noise stays acceptable

Interpretation:
- `afe_peak` is low while `cap_peak` is active:
  - the next tuning point is AFE policy, not replay volume
- `afe_peak` is healthy but `play_peak` is still low:
  - the next tuning point is only replay gain
- `afe_peak` and `play_peak` are both healthy but far-field speech is still poor:
  - stop tuning replay gain and move next to `VAD + reference-path + AEC`

## Step 3.1.1
Build and flash:
```bash
cd /root/ameba-river
source env.sh
ameba.py soc RTL8730E
ameba.py build -p
ameba.py flash -p /dev/ttyUSB0 -b 1500000 -m nor
ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Boot-time expectation:
```text
[river][voice] preproc afe: aec=off ns=on(mid) agc=on(fixed=9dB) ssl=off ref=0
[river][voice] audio echo gain: hw=0.80 sw=1.00 pcm=x2 post_agc=target9000/maxx2 floor=192 cap=0x30 preproc=aivoice_afe
```

Expected diagnostics:
```text
[river][voice][diag] cap_peak=[..., ...] afe_peak=... play_peak=[..., ...] read_ok=... proc_ok=... write_ok=... read_fail=... proc_fail=... write_fail=... partial_read=... partial_proc=...
```

Manual check:
- Keep the room quiet for `3-5` seconds first and listen for idle hiss.
- Then speak at `20-30 cm`, and again at `0.5-1.0 m`.
- Compare with Step `3.1`:
  - whether idle noise is clearly lower
  - whether near-field speech remains large enough
  - whether far-field speech is still understandable enough for the next AEC step

Interpretation:
- idle hiss drops clearly and near speech remains usable:
  - this round is successful; move next to playback-reference plumbing and `AEC`
- idle hiss drops but far speech becomes too weak:
  - the next adjustment should be limited gain rebalance, not reintroducing `VAD`
- idle hiss is still large even after this step:
  - stop tuning replay gain and move next to `AEC/reference-path`

## Step 3.2
Build and flash:
```bash
cd /root/ameba-river
source env.sh
ameba.py soc RTL8730E
ameba.py build -p
ameba.py flash -p /dev/ttyUSB0 -b 1500000 -m nor
ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Boot-time expectation:
```text
[river][voice] playback ref: deferred backend=playback_ring source=post-delay mono speaker feed
[river] local_playback_ref=playback_ring
[river][voice] audio echo ref: backend=playback_ring source=post-delay mono history=1536ms aec=off
```

Expected diagnostics:
```text
[river][voice][diag] ... ref_read_ok=... ref_read_miss=... ref_write_ok=... ref_write_fail=... ...
```

Manual check:
- Focus on serial diagnostics in this step; audible behavior should stay close to Step `3.1.1`.
- After boot, let the board run for a few seconds.
- Confirm:
  - `ref_write_ok` keeps increasing
  - `ref_write_fail` stays `0`
  - `ref_read_ok` becomes stable after startup
  - `ref_read_miss` should mostly be limited to startup or reset moments

Interpretation:
- `ref_write_ok` stable and `ref_write_fail=0`:
  - the speaker-reference path is alive and ready for the `AEC` step
- `ref_write_fail` increases:
  - stop before enabling `AEC`; the reference ring path is not stable enough yet
- audible quality changes sharply in this step:
  - that is unexpected; inspect the new reference counters first before touching `AEC`

## Step 3.3
Build and flash:
```bash
cd /root/ameba-river
source env.sh
ameba.py soc RTL8730E
ameba.py build -p
ameba.py flash -p /dev/ttyUSB0 -b 1500000 -m nor
ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Boot-time expectation:
```text
[river][voice] preproc afe: mode=com aec=on(mid,res=mid) ns=on(mid) agc=on(adaptive+5dB) ssl=off ref=playback_ring(1ch)
[river][voice] audio echo config: 16000 Hz capture dual-mic + 1ch ref -> AEC/AFE 1ch -> 16000 Hz playback dual-mono, 1000 ms delay, AMIC1+AMIC3 -> speaker
[river][voice] audio echo ref: backend=playback_ring source=post-delay mono history=1536ms aec=on
```

Expected diagnostics:
```text
[river][voice][diag] cap_peak=[..., ...] afe_peak=... play_peak=[..., ...] read_ok=... proc_ok=... write_ok=... ref_read_ok=... ref_read_miss=... ref_write_ok=... ref_write_fail=... read_fail=... proc_fail=... write_fail=...
```

Manual check:
- First listen in a quiet room for `3-5` seconds:
  - compare idle hiss with Step `3.1.1`
- Then speak at `20-30 cm`
- Then speak again at `0.5-1.0 m`
- Compare with the previous AFE-only round:
  - whether idle noise drops
  - whether near speech still stays clear enough
  - whether farther speech is preserved or becomes too suppressed

Interpretation:
- `ref_write_ok` and `ref_read_ok` are stable, while idle hiss is lower:
  - the `AEC` path is alive and helping
- reference counters are healthy but speech becomes thin or unstable:
  - tune `AEC` policy or reference timing next, not `VAD`
- `proc_fail` grows after `AEC` is enabled:
  - treat this as a preproc integration issue before changing capture or speaker routing
- `ref_read_miss` keeps growing after startup:
  - the reference ring is not keeping pace; do not start beamforming work yet

## Step 2.6.1
Build and flash:
```bash
cd /root/ameba-river
source env.sh
ameba.py soc RTL8730E
ameba.py build -p
ameba.py flash -p /dev/ttyUSB0 -b 1500000 -m nor
```

Boot-time expectation:
```text
[river][voice] audio echo config: 16000 Hz capture mono -> 16000 Hz playback dual-mono, 1000 ms delay, AMIC3 -> speaker
[river][voice] audio echo gain: hw=0.45 sw=1.00 cap=0x20 gate=1024 mic=AMIC3 micbst=5dB
```

Expected diagnostics:
- `cap_peak` remains meaningful while speaking
- `play_peak` remains meaningful about `1 second` later
- If this step works, the user should finally hear delayed replay because the playback format now matches the previously validated direct speaker test shape

## Step 2.6.2
Build and flash:
```bash
cd /root/ameba-river
source env.sh
ameba.py soc RTL8730E
ameba.py build -p
ameba.py flash -p /dev/ttyUSB0 -b 1500000 -m nor
```

Boot-time expectation:
```text
[river][voice] audio echo gain: hw=0.60 sw=1.00 pcm=x4 cap=0x20 gate=1024 mic=AMIC3 micbst=5dB
```

Manual check:
- Speak close to the microphone for `2-3 seconds`.
- Wait about `1 second`.
- Confirm whether the delayed replay is now comfortably audible.
- Also watch for clipping or harsh distortion because this step intentionally raises gain aggressively.

## Step 2.7
Build and flash:
```bash
cd /root/ameba-river
source env.sh
ameba.py soc RTL8730E
ameba.py build -p
ameba.py flash -p /dev/ttyUSB0 -b 1500000 -m nor
```

Boot-time expectation:
```text
[river][voice] board array: EV8730EA2/EV730EA2 linear-2mic-50mm primary=AMIC1 secondary=AMIC3 spacing=50mm
[river][voice] board array aux: AMIC5 reserved for future AFE/beamforming raw tap
[river][voice] aivoice-ready geometry: AFE_LINEAR_2MIC_50MM
[river][voice] audio echo config: 16000 Hz capture dual-mic -> 16000 Hz playback dual-mono, 1000 ms delay, AMIC1+AMIC3 mix -> speaker
[river][voice] audio echo array: linear-2mic-50mm spacing=50mm aivoice=AFE_LINEAR_2MIC_50MM aux=AMIC5(reserved)
```

Manual check:
- No `river` shell command is required in this step.
- Speak at the board from the normal frontal direction for `2-3` seconds.
- Wait about `1 second` and listen for delayed replay.
- Compare the result with the previous single-mic echo build:
  - whether voice loudness improves
  - whether front-facing speech is cleaner
  - whether both capture channels show activity in diagnostics

Expected diagnostics:
```text
[river][voice][diag] cap_peak=[..., ...] play_peak=[..., ...] read_ok=50 write_ok=50 read_fail=0 write_fail=0 partial=0
```

Quick interpretation:
- `cap_peak` channel 0 and channel 1 both move while speaking:
  - the `AMIC1 + AMIC3` pair is alive
- only one capture channel moves consistently:
  - one array leg or its gain/routing still needs adjustment
- delayed playback is present but noisy:
  - the dual-mic digital path works, but this raw average still needs AFE / beamforming / gain tuning

## Step 2.8
Build and flash:
```bash
cd /root/ameba-river
source env.sh
ameba.py soc RTL8730E
ameba.py build -p
ameba.py flash -p /dev/ttyUSB0 -b 1500000 -m nor
```

Boot-time expectation:
```text
[river][voice] audio echo gain: hw=0.60 sw=1.00 pcm=x2 agc_target=6000 agc_max=x8 gate=256 micbst=[20dB,20dB]
[river][voice] audio echo mix: dominant=3 weak=1 focus_ratio=140%
```

Manual check:
- Stand at several distances and compare:
  - near field: `20-30 cm`
  - moderate field: about `50-100 cm`
- Speak in front of the board for `2-3` seconds each time.
- Wait about `1 second` and compare with Step 2.7:
  - whether replay loudness improved
  - whether moderate-distance speech is still audible
  - whether background hiss or clipping became worse

Expected diagnostics:
```text
[river][voice][diag] cap_peak=[..., ...] play_peak=[..., ...] read_ok=50 write_ok=50 read_fail=0 write_fail=0 partial=0
```

Quick interpretation:
- moderate-distance speech becomes audible and `cap_peak` stays nonzero:
  - the raw dual-mic debug path is good enough to move on to AFE integration
- loud near-field speech becomes harsh but farther speech improves:
  - current AGC / gain move is helping, but real compressor / AFE is the next step
- far speech is still weak while `cap_peak` is also low:
  - capture sensitivity is still the main bottleneck, so next step should compare another mic path or AFE front-end
