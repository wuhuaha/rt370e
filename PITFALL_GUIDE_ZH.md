# RTL8730E 嵌入式联网“填坑”指南

本文件记录了在 `ameba-river` 项目开发中遇到的典型硬件/SDK 级陷阱及对应的解决方案，为后续开发提供参考。

---

## 陷阱 1：Wi-Fi 驱动背景竞态 (Race Condition)
- **现象**：启动时频繁出现 `RTK_ERR_BUSY (-3)`，且应用层无法控制连接过程。
- **原因**：SDK 默认开启了 `Fast-connect`，在应用层任务启动前，驱动已根据 Flash 残留信息开始连接。
- **对策**：
    1.  在 `wlan_init` 之前显式调用 `wifi_fast_connect_enable(0)`。
    2.  如果仍有干扰，启动时调用 `erase_wifi_config()`。
    3.  采用“主动接管策略”：检查 `wifi_get_join_status`，若已连接则直接跳入 DHCP 流程。

## 陷阱 2：L2 成功但 L3 DHCP 失败 (The DHCP Wall)
- **现象**：日志显示 `[$]wifi connected`，但随后 `[$]wifi got ip timeout` 导致断连。
- **原因**：
    - **省电模式干扰**：`LPS/IPS` 开启时，驱动可能漏掉 DHCP 的广播 ACK 包。
    - **RSSI 信号太弱**：信号低于 `-75dB` 时，即使 L2 关联成功，L3 握手包也极易丢失。
- **对策**：
    - **物理锁定**：在连接和 DHCP 全过程中，强制锁定驱动工作在 `Active` 模式。
    - **RSSI 门限**：在扫描阶段直接过滤掉低于 `-80dB` 的 AP，避免无效的策略切换。

## 陷阱 3：云端鉴权的时间依赖 (SNTP Bottleneck)
- **现象**：Wi-Fi 已连，但 ASR 无法启动，日志报错：`status=-4 wifi=connected time_ready=no`。
- **原因**：iFlytek 等 API 签名（HMAC）严重依赖系统 UTC 时间。即便 Wi-Fi 连上，如果 SNTP 没同步，鉴权必败。
- **对策**：
    1.  将 `SNTP_Ready` 状态作为 ASR 链路开启的硬性闸门（已在 `river_cloud_adapter` 实现）。
    2.  在获取 IP 后立即**强制触发**一次 SNTP 同步，缩短从“联网”到“可用”的空白期。

## 陷阱 5：凭据敏感度与“不可见字符” (Credential Sensitivity)
- **现象**：Wi-Fi 驱动正常，信号强（>-70dB），代码逻辑无误，但始终返回 `-4109(auth_fail)`。
- **原因**：
    1.  从外部文档粘贴的密码可能带有**零宽空格 (Zero-Width Space)** 或非标准的 ASCII 字符。
    2.  `RIVER_WIFI_STA_PASSWORD` 宏定义中可能包含了不可见的 `\r`（由于 Windows/Linux 换行符不一致）。
- **对策**：
    1.  手动重新输入密码字符串，避免使用复制粘贴。
    2.  在连接前打印密码长度 `strlen()`，验证是否符合预期。

  - 在应用层增加 `connection_latched` 之类的单次成功闸门。
  - 只有在 `disconnect`、显式重连或 IP 变化后，才允许再次打印连接成功。

## 陷阱 5：VAD 和在线 ASR 没日志，不代表链路没跑
- **现象**：开发者明明说话了，但串口里看不到 `vad` 或 `asr` 日志。
- **原因**：
  - 当前默认策略是“只在 VAD 状态变化时打印 `INFO`”，不是高频刷屏。
  - 在线 ASR 还受 `Wi-Fi connected + SNTP ready + stream active` 三重门槛控制。
- **对策**：
  - 至少打印一次 `vad initial state`，避免误判为 VAD 没工作。
  - 在 `speech` 触发但云端流未激活时，打印明确原因，例如 `wifi=disconnected`、`time_ready=no`、`stream deferred`。
  - 不要仅凭“没有识别结果”就判断模型或移植失败，先看前置门槛日志。

---
