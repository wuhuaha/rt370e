# WebRTC AECM 接入 river 记录

## 当前状态（2026-03-16）

这批 `WebRTC AECM` 代码当前被定位为**实验资产**，已保留，但**没有并入主线运行链路**。

当前已执行的整理动作：

- 保留：
  - `components/river_voice/river_voice_webrtc_aecm_adapter.c`
  - `components/river_voice/river_voice_webrtc_aecm_adapter.h`
  - `third_party/webrtc_aecm/`
  - 本接入记录文档
- 已回退主线未提交改动：
  - `components/river_voice/CMakeLists.txt`
  - `components/river_voice/river_voice_frontend.c`
  - `components/river_voice/river_voice_preproc.c`
  - `components/river_voice/river_voice_preproc_fixed_dsb.c`
  - `components/river_voice/river_voice_vad_probe.c`

当前主线基线仍然是：

- `capture -> fixed_dsb -> silero_vad -> online ASR`

而不是：

- `capture -> webrtc_aecm_dsb -> silero_vad -> online ASR`

## 分支内阶段进展（debug/webrtc-aec）

### Phase 1 已落地：建立独立实验 profile，默认主链不变

当前已经完成：

- 新增独立实验 profile：
  - `fixed_dsb_webrtc_aecm`
- 默认主链仍保持：
  - `asr_mainline`
  - `fixed_dsb`
- 当前即使切到实验 profile，运行时也只会明确打印“实验 profile 已选中”，不会偷偷把未完成的 `AEC` 路径接进默认链路

这样做的目的：

- 先把配置边界和日志边界建立清楚
- 防止后续 AEC 试验再次污染当前稳定的 `fixed_dsb -> silero_vad -> online ASR` 主链

### Phase 2 已启动：把 AECM adapter 升级为“延迟但连续”的严格对齐模块

当前已完成的整理：

- 保留 `16ms = 256 samples` 主帧输入接口不变
- 明确 `AECM` 内部处理块仍为：
  - `10ms = 160 samples`
- 将 adapter 的输出契约改为：
  - `delayed-but-contiguous stream`
  - 只有在累计到至少一个完整主帧输出后，才允许上层取走 `256` 点
- 增加 adapter 可观测性：
  - `frames_pushed`
  - `frames_popped`
  - `blocks_processed`
  - `input_push_failures`
  - `process_failures`
  - `output_underruns`
  - `resets`
  - `samples_pushed`
  - `samples_popped`
  - `ref/mic_in/mic_out fifo depth`
  - `max fifo depth`
  - `primed_output`

当前阶段的关键判断：

- 这一步还没有把 AEC 正式接回 `river_voice_preproc`
- 但已经把“原型适配器”升级成了一个更适合后续集成和排障的基础模块
- 下一步仍然要完成：
  - 参考激活滞回模型
  - `preproc` 接入与安全 fallback

### Phase 3 已启动：为 playback reference 建立滞回状态机

当前已补充到 adapter 内部的能力：

- 参考状态枚举：
  - `missing`
  - `idle`
  - `active`
- 参考策略参数：
  - `enter_peak`
  - `exit_peak`
  - `stable_frames`
  - `hangover_frames`
  - `active_window_frames`
- 参考可观测性：
  - `last_ref_peak`
  - `ref_active_ratio_q15`
  - `ref_above_enter_streak`
  - `ref_below_exit_streak`
  - `ref_frames_seen`
  - `ref_state_entered_active`
  - `ref_state_exited_active`

当前阶段的定位：

- 先把“参考活跃判断”做成 adapter 内部稳定能力
- 还没有把它驱动到 `preproc` 的 AEC 开关
- 下一步才是把这个状态机接到实验 profile 的实际旁路 / AEC 选择逻辑中

### Phase 4 已落地：实验 profile 接回 preproc，但默认主链仍不变

当前已经完成：

- `fixed_dsb_webrtc_aecm` 实验 profile 现在可以真实打开：
  - `playback ref`
  - `webrtc_aecm_adapter`
  - `reference-gated` AEC 旁路切换
