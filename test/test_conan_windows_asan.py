import ast
import importlib.util
import re
import subprocess
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
                "set(RSTREAM_TEST_WINDOWS_PIPE_ASAN " + variable + ")\n",
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
