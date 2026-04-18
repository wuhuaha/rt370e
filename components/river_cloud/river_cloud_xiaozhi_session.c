/* 小智会话编排：处理唤醒准入、会话窗口、ASR/TTS 与回放衔接。 */
#include <stdio.h>
#include <string.h>

#include "river/river_log.h"
#include "river/river_playback_service.h"
#include "river/river_runtime_stats.h"
#include "river/river_voice_profile.h"
#include "river/river_voice_runtime_policy.h"
#include "river/river_wifi_station.h"

#include "river_cloud_internal.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.cloud"

#if RIVER_CLOUD_BACKEND_XIAOZHI_ENABLED
bool river_cloud_xiaozhi_full_duplex_experiment_enabled(void)
{
#if defined(CONFIG_RIVER_XIAOZHI_FULL_DUPLEX_EXPERIMENT_EN) && \
    CONFIG_RIVER_XIAOZHI_FULL_DUPLEX_EXPERIMENT_EN
    return true;
#else
    return false;
#endif
}

void river_cloud_xiaozhi_get_duplex_ready_eval(river_voice_duplex_ready_eval_t *eval)
{
    river_voice_runtime_duplex_ready_eval(river_cloud_xiaozhi_full_duplex_experiment_enabled(),
                                          river_voice_profile_active_preproc(),
                                          eval);
}

static const char *river_cloud_xiaozhi_runtime_duplex_fallback_reason(
    const river_voice_duplex_ready_eval_t *eval)
{
    if (eval == NULL) {
        return "half_duplex_aec_blocked";
    }

    switch (eval->reason) {
    case RIVER_VOICE_DUPLEX_READY_EXPERIMENT_OFF:
        return "half_duplex_experiment_disabled";
    case RIVER_VOICE_DUPLEX_READY_PROFILE_NO_REF:
        return "half_duplex_no_playback_reference";
    case RIVER_VOICE_DUPLEX_READY_PLAYBACK_RESTART_PENDING:
        return "half_duplex_restart_pending";
    case RIVER_VOICE_DUPLEX_READY_REF_IDLE:
        return "half_duplex_ref_idle";
    case RIVER_VOICE_DUPLEX_READY_AEC_BLOCKED:
    default:
        return "half_duplex_aec_blocked";
    }
}

const char *river_cloud_xiaozhi_duplex_fallback_reason(
    const river_voice_duplex_ready_eval_t *eval)
{
    const char *policy_reason;

    policy_reason = river_xiaozhi_duplex_default_fallback_reason();
    if (policy_reason != NULL) {
        return policy_reason;
    }
    if (eval != NULL && eval->ready) {
        return NULL;
    }
    return river_cloud_xiaozhi_runtime_duplex_fallback_reason(eval);
}

bool river_cloud_xiaozhi_idle_requires_wakeword(void)
{
#if defined(CONFIG_RIVER_VOICE_CAPABILITY_KWS) && CONFIG_RIVER_VOICE_CAPABILITY_KWS
    return true;
#else
    return false;
#endif
}

bool river_cloud_xiaozhi_listening_active(void)
{
    return g_river_cloud.xiaozhi_listening;
}

bool river_cloud_xiaozhi_conversation_window_active(void)
{
    return g_river_cloud.xiaozhi_window_active;
}

uint64_t river_cloud_xiaozhi_conversation_window_remaining_ms(uint64_t now_ms)
{
    if (!g_river_cloud.xiaozhi_window_active ||
        g_river_cloud.xiaozhi_window_deadline_ms <= now_ms) {
        return 0U;
    }

    return g_river_cloud.xiaozhi_window_deadline_ms - now_ms;
}

bool river_cloud_xiaozhi_local_close_pending(void)
{
    return g_river_cloud.xiaozhi_local_close_pending;
}

uint64_t river_cloud_xiaozhi_local_close_remaining_ms(uint64_t now_ms)
{
    if (!g_river_cloud.xiaozhi_local_close_pending ||
        g_river_cloud.xiaozhi_local_close_deadline_ms <= now_ms) {
        return 0U;
    }

    return g_river_cloud.xiaozhi_local_close_deadline_ms - now_ms;
}

bool river_cloud_xiaozhi_listen_stop_pending(void)
{
    return g_river_cloud.xiaozhi_listen_stop_pending;
}

static bool river_cloud_xiaozhi_control_pending(void)
{
    return g_river_cloud.xiaozhi_control_ready != NULL &&
           rtos_sema_get_count(g_river_cloud.xiaozhi_control_ready) > 0U;
}

static bool river_cloud_xiaozhi_transport_active(void)
{
    if (!g_river_cloud.initialized || !g_river_cloud.xiaozhi_enabled) {
        return false;
    }

    return river_xiaozhi_session_open() || river_cloud_xiaozhi_playback_lane_engaged() ||
           river_cloud_xiaozhi_listening_active();
}

bool river_cloud_xiaozhi_uplink_active(void)
{
    uint32_t queued_frames;

    if (!g_river_cloud.initialized || !g_river_cloud.xiaozhi_enabled ||
        !g_river_cloud.xiaozhi_uplink_ring.initialized) {
        return false;
    }

    queued_frames = river_cloud_xiaozhi_uplink_ready_frames();
    return river_cloud_xiaozhi_uplink_keepalive_needed(queued_frames);
}

void river_cloud_xiaozhi_copy_optional_text(char *dst,
                                            size_t dst_size,
                                            const char *src)
{
    if (dst == NULL || dst_size == 0U) {
        return;
    }
    if (src == NULL || src[0] == '\0') {
        dst[0] = '\0';
        return;
    }

    snprintf(dst, dst_size, "%s", src);
}

uint32_t river_cloud_xiaozhi_uplink_ready_frames(void)
{
    uint32_t ready_frames;

    ready_frames = river_audio_frame_ring_count(&g_river_cloud.xiaozhi_uplink_ring);
    if (g_river_cloud.xiaozhi_uplink_retry_valid && ready_frames < UINT32_MAX) {
        ready_frames++;
    }
    return ready_frames;
}

uint32_t river_cloud_xiaozhi_trim_uplink_stale_frames(uint32_t keep_frames)
{
    uint32_t dropped = 0U;

    if (!g_river_cloud.xiaozhi_uplink_ring.initialized) {
        return 0U;
    }

    while (river_cloud_xiaozhi_uplink_ready_frames() > keep_frames) {
        if (river_audio_frame_ring_count(&g_river_cloud.xiaozhi_uplink_ring) == 0U) {
            break;
        }
        if (river_audio_frame_ring_read(&g_river_cloud.xiaozhi_uplink_ring,
                                        g_river_cloud.xiaozhi_uplink_drop_frame) != RIVER_OK) {
            break;
        }
        dropped++;
    }

    if (dropped != 0U) {
        g_river_cloud.xiaozhi_uplink_stale_dropped += dropped;
    }
    return dropped;
}

static river_status_t river_cloud_xiaozhi_queue_uplink_packet(const uint8_t *pcm,
                                                              size_t pcm_bytes)
{
    river_status_t status;
    uint32_t keep_before_write;

    if (pcm == NULL || pcm_bytes == 0U || !g_river_cloud.xiaozhi_uplink_ring.initialized) {
        return RIVER_ERR_ARG;
    }

    /*
     * Keep the realtime uplink queue short. Once the transport falls behind,
     * older audio is less valuable than preserving a low-latency tail for live
     * ASR. This mirrors the reference xiaozhi client more closely than waiting
     * for the full 64-frame ring to overflow.
     */
    keep_before_write = (RIVER_CLOUD_XIAOZHI_UPLINK_STALE_FRAMES_MAX > 0U) ?
                            (RIVER_CLOUD_XIAOZHI_UPLINK_STALE_FRAMES_MAX - 1U) :
                            0U;
    (void)river_cloud_xiaozhi_trim_uplink_stale_frames(keep_before_write);

    status = river_audio_frame_ring_write(&g_river_cloud.xiaozhi_uplink_ring, pcm);
    if (status == RIVER_OK) {
        return RIVER_OK;
    }

    if (river_audio_frame_ring_read(&g_river_cloud.xiaozhi_uplink_ring,
                                    g_river_cloud.xiaozhi_uplink_drop_frame) != RIVER_OK) {
        return status;
    }

    status = river_audio_frame_ring_write(&g_river_cloud.xiaozhi_uplink_ring, pcm);
    if (status == RIVER_OK) {
        g_river_cloud.xiaozhi_uplink_ring_dropped++;
        RIVER_LOGW("xiaozhi uplink ring overflow: dropped=%lu queued=%lu capacity=%u",
                   (unsigned long)g_river_cloud.xiaozhi_uplink_ring_dropped,
                   (unsigned long)river_audio_frame_ring_count(&g_river_cloud.xiaozhi_uplink_ring),
                   (unsigned int)RIVER_CLOUD_XIAOZHI_UPLINK_RING_FRAMES);
    }
    return status;
}

river_status_t river_cloud_xiaozhi_push_pcm(const uint8_t *pcm, size_t pcm_bytes)
{
    size_t frame_bytes;

    if (pcm == NULL || pcm_bytes == 0U || !g_river_cloud.xiaozhi_uplink_ring.initialized) {
        return RIVER_ERR_ARG;
    }

    frame_bytes = RIVER_CLOUD_XIAOZHI_UPLINK_PCM_FRAME_MAX;
    if ((g_river_cloud.xiaozhi_uplink_accum_bytes + pcm_bytes) >
        sizeof(g_river_cloud.xiaozhi_uplink_accum)) {
        return RIVER_ERR_BUSY;
    }

    memcpy(g_river_cloud.xiaozhi_uplink_accum + g_river_cloud.xiaozhi_uplink_accum_bytes,
           pcm,
           pcm_bytes);
    g_river_cloud.xiaozhi_uplink_accum_bytes += pcm_bytes;

    while (g_river_cloud.xiaozhi_uplink_accum_bytes >= frame_bytes) {
        river_status_t status;

        status = river_cloud_xiaozhi_queue_uplink_packet(g_river_cloud.xiaozhi_uplink_accum,
                                                         frame_bytes);
        if (status != RIVER_OK) {
            return status;
        }
        g_river_cloud.xiaozhi_uplink_accum_bytes -= frame_bytes;
        if (g_river_cloud.xiaozhi_uplink_accum_bytes > 0U) {
            memmove(g_river_cloud.xiaozhi_uplink_accum,
                    g_river_cloud.xiaozhi_uplink_accum + frame_bytes,
                    g_river_cloud.xiaozhi_uplink_accum_bytes);
        }
    }

    return RIVER_OK;
}

bool river_cloud_xiaozhi_io_has_work(void)
{
    return river_cloud_xiaozhi_control_pending() ||
           river_cloud_xiaozhi_transport_active() ||
           river_cloud_xiaozhi_uplink_active();
}

