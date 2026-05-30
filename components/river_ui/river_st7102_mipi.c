#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ameba_soc.h"
#include "os_wrapper.h"

#include "river/river_log.h"
#include "river/river_types.h"

#include "river_orvibo_ui_internal.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.ui.panel"

#define RIVER_ST7102_WIDTH       480U
#define RIVER_ST7102_HEIGHT      480U
#define RIVER_ST7102_BPP         16U
#define RIVER_ST7102_MIPI_MHZ    1000000UL
#define RIVER_ST7102_T_LPX       5U
#define RIVER_ST7102_T_HS_PREP   6U
#define RIVER_ST7102_T_HS_TRAIL  8U
#define RIVER_ST7102_T_HS_EXIT   7U
#define RIVER_ST7102_T_HS_ZERO   10U

#define RIVER_MIPI_DSI_DCS_SHORT_WRITE       0x05U
#define RIVER_MIPI_DSI_DCS_SHORT_WRITE_PARAM 0x15U
#define RIVER_MIPI_DSI_DCS_LONG_WRITE        0x39U

typedef struct {
    uint8_t cmd;
    uint8_t len;
    const uint8_t *data;
    uint16_t delay_ms;
} river_st7102_cmd_t;

typedef struct {
    bool initialized;
    bool lcdc_enabled;
    uint32_t flips;
    uint32_t lane_num;
    uint32_t lane_mbps;
    uint32_t line_time;
    LCDC_InitTypeDef lcdc;
    MIPI_InitTypeDef mipi;
    char last_error[80];
} river_st7102_context_t;

static river_st7102_context_t g_st7102;

