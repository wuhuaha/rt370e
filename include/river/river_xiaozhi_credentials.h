/* 小智服务的默认 OTA/WS 连接参数。 */
#ifndef AMEBA_RIVER_XIAOZHI_CREDENTIALS_H
#define AMEBA_RIVER_XIAOZHI_CREDENTIALS_H

/*
 * Native agent-server realtime websocket configuration.
 *
 * This debug branch now targets the self-hosted agent-server native RTOS
 * contract directly. Keep the legacy ota/token fields empty so the old
 * XiaoZhi bootstrap path is effectively disabled while the higher layers are
 * still being renamed.
 */
#define RIVER_XIAOZHI_OTA_URL                   ""
#define RIVER_XIAOZHI_URL                       "wss://101.33.235.154/v1/realtime/ws"
#define RIVER_XIAOZHI_TOKEN                     ""
#define RIVER_XIAOZHI_REALTIME_SUBPROTOCOL      "agent-server.realtime.v0"
#define RIVER_XIAOZHI_REALTIME_PROTOCOL_VERSION "rtos-ws-v0"
#define RIVER_XIAOZHI_REALTIME_CLIENT_TYPE      "rtos"
#define RIVER_XIAOZHI_PROTOCOL_VERSION          3U
#define RIVER_XIAOZHI_ENABLE_MCP                1

#define RIVER_XIAOZHI_UPLINK_FORMAT             "pcm16le"
#define RIVER_XIAOZHI_UPLINK_SAMPLE_RATE        16000U
#define RIVER_XIAOZHI_UPLINK_CHANNELS           1U
/*
 * The native RTOS websocket profile recommends 20 ms pcm16le chunks for the
 * first bring-up pass. River's local capture path still runs at 16 ms, so the
 * cloud adapter will continue bundling frames into 20 ms uplink packets.
 */
#define RIVER_XIAOZHI_UPLINK_FRAME_DURATION_MS  20U

/*
 * XiaoZhi uplink audio is already bounded by the project-side Opus packet and
 * websocket binary frame contract, so keeping an 8 KB wsclient tx buffer only
 * wastes heap and makes queue growth fail late under pressure.
 *
 * 1024 B covers:
 * - current Opus/binary uplink packets (well below 512 B payload + framing)
 * - hello/listen/abort control JSON
 * - current MCP response envelopes
 */
#define RIVER_XIAOZHI_WS_TX_MAX                 1024
#define RIVER_XIAOZHI_WS_RX_MAX                 12288
/*
 * Keep enough headroom that brief WLAN / TLS send stalls do not immediately
 * push realtime audio into queue-full churn. The queue is still intentionally
 * bounded so the project-side stale-frame trimming remains the freshness guard
 * under sustained congestion.
 */
#define RIVER_XIAOZHI_WS_QUEUE_MAX              16
/*
 * Keep most send buffers warm in the SDK recycle queue so short bursty speech
 * does not repeatedly pay malloc/free churn while still leaving some slack for
 * the SDK to release excess buffers once traffic calms down.
 */
#define RIVER_XIAOZHI_WS_STABLE_BUF_NUM         ((RIVER_XIAOZHI_WS_QUEUE_MAX * 3U) / 4U)
#define RIVER_XIAOZHI_BOOTSTRAP_CACHE_TTL_MS    (10U * 60U * 1000U)
#define RIVER_XIAOZHI_OPEN_READY_WAIT_MS        10000U
#define RIVER_XIAOZHI_OTA_HTTP_TIMEOUT_SEC      10U

#endif
