#!/usr/bin/env python3
"""HF Space supervisor (Step 6). Owns app_port, spawns the engine (`--serve`), and exposes
POST /backup + GET /health. Restore-on-boot pulls the latest village.db release asset from
the dedicated data repo before the engine starts, so an HF sleep/reschedule resumes the same
run instead of resetting to tick 0 (the point of Step 4's EngineCheckpoint).

GITHUB_TOKEN and TBV_BACKUP_TOKEN are HF Space secrets (env vars), never committed.
TBV_BACKUP_TOKEN gates POST /backup (Bearer auth, 401 on missing/wrong) -- required per
MSG_075 §3.5/§5d: the Space's own privacy setting is not a substitute for authenticating
the one state-mutating route. TBV_DATA_REPO defaults to the dedicated `10-Bit-Village-Data`
repo (MASTER PLAN Step 6: engine repo stays clean, heavy DB doesn't trip HF's
git-push-triggers-rebuild deploy model). Releases are pruned to the last 10 (spec §2).

Step 7 fish tank: GET /ws is a minimal hand-rolled WebSocket server (RFC6455 handshake +
text-frame writer, no `websockets` dependency) pushing live VillagerState + MemoryGraph
events, event-driven on tick change -- not the pre-HF-pivot MSG_062 10Hz binary-spatial
premise, which real measured throughput (~354 ticks/hour, ~10s/tick) made moot (10Hz would
push identical frozen state ~100x per real tick change). Reuses the same ThreadingTCPServer
already proven for /health + /backup -- each connection gets its own thread, a long-lived
push loop fits that model directly, no async runtime needed. GET /status is the literal
MSG_075 5e-gate route (tick/uptime/last_backup); /health is kept as-is (AGY packet 089:
functionally equivalent, no need to replace).

Any other GET also serves replay/ (COPYd to ./web at Docker build) as static files, `/` ->
index.html -- HF loads the Space's own app_port page in an iframe, and without this a real
visitor just saw the bare {"error": "not found"} from the JSON API instead of the fish tank.
"""
import base64
import hashlib
import hmac
import http.server
import json
import mimetypes
import os
import signal
import socketserver
import sqlite3
import subprocess
import sys
import time
import urllib.error
import urllib.request

PORT = int(os.environ.get("PORT", 7860))
WEB_ROOT = os.path.abspath("web")  # Dockerfile COPYs replay/ here; absent in local dev runs
DATA_REPO = os.environ.get("TBV_DATA_REPO", "DevOGhere/10-Bit-Village-Data")
TOKEN = os.environ.get("GITHUB_TOKEN", "")
BACKUP_TOKEN = os.environ.get("TBV_BACKUP_TOKEN", "")
API = f"https://api.github.com/repos/{DATA_REPO}"
STAGING_DB = "staging/backup.db"
STAGING_MARKER = "staging/backup.done"
DB_PATH = "village.db"
BACKUP_TIMEOUT_S = 30
STALE_AFTER_S = 3600
SPECTATOR_POLL_S = 2  # well above the real ~10s/tick cadence; still responsive, cheap to poll
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"  # RFC6455 fixed handshake constant

engine_proc = None
last_backup_ts = None
boot_ts = time.time()


def gh_request(method, url, data=None, headers=None, raw=False):
    hdrs = {"Authorization": f"token {TOKEN}", "Accept": "application/vnd.github+json"}
    if headers:
        hdrs.update(headers)
    body = data if raw else (json.dumps(data).encode() if data is not None else None)
    req = urllib.request.Request(url, data=body, method=method, headers=hdrs)
    with urllib.request.urlopen(req, timeout=BACKUP_TIMEOUT_S) as resp:
        return resp.status, resp.read()


