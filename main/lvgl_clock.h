#ifndef LVGL_CLOCK_H
#define LVGL_CLOCK_H

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Clock display mode: digital text vs analog pointer.
 * EC11 button cycles through these modes.
 */
typedef enum {
    CLOCK_DIGITAL,    // Time + date as text labels
    CLOCK_ANALOG,     // Circular face with hour/minute/second hands
} clock_display_mode_t;

/**
 * Content shown in digital mode.
 * EC11 rotation cycles through these options.
 */
typedef enum {
    CONTENT_TIME_ONLY,     // HH:MM:SS
    CONTENT_DATE_ONLY,     // YYYY-MM-DD
    CONTENT_TIME_DATE,      // HH:MM:SS + YYYY-MM-DD
    CONTENT_DOW_DATE,       // Mon  2026-04-21
    CONTENT_COUNT,
} clock_content_t;

/**
 * @brief Initialize clock display objects on the given screen.
 * @param scr    Parent LVGL screen object.
 * @param init_mode  Initial display mode (digital or analog).
 */
void lvgl_clock_init(lv_obj_t *scr, clock_display_mode_t init_mode);

/**
 * @brief Update clock display.
 *        Call this every CFG_CLOCK_UPDATE_MS (e.g., 1000ms).
 *        Only invalidates changed regions — safe for GC9A01.
 * @param force_full_redraw  If true, invalidates entire screen.
 */
void lvgl_clock_update(bool force_full_redraw);

/**
 * @brief Switch between digital and analog mode.
 * @param mode  Target display mode.
 */
void lvgl_clock_set_mode(clock_display_mode_t mode);

/**
 * @brief Cycle to next content variant (time/date/dow, etc.).
 *        Called when EC11 is rotated in clock mode.
 */
void lvgl_clock_next_content(void);

/**
 * @brief Get current display mode.
 */
clock_display_mode_t lvgl_clock_get_mode(void);

/**
 * @brief Get current content type.
 */
clock_content_t lvgl_clock_get_content(void);

/**
 * @brief Check if clock display is currently active.
 */
bool lvgl_clock_is_active(void);

#ifdef __cplusplus
}
#endif

#endif /* LVGL_CLOCK_H */
