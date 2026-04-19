/* 应用编排入口：按依赖顺序拉起 Wi-Fi、云端、语音和交互子系统。 */
#include "river/river_app.h"
#include "river/river_cloud.h"
#include "river/river_dialog_cloud_port.h"
#include "river/river_dialog_runtime.h"
#include "river/river_interaction_state.h"
#include "river/river_interaction_diag.h"
#include "river/river_log.h"
#include "river/river_online_control.h"
#include "river/river_playback_service.h"
#include "river/river_reset_trace.h"
#include "river/river_reference_service.h"
#include "river/river_runtime_stats.h"
#include "river/river_voice.h"
#include "river/river_voice_capture.h"
#include "river/river_voice_kws.h"
#include "river/river_voice_profile.h"
#include "river/river_wifi_station.h"

#include "river_session_coordinator.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.app"

static const char *river_app_dialog_cloud_provider_name(void)
{
    return river_cloud_asr_provider_name();
}

static bool river_app_dialog_cloud_asr_streaming_supported(void)
{
    return river_cloud_asr_streaming_supported();
}

static bool river_app_dialog_cloud_asr_batch_supported(void)
{
    return river_cloud_asr_batch_supported();
}

static river_status_t river_app_dialog_cloud_asr_audio_open(
    const river_dialog_asr_audio_desc_t *audio,
    uint32_t pre_roll_ms,
    uint32_t post_roll_ms)
{
    river_cloud_asr_audio_desc_t cloud_audio;

    if (audio == NULL) {
        return RIVER_ERR_ARG;
    }

    cloud_audio.sample_rate = audio->sample_rate;
    cloud_audio.channels = audio->channels;
    cloud_audio.bits_per_sample = audio->bits_per_sample;
    cloud_audio.frame_ms = audio->frame_ms;
    cloud_audio.encoding = audio->encoding;
    return river_cloud_asr_audio_open(&cloud_audio, pre_roll_ms, post_roll_ms);
}

static void river_app_dialog_cloud_asr_audio_close(void)
{
    river_cloud_asr_audio_close();
}

static river_status_t river_app_dialog_cloud_asr_stream_push_frame(const uint8_t *pcm,
                                                                   size_t bytes,
                                                                   bool is_speech)
{
    return river_cloud_asr_stream_push_frame(pcm, bytes, is_speech);
}

static river_status_t river_app_dialog_cloud_asr_batch_submit_segment(
    const uint8_t *pcm,
    size_t bytes,
    const river_voice_segment_desc_t *segment)
{
    return river_cloud_asr_batch_submit_segment(pcm, bytes, segment);
}

static river_status_t river_app_dialog_cloud_interrupt_tts_with_reason(const char *reason)
{
    return river_cloud_adapter_interrupt_tts_with_reason(reason);
}

static bool river_app_dialog_cloud_conversation_window_active(void)
{
    return river_cloud_adapter_conversation_window_active();
}

static const river_dialog_cloud_port_t g_river_app_dialog_cloud_port = {
    .provider_name = river_app_dialog_cloud_provider_name,
    .asr_streaming_supported = river_app_dialog_cloud_asr_streaming_supported,
    .asr_batch_supported = river_app_dialog_cloud_asr_batch_supported,
    .asr_audio_open = river_app_dialog_cloud_asr_audio_open,
    .asr_audio_close = river_app_dialog_cloud_asr_audio_close,
    .asr_stream_push_frame = river_app_dialog_cloud_asr_stream_push_frame,
    .asr_batch_submit_segment = river_app_dialog_cloud_asr_batch_submit_segment,
    .interrupt_tts_with_reason = river_app_dialog_cloud_interrupt_tts_with_reason,
    .conversation_window_active = river_app_dialog_cloud_conversation_window_active,
};

