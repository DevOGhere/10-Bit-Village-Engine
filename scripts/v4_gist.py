"""Python mirror of v4_gist() in include/cognition/degradation.h (keep in sync)."""
def gist(t):
    drop=[False]*len(t); st=[]
    for i,c in enumerate(t):
        if c=='(': st.append(i)
        elif c==')' and st:
            o=st.pop()
            for k in range(o,i+1): drop[k]=True
    op=-1
    for i,c in enumerate(t):
        if drop[i] or c!='*': continue
        if op<0: op=i
        else:
            for k in range(op,i+1): drop[k]=True
            op=-1
    u=''.join((' ' if c=='\n' else c) for i,c in enumerate(t) if not drop[i])
    res=[];sents=0;words=0;inw=False
    for i,c in enumerate(u):
        if c==' ':
            inw=False
            if res and res[-1]!=' ': res.append(c)
            continue
        if not inw:
            inw=True; words+=1
            if words>35: break
        res.append(c)
        if c in '.!?' and (i+1>=len(u) or u[i+1]==' '):
            sents+=1
            if sents>=2: break
    g=''.join(res).strip()
    if g: return g
    return ' '.join(t.split()[:35])
