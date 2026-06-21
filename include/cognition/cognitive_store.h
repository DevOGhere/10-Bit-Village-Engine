#pragma once
// Per-villager free-text memory store with salience eviction (Phase 2).
// Free prose (EXPERIENCE/HEARSAY/DREAM) is variable-length, so it lives in std::string
// here (full text persisted to SQLite MemoryGraph separately). Deterministic throughout.
#include <string>
#include <vector>
#include <cstdint>
#include "core/types.h"

namespace tbv {

struct MemoryEntry {
    uint32_t mem_id      = 0;     // monotonic per run
    uint64_t tick        = 0;
    uint32_t actor_id    = 0;     // who the memory is about / its source villager
    int32_t  importance  = 0;     // engine-derived salience
    MemType  type        = MemType::EXPERIENCE;
    uint8_t  source_depth = 0;    // hearsay hop count (0 = firsthand)
    std::string text;             // the free prose
};

class CognitiveStore {
public:
    explicit CognitiveStore(size_t cap = 500) : cap_(cap) {}

    void add(MemoryEntry e, uint64_t current_tick = 0) {
        mems_.push_back(std::move(e));
        if (mems_.size() > cap_) evict_lowest(current_tick);
    }

    // Lazy time-decay (QA [046]): computed at access, never mutated per-tick (avoids O(N) churn).
    static int32_t effective_importance(const MemoryEntry& m, uint64_t current_tick) {
        uint64_t age = (current_tick > m.tick) ? (current_tick - m.tick) : 0;
        return m.importance - (int32_t)(age / 10);
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

private:
    // Evict lowest EFFECTIVE importance (decay-aware, consistent with most_salient — QA M3);
    // tie-break = lowest mem_id (forget oldest trivia first). Deterministic.
    void evict_lowest(uint64_t current_tick) {
        size_t idx = 0;
        int32_t lo = effective_importance(mems_[0], current_tick);
        for (size_t i = 1; i < mems_.size(); ++i) {
            int32_t eff = effective_importance(mems_[i], current_tick);
            if (eff < lo || (eff == lo && mems_[i].mem_id < mems_[idx].mem_id)) {
                idx = i; lo = eff;
            }
        }
        mems_.erase(mems_.begin() + idx);
    }

    size_t cap_;
    std::vector<MemoryEntry> mems_;
};

} // namespace tbv
