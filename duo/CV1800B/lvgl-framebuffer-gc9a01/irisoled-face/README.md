# IrisOLED Face Player For Duo SPI LCD

This player adapts the MIT-licensed bitmap expressions from
`orji123/Irisoled` to the Milk-V Duo framebuffer-backed SPI LCD.

It runs as one persistent daemon that keeps drawing the default `normal`
animation. Command invocations send a small UNIX socket message to the daemon,
so the face changes immediately without restarting the display program.

## Source Material

- Upstream: https://github.com/orji123/Irisoled
- License: MIT
- Vendored source path: `../../eyes-resources/Irisoled`
- Generated bitmap header: `irisoled_bitmaps.h`

Regenerate the header after updating upstream:

```bash
make irisoled_bitmaps.h
```

## Build

```bash
cd duo/CV1800B/lvgl-framebuffer-gc9a01/irisoled-face
make
```

The output is a static RISC-V binary:

```text
irisoled-face
```

## Install On Duo

```bash
make scp
sshpass -p milkv ssh root@192.168.42.1 'sh /root/install-init.sh'
```

The install script:

- installs `/usr/bin/irisoled-face`
- creates `/etc/init.d/S99irisoled-face`
- starts the daemon immediately
- enables it for the next boot through the normal Buildroot init order

Manual daemon start:

```bash
/usr/bin/irisoled-face daemon --fb /dev/fb0 --udp-port 25250 --fps 20 --width 224 --height 112 --color 00d7ff --default normal
```

## Runtime Commands

Default state is `normal` at daemon startup. Temporary commands play a requested
expression for a repeat count or duration, then return to the current default
state. The default can be changed at runtime.

```bash
# list supported IrisOLED names
irisoled-face list

# play an expression sequence three times, then return to normal
irisoled-face play happy --repeat 3

# hold/play angry for five seconds, then return to normal
irisoled-face play angry --duration 5s

# busy is a convenience alias for focused
irisoled-face play busy --duration 10s

# set the default idle state; future play commands return to this
irisoled-face default sleepy
irisoled-face default busy

# reset the default state to normal and immediately show normal
irisoled-face normal

# stop the daemon
irisoled-face stop-daemon
```

Durations accept plain milliseconds, `ms`, or `s`:

```bash
irisoled-face play sleepy --duration 8000
irisoled-face play furious --duration 2500ms
irisoled-face play excited --duration 6s
```

## Low-Latency Upper-Host Control

The daemon also listens on UDP port `25250` by default. This avoids the
per-command SSH login cost from tools such as `plink.exe`, while still using the
same state machine as the local CLI. Send one ASCII command per UDP packet to
`192.168.42.1:25250`.

Accepted UDP payloads:

```text
default sad
irisoled-face default sad
normal
play happy --repeat 3
play angry --duration 5s
play busy duration=2500ms
```

Windows PowerShell fire-and-forget example:

```powershell
$u = New-Object Net.Sockets.UdpClient
$b = [Text.Encoding]::ASCII.GetBytes("default sad")
[void]$u.Send($b, $b.Length, "192.168.42.1", 25250)
$u.Close()
```

One-line PowerShell example:

```powershell
powershell -NoProfile -Command "$u=New-Object Net.Sockets.UdpClient; $b=[Text.Encoding]::ASCII.GetBytes('play happy --repeat 3'); [void]$u.Send($b,$b.Length,'192.168.42.1',25250); $u.Close()"
```

The daemon replies with `OK` or `ERR` as a UDP packet for clients that want an
acknowledgement.

## Supported Names

Native IrisOLED names:

```text
alert angry blink_down blink_up blink bored despair disoriented
excited focused furious happy look_down look_left look_right look_up
normal sad scared sleepy surprised wink_left wink_right worried
battery_full battery_low battery left_signal logo mode right_signal warning
```

Convenience aliases:

```text
idle -> normal
sleep, sleeping -> sleepy
busy -> focused
```

## State Machine

- Idle state loops the current default sequence. Startup default is `normal`.
- A command switches into command state.
- `--repeat N` plays that expression sequence N times.
- `--duration T` keeps that expression active until T expires.
- After command state completes, the daemon returns to the current default loop.
- `default <name>` changes the current default and immediately switches to it.
- `normal` resets the default to `normal` and immediately switches to it.

Commands replace the current command state immediately. For example, if `angry`
is playing and `happy` is sent, `happy` takes over right away.

Static IrisOLED expressions such as `sad`, `bored`, `worried`, and `surprised`
hold the exact requested bitmap. They do not insert dimmed fallback frames.

## Logs

The init script writes logs here:

```bash
tail -f /tmp/irisoled-face.log
```

## GIF Previews

Generate host-side GIF previews for every supported command sequence:

```bash
cd duo/CV1800B/lvgl-framebuffer-gc9a01/irisoled-face
make previews
open previews/index.html
```

The previews use the same 240x240 black canvas, 224x112 face size, and
`00d7ff` color as the default Duo daemon.
