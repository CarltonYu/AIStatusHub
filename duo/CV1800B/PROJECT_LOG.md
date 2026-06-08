# MilkV Duo + GC9A01 圆形屏 开发记录

## 一、项目目标

在 MilkV Duo (CV1800B) 上运行一个接口软件，通过 SPI 控制 1.28 寸圆形 IPS 屏（GC9A01，240×240），接收不同输入时显示不同画面。

---

## 二、硬件接线（用户实际接线）

| 屏幕引脚 | Duo 引脚 | 名称 | 功能 |
|---------|---------|------|------|
| VCC | Pin 36 | 3V3(OUT) | 供电 |
| GND | — | GND | 地线 |
| SCL | Pin 9 | GP6 | SPI2_SCK |
| SDA | Pin 10 | GP7 | SPI2_SDO (MOSI) |
| DC | Pin 5 | GP3 | 数据/命令切换 |
| CS | Pin 12 | GP9 | SPI2_CS |
| RST | Pin 11 | GP8 | 复位 |

> **重要发现**：用户当前的接线**正好对应 Duo 的 SPI2 硬件接口**，不需要改线即可使用硬件 SPI。

### GPIO 编号映射（关键纠正）

此前误认为所有 GPx 都在 `gpiochip480`（base=480），实际 Duo 的 GPIO 分布在多个 bank：

| 物理引脚 | NAME | 所属 Bank | sysfs 编号 | 计算方式 |
|---------|------|----------|-----------|---------|
| Pin 5 | GP3 | PWR_GPIO | **377** | 352 + 25 |
| Pin 9 | GP6 | PWR_GPIO | **375** | 352 + 23 |
| Pin 10 | GP7 | PWR_GPIO | **374** | 352 + 22 |
| Pin 11 | GP8 | PWR_GPIO | **373** | 352 + 21 |
| Pin 12 | GP9 | PWR_GPIO | **370** | 352 + 18 |
| Pin 14 | GP10 | XGPIOC | **425** | 416 + 9 |
| Pin 15 | GP11 | XGPIOC | **426** | 416 + 10 |

> 之前测试 export `483` 成功，但 `483 = 480 + 3` 对应的是 **XGPIOA[3]**，不是物理引脚 GP3。这是屏幕始终不亮的**根本原因**。

---

## 三、开发机环境（macOS）

### 3.1 交叉编译器

MilkV 官方 `host-tools` 是 Linux x86_64 ELF 二进制，macOS 无法直接运行。

**解决方案**：安装 Zig 编译器进行交叉编译。

```bash
brew install zig
```

编译命令：
```bash
zig cc -target riscv64-linux-musl -O2 -static
```

### 3.2 文件传输工具

Duo 上是 Dropbear SSH，macOS OpenSSH 9.0+ 默认使用 SFTP 协议，但 Duo 没有 `sftp-server`。

**解决方案**：
```bash
# 强制使用旧版 SCP 协议
scp -O file root@192.168.42.1:/root/

# 或使用 sshpass 免交互
brew install sshpass
sshpass -p "milkv" scp -O file root@192.168.42.1:/root/
```

---

## 四、代码文件

### 4.1 目录结构

```
duo/CV1800B/
├── c-gc9a01/              # C 驱动代码
│   ├── gc9a01.h           # 驱动头文件（软件 SPI 版本，已废弃）
│   ├── gc9a01.c           # 驱动实现（软件 SPI，已废弃）
│   ├── main.c             # 全屏测试 demo（软件 SPI，已废弃）
│   ├── test_init.c        # 极简初始化测试（软件 SPI，已废弃）
│   ├── test_full_init.c   # 完整初始化 + 全屏白（软件 SPI，已废弃）
│   ├── test_hw_spi.c      # 硬件 SPI2 测试（当前主测试）
│   ├── Makefile           # Zig 交叉编译
│   └── README.md          # 使用说明
├── BUILD_CUSTOM_IMAGE.md  # 定制镜像编译指南
└── PROJECT_LOG.md         # 本文档
```

