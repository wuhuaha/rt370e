#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "basic_types.h"
#include "os_wrapper.h"
#include "os_wrapper_memory.h"

#include "audio/audio_control.h"
#include "audio/audio_service.h"

#include "river/river_cloud.h"
#include "river/river_log.h"
#include "river/river_voice.h"
#include "river/river_wifi_station.h"
#include "river/river_voice_board.h"
#include "river/river_voice_capture.h"
#include "river/river_voice_detector.h"
#include "river/river_voice_preproc.h"
#include "river/river_voice_segment_buffer.h"
#include "river/river_voice_segment_sink.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.voice.probe"

#ifndef CONFIG_RIVER_VAD_PROBE_DIAG_WINDOW_MS
#define CONFIG_RIVER_VAD_PROBE_DIAG_WINDOW_MS 128
#endif

#ifndef CONFIG_RIVER_VAD_PROBE_PRE_ROLL_MS
#define CONFIG_RIVER_VAD_PROBE_PRE_ROLL_MS 320
#endif

#ifndef CONFIG_RIVER_VAD_PROBE_POST_ROLL_MS
#define CONFIG_RIVER_VAD_PROBE_POST_ROLL_MS 640
#endif

#ifndef CONFIG_RIVER_VAD_PROBE_MAX_SEGMENT_MS
#define CONFIG_RIVER_VAD_PROBE_MAX_SEGMENT_MS 8000
#endif

#ifndef CONFIG_RIVER_VAD_PROBE_SEGMENT_MIN_FREE_HEAP_KB
#define CONFIG_RIVER_VAD_PROBE_SEGMENT_MIN_FREE_HEAP_KB 64
#endif

#define RIVER_VOICE_VAD_PROBE_DIAG_WINDOW_MS CONFIG_RIVER_VAD_PROBE_DIAG_WINDOW_MS
#define RIVER_VOICE_VAD_PROBE_PRE_ROLL_MS    CONFIG_RIVER_VAD_PROBE_PRE_ROLL_MS
#define RIVER_VOICE_VAD_PROBE_POST_ROLL_MS   CONFIG_RIVER_VAD_PROBE_POST_ROLL_MS
#define RIVER_VOICE_VAD_PROBE_MAX_SEGMENT_MS CONFIG_RIVER_VAD_PROBE_MAX_SEGMENT_MS
#define RIVER_VOICE_VAD_PROBE_SEGMENT_MIN_FREE_HEAP_BYTES \
    ((uint32_t)CONFIG_RIVER_VAD_PROBE_SEGMENT_MIN_FREE_HEAP_KB * 1024U)

#define RIVER_VOICE_VAD_PROBE_TASK_STACK         (1024U * 12U)
#define RIVER_VOICE_VAD_PROBE_TASK_PRIORITY      4U
#define RIVER_VOICE_VAD_PROBE_CAPTURE_VOLUME     0x24U
#define RIVER_VOICE_VAD_PROBE_CAPTURE_HPF_FC     0U
#define RIVER_VOICE_VAD_PROBE_PRIMARY_MIC_GAIN   AUDIO_MICBST_GAIN_15DB
#define RIVER_VOICE_VAD_PROBE_SECONDARY_MIC_GAIN AUDIO_MICBST_GAIN_15DB

typedef struct {
    bool running;
    bool stop_requested;
    bool diag_enabled;
    bool segment_buffer_enabled;
    rtos_task_t task;
    river_voice_capture_t capture;
    river_voice_preproc_t preproc;
    river_voice_detector_t detector;
    river_voice_segment_buffer_t segment_buffer;
    uint8_t *capture_buffer;
    uint8_t *enhanced_buffer;
    size_t capture_chunk_bytes;
    size_t enhanced_chunk_bytes;
    uint32_t diag_read_ok;
    uint32_t diag_proc_ok;
    uint32_t diag_det_ok;
    uint32_t diag_read_fail;
    uint32_t diag_proc_fail;
    uint32_t diag_det_fail;
    uint32_t diag_partial_read;
    uint32_t diag_partial_proc;
    uint32_t diag_vad_decisions;
    uint32_t diag_vad_speech;
    uint32_t diag_vad_speech_start;
    uint32_t diag_vad_speech_end;
    uint32_t diag_segment_unsupported;
    uint32_t diag_segment_fail;
    uint32_t diag_cloud_stream_ok;
    uint32_t diag_cloud_stream_busy;
    uint32_t diag_cloud_stream_fail;
    uint32_t diag_chunks_until_log;
    uint16_t diag_capture_peak_ch0;
    uint16_t diag_capture_peak_ch1;
    uint16_t diag_enhanced_peak;
    uint16_t diag_vad_probability_raw_q15;
    uint16_t diag_vad_probability_q15;
    bool diag_vad_state_initialized;
    bool diag_vad_is_speech;
    bool diag_vad_prev_is_speech;
    bool diag_vad_last_logged_is_speech;
} river_voice_vad_probe_context_t;

