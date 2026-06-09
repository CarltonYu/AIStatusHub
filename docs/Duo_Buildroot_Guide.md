# MilkV Duo Buildroot 编译指南（macOS → Linux 模拟器）

## 目标

在 macOS 上通过 Docker 运行 Linux，编译自定义的 MilkV Duo Buildroot 镜像，解决以下问题：

1. **spidev 缓冲区太小** → 240×240 眼睛动画只能跑 ~15 FPS
2. **内存分配** → 小核只留 1MB，其余全给 Linux 大核
3. **摄像头内存不划分** → ION/CMA 已关闭，overlay 已清理

## 已完成的 SDK 修改

以下修改已在 `duo/CV1800B/duo-buildroot-sdk` 中完成，无需在 Linux 中再次修改：

### 1. spidev 缓冲区增大
**文件**: `linux_5.10/drivers/spi/spidev.c`  
**修改**: `bufsiz` 从 `4096` → `65536`

```c
static unsigned bufsiz = 65536;  /* Increased for GC9A01 240x240 SPI frame transfers */
```

这样 `eye-anim` 传 240×240 帧（115200 字节）时只需 **1 次 ioctl**，配合 20MHz SPI 可稳定 **30+ FPS**。

### 2. 小核内存缩减
**文件**: `build/boards/cv180x/cv1800b_milkv_duo_sd/memmap.py`  
**修改**: `FREERTOS_SIZE` 从 `8 * SIZE_1M` → `1 * SIZE_1M`

```python
FREERTOS_SIZE = 1 * SIZE_1M  # Minimal FreeRTOS region; Linux gets 63MB
```

- 总 DRAM: 64MB
- FreeRTOS (小核): 1MB
- Linux (大核): **63MB**

### 3. 摄像头/多媒体内存关闭
**文件**: `build/boards/cv180x/cv1800b_milkv_duo_sd/memmap.py`  
**状态**: 已关闭（`ION_SIZE = 0` 等）

**文件**: `build/boards/cv180x/cv1800b_milkv_duo_sd/linux/cvitek_cv1800b_milkv_duo_sd_defconfig`  
**状态**: 已关闭

```
# CONFIG_CMA is not set
# CONFIG_ION is not set
# CONFIG_DMA_CMA is not set
```

**overlay 状态**: `device/milkv-duo-sd/overlay` 中无 camera/ISP/VENC 相关文件。

---

## 环境准备：macOS → Docker Linux

### 1. 安装 Docker Desktop

```bash
brew install --cask docker
# 或者从 https://www.docker.com/products/docker-desktop 下载
```

启动 Docker Desktop，确保状态栏图标显示 🐳 且状态为 "Running"。

### 2. 创建编译容器

在项目根目录打开终端：

```bash
cd /path/to/AIStatusHub.git

# 启动 Ubuntu 容器，挂载当前项目目录
docker run -it --name duo-builder \
  -v "$(pwd):/workspace" \
  -w /workspace \
  ubuntu:22.04 bash
```

容器内先换国内源并安装依赖（以下命令在容器内执行）：

```bash
# 换阿里云源（可选，加速下载）
sed -i 's/archive.ubuntu.com/mirrors.aliyun.com/g' /etc/apt/sources.list
apt-get update

# 安装编译工具链
apt-get install -y \
  build-essential bison flex libssl-dev bc \
  python3 python3-pip python3-venv \
  git wget curl unzip \
  libncurses5-dev libncursesw5-dev \
  device-tree-compiler \
  cpio rsync file texinfo \
  gawk chrpath diffstat \
  zstd lz4

# 退出容器时保持运行（Ctrl+P, Ctrl+Q）
# 或者 exit 后重新进入：docker start -i duo-builder
```

### 3. 下载 host-tools（交叉编译器）

在 **macOS 宿主机** 上执行（因为 Duo SDK 的 `host-tools` 需要预先下载）：

```bash
cd duo/CV1800B/duo-buildroot-sdk

# 如果尚未下载 host-tools
wget https://sophon-file.sophon.cn/sophon-prod-s3/drive/23/03/07/16/host-tools.tar.gz
tar xzf host-tools.tar.gz
```

> 如果仓库里已经有 `host-tools` 目录，跳过此步。

---

## 编译步骤

以下命令全部在 **Docker 容器内** 执行。

### 1. 进入 SDK 目录

```bash
cd /workspace/duo/CV1800B/duo-buildroot-sdk
```

### 2. 加载环境变量

```bash
source build/envsetup_milkv.sh
```

### 3. 选择板级配置

```bash
defconfig cv1800b_milkv_duo_sd
```

