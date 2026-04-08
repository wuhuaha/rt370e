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

## Step 5.21
Documentation review:
```bash
cd /root/ameba-river
sed -n '1,220p' doc/KWS_BC_RESNET_REPLACEMENT_PLAN_ZH.md
sed -n '1,220p' .codex/plan.md
sed -n '1,220p' plan.md
```

Expected review result:
- the replacement plan explicitly records the two direct blockers:
  - missing `Add` op
  - `98x40x1` vs `40x98x1` input-layout mismatch
- the plan files state that BC-ResNet replacement is the immediate hotfix track before further refactor work continues

## Step 5.22
Rebuild the firmware after the BC-ResNet replacement:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully
- output images exist
- current sizes are:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3593568`
  - `build_RTL8730E/ota_all.bin 3593600`

Flash and validate the new KWS runtime on board:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py flash -p /dev/ttyUSB0 -b 1500000 -m nor
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Board-side pass signals:
- KWS initializes successfully without resolver or tensor-allocation failure
- boot logs include the new shape/layout line:
  - `kws input shape: src=schema dims=[1,40,98,1] layout=mels_frames`
- backend profile log reflects the replaced model:
  - `kws backend: runtime=tflite_micro input=40x98x1 ... variant=bc_resnet_best`
- wake pipeline remains active:
  - `wake-stage validation path: capture -> fixed_dsb -> log_mel -> local_kws -> wake event`
- after speaking the wake word, logs show:
  - `wakeword hit: text=小欧管家 ...`
  - followed by `xiaozhi connecting: ...`

Failure signals to watch:
- `kws init failed status=...`
- `kws op resolver registration failed`
- `kws AllocateTensors failed`
- `kws input shape unsupported`

Interpretation:
- Pass:
  - BC-ResNet has been integrated into the existing board runtime contract successfully
  - the board is filling the model input tensor in the correct `40x98x1` order
- Fail:
  - if `AllocateTensors failed` appears, arena budget must be reassessed next
  - if `input shape unsupported` appears, the embedded asset or model metadata does not match the expected deployment contract

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

## Step 5.23
Documentation check:
```bash
cd /root/ameba-river
test -f knowledge/WSL2_FLASH_TROUBLESHOOTING_ZH.md
sed -n '1,220p' knowledge/WSL2_FLASH_TROUBLESHOOTING_ZH.md
```

If the WSL2 flashing issue recurs, re-run the recorded triage flow:
```bash
cd /root/ameba-river
source env.sh
ls -l /dev/ttyUSB* /dev/ttyACM* /dev/ttyS* 2>/dev/null
stty -F /dev/ttyUSB0 -a
python3 tools/river_flash.py -p /dev/ttyUSB0 --log-level debug
```

Expected result:
- the knowledge note exists under `knowledge/`
- the note records that current images still fit the project profile range
- if a USB-bridged serial node is present, `river_flash.py` prints the project profile path before download:
  - `/root/ameba-river/board/rtl8730e/profiles/RTL8730E_NOR.rdev`

## Step 5.24
Rebuild after tightening XiaoZhi idle admission:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully
- output images remain:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3593568`
  - `build_RTL8730E/ota_all.bin 3593600`

Board validation after flash:
```bash
cd /root/ameba-river
source env.sh
python3 tools/river_flash.py -p /dev/ttyUSB0 --log-level debug
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Board-side check:
- let Wi-Fi connect and SNTP become ready
- before speaking the wakeword, speak a few short voice bursts near the microphones
- then watch the runtime logs

Pass signals:
- no repeated spam of:
  - `xiaozhi conversation window aborted: reason=followup_transport_unavailable`
- `stream_busy` no longer rises simply because idle speech was detected before a wakeword
- the first XiaoZhi session still opens only after a real wakeword path:
  - `wakeword hit`
  - `wakeword queued`
  - `xiaozhi connecting`
  - `Connected to websocket server`
  - `server hello`

If KWS is inactive at boot, expected warning now becomes:
- `kws init failed status=...; continue with stable non-kws path`
- `wake admission fallback disabled: idle VAD admission stays wakeword-gated while local KWS is inactive`

## Step 5.25
Rebuild after increasing the BC-ResNet KWS tensor arena:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully
- output images remain:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3593568`
  - `build_RTL8730E/ota_all.bin 3593600`

Flash and monitor on board:
```bash
cd /root/ameba-river
source env.sh
python3 tools/river_flash.py -p /dev/ttyUSB0 --log-level debug
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Board-side check:
- let the board boot and watch the KWS initialization phase before Wi-Fi connect completes
- confirm the old boot failure is gone
- then say the wakeword a few times after Wi-Fi and time sync are ready

Pass signals:
- these old failure lines no longer appear:
  - `Failed to resize buffer. Requested: 159744, available 152920, missing: 6824`
  - `kws AllocateTensors failed: arena=160KB model=56024B`
- KWS boot logs appear normally again, for example:
  - `kws tensor io: ...`
  - `kws alloc: ...`
  - `kws backend: ...`
- after a real wakeword, the runtime reaches the XiaoZhi connect path again:
  - `wakeword hit`
  - `wakeword queued`
  - `xiaozhi connecting`
  - `Connected to websocket server`

Fail interpretation:
- if `AllocateTensors failed` still appears with the new arena, collect the new exact `Requested/available/missing` numbers first; do not guess at the next size bump

## Step 5.26
Rebuild after adding the project-side patched `MEAN` op for BC-ResNet KWS:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully
- output images become:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3601760`
  - `build_RTL8730E/ota_all.bin 3601792`

Flash and monitor on board:
```bash
cd /root/ameba-river
source env.sh
python3 tools/river_flash.py -p /dev/ttyUSB0 --log-level debug
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Board-side check:
- let the board boot, connect Wi-Fi, and complete SNTP sync
- say the wakeword a few times until the runtime reaches `kws gate open`
- keep watching the serial log during the first real KWS inference window

Pass signals:
- these old crash lines do not appear anymore:
  - `Data abort with Data Fault Status Register 0x00001a11`
  - immediate reboot right after `kws gate open`
- KWS reaches a real wake result instead of crashing:
  - `kws gate open`
  - `wakeword hit`
  - `wakeword queued`
  - `xiaozhi connecting`
- no patch fallback error is printed:
  - `river kws mean patch got unsupported reduce pattern`

Fail interpretation:
- if the board still crashes, capture the new fault PC/LR and full first-crash stack; do not assume it is still the same `MEAN` issue
- if `river kws mean patch got unsupported reduce pattern` appears, capture the full surrounding KWS logs because the model is using a reduce pattern outside the currently patched cases

## Step 5.27
Review the new KWS export contract document:
```bash
cd /root/ameba-river
sed -n '1,260p' knowledge/KWS_MODEL_EXPORT_CONTRACT_ZH.md
```

Expected review result:
- the document clearly specifies:
  - input/output tensor contract
  - int8 quantization requirement
  - operator whitelist
  - operator blacklist
  - export-side prohibition on post-export graph rewriting
  - required delivery report fields
  - acceptance criteria for training-side handoff

Manual check:
- confirm the document can be forwarded directly to the training team without needing firmware-side explanation
- confirm it explicitly states that future embedded export types should avoid `MEAN`

Expected outcome:
- no firmware rebuild is required for this step
- no binary output changes are expected for this step

## Step 5.28
Rebuild after letting the patched KWS `MEAN` op reuse SDK `prepare`:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully
- output images remain:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3601760`
  - `build_RTL8730E/ota_all.bin 3601792`

Flash and monitor on board:
```bash
cd /root/ameba-river
source env.sh
python3 tools/river_flash.py -p /dev/ttyUSB0 --log-level debug
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Board-side check:
- let the board complete boot and watch the KWS init block before testing wakeword
- confirm the old `MEAN prepare` failure is gone
- after Wi-Fi and SNTP are ready, say the wakeword a few times

Pass signals:
- these old boot-failure lines no longer appear:
  - `axis->type != kTfLiteInt32 (0 != 2)`
  - `Node MEAN ... failed to prepare`
  - `kws AllocateTensors failed: arena=192KB model=56024B`
- KWS init logs appear normally again, for example:
  - `kws tensor io: ...`
  - `kws alloc: ...`
  - `kws backend: ...`
- after a real wakeword, runtime proceeds past boot monitoring:
  - `kws gate open`
  - `wakeword hit`
  - `wakeword queued`
  - `xiaozhi connecting`

Fail interpretation:
- if the old `axis->type` or `Node MEAN ... failed to prepare` lines still appear, the board is still running a pre-fix image or the flash did not actually complete
- if boot succeeds but a `Data abort` appears again after `kws gate open`, then `prepare` is fixed and the remaining issue is in the patched `MEAN` eval path

## Step 5.29
Rebuild after caching `keep_dims` inside the patched KWS `MEAN` op:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully
- output images remain:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3601760`
  - `build_RTL8730E/ota_all.bin 3601792`

Flash and monitor on board:
```bash
cd /root/ameba-river
source env.sh
python3 tools/river_flash.py -p /dev/ttyUSB0 --log-level debug
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Board-side check:
- let the board boot completely
- confirm KWS still initializes and reaches `kws gate open`
- then say the wakeword a few times and keep watching the first real inference window

Pass signals:
- these runtime failure lines no longer appear:
  - `params != NULL was not true`
  - `Node MEAN ... failed to invoke`
  - `kws worker process failed: status=-6`
- KWS proceeds to a real inference result instead of failing inside patched `MEAN`:
  - `kws gate open`
  - `wakeword hit`
  - `wakeword queued`
  - `xiaozhi connecting`

Secondary observation:
- if `kws tensor data drift: ...` still appears but wakeword can complete and no worker failure follows, capture it anyway because it may indicate a separate runtime-handle issue

Fail interpretation:
- if `params != NULL was not true` still appears, the board is still running a pre-fix image
- if `Node MEAN ... failed to invoke` still appears with a different message, capture the new exact log because the blocker has moved past the old builtin-data dependency
- if `wakeword hit` still never appears but there are no `MEAN` failures anymore, the remaining problem is no longer operator bring-up; it has moved to threshold / score / wake path behavior

## Step 5.30
Rebuild after broadening patched KWS `MEAN` matching from `keep_dims` to output-shape-based matching:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully
- output images remain:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3601760`
  - `build_RTL8730E/ota_all.bin 3601792`

Flash and monitor on board:
```bash
cd /root/ameba-river
source env.sh
python3 tools/river_flash.py -p /dev/ttyUSB0 --log-level debug
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Board-side check:
- let the board boot fully, connect Wi-Fi, and complete SNTP sync
- say the wakeword a few times until runtime reaches `kws gate open`
- keep watching the first real KWS inference window after gate open

Pass signals:
- these runtime failure lines no longer appear:
  - `river kws mean patch got unsupported reduce pattern`
  - `Node MEAN (number 3) failed to invoke with status 1`
  - `kws worker process failed: status=-6`
- KWS proceeds past patched `MEAN` into a real wake path result:
  - `kws gate open`
  - `wakeword hit`
  - `wakeword queued`
  - `xiaozhi connecting`

Fail interpretation:
- if `river kws mean patch got unsupported reduce pattern` still appears, capture the new full log line including:
  - `axes_len`
  - `axis0`
  - `axis1`
  - `keep_dims`
  - `in_rank`
  - `out_rank`
- if `MEAN` errors disappear but `wakeword hit` still never appears, operator bring-up is no longer the blocker; the next issue is threshold / score / wake-path behavior
- if `kws tensor data drift: ...` still appears after `MEAN` succeeds, treat it as a separate runtime-handle issue in the KWS tensor plumbing rather than another reduce-pattern issue

## Step 5.31
Rebuild after forcing patched KWS `MEAN` op-data to an aligned `user_data` address:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully
- output images remain:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3601760`
  - `build_RTL8730E/ota_all.bin 3601792`

Flash and monitor on board:
```bash
cd /root/ameba-river
source env.sh
python3 tools/river_flash.py -p /dev/ttyUSB0 --log-level debug
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Board-side check:
- let the board boot fully, connect Wi-Fi, and complete SNTP sync
- say the wakeword a few times until runtime reaches `kws gate open`
- keep watching the first real inference window after gate open

Pass signals:
- these old crash lines no longer appear:
  - `Data abort with Data Fault Status Register 0x00000221`
  - `Address of Instruction causing Data abort 0x6035f448`
- KWS proceeds past the first real `MEAN` invocation instead of rebooting immediately after `kws gate open`
- ideal forward progress is:
  - `kws gate open`
  - `wakeword hit`
  - `wakeword queued`
  - `xiaozhi connecting`

Fail interpretation:
- if a new `Data abort` still appears, capture the new fault PC/LR and registers; do not assume it is the same unaligned `user_data` issue unless the fault PC is still `0x6035f448`
- if `kws gate open` is stable and there is no crash but still no `wakeword hit`, operator bring-up is no longer the blocker; the next issue is score / threshold / wake-path behavior
- if `kws tensor data drift: ...` still appears after the crash is gone, treat it as a separate interpreter-state issue worth fixing next even if wakeword can already run through

## Step 5.32
Rebuild after switching patched KWS `MEAN` runtime back to SDK `EvalMeanHelper(...)` while keeping project-local reducer-param caching:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully
- output images change to:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3593568`
  - `build_RTL8730E/ota_all.bin 3593600`

Flash and monitor on board:
```bash
cd /root/ameba-river
source env.sh
python3 tools/river_flash.py -p /dev/ttyUSB0 --log-level debug
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Board-side check:
- let the board boot fully, connect Wi-Fi, and complete SNTP sync
- say the wakeword a few times until runtime reaches `kws gate open`
- keep watching the first real KWS inference window after gate open

Pass signals:
- these old patched-eval crash signatures no longer appear:
  - `Data abort with Data Fault Status Register 0x00000221`
  - `Address of Instruction causing Data abort 0x6035f474`
  - `Previous Mode's LR is 0x6035e1ec`
- KWS now survives the first real `MEAN` invoke instead of faulting right after `kws gate open`
- ideal forward progress is:
  - `kws gate open`
  - `wakeword hit`
  - `wakeword queued`
  - `xiaozhi connecting`

Fail interpretation:
- if a new `Data abort` still appears, capture the new fault PC/LR and registers; do not assume it is the same `MEAN` eval-loop fault unless the new PC maps back into the patched `MEAN`
- if `kws worker process failed: status=-6` returns without a hard fault, capture the exact new `Node MEAN ...` message; that means reducer-metadata repair worked but runtime still disagrees with the upstream helper on some detail
- if `kws gate open` becomes stable and there is no crash but still no `wakeword hit`, operator bring-up is no longer the blocker; the next issue is score / threshold / wake-path behavior

## Step 5.33
Documentation / prep check for switching to a no-`MEAN` KWS model:
```bash
cd /root/ameba-river
git branch --show-current
test -f doc/KWS_MEAN_OPERATOR_POSTMORTEM_ZH.md
sed -n '1,80p' doc/KWS_MEAN_OPERATOR_POSTMORTEM_ZH.md
test ! -d build_RTL8730E/build && echo "build dir cleaned"
git status --short --branch
```

Expected result:
- current branch is the dedicated no-`MEAN` preparation branch created after this step
- `doc/KWS_MEAN_OPERATOR_POSTMORTEM_ZH.md` exists and begins with the postmortem summary
- `build_RTL8730E/build` no longer exists, so the large intermediate build directory has been cleaned
- worktree is clean except the user-owned untracked paths:
  - `.env`
  - `tools/kws/`

When the new model is ready, use a clean rebuild from this branch:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
```

First board-side check for the new model:
- confirm boot, Wi-Fi, and SNTP still complete
- confirm the first `kws gate open` no longer produces any `Node MEAN ...` log
- if a crash still happens, capture the new fault PC/LR before assuming it is related to the old `MEAN` issue

## Step 5.34
Rebuild the prep branch with non-mainline voice paths compiled out by default:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
rg -n "CONFIG_RIVER_KWS_MEAN_PATCH_EN|CONFIG_RIVER_AUDIO_ECHO_DEBUG_EN|CONFIG_RIVER_WEBRTC_AECM_EXPERIMENT_EN" \
  build_RTL8730E/build/.config \
  build_RTL8730E/build/project_ap/.config_ca32 \
  build_RTL8730E/build/project_hp/.config_km4 \
  build_RTL8730E/build/project_lp/.config_km0
rg -n "river_voice_kws_mean_patch\\.o|river_voice_kws_mean_patch\\.cc" \
  build_RTL8730E/build/build.ninja \
  build_RTL8730E/build/compile_commands.json
```

Expected build result:
- build completes successfully on branch `prep/kws-no-mean-model`
- output images are:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3556704`
  - `build_RTL8730E/ota_all.bin 3556736`
- generated configs show all prep-branch isolation gates are off:
  - `# CONFIG_RIVER_AUDIO_ECHO_DEBUG_EN is not set`
  - `# CONFIG_RIVER_WEBRTC_AECM_EXPERIMENT_EN is not set`
  - `# CONFIG_RIVER_KWS_MEAN_PATCH_EN is not set`
- the final `rg` against `build.ninja` and `compile_commands.json` returns no match, confirming `river_voice_kws_mean_patch.cc` is not compiled in this default prep build

Optional quick monitor sanity check:
```text
river
river audio
river audio probe status
```

Expected monitor behavior:
- `river` help no longer advertises `river audio <start|stop|status>`, `river audio echo ...`, or `river audio diag ...`
- `river audio` reports that audio echo debug is disabled in the current build
- `river audio probe status` still works

Important interpretation:
- this step is a prep step for the incoming no-`MEAN` model, not a claim that the old `MEAN` model is now runtime-safe on this branch
- if you flash this build before replacing the model asset, do not treat old-model KWS runtime behavior as the acceptance criterion for this step

## Step 5.35
Rebuild the prep branch after switching the embedded KWS asset to the no-`MEAN` model and compiling legacy KWS compatibility out by default:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
rg -n "CONFIG_RIVER_KWS_LEGACY_MODEL_COMPAT_EN" \
  build_RTL8730E/build/.config \
  build_RTL8730E/build/project_ap/.config_ca32 \
  build_RTL8730E/build/project_hp/.config_km4 \
  build_RTL8730E/build/project_lp/.config_km0
rg -n "river_voice_kws_mean_patch\\.o|river_voice_kws_mean_patch\\.cc" \
  build_RTL8730E/build/build.ninja \
  build_RTL8730E/build/compile_commands.json
