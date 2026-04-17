# Full-Duplex Voice Execution Plan

Status: active
Last Updated: 2026-04-17
Branch: `agent-server-v2`

## 1. 当前背景

- 当前主问题：
  - 端侧与服务侧已经具备双向实时音频传输、流式文本输出、流式 TTS、
    barge-in 和会话级 follow-up 能力，但整体仍是
    `可打断的流式回合制 / 准全双工`
  - 用户当前希望评估并规划“边说边回”的全双工演进路径，而不是继续只做
    `client_commit` 驱动的 turn-based 交互
- 已知稳定基线：
  - 端侧仓库：`/root/ameba-river`
  - 默认 SDK：`/root/ameba-rtos`
  - 服务侧代码仓：`/root/agent-server`
- 当前默认 SDK：
  - `/root/ameba-rtos`
- 与本计划强相关的现有文档：
  - `doc/XIAOZHI_SESSION_STABILITY_EXECUTION_PLAN_ZH.md`
  - `/root/agent-server/docs/architecture/full-duplex-voice-assessment-zh-2026-04-10.md`
  - `/root/agent-server/docs/architecture/local-open-source-full-duplex-roadmap-zh-2026-04-10.md`
  - `/root/agent-server/docs/architecture/voice-demo-realtime-optimization-zh-2026-04-14.md`
  - `/root/agent-server/docs/adr/0009-advertise-commit-driven-turn-semantics-until-server-vad-exists.md`
  - `/root/agent-server/docs/protocols/realtime-voice-client-implementation-guide-v0-zh-2026-04-16.md`
  - `/root/agent-server/docs/protocols/realtime-voice-client-collaboration-proposal-v0-zh-2026-04-16.md`
  - `/root/agent-server/docs/architecture/server-primary-hybrid-min-device-capabilities-and-interruption-zh-2026-04-16.md`
  - `/root/agent-server/docs/architecture/voice-architecture-execution-roadmap-zh-2026-04-16.md`

## 2. 目标

- 明确回答当前两端是否已经具备全双工基础，以及离“边说边回”还差哪些层
- 把演进路线拆成：
  - 服务侧任务块
  - 端侧任务块
  - 联调与协议收口任务块
- 给出一条不推翻现有架构、但能逐步演进到真全双工的执行路线
- 保留现有 `half-duplex / client_commit` 路径作为 fallback，避免一次性切断现有板端可用性

## 3. 非目标

- 不在本计划中直接实现纯端到端 `speech-to-speech` 模型替换
- 不要求第一步就修改公开 websocket 协议
- 不把当前 XiaoZhi 会话稳定性修复和全双工演进混成同一个提交
- 不假设 `/root/agent-server` 本地代码等于当前线上部署版本
- 不在未完成 AEC / reference 基线前，强行把所有板子切到全双工

## 4. 约束 / Guardrails

- 服务侧代码在 `/root/agent-server`，但实际部署实例不一定就是该本地版本
- 服务端代码在持续演进，所有“服务现状”判断都必须区分：
  - 本地代码现状
  - 当前部署实例行为
- 端侧改造必须保持：
  - 当前 `wake -> session.start -> audio.in.commit -> response` 路径可回退
  - 当前板端日志和 runtime stats 不被削弱
- 服务侧改造必须保持：
  - `internal/voice` 继续作为共享语音编排层
  - `internal/gateway` 只做 transport adaptation
  - `internal/agent` 不直接耦合 websocket 细节
- 未拿到稳定 AEC / playback reference 前，不把“真全双工”作为端侧默认行为

## 5. 已知事实

- 端侧最近已落地两个与全双工直接相关的保护性切片：
  - `5.164 xiaozhi duplex capability advertisement gate`
  - `5.165 xiaozhi tts_start local-round duplex policy gate`
- 端侧当前默认构建仍保持保守 fallback：
  - `session.start` 默认仍上报 `half_duplex=true`
  - `tts_start` 默认仍会走本地 round close
- 端侧当前已经具备最小实验闸门，但还没有形成完整 duplex 主链：
  - 显式开启 duplex experiment 后，只有在“当前 voice profile 宣称具备 playback reference 能力”时，`tts_start` 才会保留本地 round
  - 当前判断仍是 profile/capability 级，不是 runtime-ready 级
  - 相关代码：
    - `components/river_cloud/river_cloud_xiaozhi_session.c`
    - `components/river_cloud/river_cloud_adapter.c`
- 端侧已经有：
  - `barge_in_listening` 交互态
  - 本地 playback interrupt
  - playback ducking API
  - 但默认构建未启用 WebRTC AECM 实验路径：
    - `prj.conf`
- 端侧当前仍缺两块关键收口：
  - `session.update` 只消费顶层 `state`，尚未消费服务端新增的：
    - `input_state`
    - `output_state`
    - `barge_in_enabled`
    - `turn_id`
    - `accept_reason`
  - `near-end` barge-in 仍主要走“检测到就 interrupt TTS”的硬策略，尚未形成 `duck-first` 本地仲裁
- 近期板端日志多次出现：
  - `echo:0B`
  - `ref_peak=0`
  - 说明全双工真正可用之前，AEC / reference 仍是显著前置条件
- 服务侧本地代码仓 `/root/agent-server` 已经明显前进：
  - internal session core 已支持输入/输出双轨状态
  - speaking 期间 input preview、barge-in 策略和 soft ducking 已进入共享 runtime
  - native realtime 主路径已支持 early audio start，而不是必须等最终完整响应闭合后再播
  - `session.update` 协议兼容扩展已经存在
- 2026-04-16 的服务侧新文档已经把端侧协同边界说清楚：
  - 端侧不是第二编排层；accepted-turn 仍以 `accept_reason` 为准
  - `input.speech.start` / `input.preview` / `input.endpoint` 都是观察事件，不是 stop/commit 指令
  - `audio.out.started` / `mark` / `cleared` / `completed` 是播放事实回报，不是策略命令
  - 所有新能力必须经过 discovery + `session.start.capabilities` 双向协商
- 但服务侧 discovery / runtime 对外仍保持兼容口径：
  - `turn_mode = client_wakeup_client_commit`
  - `server_endpoint` 是否真正启用仍取决于当前部署配置
  - 本地代码现状不等于线上实例行为
