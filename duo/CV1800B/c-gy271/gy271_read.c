#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#ifndef I2C_SLAVE
#define I2C_SLAVE 0x0703
#endif

#define DEFAULT_I2C_DEV "/dev/i2c-1"
#define QMC5883L_ADDR 0x0d
#define HMC5883L_ADDR 0x1e
#define PI 3.14159265358979323846

typedef enum {
    CHIP_AUTO = 0,
    CHIP_QMC5883L,
    CHIP_HMC5883L,
} chip_type_t;

typedef struct {
    const char *device;
    int addr;
    chip_type_t chip;
    int interval_ms;
    int once;
    int do_pinmux;
} options_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
    double x_ut;
    double y_ut;
    double z_ut;
    double heading_deg;
} mag_sample_t;

static volatile sig_atomic_t keep_running = 1;

static void on_signal(int sig) {
    (void)sig;
    keep_running = 0;
}

static void usage(const char *argv0) {
    printf(
        "Usage: %s [options]\n"
        "\n"
        "Read GY-271 magnetometer data on Milk-V Duo CV1800B.\n"
        "\n"
        "Options:\n"
        "  --device PATH       I2C device, default " DEFAULT_I2C_DEV "\n"
        "  --addr auto|0x0d|0x1e\n"
        "                      I2C address, default auto\n"
        "  --chip auto|qmc|hmc Sensor core, default auto\n"
        "  --interval-ms N     Print interval in ms, default 200\n"
        "  --once              Read one sample and exit\n"
        "  --no-pinmux         Do not run duo-pinmux for GP4/GP5\n"
        "  -h, --help          Show this help\n"
        "\n"
        "Default wiring uses Duo I2C1:\n"
        "  GY-271 SCL -> Duo Pin 6  GP4 / I2C1_SCL\n"
        "  GY-271 SDA -> Duo Pin 7  GP5 / I2C1_SDA\n",
        argv0);
}

static int parse_int(const char *s, int *out) {
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(s, &end, 0);
    if (errno != 0 || end == s || *end != '\0' || value < 0 || value > 0x7f) {
        return -1;
    }

    *out = (int)value;
    return 0;
}

static int parse_args(int argc, char **argv, options_t *opts) {
    opts->device = DEFAULT_I2C_DEV;
    opts->addr = -1;
    opts->chip = CHIP_AUTO;
    opts->interval_ms = 200;
    opts->once = 0;
    opts->do_pinmux = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "--device") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "--device requires a value\n");
                return -1;
            }
            opts->device = argv[i];
        } else if (strcmp(argv[i], "--addr") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "--addr requires a value\n");
                return -1;
            }
            if (strcmp(argv[i], "auto") == 0) {
                opts->addr = -1;
            } else if (parse_int(argv[i], &opts->addr) != 0) {
                fprintf(stderr, "Invalid --addr value: %s\n", argv[i]);
                return -1;
            }
        } else if (strcmp(argv[i], "--chip") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "--chip requires a value\n");
                return -1;
            }
            if (strcmp(argv[i], "auto") == 0) {
                opts->chip = CHIP_AUTO;
            } else if (strcmp(argv[i], "qmc") == 0 || strcmp(argv[i], "qmc5883l") == 0) {
                opts->chip = CHIP_QMC5883L;
            } else if (strcmp(argv[i], "hmc") == 0 || strcmp(argv[i], "hmc5883l") == 0) {
                opts->chip = CHIP_HMC5883L;
            } else {
                fprintf(stderr, "Invalid --chip value: %s\n", argv[i]);
                return -1;
            }
        } else if (strcmp(argv[i], "--interval-ms") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "--interval-ms requires a value\n");
                return -1;
            }
            opts->interval_ms = atoi(argv[i]);
            if (opts->interval_ms < 20 || opts->interval_ms > 60000) {
                fprintf(stderr, "--interval-ms must be between 20 and 60000\n");
                return -1;
            }
        } else if (strcmp(argv[i], "--once") == 0) {
            opts->once = 1;
        } else if (strcmp(argv[i], "--no-pinmux") == 0) {
            opts->do_pinmux = 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return -1;
        }
    }

    if (opts->chip == CHIP_QMC5883L && opts->addr < 0) {
        opts->addr = QMC5883L_ADDR;
    } else if (opts->chip == CHIP_HMC5883L && opts->addr < 0) {
        opts->addr = HMC5883L_ADDR;
    }

    return 0;
}

static void run_pinmux_command(const char *cmd) {
    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "warning: pinmux command failed: %s\n", cmd);
    }
}

