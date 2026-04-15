#include "hal_ec11.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/queue.h"

#define EC11_CLK_GPIO       5
#define EC11_DT_GPIO        6
#define EC11_SW_GPIO        7

// 软件消抖时间
#define EC11_DEBOUNCE_US    5000
#define EC11_SW_DEBOUNCE_US 200000

static const char *TAG = "HAL_EC11";

static volatile int s_angle = 90;
static volatile int s_last_clk = 1;
static volatile int s_step = 2;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static volatile int64_t s_last_irq_time = 0;
static volatile int64_t s_last_sw_time = 0;

static QueueHandle_t s_event_queue = NULL;
static bool s_initialized = false;

typedef struct {
    hal_ec11_event_t event;
    int angle;
} ec11_evt_t;

static void IRAM_ATTR ec11_isr_handler(void *arg)
{
    int clk = gpio_get_level(EC11_CLK_GPIO);
    int64_t now = esp_timer_get_time();

    if (clk == 0 && s_last_clk == 1) {
        if (now - s_last_irq_time >= EC11_DEBOUNCE_US) {
            s_last_irq_time = now;

            int dt = gpio_get_level(EC11_DT_GPIO);
            hal_ec11_event_t evt;
            bool moved = false;

            portENTER_CRITICAL_ISR(&s_lock);
            if (dt == 1) {
                if (s_angle < 180) {
                    s_angle += s_step;
                    if (s_angle > 180) s_angle = 180;
                    evt = EC11_EVENT_ROTATE_CW;
                    moved = true;
                } else {
                    evt = EC11_EVENT_ROTATE_CW;
                }
            } else {
                if (s_angle > 0) {
                    s_angle -= s_step;
                    if (s_angle < 0) s_angle = 0;
                    evt = EC11_EVENT_ROTATE_CCW;
                    moved = true;
                } else {
                    evt = EC11_EVENT_ROTATE_CCW;
                }
            }
            portEXIT_CRITICAL_ISR(&s_lock);

            if (moved && s_event_queue != NULL) {
                ec11_evt_t ev = { .event = evt, .angle = s_angle };
                BaseType_t woken = pdFALSE;
                xQueueSendFromISR(s_event_queue, &ev, &woken);
                if (woken == pdTRUE) {
                    portYIELD_FROM_ISR();
                }
            }
        }
    }
    s_last_clk = clk;
}

static void IRAM_ATTR ec11_sw_isr_handler(void *arg)
{
    int level = gpio_get_level(EC11_SW_GPIO);
    int64_t now = esp_timer_get_time();

    if (level == 0 && (now - s_last_sw_time >= EC11_SW_DEBOUNCE_US)) {
        s_last_sw_time = now;

        portENTER_CRITICAL_ISR(&s_lock);
        s_angle = 90;
        portEXIT_CRITICAL_ISR(&s_lock);

        if (s_event_queue != NULL) {
            ec11_evt_t ev = { .event = EC11_EVENT_BTN_PRESSED, .angle = 90 };
            BaseType_t woken = pdFALSE;
            xQueueSendFromISR(s_event_queue, &ev, &woken);
            if (woken == pdTRUE) {
                portYIELD_FROM_ISR();
            }
        }
    }
}

hal_err_t hal_ec11_init(void)
{
    if (s_initialized) return HAL_OK;

    s_event_queue = xQueueCreate(8, sizeof(ec11_evt_t));
    if (s_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create event queue");
        return HAL_ERR;
    }

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

    s_angle = 90;
    s_last_clk = 1;
    s_step = 2;
    s_last_irq_time = 0;
    s_last_sw_time = 0;
    s_initialized = true;

    ESP_LOGI(TAG, "EC11 initialized: CLK=%d DT=%d SW=%d (debounce=%dus)",
             EC11_CLK_GPIO, EC11_DT_GPIO, EC11_SW_GPIO, EC11_DEBOUNCE_US);
    return HAL_OK;
}

hal_err_t hal_ec11_deinit(void)
{
    if (!s_initialized) return HAL_OK;
    gpio_isr_handler_remove(EC11_CLK_GPIO);
    gpio_isr_handler_remove(EC11_SW_GPIO);
    gpio_uninstall_isr_service();
    if (s_event_queue) {
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
    }
    s_initialized = false;
    return HAL_OK;
}

QueueHandle_t hal_ec11_get_queue(void)
{
    return s_event_queue;
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
