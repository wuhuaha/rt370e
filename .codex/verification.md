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

## Step 4.21
Build and flash:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py soc RTL8730E
CCACHE_DISABLE=1 python3 /root/ameba-rtos-1.2/ameba.py build -p
python3 tools/river_flash.py -p /dev/ttyUSB0
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Boot-time expectation:
```text
[river][voice] preproc backend: ... profile=asr_mainline
[river][voice] detector backend: silero_vad ... enter_q15=9000 exit_q15=2500 hangover=10 ema_shift=1 ...
[river][voice] detector reference: aivoice_vad_v1 diagnostic-only ...
[river][voice] boot vad probe diagnostics enabled
[river][voice] boot vad probe autostart enabled
[river][voice] vad probe segment buffer: pre=384ms post=768ms max=8000ms
[river][voice] segment sink: online_asr_stub ...
[river][voice] vad probe started
[river] audio_echo=stopped
[river] audio_vad_probe=running
```

Runtime expectation:
```text
[river][voice][probe] ... vad_raw_q15=... vad_prob_q15=... vad=speech|silence vad_start=... vad_end=... sdk_vad=... seg=active|ready|idle seg_pre_ms=... seg_post_left_ms=... seg_active_ms=... seg_ready_ms=...
[river][voice][segment] ready: bytes=... ms=... pre=384ms post=768ms completed=... dropped=...
```

Manual checks:
- Stay silent for `3-5s` and confirm `vad=silence` dominates, while `sdk_vad` also mostly stays `silence`.
- Speak short Chinese phrases such as `打开客厅灯` and `关闭风扇`.
- Confirm short utterances still appear in the high-frequency probe logs.
- Confirm at least one `[river][voice][segment] ready: ...` line appears after speech ends.

Interpretation:
- `vad_raw_q15` rises but `vad_prob_q15` stays low:
  - smoothing or decision policy is still too conservative
- `Silero` and `sdk_vad` both stay low:
  - inspect shared capture / AFE path before suspecting the model
- `Silero` triggers often but `sdk_vad` never does:
  - current `Silero` thresholds are more recall-oriented, so some extra false positives are expected
- `segment] ready` never appears:
  - post-roll or segment-buffer path is not finalizing correctly

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

