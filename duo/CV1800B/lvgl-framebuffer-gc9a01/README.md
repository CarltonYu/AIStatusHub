# GC9A01 Linux Framebuffer + LVGL Version

This version follows Milk-V's official "SPI Display LVGL" route: the kernel
creates `/dev/fb0`, then LVGL writes to the Linux framebuffer. The official
example targets ST7789V 240x320; this version adapts the same path for the
current Duo CV1800B + GC9A01 240x240 wiring.

References:

- Milk-V SPI Display LVGL: https://milkv.io/docs/duo/resources/spilvgl
- Milk-V Duo pinmux: https://milkv.io/docs/duo/application-development/pinmux
- Milk-V Duo GPIO pinout: https://milkv.io/docs/duo/getting-started/duo
- Official LVGL framebuffer demo: https://github.com/milkv-duo/duo-lvgl-fb-demo

## What This Version Changes

- U-Boot pinmux is changed before Linux boots:
  - GP3 / Pin 5 becomes GPIO for LCD DC.
  - GP8 / Pin 11 becomes GPIO for LCD reset.
  - GP6, GP7, GP9 stay on SPI2 SCK, SPI2 SDO, SPI2 CS.
- Linux DTS replaces `spidev@0` on `&spi2` with `gc9a01@0`.
- A new fbtft driver `fb_gc9a01` is added, using the init sequence that matched
  the previous direct SPI test.
- The LVGL demo is patched from 240x320 to 240x240.

This route is different from `c-gc9a01/test_hw_spi`: the direct SPI test uses
manual GPIO CS, while this framebuffer version uses the hardware SPI2 CS pin.
For this version, GP9 / Pin 12 must be `SPI2_CS_X`.

## Pin Binding

| LCD pin | Duo pin | Duo function in this version | Kernel reference |
| --- | --- | --- | --- |
| VCC | Pin 36 | 3V3 out | power |
| GND | GND | GND | ground |
| SCL | Pin 9 / GP6 | SPI2_SCK | `SD1_CLK` |
| SDA | Pin 10 / GP7 | SPI2_SDO | `SD1_CMD` |
| DC | Pin 5 / GP3 | GPIO, PWR_GPIO[25] | `&porte 25` |
| CS | Pin 12 / GP9 | SPI2_CS_X | `SD1_D3` |
| RST | Pin 11 / GP8 | GPIO, PWR_GPIO[21] | `&porte 21` |

## Patch SDK

Run this on a Linux x86_64 build machine for the Duo SDK. The SDK is large and
the official build flow is Linux-oriented.

```bash
cd duo/CV1800B/lvgl-framebuffer-gc9a01
./scripts/apply-sdk-patch.sh ../duo-buildroot-sdk
```

Then build the Duo SD image:

```bash
cd ../duo-buildroot-sdk
./build.sh milkv-duo-sd
```

Burn the generated image from the SDK `out/` directory to the microSD card and
boot the Duo.

## Verify Framebuffer On Duo

After booting the patched image:

```bash
ssh root@192.168.42.1
dmesg | grep -i gc9a01
ls -l /dev/fb0
cat /sys/class/graphics/fb0/name
```

Expected signal:

```text
graphics fb0: fb_gc9a01 frame buffer, 240x240
```

A quick raw framebuffer test:

```bash
dd if=/dev/urandom of=/dev/fb0 bs=115200 count=1
dd if=/dev/zero of=/dev/fb0 bs=115200 count=1
```

Or build and run the included smoke test. It defaults to `zig cc` so it can be
built from macOS like the existing direct SPI test:

```bash
cd duo/CV1800B/lvgl-framebuffer-gc9a01/fb-smoke
make
scp -O build/fb-fill root@192.168.42.1:/root/
ssh root@192.168.42.1 '/root/fb-fill'
```

## 200x200 Eye Animation On Framebuffer

The `fb-eye` player uses the same RGB565 animation package as the direct SPI
player, but writes to `/dev/fb0`. Use this after booting a patched image that
exposes the SPI LCD as a Linux framebuffer:

