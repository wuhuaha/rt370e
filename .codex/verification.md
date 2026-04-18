# Verification

## Step 5.216
Validate that XiaoZhi idle reopen gating now lives in session runtime and the
adapter only delegates follow-up-open policy:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
sed -n '2046,2175p' components/river_cloud/river_cloud_adapter.c
sed -n '238,346p' components/river_cloud/river_cloud_xiaozhi_session.c
rg -n 'river_cloud_xiaozhi_maybe_start_followup_round|river_cloud_xiaozhi_no_ref_reopen_ready' \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  components/river_cloud/river_cloud_adapter.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- adapter `river_cloud_xiaozhi_stream_push_frame(...)` no longer directly owns:
  - wakeword-window reopen gating
  - no-ref reopen gating
  - open-hold speech-frame counting
- session runtime exports and implements:
  - `river_cloud_xiaozhi_maybe_start_followup_round(...)`
- `river_cloud_xiaozhi_no_ref_reopen_ready(...)` is only implemented inside the
  session runtime source

## Step 5.215
Validate that XiaoZhi terminal close-session tail actions for `network_lost`
and `bridge_close` now live in session runtime:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'apply_network_lost_terminal_policy|apply_bridge_close_terminal_policy|request_close_session\\(' \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  components/river_cloud/river_cloud_adapter.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - session runtime terminal-policy helpers own the close-session tail action
  - adapter terminal paths no longer append `request_close_session()` after
    invoking those helpers

## Step 5.214
Validate that XiaoZhi follow-up reopen round-start policy now lives in session
runtime and adapter only delegates to the exported helper:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'river_cloud_xiaozhi_start_followup_round|followup_transport_unavailable|reopen_overlap|river_cloud_xiaozhi_stream_push_frame' \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  components/river_cloud/river_cloud_adapter.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - session runtime exports `river_cloud_xiaozhi_start_followup_round(...)`
  - session runtime owns `followup_transport_unavailable` / `reopen_overlap`
    round-start policy
  - adapter reopen-open path only delegates to the exported helper before local
    pre-roll replay

## Step 5.213
Validate that XiaoZhi TTS interrupt policy now lives in session runtime and
adapter only dispatches to the exported helper:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'river_cloud_xiaozhi_interrupt_tts|river_cloud_adapter_interrupt_tts_with_reason|tts interrupt requested' \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  components/river_cloud/river_cloud_adapter.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - session runtime exports `river_cloud_xiaozhi_interrupt_tts(...)`
  - session runtime owns the XiaoZhi `tts interrupt requested` log and policy
  - adapter only dispatches through the exported helper for XiaoZhi

## Step 5.212
Validate that XiaoZhi ASR round lifecycle now lives in session runtime and
adapter only consumes the exported helpers:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'river_cloud_xiaozhi_round_begin|river_cloud_xiaozhi_round_note_packet_sent' \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  components/river_cloud/river_cloud_adapter.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - session runtime exports `river_cloud_xiaozhi_round_begin(...)`
  - session runtime exports `river_cloud_xiaozhi_round_note_packet_sent(...)`
  - adapter only consumes those exports and no longer defines their bodies locally

## Step 5.211
Validate that XiaoZhi `listen_stop` completion round-close policy now lives in
session runtime and adapter only consumes the exported helper:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'apply_listen_stop_completion_round_policy|finalize_listen_stop_if_ready|post_stop_result_round_policy' \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  components/river_cloud/river_cloud_adapter.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - session runtime exports `river_cloud_xiaozhi_apply_listen_stop_completion_round_policy(...)`
  - adapter `river_cloud_xiaozhi_finalize_listen_stop_if_ready()` path now only calls the exported helper for completion round close

## Step 5.210
Validate that XiaoZhi `bridge_close` terminal cleanup policy now lives in
session runtime and adapter only consumes the exported helper:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'apply_bridge_close_terminal_policy|bridge_close|asr_audio_close' \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  components/river_cloud/river_cloud_adapter.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - session runtime exports `river_cloud_xiaozhi_apply_bridge_close_terminal_policy(...)`
  - adapter `river_cloud_asr_audio_close()` path now only calls the exported helper for terminal cleanup

## Step 5.209
Validate that XiaoZhi `network_lost` terminal cleanup policy now lives in
session runtime and adapter only consumes the exported helper:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'apply_network_lost_terminal_policy|network_lost|notify_network_lost' \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  components/river_cloud/river_cloud_adapter.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - session runtime exports `river_cloud_xiaozhi_apply_network_lost_terminal_policy(...)`
  - adapter `river_cloud_adapter_notify_network_lost()` path now only calls the exported helper for terminal cleanup

## Step 5.208
Validate that XiaoZhi `transport_closed` terminal cleanup policy now lives in
session runtime and adapter only consumes the exported helper:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'apply_transport_closed_terminal_policy|transport_closed|RIVER_XIAOZHI_EVENT_SESSION_CLOSED' \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  components/river_cloud/river_cloud_adapter.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - session runtime exports `river_cloud_xiaozhi_apply_transport_closed_terminal_policy(...)`
  - adapter `RIVER_XIAOZHI_EVENT_SESSION_CLOSED` path now only calls the exported helper

## Step 5.207
Validate that XiaoZhi `post_stop_result` local-close policy now lives in
session runtime and adapter only consumes the exported helper:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'apply_post_stop_result_round_policy|post_stop_result|close_local_round_for_cause\\(' \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  components/river_cloud/river_cloud_adapter.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - session runtime exports `river_cloud_xiaozhi_apply_post_stop_result_round_policy(...)`
  - adapter listen-stop path now only calls the exported helper

## Step 5.206
Validate that XiaoZhi `LLM` local-close policy now lives in session runtime and
adapter only consumes the exported helper:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'apply_llm_round_policy|finalize_pending_text\\(\"llm\"\\)|close_local_round_for_cause\\(|RIVER_XIAOZHI_EVENT_LLM' \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  components/river_cloud/river_cloud_adapter.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - session runtime exports `river_cloud_xiaozhi_apply_llm_round_policy(...)`
  - `finalize_pending_text("llm")` now lives in session runtime
  - adapter `RIVER_XIAOZHI_EVENT_LLM` path only calls the exported helper

## Step 5.205
Validate that XiaoZhi `reopen_overlap` local-close policy now lives in session
runtime and adapter only consumes the exported helper:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'apply_reopen_overlap_round_policy|reopen_overlap' \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  components/river_cloud/river_cloud_adapter.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - session runtime exports `river_cloud_xiaozhi_apply_reopen_overlap_round_policy(...)`
  - adapter overlap-close branch only calls the exported helper
  - note/close logic for `reopen_overlap` is no longer assembled in adapter

## Step 5.204
Validate that XiaoZhi `tts_stop` round-close policy now lives in session
runtime and adapter only consumes the exported helper:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'apply_tts_stop_round_policy|tts_stop' \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  components/river_cloud/river_cloud_adapter.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - session runtime exports `river_cloud_xiaozhi_apply_tts_stop_round_policy(...)`
  - adapter stop branch only calls the exported helper
  - the local stop-path policy body is no longer assembled in the adapter

## Step 5.203
Validate that the redundant XiaoZhi `keep_local_round_on_tts_start` predicate
is gone after TTS-start policy moved into session runtime:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'keep_local_round_on_tts_start|apply_tts_start_round_policy' \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  components/river_cloud/river_cloud_adapter.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - `keep_local_round_on_tts_start` is no longer exported or defined
  - `apply_tts_start_round_policy` remains the only session-runtime TTS-start policy helper

## Step 5.202
Validate that XiaoZhi `tts_start` keep-open / close policy now lives in
session runtime and adapter only consumes the exported helper:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'apply_tts_start_round_policy|tts_start keeps local round open|tts_start falls back to round close' \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  components/river_cloud/river_cloud_adapter.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - session runtime exports `river_cloud_xiaozhi_apply_tts_start_round_policy(...)`
  - the helper owns the keep-open vs round-close logging/policy
  - adapter only calls the exported helper at `tts_start`

## Step 5.201
Validate that the `capture held during playback` predicate now lives behind a
session-runtime helper and adapter only consumes the exported reducer:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'capture_held_by_playback|duplex_fallback_reason\\(&duplex_eval\\)|playback_lane_engaged\\(\\) && resolved_reason' \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  components/river_cloud/river_cloud_adapter.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - session runtime exports `river_cloud_xiaozhi_capture_held_by_playback(...)`
  - the helper owns the `playback_lane_engaged + fallback_reason` reducer
  - adapter capture path now consumes that helper directly

## Step 5.200
Validate that XiaoZhi `no_ref` reopen / open-hold policy helpers now live in
session runtime and that adapter only consumes the exported helpers:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'open_hold_frames_required|no_ref_reopen_ready|playback_lane_engaged' \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  components/river_cloud/river_cloud_adapter.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - session runtime exports the two helper bodies
  - adapter keeps only the call sites
  - `no_ref_reopen_ready()` now consumes `playback_lane_engaged`

## Step 5.199
Validate that cloud/runtime now exports one explicit `playback_lane_engaged`
truth and that adapter/core policy consume it instead of reconstructing
engagement from scattered raw flags:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'playback_lane_engaged|river_cloud_xiaozhi_playback_lane_engaged' \
  include/river/river_cloud.h \
  include/river/river_dialog_runtime.h \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_playback_runtime.c \
  components/river_cloud/river_cloud_adapter.c \
  components/river_core/river_dialog_runtime.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - cloud/runtime snapshot structs export `playback_lane_engaged`
  - XiaoZhi playback runtime owns `river_cloud_xiaozhi_playback_lane_engaged()`
  - adapter transport/capture gating and dialog runtime derivation consume that
    exported lane truth

## Step 5.198
Validate that `restart_pending` is no longer treated as a generic missing/idle
reference condition and now surfaces as its own AEC/duplex reason:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'PLAYBACK_RESTART_PENDING|playback_restart_pending|restart_pending|half_duplex_restart_pending' \
  include/river/river_voice_runtime_policy.h \
  components/river_voice/river_voice_runtime_policy.c \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  components/river_voice/river_voice_preproc_fixed_dsb.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - voice runtime exports dedicated AEC/duplex `restart_pending` reasons
  - XiaoZhi fallback mapping exposes `half_duplex_restart_pending`
  - fixed-dsb AECM summary tracks `restart_pending` separately from generic
    `block_playback/ref_missing/ref_idle`

## Step 5.197
Validate that `restart_pending` is now a first-class playback state and that
XiaoZhi downlink knows it must fresh-start from that state:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'RESTART_PENDING|restart_pending|playback_needs_start' \
  include/river/river_playback_service.h \
  components/river_voice/river_playback_service.c \
  components/river_core/river_dialog_runtime.c \
  components/river_core/river_session_coordinator.c \
  components/river_cloud/river_cloud_xiaozhi_playback_runtime.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - playback service exports `RIVER_PLAYBACK_RESTART_PENDING`
  - dialog runtime and session coordinator fold it into the recovering path
  - XiaoZhi downlink worker explicitly treats it as `playback_needs_start`

## Step 5.196
Validate that recover-first restart failure now falls back to a clean fresh
start path instead of first surfacing a fatal playback error:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'recover fallback|fresh start|playback flush restart failed|RIVER_PLAYBACK_ERROR' \
  components/river_voice/river_playback_service.c \
  components/river_cloud/river_cloud_xiaozhi_playback_runtime.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - playback service has a dedicated `recover fallback` log for restart failure
  - XiaoZhi runtime logs `recover fallback to fresh start`
  - the old fatal `playback flush restart failed` path remains only for
    non-recovering flush failures

## Step 5.195
Validate that recoverable playback rebuffer/restart is now exported as runtime
truth and no longer depends on local active-gap inference:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'playback_rebuffer_pending|playback_cloud_active|playback_recovering|RIVER_PLAYBACK_RECOVERING' \
  include/river \
  components/river_cloud/river_cloud_adapter.c \
  components/river_core/river_dialog_runtime.c \
  components/river_voice/river_playback_service.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - cloud snapshot exports `playback_rebuffer_pending`
  - dialog runtime stores `playback_cloud_active` and derived
    `playback_recovering`
  - playback service still treats `RIVER_PLAYBACK_RECOVERING` as active

## Step 5.194
Validate that downlink/prefetch runtime now owns a larger local buffer budget
and adaptive rebuffer threshold model:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'DOWNLINK_RING_FRAMES|DOWNLINK_START_FRAMES|DOWNLINK_REBUFFER_START_FRAMES|DOWNLINK_STARVED_REBUFFER_MS|DOWNLINK_PREFETCH_MARGIN_MS|DOWNLINK_REBUFFER_EXTRA_FRAMES|PLAYBACK_BUFFER_FRAMES' \
  components/river_cloud/river_cloud_internal.h
rg -n 'prefetch_target_ms|last_meta_gap_ms|target_ms=|upstream gap rebuffer|playback prefetch|playback rebuffer requested' \
  components/river_cloud/river_cloud_xiaozhi_playback_runtime.c \
  components/river_cloud/river_cloud_adapter.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - cloud internal constants now expose the larger downlink/playback buffer
    budget
  - playback runtime exports the adaptive `prefetch_target_ms` / meta-gap logic
  - adapter diagnostics expose `target_ms`, `meta_gap_ms`, and `rebuffer_count`

## Step 5.193
Validate that active-stream finish truth and endpoint soft-close timeout
decision now live in XiaoZhi session runtime:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'STREAM_FINISH_|note_round_finish_request|poll_endpoint_soft_close_timeout|commit_active_stream_finish_for_cause|complete_active_stream_finish' \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  components/river_cloud/river_cloud_adapter.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - session runtime exports the typed stream-finish cause family
  - endpoint timeout polling and stream-finish state commit live in
    `river_cloud_xiaozhi_session.c`
  - adapter only keeps the transport-tail glue helper

## Step 5.192
Validate that endpoint/local-close state helpers now live in XiaoZhi session
runtime instead of the adapter:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'duplex_soft_endpoint_enabled|duplex_speaking_uplink_continuation_active|clear_endpoint_soft_close_state|cancel_endpoint_soft_close|arm_endpoint_soft_close|should_defer_local_close|arm_local_close_defer|check_local_close_timeout' \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  components/river_cloud/river_cloud_adapter.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - the duplex soft-endpoint predicate is exported by session runtime too
  - those helpers are exported by session runtime
  - adapter no longer defines the helper bodies locally

## Step 5.191
Validate that local round close and round-finish ownership have started moving
from the adapter into XiaoZhi session runtime:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'ROUND_CLOSE_(LOCAL_RESOLVED|SERVER_RESPONSE)|close_local_round_for_cause|round_finish\\(' \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  components/river_cloud/river_cloud_adapter.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - session runtime exports the new round-close cause family
  - round-finish ownership now lives in `river_cloud_xiaozhi_session.c`
  - adapter local-close branches call `close_local_round_for_cause(...)`

## Step 5.190
Validate that playback write recovery now prefers an in-place flush/restart
instead of always escalating to a full stop/start cycle:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'playback_write_failed|playback recover|recover fallback to stop|RIVER_PLAYBACK_RECOVERING' \
  components/river_cloud/river_cloud_xiaozhi_playback_runtime.c \
  components/river_voice/river_playback_service.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - XiaoZhi write-fail recovery now tries `flush_stream_ex(...)` first
  - playback service can flush/restart from `RIVER_PLAYBACK_RECOVERING`
  - stop/start is only the fallback when same-track recover fails

## Step 5.189
Validate that `session coordinator` barge-in admission now keys off the
dialog-runtime snapshot instead of falling back to playback-service state:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'playback_interruptible|playback_terminal_state|river_playback_service_state_active|river_session_try_interrupt_playback_on_asr_text' \
  components/river_core/river_session_coordinator.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - `session_coordinator` uses a dialog-runtime snapshot predicate for
    playback interruption
  - the old `river_playback_service_state_active(...)` fallback is gone from
    the barge-in admission path
  - terminal playback state blocks stale interrupt attempts

## Step 5.188
Validate that `dialog runtime` now derives speaking/playback interaction from
terminal-aware playback truth instead of directly trusting late speaking lane
state:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'playback_terminal_closed|compute_playback_active_locked|output_speaking_effective|playback_terminal_waiting' \
  components/river_core/river_dialog_runtime.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - dialog runtime has terminal-aware playback/effective-speaking helpers
  - playback activity is re-derived through the same reducer on cloud sync and
    playback-state ingress
  - speaking interaction no longer keys only on raw `output_lane=speaking`

## Step 5.187
Validate that fatal downlink oversize now goes through the same typed playback
abort reducer instead of a manual clear/reset/stop sequence:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'FRAME_OVERSIZE|xiaozhi_downlink_frame_oversize|playback_abort_for_cause' \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_playback_runtime.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - `frame_oversize` has its own typed abort cause
  - the downlink worker routes that fatal path through
    `playback_abort_for_cause(...)`
  - the playback-service stop reason remains `xiaozhi_downlink_frame_oversize`

## Step 5.186
Validate that adapter terminal-close branches now route through a single
runtime-owned typed cause reducer instead of assembling local abort triples:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'PLAYBACK_ABORT_(INTERRUPT|TRANSPORT_CLOSED|NETWORK_LOST|BRIDGE_CLOSE)|playback_abort_for_cause|playback abort: cause=' \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_playback_runtime.c \
  components/river_cloud/river_cloud_adapter.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - typed playback abort causes exist for the main terminal-close entries
  - adapter branches call `playback_abort_for_cause(...)`
  - runtime logs normalized abort facts from the reducer itself

## Step 5.185
Validate that local terminal outcome and protocol terminal ACK truth are now
split all the way through the runtime snapshot chain:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'playback_terminal_state|playback_terminal_reason|local_completed|local_cleared|playback_terminal_ack' \
  include/river/river_cloud.h \
  include/river/river_dialog_runtime.h \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_playback_runtime.c \
  components/river_cloud/river_cloud_adapter.c \
  components/river_core/river_dialog_runtime.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - runtime-owned local terminal states now exist:
    - `local_completed`
    - `local_cleared`
  - cloud/dialog runtime snapshots export:
    - `playback_terminal_state`
    - `playback_terminal_reason`
  - diagnostics now show terminal result separately from protocol terminal ACK

## Step 5.184
Validate that `cleared` is no longer fabricated locally and that terminal tail
waiting is exported through the cloud/dialog runtime snapshots:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'playback_terminal_waiting|playback_terminal_wait_reason|await_last_segment_meta|await_segment_queue_drain|await_last_segment_tail|clear kept local only' \
  include/river/river_cloud.h \
  include/river/river_dialog_runtime.h \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_playback_runtime.c \
  components/river_cloud/river_cloud_adapter.c \
  components/river_core/river_dialog_runtime.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - local-only clear no longer fabricates terminal `cleared`
  - terminal-tail waiting is exported via cloud/dialog runtime snapshots
  - the runtime exposes structured reasons for pending final-tail waits

## Step 5.183
Validate that terminal `completed` is now gated by last-segment truth instead
of only by a transient local drain/idle point:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'playback_last_segment_observed|playback_completed_ready|try_queue_playback_completed_ack|last_fully_heard_segment_id' \
  components/river_cloud/river_cloud_xiaozhi_playback_runtime.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - terminal `completed` has explicit last-segment predicates
  - pending-stop no longer queues `completed` before the last segment is both
    observed and fully heard

## Step 5.182
Validate that playback write-path churn now lands on an explicit recoverable
state and no longer forces the dialog runtime onto the fatal playback-error
path:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'RIVER_PLAYBACK_RECOVERING|playback_recovering|playback_write_failed' \
  include/river/river_playback_service.h \
  components/river_voice/river_playback_service.c \
  components/river_core/river_session_coordinator.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - playback service exposes `RIVER_PLAYBACK_RECOVERING`
  - `AudioTrack_Write()` failure maps to `RIVER_PLAYBACK_RECOVERING`
  - session-coordinator runtime sync now distinguishes
    `playback_recovering` from `playback_error`

## Step 5.181
Validate that the playback runtime now proactively re-buffers sustained
upstream starvation gaps before they degrade into the old write-failure
recovery path:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'STARVED_REBUFFER_MS|starved_since_ms|upstream gap rebuffer|playback_starved' \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_playback_runtime.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - the playback runtime exposes a dedicated starvation timeout and watch state
  - sustained empty-downlink gaps are converted into controlled
    `xiaozhi_playback_starved` rebuffer recovery
  - starvation watch state is explicitly cleared on playback reset/restart

## Step 5.180
Validate that the remaining XiaoZhi playback reset/meta/stop helpers are now
implemented in the playback runtime, and that session/adapter branches consume
runtime predicates instead of raw playback fields:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'river_cloud_xiaozhi_(clear_playback_meta_state|cancel_playback_stop|playback_note_duplex_ready|mark_playback_started|arm_playback_stop|reset_playback_state|reset_downlink_state)\\(' \
  components/river_cloud/river_cloud_xiaozhi_playback_runtime.c \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  components/river_cloud/river_cloud_internal.h
rg -n 'playback_has_work|playback_note_duplex_ready' \
  components/river_cloud/river_cloud_adapter.c \
  components/river_cloud/river_cloud_xiaozhi_session.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - the helper implementations live in
    `river_cloud_xiaozhi_playback_runtime.c`
  - `river_cloud_xiaozhi_session.c` consumes the runtime helper boundary
    instead of defining those playback helpers locally
  - interrupt/window-timeout/duplex-ready branches now read
    `playback_has_work()` or `playback_note_duplex_ready()`

## Step 5.179
Validate that playback termination truth is now exposed as runtime-owned
queries/operations and that adapter close/interrupt branches consume those
APIs instead of rebuilding stop/reset logic locally:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'river_cloud_xiaozhi_playback_(output_active|has_work|abort)\\(' \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_playback_runtime.c \
  components/river_cloud/river_cloud_adapter.c
rg -n 'transport_closed|network_lost|xiaozhi_interrupt|bridge_close' \
  components/river_cloud/river_cloud_adapter.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - playback runtime declares and implements:
    - `playback_output_active`
    - `playback_has_work`
    - `playback_abort`
  - adapter close/interrupt branches now route through `playback_abort(...)`
    instead of manually sequencing `finalize_cleared + reset_downlink +
    stop_stream + reset_playback`

## Step 5.178
Validate that XiaoZhi downlink / playback media ownership is now extracted into
its own runtime module and that `river_cloud_adapter.c` only consumes the new
runtime boundary instead of keeping duplicate local implementations:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'river_cloud_xiaozhi_playback_runtime|river_cloud_xiaozhi_playback_(note_meta|check_pending_stop|finalize_cleared|start_downlink_if_needed|handle_audio_event|queued_frames)|river_cloud_xiaozhi_control_request_async' \
  components/river_cloud/CMakeLists.txt \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_playback_runtime.c \
  components/river_cloud/river_cloud_adapter.c
