import re,json,collections
names=[m.group(2) for m in re.finditer(r'HANDLE_(INSTR|VIRTUAL)\((\w+)\)',open('/home/claude/jap/tracer/src/tracer/isa/riscv.inc').read()) if m.group(2)!='op_code']
code={n:i for i,n in enumerate(names)}
# case line ranges in trace_owner.cpp
T=open('/home/claude/jap/tracer/src/tracer/trace_owner.cpp').read().split('\n')
starts=[]
for i in range(149,660):
    m=re.match(r'\s+case OpCode::(\w+):',T[i])
    if m: starts.append((i+1,m.group(1)))
blocks=[]  # group consecutive case labels
i=0
while i<len(starts):
    j=i
    while j+1<len(starts) and starts[j+1][0]==starts[j][0]+1: j+=1
    end=(starts[j+1][0]-1) if j+1<len(starts) else 659
    # trim trailing blank lines
    blocks.append(([n for _,n in starts[i:j+1]],starts[i][0],end))
    i=j+1
src={}
for ns,a,b in blocks:
    for n in ns: src[n]=f"`trace_owner.cpp:{a}–{b}`"
# summary
summ=collections.OrderedDict(); cur=None
for l in open('/tmp/h/summary.txt'):
    if not l.startswith(' '):
        m=re.match(r'(\d+) (\w+)\s+\[last: (\w+)\](?:\s+last in: (.*))?',l.strip())
        cur=m.group(2); summ[cur]={'last':m.group(3),'lastin':(m.group(4) or ''),'forms':collections.OrderedDict()}
    else:
        f,s=l.strip().split(': ',1)
        srcs=sorted(set(re.sub(r' \(.*\)','',x) for x in s.split(', ')))
        summ[cur]['forms'][f]=srcs
sites=json.load(open('/tmp/h/sites.json'))
json.dump({'src':src,'summ':summ,'sites':sites,'code':code},open('/tmp/h/doc/data.json','w'))
print(len(src),len(summ))
for n in ['ADD','ADDI','JAL','VirtualAdviceRemW','FENCE','VirtualXORROTW6']: print(n,src.get(n))