static const uint8_t k_st7102_99_a2[] = {0x71, 0x02, 0xA2};
static const uint8_t k_st7102_99_a3[] = {0x71, 0x02, 0xA3};
static const uint8_t k_st7102_99_a4[] = {0x71, 0x02, 0xA4};
static const uint8_t k_st7102_b0[] = {0x22, 0x61, 0x1E, 0x61, 0x2F, 0x39, 0x39};
static const uint8_t k_st7102_b7[] = {0x46, 0x46};
static const uint8_t k_st7102_bf[] = {0x50, 0x50};
static const uint8_t k_st7102_d7[] = {0x00, 0x10, 0x8C, 0x08, 0xF0, 0xF0};
static const uint8_t k_st7102_a3[] = {
    0x40, 0x03, 0x8C, 0x40, 0x45, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x1E, 0x01, 0x00, 0x12, 0x00, 0x45,
    0x05, 0x00, 0x00, 0x00, 0x00, 0x1E, 0x01, 0x00,
    0x12, 0x20, 0x52, 0x00, 0x05, 0x00, 0x00, 0xFF
};
static const uint8_t k_st7102_a6[] = {
    0x08, 0x00, 0x24, 0x55, 0x35, 0x00, 0x76, 0x40,
    0x4E, 0x4E, 0x00, 0x24, 0x55, 0x00, 0x00, 0x40,
    0x40, 0x4E, 0x4E, 0x02, 0xAC, 0x51, 0x00, 0xCC,
    0x40, 0x40, 0x4E, 0x4E, 0x00, 0xAC, 0x11, 0x00,
    0x00, 0x40, 0x40, 0x4E, 0x4E, 0x00, 0x00, 0x06,
    0x00, 0x00, 0x00, 0x00
};
static const uint8_t k_st7102_a7[] = {
    0x19, 0x19, 0x00, 0x64, 0x40, 0x07, 0x16, 0x40,
    0x00, 0x44, 0x43, 0x4E, 0x4E, 0x00, 0x64, 0x40,
    0x25, 0x34, 0x00, 0x00, 0x42, 0x41, 0x4E, 0x4E,
    0x00, 0x64, 0x40, 0x4B, 0x5A, 0x00, 0x00, 0x42,
    0x41, 0x4E, 0x4E, 0x00, 0x24, 0x40, 0x69, 0x78,
    0x00, 0x00, 0x40, 0x40, 0x4E, 0x4E, 0x00, 0x44
};
static const uint8_t k_st7102_ac[] = {
    0x00, 0x1C, 0x04, 0x1A, 0x19, 0x1B, 0x1B, 0x18,
    0x06, 0x13, 0x19, 0x11, 0x1B, 0x08, 0x18, 0x0A,
    0x01, 0x1C, 0x04, 0x1A, 0x19, 0x1B, 0x1B, 0x18,
    0x06, 0x12, 0x19, 0x10, 0x1B, 0x09, 0x18, 0x0B,
    0xBF, 0xAA, 0xBF, 0xAA, 0x00
};
static const uint8_t k_st7102_ad[] = {0xCC, 0x40, 0x46, 0x11, 0x04, 0x6F, 0x6F};
static const uint8_t k_st7102_e8[] = {
    0x30, 0x07, 0x05, 0x6A, 0x6A, 0x9C, 0x00, 0xE2,
    0x04, 0x00, 0x00, 0x00, 0x00, 0xEF
};
static const uint8_t k_st7102_75[] = {0x03, 0x04};
static const uint8_t k_st7102_e7[] = {
    0x8B, 0x3C, 0x00, 0x0C, 0xF0, 0x5D, 0x00, 0x5D,
    0x00, 0x5D, 0x00, 0x5D, 0x00, 0xFF, 0x00, 0x08,
    0x7B, 0x00, 0x00, 0xC8, 0x6A, 0x5A, 0x08, 0x1A,
    0x3C, 0x00, 0xA1, 0x01, 0x8C, 0x01, 0x7F, 0xF0,
    0x22
};
static const uint8_t k_st7102_e9[] = {0x3C, 0x7F, 0x08, 0x10, 0x1A, 0x7A, 0x22, 0x1A, 0x33};
static const uint8_t k_st7102_c8[] = {
    0x00, 0x00, 0x15, 0x26, 0x44, 0x00, 0x78, 0x03,
    0xBE, 0x06, 0x11, 0x1C, 0x09, 0x8A, 0x03, 0x21,
    0xD4, 0x01, 0x11, 0x0F, 0x22, 0x4A, 0x0F, 0x8F,
    0x0A, 0x32, 0xF0, 0x0A, 0x41, 0x0D, 0xF3, 0x80,
    0x0D, 0xAE, 0xC5, 0x03, 0xC4
};
static const uint8_t k_st7102_35[] = {0x00};
static const uint8_t k_st7102_36[] = {0x00};

static const river_st7102_cmd_t k_st7102_init_cmds[] = {
    {0x99, sizeof(k_st7102_99_a2), k_st7102_99_a2, 0},
    {0x99, sizeof(k_st7102_99_a3), k_st7102_99_a3, 0},
    {0x99, sizeof(k_st7102_99_a4), k_st7102_99_a4, 0},
    {0xB0, sizeof(k_st7102_b0), k_st7102_b0, 0},
    {0xB7, sizeof(k_st7102_b7), k_st7102_b7, 0},
    {0xBF, sizeof(k_st7102_bf), k_st7102_bf, 0},
    {0xD7, sizeof(k_st7102_d7), k_st7102_d7, 0},
    {0xA3, sizeof(k_st7102_a3), k_st7102_a3, 0},
    {0xA6, sizeof(k_st7102_a6), k_st7102_a6, 0},
    {0xA7, sizeof(k_st7102_a7), k_st7102_a7, 0},
    {0xAC, sizeof(k_st7102_ac), k_st7102_ac, 0},
    {0xAD, sizeof(k_st7102_ad), k_st7102_ad, 0},
    {0xE8, sizeof(k_st7102_e8), k_st7102_e8, 0},
    {0x75, sizeof(k_st7102_75), k_st7102_75, 0},
    {0xE7, sizeof(k_st7102_e7), k_st7102_e7, 0},
    {0xE9, sizeof(k_st7102_e9), k_st7102_e9, 0},
    {0xC8, sizeof(k_st7102_c8), k_st7102_c8, 0},
    {0xC9, sizeof(k_st7102_c8), k_st7102_c8, 0},
    {0x35, sizeof(k_st7102_35), k_st7102_35, 0},
    {0x36, sizeof(k_st7102_36), k_st7102_36, 0},
    {0x11, 0, NULL, 250},
    {0x29, 0, NULL, 200},
};

