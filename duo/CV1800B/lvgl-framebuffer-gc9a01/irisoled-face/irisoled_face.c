#include <errno.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "irisoled_bitmaps.h"

#define DEFAULT_FB "/dev/fb0"
#define DEFAULT_SOCKET "/tmp/irisoled-face.sock"
#define DEFAULT_UDP_PORT 25250
#define DEFAULT_FPS 20
#define DEFAULT_FACE_W 224
#define DEFAULT_FACE_H 112
#define DEFAULT_COLOR 0x00d7ff
#define DEFAULT_EXPRESSION "normal"
#define MAX_SEQUENCE_STEPS 16
#define MAX_COMMAND 160

struct fb_ctx {
    int fd;
    uint8_t *mem;
    size_t bytes;
    struct fb_fix_screeninfo finfo;
    struct fb_var_screeninfo vinfo;
};

struct render_cfg {
    const char *fb_path;
    const char *socket_path;
    uint16_t udp_port;
    uint32_t fps;
    uint32_t face_w;
    uint32_t face_h;
    uint8_t color_r;
    uint8_t color_g;
    uint8_t color_b;
    const char *default_name;
};

struct frame_step {
    const char *name;
    int duration_ms;
    int dx;
    int dy;
    int brightness;
    int invert;
};

struct sequence {
    struct frame_step steps[MAX_SEQUENCE_STEPS];
    size_t count;
};

struct playback {
    struct sequence seq;
    size_t index;
    long long step_until;
    long long command_until;
    int repeat_left;
    int command_mode;
    char active_name[64];
    char default_name[64];
};

static volatile sig_atomic_t g_running = 1;

static void on_signal(int signum)
{
    (void)signum;
    g_running = 0;
}

static long long now_ms(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
}

static int parse_u32_arg(const char *s, uint32_t *out)
{
    char *end = NULL;
    unsigned long value;

    if (!s || !*s)
        return -1;
    errno = 0;
    value = strtoul(s, &end, 10);
    if (errno || !end || *end)
        return -1;
    *out = (uint32_t)value;
    return 0;
}

static int parse_u16_arg(const char *s, uint16_t *out)
{
    uint32_t value;

    if (parse_u32_arg(s, &value) < 0 || value > 65535U)
        return -1;
    *out = (uint16_t)value;
    return 0;
}

static int parse_duration_ms(const char *s, uint32_t *out)
{
    char *end = NULL;
    unsigned long value;

    if (!s || !*s)
        return -1;
    errno = 0;
    value = strtoul(s, &end, 10);
    if (errno || !end)
        return -1;
    if (strcmp(end, "ms") == 0 || *end == '\0') {
        *out = (uint32_t)value;
        return 0;
    }
    if (strcmp(end, "s") == 0) {
        *out = (uint32_t)(value * 1000UL);
        return 0;
    }
    return -1;
}

static int parse_hex_color(const char *s, uint8_t *r, uint8_t *g, uint8_t *b)
{
    char *end = NULL;
    unsigned long value;

    if (!s)
        return -1;
    if (*s == '#')
        s++;
    if (strlen(s) != 6)
        return -1;
    errno = 0;
    value = strtoul(s, &end, 16);
    if (errno || !end || *end)
        return -1;
    *r = (uint8_t)((value >> 16) & 0xff);
    *g = (uint8_t)((value >> 8) & 0xff);
    *b = (uint8_t)(value & 0xff);
    return 0;
}

static uint32_t chan(uint8_t value, const struct fb_bitfield *field)
{
    if (!field->length)
        return 0;
    return (uint32_t)(value >> (8 - field->length)) << field->offset;
}

static uint32_t pack_color(const struct fb_var_screeninfo *vinfo,
                           uint8_t r, uint8_t g, uint8_t b)
{
    return chan(r, &vinfo->red) |
           chan(g, &vinfo->green) |
           chan(b, &vinfo->blue);
}

