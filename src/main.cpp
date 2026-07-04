#include <iostream>
#include <string>
#include <cstdlib>
#include <vector>
#include "engine/world.h"
#include "infra/db.h"
#include "cognition/llama_bridge.h"
#include "cognition/coinage.h"
#include "cognition/degradation.h"   // hearsay_hop, levenshtein, extract_hearsay_fields

#include "cognition/resolver.h"
#include "cognition/cognitive_store.h"
#include <set>
#include <sstream>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <fstream>

// --serve signal handling (Step 5b). Handlers touch ONLY these flags -- async-signal-safety
// forbids calling SQLite/iostreams/malloc from a handler. Real work happens at the next tick
// boundary in the --serve loop, which polls and clears these.
static volatile sig_atomic_t g_backup_requested = 0;
static volatile sig_atomic_t g_shutdown_requested = 0;
static void tbv_on_sigusr1(int) { g_backup_requested = 1; }
static void tbv_on_sigterm(int) { g_shutdown_requested = 1; }

#define TBV_ENGINE_VERSION "65debc3"

// Jaccard token-overlap (lowercase, alpha-only, len>2) — Phase 2 drift metric.
static std::set<std::string> tok_set(std::string s) {
    for (auto& c : s) c = (char)tolower(c);
    std::set<std::string> r; std::stringstream ss(s); std::string w;
    while (ss >> w) { std::string x; for (char c : w) if (isalpha((unsigned char)c)) x += c;
        if (x.size() > 2) r.insert(x); }
    return r;
}
static double jaccard(const std::set<std::string>& a, const std::set<std::string>& b) {
    if (a.empty() || b.empty()) return 0.0;
    int inter = 0; for (auto& x : a) if (b.count(x)) inter++;
    return (double)inter / (double)(a.size() + b.size() - inter);
}

// FNV-1a 64-bit over the seed-driven RAM state (Phase 3 determinism gate). Hashes every
// villager's resident memories (mem_id/importance/type/depth/text) + positions + needs.
static uint64_t fnv_u64(uint64_t h, uint64_t x) {
    for (int b = 0; b < 8; ++b) { h ^= (x & 0xff); h *= 1099511628211ULL; x >>= 8; }
    return h;
}
static uint64_t fnv_str(uint64_t h, const std::string& s) {
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}
static uint64_t world_hash(const tbv::WorldState& w) {
    uint64_t h = 1469598103934665603ULL;
    for (uint32_t i = 0; i < tbv::MAX_VILLAGERS; ++i) {
        h = fnv_u64(h, w.pos_x[i]); h = fnv_u64(h, w.pos_y[i]);
        h = fnv_u64(h, (uint64_t)w.needs[i].hunger);
        h = fnv_u64(h, (uint64_t)w.needs[i].social);
        h = fnv_u64(h, (uint64_t)w.needs[i].safety);
        h = fnv_u64(h, w.holding_food[i] ? 1u : 0u);
        for (const auto& m : w.stores[i].all()) {
            h = fnv_u64(h, m.mem_id); h = fnv_u64(h, (uint64_t)m.importance);
            h = fnv_u64(h, (uint64_t)m.type); h = fnv_u64(h, m.source_depth);
            h = fnv_str(h, m.text);
        }
    }
    return h;
}

// WorldDigest periodic snapshot (Step 4) — shared by --run's 100-tick cadence and the
// --checkpoint_gate proof. belief_count/avg_importance/max_hearsay_depth are live-state
// aggregates (decay-aware via effective_importance); genome_dist_hash is an FNV digest over
// every villager's packed genome (deterministic, villager-id order).
static void log_digest_snapshot(tbv::Database& db, const tbv::WorldState& w,
                                const std::string& run_id, uint64_t seed) {
    using namespace tbv;
    int belief_count = 0, coined = (int)w.coined_words.size();
    long long imp_sum = 0; int max_depth = 0;
    for (uint32_t i = 0; i < MAX_VILLAGERS; ++i) {
        for (const auto& m : w.stores[i].all()) {
            belief_count++;
            imp_sum += CognitiveStore::effective_importance(m, w.current_tick);
            if (m.type == MemType::HEARSAY && m.source_depth > max_depth) max_depth = m.source_depth;
        }
    }
    int avg_imp = belief_count ? (int)(imp_sum / belief_count) : 0;
    uint64_t gh = 1469598103934665603ULL;
    for (uint32_t i = 0; i < MAX_VILLAGERS; ++i) gh = fnv_u64(gh, w.genomes[i].pack());
    db.log_world_digest(run_id, w.current_tick, seed, world_hash(w), belief_count, coined,
                        avg_imp, max_depth, gh);
}