static void river_st7102_gpio_out(uint32_t pin, bool high)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.GPIO_Pin = pin;
    gpio.GPIO_Mode = GPIO_Mode_OUT;
    gpio.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(&gpio);
    GPIO_WriteBit(pin, high ? 1 : 0);
}

static void river_st7102_power_reset(void)
{
    river_st7102_gpio_out(_PB_26, true);
    river_st7102_gpio_out(_PA_16, false);
    rtos_time_delay_ms(10);
    river_st7102_gpio_out(_PA_14, true);
    rtos_time_delay_ms(10);
    river_st7102_gpio_out(_PA_14, false);
    rtos_time_delay_ms(10);
    river_st7102_gpio_out(_PA_14, true);
    rtos_time_delay_ms(120);
}

static void river_st7102_send_dcs(uint8_t cmd, uint8_t payload_len, const uint8_t *payload)
{
    uint32_t word0;
    uint32_t word1;
    uint32_t addr;
    uint32_t idx;
    uint8_t buf[80] = {0};

    if (payload_len == 0U) {
        MIPI_DSI_CMD_Send(MIPI, RIVER_MIPI_DSI_DCS_SHORT_WRITE, cmd, 0);
        return;
    }
    if (payload_len == 1U) {
        MIPI_DSI_CMD_Send(MIPI, RIVER_MIPI_DSI_DCS_SHORT_WRITE_PARAM, cmd, payload[0]);
        return;
    }
    if ((size_t)payload_len > sizeof(buf) - 1U) {
        payload_len = (uint8_t)(sizeof(buf) - 1U);
    }
    buf[0] = cmd;
    memcpy(&buf[1], payload, payload_len);
    payload_len++;

    for (addr = 0U; addr < ((uint32_t)payload_len + 7U) / 8U; ++addr) {
        idx = addr * 8U;
        word0 = (uint32_t)buf[idx] |
                ((uint32_t)buf[idx + 1U] << 8) |
                ((uint32_t)buf[idx + 2U] << 16) |
                ((uint32_t)buf[idx + 3U] << 24);
        word1 = (uint32_t)buf[idx + 4U] |
                ((uint32_t)buf[idx + 5U] << 8) |
                ((uint32_t)buf[idx + 6U] << 16) |
                ((uint32_t)buf[idx + 7U] << 24);
        MIPI_DSI_CMD_LongPkt_MemQWordRW(MIPI, addr, &word0, &word1, FALSE);
    }
    MIPI_DSI_CMD_Send(MIPI, RIVER_MIPI_DSI_DCS_LONG_WRITE, payload_len, 0);
}

static void river_st7102_push_init_table(void)
{
    size_t i;

    MIPI_DSI_TO1_Set(MIPI, DISABLE, 0);
    MIPI_DSI_TO2_Set(MIPI, ENABLE, 0x7FFFFFFF);
    MIPI_DSI_TO3_Set(MIPI, DISABLE, 0);
    MIPI_DSI_init(MIPI, &g_st7102.mipi);
    for (i = 0U; i < sizeof(k_st7102_init_cmds) / sizeof(k_st7102_init_cmds[0]); ++i) {
        river_st7102_send_dcs(k_st7102_init_cmds[i].cmd,
                              k_st7102_init_cmds[i].len,
                              k_st7102_init_cmds[i].data);
        if (k_st7102_init_cmds[i].delay_ms != 0U) {
            rtos_time_delay_ms(k_st7102_init_cmds[i].delay_ms);
        } else {
            rtos_time_delay_ms(1);
        }
    }
}

