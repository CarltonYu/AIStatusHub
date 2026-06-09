# MilkV Duo (CV1800B) 镜像编译指南

## 概述

在 **macOS x86_64** 上通过 **Lima VM** (Ubuntu 22.04) 编译 MilkV Duo 的自定义 Buildroot SD 卡镜像。

**核心修改：**
- `spidev.bufsiz=65536`：支持单帧 240×240 SPI 传输
- `FREERTOS_SIZE=1MB`：小核只留 1MB，Linux 大核得 63MB
- ION/CMA 关闭：不分配摄像头多媒体内存
- **GC9A01 / ST7789V framebuffer 驱动**：内核原生支持，接入即出 `/dev/fb0`

---

## 1. 环境准备

### 1.1 宿主机 (macOS)

```bash
# 安装 Lima
brew install lima

# 启动 Ubuntu 22.04 VM（首次会自动下载镜像）
limactl start --name=duo-builder template://ubuntu-lts

# 进入 VM
limactl shell duo-builder
```

### 1.2 VM 内安装依赖

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential bison flex libssl-dev bc \
  python3 python3-pip \
  git wget curl unzip \
  libncurses5-dev libncursesw5-dev \
  device-tree-compiler \
  cpio rsync file texinfo \
  gawk chrpath diffstat \
  zstd lz4 lzma \
  cmake expect mtools genext2fs parted
```

### 1.3 下载交叉编译器 (host-tools)

在 **macOS 宿主机** 上执行（项目目录已被 Lima 挂载）：

```bash
cd duo/CV1800B/duo-buildroot-sdk

# 如未下载
wget https://sophon-file.sophon.cn/sophon-prod-s3/drive/23/03/07/16/host-tools.tar.gz
tar xzf host-tools.tar.gz
```

---

## 2. 关键：Lima VM 文件系统限制

Lima 通过 **virtiofs/9p** 挂载 macOS 目录到 VM，**不支持 Linux 硬链接 (hard-link) 和 setuid 位**。这会导致 buildroot 的 `rsync` 和 `tar` 操作失败。

**解决方案：** 将编译输出、rootfs 提取、genimage 临时目录全部重定向到 VM **本地 ext4 磁盘** (`/tmp/...`)，再用符号链接指向项目目录。

以下 patch 已包含在仓库中，无需手动修改：

| Patch 文件 | 作用 |
|-----------|------|
| `build/Makefile` (sd_image 目标) | rootfs 提取到 `/tmp/br-rootfs-local` |
| `device/gen_burn_image_sd.sh` | genimage tmp 重定向到 `/tmp/genimage-tmp` |
| `buildroot-2021.05/configs/milkv-duo-sd_musl_riscv64_defconfig` | 关闭 `BR2_PER_PACKAGE_DIRECTORIES` |

> 如需手动创建符号链接：
> ```bash
> # buildroot output
> rm -rf /tmp/duo-buildroot-output && mkdir -p /tmp/duo-buildroot-output
> ln -sfn /tmp/duo-buildroot-output duo-buildroot-sdk/buildroot-2021.05/output
>
> # rootfs extract
> rm -rf /tmp/br-rootfs-local && mkdir -p /tmp/br-rootfs-local
> ```

---

## 3. 全量编译

```bash
# 进入项目目录（VM 内已挂载 macOS 目录）
cd /Users/carlton/Documents/code/self/AIStatusHub.git/duo/CV1800B/duo-buildroot-sdk

# 加载环境
source build/envsetup_milkv.sh

# 选择板级配置
defconfig cv1800b_milkv_duo_sd

# 开始全量编译（首次 30-90 分钟）
build_all
```

编译流程：
1. u-boot
2. linux kernel（含 spidev patch、GC9A01/ST7789V 驱动）
3. osdrv / middleware
4. buildroot rootfs
5. 打包 boot.sd / fip.bin

输出路径：
```
install/soc_cv1800b_milkv_duo_sd/
├── fip.bin          # bootloader
├── rawimages/
│   └── boot.sd      # kernel + dtb + initramfs
└── rootfs.tar.xz    # buildroot rootfs
```

---

## 4. 手动打包 SD 镜像

SDK 自带的 `genimage` 版本较旧，不支持 `hdimage` 的 `size` 参数，且 `genext2fs` 会 segfault。使用以下手动打包脚本：

```bash
# 在 VM 内执行
OUT=install/soc_cv1800b_milkv_duo_sd
BOOT="$OUT/boot.vfat"
ROOTFS="$OUT/rootfs.ext4"

# 1. 创建 boot.vfat（128MB）
rm -f "$BOOT"
dd if=/dev/zero of="$BOOT" bs=1M count=128
mkfs.vfat -F 32 -n boot "$BOOT"
mcopy -i "$BOOT" "$OUT/fip.bin" ::/
mcopy -i "$BOOT" "$OUT/rawimages/boot.sd" ::/

# 2. 创建 rootfs.ext4（256MB）
# rootfs 已从 buildroot 的 rootfs.tar.xz 提取到 /tmp/br-rootfs-local
rm -f "$ROOTFS"
dd if=/dev/zero of="$ROOTFS" bs=1M count=256
mke2fs -t ext4 -d /tmp/br-rootfs-local "$ROOTFS"

# 3. 组装 SD 卡镜像（400MB）
IMG_TMP=/tmp/milkv-duo-sd.img
rm -f "$IMG_TMP"
dd if=/dev/zero of="$IMG_TMP" bs=1M count=400

