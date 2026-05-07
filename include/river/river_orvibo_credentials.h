/* Orvibo voice client default realtime server credentials. */
#ifndef AMEBA_RIVER_ORVIBO_CREDENTIALS_H
#define AMEBA_RIVER_ORVIBO_CREDENTIALS_H

#define RIVER_ORVIBO_WS_URL              "ws://101.33.235.154:8081/xiaozhi/v1/"
#define RIVER_ORVIBO_WS_TOKEN            ""
#define RIVER_ORVIBO_PROTOCOL_VERSION    3U
#define RIVER_ORVIBO_ENABLE_MCP          1

#define RIVER_ORVIBO_AUDIO_SAMPLE_RATE        16000U
#define RIVER_ORVIBO_AUDIO_CHANNELS           1U
#define RIVER_ORVIBO_AUDIO_FRAME_DURATION_MS  60U
#define RIVER_ORVIBO_AUDIO_FORMAT             "opus"

#define RIVER_ORVIBO_WS_TX_MAX          2048
#define RIVER_ORVIBO_WS_RX_MAX          8192
#define RIVER_ORVIBO_WS_QUEUE_MAX       32
#define RIVER_ORVIBO_WS_STABLE_BUF_NUM  24

#endif