- 默认主链仍保持：
  - `capture -> fixed_dsb -> silero_vad -> online ASR`
- `vad_probe` 只在实验 profile 下才会：
  - 打开 `river_voice_ref`
  - 读取 mono playback ref
  - 把参考送入 `preproc`

当前实验 profile 的运行原则：

- 默认输出始终先走 `fixed_dsb`
- 只有当 `ref_state == active` 且 `adapter` 已经攒够一个完整主帧输出时，才切到 `AECM` 处理结果
- 当参考不存在、参考不活跃、输出未就绪、`push/pop/process` 任一步失败时，立即回退到 `fixed_dsb`
- `idle/missing` 状态下会主动 drain 掉 adapter 内部输出，避免旧帧残留污染后续激活段

这一步的意义：

- 实验 profile 终于具备“真实接入”的能力，而不只是日志占位
- 同时仍然把风险限定在实验链，不影响当前 `fixed_dsb` 主线

### Phase 5 已启动：板端默认打开实验 profile，并补充资源占用统计

当前新增：

- `prj.conf` 已切到：
  - `fixed_dsb_webrtc_aecm`
- 新增 `river.stats` 轻量资源快照：
  - `boot_ready`
  - `wifi_connected`
  - `asr_stream_active`
  - `asr_stream_finish`
  - `vad_speech`
  - `vad_silence`
- 新增 `preproc aecm stats` 运行态日志：
  - `ref_state`
  - `ratio_q15`
  - `peak`
  - `frames_pushed/popped`
  - `blocks_processed`
  - `input_push_failures`
  - `process_failures`
  - `output_underruns`
  - `resets`
  - `used/fallback`

这一步的目的：

- 把实验 profile 真正打开到板端验证
- 让每一次 `speech/silence`、流打开/关闭、Wi‑Fi 连通时，都能拿到稳定的资源快照
- 把 `AECM` 实验状态和系统资源状态分开观测，避免把“算法问题”和“资源问题”混在一起

### Phase 6 已落地：AEC 输入模型收敛为原生 `mic0 + mic1 + ref`

当前实验分支已不再使用：

- `playback_ring ref`
- 前处理阶段软件拼接 `[mic0, mic1, ref]`

而是直接收敛为：

- `capture(3ch) = mic0 + mic1 + ref`

具体实现约束：

- 仅实验 profile：
  - `fixed_dsb_webrtc_aecm`
  - 会把录音 `channel_count` 提升为 `3`
  - 并通过 `AudioRecord_SetParameters("ref_channel=2;cap_mode=no_afe_pure_data")`
    将第 3 路标记为 reference
- 默认主线：
  - 仍保持 `2ch mic`
  - 不引入原生 ref

这样调整后的意义：

- 实验链终于与 SDK 官方 `3ch ref` 输入模型一致
- 去掉了 `playback_ring` 带来的软件时序 tap 偏差
- 后续 AEC 评估可以集中到：
  - `10ms/16ms` 对齐
  - reference-active 滞回
  - 双讲/打断效果

当前实验链新的输入模型是：

- `capture(2mic+ref native ch3) -> fixed_dsb/webrtc_aecm(exp) -> silero_vad -> online ASR`

## 这次审查后的结论

方向是对的，但当前实现还**不适合直接合入主线**。主要原因不是代码风格，而是时序和算法接法风险会直接影响 VAD / ASR 结果。

### 1. AECM 的 10ms 处理块与当前 16ms 主帧没有被严格对齐

当前主链路每帧为：

- `16 ms @ 16 kHz = 256 samples`

而 WebRTC AECM 实际处理块是：

- `10 ms @ 16 kHz = 160 samples`

当前实验实现采用了：

- 输入一整帧 `256`
- 内部按 `160` 处理
- 再尝试攒够 `256` 输出

这会带来一个核心问题：

- 输出帧边界不再与输入帧边界严格对应
- 结果可能包含上一帧尾部与当前帧前部的拼接
- 会直接影响：
  - `Silero VAD` 的帧级时序
  - 后续 `ASR` 的语义边界

