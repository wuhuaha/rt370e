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
#define RIVER_CLOUD_XIAOZHI_UPLINK_PACKET_MAX 512U
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
#define RIVER_CLOUD_XIAOZHI_DOWNLINK_RING_FRAMES 16U
#define RIVER_CLOUD_XIAOZHI_DOWNLINK_START_FRAMES 8U
#define RIVER_CLOUD_XIAOZHI_PLAYBACK_BUFFER_FRAMES 3U
#define RIVER_CLOUD_XIAOZHI_PLAYBACK_BUFFER_FRAMES_FALLBACK 2U
#define RIVER_CLOUD_XIAOZHI_PLAYBACK_REF_HISTORY_MS 320U
#define RIVER_CLOUD_XIAOZHI_UPLINK_POLL_MS    5U
#define RIVER_CLOUD_XIAOZHI_UPLINK_RING_FRAMES 64U
#define RIVER_CLOUD_XIAOZHI_UPLINK_STALE_FRAMES_MAX 6U
#define RIVER_CLOUD_XIAOZHI_UPLINK_BUSY_BACKOFF_MAX_MS 160U
#define RIVER_CLOUD_XIAOZHI_UPLINK_BUSY_LOG_INTERVAL_MS 1000U
#define RIVER_CLOUD_XIAOZHI_CONTROL_QUEUE_DEPTH 8U
#define RIVER_CLOUD_XIAOZHI_CONTROL_WAIT_MS 0xFFFFFFFFU
#define RIVER_CLOUD_XIAOZHI_CONTROL_ARG_MAX 64U
#define RIVER_CLOUD_XIAOZHI_PRE_ROLL_MAX_MS    128U
#define RIVER_CLOUD_XIAOZHI_OPEN_HOLD_FRAMES   2U
#define RIVER_CLOUD_XIAOZHI_WAKE_WINDOW_FOLLOWUP_MS 8000U
#define RIVER_CLOUD_XIAOZHI_POST_TTS_SILENCE_CLOSE_MS 3000U
#define RIVER_CLOUD_XIAOZHI_UPLINK_PCM_FRAME_MAX \
    ((RIVER_XIAOZHI_UPLINK_SAMPLE_RATE * RIVER_XIAOZHI_UPLINK_CHANNELS * \
      sizeof(int16_t) * RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS) / 1000U)

typedef enum {
    RIVER_CLOUD_XIAOZHI_CTRL_OPEN_AND_LISTEN = 0,
    RIVER_CLOUD_XIAOZHI_CTRL_LISTEN_STOP = 1,
    RIVER_CLOUD_XIAOZHI_CTRL_ABORT = 2,
    RIVER_CLOUD_XIAOZHI_CTRL_CLOSE_SESSION = 3
} river_cloud_xiaozhi_control_op_t;

