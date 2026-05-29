# RTL8730E 4 寸板硬件固件说明书

日期：2026-05-29
对象：Orvibo RTL8730E 4 寸板，原理图 `RTOS_4InchLCD` / `MixPad4_RTL8730`，Rev `V0.1` / `V1A 20260506`
目的：把 `doc/hard/` 的原理图、丝印图和本地 datasheet 转成固件同事可执行的 pinmux、驱动配置、启动日志和验证清单。

## 结论摘要

- 当前板子的数字麦克风不是 EVB AMIC 路径，而是 PDM/DMIC：`PDM_CLK -> RTL8730E PA2`，`PDM_DAT1 -> RTL8730E PA4`。在 AmebaSmart SDK 里，DATA1 对应 `AUDIO_DMIC3/DMIC4`，项目已用 AP Audio HAL override 固定 `AUDIO_HW_DMIC_CLK_PIN=_PA_2`、`AUDIO_HW_DMIC_DATA1_PIN=_PA_4`，并已实板验证有稳定语音输入。
- 启动介质是 GigaDevice `GD5F1GM7UEYIGR` SPI NAND，原理图标注 128MB，实板 flash 工具也识别为 `GD5F1GM7U 1Gb/128MB`。烧录默认必须走 NAND profile，不要再用 NOR profile。
- BL702 Zigbee 子系统由 RTL8730E 控制电源、复位、boot strap 和 UART1。原理图写明 `GPIO28: 0 boot from Flash, 1 boot from Uart`，固件必须避免误拉高 `BOOT_BL702` 进入 UART boot。
- LCD 是 2-lane MIPI DSI + 触摸 I2C + 背光 PWM；但缺少 LCD panel IC、touch controller 型号和初始化时序资料，当前只能给接口映射，不能锁定完整 panel driver。
- 温湿度传感器通过 CN5 FPC 暴露 `TH_I2C_SDA/SCL` 和 3.3V，但原理图未给外接传感器型号、地址和中断脚。固件需要硬件同事补充模块资料或上电 I2C scan。
- AXS2033 功放 `SHUT/SD` 由 `MUTE` 网控制；SD 电压区间决定 shutdown、AB/D 类和防破音模式。固件不能只按布尔 mute 理解，需要确认 `MUTE` 实际电平是否能达到目标 SD 电压区间。

## Evidence Index

| ID | Claim | Source | Type | Confidence | Notes |
| --- | --- | --- | --- | --- | --- |
| E1 | `PDM_CLK` 接 RTL8730E `PA2`，`PDM_DAT1` 接 RTL8730E `PA4` | `doc/hard/RTL8730 4寸SCH.pdf` P09/P11；用户确认 | schematic + user fact | A | P09 显示 `PA4 -> PDM_DAT1`，P11 显示 `PA2 -> PDM_CLK`。 |
| E2 | AmebaSmart SDK 中 `DMIC3/DMIC4` 使用 `AUDIO_HW_DMIC_DATA1_PIN`，并共用 `AUDIO_HW_DMIC_CLK_PIN` | `/root/ameba-rtos/component/audio/audio_hal/amebasmart/ameba_audio_stream_capture.c:352` | SDK | A | DATA0/1/2/3 分别映射 DMIC1/2、3/4、5/6、7/8。 |
| E3 | SDK 默认 DMIC pin 是 PB 组，不匹配当前板子 PA2/PA4 | `/root/ameba-rtos/component/soc/usrcfg/amebasmart/include/ameba_audio_hw_usrcfg.h:84` | SDK | A | 默认 `_PB_22/_PB_18` 等需要项目覆盖。 |
| E4 | 项目当前固件固定 PA2/PA4 DATA1 方案 | `include/river/river_audio_hw_overrides.h:12`；`components/river_voice/river_voice_board.c:13` | code | A | profile 为 `pdm-2mic-pa2-pa4-data1`，primary/secondary 为 `AUDIO_DMIC3/4`。 |
| E5 | PA2/PA4 DATA1 方案已实板验证 | `.codex/changes.md` Step H.xiaozhi-client.69 | runtime | A | 非零 peak、VAD speech、KWS trigger、server STT/TTS 已出现。 |
| E6 | MSM261DDB021 是 PDM 数字麦，VDD 1.6-3.6V，标准性能 clock 1.1-4.8MHz | `doc/hard/1.01.070080 MSM261DDB021_Rev1.0.pdf` p4/p9/p10/p14 | datasheet | A | pin1 DATA、pin4 CLK；L/R 决定边沿/槽位。 |
| E7 | AXS2033 是 3.1W 单声道 AB/D 类功放，SD 电压区间控制模式 | `doc/hard/AXS2033.pdf` p2/p8；ChipSourceTek product page | datasheet + vendor page | A/B | 本地 PDF 为主要证据，网页用于型号交叉确认。 |
| E8 | NAND 型号为 `GD5F1GM7UEYIGR`，原理图标注 128MB | `doc/hard/RTL8730 4寸SCH.pdf` P11；GigaDevice product page | schematic + vendor page | A/B | 实板 flash 工具识别 `GD5F1GM7U 1Gb/128MB`。 |
| E9 | BL702 支持 BLE/Zigbee，板上 UART1 连接 RTL8730E | `doc/hard/RTL8730 4寸SCH.pdf` P12；Bouffalo Lab product page | schematic + vendor page | A/B | 官方网页说明 BL702 支持 BLE/Zigbee 和 RISC-V。 |
| E10 | GPIO28 boot strap：0 从 Flash 启动，1 从 UART 启动 | `doc/hard/RTL8730 4寸SCH.pdf` P12 | schematic | A | 原理图直接标注。 |
| E11 | LCD 接口为 MIPI DSI + touch I2C + backlight PWM | `doc/hard/RTL8730 4寸SCH.pdf` P10 | schematic | A | 缺少 panel/touch IC 型号。 |
| E12 | 丝印图标注 `MixPad4_RTL8730 V1A 20260506`，CN7 `MIC/IR`，CN5 `TH`，T2 Wi-Fi，T3 Zigbee | `doc/hard/RTL8730_4寸_丝印图.pdf` p2 渲染/OCR | visual/OCR | B | 丝印无文本层，低于原理图可信度。 |

