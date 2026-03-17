# 语音交互重构方案（评审稿）

## 1. 文档目的

本文档给出 `ameba-river` 当前语音交互栈的一份中期重构方案，目标不是“推倒重来”，而是在保持当前可运行主链的前提下，逐步把系统重构为：

- 更现代
- 更高效
- 更易维护
- 更易扩展
- 更适合后续接入 `KWS / AEC / DoA / 多轮对话 / 自建服务端`

本文档先作为**架构评审稿**，供后续按阶段实施。

当前稳定主链基线仍然是：

`capture -> fixed_dsb -> silero_vad -> streaming asr`

当前 AEC 方向仍视为独立实验链，不并入稳定主链。

---

## 2. 当前代码现状判断

结合当前项目实现、`Ameba SDK speechmind/aivoice` 示例、以及 `/root/esp-sr` 的 AFE 设计，可以明确看到当前工程已经形成了一个“可运行原型”，但还没有收敛成一个长期可维护的语音运行时架构。

### 2.1 当前已经做对的部分

- `capture / preproc / detector / cloud` 已有基础边界
- `fixed_dsb` 作为当前 ASR 主前端是合理的
- `Silero VAD` 独立于 SDK AFE，是正确方向
- `AEC` 被限制在实验链，而不是直接污染主链
- `playback_ref` 已被抽成了单独模块，而不是散落在算法实现里

### 2.2 当前主要问题

#### 问题 A：播放状态不是一等公民

当前“播放是否进行中”“参考信号是否可用”“是否允许打断”并没有由一个统一的播放服务负责，而是散落在：

- `river_voice_echo.c`
- `river_voice_ref.c`
- `river_voice_preproc_*`
- 实验 `AEC` 路径

结果是：

- 播放状态目前容易退化成“靠能量猜”
- 参考信号生命周期不清晰
- 后续 AEC / barge-in / TTS / 音效提示音很容易互相打架

#### 问题 B：系统状态没有统一管理

当前项目存在多个“事实上的状态源”：

- `wifi` 状态
- `cloud stream` 状态
- `vad` 状态
- `echo` 是否运行
- `playback ref` 是否打开
- `online_control` 是否可用

但没有一个统一的“交互状态管理器”来表达例如：

- `idle`
- `wake_monitoring`
- `wake_confirmed`
- `listening`
- `thinking`
- `speaking`
- `barge_in`
- `error_recovering`

这会直接导致：

- 行为判断分散在不同模块
- 后续自然多轮对话难以落地
- AEC 开关条件不可靠
- 播放和录音打断策略难统一

#### 问题 C：调试路径与产品路径混杂

当前 `vad_probe`、`echo`、`asr bridge`、实验 `AEC` 路径共用很多底层资源，但不是通过统一的 runtime graph 编排，而是通过多个模块各自打开：

- `capture`
- `preproc`
- `ref`
- `cloud bridge`

这会导致：

- 调试消费者与产品消费者争资源
- 同一能力可能被重复初始化
- 某个实验路径容易污染主路径

#### 问题 D：播放/参考/AEC 的输入模型还不够稳定

现在已经在尝试把实验 AEC 输入模型收敛为：

`mic0 + mic1 + ref`

方向是对的，但配套的运行时控制还没抽象出来：

- reference 的来源
- playback 生命周期
- reference 有效性
- playback 优先级
- playback 队列
- playback 完成/中断回调

这些如果不抽成公共服务，AEC 永远会和调试代码耦在一起。

#### 问题 E：业务层和语音层还没有彻底解耦

当前 `online_control`、`river.app`、`vad_probe`、`cloud adapter` 之间仍然偏“线性原型化接法”，这会在后续出现：

- 语音策略修改时牵动业务逻辑
- 业务策略修改时牵动音频链
- 测试边界不清

---

## 3. 重构目标

本次重构的目标应当明确限制为：

### 3.1 核心目标

- 建立**统一的语音运行时状态模型**
- 建立**独立的播放服务与参考服务**
- 建立**清晰的唤醒前 / 唤醒后双 profile 架构**
- 建立**稳定的多线程与环形缓冲协作模型**
- 建立**实验能力与稳定主链隔离机制**

### 3.2 非目标

以下内容不应在本轮重构里一口气完成：

- 一次性替换所有现有模块
- 立刻上完整 KWS
- 立刻上完整 DoA 闭环
- 立刻实现复杂对话管理
- 立刻推翻现有 `fixed_dsb + silero_vad + streaming asr`

---

