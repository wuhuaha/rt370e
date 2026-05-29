# RTL8730E 4 寸板硬件固件说明书

日期：2026-05-29

对象：Orvibo RTL8730E 4 寸板，主原理图 `RTOS_4InchLCD` / `MixPad4_RTL8730`，Rev `V0.1`，日期 `2026-05-06`。

目的：充分解读 `doc/hard/` 中的硬件资料，把关键器件型号、引脚接线、外部资料和 BSP/HAL/driver bring-up 建议整理成固件同事可执行的 handoff。本文不设计业务流程，不假设产品层播放、录音、交互或网络策略。

交互版：同目录 [RTL8730E_4INCH_HARDWARE_FIRMWARE_GUIDE_ZH.html](/root/ameba-river/doc/RTL8730E_4INCH_HARDWARE_FIRMWARE_GUIDE_ZH.html) 提供离线搜索、过滤、关键器件卡片、接线图、可勾选 checklist。

## 摘要

- 主控是 `RTL8730EAM`。固件直接关心的页是 P06-P11：电源域、40MHz 晶振、USB/reset、PDM/LINEOUT、RF、MIPI DSI、GPIO、SPI NAND。
- 数字麦克风走 PDM：`PDM_CLK -> RTL8730E PA2`，`PDM_DAT1 -> RTL8730E PA4`。AmebaSmart Audio HAL 中 DATA1 对应 `AUDIO_DMIC3/DMIC4`，不是 DATA0 的 `DMIC1/DMIC2`。
- 音频输出是 RTL8730E 内部 codec 差分 line-out 到 `U7 AXS2033` 功放，再到 `CN2` 1W 喇叭接口。固件边界应描述为 `Audio HAL speaker/line-out -> LINEOUT_LN/LP -> AXS2033 -> CN2`。
- 功放控制网名是 `MUTE`，接 RTL8730E `PB25`。AXS2033 的 `SD/SHUT` 脚不是简单 enable，电压区间决定 shutdown、AB 类、D 类不同工作模式，调试小声/无声时必须实测 U7 pin1。
- 当前固件默认播放音量已经是最大档：MCP 音量默认 `100`，本地 playback/tone/echo 路径音量常量为 `1.00f`。如果扬声器仍小，优先检查 U7 SD 电压、LINEOUT 差分幅度、AXS2033 输出、喇叭负载和增益阻容。
- 启动 Flash 是 `U11 GD5F1GM7UEYIGR` SPI NAND，原理图标注 128MB。烧录必须使用 NAND profile，不要套 NOR profile。
- LCD 连接器 `CN6 A113F-15025WUA-R01` 引出 MIPI DSI D0/D1/CLK、LCD reset、背光、触摸 I2C/INT/RST。原理图有两条 data lane，供应商 init table 有 `SSD_LANE(1,0)`，lane 配置需在驱动里可配置并实测确认。
- 触摸资料来自本地 Sitronix ST71xx/ST7102 包，候选 I2C 地址 `0x55`，IRQ falling、reset active-low 是初始假设，必须以上电后的 I2C scan 和 chip id 为准。
- BL702 Zigbee 子系统由 RTL8730E 控制电源、reset、boot strap 和 UART1。`BOOT_BL702` 对应 BL702 GPIO28，原理图注释：`0` 从 Flash boot，`1` 从 UART boot。

## 报告边界

| 项 | 说明 |
| --- | --- |
| 面向读者 | BSP、HAL、外设驱动、pinmux、启动下载、电源/复位时序、板级诊断的软件同事。 |
| 范围内 | SoC 引脚、器件型号、连接器 pin/net、驱动配置、SDK 默认值差异、测量点、预期底层日志、失败排查。 |
| 范围外 | 产品交互、业务播放/录音流程、会话状态机、UI 行为、网络服务策略。 |
| 写法约束 | 从 SoC/HAL/driver 可见接口写到 net、器件、连接器、测点；不把上层业务路径写成硬件结论。 |

## 证据索引

