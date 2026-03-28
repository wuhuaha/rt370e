# DS-CNN Branch Execution Plan

Date: 2026-03-27
Branch: `DS-CNN`

## Current Objective

当前优先目标已经从“先 build”切到“板端业务流收口”：

- 唤醒词本地检测
- 唤醒后进入 `XiaoZhi realtime` 会话
- 由当前 `VAD` 辅助音频开闭与上行
- 在线文本/TTS 调试注入代码默认不编入 bin，以降低体积并减少无关运行时分支

当前这一步的目标不是继续训练，而是先把板端运行时边界和镜像体积收紧，再做板端 smoke。

## Why Build First

近期已经完成两份迁移评估：

- `doc/DSCNN_KWS_TRAINING_PRO_MIGRATION_REPORT_ZH.md`
- `doc/RIVER_OPENWAKEWORD_LAB_MIGRATION_REPORT_ZH.md`

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

Status: completed

Build 通过后，立即审视：

- 二进制大小是否异常膨胀
- 当前 `DS-CNN` 分支是否已经引入运行时不兼容依赖
- 当前代码是否仍然保持可烧录、可继续联调的状态

本阶段只做“工程可用性”判断，不做模型效果判断。

本次结论：

- 二进制大小无异常膨胀
  - 与 `xiaozhi` 分支最近一次完整 build 基线一致：
    - `km4_boot_all.bin` = `51872`
    - `km0_km4_ca32_app.bin` = `3605856`
    - `ota_all.bin` = `3605888`
- 当前 `DS-CNN` 分支相对 `xiaozhi` 的差异仍然只是文档与计划层，不包含新的运行时代码改动，因此当前固件行为基线仍可视为 `xiaozhi` 运行时基线
- 当前主应用镜像仍然超出 SDK stock profile 的 app 下载上限
  - app start = `0x08040000`
  - app end = `0x083B0560`
  - 相对 SDK stock `0x08300000` 超出 `722272` 字节
  - 因此继续要求使用项目自定义 profile 与 `tools/river_flash.py`
- 当前 KWS runtime 的 op resolver 仍只注册 `6` 个 op：
  - `Quantize`
  - `Conv2D`
  - `DepthwiseConv2D`
  - `Mean`
  - `FullyConnected`
  - `Logistic`
- 这意味着：
  - 当前 build-stable baseline 没问题
  - 但如果后续导入依赖 `PAD` 的 student 模型，必须先补 resolver 或重新导出 no-pad 模型

评审决定：

- 当前分支先保持为 `build-stable` 基线
- Phase 2 可以开始，但前提是明确采用“最小化 runtime 集成”而不是直接替换当前板端 KWS 路径

### Phase 2: Runtime Integration Decision

Status: completed

只有在 `build` 通过之后，才进入以下二选一判断：

1. 保持当前分支作为纯 build-stable 基线，继续补板端运行时观测
2. 开始最小化迁入 `/root/river-openwakeword-lab` 中已经验证过的 student/runtime 资产

若进入迁移阶段，首个硬约束是先解决以下一项：

- 给当前 TFLM resolver 增加 `PAD` 支持
- 或重新导出一个不依赖 `PAD` 的 student 模型

本次决策：

- 采用“最小化 runtime 集成”路线
- 不直接删除当前基线模型
- 在 `DS-CNN` 分支中把 `/root/river-openwakeword-lab` 的 `round6 targeted` student 作为默认实验模型接入
- 同时保留基线模型回退开关，避免把实验模型硬编码成不可逆替换

本次落地：

- 在 `Kconfig` 中新增 KWS 模型变体选择
  - `RIVER_KWS_MODEL_VARIANT_BASELINE`
  - `RIVER_KWS_MODEL_VARIANT_ROUND6_TARGETED_EXPERIMENTAL`
- 在 `prj.conf` 中将 `DS-CNN` 分支默认切到 `RIVER_KWS_MODEL_VARIANT_ROUND6_TARGETED_EXPERIMENTAL=y`
- 将 `round6 targeted` 导出的 `int8 TFLite` 生成嵌入式头文件：
  - `components/river_voice/generated/xiaou_student_round6_targeted_int8_model_data.h`
  - `components/river_voice/generated/xiaou_student_round6_targeted_int8_metadata.json`