## 4. 总体设计原则

### 4.1 服务化，而不是“工具函数化”

后续语音系统应重构为几个稳定服务：

- `CaptureService`
- `PlaybackService`
- `ReferenceService`
- `PreprocService`
- `DetectorService`
- `ASRService`
- `InteractionStateManager`
- `RuntimeTelemetryService`

### 4.2 主链稳定，实验链隔离

默认产品链必须始终可单独验证：

`capture -> fixed_dsb -> silero_vad -> asr`

任何 `AEC / KWS / DoA / 新 BF / 新 NS` 都必须通过：

- 独立 profile
- 独立开关
- 独立日志
- 独立资源统计

进入系统。

### 4.3 状态驱动，而不是能量猜测驱动

例如：

- “当前是否在播音”
- “是否允许 AEC 打开”
- “当前是否处于连续对话窗口”

这些都不应主要依赖音频能量推断，而应主要依赖：

- 播放服务状态
- 会话状态机
- 参考有效性状态

音频能量只能作为辅助校验，不应作为主真相源。

### 4.4 单一职责

必须避免以下反模式：

- `vad_probe` 同时承担调试、状态控制、参考管理、ASR推流
- `echo` 同时承担播放、参考、AEC验证、音频路由
- `app` 同时承担启动、状态协调、业务逻辑、日志聚合

### 4.5 采用“分层状态机”，而不是“一个巨大的总状态机”

结合 `esp-sr` 与 `speechmind/aivoice` 的开源/SDK实践，可以看到成熟语音系统普遍不是把所有状态都揉进一个大 `switch-case`，而是拆成 4 层：

1. 数据面状态
- `ring buffer` 深度
- `feed/fetch` 是否 ready
- `capture/preproc/playback` 是否运行
- underrun / overrun / queue busy

2. 算法状态
- `vad_state`
- `wakeup_state`
- `trigger_channel`
- `ref_state`
- `beam_state`

3. 模块运行状态
- `PlaybackService`
- `ReferenceService`
- `PreprocService`
- `ASRService`
- 模块 enable / disable / fallback

4. 交互/会话状态
- `idle`
- `wake_monitoring`
- `listening`
- `speaking`
- `thinking`
- `barge_in`
- `follow_up`

本项目后续也应遵循这一分层方式：

- `Pipeline` 不负责业务状态
- `Detector/KWS` 不直接改播放状态
- `Playback` 不直接决定会话状态
- `InteractionStateManager` 只消费各模块状态与事件，形成统一交互决策

### 4.6 Profile 优先于 giant if-else

成熟实现更倾向于：

- 先定义 `profile`
- 再在 `profile` 内声明算法组合
- 运行时只做少量显式状态切换

而不是在一条主链里堆越来越多的：

- `if (wake)`
- `if (aec)`
- `if (ref)`
- `if (barge_in)`
- `if (playback_running)`

因此本项目后续应尽量遵循：

- `wake_profile`
- `asr_profile`
- `barge_in_profile`
- `exp_profile`

而不是继续在 `vad_probe` 或 `preproc_fixed_dsb` 中堆条件分支。

---

## 5. 目标架构

建议的目标架构如下：

```text
                   +------------------------------+
                   |   InteractionStateManager    |
                   | idle/wake/listen/speak/...   |
                   +---------------+--------------+
                                   |
             +---------------------+----------------------+
             |                                            |
             v                                            v
+------------------------+                    +------------------------+
|   Voice Frontend RT    |                    |   Playback Runtime     |
|                        |                    |                        |
| CaptureService         |                    | PlaybackService        |
| PreprocService         |                    | Prompt/TTS queue       |
| DetectorService        |                    | Priority scheduler     |
| SessionRouter          |                    | Ref export             |
+-----------+------------+                    +------------+-----------+
            |                                              |
            v                                              v
+------------------------+                    +------------------------+
|      ASR Service       |                    |    ReferenceService    |
| stream open/feed/close |                    | ref ring / native ch3  |
| partial/final/session  |                    | validity state         |
+------------------------+                    +------------------------+
```

---

## 6. 推荐模块划分

## 6.1 `PlaybackService`

这是本轮最值得优先抽象的模块。

### 职责

- 播放音频数据
- 管理播放队列
- 管理播放优先级
- 暴露当前播放状态
- 产出 `reference stream`
- 管理播放开始/结束/中断事件

### 不应再由外部模块负责的事情

- 用能量判断“是不是正在播放”
- 外部模块自行管理 reference ring
- 每个消费者自己猜播放是否结束

