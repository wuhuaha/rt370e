# RTL8730E 4 寸板硬件固件说明书

日期：2026-05-29
对象：Orvibo RTL8730E 4 寸板，原理图 `RTOS_4InchLCD` / `MixPad4_RTL8730`，Rev `V0.1` / `V1A 20260506`
目的：把 `doc/hard/` 的原理图、丝印图和本地 datasheet 转成固件同事可执行的 pinmux、驱动配置、启动日志和验证清单。
边界：本文服务于硬件资料到 BSP/HAL/驱动开发的交接，重点是 SoC 引脚、外设实例、板级电路、SDK/HAL 配置、测点和风险；不定义云端、业务协议、产品交互或应用层播放/会话方案。运行日志只作为硬件路径验证证据使用。

## 结论摘要

- 当前板子的数字麦克风不是 EVB AMIC 路径，而是 PDM/DMIC：`PDM_CLK -> RTL8730E PA2`，`PDM_DAT1 -> RTL8730E PA4`。在 AmebaSmart SDK 里，DATA1 对应 `AUDIO_DMIC3/DMIC4`，项目已用 AP Audio HAL override 固定 `AUDIO_HW_DMIC_CLK_PIN=_PA_2`、`AUDIO_HW_DMIC_DATA1_PIN=_PA_4`，并已实板验证有稳定语音输入。
- 启动介质是 GigaDevice `GD5F1GM7UEYIGR` SPI NAND，原理图标注 128MB，实板 flash 工具也识别为 `GD5F1GM7U 1Gb/128MB`。烧录默认必须走 NAND profile，不要再用 NOR profile。
- BL702 Zigbee 子系统由 RTL8730E 控制电源、复位、boot strap 和 UART1。原理图写明 `GPIO28: 0 boot from Flash, 1 boot from Uart`，固件必须避免误拉高 `BOOT_BL702` 进入 UART boot。
- LCD/触摸资料已补充：本地新增 ST7102 480x480 init table 和 Sitronix touch driver 移植手册。原理图路由了 DSI `D0/D1/CLK` 两条 data lane，但 init table 写 `SSD_LANE(1,0)`；固件实现前需要向屏厂确认该屏实际使用 1-lane 还是 2-lane，以及完整 DSI timing。
- 温湿度传感器通过 CN5 FPC 暴露 `TH_I2C_SDA/SCL` 和 3.3V，但原理图未给外接传感器型号、地址和中断脚。固件需要硬件同事补充模块资料或上电 I2C scan。
- 音频输出硬件路径是 RTL8730E 内部 Audio HAL speaker/LINEOUT route -> `LINEOUT_LN/LP` 差分线 -> `C76/C153 + R56/R57` 输入网络 -> AXS2033 -> CN2 喇叭。AXS2033 `SHUT/SD` 由 `MUTE` 网控制，原理图连接到 RTL8730E `PB25`；SDK 默认功放脚是 `_PB_19`，项目当前 audio override 只改了 PDM PA2/PA4，因此 BSP/HAL 配置需要特别核对 `AUDIO_HW_AMPLIFIER_PIN` 或等效 PB25 控制路径。

## 报告边界和读者

- 读者：负责 BSP、HAL、外设驱动、pinmux、电源/复位时序、板级诊断和量产验证的软件同事。
- 本文可以给出：硬件路径、SoC 引脚、外设实例、SDK/HAL 可见设备、driver config、boot/download 约束、预期底层日志、测点和验证步骤。
- 本文不定义：云端协议、产品交互、应用会话、助手行为、业务播放策略或业务状态机。若运行日志里出现这些词，只作为证据背景；结论必须回到硬件或 HAL/driver 层。
- 音频输出的正确写法是 `Audio HAL speaker route -> LINEOUT -> AXS2033 -> CN2 speaker`；不要把它扩展成某个业务音源到喇叭的产品流程。

## Evidence Index

