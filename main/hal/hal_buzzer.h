#pragma once

#include <stdint.h>

typedef enum {
    HAL_BUZZER_OK,
    HAL_BUZZER_ERR,
    HAL_BUZZER_ERR_NOT_INIT,
} hal_buzzer_err_t;

hal_buzzer_err_t hal_buzzer_init(void);
hal_buzzer_err_t hal_buzzer_play_hello_world(void);
hal_buzzer_err_t hal_buzzer_stop(void);
hal_buzzer_err_t hal_buzzer_deinit(void);
