/* Orvibo branch monitor commands. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ameba_soc.h"
#include "basic_types.h"
#include "os_wrapper.h"
#include "platform_stdlib.h"

#include "audio/audio_control.h"
#include "audio/audio_service.h"

#include "river/river_orvibo_app.h"
#include "river/river_orvibo_audio_service.h"
#include "river/river_orvibo_mcp_volume.h"
#include "river/river_orvibo_protocol.h"
#include "river/river_orvibo_ui.h"
#include "river/river_playback_service.h"
#include "river/river_runtime_stats.h"
#include "river/river_voice.h"
#include "river/river_voice_kws.h"

#ifdef CONFIG_RIVER_DIAG_CMD_EN

#define RIVER_DIAG_PLAYBACK_TONE_SAMPLE_RATE     16000U
#define RIVER_DIAG_PLAYBACK_TONE_FRAME_MS        20U
#define RIVER_DIAG_PLAYBACK_TONE_CHANNELS        2U
#define RIVER_DIAG_PLAYBACK_TONE_BITS_PER_SAMPLE 16U
#define RIVER_DIAG_PLAYBACK_TONE_FRAME_SAMPLES \
    ((RIVER_DIAG_PLAYBACK_TONE_SAMPLE_RATE * RIVER_DIAG_PLAYBACK_TONE_FRAME_MS) / 1000U)
#define RIVER_DIAG_PLAYBACK_TONE_PCM_SAMPLES \
    (RIVER_DIAG_PLAYBACK_TONE_FRAME_SAMPLES * RIVER_DIAG_PLAYBACK_TONE_CHANNELS)
#define RIVER_DIAG_PLAYBACK_TONE_FRAME_BYTES \
    (RIVER_DIAG_PLAYBACK_TONE_PCM_SAMPLES * sizeof(int16_t))
#define RIVER_DIAG_PLAYBACK_TONE_DEFAULT_FREQ_HZ 1000U
#define RIVER_DIAG_PLAYBACK_TONE_DEFAULT_MS      1000U
#define RIVER_DIAG_PLAYBACK_TONE_DEFAULT_LEVEL   25U
#define RIVER_DIAG_PLAYBACK_TONE_MIN_FREQ_HZ     100U
#define RIVER_DIAG_PLAYBACK_TONE_MAX_FREQ_HZ     4000U
#define RIVER_DIAG_PLAYBACK_TONE_MIN_MS          100U
#define RIVER_DIAG_PLAYBACK_TONE_MAX_MS          10000U
#define RIVER_DIAG_PLAYBACK_TONE_MIN_LEVEL       1U
#define RIVER_DIAG_PLAYBACK_TONE_MAX_LEVEL       80U
#define RIVER_DIAG_PLAYBACK_TONE_BUFFER_FRAMES   4U
#define RIVER_DIAG_PLAYBACK_TONE_DRAIN_EXTRA_MS  1500U
#define RIVER_DIAG_PLAYBACK_TONE_HW_VOLUME       1.00f

static int16_t g_river_diag_playback_tone_frame[RIVER_DIAG_PLAYBACK_TONE_PCM_SAMPLES];

static void river_diag_help(void)
{
    printf("\triver status\n");
    printf("\triver orvibo <status|connect|refresh|listen <start|stop>|abort|protocol <1|2|3>|volume <0-100>>\n");
    printf("\triver ui <status|touch scan|text <asr|tts|emoji> <text>>\n");
    printf("\triver audio <status>\n");
    printf("\triver playback <status|tone [freq_hz] [duration_ms] [level_pct]|stop|interrupt|flush|duck <gain>|unduck>\n");
    printf("\triver kws <status|debug local <on|off|status>|dump <next|off|clear|status|meta|chunk <feat_f32|input_raw|output_raw> <index>>|align <run|status>>\n");
}

static bool river_diag_parse_u32_arg(const char *text, uint32_t *value_out)
{
    char *end = NULL;
    unsigned long parsed;

    if (text == NULL || value_out == NULL || text[0] == '\0') {
        return false;
    }
    parsed = strtoul(text, &end, 10);
    if (end == text || end == NULL || *end != '\0') {
        return false;
    }
    *value_out = (uint32_t)parsed;
    return true;
}

static int16_t river_diag_playback_triangle_sample(uint32_t phase, uint32_t amplitude)
{
    const uint32_t half_cycle = RIVER_DIAG_PLAYBACK_TONE_SAMPLE_RATE / 2U;
    int32_t sample;

    if (phase < half_cycle) {
        sample = ((int32_t)(2U * amplitude * phase) / (int32_t)half_cycle) -
                 (int32_t)amplitude;
    } else {
        sample = (int32_t)amplitude -
                 ((int32_t)(2U * amplitude * (phase - half_cycle)) /
                  (int32_t)half_cycle);
    }

    if (sample > 32767) {
        sample = 32767;
    } else if (sample < -32768) {
        sample = -32768;
    }
    return (int16_t)sample;
}

static void river_diag_playback_fill_tone_frame(uint32_t freq_hz,
                                                uint32_t level_pct,
                                                uint32_t *phase)
{
    uint32_t frame;
    uint32_t sample_index = 0U;
    uint32_t amplitude = (32767U * level_pct) / 100U;

    for (frame = 0U; frame < RIVER_DIAG_PLAYBACK_TONE_FRAME_SAMPLES; ++frame) {
        int16_t sample = river_diag_playback_triangle_sample(*phase, amplitude);

        g_river_diag_playback_tone_frame[sample_index++] = sample;
        g_river_diag_playback_tone_frame[sample_index++] = sample;
        *phase += freq_hz;
        while (*phase >= RIVER_DIAG_PLAYBACK_TONE_SAMPLE_RATE) {
            *phase -= RIVER_DIAG_PLAYBACK_TONE_SAMPLE_RATE;
        }
    }
}

static void river_diag_playback_print_tone_usage(void)
{
    printf("[river][diag] usage: river playback tone [freq_hz] [duration_ms] [level_pct]\n");
    printf("[river][diag] ranges: freq=%lu-%luHz duration=%lu-%lums level=%lu-%lu%%\n",
           (unsigned long)RIVER_DIAG_PLAYBACK_TONE_MIN_FREQ_HZ,
           (unsigned long)RIVER_DIAG_PLAYBACK_TONE_MAX_FREQ_HZ,
           (unsigned long)RIVER_DIAG_PLAYBACK_TONE_MIN_MS,
           (unsigned long)RIVER_DIAG_PLAYBACK_TONE_MAX_MS,
           (unsigned long)RIVER_DIAG_PLAYBACK_TONE_MIN_LEVEL,
           (unsigned long)RIVER_DIAG_PLAYBACK_TONE_MAX_LEVEL);
}

static u32 river_diag_playback_tone_cmd(u16 argc, u8 *argv[])
{
    river_playback_stream_config_t config;
    river_status_t status;
    uint32_t freq_hz = RIVER_DIAG_PLAYBACK_TONE_DEFAULT_FREQ_HZ;
    uint32_t duration_ms = RIVER_DIAG_PLAYBACK_TONE_DEFAULT_MS;
    uint32_t level_pct = RIVER_DIAG_PLAYBACK_TONE_DEFAULT_LEVEL;
    uint32_t phase = 0U;
    uint32_t frame_count;
    uint32_t frame;
    float saved_left = 0.0f;
    float saved_right = 0.0f;
    bool volume_saved = false;

    if (argc > 5) {
        river_diag_playback_print_tone_usage();
        return 0;
    }
    if (argc >= 3 && !river_diag_parse_u32_arg((const char *)argv[2], &freq_hz)) {
        river_diag_playback_print_tone_usage();
        return 0;
    }
    if (argc >= 4 && !river_diag_parse_u32_arg((const char *)argv[3], &duration_ms)) {
        river_diag_playback_print_tone_usage();
        return 0;
    }
    if (argc >= 5 && !river_diag_parse_u32_arg((const char *)argv[4], &level_pct)) {
        river_diag_playback_print_tone_usage();
        return 0;
    }
    if (freq_hz < RIVER_DIAG_PLAYBACK_TONE_MIN_FREQ_HZ ||
        freq_hz > RIVER_DIAG_PLAYBACK_TONE_MAX_FREQ_HZ ||
        duration_ms < RIVER_DIAG_PLAYBACK_TONE_MIN_MS ||
        duration_ms > RIVER_DIAG_PLAYBACK_TONE_MAX_MS ||
        level_pct < RIVER_DIAG_PLAYBACK_TONE_MIN_LEVEL ||
        level_pct > RIVER_DIAG_PLAYBACK_TONE_MAX_LEVEL) {
        river_diag_playback_print_tone_usage();
        return 0;
    }
    if (river_playback_service_active()) {
        printf("[river][diag] playback tone skipped: playback service busy\n");
        river_playback_service_dump_status();
        return 0;
    }

    AudioService_Init();
    AudioControl_SetPlaybackDevice(DEVICE_OUT_SPEAKER);
    AudioControl_SetPlaybackMute(false);
    AudioControl_SetAmplifierMute(false);
    if (AudioControl_GetHardwareVolume(&saved_left, &saved_right) == 0) {
        volume_saved = true;
    }
    AudioControl_SetHardwareVolume(RIVER_DIAG_PLAYBACK_TONE_HW_VOLUME,
                                   RIVER_DIAG_PLAYBACK_TONE_HW_VOLUME);

    memset(&config, 0, sizeof(config));
    config.stream_name = "diag_tone";
    config.priority = RIVER_PLAYBACK_PRIO_DEBUG;
    config.sample_rate = RIVER_DIAG_PLAYBACK_TONE_SAMPLE_RATE;
    config.frame_ms = RIVER_DIAG_PLAYBACK_TONE_FRAME_MS;
    config.playback_channels = RIVER_DIAG_PLAYBACK_TONE_CHANNELS;
    config.bits_per_sample = RIVER_DIAG_PLAYBACK_TONE_BITS_PER_SAMPLE;
    config.playback_frame_bytes = RIVER_DIAG_PLAYBACK_TONE_FRAME_BYTES;
    config.buffer_frame_count = RIVER_DIAG_PLAYBACK_TONE_BUFFER_FRAMES;
    config.disable_track_reuse = true;
    config.defer_start_until_prefilled = false;
    config.volume_left = 1.0f;
    config.volume_right = 1.0f;
    config.reference_export = false;

    printf("[river][diag] playback tone start: route=speaker/LINEOUT amp=PB25/MUTE freq=%luHz duration=%lums level=%lu%% hw=%.2f\n",
           (unsigned long)freq_hz,
           (unsigned long)duration_ms,
           (unsigned long)level_pct,
           (double)RIVER_DIAG_PLAYBACK_TONE_HW_VOLUME);
    status = river_playback_service_start_stream(&config);
    if (status != RIVER_OK) {
        printf("[river][diag] playback tone start failed status=%d\n", (int)status);
        if (volume_saved) {
            AudioControl_SetHardwareVolume(saved_left, saved_right);
        }
        return 0;
    }

    frame_count = (duration_ms + RIVER_DIAG_PLAYBACK_TONE_FRAME_MS - 1U) /
                  RIVER_DIAG_PLAYBACK_TONE_FRAME_MS;
    for (frame = 0U; frame < frame_count; ++frame) {
        river_diag_playback_fill_tone_frame(freq_hz, level_pct, &phase);
        status = river_playback_service_write((const uint8_t *)g_river_diag_playback_tone_frame,
                                             RIVER_DIAG_PLAYBACK_TONE_FRAME_BYTES,
                                             NULL,
                                             0U,
                                             true);
        if (status != RIVER_OK) {
            printf("[river][diag] playback tone write failed frame=%lu/%lu status=%d\n",
                   (unsigned long)(frame + 1U),
                   (unsigned long)frame_count,
                   (int)status);
            break;
        }
    }

    if (status == RIVER_OK) {
        status = river_playback_service_wait_idle_ex(
            duration_ms + RIVER_DIAG_PLAYBACK_TONE_DRAIN_EXTRA_MS,
            RIVER_DIAG_PLAYBACK_TONE_FRAME_MS,
            "diag_tone_drain");
    }
    (void)river_playback_service_stop_stream_ex(status == RIVER_OK ?
                                                   "diag_tone_done" :
                                                   "diag_tone_abort");
    if (volume_saved) {
        AudioControl_SetHardwareVolume(saved_left, saved_right);
    }

    if (status == RIVER_OK) {
        printf("[river][diag] playback tone done\n");
    } else {
        printf("[river][diag] playback tone ended with status=%d\n", (int)status);
    }
    return 0;
}

static u32 river_diag_kws_cmd(u16 argc, u8 *argv[])
{
    if (argc < 2) {
        printf("[river][diag] usage: river kws <status|debug local <on|off|status>|dump <next|off|clear|status|meta|chunk <feat_f32|input_raw|output_raw> <index>>|align <run|status>>\n");
        return 0;
    }

    if (strcmp((const char *)argv[1], "status") == 0) {
        river_voice_kws_dump_status();
        return 0;
    }

    if (strcmp((const char *)argv[1], "debug") == 0) {
        if (argc < 4 || strcmp((const char *)argv[2], "local") != 0) {
            printf("[river][diag] usage: river kws debug local <on|off|status>\n");
            return 0;
        }
        if (strcmp((const char *)argv[3], "on") == 0) {
            river_voice_kws_set_local_debug_mode(true);
        } else if (strcmp((const char *)argv[3], "off") == 0) {
            river_voice_kws_set_local_debug_mode(false);
        } else if (strcmp((const char *)argv[3], "status") != 0) {
            printf("[river][diag] usage: river kws debug local <on|off|status>\n");
            return 0;
        }
        river_voice_kws_dump_status();
        return 0;
    }

    if (strcmp((const char *)argv[1], "dump") == 0) {
        river_voice_kws_tensor_dump_buffer_t dump_buffer;
        uint32_t chunk_index;

        if (argc < 3) {
            printf("[river][diag] usage: river kws dump <next|off|clear|status|meta|chunk <feat_f32|input_raw|output_raw> <index>>\n");
            return 0;
        }
        if (strcmp((const char *)argv[2], "next") == 0) {
            if (river_voice_kws_request_tensor_dump_next() != RIVER_OK) {
                printf("[river][diag] kws dump arm failed\n");
            }
            return 0;
        }
        if (strcmp((const char *)argv[2], "off") == 0) {
            river_voice_kws_cancel_tensor_dump();
            return 0;
        }
        if (strcmp((const char *)argv[2], "clear") == 0) {
            river_voice_kws_clear_tensor_dump();
            return 0;
        }
        if (strcmp((const char *)argv[2], "status") == 0) {
            river_voice_kws_dump_status();
            return 0;
        }
        if (strcmp((const char *)argv[2], "meta") == 0) {
            river_voice_kws_dump_tensor_meta();
            return 0;
        }
        if (strcmp((const char *)argv[2], "chunk") == 0) {
            if (argc < 5) {
                printf("[river][diag] usage: river kws dump chunk <feat_f32|input_raw|output_raw> <index>\n");
                return 0;
            }
            if (strcmp((const char *)argv[3], "feat_f32") == 0) {
                dump_buffer = RIVER_VOICE_KWS_TENSOR_DUMP_FEATURE_F32;
            } else if (strcmp((const char *)argv[3], "input_raw") == 0) {
                dump_buffer = RIVER_VOICE_KWS_TENSOR_DUMP_INPUT_RAW;
            } else if (strcmp((const char *)argv[3], "output_raw") == 0) {
                dump_buffer = RIVER_VOICE_KWS_TENSOR_DUMP_OUTPUT_RAW;
            } else {
                printf("[river][diag] dump label must be feat_f32, input_raw, or output_raw\n");
                return 0;
            }
            if (!river_diag_parse_u32_arg((const char *)argv[4], &chunk_index) ||
                chunk_index == 0U) {
                printf("[river][diag] dump chunk index must be a positive integer\n");
                return 0;
            }
            if (river_voice_kws_dump_tensor_chunk(dump_buffer, chunk_index) != RIVER_OK) {
                printf("[river][diag] kws dump chunk failed\n");
            }
            return 0;
        }
        printf("[river][diag] usage: river kws dump <next|off|clear|status|meta|chunk <feat_f32|input_raw|output_raw> <index>>\n");
        return 0;
    }

    if (strcmp((const char *)argv[1], "align") == 0) {
        if (argc < 3) {
            printf("[river][diag] usage: river kws align <run|status>\n");
            return 0;
        }
        if (strcmp((const char *)argv[2], "status") == 0) {
            river_voice_kws_dump_alignment_status();
            return 0;
        }
        if (strcmp((const char *)argv[2], "run") == 0) {
            river_status_t status = river_voice_kws_run_alignment_sample(true);
            if (status != RIVER_OK) {
                printf("[river][diag] kws align run failed status=%d\n", (int)status);
            }
            return 0;
        }
        printf("[river][diag] usage: river kws align <run|status>\n");
        return 0;
    }

    printf("[river][diag] usage: river kws <status|debug local <on|off|status>|dump <next|off|clear|status|meta|chunk <feat_f32|input_raw|output_raw> <index>>|align <run|status>>\n");
    return 0;
}

static u32 river_diag_orvibo_cmd(u16 argc, u8 *argv[])
{
    if (argc < 2) {
        printf("[river][diag] usage: river orvibo <status|connect|refresh|listen <start|stop>|abort|protocol <1|2|3>|volume <0-100>>\n");
        return 0;
    }

    if (strcmp((const char *)argv[1], "status") == 0) {
        river_orvibo_app_print_status();
        return 0;
    }
    if (strcmp((const char *)argv[1], "connect") == 0) {
        river_orvibo_app_request_connect();
        return 0;
    }
    if (strcmp((const char *)argv[1], "refresh") == 0) {
        river_orvibo_app_request_access_refresh();
        return 0;
    }
    if (strcmp((const char *)argv[1], "listen") == 0) {
        if (argc < 3) {
            printf("[river][diag] usage: river orvibo listen <start|stop>\n");
            return 0;
        }
        if (strcmp((const char *)argv[2], "start") == 0) {
            river_orvibo_app_request_listen_start();
        } else if (strcmp((const char *)argv[2], "stop") == 0) {
            river_orvibo_app_request_listen_stop();
        } else {
            printf("[river][diag] usage: river orvibo listen <start|stop>\n");
        }
        return 0;
    }
    if (strcmp((const char *)argv[1], "abort") == 0) {
        river_orvibo_app_request_abort();
        return 0;
    }
    if (strcmp((const char *)argv[1], "protocol") == 0) {
        river_orvibo_protocol_config_t config;
        uint32_t version;

        if (argc < 3 || !river_diag_parse_u32_arg((const char *)argv[2], &version) ||
            (version != 1U && version != 2U && version != 3U)) {
            printf("[river][diag] usage: river orvibo protocol <1|2|3>\n");
            return 0;
        }
        if (river_orvibo_protocol_get_config(&config) == RIVER_OK) {
            config.protocol_version = (uint16_t)version;
            (void)river_orvibo_protocol_set_config(&config);
        }
        river_orvibo_protocol_dump_status();
        return 0;
    }
    if (strcmp((const char *)argv[1], "volume") == 0) {
        uint32_t volume;

        if (argc < 3 || !river_diag_parse_u32_arg((const char *)argv[2], &volume) ||
            volume > 100U) {
            printf("[river][diag] usage: river orvibo volume <0-100>\n");
            return 0;
        }
        river_orvibo_mcp_volume_set((uint8_t)volume);
        river_orvibo_mcp_volume_dump_status();
        return 0;
    }

    printf("[river][diag] usage: river orvibo <status|connect|refresh|listen <start|stop>|abort|protocol <1|2|3>|volume <0-100>>\n");
    return 0;
}

static u32 river_diag_playback_cmd(u16 argc, u8 *argv[])
{
    if (argc < 2) {
        printf("[river][diag] usage: river playback <status|tone [freq_hz] [duration_ms] [level_pct]|stop|interrupt|flush|duck <gain>|unduck>\n");
        return 0;
    }
    if (strcmp((const char *)argv[1], "status") == 0) {
        river_playback_service_dump_status();
        return 0;
    }
    if (strcmp((const char *)argv[1], "tone") == 0) {
        return river_diag_playback_tone_cmd(argc, argv);
    }
    if (strcmp((const char *)argv[1], "stop") == 0) {
        (void)river_playback_service_stop_stream();
        return 0;
    }
    if (strcmp((const char *)argv[1], "interrupt") == 0) {
        (void)river_playback_service_interrupt_stream();
        return 0;
    }
    if (strcmp((const char *)argv[1], "flush") == 0) {
        (void)river_playback_service_flush_stream();
        return 0;
    }
    if (strcmp((const char *)argv[1], "duck") == 0) {
        if (argc < 3) {
            printf("[river][diag] usage: river playback duck <gain>\n");
            return 0;
        }
        (void)river_playback_service_set_ducking(true, (float)atof((const char *)argv[2]));
        return 0;
    }
    if (strcmp((const char *)argv[1], "unduck") == 0) {
        (void)river_playback_service_set_ducking(false, 1.0f);
        return 0;
    }
    printf("[river][diag] usage: river playback <status|tone [freq_hz] [duration_ms] [level_pct]|stop|interrupt|flush|duck <gain>|unduck>\n");
    return 0;
}

static u32 river_diag_ui_cmd(u16 argc, u8 *argv[])
{
    if (argc < 2) {
        printf("[river][diag] usage: river ui <status|touch scan|text <asr|tts|emoji> <text>>\n");
        return 0;
    }
    if (strcmp((const char *)argv[1], "status") == 0) {
        river_orvibo_ui_dump_status();
        return 0;
    }
    if (strcmp((const char *)argv[1], "touch") == 0) {
        if (argc >= 3 && strcmp((const char *)argv[2], "scan") == 0) {
            river_orvibo_ui_touch_scan();
            return 0;
        }
        printf("[river][diag] usage: river ui touch scan\n");
        return 0;
    }
    if (strcmp((const char *)argv[1], "text") == 0) {
        if (argc < 4) {
            printf("[river][diag] usage: river ui text <asr|tts|emoji> <text>\n");
            return 0;
        }
        if (strcmp((const char *)argv[2], "asr") == 0) {
            (void)river_orvibo_ui_set_asr_text((const char *)argv[3]);
        } else if (strcmp((const char *)argv[2], "tts") == 0) {
            (void)river_orvibo_ui_set_tts_text((const char *)argv[3]);
        } else if (strcmp((const char *)argv[2], "emoji") == 0) {
            (void)river_orvibo_ui_set_emoji((const char *)argv[3]);
        } else {
            printf("[river][diag] usage: river ui text <asr|tts|emoji> <text>\n");
        }
        return 0;
    }
    printf("[river][diag] usage: river ui <status|touch scan|text <asr|tts|emoji> <text>>\n");
    return 0;
}

static u32 river_diag_cmd(u16 argc, u8 *argv[])
{
    if (argc == 0) {
        river_diag_help();
        return 0;
    }
    if (strcmp((const char *)argv[0], "status") == 0) {
        river_orvibo_app_print_status();
        river_runtime_stats_snapshot("diag_status");
        return 0;
    }
    if (strcmp((const char *)argv[0], "orvibo") == 0) {
        return river_diag_orvibo_cmd(argc, argv);
    }
    if (strcmp((const char *)argv[0], "ui") == 0) {
        return river_diag_ui_cmd(argc, argv);
    }
    if (strcmp((const char *)argv[0], "audio") == 0) {
        if (argc >= 2 && strcmp((const char *)argv[1], "status") == 0) {
            river_orvibo_audio_service_dump_status();
            return 0;
        }
        printf("[river][diag] usage: river audio <status>\n");
        return 0;
    }
    if (strcmp((const char *)argv[0], "playback") == 0) {
        return river_diag_playback_cmd(argc, argv);
    }
    if (strcmp((const char *)argv[0], "kws") == 0) {
        return river_diag_kws_cmd(argc, argv);
    }
    river_diag_help();
    return 0;
}

CMD_TABLE_DATA_SECTION
const COMMAND_TABLE river_cmd_table[] = {
    {"river", river_diag_cmd},
};
#endif
