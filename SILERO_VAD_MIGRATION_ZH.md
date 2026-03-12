# Silero VAD 迁移说明（中文）

这份文档面向 `ameba-river` 项目中的中文开发者，目的不是记录“发生过什么”，而是给出一份可以直接照着执行的迁移说明书，方便后续：

- 重新导出当前 `Silero VAD` 模型
- 替换为新的 `Silero` 上游模型
- 重新生成固件内嵌模型数据
- 在 `RTL8730E` 项目里继续验证和审核

如果你想看更偏“过程记录”的版本，可以再参考：

- [.codex/silero_vad_migration_zh.md](/root/ameba-river/.codex/silero_vad_migration_zh.md)
- [.codex/silero_vad_porting.md](/root/ameba-river/.codex/silero_vad_porting.md)

## 1. 当前迁移结果

当前项目已经完成这些事情：

- 固定了官方 `Silero VAD` 上游模型来源
- 从官方 ONNX 重建出可用的 `TFLite` 模型
- 将 `.tflite` 转成了固件内嵌的 `.h/.cc`
- 在 `RTL8730E` 板端通过 `TFLite Micro` 跑通了 `Silero VAD`
- 已接入项目的 `detector` 层，并在 `vad_probe` 链路中实际运行

当前项目使用的关键文件：

- 官方 ONNX：
  - [silero_vad_16k_op15.onnx](/root/ameba-river/third_party/silero_vad/upstream/silero_vad_16k_op15.onnx)
- 生成的 TFLite：
  - [silero_vad_16k_b1_fp32.tflite](/root/ameba-river/third_party/silero_vad/generated/silero_vad_16k_b1_fp32.tflite)
- 固件内嵌模型：
  - [river_silero_vad_model_data.h](/root/ameba-river/components/river_voice/generated/river_silero_vad_model_data.h)
  - [river_silero_vad_model_data.cc](/root/ameba-river/components/river_voice/generated/river_silero_vad_model_data.cc)
- 板端运行时：
  - [river_voice_detector_silero.cc](/root/ameba-river/components/river_voice/river_voice_detector_silero.cc)

## 2. 当前模型与运行时约束

这部分是后续换模型时最容易踩坑的地方。

### 2.1 当前固定的上游来源

- 仓库：`https://github.com/snakers4/silero-vad`
- 固定 commit：`0dd0d85ee86b1f9d178dc26a04e60e90de26a80f`
- 当前选用文件：
  - `third_party/silero_vad/upstream/silero_vad_16k_op15.onnx`
- 当前官方 ONNX sha256：
  - `7ed98ddbad84ccac4cd0aeb3099049280713df825c610a8ed34543318f1b2c49`

### 2.2 当前运行时契约

当前不是任意 `Silero` 模型都能直接替换，运行时默认假设这些条件：

- 输入采样率：`16 kHz`
- 当前 chunk：`512 samples`
- rolling context：`64 samples`
- 实际模型音频输入：`576 samples`
- recurrent state shape：`[2, 1, 128]`
- 输出：
  - `output`：语音概率
  - `stateN`：下一帧状态

项目里的 `preproc` 每次只输出 `256 samples / 16 ms`，所以 `detector` 层会自己做：

- `2 x 256` -> `512` 当前窗口
- 再拼 `64` 历史 context
- 最终构成 `[1, 576]`

如果你换的新模型不满足这个契约，就不能只替换模型文件，必须同步改：

- [river_voice_detector_silero.cc](/root/ameba-river/components/river_voice/river_voice_detector_silero.cc)
- 相关日志描述
- 可能还包括阈值和状态缓存逻辑

## 3. 目录结构说明

### 3.1 上游与生成产物

- `third_party/silero_vad/upstream/`
  - 放上游模型和上游元数据
- `third_party/silero_vad/generated/`
  - 放当前导出的 `.tflite`、校验结果、生成元数据

### 3.2 转换工具

- `tools/silero_vad/`
  - 放 PC 侧的迁移、提取、重建、生成脚本

### 3.3 固件接入

- `components/river_voice/generated/`
  - 放由 `.tflite` 生成的固件内嵌模型源文件
- `components/river_voice/river_voice_detector_silero.cc`
  - 板端运行时接入

## 4. tools/silero_vad 各文件作用与用法

这一节是本文最重要的部分。

### 4.1 [stage_conversion_source.py](/root/ameba-river/tools/silero_vad/stage_conversion_source.py)

作用：

- 把仓库里的官方 ONNX 复制到临时路径
- 默认把临时副本设成只读
- 防止转换工具把仓库里的原始 ONNX 原地改坏

