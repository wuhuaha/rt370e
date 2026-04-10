# `student_dscnn_tiny_v2_int8_debug` 对拍状态与闭环清单

日期：2026-04-10

## 1. 目的

这份文档只回答当前一个非常具体的问题：

- 当前 `student_dscnn_tiny_v2_int8_debug` 板端和本机 exact parity 还差什么
- 哪些问题已经解决
- 现在下一步应该怎么把这条线真正闭环

本文不重复讲所有 KWS 对拍原理，原理与通用流程见：

- [KWS_BOARD_HOST_PARITY_DEBUG_GUIDE_ZH.md](./KWS_BOARD_HOST_PARITY_DEBUG_GUIDE_ZH.md)

## 2. 当前结论

先给结论：

- 当前 `INT8` 的“量化数学正确性”不是主要疑点。
- 历史上 `student_bc_resnet_tiny_v2_int8_debug` 已经完成过板端 / host exact parity。
- 当前新的 `student_dscnn_tiny_v2_int8_debug` 之前真正卡住的，不是模型输出不一致，而是：
  - `river kws align run` 成功返回后
  - monitor / shell 不能继续稳定执行 `river kws dump meta`
  - 因此拿不到完整 dump 做 host replay
- 到 `2026-04-10` 为止，这个“align 后 shell 卡死”问题已经被明显收敛并压下：
  - 移除了 `river_voice_kws_run_alignment_sample(...)` 里落在 shell task 栈上的 `512B` 尾静音局部数组
  - 当前 board 记录已经证明 `align run` 返回后，`dump meta` 与 `align status` 都能继续执行

因此，当前真正剩下的闭环项已经收缩为：

- 用当前最新固件重新抓一份完整 `feat_f32 / input_raw / output_raw`
- 在 host 上跑一次 `student_dscnn_tiny_v2` INT8 replay
- 给出这一个模型自己的 exact parity 结果与性能记录

## 3. 已解决的问题

## 3.1 `align run` 后 monitor 不再立即失效

此前板测中最麻烦的问题是：

- `kws align replay done: ...`
- `[river][diag] kws align run returned status=0`

已经打印，但紧接着出现：

- `[INIC-E] WIFI TRX IPC 4 timeout`

之后再发：

- `river kws dump meta`
- `river kws align status`

只会被串口回显，不会实际执行。

这个问题当前已经被压下。最近一轮板测已经证明：

- `river kws dump meta` 可以执行
- `[river][diag] kws dump meta returned` 可以打印
- `river kws align status` 可以继续执行
- 没再看到同窗口下立刻出现的 `WIFI TRX IPC 4 timeout`

也就是说：

- 之前的核心阻塞点已经不是“INT8 模型导致系统死锁”
- 更像是 shell task 栈过紧导致的 monitor 返回路径不稳定

## 3.2 当前活动模型已经切到 `student_dscnn_tiny_v2_int8_debug`

当前项目活动配置不是旧的 BC-ResNet INT8，而是：

- `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_DSCNN_TINY_V2_INT8_DEBUG=y`

对应代码入口：

- [prj.conf](../prj.conf)
- [components/river_voice/river_voice_kws.cc](../components/river_voice/river_voice_kws.cc)

所以后续任何对拍、性能、结论，都必须明确指向：

- `student_dscnn_tiny_v2_int8_debug`

不能再拿旧的 `student_bc_resnet_tiny_v2_int8_debug` 结果直接代替。

## 3.3 `INT16` 仍然不在当前闭环范围内

当前 KWS 接入层放行的是：

- `uint8`
- `int8`
- `float32`

不包含 `int16`。

所以当前要完成的是：

- `INT8` exact parity 闭环

而不是：

- 顺带把 `INT16` 也一起打通

## 4. 当前还没完成的事项

## 4.1 `student_dscnn_tiny_v2_int8_debug` 的完整 chunk dump 还没正式归档

当前最新稳定日志已经能拿到：

- `kws tensor dump begin`
- `kws tensor dump meta`

但还需要确认完整的：

- `feat_f32`
- `input_raw`
- `output_raw`
- `end`

都已经成功拉回并保存到一份可 replay 的日志中。

没有完整 chunk，就不能正式得出：

- `quant_parity: diff_bytes=...`
- `output_parity: bytes_equal=... raw_equal=...`

## 4.2 这个模型自己的 exact parity 结果还没有最终写实

旧的 BC-ResNet INT8 已经证明过：

- `quant_parity: diff_bytes=0/4040`
- `output_parity: bytes_equal=yes raw_equal=yes`

但这不能直接转移到当前 `student_dscnn_tiny_v2_int8_debug`。

当前这个模型必须单独证明：

- board 输入张量和 host replay 输入张量一致
- board 输出字节和 host 输出字节一致
- `raw / score / q15` 一致

## 4.3 还缺这个模型自己的性能归档

当前历史记录里已经看到它在板端大约：

- `infer_us ≈ 491-493 ms`

但后续正式结论需要与 exact parity 同步归档：

- 当前模型名
- 板端 arena / slack
- `infer_us[last/avg/max]`
- live / align 两种场景是否同量级
- 是否值得继续作为 INT8 候选

## 5. 当前建议的闭环顺序

当前最稳妥的顺序是：

1. 刷入当前 `HEAD` 固件
2. 确认 boot 日志中的 `variant=student_dscnn_tiny_v2_int8_debug`
3. 执行：
   - `river kws debug local on`
   - `river audio probe stop`
   - `river kws debug local off`
   - `river kws align run`
4. 在 `align run` 成功后，手动拉取：
   - `river kws dump meta`
   - `river kws dump chunk feat_f32 ...`
   - `river kws dump chunk input_raw ...`
   - `river kws dump chunk output_raw ...`
5. 在 host 上对同一份 `model.int8.tflite` 做 replay
6. 记录：
   - `quant_parity`
   - `output_parity`
   - `raw / score / q15`
   - `infer_us`

## 6. 现在不该再混淆的几件事

后续讨论中要避免把下面几件事混在一起：

- `INT8` 数值链路正确性
- 当前具体模型 `student_dscnn_tiny_v2_int8_debug` 是否已完成 exact parity
- `align run` 后 monitor 是否稳定
- `INT8` 是否有实时性收益

这四件事分别对应：

1. 历史上旧 INT8 模型已经证明 `INT8` 数值链路本身可做通
2. 当前模型还要自己完成一次 exact parity
3. monitor 稳定性问题已基本被 shell 栈压力修正压下
4. 即使 parity 成功，也仍要单独判断 `infer_us` 是否值得继续

## 7. 当前项目口径

截至本文写入时，当前项目对 `student_dscnn_tiny_v2_int8_debug` 的正确口径应当是：

- “之前阻塞 exact parity 的 shell / monitor 返回路径问题已基本解决”
- “当前剩余工作不是再猜系统为何卡死，而是尽快补齐完整 dump、host replay 与性能归档”

这意味着下一步工作的重心已经从：

- “定位为什么 `align run` 后命令失效”

转到：

- “把当前模型的 exact parity 和性能记录真正做完”
