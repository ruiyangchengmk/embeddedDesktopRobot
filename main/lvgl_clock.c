/**
 * lvgl_clock.c — LVGL 时钟显示（数字 + 指针模式）
 *
 * GC9A01 刷新率限制：
 * - 整屏刷新约 16ms，频繁全屏刷新会导致颜色错乱
 * - 解决方案：只在脏区域（changed regions）调用 lv_obj_invalidate()
 * - 指针模式下：表盘静态绘制，指针每次只更新两根线（上一位置→黑色，当前位置→白色）
 */

#include "lvgl_clock.h"
#include "hal_clock.h"
#include "app_config.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "LVGL_CLOCK";

// === Static LVGL objects ===
static lv_obj_t *s_time_label = NULL;
static lv_obj_t *s_date_label = NULL;
static lv_obj_t *s_face_circle = NULL;
static lv_obj_t *s_hour_line = NULL;
static lv_obj_t *s_min_line = NULL;
static lv_obj_t *s_sec_line = NULL;

static clock_display_mode_t s_mode = CLOCK_DIGITAL;
static clock_content_t s_content = CONTENT_TIME_ONLY;
static bool s_initialized = false;

// Previous hand endpoints for erasure (screen coordinates, center=120,120)
typedef struct { int16_t x; int16_t y; } point_t;
static point_t s_prev_hour = {120, 120};
static point_t s_prev_min = {120, 120};
static point_t s_prev_sec = {120, 120};

// Clock face geometry
#define FACE_CX  120
#define FACE_CY  120
#define FACE_R   (CFG_MODE_CLOCK_DISPLAY ? CFG_CLOCK_ANALOG_FACE_R : 100)
#define HOUR_LEN (CFG_MODE_CLOCK_DISPLAY ? CFG_CLOCK_ANALOG_HOUR_LEN : 60)
#define MIN_LEN  (CFG_MODE_CLOCK_DISPLAY ? CFG_CLOCK_ANALOG_MIN_LEN : 80)
#define SEC_LEN  (CFG_MODE_CLOCK_DISPLAY ? CFG_CLOCK_ANALOG_SEC_LEN : 90)

static inline void calc_hand_endpoint(int hours, int minutes, int seconds,
                                     int hand_length,
                                     int *out_x, int *out_y)
{
    float total_min = hours * 60.0f + minutes + seconds / 60.0f;
    float angle_rad = (total_min / (12.0f * 60.0f)) * 2.0f * M_PI - M_PI_2;
    *out_x = (int)(FACE_CX + hand_length * cosf(angle_rad) + 0.5f);
    *out_y = (int)(FACE_CY + hand_length * sinf(angle_rad) + 0.5f);
}

static inline void calc_sec_endpoint(int seconds,
                                      int hand_length,
                                      int *out_x, int *out_y)
{
    float angle_rad = (seconds / 60.0f) * 2.0f * M_PI - M_PI_2;
    *out_x = (int)(FACE_CX + hand_length * cosf(angle_rad) + 0.5f);
    *out_y = (int)(FACE_CY + hand_length * sinf(angle_rad) + 0.5f);
}