static river_voice_vad_probe_context_t g_river_voice_vad_probe;
static bool g_river_voice_vad_probe_diag_enabled;

static uint32_t river_voice_vad_probe_frames_to_ms(uint32_t frames)
{
    return frames * river_voice_board_array_profile()->frame_ms;
}

static uint32_t river_voice_vad_probe_ms_to_frames(uint32_t duration_ms, uint32_t frame_ms)
{
    if (frame_ms == 0U) {
        return 0U;
    }
    return (duration_ms + frame_ms - 1U) / frame_ms;
}

static uint16_t river_voice_vad_probe_abs16(int32_t value)
{
    if (value < 0) {
        value = -value;
    }
    if (value > 32767) {
        value = 32767;
    }
    return (uint16_t)value;
}

static uint16_t river_voice_vad_probe_update_peak(const uint8_t *buffer,
                                                  size_t bytes,
                                                  uint32_t channels,
                                                  uint16_t *peak_ch0,
                                                  uint16_t *peak_ch1)
{
    const int16_t *samples;
    size_t sample_count;
    size_t index;
    uint16_t max_peak;

    if (buffer == 0 || bytes < 2U || peak_ch0 == 0 || peak_ch1 == 0) {
        return 0U;
    }

    samples = (const int16_t *)buffer;
    sample_count = bytes / sizeof(int16_t);
    max_peak = 0U;

    if (channels == 1U) {
        for (index = 0; index < sample_count; ++index) {
            uint16_t peak;

            peak = river_voice_vad_probe_abs16(samples[index]);
            if (peak > *peak_ch0) {
                *peak_ch0 = peak;
            }
            if (peak > max_peak) {
                max_peak = peak;
            }
        }
        return max_peak;
    }

    for (index = 0; (index + 1U) < sample_count; index += 2U) {
        uint16_t peak0;
        uint16_t peak1;

        peak0 = river_voice_vad_probe_abs16(samples[index]);
        peak1 = river_voice_vad_probe_abs16(samples[index + 1U]);
        if (peak0 > *peak_ch0) {
            *peak_ch0 = peak0;
        }
        if (peak1 > *peak_ch1) {
            *peak_ch1 = peak1;
        }
        if (peak0 > max_peak) {
            max_peak = peak0;
        }
        if (peak1 > max_peak) {
            max_peak = peak1;
        }
    }

    return max_peak;
}

static void river_voice_vad_probe_reset_diag_counters(void)
{
    uint32_t frame_ms;

    frame_ms = river_voice_board_array_profile()->frame_ms;
    g_river_voice_vad_probe.diag_read_ok = 0U;
    g_river_voice_vad_probe.diag_proc_ok = 0U;
    g_river_voice_vad_probe.diag_det_ok = 0U;
    g_river_voice_vad_probe.diag_read_fail = 0U;
    g_river_voice_vad_probe.diag_proc_fail = 0U;
    g_river_voice_vad_probe.diag_det_fail = 0U;
    g_river_voice_vad_probe.diag_partial_read = 0U;
    g_river_voice_vad_probe.diag_partial_proc = 0U;
    g_river_voice_vad_probe.diag_vad_decisions = 0U;
    g_river_voice_vad_probe.diag_vad_speech = 0U;
    g_river_voice_vad_probe.diag_vad_speech_start = 0U;
    g_river_voice_vad_probe.diag_vad_speech_end = 0U;
    g_river_voice_vad_probe.diag_segment_unsupported = 0U;
    g_river_voice_vad_probe.diag_segment_fail = 0U;
    g_river_voice_vad_probe.diag_cloud_stream_ok = 0U;
    g_river_voice_vad_probe.diag_cloud_stream_busy = 0U;
    g_river_voice_vad_probe.diag_cloud_stream_fail = 0U;
    g_river_voice_vad_probe.diag_capture_peak_ch0 = 0U;
    g_river_voice_vad_probe.diag_capture_peak_ch1 = 0U;
    g_river_voice_vad_probe.diag_enhanced_peak = 0U;
    g_river_voice_vad_probe.diag_vad_probability_raw_q15 = 0U;
    g_river_voice_vad_probe.diag_vad_probability_q15 = 0U;
    g_river_voice_vad_probe.diag_vad_is_speech = false;
    g_river_voice_vad_probe.diag_chunks_until_log =
        (RIVER_VOICE_VAD_PROBE_DIAG_WINDOW_MS + (frame_ms / 2U)) / frame_ms;
    if (g_river_voice_vad_probe.diag_chunks_until_log == 0U) {
        g_river_voice_vad_probe.diag_chunks_until_log = 1U;
    }
}

