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

## Step 4.7
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
[river][voice] detector backend: silero_vad runtime=tflite_micro feed=256 samples window=512 samples context=64 samples model_input=576 samples model=silero_vad_16k_b1_fp32.tflite threshold_q15=16384 arena=256KB
[river][voice] detector policy: direct official-model migration is complete; compression stays deferred until on-device flash/heap/latency data requires it
[river][voice] silero_vad runtime ready: model=silero_vad_16k_b1_fp32.tflite arena=256KB used=...B threshold_q15=16384
[river] local_detector=silero_vad
```

Runtime diagnostics expectation:
```text
[river][voice][diag] ... vad_prob_q15=... vad=silence|speech vad_decisions=... vad_speech=... det_ok=... det_fail=...
```

Manual check:
- Stay silent for `3-5s` and confirm `vad=silence` dominates.
- Speak near-field for `2-3s` and confirm `vad_prob_q15` rises and `vad_speech` increments.
- Speak at `0.5-1.0m` and compare whether speech probability is still meaningfully above silence.

Interpretation:
- `silero_vad runtime ready` does not appear:
  - detector did not initialize on-device; inspect tensor arena and boot logs first
- `det_fail` increments:
  - detector inference is unstable; do not tune threshold yet
- `det_ok` increments but `vad_prob_q15` stays near `0` even during speech:
  - enhancement output or detector feed cadence is wrong
- `vad_prob_q15` stays high in silence:
  - threshold is too low or the current AEC/AFE profile leaks too much non-speech energy into the detector
  - the direct speaker playback path is proven

## Step 4.8
Build and flash with the project-owned NOR profile:
```bash
cd /root/ameba-river
source env.sh
ameba.py soc RTL8730E
ameba.py build -p
python3 tools/river_flash.py -p /dev/ttyUSB0
```

Expected wrapper output:
```text
[river_flash] profile=/root/ameba-river/board/rtl8730e/profiles/RTL8730E_NOR.rdev
[river_flash] image_dir=/root/ameba-river/build_RTL8730E/build/project_hp/image
```

Expected profile behavior:
- flashing uses the project-owned development NOR profile instead of the SDK stock `RTL8730E_NOR.rdev`
- `km4_boot_all.bin` still downloads into `0x08000000-0x08040000`
- `km0_km4_ca32_app.bin` is allowed to download into `0x08040000-0x08600000`

Manual check:
- confirm that the previous "bin too large" flash rejection no longer appears
- after flashing, monitor the board and confirm the normal boot log still appears

Interpretation:
- flashing still reports the image is too large:
  - confirm the wrapper printed the project profile path, not the SDK path
- boot fails after flashing:
  - treat this as a flash-layout compatibility issue, not a wrapper bug
  - compare the downloaded image size and any boot-stage fault log before enlarging the profile further

## Step 4.9
Build and flash after the `Silero` runtime construction fix:
```bash
cd /root/ameba-river
source env.sh
CCACHE_DISABLE=1 ameba.py build -p
python3 tools/river_flash.py -p /dev/ttyUSB0
ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Expected boot-time behavior:
- boot should no longer abort before detector runtime initialization
- these lines should now appear after the `AIVOICE` banner:

```text
[river][voice] silero_vad runtime ready: model=silero_vad_16k_b1_fp32.tflite arena=256KB used=...B threshold_q15=16384
[river] local_detector=silero_vad
```

Interpretation:
- the board still aborts before `silero_vad runtime ready`:
  - the next suspect is tensor binding or `AllocateTensors`, not the previous `MicroMutableOpResolver` lifetime bug
- the runtime-ready line appears and diagnostics continue:
  - the first board-side `Silero` boot crash is resolved

