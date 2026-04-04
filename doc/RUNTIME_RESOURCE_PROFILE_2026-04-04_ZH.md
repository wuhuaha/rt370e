# 2026-04-04 运行方案与资源画像

Date: 2026-04-04

## 1. 范围与口径

本文整理当前基线下 Ameba River 的本地唤醒方案、实现路径，以及基于一段真实板端运行日志得到的 CPU / 内存 / 时延 / 队列占用画像。

本报告的统计口径分两类：

- 固件直出值：直接来自 `river.stats`、`kws perf`、`kws status`、`kws peak`、`vad state` 日志。
- 样本推导值：基于用户提供的 `2026-04-04 15:42:06.287` 到 `2026-04-04 15:42:20.844` 这一段日志计算得到，只代表该 14.557 秒观察窗口，不代表全天平均值。

本报告对应的核心实现文件：

- 交互态与会话窗口：
  - `components/river_core/river_interaction_state.c`
  - `components/river_cloud/river_cloud_xiaozhi_session.c`
- VAD 热路径：
  - `components/river_voice/river_voice_vad_probe.c`
- 本地 KWS：
  - `components/river_voice/river_voice_kws.cc`
  - `include/river/river_voice_kws.h`
- 运行时资源统计：
  - `components/river_common/river_runtime_stats.c`

## 2. 当前方案总览

### 2.1 运行目标

当前链路的目标不是“持续全量 KWS”，而是：

1. 云端 follow-up 会话窗口关闭后回到 `wake_monitoring`
2. 本地 VAD 只在检测到 speech 时打开 KWS gate
3. KWS 在 gate 内对增强后的单声道语音做本地唤醒
4. 命中后由 session coordinator / cloud adapter 把唤醒结果接到小智 websocket 会话

### 2.2 实际数据链

当前工程实现的本地唤醒链路是：

```text
cloud follow-up timeout
-> interaction_state: follow_up -> wake_monitoring
-> capture + fixed_dsb mono
-> silero VAD probe
-> VAD-gated KWS pre-roll/queue
-> TFLite Micro BC-ResNet FP32 KWS
-> wakeword hit
-> river.session queued
-> xiaozhi bootstrap + websocket connect
```

### 2.3 当前 KWS 关键配置

配置和实现来自 `prj.conf` 与 `river_voice_kws.cc`：

- 模型：`bc_resnet_v3_fp32_experimental`
- 运行时：`tflite_micro`
- 输入帧：`16 ms`，`256 samples`
- 特征：`40` mel bins，`98` frames
- FFT / hop：`512 / 160`
- 推理 stride：`8` 帧，即 `128 ms`
- 输入队列：`64` 帧
- pre-roll：`320 ms`，即 `20` 帧
- 阈值：`CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=1600`
  - 对应 `48 pm`
- hold：`1`
- cooldown：`1800 ms`
- Tensor arena：`688 KiB`

## 3. 样本窗口发生了什么

### 3.1 时序摘要

这一段日志描述的是一次完整的“从 follow-up 结束重新回到唤醒待机，再到再次唤醒并重连云端”的过程。

关键时序：

- `15:42:06.287`
  - 小智 conversation window 因 `followup_timeout` 关闭
  - `interaction_state` 从 `follow_up` 切回 `wake_monitoring`
- `15:42:09.960`
  - 第 1 次 speech gate 打开，未命中唤醒
- `15:42:14.952`
  - 第 2 次 speech gate 打开，未命中唤醒
- `15:42:19.307`
  - 第 3 次 speech gate 打开
- `15:42:20.321`
  - 本地 KWS 命中 `小欧管家`
- `15:42:20.323`
  - `river.session` 排队唤醒文本
- `15:42:20.577`
  - 开始连接小智 websocket

### 3.2 样本规模

- 观察窗口总长：`14.557 s`
- VAD speech 事件：`3` 次
- VAD silence 事件：`3` 次
- KWS gate open：`3` 次
- KWS gate close：`2` 次
- 可见 KWS 推理：`21` 次
  - gate=open：`8`
  - gate=closed：`13`
- 本窗口内实际唤醒命中：`1` 次

## 4. 总体资源概览

### 4.1 CPU 总览

`river.stats snapshot` 只给出了两个采样点，因此 CPU 平均值是“基于两个采样点的双核平均忙闲估算”。