static int is_rgb565(const struct fb_var_screeninfo *vinfo)
{
    return vinfo->bits_per_pixel == 16 &&
           vinfo->red.offset == 11 && vinfo->red.length == 5 &&
           vinfo->green.offset == 5 && vinfo->green.length == 6 &&
           vinfo->blue.offset == 0 && vinfo->blue.length == 5;
}

static int open_fb(struct fb_ctx *fb, const char *dev)
{
    memset(fb, 0, sizeof(*fb));
    fb->fd = open(dev, O_RDWR);
    if (fb->fd < 0) {
        fprintf(stderr, "open %s: %s\n", dev, strerror(errno));
        return -1;
    }

    if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &fb->finfo) < 0) {
        perror("FBIOGET_FSCREENINFO");
        close(fb->fd);
        return -1;
    }
    if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &fb->vinfo) < 0) {
        perror("FBIOGET_VSCREENINFO");
        close(fb->fd);
        return -1;
    }
    if (fb->vinfo.bits_per_pixel != 16 && fb->vinfo.bits_per_pixel != 32) {
        fprintf(stderr, "unsupported framebuffer bpp: %u\n",
                fb->vinfo.bits_per_pixel);
        close(fb->fd);
        return -1;
    }

    fb->bytes = fb->finfo.smem_len;
    if (!fb->bytes)
        fb->bytes = (size_t)fb->finfo.line_length * fb->vinfo.yres;

    fb->mem = mmap(NULL, fb->bytes, PROT_READ | PROT_WRITE, MAP_SHARED,
                   fb->fd, 0);
    if (fb->mem == MAP_FAILED) {
        perror("mmap framebuffer");
        close(fb->fd);
        return -1;
    }

    printf("%s: %ux%u, %u bpp, line_length=%u\n",
           dev, fb->vinfo.xres, fb->vinfo.yres,
           fb->vinfo.bits_per_pixel, fb->finfo.line_length);
    return 0;
}

static void close_fb(struct fb_ctx *fb)
{
    if (fb->mem && fb->mem != MAP_FAILED)
        munmap(fb->mem, fb->bytes);
    if (fb->fd >= 0)
        close(fb->fd);
}

static const char *alias_name(const char *name)
{
    if (strcmp(name, "idle") == 0)
        return "normal";
    if (strcmp(name, "sleep") == 0 || strcmp(name, "sleeping") == 0)
        return "sleepy";
    if (strcmp(name, "busy") == 0)
        return "focused";
    return name;
}

static const char *resolve_bitmap_name(const char *name)
{
    const char *canon = alias_name(name);
    size_t i;

    for (i = 0; i < irisoled_bitmap_count; i++) {
        if (strcmp(irisoled_bitmaps[i].name, canon) == 0)
            return irisoled_bitmaps[i].name;
    }
    return NULL;
}

static const char *canonical_name(const char *name)
{
    const char *resolved = resolve_bitmap_name(name);

    return resolved ? resolved : alias_name(name);
}

static const uint8_t *find_bitmap(const char *name)
{
    const char *resolved = resolve_bitmap_name(name);
    size_t i;

    if (!resolved)
        return NULL;

    for (i = 0; i < irisoled_bitmap_count; i++) {
        if (strcmp(irisoled_bitmaps[i].name, resolved) == 0)
            return irisoled_bitmaps[i].data;
    }
    return NULL;
}

static int bitmap_pixel(const uint8_t *bitmap, int x, int y)
{
    const uint8_t byte = bitmap[y * 16 + x / 8];
    return (byte & (0x80 >> (x & 7))) != 0;
}

