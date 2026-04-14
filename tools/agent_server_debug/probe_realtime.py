#!/usr/bin/env python3
"""Probe agent-server realtime connectivity without external dependencies."""

from __future__ import annotations

import argparse
import base64
import http.client
import os
import socket
import ssl
import sys
from dataclasses import dataclass
from typing import Iterable


DEFAULT_DISCOVERY_PATH = "/v1/realtime"
DEFAULT_WS_PATH = "/v1/realtime/ws"
DEFAULT_SUBPROTOCOL = "agent-server.realtime.v0"


@dataclass
class ProbeResult:
    kind: str
    target: str
    ok: bool
    detail: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, help="Target host or IP.")
    parser.add_argument(
        "--ports",
        nargs="+",
        type=int,
        default=[443, 8080, 80],
        help="Ports to probe.",
    )
    parser.add_argument(
        "--schemes",
        nargs="+",
        default=["https", "wss", "http", "ws"],
        help="Schemes to probe in order.",
    )
    parser.add_argument(
        "--discovery-path",
        default=DEFAULT_DISCOVERY_PATH,
        help="HTTP discovery path.",
    )
    parser.add_argument(
        "--ws-path",
        default=DEFAULT_WS_PATH,
        help="WebSocket path.",
    )
    parser.add_argument(
        "--subprotocol",
        default=DEFAULT_SUBPROTOCOL,
        help="WebSocket subprotocol.",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=5.0,
        help="Per-request timeout in seconds.",
    )
    parser.add_argument(
        "--server-name",
        default="",
        help="Override TLS SNI server name.",
    )
    parser.add_argument(
        "--host-header",
        default="",
        help="Override HTTP Host header.",
    )
    parser.add_argument(
        "--insecure",
        action="store_true",
        help="Disable TLS certificate verification for debug checks.",
    )
    return parser.parse_args()


def make_ssl_context(insecure: bool) -> ssl.SSLContext:
    if insecure:
        context = ssl._create_unverified_context()  # noqa: SLF001
    else:
        context = ssl.create_default_context()
    return context


def tcp_probe(host: str, port: int, timeout: float) -> ProbeResult:
    target = f"{host}:{port}"
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return ProbeResult("tcp", target, True, "connect ok")
    except Exception as exc:  # noqa: BLE001
        return ProbeResult("tcp", target, False, f"{type(exc).__name__}: {exc}")


def http_probe(
    scheme: str,
    host: str,
    port: int,
    path: str,
    timeout: float,
    server_name: str,
    host_header: str,
    insecure: bool,
) -> ProbeResult:
    target = f"{scheme}://{host}:{port}{path}"
    conn: http.client.HTTPConnection | http.client.HTTPSConnection | None = None
    try:
        if scheme == "https":
            conn = http.client.HTTPSConnection(
                host=host,
                port=port,
                timeout=timeout,
                context=make_ssl_context(insecure),
            )
        else:
            conn = http.client.HTTPConnection(host=host, port=port, timeout=timeout)
        headers = {}
        if host_header:
            headers["Host"] = host_header
        conn.request("GET", path, headers=headers)
        resp = conn.getresponse()
        body = resp.read(256)
        snippet = body.decode("utf-8", errors="replace").replace("\n", "\\n")
        return ProbeResult(
            f"http:{scheme}",
            target,
            True,
            f"status={resp.status} reason={resp.reason} body={snippet}",
        )
    except ssl.SSLError as exc:
        detail = f"SSLError: {exc}"
        if server_name:
            detail += f" sni={server_name}"
        return ProbeResult(f"http:{scheme}", target, False, detail)
    except Exception as exc:  # noqa: BLE001
        return ProbeResult(f"http:{scheme}", target, False, f"{type(exc).__name__}: {exc}")
    finally:
        if conn is not None:
            conn.close()


def websocket_probe(
    scheme: str,
    host: str,
    port: int,
    path: str,
    subprotocol: str,
    timeout: float,
    server_name: str,
    host_header: str,
    insecure: bool,
) -> ProbeResult:
    target = f"{scheme}://{host}:{port}{path}"
    key = base64.b64encode(os.urandom(16)).decode("ascii")
    sock = None
    try:
        sock = socket.create_connection((host, port), timeout=timeout)
        if scheme == "wss":
            context = make_ssl_context(insecure)
            sock = context.wrap_socket(
                sock,
                server_hostname=server_name or host,
            )
        request_host = host_header or host
        request = (
            f"GET {path} HTTP/1.1\r\n"
            f"Host: {request_host}\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            f"Sec-WebSocket-Protocol: {subprotocol}\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n"
        )
        sock.sendall(request.encode("ascii"))
        response = recv_http_headers(sock)
        status_line = response.split("\r\n", 1)[0]
        ok = " 101 " in status_line
        detail = status_line
        if "Sec-WebSocket-Protocol:" in response:
            for line in response.split("\r\n"):
                if line.lower().startswith("sec-websocket-protocol:"):
                    detail += f" {line.strip()}"
                    break
        return ProbeResult(f"ws:{scheme}", target, ok, detail)
    except ssl.SSLError as exc:
        return ProbeResult(f"ws:{scheme}", target, False, f"SSLError: {exc}")
    except Exception as exc:  # noqa: BLE001
        return ProbeResult(f"ws:{scheme}", target, False, f"{type(exc).__name__}: {exc}")
    finally:
        if sock is not None:
            sock.close()


def recv_http_headers(sock: socket.socket) -> str:
    chunks = []
    while True:
        data = sock.recv(4096)
        if not data:
            break
        chunks.append(data)
        merged = b"".join(chunks)
        if b"\r\n\r\n" in merged:
            return merged.split(b"\r\n\r\n", 1)[0].decode("utf-8", errors="replace")
    return b"".join(chunks).decode("utf-8", errors="replace")


def print_result(result: ProbeResult) -> None:
    status = "PASS" if result.ok else "FAIL"
    print(f"[{status}] {result.kind:<8} {result.target} :: {result.detail}")


def iter_unique(values: Iterable[str]) -> list[str]:
    seen: set[str] = set()
    ordered: list[str] = []
    for value in values:
        if value not in seen:
            ordered.append(value)
            seen.add(value)
    return ordered


def main() -> int:
    args = parse_args()
    schemes = iter_unique(args.schemes)
    any_ok = False

    for port in args.ports:
        tcp_result = tcp_probe(args.host, port, args.timeout)
        print_result(tcp_result)
        any_ok = any_ok or tcp_result.ok

        for scheme in schemes:
            if scheme in ("http", "https"):
                if (scheme == "http" and port == 443) or (scheme == "https" and port == 80):
                    continue
                result = http_probe(
                    scheme=scheme,
                    host=args.host,
                    port=port,
                    path=args.discovery_path,
                    timeout=args.timeout,
                    server_name=args.server_name,
                    host_header=args.host_header,
                    insecure=args.insecure,
                )
                print_result(result)
                any_ok = any_ok or result.ok
            elif scheme in ("ws", "wss"):
                if (scheme == "ws" and port == 443) or (scheme == "wss" and port == 80):
                    continue
                result = websocket_probe(
                    scheme=scheme,
                    host=args.host,
                    port=port,
                    path=args.ws_path,
                    subprotocol=args.subprotocol,
                    timeout=args.timeout,
                    server_name=args.server_name,
                    host_header=args.host_header,
                    insecure=args.insecure,
                )
                print_result(result)
                any_ok = any_ok or result.ok

    return 0 if any_ok else 1


if __name__ == "__main__":
    sys.exit(main())
