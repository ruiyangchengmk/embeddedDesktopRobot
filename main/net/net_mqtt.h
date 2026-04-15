#ifndef NET_MQTT_H
#define NET_MQTT_H

#include "hal/hal_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback prototype for incoming MQTT commands.
 *
 * @param angle Target servo angle parsed from MQTT payload.
 */
typedef void (*net_mqtt_cmd_cb_t)(int angle);

/**
 * @brief Initialize MQTT client and connect to broker.
 *
 * @return HAL_OK on success.
 */
hal_err_t net_mqtt_init(void);

/**
 * @brief Register command callback.
 *
 * @param cb Callback function.
 * @return HAL_OK on success.
 */
hal_err_t net_mqtt_register_cmd_callback(net_mqtt_cmd_cb_t cb);

/**
 * @brief Publish sensor data to MQTT broker.
 *
 * @param angle Current servo angle.
 * @param r Red component.
 * @param g Green component.
 * @param b Blue component.
 * @return HAL_OK on success.
 */
hal_err_t net_mqtt_publish_sensor(int angle, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Publish heartbeat message.
 *
 * @return HAL_OK on success.
 */
hal_err_t net_mqtt_publish_heartbeat(void);

#ifdef __cplusplus
}
#endif

#endif /* NET_MQTT_H */
