#include "gc9a01.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define GPIO_SYSFS_PATH "/sys/class/gpio"

static int gpio_export(int pin) {
    char path[128];
    int fd;
    
    snprintf(path, sizeof(path), "%s/gpio%d", GPIO_SYSFS_PATH, pin);
    if (access(path, F_OK) == 0) {
        return 0;
    }
    
    fd = open(GPIO_SYSFS_PATH "/export", O_WRONLY);
    if (fd < 0) {
        perror("gpio export");
        return -1;
    }
    char buf[8];
    int n = snprintf(buf, sizeof(buf), "%d", pin);
    write(fd, buf, n);
    close(fd);
    usleep(100000);
    return 0;
}

static int gpio_open_value(int pin) {
    char path[128];
    snprintf(path, sizeof(path), "%s/gpio%d/value", GPIO_SYSFS_PATH, pin);
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        perror("gpio open value");
    }
    return fd;
}

static int gpio_set_direction(int pin, const char *dir) {
    char path[128];
    snprintf(path, sizeof(path), "%s/gpio%d/direction", GPIO_SYSFS_PATH, pin);
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        perror("gpio direction");
        return -1;
    }
    write(fd, dir, strlen(dir));
    close(fd);
    return 0;
}

static inline void gpio_set(int fd, int val) {
    if (val) {
        write(fd, "1", 1);
    } else {
        write(fd, "0", 1);
    }
}

static inline void spi_write_bit(gc9a01_t *dev, uint8_t bit) {
    // CPOL=0, CPHA=0: data sampled on rising edge
    gpio_set(dev->fd_sck, 0);
    gpio_set(dev->fd_mosi, bit ? 1 : 0);
    gpio_set(dev->fd_sck, 1);
}

static void spi_write_byte(gc9a01_t *dev, uint8_t byte) {
    for (int i = 7; i >= 0; i--) {
        spi_write_bit(dev, (byte >> i) & 1);
    }
}

static void spi_write_data(gc9a01_t *dev, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        spi_write_byte(dev, data[i]);
    }
}

