# 2026-04-09 项目状态全景快照

## 1. 文档目的

本文用于回答截至 `2026-04-09`，`/root/ameba-river` 当前到底处于什么状态：

- 当前主分支、主目标、主基线是什么
- 端侧整条语音链路分别走到哪一步
- 哪些功能已经真实跑通，哪些只是调试能力
- 当前已经试过哪些模型，它们各自的板端价值如何
- 目前遇到的主要问题是什么，问题位于哪一层
- 后续最应该优先做什么，不应该再重复踩什么坑

本文不是单一模型的性能报告，也不是单一问题的 postmortem，而是一份项目级状态快照。

---

## 2. 当前项目结论摘要

先给结论：

- 当前项目已经不是“只能编译”的阶段，而是已经具备完整的板端语音主链：
  - `boot -> wifi -> sntp -> local wakeword -> cloud ASR -> LLM -> TTS playback`
- 当前最重要的工程资产不是某一个模型，而是已经建立并保留的：
  - 板端 / 本机精确对拍机制
  - 板端对拍命令集与 host replay 工具
  - latest-SDK 与 dirty-SDK 的可比较运行基线
- 当前最稳的 KWS 调试基线仍然是：
  - `student_bc_resnet_tiny_v2_fp32_debug`
  - 原因不是它实时性好，而是它已经同时满足：
    - 部署链路正确
    - 板端 / 本机对拍可复现
    - latest-SDK 上也已验证可运行
- 当前最主要的问题已经不是“模型能不能接上板”，而是三类运行质量问题：
  1. `student` 系列大多不满足实时性预算
  2. `INT8` 当前没有兑现理论性能收益
  3. XiaoZhi 云上行存在 `send_queue_busy / backpressure`
- 当前项目已经具备继续往前推进的条件，但推进方式必须是：
  - 保留既有对拍基线
  - 小步验证
  - 不把云链路问题误判成模型部署问题

---

## 3. 当前仓库与环境基线

### 3.1 当前开发分支

- 当前分支：`kws`

### 3.2 当前仓库角色

当前仓库是外部 Ameba RTOS 项目仓，主要承载：

- `RTL8730E` 端侧应用
- 本地语音前端
- 本地 KWS 运行时
- 云端会话适配与对接
- 板端调试命令与对拍工具
- 项目级文档与流程记录

### 3.3 当前实际在用的 SDK 基线

当前需要区分两套 SDK 视角：

1. dirty-SDK 工作基线
   - 路径：`/root/ameba-rtos-1.2`
   - 特点：
     - 带本地补丁
     - 历史上承担了大量 correctness bring-up
     - 其中包含当前量化 correctness 依赖的 patch

2. latest-SDK 复核基线
   - 路径：`/root/ameba-rtos`
   - 特点：
     - 用户提供的最新 SDK checkout
     - 已同步当前项目所需的关键 patch 子集
     - 已完成最新 student FP32 控制固件 build / flash / parity / live runtime 验证

### 3.4 当前 latest-SDK 结论

截至目前，`/root/ameba-rtos` 上的 latest-SDK 已经确认：

- 能成功构建当前 student FP32 调试固件
- 端侧能够正常启动并输出应用日志
- 板端 / 本机对拍成立
- 唤醒后云链路可以真实跑通

因此，latest-SDK 当前不再只是“理论可迁移目标”，而是：

- 已被验证为可运行的现实基线

---

## 4. 当前整体链路状态

## 4.1 当前板端主链

当前本地到云端的主链已经形成如下结构：

```text
capture(AMIC1 + AMIC3, 16k, 16ms, 2ch)
  -> fixed_dsb
  -> silero_vad
  -> local_kws
  -> wake handoff
  -> xiaozhi realtime websocket session
  -> cloud ASR / LLM / TTS
  -> playback service
```

### 4.1.1 采集与前处理

当前采集和前处理基线：

- 板型：`EV8730EA2/EV730EA2`
- 双麦阵列：`AMIC1 + AMIC3`
- 阵列间距：`50mm`
- 采样率：`16kHz`
- 采集帧长：`16ms`
- 采集声道：`2ch`
- 前处理：`fixed_dsb`