### 4.2 当前主测试程序

`test_hw_spi.c`：使用 `/dev/spidev0.0`（SPI2）+ sysfs GPIO（377/373/370）

- SPI 设备：`/dev/spidev0.0`
- DC：377（Pin5 / GP3）
- RST：373（Pin11 / GP8）
- CS：370（Pin12 / GP9，手动 GPIO 管理）
- SCK：硬件自动管理（Pin9 / GP6）
- MOSI：硬件自动管理（Pin10 / GP7）

程序启动时会自动执行 pinmux：

```bash
duo-pinmux -w GP3/GP3
duo-pinmux -w GP6/SPI2_SCK
duo-pinmux -w GP7/SPI2_SDO
duo-pinmux -w GP8/GP8
duo-pinmux -w GP9/GP9
```

原因：官方镜像默认可能将 GP3 配为 `UART4_RX`，GP8 配为 `SPI2_SDI`，GP9 配为 `SPI2_CS_X`。当前接线里 GP3/GP8/GP9 分别作为 DC/RST/CS GPIO 使用，必须切回 GPIO 功能。

### 4.3 编译与运行

```bash
cd duo/CV1800B/c-gc9a01
make                          # 编译 gc9a01-demo（旧版软件 SPI）
make test-hw-spi             # 编译 test-hw-spi（硬件 SPI2）

# 传输到 Duo
sshpass -p "milkv" scp -O test-hw-spi root@192.168.42.1:/root/

# 在 Duo 上运行
chmod +x /root/test-hw-spi
/root/test-hw-spi
```

---

## 五、路线 B：定制 Buildroot 镜像

### 5.1 已完成的配置修改

#### 5.1.1 内存分配

**文件**：`duo-buildroot-sdk/build/boards/cv180x/cv1800b_milkv_duo_sd/memmap.py`

| 参数 | 原值 | 修改后 | 说明 |
|------|------|--------|------|
| `FREERTOS_SIZE` | 768 KB | **8 MB** | 小核运行内存 |
| `ION_SIZE` | ~26.8 MB | **0 MB** | 摄像头/多媒体内存（关闭）|

**效果**：Linux 可用 56 MB，小核 8 MB，无摄像头内存分配。

#### 5.1.2 Linux 内核配置

**文件**：`build/boards/cv180x/cv1800b_milkv_duo_sd/linux/cvitek_cv1800b_milkv_duo_sd_defconfig`

已注释掉以下配置：
- `CONFIG_CMA`
- `CONFIG_ION` / `CONFIG_ION_SYSTEM_HEAP` / `CONFIG_ION_CARVEOUT_HEAP` / `CONFIG_ION_CMA_HEAP`
- `CONFIG_DMA_CMA`
- `CONFIG_CMA_SIZE_MBYTES`

#### 5.1.3 Rootfs Overlay 清理

从 `device/milkv-duo-sd/overlay/` 删除摄像头相关文件：
- `camera-test.sh`
- `sample_vi_fd`（人脸检测二进制）
- `libcviruntime.so`
- `libcvikernel.so`
- `sensor_cfg_*.ini`
- `*.cvimodel`

### 5.2 编译指南

Buildroot **必须在 Linux x86_64 上编译**，macOS 不支持。

```bash
cd duo-buildroot-sdk
./build.sh milkv-duo-sd
```

产出：`out/milkv-duo-sd_YYYY-MMDD-HHMM.img`

详细步骤见 `BUILD_CUSTOM_IMAGE.md`。

---

## 六、关键发现与记录

### 6.1 GPIO 分布

Duo 有 5 个 gpiochip，base 分别为 352/384/416/448/480。
- PWR_GPIO (gpiochip4, base=352)：GP0~GP9
- XGPIOC (gpiochip2, base=416)：GP10~GP15
- XGPIOA (gpiochip0, base=480)：GP16~GP27

