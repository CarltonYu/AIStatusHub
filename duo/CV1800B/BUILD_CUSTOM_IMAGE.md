# 定制 MilkV Duo 镜像（无摄像头 / 小核 8MB）

## 修改汇总

### 1. 内存分配（已改）

**文件**: `build/boards/cv180x/cv1800b_milkv_duo_sd/memmap.py`

| 参数 | 原值 | 新值 |
|------|------|------|
| `FREERTOS_SIZE` | 768KB | **8MB** |
| `ION_SIZE` | ~26.8MB | **0MB** |

- Linux 可用内存：64MB - 8MB = **56MB**
- 小核（C906L FreeRTOS）内存：**8MB**（原 768KB）
- 摄像头/多媒体内存：**0MB**（彻底关闭）

### 2. Linux 内核配置（已改）

**文件**: `build/boards/cv180x/cv1800b_milkv_duo_sd/linux/cvitek_cv1800b_milkv_duo_sd_defconfig`

已注释掉以下配置：
- `CONFIG_CMA`
- `CONFIG_ION` / `CONFIG_ION_SYSTEM_HEAP` / `CONFIG_ION_CARVEOUT_HEAP` / `CONFIG_ION_CMA_HEAP`
- `CONFIG_DMA_CMA`
- `CONFIG_CMA_SIZE_MBYTES`

### 3. Rootfs Overlay 清理（已删）

从 `device/milkv-duo-sd/overlay/` 删除了摄像头相关文件：
- `camera-test.sh`
- `sample_vi_fd`（人脸检测二进制）
- `libcviruntime.so`, `libcvikernel.so`
- `sensor_cfg_*.ini`（sensor 配置文件）
- `*.cvimodel`（AI 模型文件）

## 编译步骤（必须在 Linux x86_64 上执行）

```bash
cd duo-buildroot-sdk

# 1. 确保 host-tools 已下载
if [ ! -d host-tools ]; then
    git clone https://github.com/milkv-duo/host-tools.git
fi

# 2. 编译 milkv-duo-sd（SD 卡版本）
./build.sh milkv-duo-sd

# 3. 产出镜像
# out/milkv-duo-sd_YYYY-MMDD-HHMM.img
```

编译耗时：30~120 分钟（取决于机器性能）。

## 烧录到 SD 卡

### macOS

```bash
# 1. 插入 SD 卡，确认设备号（如 /dev/disk2，注意不是系统盘！）
diskutil list

# 2. 卸载 SD 卡（不要弹出）
diskutil unmountDisk /dev/disk2

# 3. 烧录（替换 xxxx 为实际文件名，注意 rdisk2 比 disk2 更快）
sudo dd if=out/milkv-duo-sd_xxxx.img of=/dev/rdisk2 bs=1m status=progress

# 4. 弹出
diskutil eject /dev/disk2
```

### Linux

```bash
lsblk  # 确认 SD 卡设备，如 /dev/sdb
sudo dd if=out/milkv-duo-sd_xxxx.img of=/dev/sdb bs=1M status=progress
sync
```

## 注意事项

1. **小核固件**：默认的 FreeRTOS 固件（`fw_freertos.bin`）会继续加载，但运行在 8MB 的内存区域中。如需自定义小核程序，在 `freertos/cvitek/` 下开发并重新编译。

2. **ION 关闭影响**：ION 内存池关闭后，SoC 的 ISP/Codec/TPU 等硬件加速模块无法使用。如果你后续需要 AI 推理或视频编解码，需重新编译开启 ION。

3. **mailbox 保留**：`CONFIG_CVI_MAILBOX=y` 已保留，大小核通信功能正常。

4. **SPI 屏幕**：
   - ST7789V 240×320 屏注册为 `/dev/fb0`，开机显示 Linux 控制台（fbcon/getty）。
   - GC9A01 240×240 圆形表情屏由 `/dev/spidev0.1` 用户态驱动，开机自动启动 `gc9a01-face-daemon`。
   - `gc9a01-face-daemon` 监听 UDP 25250，支持 `default <expr>` / `normal` / `play <expr>` 等命令，与 AIStatusHub 的 `irisoled-udp` 输出协议兼容。
   - 源码与构建脚本位于 `duo/CV1800B/gc9a01-face-daemon/`。
