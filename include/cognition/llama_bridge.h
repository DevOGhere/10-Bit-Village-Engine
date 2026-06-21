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

class LlamaBridge {
public:
    explicit LlamaBridge(const std::string& model_path);
    ~LlamaBridge();
    LlamaBridge(const LlamaBridge&) = delete;
    LlamaBridge& operator=(const LlamaBridge&) = delete;

    // Hybrid cognition: free thought -> free intent -> deterministic resolver.
    Cognition infer(VillagerID id, const PerceptionContext& ctx, uint64_t seed);

    // Phase 2: retell a heard memory through this villager's mind (free, distorting).
    std::string retell(VillagerID id, const std::string& heard, uint64_t seed);

    // Phase 2: recombine memory fragments into a surreal DREAM (free).
    std::string dream(VillagerID id, const std::vector<std::string>& fragments, uint64_t seed);

private:
    llama_model* model = nullptr;
    llama_context* ctx = nullptr;
    const llama_vocab* vocab = nullptr;
    int n_thought_cap = 160;
};

} // namespace tbv