rg -n 'static .*river_cloud_xiaozhi_(check_pending_playback_stop|handle_audio_event|start_playback_if_needed|prepare_decoder_if_needed|downlink_task|downlink_active)' \
  components/river_cloud/river_cloud_adapter.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - the new `river_cloud_xiaozhi_playback_runtime.c` is wired into the build
  - adapter/runtime shared playback entrypoints are declared in
    `river_cloud_internal.h`
  - `river_cloud_adapter.c` routes playback work through the new
    `river_cloud_xiaozhi_playback_*` API family
- the second `rg` prints no matches, proving the old duplicated playback /
  downlink implementations are gone from `river_cloud_adapter.c`

## Step 5.177
Validate that `dialog runtime` is now the only interaction-state truth source
in `river_core`, and that cloud/provider state can be ingested through the new
runtime snapshot path:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'river_dialog_runtime|get_runtime_snapshot|boot_ready' \
  include/river/river_dialog_runtime.h \
  components/river_core/river_dialog_runtime.c \
  components/river_core/river_app.c \
  include/river/river_cloud.h \
  components/river_cloud/river_cloud_adapter.c
rg -n 'RIVER_SESSION_PHASE|river_interaction_state_set\\(' \
  components/river_core/river_session_coordinator.c \
  components/river_core/river_app.c \
  components/river_core/river_dialog_runtime.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - `river_dialog_runtime` exists as a new core module
  - `river_cloud_adapter_get_runtime_snapshot(...)` exists and is used by the
    runtime
  - `river_app.c` no longer writes interaction state directly
  - `river_session_coordinator.c` no longer contains the old
    `RIVER_SESSION_PHASE_*` state machine

Board validation after flashing:
```text
river status
river xiaozhi status
```

Expected result:
- status output now includes a `dialog_runtime` line
- one wake / speak / reply round should keep `interaction_state` transitions
  aligned with runtime facts instead of flipping from multiple owners

## Step 5.176
Validate that the branch now records the runtime re-architecture direction,
removes direct `river_voice -> river_cloud` coupling, and preserves XiaoZhi
uplink frames across retry paths while exposing pacing metrics:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'river_cloud_' components/river_voice
rg -n 'river_dialog_cloud_port|river_dialog_cloud_' \
  include/river/river_dialog_cloud_port.h \
  components/river_core/river_dialog_cloud_port.c \
  components/river_core/river_app.c \
  components/river_voice/river_voice_vad_probe.c \
  components/river_voice/river_voice_segment_sink.c \
  components/river_voice/river_voice_kws.cc
rg -n 'UPLINK_DRAIN_BURST_MAX|uplink_retry_valid|audio_ms=|realtime_gap_ms=|pace_pct=|VOICE_RUNTIME_REARCHITECTURE_EXECUTION_PLAN_ZH|VOICE_RUNTIME_ARCHITECTURE_REVIEW_ZH_2026-04-17' \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_adapter.c \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  .codex/active_context.md \
  .codex/active_plans.md \
  .codex/changes.md \
  doc/VOICE_RUNTIME_REARCHITECTURE_EXECUTION_PLAN_ZH.md \
  doc/VOICE_RUNTIME_ARCHITECTURE_REVIEW_ZH_2026-04-17.md
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- `rg -n 'river_cloud_' components/river_voice` returns no matches
- static grep confirms:
  - the new dialog cloud port exists and is used by `river_voice`
  - uplink retry/burst/pacing metrics exist in the XiaoZhi runtime path
  - the new architecture review and execution plan are both linked from the
    active context / plan records

Board validation after flashing:
```text
river xiaozhi status
```

Run one wake / speak / reply round and inspect monitor logs for lines similar
to:
```text
xiaozhi asr round finish: ... audio_ms=... realtime_gap_ms=... pace_pct=...
```

Expected result:
- if the previous slow-uplink issue still reproduces, board logs can now state
  directly how far wall-clock uplink pace drifted from audio time
- if the uplink worker catches up correctly, `pace_pct` should move closer to
  realtime and `realtime_gap_ms` should shrink compared with the previous logs

## Step 5.175
Validate that the branch now removes local TTS over-gain, suppresses hard
`no_ref` barge-in cuts, and resumes from `write_failed` through a deeper
rebuffer path instead of clearing playback:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'PLAYBACK_GAIN_NUM 1|PLAYBACK_GAIN_DEN 1|DOWNLINK_REBUFFER_START_FRAMES 18U|rebuffer requested|rebuffer resumed|rebuffer=%s|interrupt suppressed: mode=no_ref_duck_only|paused_at_ms|downlink_retry_valid|5\.175' \
  components/river_cloud/river_cloud_adapter.c \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  components/river_voice/river_voice_vad_probe.c \
  .codex/active_context.md \
  doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - playback gain is now `1/1`
  - the branch contains the `18`-frame rebuffer watermark and retry-frame path
  - `river_voice_vad_probe.c` now logs `mode=no_ref_duck_only`
  - active context and duplex plan both record `5.175` as landed

Board validation after flashing:
```text
river xiaozhi status
```

Run the same reproduction that previously showed:
```text
barge-in interrupt: mode=no_ref_strict ...
xiaozhi playback write failed: ...
underrun
```

Expected result:
- local monitor logs now prefer:
  - `barge-in interrupt suppressed: mode=no_ref_duck_only ...`
  - `xiaozhi playback start: ... gain=1/1 ... rebuffer=yes|no`
  - `xiaozhi playback rebuffer requested: ...`
  - `xiaozhi playback rebuffer resumed: ...`
- the same scenario should show:
  - fewer or no hard TTS cuts while still in `mode=no_ref`
  - fewer or no repeated full-response restarts after `write_failed`
  - audibly lower clipping/distortion compared with the previous `gain=5/2`

## Step 5.174
Validate that the branch now favors playback continuity on the current
half-duplex XiaoZhi path and no longer allows `no_ref` barge-in to cut TTS on
very short evidence:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'BARGE_IN_NOREF_|DOWNLINK_RING_FRAMES 32U|DOWNLINK_START_FRAMES 12U|PLAYBACK_BUFFER_FRAMES 6U|PLAYBACK_BUFFER_FRAMES_FALLBACK 4U|mode=%s buffer=%u|no_ref_strict|5\.174' \
  components/river_voice/river_voice_vad_probe.c \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_adapter.c \
  .codex/active_context.md \
  doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - `river_voice_vad_probe.c` contains the stricter `BARGE_IN_NOREF_*`
    thresholds
  - `river_cloud_internal.h` contains the enlarged `32 / 12 / 6 / 4` buffering
    constants
  - `river_cloud_adapter.c` logs `start=...` and `buffer=...`
  - active context and duplex plan both record `5.174` as landed

Board validation after flashing:
```text
river xiaozhi status
```

Expected result:
- XiaoZhi downlink status now reports queue capacity `32`

Reproduce the original wake -> speak -> reply scenario and inspect monitor logs
for lines similar to:
```text
xiaozhi playback start: ... queued=12 ... start=12 mode=no_ref buffer=6 ...
barge-in interrupt: mode=no_ref_strict ... hit_frames=12 ...
```

Expected result:
- `no_ref` playback starts only after a deeper downlink pre-buffer has built
- if local barge-in still interrupts in `no_ref` mode, it now requires the
  stricter `mode=no_ref_strict` path instead of the old `1/5` frame behavior
- the original scenario should show fewer:
  - `underrun`
  - `xiaozhi playback write failed`
  - playback-stop resets after only a short fragment

## Step 5.173B
Validate that the branch now exposes a complete board-visible timing chain for
`preview -> accept -> response.start -> audio.out.meta` without changing the
current duplex policy:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'accept latched|timing_age_ms|timing_chain_ms|from_preview_start_ms|from_preview_update_ms|from_accept_ms|from_response_start_ms|5\.173B' \
  components/river_cloud/river_xiaozhi_ws.c \
  .codex/active_context.md \
  doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - transport logs the accept latch and stage-to-stage elapsed timings
  - status output now includes `timing_age_ms` and `timing_chain_ms`
  - active context and duplex plan both record `5.173B` as landed

Board validation after flashing:
```text
river xiaozhi status
```

Expected result:
- status output now contains lines similar to:
  - `xiaozhi timing_age_ms session_update=... accept=...`
  - `xiaozhi timing_chain_ms accept_from_preview_start=... response_from_accept=...`

Run one wake / speak / reply round and inspect monitor logs for lines similar
to:
```text
xiaozhi accept latched: ...
xiaozhi response.start: ... from_accept_ms=...
xiaozhi audio.out.meta: ... from_response_start_ms=...
```

Expected result:
- board logs can directly tell whether the dominant delay happened:
  - before accepted-turn
  - between accepted-turn and `response.start`
  - between `response.start` and first playback metadata

## Step 5.173A
Validate that the branch now emits enough XiaoZhi collaboration debug logs to
separate negotiation failure, sparse server semantics, sparse payload shape,
and playback-ack send failures during upcoming server bring-up:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'collaboration gate|collaboration preview|collaboration playback_ack|semantic sparse|updated before accept|wake admission policy|payload invalid|sparse payload|arrived before preview negotiation|playback ack .* send failed|5\.173A' \
  components/river_cloud/river_xiaozhi_ws.c \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  components/river_cloud/river_cloud_adapter.c \
  .codex/active_context.md \
  doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - discovery / `session.start` collaboration snapshots exist
  - sparse `session.update` and pre-accept semantic logs exist
  - sparse preview payload / invalid payload warnings exist
  - playback-ack failure logs exist
  - active context and duplex plan both record `5.173A` as landed

Board validation after flashing:
```text
river xiaozhi status
```

Expected result:
- status output contains richer collaboration diagnostics, including:
  - `preview_reason=...`
  - `playback_ack_reason=...`
- wake a session and inspect monitor logs for lines similar to:
  - `xiaozhi collaboration gate: ...`
  - `xiaozhi collaboration preview: ...`
  - `xiaozhi collaboration playback_ack: ...`
  - `xiaozhi session.update semantic sparse: ...`
  - `xiaozhi turn semantics updated before accept: ...`

If the server sends sparse collaboration events or the device cannot write back
playback ACKs, expect warnings similar to:
```text
xiaozhi input.preview sparse payload: ...
xiaozhi input.endpoint sparse payload: ...
xiaozhi message payload invalid: ...
xiaozhi playback ack mark send failed: ...
```

## Step 5.173
Validate that the branch now encodes an explicit XiaoZhi duplex default-on
policy and a richer half-duplex fallback matrix spanning service negotiation
and local runtime readiness:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'RIVER_XIAOZHI_FULL_DUPLEX_DEFAULT_ON_EN|half_duplex_default_policy_disabled|half_duplex_service_collaboration_unavailable|half_duplex_service_endpoint_unavailable|half_duplex_service_endpoint_disabled|half_duplex_service_preview_unavailable|half_duplex_service_playback_ack_unavailable|duplex_policy default_on|default_reason=' \
  Kconfig \
  prj.conf \
  components/river_cloud/river_xiaozhi_ws.c \
  include/river/river_xiaozhi_ws.h \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  components/river_cloud/river_cloud_adapter.c \
  components/river_cloud/river_cloud_internal.h \
  .codex/active_context.md \
  doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - the branch now has a separate `CONFIG_RIVER_XIAOZHI_FULL_DUPLEX_DEFAULT_ON_EN`
    policy switch
  - `session.start` / status / fallback code all know the richer service-side
    fallback reasons
  - active context and duplex plan both record `5.173` as landed

Board validation after flashing:
```text
river xiaozhi status
```

Expected result:
- status output now contains an explicit policy line similar to:
  - `xiaozhi duplex_policy default_on=yes|no default_reason=...`
- the same dump also shows:
  - `voice_collaboration=yes|no`
  - `server_endpoint=yes/no`
  - `preview_events=yes|no`
  - `playback_ack=segment_mark_v1|-`

Run one discovery / wake / speaking-time round and inspect monitor logs:
```text
xiaozhi session.start sent: ... half_duplex=... default_on=... default_reason=...
xiaozhi tts_start keeps local round open: ...
xiaozhi tts_start falls back to round close: fallback=...
xiaozhi capture held during playback: fallback=...
```

Expected result:
- when discovery + board profile satisfy the full matrix:
  - `session.start` logs `half_duplex=no`
  - `default_on=yes`
  - speaking-time logs can stay on the duplex-ready path
- when any prerequisite is missing:
  - `session.start` logs `half_duplex=yes`
  - `default_reason` names the exact fallback cause
  - speaking-time fallback / capture-hold logs reuse the same reason family

## Step 5.172
Validate that the branch now builds a dedicated duplex-ready experimental board
profile and that native `ch3` reference telemetry is wired into both runtime
duplex evaluation and VAD-side `ref_peak` logs:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'CONFIG_RIVER_XIAOZHI_FULL_DUPLEX_EXPERIMENT_EN=y|CONFIG_RIVER_WEBRTC_AECM_EXPERIMENT_EN=y|CONFIG_RIVER_VOICE_PREPROC_PROFILE_FIXED_DSB_WEBRTC_AECM=y|runtime_native_reference|native_reference_peak|update_interleaved_channel_peak|ref_ratio_q15|5\.172|5\.173' \
  prj.conf \
  components/river_voice/river_voice_runtime_policy.c \
  include/river/river_voice_runtime_policy.h \
  components/river_voice/river_voice_preproc_fixed_dsb.c \
  components/river_voice/river_voice_vad_probe.c \
  components/river_cloud/river_cloud_adapter.c \
  .codex/active_context.md \
  doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - the branch build now enables the XiaoZhi duplex experiment and the
    `fixed_dsb_webrtc_aecm` profile
  - runtime duplex evaluation now carries native reference telemetry fields
  - `vad_probe` has a dedicated native `ch3` peak path
  - active context and duplex plan both record `5.172` as landed
  - next slice moves to `5.173`

Board validation after flashing:
```text
river xiaozhi status
```

Expected result:
- status output contains a duplex line with:
  - `ref_peak=...`
  - `ref_ratio_q15=...`
  - `duplex_ready=...`
- once TTS playback is active on the experimental profile:
  - `ref_peak` should no longer stay at `0` forever
  - `ref_activity` can progress to `active`
  - `duplex_ready=yes` becomes possible only after real native-ref activity is
    observed

Wake the board, trigger XiaoZhi playback, then inspect monitor logs:
```text
capture profile: ... +REF(native ch3)
preproc backend: fixed_dsb + webrtc_aecm [experimental native-3ch-ref]
webrtc_aecm ref_state=active ...
vad state=... ref_peak=...
xiaozhi duplex_ready=... ref_peak=... ref_ratio_q15=...
```

Expected result:
- the experimental 3-channel capture profile is clearly selected
- AECM logs show real native-ref state transitions instead of a permanently
  missing/zero reference
- `vad_probe` and duplex runtime logs observe the same far-end reference source

## Step 5.171
Validate that local speaking-time barge-in arbitration now ducks first and only
escalates to hard interrupt after sustained near-end speech:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'BARGE_IN_DUCK_HIT_FRAMES|BARGE_IN_INTERRUPT_HIT_FRAMES|BARGE_IN_RELEASE_FRAMES|BARGE_IN_DUCK_GAIN|barge-in duck|barge-in duck release|barge-in interrupt' \
  components/river_voice/river_voice_vad_probe.c \
  .codex/active_context.md \
  doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - barge-in handling now has separate duck / interrupt / release thresholds
  - local logs explicitly distinguish duck, release, and interrupt
  - active context and duplex plan both record `5.171` as landed
  - next slice moves to `5.172`

Board validation after flashing:
```text
river playback status
```

Expected result:
- during a short speaking-time interjection:
  - playback status briefly shows `ducked=on`
  - interrupt count does not necessarily increase
- during a sustained interjection:
  - playback interrupt count increases

Wake the board, wait for TTS speaking, then test three cases in monitor logs:
```text
barge-in duck: ...
barge-in duck release: ...
barge-in interrupt: ...
```

Expected result:
- 轻声附和 / 极短插话只出现 `duck` 和随后 `duck release`
- 持续插话才升级到 `barge-in interrupt`
- half-duplex / non-speaking path不受影响

## Step 5.170
Validate that duplex-ready speaking rounds now keep the local uplink alive
across short silence while output is still speaking, and only resolve the
pending close after the output lane leaves `speaking`:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'duplex_speaking_uplink_continuation_active|endpoint_soft_close|5\.170|5\.171' \
  components/river_cloud/river_cloud_adapter.c \
  .codex/active_context.md \
  doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - speaking-time endpoint soft-close timeout now checks a dedicated continued
    uplink gate
  - active context and duplex plan both record `5.170` as landed
  - next slice moves to `5.171`

Board validation after flashing:
```text
river xiaozhi status
```

Expected result:
- during duplex-ready speaking playback with a short user pause, status keeps:
  - `listening=yes`
  - `stream=yes`
  - `endpoint_soft_close pending=yes`
  - `left_ms=0` may already appear while output is still speaking

Wake the board, wait for TTS speaking, then insert a short pause mid-barge-in
and inspect realtime logs:
```text
xiaozhi hint-only endpoint: trigger=post_roll reason=local_silence ...
xiaozhi session.update: ... state=speaking ...
xiaozhi deferred local close resolved: trigger=endpoint_soft_close_timeout ...
```

Expected result:
- the `hint-only endpoint` may arm during speaking-time silence
- but the deferred local close should not resolve until the output lane leaves
  the speaking state
- no extra `listen_start` jitter should appear during the same speaking turn

## Step 5.169
Validate that speaking-time duplex-ready rounds now treat short silence and
`input.endpoint` as hint/defer signals instead of immediate hard local close:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'endpoint_soft_close|hint-only endpoint|interrupt hint|speech_resumed|input_endpoint_clear|RIVER_CLOUD_XIAOZHI_ENDPOINT_SOFT_CLOSE_DEFER_MS' \
  components/river_cloud/river_cloud_adapter.c \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  .codex/active_context.md \
  doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - endpoint-soft-close state is now tracked in the cloud runtime
  - `input.speech.start` logs an `interrupt hint`
  - `input.endpoint` logs `hint-only endpoint`
  - local post-roll silence can arm the deferred close path
  - active context and duplex plan both record `5.169` as landed

Board validation after flashing:
```text
river xiaozhi status
```

Expected result:
- status now prints a line similar to:
  - `xiaozhi endpoint_soft_close pending=yes|no reason=... left_ms=...`

Wake the board once and inspect the realtime logs:
```text
xiaozhi interrupt hint: trigger=input_speech_start ...
xiaozhi hint-only endpoint: trigger=input_endpoint ...
xiaozhi hint-only endpoint: trigger=post_roll reason=local_silence ...
xiaozhi deferred local close cancelled: trigger=speech_resumed ...
xiaozhi deferred local close resolved: trigger=endpoint_soft_close_timeout ...
```

Expected result:
- half-duplex default behavior remains unchanged when duplex-ready is false
- duplex-ready speaking path no longer closes immediately on the first short
  local silence or `input.endpoint`
- speech resumption within the short defer window keeps the round alive

## Step 5.168
Validate that XiaoZhi speaking-time duplex decisions now depend on runtime
readiness instead of only the active profile's static playback-reference
capability:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'duplex_ready|RIVER_VOICE_DUPLEX_READY_|last_(open|reset|write|read)_ms|half_duplex_ref_idle|half_duplex_aec_blocked|xiaozhi_playback_duplex_ready_seen' \
  include/river/river_voice_runtime_policy.h \
  include/river/river_reference_service.h \
  components/river_voice/river_voice_runtime_policy.c \
  components/river_voice/river_reference_service.c \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  components/river_cloud/river_cloud_adapter.c \
  .codex/active_context.md \
  doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - runtime policy now exports `river_voice_runtime_duplex_ready_eval()`
  - runtime-ready reasons include `ref_idle` and `aec_blocked`
  - reference-service stats now expose `last_open_ms/last_reset_ms/last_write_ms/last_read_ms`
  - XiaoZhi runtime tracks `xiaozhi_playback_duplex_ready_seen`
  - active context and duplex plan both record `5.168` as landed

Board validation after flashing:
```text
river xiaozhi status
```

Expected result:
- status now prints one duplex line similar to:
  - `xiaozhi duplex_ready=yes|no reason=... aec=... ref_state=... ref_activity=... ref_queue=... ref_age_ms=... playback_active=... duplex_seen=...`

Wake the board once and inspect the realtime logs:
```text
xiaozhi tts_start keeps local round open: duplex_ready=...
xiaozhi tts_start falls back to round close: duplex_ready=no reason=...
xiaozhi fallback: reason=... duplex_ready=... duplex_reason=...
xiaozhi capture held during playback: duplex_ready=no reason=...
```

Expected result:
- keep-open decisions are now explained by runtime `duplex_ready`
- half-duplex fallback reasons clearly distinguish:
  - experiment disabled
  - no playback reference capability
  - reference idle at runtime
  - AEC blocked at runtime
- default conservative behavior stays unchanged when runtime readiness is false

## Step C5
Validate that the device now provides the full XiaoZhi
`segment_mark_v1` playback ACK fact chain and that negotiation stays truthful:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'audio_out_(started|mark|cleared|completed)|playback_segment|last_fully_heard|terminal_ack|PLAYBACK_CLEARED|PLAYBACK_MARK|segment_mark_v1' \
  include/river/river_xiaozhi_ws.h \
  components/river_cloud/river_xiaozhi_ws.c \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  components/river_cloud/river_cloud_adapter.c \
  .codex/active_context.md \
  doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - transport now exports `send_audio_out_mark()` and `send_audio_out_cleared()`
  - cloud runtime now tracks per-segment playback state and terminal ACK state
  - negotiation requires full `segment_mark_v1` support before declaration
  - the active context and duplex plan both record `C5` as landed

Board validation after flashing:
```text
river xiaozhi status
```

Expected result:
- status now prints lines similar to:
  - `xiaozhi playback_ack terminal=... clear_reason=... queued_segments=... last_started=... last_fully_heard=...`
  - `xiaozhi playback_meta response_id=... playback_id=... segment_id=...`

Wake the board once and inspect the realtime logs:
```text
xiaozhi playback fact observed: response_id=... playback_id=... segment_id=...
xiaozhi playback ack started queued: ...
xiaozhi playback ack mark sent: ...
xiaozhi playback ack cleared queued: ...
xiaozhi playback ack completed queued: ...
```

Expected result:
- normal natural playback emits:
  - `started`
  - one or more monotonic `mark`
  - one `completed`
- local clear/interruption paths emit:
  - a final partial `mark` when the current segment has started
  - `cleared_after_segment_id=...` only when a fully heard segment exists
- clear-before-start paths do not fabricate `audio.out.cleared`

## Workflow Sync 2026-04-16
Validate that the repository now persists the default Chinese commit-message
rule and that the Codex harness remains consistent:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
rg -n "Chinese commit messages|中文 commit|clear Chinese descriptions|Latest workflow sync" \
  AGENTS.md \
  .codex/active_context.md \
  .codex/changes.md
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- static grep confirms:
  - `AGENTS.md` now records the default Chinese commit-message convention
  - `.codex/active_context.md` records the workflow sync
  - `.codex/changes.md` records this process update

