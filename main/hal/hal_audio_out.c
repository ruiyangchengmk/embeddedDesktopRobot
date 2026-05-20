#include "hal_audio_out.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "driver/i2s_std.h"
#include "esp_log.h"

#define AUDIO_OUT_SAMPLE_RATE_HZ   16000
#define AUDIO_OUT_BCLK_GPIO        13
#define AUDIO_OUT_WS_GPIO          14
#define AUDIO_OUT_DOUT_GPIO        15
#define AUDIO_OUT_WRITE_SAMPLES    256
#define AUDIO_OUT_STOP_SILENCE_MS  120
#define AUDIO_OUT_DRAIN_MS         250
#define AUDIO_OUT_GAIN_NUM         1
#define AUDIO_OUT_GAIN_DEN         2

static const char *TAG = "HAL_AUDIO";

static i2s_chan_handle_t s_tx_chan = NULL;
static bool s_initialized = false;
static bool s_tx_enabled = false;
static int16_t s_stereo_buf[AUDIO_OUT_WRITE_SAMPLES * 2];

static hal_err_t hal_audio_out_write_frames(const int16_t *frames, size_t frame_count)
{
    size_t bytes_written = 0;
    size_t bytes_to_write = frame_count * 2 * sizeof(int16_t);

    if (!s_initialized || !s_tx_chan || !s_tx_enabled) {
        return HAL_ERR_NOT_INIT;
    }

    if (!frames || frame_count == 0) {
        return HAL_ERR_INVALID_ARG;
    }

    if (i2s_channel_write(s_tx_chan, frames, bytes_to_write, &bytes_written,
                          portMAX_DELAY) != ESP_OK || bytes_written != bytes_to_write) {
        ESP_LOGE(TAG, "i2s write failed: wrote %u/%u bytes",
                 (unsigned)bytes_written, (unsigned)bytes_to_write);
        return HAL_ERR;
    }

    return HAL_OK;
}

hal_err_t hal_audio_out_init(void)
{
    if (s_initialized) {
        return HAL_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    if (i2s_new_channel(&chan_cfg, &s_tx_chan, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to allocate I2S TX channel");
        return HAL_ERR;
    }

    i2s_std_slot_config_t slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_OUT_SAMPLE_RATE_HZ),
        .slot_cfg = slot_cfg,
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = AUDIO_OUT_BCLK_GPIO,
            .ws = AUDIO_OUT_WS_GPIO,
            .dout = AUDIO_OUT_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    if (i2s_channel_init_std_mode(s_tx_chan, &std_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2S std mode");
        i2s_del_channel(s_tx_chan);
        s_tx_chan = NULL;
        return HAL_ERR;
    }

    if (i2s_channel_enable(s_tx_chan) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S TX channel");
        i2s_del_channel(s_tx_chan);
        s_tx_chan = NULL;
        return HAL_ERR;
    }

    s_initialized = true;
    s_tx_enabled = true;
    ESP_LOGI(TAG, "Audio out initialized: MCLK=unused BCLK=%d WS=%d DOUT=%d @ %d Hz",
             AUDIO_OUT_BCLK_GPIO, AUDIO_OUT_WS_GPIO, AUDIO_OUT_DOUT_GPIO,
             AUDIO_OUT_SAMPLE_RATE_HZ);
    return HAL_OK;
}

hal_err_t hal_audio_out_deinit(void)
{
    if (!s_initialized) {
        return HAL_OK;
    }

    if (s_tx_chan) {
        if (s_tx_enabled) {
            i2s_channel_disable(s_tx_chan);
            s_tx_enabled = false;
        }
        i2s_del_channel(s_tx_chan);
        s_tx_chan = NULL;
    }

    s_initialized = false;
    return HAL_OK;
}

hal_err_t hal_audio_out_begin_playback(void)
{
    if (!s_initialized || !s_tx_chan) {
        return HAL_ERR_NOT_INIT;
    }
    return HAL_OK;
}

hal_err_t hal_audio_out_end_playback(void)
{
    if (!s_initialized || !s_tx_chan) {
        return HAL_ERR_NOT_INIT;
    }
    memset(s_stereo_buf, 0, sizeof(s_stereo_buf));
    size_t tail_samples = (AUDIO_OUT_SAMPLE_RATE_HZ * AUDIO_OUT_STOP_SILENCE_MS + 999U) / 1000U;
    while (tail_samples > 0) {
        size_t chunk = tail_samples > AUDIO_OUT_WRITE_SAMPLES ? AUDIO_OUT_WRITE_SAMPLES : tail_samples;
        hal_err_t err = hal_audio_out_write_frames(s_stereo_buf, chunk);
        if (err != HAL_OK) {
            return err;
        }
        tail_samples -= chunk;
    }

    vTaskDelay(pdMS_TO_TICKS(AUDIO_OUT_DRAIN_MS));
    return HAL_OK;
}

hal_err_t hal_audio_out_play_mono16(const int16_t *samples, size_t sample_count)
{
    size_t offset = 0;

    if (!s_initialized || !s_tx_enabled) {
        return HAL_ERR_NOT_INIT;
    }
    if (!samples || sample_count == 0) {
        return HAL_ERR_INVALID_ARG;
    }

    while (offset < sample_count) {
        size_t chunk = sample_count - offset;
        if (chunk > AUDIO_OUT_WRITE_SAMPLES) {
            chunk = AUDIO_OUT_WRITE_SAMPLES;
        }

        for (size_t i = 0; i < chunk; ++i) {
            int32_t scaled = ((int32_t)samples[offset + i] * AUDIO_OUT_GAIN_NUM) / AUDIO_OUT_GAIN_DEN;
            if (scaled > INT16_MAX) {
                scaled = INT16_MAX;
            } else if (scaled < INT16_MIN) {
                scaled = INT16_MIN;
            }
            s_stereo_buf[i * 2] = (int16_t)scaled;
            s_stereo_buf[i * 2 + 1] = (int16_t)scaled;
        }
        hal_err_t err = hal_audio_out_write_frames(s_stereo_buf, chunk);
        if (err != HAL_OK) {
            return err;
        }
        offset += chunk;
    }

    return HAL_OK;
}

hal_err_t hal_audio_out_play_silence_ms(uint32_t duration_ms)
{
    size_t total_samples;

    if (!s_initialized || !s_tx_enabled) {
        return HAL_ERR_NOT_INIT;
    }

    memset(s_stereo_buf, 0, sizeof(s_stereo_buf));
    total_samples = (AUDIO_OUT_SAMPLE_RATE_HZ * (size_t)duration_ms + 999U) / 1000U;

    while (total_samples > 0) {
        size_t chunk = total_samples > AUDIO_OUT_WRITE_SAMPLES ? AUDIO_OUT_WRITE_SAMPLES : total_samples;
        hal_err_t err = hal_audio_out_write_frames(s_stereo_buf, chunk);
        if (err != HAL_OK) {
            return err;
        }
        total_samples -= chunk;
    }

    return HAL_OK;
}
