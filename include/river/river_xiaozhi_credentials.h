#ifndef AMEBA_RIVER_XIAOZHI_CREDENTIALS_H
#define AMEBA_RIVER_XIAOZHI_CREDENTIALS_H

/*
 * XiaoZhi realtime websocket configuration.
 *
 * Keep URL/TOKEN empty by default so the current flashable Iflytek path remains
 * the active baseline until XiaoZhi server details are intentionally provided.
 */
#define RIVER_XIAOZHI_OTA_URL                   "https://api.tenclass.net/xiaozhi/ota/"
#define RIVER_XIAOZHI_URL                       ""
#define RIVER_XIAOZHI_TOKEN                     ""
#define RIVER_XIAOZHI_PROTOCOL_VERSION          3U
#define RIVER_XIAOZHI_ENABLE_MCP                1

#define RIVER_XIAOZHI_UPLINK_FORMAT             "opus"
#define RIVER_XIAOZHI_UPLINK_SAMPLE_RATE        16000U
#define RIVER_XIAOZHI_UPLINK_CHANNELS           1U
/*
 * Official docs and ESP reference commonly use 60 ms, but py-xiaozhi falls
 * back to 20 ms on non-ESP clients. Ameba currently uses the SDK libopus
 * directly, and 20 ms is materially safer here while remaining protocol-
 * compatible with the XiaoZhi server.
 */
#define RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS  20U

#define RIVER_XIAOZHI_WS_TX_MAX                 8192
#define RIVER_XIAOZHI_WS_RX_MAX                 12288
#define RIVER_XIAOZHI_WS_QUEUE_MAX              16
#define RIVER_XIAOZHI_OPEN_READY_WAIT_MS        10000U
#define RIVER_XIAOZHI_OTA_HTTP_TIMEOUT_SEC      10U

#endif
