# SDK 官方 3ch Ref 路径 vs river playback_ring Ref 路径

> 备注：`debug/webrtc-aec` 实验分支当前已经把 AEC 输入模型收敛为原生
> `capture(3ch) = mic0 + mic1 + ref`。
> 本文仍保留 `playback_ring ref` 路径对比，作为历史方案和架构取舍记录。

## 1. 文档目的

本文用于说明两条不同的 AEC 参考信号路径：

- SDK 官方 `3ch ref` 路径
- 当前 `river` 项目的 `playback_ring ref` 路径

目标是让后续 AEC 方案设计、实验验证和架构决策有统一参考。

## 2. 结论摘要

两条路径的核心区别不在于“AEC 算法本身”，而在于“参考信号从哪里来、以什么形式进入前端”。

- SDK 官方路径：
  - 参考信号是录音输入帧中的第 3 路
  - 输入形式天然是 `mic0 + mic1 + ref`
  - 更接近官方 AFE/AEC 的原生设计
- 当前 river 路径：
  - 参考信号来自播放链软件 tap
  - 先进入 `playback_ring`
  - 再在前处理阶段与双麦数据逻辑拼接
  - 集成灵活，但时序、延迟和 tap 点需要自己管理

如果目标是尽可能复现 SDK 官方 AEC 输入模型，最终更推荐往“原生 3 路 ref”方向演进。

## 3. SDK 官方 3ch ref 路径

### 3.1 关键事实

SDK `speechmind` 示例明确说明：

- 录音器输出 3 路音频
- `ch1/ch2` 是双麦
- `ch3` 是 `AEC reference`

参考：