| ID | Claim | Source | Type | Confidence | Notes |
| --- | --- | --- | --- | --- | --- |
| E1 | `PDM_CLK` 接 RTL8730E `PA2`，`PDM_DAT1` 接 RTL8730E `PA4` | `doc/hard/RTL8730 4寸SCH.pdf` P09/P11；用户确认 | schematic + user fact | A | P09 显示 `PA4 -> PDM_DAT1`，P11 显示 `PA2 -> PDM_CLK`。 |
| E2 | AmebaSmart SDK 中 `DMIC3/DMIC4` 使用 `AUDIO_HW_DMIC_DATA1_PIN`，并共用 `AUDIO_HW_DMIC_CLK_PIN` | `/root/ameba-rtos/component/audio/audio_hal/amebasmart/ameba_audio_stream_capture.c:352` | SDK | A | DATA0/1/2/3 分别映射 DMIC1/2、3/4、5/6、7/8。 |
| E3 | SDK 默认 DMIC pin 是 PB 组，不匹配当前板子 PA2/PA4 | `/root/ameba-rtos/component/soc/usrcfg/amebasmart/include/ameba_audio_hw_usrcfg.h:84` | SDK | A | 默认 `_PB_22/_PB_18` 等需要项目覆盖。 |
| E4 | 项目当前固件固定 PA2/PA4 DATA1 方案 | `include/river/river_audio_hw_overrides.h:12`；`components/river_voice/river_voice_board.c:13` | code | A | profile 为 `pdm-2mic-pa2-pa4-data1`，primary/secondary 为 `AUDIO_DMIC3/4`。 |
| E5 | PA2/PA4 DATA1 方案已实板验证为有效采集路径 | `.codex/changes.md` Step H.xiaozhi-client.69 | runtime | A | 非零 capture/preproc peak、VAD/KWS 相关运行证据已出现；业务层结果不作为本文结论来源。 |
| E6 | MSM261DDB021 是 PDM 数字麦，VDD 1.6-3.6V，标准性能 clock 1.1-4.8MHz | `doc/hard/1.01.070080 MSM261DDB021_Rev1.0.pdf` p4/p9/p10/p14 | datasheet | A | pin1 DATA、pin4 CLK；L/R 决定边沿/槽位。 |
| E7 | AXS2033 是 3.1W 单声道 AB/D 类功放，VDD 2.5-5.5V，SD 电压区间控制 shutdown/AB/D 类模式 | `doc/hard/AXS2033.pdf` p2/p4/p8；ChipSourceTek product page | datasheet + vendor page | A/B | 本地 PDF 为主要证据，网页用于型号交叉确认。 |
| E8 | NAND 型号为 `GD5F1GM7UEYIGR`，原理图标注 128MB | `doc/hard/RTL8730 4寸SCH.pdf` P11；GigaDevice product page | schematic + vendor page | A/B | 实板 flash 工具识别 `GD5F1GM7U 1Gb/128MB`。 |
| E9 | BL702 支持 BLE/Zigbee，板上 UART1 连接 RTL8730E | `doc/hard/RTL8730 4寸SCH.pdf` P12；Bouffalo Lab product page | schematic + vendor page | A/B | 官方网页说明 BL702 支持 BLE/Zigbee 和 RISC-V。 |
| E10 | GPIO28 boot strap：0 从 Flash 启动，1 从 UART 启动 | `doc/hard/RTL8730 4寸SCH.pdf` P12 | schematic | A | 原理图直接标注。 |
| E11 | LCD 接口为 MIPI DSI + touch I2C + backlight PWM | `doc/hard/RTL8730 4寸SCH.pdf` P10 | schematic | A | 后续由 E16-E18 补充 ST7102/Sitronix 资料，但 lane/timing/address 仍需确认。 |
| E12 | 丝印图标注 `MixPad4_RTL8730 V1A 20260506`，CN7 `MIC/IR`，CN5 `TH`，T2 Wi-Fi，T3 Zigbee | `doc/hard/RTL8730_4寸_丝印图.pdf` p2 渲染/OCR | visual/OCR | B | 丝印无文本层，低于原理图可信度。 |
| E13 | AXS2033 输入网络使用 `R56/R57=39K` 和 `C76/C153=0.1uF`；按 datasheet 公式估算电压增益约 9.09 倍 / 19.2dB，输入高通截止约 40.8Hz | `doc/hard/RTL8730 4寸SCH.pdf` P08；`doc/hard/AXS2033.pdf` p8 | schematic + datasheet | B | datasheet 示例为 22K -> 23.4dB；本板 39K 是按公式推算，需要实际响度验证。 |
| E14 | 原理图 `MUTE` 连接 RTL8730E `PB25`，SDK 默认 `AUDIO_HW_AMPLIFIER_PIN` 是 `_PB_19` | `doc/hard/RTL8730 4寸SCH.pdf` P08/P11；`/root/ameba-rtos/component/soc/usrcfg/amebasmart/include/ameba_audio_hw_usrcfg.h:19` | schematic + SDK | A | 项目当前 `river_audio_hw_overrides.h` 未覆盖功放脚。 |
| E15 | AmebaSmart Audio HAL 的 speaker 输出走内部 codec line-out 差分模式，创建 stream out 时默认 `AMEBA_AUDIO_DEVICE_SPEAKER` | `/root/ameba-rtos/component/audio/audio_hal/amebasmart/primary_audio_hw_stream_out.c:508`；`ameba_audio_stream_render.c:508` / `:270` | SDK | A | `AMEBA_AUDIO_DEVICE_SPEAKER` -> `APP_LINE_OUT`，对 DAC L/R line-out 设 `DIFF` 并 unmute。 |
| E16 | ST7102 init table 标注 480x480/60Hz，启用 TE，`SSD_MODE(1,1)`，`0x11` 后延时 250ms，`0x29` 后延时 200ms | `doc/hard/LCD/ST7102_480480_60hz_0109_tp 3ms .txt` | vendor init table | B | 文本未给完整 porch/lane rate/reset timing；文件名和命令表支持 panel bring-up 初始依据。 |
| E17 | ST7102 init table 写 `SSD_LANE(1,0)`，但原理图 CN6 路由 DSI D0/D1/CLK | `doc/hard/LCD/ST7102_480480_60hz_0109_tp 3ms .txt`；`doc/hard/RTL8730 4寸SCH.pdf` P10 | init table + schematic | B | 这是需要硬件/屏厂澄清的 lane 配置冲突，不应由软件静默选择。 |
| E18 | Sitronix touch driver 手册适用 ST7123/ST7121P/ST7123P/ST7102，支持 I2C/SPI；I2C 建议 400kHz，SPI 建议 8MHz，IRQ falling/active-low，RST active-low | `doc/hard/LCD/Sitronix Touch Driver 移植手册.pdf` p1/p4/p5/p10/p18 | vendor guide | B | 手册偏 Linux 移植说明；GPIO 编号、地址和 firmware 文件仍需按本板实测。 |
| E19 | Sitronix 源码包 `sample.dtsi` 给出 I2C `compatible="sitronix_ts"`、`reg=<0x55>`；SPI 示例 `spi-max-frequency=<0x7a1200>` 即 8MHz、mode 3 | `doc/hard/LCD/ST_TDDI_TPDriver_v45.00.260402/.../dts/sample.dtsi` | vendor source bundle | B | `0x55` 可作为优先 probe 候选，但不能替代上电 scan。 |
| E20 | Sitronix porting guide/source 要求选择平台、I2C/SPI、flash boot/host download；正常 probe 会打印 IC SFR/Display ID/Chip ID，驱动源码使用 falling IRQ 并含 ST7102 self-test header | `TouchDriver_PortingGuide.txt`；`touchscreen/sitronix_ts/sitronix_ts.c`；`sitronix_ts_test_st7102.h` | vendor source bundle | B | Linux driver 结构不能直接照搬到 Ameba，但寄存器访问、reset/IRQ、固件下载、自检和预期日志可迁移参考。 |

## Artifact Inventory

| Artifact | Path / URL | Pages / Sheets Used | Extraction Method | Notes |
| --- | --- | --- | --- | --- |
| 主原理图 | `doc/hard/RTL8730 4寸SCH.pdf` | 13 pages；重点 P05-P13 | PyMuPDF text + page render | 有文本层，关键 net 可直接提取并视觉复核。 |
| 丝印图 | `doc/hard/RTL8730_4寸_丝印图.pdf` | 2 pages | PyMuPDF render + Tesseract OCR | 无文本层；仅用于定位连接器/测试点。 |
| PDM 麦 datasheet | `doc/hard/1.01.070080 MSM261DDB021_Rev1.0.pdf` | 16 pages；重点 p4/p9/p10/p14 | PyMuPDF text | 本地供应商规格书，Rev 1.0。 |
| 功放 datasheet | `doc/hard/AXS2033.pdf` | 9 pages；重点 p2/p8 | PyMuPDF text | 本地规格书，部分中文 OCR/字体有轻微乱码。 |
| ST7102 LCD init table | `doc/hard/LCD/ST7102_480480_60hz_0109_tp 3ms .txt` | 文本命令表 | direct text | 本地新增屏厂/供应商初始化表，含 lane/mode、DCS command、sleep out/display on 延时。 |
| Sitronix touch driver 手册 | `doc/hard/LCD/Sitronix Touch Driver 移植手册.pdf` | 21 pages；重点 p1/p4-p5/p10-p11/p18 | PyMuPDF text + selected page render | 本地新增触摸移植手册，适用 ST71xx/ST7102；示例偏 Linux device tree，需要迁移到 Ameba BSP/driver。 |
| Sitronix touch driver 源码包 | `doc/hard/LCD/ST_TDDI_TPDriver_v45.00.260402/` | `sample.dtsi`、porting guide、`sitronix_ts*.c/h`、ST7102 self-test header | direct source/text review | 本地新增 vendor Linux driver 参考包，含 I2C/SPI DTS 示例、probe 日志、自检和 firmware loading 路径。 |
| RTL8730E SDK | `/root/ameba-rtos` | audio HAL、usrcfg、fwlib headers | `rg` + source review | 用于确认 DMIC category 和 clock/pinmux。 |
| 外部网页 | Realtek Ameba IoT docs, GigaDevice, Bouffalo Lab, ChipSourceTek | 相关产品页 | browser/curl/Jina mirror | 用于型号/能力交叉确认；关键配置仍以本地原理图/SDK为准。 |

## Engineer-Supplied References

