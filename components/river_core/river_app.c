#include <stdio.h>
#include <string.h>

#include "basic_types.h"
#include "os_wrapper.h"

#include "FreeRTOS.h"
#include "task.h"

#include "river/river_app.h"
#include "river/river_cloud.h"
#include "river/river_log.h"
#include "river/river_online_control.h"
#include "river/river_voice.h"
#include "river/river_voice_vad_reference.h"
#include "river/river_wifi_station.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.app"

static char g_river_app_last_partial[192];

static void river_app_on_voice_event(const river_voice_event_t *event)
{
    if (event == 0) {
        return;
    }

    RIVER_LOGD("voice event=%d confidence=%d", event->type, event->confidence);
    if (event->text != 0) {
        RIVER_LOGD("voice text=%s", event->text);
    }
}

static void river_app_on_cloud_asr_result(const river_cloud_asr_result_t *result,
                                          void *user_data)
{
    (void)user_data;

    if (result == NULL) {
        return;
    }

    switch (result->type) {
    case RIVER_CLOUD_ASR_EVENT_PARTIAL:
        if ((result->text != NULL) && (result->text[0] != '\0') &&
            (strcmp(g_river_app_last_partial, result->text) != 0)) {
            snprintf(g_river_app_last_partial,
                     sizeof(g_river_app_last_partial),
                     "%s",
                     result->text);
            RIVER_LOGI("asr provider=%s partial sid=%s text=%s",
                       result->provider_name != NULL ? result->provider_name : "-",
                       result->sid != NULL ? result->sid : "-",
                       result->text);
        }
        break;
    case RIVER_CLOUD_ASR_EVENT_FINAL:
        g_river_app_last_partial[0] = '\0';
        RIVER_LOGI("asr provider=%s final sid=%s text=%s",
                   result->provider_name != NULL ? result->provider_name : "-",
                   result->sid != NULL ? result->sid : "-",
                   result->text != NULL ? result->text : "-");
        break;
    case RIVER_CLOUD_ASR_EVENT_ERROR:
        g_river_app_last_partial[0] = '\0';
        RIVER_LOGE("asr provider=%s error code=%d sid=%s msg=%s",
                   result->provider_name != NULL ? result->provider_name : "-",
                   result->code,
                   result->sid != NULL ? result->sid : "-",
                   result->message != NULL ? result->message : "-");
        break;
    case RIVER_CLOUD_ASR_EVENT_SESSION_STARTED:
        g_river_app_last_partial[0] = '\0';
        RIVER_LOGI("asr provider=%s session started sid=%s",
                   result->provider_name != NULL ? result->provider_name : "-",
                   result->sid != NULL ? result->sid : "-");
        break;
    case RIVER_CLOUD_ASR_EVENT_SESSION_CLOSED:
        g_river_app_last_partial[0] = '\0';
        RIVER_LOGI("asr provider=%s session closed sid=%s",
                   result->provider_name != NULL ? result->provider_name : "-",
                   result->sid != NULL ? result->sid : "-");
        break;
    default:
        break;
    }
}

static void river_app_boot_monitor_task(void *param)
{
    (void)param;

    RIVER_LOGI("boot monitor active: waiting for network and time sync...");

    while (1) {
        if (river_wifi_station_is_connected()) {
            break;
        }
        rtos_time_delay_ms(500);
    }

    /* Wait one more second for network stack/background SDK logic to settle */
    rtos_time_delay_ms(1000);

    RIVER_LOGI("network and time ready; initiating deferred voice startup");

    if (river_voice_frontend_init() != RIVER_OK) {
        RIVER_LOGE("deferred voice frontend init failed");
        rtos_task_delete(NULL);
        return;
    }

#ifdef CONFIG_RIVER_VAD_PROBE_AUTOSTART
    RIVER_LOGI("starting deferred vad probe...");
    if (river_voice_vad_probe_start() != RIVER_OK) {
        RIVER_LOGE("deferred vad probe start failed");
    }
#endif

#ifdef CONFIG_RIVER_AUDIO_ECHO_AUTOSTART
    RIVER_LOGI("starting deferred audio echo...");
    if (river_voice_echo_start() != RIVER_OK) {
        RIVER_LOGE("deferred audio echo start failed");
    }
#endif

    river_app_print_status();
    RIVER_LOGI("deferred boot sequence complete");
    rtos_task_delete(NULL);
}