```

Expected build result:
- build completes successfully on branch `prep/kws-no-mean-model`
- output images are:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
  - `build_RTL8730E/ota_all.bin 3560832`
- generated configs show legacy KWS compatibility is compiled out by default:
  - `# CONFIG_RIVER_KWS_LEGACY_MODEL_COMPAT_EN is not set`
- the final `rg` against `build.ninja` and `compile_commands.json` returns no match, confirming `river_voice_kws_mean_patch.cc` is not compiled in this default no-`MEAN` build

Flash and serial verification:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0 --log-level debug
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

After monitor connects, trigger a software reboot to capture the full boot log:
```text
AT+RST
```

Pass signals from the rebooted board log:
- KWS init completes with the new embedded model:
  - `kws quant: in_src=schema scale_u6=34970 zp=-3 out_src=schema scale_u6=3906 zp=-128`
  - `kws input shape: src=schema dims=[1,40,98,1] layout=mels_frames`
  - `kws output shape: src=schema dims=[1,1,1,1] values=1`
  - `kws backend: ... model=54104B variant=bc_resnet_epoch1_debug ...`
- non-mainline debug isolation still holds:
  - `audio_echo=compiled=no`
- normal platform bring-up still works:
  - Wi-Fi associates and gets `ip=192.168.5.20`
  - `sntp ready: utc=...`
- live wake path still works after the migration:
  - `kws gate open`
  - `kws gate close`
  - `wakeword hit: text=小欧管家`
  - `wakeword queued`
  - `xiaozhi connecting`
  - `Connected to websocket server`
- these old failure signatures do not appear in the observed boot / init window:
  - `Node MEAN (number 3) failed to invoke`
  - `river kws mean patch got unsupported reduce pattern`
  - `Data abort with Data Fault Status Register`

Interpretation:
- this step validates migration readiness of the no-`MEAN` model on the current board/runtime baseline
- if wakeword accuracy is still weak, that is now a model-quality / threshold problem rather than a `MEAN` operator bring-up blocker

## Step 5.36
Rebuild after moving xiaozhi follow-up timeout teardown off the real-time capture path:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully on branch `prep/kws-no-mean-model`
- output images remain:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
  - `build_RTL8730E/ota_all.bin 3560832`

Flash and serial verification:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

After monitor connects, run:
```text
river status
river xiaozhi bootstrap
river xiaozhi connect
river xiaozhi listen start
river xiaozhi listen stop
river xiaozhi disconnect
```

Wait at least `10-12` seconds after `river xiaozhi disconnect`, then run:
```text
river status
```

Pass signals from the observed serial session:
- `river status` before xiaozhi commands shows:
  - `interaction_state=wake_monitoring`
  - `capture_service=running ... queue=0/100 ... dropped=0`
- `river xiaozhi bootstrap` completes and logs:
  - `xiaozhi ota bootstrap ok: ... token_set=yes ...`
- `river xiaozhi connect` completes and logs:
  - `xiaozhi connecting: url=wss://api.tenclass.net/xiaozhi/v1/ ...`
  - `Connected to websocket server`
  - `server hello: sid=...`
- after `river xiaozhi listen start`, `river xiaozhi listen stop`, and `river xiaozhi disconnect`, no repeated overflow warning appears during the idle wait:
  - `capture frame ring overflow: dropped=...`
- the final `river status` still shows:
  - `capture_service=running ... queue=0/100 ... dropped=0`
  - `xiaozhi runtime ... session=closed ... window=no`

Optional exact user-path regression check:
- Say the wake phrase `小欧管家`
- Let the board enter xiaozhi streaming, then stop speaking and allow the `follow_up` window to expire naturally
- During and after the timeout, confirm that the old failure signature does not appear:
  - `capture frame ring overflow: dropped=...`

Interpretation:
- this step validates that timeout-driven xiaozhi teardown no longer blocks the capture consumer thread
- if the optional真人语音 path still reproduces overflow, re-open investigation specifically around the wakeword-opened follow-up window path rather than the general xiaozhi transport lifecycle

## Step 5.37
Rebuild after bounding xiaozhi websocket send latency and serializing `listen stop -> listen start` handoff:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully
- image sizes remain:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
  - `build_RTL8730E/ota_all.bin 3560832`

Preferred board verification on a network where `xiaozhi bootstrap` can resolve DNS:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

After monitor connects, perform the exact user path:
```text
1. Say: 小欧管家
2. Wait for:
   - wakeword hit
   - asr provider=xiaozhi_realtime session started
3. Stop speaking and let the session close naturally
4. During follow_up, speak again within a few seconds
```

Pass signals:
- the board may log transient backpressure such as:
  - `speech detected but cloud stream backpressured/deferred: ... status=-4`
- but it must not enter the old fatal symptom:
  - `capture frame ring overflow: dropped=...`
- expected good-path logs include either:
  - a new `asr provider=xiaozhi_realtime session started sid=...`
  - or bounded `BUSY` backpressure followed by recovery on later speech frames

Current bench notes from this turn:
- local build passed
- on the current `ORVIBO` network, `river xiaozhi bootstrap` failed with:
  - `[HTTPC] ERROR: gethostbyname`
  - `xiaozhi ota bootstrap failed`
- because of that DNS failure, the exact online wakeword/follow-up regression path was not re-run end-to-end in this turn
- if flashing from the current shell fails with `Enter download mode fail: ErrType.DEV_TIMEOUT`, re-enter download mode manually or from the shell and retry:
  - `reboot uartburn`
  - then rerun `python3 tools/river_flash.py -p /dev/ttyUSB0`

Interpretation:
- this step specifically targets the `asr_session_closed` immediate-reopen stall seen in the user log, not the already-addressed follow-up timeout teardown path
- if overflow still reproduces after this step on a DNS-working network, the next debug target should be the residual websocket ready/recycle queue state across xiaozhi stream rollover

## Step 5.38
Rebuild after hardening the no-`MEAN` KWS runtime input binding:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully
- image sizes remain:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
  - `build_RTL8730E/ota_all.bin 3560832`

User-driven flash and serial verification:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

After monitor connects, capture a fresh boot:
```text
AT+RST
```

Boot-time pass signals:
- no crash signature appears:
  - `Data abort with Data Fault Status Register`
- KWS init still reports the expected no-`MEAN` model contract:
  - `kws quant: in_src=schema scale_u6=34970 zp=-3 out_src=schema scale_u6=3906 zp=-128`
  - `kws input shape: src=schema dims=[1,40,98,1] layout=mels_frames`
  - `kws output shape: src=schema dims=[1,1,1,1] values=1`
- the old stale-binding symptom does not reappear:
  - `kws tensor data drift: runtime_input=...`

Wakeword regression check:
```text
1. Wait until Wi-Fi and SNTP are ready
2. Clearly say: 小欧管家
3. Repeat 3-5 times if needed
```

Pass signals during the wake test:
- the score is no longer pinned at the old failure plateau:
  - `score_pm=140 gate_best_pm=140 ...` repeating for every gate cycle
- no sustained KWS queue saturation:
  - `queue=40/40` with fast-growing `dropped=...`
- at least one successful wake path appears:
  - `wakeword hit: text=小欧管家`
  - `wakeword queued`

Interpretation:
- if the stale-binding warning disappears and scores recover, this step fixed the runtime input binding issue without reintroducing the post-`Invoke()` crash
- if boot is stable but scores are still pinned low, the next debug target is model-input content correctness rather than tensor binding lifetime

Observed result on `2026-03-31` from user serial log:
- pass:
  - no `Data abort`
  - `kws quant: in_src=schema scale_u6=34970 zp=-3 out_src=schema scale_u6=3906 zp=-128`
  - `kws input shape: src=schema dims=[1,40,98,1] layout=mels_frames`
  - `kws output shape: src=schema dims=[1,1,1,1] values=1`
  - no `kws tensor data drift`
  - wakeword recovered to a valid hit:
    - `wakeword hit: text=小欧管家 score_pm=265 q15=8704`
  - no KWS queue blow-up:
    - first hit path showed `queue=0/40 peak=0 dropped=0`
  - no capture-path regression:
    - no `capture frame ring overflow`
  - xiaozhi path was usable end-to-end:
    - websocket connected
    - `asr provider=xiaozhi_realtime session started`
    - multiple follow-up / barge-in reopen cycles succeeded
    - `xiaozhi conversation window closed: reason=followup_timeout`
- residual issue:
  - TTS playback still logged intermittent `underrun`
  - one playback cycle logged:
    - `xiaozhi playback write failed: mono=960B stereo=1920B`
    - interaction briefly entered `error_recovering` and then recovered

Result classification:
- `Step 5.38` is board-verified as passed for the intended KWS / session-stability objective
- next serial/debug step should target playback underrun recovery rather than KWS wake correctness

## Step 5.40
Rebuild after removing unsafe runtime `interpreter->input(0)` polling from the KWS worker:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully
- image sizes remain:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
  - `build_RTL8730E/ota_all.bin 3560832`

User-driven flash and serial verification:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

After monitor connects:
```text
AT+RST
```

Primary pass criterion for this step:
- after Wi-Fi association, the old early crash must not appear:
  - `Data abort with Data Fault Status Register 0x00001210`
  - `Address of Instruction causing Data abort 0x60354ab8`
  - `GetTensorData<float>(TfLiteTensor*)`

Boot-time pass signals:
- normal KWS init still appears:
  - `kws quant: in_src=schema scale_u6=34970 zp=-3 out_src=schema scale_u6=3906 zp=-128`
  - `kws input shape: src=schema dims=[1,40,98,1] layout=mels_frames`
  - `kws output shape: src=schema dims=[1,1,1,1] values=1`
- Wi-Fi still associates and gets IP without crashing

Wake regression check:
```text
1. Wait for Wi-Fi and SNTP ready
2. Clearly say: 小欧管家
3. Repeat 2-3 times if needed
```

Pass signals:
- wake is still alive after removing runtime rebind:
  - `wakeword hit: text=小欧管家`
  - `wakeword queued`
- no new crash appears before or after wake

Interpretation:
- if the early Wi-Fi-adjacent crash disappears and wake still works, this step confirms the runtime `input(0)` polling path was the real remaining KWS instability
- if wake degrades again but crash is gone, the next step should search for a safer non-`input(0)` method to validate cached tensor buffers

## Step 5.41
Rebuild after restoring interaction-state sync when the xiaozhi follow-up window closes:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully
- image sizes remain:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
  - `build_RTL8730E/ota_all.bin 3560832`

User-driven flash and serial verification:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

After monitor connects:
```text
AT+RST
```

Runtime repro for this step:
```text
1. Wait until Wi-Fi and SNTP are ready
2. Clearly say: 小欧管家
3. Let xiaozhi enter ASR/TTS and finish one full reply
4. Wait for the follow-up window to expire without rebooting
5. After the timeout, clearly say: 小欧管家 again
```

Primary pass criteria:
- when the follow-up window times out, the interaction state must explicitly return to wake monitoring:
  - `xiaozhi conversation window closed: reason=followup_timeout`
  - `interaction_state: follow_up -> wake_monitoring reason=followup_timeout`
- the second wake works without reset:
  - `wakeword hit: text=小欧管家`
  - `wakeword queued text=小欧管家`

Secondary checks:
- KWS should no longer stay stuck in the old stale state after the first turn:
  - avoid repeated `kws status: ... ready=no ...` after the follow-up window is already closed
- abnormal local teardown should also re-sync back to a sane idle/wake state:
  - `interaction_state: ... -> wake_monitoring reason=transport_closed`
  - or `interaction_state: ... -> wake_monitoring reason=network_lost`

Interpretation:
- if the timeout now produces `follow_up -> wake_monitoring` and the second wake works, the bug was the missing post-window state sync rather than a new KWS scoring regression
- if the timeout closes the cloud window but no interaction-state transition appears, the remaining issue is still in runtime state propagation, not in wakeword detection itself

Observed local result on `2026-03-31`:
- pass:
  - local `RTL8730E` build succeeded
  - output images remained:
    - `build_RTL8730E/km4_boot_all.bin 51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
    - `build_RTL8730E/ota_all.bin 3560832`

## Step 5.42
Rebuild after switching playback to reuse a compatible cached `AudioTrack` and adding playback heap snapshots:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully
- image sizes remain:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
  - `build_RTL8730E/ota_all.bin 3560832`

User-driven flash and serial verification:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

After monitor connects:
```text
AT+RST
```

Runtime repro for this step:
```text
1. Wait until Wi-Fi and SNTP are ready
2. Clearly say: 小欧管家
3. Let xiaozhi finish one full ASR + TTS turn
4. Wait for follow-up timeout and rearm
5. Repeat steps 2-4 at least 3 times without rebooting
```

Primary pass criteria:
- repeated playback starts should begin reusing the cached track:
  - `playback start: ... reuse=yes`
- the playback-service dump or related logs should show reuse counters increasing:
  - `track=create/reuse/destroy`
- playback heap snapshots should no longer show the earlier cliff-like drop across turns:
  - `snapshot reason=playback_start_prepare ...`
  - `snapshot reason=playback_start_reuse ...`
  - `snapshot reason=playback_stop_cached ...`

Secondary checks:
- wake rearm must remain intact after this playback change:
  - `interaction_state: follow_up -> wake_monitoring reason=followup_timeout`
- watch whether `underrun` becomes less frequent after the first turn
- if `reuse=no` keeps appearing for every turn, record the full `playback start: ...` line because that means runtime config is not as stable as expected

Interpretation:
- if later turns show `reuse=yes` and heap stays roughly stable, the main leak/regression was likely repeated playback-object lifecycle churn
- if `reuse=yes` appears but heap still keeps collapsing, the retained allocation is probably outside the `AudioTrack` object itself and the next step should focus on cloud/TTS path buffers
- if `reuse=yes` appears and `underrun` remains frequent, this step improved lifetime stability but not pacing, so the next step should focus on ring depth / write cadence rather than heap retention

Observed local result on `2026-03-31`:
- pass:
  - local `RTL8730E` build succeeded
  - output images remained:
    - `build_RTL8730E/km4_boot_all.bin 51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
    - `build_RTL8730E/ota_all.bin 3560832`

## Step 5.43
Rebuild after hardening KWS queue overflow handling and gate-reset rearm:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully
- image sizes remain:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
  - `build_RTL8730E/ota_all.bin 3560832`

User-driven flash and serial verification:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

After monitor connects:
```text
AT+RST
```

Runtime repro for this step:
```text
1. Let the board boot and wait until Wi-Fi and SNTP are ready
2. Speak around the board for 10-20 seconds, including several short speech bursts and pauses
3. Watch the repeated `kws status`, `kws gate open/close`, and `river.stats` lines before the first successful wake
4. Then clearly say: 小欧管家
5. If wake succeeds, repeat one more wake cycle without rebooting
```

Primary pass criteria:
- the old control-item loss symptom is gone:
  - no `kws queue dropped control item: type=1`
- gate rearm can now explicitly drain stale backlog when needed:
  - `kws gate rearm cleared stale queue: pcm=... ctrl=...`
- KWS queue no longer remains stuck at saturation across many gate transitions:
  - avoid repeated `kws status: ... queue=40/40 ... dropped=...` for long periods
  - avoid `kws gate open: ... queue=40/40` immediately followed by more control-item loss

Secondary checks:
- `river_kws` CPU share should no longer dominate snapshots as severely as in the failure log
- if wake hits recover, confirm the normal lines reappear:
  - `wakeword hit: text=小欧管家`
  - `wakeword queued text=小欧管家`
- if queue behavior is fixed but scores still stay near `234 pm`, record the nearby `cap_peak` / `afe_peak` lines because the next suspect becomes front-end audio saturation or feature quality, not queue control

Interpretation:
- if control-item loss disappears and queue saturation improves, this step fixed the deterministic KWS worker queue bug
- if control-item loss disappears but wake still never crosses threshold, the next debugging target is the front-end audio path rather than queue correctness
- if `queue=40/40` and dropped counters still explode even after this fix, re-check for another producer/consumer contract violation outside the current KWS ring