## Step C4
Validate that the device now keeps accepted-turn semantics aligned with
`session.update.accept_reason`, does not leak old turn acceptance across rounds,
and emits explicit fallback reasoning while staying on the conservative runtime
baseline:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'clear_session_update_cache|turn accepted|await_accept_reason|turn_semantics|half_duplex_experiment_disabled|half_duplex_no_playback_reference|half_duplex_capture_held_during_playback|finalize_pending_text\("' \
  include/river/river_xiaozhi_ws.h \
  components/river_cloud/river_xiaozhi_ws.c \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  components/river_cloud/river_cloud_adapter.c \
  .codex/active_context.md \
  doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - transport now exports `river_xiaozhi_clear_session_update_cache()`
  - the cloud runtime now tracks and logs `turn_semantics`
  - pending-text finalization callsites all pass an explicit trigger string
  - explicit fallback reasons exist for both conservative half-duplex policy
    and waiting for accepted-turn
  - the active context and duplex plan both record `C4` as landed

Board validation after flashing:
```text
river xiaozhi status
```

Expected result:
- status now prints a turn-semantics line similar to:
  - `xiaozhi turn_semantics accepted=yes|no accept_reason=... turn_id=... input_state=... output_state=... barge_in_enabled=yes|no|- fallback=...`
- opening a fresh round should not inherit the previous round's
  `accept_reason/turn_id`

Wake the board once and inspect the realtime logs:
```text
xiaozhi turn accepted: trigger=... accept_reason=...
xiaozhi pending text waits for accepted turn: trigger=...
xiaozhi accepted turn final text: trigger=...
xiaozhi playback fact observed: ...
xiaozhi fallback: reason=...
```

Expected result:
- accepted-turn is logged only after `accept_reason` becomes available
- pending text is withheld until accepted-turn is confirmed
- preview observation and playback-fact logs are clearly distinct from
  accepted-turn logs
- when the board stays on the conservative path, fallback reasons are explicit

## Step C3
Validate that the device now consumes XiaoZhi playback-truth metadata and
reports the minimal started/completed playback facts without blocking local
playback:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n 'RIVER_XIAOZHI_EVENT_AUDIO_OUT_META|audio\.out\.meta|send_audio_out_started|send_audio_out_completed|segment_mark_v1|playback_meta' \
  include/river/river_xiaozhi_ws.h \
  components/river_cloud/river_xiaozhi_ws.c \
  components/river_cloud/river_cloud_adapter.c \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  .codex/active_context.md \
  doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - the XiaoZhi event contract now includes `RIVER_XIAOZHI_EVENT_AUDIO_OUT_META`
  - the transport parses `audio.out.meta`
  - `session.start` can now declare `playback_ack=segment_mark_v1`
  - the transport and cloud adapter both expose `playback_meta` status lines
  - the active context and duplex plan both record `C3` as landed

Board validation after flashing:
```text
river xiaozhi status
```

Expected result:
- status now prints a playback context line similar to:
  - `xiaozhi playback_meta=response_id=... playback_id=... segment_id=... expected_duration_ms=... is_last_segment=yes|no valid=yes|no`
- the cloud adapter status also prints:
  - `xiaozhi playback_meta response_id=... playback_id=... segment_id=... text=... expected_duration_ms=... started_ack=yes|no completed_ack=yes|no valid=yes|no`

Wake the board once and inspect the realtime logs:
```text
xiaozhi session.start sent: ... preview_events=yes|no playback_ack=segment_mark_v1
xiaozhi audio.out.meta: ...
xiaozhi playback ack started queued: ...
xiaozhi playback ack started sent: ...
xiaozhi playback ack completed queued: ...
xiaozhi playback ack completed sent: ...
```

Expected result:
- when discovery advertises playback collaboration, `session.start` now
  declares:
  - `playback_ack=segment_mark_v1`
- `audio.out.meta` is parsed and logged before the ACK path is used
- `audio.out.started` and `audio.out.completed` are queued and sent on the
  async control path
- runtime behavior remains conservative:
  - `audio.out.mark` and `audio.out.cleared` are still absent
  - ACK queue/send failures only warn and do not block playback

## Step C2
Validate that the device now consumes XiaoZhi preview-observation events
without changing the current commit/local-close control path:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n "RIVER_XIAOZHI_EVENT_INPUT_SPEECH_START|RIVER_XIAOZHI_EVENT_INPUT_PREVIEW|RIVER_XIAOZHI_EVENT_INPUT_ENDPOINT|preview_state=|xiaozhi preview preview_id=|input\\.speech\\.start|input\\.preview|input\\.endpoint|client_supports_preview_events" \
  include/river/river_xiaozhi_ws.h \
  components/river_cloud/river_xiaozhi_ws.c \
  components/river_cloud/river_cloud_adapter.c \
  components/river_cloud/river_cloud_internal.h \
  components/river_cloud/river_cloud_xiaozhi_session.c \
  .codex/active_context.md \
  doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - the XiaoZhi event contract now includes the three preview-observation event
    types
  - the transport parses `input.speech.start`, `input.preview`, and
    `input.endpoint`
  - status output includes both the transport-side `preview_state=...` line and
    the cloud-side `xiaozhi preview preview_id=...` line
  - the active context and duplex plan both record `C2` as landed

Board validation after flashing:
```text
river xiaozhi status
```

Expected result:
- status now prints a preview observation line similar to:
  - `xiaozhi preview_state=preview_id=... speech_started=yes|no text=... stable_prefix=... is_final=yes|no endpoint_candidate=yes|no endpoint_reason=... source=... audio_offset_ms=...`
- the cloud adapter status also prints:
  - `xiaozhi preview preview_id=... speech_started=... text=...`

Wake the board once and inspect the realtime logs:
```text
xiaozhi session.start sent: ... preview_events=yes|no playback_ack=-
xiaozhi input.speech.start: ...
xiaozhi input.preview: ...
xiaozhi input.endpoint: ...
```

Expected result:
- when discovery advertises preview collaboration, `session.start` now declares:
  - `preview_events=yes`
- the three preview-observation events are parsed and logged
- runtime behavior remains conservative:
  - no preview event forces commit
  - no preview text is treated as accepted-turn

## Step C1
Validate that the device now negotiates XiaoZhi collaboration capabilities from
discovery without advertising unimplemented preview/ACK features by default:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n "RIVER_XIAOZHI_DISCOVERY_PATH_DEFAULT|river_xiaozhi_refresh_discovery_profile|voice_collaboration|preview_events|playback_ack|declared_preview|declared_playback_ack" \
  components/river_cloud/river_xiaozhi_ws.c \
  .codex/active_context.md \
  doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- build output ends with:
  - `Build done`
- static grep confirms:
  - the transport now contains a discovery refresh path before session open
  - `voice_collaboration.preview_events` and `playback_ack` are parsed and
    cached
  - status/log output exposes `declared_preview` and
    `declared_playback_ack`
  - the active context and execution plan both record `C1` as landed

Board validation after flashing:
```text
river xiaozhi status
```

Expected result:
- status now prints an extra discovery line similar to:
  - `xiaozhi discovery turn_mode=... server_endpoint=yes|no/... voice_collaboration=yes|no preview_events=yes|no declared_preview=yes|no playback_ack=segment_mark_v1|- declared_playback_ack=segment_mark_v1|-`
- with the current default client support baseline, `declared_preview` should
  still be `no` and `declared_playback_ack` should still be `-` even if the
  server advertises those abilities

Wake the board once and inspect the connect logs:
```text
wakeword hit ...
xiaozhi discovery ready: ...
xiaozhi session.start sent: ... preview_events=no playback_ack=-
```

Expected result:
- discovery is attempted before websocket open and logs its parsed result
- the default session-start path remains compatibility-safe:
  - `preview_events=no`
  - `playback_ack=-`

## Plan Sync 2026-04-16
Validate that the device-side duplex plan now reflects the new 2026-04-16
server-side collaboration docs and exposes the added protocol-collaboration
slices in the active context:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
rg -n "realtime-voice-client-implementation-guide|realtime-voice-client-collaboration-proposal|voice-architecture-execution-roadmap|server-primary-hybrid|min-device-capabilities|Step C1|Step C2|Step C3|Step C4|preview-aware|playback-truth" \
  doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md \
  .codex/active_context.md
git diff --check
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `rg` output confirms:
  - the plan now references the new 2026-04-16 `/root/agent-server` docs
  - the device roadmap now contains `C1` through `C4`
  - `.codex/active_context.md` includes the same new collaboration slices
- `git diff --check` prints no whitespace or patch-format errors

## Step 5.167
Validate that the device now consumes and exposes the richer realtime
`session.update` fields without changing the current control path:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n "input_state|output_state|barge_in_enabled|turn_id|accept_reason|session_lane_state" \
  include/river/river_xiaozhi_ws.h \
  components/river_cloud/river_xiaozhi_ws.c
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- build output ends with:
  - `Build done`
- static grep confirms:
  - new accessor declarations for richer session-update fields exist in
    `river_xiaozhi_ws.h`
  - `river_xiaozhi_ws.c` now parses and logs
    `input_state/output_state/barge_in_enabled/turn_id/accept_reason`
  - `river_xiaozhi_dump_status()` now prints a `session_lane_state=...` line

Board validation after flashing:
```text
river xiaozhi status
```

Expected result:
- status now prints the original transport summary line plus a second line:
  - `xiaozhi session_lane_state=... input_state=... output_state=... barge_in_enabled=... turn_id=... accept_reason=...`
- when connected to a newer native realtime server, runtime logs should include:
  - `xiaozhi session.update: sid=... state=... input_state=... output_state=... barge_in_enabled=... accept_reason=... turn_id=...`
- when connected to an older or compatibility-only server that omits those
  fields, the same log line should still work and print `-` for missing fields
  rather than failing parsing

## Step 5.166
Validate that the device-side duplex roadmap is now converged into concrete
implementation slices and that the active context points to the new sequence:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
rg -n "5\\.167|5\\.168|5\\.169|5\\.170|5\\.171|5\\.172|5\\.173|session.update|runtime-ready|duck-first" \
  doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md \
  .codex/active_context.md
git diff --check
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `rg` output confirms:
  - the full-duplex execution plan now includes the concrete device-side slices
    `5.167` through `5.173`
  - `.codex/active_context.md` points to the same staged sequence
- `git diff --check` prints no whitespace or patch-format errors

## Step 5.165
Validate the `tts_start` local-round policy gate against the latest SDK:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
```

Default-profile smoke check on board:
```bash
cd /root/ameba-river
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 /root/ameba-river/tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000
```

In the monitor, say:
- `小欧管家，今天周几`

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- build output ends with:
  - `Build done`
- default profile remains on the conservative baseline:
  - wake -> asr -> response still works as before
  - server `tts_start` still closes the local round

Optional experiment checks:
- if only `CONFIG_RIVER_XIAOZHI_FULL_DUPLEX_EXPERIMENT_EN=y` is enabled but
  the active profile still lacks playback reference support, `tts_start` should
  log:
  - `xiaozhi tts_start falls back to round close: duplex_experiment=yes playback_ref=no`
- if both the duplex experiment and a playback-reference-capable profile are
  enabled, `tts_start` should log:
  - `xiaozhi tts_start keeps local round open: duplex_experiment=yes playback_ref=yes`
  - and the old unconditional `tts_start -> close local round` path should no
    longer run for that case

## Step 5.164
Validate the new XiaoZhi duplex capability gate against the latest SDK:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
```

Then flash and verify the default half-duplex baseline on board:
```bash
cd /root/ameba-river
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 /root/ameba-river/tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000
```

In the monitor, wake the device once and inspect `session.start`:
- `小欧管家`

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- build output ends with:
  - `Build done`
- on the default profile, the log should now include:
  - `xiaozhi session.start sent: ... duplex=half_duplex half_duplex=yes`
- functional behavior should remain on the old baseline:
  - wake admission still succeeds
  - local ASR round open / close sequencing is unchanged

Optional experiment check after explicitly enabling
`CONFIG_RIVER_XIAOZHI_FULL_DUPLEX_EXPERIMENT_EN=y` in a dedicated profile:
- the `session.start` log should switch to:
  - `duplex=full_duplex_experiment half_duplex=no`
- this step alone does not guarantee true duplex behavior; it only changes the
  advertised session capability contract.

## Step 5.163
Validate the new full-duplex execution plan registration and Codex harness
consistency:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
git diff --check
```

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- `git diff --check` prints no whitespace or patch-format errors
- the new document exists:
  - `doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md`
- `.codex/active_plans.md` lists it as a secondary active execution plan

## Step 5.162
Validate the post-commit response-wait extension against the latest SDK:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
```

Then flash and verify on board:
```bash
cd /root/ameba-river
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 /root/ameba-river/tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000
```

In the monitor, say a slightly longer utterance such as:
- `小欧管家，帮我把客厅的灯打开`
- or another natural sentence lasting around `3~5s`

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- build output ends with:
  - `Build done`
- on board:
  - after post-roll / commit, logs should include:
    - `xiaozhi response wait armed after commit: timeout_ms=6000`
  - if the server is only moderately slower, avoid the old pattern where:
    - `state=thinking`
    - then only a few seconds later
    - `xiaozhi conversation window closed: reason=followup_timeout`
  - successful cases should now have more time to reach:
    - `response.start`
    - `response.chunk`
    - audio playback

## Step 5.161
Validate the server-response-synced local round close against the latest SDK:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
```

Then flash and verify the native realtime timing on board:
```bash
cd /root/ameba-river
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 /root/ameba-river/tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000
```

In the monitor, say:
- `小欧管家，今天周几啊`
- or another short natural utterance with a small pause in the middle

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- build output ends with:
  - `Build done`
- on board:
  - `response.start` or `tts sentence_start` should be followed by a local ASR
    round close without waiting for the old 2s timeout path
  - avoid a late extra `session.update: state=thinking` caused by a delayed
    client-side close after the response has already started
  - the finished round reason should prefer the server-response trigger path
    over `timeout` for this case

## Step 5.160
Validate the River websocket lifecycle cleanup fix against the latest SDK:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
```

Optional host-side control check against the deployed realtime server:
```bash
cd /root/ameba-river
python3 tools/agent_server_debug/probe_realtime.py --host 101.33.235.154 --ports 8080 --schemes ws http
```

Then flash and verify the board-side native realtime path again:
```bash
cd /root/ameba-river
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 /root/ameba-river/tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000
```

In the monitor, verify:
- `小欧管家，今天天气怎么样`
- or:
  - `river xiaozhi connect`
  - `river xiaozhi status`

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- build output ends with:
  - `Build done`
- host-side probe still shows:
  - `ws://101.33.235.154:8080/v1/realtime/ws` -> `101 Switching Protocols`
- on board, the first wake should still reach:
  - `Connected to websocket server`
  - `xiaozhi transport ready`
  - `xiaozhi session.start sent`
- if a transport close still happens, the next wake should begin from a clean
  local session context:
  - avoid reusing the old `sid=...` in the next `wake admission begin` log
- reconnect behavior should improve because the old wsclient socket / queue /
  mutex teardown now runs before the outer context is freed

Observed result on 2026-04-14:
- `python3 tools/diag/check_codex_harness.py` passed
- latest-SDK build passed and ended with `Build done`
- `python3 tools/agent_server_debug/probe_realtime.py --host 101.33.235.154 --ports 8080 --schemes ws http` passed:
  - `ws://101.33.235.154:8080/v1/realtime/ws` returned
    `101 Switching Protocols`
  - `http://101.33.235.154:8080/v1/realtime` returned `200 OK`
- manual websocket replay already showed:
  - the server accepts `session.start`
  - the server remains open across `51` raw PCM uplink frames in the same
    native profile
- post-flash board validation remains pending for this step

## Step 5.159
Validate the SDK websocket connect-error patch and current cloud reachability:
```bash
cd /root/ameba-river
python3 tools/agent_server_debug/probe_realtime.py --host 101.33.235.154 --ports 443 8080 80 --schemes ws wss http https
python3 tools/sdk/apply_wsclient_connect_error_patch.py --sdk-root /root/ameba-rtos
python3 tools/sdk/apply_wsclient_connect_error_patch.py --sdk-root /root/ameba-rtos --check
python3 tools/diag/check_codex_harness.py
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
```

Then flash and verify the board-side native realtime open path again:
```bash
cd /root/ameba-river
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 /root/ameba-river/tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000
```

In the monitor, verify:
- `小欧管家，今天周几`
- or:
  - `river xiaozhi connect`
  - `river xiaozhi status`

Expected result:
- the local probe reflects the real server state on the current day:
  - if the service is still down, `probe_realtime.py` reports `Connection refused`
  - if the service has recovered, it should show `101 Switching Protocols`
- the patch tool output includes:
  - `status=changed` on first apply or `status=unchanged` if already patched
  - `applied` on `--check`
- harness output contains:
  - `check_codex_harness: all checks passed`
- build output ends with:
  - `Build done`
- on board, a raw TCP reachability failure should now surface explicitly as one
  of:
  - `Connect failed after select: so_error=...`
  - `Connect timeout after ... ms`
  - `Connect select failed ret(...) errno(...)`
  - `connect failed ret(...) errno(...)`
- the old ambiguous failure should no longer be the only signal:
  - avoid relying on bare `ws_connect_url: ERROR: Sending handshake failed`

Observed result on 2026-04-14:
- local probe from `/root/ameba-river` showed:
  - `101.33.235.154:443` `Connection refused`
  - `101.33.235.154:8080` `Connection refused`
  - `101.33.235.154:80` `Connection refused`
- `python3 tools/sdk/apply_wsclient_connect_error_patch.py --sdk-root /root/ameba-rtos`
  returned:
  - `status=changed`
- `python3 tools/sdk/apply_wsclient_connect_error_patch.py --sdk-root /root/ameba-rtos --check`
  returned:
  - `applied`
- `python3 tools/diag/check_codex_harness.py` passed
- the latest-SDK build passed with `Build done`
- post-flash board validation is still pending for this step

## Step 5.158
Validate the SDK handshake string-termination fix against the latest SDK:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
```

Then flash and verify the board-side native realtime upgrade again:
```bash
cd /root/ameba-river
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 /root/ameba-river/tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000
```

In the monitor, verify:
- `小欧管家，今天周几`
- or:
  - `river xiaozhi connect`
  - `river xiaozhi status`

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- build output ends with:
  - `Build done`
- board log still targets:
  - `xiaozhi connecting: url=ws://101.33.235.154:8080/v1/realtime/ws ...`
- the old send-stage failure should disappear:
  - no `ws_connect_url: ERROR: Sending handshake failed`
- instead the websocket should either:
  - reach `Connected to websocket server`
  - or move on to a later, more specific protocol-stage failure

Observed result on 2026-04-14:
- `python3 tools/diag/check_codex_harness.py` passed
- the latest-SDK build passed with `Build done`
- board validation is still pending for this step

## Step 5.157
Validate the WebSocket handshake fix against the latest SDK:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
```

Then flash and verify the board-side native realtime upgrade again:
```bash
cd /root/ameba-river
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 /root/ameba-river/tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000
```

In the monitor, verify:
- `小欧管家，今天周几`
- or:
  - `river xiaozhi connect`
  - `river xiaozhi status`

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- build output ends with:
  - `Build done`
- board log still targets:
  - `xiaozhi connecting: url=ws://101.33.235.154:8080/v1/realtime/ws ...`
- but the old handshake failure should disappear:
  - no `Got bad status connecting to HTTP/1.1 400 Bad Request`
  - no `Response header is wrong`
- instead, the websocket should upgrade and continue into native session logs:
  - `xiaozhi transport ready: ...`
  - `xiaozhi session.start sent: ...`

Observed result on 2026-04-14:
- `python3 tools/diag/check_codex_harness.py` passed
- the latest-SDK build passed with `Build done`
- board validation is still pending for this step

## Step 5.156
Validate the cloud deployment shape first, then rebuild the board image with
the corrected default endpoint:
```bash
cd /root/ameba-river
python3 -m py_compile tools/agent_server_debug/probe_realtime.py
python3 tools/agent_server_debug/probe_realtime.py --host 101.33.235.154
python3 tools/diag/check_codex_harness.py
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
```

Then flash and verify the board now opens plain WS to the cloud deployment:
```bash
cd /root/ameba-river
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 /root/ameba-river/tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000
```

In the monitor, verify:
- `小欧管家，今天周几`
- or:
  - `river xiaozhi connect`
  - `river xiaozhi status`

Expected result:
- probe output shows:
  - `443` refused
  - `http://101.33.235.154:8080/v1/realtime` returns `200 OK`
  - `ws://101.33.235.154:8080/v1/realtime/ws` returns `101 Switching Protocols`
- harness output contains:
  - `check_codex_harness: all checks passed`
- build output ends with:
  - `Build done`
- board log no longer attempts:
  - `wss://101.33.235.154/v1/realtime/ws`
- board log instead shows:
  - `xiaozhi connecting: url=ws://101.33.235.154:8080/v1/realtime/ws ...`
- the old transport-open failure should disappear:
  - no `net_connect -68`
  - no `xiaozhi_ws_connect_failed` caused by `443`

Observed result on 2026-04-14:
- `python3 -m py_compile tools/agent_server_debug/probe_realtime.py` passed
- host-network probe confirmed:
  - `443` refused
  - `8080/http` works
  - `8080/ws` works
  - `8080/https` and `8080/wss` do not work
- board rebuild / flash / runtime validation is still pending for this step

## Step 5.155
Validate the first native realtime transport slice against the latest SDK:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
```

Then flash and verify a real board interaction:
```bash
cd /root/ameba-river
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 /root/ameba-river/tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000
```

In the monitor, verify both text and voice entry paths:
- `river xiaozhi connect`
- `river xiaozhi listen detect 今天周几`
- then do a real wakeword + spoken question, for example:
  - `小欧管家，今天天气怎么样`

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- build output ends with:
  - `Build done`
- `connect` no longer needs OTA/bootstrap before websocket open:
  - no `xiaozhi ota bootstrap ok` log is required on this branch
  - websocket open reaches:
    - `xiaozhi transport ready: sample_rate=16000 frame_duration=20ms subprotocol=agent-server.realtime.v0`
- realtime control flow uses native events:
  - `xiaozhi session.start sent: ... codec=pcm16le`
  - `xiaozhi session.update: ... state=active`
  - `xiaozhi response.start: ...`
  - `xiaozhi response.chunk: ...`
  - after `xiaozhi session.end: ...`, the next follow-up turn should emit a new
    `xiaozhi session.start sent: ...` on the same websocket rather than a fresh
    `xiaozhi connecting: ...`
- downlink playback now comes from native PCM binary frames:
  - `playback start: stream=xiaozhi_tts ...`
  - `playback stop: stream=xiaozhi_tts ...`
- for spoken turns, runtime accounting should stay healthy:
  - `xiaozhi asr round begin: ...`
  - `xiaozhi asr round finish: ...`
  - `busy=0 fail=0`

Observed result on 2026-04-14:
- `python3 tools/diag/check_codex_harness.py` passed
- the latest-SDK build passed with `Build done`
- board validation is still pending for this step

## Step 5.154
Validate the XiaoZhi local `post_roll/close` defer path against the latest SDK:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 /root/ameba-river/tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000
```