## Step 5.0
Build and flash:
```bash
cd /root/ameba-river
source env.sh
CCACHE_DISABLE=1 python3 /root/ameba-rtos-1.2/ameba.py build -p
python3 tools/river_flash.py -p /dev/ttyUSB0
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Boot-time expectation:
```text
[river][wifi] autoconnect init: ssid=Keeu retry_ms=5000
[river][cloud] sntp init: server=pool.ntp.org interval_ms=3600000
[river][cloud] online asr provider init: iflytek_rtasr stream=yes batch=no
[river][voice] detector backend: silero_vad ...
[river][voice] vad probe started
[river] local_segment_sink=cloud_asr_batch_bridge
[river][cloud] asr provider=iflytek_rtasr stream=yes batch=no ...
```

Expected runtime path:
- streaming path:
  - `vad_probe -> river_cloud_asr_stream_push_frame() -> iflytek_rtasr`
- non-streaming path:
  - `segment_buffer ready -> river_voice_segment_sink_submit() -> river_cloud_asr_batch_submit_segment()`

Expected Wi-Fi behavior:
```text
[river][wifi] connect ssid=Keeu attempt=1
[river][wifi] connected ssid=Keeu ip=...
```

Expected cloud behavior when UTC and Wi-Fi are ready and speech arrives:
```text
[river][cloud] asr bridge open: provider=iflytek_rtasr 16000Hz/1ch/16bit frame=16ms pre=384ms post=768ms
[river][cloud][iflytek] stream open: 16000Hz/1ch/16bit seq=1
[river][asr][iflytek_rtasr] session started sid=...
[river][asr][iflytek_rtasr] partial sid=... text=...
[river][asr][iflytek_rtasr] final sid=... text=...
[river][asr][iflytek_rtasr] session closed sid=...
```

Probe diagnostics should now also expose cloud-side counters:
```text
[river][voice][probe] ... cloud_stream_ok=... cloud_stream_busy=... cloud_stream_fail=... seg_unsupported=... seg_fail=...
```

Interpretation:
- `cloud_stream_busy` increases while Wi-Fi is not connected:
  - local VAD path is working, but network is not yet ready
- `cloud_stream_busy` increases while Wi-Fi is connected but UTC is not ready:
  - SNTP has not completed yet, so signed RTASR URL generation is intentionally deferred
- `cloud_stream_fail` increases:
  - provider open/send/poll path needs inspection
- `seg_unsupported` increases:
  - expected for the current iFlytek provider, because only streaming is implemented
- no `[river][asr][iflytek_rtasr] ...` lines appear even though `cloud_stream_ok` rises:
  - inspect WebSocket callback / server response parsing first

Current validation gap:
- local build is complete and board image is generated
- end-to-end live-service verification still requires the target board to reach the public iFlytek service
- this repository-side environment does not verify external network traffic by itself

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

## Step 4.17
Rebuild the stabilized-Silero image:
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

Expected boot log changes:
- detector profile should now print:
  - `enter_q15=12000`
  - `exit_q15=4500`
  - `hangover=8`
  - `ema_shift=2`
- RGB should no longer claim ready; instead it should print a deferred warning:
  - `rgb indicator deferred: EV8730EA2 USER LED is passive RGB ...`

Expected runtime effect:
- during continuous speech, `vad=speech` should persist more steadily instead of dropping back to `silence` on brief probability dips
- the printed `vad_prob_q15` should now track the smoothed decision probability instead of the raw per-window output

Interpretation:
- if speech still drops to `silence` too often while talking continuously:
  - further tuning should focus on:
    - lower enter threshold
    - larger hangover
    - or detector-side cache / input normalization checks
- if RGB remains deferred:
  - next board step is hardware confirmation of:
    - `R25`, `R27`, `R31`
    - actual `LEDR/LEDG/LEDB` GPIO mapping

## Step 4.18
Rebuild the dual-reference detector image:
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

Expected boot log changes:
- detector profile should now print:
  - `enter_q15=12000`
  - `exit_q15=4500`
  - `hangover=8`
  - `ema_shift=2`
- detector reference profile should now print:
  - `detector reference: aivoice_vad_v1 diagnostic-only ...`
- RGB should print a deferred warning instead of `rgb indicator ready`

Expected runtime diagnostics:
- each `[river][voice][diag]` line should now contain:
  - `vad_raw_q15=...`
  - `vad_prob_q15=...`
  - `sdk_vad=speech|silence|disabled`
  - `sdk_events=...`
  - `sdk_start=...`
  - `sdk_end=...`
  - `sdk_offset_ms=...`

Interpretation:
- if `vad_raw_q15` is high while `vad_prob_q15` stays low:
  - the smoothing / hysteresis policy is too conservative
- if both `Silero` and `sdk_vad` mostly stay silent while speaking:
  - first suspect the shared input chain, not only the `Silero` model
- if `Silero` stays mostly silent while `sdk_vad` toggles normally:
  - focus on `Silero` threshold / stream framing / state handling
- if RGB remains dark and boot prints the deferred message:
  - that is expected until the actual `LEDR/LEDG/LEDB` mapping is implemented

## Step 4.20
Rebuild the pure VAD probe image:
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

Expected boot log changes:
- preproc profile should switch to:
  - `profile=asr_mainline`
  - `aec=off`
- boot should print:
  - `boot vad probe diagnostics enabled`
  - `boot vad probe autostart enabled`
  - `vad probe config: ... detector-only ... diag_window~240ms`
- app status should show:
  - `audio_vad_probe=running`
  - `audio_echo=stopped`

Expected runtime diagnostics:
- logs should move from:
  - `[river][voice][diag] ...`
- to:
  - `[river][voice][probe] ...`
- every log line should still include:
  - `vad_raw_q15`
  - `vad_prob_q15`
  - `vad=speech|silence`
  - `sdk_vad=speech|silence|disabled`
  - `sdk_events/sdk_start/sdk_end/sdk_offset_ms`

Interpretation:
- if short utterances are still missing entirely:
  - inspect whether the issue is already visible in `vad_raw_q15`
- if `Silero` and SDK VAD both improve noticeably in pure probe mode:
  - previous instability was mainly caused by the `AEC + playback` validation path
- if `Silero` remains much less stable than SDK VAD on the same probe stream:
  - continue tuning `Silero` thresholds / smoothing / stream-state handling

## Step 4.22
Build the logging-layer update:
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

Expected runtime behavior:
- boot and service lifecycle logs remain visible at `INFO`
- high-rate VAD probe lines are suppressed at the default log level
- VAD state changes still print because they are now emitted at `INFO`
- every project-owned log line should now include:
  - a millisecond timestamp
  - a level marker
  - a stable module tag

Expected VAD behavior at default `INFO`:
- logs should not spam every `~96ms`
- only transitions such as:
  - `vad state=speech ...`
  - `vad state=silence ...`
  should remain visible

Expected VAD behavior at `DEBUG`:
- periodic probe lines should still be available for deep tuning
- segment-ready diagnostics and tensor-inventory style details should also remain available

## Step 4.23
Build the heap-guarded SDK VAD reference update:
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

Expected runtime behavior when online ASR + Silero are active and heap is tight:
- `Silero VAD` still reaches:
  - `silero_vad runtime ready: ...`
- the optional SDK comparison path may now log either:
  - normal profile information, if heap is sufficient
  - or:
    - `sdk_vad reference skipped: free_heap=... min_required=... create_scratch~256064B`
    - `sdk_vad reference auto-disabled; keep silero-only decision logging`
- the previous vendor-side crash-style line should disappear:
  - `Malloc failed. Core:[CA32], Task:[NoTsk], [free heap size: ...] [xWantedSize:256064]`

Expected behavior after the fix:
- pure VAD probe continues running
- online ASR bridge still opens
- SDK VAD reference becomes opportunistic instead of mandatory

## Step 4.24
Build the batch-segment heap hardening update:
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

Expected runtime behavior with `iflytek_rtasr`:
- `Silero VAD` still reaches:
  - `silero_vad runtime ready: ...`
- the cloud ASR audio bridge still opens:
  - `asr bridge open: provider=iflytek_rtasr ...`
- `vad_probe` now reports the batch segment path as disabled instead of attempting a large allocation:
  - `vad probe segment buffer disabled: provider=iflytek_rtasr batch=no stream-only bridge active`
  - or, for a future batch-capable provider with low heap:
    - `vad probe segment buffer skipped: free_heap=... required~... headroom=... provider=... batch=yes`
- the low-level allocator failure should disappear:
  - `Malloc failed. Core:[CA32], Task:[NoTsk], [free heap size: ...] [xWantedSize:256064]`

## Step 4.25
Build the Wi-Fi connect fallback update:
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

Expected runtime behavior:
- `river_wifi_station` should first log a basic connect attempt:
  - `connect strategy=basic ssid=Keeu ...`
- If that fails, and scan data is available, it should retry with a scan-bound attempt:
  - `connect strategy=scan_exact ...`
- If the target AP is `WPA2/WPA3 mixed`, a final compatibility fallback may appear:
  - `connect strategy=scan_wpa2_fallback ...`
- Failures should now include both the strategy and decoded join status:
  - `connect strategy=... failed err=...(<name>) join=<status>`
- Between attempts, the app should no longer hammer the driver immediately after a failed join; it now disconnects and waits for the join state to settle first.
- SDK fast-connect should also be disabled during bring-up:
  - `sdk fast connect disabled; river owns initial connect policy`
- If `wifi_connect()` reports `busy` while the SDK is already progressing a join, the app should now wait and adopt that connection instead of immediately disconnecting it:
  - `connect strategy=... busy; wait existing join flow`
- If the driver reaches `RTW_JOINSTATUS_SUCCESS` first and only DHCP is pending, the app should request an IPv4 lease and complete the connection instead of restarting the join.

Follow-up Wi-Fi verification after startup-race hardening:
```bash
cd /root/ameba-river
source env.sh
CCACHE_DISABLE=1 ameba.py build -p
python3 tools/river_flash.py -p /dev/ttyUSB0
ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Expected additional runtime behavior:
- SDK fast-connect should now be disabled before WLAN init:
  - `sdk fast connect pre-disabled before wlan init`
