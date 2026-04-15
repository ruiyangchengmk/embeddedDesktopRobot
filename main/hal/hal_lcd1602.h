#ifndef HAL_LCD1602_H
#define HAL_LCD1602_H

#include "hal_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize LCD1602A via PCF8574T I2C adapter.
 *
 * I2C config: SDA=GPIO8, SCL=GPIO9, Address=0x27/0x3F auto-detect.
 *
 * @return HAL_OK on success.
 */
hal_err_t hal_lcd1602_init(void);

/**
 * @brief Clear LCD screen.
 *
 * @return HAL_OK on success.
 */
hal_err_t hal_lcd1602_clear(void);

/**
 * @brief Set cursor position.
 *
 * @param row Row index (0 or 1).
 * @param col Column index (0 ~ 15).
 * @return HAL_OK on success.
 */
hal_err_t hal_lcd1602_set_cursor(uint8_t row, uint8_t col);

/**
 * @brief Print a string at current cursor position.
 *
 * @param str Null-terminated string.
 * @return HAL_OK on success.
 */
hal_err_t hal_lcd1602_print(const char *str);

/**
 * @brief Print a formatted string (like printf) to LCD row 0.
 *
 * @param fmt Format string.
 * @param ... Variadic args.
 * @return HAL_OK on success.
 */
hal_err_t hal_lcd1602_printf_row0(const char *fmt, ...);

/**
 * @brief Print a formatted string (like printf) to LCD row 1.
 *
 * @param fmt Format string.
 * @param ... Variadic args.
 * @return HAL_OK on success.
 */
hal_err_t hal_lcd1602_printf_row1(const char *fmt, ...);

/**
 * @brief Turn backlight on/off.
 *
 * @param on true=on, false=off.
 * @return HAL_OK on success.
 */
hal_err_t hal_lcd1602_backlight(bool on);

#ifdef __cplusplus
}
#endif

#endif /* HAL_LCD1602_H */
