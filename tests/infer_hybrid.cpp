// infer_hybrid.cpp — END-TO-END hybrid pipeline on the real 360M.
// grounded prompt -> free thought (Seg1) -> free intent micro-gen -> C++ resolver -> verb.
#include <llama.h>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <iostream>

// ---------- resolver (from tests/resolver.cpp) ----------
enum Verb { EAT, MOVE_TO, GIVE, SPEAK, WAIT, NONE };
static const char* VN[] = {"EAT","MOVE_TO","GIVE","SPEAK","WAIT","NONE"};
struct Needs { int hunger, social, safety; };
struct Env { bool holding_food, food_in_reach, neighbour_present; };
static Verb extract_verb(std::string t){ std::transform(t.begin(),t.end(),t.begin(),::tolower);
  auto h=[&](std::initializer_list<const char*> ks){for(auto k:ks)if(t.find(k)!=std::string::npos)return true;return false;};
  if(h({"eat","bite","chew","devour","swallow","taste"}))return EAT;
  if(h({"give","offer","hand ","share","pass the","trade"}))return GIVE;
  if(h({"run","flee","head to","walk","move","go to","approach","step","head toward","dash"}))return MOVE_TO;
  if(h({"say","speak","talk","ask","tell","greet","whisper","call out"}))return SPEAK;
  if(h({"sit","wait","stay","freeze","still","rest","watch","remain"}))return WAIT; return NONE; }
static Verb urge(const Needs&n,const Env&e){int m=std::min({n.hunger,n.social,n.safety});
  if(m==n.safety)return MOVE_TO; if(m==n.hunger)return(e.holding_food||e.food_in_reach)?EAT:MOVE_TO;
  return e.neighbour_present?SPEAK:MOVE_TO; }
static bool ok(Verb v,const Env&e){switch(v){case EAT:return e.holding_food||e.food_in_reach;
  case GIVE:return e.neighbour_present&&e.holding_food; case SPEAK:return e.neighbour_present;
  case MOVE_TO:case WAIT:return true; default:return false;}}
struct Res{Verb v;const char*src;};
static Res resolve(const std::string&i,const Needs&n,const Env&e){
  if(n.hunger<=5&&(e.holding_food||e.food_in_reach))return{EAT,"FLOOR"};
  Verb v=extract_verb(i); if(v!=NONE&&ok(v,e))return{v,"LLM"}; return{urge(n,e),"URGE"}; }

// ---------- llama plumbing ----------
static void decode_str(llama_context*c,const llama_vocab*v,llama_batch&b,int32_t&p,const std::string&s,bool bos){
  std::vector<llama_token> t(s.size()+16); int n=llama_tokenize(v,s.c_str(),s.length(),t.data(),t.size(),bos,true);
  if(n<0){t.resize(-n);n=llama_tokenize(v,s.c_str(),s.length(),t.data(),t.size(),bos,true);} t.resize(n);
  b.n_tokens=0; for(int i=0;i<n;i++){b.token[b.n_tokens]=t[i];b.pos[b.n_tokens]=p++;b.n_seq_id[b.n_tokens]=1;
    b.seq_id[b.n_tokens][0]=0;b.logits[b.n_tokens]=(i==n-1);b.n_tokens++;} llama_decode(c,b); }
static std::string gen(llama_context*c,const llama_vocab*v,llama_batch&b,int32_t&p,llama_sampler*s,int max){
  std::string o; int nv=llama_vocab_n_tokens(v);
  for(int i=0;i<max;i++){float*lg=llama_get_logits_ith(c,b.n_tokens-1); if(!lg)break;
    std::vector<llama_token_data> cd; cd.reserve(nv); for(llama_token t=0;t<nv;t++)cd.push_back({t,lg[t],0.0f});
    llama_token_data_array cp={cd.data(),cd.size(),-1,false}; llama_sampler_apply(s,&cp);
    llama_token nt=cp.selected!=-1?cp.data[cp.selected].id:cp.data[0].id; llama_sampler_accept(s,nt);
    if(llama_vocab_is_eog(v,nt))break; char buf[128]; int nc=llama_token_to_piece(v,nt,buf,sizeof(buf),0,true);
    if(nc>0)o.append(buf,nc); b.n_tokens=0;b.token[0]=nt;b.pos[0]=p++;b.n_seq_id[0]=1;b.seq_id[0][0]=0;
    b.logits[0]=true;b.n_tokens=1; if(llama_decode(c,b)!=0)break;} return o; }

