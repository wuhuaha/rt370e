# `/root/kws-training-pro` DS-CNN 模型迁移评估报告

Date: 2026-03-27

## 1. 结论摘要

结论先行：

- **可以迁移，但只能“选择性迁移”，不能整仓直接迁移。**
- **最值得迁移的是方法和结构，不是当前现成权重。**
- **`/root/kws-training-pro` 中的 DS-CNN 架构本身具备落地潜力，但当前 student 训练链存在输入特征契约不一致、验证链不一致、蒸馏损失定义不严谨等问题。**
- **如果直接把 `/root/kws-training-pro/models/dscnn_v2_ultra/dscnn_v2_best.onnx` 当成板端可用模型，风险很高，不建议。**
- **正确路径是：保留该仓库中的 DS-CNN 架构、teacher-student 思路、部分增强/采样策略；重新对齐到当前 `ameba-river` 板端真实前端，再在当前语料体系下重新训练、量化、部署。**

一句话判断：

- **架构可迁**
- **训练思想可迁**
- **特征提取实现不能原样迁**
- **现成 student 权重不建议直接迁**
- **需要在 `ameba-river` 目标契约下重训后再部署**

## 2. 本次评估范围

本次评估覆盖两部分：

- 训练仓：`/root/kws-training-pro`
- 目标工程：`/root/ameba-river`

重点回答的问题：

1. `/root/kws-training-pro` 的 DS-CNN 是否与当前板端运行时兼容
2. 其 student 模型是否可以直接迁到当前板端
3. 如果不能直接迁，哪些部分应该保留，哪些部分必须重做
4. 一条专业、可落地的迁移实施路线应该是什么

## 3. `/root/kws-training-pro` 实际包含什么

该路径下不是单一 DS-CNN 模型文件，而是一套完整的 teacher-student 训练工作区，主要组成如下：

- teacher：
  - `train_v2.py`
  - `configs/training_config.yaml`
  - `KWSModel(input_shape=(16, 96))`
- student：
  - `model_dscnn.py`
  - `train_dscnn_distill.py`
  - `train_dscnn_v2.py`
  - `river_kws_features.py`
  - `validate_final.py`
- 导出产物：
  - `models/dscnn_v1/dscnn_xiaou.onnx`
  - `models/dscnn_v2_ultra/dscnn_v2_best.onnx`
  - `models/xiaou_v2_balanced/best_model.onnx`
- 文档：
  - `README.md`
  - `DOCS_QUANT_DEPLOY.md`

从设计意图看，它采用的确实是合理方向：

- **teacher**：较重的 DNN + OpenWakeWord embedding
- **student**：较轻的 DS-CNN，面向端侧部署

这一大方向与当前 `ameba-river` 的板端诉求是匹配的。

## 4. 当前 `ameba-river` 板端真实运行契约

当前板端 KWS 运行时的真实契约，以 `components/river_voice/river_voice_kws.cc` 为准，而不是以训练仓中的文档描述为准。

### 4.1 板端音频前端链路

当前链路是：

`capture(16ms, 2ch) -> fixed_dsb mono -> silero VAD -> VAD gate -> KWS worker -> log-mel -> DS-CNN -> wake event`

关键参数：

- 采样率：`16 kHz`
- 帧长：`16 ms`
- 输入到 KWS 的 PCM 帧：`256 samples`
- STFT 窗长：`512`
- hop：`160`
- mel bins：`40`
- feature frames：`98`
- pre-roll：`320 ms`
- inference stride：当前 `prj.conf` 为 `4`
- cooldown：`1800 ms`

### 4.2 板端输入特征不是“任意 Mel”

当前板端输入不是泛化意义上的 librosa mel，而是**一套非常具体的 streaming log-mel 契约**：

1. WebRTC `RealFFT`
2. 功率谱 `power spectrum`
3. 三角 mel filterbank
4. `mel_band_norm`
5. `10 * log10(energy)`
6. 取整窗内 `max_db`
7. 转成 `relative_db = frame_db - max_db`
8. 裁剪到 `[-80 dB, 0 dB]`
9. 使用固定全局均值/方差归一化：
   - `mean = -42.1177063`
   - `std = 17.5219841`

因此当前板端模型真正看到的是：

- **流式**
- **相对 dB**
- **98x40**
- **严格依赖板端同一套前端实现**

### 4.3 板端 TFLM op 能力

当前 `river_voice_kws.cc` 注册的 op 为：

- `Quantize`
- `Conv2D`
- `DepthwiseConv2D`
- `Mean`
- `FullyConnected`
- `Logistic`

这说明只要导出的 TFLite 图经过常规 folding，DS-CNN 这类结构在 op 级别是**有希望直接落板**的。

