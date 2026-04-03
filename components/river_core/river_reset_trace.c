/* 使用 SDK backup register 保存最近交互状态，跨 system reset/watchdog reset 辅助回溯。 */
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "ameba_soc.h"
#include "os_wrapper.h"

#include "river/river_log.h"
#include "river/river_reset_trace.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.reset"

#define RIVER_RESET_TRACE_MAGIC           0xA5U
#define RIVER_RESET_TRACE_REASON_BYTES    8U
#define RIVER_RESET_TRACE_REASON_UNKNOWN  "-"

static uint32_t river_reset_trace_pack_meta(river_interaction_state_t state,
                                            uint16_t uptime_ds)
{
    return ((uint32_t)RIVER_RESET_TRACE_MAGIC << 24) |
           (((uint32_t)state & 0xFFU) << 16) |
           (uint32_t)uptime_ds;
}

static bool river_reset_trace_meta_valid(uint32_t meta)
{
    return ((meta >> 24) & 0xFFU) == RIVER_RESET_TRACE_MAGIC;
}

static river_interaction_state_t river_reset_trace_meta_state(uint32_t meta)
{
    return (river_interaction_state_t)((meta >> 16) & 0xFFU);
}

static uint16_t river_reset_trace_meta_uptime_ds(uint32_t meta)
{
    return (uint16_t)(meta & 0xFFFFU);
}

static void river_reset_trace_encode_reason(const char *reason,
                                            uint32_t *word0,
                                            uint32_t *word1)
{
    char buffer[RIVER_RESET_TRACE_REASON_BYTES] = {0};
    uint32_t index;

    if (word0 == NULL || word1 == NULL) {
        return;
    }

    if (reason != NULL && reason[0] != '\0') {
        strncpy(buffer, reason, sizeof(buffer));
    }

    *word0 = 0U;
    *word1 = 0U;
    for (index = 0U; index < 4U; ++index) {
        *word0 |= ((uint32_t)(uint8_t)buffer[index]) << (index * 8U);
        *word1 |= ((uint32_t)(uint8_t)buffer[index + 4U]) << (index * 8U);
    }
}

static void river_reset_trace_decode_reason(uint32_t word0,
                                            uint32_t word1,
                                            char *buffer,
                                            size_t buffer_size)
{
    uint32_t index;

    if (buffer == NULL || buffer_size == 0U) {
        return;
    }

    memset(buffer, 0, buffer_size);
    for (index = 0U; index < 4U && index < buffer_size - 1U; ++index) {
        buffer[index] = (char)((word0 >> (index * 8U)) & 0xFFU);
    }
    for (index = 0U; index < 4U && (index + 4U) < buffer_size - 1U; ++index) {
        buffer[index + 4U] = (char)((word1 >> (index * 8U)) & 0xFFU);
    }
}

static uint16_t river_reset_trace_current_uptime_ds(void)
{
    uint64_t uptime_ms;

    uptime_ms = (uint64_t)rtos_time_get_current_system_time_ms();
    return (uint16_t)((uptime_ms / 100U) & 0xFFFFU);
}

void river_reset_trace_mark(river_interaction_state_t state, const char *reason)
{
    uint32_t reason_word0;
    uint32_t reason_word1;

    river_reset_trace_encode_reason(reason, &reason_word0, &reason_word1);
    BKUP_Write(BKUP_REG1, river_reset_trace_pack_meta(state, river_reset_trace_current_uptime_ds()));
    BKUP_Write(BKUP_REG2, reason_word0);
    BKUP_Write(BKUP_REG3, reason_word1);
}

void river_reset_trace_boot_init(void)
{
    uint32_t meta;
    uint32_t reason_word0;
    uint32_t reason_word1;
    char reason[sizeof("followup")];
    uint32_t boot_reason;

    meta = BKUP_Read(BKUP_REG1);
    reason_word0 = BKUP_Read(BKUP_REG2);
    reason_word1 = BKUP_Read(BKUP_REG3);
    boot_reason = (uint32_t)BOOT_Reason();

    if (river_reset_trace_meta_valid(meta)) {
        river_reset_trace_decode_reason(reason_word0, reason_word1, reason, sizeof(reason));
        RIVER_LOGI("reset trace previous: boot_reason=0x%04lx state=%s reason8=%s uptime_ds=%lu",
                   (unsigned long)boot_reason,
                   river_interaction_state_name(river_reset_trace_meta_state(meta)),
                   reason[0] != '\0' ? reason : RIVER_RESET_TRACE_REASON_UNKNOWN,
                   (unsigned long)river_reset_trace_meta_uptime_ds(meta));
    } else {
        RIVER_LOGI("reset trace previous: boot_reason=0x%04lx state=none",
                   (unsigned long)boot_reason);
    }

    river_reset_trace_mark(RIVER_INTERACTION_BOOTING, "boot");
}

void river_reset_trace_dump_status(void)
{
    uint32_t meta;
    uint32_t reason_word0;
    uint32_t reason_word1;
    char reason[sizeof("followup")];

    meta = BKUP_Read(BKUP_REG1);
    reason_word0 = BKUP_Read(BKUP_REG2);
    reason_word1 = BKUP_Read(BKUP_REG3);

    if (!river_reset_trace_meta_valid(meta)) {
        RIVER_LOGI("reset_trace=invalid");
        return;
    }

    river_reset_trace_decode_reason(reason_word0, reason_word1, reason, sizeof(reason));
    RIVER_LOGI("reset_trace=armed state=%s reason8=%s uptime_ds=%lu",
               river_interaction_state_name(river_reset_trace_meta_state(meta)),
               reason[0] != '\0' ? reason : RIVER_RESET_TRACE_REASON_UNKNOWN,
               (unsigned long)river_reset_trace_meta_uptime_ds(meta));
}