void river_cloud_xiaozhi_note_asr_result_emitted(river_cloud_asr_event_type_t type)
{
    if (type == RIVER_CLOUD_ASR_EVENT_PARTIAL) {
        g_river_cloud.xiaozhi_asr_round_partial_seen = true;
        if (g_river_cloud.xiaozhi_asr_round_partial_count < UINT32_MAX) {
            g_river_cloud.xiaozhi_asr_round_partial_count++;
        }
    } else if (type == RIVER_CLOUD_ASR_EVENT_FINAL) {
        g_river_cloud.xiaozhi_asr_round_final_seen = true;
        if (g_river_cloud.xiaozhi_asr_round_final_count < UINT32_MAX) {
            g_river_cloud.xiaozhi_asr_round_final_count++;
        }
    }
}

void river_cloud_xiaozhi_fill_runtime_snapshot(river_cloud_runtime_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    snapshot->conversation_window_active = river_cloud_xiaozhi_conversation_window_active();
    snapshot->listening = river_cloud_xiaozhi_listening_active();
    snapshot->playback_active = g_river_cloud.xiaozhi_playback_active;
    snapshot->playback_lane_engaged = river_cloud_xiaozhi_playback_lane_engaged();
    snapshot->playback_rebuffer_pending = g_river_cloud.xiaozhi_playback_rebuffer_pending;
    snapshot->playback_terminal_waiting = g_river_cloud.xiaozhi_playback_terminal_waiting;
    snapshot->tts_stop_pending = g_river_cloud.xiaozhi_tts_stop_pending;
    snapshot->turn_accepted = g_river_cloud.xiaozhi_turn_accepted;
    snapshot->barge_in_enabled_known = g_river_cloud.xiaozhi_transport_barge_in_enabled_known;
    snapshot->barge_in_enabled = g_river_cloud.xiaozhi_transport_barge_in_enabled;
    river_cloud_xiaozhi_copy_optional_text(snapshot->session_id,
                                           sizeof(snapshot->session_id),
                                           g_river_cloud.xiaozhi_session_id);
    river_cloud_xiaozhi_copy_optional_text(snapshot->turn_id,
                                           sizeof(snapshot->turn_id),
                                           g_river_cloud.xiaozhi_turn_id);
    river_cloud_xiaozhi_copy_optional_text(snapshot->accept_reason,
                                           sizeof(snapshot->accept_reason),
                                           g_river_cloud.xiaozhi_accept_reason);
    river_cloud_xiaozhi_copy_optional_text(snapshot->playback_terminal_state,
                                           sizeof(snapshot->playback_terminal_state),
                                           g_river_cloud.xiaozhi_playback_terminal_state);
    river_cloud_xiaozhi_copy_optional_text(snapshot->playback_terminal_reason,
                                           sizeof(snapshot->playback_terminal_reason),
                                           g_river_cloud.xiaozhi_playback_clear_reason);
    river_cloud_xiaozhi_copy_optional_text(snapshot->playback_terminal_wait_reason,
                                           sizeof(snapshot->playback_terminal_wait_reason),
                                           g_river_cloud.xiaozhi_playback_terminal_wait_reason);
    river_cloud_xiaozhi_copy_optional_text(snapshot->input_state,
                                           sizeof(snapshot->input_state),
                                           g_river_cloud.xiaozhi_input_state);
    river_cloud_xiaozhi_copy_optional_text(snapshot->output_state,
                                           sizeof(snapshot->output_state),
                                           g_river_cloud.xiaozhi_output_state);
}

void river_cloud_xiaozhi_dump_session_status(uint64_t now_ms)
{
    uint64_t remaining_ms;
    uint64_t local_close_left_ms;
    uint64_t endpoint_soft_close_left_ms;

    remaining_ms = river_cloud_xiaozhi_conversation_window_remaining_ms(now_ms);
    local_close_left_ms = river_cloud_xiaozhi_local_close_remaining_ms(now_ms);
    endpoint_soft_close_left_ms =
        (g_river_cloud.xiaozhi_endpoint_soft_close_pending &&
         g_river_cloud.xiaozhi_endpoint_soft_close_deadline_ms > now_ms) ?
            (g_river_cloud.xiaozhi_endpoint_soft_close_deadline_ms - now_ms) :
            0U;

    RIVER_LOGI("xiaozhi runtime enabled=%s io=%s session=%s listening=%s playback=%s stop_pending=%s close_pending=%s window=%s followup_left_ms=%lu close_left_ms=%lu wake_admission=%s sid=%s pending_text=%s",
               g_river_cloud.xiaozhi_enabled ? "yes" : "no",
               g_river_cloud.xiaozhi_io_started ? "running" : "off",
               river_xiaozhi_session_open() ? "open" : "closed",
               river_cloud_xiaozhi_listening_active() ? "yes" : "no",
               g_river_cloud.xiaozhi_playback_active ? "yes" : "no",
               g_river_cloud.xiaozhi_tts_stop_pending ? "yes" : "no",
               river_cloud_xiaozhi_local_close_pending() ? "yes" : "no",
               river_cloud_xiaozhi_conversation_window_active() ? "yes" : "no",
               (unsigned long)remaining_ms,
               (unsigned long)local_close_left_ms,
               river_cloud_xiaozhi_idle_requires_wakeword() ? "wakeword" : "legacy_vad",
               g_river_cloud.xiaozhi_session_id[0] != '\0' ? g_river_cloud.xiaozhi_session_id : "-",
               g_river_cloud.xiaozhi_pending_text_valid ? g_river_cloud.xiaozhi_pending_text : "-");
    RIVER_LOGI("xiaozhi preview preview_id=%s speech_started=%s text=%s stable_prefix=%s is_final=%s endpoint_candidate=%s reason=%s source=%s audio_offset_ms=%lu",
               g_river_cloud.xiaozhi_preview_id[0] != '\0' ? g_river_cloud.xiaozhi_preview_id :
                                                             "-",
               g_river_cloud.xiaozhi_preview_speech_started ? "yes" : "no",
               g_river_cloud.xiaozhi_preview_text[0] != '\0' ? g_river_cloud.xiaozhi_preview_text :
                                                               "-",
               g_river_cloud.xiaozhi_preview_stable_prefix[0] != '\0' ?
                   g_river_cloud.xiaozhi_preview_stable_prefix :
                   "-",
               g_river_cloud.xiaozhi_preview_final ? "yes" : "no",
               g_river_cloud.xiaozhi_preview_endpoint_candidate ? "yes" : "no",
               g_river_cloud.xiaozhi_preview_endpoint_reason[0] != '\0' ?
                   g_river_cloud.xiaozhi_preview_endpoint_reason :
                   "-",
               g_river_cloud.xiaozhi_preview_source[0] != '\0' ? g_river_cloud.xiaozhi_preview_source :
                                                                 "-",
               (unsigned long)g_river_cloud.xiaozhi_preview_audio_offset_ms);
    RIVER_LOGI("xiaozhi endpoint_soft_close pending=%s reason=%s left_ms=%lu",
               g_river_cloud.xiaozhi_endpoint_soft_close_pending ? "yes" : "no",
               g_river_cloud.xiaozhi_endpoint_soft_close_reason[0] != '\0' ?
                   g_river_cloud.xiaozhi_endpoint_soft_close_reason :
                   "-",
               (unsigned long)endpoint_soft_close_left_ms);
    RIVER_LOGI("xiaozhi turn_semantics accepted=%s accept_reason=%s turn_id=%s input_state=%s output_state=%s barge_in_enabled=%s fallback=%s",
               river_cloud_xiaozhi_turn_accepted() ? "yes" : "no",
               g_river_cloud.xiaozhi_accept_reason[0] != '\0' ? g_river_cloud.xiaozhi_accept_reason :
                                                                "-",
               g_river_cloud.xiaozhi_turn_id[0] != '\0' ? g_river_cloud.xiaozhi_turn_id : "-",
               g_river_cloud.xiaozhi_input_state[0] != '\0' ? g_river_cloud.xiaozhi_input_state :
                                                              "-",
               g_river_cloud.xiaozhi_output_state[0] != '\0' ?
                   g_river_cloud.xiaozhi_output_state :
                   "-",
               g_river_cloud.xiaozhi_transport_barge_in_enabled_known ?
                   (g_river_cloud.xiaozhi_transport_barge_in_enabled ? "yes" : "no") :
                   "-",
               g_river_cloud.xiaozhi_semantic_fallback_reason[0] != '\0' ?
                   g_river_cloud.xiaozhi_semantic_fallback_reason :
                   "-");
}

void river_cloud_xiaozhi_dump_io_status(void)
{
    RIVER_LOGI("xiaozhi control queue=%lu/%u peak=%lu owner=%s",
               (unsigned long)g_river_cloud.xiaozhi_control_count,
               (unsigned int)RIVER_CLOUD_XIAOZHI_CONTROL_QUEUE_DEPTH,
               (unsigned long)g_river_cloud.xiaozhi_control_high_watermark,
               g_river_cloud.xiaozhi_io_started ? "running" : "off");
    RIVER_LOGI("xiaozhi uplink queue=%lu/%u dropped=%lu stale_drop=%lu busy=%lu fail=%lu stop_pending=%s owner=%s",
               (unsigned long)river_cloud_xiaozhi_uplink_ready_frames(),
               (unsigned int)RIVER_CLOUD_XIAOZHI_UPLINK_RING_FRAMES,
               (unsigned long)g_river_cloud.xiaozhi_uplink_ring_dropped,
               (unsigned long)g_river_cloud.xiaozhi_uplink_stale_dropped,
               (unsigned long)g_river_cloud.xiaozhi_uplink_busy_count,
               (unsigned long)g_river_cloud.xiaozhi_uplink_fail_count,
               river_cloud_xiaozhi_listen_stop_pending() ? "yes" : "no",
               g_river_cloud.xiaozhi_io_started ? "running" : "off");
    RIVER_LOGI("xiaozhi asr round id=%lu active=%s pre_roll_frames=%lu packets=%lu partial=%lu final=%lu close_reason=%s",
               (unsigned long)g_river_cloud.xiaozhi_asr_round_id,
               g_river_cloud.xiaozhi_asr_round_active ? "yes" : "no",
               (unsigned long)g_river_cloud.xiaozhi_asr_round_pre_roll_frames,
               (unsigned long)g_river_cloud.xiaozhi_asr_round_packets_sent,
               (unsigned long)g_river_cloud.xiaozhi_asr_round_partial_count,
               (unsigned long)g_river_cloud.xiaozhi_asr_round_final_count,
               g_river_cloud.xiaozhi_asr_round_close_reason[0] != '\0' ?
                   g_river_cloud.xiaozhi_asr_round_close_reason :
                   "-");
}

