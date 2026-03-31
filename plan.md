# Runtime Stabilization And Refactor Plan

Date: 2026-03-31
Branch: `refactor`

## Current Objective

当前已从 `prep/kws-no-mean-model` 切出 `refactor` 分支。

这个分支的首要目标不是继续叠加新功能，而是在保住当前板端基线的前提下，对现有代码做一次可回归的整理和重构。

当前原则：

- 先整理边界，再继续加复杂度
- 先拆热点大文件，再讨论进一步抽象
- 每一刀都保持可编译、可回滚、可板端验证
- 不把“重构”做成行为变化和问题定位同时发生的混合提交

运行时问题仍然存在，但接下来会尽量通过更清晰的模块边界去承接后续修复，而不是继续把补丁堆进现有耦合点。

已经完成并验证的事项：

- `xiaozhi` follow-up 窗口关闭后，交互状态会重新回到 `wake_monitoring`
- “唤醒一次后无法再次唤醒”的问题已经修复

当前最高优先级问题：

- `KWS` 队列频繁卡在 `queue=40/40`
- 日志出现 `kws queue dropped control item: type=1`
- `river_kws` CPU 占用异常偏高，且唤醒分数长期卡在 `234 pm` 左右，无法触发阈值
- 这些现象说明当前先要修掉 gate/reset 控制项在拥塞时丢失的问题，再继续看播放与堆水位

当前优先级顺序：

1. 模块边界与大文件拆分
2. session / interaction / cloud 契约收口
3. `KWS` / playback / `xiaozhi` 热路径内聚化，减少跨层直接依赖
4. 在不改变当前行为的前提下保留已有运行时修复
5. 后续再继续收紧热路径内存预算和播放稳定性

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
- `components/river_cloud/river_cloud_adapter.c`
  - 仍然承接了过多 provider/session 状态胶水
  - 是这次整理中最适合先下刀的跨层耦合热点
- `components/river_voice/river_playback_service.c`
  - 每次 `TTS` 都重新 `AudioTrack_Create -> Init -> Start -> Destroy`
  - 播放生命周期仍需要后续整理，但这一步先不改行为

## Guardrails

- 不修改 `/root/ameba-rtos-1.2`
- 重构提交默认以“结构整理”为目标，不混入新的行为变更
- 每一步只解决一个明确问题
- 每一步都更新：
  - `.codex/changes.md`
  - `.codex/verification.md`
- 每一步都单独提交
- 不为了“看起来更抽象”牺牲板端可验证性

## Refactor Track

本分支的整理目标：

- 把 `river_core`、`river_voice`、`river_cloud` 的对外边界先钉牢
- 逐步拆掉承载过多职责的大文件
- 保持 `xiaozhi`、`KWS`、`VAD`、播放链路的现有行为不变

第一轮切片优先级：

1. `components/river_cloud/river_cloud_adapter.c`
2. `components/river_core/river_app.c`
3. `components/river_voice/river_voice_kws.cc`

每一轮切片要求：

- 先移动职责，再考虑更抽象的公共层
- 先把 provider-specific 逻辑收回 provider 文件，再精简 adapter
- 先保持日志和外部接口稳定，再讨论内部重命名

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

Status: in progress

目标：

- 继续清理 `river_voice`、`river_core`、`river_cloud` 的职责边界
- 在不破坏当前板端基线的前提下逐步拆分大文件

## Immediate Next Step

下一步先做 `refactor` 分支的第一刀：

1. 先梳理 `river_cloud_adapter.c` 当前承担的职责边界
2. 把 provider/session 相关胶水从 adapter 里拆到更小的实现单元
3. 保持 `river_cloud.h` 对外接口和当前日志语义稳定
4. 每拆一刀都保证本地构建通过，再继续下一刀

原因：

- 当前分支已经切到专门的重构轨道，目标应从“继续叠补丁”转成“先把结构理顺”
- `river_cloud_adapter.c` 是当前跨层耦合最明显的热点，最适合作为第一刀
