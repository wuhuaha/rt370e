#include <stdio.h>

#include "river/river_voice.h"
#include "river/river_voice_board.h"
#include "river/river_voice_capture.h"
#include "river/river_voice_detector.h"
#include "river/river_voice_preproc.h"
#include "river/river_voice_ref.h"
#include "river/river_voice_vad_reference.h"

static river_voice_event_handler_t g_river_voice_handler = 0;

river_status_t river_voice_frontend_init(void)
{
    printf("[river][voice] frontend init: %s\n", river_voice_frontend_mode_name());
    river_voice_board_dump_array_profile();
    river_voice_capture_dump_profile();
    river_voice_preproc_dump_profile();
    river_voice_detector_dump_profile();
    river_voice_vad_reference_dump_profile();
    river_voice_ref_dump_profile();
    printf("[river][voice] current board path follows SDK speechmind/aivoice baseline: AMIC1 + AMIC3 dual mic\n");
    printf("[river][voice] pure vad validation path: capture -> aivoice_afe -> silero + sdk_vad_ref -> serial diagnostics\n");
    printf("[river][voice] board audio echo test: river audio start | river audio stop | river audio status\n");
    return RIVER_OK;
}

void river_voice_frontend_set_handler(river_voice_event_handler_t handler)
{
    g_river_voice_handler = handler;
    (void)g_river_voice_handler;
}

const char *river_voice_frontend_mode_name(void)
{
    return "asr-first";
}
