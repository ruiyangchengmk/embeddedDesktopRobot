#ifndef HAL_EC11_H
#define HAL_EC11_H

#include "hal_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EC11_EVENT_ROTATE_CW,   /*!< Clockwise rotation */
    EC11_EVENT_ROTATE_CCW,  /*!< Counter-clockwise rotation */
    EC11_EVENT_BTN_PRESSED, /*!< Button pressed */
} hal_ec11_event_t;

/**
 * @brief EC11 event callback prototype.
 *
 * @param event Event type.
 * @param current_angle Current accumulated angle (0 ~ 180 by default).
 * @param user_data User context passed during registration.
 */
typedef void (*hal_ec11_cb_t)(hal_ec11_event_t event, int current_angle, void *user_data);

/**
 * @brief Initialize EC11 rotary encoder GPIO and interrupts.
 *
 * Default GPIOs: CLK=5, DT=6, SW=7
 *
 * @return HAL_OK on success.
 */
hal_err_t hal_ec11_init(void);

/**
 * @brief Deinitialize EC11 resources.
 *
 * @return HAL_OK on success.
 */
hal_err_t hal_ec11_deinit(void);

/**
 * @brief Get the event queue handle for use with FreeRTOS xQueueReceive.
 *
 * Use this to receive EC11 events in a task context instead of callbacks.
 *
 * @return Queue handle.
 */
QueueHandle_t hal_ec11_get_queue(void);

/**
 * @brief Get current encoder angle.
 *
 * @return Current angle.
 */
int hal_ec11_get_angle(void);

/**
 * @brief Set encoder angle manually (useful for remote reset).
 *
 * @param angle Target angle.
 */
void hal_ec11_set_angle(int angle);

/**
 * @brief Set step size per encoder tick.
 *
 * Default is 2 degrees.
 *
 * @param step Step size.
 */
void hal_ec11_set_step(int step);

#ifdef __cplusplus
}
#endif

#endif /* HAL_EC11_H */
