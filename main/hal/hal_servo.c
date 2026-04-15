#include "hal_servo.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_check.h"

#define SERVO_GPIO          4
#define SERVO_LEDC_TIMER    LEDC_TIMER_0
#define SERVO_LEDC_MODE     LEDC_LOW_SPEED_MODE
#define SERVO_LEDC_CHANNEL  LEDC_CHANNEL_0
#define SERVO_LEDC_DUTY_RES LEDC_TIMER_14_BIT
#define SERVO_FREQUENCY     50
#define SERVO_DUTY_MAX      ((1 << 14) - 1)

static const char *TAG = "HAL_SERVO";
static int s_current_angle = 90;
static bool s_initialized = false;

hal_err_t hal_servo_init(void)
{
    if (s_initialized) {
        return HAL_OK;
    }

    ledc_timer_config_t timer_conf = {
        .speed_mode       = SERVO_LEDC_MODE,
        .duty_resolution  = SERVO_LEDC_DUTY_RES,
        .timer_num        = SERVO_LEDC_TIMER,
        .freq_hz          = SERVO_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    ledc_channel_config_t channel_conf = {
        .gpio_num       = SERVO_GPIO,
        .speed_mode     = SERVO_LEDC_MODE,
        .channel        = SERVO_LEDC_CHANNEL,
        .timer_sel      = SERVO_LEDC_TIMER,
        .duty           = 0,
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_conf));

    s_current_angle = 90;
    hal_servo_set_angle(s_current_angle);
    s_initialized = true;

    ESP_LOGI(TAG, "Servo initialized on GPIO%d", SERVO_GPIO);
    return HAL_OK;
}

hal_err_t hal_servo_deinit(void)
{
    if (!s_initialized) {
        return HAL_OK;
    }
    ledc_stop(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL, 0);
    s_initialized = false;
    return HAL_OK;
}

hal_err_t hal_servo_set_angle(int angle)
{
    if (!s_initialized) {
        return HAL_ERR_NOT_INIT;
    }
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;

    // pulse_width = 0.5ms + (angle / 180.0) * 2.0ms
    uint32_t duty = (uint32_t)(((0.5 + (angle / 180.0) * 2.0) / 20.0) * SERVO_DUTY_MAX);
    ESP_ERROR_CHECK(ledc_set_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL));

    s_current_angle = angle;
    return HAL_OK;
}

int hal_servo_get_angle(void)
{
    return s_current_angle;
}
