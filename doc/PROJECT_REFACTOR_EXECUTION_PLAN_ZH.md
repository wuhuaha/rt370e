# Ameba River 项目重构执行方案

日期：2026-03-30
分支：`DS-CNN`

## 1. 目的

这份文档不是再写一版“架构愿景”，而是把当前代码基线上的重构工作收敛成可执行、可验证、可分步提交的路线。

当前目标有且只有三条：

- 先修正运行时正确性，避免 wake、time、session 状态错位
- 再收紧堆内存和热路径分配，降低 CA32/KWS/XiaoZhi 相关不稳定性
- 最后重整层次边界和大文件职责，提升可维护性，同时兼顾镜像体积

## 2. 当前基线判断

基于当前代码和板端日志，项目已经具备完整业务主链：

- 本地 `KWS`
- 本地 `VAD`
- `Wi-Fi` 自动连接
- `XiaoZhi realtime` 会话
- `TTS` 下行播放

但当前主问题已经不是“功能有没有”，而是“状态归属和内存预算是否一致”。

本轮审视后的核心判断：

- `wakeword -> session open` 的准入流程存在丢事件风险
- time-ready 语义不统一，build-time seed 没有形成一致契约
- XiaoZhi 上行和部分播放路径仍有不必要的动态分配
- `river_voice` 与 `river_cloud` 之间存在反向直接依赖
- 多个核心文件已经同时承担 policy、runtime、diagnostic 三类职责

## 3. 重构原则

### 3.1 顺序原则

严格按以下顺序推进：

1. 正确性
2. 内存与热路径
3. 边界与职责
4. 体积与 clean code

不允许先做“大拆分”，再回头补正确性。

### 3.2 所有权原则

任何运行时状态必须能回答三个问题：

- 谁创建
- 谁更新
- 谁关闭

无法回答这三个问题的状态，视为需要重构。

### 3.3 嵌入式约束原则

所有优化同时满足：

- 不修改 `/root/ameba-rtos-1.2`
- 不引入新的长生命周期 heap 抖动
- 不为了“好看”增加额外抽象层数
- 不把热路径拆成大量低价值回调

## 4. 当前关键问题清单

### P0: 正确性 / 稳定性

#### P0.1 wakeword deferred admission 会丢失

现状：

- `river_session_coordinator` 在真正准入前就清空 pending wakeword
- 若 `river_cloud_adapter_begin_conversation_window()` 返回 busy，当前 wake 只记日志，不保留重试

影响：

- 会出现“已经识别到唤醒，但没去连小智”的现象
- `wifi`、`time_ready`、session window 的瞬时不可用会直接丢失当前 wake

目标：

- wake event 从“一次性尝试”改为“带原因的待受理事件”
- 在 `wifi connected`、`time ready`、`session available` 条件满足时重试一次

#### P0.2 time-ready 契约不一致

现状：

- system time、estimated time、build-seeded time 并存
- 对外 `utc_ready/time_ready/now_utc_seconds` 语义没有统一

影响：

- 日志显示“seed utc estimate from build time”
- 但 session admission 仍可能因为 `time_ready=no` 被拒绝

目标：

- 明确区分：
  - `system_utc`
  - `estimated_utc`
  - `admission_time_ready`
- 对外只保留一套稳定准入语义

### P1: 堆内存 / 热路径

#### P1.1 XiaoZhi 上行封包热路径动态分配

现状：

- 上行二进制发送时按包 `malloc/free`

影响：

- 增加内存碎片
- 增加发送路径抖动
- 长时间运行下稳定性变差

目标：

- 改为固定工作缓冲区或静态 scratch buffer
- 明确最大 payload 和失败策略

#### P1.2 播放缓冲预算不透明

现状：

- 播放服务使用 `driver min buffer * frame_count`
- 云侧 XiaoZhi 再额外叠加 fallback 策略

影响：

- CA32 heap 压力难以预估
- 当前日志里的 TTS 播放 malloc 失败就来自这里

目标：

- 让播放缓冲从“乘法策略”改为“显式预算”
- 日志输出最终申请字节数、来源和模式

#### P1.3 pre-roll 分配和逻辑容量不一致

现状：