Observed local result on `2026-03-31`:
- pass:
  - local `RTL8730E` build succeeded
  - output images remained:
    - `build_RTL8730E/km4_boot_all.bin 51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
    - `build_RTL8730E/ota_all.bin 3560832`

## Step 5.44
Verify the refactor-branch kickoff state:
```bash
cd /root/ameba-river
git branch --show-current
git log --oneline -1
git stash list --max-count=1
git status --short
```

Expected result:
- current branch is `refactor`
- the latest commit is the refactor-branch kickoff commit for this step
- the latest stash entry preserves the pre-branch local workspace:
  - `stash@{0}: On kws-no-mean-model: pre-refactor-branch-worktree-backup-20260331`
- working tree is clean after the kickoff commit

Interpretation:
- if the branch is `refactor` and the stash entry still exists, the old local workspace is safely preserved and the new branch can stay clean for structural work
- if `git status --short` is not clean, stop before the first refactor slice and identify whether the dirt is new work or an accidentally restored local file

Observed local result on `2026-03-31`:
- pass:
  - stash backup was created before branching
  - current branch switched to `refactor`
  - this step intentionally changed only planning/docs state, not runtime code

## Step 5.45
Rebuild after switching KWS worker wakeup to event-driven mode and capping gate-open pre-roll flush:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully
- output images remain valid

User-driven flash and serial verification:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

After monitor connects:
```text
AT+RST
```

Runtime repro for this step:
```text
1. Let the board boot and wait until Wi-Fi and SNTP are ready
2. Speak around the board in several short bursts with short pauses, so VAD repeatedly opens/closes KWS gating
3. Watch the first `kws backend` and `kws worker` profile logs after boot
4. Then trigger at least 2 wake attempts with 小欧管家
```

Primary pass criteria:
- boot logs expose the new realtime policy:
  - `kws backend: ... pre_roll_flush=8 ...`
  - `kws worker: priority=5 ... wake=event wait_ms=100 pre_roll_flush=8`
- when pre-roll backlog is larger than the allowed replay cap, the trim log appears:
  - `kws pre-roll trim: dropped=... keep=8/...`
- queue saturation is materially reduced versus the earlier failure logs:
  - avoid repeated long-lived `kws status: ... queue=40/40 ... dropped=...`
  - avoid `kws gate open` immediately being followed by a near-full queue unless there is clearly abnormal load

Secondary checks:
- `river.stats` CPU snapshots should show `river_kws` no longer dominating as aggressively under no-wake short-burst speech
- wake detection should remain alive:
  - `wakeword hit: text=小欧管家`
  - `wakeword queued text=小欧管家`
- if queue occupancy is improved but wake still remains weak, record nearby `score_pm`, `cap_peak`, and `afe_peak` lines; that would shift the next bottleneck from realtime scheduling to front-end audio / model quality

Interpretation:
- if the new worker/profile logs appear and queue plateaus shrink, this step improved the KWS realtime path even before any model changes
- if the queue still pegs at `40/40`, the next suspect is not simple polling overhead anymore, but remaining producer burst size or insufficient consumer compute budget
- if wake quality regresses while queue occupancy improves, the pre-roll flush cap may be too aggressive and should be tuned rather than reverted wholesale

Observed local result on `2026-03-31`:
- pending board verification

## Step 5.46
Rebuild after separating KWS reset semantics from queued PCM and adding high-water backlog trim:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully
- output images remain valid

User-driven flash and serial verification:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

After monitor connects:
```text
AT+RST
```

Runtime repro for this step:
```text
1. Let the board boot and wait until Wi-Fi and SNTP are ready
2. Speak in repeated short bursts so VAD opens/closes KWS gating under mild overload
3. Watch the first `kws worker` and `kws backend` profile logs after boot
4. Then continue speaking long enough to provoke backlog growth, and finally retry at least 2 wake attempts with 小欧管家
```

Primary pass criteria:
- boot/profile logs expose the new overload policy:
  - `kws worker: ... trim=30->13`
  - `kws backend: ... trim=30->13`
- when the KWS queue approaches saturation, the new trim log appears:
  - `kws input trim: dropped=... queue=...->... target=13`
- periodic status logs now show trim counters:
  - `kws status: ... trim_ops=... trim_drop=...`
- queue saturation recovery is materially better than the earlier failure logs:
  - avoid long-lived plateaus where many consecutive status lines stay at `queue=40/40`
  - if `queue=40/40` appears briefly, it should fall back quickly rather than remain pinned while `dropped` keeps climbing

Secondary checks:
- `kws gate rearm cleared stale queue: pcm=...` may appear when a new speech gate reopens while stale PCM is still queued; this is acceptable and should no longer depend on a queued RESET control item
- `river.stats` CPU snapshots should show `river_kws` spending less time dominating the system under no-wake burst speech
- wake detection must remain alive:
  - `wakeword hit: text=小欧管家`
  - `wakeword queued text=小欧管家`

Interpretation:
- if trim logs/counters appear and `queue=40/40` plateaus shrink, this step improved realtime overload behavior even if wake quality still needs separate tuning
- if trim fires continuously and the queue still cannot recover, the next bottleneck is likely consumer compute budget rather than queue policy
- if queue health improves but wake quality regresses, the trim target may be too aggressive and should be tuned rather than reverting reset decoupling

Observed local result on `2026-04-01`:
- local rebuild passed
- output images:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
  - `build_RTL8730E/ota_all.bin 3560832`
- board verification still pending

## Step 5.53
Rebuild after switching the default baseline KWS asset to `bc_resnet_v3_production.tflite`:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
strings build_RTL8730E/km0_km4_ca32_app.bin | rg -n "bc_resnet_v3_production|bc_resnet_epoch1_debug"
```

Expected build result:
- build completes successfully
- no new KWS compile or link error is introduced
- app image still contains `bc_resnet_v3_production`
- app image no longer contains `bc_resnet_epoch1_debug`

User-driven flash and serial verification:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Primary runtime checks:
```text
1. Let the board boot through Wi-Fi connect and SNTP ready
2. Confirm KWS boot log prints:
   - variant=bc_resnet_v3_production
   - input shape dims=[1,40,98,1]
3. Confirm quant log now reflects the new schema-driven input quantization:
   - scale_u6 around 37329
   - zp=-6
4. Test several wake attempts with the normal wake phrase and compare recall against the previous build
```

Expected log behavior:
- `kws backend: ... model=54104B variant=bc_resnet_v3_production ...`
- `kws quant: in_src=schema scale_u6=37329 zp=-6 ...`
- no `MEAN`-related boot failure or unsupported-op log appears

Pass criteria:
- boot completes normally
- KWS init and runtime inference continue to work with the new model payload
- the board logs the new variant and quantization parameters instead of the old epoch1 debug values

Observed local result on `2026-04-01`:
- local rebuild passed
- output images:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
  - `build_RTL8730E/ota_all.bin 3560832`
- binary string check:
  - contains `bc_resnet_v3_production`
  - does not contain `bc_resnet_epoch1_debug`
- board verification pending

## Step 5.49
Rebuild after re-arming the follow-up window on XiaoZhi listen / ASR start:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully
- output images remain valid

User-driven flash and serial verification for the reported follow-up premature close:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Suggested reproduction flow after monitor connects:
```text
1. Let the board boot and connect Wi-Fi
2. Wake the device and complete one normal XiaoZhi round with TTS playback
3. Wait until TTS finishes and the interaction enters `follow_up`
4. Near the end of that follow-up period, speak again to trigger a second `asr session started`
5. Watch whether the websocket still closes immediately after the second ASR round ends
```

Primary pass criteria:
- after a follow-up `asr provider=xiaozhi_realtime session started sid=...`, the websocket should not be closed almost immediately just because the earlier post-TTS tail timer had already expired
- in the previously failing scenario, the second follow-up round should now have enough time to receive normal downstream events such as:
  - `stt sid=...`
  - `llm sid=...`
  - `tts sid=... state=sentence_start`

Secondary checks:
- normal post-TTS idle close should still work when the user does not start another turn:
  - `xiaozhi conversation window closed: reason=followup_timeout`
- the change should not alter first-turn wake admission or the earlier SNTP gate behavior

Interpretation:
- if the second follow-up round now survives long enough to receive downstream text/tts, the issue was a local window-contract bug rather than a server-side early disconnect
- if the second round still closes immediately and no new `stt/llm/tts` arrives, capture the exact logs after the second `session started`; at that point the next suspect becomes transport/server behavior rather than local timeout state

Observed local result on `2026-04-01`:
- local rebuild passed
- output images:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
  - `build_RTL8730E/ota_all.bin 3560832`
- board verification still pending

## Step 5.48
Rebuild after adding low-heap playback-cache reclaim ahead of XiaoZhi reconnect:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully
- output images remain valid

User-driven flash and serial verification for the reported reconnect failure:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Suggested reproduction flow after monitor connects:
```text
1. Let the board boot and connect Wi-Fi
2. Wake the device and complete one XiaoZhi round so TTS playback starts and stops once
3. Wait until the conversation window closes with `followup_timeout`
4. Wake the device again and watch the logs from the new `xiaozhi connecting` attempt
```

Primary pass criteria:
- the reconnect path must no longer fail with:
  - `Malloc failed. Core:[CA32], Task:[river_wake_evt], [free heap size: ...] [xWantedSize:640]`
- the same reconnect should no longer cascade into:
  - `WIFI TRX IPC 4 timeout`
- under the low-heap reproduction, the monitor should now show a preconnect reclaim log before session open:
  - `xiaozhi preconnect reclaimed idle playback cache: heap_free=...->... threshold=65536`

Secondary checks:
- if free heap is already above the reclaim threshold, the reclaim log may not appear; that is acceptable as long as the reconnect still succeeds
- after reclaim, the next TTS playback may log `reuse=no`; that is acceptable because this step intentionally prefers reconnect availability over keeping an idle playback cache warm

Interpretation:
- if the reclaim log appears and the reconnect succeeds, this step fixed the priority inversion between cached playback memory and the next cloud-session open
- if the reclaim log appears but allocation failure still happens, the next suspect is websocket / TLS buffer sizing rather than idle playback cache
- if no reclaim log appears and the same failure repeats, capture the surrounding heap snapshots and full `xiaozhi connecting` block; the remaining pressure is likely coming from another retained resource

Observed local result on `2026-04-01`:
- local rebuild passed
- output images:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
  - `build_RTL8730E/ota_all.bin 3560832`
- board verification still pending

## Step 5.47
Rebuild after isolating the SNTP time-wait gate behind the Iflytek backend macro:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully
- output images remain valid

User-driven flash and serial verification for the default XiaoZhi-only build:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

After monitor connects:
```text
AT+RST
```

Primary runtime check:
```text
1. Let the board boot
2. Wait until Wi-Fi becomes connected
3. Before `sntp ready: utc=...` appears, try wakeword + XiaoZhi interaction
```

Pass criteria for the XiaoZhi-only build:
- wake/business admission is no longer blocked by SNTP readiness
- logs may still show:
  - `sntp kick: network ready; request immediate sync`
  - later `sntp ready: utc=...`
- but wake should already be allowed before that final SNTP-ready line
- specifically, avoid the old behavior where business is deferred only because `time_ready=no`

Regression check for Iflytek split builds:
```text
1. Switch build config to `CONFIG_RIVER_CLOUD_BACKEND_IFLYTEK_SPLIT=y`
2. Rebuild/flash again
3. Confirm Iflytek ASR/TTS paths still require time-ready before use
```

Expected Iflytek behavior:
- the UTC/time-ready wait remains intact for auth/signature-sensitive Iflytek flows

Observed local result on `2026-04-01`:
- local rebuild passed
- output images:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
  - `build_RTL8730E/ota_all.bin 3560832`
- board verification still pending

## Step 5.50
Rebuild after introducing coordinator-owned session phases and centralized interaction-state mapping:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully
- no new compile or link errors appear in `river_session_coordinator`
- output images remain valid

User-driven flash and serial verification:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Primary runtime checks:
```text
1. Let the board boot to `wake_monitoring`
2. Trigger one normal wakeword -> ASR -> TTS round
3. Let TTS finish and observe follow-up entry / exit
4. Trigger a second wakeword after follow-up timeout
```

Expected log behavior:
- normal path should still show a stable order such as:
  - `interaction_state: booting -> wake_monitoring`
  - `wakeword queued ...`
  - `interaction_state: wake_monitoring -> wake_confirmed`
  - `interaction_state: wake_confirmed -> asr_streaming`
  - later `follow_up` or `speaking` transitions as before
- wakeword rejection logs should now report coordinator phase:
  - `wakeword ignored: session_phase=...`
- if an unexpected runtime jump happens, a new warning may appear:
  - `session phase transition outside preferred contract: ...`
  this should be treated as a control-plane contract clue, not as a harmless cosmetic log

Pass criteria:
- wakeword / ASR / TTS baseline still works
- interaction-state logs still advance through the expected user-visible phases
- no obvious regression such as getting stuck in `wake_confirmed` or missing `wake_monitoring` after timeout

Observed local result on `2026-04-01`:
- local rebuild passed
- output images:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
  - `build_RTL8730E/ota_all.bin 3560832`
- board verification still pending

## Step 5.51
Rebuild after centralizing XiaoZhi local runtime cleanup and playback/session helper ownership:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully
- no new compile or link errors appear in `river_cloud_adapter` or `river_cloud_xiaozhi_session`
- output images remain valid

User-driven flash and serial verification:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Primary runtime checks:
```text
1. Let the board boot and connect Wi-Fi
2. Trigger one normal wakeword -> ASR -> TTS round
3. Let the dialogue close naturally, or briefly cut network to force `network_lost`
4. After teardown, inspect the next `river` status dump or wait for the next relevant runtime log
```

Expected log behavior:
- normal path should still show:
  - `xiaozhi playback start: ...`
  - `tts ... state=start`
  - `tts ... state=stop`
- on transport teardown, cleanup should still begin from the same cause log:
  - `xiaozhi transport closed: ...`
  - or `network_lost`
- after cleanup, runtime status should no longer keep stale conversation metadata:
  - `sid=-`
  - `pending_text=-`
  - window inactive
- this step should not add extra duplicate cleanup logs for the same teardown cause

Pass criteria:
- wakeword / ASR / TTS baseline still works
- `transport_closed`, `network_lost`, and audio-close paths do not regress into stuck `follow_up` / stale `sid`
- playback start/stop behavior remains unchanged from the user-visible perspective

Observed local result on `2026-04-01`:
- local rebuild passed
- output images:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
  - `build_RTL8730E/ota_all.bin 3560832`
- board verification still pending

## Step 5.52
Rebuild after boosting speaker playback loudness for TTS paths:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully
- no new compile or link errors appear in `river_playback_service`, `river_cloud_adapter`, or `river_tts_iflytek_ws`
- output images remain valid

User-driven flash and serial verification:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Primary runtime checks:
```text
1. Let the board boot and connect Wi-Fi
2. Trigger one XiaoZhi wakeword -> ASR -> TTS round
3. Listen for the TTS loudness change on the same board / speaker position used before
4. If using Iflytek TTS in another build path, trigger one playback round there as well
```

Expected log behavior:
- playback start should still succeed normally
- XiaoZhi playback log now exposes the configured software gain:
  - `xiaozhi playback start: ... gain=2/1`
- no new playback write errors or playback-start failures should appear

Pass criteria:
- TTS is audibly louder than the previous build
- no severe distortion, repeated underrun, or playback start failure is introduced
- user-visible playback flow remains the same aside from loudness

Observed local result on `2026-04-01`:
- local rebuild passed
- output images:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
  - `build_RTL8730E/ota_all.bin 3560832`
- board verification still pending

## Step 5.53
Export, embed, build, and flash the float32 BC-ResNet baseline:
```bash
cd /root/ameba-river
/root/kws-training-pro/.venv-training/bin/python tools/kws/export_bc_resnet_tflite.py \
  --checkpoint /root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_best.pth \
  --output /tmp/bc_resnet_v3_production_fp32.tflite \
  --quantization float32
python3 tools/kws/embed_tflite_model.py \
  --input /tmp/bc_resnet_v3_production_fp32.tflite \
  --header components/river_voice/generated/river_wake_word_model_data.h \
  --symbol kws_model
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
strings build_RTL8730E/km0_km4_ca32_app.bin | rg 'bc_resnet_v3_production_fp32|bc_resnet_v3_production'
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
```

Expected export result:
- `/tmp/bc_resnet_v3_production_fp32.tflite` is created
- exporter reports:
  - `mode=float32`
  - `input_dtype=float32`
  - `output_dtype=float32`
  - zero quantization on input/output

Expected build result:
- build completes successfully
- no new compile or link errors appear in `river_voice_kws` or the generated model header
- `strings build_RTL8730E/km0_km4_ca32_app.bin` contains `bc_resnet_v3_production_fp32`

Expected board result after flash:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Primary runtime checks:
```text
1. Reconnect monitor before or during reset so the boot log is captured.
2. Confirm the boot-time KWS profile line shows:
   - variant=bc_resnet_v3_production_fp32
   - model=77848B
3. Confirm the tensor-io log shows float32 runtime/model/effective types:
   - runtime_in=float32 runtime_out=float32
   - model_in=float32 model_out=float32
   - effective_in=float32 effective_out=float32
4. Run `river status` once the shell is ready and confirm the board is alive after flash.
5. Speak the wake word and compare whether KWS scores still collapse near the old fixed `62pm` value.
```

Pass criteria:
- float32 model exports and embeds successfully
- firmware rebuilds with the float32 header in place
- flash completes successfully
- boot log identifies the float32 variant
- board remains responsive after flash

Observed result on `2026-04-01`:
- float32 export passed:
  - `wrote=/tmp/bc_resnet_v3_production_fp32.tflite bytes=77848 mode=float32`
  - `input_dtype=float32`
  - `output_dtype=float32`
- build passed
- output images:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3585376`
  - `build_RTL8730E/ota_all.bin 3585408`
- binary string verification passed:
  - `bc_resnet_v3_production_fp32`
- flash passed on `/dev/ttyUSB0`
- post-flash monitor command `river status` succeeded and showed the board running normally
- boot-time `kws backend` / tensor-io lines were not captured in this run because monitor attached after reset

## Step 5.54
Export, embed, build, flash, and verify the calibrated int8 BC-ResNet deployment:
```bash
cd /root/ameba-river
/root/kws-training-pro/.venv-training/bin/python tools/kws/export_bc_resnet_tflite.py \
  --checkpoint /root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_best.pth \
  --output /tmp/bc_resnet_v3_production_int8_cal.tflite \
  --quantization int8
python3 tools/kws/embed_tflite_model.py \
  --input /tmp/bc_resnet_v3_production_int8_cal.tflite \
  --header components/river_voice/generated/river_wake_word_model_data.h \
  --symbol kws_model
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
strings build_RTL8730E/km0_km4_ca32_app.bin | rg 'bc_resnet_v3_production_int8_cal|bc_resnet_v3_production_fp32'
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
```

Expected export result:
- `/tmp/bc_resnet_v3_production_int8_cal.tflite` is created
- exporter reports:
  - `mode=int8`
  - `representative_samples=256`
  - nontrivial input quantization derived from real representative features, not random tensors

Expected build result:
- build completes successfully
- `strings build_RTL8730E/km0_km4_ca32_app.bin` contains `bc_resnet_v3_production_int8_cal`
- image sizes stay on the normal int8 footprint, not the larger float32 footprint

Expected board result after flash:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Primary runtime checks:
```text
1. Confirm the board no longer prints `kws AllocateTensors failed`.
2. Confirm KWS activity is visible either through live `kws gate open/close` logs or through `river status`.
3. Run `river status` and confirm it prints a `river.voice.kws] kws status:` line after the diagnostic hook is added.
4. Confirm the board still reaches Wi-Fi connected state and remains responsive.
```

Pass criteria:
- int8-cal model exports and embeds successfully
- firmware rebuilds and flashes successfully
- board no longer falls back out of KWS initialization
- `river status` or live logs show KWS is active on-device

Observed result on `2026-04-01`:
- calibrated int8 export passed:
  - `wrote=/tmp/bc_resnet_v3_production_int8_cal.tflite bytes=54104 mode=int8`
  - `representative_samples=256`
  - `input_quant=(0.017904678359627724, -7)`
  - `output_quant=(0.00390625, -128)`
- build passed
- output images:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
  - `build_RTL8730E/ota_all.bin 3560832`
- binary string verification passed:
  - `bc_resnet_v3_production_int8_cal`
- flash passed on `/dev/ttyUSB0`
- first int8-cal runtime verification passed before the status-path reflashing:
  - live monitor showed `kws gate open` and `kws gate close`
- after reflashing the status-path patch, `river status` printed:
  - `2026-04-01 13:02:18.667 [0000013723][I][river.voice.kws] kws status: gate=closed ready=no ... opens=1 closes=1`
- same `river status` run also showed:
  - `tasks=17`
  - Wi-Fi connected on `2026-04-01 13:02:13.836`
- no `AllocateTensors failed` line appeared in the int8-cal board runs

## Step 5.55
Embed, build, flash, and verify the production-final deployment state from the algorithm team's `final_v2` artifact:
```bash
cd /root/ameba-river
sha256sum \
  /root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_v3_production_final.tflite \
  /root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_v3_production_final_v2.tflite
