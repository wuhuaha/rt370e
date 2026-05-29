# RTL8730E 4 寸板硬件固件说明书

日期：2026-05-29
对象：Orvibo RTL8730E 4 寸板，主原理图 `RTOS_4InchLCD` / `MixPad4_RTL8730`，Rev `V0.1`，日期 `2026-05-06`。
目的：把 `doc/hard/` 中的原理图、丝印图、器件资料和本地 SDK/项目绑定点整理成固件同事可执行的 BSP/HAL/驱动 handoff。

## Summary

- 数字麦克风使用 PDM/DMIC：`PDM_CLK -> RTL8730E PA2`，`PDM_DAT1 -> RTL8730E PA4`。在 AmebaSmart Audio HAL 中应使用 DATA1 对应的 `AUDIO_DMIC3/DMIC4`，项目已通过 AP Audio HAL override 固定 `AUDIO_HW_DMIC_CLK_PIN=_PA_2`、`AUDIO_HW_DMIC_DATA1_PIN=_PA_4`。
- 音频输出是内部 codec line-out 差分输出到 AXS2033 功放：`AMEBA_AUDIO_DEVICE_SPEAKER -> APP_LINE_OUT -> LINEOUT_LN/LP -> C76/C153 + R56/R57 -> AXS2033 -> CN2 speaker`。功放 `SHUT/SD` 控制网名为 `MUTE`，接 RTL8730E `PB25`；SDK 默认 `_PB_19` 不匹配，项目已覆盖为 `_PB_25`。
- 启动 Flash 是 GigaDevice `GD5F1GM7UEYIGR` SPI NAND，原理图标注 128MB，项目烧录 wrapper 默认使用 NAND profile。当前硬件不要使用 NOR profile。
- LCD 走 MIPI DSI，两条 data lane 和 clock lane 均已路由到 CN6；本地 ST7102 init table 写 `SSD_LANE(1,0)`，与原理图两条 data lane 路由存在待确认项。
- 触摸走 Sitronix ST71xx/ST7102 风格的 I2C：`PB10/PB11 -> TP_SDA2/TP_SCL2`，候选地址 `0x55` 来自供应商源码包，仍需上电 scan 和 reset 后读 chip id。
- BL702 Zigbee 子系统由 RTL8730E 通过电源、复位、boot strap 和 UART1 管理。`BOOT_BL702` 对应 BL702 GPIO28，原理图标注 `0 boot from Flash, 1 boot from Uart`，固件默认必须避免拉高进入下载模式。
- 温湿度 FPC 只给出 `TH_I2C_SDA/SCL` 和 3.3V，没有传感器型号、地址和寄存器资料；该外设当前只能做总线扫描和硬件澄清。

## Report Boundary

- 面向读者：BSP、HAL、外设驱动、pinmux、启动下载、电源/复位时序和板级验证的软件同事。
- 范围内：SoC 引脚、外设实例、驱动配置、SDK 默认值差异、板级电路路径、测试命令、预期底层日志、示波器/逻辑分析仪测点和失败排查树。
- 范围外：产品交互、网络服务策略、应用层播放/录音策略、会话状态机、UI 行为。若运行日志包含这些名字，只能作为硬件路径已工作的背景证据，结论需落回 BSP/HAL/driver。

## Evidence Index

