import re,collections
def load(p):
    ops=collections.OrderedDict(); cur=None; var=None
    for line in open(p):
        line=line.rstrip('\n')
        if line.startswith('OP '):
            name,nc=line[3:].split(' num_cycles='); cur=ops.setdefault(name,{'nc':int(nc),'v':collections.OrderedDict()})
        elif line.startswith('V '):
            key=' '.join(line.split()[2:6]); m=re.search(r'rows=(\d+) ncyc=(\d+)',line)
            var=cur['v'][key]={'rows':int(m[1]),'ncyc':int(m[2]),'body':[]}
        else:
            var['body'].append(line.split(' ',3)[3] if False else ' '.join(line.split()[1:]))
    return ops
o=load('old.txt'); n=load('new.txt')
print("ops only old:",[k for k in o if k not in n]); print("ops only new:",[k for k in n if k not in o])
same=[];changed=[]
for k in o:
    if k not in n: continue
    a,b=o[k],n[k]
    if a['nc']==b['nc'] and all(a['v'][v]['body']==b['v'][v]['body'] for v in a['v']): same.append(k); continue
    rows=sorted({(a['v'][v]['rows'],b['v'][v]['rows']) for v in a['v']})
    changed.append(k); print(f"CHANGED {k}: num_cycles {a['nc']}->{b['nc']}; rows(old,new) {rows}")
print(len(same),"unchanged:"," ".join(same))
# self-consistency: rows == num_cycles(instr) for each variant
for tag,d in (('old',o),('new',n)):
    bad=[(k,v,x['rows'],x['ncyc']) for k in d for v,x in d[k]['v'].items() if x['rows']!=x['ncyc']]
    print(tag,"rows!=num_cycles(instr):",len(bad), sorted({b[0] for b in bad}))
# opcodes used in emitted rows
def used(d): return collections.Counter(l.split()[0] for k in d for x in d[k]['v'].values() for l in x['body'])
uo,un=used(o),used(n)
print("row opcodes only in old expansions:",sorted(set(uo)-set(un)))
print("row opcodes only in new expansions:",sorted(set(un)-set(uo)))
