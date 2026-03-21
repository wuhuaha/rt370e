#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "river/river_log.h"
#include "river/river_online_control.h"
#include "river_xiaozhi_mcp_bridge.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.cloud.xz.mcp"

static void river_xiaozhi_mcp_add_id(cJSON *root, const cJSON *id_obj)
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

static river_status_t river_xiaozhi_mcp_parse_tool_name(const char *tool_name,
                                                        char *device_name,
                                                        size_t device_name_size,
                                                        char *action_name,
                                                        size_t action_name_size)
{
    const char *device_start;
    const char *device_end;
    const char *action_start;
    size_t device_len;

    if (tool_name == NULL || device_name == NULL || action_name == NULL ||
        device_name_size == 0U || action_name_size == 0U) {
        return RIVER_ERR_ARG;
    }

    if (strncmp(tool_name, "self.", 5U) != 0) {
        return RIVER_ERR_UNSUPPORTED;
    }

    device_start = tool_name + 5U;
    device_end = strchr(device_start, '.');
    if (device_end == NULL) {
        return RIVER_ERR_UNSUPPORTED;
    }

    action_start = device_end + 1;
    if (*action_start == '\0') {
        return RIVER_ERR_UNSUPPORTED;
    }

    device_len = (size_t)(device_end - device_start);
    if (device_len == 0U || device_len >= device_name_size) {
        return RIVER_ERR_UNSUPPORTED;
    }

    memcpy(device_name, device_start, device_len);
    device_name[device_len] = '\0';

    if (strcmp(action_start, "turn_on") == 0 || strcmp(action_start, "on") == 0) {
        snprintf(action_name, action_name_size, "%s", "on");
    } else if (strcmp(action_start, "turn_off") == 0 || strcmp(action_start, "off") == 0) {
        snprintf(action_name, action_name_size, "%s", "off");
    } else if (strcmp(action_start, "toggle") == 0) {
        snprintf(action_name, action_name_size, "%s", "toggle");
    } else {
        return RIVER_ERR_UNSUPPORTED;
    }

    return RIVER_OK;
}

static river_status_t river_xiaozhi_mcp_build_result(const cJSON *id_obj,
                                                     bool is_error,
                                                     const char *text,
                                                     char *response_json,
                                                     size_t response_json_size)
{
    cJSON *root = NULL;
    cJSON *result = NULL;
    cJSON *content = NULL;
    cJSON *content_item = NULL;
    char *printed = NULL;
    river_status_t status = RIVER_ERR_IO;

    if (response_json == NULL || response_json_size == 0U) {
        return RIVER_ERR_ARG;
    }

    root = cJSON_CreateObject();
    result = cJSON_CreateObject();
    content = cJSON_CreateArray();
    content_item = cJSON_CreateObject();
    if (root == NULL || result == NULL || content == NULL || content_item == NULL) {
        goto exit;
    }

    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    river_xiaozhi_mcp_add_id(root, id_obj);

    cJSON_AddStringToObject(content_item, "type", "text");
    cJSON_AddStringToObject(content_item, "text", text != NULL ? text : (is_error ? "false" : "true"));
    cJSON_AddItemToArray(content, content_item);
    content_item = NULL;

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
    if (content_item != NULL) {
        cJSON_Delete(content_item);
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
    if (status != RIVER_OK && response_json_size > 0U) {
        response_json[0] = '\0';
    }
    return status;
}

river_status_t river_xiaozhi_mcp_bridge_handle(const cJSON *payload,
                                               char *response_json,
                                               size_t response_json_size)
{
    const cJSON *method_obj;
    const cJSON *params_obj;
    const cJSON *name_obj;
    const cJSON *id_obj;
    char device_name[24];
    char action_name[16];
    river_status_t status;

    if (payload == NULL || response_json == NULL || response_json_size == 0U) {
        return RIVER_ERR_ARG;
    }

    response_json[0] = '\0';
    method_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "method");
    params_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "params");
    name_obj = cJSON_IsObject(params_obj) ?
               cJSON_GetObjectItemCaseSensitive((cJSON *)params_obj, "name") : NULL;
    id_obj = cJSON_GetObjectItemCaseSensitive((cJSON *)payload, "id");

    if (!cJSON_IsString(method_obj) || method_obj->valuestring == NULL) {
        return RIVER_ERR_UNSUPPORTED;
    }

    if (strcmp(method_obj->valuestring, "tools/call") != 0) {
        return RIVER_ERR_UNSUPPORTED;
    }

    if (!cJSON_IsString(name_obj) || name_obj->valuestring == NULL) {
        return river_xiaozhi_mcp_build_result(id_obj,
                                              true,
                                              "missing tool name",
                                              response_json,
                                              response_json_size);
    }

    status = river_xiaozhi_mcp_parse_tool_name(name_obj->valuestring,
                                               device_name,
                                               sizeof(device_name),
                                               action_name,
                                               sizeof(action_name));
    if (status != RIVER_OK) {
        RIVER_LOGW("unsupported mcp tool: %s", name_obj->valuestring);
        return river_xiaozhi_mcp_build_result(id_obj,
                                              true,
                                              "unsupported tool",
                                              response_json,
                                              response_json_size);
    }

    status = river_online_control_set_device(device_name, action_name);
    if (status != RIVER_OK) {
        RIVER_LOGW("mcp tool failed: %s -> %s status=%d",
                   device_name,
                   action_name,
                   status);
        return river_xiaozhi_mcp_build_result(id_obj,
                                              true,
                                              "tool call failed",
                                              response_json,
                                              response_json_size);
    }

    RIVER_LOGI("mcp tool applied: %s -> %s", device_name, action_name);
    return river_xiaozhi_mcp_build_result(id_obj,
                                          false,
                                          "true",
                                          response_json,
                                          response_json_size);
}
