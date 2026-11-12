#!/usr/bin/env python3
"""V4 offline metrics over a village.db (read-only): hop histogram, origin-word retention per hop,
unique carriers per chain, no-echo hop share, relay-delay percentiles. usage: v4_metrics.py village.db"""
import sqlite3, sys, re, collections, json
SW=set("the a an and or but of to in on at for with is was are were be been it its this that they them their you your i me my we our he she his her as by from not no so do does did have has had will would can could just what which who into out up about all some someone there here very".split())
cw=lambda t:set(w for w in re.findall(r"[a-z']+",t.lower()) if len(w)>3 and w not in SW)
db=sqlite3.connect('file:'+sys.argv[1]+'?mode=ro',uri=True)
rows=db.execute("select chain_id,hop,src_villager,dst_villager,tick,inbound_text,outbound_text from HearsayChain order by tick").fetchall()
out={'rows':len(rows)}
n=len(rows) or 1
h=collections.Counter(r[1] for r in rows); out['hop_share']={k:round(h[k]/n,4) for k in sorted(h)}
origin={}; ret=collections.defaultdict(list)
for c,hp,s,d,t,i,o in rows:
    if hp==1: origin.setdefault(c,cw(i))
for c,hp,s,d,t,i,o in rows:
    if origin.get(c): ret[hp].append(len(origin[c]&cw(o.split(' They say it is ')[0]))/len(origin[c]))
out['origin_retention']={k:round(sum(v)/len(v),3) for k,v in sorted(ret.items())}
car=collections.defaultdict(set); seen=collections.defaultdict(set); echo=collections.Counter(); tot=collections.Counter()
for c,hp,s,d,t,i,o in rows:
    if hp>=2:
        tot[hp]+=1; echo[hp]+= d in seen[c]
    seen[c].update((s,d)); car[c].update((s,d))
cs=sorted(len(v) for v in car.values())
out['carriers_per_chain']={'mean':round(sum(cs)/len(cs),2) if cs else 0,'max':cs[-1] if cs else 0}
out['echo_share_hop>=2']=round(sum(echo.values())/max(1,sum(tot.values())),3)
h1={}
for c,hp,s,d,t,i,o in rows:
    if hp==1: h1.setdefault((c,d),t)
dl=sorted(t-h1[(c,s)] for c,hp,s,d,t,i,o in rows if hp==2 and (c,s) in h1)
out['relay_delay_p10_50_90']=[dl[int(len(dl)*p)] for p in (.1,.5,.9)] if dl else None
print(json.dumps(out,indent=1))
