/* 云端适配层公共接口：统一抽象在线 ASR、TTS 与会话状态。 */
#ifndef AMEBA_RIVER_CLOUD_H
#define AMEBA_RIVER_CLOUD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "river/river_types.h"
#include "river/river_voice_segment_sink.h"
#include "river/river_xiaozhi_ws.h"

#if defined(CONFIG_RIVER_CLOUD_BACKEND_XIAOZHI_REALTIME)
#define RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED 1
#else
#define RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED 0
#endif

#if defined(CONFIG_RIVER_CLOUD_BACKEND_IFLYTEK_SPLIT)
#define RIVER_CLOUD_BACKEND_IFLYTEK_ENABLED 1
#else
#define RIVER_CLOUD_BACKEND_IFLYTEK_ENABLED 0
#endif

typedef enum {
    RIVER_CLOUD_ASR_EVENT_PARTIAL = 0,
    RIVER_CLOUD_ASR_EVENT_FINAL = 1,
    RIVER_CLOUD_ASR_EVENT_ERROR = 2,
    RIVER_CLOUD_ASR_EVENT_SESSION_STARTED = 3,
    RIVER_CLOUD_ASR_EVENT_SESSION_CLOSED = 4
} river_cloud_asr_event_type_t;

typedef struct {
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t bits_per_sample;
    uint32_t frame_ms;
    const char *encoding;
} river_cloud_asr_audio_desc_t;

typedef struct {
    river_cloud_asr_event_type_t type;
    const char *provider_name;
    const char *text;
    const char *sid;
    const char *message;
    uint32_t sequence;
    int code;
    bool is_final;
} river_cloud_asr_result_t;

typedef void (*river_cloud_asr_result_handler_t)(const river_cloud_asr_result_t *result,
                                                 void *user_data);
typedef void (*river_cloud_state_sync_handler_t)(const char *reason,
                                                 void *user_data);

#define RIVER_CLOUD_RUNTIME_PROVIDER_MAX 32U
#define RIVER_CLOUD_RUNTIME_ID_MAX       96U
#define RIVER_CLOUD_RUNTIME_REASON_MAX   64U
#define RIVER_CLOUD_RUNTIME_STATE_MAX    32U

typedef enum {
    RIVER_CLOUD_PLAYBACK_PHASE_IDLE = 0,
    RIVER_CLOUD_PLAYBACK_PHASE_PREFETCHING,
    RIVER_CLOUD_PLAYBACK_PHASE_WAITING_SEGMENT,
    RIVER_CLOUD_PLAYBACK_PHASE_PLAYING,
    RIVER_CLOUD_PLAYBACK_PHASE_REBUFFERING,
    RIVER_CLOUD_PLAYBACK_PHASE_DRAINING
} river_cloud_playback_phase_t;

typedef enum {
    RIVER_CLOUD_PLAYBACK_BACKEND_DETACHED = 0,
    RIVER_CLOUD_PLAYBACK_BACKEND_OWNED_ACTIVE,
    RIVER_CLOUD_PLAYBACK_BACKEND_FOREIGN_ACTIVE,
    RIVER_CLOUD_PLAYBACK_BACKEND_RESTART_PENDING
} river_cloud_playback_backend_state_t;

typedef enum {
    RIVER_CLOUD_PLAYBACK_TERMINAL_WAIT_NONE = 0,
    RIVER_CLOUD_PLAYBACK_TERMINAL_WAIT_LAST_SEGMENT_META,
    RIVER_CLOUD_PLAYBACK_TERMINAL_WAIT_QUEUE_DRAIN,
    RIVER_CLOUD_PLAYBACK_TERMINAL_WAIT_LAST_SEGMENT_TAIL
} river_cloud_playback_terminal_wait_kind_t;

typedef enum {
    RIVER_CLOUD_PLAYBACK_TERMINAL_STATE_NONE = 0,
    RIVER_CLOUD_PLAYBACK_TERMINAL_STATE_COMPLETED,
    RIVER_CLOUD_PLAYBACK_TERMINAL_STATE_CLEARED,
    RIVER_CLOUD_PLAYBACK_TERMINAL_STATE_LOCAL_COMPLETED,
    RIVER_CLOUD_PLAYBACK_TERMINAL_STATE_LOCAL_CLEARED
} river_cloud_playback_terminal_state_t;

typedef enum {
    RIVER_CLOUD_PLAYBACK_REBUFFER_CAUSE_NONE = 0,
    RIVER_CLOUD_PLAYBACK_REBUFFER_CAUSE_UPSTREAM_STARVED,
    RIVER_CLOUD_PLAYBACK_REBUFFER_CAUSE_WRITE_FAILED
} river_cloud_playback_rebuffer_cause_t;