- SDK LPS should now be disabled during bring-up:
  - `sdk lps disabled during bring-up`
- The STA task should not immediately launch a fresh scan/connect while the driver is still internally busy:
  - `scan busy for ssid=Keeu; wait idle and retry once`
  - or `connect wait-idle timeout before strategy=...`
- If the SSID is found by scan, the app should prefer the deterministic candidate-bound attempt first:
  - `connect strategy=scan_exact ssid=Keeu channel=<n> sec=<security> ...`
- `basic` should remain as a later fallback rather than the default first path once scan metadata exists.

## Step 5.1
Full `xiaozhi` branch build verification:
```bash
cd /root/ameba-river
git branch --show-current
git rev-parse --short HEAD

source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p

stat -c '%n %s %y' \
  build_RTL8730E/km4_boot_all.bin \
  build_RTL8730E/km0_km4_ca32_app.bin \
  build_RTL8730E/ota_all.bin
```

Expected result:
- current branch is `xiaozhi`
- HEAD is `43737ec`
- build finishes with `Build done`
- image artifacts exist and are non-zero; on this run:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3605856`
  - `build_RTL8730E/ota_all.bin 3605888`

## Step 5.2
Migration assessment evidence collection:
```bash
cd /root/ameba-river
git branch --show-current
git status --short

sed -n '1,220p' /root/kws-training-pro/README.md
sed -n '1,220p' /root/kws-training-pro/model_dscnn.py
sed -n '1,260p' /root/kws-training-pro/train_dscnn_v2.py
sed -n '1,220p' /root/kws-training-pro/river_kws_features.py
sed -n '1,220p' /root/kws-training-pro/validate_final.py
sed -n '1,240p' /root/kws-training-pro/configs/training_config.yaml
sed -n '1,260p' /root/kws-training-pro/DOCS_QUANT_DEPLOY.md
sed -n '1,240p' /root/ameba-river/components/river_voice/river_voice_kws.cc
sed -n '820,1075p' /root/ameba-river/components/river_voice/river_voice_kws.cc
sed -n '1,220p' /root/ameba-river/components/river_voice/river_voice_frontend.c
sed -n '1,240p' /root/ameba-river/KWS_PIPELINE_ZH.md
rg -n "CONFIG_RIVER_KWS_TENSOR_ARENA_KB|CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15|CONFIG_RIVER_KWS_TRIGGER_HOLD_FRAMES|CONFIG_RIVER_KWS_COOLDOWN_MS|CONFIG_RIVER_KWS_INFERENCE_STRIDE_FRAMES|CONFIG_RIVER_KWS_VAD_PRE_ROLL_MS" /root/ameba-river/prj.conf -S
find /root/kws-training-pro/models -maxdepth 3 -type f \( -name '*.onnx' -o -name '*.tflite' -o -name '*.pth' \) -printf '%P\t%s\n' | sort
```

Expected review outcome:
- current branch is `DS-CNN`
- worktree remains clean except the user-owned untracked `.env`
- evidence shows two distinct student paths in `/root/kws-training-pro`:
  - legacy `40x101`
  - newer `40x98`
- evidence also shows the current board contract in `ameba-river` is `98x40`, VAD-gated, streaming, and not equivalent to the legacy path
- the final written conclusion is captured in:
  - `DSCNN_KWS_TRAINING_PRO_MIGRATION_REPORT_ZH.md`

## Step 5.3
OpenWakeWord external lab migration assessment evidence collection:
```bash
cd /root/ameba-river
git branch --show-current
git status --short

