#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "lwip_netconf.h"
#include "os_wrapper.h"
#include "os_wrapper_memory.h"
#include "wifi_api.h"
#include "wifi_api_ext.h"

#include "river/river_log.h"
#include "river/river_wifi_credentials.h"
#include "river/river_wifi_station.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.wifi"

#define RIVER_WIFI_STA_TASK_STACK    (1024U * 4U)
#define RIVER_WIFI_STA_TASK_PRIORITY 3U
#define RIVER_WIFI_STA_RETRY_MS      5000U

typedef struct {
    bool initialized;
    bool connected;
    bool connecting;
    bool sdk_autoreconnect_disabled;
    rtos_task_t task;
    uint32_t connect_attempts;
    uint32_t connect_successes;
    uint32_t connect_failures;
    int last_error;
} river_wifi_station_context_t;

static river_wifi_station_context_t g_river_wifi_station;

typedef struct {
    bool valid;
    struct rtw_scan_result result;
} river_wifi_scan_candidate_t;

typedef enum {
    RIVER_WIFI_CONNECT_STRATEGY_BASIC = 0,
    RIVER_WIFI_CONNECT_STRATEGY_SCAN_EXACT,
    RIVER_WIFI_CONNECT_STRATEGY_SCAN_WPA2_FALLBACK,
} river_wifi_connect_strategy_t;

static const char *river_wifi_station_join_status_name(u8 status)
{
    switch (status) {
    case RTW_JOINSTATUS_UNKNOWN:
        return "unknown";
    case RTW_JOINSTATUS_STARTING:
        return "starting";
    case RTW_JOINSTATUS_SCANNING:
        return "scanning";
    case RTW_JOINSTATUS_AUTHENTICATING:
        return "authenticating";
    case RTW_JOINSTATUS_AUTHENTICATED:
        return "authenticated";
    case RTW_JOINSTATUS_ASSOCIATING:
        return "associating";
    case RTW_JOINSTATUS_ASSOCIATED:
        return "associated";
    case RTW_JOINSTATUS_4WAY_HANDSHAKING:
        return "4way_handshaking";
    case RTW_JOINSTATUS_4WAY_HANDSHAKE_DONE:
        return "4way_done";
    case RTW_JOINSTATUS_SUCCESS:
        return "success";
    case RTW_JOINSTATUS_FAIL:
        return "fail";
    case RTW_JOINSTATUS_DISCONNECT:
        return "disconnect";
    default:
        return "invalid";
    }
}

static const char *river_wifi_station_error_name(int error)
{
    switch (-error) {
    case RTK_ERR_WIFI_CONN_INVALID_KEY:
        return "invalid_key_format";
    case RTK_ERR_WIFI_CONN_SCAN_FAIL:
        return "scan_fail";
    case RTK_ERR_WIFI_CONN_AUTH_FAIL:
        return "auth_fail";
    case RTK_ERR_WIFI_CONN_AUTH_PASSWORD_WRONG:
        return "auth_password_wrong";
    case RTK_ERR_WIFI_CONN_ASSOC_FAIL:
        return "assoc_fail";
    case RTK_ERR_WIFI_CONN_4WAY_HANDSHAKE_FAIL:
        return "4way_handshake_fail";
    case RTK_ERR_WIFI_CONN_4WAY_PASSWORD_WRONG:
        return "4way_password_wrong";
    case RTK_ERR_BUSY:
        return "busy";
    case RTK_ERR_TIMEOUT:
        return "timeout";
    case RTK_ERR_BADARG:
        return "badarg";
    default:
        return "other";
    }
}

static const char *river_wifi_station_security_name(u32 security)
{
    switch (security) {
    case RTW_SECURITY_OPEN:
        return "open";
    case RTW_SECURITY_WEP_PSK:
        return "wep_psk";
    case RTW_SECURITY_WEP_SHARED:
        return "wep_shared";
    case RTW_SECURITY_WPA_TKIP_PSK:
        return "wpa_tkip";
    case RTW_SECURITY_WPA_AES_PSK:
        return "wpa_aes";
    case RTW_SECURITY_WPA_MIXED_PSK:
        return "wpa_mixed";
    case RTW_SECURITY_WPA2_TKIP_PSK:
        return "wpa2_tkip";
    case RTW_SECURITY_WPA2_AES_PSK:
        return "wpa2_aes";
    case RTW_SECURITY_WPA2_MIXED_PSK:
        return "wpa2_mixed";
    case RTW_SECURITY_WPA_WPA2_TKIP_PSK:
        return "wpa_wpa2_tkip";
    case RTW_SECURITY_WPA_WPA2_AES_PSK:
        return "wpa_wpa2_aes";
    case RTW_SECURITY_WPA_WPA2_MIXED_PSK:
        return "wpa_wpa2_mixed";
    case RTW_SECURITY_WPA3_AES_PSK:
        return "wpa3_sae";
    case RTW_SECURITY_WPA2_WPA3_MIXED:
        return "wpa2_wpa3_mixed";
    default:
        return "unknown";
    }
}

