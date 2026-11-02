import numpy as np, random, math
from emu_view import run_view
def qrot(q,v):
    x,y,z,w=q; 
    R=np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],
                [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],
                [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
    return R@np.array(v,dtype=np.float64)
def view(q,T,eye):
    e=-1.0 if eye==0 else 1.0
    eyep=np.array(T)+qrot(q,(e*0.25,0,0))
    f=qrot(q,(0,0,-1)); up=qrot(q,(0,1,0))
    f/=np.linalg.norm(f); 
    s=np.cross(f,up); s/=np.linalg.norm(s); u=np.cross(s,f)
    M=np.eye(4)
    M[0,:3]=s; M[1,:3]=u; M[2,:3]=-f
    M[0,3]=-s@eyep; M[1,3]=-u@eyep; M[2,3]=f@eyep
    return M.T.flatten()  # column-major
random.seed(1)
worst=0
for i in range(25):
    q=np.array([random.uniform(-1,1) for _ in range(4)]); q/=np.linalg.norm(q)
    T=[random.uniform(-5,5) for _ in range(3)]
    for eye in (0,1):
        a=np.array(run_view(tuple(q),tuple(T),0,eye),dtype=np.float64)
        b=view(q,T,eye)
        worst=max(worst,np.abs(a-b).max())
print('max abs diff over 50 random cams:',worst)