| ID | 结论 | 来源 | 类型 | 可信度 | 备注 |
| --- | --- | --- | --- | --- | --- |
| E1 | 主原理图为 `RTOS_4InchLCD` / `MixPad4_RTL8730`，Rev `V0.1`，日期 `2026-05-06` | `doc/hard/RTL8730 4寸SCH.pdf` title blocks | schematic | A | P05-P13 已抽取文本并渲染复核。 |
| E2 | `PDM_CLK` 接 RTL8730E `PA2`，`PDM_DAT1` 接 `PA4` | 主原理图 P09/P11；用户硬件说明 | schematic + user fact | A | 用户说明与原理图一致。 |
| E3 | AmebaSmart Audio HAL DATA1 对应 `AUDIO_DMIC3/DMIC4` | `/root/ameba-rtos/component/audio/audio_hal/amebasmart/ameba_audio_stream_capture.c` | SDK source | A | DATA0/1/2/3 分别映射 DMIC1/2、3/4、5/6、7/8。 |
| E4 | 项目已覆盖 DMIC clock/data1/amp pin | `include/river/river_audio_hw_overrides.h`、`CMakeLists.txt` | project source | A | `_PA_2`、`_PA_4`、`_PB_25`。 |
| E5 | PA2/PA4 DATA1 已实板验证可采到有效输入 | `.codex/changes.md` Step H.xiaozhi-client.69 | runtime record | A | capture/preproc peak 非零。 |
| E6 | 当前项目播放音量默认已调到最大 | `components/river_cloud/river_orvibo_mcp_volume.c`、`components/river_voice/river_orvibo_audio_service.c`、`components/river_diag/river_diag_cmd.c`、`components/river_voice/river_voice_echo.c` | project source | A | 默认 100%，播放/tone/echo `1.00f`。 |
| E7 | `MSM261DDB021` 是 PDM digital output MEMS microphone | `doc/hard/1.01.070080 MSM261DDB021_Rev1.0.pdf` | local datasheet | A | 3.50mm x 2.65mm x 0.98mm LGA，PDM 输出。 |
| E8 | `U7` 原理图 raw value 为 `U/AXS2033/QFN8/AXS`，归一化型号为 `AXS2033` | 主原理图 P08；`doc/hard/AXS2033.pdf` | schematic + datasheet | A | 3.1W 单声道 AB/D 类音频功放。 |
| E9 | AXS2033 SD/SHUT 电压区间选择 shutdown/AB/D 工作模式 | `doc/hard/AXS2033.pdf` p2/p8 | local datasheet | A | 不是普通二值 enable。 |
| E10 | RTL8730E `LINEOUTLN/LP` pins 51/52 接 AXS2033 `IN-/IN+` | 主原理图 P08 | schematic | A | 经 C76/C153 和 R56/R57。 |
| E11 | `PB25 -> MUTE -> U7 SHUT/SD` | 主原理图 P08/P11 | schematic | A | R20 100R、R21 10K、C69/C71 构成控制/延时网络。 |
| E12 | `U7` BTL 输出经 FB1/FB2 到 `CN2` 喇叭接口 | 主原理图 P08 | schematic | A | `CN2 pin2=SPK_OUTP`、`pin1=SPK_OUTN`，不能把任一端当地。 |
| E13 | 功放回采/参考网络由 `SPK_OUTP/N` 经 R22/R26 等回到 `MIC5_P/N` | 主原理图 P08 | schematic | A | 用于回采/参考，不等于外部麦输入。 |
| E14 | `U11 GD5F1GM7UEYIGR` 是 SPI NAND，原理图标注 `Nand Flash128MB/WSON8/GD` | 主原理图 P11 | schematic | A | QSPI pins PC1-PC6。 |
| E15 | GigaDevice 在线资料把 GD5F1GM7UE 归类为 1Gb SPI NAND | GigaDevice product page | vendor web | B | 与本板 128MB 标注一致。 |
| E16 | Realtek 工具/文档区分 NAND/NOR 镜像或下载 profile | Realtek AmebaSmart Image Tool / flash layout docs | vendor docs | B | 当前板以本地 wrapper 和实物为准。 |
| E17 | `CN6 A113F-15025WUA-R01` 引出 DSI D0/D1/CLK、touch I2C/INT/RST、backlight | 主原理图 P10 | schematic | A | 两条 data lane 均布线。 |
| E18 | `U10 STI9287C` 是 LCD 背光升压/LED driver，EN 接 `LCD_BL_PWM` | 主原理图 P10；Toll Semi online page | schematic + vendor web | A/B | LED4 串 2 并，5V 输入。 |
| E19 | Sitronix 本地手册/源码支持 ST7123/ST7121P/ST7123P/ST7102，示例 I2C 地址 `0x55` | `doc/hard/LCD/` | local vendor docs/source | B | Linux DTS 示例，Ameba 侧需迁移。 |
| E20 | LCD init table 写 `SSD_LANE(1,0)`、`0x11` 后 250ms、`0x29` 后 200ms | `doc/hard/LCD/ST7102_480480_60hz_0109_tp 3ms .txt` | local vendor init | B | 与原理图两 data lane 路由存在待确认点。 |
| E21 | `U12 BL702C-10-Q2H` 是 BL702 子系统主控，UART1 连接 RTL8730E | 主原理图 P12 | schematic | A | RTL TX1/RX1 到 BL702 GPIO15/GPIO14。 |
| E22 | BL702 GPIO28 boot strap：0 Flash，1 UART | 主原理图 P12 注释 | schematic | A | 默认态影响 Zigbee 子系统启动。 |
| E23 | Bouffalo SDK/GitHub 可作为 BL702 GPIO/UART/boot 参考 | BouffaloLab SDK GitHub | vendor SDK | B | 协议和量产固件仍需硬件/无线 owner 提供。 |
| E24 | `U8 FLT18D24255171D-3271A` 连接 2.4G/5G/ANT，RF path 不应由普通固件调试随意改 | 主原理图 P09 | schematic | A | RF 参数涉及射频硬件与法规。 |
| E25 | `U3 TMI3411/2A`、`U1/U4 TMI6050-33`、`U2 EY404-CF42F1` 是电源/电源键相关器件 | 主原理图 P05 | schematic | A | 在线资料仅作为补充，固件以原理图电源网和实测为准。 |
| E26 | `X1` 40MHz 连接 RTL8730E `XI/XO`，`X2` 32MHz 连接 BL702 | 主原理图 P07/P12 | schematic | A | 时钟异常会表现为 SoC/BL702 启动异常。 |
| E27 | `CN5 A113F-15006WUA-R01` 温湿度 FPC 只给出 I2C 和 3.3V，未给传感器型号 | 主原理图 P13 | schematic | A | 只能先做 bus scan 和硬件澄清。 |
| E28 | Linux Sitronix driver、Zephyr DMIC、Linux MTD SPI NAND、LVGL display porting 可作为驱动边界参考 | 上游文档/源码 | open-source docs/source | B/C | 只参考驱动结构和验证方法，不证明本板接线。 |
| E29 | Realtek RTL8730E Hardware Design Guide 可用于 SoC 电源/硬件设计交叉检查 | Realtek hardware guide | vendor docs | B | 不替代本板原理图。 |

## 资料清单

| 资料 | 路径 / URL | 使用范围 | 方法 | 备注 |
| --- | --- | --- | --- | --- |
| 主原理图 | `doc/hard/RTL8730 4寸SCH.pdf` | P05-P13 | PyMuPDF text + rendered image | 主要硬件证据。 |
| 丝印图 | `doc/hard/RTL8730_4寸_丝印图.pdf` | p1-p2 | render | 辅助定位，文本层不足。 |
| PDM 麦 datasheet | `doc/hard/1.01.070080 MSM261DDB021_Rev1.0.pdf` | p3-p14 | text + render | 本地高可信资料。 |
| 功放 datasheet | `doc/hard/AXS2033.pdf` | p1-p9 | text + render | 本地高可信资料。 |
| LCD init table | `doc/hard/LCD/ST7102_480480_60hz_0109_tp 3ms .txt` | full file | text | 缺完整 timing sheet。 |
| Sitronix touch 手册 | `doc/hard/LCD/Sitronix Touch Driver 移植手册.pdf` | p1-p21 | text + render | ST71xx/ST7102 移植参考。 |
| Sitronix source bundle | `doc/hard/LCD/ST_TDDI_TPDriver_v45.00.260402/` | source | source review | DTS、probe、self-test、I2C/SPI 代码。 |
| SDK | `/root/ameba-rtos` | audio HAL、usrcfg、flash/image tools | source review | 检查 SDK 默认值和项目 override。 |
| 项目源码 | `/root/ameba-river` | include/components/tools | source review | 检查已落地 board constants、音量和诊断命令。 |

## 固件工程师入口地图

