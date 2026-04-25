/* 云端模块内部共享状态、常量与上下文定义。 */
#ifndef RIVER_CLOUD_INTERNAL_H
#define RIVER_CLOUD_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "os_wrapper.h"

#include "river/river_audio_frame_ring.h"
#include "river/river_cloud.h"
#include "river/river_opus_codec.h"
#include "river/river_voice_runtime_policy.h"
#include "river/river_xiaozhi_credentials.h"
#include "river/river_xiaozhi_ws.h"
#include "river_asr_provider_internal.h"

#if RIVER_CLOUD_BACKEND_IFLYTEK_ENABLED
#define RIVER_CLOUD_BUSINESS_TIME_WAIT_REQUIRED 1
#else
#define RIVER_CLOUD_BUSINESS_TIME_WAIT_REQUIRED 0
#endif

#define RIVER_CLOUD_DEFAULT_SNTP_SERVER      "pool.ntp.org"
#define RIVER_CLOUD_SNTP_UPDATE_INTERVAL_MS  (60U * 60U * 1000U)
#define RIVER_CLOUD_TIME_READY_EPOCH_MIN     1700000000UL
#define RIVER_CLOUD_BUILD_TZ_OFFSET_SECONDS  (8L * 60L * 60L)
#define RIVER_CLOUD_STREAM_MIN_ACTIVE_MS     1200U
#define RIVER_CLOUD_XIAOZHI_PROVIDER_NAME    "xiaozhi_realtime"
#define RIVER_CLOUD_XIAOZHI_TEXT_MAX         256U
#define RIVER_CLOUD_XIAOZHI_SESSION_ID_MAX   96U
#define RIVER_CLOUD_XIAOZHI_PREVIEW_ID_MAX   64U
#define RIVER_CLOUD_XIAOZHI_PREVIEW_SOURCE_MAX 32U
#define RIVER_CLOUD_XIAOZHI_PREVIEW_REASON_MAX 64U
#define RIVER_CLOUD_XIAOZHI_TURN_ID_MAX      96U
#define RIVER_CLOUD_XIAOZHI_ACCEPT_REASON_MAX 64U
#define RIVER_CLOUD_XIAOZHI_LANE_STATE_MAX   32U
#define RIVER_CLOUD_XIAOZHI_FALLBACK_REASON_MAX 64U
#define RIVER_CLOUD_XIAOZHI_TERMINAL_ACK_MAX 16U
#define RIVER_CLOUD_XIAOZHI_PLAYBACK_CLEAR_REASON_MAX 48U
#define RIVER_CLOUD_XIAOZHI_PLAYBACK_SEGMENTS_MAX 8U
#define RIVER_CLOUD_XIAOZHI_RESPONSE_ID_MAX  96U
#define RIVER_CLOUD_XIAOZHI_PLAYBACK_ID_MAX  96U
#define RIVER_CLOUD_XIAOZHI_SEGMENT_ID_MAX   96U
#define RIVER_CLOUD_XIAOZHI_UPLINK_PACKET_MAX \
    ((RIVER_XIAOZHI_UPLINK_SAMPLE_RATE * RIVER_XIAOZHI_UPLINK_CHANNELS * \
      sizeof(int16_t) * RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS) / 1000U)
#define RIVER_CLOUD_XIAOZHI_UPLINK_ACCUM_MAX 4096U
#define RIVER_CLOUD_XIAOZHI_DOWNLINK_PCM_SAMPLES_MAX 2048U
#define RIVER_CLOUD_XIAOZHI_DOWNLINK_PCM_BYTES_MAX \
    (((24000U * 60U) / 1000U) * sizeof(int16_t))
