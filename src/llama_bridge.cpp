#include "cognition/llama_bridge.h"
#include <llama.h>
#include <vector>
#include <stdexcept>

namespace tbv {

LlamaBridge::LlamaBridge(const std::string& model_path) {
    llama_backend_init();
    llama_model_params mparams = llama_model_default_params();
    model = llama_model_load_from_file(model_path.c_str(), mparams);
    if (!model) throw std::runtime_error("Failed to load llama model");
    vocab = llama_model_get_vocab(model);

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx = 2048;          // re-sized: ~160-tok thought + intent fits; revisit for concurrency
    cparams.n_threads = 1;
    cparams.n_threads_batch = 1;
    ctx = llama_init_from_model(model, cparams);
    if (!ctx) throw std::runtime_error("Failed to create llama context");
}

LlamaBridge::~LlamaBridge() {
    if (ctx) llama_free(ctx);
    if (model) llama_model_free(model);
    llama_backend_free();
}

// Decode a raw string into the active context (appends, no KV reset).
static void decode_str(llama_context* ctx, const llama_vocab* vocab, llama_batch& b,
                       int32_t& pos, const std::string& s, bool add_bos) {
    std::vector<llama_token> t(s.size() + 16);
    int n = llama_tokenize(vocab, s.c_str(), s.length(), t.data(), t.size(), add_bos, true);
    if (n < 0) { t.resize(-n); n = llama_tokenize(vocab, s.c_str(), s.length(), t.data(), t.size(), add_bos, true); }
    t.resize(n);
    b.n_tokens = 0;
    for (int i = 0; i < n; ++i) {
        b.token[b.n_tokens] = t[i]; b.pos[b.n_tokens] = pos++;
        b.n_seq_id[b.n_tokens] = 1; b.seq_id[b.n_tokens][0] = 0;
        b.logits[b.n_tokens] = (i == n - 1); b.n_tokens++;
    }
    llama_decode(ctx, b);
}

// Free (un-grammared) generation segment. Returns text.
static std::string gen(llama_context* ctx, const llama_vocab* vocab, llama_batch& b,
                       int32_t& pos, llama_sampler* chain, int max_tokens) {
    std::string out; int n_vocab = llama_vocab_n_tokens(vocab);
    for (int i = 0; i < max_tokens; ++i) {
        float* lg = llama_get_logits_ith(ctx, b.n_tokens - 1);
        if (!lg) break;
        std::vector<llama_token_data> cd; cd.reserve(n_vocab);
        for (llama_token t = 0; t < n_vocab; ++t) cd.push_back(llama_token_data{t, lg[t], 0.0f});
        llama_token_data_array cp = { cd.data(), cd.size(), -1, false };
        llama_sampler_apply(chain, &cp);
        llama_token nt = (cp.selected != -1) ? cp.data[cp.selected].id : cp.data[0].id;
        llama_sampler_accept(chain, nt);
        if (llama_vocab_is_eog(vocab, nt)) break;
        char buf[128]; int nc = llama_token_to_piece(vocab, nt, buf, sizeof(buf), 0, true);
        if (nc > 0) out.append(buf, nc);
        b.n_tokens = 0; b.token[0] = nt; b.pos[0] = pos++;
        b.n_seq_id[0] = 1; b.seq_id[0][0] = 0; b.logits[0] = true; b.n_tokens = 1;
        if (llama_decode(ctx, b) != 0) break;
    }
    return out;
}

Cognition LlamaBridge::infer(VillagerID id, const PerceptionContext& pc, uint64_t seed) {
    (void)id;
    // ChatML-wrap the grounded situation.
    llama_chat_message msg[1]; msg[0].role = "user"; msg[0].content = pc.situation.c_str();
    const char* tmpl = llama_model_chat_template(model, nullptr);
    std::vector<char> tp(pc.situation.size() + 1024);
    int r = llama_chat_apply_template(tmpl, msg, 1, true, tp.data(), tp.size());
    if (r > (int)tp.size()) { tp.resize(r); r = llama_chat_apply_template(tmpl, msg, 1, true, tp.data(), tp.size()); }
    std::string prompt = (r > 0) ? std::string(tp.data()) : pc.situation;

    // Fresh context per call keeps determinism + isolates KV (Phase 1).
    llama_memory_t mem = llama_get_memory(ctx);
    llama_memory_seq_rm(mem, 0, -1, -1);

    llama_batch b = llama_batch_init(2048, 0, 1);
    int32_t pos = 0;
    decode_str(ctx, vocab, b, pos, prompt, true);

    llama_sampler_chain_params sp = llama_sampler_chain_default_params();
    llama_sampler* chain = llama_sampler_chain_init(sp);
    llama_sampler_chain_add(chain, llama_sampler_init_penalties(200, 1.15f, 0.0f, 0.0f));
    llama_sampler_chain_add(chain, llama_sampler_init_top_k(50));
    llama_sampler_chain_add(chain, llama_sampler_init_top_p(0.9, 1));
    llama_sampler_chain_add(chain, llama_sampler_init_temp(0.8));
    llama_sampler_chain_add(chain, llama_sampler_init_dist(seed));

    // Segment 1: free thought.
    std::string thought = gen(ctx, vocab, b, pos, chain, n_thought_cap);
    // Free intent micro-gen (fresh user turn, same context).
    decode_str(ctx, vocab, b, pos,
        "<|im_end|>\n<|im_start|>user\nIn one short sentence, what do you physically do right now?<|im_end|>\n<|im_start|>assistant\nI ", false);
    std::string intent = gen(ctx, vocab, b, pos, chain, 28);

    llama_sampler_free(chain);
    llama_batch_free(b);
    llama_memory_seq_rm(mem, 0, -1, -1);

    Resolved res = resolve(intent, pc.needs, pc.env);
    return { thought, intent, res.verb, res.source };
}

std::string LlamaBridge::retell(VillagerID id, const std::string& heard, uint64_t seed) {
    (void)id;
    std::string user = "You overhear someone in the village saying: \"" + heard +
        "\" In your own words, what do you make of it and pass along to others?";
    llama_chat_message msg[1]; msg[0].role = "user"; msg[0].content = user.c_str();
    const char* tmpl = llama_model_chat_template(model, nullptr);
    std::vector<char> tp(user.size() + 1024);
    int r = llama_chat_apply_template(tmpl, msg, 1, true, tp.data(), tp.size());
    if (r > (int)tp.size()) { tp.resize(r); r = llama_chat_apply_template(tmpl, msg, 1, true, tp.data(), tp.size()); }
    std::string prompt = (r > 0) ? std::string(tp.data()) : user;

    llama_memory_t mem = llama_get_memory(ctx);
    llama_memory_seq_rm(mem, 0, -1, -1);
    llama_batch b = llama_batch_init(2048, 0, 1);
    int32_t pos = 0;
    decode_str(ctx, vocab, b, pos, prompt, true);

    llama_sampler_chain_params sp = llama_sampler_chain_default_params();
    llama_sampler* chain = llama_sampler_chain_init(sp);
    llama_sampler_chain_add(chain, llama_sampler_init_penalties(200, 1.15f, 0.0f, 0.0f));
    llama_sampler_chain_add(chain, llama_sampler_init_top_k(50));
    llama_sampler_chain_add(chain, llama_sampler_init_top_p(0.9, 1));
    llama_sampler_chain_add(chain, llama_sampler_init_temp(0.8));
    llama_sampler_chain_add(chain, llama_sampler_init_dist(seed));

    std::string out = gen(ctx, vocab, b, pos, chain, 90);
    llama_sampler_free(chain);
    llama_batch_free(b);
    llama_memory_seq_rm(mem, 0, -1, -1);
    return out;
}

std::string LlamaBridge::dream(VillagerID id, const std::vector<std::string>& fragments, uint64_t seed) {
    (void)id;
    std::string frag;
    for (size_t i = 0; i < fragments.size(); ++i) frag += "\n- \"" + fragments[i] + "\"";
    std::string user = "You drift into sleep. Fragments of your memories swirl together:" + frag +
        "\nDescribe the strange, surreal dream that forms from them.";
    llama_chat_message msg[1]; msg[0].role = "user"; msg[0].content = user.c_str();
    const char* tmpl = llama_model_chat_template(model, nullptr);
    std::vector<char> tp(user.size() + 1024);
    int r = llama_chat_apply_template(tmpl, msg, 1, true, tp.data(), tp.size());
    if (r > (int)tp.size()) { tp.resize(r); r = llama_chat_apply_template(tmpl, msg, 1, true, tp.data(), tp.size()); }
    std::string prompt = (r > 0) ? std::string(tp.data()) : user;

    llama_memory_t mem = llama_get_memory(ctx);
    llama_memory_seq_rm(mem, 0, -1, -1);
    llama_batch b = llama_batch_init(2048, 0, 1);
    int32_t pos = 0;
    decode_str(ctx, vocab, b, pos, prompt, true);

    llama_sampler_chain_params sp = llama_sampler_chain_default_params();
    llama_sampler* chain = llama_sampler_chain_init(sp);
    llama_sampler_chain_add(chain, llama_sampler_init_penalties(200, 1.15f, 0.0f, 0.0f));
    llama_sampler_chain_add(chain, llama_sampler_init_top_k(50));
    llama_sampler_chain_add(chain, llama_sampler_init_top_p(0.9, 1));
    llama_sampler_chain_add(chain, llama_sampler_init_temp(0.9)); // hotter -> more surreal
    llama_sampler_chain_add(chain, llama_sampler_init_dist(seed));

    std::string out = gen(ctx, vocab, b, pos, chain, 100);
    llama_sampler_free(chain);
    llama_batch_free(b);
    llama_memory_seq_rm(mem, 0, -1, -1);
    return out;
}

} // namespace tbv
