# Duo CV1800B 双 SPI 屏：ST7789V framebuffer 控制台 + GC9A01 表情屏

记录时间：2026-06-12

## 目标

当前 Duo 镜像已经能驱动一块 SPI 表情屏。下一步希望新增一块 2 寸 240x320 ST7789V2/ST7789V SPI 屏，用 Linux framebuffer 显示 Duo 上的命令行；原来的 GC9A01 表情屏继续保留。

结论：

- ST7789V 屏优先按 Milk-V 官方 SPI Display LVGL 文档接线，占用 SPI2 的官方硬件 CS。
- GC9A01 表情屏改成共享 SPI2 的 SCK/MOSI，但使用新增 GPIO 做 CS。
- Duo 上虽然能看到 `/dev/spidev0.0` 和 `/dev/spidev1.0`，但 40pin 排针上最适合接屏的是 SPI2。SPI3 主要落在 MIPI 相关 PAD，不建议作为普通外接第二屏的接线方案。
- 两块屏共用 SPI2 会影响同时全屏刷新率；但 ST7789V 作为命令行屏、GC9A01 作为低帧率/局部刷新的表情屏，方案可行。
- 预留 USB 键盘时不要把 USB D+/D- 接到 GP4/GP5；GP4/GP5 不是 USB 数据线。
- 真正的 Linux 控制台需要打开 VT/fbcon/HID/input 相关内核配置；当前目标 defconfig 里 `CONFIG_VT` 仍是关闭状态。
- Duo 只有一个主要 USB OTG 口；如果切成 USB host 接键盘，原来的 USB-NCM/RNDIS SSH 网络通常不能同时保留，需要以太网或串口兜底。

参考：

- Milk-V Duo 引脚文档：https://milkv.io/zh/docs/duo/getting-started/duo
- Milk-V SPI Display LVGL 文档：https://milkv.io/zh/docs/duo/resources/spilvgl
- 本项目单 framebuffer 屏记录：`NATIVE_FRAMEBUFFER_SPI_LCD.md`
- 本项目原 GC9A01 接线记录：`PIN_BINDINGS.md`

## 推荐接线

### ST7789V/ST7789V2 新屏，按官方接法

这块屏作为主 framebuffer/控制台屏，优先保持官方 ST7789V 示例接线。

| ST7789V 引脚 | Duo 物理脚 | Duo Pin 名字 | 用途 |
| --- | ---: | --- | --- |
| GND | Pin 38 | GND | 共地 |
| VCC | Pin 36 | 3V3(OUT) | 3.3V 供电 |
| SCL/SCK | Pin 9 | GP6 / SPI2_SCK | SPI 时钟 |
| SDA/MOSI | Pin 10 | GP7 / SPI2_SDO | SPI MOSI |
| RES/RST | Pin 21 | GP16 / XGPIOA[23] | 复位 GPIO |
| DC | Pin 22 | GP17 / XGPIOA[24] | 数据/命令 GPIO |
| CS | Pin 12 | GP9 / SPI2_CS_X | SPI2 硬件 CS0 |
| BL/BLK | Pin 36 | 3V3(OUT) | 背光常亮 |

注意：

- 这块屏占用 Pin12 `SPI2_CS_X`，所以原表情屏不能继续把 CS 接在 Pin12。
- 如果颜色红蓝反了，DTS 节点里加 `bgr;`。
- 如果显示区域有偏移，需要根据具体 2 寸模块调整 ST7789 的列/行偏移，或改驱动 init/地址窗口逻辑。

### GC9A01 表情屏，改成新增 CS

表情屏继续挂在同一条 SPI2 总线上，SCK/MOSI 可以和 ST7789V 并联；DC/RST 可以沿用原来的线；CS 必须换到新的 GPIO。

| GC9A01 引脚 | Duo 物理脚 | Duo Pin 名字 | Linux sysfs GPIO | 用途 |
| --- | ---: | --- | ---: | --- |
| VCC | Pin 36 | 3V3(OUT) | - | 屏幕供电 |
| GND | GND | GND | - | 共地 |
| SCL/SCK | Pin 9 | GP6 / SPI2_SCK | - | 与 ST7789V 共用 |
| SDA/MOSI | Pin 10 | GP7 / SPI2_SDO | - | 与 ST7789V 共用 |
| DC | Pin 5 | GP3 / PWR_GPIO[25] | 377 | 可沿用原线 |
| RST | Pin 11 | GP8 / PWR_GPIO[21] | 373 | 可沿用原线 |
| CS | Pin 29 | GP22 / PWR_GPIO[4] | 356 | 新增片选 |

需要改动的表情屏排线：

