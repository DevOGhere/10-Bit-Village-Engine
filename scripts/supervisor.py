#!/usr/bin/env python3
"""HF Space supervisor (Step 6). Owns app_port, spawns the engine (`--serve`), and exposes
POST /backup + GET /health. Restore-on-boot pulls the latest village.db release asset from
the dedicated data repo before the engine starts, so an HF sleep/reschedule resumes the same
run instead of resetting to tick 0 (the point of Step 4's EngineCheckpoint).

Run 1 -> Run 2 cutover (2026-07-12): all `backup-*` releases in the data repo were
deleted (full history preserved separately as the non-`backup-`-prefixed
`run1-full-history-tick77926` release, which restore_on_boot's tag-prefix filter
will never match) so the next boot finds nothing to restore and starts fresh at
tick 0 under the upgraded Phase A/B/C cognition code.

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
import threading
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
# §C2 -- weekly-cadence archive tier, ~60000 ticks at measured ~354 ticks/hr HF throughput
# (real number, not a guess: packet-5e's measured live rate). Never pruned; kills the ~2GB
# release-asset wall permanently since the rolling backup-* releases stay small once the
# text tables get pruned after each confirmed archive.
ARCHIVE_EVERY_TICKS = int(os.environ.get("TBV_ARCHIVE_EVERY_TICKS", 60000))
SPECTATOR_POLL_S = 2  # well above the real ~10s/tick cadence; still responsive, cheap to poll
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"  # RFC6455 fixed handshake constant
WS_BACKLOG_TICKS = 500  # A2: cap a connecting spectator's catch-up frame (Run 2 Plan §A2)
TOTALS_POLL_S = 10       # ~matches real tick cadence; new rows per poll are usually 0-few

MEMTYPE_NAMES = {0: "EXPERIENCE", 1: "HEARSAY", 2: "DREAM"}  # core/types.h MemType

engine_proc = None
last_backup_ts = None
boot_ts = time.time()

# ---- A2: run-so-far totals (Village Pulse tiles, packet 099) --------------------------
# packet 099's tiles claimed "true run totals" by relying on the WS catch-up frame replaying
# the FULL MemoryGraph history to every connecting client -- A2's WS_BACKLOG_TICKS cap breaks
# that assumption, so totals move here instead. QA (packet 102, item 6) required incremental
# counters, not a COUNT(*) scan on a 100MB WAL DB per /status call: one seed scan at boot,
# then a single background thread advances the count via `id > cursor` (indexed range, cheap)
# on its own poll loop -- independent of any WS connection, so multiple simultaneous
# spectators don't multiply the query load and totals never double-count.
run_totals_lock = threading.Lock()
run_totals = {"EXPERIENCE": 0, "HEARSAY": 0, "DREAM": 0, "coined": 0}
_totals_cursor = {"mem_id": 0, "coin_rowid": 0}


def seed_run_totals():
    con = sqlite3.connect(DB_PATH, timeout=1)
    cur = con.cursor()
    cur.execute("SELECT type, COUNT(*) FROM MemoryGraph GROUP BY type")
    for t, c in cur.fetchall():
        run_totals[MEMTYPE_NAMES.get(t, "EXPERIENCE")] = c
    cur.execute("SELECT COALESCE(MAX(id), 0) FROM MemoryGraph")
    _totals_cursor["mem_id"] = cur.fetchone()[0]
    cur.execute("SELECT COUNT(*), COALESCE(MAX(rowid), 0) FROM CoinedWords")
    coined, max_rowid = cur.fetchone()
    run_totals["coined"] = coined
    _totals_cursor["coin_rowid"] = max_rowid
    con.close()


def totals_poller():
    while True:
        time.sleep(TOTALS_POLL_S)
        try:
            con = sqlite3.connect(DB_PATH, timeout=1)
            cur = con.cursor()
            cur.execute(
                "SELECT type, COUNT(*), MAX(id) FROM MemoryGraph WHERE id > ? GROUP BY type",
                (_totals_cursor["mem_id"],),
            )
            rows = cur.fetchall()
            cur.execute(
                "SELECT COUNT(*), MAX(rowid) FROM CoinedWords WHERE rowid > ?",
                (_totals_cursor["coin_rowid"],),
            )
            coined_new, coined_max_rowid = cur.fetchone()
            con.close()
            if rows or coined_new:
                with run_totals_lock:
                    for t, c, max_id in rows:
                        run_totals[MEMTYPE_NAMES.get(t, "EXPERIENCE")] += c
                        _totals_cursor["mem_id"] = max(_totals_cursor["mem_id"], max_id)
                    if coined_new:
                        run_totals["coined"] += coined_new
                        _totals_cursor["coin_rowid"] = coined_max_rowid
        except sqlite3.Error as e:
            print(f"supervisor: totals_poller error: {e}", flush=True)


def gh_request(method, url, data=None, headers=None, raw=False):
    hdrs = {"Authorization": f"token {TOKEN}", "Accept": "application/vnd.github+json"}
    if headers:
        hdrs.update(headers)
    body = data if raw else (json.dumps(data).encode() if data is not None else None)
    req = urllib.request.Request(url, data=body, method=method, headers=hdrs)
    with urllib.request.urlopen(req, timeout=BACKUP_TIMEOUT_S) as resp:
        return resp.status, resp.read()


def restore_on_boot():
    """Pull the newest backup-* release's village.db asset into CWD. No-op on first-ever boot."""
    if not TOKEN:
        print("restore: no GITHUB_TOKEN set, skipping (fresh boot)", flush=True)
        return
    # C2 (Run 2 Plan §C2, sequencing item 8 -- moved into the cutover per packet 102): the
    # OLD `releases?per_page=1` took releases[0] = newest release by ANY tag. That was a
    # landmine even before C2's archive tier existed (a stray non-backup release created
    # after the last real backup would silently look like "no village.db asset -> fresh
    # boot", wiping the run's continuity) and is now a certainty once `archive-*` releases
    # exist alongside `backup-*` ones (weekly cadence, so an archive WILL often be newer
    # than the latest rolling backup). Filter explicitly to the `backup-` tag prefix instead
    # -- archives use a different asset name (village-archive.db) anyway, but filtering by
    # tag first means a differently-named-but-still-matched asset can never masquerade as a
    # restore point, and this fails closed (fresh boot) rather than restoring the wrong data.
    try:
        _, body = gh_request("GET", f"{API}/releases?per_page=20")
    except urllib.error.HTTPError as e:
        print(f"restore: API error {e.code}, fresh boot", flush=True)
        return
    releases = json.loads(body)
    release = next((r for r in releases if r.get("tag_name", "").startswith("backup-")), None)
    if release is None:
        print("restore: no backup-* releases in the newest 20, fresh boot", flush=True)
        return
    asset = next((a for a in release.get("assets", []) if a["name"] == "village.db"), None)
    if not asset:
        print(f"restore: {release['tag_name']} has no village.db asset, fresh boot", flush=True)
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
    """Keep only the `keep` most recent BACKUP releases (spec §2: retention = keep-last-10).
    Best-effort -- a failed prune here must not fail the backup that already succeeded.
    §C2: filtered to the `backup-` tag prefix -- the old ANY-tag version would eventually
    prune `archive-*` releases too (the whole point of the archive tier is that they're
    NEVER pruned), and would also delete the historical `run1-final-archive` cutover
    artifact once enough backups rolled past it."""
    try:
        _, body = gh_request("GET", f"{API}/releases?per_page=100")
        releases = json.loads(body)
        backups = [r for r in releases if r.get("tag_name", "").startswith("backup-")]
        for old in backups[keep:]:
            gh_request("DELETE", f"{API}/releases/{old['id']}")
    except Exception as e:
        print(f"prune: non-fatal, {e}", flush=True)