parted -s "$IMG_TMP" mklabel msdos
parted -s "$IMG_TMP" mkpart primary fat32 1MiB 129MiB
parted -s "$IMG_TMP" mkpart primary ext4 129MiB 385MiB
parted -s "$IMG_TMP" set 1 boot on

# 写入分区（offset 单位 512B 扇区）
dd if="$BOOT"   of="$IMG_TMP" bs=512 seek=2048   conv=notrunc
dd if="$ROOTFS" of="$IMG_TMP" bs=512 seek=263168 conv=notrunc

mv -f "$IMG_TMP" "$OUT/milkv-duo-sd.img"
```

最终镜像：
```
install/soc_cv1800b_milkv_duo_sd/milkv-duo-sd.img
```

分区布局：
| 分区 | 类型 | 大小 | 内容 |
|------|------|------|------|
| p1 | FAT32 (0x0C) | 128MB | fip.bin + boot.sd |
| p2 | ext4 (0x83) | 256MB | buildroot rootfs |

---

## 5. 烧录到 SD 卡

在 **macOS 宿主机** 上：

```bash
# 1. 找到 SD 卡设备（例如 /dev/disk4）
diskutil list

# 2. 卸载但不弹出
diskutil unmountDisk /dev/disk4

# 3. 写入镜像（用 rdisk 加速）
sudo dd if=duo/CV1800B/duo-buildroot-sdk/install/soc_cv1800b_milkv_duo_sd/milkv-duo-sd.img \
  of=/dev/rdisk4 bs=1m status=progress

# 4. 同步并弹出
sync
diskutil eject /dev/disk4
```

---

## 6. 板上验证

插卡上电，SSH 登录：

```bash
# 1. spidev 缓冲区
cat /sys/module/spidev/parameters/bufsiz
# → 65536

# 2. 内存总量
cat /proc/meminfo | grep MemTotal
# → ~65024 kB (63MB)

# 3. 无 ION/CMA
dmesg | grep -i ion    # 无输出
dmesg | grep -i cma    # 无输出

# 4. framebuffer 设备（接上 GC9A01 或 ST7789V）
ls /dev/fb*
# → /dev/fb0

# 5. 屏幕初始化日志
dmesg | grep -E "fbtft|gc9a01|st7789v"
```

---

## 7. 内核 / 设备树增量编译

如果仅修改了内核源码或设备树（如调整 GC9A01 引脚），无需全量编译：

```bash
cd duo/CV1800B/duo-buildroot-sdk
source build/envsetup_milkv.sh
defconfig cv1800b_milkv_duo_sd

# 只编译 kernel + dtb + boot，不重新编译 buildroot
build_kernel
```

然后重新执行第 4 节的打包步骤即可。

---

## 8. 已修改文件清单

| 文件 | 修改内容 | 目的 |
|------|---------|------|
| `linux_5.10/drivers/spi/spidev.c` | `bufsiz = 65536` | 大帧 SPI 传输 |
| `linux_5.10/drivers/staging/fbtft/fb_gc9a01.c` | **新增** GC9A01 驱动 | 240×240 圆屏支持 |
| `linux_5.10/drivers/staging/fbtft/Makefile` | 添加 `fb_gc9a01.o` | 编译新驱动 |
| `linux_5.10/drivers/staging/fbtft/Kconfig` | 添加 `FB_TFT_GC9A01` | 内核配置项 |
| `linux_5.10/arch/riscv/boot/dts/cvitek/cv1800b_milkv_duo_sd.dts` | 添加 `gc9a01@0` + `st7789v@1` 节点 | 设备树绑定 |
| `build/boards/cv180x/cv1800b_milkv_duo_sd/linux/cvitek_cv1800b_milkv_duo_sd_defconfig` | `FB_TFT_GC9A01=y`, `FB_TFT_ST7789V=y` | 启用驱动 |
| `build/boards/cv180x/cv1800b_milkv_duo_sd/memmap.py` | `FREERTOS_SIZE = 1MB` | Linux 得 63MB |
| `build/Makefile` | rootfs 提取到 `/tmp/br-rootfs-local` | 绕过 virtiofs setuid 限制 |
| `device/gen_burn_image_sd.sh` | genimage tmp 到 `/tmp/genimage-tmp` | 绕过 virtiofs 限制 |
| `buildroot-2021.05/configs/..._defconfig` | 关闭 `BR2_PER_PACKAGE_DIRECTORIES` | 绕过硬链接限制 |
| `linux_5.10/drivers/staging/android/ion/cvitek/Makefile` | 添加 `obj-$(CONFIG_ION)` 守卫 | 修复 ION 编译错误 |

---

## 9. 接线参考

### GC9A01 (240×240 圆屏) — SPI2 CS0

| 功能 | GP 引脚 |
|------|---------|
| CS | GP6 (硬件 CS0) |
| SCK | GP7 |
| MOSI | GP8 |
| RST | GP10 |
| DC | GP11 |
| LED/BL | GP22 |

### ST7789V (240×320 方屏) — SPI2 CS1 (GPIO)

| 功能 | GP 引脚 |
|------|---------|
| CS | GP14 (GPIO CS) |
| SCK | GP7 |
| MOSI | GP8 |
| RST | GP12 |
| DC | GP13 |
| LED/BL | GP22 |

> 两个屏幕可独立使用，LED 背光共用 GP22。
