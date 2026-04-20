#ifndef HAL_GC9A01_H
#define HAL_GC9A01_H

#include "hal_common.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GC9A01_RESX_GPIO    2
#define GC9A01_DCX_GPIO     1
#define GC9A01_SDA_GPIO    11
#define GC9A01_SCL_GPIO    12
#define GC9A01_CS_GPIO     10
#define GC9A01_SPI_HOST    SPI2_HOST

#define GC9A01_WIDTH   240
#define GC9A01_HEIGHT  240

// Tunable panel settings for bring-up/debugging.
#define GC9A01_MADCTL_VALUE       0x00
#define GC9A01_COLMOD_VALUE       0x05
#define GC9A01_TE_ENABLE          0
#define GC9A01_TE_MODE            0x00
#define GC9A01_COLOR_INVERSION    1

hal_err_t hal_gc9a01_init(void);

// Direct SPI test (bypasses esp_lcd framework)
hal_err_t hal_gc9a01_spi_test(void);

/**
 * @brief Draw a rectangular bitmap on the GC9A01 display.
 * @param x_start  left column (inclusive)
 * @param y_start  top row (inclusive)
 * @param x_end    right column (exclusive)
 * @param y_end    bottom row (exclusive)
 * @param color_data  RGB565 pixel data (2 bytes per pixel)
 */
hal_err_t hal_gc9a01_draw_bitmap(int x_start, int y_start, int x_end, int y_end,
                                  const void *color_data);

#ifdef __cplusplus
}
#endif

#endif /* HAL_GC9A01_H */