python3 tools/kws/embed_tflite_model.py \
  --input /root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_v3_production_final_v2.tflite \
  --header components/river_voice/generated/river_wake_word_model_data.h \
  --symbol kws_model
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
strings build_RTL8730E/km0_km4_ca32_app.bin | rg 'bc_resnet_v3_production_final|bc_resnet_v3_production_int8_cal'
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
```

Expected pre-build result:
- `bc_resnet_v3_production_final.tflite` and `bc_resnet_v3_production_final_v2.tflite` have the same SHA-256
- the deployment report remains the source of truth for:
  - balanced threshold `0.6`
  - input quantization `scale=0.01790468 zp=-7`
  - output quantization `scale=0.00390625 zp=-128`

Expected build result:
- build completes successfully
- `strings build_RTL8730E/km0_km4_ca32_app.bin` contains `bc_resnet_v3_production_final`
- `strings build_RTL8730E/km0_km4_ca32_app.bin` does not contain `bc_resnet_v3_production_int8_cal`
- image sizes stay on the normal int8 footprint

Expected board result after flash:
- flash completes successfully on `/dev/ttyUSB0`
- the board resets normally after download

Optional follow-up monitor check:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Expected monitor behavior:
- serial connection succeeds
- if the existing SDK monitor issue is still present, it may print:
  - `Failed to get cmd list: Get cmd list expired`
- this monitor limitation does not invalidate the flash result

Observed result on `2026-04-01`:
- upstream artifact equivalence confirmed:
  - both files hashed to `4e7f368f67f9ba7ad98e1b037c1e447f3228dca43305a03e8b5cf46a533523d6`
  - both files were `54104` bytes
- build passed
- output images:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
  - `build_RTL8730E/ota_all.bin 3560832`
- binary string verification passed:
  - `bc_resnet_v3_production_final`
- flash passed on `/dev/ttyUSB0`
- post-flash monitor behavior matched the known issue:
  - serial connection succeeded
  - SDK monitor then printed `Failed to get cmd list: Get cmd list expired`

## Step 5.56 Verification
Forced deployment target:
- `/root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_v3_production_final_v2.tflite`
- `/root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_v3_production_final_v2.tflite.meta.json`

Upstream export facts to preserve with the deployment:
- model size `54104`
- MD5 `0cd2c03ec46888ff0506a9e41ab7a33f`
- SHA-256 `19fa4dca80ff4355b9de6da242789aabb16abed63820b2a3bd00a4c979a70a0b`
- exporter parity summary:
  - `pt_vs_tflite mean_abs=0.054256`
  - threshold `0.4` agreement `114/128`
  - threshold `0.6` agreement `121/128`
  - threshold `0.8` agreement `123/128`

Build and image verification commands:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
strings build_RTL8730E/km0_km4_ca32_app.bin | rg 'bc_resnet_v3_production_final_v2|bc_resnet_v3_production_final'
```

Expected build result:
- build completes successfully
- the application image contains `bc_resnet_v3_production_final_v2`
- image sizes remain on the normal int8 footprint

Observed build result on `2026-04-01`:
- build passed
- image sizes:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
  - `build_RTL8730E/ota_all.bin 3560832`
- binary string verification passed:
  - `bc_resnet_v3_production_final_v2`

WSL device reattach commands used before flashing:
```bash
usbipd.exe list
usbipd.exe attach --wsl --busid 3-4
```

Observed device result:
- host listed the board as `3-4 067b:23a3 Prolific PL2303GC USB Serial COM Port (COM3) Shared`
- after attach, `/dev/ttyUSB0` reappeared inside WSL

Flash command:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
```

Expected flash result:
- flashing completes successfully
- tool reports `Finished PASS`
- device resets after download

Observed flash result on `2026-04-01 15:20`:
- flash passed on `/dev/ttyUSB0`
- `km4_boot_all.bin` and `km0_km4_ca32_app.bin` both downloaded successfully
- tool reported `Finished PASS`
- flash tool issued `Reset device without DTR/RTS`

Optional serial confirmation command:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Observed monitor result on `2026-04-01`:
- serial connection to `/dev/ttyUSB0` succeeded
- this turn did not capture a fresh boot banner because the monitor attached after the reset window

## Step 5.57 Verification
Offline triplet comparison target samples:
- `/root/kws-dataset-pro-blueprint/data/augmented_final/device_recordings___positive___positive__pos_neutral_mid_001__小欧管家__take001__rtl8730e-board__rec-1774343625242-44fbbee1.wav`
- `/root/kws-dataset-pro-blueprint/data/augmented_final/device_recordings___negative___hard_negative__hn_shang_jia_001__小欧商家__take001__rtl8730e-board__rec-1774403309113-d87271e2.wav`
- `/root/kws-dataset-pro-blueprint/data/augmented_final/device_recordings___negative___verifier_negative__vn_context_005__这是谁家的小欧管家__take001__rtl8730e-board__rec-1774401769981-9199eabe.wav`

Comparison command:
```bash
cd /root/ameba-river
python3 tools/kws/compare_triplet_kws.py \
  --checkpoint /root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_best.pth \
  --tflite /root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_v3_production_final_v2.tflite \
  --wav /root/kws-dataset-pro-blueprint/data/augmented_final/device_recordings___positive___positive__pos_neutral_mid_001__小欧管家__take001__rtl8730e-board__rec-1774343625242-44fbbee1.wav \
  --wav /root/kws-dataset-pro-blueprint/data/augmented_final/device_recordings___negative___hard_negative__hn_shang_jia_001__小欧商家__take001__rtl8730e-board__rec-1774403309113-d87271e2.wav \
  --wav /root/kws-dataset-pro-blueprint/data/augmented_final/device_recordings___negative___verifier_negative__vn_context_005__这是谁家的小欧管家__take001__rtl8730e-board__rec-1774401769981-9199eabe.wav
```

Expected result:
- the script prints one report per WAV with:
  - training-frontend feature stats
  - board-faithful host-replay feature stats
  - PT score on both feature paths
  - TFLite score on both feature paths
- if frontend drift is the dominant issue, feature diffs or PT score diffs should be obviously large on the board-faithful path

Observed result on `2026-04-01`:
- positive wake word `小欧管家`:
  - feature diff `mean_abs=0.002765`, `max_abs=0.026696`
  - PT `0.828093` vs board-faithful PT `0.827219`
  - TFLite `0.843750 (raw=88)` vs board-faithful TFLite `0.855469 (raw=91)`
- hard negative `小欧商家`:
  - feature diff `mean_abs=0.001837`, `max_abs=0.019436`
  - PT `0.001380` vs board-faithful PT `0.001369`
  - TFLite `0.007812 (raw=-126)` vs board-faithful TFLite `0.015625 (raw=-124)`
- context negative `这是谁家的小欧管家`:
  - feature diff `mean_abs=0.000637`, `max_abs=0.012912`
  - PT `0.413958` vs board-faithful PT `0.413964`
  - TFLite `0.425781 (raw=-19)` vs board-faithful TFLite `0.414062 (raw=-22)`

Interpretation:
- current training frontend and board-faithful host replay frontend are close enough that frontend drift is not the primary explanation for the on-board repeated `0.375/raw=-32`
- the remaining discrepancy is more likely in exported-model behavior or board runtime tensor/output handling than in frontend feature extraction

## Step 5.58 Verification
Build command:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully with the new KWS diagnostic fields compiled in
- image size grows slightly versus the previous deployment because of the extra state and log strings

Observed build result on `2026-04-01`:
- build passed twice during this step:
  - once for the initial diagnostic implementation
  - once more for the `last_*` status-label cleanup
- final image sizes were:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3564896`
  - `build_RTL8730E/ota_all.bin 3564928`

Flash command:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
```

Expected flash result:
- flashing completes successfully
- tool reports `Finished PASS`

Observed flash result on `2026-04-01`:
- initial retries were needed because the serial download path was unstable:
  - one attempt failed at `80%` on `km0_km4_ca32_app.bin` with `b'\\xe2'`
  - two later attempts failed to enter download mode with `ErrType.SYS_PROTO`
- the board was then forced back into ROM download mode from the serial side, after which the final flash passed:
  - final successful flash completed at `2026-04-01 16:19:54`
  - tool reported `Finished PASS`

Live monitor command used for diagnosis:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Expected live-diagnostic behavior:
- `river status` prints new last-inference snapshot fields:
  - `last_raw=...`
  - `same=[r:... f:... i:...]`
  - `last_feat_hash=...`
  - `last_input_hash=...`
- active speech should eventually produce `kws diag: ...` with:
  - `raw=...`
  - `same=[raw:... feat:... input:...]`
  - `feat_hash=...`
  - `input_hash=...`

Observed live result on `2026-04-01`:
- `river status` on the first diagnostic flash already showed the new snapshot fields in the KWS status line
- a live speech-triggered inference produced:
```text
2026-04-01 16:13:22.243 [0000235007][I][river.voice.kws] kws diag: infer=2 gate=open out_type=int8 raw=52 score=0.703125 q15=23039 same=[raw:2 feat:1 input:1] feat_hash=0x94fb99dc input_hash=0x3cab68fc max_db_milli=23274 feat[min_milli=-2162 max_milli=2404 mean_milli=516] input[min=-128 max=127 mean_milli=21 probes=80,116,-5,-13]
```
- the same window immediately triggered:
```text
2026-04-01 16:13:22.244 [0000235009][I][river.voice.kws] wakeword hit: text=小欧管家 score_pm=703 q15=23039 triggers=2 cooldown_ms=1800 mode=threshold
```
- the same session also exposed a separate post-wake heap warning:
```text
2026-04-01 16:13:22.437 Malloc failed. Core:[CA32], Task:[river_wake_evt], [free heap size: 1280] [xWantedSize:1408]
```

Interpretation:
- `same=[raw:2 feat:1 input:1]` is the key result from this step
- it means:
  - current and previous inference had different pre-quantized feature hashes
  - current and previous inference had different quantized input tensor hashes
  - but current and previous inference still returned the same raw output scalar `52`
- for this captured case, repeated confidence is therefore not explained by:
  - stale frontend features
  - stale tensor writes
  - threshold-only configuration
- the remaining likely causes are concentrated on the model/output side:
  - output collapse onto a few repeated raw bins
  - model discrimination weakness under adjacent windows
  - export/runtime behavior that keeps many nearby windows on the same output bucket

Note on final deployed image:
- the final reflashed image in this step only renamed the status-line snapshot labels from `raw/feat_hash/input_hash` to `last_raw/last_feat_hash/last_input_hash`
- the diagnostic logic and inference-side evidence above still apply to the final flashed code

## Step 5.59 Verification
Build command:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully with the new pull-style KWS dump commands compiled in
- image size increases modestly because the board now retains an exact-tensor snapshot in RAM and exposes new diag command strings

Observed build result on `2026-04-02`:
- build passed
- final image sizes were:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3568992`
  - `build_RTL8730E/ota_all.bin 3569024`

Flash command:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
```

Expected flash result:
- flashing completes successfully
- tool reports `Finished PASS`

Observed flash result on `2026-04-02`:
- flash passed on the first attempt for this step
- tool reported:
  - `km4_boot_all.bin download done: 51KB / 461.0ms / 906.0Kbps`
  - `km0_km4_ca32_app.bin download done: 3486KB / 32186.0ms / 887.0Kbps`
  - `Finished PASS`

Next live board check for the user:
```text
river kws dump clear
river kws dump next
```

Expected live behavior after `river kws dump next`:
- the board does not print hundreds of dump lines immediately anymore
- after the next KWS inference window is captured, it should print a compact line similar to:
  - `kws tensor dump captured: seq=... infer=... feat_chunks=... input_chunks=... output_chunks=...`

Then query the cached snapshot:
```text
river kws dump meta
river kws dump chunk output_raw 1
river kws dump chunk input_raw 1
```

Expected live behavior for the pull-style dump:
- `river kws dump meta` prints:
  - one `kws tensor dump begin: ...` line
  - one `kws tensor dump meta: ...` line
  - one `kws tensor dump snapshot: seq=... infer=... chunks=[...]` line
- `river kws dump chunk output_raw 1` prints exactly one `output_raw` chunk line
- `river kws dump chunk input_raw 1` prints exactly one `input_raw` chunk line
- invalid requests such as `river kws dump chunk input_raw 99999` should fail with a clear range error instead of emitting partial garbage

Current diagnostic interpretation:
- this step does not yet prove where the constant-confidence bug lives
- it makes the next verification reliable enough to answer that question by comparing:
  - board-captured feature tensor
  - board-captured raw input tensor
  - board-captured raw output tensor
  with host replay, one chunk at a time

## Step 5.60 Verification
Build command:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully with the new local-only KWS debug command and wake-handoff suppression logic compiled in
- image size should stay effectively unchanged versus Step `5.59`, because this step only adds a small runtime flag and a few log strings

Observed build result on `2026-04-02`:
- build passed
- final image sizes were:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3568992`
  - `build_RTL8730E/ota_all.bin 3569024`

Flash command:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
```

Expected flash result:
- flashing completes successfully
- tool reports `Finished PASS`

Current board-debug procedure for exact tensor replay:
```text
river kws debug local on
river kws dump clear
river kws dump next
```

Why this sequence exists:
- `river kws debug local on`
  - keeps wakeword/KWS active
  - but suppresses wake-triggered cloud/session handoff
  - this avoids the CA32 low-heap path from breaking later dump retrieval
- `river kws dump next`
  - arms capture of the next exact feature/input/output tensor triplet only

Expected immediate status/logs after enabling local debug:
```text
[river.voice.kws] kws debug local_only: enabled=yes note=wakeword_still_runs_cloud_handoff=suppressed
[river.voice.kws] kws debug status: local_only=yes wake_handoff=blocked reason=local_debug
```

Expected live behavior after speaking the wakeword:
- you should still see the normal local KWS evidence:
  - `kws diag: ...`
  - `kws tensor dump captured: seq=... infer=...`
  - `wakeword hit: text=小欧管家 ...`
- but instead of cloud handoff you should now see:
  - `wakeword handoff held: reason=local_debug ...`
  - or `wakeword handoff held: reason=tensor_dump_ready ...`
- and you should **not** see wake-triggered cloud transport logs such as:
  - `xiaozhi connecting: ...`
  - `server hello: ...`
  - `xiaozhi conversation window opened: ...`

Then pull back the cached snapshot:
```text
river kws dump meta
river kws dump chunk output_raw 1
river kws dump chunk input_raw 1
river kws dump chunk feat_f32 1
```

Expected dump retrieval behavior in local debug mode:
- `meta` prints the same `begin/meta/snapshot` lines as Step `5.59`
- `chunk ...` prints the requested chunk lines
- there should be no concurrent `river_wake_evt` heap/IPC cascade during this retrieval window

After the tensor dump session is complete:
```text
river kws debug local off
```

Expected post-debug behavior:
- subsequent wakewords can again proceed into normal XiaoZhi/session handoff
- status should return to:
  - `local_only=no`
  - `wake_handoff=normal` unless a fresh dump snapshot is still pending/ready

## Step 5.61 Verification
Build command:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
```

Expected build result:
- build completes successfully after the Wi‑Fi credential update
- only project-side station credentials change; connection state machine code stays untouched

Observed build result on `2026-04-02`:
- build passed
- final image sizes were unchanged:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3568992`
  - `build_RTL8730E/ota_all.bin 3569024`

Flash command:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
```

Expected boot-time Wi‑Fi logs after flash:
```text
[river.wifi] autoconnect init: ap_count=1 primary=river retry_ms=5000
```

Expected connect-cycle logs after Wi‑Fi starts:
```text
[river.wifi] connect attempt=1 ap_count=1 next_index=0 current=river
```

Expected behavioral change:
- the board no longer scans/rotates into the previous fallback SSIDs
- there should be no later logs mentioning:
  - `WLL2G`
  - `ORVIBO`
- only the single configured AP `river` should appear in:
  - boot-time autoconnect status
  - retry logs
  - successful connect logs

## Step 5.62 Verification
Build command after reverting TCP debug transport:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
```

Flash command to bring the board back to the same baseline:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
```

Expected repo/runtime baseline after this revert:
- there is no `river tcpdiag ...` command surface anymore
- firmware no longer contains the board-side TCP diag client
- KWS debugging still relies on the existing serial/local tools:
  - `river kws status`
  - `river kws debug local on`
  - `river kws dump next`
  - `river kws dump meta`
  - `river kws dump chunk ...`

Expected boot/runtime emphasis after the revert:
- focus returns to local wakeword diagnosis rather than host transport diagnosis
- the wakeword investigation path is again:
  - board log / serial monitor
  - local debug mode
  - pull-based tensor dump
  - host-side replay of captured tensors

Observed build result on `2026-04-02`:
- build passed
- final image sizes were:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 3568992`
  - `build_RTL8730E/ota_all.bin 3569024`

Observed flash result on `2026-04-02`:
- flashing completed successfully
- tool reported `Finished PASS`

## Step 5.63 Verification
This is a documentation-only step.

Review commands:
```bash
cd /root/ameba-river
sed -n '1,260p' doc/KWS_BOARD_FALSE_WAKE_DEBUG_CHECKLIST_ZH.md
git diff --check
```

Expected result:
- the new document exists and is readable
- it includes:
  - current symptom summary
  - deployment/runtime pitfalls already encountered
  - prioritized suspicion list
  - staged analysis plan
  - multiple debug methods with individual exit mechanisms
  - actual serial-debugging pitfalls seen in this project
- `git diff --check` reports no patch-format errors

## Step 5.64 Verification
This is a documentation-only step.

Review commands:
```bash
cd /root/ameba-river
sed -n '1,320p' doc/KWS_FP32_DEPLOYMENT_GATES_ZH.md
git diff --check
```

Expected result:
- the new document exists and is readable
- it clearly separates:
  - compatibility hard gates
  - memory/arena gates
  - algorithm-side delivery requirements
  - first-board-bring-up acceptance gates
- it contains explicit direct-deploy thresholds for the current full profile:
  - recommended `KWS arena <= 224KB`
  - direct-deploy upper bound `KWS arena <= 256KB`
- it states that `.tflite` size is only a secondary screen and `AllocateTensors()` arena is the primary gate
- `git diff --check` reports no patch-format errors

## Step 5.65 Verification
This is a documentation-only step.

Review commands:
```bash
cd /root/ameba-river
sed -n '1,260p' doc/RTL8730E_MEMORY_LAYERING_EXPLAINER_ZH.md
git diff --check
```

Expected result:
- the new document exists and is readable
- it clearly explains the distinction between:
  - physical `64MB` DRAM capacity
  - current firmware-visible layout window
  - current `CA32` carveout
  - static-section occupancy vs runtime heap
  - total free heap vs largest contiguous free block
- it contains a text memory-layer diagram
- it ties the explanation back to current project evidence, including:
  - boot log `0x60800000` vs `0x64000000`
  - `CA32_BL3_DRAM_NS` `4MB` carveout
  - `__psram_heap_buffer_*` heap derivation
  - historical `heap_free` runtime numbers
- `git diff --check` reports no patch-format errors

## Step 5.66 Verification
This is a documentation-only step.

Review commands:
```bash
cd /root/ameba-river
sed -n '1,320p' doc/RTL8730E_MEMORY_LAYOUT_OPTIONS_ZH.md
git diff --check
```

Expected result:
- the new document exists and is readable
- it clearly states the recommendation:
  - memory-layout adjustment is strategically worthwhile
  - but should not be the first reaction to the current model-quality issue
- it contains a comparison table covering multiple layout strategies
- it explicitly compares at least:
  - no-layout-change trim path
  - conservative expansion
  - `aivoice`-style expansion
  - aggressive near-64MB expansion
- it includes verification focus and rollback conditions for each path
- it ties the recommendation back to current project evidence:
  - physical `64MB` vs current `8MB` layout window
  - current `4MB` CA32 carveout
  - CA32 heap derivation from linker tail
  - KM4 heap-extend presence in the SDK
- `git diff --check` reports no patch-format errors

## Step 5.67 Verification
Build:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
```

