#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""Conan recipe package for TestPackageConan
"""

import os

from conan import ConanFile
import conan.tools.build
import conan.tools.cmake


class TestPackageConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    test_type = "explicit"

    def requirements(self):
        # Keep Boost as a direct consumer requirement: rstream must not impose its
        # distribution-build component pruning on the root dependency graph.
        self.requires(self.tested_reference_str)
        boost_ref = self.conf.get("user.rstream:test_boost_ref", default=None)
        self.requires(boost_ref or "boost/[>=1.83 <2]", force=bool(boost_ref))
        openssl_ref = self.conf.get("user.rstream:test_openssl_ref", default=None)
        if openssl_ref:
            self.requires(openssl_ref)

    def layout(self):
        conan.tools.cmake.cmake_layout(self, build_folder=os.getenv("TEST_BUILD_FOLDER", "build"))
        self.cpp.build.bindirs = ["bin"]

    def generate(self):
        cmake_toolchain = conan.tools.cmake.CMakeToolchain(self)
        cmake_toolchain.user_presets_path = os.path.join(os.getenv("TEST_BUILD_FOLDER", "build"), "CMakeUserPresets.json")
        dependency_options = self.dependencies["rstream"].options
        cmake_toolchain.variables["RSTREAM_TEST_EXPECT_SHARED_LIBS"] = (
            "ON" if str(dependency_options.shared).lower() == "true" else "OFF"
        )
        cmake_toolchain.variables["RSTREAM_TEST_EXPECT_STATIC_PLUGINS"] = (
            "ON" if str(dependency_options.static_plugins).lower() == "true" else "OFF"
        )
        cmake_toolchain.variables["RSTREAM_TEST_FULLY_STATIC"] = (
            "ON" if str(dependency_options.static_libstdcxx).lower() == "true" else "OFF"
        )
        cmake_toolchain.generate()
        cmake_deps = conan.tools.cmake.CMakeDeps(self)
        cmake_deps.generate()

    def build(self):
        cmake = conan.tools.cmake.CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        # can_run() keeps foreign targets disabled by default and honors an
        # explicit qualification environment (native static binary or emulator).
        if conan.tools.build.can_run(self):
            conan.tools.cmake.CMake(self).ctest(
                cli_args=["--parallel", "1", "--output-on-failure", "--no-tests=error"]
            )
