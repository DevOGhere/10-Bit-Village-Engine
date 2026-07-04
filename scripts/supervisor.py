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
"""
import hmac
import http.server
import json
import os
import signal
import socketserver
import subprocess
import sys
import time
import urllib.error
import urllib.request

PORT = int(os.environ.get("PORT", 7860))
DATA_REPO = os.environ.get("TBV_DATA_REPO", "DevOGhere/10-Bit-Village-Data")
TOKEN = os.environ.get("GITHUB_TOKEN", "")
BACKUP_TOKEN = os.environ.get("TBV_BACKUP_TOKEN", "")
API = f"https://api.github.com/repos/{DATA_REPO}"
STAGING_DB = "staging/backup.db"
STAGING_MARKER = "staging/backup.done"
DB_PATH = "village.db"
BACKUP_TIMEOUT_S = 30
STALE_AFTER_S = 3600

engine_proc = None
last_backup_ts = None


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
        else:
            self._json(404, {"error": "not found"})

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
    httpd.serve_forever()


if __name__ == "__main__":
    main()
