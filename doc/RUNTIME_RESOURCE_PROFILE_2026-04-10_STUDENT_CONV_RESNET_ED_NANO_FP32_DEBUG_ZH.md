# `student_conv_resnet_ed_nano_v1_fp32_debug` 板端部署、对拍与性能记录

日期：2026-04-10

## 1. 结论摘要

这次 `student_conv_resnet_ed_nano_v1_fp32_debug` 的结论也已经比较清楚：

- 部署正确性已闭环
  - 板端嵌入头文件与算法导出 `model.fp32.tflite` 已做字节级一致性校验
  - 板端 `40x101x1` FP32 前端契约与 bundle 要求一致
  - host replay 最终得到：
    - `output_parity: bytes_equal=yes raw_equal=yes`
- 当前板端实时性显著优于同家族 `tiny` FP32 调试变体
  - 当前对拍样本 `infer_us=29826`
  - 相比 `student_conv_resnet_ed_tiny_v1_fp32_debug` 的 `~96420 us`
    - 约快 `3.23x`
- 当前资源占用也明显更轻
  - `arena_used=436528 B`
  - 相比 `student_conv_resnet_ed_tiny_v1_fp32_debug` 的 `654960 B`
    - 约降到其 `66.6%`
- 但它仍未完全进入算法 bundle 自己声明的预算
  - bundle `cpu_peak_budget_ms=18.0`
  - 当前板端实测 `29.826 ms`
  - 约为预算的 `1.66x`
  - bundle `board_memory_budget_kb=384`
  - 当前 `arena_used=436528 B`，约 `426.3 KiB`
  - 约为预算的 `1.11x`

一句话概括：

- 这是一个“部署正确、显著快于当前 `conv_resnet_ed_tiny` FP32 基线、已经接近预算，但还没有完全达标”的模型。

## 2. 当前测试固件口径

本次测试基于 latest-SDK `/root/ameba-rtos` 编译并刷板，活动配置为：

- `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_CONV_RESNET_ED_NANO_V1_FP32_DEBUG=y`
- `CONFIG_RIVER_KWS_TENSOR_ARENA_KB=2048`
- `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=9008`
- `CONFIG_RIVER_KWS_TRIGGER_HOLD_FRAMES=1`
- `CONFIG_RIVER_KWS_COOLDOWN_MS=1800`

编译命令：

```bash
cd /root/ameba-river
export AMEBA_SDK_ROOT=/root/ameba-rtos
source /root/ameba-river/env.sh
python /root/ameba-rtos/ameba.py soc RTL8730E
python /root/ameba-rtos/ameba.py build -p
```

刷板命令：

```bash
cd /root/ameba-river
bash -lc "printf 'reboot uartburn\r' > /dev/ttyUSB0"
export AMEBA_SDK_ROOT=/root/ameba-rtos
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
```

产物尺寸：

| 项目 | 数值 |
| --- | ---: |
| `ap_image_all.bin` | `3275872 B` |
| `km4_image2_all.bin` | `380448 B` |
| `km0_image2_all.bin` | `94208 B` |
| `km0_km4_ca32_app.bin` | `3758720 B` |
| `km4_boot_all.bin` | `51872 B` |

## 3. 模型文件一致性

板端嵌入头文件：

- `components/river_voice/generated/student_conv_resnet_ed_nano_v1_fp32_model_data.h`

算法导出模型：

- `/root/kws-trainint/artifacts/exports/student_conv_resnet_ed_nano_v1/model.fp32.tflite`

一致性校验结果：

| 项目 | 数值 |
| --- | --- |
| header bytes | `141700` |
| model bytes | `141700` |
| header sha256 | `5c955b390db469ddd5d82c22c2b00022eb8a82f596e4e5dd2194d3c7b07c7727` |
| model sha256 | `5c955b390db469ddd5d82c22c2b00022eb8a82f596e4e5dd2194d3c7b07c7727` |
| exact match | `yes` |

结论：

- 当前板端嵌入模型与算法交付的 FP32 TFLite 是同一份文件

## 4. Boot / Init 契约确认

板端 boot log 已确认：