### 2. AEC 启停逻辑过于激进

当前实验版使用了“单帧参考峰值门限”来决定是否启用 AEC。

这会导致：

- 参考一旦短暂变弱，就立刻旁路
- 旁路时还会 reset AEC 内部状态
- 下一次参考恢复时，AEC 又要重新收敛

对于真实中控屏场景：

- TTS
- 提示音
- 断续播报
- 短暂停顿

这种策略很容易让 AEC 永远处于“刚收敛又被清空”的抖动状态。

### 3. `vad_probe` 不应该被污染成 AEC 实验路径

`vad_probe` 当前主职责是：

- 验证采集、前处理、VAD、ASR 上行是否正常

如果把 `playback reference`、AEC 触发、AEC 异常排查一起塞进去，会再次把以下问题混在一起：

- 麦克风采集问题
- 参考链路问题
- VAD 问题
- AEC 问题
- 云端上行问题

这不利于后续继续稳定迭代。

### 4. 命名与实际运行状态不应再次脱节

当前实验版一度想把后端统一叫成：

- `webrtc_aecm_dsb`

但实际上大量时间仍在走：

- `DSB-only bypass`

这会重演之前“模块名和真实运行算法不一致”的问题。后续设计必须保证：

- 主链名称反映真实默认行为
- AEC 作为明确的实验 profile 或运行时状态展示

## 下一版正确实施原则

后续如果继续接入 WebRTC AECM，建议按下面的原则重做：

### 原则 1：`BF/DSB` 主链保持稳定

当前主线继续以：

- `fixed_dsb`

作为默认前处理，不要在主线上直接掺入 AEC 实验逻辑。

### 原则 2：AEC 只作为独立实验 profile 或独立开关

不要直接污染 `asr_mainline`。

更合理的方式是：

- `fixed_dsb`：产品主链
- `fixed_dsb + webrtc_aecm`：实验 profile

### 原则 3：先解决 10ms / 16ms 的严格对齐问题

在把 AEC 接回主链前，必须先解决：

- 输入帧 `256`
- AECM 块 `160`
- 输出帧与当前时刻的严格对应关系

只有对齐模型清楚后，才谈效果评估。

### 原则 4：参考激活必须用“带滞回的稳定窗口”

不能再用单帧峰值直接开关 AEC。

下一版应至少具备：

- enter threshold
- exit threshold
- active ratio window
- hangover / hysteresis

### 原则 5：实验链与主线隔离

保留：

- `adapter`
- `third_party`
- 实验记录

但不要让实验性实现直接进入默认产品链路。

## 目标

把基于 WebRTC AECM 的回声消除接入 `ameba-river` 当前真实运行链路：

- `capture -> preproc -> silero_vad -> cloud asr`

要求：

- 有外放时，能够利用播放参考做 AEC。
- 无外放时，不影响现有双麦 + DSB + VAD + ASR 链路。
- 不能再依赖之前只接在 `aivoice/speechmind_demo` 里的那条路径。

## 实际链路分析

从启动日志确认，当前运行链路不是 `aivoice`，而是：

- 双麦采集：`AMIC1 + AMIC3`
- 前处理：`fixed_dsb`
- 检测：`silero_vad`
- 输出：`cloud asr stream`

因此 WebRTC AECM 必须接入 `components/river_voice`，而不是之前的 `examples/speechmind_demo/platform/ameba_dsp/voice_service.c`。

## 接入位置

最终选择接入到 `river_voice_preproc`：

- 输入：2 路 mic 原始采集帧
- 可选参考：`river_voice_ref` 提供的播放参考环
- 输出：1 路增强后的单声道，继续送给 Silero VAD 和在线 ASR

这样可以保证：

- 对 `vad_probe`、`audio echo test`、后续实时前端都复用同一个预处理后端
- AEC 与 DSB 统一在前处理层完成

## 方案设计

新后端逻辑是：