sed -n '1,240p' /root/river-openwakeword-lab/tools/openwakeword/README.md
sed -n '1,260p' /root/river-openwakeword-lab/tools/openwakeword/river_kws_features.py
sed -n '1,640p' /root/river-openwakeword-lab/tools/openwakeword/train_xiaou_student_dscnn.py
sed -n '1,640p' /root/river-openwakeword-lab/tools/openwakeword/export_xiaou_student_tflite.py
sed -n '1,320p' /root/river-openwakeword-lab/tools/openwakeword/extract_xiaou_student_features.py
sed -n '1,320p' /root/river-openwakeword-lab/tools/openwakeword/extract_xiaou_teacher_features.py
sed -n '1,260p' /root/river-openwakeword-lab/tools/openwakeword/eval_xiaou_guanjia.py

cat /root/river-openwakeword-lab/artifacts/openwakeword/xiaou_student_round6_baseline/training/xiaou_student_round6_baseline_report.json
cat /root/river-openwakeword-lab/artifacts/openwakeword/xiaou_student_round6_targeted/training/xiaou_student_round6_targeted_report.json
cat /root/river-openwakeword-lab/artifacts/openwakeword/xiaou_student_round6_targeted/export/xiaou_student_round6_targeted_int8_export_report.json
cat /root/river-openwakeword-lab/artifacts/openwakeword/xiaou_teacher_round5/eval/board_eval_corrected_report.json
find /root/river-openwakeword-lab/artifacts/openwakeword/xiaou_teacher_round6 -maxdepth 3 -type f | sort

rg -n "assistant|soft target|KD|distill" /root/river-openwakeword-lab/tools/openwakeword /root/river-openwakeword-lab/docs -S
ls -1 /root/river-openwakeword-lab/tools/openwakeword/train_xiaou_teacher_assistant.py /root/river-openwakeword-lab/tools/openwakeword/train_xiaou_student_verifier.py /root/river-openwakeword-lab/tools/openwakeword/eval_xiaou_student.py 2>/dev/null || true

sed -n '590,640p' /root/ameba-river/components/river_voice/river_voice_kws.cc
```

Expected review outcome:
- current branch is `DS-CNN`
- worktree remains clean except the user-owned untracked `.env`
- evidence shows `/root/river-openwakeword-lab` already has a board-aligned `98x40` student feature path plus reproducible int8 export
- evidence also shows the current best `round6` student is still `no-deploy` because board negative FPR remains too high
- evidence shows the exported student currently contains `PAD`, while the current branch resolver does not yet register `AddPad()`
- the final written conclusion is captured in:
  - `RIVER_OPENWAKEWORD_LAB_MIGRATION_REPORT_ZH.md`

## Step 5.4
Plan update and full build verification:
```bash
cd /root/ameba-river
sed -n '1,220p' plan.md

source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p

ls -l build_RTL8730E/km4_boot_all.bin \
      build_RTL8730E/km0_km4_ca32_app.bin \
      build_RTL8730E/ota_all.bin

git status --short
```

Expected result:
- `plan.md` now makes full build verification the first gate on branch `DS-CNN`
- build finishes with `Build done`
- current top-level artifacts exist with the verified sizes:
  - `build_RTL8730E/km4_boot_all.bin`: `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin`: `3605856`
  - `build_RTL8730E/ota_all.bin`: `3605888`
- `git status --short` shows only:
  - tracked doc changes from this step
  - the user-owned untracked `.env`

## Step 5.5
Build result review:
```bash
cd /root/ameba-river
git branch --show-current
git status --short

git show 10fec1e:.codex/changes.md | tail -n 20
ls -l build_RTL8730E/km4_boot_all.bin \
      build_RTL8730E/km0_km4_ca32_app.bin \
      build_RTL8730E/ota_all.bin

python3 - <<'PY'
app_size=3605856
start=0x08040000
end=start+app_size
limit=0x08300000
print(hex(start), hex(end))
print('fits_sdk_stock', end <= limit)
print('overflow_bytes', max(0, end - limit))
PY

sed -n '1,80p' board/rtl8730e/profiles/RTL8730E_NOR.json
sed -n '1,80p' board/rtl8730e/profiles/RTL8730E_NOR.sdk.json

sed -n '596,618p' components/river_voice/river_voice_kws.cc
git diff --stat xiaozhi..DS-CNN
sed -n '1,220p' plan.md
```

Expected result:
- current branch is `DS-CNN`
- worktree remains clean except the user-owned `.env` before this step's tracked doc edits
- current image sizes match the last verified `xiaozhi` baseline exactly:
  - `km4_boot_all.bin`: `51872`
  - `km0_km4_ca32_app.bin`: `3605856`
  - `ota_all.bin`: `3605888`
- app placement does **not** fit the SDK stock app range:
  - start `0x08040000`
  - end `0x083B0560`
  - overflow `722272` bytes beyond stock `0x08300000`
- project profile still expands the app range to `0x08600000`, so flashing remains valid only with the project-owned profile/tooling
- current `river_voice_kws.cc` resolver still registers only the existing `6` ops and does not register `AddPad()`
- `git diff --stat xiaozhi..DS-CNN` shows documentation-only divergence, confirming no new runtime source delta has been introduced on this branch

## Step 5.6
Phase 2 runtime landing:
```bash
cd /root/ameba-river

python3 /root/river-openwakeword-lab/tools/openwakeword/export_embedded_model.py \
  --tflite-model /root/river-openwakeword-lab/artifacts/openwakeword/xiaou_student_round6_targeted/export/xiaou_student_round6_targeted_int8.tflite \
  --output-dir /root/ameba-river/components/river_voice/generated \
  --model-name xiaou_student_round6_targeted_int8 \
  --symbol-name kws_model_round6_targeted \
  --primary-threshold 0.65 \
  --notes "Round6 targeted DS-CNN student experimental embedded variant for DS-CNN branch"

