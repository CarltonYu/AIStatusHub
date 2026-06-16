# UDP Console Input Daemon

Duo 端守护进程，把通过 UDP 收到的文字/按键注入到本地 TTY（默认 `/dev/tty1`），
让文字直接显示在 ST7789V  framebuffer 控制台（Linux 命令行）上。

## 端口

- `25251`

## 支持的命令

每个 UDP 包一行，支持以下三种格式。如果一行不以 `string ` / `cmd ` / `combo `
开头，则整行按 `string ` 处理。

### `string <text>`

把后面的文字逐字符“键入”到 Duo 的命令行。

```bash
echo "string hello world" | nc -u -w 0 192.168.42.1 25251
```

### `cmd <key>`

按下特殊键。

| 命令示例 | 作用 |
| --- | --- |
| `cmd enter` | 回车（`\n`） |
| `cmd return` | 回车（`\r`） |
| `cmd tab` | Tab |
| `cmd backspace` | 退格（`0x7f`） |
| `cmd delete` | Delete 键 |
| `cmd escape` / `cmd esc` | Esc |
| `cmd space` | 空格 |
| `cmd up/down/left/right` | 方向键 |
| `cmd home/end` | Home / End |
| `cmd pgup/pgdown` | Page Up / Page Down |
| `cmd insert` | Insert |

```bash
echo "cmd enter" | nc -u -w 0 192.168.42.1 25251
```

### `combo <mod>+<key>`

组合键。支持多个 modifier 用 `+` 连接，例如 `ctrl+alt+c`。

| 示例 | 作用 |
| --- | --- |
| `combo ctrl+c` | Ctrl+C（`0x03`） |
| `combo ctrl+d` | Ctrl+D（`0x04`） |
| `combo ctrl+l` | Ctrl+L（清屏） |
| `combo ctrl+z` | Ctrl+Z |
| `combo shift+a` | 大写 A |
| `combo alt+f` | Alt+F（发送 `Esc f`） |

```bash
echo "combo ctrl+c" | nc -u -w 0 192.168.42.1 25251
```

## 为什么 `nc` 会卡住

macOS 自带的 `nc -u` 在发送完后默认不会立即退出，导致终端像在等待输入。
解决方法是加上 `-w 0`（发送后立即超时退出）：

```bash
# 推荐：加 -w 0
echo "string hello" | nc -u -w 0 192.168.42.1 25251

# 或者指定 1 秒超时
echo "string hello" | nc -u -w 1 192.168.42.1 25251
```

如果 `-w` 在你的系统上不可用，也可以用 Python：

```bash
python3 - <<'PY'
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.sendto(b"string hello\n", ("192.168.42.1", 25251))
PY
```

## 上位机工具

推荐使用项目里提供的跨平台工具 `duo-console`（源码在 `tools/duo-console/`），
安装后可以直接输入命令，不必再写 `nc`：

```bash
# 直接发送
duo-console "string ls /"
duo-console "cmd enter"

# 交互式 REPL
duo-console
> string ls /
> cmd enter
> ^D
```

macOS 上已安装到 `~/.local/bin/duo-console`，新终端直接可用。

## 测试流程

1. 给 Duo 上电，确认 ST7789V 屏幕显示 Linux 命令行提示符。
2. 在上位机执行：

```bash
duo-console "string ls /"
duo-console "cmd enter"
```

3. 观察 Duo 屏幕：应该出现 `ls /` 并执行，列出根目录内容。

## 字体大小

镜像启动参数里加了 `fbcon=font:8x8`，控制台字体为 8×8 像素，比默认 8×16
多一倍行数，适合 320×240 的小屏。
