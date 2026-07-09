#pragma once
// Per-villager free-text memory store with salience eviction (Phase 2).
// Free prose (EXPERIENCE/HEARSAY/DREAM) is variable-length, so it lives in std::string
// here (full text persisted to SQLite MemoryGraph separately). Deterministic throughout.
#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include "core/types.h"

namespace tbv {

struct MemoryEntry {
    uint32_t mem_id       = 0;    // monotonic per run
    uint64_t tick         = 0;
    uint32_t actor_id     = 0;    // who the memory is about / its source villager
    int32_t  importance   = 0;    // engine-derived salience
    MemType  type         = MemType::EXPERIENCE;
    uint8_t  source_depth = 0;    // hearsay hop count (0 = firsthand)
    uint32_t origin_mem_id = 0;   // belief-lineage root: self for EXPERIENCE/DREAM,
                                  // inherited from the source memory for HEARSAY (Step 4)
    std::string text;             // the free prose
    uint16_t retell_count  = 0;   // §B3 (Run 2 Plan) — bumped each time this memory is
                                  // selected as the source of a hearsay hop; must survive
                                  // checkpoint save/restore (db.cpp CheckpointMemory).
};

// §B3 — the calcification dial. Too low: Run 1's winner-take-all monoculture persists.
// Too high: no myth ever stabilizes long enough to calcify, killing the thesis's "shared
// mythology" column. QA-approved (packet 102 item 1): observed importance band is
// 1300-1700 with age-decay ~age/10, so 150/retell ~= 5 retells before a memory rotates
// out of contention — enough pressure to let young lineages compete without erasing
// genuinely important ones. Not pre-blessed as final -- A1 metric 8 (myth-persistence)
// is the guardrail; if it drops as diversity improves, this is overtuned.
constexpr int32_t RETELL_PENALTY = 150;

// FNV-1a 64-bit over free text. Used as BeliefSurvival's content_hash (Step 4) —
// deterministic, cheap, no collision-resistance requirement (observational only).
inline uint64_t fnv64_text(const std::string& s) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}

class CognitiveStore {
public:
    explicit CognitiveStore(size_t cap = 500) : cap_(cap) {}

    // Returns the evicted entry (if eviction happened) so callers can mark its death
    // (BeliefSurvival, Step 4). Previously discarded silently.
    std::optional<MemoryEntry> add(MemoryEntry e, uint64_t current_tick = 0) {
        mems_.push_back(std::move(e));
        if (mems_.size() > cap_) return evict_lowest(current_tick);
        return std::nullopt;
    }

    // Checkpoint restore (Step 4): replace contents verbatim, preserving exact insertion
    // order — dream() reads from the BACK of the vector (most recent insertion), not sorted
    // by importance/tick/mem_id, so restore must match that order exactly via the caller's
    // explicit seq ordering (CheckpointMemory.seq ASC).
    void load_raw(std::vector<MemoryEntry> entries) {
        mems_ = std::move(entries);
    }

    // Lazy time-decay (QA [046]): computed at access, never mutated per-tick (avoids O(N) churn).
    // §B3: retell fatigue stacks on top of age-decay — a memory retold often rotates out of
    // most_salient() contention sooner, even if still fairly fresh. Interplay note: this also
    // pushes memories below DEGRADE_THRESHOLD sooner, compounding with proper-noun masking
    // (degradation.h) -- accounted for when tuning RETELL_PENALTY.
    static int32_t effective_importance(const MemoryEntry& m, uint64_t current_tick) {
        uint64_t age = (current_tick > m.tick) ? (current_tick - m.tick) : 0;
        return m.importance - (int32_t)(age / 10) - (int32_t)m.retell_count * RETELL_PENALTY;
    }

    // Highest effective importance; tie-break = NEWEST (highest mem_id) so villagers retell
    // fresh gossip (QA-proposed MSG[043], Builder-applied). Deterministic.
    const MemoryEntry* most_salient(uint64_t current_tick = 0) const {
        if (mems_.empty()) return nullptr;
        const MemoryEntry* best = &mems_[0];
        int32_t best_eff = effective_importance(*best, current_tick);
        for (const auto& m : mems_) {
            int32_t eff = effective_importance(m, current_tick);
            if (eff > best_eff || (eff == best_eff && m.mem_id > best->mem_id)) {
                best = &m; best_eff = eff;
            }
        }
        return best;
    }

    size_t size() const { return mems_.size(); }
    const std::vector<MemoryEntry>& all() const { return mems_; }

    // §B3 — bump retell_count on the memory a hearsay hop just read from. Engine dispatch
    // is strictly sequential (LOCK 1), so this is deterministic. Finds-and-mutates an
    // EXISTING element in place (never push_back/erase) -- a `const MemoryEntry*` obtained
    // from most_salient() just before calling this stays valid, since no reallocation occurs.
    void note_retold(uint32_t mem_id) {
        for (auto& m : mems_) {
            if (m.mem_id == mem_id) { m.retell_count++; return; }
        }
    }

private:
    // Evict lowest EFFECTIVE importance (decay-aware, consistent with most_salient — QA M3);
    // tie-break = lowest mem_id (forget oldest trivia first). Deterministic.
    MemoryEntry evict_lowest(uint64_t current_tick) {
        size_t idx = 0;
        int32_t lo = effective_importance(mems_[0], current_tick);
        for (size_t i = 1; i < mems_.size(); ++i) {
            int32_t eff = effective_importance(mems_[i], current_tick);
            if (eff < lo || (eff == lo && mems_[i].mem_id < mems_[idx].mem_id)) {
                idx = i; lo = eff;
            }
        }
        MemoryEntry evicted = std::move(mems_[idx]);
        mems_.erase(mems_.begin() + idx);
        return evicted;
    }

    size_t cap_;
    std::vector<MemoryEntry> mems_;
};

} // namespace tbv
