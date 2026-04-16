/**
 * main.c — ESP32-S3 端侧控制节点
 *
 * 架构：输入（EC11）广播 → 各消费者独立任务订阅
 * 原则：单一硬件变更不影响其他硬件；分层架构最小修改。
 *
 * 任务优先级（从高到低）：
 *   ec11_reader  20  — 最高：只管读取 EC11 角度，实时广播
 *   servo_task   15  — 舵机：消费角度，做平滑 lerp
 *   rgb_task     14  — RGB：消费角度，实时更新颜色
 *   lvgl_task     5  — 显示：消费角度，驱动 GC9A01 方块旋转
 *   lcd_task      3  — LCD1602：定期刷新显示（慢，不影响其他硬件）
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "freertos/queue.h"

#include "hal/hal_servo.h"
#include "hal/hal_rgb.h"
#include "hal/hal_ec11.h"
#include "hal/hal_lcd1602.h"
#include "hal/hal_gc9a01.h"

#include "lvgl.h"
#include "app_config.h"

static const char *TAG = "APP";

// ================================================================
// 全局广播队列（ec11_reader → 所有消费者）
// ================================================================
static QueueHandle_t s_angle_queue = NULL;

// 队列接收用的本地结构体（与 hal_ec11.c 内部的 ec11_evt_t 布局一致）
typedef struct {
    hal_ec11_event_t event;
    int angle;
} ec11_evt_t;

// 广播消息格式
typedef struct {
    int angle;                        // EC11 当前角度
    int servo_target;                 // 映射后的舵机目标角度
    uint8_t r, g, b;                // 对应 RGB 颜色
    int button;                      // 1=按键按下，0=正常旋转
} angle_broadcast_t;

// ================================================================
// 辅助函数
// ================================================================
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

// ================================================================
// 最高优先级任务：只读 EC11 角度，立即广播
// ================================================================
static void ec11_reader_task(void *arg)
{
    QueueHandle_t q = hal_ec11_get_queue();

    while (1) {
        ec11_evt_t ev;
        BaseType_t ok = xQueueReceive(q, &ev, portMAX_DELAY);
        if (ok != pdTRUE) continue;

        angle_broadcast_t msg = {
            .angle = ev.angle,
            .button = (ev.event == EC11_EVENT_BTN_PRESSED) ? 1 : 0,
        };

        if (msg.button) {
            hal_ec11_set_angle(CFG_ENCODER_RESET);
            msg.angle = CFG_ENCODER_RESET;
        }

        msg.servo_target = CFG_EC11_TO_SERVO(msg.angle);
        color_from_angle(msg.servo_target, &msg.r, &msg.g, &msg.b);

        // 广播给所有消费者（非阻塞，队列满则丢弃旧消息）
        BaseType_t sent = xQueueSend(s_angle_queue, &msg, 0);
        (void)sent;
    }
}

// ================================================================
// 消费者1：舵机平滑 lerp（独立任务，不被 SPI 阻塞）
// ================================================================
static void servo_task(void *arg)
{
    int target = CFG_SERVO_INITIAL;
    int current = CFG_SERVO_INITIAL;

    hal_servo_set_angle(CFG_SERVO_INITIAL);

    while (1) {
        angle_broadcast_t msg;
        // 非阻塞读取，队列空则继续 lerp
        if (xQueueReceive(s_angle_queue, &msg, 0) == pdTRUE) {
            if (msg.button) {
                // 按键重置：立即跳到初始角度
                target = CFG_SERVO_INITIAL;
                current = CFG_SERVO_INITIAL;
                hal_servo_set_angle(current);
                ESP_LOGI(TAG, "[servo] BTN reset → %d", current);
            } else {
                target = msg.servo_target;
            }
        }

        // 每 10ms 逼近 1°，180° 全程约 1.8s
        if (current < target) {
            current++;
        } else if (current > target) {
            current--;
        }
        hal_servo_set_angle(current);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ================================================================
// 消费者2：RGB 颜色实时更新（独立任务，不被 SPI 阻塞）
// ================================================================
static void rgb_task(void *arg)
{
    hal_rgb_set_color(CFG_RGB_INITIAL_R, CFG_RGB_INITIAL_G, CFG_RGB_INITIAL_B);

    while (1) {
        angle_broadcast_t msg;
        // 阻塞等待新消息，来了就立即更新颜色（无 lerp，纯跳变）
        if (xQueueReceive(s_angle_queue, &msg, portMAX_DELAY) == pdTRUE) {
            if (msg.button) {
                // 按键重置：保持初始颜色不动
            } else {
                hal_rgb_set_color(msg.r, msg.g, msg.b);
            }
        }
    }
}

// ================================================================
// 消费者3：GC9A01 方块旋转（独立任务，独立 SPI 刷新节奏）
// ================================================================
static lv_obj_t *s_gc_square = NULL;

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    int x_start = area->x1;
    int y_start = area->y1;
    int x_end   = area->x2 + 1;
    int y_end   = area->y2 + 1;

    hal_gc9a01_draw_bitmap(x_start, y_start, x_end, y_end, px_map);
    lv_display_flush_ready(disp);
}

static void lvgl_task(void *arg)
{
    lv_init();

    size_t buf_pixels = GC9A01_WIDTH * GC9A01_HEIGHT / 10;
    static lv_color_t *buf1 = NULL;
    buf1 = heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (!buf1) {
        ESP_LOGW(TAG, "[lvgl] PSRAM alloc failed, using internal RAM");
        buf1 = malloc(buf_pixels * sizeof(lv_color_t));
    }
    if (!buf1) {
        ESP_LOGE(TAG, "[lvgl] buffer alloc FAILED");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "[lvgl] buffer: %d pixels", buf_pixels);

    lv_display_t *disp = lv_display_create(GC9A01_WIDTH, GC9A01_HEIGHT);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, buf1, NULL, buf_pixels * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    s_gc_square = lv_obj_create(scr);
    lv_obj_set_size(s_gc_square, 80, 80);
    lv_obj_set_pos(s_gc_square, 80, 80);
    lv_obj_set_style_radius(s_gc_square, 0, 0);
    lv_obj_set_style_bg_color(s_gc_square, lv_color_make(0, 0, 255), 0);
    lv_obj_set_style_border_color(s_gc_square, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_gc_square, 4, 0);

    // 初始角度
    int last_angle = -1;

    while (1) {
        angle_broadcast_t msg;
        // 非阻塞：队列空不等待，继续处理 lv_timer_handler
        if (xQueueReceive(s_angle_queue, &msg, 0) == pdTRUE) {
            if (msg.button == 0 && msg.angle != last_angle) {
                last_angle = msg.angle;
                // EC11 0-180° → 方块 0-360°
                int square_angle = msg.angle * 2;
                lv_obj_set_style_transform_rotation(s_gc_square,
                                                    (int16_t)(square_angle * 10), 0);
                // 角度变化才触发一次 SPI 刷新
                lv_refr_now(disp);
            }
        }
        // lv_timer_handler 也需要运行（LVGL 内部定时器）
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ================================================================
// 消费者4：LCD1602 定期刷新（最低优先级，慢刷新不影响系统）
// ================================================================
static void lcd_task(void *arg)
{
    // LCD 初始化在 app_main 里已完成，这里只管刷新
    int last_display_angle = -1;

    while (1) {
        angle_broadcast_t msg;
        // 阻塞等待，每 100ms 刷新一次 LCD（不需要太快）
        if (xQueueReceive(s_angle_queue, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (msg.button == 0 && msg.angle != last_display_angle) {
                last_display_angle = msg.angle;
                hal_lcd1602_set_cursor(0, 0);
#if CFG_DISPLAY_MODE == 1
                hal_lcd1602_printf_row0("Angle: %3d deg", msg.servo_target);
                hal_lcd1602_printf_row1("R:%3u G:%3u B:%3u", msg.r, msg.g, msg.b);
#elif CFG_DISPLAY_MODE == 2
                hal_lcd1602_printf_row0("Angle: %3d deg", msg.servo_target);
                hal_lcd1602_printf_row1("%s", CFG_DISPLAY_CUSTOM_TEXT);
#elif CFG_DISPLAY_MODE == 3
                hal_lcd1602_printf_row0("Angle: %3d deg", msg.servo_target);
                hal_lcd1602_set_cursor(1, 0);
                hal_lcd1602_print("                ");
#elif CFG_DISPLAY_MODE == 4
                hal_lcd1602_printf_row0("%s", CFG_DISPLAY_CUSTOM_TEXT);
                hal_lcd1602_printf_row1("Angle: %3d deg", msg.servo_target);
#endif
            }
        }
        // vTaskDelay 由上面的 pdMS_TO_TICKS(100) 保证了
    }
}

// ================================================================
// 主入口
// ================================================================
void app_main(void)
{
    // ---- 初始化所有 HAL（各自独立，互不依赖）----
    ESP_ERROR_CHECK(hal_ec11_init() == HAL_OK ? ESP_OK : ESP_FAIL);
    hal_ec11_set_step(CFG_ENCODER_STEP_SIZE);
    hal_ec11_set_angle(CFG_ENCODER_INITIAL);

    ESP_ERROR_CHECK(hal_servo_init() == HAL_OK ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(hal_rgb_init() == HAL_OK ? ESP_OK : ESP_FAIL);

    hal_lcd1602_init();
    hal_lcd1602_backlight(true);
    hal_lcd1602_clear();
    hal_lcd1602_printf_row0(CFG_DISPLAY_STARTUP_ROW0);
    hal_lcd1602_printf_row1(CFG_DISPLAY_STARTUP_ROW1);

    // ---- 创建广播队列（深度 8，足够覆盖所有消费者）----
    s_angle_queue = xQueueCreate(8, sizeof(angle_broadcast_t));
    if (!s_angle_queue) {
        ESP_LOGE(TAG, "angle queue create failed");
        return;
    }

    // ---- GC9A01 直接 SPI 测试（RED→GREEN→BLUE）----
    ESP_LOGI(TAG, ">>> hal_gc9a01_spi_test()...");
    hal_gc9a01_spi_test();
    ESP_LOGI(TAG, ">>> hal_gc9a01_spi_test() done");

    vTaskDelay(pdMS_TO_TICKS(5000));

    // ---- 初始化 GC9A01 显示 ----
    ESP_LOGI(TAG, ">>> hal_gc9a01_init() for LVGL...");
    if (hal_gc9a01_init() != HAL_OK) {
        ESP_LOGW(TAG, ">>> hal_gc9a01_init() FAILED");
    }

    // ---- 启动所有任务（从高优先级到低）----
    // 最高：只读 EC11
    xTaskCreatePinnedToCore(ec11_reader_task, "ec11_reader", 4096, NULL, 20, NULL, 0);
    // 消费者：舵机（RPi lerp）、RGB（直接更新）
    xTaskCreatePinnedToCore(servo_task, "servo", 4096, NULL, 15, NULL, 0);
    xTaskCreatePinnedToCore(rgb_task,   "rgb",   4096, NULL, 14, NULL, 0);
    // 消费者：GC9A01 显示（独立 SPI 刷新）
    xTaskCreatePinnedToCore(lvgl_task,  "lvgl",  8192, NULL,  5, NULL, 1);
    // 消费者：LCD1602（慢刷新，最低优先级）
    xTaskCreatePinnedToCore(lcd_task,    "lcd",   4096, NULL,  3, NULL, 0);

    ESP_LOGI(TAG, "System initialized — ec11_reader(20) servo(15) rgb(14) lvgl(5) lcd(3)");
}