## Artifact Inventory

| Artifact | Path / URL | Pages / Sheets Used | Extraction Method | Notes |
| --- | --- | --- | --- | --- |
| 主原理图 | `doc/hard/RTL8730 4寸SCH.pdf` | 13 pages；重点 P05-P13 | PyMuPDF text + page render | 有文本层，关键 net 可直接提取并视觉复核。 |
| 丝印图 | `doc/hard/RTL8730_4寸_丝印图.pdf` | 2 pages | PyMuPDF render + Tesseract OCR | 无文本层；仅用于定位连接器/测试点。 |
| PDM 麦 datasheet | `doc/hard/1.01.070080 MSM261DDB021_Rev1.0.pdf` | 16 pages；重点 p4/p9/p10/p14 | PyMuPDF text | 本地供应商规格书，Rev 1.0。 |
| 功放 datasheet | `doc/hard/AXS2033.pdf` | 9 pages；重点 p2/p8 | PyMuPDF text | 本地规格书，部分中文 OCR/字体有轻微乱码。 |
| RTL8730E SDK | `/root/ameba-rtos` | audio HAL、usrcfg、fwlib headers | `rg` + source review | 用于确认 DMIC category 和 clock/pinmux。 |
| 外部网页 | Realtek Ameba IoT docs, GigaDevice, Bouffalo Lab, ChipSourceTek | 相关产品页 | browser/curl/Jina mirror | 用于型号/能力交叉确认；关键配置仍以本地原理图/SDK为准。 |

## Engineer-Supplied References

| Material | Path / URL | Provider / Context | Used For | Version / Revision Check |
| --- | --- | --- | --- | --- |
| 当前硬件资料目录 | `doc/hard/` | 用户指定 | 全板分析输入 | 主原理图日期 2026-05-06，Rev V0.1。 |
| 麦克风连接事实 | 用户消息：`PDM_CLK 接 PA2, PDM_DAT1 接 PA4` | 用户/硬件上下文 | 数字麦路线确认 | 与 P09/P11 原理图一致。 |
| 运行验证记录 | `.codex/changes.md` Step H.69 | 当前项目记录 | PA2/PA4 DATA1 实板闭环 | 与当前代码 H.70 保留方案一致。 |

## Missing Inputs And Clarifications

