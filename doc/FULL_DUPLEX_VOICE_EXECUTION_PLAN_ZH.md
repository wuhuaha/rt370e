# Full-Duplex Voice Execution Plan

Status: active
Last Updated: 2026-04-15
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

- 端侧当前仍主动声明 `half_duplex=true`：
  - `components/river_cloud/river_xiaozhi_ws.c`
- 端侧当前在服务端 `tts_start` 到来时会关闭本地 ASR round，而不是保持并行输入：
  - `components/river_cloud/river_cloud_adapter.c`
- 端侧已经有：
  - `barge_in_listening` 交互态
  - 本地 playback interrupt
  - playback ducking API
  - 但默认构建未启用 WebRTC AECM 实验路径：
    - `prj.conf`
- 近期板端日志多次出现：
  - `echo:0B`
  - `ref_peak=0`
  - 说明全双工真正可用之前，AEC / reference 仍是显著前置条件
- 服务侧当前 discovery / config 仍默认发布：
  - `turn_mode = client_wakeup_client_commit`
  - `ServerEndpointEnabled = false`
- 服务侧已经具备部分关键骨架：
  - `StreamingTranscriber`
  - `InputPreview`
  - `SilenceTurnDetector`
  - adaptive barge-in
  - `StreamingResponder`
  - `SpeechPlanner`
  - heard-text persistence after interruption
- 但服务侧当前仍存在三处关键限制：
  - `InputPreview` 主要仍用于 `commit suggestion`
  - `RealtimeSession` 仍是单状态机：
    - `active`
    - `thinking`
    - `speaking`
  - `AudioStream` 真正开始下发仍依赖 `RespondStream()` 返回 `TurnResponse`
- 因此当前最准确的定位是：
  - 传输层已经具备双工基础
  - 打断层已有初步骨架
  - 真正缺的是跨 ASR / endpointing / planning / TTS / interruption 的
    `Voice Orchestration Core`

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
  - `D1` 端侧 AEC / reference 基线
  - `S1` 服务侧 server endpoint 主路径化
- 第二优先：
  - `S3` 增量 TTS 真正提前启动
  - `D3` 端侧 endpointing 软化
- 第三优先：
  - `S4` 与 `D4` 的 interruption / ducking 仲裁
- 最后收口：
  - `S2` 双轨 session core
  - `J2` discovery / protocol 口径升级

## 9. 当前最重要的判定

- 现阶段“能不能做全双工”的答案是：
  - 能做，但不是只改一个参数或只把 `client_commit` 改成 `server_vad`
- 现阶段“最先该做什么”的答案是：
  - 端侧先补声学前提
  - 服务侧补厚 `Voice Orchestration Core`
- 若跳过 AEC / reference 直接做逻辑全双工，结果大概率只是：
  - 更快地回声误触发
  - 更频繁地误打断
  - 更差的真实体验
