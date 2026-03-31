# Playback Stability Plan

Date: 2026-03-31
Branch: `DS-CNN`

## Current Objective

当前分支的首要目标不再是继续做唤醒模型迁移，而是先把现有板端链路跑稳。

已经完成并验证的事项：

- `xiaozhi` follow-up 窗口关闭后，交互状态会重新回到 `wake_monitoring`
- “唤醒一次后无法再次唤醒”的问题已经修复

当前最高优先级问题：

- 多轮 `xiaozhi` 会话后，`heap_free` 从约 `79KB` 快速跌到约 `8KB`
- `TTS` 播放仍会出现 `underrun`
- 现象更像播放路径资源生命周期不稳，而不是 `KWS` 再次失效

当前优先级顺序：

1. 播放资源稳定性与堆水位
2. `xiaozhi` 热路径内存预算收紧
3. session / time-ready 语义收口
4. 模块边界与大文件拆分

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

### Phase 1: Playback Stability

Status: in progress

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

### Phase 2: XiaoZhi Hot Path Memory Budget

Status: next

目标：

- 继续检查 `xiaozhi` 上下行热路径是否还有不必要的动态分配或积压

重点文件：

- `components/river_cloud/river_cloud_adapter.c`
- `components/river_cloud/river_xiaozhi_ws.c`

### Phase 3: Session Contract Cleanup

Status: planned

目标：

- 收口 `time_ready` / `utc_ready` / session admission 契约
- 避免把“短时条件未就绪”和“真实失败”混在一起

### Phase 4: Boundary Cleanup

Status: planned

目标：

- 继续清理 `river_voice`、`river_core`、`river_cloud` 的职责边界
- 在不破坏当前板端基线的前提下逐步拆分大文件

## Immediate Next Step

下一步先完成 `Phase 1`：

1. 在播放服务里复用兼容 `AudioTrack`
2. 给播放 start / stop 加堆快照
3. 用多轮 `xiaozhi` 会话日志验证：
   - `reuse=yes` 是否出现
   - `heap_free` 是否回稳
   - `underrun` 是否改善

原因：

- 当前用户日志里最突出的回归不是唤醒，而是播放后的内存塌陷
- 这个方向改动范围小，最适合先做板端闭环
