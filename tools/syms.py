import struct,sys
d=open(sys.argv[1],'rb').read()
_,t,dt,b,s=struct.unpack('>HIIII',d[:18])
off=28+t+dt
syms=[]
for i in range(s//14):
    e=d[off+i*14:off+i*14+14]
    n=e[:8].rstrip(b'\0').decode('latin1'); ty,v=struct.unpack('>HI',e[8:])
    syms.append((v,ty,n))
for v,ty,n in sorted(syms): print('%06x %04x %s'%(v,ty,n))
print('text %x data %x bss %x'%(t,dt,b))