- 因此当前最准确的定位已经更新为：
  - 服务侧本地仓已经具备“真全双工前的主干骨架”
  - 当前端到端瓶颈已更多转移到端侧：
    - 声学前提是否成立
    - 本地 round / uplink / playback 的并行编排是否收口
    - 是否能消费服务侧新增 lane-state 信号
- 基于新云侧文档反看端侧现状，协作协议层已经完成四步基线，但仍未收口完整闭环：
  - 已消费 `GET /v1/realtime` discovery 中的 `voice_collaboration`
  - `session.start.capabilities` 已能按协商结果声明：
    - `preview_events`
    - `playback_ack.mode=segment_mark_v1`
  - 端侧已把 accepted-turn 语义同步进 cloud runtime：
    - `accept_reason`
    - `turn_id`
    - `input_state`
    - `output_state`
    - `barge_in_enabled`
  - 端侧已解析并缓存观察事件：
    - `input.speech.start`
    - `input.preview`
    - `input.endpoint`
  - 端侧已解析并缓存播放上下文：
    - `audio.out.meta`
  - 端侧已发送最小 playback fact ACK：
    - `audio.out.started`
    - `audio.out.completed`
  - 仍未发送：
    - `audio.out.mark`
    - `audio.out.cleared`
  - `response.start` 目前也还没有把 `turn_id/trace_id` 收进端侧播放上下文

## 6. 风险与未知项

- 线上部署和 `/root/agent-server` 本地代码可能不同步，导致“代码可行”和“线上行为”不一致
- 端侧若在无 AEC / reference 的前提下直接开启全双工，容易出现：
  - 回声误触发
  - 自我打断
  - false barge-in
  - ASR 污染
- 服务侧若只把 `client_commit` 换成 `server_endpoint`，但不改输入/输出双轨状态机，仍会停留在“自动 commit 的回合制”
- 若过早修改公网协议，会把当前板端稳定性问题与协议演进问题混在一起

## 7. 双端任务块

### 7.1 服务侧任务块

2026-04-16 复盘说明：

- 基于 `/root/agent-server` 当前本地代码，`S2`、`S3`、`S4` 的主干能力已不再是“纯待实现”状态
- 对端侧规划而言，这些项现在更像：
  - 部署实例是否已具备的确认项
  - 剩余协议 / 运行时收口项
- 因此端侧后续切片不应再把“等待服务侧先补双轨状态机”视为阻塞前提

#### S1: 把 server endpoint 从实验能力升级为主路径候选

目标：

- 让服务端先具备“连续收音 + 服务端收尾”的稳定能力，但先不要求公网协议立刻切换

范围：

- `/root/agent-server/internal/app/config_voice.go`
- `/root/agent-server/internal/gateway/realtime_ws.go`
- `/root/agent-server/internal/gateway/xiaozhi_ws.go`
- `/root/agent-server/internal/voice/turn_detector.go`

完成标准：

- `ServerEndpointEnabled` 能稳定用于实时音频主链，而不是只用于实验 / debug
- `InputPreview` 不再只是“建议 commit”的被动信号，而是 turn orchestration 的主输入之一
- 服务端能稳定输出：
  - speech start
  - partial updated
  - endpoint candidate
  - actual turn accept reason

验证：

```bash
cd /root/agent-server
go test ./internal/gateway ./internal/voice
```

期望结果：

- 网关与 voice 单测通过
- `server_endpoint` 路径能稳定驱动 turn accept

#### S2: 把 session core 从单状态机升级为输入/输出双轨模型

目标：

- 不再让 `CommitTurn()` 成为“输入结束 + 开始 thinking + 进入 speaking”的单一总闸门

范围：

- `/root/agent-server/internal/session/realtime_session.go`
- `/root/agent-server/internal/gateway/realtime_ws.go`
- `/root/agent-server/internal/gateway/output_flow.go`

完成标准：

- 服务端内部至少能区分：
  - input state
  - output state
- speaking 期间仍允许持续输入预览，而不是只能靠“打断后重开下一轮”
- 新状态模型不破坏现有 half-duplex fallback

验证：

```bash
cd /root/agent-server
go test ./internal/session ./internal/gateway
```

期望结果：

- session 层测试可覆盖 speaking + preview + interruption 并存的状态流转

#### S3: 让语音编排层支持真正的“边生成边说”

目标：

- 把当前“流式文字先出、音频等返回后再开始”的模式推进到“稳定意群一出现就可启动 TTS”

范围：

- `/root/agent-server/internal/voice/asr_responder.go`
- `/root/agent-server/internal/voice/speech_planner.go`
- `/root/agent-server/internal/gateway/turn_flow.go`
- `/root/agent-server/internal/voice/synthesis_audio.go`

完成标准：

- 服务端无需等待完整 `TurnResponse` 收口，便能启动首段音频下发
- `response.start`、text delta、audio stream 三者的生命周期可以真正重叠
- 起播 latency 主要受 planner / synthesizer 限制，而不是受 `RespondStream()` 返回时机限制

验证：

```bash
cd /root/agent-server
go test ./internal/voice ./internal/gateway
```

期望结果：

- speech planner 和 audio streaming 相关单测通过
- 首个 text delta 与首个 audio byte 的时间关系可被显式测试

#### S4: interruption policy 从硬 cancel 升级为多策略仲裁

目标：

- 区分：
  - backchannel
  - duck only
  - hard interrupt
  - ignore

范围：

- `/root/agent-server/internal/voice/barge_in.go`
- `/root/agent-server/internal/gateway/realtime_ws.go`
- `/root/agent-server/internal/gateway/output_flow.go`
- `/root/agent-server/internal/voice/session_orchestrator.go`

完成标准：

- 用户短附和音不会默认触发 hard interrupt
- 被打断后 persisted memory 记录的是 heard text，而不是未播完全文
- 可为后续 resume / continue 策略留出接口

验证：

```bash
cd /root/agent-server
go test ./internal/voice ./internal/gateway -run 'BargeIn|SessionOrchestrator|Realtime'
```

期望结果：

- barge-in、heard-text、interruption 相关测试通过

### 7.2 端侧任务块

#### D1: 先做实 AEC / reference 基线，再谈默认全双工

目标：

- 让板端具备真正可支撑全双工的声学条件，而不是只改状态机

范围：

- `prj.conf`
- `components/river_voice/river_voice_runtime_policy.c`
- `components/river_voice/river_voice_preproc_fixed_dsb.c`
- `components/river_voice/river_reference_service.c`
- `components/river_voice/river_playback_service.c`

