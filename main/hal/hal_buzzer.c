/**
 * hal_buzzer.c — 无源蜂鸣器驱动 (GPIO3)
 *
 * 硬件：红线→GPIO3，黑线→GND
 * 驱动：LEDC_TIMER_1 + LEDC_CHANNEL_1（独立于舵机/LEDC_TIMER_0）
 * 控制：EC11 按键触发 → 队列消息 → buzzer_task 非阻塞播放旋律
 *
 * 旋律：H-E-L-L-O [space] W-O-R-L-D（音符频率）
 * H=494Hz E=330Hz L=392Hz O=523Hz W=392Hz R=392Hz D=294Hz
 */

#include "hal_buzzer.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define BUZZER_GPIO     3
#define BUZZER_TIMER    LEDC_TIMER_1
#define BUZZER_MODE     LEDC_LOW_SPEED_MODE
#define BUZZER_CHANNEL  LEDC_CHANNEL_1
#define BUZZER_DUTY_RES LEDC_TIMER_8_BIT

static const char *TAG = "HAL_BUZZER";
static bool s_initialized = false;
static TaskHandle_t s_buzzer_task_handle = NULL;
static QueueHandle_t s_buzzer_queue = NULL;

typedef enum {
    BUZZER_CMD_NONE,
    BUZZER_CMD_PLAY_HELLO,
} buzzer_cmd_t;

static void buzzer_task(void *arg);

hal_buzzer_err_t hal_buzzer_init(void)
{
    if (s_initialized) {
        return HAL_BUZZER_OK;
    }

    ledc_timer_config_t timer_conf = {
        .speed_mode = BUZZER_MODE,
        .duty_resolution = BUZZER_DUTY_RES,
        .timer_num = BUZZER_TIMER,
        .freq_hz = 440,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    ledc_channel_config_t channel_conf = {
        .gpio_num = BUZZER_GPIO,
        .speed_mode = BUZZER_MODE,
        .channel = BUZZER_CHANNEL,
        .timer_sel = BUZZER_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_conf));

    s_buzzer_queue = xQueueCreate(1, sizeof(buzzer_cmd_t));
    if (!s_buzzer_queue) {
        ESP_LOGE(TAG, "Queue create failed");
        return HAL_BUZZER_ERR;
    }

    BaseType_t r = xTaskCreatePinnedToCore(
        buzzer_task, "buzzer", 4096, NULL, 5, &s_buzzer_task_handle, 0);

    if (r != pdPASS) {
        ESP_LOGE(TAG, "Task create failed");
        return HAL_BUZZER_ERR;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Buzzer initialized on GPIO%d", BUZZER_GPIO);
    return HAL_BUZZER_OK;
}

hal_buzzer_err_t hal_buzzer_deinit(void)
{
    if (!s_initialized) {
        return HAL_BUZZER_OK;
    }
    if (s_buzzer_task_handle) {
        vTaskDelete(s_buzzer_task_handle);
        s_buzzer_task_handle = NULL;
    }
    if (s_buzzer_queue) {
        vQueueDelete(s_buzzer_queue);
        s_buzzer_queue = NULL;
    }
    ledc_stop(BUZZER_MODE, BUZZER_CHANNEL, 0);
    s_initialized = false;
    return HAL_BUZZER_OK;
}

hal_buzzer_err_t hal_buzzer_stop(void)
{
    if (!s_initialized) {
        return HAL_BUZZER_ERR_NOT_INIT;
    }
    ESP_ERROR_CHECK(ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, 0));
    ESP_ERROR_CHECK(ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL));
    return HAL_BUZZER_OK;
}

hal_buzzer_err_t hal_buzzer_play_hello_world(void)
{
    if (!s_initialized) {
        return HAL_BUZZER_ERR_NOT_INIT;
    }
    buzzer_cmd_t cmd = BUZZER_CMD_PLAY_HELLO;
    xQueueSend(s_buzzer_queue, &cmd, 0);
    return HAL_BUZZER_OK;
}

static void buzzer_task(void *arg)
{
    (void)arg;
    buzzer_cmd_t cmd = BUZZER_CMD_NONE;

    while (1) {
        if (xQueueReceive(s_buzzer_queue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (cmd == BUZZER_CMD_PLAY_HELLO) {
            // H=494, E=330, L=392, L=392, O=523, (sp)=0, W=392, O=523, R=392, L=392, D=294
            uint32_t notes[][2] = {
                { 494, 250 }, { 330, 250 }, { 392, 250 }, { 392, 250 },
                { 523, 250 }, {   0, 100 }, { 392, 250 }, { 523, 250 },
                { 392, 250 }, { 392, 250 }, { 294, 500 },
            };
            ESP_LOGI(TAG, "Playing hello world...");
            for (size_t i = 0; i < sizeof(notes) / sizeof(notes[0]); i++) {
                uint32_t freq = notes[i][0];
                uint32_t ms = notes[i][1];
                if (freq == 0) {
                    ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, 0);
                } else {
                    ledc_set_freq(BUZZER_MODE, BUZZER_TIMER, freq);
                    ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, 128);
                }
                ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL);
                vTaskDelay(pdMS_TO_TICKS(ms));
            }
            ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, 0);
            ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL);
            ESP_LOGI(TAG, "Done!");
        }
    }
}
