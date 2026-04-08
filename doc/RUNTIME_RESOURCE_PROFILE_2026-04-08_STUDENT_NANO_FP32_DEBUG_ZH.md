# 2026-04-08 Student Nano FP32 Debug 运行资源画像

Date: 2026-04-08

## 1. 范围与结论

本文记录 `student_bc_resnet_nano_v2_fp32_debug` 在 `RTL8730E` 板端的首次正式部署验证结果，目标是同时回答三件事：

- 当前板端接入的是否就是算法交付的 nano FP32 bundle
- 板端 `40x101 / float32 -> float32` 部署链路是否正确
- 当前资源与实时性是否值得继续作为上板候选

本次固件配置来自：

- [prj.conf](../prj.conf)
- `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_NANO_V2_FP32_DEBUG=y`
- `CONFIG_RIVER_KWS_TENSOR_ARENA_KB=4096`
- `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=8851`

结论先给出：

- nano FP32 已经正确接入当前板端主调试链路，boot log 明确确认：
  - `variant=student_bc_resnet_nano_v2_fp32_debug`
  - `runtime_in=float32 runtime_out=float32`
  - `dims=[1,40,101,1]`
  - `fft=400 hop=160 center=yes`
- 板端嵌入模型与算法 bundle 的 FP32 `.tflite` 字节完全一致：
  - `sha256=4ff052777e4796db44d5899e94c15eb62c3d24431edcc065d9388671c4df3945`
- 保留既有 `river kws align run` 对拍机制后，板端与 host 已证明部署正确：
  - `feature hash` 一致
  - `input hash` 一致
  - `output_raw` 在 `--builtin-ref` 口径下 `bytes_equal=yes`
- 当前 nano FP32 明显快于之前的 `student_bc_resnet_tiny_v2_fp32_debug`，但仍未达到实时预算：
  - nano 本次 `align` 样本 `infer_us=277865`
  - 约等于 `277.865 ms`
  - bundle 声明 `cpu_peak_budget_ms=24.0`
  - 当前仍约为预算的 `11.58x`
- 因此，这个 nano FP32 版本适合作为部署正确性与模型路线 bring-up 候选，但不适合作为当前 `RTL8730E` 的实时量产方案。

## 2. 测量方法

### 2.1 固件与串口

固件编译与刷板沿用既有流程：

```bash
cd /root/ameba-river
source env.sh >/dev/null
python3 /root/ameba-rtos-1.2/ameba.py build -p
python3 tools/river_flash.py -p /dev/ttyUSB0
```

串口抓取：

```bash
rm -f /tmp/kws_nano_fp32_debug.log
script -q -f /tmp/kws_nano_fp32_debug.log -c "bash -lc 'stty -F /dev/ttyUSB0 1500000 raw -echo; cat /dev/ttyUSB0'"
```

若抓取晚于 boot，补一条：

```bash
bash -lc "printf 'reboot\r' > /dev/ttyUSB0"
```

### 2.2 板端 / 本机对拍流程

本次没有引入新的串口机制，完全复用既有命令：

```text
river kws debug local on
river audio probe stop
river kws align status
river kws align run
river kws debug local off
river audio probe start
```

Host replay 命令：

```bash
cd /root/ameba-river
OMP_NUM_THREADS=1 TF_NUM_INTRAOP_THREADS=1 TF_NUM_INTEROP_THREADS=1 \
python3 tools/kws/replay_board_tensor_dump.py \
  --log /tmp/kws_nano_fp32_debug.log \
  --model /root/kws-trainint/artifacts/exports/student_bc_resnet_nano_v2/model.fp32.tflite \
  --seq latest \
  --builtin-ref
```

说明：

- 对 `FP32` exact parity，当前推荐显式加 `--builtin-ref`
- 默认 host delegate 可能造成最低有效位漂移，表现为：
  - `raw_equal=yes`
  - `exact` 基本一致
  - 但 `bytes_equal=no`
- 这不是板端部署错误，而是 host 侧 delegate 数值路径差异

## 3. 当前固件契约