| Missing Item / Question | Why It Matters To Firmware | Current Best Guess | Confidence | Recommended Owner / Check |
| --- | --- | --- | --- | --- |
| LCD panel IC、分辨率、DSI timing、初始化命令 | 决定 MIPI DSI driver、lane rate、reset/backlight 顺序 | 4 寸 MIPI DSI panel，经 CN6 `A113F-15025WUA-R01` 接入 | C | 硬件/屏供应商提供 datasheet 和 init table。 |
| Touch controller 型号、I2C 地址、中断极性、复位时序 | 决定 touch driver 和开机 probe | 触摸走 `TP_SCL2/TP_SDA2`，且经 100R 串到 `TH_I2C` nets | C | 硬件提供触摸 IC；固件上电 I2C scan + 复位测量。 |
| TH FPC 外接温湿度传感器型号和 I2C 地址 | 没有型号无法写 sensor driver | CN5 是外接 TH 模块，3.3V + I2C | C | 硬件提供模块 BOM；固件 scan `TH_I2C`。 |
| CN7 上 MIC/IR 小板的实际麦克风数量、L/R 接法、供电脚定义 | 影响 PDM 左右槽位、双麦相位、波束形成 | MSM261DDB021 双麦，DATA 复用一根 PDM_DAT1，L/R 一高一低 | B | 硬件提供小板原理图；逻辑分析 PDM CLK/DATA 和 L/R。 |
| `MUTE` 到 AXS2033 `SHUT/SD` 的实际电压范围 | SD 不是简单 enable，电压区间决定 D 类/AB/关断 | `PB25 -> MUTE -> R20/R21/C69/C71 -> SHUT`，可能做延时/分压 | B | 示波器测 SD 电压；确认固件 high/low 对应模式。 |
| BL702 固件/协议和 UART 波特率 | 决定 Zigbee 子系统启动、升级和通信协议 | RTL8730 UART1 透传到 BL702 GPIO14/15 | B | Zigbee 固件负责人提供协议；串口探测 boot log。 |
| Realtek RTL8730E 完整 pinmux/reference manual | 可进一步核验所有 PA/PB/PC 复用功能 | 本地 SDK 已足够确认当前 DMIC 和 audio HAL | B | 如要扩展 LCD/IR/Zigbee，补官方 pinmux 表。 |

## Hypotheses / Candidate Solutions

| Hypothesis | Evidence For | Evidence Against / Risk | Confidence | Suggested Firmware Path | Validation Step |
| --- | --- | --- | --- | --- | --- |
| PDM 双麦在同一 DATA1 上用 L/R 边沿复用 | MSM261DDB021 支持 L/R 边沿选择；CN7 只有 `PDM_DAT1` 一根数据线；实板 DMIC3/4 有效 | 小板 L/R 接法未给出，左右声道可能反或同槽 | B | 保持 `DMIC3/DMIC4`，后续用相位/声源方向测试修正通道顺序 | 近场分别遮挡/敲击两颗麦，记录 ch0/ch1 peak。 |
| `MUTE=high` 可能不是单纯 unmute，而是选择 AXS2033 D 类防破音模式 | AXS2033 SD >2.3V 进入 D 类防破音模式；原理图 `MUTE` 进 SHUT | 分压/RC 可能导致中间电压，错误模式会无声或 pop | B | 播放前后按既有 playback mute 管理，但加入 SD 电压实测要求 | 测 U7 pin1 在 boot、idle、playback、mute 的电压。 |
| `TH_I2C` 和 `TP_SDA2/TP_SCL2` 可能共用 RTL8730E 的 PB10/PB11 I2C | P10 中 `TP_SDA2/SCL2` 经 R36/R37 接 `TH_I2C_SDA/SCL`，P13 直接用 TH nets | 若触摸和 TH 同总线，地址冲突/复位时序会影响 probe | B | 做单总线 I2C scan，驱动按地址注册，不硬编码单设备 | 分别接/断 TH FPC 和 LCD，比较 scan 结果。 |
| BL702 默认应从 Flash 启动，只有下载/恢复时拉高 `BOOT_BL702` | P12 明确 `GPIO28 0 boot From Flash 1 boot From Uart` | 如果 BOOT 控制极性被上层误设，高电平会卡在 UART boot | A | 默认把 BOOT GPIO 配为输出低或输入下拉，仅升级流程临时拉高 | 复位 BL702 后观察 debug UART 是否正常启动。 |

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
| Amp mute/mode | `MUTE` | `PB25` pin 96 | GPIO | 3.3V control to U7 SD | R20 100R + R21 10K/C69/C71 | P08/P11, AXS2033 datasheet | 固件按“功放 SD 控制”命名更准确；需实测电压区间。 |
| LCD power enable | `LCD_PWR_ON` | `PB26` pin 97 | GPIO | 3.3V control | 电源页 U4 EN | P05/P11 | DSI/touch 初始化前先上 LCD 3.3V。 |
| LCD reset | `RST_LCD` | `PA14` pin 16 | GPIO | 3.3V_LCD | R34 100K pull-up，R35 1K 串 | P10 | 按 panel 时序拉低/释放。 |
| Backlight PWM | `LCD_BL_PWM` | `PA16` pin 18 | PWM/GPIO | 3.3V control, 5V boost | R42 100R，R43 10K pulldown | P10 | 初始化完 panel 后打开 PWM；默认低关背光。 |
| Touch interrupt/reset | `TP_INT`, `TP_RST` | `PA9`, `PA10` | GPIO IRQ / GPIO | 3.3V_LCD | 4.7K pull-up group | P10 | 等待 touch IC 型号确认中断极性和 reset pulse。 |
| Touch/TH I2C | `TP_SDA2/SCL2`, `TH_I2C_SDA/SCL` | `PB10/PB11` | I2C | 3.3V_LCD / 3.3V | 4.7K pull-ups | P10/P13 | 做 bus scan；注意 touch 和 TH 可能同总线。 |
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