#define RIVER_CLOUD_XIAOZHI_PLAYBACK_DRAIN_MS 500U
#define RIVER_CLOUD_XIAOZHI_TTS_STREAM_NAME  "xiaozhi_tts"
#define RIVER_CLOUD_XIAOZHI_IO_TASK_STACK    (1024U * 48U)
#define RIVER_CLOUD_XIAOZHI_IO_TASK_PRIO     4U
#define RIVER_CLOUD_XIAOZHI_IO_ACTIVE_MS     5U
#define RIVER_CLOUD_XIAOZHI_IO_FAIRNESS_DELAY_MS 1U
#define RIVER_CLOUD_XIAOZHI_IO_IDLE_MS       20U
#define RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_STACK (1024U * 16U)
#define RIVER_CLOUD_XIAOZHI_DOWNLINK_TASK_PRIO  4U
#define RIVER_CLOUD_XIAOZHI_DOWNLINK_POLL_MS    5U
#define RIVER_CLOUD_XIAOZHI_DOWNLINK_IDLE_MS    20U
/* Favor continuity over lowest latency while the service often stays on no-ref playback. */
#define RIVER_CLOUD_XIAOZHI_DOWNLINK_RING_FRAMES 96U
#define RIVER_CLOUD_XIAOZHI_DOWNLINK_START_FRAMES 16U
#define RIVER_CLOUD_XIAOZHI_DOWNLINK_REBUFFER_START_FRAMES 28U
#define RIVER_CLOUD_XIAOZHI_DOWNLINK_STARVED_REBUFFER_MS 120U
#define RIVER_CLOUD_XIAOZHI_DOWNLINK_PREFETCH_MARGIN_MS 120U
#define RIVER_CLOUD_XIAOZHI_DOWNLINK_REBUFFER_EXTRA_FRAMES 4U
#define RIVER_CLOUD_XIAOZHI_PLAYBACK_BUFFER_FRAMES 12U
#define RIVER_CLOUD_XIAOZHI_PLAYBACK_BUFFER_FRAMES_FALLBACK 8U
#define RIVER_CLOUD_XIAOZHI_PLAYBACK_REF_HISTORY_MS 320U
#define RIVER_CLOUD_XIAOZHI_UPLINK_POLL_MS    5U
#define RIVER_CLOUD_XIAOZHI_UPLINK_RING_FRAMES 64U
#define RIVER_CLOUD_XIAOZHI_UPLINK_STALE_FRAMES_MAX 6U
#define RIVER_CLOUD_XIAOZHI_UPLINK_DRAIN_BURST_MAX 4U
#define RIVER_CLOUD_XIAOZHI_UPLINK_BUSY_BACKOFF_MAX_MS 160U
#define RIVER_CLOUD_XIAOZHI_UPLINK_BUSY_LOG_INTERVAL_MS 1000U
#define RIVER_CLOUD_XIAOZHI_CONTROL_QUEUE_DEPTH 8U
#define RIVER_CLOUD_XIAOZHI_CONTROL_WAIT_MS 0xFFFFFFFFU
#define RIVER_CLOUD_XIAOZHI_CONTROL_ARG_MAX 64U
#define RIVER_CLOUD_XIAOZHI_PRE_ROLL_MAX_MS    128U
#define RIVER_CLOUD_XIAOZHI_OPEN_HOLD_FRAMES   2U
#define RIVER_CLOUD_XIAOZHI_NOREF_OPEN_HOLD_FRAMES 6U
#define RIVER_CLOUD_XIAOZHI_NOREF_REOPEN_GUARD_MS 480U
#define RIVER_CLOUD_XIAOZHI_NOREF_REARM_SILENCE_FRAMES 6U
#define RIVER_CLOUD_XIAOZHI_WAKE_WINDOW_FOLLOWUP_MS 8000U
#define RIVER_CLOUD_XIAOZHI_POST_TTS_SILENCE_CLOSE_MS 3000U
#define RIVER_CLOUD_XIAOZHI_POST_COMMIT_RESPONSE_WAIT_MS 6000U
#define RIVER_CLOUD_XIAOZHI_LOCAL_CLOSE_DEFER_MS 2000U
#define RIVER_CLOUD_XIAOZHI_ENDPOINT_SOFT_CLOSE_DEFER_MS 320U
#define RIVER_CLOUD_XIAOZHI_PLAYBACK_MARK_INTERVAL_MS 80U
#define RIVER_CLOUD_XIAOZHI_UPLINK_PCM_FRAME_MAX \
    ((RIVER_XIAOZHI_UPLINK_SAMPLE_RATE * RIVER_XIAOZHI_UPLINK_CHANNELS * \
      sizeof(int16_t) * RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS) / 1000U)

typedef enum {
    RIVER_CLOUD_XIAOZHI_CTRL_OPEN_AND_LISTEN = 0,
    RIVER_CLOUD_XIAOZHI_CTRL_LISTEN_STOP = 1,
    RIVER_CLOUD_XIAOZHI_CTRL_ABORT = 2,
    RIVER_CLOUD_XIAOZHI_CTRL_CLOSE_SESSION = 3,
    RIVER_CLOUD_XIAOZHI_CTRL_PLAYBACK_STARTED = 4,
    RIVER_CLOUD_XIAOZHI_CTRL_PLAYBACK_COMPLETED = 5,
    RIVER_CLOUD_XIAOZHI_CTRL_PLAYBACK_MARK = 6,
    RIVER_CLOUD_XIAOZHI_CTRL_PLAYBACK_CLEARED = 7
} river_cloud_xiaozhi_control_op_t;

typedef enum {
    RIVER_CLOUD_XIAOZHI_PLAYBACK_ABORT_INTERRUPT = 0,
    RIVER_CLOUD_XIAOZHI_PLAYBACK_ABORT_TRANSPORT_CLOSED = 1,
    RIVER_CLOUD_XIAOZHI_PLAYBACK_ABORT_NETWORK_LOST = 2,
    RIVER_CLOUD_XIAOZHI_PLAYBACK_ABORT_BRIDGE_CLOSE = 3,
    RIVER_CLOUD_XIAOZHI_PLAYBACK_ABORT_FRAME_OVERSIZE = 4
} river_cloud_xiaozhi_playback_abort_cause_t;

typedef enum {
    RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_LOCAL_RESOLVED = 0,
    RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_SERVER_RESPONSE = 1
} river_cloud_xiaozhi_round_close_cause_t;

typedef enum {
    RIVER_CLOUD_XIAOZHI_STREAM_FINISH_POST_ROLL = 0,
    RIVER_CLOUD_XIAOZHI_STREAM_FINISH_ENDPOINT_TIMEOUT = 1
} river_cloud_xiaozhi_stream_finish_cause_t;