static void river_voice_vad_probe_log_diagnostics_if_needed(void)
{
    river_voice_segment_buffer_status_t segment_status;

    if (!g_river_voice_vad_probe.diag_enabled) {
        return;
    }

    if (g_river_voice_vad_probe.diag_chunks_until_log > 0U) {
        g_river_voice_vad_probe.diag_chunks_until_log--;
    }
    if (g_river_voice_vad_probe.diag_chunks_until_log > 0U) {
        return;
    }

    river_voice_segment_buffer_get_status(&g_river_voice_vad_probe.segment_buffer,
                                          &segment_status);

    RIVER_LOGD("cap_peak=[%u,%u] afe_peak=%u vad_raw_q15=%u vad_prob_q15=%u vad=%s vad_decisions=%lu vad_speech=%lu vad_start=%lu vad_end=%lu sdk_vad=%s sdk_events=%lu sdk_start=%lu sdk_end=%lu sdk_offset_ms=%lu seg=%s seg_pre_ms=%lu seg_post_left_ms=%lu seg_active_ms=%lu seg_ready_ms=%lu seg_done=%lu seg_drop=%lu cloud_stream_ok=%lu cloud_stream_busy=%lu cloud_stream_fail=%lu read_ok=%lu proc_ok=%lu det_ok=%lu seg_unsupported=%lu seg_fail=%lu read_fail=%lu proc_fail=%lu det_fail=%lu partial_read=%lu partial_proc=%lu",
               (unsigned int)g_river_voice_vad_probe.diag_capture_peak_ch0,
               (unsigned int)g_river_voice_vad_probe.diag_capture_peak_ch1,
               (unsigned int)g_river_voice_vad_probe.diag_enhanced_peak,
               (unsigned int)g_river_voice_vad_probe.diag_vad_probability_raw_q15,
               (unsigned int)g_river_voice_vad_probe.diag_vad_probability_q15,
               g_river_voice_vad_probe.diag_vad_is_speech ? "speech" : "silence",
               (unsigned long)g_river_voice_vad_probe.diag_vad_decisions,
               (unsigned long)g_river_voice_vad_probe.diag_vad_speech,
               (unsigned long)g_river_voice_vad_probe.diag_vad_speech_start,
               (unsigned long)g_river_voice_vad_probe.diag_vad_speech_end,
               "disabled",
               0UL,
               0UL,
               0UL,
               0UL,
               g_river_voice_vad_probe.segment_buffer_enabled ?
                   (segment_status.active ? "active" : (segment_status.ready ? "ready" : "idle")) :
                   "disabled",
               (unsigned long)river_voice_vad_probe_frames_to_ms(segment_status.prebuffered_frames),
               (unsigned long)river_voice_vad_probe_frames_to_ms(segment_status.post_roll_frames_left),
               (unsigned long)river_voice_vad_probe_frames_to_ms(segment_status.active_frames),
               (unsigned long)river_voice_vad_probe_frames_to_ms(segment_status.ready_frames),
               (unsigned long)segment_status.segments_completed,
               (unsigned long)segment_status.segments_dropped,
               (unsigned long)g_river_voice_vad_probe.diag_cloud_stream_ok,
               (unsigned long)g_river_voice_vad_probe.diag_cloud_stream_busy,
               (unsigned long)g_river_voice_vad_probe.diag_cloud_stream_fail,
               (unsigned long)g_river_voice_vad_probe.diag_read_ok,
               (unsigned long)g_river_voice_vad_probe.diag_proc_ok,
               (unsigned long)g_river_voice_vad_probe.diag_det_ok,
               (unsigned long)g_river_voice_vad_probe.diag_segment_unsupported,
               (unsigned long)g_river_voice_vad_probe.diag_segment_fail,
               (unsigned long)g_river_voice_vad_probe.diag_read_fail,
               (unsigned long)g_river_voice_vad_probe.diag_proc_fail,
               (unsigned long)g_river_voice_vad_probe.diag_det_fail,
               (unsigned long)g_river_voice_vad_probe.diag_partial_read,
               (unsigned long)g_river_voice_vad_probe.diag_partial_proc);

    river_voice_vad_probe_reset_diag_counters();
}

