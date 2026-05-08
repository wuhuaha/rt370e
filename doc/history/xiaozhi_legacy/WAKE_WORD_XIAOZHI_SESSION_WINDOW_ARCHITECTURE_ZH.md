# 本地唤醒词与小智会话窗口架构设计

## 1. 文档目的

本文档定义 `ameba-river` 在 `xiaozhi` 分支上的下一阶段语音交互方案：

- 在待机阶段引入本地唤醒词识别
- 保持当前 `fixed_dsb` 双麦前端能力
- 让 `XiaoZhi` 会话从“本地 VAD 裁句驱动”逐步转向“唤醒后会话窗口驱动”
- 在不长期占用云端资源的前提下，把交互体验尽量做得更接近原生 `xiaozhi-esp32 / py-xiaozhi`

本文档不是协议接入文档。小智会话协议、`Opus` 收发、`MCP` 桥接已经在 [XIAOZHI_REALTIME_INTERACTION_ARCHITECTURE_ZH.md](/root/ameba-river/XIAOZHI_REALTIME_INTERACTION_ARCHITECTURE_ZH.md) 中定义。

## 2. 当前问题定义

当前板端已经跑通：

- `OTA bootstrap -> websocket.url/token`
- 小智 `hello / listen / stt / llm / tts / mcp`
- `Opus` 上行 / 下行
- `PlaybackService`、本地打断、上游 `abort`

但当前体验与原生小智 client 仍有明显差距。根因不是服务端慢，而是本地会话模型仍然偏向：

```text
本地 VAD 命中
-> 打开一小段 listen
-> 送一小段音频
-> 本地静音后 stop
-> 下一句再重复
```

这会带来几个直接后果：

- 短句首字更容易丢失
- `session started -> session closed` 过于频繁
- 空 listen 段较多
- 说播切换不够自然
- 看起来像“小智 manual 的自动版”，而不是稳定的 `auto / realtime`

## 3. 设计结论

### 3.1 不应继续让 VAD 充当会话边界裁判

当前 `VAD` 的职责过重：

- 决定何时 start listen
- 决定何时 stop listen
- 同时还承担 barge-in 检测

这不利于做出接近原生小智的连续对话体验。

### 3.2 更合理的主导者应当是“本地唤醒词 + 会话窗口”

新的推荐模型是：

```text
Wake Stage:
capture -> fixed_dsb -> mono -> local KWS

命中唤醒词后:
打开 XiaoZhi 会话窗口
-> listen start(auto)
-> 持续若干轮对话
-> VAD 只做辅助
-> 窗口超时/明确结束后关闭
```

### 3.3 第一阶段目标不是直接上真正 realtime

在当前板端状态下，推荐先实现：

```text
KWS -> XiaoZhi auto conversation window
```

而不是直接：

```text
KWS -> XiaoZhi realtime until end
```

原因：

- 当前 `AEC` 仍未稳定进入默认主链
- 当前小智 24k 下行播放仍存在 `underrun` 风险
- 真正 `realtime` 对播放期听音、回声管理、双工稳定性要求更高

因此第一阶段应以“低云端成本、明显改善首字和句间体验”为目标，而不是一开始就追求持续全双工极限形态。

## 4. 当前链路与目标链路对比

### 4.1 当前链路

```text
AMIC1 + AMIC3
-> fixed_dsb
-> silero_vad
-> 本地 VAD 决定开段
-> XiaoZhi listen start(realtime)
-> Opus uplink
-> 服务端 stt/llm/tts
-> Opus downlink
-> PlaybackService
```

特点：

- 云端资源按句子触发，不是全天持续
- 本地可以很好地控住 admission
- 但交互节奏更像 `manual-like sessioning`

### 4.2 目标链路

```text
Wake Stage:
AMIC1 + AMIC3
-> fixed_dsb
-> mono
-> log-mel frontend
-> DS-CNN KWS

Post-Wake Stage:
mono PCM
-> XiaoZhi session window
-> listen start(auto)
-> VAD 辅助上传/打断/超时
-> TTS / barge-in / MCP
-> timeout 或显式结束
-> close window
```

特点：

