# RTL8730E 4 寸板硬件固件说明书

日期：2026-05-29
对象：Orvibo RTL8730E 4 寸板，主原理图 `RTOS_4InchLCD` / `MixPad4_RTL8730`，Rev `V0.1`，日期 `2026-05-06`。
目的：把 `doc/hard/` 中的原理图、丝印图、器件资料、SDK 默认值和本项目绑定点整理成固件同事可执行的 BSP/HAL/driver handoff。
交互版：同目录 `doc/RTL8730E_4INCH_HARDWARE_FIRMWARE_GUIDE_ZH.html` 提供离线搜索、过滤、硬件路径图和 board-validation checklist，内容与本 Markdown 证据编号保持一致。

## Summary

- 数字麦克风是 PDM/DMIC：`PDM_CLK -> RTL8730E PA2`，`PDM_DAT1 -> RTL8730E PA4`。AmebaSmart Audio HAL DATA1 对应 `AUDIO_DMIC3/DMIC4`，项目已固定 `AUDIO_HW_DMIC_CLK_PIN=_PA_2`、`AUDIO_HW_DMIC_DATA1_PIN=_PA_4`。
- 音频输出是 RTL8730E 内部 codec line-out 差分输出到 AXS2033 功放：`AMEBA_AUDIO_DEVICE_SPEAKER -> APP_LINE_OUT -> LINEOUT_LN/LP -> C76/C153 + R56/R57 -> AXS2033 -> CN2 speaker`。功放 `SHUT/SD` 控制网名 `MUTE` 接 `PB25`，项目已覆盖 SDK 默认功放脚为 `_PB_25`。
- 当前固件默认输出音量已调到最大：MCP 本地默认 `100`，Orvibo playback、diagnostic tone、echo 调试路径硬件/软件播放音量均为 `1.00f`。若仍小声，应优先测 U7 SD 电压、LINEOUT 差分幅度、AXS2033 输出和喇叭负载，而不是继续猜上层播放路径。
- 启动 Flash 是 GigaDevice `GD5F1GM7UEYIGR` SPI NAND，原理图标注 128MB。项目烧录 wrapper 默认使用 NAND profile，当前硬件不要使用 NOR profile。
- LCD 走 MIPI DSI，原理图路由 D0/D1/CLK 两条 data lane 加 clock lane 到 CN6；本地 ST7102 init table 写 `SSD_LANE(1,0)`，与两条 data lane 路由存在待确认冲突。
- 触摸走 Sitronix ST71xx/ST7102 风格 I2C：`PB10/PB11 -> TP_SDA2/TP_SCL2`，候选地址 `0x55` 来自供应商源码包和移植手册示例，必须在上电/reset 后 scan 和读 chip id。
- BL702 Zigbee 子系统由 RTL8730E 通过电源、复位、boot strap 和 UART1 管理。`BOOT_BL702` 对应 BL702 GPIO28，原理图标注 `0 boot from Flash, 1 boot from Uart`，默认路径必须避免拉高进入下载模式。
- 温湿度 FPC 只有 `TH_I2C_SDA/SCL` 和 3.3V，没有传感器型号、地址和寄存器资料；当前只能做总线扫描和硬件澄清。

## Report Boundary

- 面向读者：BSP、HAL、外设驱动、pinmux、启动下载、电源/复位时序、板级诊断和测量的软件同事。
- 范围内：SoC 引脚、外设实例、SDK 默认值差异、驱动配置、板级电路路径、预期底层日志、诊断命令、示波器/逻辑分析仪测点和失败排查树。
- 范围外：产品交互、网络服务策略、应用层播放/录音策略、会话状态机、UI 行为。运行日志里出现的产品名只能作为验证背景，结论必须落回硬件或 HAL/driver 层。

## Evidence Index