static void put_pixel(struct fb_ctx *fb, int x, int y,
                      uint8_t r, uint8_t g, uint8_t b)
{
    const int bpp = fb->vinfo.bits_per_pixel / 8;
    uint8_t *dst;

    if (x < 0 || y < 0 || x >= (int)fb->vinfo.xres || y >= (int)fb->vinfo.yres)
        return;

    dst = fb->mem + (size_t)y * fb->finfo.line_length + (size_t)x * bpp;
    if (is_rgb565(&fb->vinfo)) {
        uint16_t pixel = (uint16_t)(((r & 0xf8) << 8) |
                                    ((g & 0xfc) << 3) |
                                    (b >> 3));
        memcpy(dst, &pixel, sizeof(pixel));
    } else {
        uint32_t pixel = pack_color(&fb->vinfo, r, g, b);
        memcpy(dst, &pixel, (size_t)bpp);
    }
}

static void clear_fb(struct fb_ctx *fb)
{
    size_t visible = (size_t)fb->finfo.line_length * fb->vinfo.yres;

    memset(fb->mem, 0, visible);
}

static void draw_bitmap_frame(struct fb_ctx *fb, const struct render_cfg *cfg,
                              const struct frame_step *step)
{
    const uint8_t *bitmap = find_bitmap(step->name);
    uint32_t face_w = cfg->face_w;
    uint32_t face_h = cfg->face_h;
    int x0;
    int y0;
    uint32_t x;
    uint32_t y;
    uint8_t r;
    uint8_t g;
    uint8_t b;

    if (!bitmap)
        bitmap = find_bitmap("normal");

    if (face_w > fb->vinfo.xres)
        face_w = fb->vinfo.xres;
    if (face_h > fb->vinfo.yres)
        face_h = fb->vinfo.yres;

    clear_fb(fb);
    x0 = ((int)fb->vinfo.xres - (int)face_w) / 2 + step->dx;
    y0 = ((int)fb->vinfo.yres - (int)face_h) / 2 + step->dy;
    r = (uint8_t)(cfg->color_r * step->brightness / 255);
    g = (uint8_t)(cfg->color_g * step->brightness / 255);
    b = (uint8_t)(cfg->color_b * step->brightness / 255);

    for (y = 0; y < face_h; y++) {
        int sy = (int)(y * IRISOLED_BITMAP_HEIGHT / face_h);

        for (x = 0; x < face_w; x++) {
            int sx = (int)(x * IRISOLED_BITMAP_WIDTH / face_w);
            int on = bitmap_pixel(bitmap, sx, sy);

            if (step->invert)
                on = !on;
            if (on)
                put_pixel(fb, x0 + (int)x, y0 + (int)y, r, g, b);
        }
    }
    msync(fb->mem, fb->bytes, MS_ASYNC);
}

static void seq_clear(struct sequence *seq)
{
    memset(seq, 0, sizeof(*seq));
}

static void seq_add(struct sequence *seq, const char *name,
                    int duration_ms, int dx, int dy, int brightness)
{
    struct frame_step *step;
    const char *resolved = resolve_bitmap_name(name);

    if (seq->count >= MAX_SEQUENCE_STEPS)
        return;
    step = &seq->steps[seq->count++];
    step->name = resolved ? resolved : "normal";
    step->duration_ms = duration_ms;
    step->dx = dx;
    step->dy = dy;
    step->brightness = brightness;
    step->invert = 0;
}

static void build_normal_sequence(struct sequence *seq)
{
    seq_clear(seq);
    seq_add(seq, "normal", 2600, 0, 0, 255);
    seq_add(seq, "blink_down", 80, 0, 0, 220);
    seq_add(seq, "blink", 90, 0, 0, 220);
    seq_add(seq, "blink_up", 80, 0, 0, 220);
    seq_add(seq, "normal", 1800, 0, 0, 255);
}

