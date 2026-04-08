# 2026-04-08 Student DS-CNN Small FP32 Debug 运行资源画像

Date: 2026-04-08

## 1. 结论先给

`student_dscnn_small_v2_fp32_debug` 已经在当前 `ameba-river` 板端正确接入，并通过了既有板端 / 本机对拍链路验证：

- boot 明确确认：
  - `variant=student_dscnn_small_v2_fp32_debug`
  - `runtime_in=float32 runtime_out=float32`
  - `dims=[1,40,101,1]`
  - `fft=400 hop=160 center=yes`
- 板端嵌入 header 与算法交付 FP32 `.tflite` 完全一致：
  - bytes=`23600`
  - sha256=`e0a2bedd32801d3d05ca4b0b3369137bedba990d28e02b5253696dc021e56925`
- 保留原有 `river kws align run` 对拍机制后，板端与 host 结果一致：
  - board `raw=333 score=0.333065 q15=10914`
  - host `raw=333 exact=0.333065`
  - `feature hash=0xf6cf59f0`
  - `effective input hash=0x8aa04513`
  - `output_parity: bytes_equal=no raw_equal=yes`

但从板端资源与实时性看，这个模型不值得继续深挖：

- `arena_used=1945648B`
- `arena_slack=151504B`
- `infer_us[last=389384 avg=389256 max=389454]`
- 相对 bundle 预算：
  - memory 约 `4.95x` 超预算
  - cpu 约 `11.12x` 超预算
- 相对已测 `student_dscnn_tiny_v2_fp32_debug`：
  - arena 约 `1.67x` 更大
  - 推理时延约 `2.12x` 更慢

结论：

- 这版 `DS-CNN small FP32` 的部署是正确的
- 但它不是更好的板端候选
- 当前更像是“算法离线质量略有提升，但板端成本明显恶化”的模型

## 2. 当前固件契约

本次固件配置：

- [prj.conf](../prj.conf)
- `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_DSCNN_SMALL_V2_FP32_DEBUG=y`
- `CONFIG_RIVER_KWS_TENSOR_ARENA_KB=2048`
- `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=10534`

boot 关键日志：

```text
kws init plan: heap_free=14369856 ctx=26272B arena=2048KB model=23600B align=32
kws alloc: ... input_bytes=16160 output_bytes=4 arena_used=1945648B arena_slack=151504B
kws backend: runtime=tflite_micro input=40x101x1 log_mel sr=16k fft=400 hop=160 center=yes arena=2048KB model=23600B variant=student_dscnn_small_v2_fp32_debug stride=16 threshold_q15=10534 hold=1 cooldown_ms=1800 gate=vad pre_roll_ms=320 pre_roll_flush=16 queue=64 trim=48->21
```

当前部署契约：

| 项目 | 当前板端实测 |
| --- | --- |
| 变体 | `student_dscnn_small_v2_fp32_debug` |
| 模型大小 | `23600 B` |
| runtime 输入/输出 | `float32 -> float32` |
| 输入 shape | `[1,40,101,1]` |
| 输出 shape | `[1,1,1,1]` |
| frontend | `40 mel`, `101 frames`, `fft=400`, `hop=160`, `center=yes` |
| 归一化 | `per_clip_mean_std` |
| arena 配置 | `2048 KB` |
| arena 实际使用 | `1945648 B` |
| arena 剩余 | `151504 B` |
| stride | `16` |
| 阈值 | `threshold_q15=10534` |
| 唤醒词 | `小欧管家` |

## 3. 模型文件一致性

板端 header 与算法 bundle 对照：

| 项目 | 数值 |
| --- | --- |
| header bytes | `23600` |
| model bytes | `23600` |
| header sha256 | `e0a2bedd32801d3d05ca4b0b3369137bedba990d28e02b5253696dc021e56925` |
| model sha256 | `e0a2bedd32801d3d05ca4b0b3369137bedba990d28e02b5253696dc021e56925` |
| exact match | `yes` |

因此本次板端结果可直接代表算法交付的这份 FP32 bundle，不存在“板端嵌错模型文件”的干扰项。

## 4. 板端 / 本机对拍

### 4.1 板端 `align` 结果

保留原有流程：

```text
river kws debug local on
river audio probe stop
river kws align status
river kws align run
river audio probe start
river kws debug local off
```

关键日志：

```text
kws align guard: kws=ready probe=stopped interaction=wake_monitoring detection=ready worker=idle snapshot=empty local_only=yes
kws diag: infer=1 ... raw=316 score=0.316074 q15=10357 ...
kws diag: infer=2 ... raw=333 score=0.333065 q15=10914 ...
wakeword hit: text=小欧管家 score_pm=333 q15=10914 triggers=2 cooldown_ms=1800 mode=threshold
kws align replay captured: seq=2 infer=4 score=0.333065 q15=10914
```

说明：

- 这次 `align` 样本确实在板端触发了唤醒
- 分数高于当前阈值 `q15=10534`
- 但 margin 并不大：
  - 命中 `q15=10914`
  - 阈值 `q15=10534`
  - 仅高出 `380 q15`