void river_cloud_xiaozhi_note_stt_observation(const river_xiaozhi_event_t *event)
{
    char next_text[RIVER_CLOUD_XIAOZHI_TEXT_MAX];

    if (event == NULL || event->text == NULL || event->text[0] == '\0') {
        return;
    }

    river_cloud_xiaozhi_copy_optional_text(next_text, sizeof(next_text), event->text);
    if (next_text[0] == '\0') {
        return;
    }
    if (g_river_cloud.xiaozhi_pending_text_valid &&
        strcmp(g_river_cloud.xiaozhi_pending_text, next_text) == 0) {
        return;
    }

    river_cloud_xiaozhi_copy_optional_text(g_river_cloud.xiaozhi_pending_text,
                                           sizeof(g_river_cloud.xiaozhi_pending_text),
                                           next_text);
    g_river_cloud.xiaozhi_pending_text_valid = true;
    g_river_cloud.xiaozhi_pending_text_finalized = false;
    river_cloud_emit_asr_result(RIVER_CLOUD_ASR_EVENT_PARTIAL,
                                g_river_cloud.xiaozhi_pending_text,
                                river_cloud_xiaozhi_current_sid(),
                                NULL,
                                0,
                                false);
}

void river_cloud_xiaozhi_note_preview_observation(const river_xiaozhi_event_t *event)
{
    if (event == NULL) {
        return;
    }

    if (event->preview_id != NULL && event->preview_id[0] != '\0' &&
        strcmp(g_river_cloud.xiaozhi_preview_id, event->preview_id) != 0) {
        river_cloud_xiaozhi_clear_preview_state();
    }

    river_cloud_xiaozhi_copy_optional_text(g_river_cloud.xiaozhi_preview_id,
                                           sizeof(g_river_cloud.xiaozhi_preview_id),
                                           event->preview_id);
    if (event->text != NULL && event->text[0] != '\0') {
        river_cloud_xiaozhi_copy_optional_text(g_river_cloud.xiaozhi_preview_text,
                                               sizeof(g_river_cloud.xiaozhi_preview_text),
                                               event->text);
    }
    if (event->stable_prefix != NULL && event->stable_prefix[0] != '\0') {
        river_cloud_xiaozhi_copy_optional_text(g_river_cloud.xiaozhi_preview_stable_prefix,
                                               sizeof(g_river_cloud.xiaozhi_preview_stable_prefix),
                                               event->stable_prefix);
    }
    if (event->source != NULL && event->source[0] != '\0') {
        river_cloud_xiaozhi_copy_optional_text(g_river_cloud.xiaozhi_preview_source,
                                               sizeof(g_river_cloud.xiaozhi_preview_source),
                                               event->source);
    }
    if (event->reason != NULL && event->reason[0] != '\0') {
        river_cloud_xiaozhi_copy_optional_text(
            g_river_cloud.xiaozhi_preview_endpoint_reason,
            sizeof(g_river_cloud.xiaozhi_preview_endpoint_reason),
            event->reason);
    }
    if (event->audio_offset_ms != 0U) {
        g_river_cloud.xiaozhi_preview_audio_offset_ms = event->audio_offset_ms;
    }

    switch (event->type) {
    case RIVER_XIAOZHI_EVENT_INPUT_SPEECH_START:
        g_river_cloud.xiaozhi_preview_speech_started = true;
        break;
    case RIVER_XIAOZHI_EVENT_INPUT_PREVIEW:
        g_river_cloud.xiaozhi_preview_final = event->is_final;
        break;
    case RIVER_XIAOZHI_EVENT_INPUT_ENDPOINT:
        g_river_cloud.xiaozhi_preview_endpoint_candidate = event->candidate;
        break;
    default:
        break;
    }
}

bool river_cloud_xiaozhi_playback_allows_vad_open(void)
{
    river_voice_duplex_ready_eval_t eval;
    const char *fallback_reason;

    river_cloud_xiaozhi_get_duplex_ready_eval(&eval);
    fallback_reason = river_cloud_xiaozhi_duplex_fallback_reason(&eval);
    if (fallback_reason == NULL) {
        river_cloud_xiaozhi_playback_note_duplex_ready();
    }
    return fallback_reason == NULL;
}

bool river_cloud_xiaozhi_capture_held_by_playback(
    const river_voice_duplex_ready_eval_t *eval,
    const char **fallback_reason)
{
    const char *resolved_reason = river_cloud_xiaozhi_duplex_fallback_reason(eval);

    if (fallback_reason != NULL) {
        *fallback_reason = resolved_reason;
    }

    return river_cloud_xiaozhi_playback_lane_engaged() && resolved_reason != NULL;
}

void river_cloud_xiaozhi_apply_tts_start_round_policy(void)
{
    river_voice_duplex_ready_eval_t duplex_eval;
    const char *fallback_reason;

    river_cloud_xiaozhi_get_duplex_ready_eval(&duplex_eval);
    fallback_reason = river_cloud_xiaozhi_duplex_fallback_reason(&duplex_eval);

    if (fallback_reason == NULL) {
        RIVER_LOGI("xiaozhi tts_start keeps local round open: duplex_default_on=yes duplex_ready=yes reason=%s aec=%s ref_state=%s ref_activity=%s ref_peak=%u ref_ratio_q15=%u stream=%s stop_pending=%s close_pending=%s",
                   river_voice_runtime_duplex_ready_reason_name(duplex_eval.reason),
                   river_voice_runtime_aec_gate_reason_name(duplex_eval.aec_reason),
                   river_reference_service_state_name(duplex_eval.reference_state),
                   river_voice_runtime_reference_activity_name(duplex_eval.reference_activity),
                   (unsigned int)duplex_eval.native_reference_peak,
                   (unsigned int)duplex_eval.native_reference_ratio_q15,
                   g_river_cloud.stream_active ? "yes" : "no",
                   g_river_cloud.xiaozhi_listen_stop_pending ? "yes" : "no",
                   g_river_cloud.xiaozhi_local_close_pending ? "yes" : "no");
        return;
    }

    river_cloud_xiaozhi_note_semantic_fallback(fallback_reason);
    RIVER_LOGI("xiaozhi tts_start falls back to round close: fallback=%s duplex_default_on=%s duplex_ready=%s reason=%s aec=%s ref_state=%s ref_activity=%s ref_peak=%u ref_ratio_q15=%u ref_queue=%lu/%lu ref_age_ms=%lu",
               fallback_reason,
               river_xiaozhi_duplex_default_on_allowed() ? "yes" : "no",
               duplex_eval.ready ? "yes" : "no",
               river_voice_runtime_duplex_ready_reason_name(duplex_eval.reason),
               river_voice_runtime_aec_gate_reason_name(duplex_eval.aec_reason),
               river_reference_service_state_name(duplex_eval.reference_state),
               river_voice_runtime_reference_activity_name(duplex_eval.reference_activity),
               (unsigned int)duplex_eval.native_reference_peak,
               (unsigned int)duplex_eval.native_reference_ratio_q15,
               (unsigned long)duplex_eval.reference_queue_frames,
               (unsigned long)duplex_eval.reference_queue_peak_frames,
               (unsigned long)duplex_eval.reference_last_write_age_ms);
    river_cloud_xiaozhi_close_local_round_for_cause(
        RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_SERVER_RESPONSE,
        "tts_start");
}

void river_cloud_xiaozhi_apply_tts_stop_round_policy(void)
{
    river_cloud_xiaozhi_finalize_pending_text("tts_stop");
    river_cloud_xiaozhi_close_local_round_for_cause(
        RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_LOCAL_RESOLVED,
        "tts_stop");
    river_cloud_xiaozhi_window_touch(
        RIVER_CLOUD_XIAOZHI_POST_TTS_SILENCE_CLOSE_MS,
        "tts_stop");
    river_cloud_xiaozhi_arm_playback_stop(RIVER_CLOUD_XIAOZHI_PLAYBACK_DRAIN_MS);
}

void river_cloud_xiaozhi_apply_reopen_overlap_round_policy(void)
{
    river_cloud_xiaozhi_note_round_finish_request("reopen_overlap");
    river_cloud_xiaozhi_close_local_round_for_cause(
        RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_LOCAL_RESOLVED,
        "reopen_overlap");
}

void river_cloud_xiaozhi_apply_llm_round_policy(void)
{
    river_cloud_xiaozhi_finalize_pending_text("llm");
    river_cloud_xiaozhi_close_local_round_for_cause(
        RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_LOCAL_RESOLVED,
        "llm");
}

void river_cloud_xiaozhi_apply_post_stop_result_round_policy(void)
{
    if (g_river_cloud.xiaozhi_pending_text_finalized ||
        g_river_cloud.xiaozhi_asr_round_final_seen) {
        river_cloud_xiaozhi_close_local_round_for_cause(
            RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_LOCAL_RESOLVED,
            "post_stop_result");
    }
}

void river_cloud_xiaozhi_apply_listen_stop_completion_round_policy(void)
{
    g_river_cloud.xiaozhi_listen_stop_pending = false;
    if (g_river_cloud.xiaozhi_local_close_pending) {
        g_river_cloud.xiaozhi_listening = false;
        river_cloud_xiaozhi_apply_post_stop_result_round_policy();
        return;
    }

    river_cloud_xiaozhi_emit_session_closed();
    river_cloud_xiaozhi_round_finish(NULL);
    river_runtime_stats_snapshot("asr_stream_finish");
}

void river_cloud_xiaozhi_maybe_finalize_listen_stop(uint32_t queued_frames, size_t accum_bytes)
{
    river_status_t status;

    if (!g_river_cloud.xiaozhi_listen_stop_pending || queued_frames != 0U || accum_bytes != 0U) {
        return;
    }

    if (river_xiaozhi_session_open() && g_river_cloud.xiaozhi_listening) {
        status = river_cloud_xiaozhi_request_listen_stop();
        if (status != RIVER_OK) {
            return;
        }
    }

    river_cloud_xiaozhi_apply_listen_stop_completion_round_policy();
}

bool river_cloud_xiaozhi_uplink_keepalive_needed(uint32_t queued_frames)
{
    return queued_frames > 0U || g_river_cloud.xiaozhi_listen_stop_pending;
}

bool river_cloud_xiaozhi_uplink_send_ready(void)
{
    return river_xiaozhi_session_open() && g_river_cloud.xiaozhi_listening;
}

void river_cloud_xiaozhi_apply_transport_closed_terminal_policy(void)
{
    river_cloud_xiaozhi_finalize_pending_text("transport_closed");
    river_cloud_xiaozhi_round_finish("transport_closed");
    river_cloud_xiaozhi_window_abort_local("transport_closed");
    if (river_cloud_xiaozhi_playback_has_work()) {
        (void)river_cloud_xiaozhi_playback_abort_for_cause(
            RIVER_CLOUD_XIAOZHI_PLAYBACK_ABORT_TRANSPORT_CLOSED,
            NULL);
    }
    river_cloud_xiaozhi_reset_transport_state(true);
    river_cloud_xiaozhi_check_window_timeout();
}

void river_cloud_xiaozhi_apply_network_lost_terminal_policy(void)
{
    if (river_cloud_xiaozhi_playback_has_work()) {
        (void)river_cloud_xiaozhi_playback_abort_for_cause(
            RIVER_CLOUD_XIAOZHI_PLAYBACK_ABORT_NETWORK_LOST,
            NULL);
    }
    river_cloud_xiaozhi_round_finish("network_lost");
    river_cloud_xiaozhi_reset_transport_state(false);
    (void)river_cloud_xiaozhi_request_close_session();
}