这一层已经不是实验性 stub，而是当前项目真实使用的端侧前端。

### 4.1.2 VAD

当前 VAD 为：

- `Silero VAD`
- `16kHz mono`
- feed 粒度：`256 samples`
- 对应前端输入帧长：`16ms`

这一层当前已经稳定接入，并且是本地 wake stage 与 cloud stream gate 的基础。

### 4.1.3 KWS

当前 KWS 运行时已经具备：

- TFLite Micro 模型加载
- frontend 特征提取
- 本地推理
- 板端张量 dump
- host replay
- local-only 调试模式
- `river kws align run` 编译样本回放

这意味着当前 KWS 已经不是“只看分数”的黑盒，而是有完整可验证工具链的系统模块。

### 4.1.4 云端链路

当前云端 XiaoZhi 实时链路已经具备：

- OTA bootstrap
- websocket connect
- session hello
- realtime ASR audio uplink
- text / emotion / TTS downlink
- 播放服务接管扬声器输出

从最近 latest-SDK 实机日志看，已经实际观察到：

- `wakeword hit`
- `Connected to websocket server`
- `server hello`
- `asr session started`
- partial / final STT
- LLM 日志
- `tts sentence_start`
- `playback start`
- `playback stop`

所以这一层当前结论是：

- 功能已真实跑通
- 但实时质量仍有问题

---

## 5. 当前已经实现的能力

## 5.1 板端应用能力

当前项目已经实现并验证过的板端能力包括：

- 正常 boot
- Wi-Fi 自动连接
- SNTP 同步
- 本地 wake monitoring
- 本地 KWS 唤醒触发
- 云端实时会话建立
- TTS 播放
- follow-up window / interaction state 管理
- playback service
- playback reference service 基础框架
- online control stub

## 5.2 调试与可观测性能力

当前项目调试能力已经较完整，重点包括：

- `river status`
- `river kws debug local on/off/status`
- `river kws align status`
- `river kws align run`
- `river audio probe start/stop`
- KWS runtime/perf/status 日志
- 板端张量 dump 日志
- host replay 工具
- 运行时 heap / stack / queue / infer_us / score 日志

当前最关键的工程价值，不是某一个命令本身，而是这些命令形成了可重复的诊断闭环。

---

## 6. 当前最重要的工程资产

## 6.1 板端 / 本机对拍机制已建立并必须保留

这是当前项目里最核心的工程资产之一。

当前已经建立的调试闭环是：

```text
board runtime
  -> tensor dump
  -> host replay same tflite
  -> compare feature hash / input hash / output raw / q15 / score
```

这条机制已经被用于：

- `student_bc_resnet_tiny_v2_fp32_debug`
- `student_bc_resnet_tiny_v2_int8_debug`
- `student_bc_resnet_nano_v2_fp32_debug`
- `student_dscnn_tiny_v2_fp32_debug`
- `student_dscnn_small_v2_fp32_debug`
- latest-SDK student FP32 复核

这条机制的意义是：

- 它能把“模型本身问题”和“部署适配问题”分开
- 它能把“端侧结果异常”定位到输入、特征、量化、输出哪一层
- 它能让后续模型调试不再靠猜

当前项目已经明确形成的协作约束是：

- 后续所有唤醒词模型调试，都必须保留这套板端 / 本机对拍机制
- 不能因为切模型或换 SDK，把这套机制丢掉

### 6.1.1 当前对拍文档

对拍机制已有独立文档：

- [KWS_BOARD_HOST_PARITY_DEBUG_GUIDE_ZH.md](./KWS_BOARD_HOST_PARITY_DEBUG_GUIDE_ZH.md)

## 6.2 串口调试策略已经收敛

当前串口调试已经形成较明确的工程结论：

- 不应随意改串口调试方式
- 原始 `cat /dev/ttyUSB0` 在部分阶段可用，但不稳定
- 进行交互式命令调试时，官方 `monitor.py` 更可靠

