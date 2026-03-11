#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "basic_types.h"
#include "os_wrapper.h"

#include "audio/audio_control.h"
#include "audio/audio_service.h"
#include "audio/audio_track.h"

#include "river/river_board_rgb.h"
#include "river/river_voice.h"
#include "river/river_voice_board.h"
#include "river/river_voice_capture.h"
#include "river/river_voice_detector.h"
#include "river/river_voice_preproc.h"
#include "river/river_voice_ref.h"
#include "river/river_voice_vad_reference.h"

#define RIVER_VOICE_ECHO_PLAYBACK_CHANNELS     2U
#define RIVER_VOICE_ECHO_BYTES_PER_SAMPLE      2U
#define RIVER_VOICE_ECHO_TARGET_DELAY_MS       1000U
#define RIVER_VOICE_ECHO_WARMUP_MS             96U
#define RIVER_VOICE_ECHO_TASK_STACK            (1024U * 12U)
#define RIVER_VOICE_ECHO_TASK_PRIORITY         4U
#define RIVER_VOICE_ECHO_CAPTURE_VOLUME        0x30U
#define RIVER_VOICE_ECHO_CAPTURE_HPF_FC        0U
#define RIVER_VOICE_ECHO_DIAG_WINDOW_MS        1000U
#define RIVER_VOICE_ECHO_PLAYBACK_HW_VOLUME    0.80f
#define RIVER_VOICE_ECHO_PLAYBACK_SW_VOLUME    1.00f
#define RIVER_VOICE_ECHO_PLAYBACK_PCM_GAIN     2U
#define RIVER_VOICE_ECHO_POST_AGC_GATE         192U
#define RIVER_VOICE_ECHO_POST_AGC_TARGET_PEAK  9000U
#define RIVER_VOICE_ECHO_POST_AGC_MAX_GAIN     2U
#define RIVER_VOICE_ECHO_REF_HISTORY_MS        1536U

typedef struct {
    bool running;
    bool stop_requested;
    bool track_started;
    bool diag_enabled;
    bool vad_reference_enabled;
    rtos_task_t task;
    river_voice_capture_t capture;
    river_voice_preproc_t preproc;
    river_voice_detector_t detector;
    struct AudioTrack *track;
    uint8_t *delay_buffer;
    uint8_t *capture_buffer;
    uint8_t *enhanced_buffer;
    uint8_t *reference_buffer;
    uint8_t *playback_buffer;
    uint8_t *track_buffer;
    size_t capture_chunk_bytes;
    size_t enhanced_chunk_bytes;
    size_t playback_chunk_bytes;
    size_t delay_buffer_bytes;
    size_t read_offset;
    size_t write_offset;
    uint32_t actual_delay_ms;
    uint32_t warmup_bytes_remaining;
    uint32_t diag_read_ok;
    uint32_t diag_proc_ok;
    uint32_t diag_write_ok;
    uint32_t diag_read_fail;
    uint32_t diag_proc_fail;
    uint32_t diag_write_fail;
    uint32_t diag_partial_read;
    uint32_t diag_partial_proc;
    uint32_t diag_ref_read_ok;
    uint32_t diag_ref_read_miss;
    uint32_t diag_ref_write_ok;
    uint32_t diag_ref_write_fail;
    uint32_t diag_det_ok;
    uint32_t diag_det_fail;
    uint32_t diag_vad_decisions;
    uint32_t diag_vad_speech;
    uint32_t diag_sdk_vad_events;
    uint32_t diag_sdk_vad_speech_start;
    uint32_t diag_sdk_vad_speech_end;
    uint32_t diag_sdk_vad_last_offset_ms;
    uint32_t diag_chunks_until_log;
    uint16_t diag_capture_peak_ch0;
    uint16_t diag_capture_peak_ch1;
    uint16_t diag_enhanced_peak;
    uint16_t diag_playback_peak_ch0;
    uint16_t diag_playback_peak_ch1;
    uint16_t diag_vad_probability_raw_q15;
    uint16_t diag_vad_probability_q15;
    bool diag_vad_is_speech;
    bool diag_sdk_vad_is_speech;
} river_voice_echo_context_t;

static river_voice_echo_context_t g_river_voice_echo;
static bool g_river_voice_echo_diag_enabled;

static size_t river_voice_min_size(size_t left, size_t right)
{
    return left < right ? left : right;
}

static uint16_t river_voice_echo_abs16(int32_t value)
{
    if (value < 0) {
        value = -value;
    }
    if (value > 32767) {
        value = 32767;
    }
    return (uint16_t)value;
}

static int16_t river_voice_echo_sat16(int32_t value)
{
    if (value > 32767) {
        return 32767;
    }
    if (value < -32768) {
        return -32768;
    }
    return (int16_t)value;
}

