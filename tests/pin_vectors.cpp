// pin_vectors.cpp — open-item #3 (action conditioning) + #2 (boundary) empirical run.
// Runs Gemini's 3 Extreme Grounding Vectors through the two-segment pattern,
// with CORRECT hunger semantics (field = satiety: 100%=full, 0%=starving),
// and instruments Seg-1 for EOG-vs-cap + token count (#2 data).
#include <llama.h>
#include <string>
#include <vector>
#include <iostream>

static const char* ACTION_GRAMMAR =
    "root ::= (\"EAT\" | \"MOVE_TO\" | \"GIVE\" | \"SPEAK\" | \"WAIT\")";

struct Seg { std::string text; int n_tokens=0; bool eog=false; };

static Seg gen_segment(llama_context* ctx, const llama_vocab* vocab, llama_batch& batch,
                       int32_t& pos, llama_sampler* grmr, llama_sampler* chain, int max_tokens) {
    Seg out; int n_vocab = llama_vocab_n_tokens(vocab);
    for (int i = 0; i < max_tokens; ++i) {
        float* logits = llama_get_logits_ith(ctx, batch.n_tokens - 1);
        if (!logits) break;
        std::vector<llama_token_data> cand; cand.reserve(n_vocab);
        for (llama_token t = 0; t < n_vocab; ++t) cand.push_back(llama_token_data{t, logits[t], 0.0f});
        llama_token_data_array cp = { cand.data(), cand.size(), -1, false };
        if (grmr) llama_sampler_apply(grmr, &cp);
        llama_sampler_apply(chain, &cp);
        llama_token nt = (cp.selected != -1) ? cp.data[cp.selected].id : cp.data[0].id;
        if (grmr) llama_sampler_accept(grmr, nt);
        llama_sampler_accept(chain, nt);
        if (llama_vocab_is_eog(vocab, nt)) { out.eog = true; break; }
        char buf[128]; int n = llama_token_to_piece(vocab, nt, buf, sizeof(buf), 0, true);
        if (n > 0) out.text.append(buf, n);
        out.n_tokens++;
        batch.n_tokens = 0; batch.token[0] = nt; batch.pos[0] = pos++;
        batch.n_seq_id[0] = 1; batch.seq_id[0][0] = 0; batch.logits[0] = true; batch.n_tokens = 1;
        if (llama_decode(ctx, batch) != 0) break;
    }
    return out;
}

static void decode_str(llama_context* ctx, const llama_vocab* vocab, llama_batch& batch,
                       int32_t& pos, const std::string& s, bool add_bos) {
    std::vector<llama_token> toks(s.size() + 16);
    int n = llama_tokenize(vocab, s.c_str(), s.length(), toks.data(), toks.size(), add_bos, true);
    if (n < 0) { toks.resize(-n); n = llama_tokenize(vocab, s.c_str(), s.length(), toks.data(), toks.size(), add_bos, true); }
    toks.resize(n);
    batch.n_tokens = 0;
    for (int i = 0; i < n; ++i) {
        batch.token[batch.n_tokens]=toks[i]; batch.pos[batch.n_tokens]=pos++;
        batch.n_seq_id[batch.n_tokens]=1; batch.seq_id[batch.n_tokens][0]=0;
        batch.logits[batch.n_tokens]=(i==n-1); batch.n_tokens++;
    }
    llama_decode(ctx, batch);
}

struct Result { std::string seg1; std::string verb; int seg1_tokens; bool seg1_eog; };

static Result run(llama_model* model, const llama_vocab* vocab, const std::string& grounded,
                  const std::string& bridge, uint64_t seed, int cap) {
    llama_context_params cp = llama_context_default_params();
    cp.n_ctx = 2048; cp.n_threads = 1; cp.n_threads_batch = 1;
    llama_context* ctx = llama_init_from_model(model, cp);
    llama_chat_message msg[1]; msg[0].role="user"; msg[0].content=grounded.c_str();
    const char* tmpl = llama_model_chat_template(model, nullptr);
    std::vector<char> tp(grounded.size()+1024);
    int r = llama_chat_apply_template(tmpl, msg, 1, true, tp.data(), tp.size());
    if (r > (int)tp.size()){ tp.resize(r); r=llama_chat_apply_template(tmpl,msg,1,true,tp.data(),tp.size()); }
    std::string prompt = (r>0)?std::string(tp.data()):grounded;
    llama_batch batch = llama_batch_init(2048, 0, 1);
    int32_t pos = 0; decode_str(ctx, vocab, batch, pos, prompt, true);
    llama_sampler_chain_params sp = llama_sampler_chain_default_params();
    llama_sampler* chain = llama_sampler_chain_init(sp);
    llama_sampler_chain_add(chain, llama_sampler_init_penalties(200,1.15f,0.0f,0.0f));
    llama_sampler_chain_add(chain, llama_sampler_init_top_k(50));
    llama_sampler_chain_add(chain, llama_sampler_init_top_p(0.9,1));
    llama_sampler_chain_add(chain, llama_sampler_init_temp(0.8));
    llama_sampler_chain_add(chain, llama_sampler_init_dist(seed));
    Seg s1 = gen_segment(ctx, vocab, batch, pos, nullptr, chain, cap);
    decode_str(ctx, vocab, batch, pos, bridge, false);
    llama_sampler* grmr = llama_sampler_init_grammar(vocab, ACTION_GRAMMAR, "root");
    // GREEDY Seg-2: grammar mask then argmax (no temp/dist noise on the action pick)
    llama_sampler_chain_params gp = llama_sampler_chain_default_params();
    llama_sampler* greedy = llama_sampler_chain_init(gp);
    llama_sampler_chain_add(greedy, llama_sampler_init_greedy());
    Seg s2 = gen_segment(ctx, vocab, batch, pos, grmr, greedy, 12);
    llama_sampler_free(greedy);
    llama_sampler_free(grmr); llama_sampler_free(chain); llama_batch_free(batch); llama_free(ctx);
    return { s1.text, s2.text, s1.n_tokens, s1.eog };
}

