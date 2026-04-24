/**
 * event_broker.c — 轻量级本地事件总线（替代 UDP 广播）
 *
 * 基于 FreeRTOS Queue 的发布-订阅模型。
 * 支持事件类型订阅，以及轻量节流避免抖动风暴。
 */

#include "freertos/FreeRTOS.h"
#include "event_broker.h"
#include "esp_log.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const char *TAG = "EBROKER";

// ================================================================
// 内部类型
// ================================================================

typedef struct {
    event_type_t type;
    QueueHandle_t queue;
} subscriber_t;

// ================================================================
// 全局状态
// ================================================================

#define MAX_SUBSCRIBERS 4

static subscriber_t s_subscribers[MAX_SUBSCRIBERS];
static int s_subscriber_count = 0;
static QueueHandle_t s_broker_queue = NULL;
static int64_t s_last_publish_us[16] = {0};  // 按 event_type 索引
static int64_t s_last_drop_log_us = 0;
static bool s_initialized = false;

// ================================================================
// 内部函数
// ================================================================

static bool should_throttle(event_type_t type)
{
    int64_t now = esp_timer_get_time();
    int idx = (int)type;
    if (idx < 0 || idx >= 16) return true;
    if (now - s_last_publish_us[idx] < 1000) {  // 1ms guard to filter extreme bounce
        return true;
    }
    s_last_publish_us[idx] = now;
    return false;
}

static void broadcast_event(broker_event_t *ev)
{
    for (int i = 0; i < s_subscriber_count; i++) {
        if (s_subscribers[i].type == ev->type || s_subscribers[i].type == EVENT_TYPE_ALL) {
            BaseType_t sent = xQueueSend(s_subscribers[i].queue, ev, 0);
            if (sent != pdTRUE) {
                int64_t now = esp_timer_get_time();
                if ((now - s_last_drop_log_us) >= 500000) {
                    ESP_LOGW(TAG, "Drop event type=%d for subscriber=%d", ev->type, i);
                    s_last_drop_log_us = now;
                }
            }
            (void)sent;
        }
    }
}

static void broadcast_event_from_isr(broker_event_t *ev, BaseType_t *woken)
{
    for (int i = 0; i < s_subscriber_count; i++) {
        if (s_subscribers[i].type == ev->type || s_subscribers[i].type == EVENT_TYPE_ALL) {
            BaseType_t sent = xQueueSendFromISR(s_subscribers[i].queue, ev, woken);
            (void)sent;
        }
    }
}

// ================================================================
// 公共 API
// ================================================================

hal_err_t event_broker_init(void)
{
    if (s_initialized) return HAL_OK;

    s_broker_queue = xQueueCreate(10, sizeof(broker_event_t));
    if (s_broker_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create broker queue");
        return HAL_ERR;
    }

    s_subscriber_count = 0;
    s_initialized = true;
    ESP_LOGI(TAG, "Event broker initialized");
    return HAL_OK;
}

hal_err_t event_broker_deinit(void)
{
    if (!s_initialized) return HAL_OK;
    if (s_broker_queue) {
        vQueueDelete(s_broker_queue);
        s_broker_queue = NULL;
    }
    s_subscriber_count = 0;
    s_initialized = false;
    return HAL_OK;
}

hal_err_t event_broker_subscribe(event_type_t type, QueueHandle_t queue)
{
    if (s_subscriber_count >= MAX_SUBSCRIBERS) return HAL_ERR_NO_MEM;
    if (!s_initialized) return HAL_ERR_NOT_INIT;

    s_subscribers[s_subscriber_count++] = (subscriber_t){ type, queue };
    ESP_LOGI(TAG, "Subscribed to event type=%d", type);
    return HAL_OK;
}

hal_err_t event_broker_publish(event_type_t type, int32_t value)
{
    if (!s_initialized) return HAL_ERR_NOT_INIT;

    if (should_throttle(type)) {
        return HAL_OK;  // 节流，不发布
    }

    broker_event_t ev = {
        .type = type,
        .value = value,
        .timestamp_us = esp_timer_get_time(),
    };

    broadcast_event(&ev);
    return HAL_OK;
}

QueueHandle_t event_broker_get_queue(void)
{
    return s_broker_queue;
}

hal_err_t event_broker_broadcast(event_type_t type, int32_t value)
{
    if (!s_initialized) return HAL_ERR_NOT_INIT;

    broker_event_t ev = {
        .type = type,
        .value = value,
        .timestamp_us = esp_timer_get_time(),
    };

    BaseType_t woken = pdFALSE;
    broadcast_event_from_isr(&ev, &woken);
    if (woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
    return HAL_OK;
}