static void build_expression_sequence(struct sequence *seq, const char *name)
{
    const char *canon = canonical_name(name);

    seq_clear(seq);
    if (strcmp(canon, "normal") == 0) {
        build_normal_sequence(seq);
    } else if (strcmp(canon, "sleepy") == 0) {
        seq_add(seq, "sleepy", 900, 0, 0, 210);
        seq_add(seq, "blink_down", 250, 0, 1, 160);
        seq_add(seq, "blink", 900, 0, 2, 130);
        seq_add(seq, "blink_up", 250, 0, 1, 160);
    } else if (strcmp(canon, "blink") == 0) {
        seq_add(seq, "blink_down", 90, 0, 0, 255);
        seq_add(seq, "blink", 130, 0, 0, 255);
        seq_add(seq, "blink_up", 90, 0, 0, 255);
    } else if (strcmp(canon, "happy") == 0 || strcmp(canon, "excited") == 0) {
        seq_add(seq, canon, 220, 0, 0, 255);
        seq_add(seq, canon, 140, 0, -4, 255);
        seq_add(seq, canon, 180, 0, 0, 255);
        seq_add(seq, canon, 140, 0, -2, 235);
        seq_add(seq, canon, 280, 0, 0, 255);
    } else if (strcmp(canon, "angry") == 0 || strcmp(canon, "furious") == 0) {
        seq_add(seq, canon, 120, -5, 0, 255);
        seq_add(seq, canon, 120, 5, 0, 255);
        seq_add(seq, canon, 120, -3, 0, 255);
        seq_add(seq, canon, 120, 3, 0, 255);
        seq_add(seq, canon, 500, 0, 0, 255);
    } else if (strcmp(canon, "focused") == 0 || strcmp(canon, "alert") == 0) {
        seq_add(seq, canon, 320, 0, 0, 255);
        seq_add(seq, canon, 180, 0, 0, 220);
        seq_add(seq, canon, 320, 0, 0, 255);
    } else if (strcmp(canon, "disoriented") == 0 || strcmp(canon, "scared") == 0) {
        seq_add(seq, canon, 120, -4, -2, 255);
        seq_add(seq, canon, 120, 4, 2, 230);
        seq_add(seq, canon, 120, -2, 1, 255);
        seq_add(seq, canon, 420, 0, 0, 240);
    } else {
        seq_add(seq, canon, 1000, 0, 0, 255);
    }
}

static void playback_set_default_name(struct playback *pb, const char *name)
{
    snprintf(pb->default_name, sizeof(pb->default_name), "%s", canonical_name(name));
}

static void playback_start_default(struct playback *pb)
{
    build_expression_sequence(&pb->seq, pb->default_name);
    if (!pb->seq.count)
        build_expression_sequence(&pb->seq, DEFAULT_EXPRESSION);

    pb->index = 0;
    pb->step_until = now_ms() + pb->seq.steps[0].duration_ms;
    pb->command_until = 0;
    pb->repeat_left = -1;
    pb->command_mode = 0;
    snprintf(pb->active_name, sizeof(pb->active_name), "%s", pb->default_name);
}

static void playback_init(struct playback *pb, const char *default_name)
{
    memset(pb, 0, sizeof(*pb));
    playback_set_default_name(pb, default_name);
    playback_start_default(pb);
}

static void playback_start_command(struct playback *pb, const char *name,
                                   int repeat, uint32_t duration_ms)
{
    build_expression_sequence(&pb->seq, name);
    if (!pb->seq.count)
        build_expression_sequence(&pb->seq, "normal");

    pb->index = 0;
    pb->step_until = now_ms() + pb->seq.steps[0].duration_ms;
    pb->command_until = duration_ms ? now_ms() + duration_ms : 0;
    pb->repeat_left = duration_ms ? -1 : repeat;
    pb->command_mode = 1;
    snprintf(pb->active_name, sizeof(pb->active_name), "%s", canonical_name(name));
}

static const struct frame_step *playback_tick(struct playback *pb)
{
    long long now = now_ms();

    if (pb->command_mode && pb->command_until > 0 && now >= pb->command_until) {
        playback_start_default(pb);
        printf("state: %s\n", pb->active_name);
    } else if (now >= pb->step_until && pb->seq.count > 0) {
        pb->index++;
        if (pb->index >= pb->seq.count) {
            pb->index = 0;
            if (pb->command_mode && pb->repeat_left > 0) {
                pb->repeat_left--;
                if (pb->repeat_left <= 0) {
                    playback_start_default(pb);
                    printf("state: %s\n", pb->active_name);
                }
            }
        }
        pb->step_until = now_ms() + pb->seq.steps[pb->index].duration_ms;
    }
    return &pb->seq.steps[pb->index];
}

