#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "lwip_netconf.h"
#include "os_wrapper.h"
#include "wifi_api.h"

#include "river/river_wifi_credentials.h"
#include "river/river_wifi_station.h"

#define RIVER_WIFI_STA_TASK_STACK    (1024U * 4U)
#define RIVER_WIFI_STA_TASK_PRIORITY 3U
#define RIVER_WIFI_STA_RETRY_MS      5000U

typedef struct {
    bool initialized;
    bool connected;
    bool connecting;
    rtos_task_t task;
    uint32_t connect_attempts;
    uint32_t connect_successes;
    uint32_t connect_failures;
    int last_error;
} river_wifi_station_context_t;

static river_wifi_station_context_t g_river_wifi_station;

static void river_wifi_station_task(void *param)
{
    struct rtw_network_info connect_param;
    int result;

    (void)param;

    memset(&connect_param, 0, sizeof(connect_param));
    memcpy(connect_param.ssid.val,
           RIVER_WIFI_STA_SSID,
           strlen(RIVER_WIFI_STA_SSID));
    connect_param.ssid.len = strlen(RIVER_WIFI_STA_SSID);
    connect_param.password = (unsigned char *)RIVER_WIFI_STA_PASSWORD;
    connect_param.password_len = strlen(RIVER_WIFI_STA_PASSWORD);

    while (1) {
        if (!wifi_is_running(STA_WLAN_INDEX)) {
            wifi_on(RTW_MODE_STA);
            rtos_time_delay_ms(1000);
            continue;
        }

        if (LwIP_Check_Connectivity(NETIF_WLAN_STA_INDEX) == CONNECTION_VALID) {
            g_river_wifi_station.connected = true;
            g_river_wifi_station.connecting = false;
            rtos_time_delay_ms(1000);
            continue;
        }

        g_river_wifi_station.connected = false;
        g_river_wifi_station.connecting = true;
        g_river_wifi_station.connect_attempts++;

        printf("[river][wifi] connect ssid=%s attempt=%lu\n",
               RIVER_WIFI_STA_SSID,
               (unsigned long)g_river_wifi_station.connect_attempts);

        result = wifi_connect(&connect_param, 1);
        if (result == RTK_SUCCESS) {
            result = LwIP_IP_Address_Request(NETIF_WLAN_STA_INDEX);
            if (result == DHCP_ADDRESS_ASSIGNED) {
                g_river_wifi_station.connected = true;
                g_river_wifi_station.connecting = false;
                g_river_wifi_station.connect_successes++;
                g_river_wifi_station.last_error = 0;
                printf("[river][wifi] connected ssid=%s ip=%u.%u.%u.%u success=%lu\n",
                       RIVER_WIFI_STA_SSID,
                       (unsigned int)LwIP_GetIP(NETIF_WLAN_STA_INDEX)[0],
                       (unsigned int)LwIP_GetIP(NETIF_WLAN_STA_INDEX)[1],
                       (unsigned int)LwIP_GetIP(NETIF_WLAN_STA_INDEX)[2],
                       (unsigned int)LwIP_GetIP(NETIF_WLAN_STA_INDEX)[3],
                       (unsigned long)g_river_wifi_station.connect_successes);
                rtos_time_delay_ms(1000);
                continue;
            }

            wifi_disconnect();
            g_river_wifi_station.last_error = result;
        } else {
            g_river_wifi_station.last_error = result;
        }

        g_river_wifi_station.connecting = false;
        g_river_wifi_station.connect_failures++;
        printf("[river][wifi] connect failed ssid=%s err=%d failures=%lu retry_ms=%u\n",
               RIVER_WIFI_STA_SSID,
               g_river_wifi_station.last_error,
               (unsigned long)g_river_wifi_station.connect_failures,
               (unsigned int)RIVER_WIFI_STA_RETRY_MS);
        rtos_time_delay_ms(RIVER_WIFI_STA_RETRY_MS);
    }
}

river_status_t river_wifi_station_init(void)
{
    if (g_river_wifi_station.initialized) {
        return RIVER_OK;
    }

    memset(&g_river_wifi_station, 0, sizeof(g_river_wifi_station));
    if (rtos_task_create(&g_river_wifi_station.task,
                         "river_wifi_sta",
                         river_wifi_station_task,
                         NULL,
                         RIVER_WIFI_STA_TASK_STACK,
                         RIVER_WIFI_STA_TASK_PRIORITY) != RTK_SUCCESS) {
        printf("[river][wifi] autoconnect task create failed\n");
        return RIVER_ERR_IO;
    }

    g_river_wifi_station.initialized = true;
    printf("[river][wifi] autoconnect init: ssid=%s retry_ms=%u\n",
           RIVER_WIFI_STA_SSID,
           (unsigned int)RIVER_WIFI_STA_RETRY_MS);
    return RIVER_OK;
}

bool river_wifi_station_is_connected(void)
{
    return g_river_wifi_station.connected;
}

const char *river_wifi_station_ssid(void)
{
    return RIVER_WIFI_STA_SSID;
}

const char *river_wifi_station_status_name(void)
{
    if (!g_river_wifi_station.initialized) {
        return "disabled";
    }
    if (g_river_wifi_station.connected) {
        return "connected";
    }
    if (g_river_wifi_station.connecting) {
        return "connecting";
    }
    return "disconnected";
}

void river_wifi_station_dump_status(void)
{
    printf("[river][wifi] status=%s ssid=%s attempts=%lu success=%lu fail=%lu last_err=%d\n",
           river_wifi_station_status_name(),
           river_wifi_station_ssid(),
           (unsigned long)g_river_wifi_station.connect_attempts,
           (unsigned long)g_river_wifi_station.connect_successes,
           (unsigned long)g_river_wifi_station.connect_failures,
           g_river_wifi_station.last_error);
}
