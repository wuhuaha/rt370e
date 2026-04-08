# 2026-04-08 Student DS-CNN Tiny FP32 Debug 运行资源画像

Date: 2026-04-08

## 1. 范围与结论

本文记录 `student_dscnn_tiny_v2_fp32_debug` 在 `RTL8730E` 板端的首次正式 bring-up、板端 / 本机对拍与资源测量结果，目标是同时回答四件事：

- 当前板端接入的是否就是算法交付的 DS-CNN tiny FP32 bundle
- 当前 `40x101 / float32 -> float32` 部署链路是否正确
- 当前资源与实时性是否值得继续扩展到同系列模型
- 在现有 bundle 中，接下来哪些模型还值得直接继续试板

本次固件配置来自：

- [prj.conf](../prj.conf)
- `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_DSCNN_TINY_V2_FP32_DEBUG=y`
- `CONFIG_RIVER_KWS_TENSOR_ARENA_KB=2048`
- `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=9125`

结论先给出：

- `student_dscnn_tiny_v2_fp32_debug` 已正确接入当前板端并成功初始化，boot log 明确确认：
  - `variant=student_dscnn_tiny_v2_fp32_debug`
  - `runtime_in=float32 runtime_out=float32`
  - `dims=[1,40,101,1]`
  - `fft=400 hop=160 center=yes`
- 板端嵌入模型与算法 bundle 的 FP32 `.tflite` 字节完全一致：
  - `sha256=836b18e8c315c9142f5744bfa0183a35e4d011019c2526e33f86dd3da166c7c7`
- 保留既有 `river kws align run` 对拍机制后，板端与 host 已证明部署正确：
  - `feature hash` 一致
  - `effective input hash` 一致
  - 板端与 host 都得到 `raw=297 q15=9723 score=0.296720`
  - 当前仍存在 `float32` 输出最低字节级别差异，表现为 `bytes_equal=no raw_equal=yes`
  - 这更像 host / board 浮点执行路径的最低有效位漂移，不是部署接线错误
- 当前 DS-CNN tiny FP32 明显快于此前的 BC-ResNet tiny / nano FP32：
  - 本次 `align` 样本 `infer_us[last=183988 avg=183969 max=183988]`
  - 约等于 `183.969 ms`
  - 相比 `student_bc_resnet_nano_v2_fp32_debug` 的 `277.865 ms` 更快
- 但它仍远高于 bundle 声明的 `cpu_peak_budget_ms=18.0`：
  - 当前约为预算的 `10.22x`
- 结合 `stride=16`，当前 debug 配置下仍能跑完一次推理再等下一次窗口，所以它是一个“值得继续试板的 bring-up 候选”，但还不是实时量产方案。

## 2. 测量方法

### 2.1 固件编译与刷板

```bash
cd /root/ameba-river
source env.sh >/dev/null
python3 /root/ameba-rtos-1.2/ameba.py build -p
python3 tools/river_flash.py -p /dev/ttyUSB0
```

### 2.2 串口抓取

本次为了拿到完整 boot + align dump，使用只读串口抓取，不改已有串口调试命令：

```bash
python3 - <<'PY'
import serial
from pathlib import Path

log = Path('/tmp/kws_dscnn_tiny_fp32_debug.log')
with serial.Serial('/dev/ttyUSB0', 1500000, timeout=1) as ser, log.open('wb') as f:
    while True:
        data = ser.read(4096)
        if data:
            f.write(data)
            f.flush()
PY
```

若抓取晚于 boot，补一条：

```bash
bash -lc "printf 'reboot\r' > /dev/ttyUSB0"
```

### 2.3 板端 / 本机对拍流程

完全复用既有调试链路：

```text
river kws debug local on
river audio probe stop
river kws align status
river kws align run
river audio probe start
river kws debug local off
```

Host replay 命令：

```bash
cd /root/ameba-river
TF_ENABLE_ONEDNN_OPTS=0 \
OMP_NUM_THREADS=1 \
TF_NUM_INTRAOP_THREADS=1 \
TF_NUM_INTEROP_THREADS=1 \
python3 tools/kws/replay_board_tensor_dump.py \
  --log /tmp/kws_dscnn_tiny_fp32_debug.log \
  --model /root/kws-trainint/artifacts/exports/student_dscnn_tiny_v2/model.fp32.tflite \
  --seq latest \
  --builtin-ref
```

## 3. 当前固件契约