| ID | Claim | Source | Type | Confidence | Notes |
| --- | --- | --- | --- | --- | --- |
| E1 | `PDM_CLK` 接 RTL8730E `PA2`，`PDM_DAT1` 接 `PA4` | `doc/hard/RTL8730 4寸SCH.pdf` P09/P11；用户明确说明 | schematic + user fact | A | P09 标出 `PA4/PDM_DAT1`，P11 标出 `PA2/PDM_CLK`。 |
| E2 | AmebaSmart `DMIC3/DMIC4` 使用 `AUDIO_HW_DMIC_DATA1_PIN`，DMIC clock 使用 `AUDIO_HW_DMIC_CLK_PIN` | `/root/ameba-rtos/component/audio/audio_hal/amebasmart/ameba_audio_stream_capture.c:352` | SDK | A | DATA0/1/2/3 分别覆盖 DMIC1/2、3/4、5/6、7/8。 |
| E3 | SDK 默认 DMIC pins 和 amplifier pin 是 reference-board 配置，不匹配本板 | `/root/ameba-rtos/component/soc/usrcfg/amebasmart/include/ameba_audio_hw_usrcfg.h:19`、`:84` | SDK | A | 默认 `_PB_22/_PB_18` 和 `_PB_19` 均需要项目覆盖。 |
| E4 | 项目已覆盖 PDM 和功放 pin | `include/river/river_audio_hw_overrides.h`、`CMakeLists.txt` | code | A | AP `audio_hal_${c_CURRENT_IMAGE}` 编译时 `-include` 项目覆盖头。 |
| E5 | PA2/PA4 DATA1 已实板验证能采到有效语音 | `.codex/changes.md` Step H.xiaozhi-client.69 | runtime | A | 采集/预处理 peak 非零，VAD/KWS 底层链路有输入；不把应用层行为作为硬件结论。 |
| E6 | MSM261DDB021 是 PDM 数字麦，VDD 1.6-3.6V，标准性能 clock 条件 2.4MHz，L/R 选择 DATA 边沿 | `doc/hard/1.01.070080 MSM261DDB021_Rev1.0.pdf` p4/p9/p10/p14 | datasheet | A | pin1 DATA、pin2 L/R、pin4 CLK、pin5 VDD。 |
| E7 | AXS2033 是 3.1W 单声道 AB/D 类功放，VDD 2.5-5.5V，SD 电压区间控制 shutdown/AB/D 模式 | `doc/hard/AXS2033.pdf` p1/p3/p8；ChipSourceTek AXS2033 product page | datasheet + vendor web | A/B | 本地 PDF 为主证据，网页只作型号交叉确认。 |
| E8 | `LINEOUTLN/LP` 接 AXS2033 `IN-/IN+`，`SPKP/SPKN` 到 CN2 | `doc/hard/RTL8730 4寸SCH.pdf` P08 render/text | schematic | A | CN2 是 BTL/differential speaker output。 |
| E9 | `MUTE` 接 RTL8730E `PB25`，并进入 AXS2033 `SHUT/SD` | `doc/hard/RTL8730 4寸SCH.pdf` P08/P11 | schematic | A | `PB25 -> MUTE -> R20/R21/C69/C71 -> U7 pin1`。 |
| E10 | `GD5F1GM7UEYIGR` 为板上 SPI NAND，原理图标注 128MB | `doc/hard/RTL8730 4寸SCH.pdf` P11；GigaDevice product page | schematic + vendor web | A/B | GigaDevice page 标注 1Gb、2.7-3.6V、x1/x2/x4、WSON8。 |
| E11 | Realtek 官方工具文档支持 RTL8730E NAND/NOR 下载 profile 区分 | Ameba IoT Image Tool docs | vendor docs | B | 项目 wrapper 在此基础上固定当前硬件 NAND profile。 |
| E12 | LCD CN6 路由 MIPI DSI D0/D1/CLK、reset、touch I2C/INT/RST 和 backlight | `doc/hard/RTL8730 4寸SCH.pdf` P10 | schematic | A | CN6 `A113F-15025WUA-R01`。 |
| E13 | ST7102 init table 标注 `SSD_LANE(1,0)`、TE `0x35`、sleep out `0x11` 后 250ms、display on `0x29` 后 200ms | `doc/hard/LCD/ST7102_480480_60hz_0109_tp 3ms .txt` | vendor init table | B | init table 未包含完整 porch/lane rate/reset timing。 |
| E14 | Sitronix 手册说明 ST7123/ST7121P/ST7123P/ST7102 等支持 I2C/SPI，I2C 示例 400kHz、地址 `0x55`，IRQ falling，RST active-low | `doc/hard/LCD/Sitronix Touch Driver 移植手册.pdf`、`sample.dtsi` | vendor guide/source | B | 示例偏 Linux DTS，Ameba 侧需迁移为 board config。 |
| E15 | BL702 支持 BLE/Zigbee/Thread，内置 RISC-V CPU；本板 UART1 连接 BL702 GPIO14/15 | `doc/hard/RTL8730 4寸SCH.pdf` P12；Bouffalo Lab product page | schematic + vendor web | A/B | 协议和波特率仍需 BL702 固件 owner 提供。 |
| E16 | BL702 GPIO28 boot strap：0 从 Flash，1 从 UART | `doc/hard/RTL8730 4寸SCH.pdf` P12 | schematic | A | 固件默认应保持 `BOOT_BL702` 低。 |
| E17 | TH FPC 只暴露 3.3V、`TH_I2C_SDA/SCL` | `doc/hard/RTL8730 4寸SCH.pdf` P13 | schematic | A | 未提供传感器型号/地址。 |
| E18 | RTL8730E Hardware Design Guide 说明 RTL8730E 供电结构和 DCDC_AUD/DCDC_CORE/DCDC_MEM 角色 | Realtek RTL8730E Hardware Design Guide R2.3 | vendor docs | B | 作为 SoC 供电结构交叉参考，不替代本板原理图。 |

## Artifact Inventory

