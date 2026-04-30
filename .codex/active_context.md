# Codex Active Context

This file is the canonical volatile context for Codex-facing work in this
repository. Update it when the working branch, active objective, SDK baseline,
or top-of-tree verification target changes.

## Active Working Set

- Current working branch: `agent-server-v2`
- Active SDK baseline: `/root/ameba-rtos`
- Active build command:
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'`
- Active flash command:
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 /root/ameba-river/tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'`
- Active monitor command:
  - `python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000`
- Latest landed step:
  - `5.561 empty turn followup recover now runs on IO tick, stale ASR active cleared from cloud truth（待验证）`
- Latest workflow sync:
  - future `git commit` messages in this repository should use clear Chinese
    descriptions by default
- Latest planning sync:
  - newest landed runtime bug-fix slice:
    - Step 5.561 收空响应后无 TTS 的真正状态机脱节：
      - 云端返回 `state=active input_state=active output_state=idle` 后，端侧不应长期停在 `asr_streaming` 但没有任何新的 `listen_start/asr round begin`
      - dialog runtime 现在在 cloud snapshot 可用时把 `asr_session_active` 直接收敛到 `cloud_round_active`
      - XiaoZhi 的 empty-turn follow-up recover 改为在 IO tick 中统一执行，避免在 websocket 事件回调里同步 reopen
    - 下一步上板验证：
      - empty-turn active-return 后不再卡住 `interaction_state=asr_streaming`
      - IO tick 会看到 `xiaozhi empty turn followup recover: action=reopen_listen`
      - 下一句用户语音应重新进入 `asr round begin` / `asr stream active`
  - newest landed runtime bug-fix slice:
    - Step 5.560 收首轮无 TTS 的半速 uplink：
      - 15:04 新日志显示 `audio_ms=1380 / duration_ms=2812 / pace_pct=49 / packets=69`
      - `preview_warmup ... bypass=0 backlog_max=2` 说明 warmup catch-up 门槛过严，`backlog=1~2` 时根本不追平
      - 现在把 warmup burst/bypass 触发门槛从 `>1` 放宽到 `>0`
    - 下一步上板验证：
      - `pace_pct` 不再卡在 49~50
      - `preview_warmup ... bypass=` 开始大于 0
      - 首轮不再直接 `empty turn returned active`，而是进入正常 `response.start/audio.out.meta`
  - newest landed runtime bug-fix slice:
    - Step 5.559 对齐服务侧新的 segment 合同：
      - 服务侧不再对同一 `segment_id` 二次补发 `audio.out.meta` 修正 `is_last_segment`
      - 端侧 `playback_note_meta()` 不再等待 same-segment `false -> true` promotion，也不再用 duplicate same-segment meta 驱动 terminal fold
      - duplicate same-segment meta 现在只打印观察日志，不再改写 `wait_context / last_segment_context` 以外的 playback 状态机
    - 下一步上板验证：
      - 同一 `segment_id` 重复 meta 只出现 `duplicate same-segment meta ignored`
      - completed/cleared 按首个 segment 事实闭环，不再等 second meta
      - 播放尾态结束后，后续一句话能重新进入 ASR
  - newest landed runtime bug-fix slice:
    - Step 5.558 收当前最差体验三连：
      - 首轮 wakeword 会话不再把第一段 pre-roll 原样重放给服务端，降低 wake residual 空轮次
      - 长单段 `segment_prefetch` 起播门槛加上限，避免 TTS 起播前长时间沉默
      - `upstream_starved` 的 recover 门槛从 120 ms 放宽到 240 ms，减少中途短抖动直接 flush
    - 下一步上板验证：
      - 观察是否出现 `xiaozhi wake preroll dropped once`
      - 长段回复的 `audio.out.meta -> playback start` 等待是否下降
      - `supply_gap_ms≈100~200 ms` 的抖动是否不再立刻触发 `playback recover`
  - newest landed runtime bug-fix slice:
    - Step 5.557 收主回答尾段提前切断：
      - 旧逻辑在 `played_duration_ms == expected_duration_ms` 时就立刻 fully-heard + pop 当前 non-last segment
      - 队列随即变空，runtime 进入 `waiting_next_segment`，再被 `segment gap hold / playback recover` 拉去 flush/restart
      - 现在对“当前是唯一 segment 且还不是 last”的场景增加 tail drain grace，先给 backend buffer 排空和 late promotion/successor 一个安全窗口
    - 下一步上板验证：
      - `played_duration_ms=expected_duration_ms` 后不应立刻出现 `playback recover` / `segment gap hold`
      - same-segment late promotion 仍可继续本地折叠/升级
      - 主回答尾音应完整播完，不再只剩前半段
  - newest landed runtime bug-fix slice:
    - Step 5.556 对齐服务侧 same-segment promotion 语义：
      - 同一 `response_id + playback_id + segment_id` 仅做 `false -> true` last 升级时，端侧按原 segment 的元数据升级处理
      - 不再把这类 promotion 当成新 segment 重新驱动 prefetch / recover / gap hold / stop
      - 若 mark 已覆盖 expected duration，仍可直接本地折叠闭环
    - 下一步上板验证：
      - 观察 `xiaozhi playback same-segment last-meta promotion`
      - false->true promotion 不再触发新分段切换副作用
  - newest landed runtime bug-fix slice:
    - Step 5.555 收 playback stale tail：
      - 同一 `segment_id` 先报 non-last、播完后又迟到补成 last 时，旧逻辑还会把它当普通 meta 重发
      - 现在若当前 tail 已播到 expected mark 且正处于 `waiting_next_segment` 安全窗口，就直接折叠这次 late last-meta upgrade
      - 目标是收掉 `_0002` 这类播完后又被迟到 meta 拉回 recover/gap hold 的抖动
    - 下一步上板验证：
      - 观察 `xiaozhi playback late last meta consumed stale current tail`
      - 同段 late last upgrade 不再触发额外 `playback recover` / `segment gap hold`
  - newest landed runtime bug-fix slice:
    - Step 5.554 恢复受控的 preview warmup/catch-up：
      - 旧 uplink 成功发完一帧后仍严格等下一拍，preview startup 几乎没有追平空间
      - 现在只在 `preview_warmup` 窗口内、且队列里确实还有 backlog 时，允许最多 `3` 帧的短 burst，并记录 `preview_warmup_bypass_count`
      - steady-state drain 仍保持 `1` 帧，不把常态 uplink 改成长期突发发送
    - 下一步上板验证：
      - `burst_max` / `preview_warmup_bypass_count` 在启动积压轮次应开始变化
      - 首个 preview partial 更早到达
      - accept 前 preview/uplink backlog 指标应比修复前下降
  - newest landed runtime bug-fix slice:
    - Step 5.553 放宽 endpoint hint 本地 soft-close：
      - 旧的 `RIVER_CLOUD_XIAOZHI_ENDPOINT_SOFT_CLOSE_DEFER_MS=320 ms` 太短，preview refresh / finalize 稍晚一点就会先被本地 close 抢断
      - 现在把 hint-only defer 提到 `960 ms`
      - 同时 `input_preview` 只要 `text` 或 `stable_prefix` 有进展，就撤销 endpoint soft-close
      - 目标是给 server-owned endpoint path 多一轮补救空间，降低 `endpoint_soft_close_timeout` 抢断 preview 的概率
    - 下一步上板验证：
      - `endpoint_soft_close_timeout` 触发频率应下降
      - preview 仍在变好的轮次不再轻易被端侧本地 close 抢先截断
      - 然后继续做 uplink preview warmup/catch-up
  - newest landed runtime bug-fix slice:
    - Step 5.552 先收 false accept 入口：
      - 端侧 no-ref reopen 旧门槛只有 `6` 帧/120 ms，和服务侧 turn3 的残留量级直接重合
      - 现在把 `RIVER_CLOUD_XIAOZHI_NOREF_OPEN_HOLD_FRAMES` 提到 `12` 帧/240 ms，只影响 no-ref reopen 路径
      - 目标是先削掉播放尾边短残留语音误开 follow-up round 的端侧入口，降低 silent/stale accepted turn
    - 下一步上板验证：
      - 120 ms 左右 residual 不再轻易重开 ASR
      - `accepted 后无 response.start` 的 false accept turn 数量先下降
      - 正常完整 follow-up 语音仍能继续进入 ASR
  - newest landed runtime bug-fix slice:
    - Step 5.551 根据 2026-04-28 17:12 新日志继续收口：
      - 上一轮 `late completed audio dropped` 已经收住 final tail `cancel_stop/arm_stop` 抖动；新的主卡点收敛到 `_0001 -> _0002` 多段交接
      - 根因是 `river_cloud_xiaozhi_playback_note_meta(...)` 过早把新 segment `valid/count` 对外发布，downlink worker 在当前 head 被 pop 的瞬间能看到一个 ids 尚未填完的 next slot
      - 端侧现在改为先填完 `response_id/playback_id/segment_id/text/expected_duration_ms/is_last_segment`，再发布 `valid/count`
      - 目标是消除 `xiaozhi playback ack started send failed: status=-1 ... playback_id=- segment_id=-` 这类 partial-id started-ack
    - 下一步上板验证：
      - `_0002` meta 后不再出现 started queue/send failed 且 `playback_id=- segment_id=-`
      - 应继续看到 `_0002` 的 started/mark 推进，而不是只剩 `_0001`
      - transport close 时 `last_fully_heard` 应推进到最后一段，不再拖到 `idle_timeout`
  - newest landed runtime bug-fix slice:
    - Step 5.550 根据 2026-04-28 16:20 新日志继续收口：
      - `accepted -> response.start` 主链已恢复，多轮会话可进入第 2 轮；当前新的主卡点是 playback 终段已经 completed-ready 后仍有 late audio 迟到，反复触发 `cancel_stop`
      - 端侧现在在 `stop_pending=yes && playback_completed_ready()` 时直接丢弃 late completed audio，不再写入 ring，也不再取消 stop
      - 新日志 `xiaozhi late completed audio dropped: ...` 用于确认是否命中这条尾段兜底
    - 下一步上板验证：
      - final mark 后不再反复看到 `draining -> playing reason=cancel_stop queued=1 segments=0`
      - 若仍有迟到尾帧，应直接看到 `late completed audio dropped`
      - 若 turn 3 的服务侧 400 由尾态污染引起，这一刀后应显著收敛或消失
  - newest landed runtime bug-fix slice:
    - Step 5.549 对齐 2026-04-28 服务侧 empty-turn / EOF 定位：
      - `accepted` 后新增 `accepted_response_watchdog`，端侧不再默认把 accepted 建模成“必然会收到 `response.start`”
      - 若服务端把本轮按空语音直接收回 `active/idle`，端侧会明确走 `empty_turn returned active` 路径，清 preview / turn semantics，并重新 touch follow-up window
      - 仅在这条 empty-turn active-return 路径内，新增一次窄范围 `transport_closed` auto-recover：若 follow-up window 仍有效、Wi-Fi 在线、且本地没有 active stream / playback turn，则直接重开 `open_session_and_listen()`
      - 若 accepted 后既没有 `response.start` 也没有 active-return，6s watchdog 会主动 abort 并重同步会话状态，避免 accepted 后永久悬挂
    - 下一步上板验证：
      - empty-turn 回 active 时应看到 `xiaozhi empty turn returned active: ...`
      - 若随后 1 次 follow-up EOF 落在 recovery window 内，应看到 `xiaozhi transport closed followup recover/recovered: ...`
      - 若服务端/链路让 accepted 长时间没有下一步，应看到 `xiaozhi accepted response watchdog timeout: ...`
  - newest landed runtime bug-fix slice:
    - Step 5.548 继续收窄“播完一轮 TTS 后后续说话无反应”的残留空间：
      - 复查 Step 5.547 后确认，仍存在另一类 stale output-turn：`playback_lane` 已经空闲，但 `playback_turn` 仍因 terminal truth 残留而阻断 reopen
      - stale output guard 现在不再要求 `playback_lane_engaged=yes`，而是只要满足：
        - `window_active=yes`
        - `stream_active=no`
        - 服务端 `output_state` 不在 `thinking/speaking`
        - `response_waiting_audio=no`
        - `playback_turn_active=yes`
        - `playback_output_active=no`
        - `playback_rebuffer_pending=no`
      - 因而同一条 720ms guard 现在同时覆盖：
        - lane 残留
        - terminal-only turn 残留
      - 新日志额外带 `wait=yes/no`，便于区分是 wait-context 卡住还是 terminal truth 单独残留
    - 下一步上板验证：
      - 若旧故障再次出现，但 `playback_lane=no playback_turn=yes`，也应看到 `xiaozhi stale output guard armed`
      - 必要时应继续看到 `forcing playback clear`
      - 触发后同一句 follow-up 应继续进入 ASR，而不是只剩 VAD
  - newest landed runtime bug-fix slice:
    - Step 5.547 再补一条多轮对话兜底：
      - 如果服务端 `output_state` 已不在 `thinking/speaking`，物理 playback 也不活跃，但本地 `playback_lane/turn` 仍残留，持续用户语音现在会先 arm `stale_output_guard`
      - 若该异常态持续 `720ms`，端侧会本地触发一次 `xiaozhi_stale_output_guard` playback interrupt/clear，再立即重评 follow-up reopen
      - 目标是把“output-turn truth 残留导致整轮只剩 VAD、没有 ASR”从永久卡死降级为可自恢复异常
    - 下一步上板验证：
      - 正常 `thinking/speaking/rebuffer` 期间不应误触发该 guard
      - 异常复现时应先看到 `stale output guard armed`，必要时再看到 `forcing playback clear`
      - 触发后同一句 follow-up 能继续进入 ASR，而不是只能等 `idle_timeout`
  - newest landed runtime bug-fix slice:
    - Step 5.546 根据 2026-04-28 14:30 `已帮你打开灯光。` 新日志继续收口：
      - late last-meta 现在不仅覆盖“queue 已空”的尾态，也覆盖“current segment 仍残留，但 runtime 已经停在 `OWNED_PAUSED + WAITING_NEXT_SEGMENT`”的 stale current tail
      - 对 same-segment stale current tail，若 `last_mark_ms >= expected_duration_ms`，直接标记 fully-heard、弹出 current segment，再复用既有 terminal fold/completed 收口
      - 新增 `late last meta consumed stale current tail` 日志，并在 `late last meta folded` 中补充 `consumed_current=yes/no`
    - 下一步上板验证：
      - 对用户 14:30 那组日志模式，final `played_duration_ms=1100` 后不再继续挂在 `wait_next=yes`
      - 应看到 `xiaozhi playback late last meta consumed stale current tail: ...` 或 `late last meta folded: ... consumed_current=yes`
      - 后续一句话能重新进入 ASR，而不是只剩 VAD `speech/silence`
  - newest landed runtime bug-fix slice:
    - Step 5.543 根据 2026-04-28 13:52 `已帮你打开灯光。` 新日志继续收口：
      - 同一 `segment_id` 已达到 final mark 后，若 only `marked_context` 对齐而 `fully_heard_context` 仍未及时落账，晚到 `is_last_segment=yes` 现在也能补齐 terminal fold
      - fold 前会在 `last_mark_ms >= expected_duration_ms` 时，把 same-segment `marked_context` 合成为 `fully_heard_context`
      - 新增 `late last meta synthesized fully-heard` 诊断日志，便于确认是否命中这条兜底路径
    - 下一步上板验证：
      - 播放完 `已帮你打开灯光。` 后，不再卡在 `prefetching/backend=owned_paused`
      - 日志应出现 `late last meta folded`，必要时还会出现 `late last meta synthesized fully-heard`
      - 下一句能重新进入 ASR，而不是只剩 VAD 日志直到 `idle_timeout`
  - newest landed runtime bug-fix slice:
    - Step 5.542 根据 2026-04-28 13:34 新日志继续收口：
      - 服务端对同一 `segment_id` 补发 `is_last_segment=yes` 时，若该 segment 已 fully-heard，不再重建新的 playback segment
      - same fully-heard segment 的 late last-meta 改为直接折叠进 terminal lineage，避免 playback 卡在 `owned_paused/prefetching`
      - 对 paused attached stale tail，会 reset queued tail 并停止 `xiaozhi_late_last_meta` backend，再尝试 completed 收口
    - 下一步上板验证：
      - 最后一段 final mark 后，即使服务端再补同 `segment_id is_last_segment=yes`，也不会再卡住 output turn
      - 播放完 `我没听清，请再说一遍。` 后，下一句能重新进入 ASR
      - 不再一直拖到 `xiaozhi session.end: ... idle_timeout` 才退出 `barge_in_listening`
  - newest landed runtime bug-fix slice:
    - Step 5.541 根据 2026-04-28 新复测日志继续收口：
      - completed ACK queue/sent 增加 `completed_reported` 早退，并在 `COMPLETED_QUEUED` 后立即同步 report flags，避免同一 playback 重复 `completed`
      - dialog runtime 的 output-turn engaged 现在直接承认 `lane_engaged`，覆盖 `audio.out.meta` 到真实 playback start 之间，以及 stop-pending/drain 期间的窗口
      - transport close 终态策略先清 session-update cache、preview state、turn semantics，再触发 playback-stop 驱动的状态同步，避免旧 `previewing` truth 泄漏
    - 下一步上板验证：
      - 同一 `playback_id` 只出现一次 `xiaozhi playback ack completed queued/sent`
      - `audio.out.meta` 到 `playback start` 之间不再出现 `... -> asr_streaming reason=playback_state`
      - drain 尾态 `tts_stop_pending=yes` 时不再提前回到 `asr_streaming`
      - `transport_closed` 后不再残留旧 `input_state=previewing` 的 `turn accepted: trigger=poll`
  - newest landed runtime bug-fix slice:
    - Step 5.540 根据 2026-04-28 `未识别到有效语音` 新日志继续收口：
      - playback terminal completed 后，晚到 downlink audio 不再无条件取消 `tts_stop_pending`，避免 `draining -> playing reason=cancel_stop queued=1 segments=0`
      - completed ACK 改为 backend stream 真正 detach 后再 queue/sent，避免 DAC 尚未 drain 完就提前报 completed
      - `reset_transport_state()` 先清 stream/preview/turn 语义，再发 `session_closed`，避免 transport close 时错误发布 `asr_streaming`
    - 下一步上板验证：
      - 服务端播放 `未识别到有效语音。` 后，播放尾态能正常 stop，后续说话可再次触发响应
      - 不再出现 `playback phase: draining -> playing reason=cancel_stop queued=1 segments=0`
      - transport close 时不再带着旧 `previewing` 语义回到 `asr_streaming`
  - newest landed runtime bug-fix slice:
    - Step 5.539 根据 2026-04-28 旧日志复盘继续收口 zero-duration ACK tail：
      - playback terminal 对外 `terminal_closed` 现在要求 terminal 已完成且 backend 已退出 output-active，且不再 `tts_stop_pending`
      - zero-duration fast-launch ACK 的 local completed / terminal close 对 dialog runtime 的可见性延后到 DAC drain 结束后，避免播放尾巴期间提前回到 `asr_streaming`
      - dialog runtime 状态切换前新增 `dialog_runtime interaction_transition` 日志，带 `terminal_closed/playback_active/tts_stop_pending/duplex_ready_seen`
    - 下一步上板验证：
      - zero-duration ACK 尾态不再出现 `interaction_state: barge_in_listening -> asr_streaming reason=arm_stop` 早于物理 playback stop
      - transition 日志在 playback drain 期间应保持 `terminal_closed=no`，并带出 `tts_stop_pending=yes` 或 `playback_active=yes`
      - drain 完成后才允许 `terminal_closed=yes` 并退出 output-turn engaged
  - newest landed runtime bug-fix slice:
    - Step 5.538 根据 2026-04-28 上板日志继续收口：
      - uplink 成功发送后的 next due 改为沿既有 deadline 递进，减少一次 late send 对后续 20ms pacing 的永久漂移
      - ASR round `burst_max` 不再复用 backlog 深度，只统计真实单轮发送 burst；backlog 继续看 `backlog_p95/max`
      - async playback mark ACK 入队前增加同 segment + played duration 的队列级去重，避免重复 `audio.out.mark`
    - 下一步上板验证：
      - 正常 round 的 `xiaozhi asr round finish` 应回到 `burst_max=1`
      - `send_interval_p50/p95/max` 应明显收敛，不再被一次 late send 持续拖慢
      - 不再出现同一 `segment_id` 同一 `played_duration_ms` 的重复 `playback ack mark sent`
  - newest landed runtime bug-fix slice:
    - Step 5.537 修正 Step 5.536 复查发现的残留路径：
      - `input.endpoint candidate=yes` 不再在本地 post-roll 时直接关闭 round
      - endpoint candidate 只作为观察事件，转入 `server_accept_wait`，继续等待 `session.update.accept_reason`
      - 若服务端只给 candidate 但不 accepted，1.8s 后仍可 fallback commit
    - 下一步上板验证：
      - endpoint candidate 后应看到 `waits for accepted truth` 与 `server accept wait armed`
      - local round 应由后续 accepted truth 或 fallback commit 收口
  - newest landed runtime bug-fix slice:
    - Step 5.536 对齐服务侧 2026-04-27 端侧 P0/P1/P2 建议：
      - server endpoint 可用时，本地 VAD post-roll 只进入 `server_accept_wait`，默认不发正常路径 `audio.in.commit`
      - 1.8s 内等待服务端 `session.update.accept_reason` 作为 accepted truth，超时才 fallback commit
      - 上行取消 preview/backlog due bypass，drain burst 上限为 1，发送后按 `now + 20ms` 排下一帧，避免 backlog burst
      - 零时长 last segment 必须等 downlink queue/retry drain 且已有真实 mark 后才标记 fully-heard 并推进 completed ACK
      - ASR round finish 日志新增 frame bytes、send interval max、capture age max、backlog max、send duration max 与 dropped_frames
    - 下一步上板验证：
      - server endpoint mode 下本地 stop 后应看到 `server accept wait armed`，不应出现正常路径 `audio.in.commit`
      - 常态 `burst_max=1`，`send_interval_p50/p95/max` 接近 20ms；弱网时丢旧帧而不是补发积压
      - `expected_duration_ms=0 is_last_segment=yes` 应在 queue drain 后 completed，不再触发服务端 audio stream deadline
  - newest landed protocol-sync slice:
    - Step 5.531 对齐服务端 `1a94986 Optimize realtime preview backlog handling`：
      - 服务端 preview 观察现在明确为 latest-state hints，允许合并或跳过中间 `input.preview / input.accept_ready / input.endpoint`
      - 端侧 accepted-turn 判定不变，仍只认 `session.update.accept_reason`
      - 新增 `input.accept_ready` 解析、日志和 preview truth 缓存，避免继续打印 ignore unknown event
      - discovery preview 日志新增 `accept_ready` 能力位
      - `input.accept_ready` 只更新窗口和观察状态，不触发 local round close / audio.in.commit / accepted-turn
    - 下一步上板验证：
      - updated server 下不再出现 `ignore xiaozhi message type=input.accept_ready`
      - 收到 accept-ready 时打印 `xiaozhi input.accept_ready: ...`，cloud/transport preview 状态行显示 `accept_ready=yes`
      - 真正 accepted 仍由 `session.update accept_reason=server_endpoint` 驱动
  - newest landed latency slice:
    - Step 5.530 对齐服务侧 `preview_uplink_realtime_ratio=0.52~0.65` 定位：
      - 端侧确认当前 20 ms PCM 帧为 640B，发送路径直接 `ws_sendBinary`，未被旧 512B scratch 常量截断
      - 真实慢点是 warmup 阶段仍被 20 ms due pacing / I/O 调度限制，导致首段音频慢慢到达服务端
      - 新增 `RIVER_CLOUD_XIAOZHI_UPLINK_PREVIEW_WARMUP_MS=320` 与 `RIVER_CLOUD_XIAOZHI_UPLINK_PREVIEW_BURST_MAX=6`
      - 仅在本轮已发送音频少于 320 ms 且 ring/retry 确有待发帧时绕过 due；warmup 结束后回到原 20 ms pacing / 常态 2 帧上限
      - `xiaozhi asr round finish` 新增 `preview_warmup[target_ms=320 done_ms=... bypass=...]`，用于与服务侧 preview ratio 对齐
    - 下一步上板验证：
      - `preview_warmup.done_ms` 无 backpressure 时应接近/低于 350 ms
      - `pace_pct` 应明显高于历史 52~56，`send_interval_p50/p95` 不应继续停在 37~61 ms
      - `busy/fail/stale_drop/ring_drop` 常态保持 0；若仍慢，优先看 `send_duration_p95` 和 WS backpressure
  - newest runtime bug-fix slice in progress:
    - Step 5.529 继续收口 2026-04-27 `audio.out.meta` 后播放启动异常的端侧风险：
      - playback stats / reference read+stats / native capture reference observation / dialog voice policy view 均改为 capture-hot-path 非阻塞降级
      - runtime stats snapshot 改为非阻塞，避免 VAD state-change 诊断卡住采集消费者
      - VAD barge-in duck 控制改为非阻塞；release 拿不到 playback lock 时保留 duck-active 标志以便后续重试
      - 目标是即使 AudioTrack 或 reference 维护短时卡顿，也不再饿死 VAD/capture consumer 导致 `capture frame ring overflow` 持续增长
    - 下一步上板验证：
      - `audio.out.meta` 后不再出现 `interaction_state: thinking -> asr_streaming reason=note_meta`
      - 不再出现空 ASR round：`duration_ms=4 audio_ms=0 packets=0`
      - `playback_start_prepare` 后不再持续刷 `capture frame ring overflow`
    - Step 5.528 针对 2026-04-27 新日志中的端侧问题：
      - 服务端已下发 `audio.out.meta` 且 `output_state=speaking`，端侧不应由 `note_meta` 把 interaction 推回 `asr_streaming`
      - speaking playback lane engaged 但物理播放/AEC 未 ready 时，follow-up reopen 继续阻断，避免空 ASR round
      - XiaoZhi downlink task 优先级降到 VAD/capture consumer 之下，避免 AudioTrack 启动卡顿导致 mic capture ring 持续 overflow
      - native capture reference 配置下 TTS playback 不再额外打开 playback reference export，降低播放启动耦合风险
      - playback service 新增 backend prepare / AudioTrack_Start 前日志，便于定位后续卡点
    - 下一步上板验证：
      - `audio.out.meta` 后不再出现 `interaction_state: thinking -> asr_streaming reason=note_meta`
      - 不再出现空 ASR round：`duration_ms=4 audio_ms=0 packets=0`
      - 不再持续刷 `capture frame ring overflow`
  - newest landed runtime bug-fix slice:
    - Step 5.527 收口 2026-04-26 新日志中的残留 `audio.in.commit` 竞态：
      - 当服务侧 preview 已给出 `input.endpoint candidate=yes` 且 discovery/negotiation 表示 server endpoint 可用时，端侧 post-roll 不再发送本地 `audio.in.commit`
      - 该路径直接以 `server_endpoint_candidate` 关闭本地 ASR round，等待服务端随后返回 `server_endpoint` accept
      - `audio.in.commit` wire-level 入口新增二道保险：当 session 已非 `active`、input 已 `committed`、output 为 `thinking/speaking` 或 response 已开始时跳过发送并打印 skip reason
      - 下一步上板验证：
        - endpoint candidate 后不应再出现 `turn_not_ready / audio.in.commit is accepted only while the session is active`
        - 应看到 `xiaozhi server endpoint candidate suppresses local audio.in.commit` 或 `xiaozhi audio.in.commit skipped: ...` 诊断
        - no-audio 主现象若仍存在，应继续表现为 `response audio abandoned`，而不是端侧 commit error
  - newest landed runtime bug-fix slice:
    - Step 5.526 对齐 2026-04-26 板端 no-audio 复现日志：
      - 服务端 accepted 后、response 尚未完成前，端侧不再因为本地 VAD speech 立即重开 follow-up ASR round
      - 当 accepted turn 仍处于 `output_state=thinking`，或 `response.start` 后尚未观察到 `audio.out.meta` 时，follow-up reopen 会被 `response_pending` guard 阻断
      - 阻断期间清零 open-hold 计数，并只打印一次 `xiaozhi followup reopen blocked: reason=response_pending ...`
      - 默认关闭本地 no-audio fallback prompt：`RIVER_CLOUD_XIAOZHI_LOCAL_RETRY_PROMPT_ENABLED=0`
      - no-audio abandoned / timeout recovery 只记录 `local fallback prompt skipped`，不再启动现场尚未证明非阻塞的 `xiaozhi_local_retry` playback
      - 下一步上板验证：
        - no-audio 后不应再出现默认 `playback_start_prepare` / 长时间 `capture frame ring overflow`
        - response pending 期间不应再看到新的 ASR round reopen
  - newest landed runtime bug-fix slice:
    - Step 5.525 对齐 2026-04-26 服务侧首音频失败处理：
      - 服务侧现在只有首个真实 audio chunk ready 后才进入 `speaking` / 发送 `audio.out.meta`
      - 若服务侧返回 `session.update state=active / output_state=idle` 且本轮仍未观察到 `audio.out.meta`，端侧会清理 response-audio wait lineage
      - 新增 `xiaozhi response audio abandoned` 与 `xiaozhi response audio wait cleared` 日志，区分“服务已放弃本次音频”与“服务完全悬挂”
      - 不发送伪造的 `audio.out.started / mark / completed`，playback facts 仍只由 `audio.out.meta` + 实际播放驱动
      - 若服务侧既不回 active/idle 也不发 `audio.out.meta`，Step 5.523 的 5s `response_audio_timeout` recovery 仍保留
    - 下一步上板验证：
      - 首音频失败但服务回 active/idle 时，应看到 abandoned/cleared 日志且不再 5s 后 abort/close 同一 response
      - 服务完全悬挂时仍应进入 `response_audio_timeout` recovery
  - newest landed runtime bug-fix slice:
    - Step 5.524 基于 2026-04-26 服务侧更新版 RTOS 实时建议，把端侧剩余首个改进点选为 uplink bounded catch-up：
      - 保持 20ms PCM 帧格式不变
      - successful send 后用 `next_send_ms` 按 20ms cadence 排下一帧
      - 本地 I/O 调度已经落后时才允许最多 `RIVER_CLOUD_XIAOZHI_UPLINK_DRAIN_BURST_MAX=2` 的 catch-up
      - 等待下一帧 due 时不在 uplink path 内 sleep，避免阻塞 WS poll/control/downlink 处理
    - 新增 `burst_max` 诊断：
      - `river xiaozhi status` 输出全局/round burst 最大值
      - `xiaozhi asr round finish` 输出 round-level `burst_max`，便于和服务侧 send interval / capture age 观测对齐
    - 本步不改变 no-audio 收口策略：
      - 无 `audio.out.meta` / PCM 时仍不伪造播放事实
      - 继续由 Step 5.523 的 response audio timeout recovery 释放 session/window，等待服务侧 TTS 队列修复
    - 下一步上板验证：
      - 常态 `burst_max` 应接近 1，调度落后时不超过 2
      - 不应回归重复 `turn_not_ready` / `audio.in.commit` loop
      - 服务 TTS 无 meta 时仍应 timeout recovery 并允许后续唤醒
  - newest landed runtime bug-fix slice:
    - Step 5.523 在 `response.start` 后 5s 无 `audio.out.meta` 的诊断基础上增加本地恢复动作：
      - 首次 timeout 后打印 `xiaozhi response audio timeout recovery`
      - 请求 `tts interrupt` / server abort
      - 用 `SERVER_RESPONSE` 收口本地 ASR round，避免后续重复 `audio.in.commit`
      - 关闭 conversation window 并请求关闭 XiaoZhi session
    - 目标是服务侧 TTS 堵塞时端侧不再长时间卡在旧 speaking/window，后续唤醒有机会重新建会话
    - 下一步上板验证：
      - TTS 堵塞时 timeout 后应进入 recovery 并能再次唤醒
      - TTS 恢复时正常 `audio.out.meta` / PCM 到达则不触发 recovery
  - newest landed runtime bug-fix slice:
    - 基于 2026-04-25 服务侧与板端日志复盘，“一直没有声音”的直接端侧现象是：
      - 已收到 realtime `response.start` / `response.chunk` 文本
      - 未收到 `audio.out.meta` 或 binary PCM
      - 服务侧定位 qwen_unified TTS 单并发队列堵塞，导致 agentd 等待音频超时
    - Step 5.522 将端侧 realtime 文本 response 与音频播放事实拆开：
      - `response.start` / `response.chunk` 不再合成本地 `RIVER_XIAOZHI_EVENT_TTS`
      - legacy `tts` 消息仍保留兼容 TTS event 路径
      - 音频播放事实继续只由 `audio.out.meta` 与 binary PCM 驱动
    - 新增 `5s` response audio wait timeout 诊断：
      - 若 response 已开始但未看到 `audio.out.meta`，打印 `xiaozhi response audio wait timeout`
      - 日志带 response id、turn semantics、accepted 状态、playback 活跃状态与队列帧数
    - 下一步上板验证：
      - 服务端 TTS 堵塞时应看到 audio wait timeout，而不是误导性的 `xiaozhi tts sentence_start`
      - 服务端 TTS 恢复后应看到 `audio.out.meta` / binary PCM / playback start，且不触发 timeout
  - newest landed runtime bug-fix slice:
    - 2026-04-25 板端日志显示 `server_endpoint` 已接受本轮输入并把
      `input_state` 推进到 `committed` 后，本地 post-roll 仍发送兼容
      `audio.in.commit`
    - 服务端拒绝该重复 commit 并返回 `turn_not_ready` /
      `audio.in.commit is accepted only while the session is active`，触发
      `asr_error`、`error_recovering` 与同一 server session 内重复 reopen ASR
      loop
    - Step 5.521 现在在 accepted + `input_state=committed` 时关闭本地 round，
      并在 `river_xiaozhi_send_audio_commit_internal()` 增加 committed 语义缓存
      保险，避免再发 wire-level `audio.in.commit`
    - 当前“无声音”仍需单独追踪：日志只有 `response.start` / `response.chunk`
      文本，没有 `audio.out.meta` 或 binary PCM，设备端没有可播放下行输入
    - 下一步上板验证：
      - 不再出现 `turn_not_ready` / `error_recovering` loop
      - 若仍无声，确认服务端是否实际下发 `audio.out.meta` 与 binary 音频
  - newest landed runtime-ownership slice:
    - 继续治理 XiaoZhi WS receive path，把 legacy 文本消息处理从 realtime message handler include 中拆出：
      - `river_xiaozhi_ws_message_handlers.inc` 从 `956` 行降到 `707` 行
      - 新增 `components/river_cloud/river_xiaozhi_ws_legacy_handlers.inc`，承载约 `250` 行 legacy handler
    - legacy handler 模块集中承载 `stt` / `llm` / `tts` / `system` / `alert` / `mcp`，message handler include 更聚焦 realtime receive、text/binary dispatch 与 ws callback
    - 仍不新增 public header；legacy include 保留在原 WS translation unit 内，避免 receive truth/helper 外扩
    - 下一步继续聚焦：
      - 继续压窄 `river_xiaozhi_ws_bootstrap_discovery.inc` 或 `river_cloud_xiaozhi_playback_terminal_ack.inc` 的低频职责边界
  - newest landed runtime-ownership slice:
    - 继续治理 XiaoZhi playback downlink worker，把 abort / terminal policy 从高频 worker shell 中拆出：
      - `river_cloud_xiaozhi_playback_downlink_worker.inc` 从 `358` 行降到 `178` 行
      - 新增 `components/river_cloud/river_cloud_xiaozhi_playback_abort_policy.inc`，承载约 `181` 行 abort / terminal policy
    - abort policy 模块集中承载 terminal ACK include chain、pending stop、backend refresh policy、terminal playback policy、abort cause/reason mapping 与 abort-for-cause
    - worker shell 现在只保留 current segment start hook、abort policy include、downlink cycle include、decoder/audio event、stereo expand 与 worker task/start
    - 仍不新增 public header；policy include 保留在原 playback runtime translation unit 内，避免 truth 写面外扩
    - 下一步继续聚焦：
      - 继续压窄 `river_cloud_xiaozhi_playback_terminal_ack.inc` 的 ACK/report ownership，或评估 WS bootstrap/open 是否可继续拆成 narrow internal helper
  - newest landed runtime-ownership slice:
    - 继续治理 XiaoZhi WS public API façade，把剩余 public wrappers 按职责拆成更窄 include：
      - `river_xiaozhi_ws_public_api.inc` 从 `618` 行降到 `4` 行，只保留 include 顺序
      - 新增 `components/river_cloud/river_xiaozhi_ws_config_api.inc`，承载约 `308` 行 init/config/getter wrapper
      - 新增 `components/river_cloud/river_xiaozhi_ws_session_api.inc`，承载约 `306` 行 bootstrap/open/close/poll wrapper
    - public API façade 现在只串联 config/session/send/status 四个私有 include；仍不新增 public header，不扩大 WS truth/helper 写面
    - 下一步继续聚焦：
      - 转向 playback worker shell 内 terminal ACK / abort ownership，或评估 WS bootstrap/open 是否可进一步拆出 narrow internal helper
  - newest landed runtime-ownership slice:
    - 继续治理 XiaoZhi WS public API include，把 send wrappers 从 session/config/open/poll API 中拆出：
      - `river_xiaozhi_ws_public_api.inc` 从 `719` 行降到 `618` 行
      - 新增 `components/river_cloud/river_xiaozhi_ws_send_api.inc`，承载约 `101` 行 send wrapper
    - send API 模块集中承载 listen/abort/audio.out/audio/MCP public send wrappers；status dump 继续保持独立 include
    - 仍不新增 public header；send wrappers 继续在原 WS translation unit 内消费 private/static send helper，避免 transport truth 写面外扩
    - 下一步继续聚焦：
      - 评估 `river_xiaozhi_ws_public_api.inc` 中 bootstrap/open/session ownership 是否还需继续拆窄，或转向 playback worker shell 的 terminal ACK / abort ownership
  - newest landed runtime-ownership slice:
    - 继续治理 XiaoZhi WS public API include，把低频 status dump 从 session/open/send wrapper 集合中拆出：
      - `river_xiaozhi_ws_public_api.inc` 从 `952` 行降到 `719` 行
      - 新增 `components/river_cloud/river_xiaozhi_ws_status_dump.inc`，承载约 `234` 行 status dump formatter
    - status dump 模块集中承载 send queue/bootstrap/timing/preview/playback/discovery/activation 诊断输出
    - 仍不新增 public header；status dump 继续在原 WS translation unit 内读取 private/static truth helper，避免诊断读取面外扩
    - 下一步继续聚焦：
      - 将 `river_xiaozhi_ws_public_api.inc` 继续拆成 session/config wrappers 与 send wrappers，或治理 playback worker shell 内 terminal ACK / abort ownership
  - newest landed runtime-ownership slice:
    - 继续治理 playback downlink worker 的高频 loop 与恢复策略边界：
      - `river_cloud_xiaozhi_playback_downlink_worker.inc` 从 `1277` 行降到 `358` 行
      - 新增 `components/river_cloud/river_cloud_xiaozhi_playback_rebuffer_recovery.inc`，
        承载约 `538` 行 rebuffer / recovery helper
      - 新增 `components/river_cloud/river_cloud_xiaozhi_playback_downlink_cycle.inc`，
        承载约 `383` 行 playback prepare / cycle helper
    - rebuffer recovery 模块集中承载 start/resume/low-water/segment-gap 门限、upstream
      starvation、managed rebuffer、write_failed starvation preference 与 recovery fallback
    - downlink cycle 模块集中承载 playback start/compact retry、prepare wait-kind/status、
      frame acquire/write outcome、cycle result 与 wait-plan 投影
    - worker shell 保留 terminal ACK include、abort policy、decoder/audio event、stereo
      expand 与 worker task/start；仍不新增 public header，不扩大 truth 写面
    - 下一步继续聚焦：
      - `river_xiaozhi_ws_public_api.inc` 的 status dump/session API 继续拆窄，或把
        playback abort/terminal ACK shell 再拆成更小 ownership 模块
  - newest landed runtime-ownership slice:
    - 继续压缩 playback runtime 主文件并治理 view/result schema 边界：
      - `river_cloud_xiaozhi_playback_runtime.c` 从 `2550` 行降到 `2063` 行
      - 新增 `components/river_cloud/river_cloud_xiaozhi_playback_runtime_views.inc`，
        承载约 `488` 行 runtime view / typed result helper
    - 新模块集中承载：
      - current segment accessor、supply kind 计算、downlink wait/outcome name helper
      - playback truth/observe/diag/gap/gate/recovery view structs 与 capture helper
      - write_failed followup、downlink prepare/acquire/write/cycle/wait typed result
    - 仍不新增 public header；view include 保留在原 runtime translation unit 内，避免
      truth 写面外扩
    - 下一步继续聚焦：
      - `river_xiaozhi_ws_public_api.inc` 的 status dump/session API 继续拆窄，或把
        `river_cloud_xiaozhi_playback_downlink_worker.inc` 继续拆成 rebuffer recovery
        与 downlink cycle 两个更小 ownership 模块
  - newest landed runtime-ownership slice:
    - 按“3k 行仍过大”的治理目标继续压缩 XiaoZhi WS 主文件：
      - `river_xiaozhi_ws.c` 从 `2965` 行降到 `2010` 行
      - 新增 `components/river_cloud/river_xiaozhi_ws_message_handlers.inc`，承载约
        `956` 行 receive message ownership
    - 新模块集中承载：
      - realtime `session.update/audio.out.meta/response.start/response.chunk/session.end/error`
        handlers
      - legacy `stt/llm/tts/system/alert/mcp` text handlers
      - binary/text frame 判定与 fragmented `CONTINUATION` payload fallback
    - `river_xiaozhi_ws_preview_handlers.inc` 继续在 message handler 模块内 include，
      让 preview truth/throttle/event emit 保持在 receive-path 局部 ownership 内
    - 下一步继续聚焦：
      - 继续把 `river_xiaozhi_ws_public_api.inc` 的 status dump / session API 分裂成
        更窄 include，或评估 playback runtime 的 segment queue / truth view 是否可
        提升为独立 private module
  - newest landed runtime-ownership slice:
    - 按“主文件低于 3k 行”的治理目标继续拆分：
      - `river_xiaozhi_ws.c` 从 `3713` 行降到 `2965` 行
      - `river_cloud_xiaozhi_playback_runtime.c` 从 `3826` 行降到 `2550` 行
    - 新增 `components/river_cloud/river_xiaozhi_ws_bootstrap_discovery.inc`：
      - 集中承载 HTTP URL 解析、OTA bootstrap POST、discovery GET、bootstrap /
        discovery JSON parse
      - 主 WS 文件更聚焦 realtime transport、send queue、negotiation/cache 状态与
        dispatch
    - 新增 `components/river_cloud/river_cloud_xiaozhi_playback_downlink_worker.inc`：
      - 集中承载 downlink start gate、segment gap/rebuffer、cycle plan、decoder/audio
        event、worker task 与 terminal ACK include
      - 主 playback runtime 更聚焦 backend/source/truth view、lineage、terminal wait、
        policy/status include
    - 下一步继续聚焦：
      - 把已稳定的 `.inc` ownership 边界继续缩窄 internal interface；优先评估
        bootstrap/discovery 和 downlink worker 是否可升级为独立 `.c` + narrow
        private header
  - newest landed runtime-ownership slice:
    - 继续对两个超大 XiaoZhi runtime 文件做第二轮低风险 include-split：
      - `river_xiaozhi_ws.c` 从 `4664` 行降到 `3713` 行
      - `river_cloud_xiaozhi_playback_runtime.c` 从 `4364` 行降到 `3826` 行
    - 新增 `components/river_cloud/river_xiaozhi_ws_public_api.inc`：
      - 集中承载 public API、session open/close、send wrappers 与 status dump
      - 主 transport 文件更聚焦 send queue、protocol parse/dispatch 与 realtime
        receive handlers
    - 新增 `components/river_cloud/river_cloud_xiaozhi_playback_public_policy.inc`：
      - 集中承载 playback status/snapshot export、capture/session policy reset、
        backend pause/hold/rebuffer entry
      - 主 playback runtime 更聚焦 downlink ring、start gate、segment/rebuffer
        worker 与 terminal ACK include
    - 下一步继续聚焦：
      - 在这些 include 边界内继续把 status/export 变成更窄的 typed view；
        若静态 helper 依赖继续下降，再把稳定边界提升为独立 `.c`
  - newest landed runtime-ownership slice:
    - 直接对两个超大 XiaoZhi runtime 文件做低风险 include-split：
      - `river_xiaozhi_ws.c` 从 `4971` 行降到 `4664` 行
      - `river_cloud_xiaozhi_playback_runtime.c` 从 `5215` 行降到 `4364` 行
    - 新增 `components/river_cloud/river_xiaozhi_ws_preview_handlers.inc`：
      - 集中承载 `input.speech.start` / `input.preview` / `input.endpoint`
        receive-path handlers
      - high-frequency preview throttle、preview truth 更新与 event emit 保持在
        同一局部模块
    - 新增 `components/river_cloud/river_cloud_xiaozhi_playback_lineage.inc`：
      - 集中承载 playback lineage canonical truth helper，避免
        response/meta/started/mark/cleared/completed 阶段事实散回主 runtime
    - 新增 `components/river_cloud/river_cloud_xiaozhi_playback_terminal_ack.inc`：
      - 集中承载 fully-heard context、completed readiness、ACK progress 与
        terminal clear/finalize 路径
    - 下一步继续聚焦：
      - 在新拆出的 `.inc` 内继续把 preview truth 与 playback lineage truth
        进一步 typed reducer 化；等静态边界稳定后再评估是否提升为独立 `.c`
        translation unit
  - newest landed runtime-ownership slice:
    - 白盒审视当前实时交互链路后，先优化一个确定性高频热点：
      - `river_xiaozhi_ws.c` 的 `input.preview` 处理位于 WebSocket receive/dispatch
        路径
      - preview partial 可能高频到达，原实现每条都同步格式化完整
        `text/stable_prefix/timing` 并 `RIVER_LOGI`
      - 日志 IO 与字符串格式化会抢占同一条实时消息处理路径，增加
        preview/accept/response.start 时延抖动
    - 新增 `RIVER_XIAOZHI_PREVIEW_LOG_INTERVAL_MS = 250`：
      - 首条 preview、`stable_prefix` 变化、final preview 仍立即记录
      - 其余 partial preview 只更新状态并正常 emit event，不再每条同步打印
    - `river xiaozhi status` 的 `preview_state` 行新增
      `preview_updates` 与 `preview_logs=emitted/suppressed`
    - 下一步继续聚焦：
      - 继续审视 receive path 上其他高频同步日志/字符串格式化点，优先保持
        transport receive、preview dispatch、accept latch 三段低抖动
  - newest landed runtime-ownership slice:
    - 在 Step 5.507 已建立 playback lineage truth 后，继续白盒收口剩余分散读写：
      - last segment observed / fully heard context 仍主要从 terminal truth 读取
      - started/cleared/completed reported flags 仍由多个调用点直接赋值
      - status dump 仍混合读取 meta truth、terminal truth 与 lineage truth
    - `river_cloud_xiaozhi_playback_lineage_truth_t` 新增
      `last_segment_context` 与 `fully_heard_context`，让 last/meta/started/mark/
      cleared/completed/heard 链路都挂到同一 lineage truth 上
    - `playback_completed_ready()`、completed wait-kind、cleared/completed ACK
      发送上下文、status dump 均改为优先读取 lineage truth
    - `playback_note_meta()` 仍保留 `meta_truth->current_context` 作为 legacy
      mirror，但 response/playback change detection、segment queue population、
      wait/last/heard terminal context 派生与 gap/status 诊断都改为读取 lineage
      context
    - 新增 `river_cloud_xiaozhi_sync_playback_terminal_report_flags()`，让 legacy
      terminal flags 由 lineage truth 派生，避免业务逻辑继续手写
      started/cleared/completed reported 布尔组合
    - 下一步继续聚焦：
      - 把 remaining legacy terminal context 字段降级为兼容镜像，逐步把 snapshot/export
        也切到 lineage view
  - newest landed runtime-ownership slice:
    - 本地白盒重构继续收口两个确定性关键问题：
      - `write_failed` recovery followup 仍通过 `inline_success` / `managed_rebuffer`
        布尔组合表达，managed rebuffer 执行结果没有回填到 downlink write result
      - playback response/meta/started/mark/cleared/completed 分散在 transport、
        meta truth、segment queue 与 terminal truth 中，状态串联需要跨结构推断
    - 新增 typed `river_cloud_xiaozhi_write_failed_followup_kind_t`，把恢复后续动作
      收敛为 `inline_replay_consumed` / `managed_rebuffer` / `none`，并让
      write result 持有 `recovery_followup_kind`、`recovery_status` 与
      `recovery_path`
    - 新增 `river_cloud_xiaozhi_playback_lineage_truth_t` 与
      `river_cloud_xiaozhi_playback_lineage_stage_t`，把 response.start、
      audio.out.meta、started ack、mark ack、cleared ack、completed ack 以及
      local terminal fallback 串到单一 lineage truth
    - `river xiaozhi status` 新增 `xiaozhi playback_lineage ...` 行，可直接查看
      stage 与对应 response/segment lineage
    - 下一步继续聚焦：
      - 把 lineage truth 进一步接入 snapshot/export，减少 status-only 诊断依赖；
        同时根据板端 write_failed recovery status 判断是否需要 cooldown 或退避策略
  - newest landed runtime-ownership slice:
    - 白盒审视发现一个确定性关键问题：downlink cycle 已有
      `wait/outcome/cycle_status`，但 `prepare_downlink_playback()` 仍用裸
      `bool` 把 rebuffer wait、start threshold、backend recovering 和
      playback start failure 都折叠成 generic `playback_not_ready`，其中
      start failure 还会丢失原始 status
    - 新增 `river_cloud_xiaozhi_downlink_playback_prepare_result_t`，让 playback
      prepare 阶段返回 concrete `wait_kind`、source `river_status_t status`
      和 `ready`
    - 扩展 downlink wait reason：`rebuffer_wait` / `stop_pending` /
      `paused_resume_wait` / `backend_recovering` / `start_threshold` /
      `playback_start_failed`
    - `river_cloud_xiaozhi_downlink_cycle_plan_t` 现在持有 prepare status，
      cycle processor 会把 start failure status 投影到 `cycle_status`
    - 这一步把：
      - generic `playback_not_ready` with `cycle_status=0`
      收口成：
      - concrete prepare wait reason plus start-failure status truth
    - 下一步继续聚焦：
      - 根据真实板端的 `wait=<prepare-reason>` 与 `cycle_status`，判断是否需要
        对 `playback_start_failed` / `backend_recovering` 加 cooldown 或进一步统一
        detached fresh-start 与 attached resume 的 start policy
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续把 XiaoZhi downlink worker 的 frame-consumed /
      write-failed / aborted 分支收口成 cycle outcome truth，避免 status 侧再从
      多个布尔字段推断
    - 新增 `river_cloud_xiaozhi_downlink_cycle_outcome_t`，覆盖 inactive /
      not_ready / acquire_miss / write_ok / write_recovered / write_failed /
      aborted / step_policy
    - `river_cloud_xiaozhi_downlink_task_cycle_result_t` 现在显式记录
      `outcome`，写入分支通过 helper 把 `frame_consumed` / `write_failed` /
      `aborted` 归一为 cycle outcome
    - downlink runtime truth 新增 `last_cycle_outcome`
    - `river xiaozhi status` 的 downlink 行现在输出
      `wait=<kind>/<delay>ms outcome=<name> cycle_status=<status>`
    - 这一步把：
      - source status is visible but branch outcome still lives in sub-result booleans
      收口成：
      - cycle outcome is owned by the cycle result and published as runtime truth
    - 下一步继续聚焦：
      - 继续检查 downlink worker loop 与 playback recovery 链路是否还有裸策略投影，
        尽量让 wait/status/recovery 诊断都来自 typed result
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续把 XiaoZhi downlink acquire/write 子结果中的 status/error
      细节纳入 typed result，让 acquire miss 与 write failed 不再只携带布尔结果
    - `river_cloud_xiaozhi_downlink_frame_acquire_result_t` 现在记录
      `river_status_t status`，ring read 失败时保留原始
      `river_audio_frame_ring_read(...)` 返回值
    - `river_cloud_xiaozhi_downlink_frame_write_result_t` 现在记录
      `river_status_t status`，frame oversize 和 playback write failure 会保留明确错误码
    - cycle result / wait plan 现在把子结果 status 投影到 downlink runtime truth 的
      `last_cycle_status`
    - `river xiaozhi status` 的 downlink 行现在输出
      `wait=<kind>/<delay>ms cycle_status=<status>`
    - 这一步把：
      - typed sub-result only says acquired/write_failed
      收口成：
      - typed sub-result owns source status and publishes it as cycle diagnostic truth
    - 下一步继续聚焦：
      - 继续检查 downlink worker 的 cycle/wait truth 是否还缺少恢复路径、
        frame-consumed/aborted 等结果投影，逐步完成 downlink ownership 边界
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续细分 XiaoZhi downlink ready-plan 的 not-ready 原因，
      让 wait reason 不再只有粗粒度 `not_ready`
    - 扩展 `river_cloud_xiaozhi_downlink_wait_kind_t`：
      - `starved`
      - `segment_gap`
      - `empty`
      - `playback_not_ready`
    - `river_cloud_xiaozhi_downlink_cycle_plan_t` 现在持有 `wait_kind`，
      ready-plan 在各个 not-ready 分支中显式标注 blocked reason
    - wait plan 现在直接消费 cycle plan 的 ready-block reason，status 中的
      `wait=<kind>/<delay>ms` 可以区分上游断供、segment gap、空队列和
      playback start/prep gate 未满足
    - 这一步把：
      - generic `not_ready`
      收口成：
      - ready-plan owns concrete blocked reason before wait-plan projection
    - 下一步继续聚焦：
      - 继续把 acquire/write 子结果中的 status/error 细节纳入 typed result，
        让 acquire miss 和 write failed 也能携带更明确的 failure reason
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续让 XiaoZhi downlink cycle sub-results 服务诊断，把 worker
      wait plan 的原因锁存到 downlink runtime truth 并在 status 中输出
    - 新增：
      - `river_cloud_xiaozhi_downlink_wait_kind_t`
      - `last_wait_kind` / `last_wait_delay_ms` downlink runtime truth
    - wait plan 现在根据 cycle result 的子结果区分 inactive / not_ready /
      acquire_miss / write_failed / step_policy
    - `river xiaozhi status` downlink 行现在输出 `wait=<kind>/<delay>ms`
    - 这一步把：
      - wait plan 只表达 sleep/no-sleep 与 delay duration
      收口成：
      - typed wait reason consumes ready/acquire/write sub-results and becomes runtime-visible truth
    - 下一步继续聚焦：
      - 继续检查 downlink ready plan 内部的 not-ready 分支，逐步把 starved /
        segment-gap / empty / playback-not-ready 等原因显式化
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续类型化 XiaoZhi downlink worker sleep/idle 收尾策略，把
      cycle result 到 RTOS delay 的投影收口成 typed wait plan
    - 新增：
      - `river_cloud_xiaozhi_downlink_task_wait_plan_t`
      - `river_cloud_xiaozhi_build_downlink_task_wait_plan()`
    - `river_cloud_xiaozhi_finish_downlink_task_cycle()` 现在只消费 wait plan，
      不再内联把 cycle step result 分支直接映射到 `rtos_time_delay_ms(...)`
    - 这一步把：
      - finish helper 内直接判断 `SLEEP_IDLE` / `SLEEP_POLL` 并执行 delay
      收口成：
      - typed wait plan owns sleep/no-sleep and delay duration before the RTOS effect
    - 下一步继续聚焦：
      - 继续检查 downlink cycle 中 ready/acquire/write 子结果是否还能服务诊断或
        recovery policy，避免 typed result 只停留在结构包装层
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续类型化 XiaoZhi downlink worker cycle，把 ready / acquire / write
      三段结果组合成 cycle-level typed result
    - 新增：
      - `river_cloud_xiaozhi_downlink_task_cycle_result_t`
    - `river_cloud_xiaozhi_process_downlink_task_cycle()` 现在返回完整 cycle result，
      显式持有 cycle plan、acquire result、write result 和最终 worker step policy
    - `river_cloud_xiaozhi_downlink_task()` 现在先接收 cycle result，再交给
      `river_cloud_xiaozhi_finish_downlink_task_cycle()` 处理 sleep policy
    - 这一步把：
      - ready / acquire / write 分支在 cycle 函数中直接投影成裸 step result
      收口成：
      - cycle-level typed result owns sub-step observations and final worker policy
    - 下一步继续聚焦：
      - 压缩 `finish_downlink_task_cycle()` 与 worker loop 的剩余裸 step-result 投影，
        并继续把 downlink sleep/idle policy 收口到 typed worker result
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续类型化 XiaoZhi downlink current-frame write step，把
      `river_cloud_xiaozhi_write_current_downlink_frame_step()` 的裸 step-result
      返回升级为 typed frame-write result
    - 新增：
      - `river_cloud_xiaozhi_downlink_frame_write_result_t`
    - `river_cloud_xiaozhi_write_current_downlink_frame_step()` 现在显式返回
      `step_result`、`frame_consumed`、`write_failed` 和 `aborted`
    - `river_cloud_xiaozhi_process_downlink_task_cycle()` 先消费 typed write result，
      再只在 worker cycle 边界投影 step result
    - 这一步把：
      - success / write-failed / abort 分支直接折叠成裸 worker step result
      收口成：
      - typed frame-write result owns branch outcome, frame consumption and worker step policy
    - 下一步继续聚焦：
      - 把 ready/acquire/write 三段组合成 cycle-level typed result，继续压缩
        `process_downlink_task_cycle()` 中的隐式控制流
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续类型化 XiaoZhi downlink write-failed 恢复结果，把 handler 的
      `bool` 返回升级为 step-result view
    - 新增：
      - `river_cloud_xiaozhi_write_failed_recovery_result_t`
    - `river_cloud_xiaozhi_handle_playback_write_failed()` 现在显式返回
      `frame_consumed` 和 `step_result`
    - `river_cloud_xiaozhi_write_current_downlink_frame_step()` 直接消费 recovery result，
      不再用 bool + 三元表达式推导下一步动作
    - 这一步把：
      - bool 表达“inline replay 是否成功并已消费帧”
      收口成：
      - typed recovery result owns frame consumption and worker step policy
    - 下一步继续聚焦：
      - write step 自身的结果结构化，以及 pending-stop / ACK / segment-start
        后效应继续向 downlink runtime owner helper 聚合
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续收口 XiaoZhi downlink write step 的成功路径，把成功写入后的
      frame consume / segment start / ACK progress / pending-stop check 封装为单一 helper
    - 新增：
      - `river_cloud_xiaozhi_finish_successful_downlink_frame_write()`
    - `river_cloud_xiaozhi_write_current_downlink_frame_step()` 现在只保留 write view、
      oversize abort、write effect、write-failed recovery 委托和 successful-write finish 委托
    - 这一步把：
      - write step 内散落 consume/current segment/ACK/pending-stop 副作用
      收口成：
      - successful-write finish helper owns post-write effects
    - 下一步继续聚焦：
      - downlink write abort / write-failed branch 的 step-result 继续显式化，逐步让
        `process_downlink_task_cycle()` 只组合 typed plan/result
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续拆分 XiaoZhi downlink worker cycle，把 current-frame acquire
      从裸 `bool` 结果升级为 typed result
    - 新增：
      - `river_cloud_xiaozhi_downlink_frame_acquire_result_t`
    - `river_cloud_xiaozhi_acquire_current_downlink_frame()` 现在显式返回
      `acquired` 和 `step_result`
    - `river_cloud_xiaozhi_process_downlink_task_cycle()` 现在消费 acquire result
      内的下一步动作，不再在调用方硬编码 acquire 失败为 `SLEEP_POLL`
    - 这一步把：
      - ready plan typed，但 acquire 仍是裸 bool + 调用方补 sleep policy
      收口成：
      - ready/acquire/write 三段都具备 typed step-result 边界
    - 下一步继续聚焦：
      - downlink write step result 的成功/失败/abort 分支进一步类型化，为后续
        worker loop 简化和 rebuffer 策略回收做准备
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续收缩 XiaoZhi playback `write_failed` managed rebuffer
      follow-up，把 handler 尾部的 start/log/execute 三段副作用封装成单一 executor
    - 新增：
      - `river_cloud_xiaozhi_execute_write_failed_managed_rebuffer_followup()`
    - `river_cloud_xiaozhi_handle_playback_write_failed()` 现在只编排 recovery view、
      starvation watch clear、首条 failure log、inline recover 和 managed follow-up 委托
    - 这一步把：
      - handler 内直接串联 start request、log request、execute request
      收口成：
      - follow-up executor owns managed rebuffer effects
    - 下一步继续聚焦：
      - downlink task ready/acquire/write cycle 的 plan/result 拆分，继续压缩 worker loop
        中的隐式副作用
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续收口 XiaoZhi playback inline recover replay 写入路径，让恢复重放
      复用 current-frame write view/effect
    - `river_cloud_xiaozhi_try_write_failed_inline_recover()` 现在接收
      `river_cloud_xiaozhi_downlink_write_view_t`，不再单独传递
      `mono_bytes/stereo_bytes`
    - inline recover replay 的二次写入不再直接调用
      `river_playback_service_write(...)`，而是复用：
      - `river_cloud_xiaozhi_write_current_downlink_frame_audio()`
    - 这一步把：
      - normal write 与 inline recover replay 各自维护一份 service-write 调用
      收口成：
      - current-frame playback write has a single effect helper
    - 下一步继续聚焦：
      - 把 managed rebuffer follow-up 的 request/execute 进一步拆成 typed plan/result，
        并继续缩小 `handle_playback_write_failed()` 的编排体积
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续拆分 XiaoZhi playback `write_failed` 恢复链，把故障恢复视图
      从 handler 的副作用编排中抽出
    - 新增：
      - `river_cloud_xiaozhi_write_failed_recovery_view_t`
      - `river_cloud_xiaozhi_capture_write_failed_recovery_view()`
      - `river_cloud_xiaozhi_log_write_failed_recovery_view()`
    - `river_cloud_xiaozhi_handle_playback_write_failed()` 现在接收上一阶段的
      `river_cloud_xiaozhi_downlink_write_view_t`，并通过 typed recovery view
      统一持有 recovery plan / recover reason / recovery path / timestamp / frame size
    - 这一步把：
      - handler 内直接推导 recover reason、拼首条 failure log、保存 recovery path
      收口成：
      - typed recovery view owns write-failed query/log projection before effects
    - 下一步继续聚焦：
      - 把 inline recover replay 的再次 write 也改为消费统一 write view/effect，
        减少重复 `river_playback_service_write(...)` 入口
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续重建 XiaoZhi downlink/playback 写入边界，把 current-frame
      写入前的派生视图和实际 playback write 副作用拆开
    - 新增：
      - `river_cloud_xiaozhi_downlink_write_view_t`
      - `river_cloud_xiaozhi_capture_current_downlink_write_view()`
      - `river_cloud_xiaozhi_write_current_downlink_frame_audio()`
    - `river_cloud_xiaozhi_write_current_downlink_frame_step()` 现在先捕获
      `mono_bytes/stereo_bytes/frame_too_large` typed write view，再执行单一
      playback write effect helper
    - 这一步把：
      - acquire/write/rebuffer 巨函数内混合 query、validation、format expansion
        和 playback-service write
      收口成：
      - typed write view plus isolated playback write effect
    - 下一步继续聚焦：
      - 进一步拆分 `river_cloud_xiaozhi_handle_playback_write_failed(...)` 的
        query/plan/effect 边界
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续重建 XiaoZhi downlink/playback 边界，把 downlink retry-frame
      的 pending/keep/consume 生命周期收口到 owner helper
    - 新增：
      - `river_cloud_xiaozhi_downlink_retry_frame_pending()`
      - `river_cloud_xiaozhi_keep_current_downlink_frame_for_retry()`
      - `river_cloud_xiaozhi_consume_current_downlink_frame()`
    - queued-frame 统计、playback work 判断、managed rebuffer、inline recover、
      正常写入成功和 current-frame acquire 统一消费 retry-frame helper
    - 这一步把：
      - multiple playback paths directly read/write `retry_valid`
      收口成：
      - owner helpers define retry-frame lifecycle transitions
    - 下一步继续聚焦：
      - downlink task acquire/write/rebuffer decision 的 query/effect 拆分
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续重建 XiaoZhi downlink/playback 边界，把 downlink ring 的初始化、
      reset、latest-frame write/drop 和 supply timestamp 更新收口到 owner helper
    - 新增：
      - `river_cloud_xiaozhi_note_downlink_supply()`
      - `river_cloud_xiaozhi_reset_downlink_ring_runtime()`
      - `river_cloud_xiaozhi_ensure_downlink_ring()`
      - `river_cloud_xiaozhi_write_downlink_frame_latest()`
    - audio-event 主流程不再内联 downlink ring overflow 丢帧与 last-supply 更新策略
    - reset-downlink 和 pending-stop drop 路径复用统一 reset helper
    - 这一步把：
      - direct downlink ring/storage/drop/supply mutation in playback main flow
      收口成：
      - downlink ring owner helpers
    - 下一步继续聚焦：
      - 把 downlink task acquire/write/rebuffer decision 继续拆成 query/view 与 effect
        两层，减少 playback runtime 巨函数中的隐式副作用
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续把 XiaoZhi playback tail / no-ref reopen 真相从散落裸字段收口到
      既有 playback runtime/gate truth
    - 新增到 playback runtime truth：
      - `tts_stop_deadline_ms`
    - 新增到 playback gate truth：
      - `no_ref_reopen_guard_deadline_ms`
      - `no_ref_reopen_silence_frames`
      - `no_ref_reopen_rearm`
    - `river_cloud_context_t` 不再保留散落的：
      - `xiaozhi_tts_stop_deadline_ms`
      - `xiaozhi_no_ref_reopen_guard_deadline_ms`
      - `xiaozhi_no_ref_reopen_silence_frames`
      - `xiaozhi_no_ref_reopen_rearm`
    - playback stop arm/cancel/poll、playback reset、no-ref reopen readiness 和
      runtime status dump 现在统一消费 playback runtime/gate truth
    - 这一步把：
      - scattered playback tail and no-ref reopen fields
      收口成：
      - playback runtime truth + playback gate truth
    - 下一步继续聚焦：
      - 清理 downlink/playback 内部直接访问，建立更清晰的 query/view/export 边界
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续把 XiaoZhi session/window/local-close 真相从散落裸字段收口成显式
      typed truth
    - 新增：
      - `river_cloud_xiaozhi_session_window_truth_t`
    - `river_cloud_context_t` 不再保留散落的：
      - `xiaozhi_listening`
      - `xiaozhi_window_active`
      - `xiaozhi_listen_stop_pending`
      - `xiaozhi_local_close_pending`
      - `xiaozhi_window_deadline_ms`
      - `xiaozhi_local_close_deadline_ms`
    - listen-start / listen-stop completion、conversation window touch/close/abort、
      local-close defer/timeout、follow-up reopen gate 和 playback-side close/rebuffer
      guard 现在统一消费：
      - `g_river_cloud.xiaozhi_session_window_truth`
    - 这一步把：
      - scattered dialog/session window flags and deadlines
      收口成：
      - explicit session window truth
    - 下一步继续聚焦：
      - no-ref reopen / TTS tail deadline 这些仍与 playback 卡顿、follow-up reopen
        稳定性直接相关的剩余运行态
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续把 XiaoZhi preview transcript / endpoint candidate 真相从散落裸字段收口成显式
      typed truth
    - 新增：
      - `river_cloud_xiaozhi_preview_transcript_truth_t`
    - `river_cloud_context_t` 不再保留散落的：
      - `xiaozhi_preview_speech_started`
      - `xiaozhi_preview_endpoint_candidate`
      - `xiaozhi_preview_final`
      - `xiaozhi_preview_audio_offset_ms`
      - `xiaozhi_preview_id`
      - `xiaozhi_preview_text`
      - `xiaozhi_preview_stable_prefix`
      - `xiaozhi_preview_source`
      - `xiaozhi_preview_endpoint_reason`
    - preview observation、preview clear、runtime dump 和 interrupt hint 现在统一消费：
      - `g_river_cloud.xiaozhi_preview_transcript_truth`
    - 这一步把：
      - scattered preview transcript / endpoint candidate bag
      收口成：
      - explicit preview transcript truth
    - 下一步继续聚焦：
      - remaining session/window/local-close/no-ref reopen 运行态，判断是否继续收口
        为 typed truth 或转入 query/export 边界整理
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续把 XiaoZhi pending transcript 真相从散落裸字段收口成显式
      typed truth
    - 新增：
      - `river_cloud_xiaozhi_pending_transcript_truth_t`
    - `river_cloud_context_t` 不再保留散落的：
      - `xiaozhi_pending_text_valid`
      - `xiaozhi_pending_text_finalized`
      - `xiaozhi_pending_text`
    - STT observation、pending clear、finalize readiness、accepted-turn final emit、
      post-stop result policy、local-close defer 和 runtime dump 现在统一消费：
      - `g_river_cloud.xiaozhi_pending_transcript_truth`
    - 这一步把：
      - scattered pending text validity/finalization/text bag
      收口成：
      - explicit pending transcript truth
    - 下一步继续聚焦：
      - preview transcript / endpoint candidate 这批服务端预览与端点观测状态
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续把 XiaoZhi endpoint soft-close 真相从散落裸字段收口成显式
      typed truth
    - 新增：
      - `river_cloud_xiaozhi_endpoint_soft_close_truth_t`
    - `river_cloud_context_t` 不再保留散落的：
      - `xiaozhi_endpoint_soft_close_pending`
      - `xiaozhi_endpoint_soft_close_deadline_ms`
      - `xiaozhi_endpoint_soft_close_reason`
    - endpoint soft-close pending / remaining / reason / clear / arm / timeout-poll
      现在统一消费：
      - `g_river_cloud.xiaozhi_endpoint_soft_close_truth`
    - 这一步把：
      - scattered endpoint defer pending/deadline/reason bag
      收口成：
      - explicit endpoint soft-close truth
    - 下一步继续聚焦：
      - pending transcript / preview transcript 这批文本与端点观测状态，避免
        STT / preview / finalize 路径继续散读散写
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续把 XiaoZhi ASR round stats 真相从散落裸字段收口成显式
      typed truth
    - 新增：
      - `river_cloud_xiaozhi_asr_round_truth_t`
    - `river_cloud_context_t` 不再保留散落的：
      - `xiaozhi_asr_round_id`
      - `xiaozhi_asr_round_active`
      - `xiaozhi_asr_round_started_ms`
      - `xiaozhi_asr_round_first_packet_ms`
      - `xiaozhi_asr_round_pre_roll_frames`
      - `xiaozhi_asr_round_packets_sent`
      - `xiaozhi_asr_round_partial_count`
      - `xiaozhi_asr_round_final_count`
      - `xiaozhi_asr_round_partial_seen`
      - `xiaozhi_asr_round_final_seen`
      - `xiaozhi_asr_round_busy_base`
      - `xiaozhi_asr_round_fail_base`
      - `xiaozhi_asr_round_stale_drop_base`
      - `xiaozhi_asr_round_ring_drop_base`
      - `xiaozhi_asr_round_close_reason`
    - ASR result emitted、round begin / packet-sent / finish、local-close defer、
      follow-up reopen 和 IO status 诊断现在统一消费：
      - `g_river_cloud.xiaozhi_asr_round_truth`
    - 这一步把：
      - scattered ASR round timing/counter/reason bag
      收口成：
      - explicit ASR round truth
    - 下一步继续聚焦：
      - pending / preview / endpoint soft-close 这批与 endpoint 延迟、响应慢和
        transcript 可观测性直接相关的状态
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续把 XiaoZhi control queue 真相从散落裸字段收口成显式
      typed truth
    - 新增：
      - `river_cloud_xiaozhi_control_queue_truth_t`
    - `river_cloud_context_t` 不再保留散落的：
      - `xiaozhi_control_read_index`
      - `xiaozhi_control_write_index`
      - `xiaozhi_control_count`
      - `xiaozhi_control_high_watermark`
    - control queue 入队、出队和 IO 诊断现在统一消费：
      - `g_river_cloud.xiaozhi_control_queue_truth`
    - 这一步把：
      - scattered control queue counters bag
      收口成：
      - explicit control queue truth
    - 下一步继续聚焦：
      - ASR round stats / preview text / endpoint soft-close 这些仍与响应慢、
        端点延迟和可观测性直接相关的 cloud-owned 状态
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续把 XiaoZhi io/uplink runtime 真相从散落裸字段收口成显式
      typed truth
    - 新增：
      - `river_cloud_xiaozhi_uplink_runtime_truth_t`
    - `river_cloud_context_t` 不再保留散落的：
      - `xiaozhi_io_started`
      - `xiaozhi_open_speech_frames`
      - `xiaozhi_uplink_timestamp_ms`
      - `xiaozhi_uplink_ring_dropped`
      - `xiaozhi_uplink_busy_count`
      - `xiaozhi_uplink_fail_count`
      - `xiaozhi_uplink_stale_dropped`
      - `xiaozhi_uplink_busy_streak`
      - `xiaozhi_uplink_next_send_ms`
      - `xiaozhi_uplink_last_busy_log_ms`
      - `xiaozhi_uplink_accum_bytes`
      - `xiaozhi_uplink_retry_valid`
    - 这一步把：
      - scattered io/uplink backpressure bag
      收口成：
      - explicit uplink runtime truth
    - 下一步继续聚焦：
      - 评估 control queue / ASR round stats / preview text 这些剩余 cloud-owned
        状态是否继续收口成 typed truth，或者转向 typed query/export 边界
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续把 XiaoZhi transport/server audio format
      真相从散落裸字段收口成显式 typed truth
    - 新增：
      - `river_cloud_xiaozhi_server_audio_format_truth_t`
    - `river_cloud_context_t` 不再保留散落的：
      - `xiaozhi_server_sample_rate`
      - `xiaozhi_server_frame_duration_ms`
    - `adapter` / `session` / `playback_runtime` 里的：
      - default format init
      - server hello observation
      - decoder open success note
      - audio event sample-rate / frame-duration fallback
      现在都统一消费 transport-owned server-audio-format truth
    - 这一步把：
      - scattered server-format fallback bag
      收口成：
      - explicit server audio format truth
    - 下一步继续聚焦：
      - playback/downlink 范围已基本完成状态真相收口，继续评估是否转向
        `resource boundary + typed query/export`，或者收口 XiaoZhi io/uplink
        worker 生命周期，减少 dialog/runtime 对 cloud transport 执行细节的感知
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续把 XiaoZhi downlink worker / stream-format
      真相从散落裸字段收口成显式 typed truth
    - 新增：
      - `river_cloud_xiaozhi_downlink_stream_truth_t`
    - `river_cloud_context_t` 不再保留散落的：
      - `xiaozhi_downlink_started`
      - `xiaozhi_downlink_sample_rate`
      - `xiaozhi_downlink_frame_duration_ms`
    - `river_cloud_xiaozhi_playback_runtime.c` 里的：
      - frame-duration fallback
      - diag export 的 sample-rate / frame-duration / worker-started
      - backend playback start 参数拼装
      - audio-event format note
      - downlink worker start gate / started 标记
      现在都统一消费 downlink-owned stream truth
    - 这一步把：
      - scattered downlink worker/format bag
      收口成：
      - explicit downlink stream truth
    - 下一步继续聚焦：
      - 评估 `server_sample_rate/frame_duration_ms` 以及 decode-format fallback
        是否继续收口成 typed codec truth；如果不是，就把剩余 playback/downlink
        范围转向资源边界和更高层 dialog/playback query 收敛
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续把 XiaoZhi playback segment queue
      真相从散落裸字段收口成显式 typed truth
    - 新增：
      - `river_cloud_xiaozhi_playback_segment_queue_truth_t`
    - `river_cloud_context_t` 不再保留散落的：
      - `xiaozhi_playback_segment_head`
      - `xiaozhi_playback_segment_count`
      - `xiaozhi_playback_segments[...]`
    - `river_cloud_xiaozhi_playback_runtime.c` 里的：
      - supply-source queued segment capture
      - current-segment lookup / queue pop
      - playback status dump queued count
      - clear-meta reset / queue memset
      - note-meta existing-slot scan / tail append / queue-full 判定
      现在都统一消费 playback-owned segment-queue truth
    - 这一步把：
      - scattered playback segment queue bag
      收口成：
      - explicit playback segment queue truth
    - 下一步继续聚焦：
      - 把 downlink format / worker lifecycle / start-stop 运行态
        这批剩余 coarse ownership 继续推进成 typed truth / typed query，减少
        session/dialog/adapter 对播放供给细节和恢复执行细节的散读
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续把 XiaoZhi downlink/playback 的 start-gate /
      starvation-watch / retry / ring-overflow 真相从散落裸字段收口成显式 typed truth
    - 新增：
      - `river_cloud_xiaozhi_playback_gate_truth_t`
      - `river_cloud_xiaozhi_downlink_runtime_truth_t`
    - `river_cloud_context_t` 不再保留散落的：
      - `xiaozhi_playback_start_policy`
      - `xiaozhi_playback_start_frames`
      - `xiaozhi_playback_prefetch_frames`
      - `xiaozhi_playback_buffer_frames`
      - `xiaozhi_playback_start_cautious_history`
      - `xiaozhi_downlink_ring_dropped`
      - `xiaozhi_downlink_retry_valid`
      - `xiaozhi_downlink_starved_since_ms`
      - `xiaozhi_downlink_last_supply_ms`
    - `river_cloud_xiaozhi_playback_runtime.c` 里的：
      - start-gate store / refresh / current-gate / diag capture
      - queued-frames / has-work / buffer-budget 计算
      - rebuffer keep-retry / inline-recover / write-success 生命周期
      - reset-downlink / pending-stop drop / starvation-watch / ring-overflow
        路径
      现在都统一消费 playback-gate / downlink-runtime truth
    - 这一步把：
      - scattered gate/watch/retry thresholds bag
      收口成：
      - explicit playback gate truth
      - explicit downlink runtime truth
    - 下一步继续聚焦：
      - 把 downlink format / segment queue / worker lifecycle
        这批剩余 coarse ownership 继续推进成 typed truth / typed view，减少
        session/dialog 对云端播放供给细节的散读
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续把 XiaoZhi downlink/playback 的 execution / recovery
      真相从散落裸字段收口成显式 typed runtime truth
    - 新增：
      - `river_cloud_xiaozhi_playback_runtime_truth_t`
    - `river_cloud_context_t` 不再保留散落的：
      - `xiaozhi_playback_active`
      - `xiaozhi_playback_phase`
      - `xiaozhi_playback_rebuffer_cause`
      - `xiaozhi_playback_recovery_path`
      - `xiaozhi_playback_recovery_outcome`
      - `xiaozhi_playback_rebuffer_pending`
      - `xiaozhi_tts_stop_pending`
      - `xiaozhi_playback_rebuffer_count`
      - `xiaozhi_playback_rebuffer_streak`
    - `river_cloud_xiaozhi_playback_runtime.c` 里的：
      - phase/rebuffer/recovery accessors
      - start-gate / prefetch / diag / segment-gap hold capture
      - reset / started / stop / rebuffer / resume / pending-stop 生命周期
      - playback-start / prefetch / rebuffer / pending-stop / ack-progress 日志与判断
      不再混读写散落的 `g_river_cloud.xiaozhi_playback_*` 裸字段，而是统一消费
      playback-owned runtime truth
    - 这一步把：
      - scattered playback execution/recovery bag
      收口成：
      - explicit playback runtime truth
      - shared typed execution/recovery access boundary
    - 下一步继续聚焦：
      - 把 playback start-gate / starvation watch / retry / buffer-threshold
        这批剩余 coarse ownership 继续往 typed truth / typed view 推进，减少
        session/dialog/adapter 回读 downlink 细节
  - newest landed runtime-ownership slice:
    - `river_cloud` 继续把 XiaoZhi downlink/playback 的 meta / terminal /
      context 真相从散落裸字段收口成显式 typed truth
    - 新增：
      - `river_cloud_xiaozhi_playback_context_truth_t`
      - `river_cloud_xiaozhi_playback_meta_truth_t`
      - `river_cloud_xiaozhi_playback_terminal_truth_t`
    - `river_cloud_xiaozhi_playback_runtime.c` 里的：
      - note_meta
      - started/cleared/completed ack
      - last-segment / fully-heard context
      - segment-gap hold / prefetch / playback-start 日志
      不再混读写散落的 `g_river_cloud.xiaozhi_playback_*` 裸字段，而是统一消费
      playback-owned truth
    - 这一步把：
      - scattered playback terminal/meta/context bag
      收口成：
      - explicit playback-owned truth
      - shared typed context/meta/terminal access boundary
    - 下一步继续聚焦：
      - 把 playback rebuffer / phase / recovery 这批剩余 coarse state 继续收口成
        更明确的 playback/downlink truth，减少 session/dialog/adapter
        对恢复细节的散读
  - newest landed runtime-ownership slice:
    - `river_cloud` 先把 XiaoZhi `turn semantics` 从散落的
      `g_river_cloud.xiaozhi_*` 裸字段收口成显式子状态与窄视图
    - 新增显式 cloud-owned 真相与读取边界：
      - `river_cloud_xiaozhi_turn_semantics_state_t`
      - `river_cloud_xiaozhi_turn_semantics_view_t`
      - `river_cloud_xiaozhi_capture_turn_semantics_view(...)`
      - `river_cloud_xiaozhi_clear_session_id(...)`
    - `river_cloud_xiaozhi_fill_runtime_snapshot(...)`、
      `river_cloud_xiaozhi_dump_session_status(...)`、
      `river_cloud_xiaozhi_turn_accepted(...)`、
      `river_cloud_xiaozhi_note_semantic_fallback(...)`、
      `river_cloud_xiaozhi_commit_pending_text_finalization(...)`
      现在都统一消费 typed turn-semantics view
    - `river_cloud_xiaozhi_playback_runtime.c` 的 speaking 判定也不再直读
      `g_river_cloud.xiaozhi_output_state`
    - 这一步把：
      - scattered cloud turn-semantics bag
      收口成：
      - explicit XiaoZhi turn-semantics truth
      - typed turn-semantics export/view boundary
  - newest landed runtime-ownership slice:
    - `dialog runtime` 开始把外部模块从整份 `snapshot` 读取中解耦，先收口
      `river_voice_runtime_policy.c` 对 `dialog snapshot` 的直接策略解释
    - 新增窄化对外真相：
      - `river_dialog_runtime_voice_policy_view_t`
      - `river_dialog_runtime_get_voice_policy_view(...)`
      - `river_dialog_runtime_capture_voice_policy_view_locked(...)`
    - `river_voice_runtime_policy.c` 不再获取整份：
      - `river_dialog_runtime_snapshot_t`
      - `river_dialog_runtime_get_snapshot(...)`
      来判断 cloud playback engaged / quiet window /
      restart pending block
    - AEC / duplex policy 现在只消费 voice-policy 相关最小字段，而不是继续耦合
      dialog snapshot 的宽字段集合
    - 这一步把：
      - external snapshot-shaped policy dependency
      收口成：
      - typed dialog voice-policy view
  - newest landed runtime-ownership slice:
    - `dialog runtime` 继续把剩余 `control_facts -> snapshot` 导出边界收口成
      typed control export view，并把命名拉齐到
      `*_state_to_snapshot_locked(...)`
    - 新增：
      - `river_dialog_runtime_control_export_view_t`
      - `river_dialog_runtime_capture_control_export_view_locked(...)`
      - `river_dialog_runtime_apply_control_export_view_to_snapshot_locked(...)`
      - `river_dialog_runtime_export_control_state_to_snapshot_locked(...)`
    - 原先直接从 `control_facts` 抄字段到 snapshot 的：
      - `export_control_facts_to_snapshot_locked(...)`
      已退出
    - `wake_admission`、`tts_interrupt_requested`、cloud-event 控制刷新、
      local playback fallback 控制刷新、`reconcile_facts_locked(...)`
      初始化路径现在都统一走 shared control export view
    - 这一步把：
      - control snapshot field copy
      收口成：
      - typed control export projection
  - newest landed runtime-ownership slice:
    - `dialog runtime` 继续把 cloud-owned 的 `round/io/session/playback`
      snapshot 导出边界收口成 typed `cloud export view`
    - 新增：
      - `river_dialog_runtime_round_export_view_t`
      - `river_dialog_runtime_io_export_view_t`
      - `river_dialog_runtime_session_export_view_t`
      - `river_dialog_runtime_cloud_playback_export_view_t`
      - `river_dialog_runtime_cloud_export_view_t`
      - `river_dialog_runtime_capture_cloud_export_view_locked(...)`
      - `river_dialog_runtime_apply_cloud_export_view_to_snapshot_locked(...)`
      - `river_dialog_runtime_export_cloud_state_to_snapshot_locked(...)`
    - 原先分别直写 snapshot 的：
      - `export_round_facts_to_snapshot_locked(...)`
      - `export_io_facts_to_snapshot_locked(...)`
      - `export_session_facts_to_snapshot_locked(...)`
      - `export_playback_facts_to_snapshot_locked(...)`
      已退出
    - `import_cloud_snapshot_locked(...)` 与 cloud-event 的 `session_id`
      刷新路径现在统一复用 shared cloud export view
    - 这一步把：
      - split cloud snapshot field-copy helpers
      收口成：
      - typed cloud export projection
  - newest landed runtime-ownership slice:
    - `dialog runtime` 继续把 publish side 的 snapshot 导出边界收口成显式
      export view
    - 新增：
      - `river_dialog_runtime_publish_export_view_t`
      - `river_dialog_runtime_capture_publish_export_view_locked(...)`
      - `river_dialog_runtime_apply_publish_export_view_to_snapshot_locked(...)`
    - `river_dialog_runtime_export_publish_state_to_snapshot_locked(...)`
      不再直接从 `publish_state` 抄字段到 snapshot，而是统一走：
      - `capture + apply`
    - 当前 runtime-state snapshot 导出已两侧对齐：
      - playback/error -> explicit export view
      - publish state -> explicit export view
    - 这一步把：
      - publish snapshot field copy from runtime truth
      收口成：
      - typed publish export view
  - newest landed runtime-ownership slice:
    - `dialog runtime` 继续把 playback/error 的 snapshot 导出边界收口成显式
      export view
    - 新增：
      - `river_dialog_runtime_playback_error_export_view_t`
      - `river_dialog_runtime_capture_playback_error_export_view_locked(...)`
      - `river_dialog_runtime_apply_playback_error_export_view_to_snapshot_locked(...)`
    - 原先直接从 `error_truth` / `playback_truth` 抄字段到 snapshot 的导出 helper，
      已收口为：
      - `river_dialog_runtime_export_playback_error_state_to_snapshot_locked(...)`
    - `refresh_error_recovering_locked(...)`、
      `refresh_playback_locked(...)`、
      `export_runtime_state_to_snapshot_locked(...)`
      现在都统一走 shared export view
    - 这一步把：
      - snapshot field-by-field copy from runtime truth
      收口成：
      - typed playback/error export view
  - newest landed runtime-ownership slice:
    - `dialog runtime` 继续把剩余 `derived_facts` 混合状态拆成显式
      `error_truth` / `playback_truth`
    - 删除：
      - `river_dialog_runtime_derived_facts_t`
      - `g_river_dialog_runtime.derived_facts`
    - 新增显式 runtime-owned truth：
      - `river_dialog_runtime_error_truth_t`
      - `river_dialog_runtime_playback_truth_t`
      - `g_river_dialog_runtime.error_truth`
      - `g_river_dialog_runtime.playback_truth`
    - `refresh_error_recovering_locked(...)` 现在只更新 `error_truth`
    - `refresh_playback_locked(...)` 现在只更新 `playback_truth`
    - `export_playback_error_facts_to_snapshot_locked(...)` 再统一把这两组 truth
      投影到 snapshot
    - 这一步把：
      - residual derived state bag
      收口成：
      - explicit error truth
      - explicit playback truth
  - newest landed runtime-ownership slice:
    - `dialog runtime` 继续把 `snapshot export` 的 publish/runtime 边界收口成
      更小的 typed helper
    - 原先混合导出 `publish_state + derived_facts` 的
      `river_dialog_runtime_export_derived_facts_to_snapshot_locked(...)`
      已拆成：
      - `river_dialog_runtime_export_playback_error_facts_to_snapshot_locked(...)`
      - `river_dialog_runtime_export_publish_state_to_snapshot_locked(...)`
      - `river_dialog_runtime_export_runtime_state_to_snapshot_locked(...)`
    - playback/error 变更路径现在只刷新：
      - `error_recovering`
      - `error_kind`
      - `playback_owner_kind`
      - `playback_active`
      - `playback_recovering`
    - publish/reason 变更路径现在只刷新：
      - `interaction_state`
      - `transition_count`
      - `reason`
    - 这一步把：
      - mixed snapshot export helper
      收口成：
      - explicit publish export boundary
      - explicit playback/error export boundary
  - newest landed runtime-ownership slice:
    - `dialog runtime` 继续把 interaction publish 的
      `state/count/reason` 收口成统一 `publish_state`
    - `river_dialog_runtime_publish_observe_t` 已提升为：
      - `river_dialog_runtime_publish_state_t`
      并统一持有：
      - `interaction_state`
      - `transition_count`
      - `reason`
    - `derived_facts` 已不再承载 interaction publish 状态：
      - `derived_facts.interaction_state`
      - `derived_facts.transition_count`
      均已移除
    - `publish_locked(...)` 不再手工做：
      - `transition_count++`
      - `interaction_state = next_state`
      而是统一应用 typed publish view
    - 这一步把：
      - interaction publish state <- derived cache
      收口成：
      - explicit publish state
  - newest landed runtime-ownership slice:
    - `dialog runtime` 继续把 interaction publish 的 `reason` 边界收口成
      typed reason view
    - 新增统一 reason publish carrier：
      - `river_dialog_runtime_publish_observe_t`
      - `river_dialog_runtime_publish_reason_view_t`
      - `river_dialog_runtime_capture_publish_reason_view_locked(...)`
      - `river_dialog_runtime_apply_publish_reason_view_locked(...)`
    - `derived_facts.reason` 已退出 runtime 逻辑路径：
      - `snapshot.reason` 现在从 `publish_observe.reason` 导出
      - `publish_locked(...)` 不再反向读取 `derived_facts.reason`
      - `finalize_commit_locked(...)`、`init(...)`、
        `note_tts_interrupt_requested(...)`
        也不再直接写 `derived_facts.reason`
    - 这一步把：
      - reason publish / snapshot export <- derived cache back-dependency
      收口成：
      - explicit publish reason boundary
  - newest landed runtime-ownership slice:
    - `dialog runtime` 继续把 interaction publish 的 `previous -> next`
      transition 收口成 typed publish view
    - 新增统一 publish capture：
      - `river_dialog_runtime_interaction_publish_view_t`
      - `river_dialog_runtime_capture_interaction_publish_view_locked(...)`
    - 删除单用途 helper：
      - `river_dialog_runtime_compute_interaction_state_locked(...)`
    - `river_dialog_runtime_publish_locked(...)` 不再手工做：
      - next-state 计算
      - state-changed 判定
      - transition_count 递增
      而是统一从 publish view 读取：
      - `previous_state`
      - `next_state`
      - `state_changed`
    - 这一步把：
      - interaction publish transition branching
      收口成：
      - single publish view
  - newest landed runtime-ownership slice:
    - `dialog runtime` 继续把 commit checkpoint 的 `before/after` 判定收口到
      raw interaction evaluation
    - 新增统一 checkpoint 转换 helper：
      - `river_dialog_runtime_capture_commit_checkpoint_from_interaction_eval(...)`
    - `river_dialog_runtime_capture_commit_checkpoint_locked(...)`
      不再回读：
      - `derived_facts.playback_active`
      - `derived_facts.playback_recovering`
      - `derived_facts.error_recovering`
      - `derived_facts.interaction_state`
      而是统一从同一次 interaction evaluation 派生
    - `river_dialog_runtime_commit_ingress(...)` 与
      `river_dialog_runtime_finalize_commit_locked(...)`
      的 `before/after` checkpoint 现在都使用 shared raw checkpoint capture
    - 这一步把：
      - commit publish gating <- derived checkpoint cache
      收口成：
      - raw interaction-eval checkpoint view
  - newest landed runtime-ownership slice:
    - `dialog runtime` 继续把 interaction 的 `projection -> state/policy`
      评估链收口成单一 typed evaluation
    - 新增统一 interaction evaluation：
      - `river_dialog_runtime_interaction_eval_t`
      - `river_dialog_runtime_capture_interaction_eval_locked(...)`
    - `river_dialog_runtime_interaction_projection_t` 不再反向读取：
      - `derived_facts.interaction_state`
      并且 `error_recovering` 也不再从：
      - `derived_facts.error_recovering`
      回填，而是直接从 runtime raw error flags 派生
    - `river_dialog_runtime_compute_interaction_state_locked(...)`、
      `river_dialog_runtime_cloud_round_active_locked(...)`、
      `river_dialog_runtime_wakeword_block_reason_locked(...)`、
      `river_dialog_runtime_allows_barge_in_interrupt(...)`
      现在统一读取 shared interaction evaluation
    - 这一步把：
      - split interaction-state / policy checks
      收口成：
      - single interaction evaluation view
  - newest landed runtime-ownership slice:
    - `dialog runtime` 继续把 playback 的 `projection -> truth -> output_turn`
      评估链收口成单一 typed evaluation
    - 新增统一 playback evaluation：
      - `river_dialog_runtime_playback_eval_t`
      - `river_dialog_runtime_capture_playback_eval_locked(...)`
      - `river_dialog_runtime_output_turn_quiesced_from_playback_eval(...)`
    - `river_dialog_runtime_capture_interaction_projection_locked(...)`
      不再从 `derived_facts` 反向读取：
      - `playback_active`
      - `playback_recovering`
      而是从同一次 playback evaluation 的 truth 直接派生
    - `river_dialog_runtime_refresh_playback_locked(...)`、
      `river_dialog_runtime_output_turn_quiesced_locked(...)`、
      `river_dialog_runtime_playback_error_is_managed_recovery_locked(...)`
      现在都统一读取 shared playback evaluation
    - 这一步把：
      - split playback projection/truth/output-turn evaluation
      收口成：
      - single playback evaluation view
  - newest landed runtime-ownership slice:
    - `dialog runtime` 继续把 cloud snapshot 的 playback 导入收口成单一
      typed import
    - 新增统一 playback import：
      - `river_dialog_runtime_cloud_playback_import_t`
    - `river_dialog_runtime_cloud_import_t` 不再只携带：
      - `playback_facts`
      而是统一携带：
      - `playback.facts`
      - `playback.observe`
    - `river_dialog_runtime_capture_cloud_snapshot(...)`
      不再额外输出独立的 `cloud_playback_observe`
    - `river_dialog_runtime_import_cloud_snapshot_locked(...)`
      现在一次性导入：
      - `cloud_playback_facts`
      - `cloud_playback_observe`
    - `river_dialog_runtime_ingress_t` 与 commit 流程不再并行携带：
      - `has_cloud_playback_observe`
      - `cloud_playback_observe`
    - 这一步把：
      - split cloud-playback facts/observe ingress transport
      收口成：
      - single cloud-playback import
  - newest landed runtime-ownership slice:
    - `downlink/playback runtime` 继续把 playback 对外观测导出收口成 typed
      diagnostics view
    - 新增统一 diagnostics capture：
      - `river_cloud_xiaozhi_playback_diag_view_t`
      - `river_cloud_xiaozhi_capture_playback_diag_view(...)`
    - `river_cloud_xiaozhi_dump_playback_status(...)` 与
      `river_cloud_xiaozhi_fill_playback_runtime_snapshot(...)`
      现在共享同一份 playback 采样结果，而不是分别再去拼：
      - truth view
      - observe view
      - start gate
      - queue/rebuffer counters
    - 新增统一 recovery outcome 文本边界 helper：
      - `river_cloud_xiaozhi_playback_recovery_outcome_label(...)`
    - `downlink queue` 日志与 runtime snapshot 的 recovery path/outcome、
      start gate、rebuffer cause、queue counters 现在都从 diagnostics view 读取
    - 这一步把：
      - duplicated playback export sampling
      收口成：
      - single playback diagnostics capture view
  - newest landed runtime-ownership slice:
    - `downlink/playback runtime` 继续把 rebuffer recovery path 的本地真相源收口成 typed path
    - `river_cloud_xiaozhi_playback_rebuffer_observe_view_t` 不再保存
      字符串 `recover_path`，而是保存：
      - `river_cloud_playback_recovery_path_t recovery_path`
    - 删除仅为字符串路径服务的中间结果：
      - `river_cloud_xiaozhi_managed_rebuffer_recovery_result_t`
    - managed rebuffer 执行链现在统一复用：
      - `river_cloud_xiaozhi_rebuffer_recovery_result_t`
    - `river_cloud_xiaozhi_request_playback_rebuffer_recovery(...)`
      不再返回：
      - `river_status_t + recover_path_out`
      而是直接返回 typed recovery result
    - `write_failed`、`inline recover request`、`managed rebuffer request`、
      `upstream gap rebuffer` 现在都在本地传递：
      - `river_cloud_playback_recovery_path_t`
      只在日志边界通过：
      - `river_cloud_xiaozhi_playback_recovery_path_label(...)`
      转成字符串
    - 这一步把：
      - string-based recovery-path propagation
      收口成：
      - typed recovery-path truth + log-boundary translation
  - newest landed runtime-ownership slice:
    - `downlink/playback runtime` 继续把 managed rebuffer 执行结果收口成 typed result
    - 新增统一 managed rebuffer recovery result：
      - `river_cloud_xiaozhi_managed_rebuffer_recovery_result_t`
    - `river_cloud_xiaozhi_execute_managed_playback_rebuffer_recovery(...)`
      不再返回：
      - `river_status_t + recover_path_out`
      的松散组合，而是统一返回 typed result
    - `river_cloud_xiaozhi_execute_managed_playback_rebuffer_request(...)`
      现在也直接返回 typed result，并统一使用：
      - `result.status`
      - `result.recover_path`
      处理 fresh-start fallback 日志
    - `write_failed` 与 `upstream starved` 调用侧不再把 managed execute 结果拆成：
      - return status
      - mutable recover_path out-param
    - 这一步把：
      - split managed-rebuffer status/path result handling
      收口成：
      - single typed managed-rebuffer result
  - newest landed runtime-ownership slice:
    - `downlink/playback runtime` 继续把 rebuffer recovery path 的 preferred/fallback 执行收口成 typed attempt/result
    - 新增统一 recovery attempt/result：
      - `river_cloud_xiaozhi_rebuffer_recovery_attempt_t`
      - `river_cloud_xiaozhi_rebuffer_recovery_result_t`
      - `river_cloud_xiaozhi_capture_rebuffer_recovery_attempt(...)`
      - `river_cloud_xiaozhi_execute_rebuffer_recovery_attempt(...)`
    - 新增统一 path executor：
      - `river_cloud_xiaozhi_execute_rebuffer_recovery_path(...)`
    - `river_cloud_xiaozhi_request_playback_rebuffer_recovery(...)` 不再手写：
      - `prefer_service_recover`
      - `service_recover -> stop_rebuffer fallback`
      - `stop_rebuffer -> service_recover fallback`
      三段变量翻译和执行分支
    - recovery fallback 日志现在统一归一成：
      - `xiaozhi playback recovery fallback: from=... to=...`
    - 这一步把：
      - split preferred/fallback recovery path orchestration
      收口成：
      - typed rebuffer recovery attempt/result
  - newest landed runtime-ownership slice:
    - `downlink/playback runtime` 继续把 rebuffer 请求日志的公共观测字段收口成 typed observe view
    - 新增统一 rebuffer observe view：
      - `river_cloud_xiaozhi_playback_rebuffer_observe_view_t`
      - `river_cloud_xiaozhi_capture_playback_rebuffer_observe_view(...)`
    - `write_failed rebuffer request`、`inline service recover request`、`upstream gap rebuffer`
      日志现在统一从 observe view 读取：
      - `cause`
      - `supply`
      - `phase`
      - `backend`
      - `start_gate`
      - `queued/low_water`
      - `prefetch_target_ms`
      - `rebuffer_count/streak`
      - `recover_path`
    - `rebuffer` 相关日志不再分散从全局和 plan 混合拉取同一组字段
    - 这一步把：
      - duplicated rebuffer log field assembly
      收口成：
      - single typed rebuffer observe view
  - newest landed runtime-ownership slice:
    - `downlink/playback runtime` 继续把 managed rebuffer 执行收口成 typed request executor
    - 新增统一 managed request 描述与执行入口：
      - `river_cloud_xiaozhi_managed_rebuffer_request_t`
      - `river_cloud_xiaozhi_capture_managed_playback_rebuffer_request(...)`
      - `river_cloud_xiaozhi_start_managed_playback_rebuffer_request(...)`
      - `river_cloud_xiaozhi_execute_managed_playback_rebuffer_request(...)`
    - `river_cloud_xiaozhi_write_failed_followup_t` 不再直接暴露
      `force_stop_rebuffer`，而是携带完整 `rebuffer_request`
    - `write_failed` 与 `upstream starved` 两条链路现在统一走：
      - typed request capture
      - managed rebuffer start
      - caller-specific request log
      - shared recovery execute / fallback log
    - 删除旧的 write-failed 专用 wrapper：
      - `river_cloud_xiaozhi_handle_write_failed_managed_rebuffer(...)`
    - 这一步把：
      - per-caller managed-rebuffer begin/execute/fallback orchestration
      收口成：
      - shared typed managed-rebuffer request executor
  - newest landed runtime-ownership slice:
    - `downlink/playback runtime` 继续把 write-failed 恢复链收口成 typed follow-up
    - 新增统一 follow-up 描述：
      - `river_cloud_xiaozhi_write_failed_followup_t`
      - `river_cloud_xiaozhi_capture_write_failed_followup(...)`
    - `river_cloud_xiaozhi_handle_playback_write_failed(...)` 不再手写三段
      `inline_result -> managed_rebuffer/log/force_stop` 分支翻译：
      - `inline_success`
      - `managed_rebuffer`
      - `force_stop_rebuffer`
      - `request_log`
      现在都先通过 typed follow-up 收口，再统一执行 managed rebuffer
    - 这一步把：
      - split inline-result follow-up branching
      收口成：
      - single typed write-failed follow-up
  - newest landed runtime-ownership slice:
    - `dialog runtime` 继续把 playback raw projection 与派生 truth 解耦
    - 删除 `playback projection` 对已派生 truth 的反向依赖：
      - `river_dialog_runtime_playback_projection_t` 不再携带
        `playback_recovering`
      - `river_dialog_runtime_capture_playback_projection_locked(...)`
        不再回填 `derived_facts->playback_recovering`
    - 新增统一 playback truth helper：
      - `river_dialog_runtime_playback_truth_t`
      - `river_dialog_runtime_capture_playback_truth_from_projection(...)`
    - `dialog runtime` 现在统一通过 raw projection -> playback truth 链路派生：
      - `playback_active`
      - `playback_recovering`
      - `playback_owner_kind`
      - `output_turn_quiesced` 的 recovering 判定
    - 删除旧的分散 truth helper：
      - `river_dialog_runtime_compute_playback_recovering_from_projection(...)`
      - `river_dialog_runtime_compute_playback_active_from_projection(...)`
      - `river_dialog_runtime_compute_playback_owner_kind_from_projection(...)`
    - 这一步把：
      - playback projection <- derived truth backfill
      收口成：
      - raw playback projection + single playback truth helper
  - newest landed runtime-ownership slice:
    - `dialog runtime` 继续把 local playback import 从分散 prepare/apply 收口成显式 plan
    - 删除旧的两段式 helper：
      - `river_dialog_runtime_prepare_local_playback_import_locked(...)`
      - `river_dialog_runtime_apply_local_playback_import_locked(...)`
      - `river_dialog_runtime_apply_local_playback_state_locked(...)`
    - 新增统一 import plan：
      - `river_dialog_runtime_local_playback_import_plan_t`
      - `river_dialog_runtime_prepare_local_playback_import_plan_locked(...)`
      - `river_dialog_runtime_apply_local_playback_import_plan_locked(...)`
    - `dialog runtime` 现在先生成 local playback import plan，再在 cloud import
      落地后统一应用：
      - stream ownership claim/release
      - playback state 写入
      - error recovery / interrupt clear
    - 这一步把：
      - split local-playback prepare/apply mutation boundary
      收口成：
      - single typed local-playback import plan
  - newest landed runtime-ownership slice:
    - `dialog runtime` 继续收口 local playback shadow 的分散判定 helper
    - 删除分散 helper：
      - `local_playback_state_active_locked(...)`
      - `local_playback_state_recovering_locked(...)`
      - `local_playback_shadow_drives_truth_locked(...)`
      - `local_playback_shadow_active_fallback_locked(...)`
      - `local_playback_shadow_recovering_fallback_locked(...)`
    - 新增统一 shadow view：
      - `river_dialog_runtime_local_playback_shadow_view_t`
      - `river_dialog_runtime_capture_local_playback_shadow_view_locked(...)`
    - `dialog runtime` 现在统一通过 shadow view 驱动：
      - playback projection 的 local fallback 注入
      - local playback import 的 truth-drive gating
      - runtime dump 的 local playback 观测
    - 这一步把：
      - scattered local playback shadow predicates
      收口成：
      - single local shadow view
  - newest landed runtime-ownership slice:
    - `downlink/playback` 继续收口 ready-cycle 的单层 executor 壳
    - 删除中间 helper：
      - `river_cloud_xiaozhi_execute_ready_downlink_cycle(...)`
    - `river_cloud_xiaozhi_process_downlink_task_cycle(...)` 现在直接接管：
      - ready cycle 下的 current-frame acquire
      - write-step dispatch
    - 这一步把：
      - single-layer ready-cycle executor shell
      收口成：
      - direct cycle-processor ownership
  - newest landed runtime-ownership slice:
    - `downlink/playback` 继续把当前帧写入执行收口成单一 write-step helper
    - 删除中间 typed result：
      - `river_cloud_xiaozhi_downlink_frame_write_result_t`
    - `river_cloud_xiaozhi_write_current_downlink_frame_step(...)` 现在直接返回：
      - `river_cloud_xiaozhi_downlink_task_step_result_t`
    - `river_cloud_xiaozhi_execute_ready_downlink_cycle(...)` 不再负责：
      - write-result -> task-step 的二次翻译
      - post-write completion dispatch
    - 这一步把：
      - split write-result + post-write completion contract
      收口成：
      - single write-step outcome
  - newest landed runtime-ownership slice:
    - `downlink/playback` 继续把 `acquire_current_downlink_frame(...)`
      的 ready 判定从二值枚举收口成布尔返回
    - 删除中间 typed result：
      - `river_cloud_xiaozhi_downlink_frame_acquire_result_t`
    - `river_cloud_xiaozhi_acquire_current_downlink_frame(...)` 现在直接返回：
      - `bool`
    - `river_cloud_xiaozhi_execute_ready_downlink_cycle(...)` 不再负责：
      - acquire_result -> sleep/continue 的枚举翻译
    - 这一步把：
      - binary frame-acquire contract
      收口成：
      - direct acquire predicate
  - newest landed runtime-ownership slice:
    - `downlink/playback` 继续把 `prepare_downlink_playback(...)`
      的 ready 判定从二值枚举收口成布尔返回
    - 删除中间 typed result：
      - `river_cloud_xiaozhi_downlink_prepare_result_t`
    - `river_cloud_xiaozhi_prepare_downlink_playback(...)` 现在直接返回：
      - `bool`
    - `river_cloud_xiaozhi_prepare_downlink_cycle_plan(...)` 不再负责：
      - `prepare_result` -> ready boolean 的翻译
    - 这一步把：
      - binary prepare-result contract
      收口成：
      - direct ready predicate
  - newest landed runtime-ownership slice:
    - `downlink/playback` 继续把 prepare 阶段从 `result + out param`
      收口成单一 cycle plan
    - 删除中间 typed result：
      - `river_cloud_xiaozhi_downlink_cycle_result_t`
    - 新增统一 plan：
      - `river_cloud_xiaozhi_downlink_cycle_plan_t`
    - 新增统一 helper：
      - `river_cloud_xiaozhi_prepare_downlink_cycle_plan(...)`
    - `river_cloud_xiaozhi_process_downlink_task_cycle(...)` 现在只按：
      - `cycle_plan.ready`
      - `cycle_plan.step_result`
      - `cycle_plan.queued_frames`
      决定后续执行
    - 这一步把：
      - cycle result + queued_frames_out split contract
      收口成：
      - single cycle plan truth
  - newest landed runtime-ownership slice:
    - `downlink/playback` 继续把 ready downlink cycle 的执行结果直接归一到
      worker task-step
    - 删除中间 typed result：
      - `river_cloud_xiaozhi_downlink_execute_result_t`
    - `river_cloud_xiaozhi_execute_ready_downlink_cycle(...)` 现在直接返回：
      - `river_cloud_xiaozhi_downlink_task_step_result_t`
    - `river_cloud_xiaozhi_process_downlink_task_cycle(...)` 不再负责：
      - execute-result -> task-step 的二次翻译
    - 该 helper 链现在统一接管：
      - acquire miss -> sleep poll
      - write retry -> sleep poll
      - write abort/progress -> continue
    - 这一步把：
      - ready-cycle local result fan-out
      收口成：
      - direct task-step outcome
  - newest landed runtime-ownership slice:
    - `downlink/playback` 继续把 downlink worker 的 task-step 调度壳从主循环里
      收口成单一 cycle processor
    - 新增本地 typed result：
      - `river_cloud_xiaozhi_downlink_task_step_result_t`
    - 新增统一 helper：
      - `river_cloud_xiaozhi_process_downlink_task_cycle(...)`
      - `river_cloud_xiaozhi_finish_downlink_task_cycle(...)`
    - 该 helper 现在统一接管：
      - prepare/execute result fan-in
      - idle delay dispatch
      - poll delay dispatch
    - `downlink task` 现在只保留：
      - endless loop
      - call `process_downlink_task_cycle(...)`
      - call `finish_downlink_task_cycle(...)`
  - newest landed runtime-ownership slice:
    - `downlink/playback` 继续把 ready 状态下的 downlink cycle 执行从主循环里
      收口成统一 executor
    - 新增本地 typed result：
      - `river_cloud_xiaozhi_downlink_execute_result_t`
    - 新增统一 helper：
      - `river_cloud_xiaozhi_execute_ready_downlink_cycle(...)`
    - 该 helper 现在统一接管：
      - current-frame acquire dispatch
      - frame write dispatch
      - write-aborted immediate continue
      - write-retry poll gating
      - post-write completion dispatch
    - `downlink task` 在 prepare-ready 之后现在只按 typed execute result 决定：
      - poll delay
      - continue
  - newest landed runtime-ownership slice:
    - `downlink/playback` 继续把 downlink worker 的前置调度执行从主循环里
      收口成统一 cycle helper
    - 新增本地 typed result：
      - `river_cloud_xiaozhi_downlink_cycle_result_t`
    - 新增统一 helper：
      - `river_cloud_xiaozhi_prepare_downlink_cycle(...)`
    - 该 helper 现在统一接管：
      - downlink active gating
      - playback ACK progress refresh before cycle
      - rebuffer-starved gating
      - segment-gap pause gating
      - empty-queue pending-stop check
      - playback prepare dispatch
    - `downlink task` 在 acquire/write 之前现在只按 typed cycle result 决定：
      - idle delay + continue
      - poll delay + continue
      - ready
  - newest landed runtime-ownership slice:
    - `downlink/playback` 继续把单帧写成功后的收尾执行从 downlink task 主循环里
      收口成统一 helper
    - 新增统一 helper：
      - `river_cloud_xiaozhi_complete_written_downlink_frame(...)`
    - 该 helper 现在统一接管：
      - segment start latch on progress
      - playback ACK progress refresh
      - pending-stop check after progress
    - `downlink task` 在 frame write 成功后现在只保留：
      - call `complete_written_downlink_frame(...)`
  - newest landed runtime-ownership slice:
    - `downlink/playback` 继续把当前帧获取执行从 downlink task 主循环里
      收口成统一 helper
    - 新增本地 typed result：
      - `river_cloud_xiaozhi_downlink_frame_acquire_result_t`
    - 新增统一 helper：
      - `river_cloud_xiaozhi_acquire_current_downlink_frame(...)`
    - 该 helper 现在统一接管：
      - retry-valid fast path
      - downlink ring read
      - ring read failure后的 pending-stop check
    - `downlink task` 在 prepare 之后、write 之前现在只按 typed
      frame-acquire result 决定：
      - delay + continue
      - ready
  - newest landed runtime-ownership slice:
    - `downlink/playback` 继续把 playback 准备阶段从 downlink task 主循环里
      收口成统一 helper
    - 新增本地 typed result：
      - `river_cloud_xiaozhi_downlink_prepare_result_t`
    - 新增统一 helper：
      - `river_cloud_xiaozhi_prepare_downlink_playback(...)`
    - 该 helper 现在统一接管：
      - rebuffer resume readiness gating
      - pending-stop before-active gating
      - paused backend resume path
      - recovering backend stall
      - start-threshold / start-playback gating
    - `downlink task` 在读 ring / 写 frame 前现在只按 typed prepare result
      决定：
      - delay + continue
      - ready
  - newest landed runtime-ownership slice:
    - `downlink/playback` 继续把单帧写入执行从 downlink task 主循环里
      收口成统一 helper
    - 新增本地 typed result：
      - `river_cloud_xiaozhi_downlink_frame_write_result_t`
    - 新增统一 helper：
      - `river_cloud_xiaozhi_write_current_downlink_frame(...)`
    - 该 helper 现在统一接管：
      - frame oversize abort
      - stereo expand
      - playback service write
      - write_failed handler dispatch
      - retry-valid clear on success
    - `downlink task` 现在只按 typed result 决定：
      - continue
      - delay + continue
      - progress
  - newest landed runtime-ownership slice:
    - `downlink/playback` 继续把 `write_failed` 从 downlink task 主循环里
      收口成单入口 handler
    - 新增统一 helper：
      - `river_cloud_xiaozhi_handle_playback_write_failed(...)`
    - `downlink task` 在 `river_playback_service_write(...)` 失败后现在只保留：
      - call handler
      - delay + continue on non-inline recovery
    - `write_failed` 的：
      - recovery plan capture
      - initial failure log
      - inline recover dispatch
      - managed rebuffer fallback dispatch
      现在都由专用 handler 接管
    - 这一步把主循环从：
      - write_failed local orchestration
      推进到：
      - single write-failure entrypoint
  - newest landed runtime-ownership slice:
    - `downlink/playback` 继续把 `write_failed` 的 inline recover / replay /
      managed rebuffer fallback 从 downlink task 主循环里抽成专用 helper
    - 新增本地 typed result：
      - `river_cloud_xiaozhi_inline_recover_result_t`
    - 新增统一 helper：
      - `river_cloud_xiaozhi_try_write_failed_inline_recover(...)`
      - `river_cloud_xiaozhi_handle_write_failed_managed_rebuffer(...)`
      - `river_cloud_xiaozhi_log_write_failed_rebuffer_request(...)`
    - `downlink task` 里的 `write_failed` 主分支现在只保留：
      - capture plan
      - choose recover reason
      - dispatch inline result / fallback executor
    - 这一步把 `write_failed` 路径从：
      - nested inline recovery control flow
      推进到：
      - typed inline-result + dedicated executors
  - newest landed runtime-ownership slice:
    - `downlink/playback` 继续把 managed rebuffer 的状态迁移与恢复执行
      从分散分支里收口成统一 helper
    - 新增统一 helper：
      - `river_cloud_xiaozhi_begin_managed_playback_rebuffer(...)`
      - `river_cloud_xiaozhi_execute_managed_playback_rebuffer_recovery(...)`
    - `maybe_rebuffer_starved(...)` 与 `write_failed` 的三条 managed rebuffer
      入口现在共用同一条：
      - rebuffer state enter
      - retry frame retain
      - actual recovery execute
    - 这一步把 downlink/playback 从：
      - scattered rebuffer transition mutations
      推进到：
      - shared managed-rebuffer transition helpers
  - newest landed runtime-ownership slice:
    - `downlink/playback` 继续把 recovery observability 从 `path only` 推进到
      `path + outcome`
    - 新增公共枚举：
      - `river_cloud_playback_recovery_outcome_t`
    - `cloud runtime snapshot` / `dialog runtime snapshot` 现在都会镜像：
      - `playback_recovery_outcome_kind`
    - XiaoZhi playback runtime 现在会把：
      - inline replay success
        记为 `inline_replay`
      - managed rebuffer enter
        记为 `managed_rebuffer`
    - cloud/dialog runtime dump 现在也开始直接打印 `recovery_outcome`
  - newest landed runtime-ownership slice:
    - `downlink/playback` 继续把 `playback_recovery_path` 推进成当前 response 内
      稳定可见的最近恢复动作观测
    - 新增统一 helper：
      - `river_cloud_xiaozhi_set_playback_recovery_path(...)`
    - `request_playback_rebuffer_recovery(...)` 与 `write_failed` 的 inline
      recover / direct stop fallback 分支现在都经过同一条 recovery-path 写入链
    - `finish_playback_rebuffer()` 不再清空 `playback_recovery_path`，因此
      cloud/dialog snapshot 在当前 response 生命周期内会保留最近一次实际恢复路径
    - XiaoZhi playback status dump 现在也开始打印 `recovery_path`
  - newest landed runtime-ownership slice:
    - `downlink/playback` 继续把 actual recovery path 提升成 cloud/dialog 可见的
      typed observability
    - 新增公共枚举：
      - `river_cloud_playback_recovery_path_t`
    - `cloud runtime snapshot` / `dialog runtime snapshot` 现在都会镜像：
      - `playback_recovery_path_kind`
    - XiaoZhi playback runtime 会在 `request_playback_rebuffer_recovery(...)`
      内按 fallback 后的实际路径更新 `xiaozhi_playback_recovery_path`
    - `dialog runtime dump` 与 `xiaozhi runtime dump` 也开始直接打印
      `recovery_path`
  - newest landed runtime-ownership slice:
    - `downlink/playback` 继续把 `write_failed` / `starved` 恢复决策收口成统一
      typed recovery plan
    - 新增：
      - `river_cloud_xiaozhi_playback_recovery_plan_t`
      - `river_cloud_xiaozhi_playback_recovery_path_t`
    - `maybe_rebuffer_starved(...)` 与 `write_failed` 分支现在统一消费 recovery
      plan
    - `request_playback_rebuffer_recovery(...)` 现在会在 fallback 后返回实际采用的
      recovery path，不再把回退链错误打印成初始偏好路径
  - newest landed runtime-ownership slice:
    - `dialog runtime` 继续把 playback terminal 文本观测从 `session_observe`
      中拆出去
    - `river_dialog_runtime_cloud_session_observe_t` 不再承载：
      - `playback_terminal_reason`
      - `playback_terminal_wait_reason`
    - 上述字段现在统一并入：
      - `river_dialog_runtime_cloud_playback_observe_t`
    - snapshot capture/export 现在都改为从 `cloud_playback_observe` 搬运这些
      playback terminal 文本观测
  - newest landed runtime-ownership slice:
    - `dialog runtime` 继续把 playback 里的 observe-only 字段从 typed facts 中拆出去
    - `river_dialog_runtime_cloud_playback_facts_t` 不再承载：
      - `terminal_state_kind`
      - `rebuffer_cause_kind`
      - `start_policy_kind`
      - `start_frames`
      - `prefetch_frames`
      - `start_cautious_history`
    - 上述字段现在统一并入：
      - `river_dialog_runtime_cloud_playback_observe_t`
    - snapshot capture/export 现在都改为从 `cloud_playback_observe` 搬运这些
      observe-only 字段
  - newest landed runtime-ownership slice:
    - `dialog runtime` 继续把 `io/session` 里的纯观测字段从 typed facts 中拆出去
    - `river_dialog_runtime_cloud_io_facts_t` 现在只承载：
      - `input_lane`
      - `output_lane`
    - 新增：
      - `river_dialog_runtime_cloud_io_observe_t`
    - `river_dialog_runtime_cloud_session_facts_t` 现在只承载：
      - `turn_accepted`
      - `barge_in_enabled_known`
      - `barge_in_enabled`
    - 新增：
      - `river_dialog_runtime_cloud_session_observe_t`
    - `capture_cloud_snapshot(...)` / `import_cloud_snapshot_locked(...)` /
      snapshot export 现在都按 `facts` / `observe` 两条链分别搬运 `io/session`
      数据
    - `apply_cloud_event_locked(...)` 更新 sid 时也改写到
      `cloud_session_observe.session_id`
  - newest landed runtime-ownership slice:
    - `dialog runtime` 继续把 cloud snapshot ingress 从扁平字段包收口成与内部事实一致的分层载荷
    - `river_dialog_runtime_cloud_import_t` 不再平铺 round/io/session/playback
      相关字段
    - 现在直接按内部真相结构分成：
      - `round_facts`
      - `io_facts`
      - `session_facts`
      - `playback_facts`
    - `river_dialog_runtime_capture_cloud_snapshot(...)` 现在直接把 cloud runtime
      snapshot 填充进这些 typed facts
    - `river_dialog_runtime_import_cloud_snapshot_locked(...)` 也改为整块吸收这些
      facts，不再逐字段搬运
  - newest landed runtime-ownership slice:
    - `dialog runtime` 继续把 cloud snapshot 输入层里的 playback 观测与语义事实拆开
    - `river_dialog_runtime_cloud_import_t` 不再承载：
      - `playback_phase_known`
      - `playback_phase_kind`
    - `river_dialog_runtime_ingress_t` 新增独立 observe 载荷：
      - `has_cloud_playback_observe`
      - `cloud_playback_observe`
    - 新增统一 snapshot 捕获入口：
      - `river_dialog_runtime_capture_cloud_snapshot(...)`
      它会一次性拆出 semantic import 与 observe import
    - 新增独立导入路径：
      - `river_dialog_runtime_import_cloud_playback_observe_locked(...)`
    - `commit_cloud_event` / `reduce_local_playback_event` / `sync_cloud_state`
      现在都会单独携带 playback phase 观测值进入 reducer/import 边界
  - newest landed runtime-ownership slice:
    - `cloud playback runtime` 继续把 phase 观测值从 truth 链里彻底拆出去
    - 新增：
      - `river_cloud_xiaozhi_playback_observe_view_t`
      - `river_cloud_xiaozhi_capture_playback_observe_view(...)`
      - `river_cloud_xiaozhi_playback_observed_phase_kind()`
    - `river_cloud_xiaozhi_playback_backend_source_t` 不再承载：
      - `phase`
    - `river_cloud_xiaozhi_fill_playback_runtime_snapshot(...)` 现在同时读取：
      - `truth_view`
      - `observe_view`
    - `playback_phase_known/playback_phase_kind/playback_phase` 文本
      统一改为从 `observe_view` 导出，不再从 `truth_view.backend_source.phase`
      间接读取
    - playback 日志/诊断路径也统一改读
      `river_cloud_xiaozhi_playback_observed_phase_kind()`
  - newest landed runtime-ownership slice:
    - `dialog runtime` 继续清理“语义事实”和“观测字段”的边界
    - `river_dialog_runtime_cloud_playback_facts_t` 不再承载：
      - `phase_known`
      - `phase_kind`
    - 新增独立观测结构：
      - `river_dialog_runtime_cloud_playback_observe_t`
    - cloud snapshot 的 `playback_phase_known/playback_phase_kind` 现在单独落到
      `cloud_playback_observe`
    - `dialog_runtime` 导出 snapshot 时再从 observe 结构镜像 phase 观测值，
      避免 phase 与 typed playback 语义事实混放
  - newest landed runtime-ownership slice:
    - `dialog runtime` 继续缩减内部 projection 对 coarse playback phase 的依赖
    - `river_dialog_runtime_playback_projection_t` 不再镜像：
      - `phase_known`
      - `phase_kind`
    - `river_dialog_runtime_capture_playback_projection_locked(...)`
      不再把 cloud playback phase 塞进 projection 内部语义
    - `river_dialog_runtime_playback_turn_retains_output_turn_from_projection(...)`
      只保留：
      - `!cloud_runtime_available -> true`
      这个保守 fallback，不再依赖 `!phase_known`
    - phase 继续保留在 cloud/dialog snapshot 里做观测，但 projection 内部决策
      现在只消费 typed playback truth 与 runtime availability
  - newest landed runtime-ownership slice:
    - `cloud playback runtime` 继续把内部播放语义从 coarse phase 解耦
    - `river_cloud_xiaozhi_playback_backend_source_t` 现在额外镜像：
      - `stop_pending`
      - `rebuffer_pending`
      - `physical_active`
      - `waiting_next_segment`
    - `river_cloud_xiaozhi_compute_playback_backend_state_from_source(...)`
      不再读取：
      - `phase == REBUFFERING`
      - `phase_is_output_active(...)`
      现在直接从底层 truth 判定 recovering / owned_active / owned_paused
    - `river_cloud_xiaozhi_playback_hold_kind_from_source(...)` 不再依赖
      `phase == WAITING_SEGMENT`
    - `river_cloud_xiaozhi_playback_output_active()` /
      `river_cloud_xiaozhi_output_speaking_active()` 也统一改读：
      - `backend_source_output_active(...)`
      - `truth_view_retains_output_turn(...)`
      让 phase 进一步退回到观测值角色
  - newest landed runtime-ownership slice:
    - `cloud playback runtime` 继续把导出链上的 coarse phase 依赖向 typed truth
      收口
    - `river_cloud_xiaozhi_playback_truth_view_t` 现在额外镜像：
      - `queued_frames`
      - `tts_stop_pending`
      - `rebuffer_pending`
    - 新增统一 typed helper：
      - `river_cloud_xiaozhi_playback_supply_engages_lane(...)`
      - `river_cloud_xiaozhi_playback_lane_engaged_from_truth_view(...)`
      - `river_cloud_xiaozhi_playback_turn_active_from_truth_view(...)`
    - `river_cloud_xiaozhi_playback_lane_engaged()` /
      `river_cloud_xiaozhi_playback_turn_active()` /
      `river_cloud_xiaozhi_capture_held_by_playback(...)` /
      `river_cloud_xiaozhi_fill_playback_runtime_snapshot(...)`
      现在统一复用这套 helper，不再在导出链上回退到 `phase != IDLE`
  - newest landed runtime-ownership slice:
    - `dialog runtime` 继续去掉内部派生逻辑对 coarse playback phase 的直接依赖
    - `river_dialog_runtime_playback_projection_t` 现在显式镜像：
      - `supply_kind`
    - `river_dialog_runtime_playback_waiting_segment_from_projection(...)`
      现在改为直接读取：
      - `playback_supply_kind == WAITING_NEXT_SEGMENT`
      不再依赖 `phase_kind == WAITING_SEGMENT`
    - `river_dialog_runtime_playback_turn_retains_output_turn_from_projection(...)`
      现在改为通过 typed helper 读取：
      - `rebuffer_pending`
      - `backend_state_kind == OWNED_RECOVERING`
      - `backend_state_kind == RESTART_PENDING`
      不再直接依赖 `phase_kind == REBUFFERING`
    - 同时保留：
      - `!phase_known && !cloud_runtime_available`
      的保守 fallback，避免 cloud snapshot 缺席时误伤本地 shadow 语义
  - newest landed runtime-ownership slice:
    - playback runtime 继续把 detached quiet-window 的最后一段 phase fallback
      替换成 typed supply truth
    - 新增公开枚举：
      - `river_cloud_playback_supply_kind_t`
      - `river_cloud_playback_supply_kind_name(...)`
    - `river_cloud_runtime_snapshot_t` 与 `river_dialog_runtime_snapshot_t`
      现在都显式导出：
      - `playback_supply_kind`
    - `dialog runtime` 的 cloud import / playback facts / exported snapshot 也
      同步镜像了这条 supply truth，`dialog_runtime dump` 新增 `supply=...`
    - `river_cloud_xiaozhi_playback_quiet_window_from_gate_view()` 在 detached
      场景下改为直接读取 `supply_kind != NONE`
    - `river_voice_runtime_dialog_playback_quiet_window()` 也同步改成读取
      `snapshot->playback_supply_kind`
    - 这一步继续把 quiet-window / restart-pending 在 detached 场景下最后一段对
      coarse phase 的常态依赖替换成 typed supply truth
  - newest landed runtime-ownership slice:
    - XiaoZhi downlink / playback 继续把 `quiet_window` / `capture_held`
      从粗 phase 判定收口到 typed playback truth
    - playback runtime 新增：
      - `river_cloud_xiaozhi_playback_gate_view_t`
      - `river_cloud_xiaozhi_capture_playback_gate_view(...)`
      - `river_cloud_xiaozhi_playback_quiet_window_from_gate_view(...)`
    - `playback_quiet_window_allows_vad_open()` 现在优先读取：
      - `backend_state`
      - `hold_kind`
      - `terminal_wait_kind`
      - `tts_stop_pending`
      - `output_active`
      不再直接把整类 `PREFETCHING/REBUFFERING/WAITING_SEGMENT` 全部视为静默窗口
    - `capture_held_by_playback(...)` 也改为复用同一 gate truth
    - `river_voice_runtime_restart_pending_requires_block(...)` 现在同步改成
      基于 dialog snapshot typed playback truth 的 quiet-window 语义，不再依赖
      单独的 coarse `restart_pending_quiet_phase(...)`
    - 当前仅在 `DETACHED` 的残余过渡态继续保留 phase fallback；后续还可以继续把
      supply/waiting truth 显式导出后去掉这层兜底
  - newest landed runtime-ownership slice:
    - XiaoZhi downlink / playback 继续把 `segment_gap_hold` 从固定阈值收紧为动态低水位
    - 固定的 `1 frame` hold 常量已经移除，当前 hold 阈值现在统一由
      `river_cloud_xiaozhi_downlink_segment_gap_hold_frames()` 派生：
      - `attached_resume_threshold_frames - 1`
      - 再受 `starved_low_water_frames` 约束
      - 至少保持 `1 frame`
    - 这让 worker 能在“仍低于 attached resume 门槛、但已接近尾部 underrun 风险”的
      区间里更早进入 segment-gap hold，而不是等到只剩固定 `1 frame`
    - 同时 hold 阈值始终尽量低于 attached resume 门槛，避免 hold 之后下一轮立刻
      自动 resume
    - 这一步继续把段间恢复从硬编码 `1 frame` 猜测推进成依赖 runtime
      start/resume 真相的动态 low-water hold
  - newest landed runtime-ownership slice:
    - XiaoZhi downlink / playback 继续收窄 `segment_gap_hold` 的破坏边界
    - `river_cloud_xiaozhi_hold_playback_for_segment_gap(...)` 的 attached 路径现在
      不再走 destructive `flush_stream_ex(...)`，而是改为优先
      `river_playback_service_recover_stream_ex(...)`
    - 这继续保留现有：
      - attached hold
      - `OWNED_PAUSED -> maybe_resume_paused_playback()` 恢复链
      但不再为常态段间等待重置 reference/AEC 历史
    - `segment gap hold` 诊断日志现在改为显式区分：
      - `attached_recover`
      - `detached_stop`
    - 这一步继续把段间等待从“借 flush 实现暂停”收紧到“轻量 recover 实现 attached
      hold”，减少段间抖动时 reference/AEC 被不必要打断的概率
  - newest landed runtime-ownership slice:
    - XiaoZhi downlink / playback 继续收紧纯 `write_failed` 的瞬时恢复窗口
    - 当 cause 仍是 `WRITE_FAILED` 且 recovery=`service_recover` 时，worker 现在会在
      `river_playback_service_recover_stream_ex(...)` 成功后，同一轮立即重写当前帧，
      不再额外等待下一次 poll 才消费 `retry_valid`
    - `xiaozhi_downlink_retry_valid` 现在只在真正进入：
      - recover fallback
      - inline replay fallback
      - 非 inline rebuffer/retry
      时才置位；recover 成功且即时重写成功的路径不再污染 retry/rebuffer 语义
    - 新增诊断日志：
      - `xiaozhi playback inline recover replay succeeded`
      - `xiaozhi playback rebuffer requested after inline replay fallback`
    - 这一步继续把 downlink write-fail recovery 从“recover 成功但仍要等下一轮”
      收紧到“recover 成功即刻重写当前帧”，进一步缩小一次 poll 周期带来的可闻卡顿窗口
  - newest landed runtime-ownership slice:
    - XiaoZhi downlink / playback 继续拆分 `write_failed` 的 recover 与 rebuffer 语义
    - 纯 `WRITE_FAILED` 且 recovery=`service_recover` 的路径现在不再立刻：
      - `note_playback_rebuffer(...)`
      - 进入 `rebuffer_pending`
      - 累加 `rebuffer_streak`
    - worker 现在先尝试 inline `river_playback_service_recover_stream_ex(...)`；
      只有 recover 失败并回退到 `stop_rebuffer` 时，才正式登记 rebuffer
    - 新增诊断日志：
      - `xiaozhi playback service recover requested`
      - `xiaozhi playback rebuffer requested after recover fallback`
    - 这一步继续把 downlink write-fail recovery 从“瞬时写失败立即升级成 rebuffer”
      收紧到“先尝试 service recover，只有 recover 失败才进入 rebuffer”，降低
      `rebuffer_pending/streak/start_gate` 被误污染的概率
  - newest landed runtime-ownership slice:
    - playback service 继续拆分 `flush` / `recover` 的破坏边界
    - 新增：
      - `river_playback_service_restart_started_track_locked(...)`
    - `river_playback_service_flush_locked(...)` 现在继续保留 destructive flush 语义：
      - reset reference
      - restart track
    - `river_playback_service_recover_locked(...)` 现在改为直接重启 track，不再在
      transient `playback_write_failed` 恢复成功时调用
      `river_reference_service_reset()`
    - recover 失败时仍保持原有回退：
      - `close_locked(true)`
      - `RIVER_PLAYBACK_RESTART_PENDING`
    - 这一步把 downlink write-fail recovery 的 blast radius 从
      “reset reference + restart track” 收紧到“优先仅恢复 track”，为后续继续重建
      rebuffer / restart policy 提供更稳定的 AEC 连续性
  - newest landed runtime-ownership slice:
    - XiaoZhi downlink / playback 继续重建 segment-gap 恢复链
    - 新增：
      - `river_cloud_xiaozhi_segment_gap_hold_view_t`
    - `river_cloud_xiaozhi_maybe_pause_for_segment_gap()` 现在先捕获
      `truth_view + queued_frames`，当 supply 已进入 `WAITING_NEXT_SEGMENT`
      且 queued 只剩 1 帧时，提前执行 segment-gap hold
    - worker 主循环现在在 zero-queue 兜底前先处理低水位 hold，避免硬件先
      underrun 再进入 write_failed/rebuffer
    - 这一步把 segment-gap 恢复从“硬件先 underrun、再 write_failed/rebuffer”
      前移到“接近队尾时主动挂起”，减少一个放大抖动的卡顿入口
  - newest landed runtime-ownership slice:
    - dialog runtime 继续把 residual control / derived state 从 exported snapshot 中剥离
    - 新增：
      - `river_dialog_runtime_control_facts_t`
      - `river_dialog_runtime_derived_facts_t`
      - `river_dialog_runtime_export_control_facts_to_snapshot_locked(...)`
      - `river_dialog_runtime_export_derived_facts_to_snapshot_locked(...)`
    - 以下 residual dialog state 现在改由 internal facts 持有，再统一镜像到
      exported snapshot：
      - `boot_ready / wake_confirmed / asr_session_active`
      - `wake_admission_pending / tts_interrupt_requested`
      - `error_recovering / error_kind`
      - `playback_active / playback_recovering / playback_owner_kind`
      - `interaction_state / transition_count / reason`
    - `capture_playback_projection_locked(...)` 与
      `capture_interaction_projection_locked(...)` 现在改读 internal
      control/derived facts，不再把 exported snapshot 当作内部 dialog 真相源
    - 这一步把 dialog runtime 内部 reducer / projection / publish 对 exported
      snapshot 的依赖进一步压缩到 export/get/dump 边界，为后续继续重建
      downlink/playback 恢复路径提供稳定的单一 dialog 真相源
  - newest landed runtime-ownership slice:
    - dialog runtime 继续把 turn/session/terminal metadata 从 exported snapshot 中剥离
    - 新增：
      - `river_dialog_runtime_cloud_session_facts_t`
      - `g_river_dialog_runtime.cloud_session_facts`
      - `river_dialog_runtime_export_session_facts_to_snapshot_locked(...)`
    - `apply_cloud_event_locked(...)` 的 `sid` 更新现在改为写入 internal
      `cloud_session_facts`
    - `import_cloud_snapshot_locked(...)` 现在先写 internal session facts，再统一镜像到
      exported snapshot
    - 这一步继续把 dialog runtime 推进成唯一真相源，进一步减少“修改 exported snapshot
      就是在修改内部真相”的残留路径，为后续继续剥离 boot/wake/asr/error 等 residual
      state 做准备
  - newest landed runtime-ownership slice:
    - dialog runtime 继续把 interaction/raw facts 从 exported snapshot 中剥离
    - 新增：
      - `river_dialog_runtime_cloud_round_facts_t`
      - `river_dialog_runtime_cloud_io_facts_t`
      - `river_dialog_runtime_export_round_facts_to_snapshot_locked(...)`
      - `river_dialog_runtime_export_io_facts_to_snapshot_locked(...)`
    - `capture_interaction_projection_locked(...)` 现在改为直接读取 internal：
      - `cloud_round_facts`
      - `cloud_io_facts`
    - `capture_playback_projection_locked(...)` 的 `output_lane` 现在也改读
      internal `cloud_io_facts`
    - `import_cloud_snapshot_locked(...)` 现在先写 round/io facts，再统一镜像到
      exported snapshot
    - 这一步继续把 dialog runtime 推进成唯一真相源，进一步明确内部 round/io raw facts
      与对外 snapshot export 的分层，为后续继续剥离 turn/session metadata 做准备
  - newest landed runtime-ownership slice:
    - dialog runtime 开始把内部 playback raw facts 与 exported snapshot 拆层
    - 新增：
      - `river_dialog_runtime_cloud_playback_facts_t`
      - `g_river_dialog_runtime.cloud_playback_facts`
      - `river_dialog_runtime_export_playback_facts_to_snapshot_locked(...)`
    - `capture_playback_projection_locked(...)` 现在改为直接读取 internal
      `cloud_playback_facts`，不再把 exported snapshot 当作内部 playback raw truth
    - `import_cloud_snapshot_locked(...)` 现在先写 playback facts，再统一镜像到
      exported snapshot
    - 这一步继续把 dialog runtime 推进成唯一真相源，开始把“内部 raw playback facts”
      与“对外 snapshot export”显式拆层，为后续继续拆 turn/input/output raw facts
      做准备
  - newest landed runtime-ownership slice:
    - dialog runtime 继续把入口层收口成统一的 dialog-owned ingress reducer
    - 新增：
      - `river_dialog_runtime_local_playback_import_t`
      - `river_dialog_runtime_ingress_t`
      - `river_dialog_runtime_capture_local_playback_import(...)`
      - `river_dialog_runtime_commit_ingress(...)`
    - `commit_cloud_event(...)` / `sync_cloud_state(...)` /
      `reduce_local_playback_event(...)` 现在都只负责组装 dialog-owned ingress，
      再走同一条 reducer 提交路径
    - local playback callback 不再把外部 `river_playback_stream_config_t`
      直接带进 reducer 提交逻辑，而是先复制为 dialog 自己拥有的 local import 载体
    - 统一 ingress 仍保持既有关键顺序：
      - cloud-event: apply event -> import cloud -> reconcile -> finalize
      - local-playback: resolve ownership -> import cloud -> apply local shadow -> reconcile -> finalize
    - 这一步继续把 dialog runtime 推进成唯一 dialog 真相源，进一步削弱 callback /
      adapter 结构对 reducer 提交面的直接影响，为后续继续拆 raw-fact 与
      derived-truth 边界做准备
  - newest landed runtime-ownership slice:
    - dialog runtime 继续把 cloud ingress 收口成 dialog-owned
      `cloud_import` 载体
    - 新增：
      - `river_dialog_runtime_cloud_import_t`
      - `river_dialog_runtime_capture_cloud_import(...)`
    - dialog runtime 现在只在 `capture_cloud_import(...)` 这一层接触
      `river_cloud_runtime_snapshot_t`
    - `import_cloud_snapshot_locked(...)` 现在改为只消费 dialog 自己拥有的
      `cloud_import` 原始事实，而不再把 adapter export snapshot 直接送入 reducer
    - `commit_cloud_event(...)` / `sync_cloud_state(...)` /
      `reduce_local_playback_event(...)` 现在统一先：
      - capture cloud import
      - import facts
      - reconcile facts
      - finalize commit
    - 这一步继续把 dialog runtime 推进成 dialog 真相源，削弱 reducer 对
      cloud adapter 导出结构的耦合，为下一步继续压缩 typed ingress / reducer
      管线做准备
  - newest landed runtime-ownership slice:
    - dialog runtime 已把 cloud snapshot 路径从单个混合 helper 拆成：
      - `import_cloud_snapshot_locked(...)`
      - `reconcile_facts_locked(...)`
    - `import_cloud_snapshot_locked(...)` 现在只导入 cloud raw facts
    - `reconcile_facts_locked(...)` 现在统一负责：
      - clear local playback error shadow when cloud runtime is available
      - refresh error/playback truth
      - latch cloud-round-backed `asr_session_active`
      - clear quiesced `tts_interrupt_requested`
    - `commit_cloud_event(...)` / `sync_cloud_state(...)` /
      `reduce_local_playback_event(...)` 现在都显式复用：
      - import facts
      - reconcile facts
      - finalize commit
    - 这一步继续把 dialog runtime 入口推进成更清楚的 reducer 管线，为后续把
      raw cloud snapshot 再收成更明确的 dialog import carrier 做准备
  - newest landed runtime-ownership slice:
    - dialog runtime 现在为 cloud event / cloud sync / local playback reducer
      引入统一的 `commit checkpoint + commit policy`
    - 新增内部辅助层：
      - `capture_commit_checkpoint_locked(...)`
      - `commit_checkpoint_changed(...)`
      - `finalize_commit_locked(...)`
    - `commit_cloud_event(...)` / `sync_cloud_state(...)` /
      `reduce_local_playback_event(...)` 现在开始共用同一条提交边界
    - local playback reducer 不再单独手工维护：
      - `prev_playback_active`
      - `prev_playback_recovering`
      - `prev_error_recovering`
      - `prev_interaction_state`
    - 这一步继续把 dialog runtime 从“各入口各自拼 publish gating”推进成
      “single reducer/commit boundary -> publish policy”
  - newest landed runtime-ownership slice:
    - dialog runtime 继续为输入侧 / 会话侧派生引入内部
      `interaction_projection`
    - `capture_interaction_projection_locked(...)` 会一次性锁存：
      - boot / error / asr / wake / window
      - cloud close / listen-stop
      - `tts_interrupt_requested`
      - input/output lane
      - 当前 interaction state
      - 复用的 playback projection
    - 以下派生现在开始复用同一份 interaction projection，而不再散读
      `g_river_dialog_runtime.snapshot`：
      - `cloud_round_active`
      - `tts_interrupt_inflight`
      - `interaction_state`
      - `wakeword_block_reason`
      - `allows_barge_in_interrupt`
    - 这一步开始把 dialog runtime 输入侧 / 会话侧真相也推进成
      “single interaction projection -> admission / interaction truth”
  - newest landed runtime-ownership slice:
    - dialog runtime 现在为 playback 派生引入内部
      `playback_projection`
    - `capture_playback_projection_locked(...)` 会一次性锁存 dialog runtime 当前计算
      playback truth 所需的投影输入，包括：
      - cloud playback facts
      - backend / phase / hold / terminal wait
      - local playback shadow fallback
      - output lane
    - 以下派生现在都开始复用同一份 projection，而不再散读
      `g_river_dialog_runtime.snapshot`：
      - `playback_owner_kind`
      - `playback_recovering`
      - `playback_active`
      - `output_turn_engaged`
      - `output_turn_quiesced`
    - 这一步开始把 dialog runtime 的 playback truth 从“多字段散读派生”
      收口成“single projection -> playback/output truth”
  - newest landed runtime-ownership slice:
    - XiaoZhi playback runtime 继续把 control-path 收口到显式
      `playback_truth_view`
    - 以下边界现在都改为先捕获 truth-view，再消费同一份
      `backend/phase/hold/output_active` 真相：
      - `apply_transport_reset_playback_policy()`
      - `apply_session_start_playback_policy()`
      - `playback_note_duplex_ready()`
      - `reset_playback_state()`
      - `playback_check_pending_stop()`
      - `playback_abort_for_cause()`
      - `start_playback_if_needed()`
    - `playback_abort` 日志现在也直接打印 truth-view 里的
      `phase/hold/backend`
    - 这一步继续减少 control-path 与 worker-path 对 playback backend 的分叉判定
  - newest landed runtime-ownership slice:
    - XiaoZhi playback runtime 继续把观测面收口到显式 `playback_truth_view`
    - `playback_truth_view` 现在额外派生 `hold_kind`，使执行面和观测面共用
      同一份 phase/backend/hold/supply 真相
    - `dump_playback_status()` 现在改为一次性捕获 truth-view，并在
      `playback_terminal` / `downlink` 诊断日志里复用：
      - `phase`
      - `hold`
      - `backend`
      - `supply`
    - `fill_playback_runtime_snapshot()` 现在也直接消费同一份 truth-view，而不再
      单独重建 backend-source
    - 这一步继续减少“执行面按一套 truth 决策、诊断面按另一套 helper 读取”的漂移
  - newest landed runtime-ownership slice:
    - XiaoZhi playback runtime 继续把 downlink worker 的核心分支收口到显式
      `playback_truth_view`
    - `maybe_resume_paused_playback()` 现在改为消费已捕获的 truth-view，而不再
      在函数内部重新读取 `phase/backend`
    - downlink worker 主循环在单次决策窗口内开始复用同一份 truth-view，用于：
      - `rebuffer_resume_ready`
      - `tts_stop_pending`
      - `paused -> resume`
      - `needs_start / recovering / start_threshold`
    - `playback write failed -> rebuffer` 路径现在也复用同一份 truth-view 来驱动：
      - `supply_kind`
      - `phase/backend` 诊断日志
      - recovery path 选择
    - 这一步继续把 worker 从“循环内多 helper 重读”推进成
      “single worker decision -> single truth-view”，减少一次循环内的
      backend/supply 读偏斜
  - newest landed runtime-ownership slice:
    - XiaoZhi playback runtime 现在把 recovery 关键分支收口到显式
      `playback_truth_view`
    - `capture_playback_truth_view(...)` 会一次性锁存并派生：
      - `backend_source`
      - `supply_source`
      - `backend_state`
      - `supply_kind`
      - `output_active`
    - `maybe_pause_for_segment_gap()` 与 `maybe_rebuffer_starved()` 现在都开始复用
      同一份 truth-view，而不再各自分别重读：
      - `phase`
      - `backend`
      - `supply`
    - `segment_gap_hold` 日志也改成直接打印 truth-view 里的
      `phase/backend`
    - 这一步继续把 downlink/playback recovery 从“多 helper 组合读”推进成
      “显式 truth-view -> recovery 判定”，减少 worker loop 内部的读偏斜
  - newest landed runtime-ownership slice:
    - XiaoZhi playback runtime 现在为 supply 判定引入显式
      `playback_supply_source`
    - `capture_playback_supply_source(...)` 会一次性锁存：
      - `wait_context_valid`
      - `last_segment_observed`
      - `segment_count`
    - `compute_playback_waiting_next_segment_from_source(...)` 与
      `compute_playback_supply_kind_from_source(...)` 现在都只消费这份 source
    - 以下路径开始复用同一份 supply-source：
      - `playback_waiting_next_segment()`
      - `playback_supply_kind()`
      - `capture_playback_phase_source(...)`
    - 这一步继续把 `waiting_next_segment / current_segment / terminal_tail`
      的供给真相边界从多点混读收口成显式 supply source，减少 worker 与 phase
      派生之间的 supply 读偏斜
  - newest landed runtime-ownership slice:
    - XiaoZhi playback runtime 现在为 backend 派生引入显式
      `playback_backend_source`
    - `capture_playback_backend_source(...)` 会一次性锁存：
      - `service_view.state`
      - `service_view.active`
      - `service_view.owned_stream`
      - `phase`
    - `compute_playback_backend_state_from_source(...)` 现在只消费这份 source，
      不再在 backend 判定过程中分别读取 service stats 与 phase
    - 关键消费点也开始复用同一份 source：
      - `playback_output_active()`
      - `playback_hold_kind()`
      - `fill_playback_runtime_snapshot()`
      - `tts_start` / `playback_started` fallback 日志
    - 这一步继续把 playback backend 从“service state + 全局 phase 混合读取”
      推进成“显式 backend source -> backend truth”的单向派生，并减少 phase/backend
      双读带来的诊断读偏斜
  - newest landed runtime-ownership slice:
    - XiaoZhi playback runtime 现在为 phase 派生引入了显式
      `playback_phase_source`
    - `capture_playback_phase_source(...)` 会一次性锁存：
      - `stop_pending`
      - `rebuffer_pending`
      - `physical_active`
      - `queued_frames`
      - `segment_count`
      - `waiting_next_segment`
    - `compute_playback_phase_from_source(...)` 现在只消费这份 source，
      不再在 phase 派生过程中散落读取全局态
    - `refresh_playback_phase(...)` 也直接复用同一份 source 打日志，观测面补齐：
      - `segments`
      - `wait_next`
    - 这一步继续把 playback phase 从“混合读取 shadow/queue/wait 状态”
      推进成“显式 physical + queue/segment semantic source -> phase”的单向派生
  - newest landed runtime-ownership slice:
    - XiaoZhi playback runtime 现在把本文件内 residual 的
      `xiaozhi_playback_active` 读取统一收口到
      `river_cloud_xiaozhi_playback_physical_active()`
    - `compute_playback_phase()` 与 `arm_playback_stop()` 现在都显式消费这份
      named physical truth，而不再裸读 shadow 成员
    - `playback phase`、`interrupt hint`、`hint-only endpoint`、`duplex dump`
      诊断日志现在把字段明确标成：
      - `playback_physical`
      并补齐：
      - `phase`
      - `backend`
    - 这一步继续把 playback runtime 内“物理播放中”和“语义播放占用中”拆开命名，
      避免板端继续把单个 `playback=yes/no` 字段误当成 semantic truth
  - newest landed runtime-ownership slice:
    - XiaoZhi playback runtime 的策略层又收紧了两处
      `xiaozhi_playback_active` shadow 消费：
      - `playback_has_work()`
      - `segment_prefetch_target_needed()`
    - 两者现在统一改为消费
      `river_cloud_xiaozhi_playback_output_active()`
    - 这意味着：
      - downlink task 的唤醒条件
      - segment predictive prefetch 的抑制条件
      都开始直接跟随 runtime `phase + backend` typed truth
    - 这一步继续把 playback runtime 的策略层从 coarse shadow bool 收口到
      semantic output-active truth，为后续继续处理 `compute_playback_phase()`
      内部残留的 physical/semantic 混用做准备
  - newest landed runtime-ownership slice:
    - XiaoZhi playback runtime 的两个 recovery 分支现在不再直接看
      `g_river_cloud.xiaozhi_playback_active` 这个影子布尔：
      - `maybe_pause_for_segment_gap()`
      - `maybe_rebuffer_starved()`
    - 两者现在统一改为消费
      `river_cloud_xiaozhi_playback_output_active()`
    - 这意味着段间 hold / starvation rebuffer 的触发前提开始直接跟随：
      - runtime phase truth
      - typed backend truth
      而不是继续被局部 `playback_active` 影子位牵着走
    - 这一步继续把 downlink/playback recovery 的判定边界从 coarse
      shadow bool 收口到 runtime-owned semantic truth，为后面继续清理
      prefetch / phase 内 residual `playback_active` 消费点打基础
  - newest landed runtime-ownership slice:
    - `voice runtime` 的 `restart_pending` hard block 现在优先消费
      `dialog runtime` 导出的 typed backend truth
    - 当前只要 dialog snapshot 仍认定：
      - `playback_owner_kind == CLOUD`
      - backend=`restart_pending`
      - 且不在 quiet phase
      就会继续落到
      `RIVER_VOICE_AEC_GATE_BLOCKED_PLAYBACK_RESTART_PENDING`
    - raw `playback_state == RESTART_PENDING` 现在只在拿不到 dialog snapshot 时
      才作为兜底路径
    - 这一步继续把 AEC restart gate 从“先看物理 service state 再问语义”推进到
      “先看 dialog/runtime backend 真相，再用 raw state 兜底”
  - newest landed runtime-ownership slice:
    - `voice runtime` 在 `uses_native_capture_ref` 且 native ref 尚未可用时，
      不再重新读取 raw `playback_state_active()` 去区分：
      - `ref_idle`
      - `ref_missing`
    - 现在只要已经通过前置 AEC playback gate，就统一把这类窗口解释成
      `ref_idle`
    - 这意味着 native reference 的缺席不再在 AEC/duplex 评估末端再次从
      物理 playback state 反推语义，reference 分类与前面已接受的
      dialog/playback gate 真相保持一致
    - 这一步继续清理 `voice runtime` 内 residual 的 raw playback-state
      语义依赖，让 `ref_idle/ref_missing` 的边界更贴近 runtime gate 真相
  - newest landed runtime-ownership slice:
    - `voice runtime` 的 AEC/duplex 评估结构现在开始显式携带
      `dialog runtime` 导出的：
      - `playback_owner_kind`
      - `error_kind`
    - `preproc` 的 `webrtc_aecm gate=...` 迁移日志现在会直接打印：
      - raw `playback_state`
      - typed `playback_owner_kind`
      - typed `error_kind`
    - XiaoZhi duplex/fallback 关键日志也同步补上这三项，板端看到
      `aec_blocked` / `duplex_ready=no` 时，不再需要从粗粒度布尔反推当前是：
      - 物理 playback 不在场
      - 还是 dialog 语义 owner 已切换
      - 还是 error truth 仍挂在 fallback 阶段
    - 这一步继续把 AEC/preproc 诊断面与 `dialog runtime` 真相源对齐，为下一步
      继续清理 residual raw playback-state 语义依赖提供可观测基础
  - newest landed runtime-ownership slice:
    - voice runtime 现在开始显式消费 `dialog runtime` 导出的
      `playback_owner_kind`
    - dialog snapshot 兜底保持 playback-engaged 的 helper 现在明确只接受：
      - `playback_owner_kind == CLOUD`
      - 且仍有：
        - `playback_lane_engaged`
        - 或 `playback_recovering`
        - 或 `playback_turn_active`
    - dedicated `restart_pending` hard block 与 generic dialog-playback
      fallback 现在都复用这条显式 cloud-owner 判定
    - 本地 playback-service state 仍保留为“物理播放是否真的存在”的底层真相；
      这一步只把 dialog snapshot fallback 从隐式布尔组合推进成 typed owner
      truth 消费
  - newest landed runtime-ownership slice:
    - `dialog runtime` 现在已把 playback ownership truth 正式外显成
      snapshot 级别的 `playback_owner_kind`
    - 新增公开枚举：
      - `NONE`
      - `CLOUD`
      - `LOCAL_FALLBACK`
    - `refresh_playback_locked()` 现在统一同时派生：
      - `playback_active`
      - `playback_recovering`
      - `playback_owner_kind`
    - dump 观测面也已新增 `owner=<kind>`
    - 这一步继续把 `dialog runtime` 从隐式 ownership 推断推进成 typed
      playback ownership export
  - newest landed runtime-ownership slice:
    - `dialog runtime` 现在已把 typed error source 正式外显成
      snapshot 级别的 `error_kind`
    - 新增公开枚举：
      - `NONE`
      - `ASR`
      - `LOCAL_PLAYBACK`
      - `MIXED`
    - `refresh_error_recovering_locked()` 现在统一同时派生：
      - `error_recovering`
      - `error_kind`
    - dump 观测面也从 `error=yes/no` 升级为 `error=yes/no/<kind>`
    - 这一步继续把 `dialog runtime` 从聚合 bool 推进成 typed truth export
  - newest landed runtime-ownership slice:
    - `dialog runtime` 现在只会在事件已确认属于当前 dialog-owned playback
      stream 时，才在 `IDLE` 上清理：
      - `local_playback_stream_owned`
      - `local_playback_stream_name`
    - foreign playback stream 的 `IDLE` 事件不再先把当前 owned stream tracking
      擦掉
    - 这一步继续把 local playback ownership 真相收口到“只消费本 dialog
      stream 的事件”
  - newest landed runtime-ownership slice:
    - `dialog runtime` 内部已把 `error_recovering` 拆成：
      - `asr_error_recovering`
      - `local_playback_error_recovering`
    - 对外导出的 `snapshot.error_recovering` 现在统一经
      `refresh_error_recovering_locked()` 聚合
    - cloud snapshot 一旦重新可用，会主动清掉
      `local_playback_error_recovering` shadow
    - 这一步继续把 `dialog runtime` 从 coarse error bool 推进成
      typed internal truth
  - newest landed runtime-ownership slice:
    - `dialog runtime` 现在新增
      `local_playback_shadow_drives_truth_locked()`
    - local playback event 在 cloud runtime 已可用时，仍会更新本地 playback
      诊断 shadow，但不再继续直接：
      - `refresh_playback`
      - 改写 `error_recovering`
      - 清理 `tts_interrupt_requested`
    - 这一步把 `dialog runtime` 继续从“本地 playback 事件也能直接推交互态”
      收口到“cloud truth 优先，local playback 只在 cloud 不可用时兜底”
  - newest landed runtime-ownership slice:
    - voice runtime 的 generic playback AEC gate 现在也消费 dialog runtime
      的 playback-lane truth
    - 本地 playback-service 短暂 inactive 时，只要 dialog runtime 仍认定：
      - lane engaged
      - recovering
      - turn active
      AEC path 就不再立刻退回 `block_playback`
    - `restart_pending` 与 generic playback gate 现在共用同一份
      dialog snapshot
    - 这一步继续把 duplex/AEC 的播放占用判定收口到 dialog runtime 真相源
  - newest landed runtime-ownership slice:
    - XiaoZhi playback backend truth 现在只对 owned stream 承认
      `backend_restart_pending`
    - `playback_backend_state()` 不再在 ownership 判定之前就无条件吞掉
      raw `RESTART_PENDING`
    - foreign stream 的 restart/recover 不再污染 XiaoZhi 本流的 backend truth
    - 这一步继续把 playback backend 语义收回到“owned stream + typed truth”
  - newest landed runtime-ownership slice:
    - voice runtime 的 `restart_pending` AEC gate 不再只看本地
      `playback_state`
    - `restart_pending` 是否仍应硬阻塞，现在先由 dialog runtime snapshot 判定：
      - lane 是否仍 engaged
      - backend 是否仍是 `restart_pending`
      - 是否已进入 quiet recovery window
    - `prefetching/rebuffering/waiting_segment` 这类 quiet recovery 窗口内，
      AEC path 不再被 `restart_pending` 提前 reset
    - 这一步继续把 duplex/AEC 恢复门控从 playback-service coarse state
      收回到 dialog/runtime 真相源
  - newest landed runtime-ownership slice:
    - playback runtime 现在区分：
      - detached cold start
      - restart-pending restart
    - `RESTART_PENDING` 重新起播不再一律等待 cold-start `start_frames`
    - 新增 backend-aware start threshold helper：
      - `downlink_start_threshold_for_backend(...)`
    - `restart_pending` restart 现在复用 attached-resume 门限：
      - `min(start_frames, buffer_frames)`
    - 这一步继续把 playback start policy 从粗粒度 backend state 中拆开，
      降低 recover/restart 后再次“冷启动式”排队
  - newest landed runtime-ownership slice:
    - playback runtime 现在显式区分：
      - backend 仍保有 turn / restart 语义
      - backend 底层 stream 是否真的 attached
    - `RESTART_PENDING` 不再被 stop/flush/abort/pending-stop 这类边界
      当成 attached stream
    - `pending_stop` 现在会把 `restart_pending` 统一按 non-attached backend
      立即 drop queue / ack / reset
    - `transport_reset` / `session_start` / `segment_gap_hold` 也不再对已脱离
      硬件的 backend 再次 stop/flush
  - current next runtime slice:
    - 继续收口 worker / dump / snapshot 里的组合 truth 读边界
    - 下一刀优先判断：
      - cloud runtime snapshot 对 dialog runtime 的 projection 输入字段是否还需继续
        消减和分层，避免 snapshot 同时承担 transport export 与 dialog truth 中转
      - dialog runtime 是否要进一步把 publish/sync/reduce-local-playback 这些入口
        也收口到显式 reducer/commit 边界，彻底形成单向状态归约
  - newest landed runtime-ownership slice:
    - downlink/playback runtime 现在显式拆分：
      - cold start threshold
      - attached resume threshold
      - actual playback buffer budget
    - playback start 时会锁存本次真正下发给 playback service 的
      `buffer_frames`
    - attached hold / rebuffer resume 不再一律等待 cold-start `start_frames`
    - 当前 attached resume 门限统一变成：
      - `min(start_frames, buffer_frames)`
    - playback status / resume 日志现在会直接暴露：
      - `start`
      - `resume`
      - `buffer`
  - newest landed runtime-ownership slice:
    - playback rebuffer 恢复决策现在收口到统一 helper
    - `UPSTREAM_STARVED + CURRENT_SEGMENT` 也开始优先 attached
      `service_recover`
    - `maybe_rebuffer_starved()` 不再默认先走 detached `stop_rebuffer`
    - timer-starved / write-failed 两条恢复路径现在共用同一套恢复选择与回退
      语义
  - newest landed runtime-ownership slice:
    - playback runtime / dialog runtime 现在显式导出 `playback_hold_kind`
    - `owned_paused + waiting_segment` 会被 typed 成
      `segment_gap` hold，而不是继续让 core 从 backend state 猜
    - dialog runtime 不再把 `OWNED_PAUSED` 一律解释成
      `playback_recovering`
    - output continuity 现在由 hold truth 保留，recovery/error 语义不再被
      正常段间 hold 污染
  - newest landed runtime-ownership slice:
    - `segment_gap_pause` 现在优先走 attached `flush` hold，失败才回退到
      detached stop
    - downlink worker 现在会在 backend=`owned_paused` 且 refill 达标时显式
      resume playback runtime
    - transport reset / session start 也会先 stop 已 attach 的 owned backend，
      避免 segment-gap attached pause 遗留悬挂 backend
  - newest landed runtime-ownership slice:
    - `maybe_rebuffer_starved()` 不再在 `waiting_next_segment` 窗口里提前触发
      upstream-starved rebuffer
    - 当 supply truth 已明确进入：
      - `waiting_next_segment`
      starvation watch 会被直接清掉，继续让 segment-gap 路径接手
    - 这继续把“段间晚到”从 generic low-water stop/rebuffer 模型中拆出去
  - newest landed runtime-ownership slice:
    - `write_failed` 恢复链现在也显式消费 runtime-owned supply truth
    - 当 rebuffer cause 被判定为 `upstream_starved`，且当前 supply 已明确是
      `waiting_next_segment` 时：
      - 不再默认 detached `stop_rebuffer`
      - 改为优先 attached `service_recover`
    - `write_failed` 日志也会直接打印当前 supply kind，便于板端确认当前是在：
      - 当前段内断流
      - 段间晚到
      - 还是 terminal tail
  - newest landed runtime-ownership slice:
    - `audio.out.completed` ACK 已不再回退到“当前 playback meta 的
      response/playback 上下文”
    - completed 终态现在只消费 terminal last-segment 自带的：
      - `response_id`
      - `playback_id`
      - `segment_id`
    - playback dump 也新增 terminal context 观测面，便于板端直接确认
      completed 将绑定的 playback lineage
  - newest landed runtime-ownership slice:
    - XiaoZhi playback runtime 已把 fully-heard segment 的：
      - `response_id`
      - `playback_id`
      - `segment_id`
      一起保存成 heard context
    - `audio.out.cleared` ACK 不再拿“当前 playback meta 的 response/playback
      上下文”去发送
    - cleared 终态现在直接消费 runtime-owned fully-heard segment truth，
      继续减少新 response / 新 meta 覆盖旧 playback context 时的错 ACK 风险
  - newest planning sync:
    - 已把 `/root/agent-server` 2026-04-21 主线语音进展回灌到设备侧计划：
      - 服务侧不再主要阻塞于双轨 session / preview-first / playback truth 主干
      - 当前更偏向预算收口与回归基线：
        - `preview_first_partial / accept / interrupt_cutoff`
        - `accepted_turn -> first_audio`
        - dedicated semantic judge lane
    - 设备侧剩余重构优先级现调整为：
      - P0：downlink / playback 真相链与恢复模型
      - P1：`dialog runtime` 唯一真相源收口
      - P2：runtime-ready duplex / capture / AEC gate
      - P3：统一 turn timeline 与板端回归基线
      - P4：duck-first / keep-listening 行为优化
  - newest landed runtime-ownership slice:
    - XiaoZhi transport 侧已移除冗余诊断 shadow
      `last_playback_meta_valid`
    - transport playback-meta 的 `valid=` 现在直接由 cached meta context
      推导：
      - `last_response_id`
      - `last_playback_id`
      - `last_segment_id`
    - `last_playback_is_last_segment` 继续只表示最近一条 meta 的 event-local
      fact，不再兼任 validity 总开关
  - newest landed runtime-ownership slice:
    - XiaoZhi playback runtime 已移除残留的
      `xiaozhi_playback_meta_valid` shadow
    - 当前 playback meta 是否有效，现只由 typed segment context 提供：
      - `response_id`
      - `playback_id`
      - `segment_id`
    - `current_meta_is_last_segment` 与 playback dump 的 `valid=` 都不再经过
      额外的 coarse bool 中转
  - newest landed runtime-ownership slice:
    - cloud/dialog runtime snapshot 已正式导出 `playback_turn_active`
    - `dialog_runtime` 的 retained output-turn fallback 不再只看
      `playback_lane_engaged`
    - lane 已释放但 turn 仍 active 时，只有：
      - `output_lane=speaking`
      - 且未进入 suppress-speaking 的 terminal wait
      才继续保留 speaking/output-turn
    - `output_turn_quiesced` 也已显式要求
      `playback_turn_active=no`
  - newest landed runtime-ownership slice:
    - `playback_turn_active()` 已不再直接读取 coarse
      `xiaozhi_playback_meta_valid`
    - retained playback turn 现在改由：
      - playback lane engaged
      - terminal 仍 open 且 response/playback context 有效
      共同决定
    - `playback_note_meta()` 现在会在写入 queue / wait / terminal context 后立即刷新
      playback phase，缩短 meta 到 snapshot 的 stale phase 窗口
  - newest landed runtime-ownership slice:
    - XiaoZhi playback runtime 已移除残留的 coarse global
      `xiaozhi_playback_last_segment` shadow bool
    - 当前 meta 的 terminal 语义现在改由：
      - event-local `is_last_segment`
      - stored terminal last-segment context
      共同提供
    - queued segment 的 `is_last_segment` 也已直接写入 event fact，不再经过
      额外 runtime shadow bool 中转
  - newest landed runtime-ownership slice:
    - XiaoZhi playback runtime 已显式抽出 typed downlink supply truth：
      - `CURRENT_SEGMENT`
      - `WAITING_NEXT_SEGMENT`
      - `TERMINAL_TAIL`
      - `NONE`
    - `maybe_rebuffer_starved()` 不再直接读取 coarse global
      `xiaozhi_playback_last_segment`
    - terminal tail / 无供给真相窗口现在会直接清掉 starvation watch，只让
      “当前段仍在消费”或“明确等待下一段”继续进入 upstream-starved 判定
  - newest landed runtime-ownership slice:
    - playback runtime 已独立保存 waiting-segment context：
      - `response_id`
      - `playback_id`
      - `segment_id`
    - `WAITING_SEGMENT` 不再由 `meta_valid + !last_segment` 这组 global shadow
      推导
    - non-terminal meta 会写入待续段上下文，terminal/invalid meta 会清掉它
  - newest landed runtime-ownership slice:
    - playback start-gate 的 predictive segment-prefetch 已直接读取
      queue 头段/当前待播 segment
    - `segment_prefetch_target_needed()` 不再继续把
      `meta_valid/last_segment` 当成“当前待播段”真相
    - 这继续把起播门限策略从 coarse global meta shadow 收口到
      runtime-owned segment queue truth
  - newest landed runtime-ownership slice:
    - playback runtime 已独立保存 terminal last-segment context：
      - `response_id`
      - `playback_id`
      - `segment_id`
    - `completed_ready` / terminal wait 不再把“当前最近一条 meta 的
      segment_id”当成最后一段真相
    - completed ACK 也会优先消费这份 terminal response/playback context，
      进一步减少 terminal close 被全局 shadow 扰动
  - newest landed runtime-ownership slice:
    - playback ACK / terminal 路径已开始拆分：
      - response-level context
      - segment-level context
    - `started/mark` 不再被全局 `meta_valid` 粗短路
    - `cleared/completed` 也不再错误依赖当前 `segment_id` 仍然挂在全局 meta 上
  - newest landed runtime-ownership slice:
    - `write_failed` rebuffer 不再默认走同一套 `stop_rebuffer`
    - 本地写链路失败现在优先 attached `service_recover`
    - 上游 starved 仍优先 stop/fresh-start，避免把两类问题继续折叠成同一套
      stop/restart 风暴
  - newest landed runtime-ownership slice:
    - downlink worker 现在会先让 rebuffer gate 决定 attached resume /
      detached fresh-start
    - `OWNED_RECOVERING` 不再在真正 `finish_playback_rebuffer()` 前就把 loop
      提前短路
    - rebuffer 恢复链现在会在 gate 后刷新一次 backend truth，再进入 start/wait
      分支
  - newest landed runtime-ownership slice:
    - `playback_backend_state()` 不再只因 phase 仍是 `rebuffering` 就无条件
      投影 `owned_recovering`
    - rebuffer phase 与 backend attached/owner truth 继续拆开
    - stop-after-rebuffer 之后若 backend 已 detached，downlink resume 现在能
      正确识别 fresh-start 条件
  - newest landed runtime-ownership slice:
    - XiaoZhi playback runtime 已显式拆出：
      - `playback_turn_active()`
      - `playback_has_work()`
    - retained playback turn 与 downlink worker live-ness 不再共用
      `lane_engaged/meta_valid` 这套粗粒度判定
    - downlink worker / transport poll / interrupt/config/window gate 现在各自
      消费对应层级的 playback truth
  - newest landed runtime-ownership slice:
    - XiaoZhi playback runtime 现在会把 `rebuffering` 也视为 quiet window
    - 重缓冲静默期不再因为 `lane_engaged` 继续错误 hold capture/VAD
    - 这继续把“response 仍在继续，但当前无真实输出”的阶段从 generic
      playback-held 语义里拆出来
  - newest landed runtime-ownership slice:
    - server `output_state=speaking` 不再在 `prefetching/idle` 阶段直接投影成
      cloud duplex 的 speaking output
    - `output_speaking_active()` 现在只会在 runtime phase 仍保留 output turn 时，
      才接受 transport-side speaking state
    - 这继续减少了首段预取窗口里 soft-endpoint / uplink continuation 被
      transport 语义抢跑的问题
  - newest landed runtime-ownership slice:
    - `tts_start` 不再只凭预判的 duplex/AEC fallback 就立即关闭本地 round
    - 现在只有当 playback runtime 当前确实 `capture_held=yes` 时，才会执行
      server-response close
    - `playback_started` 也新增了对应 policy，把 half-duplex 关轮延后到真实
      起播并开始 hold capture 的时刻
  - newest landed runtime-ownership slice:
    - XiaoZhi playback runtime 现在会把 `prefetching` 也视为 quiet window
    - `playback_allows_vad_open()` 与 `capture_held_by_playback()` 不再把
      prefetch-only lane occupancy 当成 generic playback hold
    - 这继续把 pre-start 窗口从粗粒度 `lane_engaged` 语义里拆出来，避免尚未
      出声就继续卡住 capture/VAD
  - newest landed runtime-ownership slice:
    - `tts_stop_pending` 后只要本地已无真实有声输出，playback runtime 现在就会把
      该窗口视为 quiet window
    - `playback_allows_vad_open()` 与 `capture_held_by_playback()` 不再让这种
      terminal tail wait 继续走 generic playback-held 语义
    - 这继续把 capture hold 从 lane occupancy 收口到真实媒体输出真相
  - newest landed runtime-ownership slice:
    - `tts_stop_pending` 建立后，detached backend 上残留的 downlink 队列
      不再由 worker 间接复活
    - playback runtime 现在会主动丢弃这部分 detached residual audio，并继续走
      terminal completed 收口
    - downlink worker 也不再在 stop_pending 期间 fresh-start 非
      `owned_active` backend
  - newest landed runtime-ownership slice:
    - XiaoZhi playback 的 `playback_output_active` 不再只由
      `playing/draining` phase 直接投影
    - 现在要求 phase 仍位于 output-active 窗口，且 backend 仍保持
      `owned_active`
    - 这继续把 cloud/dialog/duplex 看到的“还在播”真相从 phase-only 收口到
      playback runtime 自己的 backend owner truth
  - newest landed runtime-ownership slice:
    - XiaoZhi playback runtime 已把 `backend_attached` 与“媒体 active”语义
      明确拆开
    - stop/abort/pause 等 control path 不再沿用模糊的 `backend_owned`
      命名
  - newest landed runtime-ownership slice:
    - `dialog_runtime` 的 managed playback-error 吸收与本地 no-op 吸收，
      现在都改为依赖 `cloud_runtime_available`
    - `owned_paused` 也被视为受控 playback owner 过渡真相
  - newest landed runtime-ownership slice:
    - `dialog_runtime` 的 `playback_lane_engaged` 不再只因
      `phase unknown` 就默认保留 output ownership
    - lane fallback 现在也只在拿不到 cloud runtime snapshot 时才保留
  - newest landed runtime-ownership slice:
    - `dialog_runtime` 现在只会在拿不到 cloud runtime snapshot 时，才让
      本地 playback shadow 回填 `playback_active/recovering`
    - `phase unknown` 本身不再构成 local shadow 介入常态派生的理由
  - newest landed runtime-ownership slice:
    - `dialog_runtime` 不再在 `phase unknown` 下让本地 playback shadow
      旁路清理 `tts_interrupt_requested`
    - cloud snapshot merge 与本地 playback idle 都统一收口到
      `output_turn_quiesced` 这条派生真相
  - newest landed runtime-ownership slice:
    - XiaoZhi `backend_state` 不再因为 phase 仍是 `playing/draining`，
      就在 playback-service 已不活跃时继续投影 `owned_active`
    - backend occupancy 与 media/output phase 继续拆开，各自保持独立真相
  - newest landed runtime-ownership slice:
    - `dialog_runtime` 已开始把 `playback_backend_state_kind=owned_paused`
      视为 managed playback transition
    - 本地 `playback_error` 若落在 XiaoZhi backend 自己的 pause/detach 窗口，
      不再轻易放大成独立 `error_recovering`
  - newest landed runtime-ownership slice:
    - XiaoZhi playback backend truth 新增 `owned_paused`
    - `backend_state()` 现在会同时吸收：
      - playback runtime phase
      - playback-service 当前 stream owner
    - downlink/start 不再把“本地 backend 还在 pause/detach 过渡”直接当成
      detached/foreign 去 fresh-start
  - newest landed runtime-ownership slice:
    - `dialog_runtime.snapshot.playback_active` 在 phase 已知时不再因
      `playback_lane_engaged=yes` 自动保持为 `true`
    - `prefetching` / `waiting_segment` 这类无真实媒体输出的阶段不再投影成
      core 侧 playback-active
    - `playback_error` 的 managed-recovery 判定也不再把 generic lane
      occupancy 当成证据
  - newest landed runtime-ownership slice:
    - `dialog_runtime` 的 output turn 不再因
      `playback_lane_engaged=yes` 就自动视为还在“说话”
    - phase 已知时，当前只让：
      - `playback_active`
      - `playback_recovering`
      - `rebuffering`
      继续保留 output-turn ownership
    - `prefetching` / `waiting_segment` 这类无媒体 backing 的 lane 占用不再
      直接投影成 `speaking` / `barge_in_listening`
  - newest landed runtime-ownership slice:
    - `dialog_runtime` 的本地 playback 入口不再分成：
      - 一次只更新 `local_playback_stream_owned`
      - 再单独一次吸收 cloud snapshot + local playback state
    - 本地 playback listener 现在在同一 reducer 内完成：
      - ownership 识别
      - cloud snapshot merge
      - local playback shadow 更新
      - aggregate interaction 派生
    - 这继续减少了 `dialog runtime` 里“局部状态先抢跑，再等后续
      cloud sync 修正”的窗口
  - newest landed runtime-ownership slice:
    - `prefetch_segment` 起播门限现在不再只依赖历史 `meta_gap`
    - 当当前 `audio.out.meta` 已有效、当前段不是最后一段、且预测
      `prefetch_target_ms` 已明显高于基线预算时，也会直接进入
      `PREFETCH_SEGMENT`
    - 这让首次起播和段间恢复在尚未积累历史断供前，也能直接消费当前
      `expected_duration_ms` 建立更保守的预取预算
  - newest landed runtime-ownership slice:
    - XiaoZhi downlink `write_failed` 现在统一先走：
      - `stop_stream_ex(...)`
      - runtime-owned `rebuffer_pause`
    - 不再按 `recover_cause` 把普通 `write_failed` 默认分流回
      playback-service `recover`
    - `recover_stream_ex(...)` 现只保留为 stop 失败时的兜底语义
    - 这一步继续把 playback backend 的恢复入口收口到单一 `stop/rebuffer`
      真相链
  - newest landed runtime-ownership slice:
    - `dialog_runtime` 内部 local playback shadow 已从：
      - `playback_state`
      - `playback_local_active`
      - `playback_local_recovering`
      收成单一 private `playback_state`
    - phase-unknown fallback 与 diagnostics dump 现在都按需从
      `playback_state` 派生 active/recovering
    - 这一步继续压缩了 runtime 内部重复缓存的本地 playback 事实，减少内部阴影态
      漂移
  - newest landed runtime-ownership slice:
    - playback runtime 现在会把本地 playback-service 的
      `RIVER_PLAYBACK_RECOVERING` 上推成 cloud-owned backend truth：
      - `RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_RECOVERING`
    - XiaoZhi downlink worker 在 `owned_recovering` 时不再误走 fresh start
    - `dialog_runtime` 的 `playback_recovering` 常态也开始直接消费：
      - `owned_recovering`
      - `restart_pending`
    - 这一步继续把 recovering 语义从本地 playback listener shadow 推进到
      playback runtime 自己导出的 typed truth
  - newest landed runtime-ownership slice:
    - XiaoZhi playback 在 `upstream_starved` 场景下已不再默认走同轨
      `recover`
    - `upstream gap rebuffer` 与 starved `write_failed` 现在都会优先执行：
      - `stop_stream_ex(...)`
      - runtime-owned `rebuffer_pause`
    - 只有在本地 stop 自身失败时，才会回退到 playback-service recover
    - 这一步把常见上游断粮从本地 `RECOVERING/flush-restart` 语义里拆开，
      让 residual 硬 `write_failed` 才继续保留 recover 语义
  - newest landed runtime-ownership slice:
    - `dialog_runtime` 现在已显式吸收 `wake_admission_pending`
    - wakeword detection / admission gating 会直接把
      `wake_admission_pending` 当成阻断真相
    - `wake_admission` bridge 在 queued / accepted / failed 转移上同步维护这条
      runtime truth
    - 这一步继续把 wake admission 从“bridge 内部局部 pending/retry 状态”
      推进到“runtime 也显式知道已有待处理 wake”，从而减少重复检测和被动 coalesce
  - newest landed runtime-ownership slice:
    - `session_coordinator` 已不再直接读取
      `river_voice_kws_wake_handoff_block_reason()`
    - `wake_admission_submit()` 现在会先吸收 KWS handoff debug blocker，
      再吸收 `dialog_runtime` 的 wakeword admission blocker
    - wakeword 的 blocked / queued / coalesced 日志也已统一收口到
      `wake_admission` 入口
    - 这一步继续把唤醒接力从“协调器先做一层 KWS 私有判定、准入桥再做一层
      runtime 判定”，收口成 core-owned 的单一提交边界
  - newest landed runtime-ownership slice:
    - `dialog_cloud_port` 上已无消费者的 `conversation_window_active`
      侧门已被删除
    - app 装配层也已同步移除对应 callback wiring
    - `conversation_window` 的读取真相现继续只归 `dialog_runtime` 所有
    - 这一步继续压缩了 cloud-port 上残留的旧查询侧门，让 port 更接近纯 command
      ingress 边界
  - newest landed runtime-ownership slice:
    - wakeword detection 与 wake admission 现在共享同一条
      `dialog_runtime` 阻断真相
    - `dialog_runtime` 已导出：
      - `river_dialog_runtime_wakeword_detection_block_reason()`
      - `river_dialog_runtime_allows_wakeword_detection()`
    - `river_voice_kws_detection_allowed()` 已不再自行拼
      `conversation_window + interaction_state`
    - 这一步继续把 wakeword gating 从 voice/KWS 层的 raw 条件判断，
      收口到 `dialog runtime` 的统一结论
  - newest landed runtime-ownership slice:
    - `dialog_runtime` 现在已把 `tts_interrupt_requested` 的清理条件统一收口到：
      - `output_turn_quiesced`
    - 本地 playback idle 回调与 cloud snapshot sync 不再各自拼一套 raw 清理条件
    - cloud sync 现在也会先刷新 playback 派生，再决定是否清 interrupt latch
    - 这一步继续把 interrupt-clear policy 从分裂的边缘条件判断，
      收口到同一个 runtime owner 派生
  - newest landed runtime-ownership slice:
    - `dialog_runtime` 现在已显式区分：
      - 有声 playback active
      - output turn 仍被占用、但当前处于 silent gap
    - `waiting_segment` 不再仅因 `playback_lane_engaged` 就继续把
      `playback_active` 维持为 `true`
    - `river_dialog_runtime_allows_barge_in_interrupt()` 与 interaction 派生
      已改为依赖新的 output-turn-engaged 语义，而不是把 silent gap 伪装成
      `playback_active`
    - 这一步继续把 `dialog runtime` 从“用错误的 active 投影维持 barge-in”推进到
      “显式区分 audible playback 与 response turn ownership”
  - newest landed runtime-ownership slice:
    - `waiting_segment` 不再只是一条诊断/phase 真相，也开始直接参与 duplex /
      capture 语义
    - XiaoZhi playback runtime 现在会把段间静默视为：
      - response 尚在继续
      - 但本地 VAD / capture / soft-endpoint 已可重新打开
    - `river_cloud_xiaozhi_playback_allows_vad_open()` 在 `waiting_segment`
      时直接返回 `true`
    - `river_cloud_xiaozhi_capture_held_by_playback(...)` 在
      `waiting_segment` 时直接返回 `false`，并清空 fallback reason
    - 这一步继续把 downlink/playback 从“lane 仍 engaged 就继续 hold capture”的
      粗粒度语义，推进到“显式区分段间静默和真正的播放/AEC 阻断”
  - newest landed runtime-ownership slice:
    - playback owner 新增了显式段间等待 phase：
      - `RIVER_CLOUD_PLAYBACK_PHASE_WAITING_SEGMENT`
      - `waiting_segment`
    - XiaoZhi downlink/playback runtime 现在会在“当前 segment 已播完、下一段尚未到达”
      时主动 pause backend，并把 phase 收口到 `waiting_segment`
    - 这让 inter-segment 供给间隙不再默认继续滑入：
      - `underrun`
      - `playback_write_failed`
      - `rebuffer requested`
      这条本地恢复路径
    - 这一步继续把 downlink/playback 的恢复模型从“被动等本地故障”推进到
      “先显式承认上游段间等待”
  - newest landed runtime-ownership slice:
    - playback owner 的 `backend state` 已从两个跨层布尔投影收口成公共
      typed truth：
      - `river_cloud_playback_backend_state_t`
      - `playback_backend_state_kind`
    - `dialog_runtime` 的公开 snapshot 已删除：
      - `playback_backend_owned`
      - `playback_backend_restart_pending`
      并改为直接吸收 `playback_backend_state_kind`
    - XiaoZhi downlink/playback runtime 的 backend ownership / restart-pending
      判断现在直接用公共 enum 维护，不再保留私有 backend-state 类型
    - 这一步继续把 downlink/playback backend ownership 真相从跨层布尔投影，
      收口到公共 owner typed truth
  - newest landed runtime-ownership slice:
    - playback owner 的 `phase` 已从 XiaoZhi 私有 enum 提升成公共
      typed truth：
      - `river_cloud_playback_phase_t`
      - `playback_phase_kind`
    - `dialog_runtime` 的公开 snapshot 已删除字符串：
      - `playback_phase`
      并改为直接吸收 `playback_phase_kind`
    - XiaoZhi downlink/playback runtime 的 `phase` 现在直接用公共 enum 维护，
      不再保留私有 playback-phase 类型
    - 这一步继续把 downlink/playback 的 phase 真相从私有 enum / 跨层文本字段，
      收口到公共 owner typed truth
  - newest landed runtime-ownership slice:
    - playback owner 的 `terminal state` 已从内部字符串真相提升成公共
      typed truth：
      - `river_cloud_playback_terminal_state_t`
      - `playback_terminal_state_kind`
    - `dialog_runtime` 的公开 snapshot 已删除字符串：
      - `playback_terminal_state`
      并改为直接吸收 `playback_terminal_state_kind`
    - XiaoZhi downlink/playback runtime 的 terminal reset / ack 映射 /
      local fallback 已全部收口到公共 enum
    - 这一步继续把 downlink/playback 的 terminal 语义从内部字符串与跨层文本
      投影，收口到公共 typed truth
  - newest landed runtime-ownership slice:
    - playback owner 的 `start policy` 已从 XiaoZhi 私有 enum 提升成公共
      typed truth：
      - `river_cloud_playback_start_policy_t`
      - `playback_start_policy_kind`
    - `dialog_runtime` 的公开 snapshot 已删除字符串：
      - `playback_start_policy`
      并改为直接吸收 `playback_start_policy_kind`
    - 这一步继续把 downlink/playback 的启动门限决策从私有 owner enum 和跨层文本
      收口到公共 typed truth
  - previous runtime-ownership slice:
    - playback owner 的 `rebuffer cause` 已从 XiaoZhi 私有 enum 提升成公共
      typed truth：
      - `river_cloud_playback_rebuffer_cause_t`
      - `playback_rebuffer_cause_kind`
    - `dialog_runtime` 的公开 snapshot 已删除字符串：
      - `playback_rebuffer_cause`
      并改为直接吸收 `playback_rebuffer_cause_kind`
    - 这一步继续把 downlink/playback 的恢复语义从私有 owner enum 和跨层文本字段
      收口到公共 typed truth
  - previous runtime-ownership slice:
    - `dialog_runtime` 的公开 snapshot 已删除：
      - `playback_state`
      - `playback_local_active`
      - `playback_local_recovering`
    - 本地 playback shadow 现在只保留在 runtime 内部，用于：
      - stream ownership ingress
      - phase-missing fallback
      - diagnostics dump
    - 这一步继续把对外 `dialog runtime` 真相面收口到 typed owner truth，而把
      local playback shadow 明确降为内部实现细节
  - previous runtime-ownership slice:
    - `dialog_runtime` 已把剩余 local playback shadow 的：
      - active fallback
      - recovering fallback
      收口到显式 `phase unknown` helper
    - `compute_playback_active_locked()` /
      `compute_playback_recovering_locked()` 不再直接把
      `playback_local_*` 混入 phase-known 常态派生
    - interrupt-clear 的 local shadow 阻断路径也已复用同一 fallback helper
    - 这一步继续把 local playback shadow 压回：
      - purely-diagnostic local signal
      - phase-missing fallback
  - previous runtime-ownership slice:
    - cloud playback runtime 已把 terminal wait 从 generic bool/text 提升成
      typed truth：
      - `playback_terminal_wait_kind`
    - XiaoZhi owner 直接输出：
      - `await_last_segment_meta`
      - `await_segment_queue_drain`
      - `await_last_segment_tail`
    - `dialog_runtime` 已开始直接消费这条 typed wait truth，只把：
      - queue drain
      - last segment tail
      当成真正的尾段等待；`await_last_segment_meta` 不再被误压成尾段静默
      投影
    - 这一步继续减少了 core 对 generic `terminal_waiting` 的过度二次解释
  - previous runtime-ownership slice:
    - 当 `playback_phase_known` 已成立时，`dialog_runtime` 不再让
      `playback_local_active` 阻断 interrupt latch 清理
    - local playback shadow 现在进一步退回到 `phase unknown` 的兜底路径
    - 这一步继续减少了 core 行为层对本地 playback listener shadow 的常态依赖
  - previous runtime-ownership slice:
    - `dialog_runtime` 已删除 app 侧 dialog playback stream 注册依赖
    - dialog playback ingress 现在直接按 `RIVER_PLAYBACK_PRIO_TTS` 识别
    - `river_app_boot()` 不再装配 `xiaozhi_tts` / `iflytek_tts` 白名单
    - 这一步继续把 dialog playback ownership truth 从 app wiring 收回 runtime
  - previous runtime-ownership slice:
    - `dialog_runtime` 的 playback ingress 现在会先抓取当前 cloud runtime
      snapshot，再吸收本地 playback state callback
    - `managed recovery` / `interrupt latch clear` / interaction 派生现在都基于
      同一时刻的 playback owner truth，而不是前一次残留的 cloud playback 视图
    - 这一步继续削弱了本地 `RIVER_PLAYBACK_*` listener 作为 aggregate truth
      source 的地位
  - created a dedicated runtime re-architecture track for the recurring
    device-side latency / playback churn issues:
    - architecture review:
      - `doc/VOICE_RUNTIME_ARCHITECTURE_REVIEW_ZH_2026-04-17.md`
    - active execution plan:
      - `doc/VOICE_RUNTIME_REARCHITECTURE_EXECUTION_PLAN_ZH.md`
  - first landed slice on that plan:
    - added a core-owned dialog cloud port so `river_voice` no longer calls
      `river_cloud_*` directly
    - hardened XiaoZhi uplink pacing with:
      - retry-preserved in-flight frame ownership
      - bounded burst drain
      - round-level pacing metrics
  - second landed slice on that plan:
    - introduced a core-owned `dialog runtime` truth source
    - moved boot / cloud-state / playback / ASR interaction-state derivation
      behind that runtime
    - removed the old `session_coordinator` coarse phase truth source
    - added a generic cloud runtime snapshot export so `river_core` no longer
      reaches into XiaoZhi runtime details to derive state
  - third landed slice on that plan:
    - extracted XiaoZhi downlink / playback media logic into a dedicated
      `river_cloud_xiaozhi_playback_runtime.c`
    - moved playback meta ownership, ACK progress, rebuffer/retry handling,
      decoder/downlink worker logic, and playback start policy behind exported
      runtime APIs
    - `river_cloud_adapter.c` now consumes the playback runtime boundary
      instead of embedding duplicated playback/downlink implementations
  - fourth landed slice on that plan:
    - playback runtime now owns:
      - `output_active`
      - `has_work`
      - `abort`
      terminal semantics for XiaoZhi playback
    - adapter close/interrupt/config-refresh branches no longer assemble
      playback stop/reset logic from raw flags and ring/meta checks
  - fifth landed slice on that plan:
    - playback runtime now also owns the remaining playback-local helper set:
      - playback meta clear
      - stop cancel / stop arm
      - playback start / reset
      - downlink reset
      - duplex-ready playback note
    - `river_cloud_xiaozhi_session.c` no longer implements playback-owned
      reset/meta/stop helpers
    - follow-up timeout and XiaoZhi interrupt admission now key off runtime
      playback predicates instead of direct raw playback fields
  - sixth landed slice on that plan:
    - playback runtime now distinguishes a sustained upstream supply gap from a
      later hard write failure one step earlier
    - empty-downlink starvation is converted into a controlled
      `xiaozhi_playback_starved` rebuffer path after a bounded timeout instead
      of waiting for the AudioTrack path to fail first
    - starvation watch state is reset together with playback/downlink runtime
      restart points so the recovery path stays deterministic
  - seventh landed slice on that plan:
    - playback service now exposes an explicit `recovering` state instead of
      collapsing write-path churn into fatal `playback_error`
  - latest landed runtime-ownership slice:
    - session runtime now owns the full XiaoZhi transport-event dispatch shell:
      - per-event `refresh_turn_semantics("event")`
      - grouped event switch / reducer routing
    - adapter `river_cloud_xiaozhi_event_handler(...)` is now reduced to a
      transport callback shim that only forwards `river_xiaozhi_event_t`
      objects into runtime
  - previous runtime-ownership slice:
    - cloud playback runtime snapshot 已继续补齐 backend ownership truth：
      - `playback_backend_owned`
      - `playback_backend_restart_pending`
    - `dialog_runtime` 已开始直接消费这两条 typed truth
    - local `IDLE` 清 interrupt latch 与 managed recovery 分类都已开始受
      backend restart-pending 保护
    - 这一步继续降低了 core 对本地 playback listener 瞬时边缘事件的依赖
  - previous runtime-ownership slice:
    - `dialog_runtime` 已不再依赖 playback phase 字符串做行为判断
    - `"playing" / "draining" / "rebuffering"` 这些文本现在只保留给日志投影
    - playback active / recovering / managed-recovery 派生已进一步只消费 typed
      playback truth
    - 这意味着 core 行为层已继续远离：
      - `strcmp(playback_phase, "...")`
      这类 owner 外字符串解释
  - previous runtime-ownership slice:
    - cloud playback runtime snapshot 新增 typed bool：
      - `playback_phase_known`
      - `playback_terminal_closed`
    - `dialog_runtime` 现在直接吸收并消费这两条 playback owner truth
    - core 侧对：
      - `playback_phase[0] != '\0'`
      - `playback_terminal_state[0] != '\0'`
      这类字符串侧推已开始退场
    - `allows_barge_in_interrupt()` 与 runtime status dump 也已开始转向 typed
      playback truth
  - previous runtime-ownership slice:
    - `dialog_runtime` 已删除公开的 playback-state 侧门：
      - `river_dialog_runtime_note_playback_state(...)`
    - 本地 playback 真相现在继续只经由：
      - `river_dialog_runtime_on_playback_state(...)`
      这条 listener ingress 进入 runtime
    - 本地 `RIVER_PLAYBACK_IDLE` 也不再无条件清掉：
      - `tts_interrupt_requested`
    - 若 cloud playback 真相仍显示 lane engaged / rebuffer / stop-pending /
      terminal-waiting，interrupt latch 会继续保留，直到聚合 playback truth
      真正收口
    - 这一步继续压缩了“本地 playback service 瞬时边缘事件直接改写 runtime”的范围
  - previous runtime-ownership slice:
    - `dialog_runtime` 已删除未再使用的外部错误入口：
      - `river_dialog_runtime_note_error(...)`
      - `river_dialog_runtime_clear_error(...)`
    - coarse `error_recovering` 语义不再允许从 runtime 外部被随意直接改写
    - 后续应继续：
      - 收紧 local playback edge event 对 runtime 聚合态的影响
      - 继续让 playback/downlink owner 自己输出 typed truth
  - previous runtime-ownership slice:
    - `dialog_runtime` 现在在 managed playback recovery 中保留
      `tts_interrupt_requested`
    - 本地 `RIVER_PLAYBACK_ERROR` 若只是 playback runtime 管理下的恢复路径，
      不会再把 interrupt in-flight latch 提前清空
    - 这进一步避免了 recovery 窗口里的重复 barge-in interrupt
    - 下一步应继续：
      - 让更多本地 playback service 边缘信号只作为 playback runtime 内部细节
      - 继续把 stop/reset/rebuffer 的 coarse 解释从 core 侧挤掉
  - previous runtime-ownership slice:
    - `dialog_runtime` 现在会把“受 cloud playback runtime 管理的本地
      playback_error”吸收到 `playback_recovering`
    - 若 cloud playback truth 已表明当前仍在：
      - rebuffering
      - playback_cloud_active
      - playback_lane_engaged
      之一，则不再额外把交互态抬成 `error_recovering`
    - `river_dialog_runtime_on_playback_state()` 也不再对每次
      `RIVER_PLAYBACK_ERROR` 无条件叠加：
      - `note_error("playback_error")`
    - 这一步直接针对日志里频繁出现的：
      - `speaking -> error_recovering -> speaking`
      状态风暴做收口
    - 该步之后，`speaking -> error_recovering -> speaking` 这类风暴已继续被压缩
  - previous runtime-ownership slice:
    - `river_cloud_xiaozhi_dump_session_status()` 现在会先读取
      `river_cloud_runtime_snapshot_t`
    - session status dump 中的：
      - playback active/phase/rebuffer/tts_stop_pending
      - local_close/window remaining
      - turn semantics
      已开始统一走 runtime snapshot 投影，而不是日志路径自己重拼
    - `xiaozhi_session.c` 已去掉对：
      - `g_river_cloud.xiaozhi_tts_stop_pending`
      的最后一处 session-side 直读
    - 该步之后，XiaoZhi session 诊断面也已开始贴近 runtime snapshot 真相源
  - previous runtime-ownership slice:
    - wake admission 的 queued/coalesced/deferred/retry worker 已从
      `session_coordinator` 抽到新的 core-owned bridge：
      - `river_dialog_wake_admission`
    - 新 bridge 直接拥有：
      - block-reason 检查
      - pending/retry worker
      - inline fallback
    - `session_coordinator` 已不再持有：
      - wake admission task
      - sema/mutex
      - pending/deferred/confidence/text
    - 这一步继续把 coordinator 压回：
      - wake handoff gate
      - ASR 文本 fanout
      的薄壳
    - 该步之后，coordinator 中的 wake admission worker 已完成下沉，后续主焦点
      已切回 downlink/playback runtime
  - previous runtime-ownership slice:
    - `dialog_cloud_port` 现在会在
      `begin_conversation_window(source)` 成功后，立即把 wake admission success
      同步写入 `dialog runtime`
    - source -> runtime reason 的归一已集中在 port 内：
      - `wakeword -> wakeword_detected`
    - `session_coordinator` 已删除 wake admission success 路径上的手工：
      - `river_dialog_runtime_note_wake_confirmed_with_cloud_state(...)`
    - 这让：
      - cloud begin_conversation_window
      - dialog runtime wake_confirmed note
      成为同一条 core-owned 原子 ingress
    - 该步之后，wake admission success side effects 已原子化，为继续下沉
      worker/runtime ownership 铺平了边界
  - previous runtime-ownership slice:
    - `dialog_cloud_port` 已补齐：
      - `begin_conversation_window(const char *source)`
    - `river_app` 已将 wake admission 入口绑定到 concrete adapter，但
      `session_coordinator` 本身不再直接调用
      `river_cloud_adapter_begin_conversation_window()`
    - wakeword worker / fallback 路径现在统一通过：
      - `river_dialog_cloud_begin_conversation_window("wakeword")`
    - 这让 core 层剩余的 wake admission cloud ingress 也开始与：
      - ASR audio open/push
      - batch submit
      - interrupt_tts
      共享同一条 stable port 边界
    - 该步之后，wake admission ingress 已统一走 stable port 边界，为后续继续
      收口成功 side effects 和 worker shell 做准备
  - previous runtime-ownership slice:
    - `dialog runtime` 现在继续吸收本地 `interrupt_tts` in-flight 真相：
      - snapshot 新增 `tts_interrupt_requested`
      - `allows_barge_in_interrupt()` 现同时检查：
        - `tts_stop_pending`
        - `tts_interrupt_requested`
    - `dialog_cloud_port` 在 interrupt 成功后会立刻把本地 interrupt 请求锁存进
      `dialog runtime`，减少 cloud stop-pending 回流前的重复中断窗口
    - `session_coordinator` 已删除：
      - `barge_in_interrupt_requested`
      - `state_lock`
    - ASR 文本触发的 barge-in interrupt 改为消费 `dialog_cloud_port` +
      `dialog runtime` policy，不再由 coordinator 自己维护本地 latch
  - previous runtime-ownership slice:
    - `dialog runtime` 现在继续直接吸收 XiaoZhi round/window typed truth：
      - `listen_stop_pending`
      - `local_close_pending`
      - `conversation_window_remaining_ms`
      - `local_close_remaining_ms`
    - cloud round 仍处于：
      - listening
      - stream active
      - listen-stop pending
      - local-close pending
      - input committed/active
      之一时，`dialog runtime` 会继续维持 `asr_session_active`
    - wakeword admission block reason 已开始优先输出：
      - `conversation_window_active`
      - `cloud_local_close_pending`
      - `cloud_listen_stop_pending`
    - `dialog runtime` status dump 也直接打印 stop-pending / local-close /
      round/window remaining truth，便于后续继续收口 coordinator bridge
  - previous runtime-ownership slice:
    - XiaoZhi round runtime 已继续接管 round 启动入口：
      - `open_session_and_listen()`
      - `start_followup_round()`
      - `maybe_start_followup_round()`
      - `begin_conversation_window()`
    - `wake admission` / `follow-up reopen` / `open-listen` 的 round 启停路径
      不再在 `xiaozhi_session.c` 内联维护
    - 下一步应继续把 round/window typed truth 更直接投影到
      `dialog runtime`，减少 core 再从 cloud snapshot 做二次解释
  - previous runtime-ownership slice:
    - XiaoZhi round runtime 继续接管：
      - open-and-listen session policy
      - post-commit wait / local-close defer
      - local round close / timeout
      - transport reset / window timeout
      - ASR session started / closed emit
    - `xiaozhi_session.c` 进一步只保留：
      - transport control
      - semantic/turn state
      - uplink/capture 主流程
    - 下一步应继续把：
      - wake admission
      - follow-up reopen
      - open/listen round 启停写路径
      收成更少的 round runtime / dialog runtime typed 入口
  - previous runtime-ownership slice:
    - `listening/window/listen_stop/local_close` 这组 XiaoZhi round 辅助状态
      已从 `river_cloud_xiaozhi_session.c` 拆到新的
      `river_cloud_xiaozhi_round_runtime.c`
    - `xiaozhi_session.c` 继续收缩为：
      - transport event reducer
      - semantic/turn state
      - uplink 主流程
    - 这为后续把 round/window 真相继续接到 dialog runtime 铺平边界
  - previous runtime-ownership slice:
    - `river_dialog_cloud_conversation_window_active()` 已优先读取
      `dialog runtime` snapshot
    - voice/KWS 侧 follow-up window 判定开始由 dialog runtime 对外提供
    - cloud adapter callback 退为 runtime 不可用时的 fallback
  - older runtime-ownership slice:
    - session 已不再直读：
      - `xiaozhi_playback_rebuffer_pending`
      - `xiaozhi_playback_active`
    - transport reset / session start / playback start 这些路径对
      `no_ref reopen` 的 reset 也统一走 playback runtime
    - `xiaozhi_session.c` 进一步只消费 typed playback helper
  - older runtime-ownership slice:
    - `endpoint_soft_close` 状态机已并入 playback runtime
    - session 现在只通过 typed helper 读取：
      - pending
      - remaining_ms
      - reason
    - session_close / transport_reset 这类路径也不再手工清空这组字段
  - older runtime-ownership slice:
    - `no-ref reopen/open_hold` gating now sits with playback runtime:
      - `open_hold_frames_required()`
      - `playback_followup_reopen_ready()`
    - `maybe_start_followup_round()` no longer carries the reopen guard/rearm
      state machine inline
    - this removes another cross-file split-brain between playback state writes
      and session-side reinterpretation
  - older runtime-ownership slice:
    - playback runtime now also owns:
      - `duplex_soft_endpoint_enabled()`
      - `duplex_speaking_uplink_continuation_active()`
      - local `output_speaking_active()`
    - this keeps speaking-output / soft-endpoint continuation semantics on the
      downlink runtime side instead of in `xiaozhi_session.c`
  - older runtime-ownership slice:
    - playback-side duplex admission helpers now live in
      `river_cloud_xiaozhi_playback_runtime.c`:
      - `playback_allows_vad_open()`
      - `capture_held_by_playback()`
      - `apply_tts_start_round_policy()`
    - `xiaozhi_session.c` no longer owns those playback/downlink rules
    - this keeps AEC/VAD admission and `tts_start` round-close policy attached
      to the downlink runtime truth owner
  - older runtime-ownership slice:
    - session_coordinator no longer reads dialog-runtime snapshots directly for:
      - wakeword admission
      - barge-in interrupt
    - those checks are now exposed as typed dialog-runtime policy helpers,
      keeping the admission rules inside the truth source itself
    - this removes another class of coordinator-side rule reconstruction
  - older runtime-ownership slice:
    - cloud ASR lifecycle no longer needs session_coordinator as a truth bridge
    - app now fans out ASR results so:
      - dialog runtime absorbs lifecycle truth directly
      - session_coordinator keeps only text/barge-in/diag side effects
    - this removes another coordinator-owned cloud->dialog bridge and keeps the
      lifecycle truth closer to dialog runtime itself
  - older runtime-ownership slice:
    - XiaoZhi playback runtime now directly fills playback-owned fields in
      `river_cloud_runtime_snapshot_t`
    - `river_cloud_xiaozhi_session.c` no longer hand-assembles playback phase /
      rebuffer / terminal / start-gate fields inline
    - this keeps playback snapshot projection ownership aligned with:
      - playback status dump ownership
      - playback phase/rebuffer/start-gate runtime truth ownership
  - earlier runtime-ownership slice:
    - dialog runtime boot / wake / ASR cloud-backed ingress now shares one
      internal typed reducer instead of five handwritten
      capture-mutate-publish paths
    - new internal reducer helpers now own:
      - local fact application
      - cloud snapshot absorption
      - publish reason selection
    - this further reduces truth-source drift inside dialog runtime itself and
      keeps cloud-backed ingress aligned with the long-term reducer architecture
  - earlier runtime-ownership slice:
    - XiaoZhi playback start-gate snapshot is now exported through:
      - `river_cloud_runtime_snapshot_t`
      - `river_dialog_runtime_snapshot_t`
    - session snapshot fill no longer needs to reconstruct that gate; it simply
      projects the runtime-owned snapshot fields upward:
      - `playback_start_policy`
      - `playback_start_frames`
      - `playback_prefetch_frames`
      - `playback_start_cautious_history`
    - dialog runtime status now prints the active gate truth directly, so board
      diagnostics can correlate:
      - playback phase
      - rebuffer cause
      - current start/prefetch gate
      from one runtime chain
  - prior runtime-ownership slice:
    - XiaoZhi playback start gate is now stored as a runtime-owned snapshot
      instead of being re-derived independently at each read site
    - runtime now splits start-gate ownership into:
      - `build_start_gate`
      - `refresh_start_gate`
      - `current_start_gate`
    - snapshot refresh is now wired into gate-affecting state transitions:
      - meta clear
      - downlink/playback reset
      - rebuffer enter/finish
      - clean-segment streak clear
      - playback meta prefetch update
      - downlink frame-duration update
    - this keeps worker/start/rebuffer diagnostics on the same gate truth and
      removes one more class of “同一时刻不同调用点各自现算出不同门限”的 drift
  - prior runtime-ownership slice:
    - XiaoZhi downlink start gating now comes from one explicit typed prefetch
      policy helper instead of being split across scattered threshold
      heuristics:
      - `baseline`
      - `rebuffer_fast`
      - `segment_prefetch`
      - `starved_prefetch`
    - runtime now computes and reuses one typed start-gate result:
      - `start_frames`
      - `prefetch_frames`
      - `cautious_history`
    - `audio.out.meta` segment cadence can now raise the start threshold even
      outside the active `UPSTREAM_STARVED` path, which directly targets the
      current long-segment / slow-supply / segment-boundary playback churn
    - board diagnostics now print the same typed gate on:
      - status
      - prefetch
      - upstream-gap rebuffer
      - playback start
      - rebuffer requested
  - earlier runtime-ownership slice:
    - XiaoZhi playback segment truth now also records whether a segment has
      already gone through local rebuffer:
      - `rebuffered`
    - active rebuffer history `streak` is no longer cleared by any segment that
      merely finishes after recover
    - only a fully-heard clean segment clears that `streak`, while recovered
      segments now explicitly preserve it with:
      - `reason=segment_recovered`
    - this keeps the start-threshold history aligned with actual uninterrupted
      playback stability instead of “recover 后勉强播完也算稳定”
  - earlier runtime-ownership slice:
    - XiaoZhi playback runtime now separates:
      - cumulative rebuffer diagnostics
      - active restart-threshold history
      through:
      - `xiaozhi_playback_rebuffer_count`
      - `xiaozhi_playback_rebuffer_streak`
    - start-threshold inflation now keys off the active `streak` instead of the
      cumulative total, and a fully-heard segment clears that `streak`
    - playback diagnostics now log both:
      - `rebuffer_total`
      - `streak`
    - playback-service public control wrappers for:
      - `stop`
      - `interrupt`
      - `flush`
      - `recover`
      now propagate the real underlying control result to callers
    - this removes one more false-success path in the downlink recovery model
      and prevents old rebuffer history from permanently biasing later
      playback starts within the same response
  - previous runtime-ownership slice:
    - playback service now exposes explicit recover semantics:
      - `recover_stream_ex`
      - `recover_stream`
    - XiaoZhi playback runtime now routes both:
      - `upstream_starved`
      - residual `write_failed`
      rebuffer recovery through `recover`, instead of mixing `stop` and `flush`
    - this reduces one more source of stop/start churn on recoverable playback
      gaps
  - previous runtime-ownership slice:
    - dialog runtime now translates local playback-service callbacks into typed
      local truth:
      - `playback_local_active`
      - `playback_local_recovering`
    - aggregate playback derivation now consumes:
      - cloud playback phase
      - local typed playback truth
      - lane / rebuffer truth
      instead of branching directly on raw `RIVER_PLAYBACK_*` enums in the
      reducer path
    - `dialog_runtime_dump_status()` now exposes both aggregate playback truth
      and local playback truth for board-side diagnosis
  - earlier runtime-ownership slice:
    - dialog runtime local playback ingress now consumes only app-registered
      dialog stream names instead of all `RIVER_PLAYBACK_PRIO_TTS` streams
    - `river_app` currently registers:
      - `xiaozhi_tts`
      - `iflytek_tts`
    - dialog runtime now latches the exact owned stream name so cleared-config
      terminal callbacks stay scoped to the same stream until `IDLE`
  - prior runtime-ownership slice:
    - session runtime now also owns the XiaoZhi I/O-loop housekeeping reducers:
      - `io_tick` turn/window maintenance
      - post-poll accepted-turn finalize glue
      - post-uplink playback/endpoint/local-close housekeeping
    - adapter `river_cloud_xiaozhi_io_task(...)` now keeps only poll/uplink
      scheduling order instead of mutating those session/playback truths inline
  - latest landed runtime-ownership slice:
    - session runtime now owns the XiaoZhi `OPEN_AND_LISTEN` transport helper:
      - session open
      - session-id sync from transport cache
      - `listen_start` gating against runtime listening truth
    - adapter `river_cloud_xiaozhi_control_execute(...)` now forwards that
      control op instead of directly mutating those transport/session facts
  - newest landed runtime-ownership slice:
    - session runtime now also owns the remaining session-side transport
      control reducer for:
      - `listen_stop`
      - `abort`
      - `close_session`
    - adapter `river_cloud_xiaozhi_control_execute(...)` now keeps fewer
      transport/session branches and continues shrinking toward a dispatch-only
      shell
  - latest landed runtime-ownership slice:
    - playback runtime now owns the `PLAYBACK_*` ACK transport reducer for:
      - `started`
      - `mark`
      - `cleared`
      - `completed`
    - adapter `river_cloud_xiaozhi_control_execute(...)` no longer directly
      sends playback ACKs and continues shrinking toward a pure control-dispatch
      shell
  - newest landed runtime-ownership slice:
    - session runtime now owns the full XiaoZhi control-op dispatch shell,
      routing:
      - session-side transport control
      - playback ACK transport control
    - adapter `river_cloud_xiaozhi_control_execute(...)` is now reduced to a
      one-line runtime call, matching the earlier event-dispatch shrink pattern
    - dialog runtime ingress now only treats `RIVER_PLAYBACK_ERROR` as fatal
      `error_recovering`, while recoverable write churn stays on the
      `playback_recovering` path
    - this gives the runtime truth source a real semantic split between
      recoverable rebuffer/restart churn and hard local playback faults
  - newest landed runtime-ownership slice:
    - session runtime now also owns the XiaoZhi control-request queue path:
      - sync request submission
      - async request submission
      - queue drain and completion signaling
      - session-side request wrapper entrypoints
    - adapter `river_cloud_xiaozhi_io_task(...)` now only invokes the
      runtime-owned queue drain helper instead of directly maintaining queue
      read/write/completion semantics
  - newest landed runtime-ownership slice:
    - session runtime now also owns the XiaoZhi uplink I/O service path:
      - retry-preserved frame drain
      - send-ready gating
      - busy backoff / backpressure logging
      - uplink timestamp advance
      - round packet-sent accounting
    - adapter `river_cloud_xiaozhi_io_task(...)` now only schedules the
      runtime uplink helper, and adapter uplink sending is reduced to the thin
      transport shell:
      - `river_cloud_xiaozhi_send_uplink_transport(...)`
  - newest landed runtime-ownership slice:
    - playback runtime now also owns the remaining adapter-facing playback
      backend lifecycle glue for:
      - backend refresh / downlink worker bootstrap
      - playback-state reset on backend refresh
      - bridge-close decoder tail teardown
    - adapter XiaoZhi init / config-refresh path now only calls:
      - `river_cloud_xiaozhi_apply_playback_backend_refresh_policy(...)`
  - newest landed runtime-ownership slice:
    - playback runtime now also owns the grouped terminal-close playback
      reducer for:
      - `transport_closed`
      - `network_lost`
      - `bridge_close`
    - session runtime terminal branches no longer inline playback abort gating,
      and adapter `river_cloud_asr_audio_close()` no longer appends a separate
      playback bridge-close tail after calling the session runtime policy
  - newest landed runtime-ownership slice:
    - playback runtime now also owns the session-entry playback cleanup
      reducers for:
      - transport reset
      - session start
    - `river_cloud_xiaozhi_session.c` no longer directly calls:
      - `river_cloud_xiaozhi_reset_downlink_state()`
      - `river_cloud_xiaozhi_clear_playback_meta_state()`
      from its reset/open branches
    - grouped runtime-owned helpers now normalize those entry points:
      - `river_cloud_xiaozhi_apply_transport_reset_playback_policy()`
      - `river_cloud_xiaozhi_apply_session_start_playback_policy()`
  - newest landed runtime-ownership slice:
    - playback runtime now also owns the capture-entry playback policy for:
      - pending-stop observation on capture ingress
      - duplex-held capture gate
      - fallback logging and held-capture pre-roll reset
      - capture-exit pending-stop observation
    - adapter `river_cloud_xiaozhi_stream_push_frame(...)` now only calls:
      - `river_cloud_xiaozhi_apply_capture_entry_playback_policy()`
      - `river_cloud_xiaozhi_apply_capture_exit_playback_policy()`
  - newest landed runtime-ownership slice:
    - session runtime now also owns the active-stream capture tail reducer for:
      - `speech_resumed` endpoint-soft-close cancel
      - silence/post-roll progression
      - duplex soft-endpoint vs local stream-finish dispatch
    - adapter `river_cloud_xiaozhi_stream_push_frame(...)` no longer directly
      mutates `silence_frames` or branches on
      `duplex_soft_endpoint_enabled()` after PCM push
  - newest landed runtime-ownership slice:
    - session runtime now also owns the bridge-close capture reducer for:
      - active-stream finish on bridge close
      - bridge-close terminal-policy sequencing
    - adapter `river_cloud_asr_audio_close()` now only calls:
      - `river_cloud_xiaozhi_apply_bridge_close_capture_policy()`
      before its encoder/generic bridge teardown
  - newest landed runtime-ownership slice:
    - session runtime now also owns the bridge-open capture/uplink reducer for:
      - uplink audio format validation
      - XiaoZhi pre-roll cap normalization
      - uplink ring lifecycle normalization
      - uplink timestamp / retry / busy-metric reset
      - bridge-open listen-gate logging
    - adapter `river_cloud_asr_audio_open()` now only calls:
      - `river_cloud_xiaozhi_apply_bridge_open_capture_policy()`
      around its generic bridge allocation path
    - adapter `river_cloud_asr_audio_close()` no longer carries a leftover
      XiaoZhi-only `xiaozhi_open_speech_frames` reset
  - newest landed runtime-ownership slice:
    - session runtime now also owns the XiaoZhi stream-push capture reducer for:
      - inactive-stream followup-open branching
      - pre-roll replay into uplink on stream activation
      - active-stream feed bookkeeping
      - stream activation log / stats transition
    - adapter `river_cloud_xiaozhi_stream_push_frame(...)` now only wraps:
      - capture-entry playback policy
      - runtime-owned stream-push capture policy
      - capture-exit playback policy
    - generic bridge helpers needed by that reducer are now exported on the
      internal boundary instead of staying adapter-local:
      - `river_cloud_log_stream_open_deferred_once(...)`
      - `river_cloud_reset_stream_open_deferred_state()`
      - `river_cloud_pre_roll_store(...)`
  - newest landed runtime-ownership slice:
    - XiaoZhi playback runtime now owns an explicit typed playback-backend
      truth for local backend ownership:
      - `detached`
      - `owned_active`
      - `foreign_active`
      - `restart_pending`
    - downlink start/rebuffer/pending-stop/abort paths now consume that typed
      backend state instead of scattering raw checks across:
      - `xiaozhi_playback_active`
      - `river_playback_service_active()`
      - `RIVER_PLAYBACK_RESTART_PENDING`
    - this prevents XiaoZhi cleanup/recovery paths from treating a foreign
      active playback stream as XiaoZhi-owned, and gives logs a direct
      ownership signal for future board traces
  - newest landed runtime-ownership slice:
    - dialog runtime now only absorbs dialog-related local playback-service
      streams:
      - current rule: `RIVER_PLAYBACK_PRIO_TTS`
      - shared `audio_echo` / debug playback no longer enters dialog truth
    - dialog runtime also keeps the owned local playback latch through
      cleared-config terminal transitions until `RIVER_PLAYBACK_IDLE`, so the
      local playback ingress still closes cleanly after:
      - `RECOVERING`
      - `RESTART_PENDING`
      - `IDLE`
  - newest landed runtime-ownership slice:
    - XiaoZhi downlink runtime now records explicit last-supply timing for
      playback starvation truth:
      - `xiaozhi_downlink_last_supply_ms`
    - upstream-starvation rebuffering is now triggered from:
      - low-water queued frames
      - adaptive supply-gap timing
      instead of waiting only for `queued=0`
    - the downlink worker no longer clears starvation watch state on every
      non-zero queue/write loop, which lets runtime keep a continuous starvation
      observation window before `write_failed`
  - newest landed runtime-ownership slice:
    - XiaoZhi playback runtime now treats low-water supply-gap `write_failed`
      events as `upstream_starved` recovery instead of always forcing local
      `flush/restart`
    - the write-failure branch now explicitly selects:
      - `recovery=stop`
      - `recovery=flush`
      and logs the chosen path together with:
      - `queued`
      - `low`
      - `supply_gap_ms`
  - previous landed runtime-ownership slice:
    - dialog runtime now directly owns the local playback-service listener
      ingress:
      - `river_dialog_runtime_on_playback_state(...)`
    - `river_app` now registers that callback straight into:
      - `river_playback_service_register_listener(...)`
    - `session_coordinator` no longer owns:
      - `river_session_coordinator_on_playback_state(...)`
    - this removes another coordinator-side truth bridge so local playback
      facts now enter dialog runtime without an intermediate owner
  - previous landed runtime-ownership slice:
    - dialog runtime now exports an atomic wake-admission fusion API:
      - `river_dialog_runtime_note_wake_confirmed_with_cloud_state(...)`
    - `session_coordinator` wake admission success paths now call that single
      dialog-runtime entrypoint instead of explicitly sequencing:
      - `note_wake_confirmed(...)`
      - `sync_cloud_state("wakeword_detected")`
    - the explicit wakeword cloud-sync bridge has been removed from
      coordinator
  - previous landed runtime-ownership slice:
    - cloud adapter now exports provider capability:
      - `river_cloud_adapter_runtime_self_sync_active()`
    - XiaoZhi session runtime now self-publishes state sync after:
      - `asr_error`
      - `asr_session_started`
      - `asr_session_closed`
    - `session_coordinator` only keeps the ASR lifecycle cloud-snapshot pull as
      a fallback for providers that do not self-sync runtime truth
  - previous landed runtime-ownership slice:
    - dialog runtime now directly owns the cloud state-sync ingress callback:
      - `river_dialog_runtime_on_cloud_state_sync(...)`
    - `river_app` no longer bridges cloud-state sync just to forward it into
      dialog runtime
    - `session_coordinator` playback listener no longer re-pulls the cloud
      runtime snapshot on every local playback-service state change
    - the stale `river_session_coordinator_sync_interaction_state(...)` bridge
      has been removed
  - previous landed runtime-ownership slice:
    - XiaoZhi playback runtime now proactively triggers cloud state sync when:
      - `playback_phase` changes
      - or rebuffer cause changes without a phase transition
    - this starts removing another remaining indirection where outer
      `session_coordinator` playback-state callbacks had to pull a fresh cloud
      snapshot just to let dialog/core observe playback-runtime truth updates
  - previous landed runtime-ownership slice:
    - XiaoZhi downlink restart gating now differentiates:
      - `upstream_starved`
      - `write_failed`
      instead of forcing both recovery classes through the same prefetch-sized
      refill wait
    - runtime downlink diagnostics now also print the current `start`
      threshold, so the effective restart gate can be checked directly on the
      board
  - previous landed runtime-ownership slice:
    - XiaoZhi playback runtime now also owns an explicit `rebuffer_cause`
      truth:
      - `upstream_starved`
      - `write_failed`
    - cloud/dialog runtime snapshot path now exports:
      - `playback_rebuffer_cause`
    - playback phase / rebuffer / session-status diagnostics now print that
      cause directly, which removes another remaining layer of log-only
      inference from the downlink recovery model
  - previous landed runtime-ownership slice:
    - dialog runtime `playback_recovering` is now also phase-first:
      - if `playback_phase` is known, recovery truth comes from phase/rebuffer
      - playback-service `recovering/restart_pending` only remain as phase-miss
        fallback
    - this blocks another residual leak where bottom-layer restart states could
      override an already-stable runtime phase truth
  - previous landed runtime-ownership slice:
    - dialog runtime now suppresses playback-state publishes when:
      - `playback_phase` is already known
      - and the new playback-service state does not change effective playback
        or interaction facts
    - this reduces residual upper-layer churn during recovery windows after
      phase-first truth is already stable
  - previous landed runtime-ownership slice:
    - dialog runtime playback derivation now treats XiaoZhi playback phase as
      the first-class truth:
      - `playback_recovering` prefers `rebuffering`
      - `playback_active` trusts phase before service/bool compatibility
    - XiaoZhi cloud runtime snapshot also now exports:
      - `playback_active = playback_output_active`
      so upper layers no longer ingest the old raw active bit
  - previous landed runtime-ownership slice:
    - XiaoZhi playback runtime now owns an explicit `playback_phase` truth:
      - `idle`
      - `prefetching`
      - `playing`
      - `rebuffering`
      - `draining`
    - playback lane helpers, cloud snapshot export, and dialog runtime dump now
      consume that phase instead of re-guessing playback occupancy from
      scattered booleans and playback-service activity
  - previous landed runtime-ownership slice:
    - XiaoZhi downlink runtime now treats `rebuffer_pending` as a real write
      resume gate:
      - wait for adaptive queued-frame threshold
      - only clear rebuffer after the threshold is satisfied
    - this closes a gap where write-failure recovery could flush the playback
      service and then immediately retry the same frame without enough refill
  - previous landed runtime-ownership slice:
    - generic ASR bridge runtime now also owns the last generic realtime
      open reducer:
      - `river_cloud_business_time_ready(...)`
      - `river_cloud_stream_open_and_flush(...)`
    - adapter no longer defines any generic realtime capture reducer body;
      it now only orchestrates:
      - validation
      - runtime helper calls
      - backend dispatch
  - previous landed runtime-ownership slice:
    - generic ASR bridge runtime now also owns the shared realtime reducer
      helpers for:
      - pre-roll reset
      - pre-roll store
      - active-stream finish
    - adapter now only keeps:
      - generic `stream_open_and_flush()` reducer
      - high-level bridge orchestration
  - previous landed runtime-ownership slice:
    - generic ASR bridge runtime now owns the base capture bridge state
      allocation/reset for:
      - `audio_desc`
      - `frame_bytes`
      - `pre_roll_buffer`
      - `pre_roll_capacity_frames`
      - `post_roll_frames`
      - `silence_frames`
    - adapter `river_cloud_asr_audio_open()/close()` now delegates to:
      - `river_cloud_prepare_audio_bridge_state(...)`
      - `river_cloud_reset_audio_bridge_state()`
  - previous landed runtime-ownership slice:
    - XiaoZhi capture dispatch no longer uses an adapter-local stream-push
      shim:
      - removed `river_cloud_xiaozhi_stream_push_frame(...)`
      - `river_cloud_asr_stream_push_frame(...)` now directly calls
        `river_cloud_xiaozhi_apply_capture_stream_policy(...)`
    - adapter is reduced further to:
      - shared bridge argument validation
      - streaming support guard
      - backend branch to runtime
  - previous landed runtime-ownership slice:
    - session runtime now also owns the last playback wrapper around the
      XiaoZhi capture push path:
      - capture-entry playback gate
      - capture-exit playback tail
    - adapter `river_cloud_xiaozhi_stream_push_frame(...)` is now reduced to:
      - input validation
      - `river_cloud_xiaozhi_apply_capture_stream_policy(...)`
  - eighth landed slice on that plan:
    - terminal `completed` is now gated by `last_segment observed + fully
      heard`, not only by a transient local drain point
    - `tts_stop_pending` no longer resets playback runtime state early when the
      local player goes idle before the final tail has actually been observed
    - this narrows the false-completion window that previously let late
      segments arrive after the device had already decided the response was
      completed
  - ninth landed slice on that plan:
    - local-only clear no longer fabricates terminal `cleared` truth when the
      board never actually queued `audio.out.cleared`
    - terminal-tail waiting is now exported as runtime-owned state all the way
      through cloud snapshot -> dialog runtime snapshot
    - runtime dumps can now distinguish ordinary stop-pending playback from
      “waiting for final tail/meta before terminal completion”
  - tenth landed slice on that plan:
    - playback runtime now separates:
      - local terminal outcome
      - protocol terminal ACK truth
    - local-only terminal outcomes now explicitly surface as:
      - `local_completed`
      - `local_cleared`
    - cloud snapshot and dialog runtime snapshot now both export:
      - `playback_terminal_state`
      - `playback_terminal_reason`
    - adapter/dialog diagnostics now show:
      - terminal state
      - terminal ACK
      independently
  - eleventh landed slice on that plan:
    - adapter terminal-close branches now use one playback-runtime
      cause reducer instead of locally assembling:
      - `clear_reason`
      - `stream_reason`
      - `interrupt_stream`
    - typed runtime abort causes now cover:
      - `interrupt`
      - `transport_closed`
      - `network_lost`
      - `bridge_close`
    - playback runtime now emits one normalized abort fact with:
      - cause
      - clear reason
      - stream reason
      - interrupt mode
      - entry playback/work state
  - twelfth landed slice on that plan:
    - the remaining local playback data-plane fatal path
      `frame_oversize` now also routes through the same playback-runtime typed
      abort reducer
    - downlink worker no longer hand-assembles:
      - finalize_cleared
      - reset_downlink
      - stop_stream
      - reset_playback
      for that fatal path
    - this leaves the playback runtime with one common terminal-close entry for
      both:
      - transport-side aborts
      - local fatal downlink faults
  - thirteenth landed slice on that plan:
    - `dialog runtime` no longer treats late `output_lane=speaking` as
      sufficient truth after local playback has already entered a terminal
      outcome
    - local terminal close and terminal-tail wait are now consumed directly in
      the interaction-state derivation path
    - playback-active truth is now re-derived through the same terminal-aware
      reducer on both:
      - cloud snapshot sync
      - playback-service ingress
  - fourteenth landed slice on that plan:
    - `session coordinator` no longer bypasses `dialog runtime` by checking
      playback-service local activity directly during barge-in admission
    - barge-in text confirmation now consumes one runtime-owned predicate:
      - playback is locally interruptible
      - playback has not already entered a terminal state
  - fifteenth landed slice on that plan:
    - XiaoZhi downlink write-fail recovery now tries same-track
      `flush/restart` first instead of immediately tearing down the whole
      playback stream
    - playback service can now recover from `RIVER_PLAYBACK_RECOVERING` back
      to `RIVER_PLAYBACK_RUNNING`
    - stop/start is retained only as the fallback path when same-track recover
      fails
  - latest landed slice on that plan:
    - session runtime now exports adapter-facing getters for:
      - `listening`
      - `conversation_window_active`
      - `conversation_window_remaining_ms`
    - adapter transport-active, config busy guard, status dump, and public
      runtime snapshot/window getters now consume those exported session facts
      instead of directly reading `xiaozhi_listening/xiaozhi_window_active`
    - `open_and_listen` success-time `listening=true` normalization is now also
      applied by the exported request-success session policy, so adapter
      transport execution no longer owns that state write
    - adapter dump/uplink diagnostics now also consume exported runtime facts
      for:
      - `local_close_pending`
      - `local_close_remaining_ms`
      - `listen_stop_pending`
      instead of directly reading those session fields
    - preview observation helper ownership is now also session-runtime-owned:
      - `river_cloud_xiaozhi_note_preview_observation(...)`
      - `river_cloud_xiaozhi_copy_optional_text(...)`
      so adapter no longer implements preview-state mutation helpers locally
  - sixteenth landed slice on that plan:
    - local round-close truth has started moving out of
      `river_cloud_adapter.c` into `river_cloud_xiaozhi_session.c`
    - `round_finish` pacing truth now lives with other XiaoZhi session
      lifecycle ownership
    - adapter `local_close_resolved / server_response_started` paths now upload
      only typed round-close causes into session runtime
  - seventeenth landed slice on that plan:
    - `endpoint soft close / local close defer` helper ownership has also
      started moving out of the adapter and into XiaoZhi session runtime
    - adapter now consumes exported session-runtime helpers for:
      - endpoint soft-close arm/cancel
      - local-close defer arm/check
      - duplex-speaking uplink continuation predicate
  - eighteenth landed slice on that plan:
    - `active stream finish` state commit and `endpoint soft-close timeout`
      decision now also live in XiaoZhi session runtime
    - adapter keeps only the transport tail that flushes the last accumulator
      frame and finalizes `listen_stop`
    - local-close/server-response branches now forward typed causes directly
      instead of routing through adapter-local wrappers
  - nineteenth landed slice on that plan:
    - downlink / playback local buffering was rebuilt around a larger jitter
      budget:
      - downlink ring `96`
      - start threshold `16`
      - rebuffer threshold `28`
      - playback target/fallback buffer `12/8`
    - playback runtime now derives adaptive prefetch truth from
      `audio.out.meta` arrival cadence:
      - `playback_last_meta_gap_ms`
      - `playback_prefetch_target_ms`
    - starvation-triggered rebuffer now waits against that adaptive target
      instead of a small fixed timeout, and adapter diagnostics now print:
      - `target_ms`
      - `meta_gap_ms`
      - `rebuffer_count`
  - twentieth landed slice on that plan:
    - recoverable playback churn is now exported as runtime truth end-to-end:
      - cloud snapshot exports `playback_rebuffer_pending`
      - dialog runtime stores `playback_cloud_active`
      - dialog runtime derives `playback_recovering`
    - `dialog runtime` playback-active truth now stays engaged across
      same-track recover / rebuffer churn instead of inferring “stopped” from
      a transient local active gap
    - `river_playback_service_state_active()` now also treats
      `RIVER_PLAYBACK_RECOVERING` as active, so AEC/VAD and interaction truth
      no longer flap on recover-first restart
  - twenty-first landed slice on that plan:
    - recover-first restart failure no longer escalates into
      `RIVER_PLAYBACK_ERROR` before the runtime can try a normal fresh start
  - twenty-second landed slice on that plan:
    - AEC/duplex evaluation now treats `RIVER_PLAYBACK_RESTART_PENDING` as its
      own runtime reason instead of collapsing it into generic
      `ref_missing/ref_idle`
    - the voice runtime now exports:
      - `RIVER_VOICE_AEC_GATE_BLOCKED_PLAYBACK_RESTART_PENDING`
      - `RIVER_VOICE_DUPLEX_READY_PLAYBACK_RESTART_PENDING`
    - XiaoZhi fallback logs now surface:
      - `half_duplex_restart_pending`
  - twenty-third landed slice on that plan:
    - XiaoZhi `tts_start` keep-open / round-close policy now lives in session
      runtime instead of the adapter
    - adapter TTS-start handling now only finalizes pending text, calls the
      exported runtime helper, and keeps the playback-stop cancel tail glue
  - twenty-fourth landed slice on that plan:
    - the now-redundant `keep_local_round_on_tts_start` predicate was removed
      after `tts_start` policy had already been centralized in session runtime
    - the session-runtime TTS-start surface is now a single exported policy
      helper
  - latest landed slice on that plan:
    - XiaoZhi `SERVER_HELLO` transport observation has also moved out of
      `river_cloud_adapter.c` into session runtime
    - adapter `RIVER_XIAOZHI_EVENT_SERVER_HELLO` handling now only dispatches
      to:
      - `river_cloud_xiaozhi_note_server_hello_observation(...)`
    - session runtime now owns:
      - server sample-rate truth
      - server frame-duration truth
      - session-id sync from transport cache
  - twenty-fifth landed slice on that plan:
    - XiaoZhi `tts_stop` round-close policy now lives in session runtime
    - adapter TTS-stop handling now only calls the exported runtime helper
  - twenty-sixth landed slice on that plan:
    - XiaoZhi local-close `reopen_overlap` policy now lives in session runtime
    - adapter reopen path now only calls the exported overlap helper
  - twenty-seventh landed slice on that plan:
    - XiaoZhi `LLM` local-close policy now lives in session runtime
    - adapter `RIVER_XIAOZHI_EVENT_LLM` branch now only calls the exported
      runtime helper
  - twenty-eighth landed slice on that plan:
    - XiaoZhi `post_stop_result` local-close policy now lives in session runtime
    - adapter listen-stop completion path now only calls the exported runtime
      helper
  - twenty-ninth landed slice on that plan:
    - XiaoZhi `transport_closed` terminal cleanup policy now lives in session runtime
    - adapter `RIVER_XIAOZHI_EVENT_SESSION_CLOSED` branch now only calls the
      exported runtime helper
  - thirtieth landed slice on that plan:
    - XiaoZhi `network_lost` terminal cleanup policy now lives in session runtime
    - adapter `river_cloud_adapter_notify_network_lost()` path now only calls
      the exported runtime helper for terminal cleanup
    - fixed-dsb AECM summary now counts:
      - `restart_pending`
      separately from generic playback-disabled/reference-idle churn
  - thirty-first landed slice on that plan:
    - XiaoZhi `bridge_close` terminal cleanup policy now lives in session runtime
    - adapter `river_cloud_asr_audio_close()` path now only calls the exported
      runtime helper for terminal cleanup, while keeping close-session and
      codec-close tail actions in the adapter
  - thirty-second landed slice on that plan:
    - XiaoZhi `listen_stop` completion round-close policy now lives in session runtime
    - adapter `river_cloud_xiaozhi_finalize_listen_stop_if_ready()` now only
      keeps uplink-drained / `listen_stop` transport gating and calls the
      exported runtime helper for the completion round policy
  - thirty-third landed slice on that plan:
    - XiaoZhi ASR round lifecycle now lives in session runtime
    - adapter no longer defines local:
      - `round_begin`
      - `round_note_packet_sent`
      implementations
  - thirty-fourth landed slice on that plan:
    - XiaoZhi TTS interrupt policy now lives in session runtime
    - adapter `river_cloud_adapter_interrupt_tts_with_reason(...)` now only
      keeps provider dispatch and calls exported runtime helper
  - thirty-fifth landed slice on that plan:
    - XiaoZhi follow-up reopen round-start policy now lives in session runtime
    - adapter reopen-open path now only computes pre-roll count and delegates
      follow-up round-start policy to runtime
  - thirty-sixth landed slice on that plan:
    - XiaoZhi `network_lost` / `bridge_close` terminal-policy helpers now own
      close-session tail actions
    - adapter terminal paths no longer append local close-session requests
  - thirty-seventh landed slice on that plan:
    - XiaoZhi idle reopen gate now lives in session runtime
    - adapter capture path no longer directly decides:
      - wakeword-window reopen eligibility
      - no-ref reopen rearm / guard gating
      - open-hold frame accumulation thresholds
  - thirty-eighth landed slice on that plan:
    - XiaoZhi listen-stop completion policy now lives in session runtime
    - adapter uplink service path now only samples drain state and delegates:
      - queued uplink frame count
      - pending accum buffer bytes
  - thirty-ninth landed slice on that plan:
    - XiaoZhi listen-stop reopen busy gate now lives in session runtime
    - adapter reopen-open path no longer directly reads
      `xiaozhi_listen_stop_pending` before delegating to runtime
  - fortieth landed slice on that plan:
    - XiaoZhi open-and-listen success policy now clears stop intent in session
      runtime
    - adapter `CTRL_OPEN_AND_LISTEN` path no longer directly clears
      `xiaozhi_listen_stop_pending`
  - forty-first landed slice on that plan:
    - XiaoZhi uplink keepalive gate now lives in session runtime
    - adapter `uplink_active()` no longer directly folds
      `xiaozhi_listen_stop_pending`
  - forty-second landed slice on that plan:
    - XiaoZhi uplink send-ready gate now lives in session runtime
    - adapter control/uplink 路径不再重复拼接：
      - `river_xiaozhi_session_open() && g_river_cloud.xiaozhi_listening`
  - twenty-third landed slice on that plan:
    - cloud/runtime now export a first-class playback-lane engagement fact:
      - `playback_lane_engaged`
    - XiaoZhi playback runtime owns the reducer for that fact by folding:
      - playback output active
      - rebuffer pending
      - playback-service active states
    - adapter transport/capture gating and dialog runtime playback derivation
      now consume that exported lane truth instead of reassembling engagement
      from raw local flags
  - twenty-fourth landed slice on that plan:
    - XiaoZhi `open_hold_frames_required` and `no_ref_reopen_ready` helper
      ownership has moved out of `river_cloud_adapter.c` into session runtime
    - the session helper now directly consumes `playback_lane_engaged`, so
      `no_ref` reopen no longer depends on adapter-local call ordering to avoid
      reopening during an occupied playback lane
    - adapter capture-open flow now only calls exported helpers for those two
      policy decisions
  - twenty-fifth landed slice on that plan:
    - `capture held during playback` has also started moving behind a
      session-runtime helper:
      - `river_cloud_xiaozhi_capture_held_by_playback(...)`
    - that helper now owns the combined reducer:
      - `playback_lane_engaged`
      - `duplex_fallback_reason`
    - adapter capture path now consumes the helper and no longer reconstructs
      that predicate locally
    - playback service now tears down the failed track and returns to
      restartable `IDLE` with a dedicated `recover fallback` path
    - XiaoZhi downlink recovery no longer forces an extra redundant local stop
      after that fallback, reducing fatal/error noise on the rebuffer path
  - twenty-second landed slice on that plan:
    - playback service now exposes one explicit intermediate state:
      - `RIVER_PLAYBACK_RESTART_PENDING`
    - this keeps the rest of the stack on the “recoverable playback still
      engaged” path after a broken track is torn down, without pretending the
      track is still running
    - XiaoZhi downlink worker now explicitly fresh-starts from:
      - `IDLE`
      - `RESTART_PENDING`
      so the new engaged state does not stall restart
  - aligned the device-side duplex roadmap to the 2026-04-16
    `/root/agent-server` protocol/architecture docs:
    - preview-aware input events
    - playback-truth ACKs
    - discovery + `session.start.capabilities` collaboration negotiation
  - re-prioritized the next device slice after reviewing the latest
    `/root/agent-server` playback-truth commits:
    - `2a2c9cf`
    - `dd10dff`
    - `d0d81ee`
    - `46aef68`
    - `74a9c6d`
  - conclusion:
    - `segment_mark_v1` now depends on the full
      `audio.out.started/mark/cleared/completed` truth chain, so device work had
      to land `C5` before returning to `5.168`
  - `5.168` is now landed:
    - XiaoZhi speaking-time duplex decisions are gated by runtime-truthful
      `duplex_ready`, not only static profile capability
    - runtime duplex evaluation now combines:
      - duplex experiment switch
      - active profile capability
      - reference service state / recent activity
      - AEC gate result
    - the next implementation slice is:
      - `5.169` speaking-time local endpoint softening
  - `5.169` is now landed:
    - duplex-ready speaking rounds now treat `input.endpoint` and short local
      silence as hint/defer signals instead of immediate hard local close
    - the device now exposes `endpoint_soft_close pending/reason/left_ms`
      runtime status for board validation
    - the next implementation slice is:
      - `5.170` speaking-time uplink continuation
  - `5.170` is now landed:
    - an expired `endpoint_soft_close` no longer resolves into local
      `audio.in.commit` while the duplex-ready output lane is still effectively
      speaking
    - speaking-time local silence can therefore keep `listening=yes` and the
      uplink round alive across pause gaps without repeated local stop/reopen
    - once output leaves the speaking lane, the pending close reuses the
      existing commit/stop pipeline
    - the next implementation slice is:
      - `5.171` duck-first interruption policy
  - `5.171` is now landed:
    - local speaking-time near-end speech no longer jumps straight from
      “qualifying VAD hit” to hard interrupt
    - the device now ducks first, releases on short/noisy speech, and only
      escalates to hard interrupt after sustained evidence
    - the next implementation slice is:
      - `5.172` board-profile duplex-ready acoustic baseline
  - `5.172` is now landed:
    - the branch build now selects a dedicated `duplex-ready experimental`
      board profile:
      - `fixed_dsb_webrtc_aecm`
      - `CONFIG_RIVER_XIAOZHI_FULL_DUPLEX_EXPERIMENT_EN=y`
    - native-capture-ref duplex gating no longer trusts playback state alone;
      it now consumes actual AECM ref telemetry:
      - `ref_activity`
      - `ref_peak`
      - `ref_ratio_q15`
      - freshness age
    - `vad_probe` now measures `ref_peak` from capture `ch3` when the active
      profile uses native reference, so board logs and local barge-in
      arbitration can observe the real far-end reference source
  - `5.173` is now landed:
    - the branch now separates:
      - duplex experiment compiled in
      - default-on policy enabled
      - per-session permission to advertise `half_duplex=false`
    - `session.start` default duplex advertisement now requires the full
      device + service matrix:
      - active profile supports playback reference
      - discovery advertises `voice_collaboration`
      - server endpoint is available and enabled
      - `preview_events` negotiation succeeds
      - `playback_ack.mode=segment_mark_v1` negotiation succeeds
    - speaking-time keep-open / capture-hold / status logs now share the same
      richer fallback reason family, including:
      - `half_duplex_default_policy_disabled`
      - `half_duplex_service_collaboration_unavailable`
      - `half_duplex_service_endpoint_unavailable`
      - `half_duplex_service_endpoint_disabled`
      - `half_duplex_service_preview_unavailable`
      - `half_duplex_service_playback_ack_unavailable`
    - the next focus is:
      - board regression of the 5.173 default-on matrix on the duplex-ready
        experimental profile
  - `5.173A` is now landed:
    - this slice is logging-only; no duplex behavior or negotiation policy was
      changed
    - the device now emits explicit collaboration snapshots at:
      - discovery refresh
      - `session.start`
      - discovery-refresh failure fallback
    - the device now warns on:
      - sparse `session.update` semantics
      - turn semantics arriving before accept
      - sparse / invalid preview payloads
      - preview events arriving before negotiation
      - playback-ack send failures with negotiated mode + last error
    - the next focus is:
      - use `5.173A` logs to validate the updated server-side collaboration
        chain once the service changes are available
  - `5.173B` is now landed:
    - this slice is also logging-only; no duplex policy or turn-state behavior
      was changed
    - XiaoZhi transport now latches and preserves accept-turn semantics across
      sparse `session.update` payloads within a round until the explicit
      session-update cache clear path runs for the next listen round
    - board logs now expose timing correlation across:
      - `input.speech.start`
      - `input.preview`
      - `input.endpoint`
      - `session.update.accept_reason`
      - `response.start`
      - `audio.out.meta`
    - `river xiaozhi status` now exports:
      - `timing_age_ms`
      - `timing_chain_ms`
    - the next focus is:
      - use `5.173A` + `5.173B` logs to validate the updated server-side
        collaboration chain once the service changes are available
  - `5.174` is now landed:
    - `no_ref` / half-duplex fallback playback no longer uses the same
      aggressive local barge-in thresholds as duplex-ready playback
    - local interruption in that path now requires stronger sustained evidence:
      - higher peak / ratio / margin gates
      - duck `3` hit frames
      - interrupt `12` hit frames
    - XiaoZhi downlink/playback buffering is now biased toward continuity:
      - ring `32`
      - start watermark `12`
      - playback buffer `6`
      - compact buffer `4`
    - the next focus is:
      - flash `5.174` to board and regress the original "未识别到有效语音 /
        只播片段" scenario against the current half-duplex service deployment
  - `5.175` is now landed:
    - local XiaoZhi playback gain is back to unity:
      - `5/2 -> 1/1`
    - strict `no_ref` barge-in no longer escalates to a hard interrupt:
      - the board now keeps ducking but logs
        `mode=no_ref_duck_only`
    - `write_failed` no longer clears playback state immediately:
      - the current frame is retained for retry
      - playback ACK timing pauses while rebuffering
      - resume now waits for a deeper `18`-frame watermark
    - the next focus is:
      - flash `5.175` to board and regress the original clipping /
        `write_failed` / short-playback scenario against the current
        half-duplex service deployment
