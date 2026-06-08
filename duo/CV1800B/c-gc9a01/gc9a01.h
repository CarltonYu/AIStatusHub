#ifndef GC9A01_H
#define GC9A01_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define GC9A01_WIDTH  240
#define GC9A01_HEIGHT 240

// User's actual wiring on MilkV Duo
// Pin 5=GP3(DC), Pin 9=GP4(SCK), Pin 10=GP5(MOSI), Pin 11=GP7(RST), Pin 12=GP8(CS)
// sysfs GPIO numbers = 480 + GPx on Duo
#define GC9A01_PIN_DC   483
#define GC9A01_PIN_SCK  484   // SCL on screen
#define GC9A01_PIN_MOSI 485   // SDA on screen
#define GC9A01_PIN_CS   488
#define GC9A01_PIN_RST  487

// Color macros (RGB565)
#define GC9A01_COLOR_BLACK   0x0000
#define GC9A01_COLOR_WHITE   0xFFFF
#define GC9A01_COLOR_RED     0xF800
#define GC9A01_COLOR_GREEN   0x07E0
#define GC9A01_COLOR_BLUE    0x001F
#define GC9A01_COLOR_YELLOW  0xFFE0
#define GC9A01_COLOR_CYAN    0x07FF
#define GC9A01_COLOR_MAGENTA 0xF81F

typedef struct {
    int fd_dc;
    int fd_rst;
    int fd_cs;
    int fd_sck;
    int fd_mosi;
} gc9a01_t;

// Initialize GPIOs (software SPI)
bool gc9a01_init(gc9a01_t *dev);

// Shutdown and release resources
void gc9a01_deinit(gc9a01_t *dev);

// Reset the display
void gc9a01_reset(gc9a01_t *dev);

// Send command (with optional data)
void gc9a01_write_cmd(gc9a01_t *dev, uint8_t cmd, const uint8_t *data, size_t len);

// Set display window
void gc9a01_set_window(gc9a01_t *dev, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

// Write RGB565 pixel data to the current window
void gc9a01_write_pixels(gc9a01_t *dev, const uint16_t *rgb565_data, size_t count);

// Full display update from a 240x240 RGB565 buffer
void gc9a01_display_image(gc9a01_t *dev, const uint16_t *rgb565_buf);

// Fill screen with a single color
void gc9a01_fill_screen(gc9a01_t *dev, uint16_t color);

// Clear screen (black)
static inline void gc9a01_clear(gc9a01_t *dev) {
    gc9a01_fill_screen(dev, GC9A01_COLOR_BLACK);
}

#endif // GC9A01_H
