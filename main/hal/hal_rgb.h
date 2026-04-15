#ifndef HAL_RGB_H
#define HAL_RGB_H

#include "hal_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize on-board WS2812 RGB LED via RMT.
 *
 * Default GPIO: 48 (ESP32-S3-DevKitC-1)
 *
 * @return HAL_OK on success.
 */
hal_err_t hal_rgb_init(void);

/**
 * @brief Deinitialize RGB resources.
 *
 * @return HAL_OK on success.
 */
hal_err_t hal_rgb_deinit(void);

/**
 * @brief Set RGB color.
 *
 * @param r Red (0-255)
 * @param g Green (0-255)
 * @param b Blue (0-255)
 * @return HAL_OK on success.
 */
hal_err_t hal_rgb_set_color(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Set RGB color via struct.
 *
 * @param color Color structure.
 * @return HAL_OK on success.
 */
hal_err_t hal_rgb_set_color_by_struct(hal_rgb_color_t color);

#ifdef __cplusplus
}
#endif

#endif /* HAL_RGB_H */