river_status_t river_app_boot(void)
{
    RIVER_LOGI("ameba-river boot");
    RIVER_LOGI("target=RTL8730E");
    river_reset_trace_boot_init();

    /* 先初始化所有基础服务，再绑定跨模块回调关系。 */
    river_runtime_stats_init();
    if (river_interaction_state_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    if (river_playback_service_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    if (river_dialog_runtime_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }

    if (river_wifi_station_init() != RIVER_OK) {
        return RIVER_ERR_UNSUPPORTED;
    }

    if (river_cloud_adapter_init() != RIVER_OK) {
        return RIVER_ERR_UNSUPPORTED;
    }

    if (river_session_coordinator_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    if (river_dialog_cloud_port_register(&g_river_app_dialog_cloud_port) != RIVER_OK) {
        return RIVER_ERR_UNSUPPORTED;
    }
    river_playback_service_register_listener(river_session_coordinator_on_playback_state, NULL);
    river_voice_frontend_set_handler(river_session_coordinator_on_voice_event);
    river_cloud_adapter_set_result_handler(river_session_coordinator_on_cloud_asr_result, NULL);
    river_cloud_adapter_set_state_sync_handler(river_dialog_runtime_on_cloud_state_sync, NULL);

    if (river_voice_frontend_init() != RIVER_OK) {
        return RIVER_ERR_UNSUPPORTED;
    }

    if (river_online_control_init() != RIVER_OK) {
        return RIVER_ERR_UNSUPPORTED;
    }
    if (river_interaction_diag_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }

#ifdef CONFIG_RIVER_AUDIO_ECHO_DIAG_DEFAULT_ON
    river_voice_echo_set_diag_enabled(true);
    RIVER_LOGI("boot audio echo diagnostics enabled");
#endif

#ifdef CONFIG_RIVER_VAD_PROBE_DIAG_DEFAULT_ON
    river_voice_vad_probe_set_diag_enabled(true);
    RIVER_LOGI("boot vad probe diagnostics enabled");
#endif

#ifdef CONFIG_RIVER_VAD_PROBE_AUTOSTART
    RIVER_LOGI("boot vad probe autostart enabled");
    if (river_voice_vad_probe_start() != RIVER_OK) {
        RIVER_LOGE("boot vad probe autostart failed");
    }
#endif

#ifdef CONFIG_RIVER_AUDIO_ECHO_AUTOSTART
    RIVER_LOGI("boot audio echo autostart enabled");
    if (river_voice_echo_start() != RIVER_OK) {
        RIVER_LOGE("boot audio echo autostart failed");
    }
#endif

    river_dialog_runtime_mark_boot_ready("boot_ready");
    river_dialog_runtime_sync_cloud_state("boot_ready");
    river_app_print_status();
    river_runtime_stats_snapshot("boot_ready");
    return RIVER_OK;
}

void river_app_print_status(void)
{
    const char *profile_name;
    const river_voice_profile_config_t *profile;

    /* 统一汇总当前构建配置和各运行时子系统状态，便于串口诊断。 */
    profile = river_voice_profile_active();
    profile_name = river_voice_preproc_profile_name();
    RIVER_LOGI("local_frontend=%s", river_voice_frontend_mode_name());
    RIVER_LOGI("local_preproc=%s", river_voice_preproc_backend_name());
    RIVER_LOGI("local_preproc_profile=%s", profile_name);
    RIVER_LOGI("local_detector=%s", river_voice_detector_backend_name());
    if (profile->uses_native_capture_ref) {
        RIVER_LOGI("local_aec_ref=native_capture_ch3");
    } else {
        RIVER_LOGI("local_playback_ref=%s", river_reference_service_backend_name());
    }
    RIVER_LOGI("local_segment_sink=%s", river_voice_segment_sink_name());
#ifdef CONFIG_RIVER_OFFLINE_ASR_RESERVED
    RIVER_LOGI("offline_asr=reserved");
#else
    RIVER_LOGI("offline_asr=disabled");
#endif
#ifdef CONFIG_RIVER_ONLINE_CONTROL_EN
    RIVER_LOGI("online_control=enabled");
#else
    RIVER_LOGI("online_control=disabled");
#endif
    river_interaction_state_dump_status();
    river_dialog_runtime_dump_status();
    river_reset_trace_dump_status();
    river_playback_service_dump_status();
    river_voice_capture_dump_status();
    river_reference_service_dump_status();
    river_voice_echo_dump_status();
    river_voice_kws_dump_status();
    river_voice_vad_probe_dump_status();
    river_wifi_station_dump_status();
    river_cloud_adapter_dump_status();
    river_online_control_dump_status();
    river_interaction_diag_dump_status();
}