- Primary active execution plan:
  - `doc/VOICE_RUNTIME_REARCHITECTURE_EXECUTION_PLAN_ZH.md`
- Latest runtime-owned adapter shrink slice:
  - adapter capture/I/O paths now delegate XiaoZhi uplink ingress and
    stream-finish closure to runtime:
    - `river_cloud_xiaozhi_trim_uplink_stale_frames(...)`
    - `river_cloud_xiaozhi_push_pcm(...)`
    - `river_cloud_xiaozhi_complete_active_stream_finish(...)`
  - adapter no longer owns XiaoZhi:
    - uplink stale-tail trimming
    - PCM accumulator framing
    - padded flush on active-stream finish
  - the next focus remains:
    - continue moving remaining XiaoZhi runtime truth/projection helpers out
      of `river_cloud_adapter.c`

## Current Runtime Focus

- Keep the board-side `wake -> VAD/KWS -> native realtime session` path usable.
- The current branch build now defaults to the dedicated duplex experiment
  profile while still preserving runtime fallback gates:
  - `fixed_dsb_webrtc_aecm`
  - native `2mic + ref(ch3)` capture
  - XiaoZhi full-duplex experiment advertisement on
- Replace the old XiaoZhi wire contract with direct `rtos-ws-v0` transport while
  keeping the existing upper cloud state machine temporarily stable.
