# 小智接入实施计划

## 1. 目标

在不破坏当前可烧录 `Iflytek` 全双工基线的前提下，逐步接入小智服务器，实现：

- 实时语音上行
- 服务端文本/语音事件接收
- 播放中插话打断
- 设备控制 `MCP`

当前状态：

- `Phase A-D` 的代码实现已经落地
- `Phase E` 的观测与日志能力也已落地到可烧录固件
- 当前主要剩余工作是板端联调、长时间运行验证和参数收敛

## 2. 实施原则

### 2.1 先会话，再音频，再体验

优先顺序必须是：

1. 协议骨架
2. 二进制音频
3. 状态机映射
4. 体验打磨

### 2.2 永远保留回退路径

整个接入过程中必须始终保留：

- `Iflytek RTASR`
- `Iflytek WS TTS`
- 当前 `PlaybackService + barge-in` 主链

### 2.3 非侵入式接入

小智模块应尽量新增，不随意重写现有：

- capture
- preproc
- playback
- ref
- interaction_state

## 3. 分阶段计划

### Phase A: 协议骨架

状态：

- 已完成

目标：

- 落地 `river_xiaozhi_ws` 会话层

内容：

- WebSocket 连接管理
- `hello`
- `listen start/stop/detect`
- `abort`
- 服务端 `stt / llm / tts / mcp` 文本消息解析骨架
- 状态和统计结构

验收：

- 可在板端建立会话并打印握手日志

### Phase B: Opus 上下行

状态：

- 已完成

目标：

- 打通小智的二进制音频路径

内容：

- `river_opus_codec`
- 上行 `PCM -> Opus`
- 下行 `Opus -> PCM`
- 动态适配服务端返回的音频参数

验收：

- 板端可稳定发送语音并播放服务端音频

### Phase C: 与现有运行时对接

状态：

- 已完成

目标：

- 把小智会话映射到当前 `river` 运行时

内容：

- `stt partial/final` 映射到统一结果出口
- `tts start/stop/sentence_start` 映射到当前播放状态
- 播放中插话打断后自动发送 `abort`

验收：

- 播放中说话可中断当前播报

### Phase D: MCP 设备控制

状态：

- 已完成

目标：

- 服务端可以直接驱动本地设备控制面

内容：

- `river_xiaozhi_mcp_bridge`
- 映射到当前 `river_online_control`

验收：

- 小智服务端工具调用能操作 `light / fan / curtain / socket`

### Phase E: 体验与观测

状态：

- 代码实现已完成，当前进入板端验证阶段

目标：

- 把链路做成可稳定调试、可长时间运行

内容：

- 增加状态与统计输出
- 增加 `sentence_start` 体验日志
- 增加错误码和回退日志
- 长时间全双工回归

验收：

- `ASR + TTS + barge-in + MCP` 长时间混合运行稳定

## 4. 文件规划

第一批预计涉及：

- `components/river_cloud/river_xiaozhi_ws.c`
- `include/river/river_xiaozhi_ws.h`
- `components/river_cloud/river_xiaozhi_mcp_bridge.c`
- `include/river/river_xiaozhi_mcp_bridge.h`
- `components/river_cloud/river_opus_codec.c`
- `include/river/river_opus_codec.h`

后续按需要再扩展：

- `river_cloud_adapter`
- `river_app`
- `river_online_control`

## 5. 风险点

- 小智是单会话协议，如果硬塞进现有拆分 provider 模型，后续状态会越来越乱
- Opus 下行采样率如果假定固定为 16k，容易在服务端参数变化时出错
- 如果插话时只发 `abort` 不先本地打断，用户体感会变差
- 如果为了接小智而破坏当前 Iflytek 回退路径，后续调试会失去可靠基线

## 6. 当前建议执行顺序

1. 先完成板端连接配置和 `hello` 基本联调
2. 验证实时上行/下行音频与 `sentence_start` 日志是否一致
3. 验证播放期 `barge-in` 是否稳定触发本地打断与上游 `abort`
4. 验证 `MCP` 设备控制和回退链路是否都保持可用
5. 最后做长时间 soak test，并根据资源曲线决定是否还需要额外优化
