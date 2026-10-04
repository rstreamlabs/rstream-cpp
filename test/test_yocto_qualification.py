"""Guard the scope and execution policy of the unpublished SDK pilot."""
import importlib.util
import contextlib
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'conan'))
spec = importlib.util.spec_from_file_location('qualify_yocto', ROOT / 'conan/qualify_yocto.py')
pilot = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pilot)


class YoctoQualificationTest(unittest.TestCase):
    def test_library_only_plan_keeps_public_dependencies_and_tests_without_cli(self):
        import shlex
        for host in ('x86_64', 'aarch64'):
            for libc in ('musl', 'glibc'):
                result = subprocess.run([
                    sys.executable, str(ROOT / 'conan/qualify_yocto.py'),
                    '--arch', 'arm64', '--libc', libc, '--sdk-host', host,
                    '--library-only', '--plan', '--conan', '/does/not/exist'],
                    capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                args = shlex.split(result.stdout)
                for option in ('build_bins=False', 'with_ncurses=False', 'enable_testing=True',
                               'enable_strict_warnings=True', 'warnings_as_errors=True'):
                    self.assertIn('rstream/*:' + option, args)
                self.assertIn('tools.build:skip_test=False', args)
                for option in ('build_os', 'build_arch', 'build_channel', 'ncurses_ref', 'boost_ref'):
                    self.assertFalse(any(arg.startswith(f'rstream/*:{option}=') for arg in args))

    def test_library_only_cannot_enable_private_packaging_exception(self):
        result = subprocess.run([
            sys.executable, str(ROOT / 'conan/qualify_yocto.py'), '--arch', 'arm64',
            '--libc', 'musl', '--library-only', '--arm-ncurses-exception',
            '--plan', '--conan', '/does/not/exist'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn('not allowed with argument', result.stderr)
        with self.assertRaisesRegex(ValueError, 'private packaging dependencies'):
            pilot.build_arguments('arm64', 'musl', '6.0.3', 2, True, library_only=True)

    def test_extended_targets_use_valid_conan_settings_and_distinct_sdk_abis(self):
        from conan.internal.model.settings import Settings
        identities = set()
        for arch in pilot.CONAN_ARCHES:
            for libc in ('musl', 'glibc'):
                args = pilot.build_arguments(arch, libc, '6.0.3', 2, False)
                values = [args[i + 1].split('=', 1)
                          for i, arg in enumerate(args) if arg == '-s:h']
                settings = Settings.loads((ROOT / 'conan/config/settings.yml').read_text())
                settings.update_values([
                    ('os', 'Linux'), ('compiler', 'gcc'), ('compiler.version', '15'),
                    ('compiler.cppstd', '20'), ('compiler.libcxx', 'libstdc++11'),
                    ('build_type', 'Release'), *values])
                settings.validate()
                identity = (settings.get_safe('arch'), settings.get_safe('os.sdk'))
                self.assertNotIn(identity, identities)
                identities.add(identity)
                self.assertIn(f'yocto-toolchain/*:arch={arch}', args)
                self.assertIn('tools.build:skip_test=False', args)
                self.assertFalse(any('ncurses_ref=' in arg or 'boost_ref=' in arg for arg in args))

    def test_extended_runtime_selects_endianness_cpu_and_target_sysroot(self):
        cases = {
            'x86_i686': ('qemu-i386', '-cpu', 'pentium3'),
            'x86_core2': ('qemu-i386', '-cpu', 'core2duo'),
            'armv6': ('qemu-arm', '-cpu', 'arm1176'),
            'armv6hf': ('qemu-arm', '-cpu', 'arm1176'),
            'armv7': ('qemu-arm', '-cpu', 'cortex-a15'),
            'mips': ('qemu-mips',), 'mipsle': ('qemu-mipsel',),
            'mips64': ('qemu-mips64',), 'mips64le': ('qemu-mips64el',),
            'ppc64': ('qemu-ppc64',), 'ppc64le': ('qemu-ppc64le',),
            'riscv64': ('qemu-riscv64',),
            'loong64': ('qemu-loongarch64',),
        }
        for arch, (emulator, *options) in cases.items():
            for host in ('x86_64', 'aarch64'):
                for libc in ('musl', 'glibc'):
                    with self.subTest(arch=arch, host=host, libc=libc), \
                            patch.object(pilot.shutil, 'which', side_effect=lambda exe: '/runner/' + exe), \
                            patch.object(pilot, 'glibc_loader_arguments', return_value=['/sdk-loader']):
                        self.assertEqual(
                            pilot.runtime_command(arch, libc, Path('/target sdk'), host),
                            ['/runner/' + emulator, *options, '-L', '/target sdk'] + (['/sdk-loader'] if libc == 'glibc' else []))
            with patch.object(pilot.shutil, 'which', return_value=None):
                with self.assertRaisesRegex(RuntimeError, emulator):
                    pilot.runtime_command(arch, 'glibc', Path('/target sdk'))

    def test_extended_cli_plans_preserve_architecture_without_running_conan(self):
        import shlex
        for arch in pilot.CONAN_ARCHES:
            version = '6.0.3' if arch == 'loong64' else '5.0.10'
            command = [sys.executable, str(ROOT / 'conan/qualify_yocto.py'),
                       '--arch', arch, '--libc', 'glibc', '--sdk-host', 'aarch64',
                       '--sdk-version', version, '--plan', '--conan', '/does/not/exist']
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            arguments = shlex.split(result.stdout)
            self.assertIn(f'os.sdk=yocto-toolchain-{version}-{arch}-glibc', arguments)
            build = [arguments[i + 1] for i, arg in enumerate(arguments) if arg == '-s:b']
            self.assertIn('arch=armv8', build)
            self.assertIn('rstream/*:enable_testing=True', arguments)
            self.assertNotIn('upload', arguments)

    def test_instrumented_plan_configures_real_cmake_test_limits_and_runner(self):
        import json
        import shlex
        from conan.internal.model.conf import ConfDefinition
        from conan.tools.cmake.utils import parse_extra_variable
        with tempfile.TemporaryDirectory(prefix='runner with spaces ') as directory:
            source = Path(directory)
            runner = source / 'sde64'
            runner.write_text('#!/bin/sh\nexit 1\n')
            runner.chmod(0o755)
            command = [sys.executable, str(ROOT / 'conan/qualify_yocto.py'),
                       '--arch', 'x86_64_v4', '--libc', 'musl', '--plan',
                       '--runner-command', shlex.join([str(runner), '-skx', '--']),
                       '--test-timeout-scale', '4', '--test-timeout-seconds', '300']
            args = shlex.split(subprocess.check_output(command, text=True))
            configuration = next(arg for arg in args
                                 if arg.startswith('tools.cmake.cmaketoolchain:extra_variables='))
            conf = ConfDefinition()
            conf.loads(configuration)
            variables = conf.get('tools.cmake.cmaketoolchain:extra_variables', check_type=dict)
            self.assertIn('tools.build:skip_test=False', args)
            # Exercise the same toolchain set() statements as Conan, not -D
            # command-line cache entries, which would hide scalar resets.
            toolchain = source / 'conan_toolchain.cmake'
            toolchain.write_text('\n'.join(
                f'set({key} {parse_extra_variable("tools.cmake.cmaketoolchain:extra_variables", key, value)})'
                for key, value in variables.items()) + '\n')
            (source / 'CMakeLists.txt').write_text(
                'cmake_minimum_required(VERSION 3.10)\nproject(timeout_control LANGUAGES NONE)\n'
                f'include("{ROOT / "cmake/tests.cmake"}")\n'
                'if(NOT RSTREAM_TEST_TIMEOUT_SCALE EQUAL 4)\nmessage(FATAL_ERROR "Scale lost")\nendif()\n'
                'add_test(NAME control COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR} /fixture)\n'
                'rstream_configure_test(control)\n')
            build = source / 'build'
            subprocess.run(['cmake', '-S', str(source), '-B', str(build),
                            f'-DCMAKE_TOOLCHAIN_FILE={toolchain}'],
                           check=True, capture_output=True)
            test = json.loads(subprocess.check_output(
                ['ctest', '--test-dir', str(build), '--show-only=json-v1'], text=True))['tests'][0]
            self.assertEqual(test['command'], [str(runner), '-skx', '--', '/fixture'])
            self.assertEqual(next(p['value'] for p in test['properties'] if p['name'] == 'TIMEOUT'), 300)

    def test_nonpositive_instrumented_limits_fail_before_conan(self):
        for flag in ('--test-timeout-scale', '--test-timeout-seconds'):
            for value in ('0', '-1'):
                result = subprocess.run([sys.executable, str(ROOT / 'conan/qualify_yocto.py'),
                                         '--arch', 'x86_64', '--libc', 'musl',
                                         flag, value, '--conan', '/does/not/exist'],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 2)
                self.assertIn('positive integers', result.stderr)

    def test_cmake_startup_runner_is_optional_and_preserves_arguments(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'source'
            source.mkdir()
            (source / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.20)
project(rstream LANGUAGES NONE)
enable_testing()
set(ENABLE_TESTING ON)
set(RSTREAM_TEST_TIMEOUT_SCALE 1)
function(rstream_configure_test)
endfunction()
add_subdirectory(webtty)
''')
            webtty = source / 'webtty'
            webtty.mkdir()
            (webtty / 'CMakeLists.txt').write_text((ROOT / 'bin/webtty/bin/CMakeLists.txt').read_text())
            for name in ('client', 'server'):
                child = webtty / name
                child.mkdir()
                (child / 'CMakeLists.txt').write_text(
                    f'add_executable(rstream-webtty-{name} IMPORTED GLOBAL)\n'
                    f'set_target_properties(rstream-webtty-{name} PROPERTIES IMPORTED_LOCATION /fake/{name}'
                    ' CROSSCOMPILING_EMULATOR "${CMAKE_CROSSCOMPILING_EMULATOR}")\n')
            for mode, args, expected in (
                ('native', [], []),
                ('cross-static', ['-DCMAKE_SYSTEM_NAME=Linux'], []),
                ('cross-runner', ['-DCMAKE_SYSTEM_NAME=Linux',
                                  '-DCMAKE_CROSSCOMPILING_EMULATOR=/runner with spaces;-L;/sdk path'],
                 ['/runner with spaces', '-L', '/sdk path']),
            ):
                build = Path(directory) / mode
                subprocess.run(['cmake', '-S', str(source), '-B', str(build), *args],
                               check=True, capture_output=True)
                import json
                tests = json.loads(subprocess.check_output(
                    ['ctest', '--test-dir', str(build), '--show-only=json-v1'], text=True))['tests']
                self.assertEqual(len(tests), 4)
                for test in tests:
                    self.assertEqual(test['command'][5:], expected)

    def test_cli_startup_accepts_an_explicit_runner(self):
        # A non-executable Python fixture requires the supplied interpreter,
        # just as a cross-built ELF requires QEMU or the SDK dynamic loader.
        with tempfile.TemporaryDirectory(prefix='webtty startup ') as directory:
            binary = Path(directory) / 'rstream-webtty-client'
            binary.write_text('print("rstream-webtty-client Usage: test")\n')
            script = ROOT / 'bin/webtty/bin/test_cli_startup.py'
            for option in ('--help', '--version'):
                subprocess.run([sys.executable, str(script), str(binary), option,
                                '10', sys.executable], check=True, capture_output=True)

    def test_cli_startup_runner_cannot_hide_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'rstream-webtty-client'
            binary.write_text('print("rstream-webtty-client Usage: test")\nraise SystemExit(7)\n')
            result = subprocess.run([
                sys.executable, str(ROOT / 'bin/webtty/bin/test_cli_startup.py'),
                str(binary), '--help', '10', sys.executable], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('failed (0x7)', result.stderr)

    def test_public_x86_pilot_keeps_tests_and_static_musl_consumer(self):
        args = pilot.build_arguments('x86_64', 'musl', '6.0.3', 4, False)
        self.assertIn('tools.build.cross_building:can_run=True', args)
        self.assertIn('tools.build:skip_test=False', args)
        self.assertIn('rstream/*:enable_testing=True', args)
        self.assertIn('rstream/*:warnings_as_errors=True', args)
        self.assertIn('rstream/*:static_libstdcxx=True', args)
        self.assertFalse(any('ncurses_ref=' in arg or 'boost_ref=' in arg for arg in args))

    def test_arm_exception_is_opt_in_and_packaging_only(self):
        args = pilot.build_arguments('arm64', 'glibc', '6.0.3', 4, True)
        self.assertIn('arch=armv8', args)
        self.assertIn('yocto-toolchain/*:arch=arm64', args)
        self.assertIn('rstream/*:static_libstdcxx=False', args)
        self.assertIn('rstream/*:ncurses_ref=ncurses/6.5@rstream/wrynose', args)
        for option in ('build_os=linux', 'build_arch=arm64', 'build_channel=dev'):
            self.assertIn('rstream/*:' + option, args)
        self.assertFalse(any('boost_ref=' in arg for arg in args))

    def test_x86_isa_levels_select_distinct_sdks_with_the_same_conan_arch(self):
        for arch in pilot.X86_TARGETS:
            args = pilot.build_arguments(arch, 'musl', '5.0.10', 2, False)
            host = [args[i + 1] for i, arg in enumerate(args) if arg == '-s:h']
            self.assertIn('arch=x86_64', host)
            self.assertIn(f'os.sdk=yocto-toolchain-5.0.10-{arch}-musl', host)
            self.assertIn(f'yocto-toolchain/*:arch={arch}', args)
            self.assertIn(f'rstream/*:build_arch={arch}', args)
            self.assertFalse(any('ncurses_ref=' in arg for arg in args))

    def test_x86_isa_level_rejects_missing_instructions(self):
        with patch.object(pilot, 'native_x86_features', return_value=set()):
            for arch in pilot.X86_TARGETS[1:]:
                with self.assertRaisesRegex(RuntimeError, 'Host lacks'):
                    pilot.runtime_command(arch, 'musl', Path('/sdk'))
        v2 = {'cx16', 'lahf_lm', 'popcnt', 'sse4_1', 'sse4_2', 'ssse3'}
        with patch.object(pilot, 'native_x86_features', return_value=v2):
            self.assertEqual(pilot.runtime_command('x86_64_v2', 'musl', Path('/sdk')), [])
            with self.assertRaisesRegex(RuntimeError, 'Host lacks'):
                pilot.runtime_command('x86_64_v3', 'musl', Path('/sdk'))
        v3 = v2 | {'avx', 'avx2', 'bmi1', 'bmi2', 'f16c', 'fma', 'movbe', 'xsave', 'abm'}
        with patch.object(pilot, 'native_x86_features', return_value=v3):
            self.assertEqual(pilot.runtime_command('x86_64_v3', 'musl', Path('/sdk')), [])
            with self.assertRaisesRegex(RuntimeError, 'avx512'):
                pilot.runtime_command('x86_64_v4', 'musl', Path('/sdk'))

    def test_x86_features_are_intersected_across_cores(self):
        with patch.object(Path, 'read_text', return_value='flags : sse avx avx2\nflags : sse avx\n'):
            self.assertEqual(pilot.native_x86_features(), {'sse', 'avx'})

    def test_missing_isa_instructions_fail_before_conan(self):
        arguments = ['qualify_yocto.py', '--arch', 'x86_64_v4', '--libc', 'musl']
        error = io.StringIO()
        with patch.object(sys, 'argv', arguments), \
                patch.object(pilot.platform, 'system', return_value='Linux'), \
                patch.object(pilot.platform, 'machine', return_value='x86_64'), \
                patch.object(pilot, 'native_x86_features', return_value=set()), \
                patch.object(pilot.subprocess, 'run') as run, \
                contextlib.redirect_stderr(error):
            with self.assertRaises(SystemExit) as result:
                pilot.main()
            self.assertEqual(result.exception.code, 2)
            run.assert_not_called()
        self.assertIn('Host lacks', error.getvalue())

    def test_explicit_x86_runner_preserves_arguments_and_glibc_loader(self):
        with tempfile.TemporaryDirectory() as directory:
            sysroot = Path(directory)
            (sysroot / 'lib').mkdir()
            loader = sysroot / 'lib/ld-linux-x86-64.so.2'
            loader.touch()
            explicit = '"/runner with spaces/sde64" -skx --'
            with patch.object(pilot.shutil, 'which', return_value='/runner with spaces/sde64'):
                prefix = ['/runner with spaces/sde64', '-skx', '--']
                self.assertEqual(pilot.runtime_command('x86_64_v4', 'musl', sysroot,
                                                       explicit=explicit), prefix)
                dynamic = pilot.runtime_command('x86_64_v4', 'glibc', sysroot,
                                                'aarch64', explicit)
                self.assertEqual(dynamic[:6], [*prefix, str(loader), '--inhibit-cache', '--library-path'])
                self.assertIn(str(sysroot / 'usr/lib'), dynamic[6].split(':'))

    def test_unverified_arm_host_x86_isa_runner_is_rejected(self):
        for arch in pilot.X86_TARGETS[1:]:
            with self.assertRaisesRegex(RuntimeError, 'verified explicit runner'):
                pilot.runtime_command(arch, 'musl', Path('/sdk'), 'aarch64')
        with patch.object(pilot.shutil, 'which', return_value=None):
            with self.assertRaisesRegex(RuntimeError, 'runner executable is missing'):
                pilot.runtime_command('x86_64_v4', 'musl', Path('/sdk'), explicit='/missing -skx --')

    def test_isa_plan_is_portable_and_preserves_target_identity(self):
        for arch in pilot.X86_TARGETS[1:]:
            command = [sys.executable, str(ROOT / 'conan/qualify_yocto.py'),
                       '--arch', arch, '--libc', 'musl', '--sdk-version', '5.0.10',
                       '--runner-command', '/not/installed/sde64 -skx --',
                       '--plan', '--conan', '/does/not/exist']
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('arch=x86_64', result.stdout)
            self.assertIn(f'os.sdk=yocto-toolchain-5.0.10-{arch}-musl', result.stdout)

    def test_arm_execution_requires_qemu(self):
        with patch.object(pilot.shutil, 'which', return_value=None):
            with self.assertRaisesRegex(RuntimeError, 'qemu-aarch64'):
                pilot.runtime_command('arm64', 'musl', Path('/sdk/sysroot'))
        with patch.object(pilot.shutil, 'which', return_value='/usr/bin/qemu-aarch64'):
            self.assertEqual(pilot.runtime_command('arm64', 'musl', Path('/sdk/sysroot')),
                             ['/usr/bin/qemu-aarch64', '-L', '/sdk/sysroot'])

    def test_arm32_runner_uses_target_cpu_and_sdk_runtime_for_both_libcs(self):
        for libc in ('musl', 'glibc'):
            with patch.object(pilot.shutil, 'which', return_value='/usr/bin/qemu-arm'), \
                    patch.object(pilot, 'glibc_loader_arguments', return_value=['/sdk-loader']):
                self.assertEqual(pilot.runtime_command('armv7hf', libc, Path('/sdk target')),
                                 ['/usr/bin/qemu-arm', '-cpu', 'cortex-a15', '-L', '/sdk target'] + (['/sdk-loader'] if libc == 'glibc' else []))
        with patch.object(pilot.shutil, 'which', return_value=None):
            with self.assertRaisesRegex(RuntimeError, 'qemu-arm'):
                pilot.runtime_command('armv7hf', 'musl', Path('/sdk'))

    def test_arm32_settings_keep_hard_float_and_private_recipe_opt_in(self):
        args = pilot.build_arguments('armv7hf', 'musl', '5.0.10', 2, False)
        self.assertIn('arch=armv7hf', args)
        self.assertIn('os.sdk=yocto-toolchain-5.0.10-armv7hf-musl', args)
        self.assertIn('yocto-toolchain/*:arch=armv7hf', args)
        self.assertFalse(any('ncurses_ref=' in arg for arg in args))
        args = pilot.build_arguments('armv7hf', 'musl', '5.0.10', 2, True)
        self.assertIn('rstream/*:ncurses_ref=ncurses/6.5@rstream/scarthgap', args)

    def test_arm32_boost_assembly_tuning_is_scoped_and_uses_public_options(self):
        expected = ('boost/*:extra_b2_flags=asmflags=-march=armv7-a '
                    'asmflags=-mfpu=vfp asmflags=-mfloat-abi=hard')
        for libc in ('musl', 'glibc'):
            args = pilot.build_arguments('armv7hf', libc, '5.0.10', 2, False)
            self.assertIn(expected, args)
            self.assertIn('openssl/*:user.openssl:target=linux-armv4', args)
            self.assertFalse(any('boost_ref=' in arg for arg in args))
        for arch, version in (('arm64', '5.0.10'), ('x86_64_v4', '5.0.10'), ('armv7hf', '6.0.3')):
            args = pilot.build_arguments(arch, 'musl', version, 2, False)
            self.assertFalse(any('extra_b2_flags=' in arg for arg in args))
            targets = [arg for arg in args if 'user.openssl:target=' in arg]
            self.assertEqual(targets, ['openssl/*:user.openssl:target=linux-armv4']
                             if arch == 'armv7hf' else [])

    def test_openssl_abi_targets_are_shared_by_library_and_distribution_plans(self):
        from yocto_openssl_target import openssl_target
        # Endianness and word size must not collapse onto a different ABI.
        cases = {'mips': 'linux-mips32', 'mipsle': 'linux-mips32',
                 'mips64': 'linux64-mips64', 'mips64le': 'linux64-mips64',
                 'ppc64': 'linux-ppc64', 'ppc64le': 'linux-ppc64le',
                 'armv6': 'linux-armv4', 'armv7hf': 'linux-armv4',
                 'x86_core2': 'linux-x86', 'riscv64': 'linux64-riscv64'}
        for version in ('5.0.10', '6.0.3'):
            for host in ('x86_64', 'aarch64'):
                for libc in ('musl', 'glibc'):
                    for arch, target in cases.items():
                        for library_only in (True, False):
                            args = pilot.build_arguments(arch, libc, version, 2, False,
                                                         host, library_only=library_only)
                            self.assertIn('openssl/*:user.openssl:target=' + target, args)
                            self.assertFalse(any('openssl_ref=' in arg for arg in args))
        self.assertIsNone(openssl_target('armv7hf', '5.0.2'))
        self.assertIsNone(openssl_target('x86_64', '6.0.3'))
        self.assertIsNone(openssl_target('arm64', '6.0.3'))

    def test_loongarch_uses_public_recipes_and_stays_out_of_legacy_matrix(self):
        from yocto_openssl_target import openssl_target
        for host in ('x86_64', 'aarch64'):
            for libc in ('musl', 'glibc'):
                args = pilot.build_arguments('loong64', libc, '6.0.3', 2, False,
                                             host, library_only=True)
                self.assertIn('arch=loongarch64', args)
                self.assertIn('yocto-toolchain/*:arch=loong64', args)
                self.assertIn('openssl/*:user.openssl:target=linux64-loongarch64', args)
                self.assertIn('rstream/*:build_bins=False', args)
                self.assertIn('rstream/*:with_ncurses=False', args)
                self.assertFalse(any('_ref=' in value or 'extra_b2_flags=' in value for value in args))
                with self.assertRaisesRegex(ValueError, 'only in the Wrynose'):
                    pilot.build_arguments('loong64', libc, '5.0.10', 2, False, host)
                result = subprocess.run([
                    sys.executable, str(ROOT / 'conan/qualify_yocto.py'),
                    '--arch', 'loong64', '--libc', libc, '--sdk-host', host,
                    '--sdk-version', '5.0.10', '--plan', '--conan', '/does/not/exist'],
                    capture_output=True, text=True)
                self.assertEqual(result.returncode, 2)
                self.assertIn('only in the Wrynose', result.stderr)
        self.assertIsNone(openssl_target('loong64', '5.0.10'))

    def test_arm32_private_recipe_is_rejected_outside_its_sdk_scope(self):
        for version, host in (('6.0.3', 'x86_64'), ('5.0.10', 'aarch64')):
            result = subprocess.run([
                sys.executable, str(ROOT / 'conan/qualify_yocto.py'), '--arch', 'armv7hf',
                '--libc', 'musl', '--sdk-version', version, '--sdk-host', host,
                '--arm-ncurses-exception', '--plan', '--conan', '/does/not/exist'],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn('x86_64-host Scarthgap 5.0.10 ARMv7hf', result.stderr)

    def test_sdk_host_and_target_have_distinct_conan_settings(self):
        args = pilot.build_arguments('x86_64', 'glibc', '5.0.10', 2, False,
                                     sdk_host='aarch64')
        build = [args[i + 1] for i, arg in enumerate(args) if arg == '-s:b']
        host = [args[i + 1] for i, arg in enumerate(args) if arg == '-s:h']
        self.assertIn('arch=armv8', build)
        self.assertIn('arch=x86_64', host)
        self.assertIn('yocto-toolchain/*:arch=x86_64', args)
        self.assertFalse(any('ncurses_ref=' in arg for arg in args))

    def test_arm_sdk_host_needs_an_x86_target_runner(self):
        with patch.object(pilot.shutil, 'which', return_value=None):
            with self.assertRaisesRegex(RuntimeError, 'qemu-x86_64'):
                pilot.runtime_command('x86_64', 'musl', Path('/sdk'), 'aarch64')
        with patch.object(pilot.shutil, 'which', return_value='/runner/qemu-x86_64'), \
                patch.object(pilot, 'glibc_loader_arguments', return_value=['/sdk-loader']):
            for libc in ('musl', 'glibc'):
                self.assertEqual(pilot.runtime_command('x86_64', libc, Path('/sdk'), 'aarch64'),
                                 ['/runner/qemu-x86_64', '-L', '/sdk'] + (['/sdk-loader'] if libc == 'glibc' else []))

    def test_sysroot_selection_excludes_the_actual_sdk_host(self):
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory)
            native = prefix / 'sysroots/aarch64-pokysdk-linux'
            target = prefix / 'sysroots/armv8a-poky-linux-musl'
            native.mkdir(parents=True)
            target.mkdir()
            self.assertEqual(pilot.target_sysroot(prefix, 'aarch64'), target)
            with self.assertRaisesRegex(RuntimeError, 'host sysroot is missing'):
                pilot.target_sysroot(prefix, 'x86_64')
            (prefix / 'sysroots/unexpected-target').mkdir()
            with self.assertRaisesRegex(RuntimeError, 'exactly one target'):
                pilot.target_sysroot(prefix, 'aarch64')

    def test_wrong_sdk_host_userspace_fails_before_conan(self):
        sdk_host = 'x86_64' if pilot.platform.machine() == 'aarch64' else 'aarch64'
        command = [sys.executable, str(ROOT / 'conan/qualify_yocto.py'),
                   '--arch', 'arm64', '--libc', 'musl', '--sdk-host', sdk_host,
                   '--conan', '/does/not/exist']
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn(f'Linux {sdk_host} userspace', result.stderr)
        plan = subprocess.run([*command, '--plan'], capture_output=True, text=True)
        self.assertEqual(plan.returncode, 0, plan.stderr)

    def test_emulated_glibc_uses_explicit_sdk_loader_and_ignores_host_cache(self):
        with tempfile.TemporaryDirectory(prefix='SDK with spaces ') as directory:
            root = Path(directory)
            (root / 'lib').mkdir()
            loader = root / 'lib/ld-linux-aarch64.so.1'
            loader.touch()
            for host in ('x86_64', 'aarch64'):
                with self.subTest(host=host), patch.object(pilot.shutil, 'which', return_value='/runner/qemu-aarch64'):
                    command = pilot.runtime_command('arm64', 'glibc', root, host)
                    self.assertEqual(command[:6], ['/runner/qemu-aarch64', '-L', str(root), str(loader),
                                                  '--inhibit-cache', '--library-path'])
                    self.assertEqual(command[6].split(':'), [str(root / d) for d in ('lib', 'usr/lib', 'lib64', 'usr/lib64')])

    def test_glibc_loader_rejects_ambiguous_or_host_link_and_accepts_sdk_alias(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'sdk'
            (root / 'lib').mkdir(parents=True)
            with self.assertRaisesRegex(RuntimeError, 'dynamic loader'):
                pilot.glibc_loader_arguments(root)
            loader = root / 'lib/ld-linux-aarch64.so.1'
            loader.touch()
            alias = root / 'lib/ld-alias.so.1'
            alias.symlink_to(loader.name)
            self.assertEqual(pilot.glibc_loader_arguments(root)[0], str(loader))
            alias.unlink()
            alias.touch()
            with self.assertRaisesRegex(RuntimeError, 'dynamic loader'):
                pilot.glibc_loader_arguments(root)
            alias.unlink()
            loader.unlink()
            host_loader = Path(directory) / 'host-loader'
            host_loader.touch()
            loader.symlink_to(host_loader)
            with self.assertRaisesRegex(RuntimeError, 'dynamic loader'):
                pilot.glibc_loader_arguments(root)

    def test_glibc_execution_uses_sdk_loader(self):
        with tempfile.TemporaryDirectory() as directory:
            sysroot = Path(directory)
            with self.assertRaisesRegex(RuntimeError, 'dynamic loader'):
                pilot.runtime_command('x86_64', 'glibc', sysroot)
            (sysroot / 'lib').mkdir()
            loader = sysroot / 'lib/ld-linux-x86-64.so.2'
            loader.touch()
            command = pilot.runtime_command('x86_64', 'glibc', sysroot)
            self.assertEqual(command[:3], [str(loader), '--inhibit-cache', '--library-path'])
            self.assertIn(str(sysroot / 'usr/lib'), command[3].split(':'))
        self.assertEqual(pilot.runtime_command('x86_64', 'musl', Path('/sdk')), [])

    def test_plan_cannot_execute_conan(self):
        result = subprocess.run([sys.executable, str(ROOT / 'conan/qualify_yocto.py'),
                                 '--arch', 'x86_64', '--libc', 'musl', '--plan',
                                 '--conan', '/does/not/exist'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('create', result.stdout)
        self.assertNotIn('upload', result.stdout)

    def test_scarthgap_maintenance_plan_preserves_public_dependencies_and_tests(self):
        result = subprocess.run([sys.executable, str(ROOT / 'conan/qualify_yocto.py'),
                                 '--arch', 'arm64', '--libc', 'musl', '--sdk-version', '5.0.10',
                                 '--plan', '--conan', '/does/not/exist'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('os.sdk=yocto-toolchain-5.0.10-arm64-musl', result.stdout)
        self.assertIn('tools.build:skip_test=False', result.stdout)
        self.assertIn('rstream/*:enable_testing=True', result.stdout)
        self.assertNotIn('ncurses_ref=', result.stdout)
        self.assertNotIn('boost_ref=', result.stdout)

    def test_scarthgap_exception_uses_its_separate_packaging_recipe(self):
        result = subprocess.run([sys.executable, str(ROOT / 'conan/qualify_yocto.py'),
                                 '--arch', 'arm64', '--libc', 'musl', '--sdk-version', '5.0.10',
                                 '--arm-ncurses-exception', '--plan', '--conan', '/does/not/exist'],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('ncurses/6.5@rstream/scarthgap', result.stdout)
        self.assertNotIn('@rstream/wrynose', result.stdout)
        self.assertNotIn('boost_ref=', result.stdout)
        for option in ('build_os=linux', 'build_arch=arm64', 'build_channel=dev'):
            self.assertIn('rstream/*:' + option, result.stdout)

    def test_arm_host_intel_exception_is_exact_and_never_a_library_default(self):
        for libc in ('musl', 'glibc'):
            for version, host, target, allowed in (
                    ('6.0.3', 'aarch64', 'x86_64', True),
                    ('6.0.2', 'aarch64', 'x86_64', False),
                    ('5.0.10', 'aarch64', 'x86_64', False),
                    ('6.0.3', 'x86_64', 'x86_64', False),
                    ('6.0.3', 'aarch64', 'x86_64_v2', True),
                    ('6.0.3', 'x86_64', 'x86_64_v2', False),
                    ('6.0.2', 'aarch64', 'x86_64_v2', False),
                    ('5.0.10', 'aarch64', 'x86_64_v2', False),
                    ('6.0.3', 'aarch64', 'x86_64_v3', False)):
                with self.subTest(libc=libc, version=version, host=host, target=target):
                    base = [sys.executable, str(ROOT / 'conan/qualify_yocto.py'),
                            '--arch', target, '--libc', libc, '--sdk-host', host,
                            '--sdk-version', version, '--plan', '--conan', '/does/not/exist']
                    result = subprocess.run([*base, '--arm-ncurses-exception'], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0 if allowed else 2, result.stderr)
                    if allowed:
                        self.assertIn('ncurses/6.5@rstream/wrynose', result.stdout)
                        self.assertIn('arch=armv8', result.stdout)
                    public = subprocess.run([*base, '--library-only'], capture_output=True, text=True)
                    self.assertEqual(public.returncode, 0, public.stderr)
                    self.assertNotIn('ncurses_ref=', public.stdout)
                    self.assertNotIn('boost_ref=', public.stdout)

    def test_arm_host_v2_requires_explicit_runner_and_preserves_sdk_loader(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'lib').mkdir()
            loader = root / 'lib/ld-linux-x86-64.so.2'
            loader.touch()
            with self.assertRaisesRegex(RuntimeError, 'verified explicit runner'):
                pilot.runtime_command('x86_64_v2', 'musl', root, 'aarch64')
            with patch.object(pilot.shutil, 'which', return_value='/usr/local/bin/qemu-x86_64'):
                runner = 'qemu-x86_64 -cpu Nehalem'
                musl = pilot.runtime_command('x86_64_v2', 'musl', root, 'aarch64', runner)
                self.assertEqual(musl, ['qemu-x86_64', '-cpu', 'Nehalem'])
                glibc = pilot.runtime_command('x86_64_v2', 'glibc', root, 'aarch64', runner)
                self.assertEqual(glibc[:6], musl + [str(loader), '--inhibit-cache', '--library-path'])
                self.assertEqual(glibc[6].split(':'), [str(root / d) for d in ('lib', 'usr/lib', 'lib64', 'usr/lib64')])

    def test_private_ncurses_consumer_uses_v2_cpu_and_isolated_sdk_loader(self):
        from types import SimpleNamespace
        path = ROOT / 'conan/recipes/ncurses-wrynose/all/test_package/conanfile.py'
        spec = importlib.util.spec_from_file_location('ncurses_consumer', path)
        consumer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(consumer)
        for target in ('x86_64', 'x86_64_v2'):
            for libc in ('musl', 'glibc'):
                with self.subTest(target=target, libc=libc), tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    (root / 'lib').mkdir()
                    loader = root / 'lib/ld-linux-x86-64.so.2'
                    loader.touch()
                    settings = SimpleNamespace(arch='x86_64', get_safe=lambda *args:
                                               f'yocto-toolchain-6.0.3-{target}-{libc}')
                    recipe = SimpleNamespace(settings=settings,
                                             conf=SimpleNamespace(get=lambda *args: str(root)))
                    toolchain = SimpleNamespace(variables={}, generate=lambda: None)
                    with patch.object(consumer, 'can_run', return_value=True), \
                            patch.object(consumer, 'cross_building', return_value=True), \
                            patch.object(consumer.shutil, 'which', return_value='/runner/qemu-x86_64'), \
                            patch.object(consumer, 'CMakeToolchain', return_value=toolchain), \
                            patch.object(consumer, 'CMakeDeps'):
                        consumer.TestPackage.generate(recipe)
                    runtime = toolchain.variables['CMAKE_CROSSCOMPILING_EMULATOR'].split(';')
                    expected = ['/runner/qemu-x86_64']
                    if target == 'x86_64_v2':
                        expected += ['-cpu', 'Nehalem']
                    expected += ['-L', str(root)]
                    if libc == 'glibc':
                        expected += [str(loader), '--inhibit-cache', '--library-path',
                                     ':'.join(str(root / d) for d in ('lib', 'usr/lib', 'lib64', 'usr/lib64'))]
                    self.assertEqual(runtime, expected)
                    self.assertEqual(toolchain.variables['TEST_FULLY_STATIC'], libc == 'musl')

    def test_invalid_scope_fails_before_conan(self):
        for arguments in (['--arch', 'x86_64', '--arm-ncurses-exception'],
                          ['--arch', 'arm64', '--sdk-version', '5.0.2'],
                          ['--arch', 'arm64', '--sdk-version', '5.0.20'],
                          ['--arch', 'arm64', '--sdk-version', '5.0.20', '--arm-ncurses-exception'],
                          ['--arch', 'x86_64', '--jobs', '0']):
            result = subprocess.run([sys.executable, str(ROOT / 'conan/qualify_yocto.py'),
                                     '--libc', 'musl', '--plan', '--conan', '/does/not/exist',
                                     *arguments], capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)


if __name__ == '__main__':
    unittest.main()
