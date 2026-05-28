/* Wi-Fi STA 管理实现：负责连接策略、重试与状态统计输出。 */
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
#include "river/river_runtime_stats.h"
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
#define RIVER_WIFI_STA_DHCP_RETRY_COUNT 2U
#define RIVER_WIFI_STA_DHCP_RETRY_BACKOFF_MS 1000U
#define RIVER_WIFI_STA_POWER_SETTLE_MS 500U
#define RIVER_WIFI_STA_MAX_CREDENTIALS 2U
#define RIVER_WIFI_STA_PASSWORD_BUFFER_SIZE (RTW_MAX_PSK_LEN + 1U)
#define RIVER_WIFI_STA_MIN_RSSI_DBM (-80)
#define RIVER_WIFI_STA_STICKY_RETRY_RSSI_DBM (-85)
#define RIVER_WIFI_STA_SCAN_LOG_LIMIT 8U

extern int (*p_wifi_do_fast_connect)(void);
extern int (*p_store_fast_connect_info)(unsigned int data1, unsigned int data2);
extern int wifi_set_ips_internal(u8 enable);
extern void wifi_set_user_config(void);
extern struct wifi_user_conf wifi_user_config;

typedef struct {
    bool enabled;
    char ssid[RTW_ESSID_MAX_SIZE + 1U];
    char password[RIVER_WIFI_STA_PASSWORD_BUFFER_SIZE];
    u8 ssid_len;
    u8 password_len;
} river_wifi_credential_t;

typedef struct {
    bool initialized;
    bool connected;
    bool connecting;
    bool connection_latched;
    bool sdk_autoreconnect_disabled;
    bool sdk_fast_connect_disabled;
    bool sdk_lps_disabled;
    bool sdk_ips_disabled;
    bool sdk_fast_connect_profile_cleared;
    bool startup_sta_state_cleared;
    bool sdk_user_config_patched;
    bool task_started;
    bool wifi_is_running_pending;
    bool wifi_on_pending;
    bool wifi_on_seen_success;
    rtos_task_t task;
    uint32_t connect_attempts;
    uint32_t connect_successes;
    uint32_t connect_failures;
    uint32_t task_loop_count;
    uint32_t wifi_is_running_attempts;
    uint32_t wifi_is_running_start_ms;
    uint32_t wifi_is_running_last_elapsed_ms;
    uint32_t wifi_on_attempts;
    uint32_t wifi_on_start_ms;
    uint32_t wifi_on_last_elapsed_ms;
    int wifi_is_running_last_result;
    int wifi_on_last_result;
    int last_error;
    river_wifi_credential_t credentials[RIVER_WIFI_STA_MAX_CREDENTIALS];
    u8 credential_count;
    u8 next_credential_index;
    bool active_credential_valid;
    u8 active_credential_index;
    char active_ssid[RTW_ESSID_MAX_SIZE + 1U];
    u32 last_connected_ip;
    char last_connected_ssid[RTW_ESSID_MAX_SIZE + 1U];
} river_wifi_station_context_t;

static river_wifi_station_context_t g_river_wifi_station;

typedef struct {
    bool valid;
    struct rtw_scan_result result;
} river_wifi_scan_candidate_t;

typedef enum {
    RIVER_WIFI_CONNECT_STRATEGY_SCAN_AUTO = 0,
    RIVER_WIFI_CONNECT_STRATEGY_SCAN_WPA2_MIXED,
    RIVER_WIFI_CONNECT_STRATEGY_SCAN_WPA_WPA2_MIXED,
    RIVER_WIFI_CONNECT_STRATEGY_SCAN_WPA2_AES,
    RIVER_WIFI_CONNECT_STRATEGY_BASIC_AUTO,
    RIVER_WIFI_CONNECT_STRATEGY_BASIC_WPA2_MIXED,
    RIVER_WIFI_CONNECT_STRATEGY_BASIC_WPA_WPA2_MIXED,
    RIVER_WIFI_CONNECT_STRATEGY_BASIC_WPA2_AES,
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
    case RIVER_WIFI_CONNECT_STRATEGY_SCAN_AUTO:
        return "scan_auto";
    case RIVER_WIFI_CONNECT_STRATEGY_SCAN_WPA2_MIXED:
        return "scan_wpa2_mixed";
    case RIVER_WIFI_CONNECT_STRATEGY_SCAN_WPA_WPA2_MIXED:
        return "scan_wpa_wpa2_mixed";
    case RIVER_WIFI_CONNECT_STRATEGY_SCAN_WPA2_AES:
        return "scan_wpa2_aes";
    case RIVER_WIFI_CONNECT_STRATEGY_BASIC_AUTO:
        return "basic_auto";
    case RIVER_WIFI_CONNECT_STRATEGY_BASIC_WPA2_MIXED:
        return "basic_wpa2_mixed";
    case RIVER_WIFI_CONNECT_STRATEGY_BASIC_WPA_WPA2_MIXED:
        return "basic_wpa_wpa2_mixed";
    case RIVER_WIFI_CONNECT_STRATEGY_BASIC_WPA2_AES:
        return "basic_wpa2_aes";
    default:
        return "unknown";
    }
}

static char river_wifi_station_printable_char(u8 value)
{
    if ((value >= 0x20U) && (value <= 0x7eU)) {
        return (char)value;
    }

    return '.';
}

static const char *river_wifi_station_band_support_name(u8 band)
{
    switch (band) {
    case RTW_SUPPORT_BAND_2_4G:
        return "2.4g";
    case RTW_SUPPORT_BAND_5G:
        return "5g";
    case RTW_SUPPORT_BAND_2_4G_5G_BOTH:
        return "2.4g+5g";
    case RTW_SUPPORT_BAND_MAX:
        return "hw_default";
    default:
        return "unknown";
    }
}