| 指标 | 样本平均 | 峰值 | 说明 |
| --- | ---: | ---: | --- |
| Core0 空闲率 | `93.5%` | `94%` | 来自 `IDLE0` |
| Core0 忙碌率 | `6.5%` | `7%` | `100 - IDLE0` |
| Core1 空闲率 | `88.5%` | `90%` | 来自 `IDLE1` |
| Core1 忙碌率 | `11.5%` | `13%` | `100 - IDLE1` |
| 双核平均忙碌率 | `9.0%` | `10.0%` | `((100-IDLE0)+(100-IDLE1))/2` |
| Top 非 Idle 任务 CPU | `river_vad_probe: 10%` | `10%` | 两个采样点都相同 |

结论：

- 从系统总体 CPU 看，这段样本并没有出现“整机 CPU 打满”。
- 但从 KWS 自身推理时延看，KWS worker 已经超过 stride 预算，瓶颈是“局部单模块算不过来”，不是“全机总 CPU 不够”。

### 4.2 堆内存总览

| 指标 | 数值 |
| --- | ---: |
| 当前 free heap 样本平均 | `3,604,920 B` (`3520.4 KiB`) |
| 当前 free heap 样本峰值 | `3,606,144 B` (`3521.6 KiB`) |
| 当前 free heap 样本最低 | `3,604,416 B` (`3519.9 KiB`) |
| minimum ever free heap | `3,579,136 B` (`3495.2 KiB`) |
| KWS 初始化前 free heap | `4,880,192 B` (`4765.8 KiB`) |
| KWS 初始化后 free heap | `4,118,528 B` (`4022.0 KiB`) |
| KWS 初始化造成的 heap drop | `761,664 B` (`744.0 KiB`) |

结论：

- 当前窗口内 free heap 非常平稳，波动只有 `1,728 B`。
- 低水位主要不是由这段云端 reconnect 触发，而是更早的系统初始化和常驻模块分配决定的。

### 4.3 任务数与栈余量

| 指标 | 样本平均 | 最低值 | 说明 |
| --- | ---: | ---: | --- |
| 任务数 | `17` | `17` | 两个采样点一致 |
| `river_vad_probe` 栈余量 | `14,140 B` | `14,140 B` | 非常充裕 |
| `cap` 栈余量 | `2,332 B` | `2,332 B` | 余量较小但未耗尽 |
| `kws` 栈余量 | `7,468 B` | `7,304 B` | 余量充足 |
| `echo` 栈余量 | `0 B` | `0 B` | 无可见余量，需重点关注 |

结论：

- `vad_probe` 和 `kws` 的栈安全边界目前看是够的。
- `echo` 栈水位已经打到底，虽然这段日志里没有直接表现出崩溃，但这是当前样本里最明显的栈风险点。

## 5. 模块拆分资源画像

## 5.1 交互态 / 会话窗口模块

### 实现职责

- `river_cloud_xiaozhi_window_close()` 负责关闭 follow-up 会话窗口
- `river_interaction_state_set()` 负责把系统切回 `wake_monitoring`
- `river.session` 在命中后排队唤醒文本给云端

### 观察结果

| 指标 | 数值 | 说明 |
| --- | ---: | --- |
| follow-up 关闭到切回 `wake_monitoring` | 同一毫秒级日志 | 状态切换基本即时 |
| wakeword hit 到 `river.session queued` | `2 ms` | 本地命中到会话排队非常快 |
| wakeword hit 到 `xiaozhi connecting` | `256 ms` | 包括 bootstrap 与连接准备 |
| wakeword hit 到 `WSCLIENT TLS` | `459 ms` | websocket TLS 开始时间 |

结论：

- 交互态切换和 session 排队都不是当前瓶颈。
- 命中后到发起云连接的延迟在 `256 ms` 量级，属于可以接受的控制面开销。

## 5.2 VAD / 前端探测模块

### 实现职责

- `river_voice_vad_probe.c` 是板端实时热路径
- 负责接收前端增强后的单声道帧、做 VAD、并决定是否向 KWS 打开 speech gate

### CPU 与栈

| 指标 | 样本平均 | 峰值 | 说明 |
| --- | ---: | ---: | --- |
| `river_vad_probe` CPU | `10%` | `10%` | 两个 `river.stats` 快照一致 |
| `river_vad_probe` 栈余量 | `14,140 B` | `14,140 B` | 裕量充足 |

### 信号统计