## 5. `/root/kws-training-pro` DS-CNN 的兼容性评估

## 5.1 架构层面：**兼容性较好**

`model_dscnn.py` 中的 student 架构是典型端侧 DS-CNN：

- 首层 `Conv2d`
- 多层 depthwise separable block
- `AdaptiveAvgPool2d(1)`
- `Linear`
- `Sigmoid`

这类结构的优点：

- 参数量不大
- 计算图规则
- 易量化
- 与当前板端 TFLM resolver 的 op 集较匹配

基于结构手工估算，其可训练参数大约在 **2.25 万级别**，量化后权重体量属于当前 RTL8730E 可接受区间。现有产物也印证了这一点：

- `dscnn_v1/dscnn_xiaou.onnx`: `42234B`
- `dscnn_v2_ultra/dscnn_v2_best.onnx`: `40835B`

这与当前板端已集成的 DS-CNN 级别模型体量是一个量级。

**结论**：

- **DS-CNN 网络结构本身可以迁。**

## 5.2 特征契约层面：**这是最大风险点**

`/root/kws-training-pro` 内部实际上存在两套 student 特征路径：

### 路径 A：旧版 `v1`

见：

- `train_dscnn_distill.py`
- `validate_final.py`

特点：

- librosa mel
- `n_fft=480`
- `hop_length=160`
- `40 x 101`
- 文档也按 `101` 帧描述 streaming

这一路径与当前板端 `98 x 40` 契约**明显不一致**，不能直接迁。

### 路径 B：新版 `v2`

见：

- `train_dscnn_v2.py`
- `river_kws_features.py`

表面上，它已经切到：

- `512` 窗长
- `160` hop
- `40` mel
- `98` 帧

看起来更接近板端。

但真正的问题在于：

- `train_dscnn_v2.py` 调用的是 `RiverKwsFeatureExtractor.extract_from_array()`
- 这个函数**并不等于当前板端实现**

`extract_from_array()` 的主要差异：

1. 用的是 `abs(rfft)` 的幅度谱均值近似，不是板端的 `power spectrum`
2. 不是板端那套三角 mel 加权实现
3. 不是板端那套 `mel_band_norm` 归一
4. 也不是板端运行时 `max_db -> relative_db -> clip -> normalize` 的同一逻辑

同一个 `river_kws_features.py` 文件里，后面的 `extract()` 其实更接近板端真实实现，但 `train_dscnn_v2.py` 训练时并没有使用它。

这意味着：

- **`v2` 名义上是板端对齐版**
- **但其训练时实际用到的特征，仍然不是当前板端真实特征**

这是最关键的结论之一。

**结论**：

- **`/root/kws-training-pro` 的现成 student 权重，不能视为“已与当前板端前端严格对齐”。**
- **如果直接导入板端，极有可能出现 host 验证正常、板端实测偏移明显的问题。**

## 5.3 teacher-student 方法论层面：**可以保留**

`/root/kws-training-pro` 的 teacher-student 总体思路是合理的：

- teacher：高精度、重模型、宿主机训练验证
- student：轻量模型、端侧部署

这和当前项目目标完全一致，尤其适合：

- 少样本真实板载数据
- TTS 合成补覆盖
- 手机上采补多说话人/多环境
- 最终落到 `TFLite Micro`

**结论**：

- **teacher-student 方法论值得保留。**

## 5.4 蒸馏损失层面：**需要重做**

`train_dscnn_v2.py` 当前 soft loss 写法是：

- teacher 输出已经是 `sigmoid` 后概率
- student 输出也是 `sigmoid` 后概率
- 再对 student 结果取 `log`
- 用 `KLDivLoss` 去逼近 teacher 概率

这不是一个理想的 KD 形式，原因有三点：

1. 它处理的是**单标量 sigmoid 概率**，不是多类 softmax 分布
2. teacher/student 都不是 logits，温度蒸馏的语义不完整
3. 极端接近 `0/1` 的概率在数值上不稳定，也不利于学习难负例边界

更合理的做法是：

- teacher 输出保留为 logits 或至少稳定 posterior
- student 训练用：
  - `hard BCE`
  - `soft target BCE / MSE / logits distillation`
  - 必要时再加 ranking/margin loss

**结论**：

- **蒸馏路线可用，但 `/root/kws-training-pro` 当前的 KD loss 不建议直接沿用。**

## 5.5 验证链层面：**当前验证脚本不能作为最终落板依据**

`validate_final.py` 仍然验证的是：

- `dscnn_v1`
- librosa `40x101`
- 阈值固定 `0.5`

因此它无法回答下面这个真正重要的问题：

- 在**当前板端真实前端 + 当前板端真实 streaming/gate/cooldown 逻辑**下，student 是否可用