完成标准：

- 板端 speaking 期间能稳定看到非零 playback reference 活动
- AEC gate 能进入真实 active，而不是长期 `ref_missing / ref_idle`
- 监控日志不再长期停留在：
  - `echo:0B`
  - `ref_peak=0`

验证：

```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
```

期望结果：

- harness 通过
- build 通过
- 板端日志可观察到有效 reference / AEC 活动

#### D2: 从 half-duplex capability 切到可协商 duplex capability

目标：

- 让端侧根据真实运行能力声明：
  - full duplex capable
  - half duplex fallback

范围：

- `components/river_cloud/river_xiaozhi_ws.c`
- `include/river/river_xiaozhi_ws.h`
- 必要时增加端侧本地配置项

完成标准：

- 不再把 `half_duplex=true` 写死
- 端侧 capability 可以随 profile / build / runtime readiness 变化
- 不支持全双工的构建仍保留当前 fallback 行为

验证：

```bash
cd /root/ameba-river
rg -n "half_duplex" components/river_cloud/river_xiaozhi_ws.c
```

期望结果：

- capability 上报不再是固定常量

#### D3: 把本地 endpointing 从硬 turn 终点改成软提示

目标：

- 端侧本地 VAD / silence 仍保留，但不再在 speaking / duplex 模式下直接充当唯一 turn 终点

范围：

- `components/river_cloud/river_cloud_adapter.c`
- `components/river_cloud/river_cloud_internal.h`
- `components/river_voice/river_voice_vad_probe.c`

完成标准：

- duplex 模式下，本地 endpointing 更偏：
  - preview hint
  - interrupt hint
  - near-end speech hint
- 不再在 `tts_start` 到来时无条件关停本地输入 round

验证：

```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
```

期望结果：

- 端侧本地 round close 与服务端 speaking 不再强绑定为互斥关系

#### D4: 端侧播放策略升级为 duck / interrupt / keep-listening

目标：

- 让端侧在 speaking 期间优先尝试“边听边播 + ducking”，必要时再 hard interrupt

范围：

- `components/river_voice/river_playback_service.c`
- `components/river_core/river_session_coordinator.c`
- `components/river_voice/river_voice_vad_probe.c`
- `components/river_cloud/river_cloud_adapter.c`

完成标准：

- `barge_in_listening` 不只是“准备打断”的中间态，而是真正的并行 listening 态
- playback ducking 与 interrupt 有明确阈值和日志
- 误打断率可量化观察

验证：

```bash
cd /root/ameba-river
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000
```

期望结果：

- speaking 期间用户轻度插话时，可观察到 ducking 或 delayed interrupt，而不是总是立即 abort TTS

### 7.3 联调与协议任务块

#### J1: 先以内收方式联调，不急着改公网协议

目标：

- 在不破坏现有设备接入的前提下，把全双工主链跑通

范围：

- `/root/agent-server/internal/gateway/*`
- `components/river_cloud/*`

完成标准：

- 现有 `/v1/realtime/ws` 和当前 envelope 不变
- 两端可以在内部开关下跑 full-duplex candidate path
- half-duplex fallback 仍稳定可用

验证：

```bash
cd /root/agent-server
go test ./internal/gateway ./internal/voice
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
```

期望结果：

- 本地代码侧联调准备完成，但不要求线上 discovery 立刻改口径

#### J2: 稳定后再发布 vNext discovery / protocol 口径

目标：

- 等真全双工主链稳定后，再把 discovery 和协议命名改准确

范围：

- `/root/agent-server/docs/protocols/realtime-session-v0.md`
- `/root/agent-server/docs/protocols/rtos-device-ws-v0.md`
- `/root/agent-server/internal/gateway/realtime.go`
- 端侧 capability / discovery 消费逻辑

完成标准：

- `turn_mode` 不再继续发布 `client_wakeup_client_commit`
- discovery 能显式区分：
  - full duplex
  - server endpoint
  - ducking
  - half-duplex fallback

验证：

```bash
cd /root/agent-server
go test ./internal/gateway
```

期望结果：

- 文档、discovery、运行时行为重新对齐

## 8. 当前建议执行顺序

- 第一优先：
  - `5.170` 端侧 speaking-time uplink continuation
- 第二优先：
  - `5.171` 端侧 duck-first interruption policy
  - `5.172` 至少做出一个 board-profile 级 duplex-ready 声学基线
- 第三优先：
  - `5.173` 默认开启条件、回退条件和回归矩阵
- 最后收口：
  - `J2` discovery / protocol 口径升级

## 9. 当前最重要的判定

- 现阶段“能不能做全双工”的答案是：
  - 能做，但不是只改一个参数或只把 `client_commit` 改成 `server_vad`
- 现阶段“最先该做什么”的答案是：
  - 端侧先补状态同步与 runtime-ready gate
  - 再补 speaking-time keep-listening / ducking / reference 基线
- 若跳过 AEC / reference 直接做逻辑全双工，结果大概率只是：
  - 更快地回声误触发
  - 更频繁地误打断
  - 更差的真实体验

## 10. 端侧具体实施切片

前置已完成切片：

- `5.164` 已把 duplex capability advertisement 放到显式实验闸门后
- `5.165` 已把 `tts_start -> close local round` 放到显式策略闸门后
- `5.167` 已把服务侧 richer `session.update` 字段接入端侧本地缓存与日志
- `C1` 已建立 discovery + `session.start.capabilities` 协商基线
- `C2` 已建立 preview-aware 输入观察事件消费基线，当前只做 hint-only
  解析 / 缓存 / 日志，不改变现有 commit / local-close 语义
- `C3` 已让 `playback_ack=segment_mark_v1` 在 discovery 支持时可真实声明
- `C4` 已把 accepted-turn / playback-truth / fallback 语义与新协议边界对齐

从当前代码基线起，端侧按下面的连续切片继续推进。

### 10.1A 2026-04-16 云侧协议对齐后的端侧修改面

基于 `/root/agent-server` 新增的协议/架构文档，端侧后续改造不应只盯
“本地是否 keep local round”，而应拆成四块并行收口：

- 协商层：
  - discovery 读取 `voice_collaboration`
  - `session.start.capabilities` 按协商结果声明 `preview_events` /
    `playback_ack`
