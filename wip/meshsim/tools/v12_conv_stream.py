import sys,re
def args_at(s, i):
    # s[i] == '(' ; return (list of args, index after ')')
    depth=0; cur=''; out=[]; j=i
    while True:
        c=s[j]
        if c=='(':
            depth+=1
            if depth>1: cur+=c
        elif c==')':
            depth-=1
            if depth==0: out.append(cur.strip()); return out, j+1
            cur+=c
        elif c==',' and depth==1: out.append(cur.strip()); cur=''
        else: cur+=c
        j+=1
CTR={'LinkWord.M_SRC':'1','LinkWord.M_BUCKET':'1','LinkWord.M_AUG':'1','LinkWord.M_EXPLICIT':'1','LinkWord.M_GATHER':'1','LinkWord.M_CMEM':'2','LinkWord.M_PERIPH':'4'}
def conv(s):
    out=''; pos=0; n=0
    key='Fields.streamSrc('
    while True:
        i=s.find(key,pos)
        if i<0: out+=s[pos:]; return out,n
        a1,j=args_at(s,i+len(key)-1)
        m=re.match(r'\s*,\s*Fields\.streamDest\(',s[j:])
        if not m or len(a1)!=1: out+=s[pos:j]; pos=j; continue
        a2,k=args_at(s,j+m.end()-1)
        m2=re.match(r'\s*,\s*Fields\.streamCount\(',s[k:])
        if not m2: out+=s[pos:j]; pos=j; continue
        a3,l=args_at(s,k+m2.end()-1)
        if len(a3)!=4: out+=s[pos:j]; pos=j; continue
        cnt,mode,size,gap=a3; word,d=a2
        ctr=CTR.get(mode,'1')
        if mode=='LinkWord.M_EXPLICIT': dest='LinkWord.sramAddr(%s)'%word; mode='LinkWord.M_WRITE'
        elif mode=='LinkWord.M_CMEM': dest='Net.cm(%s)'%word; mode='LinkWord.M_WRITE'
        elif mode=='LinkWord.M_PERIPH': dest='(0x04000000 | (%s) << 2)'%word; mode='LinkWord.M_WRITE'
        else: dest='0'
        gapx='' if gap=='0' else ', '+gap
        out+=s[pos:i]+'Fields.streamSrc(%s%s), %s, Fields.streamCount(%s, %s, %s, %s, %s)'%(a1[0],gapx,dest,cnt,size,ctr,mode,d)
        pos=l; n+=1
for p in sys.argv[1:]:
    s=open(p).read(); s2,n=conv(s)
    if n: open(p,'w').write(s2); print(p,n)
