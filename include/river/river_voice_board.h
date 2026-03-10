#ifndef AMEBA_RIVER_VOICE_BOARD_H
#define AMEBA_RIVER_VOICE_BOARD_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *board_name;
    const char *geometry_name;
    const char *aivoice_geometry_name;
    uint32_t mic_spacing_mm;
    uint32_t sample_rate;
    uint32_t frame_ms;
    uint32_t capture_channels;
    uint32_t primary_mic;
    uint32_t secondary_mic;
    uint32_t aux_mic;
    uint32_t primary_mic_gain;
    uint32_t secondary_mic_gain;
    uint32_t aux_mic_gain;
    bool aux_mic_reserved;
} river_voice_board_array_profile_t;

const river_voice_board_array_profile_t *river_voice_board_array_profile(void);
const char *river_voice_board_mic_name(uint32_t mic_category);
const char *river_voice_board_mic_gain_name(uint32_t mic_gain);
void river_voice_board_dump_array_profile(void);

#endif