static void river_wifi_station_log_user_config(const char *source)
{
    RIVER_LOGI("user cfg source=%s country=%02x%02x(%c%c) band=%s(0x%02x) tx_pwr_sel=%u 11d=%u edcca=%u fast=%u auto=%u/%u/%u ips=%u ctrl=%u lps=%u",
               source,
               (unsigned int)wifi_user_config.country_code[0],
               (unsigned int)wifi_user_config.country_code[1],
               river_wifi_station_printable_char(wifi_user_config.country_code[0]),
               river_wifi_station_printable_char(wifi_user_config.country_code[1]),
               river_wifi_station_band_support_name(wifi_user_config.freq_band_support),
               (unsigned int)wifi_user_config.freq_band_support,
               (unsigned int)wifi_user_config.tx_pwr_table_selection,
               (unsigned int)wifi_user_config.rtw_802_11d_en,
               (unsigned int)wifi_user_config.rtw_edcca_mode,
               (unsigned int)wifi_user_config.fast_reconnect_en,
               (unsigned int)wifi_user_config.auto_reconnect_en,
               (unsigned int)wifi_user_config.auto_reconnect_count,
               (unsigned int)wifi_user_config.auto_reconnect_interval,
               (unsigned int)wifi_user_config.ips_enable,
               (unsigned int)wifi_user_config.ips_ctrl_by_usr,
               (unsigned int)wifi_user_config.lps_enable);
    RIVER_LOGI("user cfg source=%s concurrent=%u softap_offset=%u skb=%ld/%ld ampdu=%u/%u ampdu_en=%u/%u ap_sta=%u wpa=%u hidden_probe=%u shortcut=%u/%u keepalive=%u no_beacon=%u",
               source,
               (unsigned int)wifi_user_config.concurrent_enabled,
               (unsigned int)wifi_user_config.softap_addr_offset_idx,
               (long)wifi_user_config.skb_num_np,
               (long)wifi_user_config.skb_num_ap,
               (unsigned int)wifi_user_config.rx_ampdu_num,
               (unsigned int)wifi_user_config.tx_ampdu_num,
               (unsigned int)wifi_user_config.ampdu_rx_enable,
               (unsigned int)wifi_user_config.ampdu_tx_enable,
               (unsigned int)wifi_user_config.ap_sta_num,
               (unsigned int)wifi_user_config.wifi_wpa_mode_force,
               (unsigned int)wifi_user_config.probe_hidden_ap_on_passive_ch,
               (unsigned int)wifi_user_config.tx_shortcut_enable,
               (unsigned int)wifi_user_config.rx_shortcut_enable,
               (unsigned int)wifi_user_config.keepalive_interval,
               (unsigned int)wifi_user_config.no_beacon_disconnect_time);
}

static void river_wifi_station_format_ssid(const struct rtw_ssid *ssid, char *buffer, size_t buffer_size)
{
    size_t len;

    if ((buffer == NULL) || (buffer_size == 0U)) {
        return;
    }

    buffer[0] = '\0';
    if (ssid == NULL) {
        return;
    }

    len = ssid->len;
    if (len >= buffer_size) {
        len = buffer_size - 1U;
    }

    memcpy(buffer, ssid->val, len);
    buffer[len] = '\0';
}

static void river_wifi_station_copy_string(char *dst, size_t dst_size, const char *src)
{
    if ((dst == NULL) || (dst_size == 0U)) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    strncpy(dst, src, dst_size - 1U);
    dst[dst_size - 1U] = '\0';
}

static void river_wifi_station_add_credential(u8 *count, const char *ssid, const char *password)
{
    river_wifi_credential_t *credential;
    size_t ssid_len;
    size_t password_len;

    if ((count == NULL) || (*count >= RIVER_WIFI_STA_MAX_CREDENTIALS) || (ssid == NULL) || (ssid[0] == '\0')) {
        return;
    }

    credential = &g_river_wifi_station.credentials[*count];
    memset(credential, 0, sizeof(*credential));

    ssid_len = strnlen(ssid, RTW_ESSID_MAX_SIZE);
    password_len = strnlen(password ? password : "", RTW_MAX_PSK_LEN);
    if ((ssid_len == 0U) || (ssid_len > RTW_ESSID_MAX_SIZE)) {
        return;
    }

    river_wifi_station_copy_string(credential->ssid, sizeof(credential->ssid), ssid);
    river_wifi_station_copy_string(credential->password, sizeof(credential->password), password ? password : "");
    credential->ssid_len = (u8)ssid_len;
    credential->password_len = (u8)password_len;
    credential->enabled = true;
    (*count)++;
}

static void river_wifi_station_load_credentials(void)
{
    u8 index;

    g_river_wifi_station.credential_count = 0U;
    memset(g_river_wifi_station.credentials, 0, sizeof(g_river_wifi_station.credentials));

    river_wifi_station_add_credential(&g_river_wifi_station.credential_count,
                                      RIVER_WIFI_STA_PRIMARY_SSID,
                                      RIVER_WIFI_STA_PRIMARY_PASSWORD);
    river_wifi_station_add_credential(&g_river_wifi_station.credential_count,
                                      RIVER_WIFI_STA_SECONDARY_SSID,
                                      RIVER_WIFI_STA_SECONDARY_PASSWORD);

    for (index = 0U; index < g_river_wifi_station.credential_count; ++index) {
        const river_wifi_credential_t *credential = &g_river_wifi_station.credentials[index];
        RIVER_LOGI("credential[%u] ssid=%s ssid_len=%u password_len=%u",
                   (unsigned int)index,
                   credential->ssid,
                   (unsigned int)credential->ssid_len,
                   (unsigned int)credential->password_len);
    }
}

static const river_wifi_credential_t *river_wifi_station_get_active_credential(void)
{
    if (g_river_wifi_station.active_credential_valid &&
        (g_river_wifi_station.active_credential_index < g_river_wifi_station.credential_count)) {
        return &g_river_wifi_station.credentials[g_river_wifi_station.active_credential_index];
    }

    if (g_river_wifi_station.credential_count > 0U) {
        return &g_river_wifi_station.credentials[0];
    }

    return NULL;
}

static const char *river_wifi_station_selected_ssid(void)
{
    const river_wifi_credential_t *credential = river_wifi_station_get_active_credential();

    if ((credential != NULL) && (credential->ssid[0] != '\0')) {
        return credential->ssid;
    }

    return "-";
}

static void river_wifi_station_select_credential(u8 credential_index)
{
    if (credential_index >= g_river_wifi_station.credential_count) {
        return;
    }

    g_river_wifi_station.active_credential_valid = true;
    g_river_wifi_station.active_credential_index = credential_index;
    river_wifi_station_copy_string(g_river_wifi_station.active_ssid,
                                   sizeof(g_river_wifi_station.active_ssid),
                                   g_river_wifi_station.credentials[credential_index].ssid);
}

static bool river_wifi_station_strategy_uses_candidate(river_wifi_connect_strategy_t strategy)
{
    switch (strategy) {
    case RIVER_WIFI_CONNECT_STRATEGY_SCAN_AUTO:
    case RIVER_WIFI_CONNECT_STRATEGY_SCAN_WPA2_MIXED:
    case RIVER_WIFI_CONNECT_STRATEGY_SCAN_WPA_WPA2_MIXED:
    case RIVER_WIFI_CONNECT_STRATEGY_SCAN_WPA2_AES:
        return true;
    default:
        return false;
    }
}

