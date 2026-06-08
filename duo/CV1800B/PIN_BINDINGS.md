# Milk-V Duo CV1800B <-> GC9A01 屏幕引脚绑定

最后核对日期：2026-06-08

官方文档来源：

- Milk-V Duo (CV1800B) 入门指南：https://milkv.io/zh/docs/duo/getting-started/duo

## 当前实际接线

| GC9A01 屏幕引脚 | Duo 物理脚 | Duo 官方名称/功能 | Linux sysfs GPIO | 当前用途 |
|---|---:|---|---:|---|
| VCC | Pin 36 | 3V3(OUT) | - | 屏幕供电 |
| GND | GND | GND | - | 共地 |
| SCL | Pin 9 | GP6 / SPI2_SCK / PWR_GPIO[23] | 375 | SPI 时钟 |
| SDA | Pin 10 | GP7 / SPI2_SDO / PWR_GPIO[22] | 374 | SPI MOSI，向屏幕写数据 |
| DC | Pin 5 | GP3 / UART4_RX / PWR_GPIO[25] | 377 | 数据/命令选择 GPIO |
| CS | Pin 12 | GP9 / SPI2_CS_X / PWR_GPIO[18] | 370 | 片选 GPIO，程序手动控制 |
| RST | Pin 11 | GP8 / SPI2_SDI / PWR_GPIO[21] | 373 | 屏幕复位 GPIO |

## 官方 GPIO 编号依据

官方文档里 Duo 的 PWR_GPIO 属于 `gpiochip4`，base 为 `352`。

所以当前用到的 sysfs GPIO 号是：

| Duo 名称 | PWR_GPIO bit | 计算 | sysfs GPIO |
|---|---:|---|---:|
| GP3 | 25 | 352 + 25 | 377 |
| GP6 | 23 | 352 + 23 | 375 |
| GP7 | 22 | 352 + 22 | 374 |
| GP8 | 21 | 352 + 21 | 373 |
| GP9 | 18 | 352 + 18 | 370 |

注意：不要用 `480 + GPx` 计算 GP0-GP9。`gpiochip0` base 480 对应的是 XGPIOA，不是 PWR_GPIO。

## 程序里的绑定

当前推荐测试程序：

```text
duo/CV1800B/c-gc9a01/test_hw_spi.c
```

程序使用：

| 程序配置 | 值 |
|---|---|
| SPI 设备 | `/dev/spidev0.0` |
| SPI 引脚 | Pin 9 SCL、Pin 10 SDA |
| DC GPIO | 377 |
| RST GPIO | 373 |
| CS GPIO | 370 |
| SPI mode | mode 0 |
| SPI speed | 8 MHz |

## 运行前 pinmux 目标状态

当前 C 程序会自动执行下面的 pinmux 设置：

```bash
duo-pinmux -w GP3/GP3
duo-pinmux -w GP6/SPI2_SCK
duo-pinmux -w GP7/SPI2_SDO
duo-pinmux -w GP8/GP8
duo-pinmux -w GP9/GP9
```

复盘时可以手动确认：

```bash
duo-pinmux -r GP3
duo-pinmux -r GP6
duo-pinmux -r GP7
duo-pinmux -r GP8
duo-pinmux -r GP9
```

期望结果：

| Duo 名称 | 期望 pinmux |
|---|---|
| GP3 | GP3 |
| GP6 | SPI2_SCK |
| GP7 | SPI2_SDO |
| GP8 | GP8 |
| GP9 | GP9 |

## 重点复盘点

1. Pin 9/10 保持硬件 SPI2：SCL -> GP6/SPI2_SCK，SDA -> GP7/SPI2_SDO。
2. Pin 5 必须切成 GPIO：官方默认可能是 UART4_RX，但当前作为 DC。
3. Pin 11 必须切成 GPIO：官方默认可能是 SPI2_SDI，但 GC9A01 7 线屏不需要 MISO，当前作为 RST。
4. Pin 12 当前切成 GPIO：虽然官方有 SPI2_CS_X，但程序手动控制 CS，避免官方内核拒绝 `SPI_NO_CS` 时出现片选不稳定。
5. 屏幕只有背光但没有画面时，优先核对 DC/RST/CS 的 pinmux 和物理接线。