Flash:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
```

Boot/runtime checks from monitor:
```text
river kws status
```

Expected boot/runtime emphasis:
- boot must still complete with the full product image enabled
- KWS init logs should now identify the FP32 experiment profile and show a much larger arena budget:
  - `model=bc_resnet_v3_fp32_experimental`
  - `threshold_q15=17096`
  - `queue=64`
  - `arena=768KB`
- `kws tensor io` should report `effective_in=float32` and `effective_out=float32`
- `kws alloc` should report nonzero `arena_used` and `arena_slack`
- `kws memory plan` should report init-time heap before/after plus queue/pre-roll/dump reservation bytes
- periodic `kws perf` logs should appear and include:
  - `infer_us[last=... avg=... max=...]`
  - `warn=... alert=...`
  - current/min/init heap
  - queue policy and arena usage
- `river.stats` snapshots should now include `kws:<...>B` in `stack_free=[...]`

Observed build result on `2026-04-02`:
- full `RTL8730E` build passed
- resulting images were:
  - `build_RTL8730E/build/project_hp/image/km4_boot_all.bin` `51K`
  - `build_RTL8730E/build/project_lp/image/km0_image2_all.bin` `92K`
  - `build_RTL8730E/build/project_hp/image/km4_image2_all.bin` `371K`
  - `build_RTL8730E/build/project_ap/image/ap_image_all.bin` `3.0M`
  - `build_RTL8730E/km0_km4_ca32_app.bin` `3.5M`

## Step 5.68 Verification
Build:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
```

Flash:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
```

Expected boot/runtime emphasis:
- the earlier `xWantedSize:786560` boot-time failure should disappear
- KWS init should now print a staged allocation trace, including lines similar to:
  - `kws init plan: ... arena=688KB ...`
  - `kws init stage: fft_ready ...`
  - `kws init stage: arena_ready ...`
  - `kws init runtime buffers: ...`
  - `kws init stage: queue_storage_ready ...`
  - `kws init stage: signal_ready ...`
  - `kws init stage: task_ready ...`
- after successful KWS bring-up, the existing FP32 runtime logs from Step 5.67 should still appear:
  - `kws tensor io ... effective_in=float32 effective_out=float32`
  - `kws alloc ... arena_used=... arena_slack=...`
  - `kws perf: infer_us[last=... avg=... max=...] ...`

Observed result from the first `768KB` boot attempt on `2026-04-02`:
- boot reached the voice stack and then failed before KWS runtime was ready
- the decisive line was:
  - `Malloc failed. Core:[CA32], Task:[NoTsk], [free heap size: 771904] [xWantedSize:786560]`
- conclusion:
  - the board is no longer limited to the earlier sub-`256KB` KWS budget
  - but `768KB` is too aggressive for the current early-boot DRAM allocation window

Observed build result for the reduced-arena retry on `2026-04-02`:
- full `RTL8730E` rebuild passed after reducing `CONFIG_RIVER_KWS_TENSOR_ARENA_KB` to `688`
- this retry also includes the new staged KWS init allocation logs

## Step 5.69 Verification
Build:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
```

Flash:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
```

Expected boot/runtime emphasis:
- if FP32 KWS still fails during init, the boot log must now include a detailed `kws io binding` line before the failure
- if the failure persists, the follow-up `kws tensor data invalid` line must now preserve the raw pointer and allocation metadata instead of only printing `input_data/output_data`

Capture these two lines together:
```text
kws io binding: preserve_all=... input_idx=... type=... alloc=... bytes=... raw=... dims=... var=... output_idx=... type=... alloc=... bytes=... raw=... dims=... var=... arena_used=... arena_slack=...
kws tensor data invalid: input_data=... output_data=... input_raw=... output_raw=... input_alloc=... output_alloc=... input_bytes=... output_bytes=... input_idx=... output_idx=... arena_used=... arena_slack=...
```

Interpretation guide:
- `alloc=dynamic` on input or output:
  - next suspect becomes dynamic-tensor semantics or export/runtime incompatibility rather than plain arena exhaustion
- `alloc=arena_rw` but `raw=NULL`:
  - next suspect becomes TFLM planner/binding behavior on this model/runtime combination
- `raw!=NULL` but `input_data/output_data=NULL`:
  - next suspect becomes project-side typed-pointer resolution rather than allocator failure
- `arena_used` is unexpectedly tiny:
  - next suspect becomes planner not committing expected activation buffers
- `arena_used` is near the configured cap and `arena_slack` is near zero:
  - next suspect becomes marginal arena sizing or a planner edge case under pressure

Observed build result on `2026-04-03`:
- full `RTL8730E` rebuild passed after adding the new FP32 KWS I/O binding diagnostics

## Step 5.70 Verification
SDK patch check:
```bash
cd /root/ameba-river
python3 tools/sdk/apply_rtl8730e_memory_layout_patch.py --check
```

Expected result:
- prints `applied`

Build:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
```

Expected build result:
- the build completes successfully with `Build done`
- `ATF`, `CA32`, `KM4`, and `KM0` all rebuild without new layout-related linker or TrustZone errors

Recommended board-side check after flashing:
```text
look for:
- PSRAM or DRAM End in layout is 0x60C00000, but actually is 0x64000000
- kws init plan: ...
- boot_ready heap_free=...
- silero_vad runtime ready: ...
```

Expected runtime direction:
- `boot_ready heap_free` should be higher than the pre-patch baseline
- `kws init` should no longer leave only about `10KB` of free heap
- the previous `Malloc failed ... xWantedSize:105536` should either disappear or move later, which would confirm that the original blocker was the `CA32` carveout size rather than the KWS model structure itself

Observed result on `2026-04-03`:
- the conservative SDK memory-layout patch was applied successfully
- the full local `RTL8730E` rebuild passed after the patch

## Step 5.71 Verification
Build:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
```

Observed build result on `2026-04-03`:
- the full `RTL8730E` rebuild passed after lowering the KWS threshold and increasing the inference stride
- the build finished with `Build done`

Flash:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
```

Expected boot/runtime emphasis:
- the memory-layout expansion evidence should remain visible:
  - `PSRAM or DRAM End in layout is 0x60C00000, but actually is 0x64000000`
  - `kws init plan: heap_free=...` should stay in the multi-megabyte range
  - the old `Malloc failed ... xWantedSize:105536` should stay gone
- `kws status` should now report the lower smoke-test threshold:
  - `thresh_pm=48` or `thresh_pm=49`
- FP32 inference time will likely stay near `177ms`, but queue growth should be materially lower than the earlier `peak=27/64` case because `stride=16`
- if the end-to-end wake path is fundamentally healthy, repeated wake-word tries may now produce at least one of these logs:
  - `wakeword hit: text=...`
  - `wakeword queued text=...`
  - `interaction_state: wake_monitoring -> ... reason=wakeword_detected`

If no trigger occurs, capture these lines together:
```text
kws status: ... thresh_pm=... gate_best_pm=... queue=... peak=...
kws perf: infer_us[last=... avg=... max=...] ... queue[frames=64 stride=16]
```

Interpretation:
- `gate_best_pm` crosses about `49` and a `wakeword hit` appears:
  - the end-to-end board runtime works, so the next task is to replace this smoke threshold with proper model/frontend tuning
- `gate_best_pm` stays below about `49`:
  - the next blocker is model/frontend score distribution on board, not heap layout anymore
- queue growth is still aggressive even with `stride=16`:
  - the next blocker is CA32 compute budget / model cost, not threshold alone

## Step 5.72 Verification
Build:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
```

Flash:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
```

Expected boot/runtime emphasis:
- the backend line should confirm the new timing profile:
  - `stride=8`
  - `pre_roll_flush=16`
- the Silero init line should confirm the longer gate hold:
  - `hangover=18`
- when speaking the wake phrase, pre-roll trimming should reduce or disappear relative to the prior:
  - old behavior: `kws pre-roll trim: dropped=12 keep=8/20`
  - new target: either no trim or at most `dropped=4 keep=16/20`
- queue pressure should remain manageable even though inference cadence increases:
  - queue peaks materially below saturation
  - no `kws input trim:` log
- success criteria for this timing-debug step:
  - at least one `wakeword hit: text=...`
  - or, if still no trigger, `gate_best_pm` must move materially above the prior `11pm` ceiling so the next blocker is clearly narrowed to model/frontend score distribution rather than board-side timing

Capture these lines together after several wake-word attempts:
```text
kws backend: ... stride=8 ... pre_roll_flush=16 ...
silero_vad runtime ready: ... hangover=18 ...
kws pre-roll trim: ...
kws status: ... gate_best_pm=... thresh_pm=48 ... queue=... peak=...
kws perf: infer_us[last=... avg=... max=...] ... queue[frames=64 stride=8]
```

## Step 5.73 Verification
Build:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
```

Observed build result on `2026-04-03`:
- the full `RTL8730E` rebuild passed after adding KWS peak-window logging and log throttling
- the build finished with `Build done`

Flash:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
```

Expected boot/runtime emphasis:
- wake behavior should stay on the already-proven smoke profile:
  - `stride=8`
  - `thresh_pm=48`
- periodic KWS heartbeat should slow down to about every `5 s`:
  - `kws status: ...`
  - `kws perf: infer_us[last=... avg=... max=... win=...] ...`
- repeated slow-inference warnings should become much less noisy:
  - `kws infer slow:` should not print on every inference anymore
  - another slow log is expected only after a larger latency regression or after the longer heartbeat interval
- new transient diagnostics should appear only when warranted:
  - `kws peak: reason=score ...`
  - `kws peak: reason=infer ...`
  - `kws peak: reason=queue ...`
  - `kws peak: reason=trigger ...`

Recommended capture after several wake attempts:
```text
kws peak: ...
kws infer slow: ...
kws status: ...
kws perf: ...
wakeword hit: ...
```

Interpretation:
- `kws peak` shows `reason=infer` together with growing `queue` or `heap_low` pressure:
  - the next bottleneck is still compute budget / runtime scheduling, not wake threshold
- `kws peak` mostly shows `reason=score` or `gate_best` while latency stays flat:
  - the next bottleneck is more likely frontend/model score distribution than runtime jitter
- wake still fires and the new logs stay sparse:
  - this logging step succeeded and can be kept for longer board captures

## Step 5.74 Verification
Build:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
```

Observed build result on `2026-04-03`:
- the full `RTL8730E` rebuild passed after suppressing duplicate trigger-side peak logs
- the build finished with `Build done`

Flash:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
```

Expected runtime emphasis:
- on a successful wake, the log should still contain:
  - `kws peak: reason=score ...` or another meaningful peak reason
  - `wakeword hit: ...`
- but it should no longer immediately follow with another redundant:
  - `kws peak: reason=trigger ...`
  when that trigger happens inside the same just-logged peak window

Recommended capture:
```text
kws peak: ...
wakeword hit: ...
```

Success criterion:
- successful wake events still show one meaningful `kws peak` line plus `wakeword hit: ...`
- the former back-to-back `score` then `trigger` duplicate peak pair disappears

## Step 5.75 Verification
Build:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
```

Observed build result on `2026-04-03`:
- the full `RTL8730E` rebuild passed after resetting each peak window to a clean score/infer baseline
- the build finished with `Build done`

Flash:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
```

Observed flash result on `2026-04-03`:
- the board image download completed successfully on `/dev/ttyUSB0`
- the flash tool finished with `PASS`

Expected runtime emphasis:
- after one successful wake, a later unrelated `kws peak: reason=queue ...` line should no longer report the old wake score as its peak score
- each `kws peak` line should now reflect only the current window's:
  - `score_pm`
  - `gate_best_pm`
  - `infer_us`
  - queue/pre-roll/heap peaks

Recommended capture:
```text
kws peak: ...
wakeword hit: ...
kws peak: reason=queue ...
```

Success criterion:
- a later queue-only peak no longer carries forward the previous wake's `score_pm` / `gate_best_pm`
- the peak window contents are self-consistent across successive gates

## Step 5.76 Verification
Build:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
```

Observed build result on `2026-04-03`:
- the full `RTL8730E` rebuild passed after adding backup-register reset breadcrumbs
- the build finished with `Build done`

Flash:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
```

Observed flash result on `2026-04-03`:
- the board image download completed successfully on `/dev/ttyUSB0`
- the flash tool finished with `PASS`

Boot log capture:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Observed runtime result on `2026-04-03` after a manual monitor `reboot`:
- boot ROM/loader reported `KM4 BOOT REASON 400: APSYS`
- app log reported `reset trace previous: boot_reason=0x0400 state=wake_monitoring reason8=network_ uptime_ds=62`
- app status dump reported `reset_trace=armed state=wake_monitoring reason8=boot_rea uptime_ds=0`

Expected runtime emphasis:
- when the board later hits another unexpected reboot without a panic, the next boot should print the last interaction phase recorded before reset
- this should help distinguish whether the reset happened during:
  - wake monitoring
  - wake confirmed / ASR
  - playback / follow-up
  - error recovery

Recommended capture:
```text
[BOOT-I] KM4 BOOT REASON ...
[river.reset] reset trace previous: ...
[river.reset] reset_trace=armed ...
```

Success criterion:
- next-boot logs consistently include the previous recorded interaction phase
- manual `reboot` proves the breadcrumb survives at least software/AP-triggered resets

## Step 5.77 Verification
Build:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
```

Observed build result on `2026-04-03`:
- the full `RTL8730E` rebuild passed after adding websocket queue watermarking and xiaozhi uplink backoff
- the build finished with `Build done`

Flash:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
```

Observed flash result on `2026-04-03`:
- the board image download completed successfully on `/dev/ttyUSB0`
- the flash tool finished with `PASS`

Boot/runtime capture:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Recommended runtime emphasis:
- trigger a few wake + follow-up rounds like the earlier failure case
- watch specifically for these lines:

```text
[river.cloud.xiaozhi] xiaozhi ws backpressure: kind=audio ready=... recycle=... max=8 reserve=2
[river.cloud] xiaozhi uplink backpressure: queued=... busy=... streak=... backoff=... stale_drop=...
[river.voice.probe] ... stream_busy=...
```

Observed runtime result on `2026-04-03` after flashing:
- new project-side backpressure logs appeared as expected, for example:
  - `xiaozhi ws backpressure: kind=audio ready=6 recycle=0 max=8 reserve=2`
  - `xiaozhi uplink backpressure: queued=5/64 busy=72 streak=8 backoff=160ms stale_drop=106`
- the sampled monitor window did not show the old SDK spam:
  - `WSCLIENT ERROR] ws_sendData: ERROR: Not get usable buffer...`
- the sampled monitor window did not show:
  - `xiaozhi playback write failed`
- `river.voice.probe` continued to report `stream_busy=0` in the captured session

Success criterion:
- websocket congestion is reported through the new project-side logs instead of repeated SDK queue-full errors
- `stream_busy` should stay near `0` or materially lower than before during similar speech/playback windows
- follow-up rounds should avoid the earlier pattern of:
  - `ws_sendData ... Not get usable buffer`
  - `xiaozhi uplink send failed: status=-6`
  - `xiaozhi playback write failed`

## Step 5.78 Verification
This is a documentation-only step.

Review commands:
```bash
cd /root/ameba-river
sed -n '1,320p' doc/RTL8730E_LONG_TERM_MODEL_CONSTRAINTS_ZH.md
git diff --check
```

Expected result:
- the new document exists and is readable
- it clearly distinguishes:
  - physical hardware limits
  - current conservative memory/flash layout
  - current measured runtime usage
  - recommended long-term model budgets
- it explicitly states that the current branch's bottlenecks are not automatically long-term physical limits
- it gives concrete planning guidance for:
  - flash budget
  - runtime working-set budget
  - compute budget
  - when a larger `CA32` carveout or dedicated profile should be considered
- `git diff --check` reports no patch-format errors

## Step 5.79 Verification
Review commands:
```bash
cd /root/ameba-river
git diff -- components/river_voice/CMakeLists.txt components/river_voice/river_voice_detector_silero.cc
nl -ba components/river_voice/CMakeLists.txt | sed -n '1,120p'
nl -ba components/river_voice/river_voice_detector_silero.cc | sed -n '136,176p'
```

Expected review result:
- `components/river_voice/CMakeLists.txt` does not locally enable `TF_LITE_STATIC_MEMORY`
- `components/river_voice/river_voice_detector_silero.cc` keeps the guarded fallback-name logic in the tensor dump helper

