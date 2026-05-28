/* 项目侧 Wi-Fi 初始化入口：保留 WHC/LwIP 初始化，禁止 SDK 自动 wifi_on。 */
#include "platform_autoconf.h"

#ifdef CONFIG_WLAN

#include "ameba_soc.h"
#include "os_wrapper.h"
#include "wifi_intf_drv_to_upper.h"

#if defined(CONFIG_WHC_HOST) && !defined(CONFIG_WHC_INTF_IPC)
#include "whc_host_api.h"
#elif defined(CONFIG_WHC_INTF_IPC)
#include "whc_ipc.h"
#endif

#if defined(CONFIG_WHC_CMD_PATH) && !defined(CONFIG_WHC_HOST)
#include "whc_dev_api.h"
#endif

#if defined(CONFIG_LWIP_LAYER) && CONFIG_LWIP_LAYER
#include "lwip_netconf.h"
#endif

#include "river/river_log.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.wifi"

#define RIVER_WIFI_INIT_STACK_SIZE ((512U + 768U) * 4U)

void __real_wifi_init(void);

#if defined(CONFIG_WHC_HOST) || defined(CONFIG_WHC_DEV)
static void river_wifi_init_thread(void *param)
{
    UNUSED(param);

#if defined(CONFIG_WHC_HOST)
#if defined(CONFIG_LWIP_LAYER) && CONFIG_LWIP_LAYER
    RIVER_LOGI("sdk wifi_init override: LwIP_Init start");
    LwIP_Init();
    RIVER_LOGI("sdk wifi_init override: LwIP_Init done");
#endif

    RIVER_LOGI("sdk wifi_init override: whc_host_init start");
    whc_host_init();
    RIVER_LOGI("sdk wifi_init override: whc_host_init done; sdk auto wifi_on skipped");

#elif defined(CONFIG_WHC_DEV)
#if defined(CONFIG_LWIP_LAYER) && defined(CONFIG_WHC_DEV_TCPIP_KEEPALIVE)
    RIVER_LOGI("sdk wifi_init override: dev LwIP_Init start");
    LwIP_Init();
    RIVER_LOGI("sdk wifi_init override: dev LwIP_Init done");
#endif

#ifdef CONFIG_WHC_CMD_PATH
    whc_dev_init_cmd_path_task();
#endif

    RIVER_LOGI("sdk wifi_init override: whc_dev_init start");
    whc_dev_init();
    RIVER_LOGI("sdk wifi_init override: whc_dev_init done");
#endif

    rtos_task_delete(NULL);
}
#endif

void __wrap_wifi_init(void)
{
#if defined(CONFIG_WHC_HOST) || defined(CONFIG_WHC_DEV)
    RIVER_LOGI("sdk wifi_init override active: init WHC only, app owns wifi_on");
    wifi_set_rom2flash();
    if (rtos_task_create(NULL,
                         "river_wifi_init",
                         river_wifi_init_thread,
                         NULL,
                         RIVER_WIFI_INIT_STACK_SIZE,
                         5) != RTK_SUCCESS) {
        RIVER_LOGE("sdk wifi_init override failed to create init task");
    }
#else
    __real_wifi_init();
#endif
}

#endif