| 开发区域 | 先知道的硬件事实 | 关键器件/连接器 | BSP/HAL/driver 边界 | 第一验证 |
| --- | --- | --- | --- | --- |
| 语音输入 | `PDM_CLK=PA2`、`PDM_DAT1=PA4`、DATA1=`DMIC3/DMIC4` | CN7、小板 MSM261DDB021、RTL8730E U14D/U14F | Audio HAL DMIC pinmux + capture channel map | 看 `DMIC3/DMIC4` 日志和 PDM_CLK/DAT1 波形。 |
| 音频输出 | LINEOUT 差分进 AXS2033，BTL 到 CN2；`PB25` 控 SD/mode | U7 AXS2033、CN2、U14C/U14F | Audio HAL speaker/line-out + amp GPIO/mode | 本地 tone，测 U7 pin1、LINEOUT、SPK_OUTP/N。 |
| LCD 显示 | MIPI DSI D0/D1/CLK 到 CN6；reset/backlight 独立 | U14E、CN6、U10 | MIPI DSI host + panel init + backlight PWM | 先 power/reset/DSI，再开背光。 |
| Touch/TH | Touch 和 TH 可能关联到 PB10/PB11 I2C | CN6、CN5、Sitronix TDDI | I2C scan + reset/IRQ GPIO + device driver | 上电 reset 后 scan，读 chip id。 |
| Boot/Flash | 板上是 GD5F1GM7UEYIGR SPI NAND | U11 | Flash/download profile + bootloader layout | 用 NAND profile，确认工具识别 1Gb/128MB。 |
| Zigbee 子系统 | BL702 UART1、reset、boot strap、3.3V_Z 由 RTL 控 | U12、T3、PA5/PA13/PA15/PB19/PB20 | 子系统 power/reset/UART driver | 默认 BOOT 低，reset 后抓 BL702 UART log。 |
| 电源/复位 | LCD/Zigbee 是受控子电源；CHIP_EN 只是 SoC enable | U1/U3/U4/U2/Q1/Q2/SW2 | GPIO power sequencing + reset sequencing | 测 3.3V_HOLD/LCD/Z 和 CHIP_EN。 |

## 关键器件矩阵

| Block | Refdes | 归一化型号 | 原理图 raw value | 厂商/封装 | 固件相关角色 | 页 | BSP/HAL/driver 边界 | 资料 | 可信度 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| SoC | U14A-F | RTL8730EAM | `RTL8730EAM` | Realtek, module/SoC | 主控、Wi-Fi/BT、Audio、MIPI、QSPI、GPIO | P06-P11 | pinmux、HAL、boot、flash | E1/E29 | A |
| PDM mic | 小板器件 | MSM261DDB021 | 本地资料提供，主板 CN7 连接 | MEMSensing LGA | PDM digital mic | P08 + local datasheet | DMIC clock/data/channel | E2/E7 | A/B |
| MIC/IR FPC | CN7 | FPC512-10-RL-TA-01 | `CN/FPC512-10-RL-TA-01` | FPC, 10/12-pin symbol | PDM、IR、5V/3.3V 到小板 | P08 | connector pin/net validation | E2/E7 | B |
| Audio amp | U7 | AXS2033 | `U/AXS2033/QFN8/AXS` | ChipSourceTek/矽源特, DFN/QFN8 | line-out 到 speaker 功放 | P08 | amp SD/mode GPIO、speaker route | E8-E12 | A |
| Speaker connector | CN2 | WTB0818A-M02-00R | `CN/WTB0818A-M02-00R` | 2-pin WTB | BTL speaker output | P08 | board validation / speaker load | E12 | A |
| Boot flash | U11 | GD5F1GM7UEYIGR | `U/GD5F1GM7UEYIGR/Nand Flash128MB/WSON8/GD` | GigaDevice WSON8 | SPI NAND boot/download | P11 | NAND image profile、QSPI pins | E14-E16 | A/B |
| LCD/TP connector | CN6 | A113F-15025WUA-R01 | `CN/A113F-15025WUA-R01` | 27-pin FPC | panel/touch/backlight | P10 | MIPI DSI、I2C、reset、PWM | E17 | A |
| Backlight driver | U10 | STI9287C | `U/STI9287C/SOT23-6/TMI` | Toll/TMI, SOT23-6 | LED boost, EN/PWM | P10 | backlight enable/PWM timing | E18 | A/B |
| Touch controller | module | Sitronix ST7102/ST71xx | local LCD bundle | Sitronix TDDI | touch I2C/IRQ/RST | CN6 + local docs | touch driver probe/config | E19/E20 | B |
| Zigbee MCU | U12 | BL702C-10-Q2H | `U/BL702C-10-Q2H/QFN32/BL` | Bouffalo Lab QFN32 | UART-connected radio sub-MCU | P12 | power/reset/boot/UART | E21-E23 | A/B |
| RF diplexer/filter | U8 | FLT18D24255171D-3271A | same | RF component | 2.4G/5G/ANT path | P09 | RF path awareness only | E24 | A |
| Wi-Fi antenna | ANT4/T2 | ANT-MK1 / IPEX4 | `ANT/ANT-MK1/onboard`, `ANT-IPEX4` | RF antenna | RF output path | P09 | no normal firmware control | E24 | A |
| Buck regulator | U3 | TMI3411 | `U/TMI3411/2A/SOT23-5` | TMI/Toll, SOT23-5 | main buck rail | P05 | power sequencing awareness | E25 | A |
| LDOs | U1/U4 | TMI6050-33 | `U/TMI6050-33/SOT23-5` | TMI/Toll, SOT23-5 | 3.3V rails, LCD rail | P05 | LCD power enable timing | E25 | A |
| Power key IC | U2 | EY404-CF42F1 | `U/EY404-CF42F1 /SOT23-6/ELITECHIP` | EliteChip, SOT23-6 | power key / hold logic | P05 | boot/power behavior | E25 | B |
| Zigbee power switch | Q1/Q2 | IRLML6401 + S8050 | PMOS + NPN | discrete | controls `VCC_3V3_Z` | P05/P09 | Zigbee power GPIO | E25 | A |
| Main crystal | X1 | 40MHz 9pF | `X/40M/9pF/9S40000050/2016` | crystal | RTL8730E main clock | P07 | clock sanity | E26 | A |
| BL702 crystal | X2 | 32MHz | `X/32MHz/9S32000112/HL` | crystal | BL702 HF clock | P12 | BL702 boot sanity | E26 | A |
| TH FPC | CN5 | A113F-15006WUA-R01 | `FPC/A113F-15006WUA-R01/0.5MM/6PIN` | 6-pin FPC | external temp/humidity I2C | P13 | I2C scan only until model known | E27 | A |

