/*
 * Project-owned Audio HAL board overrides.
 *
 * The AmebaSmart SDK usrcfg header defines DMIC pins for the reference board.
 * Include it first, then override only the confirmed Orvibo hardware pins.
 */
#ifndef AMEBA_RIVER_AUDIO_HW_OVERRIDES_H
#define AMEBA_RIVER_AUDIO_HW_OVERRIDES_H

#include "ameba_audio_hw_usrcfg.h"

#undef AUDIO_HW_DMIC_CLK_PIN
#define AUDIO_HW_DMIC_CLK_PIN _PA_2

#undef AUDIO_HW_DMIC_DATA1_PIN
#define AUDIO_HW_DMIC_DATA1_PIN _PA_4

#undef AUDIO_HW_AMPLIFIER_PIN
#define AUDIO_HW_AMPLIFIER_PIN _PB_25

#endif