static void river_voice_vad_probe_log_state_change_if_needed(bool detector_decision_valid,
                                                             bool detector_is_speech)
{
    river_voice_segment_buffer_status_t segment_status;
    bool should_log = false;

    if (!detector_decision_valid) {
        return;
    }

    if (!g_river_voice_vad_probe.diag_vad_state_initialized) {
        should_log = true;
        g_river_voice_vad_probe.diag_vad_state_initialized = true;
    } else if (g_river_voice_vad_probe.diag_vad_last_logged_is_speech != detector_is_speech) {
        should_log = true;
    }

    if (!should_log) {
        return;
    }

    river_voice_segment_buffer_get_status(&g_river_voice_vad_probe.segment_buffer,
                                          &segment_status);
    RIVER_LOGI("vad state=%s raw_q15=%u prob_q15=%u cap_peak=[%u,%u] afe_peak=%u sdk_vad=%s sdk_events=%lu seg=%s seg_pre_ms=%lu seg_post_left_ms=%lu stream_ok=%lu stream_busy=%lu stream_fail=%lu",
               detector_is_speech ? "speech" : "silence",
               (unsigned int)g_river_voice_vad_probe.diag_vad_probability_raw_q15,
               (unsigned int)g_river_voice_vad_probe.diag_vad_probability_q15,
               (unsigned int)g_river_voice_vad_probe.diag_capture_peak_ch0,
               (unsigned int)g_river_voice_vad_probe.diag_capture_peak_ch1,
               (unsigned int)g_river_voice_vad_probe.diag_enhanced_peak,
               "disabled",
               0UL,
               g_river_voice_vad_probe.segment_buffer_enabled ?
                   (segment_status.active ? "active" : (segment_status.ready ? "ready" : "idle")) :
                   "disabled",
               (unsigned long)river_voice_vad_probe_frames_to_ms(segment_status.prebuffered_frames),
               (unsigned long)river_voice_vad_probe_frames_to_ms(segment_status.post_roll_frames_left),
               (unsigned long)g_river_voice_vad_probe.diag_cloud_stream_ok,
               (unsigned long)g_river_voice_vad_probe.diag_cloud_stream_busy,
               (unsigned long)g_river_voice_vad_probe.diag_cloud_stream_fail);
    g_river_voice_vad_probe.diag_vad_last_logged_is_speech = detector_is_speech;
}

static void river_voice_vad_probe_close_audio(void)
{
    river_cloud_asr_audio_close();
    river_voice_detector_close(&g_river_voice_vad_probe.detector);
    river_voice_preproc_close(&g_river_voice_vad_probe.preproc);
    river_voice_capture_close(&g_river_voice_vad_probe.capture);
}

static void river_voice_vad_probe_release_buffers(void)
{
    river_voice_segment_buffer_close(&g_river_voice_vad_probe.segment_buffer);
    if (g_river_voice_vad_probe.enhanced_buffer != 0) {
        rtos_mem_free(g_river_voice_vad_probe.enhanced_buffer);
        g_river_voice_vad_probe.enhanced_buffer = 0;
    }
    if (g_river_voice_vad_probe.capture_buffer != 0) {
        rtos_mem_free(g_river_voice_vad_probe.capture_buffer);
        g_river_voice_vad_probe.capture_buffer = 0;
    }

    g_river_voice_vad_probe.capture_chunk_bytes = 0U;
    g_river_voice_vad_probe.enhanced_chunk_bytes = 0U;
    river_voice_vad_probe_reset_diag_counters();
}

static river_status_t river_voice_vad_probe_prepare_buffers(void)
{
    river_voice_segment_buffer_config_t segment_config;
    uint32_t required_segment_bytes;
    uint32_t free_heap;

    g_river_voice_vad_probe.capture_chunk_bytes = g_river_voice_vad_probe.capture.frame_bytes;
    g_river_voice_vad_probe.enhanced_chunk_bytes =
        river_voice_preproc_output_frame_bytes(&g_river_voice_vad_probe.preproc);
    g_river_voice_vad_probe.diag_enabled = g_river_voice_vad_probe_diag_enabled;
    g_river_voice_vad_probe.segment_buffer_enabled = false;

    g_river_voice_vad_probe.capture_buffer =
        (uint8_t *)rtos_mem_zmalloc((uint32_t)g_river_voice_vad_probe.capture_chunk_bytes);
    g_river_voice_vad_probe.enhanced_buffer =
        (uint8_t *)rtos_mem_zmalloc((uint32_t)g_river_voice_vad_probe.enhanced_chunk_bytes);
    if (g_river_voice_vad_probe.capture_buffer == 0 ||
        g_river_voice_vad_probe.enhanced_buffer == 0) {
        river_voice_vad_probe_release_buffers();
        return RIVER_ERR_NO_MEMORY;
    }

    memset(&segment_config, 0, sizeof(segment_config));
    segment_config.sample_rate = g_river_voice_vad_probe.capture.sample_rate;
    segment_config.frame_ms = g_river_voice_vad_probe.capture.frame_ms;
    segment_config.frame_bytes = g_river_voice_vad_probe.enhanced_chunk_bytes;
    segment_config.pre_roll_ms = RIVER_VOICE_VAD_PROBE_PRE_ROLL_MS;
    segment_config.post_roll_ms = RIVER_VOICE_VAD_PROBE_POST_ROLL_MS;
    segment_config.max_segment_ms = RIVER_VOICE_VAD_PROBE_MAX_SEGMENT_MS;

    if (!river_cloud_asr_batch_supported()) {
        RIVER_LOGI("vad probe segment buffer disabled: provider=%s batch=no stream-only bridge active",
                   river_cloud_asr_provider_name());
        return RIVER_OK;
    }

    required_segment_bytes =
        river_voice_vad_probe_ms_to_frames(segment_config.pre_roll_ms, segment_config.frame_ms) *
            (uint32_t)segment_config.frame_bytes +
        river_voice_vad_probe_ms_to_frames(segment_config.max_segment_ms, segment_config.frame_ms) *
            (uint32_t)segment_config.frame_bytes;
    free_heap = rtos_mem_get_free_heap_size();
    if (free_heap < (required_segment_bytes + RIVER_VOICE_VAD_PROBE_SEGMENT_MIN_FREE_HEAP_BYTES)) {
        RIVER_LOGW("vad probe segment buffer skipped: free_heap=%luB required~%luB headroom=%luB provider=%s batch=yes",
                   (unsigned long)free_heap,
                   (unsigned long)required_segment_bytes,
                   (unsigned long)RIVER_VOICE_VAD_PROBE_SEGMENT_MIN_FREE_HEAP_BYTES,
                   river_cloud_asr_provider_name());
        return RIVER_OK;
    }

    if (river_voice_segment_buffer_open(&g_river_voice_vad_probe.segment_buffer,
                                        &segment_config) != RIVER_OK) {
        RIVER_LOGW("vad probe segment buffer open failed: free_heap=%luB required~%luB provider=%s; continue stream-only",
                   (unsigned long)free_heap,
                   (unsigned long)required_segment_bytes,
                   river_cloud_asr_provider_name());
        return RIVER_OK;
    }
    g_river_voice_vad_probe.segment_buffer_enabled = true;

    return RIVER_OK;
}

