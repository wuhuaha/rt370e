# 音频数据面队列与缓冲架构设计（评审稿）

## 1. 文档目的

本文档给出 `ameba-river` 当前语音交互系统在“播放 / 拾音 / 参考信号 / 打断控制”方面的一份正式架构建议，核心目标是：

- 提升实时性
- 降低运行期 `malloc/free` 频率
- 提升播放与拾音并行时的稳定性
- 为后续 `AEC / KWS / DoA / barge-in` 扩展预留清晰接口
- 为当前代码重构提供可直接落地的工程指引

本文档聚焦于**音频数据平面**与**交互控制平面**的协作，不讨论云服务协议细节，也不讨论具体算法效果优劣。

配套的分阶段落地计划见：

- `AUDIO_DATAFLOW_QUEUE_IMPLEMENTATION_PLAN_ZH.md`

---

## 2. 当前系统现状判断

结合当前工程实现，可以看到系统已经出现了几种不同类型的缓冲与排队模型：

- `components/river_voice/river_voice_capture.c`
  当前拾音链路使用 `mutex + sema + byte ring buffer`
- `components/river_cloud/river_tts_iflytek_ws.c`
  当前 TTS 链路已经使用 `network receive -> PCM queue -> feeder task -> playback`
- `components/river_voice/river_playback_service.c`
  当前播放服务仍然偏“直接写 AudioTrack”
- `components/river_voice/river_reference_service.c`
  当前参考信号服务更接近控制包装层，数据面仍然较轻

这说明工程方向是正确的，但还没有形成统一的、长期可维护的缓冲架构。

当前主要问题有四类：

1. 数据平面和控制平面边界不够清晰  
播放打断、参考信号生命周期、ASR/TTS 会话切换，仍有部分逻辑直接耦合在业务路径里。

2. 队列模型不统一  
有的地方是字节 ring，有的地方是同步调用，有的地方是云回调直接推进播放，缺少统一约束。

3. 运行期内存峰值偏大  
TTS、WebSocket、大消息接收、播放队列、参考历史等模块叠加时，堆压力明显。

4. 打断策略还不够“工程化”  
目前已经具备基础 barge-in 能力，但播放中断仍然偏依赖局部判定，缺少统一的“命令 + epoch + 队列复位”机制。

---

## 3. 核心结论

### 3.1 可以大量使用队列，但不能“所有环节都靠同一种队列”

最佳实践不是“所有链路都上 ring buffer”，而是按场景选型：

- 硬件/DMA 边界：`ping-pong`
- 单生产者单消费者线程边界：`SPSC ring`
- 多生产者控制命令：`command queue / mailbox`
- 速率不一致的链路：`bounded queue`
- 同线程连续算法链：尽量直通函数调用，不强行插队列

### 3.2 不建议用“定时器轮询队列”作为主时钟

音频系统的主时序应尽量由以下来源驱动：

- `AudioRecord_Read` / `AudioTrack_Write`
- DMA half/full callback
- semaphore / event 唤醒
- 阻塞式读取或等待

定时器只适合：

- 超时
- drain wait
- watchdog
- cooldown
- 统计采样

不适合做“每 10ms 扫一次有没有音频帧”的主驱动。

### 3.3 播放打断可以通过“修改游标 + 标记位”实现，但必须配合 `epoch/generation`

仅修改 `read_ptr / write_ptr / count` 并不总是足够，因为消费者可能已经取到了旧帧。

推荐做法是：

- `playback_epoch++`
- `stop_requested = true`
- 清空队列游标和计数
- 清空未对齐帧缓存
- feeder / consumer 每次处理前检查本地 `epoch`
- 一旦发现 `epoch` 改变，立即丢弃旧帧并退出当前流

这是“快速打断 + 不消费陈旧数据”的关键。

---

## 4. 总体设计原则

### 4.1 数据平面与控制平面严格分离

数据平面只负责：

- 音频帧搬运
- 抖动吸收
- 固定长度缓冲
- 播放/采集节拍对齐

控制平面只负责：

- 启停
- 打断
- 优先级
- 状态流转
- 资源所有权

### 4.2 运行期禁止不受控动态分配

实时路径中，原则上应避免：

- 每帧 `malloc/free`
- 每包 `calloc/free`
- 因消息大小波动反复扩缩容

推荐：

- 初始化或会话启动时一次性分配
- 使用固定大小 slot pool
- 使用预分配 ring
- 使用固定 frame cache

### 4.3 优先保证 `SPSC`，不要过早追求“全局无锁”

在 MCU/RTOS 语音系统里，绝大多数音频数据链路都天然适合单生产者单消费者。

因此：

