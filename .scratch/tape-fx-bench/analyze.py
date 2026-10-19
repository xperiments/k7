import os, re, subprocess, sys

def instructions(o):
    dis = subprocess.run([os.environ["OD"],'-d','--no-show-raw-insn',o],capture_output=True,text=True).stdout
    ins=[]; started=False
    for l in dis.splitlines():
        if '<tape_fx_process_s16>:' in l: started=True; continue
        if started:
            m=re.match(r'\s*([0-9a-f]+):\s+(\S+)\s*(.*)',l)
            if m: ins.append((int(m.group(1),16),m.group(2),m.group(3)))
            elif re.match(r'^[0-9a-f]+ <',l): break
    return ins

def loops(ins):
    addr=[a for a,_,_ in ins]
    idx={a:i for i,a in enumerate(addr)}
    found=[]
    for i,(a,op,rest) in enumerate(ins):
        if op.startswith('loop'):
            m=re.search(r'([0-9a-f]{3,})',rest)
            if m:
                t=int(m.group(1),16)
                if t in idx: found.append((idx[a]+1, idx[t]-1, 'loop', a))
        elif op[0] in 'jb':
            m=re.search(r'([0-9a-f]{3,})',rest)
            if m:
                t=int(m.group(1),16)
                if t in idx and t<a: found.append((idx[t], idx[a], 'branch', a))
    return found, addr

for o in sys.argv[1:]:
    ins=instructions(o)
    found,addr=loops(ins)
    spans=[]
    for s,e,kind,a in found:
        if 0<=s<=e<len(addr): spans.append((e-s+1,s,e,kind))
    spans.sort()
    if spans:
        inner=spans[0]
        outer=max(spans,key=lambda x:x[0])
        if outer is inner:
            per=inner[0]
        else:
            per=(outer[0]-inner[0]) + 2*inner[0]
        print(f"{o:24s} fn {len(ins):4d}  outer {outer[0]:4d}  inner {inner[0]:4d}  per stereo frame {per:4d}")
    else:
        print(f"{o:24s} fn {len(ins):4d}  no loops")