static river_status_t river_voice_vad_probe_open_audio(void)
{
    river_cloud_asr_audio_desc_t audio_desc;

    AudioService_Init();
    AudioControl_SetCaptureVolume(river_voice_board_array_profile()->capture_channels,
                                  RIVER_VOICE_VAD_PROBE_CAPTURE_VOLUME);
    AudioControl_SetCaptureHpfFc(0, RIVER_VOICE_VAD_PROBE_CAPTURE_HPF_FC);

    if (river_voice_capture_open(&g_river_voice_vad_probe.capture) != RIVER_OK) {
        return RIVER_ERR_UNSUPPORTED;
    }
    AudioControl_SetMicBstGain(river_voice_board_array_profile()->primary_mic,
                               RIVER_VOICE_VAD_PROBE_PRIMARY_MIC_GAIN);
    if (river_voice_board_array_profile()->capture_channels > 1U) {
        AudioControl_SetMicBstGain(river_voice_board_array_profile()->secondary_mic,
                                   RIVER_VOICE_VAD_PROBE_SECONDARY_MIC_GAIN);
    }
    if (river_voice_preproc_open(&g_river_voice_vad_probe.preproc) != RIVER_OK) {
        RIVER_LOGE("vad probe preproc open failed");
        return RIVER_ERR_UNSUPPORTED;
    }
    if (river_voice_preproc_reference_enabled(&g_river_voice_vad_probe.preproc)) {
        RIVER_LOGE("vad probe requires reference-disabled preproc profile");
        return RIVER_ERR_UNSUPPORTED;
    }
    if (river_voice_detector_open(&g_river_voice_vad_probe.detector) != RIVER_OK) {
        RIVER_LOGE("vad probe detector open failed");
        return RIVER_ERR_UNSUPPORTED;
    }
    if (river_voice_detector_input_frame_bytes(&g_river_voice_vad_probe.detector) !=
        river_voice_preproc_output_frame_bytes(&g_river_voice_vad_probe.preproc)) {
        RIVER_LOGE("vad probe detector/preproc frame mismatch: detector=%luB preproc=%luB",
                   (unsigned long)river_voice_detector_input_frame_bytes(&g_river_voice_vad_probe.detector),
                   (unsigned long)river_voice_preproc_output_frame_bytes(&g_river_voice_vad_probe.preproc));
        return RIVER_ERR_UNSUPPORTED;
    }

    memset(&audio_desc, 0, sizeof(audio_desc));
    audio_desc.sample_rate = g_river_voice_vad_probe.capture.sample_rate;
    audio_desc.channels = 1U;
    audio_desc.bits_per_sample = 16U;
    audio_desc.frame_ms = g_river_voice_vad_probe.capture.frame_ms;
    audio_desc.encoding = "pcm_s16le";
    if (river_cloud_asr_audio_open(&audio_desc,
                                   RIVER_VOICE_VAD_PROBE_PRE_ROLL_MS,
                                   RIVER_VOICE_VAD_PROBE_POST_ROLL_MS) != RIVER_OK) {
        RIVER_LOGE("vad probe cloud asr bridge open failed");
        return RIVER_ERR_UNSUPPORTED;
    }

    if (river_voice_vad_probe_prepare_buffers() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }

    RIVER_LOGI("vad probe config: %lu Hz capture dual-mic -> fixed_dsb 1ch -> detector-only, %s+%s, diag_window~%ums",
               (unsigned long)g_river_voice_vad_probe.capture.sample_rate,
               river_voice_board_mic_name(river_voice_board_array_profile()->primary_mic),
               river_voice_board_mic_name(river_voice_board_array_profile()->secondary_mic),
               (unsigned int)RIVER_VOICE_VAD_PROBE_DIAG_WINDOW_MS);
    RIVER_LOGI("vad probe gain: cap=0x%02x micbst=[%s,%s]",
               (unsigned int)RIVER_VOICE_VAD_PROBE_CAPTURE_VOLUME,
               river_voice_board_mic_gain_name(RIVER_VOICE_VAD_PROBE_PRIMARY_MIC_GAIN),
               river_voice_board_mic_gain_name(RIVER_VOICE_VAD_PROBE_SECONDARY_MIC_GAIN));
    if (g_river_voice_vad_probe.segment_buffer_enabled) {
        RIVER_LOGI("vad probe segment buffer: enabled pre=%ums post=%ums max=%ums",
                   (unsigned int)RIVER_VOICE_VAD_PROBE_PRE_ROLL_MS,
                   (unsigned int)RIVER_VOICE_VAD_PROBE_POST_ROLL_MS,
                   (unsigned int)RIVER_VOICE_VAD_PROBE_MAX_SEGMENT_MS);
    } else {
        RIVER_LOGI("vad probe segment buffer: disabled; streaming path remains active");
    }
    return RIVER_OK;
}

