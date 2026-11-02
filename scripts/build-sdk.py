#!/usr/bin/env python3
import json,os,pathlib,subprocess
ROOT=pathlib.Path(__file__).resolve().parents[1];os.chdir(ROOT)
dk=pathlib.Path(os.environ.get('DEVKITPRO',str(ROOT/'.toolchain/opt/devkitpro')))
jdk=next((ROOT/'.deps/host-jdk25').glob('*/include'))
common=['-O2','-g','-ffunction-sections','-fdata-sections','-Iinclude','-I.deps/minijvm/jvm','-I.deps/minijvm/utils','-I'+str(jdk),'-I'+str(jdk/'linux')]
sources=['runtime','runtime_minijvm','runtime_openjdk','runtime_services','native_resolver']
for host in [False,True]:
    dest=ROOT/'build'/('sdk-host' if host else 'sdk');dest.mkdir(parents=True,exist_ok=True)
    cc='gcc' if host else str(dk/'devkitA64/bin/aarch64-none-elf-gcc')
    flags=common+([] if host else ['-D__SWITCH__','-march=armv8-a+crc+crypto','-mtune=cortex-a57','-mno-outline-atomics','-mtp=soft','-fPIE','-I'+str(dk/'libnx/include')])
    objects=[]
    for name in sources:
        obj=dest/(name+'.o');subprocess.run([cc,*flags,'-c','src/'+name+'.c','-o',str(obj)],check=True);objects.append(obj)
    objects+=sorted((ROOT/'build'/('minijvm-host-objects' if host else 'minijvm')).glob('*.o'))
    if not host:
        vmem_obj=dest/'vmem_horizon.o'
        subprocess.run([cc,*flags,'-c','src/vmem_horizon.c','-o',str(vmem_obj)],check=True)
        objects.append(vmem_obj)
        ffi_obj=dest/'ffi_horizon.o'
        subprocess.run([cc,*flags,'-Ibuild/libffi/include','-c','src/ffi_horizon.c','-o',str(ffi_obj)],check=True)
        objects.append(ffi_obj)
        ffi_members=dest/'ffi-members';ffi_members.mkdir(exist_ok=True)
        subprocess.run([str(dk/'devkitA64/bin/aarch64-none-elf-ar'),'x',str(ROOT/'build/libffi-horizon.a')],cwd=ffi_members,check=True)
        objects+=sorted(ffi_members.glob('*.o'))
    ar='ar' if host else str(dk/'devkitA64/bin/aarch64-none-elf-ar')
    subprocess.run([ar,'rcs',str(dest/'libjavaswitch.a'),*map(str,objects)],check=True)
    if host:
        server=jdk.parent/'lib/server'
        subprocess.run(['gcc',*common,'tests/jni_host.c',str(dest/'libjavaswitch.a'),'-Wl,--no-as-needed','-L'+str(server),'-Wl,-rpath,'+str(server),'-ljvm','-pthread','-lm','-o','build/jni25-host'],check=True)
        subprocess.run(['gcc',*common,'tests/runtime_gate.c',str(dest/'libjavaswitch.a'),'-pthread','-lm','-o','build/runtime-gate-host'],check=True)
(ROOT/'build/sdk/runtime-contract.json').write_text(json.dumps({'abi':1,'target':'aarch64-horizon-libnx','built_backends':{'minijvm8':True,'openjdk25':False},'required_vm_symbol':'JNI_CreateJavaVM','classpath_separator':';','standard_jni_requires_linked_vm':True,'libffi':'AAPCS64 and libnx W^X closures compiled; hardware NOT TESTED'},indent=2)+'\n')
print('Reusable Horizon SDK and real host JNI invocation adapter built')
