#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <linux/spi/spidev.h>
#include <dirent.h>

#define GPIO_SYSFS_PATH "/sys/class/gpio"

#define DEFAULT_SPI_DEV_PATH  "/dev/spidev0.1"
#define DEFAULT_SPI_SPEED_HZ  20000000
#define DEFAULT_FPS           60
#define DEFAULT_CHUNK_SIZE    4096
#define PRELOAD_LIMIT_BYTES   (12U * 1024U * 1024U)

static const char *g_spi_dev_path = DEFAULT_SPI_DEV_PATH;
static int g_pin_dc = -1;    /* Pin5  GP3  PWR_GPIO[25] - auto-detected */
static int g_pin_rst = -1;   /* Pin11 GP8  PWR_GPIO[21] - auto-detected */
static int g_pin_cs = -1;    /* Pin29 GP22 PWR_GPIO[4]: now managed by SPI core cs-gpios */
static const char *g_pinmux_dc = "GP3/GP3";
static const char *g_pinmux_sck = "GP6/SPI2_SCK";
static const char *g_pinmux_mosi = "GP7/SPI2_SDO";
static const char *g_pinmux_rst = "GP8/GP8";
static const char *g_pinmux_cs = "none";
static uint32_t g_spi_speed_hz = DEFAULT_SPI_SPEED_HZ;
static size_t g_chunk_size = DEFAULT_CHUNK_SIZE;

/* Try to map a PWR_GPIO offset to a /sys/class/gpio number by looking at the
 * gpiochip labels.  On CV1800B PWR_GPIO lives on the controller at 0x05021000,
 * which appears as a gpiochip whose label contains "5021000.gpio" (or "porte").
 */
static int gpio_sysfs_number_for(const char *pattern, int offset)
{
    DIR *d = opendir(GPIO_SYSFS_PATH);
    struct dirent *e;
    int found = -1;

    if (!d) return -1;
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
        if (!f) continue;
        if (!fgets(label, sizeof(label), f)) { fclose(f); continue; }
        fclose(f);
        label[strcspn(label, "\n")] = '\0';
        if (!strstr(label, pattern))
            continue;
        f = fopen(base_path, "r");
        if (!f) continue;
        if (fscanf(f, "%d", &base) == 1 && base >= 0)
            found = base + offset;
        fclose(f);
        if (found >= 0) break;
    }
    closedir(d);
    return found;
}

static void detect_default_gpio_numbers(void)
{
    /* GC9A01 DC/RST are on PWR_GPIO_25 / PWR_GPIO_21 (Duo Pin5 / Pin11). */
    if (g_pin_dc < 0) {
        g_pin_dc = gpio_sysfs_number_for("5021000.gpio", 25);
        if (g_pin_dc < 0)
            g_pin_dc = gpio_sysfs_number_for("porte", 25);
    }
    if (g_pin_rst < 0) {
        g_pin_rst = gpio_sysfs_number_for("5021000.gpio", 21);
        if (g_pin_rst < 0)
            g_pin_rst = gpio_sysfs_number_for("porte", 21);
    }
    /* Last-resort fallback to the numbers from older kernels. */
    if (g_pin_dc < 0) g_pin_dc = 377;
    if (g_pin_rst < 0) g_pin_rst = 373;
}

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
    g_spi_dev_path = env_str("GC9A01_SPI_DEV", g_spi_dev_path);
    g_pin_dc = env_int("GC9A01_GPIO_DC", g_pin_dc);
    g_pin_rst = env_int("GC9A01_GPIO_RST", g_pin_rst);
    g_pin_cs = env_int("GC9A01_GPIO_CS", g_pin_cs);
    g_pinmux_dc = env_str("GC9A01_PINMUX_DC", g_pinmux_dc);
    g_pinmux_sck = env_str("GC9A01_PINMUX_SCK", g_pinmux_sck);
    g_pinmux_mosi = env_str("GC9A01_PINMUX_MOSI", g_pinmux_mosi);
    g_pinmux_rst = env_str("GC9A01_PINMUX_RST", g_pinmux_rst);
    g_pinmux_cs = env_str("GC9A01_PINMUX_CS", g_pinmux_cs);
    detect_default_gpio_numbers();
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
    run_pinmux(g_pinmux_dc);
    run_pinmux(g_pinmux_sck);
    run_pinmux(g_pinmux_mosi);
    run_pinmux(g_pinmux_rst);
    run_pinmux(g_pinmux_cs);
}