typedef enum {
    RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_NONE = 0,
    RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_INACTIVE = 1,
    RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_NOT_READY = 2,
    RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_ACQUIRE_MISS = 3,
    RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_WRITE_FAILED = 4,
    RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_STARVED = 5,
    RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_SEGMENT_GAP = 6,
    RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_EMPTY = 7,
    RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_PLAYBACK_NOT_READY = 8,
    RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_STEP_POLICY = 9,
    RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_REBUFFER_WAIT = 10,
    RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_STOP_PENDING = 11,
    RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_PAUSED_RESUME_WAIT = 12,
    RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_BACKEND_RECOVERING = 13,
    RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_START_THRESHOLD = 14,
    RIVER_CLOUD_XIAOZHI_DOWNLINK_WAIT_PLAYBACK_START_FAILED = 15
} river_cloud_xiaozhi_downlink_wait_kind_t;

typedef enum {
    RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_NONE = 0,
    RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_INACTIVE = 1,
    RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_NOT_READY = 2,
    RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_ACQUIRE_MISS = 3,
    RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_WRITE_OK = 4,
    RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_WRITE_RECOVERED = 5,
    RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_WRITE_FAILED = 6,
    RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_ABORTED = 7,
    RIVER_CLOUD_XIAOZHI_DOWNLINK_CYCLE_STEP_POLICY = 8
} river_cloud_xiaozhi_downlink_cycle_outcome_t;

typedef struct {
    bool valid;
    bool started;
    bool started_ack_reported;
    bool is_last_segment;
    bool rebuffered;
    uint64_t started_at_ms;
    uint64_t paused_at_ms;
    uint32_t expected_duration_ms;
    uint32_t last_mark_ms;
    char response_id[RIVER_CLOUD_XIAOZHI_RESPONSE_ID_MAX];
    char playback_id[RIVER_CLOUD_XIAOZHI_PLAYBACK_ID_MAX];
    char segment_id[RIVER_CLOUD_XIAOZHI_SEGMENT_ID_MAX];
    char text[RIVER_CLOUD_XIAOZHI_TEXT_MAX];
} river_cloud_xiaozhi_playback_segment_t;

typedef struct {
    uint32_t head;
    uint32_t count;
    river_cloud_xiaozhi_playback_segment_t
        segments[RIVER_CLOUD_XIAOZHI_PLAYBACK_SEGMENTS_MAX];
} river_cloud_xiaozhi_playback_segment_queue_truth_t;

typedef struct {
    river_cloud_xiaozhi_control_op_t op;
    char arg[RIVER_CLOUD_XIAOZHI_CONTROL_ARG_MAX];
    char response_id[RIVER_CLOUD_XIAOZHI_RESPONSE_ID_MAX];
    char playback_id[RIVER_CLOUD_XIAOZHI_PLAYBACK_ID_MAX];
    char segment_id[RIVER_CLOUD_XIAOZHI_SEGMENT_ID_MAX];
    uint32_t played_duration_ms;
    rtos_sema_t completion;
    river_status_t *result_out;
} river_cloud_xiaozhi_control_request_t;

typedef struct {
    uint32_t read_index;
    uint32_t write_index;
    uint32_t count;
    uint32_t high_watermark;
} river_cloud_xiaozhi_control_queue_truth_t;

typedef struct {
    char response_id[RIVER_CLOUD_XIAOZHI_RESPONSE_ID_MAX];
    char playback_id[RIVER_CLOUD_XIAOZHI_PLAYBACK_ID_MAX];
    char segment_id[RIVER_CLOUD_XIAOZHI_SEGMENT_ID_MAX];
} river_cloud_xiaozhi_playback_context_truth_t;

typedef enum {
    RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_NONE = 0,
    RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_RESPONSE_STARTED = 1,
    RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_META_OBSERVED = 2,
    RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_SEGMENT_STARTED = 3,
    RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_MARK_QUEUED = 4,
    RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_CLEARED_QUEUED = 5,
    RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_COMPLETED_QUEUED = 6,
    RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_LOCAL_CLEARED = 7,
    RIVER_CLOUD_XIAOZHI_PLAYBACK_LINEAGE_LOCAL_COMPLETED = 8
} river_cloud_xiaozhi_playback_lineage_stage_t;

typedef struct {
    river_cloud_xiaozhi_playback_lineage_stage_t stage;
    river_cloud_xiaozhi_playback_context_truth_t response_context;
    river_cloud_xiaozhi_playback_context_truth_t meta_context;
    river_cloud_xiaozhi_playback_context_truth_t started_context;
    river_cloud_xiaozhi_playback_context_truth_t marked_context;
    river_cloud_xiaozhi_playback_context_truth_t cleared_context;
    river_cloud_xiaozhi_playback_context_truth_t completed_context;
    river_cloud_xiaozhi_playback_context_truth_t last_segment_context;
    river_cloud_xiaozhi_playback_context_truth_t fully_heard_context;
    uint32_t last_mark_ms;
    uint64_t updated_ms;
    char reason[RIVER_CLOUD_XIAOZHI_PLAYBACK_CLEAR_REASON_MAX];
} river_cloud_xiaozhi_playback_lineage_truth_t;

typedef struct {
    river_cloud_xiaozhi_playback_context_truth_t current_context;
    char text[RIVER_CLOUD_XIAOZHI_TEXT_MAX];
    uint32_t expected_duration_ms;
    uint32_t last_meta_gap_ms;
    uint32_t prefetch_target_ms;
    uint64_t last_meta_ms;
} river_cloud_xiaozhi_playback_meta_truth_t;

