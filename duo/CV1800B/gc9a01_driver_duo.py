#!/usr/bin/env python3
# gc9a01_driver_duo.py - 适配 Milkv Duo CV1800B

import spidev
from PIL import Image, ImageDraw
import os
import subprocess
import time

# 官方镜像上优先使用 c-gc9a01/test_hw_spi.c 做点屏验证。
# Python 版本保留为原型程序，使用 sysfs GPIO，避免依赖 gpiozero。
PIN_DC = 377     # Pin5  = GP3 = PWR_GPIO[25]
PIN_RST = 373    # Pin11 = GP8 = PWR_GPIO[21]
PIN_CS = 370     # Pin12 = GP9 = PWR_GPIO[18]

SPI_BUS = 0      # /dev/spidev0.0 = SPI2 on the tested official image
SPI_DEVICE = 0
SPI_SPEED_HZ = 8000000


def configure_pinmux():
    for mux in ("GP3/GP3", "GP6/SPI2_SCK", "GP7/SPI2_SDO", "GP8/GP8", "GP9/GP9"):
        subprocess.run(
            ["duo-pinmux", "-w", mux],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )


class SysfsGPIO:
    def __init__(self, pin):
        self.pin = pin
        self.path = f"/sys/class/gpio/gpio{pin}"
        if not os.path.exists(self.path):
            with open("/sys/class/gpio/export", "w") as f:
                f.write(str(pin))
            time.sleep(0.1)
        with open(f"{self.path}/direction", "w") as f:
            f.write("out")
        self.value = open(f"{self.path}/value", "w")

    def set(self, value):
        self.value.seek(0)
        self.value.write("1" if value else "0")
        self.value.flush()

    def on(self):
        self.set(1)

    def off(self):
        self.set(0)

