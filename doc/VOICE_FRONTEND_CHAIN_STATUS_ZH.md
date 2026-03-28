# 当前前端/AFE链路状态说明

## 1. 目的

本文用于明确当前项目里“实际使用中的前端链路”和“代码已经支持但未默认作为稳定产品链使用的链路”。

这里的“AFE”不再特指 SDK `aivoice_afe`，而是泛指当前语音前端处理链：
- 多通道采集
- 参考信号接入
- 波束成形 / AEC
- VAD 前预处理

## 2. 结论总览

当前项目需要分 3 层理解：

1. 编译选中的 profile
2. 运行时真正生效的输出链
3. 代码支持但不应与稳定主链混淆的实验链/历史链

当前分支 `debug/webrtc-aec` 下：

- 编译选中的 profile：`fixed_dsb_webrtc_aecm`
- 当前大多数实际运行场景下真正生效的输出：`fixed_dsb fallback`
- 稳定可对照基线：`asr_mainline`

## 3. 当前实际使用中的链路

### 3.1 编译选中的当前 profile

当前 `prj.conf` 选中的是：

- `CONFIG_RIVER_VOICE_PREPROC_PROFILE_FIXED_DSB_WEBRTC_AECM=y`
- `CONFIG_RIVER_WEBRTC_AECM_EXPERIMENT_EN=y`

因此，当前分支默认编译的是实验 profile：

```text
capture(3ch: mic0 + mic1 + ref)
-> webrtc_aecm(experimental, gated)
-> fixed_dsb
-> silero_vad
-> streaming asr
```

相关代码：
- [prj.conf](/root/ameba-river/prj.conf)
- [river_voice_profile.c](/root/ameba-river/components/river_voice/river_voice_profile.c)
- [river_voice_capture.c](/root/ameba-river/components/river_voice/river_voice_capture.c)
- [river_voice_preproc_fixed_dsb.c](/root/ameba-river/components/river_voice/river_voice_preproc_fixed_dsb.c)

### 3.2 运行时真正大概率生效的链路

虽然实验 profile 已被选中，但 `AEC` 不是常开。

当前运行时只有在以下条件同时满足时，才会真正使用 `AEC` 输出：

- `PlaybackService` 处于活动态
- `InteractionState` 允许 AEC
- `ref_activity == active`

门控逻辑见：
- [river_voice_runtime_policy.c](/root/ameba-river/components/river_voice/river_voice_runtime_policy.c)

因此，在“当前没有真实播放内容”或“参考未活跃”的情况下，实验链虽然存在，但真正输出的仍然是：

```text
capture(3ch)
-> fixed_dsb fallback
-> silero_vad
-> streaming asr
```

也就是说：

- 当前代码里“名义上的前端”是 `fixed_dsb_webrtc_aecm`
- 当前大多数实际运行场景里的“真实输出前端”仍然是 `fixed_dsb`

## 4. 当前代码明确支持的链路

### 4.1 稳定主链：`asr_mainline`

这是当前稳定、可作为产品基线和 A/B 对照的主链：

```text
capture(2ch: mic0 + mic1)
-> fixed_dsb
-> silero_vad
-> streaming asr
```

特点：
- 双麦：`AMIC1 + AMIC3`
- 固定延迟求和波束成形
- 无 AEC 实验逻辑污染
- 适合作为所有新实验的参考链

相关代码：
- [river_voice_profile.c](/root/ameba-river/components/river_voice/river_voice_profile.c)
- [river_voice_preproc_fixed_dsb.c](/root/ameba-river/components/river_voice/river_voice_preproc_fixed_dsb.c)

### 4.2 实验链：`fixed_dsb_webrtc_aecm`

这是当前保留在分支中的 WebRTC AECM 实验链：

```text
capture(3ch: mic0 + mic1 + ref)
-> webrtc_aecm(experimental, gated)
-> fixed_dsb
-> silero_vad
-> streaming asr
```

特点：
- 输入模型已收敛为 `mic0 + mic1 + ref`
- `ref` 来自 `native_capture_ch3`
- `AEC` 经过运行时门控
- 任意失败时回退到统一延迟时间基下的 `fixed_dsb`

相关代码：
- [river_voice_preproc_fixed_dsb.c](/root/ameba-river/components/river_voice/river_voice_preproc_fixed_dsb.c)
- [river_voice_webrtc_aecm_adapter.c](/root/ameba-river/components/river_voice/river_voice_webrtc_aecm_adapter.c)
- [river_voice_runtime_policy.c](/root/ameba-river/components/river_voice/river_voice_runtime_policy.c)

### 4.3 参考服务链：`playback_ring`

虽然当前实验 AEC 输入模型已经切到 `native_capture_ch3`，但代码里仍然保留并支持软件参考服务链：

```text
playback pcm
-> PlaybackService
-> ReferenceService
-> playback_ring
-> preproc / echo test / diagnostics
```

这条链当前主要用于：
- 播放过程中的软件参考导出
- `audio echo` 诊断链
- 历史方案保留与调试

相关代码：
- [river_playback_service.c](/root/ameba-river/components/river_voice/river_playback_service.c)
- [river_reference_service.c](/root/ameba-river/components/river_voice/river_reference_service.c)
- [river_voice_ref.c](/root/ameba-river/components/river_voice/river_voice_ref.c)

## 5. 当前不再使用的历史 SDK AFE 链路

当前产品主链已经不再使用 SDK 的 `aivoice_afe` 作为主前端。

也就是说，以下 SDK 前端能力目前都不在当前主链中：

- SDK AEC
- SDK BF
- SDK NS
- SDK AFE 主处理链

当前主链已经收敛为纯本地软件前端：

```text
fixed_dsb + silero_vad
```

这也是当前 `m3-asr-baseline-fixed-dsb` 的核心方向。

## 6. 当前链路状态对照表

| 维度 | 当前状态 |
| --- | --- |
| 稳定产品基线 | `capture(2ch) -> fixed_dsb -> silero_vad -> streaming asr` |
| 当前编译选中 profile | `fixed_dsb_webrtc_aecm` |
| 当前大概率实际生效输出 | `fixed_dsb fallback` |
| 当前 AEC 输入模型 | `mic0 + mic1 + ref(native_capture_ch3)` |
| 当前软件参考服务 | `playback_ring`，仍保留并可用 |
| 当前 SDK AFE 是否在用 | 不在当前主链中使用 |

## 7. 推荐使用方式

### 7.1 做产品功能验证时

优先使用稳定主链：

```text
asr_mainline
```

原因：
- 可重复
- 易对比
- 不受实验 AEC 门控影响

### 7.2 做 AEC 验证时

使用实验 profile：

```text
fixed_dsb_webrtc_aecm
```

但需要明确：
- 没有真实播放时，`AEC` 不会真正进入工作态
- 此时实验 profile 实际只是在跑 `fixed_dsb fallback`

## 8. 后续建议

建议后续继续按下面顺序推进：

1. 保持 `asr_mainline` 作为稳定主链
2. 仅在实验 profile 下验证 WebRTC AECM
3. 明确 `PlaybackService / ReferenceService / InteractionState` 的门控关系
4. 后续如引入 `wake_profile / barge_in_profile / wake-guided BF`，继续沿用 profile 化路线，不污染稳定主链
