#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

#define GPIO_SYSFS_PATH "/sys/class/gpio"
#define DEFAULT_SPI_DEVICE_PATH "/dev/spidev0.0"
#define SPI_SPEED_HZ 8000000

// Default wiring: Pin5=DC, Pin11=RST, Pin12=manual CS.
static const char *g_spi_device_path = DEFAULT_SPI_DEVICE_PATH;
static int g_pin_dc = 377;   // Pin5 = GP3 = PWR_GPIO[25]
static int g_pin_rst = 373;  // Pin11 = GP8 = PWR_GPIO[21]
static int g_pin_cs = 370;   // Pin12 = GP9 = PWR_GPIO[18], -1 disables manual CS

static const char *g_pinmux_dc = "GP3/GP3";
static const char *g_pinmux_sck = "GP6/SPI2_SCK";
static const char *g_pinmux_mosi = "GP7/SPI2_SDO";
static const char *g_pinmux_rst = "GP8/GP8";
static const char *g_pinmux_cs = "GP9/GP9";

static int env_int(const char *name, int current) {
    const char *value = getenv(name);
    char *end = NULL;
    long parsed;

    if (!value || !*value) return current;
    parsed = strtol(value, &end, 10);
    if (!end || *end != '\0') {
        fprintf(stderr, "warning: ignoring invalid %s=%s\n", name, value);
        return current;
    }
    return (int)parsed;
}

static const char *env_str(const char *name, const char *current) {
    const char *value = getenv(name);
    return (value && *value) ? value : current;
}

static void load_config_from_env(void) {
    g_spi_device_path = env_str("GC9A01_SPI_DEV", g_spi_device_path);
    g_pin_dc = env_int("GC9A01_GPIO_DC", g_pin_dc);
    g_pin_rst = env_int("GC9A01_GPIO_RST", g_pin_rst);
    g_pin_cs = env_int("GC9A01_GPIO_CS", g_pin_cs);

    g_pinmux_dc = env_str("GC9A01_PINMUX_DC", g_pinmux_dc);
    g_pinmux_sck = env_str("GC9A01_PINMUX_SCK", g_pinmux_sck);
    g_pinmux_mosi = env_str("GC9A01_PINMUX_MOSI", g_pinmux_mosi);
    g_pinmux_rst = env_str("GC9A01_PINMUX_RST", g_pinmux_rst);
    g_pinmux_cs = env_str("GC9A01_PINMUX_CS", g_pinmux_cs);
}

static void run_pinmux(const char *mux) {
    pid_t pid;
    int status;

    if (!mux || !*mux || strcmp(mux, "none") == 0 || strcmp(mux, "-") == 0) return;

    pid = fork();
    if (pid < 0) {
        perror("fork duo-pinmux");
        return;
    }
    if (pid == 0) {
        execlp("duo-pinmux", "duo-pinmux", "-w", mux, (char *)NULL);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "warning: pinmux command failed: duo-pinmux -w %s\n", mux);
    }
}

static void configure_pinmux(void) {
    /*
     * Official Duo images boot with GP3 as UART4_RX and GP8 as SPI2_SDI.
     * This display uses GP3 for DC and GP8 for RST, so switch them to GPIO.
     * SCK/MOSI stay on SPI2. CS is controlled manually as GPIO for stable
     * display command/data framing.
     */
    run_pinmux(g_pinmux_dc);
    run_pinmux(g_pinmux_sck);
    run_pinmux(g_pinmux_mosi);
    run_pinmux(g_pinmux_rst);
    run_pinmux(g_pinmux_cs);
}

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

