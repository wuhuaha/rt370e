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

## Step 1.1: ASR/TTS 中文显示闭合

- 原因分析：
  - 2026-05-30 14:34 的板端日志显示 server STT/TTS 中文已正常进入 Orvibo protocol/app 层。
  - LVGL 页面原先的 ASR/TTS label 仍使用 `LV_FONT_DEFAULT`；当前 SDK `port/amebasmart/lv_conf.h` 中默认字体为 `lv_font_montserrat_14`，不包含 CJK glyph。
  - SDK CJK 字体子集不是默认字体，且不能覆盖本轮日志中的全部关键简体字，因此项目侧自带精简字体更可控。
- 已完成：
  - 新增 `components/river_ui/river_lv_font_zh_16.c`，16px/bpp=2 SourceHanSansSC LVGL 子集，覆盖当前 ASR/TTS 日志和智能家居常用中文。
  - `river_lvgl_port.c` 将 ASR/TTS label 字体切到 `river_lv_font_zh_16`。
  - `river_orvibo_ui.c` 对 UI 文本复制后做 UTF-8 安全截断，避免长中文句子截断半个字符。
- 验证：
  - `/root/ameba-rtos` 完整 build 已通过。
  - AP `lib_river_ui.a` 和 `target_img2_ap.axf` 均确认保留 `river_lv_font_zh_16`。
  - 仍需用户上板执行 `river ui text asr 帮我开灯`、`river ui text tts 已为你打开客厅，现在光线更充足了。`，确认屏幕中文实际渲染。

## Step 1.2: Noto cat emoji 动画播放

- 已完成：
  - 在 `components/river_ui/assets/noto_cat_lvgl/` 生成固件可编译的 Noto cat LVGL 动画资源。
  - 10 个 Noto cat GIF 均离线转换为 `80x80`、每个动画 8 帧、`LV_COLOR_FORMAT_ARGB8888`/BGRA 字节序的 `lv_image_dsc_t`。
  - `river_lvgl_port.c` 使用 `lv_animimg` 播放表情动画，并保留小号 ASCII caption；不启用 SDK GIF decoder，也不引入运行时文件系统依赖。
  - Orvibo UI 状态默认 emoji 切到 Noto cat 资源 key，LLM emotion/诊断输入通过资源别名解析到对应动画。
  - 新增 `river ui emoji <key>` 诊断别名，便于上板快速切换 `smiley/smile/joy/heart/smirk/kissing/pouting/crying/scream/face`。
- 验证：
  - `/root/ameba-rtos` 完整 build 已通过。
  - AP image 保留 `river_noto_cat_anim_*`、`lv_animimg_*` 符号和 10 个 Noto cat key。
  - 仍需用户手动烧录后验证屏幕动画实际循环播放和状态/LLM emotion 切换。

## Step 1.3: 语音交互状态表情映射

- 简化设计：
  - `starting/idle` 显示中性猫脸 `noto_cat_face_1f431`。
  - `network_wait/connecting` 显示微笑猫 `noto_smile_cat_1f638`。
  - `listening` 显示放松笑脸猫 `noto_smiley_cat_1f63a`。
  - `speaking` 显示开心猫 `noto_joy_cat_1f639`。
  - `recovering/error` 显示 `noto_pouting_cat_1f63e` / `noto_scream_cat_1f640`。
- LLM emotion 作为状态的临时覆盖：仅已知 emotion 覆盖，未知 emotion 回退到最新 voice state 表情。
- 新增 `river ui state <name>` 诊断入口，便于上板依次验证 `idle/listening/speaking/recovering/error`。

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
