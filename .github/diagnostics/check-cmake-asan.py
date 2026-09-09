import ast
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import urllib.request

from conan.tools.cmake.utils import parse_extra_variable

root = Path.cwd()
out = root / 'conpty-diagnostic'
out.mkdir(exist_ok=True)
subprocess.run(['python', 'test/test_conan_windows_asan.py'], check=True)
archive = out / 'boost.tar.gz'
with urllib.request.urlopen('https://archives.boost.io/release/1.83.0/source/boost_1_83_0.tar.gz', timeout=90) as response, archive.open('wb') as dst:
    shutil.copyfileobj(response, dst)
with tarfile.open(archive) as src:
    for member in src:
        if member.isfile() and member.name.startswith('boost_1_83_0/boost/'):
            src.extract(member, out, filter='data')
source = out / 'source'
source.mkdir(exist_ok=True)
for folder in ['lib', 'test/core/common', 'test/support', 'cmake']:
    shutil.copytree(root / folder, source / folder, dirs_exist_ok=True)
(source / 'lib/rstream/config.hpp').write_text('#pragma once\n')
boost = (out / 'boost_1_83_0').as_posix()
(source / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.10)
project(rstream LANGUAGES CXX)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_library(Boost::boost INTERFACE IMPORTED)
set_property(TARGET Boost::boost PROPERTY INTERFACE_INCLUDE_DIRECTORIES "''' + boost + '''")
add_library(rstream::core INTERFACE IMPORTED)
add_library(rstream::io INTERFACE IMPORTED)
set(ENABLE_STATIC_PLUGINS ON)
add_compile_definitions(BOOST_ALL_NO_LIB BOOST_ASIO_NO_DEPRECATED _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000006)
add_compile_options(/EHsc /W4 /WX /Zc:preprocessor)
include(cmake/tests.cmake)
add_subdirectory(test/core/common)
''')
workflow = (root / '.github/workflows/conan.yml').read_text()
requested = ast.literal_eval(re.search(r'extra_variables=(\{[^\n]+\})"', workflow).group(1))['RSTREAM_TEST_WINDOWS_PIPE_ASAN']
results = {}
for name, value in [('before', True), ('after', requested)]:
    variable = parse_extra_variable('tools.cmake.cmaketoolchain:extra_variables', 'RSTREAM_TEST_WINDOWS_PIPE_ASAN', value)
    toolchain = out / (name + '.cmake')
    toolchain.write_text('set(RSTREAM_TEST_WINDOWS_PIPE_ASAN ' + str(variable) + ')\n')
    build = out / name
    configured = subprocess.run(['cmake', '-S', str(source), '-B', str(build), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_TOOLCHAIN_FILE=' + toolchain.as_posix()], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (out / (name + '-configure.log')).write_text(configured.stdout)
    print(configured.stdout, flush=True)
    assert configured.returncode == 0
    target = 'rstream-test-core-windows-blocking-handle-asan'
    compiled = subprocess.run(['cmake', '--build', str(build), '--target', target], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=180)
    (out / (name + '-build.log')).write_text(compiled.stdout)
    print(compiled.stdout, flush=True)
    results[name] = {'build_exit': compiled.returncode}
    if name == 'before':
        assert compiled.returncode != 0 and 'unknown target' in compiled.stdout
        continue
    assert compiled.returncode == 0
    report = out / 'required-test.xml'
    tested = subprocess.run(['ctest', '--test-dir', str(build), '-R', '^' + target + '$', '--output-on-failure', '--no-tests=error', '--output-junit', str(report)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=150)
    (out / 'after-test.log').write_text(tested.stdout)
    print(tested.stdout, flush=True)
    assert tested.returncode == 0
    spec = importlib.util.spec_from_file_location('recipe', root / 'conanfile.py')
    recipe = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(recipe)
    recipe.ConanPackage.verify_windows_pipe_asan_result(report)
    results[name]['required_junit_passed'] = True
(out / 'cmake-results.json').write_text(json.dumps(results, indent=2))
