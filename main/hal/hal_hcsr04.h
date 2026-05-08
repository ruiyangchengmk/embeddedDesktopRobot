/**
 * hal_hcsr04.h — HC-SR04 超声波测距传感器抽象层
 *
 * GPIO 接线：
 *   HC-SR04 VCC  → 5V
 *   HC-SR04 GND  → GND
 *   HC-SR04 Trig → GPIO8
 *   HC-SR04 Echo → GPIO9
 */

#ifndef HAL_HCSR04_H
#define HAL_HCSR04_H

#include "hal_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize HC-SR04 GPIO and timer resources.
 *
 * @return HAL_OK on success.
 */
hal_err_t hal_hcsr04_init(void);

/**
 * @brief Deinitialize HC-SR04 resources.
 *
 * @return HAL_OK on success.
 */
hal_err_t hal_hcsr04_deinit(void);

/**
 * @brief Trigger a measurement and return the distance in centimeters.
 *
 * This is a blocking call (~20ms typical).
 *
 * @return Distance in cm (2~400), or -1 on timeout/error.
 */
float hal_hcsr04_get_distance_cm(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_HCSR04_H */
