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


def ncurses_generation(version):
    return 'scarthgap' if version == '5.0.10' else 'wrynose'


def build_arguments(arch, libc, version, jobs, arm_exception, sdk_host='x86_64'):
    args = ['-pr:h', 'yocto-toolchain', '-s:h', f'arch={"armv8" if arch == "arm64" else arch}',
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
    return args


def runtime_command(arch, libc, sysroot, sdk_host='x86_64'):
    if arch == 'arm64' or sdk_host == 'aarch64':
        emulator = 'qemu-aarch64' if arch == 'arm64' else 'qemu-x86_64'
        qemu = shutil.which(emulator)
        if not qemu:
            raise RuntimeError(f'{emulator} is required for this target runtime qualification')
        return [qemu, '-L', str(sysroot)]
    if libc == 'glibc':
        loader = sysroot / 'lib/ld-linux-x86-64.so.2'
        if not loader.is_file():
            raise RuntimeError(f'SDK dynamic loader is missing: {loader}')
        libraries = ':'.join(str(sysroot / directory)
                             for directory in ('lib', 'usr/lib', 'lib64', 'usr/lib64'))
        return [str(loader), '--library-path', libraries]
    return []


def target_sysroot(prefix, sdk_host):
    roots = prefix / 'sysroots'
    native = roots / f'{sdk_host}-pokysdk-linux'
    if not native.is_dir():
        raise RuntimeError(f'SDK host sysroot is missing: {native}')
    targets = [path for path in roots.iterdir() if path.is_dir() and path != native]
    if len(targets) != 1:
        raise RuntimeError('Expected exactly one target sysroot')
    return targets[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch', choices=['x86_64', 'arm64'], required=True)
    parser.add_argument('--libc', choices=['musl', 'glibc'], required=True)
    parser.add_argument('--sdk-version', default='6.0.3')
    parser.add_argument('--sdk-host', choices=['x86_64', 'aarch64'], default='x86_64')
    parser.add_argument('--host-execution', choices=['native', 'emulated'], default='native',
                        help='Record when the entire SDK host userspace is emulated')
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--conan', default='conan')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--arm-ncurses-exception', action='store_true',
                        help='Explicitly use the private ARM64 packaging candidate; never a library default')
    parser.add_argument('--plan', action='store_true', help='Print the package build command without running Conan')
    args = parser.parse_args()
    if not (re.fullmatch(r'6\.0\.\d+', args.sdk_version) or args.sdk_version == '5.0.10') or args.jobs < 1:
        parser.error('This pilot requires Yocto 6.0.x or pinned Scarthgap 5.0.10 and a positive job count')
    if args.arm_ncurses_exception and args.arch != 'arm64':
        parser.error('The ncurses packaging exception is limited to ARM64')
    build_args = build_arguments(args.arch, args.libc, args.sdk_version, args.jobs,
                                 args.arm_ncurses_exception, args.sdk_host)
    create = [args.conan, 'create', str(ROOT), '--build=missing', '--build=rstream/*', *build_args]
    if args.plan:
        print(shlex.join(create))
        return
    if platform.system() != 'Linux' or platform.machine() != args.sdk_host:
        parser.error(f'Run this qualification in Linux {args.sdk_host} userspace')
    if not os.environ.get('CONAN_HOME'):
        parser.error('Set an isolated CONAN_HOME before running the pilot')
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
    runner = runtime_command(args.arch, args.libc, sysroot, args.sdk_host)
    if runner:
        create += ['-c:h', 'tools.cmake.cmaketoolchain:extra_variables=' + json.dumps({
            'CMAKE_CROSSCOMPILING_EMULATOR': ';'.join(runner)})]
    create += ['--lockfile', str(input_lock), '--lockfile-out', str(output / 'dependencies.lock'), '--format=json',
               '--out-file', str(output / 'result.json')]
    (output / 'command.json').write_text(json.dumps({
        'command': create, 'sdk': sdk_ref, 'runtime': runner,
        'sdk_host': args.sdk_host, 'sdk_host_execution': args.host_execution,
        'private_ncurses_packaging_exception': args.arm_ncurses_exception}, indent=2) + '\n')
    print(shlex.join(create), flush=True)
    subprocess.run(create, check=True,
                   env=os.environ | {'TEST_BUILD_FOLDER': str(output / 'consumer')})


if __name__ == '__main__':
    main()