| 项目 | 当前板端实测 |
| --- | --- |
| 变体 | `student_bc_resnet_nano_v2_fp32_debug` |
| 模型大小 | `108828 B` |
| runtime 输入/输出 | `float32 -> float32` |
| 输入 shape | `[1,40,101,1]` |
| 输出 shape | `[1,1,1,1]` |
| layout | `mels_frames / NHWC` |
| frontend | `40 mel`, `101 frames`, `fft=400`, `hop=160`, `center=yes` |
| 归一化 | `per_clip_mean_std` |
| arena 配置 | `4096 KB` |
| arena 实际使用 | `3139392 B` |
| arena slack | `1054912 B` |
| stride | `16` 帧 |
| gate | `vad` |
| pre-roll | `320 ms` |
| pre-roll flush | `16` 帧 |
| queue | `64` 帧 |
| 当前阈值 | `threshold_q15=8851` |
| 唤醒词 | `小欧管家` |

## 4. 与 bundle 交付的匹配情况

参考：

- `/root/kws-trainint/artifacts/exports/student_bc_resnet_nano_v2/board_runbook.md`
- `/root/kws-trainint/artifacts/exports/student_bc_resnet_nano_v2/deployment_summary.json`

### 4.1 模型文件一致性

| 项目 | 数值 |
| --- | --- |
| 板端 header 字节数 | `108828` |
| host `.tflite` 字节数 | `108828` |
| header sha256 | `4ff052777e4796db44d5899e94c15eb62c3d24431edcc065d9388671c4df3945` |
| model sha256 | `4ff052777e4796db44d5899e94c15eb62c3d24431edcc065d9388671c4df3945` |
| exact match | `yes` |

结论：

- 当前板端嵌入模型与算法交付 FP32 模型是同一份文件
- 因此后续 parity 与性能结论具备有效前提

### 4.2 预算对照

| 项目 | bundle 声明/参考 | 当前板端实测 | 结论 |
| --- | ---: | ---: | --- |
| 输入契约 | `1x40x101x1` | `1x40x101x1` | 已对齐 |
| 默认阈值 | `q15=8851` | `q15=8851` | 已对齐 |
| 模型大小 | `108828 B` | `108828 B` | 已对齐 |
| memory budget | `512 KB` | `3139392 B` (`3065.8 KiB`) | `6.13x` 超预算 |
| cpu peak budget | `24.0 ms` | `277.865 ms` | `11.58x` 超预算 |

说明：

- 这里对照的是 bundle 自带预算，不是当前板端最大可分配内存
- 当前板端是“能跑起来”，但离 bundle 目标资源位仍然较远

## 5. Boot / Init 资源画像

### 5.1 KWS 初始化阶段

| 指标 | 数值 |
| --- | ---: |
| init 前 heap | `14283840 B` |
| `arena_ready` 后 heap | `10084992 B` |
| `task_ready` 后 heap | `10028096 B` |
| init 后 `boot_ready` heap_free | `9641216 B` |
| init 后 `boot_ready` heap_min | `9641216 B` |
| ctx | `26272 B` |
| pre-roll buffer | `10240 B` |
| queue buffer | `33024 B` |
| dump lazy buffer | `64644 B` |
| input tensor bytes | `16160 B` |
| output tensor bytes | `4 B` |

### 5.2 运行态 heap 快照

| 时点 | heap_free | heap_min |
| --- | ---: | ---: |
| `boot_ready` | `9641216 B` | `9641216 B` |
| `wifi_connected` | `9588864 B` | `9584640 B` |
| `align` 推理后 `kws perf` | `9817536 B` | `9584640 B` |

说明：

- `align` 前手动停止了 `audio probe`，因此当次 `kws perf` 的 `heap[now]` 高于常规监听态
- 这符合预期，不应和 live 监听态直接混为一个口径

## 6. 对拍结果

### 6.1 板端 `align` 捕获

板端日志：

```text
kws align replay start: source=compiled_pcm frames=145 emit_dump=yes
kws diag: infer=1 gate=open out_type=float32 raw=363 score=0.363446 q15=11909 ...
kws tensor dump captured: seq=1 infer=1 mode=align_best feat_chunks=253 input_chunks=253 output_chunks=1
kws infer slow: infer=1 us=277865 queue=17/64 gate=open score_pm=363
wakeword hit: text=小欧管家 score_pm=363 q15=11909 triggers=1 cooldown_ms=1800 mode=threshold
kws tensor dump meta: seq=1 feat_hash=0x7ce0b11d input_hash=0xd52f011c raw=363 score=0.363446 q15=11909 ...
```

