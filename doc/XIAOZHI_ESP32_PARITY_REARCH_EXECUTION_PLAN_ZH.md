# Xiaozhi ESP32 对齐重构执行计划

Status: active
Last Updated: 2026-05-07
Branch: `xiaozhi-client`

## 1. 当前背景

- 当前主问题：
  - `ameba-river` 现有小智接入是在既有 `river` 云层上“兼容式叠加”出来的。
  - 它已经具备可运行的 `river_xiaozhi_ws / opus / playback / mcp bridge`，但核心抽象仍然是：
    - `cloud adapter`
    - `ASR provider`
    - `TTS provider`
    - `dialog runtime`
  - 这与 `~/xiaozhi-esp32` 的主干设计并不一致。后者是：
    - `Application`
    - `AudioService`
    - `Protocol`
    - `DeviceStateMachine`
    - `McpServer`
    - 单连接实时对话通道
- 已知稳定基线：
  - 当前板端已有可工作的：
    - Wi-Fi 联网
    - 本地语音前端
    - 本地 VAD
    - 本地 AEC / ref
    - 本地唤醒词
    - 下行播放服务
  - 当前仓库也已有 XiaoZhi 对接经验、日志和稳定性修补，可作为协议细节参考。
- 当前默认 SDK：
  - `/root/ameba-rtos`
- 与本计划强相关的现有文档：
  - `doc/XIAOZHI_REALTIME_INTERACTION_ARCHITECTURE_ZH.md`
  - `doc/XIAOZHI_INTEGRATION_IMPLEMENTATION_PLAN_ZH.md`
  - `doc/VOICE_RUNTIME_REARCHITECTURE_EXECUTION_PLAN_ZH.md`
  - `doc/XIAOZHI_SESSION_STABILITY_EXECUTION_PLAN_ZH.md`

## 2. 目标

- 把当前分支改造成“面向 XiaoZhi 服务器的专用设备端实现”。
- 设备端框架、协议语义、音频收发链路尽量与 `~/xiaozhi-esp32` 主干保持同构，而不是继续在现有 `river_cloud_*` 兼容层上增量修补。
- 保留并复用当前项目中已经优于 ESP32 的本地能力：
  - VAD
  - AEC / reference path
  - 唤醒词
  - BF / preproc
- MCP 暂时只保留音量控制能力。
- 允许为了收敛架构而删除当前分支中与 XiaoZhi 专用目标无关的代码、模块和兼容路径。
- 明确利用 RTL8730E 当前比 ESP32 更宽裕的内存，优先换稳定性和结构清晰度。

## 3. 非目标

- 不再以“保留 Iflytek 回退链路”作为目标。
- 不再以“最小改动迁移成本”作为目标。
- 本轮不追求保留多 provider / 多后端切换抽象。
- 本轮不扩展完整 MCP 工具集；仅保留音量控制。
- 本轮不优先做 UI、屏幕、摄像头、资产包、激活页等 `xiaozhi-esp32` 非音频主链能力。

## 4. 约束 / Guardrails

- 本仓库仍需遵守层次边界：
  - `app/`: Ameba 入口
  - `components/river_core`: 编排 / 状态机 / 路由
  - `components/river_voice`: 本地采集 / VAD / AEC / KWS / BF
  - `components/river_cloud`: XiaoZhi 传输、协议、会话和 MCP
  - `components/river_diag`: 诊断
- VAD、AEC、唤醒词、BF 必须走当前仓库已有实现，不直接照搬 ESP32 AFE。
- 不得弱化现有 wakeword parity / tensor dump / 对比验证基础设施。
- 每个实现切片之后必须：
  - 更新 `.codex/changes.md`
  - 更新 `.codex/verification.md`
  - 提交中文 commit

## 5. 已知事实