Build:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
```

Observed build result on `2026-04-03`:
- the full `RTL8730E` rebuild passed after removing the partial `TF_LITE_STATIC_MEMORY` define
- the build finished with `Build done`

Git state checks:
```bash
cd /root/ameba-river
git status --short --branch
git log -1 --oneline
git branch --show-current
```

Expected result after the step:
- the review cleanup is recorded in the latest commit
- the current branch is `agent_server`
- the worktree is clean

## Step 5.80 Verification

Algorithm-side strict no-leakage split sanity check:

```bash
cd /root/ameba-river
python3 - <<'PY'
import sys
sys.path.insert(0, '/root/kws-training-pro')
from kws_data_split import load_manifest_items, split_items_by_origin
items = load_manifest_items('/root/kws-dataset-pro-blueprint/data/train_manifest.jsonl')
items = [item for item in items if item.get('source') == 'device_recordings']
train_items, val_items, summary = split_items_by_origin(items, val_ratio=0.1, seed=7)
print(summary)
PY
```

Expected result:
- `shared_groups` is `[]`
- `train_groups=105`
- `val_groups=12`
- positive and negative buckets both retain non-zero train/val groups

Syntax check without writing `__pycache__` into the algorithm repo:

```bash
cd /root/ameba-river
python3 - <<'PY'
from pathlib import Path
for path in [
    '/root/kws-training-pro/kws_data_split.py',
    '/root/kws-training-pro/train_v2.py',
    '/root/kws-training-pro/validate_final.py',
]:
    compile(Path(path).read_text(encoding='utf-8'), path, 'exec')
print('compile_ok')
PY
```

Expected result:
- prints `compile_ok`

Strict holdout evaluation entrypoint:

```bash
cd /root/kws-training-pro
python3 validate_final.py \
  --model models/bc_resnet_iteration3/bc_resnet_best.onnx \
  --manifest /root/kws-dataset-pro-blueprint/data/train_manifest.jsonl \
  --source device_recordings \
  --val-ratio 0.1 \
  --split-seed 7 \
  --threshold 0.6
```

Expected result:
- first prints `Strict grouped holdout: ...`
- shows bucket stats for `device_recordings/positive` and `device_recordings/negative`
- runs threshold sweep only on the grouped holdout set, not on the full `5850` pool

Note:
- the default shell `python3` on this machine currently lacks `onnxruntime`, so the last command should be run inside the usual algorithm environment that already satisfies the old script dependencies

## Step 5.81 Verification

Regenerate the compiled-in alignment sample header:

```bash
cd /root/ameba-river
python3 tools/kws/generate_alignment_sample_header.py
```

Expected result:
- prints `generated /root/ameba-river/components/river_voice/generated/river_kws_alignment_sample_data.h`
- reports `samples=37120`, `frames=145`, `lead_silence_frames=20`

Full project build:

```bash
cd /root/ameba-river
source env.sh
ameba.py soc RTL8730E
ameba.py build -p
```

Expected result:
- the full external-project build succeeds
- final line contains `Build done`

Board-side monitor procedure for deterministic KWS replay:

```text
river audio probe stop
river kws align status
river kws align run
```

Preconditions:
- the board must already have booted back into idle wake-monitoring
- do not run this while the device is still inside an active xiaozhi conversation / follow-up window

Expected runtime behavior:
- `river kws align status` prints:
  - compiled sample frame count and duration
  - current probe state
  - current interaction state
  - whether KWS worker is idle
- `river kws align run` prints:
  - `kws align replay start: source=compiled_pcm ...`
  - `kws align replay captured: seq=... infer=...`
  - one full exact tensor dump stream:
    - `kws tensor dump begin: ...`
    - `kws tensor dump meta: ...`
    - repeated `kws tensor dump feat_f32: ...`
    - repeated `kws tensor dump input_raw: ...`
    - repeated `kws tensor dump output_raw: ...`
  - `kws align replay done: dump=emitted ...`

Failure interpretation:
- if monitor prints `kws align requires probe stopped`, run `river audio probe stop` first
- if monitor prints `kws align requires idle wake monitoring`, wait until the device leaves follow-up / active session state and retry

Host-side exact replay check from the captured monitor log:

```bash
cd /root/ameba-river
python3 tools/kws/replay_board_tensor_dump.py \
  --model /root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_v3_fp32.tflite \
  --log /path/to/monitor.log
```

Expected result:
- the parser finds one complete dump record
- the replay tool reports matching or near-matching feature/input hashes and scalar output for the current `fp32_experimental` board model

Note:
- the replay command intentionally clears the in-memory snapshot after serial emission, so the serial log itself is the artifact to preserve for host-side comparison

## Step 5.82 Verification

Full project rebuild:

```bash
cd /root/ameba-river
source env.sh
ameba.py soc RTL8730E
ameba.py build -p
```

Expected result:
- the full external-project build succeeds
- final line contains `Build done`

Board-side monitor procedure after flashing the new image:

```text
river audio probe stop
river kws align status
river kws align run
```

Expected runtime behavior when boot-time KWS did not come up:
- `river kws align status` prints:
  - `kws=closed`
  - `probe=stopped`
  - `interaction=wake_monitoring`
  - `kws align hint: local kws runtime is closed; ... let river kws align run retry lazy init`
- `river kws align run` then prints one of:
  - success path:
    - `kws align lazy init: current_state=closed`
    - `kws align lazy init ok`
    - followed by the normal replay / dump logs from Step `5.81`
  - failure path:
    - `kws align lazy init: current_state=closed`
    - `kws align lazy init failed: status=...`
    - `[river][diag] kws align run failed status=...; check KWS logs above`

Failure interpretation:
- if lazy init succeeds, the original `kws=closed` blocker is fixed and the replay path should proceed normally
- if lazy init fails with a concrete status code, collect the surrounding KWS init log because the next debugging target is the real KWS init failure, not the align command itself

## Step 5.83 Verification

Full project rebuild:

```bash
cd /root/ameba-river
source env.sh
ameba.py soc RTL8730E
ameba.py build -p
```

Expected result:
- the full external-project build succeeds
- final line contains `Build done`

Optional host-side sanity check after configure/build:

```bash
cd /root/ameba-river
python3 - <<'PY'
import json
with open('build_RTL8730E/build/compile_commands.json') as f:
    data = json.load(f)
for suffix in [
    'components/river_voice/river_voice_kws.cc',
    'components/river_voice/river_voice_detector_silero.cc',
]:
    cmd = next(item['command'] for item in data if item['file'].endswith(suffix))
    print(suffix, 'TF_LITE_STATIC_MEMORY' in cmd)
PY
```

Expected result:
- both lines print `True`

Board-side boot/runtime verification after flashing:

Watch boot log for KWS init:
- no longer expect:
  - `kws io binding: ... type=none alloc=unknown raw=0x0 dims=0x0 ...`
  - `kws tensor data invalid`
- instead expect valid binding similar to:
  - `kws io binding: ... type=float32 alloc=arena_rw ... raw=0x... dims=0x...`
  - followed by normal KWS init stages and `kws status: ... ready=yes ...`

Then run:

```text
river audio probe stop
river kws align status
river kws align run
```

Expected runtime behavior:
- if boot-time KWS already initialized successfully:
  - `river kws align status` should show `kws=ready`
  - `river kws align run` should proceed directly into replay / tensor dump
- if boot-time KWS is still closed for some other reason:
  - `river kws align run` should at least no longer fail with the old invalid-tensor signature
  - collect the new init logs because the previous ABI-mismatch failure mode should be gone

## Step 5.84 Verification

Full project rebuild:

```bash
cd /root/ameba-river
source env.sh
ameba.py soc RTL8730E
ameba.py build -p
```

Expected result:
- the full external-project build succeeds
- final line contains `Build done`

Board-side monitor procedure after flashing:

```text
river audio probe stop
river kws align run
```

Expected runtime behavior:
- the board should now print:
  - `kws tensor dump armed: mode=align_best`
  - `kws align replay start: source=compiled_pcm ...`
  - one or more `kws tensor dump captured: ... mode=align_best ...` lines as replay score improves
  - final `kws align replay captured: seq=... infer=... score=... q15=...`
  - the usual full `feat_f32` / `input_raw` / `output_raw` dump stream
- the final captured score should no longer be the early low-score frame seen before this step
  - previous bad reference was `score=0.002818 q15=92`
  - the new captured score should be materially higher and should be close to the replay peak / wake-word hit frame

Manual interpretation:
- if `kws align replay captured` is now near the peak hit frame, the board-vs-host exact replay artifact is usable
- if the final captured score is still stuck near the old low-score level, collect the full replay log because the remaining bug would then be in the peak-selection policy rather than in runtime init or ABI setup

## Step 5.85 Verification

No new firmware build is required for this step if the board is already running the Step `5.84` image.

Full monitor capture for one alignment replay:

```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000 | tee /tmp/kws_align_full.log
```

Board-side monitor commands:

```text
river audio probe stop
river kws align run
```

Capture requirements:
- do not stop the monitor early
- keep logging until the board prints:
  - `kws align replay done: dump=emitted ...`
- the saved log must contain one complete dump sequence:
  - one `kws tensor dump begin: ...`
  - one `kws tensor dump meta: ...`
  - all `feat_f32 chunk=1/245 ... 245/245`
  - all `input_raw chunk=1/245 ... 245/245`
  - one `output_raw chunk=1/1 ...`

Host-side exact replay using the board-matching FP32 model:

```bash
cd /root/ameba-river
python3 tools/kws/replay_board_tensor_dump.py \
  --model /root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_v3_fp32.tflite \
  --log /tmp/kws_align_full.log
```

Expected replay output:
- the script should not fail with `dump seq=... incomplete`
- `board_hash:` should show:
  - `feature=0x63dd772f`
  - `input=0x3ec7297e`
- `host_hash:` should match the same feature/input hashes
- `board_output:` should show:
  - `raw=911`
  - `score=0.910520`
  - `q15=29835`
- `host_output:` should be the same board-equivalent score path
- `output_parity:` should ideally report:
  - `bytes_equal=yes`
  - `raw_equal=yes`

Known bad artifact:
- `/tmp/kws_align_dump_20260404_140825.log` is incomplete and should not be used for this step
- its first failed replay attempt ended with:
  - `ValueError: dump seq=1 incomplete: feat_f32, input_raw, output_raw`

## Step 5.86 Verification

Board-state diagnostic using the standard user-confirmed monitor command:

```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/tools/scripts/monitor.py \
  -p /dev/ttyUSB0 \
  -b 1500000 \
  -reset \
  -debug \
  --log \
  --log-dir /tmp/kws_monitor_reset