- Current highest-priority runtime cleanup is now:
  - continue shrinking terminal ambiguity by aligning local-clear /
    data-plane terminal close / playback terminal truth around explicit
    runtime-owned facts
  - continue moving terminal-cause derivation behind the playback runtime so
    `dialog runtime` consumes exported truth instead of reconstructing terminal
    meaning from stop/active flags
  - keep shrinking `river_cloud_adapter.c` by separating provider lifecycle /
    policy from playback/session media engines
  - preserve the already-landed `dialog runtime` as the only
    interaction-state source while the playback rebuild moves forward
- Keep the landed collaboration baseline explicit and conservative:
  - accepted-turn is confirmed only from `session.update.accept_reason`
  - preview events remain observation-only
  - playback metadata remains playback-fact-only
  - stale `session.update` turn semantics are cleared before a new listen round
  - explicit fallback reasons are logged when the device stays on the current
    half-duplex/client-commit path
- Validate the collaboration baseline on board:
  - websocket subprotocol `agent-server.realtime.v0`
  - `ws://101.33.235.154:8080/v1/realtime/ws`
  - SDK handshake sends only one valid `Sec-WebSocket-Protocol`
  - SDK handshake strings remain NUL-terminated after copy into wsclient
  - SDK plain-`ws` connect path reports real `connect` errors instead of
    collapsing them into `Sending handshake failed`
  - discovery-backed `session.start.capabilities` negotiation
  - preview-aware input observation parsing and status exposure
  - full `segment_mark_v1` playback-truth ACK chain:
    - `audio.out.started`
    - `audio.out.mark`
    - `audio.out.cleared`
    - `audio.out.completed`
  - accepted-turn / fallback semantics alignment
  - `session.start` / `audio.in.commit` / `text.in`
  - PCM16 uplink and PCM16 downlink