## 引脚接线图谱

| Path | SoC / Driver Signal | SoC Pin / Instance | Net Chain | 目标管脚/器件 | 固件含义 | 证据 | 可信度 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| PDM clock | Audio HAL DMIC CLK | RTL8730E `PA2` pin 5 | `PA2 -> PDM_CLK -> CN7 + TP12` | PDM mic CLK | `AUDIO_HW_DMIC_CLK_PIN=_PA_2` | E2-E4 | A |
| PDM data | Audio HAL DMIC DATA1 | RTL8730E `PA4` pin 7 | `PA4 -> PDM_DAT1 -> R29 100R -> CN7 + TP13` | PDM mic DATA | use `AUDIO_DMIC3/DMIC4` | E2-E5 | A |
| Amp input negative | Audio line-out L negative | U14C `LINEOUTLN` pin 51 | `LINEOUT_LN -> C76 0.1uF -> LO_LN -> R56 39K -> U7 IN- pin4` | AXS2033 IN- | differential line-out input | E10 | A |
| Amp input positive | Audio line-out L positive | U14C `LINEOUTLP` pin 52 | `LINEOUT_LP -> C153 0.1uF -> LO_LP -> R57 39K -> U7 IN+ pin3` | AXS2033 IN+ | differential line-out input | E10 | A |
| Amp SD/mode | Audio amp GPIO | RTL8730E `PB25` pin 96 | `PB25 -> MUTE -> R20 100R -> U7 SHUT/SD pin1`; R21/C69/C71 to GND | AXS2033 mode/shutdown | set/measure SD voltage, not only boolean mute | E9/E11 | A |
| Speaker BTL positive | Speaker output | U7 pin5 `VQ+`/OUT path | `U7 pin5 -> SPKP -> FB1 -> SPK_OUTP -> CN2 pin2` | speaker + | differential output, not ground referenced | E12 | A |
| Speaker BTL negative | Speaker output | U7 pin8 `VQ-`/OUT path | `U7 pin8 -> SPKN -> FB2 -> SPK_OUTN -> CN2 pin1` | speaker - | differential output, not ground referenced | E12 | A |
| Loopback/reference P | speaker reference | U7 output | `SPK_OUTP -> R22 20K -> R24/C72/C73 -> C72 -> MIC5_P` | U14C `MICIN5P` pin39 | possible playback reference / loopback | E13 | A |
| Loopback/reference N | speaker reference | U7 output | `SPK_OUTN -> R26 20K -> R25/C75/C77 -> C77 -> MIC5_N` | U14C `MICIN5N` pin40 | possible playback reference / loopback | E13 | A |
| IR LED | GPIO/PWM/IR candidate | RTL8730E `PA3` pin 6 | `PA3 -> IR_TX -> R31 -> Q3 NMOS -> IR_LED_N` | CN7 IR LED path, 5V rail | use PWM/IR mux after current check | P08/P11 | B |
| LCD DSI lane1 | MIPI DSI | U14E pins 80/81 | `DSI_DN1/DP1 -> MIPI_TXD1N/P -> TVS1 -> CN6 pins10/9` | panel D1N/D1P | lane count/timing config | E17/E20 | A |
| LCD DSI clock | MIPI DSI | U14E pins 82/83 | `DSI_CN/CP -> MIPI_TXCLKN/P -> TVS1 -> CN6 pins13/12` | panel CLKN/CLKP | DSI host clock lane | E17 | A |
| LCD DSI lane0 | MIPI DSI | U14E pins 85/86 | `DSI_DN0/DP0 -> MIPI_TXD0N/P -> TVS2 -> CN6 pins16/15` | panel D0N/D0P | lane count/timing config | E17/E20 | A |
| LCD reset | panel reset GPIO | RTL8730E `PA14` pin16 | `PA14 -> RST_LCD -> R35 1K -> CN6 pin7 RST`; R34 pull-up | panel reset | active-low likely; follow timing | E17/E20 | A/B |
| Backlight | PWM/GPIO | RTL8730E `PA16` pin18 | `PA16 -> LCD_BL_PWM -> R42 -> U10 EN`; U10 SW/L11/D1 to LEDA/LEDK | backlight boost | open after panel init | E18 | A/B |
| Touch I2C | I2C | RTL8730E `PB10/PB11` pins57/58 | `TP_SDA2/SCL2 -> CN6 pins20/19`; via R36/R37 to `TH_I2C_SDA/SCL` | touch and maybe TH bus | scan before binding | E17/E19/E27 | A/B |
| Touch IRQ/RST | GPIO IRQ/reset | `PA9/PA10` pins12/13 | `TP_INT -> CN6 pin21`, `TP_RST -> CN6 pin22` | Sitronix touch | falling/active-low candidate | E19 | B |
| SPI NAND CS | QSPI | `PC6` pin95 | `FLASH_QSPI_CSN -> U11 /CS pin1` | NAND select | NAND boot/download profile | E14-E16 | A |
| SPI NAND CLK | QSPI | `PC2` pin90 | `FLASH_QSPI_CLK -> U11 CLK pin6` | NAND clock | QSPI NAND | E14 | A |
| SPI NAND IO0/1/2/3 | QSPI | `PC3/PC5/PC4/PC1` | `IO0->U11 pin5`, `IO1->pin2`, `IO2->pin3`, `IO3->pin7` | NAND data | QSPI NAND | E14 | A |
| BL702 UART | UART1 | `PB20/PB19` pins67/66 | `RTL8730_TX1 -> BL702 GPIO15/UART_RX pin23`; `RTL8730_RX1 <- GPIO14/UART_TX pin22` | Zigbee host UART | protocol/baud TBD | E21 | A |
| BL702 reset/boot | GPIO | `PA13/PA15` pins15/17 | `RST_BL702 -> PU_CHIP pin15`; `BOOT_BL702 -> GPIO28 pin31` | sub-MCU reset/download | boot low normal, high UART boot | E22 | A |
| Zigbee power | GPIO | `PA5` pin8 | `Zigbee_PWR_ON -> Q1/Q2 -> VCC_3V3_Z` | BL702 power | sequence before UART | E25 | A |