Then perform a real two-turn interaction on board:
- first wake/query to get TTS playback, for example:
  - `小欧管家，今天周几`
- after TTS ends and the follow-up window stays open, speak a second query with a
  natural pause, for example:
  - `帮我开灯`
- optionally inspect runtime state between turns:
  - `river xiaozhi status`

Expected result:
- harness output contains:
  - `check_codex_harness: all checks passed`
- build output ends with:
  - `Build done`
- when a round reaches local `post_roll` before cloud semantics are ready, logs
  show:
  - `xiaozhi local close deferred: wait_ms=2000`
- after `listen_stop` is actually finalized, the same round settles through one
  of these triggers:
  - `xiaozhi local close resolved: trigger=post_stop_result ...`
  - `xiaozhi local close resolved: trigger=llm ...`
  - `xiaozhi local close resolved: trigger=tts_start ...`
  - `xiaozhi local close resolved: trigger=timeout ...`
  - `xiaozhi local close resolved: trigger=reopen_overlap ...`
- `river xiaozhi status` exposes the new state when relevant:
  - `close_pending=yes`
  - `close_left_ms=...`
- compared with the pre-step logs, genuine follow-up rounds should no longer
  fall back to an obviously premature local close without any explanation; if an
  empty round still happens, the new deferred-close logs should identify whether
  it was settled by timeout or by overlap with the next reopen

Observed result on 2026-04-13:
- `python3 tools/diag/check_codex_harness.py` passed
- the latest-SDK build passed with `Build done`
- board validation is still pending for this step

## Step 5.153
Validate the fragmented XiaoZhi downlink receive fix against the latest SDK:
```bash
cd /root/ameba-river
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 /root/ameba-river/tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000
```

Then run these monitor commands:
- `river xiaozhi connect`
- `river xiaozhi listen detect 今天天气怎么样`
- `river xiaozhi status`

Expected result:
- build output ends with `Build done`
- flash output ends with `Finished PASS`
- `connect` reaches:
  - `server hello: sid=...`
- `listen detect` now reaches actual downlink playback rather than text-only
  TTS state changes:
  - `playback start: stream=xiaozhi_tts ...`
  - `playback stop: stream=xiaozhi_tts ...`
- playback stop arms the current Step `5.148` guard:
  - `xiaozhi no_ref reopen guard armed: tail_ms=480 silence_frames=6`
- `river xiaozhi status` reports live downlink audio counters and runtime audio
  format:
  - `audio_rx=...` with a value greater than `0`
  - `server_audio=24000Hz/20ms`
  - `sample=24000Hz frame=20ms`

Observed result on 2026-04-13:
- all items above passed
- the validated run showed:
  - `server hello: sid=29fefc9e sample_rate=24000 frame_duration=20ms mcp=yes`
  - `playback start: stream=xiaozhi_tts ...`
  - `playback stop: stream=xiaozhi_tts epoch=3`
  - `xiaozhi no_ref reopen guard armed: tail_ms=480 silence_frames=6`
  - `audio_rx=280`
- this automated run still did not print:
  - `xiaozhi no_ref reopen rearmed after silence: ...`
- so the next runtime check should focus specifically on silence rearm after
  playback, not on downlink transport bring-up

## Step 5.152
Run the current XiaoZhi Step-A board validation and classify the remaining
blocker precisely:
```bash
cd /root/ameba-river
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 /root/ameba-river/tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000
```

Then run these monitor commands:
- `river xiaozhi status`
- `river xiaozhi bootstrap`
- `river xiaozhi connect`
- `river xiaozhi listen detect 你好`
- `river xiaozhi listen detect 今天天气怎么样`

Expected result:
- build output ends with `Build done`
- flash output ends with `Finished PASS`
- `river xiaozhi status` prints the Step `5.148` no-ref reopen status line:
  - `xiaozhi no_ref reopen rearm=... silence=... guard_left_ms=... open_hold_frames=...`
- `bootstrap` and `connect` reach:
  - `xiaozhi ota bootstrap ok`
  - `server hello: sid=...`
- `listen detect` reaches text/event flow:
  - `stt ...`
  - `llm ...`
  - `tts sid=... state=start`
  - `tts sid=... state=stop`

Current observed result on 2026-04-13:
- all items above passed
- but both automated detect runs still showed:
  - `audio_rx=0`
  - no `playback start`
  - no `playback stop`
- therefore this step does not yet prove the final target logs:
  - `xiaozhi no_ref reopen guard armed: ...`
  - `xiaozhi no_ref reopen rearmed after silence: ...`
- next verification must use a real interaction that produces actual downlink
  audio or manual wake/speak/follow-up speech on board

## Step 5.151
Validate the first pinned live active plan and its harness linkage:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
```

Expected result:
- the script exits successfully
- output contains:
  - `Current branch: kws`
  - `check_codex_harness: all checks passed`
- no traceback or `FAIL` lines are printed
- this confirms:
  - `.codex/active_plans.md` has a parseable primary active plan
  - the primary active plan file exists on disk
  - `.codex/active_context.md` points to that same live plan
  - the rest of the Codex harness entry points still agree

## Step 5.150
Validate the active-plan workflow after adding the new plan index and template:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
```

Expected result:
- the script exits successfully
- output contains:
  - `Current branch: kws`
  - `check_codex_harness: all checks passed`
- no `FAIL` lines are printed
- this confirms the Codex-facing entry points now also agree on:
  - the active-plan index `.codex/active_plans.md`
  - the execution-plan template `doc/EXECUTION_PLAN_TEMPLATE_ZH.md`
  - the active-context to active-plan linkage
  - the README / AGENTS / doc index exposure of the plan workflow

## Step 5.149
Validate the Codex harness entry points after the context cleanup:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
```

Expected result:
- the script exits successfully
- output contains:
  - `Current branch: kws`
  - `check_codex_harness: all checks passed`
- no `FAIL` lines are printed
- this confirms the current Codex-facing entry points agree on:
  - the canonical active-context file
  - the default SDK baseline `/root/ameba-rtos`
  - the current git branch
  - the historical status of root `plan.md`

## Step 5.148
Rebuild the latest-SDK image after tightening `no_ref` follow-up reopen:
```bash
cd /root/ameba-river
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python /root/ameba-rtos/ameba.py build -p'
```

Expected result:
- the build completes successfully
- final output contains:
  - `Build done`

Flash the rebuilt image:
```bash
cd /root/ameba-river
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'
```

Expected result:
- flash finishes with `Finished PASS`

Capture a fresh wake -> speak -> TTS -> follow-up log:
```bash
cd /root/ameba-river
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000
```

Expected result:
- when playback stops under the current `mode=no_ref` profile, the bridge now
  arms a local reopen guard:
  - `xiaozhi no_ref reopen guard armed: tail_ms=480 silence_frames=6`
- after playback really settles, the bridge rearms follow-up only after a short
  silence run:
  - `xiaozhi no_ref reopen rearmed after silence: frames=6`
- status dump now exposes the reopen state directly:
  - `xiaozhi no_ref reopen rearm=... silence=... guard_left_ms=... open_hold_frames=...`
- the earlier false immediate reopen after playback stop should disappear:
  - avoid the previous pattern where a new round starts only a few tens of
    milliseconds after `playback stop`
  - especially avoid the empty round symptom:
    - `xiaozhi asr round finish: ... partial=0 final=0 busy=0 fail=0 stale_drop=0 ring_drop=0`
- legitimate follow-up should still work after the guard:
  - a real next utterance should still produce:
    - `asr provider=xiaozhi_realtime session started`
    - `xiaozhi asr round begin: ...`
    - `partial` / `final` results for that round

## Step 5.147
Rebuild the latest-SDK image after moving XiaoZhi transport ownership to a
single project-side I/O thread and adding per-round ASR stats:
```bash
cd /root/ameba-river
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python /root/ameba-rtos/ameba.py build -p'
```

Expected result:
- the build completes successfully
- final output contains:
  - `Build done`

Flash the rebuilt image:
```bash
cd /root/ameba-river
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'
```

Expected result:
- flash finishes with `Finished PASS`

Capture a fresh wake-and-speak log:
```bash
cd /root/ameba-river
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000
```

Expected result:
- boot now shows the project-side owner split instead of the old project-side
  pump/uplink worker split:
  - `xiaozhi io owner started`
  - `xiaozhi downlink worker started`
- status dump exposes the new ownership and queue surfaces:
  - `xiaozhi runtime enabled=yes io=running ...`
  - `xiaozhi control queue=...`
  - `xiaozhi uplink queue=... owner=running`
  - `xiaozhi asr round id=...`
- after a wake and one ASR round, the log now contains a bounded per-round
  begin/finish pair:
  - `xiaozhi asr round begin: id=... sid=... pre_roll_frames=...`
  - `xiaozhi asr round finish: id=... sid=... reason=... duration_ms=... first_packet_delay_ms=... packets=... busy=... fail=... stale_drop=... ring_drop=... partial=... final=...`
- wake/listen control should continue to work through the owner queue:
  - `xiaozhi wake admission transport ready:`
  - `xiaozhi wake admission listen_start sent:`
  - `xiaozhi wake admission ready:`
  - `asr provider=xiaozhi_realtime session started`
- when transport pressure reproduces, the new round-finish log should let you
  tell whether the problem is:
  - control-plane serialization
  - websocket backpressure / `busy`
  - local stale-drop or ring-drop
  without correlating multiple unrelated counters by hand

## Step 5.146
Rebuild the latest-SDK image after adding XiaoZhi pump fairness yielding:
```bash
cd /root/ameba-river
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python /root/ameba-rtos/ameba.py soc RTL8730E; python /root/ameba-rtos/ameba.py build -p'
```

Expected result:
- the build completes successfully
- final output contains:
  - `Build done`

Flash the rebuilt image:
```bash
cd /root/ameba-river
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'
```

Expected result:
- flash finishes with `Finished PASS`

Capture a fresh wake log and say the wake word once:
```bash
cd /root/ameba-river
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000
```

Expected result:
- after:
  - `server hello: sid=...`
  - `xiaozhi wake admission transport ready: source=wakeword sid=...`
- the control path now continues instead of stopping there:
  - `xiaozhi wake admission listen_start sent: source=wakeword sid=...`
  - `xiaozhi wake admission ready: source=wakeword listening=yes window=open sid=...`
  - `wakeword admission accepted: text=... confidence=...`
- ASR startup resumes:
  - `asr provider=xiaozhi_realtime session started sid=...`
  - `asr stream active: provider=xiaozhi_realtime pre_roll_frames=...`
- the wake worker should no longer appear wedged behind a live websocket
  session:
  - no repeated pattern where KWS keeps retriggering while an earlier wake is
    stuck forever after `transport ready`

## Step 5.145
Rebuild the latest-SDK image after tightening xiaozhi uplink backlog control:
```bash
cd /root/ameba-river
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python /root/ameba-rtos/ameba.py soc RTL8730E; python /root/ameba-rtos/ameba.py build -p'
```

Expected result:
- the build completes successfully
- final output contains:
  - `Build done`

Flash the rebuilt image:
```bash
cd /root/ameba-river
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'
```

Expected result:
- flash finishes with `Finished PASS`

Capture a fresh wake-and-speak log:
```bash
cd /root/ameba-river
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000
```

Expected result:
- first wake still reaches:
  - `xiaozhi wake admission ready:`
  - `asr provider=xiaozhi_realtime session started`
- xiaozhi stream start now shows a smaller backlog burst:
  - `asr stream active: provider=xiaozhi_realtime pre_roll_frames=8`
    or otherwise clearly smaller than the previous `16`
- the previous local overflow storm is gone or materially reduced:
  - no long run of `xiaozhi uplink ring overflow: dropped=... queued=64 capacity=64`
- if transport pressure still exists, it should now show up as bounded realtime
  shedding instead of full-ring saturation:
  - `stale_drop=` may increase
  - occasional `xiaozhi uplink backpressure:` is acceptable
  - follow-up wake should no longer get stuck behind a long lingering
    `listen_stop_pending` drain

## Step 5.144
Rebuild the latest-SDK image after the uplink pacing change:
```bash
cd /root/ameba-river
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python /root/ameba-rtos/ameba.py soc RTL8730E; python /root/ameba-rtos/ameba.py build -p'
```

Expected result:
- the build completes successfully
- final output contains:
  - `Build done`

Flash the rebuilt image:
```bash
cd /root/ameba-river
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'
```

Expected result:
- flash finishes with `Finished PASS`

Capture a fresh wake-and-speak log after the first successful admission:
```bash
cd /root/ameba-river
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000
```

Expected result:
- the first wake still reaches:
  - `xiaozhi wake admission ready:`
  - `wakeword admission accepted:`
  - `asr provider=xiaozhi_realtime session started`
- during the following `asr_streaming` window, the previous local overflow storm
  is gone or materially reduced:
  - no long run of `xiaozhi uplink ring overflow: dropped=... queued=64 capacity=64`
- if transport pressure still exists, it should now show up primarily as the
  existing throttled backpressure signal instead of local ring saturation:
  - occasional `xiaozhi uplink backpressure:`
  - possible `stale_drop=` growth without the ring pinning at `64/64`

## Step 5.143
Rebuild the latest-SDK image after adding wake-admission tracing:
```bash
cd /root/ameba-river
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python /root/ameba-rtos/ameba.py soc RTL8730E; python /root/ameba-rtos/ameba.py build -p'
```

Expected result:
- the build completes successfully
- final output contains:
  - `Build done`

Flash the rebuilt image:
```bash
cd /root/ameba-river
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'
```

Expected result:
- flash finishes with `Finished PASS`

Capture a fresh wake log for the current stuck-after-hello case:
```bash
cd /root/ameba-river
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000
```

Expected result:
- around a successful wake, the log now shows the exact admission progression:
  - `xiaozhi wake admission begin:`
  - `xiaozhi wake admission transport ready:`
  - then either:
    - `xiaozhi wake admission listen_start sent:`
    - `xiaozhi conversation window opened:`
    - `xiaozhi wake admission ready:`
    - `wakeword admission accepted:`
  - or a precise failure point:
    - `xiaozhi wake admission open_session failed:`
    - or `xiaozhi wake admission listen_start failed:`
- compare those lines with the still-missing higher-level state transitions:
  - `interaction_state: wake_monitoring -> wake_confirmed`
  - `asr provider=xiaozhi_realtime session started`

## Step 5.142
Rebuild the latest-SDK image after the websocket queue / poll-drive change:
```bash
cd /root/ameba-river
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python /root/ameba-rtos/ameba.py soc RTL8730E; python /root/ameba-rtos/ameba.py build -p'
```

Expected result:
- the build completes successfully
- final output contains:
  - `Build done`

Flash the rebuilt image to the board:
```bash
cd /root/ameba-river
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'
```

Expected result:
- flash finishes with `Finished PASS`

Capture a fresh runtime log while reproducing the same wake-and-speak case that
previously produced `xiaozhi ws backpressure`:
```bash
cd /root/ameba-river
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000
```

Expected result:
- wake / cloud handoff still works:
  - `wakeword hit:`
  - `wakeword queued`
  - `xiaozhi connecting:`
  - `Connected to websocket server`
  - `server hello:`
  - `asr provider=xiaozhi_realtime session started`
- the earlier repeated post-handoff cleanup regression does not return:
  - no continuous stream of
    `kws disarm: begin clear_pre_roll=yes gate=closed queue_reset=yes pre_reset=yes`
- if websocket queue pressure still appears, the new queue depth is visible in
  the log:
  - `xiaozhi ws backpressure: kind=audio ... max=16 ...`
  - not the old `max=8`
- under the same utterance and WLAN conditions as the 2026-04-10 14:58:38 to
  14:58:39 failure window, `xiaozhi ws backpressure`,
  `xiaozhi uplink backpressure`, and `stale_drop` should occur less often than
  before; if it still reproduces, record the exact lines containing:
  - `ready=`
  - `recycle=`
  - `queued=`
  - `busy=`
  - `streak=`
  - `stale_drop=`

## Step 5.141
Rebuild the latest-SDK image after the KWS post-wake disarm fix:
```bash
cd /root/ameba-river
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python /root/ameba-rtos/ameba.py soc RTL8730E; python /root/ameba-rtos/ameba.py build -p'
```

Expected result:
- the build completes successfully
- final output contains:
  - `Build done`

Flash the rebuilt image and capture a fresh wake log:
```bash
cd /root/ameba-river
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py /dev/ttyUSB0 1500000 | tee /tmp/kws_post_wake_disarm_fix.log
```

Expected result:
- flash finishes with `Finished PASS`
- after one successful wake, the log still shows the normal handoff chain:
  - `wakeword hit:`
  - `wakeword queued`
  - `interaction_state: wake_monitoring -> wake_confirmed`
- but it should no longer show a burst of repeated:
  - `kws disarm: begin clear_pre_roll=yes gate=closed queue_reset=yes pre_reset=yes`
- at most one extra blocked-path cleanup immediately after handoff is
  acceptable; a continuous per-frame `gate=closed` disarm stream is not

## Step 5.140
Confirm the tracked board-profile config now selects the nano FP32 debug path:
```bash
cd /root/ameba-river
rg -n "STUDENT_CONV_RESNET_ED_NANO_V1_FP32_DEBUG|RIVER_KWS_SCORE_THRESHOLD_Q15=9008|RIVER_KWS_TENSOR_ARENA_KB=2048|RIVER_KWS_INFERENCE_STRIDE_FRAMES=16" \
  prj.conf
```

Expected result:
- `prj.conf` enables:
  - `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_CONV_RESNET_ED_NANO_V1_FP32_DEBUG=y`
  - `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=9008`
- the existing `40x101` debug arena / stride settings remain unchanged

Rebuild the latest-SDK image through the official external-project entrypoint:
```bash
cd /root/ameba-river
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python /root/ameba-rtos/ameba.py soc RTL8730E; python /root/ameba-rtos/ameba.py build -p'
```

Expected result:
- configure source is:
  - `/root/ameba-rtos/component/soc/amebasmart/project`
- configure uses:
  - `-DEXTERN_DIR=/root/ameba-river`
  - `-DEXAMPLE=/root/ameba-river`
- final output ends with `Build done`

Confirm the generated build config and linked library now point to the nano FP32 variant:
```bash
cd /root/ameba-river
rg -n "CONV_RESNET_ED_NANO_V1|CONV_RESNET_ED_TINY_V1|SCORE_THRESHOLD_Q15 9008" \
  build_RTL8730E/menuconfig/.config \
  build_RTL8730E/menuconfig/.config_ca32 \
  build_RTL8730E/menuconfig/project_ap/platform_autoconf.h \
  build_RTL8730E/build/project_ap/.config_ca32
strings build_RTL8730E/build/project_ap/make/image2/example/ameba-river/components/river_voice/lib_river_voice.a | \
  rg "student_conv_resnet_ed_(nano|tiny)_v1_fp32_debug|student_dscnn_tiny_v2_int8_debug"
```

Expected result:
- generated config enables:
  - `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_CONV_RESNET_ED_NANO_V1_FP32_DEBUG=y`
- tiny / dscnn active variants are not set
- `platform_autoconf.h` contains:
  - `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15 9008`
- `lib_river_voice.a` contains:
  - `student_conv_resnet_ed_nano_v1_fp32_debug`

Flash the rebuilt image:
```bash
cd /root/ameba-river
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'
```

Expected result:
- flash finishes with `Finished PASS`

Confirm the packaged image sizes:
```bash
cd /root/ameba-river
stat -c '%n %s' \
  build_RTL8730E/build/project_ap/image/ap_image_all.bin \
  build_RTL8730E/build/project_hp/image/km4_image2_all.bin \
  build_RTL8730E/build/project_lp/image/km0_image2_all.bin \
  build_RTL8730E/km0_km4_ca32_app.bin \
  build_RTL8730E/km4_boot_all.bin
```

Expected result:
- `ap_image_all.bin 3275872`
- `km4_image2_all.bin 380448`
- `km0_image2_all.bin 94208`
- `km0_km4_ca32_app.bin 3758720`
- `km4_boot_all.bin 51872`

Confirm the boot contract from the captured board log:
```bash
cd /root/ameba-river
rg -a -n "runtime_in=float32 runtime_out=float32|dims=\\[1,40,101,1\\]|variant=student_conv_resnet_ed_nano_v1_fp32_debug|arena_used=436528|threshold_q15=9008|kws frontend:" \
  /tmp/kws_conv_resnet_ed_nano_fp32_align.log
```

Expected result:
- boot log shows:
  - `variant=student_conv_resnet_ed_nano_v1_fp32_debug`
  - `runtime_in=float32 runtime_out=float32`
  - `dims=[1,40,101,1]`
  - `arena_used=436528`
  - `threshold_q15=9008`

Confirm the board/host replay parity result:
```bash
cd /root/ameba-river
TF_ENABLE_ONEDNN_OPTS=0 \
OMP_NUM_THREADS=1 \
TF_NUM_INTRAOP_THREADS=1 \
TF_NUM_INTEROP_THREADS=1 \
python3 tools/kws/replay_board_tensor_dump.py \
  --log /tmp/kws_conv_resnet_ed_nano_fp32_align.log \
  --model /root/kws-trainint/artifacts/exports/student_conv_resnet_ed_nano_v1/model.fp32.tflite \
  --seq latest \
  --builtin-ref
```

Expected result:
- replay prints:
  - `board_hash: feature=0x7ce0b11d input=0xd52f011c`
  - `host_hash: feature=0x7ce0b11d logged_input=n/a effective_input=0xd52f011c`
  - `board_output: raw=306 score=0.305877 exact=0.305877 q15=10023`
  - `host_output: raw=306 score=0.306000 exact=0.305877`
  - `output_parity: bytes_equal=yes raw_equal=yes first_diff=[]`

Confirm the current runtime snapshot and report:
```bash
cd /root/ameba-river
rg -a -n "kws status:|kws perf: infer_us\\[last=29826 avg=29826 max=29826|mem\\[arena=436528/2048KB" \
  /tmp/kws_conv_resnet_ed_nano_fp32_status.log
sed -n '1,260p' doc/RUNTIME_RESOURCE_PROFILE_2026-04-10_STUDENT_CONV_RESNET_ED_NANO_FP32_DEBUG_ZH.md
```

Expected result:
- runtime snapshot shows:
  - `infer_us[last=29826 avg=29826 max=29826]`
  - `arena=436528/2048KB`
- the report clearly states:
  - output parity closed
  - `29.826 ms` vs `18.0 ms`
  - `436528 B` vs `384 KB`
  - nano is about `3.23x` faster than the matching `conv_resnet_ed_tiny` FP32 debug path

## Step 5.139
Confirm the new `conv_resnet_ed_nano` FP32 debug variant is wired into the repo:
```bash
cd /root/ameba-river
rg -n "STUDENT_CONV_RESNET_ED_NANO_V1|student_conv_resnet_ed_nano_v1_fp32" \
  Kconfig \
  components/river_voice/river_voice_kws.cc \
  components/river_voice/generated/student_conv_resnet_ed_nano_v1_fp32_model_data.h \
  build_RTL8730E/menuconfig/prj.conf