- 观察层：
  - 消费 `input.speech.start` / `input.preview` / `input.endpoint`
  - 继续以 `accept_reason` 作为 accepted-turn 主信号
- 播放事实层：
  - 消费 `audio.out.meta`
  - 回传 `audio.out.started` / `mark` / `cleared` / `completed`
- 本地反射/兜底层：
  - 本地仍保留 reflex VAD、duck、急停、fallback half-duplex
  - 但不再把 preview / endpoint candidate 当成主裁决

这意味着端侧后续切片需要区分两条线：

- 协议协同线：
  - 让端侧成为 preview-aware / playback-truth-aware client
- 声学与本地编排线：
  - 让端侧在 duplex-ready 条件下真正具备“边播边听”的板端条件

2026-04-17 服务侧最新代码更新进一步改变了端侧优先级：

- 复核 `/root/agent-server` 最近的 playback-truth 相关提交后，可以确认：
  - `segment_mark_v1` 已不再是“started/completed 够用”的轻量协作
  - 服务侧现在明确依赖：
    - `audio.out.started`
    - `audio.out.mark`
    - `audio.out.cleared`
    - `audio.out.completed`
  - 这些事实会继续进入 heard-text / interruption / resume 主链
- 因此端侧必须先补齐完整播放事实链，再回到 `5.168` 的本地 duplex runtime
  gate 收口

下面的 `C1`~`C5` 是新增的协议协同切片；`5.168` 之后的切片继续负责本地
运行时与声学收口。

### 10.1B Step C1: discovery + session.start 协商基线

状态：

- 已落地设备侧 baseline：
  - `GET /v1/realtime` discovery 已接入端侧 transport
  - `session.start.capabilities` 已改为“服务端声明 + 本端支持”的协商逻辑
  - `preview_events` 现已随 `C2` 的本端能力补齐而变为可真实声明
  - `playback_ack=segment_mark_v1` 现已随 `C3` 的本端能力补齐而可真实声明
  - 完整的 `segment_mark_v1` 真相链已在 `C5` 落地
  - 下一步由 `5.168` 继续推进本地 runtime-ready duplex gate 收口

目标：

- 让端侧先具备 capability-gated 协作前提，而不是把 preview / playback
  扩展字段写死启用

范围：

- `components/river_cloud/river_xiaozhi_ws.c`
- 必要时：
  - `include/river/river_xiaozhi_ws.h`
  - `components/river_cloud/river_cloud_internal.h`
  - `tools/agent_server_debug/probe_realtime.py`

实施内容：

- 在端侧建立 discovery 消费路径，至少能读取：
  - `voice_collaboration.preview_events.*`
  - `voice_collaboration.playback_ack.*`
- `session.start.capabilities` 根据“服务端声明 + 本端支持”双向协商决定是否上报：
  - `preview_events=true`
  - `playback_ack.mode=segment_mark_v1`
- 若 discovery 缺失、mode 未识别或服务端未开启：
  - 自动回退到当前兼容基线

完成标准：

- 端侧日志能明确打印：
  - `preview_events=yes|no`
  - `playback_ack=segment_mark_v1|-`
  - `discovery_voice_collaboration=yes|no`
- 默认兼容路径不被破坏

### 10.1C Step C2: preview-aware 输入事件消费

状态：

- 已落地设备侧 baseline：
  - 端侧已解析并缓存：
    - `input.speech.start`
    - `input.preview`
    - `input.endpoint`
  - 端侧 transport 与 cloud adapter 都新增了 preview observation 状态日志
  - `session.start.capabilities.preview_events` 现在会在 discovery 支持时如实声明
  - 当前范围仍保持 observation-only，不改变 commit / local-close / accept 语义
  - 下一步由 `5.168` 与后续本地运行时切片继续把 duplex runtime 行为收口

目标：

- 让端侧能消费服务端的 preview-aware 观察事件，但不把它们误当成 stop /
  commit / accepted-turn 命令

范围：

- `components/river_cloud/river_xiaozhi_ws.c`
- `components/river_cloud/river_cloud_adapter.c`
- 必要时：
  - `include/river/river_xiaozhi_ws.h`
  - `components/river_cloud/river_cloud_internal.h`

实施内容：

- 解析并缓存：
  - `input.speech.start`
  - `input.preview`
  - `input.endpoint`
- 为端侧本地状态机提供：
  - `preview_id`
  - partial 文本
  - endpoint candidate / reason / audio_offset_ms
- 明确语义边界：
  - `input.preview` 只用于观察/UI/日志
  - `input.endpoint` 只用于 hint，不驱动本地强 commit
  - accepted-turn 仍以 `session.update.accept_reason` 为准

完成标准：

- 板端日志能看到：
  - `input.preview`
  - `input.endpoint`
  - `preview_id`
  - `audio_offset_ms`
- 现有 commit / local-close 路径默认保持不变

### 10.1D Step C3: playback-truth 上下文与 ACK 基线

状态：

- 已落地设备侧 baseline：
  - 端侧已解析并缓存：
    - `audio.out.meta`
  - 端侧 transport 与 cloud adapter 都新增了 playback meta 状态日志
  - `session.start.capabilities.playback_ack` 现在会在 discovery 支持时如实声明：
    - `segment_mark_v1`
  - 先前最小 ACK 基线已落：
    - `audio.out.started`
    - `audio.out.completed`
  - 后续 `C5` 已补齐剩余 ACK：
    - `audio.out.mark`
    - `audio.out.cleared`
  - ACK 发送继续走现有 XiaoZhi IO task / control queue 的异步低优先级路径，不阻塞本地播放
  - 下一步由 `5.168` 继续推进本地 runtime-ready duplex gate 收口

目标：

- 让端侧先具备最小 Tier-1 播放事实回报能力，给服务端的 heard-text /
  truncate / resume 链路提供可信事实

范围：

- `components/river_cloud/river_xiaozhi_ws.c`
- `components/river_cloud/river_cloud_adapter.c`
- 必要时：
  - `include/river/river_xiaozhi_ws.h`
  - `components/river_cloud/river_cloud_internal.h`
  - `components/river_cloud/river_cloud_xiaozhi_session.c`

实施内容：

- 解析 `audio.out.meta` 并缓存：
  - `response_id`
  - `playback_id`
  - `segment_id`
  - `expected_duration_ms`
  - `is_last_segment`
