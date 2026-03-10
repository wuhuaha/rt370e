#include <stdio.h>
#include <string.h>

#include "platform_stdlib.h"
#include "basic_types.h"

#include "river/river_app.h"
#include "river/river_online_control.h"

#ifdef CONFIG_RIVER_DIAG_CMD_EN
#define RIVER_ECHO_TEXT_MAX 128

static void river_diag_help(void)
{
    printf("\triver status\n");
    printf("\triver echo <text>\n");
    printf("\triver audio <start|stop|status>\n");
    printf("\triver audio echo <start|stop|status>\n");
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
    char echo_text[RIVER_ECHO_TEXT_MAX];
    const char *audio_action;

    if (argc == 0) {
        river_diag_help();
        return 0;
    }

    if (strcmp((const char *)argv[0], "status") == 0) {
        river_app_print_status();
        return 0;
    }

    if (strcmp((const char *)argv[0], "echo") == 0) {
        if (argc < 2) {
            printf("[river][diag] missing echo text\n");
            return 0;
        }

        river_diag_join_args(argc, argv, 1, echo_text, sizeof(echo_text));
        if (river_online_control_echo(echo_text) != RIVER_OK) {
            printf("[river][diag] echo failed\n");
        }
        return 0;
    }

    if (strcmp((const char *)argv[0], "device") == 0) {
        if (argc < 3) {
            printf("[river][diag] usage: river device <name> <on|off|toggle>\n");
            return 0;
        }

        if (river_online_control_set_device((const char *)argv[1], (const char *)argv[2]) != RIVER_OK) {
            printf("[river][diag] invalid device command\n");
        }
        return 0;
    }

    if (strcmp((const char *)argv[0], "audio") == 0) {
        if (argc < 2) {
            printf("[river][diag] usage: river audio <start|stop|status>\n");
            return 0;
        }

        if (strcmp((const char *)argv[1], "echo") == 0) {
            if (argc < 3) {
                printf("[river][diag] usage: river audio echo <start|stop|status>\n");
                return 0;
            }
            audio_action = (const char *)argv[2];
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