static void river_st7102_mipi_config(void)
{
    const uint32_t hsa = 4U;
    const uint32_t hbp = 30U;
    const uint32_t hfp = 30U;
    const uint32_t vsa = 4U;
    const uint32_t vbp = 12U;
    const uint32_t vfp = 12U;
    uint32_t lane_num = CONFIG_RIVER_UI_ST7102_MIPI_LANE_NUM;
    uint32_t vtotal;
    uint32_t htotal_bits;
    uint32_t overhead_cycles;
    uint32_t overhead_bits;
    uint32_t total_bits;

    if (lane_num < 1U || lane_num > 2U) {
        lane_num = 1U;
    }
    MIPI_StructInit(&g_st7102.mipi);
    g_st7102.mipi.MIPI_VideoDataFormat = MIPI_VIDEO_DATA_FORMAT_RGB565;
    g_st7102.mipi.MIPI_VideoModeInterface = MIPI_VIDEO_NON_BURST_MODE_WITH_SYNC_EVENTS;
    g_st7102.mipi.MIPI_LaneNum = (uint8_t)lane_num;
    g_st7102.mipi.MIPI_FrameRate = CONFIG_RIVER_UI_ST7102_FRAME_RATE;
    g_st7102.mipi.MIPI_HSA = (uint16_t)((hsa * RIVER_ST7102_BPP) / 8U);
    g_st7102.mipi.MIPI_HBP = (uint16_t)(((hsa + hbp) * RIVER_ST7102_BPP) / 8U);
    g_st7102.mipi.MIPI_HACT = RIVER_ST7102_WIDTH;
    g_st7102.mipi.MIPI_HFP = (uint16_t)((hfp * RIVER_ST7102_BPP) / 8U);
    g_st7102.mipi.MIPI_VSA = vsa;
    g_st7102.mipi.MIPI_VBP = vbp;
    g_st7102.mipi.MIPI_VACT = RIVER_ST7102_HEIGHT;
    g_st7102.mipi.MIPI_VFP = vfp;

    vtotal = vsa + vbp + RIVER_ST7102_HEIGHT + vfp;
    htotal_bits = (hsa + hbp + RIVER_ST7102_WIDTH + hfp) * RIVER_ST7102_BPP;
    overhead_cycles = RIVER_ST7102_T_LPX + RIVER_ST7102_T_HS_PREP +
                      RIVER_ST7102_T_HS_ZERO + RIVER_ST7102_T_HS_TRAIL +
                      RIVER_ST7102_T_HS_EXIT;
    overhead_bits = overhead_cycles * lane_num * 8U;
    total_bits = htotal_bits + overhead_bits;
    g_st7102.mipi.MIPI_VideDataLaneFreq =
        (uint16_t)((CONFIG_RIVER_UI_ST7102_FRAME_RATE * total_bits * vtotal) /
                   lane_num / RIVER_ST7102_MIPI_MHZ + 20U);
    g_st7102.mipi.MIPI_LineTime =
        (uint16_t)((g_st7102.mipi.MIPI_VideDataLaneFreq * RIVER_ST7102_MIPI_MHZ) /
                   8U / CONFIG_RIVER_UI_ST7102_FRAME_RATE / vtotal);
    g_st7102.mipi.MIPI_BllpLen = g_st7102.mipi.MIPI_LineTime / 2U;
    g_st7102.lane_num = lane_num;
    g_st7102.lane_mbps = g_st7102.mipi.MIPI_VideDataLaneFreq;
    g_st7102.line_time = g_st7102.mipi.MIPI_LineTime;
}

