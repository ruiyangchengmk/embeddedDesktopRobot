#ifndef HAL_COMMON_H
#define HAL_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} hal_rgb_color_t;

typedef enum {
    HAL_OK = 0,
    HAL_ERR = -1,
    HAL_ERR_INVALID_ARG = -2,
    HAL_ERR_NO_MEM = -3,
    HAL_ERR_NOT_INIT = -4,
} hal_err_t;

#ifdef __cplusplus
}
#endif

#endif /* HAL_COMMON_H */