| Material | Path / URL | Provider / Context | Used For | Version / Revision Check |
| --- | --- | --- | --- | --- |
| 当前硬件资料目录 | `doc/hard/` | 用户指定 | 全板分析输入 | 主原理图日期 2026-05-06，Rev V0.1。 |
| LCD/触摸补充资料 | `doc/hard/LCD/` | 用户补充 | ST7102 panel init 与 Sitronix touch driver 移植 | init table、`Sitronix Touch Driver 移植手册.pdf` 和 `ST_TDDI_TPDriver_v45.00.260402` 已纳入证据表。 |
| 麦克风连接事实 | 用户消息：`PDM_CLK 接 PA2, PDM_DAT1 接 PA4` | 用户/硬件上下文 | 数字麦路线确认 | 与 P09/P11 原理图一致。 |
| 运行验证记录 | `.codex/changes.md` Step H.69 | 当前项目记录 | PA2/PA4 DATA1 实板闭环 | 与当前代码 H.70 保留方案一致。 |

## Missing Inputs And Clarifications

| Missing Item / Question | Why It Matters To Firmware | Current Best Guess | Confidence | Recommended Owner / Check |
| --- | --- | --- | --- | --- |
| LCD DSI lane 数、lane rate、porch/sync timing、reset pulse 时序 | 决定 MIPI DSI host 配置，错误会白屏、花屏或无 TE | 已有 ST7102 480x480 init table，但 `SSD_LANE(1,0)` 与原理图 2 条 data lane 路由不完全一致 | B | 屏厂/硬件确认实际 lane；固件用示波器/DSI host 日志验证 D0/D1 是否都出数。 |
| LCD panel 完整 datasheet 或 timing sheet | init table 不等于完整 panel driver 资料，仍缺像素时钟、H/V porch、reset/backlight 时序 | ST7102/ST71xx TDDI panel，经 CN6 `A113F-15025WUA-R01` 接入，分辨率 480x480 | B | 硬件/屏供应商提供 datasheet；如没有，需从可用 demo 工程导出 DSI timing。 |
| Touch controller 实际 I2C 地址、INT/RST 极性、firmware 文件匹配关系 | 决定 touch probe、固件加载和中断处理 | Sitronix 源包 `sample.dtsi` 给出 `reg=<0x55>`，可作为优先 probe 候选；但实际模块地址、flash boot/host download 和 firmware dump 仍需确认 | B | 上电 scan + 复位后读 chip id；硬件确认触摸 IC 地址和是否有外部/内置 flash。 |
| TH FPC 外接温湿度传感器型号和 I2C 地址 | 没有型号无法写 sensor driver | CN5 是外接 TH 模块，3.3V + I2C | C | 硬件提供模块 BOM；固件 scan `TH_I2C`。 |
| CN7 上 MIC/IR 小板的实际麦克风数量、L/R 接法、供电脚定义 | 影响 PDM 左右槽位、双麦相位、波束形成 | MSM261DDB021 双麦，DATA 复用一根 PDM_DAT1，L/R 一高一低 | B | 硬件提供小板原理图；逻辑分析 PDM CLK/DATA 和 L/R。 |
| `MUTE` 到 AXS2033 `SHUT/SD` 的实际电压范围和上电默认态 | SD 不是简单 enable，电压区间决定 D 类/AB/关断；错误电平会导致无声、噪声或 POP | `PB25 -> MUTE -> R20/R21/C69/C71 -> SHUT`，GPIO high 可能进入 D 类防破音模式 | B | 示波器测 U7 pin1；确认 boot、idle、Audio HAL stream start、standby/stop 四个状态。 |
| Ameba BSP/HAL 是否已把功放脚从 SDK 默认 `_PB_19` 改为本板 `PB25` | 直接影响 Audio HAL 自动 enable/disable 功放；错脚会出现 LINEOUT 有信号但喇叭无声 | 当前项目 override 只见 DMIC PA2/PA4，未见 `AUDIO_HW_AMPLIFIER_PIN` 覆盖 | A | 软件检查编译宏或 map；若使用 HAL 自动控制，覆盖为 `_PB_25` 或实现等效 PB25 控制。 |
| BL702 固件/协议和 UART 波特率 | 决定 Zigbee 子系统启动、升级和通信协议 | RTL8730 UART1 透传到 BL702 GPIO14/15 | B | Zigbee 固件负责人提供协议；串口探测 boot log。 |
| Realtek RTL8730E 完整 pinmux/reference manual | 可进一步核验所有 PA/PB/PC 复用功能 | 本地 SDK 已足够确认当前 DMIC 和 audio HAL | B | 如要扩展 LCD/IR/Zigbee，补官方 pinmux 表。 |

## Hypotheses / Candidate Solutions

| Hypothesis | Evidence For | Evidence Against / Risk | Confidence | Suggested Firmware Path | Validation Step |
| --- | --- | --- | --- | --- | --- |
| PDM 双麦在同一 DATA1 上用 L/R 边沿复用 | MSM261DDB021 支持 L/R 边沿选择；CN7 只有 `PDM_DAT1` 一根数据线；实板 DMIC3/4 有效 | 小板 L/R 接法未给出，左右声道可能反或同槽 | B | 保持 `DMIC3/DMIC4`，后续用相位/声源方向测试修正通道顺序 | 近场分别遮挡/敲击两颗麦，记录 ch0/ch1 peak。 |
| `MUTE=high` 可能不是单纯 unmute，而是选择 AXS2033 D 类防破音模式 | AXS2033 SD >2.3V 进入 D 类防破音模式；原理图 `MUTE` 进 SHUT；SDK dummy amp GPIO enable 会输出 high | 分压/RC 可能导致中间电压，错误模式会无声或 POP；具体电平未测 | B | 在 BSP/HAL 层把 PB25 作为功放 SD 控制脚，并把“enable high -> D 类防破音模式”作为待测假设 | 测 U7 pin1 在 boot、idle、Audio HAL stream start、standby/stop 的电压。 |
| `TH_I2C` 和 `TP_SDA2/TP_SCL2` 可能共用 RTL8730E 的 PB10/PB11 I2C | P10 中 `TP_SDA2/SCL2` 经 R36/R37 接 `TH_I2C_SDA/SCL`，P13 直接用 TH nets | 若触摸和 TH 同总线，地址冲突/复位时序会影响 probe | B | 做单总线 I2C scan，驱动按地址注册，不硬编码单设备 | 分别接/断 TH FPC 和 LCD，比较 scan 结果。 |
| BL702 默认应从 Flash 启动，只有下载/恢复时拉高 `BOOT_BL702` | P12 明确 `GPIO28 0 boot From Flash 1 boot From Uart` | 如果 BOOT 控制极性被上层误设，高电平会卡在 UART boot | A | 默认把 BOOT GPIO 配为输出低或输入下拉，仅升级流程临时拉高 | 复位 BL702 后观察 debug UART 是否正常启动。 |
| LCD 可以先按 ST7102 init table 进行最小 bring-up，但 lane 数必须可配置 | init table 有完整 DCS 命令、TE、sleep out/display on 延时；原理图有 DSI D0/D1/CLK | lane 冲突未确认；缺完整 porch/lane rate 时白屏风险高 | B | BSP panel driver 把 lane_count/timing/init table 拆成 board config，不把 `SSD_LANE(1,0)` 写死到不可改路径 | 上电先不开背光，确认 DSI LP/HS 和 panel ID/TE 后再开 PWM。 |
| Touch 可以优先按 Sitronix I2C `0x55` 尝试 probe | 源码包 `sample.dtsi` 给出 `reg=<0x55>`，原理图触摸走 I2C 而不是 SPI | 模组可改地址，且同一 PB10/PB11 总线上还有 TH FPC；硬编码会掩盖地址/复位/上电问题 | B | I2C scan 记录全地址，再以 `0x55` 读 chip id / SFR version；driver config 保留可改地址 | PA10 reset 后扫描 PB10/PB11 bus，确认非 `0/0xFF` 的 SFR/chip id。 |

