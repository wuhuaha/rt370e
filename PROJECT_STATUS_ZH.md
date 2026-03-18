# 项目当前状态说明

## 1. 当前分支与基线

- 当前开发分支：`debug/webrtc-aec`
- 稳定 ASR 基线 tag：`m3-asr-baseline-fixed-dsb`
- 稳定 ASR 基线提交：`e40e017`
- 当前 AEC 实验分支保存点：`b7684da`

## 2. 当前稳定主链

当前默认、稳定、可作为 A/B 参考的主链：

```text
capture -> fixed_dsb -> silero_vad -> streaming asr
```

当前稳定主链特征：
- 双麦阵列：`AMIC1 + AMIC3`
- 阵列间距：`50mm`
- 默认前端：`Fixed Delay-and-Sum Beamforming`
- 默认检测：`Silero VAD`
- 默认识别：在线流式 ASR

## 3. 当前实验链

当前分支中的 AEC 实验链：

```text
capture(3ch: mic0 + mic1 + ref)
-> webrtc_aecm(experimental, gated)
-> fixed_dsb
-> silero_vad
-> streaming asr
```

说明：
- AEC 仅在实验 profile 下启用
- AEC 输入模型已收敛为 `mic0 + mic1 + ref`
- AEC 是否真正生效，不再只靠参考能量判断，而是由：
- `PlaybackService`
- `InteractionState`
- `ReferenceActivity`
  联合门控

## 4. 当前在线 TTS 状态

当前分支已接入讯飞在线 TTS WebSocket 接口：

```text
river tts <text>
-> river_cloud_adapter_submit_text()
-> iflytek ws tts
-> PlaybackService
-> speaker
```

说明：

- 使用 `ws://tts-api.xfyun.cn/v2/tts`
- 复用当前项目 `AppID / APIKey / APISecret`
- 鉴权算法按 TTS 官方文档单独实现
- 当前支持板端直接播报短文本

关键文件：

- `components/river_cloud/river_tts_iflytek_ws.c`
- `include/river/river_tts_iflytek_credentials.h`
- `components/river_cloud/river_ws_dispatch.c`
- `tools/tts/iflytek_tts_ws_debug.py`

## 5. 当前代码结构重点

### 5.1 核心语音组件

- `components/river_voice/river_voice_capture.c`
- `components/river_voice/river_voice_preproc.c`
- `components/river_voice/river_voice_preproc_fixed_dsb.c`
- `components/river_voice/river_voice_detector.c`
- `components/river_voice/river_voice_vad_probe.c`

### 5.2 新增的服务化边界

- `components/river_voice/river_playback_service.c`
- `components/river_voice/river_reference_service.c`
- `components/river_core/river_interaction_state.c`
- `components/river_voice/river_voice_profile.c`
- `components/river_voice/river_voice_runtime_policy.c`

## 6. 当前代码整理原则

- 稳定主链与实验链分离
- 播放状态不再主要依赖能量推断
- 参考路径有独立服务边界
- 交互状态有独立状态管理
- AEC 仅作为实验能力接入，不污染默认主链

## 7. 当前最重要文档

- `IFLYTEK_TTS_WS_INTEGRATION_ZH.md`
- `VOICE_INTERACTION_REFACTOR_PROPOSAL_ZH.md`
- `VOICE_FRONTEND_CHAIN_STATUS_ZH.md`
- `AEC_DEBUG_PLAN_GUIDE_ZH.md`
- `WAKE_ASR_AUDIO_PROFILE_DESIGN_ZH.md`
- `WEBRTC_AECM_RIVER_接入记录.md`
- `AEC_REFERENCE_PATH_COMPARISON_ZH.md`
- `.codex/plan.md`

## 8. 当前下一步建议

建议后续按以下顺序推进：

1. 先完成板端 TTS 播报验证与状态日志确认
2. 再回到板端 AEC 验证
3. 再决定是否继续收敛 WebRTC AECM
4. 然后再进入更大范围的架构重构：
   - `PlaybackService`
   - `InteractionStateManager`
   - `wake_profile / asr_profile / barge_in_profile`
   - `KWS / SpatialContext / wake-guided beamforming`