static u32 river_wifi_station_strategy_security_type(const river_wifi_credential_t *credential,
                                                     river_wifi_connect_strategy_t strategy)
{
    if ((credential == NULL) || (credential->password_len == 0U)) {
        return RTW_SECURITY_OPEN;
    }

    switch (strategy) {
    case RIVER_WIFI_CONNECT_STRATEGY_SCAN_WPA2_MIXED:
    case RIVER_WIFI_CONNECT_STRATEGY_BASIC_WPA2_MIXED:
        return RTW_SECURITY_WPA2_MIXED_PSK;
    case RIVER_WIFI_CONNECT_STRATEGY_SCAN_WPA_WPA2_MIXED:
    case RIVER_WIFI_CONNECT_STRATEGY_BASIC_WPA_WPA2_MIXED:
        return RTW_SECURITY_WPA_WPA2_MIXED_PSK;
    case RIVER_WIFI_CONNECT_STRATEGY_SCAN_WPA2_AES:
    case RIVER_WIFI_CONNECT_STRATEGY_BASIC_WPA2_AES:
        return RTW_SECURITY_WPA2_AES_PSK;
    case RIVER_WIFI_CONNECT_STRATEGY_SCAN_AUTO:
    case RIVER_WIFI_CONNECT_STRATEGY_BASIC_AUTO:
    default:
        return 0U;
    }
}

