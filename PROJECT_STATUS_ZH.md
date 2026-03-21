# 项目当前状态说明

## 1. 当前分支与基线

- 当前开发分支：`xiaozhi`
- 稳定 ASR 基线 tag：`m3-asr-baseline-fixed-dsb`
- 稳定 ASR 基线提交：`e40e017`
- 当前可烧录全双工基线 tag：`m4-full-duplex-bargein-stable`
- 当前可烧录全双工基线提交：`6290987`
- 保留的 AEC 实验分支快照：`debug/webrtc-aec`

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

## 3. 当前可烧录全双工基线

当前已跑通、可作为 `xiaozhi` 分支开发底座的链路：

```text
capture
-> fixed_dsb
-> silero_vad
-> online streaming asr

river tts <text>
-> cloud adapter
-> iflytek ws tts
-> PlaybackService
-> speaker

playback-time asr start
-> barge_in_listening
-> text-confirmed interrupt
-> interrupt tts
```

说明：
- `PlaybackService / ReferenceService / InteractionState / diag status` 已经是稳定控制面
- 播放中插话已经可以通过文本确认路径打断当前 TTS
- 网络授时已经前移到联网成功后的初始化阶段，而不是等到 ASR 首次触发
- 该基线仍然使用讯飞拆分式 `ASR + TTS`，作为 `xiaozhi` 接入阶段的回退与对照路径

## 4. 当前 `xiaozhi` 分支状态

当前分支已经把“小智接入”从文档目标推进到可烧录联调状态：

```text
接入小智服务器
-> 单条 WebSocket 实时会话
-> Opus 上行 / 下行音频
-> STT / LLM / TTS / MCP 同会话协作
-> 复用当前 PlaybackService / InteractionState / barge-in runtime
```

当前已落地能力：

- `river_xiaozhi_ws` 会话层已经接入，支持 `hello / listen / abort / stt / tts / llm / mcp`
- 已补齐 `OTA bootstrap -> websocket.url/token` 拉取链路，支持按 `xiaozhi-esp32` 的 server 下发方式接入
- 已补齐绑定提示文本中的 `6位绑定码` 提取和串口日志输出，便于板端直接抄码
- `Opus` 上下行已经接入现有音频主链，不另起一套播放/采集架构
- `sentence_start`、`stt partial/final`、`llm emotion/text` 已映射到当前日志与运行时事件面
- 播放期 `barge-in` 已支持“先本地打断播放，再向上游发 `abort`”
- `MCP tools/call` 已桥接到当前 `light / fan / curtain / socket` 控制面
- 新增 `river xiaozhi ...` 诊断命令用于板端联调

当前仍保留的关键约束：

- 小智在本项目中被视为“实时会话传输层”，而不是简单替换成另一个 `ASR provider` 或 `TTS provider`
- 当前讯飞链路必须保留，作为回退基线与对照样本
- 新接入不能破坏现有 `fixed_dsb -> silero_vad -> streaming asr` 主链可验证性
- 当前剩余工作以板端验证、长时间稳定性观察和参数收敛为主，而不是再补协议骨架

## 5. 当前保留实验链

当前保留但不进入默认产品主链的实验链：

```text
capture(3ch: mic0 + mic1 + ref)
-> webrtc_aecm(experimental, gated)
-> fixed_dsb
-> silero_vad
-> streaming asr
```

说明：
- `AEC` 仅在实验 profile 下启用
- AEC 输入模型已收敛为 `mic0 + mic1 + ref`
- AEC 资产继续保留，但不是 `xiaozhi` 分支的当前主任务

## 6. 当前代码结构重点

### 6.1 核心语音组件

- `components/river_voice/river_voice_capture.c`
- `components/river_voice/river_voice_preproc.c`
- `components/river_voice/river_voice_preproc_fixed_dsb.c`
- `components/river_voice/river_voice_detector.c`
- `components/river_voice/river_voice_vad_probe.c`

### 6.2 已稳定的服务化边界

- `components/river_voice/river_playback_service.c`
- `components/river_voice/river_reference_service.c`
- `components/river_core/river_interaction_state.c`
- `components/river_voice/river_voice_profile.c`
- `components/river_voice/river_voice_runtime_policy.c`

### 6.3 当前云接入与后续扩展重点

- `components/river_cloud/river_cloud_adapter.c`
- `components/river_cloud/river_asr_iflytek_rtasr.c`
- `components/river_cloud/river_tts_iflytek_ws.c`
- `components/river_cloud/river_xiaozhi_ws.c`
- `components/river_cloud/river_xiaozhi_mcp_bridge.c`
- `components/river_cloud/river_opus_codec.c`
- `components/river_cloud/river_online_control.c`
- `components/river_cloud/river_ws_dispatch.c`

## 7. 当前代码整理原则

- 稳定主链与实验链分离
- 播放状态不再主要依赖能量推断
- 参考路径有独立服务边界
- 交互状态有独立状态管理
- AEC 仅作为实验能力接入，不污染默认主链
- 新的云对话后端优先作为“会话层”接入，而不是把业务逻辑塞进 ASR/TTS 模块内部

## 8. 当前最重要文档

- `XIAOZHI_REALTIME_INTERACTION_ARCHITECTURE_ZH.md`
- `XIAOZHI_INTEGRATION_IMPLEMENTATION_PLAN_ZH.md`
- `IFLYTEK_TTS_WS_INTEGRATION_ZH.md`
- `VOICE_INTERACTION_REFACTOR_PROPOSAL_ZH.md`
- `VOICE_FRONTEND_CHAIN_STATUS_ZH.md`
- `AEC_DEBUG_PLAN_GUIDE_ZH.md`
- `WAKE_ASR_AUDIO_PROFILE_DESIGN_ZH.md`
- `WEBRTC_AECM_RIVER_接入记录.md`
- `AEC_REFERENCE_PATH_COMPARISON_ZH.md`
- `.codex/plan.md`

## 9. 当前下一步建议

当前最合理的下一步不再是补功能，而是板端验证：

1. 配置小智服务地址和 token，确认 `hello` 与会话保持稳定
2. 验证上行 `Opus`、下行 `Opus`、`sentence_start` 与实际播报是否一致
3. 验证播放中说话是否触发“本地打断 + 上游 `abort`”
4. 验证 `MCP` 工具调用是否能稳定驱动 `light / fan / curtain / socket`
5. 以当前讯飞拆分链路作为回退基线，对比内存低水位、长时间稳定性和交互体验
