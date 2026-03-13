#!/usr/bin/env python3
import argparse
import base64
import datetime as dt
import hashlib
import hmac
import json
import time
import urllib.parse
import uuid

from websocket import create_connection


DEFAULT_HOST = "office-api-ast-dx.iflyaisol.com"
DEFAULT_PATH = "/ast/communicate/v1"
DEFAULT_AUDIO_ENCODE = "pcm_s16le"
DEFAULT_LANG = "autodialect"
DEFAULT_SR = "16000"
DEFAULT_FRAME_BYTES = 1280
DEFAULT_FRAME_INTERVAL_MS = 40


def utc_beijing_now() -> str:
    tz = dt.timezone(dt.timedelta(hours=8))
    return dt.datetime.now(tz).strftime("%Y-%m-%dT%H:%M:%S%z")


def build_query(app_id: str,
                access_key_id: str,
                access_key_secret: str,
                audio_encode: str,
                lang: str,
                samplerate: str,
                user_uuid: str | None) -> tuple[str, dict]:
    params = {
        "accessKeyId": access_key_id,
        "appId": app_id,
        "audio_encode": audio_encode,
        "lang": lang,
        "samplerate": samplerate,
        "utc": utc_beijing_now(),
        "uuid": user_uuid or uuid.uuid4().hex,
    }
    sorted_items = sorted(params.items(), key=lambda item: item[0])
    base_string = "&".join(
        f"{urllib.parse.quote(k, safe='')}={urllib.parse.quote(str(v), safe='')}"
        for k, v in sorted_items
    )
    signature = base64.b64encode(
        hmac.new(
            access_key_secret.encode("utf-8"),
            base_string.encode("utf-8"),
            hashlib.sha1,
        ).digest()
    ).decode("utf-8")
    params["signature"] = signature
    return urllib.parse.urlencode(params), params


def recv_json(ws):
    raw = ws.recv()
    if isinstance(raw, bytes):
        raw = raw.decode("utf-8", errors="replace")
    data = json.loads(raw)
    print(json.dumps(data, ensure_ascii=False, indent=2))
    return data


def main() -> int:
    parser = argparse.ArgumentParser(description="Debug iFlytek RTASR LLM API with PCM audio.")
    parser.add_argument("--app-id", required=True)
    parser.add_argument("--access-key-id", required=True)
    parser.add_argument("--access-key-secret", required=True)
    parser.add_argument("--pcm", required=True, help="16k/16bit/mono PCM file")
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--path", default=DEFAULT_PATH)
    parser.add_argument("--scheme", default="wss", choices=["ws", "wss"])
    parser.add_argument("--audio-encode", default=DEFAULT_AUDIO_ENCODE)
    parser.add_argument("--lang", default=DEFAULT_LANG)
    parser.add_argument("--samplerate", default=DEFAULT_SR)
    parser.add_argument("--frame-bytes", type=int, default=DEFAULT_FRAME_BYTES)
    parser.add_argument("--frame-interval-ms", type=int, default=DEFAULT_FRAME_INTERVAL_MS)
    parser.add_argument("--uuid")
    args = parser.parse_args()

    query, params = build_query(
        app_id=args.app_id,
        access_key_id=args.access_key_id,
        access_key_secret=args.access_key_secret,
        audio_encode=args.audio_encode,
        lang=args.lang,
        samplerate=args.samplerate,
        user_uuid=args.uuid,
    )
    url = f"{args.scheme}://{args.host}{args.path}?{query}"
    print(f"[debug] url={url}")
    print(f"[debug] params={json.dumps(params, ensure_ascii=False)}")

    ws = create_connection(url, timeout=15, enable_multithread=True)
    session_id = None
    try:
        with open(args.pcm, "rb") as pcm_file:
            started = recv_json(ws)
            data = started.get("data") or {}
            session_id = data.get("sessionId")

            frame_index = 0
            start_ms = time.time() * 1000.0
            while True:
                chunk = pcm_file.read(args.frame_bytes)
                if not chunk:
                    break

                expected_ms = start_ms + frame_index * args.frame_interval_ms
                now_ms = time.time() * 1000.0
                wait_ms = expected_ms - now_ms
                if wait_ms > 0:
                    time.sleep(wait_ms / 1000.0)

                ws.send_binary(chunk)
                frame_index += 1

            end_payload = {"end": True}
            if session_id:
                end_payload["sessionId"] = session_id
            ws.send(json.dumps(end_payload, ensure_ascii=False))
            print(f"[debug] sent end={json.dumps(end_payload, ensure_ascii=False)}")

            while True:
                message = recv_json(ws)
                action = message.get("action")
                code = message.get("code")
                if action == "error" or (isinstance(code, int) and code != 0):
                    return 2
                data = message.get("data") or {}
                if data.get("ls") is True:
                    return 0
    finally:
        ws.close()


if __name__ == "__main__":
    raise SystemExit(main())