- Drive current multi-step work from:
  - `doc/VOICE_RUNTIME_REARCHITECTURE_EXECUTION_PLAN_ZH.md`
- Keep the duplex execution plan as a supporting protocol/runtime baseline:
  - `doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md`
- Keep the XiaoZhi stability plan as a supporting baseline while duplex work
  continues:
  - `doc/XIAOZHI_SESSION_STABILITY_EXECUTION_PLAN_ZH.md`
- The next device-side duplex code slices are now explicitly staged as:
  - board regression and log validation of the landed `5.175` playback
    stabilization on the current half-duplex service path
  - after that, return to the landed `5.173` / `5.173A` / `5.173B`
    negotiation, sparse-semantics, and playback-ack diagnostics against the
    next server-side collaboration update
- Preserve the project flash profile:
  - `board/rtl8730e/profiles/RTL8730E_NOR.rdev`

## Start Here

- `AGENTS.md`
- `README.md`
- `build.md`
- `.codex/active_plans.md`
- `doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md`
- `doc/XIAOZHI_SESSION_STABILITY_EXECUTION_PLAN_ZH.md`
- the latest sections at the top of `.codex/changes.md` and `.codex/verification.md`
- `doc/README.md` for historical design and investigation documents

## Volatile-Context Rules

- Keep branch-specific or dated status snapshots out of `README.md` and
  `build.md`; those files should stay stable.
