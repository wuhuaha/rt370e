/* Orvibo branch monitor commands. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ameba_soc.h"
#include "basic_types.h"
#include "platform_stdlib.h"

#include "river/river_orvibo_app.h"
#include "river/river_orvibo_audio_service.h"
#include "river/river_orvibo_mcp_volume.h"
#include "river/river_orvibo_protocol.h"
#include "river/river_playback_service.h"
#include "river/river_runtime_stats.h"
#include "river/river_voice.h"
#include "river/river_voice_kws.h"

#ifdef CONFIG_RIVER_DIAG_CMD_EN

static void river_diag_help(void)
{
    printf("\triver status\n");
    printf("\triver orvibo <status|connect|listen <start|stop>|abort|protocol <1|2|3>|volume <0-100>>\n");
    printf("\triver audio <status>\n");
    printf("\triver playback <status|stop|interrupt|flush|duck <gain>|unduck>\n");
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
        printf("[river][diag] usage: river orvibo <status|connect|listen <start|stop>|abort|protocol <1|2|3>|volume <0-100>>\n");
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

    printf("[river][diag] usage: river orvibo <status|connect|listen <start|stop>|abort|protocol <1|2|3>|volume <0-100>>\n");
    return 0;
}

static u32 river_diag_playback_cmd(u16 argc, u8 *argv[])
{
    if (argc < 2) {
        printf("[river][diag] usage: river playback <status|stop|interrupt|flush|duck <gain>|unduck>\n");
        return 0;
    }
    if (strcmp((const char *)argv[1], "status") == 0) {
        river_playback_service_dump_status();
        return 0;
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
    printf("[river][diag] usage: river playback <status|stop|interrupt|flush|duck <gain>|unduck>\n");
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
