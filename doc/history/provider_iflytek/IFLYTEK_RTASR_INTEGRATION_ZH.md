# 科大讯飞 RTASR 接入说明（中文）

## 1. 这次实际做了什么

本轮不是只“留接口”，而是已经把 `ameba-river` 的在线语音识别框架真正接上了第一家平台，并把后续扩展位也预留好了。

当前已完成：
- 加入了 `Wi-Fi STA` 自动联网模块。
- 加入了通用在线 ASR 适配层 `river_cloud_adapter`。
- 加入了 provider 注册表和 provider ops 抽象。
- 接入了第一家 provider：`科大讯飞 RTASR（流式 WebSocket）`。
- 将当前本地语音链路的增强后单通道音频，接到了在线流式上传桥接层。
- 保留了“分段上传”的稳定接口：
  - `segment_buffer -> segment_sink -> cloud batch bridge`
  - 目前架构就绪，但 `iflytek_rtasr` 还没有实现 batch submit。

一句话总结当前状态：

`本地 VAD / 分段 / 在线流式上传 / 云端结果回调` 这四个关键边界已经贯通，当前默认在线 provider 是科大讯飞 RTASR。

---

## 2. 当前架构

### 2.1 总体链路

当前默认运行链路：

`capture -> aivoice_afe(asr_mainline) -> silero_vad ->`

分两条输出：

1. 流式识别路径  
   `cloud_adapter(stream bridge) -> iflytek_rtasr`

2. 非流式/分段路径  
   `segment_buffer -> segment_sink -> cloud batch bridge`

### 2.2 为什么这样拆

这样拆是为了满足两个目标：

1. 当前阶段先接通科大讯飞流式识别。
2. 后续要支持：
   - 第二家 ASR 平台
   - 非流式识别
   - 自研流式/非流式 ASR
   - 自研 VAD / KWS / AEC / DSP / TFLite 模型

因此，项目里明确分成三层：

- `river_voice`
  - 只负责本地音频采集、增强、VAD、分段
- `river_cloud`
  - 只负责云端 provider 和在线识别会话
- `river_core`
  - 只负责业务层结果接收和后续控制路由

---

## 3. 这次新增/修改的核心文件

### 3.1 Wi-Fi

- `include/river/river_wifi_station.h`
- `include/river/river_wifi_credentials.h`
- `components/river_cloud/river_wifi_station.c`

作用：
- 自动 STA 联网
- 当前临时把 SSID / password 放在 `.h` 里，便于先跑通

当前配置：
- SSID: `Keeu`
- Password: `keeu123456`

### 3.2 在线 ASR 抽象层

- `include/river/river_cloud.h`
- `components/river_cloud/river_cloud_adapter.c`
- `components/river_cloud/river_asr_provider_internal.h`
- `components/river_cloud/river_asr_provider_registry.c`

作用：
- 抽象 provider 能力：
  - `streaming supported`
  - `batch supported`
  - `open/feed/finish/poll`
  - partial/final/error/session 回调
- 管理：
  - Wi-Fi readiness
  - UTC/SNTP readiness
  - 流式 pre-roll/post-roll
  - batch bridge 入口

### 3.3 科大讯飞 RTASR provider

- `include/river/river_asr_iflytek_credentials.h`
- `components/river_cloud/river_asr_iflytek_rtasr.c`

作用：
- 生成 RTASR WebSocket URL
- 进行签名
- 建立 WebSocket 连接
- 发送 `pcm_s16le` 二进制音频
- 接收 JSON 结果并解析
- 输出 partial / final / error / session 状态

### 3.4 本地语音链路对接云端

- `components/river_voice/river_voice_vad_probe.c`
- `components/river_voice/river_voice_segment_sink.c`

作用：
- `vad_probe` 在每个 detector 决策后，把增强后单通道音频帧送入在线流式桥
- 同时继续保留分段 ready 数据，走 batch handoff

### 3.5 应用层结果接收

- `components/river_core/river_app.c`

作用：
- 注册云端识别结果回调
- 打印：
  - session started
  - partial
  - final
  - error
  - session closed

---

## 4. 当前科大讯飞 RTASR 认证参数

当前临时放在：

- `include/river/river_asr_iflytek_credentials.h`

内容：
- `APPID`
- `APIKey`
- `APISecret`

这是用户明确要求的“先放 `.h` 文件里，后续再优化”的做法。

当前状态：
- 只适用于 bring-up / 首轮联调
- 不适合后续量产或正式发布

后续建议替换为：
- 安全存储
- 编译时私有配置
- 或运行时 provisioning

---

## 5. 科大讯飞 RTASR 当前实现细节

### 5.1 Endpoint

当前代码里使用：
- host: `office-api-ast-dx.iflyaisol.com`
- path: `/ast/communicate/v1`

### 5.2 音频格式

当前限制为：
- `16000 Hz`
- `1 channel`
- `16 bit`
- `pcm_s16le`

如果后续音频格式变化，当前 provider 会直接返回 `unsupported`，避免静默错误。

### 5.3 签名流程

当前实现流程：

1. 组装 query 参数：
   - `accessKeyId`
   - `appId`
   - `audio_encode`
   - `lang`
   - `samplerate`
   - `utc`
   - `uuid`
