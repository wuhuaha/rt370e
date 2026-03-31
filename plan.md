# Playback Stability Plan

Date: 2026-03-31
Branch: `DS-CNN`

## Current Objective

当前分支的首要目标仍然是把现有板端链路跑稳，但最新日志表明当前最先要收口的已经不是播放，而是 `KWS` worker 输入队列本身。

已经完成并验证的事项：

- `xiaozhi` follow-up 窗口关闭后，交互状态会重新回到 `wake_monitoring`
- “唤醒一次后无法再次唤醒”的问题已经修复

当前最高优先级问题：

- `KWS` 队列频繁卡在 `queue=40/40`
- 日志出现 `kws queue dropped control item: type=1`
- `river_kws` CPU 占用异常偏高，且唤醒分数长期卡在 `234 pm` 左右，无法触发阈值
- 这些现象说明当前先要修掉 gate/reset 控制项在拥塞时丢失的问题，再继续看播放与堆水位

当前优先级顺序：

1. `KWS` 队列完整性与 gate rearm 可靠性
2. 播放资源稳定性与堆水位
3. `xiaozhi` 热路径内存预算收紧
4. session / time-ready 语义收口
5. 模块边界与大文件拆分

## Current Baseline

当前主链已经板端可用：

- 本地 `KWS`
- 本地 `VAD`
- `Wi-Fi` 自动连接
- `xiaozhi realtime` 会话
- `TTS` 下行播放
- follow-up 超时后重新进入可唤醒状态

当前需要继续收口的不是“功能有没有”，而是“同一条链路能否连续多轮稳定运行”。

从用户日志看，当前最可疑的热点是：

- `components/river_voice/river_voice_kws.cc`
  - `input_ring` 原来以 `SPSC` 模式初始化，但生产者拥塞路径里也会主动 `read` 旧项做淘汰
  - 这会破坏 ring 的使用契约，并直接解释为什么 `RESET` 控制项会在满队列时被挤掉
- `components/river_voice/river_playback_service.c`
  - 每次 `TTS` 都重新 `AudioTrack_Create -> Init -> Start -> Destroy`
- `components/river_cloud/river_cloud_adapter.c`
  - `xiaozhi` TTS 在多轮对话下持续走该播放路径

## Guardrails

- 不修改 `/root/ameba-rtos-1.2`
- 每一步只解决一个明确问题
- 每一步都更新：
  - `.codex/changes.md`
  - `.codex/verification.md`
- 每一步都单独提交
- 不为了“看起来更抽象”牺牲板端可验证性

## Execution Phases

### Phase 0: Wake Rearm Hotfix

Status: completed

目标：

- 修复 `xiaozhi` follow-up 窗口关闭后没有重新进入 `wake_monitoring` 的状态同步缺口

结果：

- 已从实际串口日志确认二次唤醒恢复正常

### Phase 1: KWS Queue Integrity

Status: in progress

目标：

- 修复 `KWS` 输入队列在 gate 开关和 reset 重置时的控制项丢失
- 避免 `queue=40/40` 长时间钉死后把 worker 拖成高 CPU 忙转

范围：

- `components/river_voice/river_voice_kws.cc`

成功标准：

- 不再出现 `kws queue dropped control item: type=1`
- gate rearm 时旧 PCM backlog 会被主动清掉，而不是把新的 `RESET` 控制项挤掉
- `kws status` 不再长时间停留在 `queue=40/40` 且 `dropped` 快速增长
- 板端重新出现稳定唤醒，或至少先证明控制路径已经恢复正常

### Phase 2: Playback Stability

Status: next

目标：

- 收紧 `TTS` 播放生命周期，优先消除重复播放引起的堆下坠
- 给播放起停补上明确的堆快照，便于直接从日志判断资源是否回收

范围：

- `components/river_voice/river_playback_service.c`
- 必要时配套更新 `include/river/river_playback_service.h`

成功标准：

- 兼容参数下重复 `xiaozhi` TTS 优先复用同一个 `AudioTrack`
- 串口日志能看到播放启动是 `reuse=yes` 还是新建
- 多轮对话后 `heap_free` 不再像当前这样持续塌陷
- `underrun` 频率下降，或至少可和堆变化直接关联

### Phase 3: XiaoZhi Hot Path Memory Budget

Status: next

目标：

- 继续检查 `xiaozhi` 上下行热路径是否还有不必要的动态分配或积压

重点文件：

- `components/river_cloud/river_cloud_adapter.c`
- `components/river_cloud/river_xiaozhi_ws.c`

### Phase 4: Session Contract Cleanup

Status: planned

目标：

- 收口 `time_ready` / `utc_ready` / session admission 契约
- 避免把“短时条件未就绪”和“真实失败”混在一起

### Phase 5: Boundary Cleanup

Status: planned

目标：

- 继续清理 `river_voice`、`river_core`、`river_cloud` 的职责边界
- 在不破坏当前板端基线的前提下逐步拆分大文件

## Immediate Next Step

下一步先完成 `Phase 1` 的板端闭环：

1. 用 `LOCKED` ring 替代原来的错误 `SPSC` 用法
2. gate reset 前清掉陈旧 PCM backlog，确保新的 `RESET` 控制项一定能入队
3. 满队列时优先保留控制项，不再让 PCM 淘汰掉 `RESET`
4. 用板端日志验证：
   - `kws queue dropped control item: type=1` 是否消失
   - `kws gate rearm cleared stale queue: ...` 是否出现
   - `queue=40/40` 是否不再长期钉死
   - 唤醒命中是否恢复

原因：

- 最新用户日志已经把优先级重新排清楚：当前最硬的阻塞是 `KWS` 控制路径被满队列破坏
- 先把 `KWS` worker 队列契约修正，后续播放与堆问题的日志才有分析价值
