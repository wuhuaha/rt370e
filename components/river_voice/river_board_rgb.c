#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ameba_soc.h"

#include "river/river_board_rgb.h"

#define RIVER_BOARD_RGB_PIN                 _PA_9
#define RIVER_BOARD_RGB_LED_COUNT           1U
#define RIVER_BOARD_RGB_T1H_NS              800U
#define RIVER_BOARD_RGB_T1L_NS              300U
#define RIVER_BOARD_RGB_T0H_NS              300U
#define RIVER_BOARD_RGB_T0L_NS              800U
#define RIVER_BOARD_RGB_RESET_NS            300000U
#define RIVER_BOARD_RGB_FRAME_INTERVAL_NS   1000000U
#define RIVER_BOARD_RGB_WAIT_POLL_US        20U
#define RIVER_BOARD_RGB_WAIT_POLL_COUNT     500U

#define RIVER_BOARD_RGB_NS2VAL(time_ns)     ((time_ns) / 25U)

typedef struct {
    bool init_attempted;
    bool ready;
    river_board_rgb_state_t current_state;
} river_board_rgb_context_t;

static river_board_rgb_context_t g_river_board_rgb;

static uint32_t river_board_rgb_pack_grb(uint8_t red, uint8_t green, uint8_t blue)
{
    return ((uint32_t)green << 16) | ((uint32_t)red << 8) | (uint32_t)blue;
}

static const char *river_board_rgb_pin_name(void)
{
    return "PA_9";
}

static void river_board_rgb_state_to_color(river_board_rgb_state_t state,
                                           uint8_t *red,
                                           uint8_t *green,
                                           uint8_t *blue)
{
    if (red == 0 || green == 0 || blue == 0) {
        return;
    }

    switch (state) {
    case RIVER_BOARD_RGB_STATE_BOOT:
        *red = 0x18U;
        *green = 0x06U;
        *blue = 0x00U;
        break;
    case RIVER_BOARD_RGB_STATE_VAD_SILENCE:
        *red = 0x00U;
        *green = 0x00U;
        *blue = 0x14U;
        break;
    case RIVER_BOARD_RGB_STATE_VAD_SPEECH:
        *red = 0x00U;
        *green = 0x18U;
        *blue = 0x00U;
        break;
    case RIVER_BOARD_RGB_STATE_ERROR:
        *red = 0x18U;
        *green = 0x00U;
        *blue = 0x00U;
        break;
    case RIVER_BOARD_RGB_STATE_OFF:
    default:
        *red = 0x00U;
        *green = 0x00U;
        *blue = 0x00U;
        break;
    }
}

static river_status_t river_board_rgb_write_color(uint8_t red, uint8_t green, uint8_t blue)
{
    uint32_t led_data;
    uint32_t poll_count;

    led_data = river_board_rgb_pack_grb(red, green, blue);

    LEDC_SetLEDNum(LEDC_DEV, RIVER_BOARD_RGB_LED_COUNT);
    LEDC_SetTotalLength(LEDC_DEV, RIVER_BOARD_RGB_LED_COUNT);
    LEDC_Cmd(LEDC_DEV, ENABLE);

    if (LEDC_SendData(LEDC_DEV, &led_data, RIVER_BOARD_RGB_LED_COUNT) != RIVER_BOARD_RGB_LED_COUNT) {
        LEDC_SoftReset(LEDC_DEV);
        return RIVER_ERR_IO;
    }

    for (poll_count = 0U; poll_count < RIVER_BOARD_RGB_WAIT_POLL_COUNT; ++poll_count) {
        uint32_t interrupt_status;

        interrupt_status = LEDC_GetINT(LEDC_DEV);
        if ((interrupt_status & LEDC_BIT_LED_TRANS_FINISH_INT) != 0U) {
            LEDC_SoftReset(LEDC_DEV);
            return RIVER_OK;
        }
        if ((interrupt_status & (LEDC_BIT_WAITDATA_TIMEOUT_INT | LEDC_BIT_FIFO_OVERFLOW_INT)) != 0U) {
            LEDC_SoftReset(LEDC_DEV);
            return RIVER_ERR_IO;
        }
        DelayUs(RIVER_BOARD_RGB_WAIT_POLL_US);
    }

    LEDC_SoftReset(LEDC_DEV);
    return RIVER_ERR_BUSY;
}

