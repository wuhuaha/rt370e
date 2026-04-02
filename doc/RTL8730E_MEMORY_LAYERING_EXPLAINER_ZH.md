# RTL8730E 板端内存分层说明

## 1. 结论先说

板子“有 `64M` 内存”和当前日志里“只有几百 `KB` 可用 heap”并不矛盾。

这里说的是两件不同的事：

- `64M`：板级物理外部 DRAM/PSRAM 容量
- `heap_free`：当前固件布局下，当前 `CA32` 应用运行时还剩下的 `FreeRTOS heap`

更准确地说，当前系统卡住的不是“板子总内存不够”，而是：

- 当前固件给 `CA32` 应用开放的可用 DRAM 窗口有限
- 这块窗口里还要先放代码段、数据段、BSS、栈、页表、nocache 段
- 剩下才会进入 heap
- 启动后 Wi-Fi、音频、KWS、VAD、任务栈、队列、云链路等还会继续消耗 heap
- TFLM arena 还要求“大块连续内存”，不是只看总 free bytes

## 2. 为什么会产生误解

最容易混淆的是把下面几层当成同一件事：

1. 板子物理上焊了多少内存
2. 当前固件布局实际映射了多少内存
3. 当前 `CA32` 核实际分到了多少地址空间
4. 这块地址空间里有多少被静态段占掉
5. 剩下多少被注册成 `FreeRTOS heap`
6. 运行到某个时刻还剩多少 heap
7. 剩余 heap 中最大的连续空闲块有多大

只看第 `1` 层会得出“64M 很大”；而模型部署、`malloc failed`、TFLM arena 成败，通常取决于第 `5` 到第 `7` 层。

## 3. 当前板端的内存分层图

```text
物理 DRAM/PSRAM
64MB
0x60000000 ~ 0x64000000
|
|  当前固件布局先按较小窗口使用
v
布局窗口
8MB
0x60000000 ~ 0x60800000
|
|-- KM4 / secure / other reserved regions
|-- CA32_BL1_DRAM_S
`-- CA32_BL3_DRAM_NS
    0x60300000 ~ 0x60700000
    4MB
    |
    |-- .text / .rodata / vectors
    |-- .data
    |-- .bss
    |-- .stack
    |-- .nocache.data
    |-- xlat_table / mmu tables
    `-- 剩余尾段 -> __psram_heap_buffer_start__ ~ region end
                    |
                    v
               FreeRTOS heap region
                    |
                    |-- Wi-Fi
                    |-- Audio
                    |-- Playback cache
                    |-- KWS context + tensor arena
                    |-- VAD context + tensor arena
                    |-- tasks / queues / buffers / sockets
                    `-- 运行时剩余 heap_free