- 把原来 GC9A01 的 CS 从 Pin12 移到 Pin29 `GP22`。
- Pin9、Pin10、Pin5、Pin11、VCC、GND 可以不变。

## Pinmux 目标状态

烧录新镜像后，可在 Duo 上用 `duo-pinmux -r` 复核。手工调试时的目标大致如下：

```bash
duo-pinmux -w GP6/SPI2_SCK
duo-pinmux -w GP7/SPI2_SDO
duo-pinmux -w GP9/SPI2_CS_X

duo-pinmux -w GP16/GP16
duo-pinmux -w GP17/GP17

duo-pinmux -w GP3/GP3
duo-pinmux -w GP8/GP8
duo-pinmux -w GP22/GP22
```

复核：

```bash
duo-pinmux -r GP6
duo-pinmux -r GP7
duo-pinmux -r GP9
duo-pinmux -r GP16
duo-pinmux -r GP17
duo-pinmux -r GP3
duo-pinmux -r GP8
duo-pinmux -r GP22
```

## Duo 上到底有几套 SPI

本项目真机记录里曾看到两个 spidev：

```text
/dev/spidev0.0 -> SPI2, 41a0000.spi2, 40pin 的 Pin9/10/11/12
/dev/spidev1.0 -> SPI3, 41b0000.spi3
```

但是对 Duo CV1800B 40pin 外接屏来说，推荐只把 SPI2 当作稳定可用的屏幕 SPI：

- SPI2 在 40pin 上有明确引脚：Pin9 `SPI2_SCK`、Pin10 `SPI2_SDO`、Pin11 `SPI2_SDI`、Pin12 `SPI2_CS_X`。
- SPI3 在 SDK pinlist 里是 `PAD_MIPI_TXM1/P1/M0/P0` 这一组 MIPI 相关 PAD，不是普通 40pin 排针上的一套完整 SPI。

因此双屏推荐拓扑是：

```text
SPI2 SCK/MOSI shared
├── CS0: ST7789V framebuffer screen, Pin12 GP9/SPI2_CS_X
└── CS1: GC9A01 expression screen, Pin29 GP22 GPIO CS
```

## 软件改动方向

### 路线 A：ST7789V 做 framebuffer，GC9A01 继续用户态 SPI

这是当前最贴合需求的路线。

预期设备：

```text
/dev/fb0       -> ST7789V 240x320 framebuffer
/dev/spidev0.1 -> GC9A01 表情屏用户态 SPI
```

DTS 方向示意：

```dts
&spi2 {
	status = "okay";
	num-cs = <2>;

	/delete-node/ spidev@0;

	st7789v: display@0 {
		compatible = "sitronix,st7789v";
		reg = <0>;
		status = "okay";
		spi-max-frequency = <24000000>;
		spi-cpol;
		spi-cpha;
		width = <240>;
		height = <320>;
		rotate = <0>;
		fps = <30>;
		buswidth = <8>;
		reset-gpios = <&porta 23 GPIO_ACTIVE_LOW>; /* GP16 */
		dc-gpios = <&porta 24 GPIO_ACTIVE_HIGH>;   /* GP17 */
		debug = <0x0>;
	};

	spidev@1 {
		compatible = "rohm,dh2228fv";
		reg = <1>;
		status = "okay";
		spi-max-frequency = <12000000>;
	};
};
```

注意：

- 本次定制镜像先不在 DTS 里把 GP22 声明成 SPI `cs-gpios`，而是让 GC9A01 用户态程序手动拉 GP22 做 CS；这是最接近已跑通单屏程序的迁移方式。
- 如果 GC9A01 改走 `/dev/spidev0.1`，表情程序里的 SPI 设备路径要从 `/dev/spidev0.0` 改成 `/dev/spidev0.1`。
- 如果后续改成 DTS `cs-gpios` 由 SPI core 控制 GP22，程序里不要再 export/toggle 同一个 CS GPIO；应改成 `GC9A01_GPIO_CS=-1`。

当前 `c-gc9a01/test_hw_spi.c` 和 `c-gc9a01/eye_anim.c` 已支持环境变量切换接线。本次定制镜像使用：

```bash
GC9A01_SPI_DEV=/dev/spidev0.1 \
GC9A01_GPIO_CS=356 \
GC9A01_PINMUX_CS=GP22/GP22 \
/root/eye-anim /root/eye_animation.bin 20 12000000 auto
```

如果之后 `/dev/spidev0.1` 的 CS 改由 DTS `cs-gpios` 绑定到 GP22，则运行命令改成：

```bash
GC9A01_SPI_DEV=/dev/spidev0.1 \
GC9A01_GPIO_CS=-1 \
GC9A01_PINMUX_CS=none \
/root/eye-anim /root/eye_animation.bin 20 12000000 auto
```

