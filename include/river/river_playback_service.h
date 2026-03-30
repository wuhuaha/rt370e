/* 播放服务接口：统一管理 TTS/提示音流的启动、写入和抢占。 */
#ifndef AMEBA_RIVER_PLAYBACK_SERVICE_H
#define AMEBA_RIVER_PLAYBACK_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "river/river_types.h"

typedef enum {
    RIVER_PLAYBACK_IDLE = 0,
    RIVER_PLAYBACK_PREPARING,
    RIVER_PLAYBACK_RUNNING,
    RIVER_PLAYBACK_DRAINING,
    RIVER_PLAYBACK_STOPPING,
    RIVER_PLAYBACK_ERROR
} river_playback_state_t;

typedef enum {
    RIVER_PLAYBACK_PRIO_ALERT = 0,
    RIVER_PLAYBACK_PRIO_PROMPT = 1,
    RIVER_PLAYBACK_PRIO_TTS = 2,
    RIVER_PLAYBACK_PRIO_DEBUG = 3
} river_playback_priority_t;

typedef struct {
    const char *stream_name;
    river_playback_priority_t priority;
    uint32_t sample_rate;
    uint32_t frame_ms;
    uint32_t playback_channels;
    uint32_t bits_per_sample;
    size_t playback_frame_bytes;
    /* Target application-side queued frames, not a multiplier on SDK minBuffer. */
    uint32_t buffer_frame_count;
    float volume_left;
    float volume_right;
    bool reference_export;
    uint32_t reference_channels;
    size_t reference_frame_bytes;
    uint32_t reference_history_ms;
} river_playback_stream_config_t;

typedef struct {
    river_playback_state_t state;
    river_playback_priority_t priority;
    char stream_name[32];
    bool reference_export;
    bool ducked;
    uint32_t epoch;
    uint32_t epoch_advance_count;
    size_t track_buffer_bytes;
    uint32_t start_count;
    uint32_t stop_count;
    uint32_t interrupt_count;
    uint32_t flush_count;
    uint32_t duck_count;
    uint32_t write_ok;
    uint32_t write_fail;
    uint32_t ref_write_ok;
    uint32_t ref_write_fail;
    float duck_gain;
    char last_epoch_reason[48];
    char last_control[24];
    char last_control_reason[48];
    char last_interrupt_reason[48];
} river_playback_service_stats_t;

typedef void (*river_playback_service_listener_t)(river_playback_state_t state,
                                                  const river_playback_stream_config_t *config,
                                                  void *user_data);

river_status_t river_playback_service_init(void);
river_status_t river_playback_service_register_listener(river_playback_service_listener_t listener,
                                                        void *user_data);
river_status_t river_playback_service_start_stream(const river_playback_stream_config_t *config);
river_status_t river_playback_service_write(const uint8_t *playback,
                                            size_t playback_bytes,
                                            const uint8_t *reference,
                                            size_t reference_bytes,
                                            bool block);
river_status_t river_playback_service_stop_stream_ex(const char *reason);
river_status_t river_playback_service_interrupt_stream_ex(const char *reason);
river_status_t river_playback_service_flush_stream_ex(const char *reason);
river_status_t river_playback_service_set_ducking_ex(bool enabled, float gain, const char *reason);
river_status_t river_playback_service_stop_stream(void);
river_status_t river_playback_service_interrupt_stream(void);
river_status_t river_playback_service_flush_stream(void);
river_status_t river_playback_service_set_ducking(bool enabled, float gain);
river_playback_state_t river_playback_service_state(void);
uint32_t river_playback_service_epoch(void);
const char *river_playback_service_state_name(river_playback_state_t state);
bool river_playback_service_state_active(river_playback_state_t state);
bool river_playback_service_active(void);
bool river_playback_service_reference_enabled(void);
bool river_playback_service_ducked(void);
void river_playback_service_get_stats(river_playback_service_stats_t *stats);
void river_playback_service_dump_status(void);

#endif
