#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "kv.h"
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
#define RIVER_WIFI_STA_JOIN_WAIT_MS  12000U
#define RIVER_WIFI_STA_DHCP_WAIT_MS  12000U
#define RIVER_WIFI_STA_DHCP_POLL_MS  500U

extern int (*p_wifi_do_fast_connect)(void);
extern int (*p_store_fast_connect_info)(unsigned int data1, unsigned int data2);
extern int wifi_set_ips_internal(u8 enable);
extern struct wifi_user_conf wifi_user_config;

typedef struct {
    bool initialized;
    bool connected;
    bool connecting;
    bool sdk_autoreconnect_disabled;
    bool sdk_fast_connect_disabled;
    bool sdk_lps_disabled;
    bool sdk_ips_disabled;
    bool sdk_fast_connect_profile_cleared;
    bool startup_sta_state_cleared;
    bool sdk_user_config_patched;
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

static bool river_wifi_station_joined(void)
{
    u8 join_status = RTW_JOINSTATUS_UNKNOWN;

    if (wifi_get_join_status(&join_status) != RTK_SUCCESS) {
        return false;
    }

    return join_status == RTW_JOINSTATUS_SUCCESS;
}

static bool river_wifi_station_is_ready(void)
{
    u8 join_status = RTW_JOINSTATUS_UNKNOWN;

    if (wifi_get_join_status(&join_status) != RTK_SUCCESS) {
        return false;
    }

    return (join_status == RTW_JOINSTATUS_SUCCESS) && river_wifi_station_has_ipv4();
}

static bool river_wifi_station_join_in_progress(void)
{
    u8 join_status = RTW_JOINSTATUS_UNKNOWN;

    if (wifi_get_join_status(&join_status) != RTK_SUCCESS) {
        return false;
    }

    return (join_status > RTW_JOINSTATUS_UNKNOWN) && (join_status < RTW_JOINSTATUS_SUCCESS);
}

static void river_wifi_station_force_sdk_fast_connect_off(void)
{
    wifi_fast_connect_enable(0);
    p_wifi_do_fast_connect = NULL;
    p_store_fast_connect_info = NULL;
}

static void river_wifi_station_patch_user_config_once(void)
{
    if (g_river_wifi_station.sdk_user_config_patched) {
        return;
    }

    wifi_user_config.fast_reconnect_en = 0;
    wifi_user_config.auto_reconnect_en = 0;
    wifi_user_config.auto_reconnect_count = 0;
    wifi_user_config.auto_reconnect_interval = 0;
    wifi_user_config.ips_enable = 0;
    wifi_user_config.ips_ctrl_by_usr = 1;
    wifi_user_config.lps_enable = 0;

    g_river_wifi_station.sdk_user_config_patched = true;
    RIVER_LOGI("sdk user config patched before wifi_on: fast_reconnect=0 auto_reconnect=0 ips=0 lps=0");
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

static void river_wifi_station_disable_sdk_fast_connect_once(void)
{
    if (g_river_wifi_station.sdk_fast_connect_disabled) {
        return;
    }

    river_wifi_station_force_sdk_fast_connect_off();
    g_river_wifi_station.sdk_fast_connect_disabled = true;
    RIVER_LOGI("sdk fast connect disabled; river owns initial connect policy");
}

static void river_wifi_station_clear_sdk_fast_connect_profile_once(void)
{
    int ret;

    if (g_river_wifi_station.sdk_fast_connect_profile_cleared) {
        return;
    }

    ret = rt_kv_delete("wlan_data");
    if (ret == 0) {
        RIVER_LOGI("cleared sdk fast-connect flash profile");
    } else {
        RIVER_LOGI("sdk fast-connect flash profile absent or already cleared");
    }

    g_river_wifi_station.sdk_fast_connect_profile_cleared = true;
}

static void river_wifi_station_clear_sdk_fast_connect_profile_early(void)
{
    int ret = rt_kv_delete("wlan_data");

    if (ret == 0) {
        RIVER_LOGI("cleared sdk fast-connect flash profile before wifi_on");
    } else {
        RIVER_LOGI("sdk fast-connect flash profile absent before wifi_on");
    }

    g_river_wifi_station.sdk_fast_connect_profile_cleared = true;
}

static void river_wifi_station_disable_lps_once(void)
{
    if (g_river_wifi_station.sdk_lps_disabled) {
        return;
    }

    if (wifi_set_lps_enable(0) == RTK_SUCCESS) {
        RIVER_LOGI("sdk lps disabled during bring-up");
    } else {
        RIVER_LOGW("sdk lps disable failed");
    }

    g_river_wifi_station.sdk_lps_disabled = true;
}

static void river_wifi_station_disable_ips_once(void)
{
    if (g_river_wifi_station.sdk_ips_disabled) {
        return;
    }

    if (wifi_set_ips_internal(0) == RTK_SUCCESS) {
        RIVER_LOGI("sdk ips disabled during bring-up");
    } else {
        RIVER_LOGW("sdk ips disable failed");
    }

    g_river_wifi_station.sdk_ips_disabled = true;
}

static int river_wifi_station_request_dhcp(void)
{
    return LwIP_IP_Address_Request(NETIF_WLAN_STA_INDEX);
}

static void river_wifi_station_mark_connected(void)
{
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
}

static bool river_wifi_station_wait_driver_idle(uint32_t timeout_ms, bool allow_join_success)
{
    uint32_t waited_ms = 0U;
    u8 join_status = RTW_JOINSTATUS_UNKNOWN;

    while (waited_ms < timeout_ms) {
        if (wifi_get_join_status(&join_status) != RTK_SUCCESS) {
            return false;
        }

        if (allow_join_success && join_status == RTW_JOINSTATUS_SUCCESS) {
            return true;
        }

        if ((join_status == RTW_JOINSTATUS_UNKNOWN) ||
            (join_status == RTW_JOINSTATUS_FAIL) ||
            (join_status == RTW_JOINSTATUS_DISCONNECT)) {
            return true;
        }

        rtos_time_delay_ms(100);
        waited_ms += 100U;
    }

    return false;
}

static bool river_wifi_station_wait_for_ipv4_after_join(uint32_t timeout_ms, const char *source)
{
    int dhcp_result = DHCP_ADDRESS_ASSIGNED;
    uint32_t waited_ms = 0U;
    bool dhcp_requested = false;

    if (!river_wifi_station_joined()) {
        return false;
    }

    if (river_wifi_station_has_ipv4()) {
        river_wifi_station_mark_connected();
        return true;
    }

    RIVER_LOGI("wait for ipv4 source=%s timeout_ms=%lu",
               source,
               (unsigned long)timeout_ms);

    while (waited_ms < timeout_ms) {
        if (!river_wifi_station_joined()) {
            break;
        }

        if (river_wifi_station_has_ipv4()) {
            river_wifi_station_mark_connected();
            return true;
        }

        if (!dhcp_requested) {
            dhcp_requested = true;
            dhcp_result = river_wifi_station_request_dhcp();
            if (dhcp_result == DHCP_ADDRESS_ASSIGNED) {
                river_wifi_station_mark_connected();
                return true;
            }

            RIVER_LOGW("dhcp pending after join source=%s ret=%d; keep link up and poll ip",
                       source,
                       dhcp_result);
        }

        rtos_time_delay_ms(RIVER_WIFI_STA_DHCP_POLL_MS);
        waited_ms += RIVER_WIFI_STA_DHCP_POLL_MS;
    }

    if (river_wifi_station_has_ipv4()) {
        river_wifi_station_mark_connected();
        return true;
    }

    g_river_wifi_station.last_error = dhcp_result;
    RIVER_LOGW("dhcp pending/failed after join ssid=%s source=%s ret=%d waited_ms=%lu",
               RIVER_WIFI_STA_SSID,
               source,
               dhcp_result,
               (unsigned long)waited_ms);
    return false;
}

static bool river_wifi_station_try_complete_join_without_reconnect(void)
{
    if (!river_wifi_station_joined()) {
        return false;
    }

    if (river_wifi_station_has_ipv4()) {
        river_wifi_station_mark_connected();
        return true;
    }

    return river_wifi_station_wait_for_ipv4_after_join(RIVER_WIFI_STA_DHCP_WAIT_MS, "joined");
}

static bool river_wifi_station_wait_for_join_result(uint32_t timeout_ms)
{
    uint32_t waited_ms = 0U;
    u8 join_status = RTW_JOINSTATUS_UNKNOWN;

    while (waited_ms < timeout_ms) {
        if (river_wifi_station_try_complete_join_without_reconnect()) {
            return true;
        }

        if (wifi_get_join_status(&join_status) != RTK_SUCCESS) {
            return false;
        }

        if ((join_status == RTW_JOINSTATUS_FAIL) ||
            (join_status == RTW_JOINSTATUS_DISCONNECT) ||
            (join_status == RTW_JOINSTATUS_UNKNOWN)) {
            return false;
        }

        rtos_time_delay_ms(200);
        waited_ms += 200U;
    }

    return river_wifi_station_try_complete_join_without_reconnect();
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

    if (!river_wifi_station_wait_driver_idle(RIVER_WIFI_STA_IDLE_WAIT_MS, false)) {
        RIVER_LOGW("scan wait-idle timeout for ssid=%s; skip active scan this round", RIVER_WIFI_STA_SSID);
        return candidate;
    }

    scanned_ap_num = wifi_scan_networks(&scan_param, 1);
    if (scanned_ap_num == -RTK_ERR_BUSY) {
        RIVER_LOGW("scan busy for ssid=%s; wait idle and retry once", RIVER_WIFI_STA_SSID);
        if (river_wifi_station_wait_driver_idle(RIVER_WIFI_STA_IDLE_WAIT_MS, false)) {
            scanned_ap_num = wifi_scan_networks(&scan_param, 1);
        }
    }
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
    if (connect_param->password_len > 0) {
        connect_param->security_type = RTW_SECURITY_WPA2_AES_PSK;
    }
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

static void river_wifi_station_build_strategy_order(const river_wifi_scan_candidate_t *candidate,
                                                    river_wifi_connect_strategy_t *strategies,
                                                    size_t *strategy_count)
{
    *strategy_count = 0U;

    if ((candidate != NULL) && candidate->valid) {
        strategies[(*strategy_count)++] = RIVER_WIFI_CONNECT_STRATEGY_SCAN_EXACT;
        if (candidate->result.security == RTW_SECURITY_WPA2_WPA3_MIXED) {
            strategies[(*strategy_count)++] = RIVER_WIFI_CONNECT_STRATEGY_SCAN_WPA2_FALLBACK;
        }
        strategies[(*strategy_count)++] = RIVER_WIFI_CONNECT_STRATEGY_BASIC;
        return;
    }

    strategies[(*strategy_count)++] = RIVER_WIFI_CONNECT_STRATEGY_BASIC;
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

static bool river_wifi_station_adopt_existing_join_flow(uint32_t timeout_ms, const char *source)
{
    if (river_wifi_station_join_in_progress()) {
        RIVER_LOGI("join already in progress; adopt existing flow source=%s", source);
        if (river_wifi_station_wait_for_join_result(timeout_ms)) {
            return true;
        }
        RIVER_LOGW("existing join flow did not complete source=%s; reset sta state", source);
        river_wifi_station_disconnect_and_wait_idle(2000U);
        return false;
    }

    if (river_wifi_station_joined()) {
        RIVER_LOGI("joined without ip or pending dhcp; adopt existing flow source=%s", source);
        if (river_wifi_station_try_complete_join_without_reconnect()) {
            return true;
        }
        RIVER_LOGW("joined flow could not obtain ip source=%s; reset sta state", source);
        river_wifi_station_disconnect_and_wait_idle(2000U);
    }

    return false;
}

static void river_wifi_station_task(void *param)
{
    struct rtw_network_info connect_param;
    river_wifi_scan_candidate_t candidate;
    river_wifi_connect_strategy_t strategy;
    river_wifi_connect_strategy_t strategy_order[3];
    size_t strategy_count = 0U;
    size_t strategy_index = 0U;
    u8 join_status = RTW_JOINSTATUS_UNKNOWN;
    bool joined_in_wait;
    int result;

    (void)param;

    while (1) {
        if (!wifi_is_running(STA_WLAN_INDEX)) {
            river_wifi_station_force_sdk_fast_connect_off();
            river_wifi_station_patch_user_config_once();
            wifi_on(RTW_MODE_STA);
            g_river_wifi_station.sdk_autoreconnect_disabled = false;
            g_river_wifi_station.sdk_fast_connect_disabled = false;
            g_river_wifi_station.sdk_lps_disabled = false;
            g_river_wifi_station.sdk_ips_disabled = false;
            g_river_wifi_station.startup_sta_state_cleared = false;
            rtos_time_delay_ms(300);
            river_wifi_station_disconnect_and_wait_idle(3000U);
            g_river_wifi_station.startup_sta_state_cleared = true;
            RIVER_LOGI("post-wifi_on sta state reset complete; app owns first connection");
            rtos_time_delay_ms(700);
            continue;
        }

        river_wifi_station_disable_sdk_autoreconnect_once();
        river_wifi_station_disable_sdk_fast_connect_once();
        river_wifi_station_clear_sdk_fast_connect_profile_once();
        river_wifi_station_disable_lps_once();
        river_wifi_station_disable_ips_once();

        if (!g_river_wifi_station.startup_sta_state_cleared) {
            RIVER_LOGI("reset startup sta state before first app-owned connect");
            river_wifi_station_disconnect_and_wait_idle(3000U);
            g_river_wifi_station.startup_sta_state_cleared = true;
            rtos_time_delay_ms(300U);
            continue;
        }

        if (river_wifi_station_join_in_progress() || river_wifi_station_joined()) {
            if (river_wifi_station_adopt_existing_join_flow(RIVER_WIFI_STA_JOIN_WAIT_MS, "loop-top")) {
                rtos_time_delay_ms(1000);
                continue;
            }

            g_river_wifi_station.connecting = false;
            g_river_wifi_station.last_error = -RTK_ERR_TIMEOUT;
            g_river_wifi_station.connect_failures++;
            RIVER_LOGW("background join flow did not stabilize; defer fresh connect failures=%lu retry_ms=%u",
                       (unsigned long)g_river_wifi_station.connect_failures,
                       (unsigned int)RIVER_WIFI_STA_RETRY_MS);
            river_wifi_station_disconnect_and_wait_idle(2000U);
            rtos_time_delay_ms(RIVER_WIFI_STA_RETRY_MS);
            continue;
        }

        /* If we just became ready (perhaps via background join), skip re-connect */
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
        if (river_wifi_station_join_in_progress() || river_wifi_station_joined()) {
            if (river_wifi_station_adopt_existing_join_flow(RIVER_WIFI_STA_JOIN_WAIT_MS, "post-scan")) {
                rtos_time_delay_ms(1000);
                continue;
            }

            g_river_wifi_station.connecting = false;
            g_river_wifi_station.last_error = -RTK_ERR_TIMEOUT;
            g_river_wifi_station.connect_failures++;
            RIVER_LOGW("join flow appeared during scan and did not stabilize; skip reconnect this round failures=%lu retry_ms=%u",
                       (unsigned long)g_river_wifi_station.connect_failures,
                       (unsigned int)RIVER_WIFI_STA_RETRY_MS);
            river_wifi_station_disconnect_and_wait_idle(2000U);
            rtos_time_delay_ms(RIVER_WIFI_STA_RETRY_MS);
            continue;
        }

        river_wifi_station_build_strategy_order(&candidate, strategy_order, &strategy_count);
        joined_in_wait = false;
        result = RTK_FAIL;

        for (strategy_index = 0U; strategy_index < strategy_count; ++strategy_index) {
            strategy = strategy_order[strategy_index];

            /* If some other join flow appeared, adopt it instead of competing. */
            if (river_wifi_station_join_in_progress() || river_wifi_station_joined()) {
                if (river_wifi_station_adopt_existing_join_flow(RIVER_WIFI_STA_JOIN_WAIT_MS, "post-scan")) {
                    result = RTK_SUCCESS;
                    joined_in_wait = true;
                    break;
                }

                result = -RTK_ERR_TIMEOUT;
                RIVER_LOGW("join flow appeared before strategy=%s but did not stabilize; stop retrying this round",
                           river_wifi_station_connect_strategy_name(strategy));
                break;
            }

            if (!river_wifi_station_wait_driver_idle(RIVER_WIFI_STA_IDLE_WAIT_MS, true)) {
                RIVER_LOGW("connect wait-idle timeout before strategy=%s",
                           river_wifi_station_connect_strategy_name(strategy));
            }

            /* Final check: did some other join flow start while we were waiting? */
            if (river_wifi_station_join_in_progress() || river_wifi_station_joined()) {
                if (river_wifi_station_adopt_existing_join_flow(RIVER_WIFI_STA_JOIN_WAIT_MS, "pre-connect")) {
                    result = RTK_SUCCESS;
                    joined_in_wait = true;
                    break;
                }

                result = -RTK_ERR_TIMEOUT;
                RIVER_LOGW("join flow appeared during pre-connect wait for strategy=%s; stop retrying this round",
                           river_wifi_station_connect_strategy_name(strategy));
                break;
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

            if (result == -RTK_ERR_BUSY) {
                RIVER_LOGW("connect strategy=%s busy; wait existing join flow",
                           river_wifi_station_connect_strategy_name(strategy));
                if (river_wifi_station_adopt_existing_join_flow(RIVER_WIFI_STA_JOIN_WAIT_MS, "busy")) {
                    result = RTK_SUCCESS;
                    joined_in_wait = true;
                    break;
                }

                result = -RTK_ERR_TIMEOUT;
                RIVER_LOGW("busy join flow did not stabilize for strategy=%s; stop retrying this round",
                           river_wifi_station_connect_strategy_name(strategy));
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
            if (joined_in_wait && river_wifi_station_is_ready()) {
                g_river_wifi_station.connected = true;
                g_river_wifi_station.connecting = false;
                rtos_time_delay_ms(1000);
                continue;
            }

            if (river_wifi_station_wait_for_ipv4_after_join(RIVER_WIFI_STA_DHCP_WAIT_MS, "post-connect")) {
                rtos_time_delay_ms(1000);
                continue;
            }

            river_wifi_station_disconnect_and_wait_idle(1500U);
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
    river_wifi_station_force_sdk_fast_connect_off();
    river_wifi_station_patch_user_config_once();
    river_wifi_station_clear_sdk_fast_connect_profile_early();
    g_river_wifi_station.sdk_fast_connect_disabled = true;
    RIVER_LOGI("sdk fast connect pre-disabled before wlan init");
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
