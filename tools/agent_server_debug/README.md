# Agent Server Debug Tools

This directory contains local debug helpers for validating connectivity to the
cloud `agent-server` deployment before changing board-side transport logic.

Current target:

- host: `101.33.235.154`

## Probe Realtime Endpoint

`probe_realtime.py` checks:

- raw TCP reachability
- HTTP/HTTPS discovery response
- WebSocket / secure WebSocket upgrade handshake

Examples:

```bash
cd /root/ameba-river
python3 tools/agent_server_debug/probe_realtime.py --host 101.33.235.154
python3 tools/agent_server_debug/probe_realtime.py --host 101.33.235.154 --ports 443 8080 80
python3 tools/agent_server_debug/probe_realtime.py --host 101.33.235.154 --schemes https wss --insecure
python3 tools/agent_server_debug/probe_realtime.py --host 101.33.235.154 --server-name your-domain.example --host-header your-domain.example
```

Use `--server-name` and `--host-header` when the cloud deployment terminates
TLS behind a domain certificate but the board currently connects by raw IP.
