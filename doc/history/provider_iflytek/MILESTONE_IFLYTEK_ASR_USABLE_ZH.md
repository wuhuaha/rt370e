# 里程碑说明：讯飞在线 ASR 可用

日期：2026-03-13

## 当前结论

当前项目已经达到“可在板端联网后发起讯飞在线语音识别会话”的可用状态。

已确认打通的链路：

- `RTL8730E` 启动与固件加载
- Wi-Fi 自动连接并获取 IPv4 地址
- 系统 UTC 时间可用
- 语音采集、VAD 判定与云端音频桥接
- 讯飞 RTASR LLM WebSocket 鉴权、DNS、连接建立
- 板端异步 ASR worker 启动与流式发送链可工作

## 本里程碑包含的关键修改

### 1. Wi-Fi 连接可靠性增强

- 关闭 SDK fast-connect / auto-reconnect 对首次连接的干扰
- 引入多 AP 候选与扫描优先连接
- 加强 DHCP 获取与连接状态接管逻辑

### 2. 在线 ASR 框架接入

- 建立 provider registry，支持后续接入多家平台
- 接入讯飞 RTASR LLM 流式识别 provider
- 预留 batch/non-streaming 边界，当前 provider 仅实现 streaming

### 3. 语音前端到云端桥接

- 建立 `capture -> preproc -> detector -> cloud bridge` 主链路
- 纯 VAD probe 模式可直接把语音送入在线 ASR
- 通过日志可观测 VAD 状态变化与云端流状态

### 4. 讯飞协议与 SDK 兼容修复

- 修复 URL 拆分，避免 `getaddrinfo` 把 `host/path?query` 当成主机名
- 修复无 `code` 的成功消息误判为错误
- 支持 `data.action=started`
- 支持 Ameba `wsclient` 将文本帧以 `CONTINUATION` 交付的情况
- 修复本地发送背压与节流
- 补充 `frc` / 空文本 final 的显式上报

## 当前已知限制

- 当前讯飞 provider 仍以流式链路为主，batch 路径未完成
- 当前声学链路允许 `AFE bypass` 作为 bring-up 策略，不是最终产品态
- 部分云端返回场景仍需要继续观察，例如：
  - 空结果 final
  - 非文本结果帧
  - 长语音会话稳定性

## 建议的下一步

1. 固定并验证一段标准测试音频，确认能稳定得到 `partial/final`
2. 补充宿主侧调试脚本与板端结果对照流程
3. 恢复/优化 AFE，而不是长期停留在 bypass 模式
4. 在当前 provider 边界下继续接入第二家 ASR 平台，验证多 provider 架构

