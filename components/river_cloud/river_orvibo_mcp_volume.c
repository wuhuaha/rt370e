/* Orvibo MCP bridge: only expose speaker volume control. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "audio/audio_control.h"

#include "river/river_log.h"
#include "river/river_orvibo_build_info.h"
#include "river/river_orvibo_mcp_volume.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.orvibo.mcp"

static uint8_t g_river_orvibo_volume_percent = 100U;

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

static river_status_t river_orvibo_mcp_build_raw_result(const cJSON *id_obj,
                                                        cJSON *result,
                                                        char *response_json,
                                                        size_t response_json_size)
{
    cJSON *root = NULL;
    char *printed = NULL;
    river_status_t status = RIVER_ERR_IO;

    if (result == NULL || response_json == NULL || response_json_size == 0U) {
        if (result != NULL) {
            cJSON_Delete(result);
        }
        return RIVER_ERR_ARG;
    }
    response_json[0] = '\0';
    root = cJSON_CreateObject();
    if (root == NULL) {
        cJSON_Delete(result);
        return RIVER_ERR_NO_MEMORY;
    }
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    river_orvibo_mcp_add_id(root, id_obj);
    cJSON_AddItemToObject(root, "result", result);
    printed = cJSON_PrintUnformatted(root);
    if (printed != NULL &&
        snprintf(response_json, response_json_size, "%s", printed) < (int)response_json_size) {
        status = RIVER_OK;
    }
    if (printed != NULL) {
        cJSON_free(printed);
    }
    cJSON_Delete(root);
    if (status != RIVER_OK) {
        response_json[0] = '\0';
    }
    return status;
}

static river_status_t river_orvibo_mcp_build_error(const cJSON *id_obj,
                                                   const char *message,
                                                   char *response_json,
                                                   size_t response_json_size)
{
    cJSON *root = NULL;
    cJSON *error = NULL;
    char *printed = NULL;
    river_status_t status = RIVER_ERR_IO;

    if (response_json == NULL || response_json_size == 0U) {
        return RIVER_ERR_ARG;
    }
    response_json[0] = '\0';
    root = cJSON_CreateObject();
    error = cJSON_CreateObject();
    if (root == NULL || error == NULL) {
        goto exit;
    }
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    river_orvibo_mcp_add_id(root, id_obj);
    cJSON_AddStringToObject(error, "message", message != NULL ? message : "error");
    cJSON_AddItemToObject(root, "error", error);
    error = NULL;
    printed = cJSON_PrintUnformatted(root);
    if (printed != NULL &&
        snprintf(response_json, response_json_size, "%s", printed) < (int)response_json_size) {
        status = RIVER_OK;
    }

exit:
    if (printed != NULL) {
        cJSON_free(printed);
    }
    if (error != NULL) {
        cJSON_Delete(error);
    }
    if (root != NULL) {
        cJSON_Delete(root);
    }
    if (status != RIVER_OK) {
        response_json[0] = '\0';
    }
    return status;
}

static river_status_t river_orvibo_mcp_build_tool_argument_error(const cJSON *id_obj,
                                                                 const char *argument_name,
                                                                 char *response_json,
                                                                 size_t response_json_size)
{
    char message[96];

    if (argument_name == NULL || argument_name[0] == '\0') {
        return river_orvibo_mcp_build_error(id_obj,
                                            "Missing valid argument",
                                            response_json,
                                            response_json_size);
    }
    snprintf(message, sizeof(message), "Missing valid argument: %s", argument_name);
    return river_orvibo_mcp_build_error(id_obj,
                                        message,
                                        response_json,
                                        response_json_size);
}

static river_status_t river_orvibo_mcp_handle_initialize(const cJSON *id_obj,
                                                         char *response_json,
                                                         size_t response_json_size)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *capabilities = cJSON_CreateObject();
    cJSON *tools = cJSON_CreateObject();
    cJSON *server = cJSON_CreateObject();

    if (result == NULL || capabilities == NULL || tools == NULL || server == NULL) {
        if (server != NULL) {
            cJSON_Delete(server);
        }
        if (tools != NULL) {
            cJSON_Delete(tools);
        }
        if (capabilities != NULL) {
            cJSON_Delete(capabilities);
        }
        if (result != NULL) {
            cJSON_Delete(result);
        }
        return RIVER_ERR_NO_MEMORY;
    }
    cJSON_AddStringToObject(result, "protocolVersion", "2024-11-05");
    cJSON_AddItemToObject(capabilities, "tools", tools);
    tools = NULL;
    cJSON_AddItemToObject(result, "capabilities", capabilities);
    capabilities = NULL;
    cJSON_AddStringToObject(server, "name", river_orvibo_build_info_app_name());
    cJSON_AddStringToObject(server, "version", river_orvibo_build_info_app_version());
    cJSON_AddItemToObject(result, "serverInfo", server);
    server = NULL;
    return river_orvibo_mcp_build_raw_result(id_obj, result, response_json, response_json_size);
}

static cJSON *river_orvibo_mcp_build_tool(const char *name,
                                          const char *description,
                                          bool with_volume_property)
{
    cJSON *tool = cJSON_CreateObject();
    cJSON *schema = cJSON_CreateObject();
    cJSON *properties = cJSON_CreateObject();
    cJSON *volume = NULL;
    cJSON *required = NULL;

    if (tool == NULL || schema == NULL || properties == NULL) {
        goto fail;
    }
    cJSON_AddStringToObject(tool, "name", name);
    cJSON_AddStringToObject(tool, "description", description);
    cJSON_AddStringToObject(schema, "type", "object");
    if (with_volume_property) {
        volume = cJSON_CreateObject();
        required = cJSON_CreateArray();
        if (volume == NULL || required == NULL) {
            goto fail;
        }
        cJSON_AddStringToObject(volume, "type", "integer");
        cJSON_AddNumberToObject(volume, "minimum", 0);
        cJSON_AddNumberToObject(volume, "maximum", 100);
        cJSON_AddItemToObject(properties, "volume", volume);
        volume = NULL;
        cJSON_AddItemToArray(required, cJSON_CreateString("volume"));
        cJSON_AddItemToObject(schema, "required", required);
        required = NULL;
    }
    cJSON_AddItemToObject(schema, "properties", properties);
    properties = NULL;
    cJSON_AddItemToObject(tool, "inputSchema", schema);
    schema = NULL;
    return tool;

fail:
    if (required != NULL) {
        cJSON_Delete(required);
    }
    if (volume != NULL) {
        cJSON_Delete(volume);
    }
    if (properties != NULL) {
        cJSON_Delete(properties);
    }
    if (schema != NULL) {
        cJSON_Delete(schema);
    }
    if (tool != NULL) {
        cJSON_Delete(tool);
    }
    return NULL;
}

static river_status_t river_orvibo_mcp_handle_tools_list(const cJSON *id_obj,
                                                         char *response_json,
                                                         size_t response_json_size)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *tools = cJSON_CreateArray();
    cJSON *status_tool = NULL;
    cJSON *volume_tool = NULL;

    if (result == NULL || tools == NULL) {
        goto fail;
    }
    status_tool = river_orvibo_mcp_build_tool(
        "self.get_device_status",
        "Get current device status. The volume field is the speaker volume percentage.",
        false);
    volume_tool = river_orvibo_mcp_build_tool(
        "self.audio_speaker.set_volume",
        "Set speaker volume. Argument volume must be an integer from 0 to 100.",
        true);
    if (status_tool == NULL || volume_tool == NULL) {
        goto fail;
    }
    cJSON_AddItemToArray(tools, status_tool);
    status_tool = NULL;
    cJSON_AddItemToArray(tools, volume_tool);
    volume_tool = NULL;
    cJSON_AddItemToObject(result, "tools", tools);
    tools = NULL;
    return river_orvibo_mcp_build_raw_result(id_obj, result, response_json, response_json_size);

fail:
    if (volume_tool != NULL) {
        cJSON_Delete(volume_tool);
    }
    if (status_tool != NULL) {
        cJSON_Delete(status_tool);
    }
    if (tools != NULL) {
        cJSON_Delete(tools);
    }
    if (result != NULL) {
        cJSON_Delete(result);
    }
    return RIVER_ERR_NO_MEMORY;
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
    int volume_value;

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

    if (!cJSON_IsString(method_obj) || method_obj->valuestring == NULL) {
        return river_orvibo_mcp_build_error(id_obj,
                                            "missing method",
                                            response_json,
                                            response_json_size);
    }

    if (strcmp(method_obj->valuestring, "notifications/initialized") == 0) {
        response_json[0] = '\0';
        return RIVER_OK;
    }

    if (strcmp(method_obj->valuestring, "initialize") == 0) {
        return river_orvibo_mcp_handle_initialize(id_obj, response_json, response_json_size);
    }

    if (strcmp(method_obj->valuestring, "tools/list") == 0) {
        return river_orvibo_mcp_handle_tools_list(id_obj, response_json, response_json_size);
    }

    if (strcmp(method_obj->valuestring, "tools/call") != 0) {
        return river_orvibo_mcp_build_error(id_obj,
                                            "method not implemented",
                                            response_json,
                                            response_json_size);
    }

    if (!cJSON_IsString(name_obj) || name_obj->valuestring == NULL) {
        return river_orvibo_mcp_build_error(id_obj,
                                            "Missing name",
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
            return river_orvibo_mcp_build_tool_argument_error(id_obj,
                                                              "volume",
                                                              response_json,
                                                              response_json_size);
        }
        volume_value = volume_obj->valueint;
        if (volume_value < 0) {
            return river_orvibo_mcp_build_error(id_obj,
                                                "Value is below minimum allowed: 0",
                                                response_json,
                                                response_json_size);
        }
        if (volume_value > 100) {
            return river_orvibo_mcp_build_error(id_obj,
                                                "Value exceeds maximum allowed: 100",
                                                response_json,
                                                response_json_size);
        }
        river_orvibo_mcp_volume_set((uint8_t)volume_value);
        return river_orvibo_mcp_build_result(id_obj,
                                             false,
                                             "true",
                                             response_json,
                                             response_json_size);
    }

    RIVER_LOGW("unsupported mcp tool: %s", name_obj->valuestring);
    snprintf(text, sizeof(text), "Unknown tool: %s", name_obj->valuestring);
    return river_orvibo_mcp_build_error(id_obj,
                                        text,
                                        response_json,
                                        response_json_size);
}

void river_orvibo_mcp_volume_dump_status(void)
{
    RIVER_LOGI("orvibo mcp: volume=%u tools=self.get_device_status,self.audio_speaker.set_volume",
               (unsigned int)g_river_orvibo_volume_percent);
}