| ID | Claim | Source | Type | Confidence | Notes |
| --- | --- | --- | --- | --- | --- |
| E1 | `PDM_CLK` 接 `PA2`，`PDM_DAT1` 接 `PA4` | `doc/hard/RTL8730 4寸SCH.pdf` P09/P11；用户硬件说明 | schematic + user fact | A | P09/P11 文本和用户说明一致。 |
| E2 | `AUDIO_DMIC3/DMIC4` 使用 DATA1，DMIC clock 使用 `AUDIO_HW_DMIC_CLK_PIN` | `/root/ameba-rtos/component/audio/audio_hal/amebasmart/ameba_audio_stream_capture.c` | SDK | A | DATA0/1/2/3 分别对应 DMIC1/2、3/4、5/6、7/8。 |
| E3 | AmebaSmart SDK 默认 DMIC/amp pins 是 reference-board 配置，不匹配本板 | `/root/ameba-rtos/component/soc/usrcfg/amebasmart/include/ameba_audio_hw_usrcfg.h` | SDK | A | 默认 DMIC 与 amp pin 均需要项目覆盖。 |
| E4 | 项目覆盖 PDM 和功放 pin | `include/river/river_audio_hw_overrides.h`、`CMakeLists.txt` | code | A | AP `audio_hal_${c_CURRENT_IMAGE}` 编译时 `-include` 项目覆盖头。 |
| E5 | PA2/PA4 DATA1 已实板验证能采到有效语音 | `.codex/changes.md` Step H.xiaozhi-client.69 | runtime | A | 采集/预处理 peak 非零，VAD/KWS 底层链路有输入。 |
| E6 | 当前项目输出音量默认已调满 | `components/river_cloud/river_orvibo_mcp_volume.c`、`components/river_voice/river_orvibo_audio_service.c`、`components/river_diag/river_diag_cmd.c`、`components/river_voice/river_voice_echo.c` | code | A | MCP 默认 `100`，硬件/软件播放 volume `1.00f`。 |
| E7 | MSM261DDB021 是 PDM 数字麦，VDD 1.6-3.6V，Standard Performance clock 1.1-4.8MHz，典型测试 2.4MHz，L/R 由 DATA 边沿选择 | `doc/hard/1.01.070080 MSM261DDB021_Rev1.0.pdf` p3-p10/p14 | datasheet | A | 低功耗/标准模式的 clock 区间不同。 |
| E8 | AXS2033 是单声道 AB/D 类功放，VDD 2.5-5.5V，SD 电压区间决定 shutdown/AB/D 防破音模式 | `doc/hard/AXS2033.pdf` p1-p5；ChipSourceTek AXS2033 page | datasheet + vendor web | A/B | SD 不是简单数字 enable。 |
| E9 | `LINEOUTLN/LP` 接 AXS2033 `IN-/IN+`，`SPKP/SPKN` 到 CN2 | `doc/hard/RTL8730 4寸SCH.pdf` P08 | schematic | A | CN2 是 BTL/differential speaker output。 |
| E10 | `MUTE` 接 RTL8730E `PB25` 并进入 AXS2033 `SHUT/SD` | `doc/hard/RTL8730 4寸SCH.pdf` P08/P11 | schematic | A | `PB25 -> MUTE -> R20/R21/C69/C71 -> U7 pin1`。 |
| E11 | `GD5F1GM7UEYIGR` 是板上 SPI NAND，原理图标注 128MB | `doc/hard/RTL8730 4寸SCH.pdf` P11；GigaDevice product page | schematic + vendor web | A/B | GigaDevice page 标注 1Gb SPI NAND。 |
| E12 | Realtek Image Tool 区分 NAND/NOR profile | Realtek Ameba IoT Image Tool docs | vendor docs | B | 项目 wrapper 固定当前硬件 NAND profile。 |
| E13 | LCD CN6 路由 MIPI DSI D0/D1/CLK、reset、touch I2C/INT/RST 和 backlight | `doc/hard/RTL8730 4寸SCH.pdf` P10 | schematic | A | CN6 `A113F-15025WUA-R01`。 |
| E14 | ST7102 init table 标注 `SSD_LANE(1,0)`、TE `0x35`、sleep out `0x11` 后 250ms、display on `0x29` 后 200ms | `doc/hard/LCD/ST7102_480480_60hz_0109_tp 3ms .txt` | vendor init table | B | lane 数与原理图两 lane 路由需澄清。 |
| E15 | Sitronix 手册说明 ST7123/ST7121P/ST7123P/ST7102 支持 I2C/SPI，I2C 示例地址 `0x55`，IRQ falling，RST active-low | `doc/hard/LCD/Sitronix Touch Driver 移植手册.pdf`、`sample.dtsi` | vendor guide/source | B | 示例偏 Linux DTS，Ameba 侧需迁移。 |
| E16 | BL702 支持 BLE/Zigbee/Thread，本板 UART1 连接 BL702 GPIO14/15 | `doc/hard/RTL8730 4寸SCH.pdf` P12；Bouffalo Lab product page | schematic + vendor web | A/B | UART 协议/波特率仍缺失。 |
| E17 | BL702 GPIO28 boot strap：0 从 Flash，1 从 UART | `doc/hard/RTL8730 4寸SCH.pdf` P12 | schematic | A | 默认应保持 `BOOT_BL702` 低。 |
| E18 | TH FPC 只暴露 3.3V、`TH_I2C_SDA/SCL` | `doc/hard/RTL8730 4寸SCH.pdf` P13 | schematic | A | 无传感器型号/地址。 |
| E19 | RTL8730E Hardware Design Guide 可作为 SoC 供电结构和硬件设计交叉参考 | Realtek RTL8730E Hardware Design Guide R2.3 | vendor docs | B | 不替代本板原理图。 |

## Artifact Inventory

| Artifact | Path / URL | Pages / Sheets Used | Extraction Method | Notes |
| --- | --- | --- | --- | --- |
| 主原理图 | `doc/hard/RTL8730 4寸SCH.pdf` | P05-P13 | PyMuPDF text + render | 关键页已视觉复核。 |
| 丝印图 | `doc/hard/RTL8730_4寸_丝印图.pdf` | p1-p2 | render | 无文本层，仅辅助连接器/测试点定位。 |
| PDM 麦资料 | `doc/hard/1.01.070080 MSM261DDB021_Rev1.0.pdf` | p3-p10/p14 | text extraction | Rev 1.0，本地资料优先。 |
| 功放资料 | `doc/hard/AXS2033.pdf` | p1-p8 | text extraction + render | SD 电压区间、VDD、输出功率、gain 公式来自本地 PDF。 |
| LCD init table | `doc/hard/LCD/ST7102_480480_60hz_0109_tp 3ms .txt` | full file | direct text | 缺完整 timing sheet。 |
| Sitronix touch 手册 | `doc/hard/LCD/Sitronix Touch Driver 移植手册.pdf` | p1-p11/p18 | text extraction + render | 提供 bus/reset/IRQ/firmware loading 参考。 |
| Sitronix source bundle | `doc/hard/LCD/ST_TDDI_TPDriver_v45.00.260402/` | `sample.dtsi`、`sitronix_ts*.c/h` | source review | 提供 `0x55` 候选、probe/self-test 入口。 |
| SDK | `/root/ameba-rtos` | audio HAL、usrcfg、fwlib headers、flash tools | source review | 用于确认 reference default 和 override 位置。 |
| Project firmware | `/root/ameba-river` | `include/river/`、`components/`、`tools/` | source review | 用于列出已落地 board constants、diagnostics 和 flash wrapper。 |
| External references | Realtek/GigaDevice/Bouffalo/ChipSourceTek/MEMSensing pages | docs/product pages | web lookup | 用于型号/能力交叉确认；关键配置仍以本地原理图和 SDK 为准。 |

## Engineer-Supplied References

| Material | Path / URL | Provider / Context | Used For | Version / Revision Check |
| --- | --- | --- | --- | --- |
| 当前硬件资料目录 | `doc/hard/` | 用户指定 | 全板硬件输入 | 主原理图 Rev `V0.1`，日期 `2026-05-06`。 |
| LCD/触摸资料 | `doc/hard/LCD/` | 用户补充 | ST7102 panel init、Sitronix touch 迁移参考 | `ST_TDDI_TPDriver_v45.00.260402` 已纳入。 |
| PDM 连接事实 | 用户说明：`PDM_CLK 接 PA2, PDM_DAT1 接 PA4` | 用户/硬件上下文 | 数字麦 pin map | 与主原理图 P09/P11 一致。 |
| 已验证运行记录 | `.codex/changes.md` Step H.xiaozhi-client.69、Step H.xiaozhi-client.77 | 项目记录 | PA2/PA4 DATA1 实板采集、输出音量满量程配置 | 与当前固件绑定点一致。 |

