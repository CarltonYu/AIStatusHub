#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

#define GPIO_SYSFS_PATH "/sys/class/gpio"

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

static void fill_screen(int dc, int cs, int sck, int mosi, uint16_t color) {
    uint8_t col[4] = {0x00, 0, 0x00, 239};
    uint8_t row[4] = {0x00, 0, 0x00, 239};
    cmd(dc, cs, sck, mosi, 0x2A, col, 4);
    cmd(dc, cs, sck, mosi, 0x2B, row, 4);

    uint8_t hi = (color >> 8) & 0xFF;
    uint8_t lo = color & 0xFF;

    gset(cs, 0); gset(dc, 0); spi_byte(sck, mosi, 0x2C); gset(cs, 1); // RAMWR
    gset(cs, 0); gset(dc, 1);
    for (int i = 0; i < 240 * 240; i++) {
        spi_byte(sck, mosi, hi);
        spi_byte(sck, mosi, lo);
    }
    gset(cs, 1);
}

int main() {
    int dc, rst, cs, sck, mosi;
    printf("GC9A01 full init test\n");

    gpio_export(PIN_DC); gpio_export(PIN_RST); gpio_export(PIN_CS);
    gpio_export(PIN_SCK); gpio_export(PIN_MOSI);

    dc = gpio_open(PIN_DC); rst = gpio_open(PIN_RST); cs = gpio_open(PIN_CS);
    sck = gpio_open(PIN_SCK); mosi = gpio_open(PIN_MOSI);
    if (dc < 0 || rst < 0 || cs < 0 || sck < 0 || mosi < 0) {
        fprintf(stderr, "GPIO open failed\n"); return 1;
    }

    gset(cs, 1); gset(sck, 0); gset(mosi, 0); gset(dc, 1); gset(rst, 1);

    printf("Resetting...\n");
    gset(rst, 1); usleep(200000);
    gset(rst, 0); usleep(200000);
    gset(rst, 1); usleep(200000);

    printf("Sending init sequence...\n");
    cmd(dc, cs, sck, mosi, 0xEF, NULL, 0);
    cmd(dc, cs, sck, mosi, 0xEB, (uint8_t[]){0x14}, 1);
    cmd(dc, cs, sck, mosi, 0xFE, NULL, 0);
    cmd(dc, cs, sck, mosi, 0xEF, NULL, 0);
    cmd(dc, cs, sck, mosi, 0xEB, (uint8_t[]){0x14}, 1);
    cmd(dc, cs, sck, mosi, 0x84, (uint8_t[]){0x40}, 1);
    cmd(dc, cs, sck, mosi, 0x85, (uint8_t[]){0xFF}, 1);
    cmd(dc, cs, sck, mosi, 0x86, (uint8_t[]){0xFF}, 1);
    cmd(dc, cs, sck, mosi, 0x87, (uint8_t[]){0xFF}, 1);
    cmd(dc, cs, sck, mosi, 0x88, (uint8_t[]){0x0A}, 1);
    cmd(dc, cs, sck, mosi, 0x89, (uint8_t[]){0x21}, 1);
    cmd(dc, cs, sck, mosi, 0x8A, (uint8_t[]){0x00}, 1);
    cmd(dc, cs, sck, mosi, 0x8B, (uint8_t[]){0x80}, 1);
    cmd(dc, cs, sck, mosi, 0x8C, (uint8_t[]){0x01}, 1);
    cmd(dc, cs, sck, mosi, 0x8D, (uint8_t[]){0x01}, 1);
    cmd(dc, cs, sck, mosi, 0x8E, (uint8_t[]){0xFF}, 1);
    cmd(dc, cs, sck, mosi, 0x8F, (uint8_t[]){0xFF}, 1);
    cmd(dc, cs, sck, mosi, 0xB6, (uint8_t[]){0x00, 0x00}, 2);
    cmd(dc, cs, sck, mosi, 0x3A, (uint8_t[]){0x05}, 1);
    cmd(dc, cs, sck, mosi, 0x90, (uint8_t[]){0x08, 0x08, 0x08, 0x08}, 4);
    cmd(dc, cs, sck, mosi, 0xBD, (uint8_t[]){0x06}, 1);
    cmd(dc, cs, sck, mosi, 0xBC, (uint8_t[]){0x00}, 1);
    cmd(dc, cs, sck, mosi, 0xFF, (uint8_t[]){0x60, 0x01, 0x04}, 3);
    cmd(dc, cs, sck, mosi, 0xC3, (uint8_t[]){0x13}, 1);
    cmd(dc, cs, sck, mosi, 0xC4, (uint8_t[]){0x13}, 1);
    cmd(dc, cs, sck, mosi, 0xC9, (uint8_t[]){0x22}, 1);
    cmd(dc, cs, sck, mosi, 0xBE, (uint8_t[]){0x11}, 1);
    cmd(dc, cs, sck, mosi, 0xE1, (uint8_t[]){0x10, 0x0E}, 2);
    cmd(dc, cs, sck, mosi, 0xDF, (uint8_t[]){0x21, 0x0c, 0x02}, 3);
    cmd(dc, cs, sck, mosi, 0xF0, (uint8_t[]){0x45, 0x09, 0x08, 0x08, 0x26, 0x2A}, 6);
    cmd(dc, cs, sck, mosi, 0xF1, (uint8_t[]){0x43, 0x70, 0x72, 0x36, 0x37, 0x6F}, 6);
    cmd(dc, cs, sck, mosi, 0xF2, (uint8_t[]){0x45, 0x09, 0x08, 0x08, 0x26, 0x2A}, 6);
    cmd(dc, cs, sck, mosi, 0xF3, (uint8_t[]){0x43, 0x70, 0x72, 0x36, 0x37, 0x6F}, 6);
    cmd(dc, cs, sck, mosi, 0xED, (uint8_t[]){0x1B, 0x0B}, 2);
    cmd(dc, cs, sck, mosi, 0xAE, (uint8_t[]){0x77}, 1);
    cmd(dc, cs, sck, mosi, 0xCD, (uint8_t[]){0x63}, 1);
    cmd(dc, cs, sck, mosi, 0x70, (uint8_t[]){0x07, 0x07, 0x04, 0x0E, 0x0F, 0x09, 0x07, 0x08, 0x03}, 9);
    cmd(dc, cs, sck, mosi, 0xE8, (uint8_t[]){0x34}, 1);
    cmd(dc, cs, sck, mosi, 0x62, (uint8_t[]){0x18, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70}, 12);
    cmd(dc, cs, sck, mosi, 0x63, (uint8_t[]){0x18, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70}, 12);
    cmd(dc, cs, sck, mosi, 0x64, (uint8_t[]){0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07}, 7);
    cmd(dc, cs, sck, mosi, 0x66, (uint8_t[]){0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00}, 10);
    cmd(dc, cs, sck, mosi, 0x67, (uint8_t[]){0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98}, 10);
    cmd(dc, cs, sck, mosi, 0x74, (uint8_t[]){0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00}, 7);
    cmd(dc, cs, sck, mosi, 0x98, (uint8_t[]){0x3e, 0x07}, 2);
    cmd(dc, cs, sck, mosi, 0x35, NULL, 0);
    cmd(dc, cs, sck, mosi, 0x21, NULL, 0);
    cmd(dc, cs, sck, mosi, 0x11, NULL, 0); // SLPOUT
    usleep(120000);
    cmd(dc, cs, sck, mosi, 0x29, NULL, 0); // DISPON
    usleep(120000);

    printf("Filling screen white...\n");
    fill_screen(dc, cs, sck, mosi, 0xFFFF); // white
    printf("Done. Screen should be white now.\n");

    close(dc); close(rst); close(cs); close(sck); close(mosi);
    return 0;
}