source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p

ls -l build_RTL8730E/km4_boot_all.bin \
      build_RTL8730E/km0_km4_ca32_app.bin \
      build_RTL8730E/ota_all.bin \
      components/river_voice/generated/xiaou_student_round6_targeted_int8_model_data.h \
      components/river_voice/generated/xiaou_student_round6_targeted_int8_metadata.json

python3 - <<'PY'
import os
app_size = os.path.getsize('/root/ameba-river/build_RTL8730E/km0_km4_ca32_app.bin')
start = 0x08040000
end = start + app_size
limit = 0x08300000
print('app_size', app_size)
print('app_end', hex(end))
print('overflow_bytes_vs_sdk_stock', max(0, end - limit))
PY

strings build_RTL8730E/km0_km4_ca32_app.bin | rg -n "round6_targeted_experimental|kws backend: dscnn runtime=tflite_micro" -S
sed -n '1,200p' components/river_voice/generated/xiaou_student_round6_targeted_int8_metadata.json
git status --short
```

Expected result:
- generated embedded assets exist for the imported round6 targeted student
- build finishes with `Build done`
- `prj.conf` selects `CONFIG_RIVER_KWS_MODEL_VARIANT_ROUND6_TARGETED_EXPERIMENTAL=y`
- `components/river_voice/river_voice_kws.cc` now supports `PAD` and model-variant selection
- final image sizes are:
  - `km4_boot_all.bin`: `51872`
  - `km0_km4_ca32_app.bin`: `3573088`
  - `ota_all.bin`: `3573120`
- the app image still exceeds stock SDK range but by the reduced amount:
  - `overflow_bytes_vs_sdk_stock = 689504`
- `strings` confirms the landed runtime now contains `round6_targeted_experimental`
- `git status --short` after staging/commit prep shows only this step's tracked changes plus the user-owned `.env`

## Step 5.7
Repository Markdown reorganization:
```bash
cd /root/ameba-river

find . -maxdepth 1 -type f -name '*.md' | sort
find doc -maxdepth 1 -type f -name '*.md' | sort

sed -n '1,220p' README.md
sed -n '1,220p' plan.md
sed -n '1,220p' build.md
sed -n '1,220p' doc/README.md
sed -n '1,220p' doc/PROJECT_STATUS_ZH.md

rg -n 'doc/DSCNN_KWS_TRAINING_PRO_MIGRATION_REPORT_ZH.md|doc/RIVER_OPENWAKEWORD_LAB_MIGRATION_REPORT_ZH.md' plan.md
git status --short
```

Expected result:
- repository root keeps only the ongoing entry Markdown files:
  - `AGENTS.md`
  - `README.md`
  - `REVIEW.md`
  - `TIPS.md`
  - `build.md`
  - `plan.md`
- `doc/` exists and contains the relocated summary/report/design Markdown files
- `README.md` reflects the current `DS-CNN` branch rather than the old `xiaozhi`-focused baseline
- `README.md` points readers to `doc/README.md` and the key migrated reports
- `plan.md` references the migration reports under `doc/`
- `build.md` clearly states that the current branch still requires the project custom flash profile
- `doc/PROJECT_STATUS_ZH.md` reflects the present `DS-CNN` runtime/build state
- `git status --short` shows only this step's tracked doc moves/edits plus the user-owned `.env`

## Step 5.8
Debug-path compile gating for wake -> XiaoZhi runtime:
```bash
cd /root/ameba-river

sed -n '1,220p' Kconfig
sed -n '1,120p' prj.conf
sed -n '1,220p' components/river_core/CMakeLists.txt
sed -n '1,220p' components/river_core/river_interaction_diag_stub.c
sed -n '1,260p' components/river_cloud/river_online_control.c
sed -n '1,260p' components/river_diag/river_diag_cmd.c

source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p

ls -l build_RTL8730E/km4_boot_all.bin \
      build_RTL8730E/km0_km4_ca32_app.bin \
      build_RTL8730E/ota_all.bin

ls -lh \
  build_RTL8730E/build/project_ap/make/image2/example/ameba-river/components/river_core/CMakeFiles/river_core_target_img2_ap.dir/river_interaction_diag_stub.o \
  build_RTL8730E/build/project_ap/make/image2/example/ameba-river/components/river_diag/CMakeFiles/river_diag_target_img2_ap.dir/river_diag_cmd.o \
  build_RTL8730E/build/project_ap/make/image2/example/ameba-river/components/river_cloud/CMakeFiles/river_cloud_target_img2_ap.dir/river_online_control.o

strings build_RTL8730E/km0_km4_ca32_app.bin | rg -n "interaction_diag=compiled=no|online control service init: text_debug=%s|devices: text_debug=%s|round6_targeted_experimental" -S
git status --short
```

Expected result:
- `Kconfig` contains:
  - `RIVER_CLOUD_TEXT_DEBUG_EN`
  - `RIVER_INTERACTION_DIAG_EN`
- `prj.conf` explicitly sets:
  - `CONFIG_RIVER_CLOUD_TEXT_DEBUG_EN=n`
  - `CONFIG_RIVER_INTERACTION_DIAG_EN=n`
- `components/river_core/CMakeLists.txt` selects `river_interaction_diag_stub.c` when interaction diag is disabled
- `components/river_diag/river_diag_cmd.c` compile-gates:
  - `river echo`
  - `river tts`
  - `river interaction ...`
- full build finishes with `Build done`
- current image sizes are:
  - `km4_boot_all.bin` = `51872`
  - `km0_km4_ca32_app.bin` = `3564896`
  - `ota_all.bin` = `3564928`
- representative objects show the expected reduction:
  - `river_interaction_diag_stub.o` about `7.8K`
  - `river_diag_cmd.o` about `29K`
- final firmware strings still include:
  - `round6_targeted_experimental`
  - `interaction_diag=compiled=no`
  - `online control service init: text_debug=%s`
- `git status --short` shows only this step's tracked source/doc edits plus the user-owned `.env`

## Step 5.9
Check for legacy `plan.md.bk` backup:
```bash
cd /root/ameba-river