static int gpio_set_direction(int pin, const char *direction) {
    char path[128];
    snprintf(path, sizeof(path), "%s/gpio%d/direction", GPIO_SYSFS_PATH, pin);
    int fd = open(path, O_WRONLY);
    if (fd < 0) { perror("open direction"); return -1; }
    if (write(fd, direction, strlen(direction)) < 0) {
        perror("write direction");
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

static int gpio_open_value(int pin) {
    char path[128];
    snprintf(path, sizeof(path), "%s/gpio%d/value", GPIO_SYSFS_PATH, pin);
    int fd = open(path, O_WRONLY);
    if (fd < 0) perror("open value");
    return fd;
}

static int gpio_prepare(int pin) {
    if (pin < 0) return -1;
    if (gpio_export(pin) < 0) return -1;
    if (gpio_set_direction(pin, "out") < 0) return -1;
    return gpio_open_value(pin);
}

static int gset(int fd, int v) {
    if (fd < 0) return 0;
    if (lseek(fd, 0, SEEK_SET) < 0) {
        perror("gpio lseek");
        return -1;
    }
    if (write(fd, v ? "1" : "0", 1) != 1) {
        perror("gpio write");
        return -1;
    }
    return 0;
}

static int spi_xfer(int fd, const uint8_t *tx, uint8_t *rx, size_t len) {
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = (unsigned long)rx,
        .len = len,
        .speed_hz = SPI_SPEED_HZ,
        .delay_usecs = 0,
        .bits_per_word = 8,
    };
    if (ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 1) {
        perror("SPI_IOC_MESSAGE");
        return -1;
    }
    return 0;
}

static int cmd(int spi, int dc, int cs, uint8_t c, const uint8_t *d, int len) {
    if (gset(cs, 0) < 0) return -1;
    if (gset(dc, 0) < 0) return -1; // Command
    if (spi_xfer(spi, &c, NULL, 1) < 0) return -1;
    if (d && len > 0) {
        if (gset(dc, 1) < 0) return -1; // Data
        if (spi_xfer(spi, d, NULL, len) < 0) return -1;
    }
    if (gset(cs, 1) < 0) return -1;
    return 0;
}

static int fill_screen(int spi, int dc, int cs, uint16_t color) {
    uint8_t col[4] = {0x00, 0, 0x00, 239};
    uint8_t row[4] = {0x00, 0, 0x00, 239};
    if (cmd(spi, dc, cs, 0x2A, col, 4) < 0) return -1;
    if (cmd(spi, dc, cs, 0x2B, row, 4) < 0) return -1;

    uint8_t hi = (color >> 8) & 0xFF;
    uint8_t lo = color & 0xFF;

    if (cmd(spi, dc, cs, 0x2C, NULL, 0) < 0) return -1; // RAMWR

    if (gset(cs, 0) < 0) return -1;
    if (gset(dc, 1) < 0) return -1; // Data
    uint8_t buf[512];
    for (size_t i = 0; i < sizeof(buf); i += 2) {
        buf[i] = hi; buf[i+1] = lo;
    }
    size_t total = 240 * 240 * 2;
    size_t sent = 0;
    while (sent < total) {
        size_t chunk = (total - sent) > sizeof(buf) ? sizeof(buf) : (total - sent);
        if (spi_xfer(spi, buf, NULL, chunk) < 0) return -1;
        sent += chunk;
    }
    if (gset(cs, 1) < 0) return -1;
    return 0;
}

int main() {
    int spi, dc, rst, cs;
    load_config_from_env();

    printf("GC9A01 HW SPI test (%s)\n", g_spi_device_path);
    printf("GPIO DC=%d RST=%d CS=%d (%s)\n",
           g_pin_dc, g_pin_rst, g_pin_cs,
           g_pin_cs < 0 ? "SPI core CS" : "manual CS");

    configure_pinmux();

    dc = gpio_prepare(g_pin_dc);
    rst = gpio_prepare(g_pin_rst);
    cs = gpio_prepare(g_pin_cs);
    if (dc < 0 || rst < 0 || (g_pin_cs >= 0 && cs < 0)) {
        fprintf(stderr, "GPIO open failed\n"); return 1;
    }
    gset(cs, 1);
    gset(dc, 1);
    gset(rst, 1);

    spi = open(g_spi_device_path, O_RDWR);
    if (spi < 0) { perror("open spidev"); return 1; }

    uint8_t mode = g_pin_cs < 0 ? SPI_MODE_0 : (SPI_MODE_0 | SPI_NO_CS);
    uint8_t bits = 8;
    uint32_t speed = SPI_SPEED_HZ;
    if (g_pin_cs >= 0 && ioctl(spi, SPI_IOC_WR_MODE, &mode) < 0) {
        perror("SPI_IOC_WR_MODE with SPI_NO_CS");
        fprintf(stderr, "warning: falling back to SPI_MODE_0 with manual GPIO CS\n");
        mode = SPI_MODE_0;
        if (ioctl(spi, SPI_IOC_WR_MODE, &mode) < 0) { perror("SPI_IOC_WR_MODE"); return 1; }
    } else if (g_pin_cs < 0 && ioctl(spi, SPI_IOC_WR_MODE, &mode) < 0) {
        perror("SPI_IOC_WR_MODE");
        return 1;
    }
    if (ioctl(spi, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) { perror("SPI_IOC_WR_BITS_PER_WORD"); return 1; }
    if (ioctl(spi, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) { perror("SPI_IOC_WR_MAX_SPEED_HZ"); return 1; }

    printf("Resetting...\n");
    gset(rst, 1); usleep(200000);
    gset(rst, 0); usleep(200000);
    gset(rst, 1); usleep(200000);

    printf("Init sequence...\n");
    cmd(spi, dc, cs, 0xEF, NULL, 0);
    cmd(spi, dc, cs, 0xEB, (uint8_t[]){0x14}, 1);
    cmd(spi, dc, cs, 0xFE, NULL, 0);
    cmd(spi, dc, cs, 0xEF, NULL, 0);
    cmd(spi, dc, cs, 0xEB, (uint8_t[]){0x14}, 1);
    cmd(spi, dc, cs, 0x84, (uint8_t[]){0x40}, 1);
    cmd(spi, dc, cs, 0x85, (uint8_t[]){0xFF}, 1);
    cmd(spi, dc, cs, 0x86, (uint8_t[]){0xFF}, 1);
    cmd(spi, dc, cs, 0x87, (uint8_t[]){0xFF}, 1);
    cmd(spi, dc, cs, 0x88, (uint8_t[]){0x0A}, 1);
    cmd(spi, dc, cs, 0x89, (uint8_t[]){0x21}, 1);
    cmd(spi, dc, cs, 0x8A, (uint8_t[]){0x00}, 1);
    cmd(spi, dc, cs, 0x8B, (uint8_t[]){0x80}, 1);
    cmd(spi, dc, cs, 0x8C, (uint8_t[]){0x01}, 1);
    cmd(spi, dc, cs, 0x8D, (uint8_t[]){0x01}, 1);
    cmd(spi, dc, cs, 0x8E, (uint8_t[]){0xFF}, 1);
    cmd(spi, dc, cs, 0x8F, (uint8_t[]){0xFF}, 1);
    cmd(spi, dc, cs, 0xB6, (uint8_t[]){0x00, 0x00}, 2);
    cmd(spi, dc, cs, 0x3A, (uint8_t[]){0x05}, 1);
    cmd(spi, dc, cs, 0x90, (uint8_t[]){0x08, 0x08, 0x08, 0x08}, 4);
    cmd(spi, dc, cs, 0xBD, (uint8_t[]){0x06}, 1);
    cmd(spi, dc, cs, 0xBC, (uint8_t[]){0x00}, 1);
    cmd(spi, dc, cs, 0xFF, (uint8_t[]){0x60, 0x01, 0x04}, 3);
    cmd(spi, dc, cs, 0xC3, (uint8_t[]){0x13}, 1);
    cmd(spi, dc, cs, 0xC4, (uint8_t[]){0x13}, 1);
    cmd(spi, dc, cs, 0xC9, (uint8_t[]){0x22}, 1);
    cmd(spi, dc, cs, 0xBE, (uint8_t[]){0x11}, 1);
    cmd(spi, dc, cs, 0xE1, (uint8_t[]){0x10, 0x0E}, 2);
    cmd(spi, dc, cs, 0xDF, (uint8_t[]){0x21, 0x0c, 0x02}, 3);
    cmd(spi, dc, cs, 0xF0, (uint8_t[]){0x45, 0x09, 0x08, 0x08, 0x26, 0x2A}, 6);
    cmd(spi, dc, cs, 0xF1, (uint8_t[]){0x43, 0x70, 0x72, 0x36, 0x37, 0x6F}, 6);
    cmd(spi, dc, cs, 0xF2, (uint8_t[]){0x45, 0x09, 0x08, 0x08, 0x26, 0x2A}, 6);
    cmd(spi, dc, cs, 0xF3, (uint8_t[]){0x43, 0x70, 0x72, 0x36, 0x37, 0x6F}, 6);
    cmd(spi, dc, cs, 0xED, (uint8_t[]){0x1B, 0x0B}, 2);
    cmd(spi, dc, cs, 0xAE, (uint8_t[]){0x77}, 1);
    cmd(spi, dc, cs, 0xCD, (uint8_t[]){0x63}, 1);
    cmd(spi, dc, cs, 0x70, (uint8_t[]){0x07, 0x07, 0x04, 0x0E, 0x0F, 0x09, 0x07, 0x08, 0x03}, 9);
    cmd(spi, dc, cs, 0xE8, (uint8_t[]){0x34}, 1);
    cmd(spi, dc, cs, 0x62, (uint8_t[]){0x18, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70}, 12);
    cmd(spi, dc, cs, 0x63, (uint8_t[]){0x18, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70}, 12);
    cmd(spi, dc, cs, 0x64, (uint8_t[]){0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07}, 7);
    cmd(spi, dc, cs, 0x66, (uint8_t[]){0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00}, 10);
    cmd(spi, dc, cs, 0x67, (uint8_t[]){0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98}, 10);
    cmd(spi, dc, cs, 0x74, (uint8_t[]){0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00}, 7);
    cmd(spi, dc, cs, 0x98, (uint8_t[]){0x3e, 0x07}, 2);
    cmd(spi, dc, cs, 0x35, NULL, 0);
    cmd(spi, dc, cs, 0x21, NULL, 0);
    cmd(spi, dc, cs, 0x11, NULL, 0); // SLPOUT
    usleep(120000);
    cmd(spi, dc, cs, 0x29, NULL, 0); // DISPON
    usleep(120000);

    printf("Cycling red/green/blue/white...\n");
    fill_screen(spi, dc, cs, 0xF800); // red
    sleep(1);
    fill_screen(spi, dc, cs, 0x07E0); // green
    sleep(1);
    fill_screen(spi, dc, cs, 0x001F); // blue
    sleep(1);
    fill_screen(spi, dc, cs, 0xFFFF); // white
    printf("Done!\n");

    close(spi); close(dc); close(rst); close(cs);
    return 0;
}
