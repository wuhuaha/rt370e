#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ameba_soc.h"
#include "i2c_api.h"
#include "os_wrapper.h"

#include "river/river_log.h"
#include "river/river_types.h"

#include "river_orvibo_ui_internal.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.ui.touch"

#define RIVER_TOUCH_SDA_PIN      _PB_10
#define RIVER_TOUCH_SCL_PIN      _PB_11
#define RIVER_TOUCH_INT_PIN      _PA_9
#define RIVER_TOUCH_RST_PIN      _PA_10
#define RIVER_TOUCH_I2C_HZ       400000

typedef struct {
    bool initialized;
    bool ready;
    uint8_t ack_addr;
    uint32_t scans;
    uint32_t ack_count;
    char summary[96];
} river_touch_context_t;

static river_touch_context_t g_touch;
static i2c_t g_touch_i2c;

static const uint8_t k_touch_addr_candidates[] = {
    0x55, 0x5C, 0x2C, 0x14, 0x38
};

static void river_touch_copy(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0U) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_size - 1U);
    dst[dst_size - 1U] = '\0';
}

static void river_touch_gpio_out(uint32_t pin, bool high)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.GPIO_Pin = pin;
    gpio.GPIO_Mode = GPIO_Mode_OUT;
    gpio.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(&gpio);
    GPIO_WriteBit(pin, high ? 1 : 0);
}

static void river_touch_gpio_input(uint32_t pin)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.GPIO_Pin = pin;
    gpio.GPIO_Mode = GPIO_Mode_IN;
    gpio.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(&gpio);
}

static void river_touch_reset_sequence(void)
{
    river_touch_gpio_out(RIVER_TOUCH_RST_PIN, false);
    river_touch_gpio_out(RIVER_TOUCH_INT_PIN, false);
    rtos_time_delay_ms(5);
    river_touch_gpio_out(RIVER_TOUCH_RST_PIN, true);
    rtos_time_delay_ms(50);
    river_touch_gpio_input(RIVER_TOUCH_INT_PIN);
}

static river_status_t river_touch_init_bus(void)
{
    if (g_touch.initialized) {
        return RIVER_OK;
    }
    river_touch_reset_sequence();
    memset(&g_touch_i2c, 0, sizeof(g_touch_i2c));
    i2c_init(&g_touch_i2c, RIVER_TOUCH_SDA_PIN, RIVER_TOUCH_SCL_PIN);
    i2c_frequency(&g_touch_i2c, RIVER_TOUCH_I2C_HZ);
    g_touch.initialized = true;
    return RIVER_OK;
}

river_status_t river_touch_sitronix_probe_scan(char *summary, size_t summary_size)
{
    size_t i;
    char line[96];
    char *cursor = line;
    size_t left = sizeof(line);
    int wrote;

    if (river_touch_init_bus() != RIVER_OK) {
        river_touch_copy(summary, summary_size, "i2c_init_failed");
        return RIVER_ERR_IO;
    }
    g_touch.scans++;
    g_touch.ack_count = 0U;
    g_touch.ready = false;
    g_touch.ack_addr = 0U;
    wrote = snprintf(cursor, left, "ack:");
    if (wrote < 0 || (size_t)wrote >= left) {
        return RIVER_ERR_IO;
    }
    cursor += wrote;
    left -= (size_t)wrote;
    for (i = 0U; i < sizeof(k_touch_addr_candidates); ++i) {
        uint8_t addr = k_touch_addr_candidates[i];
        int ret = i2c_write(&g_touch_i2c, addr, NULL, 0, 1);

        if (ret == 0) {
            g_touch.ack_count++;
            if (!g_touch.ready) {
                g_touch.ready = true;
                g_touch.ack_addr = addr;
            }
            wrote = snprintf(cursor, left, " 0x%02x", addr);
            if (wrote > 0 && (size_t)wrote < left) {
                cursor += wrote;
                left -= (size_t)wrote;
            }
        }
    }
    if (g_touch.ack_count == 0U) {
        river_touch_copy(line, sizeof(line), "ack:none");
    }
    river_touch_copy(g_touch.summary, sizeof(g_touch.summary), line);
    river_touch_copy(summary, summary_size, g_touch.summary);
    if (g_touch.ready) {
        RIVER_LOGI("touch probe: %s primary=0x%02x", g_touch.summary, g_touch.ack_addr);
        return RIVER_OK;
    }
    RIVER_LOGW("touch probe: %s", g_touch.summary);
    return RIVER_ERR_NOT_FOUND;
}

bool river_touch_sitronix_probe_ready(void)
{
    return g_touch.ready;
}

void river_touch_sitronix_probe_dump_status(void)
{
    RIVER_LOGI("touch: init=%s ready=%s addr=0x%02x scans=%lu ack=%lu summary=%s pins=sda=PB10 scl=PB11 int=PA9 rst=PA10",
               g_touch.initialized ? "yes" : "no",
               g_touch.ready ? "yes" : "no",
               g_touch.ack_addr,
               (unsigned long)g_touch.scans,
               (unsigned long)g_touch.ack_count,
               g_touch.summary[0] != '\0' ? g_touch.summary : "-");
}
