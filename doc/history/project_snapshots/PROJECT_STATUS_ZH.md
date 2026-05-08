# 项目当前状态说明

## 1. 当前分支与目标

- 当前开发分支：`DS-CNN`
- 当前目标：先完成板端 `build + flash + boot smoke`，确认实验 `DS-CNN` 模型的运行时接入稳定
- 当前策略：不继续扩展训练链，不改 SDK，先把板端实验固件验证清楚

## 2. 当前板端语音主链

当前本地唤醒主链：

```text
capture -> fixed_dsb -> silero_vad(gate) -> dscnn_kws
```

当前主链特征：
- 双麦阵列：`AMIC1 + AMIC3`
- 阵列间距：`50mm`
- 前端预处理：`Fixed Delay-and-Sum Beamforming`
- VAD：`Silero VAD`
- KWS：`DS-CNN`
- 当前默认实验模型：`round6_targeted_experimental`
- 当前仍保留 baseline 模型回退开关

## 3. 当前工程状态

当前 `DS-CNN` 分支已经完成：

- 训练相关代码、录音服务、数据工件外置到 `/root/river-openwakeword-lab`
- 仓库内保留兼容 symlink，降低固件仓复杂度
- `round6 targeted` student 的 `int8 TFLite` 已落入板端运行时
- `river_voice_kws.cc` 已补 `PAD` resolver 支持
- `Kconfig + prj.conf` 已支持 baseline / experimental 模型切换

最新一次完整编译产物：
- `build_RTL8730E/km4_boot_all.bin` = `51872`
- `build_RTL8730E/km0_km4_ca32_app.bin` = `3573088`
- `build_RTL8730E/ota_all.bin` = `3573120`

## 4. 当前约束

- 当前实验模型只具备工程接入意义，还不是可部署结论
- 当前 app 镜像仍超过 SDK stock `RTL8730E` app window
- 继续要求使用项目自定义 profile 与 `tools/river_flash.py`
- 当前优先级是板端 smoke，不是下一轮训练

## 5. 当前最重要文档

- `README.md`
- `plan.md`
- `build.md`
- `doc/RIVER_OPENWAKEWORD_LAB_MIGRATION_REPORT_ZH.md`
- `doc/DSCNN_KWS_TRAINING_PRO_MIGRATION_REPORT_ZH.md`
- `doc/VOICE_FRONTEND_CHAIN_STATUS_ZH.md`
- `doc/KWS_PIPELINE_ZH.md`
- `doc/README.md`

## 6. 当前下一步建议

当前最合理的下一步：

1. 用项目自定义 profile 完成烧录
2. 观察启动日志中是否出现 `variant=round6_targeted_experimental`
3. 验证本地 `VAD + KWS` 是否真正起链
4. 若运行时正常，再决定是否继续做板端唤醒实测与阈值调整