typedef struct {
    bool started_reported;
    bool cleared_reported;
    bool completed_reported;
    bool waiting;
    river_cloud_playback_terminal_wait_kind_t wait_kind;
    bool duplex_ready_seen;
    char last_started_segment_id[RIVER_CLOUD_XIAOZHI_SEGMENT_ID_MAX];
    char ack[RIVER_CLOUD_XIAOZHI_TERMINAL_ACK_MAX];
    river_cloud_playback_terminal_state_t state_kind;
    char clear_reason[RIVER_CLOUD_XIAOZHI_PLAYBACK_CLEAR_REASON_MAX];
    char wait_reason[RIVER_CLOUD_XIAOZHI_PLAYBACK_CLEAR_REASON_MAX];
    river_cloud_xiaozhi_playback_context_truth_t wait_context;
    river_cloud_xiaozhi_playback_context_truth_t last_segment_context;
    river_cloud_xiaozhi_playback_context_truth_t last_fully_heard_context;
} river_cloud_xiaozhi_playback_terminal_truth_t;

typedef struct {
    river_cloud_playback_phase_t phase;
    river_cloud_playback_rebuffer_cause_t rebuffer_cause;
    river_cloud_playback_recovery_path_t recovery_path;
    river_cloud_playback_recovery_outcome_t recovery_outcome;
    uint32_t rebuffer_count;
    uint32_t rebuffer_streak;
    bool active;
    bool rebuffer_pending;
    bool stop_pending;
    uint64_t tts_stop_deadline_ms;
} river_cloud_xiaozhi_playback_runtime_truth_t;

typedef struct {
    river_cloud_playback_start_policy_t policy;
    uint32_t start_frames;
    uint32_t prefetch_frames;
    uint32_t buffer_frames;
    bool cautious_history;
    uint64_t no_ref_reopen_guard_deadline_ms;
    uint32_t no_ref_reopen_silence_frames;
    bool no_ref_reopen_rearm;
} river_cloud_xiaozhi_playback_gate_truth_t;

typedef struct {
    uint32_t ring_dropped;
    uint64_t starved_since_ms;
    uint64_t last_supply_ms;
    uint32_t last_wait_delay_ms;
    river_status_t last_cycle_status;
    river_cloud_xiaozhi_downlink_wait_kind_t last_wait_kind;
    river_cloud_xiaozhi_downlink_cycle_outcome_t last_cycle_outcome;
    bool retry_valid;
} river_cloud_xiaozhi_downlink_runtime_truth_t;

typedef struct {
    uint32_t sample_rate;
    uint32_t frame_duration_ms;
    bool worker_started;
} river_cloud_xiaozhi_downlink_stream_truth_t;

typedef struct {
    uint32_t sample_rate;
    uint32_t frame_duration_ms;
} river_cloud_xiaozhi_server_audio_format_truth_t;

typedef struct {
    uint32_t open_speech_frames;
    uint32_t timestamp_ms;
    uint32_t ring_dropped;
    uint32_t busy_count;
    uint32_t fail_count;
    uint32_t stale_dropped;
    uint32_t busy_streak;
    uint64_t next_send_ms;
    uint64_t last_busy_log_ms;
    size_t accum_bytes;
    bool retry_valid;
    bool io_started;
} river_cloud_xiaozhi_uplink_runtime_truth_t;

typedef struct {
    uint32_t id;
    bool active;
    uint32_t started_ms;
    uint32_t first_packet_ms;
    uint32_t pre_roll_frames;
    uint32_t packets_sent;
    uint32_t partial_count;
    uint32_t final_count;
    bool partial_seen;
    bool final_seen;
    uint32_t busy_base;
    uint32_t fail_base;
    uint32_t stale_drop_base;
    uint32_t ring_drop_base;
    char close_reason[32];
} river_cloud_xiaozhi_asr_round_truth_t;

typedef struct {
    bool pending;
    uint64_t deadline_ms;
    char reason[RIVER_CLOUD_XIAOZHI_PREVIEW_REASON_MAX];
} river_cloud_xiaozhi_endpoint_soft_close_truth_t;

typedef struct {
    bool valid;
    bool finalized;
    char text[RIVER_CLOUD_XIAOZHI_TEXT_MAX];
} river_cloud_xiaozhi_pending_transcript_truth_t;

typedef struct {
    bool speech_started;
    bool endpoint_candidate;
    bool is_final;
    uint32_t audio_offset_ms;
    char preview_id[RIVER_CLOUD_XIAOZHI_PREVIEW_ID_MAX];
    char text[RIVER_CLOUD_XIAOZHI_TEXT_MAX];
    char stable_prefix[RIVER_CLOUD_XIAOZHI_TEXT_MAX];
    char source[RIVER_CLOUD_XIAOZHI_PREVIEW_SOURCE_MAX];
    char endpoint_reason[RIVER_CLOUD_XIAOZHI_PREVIEW_REASON_MAX];
} river_cloud_xiaozhi_preview_transcript_truth_t;

typedef struct {
    bool listening;
    bool window_active;
    bool listen_stop_pending;
    bool local_close_pending;
    uint64_t window_deadline_ms;
    uint64_t local_close_deadline_ms;
} river_cloud_xiaozhi_session_window_truth_t;