| Artifact | Path / URL | Pages / Sheets Used | Extraction Method | Notes |
| --- | --- | --- | --- | --- |
| 主原理图 | `doc/hard/RTL8730 4寸SCH.pdf` | P05-P13 | PyMuPDF text + page render | 有文本层；关键页已视觉复核。 |
| 丝印图 | `doc/hard/RTL8730_4寸_丝印图.pdf` | p1-p2 | page render | 无文本层，仅辅助连接器/测试点定位。 |
| PDM 麦资料 | `doc/hard/1.01.070080 MSM261DDB021_Rev1.0.pdf` | p4/p9/p10/p14 | text extraction | Rev 1.0，本地资料优先级高于网页同类器件。 |
| 功放资料 | `doc/hard/AXS2033.pdf` | p1-p8 | text extraction + render | 本地 AXS2033 datasheet，SD 电压区间和 gain 公式来自该文件。 |
| LCD init table | `doc/hard/LCD/ST7102_480480_60hz_0109_tp 3ms .txt` | full file | direct text | 480x480/60Hz 相关初始依据，但缺完整 timing sheet。 |
| Sitronix touch 手册 | `doc/hard/LCD/Sitronix Touch Driver 移植手册.pdf` | p1/p4-p11/p18 | text extraction + render | 适用 ST71xx/ST7102，提供 bus/reset/IRQ/firmware loading 参考。 |
| Sitronix source bundle | `doc/hard/LCD/ST_TDDI_TPDriver_v45.00.260402/` | `sample.dtsi`、porting guide、`sitronix_ts*.c/h` | source review | 提供 I2C `0x55` 候选、probe 日志和 self-test 入口。 |
| SDK | `/root/ameba-rtos` | audio HAL、usrcfg、fwlib headers、flash tools | source review | 用于确认 HAL 默认值和本项目需要覆盖的位置。 |
| Project firmware | `/root/ameba-river` | `include/river/`、`components/`、`tools/` | source review | 用于列出已落地的 board override 和诊断命令。 |
| External official references | Realtek/GigaDevice/Bouffalo/ChipSourceTek pages | product/docs pages | web lookup | 用于型号/能力交叉确认；配置仍以本地原理图和 SDK 为准。 |

## Engineer-Supplied References

| Material | Path / URL | Provider / Context | Used For | Version / Revision Check |
| --- | --- | --- | --- | --- |
| 当前硬件资料目录 | `doc/hard/` | 用户指定 | 全板硬件输入 | 主原理图 Rev `V0.1`，日期 `2026-05-06`。 |
| LCD/触摸资料 | `doc/hard/LCD/` | 用户补充 | ST7102 panel init、Sitronix touch 迁移参考 | `ST_TDDI_TPDriver_v45.00.260402` 已纳入。 |
| PDM 连接事实 | 用户说明：`PDM_CLK 接 PA2, PDM_DAT1 接 PA4` | 用户/硬件上下文 | 数字麦 pin map | 与主原理图 P09/P11 一致。 |
| 已验证运行记录 | `.codex/changes.md` Step H.xiaozhi-client.69 | 项目记录 | PA2/PA4 DATA1 实板采集验证 | 与当前固件绑定点一致。 |

## Missing Inputs And Clarifications

| Missing Item / Question | Why It Matters To Firmware | Current Best Guess | Confidence | Recommended Owner / Check |
| --- | --- | --- | --- | --- |
| LCD 完整 timing sheet：lane_count、lane rate、porch/sync、reset pulse、power/backlight 时序 | 决定 DSI host 配置和白屏/花屏定位 | ST7102 480x480/60Hz，init table 可做初始命令表；lane 数不能静默写死 | B | 屏厂/硬件提供 datasheet；示波器/DSI log 验证 D0/D1 是否均使用。 |
| Touch 实际地址、chip id、firmware/CFG 文件、flash boot 还是 host download | 决定 probe、固件加载和触摸坐标输出 | `0x55` 是优先候选；INT falling、RST active-low | B | 上电 reset 后 I2C scan，读 SFR/chip id；供应商确认固件文件。 |
| TH FPC 外接传感器型号和地址 | 没有型号无法实现寄存器驱动 | 外接温湿度 I2C 模块 | C | 硬件提供 BOM/丝印；固件做 bus scan。 |
| CN7 MIC/IR 小板完整原理图 | 决定 PDM L/R 槽位、双麦通道顺序、IR LED 电流限制 | 两颗 PDM 麦共享 DATA1，通过 L/R 边沿复用 | B | 硬件提供小板原理图；逻辑分析 PDM DATA/CLK，近场单麦测试。 |
| AXS2033 `SHUT/SD` 实板电压 | SD 不是简单 enable，电压区间决定 shutdown/AB/D/防破音模式 | Audio HAL GPIO high 可能让 U7 进入 D 类防破音模式 | B | 测 U7 pin1 在 boot、idle、tone playback、stop 的电压。 |
| BL702 固件协议、UART 波特率、升级流程 | 决定 Zigbee 子系统驱动、复位和下载流程 | RTL8730 UART1 连 BL702 GPIO14/15，默认从 Flash boot | B | Zigbee 固件 owner 提供协议；串口抓 BL702 boot log。 |
| Realtek 完整 pinmux/reference manual | 扩展 LCD/IR/PWM/UART 时需核验复用限制 | 当前 PDM/audio/flash 已可由 SDK 和原理图闭合 | B | 若开发 LCD/IR/Zigbee 控制，补齐官方 pinmux 表。 |

## Hypotheses / Candidate Solutions