static int setup_socket(const char *path)
{
    struct sockaddr_un addr;
    int fd;

    fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    unlink(path);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind socket");
        close(fd);
        return -1;
    }
    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
        perror("fcntl socket");
        close(fd);
        unlink(path);
        return -1;
    }
    return fd;
}

static int setup_udp_socket(uint16_t port)
{
    struct sockaddr_in addr;
    int fd;
    int one = 1;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("udp socket");
        return -1;
    }
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind udp socket");
        close(fd);
        return -1;
    }
    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
        perror("fcntl udp socket");
        close(fd);
        return -1;
    }
    return fd;
}

static int send_command(const char *socket_path, const char *message)
{
    struct sockaddr_un addr;
    int fd;
    ssize_t sent;

    fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path);
    sent = sendto(fd, message, strlen(message), 0,
                  (struct sockaddr *)&addr, sizeof(addr));
    close(fd);
    if (sent < 0) {
        fprintf(stderr, "send command to %s: %s\n", socket_path, strerror(errno));
        return -1;
    }
    return 0;
}

static void lowercase_ascii(char *s)
{
    for (; s && *s; s++) {
        if (*s >= 'A' && *s <= 'Z')
            *s = (char)(*s - 'A' + 'a');
    }
}

static int handle_message(struct playback *pb, char *msg)
{
    char *cmd = strtok(msg, " \t\r\n");
    char *name;
    int repeat = 1;
    uint32_t duration_ms = 0;
    char *token;

    if (!cmd)
        return 0;
    if (strcmp(cmd, "QUIT") == 0) {
        g_running = 0;
        return 0;
    }
    if (strcmp(cmd, "NORMAL") == 0) {
        playback_set_default_name(pb, DEFAULT_EXPRESSION);
        playback_start_default(pb);
        printf("default: %s\n", pb->default_name);
        printf("state: %s\n", pb->active_name);
        return 0;
    }
    if (strcmp(cmd, "DEFAULT") == 0) {
        name = strtok(NULL, " \t\r\n");
        if (!name || !resolve_bitmap_name(name)) {
            fprintf(stderr, "unknown IrisOLED expression: %s\n", name ? name : "(null)");
            return -1;
        }
        playback_set_default_name(pb, name);
        playback_start_default(pb);
        printf("default: %s\n", pb->default_name);
        printf("state: %s\n", pb->active_name);
        return 0;
    }
    if (strcmp(cmd, "PLAY") != 0)
        return -1;

    name = strtok(NULL, " \t\r\n");
    if (!name || !resolve_bitmap_name(name)) {
        fprintf(stderr, "unknown IrisOLED expression: %s\n", name ? name : "(null)");
        return -1;
    }

    while ((token = strtok(NULL, " \t\r\n")) != NULL) {
        if (strncmp(token, "repeat=", 7) == 0) {
            repeat = atoi(token + 7);
            if (repeat <= 0)
                repeat = 1;
        } else if (strncmp(token, "duration=", 9) == 0) {
            if (parse_duration_ms(token + 9, &duration_ms) < 0)
                duration_ms = 0;
        }
    }

    playback_start_command(pb, canonical_name(name), repeat, duration_ms);
    printf("state: %s repeat=%d duration_ms=%u\n",
           canonical_name(name), repeat, duration_ms);
    return 0;
}