static const char *river_wifi_station_strategy_security_name(const river_wifi_credential_t *credential,
                                                             river_wifi_connect_strategy_t strategy)
{
    u32 security_type = river_wifi_station_strategy_security_type(credential, strategy);

    if ((credential != NULL) && (credential->password_len > 0U) && (security_type == 0U)) {
        return "auto";
    }

    return river_wifi_station_security_name(security_type);
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

static void river_wifi_station_log_join_snapshot(const char *source)
{
    u8 join_status = RTW_JOINSTATUS_UNKNOWN;
    int ret;

    ret = wifi_get_join_status(&join_status);
    if (ret == RTK_SUCCESS) {
        RIVER_LOGI("join snapshot source=%s status=%s(%u) has_ipv4=%s",
                   source,
                   river_wifi_station_join_status_name(join_status),
                   (unsigned int)join_status,
                   river_wifi_station_has_ipv4() ? "yes" : "no");
    } else {
        RIVER_LOGW("join snapshot source=%s get_status_failed ret=%d", source, ret);
    }
}

static void river_wifi_station_log_phy_snapshot(const char *source)
{
    union rtw_phy_stats phy_stats;
    int ret;

    memset(&phy_stats, 0, sizeof(phy_stats));
    ret = wifi_get_phy_stats(STA_WLAN_INDEX, NULL, &phy_stats);
    if (ret == RTK_SUCCESS) {
        RIVER_LOGI("phy snapshot source=%s rssi=%d data_rssi=%d beacon_rssi=%d snr=%d",
                   source,
                   (int)phy_stats.sta.rssi,
                   (int)phy_stats.sta.data_rssi,
                   (int)phy_stats.sta.beacon_rssi,
                   (int)phy_stats.sta.snr);
    } else {
        RIVER_LOGW("phy snapshot source=%s failed ret=%d", source, ret);
    }
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

    river_wifi_station_log_user_config("patch_entry");
    wifi_set_user_config();
    river_wifi_station_log_user_config("sdk_defaults");

    wifi_user_config.country_code[0] = '0';
    wifi_user_config.country_code[1] = '0';
    wifi_user_config.freq_band_support = RTW_SUPPORT_BAND_2_4G_5G_BOTH;
    wifi_user_config.tx_pwr_table_selection = 1;
    wifi_user_config.rtw_802_11d_en = 0;
    wifi_user_config.fast_reconnect_en = 0;
    wifi_user_config.auto_reconnect_en = 0;
    wifi_user_config.auto_reconnect_count = 0;
    wifi_user_config.auto_reconnect_interval = 0;
    wifi_user_config.ips_enable = 0;
    wifi_user_config.ips_ctrl_by_usr = 1;
    wifi_user_config.lps_enable = 0;

    g_river_wifi_station.sdk_user_config_patched = true;
    RIVER_LOGI("sdk defaults loaded then project patch applied before wifi_on: country=00 band=2.4g+5g tx_pwr_sel=1 fast_reconnect=0 auto_reconnect=0 ips=0 lps=0");
    river_wifi_station_log_user_config("patch_after");
}

static int river_wifi_station_query_wifi_is_running(void)
{
    uint32_t start_ms;
    uint32_t end_ms;
    uint32_t elapsed_ms;
    int running;
    bool log_start;

    start_ms = (uint32_t)rtos_time_get_current_system_time_ms();
    g_river_wifi_station.wifi_is_running_pending = true;
    g_river_wifi_station.wifi_is_running_start_ms = start_ms;
    g_river_wifi_station.wifi_is_running_attempts++;

    log_start = (g_river_wifi_station.wifi_is_running_attempts <= 4U) ||
                (!g_river_wifi_station.wifi_on_seen_success) ||
                (!g_river_wifi_station.connected);
    if (log_start) {
        RIVER_LOGI("wifi_is_running start attempt=%lu wlan=%u whc_api=0x4",
                   (unsigned long)g_river_wifi_station.wifi_is_running_attempts,
                   (unsigned int)STA_WLAN_INDEX);
    }
    running = wifi_is_running(STA_WLAN_INDEX);

    end_ms = (uint32_t)rtos_time_get_current_system_time_ms();
    elapsed_ms = end_ms - start_ms;
    g_river_wifi_station.wifi_is_running_pending = false;
    g_river_wifi_station.wifi_is_running_last_result = running;
    g_river_wifi_station.wifi_is_running_last_elapsed_ms = elapsed_ms;
    if (log_start ||
        (running == 0) ||
        (elapsed_ms >= 1000U)) {
        RIVER_LOGI("wifi_is_running returned ret=%d elapsed_ms=%lu",
                   running,
                   (unsigned long)elapsed_ms);
    }

    return running;
}

static int river_wifi_station_wifi_on_sta(void)
{
    uint32_t start_ms;
    uint32_t end_ms;
    uint32_t elapsed_ms;
    int ret;

    start_ms = (uint32_t)rtos_time_get_current_system_time_ms();
    g_river_wifi_station.wifi_on_pending = true;
    g_river_wifi_station.wifi_on_start_ms = start_ms;
    g_river_wifi_station.wifi_on_attempts++;

    river_wifi_station_log_user_config("wifi_on_start");
    RIVER_LOGI("wifi_on start attempt=%lu mode=sta whc_api=0x9 note=no-return-log-means-sdk-wifi_on-stalled",
               (unsigned long)g_river_wifi_station.wifi_on_attempts);

    ret = wifi_on(RTW_MODE_STA);

    end_ms = (uint32_t)rtos_time_get_current_system_time_ms();
    elapsed_ms = end_ms - start_ms;
    g_river_wifi_station.wifi_on_pending = false;
    g_river_wifi_station.wifi_on_last_result = ret;
    g_river_wifi_station.wifi_on_last_elapsed_ms = elapsed_ms;
    if (ret == RTK_SUCCESS) {
        g_river_wifi_station.wifi_on_seen_success = true;
        RIVER_LOGI("wifi_on returned ret=%d elapsed_ms=%lu", ret, (unsigned long)elapsed_ms);
    } else {
        RIVER_LOGW("wifi_on returned ret=%d elapsed_ms=%lu", ret, (unsigned long)elapsed_ms);
    }

    return ret;
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
    const river_wifi_credential_t *credential = river_wifi_station_get_active_credential();
    u32 current_ip;

    current_ip = *(u32 *)LwIP_GetIP(NETIF_WLAN_STA_INDEX);
    if (g_river_wifi_station.connection_latched &&
        (g_river_wifi_station.last_connected_ip == current_ip) &&
        (strcmp(g_river_wifi_station.last_connected_ssid, river_wifi_station_selected_ssid()) == 0)) {
        g_river_wifi_station.connected = true;
        g_river_wifi_station.connecting = false;
        g_river_wifi_station.last_error = 0;
        return;
    }

    g_river_wifi_station.connected = true;
    g_river_wifi_station.connecting = false;
    g_river_wifi_station.connection_latched = true;
    g_river_wifi_station.connect_successes++;
    g_river_wifi_station.last_error = 0;
    g_river_wifi_station.last_connected_ip = current_ip;
    river_wifi_station_copy_string(g_river_wifi_station.last_connected_ssid,
                                   sizeof(g_river_wifi_station.last_connected_ssid),
                                   river_wifi_station_selected_ssid());
    if (credential != NULL) {
        g_river_wifi_station.next_credential_index = g_river_wifi_station.active_credential_index;
    }
    RIVER_LOGI("connected ssid=%s ip=%u.%u.%u.%u success=%lu",
               river_wifi_station_selected_ssid(),
               (unsigned int)LwIP_GetIP(NETIF_WLAN_STA_INDEX)[0],
               (unsigned int)LwIP_GetIP(NETIF_WLAN_STA_INDEX)[1],
               (unsigned int)LwIP_GetIP(NETIF_WLAN_STA_INDEX)[2],
               (unsigned int)LwIP_GetIP(NETIF_WLAN_STA_INDEX)[3],
               (unsigned long)g_river_wifi_station.connect_successes);
    river_wifi_station_log_join_snapshot("connected");
    river_wifi_station_log_phy_snapshot("connected");
    river_runtime_stats_snapshot("wifi_connected");
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
    uint32_t attempt = 0U;

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

    for (attempt = 0U; attempt < RIVER_WIFI_STA_DHCP_RETRY_COUNT; ++attempt) {
        if (!river_wifi_station_joined()) {
            break;
        }

        if (river_wifi_station_has_ipv4()) {
            river_wifi_station_mark_connected();
            return true;
        }

        dhcp_result = river_wifi_station_request_dhcp();
        if (dhcp_result == DHCP_ADDRESS_ASSIGNED) {
            river_wifi_station_mark_connected();
            return true;
        }

        if (river_wifi_station_has_ipv4()) {
            river_wifi_station_mark_connected();
            return true;
        }

        if (!river_wifi_station_joined()) {
            break;
        }

        if ((attempt + 1U) < RIVER_WIFI_STA_DHCP_RETRY_COUNT) {
            RIVER_LOGW("dhcp pending/timeout after join source=%s ret=%d; retry=%lu/%u",
                       source,
                       dhcp_result,
                       (unsigned long)(attempt + 1U),
                       (unsigned int)RIVER_WIFI_STA_DHCP_RETRY_COUNT);
            rtos_time_delay_ms(RIVER_WIFI_STA_DHCP_RETRY_BACKOFF_MS);
        } else {
            RIVER_LOGW("dhcp pending after join source=%s ret=%d; retries exhausted=%u",
                       source,
                       dhcp_result,
                       (unsigned int)RIVER_WIFI_STA_DHCP_RETRY_COUNT);
        }
    }

    if (river_wifi_station_has_ipv4()) {
        river_wifi_station_mark_connected();
        return true;
    }

    g_river_wifi_station.last_error = dhcp_result;
    RIVER_LOGW("dhcp pending/failed after join ssid=%s source=%s ret=%d retries=%u",
               river_wifi_station_selected_ssid(),
               source,
               dhcp_result,
               (unsigned int)RIVER_WIFI_STA_DHCP_RETRY_COUNT);
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
    uint32_t last_log_ms = 0U;
    u8 join_status = RTW_JOINSTATUS_UNKNOWN;
    u8 last_join_status = RTW_JOINSTATUS_UNKNOWN;

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
            RIVER_LOGW("join wait stopped status=%s(%u) waited_ms=%lu",
                       river_wifi_station_join_status_name(join_status),
                       (unsigned int)join_status,
                       (unsigned long)waited_ms);
            return false;
        }

        if ((join_status != last_join_status) || (waited_ms - last_log_ms >= 1000U)) {
            RIVER_LOGI("join wait status=%s(%u) waited_ms=%lu timeout_ms=%lu",
                       river_wifi_station_join_status_name(join_status),
                       (unsigned int)join_status,
                       (unsigned long)waited_ms,
                       (unsigned long)timeout_ms);
            last_join_status = join_status;
            last_log_ms = waited_ms;
        }

        rtos_time_delay_ms(200);
        waited_ms += 200U;
    }

    river_wifi_station_log_join_snapshot("join_wait_timeout");
    return river_wifi_station_try_complete_join_without_reconnect();
}

static bool river_wifi_station_scan_candidate_better(const river_wifi_scan_candidate_t *current,
                                                     const struct rtw_scan_result *incoming)
{
    if ((current == NULL) || (!current->valid)) {
        return true;
    }

    if ((current->result.band != RTW_BAND_ON_24G) && (incoming->band == RTW_BAND_ON_24G)) {
        return true;
    }

    if ((current->result.band == incoming->band) &&
        (incoming->signal_strength > current->result.signal_strength)) {
        return true;
    }

    return false;
}

