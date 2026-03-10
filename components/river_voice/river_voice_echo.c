#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "basic_types.h"
#include "os_wrapper.h"

#include "audio/audio_control.h"
#include "audio/audio_record.h"
#include "audio/audio_service.h"
#include "audio/audio_track.h"

#include "river/river_voice.h"
#include "river/river_voice_board.h"

#define RIVER_VOICE_ECHO_SAMPLE_RATE           16000U
#define RIVER_VOICE_ECHO_PLAYBACK_CHANNELS     2U
#define RIVER_VOICE_ECHO_BYTES_PER_SAMPLE      2U
#define RIVER_VOICE_ECHO_FRAME_MS              20U
#define RIVER_VOICE_ECHO_DELAY_MS              1000U
#define RIVER_VOICE_ECHO_WARMUP_MS             100U
#define RIVER_VOICE_ECHO_TASK_STACK            (1024U * 12U)
#define RIVER_VOICE_ECHO_TASK_PRIORITY         4U
#define RIVER_VOICE_ECHO_CAPTURE_VOLUME        0x20U
#define RIVER_VOICE_ECHO_DIAG_WINDOW_MS        1000U
#define RIVER_VOICE_ECHO_CAPTURE_HPF_FC        0U
#define RIVER_VOICE_ECHO_NOISE_GATE_PEAK       1024U
#define RIVER_VOICE_ECHO_PLAYBACK_HW_VOLUME    0.60f
#define RIVER_VOICE_ECHO_PLAYBACK_SW_VOLUME    1.00f
#define RIVER_VOICE_ECHO_PLAYBACK_PCM_GAIN     4U

typedef struct {
    bool running;
    bool stop_requested;
    bool record_started;
    bool track_started;
    bool diag_enabled;
    rtos_task_t task;
    struct AudioRecord *record;
    struct AudioTrack *track;
    uint8_t *delay_buffer;
    uint8_t *capture_buffer;
    uint8_t *mix_buffer;
    uint8_t *playback_buffer;
    uint8_t *track_buffer;
    size_t capture_chunk_bytes;
    size_t mix_chunk_bytes;
    size_t playback_chunk_bytes;
    size_t delay_buffer_bytes;
    size_t read_offset;
    size_t write_offset;
    uint32_t warmup_bytes_remaining;
    uint32_t diag_read_ok;
    uint32_t diag_write_ok;
    uint32_t diag_read_fail;
    uint32_t diag_write_fail;
    uint32_t diag_partial_read;
    uint32_t diag_chunks_until_log;
    uint16_t diag_capture_peak_ch0;
    uint16_t diag_capture_peak_ch1;
    uint16_t diag_playback_peak_ch0;
    uint16_t diag_playback_peak_ch1;
} river_voice_echo_context_t;

static river_voice_echo_context_t g_river_voice_echo;
static bool g_river_voice_echo_diag_enabled;

static const river_voice_board_array_profile_t *river_voice_echo_board_profile(void)
{
    return river_voice_board_array_profile();
}

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

static void river_voice_echo_apply_noise_gate(uint8_t *buffer, size_t bytes, uint16_t peak)
{
    if (buffer == 0) {
        return;
    }

    if (peak < RIVER_VOICE_ECHO_NOISE_GATE_PEAK) {
        memset(buffer, 0, bytes);
    }
}