- 在 `river_voice_kws.cc` 中完成运行时接入：
  - 支持按配置选择 baseline / round6 student
  - resolver 增加 `AddPad()`
  - op capacity 从 `6` 调整到 `7`
  - 启动日志增加 `variant=%s`

本次结果：

- 完整 build 已通过
- 当前默认实验模型已真实编入固件
- 通过 `strings` 可见固件中包含：
  - `round6_targeted_experimental`
- 新镜像尺寸：
  - `km4_boot_all.bin` = `51872`
  - `km0_km4_ca32_app.bin` = `3573088`
  - `ota_all.bin` = `3573120`
- 相对上一版 baseline：
  - 主应用镜像缩小 `32768` 字节
- 仍然超过 SDK stock app 上限，但超出量下降到 `689504` 字节
  - 继续要求使用项目自定义 profile 与 `tools/river_flash.py`

当前判断：

- 工程上：已经具备板端编译落地条件
- 模型质量上：仍然只能视为实验模型，不可作为 deploy 默认结论

### Phase 3: Debug-Path Compile Gating

Status: completed

目标：

- 保留目标业务流：`wake word -> XiaoZhi realtime session`
- 保留当前 `VAD` 辅助的云音频桥
- 裁掉与目标流无关的在线文本/TTS 调试注入逻辑

本次落地：

- 在 `Kconfig` 中新增两个编译期开关：
  - `RIVER_CLOUD_TEXT_DEBUG_EN`
  - `RIVER_INTERACTION_DIAG_EN`
- 在 `prj.conf` 中将这两个开关默认设为 `n`
- `river_interaction_diag.c` 改为按开关二选一：
  - 开启时编译真实诊断实现
  - 关闭时编译 `river_interaction_diag_stub.c`
- `river_diag_cmd.c` 按开关裁掉以下命令分支：
  - `river echo`
  - `river tts`
  - `river interaction ...`
- `river_online_control_echo()` 改为在调试关闭时直接返回 `RIVER_ERR_UNSUPPORTED`
- `river device ...` 仍然保留，并直接走设备控制服务，不再依赖本地 interaction diag

本次结果：

- 完整 build 已通过
- 运行时仍保留：
  - 本地 `DS-CNN` 唤醒
  - `XiaoZhi realtime` 会话链
  - `VAD probe` 音频桥
  - `MCP -> device control` 落地
- 调试态标识已进入固件：
  - `interaction_diag=compiled=no`
  - `online control service init: text_debug=%s`
- 新镜像尺寸：
  - `km4_boot_all.bin` = `51872`
  - `km0_km4_ca32_app.bin` = `3564896`
  - `ota_all.bin` = `3564928`
- 相对上一版 `DS-CNN` 运行时实验固件：
  - 主应用镜像再缩小 `8192` 字节
- 关键对象变化：
  - `river_interaction_diag.o` 由约 `80K` 降为 `river_interaction_diag_stub.o` 约 `7.8K`
  - `river_diag_cmd.o` 由约 `35K` 降到约 `29K`

当前判断：

- 这一步已经把“在线 ASR/TTS 调试注入层”和“目标业务流”分离开
- 下一步应直接进入板端 smoke，而不是继续往 bin 里塞新的调试逻辑

## Explicitly Deferred

以下内容本轮暂不优先：

- 新一轮训练
- teacher / assistant / KD 落地
- `/root/kws-training-pro` 旧 student 权重直接上板
- 为了迁模型而先改 SDK

## Next Step

下一步应进入板端 smoke：

- 烧录当前 `DS-CNN` 分支固件
- 观察启动日志中的 `variant=round6_targeted_experimental`
- 验证 `interaction_diag=compiled=no`
- 验证本地 `VAD + KWS` 链路是否正常启动
- 验证唤醒后是否进入 `XiaoZhi realtime` 会话，并由当前 `VAD` 辅助音频上行
- 再决定是否继续做唤醒实测和阈值/策略微调

## Exit Criteria For This Step

只有满足以下条件，本步才算完成：

- `build` 成功
- 核心镜像产物存在
- `.codex/changes.md` 与 `.codex/verification.md` 已更新
- 提交本步结果
