# XiaoZhi 专用客户端 Clean-Slate 重构计划

Status: active
Last Updated: 2026-05-07
Branch: `xiaozhi-client`

## 1. 重构判定

这个分支不再做“在现有 river 架构上兼容 XiaoZhi”的增量接入。

当前目标是把固件重建成 XiaoZhi 服务器专用客户端。参考对象是
`~/xiaozhi-esp32` 的设备端主干：

- `Application`
- `DeviceStateMachine`
- `AudioService`
- `Protocol`
- `McpServer`

当前仓库里可保留的不是旧主干，而是少数已经验证过的本地能力和板级适配。
后续实现应允许替换或删除现有 `river_core / river_cloud / playback / online_control`
里的大部分代码，只要每一步仍可构建、可上板验证。

## 2. 硬保护边界

必须保留当前项目实现：

- VAD：
  - `components/river_voice/river_voice_detector.c`
  - `components/river_voice/river_voice_detector_silero.cc`
  - `components/river_voice/generated/river_silero_vad_model_data.*`
  - 必要的 VAD probe / 诊断入口
- 唤醒词识别：
  - `components/river_voice/river_voice_kws.cc`
  - `components/river_voice/river_voice_kws_mean_patch.*`
  - `components/river_voice/generated/*kws*`
  - KWS tensor dump、alignment replay、本地/板端对比路径

允许重构这些模块的外层接口，但不能替换算法、模型、输入契约、tensor dump、
alignment replay 和本地/板端 parity 能力。若为了新主干需要解耦旧依赖，应改成
薄适配层，而不是删掉验证能力。

## 3. 可推倒边界

这些内容不再作为兼容目标，可以删除、替换或重写：

- `Iflytek` ASR / TTS
- `ASR provider / TTS provider` 抽象
- `river_cloud_adapter`
- `river_cloud_asr_bridge_runtime`
- `river_dialog_runtime`
- `river_session_coordinator`
- `river_dialog_cloud_port`
- `river_dialog_wake_admission`
- `river_interaction_state` 旧状态模型
- 现有 XiaoZhi playback recovery 大状态机
- 现有 `river_online_control` 中非音量控制能力
- 旧的 light / fan / curtain / socket MCP 工具
- 为旧多后端架构服务的诊断命令

AEC、BF、播放服务、引用路径、音频队列可以复用实现片段，但不再是保护对象。
如果保留它们，是因为它们适合新主干，不是因为迁移成本。

## 4. 目标架构

最终主链应收敛为：

```text
app/app_main.c
  -> xz_app
       -> xz_state_machine
       -> xz_audio_service
            -> current VAD
            -> current KWS
            -> Opus encode/decode
            -> board capture/playback adapter
       -> xz_protocol
            -> websocket transport
            -> hello/listen/abort/mcp
            -> binary audio
       -> xz_mcp_volume
```

建议落点仍可放在现有目录中，但语义要改变：

- `components/river_core`：
  - 只保留 XiaoZhi 专用应用编排和设备状态机
  - 状态以 `starting / network_wait / idle / connecting / listening / speaking / error`
    为主，不再派生旧 provider 状态
- `components/river_voice`：
  - 保留当前 VAD 和 KWS 实现
  - 暴露给新 `AudioService` 的接口应更直接，避免依赖旧 `dialog_runtime`
- `components/river_cloud`：
  - 变成 XiaoZhi 协议和传输层
  - 不再承载多云层、多 provider 或旧对话运行时
- `components/river_diag`：
  - 只保留对新主链有用的诊断
  - KWS/VAD 调试命令必须保留

## 5. 协议对齐要求

`xz_protocol` 应按 `~/xiaozhi-esp32/main/protocols` 的语义重新组织：

- 统一协议接口：
  - `start`
  - `open_audio_channel`
  - `close_audio_channel`
  - `send_audio`
  - `send_wake_word_detected`
  - `send_start_listening`
  - `send_stop_listening`
  - `send_abort_speaking`
  - `send_mcp_message`
- WebSocket headers：
  - `Authorization`
  - `Protocol-Version`
  - `Device-Id`
  - `Client-Id`
- `hello`：
  - `type=hello`
  - `version`
  - `transport=websocket`
  - `features.mcp=true`
  - `audio_params.format=opus`
  - `audio_params.sample_rate=16000`
  - `audio_params.channels=1`
  - `audio_params.frame_duration=60`