static size_t detect_spidev_bufsiz(void) {
    FILE *f = fopen("/sys/module/spidev/parameters/bufsiz", "r");
    unsigned long v = DEFAULT_CHUNK_SIZE;

    if (!f) return DEFAULT_CHUNK_SIZE;
    if (fscanf(f, "%lu", &v) != 1 || v < 256) v = DEFAULT_CHUNK_SIZE;
    fclose(f);
    return (size_t)v;
}

static int gpio_export(int pin) {
    char path[128];
    if (pin < 0) return 0;
    snprintf(path, sizeof(path), "%s/gpio%d", GPIO_SYSFS_PATH, pin);
    if (access(path, F_OK) == 0) return 0;
    int fd = open(GPIO_SYSFS_PATH "/export", O_WRONLY);
    if (fd < 0) { perror("export"); return -1; }
    char buf[8]; int n = snprintf(buf, sizeof(buf), "%d", pin);
    write(fd, buf, n); close(fd);
    usleep(100000); return 0;
}

static int gpio_set_dir(int pin, const char *dir) {
    char path[128];
    if (pin < 0) return 0;
    snprintf(path, sizeof(path), "%s/gpio%d/direction", GPIO_SYSFS_PATH, pin);
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    write(fd, dir, strlen(dir)); close(fd); return 0;
}

static int gpio_open(int pin) {
    char path[128];
    if (pin < 0) return -1;
    snprintf(path, sizeof(path), "%s/gpio%d/value", GPIO_SYSFS_PATH, pin);
    return open(path, O_WRONLY);
}

static inline void gset(int fd, int v) {
    if (fd < 0) return;
    lseek(fd, 0, SEEK_SET);
    write(fd, v ? "1" : "0", 1);
}

static int spi_xfer(int fd, const uint8_t *tx, size_t len) {
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = 0,
        .len = len,
        .speed_hz = g_spi_speed_hz,
        .delay_usecs = 0,
        .bits_per_word = 8,
    };
    if (ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 1) {
        perror("SPI_IOC_MESSAGE");
        return -1;
    }
    return 0;
}

static void cmd(int spi, int dc, int cs, uint8_t c, const uint8_t *d, int len) {
    gset(cs, 0);
    gset(dc, 0);
    spi_xfer(spi, &c, 1);
    if (d && len > 0) {
        gset(dc, 1);
        spi_xfer(spi, d, len);
    }
    gset(cs, 1);
}

static void gc9a01_init(int spi, int dc, int rst, int cs) {
    gset(dc, 1); gset(cs, 1); gset(rst, 1);
    usleep(200000);
    gset(rst, 0); usleep(200000);
    gset(rst, 1); usleep(200000);

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
    cmd(spi, dc, cs, 0x11, NULL, 0); usleep(120000);
    cmd(spi, dc, cs, 0x29, NULL, 0); usleep(120000);
}

static int send_frame(int spi, int dc, int cs, const uint8_t *frame_data,
                      uint16_t w, uint16_t h) {
    // Center the eye on the 240x240 display
    uint16_t x0 = (240 - w) / 2;
    uint16_t y0 = (240 - h) / 2;
    uint16_t x1 = x0 + w - 1;
    uint16_t y1 = y0 + h - 1;

    uint8_t col[4] = { (uint8_t)(x0 >> 8), (uint8_t)(x0 & 0xFF),
                       (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFF) };
    uint8_t row[4] = { (uint8_t)(y0 >> 8), (uint8_t)(y0 & 0xFF),
                       (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFF) };
    cmd(spi, dc, cs, 0x2A, col, 4);
    cmd(spi, dc, cs, 0x2B, row, 4);
    cmd(spi, dc, cs, 0x2C, NULL, 0);

    gset(cs, 0);
    gset(dc, 1);

    size_t len = (size_t)w * h * 2;
    size_t offset = 0;
    while (offset < len) {
        size_t chunk = (len - offset) > g_chunk_size ? g_chunk_size : (len - offset);
        if (spi_xfer(spi, frame_data + offset, chunk) < 0) {
            gset(cs, 1);
            return -1;
        }
        offset += chunk;
    }

    gset(cs, 1);
    return 0;
}

