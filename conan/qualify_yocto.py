#!/usr/bin/env python3
"""Build and execute one unpublished Wrynose or Scarthgap maintenance pilot."""
import argparse
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess

from check_public_dependencies import private_dependencies, verify_public_recipes

ROOT = Path(__file__).resolve().parents[1]
X86_TARGETS = ('x86_64', 'x86_64_v2', 'x86_64_v3', 'x86_64_v4')


def native_x86_features():
    rows = re.findall(r'^flags\s*:\s*(.*)$', Path('/proc/cpuinfo').read_text(), re.M)
    return set.intersection(*(set(row.split()) for row in rows)) if rows else set()


def validate_runner(arch, sdk_host, explicit=None):
    if explicit is not None:
        if arch not in X86_TARGETS:
            raise RuntimeError('An explicit runner is currently supported only for x86_64 targets')
        prefix = shlex.split(explicit)
        if not prefix or not shutil.which(prefix[0]):
            raise RuntimeError('The explicit runner executable is missing')
        return prefix
    if arch in X86_TARGETS[1:]:
        if sdk_host != 'x86_64':
            raise RuntimeError('Select a verified explicit runner for x86 ISA levels from an ARM64 SDK host')
        required = {'cx16', 'lahf_lm', 'popcnt', 'sse4_1', 'sse4_2', 'ssse3'}
        features = native_x86_features()
        if arch in ('x86_64_v3', 'x86_64_v4'):
            required.update(('avx', 'avx2', 'bmi1', 'bmi2', 'f16c', 'fma', 'movbe', 'xsave'))
            if not features.intersection(('abm', 'lzcnt')):
                required.add('lzcnt')
        if arch == 'x86_64_v4':
            required.update(('avx512f', 'avx512bw', 'avx512cd', 'avx512dq', 'avx512vl'))
        missing = required - features
        if missing:
            raise RuntimeError(f'Host lacks {sorted(missing)}; supply a verified --runner-command')
    return []


def ncurses_generation(version):
    return 'scarthgap' if version == '5.0.10' else 'wrynose'


def build_arguments(arch, libc, version, jobs, arm_exception, sdk_host='x86_64'):
    conan_arch = {**dict.fromkeys(X86_TARGETS, 'x86_64'), 'arm64': 'armv8', 'armv7hf': 'armv7hf'}[arch]
    args = ['-pr:h', 'yocto-toolchain', '-s:h', f'arch={conan_arch}',
            '-s:h', f'os.sdk=yocto-toolchain-{version}-{arch}-{libc}', '-s:b', 'compiler.cppstd=20',
            '-s:b', f'arch={"armv8" if sdk_host == "aarch64" else sdk_host}',
            '-o:b', f'yocto-toolchain/*:arch={arch}', '-o:b', f'yocto-toolchain/*:libc={libc}',
            '-c:h', f'tools.build:jobs={jobs}', '-c:b', f'tools.build:jobs={jobs}',
            '-c:h', 'tools.build.cross_building:can_run=True',
            '-c:h', 'tools.build:skip_test=False']
    options = dict(shared=False, static_plugins=True, static_libstdcxx=libc == 'musl',
                   enable_testing=True, enable_strict_warnings=True, warnings_as_errors=True,
                   build_os='linux', build_arch=arch, build_channel='dev')
    if arm_exception:
        options['ncurses_ref'] = f'ncurses/6.5@rstream/{ncurses_generation(version)}'
    for key, value in options.items():
        args += ['-o:h', f'rstream/*:{key}={value}']
    block = re.search(r'production_boost_without_components=\((.*?)\n\)',
                      (ROOT / 'build-conan-cross.sh').read_text(), re.S)
    if not block:
        raise ValueError('Cannot find the production Boost component selection')
    for component in shlex.split(block[1]):
        args += ['-o:h', f'boost/*:without_{component}=True']
    if arch == 'armv7hf' and version == '5.0.10':
        # B2 assembly does not inherit the compiler's C/C++ tuning flags.
        args += ['-o:h', 'boost/*:extra_b2_flags=asmflags=-march=armv7-a '
                 'asmflags=-mfpu=vfp asmflags=-mfloat-abi=hard']
        args += ['-c:h', 'openssl/*:user.openssl:target=linux-armv4']
    return args


