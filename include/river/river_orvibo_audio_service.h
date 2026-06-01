/* Orvibo audio service public contract. */
#ifndef AMEBA_RIVER_ORVIBO_AUDIO_SERVICE_H
#define AMEBA_RIVER_ORVIBO_AUDIO_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "river/river_types.h"

typedef enum {
    RIVER_ORVIBO_AUDIO_MODE_IDLE = 0,
    RIVER_ORVIBO_AUDIO_MODE_LISTENING,
    RIVER_ORVIBO_AUDIO_MODE_SPEAKING
} river_orvibo_audio_mode_t;

typedef enum {
    RIVER_ORVIBO_AUDIO_EVENT_WAKE_DETECTED = 0,
    RIVER_ORVIBO_AUDIO_EVENT_SPEECH_STARTED,
    RIVER_ORVIBO_AUDIO_EVENT_SPEECH_ENDED,
    RIVER_ORVIBO_AUDIO_EVENT_UPLINK_PACKET,
    RIVER_ORVIBO_AUDIO_EVENT_PLAYBACK_STARTED,
    RIVER_ORVIBO_AUDIO_EVENT_PLAYBACK_FINISHED,
    RIVER_ORVIBO_AUDIO_EVENT_ERROR
} river_orvibo_audio_event_type_t;

typedef struct {
    river_orvibo_audio_event_type_t type;
    const char *text;
    int confidence_q15;
    const uint8_t *packet;
    size_t packet_bytes;
    uint32_t timestamp_ms;
    uint16_t vad_probability_q15;
    uint16_t vad_probability_raw_q15;
    bool is_speech;
} river_orvibo_audio_event_t;

typedef void (*river_orvibo_audio_event_handler_t)(
    const river_orvibo_audio_event_t *event,
    void *user_data);

river_status_t river_orvibo_audio_service_init(void);
river_status_t river_orvibo_audio_service_set_event_handler(
    river_orvibo_audio_event_handler_t handler,
    void *user_data);
river_status_t river_orvibo_audio_service_start(void);
river_status_t river_orvibo_audio_service_set_mode(river_orvibo_audio_mode_t mode);
void river_orvibo_audio_service_set_barge_in_enabled(bool enabled);
river_orvibo_audio_mode_t river_orvibo_audio_service_mode(void);
river_status_t river_orvibo_audio_service_handle_downlink(
    const uint8_t *packet,
    size_t packet_bytes,
    uint32_t sample_rate,
    uint32_t channels,
    uint32_t frame_duration_ms);
void river_orvibo_audio_service_prepare_tts_playback(void);
river_status_t river_orvibo_audio_service_wait_playback_idle(uint32_t timeout_ms);
void river_orvibo_audio_service_stop_playback(const char *reason);
void river_orvibo_audio_service_dump_status(void);
const char *river_orvibo_audio_mode_name(river_orvibo_audio_mode_t mode);

#endif
