/**
 * hal_ec11.c — EC11 编码器抽象层
 *
 * 旋转：GPIO 下降沿中断 + 软解码 + 消抖
 * 按键：GPIO 中断 + 200ms 消抖
 * 事件：通过 event_broker 发布
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

#define EC11_CLK_GPIO      6
#define EC11_DT_GPIO       7
#define EC11_SW_GPIO       8
#define EC11_DEBOUNCE_US   5000
#define EC11_SW_DEBOUNCE_US 200000

static const char *TAG = "HAL_EC11";

static volatile int s_angle = 90;
static volatile int s_last_clk = 1;
static volatile int s_step = 2;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile int64_t s_last_irq_time = 0;
static volatile int64_t s_last_sw_time = 0;
static volatile bool s_initialized = false;

// ================================================================
// GPIO 中断处理 — 旋转解码
// ================================================================

static void IRAM_ATTR ec11_isr_handler(void *arg)
{
    int clk = gpio_get_level(EC11_CLK_GPIO);
    int64_t now = esp_timer_get_time();

    if (clk == 0 && s_last_clk == 1) {
        if (now - s_last_irq_time < EC11_DEBOUNCE_US) {
            s_last_clk = clk;
            return;
        }
        s_last_irq_time = now;

        int dt = gpio_get_level(EC11_DT_GPIO);
        bool moved = false;

        portENTER_CRITICAL_ISR(&s_lock);
        if (dt == 1) {
            if (s_angle < 180) {
                s_angle += s_step;
                if (s_angle > 180) s_angle = 180;
                moved = true;
            }
        } else {
            if (s_angle > 0) {
                s_angle -= s_step;
                if (s_angle < 0) s_angle = 0;
                moved = true;
            }
        }
        portEXIT_CRITICAL_ISR(&s_lock);

        if (moved) {
            event_broker_broadcast(EVENT_TYPE_ENCODER_ROTATE, s_angle);
        }
    }
    s_last_clk = clk;
}

// ================================================================
// GPIO 中断处理 — 按键
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

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_set_intr_type(EC11_CLK_GPIO, GPIO_INTR_NEGEDGE));
    ESP_ERROR_CHECK(gpio_isr_handler_add(EC11_CLK_GPIO, ec11_isr_handler, NULL));
    ESP_ERROR_CHECK(gpio_set_intr_type(EC11_SW_GPIO, GPIO_INTR_NEGEDGE));
    ESP_ERROR_CHECK(gpio_isr_handler_add(EC11_SW_GPIO, ec11_sw_isr_handler, NULL));

    s_angle = CFG_ENCODER_INITIAL;
    s_last_clk = 1;
    s_step = CFG_ENCODER_STEP_SIZE;
    s_last_irq_time = 0;
    s_last_sw_time = 0;
    s_initialized = true;

    ESP_LOGI(TAG, "EC11 initialized: CLK=%d DT=%d SW=%d step=%d",
             EC11_CLK_GPIO, EC11_DT_GPIO, EC11_SW_GPIO, s_step);
    return HAL_OK;
}

hal_err_t hal_ec11_deinit(void)
{
    if (!s_initialized) return HAL_OK;
    gpio_isr_handler_remove(EC11_CLK_GPIO);
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
