# ameba-river 构建说明

这份文档只记录当前项目稳定、低熵、已经验证过的构建和烧录入口。
活动分支、当前目标和最新验证步骤请看 `.codex/active_context.md`。

## 1. 环境

- 默认 SDK 根目录：`/root/ameba-rtos`
- 项目目录：`/root/ameba-river`
- 当前目标芯片：`RTL8730E`
- 环境覆盖变量：`AMEBA_SDK_ROOT`

进入项目后，先加载环境：

```bash
cd /root/ameba-river
source env.sh
```

## 2. 标准编译命令

当前推荐直接使用：

```bash
export AMEBA_SDK_ROOT=/root/ameba-rtos
python3 "$AMEBA_SDK_ROOT/ameba.py" build -p
```

说明：

- `-p` 会并行编译
- `env.sh` 默认会回退到 `/root/ameba-rtos`
- 如果某个实验明确要求别的 SDK checkout，再单独覆盖 `AMEBA_SDK_ROOT`

## 3. 生成产物

编译成功后，常用产物位置如下：

```text
/root/ameba-river/build_RTL8730E/build/project_hp/image/km4_boot_all.bin
/root/ameba-river/build_RTL8730E/build/project_hp/image/km0_km4_ca32_app.bin
```

其中：

- `km4_boot_all.bin`：boot 镜像
- `km0_km4_ca32_app.bin`：主应用镜像

## 4. 推荐烧录命令

项目当前使用自定义烧录脚本，不建议直接手敲 SDK 默认 `flash`
命令。

```bash
cd /root/ameba-river
export AMEBA_SDK_ROOT=/root/ameba-rtos
python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor
```

串口监视：

```bash
python3 "$AMEBA_SDK_ROOT/tools/ameba/Monitor/monitor.py" -p /dev/ttyUSB0 -b 1500000
```

补充说明：

- 当前应用镜像继续要求使用项目自定义 profile：
  - `/root/ameba-river/board/rtl8730e/profiles/RTL8730E_NOR.rdev`
- 如果使用官方 GUI 下载工具，也必须加载上面的项目 profile，而不是工具自带的默认 `RTL8730E` profile。

## 5. 常用构建流程

### 增量构建

大多数改动直接执行：

```bash
cd /root/ameba-river
export AMEBA_SDK_ROOT=/root/ameba-rtos
source env.sh
python3 "$AMEBA_SDK_ROOT/ameba.py" build -p
```

### 重新配置后构建

如果改了 `Kconfig`、`prj.conf`、组件增删或 CMake 结构，仍然先直接执行上面的标准命令即可；当前工程的构建脚本会自动重建必要目录。

## 6. 结果判断

构建成功时，末尾会出现类似：

```text
Build done
Start to build RTL8730E ...
```

并且两个镜像文件时间戳会更新：

- `build_RTL8730E/build/project_hp/image/km4_boot_all.bin`
- `build_RTL8730E/build/project_hp/image/km0_km4_ca32_app.bin`

## 7. 说明

- 当前项目不建议把临时调试命令散落在聊天记录里，后续统一以这份文档和 `.codex/active_context.md` 为准。
- 如果后续增加新的 SoC、不同 flash profile 或 CI 构建流程，再单独补充到本文件。
