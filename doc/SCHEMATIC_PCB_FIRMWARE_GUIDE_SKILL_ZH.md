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
- 避坑指南 / 注意事项 / 上手建议
- 可追溯证据表和可信度

报告边界需要明确：这是“硬件资料 -> BSP/HAL/驱动开发”的交接文档，不是业务方案设计文档。可以写 SDK/HAL API、pinmux、driver config、boot logs、测量方法和开发建议；不要擅自把云端协议、产品交互、应用会话、业务播放流程等写成硬件结论。运行日志里若出现业务名，只能作为验证背景，结论必须回到硬件或 HAL/driver 层。建议写 `HAL speaker route -> LINEOUT -> amplifier -> speaker`，不要写某个业务音源到喇叭的产品流程。

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
- 当前 helper：
  - `scripts/new_evidence_table.py`：生成证据表骨架。
  - `scripts/pdf_artifact_inventory.py`：在无 Poppler 环境下批量清点 PDF/图片、提取文本、渲染指定页面。

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

如果工程师手里已有资料，可以提前指定位置，skill 会优先纳入证据索引并做版本/型号校验：

```text
使用 $schematic-pcb-firmware-guide 分析 doc/hard/board.pdf。
已有资料：
- datasheet: doc/hard/RTL8730E_Datasheet.pdf
- SDK 示例: /root/ameba-rtos/component/audio/...
- 运行日志: doc/logs/2026-05-29-audio.log
缺失内容请列出来；如果能可靠猜测，请给出假设、可信度和验证建议。
```

## Skill 内置流程

1. 输入清点：
   - 原理图 PDF/图片
   - PCB 截图或照片
   - BOM、netlist、KiCad/OrCAD/Allegro 导出
   - datasheet、SDK 文档、用户日志
   - 工程师提前给出的本地路径或 URL

2. PDF/图片处理：
   - PDF 渲染为图片
   - 提取可选中文本和表格
   - 对扫描件或截图跑 OCR
   - 必要时裁剪、放大、二值化、锐化后复核
   - 如果环境缺少 Poppler，或需要批量清点多份 PDF/图片，可以使用 helper：

```bash
python3 /root/.codex/skills/schematic-pcb-firmware-guide/scripts/pdf_artifact_inventory.py \
  doc/hard/*.pdf --out tmp/hard_skill_inventory --render-pages 5-13 --markdown
```

   该 helper 会用 PyMuPDF/Pillow 生成 artifact inventory、提取文本，并按需渲染页面，适合作为报告 Evidence Index 的输入。

3. 重点块识别：
   - MCU/SoC pin、boot strap、reset、power sequence
   - flash/PSRAM/eMMC/SDIO
   - UART/I2C/SPI/I2S/PDM/ADC/PWM/GPIO
   - sensor/audio/display/RF/USB/buttons/LEDs
   - 外部连接器和混合信号块必须写完整硬件路径，例如：
     - `HAL/SoC-visible signal -> pin/net -> analog/power component -> connector/device`
     - enable/reset/interrupt/mute/shutdown polarity
     - 电压域、pull-up/down、默认态、测点
     - SDK 默认配置是否仍指向 reference board

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

6. 缺失信息处理：
   - 把缺失资料、缺失确认项、阻塞影响列成表。
   - 对关键缺失项提出澄清问题。
   - 如果有较可靠的猜测，必须标成“假设”，给出可信度、依据、风险和验证步骤。
   - 如果存在多个可能方案，列出候选方案和最低风险验证路径。

7. 避坑指南 / 注意事项 / 上手建议：
   - 每份报告都要包含这个独立章节，位置应靠前，让新接手的软件同事先看到高概率踩坑点。
   - 这部分内容必须来自外部主资料、SDK 示例/默认值、datasheet/app note/errata、原理图深度分析或本地运行记录；不要写泛泛而谈的经验口号。
   - 每条建议要说明：主题、为什么容易错、证据、对固件质量/bring-up 效率的影响、可信度、第一步低风险检查或编码防护。
   - 优先覆盖 SDK reference board 默认值不匹配、active-low 极性、boot strap、下载 profile、总线地址不确定、lane/timing 冲突、功放 mute/mode/gain、差分输出测量、电源/复位顺序、共享总线、缺 pull-up 和测量陷阱。
   - 上手建议应包含首读文件、首跑命令、预期日志、诊断命令和“测到什么之前不要改什么”的 guardrail。
   - 仍然停留在硬件/BSP/HAL/driver 层，不写产品交互、业务播放流程或应用会话策略。