```

## 4. 当前代码和日志里的直接证据

### 4.1 物理容量比当前布局大

你给出的 boot log 里已经明确提示：

```text
PSRAM or DRAM End in layout is 0x60800000, but actually is 0x64000000
```

这句话的含义是：

- 当前布局把可见末尾先放在 `0x60800000`
- 但硬件实际探测到的末尾是 `0x64000000`

也就是：

- 物理 DRAM：`0x60000000 ~ 0x64000000`，共 `64MB`
- 当前布局窗口：`0x60000000 ~ 0x60800000`，共 `8MB`

这已经说明“物理容量”和“当前固件实际使用窗口”不是一回事。

### 4.2 当前 normal 布局下，CA32 非安全 DRAM 只有 4MB

布局文件中可以看到：

- `PSRAM_END = 0x60800000`
- `CA32_BL3_DRAM_NS : ORIGIN = 0x60300000, LENGTH = 0x60700000 - 0x60300000 /* 4MB */`

对应文件：

- `/root/ameba-rtos-1.2/component/soc/amebasmart/project/ameba_layout.ld`

这意味着当前 `CA32` 主应用并不是在 64MB 上自由分配，而是在一个 `4MB` 的 carveout 里活动。

### 4.3 这 4MB 里也不是全给 heap

`CA32` 链接脚本里，以下段都会先进 `CA32_BL3_DRAM_NS`：

- `.code`
- `.data`
- `.bss`
- `.heap`
- `.stack`
- `.nocache.data`
- `.xlat_table`

对应文件：

- `/root/ameba-rtos-1.2/component/soc/amebasmart/project/project_ap/ld/ameba_img2_xip.ld`

脚本最后才把尾部剩余空间导出为：

- `__psram_heap_buffer_start__`
- `__psram_heap_buffer_size__`

也就是说：

- 不是 “先有一个大 heap，再从 heap 里切代码/数据”
- 而是 “先把静态布局放进去，最后剩下的尾巴才变成 heap”

### 4.4 FreeRTOS heap 不是“自动等于整块 CA32 DRAM”

SDK 用的是 `heap_5`，它只会把显式注册的 region 当 heap：

- `os_heap_add(...)`
- `vPortDefineHeapRegions(...)`

对应文件：

- `/root/ameba-rtos-1.2/component/os/freertos/freertos_heap5_config.c`

对于当前 `CA32` 用户堆配置，核心映射是：

- `PSRAM_HEAP0_START = __psram_heap_buffer_start__`
- `PSRAM_HEAP0_SIZE = __psram_heap_buffer_size__`

对应文件：

- `/root/ameba-rtos-1.2/component/soc/usrcfg/amebasmart/include/ameba_userheapcfg_dram.h`

所以“系统 heap”本质上只是链接脚本尾段，而不是整板物理内存。

### 4.5 当前日志里的几百 KB 是运行时剩余 heap，不是总内存

项目里的 runtime stats 取值来自：

- `rtos_mem_get_free_heap_size()`
- `rtos_mem_get_minimum_ever_free_heap_size()`

对应文件：

- `/root/ameba-river/components/river_common/river_runtime_stats.c`

历史日志里已经有典型数值：

- `boot_ready heap_free ≈ 185216B`
- `wifi_connected heap_free ≈ 132736B`

它们表示：

- 当前通用 heap 剩余只有约 `181KB`
- 连网后只剩约 `130KB`

并不表示整板只剩这点内存。

## 5. 为什么 4MB 看起来也不像只剩 100 多 KB

这是第二个常见误区。

很多人会继续问：

- 既然 `CA32_BL3_DRAM_NS` 有 `4MB`
- 为什么最后只看到 `100~200KB` 级别的 `heap_free`

原因是这 `4MB` 并不只是“应用正文 + 一个 heap”。

它至少还包含：

- `CA32` 代码和只读数据
- 全局/静态数据
- `BSS`
- 栈
- 页表/地址转换表
- nocache 区
- FreeRTOS 对象
- 应用侧长期驻留 buffer
- 运行过程中创建的任务、队列、socket、音频对象
- KWS/VAD/播放/云链路的动态分配

此外，boot log 中的镜像加载信息：

```text
Image id=5 loaded: 0x60300000 - 0x605e2aac
```

只能说明“镜像装载内容”的覆盖范围。

它不代表：

- `0x605e2aac ~ 0x60700000` 全是可用 heap

因为 `.bss`、`.stack`、`.nocache`、`.xlat_table` 等 `NOLOAD` 段不一定体现在这个 “loaded bytes” 输出里，但它们同样占地址空间。

## 6. 内存类型还有区分

SDK 明确区分：

- `TYPE_TCM`
- `TYPE_SRAM`
- `TYPE_DRAM`

其中 `TYPE_DRAM` 的注释是：

- `include PSRAM and DDR, only can malloc heap after 0x60000000`

对应文件：

- `/root/ameba-rtos-1.2/component/os/os_wrapper/include/os_wrapper_memory.h`

这说明在这个平台上，不同内存类型并不是统一池子。

某块内存在物理上存在，不代表：

- 已经映射为当前核可分配区域
- 已经进入当前 allocator 管理
- 可以满足当前调用指定的类型约束

## 7. 为什么“还有 free”仍然可能申请失败

因为总空闲和最大连续空闲块不是一回事。

FreeRTOS 的 `HeapStats_t` 自己就把这两个概念分开了：

- `xAvailableHeapSpaceInBytes`
- `xSizeOfLargestFreeBlockInBytes`

对应文件：

- `/root/ameba-rtos-1.2/component/os/freertos/freertos_v10.4.3/Source/include/portable.h`

这意味着：

- `heap_free = 132KB`
- 不代表一定能成功申请一个 `96KB` 或 `128KB` 的连续块

如果堆已经碎片化，最大的连续块可能远小于总 free。

而 TFLM 的 tensor arena 恰恰通常需要一整块大的连续内存，所以在板端部署模型时经常会出现：

- 看上去还有内存
- 但 arena 申请失败

## 8. 这和当前 KWS / VAD 链路的关系

当前工程里：

- KWS arena 走 `TYPE_DRAM`
- Silero VAD arena 也走 `TYPE_DRAM`

对应文件：

- `/root/ameba-river/components/river_voice/river_voice_kws.cc`
- `/root/ameba-river/components/river_voice/river_voice_detector_silero.cc`

同时，云链路侧已经有明显的低内存保护逻辑：

- `RIVER_XIAOZHI_CONNECT_HEAP_RECLAIM_THRESHOLD (64U * 1024U)`

对应文件：

- `/root/ameba-river/components/river_cloud/river_xiaozhi_ws.c`

这说明当前产品链路本来就在低 heap 余量附近运行，而不是“还有几十 MB 可随便用”。

## 9. 对当前分支的工程结论

对这块板子和当前分支，应该用下面的理解方式：

### 9.1 正确理解

- 板子有 `64MB` 物理 DRAM
- 当前固件布局只显式使用了其中一部分
- 当前 `CA32` 应用只拿到其中一个更小的 carveout
- 这个 carveout 又不是全做 heap
- 最后运行时剩余 heap 只有 `100~200KB` 级是完全可能的

### 9.2 错误理解

- “板子有 64M，所以模型大一点肯定没问题”
- “heap_free 只有 130KB，说明板子其实没有 64M”

这两句都不对。

## 10. 后续分析/调优时应该优先看什么

如果后面继续评估模型部署可行性，优先看下面几项，而不是只看“板子总内存”：

1. `boot_ready heap_free`
2. `wifi_connected heap_free`
3. `xSizeOfLargestFreeBlockInBytes`
4. KWS/VAD/播放/云链路各模块的长期驻留内存
5. arena 是否要求连续大块
6. 当前布局是否真的把更大 DRAM 窗口交给了 `CA32`

## 11. 一句话总结

“`64M` 是物理资源上限，`heap_free` 是当前这套固件给当前应用留下的即时可分配余额。”

当前瓶颈不在“板子有没有 64M”，而在“当前 CA32 固件布局和运行时动态分配之后，还剩多少连续 DRAM heap 可以给 KWS/VAD/TFLM 使用”。