- 优先设计成 `SPSC`
- 优先用固定长度 frame
- 优先用无锁 ring 或极轻量原子游标

不要把“多生产者通用队列”强行用于所有链路。

### 4.4 算法主链尽量直通

如下这类强实时链路，如果运行在同一个高优先级线程中，通常不应该层层再排队：

`capture -> preproc -> vad -> asr feed`

原因：

- 少一次内存拷贝
- 少一次唤醒/切换
- 少一份队列深度不确定性
- 延迟更可控

---

## 5. 推荐架构

## 5.1 数据面总图

```text
                +---------------------+
                |  Control / State    |
                |  TurnManager        |
                |  PlaybackCommandQ   |
                +----------+----------+
                           |
                           v
Mic DMA --> Ping/Pong --> CaptureTask --> VoiceFrameRing --> VoicePipelineTask
                                                          -> DSB / AEC / VAD / ASR

Cloud TTS WS --> DecodeTask --> TtsPcmRing --> PlaybackFeederTask --> AudioTrack
                                                        |
                                                        v
                                                   RefFrameRing
                                                        |
                                                        v
                                              Barge-in / AEC / Diag
```

---

## 5.2 拾音链路

推荐模型：

`DMA ping-pong -> capture task -> fixed-frame SPSC ring -> voice pipeline task`

说明：

- `ping-pong` 负责承接硬件 DMA 中断节拍
- `capture task` 只做最小搬运和异常统计
- 进入算法主链前，统一转换成固定 frame 单位

建议：

- ring 中每个 slot 对应一个完整音频 frame
- 不建议继续用“按字节长度读写”的通用 byte ring 作为长期方案
- `drop oldest` 比 `block producer` 更适合采集链路

原因：

- 采集不能长时间阻塞 DMA 上游
- 旧数据的价值低于新数据
- 固定 frame 更利于后续接入 AEC/KWS/DoA

---

## 5.3 算法主链

推荐模型：

`VoicePipelineTask` 内部直通执行：

`frame -> preproc -> detector -> segmentation/asr feed`

建议：

- 如果当前 DSB、VAD、ASR bridge 都在同一线程内，可以直接函数调用
- 不要在每个算法模块之间都插一个新队列
- 只有在以下情况才引入新队列：
  - 算法明显更慢，需要异步化
  - 输入/输出速率不同
  - 需要跨核或跨线程运行

---

## 5.4 播放链路

推荐模型：

`TTS network receive -> decode -> PCM frame ring -> playback feeder -> AudioTrack`

这里应明确两点：

1. 网络接收与硬件播放必须解耦  
网络抖动不应直接影响 `AudioTrack_Write`

2. 播放服务本身不应承担协议解析  
播放服务只消费标准 PCM frame，不关心 websocket/json/base64

建议：

- `TtsPcmRing` 使用固定 frame slot，而不是无限制字节流
- `PlaybackFeederTask` 只负责：
  - 从 ring 取 PCM
  - 对齐到播放 frame
  - 写 `AudioTrack`
  - 同步写 `RefFrameRing`

---

## 5.5 参考信号链路

推荐模型：

`PlaybackFeederTask -> RefFrameRing -> AEC / barge-in / diag`

建议：

- 参考信号必须与“实际喂给播放设备的数据”严格对齐
- 不建议由上层业务代码单独拼接一份“猜测的 ref”
- 参考信号 ring 建议独立于播放 ring
- 播放停止时，参考 ring 必须同步复位

原因：

- AEC 关心的是“真实输出”
- barge-in 也更适合参考真实输出能量，而不是业务估计值

---

## 6. 队列模型选型建议

## 6.1 `ping-pong`

适用场景：

- DMA 驱动的采集
- DMA 驱动的播放
- 中断到线程的最小数据交接

优点：

- 最低调度复杂度
- 最稳定的硬件节拍
- 非常适合中断上下文

缺点：

- 只能吸收极小抖动
- 不适合网络/云端这种突发性输入

结论：

`ping-pong` 适合作为硬件入口，不适合作为完整系统唯一缓冲层。

## 6.2 `SPSC lock-free ring`

适用场景：

- `capture task -> voice pipeline`
- `tts decode -> playback feeder`
- `playback feeder -> ref consumer`

优点：

- 延迟小
- 上下文切换少
- 不需要重锁
- 非常适合固定帧音频流

缺点：

- 只适合单生产者单消费者
- 对多消费者场景不友好

结论：

这是音频数据平面的首选模型。

## 6.3 `command queue / mailbox`

适用场景：

- 播放请求
- 打断请求
- 状态切换
- 优先级变更

优点：

- 语义清晰
- 易做优先级
- 易做状态回调

