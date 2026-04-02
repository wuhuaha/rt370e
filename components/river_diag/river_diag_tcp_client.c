/* 板端 TCP 调试链路：Wi-Fi 就绪后主动连到 host，镜像日志并接收命令。 */
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "os_wrapper.h"
#include "os_wrapper_mutex.h"
#include "platform_stdlib.h"

#include "river/river_diag.h"
#include "river/river_log.h"
#include "river/river_wifi_station.h"

#undef RIVER_LOG_TAG
#define RIVER_LOG_TAG "river.diag.tcp"

#ifdef CONFIG_RIVER_DIAG_TCP_CLIENT_EN
#define RIVER_DIAG_TCP_TASK_STACK        (1024U * 4U)
#define RIVER_DIAG_TCP_TASK_PRIORITY     2U
#define RIVER_DIAG_TCP_RX_BUFFER_SIZE    192U
#define RIVER_DIAG_TCP_LINE_BUFFER_SIZE  256U
#define RIVER_DIAG_TCP_LOG_BUFFER_SIZE   1152U

#ifdef MSG_DONTWAIT
#define RIVER_DIAG_TCP_RECV_FLAGS MSG_DONTWAIT
#else
#define RIVER_DIAG_TCP_RECV_FLAGS 0
#endif

typedef struct {
    bool initialized;
    bool connected;
    bool sink_attached;
    bool reconnect_requested;
    rtos_task_t task;
    rtos_mutex_t tx_mutex;
    bool tx_mutex_ready;
    int socket_fd;
    uint32_t connect_attempts;
    uint32_t connect_successes;
    uint32_t connect_failures;
    uint32_t disconnects;
    uint32_t rx_commands;
    uint32_t tx_logs;
    uint32_t tx_drops;
    int last_error;
    char rx_line[RIVER_DIAG_TCP_LINE_BUFFER_SIZE];
    uint16_t rx_line_length;
} river_diag_tcp_client_t;

static river_diag_tcp_client_t g_river_diag_tcp = {
    .socket_fd = -1
};

static char river_diag_tcp_level_letter(river_log_level_t level)
{
    switch (level) {
    case RIVER_LOG_LEVEL_ERROR:
        return 'E';
    case RIVER_LOG_LEVEL_WARN:
        return 'W';
    case RIVER_LOG_LEVEL_INFO:
        return 'I';
    case RIVER_LOG_LEVEL_DEBUG:
        return 'D';
    default:
        return '?';
    }
}

static void river_diag_tcp_client_write_sink(void *user_data,
                                             river_log_level_t level,
                                             uint32_t timestamp_ms,
                                             const char *tag,
                                             const char *message)
{
    river_diag_tcp_client_t *client;
    char line[RIVER_DIAG_TCP_LOG_BUFFER_SIZE];
    int length;
    int sent;
    int send_flags;

    client = (river_diag_tcp_client_t *)user_data;
    if ((client == NULL) || !client->connected || (client->socket_fd < 0)) {
        return;
    }

    length = snprintf(line,
                      sizeof(line),
                      "[%010lu][%c][%s] %s\n",
                      (unsigned long)timestamp_ms,
                      river_diag_tcp_level_letter(level),
                      (tag != NULL && tag[0] != '\0') ? tag : "river",
                      (message != NULL) ? message : "");
    if (length <= 0) {
        return;
    }
    if ((size_t)length >= sizeof(line)) {
        length = (int)(sizeof(line) - 1U);
    }

    send_flags = 0;
#ifdef MSG_DONTWAIT
    send_flags |= MSG_DONTWAIT;
#endif

    if (client->tx_mutex_ready) {
        (void)rtos_mutex_take(client->tx_mutex, MUTEX_WAIT_TIMEOUT);
    }
    sent = send(client->socket_fd, line, (size_t)length, send_flags);
    if (client->tx_mutex_ready) {
        (void)rtos_mutex_give(client->tx_mutex);
    }

    if (sent == length) {
        client->tx_logs++;
        return;
    }

    client->tx_drops++;
    if ((sent < 0) && (errno != EAGAIN) && (errno != EWOULDBLOCK)) {
        client->last_error = errno;
        client->reconnect_requested = true;
    }
}

static void river_diag_tcp_client_detach_sink(void)
{
    if (!g_river_diag_tcp.sink_attached) {
        return;
    }

    (void)river_log_set_secondary_sink(NULL);
    g_river_diag_tcp.sink_attached = false;
}