- 二进制音频：
  - 保留 v1 裸 Opus
  - 保留 v2/v3 header 兼容
  - 下行采样率和帧长以服务端 `hello.audio_params` 为准

当前 `river_xiaozhi_ws.c` 可以作为协议细节和 Ameba websocket API 参考，但不应把
旧 `conversation window / cloud runtime truth / playback recovery` 一起搬进新主干。

## 6. 音频链路要求

`xz_audio_service` 应参考 `xiaozhi-esp32/main/audio/audio_service.*` 的结构，而不是
沿用旧 cloud/provider 调用方向。

目标数据流：

```text
Idle:
  board capture -> current KWS

Listening:
  board capture -> optional preproc -> current VAD -> 16k mono PCM queue
  PCM queue -> 60ms Opus encode -> protocol send queue

Speaking:
  protocol binary audio -> Opus decode -> playback queue -> board output

Barge-in:
  current VAD detects local speech during speaking
  -> stop local playback
  -> send abort
  -> enter listening
```

内存策略按 RTL8730E 重新设计，不受 ESP32 队列深度约束。优先保证连续性和可观测性：

- 上行不静默丢帧
- 下行不靠复杂尾态恢复掩盖状态错误
- 队列满、编码失败、解码失败、播放失败必须有明确计数和日志
- 初版可以先半双工稳定，随后再决定是否重建 AEC/BF/full-duplex

## 7. MCP 范围

MCP 只保留音量控制：

- `self.get_device_status` 可返回音量和必要状态
- `self.audio_speaker.set_volume` 设置输出音量

其他工具必须返回明确 unsupported，且不再通过 `river_online_control` 暴露
light / fan / curtain / socket。

## 8. 彻底重构验收标准

代码层必须满足：

- live CMake 编译图中不再包含：
  - `river_cloud_adapter.c`
  - `river_cloud_asr_bridge_runtime.c`
  - `river_asr_provider_registry.c`
  - `river_asr_iflytek_rtasr.c`
  - `river_tts_iflytek_ws.c`
  - `river_dialog_runtime.c`
  - `river_session_coordinator.c`
- live include 关系中不再要求新主干包含：
  - `river_dialog_runtime.h`
  - `river_dialog_cloud_port.h`
  - `river_interaction_state.h`
- VAD/KWS 仍可构建并保留诊断：
  - `river_voice_detector_*`
  - `river_voice_kws*`
  - KWS dump / align 命令
- XiaoZhi 是唯一在线对话协议。
- MCP live 工具只剩音量控制。

板端行为必须满足：

- 上电后进入 `idle`，KWS 保持可用
- 唤醒词触发后打开 XiaoZhi audio channel
- 上行音频按 16k mono Opus 60ms 发送
- 下行 Opus 正常解码播放
- 播放中本地 VAD 可触发 abort 并重新进入 listening
- 串口日志能直接看出设备状态、协议状态、音频队列状态

## 9. 执行切片

### Step A: 强化 Clean-Slate 计划

目标：

- 把计划从“激进重构”收紧成“只有 VAD/KWS 受保护，其余可推倒”的执行合同。

范围：

- `doc/XIAOZHI_ESP32_PARITY_REARCH_EXECUTION_PLAN_ZH.md`
- `.codex/active_context.md`
- `.codex/changes.md`
- `.codex/verification.md`

完成标准：

- 计划明确 VAD/KWS 的保护边界
- 计划明确旧 core/cloud/provider/dialog/playback recovery 不再是兼容目标
- 计划明确 live CMake 和 include 的负向验收标准

验证：

```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
```

期望结果：

- `check_codex_harness: all checks passed`

### Step B: 解耦受保护的 VAD/KWS

目标：

- 让当前 VAD/KWS 能被新 `xz_audio_service` 调用，而不依赖旧
  `dialog_runtime / session_coordinator / cloud_adapter`。

范围：

- `components/river_voice/`
- `include/river/river_voice_detector.h`
- `include/river/river_voice_kws.h`
- `components/river_diag/river_diag_cmd.c`

完成标准：

- KWS/VAD 的核心实现和诊断仍在
- KWS/VAD 的 public API 不再强依赖旧对话运行时
- KWS parity / tensor dump / alignment replay 能力未删减

验证：