缺点：

- 不适合搬大量 PCM 数据

结论：

控制面必须使用命令队列或等价模型，而不是复用 PCM 队列做“控制”。

---

## 7. 打断播放的最佳实践

## 7.1 推荐流程

当系统判定需要打断当前播放时：

1. `playback_epoch++`
2. `stop_requested = true`
3. 清空 `PCM queue`
4. 清空 `pending mono/stereo frame`
5. 清空 `reference queue`
6. `PlaybackFeederTask` 检查到 `epoch` 改变后立即退出当前流
7. 调用 `AudioTrack_Stop / Pause / Destroy`
8. 状态机切换到 `barge_in_listening` 或 `wake_monitoring`

## 7.2 为什么必须加 `epoch`

如果只清空队列游标，不加 `epoch`，会有三种风险：

- 消费者已经把旧帧拷到本地栈/缓存
- 旧帧已经进入待写播放缓冲
- 中断后新流和旧流帧混在一起

`epoch` 的作用是：

- 给每一段播放流一个唯一代号
- 让任何旧帧在消费者侧都失效

---

## 8. 定时器在系统中的正确角色

定时器不应承担“音频帧搬运主循环”职责。

推荐只用于：

- TTS/ASR session timeout
- feeder drain wait
- VAD/barge-in cooldown
- runtime telemetry 限频
- watchdog

不推荐用于：

- 每 10ms 扫描一次 ring 是否有数据
- 定时触发播放写入
- 定时拉取 capture 帧

原因：

- 轮询会带来空转
- 轮询会带来节拍抖动
- 轮询会降低实时性和能效

---

## 9. 对当前工程的直接建议

## 9.1 拾音链路

`components/river_voice/river_voice_capture.c`

建议从当前 `byte ring + mutex`，逐步升级为：

- 固定 frame slot
- `SPSC ring`
- 仅在启动/关闭时分配释放
- 运行期不动态扩容

## 9.2 播放链路

`components/river_cloud/river_tts_iflytek_ws.c`

当前已经具备 `queue + feeder` 雏形，建议继续收敛为：

- 固定 frame ring
- 解码线程只入队
- 播放线程只写 `AudioTrack`
- 不在 websocket 回调中做重活

## 9.3 参考信号链路

`components/river_voice/river_reference_service.c`

建议把当前偏控制层的包装，进一步拆成：

- `ReferenceControl`
- `ReferenceFrameRing`

其中数据面使用固定 frame ring，控制面保留轻量锁。

## 9.4 打断机制

建议把打断从“若干模块各自 stop”收敛成统一接口：

- `PlaybackCommandQueue`
- `PlaybackEpoch`
- `TurnManager`

即：

- 谁判定需要打断不重要
- 真正执行打断只走一条公共链

---

## 10. 资源池建议

为了进一步降低运行期波动，推荐引入以下资源池：

### 10.1 `VoiceFramePool`

用途：

- capture frame
- preproc frame
- ref frame

建议：

- 固定 frame 大小
- 固定 slot 数量
- 启动时一次性分配

### 10.2 `PlaybackFramePool`

用途：

- TTS PCM frame
- 提示音 PCM frame

建议：

- 与播放 frame 对齐
- 与 feeder 一一对应

### 10.3 `CommandPool`

用途：

- start / stop / duck / interrupt / flush 命令对象

说明：

- 控制对象很小
- 即便使用普通 queue 也可接受
- 不必为了“全无锁”过度复杂化

---

## 11. 后续扩展兼容性

该架构对后续能力扩展是友好的：

- `AEC`
  可以消费 `capture frame + ref frame`
- `KWS`
  可以在 `VoicePipelineTask` 内或其前级独立接入
- `DoA`
  可以在多通道 capture frame 上独立运行
- `Barge-in`
  可以通过播放状态、参考帧和近端特征统一决策
- `TTS/提示音/系统音`
  都可统一走 `PlaybackCommandQueue + PlaybackService`

---

## 12. 最终建议

本项目后续重构，应遵循以下统一原则：

1. 硬件边界用 `ping-pong`
2. 数据线程边界用 `SPSC ring`
3. 控制动作用 `command queue`
4. 打断通过 `epoch + queue reset + stop command`
5. 定时器只做超时与限频，不做数据主驱动
6. 运行期尽量零碎片化、零高频动态分配

一句话概括：

`音频数据靠固定帧队列流动，交互行为靠命令与状态机驱动，播放打断靠 epoch 和统一 stop 链收敛。`

这套设计最适合当前 `ameba-river` 的目标：在保留实时性和可维护性的前提下，逐步演进到更完整的全双工语音交互系统。