- 待机时不占小智云端会话
- 唤醒后不再一句一句反复开关 listen
- 会话窗口内的短停顿和追问更自然

## 5. 推荐的阶段化角色分工

### 5.1 Wake Stage

职责：

- 常驻、低成本运行
- 只负责判断“是否进入一轮对话窗口”

启用模块：

- `AMIC1 + AMIC3`
- `fixed_dsb`
- 本地 `log-mel` 特征
- 本地 `KWS`

禁用或弱化：

- 不建立小智 listen
- 不让云端承担待机阶段的 admission

### 5.2 Post-Wake Stage

职责：

- 管理一整轮或多轮自然对话
- 优先保持会话连续性

启用模块：

- `XiaoZhi websocket session`
- `listen start(auto)` 为首选
- `Opus uplink/downlink`
- `PlaybackService`
- `InteractionState`
- `MCP`

辅助模块：

- `silero_vad` 继续保留，但职责调整为：
  - 上传静音抑制
  - endpoint 辅助
  - 播放期 barge-in
  - 对话窗口超时判断

## 6. 为什么不是“VAD -> KWS -> realtime”

这个表述不够准确，因为：

- `VAD` 只回答“现在是不是有人在发声”
- `KWS` 才回答“这是不是目标唤醒词”
- `realtime/auto/manual` 是会话模式，不是检测器

更准确的链路应该是：

```text
待机:
VAD 可选作粗门控
-> KWS 做最终 admission

唤醒后:
进入 XiaoZhi session window
-> 先用 auto 模式
-> 后续再逐步逼近 realtime
```

如果把 `VAD` 放在 `KWS` 之前，也只能把它当成：

- 降功耗门控
- 特征提取唤醒条件

而不是让它再次主导整轮会话边界。

## 7. 本地唤醒词模型迁移建议

### 7.1 现有训练产物结论

来自 `/root/wake-word-trainer` 的训练结果已经满足板端迁移的基本前提：

- 模型结构：`DS-CNN`
- 输入：`98x40x1 log-mel`
- 采样率：`16kHz`
- 量化：`INT8`
- 模型大小：约 `61.9KB`

这说明“模型本体”适合上 RTL8730E。

### 7.2 真正缺口在板端前处理

当前 `ameba-river` 没有现成的板端 `log-mel / microfrontend / MFCC` 实现。

因此迁移工作重点不是：

- 训练模型

而是：

- 板端 `mono PCM -> 40x98 log-mel` 特征流水线
- 与当前 `fixed_dsb` 输出对齐
- 与 `TFLite Micro` KWS 推理循环对齐

### 7.3 推荐接法

本地 KWS 应放在：

```text
fixed_dsb 输出之后
```

理由：

- 复用当前稳定双麦阵列收益
- 让训练数据和部署数据域更接近
- 不需要直接消费双通道原始波形

推荐数据路径：

```text
capture(2ch)
-> fixed_dsb
-> mono 16k
-> log-mel ring / rolling window
-> DS-CNN KWS
```

## 8. 会话模式选择建议

### 8.1 第一阶段：`KWS -> auto`

这是最推荐的落地方向。

行为定义：

- 唤醒词命中后：
  - 打开 XiaoZhi 会话
  - 发送一次 `listen start(auto)`
- 一句话结束后：
  - 不立即销毁整个 websocket 会话
  - 保持一个短暂 follow-up 窗口
- 若用户继续说：
  - 直接进入下一轮 listen
- 若持续静音超时：
  - 关闭会话窗口

优点：

- 比当前 VAD 小段式更接近原生
- 比真正 realtime 风险更低
- 对当前无稳定 AEC 的板端更现实

### 8.2 第二阶段：`KWS -> realtime`

只有在以下条件满足后再考虑：

- 24k 下行播放稳定
- barge-in 和 `abort` 可预测
- AEC 或等效播放期拾音抑制已可用

行为定义：

- 唤醒后直接 `listen start(realtime)`
- 播放中仍维持 listening
- 用户插话时优先 `abort`

## 9. 窗口关闭规则建议

推荐会话窗口采用显式规则，而不是重新退回“每句靠 VAD stop”：

窗口关闭条件建议如下：

