# TTS Tools

## iFlytek `ws` debug script

脚本：

- [iflytek_tts_ws_debug.py](/root/ameba-river/tools/tts/iflytek_tts_ws_debug.py)

用途：

- 在宿主机上直接验证讯飞在线 TTS `ws://` 接口
- 验证 RFC1123 GMT + HMAC-SHA256 鉴权是否正确
- 输出返回的 PCM 文件，作为板端联调前的最小闭环

示例：

```bash
python3 -u /root/ameba-river/tools/tts/iflytek_tts_ws_debug.py \
  --app-id 596745ad \
  --api-key b844c42f4a7aa3f776d9a95a5e015ef4 \
  --api-secret e343debbb6bb027932b34e92f9c7cd5e \
  --text "你好，这是ameba-river的TTS联调测试。" \
  --output /tmp/iflytek_tts_debug.pcm
```

输出：

- 成功时会打印 `sid / audio_chunks / audio_bytes`
- PCM 文件默认输出到 `/tmp/iflytek_tts_ws_debug.pcm`

说明：

- 当前脚本使用 `ws`
- 不使用 `wss`
- 设计目标是尽量少依赖第三方 Python 包，便于在当前机器上快速联调
