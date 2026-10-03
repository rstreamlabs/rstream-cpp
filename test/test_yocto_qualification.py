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

    def test_invalid_scope_fails_before_conan(self):
        for arguments in (['--arch', 'x86_64', '--arm-ncurses-exception'],
                          ['--arch', 'arm64', '--sdk-version', '5.0.2'],
                          ['--arch', 'x86_64', '--jobs', '0']):
            result = subprocess.run([sys.executable, str(ROOT / 'conan/qualify_yocto.py'),
                                     '--libc', 'musl', '--plan', '--conan', '/does/not/exist',
                                     *arguments], capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)


if __name__ == '__main__':
    unittest.main()