### 建议接口

```c
typedef enum {
    RIVER_PLAYBACK_IDLE = 0,
    RIVER_PLAYBACK_PREPARING,
    RIVER_PLAYBACK_RUNNING,
    RIVER_PLAYBACK_DRAINING,
    RIVER_PLAYBACK_STOPPING,
    RIVER_PLAYBACK_ERROR
} river_playback_state_t;

typedef enum {
    RIVER_PLAYBACK_PRIO_TTS = 0,
    RIVER_PLAYBACK_PRIO_PROMPT,
    RIVER_PLAYBACK_PRIO_ALERT
} river_playback_priority_t;
```

建议能力：

- `enqueue()`
- `stop(category/id)`
- `flush()`
- `state()`
- `current_item()`
- `register_listener()`
- `reference_export_enable()`

### 为什么必须抽出来

因为后续以下功能都依赖这个模块：

- AEC 开关
- barge-in
- 连续对话
- 提示音/TTS 竞争
- 多种播放源统一调度

---

## 6.2 `ReferenceService`

### 推荐定位

`ReferenceService` 不应再只是一个简单 ring buffer 工具，而应成为：

- 统一管理参考信号来源
- 统一管理参考状态
- 统一暴露 reference 读取接口

### 支持两种来源

1. `native_capture_ch3`
2. `playback_ring`

### 对外暴露状态

```c
typedef enum {
    RIVER_REF_SOURCE_NONE = 0,
    RIVER_REF_SOURCE_PLAYBACK_RING,
    RIVER_REF_SOURCE_NATIVE_CAPTURE_CH3
} river_ref_source_t;

typedef enum {
    RIVER_REF_MISSING = 0,
    RIVER_REF_IDLE,
    RIVER_REF_ACTIVE,
    RIVER_REF_DEGRADED
} river_ref_state_t;
```

### 意义

这样后续：

- `AEC` 看的是 `ReferenceService` 状态
- 不是每个算法自己去判定参考是否存在

---

## 6.3 `InteractionStateManager`

这是第二个必须引入的核心模块。

### 建议统一管理的状态

```text
BOOTING
IDLE
WAKE_MONITORING
WAKE_CONFIRMED
LISTENING
ASR_STREAMING
THINKING
SPEAKING
BARGE_IN_LISTENING
ERROR_RECOVERING
```

### 状态机必须成为唯一真相源

例如：

- 是否允许开启 AEC
- 是否允许打断当前播报
- 是否处于 follow-up window
- 当前应该使用 wake profile 还是 asr profile

都应由 `InteractionStateManager` 给出，而不是由多个模块各自决定。

### 这将直接改善

- 播放/录音切换时序
- 唤醒后连续对话体验
- 打断逻辑
- AEC 触发逻辑

---

## 6.4 `PipelineStateModel`

为了避免把 `PlaybackService`、`ReferenceService`、`DetectorService`、`ASRService` 的状态混在一起，建议显式定义一组轻量但长期稳定的 pipeline 运行时状态。

### 推荐拆分

#### `AudioPipelineState`

用于表达链路本身是否 ready：

```text
CAPTURE_READY
PREPROC_READY
DETECTOR_READY
ASR_READY
PLAYBACK_READY
```

#### `AlgorithmRuntimeState`

用于表达算法模块运行态：

```text
VAD_SILENCE / VAD_SPEECH
KWS_IDLE / KWS_DETECTED
REF_MISSING / REF_IDLE / REF_ACTIVE / REF_DEGRADED
AEC_BYPASS / AEC_ACTIVE / AEC_FALLBACK
BF_FIXED / BF_WAKE_GUIDED / BF_FALLBACK
```

#### `SessionState`

用于表达交互会话：

```text
IDLE
WAKE_MONITORING
WAKE_CONFIRMED
LISTENING
ASR_STREAMING
THINKING
SPEAKING
BARGE_IN_LISTENING
FOLLOW_UP
ERROR_RECOVERING
```

### 设计原则

- 每层状态都有自己的 owner
- 上层只消费下层状态，不反向篡改
- 日志中必须区分“算法状态变化”和“交互状态变化”

这会让后续调试更像 `esp-sr` 的 `feed/fetch result + ringbuff_free_pct + wake/vad state` 模型，而不是一堆互相覆盖的临时日志。

---

## 6.5 `PreprocService`

当前 `river_voice_preproc` 的边界是对的，但职责需要更清晰。

### 长期目标

让 `PreprocService` 只负责：