| Hypothesis | Evidence For | Evidence Against / Risk | Confidence | Suggested Firmware Path | Validation Step |
| --- | --- | --- | --- | --- | --- |
| 双 PDM 麦通过同一 DATA1 的左右边沿复用 | MSM261DDB021 有 L/R 边沿选择；CN7 只有 `PDM_DAT1`；DATA1 已实板验证有效 | 小板 L/R 接法未知，左右声道可能反 | B | 保持 `DMIC3/DMIC4`，通道顺序留成可配置 | 分别遮挡/轻敲两颗麦，记录 ch0/ch1 peak。 |
| `PB25/MUTE` high 对应 AXS2033 工作态 | AXS2033 SD >2.3V 为 D 类防破音模式；SDK dummy amp enable 会拉 GPIO | 分压/RC 可能让 SD 落在中间区间 | B | BSP/HAL 层命名为 amp SD/mode control，不只叫 mute | 用 `river playback tone 1000 1000 25` 时测 U7 pin1。 |
| Touch 与 TH 可能共用 `PB10/PB11` I2C 总线 | P10 `TP_SDA2/SCL2` 经 R36/R37 接 `TH_I2C_SDA/SCL` | 地址冲突或 reset 时序可能影响 scan | B | 先做总线 scan，再按地址注册 touch/TH | 分别连接/断开 LCD、TH FPC 比较 scan 结果。 |
| LCD driver 首版应把 lane/timing 作为 board config | 原理图两 lane，init table 写 1 lane | 固件写死错误 lane 会导致白屏且难定位 | B | panel config 暴露 `lane_count`、`lane_rate`、porch 和 init table | 开背光前先确认 DSI LP/HS、reset 和 TE。 |
| BL702 默认从 Flash 启动，升级时才拉高 boot strap | 原理图直接标注 GPIO28 boot mode | 若默认高会卡在 UART boot | A | `BOOT_BL702` 默认低或输入下拉；升级流程临时拉高 | 拉低 boot 后复位，观察 BL702 是否正常启动。 |

## MCU / SoC Pin Map

| Function | Net | MCU Pin | Alt Function / Instance | Voltage Domain | Pull / Default | Evidence | Firmware Action |
| --- | --- | --- | --- | --- | --- | --- | --- |
| PDM clock | `PDM_CLK` | `PA2` pin 5 | DMIC CLK | 3.3V digital/audio | 未见外部上下拉 | E1/E2 | `AUDIO_HW_DMIC_CLK_PIN=_PA_2`；日志应为 `clk=PA2 data1=PA4`。 |
| PDM data | `PDM_DAT1` | `PA4` pin 7 | DMIC DATA1 | 3.3V digital | CN7 串 R29 100R | E1/E2/E5 | 使用 `AUDIO_DMIC3/DMIC4`。 |
| IR TX | `IR_TX` | `PA3` pin 6 | GPIO/PWM candidate | 3.3V control, IR LED on 5V rail | Q3 NMOS low-side，R32 10K pull-down | P08/P11 | 38kHz 载波优先 PWM；先确认 PA3 PWM mux 和 LED 电流。 |
| BL702 reset | `RST_BL702` | `PA13` pin 15 | GPIO | 3.3V_Z | R52 100R，PU_CHIP 侧 R53 10K pull-up | P11/P12 | 默认释放；复位流程拉低再释放。 |
| BL702 boot | `BOOT_BL702` | `PA15` pin 17 | GPIO | 3.3V_Z | R47 20K pull-down，R46 100R，TP31/R48 | P11/P12 | 默认低；下载/恢复流程临时拉高。 |
| Zigbee power | `Zigbee_PWR_ON` | `PA5` pin 8 | GPIO | 3.3V control | 电源页 Q1/Q2 switch | P05/P09 | 管理 `VCC_3V3_Z` 上电；和 BL702 reset/boot 时序绑定。 |
| UART1 to BL702 | `RTL8730_TX1/RX1` | `PB20/PB19` pins 67/66 | UART1 | 3.3V_Z | BL702 侧 R49/R50/R51 | P11/P12 | RTL8730 TX1 -> BL702 RX1；RTL8730 RX1 <- BL702 TX1。 |
| UART0/base | `RTL8730_TX0/RX0` | `PB24/PB23` pins 73/72 | UART0 | 3.3V_Z | R54/R55 10K pull-up | P05/P11 | CN1 debug/base interface，避免和 console 使用冲突。 |
| Amp SD/mode | `MUTE` | `PB25` pin 96 | GPIO / Audio HAL amplifier pin | 3.3V control to U7 SD | R20 100R，R21 10K，C69/C71 | E7/E9 | `AUDIO_HW_AMPLIFIER_PIN=_PB_25`；实测 U7 pin1。 |
| LCD power enable | `LCD_PWR_ON` | `PB26` pin 97 | GPIO | 3.3V control | U4 EN, R12/R14/C16 | P05/P11 | DSI/touch 初始化前上 `VCC_3V3_LCD`。 |
| LCD reset | `RST_LCD` | `PA14` pin 16 | GPIO | 3.3V_LCD | R34 100K pull-up，R35 1K | P10 | 按 panel 时序拉低/释放；缺精确 pulse。 |
| Backlight PWM | `LCD_BL_PWM` | `PA16` pin 18 | PWM/GPIO | 3.3V control, 5V boost | R42 100R，R43 10K pull-down | P10 | panel init 完成后再开 PWM。 |
| Touch IRQ | `TP_INT` | `PA9` pin 12 | GPIO IRQ | 3.3V_LCD | R40 4.7K pull-up | P10/E14 | falling/active-low 候选；实测确认。 |
| Touch reset | `TP_RST` | `PA10` pin 13 | GPIO | 3.3V_LCD | R39 4.7K pull-up | P10/E14 | active-low 候选；reset 后读 chip id。 |
| Touch/TH I2C | `TP_SDA2/SCL2`, `TH_I2C_SDA/SCL` | `PB10/PB11` pins 57/58 | I2C | 3.3V_LCD / 3.3V | R38/R39/R40/R41 4.7K, R36/R37 100R | P10/P13/E14 | 先 100/400kHz scan；地址可配置。 |
| SPI NAND | `FLASH_QSPI_*` | `PC1-PC6` | QSPI NAND | 3.3V | U11 C102 1uF | P11/E10 | 使用 NAND boot/download profile。 |
| USB | `USB_DP/DN` | `HSDP/HSDM` pins 78/79 | USB | USB PHY | CN1 to base/debug | P05/P07 | 用于下载/调试时按 Realtek 工具要求操作。 |
| Reset / chip enable | `CHIP_EN` | pin 71 | chip enable | 3.3V | R19 10K pull-up, SW2 to GND | P07 | 手动 reset，不等同断电。 |