bool gc9a01_init(gc9a01_t *dev) {
    memset(dev, 0, sizeof(*dev));
    dev->fd_dc = -1;
    dev->fd_rst = -1;
    dev->fd_cs = -1;
    dev->fd_sck = -1;
    dev->fd_mosi = -1;

    int pins[] = {GC9A01_PIN_DC, GC9A01_PIN_RST, GC9A01_PIN_CS, GC9A01_PIN_SCK, GC9A01_PIN_MOSI};
    for (size_t i = 0; i < sizeof(pins)/sizeof(pins[0]); i++) {
        if (gpio_export(pins[i]) < 0) {
            fprintf(stderr, "Failed to export GPIO %d\n", pins[i]);
            return false;
        }
        if (gpio_set_direction(pins[i], "out") < 0) {
            fprintf(stderr, "Failed to set direction for GPIO %d\n", pins[i]);
            return false;
        }
    }

    dev->fd_dc = gpio_open_value(GC9A01_PIN_DC);
    dev->fd_rst = gpio_open_value(GC9A01_PIN_RST);
    dev->fd_cs = gpio_open_value(GC9A01_PIN_CS);
    dev->fd_sck = gpio_open_value(GC9A01_PIN_SCK);
    dev->fd_mosi = gpio_open_value(GC9A01_PIN_MOSI);

    if (dev->fd_dc < 0 || dev->fd_rst < 0 || dev->fd_cs < 0 || dev->fd_sck < 0 || dev->fd_mosi < 0) {
        fprintf(stderr, "Failed to open GPIO value files\n");
        return false;
    }

    // Initialize idle state
    gpio_set(dev->fd_cs, 1);
    gpio_set(dev->fd_sck, 0);
    gpio_set(dev->fd_mosi, 0);
    gpio_set(dev->fd_dc, 1);
    gpio_set(dev->fd_rst, 1);

    gc9a01_reset(dev);

    // GC9A01 initialization sequence (from datasheet / Python reference)
    gc9a01_write_cmd(dev, 0xEF, NULL, 0);
    gc9a01_write_cmd(dev, 0xEB, (uint8_t[]){0x14}, 1);
    gc9a01_write_cmd(dev, 0xFE, NULL, 0);
    gc9a01_write_cmd(dev, 0xEF, NULL, 0);
    gc9a01_write_cmd(dev, 0xEB, (uint8_t[]){0x14}, 1);
    gc9a01_write_cmd(dev, 0x84, (uint8_t[]){0x40}, 1);
    gc9a01_write_cmd(dev, 0x85, (uint8_t[]){0xFF}, 1);
    gc9a01_write_cmd(dev, 0x86, (uint8_t[]){0xFF}, 1);
    gc9a01_write_cmd(dev, 0x87, (uint8_t[]){0xFF}, 1);
    gc9a01_write_cmd(dev, 0x88, (uint8_t[]){0x0A}, 1);
    gc9a01_write_cmd(dev, 0x89, (uint8_t[]){0x21}, 1);
    gc9a01_write_cmd(dev, 0x8A, (uint8_t[]){0x00}, 1);
    gc9a01_write_cmd(dev, 0x8B, (uint8_t[]){0x80}, 1);
    gc9a01_write_cmd(dev, 0x8C, (uint8_t[]){0x01}, 1);
    gc9a01_write_cmd(dev, 0x8D, (uint8_t[]){0x01}, 1);
    gc9a01_write_cmd(dev, 0x8E, (uint8_t[]){0xFF}, 1);
    gc9a01_write_cmd(dev, 0x8F, (uint8_t[]){0xFF}, 1);
    gc9a01_write_cmd(dev, 0xB6, (uint8_t[]){0x00, 0x00}, 2);
    gc9a01_write_cmd(dev, 0x3A, (uint8_t[]){0x05}, 1); // 16-bit color
    gc9a01_write_cmd(dev, 0x90, (uint8_t[]){0x08, 0x08, 0x08, 0x08}, 4);
    gc9a01_write_cmd(dev, 0xBD, (uint8_t[]){0x06}, 1);
    gc9a01_write_cmd(dev, 0xBC, (uint8_t[]){0x00}, 1);
    gc9a01_write_cmd(dev, 0xFF, (uint8_t[]){0x60, 0x01, 0x04}, 3);
    gc9a01_write_cmd(dev, 0xC3, (uint8_t[]){0x13}, 1);
    gc9a01_write_cmd(dev, 0xC4, (uint8_t[]){0x13}, 1);
    gc9a01_write_cmd(dev, 0xC9, (uint8_t[]){0x22}, 1);
    gc9a01_write_cmd(dev, 0xBE, (uint8_t[]){0x11}, 1);
    gc9a01_write_cmd(dev, 0xE1, (uint8_t[]){0x10, 0x0E}, 2);
    gc9a01_write_cmd(dev, 0xDF, (uint8_t[]){0x21, 0x0c, 0x02}, 3);
    gc9a01_write_cmd(dev, 0xF0, (uint8_t[]){0x45, 0x09, 0x08, 0x08, 0x26, 0x2A}, 6);
    gc9a01_write_cmd(dev, 0xF1, (uint8_t[]){0x43, 0x70, 0x72, 0x36, 0x37, 0x6F}, 6);
    gc9a01_write_cmd(dev, 0xF2, (uint8_t[]){0x45, 0x09, 0x08, 0x08, 0x26, 0x2A}, 6);
    gc9a01_write_cmd(dev, 0xF3, (uint8_t[]){0x43, 0x70, 0x72, 0x36, 0x37, 0x6F}, 6);
    gc9a01_write_cmd(dev, 0xED, (uint8_t[]){0x1B, 0x0B}, 2);
    gc9a01_write_cmd(dev, 0xAE, (uint8_t[]){0x77}, 1);
    gc9a01_write_cmd(dev, 0xCD, (uint8_t[]){0x63}, 1);
    gc9a01_write_cmd(dev, 0x70, (uint8_t[]){0x07, 0x07, 0x04, 0x0E, 0x0F, 0x09, 0x07, 0x08, 0x03}, 9);
    gc9a01_write_cmd(dev, 0xE8, (uint8_t[]){0x34}, 1);
    gc9a01_write_cmd(dev, 0x62, (uint8_t[]){0x18, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70}, 12);
    gc9a01_write_cmd(dev, 0x63, (uint8_t[]){0x18, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70}, 12);
    gc9a01_write_cmd(dev, 0x64, (uint8_t[]){0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07}, 7);
    gc9a01_write_cmd(dev, 0x66, (uint8_t[]){0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00}, 10);
    gc9a01_write_cmd(dev, 0x67, (uint8_t[]){0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98}, 10);
    gc9a01_write_cmd(dev, 0x74, (uint8_t[]){0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00}, 7);
    gc9a01_write_cmd(dev, 0x98, (uint8_t[]){0x3e, 0x07}, 2);
    gc9a01_write_cmd(dev, 0x35, NULL, 0); // TEON
    gc9a01_write_cmd(dev, 0x21, NULL, 0); // INVON
    gc9a01_write_cmd(dev, 0x11, NULL, 0); // SLPOUT
    usleep(120000);
    gc9a01_write_cmd(dev, 0x29, NULL, 0); // DISPON
    usleep(120000);

    return true;
}