def newest_archive_tick():
    """§C2 -- the newest archive-<tick> release's embedded tick, or None if none exist yet.
    Deriving the archive cadence from GH state (not a local counter) means it survives
    container restarts the same way restore_on_boot already does -- no new persistent state."""
    try:
        _, body = gh_request("GET", f"{API}/releases?per_page=50")
    except urllib.error.HTTPError:
        return None
    for r in json.loads(body):
        tag = r.get("tag_name", "")
        if tag.startswith("archive-"):
            try:
                return int(tag.split("-", 1)[1])
            except ValueError:
                continue
    return None


def maybe_archive_and_prune(current_tick):
    """§C2 -- if a weekly-cadence archive is due, upload it (from the SAME staged DB
    do_backup() just produced -- no second stage needed) then signal the engine to prune
    MemoryGraph/CognitionLog/HearsayChain rows older than its retention window. The prune
    is gated on SIGUSR2 specifically so it can only ever happen right after a confirmed
    archive upload -- never on a bare timer, since this data is genuinely gone from the
    live db afterward and the archive is what makes that safe."""
    last_tick = newest_archive_tick()
    if last_tick is not None and current_tick - last_tick < ARCHIVE_EVERY_TICKS:
        return
    tag = f"archive-{current_tick}"
    try:
        _, body = gh_request("POST", f"{API}/releases", {"tag_name": tag, "name": tag, "prerelease": True})
        release = json.loads(body)
        upload_url = release["upload_url"].split("{")[0]
        with open(STAGING_DB, "rb") as f:
            blob = f.read()
        gh_request("POST", f"{upload_url}?name=village-archive.db", data=blob, raw=True,
                   headers={"Content-Type": "application/octet-stream"})
        print(f"archive: uploaded {tag} ({len(blob)} bytes)", flush=True)
    except Exception as e:
        print(f"archive: FAILED, not pruning ({e})", flush=True)
        return
    if engine_proc is not None and engine_proc.poll() is None:
        engine_proc.send_signal(signal.SIGUSR2)
        # exact prune boundary is the engine's own TBV_TEXT_KEEP call -- not duplicated here
        print(f"archive: sent SIGUSR2 (engine will prune text tables older than its retention window)", flush=True)


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

    # §C2: piggyback the archive-cadence check on the same staged DB this backup just
    # produced -- no second engine signal/stage round-trip needed to decide "is it due".
    try:
        con = sqlite3.connect(STAGING_DB)
        row = con.execute("SELECT tick FROM EngineCheckpoint ORDER BY tick DESC LIMIT 1").fetchone()
        con.close()
        if row is not None:
            maybe_archive_and_prune(row[0])
    except Exception as e:
        print(f"archive: skipped this cycle, non-fatal ({e})", flush=True)

    return True, tag