- 先落最小 ACK：
  - `audio.out.started`
  - `audio.out.completed`
- 第二阶段再补：
  - `audio.out.mark`
  - `audio.out.cleared`
- ACK 发送必须异步、低优先级、不阻塞播放线程

完成标准：

- 端侧日志能看到播放上下文建立与 ACK 发送结果
- ACK 发送失败时不阻塞本地播放
- 默认未协商时完全不发送这些扩展事件

### 10.1E Step C4: accepted-turn / playback-truth / fallback 语义对齐

状态：

- 已落地设备侧 baseline：
  - accepted-turn 只在 cloud runtime 中由 `session.update.accept_reason` 确认
  - pending transcript 只有在 accepted-turn 成立后才会发出既有
    `RIVER_CLOUD_ASR_EVENT_FINAL`
  - 新 listen round / 新 wake window 打开前会显式清空 transport 侧
    `session.update` 缓存，避免上一轮 `accept_reason` 泄漏到新一轮
  - preview / endpoint 继续保持 observation-only，不被提升为 accepted-turn
    或 commit 命令
  - playback meta 日志已改为 playback fact 口径，不再伪装成策略事件
  - half-duplex 保守路径现在会显式记录 fallback reason：
    - `await_accept_reason`
    - `half_duplex_experiment_disabled`
    - `half_duplex_no_playback_reference`
    - `half_duplex_capture_held_during_playback`
  - transport/cloud status 现已暴露一条独立的 turn semantics 观测面：
    - `accepted`
    - `accept_reason`
    - `turn_id`
    - `input_state`
    - `output_state`
    - `barge_in_enabled`
    - `fallback`
  - `C5` 已在此语义边界上补齐完整 playback truth 终态链
  - 下一步由 `5.168` 继续推进 runtime-ready duplex gate 收口

目标：

- 让端侧本地状态机与 2026-04-16 云侧文档的职责边界完全一致

范围：

- `components/river_cloud/river_cloud_adapter.c`
- `components/river_cloud/river_cloud_xiaozhi_session.c`
- `components/river_cloud/river_xiaozhi_ws.c`
- 必要时：
  - `components/river_core/river_interaction_state.c`

实施内容：

- accepted-turn 只认：
  - `session.update.accept_reason`
- preview 事件只驱动：
  - 本地 cue
  - 日志
  - hint-only 策略
- playback fact 只表达：
  - 已播
  - 已清空
  - 已完成
  不能反向当成策略命令
- 网络异常、协商失败、AEC/reference 不合格时明确回退到：
  - 当前 half-duplex / client-commit 基线

完成标准：

- 端侧日志能清楚区分：
  - `preview observed`
  - `turn accepted`
  - `playback fact`
  - `fallback`
- 本地状态机不会再把 preview / endpoint candidate 当成 accepted-turn

### 10.1F Step C5: 补齐 segment_mark_v1 播放真相链

状态：

- 已落地设备侧 baseline：
  - 基于对 `/root/agent-server` 最新 playback-truth 提交的复核，端侧优先补齐了
    `segment_mark_v1` 的完整事实链
  - 当前端侧已支持完整 ACK：
    - `audio.out.started`
    - `audio.out.mark`
    - `audio.out.cleared`
    - `audio.out.completed`
  - 当前端侧已把多 segment 播放上下文收束为本地 segment 队列，并显式维护：
    - `last_started_segment_id`
    - `last_fully_heard_segment_id`
    - `terminal_ack`
    - `clear_reason`
  - 本地 clear/interrupt/network-lost/transport-closed/write-failed 路径已统一收敛到
    playback-truth 终态，而不是只 reset 本地播放状态
  - `session.start.capabilities.playback_ack` 只有在本端和 discovery 都完整支持
    四类 ACK 时才会如实声明 `segment_mark_v1`
  - 下一步由 `5.168` 继续推进 runtime-ready duplex gate 收口

目标：

- 让端侧向服务侧提供可信的完整播放事实链，为 heard-text /
  interruption / resume 提供真实边界，而不是只上报开始/结束两个稀疏点

范围：

- `components/river_cloud/river_cloud_adapter.c`
- `components/river_cloud/river_cloud_xiaozhi_session.c`
- `components/river_cloud/river_cloud_internal.h`
- `components/river_cloud/river_xiaozhi_ws.c`
- `include/river/river_xiaozhi_ws.h`

实施内容：

- 在端侧建立 playback-level segment 队列：
  - 同一 `response_id + playback_id` 下按 `audio.out.meta` 追加 segment
  - response/playback 切换时才清空旧播放上下文
- 基于实际本地播放进度推进事实：
  - 第一次真实本地 write 成功后发送 `audio.out.started`
  - 按 wall-clock/segment duration 周期发 `audio.out.mark`
  - segment 满额后更新 `last_fully_heard_segment_id`
- 对本地 clear 终态采用保守规则：
  - 先补一条当前 segment 的最终 `mark`
  - 仅当已知 `last_fully_heard_segment_id` 时才发 `audio.out.cleared`
  - clear-before-start 不伪造 `cleared`
- 对自然播放结束采用 playback-level 终态：
  - 先补齐剩余 segment 的最终 mark
  - 最后只发一次 `audio.out.completed`
- 所有 ACK 保持异步、低优先级，不阻塞本地播放线程

完成标准：

- 板端日志能清楚区分：
  - `started`
  - 周期 `mark`
  - `cleared`
  - `completed`
- `segment_mark_v1` 只在完整支持条件成立时声明
- 本地异常停播不再出现“只 reset，不结算 playback truth”的旧路径

### 10.1 Step 5.167: 消费服务侧 richer session.update

状态：

- 已落地，当前步骤只做：
  - richer field 解析
  - 本地缓存
  - transport/status 日志增强
- 当前步骤仍不根据这些字段改变 round / playback / interrupt 行为

目标：

- 让端侧本地状态机先“看见”服务侧新增的双轨信号，而不是继续只盯顶层 `state`

范围：

- `components/river_cloud/river_xiaozhi_ws.c`
- 必要时：
  - `include/river/river_xiaozhi_ws.h`
  - `components/river_cloud/river_cloud_adapter.c`

实施内容：

- 解析并记录：
  - `input_state`
  - `output_state`
  - `barge_in_enabled`
  - `turn_id`
  - `accept_reason`