static bool river_wifi_station_credential_is_sticky(u8 credential_index)
{
    if (credential_index >= g_river_wifi_station.credential_count) {
        return false;
    }

    if (g_river_wifi_station.active_credential_valid &&
        credential_index == g_river_wifi_station.active_credential_index) {
        return true;
    }

    if ((g_river_wifi_station.last_connected_ssid[0] != '\0') &&
        (strcmp(g_river_wifi_station.last_connected_ssid,
                g_river_wifi_station.credentials[credential_index].ssid) == 0)) {
        return true;
    }

    return false;
}

static int river_wifi_station_candidate_retry_rssi_floor(u8 credential_index)
{
    if (river_wifi_station_credential_is_sticky(credential_index)) {
        return RIVER_WIFI_STA_STICKY_RETRY_RSSI_DBM;
    }

    return RIVER_WIFI_STA_MIN_RSSI_DBM;
}

static bool river_wifi_station_candidate_signal_acceptable(u8 credential_index,
                                                           const river_wifi_scan_candidate_t *candidate)
{
    if ((candidate == NULL) || (!candidate->valid)) {
        return true;
    }

    return candidate->result.signal_strength >= river_wifi_station_candidate_retry_rssi_floor(credential_index);
}

static void river_wifi_station_append_strategy_unique(river_wifi_connect_strategy_t *strategies,
                                                      size_t *strategy_count,
                                                      size_t capacity,
                                                      river_wifi_connect_strategy_t strategy)
{
    size_t index;

    if (strategies == NULL || strategy_count == NULL || *strategy_count >= capacity) {
        return;
    }

    for (index = 0U; index < *strategy_count; ++index) {
        if (strategies[index] == strategy) {
            return;
        }
    }

    strategies[(*strategy_count)++] = strategy;
}

static bool river_wifi_station_security_preferred_scan_strategy(u32 security,
                                                                river_wifi_connect_strategy_t *strategy)
{
    if (strategy == NULL) {
        return false;
    }

    switch (security) {
    case RTW_SECURITY_WPA2_AES_PSK:
        *strategy = RIVER_WIFI_CONNECT_STRATEGY_SCAN_WPA2_AES;
        return true;
    case RTW_SECURITY_WPA2_TKIP_PSK:
    case RTW_SECURITY_WPA2_MIXED_PSK:
        *strategy = RIVER_WIFI_CONNECT_STRATEGY_SCAN_WPA2_MIXED;
        return true;
    case RTW_SECURITY_WPA_TKIP_PSK:
    case RTW_SECURITY_WPA_AES_PSK:
    case RTW_SECURITY_WPA_MIXED_PSK:
    case RTW_SECURITY_WPA_WPA2_TKIP_PSK:
    case RTW_SECURITY_WPA_WPA2_AES_PSK:
    case RTW_SECURITY_WPA_WPA2_MIXED_PSK:
        *strategy = RIVER_WIFI_CONNECT_STRATEGY_SCAN_WPA_WPA2_MIXED;
        return true;
    default:
        return false;
    }
}

static bool river_wifi_station_security_preferred_basic_strategy(u32 security,
                                                                 river_wifi_connect_strategy_t *strategy)
{
    if (strategy == NULL) {
        return false;
    }

    switch (security) {
    case RTW_SECURITY_WPA2_AES_PSK:
        *strategy = RIVER_WIFI_CONNECT_STRATEGY_BASIC_WPA2_AES;
        return true;
    case RTW_SECURITY_WPA2_TKIP_PSK:
    case RTW_SECURITY_WPA2_MIXED_PSK:
        *strategy = RIVER_WIFI_CONNECT_STRATEGY_BASIC_WPA2_MIXED;
        return true;
    case RTW_SECURITY_WPA_TKIP_PSK:
    case RTW_SECURITY_WPA_AES_PSK:
    case RTW_SECURITY_WPA_MIXED_PSK:
    case RTW_SECURITY_WPA_WPA2_TKIP_PSK:
    case RTW_SECURITY_WPA_WPA2_AES_PSK:
    case RTW_SECURITY_WPA_WPA2_MIXED_PSK:
        *strategy = RIVER_WIFI_CONNECT_STRATEGY_BASIC_WPA_WPA2_MIXED;
        return true;
    default:
        return false;
    }
}

static bool river_wifi_station_has_alternate_acceptable_candidate(const river_wifi_scan_candidate_t *candidates,
                                                                  u8 current_credential_index)
{
    u8 index;

    if (candidates == NULL) {
        return false;
    }

    for (index = 0U; index < g_river_wifi_station.credential_count; ++index) {
        if (index == current_credential_index || !g_river_wifi_station.credentials[index].enabled) {
            continue;
        }

        if (river_wifi_station_candidate_signal_acceptable(index, &candidates[index])) {
            return true;
        }
    }

    return false;
}