def restore_on_boot():
    """Pull the latest release's village.db asset into CWD. No-op on first-ever boot."""
    if not TOKEN:
        print("restore: no GITHUB_TOKEN set, skipping (fresh boot)", flush=True)
        return
    # NOT /releases/latest -- GitHub defines "latest" as the most recent NON-prerelease,
    # non-draft release, and every backup release here is created with prerelease=True
    # (coarse-cadence internal artifacts, not public cuts). /releases/latest would silently
    # 404-equivalent forever. List instead and take releases[0] (API returns newest-first).
    try:
        _, body = gh_request("GET", f"{API}/releases?per_page=1")
    except urllib.error.HTTPError as e:
        print(f"restore: API error {e.code}, fresh boot", flush=True)
        return
    releases = json.loads(body)
    if not releases:
        print("restore: no releases yet, fresh boot", flush=True)
        return
    release = releases[0]
    asset = next((a for a in release.get("assets", []) if a["name"] == "village.db"), None)
    if not asset:
        print("restore: latest release has no village.db asset, fresh boot", flush=True)
        return
    _, blob = gh_request("GET", asset["url"], headers={"Accept": "application/octet-stream"})
    with open(DB_PATH, "wb") as f:
        f.write(blob)
    print(f"restore: pulled village.db from release {release['tag_name']} ({len(blob)} bytes)", flush=True)


def push_world_digest():
    """Commit a lightweight digest JSON dump to the data repo for quick diffing (does not
    touch the heavy release asset path)."""
    import sqlite3
    con = sqlite3.connect(STAGING_DB)
    cur = con.cursor()
    cur.execute(
        "SELECT run_id, tick, seed, world_hash, belief_count, coined_terms, avg_importance, "
        "max_hearsay_depth, genome_dist_hash FROM WorldDigest ORDER BY tick DESC LIMIT 1"
    )
    row = cur.fetchone()
    if row is not None:
        cols = ["run_id", "tick", "seed", "world_hash", "belief_count", "coined_terms",
                "avg_importance", "max_hearsay_depth", "genome_dist_hash"]
        digest = dict(zip(cols, row))
    else:
        # --serve mode never calls log_digest_snapshot() (that's a --run-mode-only cadence),
        # so WorldDigest is always empty here. Fall back to EngineCheckpoint, which --serve
        # always writes -- gives at least run_id/tick instead of silently pushing nothing.
        cur.execute("SELECT run_id, tick FROM EngineCheckpoint ORDER BY tick DESC LIMIT 1")
        ckpt_row = cur.fetchone()
        digest = {"run_id": ckpt_row[0], "tick": ckpt_row[1]} if ckpt_row else {}
    con.close()
    if not digest:
        return
    content_b64 = __import__("base64").b64encode(json.dumps(digest, indent=2).encode()).decode()

    sha = None
    try:
        _, body = gh_request("GET", f"{API}/contents/world_digest.json")
        sha = json.loads(body)["sha"]
    except urllib.error.HTTPError as e:
        if e.code != 404:
            raise

    payload = {"message": f"digest @ tick {digest['tick']}", "content": content_b64}
    if sha:
        payload["sha"] = sha
    gh_request("PUT", f"{API}/contents/world_digest.json", payload)


def prune_old_releases(keep=10):
    """Keep only the `keep` most recent backup releases (spec §2: retention = keep-last-10).
    Best-effort -- a failed prune here must not fail the backup that already succeeded."""
    try:
        _, body = gh_request("GET", f"{API}/releases?per_page=100")
        releases = json.loads(body)
        for old in releases[keep:]:
            gh_request("DELETE", f"{API}/releases/{old['id']}")
    except Exception as e:
        print(f"prune: non-fatal, {e}", flush=True)


def do_backup():
    """Signal the engine to stage a fresh backup, then push it as a GitHub Release asset."""
    global last_backup_ts
    if engine_proc is None or engine_proc.poll() is not None:
        return False, "engine not running"
    try:
        os.remove(STAGING_MARKER)
    except FileNotFoundError:
        pass
    engine_proc.send_signal(signal.SIGUSR1)
    for _ in range(int(BACKUP_TIMEOUT_S / 0.5)):
        if os.path.exists(STAGING_MARKER):
            break
        time.sleep(0.5)
    else:
        return False, "timed out waiting for staging/backup.done"

    if not TOKEN:
        return False, "no GITHUB_TOKEN set"

    tag = f"backup-{int(time.time())}"
    _, body = gh_request("POST", f"{API}/releases", {"tag_name": tag, "name": tag, "prerelease": True})
    release = json.loads(body)
    upload_url = release["upload_url"].split("{")[0]
    with open(STAGING_DB, "rb") as f:
        blob = f.read()
    gh_request("POST", f"{upload_url}?name=village.db", data=blob, raw=True,
               headers={"Content-Type": "application/octet-stream"})
    push_world_digest()
    prune_old_releases()
    last_backup_ts = time.time()
    return True, tag