## Missing Inputs And Clarifications

| Missing Item / Question | Why It Matters To Firmware | Current Best Guess | Confidence | Recommended Owner / Check |
| --- | --- | --- | --- | --- |
| LCD 完整 timing sheet：lane_count、lane rate、porch/sync、reset pulse、power/backlight 时序 | 决定 DSI host 配置和白屏/花屏定位 | ST7102 480x480/60Hz，init table 可做初始命令表；lane 数不能静默写死 | B | 屏厂/硬件提供 datasheet；示波器/DSI log 验证 D0/D1 是否均使用。 |
| Touch 实际地址、chip id、firmware/CFG 文件、是否需要 host download | 决定 probe、固件加载和触摸坐标输出 | `0x55` 是优先候选；INT falling、RST active-low | B | 上电 reset 后 I2C scan，读 SFR/chip id；供应商确认固件文件。 |
| TH FPC 外接传感器型号和地址 | 没有型号无法实现寄存器驱动 | 外接温湿度 I2C 模块 | C | 硬件提供 BOM/丝印；固件做 bus scan。 |
| CN7 MIC/IR 小板完整原理图 | 决定 PDM L/R 槽位、双麦通道顺序、IR LED 电流限制 | 两颗 PDM 麦共享 DATA1，通过 L/R 边沿复用 | B | 硬件提供小板原理图；逻辑分析 PDM DATA/CLK，近场单麦测试。 |
| AXS2033 `SHUT/SD` 实板电压 | SD 电压区间决定 shutdown/AB/D/防破音模式 | Audio HAL GPIO high 可能让 U7 进入 D 类防破音模式 1 | B | 测 U7 pin1 在 boot、idle、tone playback、stop 的电压。 |
| BL702 固件协议、UART 波特率、升级流程 | 决定 Zigbee 子系统驱动、复位和下载流程 | RTL8730 UART1 连 BL702 GPIO14/15，默认从 Flash boot | B | Zigbee 固件 owner 提供协议；串口抓 BL702 boot log。 |
| Realtek 完整 pinmux/reference manual | 扩展 LCD/IR/PWM/UART 时需核验复用限制 | 当前 PDM/audio/flash 已可由 SDK 和原理图闭合 | B | 若开发 LCD/IR/Zigbee 控制，补齐官方 pinmux 表。 |

## Hypotheses / Candidate Solutions

| Hypothesis | Evidence For | Evidence Against / Risk | Confidence | Suggested Firmware Path | Validation Step |
| --- | --- | --- | --- | --- | --- |
| 双 PDM 麦通过同一 DATA1 的左右边沿复用 | MSM261DDB021 有 L/R 边沿选择；CN7 只有 `PDM_DAT1`；DATA1 已实板验证有效 | 小板 L/R 接法未知，左右声道可能反 | B | 保持 `DMIC3/DMIC4`，通道顺序留成 board config | 分别遮挡/轻敲两颗麦，记录 ch0/ch1 peak。 |
| `PB25/MUTE` high 对应 AXS2033 工作态 | AXS2033 SD >2.3V 为 D 类防破音模式 1；SDK dummy amp enable 会拉 GPIO | 分压/RC 可能让 SD 落在中间区间 | B | BSP/HAL 层命名为 amp SD/mode control，不只叫 mute | 用 `river playback tone 1000 1000 25` 时测 U7 pin1。 |
| Touch 与 TH 可能共用 `PB10/PB11` I2C 总线 | P10 `TP_SDA2/SCL2` 经 R36/R37 接 `TH_I2C_SDA/SCL` | 地址冲突或 reset 时序可能影响 scan | B | 先做总线 scan，再按地址注册 touch/TH | 分别连接/断开 LCD、TH FPC 比较 scan 结果。 |
| LCD driver 首版应把 lane/timing 做成 board config | 原理图两 lane，init table 写 1 lane | 固件写死错误 lane 会导致白屏且难定位 | B | panel config 暴露 `lane_count`、`lane_rate`、porch 和 init table | 开背光前先确认 DSI LP/HS、reset 和 TE。 |
| BL702 默认从 Flash 启动，升级时才拉高 boot strap | 原理图直接标注 GPIO28 boot mode | 若默认高会卡在 UART boot | A | `BOOT_BL702` 默认低或输入下拉；升级流程临时拉高 | 拉低 boot 后复位，观察 BL702 是否正常启动。 |

## Quick Start For Firmware Engineers

1. 先读这几处：
   - `## Pitfalls / Attention Points / Getting Started Advice`
   - `## MCU / SoC Pin Map`
   - `### PDM Microphone / Audio Capture`
   - `### Speaker / AXS2033 / Audio Output`
   - `## Firmware Bring-Up Checklist`

2. 先确认构建与下载：

```bash
cd /root/ameba-river
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000
```

3. 首次音频输入看这些日志：

```text
board array: Orvibo-RTL8730E-PDM pdm-2mic-pa2-pa4-data1 usage=DMIC primary=DMIC3 secondary=DMIC4
capture board mics applied: usage=DMIC ch0=DMIC3 ch1=DMIC4
capture dmic pinmux applied: clk=PA2 data1=PA4
[AudioHal-I] ... set DMIC clock
```

4. 首次音频输出先跑本地 tone，不要从上层播放路径开始定位：

```text
river playback tone 1000 1000 25
river playback status
```

5. 先测这些点：
   - PDM：`PDM_CLK`、`PDM_DAT1`。
   - 功放：U7 `VDD`、U7 pin1 `SHUT/SD`、`LINEOUT_LN/LP`、U7 `OUTP/OUTN`、CN2。
   - LCD：`VCC_3V3_LCD`、`RST_LCD`、DSI D0/D1/CLK、`LCD_BL_PWM`。
   - Touch/TH：PB10/PB11 I2C pull-up、PA10 reset、PA9 IRQ。