static int clear_screen_black(int spi, int dc, int cs) {
    static uint8_t zeros[DEFAULT_CHUNK_SIZE];
    uint8_t col[4] = {0x00, 0x00, 0x00, 239};
    uint8_t row[4] = {0x00, 0x00, 0x00, 239};
    size_t len = 240U * 240U * 2U;
    size_t offset = 0;

    cmd(spi, dc, cs, 0x2A, col, 4);
    cmd(spi, dc, cs, 0x2B, row, 4);
    cmd(spi, dc, cs, 0x2C, NULL, 0);

    gset(cs, 0);
    gset(dc, 1);
    while (offset < len) {
        size_t chunk = (len - offset) > sizeof(zeros) ? sizeof(zeros) : (len - offset);
        if (spi_xfer(spi, zeros, chunk) < 0) {
            gset(cs, 1);
            return -1;
        }
        offset += chunk;
    }
    gset(cs, 1);
    return 0;
}

static ssize_t read_all(int fd, void *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = read(fd, (uint8_t *)buf + total, len - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break;
        total += n;
    }
    return total;
}

static long long now_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000LL + tv.tv_usec;
}

static void wait_until_us(long long deadline) {
    while (now_us() < deadline) {
        ;
    }
}

static uint32_t parse_u32(const char *s) {
    char *end;
    unsigned long v = strtoul(s, &end, 10);
    return (uint32_t)v;
}

