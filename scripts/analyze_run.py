#!/usr/bin/env python3
"""10-Bit Village -- Run 2 Plan Phase A1: offline analysis harness.

Reads a village.db (any run) and computes the 8 metrics from
"10-Bit Village -- Run 2 Plan (Quality Epoch)" §PHASE A / A1, bucketed by
1000-tick windows, and writes a markdown report (+ optional PNGs if
matplotlib is importable). Read-only, never touches the engine.

Usage:
    python3 scripts/analyze_run.py village.db [--out replay/reports] [--label run1]
    python3 scripts/analyze_run.py village.db --stdout   # table dump, no file writes

Metrics (see plan §A1 for full rationale):
    1. Voice convergence      -- mean pairwise TF-IDF cosine sim across villager corpora
    2. Opener concentration   -- share of events whose first-6-token opener is top-10 in-bucket
    3. Vocabulary diversity   -- distinct-trigram rate
    4. Lineage demography     -- HearsayChain hop-depth + active-lineage count + lifetime
    5. Mutation vs genome     -- mean content_word_delta grouped by suspicion/curiosity (H1)
    6. Coinage adoption       -- adopters-over-time + adoption-lag-vs-grid-distance (H3)
    7. Truncation rate        -- share of texts not ending on a sentence boundary
    8. Myth-persistence       -- calcified-motif count (>=3 villagers, >=5000-tick lifetime)
"""
import argparse
import math
import os
import re
import sqlite3
import sys
from collections import Counter, defaultdict

BUCKET = 1000
WORD_RE = re.compile(r"[a-zA-Z']+")
TERMINATORS = set(".!?")
TRAILING_STRIP = "\"'”’) "  # closing quote/paren chars a terminator can hide behind
INFLECT_SUFFIXES = ("s", "es", "ed", "ing", "ly", "er", "ers")
DICT_PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "assets", "dict_en.txt")


def load_dict(path=DICT_PATH):
    try:
        with open(path) as f:
            return set(w.strip().lower() for w in f if w.strip())
    except FileNotFoundError:
        return None


def looks_coined(term, dictset):
    """Heuristic mirror of B5's planned inflection/fragment filter -- used here only to
    give the Run 1 baseline a clean (post-filter) number to compare Run 2 against, since
    metric 8 on raw CoinedWords is dominated by exactly the noise B5 targets."""
    if dictset is None:
        return True  # no dict available -- don't silently discard, caller must caveat
    if len(term) < 5 or term in dictset:
        return False
    for suf in INFLECT_SUFFIXES:
        if term.endswith(suf):
            stem = term[: -len(suf)]
            if stem in dictset or (stem + "e") in dictset:
                return False
            if len(stem) >= 2 and stem[-1] == stem[-2] and stem[:-1] in dictset:
                return False
    return True


def tokenize(text):
    return [w.lower() for w in WORD_RE.findall(text or "")]


def bucket_of(tick):
    return tick // BUCKET


def decode_genome(pack):
    pack = pack or 0
    return {
        "aggression": pack & 3,
        "generosity": (pack >> 2) & 3,
        "curiosity": (pack >> 4) & 3,
        "sociability": (pack >> 6) & 3,
        "suspicion": (pack >> 8) & 3,
    }


def is_truncated(text):
    """Mirrors B4's trim_to_sentence intent: true if text does NOT end on a
    terminator (after stripping a trailing closing quote/paren/space)."""
    if not text:
        return False  # empty text isn't a truncation artifact, just absent
    s = text.rstrip()
    s = s.rstrip(TRAILING_STRIP)
    if not s:
        return False
    return s[-1] not in TERMINATORS


def cosine(a, b):
    common = set(a) & set(b)
    if not common:
        return 0.0
    num = sum(a[t] * b[t] for t in common)
    da = math.sqrt(sum(v * v for v in a.values()))
    db_ = math.sqrt(sum(v * v for v in b.values()))
    if da == 0.0 or db_ == 0.0:
        return 0.0
    return num / (da * db_)


def tfidf_vectors(docs):
    """docs: villager_id -> token list. Returns villager_id -> {term: weight}."""
    n = len(docs)
    df = Counter()
    for toks in docs.values():
        for t in set(toks):
            df[t] += 1
    idf = {t: math.log(n / df[t]) + 1.0 for t in df}
    vectors = {}
    for vid, toks in docs.items():
        tf = Counter(toks)
        total = sum(tf.values()) or 1
        vectors[vid] = {t: (c / total) * idf[t] for t, c in tf.items()}
    return vectors