## MCU / SoC Pin Map

| Function | Net | MCU Pin | Alt Function / Instance | Voltage Domain | Pull / Default | Evidence | Firmware Action |
| --- | --- | --- | --- | --- | --- | --- | --- |
| PDM clock | `PDM_CLK` | RTL8730E `PA2` pin 5 | DMIC CLK | 3.3V digital / audio | 未见外部上下拉 | E1/E2/E4 | `AUDIO_HW_DMIC_CLK_PIN=_PA_2`；启动日志应有 `clk=PA2 data1=PA4`。 |
| PDM data | `PDM_DAT1` | RTL8730E `PA4` pin 7 | DMIC DATA1 | 3.3V digital / mic IO | CN7 串 R29 100R | E1/E2/E4/E5 | 用 `AUDIO_DMIC3/DMIC4`，不要回退 DMIC1/2 或 DMIC5/6。 |
| IR TX | `IR_TX` | RTL8730E `PA3` pin 6 | GPIO/PWM candidate | 3.3V control, IR LED on 5V rail | Q3 NMOS low-side | P08/P11 | 需要 38kHz carrier 时优先 PWM；确认 duty/current。 |
| BL702 reset | `RST_BL702` | RTL8730E `PA13` pin 15 | GPIO | 3.3V | BL702 侧 R52 100R，PU_CHIP 侧 R53 10K | P11/P12 | 默认释放复位；升级/恢复时可拉低复位。 |
| BL702 boot | `BOOT_BL702` | RTL8730E `PA15` pin 17 | GPIO | 3.3V | BL702 GPIO28 有 R47 20K 下拉、R46 100R 串联、R48 1K 到 TP31 | P11/P12 | 默认保持低；UART boot 只在下载流程临时拉高。 |
| Zigbee power enable | `Zigbee_PWR_ON` | RTL8730E `PA5` pin 8 | GPIO | 3.3V control | 电源页 U3 EN | P05/P09 | 网络/功耗策略中显式管理 BL702 电源。 |
| UART1 to BL702 | `RTL8730_TX1/RX1` | `PB20/PB19` pins 67/66 | UART1 | 3.3V | RX/TX 各有 10K 上拉到 3.3V_Z | P11/P12 | RTL8730 TX1 -> BL702 RX1，RTL8730 RX1 <- BL702 TX1。 |
| UART0 / base interface | `RTL8730_TX0/RX0` | `PB24/PB23` pins 73/72 | UART0 | 3.3V | R54/R55 10K 上拉到 VCC_3V3_Z | P05/P11 | 保留为底板/调试通信，避免与 console 冲突。 |
| Amp SD/mode | `MUTE` | `PB25` pin 96 | GPIO / Audio HAL amplifier pin | 3.3V control to U7 SD | R20 100R + R21 10K/C69/C71 | E7/E14 | BSP/HAL 应核对 `AUDIO_HW_AMPLIFIER_PIN=_PB_25` 或等效 PB25 控制；按“AXS2033 SD/mode”而不是普通 mute 命名。 |
| LCD power enable | `LCD_PWR_ON` | `PB26` pin 97 | GPIO | 3.3V control | 电源页 U4 EN | P05/P11 | DSI/touch 初始化前先上 LCD 3.3V；失败时先测 CN6 VCCIO/VCC3/VCC5/VCC_TP。 |
| LCD reset | `RST_LCD` / `Reset_LCD` | `PA14` pin 16 | GPIO | 3.3V_LCD | R34 100K pull-up，R35 1K 串 | P10/E16 | 按 panel 时序拉低/释放；当前缺精确 reset pulse，需从屏厂资料或实测补齐。 |
| Backlight PWM | `LCD_BL_PWM` | `PA16` pin 18 | PWM/GPIO | 3.3V control, 5V boost | R42 100R，R43 10K pulldown | P10 | 初始化完 panel 后打开 PWM；默认低关背光，避免白屏/花屏误判为背光问题。 |
| Touch interrupt/reset | `TP_INT`, `TP_RST` | `PA9`, `PA10` | GPIO IRQ / GPIO | 3.3V_LCD | 4.7K pull-up group | P10/E18 | Sitronix 示例为 falling/active-low IRQ、active-low reset；实际按板级 GPIO 编号和测量确认。 |
| Touch/TH I2C | `TP_SDA2/SCL2`, `TH_I2C_SDA/SCL` | `PB10/PB11` | I2C | 3.3V_LCD / 3.3V | 4.7K pull-ups + R36/R37 100R 串 | P10/P13/E18 | I2C 先按 400kHz 以内 scan；注意 touch 与 TH 可能同总线，Sitronix 地址需 probe。 |
| SPI NAND | `FLASH_QSPI_*` | `PC1-PC6` | QSPI NAND | 3.3V | U11 C102 1uF decap | P11 | 烧录 profile 和 boot profile 走 NAND。 |

## Peripheral Blocks

### Power / Power Key

- Purpose：5V 输入生成 HOLD 3.3V、LCD 3.3V、Zigbee 3.3V，并提供长按关机逻辑。
- Components：`U1/U4 TMI6050-33`，`U3 TMI3411/2A`，`U2 EY404-CF42F1`，`Q1 IRLML6401`，`Q2 S8050`。
- Nets：`VCC_5V_IN`、`VCC_3V3_HOLD`、`VCC_3V3_LCD`、`VCC_3V3_Z`、`LCD_PWR_ON`、`Zigbee_PWR_ON`、`PWR_OFF`。
- Firmware impact：开机后应明确设置 LCD/Zigbee enable 的默认态；长按 7s 关机由硬件 power key IC 参与，固件不要假设 reset 等于断电。
- Validation：测上电默认 `VCC_3V3_HOLD` 是否稳定；切换 `LCD_PWR_ON` / `Zigbee_PWR_ON` 时看对应 rail。

### RTL8730E Core / Reset / USB / Clock

- Main clock：`X1 40M` 接 RTL8730E `XI/XO`。
- Reset/enable：P07 有 `SW2`、`CHIP_EN`，USB `HSDP/HSDM` 接 `USB_DP/DN`。
- Firmware impact：USB、UART 和 NAND 下载路径需要和当前启动介质区分；当前板端下载策略已走 NAND profile。

### SPI NAND Flash

- Component：`U11 GD5F1GM7UEYIGR/Nand Flash128MB/WSON8/GD`。
- Nets：`FLASH_QSPI_CSN/CLK/IO0/IO1/IO2/IO3` 到 RTL8730E `PC6/PC2/PC3/PC5/PC4/PC1`。
- Firmware action：保留 `tools/river_flash.py` 默认 NAND；用户手动下载命令仍是：