def read_live_state(last_tick, last_event_tick, last_coin_rowid, last_spread_rowid):
    """Poll village.db for the newest ticked VillagerState rows + any MemoryGraph events
    since last_event_tick + any CoinedWords past last_coin_rowid + any CoinageSpread past
    last_spread_rowid. Read-only; the engine subprocess is the sole writer (WAL handles
    concurrent readers). Returns None when nothing new exists on any axis.

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

    # CoinedWords has PK(run_id, term) but is a plain rowid table, and rows are only ever
    # INSERT OR IGNOREd (first-coiner-wins, never updated/deleted) -- rowid is a valid
    # monotonic cursor for "what's new since last push."
    cur.execute(
        "SELECT rowid, term, coiner, birth_tick FROM CoinedWords "
        "WHERE rowid > ? ORDER BY rowid", (last_coin_rowid,)
    )
    coin_rows = cur.fetchall()

    # B5 UI adoption filter (Run 2 Plan §B5): a term with zero adopters is rendered
    # filtered-out client-side, but at the moment a term is FIRST coined it always has zero
    # adopters (adoption happens strictly after coining) -- pushing coinage rows alone would
    # make the panel start empty and (almost) never fill in, since a term already pushed
    # never gets re-sent once past this cursor. CoinageSpread is also an append-only,
    # never-updated rowid table (one row per first-time adopter, log_coinage_adoption), so the
    # same incremental-cursor pattern applies: push new adoption events separately, client
    # merges them onto terms it already knows about.
    cur.execute(
        "SELECT rowid, term, adopter_id FROM CoinageSpread "
        "WHERE rowid > ? ORDER BY rowid", (last_spread_rowid,)
    )
    spread_rows = cur.fetchall()
    con.close()

    if positions is None and not events and not coin_rows and not spread_rows:
        return None
    new_event_tick = events[-1][0] if events else last_event_tick
    return {
        "tick": tick if positions is not None else last_tick,
        "positions": positions if positions is not None else [],
        # each event keeps its OWN tick (leading column) -- events applying now can belong to
        # several distinct past ticks at once (fold-back catch-up), never assume msg.tick
        "events": events,
        "coinages": [[r[1], r[2], r[3]] for r in coin_rows],
        "adoptions": [[r[1], r[2]] for r in spread_rows],  # [term, adopter_id]
        "_last_event_tick": new_event_tick,
        "_last_coin_rowid": coin_rows[-1][0] if coin_rows else last_coin_rowid,
        "_last_spread_rowid": spread_rows[-1][0] if spread_rows else last_spread_rowid,
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
            with run_totals_lock:
                totals = {
                    "moments": run_totals["EXPERIENCE"],
                    "gossips": run_totals["HEARSAY"],
                    "dreams": run_totals["DREAM"],
                    "coined": run_totals["coined"],
                }
            self._json(200, {"tick": tick, "uptime_s": time.time() - boot_ts,
                              "last_backup_ts": last_backup_ts, "run_totals": totals})
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
        # A2 (Run 2 Plan §A2): Run 1 replayed the FULL MemoryGraph history to every
        # connecting spectator -- harmless at launch, but an unbounded catch-up frame on a
        # long-lived run (Run 1 hit ~14MB at tick ~32k) only grows. Cap it the same way the
        # coinage cursor below already does: start ~500 ticks back, then stream incrementally.
        # (Village Pulse "run totals" no longer come from this frame -- see run_totals/A2.)
        last_event_tick = -1  # -1 = no floor; a fresh/short run has nothing to cap yet
        con = sqlite3.connect(DB_PATH, timeout=1)
        row = con.execute("SELECT MAX(tick) FROM MemoryGraph").fetchone()
        con.close()
        if row and row[0] is not None:
            last_event_tick = max(-1, row[0] - WS_BACKLOG_TICKS)
        # Coinage snapshot cap: a long run accumulates thousands of coined terms, and a
        # cursor starting at 0 would dump the entire table into the first frame. Start the
        # cursor 200 rows back instead -- new spectators see the newest 200 terms, then
        # stream incrementally. Same connection as the loop's reads (WAL, read-only).
        last_coin_rowid = 0
        con = sqlite3.connect(DB_PATH, timeout=1)
        row = con.execute("SELECT MAX(rowid) FROM CoinedWords").fetchone()
        con.close()
        if row and row[0] is not None:
            last_coin_rowid = max(0, row[0] - 200)
        # B5: adoption-cursor snapshot mirrors the coinage cursor's own newest-N cap -- a
        # spectator connecting on a long run doesn't need every historical adoption, just
        # enough to light up adopters>=1 on the newest-200 coinage snapshot above it (200 is
        # a generous cap: each of those terms needs at most 1 counted event to clear the filter).
        last_spread_rowid = 0
        con = sqlite3.connect(DB_PATH, timeout=1)
        row = con.execute("SELECT MAX(rowid) FROM CoinageSpread").fetchone()
        con.close()
        if row and row[0] is not None:
            last_spread_rowid = max(0, row[0] - 200)
        try:
            while True:
                state = read_live_state(last_tick, last_event_tick, last_coin_rowid, last_spread_rowid)
                if state is not None:
                    last_tick = state["tick"]
                    last_event_tick = state.pop("_last_event_tick")
                    last_coin_rowid = state.pop("_last_coin_rowid")
                    last_spread_rowid = state.pop("_last_spread_rowid")
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

    try:
        seed_run_totals()
    except sqlite3.Error as e:
        print(f"supervisor: seed_run_totals failed (fresh boot, DB not created yet?): {e}", flush=True)
    threading.Thread(target=totals_poller, daemon=True).start()

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
