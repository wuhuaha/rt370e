# KWS 板端与本机对拍调试指南

日期：2026-04-07

## 1. 目的

这份文档服务一个明确目标：

- 让后续同事能够快速复用现有的板端 vs 本机对拍机制
- 在不破坏既有调试链路的前提下，判断“端侧模型部署是否正确”
- 把模型问题和部署 / 适配问题分离开

这份文档不讨论训练策略本身，也不替代算法评估报告。它只回答：

- 当前板端到底喂了什么给模型
- 当前板端输出和本机同一模型是否一致
- 若不一致，偏差最可能出在哪一层

## 2. 基本原则

后续所有唤醒词模型调试，默认遵守下面几条：

- 不移除、不绕过、不弱化现有板端 / 本机对拍能力
- 不为了“先跑起来”而删除 tensor dump、align replay、feature/input/output 对比等钩子
- 不在 exact parity 未通过前讨论阈值、误唤醒率、hard negative 或训练质量
- 不随意改串口调试流程；串口只是采集载体，不是问题归因对象

这几条的根本原因是：

- 只有先证明部署链路正确，后续观察到的分数、触发与漏检才有资格被解释为“模型本身问题”

## 3. 这套机制到底在证明什么

现有对拍链路的核心不是“多打一堆日志”，而是把板端推理拆成三个可验证层次：

1. `feat_f32`
   - 板端前端最终产出的 `float32` 特征张量
   - 用于回答“前端是否已经偏了”
2. `input_raw`
   - 真正喂给 TFLite Micro 输入 tensor 的原始字节
   - 用于回答“量化 / 拷贝 / 转置 / dtype 适配是否偏了”
3. `output_raw`
   - 板端从输出 tensor 直接读出的原始字节
   - 用于回答“板端解释器 / kernel / 读出路径是否偏了”

只要这三层能被 host 侧复现，就能把部署问题收敛到非常小的范围。

## 4. 代码与工具构成

### 4.1 板端命令入口

见：

- [components/river_diag/river_diag_cmd.c](../components/river_diag/river_diag_cmd.c)

当前 `river kws` 调试相关子命令包括：

- `river kws status`
- `river kws debug local <on|off|status>`
- `river kws dump <next|off|clear|status|meta|chunk ...>`
- `river kws align <run|status>`

### 4.2 板端 KWS 对拍实现

见：

- [components/river_voice/river_voice_kws.cc](../components/river_voice/river_voice_kws.cc)

重点机制：

- `river kws debug local on`
  - 打开本地调试模式
  - 保留本地 KWS 检测，但避免云端 handoff 污染 KWS 调试窗口
- `river kws align run`
  - 回放编译进固件的标准 PCM 样本
  - 自动拉起本地调试模式
  - 自动 arm tensor dump
  - 自动打印一次完整的 `begin/meta/chunk/end`
- `river kws dump next`
  - arm 下一次真实推理的快照采集
  - 适合 live speech / 真实误唤醒问题

### 4.3 Host 回放工具

见：

- [tools/kws/replay_board_tensor_dump.py](../tools/kws/replay_board_tensor_dump.py)

这个脚本负责：

- 从串口日志中解析一次完整 tensor dump
- 还原 `feat_f32` / `input_raw` / `output_raw`
- 用 host 上的同一份 `.tflite` 重放输入
- 输出 feature/input/output parity 结论

### 4.4 模型嵌入与算法导出对应关系

板端嵌入模型头文件一般位于：

- `components/river_voice/generated/*_model_data.h`

算法导出产物一般位于：

- `/root/kws-trainint/artifacts/exports/<model_name>/`
- 或 `/root/kws-training-pro/models/<model_name>/`

调试前必须先确认：

- 板端头文件里的字节流和算法导出的目标 `.tflite` 是同一个文件

否则对拍本身没有意义。

## 5. 两条标准调试路径

当前推荐只保留两条主路径。

### 5.1 路径 A：编译样本对拍

适用场景：

- 新模型第一次上板
- 新前端适配第一次联调
- 需要先证明“部署链路正确”

特点：

- 输入是编译进固件的固定 PCM 样本
- 结果可重复
- 最适合做第一轮 exact parity

推荐度：

- 最高

### 5.2 路径 B：真实语音 / 误唤醒现场对拍

适用场景：

- 编译样本对拍已经通过
- 需要分析真实误唤醒、漏唤醒、live speech 触发异常

特点：

- 用 `river kws dump next` 抓下一次真实推理
- 更贴近真实场景
- 但可重复性差，容易被 VAD、交互状态和串口时序干扰

推荐度：

- 只能在路径 A 已通过后再用

## 6. 上手前准备

### 6.1 先确认板上跑的是哪一个模型

至少要确认：

- 当前分支与提交
- 当前 `prj.conf` 选择的模型 variant
- 当前固件是否已经重新编译并刷板

对 student FP32 debug 变体，当前板端代码入口见：

- [components/river_voice/river_voice_kws.cc](../components/river_voice/river_voice_kws.cc)

其中会显式包含：