void river_cloud_xiaozhi_apply_bridge_close_terminal_policy(void)
{
    if (river_cloud_xiaozhi_playback_has_work()) {
        (void)river_cloud_xiaozhi_playback_abort_for_cause(
            RIVER_CLOUD_XIAOZHI_PLAYBACK_ABORT_BRIDGE_CLOSE,
            NULL);
    }
    river_cloud_xiaozhi_round_finish("bridge_close");
    river_cloud_xiaozhi_reset_transport_state(false);
    (void)river_cloud_xiaozhi_request_close_session();
}

uint32_t river_cloud_xiaozhi_open_hold_frames_required(void)
{
    if (!g_river_cloud.xiaozhi_window_active ||
        river_cloud_xiaozhi_playback_allows_vad_open()) {
        return RIVER_CLOUD_XIAOZHI_OPEN_HOLD_FRAMES;
    }

    return RIVER_CLOUD_XIAOZHI_NOREF_OPEN_HOLD_FRAMES;
}

static bool river_cloud_xiaozhi_no_ref_reopen_ready(bool is_speech)
{
    uint64_t now_ms;
    bool guard_active;

    if (!g_river_cloud.xiaozhi_window_active ||
        river_cloud_xiaozhi_playback_allows_vad_open()) {
        return true;
    }

    if (river_cloud_xiaozhi_playback_lane_engaged()) {
        return false;
    }

    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    guard_active =
        g_river_cloud.xiaozhi_no_ref_reopen_guard_deadline_ms != 0U &&
        now_ms < g_river_cloud.xiaozhi_no_ref_reopen_guard_deadline_ms;

    if (g_river_cloud.xiaozhi_no_ref_reopen_rearm) {
        if (is_speech) {
            g_river_cloud.xiaozhi_no_ref_reopen_silence_frames = 0U;
        } else if (g_river_cloud.xiaozhi_no_ref_reopen_silence_frames <
                   RIVER_CLOUD_XIAOZHI_NOREF_REARM_SILENCE_FRAMES) {
            g_river_cloud.xiaozhi_no_ref_reopen_silence_frames++;
        }

        if (!guard_active &&
            g_river_cloud.xiaozhi_no_ref_reopen_silence_frames >=
                RIVER_CLOUD_XIAOZHI_NOREF_REARM_SILENCE_FRAMES) {
            g_river_cloud.xiaozhi_no_ref_reopen_rearm = false;
            g_river_cloud.xiaozhi_no_ref_reopen_silence_frames = 0U;
            g_river_cloud.xiaozhi_no_ref_reopen_guard_deadline_ms = 0U;
            RIVER_LOGI("xiaozhi no_ref reopen rearmed after silence: frames=%u",
                       (unsigned int)RIVER_CLOUD_XIAOZHI_NOREF_REARM_SILENCE_FRAMES);
            return true;
        }
    }

    if (!guard_active) {
        g_river_cloud.xiaozhi_no_ref_reopen_guard_deadline_ms = 0U;
    }

    return !guard_active && !g_river_cloud.xiaozhi_no_ref_reopen_rearm;
}

river_status_t river_cloud_xiaozhi_maybe_start_followup_round(bool is_speech,
                                                              uint32_t pre_roll_frames,
                                                              bool *opened)
{
    river_status_t status;
    uint32_t open_hold_frames;

    if (opened != NULL) {
        *opened = false;
    }

    if (g_river_cloud.xiaozhi_listen_stop_pending) {
        return RIVER_ERR_BUSY;
    }

    if (!g_river_cloud.xiaozhi_window_active &&
        river_cloud_xiaozhi_idle_requires_wakeword()) {
        g_river_cloud.xiaozhi_open_speech_frames = 0U;
        return RIVER_OK;
    }

    if (!river_cloud_xiaozhi_no_ref_reopen_ready(is_speech)) {
        g_river_cloud.xiaozhi_open_speech_frames = 0U;
        return RIVER_OK;
    }

    open_hold_frames = river_cloud_xiaozhi_open_hold_frames_required();
    if (!is_speech) {
        g_river_cloud.xiaozhi_open_speech_frames = 0U;
        return RIVER_OK;
    }

    if (g_river_cloud.xiaozhi_open_speech_frames < UINT32_MAX) {
        g_river_cloud.xiaozhi_open_speech_frames++;
    }
    if (g_river_cloud.xiaozhi_open_speech_frames < open_hold_frames) {
        return RIVER_OK;
    }

    status = river_cloud_xiaozhi_start_followup_round(pre_roll_frames);
    if (status != RIVER_OK) {
        g_river_cloud.xiaozhi_open_speech_frames = 0U;
        return status;
    }

    g_river_cloud.xiaozhi_open_speech_frames = 0U;
    if (opened != NULL) {
        *opened = true;
    }
    return RIVER_OK;
}

static bool river_cloud_xiaozhi_output_speaking_active(void)
{
    return river_cloud_xiaozhi_playback_output_active() ||
           (strcmp(g_river_cloud.xiaozhi_output_state, "speaking") == 0);
}

void river_cloud_xiaozhi_apply_open_and_listen_session_policy(void)
{
    g_river_cloud.xiaozhi_listening = true;
    g_river_cloud.xiaozhi_listen_stop_pending = false;
}

bool river_cloud_xiaozhi_duplex_soft_endpoint_enabled(void)
{
    return river_cloud_xiaozhi_output_speaking_active() &&
           river_cloud_xiaozhi_playback_allows_vad_open();
}

bool river_cloud_xiaozhi_duplex_speaking_uplink_continuation_active(void)
{
    return g_river_cloud.stream_active &&
           river_cloud_xiaozhi_duplex_soft_endpoint_enabled();
}

void river_cloud_xiaozhi_note_round_finish_request(const char *reason)
{
    if (!g_river_cloud.xiaozhi_asr_round_active ||
        reason == NULL ||
        reason[0] == '\0') {
        return;
    }

    snprintf(g_river_cloud.xiaozhi_asr_round_close_reason,
             sizeof(g_river_cloud.xiaozhi_asr_round_close_reason),
             "%s",
             reason);
}

void river_cloud_xiaozhi_clear_endpoint_soft_close_state(void)
{
    g_river_cloud.xiaozhi_endpoint_soft_close_pending = false;
    g_river_cloud.xiaozhi_endpoint_soft_close_deadline_ms = 0U;
    g_river_cloud.xiaozhi_endpoint_soft_close_reason[0] = '\0';
}

void river_cloud_xiaozhi_cancel_endpoint_soft_close(const char *trigger)
{
    if (!g_river_cloud.xiaozhi_endpoint_soft_close_pending) {
        return;
    }

    RIVER_LOGI("xiaozhi deferred local close cancelled: trigger=%s reason=%s",
               trigger != NULL ? trigger : "-",
               g_river_cloud.xiaozhi_endpoint_soft_close_reason[0] != '\0' ?
                   g_river_cloud.xiaozhi_endpoint_soft_close_reason :
                   "-");
    river_cloud_xiaozhi_clear_endpoint_soft_close_state();
}

void river_cloud_xiaozhi_note_interrupt_hint(const char *trigger, const char *reason)
{
    if (!river_cloud_xiaozhi_duplex_soft_endpoint_enabled()) {
        return;
    }

    RIVER_LOGI("xiaozhi interrupt hint: trigger=%s reason=%s preview_id=%s stream=%s playback=%s",
               trigger != NULL ? trigger : "-",
               reason != NULL ? reason : "-",
               g_river_cloud.xiaozhi_preview_id[0] != '\0' ?
                   g_river_cloud.xiaozhi_preview_id :
                   "-",
               g_river_cloud.stream_active ? "yes" : "no",
               g_river_cloud.xiaozhi_playback_active ? "yes" : "no");
}

river_status_t river_cloud_xiaozhi_interrupt_tts(const char *reason)
{
    river_status_t status = RIVER_ERR_UNSUPPORTED;

    if (!(river_cloud_xiaozhi_playback_has_work() ||
          g_river_cloud.xiaozhi_listening ||
          river_xiaozhi_session_open())) {
        return RIVER_ERR_UNSUPPORTED;
    }

    status = RIVER_OK;
    if (river_cloud_xiaozhi_playback_has_work()) {
        status = river_cloud_xiaozhi_playback_abort_for_cause(
            RIVER_CLOUD_XIAOZHI_PLAYBACK_ABORT_INTERRUPT,
            reason);
    }
    if (river_xiaozhi_session_open()) {
        (void)river_cloud_xiaozhi_request_abort(reason);
    }
    RIVER_LOGI("tts interrupt requested: reason=%s", reason != NULL ? reason : "-");
    return status;
}

void river_cloud_xiaozhi_round_begin(uint32_t pre_roll_frames)
{
    g_river_cloud.xiaozhi_asr_round_id++;
    g_river_cloud.xiaozhi_asr_round_active = true;
    g_river_cloud.xiaozhi_asr_round_started_ms =
        (uint32_t)rtos_time_get_current_system_time_ms();
    g_river_cloud.xiaozhi_asr_round_first_packet_ms = 0U;
    g_river_cloud.xiaozhi_asr_round_pre_roll_frames = pre_roll_frames;
    g_river_cloud.xiaozhi_asr_round_packets_sent = 0U;
    g_river_cloud.xiaozhi_asr_round_partial_count = 0U;
    g_river_cloud.xiaozhi_asr_round_final_count = 0U;
    g_river_cloud.xiaozhi_asr_round_partial_seen = false;
    g_river_cloud.xiaozhi_asr_round_final_seen = false;
    g_river_cloud.xiaozhi_asr_round_busy_base = g_river_cloud.xiaozhi_uplink_busy_count;
    g_river_cloud.xiaozhi_asr_round_fail_base = g_river_cloud.xiaozhi_uplink_fail_count;
    g_river_cloud.xiaozhi_asr_round_stale_drop_base =
        g_river_cloud.xiaozhi_uplink_stale_dropped;
    g_river_cloud.xiaozhi_asr_round_ring_drop_base =
        g_river_cloud.xiaozhi_uplink_ring_dropped;
    g_river_cloud.xiaozhi_asr_round_close_reason[0] = '\0';
    RIVER_LOGI("xiaozhi asr round begin: id=%lu sid=%s pre_roll_frames=%lu",
               (unsigned long)g_river_cloud.xiaozhi_asr_round_id,
               river_cloud_xiaozhi_current_sid() != NULL ?
                   river_cloud_xiaozhi_current_sid() :
                   "-",
               (unsigned long)pre_roll_frames);
}