**结论**：

- **该仓的验证脚本可参考，但不能作为当前板端落地验收工具。**

## 5.6 导出部署层面：**思路可用，文档部分过时**

`DOCS_QUANT_DEPLOY.md` 的部署思路是合理的：

- ONNX -> TFLite
- PTQ / full int
- 端侧必须按 streaming 方式滑窗推理

但该文档默认描述的是：

- `101` 帧
- 每 `100 ms` 进 `10` 帧

它并没有反映当前 `ameba-river` 真实运行时：

- `98` 帧
- `16 ms` PCM 帧
- `VAD gate + pre-roll + worker ring + stride inference`

**结论**：

- **文档可作为思路参考，不可直接当成当前板端部署说明。**

## 5.7 环境层面：**迁移前需要先做环境隔离**

本次评估期间，直接在当前基础 Python 环境导入 `torch` 时出现：

- `libtorch_cuda.so: undefined symbol: ncclCommWindowDeregister`

这说明 `/root/kws-training-pro` 当前运行环境本身也存在 CUDA / PyTorch 兼容性问题。

这不影响“模型方法能否迁移”的判断，但会直接影响“能否稳定重训”和“能否复现旧模型”。

**结论**：

- **落地时必须把训练环境独立封装，不能默认依赖当前全局环境。**

## 6. 迁移判断：哪些可以迁，哪些不能直接迁

## 6.1 建议迁移的部分

建议保留并迁移：

- `model_dscnn.py` 的 DS-CNN 架构思路
- teacher-student 双阶段方法
- `train_dscnn_v2.py` 中的部分数据增强思路：
  - time shift
  - 轻量 SpecAugment
- device/mobile 正例加权思想
- `DOCS_QUANT_DEPLOY.md` 中关于流式部署和量化的总体思路

## 6.2 不建议直接迁移的部分

不建议直接原样迁移：

- `train_dscnn_distill.py` 的 `40x101` 特征路径
- `validate_final.py` 的验证结论
- `train_dscnn_v2.py` 当前 KD loss 定义
- `river_kws_features.py` 中训练实际使用的 `extract_from_array()`
- 现成 student 权重：
  - `models/dscnn_v1/dscnn_xiaou.onnx`
  - `models/dscnn_v2_ultra/dscnn_v2_best.onnx`

## 6.3 不能直接信任的部分

尤其不能直接假设下面这件事成立：

- “`v2` 的 student 是按板端特征训出来的，所以可以直接量化落板”

当前证据不支持这个结论。

## 7. 推荐落地路线

## 7.1 总体原则

正确的落地原则不是“把 `/root/kws-training-pro` 当成成品往 `ameba-river` 搬”，而是：

- **把它当成一个候选方案库**
- **把当前 `ameba-river` 板端 runtime 契约当成唯一真值**

也就是说：

- **source of truth = `ameba-river/components/river_voice/river_voice_kws.cc`**
- 训练仓必须向板端契约收敛，而不是反过来

## 7.2 Phase A：冻结板端真值契约

目标：

- 把板端前端、输入形状、量化类型、推理节奏、触发逻辑固定成唯一目标接口

本阶段输出：

1. 一份明确的 feature contract 文档
2. 一份 Python 版 reference extractor
3. 一套 feature parity 校验样本

验收标准：

- Python extractor 与板端 C 实现对同一音频片段的特征差异足够小
- 输入 shape、量化、阈值语义与板端完全一致

## 7.3 Phase B：只迁“架构”，不迁“旧权重”

目标：

- 将 `/root/kws-training-pro/model_dscnn.py` 作为候选 student 架构引入新的训练工作区

动作：

1. 保留 DS-CNN 拓扑
2. 按当前板端契约固定输入为 `1x40x98`
3. 统一输出语义为单标量 wake posterior
4. 先做 `PyTorch -> ONNX -> TFLite int8` 的纯导出 smoke test

验收标准：

- 导出后的 TFLite 只包含板端已支持 op
- 模型尺寸、arena 需求可接受

## 7.4 Phase C：重写 student 训练链

目标：

- 在当前真实数据体系下，重建板端可部署的 student 训练闭环

训练输入建议：

- 板载实录正例
- 板载近音负例
- 板载上下文/命令负例
- 手机录音正例和弱负例
- TTS 正例、近音负例、困难上下文

关键点：

1. 特征必须用板端对齐 extractor
2. hard label 先跑通 baseline
3. KD 必须重写为更稳健形式：
   - `BCE(student, hard_label)`
   - `BCE(student, teacher_prob)` 或 logits distillation
4. 训练采样重点压：
   - `phone_wake_like_negative`
   - `board_verifier_negative`
   - `board_hard_negative`
   - `wake+command`