typedef struct {
    char session_id[RIVER_CLOUD_XIAOZHI_SESSION_ID_MAX];
    bool accepted;
    bool barge_in_enabled_known;
    bool barge_in_enabled;
    char turn_id[RIVER_CLOUD_XIAOZHI_TURN_ID_MAX];
    char accept_reason[RIVER_CLOUD_XIAOZHI_ACCEPT_REASON_MAX];
    char input_state[RIVER_CLOUD_XIAOZHI_LANE_STATE_MAX];
    char output_state[RIVER_CLOUD_XIAOZHI_LANE_STATE_MAX];
    char fallback_reason[RIVER_CLOUD_XIAOZHI_FALLBACK_REASON_MAX];
} river_cloud_xiaozhi_turn_semantics_state_t;

typedef struct {
    bool accepted;
    bool barge_in_enabled_known;
    bool barge_in_enabled;
    const char *session_id;
    const char *turn_id;
    const char *accept_reason;
    const char *input_state;
    const char *output_state;
    const char *fallback_reason;
} river_cloud_xiaozhi_turn_semantics_view_t;

typedef struct {
    bool initialized;
    bool sntp_started;
    bool audio_bridge_open;
    bool stream_active;
    bool xiaozhi_enabled;
    river_cloud_asr_result_handler_t result_handler;
    void *result_handler_user;
    river_cloud_state_sync_handler_t state_sync_handler;
    void *state_sync_handler_user;
    const river_cloud_asr_provider_ops_t *provider;
    river_cloud_asr_audio_desc_t audio_desc;
    uint8_t *pre_roll_buffer;
    uint32_t pre_roll_capacity_frames;
    uint32_t pre_roll_count_frames;
    uint32_t pre_roll_write_index_frames;
    uint32_t post_roll_frames;
    uint32_t silence_frames;
    size_t frame_bytes;
    uint32_t stream_open_ok;
    uint32_t stream_open_fail;
    uint32_t stream_feed_ok;
    uint32_t stream_feed_fail;
    uint32_t stream_close_ok;
    uint32_t stream_close_fail;
    uint32_t batch_submit_ok;
    uint32_t batch_submit_fail;
    uint32_t batch_submit_unsupported;
    uint32_t partial_results;
    uint32_t final_results;
    uint32_t error_results;
    bool time_ready_announced;
    bool wake_admission_estimate_announced;
    bool time_seeded_from_build;
    int stream_open_defer_status;
    bool stream_open_defer_wifi_connected;
    bool stream_open_defer_time_ready;
    int wake_admission_defer_status;
    bool wake_admission_defer_wifi_connected;
    bool wake_admission_defer_time_ready;
    uint32_t seeded_utc_epoch;
    uint64_t seeded_utc_rtos_ms;
    uint32_t stream_started_ms;
#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
    river_cloud_xiaozhi_session_window_truth_t xiaozhi_session_window_truth;
    river_cloud_xiaozhi_turn_semantics_state_t xiaozhi_turn_semantics;
    river_cloud_xiaozhi_playback_lineage_truth_t xiaozhi_playback_lineage_truth;
    river_cloud_xiaozhi_playback_meta_truth_t xiaozhi_playback_meta_truth;
    river_cloud_xiaozhi_playback_terminal_truth_t xiaozhi_playback_terminal_truth;
    river_cloud_xiaozhi_playback_runtime_truth_t xiaozhi_playback_runtime_truth;
    river_cloud_xiaozhi_playback_gate_truth_t xiaozhi_playback_gate_truth;
    river_cloud_xiaozhi_playback_segment_queue_truth_t
        xiaozhi_playback_segment_queue_truth;
    river_cloud_xiaozhi_downlink_runtime_truth_t xiaozhi_downlink_runtime_truth;
    river_cloud_xiaozhi_downlink_stream_truth_t xiaozhi_downlink_stream_truth;
    river_cloud_xiaozhi_server_audio_format_truth_t
        xiaozhi_server_audio_format_truth;
    river_cloud_xiaozhi_uplink_runtime_truth_t xiaozhi_uplink_runtime_truth;
    river_cloud_xiaozhi_asr_round_truth_t xiaozhi_asr_round_truth;
    river_cloud_xiaozhi_endpoint_soft_close_truth_t
        xiaozhi_endpoint_soft_close_truth;
    river_cloud_xiaozhi_pending_transcript_truth_t
        xiaozhi_pending_transcript_truth;
    river_cloud_xiaozhi_preview_transcript_truth_t
        xiaozhi_preview_transcript_truth;
    river_cloud_xiaozhi_control_queue_truth_t xiaozhi_control_queue_truth;
    rtos_mutex_t xiaozhi_control_lock;
    rtos_sema_t xiaozhi_control_ready;
    rtos_sema_t xiaozhi_control_space;
    rtos_task_t xiaozhi_io_task;
    rtos_task_t xiaozhi_downlink_task;
    river_audio_frame_ring_t xiaozhi_downlink_ring;
    river_audio_frame_ring_t xiaozhi_uplink_ring;
    river_opus_encoder_t xiaozhi_encoder;
    river_opus_decoder_t xiaozhi_decoder;
    uint8_t xiaozhi_uplink_accum[RIVER_CLOUD_XIAOZHI_UPLINK_ACCUM_MAX];
    uint8_t xiaozhi_uplink_ring_storage[RIVER_CLOUD_XIAOZHI_UPLINK_PCM_FRAME_MAX *
                                        RIVER_CLOUD_XIAOZHI_UPLINK_RING_FRAMES];
    uint8_t xiaozhi_uplink_task_frame[RIVER_CLOUD_XIAOZHI_UPLINK_PCM_FRAME_MAX];
    uint8_t xiaozhi_uplink_drop_frame[RIVER_CLOUD_XIAOZHI_UPLINK_PCM_FRAME_MAX];
    uint8_t xiaozhi_uplink_packet[RIVER_CLOUD_XIAOZHI_UPLINK_PACKET_MAX];
    uint8_t xiaozhi_downlink_ring_storage[RIVER_CLOUD_XIAOZHI_DOWNLINK_PCM_BYTES_MAX *
                                          RIVER_CLOUD_XIAOZHI_DOWNLINK_RING_FRAMES];
    uint8_t xiaozhi_downlink_task_frame[RIVER_CLOUD_XIAOZHI_DOWNLINK_PCM_BYTES_MAX];
    uint8_t xiaozhi_downlink_drop_frame[RIVER_CLOUD_XIAOZHI_DOWNLINK_PCM_BYTES_MAX];
    int16_t xiaozhi_downlink_mono[RIVER_CLOUD_XIAOZHI_DOWNLINK_PCM_SAMPLES_MAX];
    int16_t xiaozhi_downlink_stereo[RIVER_CLOUD_XIAOZHI_DOWNLINK_PCM_SAMPLES_MAX * 2U];
    river_cloud_xiaozhi_control_request_t
        xiaozhi_control_queue[RIVER_CLOUD_XIAOZHI_CONTROL_QUEUE_DEPTH];
#endif
    char last_text[192];
    char last_error[128];
} river_cloud_context_t;

