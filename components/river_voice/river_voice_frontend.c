#include "river/river_log.h"
#include "river/river_voice.h"
#include "river/river_voice_board.h"
#include "river/river_voice_capture.h"
#include "river/river_voice_detector.h"
#include "river/river_voice_experiment.h"
#include "river/river_voice_kws.h"
#include "river/river_voice_preproc.h"
#include "river/river_reference_service.h"
#include "river/river_voice_profile.h"
#include "river/river_voice_segment_sink.h"

#ifndef CONFIG_RIVER_VOICE_CAPABILITY_KWS
#define CONFIG_RIVER_VOICE_CAPABILITY_KWS 0
#endif

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.voice.frontend"

static river_voice_event_handler_t g_river_voice_handler = 0;

river_status_t river_voice_frontend_init(void)
{
    const river_voice_profile_config_t *profile = river_voice_profile_active();

    RIVER_LOGI("frontend init: %s", river_voice_frontend_mode_name());
    river_voice_board_dump_array_profile();
    river_voice_capture_dump_profile();
    river_voice_preproc_dump_profile();
    river_voice_detector_dump_profile();
    river_voice_segment_sink_dump_profile();
    river_voice_experiment_dump_profile();
#if CONFIG_RIVER_VOICE_CAPABILITY_KWS
    {
        river_status_t status = river_voice_kws_init();
        if (status != RIVER_OK) {
            RIVER_LOGW("kws init failed status=%d; continue with stable non-kws path",
                       (int)status);
            RIVER_LOGW("wake admission fallback: idle VAD admission remains enabled because local KWS is inactive");
        } else {
            river_voice_kws_dump_profile();
            RIVER_LOGI("wake-stage validation path: capture -> fixed_dsb -> log_mel -> dscnn_kws -> wake event");
        }
    }
#endif
    if (profile->experimental) {
        RIVER_LOGI("current board path keeps fixed_dsb as the stable beamforming baseline while reserving a dedicated native-3ch WebRTC AECM experiment profile");
        RIVER_LOGI("current validation path remains: capture(2mic+ref) -> fixed_dsb/webrtc_aecm(exp) -> silero -> stream/buffer bridge -> runtime logs");
    } else {
        river_reference_service_dump_profile();
        RIVER_LOGI("current board path uses AMIC1 + AMIC3 dual mic with software fixed delay-and-sum beamforming");
        RIVER_LOGI("pure vad validation path: capture -> fixed_dsb -> silero -> stream/buffer bridge -> runtime logs");
    }
    RIVER_LOGI("board audio echo test: river audio start | river audio stop | river audio status");
    return RIVER_OK;
}

void river_voice_frontend_set_handler(river_voice_event_handler_t handler)
{
    g_river_voice_handler = handler;
}

river_status_t river_voice_frontend_dispatch_event(const river_voice_event_t *event)
{
    if (event == 0) {
        return RIVER_ERR_ARG;
    }
    if (g_river_voice_handler == 0) {
        return RIVER_ERR_NOT_FOUND;
    }
    g_river_voice_handler(event);
    return RIVER_OK;
}

const char *river_voice_frontend_mode_name(void)
{
    return "asr-first";
}