- [components/river_voice/generated/student_bc_resnet_tiny_v2_fp32_model_data.h](../components/river_voice/generated/student_bc_resnet_tiny_v2_fp32_model_data.h)

### 6.2 先确认 host 侧拿的是同一个 `.tflite`

推荐先做一次字节级确认。示例：

```bash
cd /root/ameba-river
python3 - <<'PY'
from pathlib import Path
import hashlib, re

header = Path('components/river_voice/generated/student_bc_resnet_tiny_v2_fp32_model_data.h').read_text()
data = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', header))
model = Path('/root/kws-trainint/artifacts/exports/student_bc_resnet_tiny_v2/model.fp32.tflite').read_bytes()

print('header_bytes', len(data))
print('header_sha256', hashlib.sha256(data).hexdigest())
print('model_bytes', len(model))
print('model_sha256', hashlib.sha256(model).hexdigest())
print('exact_match', 'yes' if data == model else 'no')
PY
```

如果 `exact_match != yes`，禁止继续做 parity 结论。

### 6.3 先确认板端处于可对拍状态

执行：

```text
river kws align status
```

重点看：

- `kws=ready`
- `probe=...`
- `interaction=...`
- `detection=ready`
- `worker=idle`
- `snapshot=empty|ready`
- `local_only=yes|no`

如果看到：

- `kws=closed`
  - 先查 boot log 里的 KWS init 失败原因
- `probe=running`
  - 对 `align run` 必须先停 probe
- `interaction != wake_monitoring`
  - 说明当前不在 idle 监听窗口，不能做标准对拍

## 7. 标准步骤：编译样本对拍

这是后续同事最应该优先掌握的一套流程。

### 7.1 板端操作

建议按下面顺序执行：

```text
river kws debug local on
river audio probe stop
river kws align status
river kws align run
river audio probe start
```

必要时恢复默认：

```text
river kws debug local off
```

### 7.2 预期关键日志

成功路径中，通常会出现：

```text
kws align guard: kws=ready ...
kws tensor dump armed: mode=align_best
kws align replay start: source=compiled_pcm frames=...
kws align replay captured: seq=... infer=... score=... q15=...
kws tensor dump begin: ...
kws tensor dump meta: ...
kws tensor dump feat_f32: ...
kws tensor dump input_raw: ...
kws tensor dump output_raw: ...
kws tensor dump end: ...
kws align replay done: dump=emitted ...
```

这些日志分别在证明：

- `align guard`
  - 当前环境是否允许开始对拍
- `replay start`
  - 编译样本已开始送入 KWS worker
- `captured`
  - 已经抓到一份快照
- `begin/meta/chunk/end`
  - 这次快照足以被 host 回放

### 7.3 若 `align run` 失败，先按日志分流

常见失败原因：

- `kws align requires probe stopped`
  - 说明没有先 `river audio probe stop`
- `kws align requires idle wake monitoring`
  - 当前处于 speaking / asr / follow_up 等交互状态
- `kws align lazy init failed`
  - KWS runtime 根本没起来，先回 boot log 排查
- `kws align snapshot missing`
  - 这次 replay 没触发到有效快照，先看阈值 / 输入样本 / worker 状态

## 8. 标准步骤：把串口日志交给 host 回放

### 8.1 最低要求

至少需要拿到一次完整的：

- `kws tensor dump begin`
- `kws tensor dump meta`
- `feat_f32` 全部 chunk
- `output_raw` 全部 chunk

对于大多数情况，还希望拿到：

- `input_raw` 全部 chunk

### 8.2 为什么 `input_raw` 有时会不完整

实际串口转录中，最容易损坏的是长行十六进制数据：

- monitor 换行
- 终端 wrap
- 日志与命令交错
- 复制时截断

这不等于板端输入真的错了，只说明“串口转录件不完美”。

### 8.3 当前 host 脚本对 `float32` 调试模型的容错

当前 [tools/kws/replay_board_tensor_dump.py](../tools/kws/replay_board_tensor_dump.py)
已经支持下面这类场景：

- `feat_f32` 完整
- `output_raw` 完整
- `input_raw` 因串口 wrap 缺失 / 坏掉部分 chunk
- 但 `meta` 中记录的 `input_hash` 与 `feat_f32` 原始字节 hash 一致

这时脚本会自动回退为：

- 用 `feat_f32` 的原始 `float32` 字节作为 effective input

这只适用于板端实际输入本来就是 `float32` 特征字节的调试路径。

### 8.4 host 回放命令

示例：

```bash
cd /root/ameba-river
python3 tools/kws/replay_board_tensor_dump.py \
  --log /tmp/kws_student_fp32_debug_replay.clean.log \
  --model /root/kws-trainint/artifacts/exports/student_bc_resnet_tiny_v2/model.fp32.tflite \
  --seq latest \
  --builtin-ref
```

说明：

- 对 `FP32` 调试模型，推荐显式加 `--builtin-ref`
- 原因不是板端输出错了，而是 host 侧 TensorFlow 默认 delegate 可能引入最低有效位漂移
- exact parity 需要优先对齐：
  - `feature hash`
  - `effective_input hash`
  - `output_raw` 原始字节