extern river_cloud_context_t g_river_cloud;

bool river_cloud_time_ready(void);
bool river_cloud_wake_admission_time_ready(void);
void river_cloud_start_sntp_if_needed(void);
void river_cloud_seed_time_from_build_if_needed(void);
void river_cloud_log_time_ready_once(void);
void river_cloud_log_wake_admission_deferred_once(river_status_t status);
void river_cloud_reset_wake_admission_deferred_state(void);
void river_cloud_log_stream_open_deferred_once(river_status_t status);
void river_cloud_reset_stream_open_deferred_state(void);
bool river_cloud_business_time_ready(void);
river_status_t river_cloud_prepare_audio_bridge_state(
    const river_cloud_asr_audio_desc_t *audio,
    uint32_t pre_roll_ms,
    uint32_t post_roll_ms);
river_status_t river_cloud_stream_open_and_flush(void);
void river_cloud_reset_audio_bridge_state(void);
river_status_t river_cloud_stream_finish_active(void);
void river_cloud_pre_roll_store(const uint8_t *pcm);
void river_cloud_pre_roll_reset(void);
void river_cloud_request_state_sync(const char *reason);

#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
void river_cloud_xiaozhi_copy_optional_text(char *dst,
                                            size_t dst_size,
                                            const char *src);
uint32_t river_cloud_xiaozhi_uplink_ready_frames(void);
bool river_cloud_xiaozhi_uplink_active(void);
uint32_t river_cloud_xiaozhi_trim_uplink_stale_frames(uint32_t keep_frames);
river_status_t river_cloud_xiaozhi_push_pcm(const uint8_t *pcm, size_t pcm_bytes);
river_status_t river_cloud_xiaozhi_send_uplink_transport(const uint8_t *pcm,
                                                         size_t pcm_bytes,
                                                         uint32_t timestamp_ms);
void river_cloud_xiaozhi_dump_session_status(uint64_t now_ms);
void river_cloud_xiaozhi_dump_io_status(void);
void river_cloud_xiaozhi_dump_playback_status(uint64_t now_ms);
void river_cloud_xiaozhi_fill_playback_runtime_snapshot(
    river_cloud_runtime_snapshot_t *snapshot);
void river_cloud_xiaozhi_fill_runtime_snapshot(river_cloud_runtime_snapshot_t *snapshot);
bool river_cloud_xiaozhi_io_has_work(void);
void river_cloud_xiaozhi_handle_transport_event(const river_xiaozhi_event_t *event);
river_status_t river_cloud_xiaozhi_execute_control_transport(
    const river_cloud_xiaozhi_control_request_t *request);
void river_cloud_xiaozhi_process_control_queue(void);
void river_cloud_xiaozhi_run_io_tick_housekeeping(void);
void river_cloud_xiaozhi_run_post_poll_housekeeping(void);
void river_cloud_xiaozhi_run_uplink_io_once(void);
void river_cloud_xiaozhi_run_post_uplink_housekeeping(void);
void river_cloud_xiaozhi_note_asr_result_emitted(river_cloud_asr_event_type_t type);
void river_cloud_xiaozhi_note_server_hello_observation(const river_xiaozhi_event_t *event);
void river_cloud_xiaozhi_note_stt_observation(const river_xiaozhi_event_t *event);
void river_cloud_xiaozhi_note_input_observation(const river_xiaozhi_event_t *event);
void river_cloud_xiaozhi_note_preview_observation(const river_xiaozhi_event_t *event);
void river_cloud_xiaozhi_note_llm_observation(const river_xiaozhi_event_t *event);
void river_cloud_xiaozhi_note_tts_observation(const river_xiaozhi_event_t *event);
void river_cloud_xiaozhi_note_session_closed_observation(const river_xiaozhi_event_t *event);
void river_cloud_xiaozhi_note_error_observation(const river_xiaozhi_event_t *event);
river_status_t river_cloud_xiaozhi_control_request_async(
    river_cloud_xiaozhi_control_op_t op,
    const char *arg,
    const char *response_id,
    const char *playback_id,
    const char *segment_id,
    uint32_t played_duration_ms);