## 工程师提供资料索引

| 资料 | 上下文 | 用途 | 版本/一致性 |
| --- | --- | --- | --- |
| `doc/hard/` | 用户指定当前硬件资料 | 主证据来源 | 主原理图 Rev V0.1，2026-05-06。 |
| `doc/hard/LCD/` | 用户补充显示屏/触摸资料 | ST7102 init、Sitronix touch | 已纳入本报告；仍缺完整 panel timing sheet。 |
| 用户说明 `PDM_CLK=PA2, PDM_DAT1=PA4` | 硬件连接事实 | DMIC bring-up | 与原理图 P09/P11 一致。 |
| `.codex/changes.md` 既有记录 | 实板 bring-up 记录 | 验证 PDM DATA1 和输出音量配置 | 与当前源码绑定点一致。 |

## 缺失信息和澄清问题

| 缺失项 / 问题 | 为什么影响固件 | 当前最佳判断 | 可信度 | 建议确认方式 |
| --- | --- | --- | --- | --- |
| LCD 完整 timing：lane_count、lane_rate、porch/sync、reset pulse、power/backlight 时序 | 决定 DSI host 和 panel driver，不可靠 init table 猜完 | 480x480/60Hz，原理图两 lane，init table 有 1-lane 迹象 | B | 屏厂提供 datasheet；驱动参数可配置；示波器/DSI log 验证 D0/D1。 |
| Touch 实际 chip id、地址、firmware/CFG 文件 | 决定 probe、固件下载、坐标解析 | `0x55` 优先候选，RST active-low、IRQ falling | B | 上电 reset 后 I2C scan + 读 chip id；核对供应商固件包。 |
| CN7 小板完整原理图 | 决定两颗 PDM 麦 L/R 顺序、IR LED 电流和 pin 编号 | PDM clock/data 已闭合，小板左右通道未知 | B | 硬件提供小板图；逐个敲击/遮挡麦判断 ch0/ch1。 |
| AXS2033 SD/SHUT 实板电压 | 决定功放模式，小声/失真定位必须知道 | `PB25` high 可能进入 D 类防破音模式 1 | B | boot、idle、tone、stop 四个状态测 U7 pin1。 |
| BL702 UART 协议、波特率、固件升级方式 | 决定 Zigbee 子系统驱动与下载流程 | UART1 + reset/boot 可控 | B | 子系统 owner 提供协议；串口抓 BL702 boot log。 |
| TH FPC 外接传感器型号/地址 | 无型号无法写寄存器驱动 | 外接 I2C 温湿度模块 | C | BOM/丝印/小板图；I2C scan。 |
| TMI/EY404 部分电源器件完整 datasheet | 影响电源时序极限和异常解释 | 原理图足够指导 GPIO 上电顺序，极限参数缺失 | B/C | 若遇到电源异常，请硬件补资料并测 EN/VOUT。 |

## 假设 / 候选方案

| 假设 | 支持证据 | 风险 | 可信度 | 建议固件路径 | 验证 |
| --- | --- | --- | --- | --- | --- |
| 双 PDM 麦共享 DATA1，通过 L/R 边沿复用 | MSM261DDB021 支持 PDM L/R，CN7 只有 DATA1，DATA1 已验证 | 左右通道可能反 | B | board config 保持 `DMIC3/DMIC4`，通道顺序可配置 | 分别敲击左右麦，记录 ch0/ch1 peak。 |
| `PB25/MUTE` high 为 AXS2033 工作态 | SD >2.3V 进入 D 类防破音模式；SDK amp GPIO 可拉高 | RC/分压导致电压落在中间模式 | B | 命名为 amp SD/mode control；驱动记录目标电平 | 本地 tone 时测 U7 pin1。 |
| Touch 与 TH 共用 PB10/PB11 I2C | P10 TP I2C 经 R36/R37 到 TH_I2C | 地址冲突或 reset 干扰 | B | bus scan 后按地址注册；touch/TH reset 分离 | 接/断 CN5、CN6 比较 scan。 |
| LCD driver 首版必须暴露 lane/timing board config | 原理图两 lane，init table 疑似 1 lane | 写死错误 lane 会白屏且难定位 | B | `lane_count/lane_rate/porch/init_table` 配置化 | 上电后先看 DSI lane 活动再判断 panel。 |
| BL702 正常工作默认从 Flash boot，升级才拉高 GPIO28 | 原理图明确 0 Flash / 1 UART | 默认高会卡下载态 | A | 默认低，升级流程临时拉高并 reset | 抓 BL702 UART boot log。 |

## 固件同事快速上手

1. 先读：`关键器件矩阵`、`引脚接线图谱`、`避坑指南`、`音频输入/输出`、`Firmware Bring-Up Checklist`。
2. 先构建，不烧录硬件前确认 profile 和编译项：

```bash
cd /root/ameba-river
export AMEBA_SDK_ROOT=/root/ameba-rtos
source ./env.sh >/dev/null
python3 /root/ameba-rtos/ameba.py build -p
```

3. 当前 NAND 硬件需要手动进下载模式；烧录命令保持：

```bash
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000
```

4. 首次语音输入确认日志应包含：

```text
board array: Orvibo-RTL8730E-PDM ... primary=DMIC3 secondary=DMIC4
capture dmic pinmux applied: clk=PA2 data1=PA4
[AudioHal-I] ... set DMIC clock
```

5. 首次语音输出用本地 tone 起步：

```text
river playback tone 1000 1000 25
river playback status
river orvibo volume 100
```

6. 测到这些之前不要改软件方向：
   - 未测到 PDM_CLK/PDM_DAT1 波形前，不切 DATA0/2/3 或改 VAD/KWS。
   - 未测 U7 pin1、LINEOUT_LN/LP、SPK_OUTP/N 前，不继续调整上层音量策略。
   - 未拿到 LCD timing 前，不写死 DSI lane/timing。
   - 未确认 TH 型号前，不写固定温湿度传感器驱动。

## 避坑指南 / 注意事项 / 上手建议

