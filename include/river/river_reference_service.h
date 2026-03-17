#ifndef AMEBA_RIVER_REFERENCE_SERVICE_H
#define AMEBA_RIVER_REFERENCE_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "river/river_types.h"

typedef enum {
    RIVER_REFERENCE_IDLE = 0,
    RIVER_REFERENCE_OPEN,
    RIVER_REFERENCE_STARVED,
    RIVER_REFERENCE_ERROR
} river_reference_state_t;

typedef struct {
    const char *stream_name;
    const char *source_name;
    uint32_t sample_rate;
    uint32_t frame_ms;
    uint32_t channels;
    uint32_t history_ms;
} river_reference_service_config_t;

typedef struct {
    river_reference_state_t state;
    char stream_name[32];
    char source_name[48];
    uint32_t sample_rate;
    uint32_t frame_ms;
    uint32_t channels;
    uint32_t history_ms;
    uint32_t open_count;
    uint32_t close_count;
    uint32_t reset_count;
    uint32_t write_ok;
    uint32_t write_fail;
    uint32_t read_ok;
    uint32_t read_miss;
} river_reference_service_stats_t;

river_status_t river_reference_service_init(void);
river_status_t river_reference_service_open(const river_reference_service_config_t *config);
void river_reference_service_reset(void);
void river_reference_service_close(void);
river_status_t river_reference_service_write(const uint8_t *data, size_t bytes);
river_status_t river_reference_service_read(uint8_t *data, size_t bytes);
bool river_reference_service_is_open(void);
river_reference_state_t river_reference_service_state(void);
const char *river_reference_service_state_name(river_reference_state_t state);
const char *river_reference_service_backend_name(void);
void river_reference_service_get_stats(river_reference_service_stats_t *stats);
void river_reference_service_dump_profile(void);
void river_reference_service_dump_status(void);

#endif
