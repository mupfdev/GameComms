import sys,re
L=open(sys.argv[1]).read().split('\n')
out=[];i=0
names={'EUSER_1148':'TRAP','EUSER_1164':'UNTRAP','EUSER_809':'PANIC','EUSER_3':'new','EUSER_858':'PushL','EUSER_828':'Pop','EUSER_1504':'delete','EUSER_630':'Leave','EUSER_119':'RPtrArr[]','EUSER_266':'RPtrArr.Count','EUSER_116':'RArr[]','EUSER_1283':'TBuf16(max)','EUSER_246':'TDes8Copy?','EUSER_251':'TDes16Copy?','EUSER_13':'CActiveScheduler::Add','EUSER_1395':'CActive::CActive','EUSER_1265':'CActive::~CActive','EUSER_979':'SetActive','EUSER_926':'User::RequestComplete','EUSER_1262':'CBase::~CBase'}
while i<len(L):
    l=L[i]
    m=re.match(r'(\w+) ldr r12, \[(r\d+), #(0x[0-9a-f]+)\]',l)
    if m and i+2<len(L) and 'mov lr, pc' in L[i+1] and 'bx r12' in L[i+2]:
        out.append(f'{m.group(1)} VCALL {m.group(2)}->+{m.group(3)}'); i+=3; continue
    m=re.match(r'(\w+) ldr(\w\w) r12, \[(r\d+), #(0x[0-9a-f]+)\]',l)
    if m and i+2<len(L) and 'lr, pc' in L[i+1] and 'bx' in L[i+2]:
        out.append(f'{m.group(1)} VCALL{m.group(2)} {m.group(3)}->+{m.group(4)}'); i+=3; continue
    for k,v in names.items(): l=l.replace('<'+k+'>','<'+v+'>')
    out.append(l); i+=1
open(sys.argv[2],'w').write('\n'.join(out))
