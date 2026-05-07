/* Orvibo MCP bridge: volume-only tool surface. */
#ifndef AMEBA_RIVER_ORVIBO_MCP_VOLUME_H
#define AMEBA_RIVER_ORVIBO_MCP_VOLUME_H

#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

#include "river/river_types.h"

river_status_t river_orvibo_mcp_volume_handle(const cJSON *payload,
                                              char *response_json,
                                              size_t response_json_size);
void river_orvibo_mcp_volume_set(uint8_t volume_percent);
uint8_t river_orvibo_mcp_volume_get(void);
void river_orvibo_mcp_volume_dump_status(void);

#endif
