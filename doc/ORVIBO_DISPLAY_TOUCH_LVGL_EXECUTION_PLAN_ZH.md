# Orvibo RTL8730E 显示与触摸 LVGL 适配执行计划

## 目标

基于 4 寸 RTL8730E 硬件说明书，在项目内接入 LVGL 显示与触摸 bring-up 路径。第一阶段按 XiaoZhi-compatible 协议事件显示：

- LLM emotion / emoji 状态
- ASR/STT 文本
- TTS sentence_start 文本
- Orvibo 客户端运行状态

## 约束

- 不修改 `/root/ameba-rtos` SDK 源码。
- 新增代码使用 Orvibo/River 命名，XiaoZhi 只作为外部 wire contract 兼容说明。
- ST7102 面板 lane 数保持 Kconfig 可配置；硬件 CN6 路由 DSI D0/D1 两条 data lane，但供应商 init 文件写 `SSD_LANE(1,0)`，上板前不能把 lane 固化为唯一事实。
- 触摸第一阶段只做 reset、I2C probe 和 LVGL pointer 占位；坐标寄存器/报文格式需上板 ACK 与寄存器验证后再闭合。
- 不触碰 VAD/KWS、tensor dump、alignment replay、board/local parity 路径。

## Step 1: 项目内 UI 框架与协议文本显示

- 新增 `components/river_ui` 与 `include/river/river_orvibo_ui.h`。
- 提供 UI queue/task，core/app 线程只投递状态与文本，不直接操作 LVGL。
- LVGL 屏幕布局显示状态、emoji/emotion、ASR、TTS 和 touch 状态。
- 接入 Orvibo app 的 state/server-text 事件：
  - `stt.text` -> ASR
  - `tts.sentence_start.text` -> TTS
  - `llm.emotion/text` -> emotion/emoji
- 新增 monitor 诊断：
  - `river ui status`
  - `river ui touch scan`
  - `river ui text <asr|tts|emoji> <text>`

## Step 2: ST7102 MIPI/LCDC bring-up 验证

- 项目内实现 ST7102 power/reset/backlight、MIPI DSI init、LCDC page flip。
- 复用 SDK low-level `MIPI_*` / `LCDC_*` API，不复用 SDK ST7701S 面板表。
- 上板验证 lane=1 与 lane=2 两组 build/profile 的点亮情况、MIPI error 和画面稳定性。

## Step 3: Sitronix touch 坐标闭合

- 使用 `river ui touch scan` 确认 I2C 地址 ACK、reset/int 时序和候选 chip id/status 寄存器。
- 根据实板读数与供应商 ST7102/ST712x 资料补齐坐标读取、按下/释放状态和方向变换。
- 接入 LVGL pointer read callback。

## 当前验证边界

本计划的初始切片以 `/root/ameba-rtos` 完整 build 通过为固件侧交付标准。按当前硬件策略，不主动烧录或串口 monitor；面板点亮、lane 选择和触摸坐标由用户手动上板验证。
