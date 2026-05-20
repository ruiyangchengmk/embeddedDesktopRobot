#include "voice_prompt.h"

#include "esp_log.h"

#include "hal/hal_audio_out.h"
#include "voice_assets.h"

static const char *TAG = "VOICE_PROMPT";

static hal_err_t voice_prompt_play_asset(voice_asset_id_t id)
{
    voice_asset_t asset;
    hal_err_t err;

    if (!voice_assets_get(id, &asset)) {
        return HAL_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "play asset %d: %u samples, %u ms",
             (int)id,
             (unsigned)asset.sample_count,
             (unsigned)((asset.sample_count * 1000U) / 16000U));

    err = hal_audio_out_begin_playback();
    if (err != HAL_OK) {
        return err;
    }

    err = hal_audio_out_play_mono16(asset.samples, asset.sample_count);

    {
        hal_err_t stop_err = hal_audio_out_end_playback();
        if (err == HAL_OK && stop_err != HAL_OK) {
            err = stop_err;
        }
    }
    return err;
}

hal_err_t voice_prompt_play_too_close_warning(void)
{
    return voice_prompt_play_asset(VOICE_ASSET_WARNING_TOO_CLOSE);
}