static const char *river_wifi_station_connect_strategy_name(river_wifi_connect_strategy_t strategy)
{
    switch (strategy) {
    case RIVER_WIFI_CONNECT_STRATEGY_BASIC:
        return "basic";
    case RIVER_WIFI_CONNECT_STRATEGY_SCAN_EXACT:
        return "scan_exact";
    case RIVER_WIFI_CONNECT_STRATEGY_SCAN_WPA2_FALLBACK:
        return "scan_wpa2_fallback";
    default:
        return "unknown";
    }
}

static bool river_wifi_station_has_ipv4(void)
{
    return (*(u32 *)LwIP_GetIP(NETIF_WLAN_STA_INDEX) != IP_ADDR_INVALID);
}

static bool river_wifi_station_is_ready(void)
{
    u8 join_status = RTW_JOINSTATUS_UNKNOWN;

    if (wifi_get_join_status(&join_status) != RTK_SUCCESS) {
        return false;
    }

    return (join_status == RTW_JOINSTATUS_SUCCESS) && river_wifi_station_has_ipv4();
}

static void river_wifi_station_disable_sdk_autoreconnect_once(void)
{
    u8 enabled = 0;

    if (g_river_wifi_station.sdk_autoreconnect_disabled) {
        return;
    }

    if ((wifi_get_autoreconnect(&enabled) == RTK_SUCCESS) && enabled) {
        if (wifi_set_autoreconnect(0) == RTK_SUCCESS) {
            RIVER_LOGI("sdk autoreconnect disabled; river owns reconnect policy");
        } else {
            RIVER_LOGW("sdk autoreconnect disable failed");
        }
    }

    g_river_wifi_station.sdk_autoreconnect_disabled = true;
}

static river_wifi_scan_candidate_t river_wifi_station_scan_target(void)
{
    river_wifi_scan_candidate_t candidate;
    struct rtw_scan_param scan_param;
    struct rtw_scan_result *records = NULL;
    int scanned_ap_num;
    u32 ap_num;
    u32 i;

    memset(&candidate, 0, sizeof(candidate));
    memset(&scan_param, 0, sizeof(scan_param));
    scan_param.ssid = (u8 *)RIVER_WIFI_STA_SSID;
    scan_param.max_ap_record_num = 16;

    scanned_ap_num = wifi_scan_networks(&scan_param, 1);
    if (scanned_ap_num <= 0) {
        RIVER_LOGW("scan failed for ssid=%s ret=%d", RIVER_WIFI_STA_SSID, scanned_ap_num);
        return candidate;
    }

    ap_num = (u32)scanned_ap_num;
    records = (struct rtw_scan_result *)rtos_mem_zmalloc(ap_num * sizeof(struct rtw_scan_result));
    if (records == NULL) {
        RIVER_LOGW("scan result alloc failed ap_num=%lu", (unsigned long)ap_num);
        return candidate;
    }

    if (wifi_get_scan_records(&ap_num, records) != RTK_SUCCESS) {
        RIVER_LOGW("scan record fetch failed");
        rtos_mem_free(records);
        return candidate;
    }

    for (i = 0; i < ap_num; ++i) {
        struct rtw_scan_result *record = &records[i];
        size_t target_len = strlen(RIVER_WIFI_STA_SSID);

        if ((record->ssid.len != target_len) ||
            (memcmp(record->ssid.val, RIVER_WIFI_STA_SSID, target_len) != 0)) {
            continue;
        }

        if ((!candidate.valid) ||
            ((candidate.result.band != RTW_BAND_ON_24G) && (record->band == RTW_BAND_ON_24G)) ||
            ((candidate.result.band == record->band) &&
             (record->signal_strength > candidate.result.signal_strength))) {
            candidate.valid = true;
            memcpy(&candidate.result, record, sizeof(candidate.result));
        }
    }

    rtos_mem_free(records);

    if (candidate.valid) {
        RIVER_LOGI("scan candidate ssid=%s bssid=%02x:%02x:%02x:%02x:%02x:%02x ch=%lu band=%s rssi=%d sec=%s(0x%lx)",
                   RIVER_WIFI_STA_SSID,
                   candidate.result.bssid.octet[0],
                   candidate.result.bssid.octet[1],
                   candidate.result.bssid.octet[2],
                   candidate.result.bssid.octet[3],
                   candidate.result.bssid.octet[4],
                   candidate.result.bssid.octet[5],
                   (unsigned long)candidate.result.channel,
                   candidate.result.band == RTW_BAND_ON_5G ? "5g" : "2.4g",
                   (int)candidate.result.signal_strength,
                   river_wifi_station_security_name(candidate.result.security),
                   (unsigned long)candidate.result.security);
    } else {
        RIVER_LOGW("scan candidate not found for ssid=%s", RIVER_WIFI_STA_SSID);
    }

    return candidate;
}

