#!/usr/bin/env python3
"""Step 7 fish-tank /ws smoke check. Minimal hand-rolled WebSocket client (stdlib only,
matches supervisor.py's hand-rolled server -- no `websockets` dependency either side)
against a running supervisor. Usage: python3 scripts/ws_spectator_check.py [port] [duration_s]
"""
import base64
import hashlib
import json
import socket
import sys
import time

HOST = "localhost"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 7860
DURATION_S = float(sys.argv[2]) if len(sys.argv) > 2 else 15

key = base64.b64encode(b"0123456789012345").decode()
sock = socket.create_connection((HOST, PORT), timeout=10)
req = (
    f"GET /ws HTTP/1.1\r\n"
    f"Host: {HOST}:{PORT}\r\n"
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