```bash
cd /root/ameba-river
export AMEBA_SDK_ROOT=/root/ameba-rtos
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000
```

- Expected logs：Flash 工具应识别 `MemoryType: NAND`、`GD5F1GM7U`、`1Gb/128MB`，最终 `Finished PASS`。
- Failure signature：`Flash type mismatch: Device: 2 / Device Profile: 1` 表示误用了 NOR profile。

### PDM Microphone / Audio Capture

- Schematic：CN7 `MIC/IR` FPC 暴露 `PDM_CLK`、`PDM_DAT1`、`VCC_3V3`、`IR_LED_N`，P08 还有 `MIC5_P/N` 用于 loopback，不是主 PDM 入口。
- Datasheet：MSM261DDB021 pin1 DATA、pin2 L/R、pin3 GND、pin4 CLK、pin5 VDD；标准性能模式 clock 1.1-4.8MHz，low-power 150-900kHz；L/R 决定在 clock rising/falling edge 上驱动 DATA。
- SDK mapping：AmebaSmart `AUDIO_DMIC3/4` -> `AUDIO_HW_DMIC_DATA1_PIN`，clock 由 `AUDIO_HW_DMIC_CLK_PIN`；SDK DMIC clock可选 5MHz、2.5MHz、1.25MHz、625kHz、312.5kHz、769.2kHz。
- Current firmware：
  - [components/river_voice/river_voice_board.c](/root/ameba-river/components/river_voice/river_voice_board.c:13)：`pdm-2mic-pa2-pa4-data1`，`AUDIO_DMIC3/DMIC4`。
  - [include/river/river_audio_hw_overrides.h](/root/ameba-river/include/river/river_audio_hw_overrides.h:12)：覆盖 PA2/PA4。
  - [components/river_voice/river_voice_capture.c](/root/ameba-river/components/river_voice/river_voice_capture.c:360)：`DEVICE_IN_DMIC_REF_AMIC`。
- Expected boot logs：

```text
board array: Orvibo-RTL8730E-PDM pdm-2mic-pa2-pa4-data1 usage=DMIC primary=DMIC3 secondary=DMIC4
capture board mics applied: usage=DMIC ch0=DMIC3 ch1=DMIC4
capture dmic pinmux applied: clk=PA2 data1=PA4
[AudioHal-I] ... set DMIC clock
capture params applied: ret=0 params=cap_mode=no_afe_pure_data
```

- Validation：说话时 `audio diag` 的 capture/preproc peak 必须非零；H.69 已观察到非零 capture/preproc peak 和本地 VAD/KWS 相关运行证据。
- Risks：左右槽位、麦间距 50mm、L/R 边沿和声学方向仍需声学测试，不影响“采到音”的基本结论。

### Speaker / Amplifier / Loopback

- Purpose：把 RTL8730E 内部 codec 的 line-out 差分模拟输出放大到 CN2 喇叭，同时提供一条模拟回采/参考线索。
- Components：`U7 AXS2033/QFN8/AXS`，CN2 `WTB0818A-M02-00R`，原理图标注 `1W Speaker`。
- Hardware path：
  - SoC/HAL 侧：AmebaSmart `AMEBA_AUDIO_DEVICE_SPEAKER` 默认进入内部 codec `APP_LINE_OUT` route；SDK 在 `ameba_audio_stream_render.c` 中把 DAC line-out 配为 `DIFF` 并 unmute。
  - SoC pins：RTL8730E `LINEOUTLN` pin51 / `LINEOUTLP` pin52 分别出 `LINEOUT_LN/LP`；`LINEOUTRP/RN` 在本页未接到 U7。
  - Input network：`LINEOUT_LN -> C76 0.1uF -> R56 39K -> U7 IN-`；`LINEOUT_LP -> C153 0.1uF -> R57 39K -> U7 IN+`。
  - Amplifier：AXS2033 `OUTP/OUTN` 对应原理图 `SPKP/SPKN`，经 FB1/FB2、TVS7/TVS8 到 CN2。CN2 两端是 BTL/differential speaker output，不要把任一端接地当单端音频。
  - Power：U7 `VDD` 接 `VCC_5V_IN`，旁路电容包括 C65/C66/C67/C78/C79 等；BYPASS/偏置相关电容会影响启动噪声和 POP。
  - Loopback：`SPK_OUTP/N` 通过 `R22/R26 20K` 与后续 RC/分压网络到 `MIC5_P/N`。这是板级模拟回采线索；是否用于 AEC/reference 取决于后续 HAL capture 配置，不能默认等同于当前软件参考通道。
- AXS2033 parameters：
  - Datasheet p4：D 类模式 VDD 2.5-5.5V；5V/4ohm/10% THD+N 典型 3.1W，5V/8ohm/10% THD+N 典型 1.8W。
  - Datasheet p8：增益由外部输入电阻 Ri 和内部 400K feedback 决定，示例 Ri=22K 时约 23.4dB。本板 `Ri=39K`，按 `400K/(39K+5K)` 估算约 9.09 倍 / 19.2dB。
  - `Ci=0.1uF` 与 `Ri=39K` 的输入高通截止按 `1/(2*pi*Ri*Ci)` 估算约 40.8Hz。
  - `SD/SHUT` 电压区间：`<0.35V` shutdown，`1.2-1.5V` AB 类，`1.7-2.1V` D 类防破音关闭，`>2.3V` D 类防破音模式 1。
- BSP/HAL boundary：
  - Realtek SDK 默认 speaker route 已对应内部 line-out，不需要 I2S 外部 codec，除非硬件改版。
  - SDK 默认功放脚 `AUDIO_HW_AMPLIFIER_PIN=_PB_19` 与本板 `MUTE=PB25` 不匹配。当前项目 `include/river/river_audio_hw_overrides.h` 只覆盖 DMIC pin；音频输出 bring-up 必须核对是否已通过编译宏或其他 board code 控制 PB25。
  - 如果使用 SDK `AmpDummy` GPIO 控制，`enabled=true` 会 GPIO high，`enabled=false` 会 GPIO low；这与 AXS2033 SD 电压模式的关系必须用 U7 pin1 实测确认。
- Expected HAL-level logs / checks：
  - Audio HAL output creation通常会出现 `startAudioHwStreamOut ...`、`tx start at:...`；项目播放服务会有 `playback start` / `playback stop` / `playback_service` status。
  - 无声时先分层：HAL output stream 是否写入并 start、LINEOUT_LN/LP 是否有差分波形、PB25/U7 pin1 是否处于目标 SD 区间、U7 VDD 是否 5V、CN2 差分端是否有波形、喇叭/线束是否正常。
- Risks：
  - 错用 SDK 默认 `_PB_19` 控制功放，会导致 HAL 认为 amplifier enabled，但 U7 `SHUT/SD` 未被正确拉高/拉低。
  - 把 `MUTE` 作为普通布尔 mute 处理会掩盖 AXS2033 多电压模式；必须记录目标电压区间。
  - CN2 是差分输出，示波器/音频注入测试需使用差分测量或同地安全测法，避免把 BTL 一端短到地。

### Wi-Fi / BT RF