static void river_voice_vad_probe_task(void *param)
{
    size_t bytes_read;
    size_t enhanced_bytes;
    river_voice_detector_result_t detector_result;

    (void)param;

    while (!g_river_voice_vad_probe.stop_requested) {
        bytes_read = (size_t)river_voice_capture_read(&g_river_voice_vad_probe.capture,
                                                      g_river_voice_vad_probe.capture_buffer,
                                                      g_river_voice_vad_probe.capture_chunk_bytes);
        if (bytes_read != g_river_voice_vad_probe.capture_chunk_bytes) {
            g_river_voice_vad_probe.diag_read_fail++;
            if (bytes_read > 0U && bytes_read < g_river_voice_vad_probe.capture_chunk_bytes) {
                memset(g_river_voice_vad_probe.capture_buffer + bytes_read,
                       0,
                       g_river_voice_vad_probe.capture_chunk_bytes - bytes_read);
                g_river_voice_vad_probe.diag_partial_read++;
            }
            river_voice_vad_probe_log_diagnostics_if_needed();
            continue;
        }
        g_river_voice_vad_probe.diag_read_ok++;
        river_voice_vad_probe_update_peak(g_river_voice_vad_probe.capture_buffer,
                                          g_river_voice_vad_probe.capture_chunk_bytes,
                                          g_river_voice_vad_probe.capture.channels,
                                          &g_river_voice_vad_probe.diag_capture_peak_ch0,
                                          &g_river_voice_vad_probe.diag_capture_peak_ch1);

        enhanced_bytes = 0U;
        if (river_voice_preproc_process(&g_river_voice_vad_probe.preproc,
                                        g_river_voice_vad_probe.capture_buffer,
                                        g_river_voice_vad_probe.capture_chunk_bytes,
                                        0,
                                        0U,
                                        g_river_voice_vad_probe.enhanced_buffer,
                                        g_river_voice_vad_probe.enhanced_chunk_bytes,
                                        &enhanced_bytes) != RIVER_OK) {
            g_river_voice_vad_probe.diag_proc_fail++;
            river_voice_vad_probe_log_diagnostics_if_needed();
            continue;
        }
        g_river_voice_vad_probe.diag_proc_ok++;
        if (enhanced_bytes < g_river_voice_vad_probe.enhanced_chunk_bytes) {
            memset(g_river_voice_vad_probe.enhanced_buffer + enhanced_bytes,
                   0,
                   g_river_voice_vad_probe.enhanced_chunk_bytes - enhanced_bytes);
            g_river_voice_vad_probe.diag_partial_proc++;
        }
        river_voice_vad_probe_update_peak(g_river_voice_vad_probe.enhanced_buffer,
                                          g_river_voice_vad_probe.enhanced_chunk_bytes,
                                          1U,
                                          &g_river_voice_vad_probe.diag_enhanced_peak,
                                          &g_river_voice_vad_probe.diag_enhanced_peak);

        if (river_voice_detector_process(&g_river_voice_vad_probe.detector,
                                         g_river_voice_vad_probe.enhanced_buffer,
                                         g_river_voice_vad_probe.enhanced_chunk_bytes,
                                         &detector_result) != RIVER_OK) {
            g_river_voice_vad_probe.diag_det_fail++;
            river_voice_vad_probe_log_diagnostics_if_needed();
            continue;
        }
        g_river_voice_vad_probe.diag_det_ok++;
        if (detector_result.decision_valid) {
            river_status_t cloud_status;
            bool previous_vad_state;

            previous_vad_state = g_river_voice_vad_probe.diag_vad_prev_is_speech;
            g_river_voice_vad_probe.diag_vad_probability_raw_q15 =
                detector_result.speech_probability_raw_q15;
            g_river_voice_vad_probe.diag_vad_probability_q15 =
                detector_result.speech_probability_q15;
            g_river_voice_vad_probe.diag_vad_is_speech = detector_result.is_speech;
            g_river_voice_vad_probe.diag_vad_decisions++;
            if (detector_result.is_speech) {
                g_river_voice_vad_probe.diag_vad_speech++;
            }
            if (detector_result.is_speech && !previous_vad_state) {
                g_river_voice_vad_probe.diag_vad_speech_start++;
            } else if (!detector_result.is_speech && previous_vad_state) {
                g_river_voice_vad_probe.diag_vad_speech_end++;
            }
            g_river_voice_vad_probe.diag_vad_prev_is_speech = detector_result.is_speech;
            if (g_river_voice_vad_probe.segment_buffer_enabled &&
                river_voice_segment_buffer_push(&g_river_voice_vad_probe.segment_buffer,
                                                g_river_voice_vad_probe.enhanced_buffer,
                                                g_river_voice_vad_probe.enhanced_chunk_bytes,
                                                detector_result.is_speech) != RIVER_OK) {
                g_river_voice_vad_probe.diag_segment_fail++;
            } else if (g_river_voice_vad_probe.segment_buffer_enabled &&
                       river_voice_segment_buffer_ready_bytes(&g_river_voice_vad_probe.segment_buffer) > 0U) {
                river_voice_segment_buffer_status_t segment_status;
                river_voice_segment_desc_t segment_desc;
                river_status_t segment_sink_status;
                size_t segment_bytes;

                river_voice_segment_buffer_get_status(&g_river_voice_vad_probe.segment_buffer,
                                                      &segment_status);
                segment_bytes =
                    river_voice_segment_buffer_ready_bytes(&g_river_voice_vad_probe.segment_buffer);
                memset(&segment_desc, 0, sizeof(segment_desc));
                segment_desc.sample_rate = g_river_voice_vad_probe.capture.sample_rate;
                segment_desc.frame_ms = g_river_voice_vad_probe.capture.frame_ms;
                segment_desc.pre_roll_ms = RIVER_VOICE_VAD_PROBE_PRE_ROLL_MS;
                segment_desc.post_roll_ms = RIVER_VOICE_VAD_PROBE_POST_ROLL_MS;
                segment_desc.segment_ms =
                    river_voice_vad_probe_frames_to_ms(segment_status.ready_frames);
                segment_desc.completed_segments = segment_status.segments_completed;
                segment_desc.dropped_segments = segment_status.segments_dropped;
                segment_sink_status = river_voice_segment_sink_submit(
                    river_voice_segment_buffer_ready_data(&g_river_voice_vad_probe.segment_buffer),
                    segment_bytes,
                    &segment_desc);
                if (segment_sink_status == RIVER_ERR_UNSUPPORTED) {
                    g_river_voice_vad_probe.diag_segment_unsupported++;
                } else if (segment_sink_status != RIVER_OK) {
                    g_river_voice_vad_probe.diag_segment_fail++;
                }
                RIVER_LOGD("segment ready: bytes=%lu ms=%lu pre=%ums post=%ums completed=%lu dropped=%lu",
                           (unsigned long)segment_bytes,
                           (unsigned long)river_voice_vad_probe_frames_to_ms(segment_status.ready_frames),
                           (unsigned int)RIVER_VOICE_VAD_PROBE_PRE_ROLL_MS,
                           (unsigned int)RIVER_VOICE_VAD_PROBE_POST_ROLL_MS,
                           (unsigned long)segment_status.segments_completed,
                           (unsigned long)segment_status.segments_dropped);
                river_voice_segment_buffer_release_ready(&g_river_voice_vad_probe.segment_buffer);
            }

            cloud_status = river_cloud_asr_stream_push_frame(
                g_river_voice_vad_probe.enhanced_buffer,
                g_river_voice_vad_probe.enhanced_chunk_bytes,
                detector_result.is_speech);
            if (cloud_status == RIVER_OK) {
                g_river_voice_vad_probe.diag_cloud_stream_ok++;
            } else if (cloud_status == RIVER_ERR_BUSY ||
                       cloud_status == RIVER_ERR_UNSUPPORTED) {
                g_river_voice_vad_probe.diag_cloud_stream_busy++;
                if (detector_result.is_speech && !previous_vad_state) {
                    RIVER_LOGI("speech detected but cloud stream backpressured/deferred: provider=%s status=%d wifi=%s",
                               river_cloud_asr_provider_name(),
                               cloud_status,
                               river_wifi_station_status_name());
                }
            } else {
                g_river_voice_vad_probe.diag_cloud_stream_fail++;
                if (detector_result.is_speech && !previous_vad_state) {
                    RIVER_LOGW("speech detected but cloud stream push failed: provider=%s status=%d wifi=%s",
                               river_cloud_asr_provider_name(),
                               cloud_status,
                               river_wifi_station_status_name());
                }
            }
        }

        river_voice_vad_probe_log_state_change_if_needed(detector_result.decision_valid,
                                                         detector_result.is_speech);

        river_voice_vad_probe_log_diagnostics_if_needed();
    }

    river_voice_vad_probe_close_audio();
    river_voice_vad_probe_release_buffers();
    g_river_voice_vad_probe.stop_requested = false;
    g_river_voice_vad_probe.running = false;
    g_river_voice_vad_probe.task = 0;
    RIVER_LOGI("vad probe stopped");
    rtos_task_delete(NULL);
}