- Validation：说话时 `audio diag` 的 capture/preproc peak 必须非零；H.69 已观察到 VAD speech、KWS trigger、server STT/TTS。
- Risks：左右槽位、麦间距 50mm、L/R 边沿和声学方向仍需声学测试，不影响“采到音”的基本结论。

### Speaker / Amplifier / Loopback

- Component：`U7 AXS2033/QFN8/AXS`，CN2 标注 `1W Speaker`。
- Inputs：RTL8730E `LINEOUT_LN/LP` 经 `C76/C153`、`R56/R57 39K` 到 U7 `IN-/IN+`。
- Outputs：U7 `SPKP/SPKN` 经磁珠和 TVS 到 CN2 speaker。
- Control：`MUTE` 接 U7 `SHUT/SD`，同时有 R20/R21/C69/C71 网络。
- Loopback：`SPK_OUTP/N` 经 `R22/R26 20K`、分压/电容到 `MIC5_P/N`，可作为播放参考/回采线索。
- Firmware action：
  - playback start 前解除 mute，stop/drain 后按策略 mute。
  - 不要把 `MUTE` 当成简单 active-high/low；SD 电压区间决定模式，必须实测。
- Validation：测 U7 pin1 在 idle/playback/mute 的电压；听感无声时先查 `MUTE` 和 `VCC_5V_IN`。

### Wi-Fi / BT RF

- Component：RTL8730E 内部 Wi-Fi/BT RF，`U8 FLT18D24255171D-3271A`，T2 IPEX，ANT4 onboard BT antenna。
- Firmware impact：RF 匹配和天线选择主要硬件侧；固件侧已处理空 efuse Wi-Fi bring-up、国家码和连接策略。
- Validation：Wi-Fi 连接日志应出现 scan、candidate、auth/assoc、DHCP；空 efuse 板用固定 Device-Id 避免服务端绑定漂移。

### LCD / Touch / Backlight

- Connector：`CN6 A113F-15025WUA-R01`。
- DSI：`MIPI_TXD1P/N`、`MIPI_TXCLKP/N`、`MIPI_TXD0P/N`。
- Power/reset：`VCC_3V3_LCD`，`RST_LCD`，`LCD_PWR_ON`。
- Touch：`TP_SCL2`、`TP_SDA2`、`TP_INT`、`TP_RST`。
- Backlight：`U10 STI9287C`，`LCD_BL_PWM`，LED4 串 2 并。
- Firmware action：先上 LCD power，再 reset panel/touch，初始化 DSI，最后开背光 PWM。
- Blocker：缺 panel/touch datasheet，无法确定 DSI init table、分辨率、lane rate、touch 地址。

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
- Missing：BL702 app 协议、baudrate、升级流程。

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
| LCD power | `LCD_PWR_ON` 控 U4 3.3V_LCD | panel/touch 时序未知 | 需补 panel init 时序 | B |