**常见误区**：`480 + GPx` 只对 GP16+ 成立。GP0~GP9 需要用 `352 + PWR_GPIO[n]` 计算。

### 6.2 SPI 设备

Duo 上有两个 spidev（2026-06-08 真机 sysfs 复核）：
- `/dev/spidev0.0`：SPI2（`41a0000.spi2`，Pin9/10/11/12，即 GP6/GP7/GP8/GP9）
- `/dev/spidev1.0`：SPI3（`41b0000.spi3`）

用户当前接线匹配 `/dev/spidev0.0`。

### 6.3 真机 pinmux 排查记录

2026-06-08 通过 SSH 连接官方镜像设备后确认：

- `/dev/spidev0.0` 和 `/dev/spidev1.0` 存在，其中 SPI2 是 `/dev/spidev0.0`。
- gpiochip base 为 `352/384/416/448/480`，与官方文档一致。
- `duo-pinmux -l` 显示：
  - GP6 = `SPI2_SCK`
  - GP7 = `SPI2_SDO`
  - GP9 = `SPI2_CS_X`
  - GP3 = `UART4_RX`
  - GP8 = `SPI2_SDI`

因此如果程序只 export GPIO377/373，但不切 pinmux，DC/RST 的电平变化不会可靠出现在物理引脚上。这个现象符合“屏幕背光亮，但画面完全无响应”。

当前 `test-hw-spi` 已在真机运行，程序输出：

```text
SPI_IOC_WR_MODE with SPI_NO_CS: Invalid argument
warning: falling back to SPI_MODE_0 with manual GPIO CS
Resetting...
Init sequence...
Cycling red/green/blue/white...
Done!
```

其中 `SPI_NO_CS` warning 是官方内核限制，程序会自动回退到普通 SPI mode 0，并继续用 GPIO370 手动控制 LCD CS。

Python 原型 `gc9a01_driver_duo.py` 已修正为 `/dev/spidev0.0` + GPIO377/373/370，并移除 `gpiozero` 依赖；但在 Duo 上整帧 PIL 绘制/刷新 20 秒内未完成，不作为点屏主验证路径。

### 6.4 网络与文件传输

- Duo 通过 USB-RNDIS 连接 macOS，IP 为 `192.168.42.1`
- Duo 无外网（macOS 802.1X 网络无法共享）
- `scp -O` 可兼容 Dropbear 的 scp
- `sshpass` 可实现免交互传输

---

## 七、当前状态与待办

### 7.1 已完成

- [x] 交叉编译环境搭建（Zig）
- [x] GPIO 编号研究（纠正错误）
- [x] SPI 设备确认（spidev0.0 = SPI2）
- [x] 硬件 SPI2 驱动程序编写
- [x] 真机确认 pinmux 默认状态，修正 DC/RST/CS pinmux 配置
- [x] 文件传输自动化（sshpass + scp -O）
- [x] 定制镜像配置修改（内存、内核、overlay）
- [x] 新增官方 LVGL framebuffer 路线版本：`lvgl-framebuffer-gc9a01/`

### 7.2 待办

- [ ] **屏幕点亮验证**：已修正 pinmux 和 GPIO direction，需要观察 `test-hw-spi` 是否能显示红/绿/蓝/白。
- [ ] **aistatus-hub 接口程序**：接收 Unix Socket / 文件状态变化 → 切换屏幕画面。
- [ ] **优化驱动**：屏幕点亮后，将正式驱动（gc9a01.c）迁移到硬件 SPI2。
- [ ] **定制镜像编译**：在 Linux x86_64 机器上执行 `./build.sh milkv-duo-sd`。
- [ ] **小核固件**：如需自定义小核程序，在 `freertos/cvitek/` 下开发。

### 7.3 LVGL framebuffer 版本

新增目录：

```text
duo/CV1800B/lvgl-framebuffer-gc9a01/
```