void river_cloud_xiaozhi_round_note_packet_sent(void)
{
    if (!g_river_cloud.xiaozhi_asr_round_active) {
        return;
    }

    if (g_river_cloud.xiaozhi_asr_round_first_packet_ms == 0U) {
        g_river_cloud.xiaozhi_asr_round_first_packet_ms =
            (uint32_t)rtos_time_get_current_system_time_ms();
    }
    if (g_river_cloud.xiaozhi_asr_round_packets_sent < UINT32_MAX) {
        g_river_cloud.xiaozhi_asr_round_packets_sent++;
    }
}

void river_cloud_xiaozhi_arm_endpoint_soft_close(const char *trigger, const char *reason)
{
    if (!river_cloud_xiaozhi_duplex_soft_endpoint_enabled() ||
        !g_river_cloud.stream_active ||
        g_river_cloud.xiaozhi_listen_stop_pending ||
        g_river_cloud.xiaozhi_endpoint_soft_close_pending) {
        return;
    }

    river_cloud_xiaozhi_copy_optional_text(g_river_cloud.xiaozhi_endpoint_soft_close_reason,
                                           sizeof(g_river_cloud.xiaozhi_endpoint_soft_close_reason),
                                           reason);
    g_river_cloud.xiaozhi_endpoint_soft_close_pending = true;
    g_river_cloud.xiaozhi_endpoint_soft_close_deadline_ms =
        (uint64_t)rtos_time_get_current_system_time_ms() +
        (uint64_t)RIVER_CLOUD_XIAOZHI_ENDPOINT_SOFT_CLOSE_DEFER_MS;
    RIVER_LOGI("xiaozhi hint-only endpoint: trigger=%s reason=%s wait_ms=%u stream=%s playback=%s",
               trigger != NULL ? trigger : "-",
               g_river_cloud.xiaozhi_endpoint_soft_close_reason[0] != '\0' ?
                   g_river_cloud.xiaozhi_endpoint_soft_close_reason :
                   "-",
               (unsigned int)RIVER_CLOUD_XIAOZHI_ENDPOINT_SOFT_CLOSE_DEFER_MS,
               g_river_cloud.stream_active ? "yes" : "no",
               g_river_cloud.xiaozhi_playback_active ? "yes" : "no");
}

static const char *river_cloud_xiaozhi_stream_finish_cause_name(
    river_cloud_xiaozhi_stream_finish_cause_t cause)
{
    switch (cause) {
    case RIVER_CLOUD_XIAOZHI_STREAM_FINISH_POST_ROLL:
        return "post_roll";
    case RIVER_CLOUD_XIAOZHI_STREAM_FINISH_ENDPOINT_TIMEOUT:
        return "endpoint_soft_close_timeout";
    default:
        return "unknown";
    }
}

bool river_cloud_xiaozhi_poll_endpoint_soft_close_timeout(char *reason, size_t reason_size)
{
    uint64_t now_ms;

    if (!g_river_cloud.xiaozhi_endpoint_soft_close_pending) {
        return false;
    }

    if (!g_river_cloud.stream_active || g_river_cloud.xiaozhi_listen_stop_pending) {
        river_cloud_xiaozhi_clear_endpoint_soft_close_state();
        return false;
    }

    if (river_cloud_xiaozhi_duplex_speaking_uplink_continuation_active()) {
        return false;
    }

    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    if (now_ms < g_river_cloud.xiaozhi_endpoint_soft_close_deadline_ms) {
        return false;
    }

    river_cloud_xiaozhi_copy_optional_text(
        reason,
        reason_size,
        g_river_cloud.xiaozhi_endpoint_soft_close_reason[0] != '\0' ?
            g_river_cloud.xiaozhi_endpoint_soft_close_reason :
            "server_endpoint_candidate");
    RIVER_LOGI("xiaozhi deferred local close resolved: trigger=endpoint_soft_close_timeout reason=%s",
               reason != NULL && reason[0] != '\0' ? reason : "-");
    return true;
}

void river_cloud_xiaozhi_commit_active_stream_finish_for_cause(
    river_cloud_xiaozhi_stream_finish_cause_t cause,
    const char *detail_reason)
{
    const char *cause_name = river_cloud_xiaozhi_stream_finish_cause_name(cause);

    if (!g_river_cloud.stream_active) {
        return;
    }

    RIVER_LOGI("xiaozhi active stream finish: cause=%s trigger=%s partial=%s final=%s",
               cause_name,
               detail_reason != NULL && detail_reason[0] != '\0' ? detail_reason : cause_name,
               g_river_cloud.xiaozhi_asr_round_partial_seen ? "yes" : "no",
               g_river_cloud.xiaozhi_asr_round_final_seen ? "yes" : "no");
    river_cloud_xiaozhi_clear_endpoint_soft_close_state();
    river_cloud_xiaozhi_note_round_finish_request("post_roll");
    g_river_cloud.xiaozhi_listen_stop_pending = true;
    river_cloud_xiaozhi_finalize_pending_text("post_roll");
    river_cloud_xiaozhi_window_touch(RIVER_CLOUD_XIAOZHI_POST_COMMIT_RESPONSE_WAIT_MS,
                                     "post_commit_wait");
    RIVER_LOGI("xiaozhi response wait armed after commit: timeout_ms=%u",
               (unsigned int)RIVER_CLOUD_XIAOZHI_POST_COMMIT_RESPONSE_WAIT_MS);
    if (river_cloud_xiaozhi_should_defer_local_close()) {
        river_cloud_xiaozhi_arm_local_close_defer();
    } else {
        g_river_cloud.xiaozhi_local_close_pending = false;
        g_river_cloud.xiaozhi_local_close_deadline_ms = 0U;
    }
    g_river_cloud.stream_active = false;
    g_river_cloud.silence_frames = 0U;
    g_river_cloud.stream_started_ms = 0U;
    g_river_cloud.xiaozhi_open_speech_frames = 0U;
    g_river_cloud.xiaozhi_uplink_accum_bytes = 0U;
    g_river_cloud.xiaozhi_uplink_retry_valid = false;
    g_river_cloud.xiaozhi_uplink_next_send_ms = 0U;
    g_river_cloud.xiaozhi_uplink_busy_streak = 0U;
    river_cloud_pre_roll_reset();
}

static river_status_t river_cloud_xiaozhi_flush_accumulator_padded(void)
{
    size_t frame_bytes;
    river_status_t status;

    if (!g_river_cloud.xiaozhi_uplink_ring.initialized ||
        g_river_cloud.xiaozhi_uplink_accum_bytes == 0U) {
        return RIVER_OK;
    }

    frame_bytes = RIVER_CLOUD_XIAOZHI_UPLINK_PCM_FRAME_MAX;
    if (g_river_cloud.xiaozhi_uplink_accum_bytes < frame_bytes) {
        memset(g_river_cloud.xiaozhi_uplink_accum + g_river_cloud.xiaozhi_uplink_accum_bytes,
               0,
               frame_bytes - g_river_cloud.xiaozhi_uplink_accum_bytes);
    }
    status = river_cloud_xiaozhi_queue_uplink_packet(g_river_cloud.xiaozhi_uplink_accum,
                                                     frame_bytes);
    g_river_cloud.xiaozhi_uplink_accum_bytes = 0U;
    return status;
}

void river_cloud_xiaozhi_complete_active_stream_finish(
    river_cloud_xiaozhi_stream_finish_cause_t cause,
    const char *detail_reason)
{
    if (!g_river_cloud.stream_active) {
        return;
    }

    (void)river_cloud_xiaozhi_flush_accumulator_padded();
    river_cloud_xiaozhi_commit_active_stream_finish_for_cause(cause, detail_reason);
    river_cloud_xiaozhi_maybe_finalize_listen_stop(
        river_cloud_xiaozhi_uplink_ready_frames(),
        g_river_cloud.xiaozhi_uplink_accum_bytes);
}

bool river_cloud_xiaozhi_should_defer_local_close(void)
{
    return g_river_cloud.xiaozhi_asr_round_active &&
           !g_river_cloud.xiaozhi_asr_round_partial_seen &&
           !g_river_cloud.xiaozhi_asr_round_final_seen &&
           !g_river_cloud.xiaozhi_pending_text_valid;
}

void river_cloud_xiaozhi_arm_local_close_defer(void)
{
    if (g_river_cloud.xiaozhi_local_close_pending) {
        return;
    }

    g_river_cloud.xiaozhi_local_close_pending = true;
    g_river_cloud.xiaozhi_local_close_deadline_ms =
        (uint64_t)rtos_time_get_current_system_time_ms() +
        (uint64_t)RIVER_CLOUD_XIAOZHI_LOCAL_CLOSE_DEFER_MS;
    RIVER_LOGI("xiaozhi local close deferred: wait_ms=%u",
               (unsigned int)RIVER_CLOUD_XIAOZHI_LOCAL_CLOSE_DEFER_MS);
}

static void river_cloud_xiaozhi_copy_semantic_text(char *dst,
                                                   size_t dst_size,
                                                   const char *src)
{
    if (dst == NULL || dst_size == 0U) {
        return;
    }
    if (src == NULL || src[0] == '\0') {
        dst[0] = '\0';
        return;
    }

    snprintf(dst, dst_size, "%s", src);
}

static bool river_cloud_xiaozhi_update_semantic_text(char *dst,
                                                     size_t dst_size,
                                                     const char *src)
{
    char next_value[96];

    if (dst == NULL || dst_size == 0U) {
        return false;
    }

    if (src == NULL || src[0] == '\0') {
        if (dst[0] == '\0') {
            return false;
        }
        dst[0] = '\0';
        return true;
    }

    memset(next_value, 0, sizeof(next_value));
    snprintf(next_value, sizeof(next_value), "%s", src);
    if (strcmp(dst, next_value) == 0) {
        return false;
    }

    snprintf(dst, dst_size, "%s", next_value);
    return true;
}

static const char *river_cloud_xiaozhi_optional_bool_text(bool known, bool enabled)
{
    if (!known) {
        return "-";
    }
    return enabled ? "yes" : "no";
}

static const char *river_cloud_xiaozhi_round_close_cause_name(
    river_cloud_xiaozhi_round_close_cause_t cause)
{
    switch (cause) {
    case RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_LOCAL_RESOLVED:
        return "local_resolved";
    case RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_SERVER_RESPONSE:
        return "server_response_started";
    default:
        return "unknown";
    }
}

void river_cloud_xiaozhi_clear_pending_text(void)
{
    g_river_cloud.xiaozhi_pending_text_valid = false;
    g_river_cloud.xiaozhi_pending_text_finalized = false;
    g_river_cloud.xiaozhi_pending_text[0] = '\0';
}

void river_cloud_xiaozhi_clear_preview_state(void)
{
    g_river_cloud.xiaozhi_preview_speech_started = false;
    g_river_cloud.xiaozhi_preview_endpoint_candidate = false;
    g_river_cloud.xiaozhi_preview_final = false;
    g_river_cloud.xiaozhi_preview_audio_offset_ms = 0U;
    g_river_cloud.xiaozhi_preview_id[0] = '\0';
    g_river_cloud.xiaozhi_preview_text[0] = '\0';
    g_river_cloud.xiaozhi_preview_stable_prefix[0] = '\0';
    g_river_cloud.xiaozhi_preview_source[0] = '\0';
    g_river_cloud.xiaozhi_preview_endpoint_reason[0] = '\0';
}

