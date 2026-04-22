/**
 * hal_clock.c — 软件计时器时钟（无 NTP/WiFi 依赖）
 *
 * 基于 esp_timer 实现，初始化时使用编译时主机时间作为起始时间。
 * 断电后时间丢失，符合需求。
 *
 * 时间更新精度：秒级。
 */

#include "hal_clock.h"
#include "app_config.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "HAL_CLOCK";
static int64_t s_start_us = 0;  // 上电时的时间戳（微秒）
static bool s_init_ok = false;

static const char *s_dow_names[] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

int hal_clock_init(void)
{
    // 使用编译时主机时间作为起始基准
    struct tm tm_build;
    memset(&tm_build, 0, sizeof(tm_build));
    tm_build.tm_year = CFG_BUILD_YEAR - 1900;
    tm_build.tm_mon  = CFG_BUILD_MONTH - 1;
    tm_build.tm_mday = CFG_BUILD_DAY;
    tm_build.tm_hour = CFG_BUILD_HOUR;
    tm_build.tm_min  = CFG_BUILD_MIN;
    tm_build.tm_sec  = 0;
    tm_build.tm_isdst = 0;

    time_t build_epoch = mktime(&tm_build);
    if (build_epoch == (time_t)-1) {
        build_epoch = 0;
    }

    // 计算从编译时刻到当前 boot 的时间偏移
    int64_t boot_us = esp_timer_get_time();
    s_start_us = boot_us - (int64_t)build_epoch * 1000000LL;

    s_init_ok = true;
    ESP_LOGI(TAG, "Clock initialized from build time: %04d-%02d-%02d %02d:%02d (boot_offset=%lld us)",
             CFG_BUILD_YEAR, CFG_BUILD_MONTH, CFG_BUILD_DAY,
             CFG_BUILD_HOUR, CFG_BUILD_MIN, (long long)s_start_us);
    return 0;
}

void hal_clock_get_time(hal_clock_time_t *out)
{
    if (!out || !s_init_ok) {
        if (out) memset(out, 0, sizeof(*out));
        return;
    }

    int64_t elapsed_us = esp_timer_get_time() - s_start_us;
    int64_t total_sec = elapsed_us / 1000000LL;

    int day_index = (int)(total_sec / 86400LL);  // 0-indexed days since boot
    int sec_of_day = (int)(total_sec % 86400LL);

    int hour = sec_of_day / 3600;
    int minute = (sec_of_day % 3600) / 60;
    int second = sec_of_day % 60;

    // Build a struct tm for date calculations
    struct tm tm_now;
    memset(&tm_now, 0, sizeof(tm_now));
    // Fake epoch: 2026-01-01 00:00:00 was a Thursday (weekday=4)
    // Add days since boot to get the actual date
    time_t epoch_secs = 1767225600LL + (day_index * 86400LL);  // 2026-01-01 00:00:00 UTC
    struct tm *gm = gmtime(&epoch_secs);
    if (gm) {
        tm_now = *gm;
    }

    out->hour = hour;
    out->minute = minute;
    out->second = second;
    out->year = tm_now.tm_year + 1900;
    out->month = tm_now.tm_mon + 1;
    out->day = tm_now.tm_mday;
    out->day_of_week = tm_now.tm_wday;  // 0=Sun
}

void hal_clock_get_time_str(char *buf, size_t bufsz, const char *format)
{
    if (!buf || bufsz == 0) return;

    hal_clock_time_t t;
    hal_clock_get_time(&t);

    if (strcmp(format, "HH") == 0) {
        snprintf(buf, bufsz, "%02d", t.hour);
    } else if (strcmp(format, "HH:MM") == 0) {
        snprintf(buf, bufsz, "%02d:%02d", t.hour, t.minute);
    } else {  // default HH:MM:SS
        snprintf(buf, bufsz, "%02d:%02d:%02d", t.hour, t.minute, t.second);
    }
}

void hal_clock_get_date_str(char *buf, size_t bufsz, const char *format)
{
    if (!buf || bufsz == 0) return;

    hal_clock_time_t t;
    hal_clock_get_time(&t);

    if (strcmp(format, "YYYY-MM-DD") == 0) {
        snprintf(buf, bufsz, "%04d-%02d-%02d", t.year, t.month, t.day);
    } else if (strcmp(format, "MM-DD") == 0) {
        snprintf(buf, bufsz, "%02d-%02d", t.month, t.day);
    } else {
        snprintf(buf, bufsz, "%04d-%02d-%02d", t.year, t.month, t.day);
    }
}

const char *hal_clock_dow_str(int dow)
{
    if (dow < 0 || dow > 6) dow = 0;
    return s_dow_names[dow];
}