- Component：RTL8730E 内部 Wi-Fi/BT RF，`U8 FLT18D24255171D-3271A`，T2 IPEX，ANT4 onboard BT antenna。
- Firmware impact：RF 匹配和天线选择主要硬件侧；BSP/网络 bring-up 需处理空 efuse 时的 country/tx power fallback、MAC/identity 来源和连接策略。
- Validation：Wi-Fi 连接日志应出现 scan、candidate、auth/assoc、DHCP；如果 efuse 为空，应记录 MAC 来源和是否稳定，业务绑定策略不在本文定义。

### LCD / Touch / Backlight

- Connector：`CN6 A113F-15025WUA-R01`。
- DSI：`MIPI_TXD1P/N`、`MIPI_TXCLKP/N`、`MIPI_TXD0P/N`。
- Power/reset：`VCC_3V3_LCD`，`RST_LCD`，`LCD_PWR_ON`。
- Touch：`TP_SCL2`、`TP_SDA2`、`TP_INT`、`TP_RST`。
- Backlight：`U10 STI9287C`，`LCD_BL_PWM`，LED4 串 2 并。
- New local references：
  - `doc/hard/LCD/ST7102_480480_60hz_0109_tp 3ms .txt`：ST7102/480x480/60Hz init table。
  - `doc/hard/LCD/Sitronix Touch Driver 移植手册.pdf`：Sitronix ST71xx/ST7102 touch driver 移植手册。
  - `doc/hard/LCD/ST_TDDI_TPDriver_v45.00.260402/`：Sitronix vendor Linux driver/source bundle。
- Hardware path：
  - RTL8730E DSI pins：`DSI_DN1/DP1` -> `MIPI_TXD1N/P`，`DSI_CN/CP` -> `MIPI_TXCLKN/P`，`DSI_DN0/DP0` -> `MIPI_TXD0N/P`。
  - CN6 panel pins：D1P/D1N pins 9/10，CLKP/CLKN pins 12/13，D0P/D0N pins 15/16。
  - CN6 power：`VCCIO` pin1、`VCC3` pin3、`VCC5` pin5、`VCC_TP` pin18，电源来自 LCD power rail。
  - Touch bus：CN6 `I2C_SCL/SDA` pins 19/20，经 `TP_SCL2/SDA2` 和 R36/R37 100R 串联到 `TH_I2C_SCL/SDA`，上拉为 4.7K 组；这意味着触摸和 TH 外设可能共享同一 I2C bus。
  - Touch control：CN6 `TP_INT` pin21、`TP_RST` pin22。
  - Backlight：`LCD_BL_PWM` 通过 R42 到 U10 EN；U10 + L11/D1 产生 LEDA/LEDK 背光驱动，R43 10K 下拉使默认关背光。
- ST7102 init evidence：
  - 文件名标注 `480480_60hz`；命令表中 `SSD_SEND(0x01,0x35,0x00)` 启用 TE，`0x36=0x00` 设置 MADCTL。
  - `SSD_SEND(0x01,0x11)` 后 `Delay(250)`，`SSD_SEND(0x01,0x29)` 后 `Delay(200)`。
  - `SSD_MODE(1,1)` 注释含义为 non-burst sync events + HS mode enable。
  - `SSD_LANE(1,0)` 注释含义为 1 lane + lane speed auto；但原理图已路由 D0/D1 两条 data lane。该冲突需要确认，不能在 driver 中硬编码为唯一事实。
- Sitronix touch evidence：
  - 手册适用 `ST7123、ST7121P、ST7123P、ST7102`，接口支持 I2C 或 SPI。
  - I2C 示例 `clock-frequency=<400000>`；SPI 示例 `spi-max-frequency=<8000000>`。
  - 示例 IRQ 是 falling edge / active-low，reset-gpio active-low；实际 GPIO 编号需映射为本板 PA9/PA10。
  - 源码包 `sample.dtsi` 的 I2C 示例是 `compatible="sitronix_ts"`、`reg=<0x55>`；SPI 示例是 8MHz、CPOL/CPHA 均为 1。当前板原理图只给出触摸 I2C，因此 SPI 示例仅作芯片能力参考。
  - `TouchDriver_PortingGuide.txt` 要求选择 platform、bus interface、boot type；`SITRONIX_TP_WITH_FLASH` 表示触摸 IC 自带 flash boot，否则 host download 要通过 `request_firmware()` 提供 firmware+CFG dump。
  - 正常 probe 日志示例包括 `IC SFR VER = 0x43`、`Display id = 0x80 0xA0 0xFB`、`Chip ID = 0x83`；异常 `0` 或 `0xFF` 表示通信总线未就绪。
  - 源码 `sitronix_ts.c` 会打印 `IC Chip ID` 和 `IC SFR VER`，IRQ 使用 `IRQF_TRIGGER_FALLING | IRQF_ONESHOT`；ST7102 自检头文件 `sitronix_ts_test_st7102.h` 可作为量产自检阈值/流程参考。
  - 手册常见问题提示 ST71xx 显示参数 `VFP` 需要大于 280，并确认 Initial Code 中是否存在 `0x79` 寄存器；当前 ST7102 init table 未看到 `0x79`，应向屏厂确认该要求是否适用于本屏版本。
- BSP/HAL/driver action：
  - 最小顺序建议停留在硬件层：`LCD_PWR_ON` 使能 LCD rail -> 按 panel 资料拉 `RST_LCD` -> 配 DSI lane/timing/init table -> 验证 panel 响应或 TE -> 再开 `LCD_BL_PWM`。
  - Touch driver 不应直接照抄 Linux DTS GPIO 编号或 DRM/fb notifier；应抽取 I2C register transaction、PA9 falling IRQ、PA10 reset、地址 probe、firmware loading、coordinate parser 和 self-test 到 Ameba BSP/driver。
  - `TP/TH` 共线时先做 I2C scan，记录所有地址，再决定 touch 与 TH 的驱动绑定顺序。
- Blockers still open：完整 DSI timing、lane count、reset pulse、背光亮度范围、touch 实际 I2C 地址/firmware 文件、TH 传感器型号。

### BL702 Zigbee

- Component：`U12 BL702C-10-Q2H/QFN32/BL`，`X2 32MHz`，T3 IPEX。
- Power：`VCC_3V3_Z`，多路 AVDD/VDDIO/DCDC decap。
- Control：
  - `RST_BL702` 到 BL702 `PU_CHIP` / reset network。
  - `BOOT_BL702` 到 `GPIO_28/Bootstrap/I2C_SCL`。
  - 原理图注释：`GPIO28 0--boot From Flash 1--boot From Uart`。
- UART：
  - RTL8730 `TX1` -> `BL702_UART_RX1` -> BL702 GPIO15/UART_RX。
  - RTL8730 `RX1` <- `BL702_UART_TX1` <- BL702 GPIO14/UART_TX。
- Firmware action：默认拉低 boot，释放 reset 后从 BL702 flash 启动；只有升级/恢复时拉高 boot 并进入 UART boot。
- Missing：BL702 固件通信协议、baudrate、升级流程。

### IR

- Hardware：CN7 `IR_LED_N` 由 `VCC_5V_IN` 供电，经 R30 2.7R；Q3 NMOS 由 `IR_TX` 控制。
- Firmware action：若做红外遥控，`IR_TX` 应走 PWM/定时器输出 38kHz carrier；确认 PA3 是否可用对应 PWM alt function。
- Risk：IR 电流由硬件限制，固件 duty 不能无限制常开。

