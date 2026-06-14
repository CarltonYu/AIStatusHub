#include "gc9a01_hw.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/spi/spidev.h>

#define GPIO_SYSFS_PATH "/sys/class/gpio"
#define DEFAULT_SPI_SPEED_HZ 12000000
#define DEFAULT_CHUNK_SIZE 4096

static int gpio_export(int pin)
{
    char path[128];
    int fd;

    if (pin < 0)
        return 0;

    snprintf(path, sizeof(path), "%s/gpio%d", GPIO_SYSFS_PATH, pin);
    if (access(path, F_OK) == 0)
        return 0;

    fd = open(GPIO_SYSFS_PATH "/export", O_WRONLY);
    if (fd < 0) {
        perror("gpio export");
        return -1;
    }
    char buf[16];
    int n = snprintf(buf, sizeof(buf), "%d", pin);
    write(fd, buf, (size_t)n);
    close(fd);
    usleep(100000);
    return 0;
}

static int gpio_set_dir(int pin, const char *dir)
{
    char path[128];
    int fd;

    if (pin < 0)
        return 0;

    snprintf(path, sizeof(path), "%s/gpio%d/direction", GPIO_SYSFS_PATH, pin);
    fd = open(path, O_WRONLY);
    if (fd < 0) {
        perror("gpio direction");
        return -1;
    }
    write(fd, dir, strlen(dir));
    close(fd);
    return 0;
}

static int gpio_open(int pin)
{
    char path[128];

    if (pin < 0)
        return -1;

    snprintf(path, sizeof(path), "%s/gpio%d/value", GPIO_SYSFS_PATH, pin);
    return open(path, O_WRONLY);
}

static inline void gpio_set(int fd, int val)
{
    if (fd < 0)
        return;
    lseek(fd, 0, SEEK_SET);
    write(fd, val ? "1" : "0", 1);
}

int gc9a01_detect_gpio(int offset)
{
    DIR *d = opendir(GPIO_SYSFS_PATH);
    struct dirent *e;
    int found = -1;

    if (!d)
        return -1;

    while ((e = readdir(d))) {
        char label_path[128], base_path[128], label[64];
        int base;
        FILE *f;

        if (strncmp(e->d_name, "gpiochip", 8) != 0)
            continue;

        snprintf(label_path, sizeof(label_path), "%s/%s/label",
                 GPIO_SYSFS_PATH, e->d_name);
        snprintf(base_path, sizeof(base_path), "%s/%s/base",
                 GPIO_SYSFS_PATH, e->d_name);

        f = fopen(label_path, "r");
        if (!f)
            continue;
        if (!fgets(label, sizeof(label), f)) {
            fclose(f);
            continue;
        }
        fclose(f);
        label[strcspn(label, "\n")] = '\0';

        if (!strstr(label, "5021000.gpio") && !strstr(label, "porte"))
            continue;

        f = fopen(base_path, "r");
        if (!f)
            continue;
        if (fscanf(f, "%d", &base) == 1 && base >= 0)
            found = base + offset;
        fclose(f);
        if (found >= 0)
            break;
    }
    closedir(d);
    return found;
}

static int spi_xfer(int spi_fd, const uint8_t *tx, size_t len,
                    uint32_t speed_hz)
{
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = 0,
        .len = len,
        .speed_hz = speed_hz,
        .delay_usecs = 0,
        .bits_per_word = 8,
    };

    if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr) < 1) {
        perror("SPI_IOC_MESSAGE");
        return -1;
    }
    return 0;
}

static void write_cmd(struct gc9a01_hw *hw, uint8_t cmd,
                      const uint8_t *data, size_t len)
{
    gpio_set(hw->dc_fd, 0);
    spi_xfer(hw->spi_fd, &cmd, 1, hw->speed_hz);
    if (data && len > 0) {
        gpio_set(hw->dc_fd, 1);
        spi_xfer(hw->spi_fd, data, len, hw->speed_hz);
    }
}

static size_t detect_spidev_bufsiz(void)
{
    FILE *f = fopen("/sys/module/spidev/parameters/bufsiz", "r");
    unsigned long v = DEFAULT_CHUNK_SIZE;

    if (!f)
        return DEFAULT_CHUNK_SIZE;
    if (fscanf(f, "%lu", &v) != 1 || v < 256)
        v = DEFAULT_CHUNK_SIZE;
    fclose(f);
    return (size_t)v;
}

static int open_spi(const char *path, uint32_t speed_hz)
{
    int fd = open(path, O_RDWR);
    uint32_t mode = SPI_MODE_0;
    uint8_t bits = 8;

    if (fd < 0) {
        fprintf(stderr, "open %s: %s\n", path, strerror(errno));
        return -1;
    }

    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0)
        perror("SPI mode");
    if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0)
        perror("SPI bits");
    if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz) < 0)
        perror("SPI speed");

    return fd;
}