6. 测到这些之前不要改：
   - 没测 PDM clock/data 前，不要再切 DATA0-3 或改 VAD/KWS。
   - 没测 U7 SD/LINEOUT/OUTP/OUTN 前，不要把小声或无声归咎于上层播放策略。
   - 没拿到 LCD timing 前，不要把 DSI lane/timing 写死。
   - 没确认 TH 型号前，不要写固定温湿度寄存器驱动。

## Pitfalls / Attention Points / Getting Started Advice

| Topic | Why This Is Easy To Get Wrong | Evidence | Firmware Impact | Confidence | First Check / Guardrail |
| --- | --- | --- | --- | --- | --- |
| SDK 默认音频引脚不是本板引脚 | AmebaSmart usrcfg 是 reference-board 默认，容易误以为 HAL 会自动适配 | E1-E4、E10 | 麦克风无输入、功放不使能、无声 | A | AP Audio HAL 必须 `-include include/river/river_audio_hw_overrides.h`；预处理应看到 PA2/PA4/PB25。 |
| `PDM_DAT1` 不是 DATA0 | SDK DATA1 对应 `DMIC3/DMIC4`，不是 `DMIC1/DMIC2` | E1/E2/E5 | 帧数增长但 peak 长期为 0，浪费时间调 VAD/KWS | A | 日志必须是 `DMIC3/DMIC4` 和 `clk=PA2 data1=PA4`；异常先测 PDM 波形。 |
| PDM 有帧不等于有有效语音 | `AudioRecord` 可能持续给帧，但 pinmux/clock/data 错时全零或低噪声 | E5/E7/runtime history | 把硬件输入问题误判为模型或阈值问题 | A | 先看 capture/preproc peak 和 PDM_CLK/PDM_DAT1，后动 VAD/KWS。 |
| MSM261DDB021 clock 模式影响性能 | datasheet 标准性能 clock 1.1-4.8MHz，测试条件 2.4MHz；低功耗模式 clock 区间不同 | E7 | 采样质量、噪声、功耗判断偏差 | A | 逻辑分析仪测实际 PDM clock；与 SDK DMIC clock selector 对齐。 |
| AXS2033 `SD` 不是普通 enable | SD 电压区间选择 shutdown、AB、D 类防破音模式 | E8/E10 | GPIO high 也可能落入非预期模式，小声/失真/无声难定位 | A | 每次播放 bring-up 都测 U7 pin1，而不是只看 GPIO log。 |
| CN2 是 BTL/differential 输出 | SPKP/SPKN 都是功放输出端，不是一个信号一个地 | E9 | 示波器接法错误、短路风险、误判无声 | A | 差分测 CN2；不要把任一端直接当 GND。 |
| 当前固件已经最大音量 | MCP 默认 `100`，playback/tone/echo 音量 `1.00f` | E6 | 小声时继续改上层音量接口收益低 | A | 小声优先测 U7 SD、AXS gain、LINEOUT 和喇叭阻抗。 |
| LCD lane 配置有实物路由与 init table 冲突 | 原理图有 D0/D1 两 lane，init table 写 `SSD_LANE(1,0)` | E13/E14 | 白屏/花屏，且容易误判为 reset 或背光 | B | lane_count 做成 board config；先验证 DSI D0/D1/CLK。 |
| 触摸 `0x55` 只是候选地址 | Sitronix 示例给 `0x55`，但实际屏/固件可能不同 | E15 | 固定地址导致 probe 失败或误绑 | B | LCD/touch 供电和 reset 后 scan，再读 chip id。 |
| Touch 与 TH 可能共享 PB10/PB11 | 原理图 TP I2C 与 TH I2C 通过串阻关联 | E13/E18 | 地址冲突、reset 干扰、scan 结果不稳定 | B | 总线 scan 日志记录所有 ACK；driver 绑定前确认设备组合。 |
| NAND/NOR profile 混用 | Realtek 工具分 profile，当前板是 GD5F1GM7 NAND | E11/E12 | 烧录直接失败或写错地址范围 | A | 当前默认命令用 `tools/river_flash.py ...`；看到 mismatch 先查 profile。 |
| BL702 boot strap 默认态关键 | GPIO28 高进入 UART boot，低从 Flash boot | E16/E17 | Zigbee 子系统不启动或卡下载态 | A | 正常 boot 保持 `BOOT_BL702` 低；升级流程显式拉高再 reset。 |
| 背光可能掩盖 DSI 问题 | 开背光后白屏不代表 DSI init 成功 | E13/E14 | 错把 panel timing 问题当 backlight 问题 | B | 先确认 power/reset/DSI/TE，再打开 `LCD_BL_PWM`。 |
| TH sensor 没型号不能写驱动 | 只有 FPC 网名，没有器件型号/地址 | E18 | 写错驱动会污染共享 I2C 调试 | A | 只能先做 bus scan 和硬件澄清。 |
| PA3/IR 不是普通 LED 测试点 | IR LED 由 5V rail、限流和 NMOS 驱动，需 carrier/PWM | schematic P08/P11 | GPIO toggle 不能代表有效 IR 发射 | B | 先确认 PA3 PWM/IR mux 和 LED 电流限制。 |
| RF/tx power 不应由软件随意改 | Wi-Fi/BT RF 匹配和法规参数属于硬件/RF 边界 | E19 + schematic RF path | 合规和射频性能风险 | B | 变更 country/tx power 前要硬件/RF review。 |

## MCU / SoC Pin Map