| 项目 | 当前板端实测 |
| --- | --- |
| 变体 | `student_dscnn_tiny_v2_fp32_debug` |
| 模型大小 | `12376 B` |
| runtime 输入/输出 | `float32 -> float32` |
| 输入 shape | `[1,40,101,1]` |
| 输出 shape | `[1,1,1,1]` |
| layout | `mels_frames / NHWC` |
| frontend | `40 mel`, `101 frames`, `fft=400`, `hop=160`, `center=yes` |
| 归一化 | `per_clip_mean_std` |
| arena 配置 | `2048 KB` |
| arena 实际使用 | `1168336 B` |
| arena slack | `928816 B` |
| stride | `16` 帧 |
| gate | `vad` |
| pre-roll | `320 ms` |
| pre-roll flush | `16` 帧 |
| queue | `64` 帧 |
| 当前阈值 | `threshold_q15=9125` |
| 唤醒词 | `小欧管家` |

## 4. 与 bundle 交付的匹配情况

参考：

- `/root/kws-trainint/artifacts/exports/student_dscnn_tiny_v2/board_runbook.md`
- `/root/kws-trainint/artifacts/exports/student_dscnn_tiny_v2/deployment_summary.json`

### 4.1 模型文件一致性

| 项目 | 数值 |
| --- | --- |
| 板端 header 字节数 | `12376` |
| host `.tflite` 字节数 | `12376` |
| header sha256 | `836b18e8c315c9142f5744bfa0183a35e4d011019c2526e33f86dd3da166c7c7` |
| model sha256 | `836b18e8c315c9142f5744bfa0183a35e4d011019c2526e33f86dd3da166c7c7` |
| exact match | `yes` |

结论：

- 当前板端嵌入模型与算法交付 FP32 模型是同一份文件
- 因此后续 parity 与性能结论具备有效前提

### 4.2 预算对照

| 项目 | bundle 声明/参考 | 当前板端实测 | 结论 |
| --- | ---: | ---: | --- |
| family / variant | `ds_cnn / tiny` | `ds_cnn / tiny` | 已对齐 |
| 参数量 | `1597` | `1597` | 已对齐 |
| 输入契约 | `1x40x101x1` | `1x40x101x1` | 已对齐 |
| 默认阈值 | `q15=9125` | `q15=9125` | 已对齐 |
| 模型大小 | `12376 B` | `12376 B` | 已对齐 |
| memory budget | `256 KB` | `1168336 B` (`1141.0 KiB`) | `4.46x` 超预算 |
| cpu peak budget | `18.0 ms` | `183.969 ms` | `10.22x` 超预算 |
| board recall ref | `0.571429` | 参考值 | 偏低 |
| board FA/h ref | `992.647059` | 参考值 | 偏高 |

说明：

- 这里对照的是 bundle 自带预算，不是当前板端最大可分配内存
- 当前板端是“已证明可接入、可对拍、可触发”，但离 bundle 目标资源位仍然较远

## 5. Boot / Init 资源画像

### 5.1 KWS 初始化阶段

| 指标 | 数值 |
| --- | ---: |
| init 前 heap | `14378048 B` |
| `arena_ready` 后 heap | `12276352 B` |
| `task_ready` 后 heap | `12219456 B` |
| init 后 `boot_ready` heap_free | `11832576 B` |
| init 后 `boot_ready` heap_min | `11832576 B` |
| ctx | `26272 B` |
| pre-roll buffer | `10240 B` |
| queue buffer | `33024 B` |
| dump lazy buffer | `64644 B` |
| input tensor bytes | `16160 B` |
| output tensor bytes | `4 B` |

### 5.2 arena 观察

boot log 关键行：

```text
kws init plan: heap_free=14378048 ctx=26272B arena=2048KB model=12376B align=32
kws alloc: ... input_bytes=16160 output_bytes=4 arena_used=1168336B arena_slack=928816B
```

说明：

- 这个 tiny FP32 模型虽然 `.tflite` 只有 `12 KB` 级别，但在当前 TFLM runtime 下仍消耗了约 `1.12 MB` arena
- 这也是它目前只能作为 debug bring-up 候选、不能按 bundle 预算直接接受的重要原因

## 6. 对拍结果

### 6.1 板端 `align` 捕获

板端日志：

```text
kws align sample: source=compiled_pcm frame_samples=256 frames=145 duration_ms=2320 pre_silence_frames=20 tail_silence_frames=4
kws align guard: kws=ready probe=stopped interaction=wake_monitoring detection=ready worker=idle snapshot=empty local_only=yes
kws diag: infer=1 ... raw=236 score=0.235520 q15=7717 ...
kws diag: infer=2 ... raw=297 score=0.296720 q15=9723 ...
wakeword hit: text=小欧管家 score_pm=296 q15=9723 triggers=1 cooldown_ms=1800 mode=threshold
kws align replay captured: seq=1 infer=2 score=0.296720 q15=9723
```

关键结果：

| 指标 | 数值 |
| --- | ---: |
| infer 序号 | `2` |
| raw | `297` |
| score | `0.296720` |
| q15 | `9723` |
| infer_us[last] | `183988` |
| infer_us[avg] | `183969` |
| infer_us[max] | `183988` |
| queue 峰值 | `13/64` |
| 是否触发唤醒 | `是` |

