#!/usr/bin/env python3
"""Step 7 fish-tank /ws smoke check. Minimal hand-rolled WebSocket client (stdlib only,
matches supervisor.py's hand-rolled server -- no `websockets` dependency either side).

Local:  python3 scripts/ws_spectator_check.py [port] [duration_s]
Remote: python3 scripts/ws_spectator_check.py --host devog-10-bit-village.hf.space \
            --tls --duration 60
        (reads WS_AUTH_TOKEN from env for a private Space's proxy auth -- never hardcode it)
"""
import base64
import hashlib
import json
import os
import socket
import ssl
import sys
import time

argv = sys.argv[1:]
HOST = "localhost"
PORT = 7860
TLS = False
DURATION_S = 15
if argv and not argv[0].startswith("--"):
    PORT = int(argv[0])
    if len(argv) > 1:
        DURATION_S = float(argv[1])
else:
    i = 0
    while i < len(argv):
        if argv[i] == "--host":
            HOST = argv[i + 1]; i += 2
        elif argv[i] == "--tls":
            TLS = True; PORT = 443; i += 1
        elif argv[i] == "--duration":
            DURATION_S = float(argv[i + 1]); i += 2
        else:
            i += 1

auth_token = os.environ.get("WS_AUTH_TOKEN", "")

key = base64.b64encode(b"0123456789012345").decode()
raw_sock = socket.create_connection((HOST, PORT), timeout=10)
sock = ssl.create_default_context().wrap_socket(raw_sock, server_hostname=HOST) if TLS else raw_sock
auth_line = f"Authorization: Bearer {auth_token}\r\n" if auth_token else ""
req = (
    f"GET /ws HTTP/1.1\r\n"
    f"Host: {HOST}\r\n"
    f"{auth_line}"
    f"Upgrade: websocket\r\n"
    f"Connection: Upgrade\r\n"
    f"Sec-WebSocket-Key: {key}\r\n"
    f"Sec-WebSocket-Version: 13\r\n\r\n"
)
sock.sendall(req.encode())
sock.settimeout(DURATION_S + 5)

resp = sock.recv(4096).decode(errors="replace")
print("--- handshake response ---")
print(resp.splitlines()[0])
assert "101" in resp.splitlines()[0], "expected 101 Switching Protocols"

t0 = time.time()
frames = []
buf = b""
while time.time() - t0 < DURATION_S:
    try:
        chunk = sock.recv(65536)
    except socket.timeout:
        break
    if not chunk:
        break
    buf += chunk
    # minimal unmasked-frame parser (server frames are unmasked, small payloads only)
    while len(buf) >= 2:
        b0, b1 = buf[0], buf[1]
        length = b1 & 0x7F
        offset = 2
        if length == 126:
            if len(buf) < 4:
                break
            length = int.from_bytes(buf[2:4], "big")
            offset = 4
        elif length == 127:
            if len(buf) < 10:
                break
            length = int.from_bytes(buf[2:10], "big")
            offset = 10
        if len(buf) < offset + length:
            break
        payload = buf[offset:offset + length]
        buf = buf[offset + length:]
        frames.append(json.loads(payload.decode()))

print(f"--- received {len(frames)} frames over {DURATION_S}s ---")
for f in frames:
    if f.get("heartbeat"):
        print("heartbeat")
        continue
    event_ticks = [e[0] for e in f.get("events", [])]
    print(f"tick={f.get('tick')} positions={len(f.get('positions', []))} events={len(event_ticks)} event_ticks={event_ticks}")

total_events = sum(len(f.get("events", [])) for f in frames if not f.get("heartbeat"))
print(f"--- total events received: {total_events} ---")

sock.close()