- `~/xiaozhi-esp32/main/application.cc` 的主干逻辑是“设备应用状态机驱动协议与音频服务”，不是 “ASR/TTS provider 拼装”。
- `~/xiaozhi-esp32/main/protocols/protocol.h` 与 `websocket_protocol.*` 已经定义了更贴近设备端的统一协议接口：
  - `OpenAudioChannel`
  - `CloseAudioChannel`
  - `SendAudio`
  - `SendWakeWordDetected`
  - `SendStartListening`
  - `SendStopListening`
  - `SendAbortSpeaking`
  - `SendMcpMessage`
- `~/xiaozhi-esp32/main/audio/audio_service.*` 的主干思路是：
  - 单一 `AudioService`
  - 本地处理后入上行编码队列
  - 下行音频进解码队列再进播放队列
  - 协议层只关心 `AudioStreamPacket`
- 当前仓库虽然已有 `river_xiaozhi_ws.c`，但其上层耦合了大量：
  - `conversation window`
  - `cloud round truth`
  - `dialog runtime`
  - `playback recovery policy`
  - `provider` 兼容状态
- 当前仓库的 `river_xiaozhi_mcp_bridge.c` 只支持 `light/fan/curtain/socket` 的 on/off/toggle，和目标分支“只保留音量控制”不一致。
- 当前仓库已有 `river_opus_codec.c`、`river_playback_service.c`、`river_voice_frontend.c`、`river_reference_service.c` 等可复用基础件，不需要照搬 ESP32 音频底座。

## 6. 设计结论

- 应保留的东西：
  - `river_voice_*` 本地前端能力
  - `river_playback_service`
  - `river_reference_service`
  - `river_wifi_station`
  - `river_opus_codec`
  - 当前 XiaoZhi 协议细节与联调日志积累
- 应删除或大幅收缩的东西：
  - Iflytek ASR / TTS provider
  - `cloud adapter` 的多后端选择逻辑
  - `dialog_runtime` 中为多云层兼容引入的大量派生状态
  - 与 XiaoZhi 专用目标无关的在线控制能力
  - 与 volume-only MCP 不匹配的工具路由
- 应新增或重构的主干：
  - `river_core` 内建立一个接近 `xiaozhi-esp32/Application + DeviceStateMachine` 的设备级编排器
  - `river_cloud` 收敛成面向 XiaoZhi 的单一 `Protocol / Session / MCP` 层
  - `river_voice` 暴露更直接的 `AudioService` 风格接口给上层消费

## 7. 风险与未知项

- 最大风险不是协议兼容，而是当前 `river_core` 的多层状态派生会持续抵消重构收益。
- 若继续保留 `dialog_runtime + session_coordinator + cloud_adapter` 的现有边界，最终只会得到“长得像 XiaoZhi、骨架仍是旧 river”的混合态。
- 当前本地前端与 `xiaozhi-esp32` 的 `AudioService` 粒度不同，需要先定义清楚：
  - 上行推送是“20 ms PCM 帧”还是“已判定语音段/窗口”
  - 播放中插话是沿用当前 `barge-in` 事实源，还是改成设备状态机直接消费
- 现有下行播放服务已经承载大量恢复策略，重构时要区分：
  - 哪些属于 Ameba 播放底座必须保留
  - 哪些只是旧云层兼容逻辑，应该删除

## 8. 执行切片

### Step A: 产出 XiaoZhi-only 目标架构与删除清单

目标：

- 形成可执行的重构蓝图，明确：
  - 保留模块
  - 删除模块
  - 需要重写的接口
  - 新主链的启动 / 会话 / 音频 / MCP 路径

范围：

- `doc/XIAOZHI_ESP32_PARITY_REARCH_EXECUTION_PLAN_ZH.md`
- `.codex/active_context.md`
- `.codex/active_plans.md`

完成标准：

- 文档中明确写出 `xiaozhi-esp32` 同构目标
- 明确把 Iflytek / 多 provider 兼容层降为删除对象
- 主目标切换到本计划

验证：

```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
```

期望结果：

- `check_codex_harness: all checks passed`

