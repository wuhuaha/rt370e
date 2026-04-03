# RTL8730E SDK 内存布局改动说明

日期：2026-04-03

## 1. 目的

这次改动只做一件事：

- 给 `RTL8730E` 当前 `ameba-river` 分支的 `CA32` 主应用提供更大的可用 DRAM / heap

当前问题已经不是“FP32 KWS 模型本身无法初始化”，而是：

- `FP32 KWS` 初始化成功后，`CA32` 剩余 heap 掉到约 `10KB`
- 随后在 `VAD / vad_probe` 继续启动阶段又出现新的 `malloc failed`

因此这一步优先处理平台可用内存，而不是继续在当前 `4MB CA32 carveout` 里压缩几十 `KB`。

## 2. 本次采用的方案

本次采用的是：

- **保守版扩容**

不是直接照搬 `aivoice` 的约 `17MB CA32` 方案，而是先把 `CA32` 从 `4MB` 扩到 `8MB`，同时仍然给 `KM4` 留 `1MB` 扩展区。

改动后的核心布局为：

- `PSRAM_END`: `0x60800000 -> 0x60C00000`
- `CA32_BL3_DRAM_NS`: `0x60300000 ~ 0x60700000 -> 0x60300000 ~ 0x60B00000`
- `KM4_DRAM_HEAP_EXT`: `0x60700000 ~ 0x60800000 -> 0x60B00000 ~ 0x60C00000`

## 3. 修改了哪些 SDK 文件

### 3.1 `/root/ameba-rtos-1.2/component/soc/amebasmart/project/ameba_layout.ld`

改动内容：

- 修改 `PSRAM_END`
- 扩大 `CA32_BL3_DRAM_NS`
- 顺延 `KM4_DRAM_HEAP_EXT`

这是最核心的链接布局改动。

### 3.2 `/root/ameba-rtos-1.2/component/soc/amebasmart/fwlib/include/hal_platform.h`

改动内容：

- 同步修改 `PSRAM_END`

这样可以避免平台头里的物理上界和链接脚本使用不同值。

## 4. 为什么只改这两处

这次没有去手改：

- `ATF platform_def.h`
- `ameba_userheapcfg_dram.h`
- `ameba_boot_trustzonecfg.c`

原因是：

- `platform_def.h` 通过 `PSRAM_END` 推导 `NS_DRAM0_SIZE`
- `CA32` heap 大小通过 linker 导出的 `__psram_heap_buffer_size__` 自动生效
- `TrustZone/MPC` 这侧通过 `__non_secure_psram_end__` 跟随布局末尾

也就是说，这一步优先保持最小改动面。

## 5. 为什么不直接照搬 aivoice

`aivoice` 的 SDK patch 对 `RTL8730E` 的做法是：

- `PSRAM_END -> 0x61500000`
- `CA32_BL3_DRAM_NS -> 0x60300000 ~ 0x61400000`

这个方案可参考，但本次没有直接照搬，原因是：

1. 当前先要确认“扩布局是否足以消掉当前共存内存瓶颈”
2. 如果直接改到约 `17MB`，后面一旦出现新问题，定位面更大
3. 先做 `8MB CA32` 更适合作为第一轮工程验证

如果这版仍不足，再升级到 `aivoice` 同级方案。

## 6. 仓库内的追踪方式

由于 SDK 文件不在 `ameba-river` 仓库内，这次额外在仓库中新增了一个可追踪脚本：

- [tools/sdk/apply_rtl8730e_memory_layout_patch.py](/root/ameba-river/tools/sdk/apply_rtl8730e_memory_layout_patch.py)

用途：

- 把这次 SDK 改动以“仓库内可审阅、可重复执行”的方式保留下来
- 避免后续只知道“SDK 被手改过”，但不知道改了什么

## 7. 预期收益

预期直接收益：

- `boot_ready heap_free` 明显上升
- `kws init` 完成后的剩余 heap 不再掉到 `10KB` 级别
- `silero_vad` / `vad_probe` 后续分配更有机会成功

本次改动不直接保证：

- 唤醒词识别准确率一定提升
- 所有本地语音链路问题都解决

它解决的是“平台可用内存太小”这一层。

## 8. 回退方式

如果要回退，只需把下面两处恢复到原值：

- `/root/ameba-rtos-1.2/component/soc/amebasmart/project/ameba_layout.ld`
- `/root/ameba-rtos-1.2/component/soc/amebasmart/fwlib/include/hal_platform.h`

或者直接重新应用原始 SDK 版本。

## 9. 后续验证重点

改完后优先关注这些日志：

- boot / image load / TrustZone 是否正常
- `boot_ready heap_free`
- `kws init plan` / `kws memory plan`
- `silero_vad runtime ready`
- 是否仍有 `Malloc failed ... xWantedSize:105536`

如果这些通过，再看：

- `Wi-Fi`
- `audio`
- `KWS + VAD + 云链路` 共存稳定性
