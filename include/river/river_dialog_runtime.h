/* 对话运行时真相源：统一维护会话、输入/输出 lane 与派生交互状态。 */
#ifndef AMEBA_RIVER_DIALOG_RUNTIME_H
#define AMEBA_RIVER_DIALOG_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "river/river_cloud.h"
#include "river/river_interaction_state.h"
#include "river/river_playback_service.h"
#include "river/river_types.h"

typedef enum {
    RIVER_DIALOG_INPUT_LANE_UNKNOWN = 0,
    RIVER_DIALOG_INPUT_LANE_IDLE,
    RIVER_DIALOG_INPUT_LANE_ACTIVE,
    RIVER_DIALOG_INPUT_LANE_COMMITTED
} river_dialog_input_lane_t;

typedef enum {
    RIVER_DIALOG_OUTPUT_LANE_UNKNOWN = 0,
    RIVER_DIALOG_OUTPUT_LANE_IDLE,
    RIVER_DIALOG_OUTPUT_LANE_THINKING,
    RIVER_DIALOG_OUTPUT_LANE_SPEAKING
} river_dialog_output_lane_t;

typedef struct {
    bool boot_ready;
    bool wake_confirmed;
    bool asr_session_active;
    bool error_recovering;
    bool conversation_window_active;
    bool cloud_listening;
    bool cloud_stream_active;
    bool cloud_listen_stop_pending;
    bool cloud_local_close_pending;
    bool playback_local_active;
    bool playback_local_recovering;
    bool playback_cloud_active;
    bool playback_lane_engaged;
    bool playback_active;
    bool playback_rebuffer_pending;
    bool playback_recovering;
    bool playback_phase_known;
    bool playback_backend_owned;
    bool playback_backend_restart_pending;
    bool playback_terminal_closed;
    bool playback_terminal_waiting;
    bool tts_stop_pending;
    bool tts_interrupt_requested;
    bool turn_accepted;
    bool barge_in_enabled_known;
    bool barge_in_enabled;
    river_playback_state_t playback_state;
    river_dialog_input_lane_t input_lane;
    river_dialog_output_lane_t output_lane;
    river_interaction_state_t interaction_state;
    uint32_t transition_count;
    char reason[48];
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
    char input_state_text[RIVER_CLOUD_RUNTIME_STATE_MAX];
    char output_state_text[RIVER_CLOUD_RUNTIME_STATE_MAX];
    uint32_t conversation_window_remaining_ms;
    uint32_t local_close_remaining_ms;
    uint32_t playback_start_frames;
    uint32_t playback_prefetch_frames;
    bool playback_start_cautious_history;
} river_dialog_runtime_snapshot_t;

river_status_t river_dialog_runtime_init(void);
void river_dialog_runtime_mark_boot_ready_with_cloud_state(const char *reason);
void river_dialog_runtime_note_wake_confirmed_with_cloud_state(const char *reason);
void river_dialog_runtime_note_asr_session_started_with_cloud_state(const char *sid,
                                                                    const char *reason);
void river_dialog_runtime_note_asr_session_closed_with_cloud_state(const char *sid,
                                                                   const char *reason);
void river_dialog_runtime_note_asr_error_with_cloud_state(const char *sid, const char *reason);
void river_dialog_runtime_on_cloud_asr_result(const river_cloud_asr_result_t *result,
                                              void *user_data);
void river_dialog_runtime_note_tts_interrupt_requested(const char *reason);
void river_dialog_runtime_on_playback_state(river_playback_state_t state,
                                            const river_playback_stream_config_t *config,
                                            void *user_data);
void river_dialog_runtime_on_cloud_state_sync(const char *reason, void *user_data);
void river_dialog_runtime_sync_cloud_state(const char *reason);
const char *river_dialog_runtime_wakeword_admission_block_reason(void);
bool river_dialog_runtime_allows_barge_in_interrupt(void);
river_interaction_state_t river_dialog_runtime_interaction_state(void);
river_status_t river_dialog_runtime_get_snapshot(river_dialog_runtime_snapshot_t *snapshot);
void river_dialog_runtime_dump_status(void);
const char *river_dialog_input_lane_name(river_dialog_input_lane_t state);
const char *river_dialog_output_lane_name(river_dialog_output_lane_t state);

#endif