### Temperature / Humidity Sensor

- Connector：`CN5 A113F-15006WUA-R01/0.5MM/6PIN`，nets `TH_I2C_SDA/SCL`、`VCC_3V3`、TVS5/TVS6。
- Firmware action：先做 I2C scan，再按确认型号加载驱动。
- Blocker：外接传感器型号和地址未知。

## Power, Reset, And Boot

| Topic | Schematic Observation | Datasheet / SDK Requirement | Firmware Impact | Confidence |
| --- | --- | --- | --- | --- |
| Main input | `VCC_5V_IN` 进入 HOLD/LCD/Zigbee regulators | 取决于电源 IC datasheet，未纳入本次资料 | 上电后不要立刻拉大负载；LCD/Zigbee 分阶段开 | B |
| RTL8730E reset | `CHIP_EN`、SW2 reset、40MHz crystal | SDK boot 依赖 NAND 和 clock | 保持现有 SDK boot，不修改 SDK 源码 | B |
| NAND boot/download | U11 SPI NAND 128MB | Flash tool 必须选择 NAND profile | `river_flash.py` 默认 NAND | A |
| BL702 boot | GPIO28 low Flash, high UART | BL702 boot strap | `BOOT_BL702` 默认低 | A |
| LCD power | `LCD_PWR_ON` 控 U4 3.3V_LCD，CN6 还有 VCCIO/VCC3/VCC5/VCC_TP pins | panel/touch 上电和 reset 时序未完整给出 | DSI/touch probe 前必须确认 rail 稳定；背光最后打开 | B |
| AXS2033 SD | `PB25/MUTE` 控 U7 SHUT/SD | SD 电压分多个工作区间，不是单纯 mute | BSP/HAL 要驱动 PB25 并测 U7 pin1 电压 | A/B |

## Clocks And Timing

| Clock / Signal | Source | Destination | Frequency / Mode | Firmware Configuration | Evidence |
| --- | --- | --- | --- | --- | --- |
| RTL8730E XTAL | X1 | RTL8730E XI/XO | 40MHz | SDK platform default | P07 |
| PDM CLK | RTL8730E PA2 | MSM261DDB021 CLK | SDK 当前 16k 路径通常配置 2.5MHz；麦支持 1.1-4.8MHz 标准模式 | DMIC clock via Audio HAL | MSM datasheet + SDK `ameba_audio.h` |
| BL702 XTAL | X2 | BL702 XTAL_HF_IN/OUT | 32MHz | BL702 固件侧 | P12 |
| LCD MIPI clock | RTL8730E DSI | CN6 panel | ST7102 init table 写 480x480/60Hz、lane speed auto；lane rate/porch 未给 | DSI timing 待屏厂确认，lane_count 不可写死 | P10 + ST7102 init |
| IR carrier | RTL8730E PA3 candidate | IR LED driver | 常见 38kHz，未由原理图证明 | 待遥控协议 | P08/P11 |

## Communication Interfaces

| Interface | Instance | Pins/Nets | Connected Device | Address / CS / IRQ | Driver Notes | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| QSPI NAND | RTL8730E flash controller | `PC1-PC6`, `FLASH_QSPI_*` | GD5F1GM7UEYIGR | CSN on PC6 | NAND boot/download profile | P11 |
| PDM/DMIC | Audio HAL | PA2 CLK, PA4 DATA1 | MSM261DDB021 mic board | shared data + L/R slot | `DMIC3/DMIC4` | P08/P09/P11 + SDK |
| Audio output | Audio HAL speaker / internal codec line-out | `LINEOUT_LN/LP`, `PB25/MUTE`, CN2 `SPKP/SPKN` | AXS2033 + 1W speaker | SD voltage mode, BTL output | speaker route -> line-out DIFF；amp pin must match PB25 | P08/P11 + SDK + AXS2033 |
| UART1 | RTL8730 UART1 | `RTL8730_TX1/RX1` | BL702 UART1 | no flow control shown | Zigbee control/protocol | P11/P12 |
| UART0 | RTL8730 UART0 | `RTL8730_TX0/RX0` | base interface CN1 | no flow control shown | debug/base board | P05/P11 |
| MIPI DSI | RTL8730 DSI | D0/D1/CLK pairs | ST7102-class LCD panel | panel reset on PA14 | init table present; lane/timing conflict open | P10 + ST7102 init |
| I2C | likely PB10/PB11 | `TP_SDA2/SCL2`, `TH_I2C_SDA/SCL` | Sitronix touch + TH | touch address unconfirmed, TP_INT PA9 | 400kHz max from Sitronix guide; scan before driver bind | P10/P13 + Sitronix guide |
| GPIO/PWM | PA16 | `LCD_BL_PWM` | STI9287C EN | PWM duty | default off, fade after panel init | P10 |
| GPIO/PWM | PA3 | `IR_TX` | NMOS IR LED | active high likely | carrier output | P08/P11 |

## Firmware Bring-Up Checklist

1. Audio capture:
   - Confirm boot logs contain `pdm-2mic-pa2-pa4-data1`, `DMIC3/DMIC4`, `clk=PA2 data1=PA4`, `set DMIC clock`, `capture params applied: ret=0`.
   - Speak near the mic and check `audio diag` capture/preproc peak is nonzero.
   - If voice quality is poor, do not scan DATA0-3 again; test L/R slot, channel order, gain/AGC, VAD/KWS threshold and echo/reference path.

2. NAND flashing:
   - Use `python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000`.
   - Expected flash identification: NAND, `GD5F1GM7U`, `1Gb/128MB`.
   - If profile mismatch appears, verify NAND/NOR selection before changing firmware.

3. BL702:
   - Default `BOOT_BL702=0` before releasing reset.
   - Toggle `RST_BL702` only after `VCC_3V3_Z` is stable.
   - Capture BL702 UART boot log and document baudrate/protocol.

4. LCD/touch:
   - 已有 ST7102 init table 和 Sitronix touch 手册，但仍需确认 DSI lane count、lane rate/porch、reset pulse、touch I2C 地址。
   - 硬件层最小顺序：`LCD_PWR_ON` high -> 确认 CN6 power rails -> `RST_LCD` pulse -> DSI lane/timing/init table -> 验证 panel/TE -> `LCD_BL_PWM`。
   - 如果按 init table 使用 `SSD_LANE(1,0)`，必须记录这是来自供应商表的 1-lane 配置；若改 2-lane，要有屏厂资料或实测依据。
   - Touch：PA10 reset 后在 PB10/PB11 对应 I2C bus scan；`0x55` 是源包给出的优先候选地址，正常 Sitronix probe 应能读到非 `0/0xFF` 的 chip id / SFR version。
   - `TP/TH` 可能共 I2C，绑定驱动前记录总线所有地址。