```

Expected result:
- `Kconfig` contains:
  - `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_CONV_RESNET_ED_NANO_V1_FP32_DEBUG`
- `components/river_voice/river_voice_kws.cc` contains:
  - `variant=student_conv_resnet_ed_nano_v1_fp32_debug`
- the generated header exists:
  - `components/river_voice/generated/student_conv_resnet_ed_nano_v1_fp32_model_data.h`
- `build_RTL8730E/menuconfig/prj.conf` enables:
  - `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_CONV_RESNET_ED_NANO_V1_FP32_DEBUG=y`

Confirm the imported header matches the algorithm FP32 export exactly:
```bash
cd /root/ameba-river
python3 - <<'PY'
from pathlib import Path
import hashlib, re
header = Path('components/river_voice/generated/student_conv_resnet_ed_nano_v1_fp32_model_data.h').read_text()
data = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', header))
model = Path('/root/kws-trainint/artifacts/exports/student_conv_resnet_ed_nano_v1/model.fp32.tflite').read_bytes()
print('header_bytes', len(data))
print('model_bytes', len(model))
print('header_sha256', hashlib.sha256(data).hexdigest())
print('model_sha256', hashlib.sha256(model).hexdigest())
print('exact_match', 'yes' if data == model else 'no')
PY
```

Expected result:
- `header_bytes 141700`
- `model_bytes 141700`
- both SHA256 are:
  - `5c955b390db469ddd5d82c22c2b00022eb8a82f596e4e5dd2194d3c7b07c7727`
- `exact_match yes`

## Step 5.138
Read the finalized FP32 deployment / parity / performance report:
```bash
cd /root/ameba-river
sed -n '1,260p' doc/RUNTIME_RESOURCE_PROFILE_2026-04-10_STUDENT_CONV_RESNET_ED_TINY_FP32_DEBUG_ZH.md
```

Confirm the board boot contract for the current FP32 image:
```bash
cd /root/ameba-river
rg -a -n "kws init plan:|runtime_in=float32 runtime_out=float32|dims=\\[1,40,101,1\\]|arena_used=654960B|variant=student_conv_resnet_ed_tiny_v1_fp32_debug|threshold_q15=9038" \
  /tmp/kws_conv_resnet_ed_tiny_fp32_reboot_boot.log
```

Expected result:
- boot log shows:
  - `variant=student_conv_resnet_ed_tiny_v1_fp32_debug`
  - `runtime_in=float32 runtime_out=float32`
  - `dims=[1,40,101,1]`
  - `arena_used=654960B`
  - `threshold_q15=9038`

Confirm board/header vs algorithm `.tflite` file identity:
```bash
cd /root/ameba-river
python3 - <<'PY'
from pathlib import Path
import hashlib, re
header = Path('components/river_voice/generated/student_conv_resnet_ed_tiny_v1_fp32_model_data.h').read_text()
data = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', header))
model = Path('/root/kws-trainint/artifacts/exports/student_conv_resnet_ed_tiny_v1/model.fp32.tflite').read_bytes()
print('header_bytes', len(data))
print('header_sha256', hashlib.sha256(data).hexdigest())
print('model_bytes', len(model))
print('model_sha256', hashlib.sha256(model).hexdigest())
print('exact_match', 'yes' if data == model else 'no')
PY
```

Expected result:
- `header_bytes 479008`
- `model_bytes 479008`
- both SHA256 are:
  - `3fa05447484ba77a6b24da050da1867271ae2d77fbd27ae1fd2144a682376e66`
- `exact_match yes`

Confirm the finalized host replay parity result:
```bash
cd /root/ameba-river
TF_ENABLE_ONEDNN_OPTS=0 \
OMP_NUM_THREADS=1 \
TF_NUM_INTRAOP_THREADS=1 \
TF_NUM_INTEROP_THREADS=1 \
python3 tools/kws/replay_board_tensor_dump.py \
  --log /tmp/kws_conv_resnet_ed_tiny_fp32_exact_parity.log \
  --model /root/kws-trainint/artifacts/exports/student_conv_resnet_ed_tiny_v1/model.fp32.tflite \
  --seq latest \
  --builtin-ref
```

Expected result:
- replay prints:
  - `note: using feature tensor bytes as effective input because they match the board input hash`
  - `board_hash: feature=0x7ce0b11d input=0xd52f011c`
  - `host_hash: feature=0x7ce0b11d logged_input=n/a effective_input=0xd52f011c`
  - `board_output: raw=345 score=0.345300 exact=0.345300 q15=11314`
  - `host_output: raw=345 score=0.345000 exact=0.345300`
  - `output_parity: bytes_equal=yes raw_equal=yes first_diff=[]`

## Step 5.137
Confirm the new `conv_resnet_ed` debug variants are wired into the repo:
```bash
cd /root/ameba-river
rg -n "STUDENT_CONV_RESNET_ED_TINY_V1|student_conv_resnet_ed_tiny_v1_" \
  Kconfig \
  components/river_voice/river_voice_kws.cc \
  components/river_voice/generated
```

Expected result:
- `Kconfig` contains both new choices:
  - `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_CONV_RESNET_ED_TINY_V1_FP32_DEBUG`
  - `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_CONV_RESNET_ED_TINY_V1_INT8_DEBUG`
- `components/river_voice/river_voice_kws.cc` contains both runtime branches:
  - `variant=student_conv_resnet_ed_tiny_v1_fp32_debug`
  - `variant=student_conv_resnet_ed_tiny_v1_int8_debug`
- the generated headers exist under:
  - `components/river_voice/generated/student_conv_resnet_ed_tiny_v1_fp32_model_data.h`
  - `components/river_voice/generated/student_conv_resnet_ed_tiny_v1_int8_model_data.h`

Spot-check the imported model header sizes:
```bash
cd /root/ameba-river
rg -n "Size: 479008 bytes|Size: 135504 bytes" \
  components/river_voice/generated/student_conv_resnet_ed_tiny_v1_fp32_model_data.h \
  components/river_voice/generated/student_conv_resnet_ed_tiny_v1_int8_model_data.h
```

Expected result:
- FP32 header reports `Size: 479008 bytes`
- INT8 header reports `Size: 135504 bytes`

## Step 5.136
Read the FP32 / INT8 comparison note:
```bash
cd /root/ameba-river
sed -n '1,260p' doc/KWS_DSCNN_TINY_FP32_INT8_COMPARISON_2026-04-10_ZH.md
```

Confirm the corresponding FP32 board baseline had already been deployed:
```bash
cd /root/ameba-river
rg -n "student_dscnn_tiny_v2_fp32_debug|infer_us\\[last=183988 avg=183969 max=183988\\]|arena 实际使用 \\| `1168336 B`" \
  doc/RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_DSCNN_TINY_FP32_DEBUG_ZH.md
```

Expected result:
- the FP32 report exists
- it clearly states:
  - `student_dscnn_tiny_v2_fp32_debug`
  - `infer_us[last=183988 avg=183969 max=183988]`
  - arena used `1168336 B`

Confirm the current INT8 board baseline:
```bash
cd /root/ameba-river
rg -n "student_dscnn_tiny_v2_int8_debug|infer_us ≈ 492\\.7 ms|last=492733|avg=492784|max=493321|arena=295764/2048KB|quant_parity: diff_bytes=0/4040|output_parity: bytes_equal=yes raw_equal=yes" \
  doc/RUNTIME_RESOURCE_PROFILE_2026-04-10_STUDENT_DSCNN_TINY_INT8_DEBUG_ZH.md
```

Expected result:
- the INT8 report exists
- it clearly states:
  - exact parity passed
  - `infer_us` around `492.7 ms`
  - arena used `295764 B`

Re-check the current latest-SDK CA32 quantized-kernel source path:
```bash
cd /root/ameba-river
nl -ba /root/ameba-rtos/component/tflite_micro/tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/conv.cc | sed -n '248,267p'
nl -ba /root/ameba-rtos/component/tflite_micro/tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/depthwise_conv.cc | sed -n '141,158p'
```

Expected result:
- `conv.cc` still shows:
  - `The current CA32 int8 conv optimized path is not reliable`
  - immediate call to `reference_integer_ops::ConvPerChannel(...)`
- `depthwise_conv.cc` still shows:
  - `The CA32 optimized int8 depthwise kernel is not reliable`
  - immediate call to `reference_integer_ops::DepthwiseConvPerChannel(...)`

Interpretation:
- this step is complete once the local repo records all three facts together:
  - the matching FP32 model had already been deployed
  - the matching INT8 model is exact-parity correct but about `2.68x` slower
  - the current latest-SDK source still routes the relevant CA32 INT8 compute
    path through reference kernels rather than a restored optimized path

## Step 5.135
Read the parity-status note for the current active INT8 model:
```bash
cd /root/ameba-river
sed -n '1,260p' doc/KWS_DSCNN_TINY_INT8_PARITY_STATUS_2026-04-10_ZH.md
```

Flash the current `HEAD` image:
```bash
cd /root/ameba-river
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
```

Capture one full INT8 exact-parity run with raw serial automation:
```bash
cd /root/ameba-river
python3 - <<'PY'
import serial, threading, time
from pathlib import Path

port = '/dev/ttyUSB0'
baud = 1500000
log_path = Path('/tmp/kws_dscnn_tiny_int8_exact_parity_20260410.log')
stop = False

with serial.Serial(port, baud, timeout=0.1) as ser, log_path.open('wb') as f:
    def reader():
        while not stop:
            data = ser.read(4096)
            if data:
                f.write(data)
                f.flush()
    t = threading.Thread(target=reader, daemon=True)
    t.start()

    def send(cmd: str, delay: float = 0.0):
        ser.write(cmd.encode('utf-8') + b'\r')
        ser.flush()
        if delay:
            time.sleep(delay)

    send('reboot', 0.40)
    send('river kws debug local on', 8.60)
    send('river audio probe stop', 0.80)
    send('river kws debug local off', 0.40)
    send('river kws align run', 6.80)
    send('river kws dump meta', 0.60)
    send('river kws dump chunk output_raw 1', 0.05)
    for i in range(1, 254):
        send(f'river kws dump chunk feat_f32 {i}', 0.03)
    for i in range(1, 65):
        send(f'river kws dump chunk input_raw {i}', 0.03)
    send('river kws align status', 1.00)
    time.sleep(2.0)
    stop = True
    t.join(timeout=1.0)

print(log_path)
PY
```

Confirm the board log contains the full current-model dump:
```bash
cd /root/ameba-river
rg -n "variant=student_dscnn_tiny_v2_int8_debug|kws align replay done|kws tensor dump begin|kws tensor dump meta:|kws tensor dump output_raw:|kws tensor dump feat_f32:|kws tensor dump input_raw:|kws align guard:" \
  /tmp/kws_dscnn_tiny_int8_exact_parity_20260410.log
```

Expected board result:
- the boot log contains:
  - `variant=student_dscnn_tiny_v2_int8_debug`
- the alignment command returns successfully:
  - `kws align replay done: dump=preserved local_only_restored=no`
- the preserved snapshot is complete:
  - one `output_raw 1/1`
  - `feat_f32 1/253` through `253/253`
  - `input_raw 1/64` through `64/64`
- `river kws align status` still executes after the dump, proving the shell is
  still usable

Replay the captured dump on host:
```bash
cd /root/ameba-river
python3 tools/kws/replay_board_tensor_dump.py \
  --log /tmp/kws_dscnn_tiny_int8_exact_parity_20260410.log \
  --model /root/kws-trainint/artifacts/exports/student_dscnn_tiny_v2/model.int8.tflite \
  --seq latest
```

Expected replay result:
- `board_hash: feature=0xf6cf59f0 input=0x1bb7980a`
- `host_hash: feature=0xf6cf59f0 logged_input=0x1bb7980a effective_input=0x1bb7980a source=input_raw`
- `quant_parity: diff_bytes=0/4040 first_diff=[]`
- `board_output: raw=-52 score=0.296875 q15=9728`
- `host_output: raw=-52 score=0.296875`
- `output_parity: bytes_equal=yes raw_equal=yes first_diff=[]`

Confirm the current board-side performance markers from the same log:
```bash
cd /root/ameba-river
rg -n "kws perf: infer_us\\[last=492733 avg=492784 max=493321|arena=295764/2048KB slack=1801388|kws align replay captured: seq=1 infer=9 score=0.296875 q15=9728" \
  /tmp/kws_dscnn_tiny_int8_exact_parity_20260410.log