## Step 4.10
Build and flash after the tensor-binding alignment fix:
```bash
cd /root/ameba-river
source env.sh
CCACHE_DISABLE=1 ameba.py build -p
python3 tools/river_flash.py -p /dev/ttyUSB0
ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Expected boot-time behavior:
- the previous
  ```text
  [river][voice] silero_vad tensor binding failed
  ```
  line should disappear
- boot should now continue to:
  ```text
  [river][voice] silero_vad runtime ready: model=silero_vad_16k_b1_fp32.tflite arena=256KB used=...B threshold_q15=16384
  ```

If binding still fails:
- the serial log should now include:
  - input count
  - output count
  - per-tensor type
  - per-tensor dims
  - tensor name
- use that dump as the next source of truth; do not guess shapes from the host export
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

## Step 4.2
Build:
```bash
cd /root/ameba-river
source env.sh
ameba.py soc RTL8730E
ameba.py build -p
```

Boot-time expectation:
```text
[river][voice] detector backend: silero_vad staged runtime=tflite_micro feed=256 samples window=512 samples model=import-pending
[river][voice] detector policy: migrate original model first, defer pruning/quantization until measured RAM/flash/latency pressure appears
[river] local_detector=silero_vad
```

Expected behavior:
- The project still boots without changing the current audio debug path.
- Detector information is now explicit in boot logs and status output.
- No real VAD gating is active in this step yet.

## Step 4.3
Repository-side checks:
```bash
cd /root/ameba-river
sha256sum third_party/silero_vad/upstream/silero_vad_16k_op15.onnx
git -C /tmp/silero-vad-upstream rev-parse HEAD
```

Expected result:
- vendored ONNX checksum is `7ed98ddbad84ccac4cd0aeb3099049280713df825c610a8ed34543318f1b2c49`
- pinned upstream commit is `0dd0d85ee86b1f9d178dc26a04e60e90de26a80f`

Boot-time expectation after rebuild:
```text
[river][voice] detector backend: silero_vad staged runtime=tflite_micro feed=256 samples window=512 samples context=64 samples model=silero_vad_16k_op15.onnx import=pending
```
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

## Step 3.4
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
[river][voice] preproc afe: mode=asr aec=off ns=off agc=on(fixed=10dB) ssl=on ref=staged-off
[river][voice] audio echo config: 16000 Hz capture dual-mic -> ASR-AFE 1ch -> 16000 Hz playback dual-mono, 1000 ms delay, AMIC1+AMIC3 -> speaker
[river][voice] audio echo ref: backend=playback_ring source=post-delay mono history=1536ms aec=staged-off
```

Expected diagnostics:
```text
[river][voice][diag] cap_peak=[..., ...] afe_peak=... play_peak=[..., ...] read_ok=... proc_ok=... write_ok=... ref_read_ok=0 ref_read_miss=0 ref_write_ok=0 ref_write_fail=0 read_fail=... proc_fail=... write_fail=...
```

Manual check:
- First speak at `20-30 cm`
- Then speak again at `0.5-1.0 m`
- Compare with the previous `COM/AEC` round:
  - whether far-field speech stays fuller and less over-suppressed
  - whether near-field speech remains stable enough for future KWS
  - whether replay hiss increases, which is acceptable within reason for this ASR-oriented phase

Interpretation:
- far-field speech becomes fuller or more natural:
  - the `ASR-first` pivot is moving in the right direction
- near-field is stable but replay hiss rises:
  - acceptable for this phase; do not rush back to `COM/AEC`
- speech becomes obviously worse at both near and far distance:
  - revisit the active ASR tuning before adding `VAD/KWS`

## Step 4.1
Build and flash:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
python3 /root/ameba-rtos-1.2/ameba.py flash -p /dev/ttyUSB0 -b 1500000 -m nor
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Boot-time expectation:
```text
[river][voice] frontend init: asr-first
[river][voice] preproc backend: aivoice_afe ... profile=asr_barge_in_aec
[river][voice] preproc afe: mode=asr aec=on ns=off agc=on(fixed=10dB) ssl=on ref=playback_ring(1ch)
[river][voice] detector backend: pending (silero_vad planned)
[river] local_preproc_profile=asr_barge_in_aec
```

Manual check:
- Confirm that no `speaker_test` logs appear anymore.
- Confirm that the board still boots and starts the echo debug path normally.
- Compare the current runtime identity with earlier builds:
  - product logs should now describe `ASR-first`
  - no log should claim that SDK `VAD` is already active

Interpretation:
- Boot succeeds and the new profile/log lines appear:
  - the codebase is successfully cleaned down to the new `ASR + AEC` skeleton
- Build succeeds but old `speaker_test` logs still appear:
  - stale image or stale flashing path should be suspected first

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

## Step 4.4
Host-side reproducibility checks:
```bash
cd /root/ameba-river
python3.10 -m venv /root/ameba-river/.venv-silero-convert
/root/ameba-river/.venv-silero-convert/bin/pip install --upgrade \
  pip setuptools wheel \
  onnx==1.17.0 onnxruntime==1.20.1 onnxsim==0.4.36 onnxoptimizer==0.3.13 \
  onnx-graphsurgeon==0.5.8 sng4onnx==1.0.4 \
  tensorflow-cpu==2.19.0 tensorflow==2.19.1 tf_keras==2.19.0 \
  onnx2tf==1.28.3 ai_edge_litert==1.2.0 \
  psutil==6.1.1 h5py==3.12.1 protobuf==5.29.3 flatbuffers==25.1.24 ml_dtypes==0.5.1
```