- 接收统一格式输入帧
- 根据当前 profile 执行预处理链
- 输出统一格式增强帧

### 不应再承担

- 自己推断系统播放状态
- 自己决定交互状态
- 自己创建参考来源

### 建议 profile 化

#### `wake_profile`

目标：

- 高召回
- 低失真
- 低功耗

建议：

- `HPF/DC remove`
- 可选保守 `fixed_dsb`
- 关闭 AEC
- 关闭或极弱 NS
- 输出给 `VAD/KWS`

#### `asr_profile`

目标：

- 语义完整
- ASR 正确率优先

建议：

- `fixed_dsb`
- `AEC` 仅在 `PlaybackService=RUNNING && ReferenceService=ACTIVE`
- 可选轻量 NS
- 宽容型 endpointing

#### `barge_in_profile`

目标：

- 播报中允许用户打断

建议：

- `fixed_dsb + AEC`
- 允许更短响应延迟
- 结合播放状态和参考状态启用

#### `wake_guided_asr_profile`

目标：

- 唤醒后锁定目标说话方向
- 提高远场和旁人干扰下的 ASR 准确率

建议：

- 输入保持多通道原始数据
- 消费 `wake steering context`
- 在 `AEC` 之后、`ASR/VAD` 之前执行唤醒导向波束
- 对 steering 结果设置 `hold`、`update`、`release`

注意：

- `wake_guided_asr_profile` 不应与 `wake_profile` 混用
- 它应该是唤醒后 profile，而不是 always-on profile

---

## 6.6 `DetectorService`

### 推荐拆分

后续应明确拆成两个逻辑层：

1. `VAD Gate`
- 判断是否有人在说话
- 做切段和节能门控

2. `Wake Engine`
- 判断是否是设备的唤醒词

### 为什么必须拆

只用 VAD 负责唤醒，会天然遇到：

- 误唤醒高
- 召回与误触发难同时兼顾
- 对电视声/旁人聊天敏感

### 推荐形态

```text
wake path:
capture -> wake_preproc -> vad_gate -> kws

asr path:
capture -> asr_preproc -> vad/endpoint -> streaming asr
```

---

## 6.7 `SpatialContextService`

如果后续要引入：

- `DoA`
- 唤醒导向波束
- trigger-channel steering
- 多麦 spatial diagnostics

那么必须提前把“空间信息”抽象成独立上下文，而不是直接塞进 `beamformer` 实现里。

### 建议抽象

```c
typedef struct {
    bool valid;
    uint32_t timestamp_ms;
    float doa_deg;
    int trigger_channel;
    int confidence_q15;
    uint32_t hold_ms;
} river_spatial_context_t;
```

### 为什么必须独立出来

- `KWS` 可能产出 trigger channel
- `DoA` 可能产出角度
- `Beamformer` 只应消费 steering，不应自己反推交互状态
- 后续替换 `DoA` 或替换 `wake guided BF` 时，不应改动 `KWS/ASR` 主链

### 推荐 owner

- 由 `DetectorService` 或专门的 `SpatialService` 产出
- 由 `PreprocService` 的 `wake_guided_asr_profile` 消费
- 生命周期由 `InteractionStateManager` 管理

---

## 6.8 `BeamSession`

唤醒导向波束不应是一个全局常量，也不应在每帧即时重算并强切。更合理的方式是把它设计成一个短生命周期的会话态。

### 建议状态

```text
BEAM_IDLE
BEAM_ACQUIRED
BEAM_HOLDING
BEAM_UPDATING
BEAM_RELEASED
```

### 建议行为

- 唤醒词确认时 `acquire`
- 在会话初期保持 steering 稳定
- 允许有限速率更新
- 会话结束时释放
- 任意异常时回退 `fixed_dsb`

### 为什么重要

如果没有 `BeamSession`：

- DoA 轻微抖动就会导致波束方向抖动
- ASR 会出现音色和清晰度不稳定
- 后续很难做自然连续对话

---

## 6.9 `ASRService`

当前 `river_cloud_adapter` 已经承担了一部分服务职责，但还不够“状态化”。

建议明确它只负责：

- 会话打开/关闭
- 音频流发送
- partial/final 结果回调
- provider 隔离

不负责：

- 决定是否进入 listening
- 决定是否允许 follow-up
- 决定是否切换 playback

这些都应交给 `InteractionStateManager`。

---

## 7. 线程与缓冲模型建议

这里建议参考 `esp-sr` 的 `feed/fetch` 思路，但不要机械照搬。

