"""Exercise packaging commands without invoking Conan builds or publication."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import shutil

ROOT = Path(__file__).resolve().parents[1]


class CrossPackagingDependenciesTest(unittest.TestCase):
    def test_cache_migration_preserves_project_sdk_settings(self):
        from conan.api.conan_api import ConanAPI
        with tempfile.TemporaryDirectory() as directory:
            cache = Path(directory)
            settings = cache / 'settings.yml'
            expected = (ROOT / 'conan/config/settings.yml').read_bytes()
            settings.write_bytes(expected)
            ConanAPI(cache_folder=str(cache)).profiles.list()
            self.assertEqual(settings.read_bytes(), expected)
            self.assertIn(b'sdk: [null, ANY]', expected)

    def test_ncurses_export_ignores_generated_consumer_files(self):
        from conan.api.conan_api import ConanAPI
        for generation in ('wrynose', 'scarthgap'):
            with tempfile.TemporaryDirectory() as directory:
                work = Path(directory)
                recipe = work / 'recipe'
                shutil.copytree(ROOT / f'conan/recipes/ncurses-{generation}/all', recipe,
                                ignore=shutil.ignore_patterns('test_package', '__pycache__'))
                api = ConanAPI(cache_folder=str(work / 'cache'))
                before, _ = api.export.export(str(recipe / 'conanfile.py'), version='6.5',
                                              user='rstream', channel=generation)
                generated = recipe / 'test_package/build/generators'
                generated.mkdir(parents=True)
                (generated / 'conan_toolchain.cmake').write_text('set(FOREIGN_BUILD_PATH /another/sdk)\n')
                after, _ = api.export.export(str(recipe / 'conanfile.py'), version='6.5',
                                             user='rstream', channel=generation)
                self.assertEqual(before.revision, after.revision)

    def commands(self, **overrides):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            conan = work / 'conan'
            conan.write_text(f'#!{sys.executable}\n' + '''import json, os, sys
from pathlib import Path
with open(os.environ['COMMAND_LOG'], 'a') as log:
    log.write(json.dumps({'args': sys.argv[1:], 'cwd': str(Path.cwd())}) + '\\n')
if sys.argv[1] == '--version': print('Conan version 2.31.2')
elif sys.argv[1] == 'inspect': print(json.dumps({'name': 'rstream', 'version': '1.14.7'}))
''')
            conan.chmod(0o755)
            uname = work / 'uname'
            uname.write_text('#!/bin/sh\nif [ "$1" = "-m" ]; then echo "${TEST_HOST_ARCH:-x86_64}"; else echo "${TEST_OS_NAME:-Linux}"; fi\n')
            uname.chmod(0o755)
            log = work / 'commands.jsonl'
            env = dict(os.environ, PATH=str(work) + os.pathsep + os.environ['PATH'],
                       COMMAND_LOG=str(log), OSS='linux', USE_DOCKER='off',
                       LINUX_TOOLCHAIN_VERSION='6.0.3', LINUX_ARCHS='x86_64 arm64',
                       LINUX_TCLIBCS='musl', LINUX_BUILD_SHARED='off',
                       LINUX_PLUGIN_MODES='static', USE_PATCHED_CONAN_DEPS='on',
                       MACOS_ARCHS='arm64', MACOS_BUILD_SHARED='off',
                       WINDOWS_ARCHS='x86_64', WINDOWS_BUILD_SHARED='off')
            env.update(overrides)
            subprocess.run(['bash', str(ROOT / 'build-conan-cross.sh'), 'build'],
                           cwd=ROOT, env=env, check=True, capture_output=True, text=True)
            return [json.loads(line) for line in log.read_text().splitlines()]

    def test_wrynose_uses_public_boost_and_only_arm_ncurses_exception(self):
        calls = self.commands()
        exports = [c for c in calls if c['args'][0] == 'export']
        self.assertEqual(len(exports), 1)
        self.assertIn(str(ROOT / 'conan/recipes/ncurses-wrynose/all'), exports[0]['args'])
        builds = [c['args'] for c in calls if c['args'][0] == 'create']
        self.assertEqual(len(builds), 2)
        for command in builds:
            self.assertFalse(any('boost_ref=' in arg for arg in command))
            self.assertIn('rstream/*:build_os=linux', command)
        self.assertFalse(any('ncurses_ref=' in arg for arg in builds[0]))
        self.assertIn('rstream/*:ncurses_ref=ncurses/6.5@rstream/wrynose', builds[1])

    def test_loongarch_packaging_selects_public_openssl_target(self):
        calls = self.commands(LINUX_ARCHS='loong64', LINUX_TCLIBCS='musl glibc')
        builds = [c['args'] for c in calls if c['args'][0] == 'create']
        self.assertEqual(len(builds), 2)
        self.assertFalse(any(c['args'][0] == 'export' for c in calls))
        for command in builds:
            self.assertIn('arch=loongarch64', command)
            self.assertIn('yocto-toolchain/6.0.3:arch=loong64', command)
            self.assertIn('openssl/*:user.openssl:target=linux64-loongarch64', command)
            self.assertFalse(any('ncurses_ref=' in arg or 'boost_ref=' in arg for arg in command))

    def test_arm_host_intel_packaging_exception_is_scoped(self):
        for version, target, host, patched, expected in (
                ('6.0.3', 'x86_64', 'aarch64', 'on', True),
                ('6.0.3', 'x86_64', 'aarch64', 'off', False),
                ('6.0.3', 'x86_64', 'x86_64', 'on', False),
                ('6.0.3', 'x86_64_v2', 'aarch64', 'on', True),
                ('6.0.3', 'x86_64_v2', 'aarch64', 'off', False),
                ('6.0.3', 'x86_64_v2', 'x86_64', 'on', False),
                ('5.0.10', 'x86_64_v2', 'aarch64', 'on', False),
                ('6.0.3', 'x86_64_v3', 'aarch64', 'on', False),
                ('6.0.2', 'x86_64', 'aarch64', 'on', False),
                ('5.0.10', 'x86_64', 'aarch64', 'on', False)):
            with self.subTest(version=version, target=target, host=host, patched=patched):
                calls = self.commands(LINUX_TOOLCHAIN_VERSION=version, LINUX_ARCHS=target,
                                      TEST_HOST_ARCH=host, USE_PATCHED_CONAN_DEPS=patched)
                exports = [c for c in calls if c['args'][0] == 'export']
                self.assertEqual(len(exports), int(expected))
                builds = [c['args'] for c in calls if c['args'][0] == 'create']
                self.assertEqual(len(builds), 1)
                self.assertEqual('rstream/*:ncurses_ref=ncurses/6.5@rstream/wrynose' in builds[0], expected)
                self.assertFalse(any('boost_ref=' in arg for arg in builds[0]))

    def test_docker_sdk_host_follows_builder_platform(self):
        for platform, expected in (('linux/amd64', 'x86_64'), ('linux/arm64', 'aarch64'),
                                   ('linux/arm64/v8', 'aarch64'), ('linux/ppc64le', 'unknown')):
            result = subprocess.run(['bash', '-c', 'source "$1" help >/dev/null; linux_sdk_host',
                                     'bash', str(ROOT / 'build-conan-cross.sh')],
                                    env=dict(os.environ, USE_DOCKER='on', CONAN_DOCKER_PLATFORM=platform),
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), expected)

    def test_scarthgap_maintenance_uses_public_boost_and_separate_arm_ncurses(self):
        calls = self.commands(LINUX_TOOLCHAIN_VERSION='5.0.10')
        exports = [c for c in calls if c['args'][0] == 'export']
        self.assertEqual(len(exports), 1)
        self.assertIn(str(ROOT / 'conan/recipes/ncurses-scarthgap/all'), exports[0]['args'])
        self.assertEqual(exports[0]['args'][-1], 'scarthgap')
        builds = [c['args'] for c in calls if c['args'][0] == 'create']
        self.assertEqual(len(builds), 2)
        self.assertFalse(any('boost_ref=' in arg for command in builds for arg in command))
        self.assertFalse(any('ncurses_ref=' in arg for arg in builds[0]))
        self.assertIn('rstream/*:ncurses_ref=ncurses/6.5@rstream/scarthgap', builds[1])
        public = self.commands(LINUX_TOOLCHAIN_VERSION='5.0.10', USE_PATCHED_CONAN_DEPS='off')
        self.assertFalse(any(c['args'][0] == 'export' for c in public))
        self.assertFalse(any('_ref=' in arg for c in public for arg in c['args']))

    def test_public_only_mode_does_not_export_or_override(self):
        calls = self.commands(USE_PATCHED_CONAN_DEPS='off')
        self.assertFalse(any(c['args'][0] == 'export' for c in calls))
        self.assertFalse(any('_ref=' in arg for c in calls for arg in c['args']))

    def test_arm32_exception_is_selected_only_for_scarthgap_packaging(self):
        for version, patched, expected in (('5.0.10', 'on', True),
                                           ('5.0.10', 'off', False),
                                           ('6.0.3', 'on', False)):
            calls = self.commands(LINUX_TOOLCHAIN_VERSION=version, LINUX_ARCHS='armv7hf',
                                  USE_PATCHED_CONAN_DEPS=patched)
            exports = [c for c in calls if c['args'][0] == 'export']
            self.assertEqual(len(exports), int(expected))
            builds = [c['args'] for c in calls if c['args'][0] == 'create']
            self.assertEqual(len(builds), 1)
            self.assertIn('arch=armv7hf', builds[0])
            self.assertEqual(any('ncurses_ref=' in arg for arg in builds[0]), expected)
            self.assertFalse(any('boost_ref=' in arg for arg in builds[0]))
            flags = [arg for arg in builds[0] if 'extra_b2_flags=' in arg]
            self.assertEqual(flags, [
                'boost/*:extra_b2_flags=asmflags=-march=armv7-a '
                'asmflags=-mfpu=vfp asmflags=-mfloat-abi=hard'
            ] if version == '5.0.10' else [])
            self.assertIn('openssl/*:user.openssl:target=linux-armv4', builds[0])

    def test_public_openssl_targets_reach_cross_packaging_without_recipe_overrides(self):
        targets = {'x86_i686': 'linux-x86', 'armv6hf': 'linux-armv4',
                   'mipsle': 'linux-mips32', 'mips64le': 'linux64-mips64',
                   'ppc64': 'linux-ppc64', 'ppc64le': 'linux-ppc64le',
                   'riscv64': 'linux64-riscv64'}
        for version in ('5.0.10', '6.0.3'):
            calls = self.commands(LINUX_TOOLCHAIN_VERSION=version,
                                  LINUX_ARCHS=' '.join(targets), USE_PATCHED_CONAN_DEPS='off')
            self.assertFalse(any(c['args'][0] == 'export' for c in calls))
            builds = [c['args'] for c in calls if c['args'][0] == 'create']
            self.assertEqual(len(builds), len(targets))
            for command, target in zip(builds, targets.values()):
                self.assertIn('openssl/*:user.openssl:target=' + target, command)
                self.assertFalse(any('_ref=' in arg for arg in command))

    def test_wrynose_x86_does_not_export_unused_recipes(self):
        self.assertFalse(any(c['args'][0] == 'export' for c in self.commands(LINUX_ARCHS='x86_64')))

    def test_macos_uses_public_dependencies(self):
        calls = self.commands(OSS='macos', TEST_OS_NAME='Darwin')
        self.assertFalse(any(c['args'][0] == 'export' for c in calls))
        self.assertFalse(any('_ref=' in arg for c in calls for arg in c['args']))

    def test_mixed_wrynose_and_legacy_targets_export_both_recipe_sets(self):
        calls = self.commands(OSS='linux windows', LINUX_ARCHS='arm64')
        exports = [c for c in calls if c['args'][0] == 'export']
        self.assertTrue(any('ncurses-wrynose/all' in ' '.join(c['args']) for c in exports))
        self.assertTrue(any(c['cwd'].endswith('/recipes/boost') for c in exports))
        builds = [c['args'] for c in calls if c['args'][0] == 'create']
        self.assertNotIn('rstream/*:boost_ref=boost/1.85.0@conan/stable', builds[0])
        self.assertIn('rstream/*:boost_ref=boost/1.85.0@conan/stable', builds[1])


if __name__ == '__main__':
    unittest.main()
