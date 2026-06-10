# GC9A01 round LCD driver for MilkV Duo (CV1800B)

This directory contains small C test programs for a 240x240 GC9A01 SPI LCD.

## Current Wiring

The current wiring matches Duo SPI2 for the data pins:

| LCD pin | Duo physical pin | Duo name | Linux/sysfs GPIO | Use |
|---------|------------------|----------|------------------|-----|
| VCC | Pin 36 | 3V3(OUT) | - | Power |
| GND | GND | GND | - | Ground |
| SCL | Pin 9 | GP6 / SPI2_SCK | 375 | SPI clock |
| SDA | Pin 10 | GP7 / SPI2_SDO | 374 | SPI MOSI |
| DC | Pin 5 | GP3 | 377 | Data/command GPIO |
| CS | Pin 12 | GP9 | 370 | Manual chip-select GPIO |
| RST | Pin 11 | GP8 | 373 | Reset GPIO |

`test_hw_spi.c` uses `/dev/spidev0.0` for SPI2 clock/MOSI on the tested
official image and controls DC/CS/RST through sysfs GPIO.

## Important Pinmux Detail

The official Duo image may boot with:

- `GP3` as `UART4_RX`
- `GP8` as `SPI2_SDI`
- `GP9` as `SPI2_CS_X`

For this wiring, the program must switch:

```bash
duo-pinmux -w GP3/GP3
duo-pinmux -w GP6/SPI2_SCK
duo-pinmux -w GP7/SPI2_SDO
duo-pinmux -w GP8/GP8
duo-pinmux -w GP9/GP9
```

The current `test_hw_spi.c` does this automatically before opening GPIOs.

## Build

On macOS, build with Zig:

```bash
cd duo/CV1800B/c-gc9a01
make test-hw-spi        # basic color test
make eye-anim           # animated eye player
```

## Animated Eye Player (`eye-anim`)

Build + upload (default 200x200 eye, 240 smooth frames, black border):

```bash
make scp-eye-anim       # uploads eye-anim + 240x240 bin
```

The upload target generates this file if it is missing:

```text
../eyes-resources/parsed/eye_animation_200x200_smooth_240f.bin
```

For smoother frame-rates, use a smaller animation:

```bash
make scp-eye-anim-240   # 240x240, best detail
make scp-eye-anim-160   # 160x160, faster
make scp-eye-anim-128   # 128x128, ~60 FPS
```

Run on Duo:

```bash
/root/eye-anim [bin_path] [target_fps, 0=uncapped] [spi_speed_hz] [auto|preload|stream]

# examples
/root/eye-anim /root/eye_animation.bin 60 20000000 auto
/root/eye-anim /root/eye_animation.bin 0  20000000 stream   # no sleep; SPI-limited
```

`eye-anim` now sets the required `duo-pinmux` modes automatically, matching the
pin binding above. In `auto` mode it preloads small animation files into RAM and
streams larger 240-frame files from storage to avoid memory pressure on Duo.

### Why frame-rate is limited

The Duo kernel’s `spidev` has `bufsiz=4096`. A 240×240 RGB565 frame is 115200 bytes,
so each frame must be split into 29 `ioctl` transfers. That overhead limits real FPS
to about **15–16 FPS** even at 20 MHz SPI. The 240-frame animation gives smoother
motion, but the visible refresh rate is still capped by SPI transfer throughput.

| Resolution | Frame bytes | `ioctl` chunks | Actual FPS @ 20MHz | Quality |
|------------|-------------|----------------|--------------------|---------|
| 240×240 | 115200 | 29 | ~16 | Best |
| 160×160 | 51200  | 13 | **~30** | Good |
| 128×128 | 32768  | 8  | **~58** | Acceptable |

If you rebuild the Buildroot/kernel later, add `spidev.bufsiz=65536` to the kernel
command line; then 240×240 should also reach 30+ FPS in a single SPI transfer.

To regenerate a different frame count:

```bash
cd duo/CV1800B/eyes-resources
.venv/bin/python generate_smooth_animation_bin.py 200 360
```

## Copy To Duo

The official image uses Dropbear. Use legacy SCP mode:

```bash
sshpass -p "milkv" scp -O test-hw-spi root@192.168.42.1:/root/
```

## Run

```bash
sshpass -p "milkv" ssh root@192.168.42.1 /root/test-hw-spi
```

The test cycles red, green, blue, then white.

On the official image, this warning is expected and harmless:

```text
SPI_IOC_WR_MODE with SPI_NO_CS: Invalid argument
warning: falling back to SPI_MODE_0 with manual GPIO CS
```

The kernel rejects `SPI_NO_CS`, so the program falls back to normal SPI mode
while still driving the LCD CS line through GPIO370.

## Legacy Files

`gc9a01.c`, `gc9a01.h`, `main.c`, `test_init.c`, and `test_full_init.c` are
older software-SPI experiments. They are useful as references for the GC9A01
initialization sequence, but they are not the current recommended test path.
