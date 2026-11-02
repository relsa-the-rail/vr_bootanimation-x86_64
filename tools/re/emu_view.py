import struct, math, random
from unicorn import *
from unicorn.arm64_const import *
from elftools.elf.elffile import ELFFile

D=open('vr_bootanimation','rb').read()
elf=ELFFile(open('vr_bootanimation','rb'))
def make():
    mu=Uc(UC_ARCH_ARM64,UC_MODE_ARM)
    for seg in elf.iter_segments():
        if seg['p_type']!='PT_LOAD': continue
        va=seg['p_vaddr']&~0xfff; end=(seg['p_vaddr']+seg['p_memsz']+0xfff)&~0xfff
        mu.mem_map(va,end-va)
        mu.mem_write(seg['p_vaddr'],D[seg['p_offset']:seg['p_offset']+seg['p_filesz']])
    mu.mem_map(0x7f000000,0x100000)   # stack
    mu.mem_map(0x20000000,0x10000)    # fake heap
    # enable FP
    mu.reg_write(UC_ARM64_REG_CPACR_EL1, 0x300000)
    return mu

def run_view(q,T,rot,eye,dt=0.0):
    mu=make()
    this=0x20000000
    def wf(a,v): mu.mem_write(a,struct.pack('<f',v))
    for i,v in enumerate(T): wf(this+0x430+4*i,v)
    for i,v in enumerate(q): wf(this+0x43c+4*i,v)  # x y z w
    mu.mem_write(this+0x30,struct.pack('<i',rot))
    sp=0x7f080000
    mu.reg_write(UC_ARM64_REG_SP,sp)
    mu.reg_write(UC_ARM64_REG_X19,this)
    mu.reg_write(UC_ARM64_REG_X29,sp)
    # replicate prologue state from 0x275e8..0x27644
    mu.mem_write(sp+8,struct.pack('<Q',this+0x44c))
    start=0x275e8
    # make hasAnimation false path irrelevant: we start after it
    mu.reg_write(UC_ARM64_REG_X8,0)
    # run prologue to loop head 0x27644 (regs set there)
    mu.emu_start(start,0x27644)
    if eye==1:
        mu.reg_write(UC_ARM64_REG_X23,1) if False else None
        mu.reg_write(UC_ARM64_REG_W23,1)
        mu.reg_write(UC_ARM64_REG_W8,0)
    try:
        mu.emu_start(0x27644,0x27940)
    except UcError as e:
        print('err',e, hex(mu.reg_read(UC_ARM64_REG_PC)))
        raise
    view=struct.unpack('<16f',mu.mem_read(sp+0x50,64))
    return view

if __name__=='__main__':
    q=(0.0,0.0,0.0,1.0)
    for eye in (0,1):
        v=run_view(q,(0,0,13),0,eye)
        print('eye',eye,[round(x,4) for x in v])
