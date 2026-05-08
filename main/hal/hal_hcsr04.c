/**
 * hal_hcsr04.c — HC-SR04 超声波测距传感器驱动
 *
 * 测量原理：
 *   1. Trig 发送 10μs 脉冲
 *   2. Echo 收到反射波后变为高电平，宽度 = 往返时间
 *   3. distance_cm = echo_time_us × 0.01715（声速 343m/s）
 *
 * GPIO：Trig=GPIO8, Echo=GPIO9
 * 测量范围：2cm ~ 400cm
 * 超时：~25ms（约 430cm 上限）
 */

#include <stdbool.h>
#include "hal_hcsr04.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "rom/ets_sys.h"

#define HCSR04_TRIG_GPIO   8
#define HCSR04_ECHO_GPIO   9
#define HCSR04_MAX_ITER    30000   /* ~30ms 超时，对应 ~500cm */
#define HCSR04_SPEED_CM_US 0.01715 /* 声速 343m/s / 2 */

static const char *TAG = "HAL_HCSR04";

static volatile bool s_initialized = false;

hal_err_t hal_hcsr04_init(void)
{
    if (s_initialized) return HAL_OK;

    gpio_config_t trig_conf = {
        .pin_bit_mask = 1ULL << HCSR04_TRIG_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&trig_conf));
    gpio_set_level(HCSR04_TRIG_GPIO, 0);

    gpio_config_t echo_conf = {
        .pin_bit_mask = 1ULL << HCSR04_ECHO_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&echo_conf));

    s_initialized = true;
    ESP_LOGI(TAG, "HC-SR04 initialized: Trig=GPIO%d Echo=GPIO%d", HCSR04_TRIG_GPIO, HCSR04_ECHO_GPIO);
    return HAL_OK;
}

hal_err_t hal_hcsr04_deinit(void)
{
    if (!s_initialized) return HAL_OK;
    s_initialized = false;
    return HAL_OK;
}

float hal_hcsr04_get_distance_cm(void)
{
    if (!s_initialized) return -1;

    /* 1. 发送 Trig 脉冲 */
    gpio_set_level(HCSR04_TRIG_GPIO, 1);
    ets_delay_us(10);
    gpio_set_level(HCSR04_TRIG_GPIO, 0);

    /* 2. 等待 Echo 上升沿（最多 ~30ms） */
    uint32_t iter = 0;
    while (gpio_get_level(HCSR04_ECHO_GPIO) == 0) {
        if (++iter > HCSR04_MAX_ITER) return -1;
        ets_delay_us(1);
    }

    /* 3. 等待 Echo 下降沿 */
    iter = 0;
    while (gpio_get_level(HCSR04_ECHO_GPIO) == 1) {
        if (++iter > HCSR04_MAX_ITER) {
            ets_delay_us(1000);
            return -1;
        }
        ets_delay_us(1);
    }

    /* 4. 计算距离 */
    float dist = iter * HCSR04_SPEED_CM_US;
    if (dist < 2.0f || dist > 400.0f) return -1;
    return dist;
}