static void river_wifi_station_scan_targets(river_wifi_scan_candidate_t *candidates, size_t candidate_count)
{
    struct rtw_scan_param scan_param;
    struct rtw_scan_result *records = NULL;
    int scanned_ap_num;
    u32 ap_num;
    u32 i;
    size_t credential_index;
    u32 log_count = 0U;
    bool any_candidate = false;

    memset(candidates, 0, candidate_count * sizeof(*candidates));
    memset(&scan_param, 0, sizeof(scan_param));
    scan_param.ssid = NULL;
    scan_param.max_ap_record_num = 24;

    RIVER_LOGI("scan start configured_ap=%u max_records=%lu",
               (unsigned int)g_river_wifi_station.credential_count,
               (unsigned long)scan_param.max_ap_record_num);
    if (!river_wifi_station_wait_driver_idle(RIVER_WIFI_STA_IDLE_WAIT_MS, false)) {
        river_wifi_station_log_join_snapshot("scan_wait_idle_timeout");
        RIVER_LOGW("scan wait-idle timeout for configured ap list; skip active scan this round");
        return;
    }

    scanned_ap_num = wifi_scan_networks(&scan_param, 1);
    if (scanned_ap_num == -RTK_ERR_BUSY) {
        RIVER_LOGW("scan busy for configured ap list; wait idle and retry once");
        if (river_wifi_station_wait_driver_idle(RIVER_WIFI_STA_IDLE_WAIT_MS, false)) {
            scanned_ap_num = wifi_scan_networks(&scan_param, 1);
        }
    }
    if (scanned_ap_num <= 0) {
        RIVER_LOGW("scan failed for configured ap list ret=%d", scanned_ap_num);
        return;
    }

    ap_num = (u32)scanned_ap_num;
    RIVER_LOGI("scan done ret=%d ap_num=%lu", scanned_ap_num, (unsigned long)ap_num);
    records = (struct rtw_scan_result *)rtos_mem_zmalloc(ap_num * sizeof(struct rtw_scan_result));
    if (records == NULL) {
        RIVER_LOGW("scan result alloc failed ap_num=%lu", (unsigned long)ap_num);
        return;
    }

    if (wifi_get_scan_records(&ap_num, records) != RTK_SUCCESS) {
        RIVER_LOGW("scan record fetch failed");
        rtos_mem_free(records);
        return;
    }

    for (i = 0; i < ap_num; ++i) {
        struct rtw_scan_result *record = &records[i];
        char scanned_ssid[RTW_ESSID_MAX_SIZE + 1U];

        if (log_count < RIVER_WIFI_STA_SCAN_LOG_LIMIT) {
            river_wifi_station_format_ssid(&record->ssid, scanned_ssid, sizeof(scanned_ssid));
            RIVER_LOGI("scan ap[%lu] ssid=%s bssid=%02x:%02x:%02x:%02x:%02x:%02x ch=%lu band=%s rssi=%d sec=%s(0x%lx)",
                       (unsigned long)i,
                       scanned_ssid[0] != '\0' ? scanned_ssid : "<hidden>",
                       record->bssid.octet[0],
                       record->bssid.octet[1],
                       record->bssid.octet[2],
                       record->bssid.octet[3],
                       record->bssid.octet[4],
                       record->bssid.octet[5],
                       (unsigned long)record->channel,
                       record->band == RTW_BAND_ON_5G ? "5g" : "2.4g",
                       (int)record->signal_strength,
                       river_wifi_station_security_name(record->security),
                       (unsigned long)record->security);
            log_count++;
        }

        for (credential_index = 0; credential_index < candidate_count; ++credential_index) {
            const river_wifi_credential_t *credential = &g_river_wifi_station.credentials[credential_index];

            if (!credential->enabled) {
                continue;
            }

            if ((record->ssid.len != credential->ssid_len) ||
                (memcmp(record->ssid.val, credential->ssid, credential->ssid_len) != 0)) {
                continue;
            }

            if (river_wifi_station_scan_candidate_better(&candidates[credential_index], record)) {
                candidates[credential_index].valid = true;
                memcpy(&candidates[credential_index].result, record, sizeof(candidates[credential_index].result));
            }
        }
    }

    rtos_mem_free(records);

    for (credential_index = 0; credential_index < candidate_count; ++credential_index) {
        const river_wifi_credential_t *credential = &g_river_wifi_station.credentials[credential_index];
        const river_wifi_scan_candidate_t *candidate = &candidates[credential_index];

        if (!credential->enabled) {
            continue;
        }

        if (candidate->valid) {
            any_candidate = true;
            RIVER_LOGI("scan candidate ssid=%s index=%lu bssid=%02x:%02x:%02x:%02x:%02x:%02x ch=%lu band=%s rssi=%d sec=%s(0x%lx)",
                       credential->ssid,
                       (unsigned long)credential_index,
                       candidate->result.bssid.octet[0],
                       candidate->result.bssid.octet[1],
                       candidate->result.bssid.octet[2],
                       candidate->result.bssid.octet[3],
                       candidate->result.bssid.octet[4],
                       candidate->result.bssid.octet[5],
                       (unsigned long)candidate->result.channel,
                       candidate->result.band == RTW_BAND_ON_5G ? "5g" : "2.4g",
                       (int)candidate->result.signal_strength,
                       river_wifi_station_security_name(candidate->result.security),
                       (unsigned long)candidate->result.security);
            if (candidate->result.signal_strength < RIVER_WIFI_STA_MIN_RSSI_DBM) {
                RIVER_LOGW("scan candidate ssid=%s weak rssi=%d threshold=%d; de-prioritize this round",
                           credential->ssid,
                           (int)candidate->result.signal_strength,
                           (int)RIVER_WIFI_STA_MIN_RSSI_DBM);
            }
        }
    }

    if (!any_candidate) {
        RIVER_LOGW("scan candidate not found for configured ap list ap_num=%lu configured_ap=%u",
                   (unsigned long)ap_num,
                   (unsigned int)g_river_wifi_station.credential_count);
    }
}

static void river_wifi_station_fill_connect_param_basic(struct rtw_network_info *connect_param,
                                                        const river_wifi_credential_t *credential,
                                                        river_wifi_connect_strategy_t strategy)
{
    memset(connect_param, 0, sizeof(*connect_param));
    memcpy(connect_param->ssid.val,
           credential->ssid,
           credential->ssid_len);
    connect_param->ssid.len = credential->ssid_len;
    connect_param->password = credential->password_len ? (unsigned char *)credential->password : NULL;
    connect_param->password_len = credential->password_len;
    connect_param->security_type = river_wifi_station_strategy_security_type(credential, strategy);
}

static void river_wifi_station_fill_connect_param(struct rtw_network_info *connect_param,
                                                  const river_wifi_credential_t *credential,
                                                  const river_wifi_scan_candidate_t *candidate,
                                                  river_wifi_connect_strategy_t strategy)
{
    river_wifi_station_fill_connect_param_basic(connect_param, credential, strategy);

    if ((candidate == NULL) || (!candidate->valid) ||
        !river_wifi_station_strategy_uses_candidate(strategy)) {
        return;
    }

    memcpy(connect_param->bssid.octet,
           candidate->result.bssid.octet,
           sizeof(connect_param->bssid.octet));
    connect_param->channel = (u8)candidate->result.channel;
    connect_param->pscan_option = RTW_PSCAN_FAST_SURVEY;
}