关键结果：

| 指标 | 数值 |
| --- | ---: |
| infer 序号 | `1` |
| raw | `363` |
| score | `0.363446` |
| q15 | `11909` |
| queue 峰值 | `17/64` |
| infer_us | `277865` |
| 是否触发唤醒 | `是` |

### 6.2 Host replay 结果

在 `--builtin-ref` 口径下：

```text
host_runtime: builtin_ref=yes
board_hash: feature=0x7ce0b11d input=0xd52f011c
host_hash: feature=0x7ce0b11d logged_input=0xd52f011c effective_input=0xd52f011c source=input_raw
quant_parity: diff_bytes=0/16160 first_diff=[]
board_output: raw=363 score=0.363446 exact=0.363446 q15=11909
host_output: raw=363 score=0.363000 exact=0.363446
output_parity: bytes_equal=yes raw_equal=yes first_diff=[]
```

结论：

- `feat_f32` 与 `input_raw` 已经完全对齐
- `output_raw` 在 reference host 口径下也完全一致
- 当前 nano FP32 的板端部署链路是正确的
- 因此后续若继续分析误唤醒、阈值或实时性，应该直接归因到模型本身与算子成本，而不是部署接线错误

### 6.3 默认 host delegate 的注意事项

如果不加 `--builtin-ref`，当前 TensorFlow 环境可能出现：

```text
output_parity: bytes_equal=no raw_equal=yes first_diff=[0]
```

这次 nano FP32 就出现过一次这样的结果。随后切到 `BUILTIN_REF` 后，立即恢复为：

- `bytes_equal=yes`
- `raw_equal=yes`

所以这里应明确判断为：

- host delegate 数值路径差异
- 不是板端推理错误

## 7. 实时性分析

### 7.1 当前实测

| 项目 | 数值 |
| --- | ---: |
| 单次推理耗时 | `277865 us` |
| 折算毫秒 | `277.865 ms` |
| 当前 stride | `16` 帧 |
| 当前 stride 预算 | `160 ms` |
| 相对 stride 预算 | `1.74x` |
| bundle cpu peak budget | `24.0 ms` |
| 相对 bundle 预算 | `11.58x` |

### 7.2 恢复正常运行后的 live 侧证据

在关闭 `local_debug` 并恢复正常监听后，后续串口日志继续出现：

```text
kws infer slow: infer=7 us=277681 queue=6/64 gate=closed score_pm=304
wakeword queued text=小欧管家 confidence=9971
xiaozhi conversation window opened: source=wakeword mode=auto timeout_ms=8000
```

这说明：

- 板子已经回到正常唤醒链路
- nano FP32 不只是 `align` 样本可触发，恢复常规监听后也能继续产生命中
- live 侧 `infer_us=277681` 与 `align` 样本 `277865` 基本同一量级

### 7.3 与 student tiny FP32 的相对位置

参考当前仓库中已记录的 tiny FP32 结果：

- [RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_FP32_DEBUG_ZH.md](./RUNTIME_RESOURCE_PROFILE_2026-04-08_STUDENT_FP32_DEBUG_ZH.md)

对比：

| 模型 | 单次推理耗时 |
| --- | ---: |
| `student_bc_resnet_tiny_v2_fp32_debug` | `675.892 ms` |
| `student_bc_resnet_nano_v2_fp32_debug` | `277.865 ms` |

结论：

- nano FP32 相比 tiny FP32 已经明显变快
- 但仍不足以满足当前板端 `160 ms stride` 的实时预算
- 从“是否值得继续上板验证”的角度，它是值得保留的 bring-up 候选
- 从“是否已经可作为实时主链”的角度，答案仍是否定的

## 8. 建议

当前建议按下面顺序推进：

1. 保留 nano 这条并行 debug 分支，不替换当前主链
2. 后续若继续追求实时性，优先转到 nano 的 INT8 路线验证
3. 对所有后续模型，继续强制保留：
   - `river kws align run`
   - tensor dump
   - host replay
   - board/header 与 host `.tflite` 一致性校验
4. FP32 对拍时，host 统一优先用 `--builtin-ref`

原因很明确：

- nano FP32 已经证明“部署正确”
- 现在的主要问题是“算得太慢”
- 不应再重复消耗时间怀疑 `40x101` 接错、模型 blob 接错或输入 tensor 接错