static uint16_t river_voice_echo_update_peak(const uint8_t *buffer,
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

            peak = river_voice_echo_abs16(samples[index]);
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

        peak0 = river_voice_echo_abs16(samples[index]);
        peak1 = river_voice_echo_abs16(samples[index + 1U]);
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

static void river_voice_echo_reset_diag_counters(void)
{
    g_river_voice_echo.diag_read_ok = 0U;
    g_river_voice_echo.diag_proc_ok = 0U;
    g_river_voice_echo.diag_write_ok = 0U;
    g_river_voice_echo.diag_read_fail = 0U;
    g_river_voice_echo.diag_proc_fail = 0U;
    g_river_voice_echo.diag_write_fail = 0U;
    g_river_voice_echo.diag_partial_read = 0U;
    g_river_voice_echo.diag_partial_proc = 0U;
    g_river_voice_echo.diag_ref_read_ok = 0U;
    g_river_voice_echo.diag_ref_read_miss = 0U;
    g_river_voice_echo.diag_ref_write_ok = 0U;
    g_river_voice_echo.diag_ref_write_fail = 0U;
    g_river_voice_echo.diag_det_ok = 0U;
    g_river_voice_echo.diag_det_fail = 0U;
    g_river_voice_echo.diag_vad_decisions = 0U;
    g_river_voice_echo.diag_vad_speech = 0U;
    g_river_voice_echo.diag_sdk_vad_events = 0U;
    g_river_voice_echo.diag_sdk_vad_speech_start = 0U;
    g_river_voice_echo.diag_sdk_vad_speech_end = 0U;
    g_river_voice_echo.diag_sdk_vad_last_offset_ms = 0U;
    g_river_voice_echo.diag_capture_peak_ch0 = 0U;
    g_river_voice_echo.diag_capture_peak_ch1 = 0U;
    g_river_voice_echo.diag_enhanced_peak = 0U;
    g_river_voice_echo.diag_playback_peak_ch0 = 0U;
    g_river_voice_echo.diag_playback_peak_ch1 = 0U;
    g_river_voice_echo.diag_vad_probability_raw_q15 = 0U;
    g_river_voice_echo.diag_vad_probability_q15 = 0U;
    g_river_voice_echo.diag_vad_is_speech = false;
    g_river_voice_echo.diag_sdk_vad_is_speech = false;
    g_river_voice_echo.diag_chunks_until_log = RIVER_VOICE_ECHO_DIAG_WINDOW_MS /
                                               river_voice_board_array_profile()->frame_ms;
}

static void river_voice_echo_log_diagnostics_if_needed(void)
{
    if (!g_river_voice_echo.diag_enabled) {
        return;
    }

    if (g_river_voice_echo.diag_chunks_until_log > 0U) {
        g_river_voice_echo.diag_chunks_until_log--;
    }
    if (g_river_voice_echo.diag_chunks_until_log > 0U) {
        return;
    }

    printf("[river][voice][diag] cap_peak=[%u,%u] afe_peak=%u play_peak=[%u,%u] vad_raw_q15=%u vad_prob_q15=%u vad=%s vad_decisions=%lu vad_speech=%lu sdk_vad=%s sdk_events=%lu sdk_start=%lu sdk_end=%lu sdk_offset_ms=%lu read_ok=%lu proc_ok=%lu det_ok=%lu write_ok=%lu ref_read_ok=%lu ref_read_miss=%lu ref_write_ok=%lu ref_write_fail=%lu read_fail=%lu proc_fail=%lu det_fail=%lu write_fail=%lu partial_read=%lu partial_proc=%lu\n",
           (unsigned int)g_river_voice_echo.diag_capture_peak_ch0,
           (unsigned int)g_river_voice_echo.diag_capture_peak_ch1,
           (unsigned int)g_river_voice_echo.diag_enhanced_peak,
           (unsigned int)g_river_voice_echo.diag_playback_peak_ch0,
           (unsigned int)g_river_voice_echo.diag_playback_peak_ch1,
           (unsigned int)g_river_voice_echo.diag_vad_probability_raw_q15,
           (unsigned int)g_river_voice_echo.diag_vad_probability_q15,
           g_river_voice_echo.diag_vad_is_speech ? "speech" : "silence",
           (unsigned long)g_river_voice_echo.diag_vad_decisions,
           (unsigned long)g_river_voice_echo.diag_vad_speech,
           g_river_voice_echo.vad_reference_enabled ?
               (g_river_voice_echo.diag_sdk_vad_is_speech ? "speech" : "silence") :
               "disabled",
           (unsigned long)g_river_voice_echo.diag_sdk_vad_events,
           (unsigned long)g_river_voice_echo.diag_sdk_vad_speech_start,
           (unsigned long)g_river_voice_echo.diag_sdk_vad_speech_end,
           (unsigned long)g_river_voice_echo.diag_sdk_vad_last_offset_ms,
           (unsigned long)g_river_voice_echo.diag_read_ok,
           (unsigned long)g_river_voice_echo.diag_proc_ok,
           (unsigned long)g_river_voice_echo.diag_det_ok,
           (unsigned long)g_river_voice_echo.diag_write_ok,
           (unsigned long)g_river_voice_echo.diag_ref_read_ok,
           (unsigned long)g_river_voice_echo.diag_ref_read_miss,
           (unsigned long)g_river_voice_echo.diag_ref_write_ok,
           (unsigned long)g_river_voice_echo.diag_ref_write_fail,
           (unsigned long)g_river_voice_echo.diag_read_fail,
           (unsigned long)g_river_voice_echo.diag_proc_fail,
           (unsigned long)g_river_voice_echo.diag_det_fail,
           (unsigned long)g_river_voice_echo.diag_write_fail,
           (unsigned long)g_river_voice_echo.diag_partial_read,
           (unsigned long)g_river_voice_echo.diag_partial_proc);

    river_voice_echo_reset_diag_counters();
}

static void river_voice_echo_ring_read(void *dst, size_t bytes)
{
    size_t first_copy;

    first_copy = river_voice_min_size(bytes, g_river_voice_echo.delay_buffer_bytes - g_river_voice_echo.read_offset);
    memcpy(dst, g_river_voice_echo.delay_buffer + g_river_voice_echo.read_offset, first_copy);
    if (bytes > first_copy) {
        memcpy((uint8_t *)dst + first_copy, g_river_voice_echo.delay_buffer, bytes - first_copy);
    }

    g_river_voice_echo.read_offset = (g_river_voice_echo.read_offset + bytes) % g_river_voice_echo.delay_buffer_bytes;
}

static void river_voice_echo_ring_write(const void *src, size_t bytes)
{
    size_t first_copy;

    first_copy = river_voice_min_size(bytes, g_river_voice_echo.delay_buffer_bytes - g_river_voice_echo.write_offset);
    memcpy(g_river_voice_echo.delay_buffer + g_river_voice_echo.write_offset, src, first_copy);
    if (bytes > first_copy) {
        memcpy(g_river_voice_echo.delay_buffer, (const uint8_t *)src + first_copy, bytes - first_copy);
    }

    g_river_voice_echo.write_offset = (g_river_voice_echo.write_offset + bytes) % g_river_voice_echo.delay_buffer_bytes;
}

static void river_voice_echo_apply_warmup(uint8_t *buffer, size_t bytes)
{
    size_t mute_bytes;

    if (g_river_voice_echo.warmup_bytes_remaining == 0U) {
        return;
    }

    mute_bytes = river_voice_min_size(bytes, (size_t)g_river_voice_echo.warmup_bytes_remaining);
    memset(buffer, 0, mute_bytes);
    g_river_voice_echo.warmup_bytes_remaining -= (uint32_t)mute_bytes;
}

static uint16_t river_voice_echo_apply_post_agc(uint8_t *buffer, size_t bytes)
{
    int16_t *samples;
    size_t sample_count;
    size_t index;
    uint16_t peak;
    uint32_t gain;

    if (buffer == 0 || bytes < sizeof(int16_t)) {
        return 0U;
    }

    peak = 0U;
    river_voice_echo_update_peak(buffer, bytes, 1U, &peak, &peak);
    if (peak < RIVER_VOICE_ECHO_POST_AGC_GATE) {
        memset(buffer, 0, bytes);
        return 0U;
    }

    gain = 1U;
    if (peak < RIVER_VOICE_ECHO_POST_AGC_TARGET_PEAK) {
        gain = (RIVER_VOICE_ECHO_POST_AGC_TARGET_PEAK + peak - 1U) / peak;
        if (gain > RIVER_VOICE_ECHO_POST_AGC_MAX_GAIN) {
            gain = RIVER_VOICE_ECHO_POST_AGC_MAX_GAIN;
        }
    }
    if (gain == 1U) {
        return peak;
    }

    samples = (int16_t *)buffer;
    sample_count = bytes / sizeof(int16_t);
    peak = 0U;
    for (index = 0; index < sample_count; ++index) {
        int16_t sample;
        uint16_t abs_peak;

        sample = river_voice_echo_sat16((int32_t)samples[index] * (int32_t)gain);
        samples[index] = sample;
        abs_peak = river_voice_echo_abs16(sample);
        if (abs_peak > peak) {
            peak = abs_peak;
        }
    }

    return peak;
}

static void river_voice_echo_expand_mono_to_stereo(uint8_t *dst, const uint8_t *src, size_t mono_bytes)
{
    const int16_t *src_samples;
    int16_t *dst_samples;
    size_t frame_count;
    size_t index;

    if (dst == 0 || src == 0) {
        return;
    }

    src_samples = (const int16_t *)src;
    dst_samples = (int16_t *)dst;
    frame_count = mono_bytes / sizeof(int16_t);

    for (index = 0; index < frame_count; ++index) {
        int16_t sample;

        sample = river_voice_echo_sat16((int32_t)src_samples[index] *
                                        (int32_t)RIVER_VOICE_ECHO_PLAYBACK_PCM_GAIN);
        *dst_samples++ = sample;
        *dst_samples++ = sample;
    }
}

static void river_voice_echo_close_audio(void)
{
    if (g_river_voice_echo.track != 0) {
        if (g_river_voice_echo.track_started) {
            AudioTrack_Pause(g_river_voice_echo.track);
            AudioTrack_Flush(g_river_voice_echo.track);
            AudioTrack_Stop(g_river_voice_echo.track);
            g_river_voice_echo.track_started = false;
        }
        AudioTrack_Destroy(g_river_voice_echo.track);
        g_river_voice_echo.track = 0;
    }

    river_voice_vad_reference_close();
    river_voice_detector_close(&g_river_voice_echo.detector);
    river_voice_preproc_close(&g_river_voice_echo.preproc);
    river_voice_capture_close(&g_river_voice_echo.capture);
    river_voice_ref_close();
}

static void river_voice_echo_release_buffers(void)
{
    if (g_river_voice_echo.track_buffer != 0) {
        rtos_mem_free(g_river_voice_echo.track_buffer);
        g_river_voice_echo.track_buffer = 0;
    }

    if (g_river_voice_echo.playback_buffer != 0) {
        rtos_mem_free(g_river_voice_echo.playback_buffer);
        g_river_voice_echo.playback_buffer = 0;
    }

    if (g_river_voice_echo.enhanced_buffer != 0) {
        rtos_mem_free(g_river_voice_echo.enhanced_buffer);
        g_river_voice_echo.enhanced_buffer = 0;
    }

    if (g_river_voice_echo.reference_buffer != 0) {
        rtos_mem_free(g_river_voice_echo.reference_buffer);
        g_river_voice_echo.reference_buffer = 0;
    }

    if (g_river_voice_echo.capture_buffer != 0) {
        rtos_mem_free(g_river_voice_echo.capture_buffer);
        g_river_voice_echo.capture_buffer = 0;
    }

    if (g_river_voice_echo.delay_buffer != 0) {
        rtos_mem_free(g_river_voice_echo.delay_buffer);
        g_river_voice_echo.delay_buffer = 0;
    }

    g_river_voice_echo.capture_chunk_bytes = 0U;
    g_river_voice_echo.enhanced_chunk_bytes = 0U;
    g_river_voice_echo.playback_chunk_bytes = 0U;
    g_river_voice_echo.delay_buffer_bytes = 0U;
    g_river_voice_echo.read_offset = 0U;
    g_river_voice_echo.write_offset = 0U;
    g_river_voice_echo.actual_delay_ms = 0U;
    g_river_voice_echo.warmup_bytes_remaining = 0U;
    river_voice_echo_reset_diag_counters();
}

static river_status_t river_voice_echo_prepare_buffers(void)
{
    uint32_t delay_frames;
    const river_voice_board_array_profile_t *profile;
    bool use_reference;

    profile = river_voice_board_array_profile();
    use_reference = river_voice_preproc_reference_enabled(&g_river_voice_echo.preproc);
    g_river_voice_echo.capture_chunk_bytes = g_river_voice_echo.capture.frame_bytes;
    g_river_voice_echo.enhanced_chunk_bytes = river_voice_preproc_output_frame_bytes(&g_river_voice_echo.preproc);
    g_river_voice_echo.playback_chunk_bytes = g_river_voice_echo.enhanced_chunk_bytes *
                                              RIVER_VOICE_ECHO_PLAYBACK_CHANNELS;
    delay_frames = (RIVER_VOICE_ECHO_TARGET_DELAY_MS + (profile->frame_ms / 2U)) / profile->frame_ms;
    if (delay_frames == 0U) {
        delay_frames = 1U;
    }
    g_river_voice_echo.delay_buffer_bytes = g_river_voice_echo.enhanced_chunk_bytes * delay_frames;
    g_river_voice_echo.actual_delay_ms = delay_frames * profile->frame_ms;
    g_river_voice_echo.warmup_bytes_remaining =
        (uint32_t)(((uint64_t)g_river_voice_echo.enhanced_chunk_bytes * RIVER_VOICE_ECHO_WARMUP_MS) /
                   g_river_voice_echo.preproc.frame_ms);
    g_river_voice_echo.diag_enabled = g_river_voice_echo_diag_enabled;

    g_river_voice_echo.delay_buffer = (uint8_t *)rtos_mem_zmalloc((uint32_t)g_river_voice_echo.delay_buffer_bytes);
    g_river_voice_echo.capture_buffer = (uint8_t *)rtos_mem_zmalloc((uint32_t)g_river_voice_echo.capture_chunk_bytes);
    g_river_voice_echo.enhanced_buffer = (uint8_t *)rtos_mem_zmalloc((uint32_t)g_river_voice_echo.enhanced_chunk_bytes);
    g_river_voice_echo.reference_buffer = use_reference ?
                                          (uint8_t *)rtos_mem_zmalloc((uint32_t)g_river_voice_echo.enhanced_chunk_bytes) :
                                          0;
    g_river_voice_echo.playback_buffer = (uint8_t *)rtos_mem_zmalloc((uint32_t)g_river_voice_echo.enhanced_chunk_bytes);
    g_river_voice_echo.track_buffer = (uint8_t *)rtos_mem_zmalloc((uint32_t)g_river_voice_echo.playback_chunk_bytes);

    if (g_river_voice_echo.delay_buffer == 0 ||
        g_river_voice_echo.capture_buffer == 0 ||
        g_river_voice_echo.enhanced_buffer == 0 ||
        (use_reference && g_river_voice_echo.reference_buffer == 0) ||
        g_river_voice_echo.playback_buffer == 0 ||
        g_river_voice_echo.track_buffer == 0) {
        river_voice_echo_release_buffers();
        return RIVER_ERR_NO_MEMORY;
    }

    return RIVER_OK;
}

static river_status_t river_voice_echo_open_audio(void)
{
    AudioTrackConfig track_config;
    size_t track_buffer_bytes;
    bool use_reference;

    AudioService_Init();
    AudioControl_SetPlaybackDevice(DEVICE_OUT_SPEAKER);
    AudioControl_SetPlaybackMute(false);
    AudioControl_SetAmplifierMute(false);
    AudioControl_SetHardwareVolume(RIVER_VOICE_ECHO_PLAYBACK_HW_VOLUME, RIVER_VOICE_ECHO_PLAYBACK_HW_VOLUME);
    AudioControl_SetCaptureVolume(river_voice_board_array_profile()->capture_channels, RIVER_VOICE_ECHO_CAPTURE_VOLUME);
    AudioControl_SetCaptureHpfFc(0, RIVER_VOICE_ECHO_CAPTURE_HPF_FC);

    if (river_voice_capture_open(&g_river_voice_echo.capture) != RIVER_OK) {
        return RIVER_ERR_UNSUPPORTED;
    }

    if (river_voice_preproc_open(&g_river_voice_echo.preproc) != RIVER_OK) {
        printf("[river][voice] preproc open failed\n");
        return RIVER_ERR_UNSUPPORTED;
    }

    if (river_voice_detector_open(&g_river_voice_echo.detector) != RIVER_OK) {
        printf("[river][voice] detector open failed\n");
        return RIVER_ERR_UNSUPPORTED;
    }
    if (river_voice_detector_input_frame_bytes(&g_river_voice_echo.detector) !=
        river_voice_preproc_output_frame_bytes(&g_river_voice_echo.preproc)) {
        printf("[river][voice] detector/preproc frame mismatch: detector=%luB preproc=%luB\n",
               (unsigned long)river_voice_detector_input_frame_bytes(&g_river_voice_echo.detector),
               (unsigned long)river_voice_preproc_output_frame_bytes(&g_river_voice_echo.preproc));
        return RIVER_ERR_UNSUPPORTED;
    }

    use_reference = river_voice_preproc_reference_enabled(&g_river_voice_echo.preproc);
    if (use_reference &&
        river_voice_ref_open(g_river_voice_echo.capture.sample_rate,
                             g_river_voice_echo.capture.frame_ms,
                             1U,
                             RIVER_VOICE_ECHO_REF_HISTORY_MS) != RIVER_OK) {
        printf("[river][voice] playback ref open failed\n");
        return RIVER_ERR_UNSUPPORTED;
    }

    g_river_voice_echo.vad_reference_enabled =
        (river_voice_vad_reference_open() == RIVER_OK);
    if (!g_river_voice_echo.vad_reference_enabled) {
        printf("[river][voice] sdk_vad reference unavailable; keep silero-only decision logging\n");
    }

    if (river_voice_echo_prepare_buffers() != RIVER_OK) {
        return RIVER_ERR_NO_MEMORY;
    }

    g_river_voice_echo.track = AudioTrack_Create();
    if (g_river_voice_echo.track == 0) {
        printf("[river][voice] create AudioTrack failed\n");
        return RIVER_ERR_UNSUPPORTED;
    }

    track_buffer_bytes = AudioTrack_GetMinBufferBytes(g_river_voice_echo.track,
                                                      AUDIO_CATEGORY_MEDIA,
                                                      g_river_voice_echo.capture.sample_rate,
                                                      AUDIO_FORMAT_PCM_16_BIT,
                                                      RIVER_VOICE_ECHO_PLAYBACK_CHANNELS);
    if (track_buffer_bytes < g_river_voice_echo.playback_chunk_bytes) {
        track_buffer_bytes = g_river_voice_echo.playback_chunk_bytes;
    }
    track_buffer_bytes *= 4U;

    track_config.category_type = AUDIO_CATEGORY_MEDIA;
    track_config.sample_rate = g_river_voice_echo.capture.sample_rate;
    track_config.format = AUDIO_FORMAT_PCM_16_BIT;
    track_config.channel_count = RIVER_VOICE_ECHO_PLAYBACK_CHANNELS;
    track_config.buffer_bytes = (uint32_t)track_buffer_bytes;
    if (AudioTrack_Init(g_river_voice_echo.track, &track_config, AUDIO_OUTPUT_FLAG_NONE) != 0) {
        printf("[river][voice] AudioTrack_Init failed\n");
        return RIVER_ERR_UNSUPPORTED;
    }

    AudioTrack_SetVolume(g_river_voice_echo.track,
                         RIVER_VOICE_ECHO_PLAYBACK_SW_VOLUME,
                         RIVER_VOICE_ECHO_PLAYBACK_SW_VOLUME);
    AudioTrack_SetStartThresholdBytes(g_river_voice_echo.track, (int32_t)track_buffer_bytes);

    if (AudioTrack_Start(g_river_voice_echo.track) != 0) {
        printf("[river][voice] AudioTrack_Start failed\n");
        return RIVER_ERR_UNSUPPORTED;
    }
    g_river_voice_echo.track_started = true;

    if (use_reference) {
        printf("[river][voice] audio echo config: %lu Hz capture dual-mic + 1ch ref -> AEC/AFE 1ch -> %lu Hz playback dual-mono, %lu ms delay, %s+%s -> speaker\n",
               (unsigned long)g_river_voice_echo.capture.sample_rate,
               (unsigned long)g_river_voice_echo.capture.sample_rate,
               (unsigned long)g_river_voice_echo.actual_delay_ms,
               river_voice_board_mic_name(river_voice_board_array_profile()->primary_mic),
               river_voice_board_mic_name(river_voice_board_array_profile()->secondary_mic));
    } else {
        printf("[river][voice] audio echo config: %lu Hz capture dual-mic -> ASR-AFE 1ch -> %lu Hz playback dual-mono, %lu ms delay, %s+%s -> speaker\n",
               (unsigned long)g_river_voice_echo.capture.sample_rate,
               (unsigned long)g_river_voice_echo.capture.sample_rate,
               (unsigned long)g_river_voice_echo.actual_delay_ms,
               river_voice_board_mic_name(river_voice_board_array_profile()->primary_mic),
               river_voice_board_mic_name(river_voice_board_array_profile()->secondary_mic));
    }
    printf("[river][voice] audio echo gain: hw=%.2f sw=%.2f pcm=x%lu post_agc=target%u/maxx%lu floor=%u cap=0x%02lx preproc=%s detector=%s\n",
           (double)RIVER_VOICE_ECHO_PLAYBACK_HW_VOLUME,
           (double)RIVER_VOICE_ECHO_PLAYBACK_SW_VOLUME,
           (unsigned long)RIVER_VOICE_ECHO_PLAYBACK_PCM_GAIN,
           (unsigned int)RIVER_VOICE_ECHO_POST_AGC_TARGET_PEAK,
           (unsigned long)RIVER_VOICE_ECHO_POST_AGC_MAX_GAIN,
           (unsigned int)RIVER_VOICE_ECHO_POST_AGC_GATE,
           (unsigned long)RIVER_VOICE_ECHO_CAPTURE_VOLUME,
           river_voice_preproc_backend_name(),
           river_voice_detector_backend_name());
    printf("[river][voice] audio echo ref: backend=%s source=post-delay mono history=%lums aec=%s\n",
           river_voice_ref_backend_name(),
           (unsigned long)RIVER_VOICE_ECHO_REF_HISTORY_MS,
           use_reference ? "on" : "staged-off");
    return RIVER_OK;
}

static void river_voice_echo_task(void *param)
{
    const bool use_reference = river_voice_preproc_reference_enabled(&g_river_voice_echo.preproc);

    (void)param;

#ifdef CONFIG_RIVER_BOARD_RGB_VAD_INDICATOR_EN
    river_board_rgb_set_state(RIVER_BOARD_RGB_STATE_VAD_SILENCE);
#endif

    while (!g_river_voice_echo.stop_requested) {
        int32_t bytes_read;
        size_t enhanced_bytes;
        river_voice_detector_result_t detector_result;

        memset(g_river_voice_echo.capture_buffer, 0, g_river_voice_echo.capture_chunk_bytes);
        if (use_reference && g_river_voice_echo.reference_buffer != 0) {
            memset(g_river_voice_echo.reference_buffer, 0, g_river_voice_echo.enhanced_chunk_bytes);
        }
        bytes_read = river_voice_capture_read(&g_river_voice_echo.capture,
                                             g_river_voice_echo.capture_buffer,
                                             g_river_voice_echo.capture_chunk_bytes);
        if (bytes_read < 0) {
            g_river_voice_echo.diag_read_fail++;
            river_voice_echo_log_diagnostics_if_needed();
            printf("[river][voice] AudioRecord_Read failed: %ld\n", (long)bytes_read);
            rtos_time_delay_ms(g_river_voice_echo.capture.frame_ms);
            continue;
        }
        g_river_voice_echo.diag_read_ok++;
        if ((size_t)bytes_read < g_river_voice_echo.capture_chunk_bytes) {
            memset(g_river_voice_echo.capture_buffer + bytes_read,
                   0,
                   g_river_voice_echo.capture_chunk_bytes - (size_t)bytes_read);
            g_river_voice_echo.diag_partial_read++;
        }

        river_voice_echo_update_peak(g_river_voice_echo.capture_buffer,
                                     g_river_voice_echo.capture_chunk_bytes,
                                     g_river_voice_echo.capture.channels,
                                     &g_river_voice_echo.diag_capture_peak_ch0,
                                     &g_river_voice_echo.diag_capture_peak_ch1);

        enhanced_bytes = 0U;
        if (use_reference) {
            if (river_voice_ref_read(g_river_voice_echo.reference_buffer,
                                     g_river_voice_echo.enhanced_chunk_bytes) == RIVER_OK) {
                g_river_voice_echo.diag_ref_read_ok++;
            } else {
                g_river_voice_echo.diag_ref_read_miss++;
            }
        }

        if (river_voice_preproc_process(&g_river_voice_echo.preproc,
                                        g_river_voice_echo.capture_buffer,
                                        g_river_voice_echo.capture_chunk_bytes,
                                        use_reference ? g_river_voice_echo.reference_buffer : 0,
                                        use_reference ? g_river_voice_echo.enhanced_chunk_bytes : 0U,
                                        g_river_voice_echo.enhanced_buffer,
                                        g_river_voice_echo.enhanced_chunk_bytes,
                                        &enhanced_bytes) != RIVER_OK) {
            g_river_voice_echo.diag_proc_fail++;
            river_voice_echo_log_diagnostics_if_needed();
            printf("[river][voice] preproc process failed\n");
            continue;
        }
        g_river_voice_echo.diag_proc_ok++;
        if (enhanced_bytes < g_river_voice_echo.enhanced_chunk_bytes) {
            memset(g_river_voice_echo.enhanced_buffer + enhanced_bytes,
                   0,
                   g_river_voice_echo.enhanced_chunk_bytes - enhanced_bytes);
            g_river_voice_echo.diag_partial_proc++;
        }

        if (river_voice_detector_process(&g_river_voice_echo.detector,
                                         g_river_voice_echo.enhanced_buffer,
                                         g_river_voice_echo.enhanced_chunk_bytes,
                                         &detector_result) != RIVER_OK) {
            g_river_voice_echo.diag_det_fail++;
#ifdef CONFIG_RIVER_BOARD_RGB_VAD_INDICATOR_EN
            river_board_rgb_set_state(RIVER_BOARD_RGB_STATE_ERROR);
#endif
            river_voice_echo_log_diagnostics_if_needed();
            printf("[river][voice] detector process failed\n");
            continue;
        }
        g_river_voice_echo.diag_det_ok++;
        if (detector_result.decision_valid) {
            g_river_voice_echo.diag_vad_probability_raw_q15 = detector_result.speech_probability_raw_q15;
            g_river_voice_echo.diag_vad_probability_q15 = detector_result.speech_probability_q15;
            g_river_voice_echo.diag_vad_is_speech = detector_result.is_speech;
            g_river_voice_echo.diag_vad_decisions++;
            if (detector_result.is_speech) {
                g_river_voice_echo.diag_vad_speech++;
            }
#ifdef CONFIG_RIVER_BOARD_RGB_VAD_INDICATOR_EN
            river_board_rgb_set_state(detector_result.is_speech
                                      ? RIVER_BOARD_RGB_STATE_VAD_SPEECH
                                      : RIVER_BOARD_RGB_STATE_VAD_SILENCE);
#endif
        }

        if (g_river_voice_echo.vad_reference_enabled) {
            river_voice_vad_reference_status_t sdk_vad_status;

            if (river_voice_vad_reference_process(g_river_voice_echo.enhanced_buffer,
                                                 g_river_voice_echo.enhanced_chunk_bytes) != RIVER_OK) {
                printf("[river][voice] sdk_vad reference feed failed\n");
            }
            river_voice_vad_reference_get_status(&sdk_vad_status);
            g_river_voice_echo.diag_sdk_vad_is_speech = sdk_vad_status.is_speech;
            g_river_voice_echo.diag_sdk_vad_events = sdk_vad_status.total_events;
            g_river_voice_echo.diag_sdk_vad_speech_start = sdk_vad_status.total_speech_start;
            g_river_voice_echo.diag_sdk_vad_speech_end = sdk_vad_status.total_speech_end;
            g_river_voice_echo.diag_sdk_vad_last_offset_ms = sdk_vad_status.last_offset_ms;
        }

        {
            uint16_t enhanced_peak;

            enhanced_peak = river_voice_echo_apply_post_agc(g_river_voice_echo.enhanced_buffer,
                                                            g_river_voice_echo.enhanced_chunk_bytes);
            if (enhanced_peak > g_river_voice_echo.diag_enhanced_peak) {
                g_river_voice_echo.diag_enhanced_peak = enhanced_peak;
            }
        }

        river_voice_echo_apply_warmup(g_river_voice_echo.enhanced_buffer, g_river_voice_echo.enhanced_chunk_bytes);
        river_voice_echo_ring_read(g_river_voice_echo.playback_buffer, g_river_voice_echo.enhanced_chunk_bytes);
        river_voice_echo_ring_write(g_river_voice_echo.enhanced_buffer, g_river_voice_echo.enhanced_chunk_bytes);
        if (use_reference) {
            if (river_voice_ref_push(g_river_voice_echo.playback_buffer,
                                     g_river_voice_echo.enhanced_chunk_bytes) == RIVER_OK) {
                g_river_voice_echo.diag_ref_write_ok++;
            } else {
                g_river_voice_echo.diag_ref_write_fail++;
            }
        }
        river_voice_echo_expand_mono_to_stereo(g_river_voice_echo.track_buffer,
                                               g_river_voice_echo.playback_buffer,
                                               g_river_voice_echo.enhanced_chunk_bytes);
        river_voice_echo_update_peak(g_river_voice_echo.track_buffer,
                                     g_river_voice_echo.playback_chunk_bytes,
                                     RIVER_VOICE_ECHO_PLAYBACK_CHANNELS,
                                     &g_river_voice_echo.diag_playback_peak_ch0,
                                     &g_river_voice_echo.diag_playback_peak_ch1);

        if (AudioTrack_Write(g_river_voice_echo.track,
                             g_river_voice_echo.track_buffer,
                             g_river_voice_echo.playback_chunk_bytes,
                             true) < 0) {
            g_river_voice_echo.diag_write_fail++;
            river_voice_echo_log_diagnostics_if_needed();
            printf("[river][voice] AudioTrack_Write failed\n");
            break;
        }

        g_river_voice_echo.diag_write_ok++;
        river_voice_echo_log_diagnostics_if_needed();
    }

    river_voice_echo_close_audio();
    river_voice_echo_release_buffers();
#ifdef CONFIG_RIVER_BOARD_RGB_VAD_INDICATOR_EN
    river_board_rgb_set_state(RIVER_BOARD_RGB_STATE_OFF);
#endif
    g_river_voice_echo.stop_requested = false;
    g_river_voice_echo.running = false;
    g_river_voice_echo.task = 0;
    printf("[river][voice] audio echo stopped\n");
    rtos_task_delete(NULL);
}

river_status_t river_voice_echo_start(void)
{
    river_status_t status;

    if (g_river_voice_echo.running) {
        printf("[river][voice] audio echo already running\n");
        return RIVER_OK;
    }

    memset(&g_river_voice_echo, 0, sizeof(g_river_voice_echo));
    river_voice_echo_reset_diag_counters();

    status = river_voice_echo_open_audio();
    if (status != RIVER_OK) {
#ifdef CONFIG_RIVER_BOARD_RGB_VAD_INDICATOR_EN
        river_board_rgb_set_state(RIVER_BOARD_RGB_STATE_ERROR);
#endif
        river_voice_echo_close_audio();
        river_voice_echo_release_buffers();
        return status;
    }

    g_river_voice_echo.running = true;
    if (rtos_task_create(&g_river_voice_echo.task,
                         "river_audio_echo",
                         river_voice_echo_task,
                         0,
                         RIVER_VOICE_ECHO_TASK_STACK,
                         RIVER_VOICE_ECHO_TASK_PRIORITY) != RTK_SUCCESS) {
        printf("[river][voice] create echo task failed\n");
        g_river_voice_echo.running = false;
#ifdef CONFIG_RIVER_BOARD_RGB_VAD_INDICATOR_EN
        river_board_rgb_set_state(RIVER_BOARD_RGB_STATE_ERROR);
#endif
        river_voice_echo_close_audio();
        river_voice_echo_release_buffers();
        return RIVER_ERR_NO_MEMORY;
    }

    printf("[river][voice] audio echo started\n");
    return RIVER_OK;
}

river_status_t river_voice_echo_stop(void)
{
    uint32_t wait_count;

    if (!g_river_voice_echo.running) {
        printf("[river][voice] audio echo already stopped\n");
        return RIVER_OK;
    }

    g_river_voice_echo.stop_requested = true;
    for (wait_count = 0; wait_count < 100U; ++wait_count) {
        if (!g_river_voice_echo.running) {
            return RIVER_OK;
        }
        rtos_time_delay_ms(20U);
    }

    printf("[river][voice] audio echo stop timeout\n");
    return RIVER_ERR_BUSY;
}

bool river_voice_echo_is_running(void)
{
    return g_river_voice_echo.running;
}

void river_voice_echo_set_diag_enabled(bool enabled)
{
    g_river_voice_echo_diag_enabled = enabled;
    g_river_voice_echo.diag_enabled = enabled;
    river_voice_echo_reset_diag_counters();
}

bool river_voice_echo_diag_enabled(void)
{
    return g_river_voice_echo_diag_enabled;
}

const char *river_voice_echo_status_name(void)
{
    return river_voice_echo_is_running() ? "running" : "stopped";
}

void river_voice_echo_dump_status(void)
{
    printf("[river] audio_echo=%s\n", river_voice_echo_status_name());
    printf("[river] audio_echo_diag=%s\n", river_voice_echo_diag_enabled() ? "on" : "off");
    if (river_voice_echo_is_running()) {
        printf("[river] audio_echo_profile=cap:%luHz/%luch(%s+%s) preproc:%s play:%luHz/%luch delay:%lums\n",
               (unsigned long)g_river_voice_echo.capture.sample_rate,
               (unsigned long)g_river_voice_echo.capture.channels,
               river_voice_board_mic_name(river_voice_board_array_profile()->primary_mic),
               river_voice_board_mic_name(river_voice_board_array_profile()->secondary_mic),
               river_voice_preproc_backend_name(),
               (unsigned long)g_river_voice_echo.capture.sample_rate,
               (unsigned long)RIVER_VOICE_ECHO_PLAYBACK_CHANNELS,
               (unsigned long)g_river_voice_echo.actual_delay_ms);
    }
}