- 保持老服务兼容：
  - 字段缺失时仍只按顶层 `state` 工作
- 先做“状态透传 + 日志 + 本地缓存”，不在本步引入行为变化

完成标准：

- 板端日志能明确区分：
  - `state=speaking input_state=previewing output_state=speaking`
  - `accept_reason=server_endpoint|audio_commit|text_input`
- 不破坏现有默认交互

建议验证：

```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
rg -n "input_state|output_state|accept_reason|barge_in_enabled|turn_id" \
  components/river_cloud/river_xiaozhi_ws.c
```

### 10.2 Step 5.168: runtime-ready duplex gate

状态：

- 已落地设备侧 baseline：
  - XiaoZhi speaking-time duplex 判定已从“profile 静态具备 playback reference
    能力”升级为“当前运行时真实 `duplex_ready`”
  - 新增统一 runtime duplex 评估，当前至少综合：
    - duplex experiment gate
    - active profile capability
    - reference service state
    - 最近 reference activity / queue peak
    - AEC gate state
  - reference service stats 现已暴露最近运行时活跃时间：
    - `last_open_ms`
    - `last_reset_ms`
    - `last_write_ms`
    - `last_read_ms`
  - `tts_start` keep-open / close、capture-held-during-playback、status dump
    现都统一输出：
    - `duplex_ready=yes|no`
    - `reason=experiment_off|profile_no_ref|ref_idle|aec_blocked|ready`
  - 当前 playback epoch 还会记住是否曾经达到 `duplex_ready=yes`，本地
    `no_ref reopen guard` 不再只依赖静态 profile capability
  - 默认 half-duplex 保守行为未变化；仅补齐了更精确的 fallback reason：
    - `half_duplex_ref_idle`
    - `half_duplex_aec_blocked`
  - 下一步由 `5.169` 继续推进 speaking-time local endpoint 软化

目标：

- 把当前“profile 具备 playback reference 能力”升级为“当前运行时真的 ready 才允许 duplex 路径”

范围：

- `components/river_cloud/river_cloud_xiaozhi_session.c`
- `components/river_cloud/river_cloud_adapter.c`
- `components/river_voice/river_reference_service.c`
- `components/river_voice/river_voice_runtime_policy.c`
- 必要时：
  - `include/river/river_reference_service.h`
  - `include/river/river_voice_runtime_policy.h`

实施内容：

- 新增统一 `duplex_ready` 判定，至少综合：
  - duplex experiment gate
  - active profile capability
  - reference service state
  - 最近 reference activity / peak
  - AEC gate state
- 所有 speaking-time keep-open 行为只认这个 runtime gate，不再只认静态 profile

完成标准：

- 日志能明确给出：
  - `duplex_ready=yes|no`
  - `reason=experiment_off|profile_no_ref|ref_idle|aec_blocked|ready`
- 默认配置行为保持不变

建议验证：

```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
```

板测关注日志：

- `ref_peak`
- `echo`
- `duplex_ready`
- `playback_ref`

### 10.3 Step 5.169: speaking-time local endpoint 软化

状态：

- 已落地设备侧 baseline：
  - duplex-ready + speaking-time 路径下，端侧新增了 `endpoint_soft_close`
    运行时状态：
    - `pending`
    - `reason`
    - `left_ms`
  - `input.speech.start` 现会在 duplex-ready speaking 路径上输出显式
    `interrupt hint`
  - `input.endpoint(candidate=true)` 现会输出 `hint-only endpoint`，并只
    arm 一个短的 deferred local close，而不是立刻硬收尾
  - 本地 post-roll silence 在 duplex-ready speaking 路径上也改为：
    - 先 arm deferred local close
    - 若短窗口内 speech 恢复则取消
    - 若超时仍静音才真正进入现有 `finish_active_stream()` 收尾链
  - 相关 state 会在 session close / transport reset 时清理，避免旧 hint
    泄漏到下一轮
  - half-duplex 默认路径保持不变
  - 下一步由 `5.170` 继续推进 speaking-time uplink continuation

目标：

- duplex-ready 条件成立时，`tts_start` 或短时 silence 不再直接成为本地 turn 的硬终点

范围：

- `components/river_cloud/river_cloud_adapter.c`
- `components/river_cloud/river_cloud_internal.h`
- `components/river_cloud/river_cloud_xiaozhi_session.c`
- `components/river_voice/river_voice_vad_probe.c`

实施内容：

- 引入 `deferred close / hint-only` 路径
- 将本地 endpoint 在 duplex 实验态下从：
  - `hard close`
  - 调整为：
    - `preview hint`
    - `interrupt hint`
    - `deferred local close`

完成标准：

- half-duplex 默认配置仍是原行为
- duplex-ready 实验路径下：
  - `tts_start` 不强关本地 round
  - 短 silence 不立刻强收尾

建议验证：

```bash
cd /root/ameba-river
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
```

板测关注日志：

- `tts_start keeps local round open`
- `deferred local close`
- `hint-only endpoint`

### 10.4 Step 5.170: speaking-time uplink continuation

状态：

- 已落地设备侧 baseline：
  - duplex-ready + speaking-time 路径下，`endpoint_soft_close` 超时不再在
    output lane 仍处于 speaking 时直接触发本地 `finish_active_stream()`
  - speaking 期间即使本地出现短 silence / endpoint hint，端侧也会继续保持：
    - `listening=yes`
    - 本地 uplink round 活跃
    - 既有 websocket / dialog 不进入 listen-stop 抖动
  - 一旦 output lane 脱离 speaking，已经到期的 deferred close 会立即复用既有
    `audio.in.commit` / stop pipeline 收尾
  - half-duplex 默认路径保持不变
  - 下一步由 `5.171` 继续推进 duck-first interruption policy

目标：

- 在 duplex-ready 实验路径下，真正做到“播的时候仍能持续上传近端语音”，但不破坏现有兼容提交边界

范围：

- `components/river_cloud/river_cloud_adapter.c`
- `components/river_cloud/river_cloud_xiaozhi_session.c`
- `components/river_cloud/river_xiaozhi_ws.c`

实施内容：

- 保持 speaking 期间本地采集与 uplink 活跃
- 避免出现：
  - 重复 `session.start`
  - 本地 listen / stop 抖动
  - commit 风暴
- 先保持兼容：
  - 仍允许 `audio.in.commit`
  - 不在本步直接引入端侧 server-owned turn-finalization