### 路线 B：两块屏都做 framebuffer

预期设备：

```text
/dev/fb0 -> ST7789V 控制台屏
/dev/fb1 -> GC9A01 表情屏
```

这条路线软件结构更统一，但需要同时让 `fb_st7789v` 和 `fb_gc9a01` 在同一条 SPI2 总线上注册两个 fbtft 节点，并确认两个 framebuffer 的启动顺序、fbcon 映射和表情 daemon 的 fb 路径。

## Linux 命令行显示到 ST7789V

ST7789V 注册成 `/dev/fb0` 之后，还不等于自动显示系统命令行。当前目标板配置已有 `CONFIG_FB=y`、`CONFIG_INPUT_EVDEV=y`、`CONFIG_FB_TFT_ST7789V=y`，但 `CONFIG_VT` 仍是关闭状态；要做真正的 Linux 控制台，至少要关注：

```text
CONFIG_VT
CONFIG_VT_CONSOLE
CONFIG_HW_CONSOLE
CONFIG_FRAMEBUFFER_CONSOLE
CONFIG_DUMMY_CONSOLE
CONFIG_FONT_8x8
CONFIG_FONT_8x16
CONFIG_INPUT
CONFIG_INPUT_EVDEV
CONFIG_INPUT_KEYBOARD
CONFIG_HID
CONFIG_HID_GENERIC
CONFIG_USB_HID
```

还需要启动 getty/console，或通过 fbcon 参数把控制台映射到目标 framebuffer。启动参数方向可以先按下面思路验证：

```text
console=tty0 fbcon=map:0
```

实际镜像里仍建议保留串口或 USB gadget 登录作为救援入口，避免 framebuffer 控制台没起来时失联。

如果先不做内核虚拟控制台，也可以走用户态终端渲染方案：程序读 `/dev/input/event*`，把终端文本画到 `/dev/fb0`。

## 刷新率预期

粗略按 RGB565 全屏刷新估算：

| 屏幕 | 分辨率 | 一帧数据 | 48 MHz SPI 理论传输时间 |
| --- | ---: | ---: | ---: |
| ST7789V | 240x320 | 153600 bytes | 约 25.6 ms |
| GC9A01 | 240x240 | 115200 bytes | 约 19.2 ms |
| 两块都全屏刷新 | - | 268800 bytes | 约 44.8 ms |

这只是纯 SPI 传输理论值，实际还会有命令、GPIO、调度、framebuffer flush 等开销。

结论：

- 两块屏共用 SPI2 时，不适合期待两块都稳定 60fps 全屏刷新。
- ST7789V 作为命令行屏通常只更新少量文本，压力很小。
- GC9A01 表情屏建议 10-20fps，优先局部刷新或降低动画刷新频率。
- 如果 GC9A01 仍用 8 MHz，表情屏全屏刷新会明显慢；稳定后可以逐步提高它的 `spi-max-frequency` 或用户态 SPI speed。

## 键盘输入方案

不要把 USB 母口的 D+/D- 接到 GP4/GP5：

- GP4/GP5 是普通 GPIO/I2C/UART/PWM 复用脚，不是 USB 差分数据线。
- 之前 GY-271 磁力计也用过 GP4/GP5 做 I2C1，接 USB 会冲突且不会被 Linux 识别成键盘。

### 方案 A：USB host 键盘，最像普通 Linux

| USB-A | 应接到 |
| --- | --- |
| VBUS | 5V USB 电源 |
| GND | GND |
| D+ | Duo 真实 USB D+ |
| D- | Duo 真实 USB D- |

推荐使用官方 USB/Ethernet IO Board，或从 Duo 的真实 USB OTG 口做 host。内核方向：

```text
CONFIG_USB=y
CONFIG_USB_DWC2=y
CONFIG_USB_ROLE_SWITCH=y
CONFIG_INPUT=y
CONFIG_INPUT_EVDEV=y
CONFIG_HID=y
CONFIG_HID_GENERIC=y
CONFIG_USB_HID=y
```

验证命令：

```bash
dmesg | grep -Ei 'usb|hid|keyboard|input'
cat /proc/bus/input/devices
ls -l /dev/input/event*
```

缺点是 Duo 的 USB OTG 口切成 host 后，原来的 USB-NCM/RNDIS 网络通常不能同时继续使用。这个方案适合最终形态，但要提前准备以太网或串口救援。

### 方案 B：UART/I2C 小键盘，保留 USB 网络

开发阶段更稳的组合是：USB 仍然保持 gadget 网络用于 SSH，键盘走一个小 MCU 或 I2C/GPIO 扩展。

可选实现：

