#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "gc9a01.h"

// Convert RGB888 to RGB565
static inline uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static void draw_pixel(uint16_t *fb, int x, int y, uint16_t color) {
    if (x < 0 || x >= GC9A01_WIDTH || y < 0 || y >= GC9A01_HEIGHT) return;
    fb[y * GC9A01_WIDTH + x] = color;
}

static void draw_hline(uint16_t *fb, int x0, int x1, int y, uint16_t color) {
    if (y < 0 || y >= GC9A01_HEIGHT) return;
    if (x0 < 0) x0 = 0;
    if (x1 >= GC9A01_WIDTH) x1 = GC9A01_WIDTH - 1;
    for (int x = x0; x <= x1; x++) {
        fb[y * GC9A01_WIDTH + x] = color;
    }
}

static void draw_vline(uint16_t *fb, int x, int y0, int y1, uint16_t color) {
    if (x < 0 || x >= GC9A01_WIDTH) return;
    if (y0 < 0) y0 = 0;
    if (y1 >= GC9A01_HEIGHT) y1 = GC9A01_HEIGHT - 1;
    for (int y = y0; y <= y1; y++) {
        fb[y * GC9A01_WIDTH + x] = color;
    }
}

static void draw_circle(uint16_t *fb, int cx, int cy, int r, uint16_t color, bool fill) {
    int x = 0, y = r;
    int d = 3 - 2 * r;
    while (y >= x) {
        if (fill) {
            draw_hline(fb, cx - x, cx + x, cy + y, color);
            draw_hline(fb, cx - x, cx + x, cy - y, color);
            draw_hline(fb, cx - y, cx + y, cy + x, color);
            draw_hline(fb, cx - y, cx + y, cy - x, color);
        } else {
            draw_pixel(fb, cx + x, cy + y, color);
            draw_pixel(fb, cx - x, cy + y, color);
            draw_pixel(fb, cx + x, cy - y, color);
            draw_pixel(fb, cx - x, cy - y, color);
            draw_pixel(fb, cx + y, cy + x, color);
            draw_pixel(fb, cx - y, cy + x, color);
            draw_pixel(fb, cx + y, cy - x, color);
            draw_pixel(fb, cx - y, cy - x, color);
        }
        x++;
        if (d > 0) {
            y--;
            d = d + 4 * (x - y) + 10;
        } else {
            d = d + 4 * x + 6;
        }
    }
}

static void draw_rect(uint16_t *fb, int x0, int y0, int x1, int y1, uint16_t color, bool fill) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (fill) {
        for (int y = y0; y <= y1; y++) {
            draw_hline(fb, x0, x1, y, color);
        }
    } else {
        draw_hline(fb, x0, x1, y0, color);
        draw_hline(fb, x0, x1, y1, color);
        draw_vline(fb, x0, y0, y1, color);
        draw_vline(fb, x1, y0, y1, color);
    }
}

static void clear_fb(uint16_t *fb, uint16_t color) {
    for (int i = 0; i < GC9A01_WIDTH * GC9A01_HEIGHT; i++) {
        fb[i] = color;
    }
}

int main(int argc, char **argv) {
    gc9a01_t lcd;
    uint16_t *fb = NULL;
    int ret = 1;

    printf("GC9A01 Demo for MilkV Duo\n");
    printf("Initializing...\n");

    if (!gc9a01_init(&lcd)) {
        fprintf(stderr, "Failed to initialize GC9A01\n");
        return 1;
    }

    fb = malloc(GC9A01_WIDTH * GC9A01_HEIGHT * sizeof(uint16_t));
    if (!fb) {
        fprintf(stderr, "Failed to allocate framebuffer\n");
        goto cleanup;
    }

    printf("Screen initialized. Running demo...\n");

    // Test 1: Clear to black
    clear_fb(fb, GC9A01_COLOR_BLACK);
    gc9a01_display_image(&lcd, fb);
    printf("1. Black screen\n");
    sleep(1);

    // Test 2: Fill red
    clear_fb(fb, GC9A01_COLOR_RED);
    gc9a01_display_image(&lcd, fb);
    printf("2. Red screen\n");
    sleep(1);

    // Test 3: Fill green
    clear_fb(fb, GC9A01_COLOR_GREEN);
    gc9a01_display_image(&lcd, fb);
    printf("3. Green screen\n");
    sleep(1);

    // Test 4: Fill blue
    clear_fb(fb, GC9A01_COLOR_BLUE);
    gc9a01_display_image(&lcd, fb);
    printf("4. Blue screen\n");
    sleep(1);

    // Test 5: Draw a circle
    clear_fb(fb, GC9A01_COLOR_BLACK);
    draw_circle(fb, GC9A01_WIDTH / 2, GC9A01_HEIGHT / 2, 80, GC9A01_COLOR_YELLOW, false);
    draw_circle(fb, GC9A01_WIDTH / 2, GC9A01_HEIGHT / 2, 60, GC9A01_COLOR_CYAN, true);
    gc9a01_display_image(&lcd, fb);
    printf("5. Circle test\n");
    sleep(2);

    // Test 6: Color bars
    clear_fb(fb, GC9A01_COLOR_BLACK);
    int bar_w = GC9A01_WIDTH / 4;
    draw_rect(fb, 0, 0, bar_w - 1, GC9A01_HEIGHT - 1, GC9A01_COLOR_RED, true);
    draw_rect(fb, bar_w, 0, bar_w * 2 - 1, GC9A01_HEIGHT - 1, GC9A01_COLOR_GREEN, true);
    draw_rect(fb, bar_w * 2, 0, bar_w * 3 - 1, GC9A01_HEIGHT - 1, GC9A01_COLOR_BLUE, true);
    draw_rect(fb, bar_w * 3, 0, GC9A01_WIDTH - 1, GC9A01_HEIGHT - 1, GC9A01_COLOR_WHITE, true);
    gc9a01_display_image(&lcd, fb);
    printf("6. Color bars\n");
    sleep(2);

    printf("Demo complete. Screen should be showing color bars.\n");
    ret = 0;

cleanup:
    free(fb);
    gc9a01_deinit(&lcd);
    return ret;
}
