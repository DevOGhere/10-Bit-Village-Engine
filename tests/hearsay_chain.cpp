// hearsay_chain.cpp — Phase 2 de-risk: 5-hop retell chain on real 360M.
// Measures (a) Jaccard drift hop0->hopK, (b) coherence (length/non-degenerate), (c) determinism.
#include <llama.h>
#include <string>
#include <vector>
#include <set>
#include <sstream>
#include <iostream>

static void decode_str(llama_context*c,const llama_vocab*v,llama_batch&b,int32_t&p,const std::string&s,bool bos){
  std::vector<llama_token> t(s.size()+16); int n=llama_tokenize(v,s.c_str(),s.length(),t.data(),t.size(),bos,true);
  if(n<0){t.resize(-n);n=llama_tokenize(v,s.c_str(),s.length(),t.data(),t.size(),bos,true);} t.resize(n);
  b.n_tokens=0; for(int i=0;i<n;i++){b.token[b.n_tokens]=t[i];b.pos[b.n_tokens]=p++;b.n_seq_id[b.n_tokens]=1;
    b.seq_id[b.n_tokens][0]=0;b.logits[b.n_tokens]=(i==n-1);b.n_tokens++;} llama_decode(c,b); }
static std::string gen(llama_context*c,const llama_vocab*v,llama_batch&b,int32_t&p,llama_sampler*s,int mx){
  std::string o; int nv=llama_vocab_n_tokens(v);
  for(int i=0;i<mx;i++){float*lg=llama_get_logits_ith(c,b.n_tokens-1); if(!lg)break;
    std::vector<llama_token_data> cd; cd.reserve(nv); for(llama_token t=0;t<nv;t++)cd.push_back({t,lg[t],0.0f});
    llama_token_data_array cp={cd.data(),cd.size(),-1,false}; llama_sampler_apply(s,&cp);
    llama_token nt=cp.selected!=-1?cp.data[cp.selected].id:cp.data[0].id; llama_sampler_accept(s,nt);
    if(llama_vocab_is_eog(v,nt))break; char bf[128]; int nc=llama_token_to_piece(v,nt,bf,sizeof(bf),0,true);
    if(nc>0)o.append(bf,nc); b.n_tokens=0;b.token[0]=nt;b.pos[0]=p++;b.n_seq_id[0]=1;b.seq_id[0][0]=0;
    b.logits[0]=true;b.n_tokens=1; if(llama_decode(c,b)!=0)break;} return o; }

static std::set<std::string> toks(std::string s){ for(auto&c:s)c=tolower(c); std::set<std::string> r;
  std::stringstream ss(s); std::string w; while(ss>>w){ std::string x; for(char c:w) if(isalpha(c))x+=c; if(x.size()>2)r.insert(x);} return r; }
static double jaccard(const std::set<std::string>&a,const std::set<std::string>&b){
  if(a.empty()||b.empty())return 0; int inter=0; for(auto&x:a)if(b.count(x))inter++;
  return (double)inter/(a.size()+b.size()-inter); }

static std::string retell(llama_model*m,const llama_vocab*v,const std::string& heard,uint64_t seed){
  llama_context_params cp=llama_context_default_params(); cp.n_ctx=2048; cp.n_threads=1; cp.n_threads_batch=1;
  llama_context*ctx=llama_init_from_model(m,cp);
  std::string user="You overhear someone in the village saying: \""+heard+"\" In your own words, what do you make of it and pass along to others?";
  llama_chat_message msg[1]; msg[0].role="user"; msg[0].content=user.c_str();
  const char*tm=llama_model_chat_template(m,nullptr); std::vector<char> tp(user.size()+1024);
  int r=llama_chat_apply_template(tm,msg,1,true,tp.data(),tp.size());
  if(r>(int)tp.size()){tp.resize(r);r=llama_chat_apply_template(tm,msg,1,true,tp.data(),tp.size());}
  std::string prompt=r>0?std::string(tp.data()):user;
  llama_batch b=llama_batch_init(2048,0,1); int32_t pos=0; decode_str(ctx,v,b,pos,prompt,true);
  llama_sampler_chain_params sp=llama_sampler_chain_default_params(); llama_sampler*ch=llama_sampler_chain_init(sp);
  llama_sampler_chain_add(ch,llama_sampler_init_penalties(200,1.15f,0.0f,0.0f));
  llama_sampler_chain_add(ch,llama_sampler_init_top_k(50)); llama_sampler_chain_add(ch,llama_sampler_init_top_p(0.9,1));
  llama_sampler_chain_add(ch,llama_sampler_init_temp(0.8)); llama_sampler_chain_add(ch,llama_sampler_init_dist(seed));
  std::string out=gen(ctx,v,b,pos,ch,90);
  llama_sampler_free(ch); llama_batch_free(b); llama_free(ctx); return out;
}

int main(){
  llama_backend_init(); llama_model_params mp=llama_model_default_params();
  llama_model*m=llama_model_load_from_file("models/smollm2-360m-instruct-q8_0.gguf",mp);
  if(!m){std::cerr<<"load fail\n";return 1;} const llama_vocab*v=llama_model_get_vocab(m);
  std::string seed_mem="The village of Brindlemark is magical, with glowing trees and sparkling lakes behind the houses.";
  std::cout<<"HOP0 (seed): "<<seed_mem<<"\n\n";
  auto t0=toks(seed_mem); std::string cur=seed_mem;
  for(int hop=1;hop<=5;hop++){
    cur=retell(m,v,cur,1000+hop);
    std::string trim=cur.size()>140?cur.substr(0,140):cur;
    std::cout<<"HOP"<<hop<<" (J0="<<jaccard(t0,toks(cur))<<", len="<<cur.size()<<"): "<<trim<<"...\n\n";
  }
  llama_model_free(m); llama_backend_free(); return 0;
}