- 若不加 `--builtin-ref` 时只看到：
  - `output_parity: bytes_equal=no`
  - 但 `raw_equal=yes`
  - 且 `exact` 基本一致
- 这优先解释为 host delegate 数值路径差异，不要直接判成板端部署错误

### 8.5 重点输出项怎么读

脚本输出会包含：

- `board_hash`
- `host_hash`
- `quant_parity`
- `board_output`
- `host_output`
- `output_parity`

其中最关键的是：

- `feature hash`
- `effective_input hash`
- `output_parity`

对于 `float32` 输出，还要同时看：

- `score`
- `exact`

说明：

- `score` 是为了统一口径而输出的展示值
- `exact` 才是 `float32` 原始输出真正对应的数值

## 9. 怎么根据结果判定问题层次

### 9.1 板端与 host 输出字节完全一致

典型表现：

- `output_parity: bytes_equal=yes raw_equal=yes`

结论：

- 当前部署链路正确
- 板端模型嵌入、输入适配、TFLM 推理和输出读出在该样本上无误
- 后续可以转入阈值、误唤醒样本、训练区分度等分析

### 9.2 `feat_f32` 对得上，但 `input_raw` 对不上

结论优先指向：

- 量化参数错
- 拷贝路径错
- NHWC / NCHW / 帧维顺序错
- 输入 dtype 解释错

也就是：

- 前端大概率没错
- 错在“前端输出到模型输入”这一步

### 9.3 `input_raw` 对得上，但 `output_raw` 对不上

结论优先指向：

- 模型文件不是同一份
- 板端 resolver / kernel 行为不一致
- 板端输出 tensor 读取或解释方式有问题

这时不要再讨论阈值。

### 9.4 `score` 看起来不同，但 `output_raw` 字节相同

这在 `float32` 调试里很常见。

例如：

- board `score=0.371203`
- host `score=0.371000`
- 但 `exact=0.371203`
- 且 `bytes_equal=yes`

这不构成不一致。它只是：

- board 直接打印了原始浮点
- host 额外打印了 `raw/1000` 的展示值

真正的仲裁标准是：

- `exact`
- `output_raw`
- `bytes_equal`

## 10. 推荐的结论模板

后续同事在转达给算法或平台同事时，建议只按下面模板汇报：

### 10.1 部署正确

- 板端嵌入模型与算法导出模型已做字节级确认，为同一文件
- 板端 `feat_f32` / `input_raw` / `output_raw` 已采集
- host 用同一 `.tflite` 回放后，`output_raw` 与板端完全一致
- 结论：当前问题不是端侧部署错误，转入模型行为分析

### 10.2 适配错误

- `feat_f32` 一致
- `input_raw` 不一致
- 结论：前端到输入 tensor 的适配有误，先修部署链路

### 10.3 运行时错误

- `input_raw` 一致
- `output_raw` 不一致
- 结论：板端运行时 / kernel / 模型文件一致性存在问题

## 11. 常见坑

### 11.1 直接拿 live speech 结论替代编译样本对拍

错误做法：

- 一上来就抓真实语音
- 一边看 VAD，一边看网络，一边看阈值

正确做法：

- 先用 `align run` 把部署正确性证明掉

### 11.2 没确认板端和 host 是同一份模型

这是最常见的无效对拍原因之一。

### 11.3 串口 transcript 坏了就误判为板端输入错误

串口转录件坏掉，不代表板端实际输入错了。

尤其在 `float32` debug 模型下，如果：

- `input_hash == feat_f32_raw_hash`

那通常说明：

- 板端实际喂入的就是 `feat_f32` 原始字节

### 11.4 未通过 parity 就讨论阈值

这会把问题方向彻底带偏。

### 11.5 调试时把既有钩子删掉

不要做下面这些事：

- 删除 `align run`
- 删除 tensor dump
- 删除 `debug local`
- 删除 host replay
- 改成“先凭感觉听板子反应”

这些做法会把后续所有模型调试重新拉回不可证伪状态。

## 12. 当前已验证的参考案例

以 `student_bc_resnet_tiny_v2 FP32 debug` 为例，当前已经验证过：

- 板端嵌入头文件与算法导出 `.tflite` 的 `sha256` 一致
- 一次编译样本对拍得到：
  - `feature_hash=0x7ce0b11d`
  - `effective_input_hash=0xd52f011c`
  - board `exact=0.371203`
  - host `exact=0.371203`
  - `output_parity: bytes_equal=yes raw_equal=yes`

这组结果的意义是：

- 当前 student FP32 debug 变体在该样本上的端侧部署已经正确
- 后续若继续看到误唤醒 / 置信度异常，应优先从模型行为、阈值与真实样本分布解释，而不是回头怀疑“是不是板端没部署对”

## 13. 后续建议

后续每个新模型上板，建议固定遵循下面顺序：

1. 先确认板端头文件与算法导出模型是同一文件
2. 跑一次 `align run`
3. 做一次 host replay exact parity
4. parity 通过后，再抓 live speech / false wake dump
5. 最后才讨论阈值与训练优化

如果这五步里第 1 到第 3 步没通过，就不要再往后推进。
