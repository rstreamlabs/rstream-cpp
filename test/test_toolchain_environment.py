#!/usr/bin/env python3

import importlib.util
import os
import unittest


REPOSITORY_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODULE_PATH = os.path.join(
    REPOSITORY_ROOT,
    "conan",
    "recipes",
    "yocto-toolchain",
    "toolchain_environment.py",
)
SPEC = importlib.util.spec_from_file_location("toolchain_environment", MODULE_PATH)
TOOLCHAIN_ENVIRONMENT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TOOLCHAIN_ENVIRONMENT)


class ToolchainEnvironmentTest(unittest.TestCase):
    def test_separates_compiler_executable_from_flags(self):
        environment = {
            "CC": "x86_64-poky-linux-gcc -m64 --sysroot=/sdk",
            "CFLAGS": "-O2",
            "CXX": "x86_64-poky-linux-g++ -m64 --sysroot=/sdk",
            "CXXFLAGS": "-O2 -DNDEBUG",
        }
        TOOLCHAIN_ENVIRONMENT.normalize_compiler_environment(environment)
        self.assertEqual(environment["CC"], "x86_64-poky-linux-gcc")
        self.assertEqual(environment["CFLAGS"], "-m64 --sysroot=/sdk -O2")
        self.assertEqual(environment["CXX"], "x86_64-poky-linux-g++")
        self.assertEqual(
            environment["CXXFLAGS"],
            "-m64 --sysroot=/sdk -O2 -DNDEBUG",
        )

    def test_preserves_quoted_flags(self):
        environment = {
            "CXX": "clang++ '-DPRODUCT_NAME=rstream sdk'",
            "CXXFLAGS": "'-DCHANNEL=dev build'",
        }
        TOOLCHAIN_ENVIRONMENT.normalize_compiler_environment(environment)
        self.assertEqual(environment["CXX"], "clang++")
        self.assertEqual(
            environment["CXXFLAGS"],
            "'-DPRODUCT_NAME=rstream sdk' '-DCHANNEL=dev build'",
        )

    def test_ignores_missing_compilers(self):
        environment = {"CFLAGS": "-O2"}
        TOOLCHAIN_ENVIRONMENT.normalize_compiler_environment(environment)
        self.assertEqual(environment, {"CFLAGS": "-O2"})

    def test_builds_conan_compiler_executables(self):
        self.assertEqual(
            TOOLCHAIN_ENVIRONMENT.compiler_executables(
                {"CC": "/sdk/bin/gcc", "CXX": "/sdk/bin/g++"}
            ),
            {"c": "/sdk/bin/gcc", "cpp": "/sdk/bin/g++"},
        )


if __name__ == "__main__":
    unittest.main()