### 7.1 推荐线程模型

```text
Task A: capture_task
  负责稳定采集，把原始 interleaved frame 推入 capture_rb

Task B: preproc_task
  从 capture_rb 取帧，按当前 profile 做 preproc，推入 preproc_rb

Task C: detector_task
  从 preproc_rb 取帧，做 VAD/KWS/endpointing/SpatialContext 生成，推事件和音频到 router

Task D: asr_uplink_task
  接收 router 输出，负责 stream open/feed/close

Task E: playback_task
  负责播放队列、AudioTrack、reference export

Task F: state_task
  负责统一状态机和 timeout/follow-up window/事件归并
```

### 7.2 为什么推荐这种模型

好处是：

- 每个任务单一职责
- 排查时更容易定位瓶颈
- 更利于资源统计
- 更适合后续替换算法模块

### 7.3 环形缓冲建议

至少显式保留以下 ring buffer：

- `capture_rb`
- `preproc_rb`
- `detector_event_q`
- `asr_tx_rb`
- `playback_rb`
- `reference_rb`（若非 native ch3）

所有 ring buffer 都应具备：

- 当前深度
- 峰值深度
- underrun
- overrun
- producer / consumer 标识

不要再让“只是一个数组 + 读写指针”的匿名缓冲散落在多个模块里。

---

## 8. 为唤醒导向波束预留的架构约束

这是本轮新增的重点。后续如果确定要引入唤醒导向波束，那么现在重构时必须预留下面这些约束，否则后面还会再做一轮大改。

### 8.1 原始多通道数据必须保留到 spatial 决策点

不要在过早阶段永久压成单声道。后续如果要做：

- `DoA`
- wake-guided beamforming
- trigger-channel steering
- `AEC -> BF -> ASR`

就必须保证：

```text
capture(multichannel) -> spatial preproc -> mono asr stream
```

### 8.2 波束 steering 不应直接依赖单帧结果

建议至少具备：

- `confidence`
- `hold_ms`
- `update_rate_limit`
- `fallback_to_fixed_dsb`

否则 steering 会抖动。

### 8.3 `AEC`、`Beamforming`、`ASR` 的顺序必须提前定义

对当前双麦中控屏场景，更合理的长期方向通常是：

```text
mic0 + mic1 + ref
-> per-channel AEC
-> wake-guided beamforming
-> optional light NS
-> vad/asr
```

不要让不同实验分支各自定义处理顺序。

### 8.4 几何与校准必须抽象成独立配置

建议保留独立配置对象：

- `ArrayGeometry`
- `MicCalibration`
- `SteeringConfig`

不要把：

- 麦间距
- 阵列方向
- steering angle
- sample delay 映射

散落在算法文件里。

### 8.5 steering 元数据必须与音频时间戳对齐

后续唤醒导向波束最大的工程风险之一，是：

- steering 已经锁对了
- 但用户第一字还是丢了

所以必须确保：

- 有 `pre-roll`
- 有 wake 触发时间戳
- 有 steering 生效时间点
- 音频与 steering 事件在同一时间基上

---

## 9. 播放状态判断应如何重构

针对你提出的关键问题，结论非常明确：

## 不应再主要基于能量判断“当前是否在播放”

### 应该以 `PlaybackService` 生命周期为主

即：

- `enqueue`
- `start`
- `running`
- `draining`
- `stop`
- `done`

这是主真相源。

### 参考能量只做辅助校验

即：

- `ref_state`
- `ref_peak`
- `ref_active_ratio`

只用于：

- 判断参考质量
- 判断 AEC 是否应该启用
- 判断 playback/ref 是否一致

### 最终建议判据

`AEC enabled` 应同时满足：

- `interaction_state in {SPEAKING, BARGE_IN_LISTENING}`
- `playback_state == RUNNING`
- `reference_state == ACTIVE`
- `reference aligned == true`

这样才是工程上稳定、专业的判据。

---

## 10. 推荐的 clean code 重构方向

### 9.1 目录层面

建议后续逐步收敛为：

```text
components/
  river_audio/
    capture/
    playback/
    reference/
    buffer/
  river_voice/
    preproc/
    detector/
    wake/
    asr_router/
  river_runtime/
    state/
    events/
    telemetry/
  river_cloud/
    asr/
    control/
```

### 9.2 命名层面

避免以下问题：

- 模块名表达实现细节而非职责
- 实验实现和稳定实现共用同名后端

建议：

- `service`：长期稳定职责边界
- `adapter`：第三方算法/接口包装
- `profile`：运行时策略组合
- `runtime`：状态与调度