static int build_public_message(const char *input, char *out, size_t out_size)
{
    char buf[MAX_COMMAND];
    char name_buf[64];
    char *cmd;
    char *name;
    char *token;
    int repeat = 1;
    uint32_t duration_ms = 0;

    if (!input || !out || out_size == 0)
        return -1;

    snprintf(buf, sizeof(buf), "%s", input);
    cmd = strtok(buf, " \t\r\n");
    if (!cmd)
        return -1;
    if (strcasecmp(cmd, "irisoled-face") == 0) {
        cmd = strtok(NULL, " \t\r\n");
        if (!cmd)
            return -1;
    }

    if (strcasecmp(cmd, "normal") == 0) {
        snprintf(out, out_size, "NORMAL");
        return 0;
    }
    if (strcasecmp(cmd, "stop") == 0 || strcasecmp(cmd, "stop-daemon") == 0) {
        snprintf(out, out_size, "QUIT");
        return 0;
    }
    if (strcasecmp(cmd, "default") == 0) {
        name = strtok(NULL, " \t\r\n");
        if (!name)
            return -1;
        snprintf(name_buf, sizeof(name_buf), "%s", name);
        lowercase_ascii(name_buf);
        if (!resolve_bitmap_name(name_buf))
            return -1;
        snprintf(out, out_size, "DEFAULT %s", canonical_name(name_buf));
        return 0;
    }
    if (strcasecmp(cmd, "play") != 0)
        return -1;

    name = strtok(NULL, " \t\r\n");
    if (!name)
        return -1;
    snprintf(name_buf, sizeof(name_buf), "%s", name);
    lowercase_ascii(name_buf);
    if (!resolve_bitmap_name(name_buf))
        return -1;

    while ((token = strtok(NULL, " \t\r\n")) != NULL) {
        if (strcmp(token, "--repeat") == 0) {
            token = strtok(NULL, " \t\r\n");
            if (!token)
                return -1;
            repeat = atoi(token);
            if (repeat <= 0)
                repeat = 1;
        } else if (strncmp(token, "repeat=", 7) == 0) {
            repeat = atoi(token + 7);
            if (repeat <= 0)
                repeat = 1;
        } else if (strcmp(token, "--duration") == 0) {
            token = strtok(NULL, " \t\r\n");
            if (!token || parse_duration_ms(token, &duration_ms) < 0)
                return -1;
        } else if (strncmp(token, "duration=", 9) == 0) {
            if (parse_duration_ms(token + 9, &duration_ms) < 0)
                return -1;
        } else {
            return -1;
        }
    }

    snprintf(out, out_size, "PLAY %s repeat=%d duration=%ums",
             canonical_name(name_buf), repeat, duration_ms);
    return 0;
}

static int handle_public_message(struct playback *pb, const char *input)
{
    char message[MAX_COMMAND];

    if (build_public_message(input, message, sizeof(message)) < 0)
        return -1;
    return handle_message(pb, message);
}

