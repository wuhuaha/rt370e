# 音频数据面队列与缓冲架构实施清单（分阶段）

## 1. 文档目的

本文档是 [AUDIO_DATAFLOW_QUEUE_ARCHITECTURE_ZH.md](/root/ameba-river/AUDIO_DATAFLOW_QUEUE_ARCHITECTURE_ZH.md) 的配套落地计划，目标是将其中的架构建议拆解为可执行、可验收、可逐步回滚的实施阶段。

本文档强调以下原则：

- 先稳主链，再做深度重构
- 先消除高风险耦合，再追求极致性能
- 每一阶段都必须保持板端可运行
- 每一阶段都必须有明确验收标准

---

## 2. 当前基线

当前代码基线具备以下条件：

- 主前端链路可运行  
  `capture -> fixed_dsb -> silero_vad -> streaming asr`

- TTS 已经具备基础异步播放能力  
  `ws -> decode -> pcm queue -> feeder -> playback`

- 播放期间已经可以进入 `barge_in_listening`

- 当前仍存在以下工程问题：
  - 队列模型不统一
  - 播放打断尚未完全收敛到统一控制面
  - 数据面仍有部分 `mutex + byte ring` 方案
  - 局部运行期内存峰值仍偏高
  - playback / ref / asr / tts 的边界还不够彻底

---

## 3. 总体实施顺序

建议严格按以下顺序推进：

1. 第一阶段：主链稳定化
2. 第二阶段：播放控制面收敛
3. 第三阶段：数据面统一成固定帧队列
4. 第四阶段：资源池与无锁化优化
5. 第五阶段：为 AEC / KWS / DoA 扩展预留正式接口

原因：

- 第一阶段不动大结构，优先把当前风险点压住
- 第二阶段把“播放与打断”从业务路径中抽出来
- 第三阶段再统一 frame/ring 模型，降低后续重构成本
- 第四阶段再做性能与碎片优化，避免一开始过度复杂
- 第五阶段最后补扩展接口，防止为未来能力提前过度设计

---

## 4. 第一阶段：主链稳定化

## 4.1 目标

在不推翻当前链路的前提下，先把最容易影响稳定性的点收敛：

- TTS 内存峰值
- WebSocket 大包处理
- 播放中 ASR/barge-in 基础打通
- 高频动态分配和高频 stats 干扰

## 4.2 实施项

1. 收敛 TTS 会话峰值内存
- 压缩 TTS PCM 软件队列容量
- 避免 websocket 单帧大消息二次拷贝
- 保持 decode buffer 会话级复用

2. 保持 TTS 网络与播放解耦
- websocket 回调只负责解析与入队
- feeder 线程只负责播放写入

3. 保持播放期间 ASR 可启动
- 播放中允许进入 `barge_in_listening`
- 播放中一旦成功开出 ASR session，可直接触发中断策略

4. 限制无意义高频资源日志
- `vad_speech / vad_silence` 的资源快照限频
- 保留关键状态点统计

## 4.3 验收标准

- 播放长 TTS 时，不再出现大块 `malloc failed`
- 播放期间，VAD/ASR 可以真实启动
- “调试”触发的 TTS 能完整播完或被正确打断
- 运行期堆最低水位相对当前有明显回升

## 4.4 风险

- 过度压缩缓冲会导致 `underrun`
- barge-in 触发过早会使 TTS 被误打断

## 4.5 回滚策略

- 保留当前 feeder 架构
- 若压缩队列后出现回归，只回退容量参数，不回退线程解耦

---

## 5. 第二阶段：播放控制面收敛

## 5.1 目标

将播放相关行为从“散落调用”收敛为统一控制链：

- start
- stop
- interrupt
- flush
- duck
- complete

## 5.2 实施项

1. 增加 `PlaybackCommandQueue`
- 命令类型至少包括：
  - `START_STREAM`
  - `STOP_STREAM`
  - `INTERRUPT_STREAM`
  - `FLUSH_STREAM`
  - `DUCK_STREAM`

2. 引入 `PlaybackEpoch`
- 每次新播放流创建时自增
- 打断时推进 epoch
- feeder 写入前检查 epoch

3. 统一播放状态回调
- 播放服务只对外暴露稳定状态：
  - `idle`
  - `preparing`
  - `running`
  - `draining`
  - `stopping`
  - `error`

4. 统一 reference 生命周期
- playback start -> open ref
- playback stop/interrupt -> reset/close ref

## 5.3 验收标准

- 所有 TTS/提示音/系统音都通过同一播放控制面启动
- 中断播放时，不再依赖若干模块各自 stop
- 打断后不会继续消费旧 PCM 数据
- reference 生命周期与 playback 生命周期严格一致

## 5.4 风险

- 状态机切换点过多，容易引入状态抖动
- 若 epoch 设计不完整，可能出现“旧帧漏播”

## 5.5 回滚策略

- 允许保留现有 `playback_service_write` 数据平面
- 仅回滚控制命令分发，不回滚已验证有效的打断判定

---

## 6. 第三阶段：数据面统一为固定帧模型

## 6.1 目标

把当前不统一的 byte ring / 临时缓存 / 隐式帧对齐，统一到“固定 frame slot”的数据面模型上。

## 6.2 实施项

1. 重构 capture ring
- 将 [river_voice_capture.c](/root/ameba-river/components/river_voice/river_voice_capture.c) 从 byte ring 改为 fixed-frame ring
- 每个 slot 对应一个完整 capture frame

