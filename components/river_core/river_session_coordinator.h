/* 会话协调器对外接口：负责连接语音事件、播放事件和云端结果。 */
#ifndef AMEBA_RIVER_SESSION_COORDINATOR_H
#define AMEBA_RIVER_SESSION_COORDINATOR_H

#include "river/river_cloud.h"
#include "river/river_playback_service.h"
#include "river/river_types.h"
#include "river/river_voice.h"

river_status_t river_session_coordinator_init(void);
void river_session_coordinator_on_voice_event(const river_voice_event_t *event);
void river_session_coordinator_on_cloud_asr_result(const river_cloud_asr_result_t *result,
                                                   void *user_data);

#endif
