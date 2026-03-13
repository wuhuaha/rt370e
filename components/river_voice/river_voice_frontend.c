#include "river/river_log.h"
#include "river/river_voice.h"
#include "river/river_voice_board.h"
#include "river/river_voice_capture.h"
#include "river/river_voice_detector.h"
#include "river/river_voice_preproc.h"
#include "river/river_voice_ref.h"
#include "river/river_voice_segment_sink.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.voice.frontend"

static river_voice_event_handler_t g_river_voice_handler = 0;

river_status_t river_voice_frontend_init(void)
{
    RIVER_LOGI("frontend init: %s", river_voice_frontend_mode_name());
    river_voice_board_dump_array_profile();
    river_voice_capture_dump_profile();
    river_voice_preproc_dump_profile();
    river_voice_detector_dump_profile();
    river_voice_ref_dump_profile();
    river_voice_segment_sink_dump_profile();
    RIVER_LOGI("current board path uses AMIC1 + AMIC3 dual mic with software fixed delay-and-sum beamforming");
    RIVER_LOGI("pure vad validation path: capture -> fixed_dsb -> silero -> stream/buffer bridge -> runtime logs");
    RIVER_LOGI("board audio echo test: river audio start | river audio stop | river audio status");
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
