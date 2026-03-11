# Silero VAD 中文迁移说明

## 1. 文档目的

这份文档面向项目中的中文开发者，目标是把 `Silero VAD` 从上游模型到 `RTL8730E` 板端可运行固件的整个迁移过程说明清楚，满足三个要求：

1. 能复现。
2. 能审核。
3. 能继续演进到后续在线 ASR / 自研 VAD。

本文不是简单的“如何调用一个模型”，而是完整说明：

- 选了哪一个上游模型，为什么。
- PC 侧如何把模型转换为项目内可用的 `TFLite`。
- 固件里接到了哪里。
- 迁移过程中踩到了哪些 `RTL8730E SDK / TFLite Micro` 兼容坑，最后怎么修的。
- 当前验证到了什么程度，下一步怎么接在线 ASR。

## 2. 当前迁移达成的效果

截至当前版本，项目已经达成以下结果：

1. `Silero VAD` 已在 `RTL8730E` 板端实际运行。
2. `Silero VAD` 已经不是“占位后端”，而是实际参与 `vad_probe` 的实时判决。
3. 日志中能同时看到：
   - `vad_raw_q15`
   - `vad_prob_q15`
   - `vad=speech|silence`
   - SDK `aivoice VAD` 参考判决
4. 默认验证链路已经切到纯 VAD 路径：
   - `capture -> aivoice_afe(asr_mainline) -> silero_vad + sdk_vad_ref -> segment buffer -> diagnostics`
5. 已经加入了面向在线 ASR 的段缓存：
   - VAD 前缓存 `pre-roll`
   - VAD 后缓存 `post-roll`
   - ready segment 可直接作为后续在线 ASR 的语音片段输入

这意味着：当前最困难的“模型迁移 + 板端跑起来 + 诊断可见”已经完成，后续主要工作变成参数收敛、路由接入、性能优化。

## 3. 为什么选 Silero VAD，而不是继续使用 SDK VAD

项目在早期验证中已经引入过 SDK `aivoice` 自带的 VAD 能力，但实际效果不满足需求：

- 对短句和弱语音不够敏感。
- 结果波动较大，不利于作为后续在线 ASR 的前置门控。
- 后续如果要切自研小模型，SDK VAD 的复用价值不高。

因此当前策略改成：

- `aivoice AFE` 继续保留，用于阵列前端增强。
- `VAD` 从 SDK 解耦，单独走 `Silero VAD`。
- 同时保留一个 `SDK VAD` 参考通路，只用于比对，不参与正式决策。

这样后续替换为自研 `TFLite Micro` / 自研 DSP VAD 时，只需要替换 `detector` 层，不需要碰 `capture/preproc/app`。

## 4. 当前目标架构

当前语音前端已经明确分层：

1. `river_voice_capture`
   - 板级采集
   - 当前输入：`AMIC1 + AMIC3`
   - `16kHz / 16ms / 2ch PCM16`

2. `river_voice_preproc`
   - 前处理适配层
   - 当前后端：`aivoice_afe`
   - 当前默认 profile：`asr_mainline`

3. `river_voice_detector`
   - 检测适配层
   - 当前正式后端：`silero_vad`
   - 当前参考后端：`aivoice_vad_v1_ref`

4. `river_voice_segment_buffer`
   - 用于为后续在线 ASR 做语音片段缓存
   - 当前配置：
     - `pre-roll = 384ms`
     - `post-roll = 768ms`
     - `max segment = 8000ms`

5. `river_voice_segment_sink`
   - 当前为 `online_asr_stub`
   - 作为 `segment_buffer` 到未来在线 ASR 的稳定交接边界

6. 后续在线 ASR
   - 还未正式接入
   - 但 `segment_buffer + segment_sink` 已经是稳定接入点

## 5. 上游模型来源与固定版本

### 5.1 上游仓库

- 仓库：`https://github.com/snakers4/silero-vad`
- 固定 commit：`0dd0d85ee86b1f9d178dc26a04e60e90de26a80f`

### 5.2 选用的模型文件

- 文件：`silero_vad_16k_op15.onnx`
- 项目内路径：
  - `third_party/silero_vad/upstream/silero_vad_16k_op15.onnx`
- sha256：
  - `7ed98ddbad84ccac4cd0aeb3099049280713df825c610a8ed34543318f1b2c49`

### 5.3 为什么选这个文件

原因有三点：

1. 项目主链路固定为 `16kHz`。
2. 这个官方模型比通用版本更适合当前嵌入式路径。
3. `op15` 比更新的 `op18` 在老一点的转换工具链上更容易作为兼容基线。

