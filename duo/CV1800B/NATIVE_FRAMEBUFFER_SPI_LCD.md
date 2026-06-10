# Duo CV1800B 原生 framebuffer SPI 屏方案

记录时间：2026-06-10

## 目标

让定制 Duo SD 镜像启动时直接把 SPI 屏注册成 Linux framebuffer，生成 `/dev/fb0`，上层程序或 LVGL 只需要写 fbdev，不再在用户态手动 bitbang/初始化屏幕。

官方 SPI Display LVGL 示例的核心思路是：在 SDK DTS 里删除 SPI 总线下的 `spidev@0`，改成具体的 fbtft LCD 节点，并启用 `CONFIG_FB_TFT_ST7789V`。本项目在这个思路上扩展了 GC9A01，同时保留 ST7789/ST7789V 的切换模板。

参考：

- Milk-V SPI Display LVGL 文档：https://milkv.io/docs/duo/resources/spilvgl
- 官方 LVGL framebuffer demo patch：https://github.com/milkv-duo/duo-lvgl-fb-demo/blob/master/duo-kernel-fb_st7789v.patch

## 当前默认接线

| LCD | Duo 引脚 | Duo 功能 | Linux GPIO |
| --- | --- | --- | --- |
| VCC | Pin 36 | 3V3_OUT | - |
| GND | GND | GND | - |
| SCL | Pin 9 | GP6 / SPI2_SCK | - |
| SDA | Pin 10 | GP7 / SPI2_SDO | - |
| CS | Pin 12 | GP9 / SPI2_CS_X | - |
| RST | Pin 11 | GP8 / PWR_GPIO_21 | 373 |
| DC | Pin 5 | GP3 / PWR_GPIO_25 | 377 |

## 已落到 SDK 源码的改动

submodule 已同步到自己的 fork：

```text
duo/CV1800B/duo-buildroot-sdk -> https://github.com/CarltonYu/duo-buildroot-sdk.git
```

核心文件：

```text
build/boards/cv180x/cv1800b_milkv_duo_sd/u-boot/cvi_board_init.c
build/boards/cv180x/cv1800b_milkv_duo_sd/dts_riscv/cv1800b_milkv_duo_sd.dts
build/boards/cv180x/cv1800b_milkv_duo_sd/linux/cvitek_cv1800b_milkv_duo_sd_defconfig
linux_5.10/drivers/staging/fbtft/Kconfig
linux_5.10/drivers/staging/fbtft/Makefile
linux_5.10/drivers/staging/fbtft/fb_gc9a01.c
```

关键点：

- U-Boot pinmux：GP3 改成 `PWR_GPIO_25` 用作 LCD DC，GP8 改成 `PWR_GPIO_21` 用作 LCD RST。
- SPI2 数据线：GP6/GP7/GP9 保持 `SPI2_SCK`、`SPI2_SDO`、`SPI2_CS_X`。
- DTS：`&spi2` 删除 `spidev@0`，默认启用 `compatible = "galaxycore,gc9a01"` 的 `display@0`。
- GC9A01：新增 fbtft 驱动 `fb_gc9a01.c`，初始化序列对齐已跑通的 `/dev/spidev0.0` 硬件 SPI 测试。
- Kernel config：`CONFIG_FB_TFT=y`、`CONFIG_FB_TFT_ST7789V=y`、`CONFIG_FB_TFT_GC9A01=y`。

## GC9A01 默认配置

当前 DTS 默认启用 GC9A01：

```dts
&spi2 {
	status = "okay";
	num-cs = <1>;
	/delete-node/ spidev@0;

	gc9a01: display@0 {
		compatible = "galaxycore,gc9a01";
		reg = <0>;
		status = "okay";
		spi-max-frequency = <8000000>;
		width = <240>;
		height = <240>;
		rotate = <0>;
		fps = <30>;
		buswidth = <8>;
		reset-gpios = <&porte 21 GPIO_ACTIVE_LOW>;
		dc-gpios = <&porte 25 GPIO_ACTIVE_HIGH>;
		debug = <0x0>;
	};
};
```

先用 8 MHz 和 SPI mode 0，是为了贴近已验证的用户态硬件 SPI 测试。画面稳定后可以再尝试把 `spi-max-frequency` 提高。

## 切换到 ST7789/ST7789V

ST7789V 驱动已经编进内核。若换成 ST7789/ST7789V 屏，同一套 SPI2 接线可以复用，但同一个 CS 上只能启用一个显示节点：

1. 把 GC9A01 节点 `status = "okay"` 改成 `status = "disabled"`，或直接注释掉。
2. 在同一个 `&spi2` 下启用文档里预留的 ST7789V `display@0` 模板。
3. 根据实际屏幕改 `width`、`height`、`rotate`。常见 ST7789 有 240x320、240x240 等版本。
4. 如果颜色红蓝反了，可给节点加 `bgr;`。
5. 官方 ST7789 示例使用 `spi-cpol; spi-cpha;`，也就是 SPI mode 3；当前模板保留了这两行。

## 构建与验证

macOS 不能直接运行 SDK 内的 Linux x86_64 toolchain。仓库里已有 Lima 辅助脚本，在仓库根目录执行：

```bash
scripts/setup-macos-duo-env.sh
limactl shell duo-builder
cd /Users/carlton/Documents/code/self/AIStatusHub.git/duo/CV1800B/duo-buildroot-sdk
source build/envsetup_milkv.sh
defconfig cv1800b_milkv_duo_sd
build_all
pack_sd_image
```

烧录新镜像后，在 Duo 上检查：

```bash
ls -l /dev/fb0
dmesg | grep -Ei 'gc9a01|st7789|fb_tft|spi'
cat /sys/class/graphics/fb0/name
```

简单刷屏烟测：

```bash
dd if=/dev/zero bs=115200 count=1 2>/dev/null | tr '\000' '\377' > /dev/fb0
```

如果没有 `/dev/fb0`，优先查 DTS 是否仍然保留了 `spidev@0`，以及 `buswidth = <8>` 是否存在。之前在官方镜像上热绑定失败的直接原因就是缺少 DTS 提供的 fbtft 属性。