static void river_wifi_station_build_strategy_order(const river_wifi_credential_t *credential,
                                                    const river_wifi_scan_candidate_t *candidate,
                                                    river_wifi_connect_strategy_t *strategies,
                                                    size_t *strategy_count)
{
    river_wifi_connect_strategy_t preferred_strategy;

    *strategy_count = 0U;

    if (credential->password_len == 0U) {
        if ((candidate != NULL) && candidate->valid) {
            river_wifi_station_append_strategy_unique(strategies,
                                                      strategy_count,
                                                      7U,
                                                      RIVER_WIFI_CONNECT_STRATEGY_SCAN_AUTO);
        }
        river_wifi_station_append_strategy_unique(strategies,
                                                  strategy_count,
                                                  7U,
                                                  RIVER_WIFI_CONNECT_STRATEGY_BASIC_AUTO);
        return;
    }

    if ((candidate != NULL) && candidate->valid) {
        if (river_wifi_station_security_preferred_scan_strategy(candidate->result.security, &preferred_strategy)) {
            river_wifi_station_append_strategy_unique(strategies,
                                                      strategy_count,
                                                      7U,
                                                      preferred_strategy);
        }
        river_wifi_station_append_strategy_unique(strategies,
                                                  strategy_count,
                                                  7U,
                                                  RIVER_WIFI_CONNECT_STRATEGY_SCAN_AUTO);
        river_wifi_station_append_strategy_unique(strategies,
                                                  strategy_count,
                                                  7U,
                                                  RIVER_WIFI_CONNECT_STRATEGY_SCAN_WPA2_AES);
        river_wifi_station_append_strategy_unique(strategies,
                                                  strategy_count,
                                                  7U,
                                                  RIVER_WIFI_CONNECT_STRATEGY_SCAN_WPA2_MIXED);
        river_wifi_station_append_strategy_unique(strategies,
                                                  strategy_count,
                                                  7U,
                                                  RIVER_WIFI_CONNECT_STRATEGY_SCAN_WPA_WPA2_MIXED);
    }

    if ((candidate != NULL) && candidate->valid &&
        river_wifi_station_security_preferred_basic_strategy(candidate->result.security, &preferred_strategy)) {
        river_wifi_station_append_strategy_unique(strategies,
                                                  strategy_count,
                                                  7U,
                                                  preferred_strategy);
    }
    river_wifi_station_append_strategy_unique(strategies,
                                              strategy_count,
                                              7U,
                                              RIVER_WIFI_CONNECT_STRATEGY_BASIC_AUTO);
    river_wifi_station_append_strategy_unique(strategies,
                                              strategy_count,
                                              7U,
                                              RIVER_WIFI_CONNECT_STRATEGY_BASIC_WPA2_AES);
    river_wifi_station_append_strategy_unique(strategies,
                                              strategy_count,
                                              7U,
                                              RIVER_WIFI_CONNECT_STRATEGY_BASIC_WPA2_MIXED);
    river_wifi_station_append_strategy_unique(strategies,
                                              strategy_count,
                                              7U,
                                              RIVER_WIFI_CONNECT_STRATEGY_BASIC_WPA_WPA2_MIXED);
}

