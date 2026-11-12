#!/usr/bin/env python3
"""Score a --telephone_bench JSONL: per-hop origin content-word retention, plus text length.
usage: v4_telephone_score.py out.jsonl [out2.jsonl ...]"""
import json, re, sys, collections
SW=set("the a an and or but of to in on at for with is was are were be been it its this that they them their you your i me my we our he she his her as by from not no so do does did have has had will would can could just what which who into out up about all some someone there here very".split())
cw=lambda t:set(w for w in re.findall(r"[a-z']+",t.lower()) if len(w)>3 and w not in SW)
def gist(t):
    out=[];d=0;star=False
    for c in t:
        if c=='(': d+=1; continue
        if c==')': d=max(0,d-1); continue
        if d: continue
        if c=='*': star=not star; continue
        if star: continue
        out.append(' ' if c=='\n' else c)
    t=''.join(out); res=[];sents=0;words=0;inw=False
    for i,c in enumerate(t):
        if c==' ': inw=False; res.append(c); continue
        if not inw:
            inw=True; words+=1
            if words>35: break
        res.append(c)
        if c in '.!?' and (i+1>=len(t) or t[i+1]==' '):
            sents+=1
            if sents>=2: break
    g=' '.join(''.join(res).split()); return g or t
USE_GIST='--gist' in sys.argv
for path in [a for a in sys.argv[1:] if not a.startswith('--')]:
    rows=[json.loads(l) for l in open(path) if l.startswith('{')]
    origin={r['seed']:cw(gist(r['text']) if USE_GIST else r['text']) for r in rows if r['hop']==0}
    ret=collections.defaultdict(list); words=collections.defaultdict(list); they=0; n=0
    for r in rows:
        if r['hop']==0 or not origin[r['seed']]: continue
        ret[r['hop']].append(len(origin[r['seed']] & cw(r['text']))/len(origin[r['seed']]))
        words[r['hop']].append(len(r['text'].split())); n+=1; they+=' They say it is ' in r['text']
    print(path, 'seeds',len(origin),'rows',n,'they_say',they)
    for h in sorted(ret): print(f"  hop{h}: retention {sum(ret[h])/len(ret[h]):.3f}  words {sum(words[h])/len(words[h]):.0f}")