## Clocks And Timing

| Clock / Signal | Source | Destination | Frequency / Mode | Firmware Configuration | Evidence |
| --- | --- | --- | --- | --- | --- |
| RTL8730E XTAL | X1 | RTL8730E XI/XO | 40MHz | SDK platform default | P07 |
| PDM CLK | RTL8730E PA2 | MSM261DDB021 CLK | SDK 当前 16k 路径通常配置 2.5MHz；麦支持 1.1-4.8MHz 标准模式 | DMIC clock via Audio HAL | MSM datasheet + SDK `ameba_audio.h` |
| BL702 XTAL | X2 | BL702 XTAL_HF_IN/OUT | 32MHz | BL702 固件侧 | P12 |
| LCD MIPI clock | RTL8730E DSI | CN6 panel | 未知 lane rate | 待 panel datasheet | P10 |
| IR carrier | RTL8730E PA3 candidate | IR LED driver | 常见 38kHz，未由原理图证明 | 待遥控协议 | P08/P11 |

## Communication Interfaces

| Interface | Instance | Pins/Nets | Connected Device | Address / CS / IRQ | Driver Notes | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| QSPI NAND | RTL8730E flash controller | `PC1-PC6`, `FLASH_QSPI_*` | GD5F1GM7UEYIGR | CSN on PC6 | NAND boot/download profile | P11 |
| PDM/DMIC | Audio HAL | PA2 CLK, PA4 DATA1 | MSM261DDB021 mic board | shared data + L/R slot | `DMIC3/DMIC4` | P08/P09/P11 + SDK |
| UART1 | RTL8730 UART1 | `RTL8730_TX1/RX1` | BL702 UART1 | no flow control shown | Zigbee control/protocol | P11/P12 |
| UART0 | RTL8730 UART0 | `RTL8730_TX0/RX0` | base interface CN1 | no flow control shown | debug/base board | P05/P11 |
| MIPI DSI | RTL8730 DSI | D0/D1/CLK pairs | LCD panel | panel reset on PA14 | missing init table | P10 |
| I2C | likely PB10/PB11 | `TP_SDA2/SCL2`, `TH_I2C_SDA/SCL` | touch + TH | unknown addresses, TP_INT PA9 | scan before driver bind | P10/P13 |
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
   - Request panel and touch datasheets before implementing driver.
   - Power sequence candidate: `LCD_PWR_ON` high -> delay -> `RST_LCD` pulse -> DSI init -> touch reset -> backlight PWM.
   - Run I2C scan on `TP/TH` bus before binding touch/TH drivers.

5. Speaker:
   - Before blaming decoder/playback, measure AXS2033 VDD and SD/MUTE voltage.
   - Check expected playback logs and speaker output at CN2.

6. IR:
   - Validate PA3 PWM capability and current limit before enabling long carrier bursts.

## Risks And Unknowns

| Risk | Impact | Confidence | How To Resolve |
| --- | --- | --- | --- |
| LCD/touch datasheet missing | Cannot write correct panel/touch drivers | A | Get vendor docs or dump known-good init sequence. |
| TH sensor model missing | Cannot know I2C address/register map | A | Provide module BOM or run I2C scan and identify chip marking. |
| AXS2033 SD voltage not measured | Could leave amp in wrong mode or shutdown | B | Probe U7 pin1 under firmware states. |
| PDM L/R slot/channel order unknown | Beamforming/KWS quality may be suboptimal | B | Per-mic near-field test and optional raw WAV dump. |
| BL702 protocol unknown | Zigbee feature cannot be integrated safely | A | Provide BL702 firmware protocol and upgrade mode docs. |
| External web datasheets not all downloaded locally | Some vendor URLs are unstable from CLI | B | Store exact RTL8730E/BL702/GD5F1 datasheets under `doc/hard/` when obtained. |

## Source Notes

- Local artifacts:
  - `doc/hard/RTL8730 4寸SCH.pdf`
  - `doc/hard/RTL8730_4寸_丝印图.pdf`
  - `doc/hard/1.01.070080 MSM261DDB021_Rev1.0.pdf`
  - `doc/hard/AXS2033.pdf`
- SDK sources:
  - `/root/ameba-rtos/component/audio/audio_hal/amebasmart/ameba_audio_stream_capture.c`
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
