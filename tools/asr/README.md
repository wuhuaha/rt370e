# ASR 调试工具

当前目录用于放置独立于板端固件的在线 ASR 调试工具。

## 目标
- 在不依赖 `RTL8730E` 固件的情况下，先验证云端 ASR 接口是否正常
- 快速区分问题来自：
  - 账号鉴权
  - WebSocket 协议接入
  - 音频格式
  - 板端网络 / 音频链路

## 当前工具
- `iflytek_rtasr_llm_debug.py`
  - 用于验证科大讯飞“实时语音转写大模型”接口
  - 对接文档：
    - `https://www.xfyun.cn/doc/spark/asr_llm/rtasr_llm.html`

## 依赖
- Python 3
- `websocket-client`

示例：

```bash
python3 -m pip install websocket-client
```

## 输入音频要求
- 16kHz
- 16bit
- 单声道
- PCM
- 推荐每 40ms 发送 1280 字节

## 建议用法
1. 优先在 PC/开发机上用该脚本验证鉴权和协议。
2. 验证通过后，再回到板端排查：
   - Wi-Fi
   - 时间同步
   - VAD 触发
   - 音频上行节奏