```bash
cd duo/CV1800B/eyes-resources
.venv/bin/python generate_smooth_animation_bin.py 200 240

cd ../lvgl-framebuffer-gc9a01/fb-eye
make
sshpass -p milkv scp -O fb-eye-anim root@192.168.42.1:/root/
sshpass -p milkv scp -O ../../eyes-resources/parsed/eye_animation_200x200_smooth_240f.bin root@192.168.42.1:/root/eye_animation.bin
sshpass -p milkv ssh root@192.168.42.1 '/root/fb-eye-anim /root/eye_animation.bin /dev/fb0 60'
```

For a maximum-throughput test, pass `0` as the FPS value:

```bash
/root/fb-eye-anim /root/eye_animation.bin /dev/fb0 0
```

## IrisOLED Robot Expressions

For robot-style expression states, use `irisoled-face`. It vendors the
MIT-licensed `orji123/Irisoled` bitmap set, runs as a daemon, and accepts
runtime commands through a UNIX socket or UDP port `25250`:

```bash
cd duo/CV1800B/lvgl-framebuffer-gc9a01/irisoled-face
make
make scp
sshpass -p milkv ssh root@192.168.42.1 'sh /root/install-init.sh'
```

Examples on Duo:

```bash
irisoled-face play happy --repeat 3
irisoled-face play angry --duration 5s
irisoled-face play busy --duration 10s
irisoled-face normal
```

Examples from an upper host on the USB network:

```powershell
powershell -NoProfile -Command "$u=New-Object Net.Sockets.UdpClient; $b=[Text.Encoding]::ASCII.GetBytes('default sad'); [void]$u.Send($b,$b.Length,'192.168.42.1',25250); $u.Close()"
powershell -NoProfile -Command "$u=New-Object Net.Sockets.UdpClient; $b=[Text.Encoding]::ASCII.GetBytes('play happy --repeat 3'); [void]$u.Send($b,$b.Length,'192.168.42.1',25250); $u.Close()"
```

See `irisoled-face/README.md` for the supported IrisOLED names and state
machine behavior.

On the currently tested official image, `/dev/fb0` does not exist yet. A
runtime attempt to bind the built-in `fb_st7789v` driver to the existing SPI2
`spidev` device failed with:

```text
fb_st7789v spi0.0: buswidth is not set
fb_st7789v: probe of spi0.0 failed with error -22
```

That confirms this route needs the patched boot DTS/kernel image rather than
only copying a user-space program to the stock image.

## Build LVGL Demo

Clone the official demo and apply the 240x240 patch:

```bash
git clone https://github.com/milkv-duo/duo-lvgl-fb-demo.git
cd duo-lvgl-fb-demo
git apply /path/to/AIStatusHub.git/duo/CV1800B/lvgl-framebuffer-gc9a01/patches/0002-duo-lvgl-fb-demo-gc9a01-240x240.patch
```

Use the Milk-V RISC-V musl toolchain from `host-tools` or from the SDK:

```bash
export PATH=$PATH:/path/to/host-tools/gcc/riscv64-linux-musl-x86_64/bin
cd lv_port_linux_frame_buffer
make clean
make -j
scp -O build/bin/demo root@192.168.42.1:/root/
ssh root@192.168.42.1 'chmod +x /root/demo && /root/demo'
```

If colors look byte-swapped, edit `lv_port_linux_frame_buffer/lv_conf.h` and set:

```c
#define LV_COLOR_16_SWAP 1
```

If red and blue are swapped but byte order is otherwise correct, add `bgr;` to
the `gc9a01@0` DTS node in the SDK patch and rebuild.

## Troubleshooting

If `/dev/fb0` is missing, the patched kernel/DTS image is not active. Check
`dmesg | grep -Ei 'gc9a01|fbtft|spi'`.

If `/dev/fb0` exists but the screen only has backlight, check runtime pinmux:

```bash
duo-pinmux -r GP3
duo-pinmux -r GP6
duo-pinmux -r GP7
duo-pinmux -r GP8
duo-pinmux -r GP9
```

Expected:

- GP3 = GPIO / GP3
- GP6 = SPI2_SCK
- GP7 = SPI2_SDO
- GP8 = GPIO / GP8
- GP9 = SPI2_CS_X

If the image is rotated or mirrored, adjust `rotate = <0>;` in the GC9A01 DTS
node to `90`, `180`, or `270`, rebuild, and retest.
