#include "voice_assets.h"

typedef struct {
    const uint8_t *start;
    const uint8_t *end;
} embedded_asset_t;

#define DECLARE_ASSET(name) \
    extern const uint8_t _binary_##name##_pcm_start[] asm("_binary_" #name "_pcm_start"); \
    extern const uint8_t _binary_##name##_pcm_end[] asm("_binary_" #name "_pcm_end")

DECLARE_ASSET(warning_too_close);

static const embedded_asset_t s_assets[VOICE_ASSET_COUNT] = {
    [VOICE_ASSET_WARNING_TOO_CLOSE] = {
        _binary_warning_too_close_pcm_start,
        _binary_warning_too_close_pcm_end,
    },
};

bool voice_assets_get(voice_asset_id_t id, voice_asset_t *out_asset)
{
    const embedded_asset_t *src;

    if (!out_asset || id >= VOICE_ASSET_COUNT) {
        return false;
    }

    src = &s_assets[id];
    out_asset->samples = (const int16_t *)src->start;
    out_asset->sample_count = (size_t)(src->end - src->start) / sizeof(int16_t);
    return out_asset->samples != NULL && out_asset->sample_count > 0;
}
