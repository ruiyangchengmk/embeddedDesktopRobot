/**
 * main.c — ESP32-S3 本地交互控制节点
 *
 * 当前稳定主链路：
 *   EC11 轮询/按键 -> event_broker -> consumer_task ->
 *   servo_task / rgb_task / lvgl_task
 *
 * 启动顺序：
 *   1. 创建私有队列并初始化 event_broker
 *   2. 初始化 servo/rgb
 *   3. 启动 consumer/servo/rgb 任务
 *   4. 初始化 GC9A01 并启动 lvgl_task
 *   5. 最后启动 EC11，避免显示链路尚未就绪时就有输入打入
 *
 * 设计原则：
 *   - 单一硬件故障或调试不应拖垮其他外设
 *   - 每个消费者只处理自己关心的输出
 *   - 快速输入场景优先保留“最新目标值”
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "freertos/queue.h"
#include "esp_timer.h"

#include "app_config.h"

#include "hal/hal_servo.h"
#include "hal/hal_rgb.h"
#include "hal/hal_ec11.h"
#include "hal/hal_gc9a01.h"
#if CFG_MODE_CLOCK_DISPLAY
#include "hal/hal_clock.h"
#include "lvgl_clock.h"
#endif

#include "event_broker.h"
#include "lvgl.h"
#include "lvgl/src/draw/sw/lv_draw_sw.h"

static const char *TAG = "APP";
#define APP_RUN_GC9A01_SPI_TEST 0

// ================================================================
// 消费者私有队列（每个任务独立，避免互斥竞争）
// ================================================================
static QueueHandle_t s_servo_queue = NULL;
static QueueHandle_t s_rgb_queue = NULL;
static QueueHandle_t s_lvgl_queue = NULL;
static QueueHandle_t s_consumer_queue = NULL;
static int64_t s_last_consumer_log_us = 0;

// 消费者消息格式
typedef struct {
    int angle;          // EC11 当前角度
    int servo_target;   // 映射后的舵机目标角度
    uint8_t r, g, b;   // 对应 RGB 颜色
    int button;         // 1=按键按下，0=正常旋转
} angle_msg_t;

static void send_servo_msg(const angle_msg_t *msg)
{
    xQueueOverwrite(s_servo_queue, msg);
}

static void send_lvgl_msg(const angle_msg_t *msg)
{
    xQueueOverwrite(s_lvgl_queue, msg);
}

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
// 事件总线消费者任务
// ================================================================
static void consumer_task(void *arg)
{
    broker_event_t ev;

    while (1) {
        if (xQueueReceive(s_consumer_queue, &ev, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        angle_msg_t msg = {0};

        if (ev.type == EVENT_TYPE_ENCODER_CLICK) {
            msg.button = 1;
            msg.angle = CFG_ENCODER_RESET;
            hal_ec11_set_angle(CFG_ENCODER_RESET);
        } else if (ev.type == EVENT_TYPE_ENCODER_ROTATE) {
            msg.button = 0;
            msg.angle = (int)ev.value;
        } else {
            continue;
        }

        msg.servo_target = CFG_EC11_TO_SERVO(msg.angle);
        color_from_angle(msg.servo_target, &msg.r, &msg.g, &msg.b);

        // 广播给所有消费者（各自私有队列）
        send_servo_msg(&msg);
        xQueueSend(s_rgb_queue, &msg, 0);
        send_lvgl_msg(&msg);

        int64_t now_us = esp_timer_get_time();
        if (msg.button || (now_us - s_last_consumer_log_us) >= 200000) {
            ESP_LOGI(TAG, "Angle: ec11=%d servo=%d RGB(%u,%u,%u)",
                     msg.angle, msg.servo_target, msg.r, msg.g, msg.b);
            s_last_consumer_log_us = now_us;
        }
    }
}

// ================================================================
// 消费者1：舵机平滑跟随
// ================================================================
static void servo_task(void *arg)
{
    int target = CFG_SERVO_INITIAL;
    int current = CFG_SERVO_INITIAL;

    hal_servo_set_angle(CFG_SERVO_INITIAL);

    while (1) {
        angle_msg_t msg;
        if (xQueueReceive(s_servo_queue, &msg, 0) == pdTRUE) {
            while (xQueueReceive(s_servo_queue, &msg, 0) == pdTRUE) {
                // 清空积压消息，只保留最新目标值。
            }
            if (msg.button) {
                target = CFG_SERVO_INITIAL;
                current = CFG_SERVO_INITIAL;
                hal_servo_set_angle(current);
                ESP_LOGI(TAG, "[servo] BTN reset -> %d", current);
            } else {
                target = msg.servo_target;
            }
        }

        int diff = target - current;
        int step = 0;
        if (diff > 0) {
            step = (diff > 30) ? 4 : (diff > 10 ? 2 : 1);
        } else if (diff < 0) {
            step = (diff < -30) ? -4 : (diff < -10 ? -2 : -1);
        }

        if (step != 0) {
            current += step;
            if ((step > 0 && current > target) || (step < 0 && current < target)) {
                current = target;
            }
        }
        hal_servo_set_angle(current);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ================================================================
// 消费者2：RGB 颜色实时更新
// ================================================================
static void rgb_task(void *arg)
{
    hal_rgb_set_color(CFG_RGB_INITIAL_R, CFG_RGB_INITIAL_G, CFG_RGB_INITIAL_B);

    while (1) {
        angle_msg_t msg;
        if (xQueueReceive(s_rgb_queue, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (!msg.button) {
                hal_rgb_set_color(msg.r, msg.g, msg.b);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ================================================================
// 消费者3：GC9A01 显示（图片模式或时钟模式）
// ================================================================
static lv_obj_t *s_gc_image = NULL;
#if CFG_MODE_CLOCK_DISPLAY
static int64_t s_last_clock_update_us = 0;
#endif

// 声明外部图片（由 control/images/resized-image.c 生成）
LV_IMAGE_DECLARE(resized_image);
LV_IMAGE_DECLARE(emotion1);
LV_IMAGE_DECLARE(emotion2);
LV_IMAGE_DECLARE(emotion3);
LV_IMAGE_DECLARE(emotion4);

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

#if CFG_MODE_IMAGES_DISPLAY_1
    // 模式1：显示单张图片，按键时旋转。
    s_gc_image = lv_image_create(scr);
    lv_image_set_src(s_gc_image, &resized_image);
    lv_obj_align(s_gc_image, LV_ALIGN_CENTER, 0, 0);
    // 旋转中心设为图片中心。
    lv_obj_set_style_transform_pivot_x(s_gc_image, 120, 0);
    lv_obj_set_style_transform_pivot_y(s_gc_image, 120, 0);
#endif

#if CFG_MODE_IMAGES_DISPLAY_2
    // 模式2：显示 4 张表情图，EC11 按键切换。
    s_gc_image = lv_image_create(scr);
    lv_image_set_src(s_gc_image, &emotion1);
    lv_obj_align(s_gc_image, LV_ALIGN_CENTER, 0, 0);
#endif

#if CFG_MODE_CLOCK_DISPLAY
    hal_clock_init();
    lvgl_clock_init(scr, CLOCK_DIGITAL);
    s_last_clock_update_us = esp_timer_get_time();
#endif

    int lvgl_loop_cnt = 0;
#if CFG_MODE_IMAGES_DISPLAY_1
    int img_angle = 0;  // 累积旋转角，单位 0.1 度
#endif
#if CFG_MODE_IMAGES_DISPLAY_2
    int s_emotion_idx = 0;
#endif

    while (1) {
        angle_msg_t msg;
        bool changed = false;

        if (xQueueReceive(s_lvgl_queue, &msg, pdMS_TO_TICKS(10)) == pdTRUE) {
#if CFG_MODE_IMAGES_DISPLAY_1 && CFG_MODE_CLOCK_DISPLAY
            if (msg.button == 1) {
                // 两种模式同时编译时：按键优先切换显示行为。
                if (lvgl_clock_is_active()) {
                    lvgl_clock_set_mode(
                        lvgl_clock_get_mode() == CLOCK_DIGITAL ? CLOCK_ANALOG : CLOCK_DIGITAL);
                } else {
                    img_angle += 900;
                    img_angle %= 3600;
                    lv_obj_set_style_transform_rotation(s_gc_image, (int16_t)img_angle, 0);
                    changed = true;
                }
            } else {
                // 时钟模式下，旋转编码器切换显示内容。
                if (lvgl_clock_is_active()) {
                    lvgl_clock_next_content();
                }
            }
#elif CFG_MODE_IMAGES_DISPLAY_1
            if (msg.button == 1) {
                // 按键旋转图片 90 度。
                img_angle += 900;
                img_angle %= 3600;
                lv_obj_set_style_transform_rotation(s_gc_image,
                                                    (int16_t)img_angle, 0);
                changed = true;
                ESP_LOGI(TAG, "[lvgl] BTN pressed, rotated to %d", img_angle);
            }
#elif CFG_MODE_CLOCK_DISPLAY
            if (msg.button == 1) {
                lvgl_clock_set_mode(
                    lvgl_clock_get_mode() == CLOCK_DIGITAL ? CLOCK_ANALOG : CLOCK_DIGITAL);
            } else {
                lvgl_clock_next_content();
            }
#elif CFG_MODE_IMAGES_DISPLAY_2
            if (msg.button == 1) {
                // 按键切换 4 张表情图。
                static const void *emotion_images[4] = {&emotion1, &emotion2, &emotion3, &emotion4};
                s_emotion_idx = (s_emotion_idx + 1) % 4;
                lv_image_set_src(s_gc_image, emotion_images[s_emotion_idx]);
                changed = true;
                ESP_LOGI(TAG, "[lvgl] emotion idx=%d", s_emotion_idx);
            }
#endif
        }

        if (changed) {
            lv_obj_invalidate(lv_screen_active());
        }

        // ---- 时钟刷新（限频）----
#if CFG_MODE_CLOCK_DISPLAY
        if (lvgl_clock_is_active()) {
            int64_t now_us = esp_timer_get_time();
            int64_t elapsed = now_us - s_last_clock_update_us;
            if (elapsed >= (int64_t)CFG_CLOCK_UPDATE_MS * 1000) {
                lvgl_clock_update(false);
                s_last_clock_update_us = now_us;
                // 立刻处理失效区域，避免时钟模式刷新感迟滞。
                lv_timer_handler();
            }
        }
#endif

        lv_tick_inc(10);
        uint32_t time_till_next = lv_timer_handler();

        if (lvgl_loop_cnt % 500 == 0) {
#if CFG_MODE_IMAGES_DISPLAY_1
            ESP_LOGI(TAG, "[lvgl] loop=%d img_angle=%d next_timer=%ums",
                     lvgl_loop_cnt, img_angle, time_till_next);
#elif CFG_MODE_IMAGES_DISPLAY_2
            ESP_LOGI(TAG, "[lvgl] loop=%d emotion_idx=%d next_timer=%ums",
                     lvgl_loop_cnt, s_emotion_idx, time_till_next);
#elif CFG_MODE_CLOCK_DISPLAY
            ESP_LOGI(TAG, "[lvgl] loop=%d clock_active=%d next_timer=%ums",
                     lvgl_loop_cnt, lvgl_clock_is_active(), time_till_next);
#endif
        }

        if (time_till_next == 0) {
            ESP_LOGW(TAG, "[lvgl] timer_handler returned 0, timers may be stuck!");
        }
        if (time_till_next > 100) {
            time_till_next = 100;  // 限制异常大的睡眠时间
        }
        lvgl_loop_cnt++;
        vTaskDelay(pdMS_TO_TICKS(time_till_next ? time_till_next : 1));
    }
}

// ================================================================
// 主入口
// ================================================================
void app_main(void)
{
    // ---- 第一步：创建所有队列（在任何任务启动前）----
    s_servo_queue = xQueueCreate(1, sizeof(angle_msg_t));
    s_rgb_queue = xQueueCreate(10, sizeof(angle_msg_t));
    s_lvgl_queue = xQueueCreate(1, sizeof(angle_msg_t));
    s_consumer_queue = xQueueCreate(32, sizeof(broker_event_t));

    if (!s_servo_queue || !s_rgb_queue || !s_lvgl_queue || !s_consumer_queue) {
        ESP_LOGE(TAG, "Queue create failed");
        return;
    }

    // ---- 初始化事件总线并订阅 ----
    ESP_ERROR_CHECK(event_broker_init() == ESP_OK ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(event_broker_subscribe(EVENT_TYPE_ENCODER_ROTATE, s_consumer_queue));
    ESP_ERROR_CHECK(event_broker_subscribe(EVENT_TYPE_ENCODER_CLICK, s_consumer_queue));

    // ---- 先初始化不会主动产生日志风暴的 HAL ----
    ESP_ERROR_CHECK(hal_servo_init() == HAL_OK ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(hal_rgb_init() == HAL_OK ? ESP_OK : ESP_FAIL);

    // ---- 启动消费者任务 ----
    xTaskCreatePinnedToCore(consumer_task, "consumer", 4096, NULL, 18, NULL, 0);
    xTaskCreatePinnedToCore(servo_task,   "servo",   4096, NULL, 15, NULL, 0);
    xTaskCreatePinnedToCore(rgb_task,     "rgb",     4096, NULL, 14, NULL, 0);

    // ---- 可选 SPI bring-up 测试；正常运行默认跳过，避免阻塞 LVGL 启动 ----
#if APP_RUN_GC9A01_SPI_TEST
    ESP_LOGI(TAG, ">>> hal_gc9a01_spi_test()...");
    hal_gc9a01_spi_test();
    ESP_LOGI(TAG, ">>> hal_gc9a01_spi_test() done");
#endif

    vTaskDelay(pdMS_TO_TICKS(1000));

    // ---- 初始化 GC9A01 显示（硬复位+清屏，为 LVGL 准备干净画布）----
    ESP_LOGI(TAG, ">>> hal_gc9a01_init() for LVGL...");
    if (hal_gc9a01_init() != HAL_OK) {
        ESP_LOGW(TAG, ">>> hal_gc9a01_init() FAILED");
    }

    // ---- 启动 LVGL 任务（GC9A01 已初始化完毕，无竞态）----
    xTaskCreatePinnedToCore(lvgl_task,   "lvgl",    8192, NULL,  5, NULL, 1);

    // ---- 最后启动 EC11，让显示链路先完全就绪 ----
    ESP_ERROR_CHECK(hal_ec11_init() == HAL_OK ? ESP_OK : ESP_FAIL);

    ESP_LOGI(TAG, "System initialized");
}