static void river_st7102_lcdc_config(void)
{
    LCDC_StructInit(&g_st7102.lcdc);
    g_st7102.lcdc.LCDC_ImageWidth = RIVER_ST7102_WIDTH;
    g_st7102.lcdc.LCDC_ImageHeight = RIVER_ST7102_HEIGHT;
    g_st7102.lcdc.LCDC_BgColorRed = 0;
    g_st7102.lcdc.LCDC_BgColorGreen = 0;
    g_st7102.lcdc.LCDC_BgColorBlue = 0;
    g_st7102.lcdc.layerx[0].LCDC_LayerEn = ENABLE;
    g_st7102.lcdc.layerx[0].LCDC_LayerImgFormat = LCDC_LAYER_IMG_FORMAT_RGB565;
    g_st7102.lcdc.layerx[0].LCDC_LayerHorizontalStart = 1;
    g_st7102.lcdc.layerx[0].LCDC_LayerHorizontalStop = RIVER_ST7102_WIDTH;
    g_st7102.lcdc.layerx[0].LCDC_LayerVerticalStart = 1;
    g_st7102.lcdc.layerx[0].LCDC_LayerVerticalStop = RIVER_ST7102_HEIGHT;
    LCDC_Init(LCDC, &g_st7102.lcdc);
    LCDC_DMAModeConfig(LCDC, LCDC_LAYER_BURSTSIZE_4X64BYTES);
    LCDC_DMADebugConfig(LCDC, LCDC_DMA_OUT_DISABLE, NULL);
    LCDC_Cmd(LCDC, ENABLE);
    while (!LCDC_CheckLCDCReady(LCDC)) {
    }
    MIPI_DSI_Mode_Switch(MIPI, ENABLE);
}

river_status_t river_st7102_mipi_init(void)
{
    if (g_st7102.initialized) {
        return RIVER_OK;
    }
    memset(&g_st7102, 0, sizeof(g_st7102));
    RIVER_LOGI("st7102 init: %lux%lu lane=%d fps=%d",
               (unsigned long)RIVER_ST7102_WIDTH,
               (unsigned long)RIVER_ST7102_HEIGHT,
               CONFIG_RIVER_UI_ST7102_MIPI_LANE_NUM,
               CONFIG_RIVER_UI_ST7102_FRAME_RATE);
    river_st7102_power_reset();
    RCC_PeriphClockCmd(APBPeriph_NULL, APBPeriph_HPERI_CLOCK, ENABLE);
    RCC_PeriphClockCmd(APBPeriph_LCDC, APBPeriph_LCDCMIPI_CLOCK, ENABLE);
    river_st7102_mipi_config();
    MIPI_Init(MIPI, &g_st7102.mipi);
    river_st7102_push_init_table();
    river_st7102_lcdc_config();
    river_st7102_gpio_out(_PA_16, true);
    g_st7102.initialized = true;
    return RIVER_OK;
}

bool river_st7102_mipi_panel_ready(void)
{
    return g_st7102.initialized;
}

void river_st7102_mipi_flip(void *buffer)
{
    if (!g_st7102.initialized || buffer == NULL) {
        return;
    }
    DCache_CleanInvalidate(0xFFFFFFFF, 0xFFFFFFFF);
    g_st7102.lcdc.layerx[0].LCDC_LayerImgBaseAddr = (uint32_t)buffer;
    LCDC_LayerConfig(LCDC, LCDC_LAYER_LAYER1, &g_st7102.lcdc.layerx[LCDC_LAYER_LAYER1]);
    LCDC_TrigerSHWReload(LCDC);
    if (!g_st7102.lcdc_enabled) {
        LCDC_Cmd(LCDC, ENABLE);
        g_st7102.lcdc_enabled = true;
    }
    g_st7102.flips++;
}

void river_st7102_mipi_dump_status(void)
{
    RIVER_LOGI("st7102: init=%s lcdc=%s lane=%lu mbps=%lu line=%lu flips=%lu error=%s",
               g_st7102.initialized ? "yes" : "no",
               g_st7102.lcdc_enabled ? "on" : "off",
               (unsigned long)g_st7102.lane_num,
               (unsigned long)g_st7102.lane_mbps,
               (unsigned long)g_st7102.line_time,
               (unsigned long)g_st7102.flips,
               g_st7102.last_error[0] != '\0' ? g_st7102.last_error : "-");
}
