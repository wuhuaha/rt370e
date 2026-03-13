# 固定延迟求和波束成形方案说明

## 1. 背景与目标

当前项目的唯一优先目标是提升在线 ASR 的识别准确率，而不是追求复杂、激进、难以调试的空间声学算法。

结合当前硬件与场景，项目采用如下约束：

- 板级阵列：`AMIC1 + AMIC3`
- 阵列形态：`2 麦线阵`
- 标称间距：`50mm`
- 采样率：`16kHz`
- 主场景：智能家居中控屏，用户通常站在设备正前方说话

在这个条件下，当前项目将 SDK 自带 BF 从运行路径中移除，改为使用软件实现的固定延迟求和波束成形（Fixed Delay-and-Sum Beamforming, DSB）。

## 2. 为什么选择 DSB

对于当前项目，DSB 有几个核心优势：

- 对 ASR 友好  
  DSB 的失真风险低于更激进的自适应波束成形，通常更利于保留语音的稳定频谱特征。

- 鲁棒性高  
  在真实房间混响、设备结构误差、麦克风一致性一般的情况下，DSB 往往比复杂算法更稳定。

- 资源占用低  
  不需要额外的大块模型内存，也不需要复杂自适应迭代。

- 易调试、易解释  
  输入、输出和参数都非常直接，后续做 A/B 对比、资源测量和回归验证更简单。

## 3. 当前实现策略

### 3.1 当前默认策略

当前项目采用：

- `Broadside` 正前方固定波束
- `0 sample` 默认延迟
- `双麦求和后除 2`
- 输出单声道 `16-bit PCM`

这意味着，在当前主场景下，系统假设：

- 用户主要面向屏幕正前方说话
- 两颗麦克风到目标声源的路径差可以先近似视为 0

对这个场景，`0-delay DSB` 是最稳妥的第一版工程实现。

### 3.2 处理流程

当前链路为：

`capture (2ch PCM16) -> fixed_dsb (1ch PCM16) -> silero_vad -> online ASR`

### 3.3 代码位置

核心实现位置：

- 预处理实现：
  - [components/river_voice/river_voice_preproc_fixed_dsb.c](/root/ameba-river/components/river_voice/river_voice_preproc_fixed_dsb.c)
- 预处理抽象：
  - [components/river_voice/river_voice_preproc.c](/root/ameba-river/components/river_voice/river_voice_preproc.c)
  - [include/river/river_voice_preproc.h](/root/ameba-river/include/river/river_voice_preproc.h)
- 板级阵列信息：
  - [components/river_voice/river_voice_board.c](/root/ameba-river/components/river_voice/river_voice_board.c)
  - [include/river/river_voice_board.h](/root/ameba-river/include/river/river_voice_board.h)
- 启动与运行日志：
  - [components/river_voice/river_voice_frontend.c](/root/ameba-river/components/river_voice/river_voice_frontend.c)
  - [components/river_voice/river_voice_vad_probe.c](/root/ameba-river/components/river_voice/river_voice_vad_probe.c)

## 4. 当前实现细节

当前 `fixed_dsb` 的处理逻辑非常简单：

1. 读取 `AMIC1` 和 `AMIC3` 两路输入
2. 对第二路应用固定整数采样延迟（当前默认 `0`）
3. 两路样本求和
4. 整体除以 `2`
5. 进行 `q15` 饱和裁剪
6. 输出为一路单声道语音

这是一种典型的工程可控实现，便于后续继续做：

- `BF on/off` 对比
- 固定延迟样本数调参
- 与原始双麦直通对比
- 与后续真实 `AEC + playback ref` 路径配合

## 5. 当前参数与后续可调项

### 当前参数

- 麦克风：`AMIC1 + AMIC3`
- 间距：`50mm`
- 波束指向：正前方
- 固定延迟：`0 sample`

### 已保留的调参点

Kconfig 中已加入：

- `CONFIG_RIVER_VOICE_DSB_SECONDARY_DELAY_SAMPLES`

当前默认是：

- `0`

如果后续离线分析或主观测试表明用户主说话方向存在固定偏移，可先从这个参数开始调。

## 6. 为什么现在不启用 SDK BF

当前项目已经明确：

- 不再使用 SDK 提供的 BF 作为主前端算法

原因不是“SDK BF 一定错误”，而是从当前项目目标出发，它有几个问题：

- 不利于问题定位  
  其内部处理不可见，难以快速判断问题来自阵列、参考、参数还是实现。

- 与当前 ASR 目标不完全一致  
  当前阶段目标是“先把 ASR 做准”，不是先把整套复杂声学前端堆满。

- 调试成本高  
  在板端资源、网络链路、在线 ASR、VAD 同时都在变化时，继续叠加黑盒 BF 会降低可控性。

## 7. 为什么当前也不默认启用 AEC

当前主链路是 `asr_mainline`，没有稳定、真实的播放参考。

因此：

- 没有真实 `playback ref` 时，不应启用 AEC
- 用假参考或零参考去“开 AEC”会直接损伤识别输入

后续如果要评估 AEC，应切到：

- 有真实扬声器回放
- 有稳定 `playback_ref`
- 可观测 `ref peak`

的场景中，再与 DSB 组合测试。

## 8. 后续建议

建议按下面顺序推进：

1. 先固定当前 `0-delay DSB`
2. 做 `BF off(主麦直通)` vs `DSB on` 的识别率对比
3. 如有需要，再测试 `secondary delay` 的固定样本偏移
4. 等真实播放参考链稳定后，再引入 `AEC`
5. 只有当 DSB 明显不够时，再考虑更复杂的波束成形方案

## 9. 参考资料

### 理论参考

- Harry L. Van Trees  
  `Optimum Array Processing: Part IV of Detection, Estimation, and Modulation Theory`

### 仿真参考

- `pyroomacoustics`

### 工程参考

- 用户提供的 DSB 工程说明，核心观点已被当前项目采纳：
  - DSB 在数学与工程上足够简单
  - 对 40mm - 60mm 双麦智能家居设备很适合
  - 对 ASR 友好
  - 非常适合作为低成本设备的首选空间前端

## 10. 当前结论

当前项目对声学前端的明确策略是：

- 以提升 ASR 准确率为唯一目标
- 以 `固定延迟求和波束成形` 作为当前主前端空间增强方案
- 暂不使用 SDK BF
- 仅在未来具备真实参考时再评估 AEC
