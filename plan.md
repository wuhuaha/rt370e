# DS-CNN Branch Execution Plan

Date: 2026-03-27
Branch: `DS-CNN`

## Current Objective

先做完整 `build` 验证，再决定是否继续做运行时迁移或模型集成。

当前这一步的目标不是继续训练，也不是直接落板新模型，而是先确认当前分支在现有代码状态下能够稳定编译，并产出可用镜像。

## Why Build First

近期已经完成两份迁移评估：

- `DSCNN_KWS_TRAINING_PRO_MIGRATION_REPORT_ZH.md`
- `RIVER_OPENWAKEWORD_LAB_MIGRATION_REPORT_ZH.md`

结论已经比较明确：

- `/root/kws-training-pro` 更适合作为方法参考，不适合直接迁现成 student 权重
- `/root/river-openwakeword-lab` 是当前更有价值的外部训练工作区
- 但该工作区当前导出的 student 仍然是 `no-deploy`
- 另外，当前板端 `components/river_voice/river_voice_kws.cc` 的 TFLM resolver 还没有补 `AddPad()`，因此不能在未验证的情况下直接推进 runtime 集成

所以本分支当前最合理的顺序是：

1. 先确认当前工程完整可编译
2. 再确认镜像大小、产物路径、基础集成状态
3. 然后再进入运行时集成决策

## Current Baseline

当前板端 KWS 运行时基线：

- feature contract: `98x40` streaming log-mel
- source file: `components/river_voice/river_voice_kws.cc`
- build entry: `build.md`

当前分支决策约束：

- 不修改 `/root/ameba-rtos-1.2` SDK 源码
- 先保住当前工程 build 基线
- 外部训练工作区继续保留在 `/root/river-openwakeword-lab`

## Immediate Plan

### Phase 0: Full Build Verification

Status: completed

执行标准命令：

```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
```

检查项：

- 编译过程无新增错误
- 产物存在并更新时间戳正常
- 重点产物：
  - `build_RTL8730E/build/project_hp/image/km4_boot_all.bin`
  - `build_RTL8730E/build/project_hp/image/km0_km4_ca32_app.bin`
- 如需要额外打包检查，再核对：
  - `build_RTL8730E/ota_all.bin`

若 build 失败，本分支优先修 build，不进行后续迁移。

本次结果：

- `build` 已通过
- 产物已生成：
  - `build_RTL8730E/km4_boot_all.bin` = `51872`
  - `build_RTL8730E/km0_km4_ca32_app.bin` = `3605856`
  - `build_RTL8730E/ota_all.bin` = `3605888`

### Phase 1: Build Result Review

Status: current

Build 通过后，立即审视：

- 二进制大小是否异常膨胀
- 当前 `DS-CNN` 分支是否已经引入运行时不兼容依赖
- 当前代码是否仍然保持可烧录、可继续联调的状态

本阶段只做“工程可用性”判断，不做模型效果判断。

### Phase 2: Runtime Integration Decision

Status: pending

只有在 `build` 通过之后，才进入以下二选一判断：

1. 保持当前分支作为纯 build-stable 基线，继续补板端运行时观测
2. 开始最小化迁入 `/root/river-openwakeword-lab` 中已经验证过的 student/runtime 资产

若进入迁移阶段，首个硬约束是先解决以下一项：

- 给当前 TFLM resolver 增加 `PAD` 支持
- 或重新导出一个不依赖 `PAD` 的 student 模型

## Explicitly Deferred

以下内容本轮暂不优先：

- 新一轮训练
- teacher / assistant / KD 落地
- `/root/kws-training-pro` 旧 student 权重直接上板
- 为了迁模型而先改 SDK

## Exit Criteria For This Step

只有满足以下条件，本步才算完成：

- `build` 成功
- 核心镜像产物存在
- `.codex/changes.md` 与 `.codex/verification.md` 已更新
- 提交本步结果