## Peripheral Blocks

### Power, Power Key, Reset

- Components：`U1/U4 TMI6050-33` 3.3V LDO，`U3 TMI3411/2A` buck，`U2 EY404-CF42F1` power key，`Q1/Q2` Zigbee power switch。
- Rails：
  - `VCC_5V_IN`：CN1/外部输入、AXS2033、backlight boost。
  - `VCC_3V3_HOLD`：RTL8730E 主 3.3V 保持供电。
  - `VCC_3V3_LCD`：LCD/touch 供电，由 `LCD_PWR_ON` 控制 U4。
  - `VCC_3V3_Z`：BL702 Zigbee 供电，由 `Zigbee_PWR_ON` 经 Q1/Q2 控制。
  - RTL8730E 内部 `VDD_0V9`、`VDDA_1V8`、`VDD_1V8` 由 P06 DCDC/电感网络提供。
- Firmware boundary：固件可控制 LCD/Zigbee 子电源 enable，但不能假设 `CHIP_EN` reset 会清空所有外设电源状态。
- Validation：
  - 上电测 `VCC_3V3_HOLD`、`VDD_0V9`、`VDDA_1V8`、`VDD_1V8`。
  - 拉高/拉低 `LCD_PWR_ON` 测 CN6 `VCCIO/VCC3/VCC5/VCC_TP`。
  - 拉高/拉低 `Zigbee_PWR_ON` 测 `VCC_3V3_Z`。

### SPI NAND / Boot / Flashing

- Component：`U11 GD5F1GM7UEYIGR/Nand Flash128MB/WSON8/GD`。
- Nets：`PC6 -> /CS`，`PC2 -> CLK`，`PC3 -> IO0/DI`，`PC5 -> IO1/DO`，`PC4 -> IO2/WP`，`PC1 -> IO3/HOLD`。
- Firmware/download action：
  - 当前 wrapper 默认 NAND：

```bash
cd /root/ameba-river
export AMEBA_SDK_ROOT=/root/ameba-rtos
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000
```

  - 若显式写法：`python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nand`。
  - 旧 NOR 硬件才使用 `-m nor`。
- Expected tool facts：当前板应识别 `MemoryType: NAND`、`GD5F1GM7U`、`1Gb/128MB`。
- Failure signature：`Flash type mismatch: Device: 2 / Device Profile: 1` 表示实际 NAND 被 NOR profile 烧录。

### PDM Microphone / Audio Capture

- Hardware path：CN7 `PDM_CLK/PDM_DAT1/VCC_3V3/GND` -> RTL8730E `PA2/PA4` -> Audio HAL DMIC DATA1 -> `AUDIO_DMIC3/DMIC4` -> `AudioRecord`。
- Mic device facts：MSM261DDB021 是 bottom-ported PDM digital output MEMS microphone，pin1 DATA、pin2 L/R、pin4 CLK、pin5 VDD；VDD 1.6-3.6V；标准性能模式 datasheet 条件 `fCLOCK=2.4MHz`。
- SDK facts：
  - `AUDIO_DMIC3/DMIC4` 配置 `AUDIO_HW_DMIC_DATA1_PIN`。
  - DMIC clock 可由 `AUDIO_HW_DMIC_CLK_PIN` 配置，AmebaSmart SDK 低层会打印 `set DMIC clock`。
  - `ameba_audio.h` 列出 DMIC clock selector：5MHz、2.5MHz、1.25MHz、625kHz、312.5kHz、769.2kHz 等。
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

- Validation：
  - 静音下 peak 应接近底噪，说话/敲击时 capture/preproc peak 明显上升。
  - 单麦近场测试确认 ch0/ch1 物理位置和 L/R 槽位。
  - 逻辑分析仪测 `PDM_CLK` 和 `PDM_DAT1`，确认 clock 频率和 data 活动。

### Speaker / AXS2033 / Audio Output

