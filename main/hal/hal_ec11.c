/**
 * hal_ec11.c — EC11 编码器抽象层
 *
 * 当前实现拆成两条路径：
 *   1. 旋转：FreeRTOS 任务以 2ms 周期轮询 CLK/DT，避免把机械抖动和方向判断塞进 ISR。
 *   2. 按键：GPIO 下降沿中断 + 200ms 消抖，只负责广播点击事件。
 *
 * 这样做的目标是让 EC11 在快速旋转时也尽量稳定，同时避免 ISR 中出现阻塞调用。
 */

#include "hal_ec11.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "event_broker.h"
#include "app_config.h"

#define EC11_CLK_GPIO        5
#define EC11_DT_GPIO         6
#define EC11_SW_GPIO         7
#define EC11_POLL_MS         2
#define EC11_SW_DEBOUNCE_US 200000

static const char *TAG = "HAL_EC11";

static volatile int s_angle = 90;
static volatile int s_step = 2;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile int64_t s_last_sw_time = 0;
static volatile bool s_initialized = false;
static TaskHandle_t s_reader_task_handle = NULL;

// ================================================================
// 轮询任务：负责旋转检测，不在 ISR 中做方向判定。
// ================================================================

static void ec11_reader_task(void *arg)
{
    int last_clk = 1;
    int64_t last_tick = 0;

    while (1) {
        int clk = gpio_get_level(EC11_CLK_GPIO);
        int dt = gpio_get_level(EC11_DT_GPIO);
        int64_t now = esp_timer_get_time() / 1000;  // ms

        // 下降沿检测（clk 从 1 变 0）
        if (last_clk == 1 && clk == 0 && (now - last_tick) > 3) {
            last_tick = now;

            portENTER_CRITICAL(&s_lock);
            if (dt == 1) {
                // 顺时针
                if (s_angle < 180) {
                    s_angle += s_step;
                    if (s_angle > 180) s_angle = 180;
                }
            } else {
                // 逆时针
                if (s_angle > 0) {
                    s_angle -= s_step;
                    if (s_angle < 0) s_angle = 0;
                }
            }
            portEXIT_CRITICAL(&s_lock);

            // 在任务上下文中发布旋转事件。
            event_broker_publish(EVENT_TYPE_ENCODER_ROTATE, s_angle);
        }

        last_clk = clk;
        vTaskDelay(pdMS_TO_TICKS(EC11_POLL_MS));
    }
}

// ================================================================
// 按键中断 + 消抖：只发事件，不做耗时控制逻辑。
// ================================================================

static void IRAM_ATTR ec11_sw_isr_handler(void *arg)
{
    int level = gpio_get_level(EC11_SW_GPIO);
    int64_t now = esp_timer_get_time();

    if (level == 0 && (now - s_last_sw_time >= EC11_SW_DEBOUNCE_US)) {
        s_last_sw_time = now;

        portENTER_CRITICAL_ISR(&s_lock);
        s_angle = CFG_ENCODER_RESET;
        portEXIT_CRITICAL_ISR(&s_lock);

        event_broker_broadcast(EVENT_TYPE_ENCODER_CLICK, CFG_ENCODER_RESET);
    }
}

// ================================================================
// 公共 API
// ================================================================

hal_err_t hal_ec11_init(void)
{
    if (s_initialized) return HAL_OK;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << EC11_CLK_GPIO) | (1ULL << EC11_DT_GPIO) | (1ULL << EC11_SW_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // 按键使用中断
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_set_intr_type(EC11_SW_GPIO, GPIO_INTR_NEGEDGE));
    ESP_ERROR_CHECK(gpio_isr_handler_add(EC11_SW_GPIO, ec11_sw_isr_handler, NULL));

    s_angle = CFG_ENCODER_INITIAL;
    s_step = CFG_ENCODER_STEP_SIZE;
    s_last_sw_time = 0;
    s_initialized = true;

    // 启动轮询任务
    BaseType_t ok = xTaskCreatePinnedToCore(
        ec11_reader_task,
        "ec11_reader",
        2048,
        NULL,
        12,
        &s_reader_task_handle,
        0
    );
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create ec11_reader_task");
        return HAL_ERR;
    }

    ESP_LOGI(TAG, "EC11 initialized: CLK=GPIO%d DT=GPIO%d SW=GPIO%d step=%d",
             EC11_CLK_GPIO, EC11_DT_GPIO, EC11_SW_GPIO, s_step);
    return HAL_OK;
}

hal_err_t hal_ec11_deinit(void)
{
    if (!s_initialized) return HAL_OK;
    if (s_reader_task_handle) {
        vTaskDelete(s_reader_task_handle);
        s_reader_task_handle = NULL;
    }
    gpio_isr_handler_remove(EC11_SW_GPIO);
    gpio_uninstall_isr_service();
    s_initialized = false;
    return HAL_OK;
}

int hal_ec11_get_angle(void)
{
    int angle;
    portENTER_CRITICAL(&s_lock);
    angle = s_angle;
    portEXIT_CRITICAL(&s_lock);
    return angle;
}

void hal_ec11_set_angle(int angle)
{
    if (angle > 180) angle = 180;
    if (angle < 0) angle = 0;
    portENTER_CRITICAL(&s_lock);
    s_angle = angle;
    portEXIT_CRITICAL(&s_lock);
}

void hal_ec11_set_step(int step)
{
    if (step < 1) step = 1;
    portENTER_CRITICAL(&s_lock);
    s_step = step;
    portEXIT_CRITICAL(&s_lock);
}

QueueHandle_t hal_ec11_get_queue(void)
{
    return event_broker_get_queue();
}
