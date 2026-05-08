# 讯飞在线 TTS `ws` 接入说明

## 1. 目标

当前项目已接入讯飞在线语音合成 WebSocket 接口，定位为：

- 作为板端播报能力的第一版在线 TTS
- 复用当前项目已有的 `AppID / APIKey / APISecret`
- 使用 `ws://tts-api.xfyun.cn/v2/tts`
- 不使用 `wss`

当前链路是：

```text
river tts <text>
-> river_cloud_adapter_submit_text()
-> river_tts_iflytek_submit_text()
-> ws://tts-api.xfyun.cn/v2/tts
-> PCM16 mono
-> PlaybackService
-> speaker
```

## 2. 官方接口与当前实现

官方文档：

- https://www.xfyun.cn/doc/tts/online_tts/API.html

当前实现文件：

- [river_tts_iflytek_ws.c](/root/ameba-river/components/river_cloud/river_tts_iflytek_ws.c)
- [river_tts_iflytek_credentials.h](/root/ameba-river/include/river/river_tts_iflytek_credentials.h)
- [river_ws_dispatch.c](/root/ameba-river/components/river_cloud/river_ws_dispatch.c)

本机联调脚本：

- [iflytek_tts_ws_debug.py](/root/ameba-river/tools/tts/iflytek_tts_ws_debug.py)

## 3. 鉴权结论

需要特别注意：

- TTS 与当前 RTASR ASR 使用的是同一组 `AppID / APIKey / APISecret`
- 但 **鉴权算法不是 RTASR 的那套 query 签名**

TTS WebSocket 鉴权按官方文档要求使用：

```text
signature_origin =
  host: tts-api.xfyun.cn
  date: <RFC1123 GMT>
  GET /v2/tts HTTP/1.1
```

然后：

```text
signature = base64(hmac-sha256(signature_origin, apiSecret))
authorization_origin =
  api_key="...", algorithm="hmac-sha256",
  headers="host date request-line", signature="..."
authorization = base64(authorization_origin)
```

最终 URL 形式：

```text
ws://tts-api.xfyun.cn/v2/tts?authorization=...&date=...&host=tts-api.xfyun.cn
```

## 4. 当前实现参数

当前默认业务参数：

- `aue=raw`
- `auf=audio/L16;rate=16000`
- `tte=UTF8`
- `vcn=xiaoyan`
- `speed=50`
- `volume=50`
- `pitch=50`

TTS 服务返回：

- `TextMessage` JSON 帧
- `data.audio` 为 base64 编码音频
- 当前项目按 `PCM16 / 16k / mono` 解码

然后板端会：

- 对齐成 `16ms / 256 samples` 单声道帧
- 扩成双声道播放
- 通过 `PlaybackService` 输出到扬声器

## 5. ASR 与 TTS 并存处理

由于 Ameba SDK 的 `ws_dispatch()` 是全局回调，ASR 和 TTS 如果都直接注册，会互相覆盖。

因此当前项目新增了共享分发层：

- [river_ws_dispatch.h](/root/ameba-river/components/river_cloud/river_ws_dispatch.h)
- [river_ws_dispatch.c](/root/ameba-river/components/river_cloud/river_ws_dispatch.c)

作用：

- 按 `wsclient_context *` 路由消息
- 允许 RTASR ASR 与 TTS 各自拥有独立回调

## 6. 板端使用方式

当前串口命令：

```text
river tts 你好，这是 TTS 测试
```

兼容旧命令：

```text
river echo 你好，这是 TTS 测试
```

状态查看：

```text
river status
```

关键日志：

- `online tts provider init: iflytek_ws speak=yes scheme=ws`
- `tts submit text=...`
- `tts speak success sid=... chunks=... audio_bytes=...`
- `tts provider=iflytek_ws ...`

## 7. 本机联调结果

当前机器已通过 Python 脚本完成 `ws` 联调验证：

- 成功建立 WebSocket 握手
- 成功发送 TTS 请求
- 成功接收多段 `audio` JSON 帧
- 成功输出 PCM 文件

调试命令：

```bash
python3 -u /root/ameba-river/tools/tts/iflytek_tts_ws_debug.py \
  --app-id 596745ad \
  --api-key b844c42f4a7aa3f776d9a95a5e015ef4 \
  --api-secret e343debbb6bb027932b34e92f9c7cd5e \
  --text "你好，这是ameba-river的TTS联调测试。" \
  --output /tmp/iflytek_tts_debug.pcm
```

## 8. 当前已知限制

- 当前是同步一次性 TTS 调用，不是后台播报队列
- 当前没有做 TTS 请求排队和打断策略
- 当前默认等待固定 `250ms` 作为播放 drain 收尾
- 当前没有引入 TTS cache
- 当前没有接入更高层的播报任务调度器

## 9. 后续建议

建议后续按下面顺序继续演进：

1. 将 TTS 请求收敛到 `PlaybackService + InteractionState` 调度框架
2. 引入播报任务优先级与可打断策略
3. 增加短句缓存与失败重试
4. 再考虑与 AEC / barge-in 做联动验证
