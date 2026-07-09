#pragma once
#include <string>
#include <vector>
#include "engine/world.h"        // VillagerID
#include "cognition/resolver.h"  // PerceptionContext, Resolved, Verb

struct llama_model;
struct llama_context;
struct llama_vocab;

namespace tbv {

// Result of one villager cognition cycle.
struct Cognition {
    std::string thought;   // free Segment-1 inner monologue (-> EXPERIENCE memory)
    std::string intent;    // free natural-language action intent
    Verb verb;             // resolved engine action
    const char* source;    // FLOOR | LLM | URGE
};

// Result of a hearsay retelling (MSG_062 §2 two-segment).
// out_text = Segment-1 free reconstruction; fields = Segment-2 structured extraction.
// distortion_injected is set by the forced-novelty fallback (in hearsay_hop, not here).
struct RetellResult {
    std::string out_text;
    HearsayFields fields;
    bool distortion_injected = false;
};

class LlamaBridge {
public:
    explicit LlamaBridge(const std::string& model_path);
    ~LlamaBridge();
    LlamaBridge(const LlamaBridge&) = delete;
    LlamaBridge& operator=(const LlamaBridge&) = delete;

    // Hybrid cognition: free thought -> free intent -> deterministic resolver.
    Cognition infer(VillagerID id, const PerceptionContext& ctx, uint64_t seed);

    // Phase 2 / MSG_062 §2: retell a heard (pre-degraded) memory through this villager's
    // mind — free reconstruction (Segment 1) + structured field extraction (Segment 2).
    // `persona` (Run 2 Plan §B2, e.g. genome::trait_prose) prepends the same genome-flavour
    // prose the ACTION path always had -- Run 1 had zero persona reaching retell/dream, so
    // every villager retold rumours in one anonymous voice.
    RetellResult retell(VillagerID id, const std::string& heard, const std::string& persona, uint64_t seed);

    // Phase 2: recombine memory fragments into a surreal DREAM (free). `persona` — see retell().
    std::string dream(VillagerID id, const std::vector<std::string>& fragments,
                       const std::string& persona, uint64_t seed);

private:
    llama_model* model = nullptr;
    llama_context* ctx = nullptr;
    const llama_vocab* vocab = nullptr;
    int n_thought_cap = 160;
};

} // namespace tbv
