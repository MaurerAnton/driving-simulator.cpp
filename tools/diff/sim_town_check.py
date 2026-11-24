#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Compare two lane-graph dumps (tools/diff/sim_town.ts, tools/dev/sim_town_dump.cpp) by what they say, not by how
the paths through the junctions are numbered:   tools/diff/sim_town_check.py a.txt b.txt"""
import sys
def load(p):
    lanes={};conns={};confl={};sib={};edges={};nodes={}
    for line in open(p):
        t=line.split()
        if t[0]=='lane':
            i=t.index('out'); j=t.index('in')
            lanes[int(t[1])]=dict(v=t[2:i],out=[int(x) for x in t[i+1:j]],inn=[int(x) for x in t[j+1:]])
        elif t[0]=='conn':
            i=t.index('pts')
            conns[int(t[1])]=dict(node=int(t[2]),frm=int(t[3]),to=int(t[4]),turn=int(t[5]),len=float(t[6]),pts=[float(x) for x in t[i+1:]])
        elif t[0]=='conflict':
            confl.setdefault(int(t[1]),[]).append((int(t[2]),float(t[3]),float(t[4]),int(t[5]),int(t[6])))
        elif t[0]=='sibling':
            sib.setdefault(int(t[1]),[]).append((int(t[2]),float(t[3]),float(t[4])))
        elif t[0]=='edge': edges[int(t[1])]=t[2:]
        elif t[0]=='node': nodes[int(t[1])]=t[2:]
    return lanes,conns,confl,sib,edges,nodes
a=load(sys.argv[1]); b=load(sys.argv[2])
print('lanes',len(a[0]),len(b[0]),'conns',len(a[1]),len(b[1]),'conflicts',sum(len(v) for v in a[2].values()),sum(len(v) for v in b[2].values()),'siblings',sum(len(v) for v in a[3].values()),sum(len(v) for v in b[3].values()))
ka={(c['frm'],c['to']):i for i,c in a[1].items()}; kb={(c['frm'],c['to']):i for i,c in b[1].items()}
print('conns only in js',sorted(set(ka)-set(kb))[:10],'only in cpp',sorted(set(kb)-set(ka))[:10])
md=0;turnDiff=0
for k in set(ka)&set(kb):
    ca=a[1][ka[k]]; cb=b[1][kb[k]]
    if ca['turn']!=cb['turn']: turnDiff+=1
    if len(ca['pts'])!=len(cb['pts']): print('pts count differs',k,len(ca['pts']),len(cb['pts'])); continue
    md=max(md,abs(ca['len']-cb['len']),max(abs(x-y) for x,y in zip(ca['pts'],cb['pts'])))
print('common conns',len(set(ka)&set(kb)),'max geom diff',md,'turn diffs',turnDiff)
# lane.out order
od=0
for i in a[0]:
    oa=[(a[1][c]['frm'],a[1][c]['to']) for c in a[0][i]['out']]; ob=[(b[1][c]['frm'],b[1][c]['to']) for c in b[0][i]['out']]
    if oa!=ob: od+=1
    ia=[(a[1][c]['frm'],a[1][c]['to']) for c in a[0][i]['inn']]; ib=[(b[1][c]['frm'],b[1][c]['to']) for c in b[0][i]['inn']]
    if ia!=ib: od+=1
print('lanes whose out/in order differs',od)
# conflicts
cd=0;cmd=0;order=0
for k in set(ka)&set(kb):
    la=[((a[1][o]['frm'],a[1][o]['to']),s1,s2,m,y) for o,s1,s2,m,y in a[2].get(ka[k],[])]
    lb=[((b[1][o]['frm'],b[1][o]['to']),s1,s2,m,y) for o,s1,s2,m,y in b[2].get(kb[k],[])]
    da={x[0]:x for x in la}; db={x[0]:x for x in lb}
    if set(da)!=set(db): cd+=1; 
    if cd==1 and set(da)!=set(db): print(' e.g. conflicts of',k,'js only',set(da)-set(db),'cpp only',set(db)-set(da))
    for o in set(da)&set(db):
        x=da[o];y=db[o]
        cmd=max(cmd,abs(x[1]-y[1]),abs(x[2]-y[2]))
        if x[3:]!=y[3:]: print(' flags differ',k,o,x[3:],y[3:])
    if [x[0] for x in la]!=[x[0] for x in lb]: order+=1
print('conns with different conflict sets',cd,'max s diff',cmd,'conflict order differs',order)
sd=0
for k in set(ka)&set(kb):
    la=sorted(((a[1][o]['frm'],a[1][o]['to']),round(u,9),round(w,9)) for o,u,w in a[3].get(ka[k],[]))
    lb=sorted(((b[1][o]['frm'],b[1][o]['to']),round(u,9),round(w,9)) for o,u,w in b[3].get(kb[k],[]))
    if la!=lb: sd+=1
print('conns with different siblings',sd)
print('edges differ',sum(1 for i in a[4] if a[4][i]!=b[4].get(i)),'nodes differ',sum(1 for i in a[5] if a[5][i]!=b[5].get(i)))
for i in a[5]:
    if a[5][i]!=b[5].get(i): print(' node',i,a[5][i],'|',b[5].get(i)); break
for i in a[4]:
    if a[4][i]!=b[4].get(i): print(' edge',i,a[4][i],'|',b[4].get(i)); break
