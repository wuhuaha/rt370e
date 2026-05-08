/* Orvibo audio service: capture, VAD/KWS, Opus uplink, and TTS playback. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "audio/audio_control.h"
#include "audio/audio_service.h"
#include "os_wrapper.h"

#include "river/river_log.h"
#include "river/river_opus_codec.h"
#include "river/river_orvibo_audio_service.h"
#include "river/river_playback_service.h"
#include "river/river_reference_service.h"
#include "river/river_voice.h"
#include "river/river_voice_board.h"
#include "river/river_voice_capture.h"
#include "river/river_voice_detector.h"
#include "river/river_voice_frame_pool.h"
#include "river/river_voice_kws.h"
#include "river/river_voice_preproc.h"
#include "river/river_voice_profile.h"
#include "river/river_voice_runtime_policy.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.orvibo.audio"

#ifndef CONFIG_RIVER_VOICE_CAPABILITY_KWS
#define CONFIG_RIVER_VOICE_CAPABILITY_KWS 0
#endif

#define RIVER_ORVIBO_AUDIO_TASK_STACK          (1024U * 32U)
#define RIVER_ORVIBO_AUDIO_TASK_PRIORITY       4U
#define RIVER_ORVIBO_CAPTURE_VOLUME            0x24U
#define RIVER_ORVIBO_CAPTURE_HPF_FC            0U
#define RIVER_ORVIBO_OPUS_FRAME_MS             60U
#define RIVER_ORVIBO_OPUS_PACKET_MAX           1536U
#define RIVER_ORVIBO_PCM_ACCUM_MAX             (RIVER_ORVIBO_OPUS_FRAME_MS * 16U * sizeof(int16_t))
#define RIVER_ORVIBO_PLAYBACK_FALLBACK_RATE_HZ 48000U
#define RIVER_ORVIBO_DOWNLINK_MAX_RATE_HZ      48000U
#define RIVER_ORVIBO_DOWNLINK_MAX_CHANNELS     2U
#define RIVER_ORVIBO_DOWNLINK_PCM_MAX          \
    (((RIVER_ORVIBO_DOWNLINK_MAX_RATE_HZ * RIVER_ORVIBO_OPUS_FRAME_MS) / 1000U) * \
     RIVER_ORVIBO_DOWNLINK_MAX_CHANNELS * sizeof(int16_t))
#define RIVER_ORVIBO_DOWNLINK_MONO_MAX         \
    (((RIVER_ORVIBO_DOWNLINK_MAX_RATE_HZ * RIVER_ORVIBO_OPUS_FRAME_MS) / 1000U) * sizeof(int16_t))
#define RIVER_ORVIBO_DOWNLINK_STEREO_MAX       (RIVER_ORVIBO_DOWNLINK_MONO_MAX * 2U)
#define RIVER_ORVIBO_DOWNLINK_BUFFER_HIGH_WATER_PCT 95U
#define RIVER_ORVIBO_TTS_BUFFER_FRAMES         24U
#define RIVER_ORVIBO_DIAG_LOG_INTERVAL_MS      5000U
#define RIVER_ORVIBO_PLAYBACK_DRAIN_POLL_MS    20U
#define RIVER_ORVIBO_RTOS_OK                   0

typedef struct {
    bool initialized;
    bool running;
    bool stop_requested;
    bool vad_state_initialized;
    bool vad_is_speech;
    bool playback_started;
    bool barge_in_enabled;
    river_orvibo_audio_mode_t mode;
    rtos_task_t task;
    rtos_mutex_t lock;
    river_voice_capture_t capture;
    river_voice_preproc_t preproc;
    river_voice_detector_t detector;
    river_opus_encoder_t encoder;
    river_opus_decoder_t decoder;
    river_orvibo_audio_event_handler_t event_handler;
    void *event_handler_user;
    uint8_t *capture_buffer;
    uint8_t *enhanced_buffer;
    size_t capture_chunk_bytes;
    size_t enhanced_chunk_bytes;
    uint8_t pcm_accum[RIVER_ORVIBO_PCM_ACCUM_MAX];
    size_t pcm_accum_bytes;
    uint8_t opus_packet[RIVER_ORVIBO_OPUS_PACKET_MAX];
    int16_t downlink_pcm[RIVER_ORVIBO_DOWNLINK_PCM_MAX / sizeof(int16_t)];
    int16_t downlink_mono[RIVER_ORVIBO_DOWNLINK_MONO_MAX / sizeof(int16_t)];
    int16_t downlink_playback_pcm[RIVER_ORVIBO_DOWNLINK_MONO_MAX / sizeof(int16_t)];
    int16_t downlink_stereo[RIVER_ORVIBO_DOWNLINK_STEREO_MAX / sizeof(int16_t)];
    uint32_t last_downlink_sample_rate;
    uint32_t last_downlink_channels;
    uint32_t last_playback_sample_rate;
    uint32_t playback_stream_sample_rate;
    uint32_t playback_stream_frame_ms;
    size_t playback_stream_mono_bytes;
    uint32_t uplink_sequence;
    uint32_t capture_ok;
    uint32_t capture_fail;
    uint32_t preproc_ok;
    uint32_t preproc_fail;
    uint32_t vad_ok;
    uint32_t vad_fail;
    uint32_t vad_speech_start;
    uint32_t vad_speech_end;
    uint32_t kws_submit_ok;
    uint32_t kws_submit_fail;
    uint32_t encode_ok;
    uint32_t encode_fail;
    uint32_t decode_ok;
    uint32_t decode_fail;
    uint32_t resample_ok;
    uint32_t resample_bypass;
    uint32_t resample_fail;
    uint32_t playback_write_ok;
    uint32_t playback_write_fail;
    uint32_t downlink_backpressure_events;
    uint32_t downlink_backpressure_high_water;
    size_t downlink_buffered_bytes;
    size_t downlink_buffer_size_bytes;
    uint16_t vad_probability_q15;
    uint16_t vad_probability_raw_q15;
    uint64_t last_diag_log_ms;
} river_orvibo_audio_context_t;

static river_orvibo_audio_context_t g_river_orvibo_audio;

static void river_orvibo_audio_emit(const river_orvibo_audio_event_t *event)
{
    if (event == NULL || g_river_orvibo_audio.event_handler == NULL) {
        return;
    }
    g_river_orvibo_audio.event_handler(event, g_river_orvibo_audio.event_handler_user);
}

static void river_orvibo_audio_emit_simple(river_orvibo_audio_event_type_t type)
{
    river_orvibo_audio_event_t event;

    memset(&event, 0, sizeof(event));
    event.type = type;
    event.vad_probability_q15 = g_river_orvibo_audio.vad_probability_q15;
    event.vad_probability_raw_q15 = g_river_orvibo_audio.vad_probability_raw_q15;
    event.is_speech = g_river_orvibo_audio.vad_is_speech;
    river_orvibo_audio_emit(&event);
}

static void river_orvibo_voice_event_handler(const river_voice_event_t *event)
{
    river_orvibo_audio_event_t out_event;

    if (event == NULL || event->type != RIVER_VOICE_EVENT_WAKEWORD) {
        return;
    }
    RIVER_LOGI("wake bridge: text=%s q15=%d mode=%s vad_speech=%s vad_prob=%u/%u",
               event->text != NULL && event->text[0] != '\0' ? event->text : "-",
               event->confidence,
               river_orvibo_audio_mode_name(g_river_orvibo_audio.mode),
               g_river_orvibo_audio.vad_is_speech ? "yes" : "no",
               (unsigned int)g_river_orvibo_audio.vad_probability_raw_q15,
               (unsigned int)g_river_orvibo_audio.vad_probability_q15);
    memset(&out_event, 0, sizeof(out_event));
    out_event.type = RIVER_ORVIBO_AUDIO_EVENT_WAKE_DETECTED;
    out_event.text = event->text;
    river_orvibo_audio_emit(&out_event);
}

const char *river_orvibo_audio_mode_name(river_orvibo_audio_mode_t mode)
{
    switch (mode) {
    case RIVER_ORVIBO_AUDIO_MODE_IDLE:
        return "idle";
    case RIVER_ORVIBO_AUDIO_MODE_LISTENING:
        return "listening";
    case RIVER_ORVIBO_AUDIO_MODE_SPEAKING:
        return "speaking";
    default:
        return "unknown";
    }
}

static void river_orvibo_audio_apply_runtime_policy(river_orvibo_audio_mode_t mode)
{
    switch (mode) {
    case RIVER_ORVIBO_AUDIO_MODE_IDLE:
        river_voice_kws_set_detection_gate(true, "orvibo_idle");
        river_voice_runtime_set_interaction_state(RIVER_VOICE_RUNTIME_INTERACTION_WAKE_MONITORING);
        river_voice_runtime_set_playback_owner(RIVER_VOICE_RUNTIME_PLAYBACK_OWNER_NONE);
        break;
    case RIVER_ORVIBO_AUDIO_MODE_LISTENING:
        river_voice_kws_set_detection_gate(false, "orvibo_listening");
        river_voice_runtime_set_interaction_state(RIVER_VOICE_RUNTIME_INTERACTION_LISTENING);
        river_voice_runtime_set_playback_owner(RIVER_VOICE_RUNTIME_PLAYBACK_OWNER_NONE);
        break;
    case RIVER_ORVIBO_AUDIO_MODE_SPEAKING:
        river_voice_kws_set_detection_gate(g_river_orvibo_audio.barge_in_enabled,
                                           g_river_orvibo_audio.barge_in_enabled ?
                                               "orvibo_barge_in" :
                                               "orvibo_speaking");
        river_voice_runtime_set_interaction_state(
            g_river_orvibo_audio.barge_in_enabled ?
                RIVER_VOICE_RUNTIME_INTERACTION_BARGE_IN_LISTENING :
                RIVER_VOICE_RUNTIME_INTERACTION_SPEAKING);
        river_voice_runtime_set_playback_owner(RIVER_VOICE_RUNTIME_PLAYBACK_OWNER_ORVIBO);
        break;
    default:
        break;
    }
}

static void river_orvibo_audio_log_diag_if_needed(void)
{
    uint64_t now_ms = (uint64_t)rtos_time_get_current_system_time_ms();

    if (g_river_orvibo_audio.last_diag_log_ms != 0U &&
        now_ms >= g_river_orvibo_audio.last_diag_log_ms &&
        (now_ms - g_river_orvibo_audio.last_diag_log_ms) < RIVER_ORVIBO_DIAG_LOG_INTERVAL_MS) {
        return;
    }
    g_river_orvibo_audio.last_diag_log_ms = now_ms;
    RIVER_LOGI("audio diag: mode=%s cap=%lu/%lu pre=%lu/%lu vad=%lu/%lu speech=%s prob=%u/%u kws=%lu/%lu enc=%lu/%lu dec=%lu/%lu rs=%lu/%lu/%lu rate=%lu/%lu->%lu play=%lu/%lu bp_evt=%lu buf=%lu/%lu",
               river_orvibo_audio_mode_name(g_river_orvibo_audio.mode),
               (unsigned long)g_river_orvibo_audio.capture_ok,
               (unsigned long)g_river_orvibo_audio.capture_fail,
               (unsigned long)g_river_orvibo_audio.preproc_ok,
               (unsigned long)g_river_orvibo_audio.preproc_fail,
               (unsigned long)g_river_orvibo_audio.vad_ok,
               (unsigned long)g_river_orvibo_audio.vad_fail,
               g_river_orvibo_audio.vad_is_speech ? "yes" : "no",
               (unsigned int)g_river_orvibo_audio.vad_probability_raw_q15,
               (unsigned int)g_river_orvibo_audio.vad_probability_q15,
               (unsigned long)g_river_orvibo_audio.kws_submit_ok,
               (unsigned long)g_river_orvibo_audio.kws_submit_fail,
               (unsigned long)g_river_orvibo_audio.encode_ok,
               (unsigned long)g_river_orvibo_audio.encode_fail,
               (unsigned long)g_river_orvibo_audio.decode_ok,
               (unsigned long)g_river_orvibo_audio.decode_fail,
               (unsigned long)g_river_orvibo_audio.resample_ok,
               (unsigned long)g_river_orvibo_audio.resample_bypass,
               (unsigned long)g_river_orvibo_audio.resample_fail,
               (unsigned long)g_river_orvibo_audio.last_downlink_sample_rate,
               (unsigned long)g_river_orvibo_audio.last_downlink_channels,
               (unsigned long)g_river_orvibo_audio.last_playback_sample_rate,
               (unsigned long)g_river_orvibo_audio.playback_write_ok,
               (unsigned long)g_river_orvibo_audio.playback_write_fail,
               (unsigned long)g_river_orvibo_audio.downlink_backpressure_events,
               (unsigned long)g_river_orvibo_audio.downlink_buffered_bytes,
               (unsigned long)g_river_orvibo_audio.downlink_buffer_size_bytes);
}

static bool river_orvibo_audio_speaking_uplink_allowed(void)
{
    river_voice_preproc_profile_t profile = river_voice_profile_active_preproc();

    return g_river_orvibo_audio.barge_in_enabled &&
           (river_voice_profile_has_capability(profile, RIVER_VOICE_CAPABILITY_AEC) ||
            river_voice_profile_has_capability(profile,
                                              RIVER_VOICE_CAPABILITY_NATIVE_CAPTURE_REF));
}

static void river_orvibo_audio_reset_vad_state(void)
{
    g_river_orvibo_audio.vad_state_initialized = false;
    g_river_orvibo_audio.vad_is_speech = false;
    g_river_orvibo_audio.vad_probability_q15 = 0U;
    g_river_orvibo_audio.vad_probability_raw_q15 = 0U;
}

static void river_orvibo_audio_handle_vad(const river_voice_detector_result_t *result)
{
    bool previous;

    if (result == NULL || !result->decision_valid) {
        return;
    }
    previous = g_river_orvibo_audio.vad_is_speech;
    g_river_orvibo_audio.vad_state_initialized = true;
    g_river_orvibo_audio.vad_is_speech = result->is_speech;
    g_river_orvibo_audio.vad_probability_raw_q15 = result->speech_probability_raw_q15;
    g_river_orvibo_audio.vad_probability_q15 = result->speech_probability_q15;
    if (result->is_speech && !previous) {
        g_river_orvibo_audio.vad_speech_start++;
        if (g_river_orvibo_audio.mode == RIVER_ORVIBO_AUDIO_MODE_LISTENING ||
            (g_river_orvibo_audio.mode == RIVER_ORVIBO_AUDIO_MODE_SPEAKING &&
             g_river_orvibo_audio.barge_in_enabled)) {
            river_orvibo_audio_emit_simple(RIVER_ORVIBO_AUDIO_EVENT_SPEECH_STARTED);
        }
    } else if (!result->is_speech && previous) {
        g_river_orvibo_audio.vad_speech_end++;
        if (g_river_orvibo_audio.mode == RIVER_ORVIBO_AUDIO_MODE_LISTENING) {
            river_orvibo_audio_emit_simple(RIVER_ORVIBO_AUDIO_EVENT_SPEECH_ENDED);
        }
    }
}

static void river_orvibo_audio_emit_uplink_packet(const uint8_t *packet,
                                                  size_t packet_bytes,
                                                  uint32_t timestamp_ms)
{
    river_orvibo_audio_event_t event;

    memset(&event, 0, sizeof(event));
    event.type = RIVER_ORVIBO_AUDIO_EVENT_UPLINK_PACKET;
    event.packet = packet;
    event.packet_bytes = packet_bytes;
    event.timestamp_ms = timestamp_ms;
    event.vad_probability_q15 = g_river_orvibo_audio.vad_probability_q15;
    event.vad_probability_raw_q15 = g_river_orvibo_audio.vad_probability_raw_q15;
    event.is_speech = g_river_orvibo_audio.vad_is_speech;
    river_orvibo_audio_emit(&event);
}

static void river_orvibo_audio_try_encode(const uint8_t *pcm, size_t pcm_bytes)
{
    size_t frame_bytes;
    size_t copy_bytes;
    size_t offset = 0U;

    if (g_river_orvibo_audio.encoder.handle == NULL) {
        return;
    }
    frame_bytes = g_river_orvibo_audio.encoder.pcm_frame_bytes;
    while (offset < pcm_bytes) {
        copy_bytes = frame_bytes - g_river_orvibo_audio.pcm_accum_bytes;
        if (copy_bytes > (pcm_bytes - offset)) {
            copy_bytes = pcm_bytes - offset;
        }
        memcpy(g_river_orvibo_audio.pcm_accum + g_river_orvibo_audio.pcm_accum_bytes,
               pcm + offset,
               copy_bytes);
        g_river_orvibo_audio.pcm_accum_bytes += copy_bytes;
        offset += copy_bytes;
        if (g_river_orvibo_audio.pcm_accum_bytes == frame_bytes) {
            size_t packet_bytes = 0U;
            uint32_t timestamp_ms =
                g_river_orvibo_audio.uplink_sequence * RIVER_ORVIBO_OPUS_FRAME_MS;

            if (river_opus_encode(&g_river_orvibo_audio.encoder,
                                  (const int16_t *)g_river_orvibo_audio.pcm_accum,
                                  frame_bytes,
                                  g_river_orvibo_audio.opus_packet,
                                  sizeof(g_river_orvibo_audio.opus_packet),
                                  &packet_bytes) == RIVER_OK) {
                g_river_orvibo_audio.encode_ok++;
                river_orvibo_audio_emit_uplink_packet(g_river_orvibo_audio.opus_packet,
                                                      packet_bytes,
                                                      timestamp_ms);
            } else {
                g_river_orvibo_audio.encode_fail++;
            }
            g_river_orvibo_audio.uplink_sequence++;
            g_river_orvibo_audio.pcm_accum_bytes = 0U;
        }
    }
}

static river_status_t river_orvibo_audio_prepare_buffers(void)
{
    river_voice_frame_pool_layout_t layout;
    river_voice_frame_pool_view_t view;

    memset(&layout, 0, sizeof(layout));
    layout.capture_bytes = g_river_orvibo_audio.capture_chunk_bytes;
    layout.enhanced_bytes = g_river_orvibo_audio.enhanced_chunk_bytes;
    if (river_voice_frame_pool_acquire(&layout, &view) != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    g_river_orvibo_audio.capture_buffer = view.capture_buffer;
    g_river_orvibo_audio.enhanced_buffer = view.enhanced_buffer;
    return RIVER_OK;
}

static river_status_t river_orvibo_audio_open(void)
{
    const river_voice_board_array_profile_t *board = river_voice_board_array_profile();

    AudioService_Init();
    AudioControl_SetCaptureVolume(board->capture_channels, RIVER_ORVIBO_CAPTURE_VOLUME);
    AudioControl_SetCaptureHpfFc(0, RIVER_ORVIBO_CAPTURE_HPF_FC);

    if (river_voice_capture_open(&g_river_orvibo_audio.capture) != RIVER_OK) {
        return RIVER_ERR_UNSUPPORTED;
    }
    if (river_voice_preproc_open(&g_river_orvibo_audio.preproc) != RIVER_OK) {
        return RIVER_ERR_UNSUPPORTED;
    }
    if (river_voice_detector_open(&g_river_orvibo_audio.detector) != RIVER_OK) {
        return RIVER_ERR_UNSUPPORTED;
    }
    if (river_voice_detector_input_frame_bytes(&g_river_orvibo_audio.detector) !=
        river_voice_preproc_output_frame_bytes(&g_river_orvibo_audio.preproc)) {
        RIVER_LOGE("detector/preproc frame mismatch: detector=%luB preproc=%luB",
                   (unsigned long)river_voice_detector_input_frame_bytes(
                       &g_river_orvibo_audio.detector),
                   (unsigned long)river_voice_preproc_output_frame_bytes(
                       &g_river_orvibo_audio.preproc));
        return RIVER_ERR_UNSUPPORTED;
    }
    g_river_orvibo_audio.capture_chunk_bytes = g_river_orvibo_audio.capture.frame_bytes;
    g_river_orvibo_audio.enhanced_chunk_bytes =
        river_voice_preproc_output_frame_bytes(&g_river_orvibo_audio.preproc);
    if (g_river_orvibo_audio.enhanced_chunk_bytes > RIVER_ORVIBO_PCM_ACCUM_MAX) {
        return RIVER_ERR_UNSUPPORTED;
    }
    if (river_orvibo_audio_prepare_buffers() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    if (river_opus_encoder_open(&g_river_orvibo_audio.encoder,
                                16000U,
                                1U,
                                RIVER_ORVIBO_OPUS_FRAME_MS,
                                16000) != RIVER_OK) {
        return RIVER_ERR_UNSUPPORTED;
    }
    RIVER_LOGI("audio open: task_stack=%u capture=%luHz/%luch/%lums preproc=%s detector=%s opus=%lums packet_max=%u",
               (unsigned int)RIVER_ORVIBO_AUDIO_TASK_STACK,
               (unsigned long)g_river_orvibo_audio.capture.sample_rate,
               (unsigned long)g_river_orvibo_audio.capture.channels,
               (unsigned long)g_river_orvibo_audio.capture.frame_ms,
               river_voice_preproc_backend_name(),
               river_voice_detector_backend_name(),
               (unsigned long)RIVER_ORVIBO_OPUS_FRAME_MS,
               (unsigned int)RIVER_ORVIBO_OPUS_PACKET_MAX);
    return RIVER_OK;
}

static void river_orvibo_audio_close(void)
{
    river_opus_encoder_close(&g_river_orvibo_audio.encoder);
    river_opus_decoder_close(&g_river_orvibo_audio.decoder);
    river_voice_detector_close(&g_river_orvibo_audio.detector);
    river_voice_preproc_close(&g_river_orvibo_audio.preproc);
    river_voice_capture_close(&g_river_orvibo_audio.capture);
    river_voice_frame_pool_release();
    g_river_orvibo_audio.capture_buffer = NULL;
    g_river_orvibo_audio.enhanced_buffer = NULL;
}

static void river_orvibo_audio_task(void *param)
{
    (void)param;

    if (river_orvibo_audio_open() != RIVER_OK) {
        g_river_orvibo_audio.running = false;
        river_orvibo_audio_emit_simple(RIVER_ORVIBO_AUDIO_EVENT_ERROR);
        rtos_task_delete(NULL);
        return;
    }
    while (!g_river_orvibo_audio.stop_requested) {
        int32_t bytes_read;
        size_t enhanced_bytes = 0U;
        river_voice_detector_result_t detector_result;

        bytes_read = river_voice_capture_read(&g_river_orvibo_audio.capture,
                                              g_river_orvibo_audio.capture_buffer,
                                              g_river_orvibo_audio.capture_chunk_bytes);
        if ((size_t)bytes_read != g_river_orvibo_audio.capture_chunk_bytes) {
            g_river_orvibo_audio.capture_fail++;
            river_orvibo_audio_log_diag_if_needed();
            continue;
        }
        g_river_orvibo_audio.capture_ok++;
        if (river_voice_preproc_process(&g_river_orvibo_audio.preproc,
                                        g_river_orvibo_audio.capture_buffer,
                                        g_river_orvibo_audio.capture_chunk_bytes,
                                        NULL,
                                        0U,
                                        g_river_orvibo_audio.enhanced_buffer,
                                        g_river_orvibo_audio.enhanced_chunk_bytes,
                                        &enhanced_bytes) != RIVER_OK) {
            g_river_orvibo_audio.preproc_fail++;
            river_orvibo_audio_log_diag_if_needed();
            continue;
        }
        g_river_orvibo_audio.preproc_ok++;
        if (enhanced_bytes < g_river_orvibo_audio.enhanced_chunk_bytes) {
            memset(g_river_orvibo_audio.enhanced_buffer + enhanced_bytes,
                   0,
                   g_river_orvibo_audio.enhanced_chunk_bytes - enhanced_bytes);
            enhanced_bytes = g_river_orvibo_audio.enhanced_chunk_bytes;
        }
        memset(&detector_result, 0, sizeof(detector_result));
        if (river_voice_detector_process(&g_river_orvibo_audio.detector,
                                         g_river_orvibo_audio.enhanced_buffer,
                                         enhanced_bytes,
                                         &detector_result) != RIVER_OK) {
            g_river_orvibo_audio.vad_fail++;
            river_orvibo_audio_log_diag_if_needed();
            continue;
        }
        g_river_orvibo_audio.vad_ok++;
        river_orvibo_audio_handle_vad(&detector_result);

        if (river_voice_kws_active()) {
            river_status_t kws_status =
                river_voice_kws_submit_frame(g_river_orvibo_audio.enhanced_buffer,
                                             enhanced_bytes,
                                             detector_result.decision_valid,
                                             detector_result.is_speech);
            if (kws_status == RIVER_OK) {
                g_river_orvibo_audio.kws_submit_ok++;
            } else {
                g_river_orvibo_audio.kws_submit_fail++;
            }
        }
        if (g_river_orvibo_audio.mode == RIVER_ORVIBO_AUDIO_MODE_LISTENING ||
            (g_river_orvibo_audio.mode == RIVER_ORVIBO_AUDIO_MODE_SPEAKING &&
             river_orvibo_audio_speaking_uplink_allowed())) {
            river_orvibo_audio_try_encode(g_river_orvibo_audio.enhanced_buffer,
                                          enhanced_bytes);
        }
        river_orvibo_audio_log_diag_if_needed();
    }

    river_orvibo_audio_close();
    g_river_orvibo_audio.running = false;
    g_river_orvibo_audio.stop_requested = false;
    g_river_orvibo_audio.task = 0;
    rtos_task_delete(NULL);
}

river_status_t river_orvibo_audio_service_init(void)
{
    if (g_river_orvibo_audio.initialized) {
        return RIVER_OK;
    }
    memset(&g_river_orvibo_audio, 0, sizeof(g_river_orvibo_audio));
    if (rtos_mutex_create(&g_river_orvibo_audio.lock) != RIVER_ORVIBO_RTOS_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    g_river_orvibo_audio.initialized = true;
    g_river_orvibo_audio.mode = RIVER_ORVIBO_AUDIO_MODE_IDLE;
    river_voice_frontend_set_handler(river_orvibo_voice_event_handler);
#if CONFIG_RIVER_VOICE_CAPABILITY_KWS
    if (river_voice_kws_init() != RIVER_OK) {
        RIVER_LOGW("kws init failed; wake word disabled");
    } else {
        river_voice_kws_dump_profile();
    }
#endif
    river_orvibo_audio_apply_runtime_policy(RIVER_ORVIBO_AUDIO_MODE_IDLE);
    return RIVER_OK;
}

river_status_t river_orvibo_audio_service_set_event_handler(
    river_orvibo_audio_event_handler_t handler,
    void *user_data)
{
    if (river_orvibo_audio_service_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    g_river_orvibo_audio.event_handler = handler;
    g_river_orvibo_audio.event_handler_user = user_data;
    return RIVER_OK;
}

river_status_t river_orvibo_audio_service_start(void)
{
    if (river_orvibo_audio_service_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    if (g_river_orvibo_audio.running) {
        return RIVER_OK;
    }
    g_river_orvibo_audio.running = true;
    if (rtos_task_create(&g_river_orvibo_audio.task,
                         "orvibo_audio",
                         river_orvibo_audio_task,
                         NULL,
                         RIVER_ORVIBO_AUDIO_TASK_STACK,
                         RIVER_ORVIBO_AUDIO_TASK_PRIORITY) != RIVER_ORVIBO_RTOS_OK) {
        g_river_orvibo_audio.running = false;
        return RIVER_ERR_NO_MEMORY;
    }
    return RIVER_OK;
}

river_status_t river_orvibo_audio_service_set_mode(river_orvibo_audio_mode_t mode)
{
    if (river_orvibo_audio_service_init() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }
    if (rtos_mutex_take(g_river_orvibo_audio.lock, MUTEX_WAIT_TIMEOUT) != RIVER_ORVIBO_RTOS_OK) {
        return RIVER_ERR_BUSY;
    }
    if (g_river_orvibo_audio.mode != mode) {
        RIVER_LOGI("audio mode: %s -> %s",
                   river_orvibo_audio_mode_name(g_river_orvibo_audio.mode),
                   river_orvibo_audio_mode_name(mode));
        if (mode != RIVER_ORVIBO_AUDIO_MODE_SPEAKING) {
            g_river_orvibo_audio.barge_in_enabled = false;
        }
        g_river_orvibo_audio.mode = mode;
        g_river_orvibo_audio.pcm_accum_bytes = 0U;
        if (mode == RIVER_ORVIBO_AUDIO_MODE_IDLE) {
            river_orvibo_audio_reset_vad_state();
        }
        river_orvibo_audio_apply_runtime_policy(mode);
    }
    rtos_mutex_give(g_river_orvibo_audio.lock);
    return RIVER_OK;
}

void river_orvibo_audio_service_set_barge_in_enabled(bool enabled)
{
    g_river_orvibo_audio.barge_in_enabled = enabled;
    if (g_river_orvibo_audio.mode == RIVER_ORVIBO_AUDIO_MODE_SPEAKING) {
        river_orvibo_audio_apply_runtime_policy(RIVER_ORVIBO_AUDIO_MODE_SPEAKING);
    }
    RIVER_LOGI("audio barge-in: %s", enabled ? "enabled" : "disabled");
}

river_orvibo_audio_mode_t river_orvibo_audio_service_mode(void)
{
    return g_river_orvibo_audio.mode;
}

static void river_orvibo_audio_expand_stereo(const int16_t *mono,
                                             size_t mono_bytes,
                                             int16_t *stereo,
                                             size_t stereo_capacity_bytes)
{
    size_t samples = mono_bytes / sizeof(int16_t);
    size_t index;

    if ((samples * 2U * sizeof(int16_t)) > stereo_capacity_bytes) {
        return;
    }
    for (index = 0U; index < samples; ++index) {
        stereo[index * 2U] = mono[index];
        stereo[index * 2U + 1U] = mono[index];
    }
}

static river_status_t river_orvibo_audio_downmix_to_mono(const int16_t *input,
                                                         size_t input_bytes,
                                                         uint32_t channels,
                                                         int16_t *mono,
                                                         size_t mono_capacity_bytes,
                                                         size_t *mono_bytes)
{
    size_t frame_count;
    size_t index;

    if (mono_bytes != NULL) {
        *mono_bytes = 0U;
    }
    if (input == NULL || mono == NULL || mono_bytes == NULL ||
        input_bytes == 0U || channels == 0U) {
        return RIVER_ERR_ARG;
    }
    if (channels == 1U) {
        if (input_bytes > mono_capacity_bytes) {
            return RIVER_ERR_NO_MEMORY;
        }
        memcpy(mono, input, input_bytes);
        *mono_bytes = input_bytes;
        return RIVER_OK;
    }
    if (channels != 2U ||
        (input_bytes % (channels * sizeof(int16_t))) != 0U) {
        return RIVER_ERR_UNSUPPORTED;
    }

    frame_count = input_bytes / (channels * sizeof(int16_t));
    if ((frame_count * sizeof(int16_t)) > mono_capacity_bytes) {
        return RIVER_ERR_NO_MEMORY;
    }
    for (index = 0U; index < frame_count; ++index) {
        int32_t left = input[index * 2U];
        int32_t right = input[index * 2U + 1U];
        mono[index] = (int16_t)((left + right) / 2);
    }
    *mono_bytes = frame_count * sizeof(int16_t);
    return RIVER_OK;
}

static bool river_orvibo_audio_playback_rate_supported(uint32_t sample_rate)
{
    switch (sample_rate) {
    case 16000U:
    case 44100U:
    case 48000U:
    case 96000U:
    case 192000U:
        return true;
    default:
        return false;
    }
}

static uint32_t river_orvibo_audio_select_playback_sample_rate(uint32_t sample_rate)
{
    if (river_orvibo_audio_playback_rate_supported(sample_rate)) {
        return sample_rate;
    }
    return RIVER_ORVIBO_PLAYBACK_FALLBACK_RATE_HZ;
}

static river_status_t river_orvibo_audio_resample_mono(const int16_t *input,
                                                       size_t input_bytes,
                                                       uint32_t input_rate,
                                                       int16_t *output,
                                                       size_t output_capacity_bytes,
                                                       uint32_t output_rate,
                                                       size_t *output_bytes)
{
    size_t input_samples;
    uint64_t output_samples64;
    size_t output_samples;
    size_t index;

    if (input == NULL || output == NULL || output_bytes == NULL ||
        input_bytes == 0U || input_rate == 0U || output_rate == 0U) {
        return RIVER_ERR_ARG;
    }
    if ((input_bytes % sizeof(int16_t)) != 0U) {
        return RIVER_ERR_ARG;
    }

    input_samples = input_bytes / sizeof(int16_t);
    output_samples64 = ((uint64_t)input_samples * (uint64_t)output_rate) /
                       (uint64_t)input_rate;
    if (output_samples64 == 0U ||
        output_samples64 > (uint64_t)(output_capacity_bytes / sizeof(int16_t))) {
        return RIVER_ERR_NO_MEMORY;
    }
    output_samples = (size_t)output_samples64;

    if (input_rate == output_rate) {
        memcpy(output, input, input_samples * sizeof(int16_t));
        *output_bytes = input_samples * sizeof(int16_t);
        return RIVER_OK;
    }

    for (index = 0U; index < output_samples; ++index) {
        uint64_t src_pos = (uint64_t)index * (uint64_t)input_rate;
        size_t src_index = (size_t)(src_pos / (uint64_t)output_rate);
        uint32_t fraction = (uint32_t)(src_pos % (uint64_t)output_rate);
        int32_t current;
        int32_t next;
        int32_t mixed;

        if (src_index >= input_samples) {
            src_index = input_samples - 1U;
        }
        current = input[src_index];
        if ((src_index + 1U) < input_samples) {
            next = input[src_index + 1U];
        } else {
            next = current;
        }
        mixed = current +
                (int32_t)(((int64_t)(next - current) * (int64_t)fraction) /
                          (int64_t)output_rate);
        if (mixed > 32767) {
            mixed = 32767;
        } else if (mixed < -32768) {
            mixed = -32768;
        }
        output[index] = (int16_t)mixed;
    }

    *output_bytes = output_samples * sizeof(int16_t);
    return RIVER_OK;
}

static river_status_t river_orvibo_audio_start_playback_if_needed(uint32_t sample_rate,
                                                                  uint32_t frame_duration_ms,
                                                                  size_t mono_bytes)
{
    river_playback_stream_config_t config;

    if (g_river_orvibo_audio.playback_started && river_playback_service_active()) {
        if (g_river_orvibo_audio.playback_stream_sample_rate == sample_rate &&
            g_river_orvibo_audio.playback_stream_frame_ms == frame_duration_ms &&
            g_river_orvibo_audio.playback_stream_mono_bytes == mono_bytes) {
            return RIVER_OK;
        }
        RIVER_LOGW("orvibo playback format changed: old=%luHz/%lums/%luB new=%luHz/%lums/%luB",
                   (unsigned long)g_river_orvibo_audio.playback_stream_sample_rate,
                   (unsigned long)g_river_orvibo_audio.playback_stream_frame_ms,
                   (unsigned long)g_river_orvibo_audio.playback_stream_mono_bytes,
                   (unsigned long)sample_rate,
                   (unsigned long)frame_duration_ms,
                   (unsigned long)mono_bytes);
        (void)river_playback_service_stop_stream_ex("orvibo_tts_format_change");
        g_river_orvibo_audio.playback_started = false;
        g_river_orvibo_audio.playback_stream_sample_rate = 0U;
        g_river_orvibo_audio.playback_stream_frame_ms = 0U;
        g_river_orvibo_audio.playback_stream_mono_bytes = 0U;
    }
    memset(&config, 0, sizeof(config));
    config.stream_name = "orvibo_tts";
    config.priority = RIVER_PLAYBACK_PRIO_TTS;
    config.sample_rate = sample_rate;
    config.frame_ms = frame_duration_ms;
    config.playback_channels = 2U;
    config.bits_per_sample = 16U;
    config.playback_frame_bytes = mono_bytes * 2U;
    config.buffer_frame_count = RIVER_ORVIBO_TTS_BUFFER_FRAMES;
    config.volume_left = 0.8f;
    config.volume_right = 0.8f;
    config.reference_export = true;
    config.reference_channels = 1U;
    config.reference_frame_bytes = mono_bytes;
    config.reference_history_ms = 600U;
    config.disable_track_reuse = false;
    config.defer_start_until_prefilled = false;
    if (river_playback_service_start_stream(&config) != RIVER_OK) {
        g_river_orvibo_audio.playback_write_fail++;
        return RIVER_ERR_BUSY;
    }
    g_river_orvibo_audio.playback_started = true;
    g_river_orvibo_audio.playback_stream_sample_rate = sample_rate;
    g_river_orvibo_audio.playback_stream_frame_ms = frame_duration_ms;
    g_river_orvibo_audio.playback_stream_mono_bytes = mono_bytes;
    river_orvibo_audio_emit_simple(RIVER_ORVIBO_AUDIO_EVENT_PLAYBACK_STARTED);
    return RIVER_OK;
}

static void river_orvibo_audio_downlink_buffer_high_water(size_t playback_bytes)
{
    river_playback_service_stats_t stats;
    size_t high_water_bytes;

    river_playback_service_get_stats(&stats);
    g_river_orvibo_audio.downlink_buffered_bytes = stats.buffered_bytes;
    g_river_orvibo_audio.downlink_buffer_size_bytes = stats.buffer_size_bytes;
    if (stats.state != RIVER_PLAYBACK_RUNNING ||
        stats.buffer_size_bytes == 0U ||
        playback_bytes == 0U) {
        return;
    }

    high_water_bytes =
        (stats.buffer_size_bytes * RIVER_ORVIBO_DOWNLINK_BUFFER_HIGH_WATER_PCT) / 100U;
    if (high_water_bytes < playback_bytes) {
        high_water_bytes = stats.buffer_size_bytes;
    }
    if (stats.buffered_bytes + playback_bytes <= high_water_bytes) {
        return;
    }

    g_river_orvibo_audio.downlink_backpressure_events++;
    g_river_orvibo_audio.downlink_backpressure_high_water = (uint32_t)high_water_bytes;
    if (g_river_orvibo_audio.downlink_backpressure_events <= 3U ||
        (g_river_orvibo_audio.downlink_backpressure_events % 20U) == 0U) {
        RIVER_LOGW("downlink playback buffer high water: frame=%lu buffer=%lu/%lu high=%lu events=%lu",
                   (unsigned long)playback_bytes,
                   (unsigned long)stats.buffered_bytes,
                   (unsigned long)stats.buffer_size_bytes,
                   (unsigned long)high_water_bytes,
                   (unsigned long)g_river_orvibo_audio.downlink_backpressure_events);
    }
}

river_status_t river_orvibo_audio_service_handle_downlink(
    const uint8_t *packet,
    size_t packet_bytes,
    uint32_t sample_rate,
    uint32_t channels,
    uint32_t frame_duration_ms)
{
    size_t pcm_bytes = 0U;
    size_t mono_bytes = 0U;
    size_t playback_mono_bytes = 0U;
    uint32_t playback_sample_rate;
    river_status_t resample_status;
    river_status_t downmix_status;

    if (packet == NULL || packet_bytes == 0U) {
        return RIVER_ERR_ARG;
    }
    if (channels == 0U) {
        channels = 1U;
    }
    if (channels > RIVER_ORVIBO_DOWNLINK_MAX_CHANNELS) {
        RIVER_LOGE("downlink unsupported channels=%lu", (unsigned long)channels);
        return RIVER_ERR_UNSUPPORTED;
    }
    if (sample_rate == 0U) {
        sample_rate = 16000U;
    }
    if (frame_duration_ms == 0U) {
        frame_duration_ms = RIVER_ORVIBO_OPUS_FRAME_MS;
    }
    if (g_river_orvibo_audio.decoder.handle == NULL ||
        g_river_orvibo_audio.decoder.sample_rate != sample_rate ||
        g_river_orvibo_audio.decoder.frame_duration_ms != frame_duration_ms ||
        g_river_orvibo_audio.decoder.channels != channels) {
        river_opus_decoder_close(&g_river_orvibo_audio.decoder);
        if (river_opus_decoder_open(&g_river_orvibo_audio.decoder,
                                    sample_rate,
                                    channels,
                                    frame_duration_ms) != RIVER_OK) {
            g_river_orvibo_audio.decode_fail++;
            return RIVER_ERR_UNSUPPORTED;
        }
    }
    if (river_opus_decode(&g_river_orvibo_audio.decoder,
                          packet,
                          packet_bytes,
                          g_river_orvibo_audio.downlink_pcm,
                          sizeof(g_river_orvibo_audio.downlink_pcm),
                          &pcm_bytes) != RIVER_OK) {
        g_river_orvibo_audio.decode_fail++;
        return RIVER_ERR_IO;
    }
    g_river_orvibo_audio.decode_ok++;
    downmix_status = river_orvibo_audio_downmix_to_mono(
        g_river_orvibo_audio.downlink_pcm,
        pcm_bytes,
        channels,
        g_river_orvibo_audio.downlink_mono,
        sizeof(g_river_orvibo_audio.downlink_mono),
        &mono_bytes);
    if (downmix_status != RIVER_OK) {
        g_river_orvibo_audio.decode_fail++;
        return downmix_status;
    }
    playback_sample_rate = river_orvibo_audio_select_playback_sample_rate(sample_rate);
    if (sample_rate != g_river_orvibo_audio.last_downlink_sample_rate ||
        channels != g_river_orvibo_audio.last_downlink_channels ||
        playback_sample_rate != g_river_orvibo_audio.last_playback_sample_rate) {
        RIVER_LOGI("downlink playback rate: server=%luHz/%luch playback=%luHz/2ch frame=%lums",
                   (unsigned long)sample_rate,
                   (unsigned long)channels,
                   (unsigned long)playback_sample_rate,
                   (unsigned long)frame_duration_ms);
        g_river_orvibo_audio.last_downlink_sample_rate = sample_rate;
        g_river_orvibo_audio.last_downlink_channels = channels;
        g_river_orvibo_audio.last_playback_sample_rate = playback_sample_rate;
    }
    resample_status = river_orvibo_audio_resample_mono(g_river_orvibo_audio.downlink_mono,
                                                       mono_bytes,
                                                       sample_rate,
                                                       g_river_orvibo_audio.downlink_playback_pcm,
                                                       sizeof(g_river_orvibo_audio.downlink_playback_pcm),
                                                       playback_sample_rate,
                                                       &playback_mono_bytes);
    if (resample_status != RIVER_OK) {
        g_river_orvibo_audio.resample_fail++;
        return resample_status;
    }
    if (playback_sample_rate == sample_rate) {
        g_river_orvibo_audio.resample_bypass++;
    } else {
        g_river_orvibo_audio.resample_ok++;
    }
    if (playback_mono_bytes == 0U ||
        (playback_mono_bytes * 2U) > sizeof(g_river_orvibo_audio.downlink_stereo)) {
        return RIVER_ERR_UNSUPPORTED;
    }
    if (river_orvibo_audio_start_playback_if_needed(playback_sample_rate,
                                                    frame_duration_ms,
                                                    playback_mono_bytes) != RIVER_OK) {
        return RIVER_ERR_BUSY;
    }
    river_orvibo_audio_expand_stereo(g_river_orvibo_audio.downlink_playback_pcm,
                                     playback_mono_bytes,
                                     g_river_orvibo_audio.downlink_stereo,
                                     sizeof(g_river_orvibo_audio.downlink_stereo));
    river_orvibo_audio_downlink_buffer_high_water(playback_mono_bytes * 2U);
    if (river_playback_service_write((const uint8_t *)g_river_orvibo_audio.downlink_stereo,
                                     playback_mono_bytes * 2U,
                                     (const uint8_t *)g_river_orvibo_audio.downlink_playback_pcm,
                                     playback_mono_bytes,
                                     true) != RIVER_OK) {
        g_river_orvibo_audio.playback_write_fail++;
        return RIVER_ERR_IO;
    }
    g_river_orvibo_audio.playback_write_ok++;
    return RIVER_OK;
}

void river_orvibo_audio_service_prepare_tts_playback(void)
{
    river_opus_decoder_close(&g_river_orvibo_audio.decoder);
    if (g_river_orvibo_audio.playback_started || river_playback_service_active()) {
        (void)river_playback_service_stop_stream_ex("orvibo_tts_restart");
    }
    g_river_orvibo_audio.playback_started = false;
    g_river_orvibo_audio.playback_stream_sample_rate = 0U;
    g_river_orvibo_audio.playback_stream_frame_ms = 0U;
    g_river_orvibo_audio.playback_stream_mono_bytes = 0U;
    RIVER_LOGI("orvibo audio: tts playback prepared");
}

river_status_t river_orvibo_audio_service_wait_playback_idle(uint32_t timeout_ms)
{
    river_status_t status;

    if (!g_river_orvibo_audio.playback_started && !river_playback_service_active()) {
        return RIVER_OK;
    }

    status = river_playback_service_wait_idle_ex(timeout_ms,
                                                RIVER_ORVIBO_PLAYBACK_DRAIN_POLL_MS,
                                                "orvibo_tts_stop");
    if (status == RIVER_OK) {
        river_orvibo_audio_service_stop_playback("orvibo_tts_drain_done");
    } else {
        river_orvibo_audio_service_stop_playback("orvibo_tts_drain_forced");
    }
    return status;
}

void river_orvibo_audio_service_stop_playback(const char *reason)
{
    if (g_river_orvibo_audio.playback_started || river_playback_service_active()) {
        (void)river_playback_service_stop_stream_ex(reason != NULL ? reason : "orvibo_stop");
        g_river_orvibo_audio.playback_started = false;
        g_river_orvibo_audio.playback_stream_sample_rate = 0U;
        g_river_orvibo_audio.playback_stream_frame_ms = 0U;
        g_river_orvibo_audio.playback_stream_mono_bytes = 0U;
        river_orvibo_audio_emit_simple(RIVER_ORVIBO_AUDIO_EVENT_PLAYBACK_FINISHED);
    }
}

void river_orvibo_audio_service_dump_status(void)
{
    RIVER_LOGI("orvibo audio: running=%s mode=%s task_stack=%u vad=%s prob=%u/%u capture=%lu/%lu preproc=%lu/%lu vad_cnt=%lu/%lu speech=%lu/%lu kws=%lu/%lu enc=%lu/%lu dec=%lu/%lu rs=%lu/%lu/%lu rate=%lu/%lu->%lu playback=%s write=%lu/%lu bp_evt=%lu bp_high=%lu buf=%lu/%lu",
               g_river_orvibo_audio.running ? "yes" : "no",
               river_orvibo_audio_mode_name(g_river_orvibo_audio.mode),
               (unsigned int)RIVER_ORVIBO_AUDIO_TASK_STACK,
               g_river_orvibo_audio.vad_is_speech ? "speech" : "silence",
               (unsigned int)g_river_orvibo_audio.vad_probability_raw_q15,
               (unsigned int)g_river_orvibo_audio.vad_probability_q15,
               (unsigned long)g_river_orvibo_audio.capture_ok,
               (unsigned long)g_river_orvibo_audio.capture_fail,
               (unsigned long)g_river_orvibo_audio.preproc_ok,
               (unsigned long)g_river_orvibo_audio.preproc_fail,
               (unsigned long)g_river_orvibo_audio.vad_ok,
               (unsigned long)g_river_orvibo_audio.vad_fail,
               (unsigned long)g_river_orvibo_audio.vad_speech_start,
               (unsigned long)g_river_orvibo_audio.vad_speech_end,
               (unsigned long)g_river_orvibo_audio.kws_submit_ok,
               (unsigned long)g_river_orvibo_audio.kws_submit_fail,
               (unsigned long)g_river_orvibo_audio.encode_ok,
               (unsigned long)g_river_orvibo_audio.encode_fail,
               (unsigned long)g_river_orvibo_audio.decode_ok,
               (unsigned long)g_river_orvibo_audio.decode_fail,
               (unsigned long)g_river_orvibo_audio.resample_ok,
               (unsigned long)g_river_orvibo_audio.resample_bypass,
               (unsigned long)g_river_orvibo_audio.resample_fail,
               (unsigned long)g_river_orvibo_audio.last_downlink_sample_rate,
               (unsigned long)g_river_orvibo_audio.last_downlink_channels,
               (unsigned long)g_river_orvibo_audio.last_playback_sample_rate,
               g_river_orvibo_audio.playback_started ? "started" : "stopped",
               (unsigned long)g_river_orvibo_audio.playback_write_ok,
               (unsigned long)g_river_orvibo_audio.playback_write_fail,
               (unsigned long)g_river_orvibo_audio.downlink_backpressure_events,
               (unsigned long)g_river_orvibo_audio.downlink_backpressure_high_water,
               (unsigned long)g_river_orvibo_audio.downlink_buffered_bytes,
               (unsigned long)g_river_orvibo_audio.downlink_buffer_size_bytes);
    river_voice_capture_dump_status();
    river_voice_kws_dump_status();
    river_playback_service_dump_status();
    river_reference_service_dump_status();
}