近期 latest-SDK 复核也再次证明：

- 使用官方 monitor 可以稳定得到 prompt 和命令响应
- 原始被动抓取方式更适合只读 boot log

### 6.2.1 当前串口策略结论

- 读 boot / 长日志：可以继续用只读抓取
- 做命令交互 / align / status：优先官方 monitor
- 不要为了图省事而重新发明一套串口交互协议

---

## 7. 当前模型验证矩阵

下表汇总当前已经完成板端 bring-up / 对拍 / 资源测量的主要模型：

| 模型变体 | 部署正确性 | 端侧对拍 | 典型 `infer_us` | 典型 arena / 资源观感 | 当前结论 |
| --- | --- | --- | ---: | --- | --- |
| `student_bc_resnet_tiny_v2_fp32_debug` | 已确认 | 已确认 | `~675 ms` | arena 约 `4.7 MB` | 当前最重要的 parity/debug 基线，不适合实时量产 |
| `student_bc_resnet_tiny_v2_int8_debug` | 已确认 | 已确认 | `~2.34 s` | correctness 成立，但性能极差 | 当前不值得作为量产路径 |
| `student_bc_resnet_nano_v2_fp32_debug` | 已确认 | 已确认 | `~278 ms` | 明显轻于 tiny FP32 | 可作为 bring-up 参考，但仍不实时 |
| `student_dscnn_tiny_v2_fp32_debug` | 已确认 | 已确认 | `~184 ms` | 当前已测 FP32 中最轻 | 目前最值得继续关注的 FP32 bring-up 候选 |
| `student_dscnn_small_v2_fp32_debug` | 已确认 | 已确认 | `~389 ms` | 比 tiny 明显更重 | 不值得继续深挖 |

### 7.1 当前最关键的模型结论

#### 7.1.1 `student_bc_resnet_tiny_v2_fp32_debug`

当前这条线的重要性不是实时性，而是：

- 部署正确
- 对拍稳定
- latest-SDK 上也已经复核成功
- 适合作为后续“端侧接线没问题”的控制组

因此它当前的项目角色应定义为：

- `correctness / parity baseline`

而不是：

- `realtime candidate`

#### 7.1.2 `student_bc_resnet_tiny_v2_int8_debug`

当前 INT8 已经证明：

- 接线不是错的
- 量化不是没生效
- 对拍也能做通

但同时也证明：

- 在当前 runtime/kernel 条件下，它没有速度收益
- 甚至显著劣于当前 FP32 debug 基线

因此它当前的项目角色应定义为：

- `quantized correctness probe`

而不是：

- `board performance solution`

#### 7.1.3 `student_bc_resnet_nano_v2_fp32_debug`

这条线证明：

- 结构变轻后，板端时延确实会下降
- 但当前下降幅度仍不足以进入实时可用区间

它的意义是：

- 证明“继续瘦身模型”是有价值方向

#### 7.1.4 `student_dscnn_tiny_v2_fp32_debug`

这条线当前的项目价值较高：

- 板端已接通
- 对拍已通过
- 时延明显优于 BC-ResNet tiny / nano

虽然它仍然高于预算，但它是目前最接近“可继续试板”的 FP32 候选之一。

---

## 8. 当前 latest-SDK 状态

## 8.1 latest-SDK 已从“怀疑对象”变成“已验证基线”

latest-SDK 路线曾一度出现：

- flash 后无正常 boot log
- 串口仅出现异常状态或无响应

但经过重新梳理后，当前更准确的结论是：

- earlier failure 与 board/session state 有明显耦合
- 在电源恢复、USB 重新 attach、使用官方 monitor 后
- latest-SDK student FP32 固件已经验证：
  - 可 build
  - 可 flash
  - 可正常运行
  - 可对拍
  - 可完成 wake -> cloud -> TTS

所以当前 latest-SDK 的状态应定义为：

- `已具备进一步迭代价值`

而不是：

- `仍不可用`

## 8.2 latest-SDK 当前已经验证的内容

已验证：