int gc9a01_hw_init(struct gc9a01_hw *hw, const char *spi_path,
                   int pin_dc, int pin_rst, uint32_t speed_hz)
{
    memset(hw, 0, sizeof(*hw));
    hw->spi_fd = -1;
    hw->dc_fd = -1;
    hw->rst_fd = -1;

    if (speed_hz == 0)
        speed_hz = DEFAULT_SPI_SPEED_HZ;
    hw->speed_hz = speed_hz;
    hw->chunk_size = detect_spidev_bufsiz();
    if (hw->chunk_size > 65536)
        hw->chunk_size = 65536;

    /* Auto-detect DC/RST GPIOs if not provided. */
    if (pin_dc < 0) {
        pin_dc = gc9a01_detect_gpio(25);
        if (pin_dc < 0)
            pin_dc = 377;
    }
    if (pin_rst < 0) {
        pin_rst = gc9a01_detect_gpio(21);
        if (pin_rst < 0)
            pin_rst = 373;
    }

    if (gpio_export(pin_dc) < 0 || gpio_export(pin_rst) < 0)
        goto fail;
    if (gpio_set_dir(pin_dc, "out") < 0 || gpio_set_dir(pin_rst, "out") < 0)
        goto fail;
    /* Give sysfs GPIO time to settle after direction change. */
    usleep(50000);

    hw->dc_fd = gpio_open(pin_dc);
    hw->rst_fd = gpio_open(pin_rst);
    if (hw->dc_fd < 0 || hw->rst_fd < 0)
        goto fail;

    hw->spi_fd = open_spi(spi_path, speed_hz);
    if (hw->spi_fd < 0)
        goto fail;

    gc9a01_hw_reset(hw);

    /* GC9A01 initialization sequence. */
    write_cmd(hw, 0xEF, NULL, 0);
    write_cmd(hw, 0xEB, (uint8_t[]){0x14}, 1);
    write_cmd(hw, 0xFE, NULL, 0);
    write_cmd(hw, 0xEF, NULL, 0);
    write_cmd(hw, 0xEB, (uint8_t[]){0x14}, 1);
    write_cmd(hw, 0x84, (uint8_t[]){0x40}, 1);
    write_cmd(hw, 0x85, (uint8_t[]){0xFF}, 1);
    write_cmd(hw, 0x86, (uint8_t[]){0xFF}, 1);
    write_cmd(hw, 0x87, (uint8_t[]){0xFF}, 1);
    write_cmd(hw, 0x88, (uint8_t[]){0x0A}, 1);
    write_cmd(hw, 0x89, (uint8_t[]){0x21}, 1);
    write_cmd(hw, 0x8A, (uint8_t[]){0x00}, 1);
    write_cmd(hw, 0x8B, (uint8_t[]){0x80}, 1);
    write_cmd(hw, 0x8C, (uint8_t[]){0x01}, 1);
    write_cmd(hw, 0x8D, (uint8_t[]){0x01}, 1);
    write_cmd(hw, 0x8E, (uint8_t[]){0xFF}, 1);
    write_cmd(hw, 0x8F, (uint8_t[]){0xFF}, 1);
    write_cmd(hw, 0xB6, (uint8_t[]){0x00, 0x00}, 2);
    write_cmd(hw, 0x3A, (uint8_t[]){0x05}, 1);
    write_cmd(hw, 0x90, (uint8_t[]){0x08, 0x08, 0x08, 0x08}, 4);
    write_cmd(hw, 0xBD, (uint8_t[]){0x06}, 1);
    write_cmd(hw, 0xBC, (uint8_t[]){0x00}, 1);
    write_cmd(hw, 0xFF, (uint8_t[]){0x60, 0x01, 0x04}, 3);
    write_cmd(hw, 0xC3, (uint8_t[]){0x13}, 1);
    write_cmd(hw, 0xC4, (uint8_t[]){0x13}, 1);
    write_cmd(hw, 0xC9, (uint8_t[]){0x22}, 1);
    write_cmd(hw, 0xBE, (uint8_t[]){0x11}, 1);
    write_cmd(hw, 0xE1, (uint8_t[]){0x10, 0x0E}, 2);
    write_cmd(hw, 0xDF, (uint8_t[]){0x21, 0x0c, 0x02}, 3);
    write_cmd(hw, 0xF0, (uint8_t[]){0x45, 0x09, 0x08, 0x08, 0x26, 0x2A}, 6);
    write_cmd(hw, 0xF1, (uint8_t[]){0x43, 0x70, 0x72, 0x36, 0x37, 0x6F}, 6);
    write_cmd(hw, 0xF2, (uint8_t[]){0x45, 0x09, 0x08, 0x08, 0x26, 0x2A}, 6);
    write_cmd(hw, 0xF3, (uint8_t[]){0x43, 0x70, 0x72, 0x36, 0x37, 0x6F}, 6);
    write_cmd(hw, 0xED, (uint8_t[]){0x1B, 0x0B}, 2);
    write_cmd(hw, 0xAE, (uint8_t[]){0x77}, 1);
    write_cmd(hw, 0xCD, (uint8_t[]){0x63}, 1);
    write_cmd(hw, 0x70, (uint8_t[]){0x07, 0x07, 0x04, 0x0E, 0x0F, 0x09, 0x07, 0x08, 0x03}, 9);
    write_cmd(hw, 0xE8, (uint8_t[]){0x34}, 1);
    write_cmd(hw, 0x62, (uint8_t[]){0x18, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70}, 12);
    write_cmd(hw, 0x63, (uint8_t[]){0x18, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70}, 12);
    write_cmd(hw, 0x64, (uint8_t[]){0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07}, 7);
    write_cmd(hw, 0x66, (uint8_t[]){0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00}, 10);
    write_cmd(hw, 0x67, (uint8_t[]){0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98}, 10);
    write_cmd(hw, 0x74, (uint8_t[]){0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00}, 7);
    write_cmd(hw, 0x98, (uint8_t[]){0x3e, 0x07}, 2);
    write_cmd(hw, 0x35, NULL, 0);
    write_cmd(hw, 0x21, NULL, 0);
    write_cmd(hw, 0x11, NULL, 0);
    usleep(120000);
    write_cmd(hw, 0x29, NULL, 0);
    usleep(120000);

    return 0;

fail:
    gc9a01_hw_deinit(hw);
    return -1;
}

