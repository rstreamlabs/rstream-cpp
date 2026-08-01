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
        self.requires(self.tested_reference_str)

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
        cmake_toolchain.generate()
        cmake_deps = conan.tools.cmake.CMakeDeps(self)
        cmake_deps.generate()

    def build(self):
        cmake = conan.tools.cmake.CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        if not conan.tools.build.cross_building(self) and conan.tools.build.can_run(self):
            self.run(os.path.join(self.cpp.build.bindirs[0], "test_package"), env="conanrun")
