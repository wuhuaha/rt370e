# 最新 ADK INT8 / INT16 量化可用性复核

日期：2026-04-08

## 1. 复核目标

本次复核要回答两个问题：

1. 芯片侧所说“最新 ADK 已没有 INT8 量化优化问题”是否能从当前可检查到的源码状态得到支持。
2. 在当前 `ameba-river` KWS 接入链路下，`INT8` 和 `INT16` 是否已经可以作为可部署目标。

本次复核坚持两个边界：

- 不直接改动当前正在使用且已带本地补丁的 `/root/ameba-rtos-1.2` 工作区。
- 不破坏现有板端 / 本机对拍链路；所有结论都以已有板端 correctness 调试经验为前提。

## 2. 本次实际核对的对象

### 2.1 SDK / 子模块版本

本次实际核对到的对象如下：

| 对象 | 路径 | 分支 / 引用 | 提交 |
| --- | --- | --- | --- |
| 当前在用 SDK | `/root/ameba-rtos-1.2` | `release/v1.2` | `8624cbeccf840c929db1624e05cc5b681024a3bf` |
| 干净临时 ADK clone | `/tmp/ameba-rtos-1.2-latest` | `release/v1.2` | `8ef72a545c384ec439eef9a200baf4f569e21a73` |
| 在用 SDK 的远端主线引用 | `/root/ameba-rtos-1.2` | `origin/master` | `2def66020a2bd6b37894e8dc8341c49130ca8405` |
| 当前子模块已抓到的主线引用 | `/root/ameba-rtos-1.2/component/tflite_micro` | `origin/main` | `8b38d3dac9ea733e93ad73c2b637ef1a28753fb3` |

### 2.2 关键结论先给出

- `release/v1.2` 的最新 superproject 本身并没有带来新的 `tflite_micro` 量化内核版本。
- 本地已经抓到的 `origin/master -> tflite_micro origin/main`，在本次关心的 4 个关键文件上，与 `release/v1.2` 指向的 `dbda29aa7240ad14cf21cf3636ff2792a05ddcc1` 也没有差异。
- 因此，基于当前可复核到的官方代码，不能得出“官方最新 ADK 已经吸收了我们当前依赖的 INT8 修复/绕行补丁”的结论。

## 3. 为什么说“只升级 ADK superproject”还不够

### 3.1 最新 `release/v1.2` 仍指向旧的 `tflite_micro`

无论是：

- 当前在用 SDK `8624cbe...`
- 还是单独拉下来的干净 `release/v1.2` 最新 superproject `8ef72a5...`

它们的 `component/tflite_micro` 子模块指针都还是：

- `dbda29aa7240ad14cf21cf3636ff2792a05ddcc1`

这意味着：

- 如果只把 ADK superproject 从当前 `release/v1.2` 更新到最新 `release/v1.2`
- 但不改变 `tflite_micro` 子模块指针

那么量化内核代码不会发生变化，`INT8 / INT16` 的真实行为也不会因为 superproject 更新而自动改变。

## 4. 当前本地 dirty SDK 里，实际在依赖哪些量化补丁

当前在用 SDK 的 `component/tflite_micro` 工作区是 dirty 的，且这些改动不是“无关噪声”，而是直接影响我们此前的 KWS 板端 correctness 与量化行为。

### 4.1 `conv.cc`：INT8 CA32 优化卷积被显式绕回 reference

当前 dirty 文件：

- `/root/ameba-rtos-1.2/component/tflite_micro/tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/conv.cc`

关键位置：

- 当前 dirty 工作区：`248-267`
- 官方 `dbda29a` 基线：`253-279`

现状对比：

- 官方基线里，`need_im2col` 场景仍会走优化 `Im2col + GEMM` 路径。
- 当前 dirty 工作区里，代码直接写明：
  - `The current CA32 int8 conv optimized path is not reliable`
  - 然后无条件回退到 `tflite::reference_integer_ops::ConvPerChannel(...)`

含义非常直接：

- 当前能保证模型输出正确的 INT8 卷积路径，依赖的是 reference fallback。
- 这也是为什么此前 INT8 上板后 correctness 可以做出来，但实时性没有得到应有量化收益。

### 4.2 `depthwise_conv.cc`：INT8 depthwise 也被显式绕回 reference

当前 dirty 文件：

- `/root/ameba-rtos-1.2/component/tflite_micro/tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/depthwise_conv.cc`

关键位置：

