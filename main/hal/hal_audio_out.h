#ifndef HAL_AUDIO_OUT_H
#define HAL_AUDIO_OUT_H

#include "hal_common.h"

#ifdef __cplusplus
extern "C" {
#endif

hal_err_t hal_audio_out_init(void);
hal_err_t hal_audio_out_deinit(void);
hal_err_t hal_audio_out_begin_playback(void);
hal_err_t hal_audio_out_end_playback(void);
hal_err_t hal_audio_out_play_mono16(const int16_t *samples, size_t sample_count);
hal_err_t hal_audio_out_play_silence_ms(uint32_t duration_ms);

#ifdef __cplusplus
}
#endif

#endif /* HAL_AUDIO_OUT_H */