| 主题 | 为什么容易错 | 证据 | 固件影响 | 可信度 | 第一检查 / Guardrail |
| --- | --- | --- | --- | --- | --- |
| SDK 默认音频脚不等于本板脚 | AmebaSmart usrcfg 是参考板默认 | E2-E4 | 无输入、无声 | A | 预处理必须看到 PA2/PA4/PB25 override。 |
| DATA1 要用 `DMIC3/DMIC4` | 容易按第一组 DMIC 误配成 DATA0 | E2/E3/E5 | 帧数增长但 peak 低/全零 | A | 日志必须出现 `DMIC3/DMIC4`。 |
| PDM 有帧不代表麦正确 | HAL 可给空帧或低噪声帧 | E5/E7 | 误判模型/阈值 | A | 先看 peak 和 PDM 波形。 |
| AXS2033 SD 是模式脚 | SD 电压区间影响 shutdown/AB/D | E9/E11 | 小声、失真、无声难定位 | A | 本地 tone 时测 U7 pin1。 |
| CN2 是 BTL 输出 | 两端都是功放输出，不是一端 GND | E12 | 错误测量或短路风险 | A | 差分测量 SPK_OUTP/N。 |
| 当前固件音量已最大 | 默认 100%，播放路径 1.00f | E6 | 继续调软件音量收益低 | A | 小声优先查硬件路径。 |
| AXS2033 增益由输入阻抗影响 | 原理图给 `Gain=400K/(Ri+5K)`，R56/R57=39K | E8/E10 | 最大音量也可能受模拟增益限制 | A | 算 Ri/增益，确认 LINEOUT 幅度和喇叭阻抗。 |
| LCD lane 有冲突风险 | 原理图两 lane，init table 疑似一 lane | E17/E20 | 白屏/花屏 | B | lane_count 做成配置，实测 D0/D1。 |
| Touch `0x55` 只是候选 | 示例地址不一定等于量产屏 | E19 | probe 失败 | B | reset 后 scan + 读 chip id。 |
| Touch 与 TH 可能共 I2C | P10/P13 网名关联 | E17/E27 | 地址冲突/scan 不稳定 | B | 所有设备 ACK 都入日志。 |
| NAND/NOR profile 不能混 | 板上是 SPI NAND | E14-E16 | 烧录失败或写错布局 | A | 用 `river_flash.py` 默认 NAND profile。 |
| BL702 boot strap 默认态关键 | GPIO28 高进 UART boot | E22 | 子系统不启动 | A | 正常运行保持 BOOT 低。 |
| 背光会掩盖 DSI 问题 | 白屏不等于 panel init 成功 | E17/E20 | 定位方向错误 | B | 先 power/reset/DSI，后开背光。 |
| IR 不是普通 LED | Q3 低侧驱动、5V IR LED，需 carrier/PWM | P08/P11 | GPIO toggle 不代表有效发射 | B | 先确认 PA3 mux、载波、电流限制。 |
| RF 参数不要随意改 | RF 匹配和法规边界不属于普通驱动调试 | E24/E29 | 认证/性能风险 | B | tx power/country 变更需硬件/RF review。 |

## 外部参考资料库

| 类别 | 来源 | URL / Path | 质量 | 用途 | 限制 |
| --- | --- | --- | --- | --- | --- |
| 官方/厂商资料 | Realtek RTL8730E Hardware Design Guide R2.3 | https://aiot.realmcu.com/en/_static/hardware/amebasmart/RTL8730E_Hardware_Design_Guide_R2.3.pdf | B | RTL8730E 电源/硬件设计交叉参考 | 不替代本板原理图。 |
| 厂商文档 | Realtek AmebaSmart Image Tool | https://ameba-doc-rtos-pro2-sdk.readthedocs-hosted.com/en/latest/application_note/04_IMAGE.html | B | 镜像/下载工具概念 | 本项目以 `/root/ameba-rtos` 和 wrapper 为准。 |
| 厂商文档 | Realtek AmebaSmart flash layout | https://ameba-doc-rtos-pro2-sdk.readthedocs-hosted.com/en/latest/application_note/08_FLASHLAYOUT.html | B | NAND/NOR layout 概念 | 需结合当前 NAND profile。 |
| 厂商文档 | Realtek audio optimization/app note | https://ameba-doc-rtos-pro2-sdk.readthedocs-hosted.com/en/latest/application_note/13_AUDIO.html | B | Audio HAL/系统音频背景 | 具体 pin 以本板 override 为准。 |
| 本地 datasheet | MEMSensing MSM261DDB021 | `doc/hard/1.01.070080 MSM261DDB021_Rev1.0.pdf` | A | PDM 麦参数、clock、电气特性 | 小板 L/R 接法仍需确认。 |
| 本地 datasheet | AXS2033 | `doc/hard/AXS2033.pdf` | A | 功放 SD 模式、增益、输出能力 | 本板实际音量还需测 LINEOUT/SD/负载。 |
| 厂商页面 | ChipSourceTek AXS2033 | https://en.chipsourcetek.com/Audio-Chip/204.html | B | 型号/器件类别在线交叉确认 | 详细参数以本地 PDF 为准。 |
| 厂商页面 | GigaDevice GD5F1GM7UE | https://www.gigadevice.com/product/flash/product-series/spi-nand-flash/standard-qspi-nand/gd5f1gm7ue | B | SPI NAND 型号/容量交叉确认 | 具体 package suffix 以原理图和采购为准。 |
| 厂商页面 | Toll Semi STI9287C | https://www.toll-semi.com/LEDbeiguangqudong/163.html | B | 背光驱动型号确认 | 需本地 datasheet 才能确认极限参数。 |
| 厂商/分销资料 | TMI3411 datasheet mirror | https://datasheet.lcsc.com/lcsc/2001060933_TMI-TMI3411_C478952.pdf | C | buck 器件资料补充 | 分销镜像，需硬件确认版本。 |
| 厂商/分销资料 | TMI6050-33 product page | https://jlcpcb.com/partdetail/TMI-TMI605033/C911079 | C | LDO 型号补充 | 分销页面，固件只作背景。 |
| 厂商 SDK | BouffaloLab SDK | https://github.com/bouffalolab/bouffalo_sdk | B | BL702 GPIO/UART/boot 参考 | 本板协议/固件不是该 SDK 自动确定。 |
| 开源实现 | Linux Sitronix ST1232 driver | https://codebrowser.dev/linux/linux/drivers/input/touchscreen/st1232.c.html | C | Touch probe/reset/IRQ 结构参考 | ST1232 不是 ST7102，不能证明地址/寄存器。 |
| 开源绑定 | Linux Sitronix device-tree binding | https://mjmwired.net/kernel/Documentation/devicetree/bindings/input/touchscreen/sitronix%2Cst1232.yaml | C | reset/irq/address 描述方式参考 | 不替代本地 Sitronix 包。 |
| 开源文档 | Zephyr DMIC API | https://docs.zephyrproject.org/latest/hardware/peripherals/audio/dmic.html | C | PDM/DMIC 配置概念参考 | 不适配 Ameba HAL API。 |
| 应用笔记 | ST AN5027 PDM digital microphones | https://www.st.com/resource/en/application_note/an5027-interfacing-pdm-digital-microphones-using-stm32-mcus-and-mpus-stmicroelectronics.pdf | C | PDM 波形/decimation 概念 | STM32 资料，不证明本板配置。 |
| 开源文档 | Linux MTD NAND driver API | https://www.kernel.org/doc/html/v4.20/driver-api/mtdnand.html | C | NAND bad block/ECC 概念 | Linux NAND API，不等于 Realtek boot ROM。 |
| 开源文档 | LVGL display porting | https://docs.lvgl.io/9.0/porting/display.html | C | 显示 buffer/flush 边界参考 | 不提供 panel timing。 |

