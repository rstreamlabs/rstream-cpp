#!/usr/bin/env python3

import importlib.util
import os
import tempfile
from types import SimpleNamespace
import unittest


REPOSITORY_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPEC = importlib.util.spec_from_file_location("rstream_deploy", os.path.join(REPOSITORY_ROOT, "deploy.py"))
DEPLOY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DEPLOY)


class WindowsRuntimeDependenciesTest(unittest.TestCase):
    def test_parse_windows_imports(self):
        output = """
          DLL Name: KERNEL32.dll
          DLL Name: libprotobuf.dll
        """
        self.assertEqual(DEPLOY.parse_windows_imports(output), ["KERNEL32.dll", "libprotobuf.dll"])

    def test_copy_transitive_runtime_dependencies(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            deploy_dir = os.path.join(temp_dir, "deploy")
            candidate_dir = os.path.join(temp_dir, "candidates")
            os.makedirs(os.path.join(deploy_dir, "bin"))
            os.makedirs(candidate_dir)
            application = os.path.join(deploy_dir, "bin", "rstream.exe")
            protobuf = os.path.join(candidate_dir, "libprotobuf.dll")
            abseil = os.path.join(candidate_dir, "libabsl_status.dll")
            for file_path in (application, protobuf, abseil):
                with open(file_path, "wb") as fp:
                    fp.write(os.path.basename(file_path).encode("ascii"))
            imports = {
                "rstream.exe": ["KERNEL32.dll", "libprotobuf.dll"],
                "libprotobuf.dll": ["libabsl_status.dll"],
                "libabsl_status.dll": ["api-ms-win-crt-runtime-l1-1-0.dll"],
            }
            DEPLOY.copy_windows_runtime_dependencies(
                deploy_dir,
                {
                    "libprotobuf.dll": protobuf,
                    "libabsl_status.dll": abseil,
                },
                lambda file_path: imports[os.path.basename(file_path)],
            )
            self.assertTrue(os.path.isfile(os.path.join(deploy_dir, "bin", "libprotobuf.dll")))
            self.assertTrue(os.path.isfile(os.path.join(deploy_dir, "bin", "libabsl_status.dll")))

    def test_missing_runtime_dependency_fails(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            deploy_dir = os.path.join(temp_dir, "deploy")
            os.makedirs(os.path.join(deploy_dir, "bin"))
            application = os.path.join(deploy_dir, "bin", "rstream.exe")
            with open(application, "wb") as fp:
                fp.write(b"application")
            with self.assertRaisesRegex(Exception, "missing Windows runtime libraries: missing.dll"):
                DEPLOY.copy_windows_runtime_dependencies(
                    deploy_dir,
                    { },
                    lambda _: ["missing.dll"],
                )


class MacosRuntimeDependenciesTest(unittest.TestCase):
    def test_parse_macos_imports(self):
        output = """
/tmp/rstream-webtty-client:
    @rpath/libyaml-cpp.0.9.dylib (compatibility version 0.9.0, current version 0.9.0)
    /usr/lib/libc++.1.dylib (compatibility version 1.0.0, current version 1.0.0)
        """
        self.assertEqual(
            DEPLOY.parse_macos_imports(output),
            ["@rpath/libyaml-cpp.0.9.dylib", "/usr/lib/libc++.1.dylib"],
        )

    def test_copy_transitive_runtime_dependencies_and_modules(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            deploy_dir = os.path.join(temp_dir, "deploy")
            candidate_dir = os.path.join(temp_dir, "candidates")
            os.makedirs(os.path.join(deploy_dir, "bin"))
            os.makedirs(candidate_dir)
            application = os.path.join(deploy_dir, "bin", "rstream-webtty-client")
            yaml = os.path.join(candidate_dir, "libyaml-cpp.0.9.dylib")
            abseil = os.path.join(candidate_dir, "libabsl_status.dylib")
            module = os.path.join(candidate_dir, "legacy.dylib")
            crypto = os.path.join(candidate_dir, "libcrypto.3.dylib")
            for file_path in (application, yaml, abseil, module, crypto):
                with open(file_path, "wb") as fp:
                    fp.write(os.path.basename(file_path).encode("ascii"))
            imports = {
                "rstream-webtty-client": [
                    "/usr/lib/libc++.1.dylib",
                    "@rpath/libyaml-cpp.0.9.dylib",
                ],
                "libyaml-cpp.0.9.dylib": ["@rpath/libabsl_status.dylib"],
                "libabsl_status.dylib": [
                    "/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation"
                ],
                "legacy.dylib": ["@rpath/libcrypto.3.dylib"],
                "libcrypto.3.dylib": ["/usr/lib/libSystem.B.dylib"],
            }
            DEPLOY.copy_macos_runtime_dependencies(
                deploy_dir,
                {
                    "libyaml-cpp.0.9.dylib": yaml,
                    "libabsl_status.dylib": abseil,
                    "libcrypto.3.dylib": crypto,
                },
                {"legacy.dylib": module},
                lambda file_path: imports[os.path.basename(file_path)],
            )
            self.assertTrue(
                os.path.isfile(os.path.join(deploy_dir, "lib", "libyaml-cpp.0.9.dylib"))
            )
            self.assertTrue(
                os.path.isfile(os.path.join(deploy_dir, "lib", "libabsl_status.dylib"))
            )
            self.assertTrue(
                os.path.isfile(os.path.join(deploy_dir, "lib", "ossl-modules", "legacy.dylib"))
            )
            self.assertTrue(
                os.path.isfile(os.path.join(deploy_dir, "lib", "libcrypto.3.dylib"))
            )

    def test_missing_runtime_dependency_fails(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            deploy_dir = os.path.join(temp_dir, "deploy")
            os.makedirs(os.path.join(deploy_dir, "bin"))
            application = os.path.join(deploy_dir, "bin", "rstream-webtty-client")
            with open(application, "wb") as fp:
                fp.write(b"application")
            with self.assertRaisesRegex(
                Exception, "missing macOS runtime libraries: @rpath/missing.dylib"
            ):
                DEPLOY.copy_macos_runtime_dependencies(
                    deploy_dir,
                    { },
                    { },
                    lambda _: ["@rpath/missing.dylib"],
                )

    def test_missing_openssl_modules_fail_packaging(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            os.makedirs(os.path.join(temp_dir, "lib"))
            dependency = SimpleNamespace(
                package_folder=temp_dir,
                ref=SimpleNamespace(name="openssl"),
            )
            with self.assertRaisesRegex(
                Exception, "missing macOS OpenSSL runtime modules"
            ):
                DEPLOY.get_macos_runtime_candidates([dependency])


if __name__ == "__main__":
    unittest.main()