ls -l plan.md.bk plan.md
find /root/ameba-river -name 'plan.md.bk' -o -name '*.bk' | sort
git log --all --name-only -- plan.md.bk
git status --short
```

Expected result:
- `plan.md` exists
- `plan.md.bk` does not exist at repository root
- repository-wide search returns no `plan.md.bk`
- `git log --all --name-only -- plan.md.bk` returns no tracked history for that file
- current authoritative plan remains `plan.md`
- `git status --short` remains clean except the user-owned `.env`

## Step 5.10
Rebuild the firmware with the baseline embedded KWS model selected:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
```

Optional local build checks:
```bash
cd /root/ameba-river
rg -n "RIVER_KWS_MODEL_VARIANT_(BASELINE|ROUND6_TARGETED_EXPERIMENTAL)" \
  build_RTL8730E/build/project_ap/.config_ca32 \
  build_RTL8730E/build/project_hp/.config_km4
strings -a build_RTL8730E/build/project_ap/image/target_img2.axf | rg "baseline_embedded|round6_targeted_experimental"
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Flash and run the isolation check on board:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py flash -p /dev/ttyUSB0 -b 1500000 -m nor
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Expected boot/runtime evidence:
- KWS startup log shows `variant=baseline_embedded`
- the log no longer shows `variant=round6_targeted_experimental`
- boot still shows:
  - `online control service init: text_debug=stubbed`
  - `interaction_diag=compiled=no`
  - VAD probe autostart and XiaoZhi realtime init lines

Board-side pass/fail check:
- Let the board reach the first Wi-Fi connect attempt under the same boot conditions that previously crashed.
- Watch the period around:
  - `kws gate open`
  - `connect attempt=1`
- Pass:
  - the board stays alive through Wi-Fi connect attempts and no dual `Data abort` appears
- Fail:
  - a crash still occurs with `variant=baseline_embedded`
  - if it does, collect the new abort addresses before changing any more runtime paths

Interpretation:
- Stable with `baseline_embedded`:
  - treat the round6 experimental DS-CNN model/runtime combination as the primary regression
- Still crashes with `baseline_embedded`:
  - continue investigating the general KWS runtime path or surrounding memory pressure, not XiaoZhi session logic first

## Step 5.11
Rebuild the firmware after shrinking XiaoZhi downlink playback buffering:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
```

Optional local source check:
```bash
cd /root/ameba-river
rg -n "RIVER_CLOUD_XIAOZHI_PLAYBACK_BUFFER_FRAMES|RIVER_CLOUD_XIAOZHI_PLAYBACK_BUFFER_FRAMES_FALLBACK" \
  components/river_cloud/river_cloud_internal.h
```

Expected source result:
- `RIVER_CLOUD_XIAOZHI_PLAYBACK_BUFFER_FRAMES 3U`
- `RIVER_CLOUD_XIAOZHI_PLAYBACK_BUFFER_FRAMES_FALLBACK 2U`

Flash and reproduce the same wake -> XiaoZhi -> TTS path on board:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py flash -p /dev/ttyUSB0 -b 1500000 -m nor
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Board-side check:
- Trigger wakeword and let XiaoZhi proceed into a spoken reply so downlink playback starts.
- Watch for these lines around TTS start:
  - `xiaozhi conversation window opened`
  - `asr session started`
  - `tts sid=... state=start`

Pass signals:
- TTS playback starts without `Malloc failed. Core:[CA32], Task:[river_xz_down]`
- no `xWantedSize:46144`
- ideally a new playback line appears:
  - `xiaozhi playback start: ... mode=no_ref`
  - or, if the first attempt is still too large, a retry line appears first:
    `xiaozhi playback start retry: status=... -> compact mode no_ref buffer_frames=2`

Fail signals:
- CA32 still logs malloc failure during XiaoZhi TTS start
- if it fails, record the new `xWantedSize` and the remaining free heap
- if compact fallback also fails, the next step should shrink playback buffering further or reduce the generic playback service allocation policy

## Step 5.12
Documentation verification for the refactor execution baseline:
```bash
cd /root/ameba-river
git diff --check -- doc/PROJECT_REFACTOR_EXECUTION_PLAN_ZH.md doc/README.md .codex/changes.md .codex/verification.md
sed -n '1,240p' doc/PROJECT_REFACTOR_EXECUTION_PLAN_ZH.md
sed -n '1,120p' doc/README.md
git status --short
```

Expected result:
- `git diff --check` returns no whitespace or patch-format issues
- `doc/PROJECT_REFACTOR_EXECUTION_PLAN_ZH.md` contains:
  - refactor goals
  - prioritized problem list
  - phased execution order
  - per-step delivery rules
- `doc/README.md` lists `PROJECT_REFACTOR_EXECUTION_PLAN_ZH.md` in:
  - `当前优先阅读`
  - `架构与实现`