void river_cloud_xiaozhi_note_audio_out_meta_observation(const river_xiaozhi_event_t *event);
void river_cloud_xiaozhi_note_response_start_observation(const river_xiaozhi_event_t *event);
void river_cloud_xiaozhi_playback_note_meta(const river_xiaozhi_event_t *event);
void river_cloud_xiaozhi_playback_check_pending_stop(void);
void river_cloud_xiaozhi_playback_finalize_cleared(const char *reason);
bool river_cloud_xiaozhi_apply_capture_entry_playback_policy(void);
void river_cloud_xiaozhi_apply_capture_exit_playback_policy(void);
void river_cloud_xiaozhi_apply_transport_reset_playback_policy(void);
void river_cloud_xiaozhi_apply_session_start_playback_policy(void);
void river_cloud_xiaozhi_apply_playback_backend_refresh_policy(void);
void river_cloud_xiaozhi_playback_start_downlink_if_needed(void);
void river_cloud_xiaozhi_apply_terminal_playback_policy(
    river_cloud_xiaozhi_playback_abort_cause_t cause);
river_status_t river_cloud_xiaozhi_execute_playback_control_transport(
    const river_cloud_xiaozhi_control_request_t *request);
river_status_t river_cloud_xiaozhi_playback_handle_audio_event(
    const river_xiaozhi_event_t *event);
uint32_t river_cloud_xiaozhi_playback_queued_frames(void);
river_cloud_playback_phase_t river_cloud_xiaozhi_playback_phase(void);
river_cloud_playback_rebuffer_cause_t
river_cloud_xiaozhi_playback_rebuffer_cause(void);
bool river_cloud_xiaozhi_playback_rebuffer_pending(void);
bool river_cloud_xiaozhi_playback_output_active(void);
bool river_cloud_xiaozhi_playback_lane_engaged(void);
bool river_cloud_xiaozhi_playback_turn_active(void);
bool river_cloud_xiaozhi_playback_has_work(void);
void river_cloud_xiaozhi_playback_note_duplex_ready(void);
river_status_t river_cloud_xiaozhi_playback_abort_for_cause(
    river_cloud_xiaozhi_playback_abort_cause_t cause,
    const char *detail_reason);
void river_cloud_emit_asr_result(river_cloud_asr_event_type_t type,
                                 const char *text,
                                 const char *sid,
                                 const char *message,
                                 int code,
                                 bool is_final);
bool river_cloud_xiaozhi_idle_requires_wakeword(void);
bool river_cloud_xiaozhi_listening_active(void);
bool river_cloud_xiaozhi_conversation_window_active(void);
uint64_t river_cloud_xiaozhi_conversation_window_remaining_ms(uint64_t now_ms);
bool river_cloud_xiaozhi_local_close_pending(void);
uint64_t river_cloud_xiaozhi_local_close_remaining_ms(uint64_t now_ms);
bool river_cloud_xiaozhi_listen_stop_pending(void);
bool river_cloud_xiaozhi_full_duplex_experiment_enabled(void);
void river_cloud_xiaozhi_get_duplex_ready_eval(river_voice_duplex_ready_eval_t *eval);
const char *river_cloud_xiaozhi_duplex_fallback_reason(
    const river_voice_duplex_ready_eval_t *eval);
bool river_cloud_xiaozhi_playback_allows_vad_open(void);
bool river_cloud_xiaozhi_capture_held_by_playback(
    const river_voice_duplex_ready_eval_t *eval,
    const char **fallback_reason);
void river_cloud_xiaozhi_apply_tts_start_round_policy(void);
void river_cloud_xiaozhi_apply_tts_stop_round_policy(void);
void river_cloud_xiaozhi_apply_reopen_overlap_round_policy(void);
void river_cloud_xiaozhi_apply_llm_round_policy(void);
void river_cloud_xiaozhi_apply_post_stop_result_round_policy(void);
void river_cloud_xiaozhi_apply_open_and_listen_session_policy(void);
void river_cloud_xiaozhi_apply_listen_stop_completion_round_policy(void);
void river_cloud_xiaozhi_maybe_finalize_listen_stop(uint32_t queued_frames,
                                                    size_t accum_bytes);
bool river_cloud_xiaozhi_uplink_keepalive_needed(uint32_t queued_frames);
bool river_cloud_xiaozhi_uplink_send_ready(void);
river_status_t river_cloud_xiaozhi_apply_bridge_open_capture_policy(
    const river_cloud_asr_audio_desc_t *audio);
void river_cloud_xiaozhi_apply_transport_closed_terminal_policy(void);
void river_cloud_xiaozhi_apply_network_lost_terminal_policy(void);
void river_cloud_xiaozhi_apply_bridge_close_capture_policy(void);
void river_cloud_xiaozhi_apply_bridge_close_terminal_policy(void);
uint32_t river_cloud_xiaozhi_open_hold_frames_required(void);
bool river_cloud_xiaozhi_playback_followup_reopen_ready(bool is_speech);
river_status_t river_cloud_xiaozhi_maybe_start_followup_round(bool is_speech,
                                                              uint32_t pre_roll_frames,
                                                              bool *opened);
