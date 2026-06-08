#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

#define GPIO_SYSFS_PATH "/sys/class/gpio"

// sysfs GPIO numbers = 480 + GPx on Duo
#define PIN_DC   483
#define PIN_SCK  484
#define PIN_MOSI 485
#define PIN_CS   488
#define PIN_RST  487

static int gpio_export(int pin) {
    char path[128];
    snprintf(path, sizeof(path), "%s/gpio%d", GPIO_SYSFS_PATH, pin);
    if (access(path, F_OK) == 0) return 0;
    int fd = open(GPIO_SYSFS_PATH "/export", O_WRONLY);
    if (fd < 0) { perror("export"); return -1; }
    char buf[8]; int n = snprintf(buf, sizeof(buf), "%d", pin);
    write(fd, buf, n); close(fd);
    usleep(100000); return 0;
}

static int gpio_open(int pin) {
    char path[128];
    snprintf(path, sizeof(path), "%s/gpio%d/value", GPIO_SYSFS_PATH, pin);
    int fd = open(path, O_WRONLY);
    if (fd < 0) perror("open value");
    return fd;
}

static inline void gset(int fd, int v) { write(fd, v ? "1" : "0", 1); }

static void spi_byte(int sck, int mosi, uint8_t b) {
    for (int i = 7; i >= 0; i--) {
        gset(sck, 0);
        gset(mosi, (b >> i) & 1);
        gset(sck, 1);
    }
}

static void cmd(int dc, int cs, int sck, int mosi, uint8_t c, const uint8_t *d, int len) {
    gset(cs, 0); gset(dc, 0); spi_byte(sck, mosi, c); gset(cs, 1);
    if (d && len > 0) {
        gset(cs, 0); gset(dc, 1);
        for (int i = 0; i < len; i++) spi_byte(sck, mosi, d[i]);
        gset(cs, 1);
    }
}

int main() {
    int dc, rst, cs, sck, mosi;
    printf("GC9A01 init test\n");

    gpio_export(PIN_DC); gpio_export(PIN_RST); gpio_export(PIN_CS);
    gpio_export(PIN_SCK); gpio_export(PIN_MOSI);

    dc = gpio_open(PIN_DC); rst = gpio_open(PIN_RST); cs = gpio_open(PIN_CS);
    sck = gpio_open(PIN_SCK); mosi = gpio_open(PIN_MOSI);
    if (dc < 0 || rst < 0 || cs < 0 || sck < 0 || mosi < 0) {
        fprintf(stderr, "GPIO open failed\n"); return 1;
    }

    gset(cs, 1); gset(sck, 0); gset(mosi, 0); gset(dc, 1); gset(rst, 1);

    // Reset
    gset(rst, 1); usleep(200000);
    gset(rst, 0); usleep(200000);
    gset(rst, 1); usleep(200000);

    // Minimal init sequence
    cmd(dc, cs, sck, mosi, 0xEF, NULL, 0);
    cmd(dc, cs, sck, mosi, 0xEB, (uint8_t[]){0x14}, 1);
    cmd(dc, cs, sck, mosi, 0xFE, NULL, 0);
    cmd(dc, cs, sck, mosi, 0xEF, NULL, 0);
    cmd(dc, cs, sck, mosi, 0x3A, (uint8_t[]){0x05}, 1); // 16-bit
    cmd(dc, cs, sck, mosi, 0x36, (uint8_t[]){0x48}, 1); // MADCTL
    cmd(dc, cs, sck, mosi, 0x11, NULL, 0); // SLPOUT
    usleep(120000);
    cmd(dc, cs, sck, mosi, 0x29, NULL, 0); // DISPON
    usleep(120000);

    printf("Init done. Filling a small area red...\n");

    // Set window to center 20x20 area (240x240 screen, center at 120,120)
    uint8_t col[4] = {0x00, 110, 0x00, 129};
    uint8_t row[4] = {0x00, 110, 0x00, 129};
    cmd(dc, cs, sck, mosi, 0x2A, col, 4);
    cmd(dc, cs, sck, mosi, 0x2B, row, 4);

    // RAMWR + 20x20 red pixels (RGB565 = 0xF800)
    gset(cs, 0); gset(dc, 0); spi_byte(sck, mosi, 0x2C); gset(cs, 1);
    gset(cs, 0); gset(dc, 1);
    for (int i = 0; i < 20 * 20; i++) {
        spi_byte(sck, mosi, 0xF8); // hi
        spi_byte(sck, mosi, 0x00); // lo
    }
    gset(cs, 1);

    printf("Done! Top-left 20x20 should be red.\n");

    close(dc); close(rst); close(cs); close(sck); close(mosi);
    return 0;
}