river_status_t river_board_rgb_init(void)
{
#ifndef CONFIG_RIVER_BOARD_RGB_VAD_INDICATOR_EN
    return RIVER_ERR_UNSUPPORTED;
#else
    LEDC_InitTypeDef ledc_init;

    if (g_river_board_rgb.ready) {
        return RIVER_OK;
    }
    if (g_river_board_rgb.init_attempted) {
        return RIVER_ERR_IO;
    }

    g_river_board_rgb.init_attempted = true;

    RCC_PeriphClockCmd(APBPeriph_LEDC, APBPeriph_LEDC_CLOCK, ENABLE);
    Pinmux_Config(RIVER_BOARD_RGB_PIN, PINMUX_FUNCTION_LEDC);

    LEDC_StructInit(&ledc_init);
    ledc_init.t0h_ns = RIVER_BOARD_RGB_NS2VAL(RIVER_BOARD_RGB_T0H_NS);
    ledc_init.t0l_ns = RIVER_BOARD_RGB_NS2VAL(RIVER_BOARD_RGB_T0L_NS);
    ledc_init.t1h_ns = RIVER_BOARD_RGB_NS2VAL(RIVER_BOARD_RGB_T1H_NS);
    ledc_init.t1l_ns = RIVER_BOARD_RGB_NS2VAL(RIVER_BOARD_RGB_T1L_NS);
    ledc_init.reset_ns = RIVER_BOARD_RGB_NS2VAL(RIVER_BOARD_RGB_RESET_NS);
    ledc_init.wait_data_time_ns = RIVER_BOARD_RGB_NS2VAL(RIVER_BOARD_RGB_RESET_NS);
    ledc_init.wait_time0_en = ENABLE;
    ledc_init.wait_time1_en = ENABLE;
    ledc_init.wait_time1_ns = RIVER_BOARD_RGB_NS2VAL(RIVER_BOARD_RGB_FRAME_INTERVAL_NS);
    ledc_init.ledc_trans_mode = LEDC_CPU_MODE;
    ledc_init.led_count = RIVER_BOARD_RGB_LED_COUNT;
    ledc_init.data_length = RIVER_BOARD_RGB_LED_COUNT;
    LEDC_Init(LEDC_DEV, &ledc_init);
    LEDC_ClearINT(LEDC_DEV, LEDC_INT_ALL);

    g_river_board_rgb.ready = true;
    g_river_board_rgb.current_state = RIVER_BOARD_RGB_STATE_OFF;

    printf("[river][board] rgb indicator ready: ws2812 ledc cpu pin=%s silence=blue speech=green error=red\n",
           river_board_rgb_pin_name());
    return RIVER_OK;
#endif
}

void river_board_rgb_set_state(river_board_rgb_state_t state)
{
#ifdef CONFIG_RIVER_BOARD_RGB_VAD_INDICATOR_EN
    river_status_t status;
    uint8_t red;
    uint8_t green;
    uint8_t blue;

    if (g_river_board_rgb.ready && g_river_board_rgb.current_state == state) {
        return;
    }

    status = river_board_rgb_init();
    if (status != RIVER_OK) {
        return;
    }

    river_board_rgb_state_to_color(state, &red, &green, &blue);
    if (river_board_rgb_write_color(red, green, blue) == RIVER_OK) {
        g_river_board_rgb.current_state = state;
    }
#else
    (void)state;
#endif
}

const char *river_board_rgb_state_name(void)
{
    switch (g_river_board_rgb.current_state) {
    case RIVER_BOARD_RGB_STATE_BOOT:
        return "boot";
    case RIVER_BOARD_RGB_STATE_VAD_SILENCE:
        return "vad_silence";
    case RIVER_BOARD_RGB_STATE_VAD_SPEECH:
        return "vad_speech";
    case RIVER_BOARD_RGB_STATE_ERROR:
        return "error";
    case RIVER_BOARD_RGB_STATE_OFF:
    default:
        return "off";
    }
}