- 先按请求值分配
- 再按 provider 实际上限截断逻辑容量

影响：

- 浪费堆空间
- 预算不透明

目标：

- 先求最终容量，再做一次性分配

### P2: 架构 / 可维护性

#### P2.1 `river_voice -> river_cloud` 直接依赖

现状：

- `river_voice_vad_probe`
- `river_voice_kws`
- `river_voice_segment_sink`

都直接调用 cloud 接口。

影响：

- voice layer 无法独立测试
- 本地链路与云端策略耦合
- 后续离线/在线融合会继续恶化

目标：

- 通过 `river_core` 或稳定 port 接口承接 session/bridge 决策
- `river_voice` 只产出事件和音频数据

#### P2.2 大文件职责混杂

重点文件：

- `components/river_cloud/river_cloud_adapter.c`
- `components/river_cloud/river_xiaozhi_ws.c`
- `components/river_voice/river_voice_kws.cc`
- `components/river_voice/river_voice_vad_probe.c`

目标：

- 每个文件只保留一种主责任
- runtime / policy / diagnostics 分离

### P3: 体积 / clean code

#### P3.1 未充分按 capability 裁剪

现状：

- `river_voice` 仍默认编入 WebRTC AECM 源和 Silero VAD 大模型数据

目标：

- 将模型数据和算法源的编入与 active capability 对齐

#### P3.2 重复工具函数和低价值样板

现状：

- 多处重复 `copy_text/copy_string`
- 部分 runtime log 组装逻辑重复

目标：

- 统一最小工具层
- 在不增加抽象噪音的前提下减小重复代码

## 5. 执行分期

### Phase 0: 建立重构基线

目标：

- 固化当前问题、目标、阶段顺序
- 更新根目录 `plan.md`

交付：

- 本文档
- 更新后的 `plan.md`

### Phase 1: 修正确性状态机

范围：

- wake admission retry
- time-ready 语义统一
- wake/session 失败原因日志标准化

成功标准：

- 出现 wake 命中时，不再因为瞬时 `wifi/time` 未就绪而直接丢失
- 日志能明确说明当前 deferred 的唯一原因

### Phase 2: 收紧堆内存与播放预算

范围：

- XiaoZhi 上行 scratch buffer
- playback 显式预算化
- pre-roll 最终容量先算后分配

成功标准：

- TTS 播放申请字节数可直接从日志读出
- 热路径不再做按包 `malloc/free`

### Phase 3: 拆开 voice 和 cloud 的反向耦合

范围：

- 将 `river_voice` 到 `river_cloud` 的直连改成稳定 port / coordinator 路径

成功标准：

- `river_voice` 不再直接感知 provider/session window 细节

### Phase 4: 大文件职责拆分

范围：

- `river_cloud_adapter.c`
- `river_xiaozhi_ws.c`
- `river_voice_kws.cc`
- `river_voice_vad_probe.c`

成功标准：

- façade、policy、runtime、diagnostic 四类代码分离

### Phase 5: 体积与 clean code 收尾

范围：

- capability 编译裁剪
- 公共小工具收敛
- 日志和状态导出收敛

成功标准：

- 减少不必要对象文件和模型常驻体积
- 减少重复样板代码

## 6. 每一步的提交规则

每一步都必须：

- 只解决一个明确问题
- 更新 `.codex/changes.md`
- 更新 `.codex/verification.md`
- 提交一个聚焦 commit

验证标准：

- 文档步：检查文档入口、阶段、验证方式是否完整
- 代码步：至少通过本地 build
- 涉及运行时状态机的步：必须附带板端复现实验方法

## 7. 当前推荐第一刀

如果按收益/风险比排序，接下来的第一刀不是拆文件，而是：

1. 修复 wakeword deferred admission 的可重试语义
2. 统一 time-ready 契约

原因：

- 这是当前业务正确性最直接的问题
- 改动范围可控
- 对后续 XiaoZhi 会话和调试日志的解释力最高

## 8. 非目标

当前重构阶段暂不做：

- 改 SDK
- 更换音频框架
- 更换主 KWS 算法
- 大规模重写日志框架
- 为了形式统一而引入泛型或宏模板抽象

重构的目标是收口，而不是扩张。
