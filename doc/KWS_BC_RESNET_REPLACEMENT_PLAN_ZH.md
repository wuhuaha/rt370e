# BC-ResNet 唤醒词模型替换方案

日期：2026-03-30

## 1. 目标

将当前板端基线唤醒词模型替换为 `kws-training-pro` 中已完成导出的 `bc_resnet_best.tflite`，用于当前板端唤醒链路验证：

- 本地 `KWS`
- 唤醒后打开 `XiaoZhi` 会话窗口
- 进入上行 `ASR` 与下行 `TTS`

本次替换优先级是：

1. 先让新模型在板端正确加载和推理
2. 保持改动范围最小
3. 不把运行时改成“只为单个模型服务”的特判地狱

## 2. 当前结论

`bc_resnet_best.tflite` 不能直接替换当前模型，但经过小范围运行时适配后可以替换。

已确认的直接阻塞项有两类：

### 2.1 算子阻塞

当前板端 `river_voice_kws.cc` 的 TFLM resolver 没有注册 `Add`。

而 `bc_resnet_best.tflite` 是典型残差网络，内部存在多个 `ADD` 节点。如果不补这个算子，模型在 `AllocateTensors()` 之前就可能因为 resolver 不完整而失败。

### 2.2 输入布局阻塞

当前线上模型输入 shape 是：

- `[1, 98, 40, 1]`

当前板端特征写入顺序也是：

- `frame -> mel`

而 `bc_resnet_best.tflite` 的输入 shape 是：

- `[1, 40, 98, 1]`

也就是同样的 `3920` 个元素，但逻辑维度次序不同。

如果只替换模型文件、不处理输入布局，最危险的结果不是“立刻报错”，而是：

- 模型可以运行
- 但输入语义错位
- 分数长期失真
- 板端出现“偶发误醒/漏醒/完全不稳”的假象

## 3. 最小替换方案

本次采用“最小侵入替换”：

### 3.1 保留现有 KWS 前端

不改这些板端前端契约：

- `16kHz`
- `512` 点窗
- `160` 点 hop
- `98 x 40` Log-Mel 特征
- 现有 `mean/std` 归一化

原因：

- `kws-training-pro/river_kws_features.py` 已与板端前端对齐
- 该训练链会在导出前把特征转为 `(40, 98)` 供模型使用
- 因此替换重点在“板端输入布局适配”，而不是重写前端

### 3.2 板端做模型驱动的输入布局适配

运行时不再硬编码“只支持 `98x40x1` 写入顺序”，而是：

1. 优先从 flatbuffer schema 读取模型输入 shape
2. 识别以下两种合法布局：
   - `[1, 98, 40, 1]`
   - `[1, 40, 98, 1]`
3. 根据布局动态决定写入顺序：
   - `frame -> mel`
   - 或 `mel -> frame`

这样做的好处：

- 兼容当前线上模型
- 兼容本次 BC-ResNet 模型
- 不引入额外特征缓冲和显式转置拷贝
- 后续再切模型时，板端不必继续手工改输入顺序

### 3.3 维持基线配置槽位，替换其模型资产

当前 `prj.conf` 仍走：

- `CONFIG_RIVER_KWS_MODEL_VARIANT_BASELINE=y`

本次不新增第三个模型变体开关，而是直接把 baseline 槽位的模型资产替换为 `bc_resnet_best.tflite` 导出的板端头文件。

原因：

- 当前目标是尽快验证板端唤醒链路
- 避免为了一个过渡期模型再扩一套新的 Kconfig 组合
- 后续若 BC-ResNet 成为正式默认，再清理命名债务

## 4. 具体改动点

### 4.1 运行时代码

文件：

- `components/river_voice/river_voice_kws.cc`

改动：

- 将 resolver 容量从 `7` 提升到 `8`
- 注册 `Add`
- 增加模型输入 shape 解析与布局判断
- 在输入张量填充阶段按布局动态写入
- 日志改为打印真实模型输入布局，避免继续误导为固定 `dscnn 98x40x1`

### 4.2 模型资产

文件：

- `components/river_voice/generated/river_wake_word_model_data.h`

改动：

- 使用 `bc_resnet_best.tflite` 重新生成
- 保持现有符号名 `kws_model / kws_model_len`
- 不改其被引用路径，降低联动范围

### 4.3 日志语义

文件：

- `components/river_voice/river_voice_frontend.c`

改动：

- 把 `dscnn_kws` 一类特定模型字样改为更中性的 `local_kws`
- 保证日志与当前实际部署模型一致

## 5. 风险与应对

### 5.1 Arena 预算风险

虽然 `bc_resnet_best.tflite` 文件体积小于当前基线模型，但残差结构和中间激活形状不同，`TFLM arena` 需求不一定更小。

本次先保持当前 `160KB` arena 不变，原因是：

- 当前设备堆预算本来就紧
- 先验证是否真的不够，再做有根据的扩容

若板端出现：

- `kws AllocateTensors failed`

再单独评估提升 `CONFIG_RIVER_KWS_TENSOR_ARENA_KB`。

### 5.2 门限未重新标定

当前 `prj.conf` 中的 `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=8192` 是临时低门限，目的是让当前弱模型阶段更容易验证唤醒链路。

替换到 BC-ResNet 后，这个门限仍然保留，方便先验证：

- 是否能稳定唤醒
- 是否能打开小智会话
- 是否存在明显误醒

等链路确认后，再回头重新标定门限。

## 6. 实施顺序

### Step A

先落本文档，并同步更新计划与 `.codex` 跟踪文件。

### Step B

替换运行时代码和模型资产。

### Step C

执行本地 `RTL8730E` 构建验证。

### Step D

记录验证方法与结果，提交实现 commit。

## 7. 验证重点

板端刷机后重点观察：

1. `kws init` 是否成功
2. 日志中是否打印真实输入布局
3. 是否还能看到 `wakeword hit`
4. 唤醒后是否能进入 `XiaoZhi` websocket 会话
5. 是否出现新的 `AllocateTensors` 或 `resolver` 失败

如果这五项通过，本次替换就算第一阶段成功。
