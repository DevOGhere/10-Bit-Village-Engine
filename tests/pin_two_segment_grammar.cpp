// pin_b.cpp — empirical pin for Cognition Re-Cut open-item #1:
// Can we run an UNGRAMMARED Segment 1 (free thought), then attach a grammar
// sampler for Segment 2 (action enum) WITHIN THE SAME KV CONTEXT, without
// corrupting state — and is Segment 2 (a) enum-constrained, (b) conditioned on
// Segment 1, (c) deterministic?
#include <llama.h>
#include <string>
#include <vector>
#include <iostream>
#include <cstring>

static const char* ACTION_GRAMMAR =
    "root ::= (\"EAT\" | \"MOVE_TO\" | \"GIVE\" | \"SPEAK\" | \"WAIT\")";

struct Sampled { std::string text; std::vector<llama_token> toks; };

// generic generation segment. grmr may be nullptr (free) or a grammar sampler.
static Sampled gen_segment(llama_context* ctx, const llama_vocab* vocab,
                           llama_batch& batch, int32_t& pos,
                           llama_sampler* grmr, llama_sampler* chain,
                           int max_tokens) {
    Sampled out;
    int n_vocab = llama_vocab_n_tokens(vocab);
    for (int i = 0; i < max_tokens; ++i) {
        float* logits = llama_get_logits_ith(ctx, batch.n_tokens - 1);
        if (!logits) break;
        std::vector<llama_token_data> cand; cand.reserve(n_vocab);
        for (llama_token t = 0; t < n_vocab; ++t)
            cand.push_back(llama_token_data{t, logits[t], 0.0f});
        llama_token_data_array cp = { cand.data(), cand.size(), -1, false };
        if (grmr) llama_sampler_apply(grmr, &cp);   // mask FIRST (only Segment 2)
        llama_sampler_apply(chain, &cp);
        llama_token nt = (cp.selected != -1) ? cp.data[cp.selected].id : cp.data[0].id;
        if (grmr) llama_sampler_accept(grmr, nt);
        llama_sampler_accept(chain, nt);
        if (llama_vocab_is_eog(vocab, nt)) break;
        char buf[128];
        int n = llama_token_to_piece(vocab, nt, buf, sizeof(buf), 0, true);
        if (n > 0) out.text.append(buf, n);
        out.toks.push_back(nt);
        batch.n_tokens = 0;
        batch.token[0] = nt; batch.pos[0] = pos++;
        batch.n_seq_id[0] = 1; batch.seq_id[0][0] = 0; batch.logits[0] = true;
        batch.n_tokens = 1;
        if (llama_decode(ctx, batch) != 0) break;
    }
    return out;
}

static void decode_str(llama_context* ctx, const llama_vocab* vocab,
                       llama_batch& batch, int32_t& pos, const std::string& s,
                       bool add_bos) {
    std::vector<llama_token> toks(s.size() + 16);
    int n = llama_tokenize(vocab, s.c_str(), s.length(), toks.data(), toks.size(), add_bos, true);
    if (n < 0) { toks.resize(-n); n = llama_tokenize(vocab, s.c_str(), s.length(), toks.data(), toks.size(), add_bos, true); }
    toks.resize(n);
    batch.n_tokens = 0;
    for (int i = 0; i < n; ++i) {
        batch.token[batch.n_tokens] = toks[i];
        batch.pos[batch.n_tokens] = pos++;
        batch.n_seq_id[batch.n_tokens] = 1;
        batch.seq_id[batch.n_tokens][0] = 0;
        batch.logits[batch.n_tokens] = (i == n - 1);
        batch.n_tokens++;
    }
    llama_decode(ctx, batch);
}

