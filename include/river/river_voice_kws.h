#ifndef AMEBA_RIVER_VOICE_KWS_H
#define AMEBA_RIVER_VOICE_KWS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "river/river_types.h"

#ifdef __cplusplus
extern "C" {
#endif

river_status_t river_voice_kws_init(void);
bool river_voice_kws_active(void);
river_status_t river_voice_kws_submit_frame(const uint8_t *data,
                                            size_t bytes,
                                            bool vad_valid,
                                            bool is_speech);
void river_voice_kws_dump_profile(void);
void river_voice_kws_dump_status(void);

#ifdef __cplusplus
}
#endif

#endif