- Purpose：把 RTL8730E 内部 codec line-out 差分模拟输出放大到 CN2 speaker。
- Components：`U7 AXS2033/QFN8/AXS`，CN2 `WTB0818A-M02-00R`，TVS7/TVS8，FB1/FB2，输入 AC coupling 和 gain 电阻网络。
- Complete hardware path：
  - HAL-visible output：`AMEBA_AUDIO_DEVICE_SPEAKER`。
  - SDK route：AmebaSmart `ameba_audio_stream_render.c` 中 speaker device 进入 `APP_LINE_OUT`，line-out 设置为 `DIFF` 并 unmute。
  - SoC pins：`LINEOUTLN` pin51 -> `LINEOUT_LN`，`LINEOUTLP` pin52 -> `LINEOUT_LP`；右声道 `LINEOUTRP/RN` 在本页未接入功放。
  - Input network：`LINEOUT_LN -> C76 0.1uF -> R56 39K -> U7 IN-`；`LINEOUT_LP -> C153 0.1uF -> R57 39K -> U7 IN+`。
  - Amplifier：AXS2033 `OUTP/OUTN` -> `SPKP/SPKN` -> FB1/FB2 -> TVS -> CN2 speaker。
  - Control：`PB25 -> MUTE -> R20 100R -> U7 SHUT/SD`，R21/C69/C71 形成默认/滤波网络。
  - Power：U7 `VDD` 接 `VCC_5V_IN`；本板标注 `VDD=5V, RL=8R, Output PWR=1.8W`。
  - Loopback/reference candidate：`SPK_OUTP/N -> R22/R26 20K -> RC/分压 -> MIC5_P/N`。这只是硬件回采网络线索，是否用于软件参考通道需另行配置验证。
- AXS2033 electrical notes：
  - VDD 2.5-5.5V。
  - D 类 5V/4ohm/10% THD+N 典型 3.1W，5V/8ohm/10% 典型 1.8W。
  - SD/SHUT：`<0.35V` shutdown，`1.2-1.5V` AB，`1.7-2.1V` D 类防破音关闭，`>2.3V` D 类防破音模式 1。
  - Datasheet 公式：gain 由外部 `Ri` 和内部 `Rf=400K`、内部输入电阻 5K 相关；本板原理图也标注 `Gain=400K/(Ri+5K)`。`Ri=39K` 估算电压增益约 9.09 倍，即约 19.2dB。
  - `Ci=0.1uF`、`Ri=39K` 输入高通估算约 `1/(2*pi*Ri*Ci)=40.8Hz`。
- Project binding：
  - `include/river/river_audio_hw_overrides.h`：`AUDIO_HW_AMPLIFIER_PIN=_PB_25`。
  - `CMakeLists.txt`：对 AP `audio_hal_${c_CURRENT_IMAGE}` target 注入 override header。
  - `components/river_diag/river_diag_cmd.c`：本地 tone 诊断命令 `river playback tone [freq_hz] [duration_ms] [level_pct]`。
  - `components/river_diag/CMakeLists.txt`：包含 `${c_CMPT_AUDIO_DIR}/interfaces` 以调用 AudioService/AudioControl。
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

- No-sound debug order：
  1. `river playback status` 看 playback service 是否 start/write/drain。
  2. 测 U7 VDD 是否为 `VCC_5V_IN`。
  3. 测 U7 pin1 `SHUT/SD` 是否落在目标区间。
  4. 差分测 `LINEOUT_LN/LP`。
  5. 测 U7 `IN+/IN-` 和 `OUTP/OUTN`。
  6. 测 CN2 线束/喇叭。
- Safety note：CN2 是 BTL/differential 输出，不要把 `SPKP` 或 `SPKN` 当单端地参考输出直接短到 GND。

### LCD / Backlight / Touch

- Connector：CN6 `A113F-15025WUA-R01`，pins include `VCCIO/VCC3/VCC5/RST/D1P/D1N/CLKP/CLKN/D0P/D0N/VCC_TP/I2C_SCL/I2C_SDA/TP_INT/TP_RST/LEDK/LEDA`。
- DSI path：
  - `DSI_DN1/DP1 -> MIPI_TXD1N/P -> CN6 D1N/P`。
  - `DSI_CN/CP -> MIPI_TXCLKN/P -> CN6 CLKN/P`。
  - `DSI_DN0/DP0 -> MIPI_TXD0N/P -> CN6 D0N/P`。
- Power/reset/backlight：
  - `LCD_PWR_ON` controls `VCC_3V3_LCD` LDO.
  - `RST_LCD` from `PA14` through R35; R34 100K pull-up to LCD 3.3V.
  - `LCD_BL_PWM` controls U10 `STI9287C` boost EN through R42/R43; backlight note says LED4 串 2 并.
- ST7102 init evidence：
  - `SSD_LANE(1,0)`：注释说明 `[0]` 为 lane select，`1` 表示 1 lane，`0` lane speed auto。
  - `SSD_SEND(0x01,0x35,0x00)` enables TE.
  - `0x11` 后 delay 250ms，`0x29` 后 delay 200ms。
  - `SSD_MODE(1,1)`：non-burst sync events + HS mode enable。