输出应包含：
```
BR_DEFCONFIG=milkv-duo-sd_musl_riscv64_defconfig
...
Output path: /workspace/duo/CV1800B/duo-buildroot-sdk/install/soc_cv1800b_milkv_duo_sd
```

### 4. 开始全量编译

```bash
build_all
```

首次编译时间较长（约 30–90 分钟，取决于机器性能和网络），会依次构建：
- u-boot
- linux kernel（含我们修改的 spidev.c）
- osdrv
- middleware（已移除 camera 相关）
- buildroot rootfs
- 打包 boot.sd / fip.bin / SD 卡镜像

编译完成后输出文件在：
```
/workspace/duo/CV1800B/duo-buildroot-sdk/install/soc_cv1800b_milkv_duo_sd/
```

### 5. 生成 SD 卡烧录镜像

```bash
pack_sd_image
```

生成的最终镜像：
```
/workspace/duo/CV1800B/duo-buildroot-sdk/install/soc_cv1800b_milkv_duo_sd/milkv-duo-sd.img
```

---

## 烧录步骤

### 1. 取出 Duo 的 SD 卡，插入 Mac

### 2. 找到 SD 卡设备名

```bash
diskutil list
```

找到类似 `/dev/disk4` 的设备（确认容量匹配，例如 16GB/32GB）。

### 3. 卸载但不弹出

```bash
diskutil unmountDisk /dev/disk4
```

### 4. 写入镜像

```bash
# 将 .img 路径替换为实际路径
sudo dd if=/path/to/AIStatusHub.git/duo/CV1800B/duo-buildroot-sdk/install/soc_cv1800b_milkv_duo_sd/milkv-duo-sd.img \
  of=/dev/rdisk4 bs=1m status=progress

# 写入完成后同步
sync
```

> 注意：使用 `/dev/rdisk4`（带 r 的裸设备）比 `/dev/disk4` 快很多。

### 5. 弹出 SD 卡，插回 Duo，上电启动

---

## 编译后验证

启动 Duo 后，SSH 登录并检查：

```bash
# 1. 确认 spidev bufsiz 已增大
cat /sys/module/spidev/parameters/bufsiz
# 期望输出: 65536

# 2. 确认内存分配
cat /proc/meminfo | grep MemTotal
# 期望约: 63MB (65024 kB 左右)

# 3. 确认没有摄像头 ION/CMA
dmesg | grep -i ion
dmesg | grep -i cma
# 期望无输出或显示 disabled

# 4. 运行 eye-anim 240×240 测帧率
/root/eye-anim /root/eye_animation.bin 30 20000000
# 期望 Actual FPS: 29+ 
```

---

## 常见问题

### Q: Docker 编译时提示缺少 `makeinfo` / `texinfo`
```bash
apt-get install -y texinfo
```

### Q: 编译中途断网，如何续编？
```bash
# 重新进入容器
docker start -i duo-builder
cd /workspace/duo/CV1800B/duo-buildroot-sdk
source build/envsetup_milkv.sh
defconfig cv1800b_milkv_duo_sd
build_all   # 会自动续编
```

### Q: 只想修改 kernel 配置（如再加一个驱动）
```bash
source build/envsetup_milkv.sh
defconfig cv1800b_milkv_duo_sd
kernel_menuconfig   # 图形化配置
build_all
```

### Q: 镜像太大，SD 卡写不下？
`milkv-duo-sd.img` 默认约 400MB，官方 16GB/32GB SD 卡足够。如想精简 rootfs，可修改：
```bash
# 容器内
vi /workspace/duo/CV1800B/duo-buildroot-sdk/buildroot-2021.05/configs/milkv-duo-sd_musl_riscv64_defconfig
# 注释掉不需要的 BR2_PACKAGE_* 包，然后重新 build_all
```

---

## 附录：修改文件清单

| 文件 | 修改内容 | 目的 |
|------|---------|------|
| `linux_5.10/drivers/spi/spidev.c` | `bufsiz = 65536` | 大帧 SPI 传输 |
| `build/boards/cv180x/cv1800b_milkv_duo_sd/memmap.py` | `FREERTOS_SIZE = 1MB` | Linux 得 63MB |
| `build/boards/cv180x/cv1800b_milkv_duo_sd/linux/cvitek_cv1800b_milkv_duo_sd_defconfig` | `# CONFIG_ION is not set`, `# CONFIG_CMA is not set` | 关闭摄像头内存 |
| `device/milkv-duo-sd/overlay/` | 移除 camera/ISP 相关文件 | 清理无用文件 |
