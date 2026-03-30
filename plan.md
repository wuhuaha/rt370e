# DS-CNN Branch Refactor Plan

Date: 2026-03-30
Branch: `DS-CNN`

## Current Objective

当前分支目标已经从“继续模型迁移”切换为“先把现有板端链路重构到稳定、可维护、可继续扩展的基线”。

但在继续主线重构前，当前插入一个更高优先级的短平快热修步骤：

- 先把基线 `KWS` 模型替换为 `bc_resnet_best.tflite`
- 验证新的本地唤醒模型能否更稳定地拉起 `XiaoZhi` 会话
- 完成该替换后，再继续后续主线重构

当前优先级顺序：

1. 修正运行时正确性
2. 收紧堆内存和热路径
3. 收敛模块边界与职责
4. 最后再做体积和 clean code 收尾

本轮不再把“继续加功能”作为主目标。

## Current Baseline

当前业务主链已经具备：

- 本地 `KWS`
- 本地 `VAD`
- `Wi-Fi` 自动连接
- `XiaoZhi realtime` 会话
- `TTS` 下行播放

当前已知问题不在“有没有链路”，而在“状态机和内存预算不够稳定”：

- wakeword 命中后，若 `wifi/time/session` 瞬时未就绪，当前 admission 可能直接丢失
- time-ready 语义不统一，build-time seed 与实际 session 准入契约不一致
- XiaoZhi 部分热路径仍存在不必要动态分配
- 播放缓冲预算仍不透明，CA32 heap 风险较高
- `river_voice -> river_cloud` 仍有直接依赖，层次边界还不干净

## Guardrails

- 不修改 `/root/ameba-rtos-1.2`
- 每一步只解决一个明确问题
- 每一步都更新：
  - `.codex/changes.md`
  - `.codex/verification.md`
- 每一步都单独提交
- 先保证正确性，再做大拆分
- 不为了“文件变小”制造新的回调地狱或抽象噪音

## Refactor References

当前重构的详细执行基线见：

- [doc/PROJECT_REFACTOR_EXECUTION_PLAN_ZH.md](/root/ameba-river/doc/PROJECT_REFACTOR_EXECUTION_PLAN_ZH.md)

相关背景文档：

- [doc/ARCHITECTURE_REFACTOR_BLUEPRINT_ZH.md](/root/ameba-river/doc/ARCHITECTURE_REFACTOR_BLUEPRINT_ZH.md)
- [doc/ARCHITECTURE_OPTIMIZATION_ZH.md](/root/ameba-river/doc/ARCHITECTURE_OPTIMIZATION_ZH.md)
- [doc/VOICE_INTERACTION_REFACTOR_PROPOSAL_ZH.md](/root/ameba-river/doc/VOICE_INTERACTION_REFACTOR_PROPOSAL_ZH.md)

## Execution Phases

### Phase 0: Plan Baseline

Status: completed

目标：

- 落一份基于当前代码和日志的执行型重构文档
- 把根目录 `plan.md` 改成当前真实执行路线

本阶段产出：

- `doc/PROJECT_REFACTOR_EXECUTION_PLAN_ZH.md`
- 本文件

### Phase 0.5: BC-ResNet Hotfix Replacement

Status: in progress

目标：

- 把当前基线唤醒词模型替换为 `bc_resnet_best.tflite`
- 只做最小运行时适配，不引入新的模型分支复杂度

范围：

- `components/river_voice/river_voice_kws.cc`
- `components/river_voice/river_voice_frontend.c`
- `components/river_voice/generated/river_wake_word_model_data.h`
- `doc/KWS_BC_RESNET_REPLACEMENT_PLAN_ZH.md`

成功标准：

- resolver 支持 `Add`
- 板端能识别并按模型真实输入布局填充张量
- `bc_resnet_best` 能成功通过本地 build 集成到镜像
- 板端唤醒后能继续进入 `XiaoZhi` 会话链路

### Phase 1: Correctness First

Status: next

目标：

- 修复 wakeword deferred admission 的丢事件问题
- 统一 `time-ready` 与 `utc-now` 契约

范围：

- `components/river_core/river_session_coordinator.c`
- `components/river_cloud/river_cloud_adapter.c`
- `components/river_cloud/river_cloud_xiaozhi_session.c`
- 必要时补充最小内部接口

成功标准：

- 唤醒命中后，不再因为瞬时 `wifi/time` 未就绪而直接丢失
- 日志能明确说明 deferred 的唯一原因
- `time_ready` 语义对 session admission 一致

### Phase 2: Memory Budget And Hot Path

Status: planned

目标：

- 去掉 XiaoZhi 上行按包 `malloc/free`
- 让 playback / pre-roll 预算显式化

范围：

- `components/river_cloud/river_xiaozhi_ws.c`
- `components/river_cloud/river_cloud_adapter.c`
- `components/river_voice/river_playback_service.c`

成功标准：

- 热路径不再做按包动态分配
- 播放申请字节数、模式和 fallback 来源可以直接从日志读出

### Phase 3: Boundary Cleanup

Status: planned

目标：

- 去掉 `river_voice -> river_cloud` 直接依赖
- 让 `river_voice` 只产出事件和音频数据，让 `river_core`/稳定 port 负责编排

范围：

- `components/river_voice/river_voice_vad_probe.c`
- `components/river_voice/river_voice_kws.cc`
- `components/river_voice/river_voice_segment_sink.c`
- `components/river_core/*`

成功标准：

- `river_voice` 不再直接感知 provider/session window 细节

### Phase 4: Large File Decomposition

Status: planned

目标：

- 按 façade / policy / runtime / diagnostics 拆分当前大文件

重点文件：

- `components/river_cloud/river_cloud_adapter.c`
- `components/river_cloud/river_xiaozhi_ws.c`
- `components/river_voice/river_voice_kws.cc`
- `components/river_voice/river_voice_vad_probe.c`

成功标准：

- 每个文件只保留一种主责任
- 热路径逻辑与诊断逻辑分离

### Phase 5: Size And Clean Code

Status: planned

目标：

- capability 编译裁剪
- 小工具函数收敛
- 降低重复样板代码

重点方向：

- WebRTC AECM 编入条件
- Silero VAD 模型数据编入条件
- `copy_text/copy_string` 一类重复工具函数

成功标准：

- 减少不必要镜像体积
- 减少重复样板，不增加新的抽象负担

## Immediate Next Step

下一步直接进入 `Phase 1` 的第一刀：

1. 让 wakeword deferred admission 变成可重试事件，而不是一次性尝试
2. 同步梳理 `river_cloud_now_utc_seconds()`、`river_cloud_utc_ready()`、`river_cloud_time_ready()` 语义

原因：

- 这是当前业务正确性最直接的问题
- 影响“唤醒后是否去连小智”
- 风险小于先拆大文件

## Verification Standard

文档步骤：

- 校验文档结构、阶段、验证规则是否完整

代码步骤：

- 至少本地 build 通过

涉及状态机步骤：

- 必须附带板端复现方法
- 必须说明 pass/fail 日志信号

## Exit Criteria For The Refactor Track

只有满足以下条件，当前重构主线才算基本完成：

- wake/session/time 状态机语义一致
- XiaoZhi 播放和上行热路径内存预算可解释
- `river_voice` 与 `river_cloud` 不再直接耦合
- 核心大文件完成第一轮职责拆分
- 相关验证方法都已经记录在 `.codex/verification.md`