- Firmware action：
  - LCD driver 不要把 lane 数不可配置地写死。首版至少暴露 `lane_count`、`lane_rate`、porch/sync、reset pulse、init table。
  - Bring-up 顺序建议：`LCD_PWR_ON` -> delay -> `RST_LCD` pulse -> DSI LP init commands -> sleep out/display on -> 开 backlight PWM。
  - 开背光前先确认 DSI/TE，避免把白屏与背光问题混在一起。
- Touch path：
  - `PB10/PB11 -> TP_SDA2/TP_SCL2 -> CN6 I2C_SDA/I2C_SCL`。
  - `PA9 -> TP_INT`，`PA10 -> TP_RST`。
  - Pull-ups are on `VCC_3V3_LCD` group.
  - Sitronix sample uses I2C `reg=<0x55>`，IRQ falling，RST active-low；SPI examples are not used by this schematic.
- Touch validation：
  - LCD power on and reset sequence complete后，对 PB10/PB11 bus 扫描。
  - 优先 probe `0x55`，读 SFR/chip id；`0x00/0xFF` 是 bus/reset/firmware 异常信号。
  - 中断脚应在触摸时产生边沿；确认 PA9 interrupt polarity。

### BL702 Zigbee Subsystem

- Component：`U12 BL702C-10-Q2H/QFN32/BL`。
- Power/clock/RF：
  - `VCC_3V3_Z` powers VDDIO/analog rails.
  - 32MHz crystal `X2` connected to `XTAL_HF_IN/OUT`。
  - RF path `Zigbee -> L12/L22 -> ZIGBEE_ANT -> T3 IPEX`。
- Control:
  - `RST_BL702` -> BL702 `PU_CHIP` through R52, with R53 pull-up.
  - `BOOT_BL702` -> GPIO28 bootstrap, R47 20K pull-down.
  - GPIO28 note: `0 boot From Flash`, `1 boot From Uart`。
- UART：
  - RTL8730 `PB20/RTL8730_TX1` -> BL702 `GPIO15/UART_RX1` via R50。
  - RTL8730 `PB19/RTL8730_RX1` <- BL702 `GPIO14/UART_TX1` via R51。
- Firmware action：
  - `Zigbee_PWR_ON` -> wait rail stable -> set `BOOT_BL702` low -> release `RST_BL702` -> open UART1。
  - Upgrade/recovery mode must be an explicit path that pulls `BOOT_BL702` high before reset.
  - Driver must make protocol baudrate/config configurable until BL702 firmware owner provides contract。

### Wi-Fi / BT RF

- RTL8730E internal Wi-Fi/BT RF nets are on P09 with filter `U8 FLT18D24255171D-3271A`，T2 Wi-Fi IPEX，ANT4 onboard BT antenna。
- Firmware boundary：
  - RF matching/antenna selection is hardware domain.
  - Wi-Fi bring-up code should log empty efuse handling, MAC source, country/tx-power fallback, scan candidate, auth/assoc and DHCP.
  - Do not alter RF power or regulatory settings without hardware/RF review。

### TH Sensor FPC

- Connector：CN5 `A113F-15006WUA-R01/0.5MM/6PIN`。
- Nets：`VCC_3V3`、`TH_I2C_SDA`、`TH_I2C_SCL`、GND/TVS。
- Firmware action：
  - Treat as external I2C module, not a known on-board sensor.
  - Add a safe bus scan diagnostic before driver binding.
  - Do not infer register map from net name。

### IR

- CN7 includes `IR_LED_N` powered from `VCC_5V_IN` through R30 2.7R and driven by Q3 NMOS; RTL8730E controls Q3 gate via `IR_TX`/R31。
- Firmware action：
  - Confirm PA3 supports desired PWM/timer output.
  - Keep duty/current conservative until LED current and thermal limits are confirmed。
  - For carrier tests, scope `IR_LED_N` and Q3 drain/source rather than relying only on GPIO toggles。

### Debug / Base Interface

- CN1 exposes `VCC_5V_IN`、USB DP/DN、`RTL8730_TX0/RX0` and GND.
- `CHIP_EN` reset button SW2 pulls enable low; this is reset, not power removal.
- Current flashing policy for this board requires manual download mode before running the flash command。

## Power, Reset, And Boot

| Topic | Schematic Observation | Datasheet / SDK Requirement | Firmware Impact | Confidence |
| --- | --- | --- | --- | --- |
| RTL8730E main power | External 3.3V plus internal DCDC rails on P06 | Realtek guide recommends embedded DCDC for core/mem/audio rails | Do not disable SoC power rails; board code mainly controls sub rails | B |
| LCD power | `LCD_PWR_ON` enables U4 3.3V LCD rail | Panel requires power before reset/init | Driver owns power/reset/backlight sequence | A |
| Zigbee power | `Zigbee_PWR_ON` switches `VCC_3V3_Z` | BL702 boot depends on rail/reset/strap | Zigbee driver must serialize power, boot, reset | A |
| NAND boot/download | SPI NAND on PC1-PC6 | Realtek tools separate NAND/NOR profile | Use `tools/river_flash.py` NAND default | A |
| BL702 boot strap | GPIO28 low Flash, high UART | Bootstrap sampled around reset | Keep `BOOT_BL702` low except upgrade | A |
| Amp mode | `PB25/MUTE` drives AXS2033 SD | SD voltage selects mode | Measure SD and keep amp control in HAL/BSP | B |