## 6. 官方流式契约

这是迁移时最关键的事实之一，必须明确：

- 当前 chunk：`512 samples`
- rolling context：`64 samples`
- 实际模型输入：`576 samples`
- recurrent state：`[2, batch, 128]`
- 输出：
  - `output`: speech probability
  - `stateN`: next recurrent state

项目当前 `preproc` 每次输出的是：

- `256 samples`
- `16 ms`
- `mono PCM16`

因此项目里做了一个适配：

- `2 x 256-sample frame` 组一个 `512-sample` 逻辑窗口
- 再由 detector 自己拼 `64-sample context`
- 最后构成 `576-sample` 真正模型输入

这件事如果理解错，整个迁移都会错。项目早期就因为把 `512` 错当成真实模型输入而走过弯路。

## 7. PC 侧迁移流程

### 7.1 独立转换环境

项目没有复用 SDK 自带 Python 环境，而是单独建立了转换虚拟环境：

- 路径：`/root/ameba-river/.venv-silero-convert`

建立方式：

```bash
python3.10 -m venv /root/ameba-river/.venv-silero-convert
```

### 7.2 安装的关键工具

核心工具包括：

- `onnx`
- `onnxruntime`
- `onnxsim`
- `onnxoptimizer`
- `onnx-graphsurgeon`
- `onnx2tf`
- `tensorflow`
- `tensorflow-cpu`
- `tf_keras`

完整安装记录见：

- `.codex/silero_vad_porting.md`

### 7.3 为什么没有直接依赖 onnx2tf 成功导出

项目初期尝试过直接把官方 ONNX 推过 `onnx2tf`，但失败了：

- 第一处失败：`wa/model/stft/Conv`
- 手工修一轮图之后，下一处失败：`wa/model/decoder/Squeeze`

结论是：

- 这条路对当前官方图不稳定
- 不能把整个迁移建立在“祈祷 onnx2tf 正常工作”上

因此项目改成：

1. 固定官方 ONNX 为唯一真源。
2. 用脚本从 ONNX 中提取权重和结构信息。
3. 在 TensorFlow 中重建模型。
4. 再从 TensorFlow 导出 `TFLite`。

## 8. 项目内的转换脚本

当前项目已经把迁移所需脚本都纳入版本控制：

### 8.1 源模型 staging

- `tools/silero_vad/stage_conversion_source.py`

作用：

- 从 vendored ONNX 复制一个临时副本到 `/tmp`
- 防止转换工具把仓库内的官方 ONNX 改坏

使用示例：

```bash
cd /root/ameba-river
source .venv-silero-convert/bin/activate
python tools/silero_vad/stage_conversion_source.py \
  --input third_party/silero_vad/upstream/silero_vad_16k_op15.onnx \
  --output /tmp/silero_vad_16k_op15.stage.onnx
```

### 8.2 ONNX 清单提取

- `tools/silero_vad/extract_onnx_manifest.py`

作用：

- 导出 ONNX 图结构和张量元信息

### 8.3 重建所需权重提取

- `tools/silero_vad/extract_reconstruction_tensors.py`

作用：

- 从官方 ONNX 中提取重建所需张量
- 包括 decoder LSTM 的关键参数

### 8.4 TensorFlow 重建 + TFLite 导出

- `tools/silero_vad/rebuild_tf_silero_vad.py`

作用：

- 用提取出来的权重在 TensorFlow 中重建 `Silero VAD`
- 输出 batch=1 的 `TFLite`
- 同时可对照 ONNXRuntime 做数值验证

### 8.5 生成固件内嵌模型数据

- `tools/silero_vad/generate_model_data.py`

作用：

- 把 `.tflite` 转成可编译进固件的 `.h/.cc`

## 9. 当前实际生成出的模型产物

### 9.1 TFLite 文件

- 路径：
  - `third_party/silero_vad/generated/silero_vad_16k_b1_fp32.tflite`
- sha256：
  - `5a532943646b1dd71930fb02e26e0600ba97ee80990302726294aef8a3142a05`
- 大小：
  - `1248388 bytes`

### 9.2 数值验证结果

项目已经做过 ONNX 对 TensorFlow / TFLite 的数值对齐验证：

- output max abs diff：
  - `1.5599653124809265e-08`
- state max abs diff：
  - `1.6689300537109375e-06`

这说明：

- 当前导出的 `TFLite` 不是“差不多像”，而是数值上已经非常接近官方 ONNX。

### 9.3 固件内嵌模型文件