5. Speaker / audio output:
   - 确认 BSP/HAL 的功放控制脚是 `PB25/MUTE`，而不是 SDK 默认 `_PB_19`；检查编译宏 `AUDIO_HW_AMPLIFIER_PIN` 或等效板级控制代码。
   - 用 Audio HAL output stream 或 SDK speaker route 做纯本地 PCM tone 测试，边界停在 `AMEBA_AUDIO_DEVICE_SPEAKER -> APP_LINE_OUT/LINEOUT`。
   - 测 U7 `VDD=VCC_5V_IN`，测 U7 pin1 `SHUT/SD` 在 boot、idle、stream start、standby/stop 的电压区间。
   - 若 HAL output stream start/write 正常但无声，依次查 `LINEOUT_LN/LP` 差分波形、U7 IN+/IN-、U7 OUTP/OUTN、CN2 喇叭线束。
   - CN2 是差分/BTL 输出，测试时不要把 `SPK_OUTP` 或 `SPK_OUTN` 当单端对地输出。

6. IR:
   - Validate PA3 PWM capability and current limit before enabling long carrier bursts.

## Risks And Unknowns

| Risk | Impact | Confidence | How To Resolve |
| --- | --- | --- | --- |
| LCD DSI lane/timing 未闭合 | 可能白屏、花屏、TE/触摸同步异常 | B | 向屏厂确认 lane_count/lane_rate/porch/reset；保留 ST7102 init table 的来源和可配置性。 |
| Sitronix touch 地址/固件匹配未闭合 | probe 失败、触摸无响应或固件下载失败 | B | reset 后 scan/读 chip id；以源包 `reg=<0x55>` 为候选但保留可配置地址，并确认 flash boot/host download。 |
| TH sensor model missing | Cannot know I2C address/register map | A | Provide module BOM or run I2C scan and identify chip marking. |
| AXS2033 SD voltage not measured | Could leave amp in wrong mode or shutdown | B | Probe U7 pin1 under boot/idle/HAL stream/standby states. |
| SDK 默认功放脚与本板不匹配 | Audio HAL 自动使能可能控制 `_PB_19` 而非 `PB25/MUTE`，导致 line-out 有信号但喇叭无声 | A | 在 BSP/HAL 配置中覆盖 `AUDIO_HW_AMPLIFIER_PIN=_PB_25` 或实现等效 PB25 控制，并用测量验证。 |
| PDM L/R slot/channel order unknown | Beamforming/KWS quality may be suboptimal | B | Per-mic near-field test and optional raw WAV dump. |
| BL702 protocol unknown | Zigbee feature cannot be integrated safely | A | Provide BL702 firmware protocol and upgrade mode docs. |
| External web datasheets not all downloaded locally | Some vendor URLs are unstable from CLI | B | Store exact RTL8730E/BL702/GD5F1 datasheets under `doc/hard/` when obtained. |

## Source Notes

- Local artifacts:
  - `doc/hard/RTL8730 4寸SCH.pdf`
  - `doc/hard/RTL8730_4寸_丝印图.pdf`
  - `doc/hard/1.01.070080 MSM261DDB021_Rev1.0.pdf`
  - `doc/hard/AXS2033.pdf`
  - `doc/hard/LCD/ST7102_480480_60hz_0109_tp 3ms .txt`
  - `doc/hard/LCD/Sitronix Touch Driver 移植手册.pdf`
  - `doc/hard/LCD/ST_TDDI_TPDriver_v45.00.260402/`
- SDK sources:
  - `/root/ameba-rtos/component/audio/audio_hal/amebasmart/ameba_audio_stream_capture.c`
  - `/root/ameba-rtos/component/audio/audio_hal/amebasmart/primary_audio_hw_stream_out.c`
  - `/root/ameba-rtos/component/audio/audio_hal/amebasmart/ameba_audio_stream_render.c`
  - `/root/ameba-rtos/component/audio/audio_hal/amebasmart/ameba_audio_stream_control.c`
  - `/root/ameba-rtos/component/audio/audio_driver/amp_dummy.c`
  - `/root/ameba-rtos/component/soc/usrcfg/amebasmart/include/ameba_audio_hw_usrcfg.h`
  - `/root/ameba-rtos/component/soc/amebasmart/fwlib/include/ameba_audio.h`
- Project sources:
  - `components/river_voice/river_voice_board.c`
  - `components/river_voice/river_voice_capture.c`
  - `include/river/river_audio_hw_overrides.h`
  - `tools/river_flash.py`
- External sources checked:
  - Realtek Ameba IoT docs: `https://ameba-aiot.github.io/ameba-iot-docs/freertos/en/latest/`
  - GigaDevice GD5F1GM7UEYIG product page: `https://www.gigadevice.com/product/flash/spi-nand-flash/gd5f1gm7ueyig`
  - Bouffalo Lab BL702/BL706 product page: `https://en.bouffalolab.com/product/?id=3&type=detail`
  - ChipSourceTek AXS2033 product page: `https://en.chipsourcetek.com/Audio-Chip/204.html`

## Skill Iteration Record

本次使用 `schematic-pcb-firmware-guide` 过程中发现一个可复用缺口：只有 PDF/图片时，需要稳定地产生 artifact inventory、文本提取和页面渲染，而环境未必安装 Poppler。因此已增强本地 skill：

- 新增 `/root/.codex/skills/schematic-pcb-firmware-guide/scripts/pdf_artifact_inventory.py`。
- `SKILL.md` 的 render/extract 步骤已补充 Poppler 缺失时使用该 helper。
- 已用当前 `doc/hard/*.pdf` 跑通 helper，生成了 `tmp/hard_skill_inventory/artifact_inventory.json` 和文本/渲染中间产物。

本轮继续发现第二个可复用缺口：硬件报告容易越界到业务方案，或只写“外设存在”而没有写完整硬件到 HAL 的路径。因此已继续增强本地 skill：

- `SKILL.md` 新增 “Set the report boundary”，明确报告边界停在 BSP/HAL/driver、pinmux、电源时序、板级测点和验证，不擅自引入云端/业务协议/应用交互。
- `SKILL.md` 要求对每个外部连接器或混合信号块写完整硬件路径，并交叉检查 SDK 默认板级配置是否仍指向 reference board。
- `references/report_template.md` 新增 `Report boundary`、`Hardware path`、`Firmware boundary`、`BSP/HAL/driver interface`、`Board constants and electrical limits`。
- 模板对 audio output 和 display/touch 增加强制提示：音频输出必须覆盖 HAL-visible playback device、SoC line-out/I2S、功放输入/输出、gain、shutdown/mute/mode、speaker connector、loopback/reference nets 和测点；显示触摸必须覆盖 panel connector、lane 冲突、reset/power/backlight、touch bus/address/IRQ/reset、init-command evidence 和 timing gaps。

本轮按用户纠偏继续收紧 skill 和报告定位：

- `SKILL.md` 新增 final scope audit，要求交付前搜索 cloud/session/TTS/STT/product flow 等词，避免把业务或产品流程写成硬件结论。
- `references/report_template.md` 新增 `Scope Audit`，明确建议必须停在 BSP/HAL/driver 配置、本地诊断或板级测量。
- 本报告新增“报告边界和读者”章节，并把音频输出验证表述收敛为 `Audio HAL speaker route -> LINEOUT -> AXS2033 -> CN2 speaker`。
- 报告纳入 `ST_TDDI_TPDriver_v45.00.260402` 源码包证据：`sample.dtsi` 的 I2C `reg=<0x55>`、porting guide 的 bus/boot type、probe 日志、falling IRQ 和 ST7102 self-test header。
