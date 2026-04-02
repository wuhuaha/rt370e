#!/usr/bin/env python3
"""Simple TCP debug server for the board-side river TCP diag client."""

from __future__ import annotations

import argparse
import socket
import sys
import threading
from typing import Optional, Tuple


class SessionState:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._conn: Optional[socket.socket] = None
        self._peer: Optional[Tuple[str, int]] = None

    def replace(self, conn: socket.socket, peer: Tuple[str, int]) -> None:
        with self._lock:
            old_conn = self._conn
            self._conn = conn
            self._peer = peer
        if old_conn is not None:
            try:
                old_conn.close()
            except OSError:
                pass

    def clear(self, conn: socket.socket) -> None:
        with self._lock:
            if self._conn is conn:
                self._conn = None
                self._peer = None

    def send_line(self, line: str) -> bool:
        payload = line.rstrip("\r\n").encode("utf-8") + b"\n"
        with self._lock:
            conn = self._conn
            peer = self._peer
        if conn is None:
            print("[host] no board connected; command dropped", file=sys.stderr)
            return False
        try:
            conn.sendall(payload)
        except OSError as exc:
            print(f"[host] send failed to {peer}: {exc}", file=sys.stderr)
            self.clear(conn)
            return False
        return True


def stdin_loop(state: SessionState) -> None:
    for raw_line in sys.stdin:
        line = raw_line.strip()
        if not line:
            continue
        state.send_line(line)


def connection_loop(conn: socket.socket, peer: Tuple[str, int], state: SessionState) -> None:
    print(f"[host] board connected from {peer[0]}:{peer[1]}", file=sys.stderr)
    state.replace(conn, peer)

    buffer = b""
    try:
        while True:
            chunk = conn.recv(4096)
            if not chunk:
                break
            buffer += chunk
            while b"\n" in buffer:
                line, buffer = buffer.split(b"\n", 1)
                text = line.decode("utf-8", errors="replace").rstrip("\r")
                if text:
                    print(text, flush=True)
    except OSError as exc:
        print(f"[host] connection error from {peer[0]}:{peer[1]}: {exc}", file=sys.stderr)
    finally:
        state.clear(conn)
        try:
            conn.close()
        except OSError:
            pass
        print(f"[host] board disconnected from {peer[0]}:{peer[1]}", file=sys.stderr)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Receive board logs over TCP and forward typed river commands back to the board."
    )
    parser.add_argument("--bind", default="0.0.0.0", help="bind address, default: 0.0.0.0")
    parser.add_argument("--port", type=int, default=8765, help="listen port, default: 8765")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    state = SessionState()

    stdin_thread = threading.Thread(target=stdin_loop, args=(state,), daemon=True)
    stdin_thread.start()

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((args.bind, args.port))
        server.listen(1)
        print(
            f"[host] listening on {args.bind}:{args.port}; type `river status` or `river kws status` and press Enter",
            file=sys.stderr,
        )

        while True:
            conn, peer = server.accept()
            connection_loop(conn, peer, state)


if __name__ == "__main__":
    raise SystemExit(main())