为什么必须有这个脚本：

- 迁移过程中已经实际踩过一次坑：某些 host 侧工具会改写输入 ONNX
- 所以仓库里的 `third_party/silero_vad/upstream/*.onnx` 必须视为只读真源

常用命令：

```bash
cd /root/ameba-river
source .venv-silero-convert/bin/activate
python tools/silero_vad/stage_conversion_source.py \
  --input third_party/silero_vad/upstream/silero_vad_16k_op15.onnx \
  --output /tmp/silero_vad_16k_op15.stage.onnx
```

输入：

- `--input`：仓库内官方 ONNX

输出：

- `--output`：临时 staged ONNX
- stdout 会输出一段 JSON，包含：
  - 输入路径
  - 输出路径
  - sha256
  - 文件大小
  - 是否只读

适用场景：

- 凡是要对 ONNX 做任何转换、简化、修图、导出尝试，都应该先跑这个脚本

### 4.2 [extract_onnx_manifest.py](/root/ameba-river/tools/silero_vad/extract_onnx_manifest.py)

作用：

- 提取 ONNX 图结构清单
- 输出 JSON 形式的结构描述，便于审核和排查

它会提取的内容包括：

- top-level inputs / outputs
- 初始化权重列表
- node 数量
- op 类型计数
- 子图结构

常用命令：

```bash
cd /root/ameba-river
source .venv-silero-convert/bin/activate
python tools/silero_vad/extract_onnx_manifest.py \
  --input third_party/silero_vad/upstream/silero_vad_16k_op15.onnx \
  --output third_party/silero_vad/upstream/silero_vad_16k_op15_manifest.json
```

用途：

- 审核模型输入输出是否变化
- 新模型迁移前先确认结构有没有本质变化
- 对比不同版本 ONNX 的结构差异

如果你准备换成新模型，这是建议最先跑的脚本之一。

### 4.3 [extract_reconstruction_tensors.py](/root/ameba-river/tools/silero_vad/extract_reconstruction_tensors.py)

作用：

- 从官方 ONNX 中提取“用于重建模型”的关键张量
- 尤其会还原 decoder `LSTM` 需要的派生张量

这个脚本不是简单 dump 全部权重，而是针对当前重建方案做的提取器。

它的输出包括：

- top-level initializer 对应张量
- 常量节点里的关键参数
- decoder `LSTM` 经 ONNX slice/concat 后的派生张量：
  - `decoder.lstm.W`
  - `decoder.lstm.R`
  - `decoder.lstm.B`

常用命令：

```bash
cd /root/ameba-river
source .venv-silero-convert/bin/activate
python tools/silero_vad/extract_reconstruction_tensors.py \
  --input third_party/silero_vad/upstream/silero_vad_16k_op15.onnx \
  --output third_party/silero_vad/upstream/silero_vad_16k_op15_reconstruction_manifest.json \
  --npz /tmp/silero_vad_16k_op15_reconstruction_tensors.npz
```

参数说明：

- `--input`：输入 ONNX
- `--output`：输出 JSON 摘要
- `--npz`：可选，把提取出的 numpy 张量打包成 `npz`

用途：

- 重建 TensorFlow 模型前的前置步骤
- 审核某个新 ONNX 是否还兼容当前重建路径

### 4.4 [rebuild_tf_silero_vad.py](/root/ameba-river/tools/silero_vad/rebuild_tf_silero_vad.py)

作用：

- 用 TensorFlow 重建 `Silero VAD`
- 对照 ONNXRuntime 做数值校验
- 导出 batch=1 的 `.tflite`

这是当前迁移链里最核心的脚本。

它内部做了这些事：

- 调用 `extract_reconstruction_tensors.build_manifest(...)` 提取权重
- 在 `SileroVadModule` 里重建：
  - STFT
  - encoder conv
  - decoder LSTM
  - final conv + sigmoid
- 用 ONNXRuntime 对同一随机输入做对照验证
- 可导出最终 `.tflite`

常用命令：

```bash
cd /root/ameba-river
source .venv-silero-convert/bin/activate
python tools/silero_vad/rebuild_tf_silero_vad.py \
  --input third_party/silero_vad/upstream/silero_vad_16k_op15.onnx \
  --verification-output third_party/silero_vad/generated/silero_vad_16k_b1_fp32_verification.json \
  --verify-cases 4 \
  --seed 8730 \
  --tflite-output third_party/silero_vad/generated/silero_vad_16k_b1_fp32.tflite
```

关键参数：

- `--verification-output`
  - 输出 ONNX vs TF/TFLite 对齐结果
