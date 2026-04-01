# Runtime Performance Optimization Plan

Date: 2026-04-01
Branch: `refactor`

## Current Objective

当前已从 `prep/kws-no-mean-model` 切出 `refactor` 分支。

这个分支的首要目标不是继续叠加新功能，而是在保住当前板端基线的前提下，优先优化实时链路的性能、时延和过载行为。

当前原则：

- 先压掉实时瓶颈，再继续做结构整理
- 先减少积压和无效工作，再讨论更抽象的公共层
- 每一刀都保持可编译、可回滚、可板端验证
- 不把“重构”做成行为变化和问题定位同时发生的混合提交

运行时问题仍然存在，但当前最需要优先处理的是队列堆积、调度唤醒方式和 burst 型 backlog 对实时性的破坏。

已经完成并验证的事项：

- `xiaozhi` follow-up 窗口关闭后，交互状态会重新回到 `wake_monitoring`
- “唤醒一次后无法再次唤醒”的问题已经修复

当前最高优先级问题：

- `KWS` 队列频繁卡在 `queue=40/40`
- 在事件驱动 wakeup 和 pre-roll 限流之后，陈旧 `PCM` backlog 仍会持续挤占实时预算
- `river_kws` CPU 占用异常偏高，且唤醒分数长期卡在 `234 pm` 左右，无法触发阈值
- 这些现象说明当前要继续把 `KWS` 过载行为做得更激进，优先保住最新语音和 reset 语义，再继续看播放与堆水位

当前优先级顺序：

1. `KWS` 实时链路吞吐与队列退化策略
2. 音频热路径的调度与唤醒模型
3. `xiaozhi` 上下行与播放链路的 backlog 控制
4. 在不改变当前行为的前提下保留已有运行时修复
5. 在性能基线稳定后继续做模块边界整理

## Current Baseline

当前主链已经板端可用：

- 本地 `KWS`
- 本地 `VAD`
- `Wi-Fi` 自动连接
- `xiaozhi realtime` 会话
- `TTS` 下行播放
- follow-up 超时后重新进入可唤醒状态

当前需要继续收口的不是“功能有没有”，而是“同一条链路能否连续多轮稳定运行”。

从用户日志和当前实现看，当前最可疑的热点是：

- `components/river_voice/river_voice_kws.cc`
  - gate open 时会把 pre-roll 一次性灌入 `input_ring`
  - worker 目前仍有“队列空则轮询等待”的路径
  - 当前最需要先优化这里的 burst/backlog 行为和 worker 调度响应
- `components/river_cloud/river_cloud_adapter.c`
  - `xiaozhi` 上下行 ring 已经有 drop-oldest 行为，但后续仍要检查高水位降级是否足够激进
- `components/river_voice/river_playback_service.c`
  - 播放生命周期和 drain/flush 语义仍需要后续优化，但这一步先不改行为

## Guardrails

- 不修改 `/root/ameba-rtos-1.2`
- 当前阶段允许为实时性做行为级优化，但每一步都必须缩在单一热点内
- 每一步只解决一个明确问题
- 每一步都更新：
  - `.codex/changes.md`
  - `.codex/verification.md`
- 每一步都单独提交
- 不为了“看起来更抽象”牺牲板端可验证性

## Optimization Track

本分支的优化目标：

- 在语音主链过载时优先保住“最新数据”和“关键控制语义”
- 避免 backlog 把系统拖进高延迟、高 CPU、低可用性的恶化闭环
- 在性能基线稳定后，再继续做边界清理和大文件拆分

第一轮切片优先级：

1. `components/river_voice/river_voice_kws.cc`
2. `components/river_cloud/river_cloud_adapter.c`
3. `components/river_voice/river_playback_service.c`

每一轮切片要求：

- 先消除 burst/backlog 引起的实时性崩坏
- 先让 consumer 的唤醒与调度优先于 backlog 继续扩大
- 先保日志和外部接口稳定，再继续更深层的结构整理

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

- 优化 `KWS` 输入链路的实时性，而不只是修补控制项丢失
- 避免 `queue=40/40` 长时间钉死后把 worker 拖成高 CPU 忙转
- 让 gate open 的 pre-roll 回放不再制造瞬时洪峰

范围：

- `components/river_voice/river_voice_kws.cc`

成功标准：

- gate rearm 时 reset 语义不再依赖共享 `PCM` 队列里的控制项
- 旧 PCM backlog 会被主动清掉，而不是继续和 reset/新语音争抢队列
- gate open 后队列占用不再因为 pre-roll flush 立刻冲到接近满队列
- 当输入队列逼近高水位时，系统会主动裁掉最旧 PCM，避免长时间钉死在 `queue=40/40`
- `kws status` 不再长时间停留在 `queue=40/40` 且 `dropped` 快速增长
- 板端重新出现稳定唤醒，或至少先证明控制路径已经恢复正常

### Phase 2: Playback Stability

Status: next

目标：

- 收紧 `TTS` 播放生命周期，优先消除重复播放引起的堆下坠
- 给播放起停补上明确的堆快照，便于直接从日志判断资源是否回收
- 在网络建连前允许主动回收“空闲但可复用”的播放缓存，避免非关键缓存挤压 `xiaozhi` 的 TLS/WS 建连峰值

范围：

- `components/river_voice/river_playback_service.c`
- 必要时配套更新 `include/river/river_playback_service.h`

成功标准：

- 兼容参数下重复 `xiaozhi` TTS 优先复用同一个 `AudioTrack`
- 串口日志能看到播放启动是 `reuse=yes` 还是新建
- 多轮对话后 `heap_free` 不再像当前这样持续塌陷
- `underrun` 频率下降，或至少可和堆变化直接关联
- 当会话打开前自由堆跌破阈值时，系统会优先释放 idle playback cache，而不是让 `river_wake_evt` 在建连路径里继续撞堆失败

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

Status: in progress

目标：

- 继续清理 `river_voice`、`river_core`、`river_cloud` 的职责边界
- 在不破坏当前板端基线的前提下逐步拆分大文件

## Immediate Next Step

上一刀已经完成：

1. `KWS` worker 从轮询空转改成事件驱动唤醒
2. 提高 `KWS` consumer 相对 producer 的调度优先级
3. 把 gate open 的 pre-roll flush 从“一次灌满”改成“只补最近且有限的几帧”

下一步进入性能优化第二刀：

1. 把 `RESET` 从 `PCM` 队列里解耦，改成独立 pending 信号，让 worker 在处理新帧前优先执行 reset
2. 给 `KWS` 输入队列加高水位裁剪策略，在 backlog 逼近满队列时主动丢弃最旧 `PCM`
3. 当前队列策略以 `CONFIG_RIVER_KWS_INPUT_QUEUE_FRAMES=40` 为基线，先按 `30 -> 13` 做高水位回落
4. 用板端日志验证：
   - `kws worker` / `kws backend` 已显示 `trim=30->13`
   - 过载时出现 `kws input trim: dropped=...`
   - `kws status` 中 `trim_ops` / `trim_drop` 增长，但 `queue=40/40` 不再长时间钉死
   - 唤醒链路是否保持可用

原因：

- 当前最明确的瓶颈不是结构抽象，而是 `KWS` 热路径仍会在过载时把旧数据堆成 backlog
- 只有先把“reset 优先级”和“旧帧主动淘汰”做对，后续对 `cloud` / `playback` 的优化和结构整理才有清晰基线
