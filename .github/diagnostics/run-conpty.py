import json
import pathlib
import subprocess
import tarfile
import time
import urllib.request

root=pathlib.Path.cwd()
out=root/'conpty-diagnostic'
out.mkdir(exist_ok=True)
archive=out/'boost.tar.gz'
print('Download Boost1.83 sources',flush=True)
with urllib.request.urlopen('https://archives.boost.io/release/1.83.0/source/boost_1_83_0.tar.gz',timeout=90) as response,archive.open('wb') as dst:
    import shutil
    shutil.copyfileobj(response,dst)
with tarfile.open(archive) as src:
    for member in src:
        if member.isfile() and (member.name.startswith('boost_1_83_0/boost/') or member.name.startswith('boost_1_83_0/libs/filesystem/src/')):
            src.extract(member,out,filter='data')
boost=out/'boost_1_83_0'
(out/'rstream').mkdir(exist_ok=True)
(out/'rstream/config.hpp').write_text('#pragma once\n')
(out/'stream.cpp').write_text((root/'bin/webtty/lib/rstream/webtty/stream.cpp').read_text())
flags=['/nologo','/c','/std:c++20','/EHsc','/GR','/MD','/O2','/Ob2','/Gy','/Zc:preprocessor','/W4','/WX','/utf-8','/D_WIN32_WINNT=0x0A00','/DNTDDI_VERSION=0x0A000006','/DBOOST_ALL_NO_LIB','/DBOOST_ASIO_NO_DEPRECATED','/external:W0','/external:I'+str(boost),'/I'+str(root/'lib'),'/I'+str(root/'bin/webtty/lib'),'/I'+str(root/'bin/webtty/lib/rstream/webtty'),'/I'+str(out)]
files=[root/'.github/diagnostics/conpty.cpp',out/'stream.cpp',root/'bin/webtty/lib/rstream/webtty/error.cpp',root/'bin/webtty/lib/rstream/webtty/detail/process.cpp']
objects=[]
for index,file in enumerate(files):
    obj=out/f'{index}.obj';objects.append(str(obj))
    command=['cl',*flags,'/Fo'+str(obj),str(file)]
    (out/f'{index}.command.json').write_text(json.dumps(command))
    subprocess.run(command,check=True,timeout=180)
for file in ['path.cpp','path_traits.cpp','codecvt_error_category.cpp','windows_file_codecvt.cpp']:
    obj=out/(file+'.obj');objects.append(str(obj))
    subprocess.run(['cl','/nologo','/c','/std:c++20','/EHsc','/MD','/O2','/Gy','/DBOOST_ALL_NO_LIB','/DBOOST_FILESYSTEM_STATIC_LINK','/I'+str(boost),'/Fo'+str(obj),str(boost/'libs/filesystem/src'/file)],check=True,timeout=90)
binary=out/'conpty.exe'
subprocess.run(['link','/OUT:'+str(binary),'/OPT:REF',*objects,'ws2_32.lib','mswsock.lib','advapi32.lib','userenv.lib','shell32.lib','DbgHelp.lib'],check=True,timeout=90)
unit_source=root/'test/core/common/test_core_windows_cancel_io.cpp'
unit_binary=out/'cancel-io.exe'
subprocess.run(['cl',*flags,'/Fo'+str(out/'cancel-io.obj'),str(unit_source)],check=True,timeout=90)
subprocess.run(['link','/OUT:'+str(unit_binary),str(out/'cancel-io.obj')],check=True,timeout=90)
for attempt in range(20):subprocess.run([str(unit_binary)],check=True,timeout=15)
core_objects=[]
for index,file in enumerate([root/'lib/rstream/core/windows/blocking_handle.cpp',root/'test/core/common/test_core_windows_blocking_handle.cpp']):
    obj=out/f'core-{index}.obj';core_objects.append(str(obj))
    subprocess.run(['cl',*flags,'/Fo'+str(obj),str(file)],check=True,timeout=90)
core_binary=out/'blocking-handle.exe'
subprocess.run(['link','/OUT:'+str(core_binary),*core_objects,'ws2_32.lib','mswsock.lib'],check=True,timeout=90)
for attempt in range(20):subprocess.run([str(core_binary)],check=True,timeout=15)
(out/'unit-results.json').write_text(json.dumps({'deterministic_cancel_io':20,'blocking_handle_runtime':20,'passed':True},indent=2))
# Qualify late cancellation with every application/Boost-header translation unit instrumented.
asan_flags=[*flags,'/fsanitize=address','/Zi']
asan_objects=[]
for index,file in enumerate([root/'lib/rstream/core/windows/blocking_handle.cpp',root/'test/core/common/test_core_windows_blocking_handle.cpp']):
    obj=out/f'asan-{index}.obj';asan_objects.append(str(obj))
    subprocess.run(['cl',*asan_flags,'/Fo'+str(obj),'/Fd'+str(out/f'asan-{index}.pdb'),str(file)],check=True,timeout=90)
asan_binary=out/'blocking-handle-asan.exe'
subprocess.run(['link','/OUT:'+str(asan_binary),'/INFERASANLIBS','/DEBUG',*asan_objects,'ws2_32.lib','mswsock.lib'],check=True,timeout=90)
for attempt in range(20):subprocess.run([str(asan_binary)],check=True,timeout=15)
old_source=out/'old-blocking-handle.cpp'
old= (root/'lib/rstream/core/windows/blocking_handle.cpp').read_text().replace('std::make_shared<operation>(', 'std::allocate_shared<operation>(boost::asio::get_associated_allocator(handler), ')
old_source.write_text(old)
old_obj=out/'old-allocator.obj'
subprocess.run(['cl',*asan_flags,'/I'+str(root/'lib/rstream/core/windows'),'/Fo'+str(old_obj),'/Fd'+str(out/'old-allocator.pdb'),str(old_source)],check=True,timeout=90)
old_binary=out/'old-allocator-asan.exe'
subprocess.run(['link','/OUT:'+str(old_binary),'/INFERASANLIBS','/DEBUG',str(old_obj),asan_objects[1],'ws2_32.lib','mswsock.lib'],check=True,timeout=90)
before=subprocess.run([str(old_binary)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=15)
(out/'old-allocator-asan.log').write_text(before.stdout)
assert before.returncode != 0 and 'AddressSanitizer' in before.stdout, before.stdout
(out/'asan-results.json').write_text(json.dumps({'before_exit':before.returncode,'after_repetitions':20,'passed':True},indent=2))
rows=[]
for attempt in range(1,2001):
    started=time.monotonic()
    log=out/f'{attempt}.log'
    with log.open('w') as dst:
        try:
            run=subprocess.run([str(binary)],stdout=dst,stderr=subprocess.STDOUT,timeout=30)
            code=run.returncode
        except subprocess.TimeoutExpired:
            code='timeout'
    row={'attempt':attempt,'exit':code,'seconds':time.monotonic()-started}
    rows.append(row)
    (out/'results.json').write_text(json.dumps(rows,indent=2))
    print(json.dumps(row),flush=True)
    if code:
        print(log.read_text(),flush=True)
        raise SystemExit(1)
    for pattern in ['trace-*.log','stream-*.log']:
        for path in out.glob(pattern):path.unlink()