```

Interpretation:
- this step is complete once the currently active INT8 image is proven
  board/host exact-parity correct
- after this point, the remaining question for
  `student_dscnn_tiny_v2_int8_debug` is no longer deployment correctness; it
  is runtime value at roughly `~493ms` board latency

## Step 5.134
Build the stack-pressure test image where the alignment trailing-silence frame
no longer lives on the shell task stack:
```bash
cd /root/ameba-river
bash -lc 'source /root/ameba-river/env.sh >/dev/null && python /root/ameba-rtos/ameba.py soc RTL8730E && python /root/ameba-rtos/ameba.py build -p'
```

Flash it:
```bash
cd /root/ameba-river
stty -F /dev/ttyUSB0 1500000 raw -echo
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
```

Capture the controlled reboot-to-align sequence:
```bash
cd /root/ameba-river
stty -F /dev/ttyUSB0 1500000 raw -echo
script -q -f /tmp/kws_int8_stack_pressure_probe.log -c "timeout 38s cat /dev/ttyUSB0"
```

While the capture is running, execute:
```bash
cd /root/ameba-river
bash -lc 'printf "reboot\r" > /dev/ttyUSB0; sleep 0.40; printf "river kws debug local on\r" > /dev/ttyUSB0; sleep 8.60; printf "river audio probe stop\r" > /dev/ttyUSB0; sleep 0.80; printf "river kws debug local off\r" > /dev/ttyUSB0; sleep 0.40; printf "river kws align run\r" > /dev/ttyUSB0; sleep 6.50; printf "river kws dump meta\r" > /dev/ttyUSB0; sleep 1.00; printf "river kws align status\r" > /dev/ttyUSB0'
```

Extract the decisive markers:
```bash
cd /root/ameba-river
rg -n "variant=|runtime_in=|shell_task|kws align replay captured|kws align replay done|\\[river\\]\\[diag\\] kws align run returned|\\[river\\]\\[diag\\] kws dump meta returned|kws tensor dump meta:|kws align guard:|WIFI TRX IPC 4 timeout|river kws dump meta|river kws align status" /tmp/kws_int8_stack_pressure_probe.log
```

Expected current board result:
- the image still boots as:
  - `variant=student_dscnn_tiny_v2_int8_debug`
  - `runtime_in=int8 runtime_out=int8`
- early runtime still shows limited shell-task headroom:
  - `shell_task:1%/700B`
- the align replay still succeeds:
  - `kws align replay captured: seq=1 infer=2 score=0.296875 q15=9728`
  - `kws align replay done: dump=preserved local_only_restored=no`
  - `[river][diag] kws align run returned status=0`

Expected improvement over the previous baseline:
- `river kws dump meta` now executes after `align run returned`
- the board prints:
  - `kws tensor dump meta: ... score=0.296875 q15=9728 ...`
  - `[river][diag] kws dump meta returned`
- `river kws align status` also executes after that
- the board prints:
  - `kws align guard: kws=ready probe=stopped interaction=wake_monitoring detection=ready worker=idle snapshot=ready local_only=no`
- no `[INIC-E] WIFI TRX IPC 4 timeout` appears in the captured align window

Interpretation:
- this step is complete once the board proves that moving the `512B`
  trailing-silence buffer off the shell task stack is enough to restore
  post-align monitor usability
- that is strong evidence the previous post-return failure was driven by
  shell-side stack pressure rather than by the replay function itself

## Step 5.133
Flash the return-path-instrumented image and reproduce the post-return INT8
alignment behavior on board:
```bash
cd /root/ameba-river
stty -F /dev/ttyUSB0 1500000 raw -echo
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
```

Capture the controlled reboot-to-align sequence:
```bash
cd /root/ameba-river
stty -F /dev/ttyUSB0 1500000 raw -echo
script -q -f /tmp/kws_int8_return_probe.log -c "timeout 38s cat /dev/ttyUSB0"
```

While that capture is active, run:
```bash
cd /root/ameba-river
bash -lc 'printf "reboot\r" > /dev/ttyUSB0; sleep 0.40; printf "river kws debug local on\r" > /dev/ttyUSB0; sleep 8.60; printf "river audio probe stop\r" > /dev/ttyUSB0; sleep 0.80; printf "river kws debug local off\r" > /dev/ttyUSB0; sleep 0.40; printf "river kws align run\r" > /dev/ttyUSB0; sleep 6.50; printf "river kws dump meta\r" > /dev/ttyUSB0; sleep 1.00; printf "river kws align status\r" > /dev/ttyUSB0'
```

Extract the decisive markers:
```bash
cd /root/ameba-river
rg -n "variant=|runtime_in=|kws debug local_only|vad probe stopped|kws align replay captured|kws align replay done|\\[river\\]\\[diag\\] kws align run returned|\\[river\\]\\[diag\\] kws dump meta returned|WIFI TRX IPC 4 timeout|river kws dump meta|river kws align status" /tmp/kws_int8_return_probe.log
```

Expected current board result:
- the flashed image boots as:
  - `variant=student_dscnn_tiny_v2_int8_debug`
  - `runtime_in=int8 runtime_out=int8`
- the controlled sequence is accepted:
  - `kws debug local_only: enabled=yes ...`
  - `vad probe stopped`
  - `kws debug local_only: enabled=no ...`
- the align replay fully completes:
  - `kws align replay captured: seq=1 infer=12 score=0.296875 q15=9728`
  - `kws align replay done: dump=preserved local_only_restored=no`
- the new diag marker appears:
  - `[river][diag] kws align run returned status=0`

Expected remaining failure:
- immediately after the returned marker:
  - `[INIC-E] WIFI TRX IPC 4 timeout`
- later commands are only echoed and do not execute:
  - `river kws dump meta`
  - `river kws align status`
- no `[river][diag] kws dump meta returned` line appears

Optional confirmation probe from the same post-align state:
```bash
cd /root/ameba-river
timeout 6s cat /dev/ttyUSB0
bash -lc "printf 'reboot\r' > /dev/ttyUSB0"
```

Expected current result:
- no clean reboot boot log is observed in the probe window

Interpretation:
- this step is complete once board logs prove that:
  - `river_voice_kws_run_alignment_sample(...)` already returned successfully
  - the post-align failure now lives after the command returns, in monitor /
    parser / broader system state rather than in the replay function itself

## Step 5.132
Confirm the new monitor return-path instrumentation is present:
```bash
cd /root/ameba-river
nl -ba components/river_diag/river_diag_cmd.c | sed -n '211,214p'
nl -ba components/river_diag/river_diag_cmd.c | sed -n '262,271p'
```

Build the updated image against the latest SDK baseline:
```bash
cd /root/ameba-river
bash -lc 'source /root/ameba-river/env.sh >/dev/null && python /root/ameba-rtos/ameba.py soc RTL8730E && python /root/ameba-rtos/ameba.py build -p'
```

Expected result:
- the `dump meta` handler prints:
  - `[river][diag] kws dump meta returned`
- the successful `align run` handler prints:
  - `[river][diag] kws align run returned status=%d`
- the build completes successfully with:
  - `Build done`

Interpretation:
- this step is complete once the firmware image contains the new return markers
  and still builds cleanly on the latest SDK baseline
- board validation of those markers is intentionally deferred to the next
  runtime reproduction step

## Step 5.131
Reproduce the latest narrowed post-success INT8 alignment state:
```bash
cd /root/ameba-river
stty -F /dev/ttyUSB0 1500000 raw -echo
rm -f /tmp/kws_int8_prev_local_off.log
(timeout 38s cat /dev/ttyUSB0 > /tmp/kws_int8_prev_local_off.log) &
reader=$!
printf 'reboot\r' > /dev/ttyUSB0
sleep 0.40
printf 'river kws debug local on\r' > /dev/ttyUSB0
sleep 8.60
printf 'river audio probe stop\r' > /dev/ttyUSB0
sleep 0.80
printf 'river kws debug local off\r' > /dev/ttyUSB0
sleep 0.40
printf 'river kws align run\r' > /dev/ttyUSB0
sleep 6.50
printf 'river kws dump meta\r' > /dev/ttyUSB0
wait $reader
```

Expected current board result:
- early boot local-debug enable is accepted:
  - `kws debug local_only: enabled=yes ...`
- live false wake may still happen before replay, but it is held locally:
  - `wakeword hit: ... score_pm=289 q15=9472`
  - `wakeword handoff held: reason=local_debug ...`
- probe stop succeeds:
  - `vad probe stopped`
- local debug is turned back off before replay:
  - `kws debug local_only: enabled=no ...`
  - `kws align cleanup: ... local_only_restore=no`
- `river kws align run` now reaches the final success marker:
  - `kws align replay done: dump=preserved local_only_restored=no`

Expected remaining failure after that success line:
- runtime immediately prints:
  - `[INIC-E] WIFI TRX IPC 4 timeout`
- the later `river kws dump meta` is only echoed by UART and does not execute

Interpretation:
- if `kws align replay done: ...` appears only when `previous_local_debug_mode=no`, then the prior missing-final-line symptom is specifically tied to the cleanup path that restores `local_debug_mode=yes`
- if `river kws dump meta` still does not execute, the remaining issue is no longer the final disarm or the missing success log; it is a later shell / monitor usability problem after the align command returns

## Step 5.130
Build the latest-SDK image that skips the redundant final align disarm when the worker is already idle:
```bash
cd /root/ameba-river
bash -lc 'source /root/ameba-river/env.sh >/dev/null && python /root/ameba-rtos/ameba.py soc RTL8730E && python /root/ameba-rtos/ameba.py build -p'
```

Flash and reproduce the narrowed latest-SDK INT8 board result:
```bash
cd /root/ameba-river
stty -F /dev/ttyUSB0 1500000 raw -echo
bash -lc "printf '\033\r\n' > /dev/ttyUSB0"
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
```

After flash succeeds, run the controlled serial sequence that suppresses the live false-wake path before alignment:
```bash
stty -F /dev/ttyUSB0 1500000 raw -echo
rm -f /tmp/kws_int8_fast_stop_align.log
(timeout 22s cat /dev/ttyUSB0 > /tmp/kws_int8_fast_stop_align.log) &
reader=$!
printf 'reboot\r' > /dev/ttyUSB0
sleep 0.40
printf 'river kws debug local on\r' > /dev/ttyUSB0
sleep 0.20
printf 'river audio probe stop\r' > /dev/ttyUSB0
sleep 0.80
printf 'river kws align run\r' > /dev/ttyUSB0
wait $reader
```

Expected current result on board:
- the image boots as `variant=student_dscnn_tiny_v2_int8_debug`
- `river kws debug local on` is accepted very early and prints:
  - `kws debug local_only: enabled=yes ...`
- `river audio probe stop` succeeds and prints:
  - `vad probe stopped`
- `river kws align run` reaches the late cleanup path:
  - `kws align replay captured: seq=1 infer=2 score=0.296875 q15=9728`
  - `kws align cleanup: status=0 emit_dump=yes local_only_restore=yes`
  - `kws align cleanup: disarm skipped worker already idle snapshot=ready`
  - `kws align cleanup: worker idle wait status=0`
  - `kws align cleanup: disarm tensor dump begin`
  - `kws align cleanup: disarm tensor dump done`
  - `kws align cleanup: local debug restored=yes`

Expected remaining failure in the current image:
- no `kws align replay done: ...`
- no successful manual `river kws dump meta` response after the run
- board starts repeating:
  - `[INIC-A] Dev api ipc timeout: cur id 0x1, evt 0x0; latest id 0x1, evt 0x0`

Additional clean-boot observation for this INT8 variant:
- if `local debug` is not enabled very early, live audio can false-trigger before alignment begins:
  - `wakeword hit: ... score_pm=304 q15=9984`
- because the active threshold is `278`, do not rely on live probe traffic remaining quiet during on-board parity work

## Step 5.129
Build the latest-SDK image with auto-summary disabled in `river kws align run`:
```bash
cd /root/ameba-river
bash -lc 'source /root/ameba-river/env.sh >/dev/null && python /root/ameba-rtos/ameba.py soc RTL8730E && python /root/ameba-rtos/ameba.py build -p'
```

Expected build result:
- `Build done`
- updated image exists at `build_RTL8730E/km0_km4_ca32_app.bin`

Board-side follow-up after flashing this build:
```text
river kws debug local on
river audio probe stop
river kws align run
river kws dump meta
```

Expected runtime behavior after this change:
- `river kws align run` should still reach:
  - `kws align replay captured: ...`
- but it should no longer auto-print:
  - `kws tensor dump begin: ...`
  - `kws tensor dump meta: ...`
  - `kws tensor dump snapshot: ...`
- if the auto-summary was the blocker, the command should now continue into:
  - `kws align cleanup: ...`
  - `kws align replay done: dump=preserved ...`
- after `align run` returns, `river kws dump meta` should execute and print the preserved snapshot metadata on demand

Interpretation:
- success means the preserved-snapshot parity path survives, while the shell no longer wedges on automatic snapshot summary emission
- if the shell still blocks before cleanup, the remaining issue is deeper than the summary logging itself

## Step 5.128
Reproduce the latest narrowed INT8 post-snapshot blocker:
```bash
cd /root/ameba-river
usbipd.exe attach --wsl --busid 4-4
rm -f /dev/ttyUSB0
mknod /dev/ttyUSB0 c 188 0
chown root:dialout /dev/ttyUSB0
chmod 660 /dev/ttyUSB0
bash -lc "printf '\033\r\n' > /dev/ttyUSB0"
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
script -q -f /tmp/kws_int8_posttrigger_align.log -c "bash -lc 'stty -F /dev/ttyUSB0 1500000 raw -echo; timeout 30s cat /dev/ttyUSB0'"
```

During that `30s` capture window, send:
```bash
bash -lc "printf 'river kws debug local on\r' > /dev/ttyUSB0"
bash -lc "printf 'river audio probe stop\r' > /dev/ttyUSB0"
bash -lc "printf 'river kws align run\r' > /dev/ttyUSB0"
```

Expected observed result for the current narrowed blocker:
- `river kws align run` now reaches all of these points:
  - `kws trigger dispatch done: ...`
  - `kws trigger post-disarm: ...`
  - `kws align replay feed done: ...`
  - `kws align replay tail done: ...`
  - `kws align replay wait idle status=0`
  - `kws align replay captured: ...`
  - `kws tensor dump begin: ...`
  - `kws tensor dump meta: ...`
  - `kws tensor dump snapshot: ...`
- immediately after snapshot summary, runtime may print:
  - `[INIC-E] WIFI TRX IPC 4 timeout`

Expected missing markers in the current failure:
- no `kws align cleanup: ...`
- no `kws align replay done: ...`

Post-hang liveness probe:
```bash
script -q -f /tmp/kws_int8_postsnapshot_probe.log -c "bash -lc 'stty -F /dev/ttyUSB0 1500000 raw -echo; timeout 12s cat /dev/ttyUSB0'"
bash -lc "printf 'river kws dump meta\r' > /dev/ttyUSB0"
```

Expected result:
- UART only echoes `river kws dump meta`
- no actual dump response executes

Interpretation:
- this confirms the command path is blocked after snapshot-summary emission but before the outer cleanup log executes

## Step 5.127
Build the latest-SDK image with post-trigger replay diagnostics:
```bash
cd /root/ameba-river
bash -lc 'source /root/ameba-river/env.sh >/dev/null && python /root/ameba-rtos/ameba.py soc RTL8730E && python /root/ameba-rtos/ameba.py build -p'
```

Expected build result:
- `Build done`
- updated image exists at `build_RTL8730E/km0_km4_ca32_app.bin`

Board-side follow-up after flashing this build:
```text
river kws debug local on
river audio probe stop
river kws align run
```

Expected new diagnostic markers:
- trigger return path:
  - `kws trigger dispatch done: ...`
  - `kws trigger post-disarm: ...`
- first replay step after trigger:
  - `kws align replay trigger observed: frame=... infer=... snapshot=...`
  - `kws align replay first post-trigger queue wait begin: frame=...`
  - `kws align replay first post-trigger queue wait done: frame=... status=...`
- replay tail / snapshot wait path:
  - `kws align replay feed done: ...`
  - `kws align replay tail done: ...`
  - `kws align replay wait idle begin: ...`
  - `kws align replay wait idle status=...`

Interpretation:
- if `kws trigger post-disarm: ...` is missing, the worker is still stuck before `emit_trigger()` fully returns
- if `queue wait begin` appears without `queue wait done`, the next blocker is inside the first post-trigger queue-room wait path
- if replay feed/tail markers appear but `kws align replay captured` is still absent, the next blocker moves to the idle/snapshot-wait stage

## Step 5.126
Reproduce the narrowed latest-SDK DS-CNN tiny INT8 alignment hang:
```bash
cd /root/ameba-river
bash -lc "printf '\033\r\n' > /dev/ttyUSB0"
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
script -q -f /tmp/kws_int8_cleanup_probe_after_reboot.log -c "bash -lc 'stty -F /dev/ttyUSB0 1500000 raw -echo; timeout 8s cat /dev/ttyUSB0'"
bash -lc "printf 'reboot\r' > /dev/ttyUSB0"
script -q -f /tmp/kws_int8_cleanup_align.log -c "bash -lc 'stty -F /dev/ttyUSB0 1500000 raw -echo; timeout 25s cat /dev/ttyUSB0'"
```

During that `25s` capture window, send:
```bash
bash -lc "printf 'river kws debug local on\r' > /dev/ttyUSB0"
bash -lc "printf 'river audio probe stop\r' > /dev/ttyUSB0"
bash -lc "printf 'river kws align run\r' > /dev/ttyUSB0"
```

Expected observed result for the current narrowed blocker:
- flash succeeds with `Finished PASS`
- the application runtime may need an explicit `reboot` before logs resume normally
- `river kws align run` shows:
  - the first alignment disarm completes
  - replay starts
  - INT8 replay inference reaches at least:
    - `infer=1 ... score=0.242188 ...`
    - `infer=2 ... score=0.296875 ...`
  - trigger-time snapshot capture occurs:
    - `kws tensor dump captured: seq=1 infer=2 ...`
  - trigger-side local debug suppression occurs:
    - `wakeword handoff held: reason=local_debug ...`
  - the second trigger-side disarm also completes its currently instrumented markers

Expected missing markers in the current failure:
- no `kws align replay captured: ...`
- no `kws align cleanup: ...`
- no `kws align replay done: ...`

Optional liveness probe after the hang:
```bash
script -q -f /tmp/kws_int8_cleanup_posthang.log -c "bash -lc 'stty -F /dev/ttyUSB0 1500000 raw -echo; timeout 10s cat /dev/ttyUSB0'"
bash -lc "printf 'river kws dump meta\r' > /dev/ttyUSB0"
```

Current interpretation:
- the hang occurs before outer alignment cleanup begins
- the command path itself remains blocked after the trigger-side disarm
- the next diagnostic step should instrument the post-trigger replay loop and the no-log calls immediately after the second `disarm`

## Step 5.125
Build the latest-SDK image with cleanup-stage diagnostics:
```bash
cd /root/ameba-river
bash -lc 'source /root/ameba-river/env.sh >/dev/null && python /root/ameba-rtos/ameba.py soc RTL8730E && python /root/ameba-rtos/ameba.py build -p'
```

Expected build result:
- `Build done`
- updated image exists at `build_RTL8730E/km0_km4_ca32_app.bin`

Board-side follow-up after flashing this build:
```text
river kws debug local on
river audio probe stop
river kws align run
```

Expected new diagnostic markers during the remaining INT8 alignment blocker:
- cleanup entry:
  - `kws align cleanup: status=... emit_dump=yes ...`
- disarm progress:
  - `kws disarm: begin ...`
  - `kws disarm: frontend reset done`
  - `kws disarm: input ring reset done`
  - `kws disarm: pre-roll ring reset done`
  - `kws disarm: input signal drained`
- post-disarm progress:
  - `kws align cleanup: disarm done`
  - `kws align cleanup: worker idle wait status=...`
  - `kws align cleanup: disarm tensor dump begin`
  - `kws align cleanup: disarm tensor dump done`
  - `kws align cleanup: local debug restored=...`

Interpretation:
- whichever last marker appears before the stall identifies the cleanup stage that is still blocking `river kws align run`
- if `kws worker idle timeout: ...` appears, use the printed `reset_pending`, `worker_processing`, and queue depth to decide whether the stuck state is in worker drain vs. ring/semaphore cleanup

## Step 5.124
Reproduce the post-power-cycle latest-SDK DS-CNN tiny INT8 alignment state:
```bash
usbipd.exe attach --wsl --busid 4-4
bash -lc 'ls -l /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || true'
script -q -f /tmp/kws_int8_retry_powercycle_probe.log -c "bash -lc 'stty -F /dev/ttyUSB0 1500000 raw -echo; timeout 5s cat /dev/ttyUSB0'"
bash -lc "printf 'river kws debug local on\r' > /dev/ttyUSB0"
bash -lc "printf 'river audio probe stop\r' > /dev/ttyUSB0"
bash -lc "printf 'river kws align status\r' > /dev/ttyUSB0"
bash -lc "printf 'river kws align run\r' > /dev/ttyUSB0"
```

Expected observed result after this specific power-cycle retry:
- the board is back in readable runtime text mode, not the earlier pure `0x00` state
- runtime logs show the DS-CNN tiny INT8 characteristics, including:
  - `out_type=int8`
  - `thresh_pm=278`
  - `arena=295764/2048KB`
  - `infer_us` around `491-493 ms`
- `river kws align run` now reaches the preserved-summary parity stage:
  - `kws align replay captured: ...`
  - `kws tensor dump begin: ...`
  - `kws tensor dump meta: ...`
  - `kws tensor dump snapshot: seq=... chunks=[feat:253 input:64 output:1]`

Current remaining blocker after that point:
- `kws align replay done: dump=preserved ...` still does not appear
- repeated `IPC Get Semaphore Timeout` starts after the snapshot line
- a subsequent `river kws dump meta` may be echoed by UART but does not execute

Interpretation:
- this retry proves the board can now enter runtime and the summary-based dump path works
- but the alignment flow still stalls after snapshot capture, so manual chunk pulling and final board/host parity are still blocked for this INT8 image

## Step 5.123
Reproduce the current latest-SDK DS-CNN tiny INT8 board blocker:
```bash
cd /root/ameba-river
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'
rm -f /tmp/kws_dscnn_tiny_int8_manual_dump.log
script -q -f /tmp/kws_dscnn_tiny_int8_manual_dump.log -c "bash -lc 'stty -F /dev/ttyUSB0 1500000 raw -echo; timeout 5s cat /dev/ttyUSB0'"
od -An -tx1 -j 160 -N 64 /tmp/kws_dscnn_tiny_int8_manual_dump.log
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000 --debug
```

Expected observed result for the current blocker:
- flash still succeeds with `Finished PASS`
- passive UART capture shows continuous `0x00` bytes instead of boot text
- official monitor connects, sends `AT+LIST`, and receives only repeated `00 / 00 00 / 00 00 00 / 00 00 00 00`
- no normal application markers appear:
  - no `File System Init Success`
  - no `ameba-river boot`
  - no `kws init`

Interpretation:
- do not continue to `river kws align run` or board/host replay while the board is in this state
- the current failure is before parity or KWS inference; it is a board-runtime bring-up blocker for this latest-SDK INT8 image

## Step 5.122
Build the latest-SDK image with the preserved snapshot summary flow:
```bash
cd /root/ameba-river
rm -rf build_RTL8730E/build
bash -lc 'source /root/ameba-river/env.sh >/dev/null && python /root/ameba-rtos/ameba.py soc RTL8730E && python /root/ameba-rtos/ameba.py build -p'
```

Expected build result:
- `Build done`
- updated image exists at `build_RTL8730E/km0_km4_ca32_app.bin`

Board-side validation target after flashing this build:
```text
river kws debug local on
river audio probe stop
river kws align run
river kws dump meta
river kws dump chunk output_raw 1
river kws dump chunk input_raw 1
river kws dump chunk feat_f32 1
```

Expected runtime behavior:
- `river kws align run` should stop after:
  - `kws align replay captured: ...`
  - `kws tensor dump begin: ...`
  - `kws tensor dump meta: ...`
  - `kws tensor dump snapshot: seq=... infer=... chunks=[feat:... input:... output:...]`
  - `kws align replay done: dump=preserved ...`
- it should no longer auto-print all tensor chunks in one burst
- the preserved snapshot should remain queryable through `river kws dump meta` and `river kws dump chunk ...`

## Step 5.121
Build the latest-SDK image with the alignment replay fix:
```bash
cd /root/ameba-river
rm -rf build_RTL8730E/build
bash -lc 'source /root/ameba-river/env.sh >/dev/null && python /root/ameba-rtos/ameba.py soc RTL8730E && python /root/ameba-rtos/ameba.py build -p'
```

Expected build result:
- `Build done`
- updated image exists at `build_RTL8730E/km0_km4_ca32_app.bin`

Board-side validation target after flashing this build:
```text
river kws debug local on
river audio probe stop
river kws align run
```

Expected runtime behavior:
- the alignment replay should no longer stall permanently after `kws tensor dump captured: ...`
- it should continue into:
  - `kws align replay captured: ...`
  - `kws tensor dump begin: ...`
  - `kws tensor dump meta: ...`
  - `kws tensor dump feat_f32: ...`
  - `kws tensor dump input_raw: ...`
  - `kws tensor dump output_raw: ...`
  - `kws align replay done: dump=emitted ...`

## Step 5.120
Build with latest SDK:
```bash
cd /root/ameba-river
rm -rf build_RTL8730E/build
bash -lc 'source /root/ameba-river/env.sh >/dev/null && python /root/ameba-rtos/ameba.py soc RTL8730E && python /root/ameba-rtos/ameba.py build -p'
```

Config confirmation:
```bash
cd /root/ameba-river
rg -n "CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_DSCNN_TINY_V2_INT8_DEBUG|CONFIG_RIVER_KWS_TENSOR_ARENA_KB|CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15" \
  build_RTL8730E/build/.config \
  build_RTL8730E/build/project_ap/.config_ca32 \
  prj.conf -S
```

Expected lines:
- `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_DSCNN_TINY_V2_INT8_DEBUG=y`
- `CONFIG_RIVER_KWS_TENSOR_ARENA_KB=2048`
- `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=9125`

Build artifacts:
```bash
cd /root/ameba-river
ls -lh \
  build_RTL8730E/km0_km4_ca32_app.bin \
  build_RTL8730E/build/project_ap/image/ap_image_all.bin \
  build_RTL8730E/build/project_hp/image/km4_image2_all.bin \
  build_RTL8730E/build/project_lp/image/km0_image2_all.bin
```

Expected result:
- full latest-SDK build completes with `Build done`
- the combined image `build_RTL8730E/km0_km4_ca32_app.bin` exists
- no parity/debug commands were removed; this step only prepares the new INT8 variant for board validation

## Step 5.119
Policy verification:
- Check `AGENTS.md` and confirm the repository now records `/root/ameba-rtos` as the default SDK baseline.
- For subsequent tasks, use the latest SDK tree unless the user explicitly redirects to another SDK checkout.

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

## Step 5.109 Verification

Build and flash the DS-CNN tiny FP32 debug variant:
```bash
cd /root/ameba-river
source env.sh >/dev/null
python3 /root/ameba-rtos-1.2/ameba.py build -p
python3 tools/river_flash.py -p /dev/ttyUSB0
```

Expected boot confirmation in the serial log:
```bash
python3 - <<'PY'
from pathlib import Path
text = Path('/tmp/kws_dscnn_tiny_fp32_debug.log').read_bytes().replace(b'\x00', b'').decode('utf-8', errors='replace')
for needle in (
    'variant=student_dscnn_tiny_v2_fp32_debug',
    'kws init plan:',
    'kws alloc:',
    'kws backend:',
    'kws input shape: src=schema dims=[1,40,101,1]',
):
    print(needle, '=>', needle in text)
PY
```

Expected result:
- all checks print `True`
- there is no `kws AllocateTensors failed`

Confirm the board-embedded header bytes exactly match the algorithm FP32 bundle:
```bash
cd /root/ameba-river
python3 - <<'PY'
from pathlib import Path
import hashlib, re

header = Path('components/river_voice/generated/student_dscnn_tiny_v2_fp32_model_data.h').read_text()
data = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', header))
model = Path('/root/kws-trainint/artifacts/exports/student_dscnn_tiny_v2/model.fp32.tflite').read_bytes()

print('header_bytes', len(data))
print('header_sha256', hashlib.sha256(data).hexdigest())
print('model_bytes', len(model))
print('model_sha256', hashlib.sha256(model).hexdigest())
print('exact_match', 'yes' if data == model else 'no')
PY
```

Expected result:
- both byte counts are `12376`
- both SHA256 values are
  `836b18e8c315c9142f5744bfa0183a35e4d011019c2526e33f86dd3da166c7c7`
- `exact_match yes`

Run the preserved board parity flow:
```text
river kws debug local on
river audio probe stop
river kws align status
river kws align run
river audio probe start
river kws debug local off
```

Expected board-side evidence in the serial log:
```bash
python3 - <<'PY'
from pathlib import Path
text = Path('/tmp/kws_dscnn_tiny_fp32_debug.log').read_bytes().replace(b'\x00', b'').decode('utf-8', errors='replace')
for needle in (
    'kws align guard: kws=ready probe=stopped interaction=wake_monitoring detection=ready worker=idle snapshot=empty local_only=yes',
    'kws tensor dump begin:',
    'kws tensor dump meta:',
    'kws tensor dump output_raw: seq=1 chunk=1/1 hex=a5eb973e',
    'kws align replay captured: seq=1 infer=2 score=0.296720 q15=9723',
    'wakeword hit: text=小欧管家 score_pm=296 q15=9723',
):
    print(needle, '=>', needle in text)
PY
```

Expected result:
- all checks print `True`

Replay the captured dump on host:
```bash
cd /root/ameba-river
TF_ENABLE_ONEDNN_OPTS=0 \
OMP_NUM_THREADS=1 \
TF_NUM_INTRAOP_THREADS=1 \
TF_NUM_INTEROP_THREADS=1 \
python3 tools/kws/replay_board_tensor_dump.py \
  --log /tmp/kws_dscnn_tiny_fp32_debug.log \
  --model /root/kws-trainint/artifacts/exports/student_dscnn_tiny_v2/model.fp32.tflite \
  --seq latest \
  --builtin-ref
```

Expected interpretation:
- `feature hash` and `effective input hash` match the board values
- the tool reports `raw_equal=yes`
- board and host both decode to `raw=297`, `q15=9723`, `exact=0.296720`
- `bytes_equal=no` is still acceptable for this FP32 candidate because the
  remaining mismatch is only at the float output byte level while the decoded
  scalar result is the same

Confirm the new runtime profile doc is present and indexed:
```bash
cd /root/ameba-river
sed -n '1,260p' doc/RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_DSCNN_TINY_FP32_DEBUG_ZH.md
rg -n "RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_DSCNN_TINY_FP32_DEBUG_ZH.md" doc/README.md
```

## Step 5.110 Verification

Build and flash the DS-CNN small FP32 debug variant:
```bash
cd /root/ameba-river
source env.sh >/dev/null
python3 /root/ameba-rtos-1.2/ameba.py build -p
python3 tools/river_flash.py -p /dev/ttyUSB0
```

Expected boot confirmation in the serial log:
```bash
python3 - <<'PY'
from pathlib import Path
text = Path('/tmp/kws_dscnn_small_fp32_debug.log').read_bytes().replace(b'\x00', b'').decode('utf-8', errors='replace')
for needle in (
    'variant=student_dscnn_small_v2_fp32_debug',
    'kws init plan:',
    'kws alloc:',
    'kws backend:',
    'kws input shape: src=schema dims=[1,40,101,1]',
):
    print(needle, '=>', needle in text)
PY
```

Expected result:
- all checks print `True`
- there is no `AllocateTensors failed`

Confirm the board-embedded header bytes exactly match the algorithm FP32 bundle:
```bash
cd /root/ameba-river
python3 - <<'PY'
from pathlib import Path
import hashlib, re

header = Path('components/river_voice/generated/student_dscnn_small_v2_fp32_model_data.h').read_text()
data = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', header))
model = Path('/root/kws-trainint/artifacts/exports/student_dscnn_small_v2/model.fp32.tflite').read_bytes()

print('header_bytes', len(data))
print('header_sha256', hashlib.sha256(data).hexdigest())
print('model_bytes', len(model))
print('model_sha256', hashlib.sha256(model).hexdigest())
print('exact_match', 'yes' if data == model else 'no')
PY
```

Expected result:
- both byte counts are `23600`
- both SHA256 values are
  `e0a2bedd32801d3d05ca4b0b3369137bedba990d28e02b5253696dc021e56925`
- `exact_match yes`

Run the preserved board parity flow:
```text
river kws debug local on
river audio probe stop
river kws align status
river kws align run
river audio probe start
river kws debug local off
```

Expected board-side evidence in the serial log:
```bash
python3 - <<'PY'
from pathlib import Path
text = Path('/tmp/kws_dscnn_small_fp32_align_full.log').read_bytes().replace(b'\x00', b'').decode('utf-8', errors='replace')
for needle in (
    'kws align guard: kws=ready probe=stopped interaction=wake_monitoring detection=ready worker=idle snapshot=empty local_only=yes',
    'kws tensor dump begin:',
    'kws tensor dump meta:',
    'kws tensor dump input_raw: seq=2 chunk=253/253',
    'kws tensor dump output_raw: seq=2 chunk=1/1 hex=8287aa3e',
    'kws align replay captured: seq=2 infer=4 score=0.333065 q15=10914',
    'wakeword hit: text=小欧管家 score_pm=333 q15=10914',
):
    print(needle, '=>', needle in text)
