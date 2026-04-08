# 2026-04-08 Student FP32 Debug 运行资源画像

Date: 2026-04-08

## 1. 范围与结论

本文记录当前 `student_bc_resnet_tiny_v2_fp32_debug` 在 `RTL8730E` 板端的实际运行指标，口径对齐算法同事 bundle 中的板端 checklist，同时保留并复用既有板端/本地对拍链路，不引入新的串口调试机制。

本次测量对应的固件配置来自：

- [prj.conf](../prj.conf)
- `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_TINY_V2_FP32_DEBUG=y`

结论先给出：

- 当前板端实际模型契约已经正确切回 student FP32 debug：
  - `variant=student_bc_resnet_tiny_v2_fp32_debug`
  - `input=float32`
  - `output=float32`
  - `shape=[1,40,101,1]`
- 本次 `align` 样本和 host replay 继续证明部署链路正确：
  - `quant_parity: diff_bytes=0/16160`
  - `output_parity: bytes_equal=yes raw_equal=yes`
- 当前实时性仍明显不达标：
  - `align` 样本 `infer_us=675892`
  - 恢复正常运行后的 live 样本 `infer_us=675354`
  - 两次样本平均 `675623 us`
- 相比 bundle 声明预算：
  - `cpu_peak_budget_ms=30.0`
  - 当前板端实测约为其 `22.5x`
- 相比当前板端 stride 预算：
  - `stride=16` 帧
  - 在 `10 ms hop` 契约下等价于 `160 ms`
  - 当前板端单次推理约为其 `4.22x`
- 当前资源压力主因仍然是模型运行时成本，不是部署接线错误。

说明：

- 这次测量使用的是板端调试阈值 `threshold_q15=384`，目的是保留 debug 可见性与对拍可用性。
- 因此本文中的唤醒命中次数、现场触发情况，不用于质量结论，只用于性能观测。
- 质量阈值仍应以 bundle 的 `default_target_recall` 档位为参考，即 `q15=9444`。

## 2. 测量方法

### 2.1 固件编译与刷板

```bash
cd /root/ameba-river
source env.sh >/dev/null
python3 /root/ameba-rtos-1.2/ameba.py build -p
python3 tools/river_flash.py -p /dev/ttyUSB0
```

### 2.2 串口抓取与对拍流程

串口抓取继续沿用既有调试方式：

```bash
script -q -f /tmp/kws_student_fp32_debug.log -c "bash -lc 'stty -F /dev/ttyUSB0 1500000 raw -echo; cat /dev/ttyUSB0'"
```

若抓取启动晚于 boot，则补一次：

```bash
bash -lc "printf 'reboot\r' > /dev/ttyUSB0"
```

板端对拍命令继续沿用现有流程：

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
  --log /tmp/kws_student_fp32_debug.log \
  --model /root/kws-trainint/artifacts/exports/student_bc_resnet_tiny_v2/model.fp32.tflite \
  --seq latest