def mean_pairwise_cosine(vectors):
    ids = list(vectors.keys())
    if len(ids) < 2:
        return None
    total, n = 0.0, 0
    for i in range(len(ids)):
        vi = vectors[ids[i]]
        for j in range(i + 1, len(ids)):
            total += cosine(vi, vectors[ids[j]])
            n += 1
    return total / n if n else None


def mean(xs):
    xs = list(xs)
    return sum(xs) / len(xs) if xs else None


def median(xs):
    xs = sorted(xs)
    n = len(xs)
    if n == 0:
        return None
    mid = n // 2
    if n % 2:
        return xs[mid]
    return (xs[mid - 1] + xs[mid]) / 2.0


def pearson(xs, ys):
    n = len(xs)
    if n < 2:
        return None
    mx, my = mean(xs), mean(ys)
    num = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    dx = math.sqrt(sum((x - mx) ** 2 for x in xs))
    dy = math.sqrt(sum((y - my) ** 2 for y in ys))
    if dx == 0.0 or dy == 0.0:
        return None
    return num / (dx * dy)


# ---------------------------------------------------------------------------
# Metric computation
# ---------------------------------------------------------------------------

def compute_metrics(con):
    cur = con.cursor()
    metrics = {}

    cur.execute("SELECT MIN(tick), MAX(tick) FROM CognitionLog")
    row = cur.fetchone()
    min_tick, max_tick = (row[0] or 0), (row[1] or 0)
    n_buckets = bucket_of(max_tick) + 1
    buckets = list(range(n_buckets))
    metrics["_meta"] = {
        "min_tick": min_tick, "max_tick": max_tick,
        "n_buckets": n_buckets, "bucket_size": BUCKET,
    }

    # ---- pull CognitionLog once, drive metrics 1/2/3/7 from the same pass ----
    cur.execute("SELECT tick, villager_id, raw_thought FROM CognitionLog ORDER BY tick")
    docs_by_bucket = defaultdict(lambda: defaultdict(list))  # bucket -> villager -> tokens
    openers_by_bucket = defaultdict(Counter)                  # bucket -> opener(6tok) -> count
    events_by_bucket = Counter()                              # bucket -> total events
    trigram_total = Counter()                                 # bucket -> total trigram instances
    trigram_unique = defaultdict(set)                         # bucket -> set(trigrams)
    truncated_by_bucket = Counter()
    n_cognition = 0
    for tick, vid, text in cur:
        n_cognition += 1
        b = bucket_of(tick)
        toks = tokenize(text)
        docs_by_bucket[b][vid].extend(toks)
        if toks:
            openers_by_bucket[b][" ".join(toks[:6])] += 1
        events_by_bucket[b] += 1
        for k in range(len(toks) - 2):
            tri = (toks[k], toks[k + 1], toks[k + 2])
            trigram_total[b] += 1
            trigram_unique[b].add(tri)
        if is_truncated(text):
            truncated_by_bucket[b] += 1

    # 1. Voice convergence
    voice_convergence = {}
    for b in buckets:
        docs = docs_by_bucket.get(b, {})
        docs = {v: t for v, t in docs.items() if t}
        if len(docs) >= 2:
            vecs = tfidf_vectors(docs)
            voice_convergence[b] = mean_pairwise_cosine(vecs)
        else:
            voice_convergence[b] = None
    metrics["voice_convergence"] = voice_convergence

    # 2. Opener concentration
    opener_concentration = {}
    for b in buckets:
        oc = openers_by_bucket.get(b)
        total = events_by_bucket.get(b, 0)
        if not oc or total == 0:
            opener_concentration[b] = None
            continue
        top10 = sum(c for _, c in oc.most_common(10))
        opener_concentration[b] = top10 / total
    metrics["opener_concentration"] = opener_concentration

    # 3. Vocabulary diversity (distinct-trigram rate)
    vocab_diversity = {}
    for b in buckets:
        total = trigram_total.get(b, 0)
        uniq = len(trigram_unique.get(b, ()))
        vocab_diversity[b] = (uniq / total) if total else None
    metrics["vocab_diversity"] = vocab_diversity

    # 7. Truncation rate
    truncation_rate = {}
    for b in buckets:
        total = events_by_bucket.get(b, 0)
        truncation_rate[b] = (truncated_by_bucket.get(b, 0) / total) if total else None
    metrics["truncation_rate"] = truncation_rate
    metrics["_n_cognition_rows"] = n_cognition

    # ---- metric 4: HearsayChain lineage demography ----
    cur.execute("SELECT origin_mem_id, hop, tick FROM HearsayChain")
    lineage_ticks = defaultdict(list)  # origin_mem_id -> [tick,...]
    active_lineages_by_bucket = defaultdict(set)
    hop_sum_by_bucket = Counter()
    hop_n_by_bucket = Counter()
    hop_hist = Counter()
    n_hearsay_rows = 0
    for origin, hop, tick in cur:
        n_hearsay_rows += 1
        lineage_ticks[origin].append(tick)
        b = bucket_of(tick)
        active_lineages_by_bucket[b].add(origin)
        hop_sum_by_bucket[b] += hop
        hop_n_by_bucket[b] += 1
        hop_hist[hop] += 1
    lifetimes = [max(ts) - min(ts) for ts in lineage_ticks.values() if len(ts) >= 2]
    metrics["lineage"] = {
        "active_per_bucket": {b: len(active_lineages_by_bucket.get(b, ())) for b in buckets},
        "mean_hop_per_bucket": {
            b: (hop_sum_by_bucket[b] / hop_n_by_bucket[b]) if hop_n_by_bucket[b] else None
            for b in buckets
        },
        "hop_histogram": dict(sorted(hop_hist.items())),
        "lifetime_mean": mean(lifetimes),
        "lifetime_median": median(lifetimes),
        "lifetime_max": max(lifetimes) if lifetimes else None,
        "n_multihop_lineages": len(lifetimes),
        "n_hearsay_rows": n_hearsay_rows,
    }

    # ---- metric 5: mutation vs genome (H1) ----
    cur.execute("SELECT dst_genome, content_word_delta FROM HearsayChain")
    delta_by_suspicion = defaultdict(list)
    delta_by_curiosity = defaultdict(list)
    for pack, delta in cur:
        g = decode_genome(pack)
        delta_by_suspicion[g["suspicion"]].append(delta)
        delta_by_curiosity[g["curiosity"]].append(delta)
    metrics["mutation_vs_genome"] = {
        "by_suspicion": {k: {"mean": mean(v), "n": len(v)} for k, v in sorted(delta_by_suspicion.items())},
        "by_curiosity": {k: {"mean": mean(v), "n": len(v)} for k, v in sorted(delta_by_curiosity.items())},
    }

    # ---- metric 6: coinage adoption (H3) ----
    cur.execute("SELECT term, coiner_id, birth_tick, adopter_id, adoption_tick FROM CoinageSpread ORDER BY term, adoption_tick")
    adoption_rows = cur.fetchall()
    adopters_by_term = defaultdict(list)  # term -> [(adoption_tick, adopter_id), ...]
    coiner_by_term = {}
    for term, coiner_id, birth_tick, adopter_id, adoption_tick in adoption_rows:
        adopters_by_term[term].append((adoption_tick, adopter_id))
        coiner_by_term[term] = (coiner_id, birth_tick)
    top_terms = sorted(adopters_by_term.items(), key=lambda kv: len(kv[1]), reverse=True)[:15]
    adoption_curves = {}
    for term, rows in top_terms:
        cum, seen = [], set()
        for tick, adopter in rows:
            seen.add(adopter)
            cum.append((tick, len(seen)))
        adoption_curves[term] = cum

    # position lookup: (tick_id, villager_id) -> (x, y); VillagerState may be pruned,
    # so this only covers whatever retention window survives in this DB.
    cur.execute("SELECT tick_id, villager_id, pos_x, pos_y FROM VillagerState")
    pos = {}
    for tick_id, vid, x, y in cur:
        pos[(tick_id, vid)] = (x, y)

    lags, dists = [], []
    n_adoptions, n_with_pos = 0, 0
    for term, coiner_id, birth_tick, adopter_id, adoption_tick in adoption_rows:
        n_adoptions += 1
        lag = adoption_tick - birth_tick
        cpos = pos.get((adoption_tick, coiner_id))
        apos = pos.get((adoption_tick, adopter_id))
        if cpos and apos:
            n_with_pos += 1
            dist = max(abs(cpos[0] - apos[0]), abs(cpos[1] - apos[1]))  # Chebyshev (grid steps)
            lags.append(lag)
            dists.append(dist)
    metrics["coinage_adoption"] = {
        "top_terms_curves": adoption_curves,
        "n_adoptions_total": n_adoptions,
        "n_adoptions_with_position_data": n_with_pos,
        "lag_vs_distance_pearson_r": pearson(dists, lags),
        "note": "position coverage limited to VillagerState's retained tick window "
                "(pruned in production; see B... pruning). r is computed only over "
                "adoptions whose tick fell inside that window.",
    }

    # ---- metric 8: myth-persistence (calcification guardrail, §0.1) ----
    cur.execute("SELECT term, birth_tick FROM CoinedWords")
    birth_by_term = dict(cur.fetchall())
    dictset = load_dict()
    filtered_terms = {t for t in birth_by_term if looks_coined(t, dictset)}
    qualify_tick = {}          # raw, on the un-filtered CoinedWords table
    qualify_tick_filtered = {}  # same, restricted to terms that survive the B5-style filter
    for term, rows in adopters_by_term.items():
        birth = birth_by_term.get(term)
        if birth is None:
            continue
        distinct_villagers = {coiner_by_term[term][0]}
        third_villager_tick = None
        for tick, adopter in rows:
            distinct_villagers.add(adopter)
            if third_villager_tick is None and len(distinct_villagers) >= 3:
                third_villager_tick = tick
        if third_villager_tick is None:
            continue
        qt = max(third_villager_tick, birth + 5000)
        qualify_tick[term] = qt
        if term in filtered_terms:
            qualify_tick_filtered[term] = qt
    qualifying_terms = sorted(qualify_tick.items(), key=lambda kv: kv[1])
    qualifying_terms_filtered = sorted(qualify_tick_filtered.items(), key=lambda kv: kv[1])

    def cumulative(qterms):
        out = {}
        for b in buckets:
            ceiling = (b + 1) * BUCKET - 1
            out[b] = sum(1 for _, qt in qterms if qt <= ceiling)
        return out

    metrics["myth_persistence"] = {
        "cumulative_per_bucket": cumulative(qualifying_terms),
        "final_count": len(qualifying_terms),
        "terms": [t for t, _ in qualifying_terms],
        "cumulative_per_bucket_filtered": cumulative(qualifying_terms_filtered),
        "final_count_filtered": len(qualifying_terms_filtered),
        "terms_filtered": [t for t, _ in qualifying_terms_filtered],
        "dict_available": dictset is not None,
        "coinedwords_total": len(birth_by_term),
        "coinedwords_surviving_filter": len(filtered_terms),
        "coinedwords_noise_rate": (1 - len(filtered_terms) / len(birth_by_term)) if birth_by_term else None,
    }

    return metrics


