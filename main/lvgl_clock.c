/**
 * lvgl_clock.c — LVGL 时钟显示（数字 + 指针模式）
 *
 * 数字模式：标签显示时间/日期
 * 指针模式：使用 lv_scale (LV_SCALE_MODE_ROUND_INNER) 实现表盘 + 指针
 */

#include "lvgl_clock.h"
#include "hal/hal_clock.h"
#include "app_config.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "LVGL_CLOCK";

// === Static LVGL objects ===
static lv_obj_t *s_time_label = NULL;
static lv_obj_t *s_date_label = NULL;
static lv_obj_t *s_scale_obj = NULL;        // lv_scale 表盘
static lv_obj_t *s_hour_needle = NULL;      // 时针
static lv_obj_t *s_min_needle = NULL;      // 分针
static lv_obj_t *s_sec_needle = NULL;      // 秒针

static clock_display_mode_t s_mode = CLOCK_DIGITAL;
static clock_content_t s_content = CONTENT_TIME_ONLY;
static bool s_initialized = false;

static lv_point_precise_t s_hour_points[2];
static lv_point_precise_t s_min_points[2];
static lv_point_precise_t s_sec_points[2];

// 表盘尺寸 (240x240 屏幕，居中)
#define SCALE_SIZE  220
#define SCALE_R     (SCALE_SIZE / 2)

// ----------------------------------------------------------------
// Digital mode: labels
// ----------------------------------------------------------------
static void create_digital_objects(lv_obj_t *scr)
{
    s_time_label = lv_label_create(scr);
    lv_label_set_text(s_time_label, "00:00:00");
    lv_obj_set_style_text_color(s_time_label, lv_color_white(), 0);
    lv_obj_align(s_time_label, LV_ALIGN_CENTER, 0, -20);

    s_date_label = lv_label_create(scr);
    lv_label_set_text(s_date_label, "2026-04-21");
    lv_obj_set_style_text_color(s_date_label, lv_color_hex(0x888888), 0);
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
    }
    if (s_content == CONTENT_DATE_ONLY || s_content == CONTENT_TIME_DATE) {
        hal_clock_get_date_str(buf, sizeof(buf), "YYYY-MM-DD");
        lv_label_set_text(s_date_label, buf);
    }
    if (s_content == CONTENT_DOW_DATE) {
        hal_clock_time_t t;
        hal_clock_get_time(&t);
        snprintf(buf, sizeof(buf), "%s", hal_clock_dow_str(t.day_of_week));
        lv_label_set_text(s_date_label, buf);
    }
}

// ----------------------------------------------------------------
// Analog mode: lv_scale (round inner) + line needles
// ----------------------------------------------------------------
static void create_analog_objects(lv_obj_t *scr)
{
    // 表盘 (round, no ticks/labels for clean look)
    s_scale_obj = lv_scale_create(scr);
    lv_obj_set_size(s_scale_obj, SCALE_SIZE, SCALE_SIZE);
    lv_scale_set_mode(s_scale_obj, LV_SCALE_MODE_ROUND_INNER);
    lv_obj_set_style_bg_color(s_scale_obj, lv_color_hex(0x111122), 0);
    lv_obj_set_style_radius(s_scale_obj, SCALE_R, 0);
    lv_scale_set_label_show(s_scale_obj, false);
    lv_scale_set_total_tick_count(s_scale_obj, 0);  // no tick marks
    lv_scale_set_angle_range(s_scale_obj, 360);
    lv_scale_set_rotation(s_scale_obj, 270);  // start from 12 o'clock
    lv_scale_set_range(s_scale_obj, 0, 60);   // 60-minute range for needle values

    // 时针 (short, thick, white) — 3x wider
    s_hour_needle = lv_line_create(s_scale_obj);
    lv_line_set_points_mutable(s_hour_needle, s_hour_points, 2);
    lv_obj_set_style_line_width(s_hour_needle, 12, 0);
    lv_obj_set_style_line_rounded(s_hour_needle, true, 0);
    lv_obj_set_style_line_color(s_hour_needle, lv_color_white(), 0);

    // 分针 (medium, cyan) — 3x wider
    s_min_needle = lv_line_create(s_scale_obj);
    lv_line_set_points_mutable(s_min_needle, s_min_points, 2);
    lv_obj_set_style_line_width(s_min_needle, 9, 0);
    lv_obj_set_style_line_rounded(s_min_needle, true, 0);
    lv_obj_set_style_line_color(s_min_needle, lv_color_hex(0x00CCCC), 0);

    // 秒针 (long, thin, red) — 3x wider
    if (CFG_CLOCK_ANALOG_SHOW_SEC) {
        s_sec_needle = lv_line_create(s_scale_obj);
        lv_line_set_points_mutable(s_sec_needle, s_sec_points, 2);
        lv_obj_set_style_line_width(s_sec_needle, 6, 0);
        lv_obj_set_style_line_rounded(s_sec_needle, true, 0);
        lv_obj_set_style_line_color(s_sec_needle, lv_color_hex(0xFF3300), 0);
    }

    // 表盘居中 (240x240 屏幕，scale 220x220 偏移 10px)
    lv_obj_align(s_scale_obj, LV_ALIGN_CENTER, 0, 0);
}

static void show_analog(bool show)
{
    if (show) {
        lv_obj_remove_flag(s_scale_obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_scale_obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_analog_hands(void)
{
    hal_clock_time_t t;
    hal_clock_get_time(&t);

    /* lv_scale rotates needles automatically based on value.
     * Range is 0-60 (60 minutes), so value = minute (0-59).
     * Hour hand: 12-hour cycle = 60 ticks, hour_value = (hour%12)*5 + minute/12
     * Second hand: separate line (not using lv_scale needle) */
    int hour_value = (t.hour % 12) * 5 + (t.minute / 12);

    lv_scale_set_line_needle_value(s_scale_obj, s_hour_needle, 60, hour_value);
    lv_scale_set_line_needle_value(s_scale_obj, s_min_needle,  80, t.minute);

    if (CFG_CLOCK_ANALOG_SHOW_SEC && s_sec_needle) {
        lv_scale_set_line_needle_value(s_scale_obj, s_sec_needle, 95, t.second);
    }
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

    // Initial update with current time
    if (s_mode == CLOCK_DIGITAL) {
        update_digital_labels();
    } else {
        update_analog_hands();
    }

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

    if (force_full_redraw) {
        lv_obj_invalidate(lv_screen_active());
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
        update_analog_hands();  // draw immediately
    }

    ESP_LOGI(TAG, "Clock mode switched to %s",
             s_mode == CLOCK_DIGITAL ? "DIGITAL" : "ANALOG");
}

void lvgl_clock_next_content(void)
{
    s_content = (clock_content_t)((s_content + 1) % CONTENT_COUNT);
    ESP_LOGI(TAG, "Clock content: %d", s_content);
}

clock_display_mode_t lvgl_clock_get_mode(void) { return s_mode; }
clock_content_t lvgl_clock_get_content(void) { return s_content; }
bool lvgl_clock_is_active(void) { return s_initialized; }
