# GY-271 on Milk-V Duo CV1800B

这个目录里是一个直接在 Duo Linux 命令行运行的 GY-271 读取程序。

程序使用 Linux `/dev/i2c-*` 接口，默认访问 `/dev/i2c-1`，并自动探测两类常见 GY-271 模块：

- `QMC5883L`，I2C 地址通常是 `0x0d`
- `HMC5883L`，I2C 地址通常是 `0x1e`

运行后会持续向 console 输出原始三轴数据、换算后的 uT 数据和未校准航向角。

## 接线

推荐使用 Duo 40-pin 排针上的 I2C1，和程序默认配置一致。

| GY-271 引脚 | Duo 物理脚 | Duo 文档名称/功能 | 说明 |
|---|---:|---|---|
| VCC | Pin 36 | `3V3(OUT)` | 给 GY-271 供电 |
| GND | 任意 GND | `GND` | 必须共地 |
| SCL | Pin 6 | `GP4` / `I2C1_SCL` | I2C 时钟 |
| SDA | Pin 7 | `GP5` / `I2C1_SDA` | I2C 数据 |
| DRDY | 不接 | - | 本程序轮询读取，不需要中断脚 |

注意：建议只用 Duo 的 `3V3(OUT)` 给 GY-271 供电，不要接 `VBUS(5V)`。很多 GY-271 小板会把 I2C 上拉到 VCC，接 5V 可能让 `SCL/SDA` 被上拉到 5V，而 Duo 排针 I/O 是 3.3V 逻辑。

## 编译

在电脑上：

```bash
cd duo/CV1800B/c-gy271
make
```

默认使用 `zig cc` 交叉编译出静态 RISC-V Linux 可执行文件：

```text
gy271-read
```

## 拷贝到 Duo

如果 Duo 通过 USB 网络在默认地址 `192.168.42.1`：

```bash
make scp
```

或者手动：

```bash
scp -O gy271-read root@192.168.42.1:/root/
```

## 在 Duo 上运行

SSH 登录 Duo：

```bash
ssh root@192.168.42.1
```

确认 I2C 设备节点：

```bash
ls /dev/i2c-*
```

运行：

```bash
chmod +x /root/gy271-read
/root/gy271-read
```

程序默认会先执行：

```bash
duo-pinmux -w GP4/IIC1_SCL
duo-pinmux -w GP5/IIC1_SDA
```

你也可以手动确认：

```bash
duo-pinmux -r GP4
duo-pinmux -r GP5
```

期望看到 `GP4` 选中 `IIC1_SCL`，`GP5` 选中 `IIC1_SDA`。

## 输出示例

```text
GY-271 reader: device=/dev/i2c-1 chip=QMC5883L addr=0x0d interval=200ms
columns: time_s raw_x raw_y raw_z ut_x ut_y ut_z heading_deg
   0.000    1234    -456     789     41.13    -15.20     26.30    339.72
   0.200    1231    -460     792     41.03    -15.33     26.40    339.50
```

停止输出按 `Ctrl-C`。

## 常用参数

只读一次：

```bash
/root/gy271-read --once
```

改为 500 ms 输出一次：

```bash
/root/gy271-read --interval-ms 500
```

指定 QMC5883L：

```bash
/root/gy271-read --chip qmc --addr 0x0d
```

指定 HMC5883L：

```bash
/root/gy271-read --chip hmc --addr 0x1e
```

如果你的系统里 I2C1 不是 `/dev/i2c-1`，可以指定设备：

```bash
/root/gy271-read --device /dev/i2c-0
```

## 参考

- Duo GPIO pinout: https://milkv.io/docs/duo/getting-started/duo
- Duo pinmux: https://milkv.io/docs/duo/application-development/pinmux