def read_live_state(last_tick, last_event_tick):
    """Poll village.db for the newest ticked VillagerState rows + any MemoryGraph events
    since last_event_tick. Read-only; the engine subprocess is the sole writer (WAL handles
    concurrent readers). Returns None when nothing new exists on either axis.

    Positions and events are NOT the same tick counter in practice: cognition dispatch
    fold-back applies actions/hearsay N+5 ticks later and dreams N+20 (Phase 3 design) --
    confirmed empirically here (VillagerState.tick_id ran ~7 ticks ahead of MemoryGraph.tick
    mid-run). An exact `WHERE tick = <current position tick>` match would silently miss
    almost every event. Track the two independently instead."""
    con = sqlite3.connect(DB_PATH, timeout=1)
    cur = con.cursor()
    cur.execute("SELECT MAX(tick_id) FROM VillagerState")
    row = cur.fetchone()
    tick = row[0] if row and row[0] is not None else None

    positions = None
    if tick is not None and tick != last_tick:
        cur.execute(
            "SELECT villager_id, pos_x, pos_y, holding_food, hunger, social, safety "
            "FROM VillagerState WHERE tick_id = ? ORDER BY villager_id", (tick,)
        )
        positions = [list(r) for r in cur.fetchall()]

    cur.execute(
        "SELECT tick, villager_id, actor_id, type, source_depth, importance, text "
        "FROM MemoryGraph WHERE tick > ? ORDER BY tick, id", (last_event_tick,)
    )
    events = [list(r) for r in cur.fetchall()]
    con.close()

    if positions is None and not events:
        return None
    new_event_tick = events[-1][0] if events else last_event_tick
    return {
        "tick": tick if positions is not None else last_tick,
        "positions": positions if positions is not None else [],
        # each event keeps its OWN tick (leading column) -- events applying now can belong to
        # several distinct past ticks at once (fold-back catch-up), never assume msg.tick
        "events": events,
        "_last_event_tick": new_event_tick,
    }


def ws_accept_key(key):
    return base64.b64encode(hashlib.sha1((key + WS_GUID).encode()).digest()).decode()


def send_ws_text_frame(sock, text):
    """RFC6455 unmasked text frame (server->client frames MUST NOT be masked, unlike
    client->server). No fragmentation -- payloads here are small JSON snapshots."""
    payload = text.encode("utf-8")
    length = len(payload)
    if length <= 125:
        header = bytes([0x81, length])
    elif length <= 65535:
        header = bytes([0x81, 126]) + length.to_bytes(2, "big")
    else:
        header = bytes([0x81, 127]) + length.to_bytes(8, "big")
    sock.sendall(header + payload)