// One full two-segment run. Returns "seg1 || ::: || seg2verb".
static std::string run_once(llama_model* model, const llama_vocab* vocab,
                            const std::string& grounded, uint64_t seed,
                            int seg1_max, bool print) {
    llama_context_params cp = llama_context_default_params();
    cp.n_ctx = 2048; cp.n_threads = 1; cp.n_threads_batch = 1;
    llama_context* ctx = llama_init_from_model(model, cp);

    // ChatML wrap
    llama_chat_message msg[1]; msg[0].role = "user"; msg[0].content = grounded.c_str();
    const char* tmpl = llama_model_chat_template(model, nullptr);
    std::vector<char> tp(grounded.size() + 1024);
    int r = llama_chat_apply_template(tmpl, msg, 1, true, tp.data(), tp.size());
    if (r > (int)tp.size()) { tp.resize(r); r = llama_chat_apply_template(tmpl, msg, 1, true, tp.data(), tp.size()); }
    std::string prompt = (r > 0) ? std::string(tp.data()) : grounded;

    llama_batch batch = llama_batch_init(2048, 0, 1);
    int32_t pos = 0;
    decode_str(ctx, vocab, batch, pos, prompt, true);

    // chain (shared style; fresh dist per run for determinism)
    llama_sampler_chain_params sp = llama_sampler_chain_default_params();
    llama_sampler* chain = llama_sampler_chain_init(sp);
    llama_sampler_chain_add(chain, llama_sampler_init_penalties(200, 1.15f, 0.0f, 0.0f));
    llama_sampler_chain_add(chain, llama_sampler_init_top_k(50));
    llama_sampler_chain_add(chain, llama_sampler_init_top_p(0.9, 1));
    llama_sampler_chain_add(chain, llama_sampler_init_temp(0.8));
    llama_sampler_chain_add(chain, llama_sampler_init_dist(seed));

    // SEGMENT 1 — FREE (no grammar)
    Sampled seg1 = gen_segment(ctx, vocab, batch, pos, nullptr, chain, seg1_max);

    // BRIDGE STRING — appended into the SAME context (no KV reset)
    decode_str(ctx, vocab, batch, pos, "\nGiven this, I will act now.\nACTION: ", false);

    // SEGMENT 2 — GRAMMAR ATTACHED MID-CONTEXT (fresh grammar sampler at root)
    llama_sampler* grmr = llama_sampler_init_grammar(vocab, ACTION_GRAMMAR, "root");
    Sampled seg2 = gen_segment(ctx, vocab, batch, pos, grmr, chain, 12);

    if (print) {
        std::cout << "--- SEGMENT 1 (free thought) ---\n" << seg1.text << "\n";
        std::cout << "--- SEGMENT 2 (constrained action) ---\n[" << seg2.text << "]\n\n";
    }
    std::string verb = seg2.text;
    llama_sampler_free(grmr);
    llama_sampler_free(chain);
    llama_batch_free(batch);
    llama_free(ctx);
    return seg1.text + " ::: " + verb;
}

int main() {
    llama_backend_init();
    llama_model_params mp = llama_model_default_params();
    llama_model* model = llama_model_load_from_file("models/smollm2-360m-instruct-q8_0.gguf", mp);
    if (!model) { std::cerr << "model load fail\n"; return 1; }
    const llama_vocab* vocab = llama_model_get_vocab(model);

    std::string grounded =
        "You are a thronglet in the village. You are starving (hunger 8%). "
        "Your neighbour Borin is right next to you holding a fresh loaf of bread. "
        "Think about your situation.";

    std::cout << "========== RUN A (seed 42) ==========\n";
    std::string a1 = run_once(model, vocab, grounded, 42, 120, true);
    std::cout << "========== RUN A' (seed 42 — determinism check) ==========\n";
    std::string a2 = run_once(model, vocab, grounded, 42, 120, false);
    std::cout << "========== RUN B (seed 99 — differential) ==========\n";
    std::string b1 = run_once(model, vocab, grounded, 99, 120, true);

    std::cout << "\n===== VERDICT =====\n";
    std::cout << "Determinism (A == A'): " << (a1 == a2 ? "PASS (byte-identical)" : "FAIL") << "\n";
    std::cout << "Differential (A != B): " << (a1 != b1 ? "PASS (seed changes output)" : "FAIL") << "\n";

    // enum check on the three verbs
    const char* enums[] = {"EAT","MOVE_TO","GIVE","SPEAK","WAIT"};
    auto verb_of = [](const std::string& s){ auto p = s.rfind(" ::: "); return p==std::string::npos? s : s.substr(p+5); };
    for (auto* tag : {"A","A'","B"}) (void)tag;
    for (const std::string& full : {a1, a2, b1}) {
        std::string v = verb_of(full);
        bool ok = false; for (auto* e : enums) if (v == e) ok = true;
        std::cout << "Seg2 verb [" << v << "] in enum: " << (ok ? "PASS" : "FAIL") << "\n";
    }
    llama_model_free(model);
    llama_backend_free();
    return 0;
}