static void river_voice_echo_reset_diag_counters(void)
{
    g_river_voice_echo.diag_read_ok = 0U;
    g_river_voice_echo.diag_write_ok = 0U;
    g_river_voice_echo.diag_read_fail = 0U;
    g_river_voice_echo.diag_write_fail = 0U;
    g_river_voice_echo.diag_partial_read = 0U;
    g_river_voice_echo.diag_capture_peak_ch0 = 0U;
    g_river_voice_echo.diag_capture_peak_ch1 = 0U;
    g_river_voice_echo.diag_playback_peak_ch0 = 0U;
    g_river_voice_echo.diag_playback_peak_ch1 = 0U;
    g_river_voice_echo.diag_chunks_until_log = RIVER_VOICE_ECHO_DIAG_WINDOW_MS / RIVER_VOICE_ECHO_FRAME_MS;
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

    printf("[river][voice][diag] cap_peak=[%u,%u] play_peak=[%u,%u] read_ok=%lu write_ok=%lu read_fail=%lu write_fail=%lu partial=%lu\n",
           (unsigned int)g_river_voice_echo.diag_capture_peak_ch0,
           (unsigned int)g_river_voice_echo.diag_capture_peak_ch1,
           (unsigned int)g_river_voice_echo.diag_playback_peak_ch0,
           (unsigned int)g_river_voice_echo.diag_playback_peak_ch1,
           (unsigned long)g_river_voice_echo.diag_read_ok,
           (unsigned long)g_river_voice_echo.diag_write_ok,
           (unsigned long)g_river_voice_echo.diag_read_fail,
           (unsigned long)g_river_voice_echo.diag_write_fail,
           (unsigned long)g_river_voice_echo.diag_partial_read);

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

    if (g_river_voice_echo.record != 0) {
        if (g_river_voice_echo.record_started) {
            AudioRecord_Stop(g_river_voice_echo.record);
            g_river_voice_echo.record_started = false;
        }
        AudioRecord_Destroy(g_river_voice_echo.record);
        g_river_voice_echo.record = 0;
    }
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

    if (g_river_voice_echo.mix_buffer != 0) {
        rtos_mem_free(g_river_voice_echo.mix_buffer);
        g_river_voice_echo.mix_buffer = 0;
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
    g_river_voice_echo.mix_chunk_bytes = 0U;
    g_river_voice_echo.playback_chunk_bytes = 0U;
    g_river_voice_echo.delay_buffer_bytes = 0U;
    g_river_voice_echo.read_offset = 0U;
    g_river_voice_echo.write_offset = 0U;
    g_river_voice_echo.warmup_bytes_remaining = 0U;
    river_voice_echo_reset_diag_counters();
}

static river_status_t river_voice_echo_prepare_buffers(void)
{
    const river_voice_board_array_profile_t *profile;
    size_t capture_bytes_per_second;
    size_t mix_bytes_per_second;
    size_t playback_bytes_per_second;

    profile = river_voice_echo_board_profile();
    capture_bytes_per_second = RIVER_VOICE_ECHO_SAMPLE_RATE *
                               profile->capture_channels *
                               RIVER_VOICE_ECHO_BYTES_PER_SAMPLE;
    mix_bytes_per_second = RIVER_VOICE_ECHO_SAMPLE_RATE *
                           RIVER_VOICE_ECHO_BYTES_PER_SAMPLE;
    playback_bytes_per_second = RIVER_VOICE_ECHO_SAMPLE_RATE *
                                RIVER_VOICE_ECHO_PLAYBACK_CHANNELS *
                                RIVER_VOICE_ECHO_BYTES_PER_SAMPLE;
    g_river_voice_echo.capture_chunk_bytes = (capture_bytes_per_second * RIVER_VOICE_ECHO_FRAME_MS) / 1000U;
    g_river_voice_echo.mix_chunk_bytes = (mix_bytes_per_second * RIVER_VOICE_ECHO_FRAME_MS) / 1000U;
    g_river_voice_echo.playback_chunk_bytes = (playback_bytes_per_second * RIVER_VOICE_ECHO_FRAME_MS) / 1000U;
    g_river_voice_echo.delay_buffer_bytes = (mix_bytes_per_second * RIVER_VOICE_ECHO_DELAY_MS) / 1000U;
    g_river_voice_echo.warmup_bytes_remaining = (uint32_t)((mix_bytes_per_second * RIVER_VOICE_ECHO_WARMUP_MS) / 1000U);
    g_river_voice_echo.diag_enabled = g_river_voice_echo_diag_enabled;

    g_river_voice_echo.delay_buffer = (uint8_t *)rtos_mem_zmalloc((uint32_t)g_river_voice_echo.delay_buffer_bytes);
    g_river_voice_echo.capture_buffer = (uint8_t *)rtos_mem_zmalloc((uint32_t)g_river_voice_echo.capture_chunk_bytes);
    g_river_voice_echo.mix_buffer = (uint8_t *)rtos_mem_zmalloc((uint32_t)g_river_voice_echo.mix_chunk_bytes);
    g_river_voice_echo.playback_buffer = (uint8_t *)rtos_mem_zmalloc((uint32_t)g_river_voice_echo.mix_chunk_bytes);
    g_river_voice_echo.track_buffer = (uint8_t *)rtos_mem_zmalloc((uint32_t)g_river_voice_echo.playback_chunk_bytes);

    if (g_river_voice_echo.delay_buffer == 0 ||
        g_river_voice_echo.capture_buffer == 0 ||
        g_river_voice_echo.mix_buffer == 0 ||
        g_river_voice_echo.playback_buffer == 0 ||
        g_river_voice_echo.track_buffer == 0) {
        river_voice_echo_release_buffers();
        return RIVER_ERR_NO_MEMORY;
    }

    return RIVER_OK;
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

static uint16_t river_voice_echo_downmix_to_mono(uint8_t *dst,
                                                 const uint8_t *src,
                                                 size_t capture_bytes,
                                                 uint32_t capture_channels)
{
    const int16_t *src_samples;
    int16_t *dst_samples;
    size_t frame_count;
    size_t index;
    uint16_t max_peak;

    if (dst == 0 || src == 0) {
        return 0U;
    }

    if (capture_channels == 1U) {
        memcpy(dst, src, capture_bytes);
        return river_voice_echo_update_peak(dst,
                                            capture_bytes,
                                            1U,
                                            &g_river_voice_echo.diag_playback_peak_ch0,
                                            &g_river_voice_echo.diag_playback_peak_ch1);
    }

    src_samples = (const int16_t *)src;
    dst_samples = (int16_t *)dst;
    frame_count = capture_bytes / (sizeof(int16_t) * capture_channels);
    max_peak = 0U;

    for (index = 0; index < frame_count; ++index) {
        int32_t sample0;
        int32_t sample1;
        int32_t mixed;
        uint16_t peak;

        sample0 = src_samples[index * capture_channels];
        sample1 = src_samples[(index * capture_channels) + 1U];
        mixed = (sample0 + sample1) / 2;
        dst_samples[index] = (int16_t)mixed;
        peak = river_voice_echo_abs16(mixed);
        if (peak > max_peak) {
            max_peak = peak;
        }
    }

    return max_peak;
}

static river_status_t river_voice_echo_open_audio(void)
{
    const river_voice_board_array_profile_t *profile;
    AudioRecordConfig record_config;
    AudioTrackConfig track_config;
    size_t track_buffer_bytes;

    profile = river_voice_echo_board_profile();
    AudioService_Init();
    AudioControl_SetPlaybackDevice(DEVICE_OUT_SPEAKER);
    AudioControl_SetPlaybackMute(false);
    AudioControl_SetAmplifierMute(false);
    AudioControl_SetHardwareVolume(RIVER_VOICE_ECHO_PLAYBACK_HW_VOLUME, RIVER_VOICE_ECHO_PLAYBACK_HW_VOLUME);

    AudioControl_SetMicUsage(AUDIO_CAPTURE_USAGE_AMIC);
    AudioControl_SetChannelMicCategory(0, profile->primary_mic);
    AudioControl_SetMicBstGain(profile->primary_mic, profile->primary_mic_gain);
    if (profile->capture_channels > 1U) {
        AudioControl_SetChannelMicCategory(1, profile->secondary_mic);
        AudioControl_SetMicBstGain(profile->secondary_mic, profile->secondary_mic_gain);
    }
    if (profile->aux_mic_reserved) {
        AudioControl_SetChannelMicCategory(2, profile->aux_mic);
        AudioControl_SetMicBstGain(profile->aux_mic, profile->aux_mic_gain);
    }
    AudioControl_SetCaptureVolume(profile->capture_channels, RIVER_VOICE_ECHO_CAPTURE_VOLUME);
    AudioControl_SetCaptureHpfFc(0, RIVER_VOICE_ECHO_CAPTURE_HPF_FC);

    g_river_voice_echo.record = AudioRecord_Create();
    if (g_river_voice_echo.record == 0) {
        printf("[river][voice] create AudioRecord failed\n");
        return RIVER_ERR_UNSUPPORTED;
    }

    record_config.sample_rate = RIVER_VOICE_ECHO_SAMPLE_RATE;
    record_config.channel_count = profile->capture_channels;
    record_config.format = AUDIO_FORMAT_PCM_16_BIT;
    record_config.device = DEVICE_IN_MIC;
    record_config.buffer_bytes = (uint32_t)g_river_voice_echo.capture_chunk_bytes;
    if (AudioRecord_Init(g_river_voice_echo.record, &record_config, AUDIO_INPUT_FLAG_NONE) != 0) {
        printf("[river][voice] AudioRecord_Init failed\n");
        return RIVER_ERR_UNSUPPORTED;
    }

    AudioRecord_SetParameters(g_river_voice_echo.record, "cap_mode=no_afe_pure_data");

    g_river_voice_echo.track = AudioTrack_Create();
    if (g_river_voice_echo.track == 0) {
        printf("[river][voice] create AudioTrack failed\n");
        return RIVER_ERR_UNSUPPORTED;
    }

    track_buffer_bytes = AudioTrack_GetMinBufferBytes(g_river_voice_echo.track,
                                                      AUDIO_CATEGORY_MEDIA,
                                                      RIVER_VOICE_ECHO_SAMPLE_RATE,
                                                      AUDIO_FORMAT_PCM_16_BIT,
                                                      RIVER_VOICE_ECHO_PLAYBACK_CHANNELS);
    if (track_buffer_bytes < g_river_voice_echo.playback_chunk_bytes) {
        track_buffer_bytes = g_river_voice_echo.playback_chunk_bytes;
    }
    track_buffer_bytes *= 4U;

    track_config.category_type = AUDIO_CATEGORY_MEDIA;
    track_config.sample_rate = RIVER_VOICE_ECHO_SAMPLE_RATE;
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

    if (AudioRecord_Start(g_river_voice_echo.record) != 0) {
        printf("[river][voice] AudioRecord_Start failed\n");
        return RIVER_ERR_UNSUPPORTED;
    }
    g_river_voice_echo.record_started = true;

    if (AudioTrack_Start(g_river_voice_echo.track) != 0) {
        printf("[river][voice] AudioTrack_Start failed\n");
        return RIVER_ERR_UNSUPPORTED;
    }
    g_river_voice_echo.track_started = true;

    printf("[river][voice] audio echo config: %lu Hz capture dual-mic -> %lu Hz playback dual-mono, %lu ms delay, %s+%s mix -> speaker\n",
           (unsigned long)RIVER_VOICE_ECHO_SAMPLE_RATE,
           (unsigned long)RIVER_VOICE_ECHO_SAMPLE_RATE,
           (unsigned long)RIVER_VOICE_ECHO_DELAY_MS,
           river_voice_board_mic_name(profile->primary_mic),
           river_voice_board_mic_name(profile->secondary_mic));
    printf("[river][voice] audio echo array: %s spacing=%lumm aivoice=%s aux=%s%s\n",
           profile->geometry_name,
           (unsigned long)profile->mic_spacing_mm,
           profile->aivoice_geometry_name,
           river_voice_board_mic_name(profile->aux_mic),
           profile->aux_mic_reserved ? "(reserved)" : "");
    printf("[river][voice] audio echo gain: hw=%.2f sw=%.2f pcm=x%lu cap=0x%02lx gate=%lu micbst=[%s,%s]\n",
           (double)RIVER_VOICE_ECHO_PLAYBACK_HW_VOLUME,
           (double)RIVER_VOICE_ECHO_PLAYBACK_SW_VOLUME,
           (unsigned long)RIVER_VOICE_ECHO_PLAYBACK_PCM_GAIN,
           (unsigned long)RIVER_VOICE_ECHO_CAPTURE_VOLUME,
           (unsigned long)RIVER_VOICE_ECHO_NOISE_GATE_PEAK,
           river_voice_board_mic_gain_name(profile->primary_mic_gain),
           river_voice_board_mic_gain_name(profile->secondary_mic_gain));
    return RIVER_OK;
}

static void river_voice_echo_task(void *param)
{
    const river_voice_board_array_profile_t *profile;

    (void)param;
    profile = river_voice_echo_board_profile();

    while (!g_river_voice_echo.stop_requested) {
        int32_t bytes_read;
        uint16_t capture_peak;

        memset(g_river_voice_echo.capture_buffer, 0, g_river_voice_echo.capture_chunk_bytes);
        bytes_read = AudioRecord_Read(g_river_voice_echo.record,
                                      g_river_voice_echo.capture_buffer,
                                      g_river_voice_echo.capture_chunk_bytes,
                                      true);
        if (bytes_read < 0) {
            g_river_voice_echo.diag_read_fail++;
            river_voice_echo_log_diagnostics_if_needed();
            printf("[river][voice] AudioRecord_Read failed: %ld\n", (long)bytes_read);
            rtos_time_delay_ms(RIVER_VOICE_ECHO_FRAME_MS);
            continue;
        }
        g_river_voice_echo.diag_read_ok++;
        if ((size_t)bytes_read < g_river_voice_echo.capture_chunk_bytes) {
            memset(g_river_voice_echo.capture_buffer + bytes_read, 0, g_river_voice_echo.capture_chunk_bytes - (size_t)bytes_read);
            g_river_voice_echo.diag_partial_read++;
        }

        capture_peak = river_voice_echo_update_peak(g_river_voice_echo.capture_buffer,
                                                    g_river_voice_echo.capture_chunk_bytes,
                                                    profile->capture_channels,
                                                    &g_river_voice_echo.diag_capture_peak_ch0,
                                                    &g_river_voice_echo.diag_capture_peak_ch1);
        river_voice_echo_downmix_to_mono(g_river_voice_echo.mix_buffer,
                                         g_river_voice_echo.capture_buffer,
                                         g_river_voice_echo.capture_chunk_bytes,
                                         profile->capture_channels);
        river_voice_echo_apply_warmup(g_river_voice_echo.mix_buffer, g_river_voice_echo.mix_chunk_bytes);
        river_voice_echo_apply_noise_gate(g_river_voice_echo.mix_buffer,
                                          g_river_voice_echo.mix_chunk_bytes,
                                          capture_peak);
        river_voice_echo_ring_read(g_river_voice_echo.playback_buffer, g_river_voice_echo.mix_chunk_bytes);
        river_voice_echo_ring_write(g_river_voice_echo.mix_buffer, g_river_voice_echo.mix_chunk_bytes);
        river_voice_echo_expand_mono_to_stereo(g_river_voice_echo.track_buffer,
                                               g_river_voice_echo.playback_buffer,
                                               g_river_voice_echo.mix_chunk_bytes);
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

    status = river_voice_echo_prepare_buffers();
    if (status != RIVER_OK) {
        printf("[river][voice] allocate echo buffers failed\n");
        return status;
    }

    status = river_voice_echo_open_audio();
    if (status != RIVER_OK) {
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
    const river_voice_board_array_profile_t *profile;

    profile = river_voice_echo_board_profile();
    printf("[river] audio_echo=%s\n", river_voice_echo_status_name());
    printf("[river] audio_echo_diag=%s\n", river_voice_echo_diag_enabled() ? "on" : "off");
    if (river_voice_echo_is_running()) {
        printf("[river] audio_echo_profile=cap:%luHz/%luch(%s+%s) play:%luHz/%luch delay:%lums\n",
               (unsigned long)RIVER_VOICE_ECHO_SAMPLE_RATE,
               (unsigned long)profile->capture_channels,
               river_voice_board_mic_name(profile->primary_mic),
               river_voice_board_mic_name(profile->secondary_mic),
               (unsigned long)RIVER_VOICE_ECHO_SAMPLE_RATE,
               (unsigned long)RIVER_VOICE_ECHO_PLAYBACK_CHANNELS,
               (unsigned long)RIVER_VOICE_ECHO_DELAY_MS);
    }
}
