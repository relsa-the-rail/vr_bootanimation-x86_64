import struct, numpy as np, random
from unicorn import *
from unicorn.arm64_const import *
from emu_view import make
def run_tomat(t,q,s,w=0.0):
    mu=make()
    mu.mem_map(0x30000000,0x1000)
    mu.reg_write(UC_ARM64_REG_TPIDR_EL0,0x30000000)
    sp=0x7f080000
    mu.reg_write(UC_ARM64_REG_SP,sp)
    nt=0x20000000
    mu.mem_write(nt,struct.pack('<3f4f3ff',*t,*q,*s,w))
    res=0x20001000
    mu.reg_write(UC_ARM64_REG_X0,nt); mu.reg_write(UC_ARM64_REG_X8,res)
    mu.reg_write(UC_ARM64_REG_X30,0x1c000)  # sentinel in text
    mu.emu_start(0x26fd0,0x1c000)
    return np.array(struct.unpack('<16f',mu.mem_read(res,64)),dtype=np.float64)
def quat_mat(q):
    x,y,z,w=q
    return np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),0],
                     [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),0],
                     [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y),0],[0,0,0,1]])
random.seed(2); worst=0
for i in range(30):
    q=np.array([random.uniform(-1,1) for _ in range(4)]); q/=np.linalg.norm(q)
    t=[random.uniform(-3,3) for _ in range(3)]; s=[random.uniform(.2,3) for _ in range(3)]
    T=np.eye(4); T[:3,3]=t; S=np.diag(s+[1.0])
    M=T@quat_mat(q)@S
    a=run_tomat(t,q,s)
    worst=max(worst,np.abs(a-M.T.flatten()).max())
print('toMat4 vs T*R*S max diff:',worst)
