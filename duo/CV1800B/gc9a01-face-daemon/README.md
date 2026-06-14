# GC9A01 Face Daemon for Duo

表情屏守护进程。让 Milk-V Duo 上的 GC9A01 圆形 SPI 屏显示 IrisOLED 风格表情，并通过 UDP 端口监听外部命令实时切换表情。

本程序用于双屏方案：

- ST7789V 240×320 屏 → `/dev/fb0`，Linux 控制台。
- GC9A01 240×240 圆屏 → `/dev/spidev0.1`，本守护进程驱动。

## 构建

```bash
cd duo/CV1800B/gc9a01-face-daemon
make
```

输出为静态 RISC-V 二进制 `gc9a01-face-daemon`。

## 启动守护进程

```bash
/usr/bin/gc9a01-face-daemon daemon \
  --spi /dev/spidev0.1 \
  --udp-port 25250 \
  --fps 20 \
  --default normal \
  --color 00d7ff
```

可选参数：

| 参数 | 说明 | 默认值 |
| --- | --- | --- |
| `--spi PATH` | SPI 设备节点 | `/dev/spidev0.1` |
| `--udp-port N` | UDP 监听端口，`0` 表示不监听 | `25250` |
| `--fps N` | 渲染帧率 | `20` |
| `--default NAME` | 默认表情 | `normal` |
| `--color RRGGBB` | 表情颜色，可带 `#` 前缀 | `00d7ff` |
| `--width N` / `--height N` | 表情渲染尺寸 | `224×112` |
| `--spi-speed HZ` | SPI 时钟 | `12000000` (12 MHz) |
| `--gpio-dc N` / `--gpio-rst N` | DC/RST 的 sysfs GPIO 号，`-1` 为自动探测 | 自动 |
| `--socket PATH` | 本地 UNIX socket 路径 | `/tmp/gc9a01-face-daemon.sock` |

## 命令总览

守护进程同时支持两种控制方式：

1. **本地 UNIX socket**：在 Duo 本机上运行 `gc9a01-face-daemon <子命令>` 发送命令。
2. **UDP 网络命令**：上位机向 `192.168.42.1:25250` 发送 ASCII 字符串。

UDP 命令协议与 `irisoled-face` 保持兼容，每条命令以换行结尾。守护进程收到后会回复 `OK\n` 或 `ERR\n`。

## 支持的命令

### 1. 切换默认表情

将默认表情设置为 `<name>`，并立即显示该表情。后续没有临时播放时，会循环播放该表情的动画序列。

```text
default <name>
```

Duo 本地：

```bash
gc9a01-face-daemon default angry
```

上位机 UDP：

```bash
# Linux / macOS
printf 'default angry\n' | nc -u -w1 192.168.42.1 25250

# 或使用 bash 内置 /dev/udp
exec 3<>/dev/udp/192.168.42.1/25250
echo 'default angry' >&3
cat <&3
exec 3<&-
exec 3>&-
```

### 2. 恢复默认 normal 表情

等价于 `default normal`。

```text
normal
```

Duo 本地：

```bash
gc9a01-face-daemon normal
```

上位机 UDP：

```bash
printf 'normal\n' | nc -u -w1 192.168.42.1 25250
```

### 3. 临时播放表情

播放指定表情的动画序列，播放结束后自动回到默认表情。

```text
play <name> [--repeat N] [--duration Ns|Nms]
```

- `--repeat N`：重复播放 N 个周期后返回默认表情。未指定时默认 `1`。
- `--duration Ns` / `--duration Nms`：持续播放指定时间后返回默认表情。指定 `--duration` 时，`--repeat` 失效。

Duo 本地：

```bash
# 播放 3 次 happy 后回到默认
gc9a01-face-daemon play happy --repeat 3

# 持续显示 angry 5 秒后回到默认
gc9a01-face-daemon play angry --duration 5s
```

上位机 UDP：

```bash
printf 'play happy --repeat 3\n' | nc -u -w1 192.168.42.1 25250
printf 'play angry --duration 5s\n' | nc -u -w1 192.168.42.1 25250
printf 'play focused --duration 500ms\n' | nc -u -w1 192.168.42.1 25250
```

### 4. 停止守护进程

停止 UDP 监听、清屏并退出进程。

```text
stop-daemon
stop
```

Duo 本地：

```bash
gc9a01-face-daemon stop-daemon
```

上位机 UDP：

```bash
printf 'stop-daemon\n' | nc -u -w1 192.168.42.1 25250
```

### 5. 查看支持的表达式列表

仅在 Duo 本地有效（UDP 无此命令）：

```bash
gc9a01-face-daemon list
```

## 上位机命令行示例

默认 Duo IP 为 `192.168.42.1`，端口 `25250`。

### Linux / macOS

使用 `nc`（OpenBSD netcat）：

