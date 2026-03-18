#!/usr/bin/env python3
import argparse
import base64
import email.utils
import hashlib
import hmac
import json
import os
import socket
import struct
import sys
import urllib.parse


DEFAULT_HOST = "tts-api.xfyun.cn"
DEFAULT_PORT = 80
DEFAULT_PATH = "/v2/tts"
DEFAULT_AUE = "raw"
DEFAULT_AUF = "audio/L16;rate=16000"
DEFAULT_TTE = "UTF8"
DEFAULT_VCN = "xiaoyan"


def build_query(api_key: str, api_secret: str, host: str, path: str) -> tuple[str, str]:
    date = email.utils.formatdate(usegmt=True)
    signature_origin = f"host: {host}\ndate: {date}\nGET {path} HTTP/1.1"
    signature = base64.b64encode(
        hmac.new(api_secret.encode("utf-8"),
                 signature_origin.encode("utf-8"),
                 hashlib.sha256).digest()
    ).decode("utf-8")
    authorization_origin = (
        f'api_key="{api_key}",algorithm="hmac-sha256",'
        f'headers="host date request-line",signature="{signature}"'
    )
    authorization = base64.b64encode(authorization_origin.encode("utf-8")).decode("utf-8")
    query = urllib.parse.urlencode({
        "host": host,
        "date": date,
        "authorization": authorization,
    })
    return query, date


def recv_exact(sock: socket.socket, size: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < size:
        block = sock.recv(size - len(chunks))
        if not block:
            raise RuntimeError("socket closed unexpectedly")
        chunks.extend(block)
    return bytes(chunks)


def send_ws_text(sock: socket.socket, text: str) -> None:
    payload = text.encode("utf-8")
    first = 0x81
    mask_bit = 0x80
    length = len(payload)

    if length < 126:
        header = bytes([first, mask_bit | length])
    elif length < (1 << 16):
        header = bytes([first, mask_bit | 126]) + struct.pack("!H", length)
    else:
        header = bytes([first, mask_bit | 127]) + struct.pack("!Q", length)

    mask = os.urandom(4)
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    sock.sendall(header + mask + masked)


def send_ws_close(sock: socket.socket) -> None:
    mask = os.urandom(4)
    sock.sendall(b"\x88\x80" + mask)


def recv_ws_frame(sock: socket.socket) -> tuple[int, bytes]:
    b1, b2 = recv_exact(sock, 2)
    opcode = b1 & 0x0F
    masked = (b2 >> 7) & 1
    length = b2 & 0x7F

    if length == 126:
        length = struct.unpack("!H", recv_exact(sock, 2))[0]
    elif length == 127:
        length = struct.unpack("!Q", recv_exact(sock, 8))[0]

    mask = recv_exact(sock, 4) if masked else b""
    payload = recv_exact(sock, length) if length else b""
    if masked:
        payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    return opcode, payload


def recv_ws_message(sock: socket.socket) -> str:
    parts: list[bytes] = []
    while True:
        b1, b2 = recv_exact(sock, 2)
        fin = (b1 >> 7) & 1
        opcode = b1 & 0x0F
        masked = (b2 >> 7) & 1
        length = b2 & 0x7F

        if length == 126:
            length = struct.unpack("!H", recv_exact(sock, 2))[0]
        elif length == 127:
            length = struct.unpack("!Q", recv_exact(sock, 8))[0]

        mask = recv_exact(sock, 4) if masked else b""
        payload = recv_exact(sock, length) if length else b""
        if masked:
            payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))

        if opcode == 0x8:
            raise RuntimeError("server closed websocket")
        if opcode == 0x9:
            pong = b"\x8A"
            pong_payload = payload
            if len(pong_payload) < 126:
                sock.sendall(pong + bytes([len(pong_payload)]) + pong_payload)
            else:
                raise RuntimeError("ping payload too large")
            continue
        if opcode not in (0x1, 0x0):
            continue

        parts.append(payload)
        if fin:
            return b"".join(parts).decode("utf-8", errors="replace")


