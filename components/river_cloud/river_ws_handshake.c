/* Orvibo WebSocket handshake shim for XiaoZhi-compatible clients. */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "websocket/libwsclient.h"

#define RIVER_WS_KEY_BYTES 16U
#define RIVER_WS_KEY_B64_BYTES 24U
#define RIVER_WS_KEY_B64_BUF_BYTES (RIVER_WS_KEY_B64_BYTES + 1U)

static const unsigned char k_river_ws_b64_encode[64] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
    'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
    'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
    'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
    'w', 'x', 'y', 'z', '0', '1', '2', '3',
    '4', '5', '6', '7', '8', '9', '+', '/'
};

static int river_ws_b64_encode_string(const unsigned char *in,
                                      int in_len,
                                      unsigned char *out)
{
    int index;
    int aligned_len;
    int c1;
    int c2;
    int c3;

    if (in == NULL || out == NULL || in_len <= 0) {
        return -1;
    }

    aligned_len = (in_len / 3) * 3;
    for (index = 0; index < aligned_len; index += 3) {
        c1 = *in++;
        c2 = *in++;
        c3 = *in++;
        *out++ = k_river_ws_b64_encode[(c1 >> 2) & 0x3F];
        *out++ = k_river_ws_b64_encode[(((c1 & 3) << 4) + (c2 >> 4)) & 0x3F];
        *out++ = k_river_ws_b64_encode[(((c2 & 15) << 2) + (c3 >> 6)) & 0x3F];
        *out++ = k_river_ws_b64_encode[c3 & 0x3F];
    }

    if (index < in_len) {
        c1 = *in++;
        c2 = ((index + 1) < in_len) ? *in++ : 0;
        *out++ = k_river_ws_b64_encode[(c1 >> 2) & 0x3F];
        *out++ = k_river_ws_b64_encode[(((c1 & 3) << 4) + (c2 >> 4)) & 0x3F];
        if ((index + 1) < in_len) {
            *out++ = k_river_ws_b64_encode[((c2 & 15) << 2) & 0x3F];
        } else {
            *out++ = '=';
        }
        *out++ = '=';
    }

    *out = '\0';
    return 0;
}

static size_t river_ws_effective_field_length(const char *field, int field_len)
{
    size_t length;

    if (field == NULL || field_len <= 0) {
        return 0U;
    }
    length = (size_t)field_len;
    if (length > 0U && field[length - 1U] == '\0') {
        length--;
    }
    return length;
}

static bool river_ws_default_port(const wsclient_context *wsclient)
{
    return wsclient != NULL &&
           ((wsclient->use_ssl == 1U && wsclient->port == 443) ||
            (wsclient->use_ssl == 0U && wsclient->port == 80));
}