| Function | Net | MCU Pin | Alt Function / Instance | Voltage Domain | Pull / Default | Evidence | Firmware Action |
| --- | --- | --- | --- | --- | --- | --- | --- |
| PDM clock | `PDM_CLK` | `PA2` pin 5 | DMIC CLK | 3.3V digital/audio | 未见外部上下拉 | E1/E2 | `AUDIO_HW_DMIC_CLK_PIN=_PA_2`。 |
| PDM data | `PDM_DAT1` | `PA4` pin 7 | DMIC DATA1 | 3.3V digital | CN7 串 R29 100R | E1/E2/E5 | 使用 `AUDIO_DMIC3/DMIC4`。 |
| IR TX | `IR_TX` | `PA3` pin 6 | GPIO/PWM/IR candidate | 3.3V control, IR LED on 5V rail | Q3 NMOS low-side，R32 10K pull-down | P08/P11 | 38kHz 载波优先 PWM/IR；先确认 mux 和 LED 电流。 |
| BL702 reset | `RST_BL702` | `PA13` pin 15 | GPIO | 3.3V_Z | R52 100R，PU_CHIP 侧 R53 10K pull-up | P11/P12 | 默认释放；复位流程拉低再释放。 |
| BL702 boot | `BOOT_BL702` | `PA15` pin 17 | GPIO | 3.3V_Z | R47 20K pull-down，R46 100R | P11/P12 | 默认低；下载/恢复流程临时拉高。 |
| Zigbee power | `Zigbee_PWR_ON` | `PA5` pin 8 | GPIO | 3.3V control | Q1/Q2 switch | P05/P09 | 管理 `VCC_3V3_Z` 上电。 |
| UART1 to BL702 | `RTL8730_TX1/RX1` | `PB20/PB19` pins 67/66 | UART1 | 3.3V_Z | BL702 侧 R49/R50/R51 | P11/P12 | RTL8730 TX1 -> BL702 RX1；RTL8730 RX1 <- BL702 TX1。 |
| UART0/base | `RTL8730_TX0/RX0` | `PB24/PB23` pins 73/72 | UART0 | 3.3V_Z | R54/R55 10K pull-up | P05/P11 | CN1 debug/base interface。 |
| Amp SD/mode | `MUTE` | `PB25` pin 96 | GPIO / Audio HAL amplifier pin | 3.3V control to U7 SD | R20 100R，R21 10K，C69/C71 | E8/E10 | `AUDIO_HW_AMPLIFIER_PIN=_PB_25`；实测 U7 pin1。 |
| LCD power enable | `LCD_PWR_ON` | `PB26` pin 97 | GPIO | 3.3V control | U4 EN, R12/R14/C16 | P05/P11 | DSI/touch 初始化前上 `VCC_3V3_LCD`。 |
| LCD reset | `RST_LCD` | `PA14` pin 16 | GPIO | 3.3V_LCD | R34 100K pull-up，R35 1K | P10 | 按 panel 时序拉低/释放。 |
| Backlight PWM | `LCD_BL_PWM` | `PA16` pin 18 | PWM/GPIO | 3.3V control, 5V boost | R42 100R，R43 10K pull-down | P10 | panel init 完成后再开 PWM。 |
| Touch IRQ | `TP_INT` | `PA9` pin 12 | GPIO IRQ | 3.3V_LCD | R40 4.7K pull-up | P10/E15 | falling/active-low 候选，实测确认。 |
| Touch reset | `TP_RST` | `PA10` pin 13 | GPIO | 3.3V_LCD | R39 4.7K pull-up | P10/E15 | active-low 候选；reset 后读 chip id。 |
| Touch/TH I2C | `TP_SDA2/SCL2`, `TH_I2C_SDA/SCL` | `PB10/PB11` pins 57/58 | I2C | 3.3V_LCD / 3.3V | 4.7K pull-up, 100R series | P10/P13/E15/E18 | 先 100/400kHz scan；地址可配置。 |
| SPI NAND | `FLASH_QSPI_*` | `PC1-PC6` | QSPI NAND | 3.3V | U11 C102 1uF | P11/E11 | 使用 NAND boot/download profile。 |
| USB | `USB_DP/DN` | `HSDP/HSDM` pins 78/79 | USB | USB PHY | CN1 to base/debug | P05/P07 | 下载/调试按 Realtek 工具要求操作。 |
| Reset / chip enable | `CHIP_EN` | pin 71 | chip enable | 3.3V | R19 10K pull-up, SW2 to GND | P07 | 手动 reset，不等同断电。 |

## Peripheral Blocks

### Power, Power Key, Reset

- Components：`U1/U4 TMI6050-33` 3.3V LDO，`U3 TMI3411/2A` buck，`U2 EY404-CF42F1` power key，`Q1/Q2` Zigbee power switch。
- Rails：`VCC_5V_IN`、`VCC_3V3_HOLD`、`VCC_3V3_LCD`、`VCC_3V3_Z`、RTL8730E internal `VDD_0V9`/`VDDA_1V8`/`VDD_1V8`。
- Firmware boundary：固件可控制 LCD/Zigbee 子电源 enable，但不能假设 `CHIP_EN` reset 会清空所有外设电源状态。
- Pitfalls / attention points：LCD/Zigbee 上电顺序要和 reset/boot strap 串行；不要并行乱拉 GPIO。
- Validation：测 `VCC_3V3_HOLD`、`VDD_0V9`、`VDDA_1V8`、`VDD_1V8`、`VCC_3V3_LCD`、`VCC_3V3_Z`。

### SPI NAND / Boot / Flashing

- Component：`U11 GD5F1GM7UEYIGR/Nand Flash128MB/WSON8/GD`。
- Nets：`PC6 -> /CS`，`PC2 -> CLK`，`PC3 -> IO0/DI`，`PC5 -> IO1/DO`，`PC4 -> IO2/WP`，`PC1 -> IO3/HOLD`。
- Firmware/download action：

```bash
cd /root/ameba-river
export AMEBA_SDK_ROOT=/root/ameba-rtos
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000
```

- Expected tool facts：当前板应识别 `MemoryType: NAND`、`GD5F1GM7U`、`1Gb/128MB`。
- Pitfalls / attention points：`Flash type mismatch: Device: 2 / Device Profile: 1` 表示 NAND 硬件被 NOR profile 烧录；`Enter download mode fail: ErrType.SYS_PROTO` 表示尚未进入下载模式，通常未开始写 flash。

### PDM Microphone / Audio Capture

- Hardware path：CN7 `PDM_CLK/PDM_DAT1/VCC_3V3/GND` -> RTL8730E `PA2/PA4` -> Audio HAL DMIC DATA1 -> `AUDIO_DMIC3/DMIC4` -> `AudioRecord`。
- Mic facts：MSM261DDB021 是 bottom-ported PDM digital output MEMS microphone，VDD 1.6-3.6V，Standard Performance clock 1.1-4.8MHz，datasheet 测试条件 2.4MHz，L/R 选择 DATA 边沿。
- Project binding：
  - `include/river/river_audio_hw_overrides.h`：`_PA_2` / `_PA_4`。
  - `components/river_voice/river_voice_board.c`：`pdm-2mic-pa2-pa4-data1`，`AUDIO_DMIC3/DMIC4`。
  - `components/river_voice/river_voice_capture.c`：`DEVICE_IN_DMIC_REF_AMIC`、16kHz、2ch、16ms frame。