static void river_wifi_station_disconnect_and_wait_idle(uint32_t timeout_ms)
{
    uint32_t waited_ms = 0U;
    u8 join_status = RTW_JOINSTATUS_UNKNOWN;

    g_river_wifi_station.connected = false;
    g_river_wifi_station.connecting = false;
    g_river_wifi_station.connection_latched = false;
    g_river_wifi_station.last_connected_ip = 0U;
    g_river_wifi_station.last_connected_ssid[0] = '\0';

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
    river_wifi_scan_candidate_t candidates[RIVER_WIFI_STA_MAX_CREDENTIALS];
    river_wifi_connect_strategy_t strategy;
    river_wifi_connect_strategy_t strategy_order[7];
    size_t strategy_count = 0U;
    size_t strategy_index = 0U;
    size_t credential_offset = 0U;
    size_t credential_index = 0U;
    u8 join_status = RTW_JOINSTATUS_UNKNOWN;
    bool joined_in_wait;
    int result;

    (void)param;
    g_river_wifi_station.task_started = true;
    RIVER_LOGI("sta task started priority=%u stack=%u retry_ms=%u",
               (unsigned int)RIVER_WIFI_STA_TASK_PRIORITY,
               (unsigned int)RIVER_WIFI_STA_TASK_STACK,
               (unsigned int)RIVER_WIFI_STA_RETRY_MS);

    while (1) {
        int wifi_running;

        g_river_wifi_station.task_loop_count++;
        wifi_running = river_wifi_station_query_wifi_is_running();
        if (!wifi_running) {
            river_wifi_station_force_sdk_fast_connect_off();
            river_wifi_station_patch_user_config_once();
            result = river_wifi_station_wifi_on_sta();
            if (result != RTK_SUCCESS) {
                g_river_wifi_station.last_error = result;
                g_river_wifi_station.connect_failures++;
                RIVER_LOGW("wifi_on failed ret=%d failures=%lu retry_ms=%u",
                           result,
                           (unsigned long)g_river_wifi_station.connect_failures,
                           (unsigned int)RIVER_WIFI_STA_RETRY_MS);
                rtos_time_delay_ms(RIVER_WIFI_STA_RETRY_MS);
                continue;
            }
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
            rtos_time_delay_ms(RIVER_WIFI_STA_POWER_SETTLE_MS);
            continue;
        }

        if (river_wifi_station_is_ready()) {
            river_wifi_station_mark_connected();
            rtos_time_delay_ms(1000);
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

        g_river_wifi_station.connected = false;
        g_river_wifi_station.connecting = true;
        g_river_wifi_station.connection_latched = false;
        g_river_wifi_station.connect_attempts++;

        RIVER_LOGI("connect attempt=%lu ap_count=%u next_index=%u current=%s",
                   (unsigned long)g_river_wifi_station.connect_attempts,
                   (unsigned int)g_river_wifi_station.credential_count,
                   (unsigned int)g_river_wifi_station.next_credential_index,
                   river_wifi_station_selected_ssid());
        river_wifi_station_scan_targets(candidates, g_river_wifi_station.credential_count);
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

        joined_in_wait = false;
        result = RTK_FAIL;

        for (credential_offset = 0U; credential_offset < g_river_wifi_station.credential_count; ++credential_offset) {
            const river_wifi_credential_t *credential;
            const river_wifi_scan_candidate_t *candidate;

            credential_index = (g_river_wifi_station.next_credential_index + credential_offset) %
                               g_river_wifi_station.credential_count;
            credential = &g_river_wifi_station.credentials[credential_index];
            candidate = &candidates[credential_index];

            if (!credential->enabled) {
                continue;
            }

            if ((candidate != NULL) && candidate->valid &&
                !river_wifi_station_candidate_signal_acceptable((u8)credential_index, candidate)) {
                RIVER_LOGW("skip ssid=%s this round: weak candidate rssi=%d threshold=%d sticky=%s",
                           credential->ssid,
                           (int)candidate->result.signal_strength,
                           river_wifi_station_candidate_retry_rssi_floor((u8)credential_index),
                           river_wifi_station_credential_is_sticky((u8)credential_index) ? "yes" : "no");
                continue;
            }

            river_wifi_station_build_strategy_order(credential, candidate, strategy_order, &strategy_count);
            river_wifi_station_select_credential((u8)credential_index);

            for (strategy_index = 0U; strategy_index < strategy_count; ++strategy_index) {
                strategy = strategy_order[strategy_index];

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

                river_wifi_station_fill_connect_param(&connect_param, credential, candidate, strategy);
                RIVER_LOGI("connect strategy=%s ssid=%s ssid_len=%u password_len=%u channel=%u sec=%s bssid=%02x:%02x:%02x:%02x:%02x:%02x scan=%s rssi=%d",
                           river_wifi_station_connect_strategy_name(strategy),
                           credential->ssid,
                           (unsigned int)credential->ssid_len,
                           (unsigned int)credential->password_len,
                           (unsigned int)connect_param.channel,
                           river_wifi_station_strategy_security_name(credential, strategy),
                           connect_param.bssid.octet[0],
                           connect_param.bssid.octet[1],
                           connect_param.bssid.octet[2],
                           connect_param.bssid.octet[3],
                           connect_param.bssid.octet[4],
                           connect_param.bssid.octet[5],
                           (candidate != NULL && candidate->valid) ? "yes" : "no",
                           (candidate != NULL && candidate->valid) ? (int)candidate->result.signal_strength : 0);

                result = wifi_connect(&connect_param, 1);
                if (result == RTK_SUCCESS) {
                    g_river_wifi_station.next_credential_index = (u8)credential_index;
                    river_wifi_station_log_join_snapshot("wifi_connect_return_success");
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
                RIVER_LOGW("connect strategy=%s ssid=%s failed err=%d(%s) join=%s",
                           river_wifi_station_connect_strategy_name(strategy),
                           credential->ssid,
                           result,
                           river_wifi_station_error_name(result),
                           river_wifi_station_join_status_name(join_status));
                river_wifi_station_log_join_snapshot("wifi_connect_failed");
                river_wifi_station_disconnect_and_wait_idle(1500U);
            }

            if (result == RTK_SUCCESS) {
                break;
            }
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

            g_river_wifi_station.next_credential_index = g_river_wifi_station.active_credential_index;
            RIVER_LOGW("keep retrying current ssid=%s after dhcp failure; do not rotate ap yet",
                       river_wifi_station_selected_ssid());
            river_wifi_station_disconnect_and_wait_idle(1500U);
        } else {
            g_river_wifi_station.last_error = result;
        }

        if (wifi_get_join_status(&join_status) != RTK_SUCCESS) {
            join_status = RTW_JOINSTATUS_UNKNOWN;
        }

        g_river_wifi_station.connecting = false;
        g_river_wifi_station.connect_failures++;
        if (g_river_wifi_station.credential_count > 0U) {
            if (river_wifi_station_has_alternate_acceptable_candidate(candidates, (u8)credential_index)) {
                g_river_wifi_station.next_credential_index =
                    (u8)((credential_index + 1U) % g_river_wifi_station.credential_count);
            } else {
                g_river_wifi_station.next_credential_index = (u8)credential_index;
                RIVER_LOGW("stay on ssid=%s next round: no acceptable alternate candidate",
                           river_wifi_station_selected_ssid());
            }
        }
        RIVER_LOGW("connect failed ssid=%s err=%d(%s) join=%s failures=%lu retry_ms=%u",
                   river_wifi_station_selected_ssid(),
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
    river_wifi_station_load_credentials();
    if (g_river_wifi_station.credential_count == 0U) {
        RIVER_LOGE("autoconnect init failed: no configured wifi credentials");
        return RIVER_ERR_ARG;
    }
    river_wifi_station_select_credential(0U);
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
    RIVER_LOGI("autoconnect init: ap_count=%u primary=%s retry_ms=%u",
               (unsigned int)g_river_wifi_station.credential_count,
               river_wifi_station_selected_ssid(),
               (unsigned int)RIVER_WIFI_STA_RETRY_MS);
    return RIVER_OK;
}

bool river_wifi_station_is_connected(void)
{
    return g_river_wifi_station.connected;
}

const char *river_wifi_station_ssid(void)
{
    return river_wifi_station_selected_ssid();
}

const char *river_wifi_station_status_name(void)
{
    if (!g_river_wifi_station.initialized) {
        return "disabled";
    }
    if (g_river_wifi_station.wifi_on_pending ||
        g_river_wifi_station.wifi_is_running_pending ||
        (!g_river_wifi_station.wifi_on_seen_success &&
         g_river_wifi_station.wifi_on_attempts > 0U)) {
        return "starting";
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
    uint32_t wifi_is_running_elapsed_ms = g_river_wifi_station.wifi_is_running_last_elapsed_ms;
    uint32_t wifi_on_elapsed_ms = g_river_wifi_station.wifi_on_last_elapsed_ms;

    if (g_river_wifi_station.wifi_is_running_pending) {
        wifi_is_running_elapsed_ms = (uint32_t)rtos_time_get_current_system_time_ms() -
                                     g_river_wifi_station.wifi_is_running_start_ms;
    }
    if (g_river_wifi_station.wifi_on_pending) {
        wifi_on_elapsed_ms = (uint32_t)rtos_time_get_current_system_time_ms() -
                             g_river_wifi_station.wifi_on_start_ms;
    }

    RIVER_LOGI("status=%s ssid=%s attempts=%lu success=%lu fail=%lu last_err=%d wifi_task=%s loops=%lu wifi_is_running=%s attempts=%lu ret=%d elapsed_ms=%lu wifi_on=%s attempts=%lu ret=%d elapsed_ms=%lu",
               river_wifi_station_status_name(),
               river_wifi_station_ssid(),
               (unsigned long)g_river_wifi_station.connect_attempts,
               (unsigned long)g_river_wifi_station.connect_successes,
               (unsigned long)g_river_wifi_station.connect_failures,
               g_river_wifi_station.last_error,
               g_river_wifi_station.task_started ? "started" : "not_started",
               (unsigned long)g_river_wifi_station.task_loop_count,
               g_river_wifi_station.wifi_is_running_pending ? "pending" : "idle",
               (unsigned long)g_river_wifi_station.wifi_is_running_attempts,
               g_river_wifi_station.wifi_is_running_last_result,
               (unsigned long)wifi_is_running_elapsed_ms,
               g_river_wifi_station.wifi_on_pending ? "pending" :
                   (g_river_wifi_station.wifi_on_seen_success ? "ok" : "not_done"),
               (unsigned long)g_river_wifi_station.wifi_on_attempts,
               g_river_wifi_station.wifi_on_last_result,
               (unsigned long)wifi_on_elapsed_ms);
    river_wifi_station_log_join_snapshot("dump_status");
    if (g_river_wifi_station.connected) {
        river_wifi_station_log_phy_snapshot("dump_status");
    }
}
