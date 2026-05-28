/*
 * HP/KM4 Wi-Fi shim: allow empty-efuse development boards to pass the SDK
 * "Press any key to ignore and continue" wait inside wifi_on().
 */
#include "ameba_soc.h"

static volatile u32 g_river_wifi_hp_wifi_on_depth;
static volatile u32 g_river_wifi_hp_key_wait_pending;
static volatile u32 g_river_wifi_hp_key_wait_bypasses;

s32 __real_wifi_on(u8 mode);
u8 __real_LOGUART_Readable(void);
void __real_LOGUART_INTConfig(LOGUART_TypeDef *UARTLOG, u32 UART_IT, u32 newState);

s32 __wrap_wifi_on(u8 mode)
{
    s32 ret;

    g_river_wifi_hp_wifi_on_depth++;
    g_river_wifi_hp_key_wait_pending = 0;
    g_river_wifi_hp_key_wait_bypasses = 0;

    DiagPrintf("[river.wifi.hp] wifi_on start mode=%lu efuse_key_wait_bypass=armed\r\n",
               (unsigned long)mode);

    ret = __real_wifi_on(mode);

    if (g_river_wifi_hp_wifi_on_depth > 0) {
        g_river_wifi_hp_wifi_on_depth--;
    }
    g_river_wifi_hp_key_wait_pending = 0;

    DiagPrintf("[river.wifi.hp] wifi_on returned ret=%ld efuse_key_wait_bypass=%lu\r\n",
               (long)ret,
               (unsigned long)g_river_wifi_hp_key_wait_bypasses);

    return ret;
}

void __wrap_LOGUART_INTConfig(LOGUART_TypeDef *UARTLOG, u32 UART_IT, u32 newState)
{
    if (g_river_wifi_hp_wifi_on_depth > 0 && (UART_IT & LOGUART_BIT_ERBI) != 0U) {
        if (newState == DISABLE) {
            g_river_wifi_hp_key_wait_pending = 1;
        } else {
            g_river_wifi_hp_key_wait_pending = 0;
        }
    }

    __real_LOGUART_INTConfig(UARTLOG, UART_IT, newState);
}

u8 __wrap_LOGUART_Readable(void)
{
    if (g_river_wifi_hp_wifi_on_depth > 0 && g_river_wifi_hp_key_wait_pending != 0U) {
        g_river_wifi_hp_key_wait_pending = 0;
        g_river_wifi_hp_key_wait_bypasses++;
        DiagPrintf("[river.wifi.hp] efuse key wait bypassed\r\n");
        return TRUE;
    }

    return __real_LOGUART_Readable();
}
