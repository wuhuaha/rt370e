/* 小智 MCP 桥接私有接口。 */
#ifndef AMEBA_RIVER_XIAOZHI_MCP_BRIDGE_H
#define AMEBA_RIVER_XIAOZHI_MCP_BRIDGE_H

#include <stddef.h>

#include "cJSON.h"

#include "river/river_types.h"

river_status_t river_xiaozhi_mcp_bridge_handle(const cJSON *payload,
                                               char *response_json,
                                               size_t response_json_size);

#endif