river_status_t river_voice_vad_probe_start(void)
{
    river_status_t status;

    if (g_river_voice_vad_probe.running) {
        RIVER_LOGW("vad probe already running");
        return RIVER_OK;
    }

    memset(&g_river_voice_vad_probe, 0, sizeof(g_river_voice_vad_probe));
    river_voice_vad_probe_reset_diag_counters();

    status = river_voice_vad_probe_open_audio();
    if (status != RIVER_OK) {
        river_voice_vad_probe_close_audio();
        river_voice_vad_probe_release_buffers();
        return status;
    }

    g_river_voice_vad_probe.running = true;
    if (rtos_task_create(&g_river_voice_vad_probe.task,
                         "river_vad_probe",
                         river_voice_vad_probe_task,
                         0,
                         RIVER_VOICE_VAD_PROBE_TASK_STACK,
                         RIVER_VOICE_VAD_PROBE_TASK_PRIORITY) != RTK_SUCCESS) {
        RIVER_LOGE("create vad probe task failed");
        g_river_voice_vad_probe.running = false;
        river_voice_vad_probe_close_audio();
        river_voice_vad_probe_release_buffers();
        return RIVER_ERR_NO_MEMORY;
    }

    RIVER_LOGI("vad probe started");
    return RIVER_OK;
}