- `git status --short` shows only this step's tracked doc updates plus the user-owned `.env`

Runtime/build note:
- This is a documentation-only step.
- No firmware build or board flash is required for this step because no runtime code changed.

## Step 5.13
Documentation verification for the root execution plan rewrite:
```bash
cd /root/ameba-river
git diff --check -- plan.md .codex/changes.md .codex/verification.md
sed -n '1,260p' plan.md
git status --short
```

Expected result:
- `git diff --check` returns no whitespace issues
- `plan.md` now contains:
  - current refactor objective
  - current baseline and guardrails
  - `Phase 0` through `Phase 5`
  - `Immediate Next Step` pointing to wake admission retry and time-ready cleanup
- `git status --short` shows only this step's tracked doc updates plus the user-owned `.env`

Runtime/build note:
- This is a documentation-only step.
- No firmware build or board flash is required for this step because no runtime code changed.

## Step 5.14
Rebuild the firmware after the wake admission retry and XiaoZhi admission-time cleanup:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully
- output images exist
- current sizes remain:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3597664`
  - `build_RTL8730E/ota_all.bin 3597696`

Flash and validate the wake-before-SNTP scenario on board:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py flash -p /dev/ttyUSB0 -b 1500000 -m nor
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Board-side validation flow:
- Boot the board and wait until:
  - Wi-Fi is still connecting or has just connected
  - `sntp ready: utc=...` has not appeared yet
- Speak the wake word once during that window.
- Continue watching the serial log without speaking the wake word again.

Pass signals:
- the log shows a held wake instead of a one-shot loss:
  - `wakeword queued ...`
  - optionally `wakeword admission deferred; retry pending ...`
- the cloud side explains the defer reason without log spam:
  - `wake admission deferred: provider=xiaozhi_realtime status=-4 wifi=... admission_time_ready=... system_time_ready=...`
- if system time is still not ready but build-seeded time is usable, the log may show:
  - `wake admission proceeding with build-seeded utc estimate`
- after the transient busy condition clears, the same wake should continue into:
  - `xiaozhi connecting: ...`
  - `Connected to websocket server`
  - `xiaozhi conversation window opened: source=wakeword ...`

Fail signals:
- the board logs one wake hit, then no retry behavior appears and XiaoZhi never connects
- the wake must be spoken a second time after Wi-Fi/time becomes ready
- `wakeword admission failed: status=...` appears for a retryable busy path

Interpretation:
- Pass:
  - the wake admission path is no longer lossy under transient Wi-Fi / time readiness conditions
- Fail:
  - if retries appear but never converge, continue by inspecting the precise busy reason in the new cloud log
  - if no retries appear, re-check the wake worker state in `river_session_coordinator.c`

## Step 5.15
Rebuild the firmware after removing per-packet heap allocation from XiaoZhi uplink websocket framing:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully
- output images exist
- current sizes remain:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3597664`
  - `build_RTL8730E/ota_all.bin 3597696`

Flash and validate the XiaoZhi wake-to-uplink path on board:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py flash -p /dev/ttyUSB0 -b 1500000 -m nor
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Board-side validation flow:
- Boot the board and let Wi-Fi connect.
- Trigger the wake word once and keep speaking for a few seconds after wake confirmation.
- Continue watching the serial log through websocket connect, ASR streaming, and the first STT result.

Pass signals:
- the transport still opens normally:
  - `xiaozhi connecting: ...`
  - `Connected to websocket server`
  - `server hello: sid=...`
- the uplink stream still starts and carries speech:
  - `asr stream active: provider=xiaozhi_realtime ...`
  - `stt sid=... text=...`
- there is no new transport regression on the uplink hot path:
  - no repeated `xiaozhi uplink send failed`
  - no `binary_payload_too_large`
  - no `binary_send_failed`

Scope note:
- This step only removes heap churn from the XiaoZhi uplink websocket framing path.
- It is not expected to fix the separate downlink/playback heap failure seen later in `river_xz_down`.

## Step 5.16
Rebuild the firmware after correcting playback-service buffer sizing:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully
- output images exist
- current sizes remain:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3597664`
  - `build_RTL8730E/ota_all.bin 3597696`

Flash and validate the XiaoZhi downlink playback path on board:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py flash -p /dev/ttyUSB0 -b 1500000 -m nor
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Board-side validation flow:
- Boot the board, let Wi-Fi connect, and trigger XiaoZhi with a wake word.
- After websocket connect succeeds, ask a short question so TTS downlink playback is exercised.
- Watch the first playback startup logs in `river_xz_down`.

Pass signals:
- playback now reports its computed budget explicitly:
  - `playback start: stream=xiaozhi_tts ... min=...B target=...B track=...B ref=yes|no`
- the final `track=` budget is no longer inflated to the old `~46080B` class caused by `minBuffer * 3`
- XiaoZhi TTS can proceed into playback without the old allocation failure:
  - no `Malloc failed. Core:[CA32], Task:[river_xz_down], ... [xWantedSize:46144]`
  - no immediate `INIC-E WIFI TRX IPC 4 timeout` following playback startup
- normal session flow continues:
  - `tts sid=... state=start`
  - `tts sid=... state=sentence_start text=...`
  - playback state transitions continue instead of aborting at track creation

Failure signals to watch:
- `track=` still lands in the old oversized class around `46080B`
- `AudioTrack_Init failed`
- `Malloc failed ... xWantedSize:46144`

Scope note:
- This step corrects the playback-service buffer sizing math.
- It does not yet shrink the reference-export pool or other downlink/runtime allocations; if board heap is still too tight after this change, that should be handled as the next separate step.

## Step 5.17
Rebuild the firmware after lowering the temporary bring-up wake threshold:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully
- output images exist
- current sizes remain:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3597664`
  - `build_RTL8730E/ota_all.bin 3597696`

