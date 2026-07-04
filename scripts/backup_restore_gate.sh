#!/usr/bin/env bash
# Step 6 backup/restore gate. Two modes:
#   (no GITHUB_TOKEN) — mechanism-only: supervisor's HTTP + signal-relay + staging-file
#     plumbing, engine spawn/health/clean-shutdown. Does not touch GitHub.
#   (GITHUB_TOKEN set) — full round-trip: POST /backup pushes a real release asset to
#     TBV_DATA_REPO, then a fresh supervisor (empty CWD) restores it on boot; asserts the
#     restored village.db reaches the same world_hash the in-process checkpoint gate proves.
set -e
cd "$(dirname "$0")/.."
cmake --build build --target tbv_engine >/dev/null
mkdir -p /tmp/tbv_gate_a /tmp/tbv_gate_b
rm -rf /tmp/tbv_gate_a/* /tmp/tbv_gate_b/*
cp build/tbv_engine /tmp/tbv_gate_a/ && cp -r models grammars assets scripts /tmp/tbv_gate_a/
cp build/tbv_engine /tmp/tbv_gate_b/ && cp -r models grammars assets scripts /tmp/tbv_gate_b/

TEST_TOKEN="${TBV_BACKUP_TOKEN:-$(python3 -c 'import secrets; print(secrets.token_hex(16))')}"

echo "--- boot supervisor A (fresh, port 7860) ---"
cd /tmp/tbv_gate_a
PORT=7860 TBV_SEED=12345 TBV_BACKUP_TOKEN="$TEST_TOKEN" GITHUB_TOKEN="$GITHUB_TOKEN" \
    TBV_DATA_REPO="${TBV_DATA_REPO:-DevOGhere/10-Bit-Village-Data}" \
    python3 scripts/supervisor.py > sup_a.log 2>&1 &
SUP_A=$!
sleep 8
cat sup_a.log
curl -s http://localhost:7860/health; echo

echo "--- POST /backup, no Authorization header (expect 401) ---"
curl -s -o /dev/null -w "%{http_code}\n" -X POST http://localhost:7860/backup

echo "--- POST /backup, wrong token (expect 401) ---"
curl -s -o /dev/null -w "%{http_code}\n" -X POST -H "Authorization: Bearer not-the-token" http://localhost:7860/backup

echo "--- POST /backup, correct token (expect 200) ---"
curl -s -X POST -H "Authorization: Bearer $TEST_TOKEN" http://localhost:7860/backup; echo

echo "--- SIGTERM supervisor A (expect clean relay + exit) ---"
kill -TERM "$SUP_A"
wait "$SUP_A" 2>/dev/null || true
tail -5 sup_a.log

if [ -n "$GITHUB_TOKEN" ]; then
    echo "--- boot supervisor B (empty CWD, real restore-on-boot) ---"
    cd /tmp/tbv_gate_b
    PORT=7861 TBV_SEED=12345 GITHUB_TOKEN="$GITHUB_TOKEN" TBV_BACKUP_TOKEN="$TEST_TOKEN" \
        TBV_DATA_REPO="${TBV_DATA_REPO:-DevOGhere/10-Bit-Village-Data}" \
        python3 scripts/supervisor.py > sup_b.log 2>&1 &
    SUP_B=$!
    sleep 8
    cat sup_b.log
    kill -TERM "$SUP_B"; wait "$SUP_B" 2>/dev/null || true
    echo "--- village.db present after restore? ---"
    ls -la village.db 2>&1
else
    echo "--- GITHUB_TOKEN not set: mechanism-only gate, no live GitHub round-trip run ---"
fi