void river_cloud_xiaozhi_clear_turn_semantics_state(void)
{
    g_river_cloud.xiaozhi_turn_accepted = false;
    g_river_cloud.xiaozhi_transport_barge_in_enabled_known = false;
    g_river_cloud.xiaozhi_transport_barge_in_enabled = false;
    g_river_cloud.xiaozhi_turn_id[0] = '\0';
    g_river_cloud.xiaozhi_accept_reason[0] = '\0';
    g_river_cloud.xiaozhi_input_state[0] = '\0';
    g_river_cloud.xiaozhi_output_state[0] = '\0';
    g_river_cloud.xiaozhi_semantic_fallback_reason[0] = '\0';
}

void river_cloud_xiaozhi_window_touch(uint32_t duration_ms, const char *reason)
{
    uint64_t now_ms;
    bool was_active;

    if (duration_ms == 0U) {
        return;
    }

    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    was_active = g_river_cloud.xiaozhi_window_active;
    g_river_cloud.xiaozhi_window_active = true;
    g_river_cloud.xiaozhi_window_deadline_ms = now_ms + (uint64_t)duration_ms;
    if (!was_active) {
        RIVER_LOGI("xiaozhi conversation window opened: source=%s mode=auto timeout_ms=%lu",
                   reason != NULL ? reason : "-",
                   (unsigned long)duration_ms);
    }
}

void river_cloud_xiaozhi_window_close(const char *reason)
{
    if (!g_river_cloud.xiaozhi_window_active && !river_xiaozhi_session_open()) {
        return;
    }

    g_river_cloud.xiaozhi_window_active = false;
    g_river_cloud.xiaozhi_window_deadline_ms = 0U;
    g_river_cloud.xiaozhi_open_speech_frames = 0U;
    g_river_cloud.xiaozhi_listen_stop_pending = false;
    if (g_river_cloud.xiaozhi_listening && river_xiaozhi_session_open()) {
        (void)river_cloud_xiaozhi_request_listen_stop();
    }
    g_river_cloud.xiaozhi_listening = false;
    river_cloud_pre_roll_reset();
    (void)river_cloud_xiaozhi_request_close_session();
    g_river_cloud.xiaozhi_session_id[0] = '\0';
    RIVER_LOGI("xiaozhi conversation window closed: reason=%s",
               reason != NULL ? reason : "-");
    river_cloud_request_state_sync(reason != NULL ? reason : "window_closed");
}

void river_cloud_xiaozhi_window_abort_local(const char *reason)
{
    bool should_log;

    should_log = g_river_cloud.xiaozhi_window_active ||
                 g_river_cloud.xiaozhi_listening ||
                 g_river_cloud.xiaozhi_listen_stop_pending ||
                 g_river_cloud.xiaozhi_open_speech_frames != 0U;

    g_river_cloud.xiaozhi_window_active = false;
    g_river_cloud.xiaozhi_window_deadline_ms = 0U;
    g_river_cloud.xiaozhi_open_speech_frames = 0U;
    g_river_cloud.xiaozhi_listen_stop_pending = false;
    river_cloud_pre_roll_reset();
    if (should_log) {
        RIVER_LOGW("xiaozhi conversation window aborted: reason=%s",
                   reason != NULL ? reason : "-");
    }
    river_cloud_request_state_sync(reason != NULL ? reason : "window_aborted");
}

void river_cloud_xiaozhi_copy_session_id_from_transport(void)
{
    const char *sid;

    sid = river_xiaozhi_session_id();
    if (sid != NULL && sid[0] != '\0') {
        snprintf(g_river_cloud.xiaozhi_session_id,
                 sizeof(g_river_cloud.xiaozhi_session_id),
                 "%s",
                 sid);
    }
}

const char *river_cloud_xiaozhi_current_sid(void)
{
    return g_river_cloud.xiaozhi_session_id[0] != '\0' ?
               g_river_cloud.xiaozhi_session_id :
               river_xiaozhi_session_id();
}

void river_cloud_xiaozhi_refresh_turn_semantics(const char *trigger)
{
    const char *turn_id = river_xiaozhi_last_turn_id();
    const char *accept_reason = river_xiaozhi_last_accept_reason();
    const char *input_state = river_xiaozhi_last_input_state();
    const char *output_state = river_xiaozhi_last_output_state();
    bool barge_in_known = river_xiaozhi_last_barge_in_enabled_known();
    bool barge_in_enabled = river_xiaozhi_last_barge_in_enabled();
    bool accepted_before = g_river_cloud.xiaozhi_turn_accepted;
    bool accept_reason_present = accept_reason != NULL && accept_reason[0] != '\0';
    bool turn_id_changed;
    bool accept_reason_changed = false;
    bool input_state_changed;
    bool output_state_changed;
    bool barge_in_changed;

    turn_id_changed = river_cloud_xiaozhi_update_semantic_text(g_river_cloud.xiaozhi_turn_id,
                                                               sizeof(g_river_cloud.xiaozhi_turn_id),
                                                               turn_id);
    if (accept_reason_present) {
        accept_reason_changed = river_cloud_xiaozhi_update_semantic_text(
            g_river_cloud.xiaozhi_accept_reason,
            sizeof(g_river_cloud.xiaozhi_accept_reason),
            accept_reason);
        g_river_cloud.xiaozhi_turn_accepted = true;
        if (strcmp(g_river_cloud.xiaozhi_semantic_fallback_reason,
                   "await_accept_reason") == 0) {
            g_river_cloud.xiaozhi_semantic_fallback_reason[0] = '\0';
        }
    }
    input_state_changed = river_cloud_xiaozhi_update_semantic_text(
        g_river_cloud.xiaozhi_input_state,
        sizeof(g_river_cloud.xiaozhi_input_state),
        input_state);
    output_state_changed = river_cloud_xiaozhi_update_semantic_text(
        g_river_cloud.xiaozhi_output_state,
        sizeof(g_river_cloud.xiaozhi_output_state),
        output_state);
    barge_in_changed =
        (g_river_cloud.xiaozhi_transport_barge_in_enabled_known != barge_in_known) ||
        (barge_in_known &&
         (g_river_cloud.xiaozhi_transport_barge_in_enabled != barge_in_enabled));
    g_river_cloud.xiaozhi_transport_barge_in_enabled_known = barge_in_known;
    g_river_cloud.xiaozhi_transport_barge_in_enabled = barge_in_known && barge_in_enabled;

    if (accept_reason_present &&
        (!accepted_before || accept_reason_changed || turn_id_changed ||
         input_state_changed || output_state_changed || barge_in_changed)) {
        RIVER_LOGI("xiaozhi turn accepted: trigger=%s accept_reason=%s turn_id=%s input_state=%s output_state=%s barge_in_enabled=%s",
                   trigger != NULL ? trigger : "-",
                   g_river_cloud.xiaozhi_accept_reason[0] != '\0' ?
                       g_river_cloud.xiaozhi_accept_reason :
                       "-",
                   g_river_cloud.xiaozhi_turn_id[0] != '\0' ?
                       g_river_cloud.xiaozhi_turn_id :
                       "-",
                   g_river_cloud.xiaozhi_input_state[0] != '\0' ?
                       g_river_cloud.xiaozhi_input_state :
                       "-",
                   g_river_cloud.xiaozhi_output_state[0] != '\0' ?
                       g_river_cloud.xiaozhi_output_state :
                       "-",
                   river_cloud_xiaozhi_optional_bool_text(
                       g_river_cloud.xiaozhi_transport_barge_in_enabled_known,
                       g_river_cloud.xiaozhi_transport_barge_in_enabled));
    } else if (!accept_reason_present &&
               (turn_id_changed || input_state_changed || output_state_changed ||
                barge_in_changed)) {
        RIVER_LOGI("xiaozhi turn semantics updated before accept: trigger=%s turn_id=%s input_state=%s output_state=%s barge_in_enabled=%s",
                   trigger != NULL ? trigger : "-",
                   g_river_cloud.xiaozhi_turn_id[0] != '\0' ?
                       g_river_cloud.xiaozhi_turn_id :
                       "-",
                   g_river_cloud.xiaozhi_input_state[0] != '\0' ?
                       g_river_cloud.xiaozhi_input_state :
                       "-",
                   g_river_cloud.xiaozhi_output_state[0] != '\0' ?
                       g_river_cloud.xiaozhi_output_state :
                       "-",
                   river_cloud_xiaozhi_optional_bool_text(
                       g_river_cloud.xiaozhi_transport_barge_in_enabled_known,
                       g_river_cloud.xiaozhi_transport_barge_in_enabled));
    }
}

bool river_cloud_xiaozhi_turn_accepted(void)
{
    return g_river_cloud.xiaozhi_turn_accepted &&
           g_river_cloud.xiaozhi_accept_reason[0] != '\0';
}

void river_cloud_xiaozhi_note_semantic_fallback(const char *reason)
{
    river_voice_duplex_ready_eval_t duplex_eval;
    const char *duplex_default_reason;
    bool duplex_default_on;

    if (reason == NULL || reason[0] == '\0') {
        return;
    }
    if (strcmp(g_river_cloud.xiaozhi_semantic_fallback_reason, reason) == 0) {
        return;
    }

    river_cloud_xiaozhi_get_duplex_ready_eval(&duplex_eval);
    duplex_default_reason = river_xiaozhi_duplex_default_fallback_reason();
    duplex_default_on = duplex_default_reason == NULL;
    river_cloud_xiaozhi_copy_semantic_text(g_river_cloud.xiaozhi_semantic_fallback_reason,
                                           sizeof(g_river_cloud.xiaozhi_semantic_fallback_reason),
                                           reason);
    RIVER_LOGW("xiaozhi fallback: reason=%s accepted=%s accept_reason=%s input_state=%s output_state=%s duplex_default_on=%s duplex_default_reason=%s duplex_ready=%s duplex_reason=%s sid=%s",
               g_river_cloud.xiaozhi_semantic_fallback_reason,
               river_cloud_xiaozhi_turn_accepted() ? "yes" : "no",
               g_river_cloud.xiaozhi_accept_reason[0] != '\0' ?
                   g_river_cloud.xiaozhi_accept_reason :
                   "-",
               g_river_cloud.xiaozhi_input_state[0] != '\0' ?
                   g_river_cloud.xiaozhi_input_state :
                   "-",
               g_river_cloud.xiaozhi_output_state[0] != '\0' ?
                   g_river_cloud.xiaozhi_output_state :
                   "-",
               duplex_default_on ? "yes" : "no",
               duplex_default_reason != NULL ? duplex_default_reason : "-",
               duplex_eval.ready ? "yes" : "no",
               river_voice_runtime_duplex_ready_reason_name(duplex_eval.reason),
               river_cloud_xiaozhi_current_sid() != NULL ? river_cloud_xiaozhi_current_sid() : "-");
}