完成标准：

- duplex-ready 路径下 speaking 期间可见：
  - 本地 listening 仍为 `yes`
  - uplink 继续推进
- 默认路径不变

建议验证：

```bash
cd /root/ameba-river
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000
```

板测关注日志：

- `listening=yes`
- `asr stream active`
- `session.update state=speaking input_state=previewing`

### 10.5 Step 5.171: duck-first 本地打断仲裁

状态：

- 已落地设备侧 baseline：
  - 端侧 VAD probe 已从“命中门限就直接 interrupt”改成：
    - `duck_only`
    - `release`
    - `hard_interrupt`
  - 第一段满足条件的 near-end speech 会先触发本地 duck，而不是立刻硬停播
  - 若后续语音很短或证据不足，会在短 release window 后自动解除 duck
  - 只有持续 near-end speech 才会升级为现有 `interrupt_tts` 路径
  - 日志现可明确区分：
    - `barge-in duck`
    - `barge-in duck release`
    - `barge-in interrupt`
  - 下一步由 `5.172` 继续推进 board-profile 级 duplex-ready 声学基线

目标：

- 让端侧从“near-end speech 一来就 interrupt”升级到“先 duck，再按阈值升级 hard interrupt”

范围：

- `components/river_voice/river_voice_vad_probe.c`
- `components/river_voice/river_playback_service.c`
- `components/river_cloud/river_cloud_adapter.c`

实施内容：

- 引入端侧 speaking-time 近端语音分级：
  - 短附和 / 轻插话 -> `duck_only`
  - 持续近端语音 -> `interrupt`
- 与服务侧新策略对齐，但本步先保证端侧本地仲裁自洽

完成标准：

- 短 near-end speech 不再总是立刻 hard stop TTS
- 日志能区分：
  - `duck`
  - `interrupt`
  - `reason`

建议验证：

```bash
cd /root/ameba-river
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000
```

板测场景：

- 轻声附和
- 短插话
- 持续插话

### 10.6 Step 5.172: 做出一个 board-profile 级 duplex-ready 声学基线

目标：

- 不再只停留在代码逻辑实验，至少做出一个“真实 reference / AEC 能工作”的板级基线

范围：

- `prj.conf`
- `board/rtl8730e/profiles/*`
- `components/river_voice/river_voice_preproc_fixed_dsb.c`
- `components/river_voice/river_reference_service.c`
- 必要时：
  - `components/river_voice/river_voice_webrtc_aecm_adapter.c`

实施内容：

- 选定一个实验 profile
- 让该 profile 在 speaking 期间稳定出现：
  - 非零 `ref_peak`
  - 非零 echo / reference activity
  - `duplex_ready=yes`

完成标准：

- 至少一个 board profile 可被明确标记为：
  - `duplex-ready experimental`
- 该 profile 的板测日志不再长期停留在：
  - `echo:0B`
  - `ref_peak=0`

建议验证：

```bash
cd /root/ameba-river
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source /root/ameba-river/env.sh; python3 /root/ameba-rtos/ameba.py build -p'
```

板测关注日志：

- `echo`
- `ref_peak`
- `aec`
- `duplex_ready`

2026-04-17 落地记录：

- 当前分支已切到专用实验 board-profile：
  - `CONFIG_RIVER_XIAOZHI_FULL_DUPLEX_EXPERIMENT_EN=y`
  - `CONFIG_RIVER_WEBRTC_AECM_EXPERIMENT_EN=y`
  - `CONFIG_RIVER_VOICE_PREPROC_PROFILE_FIXED_DSB_WEBRTC_AECM=y`
- 端侧 `duplex_ready` 对 native-capture-ref 的判断不再只看
  `playback_active`，而是接入了 AECM 预处理链发布的真实 reference
  telemetry：
  - `ref_activity`
  - `ref_peak`
  - `ref_ratio_q15`
  - freshness age
- `vad_probe` 的 `ref_peak` 观测已经补到原生采集 `ch3`：
  - 实验 profile 下，`ref_peak` 与本地 barge-in 比较逻辑不再只依赖
    playback-ring software ref
  - 因而 speaking 期间的板端日志现在能直接反映原生 `mic0 + mic1 + ref`
    输入模型里的 far-end reference
- XiaoZhi 运行态日志已经补齐：
  - `ref_peak`
  - `ref_ratio_q15`
  - 可用于和：
    - `webrtc_aecm ref_state=...`
    - `vad state=... ref_peak=...`
    - `xiaozhi duplex_ready=...`
    做同轮对照
- 当前结论：
  - `5.172` 的“实验 profile + 声学观测面”已经具备
  - 下一步转入 `5.173`，把默认开启条件和 fallback matrix 编码收口

### 10.7 Step 5.173: 默认开启条件与回退矩阵

目标：

- 在 experiment 路径打通后，把“什么时候仍必须 fallback half-duplex”编码成明确规则

范围：

- `Kconfig`
- `prj.conf`
- `doc/FULL_DUPLEX_VOICE_EXECUTION_PLAN_ZH.md`
- 必要时：
  - `components/river_cloud/river_cloud_xiaozhi_session.c`

实施内容：

- 明确 default-on 之前必须满足的条件：
  - richer session state 已消费
  - runtime-ready gate 已稳定
  - speaking-time uplink 已稳定
  - duck-first 策略已稳定
  - 至少一个 board profile 的 AEC / reference 合格
- 明确 fallback 条件：
  - reference 丢失
  - AEC gate blocked
  - 板型不支持
  - 线上服务未提供所需 lane-state / behavior

完成标准：

- 默认构建继续稳定
- experiment 构建有明确进入条件和回退条件
- 回归矩阵覆盖：
  - wake
  - first turn
  - follow-up
  - speaking-time barge-in
  - reconnect / error fallback

2026-04-17 落地记录：

- 已新增独立的 `default-on` 编译闸门：
  - `CONFIG_RIVER_XIAOZHI_FULL_DUPLEX_DEFAULT_ON_EN`
  - 用于把“实验代码已编进来”和“本 session 默认按全双工实验态宣告”
    这两个概念拆开
- 当前分支实验 profile 已显式开启：
  - `CONFIG_RIVER_XIAOZHI_FULL_DUPLEX_EXPERIMENT_EN=y`
  - `CONFIG_RIVER_XIAOZHI_FULL_DUPLEX_DEFAULT_ON_EN=y`