static void river_wifi_station_fill_connect_param_basic(struct rtw_network_info *connect_param)
{
    memset(connect_param, 0, sizeof(*connect_param));
    memcpy(connect_param->ssid.val,
           RIVER_WIFI_STA_SSID,
           strlen(RIVER_WIFI_STA_SSID));
    connect_param->ssid.len = strlen(RIVER_WIFI_STA_SSID);
    connect_param->password = (unsigned char *)RIVER_WIFI_STA_PASSWORD;
    connect_param->password_len = strlen(RIVER_WIFI_STA_PASSWORD);
}

static void river_wifi_station_fill_connect_param(struct rtw_network_info *connect_param,
                                                  const river_wifi_scan_candidate_t *candidate,
                                                  river_wifi_connect_strategy_t strategy)
{
    river_wifi_station_fill_connect_param_basic(connect_param);

    if ((candidate == NULL) || (!candidate->valid) ||
        (strategy == RIVER_WIFI_CONNECT_STRATEGY_BASIC)) {
        return;
    }

    memcpy(connect_param->bssid.octet,
           candidate->result.bssid.octet,
           sizeof(connect_param->bssid.octet));
    connect_param->channel = (u8)candidate->result.channel;
    connect_param->security_type = candidate->result.security;
    connect_param->pscan_option = RTW_PSCAN_FAST_SURVEY;

    if (strategy == RIVER_WIFI_CONNECT_STRATEGY_SCAN_WPA2_FALLBACK) {
        connect_param->security_type = RTW_SECURITY_WPA2_AES_PSK;
        RIVER_LOGI("scan candidate sec=%s; force fallback connect sec=%s for compatibility",
                   river_wifi_station_security_name(candidate->result.security),
                   river_wifi_station_security_name(connect_param->security_type));
    }
}

static void river_wifi_station_disconnect_and_wait_idle(uint32_t timeout_ms)
{
    uint32_t waited_ms = 0U;
    u8 join_status = RTW_JOINSTATUS_UNKNOWN;

    wifi_disconnect();
    rtos_time_delay_ms(300);

    while (waited_ms < timeout_ms) {
        if (wifi_get_join_status(&join_status) != RTK_SUCCESS) {
            break;
        }

        if ((join_status == RTW_JOINSTATUS_UNKNOWN) ||
            (join_status == RTW_JOINSTATUS_FAIL) ||
            (join_status == RTW_JOINSTATUS_DISCONNECT)) {
            break;
        }

        rtos_time_delay_ms(100);
        waited_ms += 100U;
    }
}

