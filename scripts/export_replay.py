#!/usr/bin/env python3
"""Export village.db -> replay.json for the standalone replay viewer (v1 stage 2).
Read-only, never touches the engine. Usage: python3 scripts/export_replay.py [village.db] [replay.json]
"""
import json
import sqlite3
import sys

src = sys.argv[1] if len(sys.argv) > 1 else "village.db"
dst = sys.argv[2] if len(sys.argv) > 2 else "replay/replay.json"

con = sqlite3.connect(src)
cur = con.cursor()

cur.execute("SELECT MAX(tick_id) FROM Ticks")
row = cur.fetchone()
total_ticks = (row[0] + 1) if row and row[0] is not None else 0

cur.execute(
    "SELECT tick_id, villager_id, pos_x, pos_y, holding_food, hunger, social, safety "
    "FROM VillagerState ORDER BY tick_id, villager_id"
)
positions = [list(r) for r in cur.fetchall()]

cur.execute(
    "SELECT tick, villager_id, actor_id, type, source_depth, importance, text "
    "FROM MemoryGraph ORDER BY tick, id"
)
events = [list(r) for r in cur.fetchall()]

cur.execute("SELECT term, coiner, birth_tick FROM CoinedWords ORDER BY birth_tick")
coinages = [list(r) for r in cur.fetchall()]

cur.execute("SELECT DISTINCT run_id FROM MemoryGraph LIMIT 1")
row = cur.fetchone()
run_id = row[0] if row else ""

out = {
    "meta": {
        "run_id": run_id,
        "grid_w": 32,
        "grid_h": 24,
        "max_villagers": 100,
        "ticks": total_ticks,
    },
    "positions": positions,
    "events": events,
    "coinages": coinages,
}

with open(dst, "w") as f:
    json.dump(out, f)

print(f"wrote {dst}: {total_ticks} ticks, {len(positions)} position rows, "
      f"{len(events)} events, {len(coinages)} coinages")