```

## 3. 当前固件契约

| 项目 | 当前板端实测 |
| --- | --- |
| 变体 | `student_bc_resnet_tiny_v2_fp32_debug` |
| 模型文件大小 | `411560 B` |
| runtime 输入/输出 | `float32 -> float32` |
| 输入 shape | `[1,40,101,1]` |
| 输出 shape | `[1,1,1,1]` |
| 输入布局 | `mels_frames / NHWC` |
| frontend | `40 mel`, `101 frames`, `fft=400`, `hop=160`, `center=yes` |
| 归一化 | `per_clip_mean_std` |
| arena 配置 | `8192 KB` |
| stride | `16` 帧 |
| gate | `vad` |
| pre-roll | `320 ms` |
| pre-roll flush | `16` 帧 |
| 输入队列 | `64` 帧 |
| 当前 debug 阈值 | `threshold_q15=384` |
| bundle 默认阈值 | `default_target_recall q15=9444` |

## 4. Checklist 摘要

### 4.1 与 bundle 声明预算对照

参考：

- `/root/kws-trainint/artifacts/exports/student_bc_resnet_tiny_v2/board_runbook.md`
- `/root/kws-trainint/artifacts/exports/student_bc_resnet_tiny_v2/deployment_summary.json`

| 项目 | bundle 声明/参考 | 当前板端实测 | 结论 |
| --- | ---: | ---: | --- |
| 输入契约 | `1x40x101x1` | `1x40x101x1` | 已对齐 |
| required ops | `ADD/AVG_POOL/CONV/LOGISTIC/MUL` | 板端已正常启动并推理 | 已接通 |
| 默认阈值 | `q15=9444` | 当前测量用 `q15=384` | 本次仅做性能画像 |
| memory budget | `768 KB` | `arena_used=4709152 B` (`4598.8 KiB`) | `5.99x` 超预算 |
| cpu peak budget | `30.0 ms` | `675.892 ms` | `22.53x` 超预算 |

### 4.2 对拍结果

Host replay 输出：

```text
board_meta: input_type=float32 output_type=float32 shape=(1, 40, 101, 1)
quant_parity: diff_bytes=0/16160 first_diff=[]
board_output: raw=371 score=0.371203 exact=0.371203 q15=12163
host_output: raw=371 score=0.371000 exact=0.371203
output_parity: bytes_equal=yes raw_equal=yes first_diff=[]
```

结论：

- 本次性能测量是在部署对拍继续通过的前提下完成的。
- 因此当前性能问题不应再归因到 `40x101` 接错、dtype 接错、或板端 blob 接错。

## 5. Boot / Init 资源画像

### 5.1 KWS 初始化内存

| 指标 | 数值 |
| --- | ---: |
| KWS init 前 heap | `13980736 B` |
| `arena_ready` 后 heap | `5587584 B` |
| `task_ready` 后 heap | `5530688 B` |
| KWS init heap drop | `8450048 B` |
| arena 使用量 | `4709152 B` (`4598.8 KiB`, `4.49 MiB`) |
| arena slack | `3679456 B` (`3593.2 KiB`) |
| ctx | `26272 B` |
| pre-roll buffer | `10240 B` |
| queue buffer | `33024 B` |
| dump lazy buffer | `64644 B` |
| input tensor bytes | `16160 B` |
| output tensor bytes | `4 B` |

### 5.2 系统 free heap 快照

| 时点 | heap_free | heap_min |
| --- | ---: | ---: |
| `boot_ready` | `5143808 B` | `5143808 B` |
| `wifi_connected` | `5091456 B` | `5087232 B` |
| `align` 推理后 `kws perf` | `5320256 B` | `5087232 B` |
| live 样本第 2 次推理后 `kws perf` | `4997376 B` | `4997184 B` |

说明：

- `align` 期间 `audio probe` 已停止，因此 heap 读数比 live 模式略高。
- live 模式下仍保留约 `4.77 MiB` 以上 free heap，说明当前板端不是“内存不够跑不起来”，而是“能跑但很慢”。

## 6. 运行期性能指标

### 6.1 `align` 样本

板端样本信息：

- `source=compiled_pcm`
- `frames=145`
- `duration_ms=2320`
- `pre_silence_frames=20`
- `tail_silence_frames=4`

关键指标：

| 指标 | 数值 |
| --- | ---: |
| `infer_us` | `675892` |
| 等效推理时延 | `675.892 ms` |
| queue 峰值 | `42/64` |
| score | `0.371203` |
| q15 | `12163` |
| raw | `371` |
| gate | `open` |
| queue drop | `0` |
| trim drop | `0` |
| peak heap 低水位 | `5091456 B` |

对应日志：

```text
kws infer slow: infer=1 us=675892 queue=42/64 gate=open score_pm=371
kws perf: infer_us[last=675892 avg=675892 max=675892 ...]
kws tensor dump meta: ... raw=371 score=0.371203 q15=12163 ...
```

### 6.2 恢复正常运行后的 live 样本

恢复 `audio probe` 和 `local_only=no` 之后，串口再次抓到自然 live 样本：

| 指标 | 数值 |
| --- | ---: |
| `infer_us` | `675354` |
| 等效推理时延 | `675.354 ms` |
| queue 峰值 | `43/64` |
| score | `0.262935` |
| q15 | `8616` |
| raw | `263` |
| 当前累计均值 | `675623 us` |
| 当前累计最大值 | `675892 us` |

对应日志：

```text
kws infer slow: infer=2 us=675354 queue=43/64 gate=open score_pm=262
kws perf: infer_us[last=675354 avg=675623 max=675892 ...]
```

### 6.3 与调度预算对照

| 项目 | 数值 |
| --- | ---: |
| stride 预算 | `160 ms` |
| `align` 样本推理时延 | `675.892 ms` |
| live 样本推理时延 | `675.354 ms` |
| 相对 stride 超预算倍数 | `4.22x` |

含义：

- 当前 `64` 帧队列和 `stride=16` 的配置，只是在尽量减缓堆积。
- 它们没有改变根因：单次 `Invoke()` 仍显著慢于调度节拍。

## 7. 观测补充

### 7.1 系统快照

`wifi_connected` 快照显示：

- `IDLE0=92%`
- `IDLE1=87%`
- `river_vad_probe=10%`

恢复 live 运行后的 `vad_speech` 快照显示：

- `IDLE0=98%`
- `IDLE1=96%`
- `river_vad_probe=1%`

这说明：

- 日志中没有出现“整机双核完全打满”的直接证据。
- 当前更像是 `KWS Invoke()` 这条局部热路径本身耗时过长。

### 7.2 阈值说明

当前测量固件使用：

- `threshold_q15=384`
- 日志里体现为 `thresh_pm=11`

这远低于 bundle 默认 `q15=9444`，因此：

- `score=0.182909`
- `score=0.262935`
- `score=0.371203`

这些样本在本次调试固件里会命中唤醒，但不能据此评估模型现场误唤醒率。

## 8. 结论

就算法 checklist 最关心的板端指标来说，当前 student FP32 debug 变体的结论非常明确：

- 契约已经正确：
  - `40x101`
  - `float32`
  - board/local parity 精确通过
- 内存已经能跑通：
  - `8192 KB` arena 下稳定初始化
  - `arena_used=4.49 MiB`
- 但实时性不满足当前板端要求：
  - 单次推理约 `675.6 ms`
  - 明显高于 `30 ms` 声明预算
  - 也明显高于当前 `160 ms` 运行节拍预算

因此当前 student FP32 debug 的定位仍应保持不变：

- 可继续作为 `板端部署正确性 + 本地/板端对拍 + FP32 参考输出` 调试变体
- 不建议作为当前 `RTL8730E` 全链路实时交互的候选主模型