2. 进行排序后的 query 签名
3. 使用 `HMAC-SHA1`
4. 对结果做 `Base64`
5. 再进行 URL encode
6. 拼成最终 `wss://...?...&signature=...`

当前实现所用库：
- `mbedtls`
- `cJSON`
- SDK 内置 `websocket` client

### 5.4 结果解析

当前从 JSON 中提取文本路径：

- `data.cn.st.rt[].ws[].cw[].w`

当前将以下事件统一回传给上层：
- `SESSION_STARTED`
- `PARTIAL`
- `FINAL`
- `ERROR`
- `SESSION_CLOSED`

---

## 6. 当前支持范围

### 6.1 已支持

- Wi-Fi 自动联网
- 流式音频桥接
- 科大讯飞 RTASR 流式 provider
- partial / final 结果回调
- provider-neutral 回调接口
- provider-neutral streaming / batch 抽象
- 分段缓存与 batch handoff 接口

### 6.2 尚未支持

- 科大讯飞 batch / 非流式上传
- 多 provider 动态切换配置
- 云端识别结果直接驱动家居控制意图解析
- 认证信息安全存储
- 联网质量自恢复策略细化
- 流式会话级 QoS 和重连策略细化

---

## 7. 当前为了多平台扩展做了哪些预留

### 7.1 provider ops 抽象

当前 provider 只要实现下面这些能力，就能接入同一套框架：

- `provider_name`
- `supports_streaming`
- `supports_batch`
- `init`
- `deinit`
- `stream_open`
- `stream_feed`
- `stream_finish`
- `stream_poll`
- `stream_active`
- `batch_submit`
- `dump_status`

这意味着后续接入：
- 讯飞非流式
- 腾讯云
- 阿里云
- 火山
- 私有 WebSocket ASR
- 自研 HTTP / WebSocket / gRPC ASR

都不需要改 `river_voice` 主链路。

### 7.2 segment sink

`segment_buffer -> segment_sink` 这条链保留下来，是专门为后续：
- 非流式 ASR
- 上传整段语音
- 或本地 ASR

预留的。

### 7.3 本地 VAD / 云端 ASR 解耦

当前 `Silero VAD` 和 `SDK VAD reference` 都只产生本地决策，不直接和 provider 代码耦合。  
后续如果换自研 VAD，不会影响 provider。

---

## 8. 如何复现这次接入

### 8.1 前提

代码目录：

```bash
cd /root/ameba-river
```

环境：

```bash
source env.sh
```

### 8.2 编译

```bash
CCACHE_DISABLE=1 python3 /root/ameba-rtos-1.2/ameba.py build -p
```

### 8.3 烧录

```bash
python3 tools/river_flash.py -p /dev/ttyUSB0
```

### 8.4 监视

```bash
python3 /root/ameba-rtos-1.2/ameba.py monitor -p /dev/ttyUSB0 -b 1500000
```

### 8.5 预期日志

先看这些：

```text
[river][wifi] autoconnect init: ssid=Keeu retry_ms=5000
[river][cloud] online asr provider init: iflytek_rtasr stream=yes batch=no
[river][voice] vad probe started
```

联网成功后：

```text
[river][wifi] connected ssid=Keeu ip=...
```

语音开始进入流式桥时：

```text
[river][cloud] asr bridge open: provider=iflytek_rtasr ...
[river][cloud][iflytek] stream open: ...
```

识别返回后：

```text
[river][asr][iflytek_rtasr] session started sid=...
[river][asr][iflytek_rtasr] partial sid=... text=...
[river][asr][iflytek_rtasr] final sid=... text=...
[river][asr][iflytek_rtasr] session closed sid=...
```

---

## 9. 如何审核这次接入是否靠谱

建议按下面顺序审核：

### 9.1 先审架构边界

确认：
- provider 逻辑不在 `river_voice`
- 本地 VAD 不直接写 WebSocket
- `river_core` 只收结果，不处理协议细节

### 9.2 再审认证与协议

重点看：
- `river_iflytek_build_url()`
- `river_iflytek_sign_query()`
- `river_iflytek_handle_text_message()`

### 9.3 再审资源与时序

重点看：
- Wi-Fi 连接是否稳定
- UTC/SNTP 就绪后才签名开流
- 流式 pre-roll / post-roll 是否符合当前 VAD 策略

### 9.4 最后审功能覆盖

确认当前状态不是“都实现了”，而是：
- streaming：已实现
- batch：接口已打通，provider 未实现

---

## 10. 当前已知限制

1. 当前 provider 只有 `iflytek_rtasr` 一个。
2. 当前 batch 接口仍未在讯飞 provider 里实现。
3. 当前认证信息和 Wi-Fi 信息放在 `.h` 文件中，仅适用于 bring-up。
4. 当前本地环境无法替代真实板端联网验证。
5. 当前流式 ASR 结果还没有进入真正的家居控制意图层，只是先完成“识别结果可回调、可观察”。

---

## 11. 下一步建议

1. 先在板端确认：
   - Wi-Fi 能稳定连上
   - RTASR 能返回 partial / final
2. 之后收紧 VAD 与 streaming 打开/关闭策略。
3. 再把 `FINAL` 文本接到在线家居控制意图解析层。
4. 后续再补：
   - 讯飞 batch
   - 第二家 ASR provider
   - 安全存储
   - 会话重连与异常恢复