def runtime_command(arch, libc, sysroot, sdk_host='x86_64', explicit=None):
    prefix = validate_runner(arch, sdk_host, explicit)
    if explicit is None and (arch in ('arm64', 'armv7hf') or sdk_host == 'aarch64'):
        emulator = {'arm64': 'qemu-aarch64', 'armv7hf': 'qemu-arm'}.get(arch, 'qemu-x86_64')
        qemu = shutil.which(emulator)
        if not qemu:
            raise RuntimeError(f'{emulator} is required for this target runtime qualification')
        cpu = ['-cpu', 'cortex-a15'] if arch == 'armv7hf' else []
        return [qemu, *cpu, '-L', str(sysroot)]
    if libc == 'glibc':
        loader = sysroot / 'lib/ld-linux-x86-64.so.2'
        if not loader.is_file():
            raise RuntimeError(f'SDK dynamic loader is missing: {loader}')
        libraries = ':'.join(str(sysroot / directory)
                             for directory in ('lib', 'usr/lib', 'lib64', 'usr/lib64'))
        return [*prefix, str(loader), '--library-path', libraries]
    return prefix


def target_sysroot(prefix, sdk_host):
    roots = prefix / 'sysroots'
    native = roots / f'{sdk_host}-pokysdk-linux'
    if not native.is_dir():
        raise RuntimeError(f'SDK host sysroot is missing: {native}')
    targets = [path for path in roots.iterdir() if path.is_dir() and path != native]
    if len(targets) != 1:
        raise RuntimeError('Expected exactly one target sysroot')
    return targets[0]