```bash
cd /root/ameba-river
rg -n "river_dialog_runtime|river_session_coordinator|river_cloud_adapter" \
  components/river_voice include/river/river_voice*.h
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

期望结果：

- `rg` 不再命中新主干必须依赖的 VAD/KWS 文件
- build 成功

### Step C: 建立新的最小编译主干

目标：

- 新增或替换为 XiaoZhi 专用 `xz_app / xz_state_machine / xz_audio_service /
  xz_protocol` 骨架，并让 live CMake 先切到新主干。

范围：

- `app/app_main.c`
- `components/river_core/`
- `components/river_cloud/`
- `components/river_voice/`
- `components/*/CMakeLists.txt`
- `include/river/`

完成标准：

- 固件能构建并启动到新 `xz_app`
- 旧 `dialog_runtime / session_coordinator / cloud_adapter / provider` 不再进入
  live CMake 编译图
- KWS/VAD 仍进入编译图

验证：

```bash
cd /root/ameba-river
rg -n "river_cloud_adapter.c|river_dialog_runtime.c|river_session_coordinator.c|river_asr_iflytek_rtasr.c|river_tts_iflytek_ws.c" \
  components/*/CMakeLists.txt
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

期望结果：

- `rg` 不命中 live CMake 源列表
- build 成功
- boot log 出现新设备状态机

### Step D: 重建 XiaoZhi 协议层

目标：

- 按 `xiaozhi-esp32` 的 `Protocol/WebsocketProtocol` 语义实现 XiaoZhi-only
  protocol。

范围：

- `components/river_cloud/`
- `include/river/river_xiaozhi*.h`

完成标准：

- WebSocket 可连接
- `hello` 可发送并解析 server hello
- `listen / abort / mcp / binary audio` 有统一出口
- 不引入旧 `cloud runtime truth`

验证：

```bash
cd /root/ameba-river
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

期望结果：

- build 成功
- 上板后可看到 websocket connect 和 server hello

### Step E: 重建 AudioService 风格音频链路

目标：

- 用当前 VAD/KWS 接入新的音频服务，上下行通过 Opus packet queue 与
  protocol 对接。

范围：

- `components/river_voice/`
- `components/river_cloud/`
- `include/river/`

完成标准：

- KWS 驱动 wake event
- VAD 驱动 speech state / barge-in
- 上行 PCM 聚合为 60ms Opus
- 下行 Opus 解码播放
- 队列状态可诊断

验证：

```bash
cd /root/ameba-river
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
python3 /root/ameba-rtos/tools/ameba/Monitor/monitor.py -p /dev/ttyUSB0 -b 1500000
```

期望结果：

- build 成功
- 唤醒后可完成一轮上行和下行播放
- 播放中说话触发 abort

### Step F: 收缩 MCP 为 volume-only

目标：

- 只保留 XiaoZhi MCP 音量控制。

范围：

- `components/river_cloud/`
- `include/river/`

完成标准：

- `self.audio_speaker.set_volume` 可用
- 非音量工具返回 unsupported
- live 代码不再暴露 light/fan/curtain/socket 控制

验证：

```bash
cd /root/ameba-river
rg -n "light|fan|curtain|socket|self.audio_speaker.set_volume|set_volume" \
  components include
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

期望结果：

- 非音量工具只出现在历史文档或删除候选中
- build 成功

### Step G: 删除旧主干残留

目标：

- 清理不再进入新主干的旧文件、头文件和诊断入口，避免后续误用。

范围：

- `components/river_core/`
- `components/river_cloud/`
- `include/river/`
- `components/river_diag/`

完成标准：

- 旧 provider/dialog/cloud adapter 源文件删除或移出 live tree
- 旧头文件不再被 include
- README/build active context 指向新主干

验证：

```bash
cd /root/ameba-river
rg -n "iflytek|asr_provider|tts_provider|river_cloud_adapter|dialog_runtime|session_coordinator" \
  app components include CMakeLists.txt Kconfig prj.conf
git diff --check
python3 tools/diag/check_codex_harness.py
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

期望结果：

- 只剩历史文档或明确兼容注释命中
- 静态检查和 build 成功

## 10. 下一步

下一步应执行 Step B：先解耦 VAD/KWS 的旧对话运行时依赖。只有这两个模块作为
受保护能力被搬进新主干，其余旧主干不再继续加补丁。
