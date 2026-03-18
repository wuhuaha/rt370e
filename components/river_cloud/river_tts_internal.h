#ifndef AMEBA_RIVER_TTS_INTERNAL_H
#define AMEBA_RIVER_TTS_INTERNAL_H

#include "river/river_types.h"

river_status_t river_tts_iflytek_init(void);
river_status_t river_tts_iflytek_submit_text(const char *text);
void river_tts_iflytek_dump_status(void);

#endif
