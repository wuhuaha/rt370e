#ifndef AMEBA_RIVER_TYPES_H
#define AMEBA_RIVER_TYPES_H

typedef enum {
    RIVER_OK = 0,
    RIVER_ERR_ARG = -1,
    RIVER_ERR_NOT_FOUND = -2,
    RIVER_ERR_UNSUPPORTED = -3,
    RIVER_ERR_BUSY = -4,
    RIVER_ERR_NO_MEMORY = -5,
    RIVER_ERR_IO = -6
} river_status_t;

#endif
