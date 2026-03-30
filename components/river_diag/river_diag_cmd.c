/* 串口诊断命令入口：集中暴露状态查询、音频测试和云端调试命令。 */
#include <stdio.h>
#include <string.h>

#include "ameba_soc.h"
#include "platform_stdlib.h"
#include "basic_types.h"

#include "river/river_app.h"
#include "river/river_cloud.h"
#include "river/river_interaction_diag.h"
#include "river/river_online_control.h"
#include "river/river_playback_service.h"
#include "river/river_runtime_stats.h"
#include "river/river_voice.h"

#ifdef CONFIG_RIVER_DIAG_CMD_EN
#if defined(CONFIG_RIVER_CLOUD_TEXT_DEBUG_EN)
#define RIVER_CLOUD_TEXT_DEBUG_ENABLED 1
#else
#define RIVER_CLOUD_TEXT_DEBUG_ENABLED 0
#endif

#if defined(CONFIG_RIVER_INTERACTION_DIAG_EN)
#define RIVER_INTERACTION_DIAG_ENABLED 1
#else
#define RIVER_INTERACTION_DIAG_ENABLED 0
#endif

#define RIVER_ECHO_TEXT_MAX 128
#define RIVER_XIAOZHI_TEXT_MAX 384

static void river_diag_help(void)
{
    printf("\triver status\n");
#if RIVER_CLOUD_TEXT_DEBUG_ENABLED
    printf("\triver echo <text>\n");
    printf("\triver tts <text>\n");
#endif
#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    printf("\triver xiaozhi <status|set|ota|bootstrap|disable|protocol|mcp|connect|disconnect|listen|abort>\n");
#endif
#if RIVER_INTERACTION_DIAG_ENABLED
    printf("\triver interaction <status|intents|tts_test>\n");
    printf("\triver interaction asr <text>\n");
    printf("\triver interaction device <light|fan|curtain|socket> <on|off>\n");
    printf("\triver interaction invoke <intent_name>\n");
#endif
    printf("\triver playback <status|stop|interrupt|flush|duck <gain>|unduck>\n");
    printf("\triver audio <start|stop|status>\n");
    printf("\triver audio echo <start|stop|status>\n");
    printf("\triver audio probe <start|stop|status>\n");
    printf("\triver audio diag <on|off|status>\n");
    printf("\triver device <light|fan|curtain|socket> <on|off|toggle>\n");
}

static void river_diag_join_args(u16 argc, u8 *argv[], u16 start, char *out_text, unsigned int out_size)
{
    u16 index;
    int offset;

    if (out_text == 0 || out_size == 0) {
        return;
    }

    out_text[0] = '\0';
    offset = 0;

    /* 把命令行剩余参数重新拼成完整文本，便于透传到云端或调试模块。 */
    for (index = start; index < argc; ++index) {
        int written;

        if ((unsigned int)offset >= (out_size - 1U)) {
            break;
        }

        written = snprintf(out_text + offset,
                           out_size - (unsigned int)offset,
                           "%s%s",
                           index == start ? "" : " ",
                           (const char *)argv[index]);
        if (written < 0) {
            out_text[0] = '\0';
            return;
        }

        offset += written;
    }
}