```text
kws init plan: heap_free=14239616 ctx=26272B arena=2048KB model=141700B align=32
kws io binding: ... input_idx=0 type=float32 ... output_idx=28 type=float32 ... arena_used=436528 arena_slack=1660624
kws tensor io: runtime_in=float32 runtime_out=float32 model_in=float32 model_out=float32 effective_in=float32 effective_out=float32
kws input shape: src=schema dims=[1,40,101,1] layout=mels_frames
kws alloc: ... input_bytes=16160 output_bytes=4 arena_used=436528B arena_slack=1660624B
kws backend: runtime=tflite_micro input=40x101x1 ... variant=student_conv_resnet_ed_nano_v1_fp32_debug ... threshold_q15=9008 ...
kws frontend: source=fixed_dsb_mono feature=log_mel bins=40 frames=101 log=natural norm=per_clip_mean_std wake_text=小欧管家
```

关键初始化指标：

| 项目 | 数值 |
| --- | ---: |
| init 前 heap | `14239616 B` |
| `ctx` | `26272 B` |
| 模型大小 | `141700 B` |
| 输入 tensor bytes | `16160 B` |
| 输出 tensor bytes | `4 B` |
| arena 实际使用 | `436528 B` |
| arena 剩余 | `1660624 B` |
| heap init -> task_ready | `14239616 -> 12081024 B` |

结论：

- 当前板端契约与算法导出 bundle 完全一致：
  - 输入 shape：`1x40x101x1`
  - 输入 dtype：`float32`
  - 输出 dtype：`float32`
  - 前端：`n_fft=400`、`hop=160`、`center=yes`、`per_clip_mean_std`

## 5. 板端 / 本机对拍结果

### 5.1 板端序列

本次沿用已经验证过的稳定序列：

```text
reboot
river kws debug local on
river audio probe stop
river kws debug local off
river kws align run
river kws dump meta
river kws dump chunk output_raw 1
river kws dump chunk feat_f32 1..253
river kws align status
```

这次串口 transcript 质量良好：

- `feat_f32` 共 `253` 个 chunk
- `1..253` 全部完整
- 不需要补块

### 5.2 板端关键结果

关键日志：

```text
kws diag: infer=1 ... raw=306 score=0.305877 q15=10023 ...
kws infer slow: infer=1 us=29826 queue=1/64 gate=open score_pm=305
wakeword hit: text=小欧管家 score_pm=305 q15=10023 triggers=1 cooldown_ms=1800 mode=threshold
kws align replay captured: seq=1 infer=1 score=0.305877 q15=10023
kws align replay done: dump=preserved local_only_restored=no
kws tensor dump meta: seq=1 feat_hash=0x7ce0b11d input_hash=0xd52f011c raw=306 score=0.305877 q15=10023 ...
kws tensor dump output_raw: seq=1 chunk=1/1 hex=f49b9c3e
```

关键结果：

| 项目 | 数值 |
| --- | --- |
| dump seq | `1` |
| feat_hash | `0x7ce0b11d` |
| input_hash | `0xd52f011c` |
| raw | `306` |
| score | `0.305877` |
| q15 | `10023` |
| threshold_q15 | `9008` |
| 是否唤醒 | `是` |
| align infer_us | `29826` |

### 5.3 Host replay 结果

host 命令：

```bash
cd /root/ameba-river
TF_ENABLE_ONEDNN_OPTS=0 \
OMP_NUM_THREADS=1 \
TF_NUM_INTRAOP_THREADS=1 \
TF_NUM_INTEROP_THREADS=1 \
python3 tools/kws/replay_board_tensor_dump.py \
  --log /tmp/kws_conv_resnet_ed_nano_fp32_align.log \
  --model /root/kws-trainint/artifacts/exports/student_conv_resnet_ed_nano_v1/model.fp32.tflite \
  --seq latest \
  --builtin-ref
```

结果：

