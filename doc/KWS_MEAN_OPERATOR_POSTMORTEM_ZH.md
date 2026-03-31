# KWS `MEAN` 算子踩坑复盘

日期：2026-03-31

## 1. 背景

这轮板端唤醒词切换到 `bc_resnet_best.tflite` 之后，主要阻塞点不是 `ADD`，而是量化 `MEAN`。

模型本身并不特殊。桌面侧检查已经确认，板端运行到的 `MEAN` 都是标准 reduce 场景：

- `axis=[2]`，例如 `[1,20,49,24] -> [1,20,1,24]`
- `axis=[1]`，例如 `[1,20,49,36] -> [1,1,49,36]`
- 可能存在 `axes=[1,2]`

也就是说，这次问题不是“模型导出错了”或“模型用了 TFLM 不支持的奇怪 `MEAN` 形态”，而是当前 Ameba/TFLM 组合在量化 `MEAN` 路径上不稳定。

## 2. 已确认的现象

### 2.1 第一阶段：`builtin_data` / reducer params 不稳定

最早的板端失败是：

- `params != NULL was not true`
- `Node MEAN (number 3) failed to invoke with status 1`

这说明 patched `MEAN` 运行时读到的 reducer 参数并不可靠，至少不能假设 `node->builtin_data` 在该路径上永远完好。

### 2.2 第二阶段：自定义模式匹配过窄

后续出现：

- `river kws mean patch got unsupported reduce pattern`

排查后确认不是模型用了新模式，而是项目侧 patched `MEAN` 对 `keep_dims` 和输出布局的匹配写得太窄，错误地把正常 `axis=[1] / [2]` 量化 `MEAN` 判成了“不支持”。

### 2.3 第三阶段：patched op-data 地址未对齐

继续推进后，第一次真实推理在板端硬 fault：

- `Data abort ... 0x00000221`
- fault PC `0x6035f448`

符号化后落在项目侧 [components/river_voice/river_voice_kws_mean_patch.cc](/root/ameba-river/components/river_voice/river_voice_kws_mean_patch.cc) 的 patched `MEAN` eval 中。进一步确认是 `node->user_data` 指向的 op-data 未按浮点访问要求对齐，导致 VFP 读 `output_scale` 时触发 data abort。

### 2.4 第四阶段：即使 eval 换回 SDK，仍然会死在量化 `MEAN`

在修了 reducer params 和 op-data 对齐后，又把 eval 委托回 SDK `tflite::EvalMeanHelper(...)`，旧的项目侧计算循环不再执行，但板端仍在首次真实推理时崩溃：

- `Data abort ... 0x00001a11`
- fault PC `0x6042f710`
- LR `0x6042f718`

符号化后落在：

- `tflite::reference_ops::QuantizedMeanOrSum<signed char, long>(...)`
- 具体是 `reduce.h` 里清零 `temp_sum[idx]` 的写路径

这说明问题已经不在项目侧自定义计算循环本身，而是在 SDK 量化 `MEAN` 的 scratch-buffer 路径。

## 3. 高置信结论

### 3.1 当前问题不值得继续在旧模型上深挖

当前能确认的链条是：

1. 项目侧 patched `MEAN` 最早确实有 reducer params 生命周期问题
2. 项目侧 patched `MEAN` 后续又确实有对齐问题
3. 这些修完后，SDK 自己的量化 `MEAN` 仍然会在 `temp_sum` 路径 data abort

因此，这轮问题不是单一 bug，而是“当前平台上的量化 `MEAN` 路径整体可信度不足”。

### 3.2 `PrepareMeanOrSumHelper(...)` 不是可靠隔离层

此前多次尝试都还在复用 SDK `PrepareMeanOrSumHelper(...)`。这意味着即使 eval 改成项目自定义逻辑，scratch buffer 申请和布局仍然可能沿用 SDK 路径。

这也是为什么“看起来已经换成自定义 `MEAN`”之后，仍然会不断遇到与 scratch / metadata / arena 相关的问题。

### 3.3 新模型移除 `MEAN` 是更正确的工程决策

现阶段目标是尽快恢复板端稳定唤醒，不是把所有时间继续消耗在一个平台相关、收益很低的 TFLM `MEAN` 适配上。

如果新模型能从拓扑上彻底移除 `MEAN`，那比继续修 patched `MEAN` 更稳，也更符合“板端可验证、小步推进”的要求。

## 4. 这轮踩坑的经验教训

### 4.1 每次 crash 都要重新符号化，不要靠直觉复用旧结论

这轮 fault PC 先后出现过：

- `0x6035f448`
- `0x6035f474`
- `0x6042f710`

它们分别对应不同问题：

- 项目侧未对齐访问
- 项目侧 patched eval 路径
- SDK 量化 `MEAN` scratch 路径

如果只看“都是 `MEAN` 崩了”，很容易把后两个问题误判成同一个根因。

### 4.2 “模型拓扑正常”不等于“平台实现可靠”

桌面侧检查已经证明模型的 `MEAN` 用法是正常的，但板端依然会崩。这类问题不能再从“模型一定有问题”出发，而要优先怀疑：

- TFLM 移植层
- arena / scratch buffer
- 对齐
- builtin data 生命周期

### 4.3 项目侧补丁如果仍依赖 SDK prepare，就不算真正隔离

只替换 eval，不替换 prepare，常常只是把问题从“运算实现”挪到“scratch 规划和元数据生命周期”。这轮排查已经把这个坑踩实了。

### 4.4 板端目标要优先于算子洁癖

从工程角度看，当前最优路径不是证明“我们一定能把 `MEAN` 补丁修好”，而是让模型规避不稳定算子，先把唤醒链路恢复到可持续推进的状态。

## 5. 对接无 `MEAN` 新模型前的明确建议

### 5.1 这几个已有改动可以保留

- `ADD` 已注册，残差模型基础依赖已补齐
- KWS 输入 shape / 布局自适配可以保留
- KWS 相关日志已经更接近真实模型布局

这些都不依赖 `MEAN`，对新模型仍然有价值。

### 5.2 这条经验要带到新模型接入

新模型到位后，优先先做三件事：

1. 桌面侧确认 op 列表里没有 `MEAN`
2. 板端第一次 `AllocateTensors()` 成功
3. 第一次真实 `kws gate open` 后不再出现与 `MEAN` 相关的 fault / `Node MEAN ...` 日志

### 5.3 旧的 patched `MEAN` 先视为过渡代码

在无 `MEAN` 新模型稳定前，不建议立刻删除现有 patched `MEAN` 代码，避免失去回滚基线。

但只要新模型板端跑稳，下一步就应该考虑：

- 彻底移除项目侧 `MEAN` patch
- 删除相关临时日志和针对性兜底
- 把这轮问题封存为历史教训，而不是继续扩散到主路径

## 6. 当前决策

这轮 `MEAN` 问题到这里可以收口，后续不再把主要精力投入到旧模型的量化 `MEAN` 内核修补。

新的工作基线应转向：

- 接入无 `MEAN` 新模型
- 用干净分支做首次板端验证
- 只保留对新模型仍然有实际价值的通用运行时适配