Flash and validate the more permissive wake path on board:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py flash -p /dev/ttyUSB0 -b 1500000 -m nor
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Board-side validation flow:
- Boot the board and let Wi-Fi connect normally.
- Confirm the KWS backend log now reports the reduced threshold:
  - `kws backend: ... threshold_q15=8192 ...`
- Confirm the periodic KWS status log reflects the lower threshold budget:
  - `kws status: ... thresh_pm=250 ...`
- Speak the wake phrase or even weaker/less precise variants and watch whether wake becomes much easier to trigger.
- Then validate that the board can still proceed through the online path:
  - `wakeword hit: ...`
  - `wakeword queued ...`
  - `xiaozhi connecting: ...`
  - `Connected to websocket server`

Pass signals:
- KWS threshold is visibly lower at boot/runtime
- wake triggers become materially easier than with the prior `21299` threshold
- the board can still enter the XiaoZhi session flow after wake

Expected side effect:
- false positives are more likely in this temporary configuration

Scope note:
- This is a temporary bring-up tuning step only.
- Once wake/session flow is validated, the threshold should be tightened again or replaced by a better model/calibration pass.

## Step 5.18
Rebuild the firmware after adding Chinese comments to project-owned source files:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
git diff --stat
```

Expected build result:
- build completes successfully
- output images exist
- current sizes remain:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3597664`
  - `build_RTL8730E/ota_all.bin 3597696`

Review check:
- `git diff --stat` shows comment-only source updates across the project-owned code tree
- no generated model data or SDK source is modified

Runtime/build note:
- This is a comment-only maintenance step.
- No new board-side behavior is expected, so a full flash/functional regression pass is not mandatory for this step.
- If a spot check is desired, boot logs should remain identical to the previous functional build.

## Step 5.19
Rebuild the firmware after tightening XiaoZhi websocket uplink tx buffer sizing:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully
- output images exist
- current sizes remain:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3597664`
  - `build_RTL8730E/ota_all.bin 3597696`

Flash and validate the XiaoZhi wake-to-uplink path on board:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py flash -p /dev/ttyUSB0 -b 1500000 -m nor
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Board-side validation flow:
- Boot the board, wait for Wi-Fi connect, and trigger XiaoZhi with the wake word.
- Confirm the session still opens normally:
  - `Connected to websocket server`
  - `server hello: sid=...`
  - `asr stream active: provider=xiaozhi_realtime ...`
- Keep speaking for a few seconds so `river_xz_up` continuously sends uplink Opus frames.

Pass signals:
- the previous large uplink heap allocation no longer appears:
  - no `Malloc failed. Core:[CA32], Task:[river_xz_up], [xWantedSize:8320]`
- the transport remains alive during continuous speech:
  - no immediate `INIC-E WIFI TRX IPC 4 timeout`
  - `stt sid=... text=...` or later cloud-side results can still arrive
- wake/session open behavior is unchanged

Failure signals to watch:
- `json_send_failed`
- `binary_send_failed`
- `ERROR: The length of data exceeded the max tx buf len`
- `ERROR: Not get usable buffer, Please enlarge max_queue_size!`

Interpretation:
- Pass:
  - the uplink websocket buffer contract is now aligned with the real packet size and no longer burns heap on `~8 KB` queue items
- Fail:
  - if `max tx buf len` appears, one of the control JSON payloads is larger than expected and the tx limit must be raised moderately
  - if queue exhaustion appears without heap failure, queue depth may need a small follow-up increase while keeping the reduced tx buffer size

## Step 5.20
Rebuild the firmware after tightening XiaoZhi follow-up handling on transport loss:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully
- output images exist
- current sizes remain:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3597664`
  - `build_RTL8730E/ota_all.bin 3597696`

Flash and validate the XiaoZhi transport-loss recovery path on board:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py flash -p /dev/ttyUSB0 -b 1500000 -m nor
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Board-side validation flow:
- Boot the board, let Wi-Fi connect, and trigger XiaoZhi with the wake word.
- If the server or transport later closes unexpectedly, watch for the new fail-closed logs:
  - `xiaozhi transport closed: sid=... window=... stream=... playback=...`
  - `xiaozhi conversation window aborted: reason=transport_closed`
- After that point, keep watching for the previous bad symptoms.

Pass signals:
- no repeating `capture frame ring overflow: dropped=...`
- no websocket queue pressure warning:
  - `ERROR: Not get usable buffer, Please enlarge max_queue_size!`
- no repeated `xiaozhi uplink send failed: status=-6` caused by stale follow-up traffic
- after transport loss, the device fails closed and waits for a fresh wake path instead of trying to recover from the audio thread

Useful interpretation:
- Pass:
  - the real-time VAD/capture path is no longer being used as a transport-recovery thread
  - stale follow-up state is being cleared promptly when XiaoZhi transport dies
- Fail:
  - if `capture frame ring overflow` still appears immediately after a XiaoZhi transport/session drop, there is still another blocking path inside the audio-side open/feed flow and that path needs to be isolated next