static void river_wifi_station_task(void *param)
{
    struct rtw_network_info connect_param;
    river_wifi_scan_candidate_t candidate;
    river_wifi_connect_strategy_t strategy;
    u8 join_status = RTW_JOINSTATUS_UNKNOWN;
    int result;

    (void)param;

    while (1) {
        if (!wifi_is_running(STA_WLAN_INDEX)) {
            wifi_on(RTW_MODE_STA);
            g_river_wifi_station.sdk_autoreconnect_disabled = false;
            rtos_time_delay_ms(1000);
            continue;
        }

        river_wifi_station_disable_sdk_autoreconnect_once();

        if (river_wifi_station_is_ready()) {
            g_river_wifi_station.connected = true;
            g_river_wifi_station.connecting = false;
            rtos_time_delay_ms(1000);
            continue;
        }

        g_river_wifi_station.connected = false;
        g_river_wifi_station.connecting = true;
        g_river_wifi_station.connect_attempts++;

        RIVER_LOGI("connect ssid=%s attempt=%lu",
                   RIVER_WIFI_STA_SSID,
                   (unsigned long)g_river_wifi_station.connect_attempts);

        candidate = river_wifi_station_scan_target();
        strategy = RIVER_WIFI_CONNECT_STRATEGY_BASIC;
        result = RTK_FAIL;

        for (strategy = RIVER_WIFI_CONNECT_STRATEGY_BASIC;
             strategy <= RIVER_WIFI_CONNECT_STRATEGY_SCAN_WPA2_FALLBACK;
             strategy++) {
            if ((strategy != RIVER_WIFI_CONNECT_STRATEGY_BASIC) && !candidate.valid) {
                break;
            }
            if ((strategy == RIVER_WIFI_CONNECT_STRATEGY_SCAN_WPA2_FALLBACK) &&
                (!candidate.valid || (candidate.result.security != RTW_SECURITY_WPA2_WPA3_MIXED))) {
                continue;
            }

            river_wifi_station_fill_connect_param(&connect_param, &candidate, strategy);
            RIVER_LOGI("connect strategy=%s ssid=%s channel=%u sec=%s bssid=%02x:%02x:%02x:%02x:%02x:%02x",
                       river_wifi_station_connect_strategy_name(strategy),
                       RIVER_WIFI_STA_SSID,
                       (unsigned int)connect_param.channel,
                       river_wifi_station_security_name(connect_param.security_type),
                       connect_param.bssid.octet[0],
                       connect_param.bssid.octet[1],
                       connect_param.bssid.octet[2],
                       connect_param.bssid.octet[3],
                       connect_param.bssid.octet[4],
                       connect_param.bssid.octet[5]);

            result = wifi_connect(&connect_param, 1);
            if (result == RTK_SUCCESS) {
                break;
            }

            g_river_wifi_station.last_error = result;
            if (wifi_get_join_status(&join_status) != RTK_SUCCESS) {
                join_status = RTW_JOINSTATUS_UNKNOWN;
            }
            RIVER_LOGW("connect strategy=%s failed err=%d(%s) join=%s",
                       river_wifi_station_connect_strategy_name(strategy),
                       result,
                       river_wifi_station_error_name(result),
                       river_wifi_station_join_status_name(join_status));
            river_wifi_station_disconnect_and_wait_idle(1500U);
        }

        if (result == RTK_SUCCESS) {
            result = LwIP_IP_Address_Request(NETIF_WLAN_STA_INDEX);
            if (result == DHCP_ADDRESS_ASSIGNED) {
                g_river_wifi_station.connected = true;
                g_river_wifi_station.connecting = false;
                g_river_wifi_station.connect_successes++;
                g_river_wifi_station.last_error = 0;
                RIVER_LOGI("connected ssid=%s ip=%u.%u.%u.%u success=%lu",
                           RIVER_WIFI_STA_SSID,
                           (unsigned int)LwIP_GetIP(NETIF_WLAN_STA_INDEX)[0],
                           (unsigned int)LwIP_GetIP(NETIF_WLAN_STA_INDEX)[1],
                           (unsigned int)LwIP_GetIP(NETIF_WLAN_STA_INDEX)[2],
                           (unsigned int)LwIP_GetIP(NETIF_WLAN_STA_INDEX)[3],
                           (unsigned long)g_river_wifi_station.connect_successes);
                rtos_time_delay_ms(1000);
                continue;
            }

            river_wifi_station_disconnect_and_wait_idle(1500U);
            g_river_wifi_station.last_error = result;
        } else {
            g_river_wifi_station.last_error = result;
        }

        if (wifi_get_join_status(&join_status) != RTK_SUCCESS) {
            join_status = RTW_JOINSTATUS_UNKNOWN;
        }

        g_river_wifi_station.connecting = false;
        g_river_wifi_station.connect_failures++;
        RIVER_LOGW("connect failed ssid=%s err=%d(%s) join=%s failures=%lu retry_ms=%u",
                   RIVER_WIFI_STA_SSID,
                   g_river_wifi_station.last_error,
                   river_wifi_station_error_name(g_river_wifi_station.last_error),
                   river_wifi_station_join_status_name(join_status),
                   (unsigned long)g_river_wifi_station.connect_failures,
                   (unsigned int)RIVER_WIFI_STA_RETRY_MS);
        river_wifi_station_disconnect_and_wait_idle(1500U);
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
        RIVER_LOGE("autoconnect task create failed");
        return RIVER_ERR_IO;
    }

    g_river_wifi_station.initialized = true;
    RIVER_LOGI("autoconnect init: ssid=%s retry_ms=%u",
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
    RIVER_LOGI("status=%s ssid=%s attempts=%lu success=%lu fail=%lu last_err=%d",
               river_wifi_station_status_name(),
               river_wifi_station_ssid(),
               (unsigned long)g_river_wifi_station.connect_attempts,
               (unsigned long)g_river_wifi_station.connect_successes,
               (unsigned long)g_river_wifi_station.connect_failures,
               g_river_wifi_station.last_error);
}