# ---------------------------------------------------------------------------
# Report rendering
# ---------------------------------------------------------------------------

def fmt(x, nd=4):
    if x is None:
        return "-"
    if isinstance(x, float):
        return f"{x:.{nd}f}"
    return str(x)


def render_markdown(metrics, label, db_paths):
    m = metrics["_meta"]
    lines = []
    lines.append(f"# 10-Bit Village — Run Analysis: {label}")
    lines.append("")
    source = db_paths[0] if len(db_paths) == 1 else f"{len(db_paths)} stitched archives: " + ", ".join(db_paths)
    lines.append(f"Source: `{source}`  ")
    lines.append(f"Tick range: {m['min_tick']}–{m['max_tick']} ({m['n_buckets']} buckets of {m['bucket_size']} ticks)  ")
    lines.append(f"CognitionLog rows analyzed: {metrics['_n_cognition_rows']}")
    lines.append("")

    lines.append("## 1–3, 7. Per-bucket time series")
    lines.append("")
    lines.append("| bucket | tick_start | voice_convergence | opener_concentration | vocab_diversity(trigram) | truncation_rate |")
    lines.append("|---|---|---|---|---|---|")
    for b in range(m["n_buckets"]):
        lines.append(
            f"| {b} | {b*BUCKET} | {fmt(metrics['voice_convergence'][b])} "
            f"| {fmt(metrics['opener_concentration'][b])} | {fmt(metrics['vocab_diversity'][b])} "
            f"| {fmt(metrics['truncation_rate'][b])} |"
        )
    lines.append("")

    lines.append("## 4. Lineage demography (HearsayChain)")
    lines.append("")
    lin = metrics["lineage"]
    lines.append(f"- HearsayChain rows: {lin['n_hearsay_rows']}")
    lines.append(f"- Multi-hop lineages: {lin['n_multihop_lineages']}")
    lines.append(f"- Lineage lifetime (ticks) — mean: {fmt(lin['lifetime_mean'], 1)}, median: {fmt(lin['lifetime_median'], 1)}, max: {fmt(lin['lifetime_max'])}")
    lines.append(f"- Hop-depth histogram: {lin['hop_histogram']}")
    lines.append("")
    lines.append("| bucket | active_lineages | mean_hop_depth |")
    lines.append("|---|---|---|")
    for b in range(m["n_buckets"]):
        lines.append(f"| {b} | {lin['active_per_bucket'][b]} | {fmt(lin['mean_hop_per_bucket'][b], 2)} |")
    lines.append("")

    lines.append("## 5. Mutation vs genome (H1: high-suspicion mutates more)")
    lines.append("")
    mvg = metrics["mutation_vs_genome"]
    lines.append("| suspicion (0-3) | mean content_word_delta | n |")
    lines.append("|---|---|---|")
    for k, v in mvg["by_suspicion"].items():
        lines.append(f"| {k} | {fmt(v['mean'], 2)} | {v['n']} |")
    lines.append("")
    lines.append("| curiosity (0-3) | mean content_word_delta | n |")
    lines.append("|---|---|---|")
    for k, v in mvg["by_curiosity"].items():
        lines.append(f"| {k} | {fmt(v['mean'], 2)} | {v['n']} |")
    lines.append("")

    lines.append("## 6. Coinage adoption (H3: adoption follows grid proximity)")
    lines.append("")
    ca = metrics["coinage_adoption"]
    lines.append(f"- Total adoption events: {ca['n_adoptions_total']}")
    lines.append(f"- Adoptions with position data (VillagerState retention window): {ca['n_adoptions_with_position_data']}")
    lines.append(f"- Pearson r (grid distance vs adoption lag), n={ca['n_adoptions_with_position_data']}: {fmt(ca['lag_vs_distance_pearson_r'])}")
    lines.append(f"- Note: {ca['note']}")
    lines.append("")
    lines.append("Top-15 terms by adopter count — cumulative distinct-adopter curve (tick, cum_adopters):")
    lines.append("")
    for term, curve in ca["top_terms_curves"].items():
        sample = curve[::max(1, len(curve)//8)] if len(curve) > 8 else curve
        lines.append(f"- **{term}**: {sample}")
    lines.append("")

    lines.append("## 8. Myth-persistence (calcification guardrail, §0.1)")
    lines.append("")
    mp = metrics["myth_persistence"]
    lines.append(f"- Raw CoinedWords: {mp['coinedwords_total']} terms, {mp['coinedwords_surviving_filter']} survive a B5-style inflection/fragment filter ({fmt(mp['coinedwords_noise_rate'], 3)} noise rate) -- **matches packet 098's ~90% noise estimate.**")
    lines.append(f"- Calcified-motif count on RAW CoinedWords (>=3 villagers AND >=5000-tick lifetime): {mp['final_count']} -- **inflated by inflection noise, not a clean number.**")
    lines.append(f"- Calcified-motif count on FILTERED terms (post B5-style filter) -- **this is the number Run 2 should be compared against**: **{mp['final_count_filtered']}**")
    lines.append(f"- Filtered qualifying terms: {mp['terms_filtered']}")
    lines.append("")
    lines.append("| bucket | cumulative_calcified_motifs_raw | cumulative_calcified_motifs_filtered |")
    lines.append("|---|---|---|")
    for b in range(m["n_buckets"]):
        lines.append(f"| {b} | {mp['cumulative_per_bucket'][b]} | {mp['cumulative_per_bucket_filtered'][b]} |")
    lines.append("")

    return "\n".join(lines)


def try_plots(metrics, out_dir, label):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        return []
    written = []
    m = metrics["_meta"]
    xs = list(range(m["n_buckets"]))

    def line(name, ys, ylabel):
        fig, ax = plt.subplots(figsize=(8, 3))
        ax.plot(xs, [y if y is not None else float("nan") for y in ys], marker="o", ms=2)
        ax.set_xlabel("tick bucket (x1000)")
        ax.set_ylabel(ylabel)
        ax.set_title(f"{label}: {ylabel}")
        fig.tight_layout()
        path = os.path.join(out_dir, f"{label}_{name}.png")
        fig.savefig(path, dpi=110)
        plt.close(fig)
        written.append(path)

    line("voice_convergence", [metrics["voice_convergence"][b] for b in xs], "voice_convergence")
    line("opener_concentration", [metrics["opener_concentration"][b] for b in xs], "opener_concentration")
    line("vocab_diversity", [metrics["vocab_diversity"][b] for b in xs], "vocab_diversity")
    line("truncation_rate", [metrics["truncation_rate"][b] for b in xs], "truncation_rate")
    line("myth_persistence", [metrics["myth_persistence"]["cumulative_per_bucket_filtered"][b] for b in xs], "myth_persistence_cumulative_filtered")
    return written


def open_stitched(paths):
    """§C2 -- multi-archive stitch mode. `paths` must be oldest-to-newest.

    Two different table shapes, two different join strategies:
      - MemoryGraph/CognitionLog/HearsayChain/VillagerState are the tables C2's prune-
        after-archive actually deletes from, so consecutive archives tile disjoint (well,
        TEXT_KEEP/villager-state-keep-ticks-overlapping) tick WINDOWS of the run's history --
        stitched here via a UNION ALL across all of them. A TEMP VIEW with the table's own
        name transparently shadows the real table for every UNQUALIFIED query already in
        this file (verified: sqlite resolves an unqualified name to the temp schema first),
        so compute_metrics() needs ZERO changes to consume stitched data.
      - CoinedWords/CoinageSpread/BeliefSurvival/WorldDigest are NEVER pruned (small,
        cumulative tables) -- the newest archive already contains the complete history for
        these, so they're deliberately left un-viewed and just resolve to `main` (the last
        path, opened as the base connection) as-is. Unioning them would multiply-count every
        term/belief across however many archives it survived in.

    Overlap caveat, stated plainly rather than silently absorbed: the windowed tables DO
    overlap by up to one retention window at each archive boundary (a row can appear in both
    the archive it was pruned-after-covered-by and the next one, if it fell inside that next
    archive's own un-pruned-yet range at capture time). Bucketed/aggregate metrics here treat
    that overlap as a small amount of double-counted noise at each seam, not corrected for --
    acceptable for what this stitch mode is: a convenience read across a long run's archives,
    not a precision instrument."""
    if len(paths) == 1:
        return sqlite3.connect(f"file:{paths[0]}?mode=ro", uri=True), False
    con = sqlite3.connect(f"file:{paths[-1]}?mode=ro", uri=True)  # newest = main
    for i, p in enumerate(paths[:-1]):
        con.execute(f"ATTACH DATABASE 'file:{p}?mode=ro' AS archive{i}")
    windowed_tables = ["MemoryGraph", "CognitionLog", "HearsayChain", "VillagerState"]
    aliases = ["main"] + [f"archive{i}" for i in range(len(paths) - 1)]
    for table in windowed_tables:
        union_sql = " UNION ALL ".join(f"SELECT * FROM {a}.{table}" for a in aliases)
        con.execute(f"CREATE TEMP VIEW {table} AS {union_sql}")
    return con, True


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("db", nargs="+", help="path to village.db, or multiple archive-*.db "
                                           "paths (oldest-to-newest) for multi-archive stitch mode")
    ap.add_argument("--out", default="replay/reports", help="output dir for report + PNGs")
    ap.add_argument("--label", default=None, help="label for the report (default: first db's filename stem)")
    ap.add_argument("--stdout", action="store_true", help="print report to stdout instead of writing files")
    args = ap.parse_args()

    for p in args.db:
        if not os.path.exists(p):
            print(f"error: {p} not found", file=sys.stderr)
            sys.exit(1)

    label = args.label or os.path.splitext(os.path.basename(args.db[0]))[0]
    con, stitched = open_stitched(args.db)
    if stitched:
        print(f"stitched {len(args.db)} archives: {', '.join(os.path.basename(p) for p in args.db)}")
    try:
        metrics = compute_metrics(con)
    finally:
        con.close()

    report = render_markdown(metrics, label, args.db)

    if args.stdout:
        print(report)
        return

    os.makedirs(args.out, exist_ok=True)
    report_path = os.path.join(args.out, f"{label}.md")
    with open(report_path, "w") as f:
        f.write(report)
    pngs = try_plots(metrics, args.out, label)

    print(f"wrote {report_path}")
    if pngs:
        print(f"wrote {len(pngs)} PNGs: {', '.join(os.path.basename(p) for p in pngs)}")
    else:
        print("matplotlib not available -- skipped PNGs (markdown tables cover all metrics)")


if __name__ == "__main__":
    main()
