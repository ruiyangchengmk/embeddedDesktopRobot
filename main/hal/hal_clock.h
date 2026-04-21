#ifndef HAL_CLOCK_H
#define HAL_CLOCK_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int hour;
    int minute;
    int second;
    int year;
    int month;
    int day;
    int day_of_week;  // 0=Sunday, 1=Monday, ..., 6=Saturday
} hal_clock_time_t;

/**
 * @brief Initialize clock subsystem.
 *        Time starts counting from 0 on boot; not synced to wall clock.
 * @return 0 on success, -1 on error.
 */
int hal_clock_init(void);

/**
 * @brief Get current time since boot.
 * @param out  Pointer to receive current time.
 */
void hal_clock_get_time(hal_clock_time_t *out);

/**
 * @brief Get current time as formatted string.
 * @param buf     Output buffer (caller allocates).
 * @param bufsz   Buffer size in bytes.
 * @param format  Format string: "HH", "HH:MM", or "HH:MM:SS".
 */
void hal_clock_get_time_str(char *buf, size_t bufsz, const char *format);

/**
 * @brief Get current date as formatted string.
 * @param buf     Output buffer (caller allocates).
 * @param bufsz   Buffer size in bytes.
 * @param format  Format string: "YYYY-MM-DD" or "MM-DD".
 */
void hal_clock_get_date_str(char *buf, size_t bufsz, const char *format);

/**
 * @brief Get day of week as string.
 * @param dow  Day of week (0=Sunday, ..., 6=Saturday).
 * @return Static string with day name.
 */
const char *hal_clock_dow_str(int dow);

#ifdef __cplusplus
}
#endif

#endif /* HAL_CLOCK_H */
