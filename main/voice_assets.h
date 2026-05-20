#ifndef VOICE_ASSETS_H
#define VOICE_ASSETS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VOICE_ASSET_WARNING_TOO_CLOSE = 0,
    VOICE_ASSET_COUNT,
} voice_asset_id_t;

typedef struct {
    const int16_t *samples;
    size_t sample_count;
} voice_asset_t;

bool voice_assets_get(voice_asset_id_t id, voice_asset_t *out_asset);

#ifdef __cplusplus
}
#endif

#endif /* VOICE_ASSETS_H */
