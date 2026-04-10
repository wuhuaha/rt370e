# `student_dscnn_tiny_v2_int8_debug` 板端对拍与性能记录

日期：2026-04-10

## 1. 结论

本次已经完成当前活动 INT8 变体的正式板端 / 本机闭环：

- 模型变体：`student_dscnn_tiny_v2_int8_debug`
- SDK 基线：`/root/ameba-rtos`
- 固件基线：当前 `kws` 分支 `HEAD`
- 板端 / host exact parity：通过
- `align run` 后 monitor / shell：已恢复可用
- 当前典型板端推理耗时：`infer_us ≈ 492.7 ms`

因此当前这条线的结论应更新为：

- `student_dscnn_tiny_v2_int8_debug` 已经不再卡在 shell / monitor 返回路径
- 部署正确性已经被当前板端 / host exact parity 证明
- 后续剩下的是“值不值得继续作为 INT8 候选”的性能与效果判断

## 2. 本次验证对象

本次板端日志明确显示：

- `variant=student_dscnn_tiny_v2_int8_debug`
- `input=40x101x1`
- `fft=400`
- `hop=160`
- `center=yes`
- `arena=2048KB`
- `threshold_q15=9125`

对应的 host replay 模型为：

- `/root/kws-trainint/artifacts/exports/student_dscnn_tiny_v2/model.int8.tflite`

## 3. 本次关键修正背景

此前这条 INT8 线的核心阻塞点不是模型输出不一致，而是：

- `river kws align run` 成功返回后
- monitor / shell 无法继续稳定执行 `river kws dump meta` / `dump chunk`

本轮是在保留既有对拍命令口径不变的前提下，基于当前分支已有修正继续验证：

- `river_voice_kws_run_alignment_sample(...)` 内不再把 `512B` 尾静音缓冲放在 shell task 栈上
- 本分支又额外提升了 AP shell 栈 headroom

最终结果是：

- `align run` 返回后
- `dump meta`
- `dump chunk output_raw`
- `dump chunk feat_f32 1..253`
- `dump chunk input_raw 1..64`

都可以稳定完成。

## 4. 本次板端流程

本次仍然严格复用现有对拍流程，没有改动调试命令语义：

```text
reboot
river kws debug local on
river audio probe stop
river kws debug local off
river kws align run
river kws dump meta
river kws dump chunk output_raw 1
river kws dump chunk feat_f32 1..253
river kws dump chunk input_raw 1..64
river kws align status
```

串口日志保存为：

- `/tmp/kws_dscnn_tiny_int8_exact_parity_20260410.log`

## 5. decisive board 证据

### 5.1 `align run` 已完整返回

日志里已经明确出现：

- `kws align replay done: dump=preserved local_only_restored=no`
- `[river][diag] kws align run returned status=0`

这说明：

- 这次不再卡在 alignment replay 内部

### 5.2 `dump meta` 与后续 chunk 命令都已执行

日志里出现：

- `kws tensor dump begin: seq=1 infer=9 ...`
- `kws tensor dump meta: seq=1 feat_hash=0xf6cf59f0 input_hash=0x1bb7980a raw=-52 score=0.296875 q15=9728 ...`
- `kws tensor dump snapshot: seq=1 infer=9 chunks=[feat:253 input:64 output:1]`
- `kws tensor dump output_raw: seq=1 chunk=1/1 hex=cc`
- `kws tensor dump feat_f32: seq=1 chunk=1/253 ...`
- ...
- `kws tensor dump input_raw: seq=1 chunk=64/64 ...`

这证明：

- 当前日志已经完整具备 host replay 所需输入

## 6. Host replay 结果

执行：

```bash
cd /root/ameba-river
python3 tools/kws/replay_board_tensor_dump.py \
  --log /tmp/kws_dscnn_tiny_int8_exact_parity_20260410.log \
  --model /root/kws-trainint/artifacts/exports/student_dscnn_tiny_v2/model.int8.tflite \
  --seq latest
```

得到：

```text
dump_seq=1 infer=9 gate=open
board_meta: input_type=int8 output_type=int8 shape=(1, 40, 101, 1) layout=mels_frames
host_runtime: builtin_ref=no
board_hash: feature=0xf6cf59f0 input=0x1bb7980a
host_hash: feature=0xf6cf59f0 logged_input=0x1bb7980a effective_input=0x1bb7980a source=input_raw
quant_parity: diff_bytes=0/4040 first_diff=[]
board_output: raw=-52 score=0.296875 q15=9728
host_output: raw=-52 score=0.296875
output_parity: bytes_equal=yes raw_equal=yes first_diff=[]
```

## 7. 对拍结论

这次已经可以明确排除：

- 模型文件和板端嵌入 blob 不一致
- `feat_f32` 前端结果漂移
- `input_raw` 量化填充错误
- `output_raw` 板端读出错误
- host replay 模型与板端模型不一致

因此当前 `student_dscnn_tiny_v2_int8_debug` 的结论是：

- 板端部署正确
- host replay 正确
- exact parity 已成立

## 8. 当前性能记录

本次日志中板端典型 `infer_us` 为：

- `last=492733`
- `avg=492784`
- `max=493321`

同一条 `kws perf` 还给出：

- `arena=295764/2048KB`
- `slack=1801388`
- `queue[frames=64 stride=16 win_peak=20]`

因此当前资源观感可以概括为：

- 模型很轻，arena 占用远小于当前 2MB 配额
- 但推理时延仍然明显高于当前 `160 ms` stride 预算

## 9. 当前工程判断

相对旧的 `student_bc_resnet_tiny_v2_int8_debug`：

- 旧模型 INT8 correctness 也成立
- 但板端 `infer_us ≈ 2.34 s`

当前 `student_dscnn_tiny_v2_int8_debug`：

- correctness 同样成立
- 板端 `infer_us ≈ 0.493 s`

这说明：

- 当前 DS-CNN tiny INT8 明显比旧 BC-ResNet tiny INT8 更值得继续保留
- 但从 realtime 角度，它仍未进入可直接上主链的范围

更准确地说，当前它的项目角色是：

- `INT8 correctness baseline`
- `INT8 deployability baseline`

而不是：

- `INT8 realtime-ready candidate`

## 10. 后续建议

当前这条线后续不该再重复花时间证明“接线是不是错了”，而应直接进入下一层问题：

1. 是否还有更轻的 INT8 模型可在当前 runtime 下把 `infer_us` 压到更接近预算
2. 是否需要进一步拆开：
   - `align` 样本耗时
   - live 真实输入耗时
3. 是否要把当前结果写回模型筛选矩阵，明确：
   - correctness 已通过
   - realtime 仍不达标