生成后的固件内嵌代码位于：

- `components/river_voice/generated/river_silero_vad_model_data.h`
- `components/river_voice/generated/river_silero_vad_model_data.cc`

## 10. 板端接入位置

### 10.1 detector 边界

对外统一接口：

- `include/river/river_voice_detector.h`

这层的意义非常重要：

- 当前后端是 `Silero`
- 后续可以换成自研 `TFLite` 模型
- 或者换成别的 VAD 后端
- 上层无需改业务逻辑

### 10.2 Silero 运行时实现

核心实现文件：

- `components/river_voice/river_voice_detector_silero.cc`

当前实现负责：

- `TFLite Micro` 初始化
- 模型输入输出绑定
- recurrent state 维护
- `256 -> 512 + 64 context` 的滑窗组装
- `q15` 概率输出
- `speech/silence` 判决

### 10.3 纯 VAD 验证路径

当前默认验证路径在：

- `components/river_voice/river_voice_vad_probe.c`

当前逻辑：

1. 采集双麦原始 PCM
2. 走 `aivoice_afe`
3. 把增强后的单通道 PCM 同时送入：
   - `Silero VAD`
   - SDK `aivoice VAD` 参考链
4. 输出更高频率的诊断日志
5. 把语音段写入 `segment_buffer`

### 10.4 段缓存

段缓存模块：

- `include/river/river_voice_segment_buffer.h`
- `components/river_voice/river_voice_segment_buffer.c`

作用：

- 在 `speech` 开始前保留一段缓存
- 在 `speech` 结束后继续保留一段尾巴
- 为后续在线 ASR 提供更完整的语音片段

### 10.5 在线 ASR 交接 stub

当前还新增了一个边界模块：

- `include/river/river_voice_segment_sink.h`
- `components/river_voice/river_voice_segment_sink.c`

当前作用不是做真实上云，而是先把接口抽出来：

- `vad_probe` 不再把“segment ready 后如何处理”写死在自身内部
- 当前只把 ready segment 提交到 `online_asr_stub`
- 后面接入真实在线 ASR 时，只需要替换 sink 实现

## 11. 迁移过程中遇到的关键问题与修复

这一节是审核和复现最有价值的部分。

### 11.1 官方 ONNX 被转换工具改坏

现象：

- 某些工具链会把仓库里的官方 ONNX 原地改写

修复：

- 恢复 vendored ONNX
- 强制引入 staging 脚本
- 以后只对临时副本操作

### 11.2 直接 onnx2tf 不稳定

现象：

- 在 `STFT` 和 `decoder` 节点失败

修复：

- 放弃“强行直接转”
- 改成 TensorFlow 重建路线

### 11.3 `MicroMutableOpResolver` 生命周期问题

现象：

- 板端早期在 resolver 初始化时直接崩溃

根因：

- C 结构体经 `zmalloc` 分配，但其中的 C++ 对象没有正确构造

修复：

- 使用 placement new / 显式析构

### 11.4 `TFLite Micro` 的 tensor 元数据异常

现象：

- `dims/name/data/type` 在 `RTL8730E SDK` 这一版 `TFLM` 上不总是可靠

出现过的情况包括：

- `data = NULL`
- `type = kTfLiteNoType`
- `dims/name` 不可依赖

修复策略：

1. 先以 persistent tensor 为主做校验。
2. eval tensor 只做 best-effort 诊断。
3. 必要时对 `data` 指针做 fallback patch。
4. 对 `kTfLiteNoType` 做兼容处理。

### 11.5 模型镜像导致 app bin 变大

现象：

- 引入 `Silero` 后，`km0_km4_ca32_app.bin` 超过 SDK 默认 NOR 下载范围

修复：

- 在项目内复制并维护开发期 NOR profile
- 新增：
  - `board/rtl8730e/profiles/RTL8730E_NOR.json`
  - `board/rtl8730e/profiles/RTL8730E_NOR.rdev`
  - `tools/river_flash.py`

注意：

- 这是开发期 profile
- 不是量产最终分区方案

## 12. 当前运行参数

截至当前版本，纯 VAD 探针的关键参数是：

- `Silero enter_q15 = 9000`
- `Silero exit_q15 = 2500`
- `hangover = 10`
- `ema_shift = 1`
- `diagnostic window ~= 96ms`
- `segment pre-roll = 384ms`
- `segment post-roll = 768ms`
- `segment max = 8000ms`

设计目标是：

- 宁可多抓一点，也不要漏掉短句
- 为后续在线 ASR 留出更完整的前后语音上下文