river_status_t river_voice_vad_probe_stop(void)
{
    uint32_t wait_count;

    if (!g_river_voice_vad_probe.running) {
        RIVER_LOGW("vad probe already stopped");
        return RIVER_OK;
    }

    g_river_voice_vad_probe.stop_requested = true;
    for (wait_count = 0; wait_count < 100U; ++wait_count) {
        if (!g_river_voice_vad_probe.running) {
            return RIVER_OK;
        }
        rtos_time_delay_ms(20U);
    }

    RIVER_LOGE("vad probe stop timeout");
    return RIVER_ERR_BUSY;
}

bool river_voice_vad_probe_is_running(void)
{
    return g_river_voice_vad_probe.running;
}

const char *river_voice_vad_probe_status_name(void)
{
    return river_voice_vad_probe_is_running() ? "running" : "stopped";
}

void river_voice_vad_probe_set_diag_enabled(bool enabled)
{
    g_river_voice_vad_probe_diag_enabled = enabled;
    g_river_voice_vad_probe.diag_enabled = enabled;
    river_voice_vad_probe_reset_diag_counters();
}

bool river_voice_vad_probe_diag_enabled(void)
{
    return g_river_voice_vad_probe_diag_enabled;
}

void river_voice_vad_probe_dump_status(void)
{
    RIVER_LOGI("audio_vad_probe=%s", river_voice_vad_probe_status_name());
    RIVER_LOGI("audio_vad_probe_diag=%s", river_voice_vad_probe_diag_enabled() ? "on" : "off");
    if (river_voice_vad_probe_is_running()) {
        RIVER_LOGI("audio_vad_probe_profile=cap:%luHz/%luch(%s+%s) preproc:%s detector:%s diag_window~%ums segment:%s[pre=%ums post=%ums max=%ums]",
                   (unsigned long)g_river_voice_vad_probe.capture.sample_rate,
                   (unsigned long)g_river_voice_vad_probe.capture.channels,
                   river_voice_board_mic_name(river_voice_board_array_profile()->primary_mic),
                   river_voice_board_mic_name(river_voice_board_array_profile()->secondary_mic),
                   river_voice_preproc_backend_name(),
                   river_voice_detector_backend_name(),
                   (unsigned int)RIVER_VOICE_VAD_PROBE_DIAG_WINDOW_MS,
                   g_river_voice_vad_probe.segment_buffer_enabled ? "enabled" : "disabled",
                   (unsigned int)RIVER_VOICE_VAD_PROBE_PRE_ROLL_MS,
                   (unsigned int)RIVER_VOICE_VAD_PROBE_POST_ROLL_MS,
                   (unsigned int)RIVER_VOICE_VAD_PROBE_MAX_SEGMENT_MS);
    }
}