PY
```

Expected result:
- all checks print `True`
- if `kws tensor dump end:` is absent but `input_raw 253/253` and `output_raw`
  are present, treat the dump as complete enough for replay

Replay the captured dump on host:
```bash
cd /root/ameba-river
TF_ENABLE_ONEDNN_OPTS=0 \
OMP_NUM_THREADS=1 \
TF_NUM_INTRAOP_THREADS=1 \
TF_NUM_INTEROP_THREADS=1 \
python3 tools/kws/replay_board_tensor_dump.py \
  --log /tmp/kws_dscnn_small_fp32_align_full.log \
  --model /root/kws-trainint/artifacts/exports/student_dscnn_small_v2/model.fp32.tflite \
  --seq latest \
  --builtin-ref
```

Expected interpretation:
- `feature hash`, `logged_input`, and `effective input` hashes match the board values
- the tool reports `quant_parity: diff_bytes=0/16160`
- board and host both decode to `raw=333`, `exact=0.333065`, `q15=10914`
- `bytes_equal=no` is still acceptable for this FP32 candidate because the
  decoded scalar output still matches exactly

Confirm the new runtime profile doc is present and indexed:
```bash
cd /root/ameba-river
sed -n '1,260p' doc/RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_DSCNN_SMALL_FP32_DEBUG_ZH.md
rg -n "RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_DSCNN_SMALL_FP32_DEBUG_ZH.md" doc/README.md
```

Reproduce the latest ADK quantization re-check:
```bash
cd /root/ameba-river
git -C /root/ameba-rtos-1.2 rev-parse HEAD
git -C /tmp/ameba-rtos-1.2-latest rev-parse HEAD
git -C /root/ameba-rtos-1.2 ls-tree HEAD component/tflite_micro
git -C /tmp/ameba-rtos-1.2-latest ls-tree HEAD component/tflite_micro
git -C /root/ameba-rtos-1.2 ls-tree origin/master component/tflite_micro
git -C /root/ameba-rtos-1.2/component/tflite_micro for-each-ref --format='%(refname:short) %(objectname)' refs/remotes/origin
git -C /root/ameba-rtos-1.2/component/tflite_micro diff \
  dbda29aa7240ad14cf21cf3636ff2792a05ddcc1..origin/main -- \
  tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/conv.cc \
  tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/depthwise_conv.cc \
  tensorflow/lite/micro/kernels/reduce_common.cc \
  tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/im2col_utils.h
```

Expected interpretation:
- `/root/ameba-rtos-1.2` resolves to `8624cbeccf840c929db1624e05cc5b681024a3bf`
- `/tmp/ameba-rtos-1.2-latest` resolves to `8ef72a545c384ec439eef9a200baf4f569e21a73`
- both `release/v1.2` trees point `component/tflite_micro` to
  `dbda29aa7240ad14cf21cf3636ff2792a05ddcc1`
- `origin/master` points `component/tflite_micro` to
  `8b38d3dac9ea733e93ad73c2b637ef1a28753fb3`
- the `git diff dbda29a..origin/main -- <4 files>` command prints nothing,
  proving the currently visible official master ref still does not absorb the
  local quantization patches used by the dirty SDK

Confirm the current dirty SDK really contains the local correctness patches:
```bash
nl -ba /root/ameba-rtos-1.2/component/tflite_micro/tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/conv.cc | sed -n '245,267p'
nl -ba /root/ameba-rtos-1.2/component/tflite_micro/tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/depthwise_conv.cc | sed -n '141,159p'
nl -ba /root/ameba-rtos-1.2/component/tflite_micro/tensorflow/lite/micro/kernels/reduce_common.cc | sed -n '142,224p'
nl -ba /root/ameba-rtos-1.2/component/tflite_micro/tensorflow/lite/micro/kernels/reduce_common.cc | sed -n '306,309p'
nl -ba /root/ameba-rtos-1.2/component/tflite_micro/tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/im2col_utils.h | sed -n '135,188p'
```

Expected interpretation:
- `conv.cc` contains the comment that the CA32 INT8 optimized conv path is "not
  reliable" and directly calls `reference_integer_ops::ConvPerChannel(...)`
- `depthwise_conv.cc` contains the same style of comment and calls
  `reference_integer_ops::DepthwiseConvPerChannel(...)`
- `reduce_common.cc` contains `IsChannelGapMeanInt8(...)` and
  `EvalChannelGapMeanInt8(...)`, and `EvalMeanHelper(...)` dispatches to that
  path for INT8 GAP / MEAN
- `im2col_utils.h` uses `single_buffer_length = kheight * kwidth * input_depth`
  instead of the old `output_depth`-based length

Confirm project-side `INT16` is still not a deployable KWS target:
```bash
cd /root/ameba-river
nl -ba components/river_voice/river_voice_kws.cc | sed -n '770,863p'
nl -ba components/river_voice/river_voice_kws.cc | sed -n '1158,1184p'
nl -ba components/river_voice/river_voice_kws.cc | sed -n '3683,3688p'
nl -ba tools/kws/export_bc_resnet_tflite.py | sed -n '36,40p'
nl -ba tools/kws/replay_board_tensor_dump.py | sed -n '93,100p'
```

Expected interpretation:
- `river_voice_kws.cc` only maps / accepts `float32`, `uint8`, and `int8`
- `INT16` is absent from schema type mapping, effective type selection, storage
  byte sizing, and init-time allowed tensor types
- `tools/kws/export_bc_resnet_tflite.py` only offers `float32` and `int8`
- `tools/kws/replay_board_tensor_dump.py` only accepts `int8`, `uint8`, and
  `float32`

Confirm the new document is present and indexed:
```bash
cd /root/ameba-river
sed -n '1,260p' doc/KWS_LATEST_ADK_QUANTIZATION_RECHECK_ZH.md
rg -n "KWS_LATEST_ADK_QUANTIZATION_RECHECK_ZH.md" doc/README.md
```

Reproduce the clean-SDK INT8 retry build:
```bash
cd /root/ameba-river
bash -lc 'mkdir -p /tmp/ccache-tmp && export CCACHE_TEMPDIR=/tmp/ccache-tmp && export AMEBA_SDK_ROOT=/tmp/ameba-rtos-1.2-latest; source env.sh; python3 /tmp/ameba-rtos-1.2-latest/ameba.py build -p'
```

Expected interpretation:
- build completes with `Build done`
- the build uses `/tmp/ameba-rtos-1.2-latest`, not the default
  `/root/ameba-rtos-1.2`

Confirm the project is configured for the clean-SDK INT8 retry:
```bash
cd /root/ameba-river
rg -n "CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_TINY_V2_INT8_DEBUG|CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15|CONFIG_RIVER_KWS_TENSOR_ARENA_KB" prj.conf
```

Expected interpretation:
- `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_TINY_V2_INT8_DEBUG=y`
- `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=9444`
- `CONFIG_RIVER_KWS_TENSOR_ARENA_KB=2048`

Flash the clean-SDK INT8 image:
```bash
cd /root/ameba-river
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
env AMEBA_SDK_ROOT=/tmp/ameba-rtos-1.2-latest python3 tools/river_flash.py -p /dev/ttyUSB0
```

Expected interpretation:
- the flash tool reports `Finished PASS`

Probe the post-flash serial state with the clean SDK monitor:
```bash
cd /root/ameba-river
python3 /tmp/ameba-rtos-1.2-latest/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000 --debug
```

Expected interpretation:
- monitor connects successfully and sends `AT+LIST`
- no valid monitor command list is returned
- instead of `ameba-river` boot logs, the serial output degrades into repeated
  `00 / 00 00 / 00 00 00 / 00 00 00 00`
- this reproduces the clean-SDK INT8 failure signature recorded in
  [doc/KWS_LATEST_ADK_QUANTIZATION_RECHECK_ZH.md](/root/ameba-river/doc/KWS_LATEST_ADK_QUANTIZATION_RECHECK_ZH.md)

Reproduce the clean-SDK FP32 control experiment:
```bash
cd /root/ameba-river
rg -n "CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_TINY_V2_FP32_DEBUG|CONFIG_RIVER_KWS_TENSOR_ARENA_KB|CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15" prj.conf
bash -lc 'mkdir -p /tmp/ccache-tmp && export CCACHE_TEMPDIR=/tmp/ccache-tmp && export AMEBA_SDK_ROOT=/tmp/ameba-rtos-1.2-latest; source env.sh; python3 /tmp/ameba-rtos-1.2-latest/ameba.py build -p'
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
env AMEBA_SDK_ROOT=/tmp/ameba-rtos-1.2-latest python3 tools/river_flash.py -p /dev/ttyUSB0
script -q -f /tmp/kws_student_fp32_clean_sdk.log -c "bash -lc 'while true; do stty -F /dev/ttyUSB0 1500000 raw -echo && cat /dev/ttyUSB0; sleep 0.2; done'"
```

Expected interpretation:
- `prj.conf` contains:
  - `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_TINY_V2_FP32_DEBUG=y`
  - `CONFIG_RIVER_KWS_TENSOR_ARENA_KB=8192`
  - `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=384`
- build completes with `Build done`
- flash completes with `Finished PASS`
- unlike the dirty-SDK FP32 baseline, the clean-SDK FP32 control image still
  does not emit normal `ameba-river` boot text
- the serial stream instead collapses into continuous `0x00` bytes, matching
  the broader clean-SDK runtime failure recorded in
  [doc/KWS_LATEST_ADK_QUANTIZATION_RECHECK_ZH.md](/root/ameba-river/doc/KWS_LATEST_ADK_QUANTIZATION_RECHECK_ZH.md)

Recheck whether the missing 17MB layout patch is the clean-SDK blocker:
```bash
cd /root/ameba-river
python3 tools/sdk/apply_rtl8730e_memory_layout_patch.py --sdk-root /root/ameba-rtos-1.2 --variant aivoice_ca32_17mb --check
python3 tools/sdk/apply_rtl8730e_memory_layout_patch.py --sdk-root /tmp/ameba-rtos-1.2-latest --variant aivoice_ca32_17mb --check
python3 tools/sdk/apply_rtl8730e_memory_layout_patch.py --sdk-root /tmp/ameba-rtos-1.2-latest --variant aivoice_ca32_17mb
bash -lc 'mkdir -p /tmp/ccache-tmp && export CCACHE_TEMPDIR=/tmp/ccache-tmp && export AMEBA_SDK_ROOT=/tmp/ameba-rtos-1.2-latest; source env.sh; python3 /tmp/ameba-rtos-1.2-latest/ameba.py build -p'
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
env AMEBA_SDK_ROOT=/tmp/ameba-rtos-1.2-latest python3 tools/river_flash.py -p /dev/ttyUSB0
script -q -f /tmp/kws_student_fp32_clean_sdk_patched.log -c "bash -lc 'stty -F /dev/ttyUSB0 1500000 raw -echo; timeout 20s cat /dev/ttyUSB0'"
bash -lc "od -An -tx1 -j 160 -N 64 /tmp/kws_student_fp32_clean_sdk_patched.log"
```

Expected interpretation:
- dirty SDK check prints `applied`
- clean SDK pre-patch check prints `not-applied`
- patching the clean SDK prints:
  - `sdk_root=/tmp/ameba-rtos-1.2-latest variant=aivoice_ca32_17mb layout=changed hal=changed`
- the rebuild still ends with `Build done`
- the reflash still ends with `Finished PASS`
- the post-flash serial capture still does not show `ameba-river boot`
- `od` shows continuous `00` bytes after the `script` header, proving the
  missing memory layout patch is not the only clean-SDK runtime blocker

Recheck whether the dirty SDK local TFLM patch set alone can recover the clean SDK:
```bash
cd /root/ameba-river
bash -lc "git -C /root/ameba-rtos-1.2/component/tflite_micro diff -- tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/conv.cc tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/depthwise_conv.cc tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/im2col_utils.h tensorflow/lite/micro/kernels/reduce_common.cc > /tmp/dirty_tflm_local.patch && git -C /tmp/ameba-rtos-1.2-latest/component/tflite_micro apply /tmp/dirty_tflm_local.patch"
bash -lc "git -C /tmp/ameba-rtos-1.2-latest/component/tflite_micro diff --stat"
bash -lc 'mkdir -p /tmp/ccache-tmp && export CCACHE_TEMPDIR=/tmp/ccache-tmp && export AMEBA_SDK_ROOT=/tmp/ameba-rtos-1.2-latest; source env.sh; python3 /tmp/ameba-rtos-1.2-latest/ameba.py build -p'
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
env AMEBA_SDK_ROOT=/tmp/ameba-rtos-1.2-latest python3 tools/river_flash.py -p /dev/ttyUSB0
script -q -f /tmp/kws_student_fp32_clean_sdk_tflm_patch.log -c "bash -lc 'stty -F /dev/ttyUSB0 1500000 raw -echo; timeout 20s cat /dev/ttyUSB0'"
bash -lc "od -An -tx1 -j 160 -N 64 /tmp/kws_student_fp32_clean_sdk_tflm_patch.log"
```

Expected interpretation:
- `git diff --stat` in the clean clone shows the same 4-file dirty TFLM patch
  set:
  - `conv.cc`
  - `depthwise_conv.cc`
  - `im2col_utils.h`
  - `reduce_common.cc`
- the rebuild still ends with `Build done`
- the reflash still ends with `Finished PASS`
- the post-flash serial capture still does not show `ameba-river boot`
- `od` still shows continuous `00` bytes after the `script` header, proving the
  dirty SDK local TFLM patch set alone is not sufficient to restore a normal
  clean-SDK runtime

Recheck whether aligning clean-SDK `component/aivoice` to the dirty SDK commit helps:
```bash
cd /root/ameba-river
bash -lc "git -C /tmp/ameba-rtos-1.2-latest/component/audio rev-parse --short HEAD && git -C /tmp/ameba-rtos-1.2-latest/component/application/speechmind rev-parse --short HEAD && git -C /tmp/ameba-rtos-1.2-latest/component/ui rev-parse --short HEAD && git -C /tmp/ameba-rtos-1.2-latest/component/aivoice rev-parse --short HEAD"
bash -lc "git -C /tmp/ameba-rtos-1.2-latest/component/tflite_micro apply -R /tmp/dirty_tflm_local.patch && git -C /tmp/ameba-rtos-1.2-latest/component/aivoice checkout 280941488cb122f608d271d0c52a274e3c33a8ec"
bash -lc 'mkdir -p /tmp/ccache-tmp && export CCACHE_TEMPDIR=/tmp/ccache-tmp && export AMEBA_SDK_ROOT=/tmp/ameba-rtos-1.2-latest; source env.sh; python3 /tmp/ameba-rtos-1.2-latest/ameba.py build -p'
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
env AMEBA_SDK_ROOT=/tmp/ameba-rtos-1.2-latest python3 tools/river_flash.py -p /dev/ttyUSB0
script -q -f /tmp/kws_student_fp32_clean_sdk_aivoice_commit.log -c "bash -lc 'stty -F /dev/ttyUSB0 1500000 raw -echo; timeout 20s cat /dev/ttyUSB0'"
bash -lc "od -An -tx1 -j 160 -N 64 /tmp/kws_student_fp32_clean_sdk_aivoice_commit.log"
```

Expected interpretation:
- the clean clone actual submodule heads show:
  - `audio=e6de3cc`
  - `speechmind=b70cfe9`
  - `ui=f5a5325`
  - `aivoice=739ba4e` before the checkout
- after the checkout, clean-clone `aivoice` is at `2809414`
- the rebuild still ends with `Build done`
- the reflash still ends with `Finished PASS`
- the post-flash serial capture still does not show `ameba-river boot`
- `od` still shows continuous `00` bytes after the `script` header, proving the
  dirty `component/aivoice` commit alone is also not sufficient to restore a
  normal clean-SDK runtime

Compare clean-vs-dirty image artifacts directly for the same FP32 control config:
```bash
cd /root/ameba-river
bash -lc 'mkdir -p /tmp/clean_sdk_aivoice_align_snapshot && cp -a build_RTL8730E/build/project_hp/image /tmp/clean_sdk_aivoice_align_snapshot/project_hp_image && cp -a build_RTL8730E/build/project_lp/image /tmp/clean_sdk_aivoice_align_snapshot/project_lp_image && cp -a build_RTL8730E/build/project_ap/image /tmp/clean_sdk_aivoice_align_snapshot/project_ap_image'
bash -lc 'mkdir -p /tmp/ccache-tmp && export CCACHE_TEMPDIR=/tmp/ccache-tmp && unset AMEBA_SDK_ROOT; source env.sh; python3 /root/ameba-rtos-1.2/ameba.py build -p'
bash -lc 'mkdir -p /tmp/dirty_sdk_fp32_control_snapshot && cp -a build_RTL8730E/build/project_hp/image /tmp/dirty_sdk_fp32_control_snapshot/project_hp_image && cp -a build_RTL8730E/build/project_lp/image /tmp/dirty_sdk_fp32_control_snapshot/project_lp_image && cp -a build_RTL8730E/build/project_ap/image /tmp/dirty_sdk_fp32_control_snapshot/project_ap_image'
bash -lc 'for f in km4_boot_all.bin km4_image2_all.bin km0_km4_ca32_app.bin; do printf "## %s\n" "$f"; stat -c "clean %n %s" "/tmp/clean_sdk_aivoice_align_snapshot/project_hp_image/$f" 2>/dev/null || true; stat -c "dirty %n %s" "/tmp/dirty_sdk_fp32_control_snapshot/project_hp_image/$f" 2>/dev/null || true; done; for f in km0_image2_all.bin; do printf "## %s\n" "$f"; stat -c "clean %n %s" "/tmp/clean_sdk_aivoice_align_snapshot/project_lp_image/$f"; stat -c "dirty %n %s" "/tmp/dirty_sdk_fp32_control_snapshot/project_lp_image/$f"; done; for f in ap_image_all.bin; do printf "## %s\n" "$f"; stat -c "clean %n %s" "/tmp/clean_sdk_aivoice_align_snapshot/project_ap_image/$f"; stat -c "dirty %n %s" "/tmp/dirty_sdk_fp32_control_snapshot/project_ap_image/$f"; done'
bash -lc 'for pair in "/tmp/clean_sdk_aivoice_align_snapshot/project_hp_image/km4_boot_all.bin /tmp/dirty_sdk_fp32_control_snapshot/project_hp_image/km4_boot_all.bin" "/tmp/clean_sdk_aivoice_align_snapshot/project_hp_image/km4_image2_all.bin /tmp/dirty_sdk_fp32_control_snapshot/project_hp_image/km4_image2_all.bin" "/tmp/clean_sdk_aivoice_align_snapshot/project_lp_image/km0_image2_all.bin /tmp/dirty_sdk_fp32_control_snapshot/project_lp_image/km0_image2_all.bin" "/tmp/clean_sdk_aivoice_align_snapshot/project_ap_image/ap_image_all.bin /tmp/dirty_sdk_fp32_control_snapshot/project_ap_image/ap_image_all.bin"; do set -- $pair; printf "## %s\n" "$(basename "$1")"; if cmp -s "$1" "$2"; then echo identical; else echo different; fi; done'
bash -lc 'cmp -l /tmp/clean_sdk_aivoice_align_snapshot/project_hp_image/km4_boot_all.bin /tmp/dirty_sdk_fp32_control_snapshot/project_hp_image/km4_boot_all.bin | sed -n "1,20p"'
bash -lc 'cmp -l /tmp/clean_sdk_aivoice_align_snapshot/project_lp_image/km0_image2_all.bin /tmp/dirty_sdk_fp32_control_snapshot/project_lp_image/km0_image2_all.bin | sed -n "1,20p"'
```

Expected interpretation:
- dirty-SDK build completes with `Build done`
- the dirty image snapshot is created successfully
- `cmp -s` reports all key images as `different`
- AP/KM4 app-side image sizes diverge materially:
  - `ap_image_all.bin`: clean `3558496`, dirty `3538016`
  - `km4_image2_all.bin`: clean `380064`, dirty `379136`
  - `km0_km4_ca32_app.bin`: clean `4040960`, dirty `4019552`
- even same-size early-chain images still differ at the byte level:
  - `km4_boot_all.bin` first observed difference at byte `10119`
  - `km0_image2_all.bin` first observed difference at byte `41`
- therefore the clean-vs-dirty split is a whole-image / boot-chain divergence,
  not merely an app-layer KWS patch difference

## Step 5.111 Verification

Build the preserved FP32 control firmware against the latest upstream SDK tree:
```bash
cd /root/ameba-river
rm -rf build_RTL8730E/build
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python /root/ameba-rtos/ameba.py soc RTL8730E; python /root/ameba-rtos/ameba.py build -p'
```

Expected result:
- the build finishes with `Build done`
- the latest SDK `.venv` is created successfully under `/root/ameba-rtos/.venv`
- no `ameba.py: command not found` error remains, because the scripted
  entrypoint uses `python /root/ameba-rtos/ameba.py`

Confirm the packaged image artifacts exist and match the recorded sizes / hashes:
```bash
cd /root/ameba-river
stat -c '%n %s' \
  build_RTL8730E/build/project_ap/image/ap_image_all.bin \
  build_RTL8730E/build/project_hp/image/km4_image2_all.bin \
  build_RTL8730E/build/project_lp/image/km0_image2_all.bin \
  build_RTL8730E/km0_km4_ca32_app.bin \
  build_RTL8730E/km4_boot_all.bin
sha256sum \
  build_RTL8730E/build/project_ap/image/ap_image_all.bin \
  build_RTL8730E/build/project_hp/image/km4_image2_all.bin \
  build_RTL8730E/build/project_lp/image/km0_image2_all.bin \
  build_RTL8730E/km0_km4_ca32_app.bin \
  build_RTL8730E/km4_boot_all.bin