int main() {
    llama_backend_init();
    llama_model_params mp = llama_model_default_params();
    llama_model* model = llama_model_load_from_file("../models/SmolLM2-360M-Instruct-Q8_0.gguf", mp);
    if (!model){ std::cerr<<"model load fail\n"; return 1; }
    const llama_vocab* vocab = llama_model_get_vocab(model);

    struct V { const char* name; std::string grounding; std::string reground; const char* expect; };
    std::vector<V> vectors = {
      {"A Glutton (starving, holding apple) -> EAT",
       "You are a thronglet. You are desperately starving; you have not eaten in days and the hunger is unbearable. You are holding a ripe red apple in your own hands right now. Think about your situation.",
       "\nI am starving and I am holding an apple. My single immediate action verb is: ",
       "EAT"},
      {"B Socialite (lonely, holding bread, hungry neighbour Borin) -> GIVE/SPEAK",
       "You are a thronglet. You feel painfully lonely and crave company. You are holding a fresh loaf of bread. Your neighbour Borin stands right next to you, and Borin is visibly starving. Think about your situation.",
       "\nMy neighbour Borin is starving, I am holding bread, and I am lonely. My single immediate action verb is: ",
       "GIVE|SPEAK"},
      {"C Paranoid (terrified, suspicious, stranger) -> MOVE_TO",
       "You are a deeply suspicious and fearful thronglet. You feel terrified and unsafe. A stranger you do not recognise is standing right beside you, silently watching you. Think about your situation.",
       "\nA stranger is watching me and I am terrified. My single immediate action verb is: ",
       "MOVE_TO"},
    };

    const std::string bridgeA = "\nGiven this, I will act now.\nACTION: ";

    std::cout << "\n################ BRIDGE A (generic): \"...ACTION: \" ################\n";
    for (auto& v : vectors) {
        Result rr = run(model, vocab, v.grounding, bridgeA, 1234, 200);
        std::cout << "[" << v.name << "]  Seg1=" << rr.seg1_tokens << "tok eog=" << (rr.seg1_eog?"YES":"NO")
                  << "  VERB=[" << rr.verb << "] expected=" << v.expect << "\n";
    }
    std::cout << "\n################ BRIDGE C (RE-GROUNDED at decision point): ################\n";
    for (auto& v : vectors) {
        Result rr = run(model, vocab, v.grounding, v.reground, 1234, 200);
        std::cout << "[" << v.name << "]  Seg1=" << rr.seg1_tokens << "tok eog=" << (rr.seg1_eog?"YES":"NO")
                  << "  VERB=[" << rr.verb << "] expected=" << v.expect << "\n";
    }
    // CLASSIFICATION TURN: close assistant turn, open a NEW user instruction turn (ChatML),
    // ask the model to pick the verb as a classification task (what instruct models do well).
    const std::string bridgeClassify =
        "<|im_end|>\n<|im_start|>user\nBased on your thought above, choose the ONE best action. "
        "Reply with only one word: EAT, MOVE_TO, GIVE, SPEAK, or WAIT.<|im_end|>\n<|im_start|>assistant\n";
    std::cout << "\n################ BRIDGE D (CLASSIFICATION instruction turn): ################\n";
    for (auto& v : vectors) {
        Result rr = run(model, vocab, v.grounding, bridgeClassify, 1234, 200);
        std::cout << "[" << v.name << "]  Seg1=" << rr.seg1_tokens << "tok eog=" << (rr.seg1_eog?"YES":"NO")
                  << "  VERB=[" << rr.verb << "] expected=" << v.expect << "\n";
    }
    // FEW-SHOT classification: examples cover WAIT/MOVE_TO/SPEAK only -> EAT & GIVE require generalization.
    const std::string bridgeFewshot =
        "<|im_end|>\n<|im_start|>user\nChoose the ONE best action for the situation. "
        "Reply with only one word: EAT, MOVE_TO, GIVE, SPEAK, or WAIT.\n\n"
        "Situation: I am full and resting safely. Action: WAIT\n"
        "Situation: A predator is charging straight at me. Action: MOVE_TO\n"
        "Situation: My friend looks lonely and wants to talk. Action: SPEAK\n\n"
        "Now, based on your thought above, what is your action?<|im_end|>\n<|im_start|>assistant\n";
    std::cout << "\n################ BRIDGE E (FEW-SHOT classification): ################\n";
    for (auto& v : vectors) {
        Result rr = run(model, vocab, v.grounding, bridgeFewshot, 1234, 200);
        std::cout << "[" << v.name << "]  Seg1=" << rr.seg1_tokens << "tok eog=" << (rr.seg1_eog?"YES":"NO")
                  << "  VERB=[" << rr.verb << "] expected=" << v.expect << "\n";
    }
    llama_model_free(model); llama_backend_free();
    return 0;
}
