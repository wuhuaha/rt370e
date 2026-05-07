/* Orvibo MCP bridge: only expose speaker volume control. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "audio/audio_control.h"

#include "river/river_log.h"
#include "river/river_orvibo_mcp_volume.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.orvibo.mcp"

static uint8_t g_river_orvibo_volume_percent = 80U;

static void river_orvibo_mcp_add_id(cJSON *root, const cJSON *id_obj)
{
    if (root == NULL || id_obj == NULL) {
        return;
    }

    if (cJSON_IsString(id_obj) && id_obj->valuestring != NULL) {
        cJSON_AddStringToObject(root, "id", id_obj->valuestring);
    } else if (cJSON_IsNumber(id_obj)) {
        cJSON_AddNumberToObject(root, "id", id_obj->valuedouble);
    } else {
        cJSON_AddNullToObject(root, "id");
    }
}

static river_status_t river_orvibo_mcp_build_result(const cJSON *id_obj,
                                                    bool is_error,
                                                    const char *text,
                                                    char *response_json,
                                                    size_t response_json_size)
{
    cJSON *root = NULL;
    cJSON *result = NULL;
    cJSON *content = NULL;
    cJSON *item = NULL;
    char *printed = NULL;
    river_status_t status = RIVER_ERR_IO;

    if (response_json == NULL || response_json_size == 0U) {
        return RIVER_ERR_ARG;
    }

    response_json[0] = '\0';
    root = cJSON_CreateObject();
    result = cJSON_CreateObject();
    content = cJSON_CreateArray();
    item = cJSON_CreateObject();
    if (root == NULL || result == NULL || content == NULL || item == NULL) {
        goto exit;
    }

    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    river_orvibo_mcp_add_id(root, id_obj);
    cJSON_AddStringToObject(item, "type", "text");
    cJSON_AddStringToObject(item, "text", text != NULL ? text : (is_error ? "false" : "true"));
    cJSON_AddItemToArray(content, item);
    item = NULL;
    cJSON_AddItemToObject(result, "content", content);
    content = NULL;
    cJSON_AddBoolToObject(result, "isError", is_error);
    cJSON_AddItemToObject(root, "result", result);
    result = NULL;

    printed = cJSON_PrintUnformatted(root);
    if (printed == NULL) {
        goto exit;
    }
    if (snprintf(response_json, response_json_size, "%s", printed) >= (int)response_json_size) {
        goto exit;
    }
    status = RIVER_OK;

exit:
    if (printed != NULL) {
        cJSON_free(printed);
    }
    if (item != NULL) {
        cJSON_Delete(item);
    }
    if (content != NULL) {
        cJSON_Delete(content);
    }
    if (result != NULL) {
        cJSON_Delete(result);
    }
    if (root != NULL) {
        cJSON_Delete(root);
    }
    if (status != RIVER_OK) {
        response_json[0] = '\0';
    }
    return status;
}

void river_orvibo_mcp_volume_set(uint8_t volume_percent)
{
    float gain;

    if (volume_percent > 100U) {
        volume_percent = 100U;
    }
    g_river_orvibo_volume_percent = volume_percent;
    gain = (float)volume_percent / 100.0f;
    AudioControl_SetHardwareVolume(gain, gain);
    RIVER_LOGI("volume set: percent=%u gain=%.2f", volume_percent, (double)gain);
}

uint8_t river_orvibo_mcp_volume_get(void)
{
    return g_river_orvibo_volume_percent;
}

river_status_t river_orvibo_mcp_volume_handle(const cJSON *payload,
                                              char *response_json,
                                              size_t response_json_size)
{
    const cJSON *method_obj;
    const cJSON *params_obj;
    const cJSON *name_obj;
    const cJSON *args_obj;
    const cJSON *id_obj;
    const cJSON *volume_obj;
    char text[96];

    if (payload == NULL || response_json == NULL || response_json_size == 0U) {
        return RIVER_ERR_ARG;
    }

    method_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "method");
    params_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "params");
    name_obj = cJSON_IsObject(params_obj) ?
                   cJSON_GetObjectItemCaseSensitive((cJSON *)params_obj, "name") :
                   NULL;
    args_obj = cJSON_IsObject(params_obj) ?
                   cJSON_GetObjectItemCaseSensitive((cJSON *)params_obj, "arguments") :
                   NULL;
    id_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "id");

    if (!cJSON_IsString(method_obj) || method_obj->valuestring == NULL ||
        strcmp(method_obj->valuestring, "tools/call") != 0) {
        return river_orvibo_mcp_build_result(id_obj,
                                             true,
                                             "unsupported method",
                                             response_json,
                                             response_json_size);
    }

    if (!cJSON_IsString(name_obj) || name_obj->valuestring == NULL) {
        return river_orvibo_mcp_build_result(id_obj,
                                             true,
                                             "missing tool name",
                                             response_json,
                                             response_json_size);
    }

    if (strcmp(name_obj->valuestring, "self.get_device_status") == 0) {
        snprintf(text, sizeof(text), "{\"volume\":%u}", (unsigned int)g_river_orvibo_volume_percent);
        return river_orvibo_mcp_build_result(id_obj,
                                             false,
                                             text,
                                             response_json,
                                             response_json_size);
    }

    if (strcmp(name_obj->valuestring, "self.audio_speaker.set_volume") == 0) {
        volume_obj = cJSON_IsObject(args_obj) ?
                         cJSON_GetObjectItemCaseSensitive((cJSON *)args_obj, "volume") :
                         NULL;
        if (!cJSON_IsNumber(volume_obj)) {
            volume_obj = cJSON_IsObject(args_obj) ?
                             cJSON_GetObjectItemCaseSensitive((cJSON *)args_obj, "level") :
                             NULL;
        }
        if (!cJSON_IsNumber(volume_obj)) {
            return river_orvibo_mcp_build_result(id_obj,
                                                 true,
                                                 "missing volume",
                                                 response_json,
                                                 response_json_size);
        }
        river_orvibo_mcp_volume_set((uint8_t)volume_obj->valueint);
        return river_orvibo_mcp_build_result(id_obj,
                                             false,
                                             "true",
                                             response_json,
                                             response_json_size);
    }

    RIVER_LOGW("unsupported mcp tool: %s", name_obj->valuestring);
    return river_orvibo_mcp_build_result(id_obj,
                                         true,
                                         "unsupported tool",
                                         response_json,
                                         response_json_size);
}

void river_orvibo_mcp_volume_dump_status(void)
{
    RIVER_LOGI("orvibo mcp: volume=%u tools=self.get_device_status,self.audio_speaker.set_volume",
               (unsigned int)g_river_orvibo_volume_percent);
}