## 13. 如何完整复现迁移

### 13.1 准备环境

```bash
cd /root/ameba-river
python3.10 -m venv .venv-silero-convert
source .venv-silero-convert/bin/activate
```

安装依赖：

```bash
pip install --upgrade \
  pip setuptools wheel \
  onnx==1.17.0 onnxruntime==1.20.1 onnxsim==0.4.36 onnxoptimizer==0.3.13 \
  onnx-graphsurgeon==0.5.8 sng4onnx==1.0.4 \
  tensorflow-cpu==2.19.0 tensorflow==2.19.1 tf_keras==2.19.0 \
  onnx2tf==1.28.3 ai_edge_litert==1.2.0 \
  psutil==6.1.1 h5py==3.12.1 protobuf==5.29.3 flatbuffers==25.1.24 ml_dtypes==0.5.1
```

### 13.2 校验上游文件

确认：

- `third_party/silero_vad/upstream/silero_vad_16k_op15.onnx`
- sha256 是否为：
  - `7ed98ddbad84ccac4cd0aeb3099049280713df825c610a8ed34543318f1b2c49`

### 13.3 导出重建所需信息

```bash
source .venv-silero-convert/bin/activate
python tools/silero_vad/extract_reconstruction_tensors.py \
  --input third_party/silero_vad/upstream/silero_vad_16k_op15.onnx \
  --output third_party/silero_vad/upstream/silero_vad_16k_op15_reconstruction_manifest.json
```

### 13.4 重建 TensorFlow / 导出 TFLite

```bash
source .venv-silero-convert/bin/activate
python tools/silero_vad/rebuild_tf_silero_vad.py \
  --input third_party/silero_vad/upstream/silero_vad_16k_op15.onnx \
  --verification-output third_party/silero_vad/generated/silero_vad_16k_b1_fp32_verification.json \
  --tflite-output third_party/silero_vad/generated/silero_vad_16k_b1_fp32.tflite
```

### 13.5 生成固件内嵌模型

```bash
source .venv-silero-convert/bin/activate
python tools/silero_vad/generate_model_data.py \
  --input third_party/silero_vad/generated/silero_vad_16k_b1_fp32.tflite \
  --header components/river_voice/generated/river_silero_vad_model_data.h \
  --source components/river_voice/generated/river_silero_vad_model_data.cc \
  --symbol river_silero_vad_model_data
```

### 13.6 构建固件

```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py soc RTL8730E
CCACHE_DISABLE=1 python3 /root/ameba-rtos-1.2/ameba.py build -p
```

### 13.7 烧录

```bash
cd /root/ameba-river
source env.sh
python3 tools/river_flash.py -p /dev/ttyUSB0
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

## 14. 如何审核迁移是否可信

建议审核时至少检查以下项目：

1. 上游模型是否来自官方仓库，hash 是否固定。
2. 生成的 `.tflite` 是否有数值验证报告。
3. 固件内嵌模型是否来自项目内生成脚本，而不是手工塞进去。
4. `river_voice_detector_silero.cc` 是否仍然保持 detector 边界清晰，没有把模型逻辑散到别处。
5. `river_voice_vad_probe.c` 是否只是验证路径，不把在线 ASR 业务逻辑写死在 probe 里。
6. `river_voice_segment_buffer.*` 是否仍然作为通用段缓存模块存在，而不是为某个单一模型写死。
7. 开发期 `.rdev` / flash profile 是否仍明确标注为开发用途。

## 15. 当前风险与下一步建议

当前主要风险不再是“模型能不能跑起来”，而是：

1. `Silero` 阈值仍需继续收敛。
2. `AIVoice AFE` 的参数与后续在线 ASR 的实际目标还需要联调。
3. 现在的纯 VAD 探针还没有真正把 ready segment 送到在线 ASR。
4. Flash / heap 已经比较紧张，后续接在线 ASR 前必须继续控制镜像增长。

推荐下一步顺序：

1. 固化 `Silero VAD + segment buffer` 当前版本。
2. 把 ready segment 对接到在线 ASR 上行层。
3. 加入 segment 级时间戳和序号。
4. 再评估是否需要量化 / 剪枝。

## 16. 当前结论

这次迁移已经完成了最关键的“从官方模型到目标板可运行”的闭环，并且保留了良好的替换边界：

- `AFE` 可替换
- `VAD` 可替换
- `segment buffer` 可复用
- 后续在线 ASR 可以直接接入

因此，这不是一次“临时把模型跑起来”的实验，而是已经具备持续演进基础的正式迁移版本。