### 4.2 Host replay 结果

Host 命令：

```bash
cd /root/ameba-river
TF_ENABLE_ONEDNN_OPTS=0 \
OMP_NUM_THREADS=1 \
TF_NUM_INTRAOP_THREADS=1 \
TF_NUM_INTEROP_THREADS=1 \
python3 tools/kws/replay_board_tensor_dump.py \
  --log /tmp/kws_dscnn_small_fp32_align_full.log \
  --model /root/kws-trainint/artifacts/exports/student_dscnn_small_v2/model.fp32.tflite \
  --seq latest \
  --builtin-ref
```

结果：

```text
board_hash: feature=0xf6cf59f0 input=0x8aa04513
host_hash: feature=0xf6cf59f0 logged_input=0x8aa04513 effective_input=0x8aa04513 source=input_raw
quant_parity: diff_bytes=0/16160 first_diff=[]
board_output: raw=333 score=0.333065 exact=0.333065 q15=10914
host_output: raw=333 score=0.333000 exact=0.333065
output_parity: bytes_equal=no raw_equal=yes first_diff=[0]
```

结论：

- 这版 `DS-CNN small FP32` 部署正确
- `feat_f32`、`input_raw` 和输出解码值与 host 一致
- 仍然只有 `float32` 最低字节级差异，不是适配错误

补充观察：

- 这次串口日志里没有单独出现 `kws tensor dump end:` 行
- 但 `input_raw` 已完整到 `253/253`，且 host replay 已成功
- 因此本次对拍依然有效

## 5. 资源与实时性

### 5.1 板端实测

| 指标 | 数值 |
| --- | ---: |
| init 前 heap | `14369856 B` |
| init 后 `task_ready` heap | `12211264 B` |
| boot ready heap_free | `11824384 B` |
| arena_used | `1945648 B` |
| arena_slack | `151504 B` |
| infer_us last | `389384` |
| infer_us avg | `389256` |
| infer_us max | `389454` |

### 5.2 与 bundle 预算对照

来自 `deployment_summary.json` 的预算：

- `board_memory_budget_kb=384`
- `cpu_peak_budget_ms=35.0`

对照结果：

| 项目 | bundle 预算 | 当前板端实测 | 结论 |
| --- | ---: | ---: | --- |
| memory | `384 KB` | `1945648 B` | `4.95x` 超预算 |
| cpu | `35.0 ms` | `389.256 ms` | `11.12x` 超预算 |

### 5.3 与 DS-CNN tiny 对照

| 项目 | DS-CNN tiny FP32 | DS-CNN small FP32 | 变化 |
| --- | ---: | ---: | --- |
| model bytes | `12376` | `23600` | `1.91x` |
| arena_used | `1168336` | `1945648` | `1.67x` |
| infer avg ms | `183.969` | `389.256` | `2.12x` |

说明：

- `small` 并没有带来更好的板端成本结构
- 对当前 CA32 + TFLM runtime 路径来说，`small` 比 `tiny` 更不适合

## 6. 值不值得继续上板深挖

我的判断：不值得。

理由：

- 部署 correctness 已经证明，没有必要继续把时间花在“是不是接错了”
- 它比 `DS-CNN tiny` 更慢、占更多 arena，且差距不小
- 命中分数只比阈值高一点，不能支撑“高成本换来明显鲁棒性提升”的论点
- 从当前结果看，它不是下一步最优试板候选

更准确地说：

- 它完成了“可接入性验证”
- 但没有通过“值得继续优化和保留”的工程筛选

## 7. 当前还值得继续尝试的模型

结合当前导出物和已测结果，建议如下：

### 7.1 仍值得直接试板

1. `student_bc_resnet_tiny_clip_v2`

理由：

- 同样是当前已支持的 `40x101 + MUL + float32` 契约
- 离线参考里 `board_fa_per_hour_at_target_recall=441.176471`，比当前 `DS-CNN small` 更低
- `board_holdout_recall=0.785714`，仍有一轮直接对拍价值
- 虽然大概率仍然不实时，但它至少还有作为“质量 / 误触折中候选”的验证价值

### 7.2 低优先级，不建议马上试

1. `student_bc_resnet_tiny_hard_only_v2`

理由：

- `board_holdout_recall=0.5`
- 这在上板前就已经太弱
- 只有在算法同事明确要验证“hard-only 数据策略是否有特殊现场收益”时才值得补试

2. `student_bc_resnet_small_v2`

理由：

- `model.fp32.tflite=1019756 B`
- `deploy_parameter_count=253281`
- 即使不看实测，只看体量也应视为低优先级 accuracy-first 试验项

## 8. 最终判断

本轮结果可以直接作为后续筛选依据：

- `student_dscnn_small_v2_fp32_debug`
  - 部署正确：`yes`
  - 本机 / 板端对拍一致：`yes`
  - 板端继续投入价值：`no`
- 下一轮最值得继续的直接候选：
  - `student_bc_resnet_tiny_clip_v2`