该版本按 Milk-V 官方 SPI Display LVGL 思路实现：内核先把 GC9A01 注册成 `/dev/fb0`，用户态 LVGL demo 只写 Linux framebuffer。与 `test-hw-spi` 不同，该版本使用 SPI2 硬件 CS，因此 GP9/Pin12 必须保持 `SPI2_CS_X`。

核心补丁：

- `patches/0001-cv1800b-duo-gc9a01-fbtft-framebuffer.patch`
  - U-Boot 早期 pinmux：GP3->GPIO/DC，GP8->GPIO/RST，GP6/GP7/GP9->SPI2。
  - DTS：`&spi2` 下删除 `spidev@0`，新增 `gc9a01@0`。
  - Kernel：新增 `fb_gc9a01.c` fbtft 驱动并启用 `CONFIG_FB_TFT_GC9A01=y`。
- `patches/0002-duo-lvgl-fb-demo-gc9a01-240x240.patch`
  - 官方 `duo-lvgl-fb-demo` 的 framebuffer 分辨率从 `240x320` 改为 `240x240`。

已在临时干净 SDK 文件集上执行：

```text
git apply --check patches/0001-cv1800b-duo-gc9a01-fbtft-framebuffer.patch
PATCH_OK
```

2026-06-08 追加实测：

- 通过 sysfs 复核 SPI 设备映射：
  - `/sys/bus/spi/devices/spi0.0 -> 41a0000.spi2`，对应 `/dev/spidev0.0`
  - `/sys/bus/spi/devices/spi1.0 -> 41b0000.spi3`，对应 `/dev/spidev1.0`
- 已修正 `test_hw_spi.c` 和文档，将 SPI2 从 `/dev/spidev1.0` 改为 `/dev/spidev0.0`。
- 已重新编译并发送到 Duo：
  - `/root/gc9a01/test-hw-spi`
  - `/root/gc9a01/lvgl-framebuffer-gc9a01/`
- 真机运行 `/root/gc9a01/test-hw-spi` 成功，退出码 0：

```text
GC9A01 HW SPI2 test (spidev0.0)
SPI_IOC_WR_MODE with SPI_NO_CS: Invalid argument
warning: falling back to SPI_MODE_0 with manual GPIO CS
Resetting...
Init sequence...
Cycling red/green/blue/white...
Done!
TEST_HW_SPI_EXIT=0
```

- 当前官方镜像没有 `/dev/fb0`，运行 framebuffer 烟测程序符合预期失败：

```text
open /dev/fb0: No such file or directory
FB_FILL_EXIT=1
```

- 尝试在官方镜像上运行时把 SPI2 从 `spidev` 临时绑定到内置 `fb_st7789v`，内核拒绝 probe：

```text
fb_st7789v spi0.0: buswidth is not set
fb_st7789v: probe of spi0.0 failed with error -22
```

结论：当前官方镜像只能运行用户态 SPI2 直驱测试；严格的 Linux framebuffer/LVGL 路线必须启动带补丁的 SDK 镜像。当前电脑是 macOS，SDK 自带 toolchain 是 Linux x86_64 ELF，且本机无 Docker/Podman，因此无法在这台机器上完成官方整镜像构建。

---

## 八、故障排查清单

### 屏幕完全不亮

1. **量电压**：屏幕 VCC ↔ GND 应为 3.3V。接上屏幕后如果电压掉到 2.5V 以下，说明供电不足。
2. **看背光**：7 针模块背光常亮，通电后应有微光。全黑 = 供电问题。
3. **检查接线**：每根杜邦线重新插紧。
4. **验证 GPIO**：
   ```bash
   echo 377 > /sys/class/gpio/export
   echo out > /sys/class/gpio/gpio377/direction
   while true; do echo 1 > /sys/class/gpio/gpio377/value; sleep 0.5; echo 0 > /sys/class/gpio/gpio377/value; sleep 0.5; done
   ```
   用万用表量 Pin5，应在 0V↔3.3V 切换。
5. **确认屏幕本身**：用 Arduino/STM32 跑 GC9A01 示例，排除屏幕故障。