static void river_diag_tcp_client_close(const char *reason, bool log_reason)
{
    bool was_connected;

    was_connected = g_river_diag_tcp.connected;
    river_diag_tcp_client_detach_sink();

    if (g_river_diag_tcp.socket_fd >= 0) {
        close(g_river_diag_tcp.socket_fd);
        g_river_diag_tcp.socket_fd = -1;
    }

    g_river_diag_tcp.connected = false;
    g_river_diag_tcp.reconnect_requested = false;
    g_river_diag_tcp.rx_line_length = 0U;

    if (was_connected) {
        g_river_diag_tcp.disconnects++;
        if (log_reason) {
            RIVER_LOGW("tcpdiag disconnected: reason=%s last_err=%d",
                       (reason != NULL && reason[0] != '\0') ? reason : "-",
                       g_river_diag_tcp.last_error);
        }
    }
}

static void river_diag_tcp_client_attach_sink(void)
{
    river_log_sink_t sink;

    sink.write = river_diag_tcp_client_write_sink;
    sink.user_data = &g_river_diag_tcp;
    if (river_log_set_secondary_sink(&sink) == RIVER_OK) {
        g_river_diag_tcp.sink_attached = true;
    } else {
        g_river_diag_tcp.sink_attached = false;
        g_river_diag_tcp.last_error = -1;
    }
}

static void river_diag_tcp_client_process_line(const char *line)
{
    if ((line == NULL) || (line[0] == '\0')) {
        return;
    }

    g_river_diag_tcp.rx_commands++;
    RIVER_LOGI("tcpdiag rx: %s", line);
    (void)river_diag_execute_command_line(line);
}

static void river_diag_tcp_client_handle_rx_bytes(const char *bytes, int length)
{
    int index;

    for (index = 0; index < length; ++index) {
        char ch;

        ch = bytes[index];
        if ((ch == '\r') || (ch == '\n')) {
            if (g_river_diag_tcp.rx_line_length > 0U) {
                g_river_diag_tcp.rx_line[g_river_diag_tcp.rx_line_length] = '\0';
                river_diag_tcp_client_process_line(g_river_diag_tcp.rx_line);
                g_river_diag_tcp.rx_line_length = 0U;
            }
            continue;
        }

        if (g_river_diag_tcp.rx_line_length >= (sizeof(g_river_diag_tcp.rx_line) - 1U)) {
            g_river_diag_tcp.rx_line_length = 0U;
            g_river_diag_tcp.last_error = EMSGSIZE;
            continue;
        }

        g_river_diag_tcp.rx_line[g_river_diag_tcp.rx_line_length++] = ch;
    }
}

static bool river_diag_tcp_client_connect_once(void)
{
    struct sockaddr_in address;
    int socket_fd;

    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        g_river_diag_tcp.last_error = errno;
        return false;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(CONFIG_RIVER_DIAG_TCP_SERVER_PORT);
    address.sin_addr.s_addr = inet_addr(CONFIG_RIVER_DIAG_TCP_SERVER_HOST);
    if (address.sin_addr.s_addr == IPADDR_NONE) {
        close(socket_fd);
        g_river_diag_tcp.last_error = EINVAL;
        return false;
    }

    g_river_diag_tcp.connect_attempts++;
    if (connect(socket_fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        g_river_diag_tcp.last_error = errno;
        close(socket_fd);
        g_river_diag_tcp.connect_failures++;
        return false;
    }

    g_river_diag_tcp.socket_fd = socket_fd;
    g_river_diag_tcp.connected = true;
    g_river_diag_tcp.connect_successes++;
    g_river_diag_tcp.last_error = 0;
    g_river_diag_tcp.reconnect_requested = false;
    river_diag_tcp_client_attach_sink();
    RIVER_LOGI("tcpdiag connected: host=%s port=%u",
               CONFIG_RIVER_DIAG_TCP_SERVER_HOST,
               (unsigned int)CONFIG_RIVER_DIAG_TCP_SERVER_PORT);
    return true;
}

static void river_diag_tcp_client_task(void *param)
{
    char rx_buffer[RIVER_DIAG_TCP_RX_BUFFER_SIZE];

    (void)param;

    while (1) {
        int received;

        if (!river_wifi_station_is_connected()) {
            if (g_river_diag_tcp.connected) {
                river_diag_tcp_client_close("wifi_down", true);
            }
            rtos_time_delay_ms(CONFIG_RIVER_DIAG_TCP_RETRY_MS);
            continue;
        }

        if (!g_river_diag_tcp.connected) {
            if (!river_diag_tcp_client_connect_once()) {
                if ((g_river_diag_tcp.connect_failures == 1U) ||
                    ((g_river_diag_tcp.connect_failures % 10U) == 0U)) {
                    RIVER_LOGW("tcpdiag connect failed: host=%s port=%u err=%d attempt=%lu",
                               CONFIG_RIVER_DIAG_TCP_SERVER_HOST,
                               (unsigned int)CONFIG_RIVER_DIAG_TCP_SERVER_PORT,
                               g_river_diag_tcp.last_error,
                               (unsigned long)g_river_diag_tcp.connect_attempts);
                }
                rtos_time_delay_ms(CONFIG_RIVER_DIAG_TCP_RETRY_MS);
                continue;
            }
        }

        if (g_river_diag_tcp.reconnect_requested) {
            river_diag_tcp_client_close("tx_error", true);
            rtos_time_delay_ms(CONFIG_RIVER_DIAG_TCP_RETRY_MS);
            continue;
        }

        received = recv(g_river_diag_tcp.socket_fd,
                        rx_buffer,
                        sizeof(rx_buffer),
                        RIVER_DIAG_TCP_RECV_FLAGS);
        if (received > 0) {
            river_diag_tcp_client_handle_rx_bytes(rx_buffer, received);
            continue;
        }

        if (received == 0) {
            river_diag_tcp_client_close("peer_closed", true);
            rtos_time_delay_ms(CONFIG_RIVER_DIAG_TCP_RETRY_MS);
            continue;
        }

        if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
            g_river_diag_tcp.last_error = errno;
            river_diag_tcp_client_close("recv_error", true);
            rtos_time_delay_ms(CONFIG_RIVER_DIAG_TCP_RETRY_MS);
            continue;
        }

        rtos_time_delay_ms(20U);
    }
}
#endif