static char *river_ws_build_handshake_header(wsclient_context *wsclient)
{
    unsigned char raw_key[RIVER_WS_KEY_BYTES];
    unsigned char key_b64[RIVER_WS_KEY_B64_BUF_BYTES];
    const char *path;
    const char *port_suffix;
    char port_buffer[12];
    const char *origin_prefix;
    const char *origin_value;
    const char *protocol_prefix;
    const char *protocol_value;
    size_t protocol_len;
    const char *version_value;
    size_t version_len;
    const char *custom_token;
    size_t custom_token_len;
    const char *header_fields;
    size_t header_fields_len;
    size_t header_len;
    char *header;
    int written;

    if (wsclient == NULL || wsclient->host == NULL) {
        return NULL;
    }

    memset(raw_key, 0, sizeof(raw_key));
    memset(key_b64, 0, sizeof(key_b64));
    ws_get_random_bytes(raw_key, sizeof(raw_key));
    if (river_ws_b64_encode_string(raw_key, sizeof(raw_key), key_b64) != 0) {
        return NULL;
    }

    path = wsclient->path != NULL ? wsclient->path : "";
    if (river_ws_default_port(wsclient)) {
        port_suffix = "";
    } else {
        snprintf(port_buffer, sizeof(port_buffer), ":%d", wsclient->port);
        port_suffix = port_buffer;
    }

    if (wsclient->origin != NULL && wsclient->origin[0] != '\0') {
        origin_prefix = "Origin: ";
        origin_value = wsclient->origin;
    } else {
        origin_prefix = "";
        origin_value = "";
    }

    protocol_len = river_ws_effective_field_length(wsclient->protocol,
                                                   wsclient->protocol_len);
    if (protocol_len > 0U) {
        protocol_prefix = "Sec-WebSocket-Protocol: ";
        protocol_value = wsclient->protocol;
    } else {
        protocol_prefix = "";
        protocol_value = "";
    }

    version_len = river_ws_effective_field_length(wsclient->version,
                                                  wsclient->version_len);
    if (version_len > 0U) {
        version_value = wsclient->version;
    } else {
        version_value = "13";
        version_len = strlen(version_value);
    }
    custom_token_len = river_ws_effective_field_length(wsclient->custom_token,
                                                       wsclient->custom_token_len);
    custom_token = custom_token_len > 0U ? wsclient->custom_token : "";
    header_fields_len = river_ws_effective_field_length(wsclient->header_fields,
                                                        wsclient->header_fields_len);
    header_fields = header_fields_len > 0U ? wsclient->header_fields : "";

    header_len = strlen("GET /") + strlen(path) + strlen(" HTTP/1.1\r\n") +
                 strlen("Host: ") + strlen(wsclient->host) + strlen(port_suffix) +
                 strlen("\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n") +
                 strlen(origin_prefix) + strlen(origin_value) +
                 (origin_prefix[0] != '\0' ? strlen("\r\n") : 0U) +
                 strlen("Sec-WebSocket-Key: ") + strlen((const char *)key_b64) +
                 strlen("\r\n") +
                 strlen(protocol_prefix) + protocol_len +
                 (protocol_prefix[0] != '\0' ? strlen("\r\n") : 0U) +
                 strlen("Sec-WebSocket-Version: ") + version_len +
                 strlen("\r\n") + custom_token_len + header_fields_len +
                 strlen("\r\n");

    header = (char *)ws_malloc((unsigned int)(header_len + 1U));
    if (header == NULL) {
        return NULL;
    }

    written = snprintf(header,
                       header_len + 1U,
                       "GET /%s HTTP/1.1\r\n"
                       "Host: %s%s\r\n"
                       "Upgrade: websocket\r\n"
                       "Connection: Upgrade\r\n"
                       "%s%s%s"
                       "Sec-WebSocket-Key: %s\r\n"
                       "%s%.*s%s"
                       "Sec-WebSocket-Version: %.*s\r\n"
                       "%.*s%.*s\r\n",
                       path,
                       wsclient->host,
                       port_suffix,
                       origin_prefix,
                       origin_value,
                       origin_prefix[0] != '\0' ? "\r\n" : "",
                       key_b64,
                       protocol_prefix,
                       (int)protocol_len,
                       protocol_value,
                       protocol_prefix[0] != '\0' ? "\r\n" : "",
                       (int)version_len,
                       version_value,
                       (int)custom_token_len,
                       custom_token,
                       (int)header_fields_len,
                       header_fields);
    if (written < 0 || (size_t)written >= header_len + 1U) {
        ws_free(header);
        return NULL;
    }
    return header;
}

int __wrap_ws_client_handshake(wsclient_context *wsclient)
{
    char *header;
    size_t header_len;
    int ret;

    header = river_ws_build_handshake_header(wsclient);
    if (header == NULL) {
        WSCLIENT_ERROR("ERROR: Orvibo handshake header build failed\n");
        return 0;
    }

    header_len = strlen(header);
    ret = wsclient->fun_ops.client_send(wsclient, header, header_len);
    if (ret > 0 && ret < (int)header_len) {
        ret = 0;
    }
    ws_free(header);
    return ret;
}