- Expected logs：

```text
board array: Orvibo-RTL8730E-PDM pdm-2mic-pa2-pa4-data1 usage=DMIC primary=DMIC3 secondary=DMIC4
capture board mics applied: usage=DMIC ch0=DMIC3 ch1=DMIC4
capture dmic pinmux applied: clk=PA2 data1=PA4
[AudioHal-I] ... set DMIC clock
capture params applied: ret=0 params=cap_mode=no_afe_pure_data
```

- Pitfalls / attention points：帧计数增长不代表麦克风硬件路径正确；要用 peak 和示波器/逻辑分析仪确认。
- Validation：静音下 peak 接近底噪，说话/敲击时 capture/preproc peak 明显上升；单麦近场测试确认 ch0/ch1 物理位置。

### Speaker / AXS2033 / Audio Output

- Purpose：把 RTL8730E 内部 codec line-out 差分模拟输出放大到 CN2 speaker。
- Complete hardware path：
  - HAL-visible output：`AMEBA_AUDIO_DEVICE_SPEAKER`。
  - SDK route：AmebaSmart render speaker device 进入 `APP_LINE_OUT`，line-out 设置为 `DIFF` 并 unmute。
  - SoC pins：`LINEOUTLN` pin51 -> `LINEOUT_LN`，`LINEOUTLP` pin52 -> `LINEOUT_LP`。
  - Input network：`LINEOUT_LN -> C76 0.1uF -> R56 39K -> U7 IN-`；`LINEOUT_LP -> C153 0.1uF -> R57 39K -> U7 IN+`。
  - Amplifier：AXS2033 `OUTP/OUTN` -> `SPKP/SPKN` -> FB1/FB2 -> TVS -> CN2 speaker。
  - Control：`PB25 -> MUTE -> R20 100R -> U7 SHUT/SD`。
  - Power：U7 `VDD` 接 `VCC_5V_IN`；本板标注 `VDD=5V, RL=8R, Output PWR=1.8W`。
  - Loopback/reference candidate：`SPK_OUTP/N -> R22/R26 20K -> RC/分压 -> MIC5_P/N`。这只是硬件回采网络线索，是否用于软件参考通道需另行配置验证。
- AXS2033 electrical notes：
  - VDD 2.5-5.5V。
  - SD：`<0.35V` shutdown，`1.2-1.5V` AB，`1.7-2.1V` D 类防破音关闭，`>2.3V` D 类防破音模式 1。
  - 本板 `Ri=39K`，原理图公式 `Gain=400K/(Ri+5K)`，估算约 9.09x / 19.2dB；`Ci=0.1uF` 输入高通约 40.8Hz。
- Project binding：
  - `include/river/river_audio_hw_overrides.h`：`AUDIO_HW_AMPLIFIER_PIN=_PB_25`。
  - `components/river_diag/river_diag_cmd.c`：`river playback tone [freq_hz] [duration_ms] [level_pct]`。
  - 当前固件默认播放音量已满量程，见 E6。
- Local validation command：

```text
river playback tone 1000 1000 25
```

- Expected logs：

```text
[river][diag] playback tone start: route=speaker/LINEOUT amp=PB25/MUTE ...
playback start: stream=diag_tone ...
playback drain complete: stream=diag_tone ...
playback stop: stream=diag_tone ...
```

- Pitfalls / attention points：小声/无声先测 SD、LINEOUT 和差分输出；CN2 不可单端接地测。
- No-sound debug order：playback status -> U7 VDD -> U7 pin1 SD -> `LINEOUT_LN/LP` -> U7 `IN+/IN-` -> U7 `OUTP/OUTN` -> CN2/喇叭。

### LCD / Backlight / Touch

- Connector：CN6 `A113F-15025WUA-R01`，包括 `VCCIO/VCC3/VCC5/RST/D1P/D1N/CLKP/CLKN/D0P/D0N/VCC_TP/I2C_SCL/I2C_SDA/TP_INT/TP_RST/LEDK/LEDA`。
- DSI path：D1、CLK、D0 均路由到 CN6。
- Power/reset/backlight：`LCD_PWR_ON` controls `VCC_3V3_LCD`；`RST_LCD=PA14`；`LCD_BL_PWM=PA16` controls U10 `STI9287C` boost EN。
- ST7102 init evidence：`SSD_LANE(1,0)`，`0x35` TE，`0x11` 后 250ms，`0x29` 后 200ms，`SSD_MODE(1,1)`。
- Firmware action：LCD driver 不要把 lane 数、lane rate、porch/sync 和 reset pulse 写死；开背光前先确认 DSI/TE。
- Touch path：`PB10/PB11 -> TP_SDA2/SCL2 -> CN6 I2C_SDA/SCL`；`PA9 -> TP_INT`；`PA10 -> TP_RST`；Sitronix 示例 `0x55`、falling IRQ、active-low reset。
- Pitfalls / attention points：白屏要先区分 power/reset/DSI/backlight；touch 地址必须 scan，不要只写死 `0x55`。

### BL702 Zigbee Subsystem

- Component：`U12 BL702C-10-Q2H/QFN32/BL`。
- Power/clock/RF：`VCC_3V3_Z`，32MHz crystal，RF path to Zigbee antenna。
- Control：`RST_BL702 -> PU_CHIP`；`BOOT_BL702 -> GPIO28`，0 从 Flash，1 从 UART。
- UART：RTL8730 `PB20/TX1` -> BL702 `GPIO15/UART_RX1`；RTL8730 `PB19/RX1` <- BL702 `GPIO14/UART_TX1`。
- Firmware action：`Zigbee_PWR_ON` -> wait rail stable -> `BOOT_BL702` low -> release `RST_BL702` -> open UART1。
- Pitfalls / attention points：升级/恢复模式要显式拉高 boot；正常启动不要让 boot strap 漂高。

### Wi-Fi / BT RF