## Clocks And Timing

| Clock / Signal | Source | Destination | Frequency / Mode | Firmware Configuration | Evidence |
| --- | --- | --- | --- | --- | --- |
| RTL8730E crystal | X1 | RTL8730E XI/XO | 40MHz | board fixed | P07 |
| BL702 crystal | X2 | BL702 XTAL_HF_IN/OUT | 32MHz | BL702 fixed | P12 |
| PDM clock | RTL8730E DMIC | MSM261DDB021 CLK | SDK chooses DMIC clock, typically 2.5MHz for 16k capture path | `AUDIO_HW_DMIC_CLK_PIN=_PA_2` | E2/E6 |
| DSI clock | RTL8730E DSI | ST7102 panel | missing lane rate | board config must expose timing | E12/E13 |
| I2C touch/TH | RTL8730E PB10/PB11 | Sitronix/TH FPC | 100/400kHz recommended for initial probe | scan first, then driver | E14/E17 |
| Backlight PWM | RTL8730E PA16 | U10 EN | duty/frequency missing | conservative PWM, default off | P10 |

## Communication Interfaces

| Interface | Instance | Pins/Nets | Connected Device | Address / CS / IRQ | Driver Notes | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| QSPI NAND | Flash controller | PC1-PC6 / `FLASH_QSPI_*` | GD5F1GM7UEYIGR | `/CS` on PC6 | NAND profile; 1Gb/128MB | E10 |
| PDM/DMIC | Audio HAL DMIC DATA1 | PA2/PA4 | MSM261DDB021 pair | DMIC3/DMIC4 | Override SDK defaults | E1-E6 |
| Audio line-out | Audio HAL speaker route | LINEOUTLN/LP | AXS2033 | PB25 controls SD | speaker route -> line-out differential | E7-E9 |
| MIPI DSI | DSI host | D0/D1/CLK | ST7102 LCD | reset PA14 | lane/timing unresolved | E12/E13 |
| I2C touch/TH | likely I2C on PB10/PB11 | TP/TH SDA/SCL | Sitronix + TH module | touch candidate 0x55, IRQ PA9 | scan before bind | E14/E17 |
| UART1 | UART1 | PB20/PB19 | BL702 GPIO15/14 | no flow control | protocol/baudrate missing | E15 |
| UART0/USB | debug/base | PB24/PB23, USB DP/DN | CN1/base board | N/A | console/download/debug owner conflict to manage | P05/P07 |
| IR | GPIO/PWM | PA3 -> Q3 | IR LED on CN7 | N/A | confirm PWM mux/current | P08 |

## Firmware Bring-Up Checklist

1. Build-time board constants:
   - `AUDIO_HW_DMIC_CLK_PIN=_PA_2`
   - `AUDIO_HW_DMIC_DATA1_PIN=_PA_4`
   - `AUDIO_HW_AMPLIFIER_PIN=_PB_25`
   - NAND profile default enabled in `tools/river_flash.py`

2. Audio capture:
   - Confirm logs show `pdm-2mic-pa2-pa4-data1` and `DMIC3/DMIC4`.
   - Confirm `[AudioHal-I] ... set DMIC clock`.
   - Speak/knock and verify capture/preproc peak rises.
   - If peak is zero, probe PDM_CLK/PDM_DAT1 before changing software path.

3. Audio output:
   - Run `river playback tone 1000 1000 25`.
   - Confirm playback logs start/write/drain/stop.
   - Measure U7 VDD, U7 pin1 SD, LINEOUT_LN/LP, U7 OUTP/OUTN, CN2.
   - If PB25 toggles but U7 SD voltage is wrong, fix SD network assumptions before changing playback service.

4. LCD:
   - Power `VCC_3V3_LCD`.
   - Apply reset pulse.
   - Send ST7102 init table with lane count configurable.
   - Verify DSI LP/HS and TE before enabling backlight.

5. Touch:
   - After LCD/touch power and reset, scan PB10/PB11 I2C.
   - Try `0x55` first, read chip id/SFR.
   - Configure PA9 falling IRQ only after polling read works.

6. BL702:
   - Set `BOOT_BL702` low.
   - Enable `VCC_3V3_Z`.
   - Release reset.
   - Open UART1 with known baudrate after owner supplies protocol.

7. TH:
   - Scan shared I2C bus.
   - Do not add a driver until module model/address is confirmed.

8. Flash:
   - Manually enter download mode.
   - Use `python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000`.
   - Verify NAND detection and `Finished PASS`.

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
  - `components/river_diag/river_diag_cmd.c`
  - `components/river_diag/CMakeLists.txt`
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
- No product interaction, application session behavior, network service policy or UI flow is used as a hardware conclusion.
