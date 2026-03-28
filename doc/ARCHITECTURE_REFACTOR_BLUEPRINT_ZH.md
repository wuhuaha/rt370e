# Ameba River 架构重构蓝图

日期：2026-03-23

## 1. 目标

本次重构的目标不是“整理代码风格”，而是把项目提升到长期可维护、可扩展、可调优的工程结构。

目标标准：

- 性能路径清晰，可定位热点
- 运行时状态单一归属，不多头拥有
- 模块边界清楚，职责尽量单一
- 日志可映射到明确的子系统责任人
- 接口层稳定，内部实现允许持续演进

参考风格来自成熟的 session-centered 开源项目，尤其是本地参考代码：

- `/root/agents/livekit-agents/livekit/agents/voice/agent_session.py`
- `/root/agents/livekit-agents/livekit/agents/voice/remote_session.py`

可借鉴的核心思想：

- 顶层入口只负责 wiring，不负责业务编排
- session orchestration 独立成层
- transport/runtime 与 policy 解耦
- 数据面与控制面分层

## 2. 当前主要问题

### 2.1 入口层历史性膨胀

`river_app.c` 曾经承接了过多运行时策略，导致：

- bootstrap 和 runtime policy 混在一起
- 会话行为难以复用和测试
- 新特性容易继续堆到入口层

### 2.2 Cloud 层职责混杂

`river_cloud_adapter.c` 目前仍是最大热点之一，混合了：

- provider façade
- xiaozhi session lifecycle
- conversation window policy
- uplink/downlink worker runtime
- pre-roll / stream open / stream close
- ASR result fanout

这使得 reopen、内存压力、状态边界问题难以快速收敛。

### 2.3 Voice 层数据面与诊断面耦合较深

`river_voice_kws.cc`、`river_voice_vad_probe.c` 既承担实时路径，又承接了较多诊断和实验逻辑，导致：

- 热路径阅读成本高
- queue 与 worker 责任边界不够锐利
- 调优时容易影响非关键路径

### 2.4 公共接口和内部接口边界不够严格

当前有些内部能力缺少统一的 private/internal 边界，容易出现：

- 不稳定内部接口被误当成公共 API 使用
- 组件内实现细节泄露到全局 include 层

## 3. 目标架构

### 3.1 Bootstrap Layer

职责：

- 进程/系统启动顺序
- 服务初始化
- 依赖装配
- 顶层状态打印

禁止承担：

- wake admission 策略
- playback / ASR 协调
- conversation session 决策

### 3.2 Session Coordination Layer

职责：

- interaction state 迁移
- wakeword 受理与合并
- ASR session 生命周期编排
- barge-in / interrupt 策略

禁止承担：

- 云传输细节
- DSP/KWS 运行时实现
- 硬件初始化

### 3.3 Voice Runtime Layer

职责：

- capture / preproc / detector / KWS 数据面
- worker / queue / pre-roll / gate
- 实时路径状态与统计

禁止承担：

- cloud conversation policy
- app 级会话策略

### 3.4 Cloud Session Layer

职责：

- provider session lifecycle
- conversation window policy
- uplink/downlink transport workers
- provider-specific runtime state
- ASR/TTS bridge

禁止承担：

- app 级 wake 受理策略
- voice runtime 细节

### 3.5 Capability / Policy Layer

职责：

- feature flag
- profile / capability selection
- experiment gating

目标是让任何一个行为都能回答：

- 这是 bootstrap 行为？
- 这是 session policy？
- 这是 transport/runtime？
- 这是 product capability？

## 4. 模块映射

### 当前已完成

- `components/river_core/river_app.c`
  已降级为 bootstrap / wiring
- `components/river_core/river_session_coordinator.c`
  成为 session orchestration 的显式 owner

### 下一阶段目标

- `components/river_cloud/river_cloud_adapter.c`
  拆为 façade + session policy + provider runtime
- `components/river_voice/river_voice_kws.cc`
  拆为 runtime worker / queue gate / observability
- `components/river_voice/river_voice_vad_probe.c`
  拆为 detector bridge / diagnostics / experiment hooks

## 5. 推荐分层切分

### Cloud

建议目标文件：

- `river_cloud_adapter.c`
  保留对外 API façade
- `river_cloud_internal.h`
  统一内部 context / internal contract
- `river_cloud_xiaozhi_session.c`
  conversation window、session open/close、wake/follow-up policy
- `river_cloud_xiaozhi_runtime.c`
  uplink/downlink worker、decoder/encoder、ring runtime
- `river_cloud_asr_bridge.c`
  provider-independent stream open/feed/finish 和 result fanout

### Voice

建议目标文件：

- `river_voice_kws_runtime.cc`
- `river_voice_kws_gate.cc`
- `river_voice_vad_probe_runtime.c`
- `river_voice_vad_probe_diag.c`

## 6. 重构原则

1. 先切 ownership，再谈风格统一。
2. 一个文件只保留一种主要责任。
3. 先建立 internal boundary，再搬运代码。
4. 每一刀必须全量编译通过。
5. 不为了“文件变小”而制造跨模块回调地狱。
6. 热路径优先保证低分支、低耦合、低观测干扰。

## 7. 成功标准

重构成功的标准不是“文件更好看”，而是：

- 新功能知道该放哪一层
- 性能问题知道该看哪一层
- 状态错乱知道该追哪一层
- 运行时日志能直达负责模块
- 模块可以逐层替换而不需要全局联动

这才是接近顶级开源项目的结构标准。
