/* Orvibo realtime protocol public contract. */
#ifndef AMEBA_RIVER_ORVIBO_PROTOCOL_H
#define AMEBA_RIVER_ORVIBO_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

#include "river/river_types.h"

typedef enum {
    RIVER_ORVIBO_PROTOCOL_EVENT_CONNECTED = 0,
    RIVER_ORVIBO_PROTOCOL_EVENT_AUDIO_CHANNEL_OPENED,
    RIVER_ORVIBO_PROTOCOL_EVENT_AUDIO_CHANNEL_CLOSED,
    RIVER_ORVIBO_PROTOCOL_EVENT_SERVER_HELLO,
    RIVER_ORVIBO_PROTOCOL_EVENT_TTS_START,
    RIVER_ORVIBO_PROTOCOL_EVENT_TTS_STOP,
    RIVER_ORVIBO_PROTOCOL_EVENT_MCP_REQUEST,
    RIVER_ORVIBO_PROTOCOL_EVENT_AUDIO_PACKET,
    RIVER_ORVIBO_PROTOCOL_EVENT_ERROR
} river_orvibo_protocol_event_type_t;

typedef struct {
    const char *url;
    const char *token;
    uint16_t protocol_version;
    bool enable_mcp;
    uint32_t uplink_sample_rate;
    uint32_t uplink_channels;
    uint32_t uplink_frame_duration_ms;
} river_orvibo_protocol_config_t;

typedef struct {
    river_orvibo_protocol_event_type_t type;
    const char *session_id;
    const char *text;
    const char *state;
    const char *reason;
    const cJSON *mcp_payload;
    const uint8_t *audio_data;
    size_t audio_bytes;
    uint32_t sample_rate;
    uint32_t frame_duration_ms;
    uint32_t timestamp_ms;
} river_orvibo_protocol_event_t;

typedef void (*river_orvibo_protocol_event_handler_t)(
    const river_orvibo_protocol_event_t *event,
    void *user_data);

river_status_t river_orvibo_protocol_init(void);
river_status_t river_orvibo_protocol_get_config(river_orvibo_protocol_config_t *config);
river_status_t river_orvibo_protocol_set_config(const river_orvibo_protocol_config_t *config);
river_status_t river_orvibo_protocol_set_event_handler(
    river_orvibo_protocol_event_handler_t handler,
    void *user_data);
river_status_t river_orvibo_protocol_open_audio_channel(void);
void river_orvibo_protocol_close_audio_channel(void);
bool river_orvibo_protocol_audio_channel_open(void);
river_status_t river_orvibo_protocol_poll(uint32_t timeout_ms);
river_status_t river_orvibo_protocol_send_audio(const uint8_t *payload,
                                                size_t bytes,
                                                uint32_t timestamp_ms);
river_status_t river_orvibo_protocol_send_wake_word_detected(const char *text);
river_status_t river_orvibo_protocol_send_start_listening(const char *mode);
river_status_t river_orvibo_protocol_send_stop_listening(void);
river_status_t river_orvibo_protocol_send_abort_speaking(const char *reason);
river_status_t river_orvibo_protocol_send_mcp_message(const char *payload_json);
void river_orvibo_protocol_dump_status(void);

#endif
