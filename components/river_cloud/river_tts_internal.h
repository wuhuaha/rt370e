#ifndef AMEBA_RIVER_TTS_INTERNAL_H
#define AMEBA_RIVER_TTS_INTERNAL_H

#include "river/river_types.h"

river_status_t river_tts_iflytek_init(void);
river_status_t river_tts_iflytek_submit_text(const char *text);
river_status_t river_tts_iflytek_request_stop_with_reason(const char *reason);
river_status_t river_tts_iflytek_request_stop(void);
void river_tts_iflytek_dump_status(void);

#endif