static bool river_cloud_xiaozhi_pending_text_ready_for_finalize(void)
{
    return g_river_cloud.xiaozhi_pending_text_valid &&
           !g_river_cloud.xiaozhi_pending_text_finalized &&
           g_river_cloud.xiaozhi_pending_text[0] != '\0';
}

static void river_cloud_xiaozhi_commit_pending_text_finalization(const char *trigger)
{
    g_river_cloud.xiaozhi_pending_text_finalized = true;
    RIVER_LOGI("xiaozhi accepted turn final text: trigger=%s accept_reason=%s turn_id=%s text=%s",
               trigger != NULL ? trigger : "-",
               g_river_cloud.xiaozhi_accept_reason[0] != '\0' ?
                   g_river_cloud.xiaozhi_accept_reason :
                   "-",
               g_river_cloud.xiaozhi_turn_id[0] != '\0' ?
                   g_river_cloud.xiaozhi_turn_id :
                   "-",
               g_river_cloud.xiaozhi_pending_text);
    river_cloud_emit_asr_result(RIVER_CLOUD_ASR_EVENT_FINAL,
                                g_river_cloud.xiaozhi_pending_text,
                                river_cloud_xiaozhi_current_sid(),
                                NULL,
                                0,
                                true);
}

void river_cloud_xiaozhi_finalize_pending_text_if_turn_accepted(const char *trigger)
{
    if (!river_cloud_xiaozhi_pending_text_ready_for_finalize()) {
        return;
    }

    river_cloud_xiaozhi_refresh_turn_semantics(trigger);
    if (!river_cloud_xiaozhi_turn_accepted()) {
        return;
    }

    river_cloud_xiaozhi_commit_pending_text_finalization(trigger);
}

void river_cloud_xiaozhi_finalize_pending_text(const char *trigger)
{
    river_cloud_xiaozhi_refresh_turn_semantics(trigger);

    if (!river_cloud_xiaozhi_pending_text_ready_for_finalize()) {
        return;
    }

    if (!river_cloud_xiaozhi_turn_accepted()) {
        river_cloud_xiaozhi_note_semantic_fallback("await_accept_reason");
        RIVER_LOGI("xiaozhi pending text waits for accepted turn: trigger=%s text=%s",
                   trigger != NULL ? trigger : "-",
                   g_river_cloud.xiaozhi_pending_text);
        return;
    }

    river_cloud_xiaozhi_commit_pending_text_finalization(trigger);
}

void river_cloud_xiaozhi_emit_session_started(void)
{
    if (!g_river_cloud.xiaozhi_listening) {
        return;
    }

    river_cloud_emit_asr_result(RIVER_CLOUD_ASR_EVENT_SESSION_STARTED,
                                NULL,
                                river_cloud_xiaozhi_current_sid(),
                                NULL,
                                0,
                                false);
}

void river_cloud_xiaozhi_emit_session_closed(void)
{
    if (!g_river_cloud.xiaozhi_listening && !g_river_cloud.xiaozhi_local_close_pending) {
        return;
    }

    g_river_cloud.xiaozhi_listening = false;
    g_river_cloud.xiaozhi_local_close_pending = false;
    g_river_cloud.xiaozhi_local_close_deadline_ms = 0U;
    g_river_cloud.xiaozhi_endpoint_soft_close_pending = false;
    g_river_cloud.xiaozhi_endpoint_soft_close_deadline_ms = 0U;
    g_river_cloud.xiaozhi_endpoint_soft_close_reason[0] = '\0';
    river_cloud_emit_asr_result(RIVER_CLOUD_ASR_EVENT_SESSION_CLOSED,
                                NULL,
                                river_cloud_xiaozhi_current_sid(),
                                NULL,
                                0,
                                false);
}

void river_cloud_xiaozhi_round_finish(const char *reason)
{
    uint32_t now_ms;
    uint32_t duration_ms;
    uint32_t first_packet_delay_ms = 0U;
    uint32_t audio_ms = 0U;
    uint32_t realtime_gap_ms = 0U;
    uint32_t pace_pct = 0U;
    uint32_t busy_delta;
    uint32_t fail_delta;
    uint32_t stale_delta;
    uint32_t ring_drop_delta;
    const char *close_reason;

    if (!g_river_cloud.xiaozhi_asr_round_active) {
        return;
    }

    now_ms = (uint32_t)rtos_time_get_current_system_time_ms();
    duration_ms = now_ms - g_river_cloud.xiaozhi_asr_round_started_ms;
    if (g_river_cloud.xiaozhi_asr_round_first_packet_ms != 0U &&
        g_river_cloud.xiaozhi_asr_round_first_packet_ms >=
            g_river_cloud.xiaozhi_asr_round_started_ms) {
        first_packet_delay_ms =
            g_river_cloud.xiaozhi_asr_round_first_packet_ms -
            g_river_cloud.xiaozhi_asr_round_started_ms;
    }
    if (g_river_cloud.xiaozhi_asr_round_packets_sent <=
        (UINT32_MAX / RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS)) {
        audio_ms = g_river_cloud.xiaozhi_asr_round_packets_sent *
                   RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS;
    } else {
        audio_ms = UINT32_MAX;
    }
    if (duration_ms > audio_ms) {
        realtime_gap_ms = duration_ms - audio_ms;
    }
    if (duration_ms != 0U) {
        pace_pct = (uint32_t)(((uint64_t)audio_ms * 100ULL) / (uint64_t)duration_ms);
    }
    busy_delta = g_river_cloud.xiaozhi_uplink_busy_count -
                 g_river_cloud.xiaozhi_asr_round_busy_base;
    fail_delta = g_river_cloud.xiaozhi_uplink_fail_count -
                 g_river_cloud.xiaozhi_asr_round_fail_base;
    stale_delta = g_river_cloud.xiaozhi_uplink_stale_dropped -
                  g_river_cloud.xiaozhi_asr_round_stale_drop_base;
    ring_drop_delta = g_river_cloud.xiaozhi_uplink_ring_dropped -
                      g_river_cloud.xiaozhi_asr_round_ring_drop_base;
    close_reason = (reason != NULL && reason[0] != '\0') ?
                       reason :
                       (g_river_cloud.xiaozhi_asr_round_close_reason[0] != '\0' ?
                            g_river_cloud.xiaozhi_asr_round_close_reason :
                            "-");

    RIVER_LOGI("xiaozhi asr round finish: id=%lu sid=%s reason=%s duration_ms=%lu audio_ms=%lu realtime_gap_ms=%lu pace_pct=%lu first_packet_delay_ms=%lu pre_roll_frames=%lu packets=%lu busy=%lu fail=%lu stale_drop=%lu ring_drop=%lu partial=%lu final=%lu seen[partial=%s final=%s]",
               (unsigned long)g_river_cloud.xiaozhi_asr_round_id,
               river_cloud_xiaozhi_current_sid() != NULL ?
                   river_cloud_xiaozhi_current_sid() :
                   "-",
               close_reason,
               (unsigned long)duration_ms,
               (unsigned long)audio_ms,
               (unsigned long)realtime_gap_ms,
               (unsigned long)pace_pct,
               (unsigned long)first_packet_delay_ms,
               (unsigned long)g_river_cloud.xiaozhi_asr_round_pre_roll_frames,
               (unsigned long)g_river_cloud.xiaozhi_asr_round_packets_sent,
               (unsigned long)busy_delta,
               (unsigned long)fail_delta,
               (unsigned long)stale_delta,
               (unsigned long)ring_drop_delta,
               (unsigned long)g_river_cloud.xiaozhi_asr_round_partial_count,
               (unsigned long)g_river_cloud.xiaozhi_asr_round_final_count,
               g_river_cloud.xiaozhi_asr_round_partial_seen ? "yes" : "no",
               g_river_cloud.xiaozhi_asr_round_final_seen ? "yes" : "no");

    g_river_cloud.xiaozhi_asr_round_active = false;
    g_river_cloud.xiaozhi_asr_round_started_ms = 0U;
    g_river_cloud.xiaozhi_asr_round_first_packet_ms = 0U;
    g_river_cloud.xiaozhi_asr_round_pre_roll_frames = 0U;
    g_river_cloud.xiaozhi_asr_round_packets_sent = 0U;
    g_river_cloud.xiaozhi_asr_round_partial_count = 0U;
    g_river_cloud.xiaozhi_asr_round_final_count = 0U;
    g_river_cloud.xiaozhi_asr_round_partial_seen = false;
    g_river_cloud.xiaozhi_asr_round_final_seen = false;
    g_river_cloud.xiaozhi_asr_round_close_reason[0] = '\0';
}

void river_cloud_xiaozhi_close_local_round_for_cause(
    river_cloud_xiaozhi_round_close_cause_t cause,
    const char *detail_reason)
{
    const char *cause_name = river_cloud_xiaozhi_round_close_cause_name(cause);
    const char *resolved_reason =
        (detail_reason != NULL && detail_reason[0] != '\0') ? detail_reason : cause_name;

    switch (cause) {
    case RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_LOCAL_RESOLVED:
        if (!g_river_cloud.xiaozhi_local_close_pending ||
            g_river_cloud.xiaozhi_listen_stop_pending) {
            return;
        }

        RIVER_LOGI("xiaozhi local round close: cause=%s trigger=%s partial=%s final=%s",
                   cause_name,
                   resolved_reason,
                   g_river_cloud.xiaozhi_asr_round_partial_seen ? "yes" : "no",
                   g_river_cloud.xiaozhi_asr_round_final_seen ? "yes" : "no");
        river_cloud_xiaozhi_emit_session_closed();
        river_cloud_xiaozhi_round_finish(resolved_reason);
        river_runtime_stats_snapshot("asr_stream_finish");
        return;

    case RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_SERVER_RESPONSE:
        if (!g_river_cloud.stream_active &&
            !g_river_cloud.xiaozhi_listen_stop_pending &&
            !g_river_cloud.xiaozhi_local_close_pending) {
            return;
        }

        RIVER_LOGI("xiaozhi local round close: cause=%s trigger=%s stream=%s stop_pending=%s close_pending=%s",
                   cause_name,
                   resolved_reason,
                   g_river_cloud.stream_active ? "yes" : "no",
                   g_river_cloud.xiaozhi_listen_stop_pending ? "yes" : "no",
                   g_river_cloud.xiaozhi_local_close_pending ? "yes" : "no");
        g_river_cloud.stream_active = false;
        g_river_cloud.silence_frames = 0U;
        g_river_cloud.stream_started_ms = 0U;
        g_river_cloud.xiaozhi_open_speech_frames = 0U;
        g_river_cloud.xiaozhi_listen_stop_pending = false;
        g_river_cloud.xiaozhi_local_close_pending = false;
        g_river_cloud.xiaozhi_local_close_deadline_ms = 0U;
        g_river_cloud.xiaozhi_endpoint_soft_close_pending = false;
        g_river_cloud.xiaozhi_endpoint_soft_close_deadline_ms = 0U;
        g_river_cloud.xiaozhi_endpoint_soft_close_reason[0] = '\0';
        g_river_cloud.xiaozhi_uplink_accum_bytes = 0U;
        g_river_cloud.xiaozhi_uplink_retry_valid = false;
        g_river_cloud.xiaozhi_uplink_next_send_ms = 0U;
        g_river_cloud.xiaozhi_uplink_busy_streak = 0U;
        river_audio_frame_ring_reset(&g_river_cloud.xiaozhi_uplink_ring);
        river_cloud_pre_roll_reset();

        river_cloud_xiaozhi_emit_session_closed();
        river_cloud_xiaozhi_round_finish(resolved_reason);
        river_runtime_stats_snapshot("asr_stream_finish");
        return;

    default:
        return;
    }
}

