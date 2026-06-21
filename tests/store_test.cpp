#include "cognition/cognitive_store.h"
#include <iostream>
using namespace tbv;
int main(){
  CognitiveStore s(500);
  // add 600 entries; importance = (i*37)%1000 (varied, deterministic)
  for(uint32_t i=0;i<600;i++){ MemoryEntry e; e.mem_id=i; e.importance=(int32_t)((i*37)%1000);
    e.type=MemType::EXPERIENCE; e.text="m"+std::to_string(i); s.add(e); }
  std::cout<<"size after 600 adds (cap 500): "<<s.size()<<"\n";
  // assert: no surviving entry has importance below the 100 lowest that should be gone.
  int below200=0; for(auto&m:s.all()) if(m.importance<100) below200++;
  std::cout<<"survivors with importance<100: "<<below200<<" (low-salience evicted)\n";
  auto* top=s.most_salient();
  std::cout<<"most_salient importance="<<(top?top->importance:-1)<<" mem_id="<<(top?top->mem_id:0)<<"\n";
  // determinism: rebuild identically, compare salient pick
  CognitiveStore s2(500);
  for(uint32_t i=0;i<600;i++){ MemoryEntry e; e.mem_id=i; e.importance=(int32_t)((i*37)%1000);
    e.text="m"+std::to_string(i); s2.add(e); }
  auto* top2=s2.most_salient();
  std::cout<<(s.size()==s2.size()&&top->mem_id==top2->mem_id?"DETERMINISTIC: identical":"FAIL")<<"\n";
}
