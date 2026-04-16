/* 小智 WebSocket 协议接口与事件结构定义。 */
#ifndef AMEBA_RIVER_XIAOZHI_WS_H
#define AMEBA_RIVER_XIAOZHI_WS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "river/river_types.h"

typedef enum {
    RIVER_XIAOZHI_EVENT_SERVER_HELLO = 0,
    RIVER_XIAOZHI_EVENT_STT = 1,
    RIVER_XIAOZHI_EVENT_LLM = 2,
    RIVER_XIAOZHI_EVENT_TTS = 3,
    RIVER_XIAOZHI_EVENT_MCP = 4,
    RIVER_XIAOZHI_EVENT_AUDIO = 5,
    RIVER_XIAOZHI_EVENT_SESSION_CLOSED = 6,
    RIVER_XIAOZHI_EVENT_ERROR = 7,
    RIVER_XIAOZHI_EVENT_INPUT_SPEECH_START = 8,
    RIVER_XIAOZHI_EVENT_INPUT_PREVIEW = 9,
    RIVER_XIAOZHI_EVENT_INPUT_ENDPOINT = 10,
    RIVER_XIAOZHI_EVENT_AUDIO_OUT_META = 11
} river_xiaozhi_event_type_t;

typedef enum {
    RIVER_XIAOZHI_BINARY_OPUS = 0,
    RIVER_XIAOZHI_BINARY_JSON = 1,
    RIVER_XIAOZHI_BINARY_PCM16 = 2
} river_xiaozhi_binary_type_t;

typedef struct {
    const char *ota_url;
    const char *url;
    const char *token;
    uint16_t protocol_version;
    bool enable_mcp;
    uint32_t uplink_sample_rate;
    uint32_t uplink_channels;
    uint32_t uplink_frame_duration_ms;
} river_xiaozhi_config_t;

typedef struct {
    river_xiaozhi_event_type_t type;
    const char *session_id;
    const char *text;
    const char *state;
    const char *emotion;
    const char *preview_id;
    const char *stable_prefix;
    const char *reason;
    const char *source;
    const char *response_id;
    const char *playback_id;
    const char *segment_id;
    uint32_t sample_rate;
    uint32_t frame_duration_ms;
    uint32_t timestamp_ms;
    uint32_t audio_offset_ms;
    uint32_t expected_duration_ms;
    const uint8_t *binary_data;
    size_t binary_bytes;
    uint16_t binary_type;
    bool candidate;
    bool is_final;
    bool is_last_segment;
} river_xiaozhi_event_t;

typedef void (*river_xiaozhi_event_handler_t)(const river_xiaozhi_event_t *event,
                                              void *user_data);

river_status_t river_xiaozhi_init(void);
river_status_t river_xiaozhi_get_config(river_xiaozhi_config_t *config);
river_status_t river_xiaozhi_set_config(const river_xiaozhi_config_t *config);
river_status_t river_xiaozhi_bootstrap(void);
river_status_t river_xiaozhi_set_event_handler(river_xiaozhi_event_handler_t handler,
                                               void *user_data);
bool river_xiaozhi_configured(void);
const char *river_xiaozhi_session_id(void);
const char *river_xiaozhi_last_text(void);
const char *river_xiaozhi_last_state(void);
const char *river_xiaozhi_last_session_state(void);
const char *river_xiaozhi_last_input_state(void);
const char *river_xiaozhi_last_output_state(void);
const char *river_xiaozhi_last_turn_id(void);
const char *river_xiaozhi_last_accept_reason(void);
bool river_xiaozhi_last_barge_in_enabled_known(void);
bool river_xiaozhi_last_barge_in_enabled(void);
void river_xiaozhi_clear_session_update_cache(void);
const char *river_xiaozhi_last_emotion(void);
const char *river_xiaozhi_last_error(void);
const char *river_xiaozhi_activation_code(void);
const char *river_xiaozhi_activation_message(void);
const char *river_xiaozhi_activation_challenge(void);
river_status_t river_xiaozhi_open_session(void);
void river_xiaozhi_close_session(void);
bool river_xiaozhi_session_open(void);
river_status_t river_xiaozhi_poll(uint32_t timeout_ms);
river_status_t river_xiaozhi_send_listen_start(const char *mode);
river_status_t river_xiaozhi_send_listen_stop(void);
river_status_t river_xiaozhi_send_listen_detect(const char *text);
river_status_t river_xiaozhi_send_abort(const char *reason);
river_status_t river_xiaozhi_send_audio_out_started(const char *response_id,
                                                    const char *playback_id,
                                                    const char *segment_id);
river_status_t river_xiaozhi_send_audio_out_mark(const char *response_id,
                                                 const char *playback_id,
                                                 const char *segment_id,
                                                 uint32_t played_duration_ms);
river_status_t river_xiaozhi_send_audio_out_cleared(const char *response_id,
                                                    const char *playback_id,
                                                    const char *cleared_after_segment_id,
                                                    const char *reason);
river_status_t river_xiaozhi_send_audio_out_completed(const char *response_id,
                                                      const char *playback_id);
river_status_t river_xiaozhi_send_audio(const uint8_t *payload,
                                        size_t bytes,
                                        uint32_t timestamp_ms);
river_status_t river_xiaozhi_send_mcp_payload(const char *payload_json);
void river_xiaozhi_dump_status(void);

#endif