1. `TTS stop` 后持续静音超过 `N` 秒
2. 明确本地结束意图
3. 网络错误或服务端关闭
4. 长时间无有效 STT/LLM/TTS 交互

建议第一阶段参数：

- `wake_window_followup_ms`: `6000 ~ 10000`
- `post_tts_silence_close_ms`: `2500 ~ 4000`
- `no_result_watchdog_ms`: `5000 ~ 8000`

这些参数应通过板端日志和体验共同收敛。

## 10. 与当前模块的映射

### 10.1 建议新增模块

- `river_voice_kws_frontend.c/.h`
  - rolling PCM -> log-mel frontend
- `river_voice_kws.c/.h`
  - `TFLM` KWS 推理封装
- `river_voice_wake_gate.c/.h`
  - wake-stage admission 和 cooldown
- `river_conversation_window.c/.h`
  - 唤醒后会话窗口时序和超时策略

### 10.2 建议复用模块

- `river_voice_capture`
- `river_voice_preproc_fixed_dsb`
- `river_voice_vad_probe`
- `river_cloud_adapter`
- `river_playback_service`
- `river_interaction_state`
- `river_xiaozhi_ws`

### 10.3 需要调整职责的模块

- [river_voice_vad_probe.c](/root/ameba-river/components/river_voice/river_voice_vad_probe.c)
  - 从“主导开段”改为“窗口内辅助上传/打断/超时”
- [river_cloud_adapter.c](/root/ameba-river/components/river_cloud/river_cloud_adapter.c)
  - 从“每句 listen start/stop”改为“窗口级 listen/session 管理”
- [river_voice_runtime_policy.c](/root/ameba-river/components/river_voice/river_voice_runtime_policy.c)
  - 真正驱动 `wake -> post_wake` 切换

## 11. 分阶段实施建议

### Phase A: 板端 KWS 最小闭环

目标：

- 在板端跑通 `fixed_dsb -> log-mel -> DS-CNN`

内容：

- 导入 `model_int8.tflite` 或生成的 C 头
- 新增 `TFLM` KWS 推理封装
- 新增 log-mel 特征提取
- 用串口打印概率、命中次数、滑窗结果

验收：

- 板端可稳定输出唤醒词命中日志

### Phase B: Wake Stage 接入

目标：

- 在待机阶段由 KWS 主导 admission

内容：

- 新增 `wake gate`
- 增加 cooldown、防抖、二次确认
- 命中后切到 `wake_confirmed/post_wake`

验收：

- 待机阶段不再用 VAD 直接开 XiaoZhi listen

### Phase C: 会话窗口

目标：

- 从“句子级开关段”过渡到“窗口级会话”

内容：

- KWS 命中后打开 XiaoZhi 会话窗口
- 第一版采用 `listen start(auto)`
- 增加窗口超时和 follow-up 策略

验收：

- 空 listen 段显著减少
- 首字体验明显改善

### Phase D: 双工增强

目标：

- 在会话窗口内稳定支持播放期插话

内容：

- 继续优化下行播放
- 强化 `abort`
- 结合 `playback_ref` 和后续 AEC 能力

验收：

- 会话窗口内的 barge-in 比当前方案更自然

### Phase E: 评估是否升级到 realtime

目标：

- 判断是否能安全切到 `listen start(realtime)`

前提：

- 下行 `underrun` 已基本消失
- 播放期误触发可控
- AEC 或等效抑制足够稳定

## 12. 不建议做的事情

- 不建议让 KWS 直接替代当前 `fixed_dsb`
- 不建议让 VAD 和 KWS 同时争夺会话边界主导权
- 不建议在当前播放链未稳定前强上真正 `realtime`
- 不建议把原生小智 client 整套应用框架硬搬进 `river`

## 13. 最终建议

推荐的总路线是：

```text
当前:
VAD-driven manual-like XiaoZhi sessioning

下一阶段:
KWS-driven XiaoZhi auto conversation window

后续成熟形态:
KWS-driven XiaoZhi realtime conversation window
```

这条路线同时满足：

- 控制云端资源
- 改善首字与句间体验
- 保留当前板级前端积累
- 为后续 AEC / 真正全双工留出升级空间