- RTL8730E internal Wi-Fi/BT RF nets are on P09 with filter `U8 FLT18D24255171D-3271A`，T2 Wi-Fi IPEX，ANT4 onboard BT antenna。
- Firmware boundary：RF matching/antenna selection is hardware domain。
- Pitfalls / attention points：country、tx power、empty efuse fallback 和 RF 合规相关；软件不要在没有硬件/RF review 的情况下随意扩大功率或绕过约束。

### TH Sensor FPC

- Connector：CN5 `A113F-15006WUA-R01/0.5MM/6PIN`。
- Nets：`VCC_3V3`、`TH_I2C_SDA`、`TH_I2C_SCL`、GND/TVS。
- Firmware action：先做安全 I2C scan，不绑定具体驱动。
- Pitfalls / attention points：不要从 `TH` 网名推断传感器型号或寄存器表。

### IR

- CN7 includes `IR_LED_N` powered from `VCC_5V_IN` through R30 2.7R and driven by Q3 NMOS；RTL8730E controls Q3 gate via `IR_TX`/R31。
- Firmware action：确认 PA3 PWM/IR mux；载波测试用示波器看 `IR_LED_N` 和 Q3，而不是只看 GPIO log。
- Pitfalls / attention points：IR LED 电流限制和热风险缺资料，测试 duty 先保守。

### Debug / Base Interface

- CN1 exposes `VCC_5V_IN`、USB DP/DN、`RTL8730_TX0/RX0` and GND。
- `CHIP_EN` reset button SW2 pulls enable low；这是 reset，不等同断电。
- 当前硬件下载需要手动进入 download mode 后再运行 flash wrapper。

## Power, Reset, And Boot

| Topic | Schematic Observation | Datasheet / SDK Requirement | Firmware Impact | Confidence |
| --- | --- | --- | --- | --- |
| RTL8730E main power | External 3.3V plus internal DCDC rails on P06 | Realtek guide describes internal DCDC rail roles | Board code mainly controls sub rails | B |
| LCD power | `LCD_PWR_ON` enables U4 3.3V LCD rail | Panel requires power before reset/init | Driver owns power/reset/backlight sequence | A |
| Zigbee power | `Zigbee_PWR_ON` switches `VCC_3V3_Z` | BL702 boot depends on rail/reset/strap | Zigbee driver must serialize power, boot, reset | A |
| NAND boot/download | SPI NAND on PC1-PC6 | Realtek tools separate NAND/NOR profile | Use project NAND profile | A |
| BL702 boot strap | GPIO28 low Flash, high UART | Bootstrap sampled around reset | Keep `BOOT_BL702` low except upgrade | A |
| Amp mode | `PB25/MUTE` drives AXS2033 SD | SD voltage selects mode | Measure SD and keep amp control in HAL/BSP | B |

## Clocks And Timing

| Clock / Signal | Source | Destination | Frequency / Mode | Firmware Configuration | Evidence |
| --- | --- | --- | --- | --- | --- |
| RTL8730E crystal | X1 | RTL8730E XI/XO | 40MHz | board fixed | P07 |
| BL702 crystal | X2 | BL702 XTAL_HF_IN/OUT | 32MHz | BL702 fixed | P12 |
| PDM clock | RTL8730E DMIC | MSM261DDB021 CLK | SDK-selected, verify against 1.1-4.8MHz standard range | `AUDIO_HW_DMIC_CLK_PIN=_PA_2` | E2/E7 |
| DSI clock | RTL8730E DSI | ST7102 panel | lane rate missing | board config must expose timing | E13/E14 |
| I2C touch/TH | RTL8730E PB10/PB11 | Sitronix/TH FPC | 100/400kHz initial probe | scan first, then driver | E15/E18 |
| Backlight PWM | RTL8730E PA16 | U10 EN | duty/frequency missing | conservative PWM, default off | P10 |

## Communication Interfaces

| Interface | Instance | Pins/Nets | Connected Device | Address / CS / IRQ | Driver Notes | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| QSPI NAND | Flash controller | PC1-PC6 / `FLASH_QSPI_*` | GD5F1GM7UEYIGR | `/CS` on PC6 | NAND profile; 1Gb/128MB | E11 |
| PDM/DMIC | Audio HAL DMIC DATA1 | PA2/PA4 | MSM261DDB021 pair | DMIC3/DMIC4 | Override SDK defaults | E1-E7 |
| Audio line-out | Audio HAL speaker route | LINEOUTLN/LP | AXS2033 | PB25 controls SD | speaker route -> line-out differential | E8-E10 |
| MIPI DSI | DSI host | D0/D1/CLK | ST7102 LCD | reset PA14 | lane/timing unresolved | E13/E14 |
| I2C touch/TH | likely I2C on PB10/PB11 | TP/TH SDA/SCL | Sitronix + TH module | touch candidate 0x55, IRQ PA9 | scan before bind | E15/E18 |
| UART1 | UART1 | PB20/PB19 | BL702 GPIO15/14 | no flow control | protocol/baudrate missing | E16 |
| UART0/USB | debug/base | PB24/PB23, USB DP/DN | CN1/base board | N/A | console/download/debug owner conflict to manage | P05/P07 |
| IR | GPIO/PWM/IR | PA3 -> Q3 | IR LED on CN7 | N/A | confirm mux/current | P08 |

## Firmware Bring-Up Checklist

1. Build-time board constants：
   - `AUDIO_HW_DMIC_CLK_PIN=_PA_2`
   - `AUDIO_HW_DMIC_DATA1_PIN=_PA_4`
   - `AUDIO_HW_AMPLIFIER_PIN=_PB_25`
   - NAND profile default enabled in `tools/river_flash.py`

2. Audio capture：
   - Confirm logs show `pdm-2mic-pa2-pa4-data1` and `DMIC3/DMIC4`。
   - Confirm `[AudioHal-I] ... set DMIC clock`。
   - Speak/knock and verify capture/preproc peak rises。
   - If peak is zero, probe PDM_CLK/PDM_DAT1 before changing software path。

3. Audio output：
   - Run `river playback tone 1000 1000 25`。
   - Confirm playback logs start/write/drain/stop。
   - Measure U7 VDD, U7 pin1 SD, LINEOUT_LN/LP, U7 OUTP/OUTN, CN2。
   - If PB25 toggles but U7 SD voltage is wrong, fix SD network assumptions before changing playback service。