### Step B: 收缩云层为 XiaoZhi 单协议栈

目标：

- 删除 Iflytek 和多 provider 路径，把 `river_cloud` 收敛成：
  - `protocol`
  - `xiaozhi session`
  - `mcp`
  - `uplink/downlink`

范围：

- `components/river_cloud/`
- `include/river/river_cloud.h`
- `include/river/river_dialog_cloud_port.h`

完成标准：

- 构建图中不再编译 Iflytek 相关源文件
- 上层不再感知“ASR provider / TTS provider”概念
- XiaoZhi 会话成为唯一云层入口

验证：

```bash
cd /root/ameba-river
rg -n "iflytek|provider_registry|asr_provider" components/river_cloud include/river
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

期望结果：

- 源码层不再存在存活的 Iflytek/provider 主链引用
- 工程成功构建

### Step C: 建立设备级状态机与应用编排主干

目标：

- 以 `xiaozhi-esp32/Application + DeviceStateMachine` 为参考，把当前
  `river_app + dialog_runtime + session_coordinator` 重构成更直接的设备主循环

范围：

- `components/river_core/`
- `include/river/river_app.h`
- `app/app_main.c`

完成标准：

- 启动链路能清楚表达：
  - boot
  - network ready
  - idle
  - wake/listen
  - speaking
  - abort / recover
- 上层状态不再依赖旧的 provider 式派生

验证：

```bash
cd /root/ameba-river
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

期望结果：

- 工程成功构建
- 串口启动日志能看出新的设备状态主干

### Step D: 以现有前端重组 AudioService 风格音频链路

目标：

- 用当前 `river_voice` 能力重建更接近 `xiaozhi-esp32/audio_service` 的数据流：
  - mic -> VAD/AEC/BF/KWS -> uplink queue
  - downlink queue -> opus decode -> playback

范围：

- `components/river_voice/`
- `components/river_cloud/`
- `include/river/river_voice*.h`

完成标准：

- 上下行都通过统一音频包接口连接
- 播放中插话能直接打断本地播放并通知 XiaoZhi `abort`
- 不再依赖旧的“conversation window + provider callbacks”组织音频生命周期

验证：

```bash
cd /root/ameba-river
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

期望结果：

- 工程成功构建
- 上板后可完成唤醒、说话、服务端回音频、插话打断

### Step E: 收缩 MCP 为 volume-only

目标：

- 删除当前与灯、风扇、窗帘、插座相关的 MCP 工具映射，仅保留音量控制

范围：

- `components/river_cloud/river_xiaozhi_mcp_bridge.c`
- `components/river_cloud/river_online_control.c`
- `include/river/river_online_control.h`

完成标准：

- XiaoZhi MCP 只接受音量相关调用
- 非音量工具调用返回明确 unsupported

验证：

```bash
cd /root/ameba-river
rg -n "light|fan|curtain|socket|volume" components/river_cloud include/river
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

期望结果：

- 工具映射只剩 volume 主链
- 工程成功构建

### Step F: 板端联调与资源放大

目标：

- 在 XiaoZhi-only 主链稳定后，重新按 Ameba 资源情况放大队列、缓冲和 worker 栈

范围：

- `components/river_cloud/`
- `components/river_voice/`
- `prj.conf`
- `board/rtl8730e/profiles/`

完成标准：

- 设备端可以在比 ESP32 更宽松的内存策略下稳定运行
- 队列和缓冲参数不再受 ESP32 约束

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
- 板端长时间运行无明显 heap/queue 抖动

## 9. 当前建议的第一刀

- 第一刀不是直接删文件，而是先把“主链目标”和“删除对象”写死。
- 之后优先顺序应为：
  1. 删多 provider / Iflytek 云层
  2. 立设备状态机主干
  3. 接通 AudioService 风格音频链路
  4. MCP 收缩到 volume-only
  5. 最后做资源放大与板端稳定性收敛