```

Expected healthy behavior:
- monitor connects successfully
- after the built-in `AT+LIST` or `reboot` probe, the board should emit readable text
- at minimum one of these should appear:
  - command-list reply
  - `BOOT-I`
  - `ROM:[`
  - normal project boot logs

Observed blocker on `2026-04-04`:
- monitor connected successfully to `/dev/ttyUSB0` at `1500000`
- the tool sent:
  - `AT+LIST\r\n`
  - `reboot\r\n`
- RX side returned only repeated `0x00` bytes:

```text
[Sent Data (Hex)]: 41 54 2B 4C 49 53 54 0D 0A
[Received Data (Hex)]: 00 00 00 00 ...
Failed to get cmd list: Get cmd list expired
[Sent Data (Hex)]: 72 65 62 6F 6F 74 0D 0A
[Received Data (Hex)]: 00 00 00 00 ...
```

Interpretation:
- do not proceed to `river kws align run` or host replay while the board stays in this state
- first restore the board to a readable text monitor state
- only after serial output returns to normal text logs should Step `5.85` be retried

## Step 5.87 Verification

Board interactivity check at the confirmed baudrate `1500000`:

```bash
python3 - <<'PY'
import time
import serial

ser = serial.Serial('/dev/ttyUSB0', 1500000, timeout=0.2)
try:
    ser.write(b'\r')
    ser.flush()
    time.sleep(0.5)
    print(ser.read(256).decode('utf-8', errors='ignore'))
    ser.write(b'river kws align status\r')
    ser.flush()
    time.sleep(1.0)
    print(ser.read(4096).decode('utf-8', errors='ignore'))
finally:
    ser.close()
PY
```

Expected behavior:
- the first probe returns `#`
- `river kws align status` returns readable KWS alignment status text

Host replay on the captured live log:

```bash
cd /root/ameba-river
python3 tools/kws/replay_board_tensor_dump.py \
  --model /root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_v3_fp32.tflite \
  --log /tmp/kws_align_full_20260404_live.log
```

Expected output on the current live capture:
- `board_hash:` should show:
  - `feature=0x63dd772f`
  - `input=0x3ec7297e`
- `host_hash:` should show:
  - `feature=0x63dd772f`
  - `effective_input=0x3ec7297e`
  - `source=feat_f32_fallback`
- `board_output:` should show:
  - `raw=911`
  - `score=0.910520`
  - `q15=29835`
- `output_parity:` should report:
  - `bytes_equal=yes`
  - `raw_equal=yes`

Important interpretation:
- if the tool reports `source=feat_f32_fallback`, that means:
  - the emitted `input_raw` bytes in this board log are not self-consistent with `input_hash`
  - but the raw `feat_f32` bytes are self-consistent with `input_hash`
  - replay result is still valid because the effective model input is reconstructed from the hash-matching float32 feature tensor

## Step 5.88 Verification

Rebuild the firmware:

```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
```

Expected build result:
- final line contains `Build done`

Flash the rebuilt image:

```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
```

Expected flash result:
- the tool reports `Finished PASS`

Capture a fresh live alignment dump:

```bash
cd /root/ameba-river
bash -lc "stty -F /dev/ttyUSB0 1500000 raw -echo && cat /dev/ttyUSB0 > /tmp/kws_align_full.log"
```

In another shell, send the probe and alignment commands:

```bash
bash -lc "printf '\r' > /dev/ttyUSB0"
bash -lc "printf 'river kws align status\r' > /dev/ttyUSB0"
bash -lc "printf 'river audio probe stop\r' > /dev/ttyUSB0"
bash -lc "printf 'river kws align run\r' > /dev/ttyUSB0"
```

After the dump completes, stop the capture process:

```bash
pkill -f "cat /dev/ttyUSB0 > /tmp/kws_align_full.log"
```

Sanity-check that the exported input tensor is no longer corrupted:

```bash
rg -n \
  "kws tensor dump feat_f32: seq=1 chunk=1/245|kws tensor dump input_raw: seq=1 chunk=1/245|kws tensor dump output_raw: seq=1 chunk=1/1" \
  /tmp/kws_align_full.log
```

Expected sanity-check result:
- `feat_f32 chunk=1/245` hex exactly equals `input_raw chunk=1/245`
- `output_raw chunk=1/1` remains `df17693f`

Replay the captured board dump on host:

```bash
cd /root/ameba-river
python3 tools/kws/replay_board_tensor_dump.py \
  --model /root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_v3_fp32.tflite \
  --log /tmp/kws_align_full.log
```

Expected replay result after this fix:
- `board_hash:` should show:
  - `feature=0x63dd772f`
  - `input=0x3ec7297e`
- `host_hash:` should show:
  - `feature=0x63dd772f`
  - `logged_input=0x3ec7297e`
  - `effective_input=0x3ec7297e`
  - `source=input_raw`
- `quant_parity:` should report:
  - `diff_bytes=0/15680`
  - `first_diff=[]`
- `board_output:` should show:
  - `raw=911`
  - `score=0.910520`
  - `q15=29835`
- `output_parity:` should report:
  - `bytes_equal=yes`
  - `raw_equal=yes`

## Step 5.89 Verification

This is a documentation-only step. No firmware rebuild is required.

Verify that the runtime profiling report exists and includes the expected sections and key metrics:

```bash
cd /root/ameba-river
rg -n \
  "双核平均忙碌率|KWS 预留工作集|平均超预算|echo.*0 B|bc_resnet_v3_fp32_experimental|xiaozhi connecting" \
  doc/RUNTIME_RESOURCE_PROFILE_2026-04-04_ZH.md
```

Expected result:
- the file `doc/RUNTIME_RESOURCE_PROFILE_2026-04-04_ZH.md` exists
- the grep output includes at least these documented points:
  - dual-core average busy rate `9.0%`
  - KWS reserved working set `816.4 KiB`
  - KWS average over-budget latency `50.764 ms`
  - `echo` stack free `0 B`
  - model variant `bc_resnet_v3_fp32_experimental`
  - cloud reconnect timing around `xiaozhi connecting`

## Step 5.90 Verification

Build:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
```

Flash:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
```

Boot-log check:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Then send:
```text
reboot
```

Expected runtime evidence from the boot log:
- KWS backend log shows the lower threshold is live:
```text
kws backend: ... threshold_q15=1024 ...
```
- KWS status log reflects the lower permille threshold after init:
```text
kws status: ... thresh_pm=31 weak_pm=31 ...
```

Expected playback-gain evidence when XiaoZhi TTS downlink starts:
```text
xiaozhi playback start: ... gain=5/2
```

Current board-side note from this run:
- Build passed.
- Flash passed.
- The boot log did confirm `threshold_q15=1024` and `thresh_pm=31 weak_pm=31`.
- The `xiaozhi playback start: ... gain=5/2` line was not observed in this run because
  the boot sequence reported `url_set=no token_set=no`, so no cloud TTS playback
  session started during the verification window.

## Step 5.91 Verification

Build:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
```

Flash:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
```

Boot-log check:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Then send:
```text
reboot
```

Expected runtime evidence from the boot log:
- The KWS backend line still shows the lowered threshold:
```text
kws backend: ... threshold_q15=1024 ...
```
- The performance line shows the new runtime stride:
```text
kws perf: ... queue[frames=64 stride=16 ...]
```

Target board-side retest after flashing:
- Repeat the same normal-speed wakeword test that previously produced:
  - queue growth into the `40+ / 64` range
  - `kws input trim: dropped=27`
  - a delayed wake hit only after several tries
- Expected improvement from this step:
  - KWS queue grows more slowly
  - trim frequency drops or disappears on short wake attempts
  - wake hits, if they happen, should arrive closer to the speaking window

Current board-side note from this run:
- Build passed.
- Flash passed.
- Boot log confirmed both `threshold_q15=1024` and `queue[frames=64 stride=16]`.
- Voice wake retest is still needed on the physical board because this step is
  meant to improve live timing under speech, not just boot-time configuration.

## Step 5.92 Verification

Build:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
```

Flash:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 460800 -m nor
```

Board debug monitor:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

Expected runtime evidence after boot / wake testing:
- The KWS status line shows the lower threshold is active:
```text
kws status: ... thresh_pm=11 weak_pm=11 ...
```
- The KWS perf line keeps the real-time-oriented stride:
```text
kws perf: ... queue[frames=64 stride=16 ...]
```
- A valid wake attempt can now cross threshold without the earlier queue-runaway
  behavior, for example:
```text
wakeword hit: text=小欧管家 score_pm=28 ...
```

Observed board-side result from the supplied runtime log:
- `stride=16` kept the queue bounded in the sampled windows (`8-10 / 64`, peak
  `14`) instead of the earlier `40+ / 64` buildup.
- The lower threshold was active (`thresh_pm=11 weak_pm=11`).
- Several short attempts still stayed below threshold (`gate_best_pm=4`, `8`),
  but a later natural wake attempt reached `score_pm=28` and triggered
  successfully.
- The device then entered the expected wake flow:
  - `wakeword queued`
  - `xiaozhi connecting`
  - `server hello`
  - follow-up ASR/TTS exchange

Current flashing note:
- This step's firmware image was built successfully.
- Flashing at `1500000` intermittently failed with `b'\\xe2'` during the large
  app image transfer.
- The successful deployment for this verification used
  `python3 tools/river_flash.py -p /dev/ttyUSB0 -b 460800 -m nor`.
- The board's normal debug monitor baud remains `1500000`.

## Step 5.93 Verification

Repository snapshot checks:
```bash
cd /root/ameba-river
git status --short
git rev-parse --short HEAD
git tag --list m7-realtime-wake-threshold-tuned
git show --no-patch --oneline m7-realtime-wake-threshold-tuned
```

Expected result:
- `git status --short` prints nothing, confirming a clean worktree
- `git tag --list ...` prints `m7-realtime-wake-threshold-tuned`
- `git show --no-patch --oneline ...` resolves to the archived snapshot commit

Scope note:
- This is a repository-hygiene step only.
- It does not change the board debug monitor settings; the normal monitor command
  remains `ameba.py monitor -p /dev/ttyUSB0 -b 1500000`.

## Step 5.94 Verification

Repository rule check:
```bash
cd /root/ameba-river
rg -n "Wakeword Debugging Discipline|board-side vs local comparison|tensor dumps|alignment replay|explicit user approval" AGENTS.md
git diff -- AGENTS.md .codex/changes.md .codex/verification.md
```

Expected result:
- `rg` shows the new persistent wakeword-debugging rule block in `AGENTS.md`
- `git diff -- ...` shows only documentation changes for this step

Scope note:
- This step changes repository guidance only.
- It does not modify firmware behavior, model artifacts, flashing flow, or the
  board debug monitor configuration.

## Step 5.95 Verification

Committed default build check:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py soc RTL8730E
python3 /root/ameba-rtos-1.2/ameba.py build -p
```

Expected result:
- the build completes with `Build done`
- the committed config remains on
  `CONFIG_RIVER_KWS_MODEL_VARIANT_FP32_EXPERIMENTAL=y`
- this step does not require any serial-debug command changes

Temporary local-only student FP32 smoke build:
```text
Temporarily flip only these two lines in prj.conf:
- set `# CONFIG_RIVER_KWS_MODEL_VARIANT_FP32_EXPERIMENTAL is not set`
- set `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_TINY_V2_FP32_DEBUG=y`
```

```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py soc RTL8730E
python3 /root/ameba-rtos-1.2/ameba.py build -p
```

```text
Immediately restore prj.conf after the smoke build:
- set `CONFIG_RIVER_KWS_MODEL_VARIANT_FP32_EXPERIMENTAL=y`
- set `# CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_TINY_V2_FP32_DEBUG is not set`
```

Expected result:
- the temporary student build also completes with `Build done`
- switching between the two variants requires only model-selection changes; no
  KWS source edits, serial-debug changes, or parity-tool removals are needed

Later board-side spot check when the temporary student build is flashed:
```text
kws backend: ... fft=400 hop=160 center=yes ...
kws frontend: ... bins=40 frames=101 log=natural norm=per_clip_mean_std ...
```

Scope note:
- This step verifies compile/link integration for the parallel variant.
- Board/local replay parity, threshold calibration, and on-device quality
  judgment for the student branch should be handled as the next separate
  runtime step.

## Step 5.96 Verification

Board-side exact tensor parity on the currently flashed firmware:
```text
1. Capture boot log and confirm the flashed model branch from the board itself.
2. Run:
   - river kws debug local on
   - river audio probe stop
   - river kws dump next
   - river kws align run
3. After the dump finishes, replay the serial log on host.
4. Restore runtime state:
   - river kws debug local off
   - river audio probe start
```

Host replay command used in this run:
```bash
cd /root/ameba-river
python3 tools/kws/replay_board_tensor_dump.py \
  --log /tmp/kws_exact_parity_serial.log \
  --model /root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_v3_fp32.tflite \
  --seq 2
```

Observed result from the board boot log:
- the flashed firmware is `bc_resnet_v3_fp32_experimental`
- the board is not currently running
  `student_bc_resnet_tiny_v2_fp32_debug`
- active frontend/runtime contract is still:
  - `input=40x98x1`
  - `fft=512`
  - `center=no`

Observed parity result for `seq=2`:
- board dump:
  - `feat_hash=0xb89e7474`
  - `input_hash=0x97354d89`
  - `raw=25`
  - `score=0.025023`
- host replay:
  - feature hash matches
  - logged input hash matches
  - `quant_parity: diff_bytes=0/15680`
  - `raw_equal=yes`
  - `host score=0.025000`
  - `bytes_equal=no`, first differing output byte index=`0`

Interpretation:
- the exact input tensor path is aligned between board and host
- the remaining float-output byte difference is tiny and does not change the
  decoded score bucket or `raw` value
- this validates the current mainline FP32 deployment path, not the student
  FP32 debug branch

Scope note:
- This step is a runtime-debug verification step only.
- It does not mean the student branch has been board-validated; that requires
  reflashing a firmware image built with
  `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_TINY_V2_FP32_DEBUG=y`.

## Step 5.97 Verification

Compile the student FP32 debug deployment image:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py soc RTL8730E
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
strings build_RTL8730E/km0_km4_ca32_app.bin | rg 'student_bc_resnet_tiny_v2_fp32_debug|bc_resnet_v3_fp32_experimental|per_clip_mean_std'
```

Expected result:
- the build completes with `Build done`
- `prj.conf` selects
  `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_TINY_V2_FP32_DEBUG=y`
- `strings ...` contains `student_bc_resnet_tiny_v2_fp32_debug`
- `strings ...` contains the student frontend log format with
  `norm=per_clip_mean_std`
- `strings ...` does not contain `bc_resnet_v3_fp32_experimental`

Observed compile result on `2026-04-07`:
- build completed successfully with `Build done`
- produced artifacts:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 4019552`
  - `build_RTL8730E/ota_all.bin 4019584`
- `strings build_RTL8730E/km0_km4_ca32_app.bin` contained:
  - `student_bc_resnet_tiny_v2_fp32_debug`
  - `kws frontend: source=fixed_dsb_mono feature=log_mel bins=%u frames=%u log=natural norm=per_clip_mean_std wake_text=%s`
- the same `strings` check did not return
  `bc_resnet_v3_fp32_experimental`

Scope note:
- This step verifies compile-time model selection only.
- It does not yet verify flashing or board/runtime parity for the student FP32
  debug branch.

## Step 5.98 Verification

Apply the larger CA32 debug layout in the external SDK:
```bash
cd /root/ameba-river
python3 tools/sdk/apply_rtl8730e_memory_layout_patch.py --variant aivoice_ca32_17mb
python3 tools/sdk/apply_rtl8730e_memory_layout_patch.py --variant aivoice_ca32_17mb --check
```

Build the student FP32 debug image with the enlarged tensor arena:
```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py soc RTL8730E
python3 /root/ameba-rtos-1.2/ameba.py build -p
stat -c '%n %s' build_RTL8730E/km4_boot_all.bin build_RTL8730E/km0_km4_ca32_app.bin build_RTL8730E/ota_all.bin
/opt/rtk-toolchain/asdk-10.3.1-4523/linux/newlib/bin/arm-none-eabi-nm -n \
  build_RTL8730E/build/project_ap/image/target_img2.axf | \
  rg '__psram_heap_buffer_size__|__psram_heap_buffer_start__|__non_secure_psram_end__|__ca32_fip_dram_start__'
```

Flash the rebuilt image:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 460800 -m nor
```

Board-side check from monitor:
```text
1. Wait for boot and Wi-Fi connection.
2. Confirm KWS is no longer closed:
   - river kws align status
3. Run the existing local-debug replay path:
   - river audio probe stop
   - river kws align run
   - river audio probe start
```

Expected result in the final fixed state:
- build completes with `Build done`
- artifact sizes remain:
  - `build_RTL8730E/km4_boot_all.bin 51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin 4019552`
  - `build_RTL8730E/ota_all.bin 4019584`
- CA32 image symbols show:
  - `__psram_heap_buffer_size__ = 0x00d78000`
  - `__psram_heap_buffer_start__ = 0x60688000`
  - `__non_secure_psram_end__ = 0x61500000`
  - `__ca32_fip_dram_start__ = 0x70300000`
- boot/runtime logs show the larger heap is available before KWS steady state
- `river kws align status` reports `kws=ready`
- `river kws align run` no longer prints `kws AllocateTensors failed`
- replay emits tensor dump chunks and ends with `kws align replay done`

Observed board result on `2026-04-07`:
- runtime after boot showed:
  - early `heap_free=13564928`
  - `wifi_connected heap_free=5091456`
  - `stack_free=[...,kws:11568B]`
- `river kws align status` reported:
  - `kws align guard: kws=ready probe=running interaction=wake_monitoring detection=ready`
- `river kws align run` reported:
  - `kws debug local_only: enabled=yes`
  - `kws align replay start: source=compiled_pcm frames=145 emit_dump=yes`
  - `kws tensor dump captured: seq=1 infer=1`
  - `wakeword hit: text=小欧管家 score_pm=371 q15=12163`
  - `kws perf: ... mem[arena=4709152/8192KB slack=3679456 ...]`
  - `kws align replay done: dump=emitted`

Interpretation:
- enlarging the CA32 layout was necessary to make large-arena experiments
  viable, but it was not sufficient by itself
- the student FP32 debug deployment was blocked specifically by the previous
  `688KB` tensor arena cap
- after raising the arena to `8192KB`, the existing board/local debug path
  works without changing the serial flow or removing any parity hooks

## Step 5.99 Verification

Confirm the embedded board model header matches the algorithm export exactly:
```bash
cd /root/ameba-river
python3 - <<'PY'
from pathlib import Path
import hashlib, re
header = Path('components/river_voice/generated/student_bc_resnet_tiny_v2_fp32_model_data.h').read_text()
values = [int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', header)]
data = bytes(values)
model = Path('/root/kws-trainint/artifacts/exports/student_bc_resnet_tiny_v2/model.fp32.tflite').read_bytes()
print('header_bytes', len(data))
print('header_sha256', hashlib.sha256(data).hexdigest())
print('model_bytes', len(model))
print('model_sha256', hashlib.sha256(model).hexdigest())
print('exact_match', 'yes' if data == model else 'no')
PY
```

Sanity-check and run the host replay against the preserved board dump:
```bash
cd /root/ameba-river
python3 -m py_compile tools/kws/replay_board_tensor_dump.py
python3 tools/kws/replay_board_tensor_dump.py \
  --log /tmp/kws_student_fp32_debug_replay.clean.log \
  --model /root/kws-trainint/artifacts/exports/student_bc_resnet_tiny_v2/model.fp32.tflite \
  --seq latest
```

Expected result for the current captured student FP32 replay:
- the model header and export report:
  - `header_bytes 411560`
  - `model_bytes 411560`
  - `header_sha256 2f21649bfbbbf69aae7f0fd1dc4ef318702cfd44c36fc1221c06ddcc215a4c51`
  - `model_sha256 2f21649bfbbbf69aae7f0fd1dc4ef318702cfd44c36fc1221c06ddcc215a4c51`
  - `exact_match yes`
- replay prints a tolerated transcript warning for the malformed serial chunk,
  but still succeeds by using the already-proven `feat_f32` raw bytes as the
  effective input:
  - `note: skipped malformed dump chunks: input_raw chunk=146/253 ...`
  - `source=feat_f32_missing_input_raw`
- parity result is exact at the output-byte level:
  - `board_hash: feature=0x7ce0b11d input=0xd52f011c`
  - `host_hash: feature=0x7ce0b11d ... effective_input=0xd52f011c`
  - `board_output: raw=371 score=0.371203 exact=0.371203 q15=12163`
  - `host_output: raw=371 score=0.371000 exact=0.371203`
  - `output_parity: bytes_equal=yes raw_equal=yes first_diff=[]`

Interpretation:
- the `score=0.371000` line on host is only the rounded `raw/1000`
  presentation of `raw=371`
- the decisive check is `exact=0.371203` plus `bytes_equal=yes`, which proves
  host replay and board output are byte-for-byte identical for this sample
- for this student FP32 debug branch, the deployment path is correct and the
  preserved board/local parity tooling remains usable even when the serial log
  wraps part of `input_raw`

## Step 5.100 Verification

Review the new guide and confirm it is indexed:
```bash
cd /root/ameba-river
sed -n '1,260p' doc/KWS_BOARD_HOST_PARITY_DEBUG_GUIDE_ZH.md
rg -n "KWS_BOARD_HOST_PARITY_DEBUG_GUIDE_ZH.md" doc/README.md
```

Check that all relative markdown links in the new guide resolve to real files:
```bash
cd /root/ameba-river
python3 - <<'PY'
from pathlib import Path
import re

root = Path('/root/ameba-river')
doc = root / 'doc/KWS_BOARD_HOST_PARITY_DEBUG_GUIDE_ZH.md'
text = doc.read_text(encoding='utf-8')
base = doc.parent
bad = []
for target in re.findall(r'\[[^\]]+\]\(([^)]+)\)', text):
    if '://' in target or target.startswith('#'):
        continue
    path = (base / target).resolve()
    if not path.exists():
        bad.append((target, str(path)))

print('broken_links', len(bad))
for target, path in bad:
    print(target, '->', path)
PY
```

Expected result:
- the new guide renders the full workflow, including:
  - mechanism overview
  - board commands
  - host replay flow
  - result interpretation
  - pitfalls and reporting template
- `doc/README.md` contains
  `KWS_BOARD_HOST_PARITY_DEBUG_GUIDE_ZH.md`
- link check prints:
  - `broken_links 0`

Interpretation:
- this step is documentation-only
- no firmware rebuild or reflashing is required
- the new document is ready to be handed to other teammates as the default
  onboarding reference for future board/local KWS deployment debugging

## Step 5.101 Verification

Review the new realtime-analysis document and confirm it is indexed:
```bash
cd /root/ameba-river
sed -n '1,260p' doc/KWS_STUDENT_FP32_REALTIME_ANALYSIS_ZH.md
rg -n "KWS_STUDENT_FP32_REALTIME_ANALYSIS_ZH.md" doc/README.md
```

Check that all relative markdown links inside the new analysis doc resolve:
```bash
cd /root/ameba-river
python3 - <<'PY'
from pathlib import Path
import re

root = Path('/root/ameba-river')
doc = root / 'doc/KWS_STUDENT_FP32_REALTIME_ANALYSIS_ZH.md'
text = doc.read_text(encoding='utf-8')
base = doc.parent
bad = []
for target in re.findall(r'\[[^\]]+\]\(([^)]+)\)', text):
    if '://' in target or target.startswith('#'):
        continue
    path = (base / target).resolve()
    if not path.exists():
        bad.append((target, str(path)))

print('broken_links', len(bad))
for target, path in bad:
    print(target, '->', path)
PY
```

Expected result:
- the document clearly states:
  - current board `infer_us` is about `675 ms`
  - the active student FP32 path uses `40 x 101` input and `float32`
  - the main cause is graph/runtime cost on `RTL8730E + TFLM FP32`
  - changing `101 -> 98` alone is not enough to recover realtime behavior
  - the practical next steps are `INT8` evaluation plus structural slimming
- `doc/README.md` contains
  `KWS_STUDENT_FP32_REALTIME_ANALYSIS_ZH.md`
- link check prints:
  - `broken_links 0`

Interpretation:
- this step is documentation-only
- no firmware rebuild or reflashing is required
- the new analysis can now be used directly when giving concrete feedback to
  the algorithm team or when deciding whether to continue board-side debug on
  the FP32 branch

## Step 5.102 Verification

Review the new INT8 realtime-estimate document and confirm it is indexed:
```bash
cd /root/ameba-river
sed -n '1,320p' doc/KWS_STUDENT_INT8_REALTIME_ESTIMATE_ZH.md
rg -n "KWS_STUDENT_INT8_REALTIME_ESTIMATE_ZH.md" doc/README.md
```

Check that all relative markdown links inside the new INT8 analysis doc resolve:
```bash
cd /root/ameba-river
python3 - <<'PY'
from pathlib import Path
import re

root = Path('/root/ameba-river')
doc = root / 'doc/KWS_STUDENT_INT8_REALTIME_ESTIMATE_ZH.md'
text = doc.read_text(encoding='utf-8')
base = doc.parent
bad = []
for target in re.findall(r'\[[^\]]+\]\(([^)]+)\)', text):
    if '://' in target or target.startswith('#'):
        continue
    path = (base / target).resolve()
    if not path.exists():
        bad.append((target, str(path)))

print('broken_links', len(bad))
for target, path in bad:
    print(target, '->', path)
PY
```

Optional reproduction of the local evidence used in the document:
```bash
cd /root/ameba-river
python3 - <<'PY'
import time
import numpy as np
import tensorflow as tf

base = '/root/kws-trainint/artifacts/exports/student_bc_resnet_tiny_v2/'
for name in ['model.int8.tflite', 'model.fp32.tflite']:
    it = tf.lite.Interpreter(model_path=base + name, num_threads=1)
    it.allocate_tensors()
    inp = it.get_input_details()[0]
    x = np.zeros(inp['shape'], dtype=inp['dtype'])
    if inp['dtype'] == np.int8:
        x.fill(int(inp['quantization'][1]))
    it.set_tensor(inp['index'], x)
    for _ in range(20):
        it.invoke()
    t0 = time.perf_counter()
    for _ in range(50):
        it.invoke()
    t1 = time.perf_counter()
    print(name, 'avg_ms', ((t1 - t0) / 50.0) * 1000.0)
PY
```

Expected result:
- the document clearly states:
  - INT8 and FP32 share the same high-cost topology
  - INT8 is much more worth boarding than the current student FP32 debug path
  - the local board-side estimate is still likely above the current `160ms`
    stride budget, so it should be treated as a parallel debug candidate first
  - first-board recommendations include keeping the FP32 parity path, using a
    dedicated INT8 debug variant, and starting from a larger arena
- `doc/README.md` contains
  `KWS_STUDENT_INT8_REALTIME_ESTIMATE_ZH.md`
- link check prints:
  - `broken_links 0`

Interpretation:
- this step is documentation-only
- no firmware rebuild or reflashing is required
- the new document can now be used as the default written recommendation before
  starting INT8 board bring-up for this student bundle

## Step 5.103 Verification

Build the active student INT8 debug firmware:
```bash
cd /root/ameba-river
source env.sh >/dev/null
python3 /root/ameba-rtos-1.2/ameba.py build -p
```

Flash the board with the current project profile:
```bash
cd /root/ameba-river
source env.sh >/dev/null
python3 tools/river_flash.py -p /dev/ttyUSB0
```

Capture a fresh boot log and verify the boot-time INT8 runtime contract:
```bash
script -q -f /tmp/kws_student_int8_serial.log -c "bash -lc 'stty -F /dev/ttyUSB0 1500000 raw -echo; cat /dev/ttyUSB0'"
```

Optional if the serial capture started after boot:
```bash
bash -lc "printf 'reboot\r' > /dev/ttyUSB0"
```

Boot log must show:
- `variant=student_bc_resnet_tiny_v2_int8_debug`
- `kws tensor io: runtime_in=int8 runtime_out=int8`
- `kws input shape: src=schema dims=[1,40,101,1]`
- `kws quant: ... in_zp=-46 ... out_zp=-128`

Run the preserved board-side parity flow without changing the existing debug
mechanism:
```bash
bash -lc "printf 'river kws debug local on\r' > /dev/ttyUSB0"
bash -lc "printf 'river audio probe stop\r' > /dev/ttyUSB0"
bash -lc "printf 'river kws align run\r' > /dev/ttyUSB0"
```

Wait until the serial log contains both:
- `kws tensor dump output_raw: seq=...`
- `kws align replay done: dump=emitted local_only_restored=yes`

Replay the captured dump on host against the exact INT8 bundle:
```bash
cd /root/ameba-river
OMP_NUM_THREADS=1 TF_NUM_INTRAOP_THREADS=1 TF_NUM_INTEROP_THREADS=1 \
python3 tools/kws/replay_board_tensor_dump.py \
  --log /tmp/kws_student_int8_serial.log \
  --model /root/kws-trainint/artifacts/exports/student_bc_resnet_tiny_v2/model.int8.tflite \
  --seq latest
```

Expected parity result:
- `board_meta: input_type=int8 output_type=int8 shape=(1, 40, 101, 1)`
- `quant_parity: diff_bytes=0/4040`
- `output_parity: bytes_equal=yes raw_equal=yes`
- `board_output: raw=-28 score=0.390625 q15=12800`
- `host_output: raw=-28 score=0.390625`

Realtime conclusion from the same board run:
- boot/alignment logs show `kws infer slow` around `2339206us` to `2344471us`
- queue trimming still appears before/around inference
- this proves deployment correctness, but does not satisfy realtime needs on
  the current `RTL8730E` board path

Restore the board to the normal runtime state after the test:
```bash
bash -lc "printf 'river kws debug local off\r' > /dev/ttyUSB0"
bash -lc "printf 'river audio probe start\r' > /dev/ttyUSB0"
```

Expected restore result:
- serial prints `kws debug local_only: enabled=no`
- serial prints `vad probe started`
- the board returns to the normal wake-monitoring path instead of remaining in
  the parity-only debug state

## Step 5.104 Verification

Review the new student INT8 board-realtime analysis document and confirm it is
indexed:
```bash
cd /root/ameba-river
sed -n '1,260p' doc/KWS_STUDENT_INT8_BOARD_REALTIME_ANALYSIS_ZH.md
rg -n "KWS_STUDENT_INT8_BOARD_REALTIME_ANALYSIS_ZH.md" doc/README.md
```

Verify the key SDK-side evidence cited by the document:
```bash
cd /root/ameba-river
nl -ba /root/ameba-rtos-1.2/component/tflite_micro/tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/conv.cc | sed -n '220,267p'
nl -ba /root/ameba-rtos-1.2/component/tflite_micro/tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/depthwise_conv.cc | sed -n '141,158p'
nl -ba /root/ameba-rtos-1.2/component/tflite_micro/tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/conv.cc | sed -n '114,170p'
```

Expected result:
- the `conv.cc` INT8 path explicitly states the optimized path is not reliable
  and immediately calls `reference_integer_ops::ConvPerChannel(...)`
- the `depthwise_conv.cc` INT8 path explicitly states the optimized path is
  not reliable and calls `reference_integer_ops::DepthwiseConvPerChannel(...)`
- the FP32 `conv` path still shows `optimized_ops::Im2col(...)` and
  `cpu_backend_gemm::Gemm(...)`

Optional cross-check of the current measured board symptom from the captured
INT8 parity log:
```bash
cd /root/ameba-river
rg -n "kws infer slow: infer=8 us=2339206|kws perf: infer_us\\[last=2339206|quant_parity: diff_bytes=0/4040|output_parity: bytes_equal=yes raw_equal=yes" \
  /tmp/kws_student_int8_serial.log \
  .codex/verification.md
```

Expected interpretation:
- the current student INT8 deployment is correct
- the current student INT8 board latency problem is dominated by runtime kernel
  choice plus the heavy model topology
- it should not be misdiagnosed as a board-integration or quantization-wiring
  error

## Step 5.105 Verification

Build the active student FP32 debug firmware:
```bash
cd /root/ameba-river
source env.sh >/dev/null
python3 /root/ameba-rtos-1.2/ameba.py build -p
```

Expected build result:
- build completes with `Build done`
- active build keeps
  `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_TINY_V2_FP32_DEBUG=y`

Flash the board with the current project profile:
```bash
cd /root/ameba-river
python3 tools/river_flash.py -p /dev/ttyUSB0
```

Capture a fresh serial log:
```bash
rm -f /tmp/kws_student_fp32_debug.log
script -q -f /tmp/kws_student_fp32_debug.log -c "bash -lc 'stty -F /dev/ttyUSB0 1500000 raw -echo; cat /dev/ttyUSB0'"
```

If capture started after boot, trigger one reboot:
```bash
bash -lc "printf 'reboot\r' > /dev/ttyUSB0"
```

Boot log must show:
- `variant=student_bc_resnet_tiny_v2_fp32_debug`
- `kws tensor io: runtime_in=float32 runtime_out=float32`
- `kws input shape: src=schema dims=[1,40,101,1]`
- `kws backend: ... fft=400 hop=160 center=yes ...`
- `kws io binding: ... arena_used=4709152 ... arena_slack=3679456`

Run the preserved board/local debug flow without changing the serial mechanism:
```bash
bash -lc "printf 'river kws debug local on\r' > /dev/ttyUSB0"
bash -lc "printf 'river audio probe stop\r' > /dev/ttyUSB0"
bash -lc "printf 'river kws align status\r' > /dev/ttyUSB0"
bash -lc "printf 'river kws align run\r' > /dev/ttyUSB0"
```

Wait until the serial log contains:
- `kws align replay start: source=compiled_pcm`
- `kws infer slow: infer=1 us=675892`
- `kws tensor dump output_raw: seq=1`
- `kws align replay done: dump=emitted local_only_restored=yes`

Replay the board dump on host against the exact FP32 bundle:
```bash
cd /root/ameba-river
OMP_NUM_THREADS=1 TF_NUM_INTRAOP_THREADS=1 TF_NUM_INTEROP_THREADS=1 \
python3 tools/kws/replay_board_tensor_dump.py \
  --log /tmp/kws_student_fp32_debug.log \
  --model /root/kws-trainint/artifacts/exports/student_bc_resnet_tiny_v2/model.fp32.tflite \
  --seq latest
```

Expected parity result:
- `board_meta: input_type=float32 output_type=float32 shape=(1, 40, 101, 1)`
- `quant_parity: diff_bytes=0/16160`
- `output_parity: bytes_equal=yes raw_equal=yes`
- `board_output: raw=371 score=0.371203 exact=0.371203 q15=12163`

Restore the board to normal runtime:
```bash
bash -lc "printf 'river kws debug local off\r' > /dev/ttyUSB0"
bash -lc "printf 'river audio probe start\r' > /dev/ttyUSB0"
```

Expected restore result:
- serial prints `kws debug local_only: enabled=no`
- serial prints `vad probe started`

Optional live-side follow-up check from the same log:
- after restore, a natural live sample should still show FP32 latency in the
  same range, for example:
  - `kws infer slow: infer=2 us=675354`
  - `kws perf: infer_us[last=675354 avg=675623 max=675892 ...]`

Review the new performance record and confirm it is indexed:
```bash
cd /root/ameba-river
sed -n '1,260p' doc/RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_FP32_DEBUG_ZH.md
rg -n "RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_FP32_DEBUG_ZH.md" doc/README.md
```

Expected interpretation:
- current student FP32 deployment and board/local parity remain correct
- current board-side student FP32 latency is still about `675ms`
- current build is suitable for deployment/parity debugging, not for realtime
  production use on `RTL8730E`

## Step 5.106 Verification

Verify the nano FP32 debug variant is wired into the source tree:
```bash
cd /root/ameba-river
rg -n "STUDENT_BC_RESNET_NANO_V2_FP32_DEBUG|student_bc_resnet_nano_v2_fp32_debug|student_bc_resnet_nano_v2_fp32_tflite" \
  Kconfig \
  prj.conf \
  components/river_voice/river_voice_kws.cc
```

Expected result:
- `Kconfig` defines `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_NANO_V2_FP32_DEBUG`
- `prj.conf` enables that nano FP32 debug variant
- `river_voice_kws.cc` maps the variant to
  `student_bc_resnet_nano_v2_fp32_tflite`

Verify the generated model header is present:
```bash
cd /root/ameba-river
ls -l components/river_voice/generated/student_bc_resnet_nano_v2_fp32_model_data.h
```

Build the active nano FP32 debug firmware:
```bash
cd /root/ameba-river
source env.sh >/dev/null
python3 /root/ameba-rtos-1.2/ameba.py build -p
```

Expected build result:
- build completes with `Build done`
- this step validates integration only; it does not yet require flashing

Scope note:
- Board/local parity confirmation and nano runtime measurement are the next
  step after this compile gate passes.

## Step 5.107 Verification

Capture a fresh nano FP32 serial log from the flashed board:
```bash
cd /root/ameba-river
rm -f /tmp/kws_nano_fp32_debug.log
script -q -f /tmp/kws_nano_fp32_debug.log -c "bash -lc 'stty -F /dev/ttyUSB0 1500000 raw -echo; cat /dev/ttyUSB0'"
```

If capture starts after boot, trigger one reboot:
```bash
bash -lc "printf 'reboot\r' > /dev/ttyUSB0"
```

Boot log must show the intended nano FP32 contract:
- `variant=student_bc_resnet_nano_v2_fp32_debug`
- `kws tensor io: runtime_in=float32 runtime_out=float32`
- `kws input shape: src=schema dims=[1,40,101,1]`
- `kws backend: ... fft=400 hop=160 center=yes ... threshold_q15=8851`
- `kws io binding: ... arena_used=3139392 ... arena_slack=1054912`

Confirm the board-embedded model matches the algorithm FP32 bundle exactly:
```bash
cd /root/ameba-river
python3 - <<'PY'
from pathlib import Path
import hashlib, re

header = Path('components/river_voice/generated/student_bc_resnet_nano_v2_fp32_model_data.h').read_text()
data = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', header))
model = Path('/root/kws-trainint/artifacts/exports/student_bc_resnet_nano_v2/model.fp32.tflite').read_bytes()

print('header_bytes', len(data))
print('header_sha256', hashlib.sha256(data).hexdigest())
print('model_bytes', len(model))
print('model_sha256', hashlib.sha256(model).hexdigest())
print('exact_match', 'yes' if data == model else 'no')
PY
```

Expected result:
- both byte counts are `108828`
- both SHA256 values are
  `4ff052777e4796db44d5899e94c15eb62c3d24431edcc065d9388671c4df3945`
- `exact_match yes`

Run the preserved board/local parity flow unchanged:
```bash
bash -lc "printf 'river kws debug local on\r' > /dev/ttyUSB0"
bash -lc "printf 'river audio probe stop\r' > /dev/ttyUSB0"
bash -lc "printf 'river kws align status\r' > /dev/ttyUSB0"
bash -lc "printf 'river kws align run\r' > /dev/ttyUSB0"
```

Wait until the log shows:
- `kws align guard: kws=ready probe=stopped interaction=wake_monitoring detection=ready worker=idle snapshot=empty local_only=yes`
- `kws align replay start: source=compiled_pcm frames=145 emit_dump=yes`
- `kws diag: infer=1 gate=open out_type=float32 raw=363 score=0.363446 q15=11909`
- `kws infer slow: infer=1 us=277865 queue=17/64 gate=open score_pm=363`
- `kws tensor dump captured: seq=1 infer=1 mode=align_best`
- `kws align replay done: dump=emitted local_only_restored=yes`

Replay the board dump on host using the reference host mode:
```bash
cd /root/ameba-river
OMP_NUM_THREADS=1 TF_NUM_INTRAOP_THREADS=1 TF_NUM_INTEROP_THREADS=1 \
python3 tools/kws/replay_board_tensor_dump.py \
  --log /tmp/kws_nano_fp32_debug.log \
  --model /root/kws-trainint/artifacts/exports/student_bc_resnet_nano_v2/model.fp32.tflite \
  --seq latest \
  --builtin-ref
```

Expected parity result:
- `host_runtime: builtin_ref=yes`
- `board_hash: feature=0x7ce0b11d input=0xd52f011c`
- `host_hash: feature=0x7ce0b11d ... effective_input=0xd52f011c source=input_raw`
- `quant_parity: diff_bytes=0/16160`
- `board_output: raw=363 score=0.363446 exact=0.363446 q15=11909`
- `host_output: raw=363 ... exact=0.363446`
- `output_parity: bytes_equal=yes raw_equal=yes`

Restore the board to normal runtime:
```bash
bash -lc "printf 'river kws debug local off\r' > /dev/ttyUSB0"
bash -lc "printf 'river audio probe start\r' > /dev/ttyUSB0"
```

Expected restore result:
- serial prints `vad probe started`
- the board is not left in `local_only` / parity-only state

Optional live-side confirmation after restore:
- later log lines should return to the normal cloud handoff path instead of
  `wakeword handoff held: reason=local_debug`
- for example:
  - `wakeword queued text=小欧管家 confidence=9971`
  - `xiaozhi conversation window opened: source=wakeword mode=auto timeout_ms=8000`
  - `kws infer slow: infer=7 us=277681 queue=6/64 gate=closed score_pm=304`

Review the new nano FP32 board profile and doc index:
```bash
cd /root/ameba-river
sed -n '1,260p' doc/RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_NANO_FP32_DEBUG_ZH.md
rg -n "RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_NANO_FP32_DEBUG_ZH.md" doc/README.md
rg -n -- "--builtin-ref|host delegate|delegate 数值路径差异" doc/KWS_BOARD_HOST_PARITY_DEBUG_GUIDE_ZH.md
```

Expected interpretation:
- nano FP32 deployment is board-correct
- board/host exact parity can be reproduced stably with `--builtin-ref`
- current nano FP32 latency is about `277.865ms`, which is much better than
  student tiny FP32 but still well above the bundle `24ms` budget

## Step 5.108 Verification

Review the new INT8/INT16 deployment-constraint document:
```bash
cd /root/ameba-river
sed -n '1,260p' doc/KWS_INT8_INT16_DEPLOYMENT_CONSTRAINTS_ZH.md
```

Cross-check the three key implementation constraints cited in the document.

1. Current KWS app rejects `int16` tensor I/O:
```bash
cd /root/ameba-river
sed -n '3633,3658p' components/river_voice/river_voice_kws.cc
```

Expected result:
- the accepted effective tensor types are only `kTfLiteUInt8`, `kTfLiteInt8`,
  and `kTfLiteFloat32`
- any other type, including `kTfLiteInt16`, falls into
  `kws tensor type unsupported`

2. Current host replay tool does not support `int16`:
```bash
cd /root/ameba-river
rg -n \"unsupported input dtype|if name == \\\"int8\\\"|if name == \\\"uint8\\\"|if name == \\\"float32\\\"\" \
  tools/kws/replay_board_tensor_dump.py
```

Expected result:
- replay input dtype decoding only handles `int8`, `uint8`, and `float32`
- there is no `int16` replay path today

3. Current CA32 quantized kernel paths are reference-dominated:
```bash
sed -n '240,280p' /root/ameba-rtos-1.2/component/tflite_micro/tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/conv.cc
sed -n '135,175p' /root/ameba-rtos-1.2/component/tflite_micro/tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/depthwise_conv.cc
sed -n '35,60p' /root/ameba-rtos-1.2/component/tflite_micro/tensorflow/lite/micro/kernels/mul.cc
sed -n '55,95p' /root/ameba-rtos-1.2/component/tflite_micro/tensorflow/lite/micro/kernels/logistic.cc
sed -n '112,154p' /root/ameba-rtos-1.2/component/tflite_micro/tensorflow/lite/micro/kernels/add.cc
```

Expected result:
- CA32 `int8 conv` explicitly calls `reference_integer_ops::ConvPerChannel(...)`
- CA32 `int8 depthwise` explicitly calls
  `reference_integer_ops::DepthwiseConvPerChannel(...)`
- `MUL`, `LOGISTIC`, and `ADD` quantized paths are all reference style

Cross-check the current board-side evidence already recorded in repo docs:
```bash
cd /root/ameba-river
rg -n \"2\\.34s|2343|2344|student_bc_resnet_tiny_v2_int8_debug|reference kernel|reference_integer_ops::ConvPerChannel\" \
  doc/KWS_STUDENT_INT8_BOARD_REALTIME_ANALYSIS_ZH.md \
  .codex/changes.md
rg -n \"675 ms|675\\.892|student_bc_resnet_tiny_v2_fp32_debug\" \
  doc/KWS_STUDENT_FP32_REALTIME_ANALYSIS_ZH.md
rg -n \"277\\.865|student_bc_resnet_nano_v2_fp32_debug\" \
  doc/RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_NANO_FP32_DEBUG_ZH.md
```

Expected interpretation:
- current project evidence is consistent with the new constraint document:
  - INT8 board correctness has already been proven
  - INT8 realtime on this runtime path is still unacceptable
  - INT16 is not currently a supported deployment target
  - future algorithm candidates must be filtered by runtime-path reality, not
    by offline budget declarations alone

Confirm the new document is indexed:
```bash
cd /root/ameba-river
rg -n \"KWS_INT8_INT16_DEPLOYMENT_CONSTRAINTS_ZH.md\" doc/README.md
```
