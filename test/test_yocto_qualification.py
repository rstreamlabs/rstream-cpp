"""Guard the scope and execution policy of the unpublished SDK pilot."""
import importlib.util
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

    def test_arm_execution_requires_qemu(self):
        with patch.object(pilot.shutil, 'which', return_value=None):
            with self.assertRaisesRegex(RuntimeError, 'qemu-aarch64'):
                pilot.runtime_command('arm64', 'musl', Path('/sdk/sysroot'))
        with patch.object(pilot.shutil, 'which', return_value='/usr/bin/qemu-aarch64'):
            self.assertEqual(pilot.runtime_command('arm64', 'glibc', Path('/sdk/sysroot')),
                             ['/usr/bin/qemu-aarch64', '-L', '/sdk/sysroot'])

    def test_glibc_execution_uses_sdk_loader(self):
        with tempfile.TemporaryDirectory() as directory:
            sysroot = Path(directory)
            with self.assertRaisesRegex(RuntimeError, 'dynamic loader'):
                pilot.runtime_command('x86_64', 'glibc', sysroot)
            (sysroot / 'lib').mkdir()
            loader = sysroot / 'lib/ld-linux-x86-64.so.2'
            loader.touch()
            command = pilot.runtime_command('x86_64', 'glibc', sysroot)
            self.assertEqual(command[:2], [str(loader), '--library-path'])
            self.assertIn(str(sysroot / 'usr/lib'), command[2].split(':'))
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

    def test_invalid_scope_fails_before_conan(self):
        for arguments in (['--arch', 'x86_64', '--arm-ncurses-exception'],
                          ['--arch', 'arm64', '--sdk-version', '5.0.2'],
                          ['--arch', 'arm64', '--sdk-version', '5.0.20'],
                          ['--arch', 'arm64', '--sdk-version', '5.0.10', '--arm-ncurses-exception'],
                          ['--arch', 'x86_64', '--jobs', '0']):
            result = subprocess.run([sys.executable, str(ROOT / 'conan/qualify_yocto.py'),
                                     '--libc', 'musl', '--plan', '--conan', '/does/not/exist',
                                     *arguments], capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)


if __name__ == '__main__':
    unittest.main()
