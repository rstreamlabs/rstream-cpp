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
stream_source=(root/'bin/webtty/lib/rstream/webtty/stream.cpp').read_text()
stream_source=stream_source.replace('namespace rstream {', '''static FILE* trace_file() { static FILE* file=[] { char path[128]; std::snprintf(path,sizeof(path),"conpty-diagnostic/stream-%lu.log",::GetCurrentProcessId()); FILE* result=nullptr; ::fopen_s(&result,path,"w"); return result?result:stderr; }(); return file; }
namespace rstream {''',1)
stream_source=stream_source.replace('#include <algorithm>','#include <algorithm>\n#include <cstdio>')
for anchor,label in [('  cancel_thread_io(reading_thread);','stop_cancel_read'),('  cancel_thread_io(writing_thread);','stop_cancel_write'),('  close_handle(in_write);','stop_close_input'),('  close_handle(out_read);','stop_close_output'),('    ::ClosePseudoConsole(console);','close_pseudoconsole'),('    reading_thread->join();','join_read'),('    writing_thread->join();','join_write'),('    if (!::ReadFile(out_read,','read_enter'),('    if (!::WriteFile(in_write,','write_enter')]:
    stream_source=stream_source.replace(anchor,'  std::fprintf(stderr, "STREAM '+label+' pid=%lu tid=%lu\\n", ::GetCurrentProcessId(), ::GetCurrentThreadId()); std::fflush(stderr);\n'+anchor)
stream_source=stream_source.replace('std::fprintf(stderr,','std::fprintf(trace_file(),').replace('std::fflush(stderr)','std::fflush(trace_file())')
(out/'stream.cpp').write_text(stream_source)
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
rows=[]
for attempt in range(1,101):
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