def ws_handshake(host: str, port: int, path_query: str) -> socket.socket:
    proxy = os.environ.get("http_proxy") or os.environ.get("HTTP_PROXY")
    request_target = path_query

    if proxy:
        proxy_url = urllib.parse.urlparse(proxy)
        connect_host = proxy_url.hostname or host
        connect_port = proxy_url.port or 80
        request_target = f"http://{host}:{port}{path_query}"
    else:
        connect_host = host
        connect_port = port

    sock = socket.create_connection((connect_host, connect_port), timeout=15)
    sock.settimeout(15)
    key = base64.b64encode(os.urandom(16)).decode("ascii")
    request = (
        f"GET {request_target} HTTP/1.1\r\n"
        f"Host: {host}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n"
    ).encode("utf-8")
    sock.sendall(request)

    response = bytearray()
    while b"\r\n\r\n" not in response:
        response.extend(sock.recv(4096))
    header = response.split(b"\r\n\r\n", 1)[0].decode("utf-8", errors="replace")
    if " 101 " not in header.splitlines()[0]:
        raise RuntimeError(f"websocket handshake failed: {header}")
    return sock


def main() -> int:
    parser = argparse.ArgumentParser(description="Debug iFlytek online TTS WebSocket API over ws://")
    parser.add_argument("--app-id", required=True)
    parser.add_argument("--api-key", required=True)
    parser.add_argument("--api-secret", required=True)
    parser.add_argument("--text", required=True)
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--path", default=DEFAULT_PATH)
    parser.add_argument("--vcn", default=DEFAULT_VCN)
    parser.add_argument("--aue", default=DEFAULT_AUE)
    parser.add_argument("--auf", default=DEFAULT_AUF)
    parser.add_argument("--tte", default=DEFAULT_TTE)
    parser.add_argument("--output", default="/tmp/iflytek_tts_ws_debug.pcm")
    args = parser.parse_args()

    query, date = build_query(args.api_key, args.api_secret, args.host, args.path)
    path_query = f"{args.path}?{query}"
    print(f"[debug] connecting: ws://{args.host}:{args.port}{path_query}")
    print(f"[debug] date={date}", flush=True)

    sock = ws_handshake(args.host, args.port, path_query)
    audio_chunks = 0
    audio_bytes = 0
    sid = None

    payload = {
        "common": {
            "app_id": args.app_id,
        },
        "business": {
            "aue": args.aue,
            "auf": args.auf,
            "vcn": args.vcn,
            "tte": args.tte,
            "speed": 50,
            "volume": 50,
            "pitch": 50,
        },
        "data": {
            "status": 2,
            "text": base64.b64encode(args.text.encode("utf-8")).decode("utf-8"),
        },
    }

    out_path = args.output
    with open(out_path, "wb") as f:
        send_ws_text(sock, json.dumps(payload, ensure_ascii=False))
        print("[debug] request sent", flush=True)

        while True:
            raw = recv_ws_message(sock)
            message = json.loads(raw)
            print(json.dumps(message, ensure_ascii=False), flush=True)

            if message.get("sid") and sid is None:
                sid = message.get("sid")
            code = message.get("code")
            if isinstance(code, int) and code != 0:
                print(f"[error] code={code} message={message.get('message')}", file=sys.stderr)
                send_ws_close(sock)
                sock.close()
                return 2

            data = message.get("data") or {}
            audio_b64 = data.get("audio")
            if audio_b64:
                audio = base64.b64decode(audio_b64)
                f.write(audio)
                audio_bytes += len(audio)
                audio_chunks += 1

            if data.get("status") == 2:
                break

    send_ws_close(sock)
    sock.close()
    print(f"[debug] sid={sid} audio_chunks={audio_chunks} audio_bytes={audio_bytes} output={out_path}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