int main(int argc, char **argv) {
    const char *bin_path = (argc > 1) ? argv[1] : "/root/eye_animation.bin";
    uint32_t target_fps = (argc > 2) ? parse_u32(argv[2]) : DEFAULT_FPS;
    uint32_t spi_speed  = (argc > 3) ? parse_u32(argv[3]) : DEFAULT_SPI_SPEED_HZ;
    const char *load_mode = (argc > 4) ? argv[4] : "auto";
    if (spi_speed == 0)  spi_speed  = DEFAULT_SPI_SPEED_HZ;

    uint32_t frame_delay_us = target_fps ? (1000000U / target_fps) : 0;

    load_config_from_env();

    setbuf(stdout, NULL);
    printf("OpenEmo Animated Eye for MilkV Duo\n");
    if (target_fps) {
        printf("Target FPS: %u, SPI speed: %u Hz, frame delay: %u us\n",
               target_fps, spi_speed, frame_delay_us);
    } else {
        printf("Target FPS: uncapped, SPI speed: %u Hz\n", spi_speed);
    }
    printf("Usage: %s [bin_path] [target_fps, 0=uncapped] [spi_speed_hz] [auto|preload|stream]\n",
           argv[0]);
    printf("SPI device: %s, GPIO DC=%d RST=%d CS=%d (%s)\n",
           g_spi_dev_path, g_pin_dc, g_pin_rst, g_pin_cs,
           g_pin_cs < 0 ? "SPI core CS" : "manual CS");

    configure_pinmux();
    gpio_export(g_pin_dc); gpio_export(g_pin_rst); gpio_export(g_pin_cs);
    gpio_set_dir(g_pin_dc, "out");
    gpio_set_dir(g_pin_rst, "out");
    gpio_set_dir(g_pin_cs, "out");

    int dc = gpio_open(g_pin_dc);
    int rst = gpio_open(g_pin_rst);
    int cs = gpio_open(g_pin_cs);
    if (dc < 0 || rst < 0 || (g_pin_cs >= 0 && cs < 0)) {
        fprintf(stderr, "GPIO open failed\n"); return 1;
    }

    int spi = open(g_spi_dev_path, O_RDWR);
    if (spi < 0) { perror("open spidev"); return 1; }

    uint32_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    if (ioctl(spi, SPI_IOC_WR_MODE, &mode) < 0) perror("SPI mode");
    if (ioctl(spi, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) perror("SPI bits");
    if (ioctl(spi, SPI_IOC_WR_MAX_SPEED_HZ, &spi_speed) < 0) perror("SPI speed");
    g_spi_speed_hz = spi_speed;
    g_chunk_size = detect_spidev_bufsiz();
    if (g_chunk_size > 65536) g_chunk_size = 65536;
    printf("SPI transfer chunk: %zu bytes\n", g_chunk_size);

    printf("Initializing GC9A01...\n");
    gc9a01_init(spi, dc, rst, cs);

    int fd = open(bin_path, O_RDONLY);
    if (fd < 0) { perror("open animation bin"); return 1; }

    uint32_t frame_count, frame_size;
    uint16_t width, height;
    if (read_all(fd, &frame_count, 4) != 4 ||
        read_all(fd, &frame_size, 4) != 4 ||
        read_all(fd, &width, 2) != 2 ||
        read_all(fd, &height, 2) != 2) {
        fprintf(stderr, "Failed to read bin header\n"); return 1;
    }

    printf("Animation: %u frames, %ux%u, frame size=%u\n", frame_count, width, height, frame_size);

    if (width > 240 || height > 240 || frame_size != (uint32_t)width * height * 2) {
        fprintf(stderr, "Invalid frame size\n"); return 1;
    }

    if ((width < 240 || height < 240) && clear_screen_black(spi, dc, cs) < 0) {
        return 1;
    }

    size_t total_bytes = (size_t)frame_count * frame_size;
    int force_preload = strcmp(load_mode, "preload") == 0;
    int force_stream = strcmp(load_mode, "stream") == 0;
    int use_preload = force_preload || (!force_stream && total_bytes <= PRELOAD_LIMIT_BYTES);
    if (!force_preload && !force_stream && strcmp(load_mode, "auto") != 0) {
        fprintf(stderr, "unknown load mode: %s\n", load_mode);
        return 1;
    }

    uint8_t *frames = NULL;
    uint8_t *stream_frame = NULL;
    if (use_preload) {
        printf("Loading %u frames into RAM (%.1f MiB)...\n",
               frame_count, total_bytes / 1048576.0);
        frames = malloc(total_bytes);
        if (!frames) { fprintf(stderr, "malloc failed for frames\n"); return 1; }

        long long t0 = now_us();
        for (uint32_t i = 0; i < frame_count; i++) {
            uint8_t *p = frames + (size_t)i * frame_size;
            ssize_t n = read_all(fd, p, frame_size);
            if (n != (ssize_t)frame_size) {
                fprintf(stderr, "Frame %u: short read %zd/%u\n", i, n, frame_size);
                free(frames); return 1;
            }
        }
        long long t1 = now_us();
        printf("Loaded in %.2f ms\n", (t1 - t0) / 1000.0);
        close(fd);
        fd = -1;
    } else {
        printf("Streaming frames from file (%.1f MiB animation, low RAM mode)\n",
               total_bytes / 1048576.0);
        stream_frame = malloc(frame_size);
        if (!stream_frame) { fprintf(stderr, "malloc failed for stream frame\n"); return 1; }
    }

    printf("Playing. Press Ctrl+C to stop.\n");

    uint32_t frame = 0;
    long long last_report = now_us();
    long long next_frame_at = last_report;
    uint32_t frames_since_report = 0;

    while (1) {
        const uint8_t *frame_data;

        if (use_preload) {
            frame_data = frames + (size_t)frame * frame_size;
        } else {
            if (frame == 0 && lseek(fd, 12, SEEK_SET) < 0) {
                perror("rewind animation");
                break;
            }
            ssize_t n = read_all(fd, stream_frame, frame_size);
            if (n != (ssize_t)frame_size) {
                fprintf(stderr, "Frame %u: short read %zd/%u\n", frame, n, frame_size);
                break;
            }
            frame_data = stream_frame;
        }

        if (send_frame(spi, dc, cs, frame_data, width, height) < 0) {
            break;
        }
        frame = (frame + 1) % frame_count;
        frames_since_report++;

        if (frame_delay_us) {
            next_frame_at += frame_delay_us;
            if (now_us() > next_frame_at + (long long)frame_delay_us) {
                next_frame_at = now_us();
            }
            wait_until_us(next_frame_at);
        }

        long long report_now = now_us();
        if (report_now - last_report >= 2000000) { // every 2s
            float actual_fps = frames_since_report * 1000000.0f / (report_now - last_report);
            printf("Actual FPS: %.1f\n", actual_fps);
            frames_since_report = 0;
            last_report = report_now;
        }
    }

    free(frames);
    free(stream_frame);
    if (fd >= 0) close(fd);
    close(spi);
    close(dc); close(rst); close(cs);
    return 0;
}
