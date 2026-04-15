#include "hal_rgb.h"
#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

#define RGB_GPIO            48
#define RMT_RESOLUTION_HZ   10000000

static const char *TAG = "HAL_RGB";

static rmt_channel_handle_t s_led_chan = NULL;
static rmt_encoder_handle_t s_copy_encoder = NULL;
static bool s_initialized = false;

static const rmt_symbol_word_t s_ws2812_zero = {
    .level0 = 1, .duration0 = 3,
    .level1 = 0, .duration1 = 9,
};

static const rmt_symbol_word_t s_ws2812_one = {
    .level0 = 1, .duration0 = 9,
    .level1 = 0, .duration1 = 3,
};

static const rmt_symbol_word_t s_ws2812_reset = {
    .level0 = 0, .duration0 = 600,
    .level1 = 0, .duration1 = 0,
};

hal_err_t hal_rgb_init(void)
{
    if (s_initialized) {
        return HAL_OK;
    }

    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = RGB_GPIO,
        .mem_block_symbols = 64,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &s_led_chan));

    rmt_copy_encoder_config_t encoder_config = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&encoder_config, &s_copy_encoder));
    ESP_ERROR_CHECK(rmt_enable(s_led_chan));

    s_initialized = true;
    ESP_LOGI(TAG, "RGB LED initialized on GPIO%d", RGB_GPIO);
    return HAL_OK;
}

hal_err_t hal_rgb_deinit(void)
{
    if (!s_initialized) {
        return HAL_OK;
    }
    rmt_disable(s_led_chan);
    rmt_del_encoder(s_copy_encoder);
    rmt_del_channel(s_led_chan);
    s_initialized = false;
    return HAL_OK;
}

hal_err_t hal_rgb_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_initialized) {
        return HAL_ERR_NOT_INIT;
    }

    rmt_symbol_word_t symbols[25];
    uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;

    for (int i = 0; i < 24; i++) {
        symbols[i] = (grb & (1 << (23 - i))) ? s_ws2812_one : s_ws2812_zero;
    }
    symbols[24] = s_ws2812_reset;

    rmt_transmit_config_t tx_config = { .loop_count = 0 };
    ESP_ERROR_CHECK(rmt_transmit(s_led_chan, s_copy_encoder, symbols, sizeof(symbols), &tx_config));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(s_led_chan, portMAX_DELAY));

    return HAL_OK;
}

hal_err_t hal_rgb_set_color_by_struct(hal_rgb_color_t color)
{
    return hal_rgb_set_color(color.r, color.g, color.b);
}