- 当前 dirty 工作区：`141-159`
- 官方 `dbda29a` 基线：`141-166`

现状对比：

- 官方基线默认走 `optimized_integer_ops::depthwise_conv::DepthwiseConvGeneral(...)`
- 当前 dirty 工作区注释明确说明当前平台该优化路径对 wakeword 模型不可靠，改为：
  - `reference_integer_ops::DepthwiseConvPerChannel(...)`

这再次说明：

- 当前板端 INT8 correctness，并不是建立在“优化路径已经修复”的前提上。
- 而是建立在“为 correctness 暂时禁用优化路径”的前提上。

### 4.3 `reduce_common.cc`：补了 INT8 GAP / MEAN 专用路径

当前 dirty 文件：

- `/root/ameba-rtos-1.2/component/tflite_micro/tensorflow/lite/micro/kernels/reduce_common.cc`

关键位置：

- 当前 dirty 工作区新增：
  - `IsChannelGapMeanInt8(...)`：`142-176`
  - `EvalChannelGapMeanInt8(...)`：`178-224`
  - `EvalMeanHelper(...)` 中挂接该 fast path：`306-309`
- 官方 `dbda29a` 基线中不存在这段 INT8 GAP / MEAN 特化实现。

这部分不是性能锦上添花，而是当前我们接 student 类量化模型时的关键正确性补丁之一。

### 4.4 `im2col_utils.h`：修了 patch buffer 长度计算

当前 dirty 文件：

- `/root/ameba-rtos-1.2/component/tflite_micro/tensorflow/lite/micro/kernels/ameba-aiot/amebasmart_ca32/im2col_utils.h`

关键位置：

- 当前 dirty 工作区：`140-150`、`177-188`
- 官方 `dbda29a` 基线：`138-150`、`175-188`

现状对比：

- 官方基线把 `ExtractPatchIntoBufferColumn(...)` 的 buffer length 传成 `output_depth`
- 当前 dirty 工作区改成：
  - `single_buffer_length = kheight * kwidth * input_depth`

这说明当前 dirty 工作区里还带着一个 im2col 相关修正，而该修正并不在本次能复核到的官方 `release/v1.2` / 本地主线引用差异里。

## 5. “官方主线已修好”这件事，目前为什么还不能成立

### 5.1 本地已抓到的 `origin/master` 也没有吸收上述关键差异

本次对以下 4 个文件执行了：

- `git diff dbda29a..origin/main -- <file>`

结果都是空输出：

- `conv.cc`
- `depthwise_conv.cc`
- `reduce_common.cc`
- `im2col_utils.h`

这表示至少在本机当前已经抓到的官方主线引用里：

- `origin/master` 对应的 `tflite_micro origin/main`
- 并没有把上面这些我们当前实际依赖的量化修正吸收进去

所以当前能够成立的判断是：

- “本机可核对到的官方 ADK 代码，还不足以证明 INT8 优化路径已经可靠可用”

而不是：

- “官方最新 ADK 已确认修好，已经可以安全去掉本地补丁”

### 5.2 当前无法进一步扩大这个结论的边界

本次尝试继续对 GitHub 上最新远端内容做直接拉取，但网络侧 TLS 连接不稳定，没能成功完成新的完整 fetch / clone。

因此这里必须把边界讲清楚：

- 本文结论对“当前本机已能复核到的官方代码状态”成立。
- 如果芯片侧所说“最新 ADK”指的是一个我们本机尚未抓到的具体提交、内部分支、补丁包或尚未同步到 `release/v1.2` / `origin/master` 的版本，那么需要对方给出：
  - superproject 精确 commit / tag
  - `component/tflite_micro` 精确 submodule commit

没有这两个精确版本号，就无法严谨地把“口头说已修好”转化成我们可验证、可复现、可上板对拍的工程事实。

## 6. INT8 目前到底算不算“可用”

要把“可用”拆成两个层面：

### 6.1 correctness 角度

`INT8` 在当前项目里并非完全不可用。

我们之前已经证明：

- 通过保留现有板端 / 本机对拍链路
- 并结合当前 dirty SDK 中的 reference fallback + GAP/MEAN 修补

是可以把 INT8 模型做成板端 / 本机一致的。

因此从 correctness 角度说：

- `INT8` 是“可调通”的
- 但前提是保留当前已验证的本地补丁和对拍机制

### 6.2 realtime / 优化收益角度

当前 `INT8` 还不能称为“优化路径已经可用”。