### 9.3 代码组织层面

每个模块尽量形成：

- `public header`
- `private context`
- `open/process/close`
- `dump_status`
- `dump_stats`

避免跨模块直接访问对方内部状态。

---

## 11. 建议的渐进式迁移方案

## Phase 1：先抽播放服务

优先级最高。

交付：

- `PlaybackService`
- 统一播放状态
- 统一播放队列
- 统一 `reference export`

收益：

- AEC 判据立刻变可靠
- 后续 TTS/提示音/中断具备统一入口

## Phase 2：引入统一状态机

交付：

- `InteractionStateManager`
- 基础状态转换
- follow-up window
- speaking/listening/barge-in 状态

收益：

- 行为决策从“分散 if-else”变为“状态驱动”

## Phase 3：重构 preproc profile

交付：

- `wake_profile`
- `asr_profile`
- `barge_in_profile`
- `exp_profile`

收益：

- 唤醒前后策略彻底区分

## Phase 4：引入统一状态模型与事件总线

交付：

- `PipelineStateModel`
- `InteractionStateManager`
- 统一事件归并

收益：

- 日志、调试、控制逻辑都开始对齐到同一套状态模型

## Phase 5：重构 detector path

交付：

- `vad_gate`
- `kws engine` 预留边界
- endpointing 与 wake 判定分离
- `SpatialContext` 预留边界

收益：

- 交互自然度和唤醒准确率能真正同时优化

## Phase 6：为空间感知与唤醒导向波束预留架构

交付：

- `SpatialContextService`
- `BeamSession`
- `wake_guided_asr_profile`

收益：

- 后续接入 `DoA` 和 wake-guided BF 时不需要再大改主链

## Phase 7：收敛实验能力

交付：

- `AEC` 只作为 profile 内模块
- `DoA` / 新 BF / 新 NS 通过实验 profile 接入

收益：

- 主链稳定，实验高效

---

## 12. 对当前项目的明确建议

如果以“投入产出比最高”的顺序来排，我建议：

1. **先重构 PlaybackService**
2. **再建立 PipelineStateModel + InteractionStateManager**
3. **再做 wake/asr/barge_in 双或三 profile**
4. **再引入 KWS，并同时预留 SpatialContext**
5. **再收敛 AEC**
6. **最后再接入 DoA / wake-guided BF / 新 NS**

这是最稳的路线。

反过来，如果现在直接继续卷：

- AEC 调参
- VAD 参数
- 参考能量判据

而不先把 `playback/state/runtime` 抽干净，后面会不断重复返工。

---

## 13. 参考与借鉴来源

本方案主要参考了以下实践方向：

### 当前项目现状

- `river_voice_capture`
- `river_voice_preproc`
- `river_voice_ref`
- `river_voice_vad_probe`
- `river_voice_echo`
- `river_cloud_adapter`

### Espressif `esp-sr`

借鉴点：

- `feed/fetch` 分层思路
- AFE 输入格式显式建模
- ring buffer / runtime stats 暴露
- `AEC / VAD / WakeNet` 以管线模块形式组织
- 数据面状态、算法状态、交互状态分层
- 通过结果结构体输出状态，而不是把所有状态揉成一个总状态机

### Ameba SDK `speechmind/aivoice`

借鉴点：

- 原生 `mic0 + mic1 + ref` 输入模型
- 全双工语音链中的 `ref` 参与方式
- AFE/录音/播放的接口组织方式
- profile/iface 切换优先于 giant if-else
- 实验算法接管失败后回退稳定主链

---

## 14. 最终结论

### 对你提出的问题的直接回答

#### 是否应该把播放单独抽成接口？

**应该，而且优先级很高。**

它不应只负责播放音频，还应负责：

- buffer 管理
- 优先级队列
- 状态维护
- reference 导出
- 播放生命周期事件

#### 是否应该有独立状态管理类？

**必须有。**

没有统一状态机，后续：

- AEC 开关
- barge-in
- follow-up
- 播放/录音协同

都会继续分散在多个模块里。

#### 是否需要对其他部分做 clean code 重构？

**需要，但要按顺序做。**

先收敛：

- `PlaybackService`
- `PipelineStateModel`
- `InteractionStateManager`
- `Profile-based Preproc`
- `SpatialContext` 预留接口

再谈：

- KWS
- AEC
- DoA
- 唤醒导向波束
- 新 BF/NS

这是对当前项目最专业、最稳健的路线。
