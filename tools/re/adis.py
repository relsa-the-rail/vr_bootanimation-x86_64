import sys, struct, re, subprocess
from capstone import *
from capstone.arm64 import *
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection
import functools

D=open('vr_bootanimation','rb').read()
elf=ELFFile(open('vr_bootanimation','rb'))
dynsym=elf.get_section_by_name('.dynsym')
# GOT slot -> name
got={}
for secname in ('.rela.plt','.rela.dyn'):
    s=elf.get_section_by_name(secname)
    if s is None: continue
    try:
        for r in s.iter_relocations():
            sym=r['r_info_sym']
            if sym: got[r['r_offset']]=dynsym.get_symbol(sym).name
    except Exception as e: pass
# symbols from minidebuginfo
import os
if not os.path.exists('syms.txt'):
    out=subprocess.run(['nm','-C','--defined-only','dbg.elf'],capture_output=True,text=True).stdout
    open('syms.txt','w').write(out)
syms={}
for l in open('syms.txt'):
    p=l.rstrip('\n').split(' ',2)
    if len(p)==3 and p[1] in 'Tt':
        a=int(p[0],16)
        syms.setdefault(a,p[2])
saddr=sorted(syms)
def symname(a):
    return syms.get(a)
# plt
plt={}
base=0x83280; 
md=Cs(CS_ARCH_ARM64,CS_MODE_ARM); md.detail=True
def foff(a):
    for seg in elf.iter_segments():
        if seg['p_type']=='PT_LOAD' and seg['p_vaddr']<=a<seg['p_vaddr']+seg['p_filesz']:
            return a-seg['p_vaddr']+seg['p_offset']
    return None
def rd(a,n):
    o=foff(a); return D[o:o+n]
# parse plt entries
for a in range(0x83280+0x20,0x83280+0x1390,16):
    ins=list(md.disasm(rd(a,16),a))
    if len(ins)>=3 and ins[0].mnemonic=='adrp':
        page=ins[0].operands[1].imm
        off=ins[1].operands[1].mem.disp
        slot=page+off
        plt[a]=got.get(slot,'?%x'%slot)
def cstr(a):
    o=foff(a)
    if o is None: return None
    e=D.index(b'\0',o)
    s=D[o:e]
    if 2<=len(s)<200 and all(32<=c<127 or c in (9,10,13) for c in s): return s.decode()
def nearest(a):
    import bisect
    i=bisect.bisect_right(saddr,a)-1
    return saddr[i] if i>=0 else None
def dis(start,end=None,out=sys.stdout):
    if end is None:
        i=saddr.index(start); 
        # next different address
        j=i+1
        while j<len(saddr) and saddr[j]==start: j+=1
        end=saddr[j] if j<len(saddr) else start+0x400
    code=rd(start,end-start)
    adrp={}
    for ins in md.disasm(code,start):
        note=''
        if ins.mnemonic=='adrp':
            adrp[ins.operands[0].reg]=ins.operands[1].imm
        elif ins.mnemonic=='add' and len(ins.operands)==3 and ins.operands[2].type==ARM64_OP_IMM and ins.operands[1].type==ARM64_OP_REG and ins.operands[1].reg in adrp:
            t=adrp[ins.operands[1].reg]+ins.operands[2].imm
            s=cstr(t)
            note='; @%x'%t + (' "%s"'%s.replace('\n','\\n')[:120] if s else '')
        elif ins.mnemonic in ('bl','b') and ins.operands[0].type==ARM64_OP_IMM:
            t=ins.operands[0].imm
            n=plt.get(t) or symname(t)
            note='; '+(n if n else hex(t))
        elif ins.mnemonic=='ldr' and ins.operands[1].type==ARM64_OP_MEM and ins.operands[1].mem.base in adrp:
            t=adrp[ins.operands[1].mem.base]+ins.operands[1].mem.disp
            note='; [%x]'%t
        out.write('%x:\t%s\t%s\t%s\n'%(ins.address,ins.mnemonic,ins.op_str,note))
if __name__=='__main__':
    a=sys.argv[1]
    if a.startswith('0x'): st=int(a,16)
    else:
        m=[k for k,v in syms.items() if a in v]
        if not m: print('no sym'); sys.exit(1)
        st=sorted(m)[0]; print('#',syms[st])
    en=int(sys.argv[2],16) if len(sys.argv)>2 else None
    dis(st,en)
