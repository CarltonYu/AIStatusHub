/*
 * Duo UDP console input daemon
 *
 * Listen on a UDP port and inject typed characters into a local Linux TTY
 * using TIOCSTI, so commands appear on the ST7789V framebuffer console (tty1).
 *
 * Supported commands (one per UDP packet/line):
 *   echo <text>        Type literal text (like a shell echo)
 *   cmd <key>          Press a special key: enter, tab, backspace, escape,
 *                      up, down, left, right, home, end, pgup, pgdown
 *   combo <mod>+<key>  Modifier combo: ctrl+c, ctrl+d, ctrl+l, alt+f4, etc.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifndef TIOCSTI
#define TIOCSTI 0x5412
#endif

static int tty_fd = -1;

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [--udp-port <port>] [--tty <path>]\n", prog);
    exit(1);
}

static void inject_char(char c) {
    if (tty_fd < 0) return;
    if (ioctl(tty_fd, TIOCSTI, &c) < 0) {
        perror("TIOCSTI");
    }
}

static void inject_string(const char *s) {
    while (*s) {
        inject_char(*s++);
    }
}

static void inject_csi(const char *seq) {
    inject_char('\x1b');
    inject_char('[');
    inject_string(seq);
}

static int handle_cmd(const char *arg) {
    if (strcmp(arg, "enter") == 0) inject_char('\n');
    else if (strcmp(arg, "return") == 0) inject_char('\r');
    else if (strcmp(arg, "tab") == 0) inject_char('\t');
    else if (strcmp(arg, "backspace") == 0) inject_char('\x7f');
    else if (strcmp(arg, "delete") == 0) inject_csi("3~");
    else if (strcmp(arg, "escape") == 0 || strcmp(arg, "esc") == 0) inject_char('\x1b');
    else if (strcmp(arg, "space") == 0) inject_char(' ');
    else if (strcmp(arg, "up") == 0) inject_csi("A");
    else if (strcmp(arg, "down") == 0) inject_csi("B");
    else if (strcmp(arg, "right") == 0) inject_csi("C");
    else if (strcmp(arg, "left") == 0) inject_csi("D");
    else if (strcmp(arg, "home") == 0) inject_csi("H");
    else if (strcmp(arg, "end") == 0) inject_csi("F");
    else if (strcmp(arg, "pgup") == 0 || strcmp(arg, "pageup") == 0) inject_csi("5~");
    else if (strcmp(arg, "pgdown") == 0 || strcmp(arg, "pagedown") == 0) inject_csi("6~");
    else if (strcmp(arg, "insert") == 0) inject_csi("2~");
    else {
        fprintf(stderr, "unknown cmd: %s\n", arg);
        return -1;
    }
    return 0;
}

static int char_to_key(char c) {
    if (c >= 'a' && c <= 'z') return c - 'a';
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '0' && c <= '9') return c - '0';
    if (c == '[') return 0x1b;
    if (c == '\\') return 0x1c;
    if (c == ']') return 0x1d;
    if (c == '^') return 0x1e;
    if (c == '_') return 0x1f;
    return -1;
}

static int handle_combo(const char *arg) {
    char buf[256];
    if (strlen(arg) >= sizeof(buf)) return -1;
    strcpy(buf, arg);

    char *plus = strrchr(buf, '+');
    if (!plus) {
        fprintf(stderr, "combo missing '+': %s\n", arg);
        return -1;
    }
    *plus = '\0';
    const char *mods = buf;
    const char *key = plus + 1;

    int ctrl = 0, alt = 0, shift = 0;
    char *tok = strtok((char *)mods, "+");
    while (tok) {
        if (strcmp(tok, "ctrl") == 0 || strcmp(tok, "control") == 0) ctrl = 1;
        else if (strcmp(tok, "alt") == 0) alt = 1;
        else if (strcmp(tok, "shift") == 0) shift = 1;
        else {
            fprintf(stderr, "unknown modifier: %s\n", tok);
            return -1;
        }
        tok = strtok(NULL, "+");
    }

    if (strlen(key) == 1) {
        char c = key[0];
        if (shift) {
            inject_char(toupper(c));
        } else if (ctrl) {
            int k = char_to_key(tolower(c));
            if (k >= 0 && k <= 26) inject_char((char)(k + 1));
            else fprintf(stderr, "unsupported ctrl combo: %s\n", arg);
        } else if (alt) {
            inject_char('\x1b');
            inject_char(c);
        } else {
            inject_char(c);
        }
    } else {
        /* named key with modifiers */
        if (ctrl && strcmp(key, "c") == 0) inject_char('\x03');
        else if (ctrl && strcmp(key, "d") == 0) inject_char('\x04');
        else if (ctrl && strcmp(key, "l") == 0) inject_char('\x0c');
        else if (ctrl && strcmp(key, "z") == 0) inject_char('\x1a');
        else if (ctrl && strcmp(key, "x") == 0) inject_char('\x18');
        else if (ctrl && strcmp(key, "v") == 0) inject_char('\x16');
        else if (strcmp(key, "enter") == 0) {
            if (ctrl) inject_char('\n');
            else inject_char('\n');
        }
        else {
            fprintf(stderr, "unsupported combo: %s\n", arg);
            return -1;
        }
    }
    return 0;
}

static void process_line(const char *line) {
    while (*line == ' ' || *line == '\t') line++;
    if (line[0] == '\0') return;

    if (strncmp(line, "echo ", 5) == 0) {
        inject_string(line + 5);
    } else if (strncmp(line, "cmd ", 4) == 0) {
        handle_cmd(line + 4);
    } else if (strncmp(line, "combo ", 6) == 0) {
        handle_combo(line + 6);
    } else {
        /* Default: treat whole line as string */
        inject_string(line);
    }
}

int main(int argc, char **argv) {
    int udp_port = 25251;
    const char *tty_path = "/dev/tty1";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--udp-port") == 0 && i + 1 < argc) {
            udp_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--tty") == 0 && i + 1 < argc) {
            tty_path = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
        } else {
            usage(argv[0]);
        }
    }

    tty_fd = open(tty_path, O_RDWR | O_NOCTTY);
    if (tty_fd < 0) {
        perror(tty_path);
        return 1;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(udp_port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    char buf[1024];
    while (1) {
        ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("recv");
            break;
        }
        if (n == 0) continue;
        buf[n] = '\0';

        /* Remove trailing newline/carriage return */
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
            buf[--n] = '\0';
        }

        process_line(buf);
    }

    close(sock);
    close(tty_fd);
    return 0;
}