void gc9a01_hw_deinit(struct gc9a01_hw *hw)
{
    if (hw->spi_fd >= 0) {
        close(hw->spi_fd);
        hw->spi_fd = -1;
    }
    if (hw->dc_fd >= 0) {
        close(hw->dc_fd);
        hw->dc_fd = -1;
    }
    if (hw->rst_fd >= 0) {
        close(hw->rst_fd);
        hw->rst_fd = -1;
    }
}

void gc9a01_hw_reset(struct gc9a01_hw *hw)
{
    /* Conservative cold-boot reset: keep RST high long enough for power to
     * stabilise, pulse low, then wait generously before sending commands.
     */
    gpio_set(hw->dc_fd, 1);
    gpio_set(hw->rst_fd, 1);
    usleep(1000000);
    gpio_set(hw->rst_fd, 0);
    usleep(200000);
    gpio_set(hw->rst_fd, 1);
    usleep(500000);
}

int gc9a01_hw_flush(struct gc9a01_hw *hw, const uint8_t *rgb565_buf)
{
    uint8_t col[4] = {0x00, 0x00, 0x00, (GC9A01_WIDTH - 1) & 0xFF};
    uint8_t row[4] = {0x00, 0x00, 0x00, (GC9A01_HEIGHT - 1) & 0xFF};
    size_t len = GC9A01_BUF_BYTES;
    size_t offset = 0;

    write_cmd(hw, 0x2A, col, 4);
    write_cmd(hw, 0x2B, row, 4);
    write_cmd(hw, 0x2C, NULL, 0);

    gpio_set(hw->dc_fd, 1);
    while (offset < len) {
        size_t chunk = (len - offset) > hw->chunk_size ? hw->chunk_size : (len - offset);
        if (spi_xfer(hw->spi_fd, rgb565_buf + offset, chunk, hw->speed_hz) < 0)
            return -1;
        offset += chunk;
    }
    return 0;
}

int gc9a01_hw_fill(struct gc9a01_hw *hw, uint16_t color)
{
    uint8_t col[4] = {0x00, 0x00, 0x00, (GC9A01_WIDTH - 1) & 0xFF};
    uint8_t row[4] = {0x00, 0x00, 0x00, (GC9A01_HEIGHT - 1) & 0xFF};
    size_t len = GC9A01_BUF_BYTES;
    size_t offset = 0;
    uint8_t pixel[2] = {(uint8_t)(color >> 8), (uint8_t)(color & 0xFF)};

    write_cmd(hw, 0x2A, col, 4);
    write_cmd(hw, 0x2B, row, 4);
    write_cmd(hw, 0x2C, NULL, 0);

    gpio_set(hw->dc_fd, 1);
    while (offset < len) {
        size_t chunk = (len - offset) > hw->chunk_size ? hw->chunk_size : (len - offset);
        /* chunk is even because chunk_size and len are even; pixel is 2 bytes. */
        size_t i;
        for (i = 0; i < chunk; i += 2) {
            if (spi_xfer(hw->spi_fd, pixel, 2, hw->speed_hz) < 0)
                return -1;
        }
        offset += chunk;
    }
    return 0;
}
