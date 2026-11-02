#!/usr/bin/env python3
"""Execute the actual AArch64 fault-island instructions with a mocked HOS kernel.

This is instruction/ABI verification, never Switch hardware execution. The real
libnx SVC wrappers are linked; only kernel transfer/return is simulated here.
"""
import json,os,pathlib,struct,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'.deps/openjdk25-emulator'))
from unicorn import Uc, UcError, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_MEM_READ_UNMAPPED, UC_HOOK_MEM_READ_PROT, UC_HOOK_CODE, UC_HOOK_INTR
from unicorn.arm64_const import *
dk=pathlib.Path(os.environ.get('DEVKITPRO',root/'.toolchain/opt/devkitpro'))
cc=str(dk/'devkitA64/bin/aarch64-none-elf-gcc');nm=str(dk/'devkitA64/bin/aarch64-none-elf-nm')
out=root/'build/openjdk25-faults';out.mkdir(exist_ok=True)
port=root/'ports/openjdk25/src/hotspot/os/horizon'
objects=[]
for source in [port/'safefetch_horizon.S',port/'horizon_fault_abi.c']:
 obj=out/(source.stem+'.o')
 subprocess.run([cc,'-march=armv8-a+crc+crypto','-I'+str(dk/'libnx/include'),'-c',str(source),'-o',str(obj)],check=True)
 objects.append(str(obj))
def target(production):
 mode='production' if production else 'boundary'
 sources=[root/'tests/openjdk25_fault_emulator_stubs.S']
 if production:sources.append(port/'exception_fallback_horizon.S')
 extra=[]
 for source in sources:
  obj=out/(source.stem+'-'+mode+'.o')
  subprocess.run([cc,*(['-DPRODUCTION_FALLBACK'] if production else []),'-c',str(source),'-o',str(obj)],check=True)
  extra.append(str(obj))
 elf=out/('fault-island-'+mode+'.elf')
 subprocess.run([cc,'-nostdlib','-Wl,-Ttext=0x100000','-Wl,-e,SafeFetch32_impl',*objects,*extra,'-L'+str(dk/'libnx/lib'),'-lnx','-o',str(elf)],check=True)
 symbols={}
 for line in subprocess.check_output([nm,str(elf)],text=True).splitlines():
  fields=line.split()
  if len(fields)==3:symbols[fields[2]]=int(fields[0],16)
 blob=elf.read_bytes();segments=[]
 phoff=struct.unpack_from('<Q',blob,32)[0];phentsize,phnum=struct.unpack_from('<HH',blob,54)
 for i in range(phnum):
  kind,flags,offset,address,physical,filesize,memsize,align=struct.unpack_from('<IIQQQQQQ',blob,phoff+i*phentsize)
  if kind==1 and memsize:segments.append((address,blob[offset:offset+filesize],memsize))
 return symbols,segments
REGS=[globals()['UC_ARM64_REG_X'+str(i)] for i in range(29)]+[UC_ARM64_REG_X29,UC_ARM64_REG_X30]
FRAME,STACK,DATA,EXIT=0x400000,0x500000,0x600000,0x900000
passed=0