river_status_t river_app_boot(void)
{
    rtos_task_t boot_task;

    RIVER_LOGI("ameba-river boot");
    RIVER_LOGI("target=RTL8730E");

    river_voice_frontend_set_handler(river_app_on_voice_event);

    if (river_wifi_station_init() != RIVER_OK) {
        return RIVER_ERR_UNSUPPORTED;
    }

    if (river_cloud_adapter_init() != RIVER_OK) {
        return RIVER_ERR_UNSUPPORTED;
    }

    river_cloud_adapter_set_result_handler(river_app_on_cloud_asr_result, NULL);

    if (river_online_control_init() != RIVER_OK) {
        return RIVER_ERR_UNSUPPORTED;
    }

#ifdef CONFIG_RIVER_AUDIO_ECHO_DIAG_DEFAULT_ON
    river_voice_echo_set_diag_enabled(true);
#endif

#ifdef CONFIG_RIVER_VAD_PROBE_DIAG_DEFAULT_ON
    river_voice_vad_probe_set_diag_enabled(true);
#endif

    /* Create background monitor to handle deferred voice startup */
    if (rtos_task_create(&boot_task, "river_boot_mon", river_app_boot_monitor_task, 
                         NULL, 1024 * 8, 2) != RTK_SUCCESS) {
        RIVER_LOGE("failed to create boot monitor task");
        return RIVER_ERR_IO;
    }

    return RIVER_OK;
}

void river_app_print_status(void)
{
    HeapStats_t stats;
    int frag_ratio = 0;

    vPortGetHeapStats(&stats);
    if (stats.xAvailableHeapSpaceInBytes > 0) {
        frag_ratio = (int)((1.0f - ((float)stats.xSizeOfLargestFreeBlockInBytes / (float)stats.xAvailableHeapSpaceInBytes)) * 100.0f);
    }

    RIVER_LOGI("system heap audit:");
    RIVER_LOGI("  total_free: %lu B", (unsigned long)stats.xAvailableHeapSpaceInBytes);
    RIVER_LOGI("  largest_block: %lu B", (unsigned long)stats.xSizeOfLargestFreeBlockInBytes);
    RIVER_LOGI("  min_ever_free: %lu B", (unsigned long)stats.xMinimumEverFreeBytesRemaining);
    RIVER_LOGI("  free_blocks: %lu", (unsigned long)stats.xNumberOfFreeBlocks);
    RIVER_LOGI("  fragmentation: %d%%", frag_ratio);

    RIVER_LOGI("local_frontend=%s", river_voice_frontend_mode_name());
    RIVER_LOGI("local_preproc=%s", river_voice_preproc_backend_name());
    RIVER_LOGI("local_preproc_profile=%s", river_voice_preproc_profile_name());
    RIVER_LOGI("local_detector=%s", river_voice_detector_backend_name());
    RIVER_LOGI("local_detector_reference=%s", river_voice_vad_reference_name());
    RIVER_LOGI("local_playback_ref=%s", river_voice_ref_backend_name());
    RIVER_LOGI("local_segment_sink=%s", river_voice_segment_sink_name());
#ifdef CONFIG_RIVER_OFFLINE_ASR_RESERVED
    RIVER_LOGI("offline_asr=reserved");
#else
    RIVER_LOGI("offline_asr=disabled");
#endif
#ifdef CONFIG_RIVER_ONLINE_CONTROL_EN
    RIVER_LOGI("online_control=enabled");
#else
    RIVER_LOGI("online_control=disabled");
#endif
    river_voice_echo_dump_status();
    river_voice_vad_probe_dump_status();
    river_wifi_station_dump_status();
    river_cloud_adapter_dump_status();
    river_online_control_dump_status();
}