Inspect the vendored official ONNX:
```bash
cd /root/ameba-river
source /root/ameba-river/.venv-silero-convert/bin/activate
python tools/silero_vad/extract_onnx_manifest.py \
  --input third_party/silero_vad/upstream/silero_vad_16k_op15.onnx \
  --output third_party/silero_vad/upstream/silero_vad_16k_op15_manifest.json
```

Expected host-side findings:
- ONNX inputs:
  - `input`
  - `state`
  - `sr`
- ONNX outputs:
  - `output`
  - `stateN`
- top-level initializers include:
  - `model.stft.forward_basis_buffer`
  - `model.encoder.*`
  - `model.decoder.decoder.2.*`

Direct conversion probe:
```bash
cd /root/ameba-river
source /root/ameba-river/.venv-silero-convert/bin/activate
onnx2tf \
  -i third_party/silero_vad/upstream/silero_vad_16k_op15.onnx \
  -o /tmp/silero_vad_16k_op15_tflite_576 \
  -b 1 \
  -ois input:1,576 state:2,1,128 \
  -coion
```

Expected current result:
- conversion is still expected to fail
- first failure point should be `wa/model/stft/Conv`
- after graph-specific transpose repair, the next failure point should move to `wa/model/decoder/Squeeze`
- this failure is a graph-layout problem, not a target-memory problem

Protect the vendored source before any future conversion:
```bash
cd /root/ameba-river
source /root/ameba-river/.venv-silero-convert/bin/activate
python tools/silero_vad/stage_conversion_source.py \
  --input third_party/silero_vad/upstream/silero_vad_16k_op15.onnx \
  --output /tmp/silero_vad_16k_op15.stage.onnx
```

Extract reconstruction-oriented tensor metadata:
```bash
cd /root/ameba-river
source /root/ameba-river/.venv-silero-convert/bin/activate
python tools/silero_vad/extract_reconstruction_tensors.py \
  --input third_party/silero_vad/upstream/silero_vad_16k_op15.onnx \
  --output third_party/silero_vad/upstream/silero_vad_16k_op15_reconstruction_manifest.json
```

Expected current result:
- the staged source copy should report the same sha256 as the vendored official ONNX
- the reconstruction manifest should include:
  - `model.stft.forward_basis_buffer`
  - `model.encoder.*`
  - `model.decoder.rnn.*`
  - `model.decoder.decoder.2.*`
  - `decoder.lstm.W`
  - `decoder.lstm.R`
  - `decoder.lstm.B`

Rebuild and verify the batch=`1` TensorFlow/TFLite artifact:
```bash
cd /root/ameba-river
source /root/ameba-river/.venv-silero-convert/bin/activate
python tools/silero_vad/rebuild_tf_silero_vad.py \
  --input third_party/silero_vad/upstream/silero_vad_16k_op15.onnx \
  --verification-output third_party/silero_vad/upstream/silero_vad_16k_tf_rebuild_verification.json \
  --verify-cases 4 \
  --seed 8730 \
  --tflite-output third_party/silero_vad/generated/silero_vad_16k_b1_fp32.tflite
```

Expected current result:
- verification report should show:
  - output max abs diff around `1e-08`
  - state max abs diff around `1e-06`
- generated artifact should exist:
  - `third_party/silero_vad/generated/silero_vad_16k_b1_fp32.tflite`
- generated artifact checksum should be:
  - `5a532943646b1dd71930fb02e26e0600ba97ee80990302726294aef8a3142a05`

## Step 4.11
Rebuild the latest detector-compatibility image:
```bash
cd /root/ameba-river
source env.sh
CCACHE_DISABLE=1 ameba.py build -p
```

Flash the project-owned NOR profile and monitor boot:
```bash
cd /root/ameba-river
source env.sh
python3 tools/river_flash.py -p /dev/ttyUSB0
ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Expected current result:
- boot should still report:
  - `detector backend: silero_vad runtime=tflite_micro`
- boot should no longer stop at:
  - `silero_vad tensor binding failed`
- next expected milestone is:
  - `silero_vad runtime ready: model=silero_vad_16k_b1_fp32.tflite ...`

If the detector still fails, the serial log should now include additional tensor buffer details:
- `silero_vad input[...] data=... bytes=...`
- `silero_vad output[...] data=... bytes=...`

Interpretation:
- `data != NULL` and `bytes` large enough:
  - detector should now be able to bind without tensor shape metadata
- `data == NULL` for one or more tensors:
  - next suspect is allocation / arena pressure rather than tensor ordering

## Step 4.12
Rebuild the latest fallback-buffer image:
```bash
cd /root/ameba-river
source env.sh
CCACHE_DISABLE=1 ameba.py build -p
```

Flash and monitor:
```bash
cd /root/ameba-river
source env.sh
python3 tools/river_flash.py -p /dev/ttyUSB0
ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Expected current result:
- boot should report:
  - `detector backend: silero_vad runtime=tflite_micro`
