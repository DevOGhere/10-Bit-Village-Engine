#include <iostream>
#include <string>
#include <cstdlib>
#include <vector>
#include "engine/world.h"
#include "infra/db.h"
#include "cognition/llama_bridge.h"
#include "cognition/coinage.h"

#include "cognition/resolver.h"
#include "cognition/cognitive_store.h"
#include <set>
#include <sstream>

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
    bool coinage = false;
    bool run_mode = false;
    int run_n = 1000;
    int p3_n = 600;
    int coin_n = 1000;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--seed" && i + 1 < argc) {
            seed = std::stoull(argv[++i]);
        }
        if (arg == "--phase1") phase1 = true;
        if (arg == "--phase2") phase2 = true;
        if (arg == "--phase3") { phase3 = true; if (i + 1 < argc && argv[i+1][0] != '-') p3_n = std::stoi(argv[++i]); }
        if (arg == "--run")    { run_mode = true; if (i + 1 < argc && argv[i+1][0] != '-') run_n = std::stoi(argv[++i]); }
        if (arg == "--coinage") { coinage = true; if (i + 1 < argc && argv[i+1][0] != '-') coin_n = std::stoi(argv[++i]); }
    }

    // ---- Phase 3 driver: wire cognition into the deterministic tick loop, write the artifact DB. ----
    if (run_mode) {
        std::cout << "Village run: seed " << seed << ", " << run_n << " ticks\n";
        try {
            tbv::LlamaBridge bridge("models/smollm2-360m-instruct-q8_0.gguf");
            tbv::Database db("village.db");
            db.init_schema();
            tbv::WorldState world; world.init(seed);
            world.attach(&bridge, &db, "run_" + std::to_string(seed));
            while (world.current_tick < (uint64_t)run_n) {
                db.log_tick_state(world); // pre-tick snapshot: positions/needs at this tick_id
                world.tick();
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
                    std::string heard = bridge.retell((tbv::VillagerID)v, src->text, s + v);
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