- `--verify-cases`
  - 随机校验样本数
- `--seed`
  - 随机种子
- `--tflite-output`
  - 输出 `.tflite`

使用建议：

- 每次换模型后都应该重新生成 verification JSON
- 审核时先看 max abs diff 是否明显变坏

### 4.5 [generate_model_data.py](/root/ameba-river/tools/silero_vad/generate_model_data.py)

作用：

- 把 `.tflite` 转成可编译进固件的 `.h/.cc`

它会生成：

- `extern const unsigned int g_<symbol>_size;`
- `extern const unsigned char g_<symbol>[];`

并把模型字节数组写到 `.cc` 中。

常用命令：

```bash
cd /root/ameba-river
source .venv-silero-convert/bin/activate
python tools/silero_vad/generate_model_data.py \
  --input third_party/silero_vad/generated/silero_vad_16k_b1_fp32.tflite \
  --header components/river_voice/generated/river_silero_vad_model_data.h \
  --source components/river_voice/generated/river_silero_vad_model_data.cc \
  --symbol river_silero_vad_model_data
```

参数说明：

- `--input`：输入 `.tflite`
- `--header`：输出头文件
- `--source`：输出源文件
- `--symbol`：嵌入符号名

适用场景：

- 每次 `.tflite` 更新后，都必须重新生成一次

## 5. 推荐的完整迁移流程

下面是“重新迁移当前模型”或“迁移相近新模型”的推荐顺序。

### Step 1：准备 host 环境

```bash
cd /root/ameba-river
python3.10 -m venv .venv-silero-convert
source .venv-silero-convert/bin/activate
pip install --upgrade \
  pip setuptools wheel \
  onnx==1.17.0 onnxruntime==1.20.1 onnxsim==0.4.36 onnxoptimizer==0.3.13 \
  onnx-graphsurgeon==0.5.8 sng4onnx==1.0.4 \
  tensorflow-cpu==2.19.0 tensorflow==2.19.1 tf_keras==2.19.0 \
  onnx2tf==1.28.3 ai_edge_litert==1.2.0 \
  psutil==6.1.1 h5py==3.12.1 protobuf==5.29.3 flatbuffers==25.1.24 ml_dtypes==0.5.1
```

### Step 2：确认源模型不被污染

```bash
sha256sum third_party/silero_vad/upstream/silero_vad_16k_op15.onnx
```

当前正确值应是：

```text
7ed98ddbad84ccac4cd0aeb3099049280713df825c610a8ed34543318f1b2c49
```

### Step 3：生成结构清单

```bash
python tools/silero_vad/extract_onnx_manifest.py \
  --input third_party/silero_vad/upstream/silero_vad_16k_op15.onnx \
  --output third_party/silero_vad/upstream/silero_vad_16k_op15_manifest.json
```

### Step 4：生成重建张量清单

```bash
python tools/silero_vad/extract_reconstruction_tensors.py \
  --input third_party/silero_vad/upstream/silero_vad_16k_op15.onnx \
  --output third_party/silero_vad/upstream/silero_vad_16k_op15_reconstruction_manifest.json \
  --npz /tmp/silero_vad_16k_op15_reconstruction_tensors.npz
```

### Step 5：重建并导出 TFLite

```bash
python tools/silero_vad/rebuild_tf_silero_vad.py \
  --input third_party/silero_vad/upstream/silero_vad_16k_op15.onnx \
  --verification-output third_party/silero_vad/generated/silero_vad_16k_b1_fp32_verification.json \
  --verify-cases 4 \
  --seed 8730 \
  --tflite-output third_party/silero_vad/generated/silero_vad_16k_b1_fp32.tflite
```

### Step 6：生成固件内嵌模型代码

```bash
python tools/silero_vad/generate_model_data.py \
  --input third_party/silero_vad/generated/silero_vad_16k_b1_fp32.tflite \
  --header components/river_voice/generated/river_silero_vad_model_data.h \
  --source components/river_voice/generated/river_silero_vad_model_data.cc \
  --symbol river_silero_vad_model_data
```

### Step 7：编译固件

```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
```

### Step 8：烧录验证