- MCU 方案：RP2040/ESP32/Pro Micro 等接实体键盘或按键矩阵，再用 UART/I2C 把按键事件发给 Duo。
- Duo 用户态守护进程读取 `/dev/ttyS*` 或 I2C，再通过 `/dev/uinput` 注入 Linux input 事件。若走这条路，内核加 `CONFIG_INPUT_UINPUT`。
- 如果暂时只需要方向键、Enter、Esc、Tab 等少量按键，可以直接用 `gpio-keys` 或矩阵键盘 DTS；但全键盘不建议直接吃 Duo 的 GPIO。

这个方案软件多一点，但不会占用 USB OTG 口，适合一边 SSH 调试、一边在 ST7789V 上看真实控制台或用户态终端。

## 网络连接方案

### 方案 A：开发期保留 USB gadget 网络

当前 Duo 常用 `192.168.42.1` 登录，目标 defconfig 也已经启用：

```text
CONFIG_USB_GADGET=y
CONFIG_USB_CONFIGFS=y
CONFIG_USB_CONFIGFS_NCM=y
CONFIG_USB_CONFIGFS_RNDIS=y
```

这条路线最适合开发和刷机验证：

- 电脑通过 USB-NCM/RNDIS SSH 到 Duo。
- 文件传输继续用 `scp -O` 兼容 Dropbear。
- 键盘输入优先走 UART/I2C MCU、小按键，或先用 SSH 远程输入。
- 外网可以尝试电脑网络共享/NAT；如果网络环境不允许，至少本机 SSH 与 scp 不受影响。

### 方案 B：最终形态使用以太网

如果要 USB host 接键盘，同时还希望 Duo 有网络，优先考虑以太网：

- 官方 USB/Ethernet IO Board 或 100M Ethernet 扩展作为主网络出口。
- 当前目标 defconfig 已有 `CONFIG_STMMAC_ETH=y`，后续重点是确认板级 DTS、PHY、RJ45/IO Board 接线和 rootfs 自动 DHCP。
- rootfs 里建议保留 BusyBox `udhcpc`，启动后用 `udhcpc -i eth0` 或 init 脚本自动拿地址。

### 方案 C：USB host + USB 网卡/无线

如果最终要用 USB host，理论上可以接 powered USB hub，同时挂 USB 键盘和 USB 网卡。需要额外打开对应 host 网卡驱动，例如：

```text
CONFIG_USB_USBNET
CONFIG_USB_NET_CDCETHER
CONFIG_USB_NET_RNDIS_HOST
CONFIG_USB_NET_AX8817X
CONFIG_USB_NET_AX88179_178A
CONFIG_USB_RTL8152
```

USB Wi-Fi 也能做，但会引入芯片驱动、firmware、`wpa_supplicant` 和供电稳定性问题，不建议作为第一版网络方案。

### 推荐组合

第一阶段开发：

```text
ST7789V /dev/fb0 真控制台
GC9A01 /dev/spidev0.1 表情屏
USB gadget 网络继续 SSH
UART/I2C MCU 或少量 GPIO keys 做本地输入
```

第二阶段独立运行：

```text
ST7789V /dev/fb0 真控制台
GC9A01 表情屏
USB host HID 键盘
以太网作为网络出口
串口作为救援入口
```

## 下一台电脑继续处理清单

1. 硬件先按本文接线，把 ST7789V 接官方 Pin12 CS，把 GC9A01 CS 移到 Pin29 GP22。
2. 在 SDK DTS 里把 SPI2 改成 ST7789V `display@0` + GC9A01 `spidev@1` 或第二个 framebuffer。
3. 在 U-Boot pinmux 里确认 GP16/GP17/GP3/GP8/GP22 都是 GPIO，GP6/GP7/GP9 是 SPI2。
4. 打开 framebuffer console、VT、USB HID keyboard 相关内核配置；如果走 MCU 注入按键，再加 `CONFIG_INPUT_UINPUT`。
5. 先决定 USB 角色：开发期保留 USB gadget 网络，最终形态再切 USB host 键盘并改用以太网。
6. 烧录后先确认：

```bash
ls -l /dev/fb* /dev/spidev*
dmesg | grep -Ei 'st7789|gc9a01|fb_tft|spi'
cat /sys/class/graphics/fb0/name
```

7. 先用 ST7789V 验证 `/dev/fb0`：

```bash
cat /dev/zero > /dev/fb0
cat /dev/random > /dev/fb0
```

8. 再迁移表情程序到 `/dev/spidev0.1`，并把 CS 改为 GP22，或改成 SPI core 自动 CS。
9. 最后验证输入和网络：`cat /proc/bus/input/devices`、`ls /dev/input/event*`、`ip addr`、`udhcpc -i eth0`。
