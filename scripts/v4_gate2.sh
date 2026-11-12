#!/bin/bash
# V4 Gate 2: 2x2 SALIENCE x RETELL on fresh scratch runs, V3 flags (G,E) on in every arm.
# Sequential (GPU does not scale with parallel procs). Seeds interleaved so partial results stay paired.
# usage: scripts/v4_gate2.sh [ticks=4000] [seeds="101 202 303"]
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; TICKS="${1:-4000}"; SEEDS="${2:-101 202 303}"
OUT="$ROOT/replay/v4_gate2"
for seed in $SEEDS; do
  for arm in S0 SAL RET BOTH; do
    D="$OUT/${arm}_$seed"; [ -f "$D/DONE" ] && continue
    rm -rf "$D"; mkdir -p "$D"; ln -s "$ROOT/models" "$D/models"; ln -s "$ROOT/assets" "$D/assets" 2>/dev/null
    SAL=0; RET=0; [ "$arm" = SAL ] || [ "$arm" = BOTH ] && SAL=1; [ "$arm" = RET ] || [ "$arm" = BOTH ] && RET=1
    ( cd "$D" && TBV_FIX_G=1 TBV_FIX_E=1 TBV_FIX_SALIENCE=$SAL TBV_FIX_RETELL=$RET \
        "$ROOT/build_local/tbv_engine" --seed "$seed" --run "$TICKS" > run.log 2>&1 && touch DONE )
    echo "$(date +%T) finished $arm seed $seed" >> "$OUT/progress.log"
  done
done
echo "ALL DONE $(date +%T)" >> "$OUT/progress.log"