class Handler(http.server.BaseHTTPRequestHandler):
    def _json(self, code, payload):
        body = json.dumps(payload).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/health":
            alive = engine_proc is not None and engine_proc.poll() is None
            stale = (last_backup_ts is None) or (time.time() - last_backup_ts > STALE_AFTER_S)
            self._json(200 if alive else 503,
                       {"engine_alive": alive, "last_backup_ts": last_backup_ts, "stale": stale})
        elif self.path == "/status":
            # Literal MSG_075 5e-gate route (tick/uptime/last_backup). Kept alongside /health
            # (packet 089: /health already gave equivalent proof) rather than replacing it.
            tick = None
            try:
                con = sqlite3.connect(DB_PATH, timeout=1)
                row = con.execute("SELECT tick FROM EngineCheckpoint LIMIT 1").fetchone()
                con.close()
                tick = row[0] if row else None
            except sqlite3.Error:
                pass
            self._json(200, {"tick": tick, "uptime_s": time.time() - boot_ts,
                              "last_backup_ts": last_backup_ts})
        elif self.path == "/ws":
            self.handle_ws_spectator()
        else:
            self.serve_static()

    def serve_static(self):
        # Visiting the Space's own page hits this (HF loads app_port's "/" in an iframe) --
        # without it, a real visitor saw a bare `{"error": "not found"}` instead of the fish
        # tank (found looking at the actual rendered Space page, not just curling routes).
        # Serves replay/ (COPYd to ./web in the Dockerfile); 404s cleanly if that dir is
        # absent, e.g. running supervisor.py directly in local dev without the image build.
        rel = "index.html" if self.path == "/" else self.path.lstrip("/")
        full = os.path.abspath(os.path.join(WEB_ROOT, rel))
        if not full.startswith(WEB_ROOT + os.sep) and full != WEB_ROOT:
            self._json(403, {"error": "forbidden"})
            return
        if not os.path.isfile(full):
            self._json(404, {"error": "not found"})
            return
        ctype, _ = mimetypes.guess_type(full)
        with open(full, "rb") as f:
            body = f.read()
        self.send_response(200)
        self.send_header("Content-Type", ctype or "application/octet-stream")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def handle_ws_spectator(self):
        key = self.headers.get("Sec-WebSocket-Key")
        if not key or self.headers.get("Upgrade", "").lower() != "websocket":
            self._json(400, {"error": "expected websocket upgrade"})
            return
        self.send_response(101, "Switching Protocols")
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", ws_accept_key(key))
        self.end_headers()
        last_tick = -1
        last_event_tick = -1
        try:
            while True:
                state = read_live_state(last_tick, last_event_tick)
                if state is not None:
                    last_tick = state["tick"]
                    last_event_tick = state.pop("_last_event_tick")
                    send_ws_text_frame(self.connection, json.dumps(state))
                else:
                    # heartbeat so HF's reverse proxy doesn't idle-time the connection out
                    # between real tick changes (~10s apart)
                    send_ws_text_frame(self.connection, json.dumps({"tick": last_tick, "heartbeat": True}))
                time.sleep(SPECTATOR_POLL_S)
        except (BrokenPipeError, ConnectionResetError, OSError):
            pass

    def do_POST(self):
        if self.path == "/backup":
            # POST /backup mutates external state (creates a GitHub Release) and this
            # Space's port is reachable by anyone who has the URL -- privacy on the Space
            # itself is a separate, revocable setting, not a substitute for authenticating
            # the one state-changing route. TBV_BACKUP_TOKEN gates it; unset means the
            # endpoint is unusable (fails closed), never silently open.
            presented = self.headers.get("Authorization", "")
            expected = f"Bearer {BACKUP_TOKEN}"
            if not BACKUP_TOKEN or not hmac.compare_digest(presented, expected):
                self._json(401, {"error": "unauthorized"})
                return
            ok, info = do_backup()
            self._json(200 if ok else 500, {"ok": ok, "info": info})
        else:
            self._json(404, {"error": "not found"})

    def log_message(self, fmt, *args):
        print("supervisor: " + (fmt % args), flush=True)


def main():
    global engine_proc
    restore_on_boot()
    os.makedirs("staging", exist_ok=True)
    engine_proc = subprocess.Popen(["./tbv_engine", "--serve"])

    def relay_term(signum, frame):
        if engine_proc and engine_proc.poll() is None:
            engine_proc.send_signal(signal.SIGTERM)
            engine_proc.wait(timeout=BACKUP_TIMEOUT_S)
        sys.exit(0)

    signal.signal(signal.SIGTERM, relay_term)

    httpd = socketserver.ThreadingTCPServer(("0.0.0.0", PORT), Handler)
    # /ws connections loop until the client disconnects (could be indefinitely) -- daemon
    # threads so a live spectator can't block process exit on SIGTERM.
    httpd.daemon_threads = True
    httpd.serve_forever()


if __name__ == "__main__":
    main()