river_status_t river_cloud_xiaozhi_apply_stream_push_capture_policy(
    const uint8_t *pcm,
    size_t bytes,
    bool is_speech,
    bool *capture_exit_needed);
river_status_t river_cloud_xiaozhi_apply_capture_stream_policy(const uint8_t *pcm,
                                                               size_t bytes,
                                                               bool is_speech);
bool river_cloud_xiaozhi_duplex_soft_endpoint_enabled(void);
bool river_cloud_xiaozhi_duplex_speaking_uplink_continuation_active(void);
bool river_cloud_xiaozhi_endpoint_soft_close_pending(void);
uint64_t river_cloud_xiaozhi_endpoint_soft_close_remaining_ms(uint64_t now_ms);
const char *river_cloud_xiaozhi_endpoint_soft_close_reason(void);
void river_cloud_xiaozhi_note_round_finish_request(const char *reason);
void river_cloud_xiaozhi_clear_endpoint_soft_close_state(void);
void river_cloud_xiaozhi_cancel_endpoint_soft_close(const char *trigger);
void river_cloud_xiaozhi_note_interrupt_hint(const char *trigger,
                                             const char *reason);
river_status_t river_cloud_xiaozhi_interrupt_tts(const char *reason);
void river_cloud_xiaozhi_round_begin(uint32_t pre_roll_frames);
void river_cloud_xiaozhi_round_note_packet_sent(void);
void river_cloud_xiaozhi_arm_endpoint_soft_close(const char *trigger,
                                                 const char *reason);
bool river_cloud_xiaozhi_poll_endpoint_soft_close_timeout(char *reason,
                                                          size_t reason_size);
void river_cloud_xiaozhi_apply_active_stream_capture_policy(bool is_speech);
void river_cloud_xiaozhi_commit_active_stream_finish_for_cause(
    river_cloud_xiaozhi_stream_finish_cause_t cause,
    const char *detail_reason);
void river_cloud_xiaozhi_complete_active_stream_finish(
    river_cloud_xiaozhi_stream_finish_cause_t cause,
    const char *detail_reason);
bool river_cloud_xiaozhi_should_defer_local_close(void);
void river_cloud_xiaozhi_clear_local_close_defer(void);
void river_cloud_xiaozhi_arm_local_close_defer(void);
void river_cloud_xiaozhi_prepare_post_commit_wait(void);
void river_cloud_xiaozhi_window_touch(uint32_t duration_ms, const char *reason);
void river_cloud_xiaozhi_window_close(const char *reason);
void river_cloud_xiaozhi_window_abort_local(const char *reason);
void river_cloud_xiaozhi_copy_session_id_from_transport(void);
void river_cloud_xiaozhi_clear_session_id(void);
const char *river_cloud_xiaozhi_current_sid(void);
void river_cloud_xiaozhi_clear_pending_text(void);
void river_cloud_xiaozhi_clear_preview_state(void);
void river_cloud_xiaozhi_clear_turn_semantics_state(void);
void river_cloud_xiaozhi_capture_turn_semantics_view(
    river_cloud_xiaozhi_turn_semantics_view_t *view);
void river_cloud_xiaozhi_clear_playback_meta_state(void);
void river_cloud_xiaozhi_refresh_turn_semantics(const char *trigger);
bool river_cloud_xiaozhi_turn_accepted(void);
void river_cloud_xiaozhi_note_semantic_fallback(const char *reason);
void river_cloud_xiaozhi_finalize_pending_text_if_turn_accepted(const char *trigger);
void river_cloud_xiaozhi_finalize_pending_text(const char *trigger);
void river_cloud_xiaozhi_emit_session_started(void);
void river_cloud_xiaozhi_emit_session_closed(void);
void river_cloud_xiaozhi_cancel_playback_stop(void);
void river_cloud_xiaozhi_mark_playback_started(void);
void river_cloud_xiaozhi_arm_playback_stop(uint32_t drain_ms);
void river_cloud_xiaozhi_reset_playback_state(void);
void river_cloud_xiaozhi_reset_downlink_state(void);
void river_cloud_xiaozhi_reset_transport_state(bool emit_session_closed);
void river_cloud_xiaozhi_check_window_timeout(void);
void river_cloud_xiaozhi_round_finish(const char *reason);
void river_cloud_xiaozhi_close_local_round_for_cause(
    river_cloud_xiaozhi_round_close_cause_t cause,
    const char *detail_reason);
void river_cloud_xiaozhi_check_local_close_timeout(void);
river_status_t river_cloud_xiaozhi_request_open_and_listen(const char *mode);
river_status_t river_cloud_xiaozhi_request_listen_stop(void);
river_status_t river_cloud_xiaozhi_request_abort(const char *reason);
river_status_t river_cloud_xiaozhi_request_close_session(void);
river_status_t river_cloud_xiaozhi_execute_session_control_transport(
    river_cloud_xiaozhi_control_op_t op,
    const char *arg);
river_status_t river_cloud_xiaozhi_execute_open_and_listen_transport(
    const char *mode);
river_status_t river_cloud_xiaozhi_open_session_and_listen(void);
river_status_t river_cloud_xiaozhi_start_followup_round(uint32_t pre_roll_frames);
river_status_t river_cloud_xiaozhi_begin_conversation_window(const char *source);
#endif

#endif