```bash
cd /root/ameba-river
source env.sh
python3 tools/river_flash.py -p /dev/ttyUSB0
ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

## 6. 如果要迁移“新模型”，具体怎么改

这里说的“新模型”分两种情况。

### 6.1 只是同一系列新上游文件

例如：

- 还是 `Silero`
- 还是 `16kHz`
- 还是 `state=[2,1,128]`
- 只是换成另一个 commit 或另一个官方 ONNX

这时建议顺序：

1. 把新 ONNX 放到 `third_party/silero_vad/upstream/`
2. 更新对应 `METADATA.md`
3. 先跑：
   - `extract_onnx_manifest.py`
   - `extract_reconstruction_tensors.py`
4. 确认：
   - 输入 shape 没变
   - state shape 没变
   - decoder LSTM 仍能被当前提取器识别
5. 再跑：
   - `rebuild_tf_silero_vad.py`
   - `generate_model_data.py`
6. 最后编译固件

### 6.2 模型结构已经变了

例如：

- 输入不是 `576`
- state 维度变化
- encoder/decoder 结构变化
- 不再是当前这套 `Silero` VAD

这时不能只换 ONNX 文件，至少要重新审视：

- [extract_reconstruction_tensors.py](/root/ameba-river/tools/silero_vad/extract_reconstruction_tensors.py)
- [rebuild_tf_silero_vad.py](/root/ameba-river/tools/silero_vad/rebuild_tf_silero_vad.py)
- [river_voice_detector_silero.cc](/root/ameba-river/components/river_voice/river_voice_detector_silero.cc)

实际原则是：

- 先保证 host 侧重建和数值校验通过
- 再考虑板端运行时适配

## 7. 板端接入位置

### 7.1 detector 接口

- [river_voice_detector.h](/root/ameba-river/include/river/river_voice_detector.h)

这层是稳定边界。后续换别的 VAD，不应该直接改业务层。

### 7.2 Silero 运行时

- [river_voice_detector_silero.cc](/root/ameba-river/components/river_voice/river_voice_detector_silero.cc)

这里负责：

- `TFLite Micro` 初始化
- model tensor 绑定
- recurrent state 管理
- `256 -> 512 + 64` 的窗口拼接
- 概率转 `q15`
- `speech/silence` 判决

### 7.3 固件内嵌模型

- [river_silero_vad_model_data.h](/root/ameba-river/components/river_voice/generated/river_silero_vad_model_data.h)
- [river_silero_vad_model_data.cc](/root/ameba-river/components/river_voice/generated/river_silero_vad_model_data.cc)

### 7.4 当前验证链路

- [river_voice_vad_probe.c](/root/ameba-river/components/river_voice/river_voice_vad_probe.c)

当前默认链路是：

- `capture -> aivoice_afe -> silero_vad -> diagnostics`

说明：

- 当前是“纯 VAD 验证优先”
- 段缓存只在适合的 provider 下启用
- 这样便于后续接在线 ASR

## 8. 重新迁移后如何快速自检

### 8.1 host 侧

先看：

- ONNX sha256 是否正确
- verification JSON 中：
  - `max_abs_diff_output`
  - `max_abs_diff_state`

如果这两个值明显恶化，先不要上板。

### 8.2 编译侧

确认：

- `components/river_voice/generated/river_silero_vad_model_data.cc` 时间戳更新
- `build_RTL8730E/build/project_hp/image/km0_km4_ca32_app.bin` 已重新生成

### 8.3 板端

启动后重点看：

- `silero_vad runtime ready: ...`
- `arena=... used=...`
- `vad state=speech`
- `vad state=silence`

如果运行时没起来，优先查：

- model data 是否重新生成
- detector 的 tensor 契约是否和模型一致
- arena 是否足够

## 9. 已知容易踩坑的点

1. 不要直接对仓库里的官方 ONNX 做转换实验。
   - 先跑 `stage_conversion_source.py`

2. 不要把 `512` 错当成真实模型输入。
   - 当前真实输入是 `576`

3. 不要只更新 `.tflite` 不更新 `generated/*.h/*.cc`。
   - 固件吃的是嵌入后的模型字节，不是磁盘上的 `.tflite`

4. 不要只看“能编译过”，还要看数值校验。
   - 否则可能迁出一个“能跑但不可信”的模型

5. 不要在运行时契约变化时只改阈值。
   - 输入窗口、state shape 变了，阈值调不回来

## 10. 结论

当前项目里的 `Silero VAD` 迁移链已经不是临时脚本拼出来的实验状态，而是已经固化成了：

- 固定上游来源
- 固定 host 工具链
- 固定重建脚本
- 固定固件嵌入方式
- 固定板端 detector 接口

后续要做的事情不是“从零重新猜”，而是按本文顺序去：

1. 确认模型来源
2. 生成结构和重建清单
3. 重建并导出 `.tflite`
4. 生成固件模型字节
5. 编译、烧录、验证

只要后续新模型没有从根本上改变输入契约和网络结构，这套流程就可以直接复用。