| 指标 | 样本平均 | 峰值 | 说明 |
| --- | ---: | ---: | --- |
| speech `raw_q15` | `25,223.7` | `31,518` | 3 次 speech 样本 |
| speech `prob_q15` | `12,956.3` | `16,457` | 3 次 speech 样本 |
| silence `raw_q15` | `63.0` | `113` | 3 次 silence 样本 |
| silence `prob_q15` | `75.7` | `137` | 3 次 silence 样本 |
| `afe_peak` | `6,980.3` | `7,292` | 全部 VAD 样本 |
| `stream_fail` | `0` | `0` | 无流转失败 |

### 工程结论

- VAD 模块本身的 CPU 占用稳定，且明显低于 KWS 计算压力。
- speech/silence 之间的 `raw_q15` / `prob_q15` 分离度足够大，gate 打开逻辑正常。
- 从 `stream_ok=1802 -> 2143` 可以看出该窗口内流处理持续推进，没有中断。

## 5.3 KWS 唤醒模块

### 实现职责

- `river_voice_kws.cc` 负责：
  - pre-roll / queue
  - log-mel 特征提取
  - TFLite Micro 推理
  - gate 内阈值触发
  - cooldown / interaction-state 准入

### 当前模型与调度配置

| 项目 | 数值 |
| --- | ---: |
| 模型 | `bc_resnet_v3_fp32_experimental` |
| Tensor arena | `688 KiB` |
| feature 形状 | `98 x 40` |
| 输入帧长 | `16 ms` |
| 推理 stride | `8 帧 = 128 ms` |
| 输入队列 | `64` 帧 |
| pre-roll | `20` 帧 = `320 ms` |
| 触发阈值 | `48 pm` |
| cooldown | `1800 ms` |

### 推理时延

这里优先使用固件直出的 `kws perf` 统计值。

| 指标 | 数值 |
| --- | ---: |
| 最新 `infer_us[last]` | `178,551 us` |
| 当前窗口 `infer_us[avg]` | `178,764 us` |
| 当前窗口 `infer_us[max]` | `179,182 us` |
| `infer_us[win]` | `179,166 us` |
| slow warn / alert | `14 / 14` |
| stride 预算 | `128 ms` |
| 平均超预算 | `50.764 ms` |
| 平均超预算比例 | `39.7%` |

关键结论：

- 当前 FP32 KWS 的平均单次推理时延 `178.764 ms` 明显大于 `128 ms` stride 预算。
- 这意味着 KWS worker 在 speech gate 内是持续欠账状态，队列增长是必然现象。
- 当前样本里最核心的性能瓶颈就在这里。

### 队列与预卷积缓存

| 指标 | 样本平均 | 峰值 | 说明 |
| --- | ---: | ---: | --- |
| 观测到的 queue 深度 | `18.4 / 64` | `32 / 64` | 基于 gate/status/peak 样本 |
| 队列占用率 | `28.7%` | `50.0%` | 同上 |
| gate 打开时 queue 初值 | `14 / 64` | `14 / 64` | 三次 open 都一致 |
| pre-roll 使用 | `20 / 20` | `20 / 20` | 打开 gate 时始终打满 |
| trim 行为 | `dropped=4 keep=16/20` | 同步发生在每次 gate open | pre-roll 正常裁切 |

关键结论：

- `32 / 64` 的峰值队列深度已经到一半，说明 KWS 明显依赖队列缓冲来消化算力赤字。
- pre-roll 长度 `20` 帧被完整利用，说明 gate 设计在捕捉前导音节方面是有效的。

### 置信度与命中情况

| 指标 | 数值 |
| --- | ---: |
| 全部 21 次推理平均 score | `0.03078` |
| 去掉最终命中后的平均 score | `0.00354` |
| gate=open 平均 score | `0.07666` |
| gate=closed 平均 score | `0.00255` |
| 本窗口峰值 raw | `576` |
| 本窗口峰值 q15 | `18,861` |
| 命中分数与阈值比例 | `575 / 48 = 11.98x` |

补充观察：

- 第 1 次 gate 的 `gate_best_pm=13`，只达到阈值的 `27.1%`
- 第 2 次 gate 的 `gate_best_pm=4`，只达到阈值的 `8.3%`
- 第 3 次 gate 命中 `575 pm`，与前两次弱 speech window 拉开明显差距

### KWS 内存占用

优先使用 `kws perf` 中的模块拆解值。

#### 固件直出内存拆分