原因不是量化本身无效，而是：

- 当前卷积与 depthwise 的 INT8 CA32 优化路径被本地补丁显式关闭
- 实际跑的是 reference integer kernel

因此当前观察到的现象才会是：

- INT8 correctness 可以成立
- 但实时性提升非常有限，甚至几乎没有达到预期

所以更准确的说法应该是：

- 当前 `INT8` 只达到了“correctness 可验证”
- 还没有达到“官方优化路径已经可靠、性能收益已兑现”

## 7. INT16 目前是否可用

结论更明确：当前 `ameba-river` KWS 接入链路下，`INT16` 仍然不可作为可部署目标。

原因不是单一点，而是整条接入链路尚未支持。

### 7.1 KWS 接入层只接受 `float32 / uint8 / int8`

文件：

- `/root/ameba-river/components/river_voice/river_voice_kws.cc`

关键约束：

- `770-783`：tensor type name 只显式处理 `float32 / uint8 / int8`
- `851-863`：schema tensor type 到 `TfLiteType` 的映射只接受 `FLOAT32 / UINT8 / INT8`
- `1158-1172`：effective tensor type 只允许 `uint8 / int8 / float32`
- `1175-1184`：storage bytes 只为 `float32 / uint8 / int8` 定义
- `3683-3688`：初始化阶段显式拒绝任何非 `uint8 / int8 / float32` 的 input/output tensor

也就是说：

- 即使底层某些 TFLM kernel 已经支持 `INT16`
- 当前 `ameba-river` KWS 接入层也会先把 `INT16` 模型挡在外面

### 7.2 导出工具也没有 `INT16` 出口

文件：

- `/root/ameba-river/tools/kws/export_bc_resnet_tflite.py`

关键位置：

- `36-40`

当前导出参数只允许：

- `float32`
- `int8`

没有 `int16` 导出模式。

### 7.3 本机回放 / 对拍工具也没接 `INT16`

文件：

- `/root/ameba-river/tools/kws/replay_board_tensor_dump.py`

关键位置：

- `93-100`

当前只识别：

- `int8`
- `uint8`
- `float32`

同样没有 `INT16`。

### 7.4 因此 INT16 的真实状态

当前 `INT16` 的状态不是“可能慢、不建议”，而是：

- 当前项目接入链路压根没有打通
- 即便底层算子局部支持，也还不能作为当前板端交付目标

## 8. 最终判断

### 8.1 INT8

当前可确认的结论是：

- 不能仅凭“升级到最新 `release/v1.2`”就认为 `INT8` 优化问题已消失
- 当前本机可核对到的官方 `origin/master / origin/main` 也还不能证明这些修复已经正式吸收
- 现阶段已验证可行的方案仍然是：
  - 保留当前 dirty SDK 中已经验证过的 correctness 补丁
  - 保留板端 / 本机对拍链路
  - 在这个基础上继续筛模型

### 8.2 INT16

当前 `INT16` 结论更明确：

- 在 `ameba-river` 现有 KWS 接入、导出、回放工具链下，`INT16` 还不可部署
- 这不是换 ADK 即可自动解决的问题

## 9. 后续建议

如果要继续严谨推进“官方量化路径是否已经可用”的验证，建议按下面顺序做：

1. 让芯片侧提供精确版本号：
   - ADK superproject commit / tag
   - `component/tflite_micro` submodule commit
2. 在单独的干净 SDK 目录复现，不污染当前在用 dirty SDK。
3. 先做源码级比对：
   - `conv.cc`
   - `depthwise_conv.cc`
   - `reduce_common.cc`
   - `im2col_utils.h`
4. 若源码级确认官方已吸收修复，再做板端验证：
   - 不移除现有板端 / 本机对拍机制
   - 先验证 correctness
   - 再比较 `infer_us` 是否真的优于当前 reference fallback 方案
5. 在 `INT16` 方向上，如果未来真要尝试，需要先补齐项目链路支持：
   - KWS tensor type 接入
   - storage / quant param / 输出解析
   - 导出工具
   - 本机回放 / 对拍工具

在这之前，不建议把 `INT16` 视为当前可交付方向。

## 10. Clean SDK 下的实际 INT8 上板复测

上面几节解决的是“源码状态能否证明官方已修好”。这一节补上更关键的一环：

- 用干净 SDK 真正编译、刷板、起机
- 看 `student_bc_resnet_tiny_v2_int8_debug` 在 clean SDK 下的真实板端行为