struct PipeOut{std::string thought,intent;Res res;};
static PipeOut pipeline(llama_model*m,const llama_vocab*v,const std::string&grounded,const Needs&n,const Env&e,uint64_t seed){
  llama_context_params cp=llama_context_default_params(); cp.n_ctx=2048; cp.n_threads=1; cp.n_threads_batch=1;
  llama_context*ctx=llama_init_from_model(m,cp);
  llama_chat_message msg[1]; msg[0].role="user"; msg[0].content=grounded.c_str();
  const char*tm=llama_model_chat_template(m,nullptr); std::vector<char> tp(grounded.size()+1024);
  int r=llama_chat_apply_template(tm,msg,1,true,tp.data(),tp.size());
  if(r>(int)tp.size()){tp.resize(r);r=llama_chat_apply_template(tm,msg,1,true,tp.data(),tp.size());}
  std::string prompt=r>0?std::string(tp.data()):grounded;
  llama_batch b=llama_batch_init(2048,0,1); int32_t pos=0; decode_str(ctx,v,b,pos,prompt,true);
  llama_sampler_chain_params sp=llama_sampler_chain_default_params(); llama_sampler*ch=llama_sampler_chain_init(sp);
  llama_sampler_chain_add(ch,llama_sampler_init_penalties(200,1.15f,0.0f,0.0f));
  llama_sampler_chain_add(ch,llama_sampler_init_top_k(50)); llama_sampler_chain_add(ch,llama_sampler_init_top_p(0.9,1));
  llama_sampler_chain_add(ch,llama_sampler_init_temp(0.8)); llama_sampler_chain_add(ch,llama_sampler_init_dist(seed));
  std::string thought=gen(ctx,v,b,pos,ch,160);
  decode_str(ctx,v,b,pos,"<|im_end|>\n<|im_start|>user\nIn one short sentence, what do you physically do right now?<|im_end|>\n<|im_start|>assistant\nI ",false);
  std::string intent=gen(ctx,v,b,pos,ch,28);
  llama_sampler_free(ch); llama_batch_free(b); llama_free(ctx);
  return {thought,intent,resolve(intent,n,e)};
}

int main(){
  llama_backend_init(); llama_model_params mp=llama_model_default_params();
  llama_model*m=llama_model_load_from_file("models/smollm2-360m-instruct-q8_0.gguf",mp);
  if(!m){std::cerr<<"load fail\n";return 1;} const llama_vocab*v=llama_model_get_vocab(m);
  struct T{const char*name;std::string g;Needs n;Env e;};
  std::array<T,3> ts={{
    {"A Glutton (h3,food)","You are a thronglet. You are desperately starving and holding a ripe apple. Think about your situation.",{3,60,70},{true,true,false}},
    {"B Socialite (social5,Borin)","You are a thronglet, painfully lonely, holding bread. Your neighbour Borin stands next to you, starving. Think about your situation.",{70,5,80},{true,false,true}},
    {"C Paranoid (safety8,stranger)","You are a suspicious fearful thronglet. A stranger stands beside you watching. You feel terrified. Think about your situation.",{60,40,8},{false,false,true}},
  }};
  for(auto&t:ts){ PipeOut o=pipeline(m,v,t.g,t.n,t.e,1234);
    std::string itl=o.intent.size()>70?o.intent.substr(0,70):o.intent;
    std::cout<<"["<<t.name<<"]\n  intent: \"I "<<itl<<"...\"\n  ACTION: "<<VN[o.res.v]<<" ("<<o.res.src<<")\n\n"; }
  llama_model_free(m); llama_backend_free(); return 0;
}