- boot should now advance past:
  - `silero_vad tensor binding failed`
- next expected milestone is:
  - `silero_vad runtime ready: model=silero_vad_16k_b1_fp32.tflite ...`

Interpretation:
- if `silero_vad runtime ready` appears:
  - the remaining `RTL8730E` SDK `TFLite Micro` blocker was missing top-level I/O buffers, now patched in firmware
- if binding still fails:
  - collect the next `silero_vad` log block
  - the problem is no longer tensor order or missing metadata, and is more likely in arena layout or invoke-time buffer ownership

## Step 4.13
Rebuild the relaxed-eval-guard image:
```bash
cd /root/ameba-river
source env.sh
CCACHE_DISABLE=1 ameba.py build -p
```

Flash and monitor:
```bash
cd /root/ameba-river
source env.sh
python3 tools/river_flash.py -p /dev/ttyUSB0
ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Expected current result:
- boot should now advance past:
  - `silero_vad tensor binding failed`
- next expected milestone remains:
  - `silero_vad runtime ready: model=silero_vad_16k_b1_fp32.tflite ...`

Interpretation:
- if runtime now opens:
  - the remaining blocker was the open-time eval-tensor guard, not model I/O binding
- if open still fails:
  - collect the next `silero_vad` block
  - the next suspect becomes invoke-time behavior or arena pressure, not tensor metadata or top-level buffers

## Step 4.14
Rebuild the eval-optional image:
```bash
cd /root/ameba-river
source env.sh
CCACHE_DISABLE=1 ameba.py build -p
```

Flash and monitor:
```bash
cd /root/ameba-river
source env.sh
python3 tools/river_flash.py -p /dev/ttyUSB0
ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Expected current result:
- boot may optionally print:
  - `silero_vad eval tensor state degraded: ...`
- detector open should no longer fail solely because eval tensors are incomplete
- next expected milestone remains:
  - `silero_vad runtime ready: model=silero_vad_16k_b1_fp32.tflite ...`

## Step 4.15
Rebuild the no-type-compatible image:
```bash
cd /root/ameba-river
source env.sh
CCACHE_DISABLE=1 ameba.py build -p
```

Flash and monitor:
```bash
cd /root/ameba-river
source env.sh
python3 tools/river_flash.py -p /dev/ttyUSB0
ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Expected current result:
- boot should no longer fail solely because top-level tensors print:
  - `type=0`
- next expected milestone is:
  - `silero_vad runtime ready: model=silero_vad_16k_b1_fp32.tflite ...`

Interpretation:
- if `runtime ready` appears:
  - this SDK stores top-level tensor type metadata in a degraded but still usable form, and detector open has been made compatible
- if open still fails:
  - collect the next `silero_vad` block
  - the next suspect shifts to invoke-time behavior or arena pressure rather than top-level tensor metadata

## Step 4.16
Rebuild the RGB-indicator image:
```bash
cd /root/ameba-river
source env.sh
CCACHE_DISABLE=1 ameba.py build -p
```

Flash and monitor:
```bash
cd /root/ameba-river
source env.sh
python3 tools/river_flash.py -p /dev/ttyUSB0
ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Expected boot log:
- the existing `Silero VAD` runtime should still reach:
  - `silero_vad runtime ready: model=silero_vad_16k_b1_fp32.tflite ...`
- a new board-side log should appear once:
  - `rgb indicator ready: ws2812 ledc cpu pin=PA_9 silence=blue speech=green error=red`

Expected LED behavior:
- just after boot:
  - amber during bring-up
- once the voice loop is running but no speech is detected:
  - blue
- while speech is detected:
  - green
- if detector open or runtime start fails:
  - red

Interpretation:
- if serial logs are healthy but the RGB LED never changes:
  - current `PA_9 + WS2812` assumption is likely wrong for this exact board population
- if blue / green switching follows speech roughly in step with `vad=silence/speech`:
  - board-side visual VAD indication is confirmed and ready for later wake-word / ASR state extension
