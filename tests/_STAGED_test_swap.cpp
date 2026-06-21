
#include <iostream>
#include <stdexcept>
#include <vector>
#include "llama.h"

// We will implement a custom infer_action directly in this file
// bypassing the one in llama_bridge.cpp for testing purposes

int main() {
    llama_backend_init();
    
    llama_model_params mparams = llama_model_default_params();
    llama_model* model = llama_load_model_from_file("../models/SmolLM2-360M-Instruct-Q8_0.gguf", mparams);
    if (!model) { std::cerr << "Failed to load model\n"; return 1; }
    
    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx = 1024;
    llama_context* ctx = llama_new_context_with_model(model, cparams);
    const llama_vocab* vocab = llama_model_get_vocab(model);

    std::string prompt = "<|im_start|>user\nYou are a hungry villager. What are you thinking?<|im_end|>\n<|im_start|>assistant\n";
    
    std::vector<llama_token> tokens(prompt.size() + 10);
    int n_tokens = llama_tokenize(vocab, prompt.c_str(), prompt.length(), tokens.data(), tokens.size(), true, true);
    if (n_tokens < 0) {
        tokens.resize(-n_tokens);
        n_tokens = llama_tokenize(vocab, prompt.c_str(), prompt.length(), tokens.data(), tokens.size(), true, true);
    }
    tokens.resize(n_tokens);

    llama_batch batch = llama_batch_init(512, 0, 1);
    for (size_t i = 0; i < tokens.size(); i++) {
        batch.token[batch.n_tokens] = tokens[i];
        batch.pos[batch.n_tokens] = i;
        batch.n_seq_id[batch.n_tokens] = 1;
        batch.seq_id[batch.n_tokens][0] = 0;
        batch.logits[batch.n_tokens] = (i == tokens.size() - 1);
        batch.n_tokens++;
    }

    if (llama_decode(ctx, batch) != 0) {
        std::cerr << "llama_decode failed\n";
        return 1;
    }

    int n_past = batch.n_tokens;

    std::cout << "--- SEGMENT 1 (FREE THOUGHT) ---\n";
    llama_sampler_chain_params sparams = llama_sampler_chain_default_params();
    llama_sampler* chain1 = llama_sampler_chain_init(sparams);
    llama_sampler_chain_add(chain1, llama_sampler_init_temp(0.8));
    llama_sampler_chain_add(chain1, llama_sampler_init_dist(1234));

    for (int i = 0; i < 20; i++) {
        llama_token id = llama_sampler_sample(chain1, ctx, -1);
        llama_sampler_accept(chain1, id);
        
        char buf[128];
        int n = llama_token_to_piece(vocab, id, buf, sizeof(buf), 0, true);
        if (n > 0) std::cout << std::string(buf, n);
        
        batch.n_tokens = 0;
        batch.token[0] = id;
        batch.pos[0] = n_past++;
        batch.n_seq_id[0] = 1;
        batch.seq_id[0][0] = 0;
        batch.logits[0] = true;
        batch.n_tokens = 1;
        if (llama_decode(ctx, batch) != 0) return 1;
    }
    llama_sampler_free(chain1);

    std::string bridge = "\nGiven this, I will act now.\nACTION: ";
    std::cout << "\n\n--- INJECTING BRIDGE ---\n" << bridge << "\n";

    std::vector<llama_token> b_tokens(bridge.size() + 10);
    int b_n = llama_tokenize(vocab, bridge.c_str(), bridge.length(), b_tokens.data(), b_tokens.size(), false, true);
    if (b_n < 0) {
        b_tokens.resize(-b_n);
        b_n = llama_tokenize(vocab, bridge.c_str(), bridge.length(), b_tokens.data(), b_tokens.size(), false, true);
    }
    b_tokens.resize(b_n);

    batch.n_tokens = 0;
    for (size_t i = 0; i < b_tokens.size(); i++) {
        batch.token[batch.n_tokens] = b_tokens[i];
        batch.pos[batch.n_tokens] = n_past++;
        batch.n_seq_id[batch.n_tokens] = 1;
        batch.seq_id[batch.n_tokens][0] = 0;
        batch.logits[batch.n_tokens] = (i == b_tokens.size() - 1);
        batch.n_tokens++;
    }
    if (llama_decode(ctx, batch) != 0) return 1;

    std::string grammar_str = "root ::= \" EAT\" | \" MOVE_TO\" | \" GIVE\" | \" WAIT\" | \"EAT\" | \"MOVE_TO\" | \"GIVE\" | \"WAIT\"";
    std::cout << "--- SEGMENT 2 (GRAMMAR ON) ---\n";
    
    llama_sampler* chain2 = llama_sampler_chain_init(sparams);
    llama_sampler* grmr = llama_sampler_init_grammar(vocab, grammar_str.c_str(), "root");
    if (!grmr) { std::cerr << "Grammar parse failed\n"; return 1; }
    llama_sampler_chain_add(chain2, grmr);
    llama_sampler_chain_add(chain2, llama_sampler_init_temp(0.8));
    llama_sampler_chain_add(chain2, llama_sampler_init_dist(1234));

    for (int i = 0; i < 5; i++) {
        llama_token id = llama_sampler_sample(chain2, ctx, -1);
        llama_sampler_accept(chain2, id);
        
        char buf[128];
        int n = llama_token_to_piece(vocab, id, buf, sizeof(buf), 0, true);
        if (n > 0) std::cout << std::string(buf, n);
        
        batch.n_tokens = 0;
        batch.token[0] = id;
        batch.pos[0] = n_past++;
        batch.n_seq_id[0] = 1;
        batch.seq_id[0][0] = 0;
        batch.logits[0] = true;
        batch.n_tokens = 1;
        if (llama_decode(ctx, batch) != 0) return 1;
    }
    std::cout << "\n";
    llama_sampler_free(chain2);
    llama_batch_free(batch);
    
    return 0;
}
