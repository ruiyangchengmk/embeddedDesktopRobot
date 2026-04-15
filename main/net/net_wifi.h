#ifndef NET_WIFI_H
#define NET_WIFI_H

#include "hal/hal_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize WiFi in Station mode.
 *
 * @return HAL_OK on success.
 */
hal_err_t net_wifi_init(void);

/**
 * @brief Block until WiFi is connected and IP is obtained.
 *
 * @param timeout_ms Max wait time in milliseconds.
 * @return HAL_OK if connected within timeout.
 */
hal_err_t net_wifi_wait_connected(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* NET_WIFI_H */
