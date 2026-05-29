# 原理图/PCB 辅助嵌入式开发 Skill 调研与使用说明

## 目标

为软件同事提供一个可复用流程：从原理图 PDF、PCB 截图、图片、BOM 或 EDA 导出开始，结合 OCR、视觉检查、外部芯片资料和 SDK 示例，输出面向固件开发的 Markdown 使用说明书。

核心产物不是电气设计审查结论，而是固件同事能直接使用的资料：

- MCU/SoC pin map
- 外设实例与 pinmux
- 电源、复位、启动脚、时钟、通信总线约束
- 驱动配置建议
- 预期启动日志
- 板级验证 checklist
- 可追溯证据表和可信度

## 外部调研结论

已检索到的外部方向主要分为三类：

1. KiCad/EDA 源文件分析类：
   - `kicad-happy` 一类工具适合有 `.kicad_sch`、netlist、ERC 输出时做可编程检查。
   - 可信度高，但前提是拿到原始 EDA 源文件；对只有 PDF/图片的场景不够直接。

2. AI/商业原理图审查类：
   - Seeed `schematic-analyzer`、Traceformer、Schemalyzer、QTex、HyperLynx 等更偏原理图规范、ERC、SI/PI 或产品化审查。
   - 可作为补充，但不能直接替代 firmware handoff 文档。

3. PDF/OCR/视觉 + datasheet/SDK 检索组合流程：
   - 最贴近当前需求。
   - 对用户实际给到的 PDF/截图/日志更鲁棒。
   - 关键是证据分级，不能把 OCR 或图片推断当成确定事实。

因此新增本地 Codex skill：

- 路径：`/root/.codex/skills/schematic-pcb-firmware-guide`
- 名称：`schematic-pcb-firmware-guide`
- 用途：原理图/PCB/硬件 PDF 到固件开发说明书。

## 使用方式

在后续任务中直接说：

```text
使用 $schematic-pcb-firmware-guide 分析 doc/hard/xxx.pdf，输出面向固件同事的 Markdown 硬件说明书。
```

也可以给出更具体要求：

```text
使用 $schematic-pcb-firmware-guide 分析这块板的音频、Wi-Fi、flash、按键和电源部分。
请输出 pinmux 表、SDK 配置建议、风险列表和上板验证 checklist。
```

## Skill 内置流程

1. 输入清点：
   - 原理图 PDF/图片
   - PCB 截图或照片
   - BOM、netlist、KiCad/OrCAD/Allegro 导出
   - datasheet、SDK 文档、用户日志

2. PDF/图片处理：
   - PDF 渲染为图片
   - 提取可选中文本和表格
   - 对扫描件或截图跑 OCR
   - 必要时裁剪、放大、二值化、锐化后复核

3. 重点块识别：
   - MCU/SoC pin、boot strap、reset、power sequence
   - flash/PSRAM/eMMC/SDIO
   - UART/I2C/SPI/I2S/PDM/ADC/PWM/GPIO
   - sensor/audio/display/RF/USB/buttons/LEDs

4. 外部资料检索：
   - 芯片 datasheet/reference manual
   - SDK 使用说明和示例
   - eval-board 原理图
   - vendor app note / errata / forum

5. 交叉验证：
   - net name vs datasheet pinmux
   - schematic vs SDK 示例
   - 用户运行日志 vs 硬件连接
   - OCR 结果 vs 视觉复核

6. Markdown 输出：
   - 摘要
   - 证据索引
   - MCU pin map
   - 外设块说明
   - 电源/复位/启动约束
   - firmware checklist
   - 风险和未知项

## 证据可信度

Skill 使用 A/B/C/D 四级：

- A：官方 datasheet/reference manual/SDK、原始 EDA/netlist、清晰原理图文本、实板日志匹配。
- B：PDF/OCR 加视觉确认、匹配的官方评估板原理图、多来源一致。
- C：社区资料、EDA 库、模糊 OCR、截图推断。
- D：无法确认，必须测量或请硬件确认。

固件关键结论必须标注证据来源和可信度。

## 当前建议

如果后续继续分析 Orvibo RTL8730E 板，建议用这个 skill 对以下内容做一次系统化 handoff：

- PDM 麦克风：`PDM_CLK -> PA2`、`PDM_DAT1 -> PA4`、SDK DATA1/DMIC3/4 映射。
- NAND flash：型号、容量、启动/烧录 profile、日志识别点。
- Wi-Fi 空 efuse：Device-Id 固定、HP efuse prompt bypass、Wi-Fi bring-up 日志边界。
- 音频播放和参考回路：I2S/codec/功放控制脚、播放参考 buffer 风险。
- 按键、LED、触摸、显示和其他 GPIO 的 active polarity。

这类报告应保存在 `doc/` 下，作为软件同事后续改驱动和定位板级问题的入口。