## MCU / SoC Pin Map

| 功能 | Net | RTL8730E Pin | 外设/复用 | 默认/电气 | 固件动作 |
| --- | --- | --- | --- | --- | --- |
| PDM clock | `PDM_CLK` | `PA2` pin 5 | DMIC CLK | 外部无明显上下拉 | `_PA_2`。 |
| PDM data | `PDM_DAT1` | `PA4` pin 7 | DMIC DATA1 | R29 100R 到 CN7 | `AUDIO_DMIC3/DMIC4`。 |
| IR TX | `IR_TX` | `PA3` pin 6 | GPIO/PWM/IR candidate | Q3 NMOS 低侧，R32 下拉 | 先确认 mux/carrier。 |
| BL702 reset | `RST_BL702` | `PA13` pin 15 | GPIO | PU_CHIP 侧上拉 | 拉低 reset，释放启动。 |
| BL702 boot | `BOOT_BL702` | `PA15` pin 17 | GPIO | R47 20K 下拉 | 默认低，下载时临时高。 |
| Zigbee power | `Zigbee_PWR_ON` | `PA5` pin 8 | GPIO | Q1/Q2 控 3.3V_Z | UART 前先上电。 |
| UART1 to BL702 | `RTL8730_TX1/RX1` | `PB20/PB19` | UART1 | 3.3V_Z | TX1->BL702 RX，RX1<-BL702 TX。 |
| UART0/base | `RTL8730_TX0/RX0` | `PB24/PB23` | UART0 | R54/R55 pull-up | debug/base interface。 |
| Amp SD/mode | `MUTE` | `PB25` pin 96 | GPIO / Audio amp | R20/R21/C69/C71 | `_PB_25`，测 U7 pin1。 |
| LCD power | `LCD_PWR_ON` | `PB26` pin 97 | GPIO | U4 EN 相关 | DSI/touch 前上 LCD rail。 |
| LCD reset | `RST_LCD` | `PA14` pin 16 | GPIO | R34 pull-up | 按 panel timing。 |
| Backlight PWM | `LCD_BL_PWM` | `PA16` pin 18 | PWM/GPIO | R43 pull-down | panel init 后打开。 |
| Touch IRQ/RST | `TP_INT/TP_RST` | `PA9/PA10` | GPIO IRQ/reset | 4.7K pull-up | falling/active-low 候选。 |
| Touch/TH I2C | `TP_SDA2/SCL2`, `TH_I2C_SDA/SCL` | `PB10/PB11` | I2C | 4.7K pull-up + 100R series | scan 后绑定。 |
| SPI NAND | `FLASH_QSPI_*` | `PC1-PC6` | QSPI NAND | 3.3V | NAND profile。 |
| USB | `USB_DP/DN` | `HSDP/HSDM` pins 78/79 | USB | CN1 | 下载/调试。 |
| Reset | `CHIP_EN` | pin 71 | SoC enable | R19 pull-up, SW2 to GND | 手动 reset。 |

## 外设块说明

### Power / Reset / Boot

- P05 显示 `U3 TMI3411/2A` buck、`U1/U4 TMI6050-33` LDO、`U2 EY404-CF42F1` power key、`Q1 IRLML6401` + `Q2 S8050` Zigbee power switch。
- 固件可直接影响的是 `LCD_PWR_ON`、`Zigbee_PWR_ON`、`RST_BL702`、`BOOT_BL702`、`CHIP_EN` reset 行为。
- `CHIP_EN` 是 SoC enable/reset，不等于全板断电；LCD/Zigbee 子电源可能保留状态，driver 初始化要主动复位外设。

### SPI NAND / 下载

- `U11 GD5F1GM7UEYIGR`：WSON8 SPI NAND，原理图标注 128MB。
- 引脚：`/CS pin1`、`CLK pin6`、`IO0 pin5`、`IO1 pin2`、`IO2/WP pin3`、`IO3/HOLD pin7`、`VDD pin8`。
- 烧录：当前 NAND 硬件需要手动进入下载模式；成功 build 后使用 `tools/river_flash.py`，不要换 NOR profile。
- 失败签名：`Flash type mismatch` 优先查 profile；`Enter download mode fail` 优先查手动下载模式。

### PDM Microphone / Audio Capture

- 硬件路径：CN7 小板 PDM mic -> `PDM_CLK/PDM_DAT1` -> RTL8730E `PA2/PA4` -> Audio HAL DATA1 -> `DMIC3/DMIC4` -> capture service。
- MSM261DDB021 资料要点：PDM digital output、VDD 1.6-3.6V、Standard Performance clock 1.1-4.8MHz、典型测试 2.4MHz、L/R 由 DATA 边沿选择。
- 项目绑定：
  - [river_audio_hw_overrides.h](/root/ameba-river/include/river/river_audio_hw_overrides.h)
  - [river_voice_board.c](/root/ameba-river/components/river_voice/river_voice_board.c)
  - [river_voice_capture.c](/root/ameba-river/components/river_voice/river_voice_capture.c)
- 验证顺序：日志 -> `PDM_CLK` 频率 -> `PDM_DAT1` 活动 -> ch0/ch1 peak -> VAD/KWS。

### Speaker / AXS2033 / Audio Output