### 6.2 Host replay 结果

Host replay 输出：

```text
note: using feature tensor bytes as effective input because they match the board input hash
board_hash: feature=0xf6cf59f0 input=0x8aa04513
host_hash: feature=0xf6cf59f0 logged_input=n/a effective_input=0x8aa04513 source=feat_f32_missing_input_raw
quant_parity: unavailable (input_raw incomplete: parsed=251/253)
board_output: raw=297 score=0.296720 exact=0.296720 q15=9723
host_output: raw=297 score=0.297000 exact=0.296720
output_parity: bytes_equal=no raw_equal=yes first_diff=[0]
```

解释如下：

- 这次串口大 dump 中，`input_raw` 缺了 `2` 个 chunk：`146`, `147`
- 但 `feat_f32` 的字节重建后，计算出的 `effective_input hash` 与板端 `input_hash` 完全一致
- 因而可以确认板端真正送入解释器的输入张量与 host 重放输入一致
- 板端与 host 的输出 raw 标量一致，都是 `297`
- 板端与 host 解码到的概率一致到当前展示精度，都是 `0.296720`
- 唯一残留差异是 `float32` 输出字节第一个字节不同，这更像最低有效位的浮点路径差异

结论：

- 当前 DS-CNN tiny FP32 的部署链路判断为“正确”
- 这次对拍已经足够把问题归因从“适配错误”移开
- 如果后续继续看到误唤醒 / 漏唤醒 / 分数异常，应优先归因模型本身和算子成本，而不是板端接线错误

## 7. 实时性分析

### 7.1 当前实测

| 指标 | 数值 |
| --- | ---: |
| 单次推理平均耗时 | `183.969 ms` |
| bundle CPU 预算 | `18.0 ms` |
| 超预算倍数 | `10.22x` |
| 当前 stride | `16` 帧 |
| 当前输入帧周期 | `16 ms` |
| 当前推理触发周期 | `256 ms` |
| 触发周期剩余裕量 | 约 `72 ms` |

### 7.2 解释

- 这说明它虽然远没达到 bundle 宣称的 `18 ms` 量级，但在当前 `stride=16` 的 debug 配置下，板端还没有被单次推理彻底压垮
- 这也是它比先前 FP32 student 方案更值得继续试板的核心原因：
  - `student_bc_resnet_tiny_v2_fp32_debug` 约 `675 ms`
  - `student_bc_resnet_nano_v2_fp32_debug` 约 `278 ms`
  - `student_dscnn_tiny_v2_fp32_debug` 约 `184 ms`

因此当前判断是：

- 作为部署正确性 / 端到端链路 bring-up 候选：`值得继续`
- 作为当前 `RTL8730E` 的最终实时部署方案：`不通过`

## 8. 后续模型试板优先级

基于当前 bundle 盘点和本次结果，后续可直接继续尝试的模型建议如下。

### 8.1 第一优先级

- `student_dscnn_small_v2`
  - 与当前 tiny 同 family，前端契约仍是 `40x101`
  - 同样只要求补 `MUL`
  - FP32 模型体积仅 `23600 B`
  - 如果 tiny 的 bring-up 与对拍已经稳定，small 是最自然的下一块放大试验

### 8.2 第二优先级

- `student_bc_resnet_tiny_clip_v2`
- `student_bc_resnet_tiny_hard_only_v2`

原因：

- 它们仍沿用当前已经打通过的 student `40x101 + MUL` 路线
- 但从已知 BC-ResNet nano / tiny 结果看，实时性大概率仍会明显差于 DS-CNN tiny
- 因此更适合作为“模型质量路线对照”，而不是“更轻实时候选”

### 8.3 不建议立刻试的

- `student_bc_resnet_small_v2`

原因：

- FP32 体积已到 `1019756 B`
- bundle 预算本身也放宽到 `40 ms`
- 在当前板端路径上，大概率会同时遭遇更大的 arena 压力和更差的推理时延

## 9. 结论

这次 `student_dscnn_tiny_v2_fp32_debug` 的价值很明确：

- 它已经证明当前并行 debug 变体接线是通的
- 它已经证明 DS-CNN 路线在 `RTL8730E` 上明显比此前 BC-ResNet student FP32 更值得继续试
- 它仍没有达到 bundle 的资源和实时预算

因此，当前最合理的下一步不是回头怀疑部署链路，而是：

1. 保留现有 board / host parity 能力不动
2. 以这次 DS-CNN tiny FP32 为基线，继续尝试 `student_dscnn_small_v2`
3. 把后续模型讨论集中到“模型结构 / 算子代价 / 预算真实性”，而不是部署正确性
