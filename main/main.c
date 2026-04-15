#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "freertos/queue.h"

#include "hal/hal_servo.h"
#include "hal/hal_rgb.h"
#include "hal/hal_ec11.h"
#include "hal/hal_lcd1602.h"

#include "app_config.h"

static const char *TAG = "APP";

typedef struct {
    hal_ec11_event_t event;
    int angle;
} ec11_evt_t;

// ---- RGB keyframe interpolation ----
static void color_from_angle(int angle, uint8_t *r, uint8_t *g, uint8_t *b)
{
    const rgb_keyframe_t *kf = s_rgb_keyframes;
    int count = CFG_RGB_KEYFRAME_COUNT;

    if (angle <= kf[0].angle) {
        *r = kf[0].r; *g = kf[0].g; *b = kf[0].b;
        return;
    }
    if (angle >= kf[count - 1].angle) {
        *r = kf[count - 1].r; *g = kf[count - 1].g; *b = kf[count - 1].b;
        return;
    }

    for (int i = 0; i < count - 1; i++) {
        if (angle >= kf[i].angle && angle <= kf[i + 1].angle) {
            int range = kf[i + 1].angle - kf[i].angle;
            int delta = angle - kf[i].angle;
            float t = (float)delta / range;
            *r = (uint8_t)(kf[i].r + t * (kf[i + 1].r - kf[i].r));
            *g = (uint8_t)(kf[i].g + t * (kf[i + 1].g - kf[i].g));
            *b = (uint8_t)(kf[i].b + t * (kf[i + 1].b - kf[i].b));
            return;
        }
    }
}

static void ec11_task(void *arg)
{
    QueueHandle_t q = hal_ec11_get_queue();

    while (1) {
        ec11_evt_t ev;
        if (xQueueReceive(q, &ev, portMAX_DELAY) == pdTRUE) {
            int ec11_angle = ev.angle;

            if (ev.event == EC11_EVENT_BTN_PRESSED) {
                // 复位到初始角度
                hal_ec11_set_angle(CFG_ENCODER_RESET);
                hal_servo_set_angle(CFG_SERVO_INITIAL);
                uint8_t r, g, b;
                color_from_angle(CFG_SERVO_INITIAL, &r, &g, &b);
                hal_rgb_set_color(r, g, b);
                ESP_LOGI(TAG, "BTN: reset to %d deg", CFG_SERVO_INITIAL);
            } else {
                // 角度映射：EC11 -> Servo
                int servo_angle = CFG_EC11_TO_SERVO(ec11_angle);
                hal_servo_set_angle(servo_angle);

                // 颜色映射：Angle -> RGB
                uint8_t r, g, b;
                color_from_angle(servo_angle, &r, &g, &b);
                hal_rgb_set_color(r, g, b);

                ESP_LOGI(TAG, "Angle: ec11=%d servo=%d RGB(%u,%u,%u)",
                         ec11_angle, servo_angle, r, g, b);
            }
        }
    }
}

// ---- LCD 运行时显示 ----
static void update_lcd_runtime(int ec11_angle, int servo_angle, uint8_t r, uint8_t g, uint8_t b)
{
    hal_lcd1602_set_cursor(0, 0);
#if CFG_DISPLAY_MODE == 1
    hal_lcd1602_printf_row0("Angle: %3d deg", servo_angle);
    hal_lcd1602_printf_row1("R:%3u G:%3u B:%3u", r, g, b);
#elif CFG_DISPLAY_MODE == 2
    hal_lcd1602_printf_row0("Angle: %3d deg", servo_angle);
    hal_lcd1602_printf_row1("%s", CFG_DISPLAY_CUSTOM_TEXT);
#elif CFG_DISPLAY_MODE == 3
    hal_lcd1602_printf_row0("Angle: %3d deg", servo_angle);
    hal_lcd1602_set_cursor(1, 0);
    hal_lcd1602_print("                ");  // 16 spaces to blank row 1
#elif CFG_DISPLAY_MODE == 4
    hal_lcd1602_printf_row0("%s", CFG_DISPLAY_CUSTOM_TEXT);
    hal_lcd1602_printf_row1("Angle: %3d deg", servo_angle);
#endif
}

void app_main(void)
{
    ESP_ERROR_CHECK(hal_servo_init() == HAL_OK ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(hal_rgb_init() == HAL_OK ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(hal_ec11_init() == HAL_OK ? ESP_OK : ESP_FAIL);
    hal_ec11_set_step(CFG_ENCODER_STEP_SIZE);

    // 初始同步：舵机角度、RGB 颜色、EC11 逻辑角度
    hal_ec11_set_angle(CFG_ENCODER_INITIAL);
    hal_servo_set_angle(CFG_SERVO_INITIAL);
    hal_rgb_set_color(CFG_RGB_INITIAL_R, CFG_RGB_INITIAL_G, CFG_RGB_INITIAL_B);

    // 启动 EC11 事件处理任务
    xTaskCreatePinnedToCore(ec11_task, "ec11", 4096, NULL, 10, NULL, 0);

    hal_err_t lcd_err = hal_lcd1602_init();
    if (lcd_err == HAL_OK) {
        hal_lcd1602_backlight(true);
        hal_lcd1602_clear();
        hal_lcd1602_printf_row0(CFG_DISPLAY_STARTUP_ROW0);
        hal_lcd1602_printf_row1(CFG_DISPLAY_STARTUP_ROW1);
        vTaskDelay(pdMS_TO_TICKS(2000));
    } else {
        ESP_LOGE(TAG, "LCD init failed, continuing without LCD");
    }

    ESP_LOGI(TAG, "System initialized. Servo range: [%d, %d] from EC11 [%d, %d]",
             CFG_SERVO_SMIN, CFG_SERVO_SMAX, CFG_SERVO_EMIN, CFG_SERVO_EMAX);

    // 主循环：检测 EC11 角度变化，刷新 LCD
    int last_angle = CFG_ENCODER_INITIAL;
    while (1) {
        int ec11_angle = hal_ec11_get_angle();
        if (ec11_angle != last_angle) {
            last_angle = ec11_angle;
            if (lcd_err == HAL_OK) {
                int servo_angle = CFG_EC11_TO_SERVO(ec11_angle);
                uint8_t r, g, b;
                color_from_angle(servo_angle, &r, &g, &b);
                update_lcd_runtime(ec11_angle, servo_angle, r, g, b);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