void river_cloud_xiaozhi_check_local_close_timeout(void)
{
    uint64_t now_ms;

    if (!g_river_cloud.xiaozhi_local_close_pending ||
        g_river_cloud.xiaozhi_local_close_deadline_ms == 0U) {
        return;
    }

    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    if (now_ms < g_river_cloud.xiaozhi_local_close_deadline_ms) {
        return;
    }

    river_cloud_xiaozhi_close_local_round_for_cause(
        RIVER_CLOUD_XIAOZHI_ROUND_CLOSE_LOCAL_RESOLVED,
        "timeout");
}

void river_cloud_xiaozhi_reset_transport_state(bool emit_session_closed)
{
    if (emit_session_closed) {
        river_cloud_xiaozhi_emit_session_closed();
    } else {
        g_river_cloud.xiaozhi_listening = false;
    }

    g_river_cloud.stream_active = false;
    g_river_cloud.silence_frames = 0U;
    g_river_cloud.stream_started_ms = 0U;
    g_river_cloud.xiaozhi_open_speech_frames = 0U;
    g_river_cloud.xiaozhi_window_active = false;
    g_river_cloud.xiaozhi_window_deadline_ms = 0U;
    g_river_cloud.xiaozhi_listen_stop_pending = false;
    g_river_cloud.xiaozhi_uplink_accum_bytes = 0U;
    g_river_cloud.xiaozhi_uplink_retry_valid = false;
    g_river_cloud.xiaozhi_uplink_next_send_ms = 0U;
    g_river_cloud.xiaozhi_uplink_busy_streak = 0U;
    g_river_cloud.xiaozhi_local_close_pending = false;
    g_river_cloud.xiaozhi_local_close_deadline_ms = 0U;
    g_river_cloud.xiaozhi_endpoint_soft_close_pending = false;
    g_river_cloud.xiaozhi_endpoint_soft_close_deadline_ms = 0U;
    g_river_cloud.xiaozhi_endpoint_soft_close_reason[0] = '\0';
    river_audio_frame_ring_reset(&g_river_cloud.xiaozhi_uplink_ring);
    river_cloud_xiaozhi_reset_downlink_state();
    river_cloud_xiaozhi_clear_pending_text();
    river_cloud_xiaozhi_clear_preview_state();
    river_cloud_xiaozhi_clear_turn_semantics_state();
    river_cloud_xiaozhi_clear_playback_meta_state();
    river_cloud_pre_roll_reset();
    g_river_cloud.xiaozhi_no_ref_reopen_rearm = false;
    g_river_cloud.xiaozhi_no_ref_reopen_silence_frames = 0U;
    g_river_cloud.xiaozhi_no_ref_reopen_guard_deadline_ms = 0U;
    g_river_cloud.xiaozhi_session_id[0] = '\0';
}

void river_cloud_xiaozhi_check_window_timeout(void)
{
    uint64_t now_ms;

    if (!g_river_cloud.xiaozhi_window_active ||
        g_river_cloud.xiaozhi_window_deadline_ms == 0U) {
        return;
    }

    now_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    if (now_ms < g_river_cloud.xiaozhi_window_deadline_ms) {
        return;
    }

    if (g_river_cloud.stream_active || river_cloud_xiaozhi_playback_has_work()) {
        return;
    }

    river_cloud_xiaozhi_window_close("followup_timeout");
}

river_status_t river_cloud_xiaozhi_open_session_and_listen(void)
{
    river_status_t status;

    status = river_cloud_xiaozhi_request_open_and_listen("auto");
    if (status != RIVER_OK) {
        return status;
    }
    river_xiaozhi_clear_session_update_cache();
    river_cloud_xiaozhi_copy_session_id_from_transport();

    /*
     * A fresh follow-up listen/asr round must re-arm the conversation window.
     * Otherwise the shorter post-TTS tail timer can expire while the user has
     * already started the next utterance, causing the websocket to close
     * immediately after this ASR round finishes.
     */
    river_cloud_xiaozhi_window_touch(RIVER_CLOUD_XIAOZHI_WAKE_WINDOW_FOLLOWUP_MS,
                                     "asr_session_start");
    river_cloud_xiaozhi_clear_pending_text();
    river_cloud_xiaozhi_clear_preview_state();
    river_cloud_xiaozhi_clear_turn_semantics_state();
    river_cloud_xiaozhi_clear_playback_meta_state();
    river_cloud_xiaozhi_emit_session_started();
    return RIVER_OK;
}

river_status_t river_cloud_xiaozhi_start_followup_round(uint32_t pre_roll_frames)
{
    river_status_t status;

    if (!river_xiaozhi_session_open()) {
        river_cloud_xiaozhi_window_abort_local("followup_transport_unavailable");
        river_cloud_xiaozhi_check_window_timeout();
        return RIVER_ERR_BUSY;
    }

    if (g_river_cloud.xiaozhi_local_close_pending) {
        river_cloud_xiaozhi_apply_reopen_overlap_round_policy();
    }

    status = river_cloud_xiaozhi_open_session_and_listen();
    if (status != RIVER_OK) {
        return status;
    }
    if (g_river_cloud.xiaozhi_asr_round_active) {
        river_cloud_xiaozhi_round_finish("reopen_overlap");
    }
    river_cloud_xiaozhi_round_begin(pre_roll_frames);
    return RIVER_OK;
}

river_status_t river_cloud_xiaozhi_begin_conversation_window(const char *source)
{
    river_status_t status;
    const char *reason = (source != NULL && source[0] != '\0') ? source : "-";
    bool was_listening;

    if (!g_river_cloud.initialized || !g_river_cloud.xiaozhi_enabled) {
        return RIVER_ERR_UNSUPPORTED;
    }

    river_cloud_start_sntp_if_needed();
    river_cloud_seed_time_from_build_if_needed();
    if (!river_wifi_station_is_connected()) {
        river_cloud_log_wake_admission_deferred_once(RIVER_ERR_BUSY);
        return RIVER_ERR_BUSY;
    }
    if (!river_cloud_wake_admission_time_ready()) {
        river_cloud_log_wake_admission_deferred_once(RIVER_ERR_BUSY);
        return RIVER_ERR_BUSY;
    }
    river_cloud_reset_wake_admission_deferred_state();
    if (RIVER_CLOUD_BUSINESS_TIME_WAIT_REQUIRED &&
        !river_cloud_time_ready() &&
        g_river_cloud.time_seeded_from_build &&
        !g_river_cloud.wake_admission_estimate_announced) {
        g_river_cloud.wake_admission_estimate_announced = true;
        RIVER_LOGI("wake admission proceeding with build-seeded utc estimate");
    }

    RIVER_LOGI("xiaozhi wake admission begin: source=%s session=%s listening=%s window=%s sid=%s",
               reason,
               river_xiaozhi_session_open() ? "open" : "closed",
               g_river_cloud.xiaozhi_listening ? "yes" : "no",
               g_river_cloud.xiaozhi_window_active ? "open" : "closed",
               river_cloud_xiaozhi_current_sid() != NULL ? river_cloud_xiaozhi_current_sid() : "-");
    RIVER_LOGI("xiaozhi wake admission policy: source=%s default_on=%s default_reason=%s voice_collaboration=%s server_endpoint=%s/%s preview_events=%s playback_ack=%s",
               reason,
               river_xiaozhi_duplex_default_on_allowed() ? "yes" : "no",
               river_xiaozhi_duplex_default_fallback_reason() != NULL ?
                   river_xiaozhi_duplex_default_fallback_reason() :
                   "-",
               river_xiaozhi_discovery_voice_collaboration_advertised() ? "yes" : "no",
               river_xiaozhi_discovery_server_endpoint_available() ? "yes" : "no",
               river_xiaozhi_discovery_server_endpoint_enabled() ? "yes" : "no",
               river_xiaozhi_preview_events_negotiated() ? "yes" : "no",
               river_xiaozhi_playback_ack_mode_negotiated() != NULL ?
                   river_xiaozhi_playback_ack_mode_negotiated() :
                   "-");

    was_listening = g_river_cloud.xiaozhi_listening;
    status = river_cloud_xiaozhi_request_open_and_listen("auto");
    if (status != RIVER_OK) {
        if (!river_xiaozhi_session_open()) {
            RIVER_LOGW("xiaozhi wake admission open_session failed: source=%s status=%d last_err=%s",
                       reason,
                       (int)status,
                       river_xiaozhi_last_error() != NULL ? river_xiaozhi_last_error() : "-");
        } else {
            RIVER_LOGW("xiaozhi wake admission listen_start failed: source=%s status=%d sid=%s last_err=%s",
                       reason,
                       (int)status,
                       river_cloud_xiaozhi_current_sid() != NULL ? river_cloud_xiaozhi_current_sid() : "-",
                       river_xiaozhi_last_error() != NULL ? river_xiaozhi_last_error() : "-");
        }
        return status;
    }
    river_xiaozhi_clear_session_update_cache();
    river_cloud_xiaozhi_copy_session_id_from_transport();
    RIVER_LOGI("xiaozhi wake admission transport ready: source=%s sid=%s",
               reason,
               river_cloud_xiaozhi_current_sid() != NULL ? river_cloud_xiaozhi_current_sid() : "-");
    if (!was_listening && g_river_cloud.xiaozhi_listening) {
        RIVER_LOGI("xiaozhi wake admission listen_start sent: source=%s sid=%s",
                   reason,
                   river_cloud_xiaozhi_current_sid() != NULL ? river_cloud_xiaozhi_current_sid() : "-");
    }

    river_cloud_xiaozhi_window_touch(RIVER_CLOUD_XIAOZHI_WAKE_WINDOW_FOLLOWUP_MS, reason);
    g_river_cloud.xiaozhi_open_speech_frames = 0U;
    river_cloud_pre_roll_reset();
    RIVER_LOGI("xiaozhi wake admission ready: source=%s listening=%s window=%s sid=%s",
               reason,
               g_river_cloud.xiaozhi_listening ? "yes" : "no",
               g_river_cloud.xiaozhi_window_active ? "open" : "closed",
               river_cloud_xiaozhi_current_sid() != NULL ? river_cloud_xiaozhi_current_sid() : "-");
    return RIVER_OK;
}
#endif