4. LCD：
   - Power `VCC_3V3_LCD`。
   - Apply reset pulse。
   - Send ST7102 init table with lane count configurable。
   - Verify DSI LP/HS and TE before enabling backlight。

5. Touch：
   - After LCD/touch power and reset, scan PB10/PB11 I2C。
   - Try `0x55` first, read chip id/SFR。
   - Configure PA9 falling IRQ only after polling read works。

6. BL702：
   - Set `BOOT_BL702` low。
   - Enable `VCC_3V3_Z`。
   - Release reset。
   - Open UART1 with known baudrate after owner supplies protocol。

7. TH：
   - Scan shared I2C bus。
   - Do not add a driver until module model/address is confirmed。

8. Flash：
   - Manually enter download mode。
   - Use `python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000`。
   - Verify NAND detection and `Finished PASS`。

## Failure Signatures

| Symptom | Likely Layer | First Checks |
| --- | --- | --- |
| Capture frame count grows but peak stays zero | PDM pinmux/data path/electrical | Logs `DMIC3/4`, PDM_CLK waveform, PDM_DAT1 activity, CN7 cable |
| `AudioRecord_SetParameters` reports record not created | AudioRecord sequencing | Ensure Init -> Start -> SetParameters order |
| Playback logs normal but no sound | Amp/line-out/speaker | PB25/U7 SD, LINEOUT_LN/LP, U7 VDD, CN2 differential output |
| Tone audible but very quiet/distorted | Gain/volume/SD mode/speaker load | R56/R57, U7 SD mode voltage, CN2 load impedance, HAL volume |
| LCD white/black screen | DSI timing/power/reset/backlight | `VCC_3V3_LCD`, reset, DSI lanes, backlight PWM |
| Touch scan no ACK | Power/reset/address/bus | `VCC_TP`, PA10 reset, PB10/PB11 pull-ups, scan 0x55 |
| BL702 no boot log | Power/boot/reset/UART baud | `VCC_3V3_Z`, BOOT low, reset pulse, UART1 RX/TX direction |
| Flash tool profile mismatch | Download profile | Use NAND, not NOR |

## Risks And Unknowns

| Risk | Impact | Confidence | How To Resolve |
| --- | --- | --- | --- |
| LCD lane/timing mismatch | White screen, unstable panel, false backlight diagnosis | B | Obtain panel timing sheet; keep lane config mutable. |
| Touch firmware/CFG mismatch | Probe succeeds but no coordinates or bad touch | B | Get exact dump/CFG from supplier; compare chip id and firmware version. |
| AXS2033 SD voltage unmeasured | Amp may stay shutdown or wrong class/mode | B | Measure U7 pin1 across states. |
| PDM L/R slot/order unknown | Beamforming and wake quality may degrade | B | Per-mic acoustic test; store channel mapping in board config. |
| TH sensor absent from docs | Cannot implement reliable driver | A | BOM/silk/module doc or scan + chip marking. |
| BL702 protocol missing | Zigbee feature cannot be safely integrated | A | Firmware owner must provide UART protocol and upgrade flow. |
| RF regulatory/power settings | Compliance and range risk | B | Hardware/RF review before changing tx power/country behavior. |

## Source Notes

- Local hardware files:
  - `doc/hard/RTL8730 4寸SCH.pdf`
  - `doc/hard/RTL8730_4寸_丝印图.pdf`
  - `doc/hard/1.01.070080 MSM261DDB021_Rev1.0.pdf`
  - `doc/hard/AXS2033.pdf`
  - `doc/hard/LCD/ST7102_480480_60hz_0109_tp 3ms .txt`
  - `doc/hard/LCD/Sitronix Touch Driver 移植手册.pdf`
  - `doc/hard/LCD/ST_TDDI_TPDriver_v45.00.260402/`
- SDK sources:
  - `/root/ameba-rtos/component/audio/audio_hal/amebasmart/ameba_audio_stream_capture.c`
  - `/root/ameba-rtos/component/audio/audio_hal/amebasmart/ameba_audio_stream_render.c`
  - `/root/ameba-rtos/component/audio/audio_hal/amebasmart/ameba_audio_stream_control.c`
  - `/root/ameba-rtos/component/soc/usrcfg/amebasmart/include/ameba_audio_hw_usrcfg.h`
  - `/root/ameba-rtos/component/soc/amebasmart/fwlib/include/ameba_audio.h`
- Project sources:
  - `CMakeLists.txt`
  - `include/river/river_audio_hw_overrides.h`
  - `components/river_voice/river_voice_board.c`
  - `components/river_voice/river_voice_capture.c`
  - `components/river_voice/river_orvibo_audio_service.c`
  - `components/river_diag/river_diag_cmd.c`
  - `tools/river_flash.py`
- External references:
  - Realtek RTL8730E Hardware Design Guide R2.3: `https://aiot.realmcu.com/en/_static/hardware/amebasmart/RTL8730E_Hardware_Design_Guide_R2.3.pdf`
  - Realtek Ameba IoT Image Tool docs: `https://ameba-aiot.github.io/ameba-iot-docs/freertos/en/latest/rst_rtos/0_tools/1_image_tool_toprst.html`
  - GigaDevice GD5F1GM7UEYIG product page: `https://www.gigadevice.com/product/flash/spi-nand-flash/gd5f1gm7ueyig`
  - Bouffalo Lab BL702/BL706 product page: `https://en.bouffalolab.com/product/?id=4&type=detail`
  - ChipSourceTek AXS2033 product page: `https://en.chipsourcetek.com/Audio-Chip/204.html`
  - MEMSensing silicon microphone product page: `https://en.memsensing.com/product/176.html`

## Scope Audit

- Audio output is described from HAL-visible speaker route to LINEOUT, AXS2033 and CN2 speaker only.
- Audio capture is described from PDM pins to Audio HAL DMIC categories only.
- Display/touch is described through DSI/I2C/reset/backlight pins, init evidence and board validation.
- Pitfalls and getting-started advice are tied to schematic, datasheet, SDK, project source, runtime records, or explicit hypotheses.
- No product interaction, application session behavior, network service policy or UI flow is used as a hardware conclusion.