- student FP32 build 正常
- board/host parity 正常
- `river kws align run` 正常
- real wakeword 正常
- websocket / ASR / TTS / playback 正常

未完成或未系统覆盖：

- 所有模型变体在 latest-SDK 上的全面复测
- latest-SDK 下 INT8 correctness / performance 的重新系统验证
- latest-SDK 下长时间 soak / 稳定性测试

---

## 9. 当前主要问题

## 9.1 问题一：student FP32 普遍不满足实时性预算

这是目前最核心的问题之一。

当前现象：

- `student_bc_resnet_tiny_v2_fp32_debug` 约 `675 ms`
- `student_bc_resnet_nano_v2_fp32_debug` 约 `278 ms`
- `student_dscnn_tiny_v2_fp32_debug` 约 `184 ms`
- 即使较好的 tiny DS-CNN，也仍高于当前实际预算

因此当前项目不能再把“FP32 已接通”误读成“FP32 已可交付”。

### 9.1.1 当前 FP32 的项目定位

当前 FP32 的价值主要在：

- 部署正确性验证
- 板端 / 本机对拍
- 前端契约冻结
- SDK/runtime 迁移复核

而不是：

- 直接作为当前产品实时主模型

## 9.2 问题二：INT8 当前没有兑现性能收益

这是第二个关键问题。

当前工程结论已经明确：

- 当前 INT8 correctness 可以成立
- 但性能路径没有兑现量化收益

原因不在模型接错，而在：

- 当前 CA32 上 `conv` / `depthwise_conv` 的 INT8 优化路径不可依赖
- 实际运行大面积落在 reference kernel
- student 图本身又偏重

### 9.2.1 当前 INT16 状态

当前 `INT16` 更不能作为当前可交付路线，原因包括：

- KWS app 当前未放行 INT16 tensor I/O
- host replay 也未支持 INT16
- 即使底层部分算子存在 INT16 路径，也没有形成可用端到端链路

## 9.3 问题三：XiaoZhi 上行存在 backpressure

当前云端链路虽然已经跑通，但存在持续质量问题：

- `xiaozhi ws backpressure`
- `xiaozhi uplink backpressure`
- `last_err=send_queue_busy`

当前已确认这不是 KWS 部署错误，而是：

- 云端实时上行队列的 freshness-first 限流机制被频繁触发

已确认的原因包括：

- websocket queue max 为 `8`
- audio reserve 为 `2`
- 因此 `ready >= 6` 即进入 backpressure
- uplink busy 后会退避并丢旧帧
- `16ms -> 20ms` packetization 再叠加 `256ms pre-roll`，在会话刚打开时形成明显突发

这意味着：

- 当前云端链路是“功能可用”
- 但“实时音频上行质量仍待优化”

### 9.3.1 当前 backpressure 文档

已有独立分析文档：

- [XIAOZHI_UPLINK_BACKPRESSURE_ANALYSIS_LATEST_SDK_FP32_ZH.md](./XIAOZHI_UPLINK_BACKPRESSURE_ANALYSIS_LATEST_SDK_FP32_ZH.md)

## 9.4 问题四：本地 `16ms` 与云上行 `20ms` 存在节拍不一致

当前本地前端和云上行之间的帧长不一致：

- 本地采集 / Silero / KWS：`16ms`
- XiaoZhi uplink Opus：`20ms`

当前结论是：

- 这确实会带来累计、分包和启动突发问题
- 但它不是当前 student 端侧实时性差的主因
- 也不是当前云链路可用性的唯一瓶颈

更重要的是：

- 当前 Opus 封装只支持 `10/20/40/60ms`
- 当前本地 `16ms` 则已经深度绑定 Silero 与 KWS 输入契约

所以这不是一个“改一个宏即可统一”的问题，而是跨前端 / VAD / KWS / cloud uplink 的契约重构问题。

---

## 10. 当前明确的约束与边界

## 10.1 不能随意破坏串口调试与对拍基线

当前已经形成明确协作约束：

- 不要乱改串口调试方式
- 不要为了局部实验破坏现有对拍链路
- 任何新模型 bring-up 都应保留板端 / 本机比对能力