static int run_daemon(struct render_cfg *cfg)
{
    struct fb_ctx fb;
    struct playback pb;
    int sock;
    int udp_sock = -1;
    long long next_frame;
    uint32_t frame_delay_ms = cfg->fps ? 1000U / cfg->fps : 50U;

    if (open_fb(&fb, cfg->fb_path) < 0)
        return 1;

    sock = setup_socket(cfg->socket_path);
    if (sock < 0) {
        close_fb(&fb);
        return 1;
    }
    if (cfg->udp_port > 0) {
        udp_sock = setup_udp_socket(cfg->udp_port);
        if (udp_sock < 0) {
            close(sock);
            unlink(cfg->socket_path);
            close_fb(&fb);
            return 1;
        }
    }

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    if (!resolve_bitmap_name(cfg->default_name)) {
        fprintf(stderr, "unknown default expression: %s; using normal\n",
                cfg->default_name);
        cfg->default_name = DEFAULT_EXPRESSION;
    }
    playback_init(&pb, cfg->default_name);
    printf("IrisOLED face daemon: socket=%s, udp=%u, fps=%u, face=%ux%u, default=%s\n",
           cfg->socket_path, cfg->udp_port, cfg->fps, cfg->face_w, cfg->face_h,
           pb.default_name);

    next_frame = now_ms();
    while (g_running) {
        char msg[MAX_COMMAND];
        ssize_t n;
        struct timeval tv;
        fd_set rfds;
        int maxfd = sock;
        long long now = now_ms();
        long long wait_ms = next_frame > now ? next_frame - now : 0;

        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);
        if (udp_sock >= 0) {
            FD_SET(udp_sock, &rfds);
            if (udp_sock > maxfd)
                maxfd = udp_sock;
        }
        tv.tv_sec = wait_ms / 1000;
        tv.tv_usec = (wait_ms % 1000) * 1000;
        if (select(maxfd + 1, &rfds, NULL, NULL, &tv) > 0) {
            if (FD_ISSET(sock, &rfds)) {
                while ((n = recv(sock, msg, sizeof(msg) - 1, 0)) > 0) {
                    msg[n] = '\0';
                    handle_message(&pb, msg);
                }
            }
            if (udp_sock >= 0 && FD_ISSET(udp_sock, &rfds)) {
                struct sockaddr_in peer;
                socklen_t peer_len = sizeof(peer);

                while ((n = recvfrom(udp_sock, msg, sizeof(msg) - 1, 0,
                                     (struct sockaddr *)&peer, &peer_len)) > 0) {
                    const char *reply;

                    msg[n] = '\0';
                    reply = handle_public_message(&pb, msg) == 0 ? "OK\n" : "ERR\n";
                    sendto(udp_sock, reply, strlen(reply), 0,
                           (struct sockaddr *)&peer, peer_len);
                    peer_len = sizeof(peer);
                }
            }
        }

        now = now_ms();
        if (now >= next_frame) {
            const struct frame_step *step = playback_tick(&pb);
            draw_bitmap_frame(&fb, cfg, step);
            next_frame = now + frame_delay_ms;
        }
    }

    clear_fb(&fb);
    msync(fb.mem, fb.bytes, MS_SYNC);
    close(sock);
    if (udp_sock >= 0)
        close(udp_sock);
    unlink(cfg->socket_path);
    close_fb(&fb);
    return 0;
}

static void print_list(void)
{
    size_t i;

    printf("IrisOLED names:\n");
    for (i = 0; i < irisoled_bitmap_count; i++)
        printf("  %s\n", irisoled_bitmaps[i].name);
    printf("Aliases: idle=normal, sleep/sleeping=sleepy, busy=focused\n");
}

static void usage(const char *argv0)
{
    printf("Usage:\n");
    printf("  %s daemon [--fb /dev/fb0] [--socket PATH] [--udp-port N] [--fps N] [--width N] [--height N] [--color RRGGBB] [--default NAME]\n", argv0);
    printf("  %s play <name> [--repeat N] [--duration 5000ms|5s] [--socket PATH]\n", argv0);
    printf("  %s default <name> [--socket PATH]\n", argv0);
    printf("  %s normal [--socket PATH]\n", argv0);
    printf("  %s stop-daemon [--socket PATH]\n", argv0);
    printf("  %s list\n", argv0);
}