1. 从 `river_voice_ref` 读取 1 路播放参考。
2. 如果当前参考有效，先对两路麦分别跑 WebRTC AECM。
3. 再把 AEC 后的两路麦做固定延时求和 `DSB`，输出 1 路单声道。
4. 如果当前没有外放参考，直接旁路 AEC，只做原来的 DSB。

关键点：

- AECM 只在“有有效参考能量”时工作。
- 没有参考时，不强行开 AEC，避免无收益甚至损伤语音。
- 参考恢复后，继续自动进入 AEC 路径。

## 遇到的问题与处理

### 1. 之前的 AECM 接入路径根本不在当前运行链路里

问题：

- 之前移植写在 `ameba-aivoice/examples/speechmind_demo/...`
- 当前实际运行的是 `ameba-river/components/river_voice/...`

处理：

- 改为在 `river_voice_preproc` 接入
- 同时修改 `river_voice_vad_probe`，让它能够读取播放参考并喂给 preproc

### 2. 当前主链路默认没有参考输入

问题：

- 原本 `vad_probe` 显式要求 `reference-disabled preproc`
- 这会直接阻断 AEC 接入

处理：

- 去掉“必须禁用参考”的限制
- 在 `vad_probe` 中打开 `river_voice_ref`
- 每帧尝试读取参考，读不到时填零并计数

### 3. WebRTC AECM 只能处理 10ms 块，当前链路是 16ms

问题：

- 当前帧长是 `16ms @ 16kHz = 256 samples`
- WebRTC AECM 处理块是 `160 samples`

处理：

- 复用之前写好的 FIFO/拼块适配器思路
- 每帧把 `256` 个点推入 AECM adapter
- 内部按 `160` 点处理，并在输出 FIFO 累积到 `256` 点后再吐出一帧

### 4. 如果无参考仍强行跑 AEC，反而可能伤害语音

问题：

- 当前很多场景下没有真实外放
- 这时 AEC 没有有效 far-end 参考

处理：

- 增加“参考能量门限判断”
- 只有参考峰值超过阈值时才让 AECM 参与
- 否则直接回退到纯 DSB 路径

### 5. 首帧无法立刻得到完整 256 点 AEC 输出

问题：

- AECM 内部先按 160 点处理
- 第一次推入 256 点时，输出端通常还攒不够完整一帧

处理：

- 允许首批帧先走 DSB
- 等 AECM 输出 FIFO 累积够 `256` 点后再切到 AEC 路径
- 避免为了“凑满第一帧”而频繁 reset AEC 状态

## 最终接入结果

修改点包括：

- `components/river_voice/river_voice_preproc.c`
- `components/river_voice/river_voice_preproc_fixed_dsb.c`
- `components/river_voice/river_voice_vad_probe.c`
- `components/river_voice/river_voice_frontend.c`
- `components/river_voice/river_voice_webrtc_aecm_adapter.{c,h}`
- `components/river_voice/CMakeLists.txt`
- `third_party/webrtc_aecm/`

## 运行时预期

正常启动时应看到：

- `preproc backend: webrtc_aecm_dsb`
- `vad probe ref: backend=playback_ring reference=enabled no_ref_policy=bypass_to_dsb ...`

有有效外放参考时应看到：

- `preproc AECM active: valid playback reference detected, backend=webrtc_aecm_dsb`

没有外放时应看到：

- `preproc AECM bypass: no active playback reference, keep DSB-only path`

## 当前原则

- 有外放：启用 WebRTC AECM
- 无外放：自动旁路 AEC，仅保留 DSB
- 不再依赖 AIVoice 内建 AEC

## 当前保留资产

这次整理后，以下内容被保留作为后续实验基础：

- `components/river_voice/river_voice_webrtc_aecm_adapter.c`
- `components/river_voice/river_voice_webrtc_aecm_adapter.h`
- `third_party/webrtc_aecm/`
- 本文档

这些内容当前的定位是：

- **实验资产**
- **非主线默认实现**
- **后续重做 AEC profile 的起点**