typedef enum {
    RIVER_CLOUD_PLAYBACK_START_POLICY_BASELINE = 0,
    RIVER_CLOUD_PLAYBACK_START_POLICY_REBUFFER_FAST,
    RIVER_CLOUD_PLAYBACK_START_POLICY_PREFETCH_SEGMENT,
    RIVER_CLOUD_PLAYBACK_START_POLICY_PREFETCH_STARVED
} river_cloud_playback_start_policy_t;

typedef struct {
    bool available;
    bool conversation_window_active;
    bool listening;
    bool stream_active;
    bool listen_stop_pending;
    bool local_close_pending;
    bool playback_active;
    bool playback_lane_engaged;
    bool playback_rebuffer_pending;
    bool playback_phase_known;
    bool playback_terminal_closed;
    bool playback_terminal_waiting;
    river_cloud_playback_phase_t playback_phase_kind;
    river_cloud_playback_backend_state_t playback_backend_state_kind;
    river_cloud_playback_terminal_wait_kind_t playback_terminal_wait_kind;
    river_cloud_playback_terminal_state_t playback_terminal_state_kind;
    river_cloud_playback_rebuffer_cause_t playback_rebuffer_cause_kind;
    river_cloud_playback_start_policy_t playback_start_policy_kind;
    bool tts_stop_pending;
    bool turn_accepted;
    bool barge_in_enabled_known;
    bool barge_in_enabled;
    char provider_name[RIVER_CLOUD_RUNTIME_PROVIDER_MAX];
    char session_id[RIVER_CLOUD_RUNTIME_ID_MAX];
    char turn_id[RIVER_CLOUD_RUNTIME_ID_MAX];
    char accept_reason[RIVER_CLOUD_RUNTIME_REASON_MAX];
    char playback_phase[RIVER_CLOUD_RUNTIME_STATE_MAX];
    char playback_rebuffer_cause[RIVER_CLOUD_RUNTIME_REASON_MAX];
    char playback_start_policy[RIVER_CLOUD_RUNTIME_REASON_MAX];
    char playback_terminal_state[RIVER_CLOUD_RUNTIME_STATE_MAX];
    char playback_terminal_reason[RIVER_CLOUD_RUNTIME_REASON_MAX];
    char playback_terminal_wait_reason[RIVER_CLOUD_RUNTIME_REASON_MAX];
    char input_state[RIVER_CLOUD_RUNTIME_STATE_MAX];
    char output_state[RIVER_CLOUD_RUNTIME_STATE_MAX];
    uint32_t conversation_window_remaining_ms;
    uint32_t local_close_remaining_ms;
    uint32_t playback_start_frames;
    uint32_t playback_prefetch_frames;
    bool playback_start_cautious_history;
} river_cloud_runtime_snapshot_t;

river_status_t river_cloud_adapter_init(void);
river_status_t river_cloud_adapter_set_result_handler(river_cloud_asr_result_handler_t handler,
                                                      void *user_data);
river_status_t river_cloud_adapter_set_state_sync_handler(
    river_cloud_state_sync_handler_t handler,
    void *user_data);
river_status_t river_cloud_adapter_set_xiaozhi_config(const river_xiaozhi_config_t *config);
void river_cloud_adapter_notify_network_ready(void);
void river_cloud_adapter_notify_network_lost(void);
river_status_t river_cloud_adapter_submit_text(const char *text);
river_status_t river_cloud_adapter_interrupt_tts_with_reason(const char *reason);
river_status_t river_cloud_adapter_interrupt_tts(void);
river_status_t river_cloud_adapter_begin_conversation_window(const char *source);
bool river_cloud_adapter_conversation_window_active(void);
bool river_cloud_utc_ready(void);
const char *river_cloud_asr_provider_name(void);
bool river_cloud_asr_streaming_supported(void);
bool river_cloud_asr_batch_supported(void);
river_status_t river_cloud_asr_audio_open(const river_cloud_asr_audio_desc_t *audio,
                                          uint32_t pre_roll_ms,
                                          uint32_t post_roll_ms);
void river_cloud_asr_audio_close(void);
river_status_t river_cloud_asr_stream_push_frame(const uint8_t *pcm,
                                                 size_t bytes,
                                                 bool is_speech);
river_status_t river_cloud_asr_batch_submit_segment(const uint8_t *pcm,
                                                    size_t bytes,
                                                    const river_voice_segment_desc_t *segment);
const char *river_cloud_playback_phase_name(river_cloud_playback_phase_t phase);
const char *river_cloud_playback_backend_state_name(
    river_cloud_playback_backend_state_t state);
const char *river_cloud_playback_rebuffer_cause_name(
    river_cloud_playback_rebuffer_cause_t cause);
const char *river_cloud_playback_terminal_state_name(
    river_cloud_playback_terminal_state_t state);
const char *river_cloud_playback_start_policy_name(
    river_cloud_playback_start_policy_t policy);
river_status_t river_cloud_adapter_get_runtime_snapshot(river_cloud_runtime_snapshot_t *snapshot);
void river_cloud_adapter_dump_status(void);

#endif