static u32 river_diag_cmd(u16 argc, u8 *argv[])
{
#if RIVER_CLOUD_TEXT_DEBUG_ENABLED || RIVER_INTERACTION_DIAG_ENABLED
    char echo_text[RIVER_ECHO_TEXT_MAX];
#endif
#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    char xiaozhi_text[RIVER_XIAOZHI_TEXT_MAX];
#endif
    const char *audio_action;

    /* 统一从这里分发所有 `river ...` 诊断子命令。 */
    if (argc == 0) {
        river_diag_help();
        return 0;
    }

    if (strcmp((const char *)argv[0], "status") == 0) {
        river_app_print_status();
        river_runtime_stats_snapshot("diag_status");
        return 0;
    }

    if (strcmp((const char *)argv[0], "echo") == 0 ||
        strcmp((const char *)argv[0], "tts") == 0) {
#if !RIVER_CLOUD_TEXT_DEBUG_ENABLED
        printf("[river][diag] cloud text/tts debug disabled in current build\n");
        return 0;
#else
        if (argc < 2) {
            printf("[river][diag] missing tts text\n");
            return 0;
        }

        river_diag_join_args(argc, argv, 1, echo_text, sizeof(echo_text));
        if (river_online_control_echo(echo_text) != RIVER_OK) {
            printf("[river][diag] echo failed\n");
        }
        return 0;
#endif
    }

    if (strcmp((const char *)argv[0], "xiaozhi") == 0) {
#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
        river_xiaozhi_config_t config;
        river_status_t status;

        if (argc < 2) {
            printf("[river][diag] usage: river xiaozhi <status|set|ota|bootstrap|disable|protocol|mcp|connect|disconnect|listen|abort>\n");
            return 0;
        }

        if (strcmp((const char *)argv[1], "status") == 0) {
            river_cloud_adapter_dump_status();
            return 0;
        }

        if (river_xiaozhi_get_config(&config) != RIVER_OK) {
            printf("[river][diag] xiaozhi config unavailable\n");
            return 0;
        }

        if (strcmp((const char *)argv[1], "set") == 0) {
            if (argc < 3) {
                printf("[river][diag] usage: river xiaozhi set <url> [token|-]\n");
                return 0;
            }

            config.url = (const char *)argv[2];
            if (argc >= 4) {
                config.token = (strcmp((const char *)argv[3], "-") == 0) ?
                                   "" :
                                   (const char *)argv[3];
            }

            status = river_cloud_adapter_set_xiaozhi_config(&config);
            if (status != RIVER_OK) {
                printf("[river][diag] xiaozhi set failed status=%d\n", status);
            } else {
                river_cloud_adapter_dump_status();
            }
            return 0;
        }

        if (strcmp((const char *)argv[1], "ota") == 0) {
            if (argc < 3) {
                printf("[river][diag] usage: river xiaozhi ota <url>\n");
                return 0;
            }

            config.ota_url = (const char *)argv[2];
            status = river_cloud_adapter_set_xiaozhi_config(&config);
            if (status != RIVER_OK) {
                printf("[river][diag] xiaozhi ota update failed status=%d\n", status);
            } else {
                river_cloud_adapter_dump_status();
            }
            return 0;
        }

        if (strcmp((const char *)argv[1], "bootstrap") == 0) {
            status = river_xiaozhi_bootstrap();
            if (status != RIVER_OK) {
                printf("[river][diag] xiaozhi bootstrap failed status=%d\n", status);
            } else {
                river_cloud_adapter_dump_status();
            }
            return 0;
        }

        if (strcmp((const char *)argv[1], "disable") == 0) {
            config.ota_url = "";
            config.url = "";
            config.token = "";
            status = river_cloud_adapter_set_xiaozhi_config(&config);
            if (status != RIVER_OK) {
                printf("[river][diag] xiaozhi disable failed status=%d\n", status);
            } else {
                river_cloud_adapter_dump_status();
            }
            return 0;
        }

        if (strcmp((const char *)argv[1], "protocol") == 0) {
            uint16_t version;

            if (argc < 3) {
                printf("[river][diag] usage: river xiaozhi protocol <2|3>\n");
                return 0;
            }

            version = (uint16_t)atoi((const char *)argv[2]);
            if (version != 2U && version != 3U) {
                printf("[river][diag] xiaozhi protocol must be 2 or 3\n");
                return 0;
            }

            config.protocol_version = version;
            status = river_cloud_adapter_set_xiaozhi_config(&config);
            if (status != RIVER_OK) {
                printf("[river][diag] xiaozhi protocol update failed status=%d\n", status);
            } else {
                river_cloud_adapter_dump_status();
            }
            return 0;
        }

        if (strcmp((const char *)argv[1], "mcp") == 0) {
            if (argc < 3) {
                printf("[river][diag] usage: river xiaozhi mcp <on|off>\n");
                return 0;
            }

            if (strcmp((const char *)argv[2], "on") == 0) {
                config.enable_mcp = true;
            } else if (strcmp((const char *)argv[2], "off") == 0) {
                config.enable_mcp = false;
            } else {
                printf("[river][diag] usage: river xiaozhi mcp <on|off>\n");
                return 0;
            }

            status = river_cloud_adapter_set_xiaozhi_config(&config);
            if (status != RIVER_OK) {
                printf("[river][diag] xiaozhi mcp update failed status=%d\n", status);
            } else {
                river_cloud_adapter_dump_status();
            }
            return 0;
        }

        if (strcmp((const char *)argv[1], "connect") == 0) {
            status = river_xiaozhi_open_session();
            if (status != RIVER_OK) {
                printf("[river][diag] xiaozhi connect failed status=%d\n", status);
            } else {
                river_cloud_adapter_dump_status();
            }
            return 0;
        }

        if (strcmp((const char *)argv[1], "disconnect") == 0) {
            river_cloud_adapter_notify_network_lost();
            river_cloud_adapter_dump_status();
            return 0;
        }

        if (strcmp((const char *)argv[1], "listen") == 0) {
            if (argc < 3) {
                printf("[river][diag] usage: river xiaozhi listen <start|stop|detect [text]>\n");
                return 0;
            }

            if (strcmp((const char *)argv[2], "start") == 0) {
                status = river_xiaozhi_send_listen_start("realtime");
            } else if (strcmp((const char *)argv[2], "stop") == 0) {
                status = river_xiaozhi_send_listen_stop();
            } else if (strcmp((const char *)argv[2], "detect") == 0) {
                if (argc < 4) {
                    printf("[river][diag] usage: river xiaozhi listen detect <text>\n");
                    return 0;
                }
                river_diag_join_args(argc, argv, 3, xiaozhi_text, sizeof(xiaozhi_text));
                status = river_xiaozhi_send_listen_detect(xiaozhi_text);
            } else {
                printf("[river][diag] usage: river xiaozhi listen <start|stop|detect [text]>\n");
                return 0;
            }

            if (status != RIVER_OK) {
                printf("[river][diag] xiaozhi listen command failed status=%d\n", status);
            }
            return 0;
        }

        if (strcmp((const char *)argv[1], "abort") == 0) {
            const char *reason = NULL;

            if (argc >= 3) {
                river_diag_join_args(argc, argv, 2, xiaozhi_text, sizeof(xiaozhi_text));
                reason = xiaozhi_text;
            }

            status = river_xiaozhi_send_abort(reason);
            if (status != RIVER_OK) {
                printf("[river][diag] xiaozhi abort failed status=%d\n", status);
            }
            return 0;
        }

        printf("[river][diag] usage: river xiaozhi <status|set|ota|bootstrap|disable|protocol|mcp|connect|disconnect|listen|abort>\n");
#else
        printf("[river][diag] xiaozhi backend disabled in current build\n");
#endif
        return 0;
    }

    if (strcmp((const char *)argv[0], "device") == 0) {
        if (argc < 3) {
            printf("[river][diag] usage: river device <name> <on|off|toggle>\n");
            return 0;
        }

        if (river_online_control_set_device((const char *)argv[1],
                                            (const char *)argv[2]) != RIVER_OK) {
            printf("[river][diag] invalid device command\n");
        }
        return 0;
    }

    if (strcmp((const char *)argv[0], "interaction") == 0) {
#if !RIVER_INTERACTION_DIAG_ENABLED
        printf("[river][diag] interaction diag disabled in current build\n");
        return 0;
#else
        if (argc < 2) {
            printf("[river][diag] usage: river interaction <status|intents|tts_test|asr|device|invoke>\n");
            return 0;
        }

        if (strcmp((const char *)argv[1], "status") == 0) {
            river_interaction_diag_dump_status();
            return 0;
        }

        if (strcmp((const char *)argv[1], "intents") == 0) {
            river_interaction_diag_dump_intents();
            return 0;
        }

        if (strcmp((const char *)argv[1], "tts_test") == 0) {
            if (river_interaction_diag_submit_tts_test("diag_cli") != RIVER_OK) {
                printf("[river][diag] interaction tts_test failed\n");
            }
            return 0;
        }

        if (strcmp((const char *)argv[1], "invoke") == 0) {
            if (argc < 3) {
                printf("[river][diag] usage: river interaction invoke <intent_name>\n");
                return 0;
            }

            if (river_interaction_diag_invoke_intent((const char *)argv[2], "diag_cli") != RIVER_OK) {
                printf("[river][diag] interaction invoke failed\n");
            }
            return 0;
        }

        if (strcmp((const char *)argv[1], "asr") == 0) {
            if (argc < 3) {
                printf("[river][diag] usage: river interaction asr <text>\n");
                return 0;
            }

            river_diag_join_args(argc, argv, 2, echo_text, sizeof(echo_text));
            if (river_interaction_diag_route_text(echo_text, "diag_cli_asr", "diag_cli") == RIVER_ERR_NOT_FOUND) {
                printf("[river][diag] no interaction intent matched\n");
            }
            return 0;
        }

        if (strcmp((const char *)argv[1], "device") == 0) {
            if (argc < 4) {
                printf("[river][diag] usage: river interaction device <name> <on|off|toggle>\n");
                return 0;
            }

            if (river_interaction_diag_execute_device((const char *)argv[2],
                                                      (const char *)argv[3],
                                                      "diag_cli") != RIVER_OK) {
                printf("[river][diag] interaction device failed\n");
            }
            return 0;
        }

        printf("[river][diag] usage: river interaction <status|intents|tts_test|asr|device|invoke>\n");
        return 0;
#endif
    }

    if (strcmp((const char *)argv[0], "playback") == 0) {
        if (argc < 2) {
            printf("[river][diag] usage: river playback <status|stop|interrupt|flush|duck <gain>|unduck>\n");
            return 0;
        }

        if (strcmp((const char *)argv[1], "status") == 0) {
            river_playback_service_dump_status();
            return 0;
        }

        if (strcmp((const char *)argv[1], "stop") == 0) {
            if (river_playback_service_stop_stream() != RIVER_OK) {
                printf("[river][diag] playback stop failed\n");
            }
            return 0;
        }

        if (strcmp((const char *)argv[1], "interrupt") == 0) {
            if (river_playback_service_interrupt_stream() != RIVER_OK) {
                printf("[river][diag] playback interrupt failed\n");
            }
            return 0;
        }

        if (strcmp((const char *)argv[1], "flush") == 0) {
            if (river_playback_service_flush_stream() != RIVER_OK) {
                printf("[river][diag] playback flush failed\n");
            }
            return 0;
        }

        if (strcmp((const char *)argv[1], "duck") == 0) {
            float gain;

            if (argc < 3) {
                printf("[river][diag] usage: river playback duck <gain>\n");
                return 0;
            }

            gain = (float)atof((const char *)argv[2]);
            if (river_playback_service_set_ducking(true, gain) != RIVER_OK) {
                printf("[river][diag] playback duck failed\n");
            }
            return 0;
        }

        if (strcmp((const char *)argv[1], "unduck") == 0) {
            if (river_playback_service_set_ducking(false, 1.0f) != RIVER_OK) {
                printf("[river][diag] playback unduck failed\n");
            }
            return 0;
        }

        printf("[river][diag] usage: river playback <status|stop|interrupt|flush|duck <gain>|unduck>\n");
        return 0;
    }

    if (strcmp((const char *)argv[0], "audio") == 0) {
        if (argc < 2) {
            printf("[river][diag] usage: river audio <start|stop|status>\n");
            return 0;
        }

        if (strcmp((const char *)argv[1], "diag") == 0) {
            if (argc < 3) {
                printf("[river][diag] usage: river audio diag <on|off|status>\n");
                return 0;
            }

            audio_action = (const char *)argv[2];
            if (strcmp(audio_action, "on") == 0) {
                river_voice_echo_set_diag_enabled(true);
                river_voice_echo_dump_status();
                return 0;
            }

            if (strcmp(audio_action, "off") == 0) {
                river_voice_echo_set_diag_enabled(false);
                river_voice_echo_dump_status();
                return 0;
            }

            if (strcmp(audio_action, "status") == 0) {
                river_voice_echo_dump_status();
                river_voice_vad_probe_dump_status();
                return 0;
            }

            printf("[river][diag] usage: river audio diag <on|off|status>\n");
            return 0;
        }

        if (strcmp((const char *)argv[1], "echo") == 0) {
            if (argc < 3) {
                printf("[river][diag] usage: river audio echo <start|stop|status>\n");
                return 0;
            }
            audio_action = (const char *)argv[2];
        } else if (strcmp((const char *)argv[1], "probe") == 0) {
            if (argc < 3) {
                printf("[river][diag] usage: river audio probe <start|stop|status>\n");
                return 0;
            }
            audio_action = (const char *)argv[2];
            if (strcmp(audio_action, "status") == 0) {
                river_voice_vad_probe_dump_status();
                return 0;
            }

            if (strcmp(audio_action, "start") == 0) {
                if (river_voice_vad_probe_start() != RIVER_OK) {
                    printf("[river][diag] vad probe start failed\n");
                }
                return 0;
            }

            if (strcmp(audio_action, "stop") == 0) {
                if (river_voice_vad_probe_stop() != RIVER_OK) {
                    printf("[river][diag] vad probe stop failed\n");
                }
                return 0;
            }

            printf("[river][diag] usage: river audio probe <start|stop|status>\n");
            return 0;
        } else {
            audio_action = (const char *)argv[1];
        }

        if (strcmp(audio_action, "status") == 0) {
            river_voice_echo_dump_status();
            return 0;
        }

        if (strcmp(audio_action, "start") == 0) {
            if (river_voice_echo_start() != RIVER_OK) {
                printf("[river][diag] audio echo start failed\n");
            }
            return 0;
        }

        if (strcmp(audio_action, "stop") == 0) {
            if (river_voice_echo_stop() != RIVER_OK) {
                printf("[river][diag] audio echo stop failed\n");
            }
            return 0;
        }

        printf("[river][diag] usage: river audio <start|stop|status>\n");
        return 0;
    }

    river_diag_help();
    return 0;
}

CMD_TABLE_DATA_SECTION
const COMMAND_TABLE river_cmd_table[] = {
    {"river", river_diag_cmd},
};
#endif
