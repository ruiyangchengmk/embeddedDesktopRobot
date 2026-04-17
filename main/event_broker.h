#ifndef EVENT_BROKER_H
#define EVENT_BROKER_H

#include "hal/hal_common.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// ================================================================
// 事件类型
// ================================================================

typedef enum {
    EVENT_TYPE_ENCODER_ROTATE  = 0,
    EVENT_TYPE_ENCODER_CLICK   = 1,
    EVENT_TYPE_MAX             = 16,
    EVENT_TYPE_ALL             = 31,  // 订阅所有类型
} event_type_t;

// ================================================================
// 事件结构体（公开给所有订阅者）
// ================================================================

typedef struct {
    event_type_t type;
    int32_t value;
    int64_t timestamp_us;
} broker_event_t;

// ================================================================
// 公共 API
// ================================================================

/**
 * @brief 初始化事件总线。
 */
hal_err_t event_broker_init(void);

/**
 * @brief 关闭事件总线。
 */
hal_err_t event_broker_deinit(void);

/**
 * @brief 订阅事件类型。
 *
 * @param type   事件类型（EVENT_TYPE_ENCODER_ROTATE / EVENT_TYPE_ENCODER_CLICK）
 * @param queue  接收事件的 FreeRTOS 队列句柄
 * @return HAL_OK on success.
 */
hal_err_t event_broker_subscribe(event_type_t type, QueueHandle_t queue);

/**
 * @brief 发布事件（受 100Hz 节流限制）。
 *
 * @param type   事件类型
 * @param value  事件值（如编码器差值或角度）
 * @return HAL_OK on success.
 */
hal_err_t event_broker_publish(event_type_t type, int32_t value);

/**
 * @brief 强制广播事件（无节流，用于按键等瞬时事件）。
 */
hal_err_t event_broker_broadcast(event_type_t type, int32_t value);

/**
 * @brief 获取事件总线的主队列句柄（用于统一接收所有事件）。
 */
QueueHandle_t event_broker_get_queue(void);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_BROKER_H */
