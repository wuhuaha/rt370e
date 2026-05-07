# Change Log

## Step A.home-ai.20
- 按当前收口目标，把本地唤醒词模型从
  `student_conv_resnet_ed_nano_v1_fp32_debug` 切换到
  `student_conv_resnet_ed_nano_current_teacher_a_v2_fp32_debug`，同时把
  `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15` 从调试态 `384` 拉回校准阈值附近。
- 本轮改动：
  - `Kconfig`
    - 新增
      `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_CONV_RESNET_ED_NANO_CURRENT_TEACHER_A_V2_FP32_DEBUG`
  - `prj.conf`
    - 关闭 `...NANO_V1_FP32_DEBUG`
    - 启用 `...NANO_CURRENT_TEACHER_A_V2_FP32_DEBUG`
    - 阈值改为 `9517`
  - `components/river_voice/river_voice_kws.cc`
    - 新增 teacher-a v2 变体分支
    - 绑定新的 generated header / symbol / variant name
  - `components/river_voice/generated/`
    - 从训练导出 bundle 机械复制
      `student_conv_resnet_ed_nano_current_teacher_a_v2_fp32_model_data.h`
- 这样切换后，板端仍保持原来的：
  - `40x101`
  - `n_fft=400`
  - centered log-mel frontend
  - conv-only resolver path
  但模型本体切到 teacher-a refresh 候选，阈值也不再停留在误报极高的 debug-low
  口径。
- Verification for this step:
  - `git diff --check` passed。
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'`
    completed with `Build done`。

## Step A.home-ai.19
- 根据 2026-05-07 16:52 这一轮最新日志，先收一刀最直接导致
  `抱歉，我刚没听清，请再说一遍。` 反复出现的上行拥塞问题：
  - 同一轮日志里已经出现明确证据：
    - `xiaozhi ws backpressure: kind=audio reason=soft_reserve ... free=2`
    - `xiaozhi uplink backpressure: ... streak=7 backoff=40ms`
    - `xiaozhi asr round finish: ... audio_ms=1760 ... pace_pct=49 ... partial=0 final=0`
  - 这说明当前板端在 websocket 轻微发送拥塞时，音频上行会被保守节流到接近
    半速；服务端拿不到完整命令，就会稳定落回 cached-response 的
    “没听清”兜底句，看起来像 TTS 在反复道歉，实质是上一段 ASR 没送完整。
- 本轮只改三处最小头间/追赶参数，不动协议和状态机：
  - `RIVER_XIAOZHI_WS_QUEUE_MAX: 16 -> 24`
    - 给 wsclient send queue 多留一些瞬时 headroom，减少音频在 soft-reserve
      阶段频繁被判 busy。
  - `RIVER_XIAOZHI_WS_AUDIO_QUEUE_RESERVE: 2 -> 1`
    - 继续保留控制消息插队空间，但不再让音频在还剩 2 个空槽时就被过早卡住。
  - `RIVER_CLOUD_XIAOZHI_UPLINK_DRAIN_BURST_MAX: 1 -> 2`
    - backlog 形成后，uplink worker 每轮允许多追一帧，避免一直卡在
      “发一帧、退避、再发一帧”的半速状态。
- 这一刀的目标很明确：
  - 先把 `pace_pct` 从当前日志里的 `49` 拉回接近实时
  - 减少 `xiaozhi ws backpressure: kind=audio reason=soft_reserve`
  - 降低服务端回 cached-response “没听清” 的频率
  - 这一步主要收 uplink；TTS 下行的 `underrun` 还需要下一轮继续压
- Verification for this step:
  - `git diff --check` passed。
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'`
    completed with `Build done`。

## Step A.home-ai.18
- 修复 Step A.home-ai.17 引入的一个会话早退回归：
  - 最新上板日志已经把问题指清楚了：
    - WebSocket 刚连上
    - 服务端先回 `session.update state=active input_state=active output_state=idle`
    - 此时还没有 `accept_reason`
    - 端侧却直接走了
      `xiaozhi single-turn output idle forced close -> response_audio_abandoned`
    - 于是刚建好的 session 被立刻关闭
- 根因：
  - `active + output_state=idle` 在 M1 下确实可作为“accepted 后等待响应异常”的
    收口信号
  - 但它**不能**在 pre-accept 阶段生效；刚建立会话时服务端处于
    `input_state=active / output_state=idle` 本来就是正常初始态
- 本轮修复：
  - `river_cloud_xiaozhi_clear_response_audio_wait_if_returned_active()` 现在增加
    双门槛：
    - `state->accepted == true`
    - `accepted_response_deadline_ms != 0`
  - 只有 accepted-turn 已经成立，并且确实进入“等待响应音频”窗口之后，
    才允许 `active + output_state=idle` 触发 abandoned 收口
- 目标效果：
  - 刚连接成功后的首个 `session.update active/idle` 不再误杀会话
  - 仍保留 accepted 后真正无响应时的单轮保守收口
- Verification for this step:
  - `git diff --check` passed。
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'`
    completed with `Build done`。

## Step A.home-ai.17
- 按最新服务侧 M1 文档把板端再收紧一轮，明确优先级是“先别错，再谈快”：
  - 服务侧当前主线已经明确是：
    - `client_wakeup_client_commit`
    - `preview_events=false`
    - `server_endpointing=false`
    - 单连接单 turn
  - 板端若还保留 empty-turn followup reopen、transport-closed followup recover
    这类多轮恢复旁路，就会在尾态残留时把问题放大成：
    - stale output guard
    - followup reopen blocked
    - 播放已结束但会话还卡在 active / speaking
- 本轮收口两件事：
  - M1 单轮保守化：
    - 新增本地判断：当 discovery 仍是 `no preview + no server endpoint` 时，
      视为 M1 单轮 client-commit 模式
    - 在该模式下禁用 empty-turn followup recover
    - 若服务端回到 `active + output_state=idle` 但本地又没有形成可闭环播放，
      直接按 `response_audio_abandoned` 收口，而不是继续 reopen listen
  - TTS 下行缓冲改成更保守的稳定优先配置：
    - downlink poll 从 `5ms` 收到 `2ms`
    - 起播门槛从 `16` 帧提高到 `20` 帧
    - startup burst 上限从 `8` 提到 `10` 帧
    - rebuffer 重起播门槛从 `28` 提到 `36` 帧
    - starved rebuffer 等待从 `240ms` 提到 `320ms`
    - playback backend buffer 从 `16/12` 提到 `24/16`
- 这一步的目标不是追最低起播时延，而是优先压掉：
  - 起播头噪
  - 尾部 underrun
  - 播放结束后的 stale/followup 乱状态
- Verification for this step:
  - `git diff --check` passed。
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'`
    completed with `Build done`。

## Step A.home-ai.16
- 按“稳定可靠 > 实时性 > CPU > 内存”的优先级，对 XiaoZhi 整条音频链路做一轮系统性收敛，先收两条最会反复制造假象的高风险路径，而不是继续在单点日志上来回补丁：
  - 上行 ASR 音频链路此前默认偏“低延迟优先”：
    - 在 transport 忙或本地 ring 积压时，会主动 trim stale frames
    - ring 写满后还会直接丢掉最老一帧再写新帧
    - 这类“静默丢旧音频”会把真实问题伪装成服务端没听清、截断、偶发空识别
  - 下行 TTS 播放链路此前默认偏“瞬时恢复优先”：
    - 起播后第一次 `AudioTrack_Write()` 失败就立刻升级成
      `service_recover -> managed_rebuffer`
    - 在当前 Ameba AudioTrack 状态机不稳定时，这会把短暂后端抖动放大成整段
      无声或 `write_failed -> stop_rebuffer` 死循环
- 本轮端侧硬化：
  - XiaoZhi uplink 改为“显式拥塞，不再静默裁剪音频”：
    - 取消 proactive stale trim
    - 取消 ring 满后“读掉最老帧再重写”的覆盖式策略
    - ring 满时直接返回 `RIVER_ERR_BUSY`，并记录
      `xiaozhi uplink ring full: ... enqueue_busy=...`
    - 同时把 uplink 累积缓冲和 ring 深度上调，给 transport 抖动留更大吸收带宽
  - XiaoZhi playback 改为“起播窗口容错，再进入恢复”：
    - 新增 `consecutive_write_failures`
    - 仅在 startup burst 窗口内，允许前 3 次连续写失败先 sleep/poll，
      不立即 stop/rebuffer
    - 一旦任意一帧成功写入，就把连续失败计数清零
    - 这样能把板端后端刚起播时的瞬时 invalid/busy 抖动和真正持续性故障区分开
- 目标效果：
  - 上行不再通过静默丢帧制造“没听清/后半句消失”的假象
  - 下行不再因为起播第一批帧的瞬态失败直接掉进整段无声恢复风暴
  - 后续如果仍有问题，日志会更直接暴露是 transport 拥塞、AudioTrack 后端不稳，
    还是真正的服务侧问题
- Verification for this step:
  - `git diff --check` passed。
  - `python3 tools/diag/check_codex_harness.py` passed。
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'`
    completed with `Build done`。

## Step A.home-ai.15
- 按“更稳的方法”重构 XiaoZhi TTS 的起播保护，不再继续依赖 SDK 的隐含行为：
  - 当前板子已经被上板日志证明：
    - 不支持 `AudioTrack_SetStartThresholdBytes`
    - 也不支持 deferred-write 的“先 write 再 start”
  - 这意味着稳定方案不能再建立在“也许 SDK 会替我卡住起播”这种假设上，
    必须把端侧自己的后端约束写清楚。
- 本轮端侧适配：
  - 保持 XiaoZhi TTS 走已验证合法的 `Start -> Write` 路径，不再碰 deferred-write。
  - 在下行 runtime 增加 `startup_burst_frames_left`：
    - 当后端刚成功启动，且本地队列已经积累了足够的短句音频时，
      进入一个受控的 startup burst 窗口。
    - burst 帧数按当前排队深度和起播门槛收敛，并额外上限为 6 帧，避免
      长流/普通 steady-state 被这个机制拖偏。
  - 目标是把最容易 underrun 的起播头部快速压进后端，减少“刚起播就空转”
    的概率，而不再触碰 SDK 不支持的状态机路径。
- 目标日志变化：
  - 起播后应看到
    `xiaozhi playback startup burst armed: queued=... burst_frames=... start=...`
  - 不应再出现 `AudioTrack-E] write: invalid state(1)`
  - 在短 cached-response 场景下，应先恢复可听声音，再观察是否还有首尾质量问题
- Verification for this step:
  - Step A.home-ai.15 `rg` verification matched
    `startup_burst_frames_left` state, startup-burst arming log, and
    `defer_start_until_prefilled = false`.
  - `git diff --check` passed。
  - `python3 tools/diag/check_codex_harness.py` passed。
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'`
    completed with `Build done`。

## Step A.home-ai.14
- 根据 2026-05-07 13:50 最新上板日志收口“完全没声音”：
  - 日志已经明确给出根因，不再需要猜测：
    - `playback start: ... deferred=yes`
    - 紧接着第一次写下行音频就报 `AudioTrack-E] write: invalid state(1)`
  - 这说明当前 Ameba SDK 这条 AudioTrack 路径并不支持“先 write 再真正 start”
    的预填充假设；上一轮把 XiaoZhi TTS 改成 deferred-write 虽然意图是绕开
    `underrun`，但在这块板子的真实状态机上反而直接把 TTS 变成无声。
- 本轮端侧适配：
  - XiaoZhi M1 TTS 取消 `defer_start_until_prefilled`，恢复 `AudioTrack_Start()`
    先发生的路径，先把可播放声音恢复成正确基线。
  - 保留前面已验证有效的两项修复：
    - `disable_track_reuse = true`
    - completed ACK 幂等 / 队列去重
  - 结论更新：
    - 这块 SDK 上“预填充后再 start”不是可行策略；
    - 后续若还要继续收短句首尾异常，应改为“start 后快速灌入后端”或下行
      worker burst 喂数，而不是再次回到 deferred-write 假设。
- 目标日志变化：
  - 不应再出现 `playback start: ... deferred=yes`
  - 不应再出现 `AudioTrack-E] write: invalid state(1)`
  - 应先恢复到“能出声”的基线，再继续观察是否还存在短句重复/截断
- Verification for this step:
  - Step A.home-ai.14 `rg` verification matched
    `config.defer_start_until_prefilled = false` for XiaoZhi TTS.
  - `git diff --check` passed。
  - `python3 tools/diag/check_codex_harness.py` passed。
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'`
    completed with `Build done`。

## Step A.home-ai.13
- 在 Step A.home-ai.12 的“预填充后启动”基础上，再次按 2026-05-07 11:43/11:44
  日志收口容易反复出现的同类问题，而不是继续只追单点现象：
  - 现有日志除了 `AudioTrack_SetStartThresholdBytes not supported` 和裸
    `underrun`，还出现了同一 `response_id/playback_id` 的
    `xiaozhi playback ack completed queued/sent` 被重复打印。
  - 这说明当前问题不只是“起播过早”，还包含播放尾态收口的幂等门不够硬；
    即使听感修复，重复 completed 也会继续让 stop/drain/terminal 路径互相踩。
- 本轮端侧适配：
  - `playback completed` 增加更严格的幂等门：
    - 若当前 `last_segment_context` 对应的 completed 已经进入 lineage /
      terminal ack，则后续路径不再二次排队。
  - XiaoZhi control queue 对异步 `PLAYBACK_COMPLETED` 再加一层底层去重：
    - 对同一 `response_id + playback_id` 的 pending completed 请求直接合并，
      避免不同收口路径在队列里并发塞入重复 completed。
  - 这样“预填充后启动”负责收 `underrun`，而“completed 去重”负责收
    尾态重复 ACK；两层一起才能避免同类问题反复回潮。
- 目标日志变化：
  - 同一 `playback_id` 只应出现一次
    `xiaozhi playback ack completed queued`
  - 同一 `playback_id` 只应出现一次
    `xiaozhi playback ack completed sent`
  - 配合 Step A.home-ai.12，短 cached-response 不应再同时出现
    `underrun`、重复开头、尾部截断、completed 双发
- Verification for this step:
  - Step A.home-ai.13 `rg` verification matched the completed dedupe gate in
    terminal-ack logic and the pending-request dedupe in the XiaoZhi control
    queue.
  - `git diff --check` passed。
  - `python3 tools/diag/check_codex_harness.py` passed。
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'`
    completed with `Build done`。

## Step A.home-ai.12
- 根据 2026-05-07 11:43/11:44 上板日志继续收 M1 cached-response 的短句重复和尾部截断：
  - Step A.home-ai.11 已经生效，最新日志明确显示
    `playback start backend call: stream=xiaozhi_tts ref=no reuse=no`，说明旧
    AudioTrack 复用路径已经被切断。
  - 但同一批日志继续出现 `AudioTrack_SetStartThresholdBytes not supported`，
    随后还有裸 `underrun`；而起播前本地其实已经有
    `queued=72` 帧，足够覆盖 `expected_duration_ms=1440` 的整句短 TTS。
  - 这说明当前主问题不再是服务端供给不足，也不再是旧 track 残留，而是
    Ameba AudioTrack 忽略 start-threshold 后，播放服务过早调用
    `AudioTrack_Start()`，硬件在真正写满前就开始跑，最终把短 cached-response
    打成重复开头、后半句截断或裸 `underrun`。
- 本轮端侧适配：
  - `river_playback_stream_config_t` 新增 `defer_start_until_prefilled` 开关。
  - 播放服务新增软件侧 start gate：可在 `start_stream()` 阶段先 prepare track
    但不立刻 `AudioTrack_Start()`，改为统计实际 `AudioTrack_Write()` 成功写入的
    字节数。
  - 当预写入达到 track buffer 阈值后，再显式启动底层 track，并打印
    `playback deferred start: ... prefetched=... threshold=...`。
  - XiaoZhi M1 TTS 固定启用该预填充起播；其它播放入口保持原有立即起播行为。
- 目标日志变化：
  - `playback start: ... reuse=no deferred=yes`
  - 在看到 `playback deferred start: ... prefetched=15360B threshold=15360B`
    之后，才出现 `ameba_audio_stream_tx_start`
  - 不应再出现裸 `underrun`
  - `抱歉，我刚刚没听清，请再说一遍。`、`好的，已经打开了。` 这类短
    cached-response 不应再出现重复开头或后续内容被截断
- Verification for this step:
  - Step A.home-ai.12 `rg` verification matched `defer_start_until_prefilled`
    in the playback config, deferred-start gate, and XiaoZhi TTS wiring.
  - `git diff --check` passed。
  - `python3 tools/diag/check_codex_harness.py` passed。
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'`
    completed with `Build done`。

## Step A.home-ai.11
- 根据 2026-05-07 11:02 上板日志继续收短 cached-response 播放重复开头和尾部截断：
  - 本轮日志中 `cached_response expected_duration_ms=1440 is_last_segment=yes`，起播时已有 `queued=72` 帧，说明完整短句已经进入本地播放队列。
  - 前一轮 `upstream_starved` 重缓冲风暴已经消失；这次异常日志的关键变化是 `playback start backend call ... reuse=yes`，随后出现裸 `underrun`。
  - Ameba 当前 `AudioTrack_Flush` 明确打印 `NOT SUPPORTED`，复用同一个 AudioTrack 容易把上一轮未真正清空的硬件/SDK 缓冲带到下一轮短 TTS，表现为开头重复、尾部被挤掉或截断。
- 本轮端侧适配：
  - `river_playback_stream_config_t` 新增 `disable_track_reuse` 开关。
  - 播放服务在该开关打开且已有 cached track 时，先销毁旧 AudioTrack，再重新 create/init，避免依赖不支持的 flush 清空旧播放实例。
  - XiaoZhi M1 TTS 播放固定启用该开关；其它播放入口保持原有默认复用策略。
- 目标日志变化：
  - M1 `xiaozhi_tts` 起播应显示 `reuse=no` 和 `playback_start_new`，不再显示 `reuse=yes` / `playback_start_reuse`。
  - 短 cached response 不应再先重复播放上一轮残留的“抱歉”。
  - 若后续仍出现裸 `underrun`，下一步应收口 AudioTrack 起播前预灌，而不是再调 `upstream_starved` rebuffer。
- Verification for this step:
  - Step A.home-ai.11 `rg` verification matched `disable_track_reuse` in the playback config, fresh-track path, and XiaoZhi TTS wiring.
  - `git diff --check` passed。
  - `python3 tools/diag/check_codex_harness.py` passed。
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`。

## Step A.home-ai.10
- 根据 2026-05-07 10:11 上板日志修复 cached-response 只播出“你想”后无声的问题：
  - 服务端返回 `cached_response expected_duration_ms=1360 is_last_segment=yes`。
  - 端侧起播时已有 `queued=68` 帧，按 20 ms/帧正好覆盖完整 1360 ms 音频，说明完整短句已经进入本地播放队列。
  - 播放约 240 ms 后队列剩 `queued=12`，旧 `upstream_starved` 逻辑把最后 12 帧尾音误判为上游断流，开始反复 `AudioTrack_Flush -> tx_close -> CreateAudioHwStreamOut -> playback recover`。
  - 这会直接截断后半句，表现为只听到“你想”，后续没有声音。
- 本轮端侧适配：
  - 将最后一段 rebuffer 防护从“已完整写入播放后端”扩展为“已写入播放后端的帧 + 本地队列剩余尾音帧”覆盖 `expected_duration_ms`。
  - 只有当前播放 segment 与已知 last segment context 完全一致时才启用该保护，避免影响多段流式音频的真实断流恢复。
  - 对已本地持有完整音频的最后一段，禁止进入 `upstream_starved` recover，让尾音自然 drain 并由 terminal ACK 路径收口。
- 目标日志变化：
  - 对 `expected_duration_ms=1360`、起播 `queued=68` 的 cached response，不应在 `queued=12` 时进入 `xiaozhi playback upstream gap rebuffer`。
  - 不应出现连续几十/上百次 `AudioTrack_Flush -> tx_close -> CreateAudioHwStreamOut -> playback recover`。
  - 应能完整听到“你想让我帮你控制什么设备？”并正常完成播放尾态。
- Verification for this step:
  - Step A.home-ai.10 `rg` verification matched the local-complete last-segment guard and `upstream_starved` skip wiring.
  - `git diff --check` passed。
  - 初次 build 发现新增 helper 缺少 `river_cloud_xiaozhi_downlink_frame_duration_ms()` 前置声明，已修复。
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`。

## Step A.home-ai.9
- 根据当前服务侧实际部署端口修正 M1 默认连接地址：
  - 直接探测 `101.33.235.154:8081` 通过：
    - TCP connect ok
    - `ws://101.33.235.154:8081/v1/realtime/ws` 返回
      `101 Switching Protocols`，并协商
      `Sec-WebSocket-Protocol: agent-server.smart-home.realtime.v1`
    - `http://101.33.235.154:8081/v1/realtime` 返回 `200 OK`
  - 对比探测 `101.33.235.154:8082` 当前失败：
    - TCP / WS / HTTP 均为 `Connection refused`
  - 项目默认固件地址仍停在
    `ws://101.33.235.154:8082/v1/realtime/ws`，这会导致板端按旧端口连接失败。
- 本轮端侧适配：
  - `RIVER_XIAOZHI_URL` 改为
    `ws://101.33.235.154:8081/v1/realtime/ws`。
  - M1 计划、活动上下文和最新验证说明同步使用 `8081`。
- Verification for this step:
  - Step A.home-ai.9 endpoint `rg` 检查通过，默认 URL 和当前板端验证命令均指向 `101.33.235.154:8081`。
  - `python3 tools/agent_server_debug/probe_realtime.py --host 101.33.235.154 --ports 8081 --schemes ws http --subprotocol agent-server.smart-home.realtime.v1` passed：TCP connect ok，WebSocket 返回 `101 Switching Protocols` 且 subprotocol 正确，HTTP discovery 返回 `200 OK`。
  - `git diff --check` passed。
  - `python3 tools/diag/check_codex_harness.py` passed。
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`。

## Step A.home-ai.8
- 按当前调试诉求降低本地 KWS 唤醒主阈值：
  - 当前 `student_conv_resnet_ed_nano_v1_fp32_debug` 的正常召回档为
    `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=9008`，约 `thresh_pm=274`。
  - 调试阶段用户反馈经常无法唤醒，优先保证 wake -> conversation window
    链路可反复验证。
  - 本轮只调整 `prj.conf` 的主触发阈值，不改模型、前端、stride、hold、
    cooldown、tensor dump 或 board/local 对拍路径。
- 本轮端侧适配：
  - `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15` 从 `9008` 临时降到 `384`。
  - 注释保留 `9008` 作为后续唤醒词模型质量优化完成后的恢复目标。
- 目标日志变化：
  - `kws backend ... threshold_q15=384`
  - `kws status ... thresh_pm=11`
  - 弱一些的有效唤醒词输入也更容易触发 `wakeword hit ... mode=threshold`。
- Verification for this step:
  - Step A.home-ai.8 `rg` verification matched the debug threshold and restore note in `prj.conf`。
  - `git diff --check` passed。
  - `python3 tools/diag/check_codex_harness.py` passed。
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`。

## Step A.home-ai.7
- 根据 2026-05-06 17:30 新上板日志，继续收 M1 播放结束后无法再次唤醒/跟进的尾态残留：
  - 当前 TTS 已能正常起播并进入 `draining -> idle`，也已经发送 `audio.out.completed`。
  - 但播放完成后 dialog runtime 仍从 `barge_in_listening -> asr_streaming reason=cancel_stop terminal_closed=yes`，而不是回到可跟进/可唤醒状态。
  - 同时服务端可能只持续发送 sparse `response_started=yes`，没有及时把 `output_state=speaking` 清成 idle；端侧旧的 output guard 恰好排除了 `output_state=speaking/thinking`，导致残留状态无法自愈。
- 本轮端侧适配：
  - dialog runtime 不再把 `input_state=committed` 当作活跃 ASR round；`committed` 只表示本轮输入已提交，捕获已经结束。
  - playback truth view 不再让已 terminal-closed 的 response context 继续维持 `playback_turn_active=yes`。
  - stale output guard 放宽到可覆盖 `output_state=thinking/speaking` 或残留 playback turn；只要没有真实 playback active、没有 rebuffer、没有等待音频响应，就允许计时后强制清理 stale output turn。
- 目标日志变化：
  - 播放完成后不应再从 `barge_in_listening` 回到长期 stale 的 `asr_streaming`。
  - `output_turn_guard ... output_state=speaking playback_turn=yes playback_active=no rebuffer=no` 不应长期阻断后续语音。
  - 第一轮 TTS 完成后，应能再次唤醒或进入正常 follow-up reopen。
- Verification for this step:
  - Step A.home-ai.7 `rg` verification matched committed-input exclusion, terminal-closed playback turn exclusion, and stale output guard recovery coverage.
  - `git diff --check` passed。
  - `python3 tools/diag/check_codex_harness.py` passed。
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`。

## Step A.home-ai.6
- 根据 2026-05-06 16:52 新上板日志继续收 M1 `cached_response` 播放尾态异常：
  - 当前 720 ms `cached_response` 在 `audio.out.meta` 后已经成功起播，日志显示 `queued=36`、`playback start`、`audio.out.started sent`，说明“不出声”主问题已从 Step A.home-ai.5 收住。
  - 但随后仅过约 130 ms，端侧就在 `queued=0` 时误触发 `xiaozhi playback upstream gap rebuffer: cause=upstream_starved`，并立刻进入 `AudioTrack_Flush -> tx_close -> CreateAudioHwStreamOut -> playback recover`。
  - 这会带来两个直接副作用：
    - 喇叭还在自然排空本地后端缓冲时被强制 flush/recreate，起播前后容易听到一声短促噪音。
    - playback runtime 长时间保留 `output_state=speaking` / `playback_turn` / `rebuffer=yes`，follow-up reopen 持续被 `output_turn_guard` 阻断，看起来像“只能唤醒一次”。
- 本轮端侧适配：
  - 给每个播放 segment 增加 `pushed_duration_ms`，统计该段已经成功写入本地播放后端的音频时长。
  - 成功写每个 fixed 20 ms downlink frame 后，先确保当前 segment 已进入 `started`，再把该帧对应的时长累加到 `pushed_duration_ms`。
  - 对当前已知的最后一段：如果 `expected_duration_ms` 对应的音频已经完整推给本地播放后端，则不再把 software ring 见底误判成 `upstream_starved`，改为只走 terminal drain/ACK 闭合。
  - 这样可以避免对已完整下推的 cached-response 尾巴执行多余的 `flush/recover`，同时减少起播噪音和输出回合卡死。
- Verification for this step:
  - Step A.home-ai.6 `rg` verification matched `pushed_duration_ms`, fully-pushed last-segment guard, and starved-rebuffer skip wiring.
  - `git diff --check` passed。
  - `python3 tools/diag/check_codex_harness.py` passed。
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`。

## Step A.home-ai.5
- 根据 2026-05-06 16:12 “已收到 `audio.out.meta` / `output_state=speaking` 但板端完全无声”的新日志，继续收 M1 下行裸 PCM 播放链路：
  - `Step A.home-ai.4` 已经放宽 WebSocket RX buffer，当前不再是 SDK 提前丢包，而是服务端已经开始回音频、端侧仍未真正起播。
  - 结合 `/root/home_ai_server/src/home_ai_server/realtime_audio_output.py` 现状可确认：M1 服务端在 `audio.out.meta` 之后直接发送裸 `pcm16le` binary，但每条 websocket binary 的大小并不等于端侧播放层的固定 20 ms frame。
  - 旧端侧把“一条 websocket binary 消息”直接当成“一帧 downlink audio”入队；当服务端发大块 cached-response 或任意大小 streaming-TTS chunk 时，`queued_frames`、ring frame size 和 backend 起播门槛都会被破坏，最终表现为服务端已 `speaking`、板端却听不到任何音频。
- 本轮端侧适配：
  - 下行 runtime 新增 `accum_bytes` 和 `xiaozhi_downlink_accum`，专门缓存未对齐到固定播放帧的裸 PCM 尾巴。
  - 新增按 `sample_rate + frame_duration_ms` 计算固定单声道 PCM frame 字节数的 helper，默认按 M1 当前 `16 kHz / 20 ms / pcm16le` 落到 640 B 一帧。
  - downlink worker 不再把整条 websocket binary 直接写进 ring，而是把任意大小 chunk 重新切成固定帧逐帧入队；不足一帧的尾巴留在 accum 中，等下一批 binary 拼满。
  - 在 `response_id/playback_id` 切换和 `output_state=idle` 尾态闭合前，若仍有未满一帧的 accum，会先 zero-pad flush 成最后一帧入队，避免尾包静默丢失。
  - playback meta 清理时同步清空 accum，避免不同 segment / response 之间串音。
- Verification for this step:
  - Step A.home-ai.5 `rg` verification matched `accum_bytes` / `downlink_accum` state, fixed-frame helper, meta-id-change flush, and output-idle flush wiring.
  - `git diff --check` passed。
  - `python3 tools/diag/check_codex_harness.py` passed。
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`。

## Step A.home-ai.4
- 根据 2026-05-06 14:45 新上板日志修复 M1 cached-response 音频被 WebSocket SDK 丢弃的问题：
  - 服务端返回 `segment_kind=cached_response`、`expected_duration_ms=720` 的可播放段后，随后发送单个 23044 B binary PCM 消息。
  - 旧端侧 `RIVER_XIAOZHI_WS_RX_MAX=12288`，Ameba wsclient 在 `ws_dispatchBinary/ws_poll` 阶段直接打印 `exceed the max rx buf` 并丢弃整条音频消息，River 播放层完全收不到 binary。
  - 结果是 playback 卡在 `prefetching` / `output_turn_guard`，后续 follow-up reopen 也被长期挡住。
- 本轮端侧适配：
  - 将 XiaoZhi 专用 WebSocket RX buffer 从 12 KB 提升到 64 KB。
  - 注释同步记录问题来源：当前 `/root/home_ai_server` 会把 cached-response 音频整段作为单条 websocket binary 发送，现有 M1 cache 候选长度可到约 1.5 s，因此端侧按约 2 s 的 16 kHz mono pcm16le 预算预留 RX。
  - 保持 TX buffer 和发送队列配置不变，只扩大下行接收上限。
- Verification for this step:
  - Step A.home-ai.4 `rg` verification matched the 64 KB XiaoZhi RX buffer constant and the `create_wsclient(..., RIVER_XIAOZHI_WS_RX_MAX, ...)` wiring.
  - `git diff --check` passed。
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`。

## Step A.home-ai.3
- 根据 2026-05-06 上板日志修复 M1 文本响应播放尾态：
  - 服务端对纯文本动作结果发送 `audio.out.meta`，其中 `segment_kind=text_only`、`expected_duration_ms=0`、`is_last_segment=yes`，且不会跟随 PCM binary。
  - 旧端侧没有识别 `segment_kind`，仍把该 meta 当成待播放音频段进入 prefetch，随后持续打印 `response output idle treated as playback terminal`，形成无音频等待/尾态刷屏。
- 本轮端侧适配：
  - WebSocket 事件新增并传递 `segment_kind`。
  - `audio.out.meta` 解析兼容 `buffered_duration_ms`，避免流式 TTS 在 `duration_ms=null` 但有 buffered audio 时被误判为文本段。
  - `text_only`、`stream_tts_empty`、`stream_tts_error` 且 last/0ms 的 meta 走文本段快速完成路径：不入播放队列，直接本地闭合播放尾态并排队 `audio.out.completed`。
  - session active/idle 回报若播放尾态已经关闭，只清理等待/看门狗并打印 `response output idle already terminal`，不再落到 empty-turn 恢复或反复 finalize。
- Verification for this step:
  - Step A.home-ai.3 `rg` verification matched `segment_kind` parsing, text-only fast completion, and already-terminal active/idle guard.
  - `git diff --check` passed。
  - `python3 tools/diag/check_codex_harness.py` passed。
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`。

## Step A.home-ai.2
- 按当前部署信息更新 `home_ai_server` 默认服务端地址：
  - 默认 WebSocket URL 从 `ws://101.33.235.154:8080/v1/realtime/ws`
    改为 `ws://101.33.235.154:8082/v1/realtime/ws`。
  - 当前 M1 适配计划和板端验证说明同步使用 `101.33.235.154:8082`。
- Verification for this step:
  - Step A.home-ai.2 endpoint `rg` 检查通过，默认 URL 和当前板端验证命令均指向 `101.33.235.154:8082`。
  - `python3 tools/agent_server_debug/probe_realtime.py --host 101.33.235.154 --ports 8082 --schemes ws http` passed：TCP connect ok，WebSocket 返回 `101 Switching Protocols`，HTTP discovery 返回 `200 OK`。
  - `git diff --check` passed。
  - `python3 tools/diag/check_codex_harness.py` passed。
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`。

## Step A.home-ai.1
- 对 `/root/home_ai_server` 做协议走查后，端侧 M1 最小切换已经落到 `home-ai` 分支：
  - 默认 realtime wire profile 改为 `protocol_version=rtos-smart-home-v1`、`subprotocol=agent-server.smart-home.realtime.v1`，并默认关闭旧 MCP。
  - discovery 解析补齐 M1 顶层 `features`，能从 `features.server_endpointing=false`、`features.preview_events=false` 和 `voice_collaboration.playback_ack.mode=started_completed_v1` 得到正确协商结果。
  - playback ACK 协商从旧 `segment_mark_v1` 收缩为 `started_completed_v1`，started/cleared/completed 可用，mark 不再作为 wire ACK 协商或排队发送。
  - `session.start` 从旧 canonical `device/audio/session/capabilities` 形状收缩为 home_ai 简化 payload：`rtos_device_id`、`client_type`、`wake_reason`、`mode_hint=m1_single_command`、`input_audio`、`output_audio`、`capabilities`。
  - `audio.in.commit` 增加 M1 推荐的 `commit_reason` 字段，同时保留旧 `reason` 兼容字段。
  - `audio.out.meta` 解析兼容 M1 `duration_ms`，映射到端侧现有 `expected_duration_ms` 播放完成逻辑。
  - 播放状态机仍维护本地 segment progress；当 mark ACK 未协商时只更新本地进度/lineage，不再队列 `audio.out.mark` 控制请求。
- 新增并登记 M1 适配执行计划：
  - `doc/HOME_AI_SERVER_M1_ADAPTATION_PLAN_ZH.md`
  - `.codex/active_context.md`
  - `.codex/active_plans.md`
- Verification for this step:
  - Step A.home-ai.1 `rg` 协议适配检查通过。
  - `git diff --check` passed。
  - `python3 tools/diag/check_codex_harness.py` passed。
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`。

## Step 5.563
- 根据上板反馈“TTS 只有前半段、没有后半段”，复查 Step 5.562 后确认新的截断风险：
  - Step 5.562 会在服务端 `output_state=idle` 后合成 terminal tail，解决缺少 `is_last_segment=yes` 的卡死
  - 但 terminal segment 的完成仍主要按 `expected_duration_ms` 墙钟推进；若后续 audio binary 迟到，端侧可能先 pop last segment、发送/进入 completed，再把迟到帧当成 `late completed audio` 丢弃
  - 这会表现为前半段能播，后半段被 stop/completed 路径截掉
- 本轮把 terminal tail 完成条件收紧：
  - last segment 达到 expected duration 后，还必须等待 downlink ring 为空、retry frame 为空
  - 最近一次 downlink audio supply 必须静默至少 `RIVER_CLOUD_XIAOZHI_PLAYBACK_DRAIN_MS`
  - playback backend 已短暂 inactive 时，只有满足同样严格的 terminal-tail drain 条件才允许推进 ACK/fully-heard，避免修复后反向卡死
  - `output_state=idle` 下的 fully-heard 合成也必须等 downlink supply 静默，避免把仍在路上的尾部音频提前视为已听完
- 目标日志变化：
  - 不应再出现 terminal completed/stop 后继续大量 `late completed audio dropped` 或后半段缺失
  - 若服务端音频尾包迟到，terminal tail 会继续保持 current segment，直到队列与供给都 drain 后再 completed/cleared
- Verification for this step:
  - Step 5.563 `rg` verification matched the terminal-tail quiet-supply guard paths
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`

## Step 5.562
- 根据 2026-04-30 17:01 新日志，前一轮“完全无 TTS”已变成“TTS 起播后段间/尾态卡死”：
  - `_0001` 与 `_0002` 的 `audio.out.meta` 都是 `is_last_segment=no`
  - `_0001` 播完后端侧进入 `waiting_next_segment` / segment-gap hold，但后续 `_0002` 下行音频没有继续消费
  - ring 最终堆到 `queued=96 capacity=96` 并持续 `downlink ring overflow`，服务端随后 `session.end idle_timeout`，`audio.out.cleared` 因 transport closed 发送失败
- 本轮优化 playback 状态机的三个卡点：
  - ACK 进度不再在 paused/recovering 且没有 stop drain 的状态下按墙钟推进，避免未实际播放的段被误 mark/pop
  - 当 wait-context 指向的非 last 段还未 fully-heard、段队列为空但下行帧已经到达时，从 wait/meta 上下文恢复当前 segment，让 worker 继续消费 queued audio；segment-gap hold 已暂停后也不再在 queued 超过 hold 水位时永久保持 paused
  - 服务端返回 `state=active output_state=idle` 且端侧已见 playback meta 时，把最后观测的 meta 本地提升为 terminal tail，驱动后续 drain/completed，而不是继续等待服务端不会再补发的 `is_last_segment=yes`
  - 若服务端已走正常 terminal meta 路径，active/idle 也按 playback tail 处理，不再回落到 empty-turn 分支
- 保持 Step 5.559 的合同约束：
  - 不恢复“等待同一 `segment_id` 第二条 meta 修正 last”的旧逻辑
  - 只在服务端明确 output idle 或端侧 wait-context 与 queued audio 明确冲突时做本地收口/恢复
- 目标日志变化：
  - 段间不再持续 `downlink ring overflow`
  - 必要时出现 `xiaozhi playback wait segment recovered`
  - 服务端 active/idle 后出现 `xiaozhi playback terminal synthesized from output idle`
  - 尾态应在 transport close 前完成 `audio.out.completed` / `audio.out.cleared` 队列或发送
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - Step 5.562 `rg` verification matched the output-idle terminal and wait-segment recovery paths
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`

## Step 5.561
- 复查 Step 5.560 后，确认 15:04 “没任何 TTS”的日志不能只用半速 uplink 解释；更关键的异常链路是：
  - 服务端返回 `state=active input_state=active output_state=idle` 后，端侧发布成 `thinking -> asr_streaming reason=empty_turn_returned_active`
  - 但没有新的 `listen_start` / `asr round begin`
  - 后续用户说话只剩 VAD 日志，说明端侧交互状态和云端真实 listen/round 状态脱节
- 本轮收口两个状态机问题：
  - dialog runtime 在云端 runtime 可用时，把 cloud round snapshot 作为 `asr_session_active` 的权威来源；当云端 round 已经不活跃时主动清掉 stale ASR active，避免 empty turn 后继续投影成 `asr_streaming`
  - XiaoZhi empty-turn recover 不再只等 transport close；IO tick 会在恢复窗口内主动执行 `empty_turn_followup_recover -> open_session_and_listen()`，重新准备 follow-up listen
  - transport/session closed 事件里不再同步重开 websocket/session，而是只标记 `transport_closed_recover_deferred`，把 reopen 放到下一轮 IO tick，避免在 websocket poll 回调中重入发送导致卡在 collaboration negotiation 日志后
- 目标日志变化：
  - empty turn 后应先回到 follow-up/可恢复状态，不再长期卡 `asr_streaming` 但没有实际 ASR round
  - 若需要恢复，应看到 `xiaozhi empty turn followup recover: action=reopen_listen`
  - 后续用户语音应重新出现 `asr round begin` / `asr stream active`，而不是只有 VAD `speech/silence`
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`

## Step 5.560
- 根据 2026-04-30 15:04 新日志，当前“没任何 TTS”不是 playback ACK/尾态问题，而是 uplink 在首轮 ASR 里只跑到了约半速：
  - `xiaozhi asr round finish` 显示 `audio_ms=1380 duration_ms=2812 pace_pct=49`
  - `packets=69` 恰好等于 `69 * 20 ms = 1380 ms`
  - 同时 `preview_warmup ... bypass=0 backlog_max=2`，说明端侧虽然存在 1~2 帧 backlog，但 warmup catch-up 门槛过严，根本没进入追平路径
- 这轮只修 uplink warmup 门槛，不碰服务端协议和 playback：
  - `river_cloud_xiaozhi_uplink_drain_burst_limit()` 从 `ready_frames > 1` 放宽到 `ready_frames > 0`
  - `river_cloud_xiaozhi_uplink_should_bypass_frame_pacing()` 也同步改成 `ready_frames > 0`
  - 并补注释说明：`ready_frames` 统计发生在当前帧已经从 ring 读出之后，因此值为 `1` 就已经代表“当前发送帧后面还压着 1 帧 backlog”
- 目标是让 preview warmup 在最常见的 `backlog=1~2` 场景也能立即追平，不再卡在 `bypass=0 / pace_pct≈50`，避免服务端因为只收到半速音频而把这轮收成 empty turn。
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`

## Step 5.559
- 服务侧已改为“不再对同一 `segment_id` 二次补发 `audio.out.meta` 修正 `is_last_segment`”，端侧相应去掉了这条旧兼容假设，避免 playback 尾态继续等一个不会再来的 same-segment promotion。
- 本轮只做 playback meta/ACK 收口，不碰 wake / uplink / session 协议：
  - `river_cloud_xiaozhi_playback_note_meta()` 不再把同一 `response_id + playback_id + segment_id` 的后续 `audio.out.meta` 当成 `false -> true` last 升级处理；
  - duplicate same-segment meta 现在只打印 `xiaozhi playback duplicate same-segment meta ignored: ...` 观察日志，不再驱动 prefetch / recover / waiting_next_segment / terminal fold；
  - `wait_context / last_segment_context` 只按首次入队的 segment 事实更新，不再被同段后续 meta 改写。
- 目标是和服务侧新语义对齐：
  - 端侧只按第一次收到的 segment 元数据继续上报真实 `started / mark / completed / cleared`
  - 不再等待同一 `segment_id` 的第二条 `audio.out.meta`
  - 收掉由 same-segment late-meta 兼容路径残留出来的 playback/output-turn 卡滞空间
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`

## Step 5.558
- 根据 2026-04-29 10:17 新日志，当前体验差的主因已经不是“尾段被提前 pop”单点，而是三条链路叠加：
  - 唤醒后首轮把 wakeword 尾音当成真实命令开流，服务侧连续 accepted 出多个空轮次；
  - 长单段 TTS 因 `segment_prefetch` 直接按整段 expected duration 预取，起播前沉默过长；
  - 长段音频中途只要上游 161 ms 级别短抖动就触发 `upstream_starved -> recover/flush`，造成中途卡顿。
- 这轮只做三刀端侧窄修，不混入新的协议改造：
  - 首轮 wakeword 会话新增一次 `drop_first_wake_preroll_once`，第一次开流不再把 wakeword 尾音 pre-roll 重放给服务端；
  - `segment_prefetch` 启播门槛新增上限 `RIVER_CLOUD_XIAOZHI_DOWNLINK_SEGMENT_START_CAP_FRAMES=40`，长单段回复不再为了“等整段”而额外沉默 1s+；
  - `RIVER_CLOUD_XIAOZHI_DOWNLINK_STARVED_REBUFFER_MS` 从 `120` 提到 `240`，避免 100 多毫秒的短暂上游抖动就直接 recover/flush。
- 目标对应新日志里的三个直接症状：
  - 收掉 `家。/看一下。/一调灯。` 这种 wake 后残留空 accepted turn；
  - 把 `audio.out.meta -> playback start` 的长空等压下来；
  - 减少 `xiaozhi playback upstream gap rebuffer ... supply_gap_ms=161` 这类短 gap 触发的中途断句。
- Verification for this step:
  - `git diff --check` passed
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`

## Step 5.557
- 继续彻底修播放尾段被切的问题；这次不再只盯 same-segment promotion，而是直接收 `ack_progress -> pop current -> waiting_next_segment -> recover/flush` 这条提前切尾链路。
- 根因是端侧 `update_playback_ack_progress()` 按 wall clock 计算 `played_duration_ms`，一旦达到 `expected_duration_ms` 就立刻把当前 segment 标 fully-heard 并 `pop`：
  - 但这时 AudioTrack/backend buffer 里的尾音通常还没真正从喇叭播完；
  - 随后队列瞬时变空，runtime 会转入 `waiting_next_segment`，再被 `segment gap hold / playback recover` 拉到 flush/restart；
  - 结果就是主回答段后半截尾巴被切掉。
- 这轮修复改成“尾段 drain grace”：
  - 当当前 segment 已跑到 `expected_duration_ms`，但队列里还没有 successor，且该 segment 还不是 last 时，先保留它，不立即 `pop`；
  - 仅在经过一个 `RIVER_CLOUD_XIAOZHI_PLAYBACK_DRAIN_MS` 的尾段排空窗口后，才允许它从队列里退出；
  - 这样 same-segment late-meta promotion 或真实 successor meta 都有机会在 flush 前赶到。
- 这样做的直接效果是：
  - 不再过早进入 `waiting_next_segment`
  - 不再在尾音仍在 backend buffer 时触发 `playback recover` / `AudioTrack_Flush`
  - 即使服务端 promotion 迟到，最多也是在尾音实际排空后再进入后续切换，不会先把可听尾巴切掉
- Verification for this step:
  - `git diff --check` passed
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`

## Step 5.556
- 继续按服务侧建议收紧 same-segment meta promotion 语义，不再只覆盖“尾段已播完再补 last”的一个窄窗口。
- 现在当 `response_id + playback_id + segment_id` 相同，且只是 `is_last_segment: false -> true` 时，端侧按“原 segment 的元数据升级”处理，不再进入新分段路径：
  - 不再走普通 `note_meta -> prefetch -> refresh_playback_phase`
  - 不再因为这次 promotion 触发 recover、prefetching、segment gap hold、stop
- 若该 promotion 命中“当前 tail 已播到 expected mark，且处于安全折叠窗口”，仍优先直接本地折叠闭环：
  - 继续复用 `river_cloud_xiaozhi_fold_late_last_segment_meta_for_current_tail()`
- 若还没到可直接折叠的时机，也只做原 segment 的 in-place 升级：
  - 更新 `expected_duration_ms/text/is_last_segment`
  - 刷新 `last_segment_context`
  - 但不会把它当成新 segment 重新驱动 playback 状态机
- 这样就和服务侧建议对齐：只有 `segment_id` 真变化时，才进入真正的新分段切换逻辑。
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`

## Step 5.555
- 开始收 `playback/meta` 侧尾巴：同一个 `segment_id` 先以 `is_last_segment=no` 发布，尾段实际播完后又迟到补一条 `is_last_segment=yes`，本地之前会把这个迟到升级当成普通重发，导致 `_0002` 这种尾段进入 `recover/gap hold/stop` 抖动。
- 这轮只修一个窄问题：当当前播放尾段已经播到 expected mark、backend 正处于 `waiting_next_segment`，而服务端又用同一个 `segment_id` 补发 “late last meta upgrade” 时，端侧不再重新发布这个 stale tail。
- 现在在 `river_cloud_xiaozhi_playback_note_meta()` 里增加一条 upgrade 快路：
  - 仅命中“已有有效 segment、旧 meta 不是 last、新 meta 改成 last”的同段升级；
  - 并且仍要求 `fold_late_last_segment_meta_for_current_tail()` 判定当前 tail 已经真正播完、处于 paused/waiting_next_segment 安全窗口；
  - 命中后直接把当前 tail 折叠成 fully-heard/last，不再走普通 `note_meta` 重发布路径。
- 目标是先收掉你最新日志里的这类模式：
  - `_0002 expected_duration_ms=1100 is_last_segment=no`
  - mark 已跑到 `1100`
  - 随后同一 `_0002` 又迟到补成 `is_last_segment=yes`
  - 端侧不应再因为这次补报进入额外 `playback recover` / `segment gap hold`
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`

## Step 5.554
- 继续按顺序压 preview/uplink backlog，这一轮只改端侧 uplink warmup/catch-up，不碰 playback ACK。
- 之前 uplink drain 是严格单帧节拍：
  - `RIVER_CLOUD_XIAOZHI_UPLINK_DRAIN_BURST_MAX=1`
  - `RIVER_CLOUD_XIAOZHI_UPLINK_PREVIEW_BURST_MAX=1`
  - 即使任务已经晚了、ring 里也还有 backlog，成功发完一帧后仍会把 `next_send_ms` 推到下一拍，几乎没有真正 catch-up 能力。
- 现在恢复一个受控的 preview warmup burst：
  - `RIVER_CLOUD_XIAOZHI_UPLINK_PREVIEW_BURST_MAX` 提到 `3`
  - 仅当 ASR round 仍在 `preview_warmup` 窗口内，且队列里确实还有后续帧时，uplink drain 才临时放宽 burst 上限
  - steady-state 常态 drain 仍保持 `1` 帧，不改长期节拍
- 成功发送后，如果仍处于 preview warmup 且队列里还有 backlog：
  - 本轮会临时 bypass 一次 20 ms 帧间 pacing，允许同一个 drain 周期继续追发
  - 并把 `preview_warmup_bypass_count` 记到现有 round metrics，便于后续对照 backlog/first_partial 改善幅度
- 目标很明确：
  - 先把“preview owner 刚启动时已经落后半拍到几拍，但端侧仍严格一拍一发”的人为积压削掉
  - 降低 `preview_uplink_realtime_ratio` 掉到 0.5x 左右的概率
  - 不把 steady-state uplink 改成长期突发发送
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`

## Step 5.553
- 继续按顺序修 endpoint soft-close 过激；上一轮先收了 false accept 入口，这一轮改本地 endpoint hint 的收口时机。
- 之前 `RIVER_CLOUD_XIAOZHI_ENDPOINT_SOFT_CLOSE_DEFER_MS=320`，只给了一个很短的 hint-only 等待窗口；一旦服务侧 preview refresh / finalize 稍微晚一点，本地就可能先用 `endpoint_soft_close_timeout` 把 active stream 关掉。
- 现在把 hint-only defer 从 `320 ms` 放宽到 `960 ms`：
  - 目标不是延后真正的 accept，而是给 server-owned endpoint path 多留一轮 preview refresh / finalize 的补救空间。
  - 仍明显短于 `RIVER_CLOUD_XIAOZHI_LOCAL_CLOSE_DEFER_MS=2000`，不会把本地 silent fallback 直接拖成长期悬挂。
- 同时把 `input_preview` 的 soft-close 取消条件从“只有 `text` 非空”放宽到“`text` 或 `stable_prefix` 任一非空”：
  - 只要 preview 已经有实际文本进展，就先撤销本地 endpoint soft-close，不再因为 payload 只更新 `stable_prefix` 就继续倒计时。
- 这一轮改动仍然是端侧窄修：
  - 不碰服务侧 accept 判定；
  - 不碰 playback ACK；
  - 只降低 preview 还在变好时被端侧过早截断的概率。
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`

## Step 5.552
- 先按优先级收 false accept 入口，先不碰服务侧 accept 判定，先把端侧最容易制造 stale residual turn 的 no-ref reopen 门槛抬高。
- 当前 no-ref follow-up reopen 只要求 `RIVER_CLOUD_XIAOZHI_NOREF_OPEN_HOLD_FRAMES=6`，也就是 120 ms 连续语音就能重开 ASR；这和服务侧 turn3 的 `audio_bytes=3840` / 120 ms 残留量级正好重合。
- 端侧现在把 no-ref reopen hold 提到 `12` 帧，也就是 240 ms：
  - 默认 full-duplex / quiet-window 路径不受影响，仍走 `RIVER_CLOUD_XIAOZHI_OPEN_HOLD_FRAMES=2`。
  - 只有 `playback_allows_vad_open()==false` 的 no-ref reopen 路径，才要求更长的连续语音。
- 目标很窄：先把“播放尾边 residual 只有百毫秒出头，也能重开 follow-up round”这一条端侧入口收紧，优先削掉 turn3 这类 120 ms false accept 样本。
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`

## Step 5.551
- 根据 2026-04-28 17:12 新日志，上一轮 `late completed audio drop` 已经把尾段 `cancel_stop/arm_stop` 抖动收住；新的主卡点进一步收敛到多 segment 交接时的 started-ack 上下文发布竞态：
  - `audio.out.meta` 已经明确给出第二段 `_0002` 的 `playback_id/segment_id`；
  - 但端侧仍在 `_0001` 收尾、切到 `_0002` 的瞬间打印：
    - `xiaozhi playback ack started send failed: status=-1 response_id=resp_... playback_id=- segment_id=-`
    - `xiaozhi playback ack started queue failed: status=-1 response_id=- playback_id=- segment_id=-`
  - 这说明 downlink worker 在 `pop current -> start next` 的边界上，读到了一个已经“对外可见”但尚未填完 ids 的 next segment。
- 根因在 `river_cloud_xiaozhi_playback_note_meta(...)`：
  - 新 segment 旧逻辑会先 `segment->valid = true`、`count++`，再写入 `response_id/playback_id/segment_id/expected_duration_ms`；
  - 一旦此时当前 head 恰好被 pop，downlink worker 会立刻把这个 next slot 当 current segment 使用，从而带着半初始化 ids 去发 `audio.out.started`。
- 修复方式保持最小：
  - 新 segment 仍先选好 tail slot 并 `memset`；
  - 但把 `valid/count` 的“发布动作”延后到 ids、text、duration、`is_last_segment` 全部填完之后；
  - 并补一条注释，明确这是为了避免 segment 交接时 started-ack 读到 partial ids。
- 这一步的目标很窄：只收口“第二段 `_0002` started-ack 因 partial publish 丢上下文”这一条并发发布竞态，先把多段播放从 `idle_timeout` 挂死拉回正常 completed/clear 收口。
- Verification for this step:
  - `git diff --check` passed
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`

## Step 5.550
- 根据 2026-04-28 16:20 新日志，empty-turn / EOF 那条 accepted 无响应死锁已经不再是主问题；新的主卡点变成 playback 尾段的 late-audio stop 抖动：
  - 在最终 `played_duration_ms` 已达到 last-segment `expected_duration_ms`、terminal 事实上已经 completed ready 后，端侧仍可能收到迟到音频帧。
  - 旧逻辑会无条件写入这些 late audio，并在 `playback_terminal_open()` 下调用 `cancel_playback_stop()`，于是出现反复：
    - `draining -> playing reason=cancel_stop queued=1 segments=0`
    - `playing -> draining reason=arm_stop queued=1 segments=0`
  - 这会把 playback 尾态拖长，导致 speaking/barge-in 保护窗口异常膨胀，并把后续 turn 推向更脆弱的时序。
- 新增 late-completed audio drop：
  - 当满足 `stop_pending=yes` 且 `playback_completed_ready()` 时，迟到 audio frame 不再写入 downlink ring，也不会再取消 stop。
  - 新日志为 `xiaozhi late completed audio dropped: ...`。
  - drop 时顺便重跑一次 `maybe_complete_terminal_playback_after_progress("late_completed_audio")`，确保 terminal-complete 收口继续推进。
- 这一步的目标很具体：先切断“尾帧迟到 -> cancel_stop -> drain/playing 来回抖动”这条链，再观察 turn 3 的服务侧 400 是否随之消失。
- Verification for this step:
  - `git diff --check` passed
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`

## Step 5.549
- 针对服务侧已确认的 `accepted -> active/idle` 空语音收口语义，端侧不再把 `accepted` 写死为“必然会收到 `response.start`”：
  - 在 `session.update accept_reason=...` 收口本地 round 后，新增 `RIVER_CLOUD_XIAOZHI_ACCEPTED_RESPONSE_WATCHDOG_MS=6000`。
  - 若后续确实收到了 `response.start`，watchdog 会立即清掉，继续沿原有播放链路走。
  - 若服务侧把本轮按空语音直接收回 `active/idle`，端侧会明确记录 `xiaozhi empty turn returned active: ...`，同步清 preview / turn semantics，并重新 touch follow-up window。
- 新增 narrow empty-turn recovery state，专门兜住“accepted 但没有 response.start”的合法 silent-recovery 路径：
  - `empty_turn_recover_deadline_ms` 只在 empty-turn active-return 场景下建立，不污染正常 TTS 回合。
  - follow-up window 内若随后发生一次 `transport_closed`，且本地没有 active stream / playback turn、Wi-Fi 仍在线，端侧会直接 `open_session_and_listen()` 重开当前会话窗口，而不是把这一轮永久打死。
  - recovery 失败时仍按原路径 `window_close("transport_recover_open_failed")` 收口，避免反复重试。
- 同时补了一条 endpoint 端自恢复 watchdog：
  - 若 accepted 后既没有 `response.start`，也没有回到 empty-turn active/idle，而是一直卡住，watchdog 超时会 `request_abort("accepted_response_watchdog")`，清理 session-update cache / preview / turn semantics，并重新同步 follow-up state。
  - 目标不是替代服务端语义，而是把端侧从“accepted 后永远等不到下一步”的僵态拉回可恢复状态。
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh >/dev/null; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`

## Step 5.548
- 复查 Step 5.547 后，继续补上另一类会把 follow-up 永久卡死的残留态：`playback_lane` 已经松开，但 `playback_turn` 仍因 terminal truth 残留而保持打开。
  - 这类问题和 Step 5.547 不同，不一定还挂在 `WAITING_NEXT_SEGMENT`；更像是：
    - 物理播放已经结束
    - 本地 queue / wait_context 已经空了
    - 服务端 `output_state` 也已经不是 `thinking/speaking`
    - 但 `playback_turn_active=yes` 仍然阻断 `maybe_start_followup_round(...)`
  - 旧的 stale-output guard 只在 `playback_lane_engaged=yes` 时触发，因此这类“terminal-only stale turn”会一直卡住 reopen，只剩 VAD。
  - 现在把 stale-output guard 的命中条件扩成：
    - 仍要求 `window_active=yes`
    - `stream_active=no`
    - `output_state` 不是 `thinking/speaking`
    - `response_waiting_audio=no`
    - `playback_turn_active=yes`
    - `playback_output_active=no`
    - `playback_rebuffer_pending=no`
  - 不再强依赖 `playback_lane_engaged=yes`，因此 guard 同时覆盖：
    - lane 残留
    - terminal-only turn 残留
  - guard 日志额外输出 `wait=yes/no`，便于区分是 segment-gap / wait-context 卡住，还是 terminal truth 单独残留。
- 这一步的目标不是放宽 output-turn 保护，而是把“所有本地已经不再输出、但 turn 仍残留”的死锁态都纳入同一条 720ms 语音自恢复兜底。
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`

## Step 5.547
- 再审视多轮对话 reopen/output-turn guard 之后，补了一条端侧兜底机制，目标不是替代主状态机，而是在 local playback truth 残留时避免整轮永久失活：
  - 新增 `RIVER_CLOUD_XIAOZHI_STALE_OUTPUT_GUARD_MS=720`
  - `river_cloud_xiaozhi_maybe_start_followup_round(...)` 在 `output_turn_guard` 阻断时，新增一条非常窄的“stale output-turn”判定：
    - `window_active=yes`
    - `stream_active=no`
    - 服务端 `output_state` 已不在 `thinking/speaking`
    - `response_waiting_audio=no`
    - 本地 `playback_lane_engaged=yes`
    - 本地 `playback_turn_active=yes`
    - 但 `playback_output_active=no`
    - 且 `playback_rebuffer_pending=no`
  - 这说明不是正常播放/思考保护，而是“本地 output-turn 真相源残留”：
    - 第一次命中时只 arm guard，并打印 `xiaozhi stale output guard armed: ...`
    - 若用户持续说话超过 `720ms` 仍未恢复，则打印 `xiaozhi stale output guard forcing playback clear: ...`
    - 端侧本地触发一次 `xiaozhi_stale_output_guard` playback interrupt/clear
    - 然后立即重评 follow-up reopen，让同一句话继续进入 ASR，而不是只能等到 `idle_timeout`
  - window close / abort / transport reset / listen reopen 时同步清掉 guard deadline，避免旧 guard 污染下一轮会话
- 这一步的设计取舍：
  - 不去放宽正常 `thinking/speaking/rebuffer` 保护，避免把“用户插话”误当成 stale state
  - 只对“服务端已不输出、物理播放也不活跃、但本地 turn 仍残留”的异常态出手
  - 兜底优先保证多轮对话最终可恢复，而不是继续依赖所有尾态路径都 100% 正确
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`

## Step 5.546
- 收口 2026-04-28 14:30 新日志里的“播完 `已帮你打开灯光。` 后再说话只剩 VAD、完全不 reopen”残留尾态：
  - 这次问题不再只是“queue 已空但 late last-meta 没折叠”，而是 current playback segment 仍残留在队列头
  - 日志模式是：final mark 已经到 `played_duration_ms=1100`，runtime 已进入 `OWNED_PAUSED + WAITING_NEXT_SEGMENT`，服务端随后才补发同一 `segment_id is_last_segment=yes`
  - 旧逻辑在这种“current stale tail”场景下仍会把迟到 terminal meta 当普通 `note_meta` 处理，output turn 继续被旧 tail 占住，后续说话只有 VAD、没有 ASR reopen
  - 现在新增 `river_cloud_xiaozhi_fold_late_last_segment_meta_for_current_tail(...)`：
    - 要求 same-segment current tail 已 started；
    - backend 已是 `OWNED_PAUSED`；
    - supply 已是 `WAITING_NEXT_SEGMENT`；
    - `output_active=no`、`tts_stop_pending=no`、`rebuffer_pending=no`；
    - 且 `last_mark_ms >= expected_duration_ms`（零时长尾段则要求存在非零 mark）
  - 命中后直接把 current stale tail 记为 fully-heard、弹出当前 segment，再复用既有 late-last-meta fold/completed 收口
  - 新增 `xiaozhi playback late last meta consumed stale current tail: ...` 日志，并在 `late last meta folded` 中补充 `consumed_current=yes/no`，便于区分命中的尾态类型
- 预期上板变化：
  - 对 14:30 这类 final mark 已到但 current tail 未退出的日志，late same-segment `is_last_segment=yes` 不会再把 playback 卡在 `wait_next=yes`
  - 回复播完后，后续一句话会重新进入 ASR，而不是只剩 VAD `speech/silence` 到 session timeout
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`

## Step 5.545
- 新增端侧实时性优化设计文档 `doc/VOICE_RUNTIME_REALTIME_OPTIMIZATION_DESIGN_ZH.md`，把当前项目中影响语音流畅性/实时性的结构性问题整理为完整技术设计：
  - 明确当前系统的核心矛盾不是功能缺失，而是“热路径被慢路径污染”
  - 梳理了六类关键问题：采集路径同步等待、控制面混队列、KWS gate 带锁读取、固定时延参数过硬、默认日志过重、barge-in 两端失衡
  - 提出目标架构：硬实时音频层、实时调度层、控制编排层、诊断与策略层四层分离
  - 提出关键改造方向：同步 control request 改异步 intent、urgent/telemetry 分队列、runtime gate snapshot、follow-up 意图保留、参数 profile 化、日志分级
  - 给出分阶段落地建议、指标体系和兼容性约束
- 文档目的：
  - 为后续“端侧实时性优先”的代码重构提供统一设计基线
  - 把零散性能/体验问题提升为可执行的架构优化方案
- Verification for this step:
  - `git diff --check` passed
  - `test -f doc/VOICE_RUNTIME_REALTIME_OPTIMIZATION_DESIGN_ZH.md` confirmed the new realtime design document exists
  - `rg -n "热路径|异步 intent|urgent control queue|lock-free|参数策略化|日志分级" doc/VOICE_RUNTIME_REALTIME_OPTIMIZATION_DESIGN_ZH.md` confirmed the core optimization sections were recorded

## Step 5.544
- 新增交互体验审查文档 `doc/VOICE_INTERACTION_HUMANIZATION_REVIEW_ZH.md`，把当前 XiaoZhi 语音链路在“人性化 / 智能化交互”上的结构性问题整理成单独研究：
  - 梳理了 `wakeword -> follow-up -> thinking -> speaking -> barge-in -> tail close` 主链路
  - 明确指出当前实现更偏“状态正确性优先”，而非“用户意图优先”
  - 归纳出五类核心问题：guard 期间用户意图丢失、wakeword 无强制接管、barge-in 两端失衡、恢复缺少自解释、关键时序参数缺乏自适应
  - 给出按优先级排序的优化方向：意图保留、wakeword override、三段式 barge-in、低成本恢复反馈、参数 profile/动态化
- 文档目的：
  - 作为后续交互体验优化的设计输入，而不是继续把体验问题零散埋在日志分析里
  - 让后续改动有统一目标：从“只修尾态”升级到“按用户意图设计状态机”
- Verification for this step:
  - `git diff --check` passed
  - `test -f doc/VOICE_INTERACTION_HUMANIZATION_REVIEW_ZH.md` confirmed the new review document exists
  - `rg -n "用户意图优先|强制接管|三段式|自解释|动态调整" doc/VOICE_INTERACTION_HUMANIZATION_REVIEW_ZH.md` confirmed the key review sections were recorded

## Step 5.543
- 收口 2026-04-28 13:52 新日志里的“播完 `已帮你打开灯光。` 后后续再说无响应”残留尾态：
  - 日志显示同一 `segment_id=..._0002` 已经打到 final mark `played_duration_ms=1100`
  - 随后 playback 落入 `playing -> prefetching ... wait_next=yes physical=no backend=owned_paused`
  - 服务端接着对同一 `segment_id` 晚到补发 `is_last_segment=yes`，但此前 fold 路径只在 `fully_heard_context` 已经对齐时才能命中
  - 若当时只有 `marked_context` 已对齐、且 `last_mark_ms >= expected_duration_ms`，旧逻辑会漏掉 terminal fold，尾态继续挂住直到 session idle-timeout
  - 现在对这类 same-segment late last-meta 允许用 `marked_context + last_mark_ms` 合成 `fully_heard_context`，再复用既有 late-last-meta fold 路径收口
  - 新增 `xiaozhi playback late last meta synthesized fully-heard: ...` 日志，便于直接确认是否命中了这条补救路径
- 预期上板变化：
  - 播完 `已帮你打开灯光。` 这类单段主回答后，即使 `fully_heard_context` 还没先对齐，晚到的 same-segment `is_last_segment=yes` 也会直接收口
  - 不再停在 `prefetching/backend=owned_paused` 然后只剩 VAD speech/silence 日志直到 `idle_timeout`
  - 下一句应重新进入 ASR / follow-up，而不是“第一句有反应、第二句无反应”
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`

## Step 5.542
- 收口 2026-04-28 13:34 新日志里的“只说一句、后续再说无响应”残留尾态：
  - 服务端会对同一 `segment_id` 先下发普通 meta，尾部再补一条同 `segment_id` 的 `is_last_segment=yes`
  - 端侧此前在该 segment 已经 `fully_heard` 且已出队后，仍把这条 late last-meta 当成新 segment 重新入队，导致 playback 卡在 `owned_paused/prefetching`，output turn 一直不退出
  - 现在对“late last-meta + same fully-heard segment + queue 已空头”的场景直接折叠为尾态更新，不再重建 segment 队列
  - 若此时 backend 只是 `OWNED_PAUSED` 挂着旧尾帧，会丢弃这批 stale queued tail、停止 paused backend，并立刻尝试 completed 收口
- 预期上板变化：
  - 不再出现 `segment_id=...0003` 已经打到最终 mark 后，又因为同 `segment_id is_last_segment=yes` 重新卡在 `prefetching/backend=owned_paused`
  - 播放完 `我没听清，请再说一遍。` 后，后续再说话可以重新进入 ASR，而不是一直到 `idle_timeout`
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`

## Step 5.541
- 收口 2026-04-28 新一轮 invalid-voice 复测暴露的三个残留问题：
  - playback completed ACK 在 queue/sent 入口新增 `completed_reported` 早退，并在 `COMPLETED_QUEUED` lineage touch 后立刻同步 terminal report flags，避免同一 `playback_id` 重复 `completed queued/sent`
  - dialog runtime 的 output-turn engaged 判定现在把 `playback lane engaged` 视为充分条件，覆盖 `audio.out.meta` 已到但物理播放尚未 active、以及 stop-pending/drain 尚未真正 detach 的窗口，避免错误回到 `asr_streaming`
  - transport close 终态策略在 stop playback 前先清 session update cache、preview state 和 turn semantics，并主动请求一次 state sync，避免关连接瞬间仍带着旧 `previewing` / accepted truth 发布交互态或 poll 日志
- 预期上板变化：
  - 同一 response/playback 只出现一次 `xiaozhi playback ack completed queued/sent`
  - `audio.out.meta` 到真实 `playback start` 之间，以及 `tts_stop_pending=yes` 的 drain 尾态期间，不再出现 `... -> asr_streaming reason=playback_state`
  - `transport_closed` 后不再继续打印带旧 `input_state=previewing` 的 `xiaozhi turn accepted: trigger=poll ...`
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`

## Step 5.540
- 修复“播完 `未识别到有效语音` 后后续说话无响应”的尾态卡死：
  - XiaoZhi playback terminal 在已经判定 completed 后，不再因为晚到的 downlink audio 无条件 `cancel_playback_stop()`
  - 这样 zero-duration ACK 在 stop-pending / drain 期间不会被重新拉回 `playing`，避免长时间卡在 `barge_in_listening`
  - `playback completed` 改为 backend stream 已退出 attached/active 后再真正 queue/sent，避免 DAC 尚未 drain 完就提前报 completed
- 修正 transport closed 的状态发布顺序：
  - `reset_transport_state()` 先清理 stream/preview/turn 语义，再决定是否发 `session_closed`
  - 避免连接关闭时用旧的 `previewing` / `stream_active` 投影把交互态错误发布成 `asr_streaming`
- 预期上板变化：
  - 出现服务端 `未识别到有效语音。` 后，播放尾态应正常 stop，不再卡住后续 follow-up / 再次说话
  - 不再出现 `playback phase: draining -> playing reason=cancel_stop queued=1 segments=0`
  - transport close 时不应再出现 `barge_in_listening -> asr_streaming reason=playback_state` 这类带旧输入语义的收口
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'` completed with `Build done`

## Step 5.539
- 收口 zero-duration fast-launch ACK 的播放尾态判定：
  - XiaoZhi playback terminal 对外 `terminal_closed` 不再只看 terminal state，而是要求 backend 已退出 output-active 且不再 `tts_stop_pending`
  - 这样 zero-duration ACK 在 DAC 还处于 drain 时，dialog runtime 仍会把当前 output turn 视为 engaged，不会因为本地 terminal 提前结束而退回 `asr_streaming`
  - 对 dialog/runtime snapshot 暂时隐藏 drain 中的 terminal completed/local-completed，可见 close 时刻延后到真实播放尾巴结束
- 补足交互态切换诊断：
  - dialog runtime 在真正发布状态切换前新增 `dialog_runtime interaction_transition: ...` 日志
  - 日志显式带出 `terminal_closed`、`playback_active`、`tts_stop_pending`、`duplex_ready_seen`
  - runtime snapshot / status dump 也同步携带 `playback_duplex_ready_seen`
- 预期上板变化：
  - zero-duration ACK 尾态不应再出现播放仍在进行时就 `barge_in_listening -> asr_streaming reason=arm_stop`
  - 若仍有 `half_duplex_aec_blocked`，其前序 transition 日志也应显示 `terminal_closed=no` 或 `tts_stop_pending=yes`，便于继续定位 AEC/ref 尾态
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `python3 /root/ameba-rtos/ameba.py build -p` completed with `Build done` against `/root/ameba-rtos`

## Step 5.528
- 修复 2026-04-27 日志中 `audio.out.meta` 后端侧播放/采集连锁问题：
  - `output_state=speaking` 且 playback lane 已经 engaged 时，dialog runtime 现在保留输出轮次，不再因为 `note_meta` 把交互态错误推回 `asr_streaming`
  - follow-up reopen 在 speaking 但物理播放/AEC 尚未 ready 时继续阻断，避免 `audio.out.meta` 后立即开空 ASR round
  - XiaoZhi downlink task 优先级降到 VAD/capture consumer 之下，避免 AudioTrack 启动路径阻塞时让 mic capture ring 堆满
  - 使用 native capture reference 的配置下，XiaoZhi TTS 启动不再额外打开 playback reference export，降低 AudioTrack start 与参考服务耦合风险
  - playback service 在 backend prepare / AudioTrack_Start 前新增明确日志，若板端仍卡住可直接定位卡在 prepare 还是 start 调用
- 预期上板变化：
  - `audio.out.meta` 后不应再看到 `interaction_state: thinking -> asr_streaming reason=note_meta`
  - 不应再出现 `duration_ms=4 audio_ms=0 packets=0` 的空 follow-up ASR round
  - 即使 AudioTrack 启动异常，也不应持续刷 `capture frame ring overflow`；日志会停在更明确的 playback backend prepare/start 位置
- Verification for this step:
  - `rg -n "RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_PRIO|native_capture_ref|playback start backend prepare|playback start backend call|playback_lane=|RIVER_DIALOG_OUTPUT_LANE_SPEAKING" ...` confirmed guards/log points
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `python3 /root/ameba-rtos/ameba.py build -p` completed with `Build done` against `/root/ameba-rtos`

## Step 5.527
- 修复 2026-04-26 新日志中残留的一次重复 `audio.in.commit`：
  - 当服务端已经通过 preview 下发 `input.endpoint candidate=yes`，且 discovery/negotiation 表明 server endpoint 可用时，端侧 post-roll 不再发送本地 `audio.in.commit`
  - 该路径改为打印 `xiaozhi server endpoint candidate suppresses local audio.in.commit ...`，并以 `server_endpoint_candidate` 收口本地 ASR round，等待服务端 accept
- 增强 wire-level commit 保险：
  - `river_xiaozhi_send_audio_commit_internal()` 在 session 非 `active`、input 已 `committed`、output 为 `thinking/speaking` 或 response 已开始时直接跳过发送
  - skip 日志携带 session/input/output/accept/response 状态，便于确认是否仍有 stale commit 请求进入发送入口
- 该步不改变 no-audio 事实边界：
  - 仍不伪造 `audio.out.meta` 或 playback fact
  - 服务侧未下发音频时仍由 `response audio abandoned` 收口
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `python3 /root/ameba-rtos/ameba.py build -p` completed successfully against `/root/ameba-rtos` with approved SDK write permission
  - `python3 tools/diag/check_codex_harness.py` passed
  - `python3 /root/ameba-rtos/ameba.py build -p` completed with `Build done` against `/root/ameba-rtos`

## Step 5.526
- 修复 2026-04-26 板端 no-audio 日志暴露的两个端侧问题：
  - 服务端 accepted 后、response 尚未完成前，端侧不再因为本地 VAD speech 立即重开 follow-up ASR round
  - response pending 期间如检测到 speech，会打印一次 `xiaozhi followup reopen blocked: reason=response_pending ...`，同时清零 open-hold 计数
- 暂停默认本地 no-audio fallback prompt：
  - 新增 `RIVER_CLOUD_XIAOZHI_LOCAL_RETRY_PROMPT_ENABLED=0`，避免 AudioTrack start/write 在现场路径未验证前阻塞 capture/VAD 消费
  - no-audio abandoned / timeout recovery 仍会清理 response wait 和 session/window，但只记录 prompt skipped，不再启动本地播放
  - 保留 unsafe-dialog-state / playback-active guard，后续若重新打开本地 prompt，需要先满足非 ASR、非 listening、非 pending close 的安全条件
- 该步目标是先阻断日志中出现的 capture frame ring overflow 连锁故障，后续再单独验证本地 prompt 的 AudioTrack 非阻塞实现。
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `python3 /root/ameba-rtos/ameba.py build -p` completed with `Build done` against `/root/ameba-rtos`

## Step 5.525
- 对齐 2026-04-26 服务侧 realtime 首音频失败语义：
  - 服务侧现在只有拿到首个真实 audio chunk 后才进入 `speaking` / 发送 `audio.out.meta`
  - 如果首音频等待失败，服务侧会返回 `session.update state=active / output_state=idle`，且不会下发可播放 PCM
- 端侧新增 no-meta abandoned 收口：
  - `response.start` 后若尚未观察到 `audio.out.meta`，并且 turn semantics 刷新到 `active/idle`，清理 response-audio wait lineage
  - 打印 `xiaozhi response audio abandoned` 与 `xiaozhi response audio wait cleared`，用于区分“服务已放弃本次音频”与“服务完全悬挂”
  - 不发送 `audio.out.started / mark / completed`，继续保持 playback facts 只能由 `audio.out.meta` + 实际播放驱动
- 保留 Step 5.523 的 5s timeout recovery：
  - 若服务侧既不回 `active/idle` 也不发 `audio.out.meta`，仍由 `response_audio_timeout` abort/close 释放本地 session/window
- Verification:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed with `check_codex_harness: all checks passed`
  - `python3 /root/ameba-rtos/ameba.py build -p` completed with `Build done` against `/root/ameba-rtos`

## Step 5.523
- 在 Step 5.522 的 response audio wait timeout 诊断基础上增加本地恢复策略：
  - `river_cloud_xiaozhi_check_response_audio_timeout()` 现在返回本次是否新触发 timeout
  - post-poll / post-uplink housekeeping 捕获首次 timeout 后执行 `response_audio_timeout` recovery
  - recovery 日志输出 session、listening、stream、window、`input_state`、`output_state`，方便和服务侧 speaking 卡住问题对齐
- timeout recovery 的本地收口动作：
  - 请求 `tts interrupt` / server abort，促使服务侧停止当前卡住的 speaking response
  - 用 `RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_SERVER_RESPONSE` 收口本地 ASR round，避免后续再走本地 `audio.in.commit`
  - 关闭 conversation window 并请求关闭 XiaoZhi session，让设备尽快回到可再次唤醒状态
- 保持播放事实边界不变：
  - 无 `audio.out.meta` / binary PCM 时不伪造 playback start
  - 服务端恢复正常音频下发时，5s 内收到 meta 就不会触发 timeout/recovery
- Verification:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `python3 /root/ameba-rtos/ameba.py build -p` completed with `Build done` against `/root/ameba-rtos`

## Step 5.522
- 基于服务侧对 2026-04-25 最近真实设备日志的定位，继续收口“文本 response 已到但无音频”的端侧表现：
  - ASR/LLM 主链路已经能进入 `response.start` / `response.chunk`
  - 失败发生在后续 `audio.out.meta` / binary PCM 长时间不到达，服务侧 qwen_unified TTS 单并发队列堵塞是主要原因
  - 端侧此前把 realtime 文本 response 合成为本地 `TTS start/sentence_start`，容易误导日志与 round/playback 判断
- 拆分 realtime 文本 response 与音频播放事实：
  - `response.start` / `response.chunk` 只保留 realtime response/text 日志与 response lineage
  - 不再从 realtime 文本 response 合成 `RIVER_XIAOZHI_EVENT_TTS`
  - legacy `tts` 文本消息仍保留原 `RIVER_XIAOZHI_EVENT_TTS` 兼容路径
  - 音频播放事实继续只由 `audio.out.meta` 与 binary PCM 驱动
- 新增 response 后音频等待诊断：
  - `response.start` 会重置 playback lineage 并记录 response id
  - 若 `5s` 内未观察到 `audio.out.meta`，打印 `xiaozhi response audio wait timeout`
  - timeout 日志带上 `response_id`、等待时长、`input_state`、`output_state`、accepted 状态、playback 活跃状态和已排队音频帧数
- Verification:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `python3 /root/ameba-rtos/ameba.py build -p` completed with `Build done` against `/root/ameba-rtos`

## Step 5.521
- 分析 2026-04-25 板端 XiaoZhi 日志后，修正 server endpoint 已提交后的重复 `audio.in.commit`：
  - 日志显示服务端已返回 `accept_reason=server_endpoint` 且 `input_state=committed`
  - 之后本地 post-roll 再发送 `audio.in.commit`，服务端按当前状态拒绝并返回 `turn_not_ready`
  - 该错误会把 interaction 推入 `error_recovering`，随后 VAD 触发同一 server session 内的重复 ASR round/reopen loop
- 新增本地语义收口：
  - `river_cloud_xiaozhi_refresh_turn_semantics()` 在 accepted + `input_state=committed` 时关闭本地 round
  - `RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_SERVER_RESPONSE` 现在允许在 `listening=yes` 的本地窗口内完成关闭
  - `river_xiaozhi_send_audio_commit_internal()` 增加二道保险，语义缓存已 committed 时跳过 wire-level `audio.in.commit`
- 对“无声音”的当前判断：
  - 本次日志中服务端只下发 `response.start` 和 `response.chunk` 文本
  - 未看到 `audio.out.meta` 或 binary PCM，下行播放链路没有可播放输入
  - 该现象需要继续从服务端 TTS/audio 输出或协议 capability 字段定位，设备端本步仅避免状态机被重复 commit 错误污染
- Verification:
  - `git diff --check` passed
  - `python3 /root/ameba-rtos/ameba.py build -p` completed with `Build done` against `/root/ameba-rtos`

## Step 5.520
- 继续治理 XiaoZhi WS receive path，把 legacy 文本消息处理从 realtime message handler include 中拆出：
  - `river_xiaozhi_ws_message_handlers.inc` 从 `956` 行降到 `707` 行
  - 新增 `components/river_cloud/river_xiaozhi_ws_legacy_handlers.inc`，当前约 `250` 行
- 新的 legacy handler 模块集中承载：
  - `stt` / `llm` / `tts` 文本消息处理
  - `system` / `alert` 低频消息处理
  - `mcp` payload bridge 与 response send 处理
- 这一步保持 receive truth source 收口原则：
  - realtime session/update、preview、audio.out.meta、response、session.end、error handler 继续留在 message handler include
  - text dispatcher 仍在同一 translation unit 内按 type 分发，不新增 public header
  - legacy handler 继续直接消费原有 private/static helper，避免把 WS receive truth/helper 外扩
- Verification:
  - `git diff --check` passed
  - `python3 /root/ameba-rtos/ameba.py build -p` completed with `Build done` against `/root/ameba-rtos`
  - static grep confirmed legacy handlers live in `river_xiaozhi_ws_legacy_handlers.inc` and `river_xiaozhi_ws_message_handlers.inc` includes that module before the text dispatcher

## Step 5.519
- 继续治理 XiaoZhi playback downlink worker，把 abort / terminal policy 从高频 worker shell 中拆出：
  - `river_cloud_xiaozhi_playback_downlink_worker.inc` 从 `358` 行降到 `178` 行
  - 新增 `components/river_cloud/river_cloud_xiaozhi_playback_abort_policy.inc`，当前约 `181` 行
- 新的 abort policy 模块集中承载：
  - terminal ACK include chain 与 terminal ACK state setter
  - abort cause / clear reason / stream reason mapping
  - pending stop、backend refresh policy、terminal playback policy
  - typed abort-for-cause 入口
- 这一步保持 truth source 收口原则：
  - worker shell 只保留 current segment start hook、downlink cycle include、decoder/audio event、stereo expand、worker task/start
  - terminal ACK 与 abort policy 仍在原 playback runtime translation unit 内消费 private/static truth helper
  - 不新增 public header，也不扩大 playback runtime truth 写面
- Verification:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed with `check_codex_harness: all checks passed`
  - `python3 /root/ameba-rtos/ameba.py build -p` completed with `Build done` against `/root/ameba-rtos`
  - static grep confirmed pending stop / backend refresh / terminal playback / abort-for-cause live in `river_cloud_xiaozhi_playback_abort_policy.inc`, while worker shell only includes the policy module

## Step 5.518
- 继续治理 XiaoZhi WS public API façade，把剩余 public API wrapper 按职责拆成更窄 include：
  - `river_xiaozhi_ws_public_api.inc` 从 `618` 行降到 `4` 行，只保留 include 顺序
  - 新增 `components/river_cloud/river_xiaozhi_ws_config_api.inc`，当前约 `308` 行
  - 新增 `components/river_cloud/river_xiaozhi_ws_session_api.inc`，当前约 `306` 行
- 新的 config API 模块集中承载：
  - init / get_config / set_config / event handler
  - configured、session id、last text/state/error/activation 等只读 getters
  - session.update cache clear 与 discovery/ack negotiation getters
- 新的 session API 模块集中承载：
  - OTA bootstrap
  - open_session / close_session / session_open / poll
  - bootstrap cache refresh/hit 与 connect handshake path
- 这一步保持 truth source 收口原则：
  - public API façade 只串联 config/session/send/status 四个私有 include
  - 不新增 public header，也不把 WS transport truth/helper 外扩到跨 translation unit
  - connect/open 的高频与低频诊断仍在同一原编译单元内消费 private/static helper
- Verification:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed with `check_codex_harness: all checks passed`
  - `python3 /root/ameba-rtos/ameba.py build -p` completed with `Build done` against `/root/ameba-rtos`
  - static grep confirmed init/config/getters live in `river_xiaozhi_ws_config_api.inc`, bootstrap/open/poll live in `river_xiaozhi_ws_session_api.inc`, and the public API façade includes config/session/send/status in order

## Step 5.517
- 继续治理 XiaoZhi WS public API include，把发送类 public wrappers 从 session/config/open/poll API 中拆出：
  - `river_xiaozhi_ws_public_api.inc` 从 `719` 行降到 `618` 行
  - 新增 `components/river_cloud/river_xiaozhi_ws_send_api.inc`，当前约 `101` 行
- 新的 send API 模块集中承载：
  - listen start/stop/detect wrappers
  - abort 与 `audio.out.*` ACK send wrappers
  - binary audio send 与 MCP payload send wrapper
- 这一步进一步压窄 public API ownership：
  - session/config/bootstrap/open/close/poll 保留在 `river_xiaozhi_ws_public_api.inc`
  - send wrapper 与 status dump 各自独立 include，仍在原 WS translation unit 内消费 private/static helper
  - 不新增 public header，也不把 transport truth 或 send helper 外扩到跨 translation unit
- Verification:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed with `check_codex_harness: all checks passed`
  - `python3 /root/ameba-rtos/ameba.py build -p` completed with `Build done` against `/root/ameba-rtos`
  - static grep confirmed send wrappers live in `river_xiaozhi_ws_send_api.inc` while `river_xiaozhi_ws_public_api.inc` includes send/status modules at the bottom

## Step 5.516
- 继续治理 `river_xiaozhi_ws_public_api.inc`，把低频 status dump 从 public API wrapper 集合中拆出：
  - `river_xiaozhi_ws_public_api.inc` 从 `952` 行降到 `719` 行
  - 新增 `components/river_cloud/river_xiaozhi_ws_status_dump.inc`，当前约 `234` 行
- 新的 status dump 模块集中承载：
  - send queue snapshot / audio soft-limit / bootstrap cache 剩余时间诊断
  - session lane、timing age、timing chain、preview state、playback meta、discovery negotiation 输出
  - activation challenge 状态输出
- 这一步保持 truth source 收口原则：
  - status dump 仍在原 WS translation unit 内 include，继续只读取 `g_river_xiaozhi` 及 private/static helper
  - 不新增 public header，也不把诊断读取路径暴露为跨 translation unit API
  - session/open/send wrappers 主路径不再携带大块诊断格式化代码，降低 public API include 的维护噪声
- Verification:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed with `check_codex_harness: all checks passed`
  - `python3 /root/ameba-rtos/ameba.py build -p` completed with `Build done` against `/root/ameba-rtos`
  - static grep confirmed `river_xiaozhi_dump_status` and timing/preview/discovery diagnostics live in `river_xiaozhi_ws_status_dump.inc` while `river_xiaozhi_ws_public_api.inc` includes it at the bottom

## Step 5.515
- 继续治理 `river_cloud_xiaozhi_playback_downlink_worker.inc`，把 downlink worker 的恢复策略与 cycle 执行从 worker shell 中拆出：
  - `river_cloud_xiaozhi_playback_downlink_worker.inc` 从 `1277` 行降到 `358` 行
  - 新增 `components/river_cloud/river_cloud_xiaozhi_playback_rebuffer_recovery.inc`，当前约 `538` 行
  - 新增 `components/river_cloud/river_cloud_xiaozhi_playback_downlink_cycle.inc`，当前约 `383` 行
- 新的 rebuffer recovery 模块集中承载：
  - start/resume/low-water/segment-gap 门限 helper
  - upstream starvation watch 与 managed rebuffer 请求
  - write_failed 是否按 starvation rebuffer 处理的判断
  - rebuffer recovery path/request/fallback 与 paused backend resume
- 新的 downlink cycle 模块集中承载：
  - playback stream start / compact retry
  - prepare playback wait-kind/status
  - acquire frame、write outcome、cycle result 与 wait-plan 投影
- 保留原 worker shell 作为高频 loop 的窄入口：
  - terminal ACK include 与 abort policy 仍在 shell 内串联
  - decoder/audio event、stereo expand、worker task/start 仍留在 shell 内
  - 没有新增 public header，也没有把 playback/runtime truth 写面外扩
- Verification:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed with `check_codex_harness: all checks passed`
  - first sandboxed build still hit SDK generated-file write restriction on `/root/ameba-rtos/.../build_info.h`; approved rerun succeeded
  - `python3 /root/ameba-rtos/ameba.py build -p` completed with `Build done`

## Step 5.514
- 继续压缩 `river_cloud_xiaozhi_playback_runtime.c`，把已稳定的 playback runtime view / typed result schema 从主文件拆出：
  - `river_cloud_xiaozhi_playback_runtime.c` 从 `2550` 行降到 `2063` 行
  - 新增 `components/river_cloud/river_cloud_xiaozhi_playback_runtime_views.inc`，当前约 `488` 行
- 新模块集中承载 playback runtime 的只读视图与局部 typed result 定义：
  - current segment accessor 与 supply kind 计算
  - downlink wait / cycle outcome name helper
  - playback truth/observe/diag/gap/gate/recovery view structs
  - write_failed followup、downlink prepare/acquire/write/cycle/wait typed result structs
  - `capture_playback_truth_view` / observe / gate / gap hold helper
- 这一步保持 truth source 收口原则：
  - 不新增 public header
  - 不把 runtime truth 写入口暴露到跨 translation unit
  - view include 只在原 runtime 编译单元内消费 private/static helper，避免高频 downlink worker 与诊断视图继续堆在主文件中
- Verification:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed with `check_codex_harness: all checks passed`
  - first sandboxed build again hit SDK generated-file write restriction on `/root/ameba-rtos/.../build_info.h`; approved rerun succeeded
  - `python3 /root/ameba-rtos/ameba.py build -p` completed with `Build done`

## Step 5.513
- 继续按“3k 行仍过大”的治理目标压缩 XiaoZhi WS 主文件；本轮把剩余 realtime/message receive dispatch 从 transport 主体中剥离：
  - `river_xiaozhi_ws.c` 从 `2965` 行降到 `2010` 行
  - 新增 `components/river_cloud/river_xiaozhi_ws_message_handlers.inc`，当前约 `956` 行
- 新模块集中承载 WebSocket 下行消息的解析与分发边界：
  - realtime semantic handlers：`session.update`、`audio.out.meta`、`response.start`、`response.chunk`、`session.end`、`error`
  - legacy text message handlers：`stt`、`llm`、`tts`、`system`、`alert`、`mcp`
  - binary/text frame 判定与 `CONTINUATION` fragmented payload fallback
- 保留 Step 5.510 拆出的 `river_xiaozhi_ws_preview_handlers.inc` 在 message handler 模块内 include：
  - preview truth、preview throttle 与 input preview event emit 仍在同一局部 receive-path ownership 内
  - transport 主文件不再直接承载高频 preview/realtime/message dispatch 细节
- 这一步仍不扩大 public header，也不把 private/static truth helper 暴露为跨 translation unit 接口；目标是先让主文件稳定到 transport/send/session ownership，再评估哪些 include 边界适合升级为 `.c` + narrow private header。
- Verification:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed with `check_codex_harness: all checks passed`
  - first sandboxed build exposed SDK-side generated-file write restriction on `/root/ameba-rtos/.../build_info.h`; rerun with approval succeeded
  - `python3 /root/ameba-rtos/ameba.py build -p` completed with `Build done`

## Step 5.512
- 按“3k 行仍过大”的治理目标继续白盒拆分两个 XiaoZhi runtime 主文件，
  这次不再只把尾部 public API 拆走，而是把剩余大块低频/专用职责从主循环
  中移出：
  - `river_xiaozhi_ws.c` 从 `3713` 行降到 `2965` 行
  - `river_cloud_xiaozhi_playback_runtime.c` 从 `3826` 行降到 `2550` 行
- 拆出 `components/river_cloud/river_xiaozhi_ws_bootstrap_discovery.inc`：
  - 集中承载 HTTP URL 解析、OTA bootstrap POST、discovery GET、bootstrap /
    discovery JSON parse
  - `river_xiaozhi_ws.c` 主体现在只保留 realtime transport、send queue、
    negotiation/cache 状态与 dispatch 关键路径，低频联网探测不再混在 receive
    path 附近
- 拆出 `components/river_cloud/river_cloud_xiaozhi_playback_downlink_worker.inc`：
  - 集中承载 downlink start gate、segment gap/rebuffer、downlink cycle plan、
    decoder/audio event、downlink worker task 与 terminal ACK include
  - `river_cloud_xiaozhi_playback_runtime.c` 主体保留 backend/source/truth view、
    lineage、terminal wait、policy/status include，媒体 worker 细节整体后移
- 这一步的治理边界：
  - 不扩大 public header，不把 private truth 写面暴露给跨 translation unit
  - 优先用 include-split 建立稳定 ownership，再决定下一轮是否把 bootstrap /
    downlink worker 升级为独立 `.c` + narrow internal header
- Verification:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed with
    `check_codex_harness: all checks passed`
  - `wc -l` confirmed both main runtime files are now below `3000` lines
  - `python3 /root/ameba-rtos/ameba.py build -p` completed with `Build done`

## Step 5.511
- 继续对两个 XiaoZhi 超大 runtime 文件做第二轮结构拆分，让主文件回到更清晰的
  high-frequency/core orchestration 视图：
  - `river_xiaozhi_ws.c` 从 `4664` 行降到 `3713` 行
  - `river_cloud_xiaozhi_playback_runtime.c` 从 `4364` 行降到 `3826` 行
- 拆出 `components/river_cloud/river_xiaozhi_ws_public_api.inc`：
  - 集中承载 XiaoZhi transport 的 public API、session open/close、send wrappers
    与 status dump
  - `river_xiaozhi_ws.c` 主体现在更聚焦 transport lock、send queue、protocol
    parse/dispatch 与 realtime receive handlers，减少高频 receive/send 路径旁边的
    低频配置/诊断噪声
- 拆出 `components/river_cloud/river_cloud_xiaozhi_playback_public_policy.inc`：
  - 集中承载 playback status/snapshot export、capture/session policy reset、
    backend pause/hold/rebuffer entry
  - 主 playback runtime 更聚焦 downlink ring、start gate、segment/rebuffer worker
    与 terminal ACK include，减少 truth export / policy shell 与媒体 worker 路径交织
- 本轮仍采用 `.inc` include-split，保持 static/private helper 访问边界不外扩；
  等这些 truth/export 边界稳定后，再评估哪些块适合提升为独立 `.c`。
- Verification:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed with
    `check_codex_harness: all checks passed`
  - `wc -l` confirmed the reduced main-file sizes and new include modules
  - `python3 /root/ameba-rtos/ameba.py build -p` completed with `Build done`

## Step 5.510
- 直接对两个超大 XiaoZhi runtime 文件做一轮低风险结构拆分，优先保持
  truth ownership 与高频路径局部性：
  - `river_xiaozhi_ws.c` 从 `4971` 行降到 `4664` 行
  - `river_cloud_xiaozhi_playback_runtime.c` 从 `5215` 行降到 `4364` 行
- 拆出 `components/river_cloud/river_xiaozhi_ws_preview_handlers.inc`：
  - 集中承载 `input.speech.start` / `input.preview` / `input.endpoint`
    receive-path handlers
  - high-frequency preview throttle、preview truth 更新与事件 emit 保持在同一
    局部模块里，减少主 transport 文件里的高频分支噪声
- 拆出 `components/river_cloud/river_cloud_xiaozhi_playback_lineage.inc`：
  - 集中承载 playback lineage canonical truth 的 stage name、clear/touch、
    segment touch 与 terminal flag 派生 helper
  - response/meta/started/mark/cleared/completed 仍由同一 lineage truth helper
    更新，避免后续改动重新把阶段事实散回 runtime 主文件
- 拆出 `components/river_cloud/river_cloud_xiaozhi_playback_terminal_ack.inc`：
  - 集中承载 fully-heard context、completed readiness、started/mark/cleared/
    completed ACK 进度与 terminal clear/finalize 路径
  - high-frequency ACK progress loop 与 lineage context 更新放在同一局部模块，
    让主 playback runtime 更聚焦 backend/rebuffer/downlink orchestration
- 本轮采用 `.inc` include-split 而不是跨 translation unit 拆分，是为了保持现有
  static helper / private context 访问边界，避免在这一步额外暴露全局接口或扩大
  真相源写面。
- Verification:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed with
    `check_codex_harness: all checks passed`
  - `rg -n "river_xiaozhi_ws_preview_handlers.inc|river_cloud_xiaozhi_playback_lineage.inc|river_cloud_xiaozhi_playback_terminal_ack.inc|playback_lineage_truth|preview_logs=" ...`
    confirmed the new split boundaries and retained truth/high-frequency hooks
  - `python3 /root/ameba-rtos/ameba.py build -p` completed with `Build done`

## Step 5.509
- 白盒审视当前实时交互链路后，选择一个确定性高频热点先优化：
  - `river_xiaozhi_ws.c` 的 `input.preview` 处理位于 WebSocket receive/dispatch
    路径
  - preview partial 可能高频到达，原实现每条都同步格式化完整
    `text/stable_prefix/timing` 并 `RIVER_LOGI`
  - 这会让日志 IO 与字符串格式化抢占同一条实时消息处理路径，影响
    preview/accept/response.start 交互时延
- 新增 preview update 日志节流：
  - `RIVER_XIAOZHI_PREVIEW_LOG_INTERVAL_MS = 250`
  - 首条 preview、`stable_prefix` 变化、final preview 仍立即记录
  - 其余 partial preview 只更新状态并正常 emit event，不再每条同步打印
- `input.preview` 仍保持完整状态更新与事件分发：
  - `last_preview_text`
  - `last_preview_stable_prefix`
  - `last_preview_update_at_ms`
  - `RIVER_XIAOZHI_EVENT_INPUT_PREVIEW`
- 新增 preview 诊断计数：
  - `preview_update_events`
  - `preview_logs_emitted`
  - `preview_logs_suppressed`
  - `river xiaozhi status` 的 `preview_state` 行现在显示
    `preview_updates` 与 `preview_logs=emitted/suppressed`
- 这一步把高频 partial 日志从默认同步路径移出，保留关键边界日志与状态
  可观测性，降低 receive thread 上的日志放大和交互时延抖动风险
- Verification:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed with
    `check_codex_harness: all checks passed`
  - `rg -n "RIVER_XIAOZHI_PREVIEW_LOG_INTERVAL_MS|preview_logs_suppressed|preview_logs=|stable_changed" ...`
    confirmed preview log throttling and status counters
  - `python3 /root/ameba-rtos/ameba.py build -p` completed with `Build done`

## Step 5.508
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
- Verification:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed with
    `check_codex_harness: all checks passed`
  - `rg -n "fully_heard_context|last_segment_context|sync_playback_terminal_report_flags|playback_lineage_truth|lineage_started|lineage_completed" ...`
    confirmed lineage truth owns terminal context/report derivation
  - `python3 /root/ameba-rtos/ameba.py build -p` completed with `Build done`

## Step 5.507
- 本地白盒重构继续收口两个确定性关键问题：
  - `write_failed` recovery followup 仍通过 `inline_success` / `managed_rebuffer`
    布尔组合表达，managed rebuffer 执行结果没有回填到 downlink write result
  - playback response/meta/started/mark/cleared/completed 分散在 transport、
    meta truth、segment queue 与 terminal truth 中，状态串联需要跨结构推断
- 新增 typed `river_cloud_xiaozhi_write_failed_followup_kind_t`，把恢复后续动作
  收敛为 `inline_replay_consumed` / `managed_rebuffer` / `none`，并让
  `river_cloud_xiaozhi_downlink_frame_write_result_t` 持有
  `recovery_followup_kind`、`recovery_status` 与 `recovery_path`
- 新增 `RIVER_XIAOZHI_EVENT_RESPONSE_START`，让 `response.start` 从 transport
  事件进入 cloud runtime，而不是只存在于 WebSocket 局部状态
- 新增 `river_cloud_xiaozhi_playback_lineage_truth_t` 与
  `river_cloud_xiaozhi_playback_lineage_stage_t`，把 response.start、
  audio.out.meta、started ack、mark ack、cleared ack、completed ack 以及 local
  terminal fallback 串到单一 lineage truth
- `river xiaozhi status` 新增 `xiaozhi playback_lineage ...` 行，可直接查看
  lineage stage 与 response/meta/started/mark/cleared/completed 上下文
- Verification:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed with
    `check_codex_harness: all checks passed`
  - `rg -n "WRITE_FAILED_FOLLOWUP|recovery_followup_kind|playback_lineage_truth|playback_lineage|RIVER_XIAOZHI_EVENT_RESPONSE_START" ...`
    confirmed typed recovery followup, response-start event routing, and lineage truth
  - `python3 /root/ameba-rtos/ameba.py build -p` completed with `Build done`

## Step 5.506
- 白盒审视发现一个确定性关键问题：Step 5.505 后 downlink cycle 已有
  `wait/outcome/cycle_status`，但 `prepare_downlink_playback()` 仍用裸 `bool`
  把 rebuffer wait、start threshold、backend recovering 和 playback start failure
  都折叠成 generic `playback_not_ready`，其中 start failure 还会丢失原始 status：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- 新增 `river_cloud_xiaozhi_downlink_playback_prepare_result_t`，让 playback prepare
  阶段返回：
  - concrete `wait_kind`
  - source `river_status_t status`
  - `ready`
- 扩展 downlink wait reason：
  - `rebuffer_wait`
  - `stop_pending`
  - `paused_resume_wait`
  - `backend_recovering`
  - `start_threshold`
  - `playback_start_failed`
- `river_cloud_xiaozhi_downlink_cycle_plan_t` 现在持有 prepare status，
  `river_cloud_xiaozhi_process_downlink_task_cycle()` 会把 start failure status
  投影到 `cycle_status`
- 这一步把 playback prepare blocked 诊断从：
  - generic `playback_not_ready` with `cycle_status=0`
  推进到：
  - concrete prepare wait reason plus start-failure status truth
- Verification:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed with
    `check_codex_harness: all checks passed`
  - `rg -n "downlink_playback_prepare_result_t|WAIT_REBUFFER_WAIT|WAIT_START_THRESHOLD|WAIT_PLAYBACK_START_FAILED|prepare_result.status|cycle_plan.status" ...`
    confirmed typed prepare result, concrete wait reasons, and status propagation
  - `python3 /root/ameba-rtos/ameba.py build -p` completed with `Build done`

## Step 5.505
- `river_cloud` 继续把 XiaoZhi downlink worker 的 frame-consumed / write-failed /
  aborted 分支收口成 cycle outcome truth，避免 status 侧再从多个布尔字段推断：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- 新增 `river_cloud_xiaozhi_downlink_cycle_outcome_t`，覆盖：
  - `inactive`
  - `not_ready`
  - `acquire_miss`
  - `write_ok`
  - `write_recovered`
  - `write_failed`
  - `aborted`
  - `step_policy`
- `river_cloud_xiaozhi_downlink_task_cycle_result_t` 现在显式记录 `outcome`，
  写入分支通过 helper 把 `frame_consumed` / `write_failed` / `aborted` 归一为
  cycle outcome
- downlink runtime truth 新增 `last_cycle_outcome`，`river xiaozhi status`
  的 downlink 行现在输出
  `wait=<kind>/<delay>ms outcome=<name> cycle_status=<status>`
- 这一步把 Step 5.504 后的 status truth 从：
  - source status is visible but branch outcome still lives in sub-result booleans
  推进到：
  - cycle outcome is owned by the cycle result and published as runtime truth
- Verification:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `rg -n "downlink_cycle_outcome_t|last_cycle_outcome|downlink_cycle_outcome|outcome=%s|WRITE_RECOVERED|CYCLE_ABORTED" ...` confirmed outcome truth and status projection
  - `python3 /root/ameba-rtos/ameba.py build -p` completed successfully against `/root/ameba-rtos` with `Build done`

## Step 5.504
- `river_cloud` 继续把 XiaoZhi downlink acquire/write 子结果中的 status/error
  细节纳入 typed result，让 acquire miss 与 write failed 不再只携带布尔结果：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- `river_cloud_xiaozhi_downlink_frame_acquire_result_t` 现在记录
  `river_status_t status`，ring read 失败时保留原始 `river_audio_frame_ring_read(...)`
  返回值
- `river_cloud_xiaozhi_downlink_frame_write_result_t` 现在记录
  `river_status_t status`，frame oversize 和 playback write failure 会保留明确错误码
- `river_cloud_xiaozhi_downlink_task_cycle_result_t` / wait plan 现在把子结果
  status 投影到 downlink runtime truth 的 `last_cycle_status`
- `river xiaozhi status` 的 downlink 行现在输出
  `wait=<kind>/<delay>ms cycle_status=<status>`，用于区分同一个 wait reason
  下的具体错误码
- 这一步把 Step 5.503 后的 acquire/write 诊断从：
  - typed sub-result only says acquired/write_failed
  推进到：
  - typed sub-result owns source status and publishes it as cycle diagnostic truth
- Verification:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `rg -n "last_cycle_status|downlink_cycle_status|cycle_status=%d|acquire_result.status|write_result.status|wait_plan.status" ...` confirmed typed status propagation and status dump
  - `python3 /root/ameba-rtos/ameba.py build -p` completed successfully against `/root/ameba-rtos` with `Build done`

## Step 5.503
- `river_cloud` 继续细分 XiaoZhi downlink ready-plan 的 not-ready 原因，
  让 wait reason 不再只有粗粒度 `not_ready`：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- 扩展 `river_cloud_xiaozhi_downlink_wait_kind_t`：
  - `starved`
  - `segment_gap`
  - `empty`
  - `playback_not_ready`
- `river_cloud_xiaozhi_downlink_cycle_plan_t` 现在持有 `wait_kind`，
  `river_cloud_xiaozhi_prepare_downlink_cycle_plan()` 在各个 not-ready 分支中显式
  标注 blocked reason
- `river_cloud_xiaozhi_build_downlink_task_wait_plan()` 现在直接消费 cycle plan 的
  ready-block reason，status 中的 `wait=<kind>/<delay>ms` 可以区分：
  - 上游断供触发 rebuffer
  - segment gap hold
  - 队列为空
  - playback start/prep gate 未满足
- 这一步把 Step 5.502 的 wait reason 从：
  - generic `not_ready`
  推进到：
  - ready-plan owns concrete blocked reason before wait-plan projection
- Verification:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `rg -n "WAIT_STARVED|WAIT_SEGMENT_GAP|WAIT_EMPTY|WAIT_PLAYBACK_NOT_READY|cycle_plan\.wait_kind|wait=<kind>" ...` confirmed the enum, ready-plan ownership, and wait-plan projection
  - `python3 /root/ameba-rtos/ameba.py build -p` completed successfully against `/root/ameba-rtos` with `Build done`

## Step 5.502
- `river_cloud` 继续让 XiaoZhi downlink cycle sub-results 服务诊断，把 worker
  wait plan 的原因锁存到 downlink runtime truth 并在 status 中输出：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- 新增：
  - `river_cloud_xiaozhi_downlink_wait_kind_t`
  - `river_cloud_xiaozhi_downlink_wait_kind_name()`
  - `last_wait_kind` / `last_wait_delay_ms` downlink runtime truth
- `river_cloud_xiaozhi_build_downlink_task_wait_plan()` 现在根据 cycle result 的
  子结果区分：
  - inactive
  - not_ready
  - acquire_miss
  - write_failed
  - step_policy
- `river xiaozhi status` 的 downlink 行现在输出 `wait=<kind>/<delay>ms`，
  让 wait/poll 的来源不再只能从裸 step result 反推
- 这一步把 Step 5.501 的 wait plan 从：
  - 只表达 sleep/no-sleep 与 delay duration
  推进到：
  - typed wait reason consumes ready/acquire/write sub-results and becomes runtime-visible truth
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `rg -n "downlink_wait_kind_t|last_wait_kind|last_wait_delay_ms|wait=%s/%lums|DOWNLINK_WAIT_WRITE_FAILED" components/river_cloud/river_cloud_internal.h components/river_cloud/river_cloud_xiaozhi_playback_runtime.c` confirmed the wait-reason truth/status path
  - `python3 /root/ameba-rtos/ameba.py build -p` completed successfully against `/root/ameba-rtos`

## Step 5.501
- `river_cloud` 继续类型化 XiaoZhi downlink worker sleep/idle 收尾策略，把
  cycle result 到 RTOS delay 的投影收口成 typed wait plan：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增：
  - `river_cloud_xiaozhi_downlink_task_wait_plan_t`
  - `river_cloud_xiaozhi_build_downlink_task_wait_plan()`
- `river_cloud_xiaozhi_finish_downlink_task_cycle()` 现在只消费 wait plan，
  不再内联把 cycle step result 分支直接映射到 `rtos_time_delay_ms(...)`
- 这一步把 worker 收尾从：
  - finish helper 内直接判断 `SLEEP_IDLE` / `SLEEP_POLL` 并执行 delay
  推进到：
  - typed wait plan owns sleep/no-sleep and delay duration before the RTOS effect
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `rg -n "downlink_task_wait_plan_t|build_downlink_task_wait_plan|finish_downlink_task_cycle" components/river_cloud/river_cloud_xiaozhi_playback_runtime.c` confirmed the wait-plan boundary
  - `python3 /root/ameba-rtos/ameba.py build -p` completed successfully against `/root/ameba-rtos`

## Step 5.500
- `river_cloud` 继续类型化 XiaoZhi downlink worker cycle，把 ready / acquire / write
  三段结果组合成 cycle-level typed result：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增：
  - `river_cloud_xiaozhi_downlink_task_cycle_result_t`
- `river_cloud_xiaozhi_process_downlink_task_cycle()` 现在返回完整 cycle result，显式持有：
  - `cycle_plan`
  - `acquire_result`
  - `write_result`
  - cycle-level `step_result`
- `river_cloud_xiaozhi_downlink_task()` 现在先接收 cycle result，再交给
  `river_cloud_xiaozhi_finish_downlink_task_cycle()` 处理 worker sleep policy
- 这一步把 downlink worker loop 从：
  - ready / acquire / write 分支在 cycle 函数中直接投影成裸 step result
  推进到：
  - cycle-level typed result owns sub-step observations and final worker policy
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `rg -n "downlink_task_cycle_result_t|process_downlink_task_cycle|finish_downlink_task_cycle" components/river_cloud/river_cloud_xiaozhi_playback_runtime.c` confirmed the typed cycle boundary
  - `python3 /root/ameba-rtos/ameba.py build -p` completed successfully against `/root/ameba-rtos`

## Step 5.499
- `river_cloud` 继续类型化 XiaoZhi downlink current-frame write step，把
  `river_cloud_xiaozhi_write_current_downlink_frame_step()` 的裸 step-result 返回
  升级为 typed frame-write result：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增：
  - `river_cloud_xiaozhi_downlink_frame_write_result_t`
- `river_cloud_xiaozhi_write_current_downlink_frame_step()` 现在显式返回：
  - `step_result`
  - `frame_consumed`
  - `write_failed`
  - `aborted`
- `river_cloud_xiaozhi_process_downlink_task_cycle()` 现在先接收 typed write result，
  再投影 worker step result
- 这一步把 write step 从：
  - success / write-failed / abort 分支直接折叠成裸 worker step result
  推进到：
  - typed frame-write result owns branch outcome, frame consumption and worker step policy

## Step 5.498
- `river_cloud` 继续类型化 XiaoZhi downlink write-failed 恢复结果，把 handler 的
  `bool` 返回升级为 step-result view：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增：
  - `river_cloud_xiaozhi_write_failed_recovery_result_t`
- `river_cloud_xiaozhi_handle_playback_write_failed()` 现在显式返回：
  - `frame_consumed`
  - `step_result`
- `river_cloud_xiaozhi_write_current_downlink_frame_step()` 不再用 bool + 三元表达式
  推导 inline replay / managed rebuffer 后的下一步动作，而是直接消费 recovery result
- 这一步把 write-failed 分支从：
  - bool 表达“inline replay 是否成功并已消费帧”
  推进到：
  - typed recovery result owns frame consumption and worker step policy

## Step 5.497
- `river_cloud` 继续收口 XiaoZhi downlink write step 的成功路径，把成功写入后的
  frame consume / segment start / ACK progress / pending-stop check 封装为单一 helper：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增：
  - `river_cloud_xiaozhi_finish_successful_downlink_frame_write()`
- `river_cloud_xiaozhi_write_current_downlink_frame_step()` 现在只保留：
  - 捕获 write view
  - oversize abort
  - write effect
  - write-failed recovery 委托
  - successful-write finish 委托
- 这一步把 downlink write 成功路径从：
  - write step 内散落 consume/current segment/ACK/pending-stop 副作用
  推进到：
  - successful-write finish helper owns post-write effects

## Step 5.496
- `river_cloud` 继续拆分 XiaoZhi downlink worker cycle，把 current-frame acquire
  从裸 `bool` 结果升级为 typed result：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增：
  - `river_cloud_xiaozhi_downlink_frame_acquire_result_t`
- `river_cloud_xiaozhi_acquire_current_downlink_frame()` 现在显式返回：
  - `acquired`
  - `step_result`
- `river_cloud_xiaozhi_process_downlink_task_cycle()` 不再把 acquire 失败硬编码成
  `SLEEP_POLL`，而是消费 acquire result 内的下一步动作
- 这一步把 downlink worker 从：
  - ready plan typed，但 acquire 仍是裸 bool + 调用方补 sleep policy
  推进到：
  - ready/acquire/write 三段都具备 typed step-result 边界

## Step 5.495
- `river_cloud` 继续收缩 XiaoZhi playback `write_failed` managed rebuffer
  follow-up，把 handler 尾部的 start/log/execute 三段副作用封装成单一 executor：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增：
  - `river_cloud_xiaozhi_execute_write_failed_managed_rebuffer_followup()`
- `river_cloud_xiaozhi_handle_playback_write_failed()` 现在只保留：
  - 捕获 recovery view
  - 清 starvation watch
  - 记录首条 write-failed log
  - 尝试 inline recover
  - inline 成功则 consume current frame
  - 否则委托 managed rebuffer follow-up executor
- 这一步把 write-failed managed 恢复从：
  - handler 内直接串联 start request、log request、execute request
  推进到：
  - follow-up executor owns managed rebuffer effects

## Step 5.494
- `river_cloud` 继续收口 XiaoZhi playback inline recover replay 写入路径，让恢复重放
  复用 current-frame write view/effect：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `river_cloud_xiaozhi_try_write_failed_inline_recover()` 现在接收
  `river_cloud_xiaozhi_downlink_write_view_t`，不再单独传递 `mono_bytes` /
  `stereo_bytes`
- inline recover replay 的二次写入不再直接调用
  `river_playback_service_write(...)`，而是复用：
  - `river_cloud_xiaozhi_write_current_downlink_frame_audio()`
- 这一步把 playback write 从：
  - normal write 与 inline recover replay 各自维护一份 service-write 调用
  推进到：
  - current-frame playback write has a single effect helper

## Step 5.493
- `river_cloud` 继续拆分 XiaoZhi playback `write_failed` 恢复链，把故障恢复视图
  从 handler 的副作用编排中抽出：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增 typed write-failed recovery view：
  - `river_cloud_xiaozhi_write_failed_recovery_view_t`
  - `river_cloud_xiaozhi_capture_write_failed_recovery_view()`
  - `river_cloud_xiaozhi_log_write_failed_recovery_view()`
- `river_cloud_xiaozhi_handle_playback_write_failed()` 现在接收上一阶段的
  `river_cloud_xiaozhi_downlink_write_view_t`，并通过 recovery view 统一持有：
  - recovery plan
  - recover reason
  - recovery path
  - failure timestamp
  - mono/stereo frame size
- 这一步把 write-failed path 从：
  - handler 内直接推导 recover reason、拼首条 failure log、保存 recovery path
  推进到：
  - typed recovery view owns write-failed query/log projection before effects

## Step 5.492
- `river_cloud` 继续重建 XiaoZhi downlink/playback 写入边界，把 current-frame
  写入前的派生视图和实际 playback write 副作用拆开：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增 current-frame write view / effect helper：
  - `river_cloud_xiaozhi_downlink_write_view_t`
  - `river_cloud_xiaozhi_capture_current_downlink_write_view()`
  - `river_cloud_xiaozhi_write_current_downlink_frame_audio()`
- `river_cloud_xiaozhi_write_current_downlink_frame_step()` 不再内联计算
  `mono_bytes/stereo_bytes`、frame-oversize 判定、stereo expansion 和
  `river_playback_service_write(...)` 调用，改为先捕获 typed write view，再执行
  单一写入副作用 helper
- 这一步把 downlink current-frame write 从：
  - acquire/write/rebuffer 巨函数内混合 query、validation、format expansion 和
    playback-service write
  推进到：
  - typed write view plus isolated playback write effect

## Step 5.491
- `river_cloud` 继续收口 XiaoZhi downlink retry-frame 真相访问，把 `retry_valid` 的查询、保留和消费封装成 helper：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增 retry-frame owner helper：
  - `river_cloud_xiaozhi_downlink_retry_frame_pending()`
  - `river_cloud_xiaozhi_keep_current_downlink_frame_for_retry()`
  - `river_cloud_xiaozhi_consume_current_downlink_frame()`
- queued-frame 统计、playback work 判断、managed rebuffer keep-retry、inline recover 成功、正常写入成功和 current-frame acquire 现在统一走 helper
- 这一步把 downlink retry-frame 从：
  - multiple playback paths directly read/write `retry_valid`
  推进到：
  - owner helpers define retry-frame lifecycle transitions

## Step 5.490
- `river_cloud` 继续重建 XiaoZhi downlink/playback 边界，把 downlink ring 的底层操作收口到 owner helper 内：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增 downlink ring owner helper：
  - `river_cloud_xiaozhi_note_downlink_supply()`
  - `river_cloud_xiaozhi_reset_downlink_ring_runtime()`
  - `river_cloud_xiaozhi_ensure_downlink_ring()`
  - `river_cloud_xiaozhi_write_downlink_frame_latest()`
- downlink audio event path 不再内联 ring 初始化、overflow 丢旧帧、drop counter 递增和 last-supply 更新时间戳逻辑，改为统一调用 owner helper
- reset-downlink 和 pending-stop drop 路径复用统一 reset helper，避免多个路径分别维护 retry-valid、last-supply 和 starvation watch
- 这一步把 downlink/playback 从：
  - playback main flow directly mutates ring/storage/drop/supply internals
  推进到：
  - owner helpers encapsulate downlink ring lifecycle and latest-frame write policy

## Step 5.489
- `river_cloud` 继续收口 XiaoZhi playback tail / no-ref reopen 真相源，消除
  playback runtime 对 TTS stop deadline、no-ref reopen guard、rearm 和 silence counter
  的散落上下文字段读写：
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `tts_stop_deadline_ms` 现在归入已有 playback runtime truth：
  - `g_river_cloud.xiaozhi_playback_runtime_truth.tts_stop_deadline_ms`
- no-ref follow-up reopen gate 状态现在归入已有 playback gate truth：
  - `g_river_cloud.xiaozhi_playback_gate_truth.no_ref_reopen_guard_deadline_ms`
  - `g_river_cloud.xiaozhi_playback_gate_truth.no_ref_reopen_silence_frames`
  - `g_river_cloud.xiaozhi_playback_gate_truth.no_ref_reopen_rearm`
- `river_cloud_context_t` 不再暴露散落的：
  - `xiaozhi_tts_stop_deadline_ms`
  - `xiaozhi_no_ref_reopen_guard_deadline_ms`
  - `xiaozhi_no_ref_reopen_silence_frames`
  - `xiaozhi_no_ref_reopen_rearm`
- playback stop arm/cancel/poll、playback reset、no-ref reopen readiness 和
  runtime status dump 现在统一消费 playback runtime/gate truth
- 这一步把 XiaoZhi playback tail / no-ref reopen 从：
  - scattered playback tail and no-ref reopen fields
  收口成：
  - playback runtime truth + playback gate truth

## Step 5.488
- `river_cloud` 继续收口 XiaoZhi session/window/local-close 真相源，消除
  `round_runtime` / `playback_runtime` 对 listening、conversation window、
  listen-stop pending、local-close pending 和对应 deadline 的散读散写：
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_round_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_round_runtime.c)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增显式 session-window-owned truth：
  - `river_cloud_xiaozhi_session_window_truth_t`
- `river_cloud_context_t` 不再暴露散落的：
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
- 这一步把 XiaoZhi session/window/local-close 从：
  - scattered dialog/session window flags and deadlines
  收口成：
  - explicit session window truth

## Step 5.487
- `river_cloud` 继续收口 XiaoZhi preview transcript / endpoint candidate 真相源，
  消除 `session` / `playback_runtime` 对 preview id、text、stable-prefix、source、
  endpoint reason、audio offset、speech-started、is-final、endpoint-candidate 这批裸字段的散读散写：
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增显式 preview-transcript-owned truth：
  - `river_cloud_xiaozhi_preview_transcript_truth_t`
- `river_cloud_context_t` 不再暴露散落的：
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
- 这一步把 XiaoZhi preview transcript 从：
  - scattered preview transcript / endpoint candidate bag
  收口成：
  - explicit preview transcript truth

## Step 5.486
- `river_cloud` 继续收口 XiaoZhi pending transcript 真相源，消除
  `session` / `round_runtime` 对 pending text valid / finalized / text 这批裸字段的散读散写：
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_xiaozhi_round_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_round_runtime.c)
- 新增显式 pending-transcript-owned truth：
  - `river_cloud_xiaozhi_pending_transcript_truth_t`
- `river_cloud_context_t` 不再暴露散落的：
  - `xiaozhi_pending_text_valid`
  - `xiaozhi_pending_text_finalized`
  - `xiaozhi_pending_text`
- STT observation、pending clear、finalize readiness、accepted-turn final emit、
  post-stop result policy、local-close defer 和 runtime dump 现在统一消费：
  - `g_river_cloud.xiaozhi_pending_transcript_truth`
- 这一步把 XiaoZhi pending transcript 从：
  - scattered pending text validity/finalization/text bag
  收口成：
  - explicit pending transcript truth

## Step 5.485
- `river_cloud` 继续收口 XiaoZhi endpoint soft-close 真相源，消除
  `playback_runtime` 对 endpoint soft-close pending / deadline / reason 这批裸字段的散读散写：
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增显式 endpoint-soft-close-owned truth：
  - `river_cloud_xiaozhi_endpoint_soft_close_truth_t`
- `river_cloud_context_t` 不再暴露散落的：
  - `xiaozhi_endpoint_soft_close_pending`
  - `xiaozhi_endpoint_soft_close_deadline_ms`
  - `xiaozhi_endpoint_soft_close_reason`
- endpoint soft-close 的 pending 查询、剩余时间、reason、clear、arm、timeout poll
  现在统一消费：
  - `g_river_cloud.xiaozhi_endpoint_soft_close_truth`
- 这一步把 XiaoZhi endpoint soft-close 从：
  - scattered endpoint defer pending/deadline/reason bag
  收口成：
  - explicit endpoint soft-close truth

## Step 5.484
- `river_cloud` 继续收口 XiaoZhi ASR round stats 真相源，消除
  `session` / `round_runtime` 对 round id、active、首包时间、发包数、
  partial/final 观测、uplink busy/drop 基线和 close reason 这批裸字段的散读散写：
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_xiaozhi_round_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_round_runtime.c)
- 新增显式 ASR-round-owned truth：
  - `river_cloud_xiaozhi_asr_round_truth_t`
- `river_cloud_context_t` 不再暴露散落的：
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
- ASR result emitted、round begin / packet-sent / finish、local close defer、
  follow-up reopen 和 IO status 诊断现在统一消费：
  - `g_river_cloud.xiaozhi_asr_round_truth`
- 这一步把 XiaoZhi ASR round stats 从：
  - scattered round timing/counter/reason bag
  收口成：
  - explicit ASR round truth

## Step 5.483
- `river_cloud` 继续收口 XiaoZhi control queue 真相源，消除
  `session` 对 control queue read/write/count/high-watermark 这批裸字段的散读散写：
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- 新增显式 control-queue-owned truth：
  - `river_cloud_xiaozhi_control_queue_truth_t`
- `river_cloud_context_t` 不再暴露散落的：
  - `xiaozhi_control_read_index`
  - `xiaozhi_control_write_index`
  - `xiaozhi_control_count`
  - `xiaozhi_control_high_watermark`
- control queue 入队、出队和诊断日志现在统一消费：
  - `g_river_cloud.xiaozhi_control_queue_truth`
- 这一步把 XiaoZhi control queue 从：
  - scattered read/write/count/high-watermark bag
  收口成：
  - explicit control queue truth

## Step 5.482
- `river_cloud` 继续收口 XiaoZhi io/uplink runtime 真相源，消除
  `adapter`、`session`、`round_runtime`、`playback_runtime` 对 uplink
  背压 / retry / drop / accumulator / io-owner 这批裸字段的散读散写：
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_xiaozhi_round_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_round_runtime.c)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增显式 uplink-owned runtime truth：
  - `river_cloud_xiaozhi_uplink_runtime_truth_t`
- `river_cloud_context_t` 不再暴露散落的：
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
- 这一步把 XiaoZhi uplink runtime 从：
  - scattered io/uplink backpressure bag
  收口成：
  - explicit uplink runtime truth

## Step 5.481
- `river_cloud` 继续收口 XiaoZhi transport/server audio format 真相源，消除
  `adapter`、`session`、`playback_runtime` 对 server sample-rate /
  frame-duration fallback 这批协商字段的散读散写：
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增显式 transport-owned format truth：
  - `river_cloud_xiaozhi_server_audio_format_truth_t`
- `river_cloud_context_t` 不再暴露散落的：
  - `xiaozhi_server_sample_rate`
  - `xiaozhi_server_frame_duration_ms`
- `river_cloud_xiaozhi_playback_runtime.c` 里原先直接依赖旧 server-format
  裸字段的关键路径，现在统一改走 typed format truth：
  - decoder open 成功后的 format note
  - audio event sample-rate / frame-duration fallback
- `adapter` / `session` 里的默认初始化和 `server hello` 观测，也统一改写到
  typed transport format truth
- 这一步把 XiaoZhi transport/server audio format 从：
  - scattered server-format fallback bag
  收口成：
  - explicit server audio format truth

## Step 5.480
- `river_cloud` 继续收口 XiaoZhi downlink worker / stream format 真相源，消除
  `playback_runtime` 对 worker-started、sample-rate、frame-duration
  这批粗粒度执行前提裸字段的混用：
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增显式 downlink-owned stream truth：
  - `river_cloud_xiaozhi_downlink_stream_truth_t`
- `river_cloud_context_t` 不再暴露散落的：
  - `xiaozhi_downlink_started`
  - `xiaozhi_downlink_sample_rate`
  - `xiaozhi_downlink_frame_duration_ms`
- `river_cloud_xiaozhi_playback_runtime.c` 里原先直接读写旧 downlink
  lifecycle / format 裸字段的关键路径，现在统一改走 typed stream truth：
  - frame-duration fallback
  - diag view 的 sample-rate / frame-duration / worker-started 导出
  - backend start-playback 参数拼装
  - audio event format note
  - downlink worker start gate / started 标记
- 这一步把 XiaoZhi downlink stream precondition 从：
  - scattered worker-started/format bag
  收口成：
  - explicit downlink stream truth

## Step 5.479
- `river_cloud` 继续收口 XiaoZhi playback segment queue 真相源，消除
  `playback_runtime` 对 segment 环形队列头指针、数量和槽位数组这批裸字段的混用：
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增显式 playback-owned queue truth：
  - `river_cloud_xiaozhi_playback_segment_queue_truth_t`
- `river_cloud_context_t` 不再暴露散落的：
  - `xiaozhi_playback_segment_head`
  - `xiaozhi_playback_segment_count`
  - `xiaozhi_playback_segments[...]`
- `river_cloud_xiaozhi_playback_runtime.c` 里原先直接读写旧 segment queue
  裸字段的关键路径，现在统一改走 typed queue truth：
  - playback supply source capture 的 queued segment 观测
  - current segment lookup / queue pop
  - playback status dump 的 queued count 展示
  - clear-meta reset / memset 清理
  - `playback_note_meta(...)` 里的 existing-slot scan / tail append / queue-full
    判定
- 这一步把 XiaoZhi playback segment queue 从：
  - scattered ring-head/count/segment-slots bag
  收口成：
  - explicit playback segment queue truth

## Step 5.478
- `river_cloud` 继续收口 XiaoZhi downlink/playback 的 start-gate /
  starvation-watch / retry / ring-overflow 真相源，消除 `playback_runtime`
  对这批阈值与恢复观测裸字段的混用：
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增显式 playback/downlink truth：
  - `river_cloud_xiaozhi_playback_gate_truth_t`
  - `river_cloud_xiaozhi_downlink_runtime_truth_t`
- `river_cloud_context_t` 不再暴露散落的：
  - `xiaozhi_playback_start_policy`
  - `xiaozhi_playback_start_frames`
  - `xiaozhi_playback_prefetch_frames`
  - `xiaozhi_playback_buffer_frames`
  - `xiaozhi_playback_start_cautious_history`
  - `xiaozhi_downlink_ring_dropped`
  - `xiaozhi_downlink_retry_valid`
  - `xiaozhi_downlink_starved_since_ms`
  - `xiaozhi_downlink_last_supply_ms`
- `river_cloud_xiaozhi_playback_runtime.c` 里原先直接读写上述裸字段的关键路径，
  现在统一改走 typed truth：
  - start-gate store / refresh / current-gate / diag capture
  - playback queued-frames / has-work / buffer-budget 计算
  - rebuffer keep-retry / inline-recover / write-success 生命周期
  - reset-downlink / pending-stop drop / starvation-watch 维护
  - upstream-starved 判定 / write-failed starved 推断
  - downlink ring overflow / last-supply 时间戳更新
- 这一步把 XiaoZhi downlink/playback 从：
  - scattered gate/watch/retry thresholds bag
  收口成：
  - explicit playback gate truth
  - explicit downlink runtime truth

## Step 5.477
- `river_cloud` 继续收口 XiaoZhi downlink/playback 的 execution / recovery
  真相源，消除 `playback_runtime` 对 phase / rebuffer / stop / recovery
  这批散落运行态裸字段的混用：
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增显式 playback-owned runtime truth：
  - `river_cloud_xiaozhi_playback_runtime_truth_t`
- `river_cloud_context_t` 不再暴露散落的：
  - `xiaozhi_playback_active`
  - `xiaozhi_playback_phase`
  - `xiaozhi_playback_rebuffer_cause`
  - `xiaozhi_playback_recovery_path`
  - `xiaozhi_playback_recovery_outcome`
  - `xiaozhi_playback_rebuffer_pending`
  - `xiaozhi_tts_stop_pending`
  - `xiaozhi_playback_rebuffer_count`
  - `xiaozhi_playback_rebuffer_streak`
- `river_cloud_xiaozhi_playback_runtime.c` 里原先直接读写旧 execution/recovery
  裸字段的关键路径，现在统一改走 typed runtime truth：
  - playback physical-active / phase / rebuffer-cause / recovery-path /
    recovery-outcome accessors
  - start-gate / prefetch / segment-gap hold / diag / rebuffer-observe capture
  - reset / started / stop / rebuffer / resume / pending-stop 生命周期
  - playback-start / prefetch / rebuffer / pending-stop / ack-progress 日志与判断
- 这一步把 XiaoZhi playback runtime 从：
  - scattered playback execution/recovery bag
  收口成：
  - explicit playback runtime truth
  - shared typed execution/recovery access boundary

## Step 5.476
- `river_cloud` 继续收口 XiaoZhi downlink/playback 的 meta / terminal /
  context 真相源，消除 `playback_runtime` 内部对散落 playback 裸字段的混用：
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增显式 playback-owned truth：
  - `river_cloud_xiaozhi_playback_context_truth_t`
  - `river_cloud_xiaozhi_playback_meta_truth_t`
  - `river_cloud_xiaozhi_playback_terminal_truth_t`
- `river_cloud_context_t` 不再暴露散落的：
  - `xiaozhi_playback_response_id/playback_id/segment_id`
  - `xiaozhi_playback_text/expected_duration_ms/last_meta_gap_ms/prefetch_target_ms/last_meta_ms`
  - `xiaozhi_playback_started_reported/cleared_reported/completed_reported`
  - `xiaozhi_playback_last_started_segment_id`
  - `xiaozhi_playback_last_segment_*`
  - `xiaozhi_playback_last_fully_heard_*`
  - `xiaozhi_playback_terminal_*`
- `river_cloud_xiaozhi_playback_runtime.c` 里原先直接拼接/更新旧 playback 裸字段的
  关键路径，现在统一改走 typed truth：
  - `river_cloud_xiaozhi_playback_note_meta(...)`
  - `river_cloud_xiaozhi_try_queue_playback_started_ack(...)`
  - `river_cloud_xiaozhi_try_queue_playback_cleared_ack(...)`
  - `river_cloud_xiaozhi_try_queue_playback_completed_ack(...)`
  - `river_cloud_xiaozhi_playback_finalize_cleared(...)`
  - segment-gap hold / prefetch / playback-start 等日志路径
- 这一步把 XiaoZhi playback runtime 从：
  - scattered playback terminal/meta/context bag
  收口成：
  - explicit playback-owned truth
  - shared typed context/meta/terminal access boundary

## Step 5.475
- `river_cloud` 先把 XiaoZhi `turn semantics` 从散落的 `g_river_cloud.xiaozhi_*`
  裸字段收口成显式子状态与窄视图：
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_cloud/river_cloud_xiaozhi_round_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_round_runtime.c)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
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
  不再直接拼接散落字段，而是统一消费：
  - typed turn-semantics view
- `river_cloud_xiaozhi_playback_runtime.c` 的输出 speaking 判定不再直接读：
  - `g_river_cloud.xiaozhi_output_state`
  而是改走：
  - `river_cloud_xiaozhi_capture_turn_semantics_view(...)`
- `adapter` / `round_runtime` 的 session-id 清理也从直接改裸字段收口成：
  - `river_cloud_xiaozhi_clear_session_id(...)`
- 这一步继续把：
  - scattered cloud turn-semantics bag
  收口成：
  - explicit XiaoZhi turn-semantics truth
  - typed turn-semantics export/view boundary

## Step 5.474
- `dialog runtime` 开始把外部模块从整份 `snapshot` 读取中解耦，先收口
  `river_voice_runtime_policy.c` 对 `dialog snapshot` 的直接策略解释：
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
  - [components/river_voice/river_voice_runtime_policy.c](/root/ameba-river/components/river_voice/river_voice_runtime_policy.c)
- 新增窄化对外真相：
  - `river_dialog_runtime_voice_policy_view_t`
  - `river_dialog_runtime_get_voice_policy_view(...)`
  - `river_dialog_runtime_capture_voice_policy_view_locked(...)`
- `river_voice_runtime_policy.c` 不再获取整份：
  - `river_dialog_runtime_snapshot_t`
  - `river_dialog_runtime_get_snapshot(...)`
  来判断：
  - cloud playback engaged
  - quiet window
  - restart pending block
- AEC / duplex policy 现在只消费 voice-policy 相关最小字段：
  - `playback_owner_kind`
  - `error_kind`
  - `playback_active`
  - `playback_recovering`
  - `playback_lane_engaged`
  - `playback_turn_active`
  - `tts_stop_pending`
  - `playback_terminal_waiting`
  - `playback_terminal_wait_kind`
  - `playback_backend_state_kind`
  - `playback_supply_kind`
  - `playback_hold_kind`
- 这一步继续把：
  - external snapshot-shaped policy dependency
  收口成：
  - typed dialog voice-policy view

## Step 5.473
- `dialog runtime` 继续把剩余 `control_facts -> snapshot` 导出边界收口成 typed
  control export view，并把命名拉齐到 `*_state_to_snapshot_locked(...)`：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 新增 control snapshot export carrier：
  - `river_dialog_runtime_control_export_view_t`
  - `river_dialog_runtime_capture_control_export_view_locked(...)`
  - `river_dialog_runtime_apply_control_export_view_to_snapshot_locked(...)`
  - `river_dialog_runtime_export_control_state_to_snapshot_locked(...)`
- 原先直接从 `control_facts` 抄字段到 snapshot 的：
  - `river_dialog_runtime_export_control_facts_to_snapshot_locked(...)`
  已退出
- `wake_admission`、`tts_interrupt_requested`、cloud-event 控制刷新、local
  playback fallback 控制刷新、`reconcile_facts_locked(...)` 初始化路径现在都统一
  走 shared control export view
- 这一步继续把：
  - control snapshot field copy
  收口成：
  - typed control export projection

## Step 5.472
- `dialog runtime` 继续把 cloud-owned 的 `round/io/session/playback`
  snapshot 导出边界收口成 typed `cloud export view`：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 新增 cloud export carrier：
  - `river_dialog_runtime_round_export_view_t`
  - `river_dialog_runtime_io_export_view_t`
  - `river_dialog_runtime_session_export_view_t`
  - `river_dialog_runtime_cloud_playback_export_view_t`
  - `river_dialog_runtime_cloud_export_view_t`
  - `river_dialog_runtime_capture_cloud_export_view_locked(...)`
  - `river_dialog_runtime_apply_cloud_export_view_to_snapshot_locked(...)`
  - `river_dialog_runtime_export_cloud_state_to_snapshot_locked(...)`
- 原先分别直写 snapshot 的四组 helper 已收口为 shared `capture + apply`：
  - `river_dialog_runtime_export_round_facts_to_snapshot_locked(...)`
  - `river_dialog_runtime_export_io_facts_to_snapshot_locked(...)`
  - `river_dialog_runtime_export_session_facts_to_snapshot_locked(...)`
  - `river_dialog_runtime_export_playback_facts_to_snapshot_locked(...)`
- `river_dialog_runtime_import_cloud_snapshot_locked(...)` 现在只通过：
  - `river_dialog_runtime_export_cloud_state_to_snapshot_locked(...)`
  导出 cloud-owned snapshot 区域
- cloud event 的 `session_id` 更新路径也不再单独直写 session snapshot，而是统一
  复用 shared cloud export view
- 这一步继续把：
  - split cloud snapshot field-copy helpers
  收口成：
  - typed cloud export projection

## Step 5.471
- `dialog runtime` 继续把 publish side 的 snapshot 导出边界收口成显式
  export view：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 新增 publish snapshot export carrier：
  - `river_dialog_runtime_publish_export_view_t`
  - `river_dialog_runtime_capture_publish_export_view_locked(...)`
  - `river_dialog_runtime_apply_publish_export_view_to_snapshot_locked(...)`
- `river_dialog_runtime_export_publish_state_to_snapshot_locked(...)`
  不再直接从 `publish_state` 抄字段到 snapshot，而是统一走：
  - `capture + apply`
- 这样当前 `dialog runtime` 的 runtime-state snapshot 导出已经两侧对齐：
  - playback/error -> explicit export view
  - publish state -> explicit export view
- 这一步继续把：
  - publish snapshot field copy from runtime truth
  收口成：
  - typed publish export view

## Step 5.470
- `dialog runtime` 继续把 playback/error 的 snapshot 导出边界收口成显式
  export view：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 新增 playback/error snapshot export carrier：
  - `river_dialog_runtime_playback_error_export_view_t`
  - `river_dialog_runtime_capture_playback_error_export_view_locked(...)`
  - `river_dialog_runtime_apply_playback_error_export_view_to_snapshot_locked(...)`
- 原先直接从 `error_truth` / `playback_truth` 抄字段到 snapshot 的：
  - `river_dialog_runtime_export_playback_error_facts_to_snapshot_locked(...)`
  已收口为：
  - `river_dialog_runtime_export_playback_error_state_to_snapshot_locked(...)`
  并统一走 `capture + apply`
- `refresh_error_recovering_locked(...)`、
  `refresh_playback_locked(...)`、
  `export_runtime_state_to_snapshot_locked(...)`
  现在都只通过 shared export view 刷新 snapshot
- 这一步继续把：
  - snapshot field-by-field copy from runtime truth
  收口成：
  - typed playback/error export view

## Step 5.469
- `dialog runtime` 继续把剩余 `derived_facts` 混合状态拆成显式
  `error_truth` / `playback_truth`：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 删除旧的混合状态包：
  - `river_dialog_runtime_derived_facts_t`
  - `g_river_dialog_runtime.derived_facts`
- 新增显式 runtime-owned truth：
  - `river_dialog_runtime_error_truth_t`
  - `river_dialog_runtime_playback_truth_t`
  - `g_river_dialog_runtime.error_truth`
  - `g_river_dialog_runtime.playback_truth`
- 错误与播放刷新路径现在分别直写对应 truth：
  - `refresh_error_recovering_locked(...)` 只更新 `error_truth`
  - `refresh_playback_locked(...)` 只更新 `playback_truth`
  - `export_playback_error_facts_to_snapshot_locked(...)` 再统一把两组 truth
    投影到 snapshot
- 这一步继续把：
  - residual derived state bag
  收口成：
  - explicit error truth
  - explicit playback truth

## Step 5.468
- `dialog runtime` 继续把 `snapshot export` 边界从混合 helper 收口成更明确
  的 publish/runtime 两层导出：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 原先混合导出 `publish_state + derived_facts` 的：
  - `river_dialog_runtime_export_derived_facts_to_snapshot_locked(...)`
  已拆成：
  - `river_dialog_runtime_export_playback_error_facts_to_snapshot_locked(...)`
  - `river_dialog_runtime_export_publish_state_to_snapshot_locked(...)`
  - `river_dialog_runtime_export_runtime_state_to_snapshot_locked(...)`
- 各调用点现在按真实 ownership 刷新更窄的 snapshot 区域：
  - playback/error 刷新路径只导出：
    - `error_recovering`
    - `error_kind`
    - `playback_owner_kind`
    - `playback_active`
    - `playback_recovering`
  - publish/reason 刷新路径只导出：
    - `interaction_state`
    - `transition_count`
    - `reason`
  - 只有 boot / cloud-event 这类确实同时影响两边的路径才走
    `export_runtime_state_to_snapshot_locked(...)`
- 这一步继续把：
  - mixed snapshot export helper
  收口成：
  - explicit publish export boundary
  - explicit playback/error export boundary

## Step 5.467
- `dialog runtime` 继续把 interaction publish 的 `state/count/reason`
  收口成统一 `publish_state`：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `river_dialog_runtime_publish_observe_t` 已提升为：
  - `river_dialog_runtime_publish_state_t`
  并统一持有：
  - `interaction_state`
  - `transition_count`
  - `reason`
- `derived_facts` 已不再承载 interaction publish 状态：
  - 删除：
    - `derived_facts.interaction_state`
    - `derived_facts.transition_count`
  - `snapshot.interaction_state` / `snapshot.transition_count` /
    `snapshot.reason` 现在统一从 `publish_state` 导出
- interaction publish view 继续收口：
  - 新增：
    - `previous_transition_count`
    - `next_transition_count`
    - `river_dialog_runtime_apply_interaction_publish_view_locked(...)`
  - `publish_locked(...)` 不再手工做：
    - `transition_count++`
    - `interaction_state = next_state`
    而是统一应用 typed publish view
- 这一步把：
  - interaction publish state <- derived cache
  收口成：
  - explicit publish state

## Step 5.466
- `dialog runtime` 继续把 interaction publish 的 `reason` 边界收口成 typed
  publish reason view：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 新增统一 reason publish carrier：
  - `river_dialog_runtime_publish_observe_t`
  - `river_dialog_runtime_publish_reason_view_t`
  - `river_dialog_runtime_capture_publish_reason_view_locked(...)`
  - `river_dialog_runtime_apply_publish_reason_view_locked(...)`
- `derived_facts.reason` 已退出 runtime 逻辑路径：
  - `snapshot.reason` 现在从独立 `publish_observe.reason` 导出
  - `publish_locked(...)` 不再反向读取 `derived_facts.reason`
  - `finalize_commit_locked(...)`、`init(...)`、
    `note_tts_interrupt_requested(...)` 也不再直接写 `derived_facts.reason`
- `river_dialog_runtime_publish_locked(...)`、
  `river_dialog_runtime_finalize_commit_locked(...)`、
  `river_dialog_runtime_init(...)`、
  `river_dialog_runtime_note_tts_interrupt_requested(...)`
  现在统一复用同一套 typed reason view，显式区分：
  - stored reason
  - outward publish reason
- 这一步把：
  - reason publish / snapshot export <- derived cache back-dependency
  收口成：
  - explicit publish reason boundary

## Step 5.465
- `dialog runtime` 继续把 interaction publish 的 `previous -> next`
  transition 收口成 typed publish view：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 新增统一 publish capture：
  - `river_dialog_runtime_interaction_publish_view_t`
  - `river_dialog_runtime_capture_interaction_publish_view_locked(...)`
- 删除单用途 helper：
  - `river_dialog_runtime_compute_interaction_state_locked(...)`
- `river_dialog_runtime_publish_locked(...)` 不再手工做：
  - `compute next_state`
  - `previous != next`
  - `transition_count++`
  而是统一从 typed publish view 读取：
  - `previous_state`
  - `next_state`
  - `state_changed`
- 这一步把：
  - interaction publish transition branching
  收口成：
  - single publish view

## Step 5.464
- `dialog runtime` 继续把 commit checkpoint 的 `before/after` 判定收口到 raw
  interaction evaluation：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 新增统一 checkpoint 转换 helper：
  - `river_dialog_runtime_capture_commit_checkpoint_from_interaction_eval(...)`
- `river_dialog_runtime_capture_commit_checkpoint_locked(...)` 不再回读：
  - `derived_facts.playback_active`
  - `derived_facts.playback_recovering`
  - `derived_facts.error_recovering`
  - `derived_facts.interaction_state`
  而是统一从同一次 `interaction evaluation` 派生
- `river_dialog_runtime_commit_ingress(...)` 与
  `river_dialog_runtime_finalize_commit_locked(...)`
  的 `before/after` checkpoint 现在都使用 shared raw checkpoint capture
- 这一步把：
  - commit publish gating <- derived checkpoint cache
  收口成：
  - raw interaction-eval checkpoint view

## Step 5.463
- `dialog runtime` 继续把 interaction 的 `projection -> state/policy`
  评估链收口成单一 typed evaluation：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 新增统一 interaction evaluation：
  - `river_dialog_runtime_interaction_eval_t`
  - `river_dialog_runtime_capture_interaction_eval_locked(...)`
- `river_dialog_runtime_interaction_projection_t` 不再回填：
  - `interaction_state`
  并且 `error_recovering` 也不再从 `derived_facts` 反向读取，而是直接从
  runtime raw error flags 派生
- `river_dialog_runtime_compute_interaction_state_locked(...)`、
  `river_dialog_runtime_cloud_round_active_locked(...)`、
  `river_dialog_runtime_wakeword_block_reason_locked(...)`、
  `river_dialog_runtime_allows_barge_in_interrupt(...)`
  现在统一复用 shared interaction evaluation
- 这一步把：
  - split interaction-state / policy checks
  收口成：
  - single interaction evaluation view

## Step 5.462
- `dialog runtime` 继续把 playback 的 `projection -> truth -> output_turn`
  评估链收口成单一 typed evaluation：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
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

## Step 5.461
- `dialog runtime` 继续把 cloud snapshot 的 playback 导入收口成单一 typed import：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 新增统一 playback import：
  - `river_dialog_runtime_cloud_playback_import_t`
- `river_dialog_runtime_cloud_import_t` 不再只携带：
  - `playback_facts`
  而是统一携带：
  - `playback.facts`
  - `playback.observe`
- `river_dialog_runtime_capture_cloud_snapshot(...)` 不再额外输出独立的
  `cloud_playback_observe`
- `river_dialog_runtime_import_cloud_snapshot_locked(...)` 现在一次性导入：
  - `cloud_playback_facts`
  - `cloud_playback_observe`
- `river_dialog_runtime_ingress_t` 与 commit 流程不再并行携带：
  - `has_cloud_playback_observe`
  - `cloud_playback_observe`
- 这一步把：
  - split cloud-playback facts/observe ingress transport
  收口成：
  - single cloud-playback import

## Step 5.460
- `downlink/playback runtime` 继续把 playback 对外观测导出收口成 typed diagnostics view：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增统一 diagnostics capture：
  - `river_cloud_xiaozhi_playback_diag_view_t`
  - `river_cloud_xiaozhi_capture_playback_diag_view(...)`
- `river_cloud_xiaozhi_dump_playback_status(...)` 与
  `river_cloud_xiaozhi_fill_playback_runtime_snapshot(...)` 现在共享同一份
  playback 采样结果，而不是分别再去拼：
  - truth view
  - observe view
  - start gate
  - queue/rebuffer counters
- 新增统一 recovery outcome 文本边界 helper：
  - `river_cloud_xiaozhi_playback_recovery_outcome_label(...)`
- `downlink queue` 日志与 runtime snapshot 的 recovery path/outcome、start gate、
  rebuffer cause、queue counters 现在都从 diagnostics view 读取
- 这一步把：
  - duplicated playback export sampling
  收口成：
  - single playback diagnostics capture view

## Step 5.459
- `downlink/playback runtime` 继续把 rebuffer recovery path 的本地真相源收口成 typed path：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `river_cloud_xiaozhi_playback_rebuffer_observe_view_t` 不再保存
  字符串 `recover_path`，而是保存：
  - `river_cloud_playback_recovery_path_t recovery_path`
- 删除仅为字符串路径服务的中间结果：
  - `river_cloud_xiaozhi_managed_rebuffer_recovery_result_t`
- managed rebuffer 执行链现在统一复用：
  - `river_cloud_xiaozhi_rebuffer_recovery_result_t`
- `river_cloud_xiaozhi_request_playback_rebuffer_recovery(...)` 不再返回：
  - `river_status_t + recover_path_out`
  而是直接返回 typed recovery result
- `write_failed`、`inline recover request`、`managed rebuffer request`、`upstream gap rebuffer`
  现在都在本地传递：
  - `river_cloud_playback_recovery_path_t`
  只在日志边界通过：
  - `river_cloud_xiaozhi_playback_recovery_path_label(...)`
  转成字符串
- 这一步把：
  - string-based recovery-path propagation
  收口成：
  - typed recovery-path truth + log-boundary translation

## Step 5.458
- `downlink/playback runtime` 继续把 managed rebuffer 执行结果收口成 typed result：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增统一 managed rebuffer recovery result：
  - `river_cloud_xiaozhi_managed_rebuffer_recovery_result_t`
- `river_cloud_xiaozhi_execute_managed_playback_rebuffer_recovery(...)` 不再返回：
  - `river_status_t + recover_path_out`
  的松散组合，而是统一返回 typed result
- `river_cloud_xiaozhi_execute_managed_playback_rebuffer_request(...)` 现在也直接返回
  typed result，并统一使用：
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

## Step 5.457
- `downlink/playback runtime` 继续把 rebuffer recovery path 的 preferred/fallback 执行收口成 typed attempt/result：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
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

## Step 5.456
- `downlink/playback runtime` 继续把 rebuffer 请求日志的公共观测字段收口成 typed observe view：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
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

## Step 5.455
- `downlink/playback runtime` 继续把 managed rebuffer 执行收口成 typed request executor：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
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

## Step 5.454
- `downlink/playback runtime` 继续把 write-failed 恢复链收口成 typed follow-up：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
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

## Step 5.453
- `dialog runtime` 继续把 playback raw projection 与派生 truth 解耦：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 删除 `playback projection` 对已派生 truth 的反向依赖：
  - `river_dialog_runtime_playback_projection_t` 不再携带 `playback_recovering`
  - `river_dialog_runtime_capture_playback_projection_locked(...)` 不再回填
    `derived_facts->playback_recovering`
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

## Step 5.452
- `dialog runtime` 继续把 local playback import 从分散 prepare/apply 收口成显式 plan：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 删除旧的两段式 helper：
  - `river_dialog_runtime_prepare_local_playback_import_locked(...)`
  - `river_dialog_runtime_apply_local_playback_import_locked(...)`
  - `river_dialog_runtime_apply_local_playback_state_locked(...)`
- 新增统一 import plan：
  - `river_dialog_runtime_local_playback_import_plan_t`
  - `river_dialog_runtime_prepare_local_playback_import_plan_locked(...)`
  - `river_dialog_runtime_apply_local_playback_import_plan_locked(...)`
- `dialog runtime` 现在先生成 local playback import plan，再在 cloud import 落地后统一应用：
  - stream ownership claim/release
  - playback state 写入
  - error recovery / interrupt clear
- 这一步把：
  - split local-playback prepare/apply mutation boundary
  收口成：
  - single typed local-playback import plan

## Step 5.451
- `dialog runtime` 继续收口 local playback shadow 的分散判定 helper：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 删除分散 helper：
  - `river_dialog_runtime_local_playback_state_active_locked(...)`
  - `river_dialog_runtime_local_playback_state_recovering_locked(...)`
  - `river_dialog_runtime_local_playback_shadow_drives_truth_locked(...)`
  - `river_dialog_runtime_local_playback_shadow_active_fallback_locked(...)`
  - `river_dialog_runtime_local_playback_shadow_recovering_fallback_locked(...)`
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

## Step 5.450
- `downlink/playback` 继续收口 ready-cycle 的单层 executor 壳：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 删除中间 helper：
  - `river_cloud_xiaozhi_execute_ready_downlink_cycle(...)`
- `river_cloud_xiaozhi_process_downlink_task_cycle(...)` 现在直接接管：
  - ready cycle 下的 current-frame acquire
  - write-step dispatch
- `downlink cycle processor` 现在直接按：
  - `cycle_plan.ready`
  - frame acquired / not acquired
  - `write_current_downlink_frame_step(...)`
  决定后续 task-step
- 这一步把：
  - single-layer ready-cycle executor shell
  收口成：
  - direct cycle-processor ownership

## Step 5.449
- `downlink/playback` 继续把当前帧写入执行收口成单一 write-step helper：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 删除中间 typed result：
  - `river_cloud_xiaozhi_downlink_frame_write_result_t`
- `river_cloud_xiaozhi_write_current_downlink_frame_step(...)` 现在直接返回：
  - `river_cloud_xiaozhi_downlink_task_step_result_t`
- `river_cloud_xiaozhi_execute_ready_downlink_cycle(...)` 不再负责：
  - write-result -> task-step 的翻译
  - post-write completion dispatch
- 单帧写入 helper 现在统一接管：
  - frame oversize abort -> continue
  - playback service write failure -> continue / sleep poll
  - write success 后的 segment start / ack progress / pending-stop 收尾
- 这一步把：
  - split write-result + post-write completion contract
  收口成：
  - single write-step outcome

## Step 5.448
- `downlink/playback` 继续把 `acquire_current_downlink_frame(...)` 的 ready 判定从二值枚举收口成布尔返回：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 删除中间 typed result：
  - `river_cloud_xiaozhi_downlink_frame_acquire_result_t`
- `river_cloud_xiaozhi_acquire_current_downlink_frame(...)` 现在直接返回：
  - `bool`
- `river_cloud_xiaozhi_execute_ready_downlink_cycle(...)` 不再负责：
  - acquire-result -> sleep/continue 的枚举翻译
- 这一步把：
  - binary frame-acquire contract
  收口成：
  - direct acquire predicate

## Step 5.447
- `downlink/playback` 继续把 `prepare_downlink_playback(...)` 的 ready 判定从二值枚举收口成布尔返回：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
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

## Step 5.446
- `downlink/playback` 继续把 prepare 阶段从 `result + out param` 收口成单一 cycle plan：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
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

## Step 5.445
- `downlink/playback` 继续把 ready downlink cycle 的执行结果直接归一到 worker task-step：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
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

## Step 5.444
- `downlink/playback` 继续把 downlink worker 的 task-step 调度壳从 downlink task 主循环中收口成单一 cycle processor：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
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
- 这一步把：
  - residual task-loop orchestration shell
  收口成：
  - single task-step processor + delay finisher

## Step 5.443
- `downlink/playback` 继续把 ready 状态下的 downlink cycle 执行从 downlink task 主循环中收口成统一 executor：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
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
- `downlink task` 在 prepare-ready 之后现在只保留：
  - call `execute_ready_downlink_cycle(...)`
  - poll delay on execute sleep result
- 这一步把：
  - scattered ready-cycle execute branching
  收口成：
  - single typed ready-cycle executor

## Step 5.442
- `downlink/playback` 继续把 downlink worker 的前置调度执行从 downlink task 主循环中收口成统一 cycle helper：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
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
- `downlink task` 在 acquire 与 write 之前现在只保留：
  - call `prepare_downlink_cycle(...)`
  - idle delay + continue on idle cycle result
  - poll delay + continue on non-ready cycle result
- 这一步把：
  - scattered pre-acquire/pre-write cycle orchestration
  收口成：
  - single typed downlink-cycle entrypoint

## Step 5.441
- `downlink/playback` 继续把单帧写成功后的收尾执行从 downlink task 主循环中收口成统一 helper：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增统一 helper：
  - `river_cloud_xiaozhi_complete_written_downlink_frame(...)`
- 该 helper 现在统一接管：
  - segment start latch on progress
  - playback ACK progress refresh
  - pending-stop check after progress
- `downlink task` 在 frame write 成功后现在只保留：
  - call `complete_written_downlink_frame(...)`
- 这一步把：
  - scattered post-write progress finalization
  收口成：
  - single post-write completion entrypoint

## Step 5.440
- `downlink/playback` 继续把当前帧获取执行从 downlink task 主循环中收口成统一 helper：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增本地 typed result：
  - `river_cloud_xiaozhi_downlink_frame_acquire_result_t`
- 新增统一 helper：
  - `river_cloud_xiaozhi_acquire_current_downlink_frame(...)`
- 该 helper 现在统一接管：
  - retry-valid fast path
  - downlink ring read
  - ring read failure 后的 pending-stop check
- `downlink task` 在 prepare 与 write 之间现在只保留：
  - call `acquire_current_downlink_frame(...)`
  - delay + continue on non-ready result
- 这一步把：
  - scattered frame acquire / retry execution
  收口成：
  - single frame-acquire entrypoint

## Step 5.439
- `downlink/playback` 继续把 playback 准备阶段从 downlink task 主循环中收口成统一 helper：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
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
- `downlink task` 在读 ring / 写 frame 前现在只保留：
  - call `prepare_downlink_playback(...)`
  - delay + continue on non-ready result
- 这一步把：
  - scattered start/resume gating
  收口成：
  - single playback-prepare entrypoint

## Step 5.438
- `downlink/playback` 继续把单帧写入执行从 downlink task 主循环中收口成统一 helper：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
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
- `downlink task` 现在只按单帧写入结果决定：
  - continue
  - delay + continue
  - progress

## Step 5.437
- `downlink/playback` 继续把 `write_failed` 从 downlink task 主循环里收口成单入口 handler：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增统一 helper：
  - `river_cloud_xiaozhi_handle_playback_write_failed(...)`
- 该 helper 现在统一接管：
  - recovery plan capture
  - initial write-failed log
  - inline recover dispatch
  - managed rebuffer fallback dispatch
- `downlink task` 在 `river_playback_service_write(...)` 失败后现在只保留：
  - call `handle_playback_write_failed(...)`
  - delay + continue on non-inline recovery
- 这一步把：
  - write_failed local orchestration
  收口成：
  - single write-failure entrypoint

## Step 5.436
- `downlink/playback` 继续把 `write_failed` 的恢复执行从 downlink task 主循环中抽成专用 helper：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增本地 typed result：
  - `river_cloud_xiaozhi_inline_recover_result_t`
- 新增统一 helper：
  - `river_cloud_xiaozhi_try_write_failed_inline_recover(...)`
  - `river_cloud_xiaozhi_handle_write_failed_managed_rebuffer(...)`
  - `river_cloud_xiaozhi_log_write_failed_rebuffer_request(...)`
- `write_failed` 主分支现在只保留：
  - capture `recovery_plan`
  - choose `recover_reason`
  - switch inline recover result
  - dispatch managed rebuffer fallback executor
- 这一步把：
  - nested inline recover / replay / fallback branches
  收口成：
  - typed inline result + dedicated recovery executors

## Step 5.435
- `downlink/playback` 继续把 managed rebuffer 的进入与恢复执行从分散分支中收口成统一 helper：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增统一 helper：
  - `river_cloud_xiaozhi_begin_managed_playback_rebuffer(...)`
  - `river_cloud_xiaozhi_execute_managed_playback_rebuffer_recovery(...)`
- `maybe_rebuffer_starved(...)` 现在不再自己拼装：
  - `note_playback_rebuffer`
  - `refresh_playback_recovery_plan_after_rebuffer_note`
  - `request_playback_rebuffer_recovery`
  这条链改为统一经过上述 helper
- `write_failed` 分支里这三类 managed rebuffer 升级路径现在也共用同一条 helper 链：
  - inline replay fallback
  - inline recover fallback
  - non-inline direct rebuffer
- 这一步把：
  - rebuffer pending / recovery outcome / retry frame / actual recovery execute
  从多个局部分支里的重复状态变更
  收口成：
  - shared managed-rebuffer transition boundary

## Step 5.434
- `downlink/playback` 继续把恢复链从“只知道走了哪条 path”推进到“还能知道这次恢复最终是 inline 自愈还是升级成 managed rebuffer”：
  - [include/river/river_cloud.h](/root/ameba-river/include/river/river_cloud.h)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 新增公共 typed 枚举：
  - `river_cloud_playback_recovery_outcome_t`
- `cloud runtime snapshot` 现在额外导出：
  - `playback_recovery_outcome_kind`
  - `playback_recovery_outcome`
- XiaoZhi playback runtime 现在会把：
  - inline `service_recover + replay` 成功
    记为 `inline_replay`
  - 任意 `note_playback_rebuffer(...)`
    记为 `managed_rebuffer`
- `cloud runtime dump` 与 `dialog runtime dump` 也开始打印：
  - `recovery_outcome=...`
- 这一步继续把恢复真相从：
  - `path only`
  推进到：
  - `path + outcome`
  让上层直接区分“这次故障已经被本轮自愈吸收”还是“已经升级成 managed rebuffer”

## Step 5.433
- `downlink/playback` 继续把 `playback_recovery_path` 从“只在 managed rebuffer
  分支里偶尔可见”的状态推进成当前 response 内稳定可见的最近恢复动作观测：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增统一 helper：
  - `river_cloud_xiaozhi_set_playback_recovery_path(...)`
- `request_playback_rebuffer_recovery(...)` 不再自己直写全局字段，改为统一经过
  上述 helper 更新 `xiaozhi_playback_recovery_path` 并触发 state sync
- `write_failed` 的 inline `service_recover` 成功、以及随后直接回退
  `stop_rebuffer` 的分支，现在也都会写入同一条 recovery-path 真相链
- `finish_playback_rebuffer()` 不再清空 `playback_recovery_path`，因此 cloud/dialog
  snapshot 会在当前 response 生命周期内保留最近一次实际恢复路径，而不是恢复一结束就丢
- XiaoZhi playback status dump 新增：
  - `recovery_path=...`
- 这一步继续把 recovery path 从：
  - “managed rebuffer 内部临时字段”
  推进成：
  - “write_failed / inline recover / fallback / rebuffer 共用的最近恢复动作观测”

## Step 5.432
- `downlink/playback` 继续把实际 recovery path 提升成跨模块可见的 typed
  observability：
  - [include/river/river_cloud.h](/root/ameba-river/include/river/river_cloud.h)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 新增公共 typed 枚举：
  - `river_cloud_playback_recovery_path_t`
- `cloud runtime snapshot` 现在额外导出：
  - `playback_recovery_path_kind`
  - `playback_recovery_path`
- XiaoZhi playback runtime 新增持久观测字段：
  - `xiaozhi_playback_recovery_path`
  并在 `request_playback_rebuffer_recovery(...)` 里按 fallback 后的实际路径写入
- playback observe view / dialog playback observe 现在都会镜像这条路径，`dialog
  runtime dump` 与 `xiaozhi runtime dump` 也开始打印 `recovery_path`
- 这一步继续把：
  - internal recovery plan
  - external cloud/dialog observability
  连接起来，避免外层只能靠 `rebuffer_pending + cause` 反推当前恢复链

## Step 5.431
- `downlink/playback` 继续把 `write_failed` / `starved` 恢复决策收口成统一 typed
  recovery plan：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增内部结构：
  - `river_cloud_xiaozhi_playback_recovery_plan_t`
  - `river_cloud_xiaozhi_playback_recovery_path_t`
- `river_cloud_xiaozhi_capture_playback_recovery_plan(...)` 现在会一次性锁存：
  - `cause`
  - `supply_kind`
  - `start_gate`
  - `low_water_frames`
  - `supply_gap_ms`
  - `recovery_path`
  - `inline_recover_allowed`
- `maybe_rebuffer_starved(...)` 与 `write_failed` 分支现在统一消费这份 recovery
  plan，不再各自散读一组局部变量重建恢复决策
- `request_playback_rebuffer_recovery(...)` 现在会在 fallback 后返回实际采用的
  recovery path，不再把：
  - `service_recover -> stop_rebuffer`
  - 或 `stop_rebuffer -> service_recover`
  的回退仍错误打印成初始偏好路径
- 这一步继续把 downlink/playback 恢复链从“多处分支各自重猜恢复策略”推进到
  “single recovery plan -> execute/fallback/log”

## Step 5.430
- `dialog runtime` 继续把 playback 文本观测从 `session_observe` 中拆出去：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `river_dialog_runtime_cloud_session_observe_t` 不再承载：
  - `playback_terminal_reason`
  - `playback_terminal_wait_reason`
- 上述字段现在统一并入：
  - `river_dialog_runtime_cloud_playback_observe_t`
- `capture_cloud_snapshot(...)` 现在把 playback terminal/wait 文本直接写入
  `cloud_playback_observe`
- `export_session_facts_to_snapshot_locked()` 不再导出 playback terminal 文本；
  这些字段改由 `export_playback_facts_to_snapshot_locked()` 从
  `cloud_playback_observe` 统一导出
- 这一步继续把 `session` 元数据与 `playback` 观测元数据拆开，避免 playback
  文本原因链继续挂在 session observe 上

## Step 5.429
- `dialog runtime` 继续把 playback 里的 observe-only 字段从 typed facts 中拆出去：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `river_dialog_runtime_cloud_playback_facts_t` 不再承载：
  - `terminal_state_kind`
  - `rebuffer_cause_kind`
  - `start_policy_kind`
  - `start_frames`
  - `prefetch_frames`
  - `start_cautious_history`
- 上述字段现在统一并入：
  - `river_dialog_runtime_cloud_playback_observe_t`
- `capture_cloud_snapshot(...)` 现在把这些字段直接写入
  `cloud_playback_observe`
- `export_playback_facts_to_snapshot_locked()` 现在也改为从
  `cloud_playback_observe` 导出：
  - terminal state
  - rebuffer cause
  - start gate/prefetch/cautious
- 这一步继续把 `dialog runtime` 的 playback 结构压实成：
  - `cloud_playback_facts` 只保留参与交互/恢复判断的 typed truth
  - `cloud_playback_observe` 统一承载 phase、terminal/rebuffer、start-gate 观测

## Step 5.428
- `dialog runtime` 继续把 `io/session` 里的纯观测字段从 typed facts 中拆出去：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `river_dialog_runtime_cloud_io_facts_t` 现在只承载：
  - `input_lane`
  - `output_lane`
- 新增 `river_dialog_runtime_cloud_io_observe_t`，独立承载：
  - `input_state_text`
  - `output_state_text`
- `river_dialog_runtime_cloud_session_facts_t` 现在只承载：
  - `turn_accepted`
  - `barge_in_enabled_known`
  - `barge_in_enabled`
- 新增 `river_dialog_runtime_cloud_session_observe_t`，独立承载：
  - `provider_name`
  - `session_id`
  - `turn_id`
  - `accept_reason`
  - `playback_terminal_reason`
  - `playback_terminal_wait_reason`
- `capture_cloud_snapshot(...)` / `import_cloud_snapshot_locked(...)` / snapshot export
  现在都按：
  - facts
  - observe
  两条链分别搬运 `io/session` 数据
- `apply_cloud_event_locked(...)` 更新 sid 时也改写到
  `cloud_session_observe.session_id`
- 这一步继续把 `dialog runtime` 内部可驱动行为的 typed truth 与纯日志/调试/镜像元数据拆开，避免：
  - `io` 文本状态
  - `session/turn/reason` 文本
  再混进内部 facts 结构

## Step 5.427
- `dialog runtime` 继续把 cloud snapshot ingress 从扁平字段包收口成与内部事实一致的分层载荷：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `river_dialog_runtime_cloud_import_t` 不再平铺：
  - round/window/listening 字段
  - input/output io 字段
  - session/turn/meta 字段
  - playback semantic 字段
- 现在它直接按内部真相结构分成：
  - `round_facts`
  - `io_facts`
  - `session_facts`
  - `playback_facts`
- `river_dialog_runtime_capture_cloud_snapshot(...)` 现在直接把 cloud runtime snapshot
  填充到这些 typed facts 载荷里，而不是先展开成一组临时扁平字段
- `river_dialog_runtime_import_cloud_snapshot_locked(...)` 也改为整块吸收：
  - `cloud_round_facts`
  - `cloud_io_facts`
  - `cloud_session_facts`
  - `cloud_playback_facts`
  不再逐字段搬运
- 这一步继续把 `dialog runtime` 的 ingress/import 边界推进到：
  - capture/import 结构与 runtime 内部事实结构同构
  - cloud import 只是 snapshot 到事实结构的搬运层
  - reducer/import 不再维护另一套平铺字段协议

## Step 5.426
- `dialog runtime` 继续把 cloud snapshot 输入层里的 playback 观测与语义事实拆开：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `river_dialog_runtime_cloud_import_t` 不再承载：
  - `playback_phase_known`
  - `playback_phase_kind`
- `river_dialog_runtime_ingress_t` 新增独立 observe 载荷：
  - `has_cloud_playback_observe`
  - `cloud_playback_observe`
- 新增统一 snapshot 捕获入口：
  - `river_dialog_runtime_capture_cloud_snapshot(...)`
  它会一次性拆出：
  - `cloud_import`
  - `cloud_playback_observe`
- 新增独立导入路径：
  - `river_dialog_runtime_import_cloud_playback_observe_locked(...)`
- `commit_cloud_event` / `reduce_local_playback_event` / `sync_cloud_state`
  现在都会把 cloud playback phase 观测值作为独立载荷送入 reducer，而不是继续混在
  cloud import 语义结构中
- 这一步继续把 `dialog runtime` 的：
  - cloud import semantic facts
  - playback phase observability
  在 ingress/import 边界彻底拆开，避免后续 reducer/import 路径把 phase 观测字段重新当成内部真相

## Step 5.425
- `cloud playback runtime` 继续把 phase 观测值从 truth 链里彻底拆出去：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增独立观测结构：
  - `river_cloud_xiaozhi_playback_observe_view_t`
  - `river_cloud_xiaozhi_capture_playback_observe_view(...)`
- 新增统一观测读取 helper：
  - `river_cloud_xiaozhi_playback_observed_phase_kind()`
- `river_cloud_xiaozhi_playback_backend_source_t` 不再承载：
  - `phase`
- `river_cloud_xiaozhi_fill_playback_runtime_snapshot(...)` 现在同时读取：
  - `truth_view`
  - `observe_view`
- `playback_phase_known`
  - `playback_phase_kind`
  - `playback_phase` 文本
  现在都改为从 `observe_view` 导出，而不是再从 `truth_view.backend_source.phase`
  间接读取
- playback 相关日志/诊断路径也统一改读：
  - `river_cloud_xiaozhi_playback_observed_phase_kind()`
  不再从：
  - `playback_source.phase`
  - `truth_view.backend_source.phase`
  取 phase
- 这一步继续把 cloud runtime 内部的：
  - typed playback truth
  - playback phase observability
  两条链拆开，为后续把日志链也从 truth 结构里解耦做准备

## Step 5.424
- `dialog runtime` 继续清理内部“语义事实”和“观测字段”的边界：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `river_dialog_runtime_cloud_playback_facts_t` 不再承载：
  - `phase_known`
  - `phase_kind`
- 新增独立观测结构：
  - `river_dialog_runtime_cloud_playback_observe_t`
- `dialog runtime` 现在把 cloud snapshot 的 phase 观测值单独存放到：
  - `cloud_playback_observe`
- `river_dialog_runtime_export_playback_facts_to_snapshot_locked()` 改为从
  `cloud_playback_observe` 导出 `playback_phase_known/playback_phase_kind`
- 这一步进一步避免 phase 观测字段与：
  - lane/turn/recovering/hold/supply
  这些语义事实混放，降低后续被误当作业务真相再次读回去的风险

## Step 5.423
- `dialog runtime` 继续缩减内部 projection 对 coarse playback phase 的依赖：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `river_dialog_runtime_playback_projection_t` 不再镜像：
  - `phase_known`
  - `phase_kind`
- `river_dialog_runtime_capture_playback_projection_locked(...)` 不再把 cloud
  playback phase 塞进 projection 内部语义
- `river_dialog_runtime_playback_turn_retains_output_turn_from_projection(...)`
  现在只保留：
  - `!cloud_runtime_available -> true`
  这个保守 fallback
- 它不再通过：
  - `!phase_known`
  来决定是否保留 output turn
- 这一步把 `dialog runtime` 内部 projection 进一步推进到：
  - phase 仅保留在 cloud/dialog snapshot 里做观测
  - projection 内部决策只读 typed playback truth 与 runtime availability

## Step 5.422
- `cloud playback runtime` 继续把内部播放语义从 coarse phase 解耦：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `river_cloud_xiaozhi_playback_backend_source_t` 现在额外镜像：
  - `stop_pending`
  - `rebuffer_pending`
  - `physical_active`
  - `waiting_next_segment`
- `river_cloud_xiaozhi_compute_playback_backend_state_from_source(...)`
  不再读取：
  - `phase == REBUFFERING`
  - `phase_is_output_active(...)`
- 现在直接用底层 truth 决定：
  - recovering
  - owned_active
  - owned_paused
- `river_cloud_xiaozhi_playback_hold_kind_from_source(...)` 不再读取：
  - `phase == WAITING_SEGMENT`
- 现在改为直接读取：
  - `waiting_next_segment`
  - `rebuffer_pending`
- `river_cloud_xiaozhi_playback_output_active()` 与
  `river_cloud_xiaozhi_output_speaking_active()` 现在统一复用 truth view helper：
  - `river_cloud_xiaozhi_playback_backend_source_output_active(...)`
  - `river_cloud_xiaozhi_playback_truth_view_retains_output_turn(...)`
- 这一步继续把 phase 从“内部决策输入”降级为“观测值”，进一步收口：
  - backend_state
  - hold_kind
  - output_active
  - speaking retain-output-turn
  对 coarse phase 的依赖

## Step 5.421
- `cloud playback runtime` 继续清理导出链中对 coarse playback phase 的直接依赖：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `river_cloud_xiaozhi_playback_truth_view_t` 现在额外镜像：
  - `queued_frames`
  - `tts_stop_pending`
  - `rebuffer_pending`
- 新增统一 typed helper：
  - `river_cloud_xiaozhi_playback_supply_engages_lane(...)`
  - `river_cloud_xiaozhi_playback_lane_engaged_from_truth_view(...)`
  - `river_cloud_xiaozhi_playback_turn_active_from_truth_view(...)`
- `river_cloud_xiaozhi_playback_lane_engaged()` 不再直接读取：
  - `phase != IDLE`
- `river_cloud_xiaozhi_playback_turn_active()` 也不再通过 `lane_engaged <- phase`
  的链路间接重建 turn 语义
- `river_cloud_xiaozhi_capture_held_by_playback(...)` 与
  `river_cloud_xiaozhi_fill_playback_runtime_snapshot(...)` 现在统一复用同一套
  typed helper，不再在导出路径上回退到：
  - `phase != IDLE`
- 这一步把：
  - public `lane_engaged`
  - public `turn_active`
  - capture-held 判定
  - cloud runtime snapshot 导出
  全部收口到同一条 typed truth 语义链，继续压缩 phase 在 playback 导出层的职责

## Step 5.420
- `dialog runtime` 继续去掉内部派生逻辑对 coarse playback phase 的直接依赖：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `river_dialog_runtime_playback_projection_t` 现在显式镜像：
  - `supply_kind`
- `river_dialog_runtime_playback_waiting_segment_from_projection(...)` 不再读取：
  - `phase_known`
  - `phase_kind == WAITING_SEGMENT`
- 现在改为直接消费已经由 playback runtime 导出的：
  - `playback_supply_kind == RIVER_CLOUD_PLAYBACK_SUPPLY_WAITING_NEXT_SEGMENT`
- `river_dialog_runtime_playback_turn_retains_output_turn_from_projection(...)`
  也不再用：
  - `phase_kind == REBUFFERING`
  来决定是否维持 output turn
- 现在改为复用新的 typed helper：
  - `river_dialog_runtime_playback_turn_recovering_from_projection(...)`
  - 读取：
    - `rebuffer_pending`
    - `backend_state_kind == OWNED_RECOVERING`
    - `backend_state_kind == RESTART_PENDING`
- 同时保留原有保守兜底：
  - `phase_unknown && !cloud_runtime_available`
  仍允许退回本地 shadow fallback 语义，不引入 cloud snapshot 缺席时的回归
- 这一步继续把 `dialog runtime` 从“根据 phase 重建播放语义”推进到“直接消费
  playback runtime 已导出的 typed truth”，进一步压实：
  - waiting-next-segment 由 supply truth 解释
  - retain-output-turn 由 recovering truth 解释

## Step 5.419
- XiaoZhi downlink / playback 继续把 detached quiet-window 从 phase fallback
  收口到显式的 supply truth：
  - [include/river/river_cloud.h](/root/ameba-river/include/river/river_cloud.h)
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
  - [components/river_voice/river_voice_runtime_policy.c](/root/ameba-river/components/river_voice/river_voice_runtime_policy.c)
- 新增公开的 playback supply truth：
  - `river_cloud_playback_supply_kind_t`
  - `river_cloud_playback_supply_kind_name(...)`
- `river_cloud_runtime_snapshot_t` 与 `river_dialog_runtime_snapshot_t` 现在都显式导出：
  - `playback_supply_kind`
- `dialog runtime` 的 cloud import / playback facts / exported snapshot 也同步纳入
  这条 supply truth，`dialog_runtime dump` 日志新增 `supply=...`
- `river_cloud_xiaozhi_playback_quiet_window_from_gate_view()` 在
  `BACKEND_DETACHED` 场景下不再回落到：
  - `PREFETCHING`
  - `REBUFFERING`
  - `WAITING_SEGMENT`
  这些 coarse phase
- 现在它直接读取：
  - `CURRENT_SEGMENT`
  - `WAITING_NEXT_SEGMENT`
  - `TERMINAL_TAIL`
  这些 typed supply truth 来识别 detached 静默窗口
- `river_voice_runtime_dialog_playback_quiet_window()` 也同步改成读取
  dialog snapshot 的 `playback_supply_kind`，去掉 detached quiet-window 的 phase 兜底
- 这一步把 `quiet_window` / `restart_pending` 在 detached 场景下最后一段对 phase 的
  常态依赖也替换成了 typed supply truth，进一步靠近：
  - playback runtime 负责导出媒体供给语义
  - dialog runtime 只镜像这条真相
  - voice runtime 只消费镜像后的 typed truth

## Step 5.418
- XiaoZhi downlink / playback 继续把 `quiet_window` / `capture_held` 从粗 phase
  判定收口到 typed playback truth：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_voice/river_voice_runtime_policy.c](/root/ameba-river/components/river_voice/river_voice_runtime_policy.c)
- 新增统一 gate 视图：
  - `river_cloud_xiaozhi_playback_gate_view_t`
  - `river_cloud_xiaozhi_capture_playback_gate_view(...)`
  - `river_cloud_xiaozhi_playback_quiet_window_from_gate_view(...)`
- `river_cloud_xiaozhi_playback_quiet_window_allows_vad_open()` 不再直接把整个：
  - `PREFETCHING`
  - `REBUFFERING`
  - `WAITING_SEGMENT`
  都当作静音窗口
- 现在优先消费的 typed truth 是：
  - `backend_state`
  - `hold_kind`
  - `terminal_wait_kind`
  - `tts_stop_pending`
  - `output_active`
- 只有 `DETACHED` 的残余过渡态才继续回落到 phase 兜底，避免完全失去对尚未
  导出 supply truth 的兼容
- `river_cloud_xiaozhi_capture_held_by_playback(...)` 也改为复用同一 gate truth，
  不再先各自重复猜测 quiet-window
- `river_voice_runtime_restart_pending_requires_block(...)` 不再依赖单独的
  `restart_pending_quiet_phase(...)` 粗 phase helper，而是改为读取 dialog
  snapshot 中已导出的 typed playback truth：
  - `playback_backend_state_kind`
  - `playback_hold_kind`
  - `playback_terminal_wait_kind`
  - `tts_stop_pending`
  - `playback_active`
- 这一步把 cloud runtime 与 voice runtime 对“当前是否属于可开放 VAD 的静默恢复窗口”
  的语义进一步对齐到同一套 playback truth 上，减少：
  - `restart_pending` 被误当作持续播放占用
  - segment-gap / terminal wait / stop-pending 被不同模块各自粗判
  - capture reopen / AEC gate / follow-up reopen 之间的语义漂移

## Step 5.417
- XiaoZhi downlink / playback 继续把 `segment_gap_hold` 从固定阈值收紧为动态低水位：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 移除了固定的 `RIVER_CLOUD_XIAOZHI_SEGMENT_GAP_HOLD_FRAMES=1`
- 新增：
  - `river_cloud_xiaozhi_downlink_segment_gap_hold_frames()`
- `segment_gap_hold` 现在改为使用：
  - `attached_resume_threshold_frames - 1`
  - 再受 `starved_low_water_frames` 约束
  - 最终保持至少 `1 frame`
- 这意味着段间 hold 不再等到只剩固定 `1 frame` 才触发，而是可以在仍低于
  attached resume 门槛、但已接近尾部 underrun 风险的窗口里更早挂起
- 同时由于 hold 阈值始终低于 attached resume 阈值，worker 不会因为刚进入 hold
  就立刻满足 resume 条件而自解
- 这一步继续把段间恢复从“硬编码 1 frame 猜测”收紧到“依赖 runtime start/resume 真相的
  typed low-water hold”，进一步减少尾部 underrun 先发生再恢复的概率

## Step 5.416
- XiaoZhi downlink / playback 继续收窄 `segment_gap_hold` 的破坏边界：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `river_cloud_xiaozhi_hold_playback_for_segment_gap(...)` 的 attached 路径现在不再走
  destructive `river_playback_service_flush_stream_ex(...)`，而是改为优先
  `river_playback_service_recover_stream_ex(...)`
- 这意味着段间 hold 仍然保持：
  - attached hold
  - `OWNED_PAUSED -> maybe_resume_paused_playback()` 现有恢复链
  但不再为常态 next-segment 等待重置 reference/AEC 历史
- hold 诊断模式也同步改成：
  - `attached_recover`
  - `detached_stop`
- 这一步继续把段间等待从“借 flush 实现暂停”收紧到“轻量 recover 实现 attached
  hold”，减少段间抖动时 reference/AEC 被不必要打断的概率

## Step 5.415
- XiaoZhi downlink / playback 继续收紧纯 `write_failed` 的瞬时恢复延迟：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 当 cause 仍是纯 `RIVER_CLOUD_PLAYBACK_REBUFFER_CAUSE_WRITE_FAILED` 且 recovery
  路径选择 `service_recover` 时，worker 现在会在 recover 成功后，同一轮立即重写
  当前 frame，而不是留到下一次 poll 再重试
- `xiaozhi_downlink_retry_valid` 现在只在真正进入：
  - recover fallback
  - inline replay fallback
  - 非 inline rebuffer/retry
  时才置位，不再被每次 recover 成功的瞬时写失败污染
- 新增更细的诊断日志：
  - `xiaozhi playback inline recover replay succeeded`
  - `xiaozhi playback rebuffer requested after inline replay fallback`
- 这一步继续把纯 `write_failed` 的恢复从“recover 成功，但还要再等一轮 worker”
  收紧到“recover 成功即刻重写当前帧”，进一步缩短一次 poll 周期的额外播放卡顿窗口，
  并继续降低 `rebuffer_pending / streak / start_gate` 的误触发概率

## Step 5.414
- XiaoZhi downlink / playback 继续拆分 `write_failed` 的 recover 与 rebuffer 语义：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `write_failed` 分支现在会先区分：
  - 纯 `RIVER_CLOUD_PLAYBACK_REBUFFER_CAUSE_WRITE_FAILED`
  - 被重分类成 `UPSTREAM_STARVED`
- 当 cause 仍是 `WRITE_FAILED` 且 recovery 路径是 `service_recover` 时，worker
  现在不再立刻：
  - `note_playback_rebuffer(...)`
  - 进入 `rebuffer_pending`
  - 累加 `rebuffer_streak`
  - 污染后续 `start_gate`
- 这类纯写失败现在先走 inline `river_playback_service_recover_stream_ex(...)`；
  只有 recover 失败并回退到 `stop_rebuffer` 时，才正式登记
  `playback_rebuffer_pending`
- 新增更细的诊断日志：
  - `xiaozhi playback service recover requested`
  - `xiaozhi playback rebuffer requested after recover fallback`
- 这一步把 downlink write-fail recovery 从“瞬时写失败立即升级成 rebuffer 语义”
  收紧到“先尝试轻量 service recover，只有 recover 失败才进入 rebuffer”，继续降低
  rebuffer 风暴和 start-gate 被误抬高的风险

## Step 5.413
- playback service 继续拆分 `flush` / `recover` 的破坏边界：
  - [components/river_voice/river_playback_service.c](/root/ameba-river/components/river_voice/river_playback_service.c)
- 新增内部辅助层：
  - `river_playback_service_restart_started_track_locked(...)`
- `river_playback_service_flush_locked(...)` 现在只保留 destructive flush 语义：
  - 先 `river_reference_service_reset()`
  - 再重启 `AudioTrack`
- `river_playback_service_recover_locked(...)` 不再复用 flush 路径，而是直接走
  track restart，不再在 transient `playback_write_failed` recover 成功时清空
  reference/AEC 历史
- recover 成功后仍回到 `RIVER_PLAYBACK_RUNNING`，但如果 restart 失败，仍保持原有
  失败回退语义：
  - `close_locked(true)`
  - `RIVER_PLAYBACK_RESTART_PENDING`
- 这一步把 downlink write-fail recovery 的 blast radius 从
  “write failed -> flush track + reset reference” 收紧到
  “write failed -> 尝试仅恢复 track；只有恢复失败才进入更重的重启路径”，为后续继续
  重建 rebuffer / recovery policy 提供更稳定的 AEC 连续性

## Step 5.412
- XiaoZhi downlink / playback 继续重建 segment-gap 恢复链：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增内部低水位 hold 视图：
  - `river_cloud_xiaozhi_segment_gap_hold_view_t`
- `waiting_next_segment` 现在不再等到 `queued_frames == 0` 才触发 hold，而是：
  - 先捕获 `truth_view + queued_frames`
  - 当 supply 已进入 `WAITING_NEXT_SEGMENT` 且 queued 只剩 1 帧时，提前执行 segment-gap hold
- `river_cloud_xiaozhi_maybe_pause_for_segment_gap()` 现在在 worker 主循环里：
  - `rebuffer_starved` 之前仍先判重缓冲
  - 再依据低水位 hold view 预先挂起 playback
  - `queued == 0` 分支退化为兜底检查
- 这一步把 segment-gap 恢复从“硬件先 underrun、再 write_failed/rebuffer”前移到
  “接近队尾时主动挂起”，减少了一个会放大抖动的卡顿入口，为后续继续重建
  downlink recovery / resume 逻辑打基础

## Step 5.411
- dialog runtime 继续把 residual control / derived state 从 exported snapshot 中剥离：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 新增内部真相载体：
  - `river_dialog_runtime_control_facts_t`
  - `river_dialog_runtime_derived_facts_t`
  - `g_river_dialog_runtime.control_facts`
  - `g_river_dialog_runtime.derived_facts`
- 新增统一镜像辅助层：
  - `river_dialog_runtime_export_control_facts_to_snapshot_locked(...)`
  - `river_dialog_runtime_export_derived_facts_to_snapshot_locked(...)`
- 以下 residual dialog state 现在改由 internal facts 持有，再镜像到 exported snapshot：
  - `boot_ready`
  - `wake_confirmed`
  - `asr_session_active`
  - `wake_admission_pending`
  - `tts_interrupt_requested`
  - `error_recovering`
  - `error_kind`
  - `playback_active`
  - `playback_recovering`
  - `playback_owner_kind`
  - `interaction_state`
  - `transition_count`
  - `reason`
- `capture_playback_projection_locked(...)` 现在改读 internal `derived_facts` 的
  `playback_recovering`
- `capture_interaction_projection_locked(...)` 现在改读：
  - internal `control_facts`
  - internal `derived_facts`
  不再把 exported snapshot 当作内部 dialog state 真相源
- `refresh_error_recovering_locked(...)` / `refresh_playback_locked(...)` /
  `publish_locked(...)` / `reconcile_facts_locked(...)` 现在统一更新 internal facts，
  再导出到 snapshot
- 这一步把 dialog runtime 内部 reducer / projection / publish 对 exported snapshot
  的依赖进一步压缩到 export/get/dump 边界，为后续继续重建 downlink/playback
  恢复路径提供稳定的单一 dialog 真相源

## Step 5.410
- dialog runtime 继续把 turn/session/terminal metadata 从 exported snapshot 中剥离：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 新增内部真相载体：
  - `river_dialog_runtime_cloud_session_facts_t`
  - `g_river_dialog_runtime.cloud_session_facts`
- 新增 `river_dialog_runtime_export_session_facts_to_snapshot_locked(...)`，统一把：
  - `turn_accepted`
  - `barge_in_enabled_*`
  - `provider/session/turn/accept_reason`
  - `playback_terminal_* reason`
  从 internal session facts 镜像到 exported snapshot
- `river_dialog_runtime_apply_cloud_event_locked(...)` 里的 `sid` 更新现在改为写入
  internal `cloud_session_facts`，不再直接修改 exported snapshot
- `river_dialog_runtime_import_cloud_snapshot_locked(...)` 现在先写 internal
  session facts，再统一导出到 snapshot
- 这一步继续把 dialog runtime 推进成唯一真相源，进一步减少
  “修改 exported snapshot 就是在修改内部真相” 的残留路径，为后续继续剥离
  boot/wake/asr/error 等 residual state 做准备

## Step 5.409
- dialog runtime 继续把 interaction/raw facts 从 exported snapshot 中剥离：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 新增内部真相载体：
  - `river_dialog_runtime_cloud_round_facts_t`
  - `river_dialog_runtime_cloud_io_facts_t`
  - `g_river_dialog_runtime.cloud_round_facts`
  - `g_river_dialog_runtime.cloud_io_facts`
- 新增导出镜像辅助层：
  - `river_dialog_runtime_export_round_facts_to_snapshot_locked(...)`
  - `river_dialog_runtime_export_io_facts_to_snapshot_locked(...)`
- `river_dialog_runtime_capture_interaction_projection_locked(...)` 现在改为直接读取：
  - internal `cloud_round_facts`
  - internal `cloud_io_facts`
  不再从 `snapshot.conversation/window/cloud_*` 和 `snapshot.input/output_lane`
  原始字段散读
- `river_dialog_runtime_capture_playback_projection_locked(...)` 的
  `output_lane` 现在也改为读取 internal `cloud_io_facts`
- `river_dialog_runtime_import_cloud_snapshot_locked(...)` 现在先写：
  - internal `cloud_round_facts`
  - internal `cloud_io_facts`
  然后统一镜像到 exported snapshot
- 这一步继续把 dialog runtime 推进成唯一真相源，进一步明确
  “内部 round/io raw facts” 与 “对外 snapshot export” 的分层，为后续继续剥离
  turn/session metadata 与其他 raw facts 做准备

## Step 5.408
- dialog runtime 开始把内部 playback raw facts 从导出 snapshot 中剥离：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 新增内部真相载体：
  - `river_dialog_runtime_cloud_playback_facts_t`
  - `g_river_dialog_runtime.cloud_playback_facts`
- 新增 `river_dialog_runtime_export_playback_facts_to_snapshot_locked(...)`，明确把
  playback raw facts 导出到对外 snapshot 作为镜像，而不是继续把 snapshot 本身当作
  内部 raw truth
- `river_dialog_runtime_capture_playback_projection_locked(...)` 现在改为直接读取
  internal `cloud_playback_facts`，不再从 `snapshot.playback_*` 原始字段散读
- `river_dialog_runtime_import_cloud_snapshot_locked(...)` 现在会先写入
  `cloud_playback_facts`，再统一镜像到 exported snapshot
- 这一步继续把 dialog runtime 推进成唯一真相源，开始把
  “内部 raw playback facts” 与 “对外 snapshot export” 明确拆层，为后续继续剥离
  turn/input/output raw facts 做准备

## Step 5.407
- dialog runtime 继续把入口层收口成统一的 dialog-owned ingress reducer：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 新增内部载体：
  - `river_dialog_runtime_local_playback_import_t`
  - `river_dialog_runtime_ingress_t`
- 新增 `river_dialog_runtime_capture_local_playback_import(...)`，把 playback listener
  的 `state + config` 先复制为 dialog 自己拥有的 local import 载体，再进入 reducer
- 新增统一提交器：
  - `river_dialog_runtime_commit_ingress(...)`
  - `river_dialog_runtime_ingress_default_reason(...)`
  - `river_dialog_runtime_prepare_local_playback_import_locked(...)`
  - `river_dialog_runtime_apply_local_playback_import_locked(...)`
- 以下入口现在都只负责组装 dialog-owned ingress，再走同一条提交路径：
  - `river_dialog_runtime_commit_cloud_event(...)`
  - `river_dialog_runtime_sync_cloud_state(...)`
  - `river_dialog_runtime_reduce_local_playback_event(...)`
- 统一 ingress 仍保持原有关键顺序不变：
  - cloud event 先 apply，再 import cloud facts
  - local playback 先 resolve ownership，再 import cloud facts，再 apply local shadow
- 这一步继续把 dialog runtime 推进成唯一 dialog 真相源，进一步切断 callback /
  adapter 配置对象对 reducer 提交面的直接影响，为后续继续压缩 raw-fact /
  derived-truth 边界做准备

## Step 5.406
- dialog runtime 继续把 cloud ingress 从直接消费 adapter snapshot 收口成
  dialog-owned import carrier：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 新增内部类型：
  - `river_dialog_runtime_cloud_import_t`
- 新增 `river_dialog_runtime_capture_cloud_import(...)`，由它在 dialog runtime 边界内
  吸收 `river_cloud_runtime_snapshot_t`，并复制为 dialog 自己拥有的 import 载体
- `import_cloud_snapshot_locked(...)` 现在改为只消费
  `river_dialog_runtime_cloud_import_t`，不再把
  `river_cloud_runtime_snapshot_t` 直接作为 reducer 输入
- 以下 ingress 入口现在统一改为：
  - 先 `capture_cloud_import(...)`
  - 再 `import_cloud_snapshot_locked(...)`
  - 再 `reconcile_facts_locked(...)`
  - 最后 `finalize_commit_locked(...)`
  - `river_dialog_runtime_commit_cloud_event(...)`
  - `river_dialog_runtime_sync_cloud_state(...)`
  - `river_dialog_runtime_reduce_local_playback_event(...)`
- 这一步继续把 dialog runtime 推进成唯一 dialog 真相源，切断 reducer 对
  cloud adapter export snapshot 结构的直接依赖，为下一步继续压缩 ingress typed
  reducer / import pipeline 做准备

## Step 5.405
- dialog runtime 继续把 cloud snapshot 路径从“import + reconcile 混合 helper”
  拆成显式两阶段：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `apply_cloud_snapshot_locked(...)` 已拆分为：
  - `import_cloud_snapshot_locked(...)`
  - `reconcile_facts_locked(...)`
- `import_cloud_snapshot_locked(...)` 现在只负责导入 cloud raw facts：
  - cloud runtime availability
  - round/window/playback raw fields
  - input/output lane text 与解析后的 lane
- `reconcile_facts_locked(...)` 现在统一负责 dialog 侧收敛：
  - clear local playback error shadow when cloud runtime is available
  - refresh error truth
  - latch cloud-round-backed `asr_session_active`
  - refresh playback truth
  - clear `tts_interrupt_requested` when output turn is quiesced
- `commit_cloud_event(...)` / `sync_cloud_state(...)` /
  `reduce_local_playback_event(...)` 现在都显式走：
  - import facts
  - reconcile facts
  - finalize commit
- 这一步继续把 dialog runtime 入口从“抓 cloud + 合并 truth + publish”推进成
  更清楚的 reducer 管线，为下一步继续把 raw cloud snapshot 包装成更明确的
  dialog import 载体做准备

## Step 5.404
- dialog runtime 为 cloud event / cloud sync / local playback reducer 新增统一的
  `commit checkpoint + commit policy` 提交边界：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 新增内部类型：
  - `river_dialog_runtime_commit_policy_t`
  - `river_dialog_runtime_commit_checkpoint_t`
- 新增统一辅助层：
  - `capture_commit_checkpoint_locked(...)`
  - `commit_checkpoint_changed(...)`
  - `finalize_commit_locked(...)`
- `river_dialog_runtime_commit_cloud_event(...)` 与
  `river_dialog_runtime_sync_cloud_state(...)` 现在都通过同一
  `finalize_commit_locked(...)` 完成提交，而不再直接各自调用
  `publish_locked(...)`
- `river_dialog_runtime_reduce_local_playback_event(...)` 不再手工保存：
  - `prev_playback_active`
  - `prev_playback_recovering`
  - `prev_error_recovering`
  - `prev_interaction_state`
  并在函数尾部拼接一段专属 publish gating
- 这一步把 dialog runtime 从“各入口各自判断是否 publish”继续推进成
  “同一 reducer/commit 边界 -> 同一提交策略”，为下一步继续拆
  cloud import / reducer / publish 分层做准备

## Step 5.403
- dialog runtime 继续为输入侧 / 会话侧派生引入内部
  `river_dialog_runtime_interaction_projection_t`：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
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

## Step 5.402
- dialog runtime 现在为 playback 派生引入内部
  `river_dialog_runtime_playback_projection_t`：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
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
  收口成“单次 projection -> playback/output truth”

## Step 5.401
- XiaoZhi playback runtime 继续把 control-path 收口到显式 `playback_truth_view`：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 以下 control-path 边界现在都改为先捕获 truth-view，再消费同一份
  `backend/phase/hold/output_active` 真相：
  - `apply_transport_reset_playback_policy()`
  - `apply_session_start_playback_policy()`
  - `playback_note_duplex_ready()`
  - `reset_playback_state()`
  - `playback_check_pending_stop()`
  - `playback_abort_for_cause()`
  - `start_playback_if_needed()`
- `playback_abort` 日志现在也直接打印 truth-view 里的 `phase/hold/backend`
- 这一步继续减少 control-path 与 worker-path 对 playback backend 的分叉判定

## Step 5.400
- XiaoZhi playback runtime 继续把观测面收口到显式 `playback_truth_view`：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `playback_truth_view` 现在额外派生 `hold_kind`，使执行面和观测面可以共用同一份
  phase/backend/hold/supply 真相
- `river_cloud_xiaozhi_dump_playback_status()` 现在改为一次性捕获 truth-view，并在
  `playback_terminal` / `downlink` 诊断日志里复用：
  - `phase`
  - `hold`
  - `backend`
  - `supply`
- `river_cloud_xiaozhi_fill_playback_runtime_snapshot()` 现在也直接消费同一份
  truth-view，而不再单独重建 backend-source
- 这一步继续减少“执行面按一套 truth 决策、诊断面按另一套 helper 读取”的漂移

## Step 5.399
- XiaoZhi playback runtime 继续把 downlink worker 的核心分支收口到显式
  `playback_truth_view`：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `river_cloud_xiaozhi_maybe_resume_paused_playback()` 现在改为直接消费
  已捕获的 truth-view，而不再在函数内部重新读取 `phase/backend`
- downlink worker 主循环在单次决策窗口内开始复用同一份 truth-view，用于：
  - `rebuffer_resume_ready`
  - `tts_stop_pending` 分支
  - `paused -> resume`
  - `needs_start / recovering / start_threshold`
- `playback write failed -> rebuffer` 路径现在也改为复用同一份 truth-view 来驱动：
  - `supply_kind`
  - `phase/backend` 诊断日志
  - recovery path 选择
- 这一步继续把 worker 从“循环内多 helper 重读”推进成
  “单次 worker decision -> single truth-view”，减少一次循环里 backend/supply
  读偏斜导致的恢复抖动

## Step 5.398
- XiaoZhi playback runtime 现在为 recovery 关键分支引入显式
  `river_cloud_xiaozhi_playback_truth_view_t`：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增 `river_cloud_xiaozhi_capture_playback_truth_view(...)`，一次性锁存并派生：
  - `backend_source`
  - `supply_source`
  - `backend_state`
  - `supply_kind`
  - `output_active`
- `river_cloud_xiaozhi_maybe_pause_for_segment_gap()` 与
  `river_cloud_xiaozhi_maybe_rebuffer_starved()` 现在都开始复用同一份
  truth-view，而不再各自分别重读 `phase` / `backend` / `supply`
- `segment_gap_hold` 关键日志也改成复用 truth-view 里的 `phase/backend`
- 这一步不改变 hold / starved-rebuffer 的触发语义，只继续把 recovery
  分支的判定边界收口成“显式 truth-view -> recovery decision”的单向派生，
  减少 worker loop 内部的组合读偏斜

## Step 5.397
- XiaoZhi playback runtime 现在为 supply 判定引入显式
  `river_cloud_xiaozhi_playback_supply_source_t`：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增 `river_cloud_xiaozhi_capture_playback_supply_source(...)`，一次性锁存：
  - `wait_context_valid`
  - `last_segment_observed`
  - `segment_count`
- `river_cloud_xiaozhi_compute_playback_waiting_next_segment_from_source(...)` 与
  `river_cloud_xiaozhi_compute_playback_supply_kind_from_source(...)` 现在只消费
  这份 source，不再在 supply 判定里分别重读 wait-context / segment_count /
  last-segment terminal context
- 以下关键读取点开始复用同一份 supply-source：
  - `river_cloud_xiaozhi_playback_waiting_next_segment()`
  - `river_cloud_xiaozhi_playback_supply_kind()`
  - `river_cloud_xiaozhi_capture_playback_phase_source()`
- 这一步不改变 `CURRENT_SEGMENT / WAITING_NEXT_SEGMENT / TERMINAL_TAIL`
  的判定语义，只继续把 playback supply truth 收口成
  “显式 supply source -> waiting/supply truth”的单向派生，并减少 phase/supply
  双向反推里的读偏斜

## Step 5.396
- XiaoZhi playback runtime 现在为 backend 派生引入显式
  `river_cloud_xiaozhi_playback_backend_source_t`：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增 `river_cloud_xiaozhi_capture_playback_backend_source(...)`，一次性锁存：
  - `service_view.state`
  - `service_view.active`
  - `service_view.owned_stream`
  - `phase`
- `river_cloud_xiaozhi_compute_playback_backend_state_from_source(...)` 现在只消费
  这份 source，不再在 backend 派生过程中分别读取 playback service stats 与
  phase
- 以下关键读取点开始复用同一份 backend-source：
  - `river_cloud_xiaozhi_playback_backend_state()`
  - `river_cloud_xiaozhi_playback_output_active()`
  - `river_cloud_xiaozhi_playback_hold_kind()`
  - `river_cloud_xiaozhi_fill_playback_runtime_snapshot()`
- `tts_start` / `playback_started` fallback 关键日志也改成复用同一份
  phase/backend source，避免日志里 phase 与 backend 各自单独重读
- 这一步不改变 backend / output-active / hold 的判定语义，只继续把
  playback backend 的真相边界收口成“显式 backend source -> backend truth”的
  单向派生，并减少 phase/backend 双读导致的观测偏斜

## Step 5.395
- XiaoZhi playback runtime 现在为 phase 派生引入显式
  `river_cloud_xiaozhi_playback_phase_source_t`：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增 `river_cloud_xiaozhi_capture_playback_phase_source(...)`，一次性锁存：
  - `stop_pending`
  - `rebuffer_pending`
  - `physical_active`
  - `queued_frames`
  - `segment_count`
  - `waiting_next_segment`
- `river_cloud_xiaozhi_compute_playback_phase_from_source(...)` 现在只消费这份
  source，不再在 phase 派生过程中散落读取全局态
- `river_cloud_xiaozhi_refresh_playback_phase(...)` 也开始复用同一份 source 打日志，
  观测面新增：
  - `segments`
  - `wait_next`
- 这一步不改变 `PLAYING/PREFETCHING/WAITING_SEGMENT/...` 的判定结论，只把
  playback phase 的真相边界继续收口到“显式 physical + queue/segment source ->
  phase”的单向派生

## Step 5.394
- XiaoZhi playback runtime 现在把本文件内 residual 的
  `g_river_cloud.xiaozhi_playback_active` 读取收口到
  `river_cloud_xiaozhi_playback_physical_active()`：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `compute_playback_phase()` 与 `river_cloud_xiaozhi_arm_playback_stop()` 现在都显式消费
 这份 named physical truth，而不再裸读 shadow 成员
- `playback phase`、`interrupt hint`、`hint-only endpoint`、`duplex dump`
 诊断日志现在把字段明确命名为：
  - `playback_physical`
  并补齐：
  - `phase`
  - `backend`
- 这一步不改变 phase / backend 判定逻辑本身，只把 playback runtime 内“物理播放中”
  和“语义播放占用中”继续拆开命名，减少板端继续把单个 `playback=yes/no`
  日志字段误当成 semantic truth

## Step 5.393
- XiaoZhi playback runtime 的两处策略层判断现在不再直接依赖
  `g_river_cloud.xiaozhi_playback_active` 影子布尔：
  - `river_cloud_xiaozhi_playback_has_work()`
  - `river_cloud_xiaozhi_segment_prefetch_target_needed()`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 两处现在统一改为消费
  `river_cloud_xiaozhi_playback_output_active()`
- 这意味着：
  - downlink task 的唤醒条件
  - segment predictive prefetch 的抑制条件
  都开始跟随 runtime `phase + backend` typed truth，而不是继续直接使用局部
  shadow bool
- 这一步继续把 playback runtime 的策略层从 coarse active shadow 收口到
  semantic output-active truth，减少 foreign/recovering/non-owned backend
  窗口对本流策略判断的污染

## Step 5.392
- XiaoZhi playback runtime 的两个 recovery 分支现在不再直接依赖
  `g_river_cloud.xiaozhi_playback_active` 影子布尔：
  - `river_cloud_xiaozhi_maybe_pause_for_segment_gap()`
  - `river_cloud_xiaozhi_maybe_rebuffer_starved()`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 两处 guard 现在统一改为消费
  `river_cloud_xiaozhi_playback_output_active()`
- 这意味着段间 hold 与 starvation rebuffer 的触发前提开始直接跟随：
  - runtime `phase`
  - typed `backend_state`
  而不再继续跟随局部 `playback_active` shadow
- 这一步继续把 downlink/playback recovery 判定从 coarse bool 收口到
  runtime-owned semantic truth，减少恢复空窗里因影子位抖动导致的误跳过 /
  误清理恢复路径

## Step 5.391
- `voice runtime` 的 `restart_pending` hard block 现在优先消费
  `dialog runtime` 的 typed backend truth：
  - [components/river_voice/river_voice_runtime_policy.c](/root/ameba-river/components/river_voice/river_voice_runtime_policy.c)
- `river_voice_runtime_restart_pending_requires_block(...)` 新增 raw
  `playback_state` 兜底参数：
  - 拿不到 dialog snapshot 时，仍退回
    `playback_state == RIVER_PLAYBACK_RESTART_PENDING`
  - 一旦拿到 dialog snapshot，就直接按 snapshot 的：
    - cloud owner
    - backend=`restart_pending`
    - quiet-phase 过滤
    来决定是否继续 hard block
- 这一步继续把 AEC `restart_pending` gate 从“先看物理 service state 再看语义”
  收口到“以 dialog/backend typed truth 为主，raw state 仅做缺失兜底”

## Step 5.390
- `voice runtime` 在 `uses_native_capture_ref` 且 native reference 尚未可用时，
  不再回退依赖 raw `playback_state_active()` 去区分 `ref_idle/ref_missing`：
  - [components/river_voice/river_voice_runtime_policy.c](/root/ameba-river/components/river_voice/river_voice_runtime_policy.c)
- 现在只要前面的 AEC playback gate 已经接受当前路径，就统一把这类 native-ref
  空窗解释成：
  - `RIVER_VOICE_REFERENCE_ACTIVITY_IDLE`
- 这一步不改变 duplex ready 的 ready/not-ready 结论，只收紧 reference
  activity 的语义边界，避免在评估尾部再次从 raw playback service state
  反推语义

## Step 5.389
- `voice runtime` 的 AEC/duplex 评估结构现在显式携带 `dialog runtime`
  导出的 typed truth：
  - `dialog_playback_owner_kind`
  - `dialog_error_kind`
  - [include/river/river_voice_runtime_policy.h](/root/ameba-river/include/river/river_voice_runtime_policy.h)
  - [components/river_voice/river_voice_runtime_policy.c](/root/ameba-river/components/river_voice/river_voice_runtime_policy.c)
- `river_voice_runtime_aec_gate_eval_base(...)` 现在在抓取 dialog snapshot 时同步锁存：
  - `playback_owner_kind`
  - `error_kind`
  并继续透传到 `river_voice_runtime_duplex_ready_eval(...)`
- `preproc` 的 gate-transition 诊断日志现在直接打印：
  - raw `playback_state`
  - typed `playback_owner_kind`
  - typed `error_kind`
  - [components/river_voice/river_voice_preproc_fixed_dsb.c](/root/ameba-river/components/river_voice/river_voice_preproc_fixed_dsb.c)
- XiaoZhi duplex/fallback 关键日志也同步补上了相同三元组：
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 这一步先不改 AEC/duplex 判定逻辑本身，只把诊断面与 `dialog runtime`
  typed truth 对齐，减少板端继续依赖粗粒度 playback 布尔反推语义归属

## Step 5.388
- voice runtime 现在开始显式消费 `dialog runtime` 导出的
  `playback_owner_kind`：
  - [components/river_voice/river_voice_runtime_policy.c](/root/ameba-river/components/river_voice/river_voice_runtime_policy.c)
- `dialog snapshot` 兜底保持 playback-engaged 的 helper 现在明确只接受：
  - `playback_owner_kind == CLOUD`
  - 且仍有：
    - `playback_lane_engaged`
    - 或 `playback_recovering`
    - 或 `playback_turn_active`
- dedicated `restart_pending` hard block 与 generic dialog-playback fallback
  现在都复用这条显式 cloud-owner 判定
- 本地 `river_playback_service_state()` 仍保留为“物理播放是否正在发生”的底层真相；
  这一步只把 dialog snapshot fallback 从隐式布尔组合推进成 typed ownership
  消费

## Step 5.387
- `dialog runtime` 现在正式把 playback ownership truth 外显成 snapshot 级别的
  `playback_owner_kind`：
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 新增 `river_dialog_playback_owner_kind_t`：
  - `NONE`
  - `CLOUD`
  - `LOCAL_FALLBACK`
- `refresh_playback_locked()` 现在统一同时派生：
  - `snapshot.playback_active`
  - `snapshot.playback_recovering`
  - `snapshot.playback_owner_kind`
- `dialog_runtime_dump_status()` 的播放观测面现在新增：
  - `owner=<kind>`
- 这一步继续把 `dialog runtime` 从“靠外部反推当前是谁在驱动 playback
  truth”推进到“直接导出 typed playback ownership truth”

## Step 5.386
- `dialog runtime` 现在正式把 typed error source 外显成 snapshot 级别的
  `error_kind`：
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 新增 `river_dialog_error_kind_t`：
  - `NONE`
  - `ASR`
  - `LOCAL_PLAYBACK`
  - `MIXED`
- `refresh_error_recovering_locked()` 现在统一同时派生：
  - `snapshot.error_recovering`
  - `snapshot.error_kind`
- `dialog_runtime_dump_status()` 的错误观测面现在从：
  - `error=yes/no`
  变成：
  - `error=yes/no/<kind>`
- 这一步继续把 `dialog runtime` 从“只导出聚合 bool”推进到
  “导出可消费的 typed error truth”

## Step 5.385
- `dialog runtime` 现在只会在 local playback event 已确认属于当前
  dialog-owned stream 时，才在 `IDLE` 上清理
  `local_playback_stream_owned/name`：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- owner 清理从 `should_absorb` 判断之前，移动到了判断之后
- 这意味着 foreign playback stream 的 `IDLE` 事件不再先把 dialog runtime
  当前 owned stream tracking 擦掉
- 这一步继续把 `dialog runtime` 对 local playback ownership 的归属边界收紧到
  “只消费本 dialog stream 的事件”

## Step 5.384
- `dialog runtime` 现在把内部 `error_recovering` 进一步拆成了两路 typed source：
  - `asr_error_recovering`
  - `local_playback_error_recovering`
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 新增 `refresh_error_recovering_locked()`，对外暴露的
  `snapshot.error_recovering` 不再由多个路径直接各自覆盖，而是统一由这两路
  source 聚合
- cloud snapshot 一旦重新可用，会主动清掉
  `local_playback_error_recovering` shadow，避免本地 playback fallback 遗留的
  error 状态继续挂在 dialog runtime 真相上
- 这意味着：
  - ASR error 不再被 local playback `RUNNING/IDLE` 之类事件误清
  - local playback fallback 抬起的 error，也不再在 cloud truth 恢复后继续残留
- 这一步继续把 `dialog runtime` 从 coarse `error_recovering` 单 bool
  改造成 typed internal truth，再向外派生单一交互态

## Step 5.383
- `dialog runtime` 现在进一步收紧 local playback shadow 对真相的写权限：
  - 新增 `local_playback_shadow_drives_truth_locked()`
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 当 cloud runtime 已可用时，local playback event 现在仍会更新本地诊断 shadow，
  但不再继续直接：
  - `refresh_playback`
  - 改写 `error_recovering`
  - 清理 `tts_interrupt_requested`
- 这意味着本地 playback `RUNNING/IDLE/ERROR` 事件不再在 cloud/dialog 真相已在场时：
  - 把 `error_recovering` 从其他来源误清掉
  - 或在 cloud recovery truth 尚未同步前，先把交互态误推成
    `error_recovering`
- 这一步继续把 `dialog runtime` 从“吸收 local playback 事件后立即重派生真相”
  收口到“cloud truth 优先，本地 playback 仅在 cloud 不可用时兜底”

## Step 5.382
- voice runtime 的 generic playback AEC gate 现在也会消费 dialog runtime
  的 playback-lane truth，而不再只看本地 playback-service `active` 状态：
  - [components/river_voice/river_voice_runtime_policy.c](/root/ameba-river/components/river_voice/river_voice_runtime_policy.c)
- 新增：
  - `dialog_snapshot_capture(...)`
  - `dialog_playback_lane_engaged(...)`
- 当本地 playback-service 暂时掉到 inactive，但 dialog runtime 仍明确给出：
  - `playback_lane_engaged`
  - 或 `playback_recovering`
  - 或 `playback_turn_active`
  时，voice runtime 不再立刻落回 `RIVER_VOICE_AEC_GATE_BLOCKED_PLAYBACK`
- `restart_pending` 的专用 gate 也同步复用同一份 dialog snapshot，而不是再次
  单独抓取一份局部状态
- 这一步继续把 duplex/AEC 的“播放是否仍占用中”收口到 dialog runtime 真相源，
  降低 local playback-service 短暂 inactive 对 AEC path 的误 reset / reopen

## Step 5.381
- XiaoZhi playback backend truth 现在进一步受 stream ownership 约束：
  - 只有 owned stream 的 `RIVER_PLAYBACK_RESTART_PENDING` 才会被映射成
    `RIVER_CLOUD_PLAYBACK_BACKEND_RESTART_PENDING`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `playback_backend_state()` 不再在 ownership 判定之前就无条件吞掉
  `RESTART_PENDING`
- 这意味着：
  - foreign stream 的 restart/recover 不会再污染 XiaoZhi 自己的 backend truth
  - backend=`restart_pending` 继续只表示“本流仍保有 restart 语义”
- 这一步继续把 downlink/playback runtime 的 backend 真相从底层
  playback-service coarse state 收回到“owned stream + typed backend”语义

## Step 5.380
- voice runtime 的 `restart_pending` AEC gate 不再无条件依赖本地
  `playback_state`：
  - 改为先读取 dialog runtime snapshot，再决定是否仍需按
    `restart_pending` 硬阻塞
  - [components/river_voice/river_voice_runtime_policy.c](/root/ameba-river/components/river_voice/river_voice_runtime_policy.c)
- 新增两层 helper：
  - `restart_pending_quiet_phase(...)`
  - `restart_pending_requires_block()`
- 当前只有在 dialog/runtime 真相同时满足以下条件时，才继续保留
  `RIVER_VOICE_AEC_GATE_BLOCKED_PLAYBACK_RESTART_PENDING`：
  - playback lane 仍 engaged
  - backend truth 仍是 `RESTART_PENDING`
  - 且不处于 `prefetching/rebuffering/waiting_segment` 这类 quiet recovery 窗口
- 如果 dialog/runtime 已经判定：
  - lane 不再 engaged
  - 或已经进入 quiet recovery window
  voice runtime 就不再提前把 AEC path 直接打成 `restart_pending` hard block，
  而是继续下沉到后续 reference / interaction gate 判定
- 这一步继续把 duplex/AEC 的恢复门控从底层 playback-service coarse state
  收回到 dialog/runtime 真相源，减少恢复空窗内的 AEC reset / reopen churn

## Step 5.379
- playback runtime 现在对 backend-aware 起播门限做了进一步拆分：
  - detached cold start
  - restart-pending restart
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增：
  - `river_cloud_xiaozhi_downlink_start_threshold_for_backend(...)`
- `downlink_task()` 在以下场景不再一律复用 cold-start `start_frames`：
  - backend 真正 detached 的 fresh start
  - backend=`RIVER_CLOUD_PLAYBACK_BACKEND_RESTART_PENDING` 的 restart
- `restart_pending` 重新起播现在直接复用 attached-resume 门限：
  - `min(start_frames, buffer_frames)`
  - 不再在已经保有 turn / queued audio 语义时退回到更保守的 cold start 等待
- 这一步继续把 playback runtime 的启动决策从 coarse backend state 拆成
  runtime-owned typed start policy，减少 recover/restart 后再次冷启动式排队

## Step 5.378
- playback runtime 现在显式区分：
  - backend 仍保有 turn / restart 语义
  - backend 底层 stream 是否真的 attached
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增更精确的 stream-attachment 语义：
  - `river_cloud_xiaozhi_playback_backend_stream_attached(...)`
  - `RIVER_CLOUD_PLAYBACK_BACKEND_RESTART_PENDING` 不再被当成 attached stream
- 以下边界不再对 `restart_pending` 误用 attached stop/flush 语义：
  - `transport_reset`
  - `session_start`
  - `segment_gap_hold`
  - `playback_abort`
  - `playback_check_pending_stop`
- `pending_stop` 现在会把 `restart_pending` 和其他 non-attached backend
  统一按“非 attached queued audio”处理：
  - 直接走 queue drop / terminal ack / runtime reset
  - 不再等 drain deadline 后再对已脱离硬件的 backend 重复 stop 一次
- 这一步继续把 downlink/playback 恢复真相从 coarse service state 中拆开，
  避免 `restart_pending` 把 detached backend 重新污染成 attached stop/start 语义

## Step 5.377
- downlink/playback runtime 现在显式区分：
  - cold start threshold
  - attached resume threshold
  - actual playback buffer budget
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- playback runtime 新增 runtime-owned `xiaozhi_playback_buffer_frames`：
  - start 时锁存本次真正下发给 playback service 的 `buffer_frames`
  - reset / meta clear 时同步清零
- 新增：
  - `playback_buffer_frame_budget()`
  - `downlink_attached_resume_threshold_frames()`
- attached hold / rebuffer resume 不再一律复用 cold start 的
  `start_frames`：
  - `rebuffer_resume_ready()`
  - `maybe_resume_paused_playback()`
  现在统一使用 `min(start_frames, buffer_frames)`
- status / resume 日志现在会直接暴露：
  - `start`
  - `resume`
  - `buffer`
- 这一步继续把 downlink/playback 恢复语义从“保守冷启动”拆成“runtime-owned
  attached resume 真相”，降低 attached hold/recover 恢复时不必要的排队等待

## Step 5.376
- playback rebuffer 恢复决策现在统一收口到：
  - `river_cloud_xiaozhi_request_playback_rebuffer_recovery(...)`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `rebuffer_prefers_service_recover(...)` 的 attached recover-first 适用范围扩大到：
  - `WRITE_FAILED`
  - `UPSTREAM_STARVED + CURRENT_SEGMENT`
  - `UPSTREAM_STARVED + WAITING_NEXT_SEGMENT`
- `maybe_rebuffer_starved()` 不再默认先走 detached `stop_rebuffer`：
  - 当前段内断流也会优先尝试 attached `service_recover`
  - 只有 recover 失败时才回退到 stop
- `write_failed` 与 `upstream gap` 两条恢复路径现在共用同一套恢复选择和回退日志
- 这一步继续削减 `underrun/write_failed/starved -> stop/start` 风暴，把 residual
  的 current-segment starvation 也推向 attached recovery-first

## Step 5.375
- playback runtime / dialog runtime 新增 typed playback hold truth：
  - [include/river/river_cloud.h](/root/ameba-river/include/river/river_cloud.h)
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 新增 `river_cloud_playback_hold_kind_t`，当前先落一类 runtime-owned hold：
  - `RIVER_CLOUD_PLAYBACK_HOLD_SEGMENT_GAP`
- cloud snapshot 现在会显式导出 `playback_hold_kind`：
  - `owned_paused + waiting_segment + !rebuffer_pending`
    不再只能让 core 从 backend state 里猜测
- dialog runtime 现在不再把 `OWNED_PAUSED` 一律解释成 `playback_recovering`：
  - `segment-gap attached hold` 改由独立的 hold truth 表达
  - playback active / output-turn continuity 继续保留，但 recovering/error 语义不再被
    正常段间 hold 污染
- cloud / dialog dump 也新增 `hold=` 观测面，便于板端确认当前 paused backend
  究竟是：
  - 正常 segment-gap hold
  - 还是其他恢复路径

## Step 5.374
- `segment_gap_pause` 现在优先走 attached flush-hold，而不是默认 detached stop：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增：
  - `river_cloud_xiaozhi_hold_playback_for_segment_gap(...)`
  - `river_cloud_xiaozhi_maybe_resume_paused_playback(...)`
- 当前进入 `waiting_next_segment` 且 queue 见底时：
  - 先尝试 `flush_stream_ex("xiaozhi_segment_gap_pause")`
  - flush 失败才回退到 detached stop
- downlink worker 现在会在：
  - backend=`owned_paused`
  - refill `>= start_frames`
  时显式 resume 本地 playback runtime，而不是一直空等
- transport reset / session start 也会先 stop 已 attach 的 owned backend，
  避免 segment-gap attached pause 在 reset 边界遗留悬挂 backend
- 这一步开始把 segment-gap 从 detached stop/start 模型推进到 attached
  hold/resume 模型，继续削减段间晚到导致的 backend restart 抖动

## Step 5.373
- `maybe_rebuffer_starved()` 不再在 `WAITING_NEXT_SEGMENT` 窗口里提前触发
  starved rebuffer：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 当 runtime-owned supply truth 已明确是 `waiting_next_segment` 时：
  - 不再因为 low-water / wait-ms 达标就提前走
    `xiaozhi_playback_starved -> stop/rebuffer`
  - 改为清掉 starvation watch，继续等 segment-gap 路径接手
- 这一步继续把“段间晚到”从 generic upstream-starved rebuffer 中拆出去，
  减少当前段尾部低水位时被提前升级成 stop/start 风暴

## Step 5.372
- `write_failed` 路径现在会把 runtime-owned supply truth 一起带入恢复决策：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 当一次 `write_failed` 被归类为 `UPSTREAM_STARVED`，但当前 supply 已明确是
  `WAITING_NEXT_SEGMENT` 时：
  - 不再默认走 detached `stop_rebuffer`
  - 改为优先 attached `service_recover`
- 新增 `playback_supply_kind_name()`，日志现在会直接打印这次 `write_failed`
  所处的 supply kind，便于把：
  - 当前段内断流
  - 段间晚到
  - terminal tail
  区分开
- 这一步继续把 `underrun/write_failed -> stop/start` 风暴里的“段间晚到”从
  统一 stop/restart 模型中拆出去，优先尝试 attached recovery，减少 backend
  detach/restart 抖动

## Step 5.371
- `audio.out.completed` ACK 现在彻底绑定到 terminal last-segment lineage：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `river_cloud_xiaozhi_try_queue_playback_completed_ack()` 不再继续保留回退到
  “当前 playback meta 的 `response_id / playback_id`” 这条旧路径：
  - terminal/completed 现在只消费 runtime-owned 的
    `last_segment_response_id / last_segment_playback_id`
- playback dump 新增 terminal-context 观测日志：
  - 现在可以直接看到 completed 准备消费的：
    - `response_id`
    - `playback_id`
    - `segment_id`
- 这一步继续把 completed 终态从 current-meta shadow 收口到 terminal
  last-segment truth，减少 response/playback rollover 时 completed ACK 错绑到
  新上下文的风险

## Step 5.370
- XiaoZhi playback runtime 现在会把最后一个 fully-heard segment 的 response /
  playback / segment 上下文一起保存成 typed heard context：
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `river_cloud_xiaozhi_mark_segment_fully_heard(...)` 不再只保存
  `last_fully_heard_segment_id`：
  - 现在也同步保存：
    - `last_fully_heard_response_id`
    - `last_fully_heard_playback_id`
- `audio.out.cleared` ACK 不再继续拿当前 playback meta 的
  `response_id / playback_id` 去拼终态：
  - `river_cloud_xiaozhi_try_queue_playback_cleared_ack(...)` 现在直接消费
    fully-heard segment 自带的 typed context
- playback dump 新增 heard-context 观测日志，便于板端确认当前 clear / truncate
  将回报给服务侧的是哪条 playback lineage
- 这一步继续把 playback truth 链上的 cleared 终态从 current-meta shadow 收口到
  runtime-owned heard-segment truth，减少新 response / 新 meta 覆盖旧 context
  时的错 ACK 风险

## Step 5.369
- 已把 `/root/agent-server` 2026-04-21 主线语音进展回灌到当前设备侧计划：
  - [doc/VOICE_RUNTIME_REARCHITECTURE_EXECUTION_PLAN_ZH.md](/root/ameba-river/doc/VOICE_RUNTIME_REARCHITECTURE_EXECUTION_PLAN_ZH.md)
  - [doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md](/root/ameba-river/doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md)
- 主计划现已明确：
  - 服务侧主干能力不再是端侧剩余重构的主要阻塞前提
  - 端侧剩余优先级重排为：
    - downlink / playback 真相链与恢复模型
    - `dialog runtime` 唯一真相源收口
    - runtime-ready duplex / capture / AEC gate
    - 统一 turn timeline / 板端回归
    - duck-first / keep-listening 行为优化
- 全双工参考计划也已同步修正：
  - 服务侧 `S1`~`S4` 更应视为部署确认与预算收口项，而非“骨架未完成”的阻塞项
  - 当前设备侧排序不再以催促服务侧补主干能力为前提
- `.codex/active_context.md` 已同步当前计划结论，便于后续切片继续围绕这组新优先级推进

## Step 5.368
- XiaoZhi transport 侧已移除冗余的诊断 shadow `last_playback_meta_valid`：
  - [components/river_cloud/river_xiaozhi_ws.c](/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c)
- transport 诊断里的 playback meta `valid=` 现在不再缓存一个独立 bool：
  - 改为直接由 cached meta context 推导：
    - `last_response_id`
    - `last_playback_id`
    - `last_segment_id`
- `last_playback_is_last_segment` 继续仅表示最近一条 `audio.out.meta` 的
  event-local fact，不再与 `valid` shadow 混在一起构成“上一条 meta 是否有效”
  的粗粒度总开关
- 这一步继续把 transport 诊断层对 playback-meta validity 的解释权，从单独缓存
  的 shadow bool 收口到已有的 typed meta context

## Step 5.367
- XiaoZhi playback runtime 已移除残留的 coarse
  `xiaozhi_playback_meta_valid` shadow：
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- “当前 playback meta 是否有效” 现在直接由 typed segment context 提供：
  - `response_id`
  - `playback_id`
  - `segment_id`
- `playback_current_meta_is_last_segment()` 不再先看 `meta_valid` shadow：
  - 改为直接消费 `playback_segment_context_valid()`
  - 再与 stored terminal last-segment context 比较
- playback dump 中的 `valid=` 也不再读 shadow bool，而是实时打印 typed
  segment context 是否完整
- 这一步继续把 playback runtime 对“当前 meta 有效性”的解释权，从粗粒度
  shadow bool 收口到 runtime-owned response/playback/segment context

## Step 5.366
- cloud/dialog runtime snapshot 现在正式导出 `playback_turn_active`：
  - [include/river/river_cloud.h](/root/ameba-river/include/river/river_cloud.h)
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `dialog_runtime` 不再只拿 `playback_lane_engaged` 去猜 retained output-turn：
  - 新增 `playback_turn_retains_output_turn_locked()`
  - lane 仍 occupied 时，继续只让 `rebuffering` 保留 output ownership
  - lane 已释放但 runtime 仍声明 `playback_turn_active=yes` 时，只有
    `output_lane=speaking` 且没有进入 suppress-speaking 的 terminal wait，
    才继续保留 speaking/output-turn
- `output_turn_quiesced_locked()` 现在也显式要求
  `playback_turn_active=no`，避免 terminal 仍 open 的 retained-turn 窗口被过早
  视为真正 quiesced
- `dialog_runtime` 日志现在会同时打印：
  - `lane`
  - `turn`
- 这一步继续把 core 对 output-turn 生命周期的解释权，从 coarse lane occupancy
  收口到 runtime-owned typed playback-turn truth，减少 lane 已空但 turn 尚未真正
  结束时的误判窗口

## Step 5.365
- XiaoZhi playback runtime 已把 `playback_turn_active()` 的 retained-turn 判定从
  coarse `playback_meta_valid` shadow 收口到 typed response/terminal truth：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `playback_turn_active()` 现在只认：
  - playback lane 当前仍 engaged
  - 或 terminal 仍 open 且 response/playback context 有效
- `playback_note_meta()` 现在会在写入 queue / wait / terminal context 后立即刷新
  playback phase
- 这一步减少了：
  - `dialog runtime`
  - follow-up window
  - interrupt / abort policy
  对 `meta_valid` 这个粗粒度 shadow 的依赖，也缩短了首条 meta 到 phase
  snapshot 更新之间的 stale 窗口

## Step 5.364
- XiaoZhi playback runtime 已移除残留的 coarse global
  `xiaozhi_playback_last_segment` shadow bool：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- 新增 `playback_current_meta_is_last_segment()`：
  - 当前 meta 是否 terminal 不再靠单独缓存一个 runtime bool
  - 改为直接比较：
    - 当前 meta context
    - terminal last-segment context
- `playback_note_meta()` 不再写入全局 `last_segment` shadow：
  - queue 中的 `segment->is_last_segment` 直接取自当前 event
  - dump/diagnostic 中的 `is_last_segment` 直接从 typed terminal context 推导
- 这一步继续把 playback runtime 的 terminal 语义从“单独缓存的布尔影子位”
  收口到：
  - event-local segment fact
  - runtime-owned terminal context

## Step 5.363
- XiaoZhi playback runtime 已把 downlink starvation/tail 的判定从 coarse
  global `last_segment` shadow 中拆开，改成消费 runtime-owned typed supply
  truth：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增 typed supply helper：
  - `river_cloud_xiaozhi_playback_supply_kind_t`
  - `playback_supply_kind()`
  - `playback_supply_expects_more_audio(...)`
- `maybe_rebuffer_starved()` 现在只会在 runtime 仍明确期待更多 downlink audio 时，
  才继续进入 starved rebuffer：
  - `CURRENT_SEGMENT`
  - `WAITING_NEXT_SEGMENT`
- 当 runtime 已进入：
  - `TERMINAL_TAIL`
  - `SUPPLY_NONE`
  starvation watch 会被直接清掉，不再继续被“最近一条 meta 恰好是
  last/non-last”这个 coarse shadow 误驱动
- 这一步继续把 downlink/playback 的 tail/starved 判断从“最近 meta 阴影”收口到
  queue-head、waiting-context、terminal-context 这些 runtime-owned typed
  truth

## Step 5.362
- XiaoZhi playback runtime 已把 `WAITING_SEGMENT` 的判定从 coarse global
  meta shadow 中拆开，改成独立保存“待续段上下文”：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- 新增 runtime-owned waiting-segment context：
  - `xiaozhi_playback_wait_response_id`
  - `xiaozhi_playback_wait_playback_id`
  - `xiaozhi_playback_wait_segment_id`
  - `playback_wait_context_valid()`
  - `store_playback_wait_context()`
  - `clear_playback_wait_context()`
- `playback_waiting_next_segment()` 现在只认：
  - 已存在有效待续段上下文
  - 当前 segment queue 已空
  不再继续从：
  - `xiaozhi_playback_meta_valid`
  - `xiaozhi_playback_last_segment`
  这组 global shadow 重建 waiting 语义
- `playback_note_meta()` 现在会：
  - 在 non-terminal meta 上保存待续段上下文
  - 在 terminal meta 或无效 meta 上清掉待续段上下文
- 这一步继续把 playback phase/wait 的语义从“最近 meta 阴影”收口到
  runtime-owned typed waiting truth

## Step 5.361
- XiaoZhi playback start-gate 的 predictive segment-prefetch 判定已开始直接
  消费 queue 头段/当前待播 segment 真相：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增：
  - `playback_prefetch_target_segment()`
- `segment_prefetch_target_needed()` 不再继续读取 coarse global shadow：
  - `xiaozhi_playback_meta_valid`
  - `xiaozhi_playback_last_segment`
- predictive prefetch 现在只在这些条件下成立：
  - 当前并未真实 active playback
  - queue 头段/当前待播 segment 存在且有效
  - 该 segment 不是 terminal last-segment
  - 预取目标仍高于 baseline start budget
- 这一步继续把 start-gate 从“最近一条 meta 阴影”收口到 runtime-owned
  segment queue truth，减少 terminal meta 或 stale global shadow 对起播门限的
  干扰

## Step 5.360
- XiaoZhi playback runtime 已把 terminal/completed 对“最后一段”的判定从
  全局当前 meta shadow 中拆开，改成显式保存独立的 terminal segment
  context：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- 新增 runtime-owned terminal last-segment context：
  - `xiaozhi_playback_last_segment_response_id`
  - `xiaozhi_playback_last_segment_playback_id`
  - `xiaozhi_playback_last_segment_segment_id`
  - `store_playback_last_segment_context()`
- `playback_completed_ready()` / `playback_completed_wait_kind()` 现在只比较：
  - `last_fully_heard_segment_id`
  - `last_segment_segment_id`
  不再错误依赖“最近一条 meta 当前挂着的 `segment_id`”
- `try_queue_playback_completed_ack()` 现在也优先消费这份 terminal
  response/playback context，再回退到当前 response context
- 这一步继续把 playback runtime 的 terminal 真相从“全局最近 meta 阴影”收口到
  “独立保存的终段上下文”，减少 completed/尾段等待被后续 meta shadow 干扰

## Step 5.359
- XiaoZhi playback ACK / terminal 路径开始把 response-level 与 segment-level
  上下文显式拆开：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增：
  - `playback_response_context_valid()`
  - `playback_segment_context_valid()`
- `started/mark` ACK 现在直接消费当前 segment 自带的
  `response_id/playback_id/segment_id`，不再被全局 `meta_valid` 一起卡住
- `cleared/completed` terminal ACK 现在只要求 response-level 上下文存在：
  - `response_id`
  - `playback_id`
  不再错误依赖当前 `segment_id` 仍然挂在全局 meta 上
- `update_playback_ack_progress()` 与 `playback_finalize_cleared()` 也不再被
  `meta_valid` 这个 coarse 布尔位提前短路
- 这一步继续把 playback runtime 从“单个 `meta_valid` 阴影布尔位”收口到更细的
  typed ACK 上下文真相

## Step 5.358
- XiaoZhi playback runtime 现在把 `write_failed` 与 `upstream_starved` 两类
  rebuffer 原因分开选择恢复路径：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增 `rebuffer_prefers_service_recover()`：
  - 本地 `WRITE_FAILED` 优先走 attached `recover_stream`
  - `UPSTREAM_STARVED` 继续优先走 `stop_rebuffer` / fresh-start
- `write_failed` 分支的恢复顺序已调整为：
  - local write failure: `service_recover -> stop_rebuffer -> fresh_start`
  - upstream starved: `stop_rebuffer -> service_recover -> fresh_start`
- 这一步继续把：
  - 本地设备写链路抖动
  - 上游供给断档
  从同一套默认 `stop/start` 风暴里拆开，减少明明 backend 仍 attached 时的
  无谓 stop/restart

## Step 5.357
- XiaoZhi downlink worker 的 rebuffer resume gate 已前移到 backend 状态分支之前：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `rebuffer_resume_ready()` 现在会统一处理两类恢复路径：
  - backend 仍 attached 且 `owned_recovering` 时，直接 `finish_playback_rebuffer()`
  - backend 已 detached 时，保留 rebuffer pending，交给后续 fresh-start
- downlink loop 现在会在 rebuffer gate 之后重新抓取一次 backend truth，再决定：
  - pending-stop
  - paused/recovering wait
  - start/restart
- 这一步修正了一个结构性短路：
  - 以前 attached recovering backend 会在真正 finish rebuffer 前先被
    `OWNED_RECOVERING` 分支拦住
  - 现在 rebuffer gate 本身负责判定“继续等 / attached resume / detached
    fresh-start”

## Step 5.356
- XiaoZhi playback runtime 的 backend truth 不再在 `rebuffering` 期间无条件
  伪装成 `owned_recovering`：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `playback_backend_state()` 现在会继续如实反映 backend owner/attached truth：
  - playback service 已不活跃时返回 `detached`
  - 只有当当前 stream 仍归 XiaoZhi 持有时，`rebuffering` 才投影成
    `owned_recovering`
- 这一步修正了 rebuffer resume 的结构性误判：
  - downlink resume 路径不再因为 phase 仍是 `rebuffering`，就误以为 backend
    还在恢复中
  - stop-after-write-failed / starved-rebuffer 之后，runtime 现在可以重新识别
    “backend 已 detached，需要 fresh-start”
- 这继续把 playback runtime 的恢复语义拆成：
  - `phase/rebuffer_pending` 表示 playback turn 正处于恢复窗口
  - `backend_state` 只表示本地 backend 当前真实 attached/owner 状态

## Step 5.355
- XiaoZhi playback runtime 现在把“回合仍保留”和“downlink worker 仍有活要干”
  显式拆成两条真相：
  - `playback_turn_active()`
  - `playback_has_work()`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `playback_turn_active()` 继续表示：
  - playback lane 尚未回到 `idle`
  - 或仍保留有效 `audio.out.meta`
- `playback_has_work()` 现在只表示 downlink worker 的真实运行条件：
  - queued frames
  - retry frame
  - active playback
  - pending stop
  - pending rebuffer
- downlink worker 的活跃判定不再依赖粗粒度 `playback_lane_engaged`：
  - `downlink_active()` 现在直接消费新的 worker-live truth
- transport/io 活跃判定也不再因为 playback lane 仍被占用就继续空转：
  - `transport_active()` 已移除对 `playback_lane_engaged` 的依赖
- 需要保留“回合还在”的路径已改为显式消费 `playback_turn_active()`：
  - `interrupt_tts()`
  - follow-up window timeout
  - XiaoZhi config busy gate
  - playback abort / terminal policy
- 这一步继续把 dialog/runtime/downlink/transport 对 playback 的消费分层：
  - turn lifecycle 看 `turn_active`
  - worker live 看 `has_work`
  - transport poll 看真实 session/listening 活跃

## Step 5.354
- XiaoZhi playback runtime 现在会把 `rebuffering` 也视为可重新打开
  VAD/capture 的 quiet window：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `playback_quiet_window_allows_vad_open()` 现在统一对白名单阶段开放：
  - `prefetching`
  - `rebuffering`
  - `waiting_segment`
  - `tts_stop_pending && !playback_output_active`
- 这一步继续把重缓冲静默期从粗粒度 `playback_lane_engaged` 收口出去，避免
  “response 仍在继续但当前无真实输出”的阶段继续错误 hold capture

## Step 5.353
- XiaoZhi cloud duplex/soft-endpoint 不再把 server `output_state=speaking`
  在 `prefetching/idle` 阶段直接投影成“已进入 speaking output”：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增 `playback_phase_retains_output_turn()`，只让这些 phase 继续保留
  `output_state=speaking` 的 output-turn 语义：
  - `playing`
  - `draining`
  - `rebuffering`
  - `waiting_segment`
- `output_speaking_active()` 现在不再让首段 `prefetch` / 无媒体 `idle` 窗口
  抢跑 soft-endpoint / uplink continuation
- 这一步继续把 cloud duplex 的 speaking truth 从 transport-side
  `output_state` 字符串，收口到 playback runtime 自己的 phase/media truth

## Step 5.352
- XiaoZhi playback runtime 不再在 `tts_start` 时，仅凭预判的 duplex/AEC
  fallback 就立即关闭本地 round：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `apply_tts_start_round_policy()` 现在改为先看
  `capture_held_by_playback()` 的 runtime 真相：
  - 如果 playback 还没有真正 hold capture，就只记录
    `capture_held=no` 并继续保持本地 round 打开
  - 只有当 playback 当前确实会阻断 capture 时，才执行 server-response
    close
- 新增 `playback_started` round policy：
  - 本地 round 关闭被延后到 playback 真正起播并开始 hold capture 的时刻
- 这一步继续把 half-duplex 关轮从“预判将来会播”收口到“当前已经真实占用媒体/
  capture”的 runtime 真相，减少首段预取窗口提前断开 uplink

## Step 5.351
- XiaoZhi playback runtime 现在会把 `prefetching` 也视为可重新打开
  VAD/capture 的 quiet window：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `playback_quiet_window_allows_vad_open()` 不再只给：
  - `waiting_segment`
  - `tts_stop_pending && !playback_output_active`
  开白名单；现在 `prefetching` 也会直接释放 capture hold
- 这一步继续把“尚未真正出声、只是 runtime 正在预取”的阶段，从粗粒度
  `playback_lane_engaged` 里拆出来，避免 pre-start 窗口继续伪装成播放阻断

## Step 5.350
- XiaoZhi playback runtime 现在把 `tts_stop_pending` 后无真实输出的静默 tail-wait
  视为可重新打开 VAD/capture 的 quiet window：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `playback_allows_vad_open()` 与 `capture_held_by_playback()` 不再只给
  `waiting_segment` 开白名单；现在 `stop_pending + !playback_output_active` 也会
  被当成静默窗口
- 这一步继续把 capture hold 从粗粒度 `playback_lane_engaged` 收口到 runtime
  自己的真实媒体输出真相，避免 terminal tail wait 继续伪装成“还在播”

## Step 5.349
- XiaoZhi playback runtime 现在把 `tts_stop_pending` 之后的 detached backend
  残留音频明确收口给 runtime 自己处理：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `playback_check_pending_stop()` 在 backend 已 detached/foreign/restart-pending
  时，不再因为队列里还有残留帧就继续挂起；现在会主动丢弃 detached residual
  queue，再进入 completed terminal 收口
- downlink worker 在 `tts_stop_pending` 期间也不再 fresh-start 非
  `owned_active` backend，避免 terminal-stop 与 residual queue 互相复活

## Step 5.348
- XiaoZhi playback runtime 现在不再让 `playing/draining` phase 单独伪装成
  “真实有声输出”：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 新增 `backend_output_active` helper，`playback_output_active()` 现在要求同时满足：
  - playback phase 仍处于 output-active 窗口
  - playback backend 仍是 `owned_active`
- 这一步继续把：
  - cloud `playback_active`
  - duplex output speaking
  - playback tail / reopen guard
  从 phase-only 投影收紧到 runtime 自己的 backend owner truth，减少
  detached/paused/restart-pending 窗口继续伪装“还在播”的机会

## Step 5.347
- XiaoZhi playback runtime 现在把 `backend_attached` 与 “正在出声”语义显式拆开：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 原先用于 stop/abort/pause 判定的 `backend_owned` helper 已改为
  `backend_attached`
- abort 日志也同步从 `stream_active` 更正为 `stream_attached`，减少后续
  downlink/playback 路径继续混淆 occupancy 与 media activity 的风险

## Step 5.346
- `dialog_runtime` 现在只要 cloud runtime snapshot 已可用，就会把本地
  playback edge event 继续收口给 runtime truth：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `playback_error_is_managed_recovery_locked()` 不再依赖 `phase_known`，
  而是依赖 `cloud_runtime_available`，并把 `owned_paused` 纳入 managed
  recovery owner truth
- 本地 playback reducer 的“无变化直接吸收”条件也从 `phase_known` 收口到了
  `cloud_runtime_available`

## Step 5.345
- `dialog_runtime` 的 `output_turn` 保留逻辑继续收紧：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `playback_lane_engaged=yes` 且 `phase unknown` 时，不再默认继续保留
  output ownership；只有拿不到 cloud runtime snapshot 时才保留这条兜底
- 这一步继续减少 core 用 lane occupancy 伪装 `speaking/output_turn` 的机会，
  让 output ownership 更依赖 runtime snapshot 真相

## Step 5.344
- `dialog_runtime` 的本地 playback shadow 继续收紧为“仅在拿不到
  cloud runtime snapshot 时才兜底”：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `local_playback_shadow_active/recovering` 不再把
  `phase unknown` 当成可介入常态派生的充分条件
- 这一步继续把：
  - `playback_active`
  - `playback_recovering`
  的解释权收回到 runtime snapshot；本地 playback listener 只在 cloud truth
  尚不可得时保留兜底语义

## Step 5.343
- `dialog_runtime` 已删除本地 playback shadow 参与
  `tts_interrupt_requested` 清理的两条旁路：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- cloud snapshot merge 与本地 playback idle 现在统一只看：
  - `output_turn_quiesced_locked()`
  不再因为 `phase unknown` 就额外走本地 shadow special-case
- 这一步继续把 interrupt-clear policy 收口到 runtime 自己的统一派生真相，
  减少 local playback edge signal 提前清 interrupt latch 的机会

## Step 5.342
- XiaoZhi playback `backend_state` 已继续停止把
  `service inactive + phase=playing/draining` 投影成 `owned_active`：
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 现在 `backend_state` 对“是否 attached backend”更严格：
  - 本地 playback-service 不活跃时直接视为 `detached`
  - `playing/draining` 仍由独立的 playback phase / playback_active truth 表示
- 这一步继续把：
  - backend occupancy
  - media/output phase
  两类真相拆开，避免用 phase 去伪造“本地 backend 还挂着”

## Step 5.341
- `dialog_runtime` 已开始直接消费 `playback_backend_state_kind=owned_paused`
  作为受控 playback 过渡真相：
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `playback_error_is_managed_recovery_locked()` 现在除：
  - `rebuffer_pending`
  - `owned_recovering`
  - `restart_pending`
  - `playback_cloud_active`
  外，也会把 `owned_paused` 视为 managed playback transition
- 这让本地 `RIVER_PLAYBACK_ERROR` 落在 XiaoZhi backend 正在
  pause/detach 的窗口时，不再被 core 侧轻易放大成独立 `error_recovering`

## Step 5.340
- XiaoZhi playback backend truth 新增显式 `owned_paused`：
  - [include/river/river_cloud.h](/root/ameba-river/include/river/river_cloud.h)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
  - 用来表示：
    - 当前 playback lane 仍属于 XiaoZhi
    - 但本地 backend 处于 pause / detaching 过渡，而不是正在出声
- `river_cloud_xiaozhi_playback_backend_state()` 现在改为组合：
  - playback runtime phase
  - playback-service 当前 stream owner
  来区分：
    - `owned_active`
    - `owned_paused`
    - `owned_recovering`
    - `foreign_active`
    - `detached`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- downlink worker / start path 也已开始直接消费这条新 truth：
  - `owned_paused` 时不再误判成 foreign/detached 后立即 fresh-start
  - 而是显式等待本地 backend 脱离 pause/detach 过渡
- 这一步继续把 downlink/playback 的 backend ownership 真相从 coarse
  playback-service active 状态，收口到 playback runtime 自己的 typed truth

## Step 5.339
- `dialog_runtime` 的 `playback_active` 派生已继续从“lane occupied”收紧到
  playback runtime 的真实媒体/恢复事实：
  - phase 已知时，不再因 `playback_lane_engaged=yes` 就把
    `playback_active` 维持为 `true`
  - `prefetching` / `waiting_segment` 这种尚未真正出声的阶段不再被 core
    投影成 active
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `playback_error_is_managed_recovery_locked()` 也同步缩窄：
  - 不再把 generic `playback_lane_engaged` 当成 managed recovery 的证据
  - 现在只接受：
    - `playback_rebuffer_pending`
    - backend `owned_recovering` / `restart_pending`
    - `playback_cloud_active`
- 这一步继续把 `dialog runtime` 的 playback 真相面收回到 runtime owner：
  - “占着 lane 但还没真正出声”不再伪装成 active / recovering
  - `playback_error` 也更少被 prefetch/gap 这类非媒体阶段误吸收到 managed
    recovery

## Step 5.338
- `dialog_runtime` 的 output-turn 派生继续从粗粒度 `playback_lane_engaged`
  收紧到“有媒体支撑的 lane truth”：
  - `prefetching`
  - `waiting_segment`
  不再仅因 lane 被占就自动把 output turn 视为 engaged
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 新增 `playback_lane_retains_output_turn_locked()`：
  - 当 playback phase 已知时，当前只让 `rebuffering` 继续保留 output-turn
    ownership
  - phase unknown 时仍保留旧的退化兜底
- `output_speaking_effective_locked()` 现在也要求 output lane 至少有一条“媒体
  backing”：
  - `playback_active`
  - `playback_recovering`
  - 或 `rebuffering` lane retention
- 这一步继续把 `dialog runtime` 的 `speaking` / `barge_in_listening` 从
  “lane 还没清空”推进到“确实还处于有声播放或受控恢复中”的 typed truth

## Step 5.337
- `dialog_runtime` 的本地 playback listener 入口已从“两段式”收口成单次 reducer：
  - 同一个 reducer 里连续完成：
    - dialog playback ownership 识别
    - cloud runtime snapshot 合并
    - local playback shadow 更新
    - aggregate playback / interaction 派生
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 这消除了原先 `on_playback_state(...)` 的双锁 / 双阶段路径：
  - 先改 `local_playback_stream_owned`
  - 再另起一次 `note_playback_state(...)`
  之间的局部抢跑窗口
- 这一步继续把 `dialog runtime` 推向真正 reducer-only 的真相源：
  - 本地 playback ingress 不再先写局部事实、再等后续 sync 覆盖
  - ownership / cloud fact / local fallback 现在在同一轮派生里完成合并

## Step 5.336
- XiaoZhi playback 的 `prefetch_segment` 起播门限现在不再只依赖
  “历史 `meta_gap` 已经偏大”：
  - 当当前 `audio.out.meta` 已有效、当前段不是最后一段、且预测
    `prefetch_target_ms` 已明显高于基线预算时，也会直接进入
    `PREFETCH_SEGMENT`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 这意味着首次起播和段间恢复在尚未积累历史 gap 之前，也能利用服务端已经给出的
  `expected_duration_ms` 做预测性预取：
  - 非终段长 segment 不再默认按 `16` 帧基线过早起播
  - 更倾向等待到 `prefetch_target_ms` 对应的预算帧数，再拉起 backend
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 这一步继续把 downlink/playback 的起播判定从“只靠过去的断供历史”推进到：
  - 消费当前 response/segment 已知事实
  - 用服务端 `audio.out.meta.expected_duration_ms` 提前建立缓冲预算
  从而减少首段和段间恢复时的 `underrun/write_failed/rebuffer` 风暴

## Step 5.335
- XiaoZhi downlink/playback 现在把残留的 `write_failed -> recover` 常态路径也收口
  到统一的 `stop/rebuffer`：
  - `write_failed` 不再区分：
    - `upstream_starved -> stop/rebuffer`
    - `write_failed -> recover`
  - 而是统一先执行：
    - `river_cloud_xiaozhi_stop_playback_for_rebuffer(...)`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `recover` 现在只保留为 stop 失败时的 backend 兜底，而不再是 write-fail 的默认
  恢复通道：
  - `recovery=stop_rebuffer` 成为所有 downlink `write_failed` 的主日志语义
  - 只有 `stop_stream_ex(...)` 自身失败时，才会打印：
    - `xiaozhi playback stop rebuffer fallback to recover`
    并回退到 `river_playback_service_recover_stream_ex(...)`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 这一步继续压缩 playback backend 的恢复语义分叉：
  - runtime 继续把重缓冲真相收口在自己手里
  - playback-service `RECOVERING` 进一步退成硬 stop 失败时的兜底语义
  - 更贴近板端日志里反复出现的：
    - `write failed`
    - `playback stop`
    - `rebuffer start/restart`
    这一条真实故障链

## Step 5.334
- `dialog_runtime` 内部的 local playback shadow 已进一步从三份缓存收成单一
  private state：
  - 删除内部缓存：
    - `playback_local_active`
    - `playback_local_recovering`
  - 保留：
    - `playback_state`
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 新增私有 helper，按需从 `playback_state` 派生本地诊断/兜底语义：
  - `river_dialog_runtime_local_playback_state_active_locked()`
  - `river_dialog_runtime_local_playback_state_recovering_locked()`
  - phase-missing fallback 与 status dump 都改为直接消费这两条派生 helper
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 这一步继续压缩了 `dialog_runtime` 内部 local playback shadow 的重复缓存：
  - stream ownership ingress 仍保留
  - phase-unknown fallback 仍保留
  - diagnostics dump 仍可见
  - 但内部不再缓存三份可能漂移的同类事实

## Step 5.333
- playback runtime 现在把本地 playback-service 的 `RIVER_PLAYBACK_RECOVERING`
  上推成了 cloud-owned typed backend truth：
  - 公共 enum 新增：
    - `RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_RECOVERING`
  - backend 状态名新增：
    - `owned_recovering`
  - [include/river/river_cloud.h](/root/ameba-river/include/river/river_cloud.h)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- XiaoZhi playback runtime 现在会在“本地 stream 仍由自己持有，但 playback service
  已进入 `RECOVERING`”时导出这条 backend truth：
  - `river_cloud_xiaozhi_playback_backend_state()` 新增 `owned_recovering`
    映射
  - downlink worker 在 `owned_recovering` 下不再误走 fresh start
  - `start_playback_if_needed(...)` 也会直接返回 `busy`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `dialog_runtime` 的 `playback_recovering` 常态判断也开始直接消费 cloud
  backend truth：
  - `playback_recovering` 现在会把：
    - `playback_rebuffer_pending`
    - `owned_recovering`
    - `restart_pending`
    统一视为 managed recovery
  - `playback_error_is_managed_recovery_locked()` 也同步吸收
    `owned_recovering`
  - 这一步继续把 `dialog runtime` 对本地 playback coarse state 的 recovering
    依赖压缩到 phase-unknown fallback
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)

## Step 5.332
- XiaoZhi downlink/playback 现在把 `upstream_starved` 的本地 backend 动作从
  同轨 `recover` 改成了显式 `stop/rebuffer`：
  - 新增：
    - `river_cloud_xiaozhi_pause_playback_for_rebuffer()`
    - `river_cloud_xiaozhi_stop_playback_for_rebuffer()`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- starvation helper 与 `write_failed` 的 starved 分支现在都优先走同一条轻量
  pause/detach 路径：
  - `upstream gap rebuffer` 不再直接调用
    `river_playback_service_recover_stream_ex(...)`
  - 当 `write_failed` 已明显符合断供语义时，日志中的 `recovery=` 现改为
    `stop_rebuffer`
  - 只有在 `stop_stream_ex(...)` 自身失败时，才回退到原有 recover 路径
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 这一步把常见的“上游断粮”从本地 playback-service 的 `RECOVERING` 语义里拆了
  出来：
  - rebuffer 真相继续由 playback runtime 持有
  - local backend 在 starved 场景下会退出到可正常再起播的 detached/idle 状态
  - residual 硬 `write_failed` 才继续使用 playback-service recover
  - 这继续压缩了日志里的 `underrun -> write_failed -> flush/restart` 抖动链

## Step 5.331
- `dialog_runtime` snapshot 现在显式吸收 `wake_admission_pending`：
  - 新增 `river_dialog_runtime_snapshot_t.wake_admission_pending`
  - 新增：
    - `river_dialog_runtime_note_wake_admission_pending()`
    - `river_dialog_runtime_clear_wake_admission_pending()`
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- wakeword gating 现在会把“已有待处理 wake admission”视为统一阻断真相：
  - `river_dialog_runtime_wakeword_block_reason_locked()` 新增
    `wake_admission_pending`
  - 这意味着 KWS detection / wake admission 不再在 bridge 已排队重试时继续放行重复 wake
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `wake_admission` bridge 在状态转移时同步维护 runtime 真相：
  - 首次 `wakeword queued` 时抬起 `wake_admission_pending`
  - `wakeword admission accepted` / `failed` 时清掉 `wake_admission_pending`
  - [components/river_core/river_dialog_wake_admission.c](/root/ameba-river/components/river_core/river_dialog_wake_admission.c)
- 这一步继续把 wake admission 从：
  - bridge 内部独占的 pending/retry 局部状态
  - runtime 只知道“当前是否能准入”
  推进到：
  - runtime 也显式知道“已经有一个 wake 在待处理重试”
  - wakeword gating 能直接消费这条真相，减少重复检测和被动 coalesce

## Step 5.330
- `session_coordinator` 不再在收到 `RIVER_VOICE_EVENT_WAKEWORD` 时直接读取
  `river_voice_kws_wake_handoff_block_reason()`：
  - 协调器现在只负责把 wakeword 事件提交到
    `river_dialog_wake_admission_submit(...)`
  - [components/river_core/river_session_coordinator.c](/root/ameba-river/components/river_core/river_session_coordinator.c)
- `wake_admission` 提交入口现在统一裁决唤醒接力阻塞原因：
  - 新增 `river_dialog_wake_admission_submit_block_reason()`
  - 先吸收 KWS 本地 handoff debug blocker
  - 再吸收 `dialog_runtime` 的 wakeword admission blocker
  - [components/river_core/river_dialog_wake_admission.c](/root/ameba-river/components/river_core/river_dialog_wake_admission.c)
- wakeword handoff 的日志语义也收口到同一处：
  - `wakeword handoff blocked`
  - `wakeword queued`
  - `wakeword coalesced while pending`
  现在都由 `wake_admission` 统一输出
- 这一步继续把 wakeword 从：
  - `session_coordinator` 上的 KWS 私有前置判定
  - `wake_admission` 内的 runtime admission 判定
  收口成 core-owned 的单一提交/准入边界

## Step 5.329
- `dialog_cloud_port` 已删除无消费者的 `conversation_window_active` 侧门：
  - 移除 `river_dialog_cloud_port_t.conversation_window_active`
  - 删除 `river_dialog_cloud_conversation_window_active()`
  - [include/river/river_dialog_cloud_port.h](/root/ameba-river/include/river/river_dialog_cloud_port.h)
  - [components/river_core/river_dialog_cloud_port.c](/root/ameba-river/components/river_core/river_dialog_cloud_port.c)
- app 装配层同步删除这条已失效的 cloud-port wiring：
  - `river_app_dialog_cloud_conversation_window_active()`
  - `g_river_app_dialog_cloud_port.conversation_window_active`
  - [components/river_core/river_app.c](/root/ameba-river/components/river_core/river_app.c)
- 这一步意味着 `conversation_window` 的读取真相已经彻底回到 `dialog_runtime`：
  - `dialog_cloud_port` 现在只保留 command / ingress 调用面
  - 不再保留一条额外的 conversation-window 查询侧门

## Step 5.328
- `dialog_runtime` 新增统一的 wakeword-detection 阻断接口：
  - `river_dialog_runtime_wakeword_detection_block_reason()`
  - `river_dialog_runtime_allows_wakeword_detection()`
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- wakeword detection 与 wake admission 现在复用同一条内部阻断真相：
  - `river_dialog_runtime_wakeword_block_reason_locked()`
  - `conversation_window_active`
  - `cloud_local_close_pending`
  - `cloud_listen_stop_pending`
  - 非 `wake_monitoring` 的 interaction state
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `river_voice_kws_detection_allowed()` 已不再自己拼：
  - `river_dialog_cloud_conversation_window_active()`
  - `river_interaction_state_get() == RIVER_INTERACTION_WAKE_MONITORING`
  而是直接消费 `dialog_runtime` 的统一结论
  - [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc)
- KWS alignment busy 日志也改为直接打印 `wakeword_detection_block_reason`，
  不再重复暴露一套独立的 interaction/window 原因拼装
  - [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc)
- 这一步继续把 wakeword 相关 gating 从：
  - KWS 一套 raw 条件
  - wake admission 一套 runtime 条件
  收口到 `dialog runtime` 的单一真相源

## Step 5.327
- `dialog_runtime` 新增统一的 interrupt-clear 判定：
  - `river_dialog_runtime_output_turn_quiesced_locked()`
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `river_dialog_runtime_local_idle_clears_tts_interrupt_locked()` 不再自己拼：
  - `playback_active`
  - `playback_lane_engaged`
  - `output_lane == speaking`
  这些 raw 条件，而是复用 `output_turn_quiesced` 语义
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- cloud snapshot sync 路径现在也改为：
  - 先 `refresh_playback`
  - 再用同一条 `output_turn_quiesced` helper 清 `tts_interrupt_requested`
  - 不再在 refresh 前基于 raw snapshot 字段各自拼装清理条件
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 这一步把 `tts_interrupt_requested` 的清理语义继续从：
  - 本地 callback 一套
  - cloud sync 一套
  收口到“output turn 已真正静止”的统一 owner 派生

## Step 5.326
- `dialog_runtime` 现在显式区分：
  - 有声 playback active
  - 输出 turn 仍被占用、但当前可能只是 silent gap
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 新增内部 helper：
  - `river_dialog_runtime_playback_waiting_segment_locked()`
  - `river_dialog_runtime_output_turn_engaged_locked()`
  - 用来把 `waiting_segment` 从粗粒度 `playback_active` 推导里拆出来
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `river_dialog_runtime_compute_playback_active_locked()` 现在不会再仅因
  `playback_lane_engaged=yes` 且 phase=`waiting_segment` 就继续把
  `playback_active` 维持为 `true`
  - 段间静默不再伪装成“本地仍有有声播放”
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `river_dialog_runtime_allows_barge_in_interrupt()` 与 interaction 派生现在改为
  依赖 `output turn engaged`，而不是错误依赖 `playback_active`
  - 这意味着 `waiting_segment` 下虽然 `playback_active` 会下降，
    但用户仍可对尚未结束的 response 触发 barge-in interrupt
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `river_dialog_runtime_output_speaking_effective_locked()` 现在会显式压低
  `waiting_segment` 下的 silent-gap speaking 投影，避免把“静默段间隙”继续误当成
  有声播放本身
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)

## Step 5.325
- `waiting_segment` 现在被明确视为“response 尚未结束，但本地正处于段间静默空窗”：
  - 新增 `river_cloud_xiaozhi_playback_silent_gap_allows_vad_open()`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `river_cloud_xiaozhi_playback_allows_vad_open()` 在
  `RIVER_CLOUD_PLAYBACK_PHASE_WAITING_SEGMENT` 时会直接返回 `true`：
  - 不再继续要求 duplex/AEC ready
  - 不再把段间静默误判成持续中的 playback AEC block
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `river_cloud_xiaozhi_capture_held_by_playback(...)` 在 `waiting_segment` 时会：
  - 直接返回 `false`
  - 显式清空 `fallback_reason`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 这一步把段间静默从“playback lane 仍 engaged，所以 capture 仍应被 AEC 阻断”
  收口成更准确的 owner truth：
  - response 仍在继续
  - 但本地 capture / VAD / soft-endpoint 已可重新打开

## Step 5.324
- playback owner 新增显式 `waiting_segment` phase：
  - `river_cloud_playback_phase_t` 增加 `RIVER_CLOUD_PLAYBACK_PHASE_WAITING_SEGMENT`
  - `river_cloud_playback_phase_name(...)` 新增 `waiting_segment`
  - [include/river/river_cloud.h](/root/ameba-river/include/river/river_cloud.h)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- XiaoZhi downlink/playback runtime 现在会显式区分“当前段已播完，但下一段还未到”：
  - 新增 `playback_waiting_next_segment()` 判定
  - known segment 已到但音频尚未入 ring 时，phase 继续保持 `prefetching`
  - 段间空窗则进入 `waiting_segment`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- downlink worker 现在会在段间空窗时主动 pause playback backend：
  - 队列耗尽且仍在同一 response 中等待下一段时，先 `stop_stream`
  - 随后转入 `waiting_segment`
  - 不再继续把这类 inter-segment gap 被动拖到本地 `underrun/write_failed`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 这一步直接把下行恢复模型里的一个关键歧义拆开：
  - `waiting_segment` 表示上游下一段尚未供给
  - `rebuffering/write_failed` 只保留给真正没被前置拦住的恢复路径

## Step 5.323
- playback owner 的 `backend state` 已从“两个分裂的布尔投影”收口成公共 typed
  truth：
  - 新增 `river_cloud_playback_backend_state_t`
  - 新增 `river_cloud_playback_backend_state_name(...)`
  - [include/river/river_cloud.h](/root/ameba-river/include/river/river_cloud.h)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- XiaoZhi downlink/playback runtime 已改为直接维护并导出公共 `backend state`：
  - 删除私有 `river_cloud_xiaozhi_playback_backend_state_t`
  - `playback_runtime_snapshot` 改为显式填充 `playback_backend_state_kind`
  - owner 内部的 start/abort/rebuffer 判断统一复用公共 enum
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `dialog_runtime` 公开 snapshot 已不再暴露：
  - `playback_backend_owned`
  - `playback_backend_restart_pending`
  - 改为吸收并导出 `playback_backend_state_kind`
  - `dialog_runtime_dump_status()` 也改为直接打印 typed backend state
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 这一步继续把 downlink/playback backend ownership 真相从跨层布尔投影收口为
  公共 owner typed truth，减少 core 对旧派生语义的重复解释。

## Step 5.322
- playback owner 的 `phase` 已从 XiaoZhi 私有 enum 提升成公共 typed truth：
  - 新增 `river_cloud_playback_phase_t`
  - 新增 `river_cloud_playback_phase_name(...)`
  - [include/river/river_cloud.h](/root/ameba-river/include/river/river_cloud.h)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- XiaoZhi downlink/playback runtime 已改为直接维护并导出公共 `phase`：
  - 删除私有 `river_cloud_xiaozhi_playback_phase_t`
  - `xiaozhi_playback_phase` 内部状态改为公共 enum
  - `playback_runtime_snapshot` 显式填充 `playback_phase_kind`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `dialog_runtime` 公开 snapshot 已不再暴露字符串 `playback_phase`：
  - 改为吸收并导出 `playback_phase_kind`
  - `dialog_runtime_dump_status()` 也改为直接打印 typed phase
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 这一步继续把 downlink/playback 的 phase 真相从 owner 内部私有 enum 与跨层
  文本投影，收口到公共 owner typed truth。

## Step 5.321
- playback owner 的 `terminal state` 已从内部字符串真相提升成公共 typed truth：
  - 新增 `river_cloud_playback_terminal_state_t`
  - 新增 `river_cloud_playback_terminal_state_name(...)`
  - [include/river/river_cloud.h](/root/ameba-river/include/river/river_cloud.h)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- XiaoZhi downlink/playback runtime 已改为直接维护并导出公共 `terminal state`：
  - 删除内部 `xiaozhi_playback_terminal_state[...]` 字符串真相
  - 改为 `xiaozhi_playback_terminal_state_kind`
  - terminal reset / ack 映射 / local fallback 全部直接落在公共 enum
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `dialog_runtime` 公开 snapshot 已不再暴露字符串 `playback_terminal_state`：
  - 改为吸收并导出 `playback_terminal_state_kind`
  - `dialog_runtime_dump_status()` 也改为直接打印 typed terminal state
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 这一步继续把 downlink/playback 的 terminal 语义从 owner 内部字符串与跨层
  文本投影，收口到公共 owner typed truth。

## Step 5.320
- playback owner 的 `start policy` 已从 XiaoZhi 私有 enum 提升成公共 typed truth：
  - 新增 `river_cloud_playback_start_policy_t`
  - 新增 `river_cloud_playback_start_policy_name(...)`
  - [include/river/river_cloud.h](/root/ameba-river/include/river/river_cloud.h)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- XiaoZhi downlink/playback runtime 已改为直接维护并导出公共 `start policy`：
  - 删除私有 `river_cloud_xiaozhi_playback_start_policy_t`
  - `xiaozhi_playback_start_policy` 内部状态改为公共 enum
  - `playback_runtime_snapshot` 显式填充 `playback_start_policy_kind`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `dialog_runtime` 公开 snapshot 已不再暴露字符串 `playback_start_policy`：
  - 改为吸收并导出 `playback_start_policy_kind`
  - `dialog_runtime_dump_status()` 也改为直接打印 typed policy
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 这一步继续把 downlink/playback 的启动门限决策从 owner 内部私有 enum 与跨层
  字符串外抛，收口为公共 typed truth。

## Step 5.319
- playback owner 的 `rebuffer cause` 已从 XiaoZhi 私有 enum 提升成公共 typed truth：
  - 新增 `river_cloud_playback_rebuffer_cause_t`
  - 新增 `river_cloud_playback_rebuffer_cause_name(...)`
  - [include/river/river_cloud.h](/root/ameba-river/include/river/river_cloud.h)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- XiaoZhi downlink/playback runtime 已改为直接维护并导出这条公共 typed cause：
  - 删除私有 `river_cloud_xiaozhi_playback_rebuffer_cause_t`
  - `playback_runtime_snapshot` 现在显式填充 `playback_rebuffer_cause_kind`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `dialog_runtime` 公开 snapshot 已不再暴露字符串 `playback_rebuffer_cause`：
  - 改为吸收并导出 `playback_rebuffer_cause_kind`
  - `dialog_runtime_dump_status()` 也改为直接打印 typed cause
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 这一步继续把 downlink/playback 的恢复语义从：
  - owner 内部私有枚举
  - 跨层字符串原因
  收口到公共 typed owner truth，减少 `dialog_runtime` 对文本诊断字段的依赖。

## Step 5.318
- `dialog_runtime` 的公开 snapshot 已不再暴露本地 playback shadow：
  - 删除 `playback_local_active`
  - 删除 `playback_local_recovering`
  - 删除 `playback_state`
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
- 上述本地 playback shadow 已下沉为 `dialog_runtime` 内部私有诊断状态：
  - 仅在 runtime 内部用于：
    - stream ownership ingress
    - phase-missing fallback
    - dump diagnostics
  - 不再作为对外公开真相的一部分
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `river_dialog_runtime_dump_status()` 改为在锁内直接抓取：
  - public snapshot
  - internal local playback shadow
  再统一打印，避免为了日志而把本地 shadow 继续留在公开 API 上。
- 这一步继续把 `dialog runtime` 的对外真相面收口到：
  - cloud/playback owner typed truth
  - dialog lane / interaction 派生
  而把本地 playback shadow 明确降为内部实现细节。

## Step 5.317
- `dialog_runtime` 已把剩余 local playback shadow 的 active/recovering 参与面
  显式收口到 `phase unknown` fallback helper：
  - `river_dialog_runtime_local_playback_shadow_active_fallback_locked()`
  - `river_dialog_runtime_local_playback_shadow_recovering_fallback_locked()`
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `compute_playback_active_locked()` 与 `compute_playback_recovering_locked()` 不再内联
  混合 `playback_local_*` 和 cloud/runtime truth：
  - phase 已知时，local shadow 不再进入常态派生
  - phase 缺席时，才退化回本地 shadow
- interrupt-clear 的 local shadow 阻断路径也改为复用同一 helper，避免未来再次把
  `playback_local_active` 从 phase-missing fallback 重新扩散回常态逻辑。
- 这一步继续把 `dialog_runtime` 中的 local playback shadow 明确降级成：
  - purely-diagnostic local signal
  - phase-missing fallback
  而不是与 playback owner truth 并列的常态行为输入。

## Step 5.316
- cloud playback runtime 现在把 terminal wait 从“布尔 + 文本原因”提升成
  typed truth：
  - 新增 `playback_terminal_wait_kind`
  - XiaoZhi playback owner 直接输出：
    - `await_last_segment_meta`
    - `await_segment_queue_drain`
    - `await_last_segment_tail`
  - [include/river/river_cloud.h](/root/ameba-river/include/river/river_cloud.h)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `dialog_runtime` snapshot 同步吸收这条 typed terminal-wait 真相，并开始用它区分：
  - 真实尾段等待
  - 仍在等待 last-segment meta 的非尾段等待
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `dialog_runtime` 现在只会在：
  - `await_segment_queue_drain`
  - `await_last_segment_tail`
  这两类 terminal wait 下压低 `output_lane=speaking` 的有效投影；
  `await_last_segment_meta` 不再被误当成“已经进入尾段静默等待”。
- 这一步继续把 core 行为层从 generic `playback_terminal_waiting` 的过度解释，
  收回到 playback owner 导出的 typed terminal truth。

## Step 5.315
- `dialog_runtime` 进一步收紧了 local playback shadow 的参与范围：
  - 当 cloud/runtime snapshot 已声明 `playback_phase_known` 时，
    `playback_local_active` 不再阻断 `tts_interrupt_requested` 的清理
  - 本地 playback shadow 现在只在 phase 缺席兜底时参与这条 interrupt clear
    判定
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 这一步继续把 `dialog_runtime` 的行为层从本地 playback listener shadow
  收回到 playback owner 导出的 typed truth。

## Step 5.314
- `dialog_runtime` 已不再要求 app 在启动时手工注册 dialog playback stream 名称：
  - 删除 `river_dialog_runtime_register_playback_stream(...)`
  - 删除 `dialog_runtime` 内部的 stream 白名单表
  - dialog playback ingress 现在直接以 `RIVER_PLAYBACK_PRIO_TTS` 识别
    dialog output
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- app 装配层同步退场这层 playback 白名单 wiring：
  - `river_app_boot()` 不再为 `xiaozhi_tts` / `iflytek_tts` 做额外注册
  - [components/river_core/river_app.c](/root/ameba-river/components/river_core/river_app.c)
- 这一步把 dialog playback 识别继续从“外部手工白名单”收回到 runtime 自身的
  typed ownership 规则，减少了 app 对 dialog/runtime 真相源的装配负担。

## Step 5.313
- `dialog_runtime` 的 playback listener ingress 现在会先拉取当前 cloud
  runtime snapshot，再吸收本地 playback service 边沿：
  - `managed_recovery`
  - `interrupt latch clear`
  - interaction 派生
  都会基于同一时刻的 playback owner truth，而不是只看上一次残留的
  cloud playback 视图
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 这一步继续把本地 `RIVER_PLAYBACK_*` callback 从“粗粒度真相源”降级成：
  - local backend shadow
  - typed playback ingress signal
  真正的 aggregate playback truth 继续优先来自 cloud/playback runtime snapshot。

## Step 5.312
- cloud playback runtime snapshot 继续补齐 backend ownership truth：
  - `playback_backend_owned`
  - `playback_backend_restart_pending`
  - [include/river/river_cloud.h](/root/ameba-river/include/river/river_cloud.h)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `dialog_runtime` 已开始直接吸收这两条 typed truth：
  - `managed_recovery` 判定现在显式覆盖 `backend_restart_pending`
  - local `IDLE` 清 interrupt latch 时也会额外确认 backend 不在 restart-pending
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 这一步继续把 runtime 对“本地 playback listener 瞬时状态”的依赖往
  playback owner 输出的 backend truth 收口。

## Step 5.311
- `dialog_runtime` 已不再依赖 playback phase 字符串做行为判断：
  - 删除了内部对
    - `"playing"`
    - `"draining"`
    - `"rebuffering"`
    这些 phase 文本的行为分支
  - `playback_phase` 现在只保留给诊断投影
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- playback 相关派生现在只消费 typed truth：
  - `playback_cloud_active`
  - `playback_lane_engaged`
  - `playback_rebuffer_pending`
  - `playback_phase_known`
  - `playback_terminal_closed`
- 这一步意味着 `dialog_runtime` 的行为面已进一步从
  “读 playback phase 字符串” 退到 “读 playback owner 导出的布尔真相”。

## Step 5.310
- cloud playback runtime snapshot 新增 typed playback truth：
  - `playback_phase_known`
  - `playback_terminal_closed`
  - [include/river/river_cloud.h](/root/ameba-river/include/river/river_cloud.h)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `dialog_runtime` snapshot 同步吸收这两条 typed truth，并开始直接消费它们：
  - `playback_phase_known` 不再通过 `playback_phase[0] != '\0'` 侧推
  - `playback_terminal_closed` 不再通过 `playback_terminal_state[0] != '\0'` 侧推
  - `allows_barge_in_interrupt()` / playback active 推导 / status dump 现在都优先走
    typed bool
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 这一步继续减少了 core 侧对 playback runtime 字符串状态的二次解释，让：
  - phase 是否已知
  - terminal 是否已闭合
  直接由 playback owner 输出布尔真相。

## Step 5.309
- `dialog_runtime` 不再暴露公开的 playback-state 侧门：
  - `river_dialog_runtime_note_playback_state(...)` 已从公开头文件删除
  - 本地 playback 真相现在继续只经由：
    - `river_dialog_runtime_on_playback_state(...)`
    这条 listener ingress 进入 runtime
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `dialog_runtime` 清理 `tts_interrupt_requested` 的条件也进一步收紧：
  - 本地 playback listener 若只看到 `RIVER_PLAYBACK_IDLE`，
    不会再立刻无条件清掉 interrupt latch
  - 当 cloud playback 真相仍显示：
    - playback_active
    - playback_recovering
    - playback_lane_engaged
    - tts_stop_pending
    - playback_terminal_waiting
    之一时，latch 会继续保留
  - 只有 aggregate playback truth 真正收口后，local idle 才能清掉这条 in-flight truth
- 这一步把：
  - local playback listener edge
  - interrupt latch clear policy
  进一步绑定到 `dialog_runtime` 聚合真相，而不是单独依赖本地 service 的瞬时 `IDLE`
  回调。

## Step 5.308
- 删除了已无调用的 `dialog_runtime` 外部错误侧门接口：
  - `river_dialog_runtime_note_error(...)`
  - `river_dialog_runtime_clear_error(...)`
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 这一步明确了 `dialog_runtime` 的错误语义入口：
  - 由 cloud/runtime snapshot 同步驱动
  - 由 playback state ingest/reducer 驱动
  - 不再保留一个可被外部任意调用的 coarse error mutation API
- 这样可以继续压缩“从真相源之外粗暴抬高/清空 error_recovering”的回归面，
  避免未来又把 `dialog_runtime` 退回成可随意改写的共享状态盒子。

## Step 5.307
- `dialog_runtime` 在 managed playback recovery 中不再因为本地
  `RIVER_PLAYBACK_ERROR` 顺手清掉 `tts_interrupt_requested`：
  - 仅当 playback 真正进入：
    - `idle`
    - 或 unmanaged `playback_error`
    时才清掉 interrupt in-flight latch
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 这一步避免了 playback runtime 正在 rebuffer/recovering 时，
  `dialog_runtime` 又过早重新放开重复 barge-in interrupt。

## Step 5.306
- `dialog_runtime` 现在会区分“真正的 playback error”与“managed recovery 中的本地
  playback_error”：
  - 若 cloud playback truth 已表明当前仍处于
    `rebuffer/recovering/cloud_active/lane_engaged`
    之一，本地 `RIVER_PLAYBACK_ERROR` 会被吸收到
    `playback_recovering`
  - 不再额外把 `interaction_state` 抬成 `error_recovering`
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `river_dialog_runtime_on_playback_state()` 不再在每次本地
  `RIVER_PLAYBACK_ERROR` 后无条件调用 `note_error("playback_error")`：
  - playback runtime 已能把受管控的 write_failed/rebuffer/restart 路径投影成
    managed recovery truth
  - `dialog_runtime` 现在优先消费这条 playback truth，而不是额外叠一层独立错误态

## Step 5.305
- `river_cloud_xiaozhi_dump_session_status()` 现在改为先读取
  `river_cloud_runtime_snapshot_t`，再输出会话状态日志：
  - listening
  - playback_active
  - playback_phase
  - playback_rebuffer_pending/cause
  - tts_stop_pending
  - local_close/window remaining
  - turn_accepted / turn_id / input_state / output_state / barge_in_enabled
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- 这一步让 status dump 继续贴近 runtime 真相源，不再在日志路径中重新拼接
  playback/turn 事实，也去掉了对 `g_river_cloud.xiaozhi_tts_stop_pending` 的最后一处
  session-side 直读。

## Step 5.304
- `wake admission` 的 pending/retry worker 已从
  `session_coordinator` 抽到新的 core-owned bridge：
  - [include/river/river_dialog_wake_admission.h](/root/ameba-river/include/river/river_dialog_wake_admission.h)
  - [components/river_core/river_dialog_wake_admission.c](/root/ameba-river/components/river_core/river_dialog_wake_admission.c)
- 新 bridge 现在直接拥有：
  - wakeword queued/coalesced/deferred/retry
  - worker unavailable 时的 inline fallback
  - wake admission block reason 检查
  - [components/river_core/river_dialog_wake_admission.c](/root/ameba-river/components/river_core/river_dialog_wake_admission.c)
- `session_coordinator` 已不再维护 wake admission 的：
  - task
  - sema/mutex
  - pending/deferred/confidence/text
  - 它现在只保留：
    - wake handoff gate
    - ASR 文本日志 / interrupt / diag fanout
  - [components/river_core/river_session_coordinator.c](/root/ameba-river/components/river_core/river_session_coordinator.c)
- `components/river_core/CMakeLists.txt` 已纳入新的 bridge 源文件：
  - [components/river_core/CMakeLists.txt](/root/ameba-river/components/river_core/CMakeLists.txt)

## Step 5.303
- `dialog_cloud_port` 现在会在 `begin_conversation_window()` 成功后，立即把
  wake admission 成功事实写回 `dialog runtime`：
  - 新增 source -> runtime reason 归一：
    - `wakeword -> wakeword_detected`
  - [components/river_core/river_dialog_cloud_port.c](/root/ameba-river/components/river_core/river_dialog_cloud_port.c)
- `session_coordinator` 不再在 wake admission 成功路径手工补
  `river_dialog_runtime_note_wake_confirmed_with_cloud_state(...)`：
  - wakeword worker 成功路径已去掉重复 side effect
  - inline fallback 也不再自行判断成功后再补 runtime note
  - [components/river_core/river_session_coordinator.c](/root/ameba-river/components/river_core/river_session_coordinator.c)
- 这一步把 `wake admission success -> dialog runtime wake_confirmed` 收口成
  `dialog_cloud_port` 内的一次原子操作，继续减少 coordinator 对 cloud-success
  副作用的拼装。

## Step 5.302
- `dialog_cloud_port` 现已补齐 wake admission 入口：
  - `begin_conversation_window(const char *source)`
  - [include/river/river_dialog_cloud_port.h](/root/ameba-river/include/river/river_dialog_cloud_port.h)
  - [components/river_core/river_dialog_cloud_port.c](/root/ameba-river/components/river_core/river_dialog_cloud_port.c)
- `river_app` 已把该入口绑定到具体 cloud adapter：
  - `river_app_dialog_cloud_begin_conversation_window()`
  - [components/river_core/river_app.c](/root/ameba-river/components/river_core/river_app.c)
- `session_coordinator` 的 wakeword worker / fallback 路径现已改走
  `river_dialog_cloud_begin_conversation_window()`：
  - `session_coordinator` 不再直接调用
    `river_cloud_adapter_begin_conversation_window()`
  - 这继续减少了 core 层对 concrete adapter 的直连，wake admission 入口开始与
    ASR / interrupt 一样统一走 core-owned dialog cloud port
  - [components/river_core/river_session_coordinator.c](/root/ameba-river/components/river_core/river_session_coordinator.c)

## Step 5.301
- `dialog runtime` 现在直接拥有本地 `interrupt_tts` in-flight 真相：
  - snapshot 新增 `tts_interrupt_requested`
  - `river_dialog_runtime_allows_barge_in_interrupt()` 现同时检查：
    - `tts_stop_pending`
    - `tts_interrupt_requested`
  - ASR lifecycle / playback idle-or-error / cloud playback fully disengaged 时会清掉
    本地 interrupt 请求锁存
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `dialog_cloud_port` 在成功调用 `interrupt_tts_with_reason()` 后，会立刻把这一事实记入
  `dialog runtime`，从而在 cloud `tts_stop_pending` 回流前就抑制重复 interrupt：
  - [components/river_core/river_dialog_cloud_port.c](/root/ameba-river/components/river_core/river_dialog_cloud_port.c)
- `session_coordinator` 不再维护本地 `barge_in_interrupt_requested`：
  - ASR 文本触发的 interrupt 改走 `river_dialog_cloud_interrupt_tts_with_reason()`
  - `state_lock` 与这条本地 latch 一并移除
  - coordinator 继续只负责：
    - 文本日志
    - 最终文本路由
    - wakeword worker / admission 调度
  - [components/river_core/river_session_coordinator.c](/root/ameba-river/components/river_core/river_session_coordinator.c)

## Step 5.300
- `dialog runtime` 现在直接吸收更多 XiaoZhi round/window typed truth，而不是只靠
  边缘 lifecycle 事件推断当前轮次：
  - `listen_stop_pending`
  - `local_close_pending`
  - `conversation_window_remaining_ms`
  - `local_close_remaining_ms`
  - [include/river/river_cloud.h](/root/ameba-river/include/river/river_cloud.h)
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
- `river_cloud_xiaozhi_fill_runtime_snapshot()` 现已把上述 round/window 事实直接从
  XiaoZhi round runtime 投影到 cloud snapshot：
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- `river_dialog_runtime_apply_cloud_snapshot_locked()` 继续收口这组 round truth：
  - cloud snapshot 中的 `listen_stop_pending/local_close_pending/window remaining`
    现会被原子吸收到 `dialog runtime snapshot`
  - 当 cloud round 仍处于 listening / stream / stop-pending / local-close-pending /
    committed-input 之一时，`dialog runtime` 会维持 `asr_session_active`
  - wakeword admission block reason 现优先暴露 typed round cause，而不是回退成
    笼统 interaction-state
  - status dump 现会直接打印：
    - `stop_pending`
    - `local_close`
    - `local_close_remaining_ms`
    - `conversation_window_remaining_ms`
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)

## Step 5.299
- 继续把 XiaoZhi 的 round 启动路径从
  `river_cloud_xiaozhi_session.c` 并入
  `river_cloud_xiaozhi_round_runtime.c`：
  - `open_session_and_listen()`
  - `start_followup_round()`
  - `maybe_start_followup_round()`
  - `begin_conversation_window()`
  - [components/river_cloud/river_cloud_xiaozhi_round_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_round_runtime.c)
- `round runtime` 现在同时拥有：
  - round/window 读 helper
  - post-commit wait / local-close defer
  - local close / timeout / reset
  - follow-up reopen / wake admission / open-listen round 启动
- `river_cloud_xiaozhi_session.c` 继续退化为：
  - transport event reducer
  - semantic/turn state
  - uplink / capture 主流程
  - 其内部已不再内联 wake admission / follow-up reopen 的 round 启动状态机

## Step 5.298
- 继续把 XiaoZhi 的 round close/reset/timeout/post-commit wait 语义收口到
  `river_cloud_xiaozhi_round_runtime.c`：
  - `apply_open_and_listen_session_policy()`
  - `emit_session_started()/emit_session_closed()`
  - `clear_local_close_defer()`
  - `prepare_post_commit_wait()`
  - `close_local_round_for_cause()`
  - `check_local_close_timeout()`
  - `reset_transport_state()`
  - `check_window_timeout()`
  - [components/river_cloud/river_cloud_xiaozhi_round_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_round_runtime.c)
- `river_cloud_xiaozhi_session.c` 进一步退成 transport/semantic/uplink 主流程：
  - post-commit wait 不再手工拼
    `listen_stop_pending/local_close_pending/window_touch`
  - follow-up reopen / wake admission / interrupt admission 继续改走 typed round
    runtime helper
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- `river_cloud_internal.h` 新增 round runtime 显式入口：
  - `clear_local_close_defer()`
  - `prepare_post_commit_wait()`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)

## Step 5.297
- 将 XiaoZhi 的 round/window 辅助状态从
  `river_cloud_xiaozhi_session.c` 拆到新的
  `river_cloud_xiaozhi_round_runtime.c`：
  - `listening_active()`
  - `conversation_window_active()/remaining_ms()`
  - `local_close_pending()/remaining_ms()`
  - `listen_stop_pending()`
  - `apply_listen_stop_completion_round_policy()`
  - `maybe_finalize_listen_stop()`
  - `uplink_keepalive_needed()`
  - `uplink_send_ready()`
  - `should_defer_local_close()`
  - `arm_local_close_defer()`
  - `window_touch()/close()/abort_local()`
  - [components/river_cloud/river_cloud_xiaozhi_round_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_round_runtime.c)
- `river_cloud_xiaozhi_session.c` 继续瘦身，只保留 transport/semantic/uplink 主流程；
  这一步把 listen-stop、follow-up window、本地延迟关闭的 owner 从大文件中抽离。
- `components/river_cloud/CMakeLists.txt` 已纳入新的 round runtime 源文件。

## Step 5.296
- `river_dialog_cloud_conversation_window_active()` 现在优先读取
  `dialog runtime` 快照中的 `conversation_window_active`，而不是直接透传
  cloud adapter：
  - `river_dialog_runtime_get_snapshot()`
  - `snapshot.conversation_window_active`
  - [components/river_core/river_dialog_cloud_port.c](/root/ameba-river/components/river_core/river_dialog_cloud_port.c)
- 这样 voice/KWS 侧通过 dialog cloud port 观察 follow-up window 时，开始以
  `dialog runtime` 作为真相源；旧的 cloud port callback 仅保留为 runtime
  不可用时的 fallback。

## Step 5.295
- `xiaozhi_session.c` 继续停止直读 playback 内部字段：
  - runtime status dump 改为通过
    `river_cloud_xiaozhi_playback_rebuffer_pending()` 读取 rebuffer 状态
  - transport closed 观测日志改为通过
    `river_cloud_xiaozhi_playback_output_active()` 读取播放活跃态
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- playback runtime 新增 typed helper：
  - `river_cloud_xiaozhi_playback_rebuffer_pending()`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `no_ref reopen` reset 语义继续回收至 playback runtime：
  - `apply_transport_reset_playback_policy()`
  - `apply_session_start_playback_policy()`
  - `mark_playback_started()`
  - `reset_playback_state()`
  - `xiaozhi_session.c` 不再在 transport reset 路径手工清这组字段

## Step 5.294
- `endpoint_soft_close` 状态机已从 `river_cloud_xiaozhi_session.c` 收口到
  `river_cloud_xiaozhi_playback_runtime.c`：
  - `clear_endpoint_soft_close_state()`
  - `cancel_endpoint_soft_close()`
  - `note_interrupt_hint()`
  - `arm_endpoint_soft_close()`
  - `poll_endpoint_soft_close_timeout()`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- playback runtime 新增 typed read helper，避免 session 继续直读内部字段：
  - `endpoint_soft_close_pending()`
  - `endpoint_soft_close_remaining_ms()`
  - `endpoint_soft_close_reason()`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- `xiaozhi_session.c` 现在只消费上述 helper，并在 reset/session_close 路径调用
  `clear_endpoint_soft_close_state()`，不再自行清零这组字段。

## Step 5.293
- `no-ref reopen/open_hold` 语义开始并回 playback runtime：
  - `river_cloud_xiaozhi_open_hold_frames_required()` 已从
    `river_cloud_xiaozhi_session.c` 移入
    `river_cloud_xiaozhi_playback_runtime.c`
  - 新增 typed helper：
    - `river_cloud_xiaozhi_playback_followup_reopen_ready(bool is_speech)`
  - `maybe_start_followup_round()` 不再自己维护 `no_ref_reopen` 的
    guard/rearm 状态机，只调用 playback runtime helper
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- 这一步把同一套 `no-ref reopen` 状态从“runtime 写一半、session 解一半”改成由
  playback runtime 统一持有和解释。

## Step 5.292
- playback runtime 继续接管 `soft endpoint` 语义：
  - `river_cloud_xiaozhi_duplex_soft_endpoint_enabled()`
  - `river_cloud_xiaozhi_duplex_speaking_uplink_continuation_active()`
  - `river_cloud_xiaozhi_output_speaking_active()` 也随之移到
    `river_cloud_xiaozhi_playback_runtime.c`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- 这一步把“server speaking / playback active 时是否允许 soft endpoint 与 uplink
  continuation” 的判定继续从 session 文件剥离，交给 downlink runtime 持有。
- `xiaozhi_session.c` 继续缩减为 round/window/session 编排层，不再持有
  playback-driven duplex continuation 规则。

## Step 5.291
- `XiaoZhi` 的 playback/duplex admission helper 继续从
  `river_cloud_xiaozhi_session.c` 收口到
  `river_cloud_xiaozhi_playback_runtime.c`：
  - `river_cloud_xiaozhi_playback_allows_vad_open()`
  - `river_cloud_xiaozhi_capture_held_by_playback()`
  - `river_cloud_xiaozhi_apply_tts_start_round_policy()`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- 这一步把“播放期间 VAD/AEC 准入”和 “`tts_start` 是否保持本地 round 打开”
  的判定从 session 文件剥离，转由 playback runtime 这个下行真相源持有。
- `xiaozhi_session` 继续缩小为 transport/session 编排层，不再实现这些下行播放
  判定规则，只消费已经公开的 playback helper。

## Step 5.290
- `session_coordinator` 对 `dialog runtime snapshot` 的两类直接解读已继续收口成
  typed policy helper 调用：
  - wakeword 准入判断
  - ASR text barge-in 打断判断
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
  - [components/river_core/river_session_coordinator.c](/root/ameba-river/components/river_core/river_session_coordinator.c)
- `dialog runtime` 新增 typed policy helper：
  - `river_dialog_runtime_wakeword_admission_block_reason()`
  - `river_dialog_runtime_allows_barge_in_interrupt()`
  - 这让 wakeword/barge-in 的准入语义回到 truth source 内部，而不是继续由
    coordinator 拉整份 snapshot 再自己拼规则
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `session_coordinator` 已删除：
  - `river_session_runtime_snapshot(...)`
  - `river_session_snapshot_playback_interruptible(...)`
  - wakeword 与 barge-in 路径现在直接消费 dialog runtime policy helper
  - 这进一步压缩 coordinator 的状态解释权，只保留事件编排与副作用
  - [components/river_core/river_session_coordinator.c](/root/ameba-river/components/river_core/river_session_coordinator.c)

## Step 5.289
- 云端 ASR lifecycle 事件现在不再先进入 `session_coordinator` 再由它桥接到
  `dialog runtime`：
  - 新增 `dialog runtime` 直接消费云端 ASR result 的入口：
    - `river_dialog_runtime_on_cloud_asr_result(...)`
  - 该入口当前直接吸收：
    - `SESSION_STARTED`
    - `SESSION_CLOSED`
    - `ERROR`
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- app 现在把 cloud ASR result 扇出给：
  - `dialog runtime`
  - `session_coordinator`
  其中：
  - `dialog runtime` 负责 lifecycle truth
  - `session_coordinator` 继续只负责：
    - partial/final 文本日志
    - barge-in 打断
    - interaction diag flush
  - [components/river_core/river_app.c](/root/ameba-river/components/river_core/river_app.c)
- `session_coordinator` 已删除对以下 lifecycle 桥接调用：
  - `note_asr_session_started_with_cloud_state(...)`
  - `note_asr_session_closed_with_cloud_state(...)`
  - `note_asr_error_with_cloud_state(...)`
  - 这继续把 `session_coordinator` 从“真相桥接层”压回“文本/策略辅助层”
  - [components/river_core/river_session_coordinator.c](/root/ameba-river/components/river_core/river_session_coordinator.c)

## Step 5.288
- XiaoZhi cloud runtime snapshot 中属于 playback/downlink 的字段，现已继续从
  `session.c` 下沉回 playback runtime 自己填充：
  - 新增 playback-owned snapshot helper：
    - `river_cloud_xiaozhi_fill_playback_runtime_snapshot(...)`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- playback runtime 现在统一负责向 cloud snapshot 投影下列播放真相：
  - `playback_active`
  - `playback_lane_engaged`
  - `playback_rebuffer_pending`
  - `playback_terminal_waiting`
  - `tts_stop_pending`
  - `playback_phase`
  - `playback_rebuffer_cause`
  - `playback_start_policy`
  - `playback_terminal_state`
  - `playback_terminal_reason`
  - `playback_terminal_wait_reason`
  - `playback_start_frames`
  - `playback_prefetch_frames`
  - `playback_start_cautious_history`
  - 这让 playback 真相对 cloud snapshot 的投影 ownership 与 status dump
    ownership 更一致，继续压缩 `session.c` 对播放内部细节的代管
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `river_cloud_xiaozhi_fill_runtime_snapshot(...)` 现在只保留会话/turn 语义的
  填充，并调用 playback runtime helper 吸收播放字段：
  - `conversation_window_active`
  - `listening`
  - `turn_accepted`
  - `barge_in_enabled`
  - `session_id` / `turn_id` / `accept_reason`
  - `input_state` / `output_state`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)

## Step 5.287
- `dialog runtime` 内部的 boot / wake / ASR cloud-backed 入口已继续收口成
  单一 typed reducer，而不再各自重复执行：
  - 抓 cloud snapshot
  - 写局部事实
  - publish
  这条样板流程
  - 新增内部 typed event：
    - `BOOT_READY`
    - `WAKE_CONFIRMED`
    - `ASR_SESSION_STARTED`
    - `ASR_SESSION_CLOSED`
    - `ASR_ERROR`
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 新增内部 helper：
  - `river_dialog_runtime_apply_cloud_event_locked(...)`
  - `river_dialog_runtime_commit_cloud_event(...)`
  - 统一负责：
    - 局部事实落入 runtime snapshot
    - 同步吸收当前 cloud snapshot
    - 发布 reason / interaction state
  - 这让 `dialog runtime` 更接近真正的 reducer 真相源，而不是保留多条同构
    的手写状态拼装路径
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 对外入口保持不变，但五条 cloud-backed public API 现在全部收口到同一内部
  reducer：
  - `river_dialog_runtime_mark_boot_ready_with_cloud_state(...)`
  - `river_dialog_runtime_note_wake_confirmed_with_cloud_state(...)`
  - `river_dialog_runtime_note_asr_session_started_with_cloud_state(...)`
  - `river_dialog_runtime_note_asr_session_closed_with_cloud_state(...)`
  - `river_dialog_runtime_note_asr_error_with_cloud_state(...)`
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)

## Step 5.286
- XiaoZhi playback runtime 已保存的 `start gate` 真相，现已继续上推到
  cloud/dialog runtime 的公开 snapshot，而不再只能停留在 playback runtime
  内部：
  - cloud runtime snapshot 新增：
    - `playback_start_policy`
    - `playback_start_frames`
    - `playback_prefetch_frames`
    - `playback_start_cautious_history`
  - dialog runtime snapshot 同步新增同名字段
  - [include/river/river_cloud.h](/root/ameba-river/include/river/river_cloud.h)
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
- XiaoZhi session 对 cloud runtime snapshot 的填充现在直接投影 playback
  runtime 已锁存的 start-gate snapshot：
  - 不重新计算门限
  - 只把 runtime 已经拥有的：
    - policy
    - start_frames
    - prefetch_frames
    - cautious_history
    继续向上透出
  - 这保证 dialog/core 消费到的仍是同一份 runtime-owned truth，而不是
    session 层二次派生的近似值
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- dialog runtime 现在会吸收并输出这组 start-gate 诊断真相：
  - `apply_cloud_snapshot_locked()` 同步保存这组字段
  - `river_dialog_runtime_dump_status()` 直接打印：
    - `start_gate`
    - `prefetch`
    - `cautious`
  - 这让上层状态与板端 status 不再只能看到：
    - `playback_phase`
    - `rebuffer_cause`
    而看不到当前实际起播门限
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)

## Step 5.285
- XiaoZhi playback start gate 现在不再只是调用点里的现算 helper，而是下沉成
  playback runtime 自己维护的稳定快照真相：
  - 新增 runtime snapshot 字段：
    - `xiaozhi_playback_start_policy`
    - `xiaozhi_playback_start_frames`
    - `xiaozhi_playback_prefetch_frames`
    - `xiaozhi_playback_start_cautious_history`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- runtime 现在显式区分三类 start-gate helper 角色：
  - `build_start_gate()` 负责从底层事实构造门限
  - `refresh_start_gate()` 负责在状态变更点刷新快照
  - `current_start_gate()` 负责让 worker / status / start / rebuffer 日志都读取
    同一份 runtime snapshot
  - 这让 start gate 进一步贴近“runtime owned truth”，不再由多个调用点各自拿
    原始字段现拼
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- start gate 快照会在真正影响门限的状态变更点被刷新：
  - playback meta 清理
  - playback/downlink reset
  - rebuffer note / rebuffer finish
  - clean segment 清掉 `streak`
  - `audio.out.meta` 更新 prefetch 目标
  - downlink frame duration 变更
  - 这保证“长段慢供给 + 恢复历史 + 当前 frame_ms”三类事实进入同一条
    runtime snapshot，而不是由读路径临时重建
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)

## Step 5.284
- XiaoZhi downlink/playback 的起播门限现在统一收口到显式 typed prefetch
  policy，而不是继续把“基础门限 / starvation 预取 / 恢复历史抬高”散落在多处
  helper 里分别推导：
  - 新增 start policy：
    - `baseline`
    - `rebuffer_fast`
    - `segment_prefetch`
    - `starved_prefetch`
  - 新增统一 start gate 结果：
    - `policy`
    - `start_frames`
    - `prefetch_frames`
    - `cautious_history`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `audio.out.meta` 观测到的 segment 节奏现在也能直接推动更保守的起播：
  - 若 `last_meta_gap_ms` 已经大于基础起播预算，则即便当前不在
    `UPSTREAM_STARVED`，runtime 也会切到 `segment_prefetch`
  - 这让“长段 / 慢供给 / 段边界 write_failed 抖动”不再只能依赖：
    - base start frames
    - rebuffer streak
    两个粗粒度信号
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- XiaoZhi playback 诊断日志也同步切到统一的 typed start-gate 语义：
  - status / prefetch / upstream-gap rebuffer / playback start / rebuffer
    requested 现在都会打印：
    - `policy`
    - `cautious`
    - `prefetch_frames`
  - `downlink_start_threshold_frames()` 本身也改为只返回统一
    `compute_start_gate()` 的结果，避免后续再长出新的门限分叉
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)

## Step 5.283
- XiaoZhi playback segment 现在显式记录本段是否经历过 rebuffer：
  - 新增 segment 字段：
    - `rebuffered`
  - 一旦某段在播放中进入 rebuffer，该段会锁存 `rebuffered=yes`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `rebuffer_streak` 的清零条件也随之收紧：
  - 不再因为“recover 后终于把这一段播完了”就立刻清掉
  - 现在只有：
    - 完整播完
    - 且该段本身没有经历 rebuffer
    的 clean segment，才会真正把 `streak` 清零
  - 若当前段是 recover 后才播完，runtime 会明确打印：
    - `reason=segment_recovered`
    并保留 `streak`
  - 若当前段是 clean segment，则打印：
    - `reason=clean_segment`
    并清掉 `streak`
  - 这让 `streak` 更接近“连续恢复历史仍未被稳定播放打断”的真实语义
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)

## Step 5.282
- XiaoZhi downlink/playback runtime 现在把“累计重缓冲次数”和“当前恢复门槛历史”
  拆开维护，避免历史 recover 永久放大后续 start gate：
  - 新增：
    - `xiaozhi_playback_rebuffer_streak`
    - `river_cloud_xiaozhi_playback_rebuffer_history_active()`
  - `xiaozhi_playback_rebuffer_count` 继续保留为累计诊断计数
  - `downlink_start_threshold_frames()` 改为只消费 active rebuffer history
    `streak`，而不是把累计总次数直接拿来抬高未来所有 restart 门槛
  - 在 segment 被完整播完后，runtime 会显式清掉该 `streak`，让播放链在
    “已经稳定跑完整段”的事实出现后回到更紧的起播门限
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- XiaoZhi playback 诊断日志也同步切到新的双指标语义：
  - `rebuffer_total`
  - `streak`
  - 这让板端日志可以区分：
    - 当前 response 到现在一共 rebuffer 了几次
    - 当前恢复门槛是不是仍被连续 recover 历史抬高
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- playback service 的 `stop/interrupt/flush/recover` public wrapper 现在会真实
  返回底层 control 结果，不再把内部失败静默吞掉后统一回 `RIVER_OK`：
  - 这意味着上层终于可以准确看到：
    - `recover` 是否真的原地成功
    - 是否已经退化到 `RESTART_PENDING` / `ERR_BUSY`
  - XiaoZhi runtime 里的 `recover_status` 日志也因此从“理论上会打印”变成了
    真正可触发的诊断入口
  - [components/river_voice/river_playback_service.c](/root/ameba-river/components/river_voice/river_playback_service.c)

## Step 5.281
- playback service 现在暴露显式的 `recover` 控制语义，而不再只让上层在
  `stop` / `flush` 之间自己猜测恢复方式：
  - 新增 API：
    - `river_playback_service_recover_stream_ex(...)`
    - `river_playback_service_recover_stream()`
  - 新增内部 control：
    - `RIVER_PLAYBACK_CONTROL_RECOVER`
  - recover 路径会先把状态推进到 `RIVER_PLAYBACK_RECOVERING`，再复用原有
    in-place flush/restart 逻辑，并在失败时保留 `RESTART_PENDING` 兜底
  - [include/river/river_playback_service.h](/root/ameba-river/include/river/river_playback_service.h)
  - [components/river_voice/river_playback_service.c](/root/ameba-river/components/river_voice/river_playback_service.c)
- XiaoZhi downlink/playback runtime 现在把两类 rebuffer 恢复统一收口到
  playback-service recover 语义：
  - `upstream_starved`
  - residual `write_failed`
  - 这意味着 runtime 不再在 recoverable rebuffer 路径上混用：
    - `stop_stream_ex(...)`
    - `flush_stream_ex(...)`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- 板端恢复日志语义也同步收口：
  - rebuffer diagnostics 现在统一打印 `recovery=recover`
  - `upstream_starved` 不再默认走一次完整 local stop/close，再由 worker
    重新 start 的重路径
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)

## Step 5.280
- `dialog runtime` 现在把本地 playback-service 回调先翻译成 typed local truth，
  再参与上层 playback 派生，不再在常态计算路径里直接读取 raw
  `river_playback_state_t`：
  - 新增 snapshot 字段：
    - `playback_local_active`
    - `playback_local_recovering`
  - 新增入口 helper：
    - `river_dialog_runtime_apply_local_playback_state_locked(...)`
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `playback_active` / `playback_recovering` 的派生现在优先消费：
  - cloud `playback_phase`
  - local typed playback truth
  - cloud `playback_lane_engaged` / `playback_rebuffer_pending`
  而不是在 reducer 内部继续散落地读旧的 playback-service coarse state
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `dialog_runtime_dump_status()` 现在额外导出本地 playback truth，便于板端继续
  区分：
  - local playback callback 还在不在
  - cloud playback phase 是否已成为主导真相
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)

## Step 5.279
- `dialog runtime` 的本地 playback 入口现在改为消费应用注册的对话流白名单，
  不再把所有 `RIVER_PLAYBACK_PRIO_TTS` 流都当成对话 owned stream：
  - 新增 API：
    - `river_dialog_runtime_register_playback_stream(const char *stream_name)`
  - `river_app` 启动时当前注册：
    - `xiaozhi_tts`
    - `iflytek_tts`
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
  - [components/river_core/river_app.c](/root/ameba-river/components/river_core/river_app.c)
- 一旦吸收某个已注册 dialog playback stream，runtime 现在会锁存该 stream
  name，并且只继续吸收这个 stream 自己的终态回调直到 `RIVER_PLAYBACK_IDLE`：
  - 新增：
    - `local_playback_stream_name`
    - `dialog_playback_streams[...]`
    - `river_dialog_runtime_matches_owned_playback_stream_locked(...)`
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- 这继续收紧了 `dialog runtime` 的本地 playback 真相边界：
  - 未来即使再出现其他共享 `TTS` 优先级流，也不会仅因 priority 相同就混入
    dialog-runtime 的 playback / interaction truth
  - 同时仍保留已拥有 stream 在 `config == NULL` 终态回调上的 clean close 行为
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)

## Step 5.278
- XiaoZhi playback runtime now classifies a subset of `write_failed` events as
  upstream starvation when the low-water queue budget has already been eaten by
  the supply gap:
  - added:
    - `river_cloud_xiaozhi_write_failed_prefers_starved_rebuffer(...)`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- The `write_failed` recovery branch now chooses between two typed recovery
  paths instead of always forcing local `flush/restart`:
  - `upstream_starved`:
    - `stop_stream_ex("xiaozhi_playback_starved_write")`
  - residual local write failure:
    - `flush_stream_ex("xiaozhi_playback_write_failed")`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- Recovery diagnostics now expose the reclassified context directly from the
  write-failure site:
  - `queued`
  - `low`
  - `supply_gap_ms`
  - `recovery=stop|flush`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)

## Step 5.277
- XiaoZhi playback runtime now records the timestamp of the last successful
  downlink supply instead of treating starvation as an `queued=0`-only event:
  - added:
    - `xiaozhi_downlink_last_supply_ms`
  - successful `audio_event` ring writes now refresh that supply timestamp
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- Upstream-starvation rebuffering now uses a low-water + supply-gap model:
  - added low-water helper:
    - `river_cloud_xiaozhi_downlink_starved_low_water_frames()`
  - `river_cloud_xiaozhi_maybe_rebuffer_starved(...)` now consumes:
    - queued frames
    - last supply timestamp
    - adaptive starvation wait
  - diagnostics now expose:
    - `supply_gap_ms`
    - `queued`
    - `low`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- The downlink worker now checks starvation before the queue fully drains and
  no longer clears the starvation watch on every non-zero queue/write loop,
  which lets the runtime convert part of the old
  `underrun -> write_failed -> flush/restart` path into an earlier controlled
  `upstream_starved` rebuffer
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)

## Step 5.276
- Tightened `dialog runtime` local playback ingress so it only absorbs
  dialog-related playback-service streams instead of every shared local
  playback state transition:
  - added local ownership latch:
    - `local_playback_stream_owned`
  - added stream classifier:
    - `river_dialog_runtime_is_dialog_playback_stream(...)`
  - current dialog-stream rule:
    - `RIVER_PLAYBACK_PRIO_TTS`
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- Once `dialog runtime` has observed an owned dialog stream, it now keeps
  absorbing its cleared-config terminal transitions until `IDLE`, so the local
  playback truth still closes cleanly across:
  - `RECOVERING`
  - `RESTART_PENDING`
  - `IDLE`
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- This stops shared playback-service foreign streams such as
  `audio_echo` (`RIVER_PLAYBACK_PRIO_DEBUG`) from polluting dialog-runtime
  playback / interaction truth while preserving the existing TTS-driven local
  playback ingress path
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
  - [components/river_voice/river_voice_echo.c](/root/ameba-river/components/river_voice/river_voice_echo.c)

## Step 5.275
- XiaoZhi playback runtime now maintains an explicit local playback-backend
  truth instead of scattering raw checks across:
  - `xiaozhi_playback_active`
  - `river_playback_service_active()`
  - `RIVER_PLAYBACK_RESTART_PENDING`
  - added backend states:
    - `detached`
    - `owned_active`
    - `foreign_active`
    - `restart_pending`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- Downlink start/rebuffer/terminal cleanup now consume that typed backend
  state, which makes ownership boundaries explicit:
  - rebuffer starvation now only watches an `owned_active` backend
  - rebuffer resume now only auto-finishes when XiaoZhi still owns the running
    backend
  - pending-stop and abort paths no longer treat any active playback-service
    stream as XiaoZhi-owned
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- Playback diagnostics now print backend ownership directly in phase/start/
  status/abort logs so future board traces can separate:
  - XiaoZhi-owned playback churn
  - foreign stream takeover
  - restart-pending recovery
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)

## Step 5.274
- `dialog runtime` now directly owns the local playback-service listener
  ingress:
  - added:
    - `river_dialog_runtime_on_playback_state(...)`
  - `river_app` now registers that callback straight into
    `river_playback_service_register_listener(...)`
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
  - [components/river_core/river_app.c](/root/ameba-river/components/river_core/river_app.c)
- Removed the now-obsolete playback listener bridge from
  `session_coordinator`:
  - removed:
    - `river_session_coordinator_on_playback_state(...)`
  - [components/river_core/river_session_coordinator.c](/root/ameba-river/components/river_core/river_session_coordinator.c)
  - [components/river_core/river_session_coordinator.h](/root/ameba-river/components/river_core/river_session_coordinator.h)

## Step 5.273
- Removed the now-unused local-only `dialog_runtime` boot/wake/ASR entrypoints
  after all active callers had moved to the atomic cloud-fused APIs:
  - removed:
    - `river_dialog_runtime_mark_boot_ready(...)`
    - `river_dialog_runtime_note_wake_confirmed(...)`
    - `river_dialog_runtime_note_asr_session_started(...)`
    - `river_dialog_runtime_note_asr_session_closed(...)`
  - kept:
    - atomic boot/wake/ASR cloud-fused reducers
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)

## Step 5.272
- `dialog runtime` now owns atomic cloud-fused entrypoints for the remaining
  boot/ASR lifecycle facts:
  - `river_dialog_runtime_mark_boot_ready_with_cloud_state(...)`
  - `river_dialog_runtime_note_asr_session_started_with_cloud_state(...)`
  - `river_dialog_runtime_note_asr_session_closed_with_cloud_state(...)`
  - `river_dialog_runtime_note_asr_error_with_cloud_state(...)`
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `river_app` and `session_coordinator` no longer keep any explicit
  `sync_cloud_state(...)` bridge calls:
  - boot-ready now enters dialog runtime through the atomic boot API
  - ASR lifecycle now enters dialog runtime through atomic cloud-fused ASR APIs
  - [components/river_core/river_app.c](/root/ameba-river/components/river_core/river_app.c)
  - [components/river_core/river_session_coordinator.c](/root/ameba-river/components/river_core/river_session_coordinator.c)
- Removed the temporary provider capability API
  `river_cloud_adapter_runtime_self_sync_active()` because coordinator no
  longer needs provider-specific sync fallback logic
  - [include/river/river_cloud.h](/root/ameba-river/include/river/river_cloud.h)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.271
- `dialog runtime` now provides an atomic wake-admission fusion entrypoint:
  - `river_dialog_runtime_note_wake_confirmed_with_cloud_state(...)`
  - this lets dialog runtime absorb:
    - local `wake_confirmed`
    - current cloud runtime snapshot
    in one ownership boundary instead of forcing the caller to sequence
    `note_wake_confirmed()` and `sync_cloud_state()`
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `session_coordinator` wake-admission success paths now call that single
  dialog-runtime entrypoint:
  - worker wake admission path
  - inline fallback wake admission path
  - explicit wakeword `sync_cloud_state("wakeword_detected")` bridge has been
    removed from coordinator
  - [components/river_core/river_session_coordinator.c](/root/ameba-river/components/river_core/river_session_coordinator.c)

## Step 5.270
- Added a public cloud capability bit so `river_core` can tell whether the
  active provider/runtime will self-publish runtime state-sync events:
  - `river_cloud_adapter_runtime_self_sync_active()`
  - [include/river/river_cloud.h](/root/ameba-river/include/river/river_cloud.h)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- XiaoZhi session runtime now self-publishes cloud state sync for ASR lifecycle
  events after emitting the corresponding result:
  - `asr_error`
  - `asr_session_started`
  - `asr_session_closed`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- `session_coordinator` now keeps the ASR lifecycle cloud-snapshot pull only as
  a fallback for providers that do not self-sync runtime truth:
  - XiaoZhi path no longer re-pulls cloud snapshot from coordinator after those
    ASR lifecycle events
  - non-self-sync providers keep the previous compatibility fallback
  - [components/river_core/river_session_coordinator.c](/root/ameba-river/components/river_core/river_session_coordinator.c)

## Step 5.269
- `dialog runtime` now directly owns the cloud state-sync callback ingress:
  - `river_app` registers `river_dialog_runtime_on_cloud_state_sync(...)`
    straight into `river_cloud_adapter_set_state_sync_handler(...)`
  - app no longer acts as an intermediate bridge just to forward cloud-state
    sync into dialog runtime
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
  - [components/river_core/river_app.c](/root/ameba-river/components/river_core/river_app.c)
- `session_coordinator` playback listener no longer re-pulls the cloud runtime
  snapshot on every local playback-service state change:
  - `river_session_coordinator_on_playback_state(...)` now only absorbs local
    playback-service truth into `dialog runtime`
  - the stale `river_session_coordinator_sync_interaction_state(...)` bridge
    has been removed
  - [components/river_core/river_session_coordinator.c](/root/ameba-river/components/river_core/river_session_coordinator.c)
  - [components/river_core/river_session_coordinator.h](/root/ameba-river/components/river_core/river_session_coordinator.h)

## Step 5.268
- XiaoZhi playback runtime now proactively publishes cloud state-sync when its
  own playback phase truth changes, instead of relying only on the outer
  playback-service listener path to pull a fresh snapshot later
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- Explicit rebuffer-cause changes now also request state-sync even when the
  phase itself does not change, so upper layers can observe the latest
  `playback_rebuffer_cause` without waiting for a separate outer poll path
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)

## Step 5.267
- Split XiaoZhi downlink restart gating by explicit `rebuffer_cause` instead of
  forcing `write_failed` and `upstream_starved` to share the same refill
  threshold:
  - upstream starvation recovery still waits for prefetch-sized refill when
    needed
  - local `write_failed` recovery now uses the tighter base rebuffer gate
    rather than inheriting the upstream-prefetch wait budget
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- Playback status dump now also prints the current downlink `start` threshold so
  the active restart gate can be verified directly from runtime diagnostics
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)

## Step 5.266
- Introduced an explicit XiaoZhi playback runtime `rebuffer_cause` truth so
  recovery no longer collapses all churn into a single boolean:
  - `upstream_starved`
  - `write_failed`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- Exported that cause through the cloud/dialog snapshot path:
  - cloud runtime snapshot now exposes `playback_rebuffer_cause`
  - dialog runtime snapshot/dump now preserves and prints the same cause
  - [include/river/river_cloud.h](/root/ameba-river/include/river/river_cloud.h)
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- Playback/downlink diagnostics now log recovery cause directly at phase
  transitions, prefetch/start, rebuffer trigger, resume, and session dump time
  instead of requiring manual log correlation across multiple branches

## Step 5.265
- Tightened dialog runtime `playback_recovering` to phase-first semantics as
  well:
  - once `playback_phase` is known, XiaoZhi recovery truth now comes from
    phase/rebuffer facts instead of `playback_state == recovering/restart_pending`
  - playback-service recovery states remain only as fallback when phase is not
    available
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- This closes a remaining gap where bottom-layer `restart_pending` could still
  leak into dialog runtime recovering state even after phase truth had already
  declared playback idle or otherwise stable

## Step 5.264
- Suppressed no-op dialog runtime playback-state publishes when XiaoZhi
  `playback_phase` is already known and the incoming playback-service state
  change does not alter any effective playback fact:
  - `playback_active`
  - `playback_recovering`
  - `error_recovering`
  - derived interaction state
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- This reduces residual upper-layer churn where playback-service state changes
  could still repeatedly poke dialog runtime during recovery windows even after
  phase-first truth had already stabilized the effective playback view

## Step 5.263
- Aligned the cloud runtime snapshot with the XiaoZhi playback runtime phase by
  exporting `playback_active` as output-active semantics instead of the old raw
  `playback_active` flag:
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Switched dialog runtime playback derivation to phase-first semantics:
  - local helpers now interpret `playback_phase`
  - `playback_recovering` prefers `rebuffering`
  - `playback_active` now trusts runtime phase first and only falls back to
    service/bool compatibility when phase is unavailable
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- This narrows the remaining gap where upper layers could still treat
  playback-service activity as the primary truth during XiaoZhi recovery/drain
  windows, even after the playback runtime had exported a stricter phase truth

## Step 5.262
- Introduced an explicit XiaoZhi playback runtime phase truth source and
  exported it through the cloud/dialog snapshot path:
  - runtime phase now collapses scattered local playback facts into:
    - `idle`
    - `prefetching`
    - `playing`
    - `rebuffering`
    - `draining`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- XiaoZhi playback lane predicates now consume that runtime phase instead of
  re-deriving occupancy from scattered booleans plus playback-service state:
  - `river_cloud_xiaozhi_playback_output_active()`
  - `river_cloud_xiaozhi_playback_lane_engaged()`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- The phase is now visible to higher layers and diagnostics:
  - cloud runtime snapshot exports `playback_phase`
  - dialog runtime snapshot/dump now log cloud playback as `active/phase`
  - XiaoZhi session dump prints the runtime phase directly
  - [include/river/river_cloud.h](/root/ameba-river/include/river/river_cloud.h)
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)

## Step 5.261
- Tightened XiaoZhi downlink rebuffer semantics so `rebuffer_pending` is now a
  real resume gate in the worker instead of only a flag:
  - worker waits until queued frames reach the adaptive rebuffer threshold
    before resuming writes
  - existing active playback sessions only clear rebuffer after that gate is met
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- This directly targets the repeated recovery storm where the worker could
  `flush` after a write failure and then immediately retry the same frame
  without refilling enough downlink backlog first
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)

## Step 5.260
- Moved the last generic realtime capture open reducer out of
  `river_cloud_adapter.c` and into the shared ASR bridge runtime:
  - `river_cloud_business_time_ready(...)`
  - `river_cloud_stream_open_and_flush(...)`
  - [components/river_cloud/river_cloud_asr_bridge_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_asr_bridge_runtime.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Adapter no longer owns any generic realtime capture reducer implementation;
  it now consumes bridge-runtime helpers for:
  - bridge-state prepare/reset
  - pre-roll mutation
  - stream-open flush
  - active-stream finish
  while keeping only orchestration and backend dispatch
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.259
- Moved generic realtime capture reducer helpers out of `river_cloud_adapter.c`
  and into the shared ASR bridge runtime:
  - `river_cloud_pre_roll_reset(...)`
  - `river_cloud_pre_roll_store(...)`
  - `river_cloud_stream_finish_active(...)`
  - [components/river_cloud/river_cloud_asr_bridge_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_asr_bridge_runtime.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Adapter now keeps only the generic `stream_open_and_flush()` reducer on the
  non-XiaoZhi realtime path, while pre-roll mutation and active-stream finish
  are owned by the bridge runtime module
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.258
- Added a generic ASR bridge runtime module so adapter no longer directly owns
  the base capture bridge state allocation/reset for:
  - `audio_desc`
  - `frame_bytes`
  - `pre_roll_buffer`
  - `pre_roll_capacity_frames`
  - `post_roll_frames`
  - `silence_frames`
  - [components/river_cloud/river_cloud_asr_bridge_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_asr_bridge_runtime.c)
  - [components/river_cloud/CMakeLists.txt](/root/ameba-river/components/river_cloud/CMakeLists.txt)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Adapter `river_cloud_asr_audio_open()/close()` now delegates bridge-state
  prepare/reset to runtime helpers:
  - `river_cloud_prepare_audio_bridge_state(...)`
  - `river_cloud_reset_audio_bridge_state()`
  which leaves adapter focused on open/close sequencing and backend-specific
  policy hooks
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.257
- Removed the XiaoZhi-only adapter shim `river_cloud_xiaozhi_stream_push_frame(...)`
  so the generic capture dispatch now routes directly from
  `river_cloud_asr_stream_push_frame(...)` into the runtime-owned helper
  `river_cloud_xiaozhi_apply_capture_stream_policy(...)`
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- Adapter XiaoZhi capture dispatch is now reduced further to:
  - shared bridge argument validation
  - streaming capability check
  - backend branch to runtime
  which keeps XiaoZhi capture ownership in runtime instead of a provider-local
  adapter wrapper
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.256
- Moved the last XiaoZhi `stream_push_frame()` playback wrapper out of
  `river_cloud_adapter.c` so adapter no longer directly owns:
  - capture-entry playback gating
  - capture-exit playback tail
  around the runtime-owned stream-push session reducer
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- Session runtime now owns the grouped capture-stream wrapper:
  - `river_cloud_xiaozhi_apply_capture_stream_policy(...)`
  - playback entry gate
  - runtime-owned stream-push capture reducer dispatch
  - playback exit tail after successful capture progress
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Adapter `river_cloud_xiaozhi_stream_push_frame(...)` is now reduced to input
  validation plus a single runtime call:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.255
- Moved the remaining XiaoZhi `stream_push_frame()` capture/session reducer
  out of `river_cloud_adapter.c` so adapter no longer directly owns:
  - inactive-stream followup-open branching
  - pre-roll replay into uplink on stream activation
  - active-stream per-frame feed bookkeeping
  - `stream_active` activation log / stats transition
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- Session runtime now owns the grouped stream-push capture reducer:
  - `river_cloud_xiaozhi_apply_stream_push_capture_policy(...)`
  - inactive-stream capture-open / pre-roll replay reducer
  - active-stream PCM feed + `stream_feed_ok/fail` bookkeeping
  - stream activation `asr stream active` log and stats snapshot
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Generic bridge helpers needed by that runtime reducer are now shared through
  the internal boundary instead of remaining adapter-local statics:
  - `river_cloud_log_stream_open_deferred_once(...)`
  - `river_cloud_reset_stream_open_deferred_state()`
  - `river_cloud_pre_roll_store(...)`
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)

## Step 5.254
- Moved the remaining XiaoZhi bridge-open capture/uplink init glue out of
  `river_cloud_adapter.c` so adapter no longer directly owns:
  - uplink audio format validation
  - XiaoZhi pre-roll cap normalization
  - uplink ring init/reset and counter reset
  - bridge-open listen-gate logging
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- Session runtime now owns the grouped bridge-open capture/uplink reducer:
  - `river_cloud_xiaozhi_apply_bridge_open_capture_policy(...)`
  - uplink ring lifecycle normalization for bridge open
  - uplink timestamp / retry / busy metrics reset
  - `xiaozhi_open_speech_frames` reset and listen-gate projection
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Adapter `river_cloud_asr_audio_open()` now only delegates the XiaoZhi
  bridge-open capture/uplink policy to runtime around its generic bridge
  allocation path, and `river_cloud_asr_audio_close()` no longer carries a
  leftover XiaoZhi-only `xiaozhi_open_speech_frames` reset:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.253
- Moved the XiaoZhi bridge-close capture teardown glue out of
  `river_cloud_adapter.c` so adapter no longer directly owns:
  - bridge-close `complete_active_stream_finish(...)` gating
  - bridge-close sequencing between active-stream finish and terminal policy
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- Session runtime now owns the grouped bridge-close capture reducer:
  - `river_cloud_xiaozhi_apply_bridge_close_capture_policy()`
  - optional active-stream finish on bridge close
  - bridge-close terminal policy dispatch
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Adapter `river_cloud_asr_audio_close()` now only delegates the XiaoZhi
  bridge-close session/capture policy to runtime, then closes the encoder and
  runs generic bridge teardown:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.252
- Moved the XiaoZhi active-stream capture tail policy out of
  `river_cloud_adapter.c` so adapter no longer directly owns:
  - `speech_resumed` endpoint-soft-close cancel
  - `silence_frames` post-push bookkeeping
  - post-roll/min-active gating
  - duplex soft-endpoint vs local stream-finish branching
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- Session runtime now owns the grouped active-stream capture reducer:
  - `river_cloud_xiaozhi_apply_active_stream_capture_policy(...)`
  - speech-resumed cancel/reset
  - silence/post-roll progression
  - `arm_endpoint_soft_close(...)` vs
    `complete_active_stream_finish(...)`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Adapter XiaoZhi stream-push path now only pushes PCM and delegates the
  active-stream capture/session policy to runtime:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.251
- Moved the XiaoZhi capture-entry playback glue out of
  `river_cloud_adapter.c` so adapter no longer directly owns:
  - capture-path `river_cloud_xiaozhi_playback_check_pending_stop()`
  - duplex hold / fallback logging on capture entry
  - pre-roll reset when playback keeps capture closed
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- Playback runtime now owns the grouped capture-entry/exit playback policies:
  - `river_cloud_xiaozhi_apply_capture_entry_playback_policy()`
  - `river_cloud_xiaozhi_apply_capture_exit_playback_policy()`
  - capture-entry pending-stop observation
  - duplex-held capture gate and fallback log
  - capture-exit pending-stop observation
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Adapter XiaoZhi stream-push path now only wraps pre-roll/followup/uplink
  scheduling around the runtime-owned playback helpers:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.250
- Moved the XiaoZhi session reset / session-start playback cleanup policy out
  of `river_cloud_xiaozhi_session.c` so session runtime no longer directly
  owns:
  - `river_cloud_xiaozhi_reset_downlink_state()`
  - `river_cloud_xiaozhi_clear_playback_meta_state()`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Playback runtime now owns the grouped playback cleanup reducers for those
  entry points:
  - `river_cloud_xiaozhi_apply_transport_reset_playback_policy()`
  - `river_cloud_xiaozhi_apply_session_start_playback_policy()`
  - transport-reset downlink reset + playback-meta clear
  - session-start playback-meta clear
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Session transport reset and open/listen round start now only call the
  runtime-owned playback policy helpers instead of mutating playback/downlink
  cleanup state inline:
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)

## Step 5.249
- Moved the XiaoZhi terminal-close playback tail out of session-runtime
  terminal branches and the adapter bridge-close path so they no longer
  directly own:
  - `playback_has_work()` gating before terminal abort
  - per-cause `playback_abort_for_cause(...)` dispatch in
    `transport_closed / network_lost / bridge_close`
  - adapter bridge-close decoder-tail invocation
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- Playback runtime now owns the grouped terminal-close playback reducer:
  - `river_cloud_xiaozhi_apply_terminal_playback_policy(...)`
  - grouped abort dispatch by typed terminal cause
  - terminal decoder teardown tail
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Session runtime terminal policies and adapter bridge-close path now only
  call the runtime-owned playback terminal reducer:
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.248
- Moved the remaining XiaoZhi playback backend-refresh and bridge-close tail
  glue out of `river_cloud_adapter.c` and into playback runtime so adapter no
  longer directly owns:
  - downlink worker bootstrap on backend init / config refresh
  - playback-state reset on backend refresh
  - decoder close on bridge-close teardown
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- Playback runtime now owns the adapter-facing lifecycle helpers for those
  media concerns:
  - `river_cloud_xiaozhi_apply_playback_backend_refresh_policy(...)`
  - `river_cloud_xiaozhi_apply_bridge_close_playback_tail(...)`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Adapter XiaoZhi init / config-refresh / bridge-close paths now only call the
  runtime-owned playback helpers instead of mutating playback lifecycle state
  inline:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.247
- Moved the XiaoZhi uplink I/O service loop out of `river_cloud_adapter.c`
  and into session runtime so adapter no longer directly owns:
  - uplink busy-backoff calculation
  - backpressure logging
  - retry-frame drain and resend pacing
  - uplink timestamp advance and round packet-sent accounting
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Session runtime now owns the full XiaoZhi uplink service reducer path:
  - `river_cloud_xiaozhi_run_uplink_io_once(...)`
  - retry-preserved frame drain
  - send-ready gating
  - stale-frame trim on send/backpressure paths
  - transport timestamp progression
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Adapter `river_cloud_xiaozhi_io_task(...)` now only schedules the runtime
  helper and the transport send shell:
  - `river_cloud_xiaozhi_run_uplink_io_once(...)`
  - `river_cloud_xiaozhi_send_uplink_transport(...)`
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.246
- Moved the XiaoZhi control-request submission path out of
  `river_cloud_adapter.c` and into session runtime so adapter no longer
  directly owns:
  - control queue writes
  - queue high-water accounting
  - sync completion setup for session-side control requests
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Session runtime now owns the full control-queue reducer path for XiaoZhi:
  - request initialization
  - sync request submission
  - async request submission
  - queue drain and completion signaling
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- XiaoZhi session-side request wrappers were moved with that queue ownership:
  - `river_cloud_xiaozhi_request_open_and_listen(...)`
  - `river_cloud_xiaozhi_request_listen_stop(...)`
  - `river_cloud_xiaozhi_request_abort(...)`
  - `river_cloud_xiaozhi_request_close_session(...)`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter `river_cloud_xiaozhi_io_task(...)` now only invokes the runtime-owned
  queue drain helper and no longer carries local control-queue logic:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.245
- Moved the last XiaoZhi control-transport dispatch shell out of
  `river_cloud_adapter.c` and into runtime so adapter control execution is now
  a one-line delegating shim:
  - `river_cloud_xiaozhi_execute_control_transport(...)`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Session runtime now owns the full XiaoZhi control-op dispatch shell for:
  - session-side transport control reducers
  - playback ACK transport reducers
  - grouped op routing across the runtime-owned helpers
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter `river_cloud_xiaozhi_control_execute(...)` now only forwards the
  prepared control request into the runtime-owned reducer and no longer
  retains any XiaoZhi op-type switch ownership:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.244
- Moved the remaining XiaoZhi playback ACK transport-control execution out of
  `river_cloud_adapter.c` and into playback runtime so adapter no longer
  directly sends:
  - `audio_out.started`
  - `audio_out.mark`
  - `audio_out.cleared`
  - `audio_out.completed`
  - `river_cloud_xiaozhi_execute_playback_control_transport(...)`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Playback runtime now owns the grouped ACK transport reducer for:
  - transport send
  - sent/failed logging
  - negotiated playback-ack mode and last-error projection on failures
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- Adapter control executor now collapses all four `PLAYBACK_*` control ops into
  one runtime-owned transport helper call:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.243
- Moved the remaining XiaoZhi session-side transport control execution out of
  `river_cloud_adapter.c` and into session runtime so adapter no longer
  directly executes:
  - `RIVER_CLOUD_XIAOZHI_CTRL_LISTEN_STOP`
  - `RIVER_CLOUD_XIAOZHI_CTRL_ABORT`
  - `RIVER_CLOUD_XIAOZHI_CTRL_CLOSE_SESSION`
  - `river_cloud_xiaozhi_execute_session_control_transport(...)`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Session runtime now owns the grouped transport reducer for those control ops:
  - `listen_stop` gating against `river_cloud_xiaozhi_uplink_send_ready()`
  - `abort` gating against `river_xiaozhi_session_open()`
  - direct session close transport teardown
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter control executor now only forwards those session-side control ops
  into the runtime-owned helper:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.242
- Moved the XiaoZhi `OPEN_AND_LISTEN` transport-control execution shell out of
  `river_cloud_adapter.c` and into session runtime so adapter no longer
  directly opens the websocket session, copies transport session-id state, or
  gates `listen_start` inline:
  - `river_cloud_xiaozhi_execute_open_and_listen_transport(...)`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Session runtime now owns the grouped `OPEN_AND_LISTEN` transport reducer for:
  - `river_xiaozhi_open_session()`
  - `river_cloud_xiaozhi_copy_session_id_from_transport()`
  - `river_cloud_xiaozhi_listening_active()` guard before
    `river_xiaozhi_send_listen_start(...)`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter control executor now only forwards the request payload into the
  runtime-owned transport helper for `RIVER_CLOUD_XIAOZHI_CTRL_OPEN_AND_LISTEN`:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.241
- Moved the XiaoZhi I/O-loop session-housekeeping glue out of
  `river_cloud_adapter.c` and into runtime-owned helpers so adapter no longer
  inlines the `io_tick/poll` maintenance sequence:
  - `river_cloud_xiaozhi_run_io_tick_housekeeping(...)`
  - `river_cloud_xiaozhi_run_post_poll_housekeeping(...)`
  - `river_cloud_xiaozhi_run_post_uplink_housekeeping(...)`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Session runtime now owns the grouped I/O-loop reducers for:
  - `io_tick` turn-semantics refresh and conversation-window timeout checks
  - post-poll accepted-turn pending-text finalize without a second refresh pass
  - post-uplink playback pending-stop, endpoint soft-close timeout, and local
    close timeout housekeeping
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter XiaoZhi I/O task is reduced further to transport scheduling order
  only:
  - control queue
  - websocket poll
  - uplink service
  - fairness delay
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.240
- Moved the last XiaoZhi transport-event dispatch shell out of
  `river_cloud_adapter.c` and into session runtime so adapter event callback is
  now transport-only:
  - `river_cloud_xiaozhi_handle_transport_event(...)`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Session runtime now owns both:
  - the per-event `river_cloud_xiaozhi_refresh_turn_semantics("event")`
  - the full XiaoZhi transport event switch for:
    - `SERVER_HELLO`
    - `STT`
    - `INPUT_*`
    - `AUDIO_OUT_META`
    - `LLM`
    - `TTS`
    - `AUDIO`
    - `SESSION_CLOSED`
    - `ERROR`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter `river_cloud_xiaozhi_event_handler(...)` now only forwards the raw
  transport event into the runtime-owned reducer and no longer retains any
  inline XiaoZhi event-type ownership:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.239
- Moved the remaining XiaoZhi `SERVER_HELLO` transport-event observation out of
  `river_cloud_adapter.c` and into session runtime so adapter no longer owns
  transport sample-rate/frame-duration/session-id synchronization inline:
  - `river_cloud_xiaozhi_note_server_hello_observation(...)`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Session runtime now owns the `SERVER_HELLO` observation reducer for:
  - server sample-rate truth
  - server frame-duration truth
  - session-id sync from transport cache
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter XiaoZhi `SERVER_HELLO` branch now only dispatches the transport
  observation into the runtime-owned reducer:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.238
- Moved the remaining XiaoZhi `STT` event follow-up window touch out of
  `river_cloud_adapter.c` and into the existing session-runtime observation
  helper so adapter `RIVER_XIAOZHI_EVENT_STT` handling is now pure dispatch:
  - `river_cloud_xiaozhi_note_stt_observation(...)`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- Session runtime `river_cloud_xiaozhi_note_stt_observation(...)` now owns
  both:
  - `stt` follow-up window touch
  - pending-text dedup / partial ASR emission
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter XiaoZhi `STT` branch no longer carries any session-semantics glue
  before delegating to runtime:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.237
- Moved the remaining XiaoZhi input-side transport glue for
  `input_speech_start / input_preview / input_endpoint` out of
  `river_cloud_adapter.c` and into a session-runtime-owned reducer so adapter
  no longer embeds preview-window touch, endpoint soft-close transitions, or
  preview interrupt hints inline:
  - `river_cloud_xiaozhi_note_input_observation(...)`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Session runtime now owns the grouped input-event reducer for:
  - `input_speech_start` follow-up window touch, endpoint-soft-close cancel,
    and interrupt hint
  - `input_preview` follow-up window touch, preview note, and text-driven
    soft-close cancel
  - `input_endpoint` follow-up window touch, preview note, and endpoint
    arm/cancel policy
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter `river_cloud_xiaozhi_event_handler(...)` now collapses those three
  input transport branches into one runtime-owned reducer call:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.236
- Moved the remaining XiaoZhi transport-event semantic branches for
  `audio_out_meta / session_closed / error` out of
  `river_cloud_adapter.c` and into runtime-owned helpers so adapter no longer
  embeds playback-fact logging, transport-closed terminal handling, or cloud
  error ASR emission inline:
  - `river_cloud_xiaozhi_note_audio_out_meta_observation(...)`
  - `river_cloud_xiaozhi_note_session_closed_observation(...)`
  - `river_cloud_xiaozhi_note_error_observation(...)`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Playback runtime now owns the `audio_out_meta` observation reducer for:
  - follow-up window touch during playback-meta arrival
  - playback meta note + prefetch truth update
  - playback fact log projection with accepted-turn visibility
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- Session runtime now owns the remaining transport terminal/error reducers for:
  - `session_closed` diagnostic log + transport-closed terminal policy apply
  - `error` message normalization + `RIVER_CLOUD_ASR_EVENT_ERROR` emission
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter `river_cloud_xiaozhi_event_handler(...)` now only dispatches
  `AUDIO_OUT_META / SESSION_CLOSED / ERROR` transport observations into
  runtime-owned reducers:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.235
- Moved XiaoZhi `LLM/TTS` transport-event observation ownership out of
  `river_cloud_adapter.c` and into session runtime so adapter no longer embeds
  wake-window extension, pending-text finalize, or TTS round-policy mutation
  inline:
  - `river_cloud_xiaozhi_note_llm_observation(...)`
  - `river_cloud_xiaozhi_note_tts_observation(...)`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Session runtime now owns the XiaoZhi `LLM/TTS` observation reducers for:
  - `llm` follow-up window touch, observation log, and round-policy apply
  - `tts start` pending-text finalize, keep-open policy, and playback-stop
    cancel sequencing
  - `tts sentence_start` last-text mutation and sentence-start log
  - `tts stop` round-stop policy
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter `river_cloud_xiaozhi_event_handler(...)` now only dispatches XiaoZhi
  `LLM/TTS` transport events into runtime-owned observation helpers instead of
  mutating session truth inline:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.234
- Moved the remaining XiaoZhi uplink-ingress / active-stream-finish helper set
  out of `river_cloud_adapter.c` and into session runtime so adapter no longer
  owns uplink queue trimming, PCM accumulator packetization, or padded
  stream-finish flushing inline:
  - `river_cloud_xiaozhi_trim_uplink_stale_frames(...)`
  - `river_cloud_xiaozhi_push_pcm(...)`
  - `river_cloud_xiaozhi_complete_active_stream_finish(...)`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Session runtime now owns the uplink-ingress reducer set for:
  - stale-tail trimming before ring write
  - overflow drop logging / ring-drop accounting
  - PCM accumulator framing into uplink packets
  - padded flush on active-stream finish
  - finish -> listen-stop finalize bridging
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter XiaoZhi capture/I/O paths now only:
  - call runtime helpers for uplink ingress and stream finish
  - retain the actual transport send path
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.233
- Moved XiaoZhi ASR partial/final emission accounting out of
  `river_cloud_adapter.c` and into a session-runtime-owned helper so adapter
  no longer mutates ASR-round emission counters inline while delivering generic
  cloud ASR results:
  - `river_cloud_xiaozhi_note_asr_result_emitted(...)`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Session runtime now owns the reducer that tracks XiaoZhi ASR result emission
  facts for the active round:
  - `partial_seen`
  - `partial_count`
  - `final_seen`
  - `final_count`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter `river_cloud_emit_asr_result(...)` now only constructs the generic
  ASR result payload and delegates XiaoZhi-specific emission bookkeeping to
  runtime before notifying listeners:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.232
- Collapsed the XiaoZhi I/O-loop work-presence gate out of
  `river_cloud_adapter.c` and into a runtime-owned helper so adapter no longer
  reconstructs whether the XiaoZhi runtime still has active control/transport/
  uplink work:
  - `river_cloud_xiaozhi_io_has_work()`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Session runtime now owns the underlying reducer that decides whether the
  XiaoZhi I/O loop should stay in its active poll cadence:
  - control-queue pending state
  - transport-active state
  - retry-aware uplink-active state
  - exported `river_cloud_xiaozhi_uplink_active()` truth reused by adapter's
    uplink service path
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter XiaoZhi I/O task now consumes a single runtime-owned work predicate
  instead of locally chaining three state checks inline:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.231
- Moved the remaining XiaoZhi control/uplink/ASR-round diagnostic projection
  out of `river_cloud_adapter.c` and into session-runtime-owned helpers so the
  adapter dump path no longer formats those runtime-truth fields inline:
  - `river_cloud_xiaozhi_dump_io_status()`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Session runtime now owns the diagnostic projection for:
  - control queue occupancy / owner state
  - uplink queue occupancy / retry-aware ready frame truth
  - ASR round summary state
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- The retry-aware uplink-ready frame calculation was also moved out of adapter
  and exported from runtime so both diagnostics and adapter transport logic now
  consume the same runtime-owned queue-count helper:
  - `river_cloud_xiaozhi_uplink_ready_frames()`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.230
- Moved the remaining XiaoZhi playback/downlink diagnostic projection out of
  `river_cloud_adapter.c` and into a playback-runtime-owned helper so adapter
  status dumping no longer formats playback-truth fields inline:
  - `river_cloud_xiaozhi_dump_playback_status(uint64_t now_ms)`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Playback runtime now owns the diagnostic projection for:
  - playback meta / segment identity / expected duration truth
  - playback terminal / tail-wait / last-heard segment truth
  - duplex-ready / negotiated-duplex policy visibility
  - no-ref reopen guard state
  - downlink queue / rebuffer observation
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- Adapter `river_cloud_adapter_dump_status()` now only delegates XiaoZhi
  session and playback diagnostic blocks to runtime before printing generic
  control/uplink/ASR queue statistics:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.229
- Moved the XiaoZhi session/preview/turn-semantics diagnostic dump out of
  `river_cloud_adapter.c` and into a runtime-owned helper so adapter dump code
  no longer directly formats those runtime-truth fields:
  - `river_cloud_xiaozhi_dump_session_status(uint64_t now_ms)`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Session runtime now owns the diagnostic projection for:
  - session/window/local-close/pending-text runtime summary
  - preview state
  - endpoint soft-close state
  - turn semantics / accept / barge-in / fallback text
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter `river_cloud_adapter_dump_status()` now only delegates that
  session-truth diagnostic block to runtime before emitting playback/downlink
  and queue statistics:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.228
- Moved XiaoZhi cloud-runtime snapshot filling out of
  `river_cloud_adapter_get_runtime_snapshot()` and into a runtime-owned helper
  so dialog runtime now consumes a runtime-exported XiaoZhi snapshot boundary
  instead of adapter-side field assembly:
  - `river_cloud_xiaozhi_fill_runtime_snapshot(...)`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Session/runtime now owns the XiaoZhi snapshot projection for:
  - conversation window / listening
  - playback local state / lane / rebuffer / terminal wait
  - accept / barge-in semantics
  - session / turn / playback semantic text fields
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter cloud snapshot export now only performs generic snapshot setup and
  delegates XiaoZhi-specific runtime truth filling to the runtime helper:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.227
- Moved the adapter-side `accept_reason`-driven pending-text finalize gate into
  session runtime so the XiaoZhi I/O loop no longer directly inspects
  `pending_text_valid/finalized` before deciding whether a turn can finalize:
  - `river_cloud_xiaozhi_finalize_pending_text_if_turn_accepted(...)`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Session runtime now centralizes the shared pending-text finalize eligibility
  and commit logic for both:
  - permissive finalize path with `await_accept_reason` fallback logging
  - accept-only finalize path used after transport polling
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter XiaoZhi I/O service loop now only triggers the runtime helper after
  `refresh_turn_semantics("poll")` instead of reading raw pending-text fields:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.226
- Moved XiaoZhi STT pending-text observation out of
  `river_cloud_adapter.c` and into session runtime so adapter-side transport
  event handling now delegates pending-text normalization to a runtime-owned
  helper:
  - `river_cloud_xiaozhi_note_stt_observation(const river_xiaozhi_event_t *event)`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Session runtime now owns STT text normalization, stored-text deduplication,
  `pending_text_valid/finalized` mutation, and partial ASR emission for XiaoZhi
  STT observations:
  - `river_cloud_xiaozhi_pending_text`
  - `river_cloud_emit_asr_result(RIVER_CLOUD_ASR_EVENT_PARTIAL, ...)`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter `RIVER_XIAOZHI_EVENT_STT` branch now only keeps the follow-up window
  touch and delegates the pending-text observation to session runtime:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.225
- Moved XiaoZhi preview-observation helper ownership out of
  `river_cloud_adapter.c` and into session runtime so transport event handling
  now delegates preview-state normalization to runtime-owned helpers:
  - `river_cloud_xiaozhi_note_preview_observation(const river_xiaozhi_event_t *event)`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Moved the shared XiaoZhi text-copy helper implementation together with that
  preview ownership so session/runtime code no longer depends on an adapter-side
  implementation:
  - `river_cloud_xiaozhi_copy_optional_text(...)`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter `input_speech_start / input_preview / input_endpoint` handling now
  only invokes the exported preview helper instead of embedding preview-state
  mutation locally:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.224
- Added XiaoZhi session-runtime getters for the remaining adapter-visible
  `local_close_pending` / `listen_stop_pending` diagnostic facts:
  - `river_cloud_xiaozhi_local_close_pending()`
  - `river_cloud_xiaozhi_local_close_remaining_ms(...)`
  - `river_cloud_xiaozhi_listen_stop_pending()`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Adapter status/uplink diagnostics now consume those getters instead of
  directly reading the raw XiaoZhi session fields:
  - close-defer remaining time in `river_cloud_adapter_dump_status()`
  - runtime `close_pending` diagnostic text
  - uplink `stop_pending` diagnostic text
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.223
- Moved XiaoZhi `open_and_listen` success-time `listening=true`
  normalization out of the adapter control executor and into the exported
  session-runtime success policy:
  - `river_cloud_xiaozhi_apply_open_and_listen_session_policy()`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Adapter `river_cloud_xiaozhi_request_open_and_listen(...)` now owns the
  request-success glue and invokes that runtime policy centrally, so callers no
  longer need to manually re-apply it after a successful request:
  - `components/river_cloud/river_cloud_adapter.c`
  - `river_cloud_xiaozhi_open_session_and_listen()`
  - `river_cloud_xiaozhi_begin_conversation_window(...)`

## Step 5.222
- Added XiaoZhi session-runtime getters for adapter-visible `listening` and
  `conversation_window` truth so adapter now consumes exported runtime facts
  instead of directly reading those session flags:
  - `river_cloud_xiaozhi_listening_active()`
  - `river_cloud_xiaozhi_conversation_window_active()`
  - `river_cloud_xiaozhi_conversation_window_remaining_ms(...)`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Adapter transport/state surfaces now route through those getters for:
  - transport-active admission
  - `open_and_listen` listen-start read-side guard
  - XiaoZhi config busy guard
  - transport-closed diagnostics
  - runtime dump follow-up window remaining time
  - public conversation-window/runtime snapshot export
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.221
- Moved the XiaoZhi uplink send-ready gate into session runtime so runtime now
  owns the canonical predicate for whether transport can actually send uplink
  audio:
  - `river_cloud_xiaozhi_uplink_send_ready()`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Adapter control/uplink paths now delegate to that helper instead of repeating
  `river_xiaozhi_session_open() && g_river_cloud.xiaozhi_listening`:
  - `RIVER_CLOUD_XIAOZHI_CTRL_LISTEN_STOP`
  - `river_cloud_xiaozhi_io_service_uplink()`
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.220
- Moved XiaoZhi uplink keepalive gating out of adapter and into session runtime
  so runtime now owns the stop-intent fact that keeps the xiaozhi I/O loop
  active after queued uplink frames drain:
  - `river_cloud_xiaozhi_uplink_keepalive_needed(uint32_t queued_frames)`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Adapter `river_cloud_xiaozhi_uplink_active()` now only samples queued uplink
  frames and delegates keepalive policy to runtime:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.219
- Moved XiaoZhi `open_and_listen` success-time stop-intent clearing out of the
  adapter and into session runtime so runtime now owns the post-success session
  normalization for:
  - follow-up reopen via `river_cloud_xiaozhi_open_session_and_listen()`
  - wake admission via `river_cloud_xiaozhi_begin_conversation_window(...)`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter `RIVER_CLOUD_XIAOZHI_CTRL_OPEN_AND_LISTEN` transport execution now no
  longer directly clears `g_river_cloud.xiaozhi_listen_stop_pending`:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.218
- Moved the XiaoZhi reopen-time `listen_stop_pending` busy gate out of
  `river_cloud_xiaozhi_stream_push_frame(...)` and into session runtime so the
  runtime now owns the full reopen admission policy while a listen-stop drain is
  still pending:
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter idle reopen path no longer directly checks
  `g_river_cloud.xiaozhi_listen_stop_pending` before delegating to
  `river_cloud_xiaozhi_maybe_start_followup_round(...)`:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.217
- Moved XiaoZhi listen-stop completion policy out of the adapter and into
  session runtime so the runtime now owns:
  - drain-complete eligibility based on queued uplink frames and accum bytes
  - `request_listen_stop()` dispatch timing
  - `river_cloud_xiaozhi_apply_listen_stop_completion_round_policy()`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Adapter uplink/stream-finish paths now only sample uplink drain state and
  delegate listen-stop completion to runtime:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.216
- Moved XiaoZhi idle reopen gate out of `river_cloud_xiaozhi_stream_push_frame(...)`
  and into session runtime so the runtime now owns:
  - wakeword-window reopen eligibility
  - no-ref reopen rearm / guard gating
  - open-hold speech-frame accumulation before follow-up round open
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Adapter idle capture path now delegates reopen-open policy to
  `river_cloud_xiaozhi_maybe_start_followup_round(...)` and keeps only:
  - listen-stop pending busy guard
  - pre-roll replay / current frame push
  - stream-open counters and logs
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.215
- Moved XiaoZhi terminal close-session tail actions for `network_lost` and
  `bridge_close` out of the adapter and into session runtime so terminal policy
  fully owns the transport close follow-through:
  - `river_cloud_xiaozhi_apply_network_lost_terminal_policy()`
  - `river_cloud_xiaozhi_apply_bridge_close_terminal_policy()`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter terminal paths now only invoke the runtime terminal policy and no
  longer append their own `request_close_session()` tail action:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.214
- Moved XiaoZhi follow-up reopen round-start policy out of the adapter and into
  session runtime so adapter no longer directly decides:
  - `followup_transport_unavailable` window abort
  - `reopen_overlap` local-close resolution
  - overlap round finish + next round begin sequencing
  - exported:
    - `river_cloud_xiaozhi_start_followup_round(uint32_t pre_roll_frames)`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter reopen-open path in `river_cloud_xiaozhi_stream_push_frame(...)` now
  only computes pre-roll size, delegates the follow-up round-start policy to
  runtime, and keeps the remaining audio replay work local:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.213
- Moved XiaoZhi TTS interrupt policy out of the adapter and into session
  runtime so adapter no longer decides whether session/playback state should
  trigger interrupt-side playback abort or transport abort:
  - exported:
    - `river_cloud_xiaozhi_interrupt_tts(const char *reason)`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter `river_cloud_adapter_interrupt_tts_with_reason(...)` now only keeps
  provider dispatch and consumes the exported session-runtime helper for
  XiaoZhi:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.212
- Moved XiaoZhi ASR round lifecycle ownership out of the adapter and into
  session runtime so adapter no longer owns local `round_begin / first_packet /
  packet_sent` transitions:
  - exported:
    - `river_cloud_xiaozhi_round_begin(...)`
    - `river_cloud_xiaozhi_round_note_packet_sent(...)`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter uplink-send and reopen-open paths now only call the exported
  session-runtime helpers for ASR round lifecycle updates:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.211
- Moved XiaoZhi `listen_stop` completion round-close policy out of the adapter
  and into session runtime so adapter no longer decides the post-drain
  `session_closed / round_finish` sequence:
  - exported:
    - `river_cloud_xiaozhi_apply_listen_stop_completion_round_policy(...)`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter `river_cloud_xiaozhi_finalize_listen_stop_if_ready()` now only keeps
  the uplink-drained / `listen_stop` transport gating and calls the exported
  runtime helper for the completion round policy:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.210
- Moved XiaoZhi `bridge_close` terminal cleanup policy out of the adapter and
  into session runtime so adapter no longer assembles the playback-abort /
  round-finish / reset sequence on audio-bridge close:
  - exported:
    - `river_cloud_xiaozhi_apply_bridge_close_terminal_policy(...)`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter `river_cloud_asr_audio_close()` now only keeps the active-stream
  finish and transport/codec tail actions, and calls the exported runtime
  helper for the terminal cleanup:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.209
- Moved XiaoZhi `network_lost` terminal cleanup policy out of the adapter and
  into session runtime so adapter no longer assembles the abort/round-finish/
  reset sequence:
  - exported:
    - `river_cloud_xiaozhi_apply_network_lost_terminal_policy(...)`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter `river_cloud_adapter_notify_network_lost()` now only calls the
  exported runtime helper and keeps the remaining transport tail actions:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.208
- Moved XiaoZhi `transport_closed` terminal cleanup policy out of the adapter
  and into session runtime so adapter no longer assembles the close/abort/reset
  sequence:
  - exported:
    - `river_cloud_xiaozhi_apply_transport_closed_terminal_policy(...)`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter `RIVER_XIAOZHI_EVENT_SESSION_CLOSED` handling now only logs the event
  and calls the exported runtime helper:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.207
- Moved XiaoZhi `post_stop_result` local-close policy out of the adapter and
  into session runtime so adapter no longer directly decides the listen-stop
  resolved close:
  - exported:
    - `river_cloud_xiaozhi_apply_post_stop_result_round_policy(...)`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter listen-stop completion path now only handles transport gating and
  calls the exported runtime helper for the local-close decision:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.206
- Moved XiaoZhi `LLM` terminal local-close policy out of the adapter and into
  session runtime so adapter no longer assembles the `llm` finalize/close path:
  - exported:
    - `river_cloud_xiaozhi_apply_llm_round_policy(...)`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter `RIVER_XIAOZHI_EVENT_LLM` handling now only logs the event, touches the
  follow-up window, and calls the exported runtime helper:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.205
- Moved XiaoZhi local-close `reopen_overlap` policy out of the adapter and
  into session runtime so adapter no longer hand-assembles that overlap-close
  path:
  - exported:
    - `river_cloud_xiaozhi_apply_reopen_overlap_round_policy(...)`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter reopen path now only calls the exported helper when a pending local
  close overlaps a new open:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.204
- Moved XiaoZhi `tts_stop` round-close policy out of `river_cloud_adapter.c`
  and into session runtime so the adapter no longer owns the stop-path round
  closure:
  - exported:
    - `river_cloud_xiaozhi_apply_tts_stop_round_policy(...)`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter TTS-stop handling now only calls the exported session-runtime helper
  and keeps no local stop-path policy body:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.203
- Removed the now-redundant XiaoZhi `keep_local_round_on_tts_start` predicate
  after `tts_start` keep-open / close policy had already been moved into
  session runtime:
  - deleted:
    - `river_cloud_xiaozhi_keep_local_round_on_tts_start()`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- The session-runtime TTS-start surface is now a single exported policy helper:
  - `river_cloud_xiaozhi_apply_tts_start_round_policy(...)`

## Step 5.202
- Moved XiaoZhi `tts_start` keep-open / round-close policy out of
  `river_cloud_adapter.c` and into session runtime so the adapter no longer
  owns that branch-local duplex decision:
  - exported:
    - `river_cloud_xiaozhi_apply_tts_start_round_policy(...)`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter TTS-start handling now only calls the exported session-runtime helper
  and keeps the transport tail glue:
  - finalize pending text
  - apply the runtime-owned round policy
  - cancel playback stop
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.201
- Added a session-runtime helper for the hot-path
  `capture held during playback` decision so adapter no longer assembles that
  predicate from raw playback-lane and duplex-fallback facts itself:
  - exported:
    - `river_cloud_xiaozhi_capture_held_by_playback(...)`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- That helper now owns the combined reducer:
  - `playback_lane_engaged`
  - `duplex_fallback_reason`
  and returns a typed fallback reason to the caller
- Adapter capture path now only calls the exported helper, then logs/acts on
  the result, instead of locally recomputing:
  - `lane_engaged && fallback_reason != NULL`
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.200
- Moved XiaoZhi `open_hold_frames_required` / `no_ref_reopen_ready` helper
  ownership out of `river_cloud_adapter.c` and into session runtime so adapter
  no longer directly carries that reopen-guard policy:
  - exported:
    - `river_cloud_xiaozhi_open_hold_frames_required()`
    - `river_cloud_xiaozhi_no_ref_reopen_ready()`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- The session-runtime reopen helper now explicitly refuses `no_ref` reopen while
  the playback lane is still engaged, so the helper itself understands the new
  playback-runtime truth instead of relying on adapter call ordering alone:
  - consumes:
    - `river_cloud_xiaozhi_playback_lane_engaged()`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Adapter capture-open flow now only consumes exported helpers for those two
  decisions and no longer defines their bodies locally:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.199
- Added an explicit cloud/runtime `playback_lane_engaged` truth so the stack no
  longer has to reconstruct “playback still occupies the turn” by locally
  mixing `playback_active`, `tts_stop_pending`, `rebuffer_pending`, and
  playback-service state:
  - `river_cloud_runtime_snapshot_t` exports `playback_lane_engaged`
  - `river_dialog_runtime_snapshot_t` stores the same field
  - [include/river/river_cloud.h](/root/ameba-river/include/river/river_cloud.h)
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
- XiaoZhi playback runtime now owns the reducer for that lane truth:
  - added `river_cloud_xiaozhi_playback_lane_engaged()`
  - it folds:
    - playback output active
    - rebuffer pending
    - playback-service active states
  - `playback_has_work()` and downlink-task liveness now consume that reducer
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- Adapter/core policy now consume the exported lane truth instead of directly
  guessing from raw playback-service state:
  - XiaoZhi transport-active gating now uses `playback_lane_engaged`
  - `capture held during playback` now keys off that same reducer
  - dialog runtime playback derivation now treats lane engagement as a first
    class input and surfaces it in runtime dumps
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)

## Step 5.198
- Separated `restart_pending` from generic `ref_missing/ref_idle` AEC truth so
  the voice/runtime stack can distinguish “playback lane still occupied during
  a recoverable restart gap” from “reference path is truly absent”:
  - added a dedicated AEC gate reason:
    - `RIVER_VOICE_AEC_GATE_BLOCKED_PLAYBACK_RESTART_PENDING`
  - added a dedicated duplex-ready reason:
    - `RIVER_VOICE_DUPLEX_READY_PLAYBACK_RESTART_PENDING`
  - [include/river/river_voice_runtime_policy.h](/root/ameba-river/include/river/river_voice_runtime_policy.h)
  - [components/river_voice/river_voice_runtime_policy.c](/root/ameba-river/components/river_voice/river_voice_runtime_policy.c)
- XiaoZhi fallback diagnostics now surface that state explicitly instead of
  folding it into the old generic half-duplex AEC block reasons:
  - runtime fallback reason string:
    - `half_duplex_restart_pending`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Fixed-dsb AECM diagnostics now count this gate path separately, so board logs
  can measure how often playback recovery gaps are being mistaken for real
  reference-lane loss:
  - added `restart_pending` counter to the AEC summary
  - [components/river_voice/river_voice_preproc_fixed_dsb.c](/root/ameba-river/components/river_voice/river_voice_preproc_fixed_dsb.c)

## Step 5.197
- Introduced an explicit playback `restart_pending` state so the stack can keep
  treating playback as engaged after a recover-first restart failure without
  lying that the track is still running:
  - new enum state:
    - `RIVER_PLAYBACK_RESTART_PENDING`
  - `river_playback_service_state_active()` now includes it
  - recovering restart failure now lands in `restart_pending` instead of plain
    `idle`
  - `start_stream()` accepts that state as a valid fresh-start source
  - [include/river/river_playback_service.h](/root/ameba-river/include/river/river_playback_service.h)
  - [components/river_voice/river_playback_service.c](/root/ameba-river/components/river_voice/river_playback_service.c)
- Dialog/runtime state derivation now treats `restart_pending` as part of the
  same recoverable playback family:
  - `playback_recovering` includes:
    - `RECOVERING`
    - `RESTART_PENDING`
    - cloud `rebuffer_pending`
  - session-coordinator playback listener maps `restart_pending` to the same
    `playback_recovering` reason family
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
  - [components/river_core/river_session_coordinator.c](/root/ameba-river/components/river_core/river_session_coordinator.c)
- XiaoZhi downlink restart policy was updated to distinguish “playback lane is
  still engaged” from “local track really needs a new start”:
  - downlink worker now explicitly fresh-starts on:
    - `IDLE`
    - `RESTART_PENDING`
  - but still avoids unnecessary takeover while another healthy track is live
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)

## Step 5.196
- Downgraded the recover-first fallback path so a same-track restart failure no
  longer reports a fatal playback error before the cloud runtime has a chance
  to fresh-start the stream:
  - when `flush/restart` fails in `RIVER_PLAYBACK_RECOVERING`, playback service
    now releases the broken track and returns to `IDLE` without emitting
    `RIVER_PLAYBACK_ERROR`
  - [components/river_voice/river_playback_service.c](/root/ameba-river/components/river_voice/river_playback_service.c)
- XiaoZhi downlink recovery now treats that branch as a recoverable
  `fresh_start` fallback instead of explicitly forcing another local `stop`
  cycle after the service already tore the failed track down:
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- Result:
  - same-track recover failure still preserves the queued audio / rebuffer
    truth
  - but it no longer escalates the dialog runtime into a spurious fatal
    `playback_error` before the next normal start attempt

## Step 5.195
- Promoted recoverable playback churn into explicit runtime truth instead of
  letting `dialog runtime` infer it indirectly from transient local active
  gaps:
  - cloud runtime snapshot now exports `playback_rebuffer_pending`
  - dialog runtime snapshot now keeps:
    - `playback_cloud_active`
    - `playback_rebuffer_pending`
    - derived `playback_recovering`
  - [include/river/river_cloud.h](/root/ameba-river/include/river/river_cloud.h)
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- Reworked dialog-runtime playback derivation so `speaking/barge_in_listening`
  is preserved across recoverable `rebuffer/recovering` churn:
  - `playback_active` is now derived from:
    - service active state
    - cloud playback active
    - runtime recovering truth
  - `output_lane=speaking` is no longer suppressed during tail-wait if the
    player is still in recoverable restart
  - runtime status dump now prints:
    - `cloud_playback`
    - `recovering`
- Tightened local playback semantics so `RIVER_PLAYBACK_RECOVERING` still
  counts as an engaged playback lane for the rest of the stack:
  - `river_playback_service_state_active()` now includes `RECOVERING`
  - this keeps AEC/VAD/dialog orchestration from treating a same-track recover
    as a real local stop
  - [components/river_voice/river_playback_service.c](/root/ameba-river/components/river_voice/river_playback_service.c)

## Step 5.194
- Rebuilt the XiaoZhi downlink jitter buffer and local playback prefetch budget
  so the device stops starting playback on an unrealistically thin queue:
  - downlink ring depth: `32 -> 96`
  - initial start threshold: `12 -> 16`
  - rebuffer restart threshold: `18 -> 28`
  - AudioTrack target/fallback buffer: `6/4 -> 12/8`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Extended playback runtime to derive an adaptive `prefetch_target_ms` from
  observed `audio.out.meta` cadence instead of reusing one static rebuffer
  threshold:
  - records:
    - `xiaozhi_playback_last_meta_ms`
    - `xiaozhi_playback_last_meta_gap_ms`
    - `xiaozhi_playback_prefetch_target_ms`
  - raises start/rebuffer thresholds after repeated rebuffer and larger
    meta-supply gaps
  - stretches starvation-trigger wait to match the active prefetch target
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- Adapter-side diagnostics now print the new downlink truth directly so board
  logs can distinguish server supply gaps from too-aggressive local start
  policy:
  - `target_ms`
  - `meta_gap_ms`
  - `rebuffer_count`
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.193
- Continued pulling XiaoZhi round-close truth out of `river_cloud_adapter.c`
  by moving these state transitions into
  `river_cloud_xiaozhi_session.c`:
  - round-finish reason latching
  - endpoint soft-close timeout decision
  - active-stream finish state commit
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Added a typed active-stream finish cause family so adapter no longer decides
  how session runtime should commit a local input round:
  - `RIVER_CLOUD_XIAOZHI_STREAM_FINISH_POST_ROLL`
  - `RIVER_CLOUD_XIAOZHI_STREAM_FINISH_ENDPOINT_TIMEOUT`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Adapter now only provides the transport tail for those finishes
  (`flush_accumulator_padded + finalize_listen_stop_if_ready`) and forwards
  typed round-close causes directly:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)

## Step 5.192
- Continued thinning `river_cloud_adapter.c` by moving XiaoZhi
  `endpoint soft close / local close defer` helper ownership into
  `river_cloud_xiaozhi_session.c`:
  - duplex soft-endpoint predicate
  - duplex-speaking uplink continuation predicate
  - endpoint soft-close clear / cancel / arm
  - interrupt hint logging
  - local-close defer predicate / arm
  - local-close timeout check
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Those helpers are now exported through the shared cloud internal contract, so
  adapter code only consumes session-runtime policy instead of owning those
  state transitions locally:
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- After this slice, adapter still owns `finish_active_stream()` and the
  endpoint-timeout trigger site, but the underlying local-close state machine
  helpers no longer live there

## Step 5.191
- Continued shrinking `river_cloud_adapter.c` by moving core local-round close
  truth into XiaoZhi session runtime:
  - `river_cloud_xiaozhi_round_finish(...)` moved out of the adapter and is now
    exported by:
    - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - added a session-runtime round-close cause family:
    - `RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_LOCAL_RESOLVED`
    - `RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_SERVER_RESPONSE`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Adapter-local close branches no longer hand-assemble those round-close
  behaviors:
  - `resolve_local_close(...)`
  - `close_round_on_server_response(...)`
  now both route through one session-runtime reducer:
  - `river_cloud_xiaozhi_close_local_round_for_cause(...)`
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- This keeps round-finish pacing truth and local-round close policy in the same
  session-runtime module, which is the next step toward removing adapter-owned
  `local close / endpoint close` truth entirely

## Step 5.190
- Started the downlink/playback recovery-model rebuild by downgrading local
  `write_failed` from an unconditional stop/start cycle into a same-track
  recover-first path:
  - XiaoZhi downlink worker now requests
    `river_playback_service_flush_stream_ex("xiaozhi_playback_write_failed")`
    before falling back to `stop_stream`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- Playback service flush/restart can now recover from the explicit
  `RIVER_PLAYBACK_RECOVERING` state instead of only servicing already-active
  tracks:
  - recovering state now records a `recover` control
  - successful recover returns the service to `RIVER_PLAYBACK_RUNNING`
  - [components/river_voice/river_playback_service.c](/root/ameba-river/components/river_voice/river_playback_service.c)
- This narrows the rebuffer blast radius after one `AudioTrack_Write` failure:
  - first try same-handle pause/flush/stop/start
  - only if that fails do we tear down the current playback stream and rebuild
    from the downlink worker

## Step 5.189
- Tightened `session coordinator` barge-in interruption admission so it now
  relies only on `dialog runtime` snapshot truth instead of secretly falling
  back to playback-service local state:
  - removed the local playback-service active helper path
  - added a runtime-snapshot playback-interruptible predicate
  - [components/river_core/river_session_coordinator.c](/root/ameba-river/components/river_core/river_session_coordinator.c)
- `river_session_try_interrupt_playback_on_asr_text()` now interrupts TTS only
  when the core snapshot says playback is still locally interruptible, which
  means:
  - playback is active in runtime truth
  - playback has not already reached a terminal state
- This closes another truth leak after Step `5.188`: `session_coordinator`
  no longer bypasses the dialog runtime and no longer reintroduces stale local
  playback-state guesses into barge-in control flow

## Step 5.188
- Hardened the core-owned `dialog runtime` so interaction derivation now
  consumes playback terminal truth explicitly instead of still treating
  `output_lane=speaking` as sufficient truth after local playback already
  reached a terminal outcome:
  - added terminal-aware runtime helpers for:
    - terminal-closed playback
    - effective playback engagement
    - effective speaking output
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- `interaction_state` derivation now suppresses stale speaking/playback
  interpretations when:
  - `playback_terminal_state` is already terminal
  - `playback_terminal_waiting` is the only remaining local fact after audio
    has already drained
- The runtime also now re-computes effective playback activity through the same
  terminal-aware reducer on both:
  - cloud snapshot sync
  - playback-service state ingress
  so local terminal truth can dominate over transient late lane/service facts

## Step 5.187
- Continued unifying XiaoZhi playback terminal close ownership by moving the
  remaining downlink data-plane fatal path into the same runtime-owned typed
  abort family:
  - added:
    - `RIVER_CLOUD_XIAOZHI_PLAYBACK_ABORT_FRAME_OVERSIZE`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- `frame_oversize` in the downlink worker no longer hand-assembles:
  - local clear
  - downlink reset
  - playback stop
  - playback reset
  and now routes through:
  - `river_cloud_xiaozhi_playback_abort_for_cause(...)`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- This removes the last known playback-runtime local terminal special case in
  the data plane, so both:
  - transport-originated aborts
  - local fatal downlink faults
  now share one typed terminal-close reducer and one normalized runtime log

## Step 5.186
- Continued shrinking XiaoZhi terminal-close ambiguity by moving the adapter’s
  terminal-abort parameter assembly behind a runtime-owned typed cause reducer:
  - added:
    - `river_cloud_xiaozhi_playback_abort_cause_t`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Replaced the old three-parameter abort interface:
  - `clear_reason`
  - `stream_reason`
  - `interrupt_stream`
  with one runtime-owned entrypoint:
  - `river_cloud_xiaozhi_playback_abort_for_cause(cause, detail_reason)`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- The playback runtime now centrally derives terminal-close semantics for:
  - `interrupt`
  - `transport_closed`
  - `network_lost`
  - `bridge_close`
  including:
  - local clear reason
  - playback-service stop/interrupt reason
  - whether the stream should be hard-interrupted or normally stopped
- Adapter branches no longer assemble playback terminal policy locally:
  - `SESSION_CLOSED`
  - `notify_network_lost()`
  - `interrupt_tts_with_reason()`
  - `asr_audio_close()`
  now all route through the same typed runtime reducer
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- Added reducer-side diagnostics so board logs can now show one normalized
  terminal-abort fact:
  - `cause`
  - resolved `clear_reason`
  - resolved `stream_reason`
  - whether it was an interrupt path
  - whether playback work / stream were active on entry

## Step 5.185
- Continued the XiaoZhi terminal-truth rebuild by separating local playback
  outcome from protocol ACK truth instead of storing both in the same
  `terminal_ack` field:
  - added a dedicated runtime-owned terminal result:
    - `xiaozhi_playback_terminal_state`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Tightened playback terminal close semantics in the runtime:
  - terminal openness is now controlled by `terminal_state`, not only by
    whether a protocol ACK string was written
  - successful protocol terminal ACK still records:
    - `completed`
    - `cleared`
  - local-only terminal outcomes now record:
    - `local_completed`
    - `local_cleared`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- This removes the remaining truth conflation in the terminal path:
  - `audio.out.completed` queue failure no longer fabricates a truthful
    protocol `completed`
  - clear-before-start / no-ACK local stops no longer look identical to a
    server-visible terminal ACK
- Exported the new terminal result through the truth-source chain:
  - `river_cloud_runtime_snapshot_t` now carries:
    - `playback_terminal_state`
    - `playback_terminal_reason`
  - `river_dialog_runtime_snapshot_t` now mirrors those fields
  - adapter / dialog status dumps now print:
    - local terminal state
    - protocol terminal ACK
    - terminal reason
  - [include/river/river_cloud.h](/root/ameba-river/include/river/river_cloud.h)
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- With this slice landed, the dialog runtime can now distinguish three
  different truths instead of one mixed terminal flag:
  - local playback terminal outcome
  - protocol terminal ACK truth
  - terminal-tail wait state

## Step 5.184
- Continued the XiaoZhi playback terminal rebuild by making `cleared` truthful
  and by exposing “waiting for final tail” as runtime-owned state instead of
  hiding it inside `tts_stop_pending`:
  - added runtime-owned tail-wait fields:
    - `xiaozhi_playback_terminal_waiting`
    - `xiaozhi_playback_terminal_wait_reason`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- `audio.out.cleared` is now only treated as reported when the board actually
  queued a cleared ACK:
  - `river_cloud_xiaozhi_try_queue_playback_cleared_ack()` now returns whether
    a real cleared ACK was queued
  - `playback_finalize_cleared()` no longer fabricates terminal `cleared`
    state for clear-before-start or other local-only stops with no
    `last_fully_heard_segment_id`
  - local-only clear now logs:
    - `xiaozhi playback clear kept local only`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- Tightened the pending-stop completion wait into an explicit runtime fact:
  - when `tts_stop_pending` cannot yet admit `completed`, the runtime now
    latches a structured wait reason such as:
    - `await_last_segment_meta`
    - `await_segment_queue_drain`
    - `await_last_segment_tail`
  - that wait state clears on playback start, reset, terminal ACK close, and
    downlink reset
- Exported the new terminal-tail wait fact through the truth-source chain:
  - `river_cloud_runtime_snapshot_t`
  - `river_dialog_runtime_snapshot_t`
  - adapter and dialog dump logs now show:
    - terminal wait on/off
    - terminal wait reason
  - [include/river/river_cloud.h](/root/ameba-river/include/river/river_cloud.h)
  - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
  - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
- This slice removes another source of terminal-state ambiguity:
  - local clear without a truthful `audio.out.cleared` no longer masquerades
    as protocol-cleared playback
  - dialog/runtime diagnostics can now distinguish ordinary playback from
    “still waiting for the final tail before terminal completion”

## Step 5.183
- Rebuilt XiaoZhi terminal `completed` admission so it now depends on
  last-segment truth instead of only depending on the local player having gone
  idle:
  - added explicit terminal predicates:
    - `river_cloud_xiaozhi_playback_last_segment_observed()`
    - `river_cloud_xiaozhi_playback_completed_ready()`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- `audio.out.completed` is no longer queued just because `tts_stop_pending`
  sees `service inactive + queued=0`:
  - the runtime now requires:
    - server-side last segment has been observed
    - local playback segment queue has fully drained
    - `last_fully_heard_segment_id` matches the latest last-segment id
  - if that condition is not yet true, the playback runtime keeps the terminal
    window open and waits for the late tail instead of prematurely closing the
    response
- Tightened pending-stop behavior around the new terminal truth:
  - the local stop path after `xiaozhi_tts_stop` no longer resets playback
    runtime state until `completed` is actually admissible
  - `playback_check_pending_stop()` now keeps waiting when the local player has
    already gone idle but the last segment has not yet been observed/heard
  - once terminal completion becomes admissible, the existing reset path still
    clears the runtime in one place
- This continues the dialog-runtime / downlink rebuild by removing one of the
  main sources of false completion:
  - `completed` is now tied to `last_segment observed + fully heard`, not only
    to a transient local drain point
  - next focus is to finish the `cleared` side of the terminal model and make
    “waiting for final tail” observably distinct from normal idle

## Step 5.182
- Continued the playback/downlink recovery rebuild by splitting recoverable
  playback churn from hard local faults at the playback-service interface
  instead of letting every write-side glitch collapse into `playback_error`:
  - added a dedicated playback state:
    - `RIVER_PLAYBACK_RECOVERING`
  - [include/river/river_playback_service.h](/root/ameba-river/include/river/river_playback_service.h)
- `AudioTrack_Write()` failure now enters an explicit recoverable playback
  state instead of immediately poisoning the dialog runtime with fatal error
  semantics:
  - `river_playback_service_write()` now reports write-path churn as
    `RIVER_PLAYBACK_RECOVERING`
  - fatal startup / flush / track-init failures still stay on
    `RIVER_PLAYBACK_ERROR`
  - state-name dumps and diagnostics now expose `recovering`
  - [components/river_voice/river_playback_service.c](/root/ameba-river/components/river_voice/river_playback_service.c)
- Tightened the dialog-runtime ingress path so only true fatal playback faults
  trip `error_recovering`:
  - `session_coordinator` now emits:
    - `playback_recovering`
    - `playback_error`
    - `playback_state`
    as distinct runtime reasons
  - only `RIVER_PLAYBACK_ERROR` still calls
    `river_dialog_runtime_note_error("playback_error")`
  - [components/river_core/river_session_coordinator.c](/root/ameba-river/components/river_core/river_session_coordinator.c)
- This slice makes the dialog runtime truth source more faithful during XiaoZhi
  rebuffer churn:
  - recoverable playback restart loops no longer masquerade as fatal runtime
    recovery
  - next focus is to finish rebuilding terminal ACK completion and last-segment
    close semantics on top of the now explicit `recovering` vs `error` split

## Step 5.181
- Started rebuilding XiaoZhi playback recovery semantics on top of the
  runtime-owned media state by separating predictable upstream starvation from
  harder local write failures:
  - added a dedicated starvation timeout budget:
    - `RIVER_CLOUD_XIAOZHI_DOWNLINK_STARVED_REBUFFER_MS`
  - added playback-runtime-owned starvation watch state:
    - `xiaozhi_downlink_starved_since_ms`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- The playback runtime now proactively converts sustained `queued=0` gaps into
  controlled rebuffer instead of waiting for the AudioTrack path to fall into a
  harder `write_failed` recovery:
  - new runtime behavior:
    - watch empty-downlink gaps while playback is still active
    - skip the rebuffer path when the known last segment has already fully
      drained locally
    - stop the stream with `xiaozhi_playback_starved` once the gap exceeds the
      starvation threshold, reusing the existing rebuffer bookkeeping
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
- Tightened the recovery bookkeeping so starvation watch state is cleared on:
  - playback start
  - rebuffer resume
  - output reset
  - downlink reset
  - successful frame writes
  - hard write-failure fallback
- This is the first concrete slice of the terminal/recovery rebuild:
  - soft upstream starvation is now a runtime-observed rebuffer cause, not
    only an eventual AudioTrack failure side effect
  - next focus is to continue splitting recoverable rebuffer from hard local
    playback faults and terminal ACK completion semantics

## Step 5.180
- Moved the remaining XiaoZhi playback reset / meta / stop helpers out of the
  session runtime and into the dedicated playback runtime so media-owned state
  is no longer implemented in two different modules:
  - `river_cloud_xiaozhi_clear_playback_meta_state()`
  - `river_cloud_xiaozhi_cancel_playback_stop()`
  - `river_cloud_xiaozhi_playback_note_duplex_ready()`
  - `river_cloud_xiaozhi_mark_playback_started()`
  - `river_cloud_xiaozhi_arm_playback_stop()`
  - `river_cloud_xiaozhi_reset_playback_state()`
  - `river_cloud_xiaozhi_reset_downlink_state()`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Tightened session/adapter behavior around the new runtime truth boundary:
  - `river_cloud_xiaozhi_playback_allows_vad_open()` now reports duplex-ready
    playback state through a runtime-owned helper instead of mutating playback
    fields inside `session.c`
  - follow-up window timeout now blocks on `playback_has_work()`
  - XiaoZhi TTS interrupt admission now also keys off `playback_has_work()`
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- This closes the “remaining playback helper ownership” cleanup from the
  execution plan and leaves the next slice focused on real terminal/recovery
  behavior instead of ownership churn:
  - rebuild `write_failed / underrun / rebuffer / cleared-completed` semantics
    on top of the now runtime-owned playback state

## Step 5.179
- Moved XiaoZhi playback terminal ownership one step further toward a true
  runtime-owned media source instead of leaving adapter branches to assemble
  stop/reset logic from raw fields:
  - new runtime queries:
    - `river_cloud_xiaozhi_playback_output_active()`
    - `river_cloud_xiaozhi_playback_has_work()`
  - new runtime stop path:
    - `river_cloud_xiaozhi_playback_abort(...)`
  - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Replaced adapter-local playback termination recipes with runtime-owned
  operations so transport/lifecycle paths now express cause instead of
  manipulating playback state piece by piece:
  - `transport_closed`
  - `network_lost`
  - XiaoZhi TTS interrupt
  - `bridge_close`
  - config-refresh busy guard
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- Tightened the internal runtime boundary:
  - adapter `transport_active()` and speaking-lane helpers now consume
    `playback_output_active()` instead of reading raw playback flags directly
  - playback worker liveness in the runtime module now also flows through the
    same runtime-owned `queued_frames + output_active` predicate
- This turns playback termination into a typed runtime API instead of a set of
  duplicated adapter branches, which is a concrete prerequisite for the next
  rebuild slice:
  - move the remaining playback reset/terminal helpers fully behind the media
    runtime and keep shrinking adapter/session cross-ownership

## Step 5.178
- Landed the first real XiaoZhi downlink / playback runtime extraction slice so
  media playback ownership no longer lives as duplicated local logic inside
  `river_cloud_adapter.c`:
  - new dedicated runtime module:
    - [components/river_cloud/river_cloud_xiaozhi_playback_runtime.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_playback_runtime.c)
  - build wiring:
    - [components/river_cloud/CMakeLists.txt](/root/ameba-river/components/river_cloud/CMakeLists.txt)
- Moved the XiaoZhi playback media engine into the new runtime file while
  preserving current behavior:
  - playback metadata ingestion and segment queue ownership
  - `audio.out.started/mark/cleared/completed` ACK progress handling
  - rebuffer bookkeeping and retry-preserved downlink frame handling
  - decoder preparation, downlink ring writes, playback start/restart policy,
    and the dedicated downlink worker task
- Exported the adapter/runtime boundary in the internal cloud contract so media
  runtime code can use the existing control queue and shared playback helpers
  without reaching through file-local statics:
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - exported shared entrypoints now include:
    - `river_cloud_xiaozhi_control_request_async(...)`
    - `river_cloud_xiaozhi_playback_note_meta(...)`
    - `river_cloud_xiaozhi_playback_check_pending_stop(...)`
    - `river_cloud_xiaozhi_playback_finalize_cleared(...)`
    - `river_cloud_xiaozhi_playback_start_downlink_if_needed(...)`
    - `river_cloud_xiaozhi_playback_handle_audio_event(...)`
    - `river_cloud_xiaozhi_playback_queued_frames()`
- Shrunk `river_cloud_adapter.c` back toward transport/policy composition:
  - removed the old embedded playback/downlink implementations
  - event routing, init/config bootstrapping, close/network-loss, and capture
    push paths now consume the dedicated playback runtime API instead
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- This closes the first concrete `Step C` extraction slice in the runtime
  re-architecture plan and sets up the next step:
  - rebuild playback recovery / rebuffer semantics on top of the extracted
    media runtime instead of continuing to grow adapter-local stop/clear logic

## Step 5.177
- Landed the first real `dialog runtime` truth-source slice so `river_core`
  now owns derived interaction state instead of letting `app` and
  `session_coordinator` each maintain their own coarse phase view:
  - new runtime public API:
    - [include/river/river_dialog_runtime.h](/root/ameba-river/include/river/river_dialog_runtime.h)
  - new core runtime implementation:
    - [components/river_core/river_dialog_runtime.c](/root/ameba-river/components/river_core/river_dialog_runtime.c)
  - build wiring:
    - [components/river_core/CMakeLists.txt](/root/ameba-river/components/river_core/CMakeLists.txt)
- Rewired boot and cloud-state sync through the new runtime instead of direct
  `interaction_state_set()` calls from the app layer:
  - `river_app_boot()` now initializes `dialog runtime`
  - boot completion now flows through:
    - `river_dialog_runtime_mark_boot_ready(...)`
    - `river_dialog_runtime_sync_cloud_state(...)`
  - runtime status is included in the standard boot/status dump
  - [components/river_core/river_app.c](/root/ameba-river/components/river_core/river_app.c)
- Turned `session_coordinator` into an event ingester instead of a parallel
  phase state machine:
  - removed the local `phase` / `asr_session_active` truth source
  - wake admission, ASR lifecycle, playback lifecycle, and playback-interrupt
    guards now read or update `dialog runtime`
  - `river_session_coordinator_sync_interaction_state()` now delegates to the
    runtime cloud-sync path
  - [components/river_core/river_session_coordinator.c](/root/ameba-river/components/river_core/river_session_coordinator.c)
- Added a generic cloud-runtime snapshot export so `river_core` can ingest
  provider/session/lane facts without poking XiaoZhi internals directly:
  - new public snapshot contract:
    - [include/river/river_cloud.h](/root/ameba-river/include/river/river_cloud.h)
  - new adapter snapshot implementation:
    - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- This closes the Step B architecture gap enough to start the next real slice:
  - rebuild XiaoZhi downlink / playback as a runtime-owned media engine on top
    of the new `dialog runtime` truth source

## Step 5.176
- Saved a new architecture review that captures the current root causes behind
  slow XiaoZhi response and playback churn, then re-designed the target runtime
  layering around a core-owned dialog runtime, explicit media engines, and
  typed ports:
  - [doc/VOICE_RUNTIME_ARCHITECTURE_REVIEW_ZH_2026-04-17.md](/root/ameba-river/doc/VOICE_RUNTIME_ARCHITECTURE_REVIEW_ZH_2026-04-17.md)
- Created and registered a new multi-step runtime re-architecture execution
  plan so the branch stops treating the recurring latency/playback regressions
  as isolated local bugs:
  - [doc/VOICE_RUNTIME_REARCHITECTURE_EXECUTION_PLAN_ZH.md](/root/ameba-river/doc/VOICE_RUNTIME_REARCHITECTURE_EXECUTION_PLAN_ZH.md)
  - [.codex/active_plans.md](/root/ameba-river/.codex/active_plans.md)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)
- Introduced a core-owned dialog cloud port and rewired the boot path to
  register the concrete cloud adapter in one place:
  - new stable port interface:
    - [include/river/river_dialog_cloud_port.h](/root/ameba-river/include/river/river_dialog_cloud_port.h)
  - new core implementation:
    - [components/river_core/river_dialog_cloud_port.c](/root/ameba-river/components/river_core/river_dialog_cloud_port.c)
  - boot-time port registration:
    - [components/river_core/river_app.c](/root/ameba-river/components/river_core/river_app.c)
  - build wiring:
    - [components/river_core/CMakeLists.txt](/root/ameba-river/components/river_core/CMakeLists.txt)
- Removed the direct `river_voice -> river_cloud` calls from the hot voice
  path so `river_voice` now consumes the dialog port instead of provider
  specifics:
  - [components/river_voice/river_voice_vad_probe.c](/root/ameba-river/components/river_voice/river_voice_vad_probe.c)
  - [components/river_voice/river_voice_segment_sink.c](/root/ameba-river/components/river_voice/river_voice_segment_sink.c)
  - [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc)
- Finished the pending XiaoZhi uplink pacing fix so the media path now models
  retry-pending in-flight frames explicitly instead of implicitly dropping them:
  - added `ready_frames` accounting that includes the retry-pending frame
  - `io_service_uplink()` now drains up to
    `RIVER_CLOUD_XIAOZHI_UPLINK_DRAIN_BURST_MAX` frames per invocation
  - `BUSY` and generic send failures now preserve the current frame for retry
    instead of consuming and losing it
  - `listen_stop` finalization now waits for the retry-pending frame as part of
    the ready queue
  - round-finish logs now expose:
    - `audio_ms`
    - `realtime_gap_ms`
    - `pace_pct`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)

## Step 5.175
- Addressed the three device-side playback regressions exposed by the latest
  XiaoZhi board log instead of waiting on further server-side TTS changes:
  - reduced local digital gain during mono->stereo expansion from `5/2` to
    unity `1/1`, so the current 16 kHz playback path no longer amplifies and
    clips already-hot service audio
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- Changed the strict `no_ref` half-duplex barge-in path from “duck then hard
  interrupt” to “duck only”:
  - once the stricter `no_ref` evidence threshold is met, the board now logs
    `barge-in interrupt suppressed: mode=no_ref_duck_only ...`
  - the local ducking path stays active, but the device no longer cuts XiaoZhi
    TTS outright just because the no-reference fallback saw sustained near-end
    speech
  - [components/river_voice/river_voice_vad_probe.c](/root/ameba-river/components/river_voice/river_voice_vad_probe.c)
- Reworked the XiaoZhi downlink `write_failed` path from “clear playback and
  drop the round” into “rebuffer and resume”:
  - playback metadata / segment queue is preserved across `write_failed`
  - the failing frame is retained for retry instead of being silently dropped
  - ACK progress pauses while rebuffering, so `audio.out.mark` timing no longer
    advances during the gap
  - restart now waits for a deeper resume watermark: `18` frames
  - board logs now expose:
    - `rebuffer=yes`
    - `xiaozhi playback rebuffer requested: ...`
    - `xiaozhi playback rebuffer resumed: ...`
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- Updated the active context and duplex execution plan so this playback-fix
  slice is recorded as the latest landed step before the next board regression:
  - [doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md](/root/ameba-river/doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)

## Step 5.174
- Tightened local barge-in interruption while XiaoZhi playback is running in
  the current no-reference / half-duplex fallback path so brief leakage no
  longer escalates into an immediate hard TTS cut:
  - `no_ref` playback now uses stricter near-end speech gates:
    - `ref_margin_peak`: `448 -> 960`
    - `min_enhanced_peak`: `1200 -> 2200`
    - `ratio_pct`: `150 -> 180`
    - duck trigger: `1 -> 3` hit frames
    - interrupt trigger: `5 -> 12` hit frames
  - interrupt logs now explicitly expose `mode=no_ref_strict` for board-side
    validation
  - [components/river_voice/river_voice_vad_probe.c](/root/ameba-river/components/river_voice/river_voice_vad_probe.c)
- Enlarged XiaoZhi downlink/playback buffering to prioritize full-sentence
  continuity over minimum startup latency on the currently negotiated
  half-duplex service path:
  - downlink ring: `16 -> 32` frames
  - playback start watermark: `8 -> 12` frames
  - primary playback buffer: `3 -> 6` frames
  - compact fallback playback buffer: `2 -> 4` frames
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Extended the XiaoZhi playback-start log so board traces can directly confirm
  the active startup watermark and playback buffer size after flashing:
  - `start=...`
  - `buffer=...`
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- Updated the active duplex plan and context so this device-side playback
  stabilization step is recorded as the newest landed slice for upcoming board
  regression:
  - [doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md](/root/ameba-river/doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)

## Step 5.173B
- Completed the pending XiaoZhi transport timing-observation slice so board
  logs can correlate the server collaboration chain without changing current
  duplex strategy or websocket contract:
  - [components/river_cloud/river_xiaozhi_ws.c](/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c)
- `session.update` handling now preserves accepted-turn cache across sparse
  updates within the same round instead of clearing it on every payload:
  - `accept_reason`
  - `turn_id`
  - `input_state`
  - `output_state`
  - `barge_in_enabled`
  - `last_accept_at_ms`
  - this keeps later `response.start` / `audio.out.meta` timing correlation
    anchored to the actual accepted turn
  - [components/river_cloud/river_xiaozhi_ws.c](/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c)
- Added concrete timing stamps and logs across the key collaboration path:
  - `input.speech.start`
  - `input.preview`
  - `input.endpoint`
  - `session.update` accept latch
  - `response.start`
  - `audio.out.meta`
  - each log now exposes stage-to-stage elapsed time such as:
    - `from_preview_start_ms`
    - `from_accept_ms`
    - `from_response_start_ms`
  - [components/river_cloud/river_xiaozhi_ws.c](/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c)
- Extended `river xiaozhi status` with a board-visible timing view so runtime
  inspection can distinguish stale state from true server latency:
  - `timing_age_ms`
  - `timing_chain_ms`
  - [components/river_cloud/river_xiaozhi_ws.c](/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c)
- Updated the duplex execution plan and active context so the branch records
  this logging-only follow-up as the latest landed step while waiting for the
  next server/device联调 round:
  - [doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md](/root/ameba-river/doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)

## Step 5.173A
- Added collaboration-negotiation debug logs so board traces can explain why
  the current session did or did not enter the XiaoZhi duplex-ready path:
  - discovery refresh snapshot
  - `session.start` snapshot
  - discovery-refresh failure fallback snapshot
  - negotiation reasons for `preview_events` and `playback_ack`
  - [components/river_cloud/river_xiaozhi_ws.c](/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c)
- Extended session semantic logs to expose sparse / pre-accept server behavior
  without changing existing state handling:
  - `session.update semantic sparse`
  - `turn semantics updated before accept`
  - wake-admission policy snapshot before listen-start
  - [components/river_cloud/river_xiaozhi_ws.c](/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Added payload-shape warnings for collaboration-related realtime messages so
  monitor logs can distinguish protocol sparsity from local state-machine bugs:
  - known message type with non-object `payload`
  - `input.speech.start` missing `preview_id`
  - `input.preview` sparse payload
  - `input.endpoint` sparse payload
  - preview/input events arriving before negotiation succeeds
  - [components/river_cloud/river_xiaozhi_ws.c](/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c)
- Added playback-ack failure logs on the adapter side with enough correlation
  to debug server/device collaboration breaks:
  - `audio.out.started`
  - `audio.out.mark`
  - `audio.out.cleared`
  - `audio.out.completed`
  - each failure log now includes:
    - control ids
    - negotiated playback-ack mode
    - current XiaoZhi last error
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- Updated the duplex execution plan and active context so this logging-only
  follow-up is recorded as landed while waiting for the next server-side
  collaboration changes:
  - [doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md](/root/ameba-river/doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)

## Step 5.173
- Added an explicit XiaoZhi duplex `default-on` policy switch so the branch can
  distinguish:
  - experiment code compiled in
  - sessions that are actually allowed to advertise `half_duplex=false`
  - [Kconfig](/root/ameba-river/Kconfig)
  - [prj.conf](/root/ameba-river/prj.conf)
- Tightened `session.start` duplex advertisement behind a full default-enable
  matrix instead of the old compile-time-only experiment flag:
  - active board/profile must support playback reference
  - discovery must advertise `voice_collaboration`
  - server endpoint must be available and enabled
  - `preview_events` negotiation must succeed
  - `playback_ack.mode=segment_mark_v1` negotiation must succeed
  - [components/river_cloud/river_xiaozhi_ws.c](/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c)
  - [include/river/river_xiaozhi_ws.h](/root/ameba-river/include/river/river_xiaozhi_ws.h)
- Unified device-side duplex fallback mapping so speaking-time keep-open,
  capture-hold, and status logs now report service-side collaboration failures
  and local runtime failures from one matrix:
  - `half_duplex_default_policy_disabled`
  - `half_duplex_service_collaboration_unavailable`
  - `half_duplex_service_endpoint_unavailable`
  - `half_duplex_service_endpoint_disabled`
  - `half_duplex_service_preview_unavailable`
  - `half_duplex_service_playback_ack_unavailable`
  - plus the existing local runtime reasons:
    - `half_duplex_no_playback_reference`
    - `half_duplex_ref_idle`
    - `half_duplex_aec_blocked`
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Extended board-visible XiaoZhi diagnostics so validation can now correlate:
  - negotiated default-on state
  - default fallback reason
  - discovery voice-collaboration/server-endpoint readiness
  - negotiated preview/playback-ack state
  - runtime `duplex_ready`
  - [components/river_cloud/river_xiaozhi_ws.c](/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- Updated the duplex execution plan and active context so `5.173` is recorded
  as landed and the branch focus moves to board regression of the new default
  matrix:
  - [doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md](/root/ameba-river/doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)

## Step 5.172
- Promoted the branch build to a dedicated `duplex-ready experimental`
  board-profile baseline by enabling:
  - `CONFIG_RIVER_XIAOZHI_FULL_DUPLEX_EXPERIMENT_EN=y`
  - `CONFIG_RIVER_WEBRTC_AECM_EXPERIMENT_EN=y`
  - `CONFIG_RIVER_VOICE_PREPROC_PROFILE_FIXED_DSB_WEBRTC_AECM=y`
  - [prj.conf](/root/ameba-river/prj.conf)
- Replaced the old native-ref runtime shortcut with real native `ch3`
  observation plumbing:
  - `duplex_ready` for native-capture-ref profiles now consumes actual AECM
    reference telemetry instead of inferring `ref_activity=active` only from
    playback state
  - [components/river_voice/river_voice_runtime_policy.c](/root/ameba-river/components/river_voice/river_voice_runtime_policy.c)
  - [include/river/river_voice_runtime_policy.h](/root/ameba-river/include/river/river_voice_runtime_policy.h)
- Published runtime native-ref telemetry from the experimental preproc path:
  - AECM ref `activity`
  - `peak`
  - `ratio_q15`
  - freshness / frame metadata
  - [components/river_voice/river_voice_preproc_fixed_dsb.c](/root/ameba-river/components/river_voice/river_voice_preproc_fixed_dsb.c)
- Fixed board-side `ref_peak` observability for the native 3-channel capture
  profile:
  - `vad_probe` now samples `capture ch3` directly when the active profile uses
    native capture ref, instead of only reading the software playback-ring ref
  - board logs and local barge-in heuristics therefore observe the same
    far-end reference source as the experimental AEC path
  - [components/river_voice/river_voice_vad_probe.c](/root/ameba-river/components/river_voice/river_voice_vad_probe.c)
- Extended XiaoZhi duplex runtime logs so board validation can correlate:
  - `duplex_ready`
  - `ref_activity`
  - `ref_peak`
  - `ref_ratio_q15`
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- Updated active context and duplex execution plan so `5.172` is recorded as
  landed and the next slice moves to `5.173`:
  - [doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md](/root/ameba-river/doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)

## Step 5.171
- Reworked the device-side speaking-time near-end arbitration from a single
  `interrupt` threshold into a local `duck_only -> release / interrupt` ladder:
  - [components/river_voice/river_voice_vad_probe.c](/root/ameba-river/components/river_voice/river_voice_vad_probe.c)
- Split the old barge-in trigger into three stages:
  - first qualifying near-end speech arms local ducking
  - short/noisy speech falls back through a short release window
  - only sustained near-end speech escalates to hard interrupt
- Added explicit local logs so board traces can distinguish:
  - `barge-in duck`
  - `barge-in duck release`
  - `barge-in interrupt`
  - `barge-in interrupt request failed`
- Reused the existing playback ducking path instead of introducing a new cloud
  wire event, so this slice stays device-local and keeps the current service
  protocol stable.
- Updated active context and duplex execution plan so `5.171` is recorded as
  landed and the next slice moves to `5.172`:
  - [doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md](/root/ameba-river/doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)

## Step 5.170
- Kept XiaoZhi duplex-ready speaking rounds on the device in continued-uplink
  mode instead of letting the deferred endpoint timer hard-close the local
  round while output is still speaking:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- Refined speaking-time endpoint-soft-close resolution:
  - `endpoint_soft_close` may still arm on short silence or server endpoint hint
  - but while `duplex_ready=yes` and the output lane is still effectively
    speaking, the timeout no longer resolves into `finish_active_stream()`
  - once output leaves the speaking lane, the already-expired pending close can
    resolve immediately and reuse the existing `audio.in.commit` / stop path
- Resulting runtime behavior on the duplex-ready experiment path:
  - `listening=yes` is preserved through speaking-time silence gaps
  - uplink can continue across those gaps without repeated local stop / reopen
  - repeated `listen_stop -> listen_start` jitter and extra commit churn are
    reduced
  - half-duplex baseline remains unchanged
- Updated active context and duplex execution plan so `5.170` is recorded as
  landed and the next slice moves to `5.171`:
  - [doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md](/root/ameba-river/doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)

## Step 5.169
- Softened XiaoZhi speaking-time local endpoint handling on the device so
  duplex-ready rounds no longer hard-close immediately on short silence or
  `input.endpoint`:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Added a speaking-time `endpoint_soft_close` runtime lane:
  - pending flag
  - deadline
  - reason
  - status output
- Introduced a duplex-ready-only endpoint softening gate:
  - only activates when runtime `duplex_ready=yes`
  - only activates while the output lane is effectively speaking
  - keeps the shipped half-duplex baseline unchanged
- Converted server preview milestones into hint semantics instead of local hard
  close commands:
  - `input.speech.start` now emits an explicit `interrupt hint` log on the
    duplex-ready speaking path
  - `input.endpoint(candidate=true)` now emits `hint-only endpoint` and arms a
    short deferred local-close timer instead of immediately closing the round
  - resumed preview / speech cancels the pending soft close
- Softened local silence endpointing during speaking-time duplex:
  - when local post-roll silence is reached in the duplex-ready speaking path,
    the board now arms deferred local close instead of immediately calling
    `finish_active_stream()`
  - if speech resumes before the short defer window expires, the pending close
    is canceled and the round continues
  - if silence holds through the defer window, the board then finishes the
    active stream and falls back to the existing close / result pipeline
- Extended reset / session-close paths to clear the new endpoint-soft-close
  state so stale hint timers do not leak across rounds or transports.
- Updated the active duplex context so `5.169` is now recorded as landed and
  the next slice moves to `5.170`:
  - [doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md](/root/ameba-river/doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)

## Step 5.168
- Tightened XiaoZhi duplex admission from static profile capability to a
  runtime-truthful `duplex_ready` gate:
  - [components/river_voice/river_voice_runtime_policy.c](/root/ameba-river/components/river_voice/river_voice_runtime_policy.c)
  - [include/river/river_voice_runtime_policy.h](/root/ameba-river/include/river/river_voice_runtime_policy.h)
  - [components/river_voice/river_reference_service.c](/root/ameba-river/components/river_voice/river_reference_service.c)
  - [include/river/river_reference_service.h](/root/ameba-river/include/river/river_reference_service.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- Added unified runtime duplex evaluation that now combines:
  - duplex experiment switch
  - active preproc profile capability
  - reference service state
  - recent reference activity / queue peak
  - runtime AEC gate result
- Added an explicit runtime-ready reason model:
  - `experiment_off`
  - `profile_no_ref`
  - `ref_idle`
  - `aec_blocked`
  - `ready`
- Extended reference-service runtime stats with timestamps so runtime policy can
  tell whether playback reference has actually been active recently:
  - `last_open_ms`
  - `last_reset_ms`
  - `last_write_ms`
  - `last_read_ms`
- Replaced XiaoZhi speaking-time keep-open decisions with the new runtime gate:
  - `tts_start` keep-open vs close no longer trusts only static
    `playback_ref=yes|no`
  - capture-held-during-playback now also reports the runtime duplex reason
  - adapter status output now prints one explicit duplex line with:
    - `duplex_ready`
    - `reason`
    - `aec`
    - `ref_state`
    - `ref_activity`
- Added a playback-epoch latch for duplex readiness:
  - the current playback remembers whether it ever reached `duplex_ready=yes`
  - `no_ref reopen guard` now keys off that runtime truth instead of only the
    profile's static capability
- Preserved the shipped conservative behavior by mapping runtime failures back
  to explicit half-duplex fallback reasons:
  - `half_duplex_experiment_disabled`
  - `half_duplex_no_playback_reference`
  - `half_duplex_ref_idle`
  - `half_duplex_aec_blocked`
- Updated the active duplex context so `5.168` is now recorded as landed and
  the next slice moves to `5.169`:
  - [doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md](/root/ameba-river/doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)

## Step C5
- Re-analyzed the latest `/root/agent-server` playback-truth updates and
  reprioritized the device roadmap before resuming duplex runtime work:
  - reviewed commits:
    - `2a2c9cf 补强早起播链路的播放真相与续播上下文`
    - `dd10dff 实现 segment 级 playback_ack 真相链路并补齐协议测试`
    - `d0d81ee 实现精确续播策略并补齐 playback_ack 多段联调文档`
    - `46aef68 深化软打断恢复与播放真相前推链路`
    - `74a9c6d 修复实时播放收尾卡死并收紧纠错型预热门槛`
  - conclusion:
    - service-side `segment_mark_v1` is no longer satisfied by only
      `audio.out.started/completed`
    - the device must now provide a truthful
      `started -> mark -> cleared/completed` fact chain before `5.168`
- Completed the device-side XiaoZhi `segment_mark_v1` playback ACK chain:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_xiaozhi_ws.c](/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c)
  - [include/river/river_xiaozhi_ws.h](/root/ameba-river/include/river/river_xiaozhi_ws.h)
- Added a segment-queue-based local playback fact model in the cloud adapter:
  - tracks per-segment `started`, `started_at_ms`, `last_mark_ms`,
    `expected_duration_ms`, and `is_last_segment`
  - keeps playback-level terminal state:
    - `terminal_ack`
    - `clear_reason`
    - `last_started_segment_id`
    - `last_fully_heard_segment_id`
- `audio.out.meta` handling is now playback-level instead of
  “segment switch clears everything”:
  - response/playback identity changes reset the queue
  - new segment metadata within the same playback is appended into the queue
- The async XiaoZhi control lane now covers all four fact events:
  - `audio.out.started`
  - `audio.out.mark`
  - `audio.out.cleared`
  - `audio.out.completed`
- Playback ACK negotiation is now truthful:
  - the client only declares `playback_ack.mode=segment_mark_v1` when:
    - local code supports all four ACK kinds
    - discovery also advertises all four ACK kinds
- The device now advances playback truth from actual local playback progress:
  - first successful local playback write starts the current segment
  - periodic wall-clock progress emits monotonic `audio.out.mark`
  - full-duration segment completion rolls the queue and updates
    `last_fully_heard_segment_id`
- Local stop / clear paths now converge on one playback-truth exit:
  - final partial `audio.out.mark` is emitted before clear when available
  - `audio.out.cleared` uses `cleared_after_segment_id=last_fully_heard_segment_id`
  - clear-before-start stays conservative and does not fabricate `cleared`
  - natural drain stop emits final per-segment marks and one playback-level
    `audio.out.completed`
  - interrupt / network-lost / transport-closed / bridge-close /
    playback-write-failed paths now all terminate playback truth locally
- Status output now exposes the new playback-truth lane directly:
  - terminal ack
  - clear reason
  - queued segment count
  - last started segment
  - last fully heard segment
- Updated the active duplex plan/context so `C5` is recorded as landed and the
  next implementation slice returns to `5.168`:
  - [doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md](/root/ameba-river/doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)

## Workflow Sync 2026-04-16
- Persisted the repository-level git commit message convention in
  [AGENTS.md](/root/ameba-river/AGENTS.md):
  - future `git commit` messages should use clear Chinese descriptions unless
    the user explicitly asks otherwise
- Reflected the workflow rule in the active Codex context so later sessions do
  not fall back to English commit messages by default:
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)
- Verification for this sync:
  - `python3 tools/diag/check_codex_harness.py`
  - `git diff --check`

## Step C4
- Aligned the device-side XiaoZhi accepted-turn semantics with the 2026-04-16
  server collaboration boundary so accepted-turn, preview observations,
  playback facts, and fallback reasons are now distinct runtime concepts:
  - [include/river/river_xiaozhi_ws.h](/root/ameba-river/include/river/river_xiaozhi_ws.h)
  - [components/river_cloud/river_xiaozhi_ws.c](/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- Added an explicit turn-semantics runtime layer inside the cloud adapter:
  - caches `turn_id`, `accept_reason`, `input_state`, `output_state`, and
    `barge_in_enabled`
  - treats accepted-turn as sticky only after a real `accept_reason` arrives
  - exposes the current turn-semantics/fallback state in
    `river_cloud_adapter_dump_status()`
- Tightened pending-text finalization semantics:
  - pending XiaoZhi transcript now waits for accepted-turn before emitting the
    existing `RIVER_CLOUD_ASR_EVENT_FINAL`
  - when accepted-turn is not ready yet, the board logs
    `xiaozhi pending text waits for accepted turn`
  - once accepted-turn appears, the board logs
    `xiaozhi accepted turn final text`
- Fixed stale turn-semantics leakage across rounds:
  - new public transport helper:
    - `river_xiaozhi_clear_session_update_cache()`
  - the cloud session layer now clears transport `session.update` cache before:
    - opening a fresh listen round
    - re-opening the conversation window from wake admission
- Added explicit conservative fallback reasoning without changing the shipped
  half-duplex baseline:
  - `half_duplex_experiment_disabled`
  - `half_duplex_no_playback_reference`
  - `half_duplex_capture_held_during_playback`
  - `await_accept_reason`
- Reframed playback metadata as playback facts instead of strategy commands:
  - `audio.out.meta` handling now logs
    `xiaozhi playback fact observed: ... accepted=...`
  - preview events remain observation-only and playback ACKs remain fact-only
- Updated the active duplex plan/context to mark `C4` as landed and move the
  next implementation slice to `5.168`:
  - [doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md](/root/ameba-river/doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)
- Verification for this step:
  - `python3 tools/diag/check_codex_harness.py`
  - `git diff --check`
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'`
  - `rg -n 'clear_session_update_cache|turn accepted|await_accept_reason|turn_semantics|half_duplex_experiment_disabled|half_duplex_no_playback_reference|half_duplex_capture_held_during_playback|finalize_pending_text\\(\"' include/river/river_xiaozhi_ws.h components/river_cloud/river_xiaozhi_ws.c components/river_cloud/river_cloud_internal.h components/river_cloud/river_cloud_xiaozhi_session.c components/river_cloud/river_cloud_adapter.c .codex/active_context.md doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md`

## Step C3
- Implemented the device-side XiaoZhi playback-truth baseline so the transport
  and cloud adapter now consume the server's playback context and report the
  first playback facts back asynchronously:
  - [include/river/river_xiaozhi_ws.h](/root/ameba-river/include/river/river_xiaozhi_ws.h)
  - [components/river_cloud/river_xiaozhi_ws.c](/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- Extended the XiaoZhi event contract with playback-truth metadata:
  - new event type:
    - `RIVER_XIAOZHI_EVENT_AUDIO_OUT_META`
  - new per-event fields:
    - `response_id`
    - `playback_id`
    - `segment_id`
    - `expected_duration_ms`
    - `is_last_segment`
- The websocket transport now parses, caches, logs, and exposes:
  - `audio.out.meta`
- `session.start.capabilities.playback_ack` negotiation is now truthful for the
  implemented baseline:
  - local playback ACK support is advertised as `segment_mark_v1`
  - actual declaration still depends on discovery advertising the same mode
- Added the minimal async playback-fact ACK path on the existing XiaoZhi
  control queue:
  - `audio.out.started` is queued after the first successful local playback
    write for the active segment
  - `audio.out.completed` is queued from the natural last-segment playback stop
    path
  - ACK sending failures only log warnings and do not block playback
- Added playback-meta runtime state on both transport and cloud-adapter sides so
  board validation can inspect the last playback context directly from status
  output:
  - transport status now prints:
    - `xiaozhi playback_meta=response_id=...`
  - cloud adapter status now prints:
    - `xiaozhi playback_meta response_id=...`
- Kept the scope intentionally minimal:
  - `audio.out.mark` and `audio.out.cleared` are still deferred to later work
  - no new playback listener plumbing was introduced; the existing XiaoZhi IO
    task and control queue remain the async send path
  - playback-truth metadata is reset on session/open transport resets so stale
    ACK context is not reused
- Updated the active duplex execution context to record that `C3` is now the
  latest landed collaboration slice and that `C4` is next:
  - [doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md](/root/ameba-river/doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)
- Verification for this step:
  - `python3 tools/diag/check_codex_harness.py` passed
  - `git diff --check` passed
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'` completed successfully against
    `/root/ameba-rtos`

## Step C2
- Implemented the device-side XiaoZhi preview-observation baseline so the
  transport now truthfully consumes the server's preview-aware input events:
  - [include/river/river_xiaozhi_ws.h](/root/ameba-river/include/river/river_xiaozhi_ws.h)
  - [components/river_cloud/river_xiaozhi_ws.c](/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- Extended the XiaoZhi event contract with preview-observation metadata:
  - new event types:
    - `RIVER_XIAOZHI_EVENT_INPUT_SPEECH_START`
    - `RIVER_XIAOZHI_EVENT_INPUT_PREVIEW`
    - `RIVER_XIAOZHI_EVENT_INPUT_ENDPOINT`
  - new per-event fields:
    - `preview_id`
    - `stable_prefix`
    - `reason`
    - `source`
    - `audio_offset_ms`
    - `candidate`
    - `is_final`
- The websocket transport now parses, caches, logs, and exposes:
  - `input.speech.start`
  - `input.preview`
  - `input.endpoint`
- Added preview runtime state on both transport and cloud-adapter sides so
  board validation can inspect the most recent preview observation window from
  status output:
  - transport status now prints:
    - `xiaozhi preview_state=...`
  - cloud adapter status now prints:
    - `xiaozhi preview preview_id=...`
- `session.start.capabilities.preview_events` negotiation is now truthful:
  - local preview-event support is marked available
  - actual declaration still depends on discovery advertising
  - `playback_ack` remains untouched and deferred to `C3`
- Kept the scope intentionally observation-only:
  - preview events only refresh logs, cached observation state, and follow-up
    window bookkeeping
  - preview text is not emitted as existing ASR partial/final callbacks
  - `input.endpoint` does not force commit or local close
  - accepted-turn semantics still stay on `session.update.accept_reason`
- Updated the active duplex execution context to record that `C2` is now the
  latest landed collaboration slice and that `C3` is next:
  - [doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md](/root/ameba-river/doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)
- Verification for this step:
  - `python3 tools/diag/check_codex_harness.py` passed
  - `git diff --check` passed
  - `python3 /root/ameba-rtos/ameba.py build -p` completed successfully against
    `/root/ameba-rtos`

## Step C1
- Implemented the device-side XiaoZhi collaboration negotiation baseline in
  [components/river_cloud/river_xiaozhi_ws.c](/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c)
  so future duplex protocol slices can be enabled by discovery instead of hard
  coding capability bits.
- Added a non-fatal discovery fetch before websocket connect:
  - derives `GET /v1/realtime` from the configured realtime websocket URL
  - parses `turn_mode`, `server_endpoint`, and
    `voice_collaboration.preview_events/playback_ack`
  - caches the server-advertised collaboration profile in the XiaoZhi transport
    context
- Added explicit discovery cache invalidation on credential changes:
  - runtime bootstrap credential reset now clears discovery state
  - websocket URL changes from bootstrap now clear discovery state
  - manual `url` / `token` overrides also clear discovery state when changed
- `session.start.capabilities` now negotiates collaboration fields instead of
  assuming them:
  - `preview_events=true` is only declared when both server discovery and local
    client support say yes
  - `playback_ack.mode=segment_mark_v1` is only declared when both sides agree
  - current local support remains intentionally disabled, so the shipped default
    path still falls back to the old compatibility baseline
- Extended logs and status dumping so board-side validation can see both the
  advertised server abilities and the actually declared client abilities:
  - `xiaozhi discovery ready: ...`
  - `xiaozhi session.start sent: ... preview_events=... playback_ack=...`
  - `river xiaozhi status` now prints a discovery/collaboration summary line
- Updated the active full-duplex execution context to record that `C1` is now
  the latest landed collaboration slice and that `C2` is the next protocol
  implementation step:
  - [doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md](/root/ameba-river/doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)
  - [.codex/active_plans.md](/root/ameba-river/.codex/active_plans.md)
- Verification for this step:
  - `python3 tools/diag/check_codex_harness.py` passed
  - `git diff --check` passed
  - `python3 /root/ameba-rtos/ameba.py build -p` completed successfully against
    `/root/ameba-rtos`

## Plan Sync 2026-04-16
- Re-read the latest `/root/agent-server` protocol and architecture docs and
  aligned the device-side duplex roadmap to the new server-driven collaboration
  boundary:
  - [doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md](/root/ameba-river/doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)
- Captured the key new service-side constraints that materially change the
  device plan:
  - the device is not a second turn-orchestration layer
  - `accept_reason` is the accepted-turn signal
  - `input.speech.start/input.preview/input.endpoint` are observation events
  - `audio.out.started/mark/cleared/completed` are playback facts
  - all of the above must be enabled through discovery +
    `session.start.capabilities` negotiation
- Added four device-side collaboration slices ahead of the deeper runtime
  duplex tuning work:
  - `C1` discovery + `session.start` collaboration negotiation baseline
  - `C2` preview-aware input-event consumption baseline
  - `C3` playback-truth metadata and ACK baseline
  - `C4` accepted-turn / playback-truth / fallback semantics alignment
- Kept the existing local-runtime duplex slices in place after the new protocol
  baseline work:
  - `5.168` runtime-ready duplex gate
  - `5.169` speaking-time local endpoint softening
  - `5.170` speaking-time uplink continuation
  - `5.171` duck-first interruption policy
  - `5.172` duplex-ready acoustic baseline
  - `5.173` default-enable / fallback matrix
- Why this planning sync matters:
  - before reading the new server docs, the device roadmap was skewed toward
    local keep-open / AEC / ducking work
  - after the new docs, it is clear that the device also needs an explicit
    protocol-collaboration track so future duplex work is negotiated and
    observable, not hardcoded

## Step 5.167
- Consumed the richer native realtime `session.update` fields on the device
  transport side without changing session-control behavior yet:
  - [include/river/river_xiaozhi_ws.h](/root/ameba-river/include/river/river_xiaozhi_ws.h)
  - [components/river_cloud/river_xiaozhi_ws.c](/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c)
  - [doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md](/root/ameba-river/doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)
- Added explicit device-side cache/accessors for service-side split-lane hints:
  - `last_session_state`
  - `last_input_state`
  - `last_output_state`
  - `last_turn_id`
  - `last_accept_reason`
  - `last_barge_in_enabled` with a separate known/unknown flag
- `session.update` handling now parses and logs:
  - `input_state`
  - `output_state`
  - `barge_in_enabled`
  - `turn_id`
  - `accept_reason`
  while preserving compatibility when older servers omit them.
- Kept the scope intentionally narrow:
  - the existing `state == active && response_started -> stop local tts marker`
    behavior is unchanged
  - no local round, playback, interrupt, or commit logic changed in this step
  - this slice is observability/state-plumbing only
- Extended `river xiaozhi status` output with a second lane-state line so board
  validation can see the last server-reported split-lane view directly.

## Step 5.166
- Converged the device-side full-duplex roadmap into concrete, sequential
  implementation slices instead of keeping it at a broad task-block level:
  - [doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md](/root/ameba-river/doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)
- Updated the plan to reflect the latest local service-side reality:
  - `/root/agent-server` is no longer treated as "still single-state only"
  - device work should no longer wait for a hypothetical server-side dual-track
    refactor before moving
  - the remaining end-to-end bottleneck is now framed more accurately as:
    - device-side acoustic readiness
    - device-side local round / uplink / playback orchestration
    - consumption of richer service-side lane-state signals
- The plan now explicitly stages the next device-side code work as:
  - `5.167` richer `session.update` consumption
  - `5.168` runtime-ready duplex gate
  - `5.169` speaking-time local endpoint softening
  - `5.170` speaking-time uplink continuation
  - `5.171` duck-first interruption policy
  - `5.172` board-profile duplex-ready acoustic baseline
  - `5.173` default-enable and fallback matrix
- Why this step exists as its own landed slice:
  - the repository already has two narrow duplex guard steps in place:
    - capability advertisement gate
    - `tts_start` local-round policy gate
  - without a concrete next-slice sequence, further duplex edits would likely
    mix:
    - observability
    - runtime gating
    - endpoint softening
    - uplink concurrency
    - ducking policy
    - board acoustic validation
  - this step defines that sequence before behavior changes resume

## Step 5.165
- Added the next device-side full-duplex experiment slice for XiaoZhi
  `tts_start` handling:
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)
- This step moves one hardcoded turn-taking behavior behind an explicit policy:
  - before:
    - server `tts_start` always forced a local round close
  - after:
    - default build still closes the local round exactly as before
    - full-duplex experiment builds now decide `tts_start` behavior from:
      - experiment gate enabled or not
      - whether the active voice profile exposes playback-reference support
- New XiaoZhi session helpers now expose:
  - `river_cloud_xiaozhi_full_duplex_experiment_enabled()`
  - `river_cloud_xiaozhi_keep_local_round_on_tts_start()`
- Runtime policy after this step:
  - if duplex experiment is off:
    - keep the old conservative `tts_start -> close local round` behavior
  - if duplex experiment is on but playback reference is still unavailable:
    - fall back to the old close behavior
    - log:
      - `xiaozhi tts_start falls back to round close: duplex_experiment=yes playback_ref=no`
  - if duplex experiment is on and playback reference is available:
    - do not hard-close the local round on `tts_start`
    - log:
      - `xiaozhi tts_start keeps local round open: duplex_experiment=yes playback_ref=yes ...`
- Why this slice is still conservative:
  - it does not globally enable overlapping capture/playback
  - it only removes one hard stop when both of these are true:
    - the duplex experiment was enabled intentionally
    - the active board profile can already provide playback reference to VAD/AEC
  - this keeps the default branch stable while creating a board-visible
    stepping stone toward true duplex behavior

## Step 5.164
- Added a device-side XiaoZhi duplex capability experiment gate so the
  websocket `session.start` contract is no longer hardcoded to
  `half_duplex=true`:
  - [Kconfig](/root/ameba-river/Kconfig)
  - [prj.conf](/root/ameba-river/prj.conf)
  - [components/river_cloud/river_xiaozhi_ws.c](/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)
- New build-time gate:
  - `CONFIG_RIVER_XIAOZHI_FULL_DUPLEX_EXPERIMENT_EN`
- Current scope of this step is intentionally narrow and board-safe:
  - default profile remains on the conservative half-duplex baseline
  - runtime listen / commit / local-close behavior is unchanged
  - only the advertised XiaoZhi session capability and diagnostics change
- Device-side behavior after this step:
  - default build still advertises:
    - `capabilities.half_duplex=true`
  - an explicit experiment build can now advertise:
    - `capabilities.half_duplex=false`
  - `session.start` log now also prints:
    - `duplex=...`
    - `half_duplex=yes|no`
- Why this slice lands first:
  - current logs still show missing AEC/reference readiness on board:
    - `echo:0B`
    - `ref_peak=0`
  - the service side is also still evolving toward a real duplex contract
  - so the safest first device change is to separate:
    - capability negotiation
    - later behavior changes such as simultaneous capture/playback policy

## Step 5.163
- Added a dedicated cross-repo full-duplex voice execution plan and registered
  it as a secondary active plan:
  - [doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md](/root/ameba-river/doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md)
  - [.codex/active_plans.md](/root/ameba-river/.codex/active_plans.md)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)
- The new document records the current judgement that the system is not yet
  true full duplex, even though both sides already have important building
  blocks:
  - device side:
    - bidirectional realtime audio transport
    - barge-in detection / interrupt hooks
    - playback ducking API
  - server side:
    - `StreamingTranscriber`
    - `InputPreview`
    - adaptive barge-in
    - `StreamingResponder`
    - `SpeechPlanner`
    - heard-text persistence
- The plan also captures the current blocking facts that make the system still
  turn-oriented:
  - device side still advertises:
    - `half_duplex=true`
  - device side still closes the local input round on:
    - `tts_start`
  - service discovery still defaults to:
    - `turn_mode=client_wakeup_client_commit`
  - service input preview still primarily drives:
    - `commitSuggested`
  - service session core is still a single state machine:
    - `active`
    - `thinking`
    - `speaking`
- The execution plan is deliberately split into:
  - service-side task blocks `S1..S4`
  - device-side task blocks `D1..D4`
  - joint / protocol task blocks `J1..J2`
- The current top-level recommendation is now documented explicitly:
  - first make AEC / playback reference truly usable on device side
  - in parallel, thicken the server-side `Voice Orchestration Core`
  - only after both are stable should discovery / public protocol wording move
    from `client_commit` semantics toward a true duplex contract

## Step 5.162
- Extended the XiaoZhi post-commit response wait so the board does not close
  the follow-up window too soon after a longer utterance:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)
- Root cause from the latest board logs on `2026-04-15`:
  - wake, websocket connect, `session.start`, and uplink all still succeeded:
    - `xiaozhi session.start sent`
    - `xiaozhi asr round begin: ... packets=129 ... busy=0 fail=0`
  - the server did acknowledge turn-end and entered:
    - `xiaozhi session.update: ... state=thinking`
  - but there were still no downstream:
    - `stt`
    - `response.start`
    - `response.chunk`
    - audio frames
  - the board then closed the conversation window only about `3s` after commit:
    - `xiaozhi local close deferred: wait_ms=2000`
    - `xiaozhi local close resolved: trigger=timeout`
    - `xiaozhi conversation window closed: reason=followup_timeout`
  - inference:
    - the fixed `8s` follow-up window was being measured from wake admission,
      not from end-of-speech
    - on longer utterances, that left too little time for server-side
      `thinking -> response.start`
- This step adds a focused timing fix:
  - when River finishes the active uplink stream and sends commit, it now
    refreshes the conversation window for an additional bounded
    post-commit response wait
  - the board also logs:
    - `xiaozhi response wait armed after commit: timeout_ms=6000`
- Expected board-side change:
  - after `state=thinking`, the websocket should stay alive long enough for a
    legitimate delayed `response.start` / audio response to arrive
  - avoid the current pattern where `followup_timeout` closes the session only
    a few seconds after commit on longer utterances

## Step 5.161
- Tightened the native realtime round-close state machine so River stops the
  local ASR round as soon as the server begins responding:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)
- Root cause from the latest board logs on `2026-04-15`:
  - the cloud session and audio uplink were already healthy:
    - `response.start`
    - `response.chunk: agent-server received text input: 今天周几啊？`
    - `packets=141 busy=0 fail=0 stale_drop=0 ring_drop=0`
  - but River sometimes kept the local ASR round open after the server had
    already started its response
  - when local VAD later reached post-roll, River still executed its own
    delayed close path and sent a late `audio.in.commit`
  - that created misaligned state transitions like:
    - `local close deferred`
    - `session.update: state=thinking`
    - `reason=reopen_overlap`
- This step adds a focused local fix:
  - when the realtime transport emits `tts/start` (currently driven by
    `response.start`, including `tts_provider=none` mode), River now closes the
    active local ASR round immediately
  - the helper clears pending local close / pending listen-stop state, resets
    queued uplink tail audio, emits local session-closed, and finishes the
    round with reason `tts_start`
  - this avoids a late client-side `audio.in.commit` after the server has
    already auto-endpointed and begun generating the response
- Expected board-side change:
  - once `response.start` arrives, River should stop waiting for the old
    local-close timeout for that round
  - the log sequence should stop showing a second local `thinking` transition
    caused by a late close after the response has already started
  - `reopen_overlap` caused by overlap with a still-pending local close should
    reduce for this server-response-started path

## Step 5.160
- Fixed a real websocket-lifecycle cleanup bug in the River native realtime
  client and tightened transport-close state reset:
  - [components/river_cloud/river_xiaozhi_ws.c](/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)
- Root cause analysis from the latest board log on `2026-04-14`:
  - the first wake now reaches:
    - `Connected to websocket server`
    - `xiaozhi transport ready`
    - `xiaozhi session.start sent`
    - `xiaozhi session.update: ... state=active`
  - but the socket then drops unexpectedly about `1.9s` later:
    - `xiaozhi websocket closed sid=sess_...`
    - `ws_poll: ERROR: Read data failed!`
  - a host-side reproduction against the same cloud server using the same
    `session.start` payload plus `51` raw `pcm16le/16k/mono` frames stayed open
    for multiple seconds and did not get server-closed
  - inference:
    - the remaining issue is on the board-side websocket lifecycle, not the
      server's normal realtime policy
  - the project close path also had a concrete local bug:
    - `river_xiaozhi_close_context()` called `ws_close()` for `WSC_OPEN`
      sockets, but `ws_close()` only sends CLOSE and flips state to `CLOSING`
    - River then immediately `ws_free()`'d the outer `wsclient_context`
      without running SDK `client_close()` teardown
    - that skips the real socket / queue / mutex cleanup and can poison later
      reconnect behavior
- This step fixes the local cleanup path directly:
  - after optional `ws_close()`, River now calls SDK `client_close()` for
    `WSC_OPEN`, `WSC_CONNECTING`, and `WSC_CLOSING` contexts before freeing the
    outer `wsclient_context`
  - transport-close callback now clears the local `session_id` after emitting
    the close event so the next wake does not inherit stale session identity
- Expected runtime change on board after this step:
  - explicit local close / reopen paths should no longer leave half-torn-down
    websocket resources behind
  - after an unexpected transport close, the next wake should start from a
    clean local session context
  - wake-admission logs after a transport close should no longer show the
    previous session id
- Validation executed on 2026-04-14:
  - host-side server behavior check:
    - realtime websocket upgrade succeeded with subprotocol
      `agent-server.realtime.v0`
    - a simulated `session.start + 51 PCM frames` run stayed connected and did
      not reproduce the board's ~`1.9s` drop
  - repo harness:
    - `cd /root/ameba-river`
    - `python3 tools/diag/check_codex_harness.py`
  - latest-SDK build:
    - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'`
- Current conclusion:
  - cloud service reachability and native session start are now both verified
  - this step removes a real board-side wsclient teardown defect that could
    corrupt follow-up reconnects after transport loss

## Step 5.159
- Added a repo-tracked SDK patch tool to fix misleading websocket-open failures
  during the native realtime migration:
  - [tools/sdk/apply_wsclient_connect_error_patch.py](/root/ameba-river/tools/sdk/apply_wsclient_connect_error_patch.py)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)
- Root cause analysis from the latest board log on `2026-04-14`:
  - repeated wake attempts still stopped at:
    - `ws_connect_url: ERROR: Sending handshake failed`
  - but an external probe from the same day against the deployed server showed:
    - `101.33.235.154:8080` `Connection refused`
    - `101.33.235.154:443` `Connection refused`
    - `101.33.235.154:80` `Connection refused`
  - the Ameba SDK `ws_hostname_connect()` uses nonblocking `connect()` plus
    `select()`, but treats any writable socket as success and never checks
    `getsockopt(... SO_ERROR ...)`
  - inference:
    - a real TCP connect failure can be misreported downstream as
      `Sending handshake failed`
    - that makes board logs ambiguous exactly when cloud reachability changes
- This step adds a reproducible SDK-side patch from inside the repo:
  - after `select_ret == 1`, the patch now verifies `SO_ERROR == 0` before
    declaring the socket connected
  - failed connects now log the real reason:
    - `Connect failed after select: so_error=...`
    - `Connect timeout after ... ms`
    - `Connect select failed ret(...) errno(...)`
    - `connect failed ret(...) errno(...)`
- Expected runtime change on board after this step:
  - when the cloud port is actually down, the old ambiguous failure should be
    replaced by an explicit connect-stage error instead of
    `ws_connect_url: ERROR: Sending handshake failed`
  - when the cloud service comes back, any remaining failure will stay in the
    HTTP/WebSocket handshake layer and can be debugged separately
- Validation executed on 2026-04-14:
  - realtime server reachability probe:
    - `python3 tools/agent_server_debug/probe_realtime.py --host 101.33.235.154 --ports 443 8080 80 --schemes ws wss http https`
    - result:
      - all probed ports returned `Connection refused`
  - repo harness:
    - `cd /root/ameba-river`
    - `python3 tools/diag/check_codex_harness.py`
  - latest-SDK build after applying the patch:
    - `bash -lc 'cd /root/ameba-river && python3 tools/sdk/apply_wsclient_connect_error_patch.py --sdk-root /root/ameba-rtos && export AMEBA_SDK_ROOT=/root/ameba-rtos && source /root/ameba-river/env.sh && python3 /root/ameba-rtos/ameba.py build -p'`
- Current conclusion:
  - the latest cloud deployment is not reachable from this machine at
    `2026-04-14`
  - this step hardens the board-side diagnostics so the next flash will say
    whether the remaining blocker is still raw TCP reachability or a true
    websocket-handshake defect

## Step 5.158
- Fixed a second Ameba SDK websocket handshake integration bug exposed after
  removing the duplicate subprotocol header:
  - [components/river_cloud/river_xiaozhi_ws.c](/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)
- Root cause analysis from the latest board log:
  - the previous `400 Bad Request` disappeared
  - the new failure moved earlier to:
    - `ws_connect_url: ERROR: Sending handshake failed`
  - the Ameba SDK `ws_handshake_header_set_protocol()` and
    `ws_handshake_set_header_fields()` helpers allocate `len + 1` bytes but only
    `memcpy(len)` and do not append a terminator
  - River was passing:
    - `strlen(...)`
  - inference:
    - the copied strings inside `wsclient` were not guaranteed to be
      NUL-terminated
    - later SDK `sprintf("%s ... %s")` handshake assembly could read beyond the
      copied buffers and corrupt the outbound handshake request
- The fix now passes explicit NUL-inclusive lengths to the SDK helpers:
  - `strlen(handshake_protocol) + 1`
  - `strlen(open_header_fields) + 1`
- Expected runtime change on board after this step:
  - the old send failure should disappear:
    - no `ws_connect_url: ERROR: Sending handshake failed`
  - the upgrade should proceed into either:
    - `Connected to websocket server`
    - or a later server-side protocol log if another issue remains
- Validation executed on 2026-04-14:
  - harness:
    - `cd /root/ameba-river`
    - `python3 tools/diag/check_codex_harness.py`
    - result: `check_codex_harness: all checks passed`
  - latest-SDK build:
    - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'`
    - result: `Build done`
- Current conclusion:
  - the remaining websocket-open blocker was still on the project-side client
    integration, not on the cloud deployment
  - this step fixes the SDK string-lifetime / terminator mismatch directly at
    the handshake boundary
  - next board validation should show whether the native upgrade now completes

## Step 5.157
- Fixed the native realtime WebSocket handshake on Ameba so the SDK no longer
  sends an invalid / conflicting subprotocol header:
  - [components/river_cloud/river_xiaozhi_ws.c](/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)
- Root cause analysis from the latest board log:
  - TCP and plain `ws://101.33.235.154:8080` reachability were already healthy
  - the failure moved to handshake time:
    - `HTTP/1.1 400 Bad Request`
  - River was manually appending:
    - `Sec-WebSocket-Protocol: agent-server.realtime.v0`
  - but the Ameba SDK websocket client also auto-generates its own
    `Sec-WebSocket-Protocol` header and falls back to:
    - `chat, superchat`
  - inference:
    - the board request could carry conflicting or duplicated subprotocol
      headers, which is consistent with the cloud server rejecting the upgrade
- The fix changes handshake ownership to the SDK-native path:
  - removed `Sec-WebSocket-Protocol` from the custom `header_fields`
  - now calls `ws_handshake_header_set_protocol()` with
    `agent-server.realtime.v0`
  - `header_fields` now only carry the custom device metadata / auth headers
- Expected runtime change on board after this step:
  - the old handshake failure should disappear:
    - no `Got bad status connecting to HTTP/1.1 400 Bad Request`
  - wake admission should move forward into native transport/session logs:
    - `xiaozhi transport ready: ...`
    - `xiaozhi session.start sent: ...`
- Validation executed on 2026-04-14:
  - harness:
    - `cd /root/ameba-river`
    - `python3 tools/diag/check_codex_harness.py`
    - result: `check_codex_harness: all checks passed`
  - latest-SDK build:
    - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'`
    - result: `Build done`
- Current conclusion:
  - the remaining blocker has been reduced from network reachability to a pure
    handshake-header issue
  - this step fixes the most likely cause in the project-side client
  - next board validation should confirm whether the websocket upgrade now
    reaches `101 Switching Protocols`

## Step 5.156
- Added a reusable local cloud-debug toolkit for the self-hosted agent-server
  deployment:
  - [tools/agent_server_debug/README.md](/root/ameba-river/tools/agent_server_debug/README.md)
  - [tools/agent_server_debug/probe_realtime.py](/root/ameba-river/tools/agent_server_debug/probe_realtime.py)
- `probe_realtime.py` uses only Python standard library and now covers:
  - raw TCP reachability
  - HTTP/HTTPS discovery probing for `/v1/realtime`
  - WS/WSS upgrade probing for `/v1/realtime/ws`
  - optional TLS SNI / Host override for future reverse-proxy debugging
- Ran the new probe from the host network against the deployed cloud server
  `101.33.235.154` and classified the current failure precisely:
  - `101.33.235.154:443` is closed:
    - TCP connect failed with `Connection refused`
  - `101.33.235.154:8080` is open but serves plain HTTP/WS, not TLS:
    - `http://101.33.235.154:8080/v1/realtime` returned `200 OK`
    - `ws://101.33.235.154:8080/v1/realtime/ws` completed `101 Switching Protocols`
    - `https://101.33.235.154:8080/...` and `wss://101.33.235.154:8080/...`
      failed with TLS record-layer errors because the endpoint is not HTTPS
- Based on the verified cloud probe, updated the board-side default native
  realtime endpoint:
  - [include/river/river_xiaozhi_credentials.h](/root/ameba-river/include/river/river_xiaozhi_credentials.h)
  - from: `wss://101.33.235.154/v1/realtime/ws`
  - to: `ws://101.33.235.154:8080/v1/realtime/ws`
- This explains the earlier board log exactly:
  - `net_connect -68` / `xiaozhi_ws_connect_failed` was caused by trying
    `wss` on `443`, where the cloud server is not listening
- Probe output also showed the current discovery surface is still bootstrap-like
  on the cloud deployment:
  - `voice_provider":"funasr_http"`
  - `tts_provider":"none"`
  - inference: transport bring-up should work after the URL fix, but native
    audio reply playback may still be absent until cloud-side TTS is enabled
- Validation executed on 2026-04-14:
  - syntax check:
    - `cd /root/ameba-river`
    - `python3 -m py_compile tools/agent_server_debug/probe_realtime.py`
  - host-network probe:
    - `cd /root/ameba-river`
    - `python3 tools/agent_server_debug/probe_realtime.py --host 101.33.235.154`
    - observed:
      - `443`: refused
      - `8080/http`: `200 OK`
      - `8080/ws`: `101 Switching Protocols`
      - `8080/https` and `8080/wss`: TLS record-layer failure
- Current conclusion:
  - the correct current debug-branch target is plain
    `ws://101.33.235.154:8080/v1/realtime/ws`
  - if `wss` is required later, that must be added on the server or via an
    external TLS reverse proxy first
  - after this step, the next board pass should recheck plain-WS session bring-up

## Step 5.155
- Swapped the debug-branch realtime transport from XiaoZhi wire compatibility
  to direct self-hosted `agent-server` `rtos-ws-v0` while keeping the current
  upper `river_xiaozhi_*` / cloud state-machine surface intact:
  - [include/river/river_xiaozhi_credentials.h](/root/ameba-river/include/river/river_xiaozhi_credentials.h)
  - [include/river/river_xiaozhi_ws.h](/root/ameba-river/include/river/river_xiaozhi_ws.h)
  - [components/river_cloud/river_xiaozhi_ws.c](/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)
- Default realtime credentials now target the deployed native server directly:
  - websocket URL: `wss://101.33.235.154/v1/realtime/ws`
  - websocket subprotocol: `agent-server.realtime.v0`
  - protocol version string: `rtos-ws-v0`
  - uplink format: `pcm16le`
  - legacy OTA/bootstrap is disabled by default on this branch
- `river_xiaozhi_ws.c` now speaks native realtime control semantics:
  - handshake adds `Sec-WebSocket-Protocol: agent-server.realtime.v0`
  - `open_session()` now only opens the websocket and marks transport ready
  - `listen_start()` maps to `session.start`
  - `listen_stop()` maps to `audio.in.commit`
  - `listen.detect()` auto-starts a session if needed, then sends `text.in`
  - `abort()` maps to `session.update { interrupt: true }`
  - `close_session()` sends `session.end` before closing the socket
  - websocket binary send/receive now uses raw frames with no XiaoZhi v2/v3
    binary header
- The native text-frame parser now translates realtime events back into the
  existing upper-layer event contract so the current cloud adapter keeps working
  during the migration:
  - `response.start` -> synthetic compat `tts state=start`
  - `response.chunk[text]` -> synthetic compat `tts state=sentence_start`
  - `session.update(state=active)` after a response -> synthetic compat
    `tts state=stop`
  - `session.end` -> compat `session_closed`
  - `error` -> compat transport error event
  - `session.end` now clears only the dialog state and ended `session_id`; the
    websocket transport stays reusable so follow-up rounds can send a fresh
    `session.start` without forcing a reconnect
- `river_cloud_adapter.c` now uses PCM rather than Opus for the native path:
  - uplink keeps the existing 16 ms capture -> 20 ms accumulator, but sends raw
    640-byte `pcm16le/16k/mono` frames
  - downlink accepts native PCM16 binary frames directly into the playback ring
  - the legacy Opus decoder path is retained only as a fallback for non-native
    binary types during the transition
  - `RIVER_CLOUD_XIAOZHI_UPLINK_PACKET_MAX` is raised to the native 20 ms PCM
    frame size
- Updated the Codex volatile context to the actual working branch:
  - `agent-server-v2`
- Validation executed on 2026-04-14:
  - harness:
    - `cd /root/ameba-river`
    - `python3 tools/diag/check_codex_harness.py`
    - result: `check_codex_harness: all checks passed`
  - latest-SDK build:
    - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'`
    - result: `Build done`
- Current conclusion:
  - this first migration slice is compile-verified on `/root/ameba-rtos`
  - the upper runtime still uses compat event names, but transport and audio
    framing are now native realtime/PCM
  - the next board pass should verify:
    - wakeword admission connects straight to `101.33.235.154`
    - no per-wake OTA/bootstrap log remains
    - `session.start` / `audio.in.commit` / PCM playback all work on board
    - a follow-up turn reuses the existing websocket and issues a fresh
      `session.start` instead of reconnecting

## Step 5.154
- Tightened XiaoZhi local `post_roll/close` settlement without delaying the
  existing `listen_stop` trigger:
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- The local runtime now distinguishes:
  - transport stop intent (`listen_stop_pending`)
  - deferred local close settlement (`close_pending`)
- Added a bounded XiaoZhi local-close defer window:
  - `RIVER_CLOUD_XIAOZHI_LOCAL_CLOSE_DEFER_MS = 2000`
- When a round reaches local `post_roll` before cloud semantics are ready, the
  adapter now:
  - arms `xiaozhi local close deferred: wait_ms=2000`
  - waits until `listen_stop` is fully finalized
  - settles local `session_closed` / per-round finish on:
    - `post_stop_result`
    - `llm`
    - `tts_start`
    - `tts_stop`
    - `timeout`
    - `reopen_overlap`
- Added new runtime observability for board triage:
  - `xiaozhi local close deferred: ...`
  - `xiaozhi local close resolved: trigger=... partial=... final=...`
  - `river xiaozhi status` now prints:
    - `close_pending=...`
    - `close_left_ms=...`
- Validation executed on 2026-04-13:
  - harness:
    - `cd /root/ameba-river`
    - `python3 tools/diag/check_codex_harness.py`
    - result: `check_codex_harness: all checks passed`
  - latest-SDK build:
    - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'`
    - result: `Build done`
- Current conclusion:
  - this step is compile-verified on `/root/ameba-rtos`
  - the next board pass should verify whether genuine follow-up rounds stop
    producing premature local close / empty-round accounting
  - if empties remain, the new `close_pending` and local-close resolution logs
    should now show whether the remaining issue is timeout-driven or reopen-driven

## Step 5.153
- Fixed XiaoZhi downlink websocket receive classification for fragmented server
  messages:
  - [components/river_cloud/river_xiaozhi_ws.c](/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c)
- Project-side websocket receive handling now distinguishes reassembled JSON from
  reassembled binary payloads:
  - `BINARY_FRAME` still goes directly to the binary handler
  - `CONTINUATION` now also goes to the binary handler when the reassembled
    payload prefix does not look like JSON
- Added low-noise malformed-frame warnings in the XiaoZhi binary decoder:
  - `binary_v2_short`
  - `binary_v2_payload_invalid`
  - `binary_v3_short`
  - `binary_v3_payload_invalid`
- Rebuilt the latest-SDK image against `/root/ameba-rtos` after the websocket
  receive fix:
  - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'`
  - result: `Build done`
- Board validation executed on 2026-04-13:
  - flash:
    - `bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"`
    - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 /root/ameba-river/tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'`
  - monitor:
    - `python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000`
  - runtime commands:
    - `river xiaozhi connect`
    - `river xiaozhi listen detect 今天天气怎么样`
    - `river xiaozhi status`
- Observed results:
  - `server hello: sid=29fefc9e sample_rate=24000 frame_duration=20ms mcp=yes`
  - during the board validation pass, transient local tracing confirmed repeated
    protocol-v3 downlink binary frames were reaching the project-side binary
    handler; that tracing was removed before landing this step
  - `playback start: stream=xiaozhi_tts ...`
  - `playback stop: stream=xiaozhi_tts epoch=3`
  - `xiaozhi no_ref reopen guard armed: tail_ms=480 silence_frames=6`
  - runtime status after reconnect showed downlink audio is now live:
    - `audio_rx=280`
    - `server_audio=24000Hz/20ms`
    - `sample=24000Hz frame=20ms`
- Conclusion:
  - the remaining blocker is no longer `audio_rx=0` / missing playback
  - the fragmented-downlink receive path is now working on board
  - the remaining Step-A validation gap is narrower:
    - this automated run still did not capture
      `xiaozhi no_ref reopen rearmed after silence: ...`
    - follow-up validation can now focus on the silence-rearm leg rather than
      on downlink transport

## Step 5.152
- Ran the current XiaoZhi Step-A board validation against the live execution
  plan:
  - [doc/XIAOZHI_SESSION_STABILITY_EXECUTION_PLAN_ZH.md](/root/ameba-river/doc/XIAOZHI_SESSION_STABILITY_EXECUTION_PLAN_ZH.md)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)
  - [.codex/verification.md](/root/ameba-river/.codex/verification.md)
- Validation commands executed:
  - latest-SDK build:
    - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'`
  - flash:
    - `bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"`
    - `bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 /root/ameba-river/tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'`
  - live monitor checks:
    - `river xiaozhi status`
    - `river xiaozhi bootstrap`
    - `river xiaozhi connect`
    - `river xiaozhi listen detect 你好`
    - `river xiaozhi listen detect 今天天气怎么样`
- Observed results:
  - build completed successfully with `Build done`
  - flash completed successfully with `Finished PASS`
  - the board monitor reached the project CLI prompt `#`
  - `river xiaozhi status` confirmed the new Step `5.148` status surface is
    present on device:
    - `xiaozhi no_ref reopen rearm=no silence=0/6 guard_left_ms=0 open_hold_frames=2`
  - `bootstrap` and `connect` succeeded:
    - `xiaozhi ota bootstrap ok`
    - `server hello: sid=...`
  - `listen detect` succeeded for text/event flow:
    - `stt`
    - `llm`
    - `tts state=start/stop`
- Current blocker:
  - both automated `listen detect` runs kept:
    - `audio_rx=0`
    - no `playback start`
    - no `playback stop`
  - because of that, this step could not yet prove the target `no_ref`
    follow-up guard logs:
    - `xiaozhi no_ref reopen guard armed`
    - `xiaozhi no_ref reopen rearmed after silence`
- Conclusion:
  - Step A is partially validated
  - software path, flash path, status path, bootstrap path, and text-event
    session path are healthy
  - the remaining unverified part is specifically the real playback-stop /
    follow-up behavior under actual voice or audio-downlink interaction

## Step 5.151
- Pinned the first live multi-step execution plan into the Codex harness:
  - [.codex/active_plans.md](/root/ameba-river/.codex/active_plans.md)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)
  - [doc/XIAOZHI_SESSION_STABILITY_EXECUTION_PLAN_ZH.md](/root/ameba-river/doc/XIAOZHI_SESSION_STABILITY_EXECUTION_PLAN_ZH.md)
  - [doc/README.md](/root/ameba-river/doc/README.md)
  - [tools/diag/check_codex_harness.py](/root/ameba-river/tools/diag/check_codex_harness.py)
- The new primary active plan is now:
  - `doc/XIAOZHI_SESSION_STABILITY_EXECUTION_PLAN_ZH.md`
- This plan turns the current XiaoZhi work into a bounded execution surface:
  - validate the `no_ref` reopen guard from step `5.148`
  - if needed, tighten local follow-up policy separately
  - only after follow-up is stable, isolate any remaining uplink quality work
- The active-context file now links directly to the primary active plan so
  Codex no longer has to guess between:
  - old `XIAOZHI_INTEGRATION_IMPLEMENTATION_PLAN`
  - old root `plan.md`
  - recent `.codex` step history
- Extended the harness checker so the new plan workflow is not just advisory:
  - it now parses `.codex/active_plans.md`
  - verifies the primary active plan exists on disk
  - verifies `.codex/active_context.md` points to that same plan
- Reason for the change:
  - after step `5.150`, the repository had a plan workflow but still no pinned
    live plan
  - without one, Codex still had to infer the active long-running task from
    historical XiaoZhi docs and recent step logs
- Ran the updated harness check after pinning the live plan:
  - `python3 tools/diag/check_codex_harness.py`
  - result: `all checks passed`

## Step 5.150
- Added a canonical active-plan workflow for multi-step Codex work:
  - [.codex/active_plans.md](/root/ameba-river/.codex/active_plans.md)
  - [doc/EXECUTION_PLAN_TEMPLATE_ZH.md](/root/ameba-river/doc/EXECUTION_PLAN_TEMPLATE_ZH.md)
  - [AGENTS.md](/root/ameba-river/AGENTS.md)
  - [README.md](/root/ameba-river/README.md)
  - [doc/README.md](/root/ameba-river/doc/README.md)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)
  - [tools/diag/check_codex_harness.py](/root/ameba-river/tools/diag/check_codex_harness.py)
- New Codex-facing plan surfaces:
  - `.codex/active_plans.md` is now the single low-entropy index for active
    multi-step execution plans
  - `doc/EXECUTION_PLAN_TEMPLATE_ZH.md` provides the required plan structure:
    - current background
    - goals / non-goals
    - guardrails
    - facts / risks
    - step slices
    - exact verification commands
- Tightened the repository harness rules so larger work now has an explicit
  workflow:
  - create the plan under `doc/`
  - register it in `.codex/active_plans.md`
  - point `.codex/active_context.md` at it when it becomes the main active
    objective
- Extended the harness checker to fail fast if the new plan workflow drifts:
  - `AGENTS.md` must point to `.codex/active_plans.md`
  - `README.md` must point to `.codex/active_plans.md`
  - `.codex/active_context.md` must point to `.codex/active_plans.md`
  - `.codex/active_plans.md` must point to `doc/EXECUTION_PLAN_TEMPLATE_ZH.md`
- Reason for the change:
  - after step `5.149`, the repository had a stable active context but still no
    canonical place to answer:
    - which long-running plan is active right now
    - where a new multi-step plan should live
    - what shape that plan should take
  - that gap would push Codex back toward scanning old plan-like documents
    under `doc/` and guessing which one matters
- Ran the updated harness check after wiring in the active-plan workflow:
  - `python3 tools/diag/check_codex_harness.py`
  - result: `all checks passed`

## Step 5.149
- Reduced Codex-facing context drift across the repository entry points:
  - [AGENTS.md](/root/ameba-river/AGENTS.md)
  - [README.md](/root/ameba-river/README.md)
  - [build.md](/root/ameba-river/build.md)
  - [plan.md](/root/ameba-river/plan.md)
  - [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)
  - [tools/diag/check_codex_harness.py](/root/ameba-river/tools/diag/check_codex_harness.py)
- Added a canonical volatile-context file for Codex work:
  - `.codex/active_context.md` now records the current branch, default SDK
    baseline, active build/flash/monitor commands, and the latest landed step
- Tightened the root entry files to stay low-entropy:
  - `README.md` is now branch-agnostic and points readers to
    `.codex/active_context.md`
  - `build.md` now matches the repository default SDK policy:
    - `/root/ameba-rtos`
    - `AMEBA_SDK_ROOT`
  - root `plan.md` is explicitly marked as a historical `refactor`-branch
    snapshot instead of looking like the active plan for today's branch
- Added a mechanical harness check:
  - `python3 tools/diag/check_codex_harness.py`
  - it fails fast when the Codex-facing entry points drift on:
    - active-context pointer
    - default SDK baseline
    - current git branch
    - root-plan historical marker
- Reason for the change:
  - the repository had multiple contradictory "current" contexts at once
  - concrete drift existed across:
    - current branch labels
    - default SDK path
    - active-vs-historical root docs
  - that raises prompt entropy for Codex and makes the first read of the repo
    less reliable than it needs to be
- Ran the new harness check after the cleanup:
  - `python3 tools/diag/check_codex_harness.py`
  - result: `all checks passed`

## Step 5.148
- Tightened XiaoZhi follow-up reopen behavior for the current `no_ref` playback
  profile:
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- Added a `no_ref` reopen rearm state in the cloud bridge:
  - playback stop now arms a short post-playback guard window
  - follow-up reopen is blocked until the guard expires and the VAD path has
    observed a minimum silence run after playback
  - this directly targets the false empty ASR round seen immediately after
    playback stop, where the board reopened on residual tail speech with:
    - `partial=0`
    - `final=0`
    - `busy=0`
- Raised the local reopen speech hold threshold for `no_ref` follow-up:
  - default open gate remains `2` frames
  - `no_ref` follow-up now requires `6` consecutive speech frames before a new
    ASR round opens
- Added debug visibility for the new state:
  - `xiaozhi no_ref reopen guard armed: ...`
  - `xiaozhi no_ref reopen rearmed after silence: ...`
  - runtime dump now prints:
    - `xiaozhi no_ref reopen rearm=... silence=... guard_left_ms=... open_hold_frames=...`
- Reason for the change:
  - the new single-owner I/O logs showed control-plane serialization was fixed
  - remaining bad behavior had moved to `no_ref` follow-up policy:
    - playback stopped
    - a new ASR round reopened tens of milliseconds later
    - that round often carried no `partial/final` result and looked like
      playback tail / residual near-end speech, not a fresh user utterance
- Rebuilt the full latest-SDK image against `/root/ameba-rtos` after the
  `no_ref` follow-up guard change:
  - `export AMEBA_SDK_ROOT=/root/ameba-rtos`
  - `source /root/ameba-river/env.sh`
  - `python /root/ameba-rtos/ameba.py build -p`
  - result: `Build done`

## Step 5.147
- Reworked the project-side XiaoZhi realtime transport ownership into a
  single-owner I/O thread:
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
- The project-side thread model now becomes:
  - `river_xz_io` owns websocket polling, wake/listen/abort/close control sends,
    and realtime uplink audio sends
  - `river_xz_down` remains responsible for downlink decode / playback feeding
  - non-owner threads no longer call transport control APIs directly; they post
    synchronous control requests into a dedicated control queue and wait for the
    I/O owner to execute them
- Added an explicit control-plane queue beside the existing audio queue:
  - `OPEN_AND_LISTEN`
  - `LISTEN_STOP`
  - `ABORT`
  - `CLOSE_SESSION`
  - owner-thread fast-path is preserved so the I/O owner executes its own
    requests directly instead of deadlocking on self-queued work
- Added per-ASR-round closed-loop stats so each round now logs:
  - round id / sid
  - pre-roll frame count
  - first-packet delay
  - packet count
  - busy / fail / stale-drop / ring-drop deltas
  - partial / final counts and seen flags
  - close reason
- Updated the runtime status dump to expose the new ownership split:
  - `xiaozhi runtime ... io=running`
  - `xiaozhi control queue=...`
  - `xiaozhi uplink queue=... owner=running`
  - `xiaozhi asr round ...`
- Rebuilt the full latest-SDK image against `/root/ameba-rtos` after the
  single-owner I/O + round-stats change:
  - `export AMEBA_SDK_ROOT=/root/ameba-rtos`
  - `source /root/ameba-river/env.sh`
  - `python /root/ameba-rtos/ameba.py build -p`
  - result: `Build done`

## Step 5.146
- Fixed a websocket transport fairness problem in the XiaoZhi pump path:
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- Added a small active-loop fairness delay to `river_xz_pump`:
  - introduced `RIVER_CLOUD_XIAOZHI_PUMP_FAIRNESS_DELAY_MS` with a `1 ms`
    project-local default
  - after each active `river_xiaozhi_poll()` slice, the pump now sleeps briefly
    before re-entering the transport mutex
- Reason for the change:
  - the latest board logs no longer showed uplink-ring overflow at the wake
    boundary, but wake admission still stopped after
    `xiaozhi wake admission transport ready`
  - code inspection showed `river_xz_pump` running at priority `4` while the
    wake admission worker `river_wake_evt` runs at priority `3`
  - once `server hello` marks the session open, the higher-priority pump could
    loop continuously around `river_xiaozhi_poll(5 ms)` and reacquire the same
    transport mutex fast enough to starve the lower-priority
    `river_xiaozhi_send_listen_start("auto")` control send
  - this explains why the project can stall locally even though the server
    already accepted the websocket and sent `server hello`
- Expected effect of this step:
  - wake admission should progress past `transport ready`
  - the board should again print:
    - `xiaozhi wake admission listen_start sent`
    - `xiaozhi wake admission ready`
    - `wakeword admission accepted`
  - this step targets local control-plane starvation; it does not change the
    KWS model threshold or server protocol
- Rebuilt the full latest-SDK image against `/root/ameba-rtos` after the pump
  fairness change:
  - `export AMEBA_SDK_ROOT=/root/ameba-rtos`
  - `source /root/ameba-river/env.sh`
  - `python /root/ameba-rtos/ameba.py soc RTL8730E`
  - `python /root/ameba-rtos/ameba.py build -p`
  - result: `Build done`

## Step 5.145
- Tightened the xiaozhi realtime uplink path to behave more like the reference
  `xiaozhi-esp32` client instead of buffering a long local backlog:
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
- The step changes three related pieces of backlog control:
  - reduced the xiaozhi-specific ASR pre-roll cap from `256 ms` to `128 ms` so
    `asr_stream_active` does not begin by bursting such a large historical
    backlog into the websocket uplink path
  - changed uplink BUSY backoff growth from frame-based (`40/80/160 ms` for a
    `20 ms` Opus frame) to poll-based (`5/10/20/40 ms`), which matches the
    current websocket pump cadence and avoids leaving the local PCM ring behind
    real time after the first transient stall
  - trimmed stale uplink PCM on the enqueue side before write, so the local ring
    stays close to the realtime tail instead of waiting to hit `64/64` and then
    logging repeated overflow drops
- Reason for the change:
  - the latest board logs still showed first wake admission succeeding, but the
    session quickly entered repeated
    `xiaozhi uplink ring overflow: ... queued=64 capacity=64`
  - compared with `/root/xiaozhi-esp32`, our Ameba path had accumulated more
    buffering layers and a much more conservative BUSY retry schedule
  - `xiaozhi-esp32` effectively drains on events and keeps queues short, so the
    right direction here is smaller burst, shorter retry delay, and earlier
    stale-frame trimming rather than allowing a long local backlog to build
- Rebuilt the full latest-SDK image against `/root/ameba-rtos` after the
  backlog-control change:
  - `export AMEBA_SDK_ROOT=/root/ameba-rtos`
  - `source /root/ameba-river/env.sh`
  - `python /root/ameba-rtos/ameba.py soc RTL8730E`
  - `python /root/ameba-rtos/ameba.py build -p`
  - result: `Build done`

## Step 5.144
- Changed the xiaozhi uplink worker pacing in
  [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c):
  - successful Opus uplink sends now clear `xiaozhi_uplink_next_send_ms`
    instead of advancing it by another `frame_duration_ms`
  - explicit backoff remains only on `RIVER_ERR_BUSY` and generic send-failure
    paths
- Reason for the change:
  - the worker previously kept a future send deadline even after a successful
    send
  - once any transient stall created backlog, the worker stayed permanently
    behind real time and the local `xiaozhi_uplink_ring` eventually filled to
    `64/64`, producing the repeated `xiaozhi uplink ring overflow` storm seen
    on the board
  - clearing the deadline on success lets the worker immediately drain queued
    PCM until it catches up, while websocket-side BUSY handling still provides
    transport backoff
- Rebuilt the full latest-SDK image against `/root/ameba-rtos` after the
  pacing fix:
  - `export AMEBA_SDK_ROOT=/root/ameba-rtos`
  - `source /root/ameba-river/env.sh`
  - `python /root/ameba-rtos/ameba.py soc RTL8730E`
  - `python /root/ameba-rtos/ameba.py build -p`
  - result: `Build done`

## Step 5.143
- Added narrow wake-admission tracing around the new post-backpressure issue:
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_core/river_session_coordinator.c](/root/ameba-river/components/river_core/river_session_coordinator.c)
- The new logs pin the wakeword handoff path at these exact points:
  - `xiaozhi wake admission begin`
  - `xiaozhi wake admission transport ready`
  - `xiaozhi wake admission listen_start failed`
  - `xiaozhi wake admission listen_start sent`
  - `xiaozhi wake admission ready`
  - `wakeword admission accepted`
- This was added because the latest board log no longer showed websocket
  backpressure, but it also did not reach the expected:
  - `xiaozhi conversation window opened`
  - `interaction_state: wake_monitoring -> wake_confirmed`
  - `asr provider=xiaozhi_realtime session started`
  after `server hello`, so the current blocker has moved from transport queue
  saturation to the wake-admission / listen-start handoff.
- Rebuilt the full latest-SDK image against `/root/ameba-rtos` after the
  tracing change:
  - `export AMEBA_SDK_ROOT=/root/ameba-rtos`
  - `source /root/ameba-river/env.sh`
  - `python /root/ameba-rtos/ameba.py soc RTL8730E`
  - `python /root/ameba-rtos/ameba.py build -p`
  - result: `Build done`

## Step 5.142
- Reduced the websocket-side source of `xiaozhi uplink backpressure` across:
  [components/river_cloud/river_xiaozhi_ws.c](/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c),
  [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c),
  [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h),
  and
  [include/river/river_xiaozhi_credentials.h](/root/ameba-river/include/river/river_xiaozhi_credentials.h):
  - increased `RIVER_XIAOZHI_WS_QUEUE_MAX` from `8` to `16` so short WLAN /
    TLS send stalls do not hit the project BUSY guard as quickly
  - reduced the active xiaozhi pump cadence from `20 ms` to `5 ms`
  - removed the extra fixed `5 ms` active-loop sleep once the pump is already
    driving an open websocket session
  - changed `river_xiaozhi_poll()` to split long polls into `5 ms`
    transport-lock slices instead of holding the websocket transport lock
    around a single `20 ms` `ws_poll()` call
  - reused the sliced `river_xiaozhi_poll()` path during session-open hello
    waiting so the same lock-hold bound applies there too
- Rebuilt the full latest-SDK image against `/root/ameba-rtos` after the
  change:
  - `export AMEBA_SDK_ROOT=/root/ameba-rtos`
  - `source /root/ameba-river/env.sh`
  - `python /root/ameba-rtos/ameba.py soc RTL8730E`
  - `python /root/ameba-rtos/ameba.py build -p`
  - result: `Build done`
- Flashed the rebuilt image to the board:
  - `python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor`
  - result: `Finished PASS`
- Ran a short live serial smoke after flashing:
  - saw a normal wake handoff chain with a single post-hit `kws disarm`
  - cloud path still reached:
    - `xiaozhi connecting`
    - `Connected to websocket server`
    - `server hello`
  - deeper same-utterance before/after backpressure comparison is still
    pending a longer runtime capture under the previously failing scenario

## Step 5.141
- Fixed the repeated post-wake `kws disarm` loop in
  [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - added `river_voice_kws_needs_disarm_for_detection_block()`
  - when wake detection is blocked because interaction has already left
    `wake_monitoring` or the cloud conversation window is active,
    `river_voice_kws_submit_frame()` now only calls
    `river_voice_kws_disarm(..., true)` if the KWS path is not already
    quiesced
- This keeps the intended one-time cleanup semantics after wake handoff, while
  preventing the per-frame `kws disarm: begin ... gate=closed` spam that
  showed up immediately after a successful wake.
- Rebuilt the full latest-SDK image against `/root/ameba-rtos` after the fix:
  - `export AMEBA_SDK_ROOT=/root/ameba-rtos`
  - `source /root/ameba-river/env.sh`
  - `python /root/ameba-rtos/ameba.py soc RTL8730E`
  - `python /root/ameba-rtos/ameba.py build -p`
  - result: `Build done`

## Step 5.140
- Switched the tracked board-profile config in [prj.conf](/root/ameba-river/prj.conf)
  to the new nano FP32 path:
  - `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_CONV_RESNET_ED_NANO_V1_FP32_DEBUG=y`
  - `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=9008`
  - retained the validated `40x101` parity settings:
    - `CONFIG_RIVER_KWS_TENSOR_ARENA_KB=2048`
    - `CONFIG_RIVER_KWS_INFERENCE_STRIDE_FRAMES=16`
    - `CONFIG_RIVER_KWS_INPUT_QUEUE_FRAMES=64`
    - `CONFIG_RIVER_KWS_PRE_ROLL_FLUSH_MAX_FRAMES=16`
- Rebuilt the full latest-SDK image through the official external-project path:
  - `source env.sh`
  - `python /root/ameba-rtos/ameba.py soc RTL8730E`
  - `python /root/ameba-rtos/ameba.py build -p`
- Verified the generated build config and compiled library now really point to
  the nano FP32 variant:
  - `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_CONV_RESNET_ED_NANO_V1_FP32_DEBUG=y`
  - `platform_autoconf.h` shows `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15 9008`
  - `lib_river_voice.a` contains:
    - `student_conv_resnet_ed_nano_v1_fp32_debug`
- Flashed the new image to real hardware and confirmed:
  - `tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor` finished with
    `PASS`
  - current packaged image sizes are:
    - `ap_image_all.bin`: `3275872 B`
    - `km4_image2_all.bin`: `380448 B`
    - `km0_image2_all.bin`: `94208 B`
    - `km0_km4_ca32_app.bin`: `3758720 B`
    - `km4_boot_all.bin`: `51872 B`
- Completed the same-caliber board parity run for
  `student_conv_resnet_ed_nano_v1_fp32_debug`:
  - boot contract confirms:
    - `runtime_in=float32 runtime_out=float32`
    - `dims=[1,40,101,1]`
    - `variant=student_conv_resnet_ed_nano_v1_fp32_debug`
    - `arena_used=436528 B`
  - align replay result:
    - `raw=306`
    - `score=0.305877`
    - `q15=10023`
    - `infer_us=29826`
    - `wakeword hit` above the current `threshold_q15=9008`
  - tensor dump transcript was complete on the first pass:
    - `feat_f32` chunk count `253/253`
- Closed board/host correctness for this nano FP32 image:
  - host replay against
    `/root/kws-trainint/artifacts/exports/student_conv_resnet_ed_nano_v1/model.fp32.tflite`
    reports:
    - `board_hash: feature=0x7ce0b11d input=0xd52f011c`
    - `host_hash: feature=0x7ce0b11d effective_input=0xd52f011c`
    - `board_output: raw=306 exact=0.305877 q15=10023`
    - `host_output: raw=306 exact=0.305877`
    - `output_parity: bytes_equal=yes raw_equal=yes`
- Recorded the full build / flash / boot / parity / performance report in:
  - `doc/RUNTIME_RESOURCE_PROFILE_2026-04-10_STUDENT_CONV_RESNET_ED_NANO_FP32_DEBUG_ZH.md`
- Current board-side conclusion:
  - `infer_us[last=29826 avg=29826 max=29826]`
  - `arena_used=436528 B`
  - vs bundle budget:
    - CPU: `29.826 ms` vs `18.0 ms` (`1.66x` over)
    - memory: `436528 B` vs `384 KB` (`1.11x` over)
  - vs `student_conv_resnet_ed_tiny_v1_fp32_debug`:
    - latency about `3.23x` faster
    - arena about `33.4%` lower

## Step 5.139
- Added a parallel `student_conv_resnet_ed_nano_v1_fp32_debug` wakeword
  bring-up path without touching the current mainline chain:
  - added
    `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_CONV_RESNET_ED_NANO_V1_FP32_DEBUG`
    to `Kconfig`
  - wired the new variant into
    [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc)
    using the same `40x101`, `n_fft=400`, centered log-mel FP32 frontend
    contract already validated on the recent `conv_resnet_ed` tiny path
- Imported the matching algorithm-side FP32 debug model into the repo as:
  - `components/river_voice/generated/student_conv_resnet_ed_nano_v1_fp32_model_data.h`
- Switched the active board-profile build config in
  `build_RTL8730E/menuconfig/prj.conf` to:
  - `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_CONV_RESNET_ED_NANO_V1_FP32_DEBUG=y`
  - `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=9008`
  - retained `CONFIG_RIVER_KWS_TENSOR_ARENA_KB=2048` and the existing
    stride / queue / pre-roll settings so the next board pass stays directly
    comparable with the recent `40x101` student measurements
- Confirmed the imported FP32 header matches the algorithm export exactly:
  - header bytes: `141700`
  - model bytes: `141700`
  - SHA256:
    `5c955b390db469ddd5d82c22c2b00022eb8a82f596e4e5dd2194d3c7b07c7727`
  - exact match: `yes`

## Step 5.138
- Validated the new `student_conv_resnet_ed_tiny_v1_fp32_debug` bring-up on
  real hardware under the latest SDK and wrote the resulting deployment /
  parity / performance report into:
  - `doc/RUNTIME_RESOURCE_PROFILE_2026-04-10_STUDENT_CONV_RESNET_ED_TINY_FP32_DEBUG_ZH.md`
- Confirmed the active board image and model contract from boot log:
  - `variant=student_conv_resnet_ed_tiny_v1_fp32_debug`
  - `runtime_in=float32 runtime_out=float32`
  - `input shape=[1,40,101,1]`
  - `threshold_q15=9038`
  - `arena_used=654960 B`
- Closed board/host deployment correctness for the FP32 debug variant:
  - the embedded header and
    `/root/kws-trainint/artifacts/exports/student_conv_resnet_ed_tiny_v1/model.fp32.tflite`
    are byte-identical
  - host replay reports:
    - `board_hash: feature=0x7ce0b11d input=0xd52f011c`
    - `host_hash: feature=0x7ce0b11d effective_input=0xd52f011c`
    - `board_output: raw=345 exact=0.345300 q15=11314`
    - `host_output: raw=345 exact=0.345300`
    - `output_parity: bytes_equal=yes raw_equal=yes`
- Recorded the important process nuance for future reuse:
  - the stable preserved parity sequence was:
    - `reboot`
    - `river kws debug local on`
    - `river audio probe stop`
    - `river kws debug local off`
    - `river kws align run`
    - `river kws dump meta`
    - `river kws dump chunk output_raw 1`
    - `river kws dump chunk feat_f32 1..253`
  - the first transcript had UART-corrupted / missing `feat_f32` chunks
    (`4`, `5`, `120`, `121`), but the preserved snapshot allowed targeted
    re-pull of only those chunks without re-running inference
- Recorded the current board-side FP32 performance conclusion:
  - live `infer_us[last=96460 avg=96420 max=96475]`
  - align capture `infer_us=96295`
  - bundle budget comparison:
    - CPU: `96.4 ms` vs `24.0 ms` (`4.02x` over)
    - memory: `654960 B` vs `512 KB` (`1.25x` over)
  - among the already archived student FP32 candidates in this repo, this is
    currently the best board result so far

## Step 5.137
- Added a parallel `student_conv_resnet_ed_tiny_v1` wakeword bring-up path
  without touching the current mainline chain:
  - copied the new algorithm-side exported headers into:
    - `components/river_voice/generated/student_conv_resnet_ed_tiny_v1_fp32_model_data.h`
    - `components/river_voice/generated/student_conv_resnet_ed_tiny_v1_int8_model_data.h`
  - added two new Kconfig model variants:
    - `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_CONV_RESNET_ED_TINY_V1_FP32_DEBUG`
    - `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_CONV_RESNET_ED_TINY_V1_INT8_DEBUG`
- Wired both variants into
  [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc)
  using the already-proven `40x101`, `n_fft=400`, centered log-mel frontend
  contract that the algorithm bundle requires.
- Kept the preserved board/local parity infrastructure unchanged:
  - no removal or weakening of tensor dumps, replay alignment, or debug-only
    host-vs-board comparison hooks
  - no change to the mainline baseline or existing student debug variants
- Confirmed the new model family stays within the current board kernel
  operator envelope declared by the export bundle:
  - model family: `conv_resnet_ed`
  - required ops: `ADD`, `AVERAGE_POOL_2D`, `CONV_2D`, `LOGISTIC`
  - frontend contract: `40x101`
  - exported model sizes:
    - FP32 `479008 B`
    - INT8 `135504 B`

## Step 5.136
- Compared the matching `student_dscnn_tiny_v2` FP32 and INT8 board baselines
  after the new INT8 exact-parity proof, and wrote the result into:
  - `doc/KWS_DSCNN_TINY_FP32_INT8_COMPARISON_2026-04-10_ZH.md`
- Confirmed from existing board reports that the corresponding FP32 model had
  already been deployed and measured earlier:
  - `student_dscnn_tiny_v2_fp32_debug`
  - board `infer_us[last=183988 avg=183969 max=183988]`
  - arena used `1168336 B`
- Compared that against the newly closed INT8 path:
  - `student_dscnn_tiny_v2_int8_debug`
  - board `infer_us[last=492733 avg=492784 max=493321]`
  - arena used `295764 B`
- Resulting same-family conclusion:
  - INT8 is about `2.68x` slower than the corresponding FP32 model on the
    current board path
  - INT8 reduces arena usage to about `25.3%` of FP32, i.e. about `74.7%`
    lower memory
- Re-checked the current latest-SDK source under `/root/ameba-rtos` instead of
  relying only on older notes:
  - `/root/ameba-rtos/component/tflite_micro/tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/conv.cc`
    still contains the explicit `int8 conv optimized path is not reliable`
    comment and immediately calls `reference_integer_ops::ConvPerChannel(...)`
  - `/root/ameba-rtos/component/tflite_micro/tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/depthwise_conv.cc`
    still contains the explicit `optimized int8 depthwise kernel is not
    reliable` comment and immediately calls
    `reference_integer_ops::DepthwiseConvPerChannel(...)`
- Updated the project interpretation accordingly:
  - current latest-SDK does not show evidence that CA32 INT8 compute
    optimization has actually come back into effect for this wakeword path
  - the current DSCNN tiny INT8 improvement over the old BC-ResNet INT8 path
    should be attributed to a lighter model topology, not to restored
    quantized-kernel acceleration

## Step 5.135
- Consolidated the current `student_dscnn_tiny_v2_int8_debug` parity state into
  a dedicated project note:
  - `doc/KWS_DSCNN_TINY_INT8_PARITY_STATUS_2026-04-10_ZH.md`
- Reflashed the current `HEAD` image from this branch onto the board and
  re-ran the preserved INT8 board/host parity workflow against the latest SDK
  baseline at `/root/ameba-rtos`.
- Verified on the new board log that the active image is still:
  - `variant=student_dscnn_tiny_v2_int8_debug`
  - `runtime_in=int8 runtime_out=int8`
  - `input=40x101x1`
- Used the preserved alignment path without changing the board-side command
  contract:
  - `reboot`
  - `river kws debug local on`
  - `river audio probe stop`
  - `river kws debug local off`
  - `river kws align run`
  - `river kws dump meta`
  - `river kws dump chunk output_raw 1`
  - `river kws dump chunk feat_f32 1..253`
  - `river kws dump chunk input_raw 1..64`
  - `river kws align status`
- New decisive result on `2026-04-10`:
  - the current DSCNN tiny INT8 path is now fully replayable on host
  - board log contains a complete dump:
    - `kws tensor dump begin`
    - `kws tensor dump meta`
    - `output_raw 1/1`
    - `feat_f32 1/253 ... 253/253`
    - `input_raw 1/64 ... 64/64`
  - host replay against
    `/root/kws-trainint/artifacts/exports/student_dscnn_tiny_v2/model.int8.tflite`
    reports:
    - `quant_parity: diff_bytes=0/4040`
    - `output_parity: bytes_equal=yes raw_equal=yes`
    - board/host both:
      - `raw=-52`
      - `score=0.296875`
      - `q15=9728`
- This closes the remaining correctness question for the currently active INT8
  model:
  - exact board/host parity now holds for
    `student_dscnn_tiny_v2_int8_debug`
  - the previously blocking post-align shell failure is no longer the main
    outstanding issue on this path
- Recorded the resulting board/profile summary in a new dedicated report:
  - `doc/RUNTIME_RESOURCE_PROFILE_2026-04-10_STUDENT_DSCNN_TINY_INT8_DEBUG_ZH.md`
  - current board runtime profile from the captured log is roughly:
    - `infer_us[last=492733 avg=492784 max=493321]`
    - `arena=295764/2048KB`
    - `slack=1801388`
- Updated project-level interpretation:
  - current DSCNN tiny INT8 is now an `INT8 correctness / deployability`
    baseline
  - it is still not a realtime-ready mainline candidate at the present
    `~493ms` board latency

## Step 5.134
- Validated the shell-stack-pressure hypothesis on real hardware with a minimal
  debug-only code change in the latest-SDK INT8 alignment path.
- In [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - removed the stack-local
    `uint8_t trailing_silence[RIVER_KWS_INPUT_FRAME_BYTES]`
    from `river_voice_kws_run_alignment_sample(...)`
  - replaced it with a static zero-filled buffer:
    - `g_river_kws_alignment_trailing_silence`
- Why this specific probe was chosen:
  - prior board proof already showed:
    - `river_voice_kws_run_alignment_sample(...)` returned
    - `[river][diag] kws align run returned status=0` printed
    - but the monitor path became unreliable immediately afterward
  - the same board boot also showed the monitor-side `shell_task` had only
    about `700B` stack headroom early in runtime
  - the alignment command itself was still allocating a `512B` silence frame on
    that shell task stack, making stack pressure the most direct low-risk
    hypothesis to test
- Board result on `2026-04-10` after flashing the new image:
  - image still boots as:
    - `variant=student_dscnn_tiny_v2_int8_debug`
    - `runtime_in=int8 runtime_out=int8`
  - the controlled sequence still reaches:
    - `kws align replay captured: seq=1 infer=2 score=0.296875 q15=9728`
    - `kws align replay done: dump=preserved local_only_restored=no`
    - `[river][diag] kws align run returned status=0`
  - but unlike the previous baseline, the post-align monitor remains usable:
    - `river kws dump meta` executes
    - `kws tensor dump meta: ... score=0.296875 q15=9728 ...`
    - `[river][diag] kws dump meta returned`
    - `river kws align status` executes
    - `kws align guard: kws=ready probe=stopped interaction=wake_monitoring detection=ready worker=idle snapshot=ready local_only=no`
- Negative evidence from the same run:
  - the prior post-return failure marker did not recur:
    - no `[INIC-E] WIFI TRX IPC 4 timeout` appeared in the captured align window
- Current conclusion:
  - the remaining post-align shell failure was strongly tied to shell-task
    stack pressure
  - removing the `512B` local alignment silence buffer was sufficient to
    restore the preserved board/host parity command workflow on this path

## Step 5.133
- Flashed the latest-SDK image containing the new diag return-path
  instrumentation commit `8c23a03` onto real hardware and revalidated the
  INT8 alignment path on board.
- Verified on boot from the captured board log:
  - `runtime_in=int8 runtime_out=int8`
  - `variant=student_dscnn_tiny_v2_int8_debug`
- Reproduced the controlled latest-SDK command sequence on board:
  - `reboot`
  - `river kws debug local on`
  - `river audio probe stop`
  - `river kws debug local off`
  - `river kws align run`
  - `river kws dump meta`
  - `river kws align status`
- New decisive board finding on `2026-04-09`:
  - the KWS replay path completes and the diag command handler also returns:
    - `kws align replay done: dump=preserved local_only_restored=no`
    - `[river][diag] kws align run returned status=0`
  - this proves the remaining blocker is no longer inside
    `river_voice_kws_run_alignment_sample(...)`
  - the command has already returned to the diag / monitor layer before the
    post-align failure appears
- Remaining failure reproduced immediately after the returned marker:
  - runtime prints:
    - `[INIC-E] WIFI TRX IPC 4 timeout`
  - subsequent commands are only echoed and do not execute:
    - `river kws dump meta`
    - `river kws align status`
- Additional probe in the same post-align state:
  - a plain `reboot` command emitted no boot log within the probe window
  - this further supports that the board monitor / command execution path is
    no longer functioning normally after the successful align command returns
- Current narrowed conclusion:
  - the remaining issue is a post-return monitor/parser/system-state problem,
    not a missing final replay log and not the core INT8 replay function

## Step 5.132
- Added one-step monitor return-path instrumentation for the remaining
  latest-SDK INT8 alignment-shell hang investigation without changing the KWS
  parity flow itself.
- In [components/river_diag/river_diag_cmd.c](/root/ameba-river/components/river_diag/river_diag_cmd.c):
  - `river kws dump meta` now prints:
    - `[river][diag] kws dump meta returned`
    after `river_voice_kws_dump_tensor_meta()` returns
  - successful `river kws align run` now prints:
    - `[river][diag] kws align run returned status=0`
    after `river_voice_kws_run_alignment_sample(true)` returns
- Purpose of this step:
  - distinguish whether the remaining post-align freeze is still inside
    `river_voice_kws_run_alignment_sample(...)`
  - or whether the command handler returns and the shell wedges later in the
    monitor path
- Scope guard:
  - no KWS runtime behavior, threshold, tensor dump contents, or board/host
    parity commands were changed
  - this is strictly additional diag output on the existing monitor commands
- Rebuilt the project against the latest SDK at `/root/ameba-rtos`; the build
  completed successfully with `Build done`

## Step 5.131
- Narrowed the remaining latest-SDK INT8 alignment blocker one step further on real hardware without changing code yet.
- Revalidated the same `student_dscnn_tiny_v2_int8_debug` image with a controlled monitor sequence:
  - early boot: `river kws debug local on`
  - after Wi-Fi settled and before replay: `river audio probe stop`
  - immediately before replay: `river kws debug local off`
  - then: `river kws align run`
- New board finding on `2026-04-09`:
  - when `align run` starts with `previous_local_debug_mode=no`, the command now reaches the final success marker:
    - `kws align replay done: dump=preserved local_only_restored=no`
  - this proves the previous missing-final-line symptom is no longer caused by the skipped final `disarm`; it is specifically correlated with the cleanup path where `align run` restores `local_debug_mode=yes`
- The same run still shows an unresolved post-success problem:
  - immediately after `kws align replay done: ...`, runtime prints:
    - `[INIC-E] WIFI TRX IPC 4 timeout`
  - a follow-up `river kws dump meta` is only echoed by UART and still does not execute
  - this means the shell / monitor path is still not reliably usable after a successful INT8 align run, even once `replay done` is printed
- Additional observations from the same controlled boot:
  - the live INT8 model still false-triggers before replay work unless `local debug` is enabled very early
  - in this run the early live false wake was:
    - `score_pm=289 q15=9472`
    - still above the configured threshold (`278`)
  - after `river kws debug local off`, `align run` internally re-enables local debug for the replay itself, so the compiled-sample wake is still held locally and does not wake the cloud path

## Step 5.130
- Validated the latest-SDK `student_dscnn_tiny_v2_int8_debug` alignment cleanup change on real hardware after flashing the rebuilt image from `/root/ameba-rtos`.
- In [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - alignment cleanup now skips the final `river_voice_kws_disarm(context, true)` only when the replay already succeeded, the KWS worker is idle, and the gate is fully cleared
  - the normal runtime KWS path is unchanged; this only affects the debug-only `river kws align run` success cleanup path
- Board validation on `2026-04-09` proved the previous hang point moved:
  - `river kws debug local on` at boot successfully blocked cloud handoff during false wakes
  - `river audio probe stop` prevented live probe traffic from racing the compiled-sample replay
  - `river kws align run` now reaches all of these later markers on board:
    - `kws align replay captured: seq=1 infer=2 score=0.296875 q15=9728`
    - `kws align cleanup: status=0 emit_dump=yes local_only_restore=yes`
    - `kws align cleanup: disarm skipped worker already idle snapshot=ready`
    - `kws align cleanup: worker idle wait status=0`
    - `kws align cleanup: disarm tensor dump begin`
    - `kws align cleanup: disarm tensor dump done`
    - `kws align cleanup: local debug restored=yes`
- Newly confirmed remaining blockers:
  - the final `kws align replay done: ...` line is still missing
  - a manual follow-up `river kws dump meta` still does not execute after that run
  - the board then falls into repeated `[INIC-A] Dev api ipc timeout: cur id 0x1 ...` logs, so the shell is still not fully returning
- Additional board finding from the same run:
  - on a clean boot without early `local debug`, this INT8 model false-triggers very early on live audio:
    - `infer=1 score_pm=304`
    - `wakeword hit: ... score_pm=304 q15=9984`
  - with the configured threshold at `278`, the current debug image is not stable enough to leave live probe traffic running during alignment work

## Step 5.129
- Trimmed the `river kws align run` UART output one step further for slow/fragile INT8 bring-up while preserving the full manual parity path.
- In [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - removed the automatic `begin/meta/snapshot` summary emission from `river kws align run`
  - kept the captured snapshot itself intact, so the existing manual commands remain the parity path:
    - `river kws dump meta`
    - `river kws dump chunk feat_f32 ...`
    - `river kws dump chunk input_raw ...`
    - `river kws dump chunk output_raw ...`
- Rationale:
  - board validation in Step `5.128` proved the command now reaches `kws align replay captured: ...`
  - but the shell still blocked immediately after the auto-emitted snapshot summary
  - reducing `align run` to preserve snapshot without auto-printing summary is the smallest behavior change that keeps the existing board/local parity mechanism intact
- Removed the now-unused helper `river_voice_kws_log_tensor_dump_snapshot_summary(...)`.
- Verified this step with a full latest-SDK rebuild against `/root/ameba-rtos`; the build completed with `Build done`.

## Step 5.128
- Re-flashed the `ace9300` latest-SDK image after the user power-cycled the board and successfully restored the PL2303 UART node inside WSL.
- A host-side serial-node issue had to be corrected first:
  - because `/dev/ttyUSB0` was missing, earlier redirections had created a regular file at that path
  - reattached USB device `4-4` with `usbipd.exe attach --wsl --busid 4-4`
  - confirmed the kernel still exposed `ttyUSB0` in `/sys/class/tty/ttyUSB0`
  - removed the bogus `/dev/ttyUSB0` file and recreated the proper character device node from kernel major/minor `188:0`
- Flash then succeeded normally with `Finished PASS`.
- Re-ran the latest-SDK DS-CNN tiny INT8 parity command path:
  - `river kws debug local on`
  - `river audio probe stop`
  - `river kws align run`
- New observed narrowing on `2026-04-09`:
  - trigger-side path fully returns now:
    - `kws trigger dispatch done: ...`
    - `kws trigger post-disarm: gate=latched ...`
  - replay loop also completes its feed and tail phases:
    - `kws align replay feed done: trigger=no infer=2 snapshot=ready`
    - `kws align replay tail done: gate=cleared infer=2 snapshot=ready`
    - `kws align replay wait idle status=0`
  - snapshot summary now fully prints:
    - `kws align replay captured: seq=1 infer=2 score=0.296875 q15=9728`
    - `kws tensor dump begin: ...`
    - `kws tensor dump meta: ...`
    - `kws tensor dump snapshot: seq=1 infer=2 chunks=[feat:253 input:64 output:1]`
  - immediately after that, runtime prints:
    - `[INIC-E] WIFI TRX IPC 4 timeout`
- The command still never reaches:
  - `kws align cleanup: ...`
  - `kws align replay done: ...`
- A follow-up `river kws dump meta` still only echoes at UART and does not execute, confirming the shell remains blocked inside the original `river kws align run`.
- Current narrowed conclusion:
  - the remaining hang is now specifically between the last line of `river_voice_kws_log_tensor_dump_snapshot_summary(...)` and the first outer cleanup log
  - the next diagnostic step should instrument the end of snapshot-summary emission and the first statement after it

## Step 5.127
- Added a second-stage INT8 alignment diagnostic slice to narrow the remaining hang that still occurs after trigger-side `disarm`.
- In [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - added `kws trigger dispatch done: ...` immediately after wakeword event dispatch returns
  - added `kws trigger post-disarm: ...` immediately after `river_voice_kws_disarm_after_trigger(...)` returns
  - added alignment replay markers for the first post-trigger phase:
    - `kws align replay trigger observed: frame=... infer=... snapshot=...`
    - `kws align replay first post-trigger queue wait begin: ...`
    - `kws align replay first post-trigger queue wait done: ...`
  - added end-of-replay phase markers:
    - `kws align replay feed done: ...`
    - `kws align replay tail done: ...`
    - `kws align replay wait idle begin/status=...`
- Verified this step with a full latest-SDK rebuild against `/root/ameba-rtos`; the build completed with `Build done`.

## Step 5.126
- Re-flashed the latest-SDK `student_dscnn_tiny_v2_int8_debug` image that includes the cleanup-stage diagnostics and re-ran the preserved board/local parity command path on `2026-04-09`.
- Flashing required the known serial recovery sequence before download mode:
  - send `ESC+CRLF`
  - send `reboot uartburn`
  - flash with `tools/river_flash.py`
  - result: `Finished PASS`
- After flashing, the board did not immediately resume normal runtime logs on its own; sending a plain `reboot` restored the application runtime, after which UART logs resumed normally.
- Reproduced the alignment path with:
  - `river kws debug local on`
  - `river audio probe stop`
  - `river kws align run`
- New observed behavior:
  - the initial alignment disarm path completed fully before replay start
  - replay progressed into real INT8 inference:
    - `infer=1 score=0.242188 q15=7936`
    - `infer=2 score=0.296875 q15=9728`
  - tensor snapshot capture is working during replay:
    - `kws tensor dump captured: seq=1 infer=2 ...`
  - local-debug wake suppression is working:
    - `wakeword handoff held: reason=local_debug ...`
  - after the trigger, a second `kws disarm` also completed all currently instrumented substeps:
    - `frontend reset done`
    - `input ring reset done`
    - `pre-roll ring reset done`
    - `input signal drained`
- But the command still never reached the later alignment-stage markers:
  - no `kws align replay captured: ...`
  - no `kws align cleanup: ...`
  - no `kws align replay done: ...`
- A follow-up `river kws dump meta` was only echoed by UART and produced no command execution output, which shows the CLI remained blocked inside the original `river kws align run`.
- Current narrowed conclusion:
  - the remaining hang is no longer in the outer cleanup block entry
  - it happens earlier, after trigger-side `river_voice_kws_disarm_after_trigger()` finishes, but before `river_voice_kws_run_alignment_sample()` reaches the post-feed snapshot/cleanup section
  - likely next suspects are the replay loop's post-trigger frame submission / queue-room checks or another no-log ring/mutex call immediately after the second disarm

## Step 5.125
- Added alignment-cleanup diagnostics for the latest-SDK `student_dscnn_tiny_v2_int8_debug` replay path so the remaining post-snapshot stall can be localized without changing the normal wakeword runtime flow.
- In [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - added timeout diagnostics in `river_voice_kws_wait_for_worker_idle(...)` to print:
    - `reset_pending`
    - `worker_processing`
    - pending input-ring frame count
    - timeout budget
  - added step-by-step logs inside `river_voice_kws_disarm(...)` to show whether cleanup reaches:
    - frontend reset
    - input-ring reset
    - pre-roll-ring reset
    - input-signal drain
  - added cleanup-stage logs in `river_voice_kws_run_alignment_sample(...)` to show whether alignment teardown reaches:
    - `disarm done`
    - `worker idle wait status=...`
    - tensor-dump disarm begin/done
    - local-debug restore
- Verified this step with a full latest-SDK rebuild against `/root/ameba-rtos`; the build completed with `Build done`.

## Step 5.124
- Re-tried the latest-SDK `student_dscnn_tiny_v2_int8_debug` board validation after the user power-cycled the board and reattached the PL2303 USB serial device.
- New observed state on `2026-04-09`:
  - the board no longer stayed in the earlier pure-`0x00` boot-failure state
  - passive UART capture immediately showed normal runtime KWS/VAD logs again
  - the active image characteristics still matched the DS-CNN tiny INT8 debug variant:
    - `out_type=int8`
    - `threshold_pm=278`
    - `arena=295764/2048KB`
    - `infer_us` around `491-493 ms`
- Ran the preserved-snapshot alignment workflow again:
  - `river kws debug local on`
  - `river audio probe stop`
  - `river kws align status`
  - `river kws align run`
- The board now progressed through the intended summary-based parity path:
  - `kws align replay captured: seq=1 infer=12 score=0.296875 q15=9728`
  - `kws tensor dump begin: ... in_type=int8 out_type=int8 ...`
  - `kws tensor dump meta: ... in_scale=0.029209241 in_zp=-9 out_scale=0.003906250 out_zp=-128`
  - `kws tensor dump snapshot: seq=1 infer=12 chunks=[feat:253 input:64 output:1]`
- But the run still did not fully return to an interactive shell:
  - `kws align replay done: dump=preserved ...` did not appear
  - after the snapshot line, the board started repeating `IPC Get Semaphore Timeout`
  - a follow-up `river kws dump meta` input was echoed by UART but produced no executed dump response
- Current conclusion:
  - the preserved-snapshot summary change is effective on board
  - the previous UART-flood failure is no longer the first blocker after this power cycle
  - another stall remains after snapshot capture, so board/local parity still cannot be completed for this INT8 image yet

## Step 5.123
- Revalidated the latest-SDK `student_dscnn_tiny_v2_int8_debug` board bring-up on `2026-04-09` after the preserved-snapshot diagnostic change.
- Flash path remained healthy:
  - sent `reboot uartburn` over `/dev/ttyUSB0`
  - flashed with `tools/river_flash.py` against `AMEBA_SDK_ROOT=/root/ameba-rtos`
  - AmebaFlash finished with `Finished PASS`
- Board runtime still failed before any KWS parity work could begin:
  - passive UART capture on `/dev/ttyUSB0` produced continuous `0x00`
  - official `monitor.py --debug` connected successfully, sent `AT+LIST`, and still received only repeated `0x00`
  - no normal boot markers appeared, including no `File System Init Success`, no `ameba-river boot`, and no KWS init logs
- This confirms the current blocker is still the latest-SDK INT8 image failing to enter a readable runtime / monitor state on board, not the new manual-chunk parity workflow.

## Step 5.122
- Refined the preserved `river kws align run` diagnostic path for the latest-SDK `student_dscnn_tiny_v2_int8_debug` bring-up.
- In [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - changed `river kws align run` to emit only tensor-dump `begin/meta/summary` after a successful replay capture
  - kept the captured snapshot alive after `align run` success so the existing `river kws dump meta` and `river kws dump chunk ...` commands can fetch data incrementally
  - only clear the preserved snapshot on alignment failure
- This keeps the board/local parity mechanism unchanged while avoiding the UART flood caused by auto-printing hundreds of dump chunks in one burst.
- Verified this step with a full latest-SDK rebuild after the change.

## Step 5.121
- Hardened the preserved `river kws align run` diagnostic path for slow KWS variants without changing the normal wakeword runtime path.
- In `components/river_voice/river_voice_kws.cc`:
  - added alignment-only queue throttling so the compiled-sample replay no longer keeps feeding frames unchecked while a slow model is still draining the worker queue
  - widened the alignment idle/snapshot wait windows for latest-SDK slow-model bring-up
- Kept the existing board/host parity mechanism intact:
  - `river kws align run`
  - `river kws dump meta`
  - `river kws dump chunk ...`
  - `tools/kws/replay_board_tensor_dump.py`
- Verified this step with a full latest-SDK rebuild after the change.

## Step 5.120
- Added a parallel `student_dscnn_tiny_v2_int8_debug` wakeword variant without removing any of the preserved board/host parity infrastructure.
- Generated and embedded the algorithm bundle's `student_dscnn_tiny_v2` INT8 TFLite model as:
  - `components/river_voice/generated/student_dscnn_tiny_v2_int8_model_data.h`
- Extended KWS model selection so the new DS-CNN tiny INT8 bundle can reuse the same `40x101`, centered log-mel, tensor-dump, align-replay, and host-replay workflow already used by prior debug variants.
- Switched the active project configuration to:
  - `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_DSCNN_TINY_V2_INT8_DEBUG=y`
  - `CONFIG_RIVER_KWS_TENSOR_ARENA_KB=2048`
  - `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=9125`
- Updated `env.sh` so the repository helper now defaults to the latest SDK checkout at `/root/ameba-rtos`.
- Verified a full `RTL8730E` build against `/root/ameba-rtos` completed successfully and produced:
  - `build_RTL8730E/km0_km4_ca32_app.bin`
  - `build_RTL8730E/build/project_ap/image/ap_image_all.bin`

## Step 5.119
- Persisted the SDK baseline rule in `AGENTS.md`.
- Future work should default to the latest SDK checkout at `/root/ameba-rtos` for build, flash, validation, and wakeword-model bring-up unless the user explicitly requests another SDK tree.

## Step 1
- Initialized external Ameba project `ameba-river`.
- Added project-level `AGENTS.md` and `.codex` workflow documents.
- Replaced default single-file example with layered directories for `core`, `voice`, `cloud`, `diag`, and `app`.
- Chose a custom staged skeleton instead of copying `speechmind` directly because `speechmind` is not exposed through the SDK `new-project -a` example path and would add too much bring-up risk for the first board-verification cycle.
- Added a first runnable feature set:
  - boot banner and status reporting
  - monitor command `river`
  - text echo path
  - simulated device control for `light`, `fan`, `curtain`, and `socket`
- Reserved stable interfaces for future local VAD, wake word, offline ASR, and real online control transport.

## Step 1.1
- Added a project-side CMake bootstrap to pre-generate `build_info.h` placeholders in `menuconfig/project_{ap,hp,lp}`.
- This works around an SDK parallel-build race seen on `RTL8730E`, where `wifi_tunnel_app` may compile before the SDK-generated `build_info.h` exists.

## Step 1.2
- Captured `RTL8730E` EVB board knowledge from the user-provided hardware guide into `.codex/knowledge.md`.
- Recorded bring-up-critical hardware facts for future implementation:
  - LOGUART and reset baseline
  - NOR/NAND and download implications
  - audio input and amplifier constraints
  - restricted GPIOs on `RTL8730EAM`
  - SWD and antenna rework notes
- Kept the original EVB PDF in `.codex` so project decisions can be traced back to the source document.

## Step 2
- Enabled the project-side audio framework and passthrough build options needed for board audio bring-up.
- Added `river_voice_echo_*` in `components/river_voice`:
  - `AudioRecord` capture from `AMIC1 + AMIC3`
  - `AudioTrack` playback to speaker
  - fixed `1000 ms` ring-buffer delay
  - short capture warm-up mute to reduce startup pop/noise
- Extended the `river` monitor command with:
  - `river audio start`
  - `river audio stop`
  - `river audio status`
  - `river audio echo <start|stop|status>`
- Kept the feature command-driven instead of auto-starting at boot so board validation can stay isolated and reversible.
- Verified the code path with a full local `RTL8730E` build after enabling audio framework support.
- Recorded the in-use board silk-screen as `EV730EA2 RO1` and kept the chip package / flash type as still-to-confirm hardware facts.

## Step 2.1
- Added serial-side audio echo diagnostics so capture and playback can be distinguished without changing the board wiring.
- Extended the monitor command with:
  - `river audio diag on`
  - `river audio diag off`
  - `river audio diag status`
- Added rolling `1 second` diagnostic logs from the echo task:
  - `cap_peak=[ch0,ch1]` for capture-side PCM peak
  - `play_peak=[ch0,ch1]` for delayed playback-side PCM peak
  - read/write success and failure counters
  - partial-read counter
- Exposed diagnostic state through `river audio status` so runtime state can be checked before and after starting the loop.
- Verified the diagnostic-enhanced build locally for `RTL8730E`.

## Step 2.2
- Added configurable boot-time audio echo autostart for board bring-up when monitor command injection is unavailable or unreliable.
- Added two project configs:
  - `CONFIG_RIVER_AUDIO_ECHO_AUTOSTART`
  - `CONFIG_RIVER_AUDIO_ECHO_DIAG_DEFAULT_ON`
- Enabled both in `prj.conf` for the current board-validation phase.
- Updated boot flow so `river_app_boot()`:
  - enables echo diagnostics at startup
  - starts the `1000 ms` delayed mic-to-speaker echo automatically
  - keeps printing `river` status after autostart so runtime state is visible from the boot log alone

## Step 2.3
- Fixed project Kconfig visibility for the `river` sources by including the generated `platform_autoconf.h` through `river_types.h`.
- This was required because the external-project compile flow passes the generated config header through include paths, not through per-file `-D CONFIG_*` flags.
- With this fix in place:
  - `CONFIG_RIVER_AUDIO_ECHO_AUTOSTART`
  - `CONFIG_RIVER_AUDIO_ECHO_DIAG_DEFAULT_ON`
  - `CONFIG_RIVER_DIAG_CMD_EN`
  - other `CONFIG_RIVER_*` switches
  now affect the compiled `river` code as intended.
- Fixed `river_diag_cmd.c` includes so the command module also builds when `CONFIG_RIVER_DIAG_CMD_EN` is truly enabled.

## Step 2.4
- Narrowed the board echo path to a lower-risk validation profile after runtime logs showed the digital chain was healthy but the replayed content was dominated by noise.
- Changed the echo profile to:
  - mono capture/playback
  - `AMIC3` only
  - lower speaker volume
  - lower mic boost
  - capture high-pass filter enabled
  - simple noise gate before the delayed replay buffer
- The goal of this step is to determine whether the EVB can produce an intelligible delayed voice replay before revisiting multi-mic raw playback.

## Step 2.5
- Added a direct speaker playback self-test based on the SDK `aplay` / `AudioTrack` path, so speaker output can be validated without involving microphone capture.
- Added `river_voice_speaker_test_*` in `components/river_voice`:
  - fixed dual-mono PCM playback to `DEVICE_OUT_SPEAKER`
  - repeating board-audible test pattern:
    - `1000 Hz` for `400 ms`
    - `200 ms` silence
    - `1500 Hz` for `400 ms`
    - `1000 ms` silence
  - lightweight serial diagnostics for playback progress
- Updated boot flow so this playback self-test can autostart independently of the echo path.
- Switched current `prj.conf` bring-up defaults from echo autostart to speaker-test autostart to isolate the analog output chain first.
- Recorded the board flash type as runtime-confirmed `NOR` from the boot log.

## Step 2.5.1
- Raised the direct speaker self-test output gain after the first board run proved playback existed but was too quiet for reliable evaluation.
- Aligned hardware volume with the SDK `aplay` default level range and increased PCM tone amplitude while keeping the same playback pattern.

## Step 2.6
- The direct speaker playback self-test is now board-proven, so the default bring-up path is switched back to mic-to-speaker echo.
- Current `prj.conf` defaults now:
  - enable echo autostart
  - enable echo diagnostics by default
  - disable speaker self-test autostart
- Raised echo playback hardware volume so delayed replay is easier to evaluate on the already-proven speaker path.
- Added an explicit echo gain log line at boot to keep runtime settings visible in serial output.

## Step 2.6.1
- Runtime diagnostics showed a healthy capture path and nonzero delayed playback PCM, but the board still produced no audible echo.
- To align echo with the already-proven direct playback route, the echo output path now uses:
  - `AMIC3` mono capture
  - mono delay buffer
  - dual-mono stereo speaker playback
  - explicit `AudioTrack_SetVolume(1.0, 1.0)`
- This step isolates whether the previous silence came from mono-track playback format mismatch rather than from capture failure.

## Step 2.6.2
- The dual-mono echo path became audible on the board, but replay loudness was still too low for practical evaluation.
- Raised echo playback output again:
  - hardware playback volume from `0.45` to `0.60`
  - added saturating PCM replay gain of `x4` before stereo duplication
- This keeps the microphone route unchanged and only increases delayed replay loudness.

## Step 2.7
- Reviewed SDK `aivoice` and `speechmind` before changing the board path:
  - `aivoice` explicitly supports `AFE_LINEAR_2MIC_30MM`, `50MM`, and `70MM`
  - `speechmind` on `AmebaSmart` uses `AFE_CONFIG_ASR_DEFAULT_2MIC50MM()`
  - the `EA` board routing in `speechmind` maps the first dual-mic pair to `AMIC1 + AMIC3`
  - `AMIC5` is also configured there as an extra raw channel
- Added an explicit board-array abstraction in `river_voice_board.*` so microphone routing and future AFE geometry stay out of the echo task itself.
- Current board baseline recorded in code:
  - board family: `EV8730EA2/EV730EA2`
  - active array: `linear-2mic-50mm`
  - active capture pair: `AMIC1 + AMIC3`
  - reserved auxiliary raw mic: `AMIC5`
- Refactored the echo path to validate dual-mic capture without forcing an early beamforming implementation:
  - capture is now `2 ch`
  - raw dual-mic PCM is downmixed to mono for the delay ring
  - delayed mono PCM is expanded back to dual-mono speaker playback
  - diagnostics keep reporting both capture channels independently
- Verified the new dual-mic-array build locally for `RTL8730E`.

## Step 2.8
- Tuned the raw dual-mic echo path for farther speech pickup and more useful board-side listening before AFE integration.
- Raised the board-array analog mic boost:
  - `AMIC1` from `15dB` to `20dB`
  - `AMIC3` from `15dB` to `20dB`
- Replaced simple `50/50` dual-mic averaging with a frame-local focused mix:
  - if one microphone is significantly stronger, mix weights become `3:1`
  - otherwise the path still behaves like a near-average mix
- Added lightweight per-frame AGC on the mixed mono debug signal:
  - target peak `6000`
  - max gain `x8`
- Reduced the pre-AGC noise gate threshold from `1024` to `256` so moderate-distance speech is less likely to be dropped entirely.
- Reduced fixed playback PCM gain from `x4` to `x2` because gain is now moved earlier into the adaptive mix stage.
- Verified the tuned build locally for `RTL8730E`.

## Step 3.0
- Started the first `RTL8730E`-appropriate AFE integration step without coupling the application to `speechmind`.
- Added `river_voice_capture.*`:
  - owns raw microphone-array capture only
  - uses board metadata from `river_voice_board.*`
  - currently outputs `16 kHz`, `16 ms`, `2 ch`, `PCM16`, matching AIVoice AFE input requirements
- Added `river_voice_preproc.*`:
  - defines a backend-neutral enhancement boundary
  - owns preproc frame metadata and backend context
  - is designed to keep future `self_dsp` or `TFLite Micro` replacement local to this layer
- Added `river_voice_preproc_aivoice.c`:
  - directly integrates SDK `aivoice_iface_afe_v1`
  - uses `AFE_CONFIG_ASR_DEFAULT_2MIC50MM()` as the starting point because this matches SDK `speechmind` on `AmebaSmart`
  - overrides the runtime policy for the current stage:
    - `ref_num = 0`
    - `enable_aec = false`
    - `enable_ns = false`
    - `enable_agc = true`
    - `enable_ssl = false`
- Updated board metadata so the voice frame cadence is `16 ms` instead of `20 ms`, matching `256 samples @ 16 kHz`.
- Refactored `river_voice_echo.c` into a debug sink on top of the new front-end:
  - input is now raw dual-mic capture
  - enhancement is now done by the AFE backend
  - delayed replay uses enhanced mono PCM expanded to dual-mono speaker output
  - diagnostics now separate:
    - raw capture health
    - preproc success/failure
    - playback write health
- Enabled the SDK-side voice resources needed by the current and next stages in `prj.conf`:
  - `AIVOICE`
  - `AFE 2MIC50MM`
  - baseline `VAD/KWS/ASR` resources for later steps
- Verified the refactored AFE-only build locally for `RTL8730E`.

## Step 3.1
- Tuned the `AFE-only` replay path for board-side intelligibility before adding VAD or AEC.
- Updated the SDK AFE runtime policy:
  - `NS` enabled
  - `NS` aggressiveness kept low to reduce noise without over-distorting speech
  - AFE fixed AGC gain raised from the default profile to `15 dB`
- Raised the board-side listening gain:
  - capture volume from `0x28` to `0x30`
  - speaker hardware volume from `0.65` to `0.80`
- Added a replay-only post-AFE adaptive gain stage in `river_voice_echo.c`:
  - target peak `12000`
  - max gain `x4`
  - silence gate `96`
- Added `afe_peak` to the serial diagnostics so the project can now distinguish:
  - raw mic energy
  - enhanced AFE output energy
  - final playback energy
- Verified the tuned build locally for `RTL8730E`.

## Step 4.7
- Integrated the verified `Silero VAD` artifact into the on-device `RTL8730E` runtime instead of leaving the detector in staged mode only.
- Added host-side model-data generation:
  - `tools/silero_vad/generate_model_data.py`
  - generated embedded model files:
    - `components/river_voice/generated/river_silero_vad_model_data.h`
    - `components/river_voice/generated/river_silero_vad_model_data.cc`
- Replaced the staged C detector shim with a real `TFLite Micro` runtime implementation:
  - `components/river_voice/river_voice_detector_silero.cc`
  - embeds `silero_vad_16k_b1_fp32.tflite`
  - uses official streaming semantics:
    - `256`-sample feed
    - `512`-sample decision window
    - `64`-sample rolling context
    - recurrent state `2 x 1 x 128`
- Added project configs for first board-side tuning:
  - `CONFIG_RIVER_SILERO_VAD_TENSOR_ARENA_KB=256`
  - `CONFIG_RIVER_SILERO_VAD_SPEECH_THRESHOLD_Q15=16384`
- Integrated detector execution into the active debug pipeline:
  - echo task now runs `detector_process()` on the enhanced mono frame before replay-only AGC/warmup changes it
  - boot/runtime diagnostics now report:
    - `vad_prob_q15`
    - `vad=speech|silence`
    - `vad_decisions`
    - `vad_speech`
    - `det_ok`
    - `det_fail`
- Kept detector output observational in this step:
  - no playback gating
  - no ASR routing decision yet
  - goal is first on-device `Silero` visibility, not policy coupling
- Added local C++-side compatibility handling instead of patching the SDK:
  - define missing `TFLITE_*` feature macros expected by the current SDK header set
  - suppress `unused-parameter` only for the local `river_voice` target so `TFLite Micro` headers compile cleanly under project `-Werror`
- Verified a full local `RTL8730E` build after runtime integration.
- Captured first resource baseline from the successful build:
  - embedded `.tflite` artifact size: about `1.2 MB`
  - `target_img2_ap.axf` size: about `14 MB`
  - `target_img2_ap.axf` sections:

## Step 4.21
- Added `river_voice_segment_buffer.*` as a reusable speech-segment cache module for the next online ASR step.
- `vad_probe` now writes enhanced mono frames into that segment buffer and emits:
  - ready-segment size
  - ready-segment duration
  - completed / dropped segment counters
- Added `river_voice_segment_sink.*` as a stable handoff boundary between buffered local speech segments and future online ASR transport.
- Current sink backend is `online_asr_stub`, so the buffered path is exercised without coupling the voice path to a cloud implementation yet.
- The default pure-VAD path now keeps buffered context around speech:
  - pre-roll `384 ms`
  - post-roll `768 ms`
  - max segment `8000 ms`
- Lowered `Silero` decision thresholds to favor recall over precision:
  - enter threshold `9000`
  - exit threshold `2500`
  - hangover `10`
  - EMA shift kept at `1` for fast reaction
- Reduced the pure-probe diagnostic window to about `96 ms` and added:
  - `vad_start`
  - `vad_end`
  - segment prebuffer / post-roll state
- Added a dedicated Chinese migration record:
  - `/.codex/silero_vad_migration_zh.md`
- The Chinese migration note now explains:
  - official upstream selection
  - host conversion path
  - device runtime integration
  - `RTL8730E SDK` compatibility fixes
  - flash profile changes
  - reproduction and audit procedure
    - `text=2355576`
    - `data=38868`
  - `bss=87680`
  - packaged app image `km0_km4_ca32_app.bin`: about `2.8 MB`

## Step 5.0
- Replaced the old cloud-ASR stub with a real provider framework under `components/river_cloud`.
- Added `river_wifi_station.*`:
  - STA auto-connect task
  - project-local temporary credentials in `include/river/river_wifi_credentials.h`
  - retry and status logging for field bring-up
- Expanded `include/river/river_cloud.h` from a text-stub surface into a provider-neutral ASR contract:
  - audio-open / audio-close
  - streaming frame push
  - batch segment submit
  - partial/final/error/session result callback
- Added `river_cloud_adapter.c`:
  - project-owned bridge between local VAD output and cloud providers
  - provider-neutral result fan-out back into `river_core`
  - streaming pre-roll / post-roll handling so provider code does not own local VAD policy
  - SNTP/UTC readiness gating for signed cloud requests
- Added provider-internal registry and ops boundary:
  - `river_asr_provider_internal.h`
  - `river_asr_provider_registry.c`
  - provider choice is no longer hardcoded in the adapter body
- Added the first real provider implementation:
  - `river_asr_iflytek_rtasr.c`
  - target service:
    - iFlytek RTASR LLM WebSocket API
  - implemented pieces:
    - query-string auth signing with `HMAC-SHA1 + Base64 + URL-encode`
    - WebSocket session open / binary PCM feed / finish
    - JSON result parsing through `cJSON`
    - partial/final/error/session callbacks
  - temporary credentials are stored in:
    - `include/river/river_asr_iflytek_credentials.h`
- Connected the local speech path to cloud ASR:
  - `vad_probe` now opens the cloud audio bridge
  - enhanced mono frames are pushed to the streaming bridge on every detector decision
  - `river_core` now logs:
    - session started
    - partial result
    - final result
    - error
    - session closed
- Kept non-streaming architecture in place for future providers:
  - `segment_buffer` still produces ready utterance segments
  - `segment_sink` now bridges those segments into `river_cloud_asr_batch_submit_segment()`
  - current iFlytek provider still reports `batch unsupported`
- Added Chinese integration / audit record:
  - `/.codex/iflytek_rtasr_integration_zh.md`

## Step 5.0.1
- Tightened the segment/batch accounting so unsupported provider batch upload is no longer misreported as a successful submit.
- `river_cloud_asr_batch_submit_segment()` now returns `RIVER_ERR_UNSUPPORTED` transparently.
- `river_voice_segment_sink` now tracks:
  - submitted segments
  - unsupported segments
  - real failures
- `vad_probe` diagnostics now distinguish:
  - `seg_unsupported`
  - `seg_fail`

## Step 4.8
- Copied the `RTL8730E NOR` device profile into the project and turned it into a project-owned flash profile flow.
- Added project-side profile sources under `board/rtl8730e/profiles/`:
  - `RTL8730E_NOR.sdk.json`: decrypted SDK stock baseline
  - `RTL8730E_NOR.json`: project development profile source
  - `RTL8730E_NOR.rdev`: encrypted profile generated from the project JSON
- Added profile tooling:
  - `tools/generate_rdev.py`: regenerate encrypted `.rdev` from project JSON
  - `tools/river_flash.py`: project-owned flash wrapper that prefers the local profile over the SDK profile
- The development NOR profile expands the app package download range:
  - SDK stock: `0x08040000-0x08300000`
  - project dev: `0x08040000-0x08600000`
- Why this was required:
  - current `km0_km4_ca32_app.bin` size: `2929664`
  - SDK stock slot size: `2883584`
  - overflow: `46080`
- The new profile is explicitly marked as development-only because it consumes the OTA2 area for extra app space.

## Step 3.1.1
- Reverted the experimental detector-gated replay step after user feedback showed this direction was not wanted for the current phase.
- Kept the architecture prepared for future `VAD`, but restored the active runtime path to:
  - `capture -> preproc -> replay`
- Retuned the AFE/replay gain policy to reduce idle noise without introducing a new detector stage:
  - AFE fixed AGC gain reduced from `15 dB` to `9 dB`
  - AFE `NS` aggressiveness raised from `low` to `mid`
  - replay-side post-AGC reduced from `target12000/maxx4` to `target9000/maxx2`
- replay-side post-AGC floor raised from `96` to `192`
- frames below the post-AGC floor are now muted instead of being replayed as background hiss
- Verified the retuned build locally for `RTL8730E`.

## Step 3.2
- Added `river_voice_ref.*` as an independent playback-reference staging layer for future `AEC`.
- Current reference design:
  - source: delayed mono frame that is actually sent toward speaker playback
  - transport: project-side ring buffer
  - consumer boundary: `river_voice_preproc_process(preproc, mic, ref, ...)`
- Widened `river_voice_preproc` so the active backend interface can already accept optional reference PCM without binding application logic to SDK-specific `AEC` details.
- Kept `aivoice_afe` in `AFE-only` mode for this step:
  - reference is staged and observable
  - `AEC` is still disabled
- Extended echo diagnostics with playback-reference counters so the next `AEC` step can be validated from serial:
  - `ref_read_ok`
  - `ref_read_miss`
  - `ref_write_ok`
  - `ref_write_fail`
- Verified the staged-reference build locally for `RTL8730E`.

## Step 3.3
- Enabled `AEC` on top of the already-staged playback-reference path instead of changing the application-layer voice pipeline.
- Updated `river_voice_preproc_aivoice.c` so the active AIVoice feed frame is now interleaved as:
  - `mic0`
  - `mic1`
  - `playback_ref`
- Switched the runtime AFE policy to a communication-oriented profile:
  - `AFE_FOR_COM`
  - `enable_aec = true`
  - `ref_num = 1`

## Step 4.2
- Added `river_voice_detector.*` as a first-class local detector boundary for the upcoming `Silero VAD` migration.
- Added `river_voice_detector_silero.c` as the staged default backend:
  - current step validates interface shape and frame organization only
  - current step does not yet import or run the real model blob
- Fixed the detector-side cadence and window assumptions in code and docs:
  - input from `preproc`: mono `PCM16`, `16 kHz`, `256 samples / 16 ms`
  - staged detector window: `512 samples / 32 ms`
- Enabled `TFLite Micro` in project config so the runtime direction is explicit from the start.
- Extended boot/status logs so the current detector backend is visible:
  - `local_detector=silero_vad`
  - detector profile dump now reports `runtime=tflite_micro`
- Updated the dedicated `Silero` porting record with the first hard decisions:
  - migrate the original model first
  - do not prune or quantize in the first migration step
  - only compress later if actual flash / RAM / latency measurements require it

## Step 4.3
- Downloaded the official `Silero VAD` upstream repository and pinned commit `0dd0d85ee86b1f9d178dc26a04e60e90de26a80f`.
- Vendored the exact first conversion input into the project:
  - `third_party/silero_vad/upstream/silero_vad_16k_op15.onnx`
- Added upstream metadata and checksum tracking in:
  - `third_party/silero_vad/upstream/METADATA.md`
- Tightened the staged detector logs so the runtime now reports the selected official source model and the official streaming contract baseline.
  - `NS mid`
  - `adaptive AGC + fixed 5 dB`
  - `RES mid`
  - `SSL off`
- Kept the project-side front-end boundary unchanged:
  - `river_voice_capture` still owns raw dual-mic capture
  - `river_voice_ref` still owns the playback reference ring
  - `river_voice_preproc_process(preproc, mic, ref, ...)` remains the only place where SDK `AEC` is bound
- Updated boot-time logs so the running profile clearly states:
  - `capture dual-mic + 1ch ref -> AEC/AFE 1ch`
  - `audio echo ref ... aec=on`
- Verified the `AEC`-enabled build locally for `RTL8730E`.

## Step 3.4
- Pivoted the active front-end strategy from `COM/AEC` back to `ASR-first`, because the project goal is now explicitly to maximize wake-word and ASR quality.
- Updated `river_voice_preproc_aivoice.c` so the running AIVoice policy is now:
  - `AFE_FOR_ASR`
  - `ref_num = 0`
  - `enable_aec = false`
  - `enable_ns = false`
  - `enable_agc = true`
  - `enable_ssl = true`
  - `agc_fixed_gain = 10 dB`
  - `enable_adaptive_agc = false`
- Kept the playback-reference packing code in place as optional infrastructure, but removed it from the active default runtime path.
- Updated `river_voice_echo.c` so the debug replay path now reflects the real running profile:
  - `capture dual-mic -> ASR-AFE 1ch -> delayed dual-mono replay`
  - `audio echo ref ... aec=staged-off`
- Verified the `ASR-first` build locally for `RTL8730E`.

## Step 4.0
- Reset the implementation roadmap around the user's final product priorities:
  - only `ASR-first`
  - raise `AEC` priority
  - stop planning around SDK `VAD`
  - migrate directly to `Silero VAD`
- Added a dedicated reproducibility document target for `Silero VAD` migration:
  - `/.codex/silero_vad_porting.md`
- Declared that future self-developed `AEC/VAD` must plug into existing stable interfaces instead of leaking SDK-specific assumptions upward.

## Step 4.1
- Removed the old direct speaker self-test implementation from the active codebase:
  - deleted `components/river_voice/river_voice_speaker_test.c`
  - removed related declarations, build entries, boot hooks, and Kconfig items
- Simplified the project config so current voice resources reflect the actual roadmap:
  - kept `AIVOICE + AFE 2MIC50MM`
  - removed SDK `VAD`
  - removed SDK `KWS`
  - removed SDK `ASR` resource selection
- Added explicit preproc profiles on the stable `river_voice_preproc` boundary:
  - `asr_mainline`
  - `asr_barge_in_aec`
- Set the current default profile to `asr_barge_in_aec`, but kept the backend policy on the `AFE_FOR_ASR` side so the product remains `ASR-first`.
- Cleaned up runtime logs and status output so they now describe:
  - `asr-first` frontend intent
  - current preproc backend
  - current preproc profile
  - `Silero VAD` still pending

## Step 4.4
- Built a dedicated host-side Silero conversion environment under:
  - `/root/ameba-river/.venv-silero-convert`
- Pinned the first host conversion toolchain in that environment:
  - `onnx`
  - `onnxruntime`
  - `onnxsim`
  - `onnxoptimizer`
  - `onnx-graphsurgeon`
  - `onnx2tf`
  - `tensorflow`
  - `tf_keras`
- Corrected the staged detector contract and documentation:
  - `512` is the logical current window
  - `64` is the rolling context
  - `576` is the real official model input tensor
- Added a reproducible ONNX inspection tool:
  - `tools/silero_vad/extract_onnx_manifest.py`
- Added a generated manifest for the vendored official source model:
  - `third_party/silero_vad/upstream/silero_vad_16k_op15_manifest.json`
- Proved that direct `onnx2tf` conversion is currently unstable for the pinned `op15` graph:
  - direct probe fails at `wa/model/stft/Conv`
  - a manual transpose repair gets past `STFT` but then fails at `wa/model/decoder/Squeeze`
- Based on the official published `tinygrad` skeleton, the next migration path is now explicitly:
  - rebuild the official network structure in host-side `Keras`
  - load weights from the official ONNX graph
  - export that reconstructed model to `.tflite`

## Step 4.5
- Found and corrected a source-hygiene issue during host-side migration:
  - the vendored `silero_vad_16k_op15.onnx` had been modified in place by local tooling
  - restored it from the pinned upstream checkout so the repository copy is again the official baseline
- Added a guarded staging tool:
  - `tools/silero_vad/stage_conversion_source.py`
  - purpose: always copy the vendored ONNX into a temporary conversion path before running host tooling
- Added a reconstruction-oriented extractor:
  - `tools/silero_vad/extract_reconstruction_tensors.py`
  - purpose: emit source tensor metadata plus derived decoder `LSTM` tensors after the ONNX slice/concat layout
- Confirmed one important canonical-layout detail from the restored official ONNX:
  - `model.decoder.rnn.weight_ih`
  - `model.decoder.rnn.weight_hh`
  - `model.decoder.rnn.bias_ih`
  - `model.decoder.rnn.bias_hh`
  are top-level initializers in the real upstream artifact
- Added a generated reconstruction manifest:
  - `third_party/silero_vad/upstream/silero_vad_16k_op15_reconstruction_manifest.json`
- This step intentionally does not add a `.tflite` model yet.
  - it narrows the migration by proving that the pinned official source can now be treated as immutable
  - and that the next `Keras` reconstruction step has a reproducible tensor map to start from

## Step 4.6
- Added `tools/silero_vad/rebuild_tf_silero_vad.py`:
  - rebuilds the pinned official `Silero VAD` graph in `TensorFlow`
  - uses an explicit ONNX-style decoder `LSTM` implementation to avoid gate-order ambiguity
  - exports a batch=`1` `TFLite` artifact
- Verified the reconstructed TensorFlow model against official ONNX:
  - output max abs diff `1.5599653124809265e-08`
  - state max abs diff `1.6689300537109375e-06`
- Added generated artifacts:
  - `third_party/silero_vad/generated/silero_vad_16k_b1_fp32.tflite`
  - `third_party/silero_vad/generated/silero_vad_16k_b1_fp32_verification.json`
  - `third_party/silero_vad/generated/METADATA.md`
- This is the first project-side `Silero VAD` detector artifact that is both:
  - directly derived from the pinned official upstream model
  - numerically checked against the original ONNX

## Step 4.9
- Root-caused the first board-side `Silero VAD` boot crash after flashing the oversized image:
  - the fault occurred inside `river_voice_detector_silero_open`
  - the crash address mapped into `tflite::MicroMutableOpResolver<16>::AddBuiltin`
  - the actual bug was object lifetime, not model size or flash layout
- Fixed `river_voice_detector_silero.cc` so C++ runtime objects are constructed and destroyed correctly:
  - explicitly placement-construct `op_resolver` before registering kernels
  - explicitly destroy `op_resolver` on every early-return and close path
  - make tensor-arena freeing symmetric for both allocation backends
- Revalidated the code-side fix locally:
  - `river_voice_detector_silero.o` rebuilds successfully with `CCACHE_DISABLE=1`
  - the earlier build interruption was due to host `ccache` permission on `/run/user/0`, not due to this source change

## Step 4.10
- Collected the next board-side runtime failure after the constructor fix:
  - detector now gets past the previous data abort
  - boot log reaches `silero_vad tensor binding failed`
- Cross-checked the exported `.tflite` artifact in the host conversion venv:
  - input 0: `serving_default_state:0` `[2,1,128]` `float32`
  - input 1: `serving_default_input:0` `[1,576]` `float32`
  - output 0: `PartitionedCall:0` `[1,1]` `float32`
  - output 1: `PartitionedCall:1` `[2,1,128]` `float32`
- Updated the device runtime binding logic accordingly:
  - bind inputs and outputs by the pinned artifact's actual interpreter index order first
  - keep shape-based fallback only as a secondary path
  - accept scalar-like probability outputs as either `[1]` or `[1,1]`
  - dump the interpreter I/O tensor inventory on binding failure so future mismatches are immediately visible on serial logs
- Revalidated the changed detector component locally:
  - `CCACHE_DISABLE=1 cmake --build ... --target river_voice_target_img2_ap` passes

## Step 4.11
- Collected the next board-side tensor inventory from the real `RTL8730E` SDK `TFLite Micro` runtime:
  - `inputs=2`, `outputs=2` are reported correctly
  - tensor structs are non-null
  - tensor `type=float32` is still valid
  - but tensor `dims` and `name` metadata are not usable on-device for this model
- Updated `river_voice_detector_silero.cc` to stop depending on tensor shape metadata at runtime:
  - trust the pinned interpreter I/O order first
  - validate tensor buffer availability through:
    - `data` pointer
    - `bytes` lower bound
    - element type
  - keep failure logs for `data` and `bytes` so future SDK/runtime differences remain diagnosable
- Rebuilt the full `RTL8730E` image successfully after this compatibility fix.
- Current expected next board behavior:
  - detector should advance past `silero_vad tensor binding failed`
  - next useful milestone log is `silero_vad runtime ready`

## Step 4.12
- Collected the next board-side `Silero` failure from the updated build:
  - tensor structs are present
  - tensor `bytes` are non-zero and match the expected payload sizes
  - but top-level `TfLiteTensor.data` is still `NULL` for all model I/O on this SDK/runtime snapshot
- Root-caused the remaining bring-up blocker:
  - the current `RTL8730E` `TFLite Micro` runtime can expose valid I/O tensor handles while leaving their top-level `data` pointers unset
  - this means detector binding cannot rely on `interpreter->input()/output()` alone even after `AllocateTensors()`
- Updated `river_voice_detector_silero.cc` to add an SDK-compatibility fallback:
  - construct the interpreter with `preserve_all_tensors=true`
  - fetch the corresponding `TfLiteEvalTensor` handles for the pinned I/O order
  - patch missing I/O buffers with detector-owned fallback storage:
    - audio input -> `audio_input_buffer`
    - state input -> `recurrent_state`
    - probability output -> `probability_output_buffer`
    - state output -> `next_state`
  - mirror eval-tensor buffers back into the persistent `TfLiteTensor` views when the SDK leaves them empty
- Revalidated after the fallback-buffer change:
  - `CCACHE_DISABLE=1 cmake --build /root/ameba-river/build_RTL8730E/build --parallel --target river_voice_target_img2_ap` passed
  - full `RTL8730E` rebuild also passed locally

## Step 4.13
- Collected the next board-side `Silero` log after fallback-buffer patching:
  - top-level persistent tensors now show the expected:
    - `dims`
    - `data`
    - `bytes`
  - detector still failed during open
- Root cause refinement:
  - the remaining open-time guard was still validating `TfLiteEvalTensor` byte lengths
  - that check is stricter than what the runtime actually needs for `Invoke()`
  - on this SDK snapshot it can reject a usable interpreter state even when the persistent I/O views are already valid
- Updated `river_voice_detector_silero.cc` again:
  - relaxed eval-tensor validation to require only:
    - non-null eval tensor handle
    - `float32` type
    - non-null `data`
  - kept the stronger payload-size checks only on the persistent tensors that the detector actually reads and writes directly
- Revalidated after the guard relaxation:
  - `CCACHE_DISABLE=1 cmake --build /root/ameba-river/build_RTL8730E/build --parallel --target river_voice_target_img2_ap` passed
  - full image rebuild was started immediately after the targeted rebuild

## Step 4.14
- Collected the next board-side `Silero` log from commit `8a7036f`:
  - persistent I/O tensors now report valid:
    - `dims`
    - `data`
    - `bytes`
  - detector still fails in open before `runtime ready`
- Root cause refinement:
  - the remaining blocker is no longer persistent-tensor binding
  - `eval tensor` availability is still SDK-specific and should not be treated as a hard prerequisite for detector open
  - the detector runtime path itself reads and writes through the persistent tensors
- Updated `river_voice_detector_silero.cc`:
  - keep best-effort eval-tensor acquisition and patching
  - downgrade eval-tensor validation from hard failure to diagnostic warning:
    - log `silero_vad eval tensor state degraded: ...` when eval views are missing or incomplete
  - keep hard open-time rejection only for persistent tensors that the detector actually uses directly
- Revalidated after the change:
  - targeted `river_voice_target_img2_ap` rebuild passed locally

## Knowledge Update
- Added an external-reference section to `.codex/knowledge.md` covering three project families relevant to future voice work:
  - Realtek `ambd_arduino` `micro_speech`
  - Google `tflite-micro` `micro_speech`
  - ARM `ML-embedded-evaluation-kit`
- Recorded not just the links but the intended usage boundary for each:
  - which one is best for Ameba-specific audio / TFLM integration
  - which one is best for upstream pipeline architecture
  - which one is best kept as an optimization reference instead of direct reusable code

## Step 4.15
- Collected the next board-side `Silero` log from the image built at `2026-03-11 16:44:00`:
  - persistent tensors now report valid:
    - shapes
    - non-null `data`
    - expected payload sizes
  - but all four top-level tensors still report `type=0`
  - detector still fails in open before `runtime ready`
- Root cause refinement:
  - on this `RTL8730E` SDK snapshot, top-level `TfLiteTensor` / `TfLiteEvalTensor` views can arrive as `kTfLiteNoType`
    even when the buffers themselves are valid and usable
  - the remaining blocker is the detector's type guard, not tensor ownership or byte length
- Updated `river_voice_detector_silero.cc`:
  - treat `kTfLiteNoType` as a compatible transient SDK state for `Silero` top-level I/O binding
  - normalize both persistent and eval tensor type fields to `kTfLiteFloat32` during patch-up
  - keep hard rejection only for:
    - null tensor handle
    - null data pointer
    - insufficient payload bytes
- Next expected milestone:
  - boot should advance past `silero_vad tensor binding failed`
  - next useful log should be `silero_vad runtime ready: ...`

## Knowledge Update
- Triaged an additional external `Float32-first Silero deployment` reference into `.codex/knowledge.md`.
- Recorded the parts that are valid for current `RTL8730E` work:
  - keep `Float32` first
  - measure real arena / latency / heap before compression
  - keep `int16 -> float32` normalization and simple state copy
- Recorded the parts that need correction for this project:
  - `CA32` uses `-mfpu=neon -mfloat-abi=hard`, not `KM4`'s `fpv5-sp-d16`
  - cache-maintenance guidance is relevant, but `Cortex-M` APIs are not drop-in for current `CA32`
  - `AllOpsResolver` is not preferred for current embedded budget
- Added a standing rule:
  - future external references should be triaged directly into `.codex/knowledge.md`
  - store actionable conclusions rather than raw prose

## Tag Update
- Added milestone note:
  - `.codex/tags/m1-silero-vad-runtime-ready.md`
- Purpose of this tag note:
  - freeze the first verified `RTL8730E` milestone where:
    - `Silero VAD` reaches `runtime ready`
    - on-device detector decisions are produced
    - the project is ready to move from runtime bring-up into policy tuning

## Step 4.16
- Added a board RGB indicator module:
  - `include/river/river_board_rgb.h`
  - `components/river_voice/river_board_rgb.c`
- Initial implementation attempted to reuse the SDK `LEDC` / `WS2812` example path.
- Added project config:
  - `CONFIG_RIVER_BOARD_RGB_VAD_INDICATOR_EN`
- Enabled the RGB VAD indicator in `prj.conf`.
- Wired state changes into the current voice runtime:
  - app boot sets `boot`
  - echo task sets `silence` once running
  - detector decision toggles `silence` / `speech`
  - detector/open/autostart failures set `error`
  - echo stop sets `off`
- Recorded the board assumption and risk boundary in `.codex/knowledge.md` so later schematic confirmation or pin remap can be traced cleanly.

## Step 4.17
- Collected board feedback after the first RGB/VAD-status experiment:
  - `Silero VAD` was active, but status flickered between `speech` and `silence` too aggressively for a stable indicator
  - the RGB LED never lit even though the software path initialized
- Refined the `Silero VAD` post-processing in `river_voice_detector_silero.cc`:
  - lowered speech-enter threshold from `16384` to `12000`
  - added explicit speech-exit threshold `4500`
  - added hangover of `8` detector decisions
  - added simple EMA smoothing with shift `2`
  - changed exported `speech_probability_q15` to the smoothed probability so diagnostics match the actual decision logic
- Replaced the earlier `WS2812` assumption in `river_board_rgb.c` with a deferred board-status stub.
- Root cause for the RGB mismatch:
  - official EVB documentation indicates `USER LED` is a passive RGB circuit using `LEDR/LEDG/LEDB`, not a serial `WS2812`
  - documentation also says `R25`, `R27`, and `R31` should be populated to use the `USER LED` circuit
- Current runtime behavior:
  - the board RGB API remains in place for future use
  - boot now prints a one-time deferred warning instead of a false `rgb indicator ready`

## Step 4.18
- Added a diagnostic-only SDK VAD reference path:
  - `include/river/river_voice_vad_reference.h`
  - `components/river_voice/river_voice_vad_reference.c`
- Reference path policy:
  - uses `aivoice_iface_vad_v1`
  - feeds the same post-AFE enhanced mono `256-sample / 16ms` audio that `Silero` sees
  - does not affect the main `Silero` decision or any future product logic
  - exists only to compare detector behavior in the same runtime input chain
- Extended `Silero` detector diagnostics:
  - added raw probability export alongside the smoothed decision probability
  - diagnostics can now distinguish:
    - raw model output
    - smoothed / hysteresis decision probability
    - SDK VAD event state
- Updated board RGB handling:
  - runtime no longer assumes `PA_9 + WS2812`
  - current board RGB path remains intentionally deferred until `LEDR/LEDG/LEDB` mapping is confirmed

## Step 4.19
- Triaged an external `TFLite Micro` initialization reference into project knowledge instead of applying it blindly.
- Recorded the parts that are directly useful for current `Silero VAD` bring-up:
  - schema-version guard
  - defensive `AllocateTensors()` failure handling
  - explicit `TensorArena` usage reporting
  - `float32 first, optimize later`
- Recorded the parts that need project-specific correction:
  - current project uses `MicroMutableOpResolver`, not `AllOpsResolver`
  - current runtime runs on `RTL8730E` `CA32`, so generic Cortex-M FPU flags are not directly applicable
  - generic `SCB_InvalidateDCache_by_Addr()` examples are not the current CA32 cache API
  - dynamic tensor-arena allocation needs allocator-level alignment scrutiny rather than static-array assumptions

## Step 4.20
- Switched the default validation runtime from echo replay to a pure detector probe path:
  - added `components/river_voice/river_voice_vad_probe.c`
  - added new public control APIs in `include/river/river_voice.h`
  - added `river audio probe <start|stop|status>` monitor handling
- Changed default boot mode:
  - disabled `CONFIG_RIVER_AUDIO_ECHO_AUTOSTART`
  - enabled `CONFIG_RIVER_VAD_PROBE_AUTOSTART`
  - selected `CONFIG_RIVER_VAD_PROBE_DIAG_DEFAULT_ON`
  - switched preproc validation from `asr_barge_in_aec` to `asr_mainline`
- Probe-path design:
  - `capture -> aivoice_afe(asr_mainline) -> silero + sdk_vad_ref -> serial diagnostics`
  - no speaker playback
  - no delay ring
  - no playback reference
  - no `AEC` in the validation path
- Raised diagnostic cadence from roughly `1s` windows to about `240ms` windows in the probe runtime so short utterances are easier to catch in serial logs.

## Step 4.22
- Added a shared project-owned logging abstraction:
  - `include/river/river_log.h`
  - `components/river_common/river_log.c`
  - `components/river_common/CMakeLists.txt`
- Logging policy is now layered instead of direct `printf` in product modules:
  - `ERROR`
  - `WARN`
  - `INFO`
  - `DEBUG`
- The default runtime level is now `INFO` through:
  - `Kconfig`
  - `CONFIG_RIVER_LOG_LEVEL_INFO=y` in `prj.conf`
- Log lines now carry a monotonic millisecond timestamp and stable tag:
  - format:
    - `[%010lu][<level>][<tag>] ...`
- `vad_probe` logging policy is now split into:
  - `INFO` only when VAD state changes
  - `DEBUG` for high-rate probe and segment diagnostics
- The new logger keeps a secondary sink abstraction for future:
  - file persistence
  - upload / relay
  - alternate transports
  These sinks are not implemented yet, but the boundary is now explicit.
- Refactored key voice/cloud modules to use the shared logger so background runtime output is more readable and easier to filter during bring-up.

## Step 4.23
- Hardened the diagnostic SDK VAD side path against heap exhaustion during online-ASR bring-up:
  - added `CONFIG_RIVER_AIVOICE_VAD_REFERENCE_MIN_FREE_HEAP_KB`
  - defaulted it to `320KB`
- `river_voice_vad_reference_open()` now checks current free heap before creating `aivoice_iface_vad_v1`.
- If free heap is below the configured floor, the SDK VAD reference is skipped with a clear warning instead of triggering a low-level `Malloc failed` path inside the vendor library.
- Updated upper-layer logs so the behavior is explicit:
  - `sdk_vad reference auto-disabled; keep silero-only decision logging`
- This keeps:
  - `Silero VAD`
  - pure VAD probe
  - cloud ASR bridge
  available even when the optional SDK comparison path is too expensive for the current heap budget.

## Step 4.24
- Hardened `vad_probe` against a second large heap consumer in the batch-segment path.
- Root cause:
  - `river_voice_vad_probe_prepare_buffers()` always opened `segment_buffer`
  - the current `iflytek_rtasr` provider is `stream=yes batch=no`
  - so the `8s` segment buffer was being allocated even though the provider could not consume it
  - this matched the runtime `Malloc failed ... xWantedSize:256064`
- Updated behavior:
  - if the active cloud provider does not support batch ASR, `vad_probe` now disables `segment_buffer` and keeps only the streaming bridge active
  - if a future provider does support batch, `vad_probe` now also checks free heap headroom before opening the batch segment buffer
  - if heap headroom is insufficient, batch buffering is skipped with a clear warning and the system continues in stream-only mode
- Added `CONFIG_RIVER_VAD_PROBE_SEGMENT_MIN_FREE_HEAP_KB` with default `64KB`
- Runtime status / diagnostics now expose whether the segment path is:
  - `enabled`
  - or `disabled`
- This preserves the architecture for future non-streaming ASR while removing a large, currently unnecessary heap allocation from the live `iflytek_rtasr` path.

## Step 4.25
- Hardened STA Wi-Fi bring-up against repeated auth / 4-way / busy failures observed while connecting to `Keeu`.
- Updated [river_wifi_station.c](/root/ameba-river/components/river_cloud/river_wifi_station.c) to use a multi-strategy connect policy:
  - try the most compatible path first: `SSID + password` only
  - if scan data is available, retry with scan-bound `BSSID + channel + security`
  - if the scanned AP reports `WPA2/WPA3 mixed`, also try a final `WPA2 AES` compatibility fallback
- The scan candidate selector now prefers `2.4G` APs over `5G` when the SSID is duplicated across bands, which is better aligned with embedded voice bring-up and home-router compatibility.
- Added explicit disconnect-and-idle waiting between retries so the driver has time to leave transitional join states before the next attempt.
- Failure logs now show the strategy name, decoded error reason, and current join state to make router-compatibility issues easier to identify on the next board run.
- Follow-up hardening:
  - explicitly disable SDK fast-connect in addition to SDK auto-reconnect
  - when `wifi_connect()` returns `-RTK_ERR_BUSY`, do not immediately declare failure; instead wait for the in-flight join flow to complete and adopt the connection if it succeeds
  - if the driver reaches `RTW_JOINSTATUS_SUCCESS` before IPv4 is assigned, the app now requests DHCP and completes the join instead of disconnecting and restarting
- This addresses the observed case where the SDK background path already printed `[$]wifi connected` but the app still treated the attempt as failed and tore it down.

## Step 4.26
- Selectively adopted the `REVIEW.md` guidance around Wi-Fi lifecycle ownership and startup race avoidance.
- Additional STA state-machine hardening in [river_wifi_station.c](/root/ameba-river/components/river_cloud/river_wifi_station.c):
  - disable SDK fast-connect before WLAN init, not only after the STA task starts
  - disable SDK LPS during bring-up to reduce auth / association instability while debugging connectivity
  - wait for the join state machine to return to an idle/disconnected state before launching a site survey or a fresh connect attempt
  - if a scan candidate is available, prefer `scan_exact` first and keep `basic` as a later fallback, instead of always trying the unconstrained path first
  - retry a busy site-survey once after waiting for driver idle
- Rationale:
  - the previous order still allowed driver-level background activity to race with the app's own connect flow
  - using the scanned `BSSID + channel + security` first is more deterministic for home-router debugging than restarting from the least constrained path each time
- Added root-level [TIPS.md](/root/ameba-river/TIPS.md) as the first-stop runtime troubleshooting guide for recurring serial-log signatures such as `auth_fail`, `busy`, and provider bring-up issues.
- Added a dedicated active Wi-Fi issue record in [.codex/issues.md](/root/ameba-river/.codex/issues.md) so transient field failures can be tracked with symptoms, mitigations, and closure criteria.
\n## Step 5\n- **Asynchronous Architecture Overhaul**: Implemented river_cloud_wk worker and Ring Buffer system.\n- **IPC Protection**: Task priority tuning to resolve net_connect -82 and IPC timeouts.\n- **Embedded Hardening**: Decomposed WS URL init and Multi-AP failover.\n- **Memory Audit**: Integrated vPortGetHeapStats for fragmentation tracking.

## Step 5.1
- Switched to branch `xiaozhi` and ran a full `RTL8730E` build at commit `43737ec`.
- This step is verification-only:
  - no firmware source files were changed
  - the goal was to confirm that the current `xiaozhi` branch still produces complete images
- Full build completed successfully and produced:
  - `build_RTL8730E/km4_boot_all.bin`
  - `build_RTL8730E/km0_km4_ca32_app.bin`
  - `build_RTL8730E/ota_all.bin`
- Current image sizes from this run:
  - `km4_boot_all.bin`: `51872`
  - `km0_km4_ca32_app.bin`: `3605856`
  - `ota_all.bin`: `3605888`

## Step 5.2
- On branch `DS-CNN`, completed a focused migration assessment of the DS-CNN training stack under `/root/kws-training-pro` against the current `ameba-river` board runtime contract.
- Added [DSCNN_KWS_TRAINING_PRO_MIGRATION_REPORT_ZH.md](/root/ameba-river/DSCNN_KWS_TRAINING_PRO_MIGRATION_REPORT_ZH.md) as the formal report for this review.
- Core conclusion captured in the report:
  - the DS-CNN architecture and teacher-student methodology are worth reusing
  - the existing student weights should **not** be treated as directly deployable on the current board
  - the biggest blocker is feature-contract mismatch, not network topology
- High-signal findings recorded:
  - `/root/kws-training-pro` contains both an old `40x101` path and a newer `40x98` path
  - the newer `train_dscnn_v2.py` still trains with `extract_from_array()`, which does not match the current board-side `river_voice_kws.cc` frontend exactly
  - the current KD loss in `train_dscnn_v2.py` is not a robust production-quality formulation for a binary wakeword student
  - the repo's validation script still validates the older `dscnn_v1` path and cannot be used as the final board deployment gate
- The report also includes a phased landing plan:
  - freeze board feature truth
  - import architecture only
  - rebuild the student training chain under the current board contract
  - unify evaluation
  - export int8 TFLite and validate on board

## Step 5.3
- On branch `DS-CNN`, completed a second migration assessment focused on `/root/river-openwakeword-lab` as the current external OpenWakeWord + DS-CNN training workspace.
- Added [RIVER_OPENWAKEWORD_LAB_MIGRATION_REPORT_ZH.md](/root/ameba-river/RIVER_OPENWAKEWORD_LAB_MIGRATION_REPORT_ZH.md) as the formal report for this review.
- Core judgment captured in the report:
  - `/root/river-openwakeword-lab` has substantially higher migration value than `/root/kws-training-pro`
  - the board-aligned student feature chain, student trainer, and int8 export path are worth reusing directly
  - the current workspace is engineering-mature but still not deployment-ready in model quality
- High-signal findings recorded:
  - the student extractor in `river-openwakeword-lab` now matches the current board `98x40 log-mel` contract much more closely than the older `kws-training-pro` implementation
  - `round6` student artifacts prove `train -> int8 tflite -> packaging` is already reproducible
  - the current best student remains `no-deploy` because board negative FPR is still too high
  - `assistant / KD / student verifier` are still documented but not actually implemented as runnable scripts
  - the current exported student includes `PAD`, while the current `DS-CNN` branch board resolver does not yet register `AddPad()`
- The report also clarifies the recommended role split:
  - keep `river-openwakeword-lab` as the external training lab
  - treat the host teacher as reference/mining infrastructure
  - treat the board-aligned DS-CNN student path as the real deployment direction

## Step 5.4
- On branch `DS-CNN`, replaced the stale architecture-refactor plan in `plan.md` with a branch-specific execution plan that puts full firmware build verification ahead of any further DS-CNN runtime migration.
- Recorded the current decision order in `plan.md`:
  - Phase 0: full build verification
  - Phase 1: review binary size and integration risk
  - Phase 2: only then choose whether to import runtime assets from `/root/river-openwakeword-lab`
- Captured the current runtime constraint directly in the plan:
  - current board KWS contract is `98x40` streaming log-mel
  - `/root/river-openwakeword-lab` remains the preferred external training lab
  - current exported student may require `PAD` support before runtime integration
- Executed the standard full build for the current `DS-CNN` branch and confirmed the build baseline is healthy.
- Verified current top-level image artifacts after the successful build:
  - `build_RTL8730E/km4_boot_all.bin`: `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin`: `3605856`
  - `build_RTL8730E/ota_all.bin`: `3605888`

## Step 5.5
- Completed `Phase 1: Build Result Review` on branch `DS-CNN` after the successful full build verification.
- High-signal review findings:
  - current image sizes are identical to the last verified `xiaozhi` full-build baseline, so there is no new binary-size inflation on this branch
  - `git diff --stat xiaozhi..DS-CNN` shows the branch currently differs only in documentation and planning files, not in runtime source code
  - the app image still exceeds the SDK stock NOR app range and therefore still depends on the project-owned development flash profile
  - current app placement math:
    - start address `0x08040000`
    - end address `0x083B0560`
    - overflow against stock `0x08300000`: `722272` bytes
  - the current KWS runtime op resolver still registers only:
    - `Quantize`
    - `Conv2D`
    - `DepthwiseConv2D`
    - `Mean`
    - `FullyConnected`
    - `Logistic`
- Review conclusion:
  - the current `DS-CNN` branch is still a valid build-stable / flash-stable baseline as long as the project custom profile and `tools/river_flash.py` are used
  - future migration of student assets from `/root/river-openwakeword-lab` must first solve the op-compatibility gate if the imported model requires `PAD`
- Updated `plan.md` accordingly:
  - `Phase 1` marked completed
  - `Phase 2` marked current, with the explicit rule that only minimal runtime integration is allowed from this baseline

## Step 5.6
- Completed `Phase 2: Runtime Integration Decision` on branch `DS-CNN` and chose the lowest-risk landing path:
  - keep the existing baseline embedded KWS model as a selectable fallback
  - import the `/root/river-openwakeword-lab` round6 targeted DS-CNN student as the default experimental model for this branch
  - solve only the minimum runtime compatibility gap needed for that student
- Added KWS model-variant selection to `Kconfig`:
  - `RIVER_KWS_MODEL_VARIANT_BASELINE`
  - `RIVER_KWS_MODEL_VARIANT_ROUND6_TARGETED_EXPERIMENTAL`
- Set `prj.conf` on branch `DS-CNN` to select:
  - `CONFIG_RIVER_KWS_MODEL_VARIANT_ROUND6_TARGETED_EXPERIMENTAL=y`
- Imported the exported round6 targeted student into project-owned generated assets:
  - `components/river_voice/generated/xiaou_student_round6_targeted_int8_model_data.h`
  - `components/river_voice/generated/xiaou_student_round6_targeted_int8_metadata.json`
- Updated `components/river_voice/river_voice_kws.cc` so the board runtime now:
  - conditionally selects baseline vs round6 student at compile time
  - registers `AddPad()` for the experimental student path
  - increases KWS resolver capacity from `6` to `7`
  - logs the active model variant string at runtime
- Full `RTL8730E` build passed with the experimental student selected by default.
- Verified landing results:
  - the final firmware now contains the string `round6_targeted_experimental`
  - image sizes became:
    - `km4_boot_all.bin`: `51872`
    - `km0_km4_ca32_app.bin`: `3573088`
    - `ota_all.bin`: `3573120`
  - compared with the previous baseline build, the app image shrank by `32768` bytes because the imported experimental student is smaller than the old embedded model
  - despite the size reduction, the app image still exceeds the SDK stock NOR app range, so this branch still depends on the project-owned flash profile and `tools/river_flash.py`
- Explicit decision retained in docs:
  - this is an engineering smoke / board-validation landing, not a deploy-ready model-quality decision

## Step 5.7
- Reorganized repository-level Markdown entrypoints to reduce root-directory clutter on branch `DS-CNN`.
- Created `doc/` as the new home for summary, report, design, migration-review, and historical note files.
- Moved the following classes of root docs into `doc/`:
  - migration assessments
  - architecture / refactor proposals
  - frontend / KWS / AEC / VAD chain notes
  - historical milestone and pitfall summaries
  - project status snapshots
- Kept ongoing root entry files in place:
  - `README.md`
  - `plan.md`
  - `build.md`
  - `AGENTS.md`
  - `TIPS.md`
  - `REVIEW.md`
- Added `doc/README.md` as the index for the relocated report/archive documents.
- Refreshed ongoing root docs to match the current `DS-CNN` branch state:
  - `README.md` now reflects the current branch, DS-CNN runtime objective, validated image sizes, flash constraints, and the new `doc/` layout
  - `plan.md` now points to the migration reports under `doc/`
  - `build.md` now explicitly documents that the current app image still requires the project-owned flash profile even when using the official GUI tool
- Refreshed `doc/PROJECT_STATUS_ZH.md` so it no longer describes the old `xiaozhi` branch as the current baseline and instead captures the present `DS-CNN` branch status.
- Chose not to delete any relocated summary Markdown in this step because each moved file still has traceability or comparison value; only location and indexing were cleaned up.

## Step 5.8
- Tightened the `DS-CNN` branch around the intended runtime business flow:
  - local wake word
  - XiaoZhi realtime session after wake
  - VAD-assisted audio bridge
  - no local online text/TTS debug injection compiled by default
- Added two compile-time gates in `Kconfig`:
  - `RIVER_CLOUD_TEXT_DEBUG_EN`
  - `RIVER_INTERACTION_DIAG_EN`
- Set both gates to `n` in `prj.conf` so the default `DS-CNN` branch firmware no longer carries:
  - direct `river echo` / `river tts` cloud text/TTS debug injection
  - local `river interaction ...` text router / tts test / deferred-tts worker
- Switched `components/river_core/CMakeLists.txt` to compile either:
  - `river_interaction_diag.c` when enabled
  - `river_interaction_diag_stub.c` when disabled
- Added `components/river_core/river_interaction_diag_stub.c` so the runtime interface remains stable while the heavy local interaction debug implementation is excluded from the image.
- Updated `components/river_diag/river_diag_cmd.c` so debug-only monitor commands are compile-gated:
  - `river echo`
  - `river tts`
  - `river interaction ...`
- Kept board-useful commands intact:
  - `river status`
  - `river xiaozhi ...`
  - `river playback ...`
  - `river audio ...`
  - `river device ...`
- Updated `components/river_cloud/river_online_control.c` so:
  - device control remains available for XiaoZhi MCP tool execution
  - `river_online_control_echo()` becomes a stub when cloud text debug is disabled
  - status logs show whether text debug was compiled or stubbed
- Verified the full `RTL8730E` build after the gating changes.
- Measured size impact:
  - `build_RTL8730E/km0_km4_ca32_app.bin` shrank from `3573088` to `3564896`
  - `build_RTL8730E/ota_all.bin` shrank from `3573120` to `3564928`
  - app delta for this step: `-8192` bytes
- Measured representative object-level impact:
  - `river_interaction_diag.o` was about `80K`; replaced by `river_interaction_diag_stub.o` about `7.8K`
  - `river_diag_cmd.o` dropped from about `35K` to about `29K`
- Updated `README.md` and `plan.md` so the current repo entrypoints now reflect:
  - the wake -> XiaoZhi realtime target flow
  - the VAD-assisted runtime expectation
  - the new debug-gating state and current image sizes

## Step 5.9
- Checked whether the repository still contains a backup file named `plan.md.bk` before deciding whether to merge or delete it.
- Verified that:
  - `/root/ameba-river/plan.md.bk` does not exist
  - a full repository search found no `plan.md.bk` and no relevant `*.bk` backup artifact
  - `git log --all --name-only -- plan.md.bk` returned no history, so this backup file is not part of the current tracked repository history
- Conclusion:
  - there is no in-repo `plan.md.bk` left to merge or delete
  - the current authoritative execution plan remains `plan.md`
  - the latest `plan.md` already subsumes the useful planning state:
    - DS-CNN experimental runtime landing
    - wake -> XiaoZhi realtime target flow
    - VAD-assisted audio bridge
    - compile-time gating for online text/TTS debug injection

## Step 5.10
- Isolated the board crash investigation away from the experimental DS-CNN student model by switching the default KWS embedded variant back to the repository baseline model in `prj.conf`.
- Kept the product path unchanged for this step:
  - local KWS still enabled
  - VAD probe still active
  - wake -> XiaoZhi realtime path still intact
  - only the embedded wake-word model variant changed
- Verified the generated build configs now select:
  - `CONFIG_RIVER_KWS_MODEL_VARIANT_BASELINE=y`
  - `# CONFIG_RIVER_KWS_MODEL_VARIANT_ROUND6_TARGETED_EXPERIMENTAL is not set`
- Verified the built CA32 image now embeds `baseline_embedded` instead of `round6_targeted_experimental`.
- Built `RTL8730E` successfully after the switch.
- Captured updated image sizes after the isolation change:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3597664`
  - `build_RTL8730E/ota_all.bin` = `3597696`
- Compared with the prior experimental-model build:
  - app image increased by `32768` bytes
  - this size increase is expected for this isolation step and is smaller risk than continuing to ship the model variant currently implicated by the crash trace
- Current conclusion:
  - the next board flash is now a high-signal A/B check
  - if the crash disappears with `baseline_embedded`, the experimental round6 model/runtime combination is the primary suspect
  - if the crash persists, investigation should continue inside the general KWS runtime path rather than in XiaoZhi session logic

## Step 5.11
- Reduced the XiaoZhi downlink playback buffer sizing again to lower CA32 heap pressure when TTS playback starts.
- Updated [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h):
  - `RIVER_CLOUD_XIAOZHI_PLAYBACK_BUFFER_FRAMES: 6 -> 3`
  - `RIVER_CLOUD_XIAOZHI_PLAYBACK_BUFFER_FRAMES_FALLBACK: 4 -> 2`
- Left the rest of the XiaoZhi realtime path unchanged:
  - wakeword admission
  - websocket/session setup
  - uplink audio framing
  - downlink ring depth and playback start threshold
- Rationale:
  - the latest successful wake -> XiaoZhi session log showed the new blocker is not recognition or websocket setup
  - the failure moved to CA32 playback start, with `river_xz_down` hitting `Malloc failed ... xWantedSize:46144`
  - the playback service multiplies the track minimum buffer by `buffer_frame_count`, so shrinking these constants is the direct low-risk lever for this allocation
- Verified a full `RTL8730E` build after the buffer reduction.
- Current packaged image sizes remained:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3597664`
  - `build_RTL8730E/ota_all.bin` = `3597696`
- Expected runtime effect:
  - first playback attempt now uses about half the prior frame multiplier
  - compact fallback is now tighter again if the first playback open still cannot fit

## Step 5.12
- Added a new execution-oriented refactor document at `doc/PROJECT_REFACTOR_EXECUTION_PLAN_ZH.md`.
- This document is intentionally different from the existing architecture blueprints:
  - it is based on the current `DS-CNN` codebase and recent board/runtime logs
  - it prioritizes work by `correctness -> memory/hot path -> boundaries -> size/clean code`
  - it turns the current review findings into phased, stepwise work that can be committed and verified incrementally
- Captured the current top-priority refactor targets:
  - wakeword deferred admission should become retryable instead of lossy
  - time-ready semantics should be unified
  - XiaoZhi uplink should stop allocating per packet on the hot path
  - playback and pre-roll memory budgeting should become explicit
  - `river_voice -> river_cloud` direct dependencies should be removed in later phases
  - the current large files should be split by façade / policy / runtime / diagnostics responsibility
- Updated `doc/README.md` so the new refactor execution plan is discoverable from:
  - `当前优先阅读`
  - `架构与实现`
- Kept this step documentation-only on purpose:
  - no source code behavior changed
  - no firmware image content changed
  - this step exists to establish the refactor baseline before rewriting `plan.md`

## Step 5.13
- Rewrote the root `plan.md` so it now reflects the actual branch objective: staged refactoring of the current runtime rather than the older migration/smoke narrative.
- The new `plan.md` now aligns with `doc/PROJECT_REFACTOR_EXECUTION_PLAN_ZH.md` and captures:
  - the current baseline chain
  - current runtime/architecture risks
  - explicit guardrails for refactor steps
  - a phased execution order from correctness to memory to boundaries to size cleanup
  - the immediate next code step: wake admission retry plus time-ready semantics cleanup
- Removed outdated guidance from the old root plan that no longer matched the current branch state:
  - experimental model migration framing
  - old smoke-first execution path
  - stale phase history that had already served as historical record in `.codex`
- Kept this step documentation-only:
  - no runtime source changed
  - no binary content changed
  - this step exists to make the root project entrypoint consistent before starting code refactors

## Step 5.14
- Completed the first code refactor step under `Phase 1`, focused on wake admission correctness rather than structural file splitting.
- Updated `components/river_core/river_session_coordinator.c` so wakeword admission is no longer lossy when the cloud side is temporarily unavailable:
  - a pending wakeword is no longer cleared before admission succeeds
  - `RIVER_ERR_BUSY` now keeps the wake event pending and retries from the wake worker every `250 ms`
  - non-retryable admission failures still clear the pending wake and log an error
  - deferred admission logging is rate-limited so retries do not spam the log on every poll
- Updated `components/river_cloud/river_cloud_adapter.c` and `components/river_cloud/river_cloud_internal.h` to make the cloud-side time semantics explicit for this path:
  - `river_cloud_time_ready()` remains the strict system-time / SNTP-ready gate
  - new internal `river_cloud_wake_admission_time_ready()` uses the best available wake-admission time source, including build-seeded UTC estimate
  - added a deduplicated `wake admission deferred` log path that reports:
    - `wifi`
    - `admission_time_ready`
    - `system_time_ready`
- Updated `components/river_cloud/river_cloud_xiaozhi_session.c` so XiaoZhi wake admission now:
  - starts SNTP and seeds build time as before
  - allows wake admission to proceed when build-seeded UTC estimate is available even if system time is not yet SNTP-ready
  - logs once when wake admission proceeds using the build-seeded UTC estimate
  - resets the deferred-state tracker after successful admission or network lifecycle changes
- Practical effect of this step:
  - a wakeword hit that happens before SNTP convergence should no longer be dropped permanently
  - the board can now hold the wake event and enter XiaoZhi once the transient busy condition clears
  - logs now make it explicit whether the blocker was Wi-Fi, strict system time, or whether wake admission proceeded on the build-seeded estimate
- Verified a full local `RTL8730E` build after the change.
- Image sizes after this step remained:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3597664`
  - `build_RTL8730E/ota_all.bin` = `3597696`
- Image size remained unchanged because this step only reworked existing control flow and logging; it did not add new large runtime assets.

## Step 5.15
- Completed the next `Phase 1` hot-path cleanup in the XiaoZhi transport layer, scoped narrowly to per-packet heap churn on uplink websocket binary framing.
- Updated [components/river_cloud/river_xiaozhi_ws.c](/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c) so `river_xiaozhi_send_binary_frame()` no longer calls `rtos_mem_malloc/free` for protocol `v2/v3` audio frames:
  - added a long-lived transport scratch buffer in `g_river_xiaozhi`
  - reused that buffer for websocket binary frame header + payload assembly
  - kept raw-payload passthrough behavior for non-`v2/v3` protocol handling
- Added an explicit framed-payload size guard tied to the current project contract:
  - max framed XiaoZhi uplink payload is now enforced as `512B`
  - oversize payloads fail fast with `binary_payload_too_large` instead of falling into hidden heap growth or fragmentation
- Why this step matters:
  - XiaoZhi uplink audio runs on a steady hot path
  - per-frame heap allocation here adds avoidable fragmentation and latency risk on embedded targets
  - this keeps the optimization local to the websocket framing layer before later memory-budget work on playback/downlink
- Verified a full local `RTL8730E` build after the change.
- Image sizes after this step remained:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3597664`
  - `build_RTL8730E/ota_all.bin` = `3597696`
- Image size remained unchanged because the step replaces transient heap usage with a small fixed in-context scratch buffer inside already-allocated runtime state.

## Step 5.16
- Completed the next `Phase 1` memory-budget cleanup on the playback path, focused on the XiaoZhi downlink/TTS startup failure that previously attempted a `~46KB` AudioTrack allocation in `river_xz_down`.
- Updated [components/river_voice/river_playback_service.c](/root/ameba-river/components/river_voice/river_playback_service.c) so playback buffer sizing now matches the service API semantics:
  - `buffer_frame_count` is treated as the target number of application playback frames
  - `AudioTrack_GetMinBufferBytes()` is treated as the SDK minimum whole-track buffer budget
  - final track buffer bytes now use:
    - `max(min_buffer_bytes, playback_frame_bytes * buffer_frame_count)`
  - the old behavior incorrectly multiplied the SDK minimum buffer by `buffer_frame_count`, which over-allocated playback memory when `min_buffer_bytes` was already larger than a single app frame
- Added a startup log for playback streams so the actual runtime budget is now visible on board:
  - stream name
  - sample rate / frame duration
  - per-frame bytes
  - SDK `min` bytes
  - application `target` bytes
  - final `track` bytes
  - whether playback reference export is enabled
- Updated [include/river/river_playback_service.h](/root/ameba-river/include/river/river_playback_service.h) to document the intended meaning of `buffer_frame_count`, so the service contract is explicit in code rather than hidden in implementation.
- Why this step matters:
  - the previous XiaoZhi downlink failure aligned with `15360B * 3 = 46080B`, meaning the service was tripling the SDK minimum whole-track buffer
  - this change preserves the SDK minimum while removing the extra amplification
  - the fix is generic for all playback-service users, but it directly targets the observed XiaoZhi downlink memory failure
- Verified a full local `RTL8730E` build after the change.
- Image sizes after this step remained:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3597664`
  - `build_RTL8730E/ota_all.bin` = `3597696`
- Image size remained unchanged because this step only corrected runtime buffer sizing math and logging; it did not add new assets or large static buffers.

## Step 5.17
- Added a temporary board-bring-up tuning step to make wake-word validation much easier while the current embedded KWS model is still weak.
- Updated [prj.conf](/root/ameba-river/prj.conf) only:
  - lowered `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15` from `21299` to `8192`
  - added an inline comment marking this as a temporary permissive threshold for board-side wake-path validation
- Kept scope intentionally narrow:
  - no KWS runtime code changed
  - no model asset changed
  - no hold/cooldown/stride/gate queue parameter changed
- Why this step matters:
  - current field logs show wake hits are possible but the model is not robust enough for comfortable board-side iteration
  - lowering the primary score threshold also lowers the fallback gate threshold automatically through the existing runtime derivation in `river_voice_kws.cc`
  - this gives a fast bring-up path for validating wake -> XiaoZhi connect -> ASR/TTS session flow before spending more time on model quality
- Verified a full local `RTL8730E` build after the config change.
- Image sizes after this step remained:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3597664`
  - `build_RTL8730E/ota_all.bin` = `3597696`
- Image size remained unchanged because this step only adjusts a config threshold and does not alter runtime assets or large static allocations.

## Step 5.18
- Added concise Chinese comments across the project-owned source tree to improve code navigation without turning the code into comment noise.
- Scope of this step:
  - covered all manually maintained `*.c` / `*.cc` / `*.h` files under:
    - `app/`
    - `components/river_common`
    - `components/river_core`
    - `components/river_diag`
    - `components/river_cloud`
    - `components/river_voice`
    - `include/river`
  - excluded:
    - `components/river_voice/generated/*`
    - `CMakeLists.txt`
    - SDK code under `/root/ameba-rtos-1.2`
- Comment style used in this step:
  - file-level Chinese summaries for each project-owned source/header file
  - a small number of focused inline comments on non-obvious orchestration paths
  - no line-by-line explanatory noise on self-explanatory code
- Practical effect:
  - each module now states its responsibility at file entry
  - internal/private headers and public headers are easier to scan when tracing responsibilities across `core`, `cloud`, and `voice`
  - complex boot/log/diag dispatch paths now have a few explicit intent comments where they help most
- Verified a full local `RTL8730E` build after the comment-only change.
- Image sizes after this step remained:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3597664`
  - `build_RTL8730E/ota_all.bin` = `3597696`
- Image size remained unchanged because this step only adds source comments and does not alter compiled behavior or runtime assets.

## Step 5.19
- Fixed the next XiaoZhi runtime memory failure on the uplink path after wake/session open had already succeeded.
- Observed board failure before this step:
  - websocket connect succeeded
  - `server hello` arrived
  - ASR streaming entered
  - then `river_xz_up` failed with:
    - `Malloc failed ... [xWantedSize:8320]`
    - followed by `INIC-E WIFI TRX IPC 4 timeout`
- Root cause:
  - XiaoZhi websocket transport was created with `RIVER_XIAOZHI_WS_TX_MAX=8192`
  - Ameba SDK `wsclient` dynamically allocates `tx_buf_len + 16` per queued send buffer item
  - under low free heap, the next uplink queue expansion in `river_xz_up` tried to allocate another `~8 KB` send buffer and failed
- Updated [include/river/river_xiaozhi_credentials.h](/root/ameba-river/include/river/river_xiaozhi_credentials.h) only:
  - lowered `RIVER_XIAOZHI_WS_TX_MAX` from `8192` to `1024`
  - lowered `RIVER_XIAOZHI_WS_QUEUE_MAX` from `16` to `4`
  - documented why the reduced values are still sufficient for the current project contract:
    - uplink Opus/binary frames are already bounded well below `512 B` payload plus framing
    - current hello/listen/abort JSON and MCP envelopes remain within the reduced headroom
- Why this step matters:
  - this directly removes the exact `~8 KB` dynamic allocation class seen in the crash log
  - it also caps worst-case websocket send-queue growth so heap pressure fails earlier and smaller instead of fragmenting late
  - scope stays narrow because no cloud/session logic or protocol payload shape was changed
- Verified a full local `RTL8730E` build after the config change.
- Image sizes after this step remained:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3597664`
  - `build_RTL8730E/ota_all.bin` = `3597696`
- Image size remained unchanged because this step only tightens runtime websocket buffer limits and does not add code or assets.

## Step 5.20
- Fixed the next XiaoZhi follow-up failure mode after the earlier uplink heap fix.
- Observed board failure before this step:
  - wake and websocket connect succeeded
  - ASR stream entered
  - later logs showed capture-side backlog and transport send pressure:
    - `capture frame ring overflow: dropped=...`
    - `ws_sendData: ERROR: Not get usable buffer, Please enlarge max_queue_size!`
    - `xiaozhi uplink send failed: status=-6`
- Root cause addressed in this step:
  - the XiaoZhi cloud path still allowed follow-up reopening logic to run from the VAD/audio processing thread
  - if the websocket transport had already been closed or become unavailable, that thread could fall into a synchronous reopen path at the wrong layer
  - this is exactly the kind of stall that starves capture consumption and turns into `capture frame ring overflow`
  - keeping the conversation window open after transport loss also allowed stale follow-up state to keep pushing toward the websocket queue instead of failing closed
- Updated project code in:
  - [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c)
  - [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
  - [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h)
- What changed:
  - added a local-only `xiaozhi` conversation-window abort helper that resets window/follow-up state without re-entering websocket close logic
  - this keeps the websocket close callback path deadlock-safe while still letting the cloud layer fail closed immediately on transport loss
  - on `RIVER_XIAOZHI_EVENT_SESSION_CLOSED`, the cloud adapter now:
    - logs the transport-close context
    - aborts the local conversation window first
    - emits the ASR `session_closed` signal with the corrected interaction-state ordering
    - clears cached uplink/downlink/session state so stale follow-up traffic does not linger
  - during follow-up auto-open, if the transport is already unavailable, the VAD path now aborts the window and returns `RIVER_ERR_BUSY` instead of trying to synchronously reconnect from the audio thread
- Why this step matters:
  - it prevents transport recovery from being driven by the real-time audio/VAD path
  - it removes the state leak where a dead XiaoZhi websocket could leave follow-up logic active long enough to back up capture and websocket send queues
  - it preserves the intended explicit recovery path:
    - first wakeword opens a session from the wake worker
    - stale follow-up does not try to do network recovery from the VAD worker
- Verified a full local `RTL8730E` build after the fix.
- Image sizes after this step remained:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3597664`
  - `build_RTL8730E/ota_all.bin` = `3597696`
- Image size remained unchanged because this step only tightens state/flow control and does not add assets or enlarge static buffers.

## Step 5.21
- Landed the dedicated BC-ResNet wake-word replacement plan before touching runtime code.
- Added [doc/KWS_BC_RESNET_REPLACEMENT_PLAN_ZH.md](/root/ameba-river/doc/KWS_BC_RESNET_REPLACEMENT_PLAN_ZH.md) to record:
  - why `bc_resnet_best.tflite` cannot be dropped in directly
  - the exact two runtime blockers:
    - missing `Add` op registration
    - input layout mismatch between current `98x40x1` write order and BC-ResNet `40x98x1`
  - the chosen minimal-change replacement strategy
  - validation focus and remaining risks
- Updated both plan trackers:
  - [plan.md](/root/ameba-river/plan.md)
  - [.codex/plan.md](/root/ameba-river/.codex/plan.md)
- Why this step matters:
  - it freezes the replacement contract before code churn starts
  - it prevents the runtime implementation from drifting into ad hoc model-specific fixes
- it makes the next implementation commit auditable against a concrete plan
- No product code or embedded assets changed in this step.

## Step 5.22
- Replaced the current baseline embedded wake-word asset with `bc_resnet_best.tflite` from `kws-training-pro`.
- Updated [components/river_voice/generated/river_wake_word_model_data.h](/root/ameba-river/components/river_voice/generated/river_wake_word_model_data.h):
  - regenerated the baseline-slot model data from `models/bc_resnet_ultra/bc_resnet_best.tflite`
  - kept the existing symbol names `kws_model` and `kws_model_len`
  - new embedded model size is `56024B`
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - increased `RIVER_KWS_OP_COUNT` from `7` to `8`
  - registered `Add` in the TFLM resolver so BC-ResNet residual paths are supported
  - added model-driven input-shape parsing with schema-first, runtime-fallback behavior
  - accepted both board-supported layouts:
    - `[1, 98, 40, 1]`
    - `[1, 40, 98, 1]`
  - added direct `mels_frames` write order support so BC-ResNet does not need an extra transpose buffer
  - updated profile logs to print the real embedded input shape and active variant name
- Updated [components/river_voice/river_voice_frontend.c](/root/ameba-river/components/river_voice/river_voice_frontend.c):
  - changed the wake-stage validation log from model-specific `dscnn_kws` wording to neutral `local_kws`
- Why this step matters:
  - the runtime is no longer hard-wired to a single `98x40x1` model layout
  - BC-ResNet can now be loaded without resolver failure on missing `Add`
  - the model replacement stayed confined to the existing baseline slot without expanding Kconfig complexity
- Verified a full local `RTL8730E` build after the replacement.
- New image sizes after this step:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3593568`
  - `build_RTL8730E/ota_all.bin` = `3593600`
- Image size decreased relative to the previous baseline because the BC-ResNet asset is smaller than the previous embedded wake-word model.

## Step 5.23
- Added a project knowledge note for the resolved WSL2 flashing issue:
  - [knowledge/WSL2_FLASH_TROUBLESHOOTING_ZH.md](/root/ameba-river/knowledge/WSL2_FLASH_TROUBLESHOOTING_ZH.md)
- Captured the concrete conclusion from this incident:
  - `0xE8 Address error` was not caused by the current firmware size exceeding the project flash profile
  - the real risk area is WSL2 serial-device visibility and whether the project-owned flash profile is actually being used
- Documented the verified size facts used during diagnosis:
  - current `km0_km4_ca32_app.bin` is `3593568B`
  - current `ota_all.bin` is `3593600B`
  - both still fit inside the project app range `0x08040000-0x08600000`
- Recorded the WSL2-specific operational guidance:
  - do not assume `COM3 -> /dev/ttyS2` is usable just because the numbering matches
  - prefer `usbipd-win` plus `/dev/ttyUSB0` or `/dev/ttyACM0`
  - always flash through `python3 tools/river_flash.py ...`
- This step is documentation-only and does not change firmware code or binary assets.

## Step 5.24
- Tightened XiaoZhi idle-admission policy so an enabled KWS build remains wakeword-gated even if the local KWS runtime is temporarily inactive.
- Updated [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c):
  - `river_cloud_xiaozhi_idle_requires_wakeword()` now keys off build capability, not runtime `river_voice_kws_active()`
  - this removes the legacy fallback where idle pure-VAD speech could reopen XiaoZhi from the audio path when local KWS init/runtime was down
- Updated [components/river_voice/river_voice_frontend.c](/root/ameba-river/components/river_voice/river_voice_frontend.c):
  - corrected the boot warning text to state that idle VAD admission stays wakeword-gated while local KWS is inactive
  - this keeps logs aligned with the new policy instead of falsely claiming that legacy VAD admission remains enabled
- Why this step matters:
  - fixes the observed runtime where a single speech burst in `wake_monitoring` repeatedly printed:
    - `xiaozhi conversation window aborted: reason=followup_transport_unavailable`
  - prevents pure VAD from repeatedly trying to open a XiaoZhi follow-up session before any real wakeword/session window exists
  - keeps the intended contract intact:
    - first turn must come from wakeword admission
    - follow-up auto-open only happens inside an already-opened conversation window
- Verified a full local `RTL8730E` build after the fix.
- Image sizes after this step remained:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3593568`
  - `build_RTL8730E/ota_all.bin` = `3593600`

## Step 5.25
- Fixed the current `bc_resnet` wake path boot failure by increasing the KWS tensor arena in [prj.conf](/root/ameba-river/prj.conf):
  - `CONFIG_RIVER_KWS_TENSOR_ARENA_KB` from `160` to `192`
- Why this step was necessary:
  - the board log showed KWS never initialized after boot:
    - `Failed to resize buffer. Requested: 159744, available 152920, missing: 6824`
    - `kws AllocateTensors failed: arena=160KB model=56024B`
  - once `AllocateTensors` fails, local KWS is inactive, so no wakeword can ever be detected and XiaoZhi will never enter the websocket connect path from idle wake monitoring
- Why this is the minimal fix:
  - no SDK source was modified
  - no model asset or operator set was changed in this step
  - the failure was a straightforward tensor-arena capacity miss after the model swap, so the first correction is to size the arena for the real model footprint
- Verified a full local `RTL8730E` build after the config bump.
- Image sizes after this step remained:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3593568`
  - `build_RTL8730E/ota_all.bin` = `3593600`

## Step 5.26
- Fixed the `bc_resnet_best` wake path crash that happened on the first real KWS inference after `kws gate open`.
- Added project-side patched quantized `MEAN` support:
  - [components/river_voice/river_voice_kws_mean_patch.h](/root/ameba-river/components/river_voice/river_voice_kws_mean_patch.h)
  - [components/river_voice/river_voice_kws_mean_patch.cc](/root/ameba-river/components/river_voice/river_voice_kws_mean_patch.cc)
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - include the patched `MEAN` registration
  - replace the fixed `MicroMutableOpResolver` usage with a local resolver that can register the project-side patched builtin `MEAN`
  - keep the rest of the BC-ResNet operator set unchanged
- Updated [components/river_voice/CMakeLists.txt](/root/ameba-river/components/river_voice/CMakeLists.txt):
  - build the new patched `MEAN` source only when KWS capability is enabled
- Why this step was necessary:
  - the board log showed KWS boot was successful, but the first real inference crashed immediately after:
    - `kws gate open: ...`
    - `Data abort with Data Fault Status Register 0x00001a11`
  - symbolication placed the fault inside TensorFlow Lite Micro quantized `MEAN`
  - the failing CA32 store target was unaligned, so the safest fix was to keep SDK sources untouched and replace only the project's KWS-side `MEAN` registration path
- Why this is the chosen fix:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no wakeword model asset was changed again
  - the patch is scoped to the known BC-ResNet reduction patterns used by the current model
- Verified a full local `RTL8730E` build after the fix.
- Image sizes after this step:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3601760`
  - `build_RTL8730E/ota_all.bin` = `3601792`

## Step 5.27
- Added a training-side KWS export contract for future embedded-friendly wakeword models:
  - [knowledge/KWS_MODEL_EXPORT_CONTRACT_ZH.md](/root/ameba-river/knowledge/KWS_MODEL_EXPORT_CONTRACT_ZH.md)
- This document is intended to be handed directly to the model-training team.
- The contract makes the deployment boundary explicit:
  - fixed input/output expectations for `Ameba River`
  - int8-only export requirement
  - operator whitelist and blacklist
  - explicit prohibition on post-export graph rewriting
  - required delivery report fields and acceptance gates
- Why this step matters:
  - the current `bc_resnet_best.tflite` is usable, but it forced the firmware side to add a patched `MEAN`
- long-term binary size and maintenance are better served by a native export type that avoids `MEAN`
- this reduces ambiguity when handing requirements to the training side
- This step is documentation-only and does not change firmware code or binary assets.

## Step 5.28
- Fixed the boot-time KWS init regression introduced by the project-side patched `MEAN` op.
- Updated [components/river_voice/river_voice_kws_mean_patch.cc](/root/ameba-river/components/river_voice/river_voice_kws_mean_patch.cc):
  - simplified `river_voice_kws_mean_patch_prepare()`
  - removed the project-side tensor prevalidation from `prepare`
  - delegated `prepare` directly to SDK `tflite::PrepareMeanOrSumHelper(...)`
- Why this step was necessary:
  - the latest board boot log proved KWS never initialized, so wakeword could never trigger:
    - `axis->type != kTfLiteInt32 (0 != 2)`
    - `Node MEAN ... failed to prepare`
    - `kws AllocateTensors failed: arena=192KB model=56024B`
  - this failure happened in the patched op `prepare` path before any real KWS inference started
- Why this is the minimal fix:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no model asset was changed again
  - the project keeps the scoped patched `MEAN` eval path, but reuses the SDK's compatible `prepare` logic instead of trying to duplicate it locally
- Verified a full local `RTL8730E` build after the fix.
- Image sizes after this step remained:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3601760`
  - `build_RTL8730E/ota_all.bin` = `3601792`

## Step 5.29
- Fixed the next KWS runtime failure after boot init recovered: patched `MEAN` eval no longer depends on `node->builtin_data` staying valid.
- Updated [components/river_voice/river_voice_kws_mean_patch.cc](/root/ameba-river/components/river_voice/river_voice_kws_mean_patch.cc):
  - introduced a project-local patched op-data wrapper around `tflite::OpDataReduce`
  - cached `ReducerOptions.keep_dims` during `init`
  - switched `prepare` to pass the embedded `OpDataReduce` to SDK `PrepareMeanOrSumHelper(...)`
  - switched `eval` to use the cached `keep_dims` instead of reading `TfLiteReducerParams` from `node->builtin_data`
  - tightened axis resolution to reject reduce patterns with more than `2` axes instead of risking local buffer overwrite
- Why this step was necessary:
  - after Step `5.28`, the board log showed KWS could boot and open the gate, but actual inference still failed in the patched `MEAN` eval path:
    - `params != NULL was not true`
    - `Node MEAN ... failed to invoke`
    - `kws worker process failed: status=-6`
  - this proved the remaining blocker had moved from `prepare` to runtime `eval`
- Why this is the minimal fix:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no model asset was changed again
  - the patch keeps reusing SDK reduce prepare logic while only caching the single reducer flag that the project-side eval actually needs
- Verified a full local `RTL8730E` build after the fix.
- Image sizes after this step remained:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3601760`
  - `build_RTL8730E/ota_all.bin` = `3601792`

## Step 5.30
- Fixed the remaining KWS runtime failure where patched `MEAN` rejected the live `bc_resnet_best` reduce pattern as unsupported.
- Reviewed the embedded wakeword model topology from [components/river_voice/generated/river_wake_word_model_data.h](/root/ameba-river/components/river_voice/generated/river_wake_word_model_data.h):
  - all model `MEAN` nodes are normal reductions on axes `[2]`, `[1]`, or `[1,2]`
  - the blocker was not a new operator pattern in the model
  - the blocker was the project-side patch matching too narrowly on `keep_dims`
- Updated [components/river_voice/river_voice_kws_mean_patch.cc](/root/ameba-river/components/river_voice/river_voice_kws_mean_patch.cc):
  - changed patched `MEAN` eval matching from `axis + keep_dims` to `axis + output shape`
  - explicitly supports the observed model output layouts:
    - `axis=2` with rank-`4` output `[N,H,1,C]`
    - `axis=2` with rank-`3` output `[N,H,C]`
    - `axis=1` with rank-`4` output `[N,1,W,C]`
    - `axis=1` with rank-`3` output `[N,W,C]`
    - `axes=1,2` with rank-`4` output `[N,1,1,C]`
    - `axes=1,2` with rank-`2` output `[N,C]`
  - expanded the unsupported-pattern log so any remaining mismatch now prints:
    - `axes_len`
    - `axis0`
    - `axis1`
    - `keep_dims`
    - `in_rank`
    - `out_rank`
- Why this step was necessary:
  - the latest board log showed KWS could now boot and run, but each real inference still failed with:
    - `river kws mean patch got unsupported reduce pattern`
    - `Node MEAN (number 3) failed to invoke`
    - `kws worker process failed: status=-6`
  - model inspection showed op `3` is a standard `MEAN(axis=[2])`, so the patch needed to recognize the real output layout instead of rejecting it
- Why this is the minimal fix:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no model asset was changed again
  - the fix stays scoped to the project's patched KWS `MEAN` eval path
- Verified a full local `RTL8730E` build after the fix.
- Image sizes after this step remained:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3601760`
  - `build_RTL8730E/ota_all.bin` = `3601792`

## Step 5.31
- Fixed the next KWS runtime crash after Step `5.30`: patched `MEAN` op-data is now returned to TFLM on an explicitly aligned address.
- Updated [components/river_voice/river_voice_kws_mean_patch.cc](/root/ameba-river/components/river_voice/river_voice_kws_mean_patch.cc):
  - changed `river_voice_kws_mean_patch_init()` to over-allocate by `alignof(river_voice_kws_mean_patch_op_data) - 1`
  - manually aligned the returned `user_data` pointer before storing the patched `OpDataReduce`
  - kept the patched `MEAN` logic otherwise unchanged
- Why this step was necessary:
  - the March 31, 2026 board log showed the previous `unsupported reduce pattern` failure was gone, but the first real KWS inference still crashed with:
    - `Data abort with Data Fault Status Register 0x00000221`
    - fault PC `0x6035f448`
  - symbolication mapped the fault to [components/river_voice/river_voice_kws_mean_patch.cc](/root/ameba-river/components/river_voice/river_voice_kws_mean_patch.cc):194, where the patched int8 `MEAN` path loads `output_scale`
  - the board register dump showed `R11 = 0x6067b663`, and disassembly proved `R11` is the patched `node->user_data` pointer, so the crash was caused by a VFP float load from an unaligned op-data address
- Why this is the minimal fix:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no model asset was changed again
  - the change is scoped to the project-side patched `MEAN` init path and directly addresses the observed unaligned `user_data` root cause
- Verified a full local `RTL8730E` build after the fix.
- Image sizes after this step remained:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3601760`
  - `build_RTL8730E/ota_all.bin` = `3601792`

## Step 5.32
- Replaced the project-side patched `MEAN` eval loop with the SDK's own `tflite::EvalMeanHelper(...)`, while still keeping the project-local fix for missing reducer params.
- Updated [components/river_voice/river_voice_kws_mean_patch.cc](/root/ameba-river/components/river_voice/river_voice_kws_mean_patch.cc):
  - removed the project-side int8/int16 `MEAN` math loop
  - kept the explicitly aligned patched op-data allocation from Step `5.31`
  - expanded patched op-data from `OpDataReduce + keep_dims` to `OpDataReduce + TfLiteReducerParams`
  - cached the full reducer params during `init`
  - temporarily reattached cached `TfLiteReducerParams` to `node->builtin_data` during `eval`
  - delegated runtime execution to SDK `tflite::EvalMeanHelper(...)`
- Why this step was necessary:
  - the latest March 31 board logs proved the old `unsupported reduce pattern` issue was gone, but real KWS inference still crashed inside the project-side eval path:
    - `Data abort with Data Fault Status Register 0x00000221`
    - fault PC moved to the patched `MEAN` eval body (`0x6035f474`)
    - register dumps still showed `node->user_data` corruption symptoms during `MEAN`
  - model inspection also showed the live `MEAN` topology is standard and already supported by the SDK reduce kernel, so keeping a custom math loop was unnecessary risk
- Why this is the minimal fix:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no model asset was changed again
  - the project-side patch now only repairs reducer metadata lifetime/alignment and leaves actual `MEAN` execution to the upstream kernel
- Verified a full local `RTL8730E` build after the fix.
- Image sizes after this step changed to:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3593568`
  - `build_RTL8730E/ota_all.bin` = `3593600`

## Step 5.33
- Captured the full `KWS MEAN` bring-up pitfalls, confirmed facts, and next-step decision in a standalone project document instead of leaving them fragmented across monitor logs and commit history.
- Added [doc/KWS_MEAN_OPERATOR_POSTMORTEM_ZH.md](/root/ameba-river/doc/KWS_MEAN_OPERATOR_POSTMORTEM_ZH.md):
  - summarizes the observed failure sequence from `params != NULL was not true` to the final SDK-side `QuantizedMeanOrSum(...)` data abort
  - separates confirmed facts from high-confidence inference
  - records why the team should stop spending the main effort on old-model `MEAN` repair
  - documents the integration stance for the upcoming no-`MEAN` model
- Updated [doc/README.md](/root/ameba-river/doc/README.md):
  - indexed the new `KWS MEAN` postmortem so it remains discoverable as part of migration / review material
- Why this step was necessary:
  - the March 31 board logs proved the current patched / SDK-delegated `MEAN` path still dies inside upstream quantized reduce scratch handling, so the important next action is to preserve the lessons and switch strategy before integrating the next model
  - without one consolidated document, the exact traps are easy to repeat when the no-`MEAN` model lands
- Why this is the minimal fix:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no runtime code or model asset was changed again
  - this step only records hard-won troubleshooting knowledge and sets up the next branch cleanly

## Step 5.34
- Isolated non-mainline voice code behind explicit build switches so the prep branch stays closer to the upcoming no-`MEAN` product path, with less dead code and fewer debug-only strings in the default image.
- Updated [Kconfig](/root/ameba-river/Kconfig):
  - added `CONFIG_RIVER_AUDIO_ECHO_DEBUG_EN`, default `n`
  - made `CONFIG_RIVER_AUDIO_ECHO_AUTOSTART` and `CONFIG_RIVER_AUDIO_ECHO_DIAG_DEFAULT_ON` depend on the echo-debug switch
  - added `CONFIG_RIVER_KWS_MEAN_PATCH_EN`, default `n`, for legacy-model-only `MEAN` adaptation code
- Updated [prj.conf](/root/ameba-river/prj.conf):
  - explicitly disables board audio echo debug build
  - explicitly disables the dedicated `WebRTC AECM` experiment build
  - explicitly disables the temporary KWS `MEAN` patch in this prep branch
  - keeps comments explaining that this branch is preparing for the incoming no-`MEAN` model rather than preserving the old troubleshooting path
- Updated [components/river_voice/CMakeLists.txt](/root/ameba-river/components/river_voice/CMakeLists.txt):
  - builds `river_voice_echo.c` only when `CONFIG_RIVER_AUDIO_ECHO_DEBUG_EN=y`
  - otherwise builds [components/river_voice/river_voice_echo_stub.c](/root/ameba-river/components/river_voice/river_voice_echo_stub.c) to keep interfaces stable
  - builds true `WebRTC AECM` experiment sources only when `CONFIG_RIVER_WEBRTC_AECM_EXPERIMENT_EN=y`
  - keeps only the FFT pieces always compiled because KWS still depends on `real_fft.h` / related code
  - builds `river_voice_kws_mean_patch.cc` only when `CONFIG_RIVER_KWS_MEAN_PATCH_EN=y`
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - isolates the patched `MEAN` registration behind `CONFIG_RIVER_KWS_MEAN_PATCH_EN`
  - adds the normal SDK `Register_MEAN()` path as the default resolver behavior
- Updated [components/river_diag/river_diag_cmd.c](/root/ameba-river/components/river_diag/river_diag_cmd.c) and [components/river_voice/river_voice_frontend.c](/root/ameba-river/components/river_voice/river_voice_frontend.c):
  - hides echo-only help text, commands, and startup hints when echo debug is compiled out
  - keeps `river audio probe ...` available in the default build
- Updated [components/river_voice/river_voice_preproc_fixed_dsb.c](/root/ameba-river/components/river_voice/river_voice_preproc_fixed_dsb.c):
  - wraps `WebRTC AECM`-only helpers so the lighter default build does not drag experiment-only code through the mainline preproc path
- Why this step was necessary:
  - the branch objective has shifted from rescuing the old `MEAN` model to preparing a clean landing zone for a replacement model with no `MEAN` operator
  - keeping echo debug, temporary `MEAN` adaptation, and dedicated `AECM` experiment code in the default image made the binary larger and the default code path harder to read
  - the generated configs now prove all three prep-branch gates are off by default:
    - `# CONFIG_RIVER_AUDIO_ECHO_DEBUG_EN is not set`
    - `# CONFIG_RIVER_WEBRTC_AECM_EXPERIMENT_EN is not set`
    - `# CONFIG_RIVER_KWS_MEAN_PATCH_EN is not set`
- Why this is the minimal fix:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - debug / experiment code is not deleted; it is compiled only when explicitly re-enabled
  - the mainline interfaces stay stable through the echo stub and existing voice abstractions
- Verified a full local `RTL8730E` build after the isolation changes.
- Verified the current build no longer compiles `river_voice_kws_mean_patch.cc`.
- Image sizes after this step changed to:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3556704`
  - `build_RTL8730E/ota_all.bin` = `3556736`
- Relative to Step `5.32`, the main app images dropped by `36864` bytes.

## Step 5.35
- Migrated the prep branch to the new no-`MEAN` KWS model and tightened the default KWS resolver so the mainline image only carries operators required by the current embedded model.
- Updated [components/river_voice/generated/river_wake_word_model_data.h](/root/ameba-river/components/river_voice/generated/river_wake_word_model_data.h):
  - replaced the embedded baseline asset with `bc_resnet_epoch1_debug.tflite`
  - preserved the exported symbol names `kws_model` / `kws_model_len`
  - new embedded model size is `54104` bytes
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - changed the default embedded model variant name to `bc_resnet_epoch1_debug`
  - added `AVERAGE_POOL_2D` registration for the new model
  - split resolver capacity into:
    - always-on mainline ops for the no-`MEAN` model
    - legacy-only `MEAN` / `FULLY_CONNECTED` compatibility ops behind a build switch
  - kept the default build path free of legacy KWS compatibility registrations
- Updated [Kconfig](/root/ameba-river/Kconfig) and [prj.conf](/root/ameba-river/prj.conf):
  - added `CONFIG_RIVER_KWS_LEGACY_MODEL_COMPAT_EN`, default `n`
  - made `CONFIG_RIVER_KWS_MEAN_PATCH_EN` depend on the new legacy-compat switch
  - explicitly kept `CONFIG_RIVER_KWS_LEGACY_MODEL_COMPAT_EN=n` in the prep branch and left the `MEAN` patch path unreachable in the default build
- Why this step was necessary:
  - the new model removes the problematic `MEAN` op entirely, but the runtime still needed `AVERAGE_POOL_2D` support to boot the graph
  - after the branch objective shifted to the no-`MEAN` model, leaving legacy KWS op registrations in the default resolver would keep unused code paths and strings in the mainline image
  - isolating legacy KWS compatibility at build time keeps future troubleshooting options without polluting the default product path
- Why this is the minimal fix:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - the old-model compatibility path is not deleted; it is explicitly opt-in
  - the mainline runtime now matches the actual operator set of the embedded no-`MEAN` model
- Verified a full local `RTL8730E` build after the migration and resolver cleanup.
- Verified generated configs keep legacy KWS compatibility compiled out by default:
  - `# CONFIG_RIVER_KWS_LEGACY_MODEL_COMPAT_EN is not set`
- Verified the current default build does not compile `river_voice_kws_mean_patch.cc`.
- Verified board flash and serial boot after the migration:
  - KWS init completed with `dims=[1,40,98,1]`, output `values=1`, and `model=54104B variant=bc_resnet_epoch1_debug`
  - `audio_echo=compiled=no` still proves the earlier non-mainline voice isolation remains effective
  - Wi-Fi association, DHCP, and SNTP all completed normally after reboot
  - observed live KWS runtime with no legacy-op failures:
    - `kws gate open`
    - `kws gate close`
    - `wakeword hit: text=小欧管家`
    - `wakeword queued`
  - observed wakeword-to-cloud handoff still works:
    - `xiaozhi connecting`
    - `Connected to websocket server`
    - `server hello: sid=...`
  - no `Node MEAN ...`, no `unsupported reduce pattern`, and no data-abort signature appeared during boot and init observation
- Image sizes after this step are:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3560800`
  - `build_RTL8730E/ota_all.bin` = `3560832`
- Relative to Step `5.34`, the main app images increased by `4096` bytes because of the new embedded model and its required op set, while the default build still keeps legacy KWS compatibility code compiled out.

## Step 5.36
- Fixed the post-session capture overflow on the no-`MEAN` KWS prep branch by moving follow-up timeout teardown fully out of the real-time capture path.
- Updated [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c):
  - kept `river_cloud_xiaozhi_check_window_timeout()` owned by the dedicated `river_xz_pump` task
  - removed the same timeout check from `river_cloud_xiaozhi_stream_push_frame()`, which is called synchronously from `river_vad_probe`
  - documented why timeout-driven websocket/session teardown must not run in the capture hot path
- Root-cause summary:
  - user logs showed `capture frame ring overflow` growing at roughly one frame per `16 ms`, which matches `river_vad_probe` fully stalling rather than merely slowing down
  - the overflow started shortly after `asr_session_closed -> follow_up`, aligning with the follow-up timeout window rather than with KWS or Wi-Fi bring-up
  - the xiaozhi timeout path can close the websocket/session, and that transport work is not acceptable inside the frame-by-frame capture consumer path
- Why this is the minimal fix:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no protocol behavior was changed; only timeout-teardown ownership moved back to the existing pump task that already polls xiaozhi state
  - the real-time audio path keeps its existing logic and simply stops performing potentially blocking timeout teardown inline
- Verified a full local `RTL8730E` build after the change.
- Verified board flash and serial runtime after the fix:
  - booted and stayed in the normal `wake_monitoring` state
  - `river xiaozhi bootstrap` completed and populated the xiaozhi runtime config
  - `river xiaozhi connect` completed and reached `server hello: sid=...`
  - after `river xiaozhi listen start`, `river xiaozhi listen stop`, and `river xiaozhi disconnect`, the board remained healthy with no repeated `capture frame ring overflow`
  - `river status` still reported `capture_service=running ... queue=0/100 ... dropped=0`
- Image sizes after this step remain:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3560800`
  - `build_RTL8730E/ota_all.bin` = `3560832`

## Step 5.37
- Tightened the xiaozhi follow-up reopen path after a more precise root-cause pass on the persistent overflow log.
- Updated [components/river_cloud/river_xiaozhi_ws.c](/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c):
  - added explicit websocket timeout policy for the xiaozhi transport:
    - receive timeout `10000 ms`
    - send timeout `200 ms`
    - connect timeout `15000 ms`
    - send queue block time `200 ms`
  - applied these settings before `ws_connect_url()`
  - rationale: SDK websocket defaults leave `send_block_time=30000 ms` and no socket send timeout, which is unacceptable when reopen control frames share the same transport as buffered audio
- Updated [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c):
  - added a fast `RIVER_ERR_BUSY` guard while `xiaozhi_listen_stop_pending` is still true
  - this serializes `listen stop` completion and the next `listen start`, so follow-up speech cannot re-enter the reopen path while the previous stream is still draining
- Refined root-cause summary:
  - the failing user log does not match follow-up timeout expiry; it stalls about `1.6 s` after renewed speech, exactly when the `100`-frame capture ring fills
  - that means `river_vad_probe` stops consuming immediately after post-session speech begins
  - the most credible blocking point is synchronous reopen work (`listen start`) colliding with residual xiaozhi websocket send backlog from the just-closed stream
- Why this is the minimal fix:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - the transport contract did not change; only timeout bounds and stop/start serialization were added around the existing xiaozhi session flow
  - the real-time path now prefers bounded `BUSY` backpressure over unbounded blocking
- Verified:
  - full local `RTL8730E` build still succeeds
  - image sizes remain unchanged:
    - `build_RTL8730E/km4_boot_all.bin` = `51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin` = `3560800`
    - `build_RTL8730E/ota_all.bin` = `3560832`
- Verification blockers on the current bench:
  - after reboot, the board associated to `ORVIBO`, but `xiaozhi bootstrap` hit `gethostbyname` failure on that network, so the exact online wakeword/follow-up path could not be re-run end-to-end
  - an additional auto-enter-download-mode timeout prevented immediately reflashing the second refinement from the current shell session

## Step 5.38
- Hardened the no-`MEAN` KWS runtime tensor binding on the input side to keep the current prep branch in a checkpointable usable state before the next serial round.
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - added a typed tensor-data accessor based on `tensorflow/lite/kernels/internal/tensor_ctypes.h`
  - replaced direct reads of `tensor->data.data` with typed pointer resolution for init-time input/output data capture
  - added a narrow runtime resync path that refreshes only `interpreter->input(0)` and its backing buffer before filling the KWS input tensor
  - changed input fill to return status so inference exits cleanly if runtime input binding is invalid
- Root-cause summary for this step:
  - failing field logs showed `kws tensor data drift: runtime_input=0x25262627 ...`, which means the cached view of the runtime input binding could become stale or nonsensical after init
  - a broader attempt that also refreshed runtime tensors after `Invoke()` caused a CA32 data abort in this SDK, so this step intentionally limits resync to the pre-inference input side only
  - the output binding remains cached on purpose because the safer goal here is to eliminate obvious stale-input writes without reintroducing the post-`Invoke()` crash
- Why this is the minimal fix:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no KWS model contract was changed; this is runtime binding hardening only
  - the change is isolated to the no-`MEAN` KWS path and leaves the already-isolated non-mainline voice code untouched
- Verified locally:
  - full `RTL8730E` build succeeds
  - image sizes remain unchanged:
    - `build_RTL8730E/km4_boot_all.bin` = `51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin` = `3560800`
    - `build_RTL8730E/ota_all.bin` = `3560832`
- Board verification is deferred to the next user-driven serial session:
  - confirm no new data abort
  - confirm the old `kws tensor data drift` warning no longer appears
  - confirm wakeword score is no longer pinned at the previously observed `140 pm` failure mode

## Step 5.39
- Closed the board-verification loop for the recent no-`MEAN` KWS runtime hardening and reclassified the remaining bench issue away from KWS.
- Verified from user-provided serial logs on the tagged prep branch:
  - boot is stable with no CA32 `Data abort`
  - KWS metadata is correct at runtime:
    - `kws quant: ... zp=-3 ... zp=-128`
    - `kws input shape: ... dims=[1,40,98,1]`
    - `kws output shape: ... dims=[1,1,1,1]`
    - `model=54104B variant=bc_resnet_epoch1_debug`
  - the old stale-binding symptom is gone:
    - no `kws tensor data drift`
  - the old no-wake failure is gone:
    - `wakeword hit: text=小欧管家 score_pm=265 q15=8704`
  - the previous follow-up/open-path stall is also gone:
    - no `capture frame ring overflow`
    - no KWS queue saturation / fast-growing drop counters
    - follow-up and barge-in reopened ASR successfully multiple times
    - conversation window closed cleanly on `followup_timeout`
- Remaining issue after this verification:
  - TTS playback still shows intermittent `underrun`
  - one observed `xiaozhi playback write failed: mono=960B stereo=1920B` pushed the interaction state through `error_recovering`, although the system recovered automatically
- Conclusion of this step:
  - current branch is now board-proven as `KWS wake + xiaozhi session + follow-up reopen` usable
  - the next debug target is playback buffer / write scheduling under TTS, not KWS model migration, tensor binding, or xiaozhi follow-up reopen correctness

## Step 5.40
- Fixed a newly exposed intermittent CA32 crash in the no-`MEAN` KWS prep branch by removing runtime re-fetch of `interpreter->input(0)` from the KWS worker path.
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - stopped calling `river_voice_kws_sync_runtime_tensors()` during every inference input fill
  - kept the init-time typed tensor-data resolution, but made runtime inference use only the already-cached `input_tensor_data`
  - reduced `river_voice_kws_fill_input_tensor()` to a simple cached-pointer validity check
- Root-cause summary:
  - the new user crash log hit `0x60354ab8`, which resolves to `tflite::GetTensorData<float>(TfLiteTensor*)`
  - call chain:
    - `river_voice_kws_task()`
    - `river_voice_kws_sync_runtime_tensors()`
    - `interpreter->input(0)` / `GetTensorData<float>()`
  - the fault happened before wake, right after Wi-Fi association, proving the remaining unstable path was the runtime KWS worker polling `input(0)`, not follow-up reopen, TTS, or cloud control
  - this confirms the Ameba/TFLM port does not safely support repeated runtime `input(0)` access from the worker thread
- Why this is the minimal fix:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no model contract or thresholds were changed
  - the change only removes the unsafe runtime rebind path; init-time tensor discovery remains intact
- Verified locally:
  - full `RTL8730E` build succeeds
  - image sizes remain unchanged:
    - `build_RTL8730E/km4_boot_all.bin` = `51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin` = `3560800`
    - `build_RTL8730E/ota_all.bin` = `3560832`
- Next board verification target:
  - confirm the early `Data abort` after Wi-Fi connect no longer appears
  - confirm wake still works after removing the unsafe runtime rebind

## Step 5.41
- Fixed a follow-up window state-sync gap that could leave the board unable to wake again after the first completed xiaozhi turn.
- Updated [include/river/river_cloud.h](/root/ameba-river/include/river/river_cloud.h):
  - added a lightweight cloud state-sync callback registration API
- Updated [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h):
  - stored the state-sync callback in cloud runtime context
  - exposed an internal helper so xiaozhi session paths can request a state recompute without depending on `river_core`
- Updated [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c):
  - implemented the new state-sync callback registration
  - added `river_cloud_request_state_sync(...)`
  - requested a state recompute on the direct `network_lost` teardown path
- Updated [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c):
  - requested a state recompute after `river_cloud_xiaozhi_window_close(...)`
  - requested a state recompute after `river_cloud_xiaozhi_window_abort_local(...)`
- Updated [components/river_core/river_app.c](/root/ameba-river/components/river_core/river_app.c):
  - wired the cloud state-sync callback to `river_session_coordinator_sync_interaction_state(...)` through a local wrapper
- Root-cause summary:
  - the user serial log showed `xiaozhi conversation window closed: reason=followup_timeout`, but there was no matching `interaction_state: follow_up -> wake_monitoring`
  - KWS detection is only allowed when `interaction_state == wake_monitoring`, so after the first turn the board could remain in a stale post-wake state and keep reporting `ready=no`
  - VAD still seeing later speech in that condition explains the observed symptom: capture stayed alive, but the second wakeword was ignored because the interaction state never re-armed KWS
- Why this is the minimal fix:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - `river_cloud` still does not directly depend on `river_core`; it only emits a generic "recompute state now" callback
  - the fix covers both normal follow-up timeout close and abnormal local-abort teardown paths that can otherwise leave the same stale state behind
- Verified locally:
  - full `RTL8730E` build succeeds
  - image sizes remain unchanged:
    - `build_RTL8730E/km4_boot_all.bin` = `51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin` = `3560800`
    - `build_RTL8730E/ota_all.bin` = `3560832`
- Next board verification target:
  - after xiaozhi reply playback drains and the window times out, confirm:
    - `xiaozhi conversation window closed: reason=followup_timeout`
    - `interaction_state: follow_up -> wake_monitoring reason=followup_timeout`
  - then trigger a second wake without rebooting and confirm:
    - `wakeword hit: text=小欧管家`
    - `wakeword queued text=小欧管家`

## Step 5.42
- Updated [plan.md](/root/ameba-river/plan.md) so the branch plan matches the current real priority:
  - the wake rearm regression is already fixed and verified
  - the current top issue is repeated playback causing heap collapse and intermittent `underrun`
  - the immediate next step is now explicitly `AudioTrack` reuse plus playback heap instrumentation
- Updated [include/river/river_playback_service.h](/root/ameba-river/include/river/river_playback_service.h):
  - added playback stats counters for track lifecycle:
    - `track_create_count`
    - `track_reuse_count`
    - `track_destroy_count`
- Updated [components/river_voice/river_playback_service.c](/root/ameba-river/components/river_voice/river_playback_service.c):
  - added a cached prepared-track state so compatible playback sessions can reuse an existing `AudioTrack`
  - changed normal `stop` / `interrupt` handling to stop and flush the active stream while keeping the compatible track object alive for the next playback start
  - kept destructive release for incompatible reconfiguration or failed restart/init/start paths
  - added playback heap snapshots around start and stop:
    - `playback_start_prepare`
    - `playback_start_new`
    - `playback_start_reuse`
    - `playback_stop_prepare`
    - `playback_stop_cached`
  - extended playback logs so board logs now show whether a start used reuse:
    - `reuse=yes|no`
  - extended `river_playback_service_dump_status()` to expose track lifecycle counters as `track=create/reuse/destroy`
- Root-cause / design summary for this step:
  - recent user logs showed wake rearm was already fixed, but repeated xiaozhi turns drove `heap_free` from about `79KB` down to about `8KB`
  - the most suspicious local lifecycle was `river_playback_service_start_stream()`, which recreated and destroyed `AudioTrack` for every TTS turn
  - this step intentionally avoids changing SDK code or weakening admission / KWS gates; it only narrows the playback lifecycle and improves diagnostics
- Why this is the minimal change:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - playback reuse is limited to config-compatible starts; incompatible or failed paths still destroy and recreate the track cleanly
  - the new heap snapshots make it possible to validate or falsify the playback-leak hypothesis directly from serial logs
- Verified locally:
  - full `RTL8730E` build succeeds
  - output images remain unchanged:
    - `build_RTL8730E/km4_boot_all.bin` = `51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin` = `3560800`
    - `build_RTL8730E/ota_all.bin` = `3560832`
- Next board verification target:
  - trigger at least `3` consecutive xiaozhi wake -> ASR -> TTS -> follow-up-timeout cycles
  - confirm later TTS starts show `reuse=yes`
  - compare playback snapshots before and after each turn to verify `heap_free` no longer collapses across turns
  - confirm whether `underrun` frequency decreases or at least correlates with low-heap snapshots

## Step 5.43
- Updated [plan.md](/root/ameba-river/plan.md) again so the active branch plan matches the newest user logs:
  - playback reuse remains relevant, but the immediate blocker is now `KWS` queue saturation and control-item loss
  - the top priority is now `KWS` queue integrity before further playback tuning
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - changed the KWS input ring from `RIVER_AUDIO_FRAME_RING_MODE_SPSC` to `RIVER_AUDIO_FRAME_RING_MODE_LOCKED`
  - removed the old generic enqueue path that treated PCM and control items the same under overflow
  - added `river_voice_kws_clear_input_queue(...)` so gate-reset can explicitly drain stale backlog before rearming
  - changed `river_voice_kws_enqueue_reset(...)` to clear queued stale items before writing a fresh `RESET`
  - changed `river_voice_kws_enqueue_pcm(...)` so a full queue preserves an older control item instead of discarding it in favor of new PCM
  - added a new info log:
    - `kws gate rearm cleared stale queue: pcm=%lu ctrl=%lu`
- Root-cause summary:
  - the new user logs showed `queue=40/40` pinned, rapidly rising `dropped`, repeated `kws queue dropped control item: type=1`, and `river_kws` CPU climbing very high
  - `river_voice_kws.cc` initialized the worker input ring as `SPSC`, but the producer overflow path also performed `river_audio_frame_ring_read(...)` to evict old items
  - that violates the ring contract and makes the gate/reset control flow unreliable exactly in the failure mode seen on the board
  - once `RESET` control items are dropped under backlog, the worker can stay busy chewing stale PCM and never cleanly rearm for the next gate/open cycle
- Why this is the minimal change:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no KWS model, thresholds, features, or wake text were changed
  - the fix is limited to queue correctness and overflow policy, which is the narrowest local explanation for the observed logs
- Verified locally:
  - full `RTL8730E` build succeeds
  - output images remain:
    - `build_RTL8730E/km4_boot_all.bin` = `51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin` = `3560800`
    - `build_RTL8730E/ota_all.bin` = `3560832`
- Next board verification target:
  - confirm `kws queue dropped control item: type=1` disappears
  - confirm gate transitions no longer leave the queue pinned at `40/40` with fast-growing drop counters
  - confirm `kws gate rearm cleared stale queue: ...` appears when backlog has to be drained
  - then re-check whether wake hits recover or whether the next blocker is now the front-end audio amplitude / clipping path

## Step 5.44
- Saved the pre-refactor local workspace into git stash instead of committing user-owned files:
  - `stash@{0}: On kws-no-mean-model: pre-refactor-branch-worktree-backup-20260331`
  - this preserves local `TIPS.md` / `.env` changes without mixing them into the refactor history
- Created and switched to a new branch from the current fix baseline:
  - source branch: `prep/kws-no-mean-model`
  - source commit: `31bf4dd`
  - new branch: `refactor`
- Updated [plan.md](/root/ameba-river/plan.md) for the new branch:
  - changed the branch marker to `refactor`
  - made the branch objective explicit: behavior-preserving cleanup and refactor before more feature churn
  - promoted `Boundary Cleanup` to in-progress on this branch
  - added a dedicated refactor track with the first slicing priority:
    - `components/river_cloud/river_cloud_adapter.c`
    - `components/river_core/river_app.c`
    - `components/river_voice/river_voice_kws.cc`
  - changed the immediate next step from runtime bug validation to the first refactor slice on `river_cloud_adapter.c`
- Why this is the right branch kickoff:
  - the repository already has several runtime fixes landed, but the remaining work is increasingly constrained by large-file coupling rather than by a single missing feature
  - creating a dedicated `refactor` branch keeps structural cleanup separate from the hotfix line
  - stashing `TIPS.md` / `.env` avoids polluting branch history with user-owned review material or environment-local state
- Scope of this step:
  - no runtime code path changed
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - this step is only the branch/bootstrap and refactor-plan handoff
- Next implementation target on `refactor`:
  - first inspect and split `components/river_cloud/river_cloud_adapter.c`
  - move provider/session glue toward smaller implementation units while keeping the external `river_cloud` contract stable

## Step 5.45
- Updated [plan.md](/root/ameba-river/plan.md) to switch the active branch theme from generic refactor-first to performance-first:
  - the current top priority is now explicit KWS realtime throughput and queue-backlog control
  - the immediate next step is no longer cloud-structure slicing, but KWS worker wakeup and pre-roll burst mitigation
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - raised the KWS worker priority from `4` to `5` so the consumer is less likely to be starved by same-priority producer-side work
  - replaced the old idle poll delay path with event-driven wakeup using a worker semaphore
  - drained the worker signal when KWS is disarmed so stale wakeups do not keep the worker spinning on an empty queue
  - kept signaling on both PCM enqueue and RESET enqueue so the worker can react immediately without waiting for a periodic poll
  - changed pre-roll replay on gate-open from "flush everything" to "keep only the most recent limited window and flush that"
  - capped the gate-open pre-roll burst with:
    - `RIVER_KWS_PRE_ROLL_FLUSH_MAX_FRAMES = 8`
  - added a new log when older pre-roll history is intentionally trimmed:
    - `kws pre-roll trim: dropped=%lu keep=%u/%u`
  - extended boot/profile logs so runtime now shows:
    - worker wake model = `event`
    - worker wait budget
    - pre-roll flush cap
- Root-cause / performance summary:
  - user logs showed `queue=40/40` saturation, fast-growing drop counters, and `river_kws` CPU rising sharply before reliable wake returned
  - after the previous control-item fix, the next clear bottlenecks were:
    - gate-open pre-roll being flushed as a burst into the KWS queue
    - the KWS worker still relying on an empty-queue poll/sleep loop instead of prompt event wakeup
    - producer and consumer operating at the same task priority
  - this combination is a classic realtime backlog amplifier: burst enqueue reduces headroom, equal-priority scheduling delays catch-up, and polling adds avoidable latency under light load
- Why this is the right first performance slice:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no wakeword model, thresholds, or feature extraction math were changed
  - the step only changes scheduling, wakeup behavior, and backlog policy in the hottest local queue
  - this directly targets realtime latency and queue occupancy before broader cloud/playback optimization
- Expected effect:
  - lower queue occupancy immediately after gate-open
  - fewer long-lived `queue=40/40` plateaus
  - less idle CPU waste from the KWS worker
  - better probability that fresh speech frames are processed before they become stale backlog
- Next board verification target:
  - confirm the boot/profile logs show:
    - `kws worker: ... wake=event ... pre_roll_flush=8`
    - `kws backend: ... pre_roll_flush=8 ...`
  - confirm `kws gate open` is no longer followed by near-immediate queue saturation
  - confirm `kws pre-roll trim: ...` appears when gate-open would otherwise replay too much backlog
  - re-check whether wake reliability improves under repeated short speech bursts

## Step 5.46
- Updated [plan.md](/root/ameba-river/plan.md) for the second KWS performance slice:
  - the event-driven worker / pre-roll-cap step is now treated as the completed first cut
  - the active focus is now separating reset semantics from queued PCM backlog and adding proactive overload trimming
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - removed `RESET` as a queued KWS input item and replaced it with a dedicated `reset_pending` signal
  - changed the worker loop so pending reset is consumed before reading more queued PCM, which gives reset semantics priority over backlog
  - changed gate rearm queue clearing from item-by-item drain to queue-count + reset, which is cheaper and avoids spending extra CPU clearing stale PCM
  - added proactive PCM backlog trimming:
    - when the input queue reaches the high-water mark, the oldest PCM is dropped until the queue falls back to a lower target
    - with the current `CONFIG_RIVER_KWS_INPUT_QUEUE_FRAMES=40`, the runtime trim policy is `30 -> 13`
  - kept the existing single-frame overflow fallback, but removed the old control-item preservation branch because control and PCM no longer share the queue
  - added new observability for overload control:
    - `kws input trim: dropped=... queue=...->... target=...`
    - `kws status: ... trim_ops=... trim_drop=...`
    - `kws worker: ... trim=30->13`
    - `kws backend: ... trim=30->13`
- Root-cause / performance summary:
  - after Step 5.45, the worker wakeup path was better, but user logs still showed `queue=40/40` plateaus, high `river_kws` CPU, and stale PCM backlog dominating the queue
  - as long as reset shared the same queue with PCM, aggressive trimming risked damaging control ordering
  - separating reset semantics makes it safe to trim old PCM harder, which is the correct realtime tradeoff for this stage
- Expected effect:
  - reset is no longer blocked behind queued PCM backlog
  - the queue can recover from saturation faster instead of remaining pinned at `40/40`
  - consumer time is spent more on fresh speech frames and less on stale backlog
  - logs can now distinguish generic drop growth from intentional high-water trim behavior
- Scope / guardrails:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no KWS model, thresholds, or feature-extraction math changed in this step
  - this slice stays strictly inside the KWS hot path and overload policy
- Local verification snapshot:
  - full `RTL8730E` rebuild passed after this change
  - output images remained:
    - `build_RTL8730E/km4_boot_all.bin 51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
    - `build_RTL8730E/ota_all.bin 3560832`

## Step 5.53
- Switched the default baseline KWS asset from `bc_resnet_epoch1_debug.tflite` to `/root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_v3_production.tflite`.
- Updated [components/river_voice/generated/river_wake_word_model_data.h](/root/ameba-river/components/river_voice/generated/river_wake_word_model_data.h):
  - regenerated the embedded `kws_model` payload from `bc_resnet_v3_production.tflite`
  - kept the exported symbol names `kws_model` / `kws_model_len`
  - embedded model size remains `54104` bytes
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - changed the default runtime variant name from `bc_resnet_epoch1_debug` to `bc_resnet_v3_production`
- Deployment notes adopted from `/root/kws-training-pro/models/bc_resnet_iteration3/DEPLOYMENT_REPORT_FINAL.md`:
  - input shape remains `[1, 40, 98, 1]`
  - output shape remains `[1, 1, 1, 1]`
  - whitelist ops remain `PAD, CONV_2D, DEPTHWISE_CONV_2D, ADD, AVERAGE_POOL_2D, LOGISTIC`
  - no `MEAN` op is reintroduced, so the current mainline resolver stays valid
- Why this step is intentionally narrow:
  - the new production model keeps the same size and operator envelope as the current baseline, so this change can stay focused on model payload replacement rather than reopening runtime compatibility work
  - schema-driven quant parsing in the existing runtime will pick up the new input quantization automatically at boot
- Local verification snapshot:
  - full `RTL8730E` rebuild passed after this change
  - output images remained:
    - `build_RTL8730E/km4_boot_all.bin 51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
    - `build_RTL8730E/ota_all.bin 3560832`
  - `strings build_RTL8730E/km0_km4_ca32_app.bin` contains `bc_resnet_v3_production`
  - `strings build_RTL8730E/km0_km4_ca32_app.bin` no longer contains `bc_resnet_epoch1_debug`

## Step 5.47
- Updated [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h):
  - added compile-time macro `RIVER_CLOUD_BUSINESS_TIME_WAIT_REQUIRED`
  - the macro is derived from `RIVER_CLOUD_BACKEND_IFLYTEK_ENABLED`, so only builds that include the Iflytek split ASR/TTS path keep the strict time-ready gate
- Updated [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c):
  - split "system time is actually ready" from "the current build must wait for time before business may proceed"
  - added `river_cloud_business_time_ready()`
  - changed generic cloud business gates to use the new compile-time policy:
    - stream-open path waits for UTC only when Iflytek backend is compiled in
    - direct TTS submit path waits for UTC only when Iflytek backend is compiled in
  - kept SNTP start/kick behavior unchanged so XiaoZhi-only builds still sync time opportunistically, but are no longer blocked on it
- Updated [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c):
  - wake admission still kicks/seeds SNTP, but it no longer blocks on time when Iflytek ASR/TTS is absent
  - the "build-seeded utc estimate" log is now only emitted when a build actually requires time gating
- Behavior summary:
  - `CONFIG_RIVER_CLOUD_BACKEND_XIAOZHI_REALTIME=y` and `CONFIG_RIVER_CLOUD_BACKEND_IFLYTEK_SPLIT=n`:
    - wake/business admission no longer waits for `sntp ready`
  - `CONFIG_RIVER_CLOUD_BACKEND_IFLYTEK_SPLIT=y`:
    - original UTC/time-ready gate remains intact for Iflytek auth/signature flows
- Scope / guardrails:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - SNTP init/kick was not removed; only the business-side wait gate was isolated
- Local verification snapshot:
  - full `RTL8730E` rebuild passed after this change
  - output images remained:
    - `build_RTL8730E/km4_boot_all.bin 51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
    - `build_RTL8730E/ota_all.bin 3560832`

## Step 5.48
- Updated [plan.md](/root/ameba-river/plan.md):
  - recorded the new playback-stability sub-goal: when `xiaozhi` is about to reconnect under heap pressure, idle playback cache must yield to the connect path
- Updated [include/river/river_playback_service.h](/root/ameba-river/include/river/river_playback_service.h):
  - added `river_playback_service_release_idle_track_cache()` as a narrow playback-service contract for reclaiming cached `AudioTrack` resources only when playback is already idle
- Updated [components/river_voice/river_playback_service.c](/root/ameba-river/components/river_voice/river_playback_service.c):
  - implemented `river_playback_service_release_idle_track_cache()`
  - the helper only releases the cached track when all of the following are true:
    - playback service is initialized
    - state is `RIVER_PLAYBACK_IDLE`
    - a cached `AudioTrack` still exists
    - no track is started and no reference export is owned
  - this keeps the hot-path behavior unchanged while letting higher-priority flows reclaim memory from noncritical cache
- Updated [components/river_cloud/river_xiaozhi_ws.c](/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c):
  - added a low-heap preconnect guard for `river_xiaozhi_open_session()`
  - before OTA bootstrap / websocket connect, if free heap is below `64KB`, the connect path now tries to release idle playback cache first
  - if reclaim happens, logs now expose the exact heap change:
    - `xiaozhi preconnect reclaimed idle playback cache: heap_free=...->... threshold=65536`
- Root-cause / realtime summary:
  - the user-provided failure log showed `playback_stop_cached` left the system around `60KB` free, then the next wake-driven `xiaozhi` connect from `river_wake_evt` fell to `704B` free and died on a `640B` allocation
  - that is the wrong priority order for a realtime assistant:
    - a reusable playback cache is optional
    - the next session open is critical
  - this step explicitly flips that priority under memory pressure so optional playback reuse cannot block the next dialogue session
- Expected effect:
  - repeated wake -> TTS -> follow-up timeout -> wake cycles should no longer fail in `river_wake_evt` just because an idle playback cache is still occupying heap
  - the failure cascade:
    - `Malloc failed. Core:[CA32], Task:[river_wake_evt], ...`
    - followed by transport instability such as `WIFI TRX IPC 4 timeout`
    should be materially less likely or disappear in the reproduced scenario
  - when reclaim happens, the next TTS may recreate its `AudioTrack` instead of reusing it; this is an intentional tradeoff in favor of availability
- Scope / guardrails:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - websocket protocol, queue sizing, and ASR/TTS business logic were not changed in this step
  - the change only affects low-heap admission behavior before a fresh `xiaozhi` session open
- Local verification snapshot:
  - full `RTL8730E` rebuild passed after this change
  - output images remained:
    - `build_RTL8730E/km4_boot_all.bin 51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
    - `build_RTL8730E/ota_all.bin 3560832`

## Step 5.49
- Updated [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c):
  - re-armed the XiaoZhi follow-up conversation window inside `river_cloud_xiaozhi_open_session_and_listen()`
  - each fresh listen / ASR round now refreshes the window to `RIVER_CLOUD_XIAOZHI_WAKE_WINDOW_FOLLOWUP_MS`
  - added an inline comment documenting the exact failure mode this prevents:
    - the shorter post-TTS tail timer could expire while the user had already started the next utterance, so the websocket closed immediately after that ASR round finished
- Root-cause / session-contract summary:
  - after `tts stop`, the current code intentionally shortens the window to `RIVER_CLOUD_XIAOZHI_POST_TTS_SILENCE_CLOSE_MS`
  - if follow-up speech starts near the end of that shorter window, ASR can reopen and run normally, but the old deadline remains in force
  - then once that ASR round ends, `followup_timeout` fires almost immediately and closes the websocket even though the user just engaged a valid next turn
  - refreshing the window on listen/asr start fixes that contract mismatch without weakening the post-TTS idle close behavior
- Expected effect:
  - in logs like the user-provided case, a second `asr provider=xiaozhi_realtime session started sid=...` during follow-up should no longer be followed almost immediately by websocket close just because the prior post-TTS deadline had already expired
  - the websocket should remain open long enough for the second round to receive normal `stt/llm/tts` traffic, unless a real transport/server issue occurs
- Scope / guardrails:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no ASR/VAD thresholds, playback parameters, or transport buffer sizes changed in this step
  - only the follow-up window timing contract was adjusted
- Local verification snapshot:
  - full `RTL8730E` rebuild passed after this change
  - output images remained:
    - `build_RTL8730E/km4_boot_all.bin 51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
    - `build_RTL8730E/ota_all.bin 3560832`

## Step 5.50
- Updated [plan.md](/root/ameba-river/plan.md):
  - added a dedicated structural-refactor track for session state ownership
  - recorded that the next cleanup focus is `follow_up / listening / speaking` contract tightening rather than another blind hot-path tweak
- Updated [components/river_core/river_session_coordinator.c](/root/ameba-river/components/river_core/river_session_coordinator.c):
  - introduced an internal `river_session_phase_t` state machine for:
    - `booting`
    - `wake_monitoring`
    - `wake_confirmed`
    - `asr_streaming`
    - `follow_up`
    - `speaking`
    - `barge_in_listening`
    - `error_recovering`
  - added a dedicated coordinator `state_lock` so runtime session flags and phase transitions stop being open-coded writes spread across callbacks
  - centralized runtime `interaction_state` writes behind one helper:
    - `river_session_apply_phase_locked()`
    - public `interaction_state` is now derived from coordinator phase, instead of each callback directly calling `river_interaction_state_set(...)`
  - made `river_session_coordinator_sync_interaction_state()` compute a target phase from current runtime facts:
    - playback active
    - ASR session active
    - cloud conversation window active
  - added transition validation / warning logs inside the coordinator so future session-contract cleanup can see illegal or surprising jumps immediately
  - updated wakeword admission to check the coordinator-owned phase instead of re-reading external interaction state
  - updated barge-in TTS interruption gating to also read coordinator phase, so both state writes and critical state reads now stay within the same control-plane owner
  - updated wakeword success, ASR session start/close, ASR error, and playback error paths to transition through the coordinator phase machine
- Structural intent / why this matters:
  - this step does not yet redesign the XiaoZhi follow-up contract
  - it first fixes the control-plane shape so the project has a single runtime owner for interaction-state transitions
  - that mirrors the stronger device-state ownership seen in `xiaozhi-esp32`, without yet rewriting transport or audio behavior
- Expected effect:
  - future `follow_up` and `barge_in` work now has one place to tighten state transitions instead of auditing multiple callbacks again
  - wakeword admission and runtime state sync now share the same coordinator-owned phase source, reducing hidden divergence between “what logs say” and “what callbacks think the state is”
  - if the system still exhibits odd state jumps, new warning logs from the phase machine should make them easier to isolate
- Scope / guardrails:
  - no SDK source under `/root/ameba-rtos-1.2` was modified
  - no audio thresholds, queue sizes, or XiaoZhi wire protocol behavior changed in this step
  - this is a control-plane refactor only; behavior is intended to remain functionally equivalent
- Local verification snapshot:
  - full `RTL8730E` rebuild passed after this change
  - output images remained:
    - `build_RTL8730E/km4_boot_all.bin 51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
    - `build_RTL8730E/ota_all.bin 3560832`

## Step 5.51
- Updated [plan.md](/root/ameba-river/plan.md):
  - added a dedicated `Phase 4.2: XiaoZhi Runtime Ownership`
  - recorded that the next structural step is to pull `listen_start / listen_stop / follow_up rearm` behind the session module as well
- Updated [components/river_cloud/river_cloud_internal.h](/root/ameba-river/components/river_cloud/river_cloud_internal.h):
  - declared shared `xiaozhi` runtime helper APIs for:
    - pending-text reset
    - playback stop arm/cancel/reset
    - downlink reset
    - transport-local runtime reset
- Updated [components/river_cloud/river_cloud_xiaozhi_session.c](/root/ameba-river/components/river_cloud/river_cloud_xiaozhi_session.c):
  - moved `xiaozhi` local runtime ownership further into the session submodule
  - added shared helpers:
    - `river_cloud_xiaozhi_clear_pending_text()`
    - `river_cloud_xiaozhi_cancel_playback_stop()`
    - `river_cloud_xiaozhi_mark_playback_started()`
    - `river_cloud_xiaozhi_arm_playback_stop()`
    - `river_cloud_xiaozhi_reset_playback_state()`
    - `river_cloud_xiaozhi_reset_downlink_state()`
    - `river_cloud_xiaozhi_reset_transport_state()`
  - reused the new pending-text helper from `river_cloud_xiaozhi_open_session_and_listen()`
  - added a brief ownership comment so future cleanup stays anchored in this file
- Updated [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c):
  - removed duplicated adapter-local helpers for playback/downlink reset
  - switched playback start / write-fail / stop-deadline handling to session-owned helpers
  - switched `tts start` / `tts stop` handling to session-owned playback helpers
  - switched `transport_closed`, `network_lost`, and `river_cloud_asr_audio_close()` to the shared `river_cloud_xiaozhi_reset_transport_state(...)` cleanup path
  - kept behavior-specific choices explicit:
    - `transport_closed` still emits `session_closed`
    - `network_lost` and `audio_close` still do not synthesize that event
- Structural intent / why this matters:
  - this step keeps the protocol and hot path unchanged
  - it reduces the number of places that manually manipulate the same `xiaozhi` runtime bits
  - it follows the same general lesson seen in `xiaozhi-esp32`: event handlers should describe causes, while state ownership should stay centralized
- Expected effect:
  - transport-loss and audio-close cleanup paths are less likely to drift out of sync over time
  - future refactors around `listen_stop_pending` and follow-up rearm now have one helper surface to extend instead of several copied reset blocks
  - runtime status after teardown should be more consistently cleared in one pass
- Local verification snapshot:
  - full `RTL8730E` rebuild passed after this change
  - output images remained:
    - `build_RTL8730E/km4_boot_all.bin 51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
    - `build_RTL8730E/ota_all.bin 3560832`

## Step 5.52
- Updated [components/river_voice/river_playback_service.c](/root/ameba-river/components/river_voice/river_playback_service.c):
  - included `audio_control.h` in the playback service path
  - added `river_playback_service_prepare_output_locked()` so each playback stream start now explicitly:
    - un-mutes playback
    - un-mutes the amplifier
    - sets hardware playback volume to `1.0 / 1.0`
  - kept this in the shared playback service so all speaker-bound playback streams benefit, not only one backend
- Updated [components/river_cloud/river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c):
  - added a XiaoZhi playback saturation helper
  - changed downlink mono->stereo expansion to apply a `2x` software PCM gain before writing to `AudioTrack`
  - extended the playback-start log to expose the active `gain=2/1`
- Updated [components/river_cloud/river_tts_iflytek_ws.c](/root/ameba-river/components/river_cloud/river_tts_iflytek_ws.c):
  - raised `iflytek_tts` stream volume from `0.85` to `1.00`
- Behavioral intent:
  - the previous XiaoZhi path had already reached `AudioTrack_SetVolume(..., 1.0, 1.0)`, so further loudness increase required changes below or alongside the track-volume layer
  - this step therefore boosts loudness in two places:
    - hardware playback volume
    - XiaoZhi PCM amplitude, with int16 saturation to avoid wraparound
- Expected effect:
  - XiaoZhi TTS should be noticeably louder on the board, not just slightly louder
  - Iflytek TTS also stops leaving `15%` of stream-side volume unused
  - clipping risk is bounded by saturation rather than integer overflow
- Local verification snapshot:
  - full `RTL8730E` rebuild passed after this change
  - output images remained:
    - `build_RTL8730E/km4_boot_all.bin 51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
    - `build_RTL8730E/ota_all.bin 3560832`

## Step 5.53
- Added repo-owned KWS export tooling so model deployment can be fixed without editing `/root/kws-training-pro`:
  - [tools/kws/export_bc_resnet_tflite.py](/root/ameba-river/tools/kws/export_bc_resnet_tflite.py)
    - exports `BC-ResNet` checkpoints to `float32` or `int8` TFLite
    - keeps the current board-compatible operator set
    - uses board-aligned frontend features from `river_kws_features.py` for `int8` representative calibration instead of random Gaussian tensors
  - [tools/kws/embed_tflite_model.py](/root/ameba-river/tools/kws/embed_tflite_model.py)
    - converts a `.tflite` into the existing `#pragma once` + `static const` header format used by the firmware
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - changed the default embedded-model variant string from `bc_resnet_v3_production` to `bc_resnet_v3_production_fp32`
  - this makes serial-side runtime identification explicit for the current board validation step
- Regenerated [components/river_voice/generated/river_wake_word_model_data.h](/root/ameba-river/components/river_voice/generated/river_wake_word_model_data.h):
  - embedded a freshly exported float32 model from `/root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_best.pth`
  - embedded model length is now `77848` bytes
- Fix intent:
  - the current board failure strongly points to bad `int8` post-training calibration, not to the frontend path or wake phrase
  - this step therefore establishes a clean float32 on-board baseline first, while also landing a repo-owned calibrated `int8` export path for the next iteration
- Local and board-side verification snapshot:
  - float32 export passed:
    - `/tmp/bc_resnet_v3_production_fp32.tflite 77848`
    - input/output dtype: `float32`
  - full `RTL8730E` rebuild passed after embedding the float32 model
  - output images became:
    - `build_RTL8730E/km4_boot_all.bin 51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin 3585376`
    - `build_RTL8730E/ota_all.bin 3585408`
  - binary verification passed:
    - `strings build_RTL8730E/km0_km4_ca32_app.bin` contains `bc_resnet_v3_production_fp32`
  - board flash passed on `/dev/ttyUSB0` at `1500000` baud using project flash wrapper
  - post-flash serial command verification passed:
    - `river status` returned normal runtime status from the flashed board
  - remaining gap:
    - this run attached monitor after the reset window, so the boot-time `kws backend: ... variant=bc_resnet_v3_production_fp32` line was not captured in the same turn

## Step 5.54
- Switched the embedded wake-word model from the failed float32 baseline to a calibrated int8 deployment:
  - regenerated [components/river_voice/generated/river_wake_word_model_data.h](/root/ameba-river/components/river_voice/generated/river_wake_word_model_data.h)
  - the embedded model now comes from `/tmp/bc_resnet_v3_production_int8_cal.tflite`
  - exported model size is `54104` bytes
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - changed the runtime variant string from `bc_resnet_v3_production_fp32` to `bc_resnet_v3_production_int8_cal`
  - keeps serial-side model identification explicit after the int8 swap
- Updated [components/river_core/river_app.c](/root/ameba-river/components/river_core/river_app.c):
  - included `river_voice_kws_dump_status()` in `river_app_print_status()`
  - `river status` now exposes a direct KWS runtime line instead of forcing boot-log timing or incidental speech logs
- Fix intent:
  - the float32 board attempt failed at boot on `2026-04-01 12:48:29` with `kws AllocateTensors failed: arena=192KB model=77848B`
  - the immediate goal of this step was therefore to return to a board-fit model while preserving deterministic runtime observability
- Local and board-side verification snapshot:
  - corrected int8 export passed with representative calibration from real feature manifests:
    - `/tmp/bc_resnet_v3_production_int8_cal.tflite 54104`
    - exporter reported `representative_samples=256`
    - input quantization changed to approximately `scale=0.0179046784 zp=-7`
    - output quantization remained `scale=0.00390625 zp=-128`
  - rebuild passed after the int8 embed and status-path change
  - output images remained:
    - `build_RTL8730E/km4_boot_all.bin 51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
    - `build_RTL8730E/ota_all.bin 3560832`
  - binary verification passed:
    - `strings build_RTL8730E/km0_km4_ca32_app.bin` contains `bc_resnet_v3_production_int8_cal`
  - board flash passed on `/dev/ttyUSB0` at `1500000` baud
  - live runtime verification passed:
    - ambient speech produced `kws gate open` / `kws gate close` logs before the status-path patch was reflashed
    - after reflashing the status-path patch, `river status` on `2026-04-01 13:02:18` printed `river.voice.kws] kws status: ...`
    - no `AllocateTensors failed` line appeared in the int8-cal board runs
    - runtime stayed at `tasks=17`, matching an active KWS task rather than the earlier float32 fallback case

## Step 5.55
- Replaced the previous calibrated-int8 model identity with the algorithm team's final production deployment configuration from `/root/kws-training-pro/models/bc_resnet_iteration3/DEPLOYMENT_REPORT_V3_FINAL.md`.
- Updated [prj.conf](/root/ameba-river/prj.conf):
  - changed `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15` from the temporary permissive `8192` to `19660`
  - this matches the report's factory-default balanced threshold of `0.6`
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - changed the runtime model variant string from `bc_resnet_v3_production_int8_cal` to `bc_resnet_v3_production_final`
  - raised the gate fallback floor from `350 pm` to `400 pm` so fallback admission is not weaker than the report's documented high-sensitivity operating point
- Regenerated [components/river_voice/generated/river_wake_word_model_data.h](/root/ameba-river/components/river_voice/generated/river_wake_word_model_data.h):
  - re-embedded the model from `/root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_v3_production_final_v2.tflite`
  - upstream `final_v2` and `final` artifacts are byte-identical:
    - SHA-256 `4e7f368f67f9ba7ad98e1b037c1e447f3228dca43305a03e8b5cf46a533523d6`
    - size `54104` bytes
- Deployment intent:
  - keep the board on the algorithm team's final production model contract
  - align the firmware threshold with the report's recommended factory default instead of the earlier low validation threshold
  - preserve explicit runtime observability through the production-final variant string
- Local and board-side verification snapshot:
  - deployment report confirmed the same tensor contract as the final production drop:
    - input `int8`, scale `0.01790468`, zero-point `-7`
    - output `int8`, scale `0.00390625`, zero-point `-128`
  - rebuild passed after the production-final embed
  - output images remained:
    - `build_RTL8730E/km4_boot_all.bin 51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
    - `build_RTL8730E/ota_all.bin 3560832`
  - binary verification passed:
    - `strings build_RTL8730E/km0_km4_ca32_app.bin` contains `bc_resnet_v3_production_final`
    - no `bc_resnet_v3_production_int8_cal` string remained in the image
  - board flash passed on `/dev/ttyUSB0` at `1500000` baud on `2026-04-01 14:10`
  - post-flash monitor still reproduces the existing SDK-side issue:
    - connection succeeds
    - command-list discovery times out with `Failed to get cmd list: Get cmd list expired`
    - this did not block rebuild or flash, but it limited same-turn runtime command verification

## Step 5.56
- Forced deployment of the current upstream export result from `/root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_v3_production_final_v2.tflite` per user instruction, even though the exporter's parity gate reported mismatches.
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - changed the runtime model variant string from `bc_resnet_v3_production_final` to `bc_resnet_v3_production_final_v2`
- Regenerated [components/river_voice/generated/river_wake_word_model_data.h](/root/ameba-river/components/river_voice/generated/river_wake_word_model_data.h):
  - re-embedded the current export artifact from `/root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_v3_production_final_v2.tflite`
  - embedded artifact properties at deployment time:
    - size `54104` bytes
    - MD5 `0cd2c03ec46888ff0506a9e41ab7a33f`
    - SHA-256 `19fa4dca80ff4355b9de6da242789aabb16abed63820b2a3bd00a4c979a70a0b`
- Recorded upstream export caveat explicitly for deployment traceability:
  - exporter wrote `/root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_v3_production_final_v2.tflite.meta.json`
  - parity gate failed after writing the model:
    - `pt_vs_tflite mean_abs=0.054256`
    - agreement `114/128` at threshold `0.4`
    - agreement `121/128` at threshold `0.6`
    - agreement `123/128` at threshold `0.8`
  - threshold derivation emitted by the exporter:
    - `0.4 -> raw_ge=-25 raw_nearest=-26`
    - `0.6 -> raw_ge=26 raw_nearest=26`
    - `0.8 -> raw_ge=77 raw_nearest=77`
- Rebuilt and reflashed the Ameba image set with the forced-export model:
  - build completed successfully with unchanged image sizes:
    - `build_RTL8730E/km4_boot_all.bin 51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin 3560800`
    - `build_RTL8730E/ota_all.bin 3560832`
  - binary verification passed:
    - `strings build_RTL8730E/km0_km4_ca32_app.bin` contains `bc_resnet_v3_production_final_v2`
  - board flash passed on `2026-04-01 15:20` after reattaching the USB serial adapter into WSL
- Deployment notes:
  - initial flash attempt failed only because `/dev/ttyUSB0` was absent from WSL
  - host-side USB/IP inspection found the board on `BUSID 3-4` as `Prolific PL2303GC USB Serial COM Port (COM3)`
  - after `usbipd.exe attach --wsl --busid 3-4`, `/dev/ttyUSB0` reappeared and flash completed with `Finished PASS`
  - post-flash serial connection succeeded, but this turn did not capture a fresh boot banner because the monitor attached after the reset window

## Step 5.57
- Added [tools/kws/compare_triplet_kws.py](/root/ameba-river/tools/kws/compare_triplet_kws.py) to do offline triage on real `rtl8730e-board` WAVs:
  - loads the production checkpoint with the exporter's internal `TorchExportBCResNet`
  - runs the exported `bc_resnet_v3_production_final_v2.tflite`
  - compares the current training-side Python frontend against a board-faithful host replay path
- The new board-faithful host replay path intentionally mirrors current board C behavior more closely than `river_kws_features.py`:
  - applies the same Hann formula as [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc)
  - rounds the windowed samples back to `int16` before FFT, matching `river_voice_kws_capture_window()`
  - uses the same WebRTC fixed-point FFT implementation from `third_party/webrtc_aecm/aecm/{real_fft.c,complex_fft.c,signal_processing_library.cc}`
  - replays the same mel-band weighting and normalization contract as the board path
- Real-board WAV triage used three recorded samples from `/root/kws-dataset-pro-blueprint/data/augmented_final`:
  - positive wake word:
    - `device_recordings___positive___positive__pos_neutral_mid_001__小欧管家__take001__rtl8730e-board__rec-1774343625242-44fbbee1.wav`
  - hard negative near-homophone:
    - `device_recordings___negative___hard_negative__hn_shang_jia_001__小欧商家__take001__rtl8730e-board__rec-1774403309113-d87271e2.wav`
  - verifier-style context negative containing the wake phrase:
    - `device_recordings___negative___verifier_negative__vn_context_005__这是谁家的小欧管家__take001__rtl8730e-board__rec-1774401769981-9199eabe.wav`
- Observed comparison results:
  - positive wake word:
    - training-vs-board feature diff `mean_abs=0.002765`, `max_abs=0.026696`
    - PT score `0.828093` vs board-faithful PT score `0.827219`
    - TFLite score `0.843750 (raw=88)` vs board-faithful TFLite `0.855469 (raw=91)`
  - hard negative `小欧商家`:
    - training-vs-board feature diff `mean_abs=0.001837`, `max_abs=0.019436`
    - PT score `0.001380` vs board-faithful PT score `0.001369`
    - TFLite score `0.007812 (raw=-126)` vs board-faithful TFLite `0.015625 (raw=-124)`
  - context negative `这是谁家的小欧管家`:
    - training-vs-board feature diff `mean_abs=0.000637`, `max_abs=0.012912`
    - PT score `0.413958` vs board-faithful PT score `0.413964`
    - TFLite score `0.425781 (raw=-19)` vs board-faithful TFLite `0.414062 (raw=-22)`
- Diagnostic conclusion from this step:
  - current training-side feature extractor and board-faithful feature replay are close enough that frontend drift is not the primary explanation for the on-board repeated `0.375/raw=-32` behavior
  - the larger residual is still on the exported TFLite side rather than on the frontend side

## Step 5.58
- Added explicit board-side KWS inference diagnostics so repeated-confidence cases can be localized to a concrete stage instead of guessing from `score_pm` alone.
- Updated [Kconfig](/root/ameba-river/Kconfig):
  - added `CONFIG_RIVER_KWS_DIAG_VERBOSE_EN`
  - added `CONFIG_RIVER_KWS_DIAG_LOG_EVERY_INFER`
- Updated [prj.conf](/root/ameba-river/prj.conf):
  - enabled `CONFIG_RIVER_KWS_DIAG_VERBOSE_EN=y` for the current board-debug phase
  - kept `CONFIG_RIVER_KWS_DIAG_LOG_EVERY_INFER` disabled so log volume stays manageable unless a full per-inference dump is explicitly needed
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - added a pre-quantized feature hash over the normalized `40x98` feature window
  - added a quantized input tensor hash over the exact tensor bytes passed to TFLM
  - captured raw output scalar before score clamping:
    - `int8/uint8` models log the actual raw tensor value
    - `float32` models log the raw output in `milli`
  - tracked repeated-value streaks for:
    - raw output
    - pre-quantized feature hash
    - quantized input hash
  - added `kws diag: ...` logs that emit on:
    - the first few inferences
    - high-confidence frames
    - repeated-pattern streak milestones
  - extended `kws status: ...` to expose the last inference snapshot using explicit `last_*` labels so gate-reset idle states do not look like current inference values
- Live board observation from the new diagnostics on `2026-04-01 16:13:22`:
  - `kws diag: infer=2 gate=open out_type=int8 raw=52 score=0.703125 q15=23039 same=[raw:2 feat:1 input:1] feat_hash=0x94fb99dc input_hash=0x3cab68fc ...`
  - immediately followed by:
    - `wakeword hit: text=小欧管家 score_pm=703 q15=23039`
- Diagnostic conclusion from this step:
  - `same=[raw:2 feat:1 input:1]` means the current inference and the previous inference did **not** reuse the same features or the same quantized input tensor, but they **did** produce the same raw model output
  - that rules out a simple “frontend没变 / tensor没更新 / 阈值设错” explanation for this captured case
  - the repeated confidence is now much more likely to come from model/output-side collapse or overly coarse output behavior under nearby input windows
- Residual runtime note from the same live session:
  - right after the wake-path handoff, CA32 logged `Malloc failed. Core:[CA32], Task:[river_wake_evt], [free heap size: 1280] [xWantedSize:1408]`
  - this is separate from the KWS raw-repeat diagnosis, but it is worth tracking because it can destabilize post-wake behavior

## Step 5.59
- Reworked the board-side exact-tensor dump path from an unsolicited UART log burst into a pull-style snapshot transport, because the previous stream-based approach was not robust enough for this board/adapter combination.
- The immediate trigger for this step was a fresh raw serial capture after reset:
  - `/tmp/tty_capture.bin` contained `2092` bytes of `0x00`
  - that means the serial path can reach a bad “readable but only zero bytes” state even without `pyserial` monitor framing, so continuing to depend on hundreds of unsolicited dump lines would remain fragile
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - added an in-RAM KWS dump snapshot that captures, for the next armed inference only:
    - exact float32 feature tensor
    - exact raw input tensor bytes passed to TFLM
    - exact raw output tensor bytes returned by TFLM
    - snapshot-specific hashes, raw output scalar, score, q15, and inference sequence
  - replaced the old `dump next` behavior so it now only arms snapshot capture and logs a compact `kws tensor dump captured: ...` summary when the snapshot is ready
  - kept the existing `begin/meta/chunk` log format for host compatibility, but now emits those lines only on explicit query
  - extended KWS dump status with:
    - `armed=yes/no`
    - `ready=yes/no`
    - `capture_seq`
    - `capture_infer`
    - per-buffer chunk counts
- Updated [include/river/river_voice_kws.h](/root/ameba-river/include/river/river_voice_kws.h):
  - added public dump-buffer enum values for `feat_f32`, `input_raw`, and `output_raw`
  - added APIs to:
    - clear a cached snapshot
    - print snapshot metadata
    - print one selected chunk by label and index
- Updated [components/river_diag/river_diag_cmd.c](/root/ameba-river/components/river_diag/river_diag_cmd.c):
  - extended monitor commands to:
    - `river kws dump next`
    - `river kws dump off`
    - `river kws dump clear`
    - `river kws dump status`
    - `river kws dump meta`
    - `river kws dump chunk <feat_f32|input_raw|output_raw> <index>`
  - added argument validation so invalid chunk labels or indices fail immediately instead of silently producing unusable output
- Kept [tools/kws/replay_board_tensor_dump.py](/root/ameba-river/tools/kws/replay_board_tensor_dump.py) compatible by preserving the same `kws tensor dump begin/meta/chunk` line grammar; the only behavioral change is that logs are now pulled in smaller operator-controlled steps instead of dumped all at once.
- Local verification on `2026-04-02`:
  - full `RTL8730E` build passed
  - final image sizes were:
    - `build_RTL8730E/km4_boot_all.bin 51872`
    - `build_RTL8730E/km0_km4_ca32_app.bin 3568992`
    - `build_RTL8730E/ota_all.bin 3569024`
  - board flash passed with `Finished PASS` at `2026-04-02 09:48:52`

## Step 5.60
- Added an explicit `no-cloud` KWS debug mode so the current board-side tensor replay workflow can be isolated from XiaoZhi session startup.
- Trigger for this step:
  - exact KWS dump capture was already succeeding on the board
  - but the post-wake handoff immediately entered `river_wake_evt`, hit a CA32 heap failure, and then blocked later `meta/chunk` retrieval:
    - `Malloc failed. Core:[CA32], Task:[river_wake_evt], [free heap size: 256] [xWantedSize:1408]`
    - later `IPC Get Semaphore Timeout`
- Updated [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc):
  - added runtime `local_debug_mode`
  - added `river_voice_kws_wake_handoff_block_reason()` so KWS can explicitly tell the session layer why wake handoff must be suppressed
  - extended `river_voice_kws_dump_status()` with a dedicated debug line:
    - `local_only=yes/no`
    - `wake_handoff=blocked/normal`
    - `reason=local_debug|tensor_dump_ready|-`
  - kept the previous automatic `tensor_dump_ready` protection, so a captured snapshot still blocks handoff even if the operator forgot to toggle local debug first
- Updated [components/river_core/river_session_coordinator.c](/root/ameba-river/components/river_core/river_session_coordinator.c):
  - before scheduling wakeword follow-up, it now checks the KWS-side block reason
  - when blocked, it prints a clear reasoned log:
    - `wakeword handoff held: reason=local_debug ...`
    - or `wakeword handoff held: reason=tensor_dump_ready ...`
  - in this state the wakeword event is still detected and logged, but no XiaoZhi conversation window is opened
- Updated [components/river_diag/river_diag_cmd.c](/root/ameba-river/components/river_diag/river_diag_cmd.c):
  - added:
    - `river kws debug local on`
    - `river kws debug local off`
    - `river kws debug local status`
  - these are intentionally separate from `river xiaozhi disable`:
    - `xiaozhi disable` mutates runtime cloud config
    - `kws debug local on` is a focused board-debug guard that preserves the configured backend but suppresses wake-triggered cloud handoff
- Updated [include/river/river_voice_kws.h](/root/ameba-river/include/river/river_voice_kws.h) with the new KWS debug control/query APIs.
- Outcome for the current debug phase:
  - wakeword, feature extraction, TFLM inference, and tensor dump capture still run normally
  - but the board no longer needs to contact XiaoZhi before `river kws dump meta/chunk ...` can be pulled back
  - this keeps the three-way comparison workflow stable:
    - training-side
    - host TFLite replay
    - exact board tensor dump

## Step 5.61
- Reduced the board-side station credentials to a single configured AP as requested.
- Updated [include/river/river_wifi_credentials.h](/root/ameba-river/include/river/river_wifi_credentials.h):
  - primary SSID changed from `WLL2G` to `river`
  - primary password changed to `wobuzhidao`
  - secondary SSID/password cleared to empty strings
- Why clearing the secondary entry is enough:
  - [components/river_cloud/river_wifi_station.c](/root/ameba-river/components/river_cloud/river_wifi_station.c) already ignores empty SSIDs in `river_wifi_station_add_credential()`
  - so this change collapses the runtime credential set from `2` entries to `1` without touching the station state machine
- Expected runtime effect after flashing:
  - boot log changes from `autoconnect init: ap_count=2 primary=...` to `ap_count=1 primary=river`
  - scan/connect rotation no longer falls back to `ORVIBO`
  - all reconnect attempts stay pinned to the single configured SSID `river`

## Step 5.62
- Reverted the temporary TCP host-debug transport after the board/host bring-up attempt failed to produce a stable, trustworthy debug path.
- Revert scope:
  - removed the project-side TCP diag client and host helper
  - removed the extra `river tcpdiag ...` monitor surface
  - removed the dedicated TCP diag Kconfig/project config switches
  - restored the previous app boot path so the project returns to the serial/KWS debug baseline
- Why this step was taken:
  - the TCP path added another uncontrolled variable to wakeword debugging
  - the recent board-side wakeword issue is still a local KWS/feature/inference problem first
  - continuing to carry the TCP transport would mix infrastructure risk with model/runtime diagnosis
- The active working baseline after this revert is again:
  - single-AP Wi-Fi on `river`
  - local KWS debug mode available
  - pull-based tensor dump workflow available
  - no TCP host-debug transport in the firmware

## Step 5.63
- Added a dedicated board-side wakeword false-trigger investigation document:
  - [doc/KWS_BOARD_FALSE_WAKE_DEBUG_CHECKLIST_ZH.md](/root/ameba-river/doc/KWS_BOARD_FALSE_WAKE_DEBUG_CHECKLIST_ZH.md)
- This document consolidates the currently scattered evidence into one project-owned reference:
  - symptom evolution from the early `different input, same output` phase to the later `variable raw/score` phase
  - deployment issues already encountered on the model/export/runtime path
  - the current prioritized suspicion list
  - a staged analysis plan with explicit stop/continue gates
  - multiple debug methods and their individual exit mechanisms
  - the serial-debugging pitfalls already observed in practice
- The intent of this step is process control rather than code change:
  - future KWS diagnosis should follow the staged checklist
  - transport, cloud handoff, and experimental branches should no longer be mixed into the main false-trigger investigation path

## Step 5.64
- Added a dedicated FP32 wake-word deployment gate document for the current full product profile:
  - [doc/KWS_FP32_DEPLOYMENT_GATES_ZH.md](/root/ameba-river/doc/KWS_FP32_DEPLOYMENT_GATES_ZH.md)
- The document turns the previous qualitative conclusion into a concrete deployment contract for the algorithm/export side:
  - hard compatibility gates:
    - pure `FP32`, no hybrid
    - single-probability output
    - supported input shapes only
    - current resolver op whitelist only
  - memory gates derived from the current board baseline:
    - current mainline `KWS arena = 192KB`
    - current mainline `Silero VAD arena = 192KB`
    - recent full-profile runtime baseline:
      - `boot_ready heap_free ≈ 185216B`
      - `wifi_connected heap_free ≈ 132736B`
    - recommended direct-deploy FP32 KWS arena target:
      - `<= 224KB`
    - direct-deploy upper bound on the current full profile:
      - `<= 256KB`
    - beyond that, the model should no longer be treated as “swap-and-flash directly usable” on this branch
- The new document also distinguishes between:
  - secondary screening by `.tflite` file size
  - primary acceptance by actual `AllocateTensors()` arena demand
- It further defines the algorithm-side delivery package required for a one-shot board bring-up:
  - model/export provenance
  - IO shape/type summary
  - operator summary
  - threshold derivation
  - arena evidence
- Purpose of this step:
  - prevent future FP32 discussions from collapsing into “file size looks small enough”
  - give the training/export side a clear, board-derived target before producing the next model

## Step 5.65
- Added a dedicated board-memory explainer document:
  - [doc/RTL8730E_MEMORY_LAYERING_EXPLAINER_ZH.md](/root/ameba-river/doc/RTL8730E_MEMORY_LAYERING_EXPLAINER_ZH.md)
- The document consolidates the recent memory-capacity discussion into one project-owned reference and explicitly explains why:
  - the board can physically have `64MB` DRAM
  - while the current `CA32` runtime still only reports `100~200KB` of free heap
- The new document breaks the problem into the concrete layers that matter for model deployment:
  - physical DRAM capacity
  - current firmware-visible layout window
  - `CA32` carveout
  - static section occupancy
  - heap registration through `heap_5`
  - runtime free heap
  - largest-contiguous-free-block vs total free bytes
- It also includes a text memory-layer diagram tied back to the current project evidence:
  - boot log `0x60800000` vs `0x64000000`
  - `CA32_BL3_DRAM_NS` `4MB` carveout in the SDK layout
  - CA32 linker-script heap derivation via `__psram_heap_buffer_*`
  - runtime heap stats from the project
  - current KWS/VAD `TYPE_DRAM` arena pressure
- Purpose of this step:
  - stop future discussions from mixing “physical memory size” with “current application free heap”
  - provide a single reference that can be reused in KWS/FP32 deployment reviews

## Step 5.66
- Added a dedicated RTL8730E memory-layout options document:
  - [doc/RTL8730E_MEMORY_LAYOUT_OPTIONS_ZH.md](/root/ameba-river/doc/RTL8730E_MEMORY_LAYOUT_OPTIONS_ZH.md)
- This document consolidates the recent layout-adjustment discussion into one project-owned reference and answers:
  - whether the current branch should consider enlarging CA32-visible DRAM
  - why the answer is “yes, but not as the first response to the current FP32 model issue”
- The new document ties the recommendation back to current platform facts:
  - physical DRAM detection reaches `0x64000000`
  - current layout still caps `PSRAM_END` at `0x60800000`
  - current CA32 non-secure carveout is only `4MB`
  - CA32 heap is derived from the tail of that carveout
  - KM4 still has an explicit PSRAM heap-extend concept in the SDK
- It also adds a concrete option comparison table, including:
  - no-layout-change / module-trim validation path
  - in-window reshuffle path
  - conservative expansion path
  - `aivoice`-style expansion path
  - aggressive near-64MB expansion path
- For each path the document records:
  - expected benefit
  - platform risk level
  - recommended usage stage
  - verification focus
  - stop/rollback conditions
- Purpose of this step:
  - separate short-term model diagnosis from mid-term platform-capacity planning
  - provide a reusable decision document before any SDK-level memory-layout work is started

## Step 5.67
- Switched the board KWS experiment profile from the int8 baseline model to the pure-FP32 model:
  - `/root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_v3_fp32.tflite`
  - embedded as `components/river_voice/generated/bc_resnet_v3_fp32_model_data.h`
- Chose the first on-board FP32 deployment threshold from existing board recordings instead of reusing the int8 threshold:
  - deduped board recordings used for selection: `29` positive, `89` negative
  - zero-false-positive operating point on that set: `0.5217425823`
  - deployed threshold: `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=17096`
- Deliberately removed the most obvious KWS-side allocator pressure before judging CPU viability:
  - expanded `CONFIG_RIVER_KWS_TENSOR_ARENA_KB` from the old operating range to allow a much larger FP32 arena
  - set the current experimental arena to `768KB`
  - increased KWS worker stack to `12KB`
  - increased KWS input queue depth to `64` frames
- Refactored KWS runtime memory ownership so the large-but-simple buffers are no longer permanently embedded in the context object:
  - pre-roll ring backing storage now uses runtime allocation
  - input queue backing storage now uses runtime allocation
  - exact-tensor dump buffers now use lazy allocation only when the dump path is armed
- Added explicit FP32-oriented performance and memory observability:
  - `kws alloc` now prints `arena_used` and `arena_slack`
  - `kws memory plan` now prints init-time heap before/after, context size, queue/pre-roll/dump reservation sizes
  - periodic `kws perf` now prints last/avg/max inference time, slow-infer counters, heap watermark, and queue policy
  - runtime snapshot logs now include `river_kws` stack free bytes
  - slow inference warnings now trigger when single inference cost crosses `10ms` and `20ms`
- Kept the full product image enabled for this first FP32 viability pass:
  - VAD, XiaoZhi cloud path, and the rest of the shipping runtime are still present
  - the goal of this step is not “FP32 in a stripped lab image”, but “can FP32 survive in the actual product boot profile after relieving the obvious KWS memory bottleneck”
- Verified this step with a full local `RTL8730E` build on `2026-04-02`.
- Observed build artifacts after the successful build:
  - `build_RTL8730E/build/project_hp/image/km4_boot_all.bin` `51K`
  - `build_RTL8730E/build/project_lp/image/km0_image2_all.bin` `92K`
  - `build_RTL8730E/build/project_hp/image/km4_image2_all.bin` `371K`
  - `build_RTL8730E/build/project_ap/image/ap_image_all.bin` `3.0M`
  - `build_RTL8730E/km0_km4_ca32_app.bin` `3.5M`

## Step 5.68
- The first FP32 boot attempt exposed the next concrete limit after the memory expansion work:
  - boot failed before KWS runtime came up
  - CA32 reported `Malloc failed ... xWantedSize:786560`
  - this corresponds to the first large FP32 tensor-arena allocation attempt at `768KB`
- Treated that log as evidence that:
  - the platform no longer fails at the old `100~200KB` heap scale
  - but `768KB` is too aggressive for the current early-boot DRAM allocation window once allocator overhead is included
- Adjusted the FP32 experiment profile from `768KB` down to `688KB` so the board still gets a large FP32 arena while leaving explicit headroom for:
  - KWS queue backing storage
  - KWS pre-roll storage
  - KWS worker task creation
- Added finer-grained KWS init allocation logs so the next boot can distinguish these phases directly:
  - overall init plan before the arena allocation
  - post-FFT / pre-arena state
  - explicit `tensor arena alloc failed` log if the large block still cannot be reserved
  - post-arena state
  - runtime-buffer reservation summary
  - queue-storage / signal / task creation checkpoints
- Purpose of this step:
  - move from “one coarse malloc fail” to a staged boot-time memory trace
  - get the FP32 profile to boot so CPU latency can be judged from real `kws perf` logs rather than guessed from static model size

## Step 5.69
- Narrowed the current FP32 bring-up blocker from generic “tensor binding failed” to a more specific TFLM I/O allocation question.
- Added a new boot-time `kws io binding` log in [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc) that now prints:
  - `preserve_all`
  - runtime input/output tensor indices
  - runtime input/output tensor types
  - input/output `allocation_type`
  - input/output `bytes`
  - input/output raw data pointers
  - input/output dims pointers
  - input/output variable flags
  - `arena_used` and `arena_slack`
- Expanded the existing `kws tensor data invalid` failure log so it now preserves the same binding metadata at the exact failure point:
  - `input_data` / `output_data`
  - `input_raw` / `output_raw`
  - `input_alloc` / `output_alloc`
  - `input_idx` / `output_idx`
  - `arena_used` / `arena_slack`
- This makes the next board run able to distinguish at least these cases directly from one log capture:
  - wrapper exists and arena binding exists
  - wrapper exists but raw pointer is still null
  - runtime tensor is marked `dynamic`
  - failure correlates with an unexpectedly tiny or saturated arena plan
- In parallel, verified the current FP32 model itself on host:
  - `/root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_v3_fp32.tflite`
  - size `77848` bytes
  - sha256 `a61bdefb5bbc20c406128bb4d7619e7ea1649737672ac453e5335907e8bd0635`
  - full TF Lite host runtime can allocate it successfully
  - host runtime reports input `index=0`, output `index=76`, both `float32`
- Current conclusion after this step:
  - the FP32 flatbuffer is not obviously corrupt at the host-runtime level
  - the next board-side suspect remains TFLM runtime I/O buffer population or wrapper binding, not simple file corruption
- Verified this step with a full local `RTL8730E` build on `2026-04-03`.

## Step 5.70
- Implemented a conservative `RTL8730E` SDK memory-layout expansion to increase the current project's `CA32` heap without jumping directly to the larger `aivoice` layout.
- Added a repository-tracked SDK patch tool:
  - `tools/sdk/apply_rtl8730e_memory_layout_patch.py`
- Applied the SDK patch to `/root/ameba-rtos-1.2` with these effective values:
  - `PSRAM_END`: `0x60800000 -> 0x60C00000`
  - `CA32_BL3_DRAM_NS`: `0x60300000 ~ 0x60700000 -> 0x60300000 ~ 0x60B00000`
  - `KM4_DRAM_HEAP_EXT`: `0x60700000 ~ 0x60800000 -> 0x60B00000 ~ 0x60C00000`
  - `hal_platform.h` `PSRAM_END`: `0x60800000 -> 0x60C00000`
- Kept this step deliberately conservative so the next board run can answer one question clearly:
  - whether the current `FP32 KWS + Silero VAD` coexistence failure is primarily caused by the `CA32` carveout being too small
- Added a dedicated project document that explains the SDK-side edits and why only these files were changed:
  - `doc/RTL8730E_SDK_MEMORY_LAYOUT_CHANGES_ZH.md`
- Verified the patched SDK layout with:
  - `python3 tools/sdk/apply_rtl8730e_memory_layout_patch.py --check`
  - a full local `RTL8730E` rebuild on `2026-04-03`, which completed successfully across `ATF + CA32 + KM4 + KM0`

## Step 5.71
- The post-layout-expansion board log on `2026-04-03` shows the current blocker has moved away from memory:
  - `kws init plan: heap_free=4966336`
  - `boot_ready heap_free=3817792`
  - the earlier `Malloc failed ... xWantedSize:105536` no longer appears
  - FP32 KWS now completes init and produces varying scores, hashes, and tensor diagnostics
- The same board log exposed two immediate runtime limits instead:
  - wake scores only peaked around `q15=2157` / `score=0.0658`, far below the prior product threshold `q15=17096`
  - FP32 inference costs about `177ms`, which let the KWS queue grow to about `27/64` with the earlier `stride=4`
- Updated the current board smoke-test profile in `prj.conf`:
  - `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=1600`
  - `CONFIG_RIVER_KWS_INFERENCE_STRIDE_FRAMES=16`
- Kept both values explicitly marked in `prj.conf` as temporary smoke-test settings, not product tuning.
- Goal of this step:
  - prove or disprove that the end-to-end wakeword path can fire on board now that the memory ceiling is no longer the blocker
  - reduce scheduler pressure enough that the current FP32 profile can still be judged meaningfully before changing model or frontend behavior again
- Verified this step with a full local `RTL8730E` rebuild on `2026-04-03`, which completed successfully with `Build done`.

## Step 5.72
- The `2026-04-03 12:00:08` board log shows the lower smoke threshold and larger heap are active, but wake still does not trigger:
  - `threshold_q15=1600`
  - `thresh_pm=48`
  - `stride=16`
  - `kws pre-roll trim: dropped=12 keep=8/20`
  - observed gate-best scores only reached about `11pm`
- Treated that log as a timing-coverage problem, not another threshold problem:
  - `stride=16` reduced queue growth, but it sampled too sparsely on the slow `~178ms` FP32 path
  - each gate was trimming the configured `320ms` pre-roll from `20` frames down to only `8`, which likely discarded the wake phrase onset
  - VAD gate hold time remained short for a slow board path that often needs more than one aligned inference per utterance
- Implemented the next timing-debug profile:
  - `CONFIG_RIVER_KWS_INFERENCE_STRIDE_FRAMES=8`
  - `CONFIG_RIVER_SILERO_VAD_HANGOVER_FRAMES=18`
  - added new Kconfig/project config `CONFIG_RIVER_KWS_PRE_ROLL_FLUSH_MAX_FRAMES`
  - set `CONFIG_RIVER_KWS_PRE_ROLL_FLUSH_MAX_FRAMES=16` in `prj.conf`
- Updated `river_voice_kws.cc` so pre-roll flush depth is no longer hard-coded at `8`; it now follows the new project config and still reports the active value in the existing backend/profile logs.
- Goal of this step:
  - keep queue pressure under control while restoring enough temporal coverage to catch the wake phrase on board
  - test whether the current FP32 model can trigger once gate duration and pre-roll preservation are no longer the dominant bottlenecks

## Step 5.73
- The subsequent board log on `2026-04-03 13:34:47` already proved the current smoke profile can wake on board:
  - `wakeword hit: text=小欧家 score_pm=61 q15=2009`
  - `wakeword queued text=小欧管家 confidence=2009`
  - `interaction_state: wake_monitoring -> wake_confirmed`
  - `asr provider=xiaozhi_realtime session started`
- This step does not retune wake behavior again. It reduces serial noise while adding targeted visibility for transient runtime spikes inside `river_voice_kws.cc`.
- Added a compact peak-window logger for KWS:
  - new `kws peak:` line reports both instantaneous and window-peak values for:
    - `score_pm`
    - `gate_best_pm`
    - `infer_us`
    - queue depth
    - pre-roll depth
    - heap low-water mark
  - logs are emitted only when a meaningful new peak appears or on wake trigger
  - repeated peak logs are rate-limited with a minimum spacing of `500 ms`
- Replaced per-inference `kws infer slow:` spam with a throttled form:
  - still requires the existing slow-alert level
  - repeats only after the configured log interval or when latency worsens by at least `5000 us`
- Slowed the periodic KWS heartbeat in `prj.conf`:
  - `CONFIG_RIVER_KWS_LOG_PERIOD_MS: 2000 -> 5000`
- Expanded periodic `kws perf:` output so each heartbeat also preserves the last window's peak timing and queue context:
  - `infer_us ... win=...`
  - `heap ... win_low=...`
  - `queue ... win_peak=...`
  - `score[win_pm=... gate_best_pm=...]`
- Tightened the peak-reason logic so `infer` peaks are based on a real new latency peak, not on whether a previous slow log happened to print.
- Goal of this step:
  - keep background logs sparse enough for long serial captures
  - still expose the short-lived queue, latency, and score spikes that explain misses or regressions
- Verified this step with a full local `RTL8730E` rebuild on `2026-04-03`, which completed successfully with `Build done`.

## Step 5.74
- The first board run with the new peak logger on `2026-04-03 14:47:54` confirmed the mechanism works:
  - `kws peak: reason=score ...`
  - `wakeword hit: ...`
  - `kws peak: reason=trigger ...`
- That same log showed the remaining noise issue clearly:
  - a successful hit could still emit two back-to-back peak lines for the same window
  - the second `reason=trigger` line did not add materially new runtime data because `wakeword hit:` already marks the trigger
- Refined `river_voice_kws.cc` so trigger-forced peak logging now deduplicates against a just-printed peak line:
  - if a peak log was emitted within the minimum peak-log interval, the trigger path now only commits the peak snapshot internally
  - the separate `wakeword hit:` line remains unchanged
  - future peak detection still sees the updated trigger/score/infer baseline and does not repeatedly rediscover the same window
- Added a small internal helper to commit the current peak snapshot so logging and suppressed-trigger bookkeeping share one path.
- Goal of this step:
  - preserve the new transient diagnostics
  - remove the last obvious duplicate line during successful wake events
- Verified this step with a full local `RTL8730E` rebuild on `2026-04-03`, which completed successfully with `Build done`.

## Step 5.75
- The next board log on `2026-04-03 15:06:34` verified that duplicate `reason=trigger` peak logs are gone, but it exposed a remaining statistics bug:
  - later `kws peak: reason=queue ...` lines could still carry forward the previous window's `score_pm` / `gate_best_pm`
  - this happened because the next peak window was seeded from the last committed inference instead of starting clean
- Refined the KWS peak-window reset logic:
  - after a peak snapshot is committed, the next window now resets:
    - `infer_us = 0`
    - `score_q15 = 0`
    - `gate_best_q15 = 0`
  - queue depth, pre-roll depth, and heap low-water still restart from the current runtime baseline
- Goal of this step:
  - make each `kws peak` line describe only the current window
  - stop old wake scores from contaminating later queue-only or heap-only peak reports
- Verified this step with a full local `RTL8730E` rebuild on `2026-04-03`, which completed successfully with `Build done`.
- Flashed the rebuilt image to `/dev/ttyUSB0` on `2026-04-03`; `python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor` finished with `PASS`.

## Step 5.76
- Added a lightweight reset breadcrumb in `river_core` using SDK backup registers `BKUP_REG1-3`:
  - the app now records the latest interaction state and an 8-byte reason prefix on every interaction-state update
  - the next boot prints the previous recorded state together with `BOOT_Reason()`
  - this gives a project-side trace for resets that do not emit a panic or exception log
- Kept the implementation board-oriented and low-risk:
  - no SDK source changes
  - only reads/writes backup registers that already survive `system reset` / watchdog-class resets
  - status dump now includes the currently armed reset trace
- Board verification on `2026-04-03`:
  - full local `RTL8730E` rebuild completed with `Build done`
  - flashed to `/dev/ttyUSB0` and the flash tool finished with `PASS`
  - after issuing a manual `reboot` from monitor, boot log showed:
    - `KM4 BOOT REASON 400: APSYS`
    - `reset trace previous: boot_reason=0x0400 state=wake_monitoring reason8=network_ uptime_ds=62`
- Outcome of this step:
  - spontaneous resets can now be correlated with the last interaction phase visible to project code
  - this is specifically aimed at diagnosing the earlier no-panic reboot after follow-up timeout

## Step 5.77
- Addressed the `xiaozhi` websocket/uplink congestion path inside the project without patching the external SDK tree.
- Tightened websocket-side backpressure handling in `river_xiaozhi_ws.c`:
  - increased `RIVER_XIAOZHI_WS_QUEUE_MAX` from `4` to `8`
  - switched `ws_set_senddata_block_time()` to non-blocking for `xiaozhi`
  - added queue-watermark checks before `ws_sendBinary()` / `ws_send()`
  - audio uplink now reserves `2` queue slots so control JSON is less likely to be starved by audio bursts
  - added throttled diagnostics:
    - `xiaozhi ws backpressure: kind=... ready=... recycle=... max=...`
    - `xiaozhi_dump_status()` now prints websocket queue depth / peak / backpressure counters
- Added `xiaozhi uplink` retreat policy in `river_cloud_adapter.c`:
  - exponential backoff up to `160 ms` on `RIVER_ERR_BUSY`
  - trims stale queued uplink PCM down to `6` frames so ASR prefers fresh speech over delayed backlog
  - rate-limited runtime log:
    - `xiaozhi uplink backpressure: queued=... busy=... streak=... backoff=... stale_drop=...`
  - status dump now exposes:
    - uplink ring overflow drops
    - stale-drop count from congestion trimming
    - busy/fail counters
- Reset the new congestion bookkeeping when a fresh xiaozhi uplink session starts or transport state is torn down.
- Verified on `2026-04-03`:
  - full local `RTL8730E` rebuild completed successfully with `Build done`
  - flashed to `/dev/ttyUSB0`; `python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor` finished with `PASS`
  - live monitor capture after flashing showed:
    - the new project-side backpressure logs firing
    - no repeated SDK-side `WSCLIENT ERROR] ws_sendData: ERROR: Not get usable buffer...`
    - no sampled `xiaozhi playback write failed`
    - `river.voice.probe ... stream_busy=0` throughout the captured interaction window
- Current assessment:
  - websocket congestion still exists at the transport level, but it is now surfaced earlier and handled in a controlled way
  - the previous failure amplification path from queue-full -> SDK error spam -> playback error recovery is materially reduced

## Step 5.78
- Added a new long-horizon resource-constraint document for model research on the current `RTL8730E` board:
  - `doc/RTL8730E_LONG_TERM_MODEL_CONSTRAINTS_ZH.md`
- The new document is intentionally written for algorithm pre-research rather than for the current branch's one-off debugging:
  - separates physical hardware limits from the current project's conservative layout
  - separates structural platform limits from current software-policy bottlenecks
  - explains what parts of today's constraints are likely to move if the project later expands the memory layout or changes the runtime profile
- Captured the current board/resource picture with concrete numbers tied to the present branch:
  - physical external memory `64MB`
  - physical NOR flash `32MB`
  - current visible DRAM/PSRAM layout window `12MB`
  - current `CA32_BL3_DRAM_NS` carveout `8MB`
  - current `CA32` heap buffer `0x004DE000` (`5,103,616 bytes`)
  - current app package size `3,601,760 bytes`
  - current development flash-profile headroom `2,427,552 bytes`
  - current sampled runtime `heap_free` / `heap_min` values from the `2026-04-03` board log
- Added explicit guidance for model planning instead of only restating raw resources:
  - recommended additional flash budget for a new model
  - recommended additional runtime working-set budget
  - compute-budget targets for always-on VAD/KWS and for larger local models
  - guidance on when a new model should be treated as requiring a larger `CA32` carveout or a dedicated runtime profile
- Included two long-term engineering judgments that are directly relevant to the algorithm team:
  - replacing unsupported ops such as `MEAN` is usually preferable to board-side op-porting unless measured accuracy loss is materially unacceptable
  - `Cortex-A32 + NEON` means future `KleidiAI`-style CPU kernel optimization could help, but it does not remove flash / heap / cache-consistency / concurrency limits by itself

## Step 5.79
- Reviewed the two local `river_voice` changes before bringing the tree back to a clean git state.
- Kept the defensive tensor-name guard in `components/river_voice/river_voice_detector_silero.cc`:
  - the dump helper now uses a fallback name when `TF_LITE_STATIC_MEMORY` is enabled
  - in the current build it is effectively a no-op, but it avoids touching `tensor->name` if a future consistent static-memory build is introduced
- Rejected and removed the attempted `TF_LITE_STATIC_MEMORY` enable from `components/river_voice/CMakeLists.txt`:
  - enabling that macro only for `river_voice` is not safe because `TfLiteTensor` / related TFLM structs change layout under the macro
  - the SDK `tensorflow-microlite` static library was not being switched in lockstep, so keeping the define only in this component would risk an ABI mismatch
- Verified the cleaned state with a full local `RTL8730E` rebuild on `2026-04-03`; the rebuild completed successfully with `Build done`.
- Committed the review cleanup as a focused git step, then created branch `agent_server` from the cleaned result for later self-hosted-server debugging.

## Step 5.80
- Hardened the algorithm-side KWS evaluation flow in `/root/kws-training-pro` to eliminate train/validation leakage from repeated device-recording augmentations.
- Added `/root/kws-training-pro/kws_data_split.py`:
  - derives a stable origin key for each sample
  - uses `rec-*` recording ids for `device_recordings`
  - strips augmentation hash suffixes for non-device sources
  - performs grouped train/val splitting by `source + label + origin`
  - emits split summaries and asserts zero overlap
- Replaced `train_v2.py::prepare_data()` random per-sample splitting with grouped no-leakage splitting:
  - keeps all variants of one original recording on exactly one side
  - adds explicit `seed` handling and prints grouped split stats
  - this automatically hardens all training scripts that import `prepare_data` from `train_v2.py`
- Reworked `validate_final.py` so it no longer sweeps the full `device_recordings` pool:
  - it now derives a strict grouped holdout from the manifest
  - prints holdout bucket stats before evaluation
  - keeps threshold sweep behavior while making the evaluated set leakage-free
- Verified the new split logic on `device_recordings`:
  - `train_items=5250`, `val_items=600`
  - `train_groups=105`, `val_groups=12`
  - `shared_groups=0`

## Step 5.81
- Added a board-side KWS alignment replay path that reuses the existing exact tensor dump mechanism but makes the input deterministic.
- Chose the smallest practical implementation instead of a generic filesystem WAV player:
  - a single compiled-in mono `16 kHz / PCM16` wake-word sample
  - generated once from a real board recording
  - replayed through the same `river_voice_kws_submit_frame()` path used by live audio
- Added `tools/kws/generate_alignment_sample_header.py`:
  - converts one mono `16 kHz` WAV into `components/river_voice/generated/river_kws_alignment_sample_data.h`
  - prepends `20` synthetic silence frames (`320 ms`) so the KWS gate opens with deterministic pre-roll rather than with a tightly trimmed wake-word clip
- Added new board KWS debug APIs in `include/river/river_voice_kws.h` and `components/river_voice/river_voice_kws.cc`:
  - `river_voice_kws_dump_alignment_status()`
  - `river_voice_kws_run_alignment_sample(bool emit_dump)`
- The new replay path is intentionally guarded so the dump is not polluted by live mic input:
  - it refuses to run while `river audio probe` is still active
  - it refuses to run unless the interaction state is back in idle wake-monitoring
  - it drains the KWS worker queue before replay starts
  - it temporarily forces `local_debug_mode` so wake-word replay does not hand off into cloud dialogue
- Added worker-idle / snapshot-wait helpers in `river_voice_kws.cc` so replay can:
  - start from a clean KWS frontend state
  - pace frames in real time (`16 ms` per frame) instead of overflowing the `64`-frame KWS queue
  - wait for the first exact tensor snapshot before auto-dumping it
- Added automatic full dump emission after replay capture:
  - `kws tensor dump begin`
  - `kws tensor dump meta`
  - all `feat_f32`, `input_raw`, and `output_raw` chunks
  - then local debug / queue state is restored and the snapshot is cleared so normal wake-word handoff is not left blocked after the debug run
- Extended the serial diag command with:
  - `river kws align status`
  - `river kws align run`
- Verified on `2026-04-04`:
  - regenerated the compiled alignment sample header successfully
  - full local `RTL8730E` rebuild completed successfully with `Build done`

## Step 5.82
- Fixed the immediate board-side usability gap in the new KWS alignment flow: `river kws align run` no longer assumes local KWS was already initialized at boot.
- Root cause from the `2026-04-04` board log:
  - `river kws align status` showed `kws=closed probe=stopped interaction=wake_monitoring detection=ready`
  - so the failure was not the probe/interation guard; it was the closed KWS runtime itself
- Updated `components/river_voice/river_voice_kws.cc`:
  - `river_voice_kws_dump_alignment_status()` now prints an explicit hint when the KWS runtime is closed
  - `river_voice_kws_run_alignment_sample()` now performs lazy `river_voice_kws_init()` before replay when needed
  - lazy-init success and failure are logged explicitly:
    - `kws align lazy init: current_state=closed`
    - `kws align lazy init ok`
    - `kws align lazy init failed: status=...`
- Updated `components/river_diag/river_diag_cmd.c`:
  - `river kws align run` now prints the exact `river_status_t` failure code instead of the previous generic precondition hint
  - this makes the next board run actionable even if lazy init still fails for a deeper reason such as memory pressure
- Verified on `2026-04-04`:
  - full local `RTL8730E` rebuild completed successfully with `Build done`

## Step 5.83
- Fixed the actual KWS init regression behind the failed `river kws align run` attempts on `2026-04-04`.
- Root cause was confirmed from the local build artifacts, not guessed:
  - the SDK `tensorflow-microlite` library is compiled with `-DTF_LITE_STATIC_MEMORY`
  - `components/river_voice/river_voice_kws.cc` and `components/river_voice/river_voice_detector_silero.cc` were being compiled without that macro
  - this made board-side `TfLiteTensor` field access ABI-incompatible in those translation units
  - the resulting symptom matched the board log exactly:
    - `type=none alloc=unknown raw=0x0 dims=0x0`
    - `kws tensor data invalid`
    - `kws align lazy init failed: status=-3`
- Updated `components/river_voice/CMakeLists.txt`:
  - added `TF_LITE_STATIC_MEMORY` as a source-level compile definition for:
    - `river_voice_kws.cc`
    - `river_voice_detector_silero.cc`
  - kept the scope narrow so only the TFLM-facing translation units adopt the SDK tensor ABI
- Verified on `2026-04-04`:
  - regenerated build metadata and completed a full local `RTL8730E` rebuild with `Build done`
  - confirmed in `build_RTL8730E/build/compile_commands.json` that both KWS/VAD translation units now compile with `-DTF_LITE_STATIC_MEMORY`

## Step 5.84
- Tightened the board-side KWS alignment replay flow so `river kws align run` now keeps the best replay frame instead of dumping the first inference after gate-open.
- Root cause from the successful `2026-04-04` board replay log:
  - the replay path was using the generic one-shot `river_voice_kws_request_tensor_dump_next()`
  - the loop also stopped submitting frames as soon as the first snapshot became ready
  - this made the dump lock onto the early low-score frame:
    - `kws align replay captured: ... score=0.002818 q15=92`
  - while the same replay later reached the actual wake-word peak:
    - `wakeword hit: ... score_pm=910 q15=29835`
- Updated `components/river_voice/river_voice_kws.cc`:
  - added an internal tensor-dump mode split:
    - `next` keeps existing one-shot behavior for normal `river kws dump next`
    - `align_best` is used only by alignment replay and overwrites the snapshot when replay score improves
  - assigned a stable dump sequence when arming so one alignment run keeps one snapshot id even if the best frame is updated multiple times
  - extended tensor-dump status / capture logs to include the active dump mode for easier serial-side diagnosis
  - changed `river_voice_kws_run_alignment_sample()` to:
    - arm `align_best` instead of `next`
    - submit the full compiled sample plus all configured tail-silence frames
    - wait for worker idle at the end of replay instead of stopping on the first ready snapshot
    - fail explicitly if the replay finishes without producing any snapshot
- Verified on `2026-04-04`:
  - full local `RTL8730E` rebuild completed successfully with `Build done`

## Step 5.85
- Added an explicit host-side verification procedure for the now-corrected KWS alignment replay artifact.
- Recorded the key `2026-04-04` board-side success values that host replay must match:
  - `feat_hash=0x63dd772f`
  - `input_hash=0x3ec7297e`
  - `raw=911`
  - `score=0.910520`
  - `q15=29835`
  - `output_raw hex=df17693f`
- Documented one important operator constraint from the first host replay attempt:
  - the temporary file `/tmp/kws_align_dump_20260404_140825.log` is not a valid replay artifact
  - it only contains `feat_f32 chunk=1..179/245`
  - it contains no `input_raw` or `output_raw`
  - `tools/kws/replay_board_tensor_dump.py` therefore fails with `dump seq=1 incomplete: feat_f32, input_raw, output_raw`
- Added a stable capture-and-replay workflow to `.codex/verification.md` so the next run produces one complete monitor log and uses the same FP32 model variant as the board:
  - capture the entire `river kws align run` UART stream to a file until `kws align replay done: dump=emitted ...`
  - replay that file with `tools/kws/replay_board_tensor_dump.py`
  - override the script default model with `/root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_v3_fp32.tflite`, because the board runtime is using `bc_resnet_v3_fp32_experimental`, not the production-final quantized path

## Step 5.86
- Ran a board-state diagnostic using the user's required monitor path:
  - `ameba.py monitor -p /dev/ttyUSB0 -b 1500000`
- Confirmed the current blocker is no longer the host replay tool or the Step `5.84` board-side peak-frame capture logic.
- Actual monitor observations on `2026-04-04`:
  - standard monitor session connects successfully to `/dev/ttyUSB0` at `1500000`
  - the monitor's initial `AT+LIST` probe times out with `Failed to get cmd list: Get cmd list expired`
  - when rerun as `-reset -debug`, the same monitor shows only raw `0x00` bytes on RX
  - after the tool sends both:
    - `AT+LIST\r\n`
    - `reboot\r\n`
    there is still no printable board response, no `BOOT-I`, and no `ROM:[`
- Additional checks performed against the same board/session:
  - direct serial writes of:
    - `\\r`
    - `river kws align status\\r`
    - Realtek sync `ESC + \\r\\n`
  produced no readable monitor output
- Conclusion from this step:
  - the board is not currently in a usable interactive monitor state for `river kws align run`
  - because the RX stream is only `0x00`, no valid tensor dump can be captured, so host-side exact replay cannot proceed yet
  - this is a board/runtime-state blocker, not a host tooling mismatch

## Step 5.87
- Rechecked the board with a raw serial terminal at the same user-confirmed baudrate `1500000` and confirmed the shell is in fact interactive.
- New direct serial evidence from `2026-04-04`:
  - sending a bare `\\r` returns `#`
  - `river kws align status` responds with:
    - `kws align sample: source=compiled_pcm frame_samples=256 frames=145 duration_ms=2320 ...`
    - `kws align guard: kws=ready probe=running interaction=wake_monitoring detection=ready worker=idle snapshot=empty local_only=no`
- Captured one full live alignment replay log to:
  - `/tmp/kws_align_full_20260404_live.log`
- The captured board log is complete for host replay:
  - `kws align replay start:` present once
  - `kws tensor dump begin:` present once
  - `kws tensor dump meta:` present once
  - `input_raw chunk=1/245 ... 245/245`
  - `output_raw chunk=1/1`
  - `kws align replay done: dump=emitted`
- Root-caused the remaining host mismatch:
  - the board dump's `feat_f32` stream is correct
  - the board dump's `input_hash` equals the FNV hash of the raw `feat_f32` bytes:
    - `0x3ec7297e`
  - but the emitted `input_raw` stream itself hashes to a different value:
    - `0x0ea3e26b`
  - the first float of emitted `input_raw` is `df17693f`, which is the model output scalar, so this `input_raw` stream is not a faithful export of the real input tensor bytes
- Updated `tools/kws/replay_board_tensor_dump.py` to add a narrow float32 fallback:
  - if `input_raw` does not match `input_hash`
  - but raw `feat_f32` bytes do match `input_hash`
  - then replay uses `feat_f32` bytes as the effective input and reports `source=feat_f32_fallback`
- Verified on the captured live log:
  - `board_hash: feature=0x63dd772f input=0x3ec7297e`
  - `host_hash: ... effective_input=0x3ec7297e source=feat_f32_fallback`
  - `board_output: raw=911 score=0.910520 q15=29835`
  - `output_parity: bytes_equal=yes raw_equal=yes`
- This completes the host-side exact replay verification path for the current FP32 alignment artifact, while also documenting that the current board-emitted `input_raw` stream is anomalous.

## Step 5.88
- Fixed the board-side FP32 tensor dump capture point so `input_raw` is snapshotted before `Invoke()` can mutate or reuse the interpreter input buffer.
- Refactored exact dump capture into two phases inside `components/river_voice/river_voice_kws.cc`:
  - added dedicated staging buffers for the feature tensor and input tensor
  - populated those staging buffers during `river_voice_kws_fill_input_tensor()`
  - copied staging buffers into the final dump snapshot only when `river_voice_kws_capture_exact_tensors()` decides to capture the current inference
- Updated tensor-dump memory accounting and teardown accordingly:
  - reserved bytes now include both staging and final snapshot storage for feature/input
  - failure cleanup frees the new staging allocations
- Verified on `2026-04-04`:
  - full local `RTL8730E` rebuild completed successfully with `Build done`
  - flashed the new image to `/dev/ttyUSB0` at `1500000` using `tools/river_flash.py`
  - captured a fresh live alignment dump to `/tmp/kws_align_full.log`
  - board dump key values remained stable:
    - `feat_hash=0x63dd772f`
    - `input_hash=0x3ec7297e`
    - `raw=911`
    - `score=0.910520`
    - `q15=29835`
    - `output_raw hex=df17693f`
  - the corruption is gone:
    - `feat_f32 chunk=1/245` and `input_raw chunk=1/245` are byte-identical
    - `output_raw chunk=1/1` remains separate and equal to `df17693f`
  - host replay now consumes the real board-exported input tensor again:
    - `host_hash: ... logged_input=0x3ec7297e effective_input=0x3ec7297e source=input_raw`
    - `quant_parity: diff_bytes=0/15680 first_diff=[]`
    - `output_parity: bytes_equal=yes raw_equal=yes`

## Step 5.89
- Added a new Chinese runtime profiling document:
  - `doc/RUNTIME_RESOURCE_PROFILE_2026-04-04_ZH.md`
- The document consolidates the current local wakeup solution and implementation path using the actual code split:
  - interaction/window close -> `wake_monitoring`
  - `fixed_dsb` mono frontend
  - `silero VAD`
  - VAD-gated `bc_resnet_v3_fp32_experimental` KWS on TFLite Micro
  - wakeword queueing into XiaoZhi cloud reconnect
- Based on the user-provided `2026-04-04 15:42:06.287` to `15:42:20.844` runtime window, the document records:
  - overall CPU and heap averages / peaks
  - VAD task CPU and stack margin
  - KWS inference average / peak latency, queue buildup, pre-roll usage, and memory breakdown
  - interaction/cloud timing from wake hit to session queueing and websocket connect
  - stack margins for `vad`, `cap`, `kws`, and the `echo` zero-headroom risk
- Key quantitative conclusions captured in the document:
  - dual-core average busy rate in the sample window is about `9.0%`
  - sampled current free heap is stable around `3.52 MiB`
  - KWS average inference time is `178.764 ms`, above the `128 ms` stride budget by `50.764 ms` (`+39.7%`)
  - observed KWS queue peak reaches `32 / 64`
  - KWS reserved working set is about `816.4 KiB`
  - `echo` task stack free is `0 B` in both snapshots
- The document explicitly separates:
  - firmware-direct counters
  - values derived from the short sample window
  so the report does not overclaim long-run averages that are not actually present in the logs.

## Step 5.90
- Tuned the current wakeword operating point for higher recall on board by lowering
  `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15` in `prj.conf` from `1600` to `1024`.
- This moves the main trigger threshold from about `48 pm` down to about `31 pm`.
- Because the code-side fallback weak-threshold floor is hardcoded at `400 pm` but
  clamped to `primary - 1` when the primary threshold is lower, the effective weak
  threshold also drops with this change instead of staying at `400 pm`.
- Raised XiaoZhi downlink playback soft gain in
  `components/river_cloud/river_cloud_adapter.c` from `2/1` to `5/2` so cloud TTS
  output is louder without changing SDK-side speaker-volume plumbing.
- Updated `doc/KWS_PIPELINE_ZH.md` so the documented live KWS configuration matches
  the current firmware:
  - arena `688 KB`
  - primary threshold `1024`
  - stride `8`
  - queue `64`
  - pre-roll flush `16`
  - effective weak threshold behavior under the runtime clamp
- Rebuilt the firmware, reflashed the board successfully, and confirmed from the
  boot log that the new KWS threshold is live on device.

## Step 5.91
- Reviewed the new board wake logs after Step 5.90 and found the main blocker was
  no longer the threshold itself:
  - the eventual hit reached `score_pm=237`, far above the active `31 pm`
    threshold
  - but the FP32 worker stayed around `179-180 ms` per inference while running
    at stride `8`, so the queue kept growing into the `40+ / 64` range
  - the board started trimming queued audio (`kws input trim: dropped=27`), and
    the effective wake hit arrived late, after repeated user retries
- Retuned the runtime for freshness instead of lowering the threshold further:
  - changed `CONFIG_RIVER_KWS_INFERENCE_STRIDE_FRAMES` from `8` back to `16`
- Kept the lower threshold from Step 5.90 in place, because the logs show the
  current misses were dominated by stale inference / backlog rather than by a
  peak score barely missing the threshold.
- Updated `doc/KWS_PIPELINE_ZH.md` so the current-config table matches the new
  stride value.
- Rebuilt and reflashed the board, then confirmed from the boot log that the
  live runtime now reports `queue[frames=64 stride=16]`.

## Step 5.92
- After restoring `stride=16`, the next board logs showed the runtime backlog
  problem was materially improved:
  - queue stayed around `8-18 / 64`
  - no new `kws input trim` storm appeared during the sampled wake attempts
  - but natural wake attempts still topped out around `gate_best_pm=8` to
    `gate_best_pm=13`, which remained below the active `31 pm` threshold
- Lowered `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15` from `1024` to `384` so the
  live threshold moved from about `31 pm` down to about `11 pm` while keeping
  `CONFIG_RIVER_KWS_INFERENCE_STRIDE_FRAMES=16`.
- Updated `doc/KWS_PIPELINE_ZH.md` again so the documented current config now
  matches the latest board-tuned values:
  - threshold `384` (`~11 pm`)
  - stride `16`
- The new board log validates that this lower threshold is active and does
  improve real wake hits:
  - runtime status shows `thresh_pm=11 weak_pm=11`
  - a wake attempt reached `score_pm=28` and triggered immediately
  - the board entered the XiaoZhi wake flow and subsequent cloud/TTS session
    successfully
- During this step, firmware flashing at `1500000` intermittently failed with
  the existing `b'\\xe2'` transfer error on the large image. The successful
  deployment for this step used a lower flash baud only for programming; the
  normal debug monitor baud remains unchanged.

## Step 5.93
- Saved the current repository state as a clean milestone snapshot after the
  recent wake-word timing and threshold tuning work.
- Verified the worktree was already clean before snapshotting, so no source
  cleanup or rollback was needed.
- Added a focused archival commit for the snapshot record and tagged the current
  `refactor` baseline as `m7-realtime-wake-threshold-tuned`.
- The goal of this step is repository hygiene:
  - preserve a stable return point for later wake-word experiments
  - keep the worktree clean before the next board-debug cycle

## Step 5.94
- Added a persistent repository rule to [AGENTS.md](/root/ameba-river/AGENTS.md)
  for future wakeword-model debugging:
  - keep the existing board-side vs local comparison and parity code paths
  - do not delete or weaken tensor dump / alignment replay / comparison hooks
    just to speed up model bring-up
  - require an equivalent or stronger validation path before any future
    replacement of that infrastructure
- This step is a collaboration and debugging-discipline safeguard only.
- No firmware logic, serial-debug settings, model selection, or board runtime
  behavior was changed in this step.

## Step 5.95
- Added a parallel KWS model variant
  `student_bc_resnet_tiny_v2_fp32_debug` in
  [Kconfig](/root/ameba-river/Kconfig) for board/local parity work without
  replacing the committed mainline model selection.
- Imported the algorithm-side FP32 debug bundle as
  [student_bc_resnet_tiny_v2_fp32_model_data.h](/root/ameba-river/components/river_voice/generated/student_bc_resnet_tiny_v2_fp32_model_data.h).
- Extended
  [river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc)
  to support variant-specific frontend/runtime contracts:
  - the existing baseline, current FP32, and round6 branches keep the legacy
    `40x98`, `n_fft=512`, relative-dB normalization path
  - the new student debug branch uses the exported `40x101`, `n_fft=400`,
    centered STFT, natural-log, per-clip mean/std frontend
  - registered `MUL` and added a student-only TFLM `RfftFloat` path while
    keeping the existing WebRTC FFT path for the current chain
  - generalized profile logging so board/local parity output now reports the
    actual selected frontend contract instead of hardcoded `fft=512` /
    `frames=98`
- Kept the committed deployment default on
  `CONFIG_RIVER_KWS_MODEL_VARIANT_FP32_EXPERIMENTAL=y` and added an explicit
  `prj.conf` line to keep
  `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_TINY_V2_FP32_DEBUG`
  disabled in the default image.
- This step does not alter serial-debug commands and does not remove any
  existing board/local comparison tooling; it only adds a selectable parallel
  debug variant.

## Step 5.96
- Ran a board-side exact tensor parity session over the existing serial debug
  flow, without changing the serial monitor settings or removing any parity
  hooks.
- The boot log from the flashed firmware proved the board is currently running
  the committed mainline FP32 branch, not the parallel student branch:
  - variant=`bc_resnet_v3_fp32_experimental`
  - input shape=`1x40x98x1`
  - frontend=`fft=512`, `frames=98`, `center=no`
  - model size=`77848B`
- Executed the existing parity path on board:
  - `river kws debug local on`
  - `river audio probe stop`
  - `river kws dump next`
  - `river kws align run`
- The board emitted a complete exact tensor dump for alignment replay
  `seq=2 infer=3` with:
  - `feat_hash=0xb89e7474`
  - `input_hash=0x97354d89`
  - `raw=25`
  - `score=0.025023`
- Replayed that same dumped tensor locally against
  `/root/kws-training-pro/models/bc_resnet_iteration3/bc_resnet_v3_fp32.tflite`
  and verified:
  - feature hash matches
  - input hash matches
  - quant parity is exact (`diff_bytes=0/15680`)
  - output `raw` matches exactly (`25`)
  - float output bytes differ by only the first byte, with host score
    `0.025000` vs board score `0.025023`
- Restored the board to the normal debug state after the run:
  - `river kws debug local off`
  - `river audio probe start`
- Conclusion of this step:
  - the existing board/local exact-tensor parity path is working
  - the currently flashed firmware is not the student FP32 debug variant, so
    this parity result validates the mainline FP32 chain only
  - student-model parity on board requires reflashing the student build first

## Step 5.97
- Switched the committed KWS model selection in [prj.conf](/root/ameba-river/prj.conf)
  from `CONFIG_RIVER_KWS_MODEL_VARIANT_FP32_EXPERIMENTAL=y` to
  `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_TINY_V2_FP32_DEBUG=y`
  so the next board run can debug the new student FP32 deployment directly.
- Kept the existing board/local parity tooling, tensor dump path, and serial
  debug command flow unchanged; this step only changes which already-integrated
  model variant is compiled into the image.
- Rebuilt the full `RTL8730E` image set successfully with the student FP32
  debug variant selected.
- Verified the produced app image embeds
  `student_bc_resnet_tiny_v2_fp32_debug` and the new student-frontend logging
  string `log=natural norm=per_clip_mean_std`, confirming this is not just a
  stale rebuild of the previous `bc_resnet_v3_fp32_experimental` image.
- Produced new firmware artifacts:
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `4019552`
  - `build_RTL8730E/ota_all.bin` = `4019584`
- This step stops at compile validation only. Flashing and board-side exact
  parity on the student branch should be handled next as a separate runtime
  verification step.

## Step 5.98
- Extended the project-owned SDK layout helper
  [tools/sdk/apply_rtl8730e_memory_layout_patch.py](/root/ameba-river/tools/sdk/apply_rtl8730e_memory_layout_patch.py)
  with a new `aivoice_ca32_17mb` variant for large-model debug bring-up.
- This variant expands the external SDK runtime layout to:
  - `PSRAM_END = 0x61500000`
  - `CA32_BL3_DRAM_NS = 0x60300000 ~ 0x61400000` (`17MB`)
  - `KM4_DRAM_HEAP_EXT = 0x61400000 ~ 0x61500000` (`1MB`)
- Verified the rebuilt CA32 image now exposes
  `__psram_heap_buffer_size__ = 0x00d78000` (about `13.47 MiB`), and the
  board runtime correspondingly showed early `heap_free` around `13.5 MiB`.
- That larger layout alone did not fix the student FP32 model:
  - the board still failed `AllocateTensors`
  - the failure remained `Requested: 4700160, available 695604`
  - this proved the direct blocker was the KWS tensor arena cap, not the total
    CA32 heap size or the existing serial/parity tooling
- Raised the project Kconfig limit for `RIVER_KWS_TENSOR_ARENA_KB` from
  `2048` to `16384` in [Kconfig](/root/ameba-river/Kconfig) so large-model
  debug builds are configurable from the project side.
- Increased the current student FP32 debug deployment arena in
  [prj.conf](/root/ameba-river/prj.conf) from `688KB` to `8192KB`.
- Rebuilt and reflashed the student FP32 debug firmware with the larger arena
  while preserving the existing board/local comparison path and serial command
  flow.
- Board verification after reflashing showed the student FP32 chain now
  initializes and runs:
  - `kws align guard: kws=ready`
  - `kws perf: mem[arena=4709152/8192KB slack=3679456 ...]`
  - `river kws align run` completed and emitted tensor dump chunks instead of
    failing in `AllocateTensors`
  - first replayed inference produced `raw=371`, `score=0.371203`,
    `q15=12163`, and a held local-debug wakeword hit
- This step keeps the previously implemented board/local tensor dump and
  parity hooks intact, so later model debugging can continue to isolate model
  quality from deployment/adaptation mistakes.

## Step 5.99
- Hardened the host-side tensor replay tool
  [tools/kws/replay_board_tensor_dump.py](/root/ameba-river/tools/kws/replay_board_tensor_dump.py)
  for the preserved board/local KWS parity workflow, without changing board
  serial commands or dump emission.
- The replay tool now:
  - tolerates malformed dump chunks in the monitor transcript instead of
    aborting the whole parse immediately
  - accepts `float32` debug dumps whose `input_raw` stream is truncated by
    serial wrapping, as long as the recorded `input_hash` proves the board fed
    the same raw bytes as `feat_f32`
  - prints the exact `float32` output scalar in addition to the rounded
    `raw/score` presentation so parity conclusions are not confused by display
    precision
- Verified that the currently embedded board model header
  [components/river_voice/generated/student_bc_resnet_tiny_v2_fp32_model_data.h](/root/ameba-river/components/river_voice/generated/student_bc_resnet_tiny_v2_fp32_model_data.h)
  is byte-identical to the algorithm export
  `/root/kws-trainint/artifacts/exports/student_bc_resnet_tiny_v2/model.fp32.tflite`:
  - size `411560`
  - sha256 `2f21649bfbbbf69aae7f0fd1dc4ef318702cfd44c36fc1221c06ddcc215a4c51`
- Replayed the preserved board dump from
  `/tmp/kws_student_fp32_debug_replay.clean.log` against that exact host model
  and confirmed deployment parity for the student FP32 debug branch:
  - board `feature_hash=0x7ce0b11d`
  - board/effective input hash `0xd52f011c`
  - board exact output `0.371203`
  - host exact output `0.371203`
  - `output_parity: bytes_equal=yes raw_equal=yes`
- Conclusion of this step:
  - the new `student_bc_resnet_tiny_v2 FP32` board deployment is correct for
    the captured sample
- current board/local mismatch risk is no longer in model embedding or TFLM
  input adaptation for this path
- the remaining issues to investigate, if any, are model behavior /
  thresholding / runtime interaction rather than this deployment chain

## Step 5.100
- Added a new onboarding and operating guide for board/local KWS deployment
  parity work:
  [doc/KWS_BOARD_HOST_PARITY_DEBUG_GUIDE_ZH.md](/root/ameba-river/doc/KWS_BOARD_HOST_PARITY_DEBUG_GUIDE_ZH.md)
- The new guide consolidates the currently scattered knowledge into one place:
  - what the board/local parity mechanism proves
  - which board commands belong to the preserved debug flow
  - how `align run`, `dump next`, tensor dump, and host replay fit together
  - how to confirm the board-embedded model and host `.tflite` are identical
  - how to interpret `feat_f32`, `input_raw`, `output_raw`, `score`, `exact`,
    and `bytes_equal`
  - which conclusions are allowed before and after parity passes
  - common pitfalls that previously caused wasted debugging cycles
- Updated [doc/README.md](/root/ameba-river/doc/README.md) so the new guide is
  discoverable from the documentation index and prioritized alongside the
  existing KWS / frontend references.
- This step intentionally does not change board code, serial flow, or the
  parity mechanisms themselves. It packages the existing proven workflow into a
  reusable handoff document for future model bring-up.

## Step 5.101
- Added a focused realtime-analysis document for the current
  `student_bc_resnet_tiny_v2_fp32_debug` branch:
  [doc/KWS_STUDENT_FP32_REALTIME_ANALYSIS_ZH.md](/root/ameba-river/doc/KWS_STUDENT_FP32_REALTIME_ANALYSIS_ZH.md)
- The new document consolidates the current investigation into one place:
  - the measured board-side symptom: `infer_us` stays around `675 ms`
  - the current runtime budget implied by `stride=16` and `hop=10 ms`
  - why the bottleneck is model `Invoke()` cost rather than serial flow,
    tensor-arena initialization, or threshold tuning
  - the current frontend / model contract used by the student FP32 debug path
  - the structural reasons the graph is unfriendly to `RTL8730E + TFLM FP32`
  - why `101 -> 98` frames is only a secondary optimization lever
  - the recommended next steps: keep FP32 for parity debug, evaluate `INT8`,
    and ask the algorithm side to reduce graph cost structurally
- Updated [doc/README.md](/root/ameba-river/doc/README.md) so this analysis is
  discoverable from the main documentation index and grouped with the existing
  KWS bring-up / parity material.
- This step is documentation-only:
  - no board code was changed
  - no serial debug command flow was modified
  - the preserved board/local parity tooling remains the baseline for future
    model deployment investigation

## Step 5.102
- Added a same-caliber local realtime-estimate and board bring-up recommendation
  document for the `student_bc_resnet_tiny_v2` INT8 bundle:
  [doc/KWS_STUDENT_INT8_REALTIME_ESTIMATE_ZH.md](/root/ameba-river/doc/KWS_STUDENT_INT8_REALTIME_ESTIMATE_ZH.md)
- The new document grounds the INT8 recommendation in concrete local evidence:
  - exact bundle contract and quantization parameters from the algorithm export
  - host-side operator inventory showing the INT8 / FP32 models share the same
    graph shape and differ mainly by tensor dtype and kernel path
  - file-size comparison: `124392B` INT8 vs `411560B` FP32
  - interpreter tensor-byte comparison: `2,800,070B` INT8 vs `11,194,508B`
    FP32
  - host-side single-thread relative runtime checks:
    - optimized path about `3.54x` faster for INT8
    - builtin reference path about `1.69x` faster for INT8
  - bundle-side parity / threshold / board-reference metrics and export-gate
    status
- Based on those inputs, the document records a local board-side estimate and
  recommendation:
  - INT8 is clearly more worth boarding than the current student FP32 debug
    path
  - expected board `infer_us` is likely improved substantially but still not
    yet safe to assume it beats the current `160ms` stride budget
  - first smoke should preserve the existing FP32 parity tooling, use a
    parallel INT8 debug variant, start with a large arena, and gate decisions
    on `infer_us`, queue growth, heap headroom, and int8-kernel stability
- Updated [doc/README.md](/root/ameba-river/doc/README.md) so the INT8
  estimate is discoverable alongside the existing FP32 realtime and parity
  debugging documents.
- This step is documentation-only:
  - no firmware code was changed
  - no serial debug command flow was modified
  - the existing board/local deployment-parity mechanism remains the required
    baseline before any INT8 board-effect conclusions are accepted

## Step 5.103
- Added a parallel board-debug KWS variant for the algorithm export
  `student_bc_resnet_tiny_v2` INT8 bundle without replacing the preserved FP32
  debug path:
  - [Kconfig](/root/ameba-river/Kconfig) now exposes
    `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_TINY_V2_INT8_DEBUG`
  - [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc)
    now wires that variant to the exported INT8 TFLite blob while reusing the
    same `40x101`, centered log-mel, per-clip norm frontend contract as the
    student FP32 debug path
  - [prj.conf](/root/ameba-river/prj.conf) switches the active deployment
    build to the INT8 debug variant, starts from a `2048KB` arena, and uses
    the bundle threshold `q15=9444`
- Added the embedded model header generated from the algorithm bundle:
  [components/river_voice/generated/student_bc_resnet_tiny_v2_int8_model_data.h](/root/ameba-river/components/river_voice/generated/student_bc_resnet_tiny_v2_int8_model_data.h)
- Built, flashed, and exercised the INT8 firmware on the board while keeping
  the existing board/local parity workflow intact:
  - boot log confirms
    `variant=student_bc_resnet_tiny_v2_int8_debug`
  - runtime tensor contract confirms `input=int8`, `output=int8`,
    `shape=1x40x101x1`
  - the preserved `river kws debug local on` + `river audio probe stop` +
    `river kws align run` flow still emits `feat_f32`, `input_raw`, and
    `output_raw` chunks for host replay
- Board/local parity passed for the INT8 path:
  - host replay from
    [tools/kws/replay_board_tensor_dump.py](/root/ameba-river/tools/kws/replay_board_tensor_dump.py)
    reported `quant_parity: diff_bytes=0/4040`
  - replay also reported `output_parity: bytes_equal=yes raw_equal=yes`
  - board and host both produced `raw=-28`, `score=0.390625`, `q15=12800` on
    the captured alignment sample
- The deployment path is therefore correct, but realtime is not:
  - board `infer_us` on this INT8 variant is about `2.34s`
  - queue trimming still occurs aggressively before inference
  - this means the current student INT8 graph is board-correct but far from
    usable realtime on `RTL8730E`
- Restored the board to normal runtime after the test by turning `local_only`
  back off and restarting `audio probe`, so the board is not left in the
  parity-only state.

## Step 5.104
- Added a focused post-measurement analysis document for the current student
  INT8 board result:
  [doc/KWS_STUDENT_INT8_BOARD_REALTIME_ANALYSIS_ZH.md](/root/ameba-river/doc/KWS_STUDENT_INT8_BOARD_REALTIME_ANALYSIS_ZH.md)
- This document records the actual root cause behind the poor board-side INT8
  realtime result, instead of leaving the older estimate as the latest story:
  - deployment parity is already correct, so the issue is no longer in
    quantization wiring or board adaptation
  - current board `infer_us` is about `2.34s`, much worse than the earlier
    student FP32 `~675ms`
  - the key runtime reason is in the SDK CA32 kernels:
    - the INT8 `conv` path is explicitly forced to
      `reference_integer_ops::ConvPerChannel(...)`
    - the INT8 `depthwise` path is explicitly forced to
      `reference_integer_ops::DepthwiseConvPerChannel(...)`
    - student-heavy `MUL`, `LOGISTIC`, and `ADD` are also still on reference
      style paths
  - meanwhile the CA32 FP32 `conv` path still keeps an optimized
    `Im2col + cpu_backend_gemm::Gemm` implementation
  - so the current result is not “INT8 quantization is useless”, but “this
    board is currently running a heavy student graph on reference INT8
    kernels”
- Updated [doc/README.md](/root/ameba-river/doc/README.md) so the new INT8
  board-realtime postmortem is discoverable next to the existing FP32
  realtime and INT8 estimate documents.
- This step is documentation-only:
  - no firmware code was changed
  - no serial debug mechanism was changed
  - it preserves the existing parity workflow as the baseline deployment proof

## Step 5.105
- Switched the active deployment build back from the temporary student INT8
  debug variant to the preserved student FP32 debug variant in
  [prj.conf](/root/ameba-river/prj.conf):
  - `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_TINY_V2_FP32_DEBUG=y`
  - `# CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_TINY_V2_INT8_DEBUG is not set`
  - kept the large `8192KB` arena and the last known usable FP32 measurement
    settings (`stride=16`, `queue=64`, `pre_roll_flush=16`, `threshold_q15=384`)
- Rebuilt and reflashed the board with the FP32 metrics build, then captured a
  fresh boot/runtime log at `/tmp/kws_student_fp32_debug.log`.
- Measured the current board-side student FP32 resource profile while keeping
  the existing serial and parity workflow intact:
  - boot log confirms
    `variant=student_bc_resnet_tiny_v2_fp32_debug`
  - boot log confirms the student contract
    `input=float32`, `output=float32`, `shape=1x40x101x1`
  - KWS init uses `arena_used=4709152B`, `arena_slack=3679456B`
  - `align` replay measured `infer_us=675892`
  - restored live runtime measured `infer_us=675354`
  - cumulative board average after two inferences is `675623us`
  - live queue peak reached `43/64`
- Replayed the captured board tensor dump on host against the exact FP32
  bundle:
  - `quant_parity: diff_bytes=0/16160`
  - `output_parity: bytes_equal=yes raw_equal=yes`
  - board and host both produced `raw=371`, `score=0.371203` on the captured
    alignment sample
- Added a dedicated dated performance record:
  [doc/RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_FP32_DEBUG_ZH.md](/root/ameba-river/doc/RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_FP32_DEBUG_ZH.md)
  which records:
  - the exact measurement commands
  - bundle checklist vs board actuals
  - KWS init memory plan and heap snapshots
  - `align` and live inference latency
  - the reminder that this build keeps a deliberately low debug threshold and
    should not be used for quality conclusions
- Updated [doc/README.md](/root/ameba-river/doc/README.md) so the new FP32
  board-profile document is indexed alongside the existing parity and
  realtime-analysis material.

## Step 5.106
- Added a parallel `student_bc_resnet_nano_v2_fp32_debug` board-debug variant
  without disturbing the preserved student/tiny parity workflow:
  - [Kconfig](/root/ameba-river/Kconfig) now exposes
    `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_NANO_V2_FP32_DEBUG`
  - [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc)
    now maps that variant to the exported nano FP32 bundle and keeps the same
    `40x101`, `fft=400`, centered log-mel, per-clip normalization contract
  - this keeps the existing board/local parity tooling applicable to the nano
    branch instead of introducing a parallel debug mechanism
- Imported the generated nano FP32 model header into the repository so the
  firmware build stays reproducible:
  [components/river_voice/generated/student_bc_resnet_nano_v2_fp32_model_data.h](/root/ameba-river/components/river_voice/generated/student_bc_resnet_nano_v2_fp32_model_data.h)
- Switched the active deployment-test build in
  [prj.conf](/root/ameba-river/prj.conf) to the nano FP32 debug branch:
  - `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_NANO_V2_FP32_DEBUG=y`
  - `CONFIG_RIVER_KWS_TENSOR_ARENA_KB=4096`
  - `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=8851`
  - preserved the already-validated parity-friendly scheduling settings
    (`stride=16`, `pre_roll_flush=16`, `queue=64`)
- Rebuilt the full `RTL8730E` firmware successfully after the nano-variant
  integration:
  - `river_voice_kws.o` compiled with the new symbol path
  - the full image build completed with `Build done`
- This step is integration-only:
  - no board flash yet
  - no serial-debug flow changes
  - parity confirmation and board performance measurement are handled next

## Step 5.107
- Completed the first full board deployment validation for
  `student_bc_resnet_nano_v2_fp32_debug` while preserving the existing
  board/local comparison workflow:
  - captured a fresh nano FP32 boot/runtime log at `/tmp/kws_nano_fp32_debug.log`
  - boot facts confirm the intended deployment contract:
    - `variant=student_bc_resnet_nano_v2_fp32_debug`
    - `runtime_in=float32 runtime_out=float32`
    - `shape=[1,40,101,1]`
    - `fft=400 hop=160 center=yes`
    - `threshold_q15=8851`
  - init memory facts from the board log show:
    - `arena_used=3139392B`
    - `arena_slack=1054912B`
    - `boot_ready heap_free=9641216B`
- Verified that the board-embedded model is byte-identical to the algorithm
  bundle FP32 `.tflite`:
  - header bytes = model bytes = `108828`
  - both hashes are
    `4ff052777e4796db44d5899e94c15eb62c3d24431edcc065d9388671c4df3945`
- Ran the preserved standard parity path on the board without changing the
  serial workflow:
  - `river kws debug local on`
  - `river audio probe stop`
  - `river kws align run`
  - `river kws debug local off`
  - `river audio probe start`
- The nano FP32 alignment sample proves the deployment path is working:
  - board `infer_us=277865`
  - board `raw=363 score=0.363446 q15=11909`
  - board produced a wake hit on the compiled sample
  - `feature hash=0x7ce0b11d`
  - `input hash=0xd52f011c`
- Hardened the host replay tool for future FP32 exact-parity work without
  touching the board-side debug path:
  - [tools/kws/replay_board_tensor_dump.py](/root/ameba-river/tools/kws/replay_board_tensor_dump.py)
    now supports `--builtin-ref`
  - this forces the host-side TFLite replay onto the builtin reference
    resolver to avoid false `bytes_equal=no` reports caused by host delegates
  - the script now also prints `host_runtime: builtin_ref=yes|no` and explains
    the retry path when a float32 dump mismatches only under delegate mode
- Replayed the captured nano dump on host against the exact FP32 bundle and
  confirmed exact parity under the new stable host mode:
  - `quant_parity: diff_bytes=0/16160`
  - `output_parity: bytes_equal=yes raw_equal=yes`
  - board and host both produced `raw=363`
  - board and host exact output both decode to `0.363446`
- Confirmed the board is restored to the normal runtime path after parity
  testing:
  - subsequent live logs no longer show `wakeword handoff held: reason=local_debug`
  - the board resumes normal `wakeword queued` and
    `xiaozhi conversation window opened` behavior
  - a later live sample still measured `infer_us=277681`, which is consistent
    with the `align` sample latency level
- Added two documentation updates so future model bring-up can reuse the same
  path reliably:
  - new board profile:
    [doc/RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_NANO_FP32_DEBUG_ZH.md](/root/ameba-river/doc/RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_NANO_FP32_DEBUG_ZH.md)
  - updated parity guide:
    [doc/KWS_BOARD_HOST_PARITY_DEBUG_GUIDE_ZH.md](/root/ameba-river/doc/KWS_BOARD_HOST_PARITY_DEBUG_GUIDE_ZH.md)
    now explicitly recommends `--builtin-ref` for FP32 exact parity
- Updated [doc/README.md](/root/ameba-river/doc/README.md) so the nano FP32
  runtime profile is indexed beside the existing student/tiny and INT8
  records.

## Step 5.108
- Added a focused constraint document that explains, with current board and
  SDK evidence, why `INT8`/`INT16` deployment is not automatically a realtime
  win on the current `RTL8730E` path:
  [doc/KWS_INT8_INT16_DEPLOYMENT_CONSTRAINTS_ZH.md](/root/ameba-river/doc/KWS_INT8_INT16_DEPLOYMENT_CONSTRAINTS_ZH.md)
- This document consolidates three layers of evidence into one place:
  - board facts already measured in this repo:
    - `student_bc_resnet_tiny_v2_int8_debug` parity is correct but
      `infer_us` is about `2.34s`
    - `student_bc_resnet_tiny_v2_fp32_debug` is about `675ms`
    - `student_bc_resnet_nano_v2_fp32_debug` is about `278ms`
  - current CA32 SDK kernel reality:
    - `int8 conv` is explicitly forced to
      `reference_integer_ops::ConvPerChannel(...)`
    - `int8 depthwise` is explicitly forced to
      `reference_integer_ops::DepthwiseConvPerChannel(...)`
    - `int8/int16` `MUL`, `LOGISTIC`, `ADD`, and pooling remain reference-style
      or generic paths
  - current project-side deployment-chain reality:
    - KWS app currently accepts only `uint8`, `int8`, and `float32` tensor I/O
    - current host replay tool likewise supports only `uint8`, `int8`, and
      `float32`
    - therefore `INT16` is not just “probably slow”, but “not an accepted
      deployment target on the current app/tooling path”
- The new document translates these facts into explicit downstream rules for
  algorithm training and candidate selection:
  - no `INT16` delivery for the current board path
  - `INT8` remains the only quantized delivery target worth considering
  - future models must stay within the currently integrated resolver subset
  - future candidates must reduce high-resolution compute early and avoid
    heavy `LOGISTIC + MUL` gating
  - board `infer_us`, not offline budget declarations alone, is the real
    acceptance gate
  - exact board/host parity remains mandatory before any threshold or quality
    discussion
- Updated [doc/README.md](/root/ameba-river/doc/README.md) so this new
  constraint document is indexed next to the existing parity, FP32, and INT8
  runtime records.

## Step 5.109
- Added a parallel DS-CNN tiny FP32 debug deployment variant without touching
  the mainline production KWS path:
  - [components/river_voice/generated/student_dscnn_tiny_v2_fp32_model_data.h](/root/ameba-river/components/river_voice/generated/student_dscnn_tiny_v2_fp32_model_data.h)
  - [Kconfig](/root/ameba-river/Kconfig)
  - [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc)
  - [prj.conf](/root/ameba-river/prj.conf)
- The new variant keeps the already-validated student debug contract intact:
  - `40x101`
  - `fft=400`
  - `hop=160`
  - `center=yes`
  - `per_clip_mean_std`
  - `float32 -> float32`
- Preserved the existing board / host parity workflow rather than introducing
  a special-case path for DS-CNN:
  - `river kws debug local on`
  - `river audio probe stop`
  - `river kws align run`
  - host replay through
    [tools/kws/replay_board_tensor_dump.py](/root/ameba-river/tools/kws/replay_board_tensor_dump.py)
- First boot with `1024KB` arena failed at `AllocateTensors`; the DS-CNN tiny
  FP32 path now uses a debug-only `2048KB` arena so bring-up can proceed to
  parity and runtime measurement.
- Rebuilt, flashed, and verified the new DS-CNN tiny FP32 variant boots
  successfully:
  - `variant=student_dscnn_tiny_v2_fp32_debug`
  - `input shape=[1,40,101,1]`
  - `runtime_in=float32 runtime_out=float32`
  - `arena_used=1168336B`
  - `arena_slack=928816B`
- Verified the embedded board model bytes match the algorithm bundle exactly:
  - header bytes `12376`
  - model bytes `12376`
  - shared `sha256=836b18e8c315c9142f5744bfa0183a35e4d011019c2526e33f86dd3da166c7c7`
- Ran the preserved `align` parity flow on board and replayed the resulting
  dump on host against the exact FP32 bundle:
  - board `raw=297 score=0.296720 q15=9723`
  - host `raw=297 exact=0.296720`
  - `feature hash=0xf6cf59f0`
  - `effective input hash=0x8aa04513`
  - `output_parity: bytes_equal=no raw_equal=yes`
- The DS-CNN tiny dump lost `input_raw` chunks `146` and `147` in serial
  capture, but the replay tool correctly fell back to `feat_f32` because the
  reconstructed feature bytes reproduced the board input hash exactly. This is
  treated as a serial dump completeness issue, not a deployment mismatch.
- Captured the first board-side runtime profile for this candidate:
  - `infer_us[last=183988 avg=183969 max=183988]`
  - `stride=16`, so the current debug cadence still has margin to the next
    `256ms` infer slot
  - still about `10.22x` slower than the bundle `18ms` CPU budget
- Added the first DS-CNN tiny FP32 board profile document:
  - [doc/RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_DSCNN_TINY_FP32_DEBUG_ZH.md](/root/ameba-river/doc/RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_DSCNN_TINY_FP32_DEBUG_ZH.md)
- Updated [doc/README.md](/root/ameba-river/doc/README.md) so the new DS-CNN
  tiny runtime profile is indexed beside the existing student bring-up and
  constraint records.

## Step 5.110
- Added a parallel DS-CNN small FP32 debug deployment variant so the next
  DS-CNN candidate can be brought up without touching the mainline KWS path:
  - [components/river_voice/generated/student_dscnn_small_v2_fp32_model_data.h](/root/ameba-river/components/river_voice/generated/student_dscnn_small_v2_fp32_model_data.h)
  - [Kconfig](/root/ameba-river/Kconfig)
  - [components/river_voice/river_voice_kws.cc](/root/ameba-river/components/river_voice/river_voice_kws.cc)
  - [prj.conf](/root/ameba-river/prj.conf)
- Kept the existing student debug frontend / parity contract unchanged:
  - `40x101`
  - `fft=400`
  - `hop=160`
  - `center=yes`
  - `per_clip_mean_std`
  - `float32 -> float32`
- Rebuilt, flashed, and verified the new DS-CNN small FP32 variant boots
  successfully:
  - `variant=student_dscnn_small_v2_fp32_debug`
  - `input shape=[1,40,101,1]`
  - `runtime_in=float32 runtime_out=float32`
  - `arena_used=1945648B`
  - `arena_slack=151504B`
- Verified the embedded board model bytes match the algorithm bundle exactly:
  - header bytes `23600`
  - model bytes `23600`
  - shared `sha256=e0a2bedd32801d3d05ca4b0b3369137bedba990d28e02b5253696dc021e56925`
- Reused the preserved board / host parity path instead of introducing a new
  special-case debug flow:
  - `river kws debug local on`
  - `river audio probe stop`
  - `river kws align run`
  - host replay through
    [tools/kws/replay_board_tensor_dump.py](/root/ameba-river/tools/kws/replay_board_tensor_dump.py)
- The board-side `align` sample triggered correctly on this model:
  - board `raw=333 score=0.333065 q15=10914`
  - threshold `q15=10534`
  - wake hit recorded on board
- Host replay against the exact FP32 bundle confirms deployment correctness:
  - `feature hash=0xf6cf59f0`
  - `effective input hash=0x8aa04513`
  - board `raw=333 exact=0.333065`
  - host `raw=333 exact=0.333065`
  - `output_parity: bytes_equal=no raw_equal=yes`
- One full-capture quirk was observed:
  - the serial log did not emit an explicit `kws tensor dump end:` line
  - but `input_raw` still reached `253/253`, `output_raw` was present, and host
    replay succeeded
  - this is treated as a serial logging quirk, not a deployment mismatch
- Captured the board-side runtime profile for this candidate:
  - `infer_us[last=389384 avg=389256 max=389454]`
  - about `11.12x` slower than the bundle `35ms` CPU budget
  - about `2.12x` slower than the already-tested `student_dscnn_tiny_v2_fp32_debug`
  - `arena_used=1945648B`, about `4.95x` above the bundle `384KB` memory budget
- Added the DS-CNN small FP32 board profile and model-selection recommendation:
  - [doc/RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_DSCNN_SMALL_FP32_DEBUG_ZH.md](/root/ameba-river/doc/RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_DSCNN_SMALL_FP32_DEBUG_ZH.md)
- Updated [doc/README.md](/root/ameba-river/doc/README.md) so the new DS-CNN
  small runtime profile is indexed next to the existing student KWS records.
- Re-checked whether "latest ADK" actually makes `INT8 / INT16` usable, without
  disturbing the current in-use dirty SDK tree:
  - current in-use SDK `/root/ameba-rtos-1.2` at
    `8624cbeccf840c929db1624e05cc5b681024a3bf`
  - clean latest `release/v1.2` clone `/tmp/ameba-rtos-1.2-latest` at
    `8ef72a545c384ec439eef9a200baf4f569e21a73`
  - both still point `component/tflite_micro` at
    `dbda29aa7240ad14cf21cf3636ff2792a05ddcc1`
- Also checked the locally available upstream refs already present in the SDK:
  - SDK `origin/master` is `2def66020a2bd6b37894e8dc8341c49130ca8405`
  - `tflite_micro origin/main` is `8b38d3dac9ea733e93ad73c2b637ef1a28753fb3`
  - but the key quantization files still show no diff versus `dbda29a`:
    - `conv.cc`
    - `depthwise_conv.cc`
    - `reduce_common.cc`
    - `im2col_utils.h`
- Captured the practical implication in a new document:
  - [doc/KWS_LATEST_ADK_QUANTIZATION_RECHECK_ZH.md](/root/ameba-river/doc/KWS_LATEST_ADK_QUANTIZATION_RECHECK_ZH.md)
  - the document separates three states that must not be conflated:
    - clean official `release/v1.2`
    - locally fetched official `origin/master / origin/main`
    - current dirty SDK with local correctness patches
- The re-check makes the current engineering status explicit:
  - current official refs visible on this machine do not prove that the INT8
    optimized CA32 path is fixed
  - current proven INT8 board correctness still depends on the existing local
    SDK patches that preserve board/host parity
  - current `ameba-river` KWS integration still rejects `INT16` at app/tooling
    level, so `INT16` is not a deployable target yet even if some lower-layer
    kernels exist
- Updated [doc/README.md](/root/ameba-river/doc/README.md) so this latest ADK
  quantization re-check is indexed next to the existing INT8 / INT16
  constraint and runtime-analysis documents.
- Added project-owned SDK path override support so `ameba-river` can build and
  flash against a clean SDK tree without touching `/root/ameba-rtos-1.2`:
  - [env.sh](/root/ameba-river/env.sh)
  - [tools/river_flash.py](/root/ameba-river/tools/river_flash.py)
  - [tools/generate_rdev.py](/root/ameba-river/tools/generate_rdev.py)
  - [components/river_cloud/CMakeLists.txt](/root/ameba-river/components/river_cloud/CMakeLists.txt)
- Switched the active deployment retry to the clean-SDK INT8 student bundle in
  [prj.conf](/root/ameba-river/prj.conf):
  - `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_TINY_V2_INT8_DEBUG=y`
  - `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=9444`
  - kept the preserved debug arena / stride / pre-roll / queue settings so the
    retry did not disturb the existing board/host parity mechanism
- Verified the clean SDK build path works end to end:
  - SDK root `/tmp/ameba-rtos-1.2-latest`
  - SDK commit `8ef72a545c384ec439eef9a200baf4f569e21a73`
  - project build completed with `Build done`
- Reflashed the board with the clean-SDK INT8 image using the existing serial
  workflow:
  - board first had to be switched into download mode via the preserved
    `reboot uartburn` monitor command
  - `tools/river_flash.py` then completed with `Finished PASS`
- Captured the critical clean-SDK board symptom and recorded it in
  [doc/KWS_LATEST_ADK_QUANTIZATION_RECHECK_ZH.md](/root/ameba-river/doc/KWS_LATEST_ADK_QUANTIZATION_RECHECK_ZH.md):
  - after successful flash, the board did not enter normal `ameba-river`
    runtime / monitor text logging
  - raw serial only showed command echo or continuous `0x00` bytes
  - clean SDK `monitor.py --debug` sent `AT+LIST` but received no monitor list,
    only repeated `00 / 00 00 / 00 00 00 / 00 00 00 00`
- The clean-SDK INT8 retry therefore tightened the engineering conclusion:
  - this is not just "INT8 still lacks speedup"
  - on the clean official SDK path, this student INT8 image does not yet reach
    the minimum bar of booting into a normal runtime state that can be used for
    board/host parity
- Ran a clean-SDK FP32 control experiment to separate "quantization issue" from
  "broader clean-SDK runtime issue":
  - switched [prj.conf](/root/ameba-river/prj.conf) back to
    `student_bc_resnet_tiny_v2_fp32_debug`
  - restored the last known usable FP32 settings:
    - `CONFIG_RIVER_KWS_TENSOR_ARENA_KB=8192`
    - `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=384`
- Rebuilt and reflashed the clean-SDK FP32 control image successfully:
  - build completed with `Build done`
  - flash completed with `Finished PASS`
- The FP32 control image showed the same post-flash failure signature as the
  clean-SDK INT8 retry:
  - immediate serial degradation into continuous `0x00` bytes
  - no `ameba-river boot` text log
  - no usable runtime / monitor state for board-host parity
- This control result materially changed the conclusion:
  - the current clean-SDK failure is not isolated to INT8 kernels
  - the repo still depends on broader dirty-SDK runtime compatibility changes
    beyond the quantization correctness patches already documented
- Extended the project-owned memory layout helper so it can target arbitrary SDK
  clones without touching the primary dirty SDK:
  - [tools/sdk/apply_rtl8730e_memory_layout_patch.py](/root/ameba-river/tools/sdk/apply_rtl8730e_memory_layout_patch.py)
    now accepts `--sdk-root`
  - this allows the clean SDK clone under `/tmp/ameba-rtos-1.2-latest` to be
    checked/patched in a controlled way
- Verified the memory-layout delta explicitly before retrying the clean SDK:
  - dirty SDK `/root/ameba-rtos-1.2` reports
    `--variant aivoice_ca32_17mb --check` as `applied`
  - clean SDK `/tmp/ameba-rtos-1.2-latest` initially reports the same check as
    `not-applied`
  - applying the patch to the clean clone reports:
    `sdk_root=/tmp/ameba-rtos-1.2-latest variant=aivoice_ca32_17mb layout=changed hal=changed`
- Rebuilt and reflashed the clean-SDK FP32 control image after applying the
  memory layout patch to the clean SDK clone:
  - build still completed with `Build done`
  - flash still completed with `Finished PASS`
- Captured the post-flash serial state for 20s and confirmed the failure
  signature did not improve:
  - the capture file contains the normal `script` header followed by continuous
    `0x00` bytes
  - there is still no `ameba-river boot` text log or usable monitor/runtime
    output
- This narrows the root-cause boundary again:
  - the clean-SDK runtime failure is not explained by the missing 17MB memory
    layout patch alone
  - the remaining suspects are other dirty-SDK runtime deltas, especially the
    TFLM submodule, `component/aivoice`, and possibly additional platform/runtime
    changes outside the layout patch
- Performed a tighter clean-SDK retry by overlaying only the dirty SDK's local
  `tflite_micro` patch set onto the clean SDK clone:
  - patched files in the clean clone:
    - `tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/conv.cc`
    - `tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/depthwise_conv.cc`
    - `tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/im2col_utils.h`
    - `tensorflow/lite/micro/kernels/reduce_common.cc`
  - patch magnitude matched the dirty SDK local diff:
    - `4 files changed, 114 insertions(+), 47 deletions(-)`
- Rebuilt and reflashed the clean-SDK FP32 control image with:
  - clean SDK memory layout patch still applied
  - dirty TFLM local patch overlaid
- Board result did not improve:
  - build completed with `Build done`
  - flash completed with `Finished PASS`
  - 20-second raw serial capture still contained only the `script` header
    followed by continuous `0x00`
  - `od -An -tx1 -j 160 -N 64 /tmp/kws_student_fp32_clean_sdk_tflm_patch.log`
    still prints repeated `00`
- This further narrows the causality:
  - dirty SDK's local TFLM patch set is not sufficient by itself to restore a
    normal clean-SDK runtime
  - the higher-priority remaining suspects are now the broader `component/aivoice`
    delta and/or other non-TFLM runtime compatibility changes in the dirty SDK
- Scanned the clean SDK clone's actual submodule heads to avoid chasing
  misleading gitlink metadata:
  - `component/audio` actual HEAD is `e6de3cc` and matches dirty SDK
  - `component/application/speechmind` actual HEAD is `b70cfe9` and matches
    dirty SDK
  - `component/ui` actual HEAD is `f5a5325` and matches dirty SDK
  - only `component/aivoice` actual HEAD was still different:
    - clean clone `739ba4e`
    - dirty SDK `2809414`
- Performed a clean-SDK retry with `component/aivoice` aligned to the dirty SDK
  commit while keeping the earlier 17MB memory layout patch:
  - reverted the temporary clean-clone TFLM overlay
  - switched clean-clone `component/aivoice` to `280941488cb122f608d271d0c52a274e3c33a8ec`
  - rebuilt the same FP32 control image and reflashed the board
- Board result still did not improve:
  - build completed with `Build done`
  - flash completed with `Finished PASS`
  - 20-second raw serial capture still contained only continuous `0x00`
  - `od -An -tx1 -j 160 -N 64 /tmp/kws_student_fp32_clean_sdk_aivoice_commit.log`
    still prints repeated `00`
- Current engineering conclusion is now tighter:
  - the clean-SDK runtime failure is not fixed by any one of the visible
    high-signal deltas already tested individually:
    - 17MB memory layout patch
    - dirty local TFLM patch set
    - dirty `component/aivoice` commit
  - remaining root causes are now more likely to involve deeper/runtime-wide
    clean-vs-dirty differences rather than a single obvious KWS-related patch
- Took a snapshot of the last clean-SDK image set that still reproduced the
  `0x00` failure:
  - `/tmp/clean_sdk_aivoice_align_snapshot`
- Rebuilt the same FP32 control configuration against the dirty SDK baseline
  and captured a second image snapshot:
  - `/tmp/dirty_sdk_fp32_control_snapshot`
- Compared clean-vs-dirty images at the artifact level and found the divergence
  is system-wide, not app-only:
  - `km4_boot_all.bin`
    - size: `51872` vs `51872`
    - hash: different
    - first observed byte difference from `cmp -l`: byte `10119`
  - `km0_image2_all.bin`
    - size: `94208` vs `94208`
    - hash: different
    - first observed byte difference from `cmp -l`: byte `41`
  - `km4_image2_all.bin`
    - size: clean `380064`, dirty `379136`
  - `ap_image_all.bin`
    - size: clean `3558496`, dirty `3538016`
  - `km0_km4_ca32_app.bin`
    - size: clean `4040960`, dirty `4019552`
- This materially shifts the debugging frame:
  - the clean-vs-dirty split is not limited to the KWS app payload
  - even early-chain images differ, so the no-log / `0x00` failure now points
    more strongly at boot-chain / image-generation / platform-runtime divergence
    than at a single model-side patch

## Step 5.111
- Switched the latest-SDK retry target to the user-provided upstream checkout:
  - `/root/ameba-rtos`
  - branch `master`
  - commit `d9800ffc6fe3754d1c275eecdfb3f2e0991dbb49`
- Synced only the SDK-side changes that are still relevant to the preserved
  `student_bc_resnet_tiny_v2_fp32_debug` control build:
  - patched top-level latest-SDK files:
    - `component/network/websocket/wsclient_api.c`
    - `component/soc/amebasmart/fwlib/include/hal_platform.h`
    - `component/soc/amebasmart/project/ameba_layout.ld`
    - `component/soc/usrcfg/amebasmart/ameba_flashcfg.c`
  - overlaid the current local `tflite_micro` patch set into
    `/root/ameba-rtos/component/tflite_micro`:
    - `tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/conv.cc`
    - `tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/depthwise_conv.cc`
    - `tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/im2col_utils.h`
    - `tensorflow/lite/micro/kernels/reduce_common.cc`
- Deliberately did not carry over the old `component/aivoice` demo patch set in
  this step:
  - current `ameba-river` wake path no longer depends on
    `examples/speechmind_demo/platform/ameba_dsp`
  - latest `master` aivoice tree has already diverged structurally from the
    dirty `release/v1.2` patch base
  - leaving it out keeps this retry focused on the current KWS control path
- Identified a latest-SDK environment difference that would otherwise look like
  a build failure:
  - `/root/ameba-rtos/env.sh` defines `ameba.py` as a shell alias
  - non-interactive `bash -lc` does not expand that alias by default
  - the correct scripted entrypoint is therefore
    `python /root/ameba-rtos/ameba.py ...`
- Confirmed the latest SDK now builds the preserved FP32 control image all the
  way through packaging:
  - build ended with `Build done`
  - produced image sizes:
    - `ap_image_all.bin = 3542112`
    - `km4_image2_all.bin = 380448`
    - `km0_image2_all.bin = 94208`
    - `km0_km4_ca32_app.bin = 4024960`
    - `km4_boot_all.bin = 51872`
  - captured image hashes for later board/runtime comparison:
    - `ap_image_all.bin`
      `2f9d35ca635bcad71060403d725a5e706c5ea0455f2e303ca51f5c65543cd508`
    - `km4_image2_all.bin`
      `47a518787efff594b981836fcbb9042b41c242850d6616cfd9d6727e215d95c7`
    - `km0_image2_all.bin`
      `328d80657d333dba15ac0efc72eec8e3c6552631014c0d4a6e204d29cb8395dd`
    - `km0_km4_ca32_app.bin`
      `469ea7db132addff59b0900b2f3bdfc18519415e6c3e1ba9c51d656a0e17b6fd`
    - `km4_boot_all.bin`
      `faf20ef92b919df5b82dfa08dc31ae6c7f20da7cd5d701905a5cb7969b4e1ab6`
- This step proves the latest upstream SDK plus the currently required river
  patches is buildable for the preserved FP32 debug path.
- This step does not yet prove board runtime is normal; flash + serial
  validation is still needed to compare against the previous `0x00` failure
  mode.

## Step 5.112
- Flashed the latest-SDK FP32 control image built in Step `5.111` to the board
  through `/dev/ttyUSB0`:
  - entered UART burn with `reboot uartburn`
  - flashed with `AMEBA_SDK_ROOT=/root/ameba-rtos`
  - flash ended with `Finished PASS`
- Captured a fresh 20-second raw boot UART log immediately after flashing:
  - `/tmp/kws_latest_sdk_fp32_boot.log`
- The runtime result is still abnormal and matches the previously observed
  latest-SDK failure signature:
  - after the `script` header, `od -An -tx1 -j 160 -N 64` shows only `00`
  - removing all `0x00` bytes leaves only the `script` start/end wrapper text
  - no normal boot markers appear:
    - no `File System Init Success`
    - no `ameba-river boot`
    - no `kws init`
- This closes the loop on the current latest-SDK retry:
  - latest upstream SDK plus the currently required river patches can compile
    and flash
  - but it still fails at runtime before any normal boot log becomes visible
- therefore the remaining blocker is still runtime / boot-chain divergence,
  not the ability to generate or download the FP32 image

## Step 5.113
- Rebuilt the same preserved FP32 control firmware against the known dirty-SDK
  baseline for a control comparison:
  - `AMEBA_SDK_ROOT=/root/ameba-rtos-1.2`
  - build completed with `Build done`
- Reflashed that dirty-SDK control image to the same board:
  - flash again ended with `Finished PASS`
- Captured a fresh 20-second raw boot UART log after the dirty-SDK reflash:
  - `/tmp/kws_dirty_sdk_fp32_boot.log`
- Unexpectedly, the dirty-SDK control image now shows the same abnormal UART
  symptom as the latest-SDK image in this board session:
  - `od -An -tx1 -j 160 -N 64` still shows only `00`
  - removing all `0x00` bytes again leaves only the `script` wrapper text
  - no `File System Init Success`, `ameba-river boot`, or `kws init`
- Performed one additional minimal UART probe on top of the dirty-SDK image:
  - sent only `ESC + CRLF`
  - captured `/tmp/kws_serial_probe_after_esc.log`
  - probe result was still continuous `0x00` without any shell/banner text
- This changes the interpretation of the current verification session:
  - Step `5.112` still proves the latest-SDK image reproduces the `0x00` boot
    failure on real hardware
  - but the board can no longer serve as a clean immediate control after the
    dirty-SDK reflash also enters the same `0x00` state
  - therefore the current session now indicates either:
    - board state / reset state / UART state has become abnormal, or
    - the current rebuilt dirty-SDK image no longer restores the previously
      known-good runtime on this board
- Immediate conclusion for this step:
  - the latest-vs-dirty runtime comparison is now blocked by board/session
    state, not by lack of a flashable control build

## Step 5.114
- After the user power-cycled the board, the USB-serial device had detached from
  WSL:
  - `/dev/ttyUSB0` disappeared
  - Windows still showed the PL2303 adapter as shared on `BUSID 4-4`
- Re-attached the USB serial device into WSL with `usbipd.exe attach --wsl` and
  recovered `/dev/ttyUSB0`.
- Probed the board immediately after the power cycle without reflashing:
  - no UART output appeared
  - but, importantly, the board was no longer stuck in continuous `0x00`
- Re-established the dirty-SDK control path on this fresh board state:
  - reflashed the dirty-SDK FP32 control firmware
  - initial 20-second passive capture was silent
  - after a minimal `ESC + CRLF` probe, the board resumed normal runtime logs
  - recovered dirty-SDK runtime evidence is in:
    - `/tmp/kws_dirty_sdk_esc_after_powercycle.log`
  - those logs include live KWS/cloud activity such as:
    - `wakeword hit: text=小欧管家`
    - `xiaozhi ota bootstrap ok`
    - `Connected to websocket server`
- Reflashed the latest-SDK FP32 control firmware on the same recovered board:
  - flash again ended with `Finished PASS`
  - the subsequent 20-second raw UART capture showed normal runtime text rather
    than `0x00`
  - latest-SDK runtime evidence is in:
    - `/tmp/kws_latest_sdk_fp32_boot_after_powercycle.log`
  - captured lines include:
    - `Closing the Connection with websocket server`
    - `river.cloud`
    - `river.interaction`
- A follow-up `ESC + CRLF` probe on the latest-SDK image then returned to
  silence, but still did not reproduce `0x00`.
- This materially changes the current conclusion:
  - the earlier latest-SDK `0x00` result is not a stable reproduction after a
    power-cycle recovery
  - board/session state is a major variable in the prior failure captures
- with the board freshly recovered, the latest-SDK FP32 image is now observed
  running and emitting normal application logs

## Step 5.115
- Continued from the recovered latest-SDK board state and validated the
  preserved board/host parity path end-to-end on the actual `master` SDK build.
- Switched from raw `cat /dev/ttyUSB0` capture to the official Ameba monitor for
  interactive work:
  - `python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000`
  - this reliably returned the `>` prompt and accepted monitor commands, while
    the raw capture path was too unstable for command/response validation
- Verified latest-SDK board command responsiveness through the preserved KWS
  debug interface:
  - `river kws debug local status`
  - `river kws align status`
  - board reported:
    - `local_only=yes`
    - `probe=stopped`
    - compiled align sample `frames=145 duration_ms=2320`
- Re-ran the preserved latest-SDK align dump flow under monitor log mode:
  - monitor log file:
    - `/tmp/kws_latest_monitor_logdir/ttyUSB0_20260409_135610.txt`
  - command sequence:
    - `river kws debug local on`
    - `river audio probe stop`
    - `river kws align run`
- Latest-SDK board-side align result is healthy and deterministic:
  - `seq=2`
  - `infer=7`
  - `raw=371`
  - `score=0.371203`
  - `q15=12163`
  - `feat_hash=0x7ce0b11d`
  - `input_hash=0xd52f011c`
  - full tensor dump completed:
    - `feat_chunks=253`
    - `input_chunks=253`
    - `output_chunks=1`
- Replayed that exact latest-SDK board dump on host against the preserved FP32
  bundle:
  - model:
    - `/root/kws-trainint/artifacts/exports/student_bc_resnet_tiny_v2/model.fp32.tflite`
  - host replay result:
    - `board_meta: input_type=float32 output_type=float32 shape=(1, 40, 101, 1)`
    - `board_hash: feature=0x7ce0b11d input=0xd52f011c`
    - `host_hash: feature=0x7ce0b11d logged_input=0xd52f011c effective_input=0xd52f011c`
    - `quant_parity: diff_bytes=0/16160`
    - `board_output: raw=371 score=0.371203 exact=0.371203 q15=12163`
    - `host_output: raw=371 score=0.371000 exact=0.371203`
    - `output_parity: bytes_equal=yes raw_equal=yes`
- Restored the board to a normal runtime state after the parity check:
  - `river kws debug local off`
  - `river audio probe start`
  - board confirmed:
    - `local_only=no`
    - `wake_handoff=normal`
    - `vad probe started`
- This step establishes the most important current fact:
  - on the recovered board, the latest-SDK FP32 deployment is not only runnable
    but also board/host parity-correct through the preserved debug workflow

## Step 5.116
- Continued on the same latest-SDK FP32 board image after Step `5.115` and
  validated the real online wake-to-cloud runtime path instead of stopping at
  tensor parity only.
- Reattached the official Ameba monitor to the live board:
  - `python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000`
  - confirmed the board stayed responsive and in normal runtime mode
- Observed a real wakeword trigger on the latest-SDK FP32 image through the
  normal runtime path:
  - `wakeword hit: text=小欧管家 score_pm=271 q15=8912 triggers=10`
  - `wakeword queued text=小欧家 confidence=8912`
  - `kws debug status` remained:
    - `local_only=no`
    - `wake_handoff=normal`
- Observed the complete cloud handoff open successfully after wake:
  - `xiaozhi ota bootstrap ok`
  - `xiaozhi connecting`
  - `Connected to websocket server`
  - `server hello: sid=8665a824`
  - `interaction_state: wake_monitoring -> wake_confirmed`
- Observed live ASR and dialogue traffic on that same session:
  - `asr provider=xiaozhi_realtime session started sid=8665a824`
  - partial/final STT logs were emitted
  - LLM response logs were emitted
- Observed the board-side TTS playback path actually start and stop on the
  latest-SDK FP32 image:
  - `tts ... state=sentence_start text=没听清呢，`
  - `playback start: stream=xiaozhi_tts rate=24000Hz frame=20ms ...`
  - `xiaozhi playback start: 24000Hz frame=20ms mono=960B queued=8 mode=no_ref gain=5/2`
  - `ameba_audio_stream_tx_start`
  - `tts ... state=stop`
  - `playback stop: stream=xiaozhi_tts epoch=11`
- Captured a runtime status snapshot during the live session:
  - `interaction_state=asr_streaming`
  - `playback_service=idle ... starts=6 stops=6`
  - `asr provider=xiaozhi_realtime ... wifi=connected`
  - `xiaozhi session=yes hello=yes`
- Current caveat remains visible but is not a functional blocker for this step:
  - repeated `xiaozhi uplink backpressure`
  - `last_err=send_queue_busy`
  - despite that, wake, websocket connect, ASR, LLM, TTS start, and playback
    stop all completed in the same session
- This step adds the runtime conclusion missing from Step `5.115`:
  - on the recovered latest-SDK board, the FP32 student debug variant is not
    only parity-correct, but also alive on the real wakeword -> cloud ASR ->
    TTS playback chain

## Step 5.117
- Investigated the remaining latest-SDK FP32 runtime caveat without changing
  code or UART behavior:
  - repeated `xiaozhi ws backpressure`
  - repeated `xiaozhi uplink backpressure`
  - runtime status ending in `last_err=send_queue_busy`
- Traced the issue through the actual project code path and confirmed the
  problem is in the cloud uplink path after wakeword, not in the KWS deploy
  path:
  - websocket queue gate:
    - `RIVER_XIAOZHI_WS_QUEUE_MAX = 8`
    - `RIVER_XIAOZHI_WS_AUDIO_QUEUE_RESERVE = 2`
    - effective audio backpressure threshold is therefore `ready >= 6`
  - websocket transport is intentionally configured non-blocking:
    - `ws_set_senddata_block_time(0)`
    - `ws_multisend_opts(..., 1)`
  - when busy occurs, the uplink worker:
    - increments `busy_count`
    - applies exponential backoff up to `160 ms`
    - trims stale uplink audio down to `6` frames
- Verified an important interpretation point from the live status snapshot:
  - `q_peak=6` matches the design-side clamp exactly and is not evidence that
    the websocket queue truly ran away beyond the intended soft ceiling
- Confirmed the current implementation also has a startup burst factor on the
  XiaoZhi path:
  - wake/open uses `256 ms` pre-roll
  - bridge input is `16 ms`
  - uplink Opus packetization is `20 ms`
  - opening a session can therefore inject roughly `12` full uplink packets
    into the project-side ring before live streaming settles into steady state
- Wrote the analysis into a dedicated project document so later model bring-up
  can distinguish:
  - KWS parity/deployment problems
  - cloud uplink congestion problems
- New document:
  - `doc/XIAOZHI_UPLINK_BACKPRESSURE_ANALYSIS_LATEST_SDK_FP32_ZH.md`
- The resulting conclusion for the current board baseline is:
  - latest-SDK FP32 is functionally alive
  - `send_queue_busy` is presently a realtime quality/freshness-protection
    issue in the XiaoZhi uplink path, not a blocker proving model deployment is
    wrong

## Step 5.118
- Added a new project-level status snapshot document that consolidates the
  current branch, SDK baselines, implemented runtime chain, verified model
  matrix, preserved parity workflow, active issues, and next-step priorities:
  - `doc/PROJECT_STATUS_SNAPSHOT_2026-04-09_ZH.md`
- This document is intentionally broader than the older
  `doc/PROJECT_STATUS_ZH.md`:
  - it reflects the current `kws` branch instead of the historical `DS-CNN`
    staging state
  - it includes the now-verified latest-SDK runtime and parity conclusions
  - it explicitly records the preserved board/host parity workflow as a
    non-negotiable project asset
  - it summarizes the tested model matrix across:
    - `student_bc_resnet_tiny_v2_fp32_debug`
    - `student_bc_resnet_tiny_v2_int8_debug`
    - `student_bc_resnet_nano_v2_fp32_debug`
    - `student_dscnn_tiny_v2_fp32_debug`
    - `student_dscnn_small_v2_fp32_debug`
  - it captures the current project-level problem map:
    - student FP32 realtime insufficiency
    - INT8 gain not materializing on current runtime
    - XiaoZhi uplink backpressure
    - local `16ms` vs uplink `20ms` cadence mismatch as a structural issue
- The document also makes the current overall engineering state explicit:
  - the project already has a runnable end-to-end chain
  - the main blockers are now runtime quality and deployability, not basic
    bring-up

## Step 5.119
- Raised AP shell stack headroom for this `kws` debug branch without modifying
  `/root/ameba-rtos`:
  - added project-local forced-include header
    `include/river/river_sdk_debug_overrides.h`
  - the header overrides `CONFIG_SHELL_TASK_STACK_BASIC_SIZE` to `8192`
  - the override is injected only on the AP `monitor_*` target from the
    external project `CMakeLists.txt`
- Kept the latest-SDK debug memory layout as the baseline instead of inventing a
  second project-side memory map:
  - checked `tools/sdk/apply_rtl8730e_memory_layout_patch.py`
  - confirmed `/root/ameba-rtos` is already in `aivoice_ca32_17mb` state
- Explicitly did not use `prj.conf` for the shell stack change:
  - the SDK `SHELL_TASK_STACK_BASIC_SIZE` symbol is a non-prompt Kconfig item
  - generated `.config_ca32` still stays at the SDK value `2440`
  - the effective debug value now comes from the AP monitor compile command via
    forced include, not from Kconfig
- Added a tiny project-local `ccache` shim and `env.sh` fallback so the debug
  branch can still build cleanly on machines where the SDK expects `ccache` but
  the binary is absent:
  - `tools/shims/ccache`
  - `env.sh`
- Reconfigured and rebuilt the full RTL8730E image successfully after the
  change:
  - AP compile commands contain
    `-include /root/ameba-river/include/river/river_sdk_debug_overrides.h`
  - final images were regenerated:
    - `build_RTL8730E/build/project_ap/image/ap_image_all.bin`
    - `build_RTL8730E/km0_km4_ca32_app.bin`
- Result for this step:
  - debug branch keeps the larger latest-SDK CA32 layout
  - AP shell now has materially more stack headroom for heavy serial/KWS debug
    flows
  - SDK sources remain untouched

## Step 5.120
- Refined XiaoZhi uplink websocket pressure handling so the board can
  distinguish "soft headroom exhausted for audio" from true queue saturation:
  - added `send_backpressure_reserve_events`
  - added `send_backpressure_full_events`
  - backpressure logs now print `reason=soft_reserve|hard_full` together with
    `ready/recycle/max/stable/free/soft_limit`
- Made the queue model explicit in both code and diagnostics:
  - current websocket queue depth stays `16`
  - audio still reserves `2` slots for control traffic
  - the effective audio soft limit is therefore `14`, not `16`
  - `ready=14/16` is now reported as a project-side `soft_reserve` event rather
    than being conflated with hard queue exhaustion
- Reduced SDK-side send-buffer allocator churn without changing the audio soft
  limit:
  - introduced `RIVER_XIAOZHI_WS_STABLE_BUF_NUM`
  - set it to `12` (`3/4` of the queue depth)
  - switched XiaoZhi from `ws_multisend_opts(..., 1)` to
    `ws_multisend_opts(..., 12)` so short uplink bursts can reuse warmed send
    buffers instead of repeatedly malloc/free'ing them
- Extended `river xiaozhi status` / boot logs to expose the real runtime queue
  model:
  - connect log now prints `txq`, `stable`, and `reserve`
  - status dump now prints `stable`, `audio_soft_limit`, `reserve_bp`, and
    `full_bp`
- Updated the latest-SDK XiaoZhi uplink analysis document to reflect the
  current behavior:
  - the previously documented `max=8`, `ready>=6`, and
    `ws_multisend_opts(..., 1)` interpretation is superseded here
  - the current `ready=14/16` high watermark is documented as the combined
    effect of the `soft_reserve` policy, `16 ms -> 20 ms` uplink cadence
    bridging, and session-open pre-roll burst
- Verification for this step:
  - `python3 tools/diag/check_codex_harness.py` passed
  - `python3 /root/ameba-rtos/ameba.py build -p` completed successfully against
    `/root/ameba-rtos`

## Step 5.121
- Removed the unconditional per-wake synchronous XiaoZhi bootstrap from the
  normal `open_session()` path:
  - `open_session()` now returns immediately if the websocket session is already
    open instead of re-running OTA/bootstrap first
  - when `ota_url` is configured, it now refreshes bootstrap only if:
    - no websocket URL is currently available, or
    - the current websocket URL/token came from bootstrap and the local cache
      TTL has expired
- Added bootstrap cache state to the XiaoZhi transport context:
  - bootstrap-derived websocket credentials are now tagged as bootstrap-owned
  - successful bootstrap stores a local TTL-based cache expiry
  - current TTL is `RIVER_XIAOZHI_BOOTSTRAP_CACHE_TTL_MS = 10 min`
- Preserved safe fallback behavior when refresh fails:
  - if a refresh attempt fails but an older cached websocket URL still exists,
    the session open path logs the refresh failure and continues using the stale
    URL instead of hard-failing immediately
- Added explicit cache invalidation rules for config changes:
  - manual `url/token` overrides disable automatic bootstrap refresh
  - changing `ota_url` invalidates bootstrap-owned cached credentials so the
    next session re-fetches from the new OTA endpoint
- Extended diagnostics for board-side timing analysis:
  - bootstrap success log now prints `cache_ttl_ms`
  - cache hits now log `xiaozhi bootstrap cache hit: refresh_in_ms=...`
  - `river xiaozhi status` now prints:
    - `bootstrap_owned`
    - `bootstrap_refresh_in_ms`
- Updated the active XiaoZhi stability plan document to record the new default
  assumption for future latency analysis:
  - the system should no longer be analyzed as "every wake always blocks on
    synchronous bootstrap"
- Verification for this step:
  - `python3 tools/diag/check_codex_harness.py` passed
  - `python3 /root/ameba-rtos/ameba.py build -p` completed successfully against
    `/root/ameba-rtos`
  - static grep confirmed the new cache TTL, cache-hit log, and
    `bootstrap_owned/bootstrap_refresh_in_ms` status fields

## Step 5.524
- Synced the active voice runtime plan with the updated 2026-04-26 service-side RTOS realtime recommendations:
  - service-side no-audio root cause remains qwen_unified TTS queue/cancel timeout
  - device-side should not synthesize playback facts without `audio.out.meta` / PCM
  - first device-side implementation slice is bounded uplink catch-up, not a second orchestrator
- Tightened XiaoZhi uplink pacing:
  - reduced `RIVER_CLOUD_XIAOZHI_UPLINK_DRAIN_BURST_MAX` from `4` to `2`
  - changed the uplink sender to respect a 20 ms `next_send_ms` cadence after successful sends
  - allowed bounded catch-up only when the local I/O task is already behind the scheduled due time
  - stopped sleeping inside the uplink send path while waiting for the next due frame, so the IO task can keep polling control/downlink work
- Added uplink burst diagnostics:
  - `river xiaozhi status` now reports global and active-round `burst_max`
  - `xiaozhi asr round finish` now reports round-level `burst_max` next to packet/pace counters
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `python3 /root/ameba-rtos/ameba.py build -p` completed successfully against `/root/ameba-rtos`

## Step 5.525
- Completed the remaining XiaoZhi device-side service-alignment items from the 2026-04-26 stability plan:
  - parsed and stored service `audio.out.meta` fields `output_lane`, `output_role`, and `phrase_id`
  - propagated those fields through `river_xiaozhi_event_t` into playback meta truth and status logs
  - kept the fields as observed service facts only; no local fast-launch/main-dialogue planner was added
- Added per-round uplink freshness telemetry:
  - fixed-size 16-sample windows for send interval, estimated capture-to-send age, backlog, and transport-send duration
  - `xiaozhi asr round finish` now reports p50/p95 metrics under `uplink_ms[...]`
  - existing 20 ms pacing and stale-drop policy are unchanged
- Added no-audio/no-ref diagnostics and safe local fallback UX:
  - response-audio abandoned logs now include `ref_enabled` and `playback_active`
  - playback status exposes `ref_enabled` next to playback meta truth
  - no-audio recovery can play a short local retry prompt through `river_playback_service` with `reference_export=true`
  - the prompt has a 15 s cooldown and does not synthesize `audio.out.meta` or send playback ACKs
- Updated `doc/XIAOZHI_SESSION_STABILITY_EXECUTION_PLAN_ZH.md` with Step E/F/G for meta fields, uplink telemetry, and no-ref/local fallback completion criteria.
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `python3 /root/ameba-rtos/ameba.py build -p` completed successfully against `/root/ameba-rtos`

## Step 5.529
- Re-reviewed the 2026-04-27 `audio.out.meta` / playback-start failure path for capture hot-path blocking risks beyond the previous state-machine fix.
- Converted capture/VAD/AEC policy reads to non-blocking best-effort snapshots:
  - playback service stats now return a stale snapshot instead of waiting behind `AudioTrack_Start/Write`
  - reference service read/stats and the lower playback-reference ring read/stats now return zero/busy instead of blocking the mic consumer
  - native capture-reference publish/get now drops or returns an empty observation if its mutex is busy
  - dialog runtime voice-policy view now uses a try-lock so cloud/playback reconciliation cannot stall AEC gating
- Made runtime stats snapshots non-blocking so VAD state-change diagnostics cannot wait forever on the stats mutex.
- Made VAD barge-in ducking control non-blocking and kept the local duck-active flag set when duck release cannot acquire the playback lock, so release can be retried instead of silently desynchronizing.
- Verification for this step:
  - `git diff --check` passed
  - first sandboxed `python3 /root/ameba-rtos/ameba.py build -p` failed because the SDK build writes `/root/ameba-rtos/component/soc/amebasmart/main/ap/inc/build_info.h.tmp` outside the workspace sandbox
  - rerun with approved SDK build permission completed successfully with `Build done`

## Step 5.530
- 对齐服务侧关于 `preview_uplink_realtime_ratio=0.52~0.65` 的定位，复查端侧 XiaoZhi uplink：
  - 当前 PCM 20 ms 帧是 640B，发送路径直接调用 `ws_sendBinary(payload, bytes, ...)`，未被旧的 512B scratch 常量截断
  - 真实慢点来自 warmup 阶段仍按 20 ms due pacing 慢慢送预滚/首段音频；日志里的 `send_interval_p50=37/61ms` 与 `pace_pct=52~56` 能解释服务端 ratio 低于 1
- 增加预览 warmup 有界追赶：
  - 新增 `RIVER_CLOUD_XIAOZHI_UPLINK_PREVIEW_WARMUP_MS=320`
  - 新增 `RIVER_CLOUD_XIAOZHI_UPLINK_PREVIEW_BURST_MAX=6`
  - 仅在本轮已发送音频少于 320 ms 且 uplink ring/retry 有待发帧时，允许绕过下一帧 due time
  - warmup 结束后自动回到原来的 20 ms pacing 和常态 `RIVER_CLOUD_XIAOZHI_UPLINK_DRAIN_BURST_MAX=2`
  - 绕过 due 时把下一次 due 重新锚定到当前时间 + 20 ms，避免 warmup burst 后累积未来长睡眠
- 增加 warmup 诊断：
  - `xiaozhi asr round finish` 新增 `preview_warmup[target_ms=320 done_ms=... bypass=...]`
  - `done_ms` 用于看端侧送满首 320 ms 音频的 wall time，`bypass` 用于确认是否真的进入首段追赶
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `python3 /root/ameba-rtos/ameba.py build -p` completed successfully against `/root/ameba-rtos` with approved SDK write permission

## Step 5.531
- Reviewed `/root/agent-server` latest commit `1a94986 Optimize realtime preview backlog handling` against the RTOS client:
  - server preview observations are now explicitly latest-state hints and may be coalesced or skipped under realtime backpressure
  - `input.accept_ready` is documented as an optional preview-side accept-ready observation
  - accepted-turn semantics remain unchanged: only `session.update.accept_reason` confirms acceptance
- Synced the device-side protocol surface without changing turn ownership:
  - added `RIVER_XIAOZHI_EVENT_INPUT_ACCEPT_READY`
  - parsed `input.accept_ready` payload fields `preview_id`, `reason`, and `audio_offset_ms`
  - cached it in the XiaoZhi preview truth as `accept_ready` / `accept_ready_reason`
  - updated cloud and transport preview status logs to show `accept_ready` separately from `endpoint_candidate`
  - parsed and logged discovery `voice_collaboration.preview_events.accept_ready`
- Preserved safety semantics:
  - `input.accept_ready` only touches the conversation window and preview observation cache
  - it does not close the local ASR round, does not send `audio.in.commit`, and does not replace `session.update.accept_reason`
- Verification for this step:
  - `git diff --check` passed

## Step 5.532
- 按 2026-04-27 板端日志把 XiaoZhi 端侧 runtime 问题做成闭环修复，而不是继续只补单点：
  - 播放 downlink 在拿到音频帧后再次校验 playback backend，若 backend 已从 `owned_active` 变成 detached/stop/recovering，则保留当前帧重试并走 pending-stop/重新 start 路径，不再把“写已释放 backend”误判成 `write_failed` rebuffer
  - follow-up ASR reopen 新增 output-turn guard：只要服务端仍处于 thinking/speaking、playback lane/turn 仍占用、或 rebuffer 未完成，就阻止本地重新打开 ASR；当前 no-ref/duck-only barge-in 只允许 duck/suppress，不再上传一轮空 follow-up
  - `audio.out.meta` 以 response/playback 变化作为新播放上下文边界，清空上一轮 meta/segment/rebuffer 状态；`expected_duration_ms=0` 和空 text 也按服务端事实写入，防止复用上一段 `1660ms` 等脏值
  - XiaoZhi I/O task 在 websocket poll 前后都尝试 drain uplink，并把常态/warmup burst 上限调到 `4/8`，允许 backlog 有界绕过 due time 追赶，避免 `send_interval_p50` 卡在约 55ms
- 预期修复的日志症状：
  - 不再出现 `xiaozhi playback write failed ... backend=detached`
  - 播放/rebuffer 期间不再因 `playback_state` 误开 `asr round id=2`
  - 新 response 的 `expected_duration_ms=0` 不再被打印成旧的 `expected_ms=1660`
  - uplink `send_interval_p50/p95` 应比历史 `55/63ms` 明显下降，若仍慢则继续看 `busy/fail/send_duration_p95`
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `python3 /root/ameba-rtos/ameba.py build -p` completed successfully against `/root/ameba-rtos` with approved SDK write permission

## Step 5.533
- 适配服务端 `smart_home_first_sound` / `rtos-smart-home-v1` 线缆协议更新，端侧不再把 XiaoZhi WebSocket profile 固定在旧 `rtos-ws-v0`：
  - discovery 解析并缓存根字段 `protocol_version`、`subprotocol`、`product_profile`、`mainline_profile`
  - WebSocket 握手使用 discovery 广告的 `subprotocol`，无 discovery 时回退旧 `agent-server.realtime.v0`
  - `session.start.payload.protocol_version` 使用 discovery 广告的协议版本，缺省仍回退旧 `rtos-ws-v0`
- 补齐诊断可见性：
  - init / config / discovery / connecting / transport ready / `session.start` / status dump 全部输出 active `wire/subprotocol/product/mainline`
  - 以后服务切到 `agent-server.smart-home.realtime.v1` 时，板端日志能直接确认是否按新 profile 握手与上报
- 保持端侧行为边界：
  - 没有引入本地 tool planner，也不把服务端 hot-input cache 镜像到设备
  - 旧 generic/legacy 服务仍可通过 discovery 缺省或失败回退继续运行
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `python3 /root/ameba-rtos/ameba.py build -p` completed successfully against `/root/ameba-rtos`
  - `curl -sS --max-time 5 http://101.33.235.154:8080/v1/realtime | python3 -m json.tool` confirmed the currently deployed service still advertises `rtos-ws-v0` / `agent-server.realtime.v0`


## Step 5.534
- 针对 2026-04-27 17:47 板端日志里的 XiaoZhi 段间播放恢复风暴收口：
  - 日志显示 `waiting_next_segment + segments=0` 段间等待期间，worker 在 `owned_active -> owned_paused -> owned_active` 间每 15~30 ms 自激振荡
  - 每轮都会触发 `playback recover`，导致 `AudioTrack_Flush / tx_close / CreateAudioHwStreamOut` 和 playback epoch 从约 `192` 快速涨到 `369+`
- 修复段间 hold 的幂等性：
  - `owned_paused + WAITING_NEXT_SEGMENT` 现在被视为已经处于 segment-gap hold，worker 直接保持 `segment_gap` 等待，不再再次调用 recover/stop
  - `hold_playback_for_segment_gap(...)` 对已暂停段间 hold 做 no-op 保护，避免未来路径绕过 `maybe_pause_for_segment_gap()` 时重新打到 playback service
  - `maybe_resume_paused_playback()` 在仍处于 `WAITING_NEXT_SEGMENT` 且还没有新 segment meta/segment queue 时禁止按 queued threshold 自恢复
- 预期修复的日志症状：
  - 段间等待最多出现一次 attached hold/recover，不再反复打印 `playback recover: stream=xiaozhi_tts epoch=...`
  - `AudioTrack_Flush / tx_close / CreateAudioHwStreamOut` 不再以 15~30 ms 周期循环
  - 新 `audio.out.meta` 到达并进入 segment queue 后才允许 paused backend resume
- Verification for this step:
  - `git diff --check` passed
  - `python3 /root/ameba-rtos/ameba.py build -p` completed successfully against `/root/ameba-rtos`

## Step 5.535
- 针对 2026-04-27 17:49 板端日志里的 XiaoZhi text-only / 零时长播放尾段收口：
  - 日志显示服务端返回 `response.chunk` 文本后直接回到 `state=active input_state=active output_state=idle`，端侧只清掉 `response audio wait`，但没有收口会话窗口和 turn semantics，导致 interaction 留在 thinking/active 并被本地 VAD 误开后续 ASR round
  - `server_returned_active_no_audio` 现在按 text-only 终止响应处理：记录恢复日志、关闭本地 round、关闭 conversation window、清掉 session.update cache / turn semantics，并触发状态同步，避免没有音频时继续留在 follow-up 会话里接收幻听输入
  - `expected_duration_ms=0 && is_last_segment=yes` 的播放段现在在downlink queue/retry drain 且已有有效 mark 后标记 fully-heard 并弹出 segment，不再永久卡在 current segment 等待 duration 达标
  - downlink 写入和轮询 ACK 进度后会在 last segment fully-heard 时主动 queue `audio.out.completed`，并给物理播放 backend 安排 drain stop，避免实时协议没有 legacy `tts.stop` 时一直等到 upstream-starved/rebuffer 或服务端 deadline
- 预期修复的日志症状：
  - text-only/no-audio response 后会出现 `xiaozhi response audio abandoned recovery ... action=close_text_only`，随后不再从 `thinking` 被本地 VAD 拉起 `asr round id=2/3`
  - 零时长 last segment 会出现 `zero-duration last segment completed after drain` 和 `playback ack completed ...`，不再等到服务端 `audio_stream_failed context deadline exceeded`
  - 已完成/正在关闭的尾段不再被 downlink starvation 误判成需要 `note_rebuffer` 的上游断流恢复
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `python3 /root/ameba-rtos/ameba.py build -p` completed successfully against `/root/ameba-rtos`

## Step 5.536
- 对齐服务侧 2026-04-27 修改建议，收口 server endpoint 模式下端侧不该继续充当第二 orchestrator 的问题：
  - 本地 VAD post-roll 在 discovery 表示 server endpoint 可用时只进入 `server_accept_wait`，默认不再发送正常路径 `audio.in.commit`
  - 等待服务端 accepted truth；只有超过 `1800ms` 仍没有服务端 accept 时才 fallback commit
  - accepted / local close / window close / transport reset 都清掉 `server_accept_wait`，避免迟到 stop/commit
- 上行改为严格 20ms pacing：
  - 常态 drain burst 上限收敛为 `1`
  - 移除 preview warmup/backlog 对 due time 的 bypass
  - 成功发送后用 `now + 20ms` 排下一帧，不再根据历史 due time catch-up 补发 backlog
  - stale queue 上限收敛为 5 帧，弱网时继续丢旧帧优先保持实时性
- Playback ACK 收口为更严格的真实播放事实：
  - 零时长 last segment 不再在首个 mark 立即 completed
  - 只有 downlink queue/retry 都 drain、segment 已 started 且有真实 mark 后，才把零时长尾段标为 fully-heard 并推进 completed ACK
  - 无 meta / 无实际 started playback 路径仍不伪造 started/completed
- 补齐端侧指标：
  - ASR round finish 日志新增 `frame_bytes`、`send_interval_max`、`capture_age_max`、`backlog_max`、`send_duration_max`、`dropped_frames`
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `python3 /root/ameba-rtos/ameba.py build -p` completed successfully against `/root/ameba-rtos`

## Step 5.537
- 复查 Step 5.536 后修正一个残留状态机差异：
  - `input.endpoint candidate=yes` 旧路径仍会在本地 post-roll 时直接 `close_local_round_for_cause(SERVER_ENDPOINT, ...)`
  - 这会把 endpoint candidate 当成 accepted truth，绕过 `server_accept_wait` 和 1.8s fallback commit
- 现在 endpoint candidate 只作为观察事件：
  - 本地 stop 时进入 `server_accept_wait`
  - 继续等待 `session.update.accept_reason` 作为 accepted truth
  - 服务端未接受时仍保留 fallback commit 兜底
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `python3 /root/ameba-rtos/ameba.py build -p` completed successfully against `/root/ameba-rtos`

## Step 5.538
- 根据 2026-04-28 上板日志继续收口两个运行时问题：
  - `xiaozhi asr round finish` 中 `burst_max=5` 虽然发送路径已限制单次 drain 只发 1 帧，但 round 统计仍把 `backlog_frames` 当成了 burst，导致日志误报
  - `xiaozhi playback ack mark sent ... played_duration_ms=3` 出现同一 `segment_id` 的重复发送，说明异步控制队列仍可能排入重复 mark ACK
- 本次修正：
  - uplink 成功发送后的下一帧 deadline 改为基于已有 due time 递进，晚点时仅允许轻量 catch-up，不再把调度漂移永久累加到后续 20ms pacing
  - ASR round 的 `burst_max` 改回只统计真实发送 burst，不再被 backlog 深度污染；backlog 继续只通过 `backlog_p95/max` 观察
  - async `PLAYBACK_MARK` 控制请求入队前增加同 `response_id/playback_id/segment_id/played_duration_ms` 的队列级去重，避免重复 mark ACK
- Verification for this step:
  - `git diff --check` passed
  - `python3 tools/diag/check_codex_harness.py` passed
  - `python3 /root/ameba-rtos/ameba.py build -p` completed successfully against `/root/ameba-rtos`