- Root `plan.md` is currently a historical `refactor`-branch snapshot, not the
  canonical active plan for today's branch.
- For larger work items, add or update an execution plan in `doc/` and
  register it in `.codex/active_plans.md`.
- After changing repo-level Codex harness files, run:
  - `python3 tools/diag/check_codex_harness.py`

## Latest Verified Step

- 2026-04-30 / branch `agent-server-v2`: Step 5.561 fixes the empty-turn follow-up dead path behind the 15:04 no-TTS log:
  - dialog runtime now clears stale `asr_session_active` from cloud runtime truth when no cloud round is active
  - XiaoZhi empty-turn follow-up recovery now runs from the IO tick and calls `open_session_and_listen()` inside the valid recovery window
  - transport/session closed handling defers recover out of the websocket event callback to avoid reentrant sends
- Verify with latest SDK `/root/ameba-rtos`:
  - `git diff --check`
  - `python3 tools/diag/check_codex_harness.py`
  - `python3 /root/ameba-rtos/ameba.py build -p` -> `Build done`
- Board expectation:
  - empty-turn active-return no longer leaves interaction stuck in `asr_streaming` without a real ASR round
  - logs show `xiaozhi empty turn followup recover: action=reopen_listen` when recovery is needed
  - the next spoken sentence reopens ASR before idle-timeout instead of leaving only VAD logs
