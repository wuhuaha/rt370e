#!/usr/bin/env python3
"""Apply the Ameba SDK websocket connect-error reporting patch.

This keeps the SDK-side delta tracked from inside the project repository while
fixing a misleading websocket error path in `/root/ameba-rtos`.
"""

from __future__ import annotations

import argparse
import pathlib
import sys


OLD_BLOCK = """\t\tif (wsclient_connecttimeout != 0) {\n\t\t\tfcntl(wsclient->sockfd, F_SETFL, fcntl(wsclient->sockfd, F_GETFL, 0) | O_NONBLOCK);\n\t\t\tconnect(wsclient->sockfd, p->ai_addr, p->ai_addrlen);\n\n\t\t\tif (errno == EINPROGRESS) {\n\t\t\t\tfd_set wfds;\n\t\t\t\tstruct timeval time_out;\n\n\t\t\t\ttime_out.tv_sec = wsclient_connecttimeout / 1000;\n\t\t\t\ttime_out.tv_usec = (wsclient_connecttimeout % 1000) * 1000;\n\t\t\t\tFD_ZERO(&wfds) ;\n\t\t\t\tFD_SET(wsclient->sockfd, &wfds);\t// Only set server fd\n\n\t\t\t\t// Use select to wait for non-blocking connect\n\t\t\t\tint select_ret = select(wsclient->sockfd + 1, NULL, &wfds, NULL, &time_out);\n\n\t\t\t\tif (select_ret == 1) {\n\t\t\t\t\tfcntl(wsclient->sockfd, F_SETFL, fcntl(wsclient->sockfd, F_GETFL, 0) & ~O_NONBLOCK);\n\t\t\t\t\tbreak;\n\t\t\t\t}\n\t\t\t}\n\t\t} else {\n"""


NEW_BLOCK = """\t\tif (wsclient_connecttimeout != 0) {\n\t\t\tint connect_ret;\n\t\t\tint so_error = 0;\n\t\t\tsocklen_t so_error_len = sizeof(so_error);\n\n\t\t\tfcntl(wsclient->sockfd, F_SETFL, fcntl(wsclient->sockfd, F_GETFL, 0) | O_NONBLOCK);\n\t\t\tconnect_ret = connect(wsclient->sockfd, p->ai_addr, p->ai_addrlen);\n\t\t\tif (connect_ret == 0) {\n\t\t\t\tfcntl(wsclient->sockfd, F_SETFL, fcntl(wsclient->sockfd, F_GETFL, 0) & ~O_NONBLOCK);\n\t\t\t\tbreak;\n\t\t\t}\n\n\t\t\tif (errno == EINPROGRESS) {\n\t\t\t\tfd_set wfds;\n\t\t\t\tstruct timeval time_out;\n\n\t\t\t\ttime_out.tv_sec = wsclient_connecttimeout / 1000;\n\t\t\t\ttime_out.tv_usec = (wsclient_connecttimeout % 1000) * 1000;\n\t\t\t\tFD_ZERO(&wfds) ;\n\t\t\t\tFD_SET(wsclient->sockfd, &wfds);\t// Only set server fd\n\n\t\t\t\t// Use select to wait for non-blocking connect and confirm SO_ERROR.\n\t\t\t\tint select_ret = select(wsclient->sockfd + 1, NULL, &wfds, NULL, &time_out);\n\n\t\t\t\tif (select_ret == 1) {\n\t\t\t\t\tif (getsockopt(wsclient->sockfd, SOL_SOCKET, SO_ERROR, &so_error, &so_error_len) == 0 && so_error == 0) {\n\t\t\t\t\t\tfcntl(wsclient->sockfd, F_SETFL, fcntl(wsclient->sockfd, F_GETFL, 0) & ~O_NONBLOCK);\n\t\t\t\t\t\tbreak;\n\t\t\t\t\t}\n\t\t\t\t\tif (so_error != 0) {\n\t\t\t\t\t\tWSCLIENT_ERROR(\"ERROR: Connect failed after select: so_error=%d\\n\", so_error);\n\t\t\t\t\t} else {\n\t\t\t\t\t\tWSCLIENT_ERROR(\"ERROR: getsockopt(SO_ERROR) failed errno(%d)\\n\", errno);\n\t\t\t\t\t}\n\t\t\t\t} else if (select_ret == 0) {\n\t\t\t\t\tWSCLIENT_ERROR(\"ERROR: Connect timeout after %d ms\\n\", wsclient_connecttimeout);\n\t\t\t\t} else {\n\t\t\t\t\tWSCLIENT_ERROR(\"ERROR: Connect select failed ret(%d) errno(%d)\\n\", select_ret, errno);\n\t\t\t\t}\n\t\t\t} else {\n\t\t\t\tWSCLIENT_ERROR(\"ERROR: connect failed ret(%d) errno(%d)\\n\", connect_ret, errno);\n\t\t\t}\n\t\t} else {\n"""


def patch_sdk_text(text: str) -> str:
    if NEW_BLOCK in text:
        return text
    if OLD_BLOCK not in text:
        raise RuntimeError("target ws_hostname_connect block not found or not unique")
    return text.replace(OLD_BLOCK, NEW_BLOCK, 1)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--sdk-root",
        default="/root/ameba-rtos",
        help="SDK root to patch or check",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Validate that the patch is already applied without modifying files",
    )
    args = parser.parse_args()

    sdk_root = pathlib.Path(args.sdk_root).resolve()
    target = sdk_root / "component" / "network" / "websocket" / "libwsclient.c"
    if not target.exists():
        raise FileNotFoundError(f"libwsclient.c not found: {target}")

    original = target.read_text(encoding="utf-8")
    updated = patch_sdk_text(original)

    if args.check:
        print("applied" if original == updated else "not-applied")
        return 0 if original == updated else 1

    if original == updated:
        print(f"sdk_root={sdk_root} target={target} status=unchanged")
        return 0

    target.write_text(updated, encoding="utf-8")
    print(f"sdk_root={sdk_root} target={target} status=changed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
