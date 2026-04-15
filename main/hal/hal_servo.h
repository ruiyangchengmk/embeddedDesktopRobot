#ifndef HAL_SERVO_H
#define HAL_SERVO_H

#include "hal_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize SG90 servo via LEDC PWM.
 *
 * Default GPIO: 4
 *
 * @return HAL_OK on success.
 */
hal_err_t hal_servo_init(void);

/**
 * @brief Deinitialize servo resources.
 *
 * @return HAL_OK on success.
 */
hal_err_t hal_servo_deinit(void);

/**
 * @brief Set servo angle (0 ~ 180 degrees).
 *
 * @param angle Target angle.
 * @return HAL_OK on success.
 */
hal_err_t hal_servo_set_angle(int angle);

/**
 * @brief Get last set servo angle.
 *
 * @return Current angle in degrees.
 */
int hal_servo_get_angle(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SERVO_H */