### 10.1 本次 clean SDK 复测前提

- 项目仓已补充 `AMEBA_SDK_ROOT` 覆盖能力，确保：
  - `env.sh`
  - `tools/river_flash.py`
  - `tools/generate_rdev.py`
  - `components/river_cloud/CMakeLists.txt`
  都可以显式指向非默认 SDK
- 本次 clean SDK 使用：
  - `/tmp/ameba-rtos-1.2-latest`
  - `release/v1.2`
  - commit `8ef72a545c384ec439eef9a200baf4f569e21a73`
- 固件配置切到：
  - `CONFIG_RIVER_KWS_MODEL_VARIANT_STUDENT_BC_RESNET_TINY_V2_INT8_DEBUG=y`
  - `CONFIG_RIVER_KWS_SCORE_THRESHOLD_Q15=9444`
  - `CONFIG_RIVER_KWS_TENSOR_ARENA_KB=2048`

### 10.2 clean SDK 构建结果

使用 clean SDK 路径构建时，项目可以正常完成编译：

- 构建命令：
  - `bash -lc 'mkdir -p /tmp/ccache-tmp && export CCACHE_TEMPDIR=/tmp/ccache-tmp && export AMEBA_SDK_ROOT=/tmp/ameba-rtos-1.2-latest; source env.sh; python3 /tmp/ameba-rtos-1.2-latest/ameba.py build -p'`
- 结果：
  - `Build done`

这说明：

- `AMEBA_SDK_ROOT` 覆盖链路本身是通的
- clean SDK 至少能把当前 INT8 debug 变体编译成可烧录镜像

### 10.3 clean SDK 刷板结果

实际刷板时观察到两种状态：

1. 直接刷：
   - `env AMEBA_SDK_ROOT=/tmp/ameba-rtos-1.2-latest python3 tools/river_flash.py -p /dev/ttyUSB0`
   - 如果板子不在下载态，会报：
     - `Flashloader download fail: ErrType.SYS_PROTO`
2. 先走已有 monitor 命令把板子切到下载态，再刷：
   - `printf 'reboot uartburn\r' > /dev/ttyUSB0`
   - 再执行同一个 `river_flash.py`
   - 可以成功刷入：
     - `Finished PASS`

这一步说明：

- clean SDK 生成的镜像可以被正常烧写
- 烧录工具链没有因为 SDK 覆盖而失效

### 10.4 刷入后的实际串口行为

刷入成功后，板子没有进入此前已验证过的 `ameba-river` 正常 runtime / monitor 文本日志状态。

使用最原始 `cat /dev/ttyUSB0` 抓取时，现象是：

- 普通 monitor 命令只有回显
- 看不到熟悉的：
  - `ameba-river boot`
  - `kws init`
  - `wifi`
  - `river ...` 文本日志

进一步用 clean SDK 自带 monitor 工具做短探测：

- 命令：
  - `python3 /tmp/ameba-rtos-1.2-latest/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000 --debug`
- monitor 会先发：
  - `AT+LIST`
- 实际观测到：
  - 没有任何 monitor 命令列表返回
  - 串口持续输出的不是文本，而是大量：
    - `00`
    - `00 00`
    - `00 00 00`
    - `00 00 00 00`

也就是说，这次 clean SDK + INT8 实测的板端形态不是：

- “能启动，只是 INT8 没提速”

而更接近：

- “烧录后串口进入异常空字节流状态，没有进入我们已知的正常应用日志/monitor 状态”

### 10.5 这次 clean SDK 实测能得出的结论

这次板端复测把结论进一步收紧了：

- clean SDK `release/v1.2` 不仅没有证明 INT8 优化路径已经恢复可用
- 在当前 `student_bc_resnet_tiny_v2_int8_debug` 实测里，板端行为甚至比 dirty SDK 下的“reference fallback 但 correctness 可对拍”更差
- 当前 clean SDK 路径下，这个 INT8 变体还没有达到“可进入正常 runtime、可做端侧/本机对拍”的最低门槛

因此当前最稳妥的工程判断是：

- clean official SDK：
  - 还不能替代当前 dirty SDK 上那套已验证 correctness 的量化补丁路径
- 当前 INT8 的可靠调试基础仍然是：
  - 保留现有板端 / 本机对拍代码
  - 保留当前 dirty SDK 中已经验证过的 correctness 兜底补丁
  - 不把“clean SDK 能编译 + 能烧录”误判成“INT8 已经可用”
