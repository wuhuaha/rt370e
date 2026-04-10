# `student_conv_resnet_ed_tiny_v1_fp32_debug` 板端部署、对拍与性能记录

日期：2026-04-10

## 1. 结论摘要

这次 `student_conv_resnet_ed_tiny_v1_fp32_debug` 的结论很明确：

- 部署正确性已经闭环，不再是“可能接错”的状态
  - 板端嵌入头文件与算法导出 `model.fp32.tflite` 已做字节级一致性校验
  - 板端 `40x101x1` FP32 前端契约与 bundle 要求一致
  - host replay 最终得到：
    - `output_parity: bytes_equal=yes raw_equal=yes`
- 当前板端实时性明显好于之前已测的多个 FP32 候选
  - 当前板端 `infer_us` 稳定在 `~96.4 ms`
  - 明显快于：
    - `student_dscnn_tiny_v2_fp32_debug`：`~184 ms`
    - `student_bc_resnet_nano_v2_fp32_debug`：`~277.9 ms`
- 但它还没有达到算法 bundle 自己声明的部署预算
  - bundle `cpu_peak_budget_ms=24.0`
  - 当前板端实测约 `96.4 ms`
  - 约为预算的 `4.0x`
  - bundle `board_memory_budget_kb=512`
  - 当前 `arena_used=654960 B`，约 `639.6 KiB`
  - 约为预算的 `1.25x`

一句话概括：

- 这是一个“部署正确、当前 FP32 候选里板端表现最好，但仍未达到目标实时预算”的模型。

## 2. 当前测试固件口径

本次测试基于 latest-SDK `/root/ameba-rtos` 编译并刷板，活动配置为：

- `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_CONV_RESNET_ED_TINY_V1_FP32_DEBUG=y`
- `CONFIG_RIVER_KWS_TENSOR_ARENA_KB=2048`
- `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=9038`
- `CONFIG_RIVER_KWS_TRIGGER_HOLD_FRAMES=1`
- `CONFIG_RIVER_KWS_COOLDOWN_MS=1800`

编译产物：

| 项目 | 数值 |
| --- | ---: |
| `km4_boot_all.bin` | `51872 B` |
| `km0_km4_ca32_app.bin` | `4094592 B` |
| `ota_all.bin` | `4094624 B` |

## 3. 模型文件一致性

板端嵌入头文件：

- `components/river_voice/generated/student_conv_resnet_ed_tiny_v1_fp32_model_data.h`

算法导出模型：

- `/root/kws-trainint/artifacts/exports/student_conv_resnet_ed_tiny_v1/model.fp32.tflite`

一致性校验结果：

| 项目 | 数值 |
| --- | --- |
| header bytes | `479008` |
| model bytes | `479008` |
| header sha256 | `3fa05447484ba77a6b24da050da1867271ae2d77fbd27ae1fd2144a682376e66` |
| model sha256 | `3fa05447484ba77a6b24da050da1867271ae2d77fbd27ae1fd2144a682376e66` |
| exact match | `yes` |

结论：

- 当前板端嵌入模型与算法交付的 FP32 TFLite 是同一份文件
- 因此前后续 parity 与性能结论具备有效前提

## 4. Boot / Init 契约确认

板端 boot log 已确认：

```text
kws init plan: heap_free=13903744 ctx=26272B arena=2048KB model=479008B align=32
kws tensor io: runtime_in=float32 runtime_out=float32 model_in=float32 model_out=float32 effective_in=float32 effective_out=float32
kws input shape: src=schema dims=[1,40,101,1] layout=mels_frames
kws alloc: ... input_bytes=16160 output_bytes=4 arena_used=654960B arena_slack=1442192B
kws backend: runtime=tflite_micro input=40x101x1 ... variant=student_conv_resnet_ed_tiny_v1_fp32_debug ... threshold_q15=9038 ...
kws frontend: source=fixed_dsb_mono feature=log_mel bins=40 frames=101 log=natural norm=per_clip_mean_std wake_text=小欧管家
```

关键初始化指标：

| 项目 | 数值 |
| --- | ---: |
| init 前 heap | `13903744 B` |
| `ctx` | `26272 B` |
| 模型大小 | `479008 B` |
| 输入 tensor bytes | `16160 B` |
| 输出 tensor bytes | `4 B` |
| arena 实际使用 | `654960 B` |
| arena 剩余 | `1442192 B` |

结论：