- [speechmind README](/root/ameba-rtos-1.2/component/application/speechmind/README.md#L9)
- [speechmind_demo README](/root/ameba-rtos-1.2/component/aivoice/examples/speechmind_demo/README.md#L9)

同时，AIVoice 的 `afe_config` 中也明确规定：

- `ref_num` 只能是 `0` 或 `1`
- `ref_num=0` 时，AEC 禁用

参考：

- [aivoice_afe_config.h](/root/ameba-rtos-1.2/component/aivoice/include/aivoice_afe_config.h#L72)

### 3.2 数据流图

```text
Speaker Playback
      |
      | 设备内部参考路径
      v
Audio Codec / Audio Capture
      |
      | 3ch frame = [mic0, mic1, ref]
      v
SpeechMind / AIVoice Input
      |
      +--> 内部 AFE/AEC 直接消费 ref
      |
      +--> 或外部 AECM 接管后消费 ref
      v
后续 KWS / VAD / ASR
```

### 3.3 更直观的模块边界图

```text
+--------------------------------------------------------------+
|                       Ameba 音频系统                         |
|                                                              |
|  +------------------+         +---------------------------+  |
|  | Speaker Playback |-------->| Reference Injection Path  |  |
|  +------------------+         +---------------------------+  |
|                                      |                       |
|                                      v                       |
|  +--------------------------------------------------------+  |
|  | Audio Recorder Output Frame                            |  |
|  |                                                        |  |
|  |   ch0 = mic0    ch1 = mic1    ch2 = AEC ref           |  |
|  +--------------------------------------------------------+  |
+--------------------------------------------------------------+
                               |
                               v
+--------------------------------------------------------------+
|                         AIVoice / AFE                        |
|                                                              |
|   输入: [mic0, mic1, ref]                                    |
|   配置: ref_num = 1                                          |
|                                                              |
|   AEC / NS / AGC / SSL / KWS / VAD / ASR                    |
+--------------------------------------------------------------+
```

### 3.4 SDK 示例中的参与方式

在 `speechmind_demo` 中：

- 输入通道数固定为 `3`
- 一帧数据从 `g_mic_ring_buffer` 读出来后就是三路输入
- 如果不启用外部 AECM，就原样喂给 AIVoice
- 如果启用外部 AECM，就先消费 `ref`，再把处理后的双麦数据送给 AIVoice

参考：

- [voice_service.c](/root/ameba-rtos-1.2/component/aivoice/examples/speechmind_demo/platform/ameba_dsp/voice_service.c#L31)
- [voice_service.c](/root/ameba-rtos-1.2/component/aivoice/examples/speechmind_demo/platform/ameba_dsp/voice_service.c#L354)
- [voice_service.c](/root/ameba-rtos-1.2/component/aivoice/examples/speechmind_demo/platform/ameba_dsp/voice_service.c#L451)

### 3.5 按时间顺序看一帧数据

```text
时刻 T:
  设备正在播音
  用户同时说话

录音侧拿到一帧:
  [mic0(t), mic1(t), ref(t)]

其中:
  mic0(t) = 用户语音 + 扬声器回声 + 环境噪声
  mic1(t) = 用户语音 + 扬声器回声 + 环境噪声
  ref(t)  = 扬声器播放参考

送入 AFE 后:
  AEC 使用 ref(t) 去估计并抵消 mic0/mic1 中的回声分量
  后续 BF/NS/AGC/VAD/ASR 再消费 AEC 后的结果
```

### 3.6 特点

- 优点
  - 输入模型标准，天然符合 AEC 设计习惯
  - `mic` 与 `ref` 同帧出现，通道关系清晰
  - 更容易复用官方 AFE/AEC 假设
- 缺点
  - 依赖底层音频采集链支持 `3ch`
  - 与具体 SoC 音频驱动/codec 配置强耦合
  - 在当前项目上接入成本更高

## 4. 当前 river playback_ring ref 路径

### 4.1 关键事实

当前 `river` 主工程的参考信号不是采集链天然第三路，而是软件维护的一个播放参考环形缓冲区：

- backend 名称：`playback_ring`
- source：`post-delay mono speaker feed`

参考：

- [river_voice_ref.c](/root/ameba-river/components/river_voice/river_voice_ref.c#L198)

### 4.2 数据流图

```text
Mic Capture (2ch)
      |
      | [mic0, mic1]
      v
Preproc Input -------------------------------+
                                             |
Speaker Playback Path                        |
      |                                      |
      | 取一份 post-delay mono PCM           |
      v                                      |
playback_ring (river_voice_ref)              |
      |                                      |
      | read 1ch ref                         |
      +-------------------------------> Preproc
                                             |
                                             | 逻辑拼接为 [mic0, mic1, ref]
                                             v
                                  WebRTC AECM Experiment
                                             |
                                             v
                                   后续 VAD / ASR
```

### 4.3 更直观的模块边界图

```text
+--------------------------------------------------------------+
|                         river 项目                           |
|                                                              |
|  +------------------+                                        |
|  | Mic Capture      |-----> [mic0, mic1] ----------------+   |
|  | (2ch only)       |                                    |   |
|  +------------------+                                    |   |
|                                                          v   |
|                                                +-------------------------+
|  +------------------+                          | Preproc                  |
|  | Playback PCM     |-----> mono tap --------->| 逻辑拼接为 [mic0,mic1,ref]|
|  | (software path)  |                          | 再送 WebRTC AECM        |
|  +------------------+                          +-------------------------+
|           |                                              |
|           v                                              v
|  +------------------+                          +-------------------------+
|  | playback_ring    |<-------------------------| ref read / push         |
|  +------------------+                          +-------------------------+
+--------------------------------------------------------------+
                                                       |
                                                       v
                                              后续 VAD / ASR
```

### 4.4 按时间顺序看一帧数据

```text
时刻 T:
  麦克风拿到:
    [mic0(t), mic1(t)]

时刻 T 或 T+Δ:
  播放链软件 tap 到一帧 mono:
    ref'(t)

然后前处理里逻辑拼接:
  [mic0(t), mic1(t), ref'(t)]

再送给实验 AECM:
  如果 ref 状态有效且输出 ready:
    使用 AECM 输出
  否则:
    回退到 DSB-only
```

这里的关键差异在于：

- `mic0/mic1` 来自采集链
- `ref'` 来自播放软件链
- 两者不是天然同一硬件采样帧，需要软件自己保证时序合理

### 4.5 当前 river 中的实现位置

#### 参考 ring 的开关与读写

- 打开 ring：
  - [river_voice_ref_open](/root/ameba-river/components/river_voice/river_voice_ref.c#L61)
- 写入参考：
  - [river_voice_ref_push](/root/ameba-river/components/river_voice/river_voice_ref.c#L138)
- 读取参考：
  - [river_voice_ref_read](/root/ameba-river/components/river_voice/river_voice_ref.c#L165)

#### 在 `river_voice_echo` 中的参考路径

- 打开参考：
  - [river_voice_echo.c](/root/ameba-river/components/river_voice/river_voice_echo.c#L510)
- 每帧读取参考：
  - [river_voice_echo.c](/root/ameba-river/components/river_voice/river_voice_echo.c#L633)
- 播放前把 mono 数据 push 回 ring：
  - [river_voice_echo.c](/root/ameba-river/components/river_voice/river_voice_echo.c#L693)

#### 在 `river_voice_vad_probe` 中的参考路径

- 打开参考：
  - [river_voice_vad_probe.c](/root/ameba-river/components/river_voice/river_voice_vad_probe.c#L468)
- 每帧读取参考：
  - [river_voice_vad_probe.c](/root/ameba-river/components/river_voice/river_voice_vad_probe.c#L564)

#### 在实验前处理中如何消费参考

实验 profile `fixed_dsb_webrtc_aecm` 会：

- 把 `reference_enabled` 打开
- 声明参考是 `1ch mono`
- 将 `mic0 + mic1 + ref` 拼成 3 路逻辑帧
- 推给 `webrtc_aecm_adapter`

参考：

- [river_voice_preproc_fixed_dsb.c](/root/ameba-river/components/river_voice/river_voice_preproc_fixed_dsb.c#L86)
- [river_voice_preproc_fixed_dsb.c](/root/ameba-river/components/river_voice/river_voice_preproc_fixed_dsb.c#L207)

### 4.6 特点

- 优点
  - 不依赖底层录音直接提供第三路参考
  - 更适合在现有双麦链上渐进式接入
  - 容易做实验 profile，不污染默认主链
- 缺点
  - `ref` 与 `mic` 不是天然同采集帧
  - 需要自己处理延迟、对齐和 tap 点一致性
  - 如果 playback tap 点选错，AEC 效果容易失真
  - 没有播放内容时，天然没有 ref

## 5. 两条路径的核心差异对比

| 项目 | SDK 官方 3ch ref | 当前 river playback_ring ref |
|---|---|---|
| 参考来源 | 采集链第 3 路 | 播放链软件 tap |
| 输入形式 | 天然 `[mic0,mic1,ref]` | 运行时逻辑拼接 `[mic0,mic1,ref]` |
| 与麦克风同步性 | 更强 | 依赖软件对齐 |
| 接入复杂度 | 较高 | 较低 |
| 实验灵活性 | 中 | 高 |
| 贴近官方原生方案 | 高 | 中 |
| 对播放链依赖 | 间接 | 直接 |
| 无播放时可用性 | 依硬件/驱动设计 | 不可用 |

## 6. 一眼看懂版

### 6.1 SDK 官方路径

```text
播放参考 ----+
            |
双麦采集 ----+----> 录音器直接输出 3ch ----> AFE/AEC
```

特点：
- 参考信号“生来就在输入帧里”
- 更像原生 AEC 设计

### 6.2 当前 river 路径

```text
双麦采集 ------------------------------+
                                      |
播放器软件 PCM ----> playback_ring ---+----> 前处理逻辑拼接 ----> AECM
```

特点：
- 参考信号是“后补进去的”
- 更灵活，但更依赖软件对齐

## 7. 哪条更适合当前阶段

### 当前阶段

更适合继续使用 `playback_ring ref`：

- 现有主链已经是 `2ch mic`
- 可以最小改动地验证：
  - `reference-active` 判定
  - `10ms/16ms` 对齐
  - AECM 是否值得继续投

### 后续若要追求更高上限

更建议探索“原生 3 路 ref”：

- 更接近 SDK 官方输入模型
- 有利于长期稳定的 AEC/AFE 方案
- 更容易与官方 AFE 参数、资源和假设保持一致

## 8. 建议的演进路线

### 路线 A：继续当前 playback_ring 实验

适合快速验证：

1. 先把 `playback_ring` 路径调通
2. 验证 `barge-in / double-talk / far-end only`
3. 观察：
   - 对齐稳定性
   - 回退路径稳定性
   - ASR 增益是否真实存在

### 路线 B：探索 SDK 风格 3ch ref 接入

适合后续长期方案：

1. 确认 AmebaSmart 音频框架是否能导出 `3ch capture`
2. 确认第 3 路 ref 是否可从 codec / capture API 直接获得
3. 如可行，将 AEC 输入模型收敛为：
   - `mic0 + mic1 + ref`
4. 让外部 AEC 或后续自研 AEC 都统一消费该输入模型

## 9. 当前建议

当前最务实的建议是：

- 短期：
  - 继续用 `playback_ring ref` 做 AEC 实验验证
  - 把 `reference-active`、对齐、回退和资源统计做好
- 中期：
  - 评估是否值得研究 SDK 原生 `3ch ref`
- 长期：
  - 若 AEC 成为主能力，优先考虑统一到“原生 3 路参考输入模型”

## 10. AEC on/off 时序图

下面用当前 `river playback_ring ref + experimental AECM` 路径举例。

### 10.1 无播放时

```text
时间轴 -->

Mic:        [ mic frame ][ mic frame ][ mic frame ][ mic frame ]
Ref:        [   empty   ][   empty   ][   empty   ][   empty   ]
Ref state:  [ missing   ][ missing   ][ missing   ][ missing   ]
AEC mode:   [ bypass    ][ bypass    ][ bypass    ][ bypass    ]
Output:     [   DSB     ][   DSB     ][   DSB     ][   DSB     ]
```

结论：
- 没有播放参考时，不应尝试 AEC
- 应稳定旁路到 `DSB-only`

### 10.2 刚开始播放，但参考还未稳定

```text
时间轴 -->

Mic:        [ mic frame ][ mic frame ][ mic frame ][ mic frame ][ mic frame ]
Ref:        [  low/0    ][   small   ][  rising   ][  active   ][  active   ]
Ref state:  [ missing   ][   idle    ][   idle    ][  active   ][  active   ]
AEC mode:   [ bypass    ][ bypass    ][ bypass    ][  enable   ][  enable   ]
Output:     [   DSB     ][   DSB     ][   DSB     ][ AEC+DSB   ][ AEC+DSB   ]
```

结论：
- 不能“检测到一点参考就立刻开 AEC”
- 需要稳定窗口和滞回
- 只有 `ref_state=active` 且输出 ready 时才切 AEC

### 10.3 播放结束后

```text
时间轴 -->

Mic:        [ mic frame ][ mic frame ][ mic frame ][ mic frame ][ mic frame ]
Ref:        [  active   ][  active   ][  drop     ][  low/0    ][  empty    ]
Ref state:  [  active   ][  active   ][  active   ][   idle    ][ missing   ]
AEC mode:   [  enable   ][  enable   ][  enable   ][ bypass    ][ bypass    ]
Output:     [ AEC+DSB   ][ AEC+DSB   ][ AEC+DSB   ][   DSB     ][   DSB     ]
```

结论：
- 播放结束后也不应立刻硬切
- 应允许短暂 hangover，再退回 `DSB-only`

## 11. 参考状态机与 fallback 图

### 11.1 推荐参考状态机

```text
                    +------------------+
                    |     missing      |
                    | 无 ref / 读不到  |
                    +------------------+
                       |          ^
       检测到参考但不稳定 |          | 连续读空 / push失败 / reset
                       v          |
                    +------------------+
                    |       idle       |
                    | 有 ref 但未活跃  |
                    +------------------+
                       |          ^
   连续 stable_frames  |          | hangover 用尽 / 峰值回落
   超过 enter_peak     v          |
                    +------------------+
                    |      active      |
                    | ref 可信且活跃   |
                    +------------------+
```

对应策略：

- `missing`
  - AEC 关闭
  - 输出走 `DSB-only`
- `idle`
  - AEC 仍关闭
  - 继续观察参考峰值和活跃比例
- `active`
  - 允许 AEC 生效
  - 但仍要满足 `adapter output ready`

### 11.2 当前实验 profile 的运行判断图

```text
每帧进入 preproc
      |
      v
是否有 reference buffer 且字节数正确？
      |-- 否 --> fallback to DSB
      |
      v
push 到 AECM adapter 是否成功？
      |-- 否 --> reset adapter -> fallback to DSB
      |
      v
ref_state 是否为 active？
      |-- 否 --> fallback to DSB
      |
      v
AECM output 是否 ready？
      |-- 否 --> fallback to DSB
      |
      v
pop AEC 输出是否成功？
      |-- 否 --> fallback to DSB
      |
      v
使用 AEC 输出，再做后续下游处理
```

### 11.3 fallback 的意义

`fallback` 不是异常处理补丁，而是实验链必须具备的主设计原则：

- AEC 条件不满足时，系统仍然应稳定工作
- 默认退路必须是“可用的 DSB 主链”
- 这样才不会因为 AEC 实验破坏当前 ASR 基线

## 12. 如何结合日志看状态切换

后续板端验证时，可以按下面的方式理解日志：

- `ref_state=missing`
  - 当前没有可用参考
  - 预期输出是 `DSB-only`
- `ref_state=idle`
  - 已经读到参考，但还不够稳定
  - 预期输出仍是 `DSB-only`
- `ref_state=active`
  - 参考稳定且满足门限
  - 才允许 `AEC+DSB`
- `fallback 增长`
  - 说明当前帧没有使用 AEC 输出
  - 原因可能是：
    - ref 不活跃
    - adapter 未 ready
    - push/pop/process 失败

如果后续要判断 AEC 是否真的起效，不能只看 `active`，还要同时看：

- `used`
- `fallback`
- `underrun`
- `process_fail`
- `resets`

只有在：

- `active` 稳定
- `used` 持续增长
- `fallback/underrun/process_fail` 不异常

时，才说明 AEC 实际在稳定参与链路。

## 13. 参考代码与文档

- SDK 官方说明：
  - [speechmind README](/root/ameba-rtos-1.2/component/application/speechmind/README.md)
- AIVoice AFE 配置：
  - [aivoice_afe_config.h](/root/ameba-rtos-1.2/component/aivoice/include/aivoice_afe_config.h)
- SDK 外部 AECM 示例：
  - [voice_service.c](/root/ameba-rtos-1.2/component/aivoice/examples/speechmind_demo/platform/ameba_dsp/voice_service.c)
- 当前项目参考 ring：
  - [river_voice_ref.c](/root/ameba-river/components/river_voice/river_voice_ref.c)
- 当前项目实验前端：
  - [river_voice_preproc_fixed_dsb.c](/root/ameba-river/components/river_voice/river_voice_preproc_fixed_dsb.c)
- 当前项目 echo / vad probe 中的参考路径：
  - [river_voice_echo.c](/root/ameba-river/components/river_voice/river_voice_echo.c)
  - [river_voice_vad_probe.c](/root/ameba-river/components/river_voice/river_voice_vad_probe.c)
