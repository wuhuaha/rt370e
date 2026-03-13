#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "lwip_netconf.h"
#include "os_wrapper.h"
#include "os_wrapper_memory.h"
#include "wifi_api.h"
#include "wifi_api_ext.h"
#include "wifi_fast_connect.h"

#include "river/river_log.h"
#include "river/river_wifi_credentials.h"
#include "river/river_wifi_station.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.wifi"

#define RIVER_WIFI_STA_TASK_STACK    (1024U * 4U)
#define RIVER_WIFI_STA_TASK_PRIORITY 3U
#define RIVER_WIFI_STA_RETRY_MS      5000U
#define RIVER_WIFI_STA_IDLE_WAIT_MS  3000U
#define RIVER_WIFI_STA_DHCP_WAIT_MS  25000U
#define RIVER_WIFI_STA_DHCP_POLL_MS  500U

typedef struct {
    const char *ssid;
    const char *pass;
} river_wifi_cred_internal_t;

static const river_wifi_cred_internal_t g_river_creds[] = {
    { RIVER_WIFI_STA_PRIMARY_SSID, RIVER_WIFI_STA_PRIMARY_PASSWORD },
    { RIVER_WIFI_STA_SECONDARY_SSID, RIVER_WIFI_STA_SECONDARY_PASSWORD }
};

typedef struct {
    bool initialized;
    bool connected;
    bool connecting;
    uint32_t connect_attempts;
    uint32_t connect_successes;
    int current_cred_index;
    rtos_task_t task;
} river_wifi_station_context_t;

static river_wifi_station_context_t g_river_wifi_station;

static bool river_wifi_station_has_ipv4(void)
{
    uint8_t *ip = LwIP_GetIP(NETIF_WLAN_STA_INDEX);
    return (ip[0] != 0 || ip[1] != 0 || ip[2] != 0 || ip[3] != 0);
}

static bool river_wifi_station_joined(void)
{
    u8 join_status = RTW_JOINSTATUS_UNKNOWN;
    if (wifi_get_join_status(&join_status) != RTK_SUCCESS) {
        return false;
    }
    return join_status == RTW_JOINSTATUS_SUCCESS;
}

static void river_wifi_station_mark_connected(void)
{
    if (!g_river_wifi_station.connected) {
        g_river_wifi_station.connected = true;
        g_river_wifi_station.connecting = false;
        g_river_wifi_station.connect_successes++;
        
        /* Kick SNTP on success */
        extern void sntp_init(void);
        sntp_init();
    }
}

static bool river_wifi_station_wait_driver_idle(uint32_t timeout_ms)
{
    uint32_t waited_ms = 0U;
    u8 join_status = RTW_JOINSTATUS_UNKNOWN;

    while (waited_ms < timeout_ms) {
        if (wifi_get_join_status(&join_status) != RTK_SUCCESS) {
            return false;
        }
        if (join_status == RTW_JOINSTATUS_UNKNOWN || join_status == RTW_JOINSTATUS_SUCCESS) {
            return true;
        }
        rtos_time_delay_ms(100);
        waited_ms += 100U;
    }
    return false;
}

static bool river_wifi_station_wait_for_ipv4(uint32_t timeout_ms)
{
    uint32_t waited_ms = 0U;
    RIVER_LOGI("waiting for ipv4...");
    
    wifi_set_lps_enable(0);
    
    while (waited_ms < timeout_ms) {
        if (!river_wifi_station_joined()) return false;
        if (river_wifi_station_has_ipv4()) {
            river_wifi_station_mark_connected();
            return true;
        }
        
        if (waited_ms == 0 || waited_ms == 5000 || waited_ms == 15000) {
            LwIP_DHCP(NETIF_WLAN_STA_INDEX, DHCP_START);
        }
        
        rtos_time_delay_ms(RIVER_WIFI_STA_DHCP_POLL_MS);
        waited_ms += RIVER_WIFI_STA_DHCP_POLL_MS;
    }
    return false;
}

static void river_wifi_station_task(void *param)
{
    (void)param;
    struct rtw_network_info connect_param;

    RIVER_LOGI("wifi station task started");

    while (1) {
        if (river_wifi_station_is_connected()) {
            rtos_time_delay_ms(5000);
            continue;
        }

        const river_wifi_cred_internal_t *cred = &g_river_creds[g_river_wifi_station.current_cred_index];
        g_river_wifi_station.connected = false;
        g_river_wifi_station.connecting = true;
        g_river_wifi_station.connect_attempts++;

        RIVER_LOGI("attempting connection to %s (cred index %d)", cred->ssid, g_river_wifi_station.current_cred_index);

        river_wifi_station_wait_driver_idle(RIVER_WIFI_STA_IDLE_WAIT_MS);

        memset(&connect_param, 0, sizeof(connect_param));
        connect_param.ssid.len = strlen(cred->ssid);
        memcpy(connect_param.ssid.val, cred->ssid, connect_param.ssid.len);
        connect_param.password = (unsigned char *)cred->pass;
        connect_param.password_len = strlen(cred->pass);
        connect_param.security_type = RTW_SECURITY_WPA2_AES_PSK;

        if (wifi_connect(&connect_param, 1) == RTK_SUCCESS) {
            if (river_wifi_station_wait_for_ipv4(RIVER_WIFI_STA_DHCP_WAIT_MS)) {
                continue;
            }
        }

        g_river_wifi_station.current_cred_index = (g_river_wifi_station.current_cred_index + 1) % (sizeof(g_river_creds)/sizeof(g_river_creds[0]));
        RIVER_LOGW("connection failed; rotating to next credential");
        rtos_time_delay_ms(RIVER_WIFI_STA_RETRY_MS);
    }
}

river_status_t river_wifi_station_init(void)
{
    if (g_river_wifi_station.initialized) return RIVER_OK;
    memset(&g_river_wifi_station, 0, sizeof(g_river_wifi_station));
    wifi_fast_connect_enable(0);
    if (rtos_task_create(&g_river_wifi_station.task, "river_wifi", river_wifi_station_task, 
                         NULL, RIVER_WIFI_STA_TASK_STACK, RIVER_WIFI_STA_TASK_PRIORITY) != RTK_SUCCESS) {
        return RIVER_ERR_IO;
    }
    g_river_wifi_station.initialized = true;
    return RIVER_OK;
}

bool river_wifi_station_is_connected(void)
{
    return g_river_wifi_station.connected && river_wifi_station_has_ipv4();
}

const char* river_wifi_station_selected_ssid(void)
{
    return g_river_creds[g_river_wifi_station.current_cred_index].ssid;
}

const char* river_wifi_station_status_name(void)
{
    if (river_wifi_station_is_connected()) return "connected";
    if (g_river_wifi_station.connecting) return "connecting";
    return "disconnected";
}

void river_wifi_station_dump_status(void)
{
    RIVER_LOGI("wifi: connected=%s attempts=%lu success=%lu", 
               g_river_wifi_station.connected ? "yes" : "no",
               (unsigned long)g_river_wifi_station.connect_attempts,
               (unsigned long)g_river_wifi_station.connect_successes);
}