river_status_t river_diag_init(void)
{
#ifdef CONFIG_RIVER_DIAG_TCP_CLIENT_EN
    if (g_river_diag_tcp.initialized) {
        return RIVER_OK;
    }

    if (rtos_mutex_create(&g_river_diag_tcp.tx_mutex) != RTK_SUCCESS) {
        RIVER_LOGE("tcpdiag init failed: tx mutex create failed");
        return RIVER_ERR_NO_MEMORY;
    }
    g_river_diag_tcp.tx_mutex_ready = true;

    if (rtos_task_create(&g_river_diag_tcp.task,
                         "river_tcpdiag",
                         river_diag_tcp_client_task,
                         NULL,
                         RIVER_DIAG_TCP_TASK_STACK,
                         RIVER_DIAG_TCP_TASK_PRIORITY) != RTK_SUCCESS) {
        rtos_mutex_delete(g_river_diag_tcp.tx_mutex);
        g_river_diag_tcp.tx_mutex_ready = false;
        RIVER_LOGE("tcpdiag init failed: task create failed");
        return RIVER_ERR_NO_MEMORY;
    }

    g_river_diag_tcp.initialized = true;
    RIVER_LOGI("tcpdiag init: host=%s port=%u retry_ms=%u",
               CONFIG_RIVER_DIAG_TCP_SERVER_HOST,
               (unsigned int)CONFIG_RIVER_DIAG_TCP_SERVER_PORT,
               (unsigned int)CONFIG_RIVER_DIAG_TCP_RETRY_MS);
#else
    RIVER_LOGI("tcpdiag disabled");
#endif
    return RIVER_OK;
}

void river_diag_dump_status(void)
{
#ifdef CONFIG_RIVER_DIAG_TCP_CLIENT_EN
    RIVER_LOGI("tcpdiag=enabled host=%s port=%u state=%s attempts=%lu success=%lu fail=%lu disconnects=%lu rx_cmd=%lu tx_log=%lu tx_drop=%lu last_err=%d",
               CONFIG_RIVER_DIAG_TCP_SERVER_HOST,
               (unsigned int)CONFIG_RIVER_DIAG_TCP_SERVER_PORT,
               g_river_diag_tcp.connected ? "connected" : "disconnected",
               (unsigned long)g_river_diag_tcp.connect_attempts,
               (unsigned long)g_river_diag_tcp.connect_successes,
               (unsigned long)g_river_diag_tcp.connect_failures,
               (unsigned long)g_river_diag_tcp.disconnects,
               (unsigned long)g_river_diag_tcp.rx_commands,
               (unsigned long)g_river_diag_tcp.tx_logs,
               (unsigned long)g_river_diag_tcp.tx_drops,
               g_river_diag_tcp.last_error);
#else
    RIVER_LOGI("tcpdiag=disabled");
#endif
}