class GC9A01_Duo:
    # GC9A01 命令
    SWRESET = 0x01
    SLPOUT = 0x11
    DISPON = 0x29
    CASET = 0x2A
    RASET = 0x2B
    RAMWR = 0x2C
    COLMOD = 0x3A
    MADCTL = 0x36
    
    def __init__(self):
        configure_pinmux()

        self.dc = SysfsGPIO(PIN_DC)
        self.rst = SysfsGPIO(PIN_RST)
        self.cs = SysfsGPIO(PIN_CS)
        self.cs.on()
        self.dc.on()
        self.rst.on()
        
        self.spi = spidev.SpiDev()
        self.spi.open(SPI_BUS, SPI_DEVICE)
        self.spi.max_speed_hz = SPI_SPEED_HZ
        self.spi.mode = 0
        
        self.width = 240
        self.height = 240
        
    def reset(self):
        self.rst.off()
        time.sleep(0.2)
        self.rst.on()
        time.sleep(0.2)

    def write_bytes(self, data):
        if isinstance(data, int):
            data = [data]
        for off in range(0, len(data), 512):
            self.spi.xfer2(data[off:off + 512], SPI_SPEED_HZ, 0, 8)
        
    def write_cmd(self, cmd, data=None):
        self.cs.off()
        self.dc.off()  # Command
        self.write_bytes([cmd])
        self.cs.on()
        
        if data is not None:
            self.cs.off()
            self.dc.on()  # Data
            self.write_bytes(data)
            self.cs.on()
            
    def init(self):
        self.reset()
        time.sleep(0.3)
        
        # 简化初始化序列
        self.write_cmd(0xEF)
        self.write_cmd(0xEB, [0x14])
        self.write_cmd(0xFE)
        self.write_cmd(0xEF)
        self.write_cmd(0xEB, [0x14])
        self.write_cmd(0x84, [0x40])
        self.write_cmd(0x85, [0xFF])
        self.write_cmd(0x86, [0xFF])
        self.write_cmd(0x87, [0xFF])
        self.write_cmd(0x88, [0x0A])
        self.write_cmd(0x89, [0x21])
        self.write_cmd(0x8A, [0x00])
        self.write_cmd(0x8B, [0x80])
        self.write_cmd(0x8C, [0x01])
        self.write_cmd(0x8D, [0x01])
        self.write_cmd(0x8E, [0xFF])
        self.write_cmd(0x8F, [0xFF])
        self.write_cmd(0xB6, [0x00, 0x00])
        self.write_cmd(0x3A, [0x05])  # 16-bit color
        self.write_cmd(0x90, [0x08, 0x08, 0x08, 0x08])
        self.write_cmd(0xBD, [0x06])
        self.write_cmd(0xBC, [0x00])
        self.write_cmd(0xFF, [0x60, 0x01, 0x04])
        self.write_cmd(0xC3, [0x13])
        self.write_cmd(0xC4, [0x13])
        self.write_cmd(0xC9, [0x22])
        self.write_cmd(0xBE, [0x11])
        self.write_cmd(0xE1, [0x10, 0x0E])
        self.write_cmd(0xDF, [0x21, 0x0c, 0x02])
        self.write_cmd(0xF0, [0x45, 0x09, 0x08, 0x08, 0x26, 0x2A])
        self.write_cmd(0xF1, [0x43, 0x70, 0x72, 0x36, 0x37, 0x6F])
        self.write_cmd(0xF2, [0x45, 0x09, 0x08, 0x08, 0x26, 0x2A])
        self.write_cmd(0xF3, [0x43, 0x70, 0x72, 0x36, 0x37, 0x6F])
        self.write_cmd(0xED, [0x1B, 0x0B])
        self.write_cmd(0xAE, [0x77])
        self.write_cmd(0xCD, [0x63])
        self.write_cmd(0x70, [0x07, 0x07, 0x04, 0x0E, 0x0F, 0x09, 0x07, 0x08, 0x03])
        self.write_cmd(0xE8, [0x34])
        self.write_cmd(0x62, [0x18, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70])
        self.write_cmd(0x63, [0x18, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70])
        self.write_cmd(0x64, [0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07])
        self.write_cmd(0x66, [0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00])
        self.write_cmd(0x67, [0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98])
        self.write_cmd(0x74, [0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00])
        self.write_cmd(0x98, [0x3e, 0x07])
        self.write_cmd(0x35)   # TEON
        self.write_cmd(0x21)   # INVON
        self.write_cmd(0x11)   # SLPOUT
        time.sleep(0.12)
        self.write_cmd(0x29)   # DISPON
        time.sleep(0.12)
        
    def set_window(self, x0, y0, x1, y1):
        self.write_cmd(self.CASET, [0x00, x0, 0x00, x1])
        self.write_cmd(self.RASET, [0x00, y0, 0x00, y1])
        
    def display_image(self, image):
        if image.size != (240, 240):
            image = image.resize((240, 240))
            
        # RGB565 转换
        pixels = list(image.getdata())
        buf = []
        for r, g, b in pixels:
            rgb = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            buf.append((rgb >> 8) & 0xFF)
            buf.append(rgb & 0xFF)
            
        self.set_window(0, 0, 239, 239)
        self.write_cmd(self.RAMWR, buf)
        
    def fill_screen(self, color):
        img = Image.new('RGB', (240, 240), color)
        self.display_image(img)
        
    def clear(self):
        self.fill_screen((0, 0, 0))

# 测试
if __name__ == "__main__":
    try:
        lcd = GC9A01_Duo()
        lcd.init()
        
        # 画测试画面
        img = Image.new('RGB', (240, 240), (0, 0, 0))
        draw = ImageDraw.Draw(img)
        
        # 红色圆环
        draw.ellipse([20, 20, 220, 220], outline=(255, 0, 0), width=5)
        # 绿色文字
        draw.text((60, 110), "Duo + GC9A01", fill=(0, 255, 0))
        # 蓝色小圆
        draw.ellipse([100, 100, 140, 140], fill=(0, 0, 255))
        
        lcd.display_image(img)
        print("屏幕初始化成功！")
        
    except Exception as e:
        print(f"错误: {e}")