```bash
DUO_IP=192.168.42.1
DUO_PORT=25250

printf 'default angry\n'  | nc -u -w1 "$DUO_IP" "$DUO_PORT"
printf 'normal\n'         | nc -u -w1 "$DUO_IP" "$DUO_PORT"
printf 'play happy --repeat 3\n' | nc -u -w1 "$DUO_IP" "$DUO_PORT"
printf 'play angry --duration 5s\n' | nc -u -w1 "$DUO_IP" "$DUO_PORT"
printf 'stop-daemon\n'   | nc -u -w1 "$DUO_IP" "$DUO_PORT"
```

使用 `socat`：

```bash
echo 'default happy' | socat - UDP:"$DUO_IP":"$DUO_PORT"
```

使用 Bash `/dev/udp`（无需安装额外工具）：

```bash
send_face() {
  exec 3<>/dev/udp/192.168.42.1/25250
  echo "$1" >&3
  timeout 1 cat <&3
  exec 3<&-
  exec 3>&-
}

send_face 'default angry'
send_face 'play happy --duration 3s'
```

### Windows

PowerShell：

```powershell
$ip = "192.168.42.1"
$port = 25250
$client = New-Object System.Net.Sockets.UdpClient
$client.Connect($ip, $port)

function Send-Face($cmd) {
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($cmd + "`n")
    $client.Send($bytes, $bytes.Length) | Out-Null
}

Send-Face "default angry"
Send-Face "play happy --repeat 3"
Send-Face "normal"
Send-Face "stop-daemon"

$client.Close()
```

### Python

```python
import socket

def send_face(cmd: str, ip="192.168.42.1", port=25250) -> str:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.settimeout(1.0)
        s.sendto((cmd + "\n").encode(), (ip, port))
        try:
            return s.recv(64).decode().strip()
        except socket.timeout:
            return ""

print(send_face("default angry"))
print(send_face("play happy --duration 5s"))
print(send_face("normal"))
```

## 与 AIStatusHub 配合

Rust 侧的 `[outputs]` 配置示例：

```toml
[[outputs]]
id = "duo-face"
type = "irisoled-udp"
enabled = true
url = "udp://192.168.42.1:25250"
busy_expression = "angry"
idle_expression = "normal"
include_remote_states = false
```

当本地 AI 任务繁忙时，AIStatusHub 自动发送 `default angry`；空闲时发送 `normal`。

## 安装到镜像

`install-init.sh` 会把二进制安装到 `/usr/bin/gc9a01-face-daemon` 并创建开机启动脚本 `/etc/init.d/S99zgc9a01-face`。

在 Duo 上执行：

```bash
scp -O gc9a01-face-daemon install-init.sh root@192.168.42.1:/root/
ssh root@192.168.42.1 'sh /root/install-init.sh'
```

启动脚本会随系统启动运行（故意命名为 `S99z*` 以确保在 `S99user` 之后执行，因为 `S99user` 会通过 `devmem` 配置 GP22/SPI CS1）：

```bash
/etc/init.d/S99zgc9a01-face start
```

脚本内置看门狗循环：如果守护进程异常退出，会自动在 3 秒后重新拉起。正常停止时请使用：

```bash
/etc/init.d/S99zgc9a01-face stop
```

日志位置：`/tmp/gc9a01-face.log`

## 接线

| GC9A01 引脚 | Duo 物理脚 | 功能 |
| --- | ---: | --- |
| VCC | Pin 36 | 3V3 |
| GND | GND | 共地 |
| SCL/SCK | Pin 9 | SPI2_SCK |
| SDA/MOSI | Pin 10 | SPI2_SDO |
| DC | Pin 5 | GP3 / PWR_GPIO_25 |
| RST | Pin 11 | GP8 / PWR_GPIO_21 |
| CS | Pin 29 | GP22 / PWR_GPIO_4（由 SPI 核心 cs-gpios 管理） |

## 支持的表达式

| 表情 | 说明 |
| --- | --- |
| `normal` | 正常/待机 |
| `alert` | 警觉 |
| `angry` | 生气 |
| `bored` | 无聊 |
| `despair` | 绝望 |
| `disoriented` | 晕眩 |
| `excited` | 兴奋 |
| `focused` | 专注 |
| `furious` | 暴怒 |
| `happy` | 开心 |
| `sad` | 难过 |
| `scared` | 害怕 |
| `sleepy` | 困倦 |
| `surprised` | 惊讶 |
| `worried` | 担忧 |
| `blink` | 眨眼（闭眼） |
| `blink_down` | 闭眼下半 |
| `blink_up` | 闭眼上半 |
| `look_down` | 向下看 |
| `look_left` | 向左看 |
| `look_right` | 向右看 |
| `look_up` | 向上看 |
| `wink_left` | 左眼 wink |
| `wink_right` | 右眼 wink |
| `battery_full` | 电池满 |
| `battery_low` | 电池低 |
| `battery` | 电池 |
| `left_signal` | 左转向 |
| `right_signal` | 右转向 |
| `logo` | Logo |
| `mode` | 模式 |
| `warning` | 警告 |

### 别名

为了与常见状态词对齐，守护进程自动把下列别名映射到标准表情：

| 别名 | 映射到 |
| --- | --- |
| `idle` | `normal` |
| `sleep`, `sleeping` | `sleepy` |
| `busy` | `focused` |

别名可用于 `default` 和 `play` 命令。