验收标准：

- host eval 上能明显压低板载负例热度
- 不是只保 recall、不顾 FPR

## 7.5 Phase D：统一评估口径

目标：

- 避免训练仓和板端仓各自一套评价标准

建议统一核心指标：

- `board_positive_eval recall`
- `board_negative_eval fpr`
- `phone_wake_like_negative` 命中率
- `board_verifier_negative` 命中率
- `wake+command` 正例召回

部署阈值选择原则：

- 不按固定 `0.5`
- 采用板载数据 sweep threshold
- 以板载负例误唤醒率和板载正例召回共同决定

## 7.6 Phase E：量化与板端集成

目标：

- 产出能够直接进入 `river_voice_kws.cc` 的 int8 TFLite

要求：

1. 代表性数据必须以板载正例/负例为主
2. TFLite 图中 op 必须匹配当前 resolver
3. 量化输入输出类型要与板端解释逻辑一致
4. 生成 header 后，先做 build-level smoke，再做 board-level live test

验收标准：

- `river_voice_kws_init()` 成功
- `AllocateTensors()` 成功
- 串口日志能看到稳定推理
- 板测 recall/FPR 不明显背离 host eval

## 7.7 Phase F：运行时联调

目标：

- 把训练好的 student 真正放到当前系统行为链里验证

必须联调的不是只有“模型”，还包括：

- VAD gate
- pre-roll
- inference stride
- trigger hold
- cooldown
- session coordinator 的 wake admission

最终验收不是“离线分数高”，而是：

- 日常对话中不乱醒
- 目标唤醒词可稳定唤醒
- `wake+command` 场景不明显丢词

## 8. 推荐执行顺序

推荐顺序如下：

1. 冻结当前板端 feature contract
2. 从 `/root/kws-training-pro` 只迁 DS-CNN 架构定义
3. 重写板端对齐 extractor
4. 先跑 hard-label baseline
5. 再接入 teacher / assistant soft target
6. 输出 int8 TFLite
7. 板端阈值/冷却/门控联调

不建议的顺序：

1. 直接迁现成 ONNX
2. 直接量化
3. 直接烧板
4. 再看为什么板上和 host 不一致

这条顺序会把“特征不一致”“流式运行时不一致”“阈值不一致”混在一起，调试成本极高。

## 9. 预计工作量

如果只做“选择性迁移 + 当前体系内重训落板”，预计工作量大致如下：

- Phase A-B：`0.5 ~ 1` 天
- Phase C：`1 ~ 2` 天
- Phase D-E：`1` 天
- Phase F：`1 ~ 2` 天

总计大约：

- **3.5 ~ 6 天工程量**

如果试图复用 `/root/kws-training-pro` 现成 student 权重并强行修补，表面上看更快，但大概率会把时间消耗在：

- host/board 行为不一致
- feature mismatch
- threshold mismatch
- 伪有效验证

这条路总体风险更高。

## 10. 最终建议

最终建议如下：

- **不要直接迁移 `/root/kws-training-pro` 的现成 student 模型权重到当前板端。**
- **应当迁移其 DS-CNN 架构与 teacher-student 思路，并在当前 `ameba-river` 板端契约下重建 student 训练链。**
- **迁移工作的第一优先级不是“导模型”，而是“对齐前端特征真值”。**

如果只用一句话概括：

- **`/root/kws-training-pro` 适合作为“方案母本”，不适合作为“现成成品”。**

## 11. 建议后续动作

建议下一步按下面顺序推进：

1. 在当前仓库中固化“板端 KWS feature contract 文档”
2. 抽取并清洗 `/root/kws-training-pro/model_dscnn.py` 为候选 student 架构
3. 用当前 `ameba-river` 板端特征契约重写 student 训练输入
4. 基于现有板载/手机/TTS 语料跑新的 baseline
5. 再决定是否继续复用其 teacher 或 assistant 路线

---

## 附：本次判断依赖的关键证据文件

- `/root/kws-training-pro/README.md`
- `/root/kws-training-pro/model_dscnn.py`
- `/root/kws-training-pro/train_dscnn_distill.py`
- `/root/kws-training-pro/train_dscnn_v2.py`
- `/root/kws-training-pro/river_kws_features.py`
- `/root/kws-training-pro/validate_final.py`
- `/root/kws-training-pro/configs/training_config.yaml`
- `/root/kws-training-pro/DOCS_QUANT_DEPLOY.md`
- `/root/ameba-river/components/river_voice/river_voice_kws.cc`
- `/root/ameba-river/components/river_voice/river_voice_frontend.c`
- `/root/ameba-river/KWS_PIPELINE_ZH.md`
- `/root/ameba-river/prj.conf`