void gc9a01_deinit(gc9a01_t *dev) {
    if (dev->fd_dc >= 0) close(dev->fd_dc);
    if (dev->fd_rst >= 0) close(dev->fd_rst);
    if (dev->fd_cs >= 0) close(dev->fd_cs);
    if (dev->fd_sck >= 0) close(dev->fd_sck);
    if (dev->fd_mosi >= 0) close(dev->fd_mosi);
    memset(dev, 0, sizeof(*dev));
}

void gc9a01_reset(gc9a01_t *dev) {
    gpio_set(dev->fd_rst, 1);
    usleep(200000);
    gpio_set(dev->fd_rst, 0);
    usleep(200000);
    gpio_set(dev->fd_rst, 1);
    usleep(200000);
}

void gc9a01_write_cmd(gc9a01_t *dev, uint8_t cmd, const uint8_t *data, size_t len) {
    gpio_set(dev->fd_cs, 0);
    gpio_set(dev->fd_dc, 0); // Command
    spi_write_byte(dev, cmd);
    
    if (data && len > 0) {
        gpio_set(dev->fd_dc, 1); // Data
        spi_write_data(dev, data, len);
    }
    gpio_set(dev->fd_cs, 1);
}

void gc9a01_set_window(gc9a01_t *dev, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    uint8_t col[4] = {0x00, x0, 0x00, x1};
    uint8_t row[4] = {0x00, y0, 0x00, y1};
    gc9a01_write_cmd(dev, 0x2A, col, 4); // CASET
    gc9a01_write_cmd(dev, 0x2B, row, 4); // RASET
}

void gc9a01_write_pixels(gc9a01_t *dev, const uint16_t *rgb565_data, size_t count) {
    size_t byte_len = count * 2;
    uint8_t *buf = malloc(byte_len);
    if (!buf) return;
    
    for (size_t i = 0; i < count; i++) {
        buf[i * 2] = (rgb565_data[i] >> 8) & 0xFF;
        buf[i * 2 + 1] = rgb565_data[i] & 0xFF;
    }
    
    gc9a01_write_cmd(dev, 0x2C, buf, byte_len); // RAMWR
    free(buf);
}

void gc9a01_display_image(gc9a01_t *dev, const uint16_t *rgb565_buf) {
    gc9a01_set_window(dev, 0, 0, GC9A01_WIDTH - 1, GC9A01_HEIGHT - 1);
    gc9a01_write_pixels(dev, rgb565_buf, GC9A01_WIDTH * GC9A01_HEIGHT);
}

void gc9a01_fill_screen(gc9a01_t *dev, uint16_t color) {
    gc9a01_set_window(dev, 0, 0, GC9A01_WIDTH - 1, GC9A01_HEIGHT - 1);
    
    uint8_t hi = (color >> 8) & 0xFF;
    uint8_t lo = color & 0xFF;
    
    gpio_set(dev->fd_cs, 0);
    gpio_set(dev->fd_dc, 0); // Command
    spi_write_byte(dev, 0x2C); // RAMWR
    gpio_set(dev->fd_dc, 1); // Data
    
    size_t total_pixels = GC9A01_WIDTH * GC9A01_HEIGHT;
    for (size_t i = 0; i < total_pixels; i++) {
        spi_write_byte(dev, hi);
        spi_write_byte(dev, lo);
    }
    
    gpio_set(dev->fd_cs, 1);
}
