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
#include "river/river_interaction_state.h"
#include "river/river_log.h"
#include "river/river_playback_service.h"
#include "river/river_reference_service.h"
#include "river/river_runtime_stats.h"
#include "river/river_voice.h"
#include "river/river_wifi_station.h"
#include "river/river_voice_board.h"
#include "river/river_voice_capture.h"
#include "river/river_voice_detector.h"
#include "river/river_voice_experiment.h"
#include "river/river_voice_frame_pool.h"
#include "river/river_voice_preproc.h"
#include "river/river_voice_profile.h"
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

#define RIVER_VOICE_VAD_PROBE_TASK_STACK         (1024U * 16U)
#define RIVER_VOICE_VAD_PROBE_TASK_PRIORITY      4U
#define RIVER_VOICE_VAD_PROBE_CAPTURE_VOLUME     0x24U
#define RIVER_VOICE_VAD_PROBE_CAPTURE_HPF_FC     0U
#define RIVER_VOICE_VAD_PROBE_PRIMARY_MIC_GAIN   AUDIO_MICBST_GAIN_20DB
#define RIVER_VOICE_VAD_PROBE_SECONDARY_MIC_GAIN AUDIO_MICBST_GAIN_20DB
#define RIVER_VOICE_VAD_PROBE_RUNTIME_STATS_INTERVAL_MS 5000U
#define RIVER_VOICE_VAD_PROBE_BARGE_IN_REF_MARGIN_PEAK 448U
#define RIVER_VOICE_VAD_PROBE_BARGE_IN_MIN_ENHANCED_PEAK 1200U
#define RIVER_VOICE_VAD_PROBE_BARGE_IN_RATIO_PCT 150U
#define RIVER_VOICE_VAD_PROBE_BARGE_IN_HIT_FRAMES 3U
#define RIVER_VOICE_VAD_PROBE_BARGE_IN_COOLDOWN_MS 1200U

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
    uint8_t *reference_buffer;
    uint8_t *playback_ref_buffer;
    size_t capture_chunk_bytes;
    size_t enhanced_chunk_bytes;
    size_t reference_chunk_bytes;
    size_t playback_ref_chunk_bytes;
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
    uint32_t diag_ref_read_ok;
    uint32_t diag_ref_read_miss;
    uint32_t diag_playback_ref_read_ok;
    uint32_t diag_playback_ref_read_miss;
    uint32_t diag_barge_in_triggered;
    uint32_t diag_chunks_until_log;
    uint16_t diag_capture_peak_ch0;
    uint16_t diag_capture_peak_ch1;
    uint16_t diag_enhanced_peak;
    uint16_t diag_playback_ref_peak;
    uint16_t diag_vad_probability_raw_q15;
    uint16_t diag_vad_probability_q15;
    uint64_t diag_last_runtime_stats_log_ms;
    uint64_t barge_in_last_trigger_ms;
    bool diag_vad_state_initialized;
    bool diag_vad_is_speech;
    bool diag_vad_prev_is_speech;
    bool diag_vad_last_logged_is_speech;
    uint8_t barge_in_hit_frames;
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