```text
note: using feature tensor bytes as effective input because they match the board input hash
board_hash: feature=0x7ce0b11d input=0xd52f011c
host_hash: feature=0x7ce0b11d logged_input=n/a effective_input=0xd52f011c source=feat_f32_missing_input_raw
board_output: raw=306 score=0.305877 exact=0.305877 q15=10023
host_output: raw=306 score=0.306000 exact=0.305877
output_parity: bytes_equal=yes raw_equal=yes first_diff=[]
```

结论：

- 当前 `student_conv_resnet_ed_nano_v1_fp32_debug` 板端部署正确
- 最终仲裁标准 `output_raw` 字节完全一致：
  - `bytes_equal=yes`
  - `raw_equal=yes`

## 6. 运行态性能与资源

在对拍样本完成后，再通过 `river kws status` 获取当前运行态快照：

```text
kws status: gate=closed ready=no score_pm=305 gate_best_pm=305 thresh_pm=274 weak_pm=274 ... hits=1 triggers=1 ...
kws perf: infer_us[last=29826 avg=29826 max=29826 win=29826 warn=1 alert=1] heap[now=11876352 min=11645376 win_low=11876352 init=14239616->12081024 min_init=12081024] mem[arena=436528/2048KB slack=1660624 ctx=26272 pre=10240 queue=33024 dump=64644] queue[frames=64 stride=16 win_peak=2] score[win_pm=305 gate_best_pm=305]
```

可得：

| 项目 | 数值 |
| --- | ---: |
| 模型大小 | `141700 B` |
| arena 实际使用 | `436528 B` |
| arena 预留配置 | `2048 KB` |
| infer_us last | `29826 us` |
| infer_us avg | `29826 us` |
| infer_us max | `29826 us` |
| heap now | `11876352 B` |
| heap min | `11645376 B` |

### 6.2 与 bundle 预算对照

算法 bundle 声明：

- `board_memory_budget_kb=384`
- `cpu_peak_budget_ms=18.0`

对照结果：

| 项目 | bundle 预算 | 当前板端实测 | 结论 |
| --- | ---: | ---: | --- |
| memory | `384 KB` | `436528 B` (`426.3 KiB`) | `1.11x` 超预算 |
| cpu peak | `18.0 ms` | `29.826 ms` | `1.66x` 超预算 |

结论：

- 这版已经明显比 `tiny` 更接近目标预算
- 但还不能直接宣告进入当前 bundle 预算

## 7. 与同家族 `tiny` FP32 调试变体对比

参考已归档报告：

- `student_conv_resnet_ed_tiny_v1_fp32_debug`

对比：

| 变体 | 模型大小 | arena 实际使用 | 板端 infer |
| --- | ---: | ---: | ---: |
| `student_conv_resnet_ed_nano_v1_fp32_debug` | `141700 B` | `436528 B` | `29826 us` |
| `student_conv_resnet_ed_tiny_v1_fp32_debug` | `479008 B` | `654960 B` | `96420 us` |

关键观察：

- `nano` 相比 `tiny`
  - 模型大小约为其 `29.6%`
  - arena 约为其 `66.6%`
  - 推理时延约为其 `30.9%`
  - 即约快 `3.23x`

这意味着：

- 当前 `conv_resnet_ed` 这条 kernel-aware 路线继续向轻量档收缩是有效的
- 主要矛盾已经从“能不能在板端正确跑起来”，转成“还能否再压到预算线内”

## 8. 当前判断

### 8.1 能不能证明部署正确

能，而且已经闭环：

- 板端 header 与算法 `.tflite` 字节级一致
- 板端前端 / shape / dtype 契约与 bundle 一致
- 对拍样本 `feat_f32 + output_raw` 完整抓取
- host replay 得到：
  - `output_parity: bytes_equal=yes raw_equal=yes`

### 8.2 当前值不值得继续

值得。

原因很明确：

- 它比当前 `conv_resnet_ed_tiny_v1_fp32_debug` 明显更快
- 它的内存占用也更健康
- 它已经接近当前 bundle 预算，而不是像 `tiny` 那样还差一个大档位

### 8.3 当前定位

我对这版的定位是：

- `best current conv_resnet_ed FP32 budget-approach baseline`

它还不是“已达标量产档”，但已经是当前这条结构路线里更值得继续优化和对照的一版。