2. 重构 TTS PCM queue
- 将当前 PCM byte queue 改为 frame slot queue
- feeder 按 frame 消费，不再自己承担大部分对齐逻辑

3. 重构 ref queue
- 将参考信号按固定 frame 推送
- barge-in / AEC / diag 统一按 frame 读取

4. 算法主链保持直通
- 不在 `preproc -> vad -> asr feed` 之间额外增加多余队列

## 6.3 验收标准

- capture / tts / ref 三条主要数据链路都以固定 frame 为基本单位
- 数据流向清晰，不再依赖“字节数凑满再拼 frame”
- 日志和统计中可以直接看到 frame 级深度和丢帧情况

## 6.4 风险

- 需要调整现有读写接口签名
- 某些旧代码假设“任意 byte 数可读写”，会被破坏

## 6.5 回滚策略

- 每条链路独立改造
- 允许先改 TTS，再改 capture，再改 ref
- 不要求一口气统一所有模块

---

## 7. 第四阶段：资源池与无锁化优化

## 7.1 目标

在数据面模型稳定后，再做性能优化，避免“边重构边调性能”导致复杂度失控。

## 7.2 实施项

1. 引入 `VoiceFramePool`
- capture frame
- ref frame
- pipeline 临时 frame

2. 引入 `PlaybackFramePool`
- tts/playback frame
- prompt/audio cue frame

3. 将适合的 SPSC ring 改为无锁
- capture task -> pipeline
- decode task -> feeder
- feeder -> ref consumer

4. 清理实时路径剩余高频堆分配
- 每帧 `malloc/free`
- 每包 `zmalloc/free`
- 临时 JSON/字符串拷贝

## 7.3 验收标准

- 实时主链无高频堆分配
- 系统长时间运行时，heap 最低水位波动更可控
- 同一场景下 CPU 和延迟指标不劣于第三阶段

## 7.4 风险

- 无锁队列实现错误会比带锁队列更难调试
- 资源池大小配置不合理会放大极端场景丢帧

## 7.5 回滚策略

- 先保留“有锁 fixed-frame ring”版本
- 无锁实现必须可通过宏或 profile 回退

---

## 8. 第五阶段：扩展接口预留

## 8.1 目标

在主链稳定、控制面清晰、数据面统一后，再对后续能力做正式接口预留：

- AEC
- KWS
- DoA
- wake-guided beamforming
- 多轮对话

## 8.2 实施项

1. 为 `AEC` 定义正式输入接口
- `mic frame`
- `ref frame`
- `aec output frame`

2. 为 `KWS` 定义前置 profile
- 唤醒前 profile
- 唤醒后 profile

3. 为 `DoA / wake-guided BF` 预留旁路输入
- 多通道 raw frame
- trigger 时刻快照
- beam steering 控制入口

4. 为 `TurnManager` 补完整策略
- listen
- think
- speak
- barge-in
- follow-up

## 8.3 验收标准

- 新算法可通过 profile/开关独立接入
- 不污染当前稳定主链
- playback/ref/capture 不需要因为每种新算法再重写一遍

## 8.4 风险

- 过早抽象容易引入“看起来优雅、实际上没人用”的接口

## 8.5 回滚策略

- 接口先最小化
- 不为暂时没有落地的能力提前建大而全框架

---

## 9. 推荐里程碑划分

建议按以下里程碑推进：

### M1：稳定可用

范围：

- 第一阶段完成

结果：

- 播放/拾音/ASR/TTS 主链稳定
- 基础 barge-in 可验证

### M2：播放收敛

范围：

- 第二阶段完成

结果：

- 打断播放行为统一
- 播放控制面明确

### M3：数据面统一

范围：

- 第三阶段完成

结果：

- capture / tts / ref 使用统一 fixed-frame 模型

### M4：性能优化

范围：

- 第四阶段完成

结果：

- 主链低碎片、低抖动、低分配频率

### M5：扩展准备完成

范围：

- 第五阶段完成

结果：

- AEC / KWS / DoA / wake-guided BF 可按统一架构接入

---

## 10. 实施时的工程纪律

每阶段实施都建议遵守以下约束：

1. 每阶段只解决一类问题  
不要在“改 capture ring”时顺手把所有状态机也推翻。

2. 每阶段必须保留板端验证路径  
至少保留：
- 本地日志
- `river status`
- 一条可复现的 TTS 测试路径
- 一条可复现的 ASR 测试路径

3. 每阶段必须补充资源统计  
至少跟踪：
- heap free / heap min
- 任务数
- 栈水位
- queue peak / overflow / underrun / overrun

4. 每阶段都要能回退  
最好通过：
- profile
- compile option
- feature flag

---

## 11. 最终建议

最推荐的推进节奏是：

1. 先完成第一阶段和第二阶段
2. 在“播放控制面稳定”之后，再动第三阶段的数据面统一
3. 在 fixed-frame 模型稳定后，再做第四阶段的无锁与资源池优化
4. 最后再补第五阶段的算法扩展接口

一句话概括：

`先稳链路，再统一控制，再统一帧模型，最后做性能极致化和算法扩展。`

这条路径最适合当前 `ameba-river`：既不会因为一次性大改而失控，也能持续把系统推向更现代、更高效、更可维护的方向。