def test_cmake_arguments(runner, scale, seconds):
    variables = {}
    if runner:
        variables['CMAKE_CROSSCOMPILING_EMULATOR'] = ';'.join(runner)
    if scale != 1:
        variables['RSTREAM_TEST_TIMEOUT_SCALE'] = {'value': scale, 'cache': True, 'type': 'STRING'}
    if seconds != 120:
        variables['RSTREAM_TEST_TIMEOUT_SECONDS'] = {'value': seconds, 'cache': True, 'type': 'STRING'}
    return ['-c:h', 'tools.cmake.cmaketoolchain:extra_variables=' + json.dumps(variables)] if variables else []


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch', choices=[*X86_TARGETS, 'arm64', 'armv7hf'], required=True)
    parser.add_argument('--libc', choices=['musl', 'glibc'], required=True)
    parser.add_argument('--sdk-version', default='6.0.3')
    parser.add_argument('--sdk-host', choices=['x86_64', 'aarch64'], default='x86_64')
    parser.add_argument('--host-execution', choices=['native', 'emulated'], default='native',
                        help='Record when the entire SDK host userspace is emulated')
    parser.add_argument('--runner-command',
                        help='Verified x86 target runner prefix, e.g. "/path/sde64 -skx --"; never evaluated by a shell')
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--test-timeout-scale', type=int, default=1,
                        help='Scale test deadlines when using slow instrumentation; production deadlines stay unchanged')
    parser.add_argument('--test-timeout-seconds', type=int, default=120,
                        help='Maximum duration of each CTest test')
    parser.add_argument('--conan', default='conan')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--arm-ncurses-exception', action='store_true',
                        help='Explicitly use a scoped private ARM packaging candidate; never a library default')
    parser.add_argument('--plan', action='store_true', help='Print the package build command without running Conan')
    args = parser.parse_args()
    if args.test_timeout_scale < 1 or args.test_timeout_seconds < 1:
        parser.error('Test timeout scale and seconds must be positive integers')
    if not (re.fullmatch(r'6\.0\.\d+', args.sdk_version) or args.sdk_version == '5.0.10') or args.jobs < 1:
        parser.error('This pilot requires Yocto 6.0.x or pinned Scarthgap 5.0.10 and a positive job count')
    if args.arm_ncurses_exception and not (args.arch == 'arm64' or (
            args.arch == 'armv7hf' and args.sdk_version == '5.0.10' and args.sdk_host == 'x86_64')):
        parser.error('The ncurses packaging exception requires ARM64 or x86_64-host Scarthgap 5.0.10 ARMv7hf')
    build_args = build_arguments(args.arch, args.libc, args.sdk_version, args.jobs,
                                 args.arm_ncurses_exception, args.sdk_host)
    create = [args.conan, 'create', str(ROOT), '--build=missing', '--build=rstream/*', *build_args]
    if args.plan:
        create += test_cmake_arguments(shlex.split(args.runner_command) if args.runner_command else [],
                                       args.test_timeout_scale, args.test_timeout_seconds)
        print(shlex.join(create))
        return
    if platform.system() != 'Linux' or platform.machine() != args.sdk_host:
        parser.error(f'Run this qualification in Linux {args.sdk_host} userspace')
    try:
        validate_runner(args.arch, args.sdk_host, args.runner_command)
    except (RuntimeError, ValueError) as error:
        parser.error(str(error))
    if not os.environ.get('CONAN_HOME'):
        parser.error('Set an isolated CONAN_HOME before running the pilot')
    os.environ.setdefault('CCACHE_DIR', str(Path(os.environ['CONAN_HOME']).resolve() / 'ccache'))
    default_output = ROOT / 'out/yocto-pilot' / args.sdk_version
    if args.sdk_host != 'x86_64':
        default_output /= args.sdk_host
    output = (args.output or default_output / f'{args.arch}-{args.libc}').resolve()
    output.mkdir(parents=True, exist_ok=True)
    os.environ['LINUX_TOOLCHAIN_VERSION'] = args.sdk_version
    if args.arm_ncurses_exception:
        subprocess.run([args.conan, 'export', str(ROOT / f'conan/recipes/ncurses-{ncurses_generation(args.sdk_version)}/all'),
                        '--version', '6.5', '--user', 'rstream', '--channel', ncurses_generation(args.sdk_version)], check=True)
    graph_path = output / 'input-graph.json'
    input_lock = output / 'input.lock'
    subprocess.run([args.conan, 'graph', 'info', str(ROOT), *build_args,
                    '--format=json', '--out-file', str(graph_path),
                    '--lockfile-out', str(input_lock)], check=True)
    graph = json.loads(graph_path.read_text())
    nodes = graph['graph']['nodes']
    public = {'graph': {'nodes': {}}}
    sdk_refs = set()
    for key, node in nodes.items():
        ref = node.get('ref', '') or ''
        if node.get('name') == 'yocto-toolchain' and node.get('context') == 'build':
            sdk_refs.add(f'{ref}:{node["package_id"]}')
        if key == '0' or node.get('context') == 'build':
            continue
        if args.arm_ncurses_exception and ref.startswith(f'ncurses/6.5@rstream/{ncurses_generation(args.sdk_version)}#'):
            continue
        public['graph']['nodes'][key] = node
    violations = private_dependencies(public)
    if violations:
        raise RuntimeError('\n'.join(violations))
    verify_public_recipes(public, args.conan)
    if len(sdk_refs) != 1:
        raise RuntimeError(f'Expected one installed SDK identity, found {sorted(sdk_refs)}')
    sdk_ref = sdk_refs.pop()
    prefix = Path(subprocess.check_output([args.conan, 'cache', 'path', sdk_ref], text=True).strip())
    metadata_files = list(prefix.rglob('provenance.json'))
    if len(metadata_files) != 1:
        raise RuntimeError('Expected exactly one SDK provenance file')
    metadata = json.loads(metadata_files[0].read_text())
    for key, expected in dict(ARCH=args.arch, TCLIBC=args.libc, yocto_version=args.sdk_version,
                              SDK_ARCH=args.sdk_host).items():
        if metadata.get(key) != expected:
            raise RuntimeError(f'SDK {key} does not match {expected}')
    sysroot = target_sysroot(prefix, args.sdk_host)
    runner = runtime_command(args.arch, args.libc, sysroot, args.sdk_host, args.runner_command)
    create += test_cmake_arguments(runner, args.test_timeout_scale, args.test_timeout_seconds)
    create += ['--lockfile', str(input_lock), '--lockfile-out', str(output / 'dependencies.lock'), '--format=json',
               '--out-file', str(output / 'result.json')]
    (output / 'command.json').write_text(json.dumps({
        'command': create, 'sdk': sdk_ref, 'runtime': runner,
        'sdk_host': args.sdk_host, 'sdk_host_execution': args.host_execution,
        'target_arch': args.arch, 'explicit_runner': args.runner_command,
        'test_timeout_scale': args.test_timeout_scale,
        'test_timeout_seconds': args.test_timeout_seconds,
        'compiler_cache': os.environ['CCACHE_DIR'],
        'private_ncurses_packaging_exception': args.arm_ncurses_exception}, indent=2) + '\n')
    print(shlex.join(create), flush=True)
    subprocess.run(create, check=True,
                   env=os.environ | {'TEST_BUILD_FOLDER': str(output / 'consumer')})


if __name__ == '__main__':
    main()