- `session.start` 的 `half_duplex` 广告不再只看 compile-time experiment，而是
  需要同时满足：
  - active board/profile 支持 playback reference
  - discovery 已声明 `voice_collaboration`
  - server endpoint `available=true`
  - server endpoint `enabled=true`
  - `preview_events` 已成功协商
  - `playback_ack.mode=segment_mark_v1` 已成功协商
- 端侧 speaking-time keep-open / capture-hold / status 也已切到同一套
  fallback matrix：
  - `half_duplex_default_policy_disabled`
  - `half_duplex_service_collaboration_unavailable`
  - `half_duplex_service_endpoint_unavailable`
  - `half_duplex_service_endpoint_disabled`
  - `half_duplex_service_preview_unavailable`
  - `half_duplex_service_playback_ack_unavailable`
  - 以及已有本地 runtime 原因：
    - `half_duplex_no_playback_reference`
    - `half_duplex_ref_idle`
    - `half_duplex_aec_blocked`
- 当前结论：
  - `5.173` 的默认开启条件和回退矩阵已编码收口
  - 下一步重点转到板端回归，验证 discovery 组合、wake/first-turn/follow-up
    以及 speaking-time barge-in 下的实际默认路径是否与日志矩阵一致

2026-04-17 调试补充（5.173A）：

- 为等待服务侧新的 collaboration / preview / playback-ack 主链继续落地，
  端侧先补了一轮纯调试日志，不改变当前行为策略
- 新增的日志覆盖：
  - discovery refresh / `session.start` / discovery 失败回退时的协商快照
  - `preview_events` 与 `playback_ack` 的未协商原因
  - `session.update` 语义稀疏、accept 前 turn 语义变化
  - `input.speech.start` / `input.preview` / `input.endpoint` 稀疏载荷
  - `audio.out.started/mark/cleared/completed` ACK 写回失败
- 目的：
  - 后续服务侧联调时，可以快速区分：
    - discovery / capability 没对齐
    - server 提前发了 preview 事件
    - payload shape 不完整
    - 端侧 ACK 回写失败或连接状态异常

2026-04-17 调试补充（5.173B）：

- 继续保持 logging-only，不改当前 duplex 行为和 websocket contract
- 补齐 transport 侧真正缺的时序观测：
  - `input.speech.start`
  - `input.preview`
  - `input.endpoint`
  - `session.update.accept_reason`
  - `response.start`
  - `audio.out.meta`
- 新增的日志与缓存行为：
  - `session.update` 不再在每次稀疏 payload 上清空本轮已接受语义
  - accept 只在真正 latch 到 accepted-turn 时更新 `last_accept_at_ms`
  - `response.start` / `audio.out.meta` 会带出：
    - `from_accept_ms`
    - `from_preview_start_ms`
    - `from_preview_update_ms`
    - `from_response_start_ms`
- `river xiaozhi status` 现可直接看到：
  - `timing_age_ms`
  - `timing_chain_ms`
- 目的：
  - 后续服务侧联调时，可以快速区分：
    - preview 很早到，但 accepted-turn 很晚
    - accepted-turn 已到，但 `response.start` 慢
    - `response.start` 已到，但首个 `audio.out.meta` 慢

2026-04-17 端侧修复（5.174）：

- 基于板端日志回归，先处理当前最影响体验的两条设备侧问题，而不是继续等
  服务侧协商链路变化：
  - 半双工 / `no_ref` 回退下的本地 barge-in interrupt 过于激进
  - 下行播放预缓冲和 AudioTrack buffer 太浅，容易只播一小段
- 本轮端侧收口：
  - `no_ref` 回退播放改用更严格的 barge-in 判定：
    - `ref_margin_peak`: `448 -> 960`
    - `min_enhanced_peak`: `1200 -> 2200`
    - `ratio_pct`: `150 -> 180`
    - duck: `1 -> 3` hit frames
    - interrupt: `5 -> 12` hit frames
  - XiaoZhi 下行播放缓冲加深：
    - ring: `16 -> 32`
    - start watermark: `8 -> 12`
    - playback buffer: `3 -> 6`
    - compact fallback buffer: `2 -> 4`
  - 播放启动日志现在会直接打印：
    - `start=...`
    - `buffer=...`
- 下一步重点：
  - 必须先把 `5.174` 烧到板上，再回归用户给出的原始场景
  - 重点看：
    - `barge-in interrupt: mode=no_ref_strict`
    - `xiaozhi playback start: ... start=12 ... buffer=6 ...`
    - `underrun`
    - `xiaozhi playback write failed`

2026-04-17 端侧修复（5.175）：

- 在用户追加的新板端日志里，问题已经进一步收敛到三条明确的设备侧缺陷：
  - 本地播放增益仍然过高，`gain=5/2` 容易把服务端已经偏热的语音继续放大
  - `no_ref_strict` 仍会在半双工回退路径下对 TTS 执行硬打断
  - `write_failed` 仍走“清空播放上下文 + reset 下行”的破坏性路径，导致
    一句话播到一半就断、然后整段重起
- 本轮端侧收口：
  - XiaoZhi 本地播放增益改回 unity：
    - `5/2 -> 1/1`
  - 严格 `no_ref` barge-in 改为：
    - 保持 duck
    - suppress hard interrupt
    - monitor 日志改为：
      - `barge-in interrupt suppressed: mode=no_ref_duck_only ...`
  - `write_failed` 改为 rebuffer / resume：
    - 保留当前 playback meta / segment 队列
    - 保留失败帧做 retry，而不是直接丢掉
    - ACK 计时在重缓冲期间暂停，避免 `audio.out.mark` 虚增
    - 恢复起播门槛改为更深的：
      - `18` frames
    - monitor 日志新增：
      - `rebuffer=yes`
      - `xiaozhi playback rebuffer requested: ...`
      - `xiaozhi playback rebuffer resumed: ...`
- 下一步重点：
  - 必须先把 `5.175` 烧到板上，再回归同一条用户场景
  - 重点看：
    - `xiaozhi playback start: ... gain=1/1 ... rebuffer=yes|no`
    - `barge-in interrupt suppressed: mode=no_ref_duck_only`
    - `xiaozhi playback rebuffer requested`
    - `xiaozhi playback rebuffer resumed`
    - `underrun`
    - `xiaozhi playback write failed`