def run(width=64, valid=False, protected=False, direct=False, descriptor=0x101, esr=0x24<<26,pstate=0,pc_delta=0,debug=0,ignore=0,info_error=0,null_frame=False):
 global passed
 uc=Uc(UC_ARCH_ARM64,UC_MODE_ARM)
 low=min(a for a,_,_ in segments)&~4095
 high=(max(a+n for a,_,n in segments)+4095)&~4095
 uc.mem_map(low,high-low)
 for address,data,size in segments:uc.mem_write(address,data)
 for address,size in [(FRAME,0x1000),(STACK,0x10000),(DATA,0x1000),(EXIT,0x1000)]:uc.mem_map(address,size)
 uc.mem_write(symbols['__nx_exception_ignoredebug'],struct.pack('<I',ignore))
 original=[0x12345000+i*0x100 for i in range(31)]
 original[0]=DATA if valid or protected else 0x700000
 original[1]=0xfedcba9876543210;original[30]=EXIT
 for register,value in zip(REGS,original):uc.reg_write(register,value)
 uc.reg_write(UC_ARM64_REG_SP,STACK+0x8000);uc.reg_write(UC_ARM64_REG_PSTATE,0)
 vectors=[(0x1000000000000000000+i) for i in range(32)]
 for i,value in enumerate(vectors):uc.reg_write(globals()['UC_ARM64_REG_Q'+str(i)],value)
 value=0x89abcdef if width==32 else 0x0123456789abcdef
 uc.mem_write(DATA,struct.pack('<Q',value))
 if protected:uc.mem_protect(DATA,0x1000,0)
 name='32' if width==32 else 'N'; fault=symbols['js_horizon_safefetch'+name+'_fault']
 frame=struct.pack('<9Q3Q4IQ',*original[:9],EXIT,STACK+0x8000,fault+pc_delta,pstate,0,0,esr,original[0])
 uc.mem_write(FRAME,frame)
 state={'fault':False,'recovered':False,'unhandled':False,'exit':False,'info':0,'callback':False}
 def invalid(machine,access,address,size,value,user):state['fault']=True;return False
 def code(machine,address,size,user):
  if address==EXIT:state['exit']=True;machine.emu_stop()
 def interrupt(machine,number,user):
  pc=machine.reg_read(UC_ARM64_REG_PC)
  instruction=struct.unpack('<I',machine.mem_read(pc-4,4))[0];svc=(instruction>>5)&0xffff
  if svc==0x29:
   state['info']+=1
   assert machine.reg_read(UC_ARM64_REG_X1)==8
   machine.reg_write(UC_ARM64_REG_X0,info_error);machine.reg_write(UC_ARM64_REG_X1,debug)
   # Adversarial syscall clobbers: stock libnx saves x9..21 for this call.
   for i in range(3,22):machine.reg_write(REGS[i],0xbad000+i)
  elif svc==0x28:
   status=machine.reg_read(UC_ARM64_REG_X0)
   if status:
    assert production and status==0xf801
    assert bytes(machine.mem_read(FRAME,0x78))==frame
    state['unhandled']=True;machine.emu_stop();return
   saved=machine.mem_read(FRAME,0x78)
   expected=(symbols['js_horizon_unhandled_exception_returnentry'] if production and not should_recover
             else symbols['js_horizon_safefetch'+name+'_continuation'])
   assert struct.unpack_from('<Q',saved,0x58)[0]==expected
   expected_frame=bytearray(frame)
   struct.pack_into('<Q',expected_frame,0,symbols['__nx_exceptiondump'] if production and not should_recover else original[1])
   if production and not should_recover:
    struct.pack_into('<Q',expected_frame,0x50,symbols['__nx_exception_stack']+0x1000)
   struct.pack_into('<Q',expected_frame,0x58,expected)
   assert bytes(saved)==bytes(expected_frame), 'unexpected saved-context modification'
   for i in range(9):machine.reg_write(REGS[i],struct.unpack_from('<Q',saved,i*8)[0])
   machine.reg_write(UC_ARM64_REG_X30,struct.unpack_from('<Q',saved,0x48)[0])
   machine.reg_write(UC_ARM64_REG_SP,struct.unpack_from('<Q',saved,0x50)[0])
   machine.reg_write(UC_ARM64_REG_PSTATE,struct.unpack_from('<I',saved,0x60)[0])
   machine.reg_write(UC_ARM64_REG_PC,expected)
   if should_recover:state['recovered']=True
  elif svc in [0x7f,0x7e]:
   assert not production
   assert machine.reg_read(UC_ARM64_REG_X0)==descriptor
   assert machine.reg_read(UC_ARM64_REG_X1)==(0 if null_frame else FRAME)
   assert bytes(machine.mem_read(FRAME,0x78))==frame
   state['unhandled']=True;machine.emu_stop()
  elif svc==0x7d:
   assert production and not should_recover
   assert machine.reg_read(UC_ARM64_REG_X0)==symbols['__nx_exceptiondump']
   dump=bytes(machine.mem_read(symbols['__nx_exceptiondump'],0x338))
   expected_dump=struct.pack('<4I33Q',descriptor,0,0,0,*original[:29],original[29],EXIT,STACK+0x8000,fault+pc_delta)
   expected_dump+=bytes(8)+b''.join(value.to_bytes(16,'little') for value in vectors)
   expected_dump+=struct.pack('<4IQ',pstate,0,0,esr,original[0])
   assert dump==expected_dump,'production fallback dump mismatch'
   state['callback']=True
  elif svc==0x26:
   assert production and state['callback']
   assert all(machine.reg_read(REGS[i])==0 for i in range(3))
   state['unhandled']=True;machine.emu_stop()
  else:raise AssertionError('unexpected SVC '+hex(svc))
 uc.hook_add(UC_HOOK_MEM_READ_UNMAPPED|UC_HOOK_MEM_READ_PROT,invalid)
 uc.hook_add(UC_HOOK_CODE,code);uc.hook_add(UC_HOOK_INTR,interrupt)
 start=symbols['SafeFetch32_impl' if width==32 else 'SafeFetchN_impl']
 if direct:
  uc.reg_write(UC_ARM64_REG_X0,descriptor);uc.reg_write(UC_ARM64_REG_X1,0 if null_frame else FRAME)
  start=symbols['__libnx_exception_entry']
 should_recover=(descriptor==0x101 and not null_frame and not(pstate&0x10) and esr>>26==0x24 and not(esr&64) and pc_delta==0 and not info_error and (not debug or ignore))
 try:uc.emu_start(start,0,count=10000)
 except UcError:
  assert state['fault'] and not direct and not valid
  assert uc.reg_read(UC_ARM64_REG_PC)==fault
  uc.reg_write(UC_ARM64_REG_X0,descriptor);uc.reg_write(UC_ARM64_REG_X1,FRAME)
  uc.emu_start(symbols['__libnx_exception_entry'],0,count=10000)
 if valid and not direct:
  assert state['exit'] and not state['recovered'];expected=value
 elif should_recover:
  assert state['exit'] and state['recovered'] and not state['unhandled'];expected=original[1]
 else:
  assert state['unhandled'] and not state['recovered'] and not state['exit'];expected=None
 if expected is not None:
  mask=(1<<width)-1
  assert uc.reg_read(UC_ARM64_REG_X0)&mask==expected&mask
 for i in range(9,30):assert uc.reg_read(REGS[i])==original[i],('register clobbered',i)
 for i,value in enumerate(vectors):assert uc.reg_read(globals()['UC_ARM64_REG_Q'+str(i)])==value
 assert uc.reg_read(UC_ARM64_REG_SP)==(symbols['__nx_exception_stack']+0x1000 if state['callback'] else STACK+0x8000)
 if info_error or debug:
  assert state['info']==(1 if production or pc_delta==0 else 0),'debug policy must not retry a failed/rejected query'
 passed+=1

for production in [False,True]:
 symbols,segments=target(production)
 for width in [32,64]:
  run(width,valid=True);run(width);run(width,protected=True)
  for kwargs in [dict(descriptor=0x100),dict(esr=0x25<<26),dict(esr=(0x24<<26)|64),dict(pstate=0x10),dict(pc_delta=4),dict(pc_delta=1),dict(debug=1),dict(debug=1,ignore=1),dict(info_error=0xf601),dict(info_error=0xf601,ignore=1),dict(null_frame=True)]:
   run(width,direct=True,**kwargs)
 # Also check query rejection inside the actual production fallback.
 for kwargs in [dict(pc_delta=4,debug=1),dict(pc_delta=4,info_error=0xf601)]:run(direct=True,**kwargs)
print(f'EMULATOR-PASSED: {passed} actual AArch64 fault-island/boundary/production-fatal cases; kernel transfer/return mocked; Switch NOT TESTED')