| 子项 | 数值 | 说明 |
| --- | ---: | --- |
| Tensor arena 已用 | `586,128 B` (`572.4 KiB`) | 模型运行时实际已占用 |
| Tensor arena 预留 | `688 KiB` | 总 arena 容量 |
| arena slack | `118,384 B` (`115.6 KiB`) | arena 余量 |
| KWS context | `25,472 B` (`24.9 KiB`) | `sizeof(context)` 相关 |
| pre-roll buffer | `10,240 B` (`10.0 KiB`) | 20 帧缓存 |
| input queue buffer | `33,024 B` (`32.3 KiB`) | 64 帧队列 |
| tensor dump buffer | `62,724 B` (`61.3 KiB`) | 包含当前 dump 预留 |

#### 推导后的 KWS 常驻占用

| 口径 | 数值 | 说明 |
| --- | ---: | --- |
| KWS 已用工作集 | `717,588 B` (`700.8 KiB`) | `arena_used + ctx + pre + queue + dump` |
| KWS 预留工作集 | `835,972 B` (`816.4 KiB`) | `arena_reserved + ctx + pre + queue + dump` |
| KWS 预留工作集 / 当前最低 free heap | `23.19%` | 以 `3,604,416 B` 为分母 |
| KWS 已用工作集 / 当前最低 free heap | `19.91%` | 同上 |

关键结论：

- KWS 是当前本地唤醒路径中最大的常驻内存消费者。
- 仅 KWS 一个模块的预留工作集就接近 `816 KiB`。
- 其中 `Tensor arena` 占 KWS 预留总量的 `84.27%`，绝对主导。

### 命中时序

| 指标 | 数值 |
| --- | ---: |
| 第 3 次 gate open 到 wakeword hit | `1014 ms` |
| 第 1 次 gate 持续时间 | `1664 ms` |
| 第 2 次 gate 持续时间 | `1472 ms` |

结论：

- 命中延迟约 `1.014 s`，与 `98` 帧特征窗口的时间尺度基本一致。
- KWS 当前是“接近 1 秒窗口”的唤醒响应，不是超低延迟亚 500ms 级方案。

## 5.4 云端连接模块

### 实现职责

- `river_cloud_xiaozhi_session.c` 负责 window 关闭、bootstrap、ws 建连

### 样本观察

| 指标 | 数值 | 说明 |
| --- | ---: | --- |
| 命中到 `HTTPC TLS` | `154 ms` | OTA/bootstrap 前置链路启动 |
| 命中到 `xiaozhi ota bootstrap ok` | `255 ms` | bootstrap 完成 |
| 命中到 `xiaozhi connecting` | `256 ms` | websocket 建连开始 |
| 命中到 `WSCLIENT TLS` | `459 ms` | websocket TLS 开始 |

资源侧结论：

- 该段日志没有给出云模块独立 CPU / heap 统计。
- 但从两个事实可以看出云侧在这段窗口内不是主瓶颈：
  - 它没有进入 `cpu_top` 前三
  - hit 前后的 `heap` 低水位没有继续明显下降，仍停在 `3,604,416 B`

## 6. 综合判断

## 6.1 当前优势

- 本地唤醒方案已经形成闭环：
  - `follow_up` 超时退出
  - 进入 `wake_monitoring`
  - VAD 打开 KWS gate
  - KWS 本地命中
  - `river.session` 排队
  - 小智云端重连
- VAD 模块 CPU 开销低且稳定
- 系统总体 free heap 在该窗口内非常平稳
- KWS 的 pre-roll、gate、队列和命中时序都符合当前设计预期

## 6.2 当前瓶颈

最突出的问题有两个：

1. `KWS FP32 推理时延 > stride 预算`
   - 平均 `178.764 ms`
   - 预算只有 `128 ms`
   - 导致 speech gate 内队列增长到 `32 / 64`

2. `echo` 任务栈余量为 `0 B`
   - 这不是当前窗口里的直接故障来源
   - 但它是最明显的栈安全风险点

## 6.3 当前工程结论

如果只看这一段样本，当前系统状态可以概括为：

- 整机 CPU 和 heap 还有余量
- VAD 不是瓶颈
- 云端连接不是瓶颈
- 真正的主瓶颈是 KWS FP32 模型推理吞吐不足
- 当前 KWS 内存占用在本地唤醒链路中占绝对大头

也就是说，下一步如果要继续优化体验，优先级应当是：

1. 降低 KWS 推理时延，至少压回 `128 ms` stride 预算以内
2. 复核 `echo` 任务栈空间
3. 只有在前两项稳定后，再看是否继续压缩 KWS 内存