// ----------------------------------------------------------------
// Digital mode: create/show/hide labels
// ----------------------------------------------------------------
static void create_digital_objects(lv_obj_t *scr)
{
    s_time_label = lv_label_create(scr);
    lv_label_set_text(s_time_label, "00:00:00");
    lv_obj_set_style_text_color(s_time_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_28, 0);
    lv_obj_align(s_time_label, LV_ALIGN_CENTER, 0, -20);

    s_date_label = lv_label_create(scr);
    lv_label_set_text(s_date_label, "2026-04-21");
    lv_obj_set_style_text_color(s_date_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(s_date_label, &lv_font_montserrat_16, 0);
    lv_obj_align(s_date_label, LV_ALIGN_CENTER, 0, 20);
}

static void show_digital(bool show)
{
    if (show) {
        lv_obj_remove_flag(s_time_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_date_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_time_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_date_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_digital_labels(void)
{
    char buf[32];

    if (s_content == CONTENT_TIME_ONLY || s_content == CONTENT_TIME_DATE) {
        hal_clock_get_time_str(buf, sizeof(buf), "HH:MM:SS");
        lv_label_set_text(s_time_label, buf);
        lv_obj_invalidate(s_time_label);
    }
    if (s_content == CONTENT_DATE_ONLY || s_content == CONTENT_TIME_DATE ||
        s_content == CONTENT_DOW_DATE) {
        hal_clock_get_date_str(buf, sizeof(buf), "YYYY-MM-DD");
        lv_label_set_text(s_date_label, buf);
        lv_obj_invalidate(s_date_label);
    }
    if (s_content == CONTENT_DOW_DATE) {
        hal_clock_time_t t;
        hal_clock_get_time(&t);
        snprintf(buf, sizeof(buf), "%s", hal_clock_dow_str(t.day_of_week));
        lv_label_set_text(s_date_label, buf);
        lv_obj_invalidate(s_date_label);
    }
}

// ----------------------------------------------------------------
// Analog mode: create/show/hide clock face
// ----------------------------------------------------------------
static void create_analog_objects(lv_obj_t *scr)
{
    // Face circle (static, drawn once)
    s_face_circle = lv_circle_create(scr);
    lv_circle_set_radius(s_face_circle, FACE_R);
    lv_obj_set_pos(s_face_circle, FACE_CX - FACE_R, FACE_CY - FACE_R);
    lv_obj_set_style_radius(s_face_circle, FACE_R, 0);
    lv_obj_set_style_bg_color(s_face_circle, lv_color_hex(0x111122), 0);  // dark bg
    lv_obj_set_style_border_color(s_face_circle, lv_color_hex(0x3366FF), 0);
    lv_obj_set_style_border_width(s_face_circle, 2, 0);

    // Hour hand (thick, white)
    s_hour_line = lv_line_create(scr);
    lv_obj_set_style_line_color(s_hour_line, lv_color_white(), 0);
    lv_obj_set_style_line_width(s_hour_line, 4, 0);

    // Minute hand (medium, cyan)
    s_min_line = lv_line_create(scr);
    lv_obj_set_style_line_color(s_min_line, lv_color_hex(0x00CCCC), 0);
    lv_obj_set_style_line_width(s_min_line, 3, 0);

    // Second hand (thin, red)
    s_sec_line = lv_line_create(scr);
    lv_obj_set_style_line_color(s_sec_line, lv_color_hex(0xFF3300), 0);
    lv_obj_set_style_line_width(s_sec_line, 2, 0);
}

static void show_analog(bool show)
{
    if (show) {
        lv_obj_remove_flag(s_face_circle, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_hour_line, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_min_line, LV_OBJ_FLAG_HIDDEN);
        if (CFG_CLOCK_ANALOG_SHOW_SEC) {
            lv_obj_remove_flag(s_sec_line, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_sec_line, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_obj_add_flag(s_face_circle, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hour_line, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_min_line, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_sec_line, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_analog_hands(void)
{
    hal_clock_time_t t;
    hal_clock_get_time(&t);

    // Erase previous positions by drawing black lines
    static const lv_point_t black_prev_hour[] = {{FACE_CX, FACE_CY}, {120, 120}};
    static const lv_point_t black_prev_min[] = {{FACE_CX, FACE_CY}, {120, 120}};
    static lv_point_t black_sec[2] = {{FACE_CX, FACE_CY}, {120, 120}};
    lv_point_t pts_black_hour[2] = {{FACE_CX, FACE_CY}, {s_prev_hour.x, s_prev_hour.y}};
    lv_point_t pts_black_min[2] = {{FACE_CX, FACE_CY}, {s_prev_min.x, s_prev_min.y}};
    lv_point_t pts_black_sec[2] = {{FACE_CX, FACE_CY}, {s_prev_sec.x, s_prev_sec.y}};

    // Draw old positions in black (erase)
    lv_line_set_points(s_hour_line, pts_black_hour, 2);
    lv_line_set_points(s_min_line, pts_black_min, 2);
    if (CFG_CLOCK_ANALOG_SHOW_SEC) {
        lv_line_set_points(s_sec_line, pts_black_sec, 2);
    }
    lv_obj_invalidate(s_hour_line);
    lv_obj_invalidate(s_min_line);
    if (CFG_CLOCK_ANALOG_SHOW_SEC) {
        lv_obj_invalidate(s_sec_line);
    }

    // Calculate new positions
    int hx, hy, mx, my, sx, sy;
    calc_hand_endpoint(t.hour % 12, t.minute, t.second, HOUR_LEN, &hx, &hy);
    calc_hand_endpoint(0, t.minute, t.second, MIN_LEN, &mx, &my);
    if (CFG_CLOCK_ANALOG_SHOW_SEC) {
        calc_sec_endpoint(t.second, SEC_LEN, &sx, &sy);
    } else {
        sx = FACE_CX; sy = FACE_CY;
    }

    // Draw new positions in color
    lv_point_t pts_hour[2] = {{FACE_CX, FACE_CY}, {hx, hy}};
    lv_point_t pts_min[2] = {{FACE_CX, FACE_CY}, {mx, my}};
    lv_point_t pts_sec[2] = {{FACE_CX, FACE_CY}, {sx, sy}};
    lv_line_set_points(s_hour_line, pts_hour, 2);
    lv_line_set_points(s_min_line, pts_min, 2);
    if (CFG_CLOCK_ANALOG_SHOW_SEC) {
        lv_line_set_points(s_sec_line, pts_sec, 2);
    }

    // Invalidate only the hand objects (not the whole face)
    lv_obj_invalidate(s_hour_line);
    lv_obj_invalidate(s_min_line);
    if (CFG_CLOCK_ANALOG_SHOW_SEC) {
        lv_obj_invalidate(s_sec_line);
    }

    // Store for next erase
    s_prev_hour.x = hx; s_prev_hour.y = hy;
    s_prev_min.x = mx; s_prev_min.y = my;
    s_prev_sec.x = sx; s_prev_sec.y = sy;
}

// ----------------------------------------------------------------
// Public API
// ----------------------------------------------------------------
void lvgl_clock_init(lv_obj_t *scr, clock_display_mode_t init_mode)
{
    if (s_initialized) return;

    s_mode = init_mode;
    create_digital_objects(scr);
    create_analog_objects(scr);

    if (s_mode == CLOCK_ANALOG) {
        show_digital(false);
        show_analog(true);
    } else {
        show_analog(false);
        show_digital(true);
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Clock display initialized in mode=%s",
             s_mode == CLOCK_DIGITAL ? "DIGITAL" : "ANALOG");
}

void lvgl_clock_update(bool force_full_redraw)
{
    if (!s_initialized) return;

    if (s_mode == CLOCK_DIGITAL) {
        update_digital_labels();
    } else {
        update_analog_hands();
    }
}

void lvgl_clock_set_mode(clock_display_mode_t mode)
{
    if (s_mode == mode) return;
    s_mode = mode;

    if (s_mode == CLOCK_DIGITAL) {
        show_analog(false);
        show_digital(true);
    } else {
        show_digital(false);
        show_analog(true);
        // Reset previous hand positions to center to avoid black flash
        s_prev_hour.x = s_prev_min.x = s_prev_sec.x = FACE_CX;
        s_prev_hour.y = s_prev_min.y = s_prev_sec.y = FACE_CY;
    }
    lv_obj_invalidate(lv_screen_active());
    ESP_LOGI(TAG, "Clock mode switched to %s",
             s_mode == CLOCK_DIGITAL ? "DIGITAL" : "ANALOG");
}

void lvgl_clock_next_content(void)
{
    s_content = (clock_content_t)((s_content + 1) % CONTENT_COUNT);
    ESP_LOGI(TAG, "Clock content: %d", s_content);
}

clock_display_mode_t lvgl_clock_get_mode(void)
{
    return s_mode;
}

clock_content_t lvgl_clock_get_content(void)
{
    return s_content;
}

bool lvgl_clock_is_active(void)
{
    return s_initialized;
}
