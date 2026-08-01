#!/usr/bin/env python3

import importlib.util
import os
import tempfile
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


if __name__ == "__main__":
    unittest.main()