- 硬件路径：Audio HAL speaker/line-out -> U14C `LINEOUTLN/LP` pins 51/52 -> C76/C153 + R56/R57 -> U7 AXS2033 `IN-/IN+` -> U7 BTL output -> FB1/FB2 -> `CN2` speaker。
- AXS2033 关键点：
  - VDD 来自 `VCC_5V_IN`。
  - `SD/SHUT` 接 `MUTE/PB25`，电压区间决定 shutdown、AB、D 类模式。
  - 原理图给出 `Gain=400K/(Ri+5K)`，本板输入电阻 R56/R57 为 39K；模拟增益不是软件音量能完全补偿的。
  - `SPK_OUTP/SPK_OUTN` 是 BTL 差分输出。
- 当前软件音量状态：
  - `river_orvibo_mcp_volume.c` 默认 `100U`。
  - `river_orvibo_audio_service.c` playback volume `1.00f`。
  - `river_diag_cmd.c` tone volume `1.00f`。
  - `river_voice_echo.c` echo playback `1.00f`。
- 小声排查顺序：U7 VDD -> U7 pin1 SD 电压 -> LINEOUT_LN/LP 差分幅度 -> SPK_OUTP/N 差分幅度 -> 喇叭阻抗/额定功率 -> R56/R57/C76/C153。

### LCD / Touch / Backlight

- CN6 pin map：
  - power/GND：1/3/5 `VCC_3V3_LCD`，2/4/6/8/11/14/17/23 GND。
  - panel reset：7 `RST`。
  - DSI：9/10 D1P/D1N，12/13 CLKP/CLKN，15/16 D0P/D0N。
  - touch：18 `VCC_TP`，19 `I2C_SCL`，20 `I2C_SDA`，21 `TP_INT`，22 `TP_RST`。
  - backlight：24 `LEDK`，25 `LEDA`。
- U10 STI9287C：VIN=5V，EN=`LCD_BL_PWM`，SW/L11/D1 boost 到 LEDA/LEDK。不要用背光亮灭判断 DSI 是否正常。
- ST7102 init table 可做初版命令表，但 lane/timing 仍缺完整 datasheet。驱动应把 lane_count、lane_rate、porch、reset delay、backlight delay 配置化。

### BL702 / Zigbee 子系统

- 电源：`Zigbee_PWR_ON` 通过 Q1/Q2 控制 `VCC_3V3_Z`。
- UART：RTL8730E `PB20/TX1 -> BL702 GPIO15/UART_RX`，`PB19/RX1 <- BL702 GPIO14/UART_TX`。
- Reset：`RST_BL702 -> PU_CHIP`。
- Boot：`BOOT_BL702 -> GPIO28`，原理图明确 0 Flash / 1 UART。
- 固件建议：正常工作默认 BOOT 低；升级/恢复流程显式拉高 BOOT 后复位；协议/波特率不要猜，先抓 boot log。

### RF / Wi-Fi / BT

- P09 包含 `U8 FLT18D24255171D-3271A`、onboard antenna `ANT4`、IPEX `T2` 和 RF 匹配网络。
- 固件只应配置合法 country/band/tx power；RF 匹配、电容电感、天线切换和认证参数属于硬件/RF review 范围。

### TH 温湿度接口

- CN5 只给出 `VCC_3V3`、`TH_I2C_SDA`、`TH_I2C_SCL` 和 GND/ESD。
- 缺传感器型号、地址、寄存器表。当前只能做 I2C scan、上拉确认、硬件澄清。

## Firmware Bring-Up Checklist

| 阶段 | 检查项 | 预期 | 失败时先查 |
| --- | --- | --- | --- |
| Build | `python3 /root/ameba-rtos/ameba.py build -p` | 编译通过 | SDK root、override include、dirty source。 |
| Flash | `tools/river_flash.py` | NAND profile / 128MB | 下载模式、profile、串口。 |
| PDM | boot log + PDM_CLK/DAT1 | `DMIC3/DMIC4`，peak 非零 | PA2/PA4 pinmux、DATA1、CN7。 |
| Playback | local tone | U7 SD 工作态，CN2 差分输出 | PB25、AXS2033 VDD、LINEOUT、speaker。 |
| LCD | power/reset/DSI before backlight | DSI lanes 活动，reset timing 正确 | lane_count、init delay、LCD rail。 |
| Touch | reset 后 I2C scan | 候选 `0x55` 或实际 ACK | power/reset/IRQ/pull-up。 |
| NAND | boot/download logs | NAND device matched | profile/layout。 |
| BL702 | power + reset + UART log | 从 Flash boot | BOOT strap、3.3V_Z、baud/protocol。 |
| TH | I2C scan | 找到外接 sensor ACK | 型号、地址、FPC。 |

## 风险和未知项

| 风险 | 影响 | 可信度 | 解决方式 |
| --- | --- | --- | --- |
| LCD lane/timing 不闭合 | 白屏/花屏，误判 backlight | B | 补 panel datasheet，驱动配置化，实测 DSI。 |
| AXS2033 SD 电压未测 | 小声/无声/失真定位不可靠 | A | 加日志只够看 GPIO，必须测 U7 pin1。 |
| CN7 小板资料缺失 | 双麦左右通道和 IR 电流不确定 | B | 补小板原理图，实测 ch0/ch1。 |
| Touch 地址/firmware 不确定 | probe 失败或坐标错误 | B | scan + chip id + 供应商配置。 |
| BL702 协议/升级流程缺失 | 子系统 driver 无法闭环 | B | 抓 UART log，找协议 owner。 |
| TH 型号缺失 | 不能写可靠传感器驱动 | A | BOM/丝印/scan。 |

## Source Notes

- 高可信本地证据：主原理图、AXS2033 PDF、MSM261DDB021 PDF、Sitronix 本地资料包、SDK 源码、项目源码。
- 外部资料用途：确认器件类别、查找官方/开源驱动边界、补充 bring-up 方法。外部资料不能覆盖本板原理图和实测。
- 对 PDF/OCR 文本的处理：raw value 只作为线索，关键型号通过原理图上下文、管脚名、封装、周边电路和 datasheet 归一化。

## Scope Audit

- 本报告未把产品交互、业务播放/录音路径或网络策略写成硬件结论。
- 音频描述从 Audio HAL/SoC line-out 和 GPIO 到 AXS2033、CN2、测点为止。
- 显示描述从 MIPI DSI/I2C/GPIO 到 CN6/U10/panel/touch 为止。
- 所有开发建议停留在 BSP/HAL/driver、诊断命令或板级测量。
