# WSL2 烧录串口与 `0xE8` 地址错误排查

## 适用场景

- 开发环境在 `WSL2`
- Windows 侧设备管理器看到串口，例如 `COM3`
- 项目使用 `RTL8730E`
- 烧录时出现：
  - `Download fail: Invalid cmd or parameter(0xE5)`
  - `Download fail: Address error(0xE8)`

## 本次排查结论

### 1. `0xE8` 不是这次固件过大导致

当前产物实测：

- `build_RTL8730E/km0_km4_ca32_app.bin` = `3593568`
- `build_RTL8730E/ota_all.bin` = `3593600`

项目自定义 profile 中，主应用下载区间为：

- `0x08040000 - 0x08600000`
- 容量 `6029312`

因此当前镜像仍在项目 profile 范围内，`0xE8` 不能简单归因于“bin 太大”。

对比：

- SDK 默认 profile 只给 `0x08040000 - 0x08300000`
- 如果误用了 SDK stock profile，当前镜像确实会越界

所以第一优先级不是缩镜像，而是确认实际使用的 profile 和串口链路。

### 2. WSL2 下不能只靠 `COM3 -> /dev/ttyS2` 机械映射

传统映射关系通常是：

- `COM1 -> /dev/ttyS0`
- `COM3 -> /dev/ttyS2`

但这只说明“编号可能对应”，不说明设备在当前 WSL2 实例里真的可用。

本次排查时，WSL2 内虽然能看到 `/dev/ttyS0..7`，但对这些节点执行 `stty -F /dev/ttySx -a` 全部返回 `Input/output error`。这说明：

- 当前环境下，`ttyS` 节点并不等于可用串口
- 继续把烧录失败直接归因到镜像大小是不成立的

### 3. WSL2 下优先使用 USB 直通后的 `ttyUSB*` / `ttyACM*`

推荐路径：

1. Windows 安装 `usbipd-win`
2. 用 `usbipd` 把 USB 串口设备附加到当前 WSL2
3. 在 WSL2 中确认出现：
   - `/dev/ttyUSB0`
   - 或 `/dev/ttyACM0`
4. 用项目脚本烧录，而不是手工调用 SDK 默认 flash

## 推荐操作序列

### Windows 侧

管理员 PowerShell：

```powershell
winget install --interactive --exact dorssel.usbipd-win
usbipd list
usbipd bind --busid <BUSID>
usbipd attach --wsl --busid <BUSID>
```

说明：

- `<BUSID>` 使用 `usbipd list` 中目标 USB 串口设备的总线号
- 设备名称常见为 `USB Serial`、`CP210x`、`CH340`、`FTDI`、`UART Bridge`

### WSL2 侧

```bash
dmesg | tail -n 30
ls -l /dev/ttyUSB* /dev/ttyACM* /dev/ttyS* 2>/dev/null
```

优先选择：

- `/dev/ttyUSB0`
- `/dev/ttyACM0`

如果必须验证传统映射，再单独试：

```bash
stty -F /dev/ttyS2 -a
```

如果仍是 `Input/output error`，不要再用 `ttyS2` 做烧录口。

### 项目烧录命令

```bash
cd /root/ameba-river
source env.sh
python3 tools/river_flash.py -p /dev/ttyUSB0 --log-level debug
```

如果实际设备是 `ttyACM0`，把端口替换即可。

## 必查点

### 1. 确认实际使用的是项目 profile

项目脚本会打印：

```text
[river_flash] profile=/root/ameba-river/board/rtl8730e/profiles/RTL8730E_NOR.rdev
```

如果不是这条项目 profile，而是 SDK 自带 profile，就要先修正烧录入口。

### 2. 确认下载的是项目主镜像目录

项目脚本会打印：

```text
[river_flash] image_dir=/root/ameba-river/build_RTL8730E/build/project_hp/image
```

当前项目常用产物：

- `km4_boot_all.bin`
- `km0_km4_ca32_app.bin`

不要在 GUI 里手动拼错镜像列表或地址。

## 快速判断逻辑

如果出现 `0xE8`：

1. 先看是否误用了 SDK stock profile
2. 再看 WSL2 里的串口节点是否真的可用
3. 最后才考虑镜像是否超出项目 profile

本次排查里，第 3 条已经被排除。

## 相关文件

- `build.md`
- `tools/river_flash.py`
- `board/rtl8730e/profiles/RTL8730E_NOR.json`
- `board/rtl8730e/profiles/RTL8730E_NOR.rdev`
- `board/rtl8730e/profiles/RTL8730E_NOR.sdk.json`