static int command_socket_arg(int argc, char **argv, int start, const char **socket_path)
{
    int i;

    for (i = start; i < argc; i++) {
        if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
            *socket_path = argv[++i];
        } else {
            return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    struct render_cfg cfg = {
        .fb_path = DEFAULT_FB,
        .socket_path = DEFAULT_SOCKET,
        .udp_port = DEFAULT_UDP_PORT,
        .fps = DEFAULT_FPS,
        .face_w = DEFAULT_FACE_W,
        .face_h = DEFAULT_FACE_H,
        .color_r = (DEFAULT_COLOR >> 16) & 0xff,
        .color_g = (DEFAULT_COLOR >> 8) & 0xff,
        .color_b = DEFAULT_COLOR & 0xff,
        .default_name = DEFAULT_EXPRESSION,
    };
    const char *socket_path = DEFAULT_SOCKET;
    int i;

    setbuf(stdout, NULL);
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "daemon") == 0) {
        for (i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--fb") == 0 && i + 1 < argc) {
                cfg.fb_path = argv[++i];
            } else if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
                cfg.socket_path = argv[++i];
            } else if (strcmp(argv[i], "--udp-port") == 0 && i + 1 < argc) {
                if (parse_u16_arg(argv[++i], &cfg.udp_port) < 0)
                    return 1;
            } else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
                if (parse_u32_arg(argv[++i], &cfg.fps) < 0 || cfg.fps == 0)
                    return 1;
            } else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
                if (parse_u32_arg(argv[++i], &cfg.face_w) < 0)
                    return 1;
            } else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
                if (parse_u32_arg(argv[++i], &cfg.face_h) < 0)
                    return 1;
            } else if (strcmp(argv[i], "--color") == 0 && i + 1 < argc) {
                if (parse_hex_color(argv[++i], &cfg.color_r, &cfg.color_g, &cfg.color_b) < 0)
                    return 1;
            } else if (strcmp(argv[i], "--default") == 0 && i + 1 < argc) {
                cfg.default_name = canonical_name(argv[++i]);
            } else {
                usage(argv[0]);
                return 1;
            }
        }
        return run_daemon(&cfg);
    }

    if (strcmp(argv[1], "list") == 0) {
        print_list();
        return 0;
    }

    if (strcmp(argv[1], "play") == 0) {
        char message[MAX_COMMAND];
        const char *name;
        int repeat = 1;
        uint32_t duration_ms = 0;

        if (argc < 3) {
            usage(argv[0]);
            return 1;
        }
        name = argv[2];
        if (!find_bitmap(canonical_name(name))) {
            fprintf(stderr, "unknown IrisOLED expression: %s\n", name);
            print_list();
            return 1;
        }
        for (i = 3; i < argc; i++) {
            if (strcmp(argv[i], "--repeat") == 0 && i + 1 < argc) {
                repeat = atoi(argv[++i]);
                if (repeat <= 0)
                    repeat = 1;
            } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
                if (parse_duration_ms(argv[++i], &duration_ms) < 0) {
                    fprintf(stderr, "invalid duration\n");
                    return 1;
                }
            } else if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
                socket_path = argv[++i];
            } else {
                usage(argv[0]);
                return 1;
            }
        }
        snprintf(message, sizeof(message), "PLAY %s repeat=%d duration=%ums",
                 canonical_name(name), repeat, duration_ms);
        return send_command(socket_path, message) < 0 ? 1 : 0;
    }

    if (strcmp(argv[1], "default") == 0) {
        char message[MAX_COMMAND];
        const char *name;

        if (argc < 3) {
            usage(argv[0]);
            return 1;
        }
        name = argv[2];
        if (!resolve_bitmap_name(name)) {
            fprintf(stderr, "unknown IrisOLED expression: %s\n", name);
            print_list();
            return 1;
        }
        if (command_socket_arg(argc, argv, 3, &socket_path) < 0) {
            usage(argv[0]);
            return 1;
        }
        snprintf(message, sizeof(message), "DEFAULT %s", canonical_name(name));
        return send_command(socket_path, message) < 0 ? 1 : 0;
    }

    if (strcmp(argv[1], "normal") == 0) {
        if (command_socket_arg(argc, argv, 2, &socket_path) < 0) {
            usage(argv[0]);
            return 1;
        }
        return send_command(socket_path, "NORMAL") < 0 ? 1 : 0;
    }

    if (strcmp(argv[1], "stop-daemon") == 0) {
        if (command_socket_arg(argc, argv, 2, &socket_path) < 0) {
            usage(argv[0]);
            return 1;
        }
        return send_command(socket_path, "QUIT") < 0 ? 1 : 0;
    }

    usage(argv[0]);
    return 1;
}