// Phase 1 Re-Cut gate vectors: grounded situation + engine facts.
static std::vector<tbv::PerceptionContext> phase1_vectors() {
    using namespace tbv;
    std::vector<PerceptionContext> v;
    v.push_back({"You are a thronglet. You are desperately starving and holding a ripe apple. Think about your situation.",
                 {3000, 60000, 70000}, {true, true, false}});
    v.push_back({"You are a thronglet, painfully lonely, holding bread. Your neighbour Borin stands next to you, starving. Think about your situation.",
                 {70000, 5000, 80000}, {true, false, true}});
    v.push_back({"You are a suspicious, fearful thronglet. A stranger stands beside you watching. You feel terrified. Think about your situation.",
                 {60000, 40000, 8000}, {false, false, true}});
    return v;
}

int main(int argc, char** argv) {
    uint64_t seed = 12345;
    bool phase1 = false;
    bool phase2 = false;
    bool phase3 = false;
    bool hearsay_fix = false;
    bool coinage = false;
    bool run_mode = false;
    bool checkpoint_gate = false;
    bool serve_mode = false;
    int run_n = 1000;
    int p3_n = 600;
    int coin_n = 1000;
    int ckpt_n = 200;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--seed" && i + 1 < argc) {
            seed = std::stoull(argv[++i]);
        }
        if (arg == "--phase1") phase1 = true;
        if (arg == "--phase2") phase2 = true;
        if (arg == "--hearsay_fix") hearsay_fix = true;
        if (arg == "--phase3") { phase3 = true; if (i + 1 < argc && argv[i+1][0] != '-') p3_n = std::stoi(argv[++i]); }
        if (arg == "--run")    { run_mode = true; if (i + 1 < argc && argv[i+1][0] != '-') run_n = std::stoi(argv[++i]); }
        if (arg == "--coinage") { coinage = true; if (i + 1 < argc && argv[i+1][0] != '-') coin_n = std::stoi(argv[++i]); }
        if (arg == "--checkpoint_gate") { checkpoint_gate = true; if (i + 1 < argc && argv[i+1][0] != '-') ckpt_n = std::stoi(argv[++i]); }
        if (arg == "--serve") serve_mode = true;
    }

    // ---- --serve (Step 5b): long-running host mode. Infinite tick loop, periodic checkpoint,
    // SIGUSR1 = on-demand staging backup, SIGTERM = final checkpoint + clean exit. Auto-resumes
    // from EngineCheckpoint if village.db already has one for this run_id; else fresh init.
    // Zero changes inside tick()/cognition path -- world_hash surface is untouched. ----
    if (serve_mode) {
        uint64_t serve_seed = seed;
        if (const char* e = std::getenv("TBV_SEED")) serve_seed = std::stoull(e);
        uint64_t ckpt_every = 500;
        if (const char* e = std::getenv("TBV_CKPT_EVERY")) ckpt_every = std::stoull(e);
        const std::string run_id = "serve";

        std::signal(SIGUSR1, tbv_on_sigusr1);
        std::signal(SIGTERM, tbv_on_sigterm);

        try {
            tbv::LlamaBridge bridge("models/smollm2-360m-instruct-q8_0.gguf");
            tbv::Database db("village.db");
            db.init_schema();

            tbv::WorldState world;
            world.init(serve_seed);
            world.attach(&bridge, &db, run_id);
            bool resumed = db.load_checkpoint(world);
            std::cout << (resumed ? "resumed from checkpoint at tick " : "fresh boot, seed ")
                      << (resumed ? world.current_tick : serve_seed) << "\n" << std::flush;

            std::filesystem::create_directories("staging");

            while (!g_shutdown_requested) {
                // Step 7 fish-tank fulcrum: --serve previously never wrote VillagerState (only
                // --run did), so a spectator polling village.db for positions would see a frozen
                // tank. Additive, observer-only (same idiom as --run's loop) -- does not touch
                // world_hash / determinism. Unbounded growth over long runs is a known tradeoff,
                // not solved here; revisit if/when long (weeks) runs make VillagerState huge.
                db.log_tick_state(world);
                world.tick();

                if (world.current_tick % ckpt_every == 0) db.save_checkpoint(world);

                if (g_backup_requested) {
                    g_backup_requested = 0;
                    db.save_checkpoint(world);
                    bool ok = db.backup_to("staging/backup.db");
                    if (ok) {
                        std::ofstream marker("staging/backup.done", std::ios::trunc);
                        marker << "tick=" << world.current_tick
                               << " engine_version=" TBV_ENGINE_VERSION "\n";
                    } else {
                        std::cout << "backup FAILED at tick " << world.current_tick << "\n" << std::flush;
                    }
                }
            }

            db.save_checkpoint(world);
            std::cout << "shutdown: final checkpoint at tick " << world.current_tick << "\n" << std::flush;
            return 0;
        } catch (const std::exception& e) {
            std::cout << "FAIL: " << e.what() << "\n";
            return 1;
        }
    }

    // ---- Phase 3 driver: wire cognition into the deterministic tick loop, write the artifact DB. ----
    if (run_mode) {
        std::cout << "Village run: seed " << seed << ", " << run_n << " ticks\n";
        try {
            tbv::LlamaBridge bridge("models/smollm2-360m-instruct-q8_0.gguf");
            tbv::Database db("village.db");
            db.init_schema();
            tbv::WorldState world; world.init(seed);
            std::string run_id = "run_" + std::to_string(seed);
            world.attach(&bridge, &db, run_id);
            while (world.current_tick < (uint64_t)run_n) {
                db.log_tick_state(world); // pre-tick snapshot: positions/needs at this tick_id
                world.tick();
                if (world.current_tick % 100 == 0) log_digest_snapshot(db, world, run_id, seed);
            }

            int exp = 0, hear = 0, dream = 0, maxd = 0;
            for (uint32_t i = 0; i < tbv::MAX_VILLAGERS; ++i)
                for (const auto& m : world.stores[i].all()) {
                    if (m.type == tbv::MemType::EXPERIENCE) exp++;
                    else if (m.type == tbv::MemType::HEARSAY) { hear++; if (m.source_depth > maxd) maxd = m.source_depth; }
                    else dream++;
                }
            std::cout << "done. RAM memories: EXPERIENCE=" << exp << " HEARSAY=" << hear
                      << " (max depth " << maxd << ") DREAM=" << dream << "\n";
            std::cout << "world_hash=" << std::hex << world_hash(world) << std::dec << "\n";
            return 0;
        } catch (const std::exception& e) {
            std::cout << "FAIL: " << e.what() << "\n";
            return 1;
        }
    }

    // ---- Coinage gate: a flagged (not-in-dictionary) term must appear in >=2 villagers'
    // memory stores — i.e. it rode hearsay/dream/action text and actually spread. ----
    if (coinage) {
        std::cout << "Coinage Gate: emergent word-coinage (" << coin_n << " ticks)\n";
        try {
            tbv::LlamaBridge bridge("models/smollm2-360m-instruct-q8_0.gguf");
            tbv::Database db(":memory:"); db.init_schema();
            tbv::WorldState world; world.init(seed);
            world.attach(&bridge, &db, "g");
            while (world.current_tick < (uint64_t)coin_n) world.tick();

            std::cout << "coined terms: " << world.coined_words.size() << "\n";
            std::string best_term; int best_spread = 0;
            for (const auto& term : world.coined_words) {
                std::set<tbv::VillagerID> users;
                for (uint32_t i = 0; i < tbv::MAX_VILLAGERS; ++i)
                    for (const auto& m : world.stores[i].all())
                        for (const auto& tok : tbv::tokenize(m.text))
                            if (tok == term) { users.insert(i); break; }
                if ((int)users.size() > best_spread) { best_spread = (int)users.size(); best_term = term; }
            }
            bool spread_ok = best_spread >= 2;
            std::cout << "best spread: \"" << best_term << "\" -> " << best_spread << " villagers\n";
            std::cout << (spread_ok ? "✅ coinage: a coined term spread to >=2 villagers\n"
                                     : "❌ coinage: no term spread to >=2 villagers\n");
            return spread_ok ? 0 : 1;
        } catch (const std::exception& e) {
            std::cout << "FAIL: " << e.what() << "\n";
            return 1;
        }
    }

    // ---- Hearsay-fix gate (MSG_062 §2): memory-degradation + two-segment retell + forced
    // novelty must break stagnation. 5 seeds x 6-hop chains. Asserts (a) hop-6 drifts from the
    // seed in >=2 of {actor,action,location}, (b) EVERY adjacent hop differs (no verbatim freeze
    // — the bug the old endpoints-only Levenshtein missed), (c) same-seed byte-identical. ----
    if (hearsay_fix) {
        std::cout << "Hearsay-Fix Gate: degradation + two-segment retell + forced novelty\n";
        try {
            tbv::LlamaBridge bridge("models/smollm2-360m-instruct-q8_0.gguf");
            const int HOPS = 6;
            const int LEV_MIN = 12;     // min edit distance between adjacent hops (anti-stagnation)
            const std::string seed_text =
                "Borin found a glowing apple near the lake behind Brindlemark.";

            // One degraded telephone-chain. Fills `texts` with HOPS+1 entries (seed + each hop).
            auto chain = [&](uint64_t s, std::vector<std::string>& texts) {
                tbv::MemoryEntry cur;
                cur.mem_id = 0; cur.tick = 0; cur.importance = 800;
                cur.type = tbv::MemType::EXPERIENCE; cur.text = seed_text;
                texts.push_back(cur.text);
                for (int hop = 1; hop <= HOPS; ++hop) {
                    // Deterministic, varied genome per hop -> genome-biased distortion.
                    tbv::Genome g{ (uint8_t)(hop & 3),        (uint8_t)((hop >> 1) & 3),
                                   (uint8_t)((s >> hop) & 3),  (uint8_t)((s >> (hop+2)) & 3),
                                   (uint8_t)(hop % 3) };
                    uint64_t tick = (uint64_t)hop * 60;        // age the memory so degradation bites
                    cur.importance = 800 - 120 * hop; if (cur.importance < 100) cur.importance = 100;
                    tbv::RetellResult rr =
                        tbv::hearsay_hop(bridge, (tbv::VillagerID)hop, cur, g, tick, s + hop);
                    texts.push_back(rr.out_text);
                    cur.mem_id = (uint32_t)hop; cur.tick = tick;
                    cur.type = tbv::MemType::HEARSAY; cur.source_depth = (uint8_t)hop;
                    cur.text = rr.out_text;
                }
            };

            bool all_drift = true, all_adjacent = true;
            uint64_t seeds[5] = { seed, seed + 101, seed + 202, seed + 303, seed + 404 };
            for (int si = 0; si < 5; ++si) {
                std::vector<std::string> texts;
                chain(seeds[si], texts);
                tbv::HearsayFields f0 = tbv::extract_hearsay_fields(texts.front());
                tbv::HearsayFields fN = tbv::extract_hearsay_fields(texts.back());
                int changed = (f0.actor != fN.actor) + (f0.action != fN.action)
                            + (f0.location != fN.location);
                int min_adj = 1 << 30;
                for (size_t i = 1; i < texts.size(); ++i)
                    min_adj = std::min(min_adj, tbv::levenshtein(texts[i-1], texts[i]));
                bool drift = (changed >= 2), adjacent = (min_adj >= LEV_MIN);
                std::cout << "seed " << seeds[si] << ": fields_changed=" << changed
                          << "  min_adjacent_lev=" << min_adj
                          << (adjacent ? "" : "  <-- STAGNATION") << "\n";
                if (!drift) all_drift = false;
                if (!adjacent) all_adjacent = false;
            }

            std::vector<std::string> t1, t2;
            chain(seeds[0], t1); chain(seeds[0], t2);
            bool deterministic = (t1 == t2);

            std::cout << (all_drift ? "✅ drift: hop-6 differs from seed in >=2 fields (all seeds)\n"
                                    : "❌ drift: insufficient field change on some seed\n");
            std::cout << (all_adjacent ? "✅ no stagnation: every adjacent hop edit-distance >= threshold\n"
                                       : "❌ stagnation: a hop pair froze near-verbatim\n");
            std::cout << (deterministic ? "✅ determinism: chain reproduces byte-identical\n"
                                        : "❌ determinism broke\n");
            return (all_drift && all_adjacent && deterministic) ? 0 : 1;
        } catch (const std::exception& e) {
            std::cout << "FAIL: " << e.what() << "\n";
            return 1;
        }
    }

    // ---- Checkpoint gate (Step 4): kill-and-restore must reproduce the uninterrupted run.
    // A runs straight through to N. B runs only to K<N, checkpoints, and is destroyed. C is a
    // FRESH WorldState (init() defaults, no shared memory with B) that loads B's checkpoint and
    // finishes K->N. hash(C) must equal hash(A) — restore must lose nothing live. Also hard-
    // asserts the existing --phase3 200 canonical hash is unperturbed (this instrumentation
    // must not touch the deterministic core) and that the 4 always-firing MSG_062 tables
    // (HearsayChain/CognitionLog/BeliefSurvival/WorldDigest) have rows. CoinageSpread is excluded
    // from the non-empty assert: word adoption is real-text-dependent and can legitimately be 0
    // in a short/seeded run. ----
    if (checkpoint_gate) {
        const int N = ckpt_n, K = 80;
        std::cout << "Checkpoint Gate: kill-and-restore (" << N << " ticks, checkpoint@" << K << ")\n";
        try {
            tbv::LlamaBridge bridge("models/smollm2-360m-instruct-q8_0.gguf");

            tbv::Database db_a(":memory:"); db_a.init_schema();
            tbv::WorldState a; a.init(seed); a.attach(&bridge, &db_a, "ckg");
            while (a.current_tick < (uint64_t)N) a.tick();
            uint64_t hash_a = world_hash(a);
            log_digest_snapshot(db_a, a, "ckg", seed); // proves WorldDigest end-to-end

            tbv::Database db_b(":memory:"); db_b.init_schema();
            {
                tbv::WorldState b; b.init(seed); b.attach(&bridge, &db_b, "ckg");
                while (b.current_tick < (uint64_t)K) b.tick();
                db_b.save_checkpoint(b);
                // b is destroyed here (end of scope) — C must restore with zero shared memory.
            }

            tbv::WorldState c; c.init(seed); c.attach(&bridge, &db_b, "ckg");
            bool loaded = db_b.load_checkpoint(c);
            if (!loaded) { std::cout << "FAIL: load_checkpoint found no row for run_id\n"; return 1; }
            while (c.current_tick < (uint64_t)N) c.tick();
            uint64_t hash_c = world_hash(c);

            bool restore_ok = (hash_a == hash_c);
            std::cout << "hash_uninterrupted=" << std::hex << hash_a
                      << "  hash_restored=" << hash_c << std::dec << "\n";
            std::cout << (restore_ok ? "✅ restore: kill-and-restore reproduces the uninterrupted run\n"
                                      : "❌ restore: checkpoint/restore diverged from ground truth\n");

            // Regression guard: this instrumentation must not perturb the deterministic core.
            tbv::Database db_r(":memory:"); db_r.init_schema();
            tbv::WorldState r; r.init(12345); r.attach(&bridge, &db_r, "g");
            while (r.current_tick < 200) r.tick();
            uint64_t canonical = 0x567898a5104b509dULL;
            bool canon_ok = (world_hash(r) == canonical);
            std::cout << "canonical --phase3 200 hash @seed 12345: " << std::hex << world_hash(r)
                      << std::dec << (canon_ok ? "  ✅ unchanged\n" : "  ❌ DRIFTED\n");

            int n_hearsay = db_b.count_rows("HearsayChain", "ckg");
            int n_cog     = db_b.count_rows("CognitionLog", "ckg");
            int n_belief  = db_b.count_rows("BeliefSurvival", "ckg");
            int n_coinage = db_b.count_rows("CoinageSpread", "ckg");
            int n_digest  = db_a.count_rows("WorldDigest", "ckg");
            bool tables_ok = (n_hearsay > 0 && n_cog > 0 && n_belief > 0 && n_digest > 0);
            std::cout << "tables: HearsayChain=" << n_hearsay << " CognitionLog=" << n_cog
                      << " BeliefSurvival=" << n_belief << " CoinageSpread=" << n_coinage
                      << " WorldDigest=" << n_digest << "\n";
            std::cout << (tables_ok ? "✅ tables: all 4 always-firing MSG_062 tables non-empty\n"
                                     : "❌ tables: one or more MSG_062 tables empty\n");

            return (restore_ok && canon_ok && tables_ok) ? 0 : 1;
        } catch (const std::exception& e) {
            std::cout << "FAIL: " << e.what() << "\n";
            return 1;
        }
    }

    // ---- Phase 3 gate: same-seed determinism + diff-seed differential + mythology emergence. ----
    if (phase3) {
        std::cout << "Phase 3 Gate: orchestration (" << p3_n << " ticks/run)\n";
        try {
            tbv::LlamaBridge bridge("models/smollm2-360m-instruct-q8_0.gguf"); // reused across runs
            auto run = [&](uint64_t s) {
                tbv::Database db(":memory:"); db.init_schema();
                tbv::WorldState w; w.init(s); w.attach(&bridge, &db, "g");
                while (w.current_tick < (uint64_t)p3_n) w.tick();
                return w;
            };
            tbv::WorldState a = run(seed);
            uint64_t ha = world_hash(a);
            tbv::WorldState b = run(seed);
            uint64_t hb = world_hash(b);
            tbv::WorldState c = run(seed + 777);
            uint64_t hc = world_hash(c);

            int dream = 0, maxd = 0;
            for (uint32_t i = 0; i < tbv::MAX_VILLAGERS; ++i)
                for (const auto& m : a.stores[i].all()) {
                    if (m.type == tbv::MemType::DREAM) dream++;
                    if (m.type == tbv::MemType::HEARSAY && m.source_depth > maxd) maxd = m.source_depth;
                }

            bool det = (ha == hb), diff = (ha != hc), emergence = (maxd >= 2 && dream >= 1);
            std::cout << "hash(seed)=" << std::hex << ha << "  rerun=" << hb << "  diff-seed=" << hc << std::dec << "\n";
            std::cout << "emergence: max hearsay depth=" << maxd << ", dreams=" << dream << "\n";
            std::cout << (det ? "✅ determinism: same-seed byte-identical\n" : "❌ determinism broke\n");
            std::cout << (diff ? "✅ differential: diff-seed diverges\n" : "❌ differential failed (vacuous gate)\n");
            std::cout << (emergence ? "✅ emergence: hearsay depth>=2 + dream exist\n" : "❌ emergence: pipeline did not fire\n");
            return (det && diff && emergence) ? 0 : 1;
        } catch (const std::exception& e) {
            std::cout << "FAIL: " << e.what() << "\n";
            return 1;
        }
    }

    if (phase2) {
        // Sequential hearsay propagation gate: seed v0, chain v0->v1->v2->v3 retelling
        // each prior villager's most-salient memory into own store. Strict sequential
        // (Gemini concurrency ruling: one context, KV wiped per retell in the bridge).
        std::cout << "Phase 2 Gate: hearsay propagation (sequential)\n";
        try {
            tbv::LlamaBridge bridge("models/smollm2-360m-instruct-q8_0.gguf");
            auto run_chain = [&](uint64_t s) {
                const int N = 4;
                std::vector<tbv::CognitiveStore> villagers(N);
                tbv::MemoryEntry seed_mem;
                seed_mem.mem_id = 0; seed_mem.importance = 1000; seed_mem.type = tbv::MemType::EXPERIENCE;
                seed_mem.text = "The village of Brindlemark is magical, with glowing trees and sparkling lakes behind the houses.";
                villagers[0].add(seed_mem);
                std::string final_text = seed_mem.text;
                for (int v = 1; v < N; ++v) {
                    const tbv::MemoryEntry* src = villagers[v - 1].most_salient();
                    std::string heard = bridge.retell((tbv::VillagerID)v, src->text, s + v).out_text;
                    tbv::MemoryEntry h;
                    h.mem_id = (uint32_t)v; h.type = tbv::MemType::HEARSAY;
                    h.source_depth = (uint8_t)(src->source_depth + 1);
                    h.importance = src->importance - 150;   // engine-derived: gossip fades per hop
                    h.text = heard;
                    villagers[v].add(h);
                    final_text = heard;
                }
                return final_text;
            };
            std::string seed_text = "The village of Brindlemark is magical, with glowing trees and sparkling lakes behind the houses.";
            std::string final1 = run_chain(seed);
            double drift = jaccard(tok_set(seed_text), tok_set(final1));
            bool anti_collapse = final1.size() > 60;            // not degenerate/empty
            std::string final2 = run_chain(seed);               // determinism re-run
            bool deterministic = (final1 == final2);
            std::cout << "drift J(seed,hop3)=" << drift << "  final_len=" << final1.size() << "\n";
            std::cout << (drift < 0.3 ? "✅ drift: real mutation\n" : "❌ drift: too little\n");
            std::cout << (anti_collapse ? "✅ anti-collapse: coherent narrative survives\n" : "❌ collapsed to noise\n");
            std::cout << (deterministic ? "✅ determinism: chain reproduces byte-identical\n" : "❌ determinism broke\n");

            // DREAM: recombine fragments (seed + last hop) into a surreal dream; check determinism.
            std::vector<std::string> frags = { seed_text, final1 };
            std::string d1 = bridge.dream(3, frags, seed + 99);
            std::string d2 = bridge.dream(3, frags, seed + 99);
            bool dream_ok = (d1.size() > 60) && (d1 == d2);
            std::cout << "DREAM len=" << d1.size() << ": " << d1.substr(0, 110) << "...\n";
            std::cout << (dream_ok ? "✅ dream: coherent + deterministic\n" : "❌ dream: degenerate or non-deterministic\n");

            // Append-only MemoryGraph persistence round-trip (in-memory DB, observer interface).
            tbv::Database mdb(":memory:");
            mdb.init_schema();
            std::string run_id = "run_" + std::to_string(seed);
            int to_write = 4;
            for (int v = 0; v < to_write; ++v) {
                tbv::MemoryEntry e;
                e.mem_id = (uint32_t)v; e.tick = (uint64_t)v; e.importance = 1000 - v * 150;
                e.type = (v == 0) ? tbv::MemType::EXPERIENCE : tbv::MemType::HEARSAY;
                e.source_depth = (uint8_t)v; e.text = (v == 0) ? seed_text : final1;
                mdb.persist_memory(run_id, (tbv::VillagerID)v, e);
            }
            int got = mdb.count_memories(run_id);
            bool persist_ok = (got == to_write);
            std::cout << "persist: wrote " << to_write << " read " << got << "\n";
            std::cout << (persist_ok ? "✅ persistence: MemoryGraph round-trip\n" : "❌ persistence failed\n");
            return (drift < 0.3 && anti_collapse && deterministic && dream_ok && persist_ok) ? 0 : 1;
        } catch (const std::exception& e) {
            std::cout << "❌ FAIL: " << e.what() << "\n";
            return 1;
        }
    }

    if (phase1) {
        std::cout << "Phase 1 Re-Cut Gate: hybrid cognition (free thought -> intent -> resolver)\n";
        try {
            tbv::LlamaBridge bridge("models/smollm2-360m-instruct-q8_0.gguf");
            auto vectors = phase1_vectors();
            bool all_wellformed = true;
            tbv::Cognition v0_first;
            for (size_t i = 0; i < vectors.size(); ++i) {
                tbv::Cognition c = bridge.infer((tbv::VillagerID)i, vectors[i], seed + i);
                if (i == 0) v0_first = c;
                std::cout << "[V" << i << "] verb=" << tbv::verb_name(c.verb)
                          << " (" << c.source << ")  intent=\"I " << c.intent.substr(0, 60) << "...\"\n";
                if (c.verb == tbv::Verb::NONE) all_wellformed = false; // resolver always yields a real verb
            }
            // Determinism: re-run vector 0 at the same seed, thought+intent+verb must reproduce.
            tbv::Cognition again = bridge.infer(0, vectors[0], seed + 0);
            bool deterministic = (again.verb == v0_first.verb && again.intent == v0_first.intent
                                  && again.thought == v0_first.thought);
            std::cout << (all_wellformed ? "✅ extractor well-formed (every cycle -> a valid engine verb)\n"
                                         : "❌ a cycle produced NONE\n");
            std::cout << (deterministic ? "✅ determinism: vector 0 reproduces byte-identical\n"
                                        : "❌ determinism broke\n");
            return (all_wellformed && deterministic) ? 0 : 1;
        } catch (const std::exception& e) {
            std::cout << "❌ FAIL: " << e.what() << "\n";
            return 1;
        }
    }

    std::cout << "Starting 10-Bit Village Engine Phase 0 (Seed: " << seed << ")\n";

    tbv::Database db("snapshot.db");
    db.init_schema();

    tbv::WorldState world;
    world.init(seed);

    // Run to exactly tick 1000
    while (world.current_tick < 1000) {
        world.tick();
        db.log_tick_state(world);
    }

    std::cout << "Reached tick 1000. Shutting down cleanly.\n";
    return 0;
}
