"""Extract ordinal -> symbol from Symbian (dlltool, PE/COFF ARM) import libraries."""
import sys, struct, re
def ar_members(data):
    assert data[:8]==b'!<arch>\n'
    p=8; longnames=b''
    while p+60<=len(data):
        h=data[p:p+60]; name=h[:16].decode('latin1').strip(); size=int(h[48:58])
        body=data[p+60:p+60+size]
        if name=='//': longnames=body
        yield name, body
        p+=60+size+(size&1)
def coff(obj):
    mach,nsec,ts,symptr,nsym,opt,ch=struct.unpack_from('<HHIIIHH',obj,0)
    secs=[]
    for i in range(nsec):
        s=obj[20+opt+40*i:20+opt+40*(i+1)]
        nm=s[:8].rstrip(b'\0').decode('latin1'); size,ptr=struct.unpack_from('<II',s,16)
        secs.append((nm,obj[ptr:ptr+size] if ptr else b''))
    strtab_off=symptr+18*nsym
    syms=[]; i=0
    while i<nsym:
        e=obj[symptr+18*i:symptr+18*(i+1)]
        if e[:4]==b'\0\0\0\0':
            o=struct.unpack_from('<I',e,4)[0]; nm=obj[strtab_off+o:].split(b'\0')[0].decode('latin1')
        else: nm=e[:8].rstrip(b'\0').decode('latin1')
        val,sec,typ,cls,aux=struct.unpack_from('<IhHBB',e,8)
        syms.append((nm,sec,cls)); i+=1+aux
    return secs,syms
def ordinals(path):
    res={}
    for name,body in ar_members(open(path,'rb').read()):
        if not body.startswith(b'\xc0\x01') and not body[:2]==b'\xf0\x01':
            if len(body)<20: continue
        try: secs,syms=coff(body)
        except Exception: continue
        ordv=None
        for nm,d in secs:
            if nm.startswith('.idata$5') and len(d)>=4:
                v=struct.unpack_from('<I',d)[0]
                if v&0x80000000: ordv=v&0xffff
        if ordv is None: continue
        for nm,sec,cls in syms:
            if cls==2 and sec>0 and not nm.startswith('__imp_') and not nm.startswith('.'):
                res[ordv]=nm
    return res
if __name__=='__main__':
    for o,n in sorted(ordinals(sys.argv[1]).items()): print(o,n)