static bool river_voice_vad_probe_playback_active(void)
{
    return river_playback_service_active();
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

    for (index = 0; index < sample_count; index += channels) {
        uint16_t peak0;
        uint16_t peak1 = 0U;

        peak0 = river_voice_vad_probe_abs16(samples[index]);
        if ((index + 1U) < sample_count) {
            peak1 = river_voice_vad_probe_abs16(samples[index + 1U]);
        }
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
    g_river_voice_vad_probe.diag_ref_read_ok = 0U;
    g_river_voice_vad_probe.diag_ref_read_miss = 0U;
    g_river_voice_vad_probe.diag_playback_ref_read_ok = 0U;
    g_river_voice_vad_probe.diag_playback_ref_read_miss = 0U;
    g_river_voice_vad_probe.diag_barge_in_triggered = 0U;
    g_river_voice_vad_probe.diag_capture_peak_ch0 = 0U;
    g_river_voice_vad_probe.diag_capture_peak_ch1 = 0U;
    g_river_voice_vad_probe.diag_enhanced_peak = 0U;
    g_river_voice_vad_probe.diag_playback_ref_peak = 0U;
    g_river_voice_vad_probe.diag_vad_probability_raw_q15 = 0U;
    g_river_voice_vad_probe.diag_vad_probability_q15 = 0U;
    g_river_voice_vad_probe.diag_vad_is_speech = false;
    g_river_voice_vad_probe.barge_in_hit_frames = 0U;
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

    RIVER_LOGD("cap_peak=[%u,%u] afe_peak=%u ref_peak=%u vad_raw_q15=%u vad_prob_q15=%u vad=%s vad_decisions=%lu vad_speech=%lu vad_start=%lu vad_end=%lu sdk_vad=%s sdk_events=%lu sdk_start=%lu sdk_end=%lu sdk_offset_ms=%lu seg=%s seg_pre_ms=%lu seg_post_left_ms=%lu seg_active_ms=%lu seg_ready_ms=%lu seg_done=%lu seg_drop=%lu cloud_stream_ok=%lu cloud_stream_busy=%lu cloud_stream_fail=%lu ref_read_ok=%lu ref_read_miss=%lu pb_ref_ok=%lu pb_ref_miss=%lu barge_in=%lu read_ok=%lu proc_ok=%lu det_ok=%lu seg_unsupported=%lu seg_fail=%lu read_fail=%lu proc_fail=%lu det_fail=%lu partial_read=%lu partial_proc=%lu",
               (unsigned int)g_river_voice_vad_probe.diag_capture_peak_ch0,
               (unsigned int)g_river_voice_vad_probe.diag_capture_peak_ch1,
               (unsigned int)g_river_voice_vad_probe.diag_enhanced_peak,
               (unsigned int)g_river_voice_vad_probe.diag_playback_ref_peak,
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
               (unsigned long)g_river_voice_vad_probe.diag_ref_read_ok,
               (unsigned long)g_river_voice_vad_probe.diag_ref_read_miss,
               (unsigned long)g_river_voice_vad_probe.diag_playback_ref_read_ok,
               (unsigned long)g_river_voice_vad_probe.diag_playback_ref_read_miss,
               (unsigned long)g_river_voice_vad_probe.diag_barge_in_triggered,
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
    uint64_t now_ms;
    bool should_log = false;
    bool should_log_runtime_stats = false;

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
    RIVER_LOGI("vad state=%s raw_q15=%u prob_q15=%u cap_peak=[%u,%u] afe_peak=%u ref_peak=%u sdk_vad=%s sdk_events=%lu seg=%s seg_pre_ms=%lu seg_post_left_ms=%lu stream_ok=%lu stream_busy=%lu stream_fail=%lu",
               detector_is_speech ? "speech" : "silence",
               (unsigned int)g_river_voice_vad_probe.diag_vad_probability_raw_q15,
               (unsigned int)g_river_voice_vad_probe.diag_vad_probability_q15,
               (unsigned int)g_river_voice_vad_probe.diag_capture_peak_ch0,
               (unsigned int)g_river_voice_vad_probe.diag_capture_peak_ch1,
               (unsigned int)g_river_voice_vad_probe.diag_enhanced_peak,
               (unsigned int)g_river_voice_vad_probe.diag_playback_ref_peak,
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

    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    if (g_river_voice_vad_probe.diag_last_runtime_stats_log_ms == 0U ||
        (now_ms - g_river_voice_vad_probe.diag_last_runtime_stats_log_ms) >=
            (uint64_t)RIVER_VOICE_VAD_PROBE_RUNTIME_STATS_INTERVAL_MS) {
        should_log_runtime_stats = true;
        g_river_voice_vad_probe.diag_last_runtime_stats_log_ms = now_ms;
    }

    if (should_log_runtime_stats) {
        river_runtime_stats_snapshot(detector_is_speech ? "vad_speech" : "vad_silence");
        river_voice_preproc_dump_runtime_stats(&g_river_voice_vad_probe.preproc);
    }

    g_river_voice_vad_probe.diag_vad_last_logged_is_speech = detector_is_speech;
}

static void river_voice_vad_probe_consider_barge_in(bool detector_decision_valid,
                                                    bool detector_is_speech,
                                                    uint16_t playback_ref_peak)
{
    river_interaction_state_t interaction_state;
    uint64_t now_ms;
    bool near_end_speech;

    interaction_state = river_interaction_state_get();
    if (!river_voice_vad_probe_playback_active() ||
        (interaction_state != RIVER_INTERACTION_SPEAKING &&
         interaction_state != RIVER_INTERACTION_BARGE_IN_LISTENING)) {
        g_river_voice_vad_probe.barge_in_hit_frames = 0U;
        return;
    }

    if (!detector_decision_valid || !detector_is_speech || playback_ref_peak == 0U) {
        g_river_voice_vad_probe.barge_in_hit_frames = 0U;
        return;
    }

    near_end_speech =
        g_river_voice_vad_probe.diag_enhanced_peak >= RIVER_VOICE_VAD_PROBE_BARGE_IN_MIN_ENHANCED_PEAK &&
        g_river_voice_vad_probe.diag_enhanced_peak >
            (uint16_t)(playback_ref_peak + RIVER_VOICE_VAD_PROBE_BARGE_IN_REF_MARGIN_PEAK) &&
        ((uint32_t)g_river_voice_vad_probe.diag_enhanced_peak * 100U) >=
            ((uint32_t)playback_ref_peak * RIVER_VOICE_VAD_PROBE_BARGE_IN_RATIO_PCT);
    if (!near_end_speech) {
        g_river_voice_vad_probe.barge_in_hit_frames = 0U;
        return;
    }

    if (g_river_voice_vad_probe.barge_in_hit_frames < 0xFFU) {
        g_river_voice_vad_probe.barge_in_hit_frames++;
    }
    if (g_river_voice_vad_probe.barge_in_hit_frames < RIVER_VOICE_VAD_PROBE_BARGE_IN_HIT_FRAMES) {
        return;
    }

    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    if (g_river_voice_vad_probe.barge_in_last_trigger_ms != 0U &&
        (now_ms - g_river_voice_vad_probe.barge_in_last_trigger_ms) <
            (uint64_t)RIVER_VOICE_VAD_PROBE_BARGE_IN_COOLDOWN_MS) {
        return;
    }

    g_river_voice_vad_probe.barge_in_last_trigger_ms = now_ms;
    g_river_voice_vad_probe.barge_in_hit_frames = 0U;
    g_river_voice_vad_probe.diag_barge_in_triggered++;
    RIVER_LOGI("barge-in detected: afe_peak=%u ref_peak=%u prob_q15=%u -> interrupt tts",
               (unsigned int)g_river_voice_vad_probe.diag_enhanced_peak,
               (unsigned int)playback_ref_peak,
               (unsigned int)g_river_voice_vad_probe.diag_vad_probability_q15);
    (void)river_cloud_adapter_interrupt_tts_with_reason("barge_in_near_end_vad");
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
    river_voice_frame_pool_release();
    g_river_voice_vad_probe.enhanced_buffer = 0;
    g_river_voice_vad_probe.reference_buffer = 0;
    g_river_voice_vad_probe.playback_ref_buffer = 0;
    g_river_voice_vad_probe.capture_buffer = 0;

    g_river_voice_vad_probe.capture_chunk_bytes = 0U;
    g_river_voice_vad_probe.enhanced_chunk_bytes = 0U;
    g_river_voice_vad_probe.reference_chunk_bytes = 0U;
    g_river_voice_vad_probe.playback_ref_chunk_bytes = 0U;
    river_voice_vad_probe_reset_diag_counters();
}

static river_status_t river_voice_vad_probe_prepare_buffers(void)
{
    river_voice_segment_buffer_config_t segment_config;
    river_voice_frame_pool_layout_t pool_layout;
    river_voice_frame_pool_view_t pool_view;
    uint32_t required_segment_bytes;
    uint32_t free_heap;

    g_river_voice_vad_probe.capture_chunk_bytes = g_river_voice_vad_probe.capture.frame_bytes;
    g_river_voice_vad_probe.enhanced_chunk_bytes =
        river_voice_preproc_output_frame_bytes(&g_river_voice_vad_probe.preproc);
    g_river_voice_vad_probe.reference_chunk_bytes =
        river_voice_preproc_reference_frame_bytes(&g_river_voice_vad_probe.preproc);
    g_river_voice_vad_probe.playback_ref_chunk_bytes =
        g_river_voice_vad_probe.capture.frame_samples * sizeof(int16_t);
    g_river_voice_vad_probe.diag_enabled = g_river_voice_vad_probe_diag_enabled;
    g_river_voice_vad_probe.segment_buffer_enabled = false;

    memset(&pool_layout, 0, sizeof(pool_layout));
    memset(&pool_view, 0, sizeof(pool_view));
    pool_layout.capture_bytes = g_river_voice_vad_probe.capture_chunk_bytes;
    pool_layout.enhanced_bytes = g_river_voice_vad_probe.enhanced_chunk_bytes;
    if (river_voice_preproc_reference_enabled(&g_river_voice_vad_probe.preproc)) {
        pool_layout.reference_bytes = g_river_voice_vad_probe.reference_chunk_bytes;
    } else {
        pool_layout.playback_ref_bytes = g_river_voice_vad_probe.playback_ref_chunk_bytes;
    }
    if (river_voice_frame_pool_acquire(&pool_layout, &pool_view) != RIVER_OK) {
        river_voice_vad_probe_release_buffers();
        return RIVER_ERR_NO_MEMORY;
    }
    g_river_voice_vad_probe.capture_buffer = pool_view.capture_buffer;
    g_river_voice_vad_probe.enhanced_buffer = pool_view.enhanced_buffer;
    g_river_voice_vad_probe.reference_buffer = pool_view.reference_buffer;
    g_river_voice_vad_probe.playback_ref_buffer = pool_view.playback_ref_buffer;

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
    const river_voice_profile_config_t *voice_profile;
    bool use_reference;

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
    voice_profile = river_voice_profile_active();
    use_reference = river_voice_preproc_reference_enabled(&g_river_voice_vad_probe.preproc);
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

    RIVER_LOGI("vad probe config: %lu Hz capture %s -> %s 1ch -> detector-only, %s+%s, diag_window~%ums",
               (unsigned long)g_river_voice_vad_probe.capture.sample_rate,
               voice_profile->uses_native_capture_ref ? "2mic+ref(native ch3)" : "dual-mic",
               voice_profile->experimental ? "fixed_dsb+webrtc_aecm(exp/native_ref)" : "fixed_dsb",
               river_voice_board_mic_name(river_voice_board_array_profile()->primary_mic),
               river_voice_board_mic_name(river_voice_board_array_profile()->secondary_mic),
               (unsigned int)RIVER_VOICE_VAD_PROBE_DIAG_WINDOW_MS);
    if (voice_profile->uses_native_capture_ref) {
        RIVER_LOGI("vad probe aec ref: source=native_capture_ch3 frame=%luB external_ref=%s",
                   (unsigned long)(g_river_voice_vad_probe.capture.frame_samples * sizeof(int16_t)),
                   use_reference ? "enabled" : "disabled");
    }
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
        const bool use_reference = river_voice_preproc_reference_enabled(&g_river_voice_vad_probe.preproc);
        uint16_t playback_ref_peak = 0U;

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
        river_voice_experiment_submit_frame(RIVER_VOICE_EXPERIMENT_DOMAIN_CAPTURE_RAW,
                                            g_river_voice_vad_probe.capture_buffer,
                                            g_river_voice_vad_probe.capture_chunk_bytes,
                                            g_river_voice_vad_probe.capture.channels);

        if (use_reference && g_river_voice_vad_probe.reference_buffer != 0) {
            memset(g_river_voice_vad_probe.reference_buffer, 0, g_river_voice_vad_probe.reference_chunk_bytes);
            if (river_reference_service_read(g_river_voice_vad_probe.reference_buffer,
                                             g_river_voice_vad_probe.reference_chunk_bytes) == RIVER_OK) {
                g_river_voice_vad_probe.diag_ref_read_ok++;
                river_voice_experiment_submit_frame(RIVER_VOICE_EXPERIMENT_DOMAIN_REFERENCE,
                                                    g_river_voice_vad_probe.reference_buffer,
                                                    g_river_voice_vad_probe.reference_chunk_bytes,
                                                    1U);
            } else {
                g_river_voice_vad_probe.diag_ref_read_miss++;
            }
        }
        if (!use_reference &&
            g_river_voice_vad_probe.playback_ref_buffer != 0 &&
            g_river_voice_vad_probe.playback_ref_chunk_bytes > 0U &&
            river_voice_vad_probe_playback_active() &&
            river_playback_service_reference_enabled()) {
            memset(g_river_voice_vad_probe.playback_ref_buffer,
                   0,
                   g_river_voice_vad_probe.playback_ref_chunk_bytes);
            if (river_reference_service_read(g_river_voice_vad_probe.playback_ref_buffer,
                                             g_river_voice_vad_probe.playback_ref_chunk_bytes) == RIVER_OK) {
                g_river_voice_vad_probe.diag_playback_ref_read_ok++;
                playback_ref_peak =
                    river_voice_vad_probe_update_peak(g_river_voice_vad_probe.playback_ref_buffer,
                                                      g_river_voice_vad_probe.playback_ref_chunk_bytes,
                                                      1U,
                                                      &g_river_voice_vad_probe.diag_playback_ref_peak,
                                                      &g_river_voice_vad_probe.diag_playback_ref_peak);
                river_voice_experiment_submit_frame(RIVER_VOICE_EXPERIMENT_DOMAIN_REFERENCE,
                                                    g_river_voice_vad_probe.playback_ref_buffer,
                                                    g_river_voice_vad_probe.playback_ref_chunk_bytes,
                                                    1U);
            } else {
                g_river_voice_vad_probe.diag_playback_ref_read_miss++;
            }
        }

        enhanced_bytes = 0U;
        if (river_voice_preproc_process(&g_river_voice_vad_probe.preproc,
                                        g_river_voice_vad_probe.capture_buffer,
                                        g_river_voice_vad_probe.capture_chunk_bytes,
                                        use_reference ? g_river_voice_vad_probe.reference_buffer : 0,
                                        use_reference ? g_river_voice_vad_probe.reference_chunk_bytes : 0U,
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
        river_voice_experiment_submit_frame(RIVER_VOICE_EXPERIMENT_DOMAIN_PREPROC_MONO,
                                            g_river_voice_vad_probe.enhanced_buffer,
                                            g_river_voice_vad_probe.enhanced_chunk_bytes,
                                            1U);

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

            river_voice_vad_probe_consider_barge_in(detector_result.decision_valid,
                                                    detector_result.is_speech,
                                                    playback_ref_peak);
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