- 当前板端契约与算法导出 bundle 完全一致：
  - 输入 shape：`1x40x101x1`
  - 输入 dtype：`float32`
  - 输出 dtype：`float32`
  - 前端：`n_fft=400`、`hop=160`、`center=yes`、`per_clip_mean_std`

## 5. 板端 / 本机对拍结果

### 5.1 稳定的板端对拍路径

这次最终采用的是当前仓库里已经验证过的稳定序列：

```text
reboot
river kws debug local on
river audio probe stop
river kws debug local off
river kws align run
river kws dump meta
river kws dump chunk output_raw 1
river kws dump chunk feat_f32 1..253
```

这条路径的关键点不是“多打一堆日志”，而是：

- 先显式把环境切到适合对拍的 idle 状态
- 再让 `align run` 自己临时打开本地调试隔离 wake handoff
- 最后从 preserved snapshot 分批拉 `meta/output/feat_f32`

### 5.2 板端对拍样本结果

板端关键日志：

```text
kws align replay done: dump=preserved local_only_restored=no
kws tensor dump meta: seq=1 feat_hash=0x7ce0b11d input_hash=0xd52f011c raw=345 score=0.345300 q15=11314 ...
kws tensor dump output_raw: seq=1 chunk=1/1 hex=39cbb03e
kws tensor dump feat_f32: seq=1 chunk=1/253 ...
kws tensor dump feat_f32: seq=1 chunk=253/253 ...
```

关键结果：

| 项目 | 数值 |
| --- | --- |
| dump seq | `1` |
| feat_hash | `0x7ce0b11d` |
| input_hash | `0xd52f011c` |
| raw | `345` |
| score | `0.345300` |
| q15 | `11314` |
| 是否唤醒 | `是` |

### 5.3 transcript 补块说明

第一次串口 transcript 里：

- `feat_f32` 共 `253` 个 chunk
- 但其中：
  - `chunk 5`、`chunk 121` 缺失
  - `chunk 4`、`chunk 120` 被串口回显串花

因为 `align run` 已经成功保留 snapshot，所以没有必要重跑整轮 replay：

- 直接对 preserved snapshot 补拉：
  - `river kws dump chunk feat_f32 5`
  - `river kws dump chunk feat_f32 121`
  - `river kws dump chunk feat_f32 4`
  - `river kws dump chunk feat_f32 120`

补块后的最终 transcript 已恢复为完整 `feat_f32 1..253`。

这说明：

- 这次问题属于串口 transcript 质量，不是模型部署错误
- preserved snapshot 机制确实能把一次“差一点”的 transcript 修回可 replay 状态

### 5.4 Host replay 结果

host 命令：

```bash
cd /root/ameba-river
TF_ENABLE_ONEDNN_OPTS=0 \
OMP_NUM_THREADS=1 \
TF_NUM_INTRAOP_THREADS=1 \
TF_NUM_INTEROP_THREADS=1 \
python3 tools/kws/replay_board_tensor_dump.py \
  --log /tmp/kws_conv_resnet_ed_tiny_fp32_exact_parity.log \
  --model /root/kws-trainint/artifacts/exports/student_conv_resnet_ed_tiny_v1/model.fp32.tflite \
  --seq latest \
  --builtin-ref
```

结果：

```text
note: using feature tensor bytes as effective input because they match the board input hash
board_hash: feature=0x7ce0b11d input=0xd52f011c
host_hash: feature=0x7ce0b11d logged_input=n/a effective_input=0xd52f011c source=feat_f32_missing_input_raw
board_output: raw=345 score=0.345300 exact=0.345300 q15=11314
host_output: raw=345 score=0.345000 exact=0.345300
output_parity: bytes_equal=yes raw_equal=yes first_diff=[]
```

结论：

- 当前 `student_conv_resnet_ed_tiny_v1_fp32_debug` 板端部署正确
- 这次 replay 里 `input_raw` 没有单独分批拉回，但对 `FP32` debug 模型这不影响结论
  - 脚本已确认：
    - `feature hash` 与板端 `input_hash` 一致
    - 因而可以用 `feat_f32` 原始字节作为 effective input
- 最终仲裁标准 `output_raw` 字节完全一致：
  - `bytes_equal=yes`
  - `raw_equal=yes`

## 6. 板端性能与资源

### 6.1 实测

来自 boot 后实时推理日志：

```text
kws perf: infer_us[last=96460 avg=96420 max=96475 ...] mem[arena=654960/2048KB ...]
```