8. Markdown 输出：
   - 摘要
   - 报告边界
   - 证据索引
   - 工程师提供资料索引
   - 缺失信息和澄清问题
   - 假设/候选方案
   - 固件同事快速上手
   - 避坑指南 / 注意事项 / 上手建议
   - MCU pin map
   - 外设块说明
   - 电源/复位/启动约束
   - firmware checklist
   - 风险和未知项
   - 如果本地固件仓库或 SDK 可见，还要给出实现绑定点：override header、CMake/build target、Kconfig、HAL API、诊断命令和建议归属的源码文件。
   - 产品硬件报告正文不得包含 skill 自身实现、agent 工作过程、提示词、迭代记录或工具开发 changelog；这些内容应放在 skill 文件、项目流程说明或普通变更记录里。

9. 完整性检查：
   - 音频输出不能只写“有功放”，要覆盖 HAL 可见播放设备、SoC line-out/I2S、功放输入/输出、增益电阻/输入电容、SD/mute/mode、喇叭连接器、回采/参考网络、测量路径和常见无声故障树。
   - 显示/触摸不能只写“有 LCD”，要覆盖 panel connector、DSI lane/timing/init table、power/reset/backlight、touch bus/address/IRQ/reset、probe 证据和缺失 timing。
   - 对传感器、无线子系统、电源、boot strap、flash 也要给出 HAL/BSP 可执行的配置项和验证方法。

10. 范围自检：
   - 交付前搜索云端、业务、会话、TTS/STT、产品交互等词，确认它们没有被写成硬件结论。
   - 避坑/建议章节要再次检查：每一条都必须能追溯证据、能减少具体固件错误或 bring-up 时间，不要保留没有证据的通用建议。
   - 对每个音频、显示、输入路径，确认描述从 SoC/HAL/driver 可见接口开始，到 pin、net、器件、连接器、测点或验证步骤结束。
   - 如果某句话在规定应用行为，要改写成硬件能力、BSP/HAL 配置或板级验证方法。

## 证据可信度

Skill 使用 A/B/C/D 四级：

- A：官方 datasheet/reference manual/SDK、原始 EDA/netlist、清晰原理图文本、实板日志匹配。
- B：PDF/OCR 加视觉确认、匹配的官方评估板原理图、多来源一致。
- C：社区资料、EDA 库、模糊 OCR、截图推断。
- D：无法确认，必须测量或请硬件确认。

固件关键结论必须标注证据来源和可信度。

## 缺失内容处理规则

- 对缺失内容不要静默跳过，要说明“缺什么、为什么影响固件、谁能确认、怎么验证”。
- 如果猜测足够可靠，可以给建议方案，但必须写清楚是假设，不可写成确定事实。
- 建议方案要尽量可逆，例如用可配置 pinmux、日志开关、一次性探测代码或示波器/逻辑分析仪测点来验证。
- 如果猜测可能导致硬件风险、烧录风险或误驱动电源/射频/电池相关电路，必须要求硬件确认后再实施。

## 当前建议

如果后续继续分析 Orvibo RTL8730E 板，建议用这个 skill 对以下内容做一次系统化 handoff：

- PDM 麦克风：`PDM_CLK -> PA2`、`PDM_DAT1 -> PA4`、SDK DATA1/DMIC3/4 映射。
- NAND flash：型号、容量、启动/烧录 profile、日志识别点。
- Wi-Fi 空 efuse：Device-Id 固定、HP efuse prompt bypass、Wi-Fi bring-up 日志边界。
- HAL 音频输出和参考回路：I2S/codec/LINEOUT/功放控制脚、板级参考网络风险。
- 按键、LED、触摸、显示和其他 GPIO 的 active polarity。

这类报告应保存在 `doc/` 下，作为软件同事后续改驱动和定位板级问题的入口。
