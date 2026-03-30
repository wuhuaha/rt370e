/* 在线控制示例实现：当前维护本地设备状态并提供文本调试回显。 */
#include <string.h>

#include "river/river_cloud.h"
#include "river/river_log.h"
#include "river/river_online_control.h"

#if defined(CONFIG_RIVER_CLOUD_TEXT_DEBUG_EN)
#define RIVER_CLOUD_TEXT_DEBUG_ENABLED 1
#else
#define RIVER_CLOUD_TEXT_DEBUG_ENABLED 0
#endif

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.control"

typedef struct {
    const char *name;
    int is_on;
} river_device_state_t;

static river_device_state_t g_river_devices[] = {
    {"light", 0},
    {"fan", 0},
    {"curtain", 0},
    {"socket", 0}
};

static river_device_state_t *river_find_device(const char *device_name)
{
    unsigned int index;

    if (device_name == 0) {
        return 0;
    }

    for (index = 0; index < (sizeof(g_river_devices) / sizeof(g_river_devices[0])); ++index) {
        if (strcmp(g_river_devices[index].name, device_name) == 0) {
            return &g_river_devices[index];
        }
    }

    return 0;
}

river_status_t river_online_control_init(void)
{
    unsigned int index;

    for (index = 0; index < (sizeof(g_river_devices) / sizeof(g_river_devices[0])); ++index) {
        g_river_devices[index].is_on = 0;
    }

    RIVER_LOGI("online control service init: text_debug=%s",
               RIVER_CLOUD_TEXT_DEBUG_ENABLED ? "compiled" : "stubbed");
    return RIVER_OK;
}

river_status_t river_online_control_echo(const char *text)
{
    if (text == 0) {
        return RIVER_ERR_ARG;
    }

#if RIVER_CLOUD_TEXT_DEBUG_ENABLED
    return river_cloud_adapter_submit_text(text);
#else
    (void)text;
    return RIVER_ERR_UNSUPPORTED;
#endif
}

river_status_t river_online_control_set_device(const char *device_name, const char *action_name)
{
    river_device_state_t *device = river_find_device(device_name);

    if (device == 0 || action_name == 0) {
        return RIVER_ERR_ARG;
    }

    if (strcmp(action_name, "on") == 0) {
        device->is_on = 1;
    } else if (strcmp(action_name, "off") == 0) {
        device->is_on = 0;
    } else if (strcmp(action_name, "toggle") == 0) {
        device->is_on = !device->is_on;
    } else {
        return RIVER_ERR_UNSUPPORTED;
    }

    RIVER_LOGI("%s => %s", device->name, device->is_on ? "on" : "off");
    return RIVER_OK;
}

void river_online_control_dump_status(void)
{
    unsigned int index;

    RIVER_LOGI("devices: text_debug=%s",
               RIVER_CLOUD_TEXT_DEBUG_ENABLED ? "compiled" : "stubbed");
    for (index = 0; index < (sizeof(g_river_devices) / sizeof(g_river_devices[0])); ++index) {
        RIVER_LOGI("  %s=%s",
                   g_river_devices[index].name,
                   g_river_devices[index].is_on ? "on" : "off");
    }
}