来自 `align` 样本捕获：

```text
kws infer slow: infer=10 us=96295 queue=6/64 gate=open score_pm=345
```

综合可得：

| 项目 | 数值 |
| --- | ---: |
| 模型大小 | `479008 B` |
| arena 实际使用 | `654960 B` |
| arena 预留配置 | `2048 KB` |
| live 口径 infer_us last | `96460 us` |
| live 口径 infer_us avg | `96420 us` |
| live 口径 infer_us max | `96475 us` |
| align 口径 infer_us | `96295 us` |

结论：

- 当前 FP32 版本推理时延非常稳定，基本落在 `96.3 ~ 96.5 ms`
- 当前主要 runtime 成本不是随机抖动，而是绝对时延本身还偏高

### 6.2 与 bundle 预算对照

算法 bundle 声明：

- `board_memory_budget_kb=512`
- `cpu_peak_budget_ms=24.0`

对照结果：

| 项目 | bundle 预算 | 当前板端实测 | 结论 |
| --- | ---: | ---: | --- |
| memory | `512 KB` | `654960 B` (`639.6 KiB`) | `1.25x` 超预算 |
| cpu peak | `24.0 ms` | `96.4 ms` | `4.02x` 超预算 |

结论：

- 这版模型已经明显接近“可板测”的资源区间
- 但如果目标是直接满足 bundle 声明预算，它还不够

## 7. 与已测 FP32 候选对比

参考仓内已归档报告：

- `student_dscnn_tiny_v2_fp32_debug`
- `student_bc_resnet_nano_v2_fp32_debug`

对比：

| 变体 | 模型大小 | arena 实际使用 | 板端 infer |
| --- | ---: | ---: | ---: |
| `student_conv_resnet_ed_tiny_v1_fp32_debug` | `479008 B` | `654960 B` | `~96.4 ms` |
| `student_dscnn_tiny_v2_fp32_debug` | `12376 B` | `1168336 B` | `~184.0 ms` |
| `student_bc_resnet_nano_v2_fp32_debug` | `108828 B` | `3139392 B` | `~277.9 ms` |

关键观察：

- 相比 `student_dscnn_tiny_v2_fp32_debug`
  - 当前 `conv_resnet_ed_tiny_v1_fp32_debug` 约快 `1.91x`
  - arena 约降到其 `56.1%`
- 相比 `student_bc_resnet_nano_v2_fp32_debug`
  - 当前 `conv_resnet_ed_tiny_v1_fp32_debug` 约快 `2.88x`
  - arena 约降到其 `20.9%`

这意味着：

- 在“已经测过的这些 FP32 学生模型”里，`conv_resnet_ed_tiny_v1_fp32_debug` 目前是板端表现最好的一个
- 它并不是“刚好能跑”，而是已经明显优于此前几条 FP32 路线

## 8. 最终判断

### 8.1 能不能证明部署正确

能。

而且已经不是弱证明，是完整闭环：

- 板端 header 与算法 `.tflite` 字节级一致
- 板端前端 / shape / dtype 契约与 bundle 一致
- preserved snapshot 成功抓到完整 `feat_f32 + output_raw`
- host replay 得到：
  - `output_parity: bytes_equal=yes raw_equal=yes`

所以：

- 后续若出现置信度、误唤醒、漏唤醒问题，不能再先怀疑“是不是板端接错了”

### 8.2 实时性值不值得继续

值得继续，但要明确边界。

值得继续的原因：

- 当前是我们已测 FP32 路线里最好的板端结果
- 已经把“部署错误”风险排除掉了
- 相比之前候选，时延和内存都明显更健康

边界也很明确：

- 它还没有达到 `24 ms` CPU 预算
- 内存也还略高于 `512 KB` 预算
- 因此它还不是“可直接宣告量产就绪”的状态

### 8.3 当前建议

我的建议是：

- 把 `student_conv_resnet_ed_tiny_v1_fp32_debug` 视为当前最有价值的 FP32 板端基线
- 后续若继续做算法迭代或结构搜索，优先围绕这条 `conv_resnet_ed` 路线优化
- 讨论阈值、误触、真实场景召回时，应默认建立在这次已确认的正确部署前提上

换句话说：

- 这版模型“已经值得继续”
- 但当前阶段更准确的定位是：
  - `best current FP32 bring-up baseline`
  - 不是 `already within deployment budget`
