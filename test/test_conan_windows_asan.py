import ast
import importlib.util
import os
import re
import shlex
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from conan.errors import ConanInvalidConfiguration
from conan.tools.cmake.utils import parse_extra_variable

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("rstream_recipe", ROOT / "conanfile.py")
RECIPE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RECIPE)
TEST_NAME = "rstream-test-core-windows-blocking-handle-asan"


class WindowsAsanRequirementTest(unittest.TestCase):
    def test_cached_sdk_cannot_skip_qualification(self):
        workflow = (ROOT / ".github/workflows/conan.yml").read_text(encoding="utf-8")
        commands = re.findall(r"conan create[^\n]+", workflow)
        self.assertEqual(len(commands), 3)
        for command in commands:
            self.assertIn('--build="rstream/*"', command)
        build_flags = [
            shlex.split(flag)[0] for flag in re.findall(r"--build=\S+", commands[0])
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            cache = path / "cache"
            (cache / "profiles").mkdir(parents=True)
            os_name = {"win32": "Windows", "darwin": "Macos"}.get(sys.platform, "Linux")
            (cache / "profiles/default").write_text(
                "[settings]\nos=" + os_name + "\n", encoding="utf-8"
            )
            marker = path / "executions.txt"
            (path / "conanfile.py").write_text(
                "from conan import ConanFile\n"
                "import os\n"
                "class Probe(ConanFile):\n"
                "    name = 'rstream'\n"
                "    version = '0.0.0'\n"
                "    def build(self):\n"
                "        with open(os.environ['RSTREAM_TEST_EXECUTIONS'], 'a') as output:\n"
                "            output.write('executed\\n')\n",
                encoding="utf-8",
            )
            environment = dict(
                os.environ, CONAN_HOME=str(cache), RSTREAM_TEST_EXECUTIONS=str(marker)
            )

            def create(flags):
                result = subprocess.run(
                    [sys.executable, "-c",
                     "import sys; from conan.cli.cli import main; main(sys.argv[1:])",
                     "create", str(path), "--no-remote", *flags],
                    env=environment,
                    capture_output=True,
                    text=True,
                    timeout=30,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                return marker.read_text(encoding="utf-8").splitlines()

            self.assertEqual(len(create(["--build=missing"])), 1)
            self.assertEqual(len(create(["--build=missing"])), 1)
            self.assertEqual(len(create(build_flags)), 2)

    def test_workflow_configuration_survives_cmake_option_defaults(self):
        workflow = (ROOT / ".github/workflows/conan.yml").read_text(encoding="utf-8")
        value = ast.literal_eval(
            re.search(r"extra_variables=(\{[^\n]+\})\"", workflow).group(1)
        )["RSTREAM_TEST_WINDOWS_PIPE_ASAN"]
        variable = parse_extra_variable(
            "tools.cmake.cmaketoolchain:extra_variables",
            "RSTREAM_TEST_WINDOWS_PIPE_ASAN",
            value,
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "toolchain.cmake").write_text(
                "set(RSTREAM_TEST_WINDOWS_PIPE_ASAN " + str(variable) + ")\n",
                encoding="utf-8",
            )
            tests = (ROOT / "cmake/tests.cmake").as_posix()
            (path / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.10)\n"
                "project(test_requirement NONE)\n"
                f'include("{tests}")\n'
                "if(NOT RSTREAM_TEST_WINDOWS_PIPE_ASAN)\n"
                '  message(FATAL_ERROR "Requested AddressSanitizer test was disabled")\n'
                "endif()\n",
                encoding="utf-8",
            )
            result = subprocess.run(
                [
                    "cmake",
                    "-S", str(path),
                    "-B", str(path / "build"),
                    "-DCMAKE_TOOLCHAIN_FILE=" + str(path / "toolchain.cmake"),
                ],
                capture_output=True,
                text=True,
                timeout=30,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def verify_report(self, cases):
        with tempfile.TemporaryDirectory() as directory:
            report = Path(directory) / "result.xml"
            report.write_text("<testsuite>" + cases + "</testsuite>", encoding="utf-8")
            RECIPE.ConanPackage.verify_windows_pipe_asan_result(report)

    def test_requires_actual_successful_execution(self):
        self.verify_report(f'<testcase name="{TEST_NAME}"/>')
        for cases in (
            '<testcase name="another-passing-test"/>',
            f'<testcase name="{TEST_NAME}"><skipped/></testcase>',
            f'<testcase name="{TEST_NAME}"><failure/></testcase>',
            f'<testcase name="{TEST_NAME}"><error/></testcase>',
            f'<testcase name="{TEST_NAME}"/><testcase name="{TEST_NAME}"/>',
        ):
            with self.subTest(cases=cases), self.assertRaises(ConanInvalidConfiguration):
                self.verify_report(cases)


if __name__ == "__main__":
    unittest.main()