## 10.2 dirty-SDK 仍然承载 correctness 价值

虽然 latest-SDK 已能运行，但 dirty-SDK 仍有现实价值：

- 当前部分 quantized correctness 仍依赖 dirty-SDK 中的本地 patch
- 一些 clean/latest 结论仍需要与 dirty 基线对照

因此短期内不能简单删掉 dirty-SDK 路线。

## 10.3 latest-SDK 迁移不能脱离板端复核

当前 latest-SDK 的正确推进方式不是：

- “看起来源码新，就默认量化问题已经解决”

而是：

- build
- flash
- boot
- parity
- live runtime

只要这五项没做全，就不能把“可能已修复”当成工程事实。

---

## 11. 当前最值得复用的文档

如果新同事需要快速进入当前项目状态，最值得先读的是：

- [PROJECT_STATUS_ZH.md](./PROJECT_STATUS_ZH.md)
- [KWS_BOARD_HOST_PARITY_DEBUG_GUIDE_ZH.md](./KWS_BOARD_HOST_PARITY_DEBUG_GUIDE_ZH.md)
- [RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_FP32_DEBUG_ZH.md](./RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_FP32_DEBUG_ZH.md)
- [RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_NANO_FP32_DEBUG_ZH.md](./RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_NANO_FP32_DEBUG_ZH.md)
- [RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_DSCNN_TINY_FP32_DEBUG_ZH.md](./RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_DSCNN_TINY_FP32_DEBUG_ZH.md)
- [RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_DSCNN_SMALL_FP32_DEBUG_ZH.md](./RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_DSCNN_SMALL_FP32_DEBUG_ZH.md)
- [KWS_STUDENT_FP32_REALTIME_ANALYSIS_ZH.md](./KWS_STUDENT_FP32_REALTIME_ANALYSIS_ZH.md)
- [KWS_INT8_INT16_DEPLOYMENT_CONSTRAINTS_ZH.md](./KWS_INT8_INT16_DEPLOYMENT_CONSTRAINTS_ZH.md)
- [KWS_LATEST_ADK_QUANTIZATION_RECHECK_ZH.md](./KWS_LATEST_ADK_QUANTIZATION_RECHECK_ZH.md)
- [XIAOZHI_UPLINK_BACKPRESSURE_ANALYSIS_LATEST_SDK_FP32_ZH.md](./XIAOZHI_UPLINK_BACKPRESSURE_ANALYSIS_LATEST_SDK_FP32_ZH.md)

---

## 12. 当前推荐的后续优先级

截至当前，我建议按以下优先级推进：

1. 继续保留并使用 `student_bc_resnet_tiny_v2_fp32_debug` 作为 correctness / parity 控制组。
2. 在 latest-SDK 基线上继续复核真正值得追的模型，不要再回到“只在 dirty-SDK 上成立”的状态。
3. 优先推进运行质量问题定位，而不是盲目再切更多模型：
   - XiaoZhi uplink backpressure 观测与削峰
   - latest-SDK 下长一点的稳定性验证
4. 若要继续试模型，优先级建议：
   - `student_dscnn_tiny_v2_fp32_debug`
   - 其次才是别的更轻量候选
5. 若要继续量化线，前提必须明确：
   - 不把“量化理论更快”当成现网事实
   - 每次都必须先过板端 / 本机对拍
6. 如果后续要解决 `16ms / 20ms` 节拍差异，应该作为一次独立的前端契约改造项目来做，而不是临时小改。

---

## 13. 当前状态的一句话定义

截至 `2026-04-09`，`ameba-river` 已经具备：

- 可运行的端侧唤醒与云端交互主链
- 可复用的板端 / 本机对拍机制
- 多个模型变体的板端资源与实时性结论
- latest-SDK 上可运行的 student FP32 基线

当前真正卡住项目继续向前的，不再是“能不能部署”，而是：

- 如何在不破坏 correctness 基线的前提下，继续把实时性、量化可用性和云链路质量逐步拉到可交付水平。