```

Expected result:
- sizes:
  - `build_RTL8730E/build/project_ap/image/ap_image_all.bin 3542112`
  - `build_RTL8730E/build/project_hp/image/km4_image2_all.bin 380448`
  - `build_RTL8730E/build/project_lp/image/km0_image2_all.bin 94208`
  - `build_RTL8730E/km0_km4_ca32_app.bin 4024960`
  - `build_RTL8730E/km4_boot_all.bin 51872`
- hashes:
  - `2f9d35ca635bcad71060403d725a5e706c5ea0455f2e303ca51f5c65543cd508`
    `build_RTL8730E/build/project_ap/image/ap_image_all.bin`
  - `47a518787efff594b981836fcbb9042b41c242850d6616cfd9d6727e215d95c7`
    `build_RTL8730E/build/project_hp/image/km4_image2_all.bin`
  - `328d80657d333dba15ac0efc72eec8e3c6552631014c0d4a6e204d29cb8395dd`
    `build_RTL8730E/build/project_lp/image/km0_image2_all.bin`
  - `469ea7db132addff59b0900b2f3bdfc18519415e6c3e1ba9c51d656a0e17b6fd`
    `build_RTL8730E/km0_km4_ca32_app.bin`
  - `faf20ef92b919df5b82dfa08dc31ae6c7f20da7cd5d701905a5cb7969b4e1ab6`
    `build_RTL8730E/km4_boot_all.bin`

Interpretation:
- this step proves the latest SDK plus the currently required river patches can
  produce a complete FP32 debug image set
- this step alone does not prove board runtime is healthy; flash + serial
  verification is still required next

## Step 5.112 Verification

Flash the latest-SDK FP32 control image to the board:
```bash
cd /root/ameba-river
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 tools/river_flash.py -p /dev/ttyUSB0'
```

Expected result:
- flash reaches `Finished PASS`
- device info is readable before download starts
- both images are downloaded:
  - `km4_boot_all.bin`
  - `km0_km4_ca32_app.bin`

Capture 20 seconds of raw boot UART immediately after flash:
```bash
rm -f /tmp/kws_latest_sdk_fp32_boot.log
script -q -f /tmp/kws_latest_sdk_fp32_boot.log -c "bash -lc 'stty -F /dev/ttyUSB0 1500000 raw -echo; timeout 20s cat /dev/ttyUSB0'"
od -An -tx1 -j 160 -N 64 /tmp/kws_latest_sdk_fp32_boot.log
bash -lc "tr -d '\000' < /tmp/kws_latest_sdk_fp32_boot.log | sed -n '1,40p'"
grep -a -n 'ameba-river boot\|File System Init Success\|kws init' /tmp/kws_latest_sdk_fp32_boot.log
```

Expected result for the current latest-SDK failure case:
- raw UART shows continuous `0x00` bytes after the `script` header:
  - `00 00 00 00 ...`
- removing `0x00` bytes leaves only the `script` wrapper text:
  - `Script started ...`
  - `Script done ...`
- `grep` finds no normal boot markers:
  - no `File System Init Success`
  - no `ameba-river boot`
  - no `kws init`

Interpretation:
- the latest-SDK FP32 image is flashable but still does not boot into a normal
  visible runtime
- this reproduces the earlier `0x00` failure mode on the user's actual
  `/root/ameba-rtos` checkout, so the blocker remains runtime / boot-chain
  divergence rather than source download or build incompleteness

## Step 5.113 Verification

Rebuild the same FP32 control firmware against the dirty-SDK baseline:
```bash
cd /root/ameba-river
rm -rf build_RTL8730E/build
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos-1.2; source /root/ameba-river/env.sh; python /root/ameba-rtos-1.2/ameba.py soc RTL8730E; python /root/ameba-rtos-1.2/ameba.py build -p'
```

Expected result:
- build finishes with `Build done`

Flash the dirty-SDK control image and capture raw boot UART:
```bash
cd /root/ameba-river
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos-1.2; python3 tools/river_flash.py -p /dev/ttyUSB0'
rm -f /tmp/kws_dirty_sdk_fp32_boot.log
script -q -f /tmp/kws_dirty_sdk_fp32_boot.log -c "bash -lc 'stty -F /dev/ttyUSB0 1500000 raw -echo; timeout 20s cat /dev/ttyUSB0'"
od -An -tx1 -j 160 -N 64 /tmp/kws_dirty_sdk_fp32_boot.log
bash -lc "tr -d '\000' < /tmp/kws_dirty_sdk_fp32_boot.log | sed -n '1,40p'"
grep -a -n 'ameba-river boot\|File System Init Success\|kws init' /tmp/kws_dirty_sdk_fp32_boot.log
```

Observed result in the current board session:
- flash still reaches `Finished PASS`
- but boot UART is again continuous `0x00`
- removing `0x00` bytes leaves only:
  - `Script started ...`
  - `Script done ...`
- `grep` finds no normal boot markers

Run one minimal UART liveness probe on the dirty-SDK image:
```bash
bash -lc "printf '\033\r\n' > /dev/ttyUSB0"
rm -f /tmp/kws_serial_probe_after_esc.log
script -q -f /tmp/kws_serial_probe_after_esc.log -c "bash -lc 'stty -F /dev/ttyUSB0 1500000 raw -echo; timeout 5s cat /dev/ttyUSB0'"
od -An -tx1 -j 160 -N 64 /tmp/kws_serial_probe_after_esc.log
```

Observed result:
- probe UART is still continuous `0x00`
- no shell prompt or banner text appears

Interpretation:
- in this session, the board can no longer act as a clean dirty-vs-latest
  runtime control, because even the dirty-SDK rebuild no longer restores normal
  serial boot output
- next recovery actions should therefore target board/session state first
  before making stronger latest-vs-dirty runtime claims

## Step 5.114 Verification

After a board power cycle, re-attach the USB serial device to WSL if needed:
```bash
usbipd.exe list
usbipd.exe attach --wsl --busid 4-4
bash -lc 'ls -l /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || true'
```

Expected result:
- the PL2303 serial device is visible in `usbipd.exe list`
- `/dev/ttyUSB0` reappears in WSL

Probe the board immediately after the power cycle:
```bash
rm -f /tmp/kws_powercycle_probe.log
script -q -f /tmp/kws_powercycle_probe.log -c "bash -lc 'stty -F /dev/ttyUSB0 1500000 raw -echo; timeout 5s cat /dev/ttyUSB0'"
od -An -tx1 -j 0 -N 128 /tmp/kws_powercycle_probe.log
```

Observed result:
- no UART output was captured
- unlike the earlier failure case, the file contains only the `script` wrapper,
  not continuous `0x00`

Reflash the dirty-SDK control image on the recovered board and probe it:
```bash
cd /root/ameba-river
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos-1.2; python3 tools/river_flash.py -p /dev/ttyUSB0'
rm -f /tmp/kws_dirty_sdk_fp32_boot_after_powercycle.log
script -q -f /tmp/kws_dirty_sdk_fp32_boot_after_powercycle.log -c "bash -lc 'stty -F /dev/ttyUSB0 1500000 raw -echo; timeout 20s cat /dev/ttyUSB0'"
bash -lc "printf '\033\r\n' > /dev/ttyUSB0"
rm -f /tmp/kws_dirty_sdk_esc_after_powercycle.log
script -q -f /tmp/kws_dirty_sdk_esc_after_powercycle.log -c "bash -lc 'stty -F /dev/ttyUSB0 1500000 raw -echo; timeout 5s cat /dev/ttyUSB0'"
```

Observed result:
- the initial 20-second passive capture may remain silent
- after `ESC + CRLF`, the board emits normal runtime logs again
- `/tmp/kws_dirty_sdk_esc_after_powercycle.log` contains normal application
  text such as:
  - `wakeword hit: text=小欧管家`
  - `xiaozhi ota bootstrap ok`
  - `Connected to websocket server`

Reflash the latest-SDK image on the same recovered board and probe it:
```bash
cd /root/ameba-river
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 tools/river_flash.py -p /dev/ttyUSB0'
rm -f /tmp/kws_latest_sdk_fp32_boot_after_powercycle.log
script -q -f /tmp/kws_latest_sdk_fp32_boot_after_powercycle.log -c "bash -lc 'stty -F /dev/ttyUSB0 1500000 raw -echo; timeout 20s cat /dev/ttyUSB0'"
rm -f /tmp/kws_latest_sdk_esc_after_powercycle.log
bash -lc "printf '\033\r\n' > /dev/ttyUSB0"
script -q -f /tmp/kws_latest_sdk_esc_after_powercycle.log -c "bash -lc 'stty -F /dev/ttyUSB0 1500000 raw -echo; timeout 5s cat /dev/ttyUSB0'"
```

Observed result:
- the latest-SDK image also flashes with `Finished PASS`
- `/tmp/kws_latest_sdk_fp32_boot_after_powercycle.log` contains normal runtime
  text rather than `0x00`, for example:
  - `Closing the Connection with websocket server`
  - `[river.cloud] xiaozhi conversation window closed: reason=followup_timeout`
  - `[river.interaction] interaction_state: wake_confirmed -> wake_monitoring`
- the follow-up ESC probe may be silent, but it still does not regress to
  `0x00`

Interpretation:
- after a real power-cycle recovery, the earlier latest-SDK `0x00` behavior is
  not stable
- board/session state significantly influenced the earlier runtime-failure
  captures
- in the recovered state, both the dirty-SDK control image and the latest-SDK
  FP32 image have been observed emitting normal runtime logs

## Step 5.115 Verification

Attach the official Ameba monitor to the recovered latest-SDK board:
```bash
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000
```

Expected result:
- monitor prints:
  - `Successfully connected to /dev/ttyUSB0, baud rate: 1500000`
- the interactive prompt `>` appears

Confirm the preserved KWS debug path is reachable on the latest-SDK board:
```text
river kws debug local status
river kws align status
```

Expected result:
- `river kws debug local status` shows:
  - `local_only=yes`
  - `kws status`
  - `kws perf`
- `river kws align status` shows:
  - `source=compiled_pcm`
  - `frames=145`
  - `duration_ms=2320`
  - `probe=stopped`

Run the preserved align dump flow under monitor log mode:
```bash
rm -rf /tmp/kws_latest_monitor_logdir
mkdir -p /tmp/kws_latest_monitor_logdir
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py \
  -p /dev/ttyUSB0 \
  -b 1500000 \
  --log \
  --log-dir /tmp/kws_latest_monitor_logdir
```

Then in the monitor console:
```text
river kws debug local on
river audio probe stop
river kws align run
```

Expected board-side result in the monitor output and log file:
- log file:
  - `/tmp/kws_latest_monitor_logdir/ttyUSB0_20260409_135610.txt`
- align replay shows:
  - `kws tensor dump armed: mode=align_best`
  - `kws align replay start`
  - `kws tensor dump captured: seq=2 infer=7 mode=align_best feat_chunks=253 input_chunks=253 output_chunks=1`
  - `wakeword hit: text=小欧管家 score_pm=371 q15=12163`
  - `kws align replay captured: seq=2 infer=7 score=0.371203 q15=12163`
  - `kws align replay done: dump=emitted`

Replay the exact latest-SDK board dump on host:
```bash
cd /root/ameba-river
OMP_NUM_THREADS=1 TF_NUM_INTRAOP_THREADS=1 TF_NUM_INTEROP_THREADS=1 \
python3 tools/kws/replay_board_tensor_dump.py \
  --log /tmp/kws_latest_monitor_logdir/ttyUSB0_20260409_135610.txt \
  --model /root/kws-trainint/artifacts/exports/student_bc_resnet_tiny_v2/model.fp32.tflite \
  --seq latest
```

Expected host replay result:
- `dump_seq=2 infer=7 gate=open`
- `board_hash: feature=0x7ce0b11d input=0xd52f011c`
- `host_hash: feature=0x7ce0b11d logged_input=0xd52f011c effective_input=0xd52f011c`
- `quant_parity: diff_bytes=0/16160`
- `board_output: raw=371 score=0.371203 exact=0.371203 q15=12163`
- `host_output: raw=371 score=0.371000 exact=0.371203`
- `output_parity: bytes_equal=yes raw_equal=yes`

Restore the board to normal runtime behavior:
```text
river kws debug local off
river audio probe start
```

Expected result:
- `local_only=no`
- `wake_handoff=normal`
- `vad probe started`

Interpretation:
- on the recovered board, the latest-SDK FP32 image passes the preserved
  board/host parity workflow end-to-end
- this means the current latest-SDK deployment is functionally correct at least
  through the preserved tensor-dump and host-replay contract

## Step 5.116 Verification

Attach the official Ameba monitor to the latest-SDK FP32 board:
```bash
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000
```

Expected result:
- monitor connects successfully and shows the `>` prompt

Trigger a real runtime session on the board:
- say the wakeword near the board
- then say one short follow-up utterance so cloud ASR and TTS both have a
  chance to run

Expected runtime logs:
- wake stage:
  - `wakeword hit: text=小欧管家`
  - `wakeword queued`
  - `Connected to websocket server`
  - `server hello: sid=...`
  - `interaction_state: wake_monitoring -> wake_confirmed`
- ASR stage:
  - `asr provider=xiaozhi_realtime session started sid=...`
  - `asr stream active`
  - one or more `partial` / `final` STT lines
- TTS/playback stage:
  - `tts sid=... state=sentence_start`
  - `playback start: stream=xiaozhi_tts`
  - `ameba_audio_stream_tx_start`
  - later `tts sid=... state=stop`
  - later `playback stop: stream=xiaozhi_tts`

During or immediately after that session, capture a status snapshot:
```text
river status
```

Expected result:
- `kws debug status` still shows:
  - `local_only=no`
  - `wake_handoff=normal`
- cloud/runtime state shows:
  - `wifi=connected`
  - `xiaozhi session=yes` while the session is open
  - nonzero `audio_rx` after TTS playback has started
- playback state shows nonzero lifecycle counters such as:
  - `starts=...`
  - `stops=...`

Interpretation:
- if the logs reach `playback start` and later `playback stop`, the board-side
  TTS output path is being exercised on the latest-SDK FP32 firmware
- if someone is physically near the board, audible confirmation should still be
  checked by ear; serial logs alone prove the playback pipeline ran, not what
  the user actually heard
- repeated `xiaozhi uplink backpressure` warnings can still appear during the
  session; treat them as a runtime quality issue unless they prevent wake, ASR,
  or TTS from completing

## Step 5.117 Verification

Read the analysis document:
```bash
cd /root/ameba-river
sed -n '1,260p' doc/XIAOZHI_UPLINK_BACKPRESSURE_ANALYSIS_LATEST_SDK_FP32_ZH.md
```

Then confirm the key code-side constraints referenced by the document:
```bash
cd /root/ameba-river
nl -ba include/river/river_xiaozhi_credentials.h | sed -n '20,50p'
nl -ba components/river_cloud/river_xiaozhi_ws.c | sed -n '228,266p'
nl -ba components/river_cloud/river_cloud_adapter.c | sed -n '300,328p'
nl -ba components/river_cloud/river_cloud_adapter.c | sed -n '1888,1948p'
nl -ba components/river_cloud/river_cloud_internal.h | sed -n '52,61p'
```

Expected result:
- credentials/config shows:
  - uplink frame duration `20 ms`
  - websocket queue max `8`
- websocket path shows:
  - audio reserve `2`
  - `send_queue_busy` is set when `(ready + reserve) >= max`
- cloud adapter shows:
  - busy path increments counters
  - busy path applies backoff
  - busy path trims stale uplink frames
- cloud internal constants show:
  - uplink ring `64`
  - stale keep `6`
  - pre-roll max `256 ms`

Optional runtime cross-check on the board:
1. attach the official monitor
2. trigger one real wake session
3. run `river status`

Expected runtime interpretation:
- if the board again shows `q_peak=6`, `bp>0`, and `last_err=send_queue_busy`
  while wake/ASR/TTS still complete, that matches the mechanism documented in
  this step
- do not interpret this alone as a KWS model deployment failure, because the
  issue lives after wakeword in the XiaoZhi uplink transport path

## Step 5.118 Verification

Read the new project status snapshot:
```bash
cd /root/ameba-river
sed -n '1,320p' doc/PROJECT_STATUS_SNAPSHOT_2026-04-09_ZH.md
```

Cross-check that the snapshot points to the expected supporting documents:
```bash
cd /root/ameba-river
ls doc/PROJECT_STATUS_ZH.md \
   doc/KWS_BOARD_HOST_PARITY_DEBUG_GUIDE_ZH.md \
   doc/RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_FP32_DEBUG_ZH.md \
   doc/RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_NANO_FP32_DEBUG_ZH.md \
   doc/RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_DSCNN_TINY_FP32_DEBUG_ZH.md \
   doc/RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_DSCNN_SMALL_FP32_DEBUG_ZH.md \
   doc/KWS_STUDENT_FP32_REALTIME_ANALYSIS_ZH.md \
   doc/KWS_INT8_INT16_DEPLOYMENT_CONSTRAINTS_ZH.md \
   doc/KWS_LATEST_ADK_QUANTIZATION_RECHECK_ZH.md \
   doc/XIAOZHI_UPLINK_BACKPRESSURE_ANALYSIS_LATEST_SDK_FP32_ZH.md
```

Expected result:
- the new snapshot document exists
- it summarizes the current `kws` branch state instead of the older `DS-CNN`
  staging snapshot
- all referenced support documents exist locally and can be opened directly

Interpretation:
- this step is complete if another engineer can open one document and get an
  accurate current overview of:
  - implemented chain status
  - verified model status
  - preserved debug/parity mechanisms
  - main blockers and recommended next priorities

## Step 5.119 Verification

Confirm the latest SDK already uses the larger debug memory layout:
```bash
cd /root/ameba-river
python3 tools/sdk/apply_rtl8730e_memory_layout_patch.py \
  --sdk-root /root/ameba-rtos \
  --variant aivoice_ca32_17mb \
  --check
```

Expected result:
- output is `applied`

Confirm the SDK Kconfig value itself was not edited by this step:
```bash
cd /root/ameba-river
rg -n --fixed-strings "CONFIG_SHELL_TASK_STACK_BASIC_SIZE=" \
  build_RTL8730E/build/project_ap/.config_ca32
```

Expected result:
- the generated config still shows the SDK value `2440`
- this proves the debug branch did not patch the SDK Kconfig default

Confirm the effective AP monitor compile uses the project-local forced include:
```bash
cd /root/ameba-river
rg -n --fixed-strings "river_sdk_debug_overrides.h" \
  build_RTL8730E/build/compile_commands.json
```

Expected result:
- the `shell_ram.c` AP compile command contains:
  `-include /root/ameba-river/include/river/river_sdk_debug_overrides.h`

Rebuild the full image from the external project:
```bash
cd /root/ameba-river
source ./env.sh >/dev/null
cmake -S /root/ameba-rtos/component/soc/amebasmart/project \
  -B /root/ameba-river/build_RTL8730E/build \
  -G Ninja \
  -DEXTERN_DIR=/root/ameba-river \
  -DFINAL_IMAGE_DIR=/root/ameba-river/build_RTL8730E
cmake --build /root/ameba-river/build_RTL8730E/build --target gen_submodule_info
cmake -S /root/ameba-rtos/component/soc/amebasmart/project \
  -B /root/ameba-river/build_RTL8730E/build \
  -G Ninja \
  -DEXTERN_DIR=/root/ameba-river \
  -DFINAL_IMAGE_DIR=/root/ameba-river/build_RTL8730E \
  -DEXAMPLE=/root/ameba-river
cmake --build /root/ameba-river/build_RTL8730E/build --parallel
```

Expected result:
- build exits successfully
- final images exist:
  - `build_RTL8730E/build/project_ap/image/ap_image_all.bin`
  - `build_RTL8730E/km0_km4_ca32_app.bin`

Post-flash debug-branch check on serial:
```text
river kws status
river kws dump status
river kws dump meta
```

Expected result:
- shell remains responsive during repeated KWS debug commands
- no obvious shell stack crash/reset appears while running normal debug flows
- this step only validates debug stability and headroom; it does not change KWS
  model behavior

Confirm the Codex harness pointers still match the repo workflow:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
```

Expected result:
- the script exits successfully with `check_codex_harness: all checks passed`

Rebuild the latest-SDK external project image after the XiaoZhi uplink queue
diagnostic changes:
```bash
cd /root/ameba-river
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

Expected result:
- the build exits successfully with `Build done`
- the build uses `/root/ameba-rtos` as the SDK baseline

Confirm the new queue model is compiled into the tree:
```bash
cd /root/ameba-river
rg -n "RIVER_XIAOZHI_WS_STABLE_BUF_NUM|soft_reserve|hard_full|audio_soft_limit|reserve_bp|full_bp" \
  include/river/river_xiaozhi_credentials.h \
  components/river_cloud/river_xiaozhi_ws.c
```

Expected result:
- `include/river/river_xiaozhi_credentials.h` defines
  `RIVER_XIAOZHI_WS_STABLE_BUF_NUM`
- `components/river_cloud/river_xiaozhi_ws.c` contains:
  - `reason=soft_reserve`
  - `reason=hard_full`
  - `audio_soft_limit`
  - `reserve_bp`
  - `full_bp`

Post-flash board validation for the `ready=14/16` high-water mark:
```text
river xiaozhi status
```

Expected result:
- the XiaoZhi status line now includes:
  - `txq=.../16`
  - `stable=12`
  - `audio_soft_limit=14`
  - `reserve_bp=...`
  - `full_bp=...`

During a wakeword -> ASR session, watch serial for:
```text
xiaozhi ws backpressure: kind=audio reason=soft_reserve ready=14 ... max=16 stable=12 reserve=2 free=2 soft_limit=14
```

Expected result:
- `ready=14/16` is now explicitly labeled `reason=soft_reserve`
- if the pressure is mostly due to the project-side audio reserve, then:
  - `reserve_bp` increases
  - `full_bp` remains `0` or materially lower
- only `reason=hard_full` or a rising `full_bp` means the SDK send queue is
  actually reaching `16/16`

Confirm the Codex harness pointers still match the repo workflow:
```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
```

Expected result:
- the script exits successfully with `check_codex_harness: all checks passed`

Rebuild the latest-SDK external project image after the XiaoZhi bootstrap cache
change:
```bash
cd /root/ameba-river
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

Expected result:
- the build exits successfully with `Build done`
- the build uses `/root/ameba-rtos` as the SDK baseline

Confirm the bootstrap cache logic is compiled into the tree:
```bash
cd /root/ameba-river
rg -n "RIVER_XIAOZHI_BOOTSTRAP_CACHE_TTL_MS|bootstrap cache hit|bootstrap_owned|bootstrap_refresh_in_ms|cache_ttl_ms" \
  include/river/river_xiaozhi_credentials.h \
  components/river_cloud/river_xiaozhi_ws.c \
  doc/XIAOZHI_SESSION_STABILITY_EXECUTION_PLAN_ZH.md
```

Expected result:
- the credentials header defines `RIVER_XIAOZHI_BOOTSTRAP_CACHE_TTL_MS`
- `river_xiaozhi_ws.c` contains:
  - `xiaozhi bootstrap cache hit: refresh_in_ms=...`
  - `cache_ttl_ms`
  - `bootstrap_owned`
  - `bootstrap_refresh_in_ms`
- the active XiaoZhi execution-plan document records the new cache-based
  connection assumption

Post-flash board validation for "skip per-wake bootstrap":
```text
river xiaozhi status
```

Expected result:
- the XiaoZhi status line now includes:
  - `bootstrap_owned=yes|no`
  - `bootstrap_refresh_in_ms=...`

Wake the device twice within the `10 min` bootstrap cache TTL and compare the
logs:
```text
1st wake:
  wakeword hit ...
  xiaozhi ota bootstrap ok: ... cache_ttl_ms=600000 ...
  xiaozhi connecting: ...

2nd wake within TTL:
  wakeword hit ...
  xiaozhi bootstrap cache hit: refresh_in_ms=...
  xiaozhi connecting: ...
```

Expected result:
- the first wake after boot or after cache expiry may still log
  `xiaozhi ota bootstrap ok`
- a later wake inside the TTL should log
  `xiaozhi bootstrap cache hit: refresh_in_ms=...`
- that later wake should not need another synchronous OTA bootstrap before
  `xiaozhi connecting`