typedef struct {
    river_cloud_xiaozhi_control_op_t op;
    char arg[RIVER_CLOUD_XIAOZHI_CONTROL_ARG_MAX];
    rtos_sema_t completion;
    river_status_t *result_out;
} river_cloud_xiaozhi_control_request_t;

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
    bool xiaozhi_listening;
    bool xiaozhi_window_active;
    bool xiaozhi_pending_text_valid;
    bool xiaozhi_pending_text_finalized;
    bool xiaozhi_playback_active;
    bool xiaozhi_tts_stop_pending;
    bool xiaozhi_listen_stop_pending;
    bool xiaozhi_io_started;
    bool xiaozhi_downlink_started;
    rtos_mutex_t xiaozhi_control_lock;
    rtos_sema_t xiaozhi_control_ready;
    rtos_sema_t xiaozhi_control_space;
    rtos_task_t xiaozhi_io_task;
    rtos_task_t xiaozhi_downlink_task;
    river_audio_frame_ring_t xiaozhi_downlink_ring;
    river_audio_frame_ring_t xiaozhi_uplink_ring;
    river_opus_encoder_t xiaozhi_encoder;
    river_opus_decoder_t xiaozhi_decoder;
    uint32_t xiaozhi_server_sample_rate;
    uint32_t xiaozhi_server_frame_duration_ms;
    uint32_t xiaozhi_downlink_sample_rate;
    uint32_t xiaozhi_downlink_frame_duration_ms;
    uint32_t xiaozhi_downlink_ring_dropped;
    uint32_t xiaozhi_open_speech_frames;
    uint32_t xiaozhi_uplink_timestamp_ms;
    uint32_t xiaozhi_uplink_ring_dropped;
    uint32_t xiaozhi_uplink_busy_count;
    uint32_t xiaozhi_uplink_fail_count;
    uint32_t xiaozhi_uplink_stale_dropped;
    uint32_t xiaozhi_uplink_busy_streak;
    uint64_t xiaozhi_tts_stop_deadline_ms;
    uint64_t xiaozhi_window_deadline_ms;
    uint64_t xiaozhi_uplink_next_send_ms;
    uint64_t xiaozhi_uplink_last_busy_log_ms;
    uint32_t xiaozhi_control_read_index;
    uint32_t xiaozhi_control_write_index;
    uint32_t xiaozhi_control_count;
    uint32_t xiaozhi_control_high_watermark;
    size_t xiaozhi_uplink_accum_bytes;
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
    uint32_t xiaozhi_asr_round_id;
    bool xiaozhi_asr_round_active;
    uint32_t xiaozhi_asr_round_started_ms;
    uint32_t xiaozhi_asr_round_first_packet_ms;
    uint32_t xiaozhi_asr_round_pre_roll_frames;
    uint32_t xiaozhi_asr_round_packets_sent;
    uint32_t xiaozhi_asr_round_partial_count;
    uint32_t xiaozhi_asr_round_final_count;
    bool xiaozhi_asr_round_partial_seen;
    bool xiaozhi_asr_round_final_seen;
    uint32_t xiaozhi_asr_round_busy_base;
    uint32_t xiaozhi_asr_round_fail_base;
    uint32_t xiaozhi_asr_round_stale_drop_base;
    uint32_t xiaozhi_asr_round_ring_drop_base;
    char xiaozhi_session_id[RIVER_CLOUD_XIAOZHI_SESSION_ID_MAX];
    char xiaozhi_asr_round_close_reason[32];
    char xiaozhi_pending_text[RIVER_CLOUD_XIAOZHI_TEXT_MAX];
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
void river_cloud_pre_roll_reset(void);
void river_cloud_request_state_sync(const char *reason);

#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
void river_cloud_emit_asr_result(river_cloud_asr_event_type_t type,
                                 const char *text,
                                 const char *sid,
                                 const char *message,
                                 int code,
                                 bool is_final);
bool river_cloud_xiaozhi_idle_requires_wakeword(void);
bool river_cloud_xiaozhi_playback_allows_vad_open(void);
void river_cloud_xiaozhi_window_touch(uint32_t duration_ms, const char *reason);
void river_cloud_xiaozhi_window_close(const char *reason);
void river_cloud_xiaozhi_window_abort_local(const char *reason);
void river_cloud_xiaozhi_copy_session_id_from_transport(void);
const char *river_cloud_xiaozhi_current_sid(void);
void river_cloud_xiaozhi_clear_pending_text(void);
void river_cloud_xiaozhi_finalize_pending_text(void);
void river_cloud_xiaozhi_emit_session_started(void);
void river_cloud_xiaozhi_emit_session_closed(void);
void river_cloud_xiaozhi_cancel_playback_stop(void);
void river_cloud_xiaozhi_mark_playback_started(void);
void river_cloud_xiaozhi_arm_playback_stop(uint32_t drain_ms);
void river_cloud_xiaozhi_reset_playback_state(void);
void river_cloud_xiaozhi_reset_downlink_state(void);
void river_cloud_xiaozhi_reset_transport_state(bool emit_session_closed);
void river_cloud_xiaozhi_check_window_timeout(void);
river_status_t river_cloud_xiaozhi_request_open_and_listen(const char *mode);
river_status_t river_cloud_xiaozhi_request_listen_stop(void);
river_status_t river_cloud_xiaozhi_request_abort(const char *reason);
river_status_t river_cloud_xiaozhi_request_close_session(void);
river_status_t river_cloud_xiaozhi_open_session_and_listen(void);
river_status_t river_cloud_xiaozhi_begin_conversation_window(const char *source);
#endif

#endif