static void configure_pinmux(void) {
    run_pinmux_command("duo-pinmux -w GP4/IIC1_SCL >/dev/null 2>&1");
    run_pinmux_command("duo-pinmux -w GP5/IIC1_SDA >/dev/null 2>&1");
}

static int i2c_select_addr(int fd, int addr) {
    if (ioctl(fd, I2C_SLAVE, addr) < 0) {
        fprintf(stderr, "I2C_SLAVE 0x%02x failed: %s\n", addr, strerror(errno));
        return -1;
    }
    return 0;
}

static int i2c_write_reg(int fd, uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    ssize_t n = write(fd, buf, sizeof(buf));
    if (n != (ssize_t)sizeof(buf)) {
        return -1;
    }
    return 0;
}

static int i2c_read_reg(int fd, uint8_t reg, uint8_t *buf, size_t len) {
    ssize_t n = write(fd, &reg, 1);
    if (n != 1) {
        return -1;
    }

    n = read(fd, buf, len);
    if (n != (ssize_t)len) {
        return -1;
    }
    return 0;
}

static int16_t i16_le(const uint8_t *p) {
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int16_t i16_be(const uint8_t *p) {
    return (int16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static double heading_degrees(double x, double y) {
    double heading = atan2(y, x) * 180.0 / PI;
    if (heading < 0.0) {
        heading += 360.0;
    }
    return heading;
}

static int qmc5883l_init(int fd) {
    if (i2c_write_reg(fd, 0x0a, 0x80) != 0) {
        return -1;
    }
    usleep(10000);

    if (i2c_write_reg(fd, 0x0b, 0x01) != 0) {
        return -1;
    }

    /*
     * Control register 0x09:
     * OSR=512, RNG=8G, ODR=200Hz, MODE=continuous.
     */
    if (i2c_write_reg(fd, 0x09, 0x1d) != 0) {
        return -1;
    }

    usleep(10000);
    return 0;
}

static int qmc5883l_read(int fd, mag_sample_t *sample) {
    uint8_t buf[6];

    if (i2c_read_reg(fd, 0x00, buf, sizeof(buf)) != 0) {
        return -1;
    }

    sample->x = i16_le(&buf[0]);
    sample->y = i16_le(&buf[2]);
    sample->z = i16_le(&buf[4]);

    sample->x_ut = (double)sample->x * 100.0 / 3000.0;
    sample->y_ut = (double)sample->y * 100.0 / 3000.0;
    sample->z_ut = (double)sample->z * 100.0 / 3000.0;
    sample->heading_deg = heading_degrees(sample->x_ut, sample->y_ut);
    return 0;
}

static int hmc5883l_init(int fd) {
    if (i2c_write_reg(fd, 0x00, 0x70) != 0) {
        return -1;
    }
    if (i2c_write_reg(fd, 0x01, 0x20) != 0) {
        return -1;
    }
    if (i2c_write_reg(fd, 0x02, 0x00) != 0) {
        return -1;
    }

    usleep(10000);
    return 0;
}

static int hmc5883l_read(int fd, mag_sample_t *sample) {
    uint8_t status = 0;
    uint8_t buf[6];

    for (int i = 0; i < 20; i++) {
        if (i2c_read_reg(fd, 0x09, &status, 1) == 0 && (status & 0x01) != 0) {
            break;
        }
        usleep(5000);
    }

    if (i2c_read_reg(fd, 0x03, buf, sizeof(buf)) != 0) {
        return -1;
    }

    sample->x = i16_be(&buf[0]);
    sample->z = i16_be(&buf[2]);
    sample->y = i16_be(&buf[4]);

    sample->x_ut = (double)sample->x * 100.0 / 1090.0;
    sample->y_ut = (double)sample->y * 100.0 / 1090.0;
    sample->z_ut = (double)sample->z * 100.0 / 1090.0;
    sample->heading_deg = heading_degrees(sample->x_ut, sample->y_ut);
    return 0;
}

static const char *chip_name(chip_type_t chip) {
    switch (chip) {
    case CHIP_QMC5883L:
        return "QMC5883L";
    case CHIP_HMC5883L:
        return "HMC5883L";
    case CHIP_AUTO:
    default:
        return "auto";
    }
}

static int init_chip(int fd, chip_type_t chip) {
    switch (chip) {
    case CHIP_QMC5883L:
        return qmc5883l_init(fd);
    case CHIP_HMC5883L:
        return hmc5883l_init(fd);
    case CHIP_AUTO:
    default:
        return -1;
    }
}

static int read_chip(int fd, chip_type_t chip, mag_sample_t *sample) {
    switch (chip) {
    case CHIP_QMC5883L:
        return qmc5883l_read(fd, sample);
    case CHIP_HMC5883L:
        return hmc5883l_read(fd, sample);
    case CHIP_AUTO:
    default:
        return -1;
    }
}

static int probe_one(int fd, chip_type_t chip, int addr) {
    mag_sample_t sample;

    if (i2c_select_addr(fd, addr) != 0) {
        return -1;
    }
    if (init_chip(fd, chip) != 0) {
        return -1;
    }
    if (read_chip(fd, chip, &sample) != 0) {
        return -1;
    }
    return 0;
}

static int detect_chip(int fd, const options_t *opts, chip_type_t *chip, int *addr) {
    if (opts->chip != CHIP_AUTO) {
        int chosen_addr = opts->addr >= 0 ? opts->addr :
                          (opts->chip == CHIP_QMC5883L ? QMC5883L_ADDR : HMC5883L_ADDR);
        if (probe_one(fd, opts->chip, chosen_addr) == 0) {
            *chip = opts->chip;
            *addr = chosen_addr;
            return 0;
        }
        fprintf(stderr, "Failed to initialize %s at 0x%02x\n", chip_name(opts->chip), chosen_addr);
        return -1;
    }

    if (opts->addr >= 0) {
        chip_type_t inferred = opts->addr == HMC5883L_ADDR ? CHIP_HMC5883L : CHIP_QMC5883L;
        if (probe_one(fd, inferred, opts->addr) == 0) {
            *chip = inferred;
            *addr = opts->addr;
            return 0;
        }
        fprintf(stderr, "Failed to initialize inferred chip at 0x%02x\n", opts->addr);
        return -1;
    }

    if (probe_one(fd, CHIP_QMC5883L, QMC5883L_ADDR) == 0) {
        *chip = CHIP_QMC5883L;
        *addr = QMC5883L_ADDR;
        return 0;
    }

    if (probe_one(fd, CHIP_HMC5883L, HMC5883L_ADDR) == 0) {
        *chip = CHIP_HMC5883L;
        *addr = HMC5883L_ADDR;
        return 0;
    }

    fprintf(stderr, "No GY-271 sensor found at 0x0d or 0x1e on this bus\n");
    return -1;
}

static double elapsed_seconds(const struct timespec *start) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec - start->tv_sec) +
           (double)(now.tv_nsec - start->tv_nsec) / 1000000000.0;
}

static void sleep_ms(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

int main(int argc, char **argv) {
    options_t opts;
    chip_type_t chip = CHIP_AUTO;
    int addr = -1;
    int fd;
    struct timespec start;

    if (parse_args(argc, argv, &opts) != 0) {
        usage(argv[0]);
        return 2;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    if (opts.do_pinmux) {
        configure_pinmux();
    }

    fd = open(opts.device, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Failed to open %s: %s\n", opts.device, strerror(errno));
        fprintf(stderr, "Check with: ls /dev/i2c-* ; try --device /dev/i2c-0 or /dev/i2c-1\n");
        return 1;
    }

    if (detect_chip(fd, &opts, &chip, &addr) != 0) {
        close(fd);
        return 1;
    }

    if (i2c_select_addr(fd, addr) != 0 || init_chip(fd, chip) != 0) {
        fprintf(stderr, "Failed to select/init %s at 0x%02x\n", chip_name(chip), addr);
        close(fd);
        return 1;
    }

    clock_gettime(CLOCK_MONOTONIC, &start);

    printf("GY-271 reader: device=%s chip=%s addr=0x%02x interval=%dms\n",
           opts.device, chip_name(chip), addr, opts.interval_ms);
    printf("columns: time_s raw_x raw_y raw_z ut_x ut_y ut_z heading_deg\n");
    fflush(stdout);

    while (keep_running) {
        mag_sample_t sample;
        double t;

        if (read_chip(fd, chip, &sample) != 0) {
            fprintf(stderr, "I2C read failed: %s\n", strerror(errno));
            close(fd);
            return 1;
        }

        t = elapsed_seconds(&start);
        printf("%8.3f %7d %7d %7d %9.2f %9.2f %9.2f %9.2f\n",
               t, sample.x, sample.y, sample.z,
               sample.x_ut, sample.y_ut, sample.z_ut, sample.heading_deg);
        fflush(stdout);

        if (opts.once) {
            break;
        }
        sleep_ms(opts.interval_ms);
    }

    close(fd);
    return 0;
}
