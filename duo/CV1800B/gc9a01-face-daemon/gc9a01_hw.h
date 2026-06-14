#ifndef GC9A01_HW_H
#define GC9A01_HW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GC9A01_WIDTH  240
#define GC9A01_HEIGHT 240
#define GC9A01_PIXELS (GC9A01_WIDTH * GC9A01_HEIGHT)
#define GC9A01_BUF_BYTES (GC9A01_PIXELS * 2)

/* GC9A01 hardware context. */
struct gc9a01_hw {
    int spi_fd;
    int dc_fd;
    int rst_fd;
    uint32_t speed_hz;
    size_t chunk_size;
};

/* Open SPI device, configure GPIO DC/RST, reset and initialize the panel.
 * spi_path: e.g. "/dev/spidev0.1"
 * pin_dc / pin_rst: sysfs GPIO numbers, or -1 to auto-detect.
 * speed_hz: SPI max speed (Hz). 0 selects a safe default.
 */
int gc9a01_hw_init(struct gc9a01_hw *hw, const char *spi_path,
                   int pin_dc, int pin_rst, uint32_t speed_hz);

/* Release SPI/GPIO resources. */
void gc9a01_hw_deinit(struct gc9a01_hw *hw);

/* Reset the panel. */
void gc9a01_hw_reset(struct gc9a01_hw *hw);

/* Flush a full 240x240 RGB565 framebuffer to the display.
 * The buffer must be GC9A01_BUF_BYTES bytes.
 */
int gc9a01_hw_flush(struct gc9a01_hw *hw, const uint8_t *rgb565_buf);

/* Fill the entire screen with one RGB565 color. */
int gc9a01_hw_fill(struct gc9a01_hw *hw, uint16_t color);

/* Try to detect the sysfs GPIO number for PWR_GPIO offset.
 * Returns -1 if detection fails.
 */
int gc9a01_detect_gpio(int offset);

#endif /* GC9A01_HW_H */
