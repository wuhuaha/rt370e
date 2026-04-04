/* 唤醒词模块公共接口。 */
#ifndef AMEBA_RIVER_VOICE_KWS_H
#define AMEBA_RIVER_VOICE_KWS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "river/river_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RIVER_VOICE_KWS_TENSOR_DUMP_FEATURE_F32 = 0,
    RIVER_VOICE_KWS_TENSOR_DUMP_INPUT_RAW = 1,
    RIVER_VOICE_KWS_TENSOR_DUMP_OUTPUT_RAW = 2
} river_voice_kws_tensor_dump_buffer_t;

river_status_t river_voice_kws_init(void);
bool river_voice_kws_active(void);
river_status_t river_voice_kws_submit_frame(const uint8_t *data,
                                            size_t bytes,
                                            bool vad_valid,
                                            bool is_speech);
void river_voice_kws_set_local_debug_mode(bool enabled);
bool river_voice_kws_local_debug_mode_enabled(void);
const char *river_voice_kws_wake_handoff_block_reason(void);
river_status_t river_voice_kws_request_tensor_dump_next(void);
void river_voice_kws_cancel_tensor_dump(void);
void river_voice_kws_clear_tensor_dump(void);
void river_voice_kws_dump_tensor_meta(void);
river_status_t river_voice_kws_dump_tensor_chunk(
    river_voice_kws_tensor_dump_buffer_t buffer,
    uint32_t chunk_index);
river_status_t river_voice_kws_run_alignment_sample(bool emit_dump);
void river_voice_kws_dump_alignment_status(void);
void river_voice_kws_dump_profile(void);
void river_voice_kws_dump_status(void);

#ifdef __cplusplus
}
#endif

#endif
